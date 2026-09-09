// ============================================================================
//  NovaDroid - dialogs.cpp  Instance create/config dialog + input dialog.
//  Standard controls, dark themed via WM_CTLCOLOR* + dark-mode themes.
// ============================================================================
#include "ui.h"
#include "v3.h"

// ---------------------------------------------------------------- theme helpers
static HBRUSH g_darkBrush = nullptr;
static HBRUSH DarkBrush() {
    if (!g_darkBrush) g_darkBrush = CreateSolidBrush(CL_SURF);
    return g_darkBrush;
}
static void DarkTheme(HWND h) {
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
}
static void ApplyDarkCtl(HWND h, HFONT f) {
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
    DarkTheme(h);
}

// ---------------------------------------------------------------- instance dialog
static const int IDC_NAME = 101, IDC_IMAGE = 102, IDC_BROWSEIMG = 103, IDC_BOOT = 104,
    IDC_CPU = 105, IDC_RAM = 106, IDC_DISK = 107, IDC_RES = 108, IDC_DPI = 109,
    IDC_FPS = 110, IDC_GPU = 111, IDC_ADB = 112, IDC_SHARED = 113, IDC_SHAREDPATH = 114,
    IDC_BROWSESHARE = 115, IDC_SHORTCUT = 116, IDC_OK = 117, IDC_CANCEL = 118,
    IDC_SOUND = 119, IDC_FULL = 120, IDC_GAME = 121;

struct DlgState {
    InstanceCfg cfg;
    bool isNew;
    bool ok;
    HWND hName, hImage, hBrowseImg, hBoot, hCpu, hRam, hDisk, hRes, hDpi, hFps, hGpu,
         hAdb, hShared, hSharedPath, hBrowseShare, hShortcut, hOk, hCancel, hSound, hFull;
};
static DlgState* g_dlg = nullptr;

static void ComboAdd(HWND h, const std::wstring& s) {
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)s.c_str());
}
static void ComboSel(HWND h, int i) { SendMessageW(h, CB_SETCURSEL, i, 0); }
static int ComboGet(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }

static void FillCombos(HWND parent) {
    DlgState* d = g_dlg;
    // boot
    ComboAdd(d->hBoot, T(S_DLG_BOOT_ISO));
    ComboAdd(d->hBoot, T(S_DLG_BOOT_DISK));
    ComboSel(d->hBoot, d->cfg.bootMode == L"disk" ? 1 : 0);
    // cpu / ram / disk
    const wchar_t* cpu[] = { L"1", L"2", L"4", L"6", L"8" };
    for (auto c : cpu) ComboAdd(d->hCpu, c);
    ComboSel(d->hCpu, d->cfg.cpuCores >= 8 ? 4 : d->cfg.cpuCores >= 6 ? 3 : d->cfg.cpuCores >= 4 ? 2 : d->cfg.cpuCores >= 2 ? 1 : 0);
    const wchar_t* ram[] = { L"1024", L"2048", L"4096", L"6144", L"8192", L"16384" };
    int ramSel = 2;
    for (int i = 0; i < 6; ++i) { ComboAdd(d->hRam, ram[i]); if (_wtoi(ram[i]) == d->cfg.ramMb) ramSel = i; }
    ComboSel(d->hRam, ramSel);
    const wchar_t* disk[] = { L"8", L"16", L"32", L"64", L"128" };
    int diskSel = 2;
    for (int i = 0; i < 5; ++i) { ComboAdd(d->hDisk, disk[i]); if (_wtoi(disk[i]) == d->cfg.diskGb) diskSel = i; }
    ComboSel(d->hDisk, diskSel);
    // resolution
    const wchar_t* res[] = { L"1280x720", L"1600x900", L"1920x1080", L"800x1280", L"720x1280", L"1080x1920" };
    int resSel = 0;
    for (int i = 0; i < 6; ++i) {
        ComboAdd(d->hRes, res[i]);
        int w = 0, hh = 0;
        swscanf(res[i], L"%dx%d", &w, &hh);
        if (w == d->cfg.resW && hh == d->cfg.resH) resSel = i;
    }
    ComboSel(d->hRes, resSel);
    // dpi / fps
    const wchar_t* dpi[] = { L"160", L"240", L"320", L"480" };
    int dpiSel = 1;
    for (int i = 0; i < 4; ++i) { ComboAdd(d->hDpi, dpi[i]); if (_wtoi(dpi[i]) == d->cfg.dpi) dpiSel = i; }
    ComboSel(d->hDpi, dpiSel);
    const wchar_t* fps[] = { L"30", L"60", L"90", L"120" };
    int fpsSel = 1;
    for (int i = 0; i < 4; ++i) { ComboAdd(d->hFps, fps[i]); if (_wtoi(fps[i]) == d->cfg.fpsLimit) fpsSel = i; }
    ComboSel(d->hFps, fpsSel);
    // gpu
    ComboAdd(d->hGpu, T(S_DLG_GPU_AUTO));
    ComboAdd(d->hGpu, T(S_DLG_GPU_STD));
    ComboAdd(d->hGpu, T(S_DLG_GPU_VIRT));
    ComboSel(d->hGpu, d->cfg.gpuMode == L"virtio" ? 2 : d->cfg.gpuMode == L"std" ? 1 : 0);
}

