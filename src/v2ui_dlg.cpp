// ============================================================================
//  NovaDroid - v2ui_dlg.cpp  Stage 2 dialogs: modal framework, key capture,
//  snapshot add, clone options, APK multi-install, click dispatcher.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v2ui_common.h"

// ---------------------------------------------------------------- v2 ui globals
std::wstring g_keymapSel;      // selected keymap profile id
std::wstring g_perfSel;        // perf page instance selector
std::vector<ApkInstallResult> g_apkResults;
int g_hover2 = -1, g_press2 = -1;   // dialog-local hover/press

// ================================================================ modal framework
struct Dlg {
    HWND h = nullptr;
    std::function<void(HDC, RECT)> draw;
    std::function<bool(int)> click;         // return true = close
    std::function<bool(MSG*)> key;          // return true = consumed
    bool done = false;
};
static Dlg* g_curDlg = nullptr;

static LRESULT CALLBACK DlgWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Dlg* d = g_curDlg;
    switch (m) {
        case WM_PAINT: {
            if (!d) break;
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);
            HBRUSH bg = CreateSolidBrush(CL_SURF);
            FillRect(mem, &rc, bg);
            DeleteObject(bg);
            g_wids.clear();
            g_hover = g_hover2; g_press = g_press2;
            if (d->draw) d->draw(mem, rc);
            g_hover2 = g_hover; g_press2 = g_press;
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!d) break;
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hit = -1;
            for (auto& w : g_wids)
                if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
            if (hit != g_hover2) { g_hover2 = hit; InvalidateRect(h, nullptr, FALSE); }
            SetCursor(hit > 0 ? LoadCursorW(nullptr, IDC_HAND) : LoadCursorW(nullptr, IDC_ARROW));
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!d) break;
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hit = -1;
            for (auto& w : g_wids)
                if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
            bool close = false;
            if (hit > 0 && d->click) close = d->click(hit);
            if (close) { d->done = true; PostMessageW(h, WM_CLOSE, 0, 0); }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR: {
            if (d && d->key) {
                MSG mm = { h, m, wp, lp };
                if (d->key(&mm)) return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (d) d->done = true;
            DestroyWindow(h);
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void V2ModalRun(HWND parent, int w, int h,
                     std::function<void(HDC, RECT)> draw,
                     std::function<bool(int)> click,
                     std::function<bool(MSG*)> key) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = DlgWndProc;
        wc.hInstance = g_hi;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"NovaDlgV2";
        RegisterClassExW(&wc);
        registered = true;
    }
    Dlg d;
    d.draw = draw; d.click = click; d.key = key;
    g_curDlg = &d;
    g_hover2 = g_press2 = -1;
    // fonts might not exist at dialog dpi; reuse main
    RECT pr; GetWindowRect(parent, &pr);
    int px = pr.left + ((pr.right - pr.left) - w) / 2;
    int py = pr.top + ((pr.bottom - pr.top) - h) / 2;
    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, L"NovaDlgV2", T(S_APP_NAME),
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              px, py, w, h, parent, nullptr, g_hi, nullptr);
    d.h = hw;
    EnableWindow(parent, FALSE);
    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);
    MSG msg;
    while (!d.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    g_curDlg = nullptr;
    UiInvalidate();
}

