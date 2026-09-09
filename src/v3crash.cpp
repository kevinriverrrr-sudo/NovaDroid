// ============================================================================
//  NovaDroid - v3crash.cpp  Stage 3 (TZ3 13): crash recovery - unexpected QEMU
//  exit analysis, hung-VM watchdog (SendMessageTimeout), recovery dialog with
//  restart/snapshot/logs/report actions, ZIP crash reports via bsdtar.
// ============================================================================
#include "v3.h"
#include "ui.h"

struct CrashEntry { std::wstring id; std::wstring reason; };
static std::mutex g_crMx;
static std::vector<CrashEntry> g_crQueue;

// ---------------------------------------------------------------- exit analysis
void V3OnQemuExit(const std::wstring& id, HANDLE hp, DWORD pid) {
    DWORD code = 0;
    GetExitCodeProcess(hp, &code);
    long long uptime = 0;
    bool unexpected = false;
    St prev = St::Stopped;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        Runtime* r = Rt(id);
        if (r) {
            uptime = (long long)GetTickCount64() - r->startedAtMs;
            prev = r->st;
            r->lastExitCode = code;
            // exit code 0 = clean (user closed the qemu window)
            // non-zero while starting/running = crash (TZ3 13.2)
            unexpected = (code != 0) && (r->st == St::Running || r->st == St::Starting ||
                                         r->st == St::Paused);
            r->crash = unexpected;
            r->hung = false;
            r->hangTicks = 0;
            if (unexpected) r->st = St::Recovery;
        }
    }
    LogW(L"crash", L"qemu exit %s code=%lu uptime=%lldms unexpected=%d",
         id.c_str(), (unsigned long)code, uptime, unexpected ? 1 : 0);
    ILog(id, L"launcher", unexpected
        ? Fmt(L"CRASH: QEMU exited unexpectedly (code %lu) after %lld s",
              (unsigned long)code, uptime / 1000)
        : Fmt(L"QEMU exited normally (code %lu)", (unsigned long)code));

    // free the ADB endpoint (TZ3 13.3) - fire and forget
    std::wstring adb = AdbExe();
    if (!adb.empty()) {
        std::wstring port;
        {
            std::lock_guard<std::mutex> lk(g_mx);
            InstanceCfg* c = Inst(id);
            if (c) port = std::to_wstring(c->adbPort);
        }
        if (!port.empty())
            RunCapture(adb, L"disconnect 127.0.0.1:" + port, L"", 4000, nullptr, nullptr, nullptr);
    }

    if (unexpected) {
        std::lock_guard<std::mutex> lk(g_crMx);
        g_crQueue.push_back({ id, Fmt(L"QEMU завершился аварийно. Код выхода: %lu. Время работы: %lld с.",
                                      (unsigned long)code, uptime / 1000) });
        if (g_wnd) PostMessageW(g_wnd, WM_APP_CRASH, 0, 0);
    }
    (void)prev; (void)pid;
}

// ---------------------------------------------------------------- hung watchdog (TZ3 13.2)
void V3WatchdogTick() {
    struct Probe { std::wstring id; DWORD pid; HWND h; long long uptime; };
    static long long tickNo = 0;
    std::vector<Probe> probes;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        for (auto& kv : g_rt) {
            if (!kv.second.hProc || !ProcAlive(kv.second.hProc)) continue;
            if (kv.second.st != St::Running && kv.second.st != St::Starting &&
                kv.second.st != St::Paused) continue;
            HWND h = kv.second.qhWnd;
            if (!h || !IsWindow(h)) {
                // qemu may not have created its window yet (early boot)
                long long up = (long long)GetTickCount64() - kv.second.startedAtMs;
                if (up > 120000) {
                    kv.second.hangTicks++;
                    if (kv.second.hangTicks == 45) {
                        std::lock_guard<std::mutex> lk2(g_crMx);
                        g_crQueue.push_back({ kv.first,
                            L"У инстанса не появилось окно более 45 секунд. Возможно, образ не загружается." });
                        if (g_wnd) PostMessageW(g_wnd, WM_APP_CRASH, 0, 0);
                    }
                }
                continue;
            }
            long long up = (long long)GetTickCount64() - kv.second.startedAtMs;
            probes.push_back({ kv.first, kv.second.pid, h, up });
        }
    }
    bool anyHung = false;
    for (auto& p : probes) {
        // grace period: first seconds after window creation
        if (p.uptime < 15000) continue;
        DWORD_PTR res = 0;
        bool responsive = SendMessageTimeoutW(p.h, WM_NULL, 0, 0,
            SMTO_BLOCK | SMTO_ABORTIFHUNG, 2000, &res) != 0;
        bool becameHung = false;
        {
            std::lock_guard<std::mutex> lk(g_mx);
            Runtime* r = Rt(p.id);
            if (!r || !r->hProc) continue;
            if (responsive) {
                r->hangTicks = 0;
                if (r->hung) { r->hung = false; ILog(p.id, L"launcher", L"VM responded again"); }
            } else {
                r->hangTicks++;
                if (r->hangTicks == 15 && !r->hung) {      // ~30 s unresponsive (TZ3 13.2)
                    r->hung = true;
                    becameHung = true;
                    ILog(p.id, L"launcher", L"VM window is not responding for ~30 s");
                }
            }
            anyHung = anyHung || r->hung;
        }
        if (becameHung) {
            std::lock_guard<std::mutex> lk2(g_crMx);
            g_crQueue.push_back({ p.id,
                L"Виртуальная машина не отвечает более 30 секунд. Возможно зависание игры или гостевой ОС." });
            if (g_wnd) PostMessageW(g_wnd, WM_APP_CRASH, 0, 0);
        }
    }
    (void)anyHung; (void)tickNo;
}

