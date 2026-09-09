// ============================================================================
//  NovaDroid - main.cpp  Entry point, main window, message pump, click logic.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v3.h"
#include "v4.h"

// ---------------------------------------------------------------- forward
static void CreateNewInstanceFlow();
static void OpenContextMenu(int instIdx, POINT pt);
static void MaybeRunDiag();
static void HandleInstanceMenu(int instIdx, int cmd);

// perf option tables (shared logic with pages.cpp rendering)
struct PerfRowDef2 { int label; std::vector<std::wstring> opts; };
static std::vector<PerfRowDef2> PerfRowsS() {
    return {
        { S_PERF_CPU,   { L"1", L"2", L"4", L"6", L"8" } },
        { S_PERF_RAM,   { L"1024", L"2048", L"4096", L"6144", L"8192", L"16384" } },
        { S_PERF_DISKGB,{ L"8", L"16", L"32", L"64", L"128" } },
        { S_PERF_RES,   { L"1280x720", L"1600x900", L"1920x1080", L"800x1280", L"720x1280", L"1080x1920" } },
        { S_PERF_DPI,   { L"160", L"240", L"320", L"480" } },
        { S_PERF_FPS,   { L"30", L"60", L"90", L"120" } },
        { S_PERF_GPU,   { T(S_DLG_GPU_AUTO), T(S_DLG_GPU_STD), T(S_DLG_GPU_VIRT) } },
        { S_PERF_PRIO,  { T(S_PRIO_N), T(S_PRIO_H) } },
    };
}
static int PerfIndexS(const InstanceCfg& c, int row) {
    auto idxOf = [](const std::vector<std::wstring>& v, const std::wstring& s) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return (int)i;
        return 0;
    };
    auto rows = PerfRowsS();
    switch (row) {
        case 0: return idxOf(rows[0].opts, Fmt(L"%d", c.cpuCores));
        case 1: return idxOf(rows[1].opts, Fmt(L"%d", c.ramMb));
        case 2: return idxOf(rows[2].opts, Fmt(L"%d", c.diskGb));
        case 3: return idxOf(rows[3].opts, Fmt(L"%dx%d", c.resW, c.resH));
        case 4: return idxOf(rows[4].opts, Fmt(L"%d", c.dpi));
        case 5: return idxOf(rows[5].opts, Fmt(L"%d", c.fpsLimit));
        case 6: return c.gpuMode == L"virtio" ? 2 : c.gpuMode == L"std" ? 1 : 0;
        default: return c.priority == L"high" ? 1 : 0;
    }
}
static void PerfSetS(InstanceCfg& c, int row, int v, const std::vector<std::wstring>& opts) {
    v = v < 0 ? 0 : v >= (int)opts.size() ? (int)opts.size() - 1 : v;
    switch (row) {
        case 0: c.cpuCores = _wtoi(opts[v].c_str()); break;
        case 1: c.ramMb = _wtoi(opts[v].c_str()); break;
        case 2: c.diskGb = _wtoi(opts[v].c_str()); break;
        case 3: swscanf(opts[v].c_str(), L"%dx%d", &c.resW, &c.resH); break;
        case 4: c.dpi = _wtoi(opts[v].c_str()); break;
        case 5: c.fpsLimit = _wtoi(opts[v].c_str()); break;
        case 6: c.gpuMode = v == 2 ? L"virtio" : v == 1 ? L"std" : L"auto"; break;
        default: c.priority = v == 1 ? L"high" : L"normal"; break;
    }
}

// ---------------------------------------------------------------- toasts
static void DrawToasts(HDC dc, RECT rc) {
    long long now = (long long)GetTickCount64();
    int m = MulDiv(16, g_dpi, 96);
    int w = MulDiv(360, g_dpi, 96);
    int y = rc.bottom - m;
    for (int i = (int)g_toasts.size() - 1; i >= 0; --i) {
        Toast& t = g_toasts[i];
        if (now - t.t0 > 4000) { g_toasts.erase(g_toasts.begin() + i); continue; }
        int lines = 0;
        RECT calc = { 0, 0, w - m * 2, 10000 };
        TxtWrap(dc, calc, t.text, Fnt(F_SMALL), CL_TEXT, &lines);
        int h = MulDiv(16, g_dpi, 96) + lines * MulDiv(16, g_dpi, 96);
        y -= h + m / 2;
        RECT r = { rc.right - m - w, y, rc.right - m, y + h };
        FillRound(dc, r, CL_SURF2, 10);
        COLORREF bar = t.type == 0 ? CL_OK : t.type == 1 ? CL_WARN : t.type == 2 ? CL_ERR : Accent();
        RECT br = { r.left, y + 4, r.left + MulDiv(4, g_dpi, 96), y + h - 4 };
        HBRUSH b = CreateSolidBrush(bar);
        FillRect(dc, &br, b);
        DeleteObject(b);
        RECT tr = { r.left + m, y + MulDiv(8, g_dpi, 96), r.right - m, y + h - MulDiv(6, g_dpi, 96) };
        Txt(dc, tr, t.text, Fnt(F_SMALL), CL_TEXT, DT_LEFT | DT_WORDBREAK);
    }
}