// ================================================================ key capture
static int g_capturedVk = 0;
static bool KeyCapDraw(HDC dc, RECT rc) {
    Txt(dc, rc, T(S_KM_PRESSKEY), Fnt(F_TITLE), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return false;
}
static bool KeyCapKey(MSG* m) {
    if (m->message == WM_KEYDOWN) {
        g_capturedVk = (int)m->wParam;
        return true;
    }
    return false;
}
bool DialogKeyCapture(HWND parent, int& vkOut) {
    g_capturedVk = 0;
    V2ModalRun(parent, MulDiv(360, g_dpi, 96), MulDiv(160, g_dpi, 96),
             KeyCapDraw, nullptr, KeyCapKey);
    if (g_capturedVk) { vkOut = g_capturedVk; return true; }
    return false;
}

// ================================================================ snapshot add
bool DialogSnapshotAdd(HWND parent, std::wstring& name, std::wstring& desc, bool& protect) {
    TextInput n, dsc;
    n.text = NowFileStamp();
    bool prot = true;
    V2ModalRun(parent, MulDiv(460, g_dpi, 96), MulDiv(300, g_dpi, 96),
        [&](HDC dc, RECT rc) {
            int m = MulDiv(20, g_dpi, 96);
            RECT t = { m, m, rc.right - m, m + MulDiv(30, g_dpi, 96) };
            Txt(dc, t, T(S_SNAP_CREATE), Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            int y = m + MulDiv(44, g_dpi, 96);
            RECT r1 = { m, y, rc.right - m, y + MulDiv(48, g_dpi, 96) };
            n.Draw(dc, r1, T(S_SNAP_NAME), 9001);
            y += MulDiv(56, g_dpi, 96);
            RECT r2 = { m, y, rc.right - m, y + MulDiv(48, g_dpi, 96) };
            dsc.Draw(dc, r2, T(S_SNAP_DESC), 9002);
            y += MulDiv(60, g_dpi, 96);
            CheckBoxV2(dc, { m, y, rc.right - m, y + MulDiv(22, g_dpi, 96) }, prot, T(S_SNAP_PROT2), 9003);
            int bh = MulDiv(38, g_dpi, 96);
            int by = rc.bottom - m - bh;
            RECT ok = { rc.right - m - MulDiv(140, g_dpi, 96), by, rc.right - m, by + bh };
            Btn(dc, 9004, ok, T(S_BTN_SAVE), BSTY_PRIMARY, !n.text.empty());
            RECT cc = { ok.left - m / 2 - MulDiv(120, g_dpi, 96), by, ok.left - m / 2, by + bh };
            Btn(dc, 9005, cc, T(S_BTN_CANCEL), BSTY_GHOST);
        },
        [&](int id) {
            n.Click(id, 9001); dsc.Click(id, 9002);
            if (id == 9003) prot = !prot;
            if (id == 9004 && !n.text.empty()) {
                name = n.text; desc = dsc.text; protect = prot;
                return true;
            }
            return id == 9005;
        },
        [&](MSG* m) {
            bool a = n.Key(m), b = dsc.Key(m);
            return a || b;
        });
    return !name.empty();
}

// ================================================================ clone options
bool DialogCloneOptions(HWND parent, InstanceCfg& c) {   // TZ 7.3
    TextInput n;
    n.text = c.name + (g_lang ? L" (copy)" : L" (копия)");
    bool linked = false, copyKm = true, copyShared = true;
    bool confirmed = false;
    V2ModalRun(parent, MulDiv(520, g_dpi, 96), MulDiv(430, g_dpi, 96),
        [&](HDC dc, RECT rc) {
            int m = MulDiv(20, g_dpi, 96);
            RECT t = { m, m, rc.right - m, m + MulDiv(30, g_dpi, 96) };
            Txt(dc, t, T(S_CLONE_TITLE), Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            int y = m + MulDiv(44, g_dpi, 96);
            RECT r1 = { m, y, rc.right - m, y + MulDiv(48, g_dpi, 96) };
            n.Draw(dc, r1, T(S_CTX_RENAME), 9100);
            y += MulDiv(56, g_dpi, 96);
            // clone type cards
            int cw = (rc.right - 2 * m - m / 2) / 2;
            RECT full = { m, y, m + cw, y + MulDiv(84, g_dpi, 96) };
            FillRound(dc, full, !linked ? Mix(Accent(), CL_SURF, 75) : CL_SURF2, 12);
            FrameRound(dc, full, !linked ? Accent() : CL_BORDER, 12);
            Reg(9101, full, K_BTN);
            Txt(dc, { full.left + 12, full.top + 8, full.right - 12, full.top + 30 }, T(S_CLONE_FULL),
                Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            Txt(dc, { full.left + 12, full.top + 32, full.right - 12, full.bottom - 8 }, T(S_CLONE_FULLD),
                Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
            RECT lnk = { full.right + m / 2, y, full.right + m / 2 + cw, y + MulDiv(84, g_dpi, 96) };
            FillRound(dc, lnk, linked ? Mix(Accent(), CL_SURF, 75) : CL_SURF2, 12);
            FrameRound(dc, lnk, linked ? Accent() : CL_BORDER, 12);
            Reg(9102, lnk, K_BTN);
            Txt(dc, { lnk.left + 12, lnk.top + 8, lnk.right - 12, lnk.top + 30 }, T(S_CLONE_LINKED),
                Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            Txt(dc, { lnk.left + 12, lnk.top + 32, lnk.right - 12, lnk.bottom - 8 }, T(S_CLONE_LINKEDD),
                Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
            y += MulDiv(96, g_dpi, 96);
            CheckBoxV2(dc, { m, y, rc.right - m, y + MulDiv(22, g_dpi, 96) }, copyKm, T(S_CLONE_KEYMAPS), 9103);
            y += MulDiv(30, g_dpi, 96);
            CheckBoxV2(dc, { m, y, rc.right - m, y + MulDiv(22, g_dpi, 96) }, copyShared, T(S_CLONE_SHAREDC), 9104);
            int bh = MulDiv(38, g_dpi, 96);
            int by = rc.bottom - m - bh;
            RECT ok = { rc.right - m - MulDiv(150, g_dpi, 96), by, rc.right - m, by + bh };
            Btn(dc, 9105, ok, T(S_BTN_CLONE), BSTY_PRIMARY, !n.text.empty());
            RECT cc = { ok.left - m / 2 - MulDiv(120, g_dpi, 96), by, ok.left - m / 2, by + bh };
            Btn(dc, 9106, cc, T(S_BTN_CANCEL), BSTY_GHOST);
        },
        [&](int id) {
            n.Click(id, 9100);
            if (id == 9101) linked = false;
            if (id == 9102) linked = true;
            if (id == 9103) copyKm = !copyKm;
            if (id == 9104) copyShared = !copyShared;
            if (id == 9105 && !n.text.empty()) {
                c.name = n.text;
                // stash flags in unused fields for the caller
                c.wantShortcut = copyKm;
                c.soundEnabled = copyShared;
                c.fullscreen = linked;
                confirmed = true;
                return true;
            }
            return id == 9106;
        },
        [&](MSG* m) { return n.Key(m); });
    return confirmed;
}

// ================================================================ APK multi-install
static bool g_snapFirst = false;
static std::vector<bool> g_miTg;
static std::vector<ApkInstallResult>* g_resOut = nullptr;
static bool g_miRunning = false;
static bool g_miDone = false;

bool DialogApkMultiInstall(const std::wstring& apkId) {   // TZ 10.4
    ApkEntry* a = ApkById(apkId);
    if (!a) return false;
    g_miTg.assign(g_insts.size(), false);
    g_resOut = &g_apkResults;
    g_apkResults.clear();
    g_miRunning = false;
    g_miDone = false;
    g_snapFirst = false;
    V2ModalRun(g_wnd, MulDiv(640, g_dpi, 96), MulDiv(560, g_dpi, 96),
        [&](HDC dc, RECT rc) {
            int m = MulDiv(20, g_dpi, 96);
            RECT t = { m, m, rc.right - m, m + MulDiv(28, g_dpi, 96) };
            Txt(dc, t, std::wstring(T(S_APKMI_TITLE)) + L": " + a->fileName, Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            RECT s = { m, m + MulDiv(30, g_dpi, 96), rc.right - m, m + MulDiv(48, g_dpi, 96) };
            Txt(dc, s, T(S_APKMI_SUB), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
            int y = m + MulDiv(58, g_dpi, 96);
            for (size_t i = 0; i < g_insts.size(); ++i) {
                Runtime* r = Rt(g_insts[i].id);
                bool ready = r && r->st == St::Running && r->adbOk;
                std::wstring label = g_insts[i].name + (ready ? L"  ●" : L"  ○");
                CheckBoxV2(dc, { m, y, rc.right - m, y + MulDiv(22, g_dpi, 96) },
                         g_miTg[i], label, (int)(ID_APKMI0 + i));
                y += MulDiv(28, g_dpi, 96);
            }
            y += m / 2;
            CheckBoxV2(dc, { m, y, rc.right - m, y + MulDiv(22, g_dpi, 96) }, g_snapFirst, T(S_APKMI_SNAPFIRST), 3892);
            y += MulDiv(34, g_dpi, 96);
            // results table
            if (!g_apkResults.empty() || g_miDone) {
                RECT hh = { m, y, rc.right - m, y + MulDiv(20, g_dpi, 96) };
                Txt(dc, hh, T(S_APKMI_DONE), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
                y += MulDiv(24, g_dpi, 96);
                int cw = (rc.right - 2 * m) / 4;
                RECT hd = { m, y, rc.right - m, y + MulDiv(20, g_dpi, 96) };
                Txt(dc, { hd.left, y, hd.left + cw, y + 20 }, L"Инстанс", Fnt(F_SMALL), CL_SUB, DT_LEFT);
                Txt(dc, { hd.left + cw, y, hd.left + 2 * cw, y + 20 }, T(S_APKMI_RESULT), Fnt(F_SMALL), CL_SUB, DT_LEFT);
                Txt(dc, { hd.left + 2 * cw, y, hd.left + 3 * cw, y + 20 }, T(S_APKMI_TIME), Fnt(F_SMALL), CL_SUB, DT_LEFT);
                Txt(dc, { hd.left + 3 * cw, y, hd.right, y + 20 }, T(S_APKMI_ERR), Fnt(F_SMALL), CL_SUB, DT_LEFT);
                y += MulDiv(22, g_dpi, 96);
                for (auto& r0 : g_apkResults) {
                    COLORREF col = r0.error == L"-" ? CL_OK : CL_ERR;
                    Txt(dc, { m, y, m + cw, y + 18 }, r0.instanceName, Fnt(F_SMALL), CL_TEXT, DT_LEFT);
                    Txt(dc, { m + cw, y, m + 2 * cw, y + 18 }, r0.result, Fnt(F_SMALL), col, DT_LEFT);
                    Txt(dc, { m + 2 * cw, y, m + 3 * cw, y + 18 }, Fmt(L"%d s", r0.seconds), Fnt(F_SMALL), CL_SUB, DT_LEFT);
                    Txt(dc, { m + 3 * cw, y, rc.right - m, y + 18 }, r0.error, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_END_ELLIPSIS);
                    y += MulDiv(20, g_dpi, 96);
                }
            }
            int bh = MulDiv(38, g_dpi, 96);
            int by = rc.bottom - m - bh;
            bool any = false;
            for (bool b : g_miTg) any = any || b;
            RECT ok = { rc.right - m - MulDiv(140, g_dpi, 96), by, rc.right - m, by + bh };
            Btn(dc, 3890, ok, g_miRunning ? L"…" : T(S_APKMI_INSTALL), BSTY_PRIMARY, any && !g_miRunning);
            RECT cc = { ok.left - m / 2 - MulDiv(120, g_dpi, 96), by, ok.left - m / 2, by + bh };
            Btn(dc, 3891, cc, g_miDone ? T(S_WIZ_FINISH) : T(S_BTN_CANCEL), BSTY_GHOST);
        },
        [&](int id) {
            for (size_t i = 0; i < g_insts.size(); ++i)
                if (id == (int)(ID_APKMI0 + i)) g_miTg[i] = !g_miTg[i];
            if (id == 3892) g_snapFirst = !g_snapFirst;
            if (id == 3890 && !g_miRunning) {
                std::vector<std::wstring> ids;
                for (size_t i = 0; i < g_insts.size(); ++i)
                    if (g_miTg[i]) ids.push_back(g_insts[i].id);
                g_miRunning = true;
                RunOpThread([ids, apkId] {
                    std::vector<ApkInstallResult> res;
                    ApkInstallMulti(ids, apkId, g_snapFirst, res);
                    g_apkResults = res;
                    g_miRunning = false;
                    g_miDone = true;
                });
                return false;
            }
            if (id == 3891) return !g_miRunning;
            return false;
        },
        nullptr);
    return g_miDone;
}

// ================================================================ init / shutdown
void V2Init() {
    ApkLibLoad();
    KeymapsLoad();
    PerfSamplerStart();
    KeymapEngineStart();
    FindLocalImages();      // warm the image scan
    LogW(L"init", L"stage 2 services initialized (apk lib: %d, keymaps: %d)",
         (int)ApkLib().size(), (int)Keymaps().size());
}

void V2Shutdown() {
    KeymapEngineStop();
}

// ================================================================ V2TimerTick
void V2TimerTick() {
    // perf page auto-refresh
    if (g_page == Pg::Perf) UiInvalidate();
}

// ================================================================ V2Click dispatcher
bool V2Click(int id) {
    // ---- APK library page
    if (id == 3871) {   // add APK
        std::wstring f = PickFile(g_wnd, L"APK (*.apk)\0*.apk\0", T(S_APK_ADDLIB), nullptr);
        if (!f.empty()) {
            std::wstring err;
            if (ApkAdd(f, g_lang ? L"Added manually" : L"Добавлено вручную", err)) {
                UiNotify((g_lang ? std::wstring(L"APK added: ") : std::wstring(L"APK добавлен: ")) + BaseName(f), 0);
            } else UiNotify(err, 2);
            UiInvalidate();
        }
        return true;
    }
    if (id == 3872) { OpenInExplorer(g_p.apks); return true; }
    if (id >= 3880 && id < 3880 + 500) {           // install via multi dialog
        size_t i = id - 3880;
        auto& lib = ApkLib();
        if (i < lib.size()) DialogApkMultiInstall(lib[i].id);
        return true;
    }
    if (id >= 4380 && id < 4380 + 500) {           // delete from library
        size_t i = id - 4380;
        auto& lib = ApkLib();
        if (i < lib.size()) {
            int mb = MessageBoxW(g_wnd, (g_lang ? L"Remove this APK from the library?" :
                                                    L"Удалить этот APK из библиотеки?"),
                                 T(S_APK_DELETE2), MB_YESNO | MB_ICONQUESTION);
            if (mb == IDYES) {
                std::wstring err;
                ApkRemove(lib[i].id, false, err);
                UiInvalidate();
            }
        }
        return true;
    }
    // ---- perf instance chips (3500..3549, K_BTN on perf page only)
    if (g_page == Pg::Perf && id >= 3500 && id < 3500 + 50) {
        int idx = id - 3500;
        if (idx >= 0 && idx < (int)g_insts.size()) { g_perfSel = g_insts[idx].id; UiInvalidate(); }
        return true;
    }
    // ---- images: cancel download
    if (id == 3899) { ImageCancelDownload(); return true; }
    // ---- backups page top buttons
    if (id == 3850) {   // create backup of selected instance
        std::wstring id0 = SelInst();
        if (id0.empty()) return true;
        int mb = MessageBoxW(g_wnd, T(S_BK_ASK), T(S_BK_CREATE), MB_YESNOCANCEL | MB_ICONQUESTION);
        if (mb == IDCANCEL) return true;
        bool snaps = mb == IDYES;
        RunOpThread([id0, snaps] {
            std::wstring err;
            bool ok = BackupInstanceV2(id0, true, snaps, err);
            UiNotify(ok ? (g_lang ? L"Backup created" : L"Резервная копия создана") : err, ok ? 0 : 2);
            UiInvalidate();
        });
        return true;
    }
    if (id == 3851) {   // import
        std::wstring f = PickFile(g_wnd,
            L"NovaDroid backup (*.novadroid-backup)\0*.novadroid-backup\0Tar archive (*.tar)\0*.tar\0\0",
            T(S_BK_IMPORT), nullptr);
        if (!f.empty()) {
            RunOpThread([f] {
                std::wstring newId, err;
                bool ok = ImportBackupFile(f, newId, err);
                UiNotify(ok ? std::wstring(T(S_MB_IMPORTOK)) : err, ok ? 0 : 2);
                UiInvalidate();
            });
        }
        return true;
    }
    if (id == 3852) { OpenInExplorer(g_p.backups); return true; }
    // ---- snapshots page: create
    if (id == 3860) {
        std::wstring id0 = SelInst();
        if (id0.empty()) return true;
        std::wstring nm, dsc; bool prot = false;
        if (DialogSnapshotAdd(g_wnd, nm, dsc, prot)) {
            RunOpThread([id0, nm, dsc, prot] {
                std::wstring err;
                std::wstring sid = SnapshotCreate(id0, nm, dsc, L"user", prot, err);
                UiNotify(sid.empty() ? err : (g_lang ? L"Snapshot created" : L"Снимок создан"),
                         sid.empty() ? 2 : 0);
                UiInvalidate();
            });
        }
        return true;
    }
    // ---- keymap top buttons
    if (id == ID_KM_ADD) { g_keymapSel = KeymapAddDefault(); UiInvalidate(); return true; }
    if (id == ID_KM_IMPORT) {
        std::wstring f = PickFile(g_wnd, L"Keymap JSON (*.json)\0*.json\0", T(S_KM_IMPORT), nullptr);
        if (!f.empty()) {
            std::wstring txt;
            if (ReadText(f, txt)) {
                JValue v; std::wstring err;
                if (JsonParse(txt, v, err)) {
                    KeymapProfile p;
                    p.id = Fmt(L"keymap-%03d", (int)Keymaps().size() + 1);
                    p.createdAt = p.updatedAt = NowIso();
                    if (auto* x = v.find(L"name")) p.name = x->asStr();
                    if (auto* x = v.find(L"packageName")) p.packageName = x->asStr();
                    if (auto* x = v.find(L"resolutionWidth")) p.resW = x->asInt(1280);
                    if (auto* x = v.find(L"resolutionHeight")) p.resH = x->asInt(720);
                    Keymaps().push_back(p);
                    KeymapsSave();
                    UiNotify(g_lang ? L"Keymap imported" : L"Раскладка импортирована", 0);
                } else UiNotify(err, 2);
            }
            UiInvalidate();
        }
        return true;
    }
    // ---- image catalog rows
    if (id >= ID_IMG0 && id < ID_IMG0 + 500) {
        size_t i = (id - ID_IMG0) / 5;
        int slot = (id - ID_IMG0) % 5;
        auto& cat = ImageCatalog();
        if (i >= cat.size()) return true;
        std::wstring local = g_p.images + L"\\" + cat[i].fileName;
        bool have = FE(local);
        if (slot == 0) {
            if (have) { ImageUseAsDefault(local); UiNotify(T(S_IMG_USEDDEF), 0); }
            else ImageDownloadAsync(i);
            UiInvalidate();
        } else if (slot == 1 && have) {
            UiNotify(T(S_IMG_SHACHECK), 3);
            std::wstring fn = cat[i].fileName;
            std::wstring want = cat[i].sha256;
            RunOpThread([local, fn, want] {
                std::wstring real = Sha256OfFile(local);
                bool ok = !real.empty() && !want.empty() &&
                          _wcsicmp(real.c_str(), want.c_str()) == 0;
                UiNotify((g_lang ? L"SHA-256 " : L"SHA-256 ") + fn + L": " +
                         (want.empty() ? (g_lang ? L"no reference hash" : L"нет эталонного хеша")
                          : ok ? T(S_IMG_OK) : T(S_IMG_FAIL)),
                         ok ? 0 : (want.empty() ? 1 : 2));
                if (!real.empty() && want.empty())
                    LogW(L"image", L"sha256(%s) = %s", fn.c_str(), real.c_str());
            });
        } else if (slot == 3 && have) {
            int mb = MessageBoxW(g_wnd, (std::wstring(g_lang ? L"Delete local image file?" : L"Удалить локальный файл образа?") +
                             L"\n" + local).c_str(), T(S_IMG_DELETE), MB_YESNO | MB_ICONWARNING);
            if (mb == IDYES) { DeleteFileW(local.c_str()); UiInvalidate(); }
        } else if (slot == 4) {
            ShellOpen(cat[i].urls[cat[i].urls.size() - 1]);
        }
        return true;
    }
    // ---- local image rows
    if (id >= ID_LOCIMG0 && id < ID_LOCIMG0 + 500) {
        size_t i = (id - ID_LOCIMG0) / 3;
        int slot = (id - ID_LOCIMG0) % 3;
        auto locals = FindLocalImages();
        if (i >= locals.size()) return true;
        if (slot == 0) { ImageUseAsDefault(locals[i]); UiNotify(T(S_IMG_USEDDEF), 0); UiInvalidate(); }
        else if (slot == 1) {
            int mb = MessageBoxW(g_wnd, (std::wstring(g_lang ? L"Delete local image file?" : L"Удалить локальный файл образа?") +
                             L"\n" + locals[i]).c_str(), T(S_IMG_DELETE), MB_YESNO | MB_ICONWARNING);
            if (mb == IDYES) { DeleteFileW(locals[i].c_str()); UiInvalidate(); }
        } else if (slot == 2) {
            std::wstring f = locals[i];
            RunOpThread([f] {
                std::wstring h = Sha256OfFile(f);
                UiNotify(h.empty() ? T(S_IMG_FAIL) : (L"SHA-256: " + h), h.empty() ? 2 : 0);
            });
        }
        return true;
    }
    // ---- backup rows
    if (id >= ID_BK0 && id < ID_BK0 + 500) {
        size_t i = (id - ID_BK0) / 4;
        int slot = (id - ID_BK0) % 4;
        auto list = ListBackups();
        if (i >= list.size()) return true;
        BackupInfo b = list[i];
        if (slot == 0) {   // restore into instance by id (create if absent)
            std::wstring iid = b.id;
            if (!Inst(iid)) {
                int mb = MessageBoxW(g_wnd,
                    (g_lang ? L"Instance from backup not found. Import as a new instance?" :
                              L"Инстанс из копии не найден. Импортировать как новый?"),
                    T(S_BK_RESTORE2), MB_YESNO | MB_ICONQUESTION);
                if (mb != IDYES) return true;
                RunOpThread([b] {
                    std::wstring newId, err;
                    if (ImportBackupFile(b.dir, newId, err))
                        UiNotify(std::wstring(T(S_MB_IMPORTOK)) + L" -> " + newId, 0);
                    else UiNotify(err, 2);
                    UiInvalidate();
                });
                return true;
            }
            int mb = MessageBoxW(g_wnd, (std::wstring(T(S_MB_RESTORED)) + L"?").c_str(), T(S_BK_RESTORE2),
                                 MB_YESNO | MB_ICONQUESTION);
            if (mb != IDYES) return true;
            RunOpThread([b, iid] {
                // import folder over existing: use ImportBackupFile on the dir (new instance) is safer,
                // but per TZ we restore disk+config into the same instance id
                std::wstring err;
                bool ok = false;
                {
                    std::wstring txt;
                    if (ReadText(b.dir + L"\\config.json", txt)) {
                        JValue v; std::wstring e;
                        if (JsonParse(txt, v, e)) {
                            std::wstring dn = FE(b.dir + L"\\disk.qcow2") ? L"disk.qcow2" : L"disk.raw";
                            if (FE(b.dir + L"\\" + dn)) {
                                InstanceCfg* c = Inst(iid);
                                if (c && !(Rt(iid) && Rt(iid)->hProc && ProcAlive(Rt(iid)->hProc))) {
                                    ok = CopyFileW((b.dir + L"\\" + dn).c_str(),
                                                   (c->Dir() + L"\\" + dn).c_str(), FALSE) != 0;
                                }
                            }
                        }
                    }
                }
                UiNotify(ok ? T(S_MB_RESTORED) :
                         (g_lang ? L"Restore failed (is the instance stopped?)" : L"Не удалось восстановить (инстанс остановлен?)"),
                         ok ? 0 : 2);
                UiInvalidate();
            });
        } else if (slot == 1) {   // export .novadroid-backup
            std::wstring dst = PickFolder(g_wnd, T(S_BK_EXPORT));
            if (!dst.empty()) {
                dst += L"\\" + BaseName(b.dir) + L".novadroid-backup";
                std::wstring src = b.dir;
                RunOpThread([src, dst] {
                    std::wstring err;
                    if (ExportBackupFile(src, dst, err))   // pass dir-based lookup
                        UiNotify(g_lang ? L"Exported" : L"Экспортировано", 0);
                    else UiNotify(err, 2);
                    UiInvalidate();
                });
            }
        } else if (slot == 2) {   // delete
            int mb = MessageBoxW(g_wnd, (g_lang ? L"Delete this backup?" : L"Удалить эту резервную копию?"),
                                 T(S_IMG_DELETE), MB_YESNO | MB_ICONWARNING);
            if (mb == IDYES) {
                std::wstring err;
                if (!DeleteBackupDir(b.dir, err)) UiNotify(err, 2);
                UiInvalidate();
            }
        } else if (slot == 3) OpenInExplorer(b.dir);
        return true;
    }
    // ---- snapshot rows
    if (id >= ID_SNAP0 && id < ID_SNAP0 + 500) {
        std::wstring iid = SelInst();
        InstanceCfg* c = Inst(iid);
        if (!c) return true;
        size_t i = (id - ID_SNAP0) / 3;
        int slot = (id - ID_SNAP0) % 3;
        auto list = LoadSnapshots(iid);
        if (i >= list.size()) return true;
        Snapshot s = list[i];
        if (slot == 0) {   // restore
            int mb = MessageBoxW(g_wnd, T(S_SNAP_WARNREST), T(S_SNAP_RESTORE),
                                 MB_YESNO | MB_ICONWARNING);
            if (mb != IDYES) return true;
            RunOpThread([iid, sid = s.id] {
                std::wstring err;
                bool ok = SnapshotRestore(iid, sid, err);
                UiNotify(ok ? T(S_MB_RESTORED) : err, ok ? 0 : 2);
                UiInvalidate();
            });
        } else if (slot == 1) {   // delete
            if (s.isProtected) {
                int mb = MessageBoxW(g_wnd, (g_lang ? L"This is a protected snapshot. Really delete it?" :
                                                     L"Это защищённый снимок. Действительно удалить?"),
                                     T(S_SNAP_DELETE), MB_YESNO | MB_ICONWARNING);
                if (mb != IDYES) return true;
            }
            RunOpThread([iid, sid = s.id] {
                std::wstring err;
                bool ok = SnapshotDelete(iid, sid, err);
                UiNotify(ok ? (g_lang ? L"Snapshot deleted" : L"Снимок удалён") : err, ok ? 0 : 2);
                UiInvalidate();
            });
        }
        return true;
    }
    // ---- keymap rows
    if (id >= ID_KM0 && id < ID_KM0 + 500) {
        size_t i = (id - ID_KM0) / 4;
        int slot = (id - ID_KM0) % 4;
        auto& kms = Keymaps();
        if (i >= kms.size()) return true;
        KeymapProfile& p = kms[i];
        g_keymapSel = p.id;
        if (slot == 0) {   // toggle engine for running instance
            std::wstring inst = SelInst();
            Runtime* r = inst.empty() ? nullptr : Rt(inst);
            if (p.enabled) {
                KeymapEngineSetInstance(L"", L"");
                p.enabled = false;
                UiNotify(g_lang ? std::wstring(L"Keymap OFF") : std::wstring(L"Раскладка ВЫКЛ"), 3);
            } else {
                if (!r || !r->hProc || !ProcAlive(r->hProc)) {
                    UiNotify(g_lang ? L"Start the instance first, then enable the keymap."
                                    : L"Сначала запустите инстанс, затем включите раскладку.", 1);
                } else {
                    InstanceCfg* c = Inst(inst);
                    p.resW = c->resW; p.resH = c->resH;
                    KeymapEngineSetInstance(inst, p.id);
                    p.enabled = true;
                    UiNotify((g_lang ? std::wstring(L"Keymap ON for ") : std::wstring(L"Раскладка ВКЛ для ")) + c->name +
                             L"  (F1 " + (g_lang ? L"toggles" : L"переключает") + L")", 0);
                }
            }
            KeymapsSave();
            UiInvalidate();
        } else if (slot == 1) {   // add binding: capture key + coords
            int vk = 0;
            if (DialogKeyCapture(g_wnd, vk)) {
                KeyBinding b;
                b.vk = vk;
                b.action = L"tap";
                b.x = p.resW / 2; b.y = p.resH / 2;
                p.bindings.push_back(b);
                p.updatedAt = NowIso();
                KeymapsSave();
                UiInvalidate();
            }
        } else if (slot == 2) {   // delete profile
            int mb = MessageBoxW(g_wnd, (g_lang ? L"Delete this keymap profile?" : L"Удалить эту раскладку?"),
                                 T(S_KM_DELETE), MB_YESNO | MB_ICONWARNING);
            if (mb == IDYES) {
                if (p.enabled) KeymapEngineSetInstance(L"", L"");
                KeymapDelete(p.id);
                if (g_keymapSel == p.id) g_keymapSel.clear();
                UiInvalidate();
            }
        } else if (slot == 3) {   // export json
            std::wstring dir = PickFolder(g_wnd, T(S_KM_EXPORT));
            if (!dir.empty()) {
                JValue v; v.type = JValue::Obj;
                v.obj[L"name"] = JValue::MakeStr(p.name);
                v.obj[L"packageName"] = JValue::MakeStr(p.packageName);
                v.obj[L"resolutionWidth"] = JValue::MakeNum(p.resW);
                v.obj[L"resolutionHeight"] = JValue::MakeNum(p.resH);
                v.obj[L"orientation"] = JValue::MakeStr(p.orientation);
                v.obj[L"activationHotkey"] = JValue::MakeStr(p.activationHotkey);
                JValue bs; bs.type = JValue::Arr;
                for (auto& b : p.bindings) {
                    JValue e; e.type = JValue::Obj;
                    e.obj[L"vk"] = JValue::MakeNum(b.vk);
                    e.obj[L"action"] = JValue::MakeStr(b.action);
                    e.obj[L"x"] = JValue::MakeNum(b.x);
                    e.obj[L"y"] = JValue::MakeNum(b.y);
                    e.obj[L"x2"] = JValue::MakeNum(b.x2);
                    e.obj[L"y2"] = JValue::MakeNum(b.y2);
                    bs.arr.push_back(e);
                }
                v.obj[L"bindings"] = bs;
                std::wstring f = dir + L"\\" + p.name + L".json";
                if (WriteText(f, JsonWrite(v)))
                    UiNotify((g_lang ? std::wstring(L"Exported: ") : std::wstring(L"Экспортировано: ")) + f, 0);
            }
        }
        return true;
    }
    // ---- binding rows
    if (id >= ID_KMBIND0 && id < ID_KMBIND0 + 500) {
        size_t i = (id - ID_KMBIND0) / 2;
        int slot = (id - ID_KMBIND0) % 2;
        KeymapProfile* p = KeymapById(g_keymapSel);
        if (!p || i >= p->bindings.size()) return true;
        if (slot == 1) {
            p->bindings.erase(p->bindings.begin() + i);
            p->updatedAt = NowIso();
            KeymapsSave();
            UiInvalidate();
        } else {
            // edit: re-capture key
            int vk = 0;
            if (DialogKeyCapture(g_wnd, vk)) {
                p->bindings[i].vk = vk;
                p->updatedAt = NowIso();
                KeymapsSave();
            }
            UiInvalidate();
        }
        return true;
    }
    return false;
}