// ---------------------------------------------------------------- crash report ZIP
std::wstring V3SaveCrashReport(const std::wstring& id) {
    std::wstring dir = g_p.dataRoot + L"\\crashes";
    MK(dir);
    std::wstring stamp = NowFileStamp();
    std::wstring zip = dir + L"\\crash-" + id + L"-" + stamp + L".zip";
    // bundle: instance logs + config + launcher log + diag snapshot
    std::wstring tmpDiag = dir + L"\\_diag.txt";
    {
        std::wstring d = Fmt(L"NovaDroid crash report\r\ninstance: %s\r\ntime: %s\r\n\r\n",
                             id.c_str(), NowIso().c_str());
        auto v2d = DiagReportText();
        d += v2d;
        WriteText(tmpDiag, d);
    }
    std::wstring instDir = Inst(id) ? Inst(id)->Dir() : L"";
    std::wstring tar = L"C:\\Windows\\System32\\tar.exe";
    if (!FE(tar)) tar = L"tar";
    std::wstring args = L"--format zip -a -cf " + Qn(zip) + L" -C " + Qn(instDir) +
                        L" logs config.json -C " + Qn(g_p.dataRoot + L"\\logs") + L" .";
    DWORD ec = 0; std::string so, se;
    RunCapture(tar, args, dir, 60000, &ec, &so, &se);
    DeleteFileW(tmpDiag.c_str());
    if (FE(zip)) {
        LogW(L"crash", L"report saved: %s", zip.c_str());
        return zip;
    }
    return L"";
}

// ---------------------------------------------------------------- recovery dialog (TZ3 13.3)
struct RecDlg {
    std::wstring id, reason;
    bool done = false;
};
static RecDlg g_rec;