// ---------------------------------------------------------------- click dispatch
static void Notify(int type, const wchar_t* msg) { UiNotify(msg, type); }

void OnClick(int id) {
    // nav (13 pages, TZ 2.0 §16.1)
    if (id >= ID_NAV0 && id < ID_NAV0 + 16) { g_page = (Pg)(id - ID_NAV0); UiInvalidate(); return; }
    if (id == 77) { g_page = Pg::Apk; UiInvalidate(); return; }
    // stage 2 widgets
    if (V2Click(id)) return;
    // stage 3 widgets
    if (V3Click(id)) return;
    // stage 4-7 widgets
    if (V4Click(id)) return;

    switch (id) {
        case ID_BTN_LAUNCH: {
            std::wstring id0 = SelInst();
            if (!id0.empty()) DoStart(id0);
            return;
        }
        case ID_BTN_STOPMAIN: {
            std::wstring id0 = SelInst();
            if (!id0.empty()) StopInstance(id0, false);
            return;
        }
        case ID_BTN_QUICK: {
            std::wstring id0 = SelInst();
            if (id0.empty()) { CreateNewInstanceFlow(); return; }
            Runtime* r = Rt(id0);
            if (r && (r->st == St::Stopped || r->st == St::StartError)) DoStart(id0);
            else UiNotify(T(S_MB_DISKBUSY), 1);
            return;
        }
        case ID_BTN_CREATE: CreateNewInstanceFlow(); return;
        case ID_BTN_IMPORT: {
            std::wstring f = PickFile(g_wnd, L"NovaDroid config (*.json)\0*.json\0", T(S_BTN_IMPORT), nullptr);
            if (!f.empty()) {
                std::wstring newId, err;
                if (ImportInstance(f, newId, err)) UiNotify(g_lang ? L"Instance imported" : L"Инстанс импортирован", 0);
                else UiNotify(L"Import error: " + err, 2);
                UiInvalidate();
            }
            return;
        }
        case ID_DIAG_RUN:
            if (!g_diagBusy.exchange(true)) {
                UiInvalidate();
                RunOpThread([] {
                    auto d = RunDiagnostics();
                    g_diag = d;
                    g_diagBusy = false;
                    g_diagDoneTick = (long long)GetTickCount64();
                    if (g_wnd) PostMessageW(g_wnd, WM_APP_DIAG, 0, 0);
                });
            }
            return;
        case ID_DIAG_COPY: {
            std::wstring rep = WinVerString() + L"\r\n";
            for (auto& d : g_diag) {
                rep += Fmt(L"[%s] %s: %s\r\n", d.level == 0 ? L"OK" : d.level == 1 ? L"WARN" :
                           d.level == 2 ? L"FAIL" : L"INFO", d.name.c_str(), d.msg.c_str());
            }
            CopyToClipboard(g_wnd, rep);
            Notify(0, T(S_MB_COPIED));
            return;
        }
        case ID_DIAG_LOGS: OpenInExplorer(g_p.logs); return;
        case ID_LOG_REFRESH: ReloadLogView(); UiInvalidate(); return;
        case ID_LOG_FOLDER: OpenInExplorer(g_p.logs); return;
        case ID_LOG_CLEAN: CleanupOldLogs(); UiNotify(L"OK", 0); ReloadLogView(); UiInvalidate(); return;
        case ID_LOG_COPY: {
            std::wstring all;
            for (auto& l : g_logLines) { all += l; all += L"\r\n"; }
            CopyToClipboard(g_wnd, all);
            Notify(0, T(S_MB_COPIED));
            return;
        }
        case ID_APK_ADD: {
            std::wstring f = PickFile(g_wnd, L"APK (*.apk)\0*.apk\0", T(S_BTN_ADD), nullptr);
            if (!f.empty()) {
                std::wstring dst = g_p.apks + L"\\" + BaseName(f);
                CopyFileW(f.c_str(), dst.c_str(), FALSE);
                UiInvalidate();
            }
            return;
        }
        case ID_APK_OPEN: OpenInExplorer(g_p.apks); return;
        case ID_FILE_PICK: {
            std::wstring id0 = SelInst();
            if (id0.empty()) return;
            std::wstring f = PickFile(g_wnd, L"All files\0*.*\0", T(S_FILE_PICK), nullptr);
            if (f.empty()) return;
            Runtime* r = Rt(id0);
            if (!r || r->busy) { Notify(1, T(S_MB_DISKBUSY)); return; }
            r->busy = true;
            UiInvalidate();
            RunOpThread([id0, f] {
                std::wstring err;
                bool ok = AdbPush(id0, f, err);
                Runtime* r2 = Rt(id0);
                if (r2) r2->busy = false;
                AppendFileOp(ok ? (L"push " + BaseName(f) + L" - OK") : (L"push " + BaseName(f) + L" - FAIL: " + err));
                UiNotify(ok ? (g_lang ? L"File sent" : L"Файл передан") : err, ok ? 0 : 2);
                UiInvalidate();
            });
            return;
        }
        case ID_FILE_PULL: {
            std::wstring id0 = SelInst();
            if (id0.empty()) return;
            InstanceCfg* c = Inst(id0);
            Runtime* r = Rt(id0);
            if (!c || !r || r->busy) { Notify(1, T(S_MB_DISKBUSY)); return; }
            r->busy = true;
            UiInvalidate();
            std::wstring dst = c->sharedFolderPath;
            RunOpThread([id0, dst] {
                std::wstring err;
                bool ok = AdbPullDownloads(id0, dst, err);
                Runtime* r2 = Rt(id0);
                if (r2) r2->busy = false;
                AppendFileOp(ok ? L"pull /sdcard/Download/ - OK" : (L"pull - FAIL: " + err));
                UiNotify(ok ? (g_lang ? L"Files fetched to shared folder" : L"Файлы скопированы в общую папку") : err,
                         ok ? 0 : 2);
                UiInvalidate();
            });
            return;
        }
        case ID_FILE_OPEN: {
            InstanceCfg* c = Inst(g_selId);
            if (c) OpenInExplorer(c->sharedFolderPath);
            return;
        }
        case ID_PERF_APPLY: {
            InstanceCfg* c = Inst(g_selId);
            if (c) { PersistInstance(*c); Notify(0, g_lang ? L"Saved" : L"Сохранено"); }
            return;
        }
        // settings
        case ID_SET_LANGDEC: case ID_SET_LANGINC:
            g_lang = g_lang ? 0 : 1;
            g_set.language = g_lang ? L"en" : L"ru";
            SaveSettings(); UiInvalidate(); return;
        case ID_SET_QEMU: {
            std::wstring f = PickFile(g_wnd, L"qemu-system-x86_64.exe\0qemu-system-x86_64.exe\0",
                                      T(S_SET_QEMU), nullptr);
            if (!f.empty()) { g_set.qemuPath = f; SaveSettings(); UiInvalidate(); }
            return;
        }
        case ID_SET_QEMURST: g_set.qemuPath.clear(); SaveSettings(); UiInvalidate(); return;
        case ID_SET_ADB: {
            std::wstring f = PickFile(g_wnd, L"adb.exe\0adb.exe\0", T(S_SET_ADB), nullptr);
            if (!f.empty()) { g_set.adbPath = f; SaveSettings(); UiInvalidate(); }
            return;
        }
        case ID_SET_IMAGE: {
            std::wstring f = PickFile(g_wnd, L"Android image\0*.iso;*.img;*.qcow2\0", T(S_SET_IMAGE), nullptr);
            if (!f.empty()) { g_set.defaultImagePath = f; SaveSettings(); UiInvalidate(); }
            return;
        }
        case ID_SET_DATA: {
            std::wstring f = PickFolder(g_wnd, T(S_SET_DATA));
            if (!f.empty()) { g_set.dataRoot = f; SaveSettings(); Notify(1, T(S_SET_RESTART)); }
            return;
        }
        case ID_SET_CHECK: g_set.checkOnStart = !g_set.checkOnStart; SaveSettings(); UiInvalidate(); return;
        case ID_SET_AUTORUN: g_set.autostartLast = !g_set.autostartLast; SaveSettings(); UiInvalidate(); return;
        case ID_SET_PORTDEC: if (g_set.baseAdbPort > 1024) { g_set.baseAdbPort--; SaveSettings(); UiInvalidate(); } return;
        case ID_SET_PORTINC: if (g_set.baseAdbPort < 50000) { g_set.baseAdbPort++; SaveSettings(); UiInvalidate(); } return;
        // ---- stage 2 settings (TZ 5.3, 14)
        case 3883: if (g_set.maxRunning > 1) { g_set.maxRunning--; SaveSettings(); UiInvalidate(); } return;
        case 3884: if (g_set.maxRunning < 16) { g_set.maxRunning++; SaveSettings(); UiInvalidate(); } return;
        case 3885: g_set.updateChannel = L"stable"; SaveSettings(); UiInvalidate(); return;
        case 3886: g_set.updateChannel = L"beta"; SaveSettings(); UiInvalidate(); return;
        case 3887: g_set.updateChannel = L"development"; SaveSettings(); UiInvalidate(); return;
        case 3888: UpdateCheckAsync(); return;
        case 3889: {
            std::wstring p;
            if (DiagExportToFile(p)) {
                UiNotify((g_lang ? L"Report saved: " : L"Отчёт сохранён: ") + p, 0);
                OpenInExplorer(g_p.logs);
            }
            return;
        }
    }

    // accents
    if (id >= ID_SET_ACC0 && id < ID_SET_ACC0 + 3) {
        const wchar_t* acc[3] = { L"#6C63FF", L"#2FB7C6", L"#9B59B6" };
        g_set.accentHex = acc[id - ID_SET_ACC0];
        SaveSettings(); UiInvalidate(); return;
    }
    // about links
    if (id >= ID_AB_LINK0 && id < ID_AB_LINK0 + 3) {
        const wchar_t* urls[3] = { L"https://www.qemu.org", L"https://www.android-x86.org",
                                   L"https://developer.android.com/tools/releases/platform-tools" };
        ShellOpen(urls[id - ID_AB_LINK0]);
        return;
    }
    // perf steppers
    if (id >= ID_PERF_DEC && id < ID_PERF_DEC + 16) {
        int row = (id - ID_PERF_DEC) / 2;
        bool inc = (id - ID_PERF_DEC) % 2 == 1;
        InstanceCfg* c = Inst(g_selId);
        if (!c) return;
        auto rows = PerfRowsS();
        if (row >= (int)rows.size()) return;
        int sel = PerfIndexS(*c, row);
        PerfSetS(*c, row, sel + (inc ? 1 : -1), rows[row].opts);
        PersistInstance(*c);
        UiInvalidate();
        return;
    }
    // perf profiles
    if (id >= ID_PERF_PROFILE && id < ID_PERF_PROFILE + 4) {
        InstanceCfg* c = Inst(g_selId);
        if (!c) return;
        int p = id - ID_PERF_PROFILE;
        if (p == 0) { c->cpuCores = 2; c->ramMb = 2048; c->resW = 1280; c->resH = 720; c->dpi = 240; c->fpsLimit = 30; }
        else if (p == 1) { c->cpuCores = 4; c->ramMb = 4096; c->resW = 1280; c->resH = 720; c->dpi = 240; c->fpsLimit = 60; }
        else if (p == 2) { c->cpuCores = 6; c->ramMb = 8192; c->resW = 1920; c->resH = 1080; c->dpi = 240; c->fpsLimit = 120; }
        c->priority = p == 2 ? L"high" : L"normal";
        PersistInstance(*c);
        UiInvalidate();
        return;
    }
    // log tabs
    if (id >= ID_LOG_TAB && id < ID_LOG_TAB + 4) { g_logTab = id - ID_LOG_TAB; ReloadLogView(); UiInvalidate(); return; }

    // toolbar (ADB controls + stage 3 window controls)
    if (id >= ID_TB0 && id < ID_TB0 + 12) {
        std::wstring id0 = SelInst();
        if (id0.empty()) return;
        int slot = id - ID_TB0;
        Runtime* r = Rt(id0);
        // stage 3 slots (TZ3 14.1): fullscreen / mouse capture / sound
        if (slot == 9) { V3FullscreenToggle(id0); return; }
        if (slot == 10) { V3CaptureToggle(id0); return; }
        if (slot == 11) {
            if (r && r->hProc) {
                r->audioMuted = !r->audioMuted;
                InstanceCfg* cc = Inst(id0);
                if (cc) V3AudioSetVolume(r->pid, cc->soundVolume, r->audioMuted);
            }
            return;
        }
        if (!r || !r->adbOk) { Notify(1, T(S_APK_NORUN)); return; }
        if (slot == 7) { // install apk
            std::wstring f = PickFile(g_wnd, L"APK (*.apk)\0*.apk\0", T(S_TB_APK), nullptr);
            if (!f.empty()) DoInstallApk(id0, f);
            return;
        }
        static const int keys[9] = { K_BACK, K_HOME, K_RECENT, K_VOLUP, K_VOLDN, -1, K_ROT, -2, -3 };
        int k = keys[slot];
        if (k == -1) { // screenshot
            r->busy = true; UiInvalidate();
            RunOpThread([id0] {
                std::wstring out, err;
                bool ok = AdbScreenshot(id0, out, err);
                Runtime* r2 = Rt(id0); if (r2) r2->busy = false;
                if (ok) UiNotify((g_lang ? L"Screenshot: " : L"Скриншот: ") + BaseName(out), 0);
                else UiNotify(L"Screenshot failed: " + err, 2);
                UiInvalidate();
            });
            return;
        }
        if (k == -2) return; // n/a
        if (k == -3) { StopInstance(id0, false); return; }
        RunOpThread([id0, k] {
            std::wstring err;
            if (!AdbKey(id0, k, err)) UiNotify(err, 2);
        });
        return;
    }

    // per-instance buttons
    if (id >= ID_INSTBTN && id < ID_INSTBTN + 400) {
        int rel = id - ID_INSTBTN;
        int idx = rel / 10, slot = rel % 10;
        if (idx < 0 || idx >= (int)g_insts.size()) return;
        std::wstring id0 = g_insts[idx].id;
        Runtime* r = Rt(id0);
        St st = r ? r->st : St::Stopped;
        if (slot == 0) {
            if (st == St::Running || st == St::Starting) StopInstance(id0, false);
            else DoStart(id0);
        } else if (slot == 2) {
            RestartInstance(id0);
        } else if (slot == 1) {
            POINT pt; GetCursorPos(&pt);
            OpenContextMenu(idx, pt);
        }
        return;
    }
    // card select
    if (id >= ID_CARD0 && id < ID_CARD0 + 100) {
        int idx = id - ID_CARD0;
        if (idx >= 0 && idx < (int)g_insts.size()) { g_selId = g_insts[idx].id; UiInvalidate(); }
        return;
    }
    // apk rows
    if (id >= ID_APK_ROW && id < ID_APK_ROW + 500) {
        int idx = id - ID_APK_ROW;
        auto apks = ListApks();
        if (idx < 0 || idx >= (int)apks.size()) return;
        std::wstring id0 = SelInst();
        Runtime* r = id0.empty() ? nullptr : Rt(id0);
        if (!id0.empty() && r && (r->st == St::Running) && r->adbOk) DoInstallApk(id0, apks[idx]);
        else Notify(1, T(S_APK_NORUN));
        return;
    }
}

