// ============================================================================
//  NovaDroid - v3ui.cpp  Stage 3 UI: full-package download wizard (progress),
//  gaming settings dialog (TZ3 14.2 "Graphics and performance" tab),
//  settings/perf/diag page sections, profiles, init/timer/click glue.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v2ui_common.h"
#include "v3.h"

extern std::wstring g_perfSel;      // v2ui_dlg.cpp - perf page selection

static inline int ClampI(int v, int lo, int hi, int def) {
    if (v < lo || v > hi) return def;
    return v;
}

void V3Notify(const std::wstring& text, int type) { UiNotify(text, type); }

// ================================================================ download wizard
struct WizDlg {
    HWND h = nullptr;
    bool done = false;
};
static WizDlg* g_wiz = nullptr;

enum { WI_DOWNLOAD = 1, WI_OFFZIP, WI_VERIFY, WI_FOLDER, WI_CLOSE };

static void WizardDraw(HDC dc, RECT rc) {
    int m = MulDiv(24, g_dpi, 96);
    int sc = g_dpi / 96;
    int y = m;
    Txt(dc, { m, y, rc.right - m, y + 34 * sc }, T(S3_WIZ_TITLE), Fnt(F_TITLE), CL_TEXT, DT_LEFT);
    y += 42 * sc;
    Txt(dc, { m, y, rc.right - m, y + 20 * sc }, T(S3_WIZ_SUB), Fnt(F_SMALL), CL_SUB, DT_LEFT);
    y += 30 * sc;

    // contents card
    int cardH = 150 * sc;
    RECT card = { m, y, rc.right - m, y + cardH };
    FillRound(dc, card, CL_SURF2, 12);
    FrameRound(dc, card, CL_BORDER, 12);
    int ry = y + 14 * sc;
    const wchar_t* rowsRu[] = {
        L"QEMU 10.x — виртуализация WHPX, GPU virgl (virtio-gpu-gl)",
        L"Android Debug Bridge (platform-tools) — ADB",
        L"Образ Android-x86 9.0-r2 x86_64 — гостевая система",
        L"Документация и лицензии сторонних компонентов",
    };
    const wchar_t* rowsEn[] = {
        L"QEMU 10.x — WHPX virtualization, virgl GPU (virtio-gpu-gl)",
        L"Android Debug Bridge (platform-tools) - ADB",
        L"Android-x86 9.0-r2 x86_64 image - guest system",
        L"Documentation and third-party licenses",
    };
    for (int i = 0; i < 4; ++i) {
        RECT rr = { m + 16 * sc, ry, rc.right - m - 16 * sc, ry + 22 * sc };
        Txt(dc, rr, (g_lang == 0 ? rowsRu[i] : rowsEn[i]), Fnt(F_SMALL), CL_TEXT, DT_LEFT);
        ry += 26 * sc;
    }
    RECT tr = { m + 16 * sc, ry, rc.right - m - 16 * sc, ry + 20 * sc };
    Txt(dc, tr, Fmt(L"%s: ≈ 1,1 GB", T(S3_WIZ_TOTAL)), Fnt(F_SMALL), CL_ACCENT2, DT_LEFT);
    y += cardH + 16 * sc;

    // status + progress
    PkgStateInfo pi = PkgInfo();
    int barH = 14 * sc;
    RECT bar = { m, y, rc.right - m, y + barH };
    FillRound(dc, bar, CL_BG, 7);
    float pr = pi.progress;
    if (pi.state == 4) pr = 1;
    if (pr > 0) {
        RECT fill = bar;
        fill.right = bar.left + (LONG)((bar.right - bar.left) * pr);
        if (fill.right > bar.right) fill.right = bar.right;
        if (fill.right > fill.left) {
            fill.left += 2; fill.top += 2; fill.bottom -= 2;
            GradientRect(dc, fill, Accent(), CL_ACCENT2);
        }
    }
    FrameRound(dc, bar, CL_BORDER, 7);
    y += barH + 10 * sc;
    std::wstring st = pi.stageText;
    if (pi.state == 1 && pi.totalBytes > 0)
        st += Fmt(L"  %s / %s (%.0f%%)", HumanSize((uint64_t)pi.gotBytes).c_str(),
                  HumanSize((uint64_t)pi.totalBytes).c_str(), pr * 100);
    Txt(dc, { m, y, rc.right - m, y + 20 * sc }, st, Fnt(F_SMALL),
        pi.state == 5 ? CL_ERR : pi.state == 4 ? CL_OK : CL_SUB, DT_LEFT);
    y += 26 * sc;
    if (pi.state == 5 && !pi.error.empty())
        Txt(dc, { m, y, rc.right - m, y + 36 * sc }, pi.error, Fnt(F_SMALL), CL_ERR, DT_LEFT | DT_WORDBREAK);
    if (pi.state == 4)
        Txt(dc, { m, y, rc.right - m, y + 36 * sc }, T(S3_WIZ_READY), Fnt(F_SMALL), CL_OK, DT_LEFT | DT_WORDBREAK);

    // buttons
    bool downloading = pi.state >= 1 && pi.state <= 3;
    int bh = 38 * sc, bw = 150 * sc, by = rc.bottom - m - bh;
    int bx = m;
    Btn(dc, WI_DOWNLOAD, { bx, by, bx + bw, by + bh }, T(S3_WIZ_DOWNLOAD), BSTY_PRIMARY, !downloading); bx += bw + 10 * sc;
    Btn(dc, WI_OFFZIP,   { bx, by, bx + bw, by + bh }, T(S3_WIZ_OFFZIP), BSTY_SURF, !downloading); bx += bw + 10 * sc;
    Btn(dc, WI_VERIFY,   { bx, by, bx + bw, by + bh }, T(S3_WIZ_VERIFY), BSTY_SURF, !downloading); bx += bw + 10 * sc;
    Btn(dc, WI_FOLDER,   { bx, by, bx + bw, by + bh }, T(S_BTN_OPENFOLDER), BSTY_SURF);
    RECT cr = { rc.right - m - 120 * sc, by, rc.right - m, by + bh };
    Btn(dc, WI_CLOSE, cr, downloading ? T(S3_WIZ_HIDE) : T(S_BTN_CANCEL), BSTY_GHOST);
}