static void ReadDialog() {
    DlgState* d = g_dlg;
    wchar_t buf[1024] = {};
    GetWindowTextW(d->hName, buf, 1024); d->cfg.name = buf;
    GetWindowTextW(d->hImage, buf, 1024); d->cfg.imagePath = buf;
    d->cfg.bootMode = ComboGet(d->hBoot) == 1 ? L"disk" : L"iso";
    const int cpuT[] = { 1, 2, 4, 6, 8 };
    d->cfg.cpuCores = cpuT[ComboGet(d->hCpu) < 0 ? 2 : ComboGet(d->hCpu)];
    const int ramT[] = { 1024, 2048, 4096, 6144, 8192, 16384 };
    int ci = ComboGet(d->hRam); d->cfg.ramMb = ramT[ci < 0 ? 2 : ci];
    const int diskT[] = { 8, 16, 32, 64, 128 };
    ci = ComboGet(d->hDisk); d->cfg.diskGb = diskT[ci < 0 ? 2 : ci];
    const int resW[] = { 1280, 1600, 1920, 800, 720, 1080 };
    const int resH[] = { 720, 900, 1080, 1280, 1280, 1920 };
    ci = ComboGet(d->hRes); if (ci < 0) ci = 0;
    d->cfg.resW = resW[ci]; d->cfg.resH = resH[ci];
    const int dpiT[] = { 160, 240, 320, 480 };
    ci = ComboGet(d->hDpi); d->cfg.dpi = dpiT[ci < 0 ? 1 : ci];
    const int fpsT[] = { 30, 60, 90, 120 };
    ci = ComboGet(d->hFps); d->cfg.fpsLimit = fpsT[ci < 0 ? 1 : ci];
    ci = ComboGet(d->hGpu); d->cfg.gpuMode = ci == 2 ? L"virtio" : ci == 1 ? L"std" : L"auto";
    d->cfg.adbEnabled = SendMessageW(d->hAdb, BM_GETCHECK, 0, 0) == BST_CHECKED;
    d->cfg.sharedFolderEnabled = SendMessageW(d->hShared, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(d->hSharedPath, buf, 1024); d->cfg.sharedFolderPath = buf;
    d->cfg.soundEnabled = SendMessageW(d->hSound, BM_GETCHECK, 0, 0) == BST_CHECKED;
    d->cfg.fullscreen = SendMessageW(d->hFull, BM_GETCHECK, 0, 0) == BST_CHECKED;
    d->cfg.wantShortcut = SendMessageW(d->hShortcut, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    static HBRUSH bgBrush = nullptr;
    if (!bgBrush) bgBrush = CreateSolidBrush(CL_BG);
    switch (m) {
        case WM_CTLCOLORSTATIC:
            SetTextColor((HDC)wp, CL_TEXT);
            SetBkColor((HDC)wp, CL_BG);
            return (LRESULT)bgBrush;
        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wp, CL_TEXT);
            SetBkColor((HDC)wp, CL_SURF2);
            return (LRESULT)DarkBrush();
        case WM_CTLCOLORLISTBOX:
            SetTextColor((HDC)wp, CL_TEXT);
            SetBkColor((HDC)wp, CL_SURF2);
            return (LRESULT)DarkBrush();
        case WM_CTLCOLORBTN:
            return (LRESULT)bgBrush;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            int style = di->CtlID == IDC_OK ? BSTY_PRIMARY : BSTY_SURF;
            RECT r = di->rcItem;
            bool hov = di->itemState & ODS_SELECTED;
            if (style == BSTY_PRIMARY)
                GradientRect(di->hDC, r, hov ? Lighten(Accent(), 30) : Accent(), hov ? Accent() : Lighten(Accent(), -30));
            else
                FillRound(di->hDC, r, hov ? Lighten(CL_SURF2, 14) : CL_SURF2, 8);
            wchar_t txt[64] = {};
            GetWindowTextW(di->hwndItem, txt, 64);
            Txt(di->hDC, r, txt, Fnt(F_BODY), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (di->itemState & ODS_FOCUS) {
                LOGBRUSH lb = { BS_SOLID, CL_ACCENT };
                HPEN p = ExtCreatePen(PS_COSMETIC | PS_ALTERNATE, 1, &lb, 0, nullptr);
                HGDIOBJ op = SelectObject(di->hDC, p);
                HGDIOBJ ob = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(di->hDC, r.left, r.top, r.right, r.bottom);
                SelectObject(di->hDC, op); SelectObject(di->hDC, ob);
                DeleteObject(p);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_BROWSEIMG && code == BN_CLICKED) {
                std::wstring f = PickFile(h, L"Android image (*.iso;*.img;*.qcow2)\0*.iso;*.img;*.qcow2\0All files\0*.*\0",
                                          T(S_DLG_IMAGE), nullptr);
                if (!f.empty()) SetWindowTextW(g_dlg->hImage, f.c_str());
            } else if (id == IDC_BROWSESHARE && code == BN_CLICKED) {
                std::wstring f = PickFolder(h, T(S_DLG_SHARED));
                if (!f.empty()) SetWindowTextW(g_dlg->hSharedPath, f.c_str());
            } else if (id == IDC_GAME && code == BN_CLICKED) {
                InstanceCfg tmp = g_dlg->cfg;
                DialogGamingSettings(h, tmp);
                g_dlg->cfg = tmp;
                return 0;
            } else if (id == IDC_OK && code == BN_CLICKED) {
                ReadDialog();
                if (TrimW(g_dlg->cfg.name).empty()) {
                    MessageBoxW(h, g_lang == 0 ? L"Введите название инстанса." : L"Enter instance name.",
                                T(S_ERR2), MB_ICONWARNING);
                    return 0;
                }
                g_dlg->ok = true;
                DestroyWindow(h);
            } else if (id == IDC_CANCEL && code == BN_CLICKED) {
                g_dlg->ok = false;
                DestroyWindow(h);
            } else if (id == IDCANCEL) {
                g_dlg->ok = false;
                DestroyWindow(h);
            }
            return 0;
        }
        case WM_CLOSE:
            g_dlg->ok = false;
            DestroyWindow(h);
            return 0;
        case WM_NCDESTROY:
            return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool InstanceDialogImpl(HWND parent, InstanceCfg& cfg, bool isNew) {
    DlgState st;
    st.cfg = cfg;
    st.isNew = isNew;
    st.ok = false;
    g_dlg = &st;

    static bool clsDone = false;
    if (!clsDone) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = g_hi;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"NovaDroidInstDlg";
        RegisterClassExW(&wc);
        clsDone = true;
    }

    int scale = g_dpi / 96;
    int W = 620 * scale, H = 830 * scale;
    RECT pr; GetWindowRect(parent, &pr);
    int x = pr.left + ((pr.right - pr.left) - W) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - H) / 2;
    if (x < 0) x = 100; if (y < 0) y = 60;

    HWND dlg = CreateWindowExW(WS_EX_CONTROLPARENT, L"NovaDroidInstDlg",
        isNew ? T(S_DLG_CREATE) : T(S_DLG_EDIT),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, W, H,
        parent, nullptr, g_hi, nullptr);
    BOOL on = TRUE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));

    HFONT f = Fnt(F_BODY), fs = Fnt(F_SMALL), fb = Fnt(F_BIG);
    auto mkLabel = [&](const wchar_t* s, int X, int Y, int Wd) {
        HWND l = CreateWindowExW(0, L"STATIC", s, WS_CHILD | WS_VISIBLE, X, Y, Wd, 22 * scale,
                                 dlg, nullptr, g_hi, nullptr);
        SendMessageW(l, WM_SETFONT, (WPARAM)fs, TRUE);
        return l;
    };
    auto mkEdit = [&](int id, const std::wstring& val, int X, int Y, int Wd) {
        HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 X, Y, Wd, 30 * scale, dlg, (HMENU)(INT_PTR)id, g_hi, nullptr);
        ApplyDarkCtl(e, f);
        return e;
    };
    auto mkCombo = [&](int id, int X, int Y, int Wd) {
        HWND c = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 X, Y, Wd, 400, dlg, (HMENU)(INT_PTR)id, g_hi, nullptr);
        ApplyDarkCtl(c, f);
        return c;
    };
    auto mkCheck = [&](int id, const std::wstring& label, bool val, int X, int Y, int Wd) {
        HWND c = CreateWindowExW(0, L"BUTTON", label.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 X, Y, Wd, 26 * scale, dlg, (HMENU)(INT_PTR)id, g_hi, nullptr);
        ApplyDarkCtl(c, f);
        SendMessageW(c, BM_SETCHECK, val ? BST_CHECKED : BST_UNCHECKED, 0);
        return c;
    };
    auto mkBtn = [&](int id, const wchar_t* s, int X, int Y, int Wd, bool owner) {
        HWND b = CreateWindowExW(0, L"BUTTON", s, WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 (owner ? BS_OWNERDRAW : BS_PUSHBUTTON),
                                 X, Y, Wd, 36 * scale, dlg, (HMENU)(INT_PTR)id, g_hi, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)fb, TRUE);
        return b;
    };

    int m = 24 * scale;
    int cw = W - 2 * m;
    int y0 = 20 * scale;
    mkLabel(T(S_DLG_NAME), m, y0, cw); y0 += 24 * scale;
    st.hName = mkEdit(IDC_NAME, st.cfg.name, m, y0, cw); y0 += 40 * scale;
    mkLabel(T(S_DLG_IMAGE), m, y0, cw); y0 += 24 * scale;
    st.hImage = mkEdit(IDC_IMAGE, st.cfg.imagePath, m, y0, cw - 100 * scale);
    st.hBrowseImg = mkBtn(IDC_BROWSEIMG, T(S_DLG_BROWSE), m + cw - 96 * scale, y0 - 3 * scale, 96 * scale, false);
    y0 += 42 * scale;
    mkLabel(T(S_DLG_BOOT), m, y0, cw / 2 - 10 * scale); y0 += 24 * scale;
    st.hBoot = mkCombo(IDC_BOOT, m, y0, cw / 2 - 10 * scale);
    y0 += 42 * scale;
    // row: cpu ram disk
    int colw = cw / 3 - 8 * scale;
    mkLabel(T(S_DLG_CPU), m, y0, colw);
    mkLabel(T(S_DLG_RAM), m + colw + 12 * scale, y0, colw);
    mkLabel(T(S_DLG_DISK), m + 2 * (colw + 12 * scale), y0, colw); y0 += 24 * scale;
    st.hCpu = mkCombo(IDC_CPU, m, y0, colw);
    st.hRam = mkCombo(IDC_RAM, m + colw + 12 * scale, y0, colw);
    st.hDisk = mkCombo(IDC_DISK, m + 2 * (colw + 12 * scale), y0, colw);
    y0 += 42 * scale;
    mkLabel(T(S_DLG_RES), m, y0, colw);
    mkLabel(T(S_DLG_DPI), m + colw + 12 * scale, y0, colw);
    mkLabel(T(S_DLG_FPS), m + 2 * (colw + 12 * scale), y0, colw); y0 += 24 * scale;
    st.hRes = mkCombo(IDC_RES, m, y0, colw);
    st.hDpi = mkCombo(IDC_DPI, m + colw + 12 * scale, y0, colw);
    st.hFps = mkCombo(IDC_FPS, m + 2 * (colw + 12 * scale), y0, colw);
    y0 += 42 * scale;
    mkLabel(T(S_DLG_GPU), m, y0, colw); y0 += 24 * scale;
    st.hGpu = mkCombo(IDC_GPU, m, y0, colw);
    y0 += 46 * scale;
    st.hAdb = mkCheck(IDC_ADB, T(S_DLG_ADB), st.cfg.adbEnabled, m, y0, cw); y0 += 30 * scale;
    st.hSound = mkCheck(IDC_SOUND, T(S_DLG_SOUND), st.cfg.soundEnabled, m, y0, cw / 2);
    st.hFull = mkCheck(IDC_FULL, T(S_DLG_FULL), st.cfg.fullscreen, m + cw / 2, y0, cw / 2); y0 += 30 * scale;
    st.hShared = mkCheck(IDC_SHARED, T(S_DLG_SHARED), st.cfg.sharedFolderEnabled, m, y0, cw); y0 += 30 * scale;
    st.hSharedPath = mkEdit(IDC_SHAREDPATH, st.cfg.sharedFolderPath, m, y0, cw - 100 * scale);
    st.hBrowseShare = mkBtn(IDC_BROWSESHARE, T(S_DLG_BROWSE), m + cw - 96 * scale, y0 - 3 * scale, 96 * scale, false);
    y0 += 42 * scale;
    st.hShortcut = mkCheck(IDC_SHORTCUT, T(S_DLG_SHORTCUT), isNew, m, y0, cw); y0 += 48 * scale;
    st.hOk = mkBtn(IDC_OK, T(S_BTN_SAVE), m, y0, 180 * scale, true);
    st.hCancel = mkBtn(IDC_CANCEL, T(S_BTN_CANCEL), m + 196 * scale, y0, 140 * scale, true);
    y0 += 46 * scale;
    std::wstring gameLbl = T(S3_G_TITLE); gameLbl += L"...";
    mkBtn(IDC_GAME, gameLbl.c_str(), m, y0, cw, false);

    FillCombos(dlg);
    EnsureDefaultSharedFolder(st.cfg);
    if (st.cfg.sharedFolderPath.empty())
        SetWindowTextW(st.hSharedPath, st.cfg.sharedFolderPath.c_str());
    else
        SetWindowTextW(st.hSharedPath, st.cfg.sharedFolderPath.c_str());

    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(st.hName);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsWindow(dlg)) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    bool ok = st.ok;
    if (ok) cfg = st.cfg;
    g_dlg = nullptr;
    return ok;
}