// ---------------------------------------------------------------- context menu
static void OpenContextMenu(int instIdx, POINT pt) {
    InstanceCfg& c = g_insts[instIdx];
    Runtime* r = Rt(c.id);
    St st = r ? r->st : St::Stopped;
    HMENU m = CreatePopupMenu();
    int i = 1;
    auto add = [&](const wchar_t* s, bool en) { AppendMenuW(m, MF_STRING | (en ? MF_ENABLED : MF_GRAYED), i++, s); };
    add(T(S_CTX_START), st == St::Stopped || st == St::StartError);
    add(T(S_CTX_STOP), st == St::Running || st == St::Starting);
    add(T(S_CTX_RESTART), st == St::Running);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    add(T(S_CTX_CONFIG), true);
    add(T(S_CTX_RENAME), true);
    add(T(S_CTX_CLONE), st == St::Stopped);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    add(T(S_NAV_SNAPS), true);
    add(T(S_CTX_BACKUP), st == St::Stopped);
    add(T(S_CTX_EXPORT), st == St::Stopped);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    add(T(S_CTX_OPENF), true);
    add(T(S_CTX_DELETE), st == St::Stopped);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
    if (cmd > 0) HandleInstanceMenu(instIdx, cmd);
}

static void HandleInstanceMenu(int instIdx, int cmd) {
    std::wstring id = g_insts[instIdx].id;
    InstanceCfg* c = Inst(id);
    Runtime* r = Rt(id);
    St st = r ? r->st : St::Stopped;
    switch (cmd) {
        case 1: DoStart(id); break;
        case 2: StopInstance(id, false); break;
        case 3: RestartInstance(id); break;
        case 4: { // configure
            if (!c) break;
            InstanceCfg tmp = *c;
            if (DialogInstance(g_wnd, tmp, false)) {
                std::lock_guard<std::mutex> lk(g_mx);
                InstanceCfg* cc = Inst(id);
                if (cc) {
                    tmp.id = cc->id;
                    tmp.adbPort = cc->adbPort;
                    tmp.createdAt = cc->createdAt;
                    tmp.lastLaunchAt = cc->lastLaunchAt;
                    *cc = tmp;
                    PersistInstance(*cc);
                }
                UiInvalidate();
            }
            break;
        }
        case 5: { // rename
            std::wstring nn = DialogInput(g_wnd, T(S_CTX_RENAME), c ? c->name : L"");
            if (!nn.empty() && c) {
                std::wstring err;
                RenameInstance(id, nn, err);
                UiInvalidate();
            }
            break;
        }
        case 6: { // clone v2 (TZ 7.3)
            InstanceCfg tmp = c ? *c : InstanceCfg();
            if (DialogCloneOptions(g_wnd, tmp)) {
                bool linked = tmp.fullscreen;      // stash from dialog
                bool copyKm = tmp.wantShortcut;
                bool copyShared = tmp.soundEnabled;
                r->busy = true; UiInvalidate();
                RunOpThread([id, tmp, linked, copyKm, copyShared] {
                    std::wstring err, newId;
                    bool ok = CloneInstanceV2(id, tmp.name, linked, copyKm, copyShared, newId, err);
                    Runtime* r2 = Rt(id); if (r2) r2->busy = false;
                    UiNotify(ok ? (g_lang ? L"Instance cloned" : L"Инстанс клонирован") : err, ok ? 0 : 2);
                    UiInvalidate();
                });
            }
            break;
        }
        case 7: { // snapshots page
            g_selId = id;
            g_page = Pg::Snapshots;
            UiInvalidate();
            break;
        }
        case 8: { // backup v2 (TZ 9)
            int mb = MessageBoxW(g_wnd, T(S_BK_ASK), T(S_CTX_BACKUP), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (mb == IDCANCEL) break;
            bool snaps = mb == IDYES;
            r->busy = true; UiInvalidate();
            RunOpThread([id, snaps] {
                std::wstring err;
                bool ok = BackupInstanceV2(id, true, snaps, err);
                Runtime* r2 = Rt(id); if (r2) r2->busy = false;
                UiNotify(ok ? (g_lang ? L"Backup created" : L"Резервная копия создана") : err, ok ? 0 : 2);
                UiInvalidate();
            });
            break;
        }
        case 9: { // export .novadroid-backup (TZ 15.1)
            std::wstring dir = PickFolder(g_wnd, T(S_CTX_EXPORT));
            if (!dir.empty()) {
                r->busy = true; UiInvalidate();
                RunOpThread([id, dir] {
                    std::wstring err, dst = dir + L"\\NovaDroid-" + id + L".novadroid-backup";
                    bool ok = ExportBackupFile(id, dst, err);
                    Runtime* r2 = Rt(id); if (r2) r2->busy = false;
                    UiNotify(ok ? (g_lang ? L"Exported" : L"Экспортировано") : err, ok ? 0 : 2);
                    UiInvalidate();
                });
            }
            break;
        }
        case 10: OpenInExplorer(c ? c->Dir() : L""); break;
        case 11: { // delete with linked-clone guard (TZ 7.4, 20.2)
            auto kids = LinkedChildren(id);
            if (!kids.empty()) {
                int mb = MessageBoxW(g_wnd, T(S_MB_PARENTLINK), T(S_MB_DELT), MB_YESNO | MB_ICONWARNING);
                if (mb != IDYES) break;
            } else {
                int mb = MessageBoxW(g_wnd, T(S_MB_DEL), T(S_MB_DELT), MB_YESNO | MB_ICONWARNING);
                if (mb != IDYES) break;
            }
            bool keep = MessageBoxW(g_wnd,
                g_lang ? L"Save a backup copy before deletion?" : L"Сохранить резервную копию перед удалением?",
                T(S_MB_DELT), MB_YESNO | MB_ICONQUESTION) == IDYES;
            std::wstring err;
            if (!DeleteInstance(id, keep, err)) UiNotify(err, 2);
            else UiNotify(g_lang ? L"Instance deleted" : L"Инстанс удалён", 0);
            UiInvalidate();
            break;
        }
    }
}

// ---------------------------------------------------------------- create instance
static void CreateNewInstanceFlow() {
    InstanceCfg c;
    c.name = g_lang ? L"New instance" : L"Новый инстанс";
    c.imagePath = g_set.defaultImagePath;
    if (WizardRun(c)) {
        WizardFinishCreate(c);
        UiInvalidate();
    }
}

// ---------------------------------------------------------------- drag & drop
void HandleDrop(HDROP hd) {
    UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
    std::wstring runningId;
    for (auto& c : g_insts) {
        Runtime* r = Rt(c.id);
        if (r && r->st == St::Running && r->adbOk) { runningId = c.id; break; }
    }
    for (UINT i = 0; i < n; ++i) {
        wchar_t buf[MAX_PATH * 2] = {};
        DragQueryFileW(hd, i, buf, MAX_PATH * 2);
        std::wstring f = buf;
        if (FileExt(f) == L".apk") {
            if (!runningId.empty()) DoInstallApk(runningId, f);
            else {
                std::wstring dst = g_p.apks + L"\\" + BaseName(f);
                CopyFileW(f.c_str(), dst.c_str(), FALSE);
                UiNotify(g_lang ? L"APK added to library. No running instance to install."
                                : L"APK добавлен в библиотеку. Нет работающего инстанса для установки.", 1);
                UiInvalidate();
            }
        } else {
            InstanceCfg* c = Inst(g_selId);
            if (c && runningId == c->id) {
                RunOpThread([f, id = c->id] {
                    std::wstring err;
                    bool ok = AdbPush(id, f, err);
                    AppendFileOp(ok ? (L"push " + BaseName(f) + L" - OK") : (L"push - FAIL"));
                    UiNotify(ok ? (g_lang ? L"File sent" : L"Файл передан") : err, ok ? 0 : 2);
                });
            } else {
                std::wstring dst = c && c->sharedFolderEnabled ? c->sharedFolderPath : g_p.apks;
                MK(dst);
                CopyFileW(f.c_str(), (dst + L"\\" + BaseName(f)).c_str(), FALSE);
                UiNotify((g_lang ? L"Saved to " : L"Сохранено в ") + dst, 0);
                UiInvalidate();
            }
        }
    }
    DragFinish(hd);
}

// ---------------------------------------------------------------- window proc
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_CREATE: {
            DragAcceptFiles(h, TRUE);
            SetTimer(h, 1, 500, nullptr);
            BOOL on = TRUE;
            DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));
            int pref = DWMWCP_ROUND;
            DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
            g_dpi = WinDpi(h);
            MakeFonts();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = MulDiv(1020, g_dpi, 96);
            mmi->ptMinTrackSize.y = MulDiv(640, g_dpi, 96);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);
            HBRUSH bg = CreateSolidBrush(CL_BG);
            FillRect(mem, &rc, bg);
            DeleteObject(bg);
            g_wids.clear();
            DrawSidebar(mem, rc);
            switch (g_page) {
                case Pg::Home: PageHome(mem, rc); break;
                case Pg::Instances: PageInstances(mem, rc); break;
                case Pg::Images: PageImages(mem, rc); break;
                case Pg::Apk: PageApkV2(mem, rc); break;
                case Pg::Backups: PageBackups(mem, rc); break;
                case Pg::Snapshots: PageSnapshots(mem, rc); break;
                case Pg::Keys: PageKeysV2(mem, rc); break;
                case Pg::Perf: PagePerfV2(mem, rc); break;
                case Pg::Files: PageFiles(mem, rc); break;
                case Pg::Logs: PageLogs(mem, rc); break;
                case Pg::Diag: PageDiag(mem, rc); break;
                case Pg::Settings: PageSettings(mem, rc); break;
                case Pg::About: PageAbout(mem, rc); break;
                case Pg::Compat: PageCompat(mem, rc); break;
                case Pg::Sensors: PageSensors(mem, rc); break;
                case Pg::Advanced: PageAdvanced(mem, rc); break;
            }
            DrawToasts(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            g_mouse = p;
            int hit = -1;
            for (auto& w : g_wids)
                if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
            if (hit != g_hover) {
                g_hover = hit;
                InvalidateRect(h, nullptr, FALSE);
            }
            if (!g_tracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
                g_tracking = true;
            }
            SetCursor(hit > 0 ? LoadCursorW(nullptr, IDC_HAND) : LoadCursorW(nullptr, IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE: g_tracking = false; g_hover = -1; InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hit = -1;
            for (auto& w : g_wids)
                if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
            g_press = hit;
            if (hit > 0) InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hit = -1;
            for (auto& w : g_wids)
                if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
            int pr = g_press;
            g_press = -1;
            if (hit > 0 && hit == pr) OnClick(hit);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_RBUTTONUP: {
            // context menu on instance card
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            for (auto& w : g_wids)
                if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) {
                    if (w.id >= ID_CARD0 && w.id < ID_CARD0 + 100) {
                        int idx = w.id - ID_CARD0;
                        if (idx < (int)g_insts.size()) {
                            g_selId = g_insts[idx].id;
                            POINT sp = p;
                            ClientToScreen(h, &sp);
                            OpenContextMenu(idx, sp);
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    }
                    break;
                }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = (short)HIWORD(wp);
            int dir = delta > 0 ? -1 : 1;
            if (g_page == Pg::Logs) {
                g_logScroll += dir * MulDiv(60, g_dpi, 96);
                if (g_logScroll < 0) g_logScroll = 0;
            } else {
                int& sc = g_scroll[(int)g_page];
                sc += dir * MulDiv(90, g_dpi, 96);
                if (sc < 0) sc = 0;
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                g_spin++;
                V2TimerTick();
                V3TimerTick();          // stage 3: watchdog/overlay/audio/pkg
                V4TimerTick();          // stage 4-7: anti-lag/discord/stream/wizard
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_DROPFILES: HandleDrop((HDROP)wp); return 0;
        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            MakeFonts();
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_APP_STATUS:
        case WM_APP_DIAG:
        case WM_APP_ADBOK: {
            InvalidateRect(h, nullptr, FALSE);
            std::wstring* idp = (std::wstring*)lp;
            if (idp) {
                std::wstring id0 = *idp;
                V3ApplyFpsLimitAsync(id0);          // TZ3 7.2 fps limiter
                if (m == WM_APP_ADBOK) V4OnAdbUp(id0);   // TZ-user: base apps auto-install
                std::lock_guard<std::mutex> lk(g_mx);
                Runtime* rr = Rt(id0);
                InstanceCfg* cc = Inst(id0);
                if (rr && cc && rr->qhWnd == nullptr) rr->qhWnd = V3FindQemuWindow(rr->pid);
            }
            delete idp;
            return 0;
        }
        case WM_APP_OPDONE:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_APP_CRASH:
            V3RecoveryPump(h);                      // stage 3 recovery dialog (TZ3 13.3)
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_APP_PKG:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_APP + 9:            // first-run wizard (TZ5 3.2)
            V4FirstRunWizard(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_APP_NOTIFY: {
            Toast t;
            t.text = *(std::wstring*)lp;
            t.type = (int)wp;
            t.t0 = (long long)GetTickCount64();
            delete (std::wstring*)lp;
            g_toasts.push_back(t);
            if (g_toasts.size() > 4) g_toasts.erase(g_toasts.begin());
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_APP_QEXIT: {
            QExitMsg* q = (QExitMsg*)lp;
            std::wstring id = q->id;
            HANDLE hp = q->hp;
            DWORD qpid = 0;
            { std::lock_guard<std::mutex> lk(g_mx); Runtime* rr = Rt(id); if (rr) qpid = rr->pid; }
            V3OnQemuExit(id, hp, qpid);     // stage 3 crash analysis (TZ3 13)
            delete q;
            {
                std::lock_guard<std::mutex> lk(g_mx);
                Runtime* r = Rt(id);
                if (r && r->hProc == hp) {
                    CloseHandle(hp);
                    r->hProc = nullptr;
                    r->pid = 0;
                    bool wasStarting = (r->st == St::Starting);
                    r->adbOk = false;
                    if (r->st != St::Stopped) r->st = wasStarting ? St::StartError : St::Stopped;
                }
            }
            LogW(L"qemu", L"process exited: %s", id.c_str());
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE: {
            bool anyRunning = false;
            {
                std::lock_guard<std::mutex> lk(g_mx);
                for (auto& kv : g_rt) if (kv.second.hProc && ProcAlive(kv.second.hProc)) anyRunning = true;
            }
            if (anyRunning) {
                int mb = MessageBoxW(h, T(S_MB_STOPQ), T(S_MB_STOPT), MB_YESNO | MB_ICONQUESTION);
                if (mb != IDYES) return 0;
                for (auto& kv : g_rt)
                    if (kv.second.hProc && ProcAlive(kv.second.hProc)) {
                        TerminateProcess(kv.second.hProc, 0);
                        CloseHandle(kv.second.hProc);
                        kv.second.hProc = nullptr;
                        kv.second.st = St::Stopped;
                    }
            }
            DestroyWindow(h);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(h, 1);
            V2Shutdown();
            V3Shutdown();
            V4Shutdown();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------------------------------------------------------------- run + entry
static void MaybeRunDiag() {
    if (!g_set.checkOnStart) return;
    if (!g_diagBusy.exchange(true)) {
        RunOpThread([] {
            g_diag = RunDiagnostics();
            g_diagBusy = false;
            if (g_wnd) PostMessageW(g_wnd, WM_APP_DIAG, 0, 0);
        });
    }
}

int WinDpi(HWND w) {
    typedef int (WINAPI *Fn)(HWND);
    static Fn f = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return f ? f(w) : 96;
}

int UiRun(HINSTANCE hInst, int nCmdShow) {
    g_hi = hInst;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"NovaDroidMain";
    if (!RegisterClassExW(&wc)) return 1;

    int W = MulDiv(1280, g_dpi, 96), H = MulDiv(800, g_dpi, 96);
    RECT wr = { 0, 0, W, H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindowExW(0, L"NovaDroidMain", T(S_APP_NAME),
                             WS_OVERLAPPEDWINDOW,
                             (sw - (wr.right - wr.left)) / 2, (sh - (wr.bottom - wr.top)) / 2,
                             wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, hInst, nullptr);
    g_wnd = h;
    if (!h) return 1;
    ShowWindow(h, nCmdShow);
    UpdateWindow(h);

    MaybeRunDiag();
    ReloadLogView();

    // parse --launch <id>
    {
        std::wstring cmd = GetCommandLineW();
        size_t p = cmd.find(L"--launch");
        if (p != std::wstring::npos) {
            size_t s = cmd.find_first_not_of(L" \t", p + 8);
            size_t e = cmd.find_first_of(L" \t\"", s);
            std::wstring id = cmd.substr(s, e - s);
            if (Inst(id)) DoStart(id);
        } else if (g_set.autostartLast && !g_insts.empty()) {
            std::wstring last = g_insts[0].id;
            for (auto& c : g_insts) if (c.lastLaunchAt > Inst(last)->lastLaunchAt) last = c.id;
            DoStart(last);
        }
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    g_startTick = GetTickCount();

    InitPaths();
    LoadSettings();
    InitPaths();                       // re-resolve with dataRoot from settings
    LoadInstances();

    // first run: create default instance "Тестовый Android" (TZ 17)
    if (g_insts.empty()) {
        InstanceCfg c;
        c.name = g_lang ? L"Test Android" : L"Тестовый Android";
        c.imagePath = g_set.defaultImagePath;
        std::wstring err;
        CreateInstance(c, err);
        LogW(L"init", L"default instance created");
    }
    V2Init();
    V3Init();
    V4Init();
    CleanupOldLogs();
    LogW(L"init", L"NovaDroid Launcher 0.4.0 started, lang=%d", g_lang);

    g_lang = (g_set.language == L"en") ? 1 : 0;
    int rc = UiRun(hInst, nCmdShow);
    SaveSettings();
    CoUninitialize();
    return rc;
}