static bool WizardClick(int id) {
    switch (id) {
    case WI_DOWNLOAD:
        PkgStartDownloadAsync();
        return false;
    case WI_OFFZIP: {
        std::wstring z = PickFile(g_wiz->h,
            L"NovaDroid package (.zip)\0*.zip\0All files\0*.*\0", T(S3_WIZ_PICKZIP), L"zip");
        if (!z.empty()) {
            std::wstring err;
            PkgUseOfflineZip(z, err);
            if (!err.empty()) V3Notify(err, 2);
        }
        return false;
    }
    case WI_FOLDER:
        OpenInExplorer(PkgRoot());
        return false;
    case WI_CLOSE:
        return true;
    }
    return false;
}

static LRESULT CALLBACK WizProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    WizDlg* d = g_wiz;
    switch (m) {
    case WM_CREATE: SetTimer(h, 1, 250, nullptr); return 0;
    case WM_TIMER:
        if (PkgInfo().state == 4) {
            // auto close shortly after success
            static long long doneAt = 0;
            if (!doneAt) doneAt = GetTickCount64();
            if (GetTickCount64() - doneAt > 1200) { doneAt = 0; PostMessageW(h, WM_CLOSE, 0, 0); }
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        if (!d) break;
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(CL_SURF);
        FillRect(mem, &rc, bg); DeleteObject(bg);
        g_wids.clear(); g_hover = -1; g_press = -1;
        WizardDraw(mem, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = -1;
        for (auto& w : g_wids)
            if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
        SetCursor(hit > 0 ? LoadCursorW(nullptr, IDC_HAND) : LoadCursorW(nullptr, IDC_ARROW));
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = -1;
        for (auto& w : g_wids)
            if (p.x >= w.r.left && p.x < w.r.right && p.y >= w.r.top && p.y < w.r.bottom) { hit = w.id; break; }
        if (hit > 0 && WizardClick(hit)) PostMessageW(h, WM_CLOSE, 0, 0);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        if (d) d->done = true;
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void V3DownloadWizard(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = WizProc; wc.hInstance = g_hi;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
        wc.lpszClassName = L"NovaDroidPkgWiz"; RegisterClassExW(&wc);
        registered = true;
    }
    WizDlg d;
    g_wiz = &d;
    int sc = g_dpi / 96;
    RECT pr; GetWindowRect(parent, &pr);
    int W = 660 * sc, H = 560 * sc;
    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, L"NovaDroidPkgWiz", T(S3_WIZ_TITLE),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        pr.left + ((pr.right - pr.left) - W) / 2, pr.top + ((pr.bottom - pr.top) - H) / 2,
        W, H, parent, nullptr, g_hi, nullptr);
    d.h = hw;
    EnableWindow(parent, FALSE);
    ShowWindow(hw, SW_SHOW);
    MSG msg;
    while (!d.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    g_wiz = nullptr;
    UiInvalidate();
}

// ================================================================ gaming settings dialog (TZ3 14.2)
enum {
    G_GPU = 201, G_BK, G_FPS, G_SCALE, G_FULL, G_BL, G_TOP, G_VSYNC, G_SND, G_VOL,
    G_MUTE, G_OVL, G_QS, G_HOT, G_SX, G_SY, G_INV, G_OK, G_CANCEL, G_PROBE, G_PROFILE
};

struct GamDlg {
    InstanceCfg cfg;
    bool ok = false;
    HWND hGpu, hBk, hFps, hScale, hProfile, hVol, hSx, hSy;
    HWND hFull, hBl, hTop, hVsync, hSnd, hMute, hOvl, hQs, hInv;
    HWND hHot, hOk, hCancel, hProbe;
};
static GamDlg* g_gam = nullptr;

static void GamAddCombo(HWND h, const std::wstring& s) { SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)s.c_str()); }