static LRESULT CALLBACK RecProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        RecDlg* rd = (RecDlg*)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)rd);
        int scale = WinDpi(h) / 96;
        HFONT f = Fnt(F_BODY), fb = Fnt(F_H2);
        auto mk = [&](const wchar_t* t, int y, int id, bool big) {
            HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                24 * scale, y, 352 * scale, 40 * scale, h, (HMENU)(INT_PTR)id, g_hi, nullptr);
            SendMessageW(b, WM_SETFONT, (WPARAM)(big ? fb : f), TRUE);
        };
        HWND ttl = CreateWindowExW(0, L"STATIC", T(S3_REC_TITLE), WS_CHILD | WS_VISIBLE,
            24 * scale, 16 * scale, 352 * scale, 28 * scale, h, nullptr, g_hi, nullptr);
        SendMessageW(ttl, WM_SETFONT, (WPARAM)fb, TRUE);
        std::wstring txt = rd->reason + L"\r\n\r\n" + std::wstring(T(S3_REC_QUESTION));
        HWND st = CreateWindowExW(0, L"STATIC", txt.c_str(), WS_CHILD | WS_VISIBLE,
            24 * scale, 52 * scale, 352 * scale, 90 * scale, h, nullptr, g_hi, nullptr);
        SendMessageW(st, WM_SETFONT, (WPARAM)f, TRUE);
        mk(T(S_BTN_RESTART), 150 * scale, 1001, true);   // запустить снова
        mk(T(S3_REC_SNAP),   198 * scale, 1002, true);   // восстановить последний снапшот
        mk(T(S3_REC_LOGS),   246 * scale, 1003, true);   // открыть логи
        mk(T(S3_REC_REPORT), 294 * scale, 1004, true);   // сохранить отчёт
        mk(T(S_BTN_CANCEL),  346 * scale, 1000, false);
        return 0;
    }
    case WM_COMMAND: {
        RecDlg* rd = (RecDlg*)GetWindowLongPtrW(h, GWLP_USERDATA);
        int id = LOWORD(wp);
        DestroyWindow(h);
        if (id == 1001) {                 // restart
            DoStart(rd->id);
        } else if (id == 1002) {          // restore latest snapshot
            auto snaps = LoadSnapshots(rd->id);
            const Snapshot* best = nullptr;
            for (auto& s : snaps)
                if (!best || s.createdAt > best->createdAt) best = &s;
            if (best) {
                std::wstring err;
                if (SnapshotRestore(rd->id, best->id, err))
                    UiNotify(Fmt(L"%s «%s»", T(S_MB_RESTORED), best->displayName.c_str()), 0);
                else
                    UiNotify(err, 2);
            } else {
                UiNotify(T(S_SNAP_EMPTY), 1);
            }
        } else if (id == 1003) {          // open logs
            std::lock_guard<std::mutex> lk(g_mx);
            InstanceCfg* c = Inst(rd->id);
            if (c) OpenInExplorer(c->Dir() + L"\\logs");
        } else if (id == 1004) {          // save report
            std::wstring zip = V3SaveCrashReport(rd->id);
            if (!zip.empty()) {
                UiNotify(Fmt(L"%s: %s", T(S3_REC_SAVED), zip.c_str()), 0);
                OpenInExplorer(g_p.dataRoot + L"\\crashes");
            } else UiNotify(T(S_ERR2), 2);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, CL_TEXT); SetBkColor((HDC)wp, CL_SURF);
        return (LRESULT)CreateSolidBrush(CL_SURF);
    case WM_CTLCOLORBTN:
        return (LRESULT)CreateSolidBrush(CL_SURF);
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool V3RecoveryDialog(HWND parent, const std::wstring& id, const std::wstring& reason) {
    static bool cls = false;
    if (!cls) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = RecProc; wc.hInstance = g_hi;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
        wc.lpszClassName = L"NovaDroidRecovery"; RegisterClassExW(&wc); cls = true;
    }
    g_rec.id = id; g_rec.reason = reason;
    int scale = WinDpi(parent) / 96;
    int W = 400 * scale, H = 430 * scale;
    RECT pr; GetWindowRect(parent, &pr);
    HWND h = CreateWindowExW(0, L"NovaDroidRecovery", T(S3_REC_TITLE),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        pr.left + ((pr.right - pr.left) - W) / 2, pr.top + ((pr.bottom - pr.top) - H) / 2,
        W, H, parent, nullptr, g_hi, &g_rec);
    BOOL on = TRUE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));
    EnableWindow(parent, FALSE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsWindow(h)) break;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    return g_rec.done;
}

void V3PendingCrash(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(g_crMx);
    g_crQueue.push_back({ id, L"" });
    if (g_wnd) PostMessageW(g_wnd, WM_APP_CRASH, 0, 0);
}

void V3RecoveryPump(HWND parent) {
    std::vector<CrashEntry> q;
    {
        std::lock_guard<std::mutex> lk(g_crMx);
        q.swap(g_crQueue);
    }
    for (auto& e : q) {
        std::wstring reason = e.reason;
        if (reason.empty()) {
            std::lock_guard<std::mutex> lk(g_mx);
            Runtime* r = Rt(e.id);
            if (r && r->hung) reason = L"Виртуальная машина не отвечает. QEMU можно завершить принудительно.";
            else reason = Fmt(L"Инстанс находится в состоянии сбоя (код выхода %lu).",
                              (unsigned long)(r ? r->lastExitCode : 0));
        }
        V3RecoveryDialog(parent, e.id, reason);
    }
}

// terminate hung qemu (called from recovery dialog context outside)
void V3TerminateHung(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(g_mx);
    Runtime* r = Rt(id);
    if (r && r->hProc) {
        TerminateProcess(r->hProc, 1);
    }
}