bool DialogInstance(HWND parent, InstanceCfg& c, bool isNew) {
    bool ok = InstanceDialogImpl(parent, c, isNew);
    if (ok && isNew && c.adbPort == 0) c.adbPort = NextFreePort();
    return ok;
}

// ---------------------------------------------------------------- input dialog
struct InState { std::wstring result; bool ok = false; HWND hEdit; };
static InState* g_in = nullptr;

static LRESULT CALLBACK InProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    static HBRUSH bg = nullptr;
    if (!bg) bg = CreateSolidBrush(CL_BG);
    switch (m) {
        case WM_CTLCOLORSTATIC:
            SetTextColor((HDC)wp, CL_TEXT); SetBkColor((HDC)wp, CL_BG); return (LRESULT)bg;
        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wp, CL_TEXT); SetBkColor((HDC)wp, CL_SURF2); return (LRESULT)DarkBrush();
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 1 || id == 2) {
                wchar_t buf[256] = {};
                if (id == 1) {
                    GetWindowTextW(g_in->hEdit, buf, 256);
                    g_in->result = buf;
                    g_in->ok = true;
                }
                DestroyWindow(h);
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

std::wstring DialogInput(HWND parent, const std::wstring& title, const std::wstring& def) {
    static bool cls = false;
    if (!cls) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = InProc;
        wc.hInstance = g_hi;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"NovaDroidInput";
        RegisterClassExW(&wc);
        cls = true;
    }
    InState st;
    g_in = &st;
    int scale = g_dpi / 96;
    int W = 440 * scale, H = 180 * scale;
    RECT pr; GetWindowRect(parent, &pr);
    HWND d = CreateWindowExW(WS_EX_CONTROLPARENT, L"NovaDroidInput", title.c_str(),
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             pr.left + ((pr.right - pr.left) - W) / 2,
                             pr.top + ((pr.bottom - pr.top) - H) / 2,
                             W, H, parent, nullptr, g_hi, nullptr);
    BOOL on = TRUE;
    DwmSetWindowAttribute(d, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def.c_str(),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                             20 * scale, 24 * scale, W - 40 * scale, 30 * scale,
                             d, (HMENU)(INT_PTR)10, g_hi, nullptr);
    ApplyDarkCtl(e, Fnt(F_BODY));
    HWND bok = CreateWindowExW(0, L"BUTTON", T(S_OK2), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                               W - 200 * scale, 80 * scale, 80 * scale, 34 * scale,
                               d, (HMENU)(INT_PTR)1, g_hi, nullptr);
    HWND bno = CreateWindowExW(0, L"BUTTON", T(S_BTN_CANCEL), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                               W - 110 * scale, 80 * scale, 90 * scale, 34 * scale,
                               d, (HMENU)(INT_PTR)2, g_hi, nullptr);
    SendMessageW(bok, WM_SETFONT, (WPARAM)Fnt(F_BODY), TRUE);
    SendMessageW(bno, WM_SETFONT, (WPARAM)Fnt(F_BODY), TRUE);
    g_in->hEdit = e;
    EnableWindow(parent, FALSE);
    ShowWindow(d, SW_SHOW);
    SetFocus(e);
    SendMessageW(e, EM_SETSEL, 0, -1);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsWindow(d)) break;
        if (!IsDialogMessageW(d, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    g_in = nullptr;
    return st.ok ? st.result : L"";
}