static LRESULT CALLBACK GamProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == G_HOT && code == BN_CLICKED) {
            int vk = 0;
            if (DialogKeyCapture(h, vk)) {
                g_gam->cfg.captureHotkey = vk;
                SetWindowTextW(g_gam->hHot, Fmt(L"%s [F12: %s]", T(S3_G_HOTKEY), KeymapVkName(vk).c_str()).c_str());
            }
            return 0;
        }
        if (id == G_PROBE && code == BN_CLICKED) {
            V3GpuProbeQemu();
            GpuInfo g = V3DetectGpu();
            GpuPlan p = V3ResolveGpu(g_gam->cfg);
            std::wstring msg = g.name + L"\r\n" + Fmt(L"%s: %s", T(S3_G_RESOLVED), p.resolved.c_str());
            if (!p.fallbackNote.empty()) msg += L"\r\n" + p.fallbackNote;
            MessageBoxW(h, msg.c_str(), T(S3_G_PROBE), MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == G_OK && code == BN_CLICKED) {
            GamDlg* d = g_gam;
            wchar_t buf[64];
            int ci = (int)SendMessageW(d->hGpu, CB_GETCURSEL, 0, 0);
            d->cfg.gpuMode = ci == 1 ? L"host" : ci == 2 ? L"swiftshader" : ci == 3 ? L"software" : L"auto";
            ci = (int)SendMessageW(d->hBk, CB_GETCURSEL, 0, 0);
            d->cfg.gpuBackend = ci == 1 ? L"opengl" : ci == 2 ? L"vulkan" : L"auto";
            ci = (int)SendMessageW(d->hFps, CB_GETCURSEL, 0, 0);
            const int fpsT[] = { 30, 45, 60, 90, 120, 0 };
            d->cfg.fpsLimit = fpsT[ci < 0 ? 2 : ci];
            ci = (int)SendMessageW(d->hScale, CB_GETCURSEL, 0, 0);
            d->cfg.scaleMode = ci == 1 ? L"stretch" : ci == 2 ? L"fill" : ci == 3 ? L"native" : L"fit";
            ci = (int)SendMessageW(d->hProfile, CB_GETCURSEL, 0, 0);
            d->cfg.perfProfile = ci == 1 ? L"eco" : ci == 2 ? L"gaming" : ci == 3 ? L"custom" : L"balanced";
            GetWindowTextW(d->hVol, buf, 64);   d->cfg.soundVolume = ClampI(_wtoi(buf), 0, 100, 80);
            GetWindowTextW(d->hSx, buf, 64);    d->cfg.sensX = ClampI(_wtoi(buf), 10, 400, 100);
            GetWindowTextW(d->hSy, buf, 64);    d->cfg.sensY = ClampI(_wtoi(buf), 10, 400, 100);
            d->cfg.fullscreen     = SendMessageW(d->hFull, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.borderless     = SendMessageW(d->hBl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.topMost        = SendMessageW(d->hTop, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.vsync          = SendMessageW(d->hVsync, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.soundEnabled   = SendMessageW(d->hSnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.muteOnMinimize = SendMessageW(d->hMute, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.fpsOverlay     = SendMessageW(d->hOvl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.quickStart     = SendMessageW(d->hQs, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->cfg.invertY        = SendMessageW(d->hInv, BM_GETCHECK, 0, 0) == BST_CHECKED;
            d->ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (id == G_CANCEL && code == BN_CLICKED) { DestroyWindow(h); return 0; }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, CL_TEXT); SetBkColor((HDC)wp, CL_BG);
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    case WM_CTLCOLORBTN:
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool DialogGamingSettings(HWND parent, InstanceCfg& c) {
    GamDlg d; d.cfg = c;
    g_gam = &d;
    int sc = g_dpi / 96;
    int W = 640 * sc, H = 700 * sc;
    RECT pr; GetWindowRect(parent, &pr);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = GamProc; wc.hInstance = g_hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(CL_BG);
    wc.lpszClassName = L"NovaDroidGamDlg";
    RegisterClassExW(&wc);
    HWND h = CreateWindowExW(0, L"NovaDroidGamDlg", T(S3_G_TITLE),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        pr.left + ((pr.right - pr.left) - W) / 2, pr.top + ((pr.bottom - pr.top) - H) / 2,
        W, H, parent, nullptr, g_hi, nullptr);
    BOOL on = TRUE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));

    HFONT f = Fnt(F_BODY), fs = Fnt(F_SMALL);
    auto label = [&](const wchar_t* s, int X, int Y, int Wd) {
        HWND l = CreateWindowExW(0, L"STATIC", s, WS_CHILD | WS_VISIBLE, X, Y, Wd, 20 * sc, h, nullptr, g_hi, nullptr);
        SendMessageW(l, WM_SETFONT, (WPARAM)fs, TRUE);
    };
    auto combo = [&](int id, int X, int Y, int Wd) {
        HWND c = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            CBS_DROPDOWNLIST | WS_VSCROLL, X, Y, Wd, 400, h, (HMENU)(INT_PTR)id, g_hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    auto check = [&](int id, const std::wstring& t, bool v, int X, int Y, int Wd) {
        HWND b = CreateWindowExW(0, L"BUTTON", t.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            BS_AUTOCHECKBOX, X, Y, Wd, 24 * sc, h, (HMENU)(INT_PTR)id, g_hi, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(b, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
        return b;
    };
    auto edit = [&](int id, const std::wstring& v, int X, int Y, int Wd) {
        HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", v.c_str(), WS_CHILD | WS_VISIBLE |
            WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, X, Y, Wd, 28 * sc, h, (HMENU)(INT_PTR)id, g_hi, nullptr);
        SendMessageW(e, WM_SETFONT, (WPARAM)f, TRUE);
        return e;
    };
    auto btn = [&](int id, const std::wstring& t, int X, int Y, int Wd) {
        HWND b = CreateWindowExW(0, L"BUTTON", t.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            BS_PUSHBUTTON, X, Y, Wd, 34 * sc, h, (HMENU)(INT_PTR)id, g_hi, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)f, TRUE);
        return b;
    };

    int m = 22 * sc, cw = W - 2 * m, colw = cw / 2 - 12 * sc;
    int y = 18 * sc;
    label(T(S3_G_GPUMODE), m, y, colw);
    label(T(S3_G_BACKEND), m + colw + 24 * sc, y, colw); y += 22 * sc;
    d.hGpu = combo(G_GPU, m, y, colw); d.hBk = combo(G_BK, m + colw + 24 * sc, y, colw); y += 44 * sc;
    label(T(S_DLG_FPS), m, y, colw);
    label(T(S3_G_SCALE), m + colw + 24 * sc, y, colw); y += 22 * sc;
    d.hFps = combo(G_FPS, m, y, colw); d.hScale = combo(G_SCALE, m + colw + 24 * sc, y, colw); y += 44 * sc;
    label(T(S3_G_PROFILE), m, y, colw); y += 22 * sc;
    d.hProfile = combo(G_PROFILE, m, y, colw); y += 44 * sc;

    y += 6 * sc;
    int cx = m;
    d.hFull = check(G_FULL, T(S_DLG_FULL), d.cfg.fullscreen, cx, y, colw);
    d.hBl = check(G_BL, T(S3_G_BORDERLESS), d.cfg.borderless, cx + colw + 24 * sc, y, colw); y += 28 * sc;
    d.hTop = check(G_TOP, T(S3_G_TOPMOST), d.cfg.topMost, cx, y, colw);
    d.hVsync = check(G_VSYNC, T(S3_G_VSYNC), d.cfg.vsync, cx + colw + 24 * sc, y, colw); y += 28 * sc;
    d.hOvl = check(G_OVL, T(S3_G_OVERLAY), d.cfg.fpsOverlay, cx, y, colw);
    d.hQs = check(G_QS, T(S3_G_QUICKSTART), d.cfg.quickStart, cx + colw + 24 * sc, y, colw); y += 34 * sc;

    // sound block
    label(T(S3_G_VOLUME), m, y, 120 * sc);
    label(L"X %", m + 200 * sc, y, 40 * sc);
    label(L"Y %", m + 280 * sc, y, 40 * sc); y += 22 * sc;
    d.hVol = edit(G_VOL, std::to_wstring(d.cfg.soundVolume), m, y, 70 * sc);
    d.hSx = edit(G_SX, std::to_wstring(d.cfg.sensX), m + 200 * sc, y, 60 * sc);
    d.hSy = edit(G_SY, std::to_wstring(d.cfg.sensY), m + 280 * sc, y, 60 * sc);
    d.hInv = check(G_INV, T(S3_G_INVY), d.cfg.invertY, m + 380 * sc, y + 2 * sc, 180 * sc); y += 36 * sc;
    d.hSnd = check(G_SND, T(S_DLG_SOUND), d.cfg.soundEnabled, cx, y, colw);
    d.hMute = check(G_MUTE, T(S3_G_MUTEMIN), d.cfg.muteOnMinimize, cx + colw + 24 * sc, y, colw); y += 34 * sc;

    d.hHot = btn(G_HOT, Fmt(L"%s [%s]", T(S3_G_HOTKEY), KeymapVkName(d.cfg.captureHotkey).c_str()), m, y, cw);
    y += 44 * sc;
    d.hProbe = btn(G_PROBE, T(S3_G_PROBE), m, y, cw);
    y += 46 * sc;
    d.hOk = btn(G_OK, T(S_BTN_SAVE), m, y, 160 * sc);
    d.hCancel = btn(G_CANCEL, T(S_BTN_CANCEL), m + 176 * sc, y, 140 * sc);

    // fill combos
    GamAddCombo(d.hGpu, T(S3_GPU_AUTO)); GamAddCombo(d.hGpu, T(S3_GPU_HOST));
    GamAddCombo(d.hGpu, T(S3_GPU_SWIFT)); GamAddCombo(d.hGpu, T(S3_GPU_SOFT));
    SendMessageW(d.hGpu, CB_SETCURSEL,
        d.cfg.gpuMode == L"host" ? 1 : d.cfg.gpuMode == L"swiftshader" ? 2 : d.cfg.gpuMode == L"software" ? 3 : 0, 0);
    GamAddCombo(d.hBk, T(S3_BK_AUTO)); GamAddCombo(d.hBk, L"OpenGL"); GamAddCombo(d.hBk, L"Vulkan");
    SendMessageW(d.hBk, CB_SETCURSEL,
        d.cfg.gpuBackend == L"opengl" ? 1 : d.cfg.gpuBackend == L"vulkan" ? 2 : 0, 0);
    const wchar_t* fpsS[] = { L"30", L"45", L"60", L"90", L"120", T(S3_FPS_OFF) };
    for (int i = 0; i < 6; ++i) GamAddCombo(d.hFps, fpsS[i]);
    SendMessageW(d.hFps, CB_SETCURSEL,
        d.cfg.fpsLimit == 30 ? 0 : d.cfg.fpsLimit == 45 ? 1 : d.cfg.fpsLimit == 90 ? 3 : d.cfg.fpsLimit == 120 ? 4 : d.cfg.fpsLimit == 0 ? 5 : 2, 0);
    GamAddCombo(d.hScale, T(S3_SC_FIT)); GamAddCombo(d.hScale, T(S3_SC_STRETCH));
    GamAddCombo(d.hScale, T(S3_SC_FILL)); GamAddCombo(d.hScale, T(S3_SC_NATIVE));
    SendMessageW(d.hScale, CB_SETCURSEL,
        d.cfg.scaleMode == L"stretch" ? 1 : d.cfg.scaleMode == L"fill" ? 2 : d.cfg.scaleMode == L"native" ? 3 : 0, 0);
    GamAddCombo(d.hProfile, T(S_PERF_BAL)); GamAddCombo(d.hProfile, T(S_PERF_ECO));
    GamAddCombo(d.hProfile, T(S_PERF_HIGH)); GamAddCombo(d.hProfile, T(S_PERF_CUSTOM));
    SendMessageW(d.hProfile, CB_SETCURSEL,
        d.cfg.perfProfile == L"eco" ? 1 : d.cfg.perfProfile == L"gaming" ? 2 : d.cfg.perfProfile == L"custom" ? 3 : 0, 0);

    EnableWindow(parent, FALSE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsWindow(h)) break;
        if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    g_gam = nullptr;
    if (d.ok) c = d.cfg;
    return d.ok;
}

// ================================================================ page sections
// settings page: full-package block
void PageSettingsPkgSection(HDC dc, RECT rc, int* yOut) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = *yOut;
    int rowH = MulDiv(52, g_dpi, 96);
    RECT hdr = { x0, y, rc.right - m, y + 26 * (g_dpi / 96) };
    Txt(dc, hdr, T(S3_PKG_TITLE), Fnt(F_H2), CL_TEXT, DT_LEFT);
    y += 32 * (g_dpi / 96);

    PkgStateInfo pi = PkgInfo();
    std::vector<std::wstring> missing;
    bool missingAny = PkgMissingCritical(missing);
    std::wstring status;
    if (PkgInstalled()) {
        status = Fmt(L"%s v%s", T(S3_PKG_OK), PkgInstalledVersion().c_str());
        if (missingAny) status += L" · " + std::wstring(T(S3_PKG_PARTIAL));
    } else if (pi.state >= 1 && pi.state <= 3) {
        status = pi.stageText;
    } else if (pi.state == 5) {
        status = std::wstring(T(S3_PKG_ERROR)) + L": " + pi.error;
    } else {
        status = T(S3_PKG_NO);
    }

    // progress bar while active
    if (pi.state >= 1 && pi.state <= 3) {
        int barH = MulDiv(12, g_dpi, 96);
        RECT bar = { x0, y, rc.right - m, y + barH };
        FillRound(dc, bar, CL_BG, 6);
        RECT fill = bar;
        fill.right = bar.left + (LONG)((bar.right - bar.left) * (pi.state == 4 ? 1.0f : pi.progress));
        if (fill.right > fill.left + 2) GradientRect(dc, fill, Accent(), CL_ACCENT2);
        FrameRound(dc, bar, CL_BORDER, 6);
        y += barH + 8 * (g_dpi / 96);
    }

    {
        RECT r = { x0, y, rc.right - m, y + rowH };
        FillRound(dc, r, CL_SURF, 12); FrameRound(dc, r, CL_BORDER, 12);
        RECT sr = { r.left + m, y, r.left + MulDiv(430, g_dpi, 96), y + rowH };
        Txt(dc, sr, status, Fnt(F_SMALL), pi.state == 5 ? CL_ERR : PkgInstalled() ? CL_OK : CL_WARN,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bw = MulDiv(140, g_dpi, 96), bh = MulDiv(32, g_dpi, 96);
        int bx = r.right - m - bw;
        bool busy = pi.state >= 1 && pi.state <= 3;
        Btn(dc, ID3_PKG0 + 0, { bx, y + (rowH - bh) / 2, bx + bw, y + (rowH + bh) / 2 },
            PkgInstalled() ? T(S3_PKG_REDOWNLOAD) : T(S3_PKG_DOWNLOAD), BSTY_PRIMARY, !busy);
        bx -= bw + m / 2;
        Btn(dc, ID3_PKG0 + 1, { bx, y + (rowH - bh) / 2, bx + bw, y + (rowH + bh) / 2 }, T(S3_WIZ_OFFZIP), BSTY_SURF);
        bx -= bw + m / 2;
        Btn(dc, ID3_PKG0 + 2, { bx, y + (rowH - bh) / 2, bx + bw, y + (rowH + bh) / 2 }, T(S3_WIZ_VERIFY), BSTY_SURF);
        bx -= MulDiv(60, g_dpi, 96);
        Btn(dc, ID3_PKG0 + 3, { bx, y + (rowH - bh) / 2, bx + MulDiv(56, g_dpi, 96), y + (rowH + bh) / 2 }, L"…", BSTY_SURF);
        y += rowH + m / 2;
    }
    *yOut = y;
}

// perf page: gaming profiles row
void PagePerfProfilesRow(HDC dc, RECT rc, int* yOut) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = *yOut;
    int rowH = MulDiv(56, g_dpi, 96);
    RECT r = { x0, y, rc.right - m, y + rowH };
    FillRound(dc, r, CL_SURF, 12); FrameRound(dc, r, CL_BORDER, 12);
    RECT tr = { r.left + m, y, r.left + MulDiv(300, g_dpi, 96), y + rowH };
    Txt(dc, tr, T(S3_PROF_TITLE), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int bw = MulDiv(150, g_dpi, 96), bh = MulDiv(34, g_dpi, 96);
    int bx = r.right - m - bw;
    const wchar_t* names[3] = { T(S_PERF_ECO), T(S_PERF_BAL), T(S_PERF_HIGH) };
    for (int i = 2; i >= 0; --i) {
        Btn(dc, ID3_PROF0 + i, { bx, y + (rowH - bh) / 2, bx + bw, y + (rowH + bh) / 2 },
            names[i], i == 2 ? BSTY_PRIMARY : BSTY_SURF);
        bx -= bw + m / 2;
    }
    y += rowH + m / 2;
    RECT hr = { x0, y, rc.right - m, y + 20 * (g_dpi / 96) };
    Txt(dc, hr, T(S3_PROF_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT);
    y += 24 * (g_dpi / 96);
    *yOut = y;
}

// diag page: GPU status card
void PageDiagGpuBlock(HDC dc, RECT rc, int* yOut) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int sc = g_dpi / 96;
    int y = *yOut;
    int cardH = MulDiv(96, g_dpi, 96);
    RECT r = { x0, y, rc.right - m, y + cardH };
    FillRound(dc, r, CL_SURF, 12); FrameRound(dc, r, CL_BORDER, 12);
    GpuInfo g = V3DetectGpu();
    GpuPlan p = V3ResolveGpu(InstanceCfg{});
    RECT lr = { r.left + m, y + 10 * sc, r.left + MulDiv(460, g_dpi, 96), y + 34 * sc };
    Txt(dc, lr, T(S3_GPU_TITLE), Fnt(F_H2), CL_TEXT, DT_LEFT);
    RECT vr = { r.left + m, y + 36 * sc, r.right - m, y + cardH - 10 * sc };
    std::wstring line = g.dxgiOk ? g.name : L"GPU: ?";
    if (g.driverVer != L"?") line += L" · " + g.driverVer;
    line += Fmt(L"\r\n%s: %s%s", T(S3_G_RESOLVED), p.resolved.c_str(),
                p.fallbackNote.empty() ? L"" : L" · fallback");
    Txt(dc, vr, line, Fnt(F_SMALL), g.whpx && p.resolved == L"host" ? CL_OK : CL_WARN, DT_LEFT);
    int bw = MulDiv(130, g_dpi, 96), bh = MulDiv(34, g_dpi, 96);
    Btn(dc, ID3_GPU_TEST, { r.right - m - bw, y + (cardH - bh) / 2, r.right - m, y + (cardH + bh) / 2 },
        T(S3_G_PROBE), BSTY_SURF);
    y += cardH + m / 2;
    *yOut = y;
}

// ================================================================ glue: init / timer / click
void V3Init() {
    PkgLoadMarker();
    V3GpuProbeQemu();
    V3InputHookStart();
    LogW(L"v3", L"gaming core init: pkg installed=%d", PkgInstalled() ? 1 : 0);
}

void V3Shutdown() {
    V3InputHookStop();
    V3OverlayHideAll();
    V3RestoreAllWindows();
}

static std::set<DWORD> g_seenPids;
static int g_bootTicks = 0;
static bool g_wizardShown = false;

void V3TimerTick() {
    static int tick = 0;
    tick++;
    RefreshQemuWindows();

    // per-instance window services
    {
        std::lock_guard<std::mutex> lk(g_mx);
        std::set<DWORD> now;
        for (auto& kv : g_rt) {
            if (!kv.second.hProc || !ProcAlive(kv.second.hProc) || !kv.second.qhWnd) continue;
            now.insert(kv.second.pid);
            if (!g_seenPids.count(kv.second.pid)) {
                g_seenPids.insert(kv.second.pid);
                InstanceCfg* c = Inst(kv.first);
                if (c) {
                    V3BorderlessApply(kv.first);
                    if (c->fpsOverlay) V3OverlayShow(kv.first, true);
                    ILog(kv.first, L"launcher", L"window services attached (fps overlay, borderless, hotkeys)");
                }
            }
        }
        for (DWORD gone : g_seenPids)
            if (!now.count(gone)) {
                g_seenPids.erase(gone);
                V3OverlayShow(L"", false);
            }
    }

    V3WatchdogTick();
    V3AudioMinimizeTick();
    if (tick % 2 == 0) V3OverlayTick();

    // first-launch bootstrap wizard
    if (!PkgInstalled() && !g_wizardShown && g_wnd && ++g_bootTicks >= 4 && !IsIconic(g_wnd)) {
        g_wizardShown = true;
        V3DownloadWizard(g_wnd);
    }
}

bool V3Click(int id) {
    if (id >= ID3_PKG0 && id < ID3_PKG0 + 4) {
        int slot = id - ID3_PKG0;
        if (slot == 0) {
            if (PkgStartDownloadAsync()) V3Notify(T(S3_PKG_STARTED), 3);
            else V3Notify(T(S3_PKG_BUSY), 1);
            return true;
        }
        if (slot == 1) {
            std::wstring z = PickFile(g_wnd, L"NovaDroid package (.zip)\0*.zip\0All files\0*.*\0",
                                      T(S3_WIZ_PICKZIP), L"zip");
            if (!z.empty()) {
                std::wstring err;
                PkgUseOfflineZip(z, err);
                if (!err.empty()) V3Notify(err, 2);
                else V3Notify(T(S3_PKG_STARTED), 3);
            }
            return true;
        }
        if (slot == 2) {
            RunOpThread([] {
                std::wstring bad; int okC = 0, totC = 0;
                if (PkgVerifyAll(bad, &okC, &totC))
                    V3Notify(Fmt(L"%s (%d/%d)", T(S3_PKG_VOK), okC, totC), 0);
                else
                    V3Notify(Fmt(L"%s: %s", T(S3_PKG_VFAIL), bad.c_str()), 2);
            });
            return true;
        }
        if (slot == 3) { OpenInExplorer(PkgRoot()); return true; }
    }
    if (id >= ID3_PROF0 && id < ID3_PROF0 + 3) {
        static const wchar_t* ids[3] = { L"eco", L"balanced", L"gaming" };
        std::wstring target = g_perfSel.empty() ? SelInst() : g_perfSel;
        if (target.empty()) { V3Notify(T(S_PERF2_NOINST), 1); return true; }
        std::wstring err;
        if (V3ApplyProfile(target, ids[id - ID3_PROF0], err))
            V3Notify(Fmt(L"%s «%s»", T(S3_PROF_APPLIED), T(S_PERF_ECO + (id - ID3_PROF0))), 0);
        else
            V3Notify(err, 2);
        UiInvalidate();
        return true;
    }
    if (id == ID3_GPU_TEST) {
        V3GpuProbeQemu();
        RunOpThread([] {
            SleepMs(2500);                      // let the qemu probe finish
            GpuInfo g = V3DetectGpu();
            GpuPlan p = V3ResolveGpu(InstanceCfg{});
            std::wstring msg = g.name + Fmt(L" (%lld MB)\r\n%s: %s", g.vramMb,
                T(S3_G_RESOLVED), p.resolved.c_str());
            V3Notify(msg, g.whpx ? 0 : 1);
        });
        return true;
    }
    return false;
}
