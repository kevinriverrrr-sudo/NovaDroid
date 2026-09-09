// ============================================================================
//  NovaDroid - v3input.cpp  Stage 3: window modes (fullscreen/borderless/topmost),
//  mouse capture (QEMU GTK grab), FPS overlay (screen-diff estimate),
//  WASAPI per-process volume + mute-on-minimize, global hotkeys via LL hook.
// ============================================================================
#include "v3.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <dwmapi.h>

// ---------------------------------------------------------------- window cache
static std::mutex g_inMx;
static std::map<DWORD, HWND> g_qhWnds;          // pid -> qemu window
static std::map<HWND, WINDOWPLACEMENT> g_fsSaved;
static std::map<HWND, LONG_PTR> g_blSaved;      // borderless: saved style
static std::atomic<bool> g_captured{ false };
static HWND g_overlay = nullptr;
static std::wstring g_overlayId;

HWND V3FindQemuWindow(DWORD pid) {
    struct Ctx { DWORD pid; HWND hwnd; } ctx = { pid, nullptr };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        Ctx* c = (Ctx*)lp;
        DWORD pid2 = 0;
        GetWindowThreadProcessId(h, &pid2);
        if (pid2 != c->pid) return TRUE;
        if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
        LONG st = GetWindowLongW(h, GWL_STYLE);
        if (st & WS_CHILD) return TRUE;
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        // GTK top-level or SDL window
        if (wcsstr(cls, L"gdk") || wcsstr(cls, L"SDL") || wcsstr(cls, L"qemu") ||
            GetWindowTextLengthW(h) > 0) {
            c->hwnd = h;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.hwnd;
}

void RefreshQemuWindows() {
    std::lock_guard<std::mutex> lk(g_inMx);
    std::map<DWORD, HWND> fresh;
    std::lock_guard<std::mutex> lk2(g_mx);
    for (auto& kv : g_rt) {
        if (kv.second.hProc && ProcAlive(kv.second.hProc)) {
            HWND h = kv.second.qhWnd;
            if (!h || !IsWindow(h)) h = V3FindQemuWindow(kv.second.pid);
            kv.second.qhWnd = h;
            if (h) fresh[kv.second.pid] = h;
        } else {
            kv.second.qhWnd = nullptr;
        }
    }
    g_qhWnds = fresh;
}

static HWND QemuHwndOfLocked(const std::wstring& id) {
    // caller holds g_mx
    Runtime* r = Rt(id);
    if (!r || !r->hProc) return nullptr;
    if (!r->qhWnd || !IsWindow(r->qhWnd)) r->qhWnd = V3FindQemuWindow(r->pid);
    return r->qhWnd;
}

// ---------------------------------------------------------------- fullscreen / borderless
static void MakeFullscreen(HWND h, bool on) {
    if (on) {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        GetWindowPlacement(h, &wp);
        g_fsSaved[h] = wp;
        LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
        SetWindowLongPtrW(h, GWL_STYLE, st & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
        HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        auto it = g_fsSaved.find(h);
        if (it == g_fsSaved.end()) return;
        LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
        SetWindowLongPtrW(h, GWL_STYLE, st | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetWindowPlacement(h, &it->second);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        g_fsSaved.erase(it);
    }
}

void V3FullscreenToggle(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(g_mx);
    HWND h = QemuHwndOfLocked(id);
    if (!h) return;
    MakeFullscreen(h, g_fsSaved.find(h) == g_fsSaved.end());
}

void V3BorderlessApply(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* c = Inst(id);
    HWND h = QemuHwndOfLocked(id);
    if (!c || !h) return;
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    if (c->borderless) {
        if (g_blSaved.find(h) == g_blSaved.end()) g_blSaved[h] = st;
        SetWindowLongPtrW(h, GWL_STYLE, st & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    } else if (g_blSaved.find(h) != g_blSaved.end()) {
        SetWindowLongPtrW(h, GWL_STYLE, g_blSaved[h]);
        g_blSaved.erase(h);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    if (c->topMost) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    else SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void V3RestoreAllWindows() {
    for (auto& kv : g_fsSaved) if (IsWindow(kv.first)) MakeFullscreen(kv.first, false);
    g_fsSaved.clear();
}

// ---------------------------------------------------------------- mouse capture (TZ3 9.1)
bool V3CaptureActive() { return g_captured.load(); }

bool V3CaptureToggle(const std::wstring& id) {
    HWND h = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        h = QemuHwndOfLocked(id);
    }
    if (!h) return false;
    // QEMU GTK: Ctrl+Alt+G toggles input grab; SDL: Ctrl+Alt grabs.
    // Send synthetic combo while qemu window is foreground.
    HWND fg = GetForegroundWindow();
    if (fg != h) {
        SetForegroundWindow(h);
        SleepMs(60);
    }
    bool want = !g_captured.load();
    const WORD vk[] = { VK_CONTROL, VK_MENU, L'G' };
    for (int i = 0; i < 3; ++i) keybd_event(vk[i], 0, 0, 0);
    for (int i = 2; i >= 0; --i) keybd_event(vk[i], 0, KEYEVENTF_KEYUP, 0);
    g_captured.store(want);
    if (want) {
        RECT rc; GetClientRect(h, &rc);
        POINT pt = { 0, 0 }; ClientToScreen(h, &pt);
        RECT scr = { pt.x, pt.y, pt.x + rc.right, pt.y + rc.bottom };
        ClipCursor(&scr);                     // extra guarantee
        ShowCursor(FALSE);
    } else {
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }
    return want;
}

// ---------------------------------------------------------------- global hotkeys (TZ3 8.2, 9.1)
static HHOOK g_kbdHook = nullptr;
static bool FindInstanceByHwnd(HWND h, std::wstring& outId, InstanceCfg& outCfg) {
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& kv : g_rt) {
        if (kv.second.hProc && kv.second.qhWnd == h) {
            outId = kv.first;
            InstanceCfg* c = Inst(kv.first);
            if (c) outCfg = *c;
            return true;
        }
    }
    return false;
}

static LRESULT CALLBACK KbdHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        HWND fg = GetForegroundWindow();
        std::wstring id;
        InstanceCfg c;
        if (fg && FindInstanceByHwnd(fg, id, c)) {
            bool fsActive = false;
            { std::lock_guard<std::mutex> lk(g_inMx); fsActive = g_fsSaved.count(fg) != 0; }
            int hot = c.captureHotkey == 0 ? VK_RCONTROL : c.captureHotkey;
            if (k->vkCode == VK_F11) { V3FullscreenToggle(id); return 1; }
            if (k->vkCode == VK_RETURN && (GetAsyncKeyState(VK_MENU) & 0x8000)) { V3FullscreenToggle(id); return 1; }
            if (k->vkCode == VK_ESCAPE && fsActive) { V3FullscreenToggle(id); return 1; }
            if (k->vkCode == hot) { V3CaptureToggle(id); return 1; }
        }
    }
    return CallNextHookEx(g_kbdHook, code, wp, lp);
}

void V3InputHookStart() {
    if (!g_kbdHook)
        g_kbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbdHook, GetModuleHandleW(nullptr), 0);
}
void V3InputHookStop() {
    if (g_kbdHook) { UnhookWindowsHookEx(g_kbdHook); g_kbdHook = nullptr; }
}

// ---------------------------------------------------------------- FPS overlay (TZ3 7.1)
static float g_fps = 0; static float g_frameMs = 0; static int g_dropped = 0;

static void OverlayRender() {
    if (!g_overlay) return;
    int W = 190, H = 44;
    RECT r; GetWindowRect(g_overlay, &r);
    W = r.right - r.left; H = r.bottom - r.top;
    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi); bi.bV5Width = W; bi.bV5Height = -H; bi.bV5Planes = 1;
    bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(sdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    // translucent rounded bg
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            DWORD* px = (DWORD*)((BYTE*)bits + y * W * 4) + x;
            *px = 0xB8111318;                        // ARGB
        }
    SetBkMode(mdc, TRANSPARENT);
    HFONT f = CreateFontW(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT f2 = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    InstanceCfg* c = nullptr;
    { std::lock_guard<std::mutex> lk(g_mx); c = Inst(g_overlayId); }
    int target = c ? c->fpsLimit : 60;
    std::wstring mode = c ? c->gpuMode : L"auto";
    std::wstring l1 = Fmt(L"FPS %d · %.1f ms", (int)(g_fps + 0.5f), g_frameMs);
    std::wstring l2 = Fmt(L"target %d · %s%s", target, mode.c_str(),
                          g_dropped > 0 ? L" · drops" : L"");
    SetTextColor(mdc, (g_fps >= (float)target * 0.85f) ? RGB(0x38, 0xC9, 0x76) :
                 (g_fps >= (float)target * 0.5f ? RGB(0xF3, 0xA9, 0x3C) : RGB(0xED, 0x5D, 0x64)));
    RECT r1 = { 10, 3, W, 24 };
    DrawTextW(mdc, l1.c_str(), -1, &r1, DT_LEFT | DT_SINGLELINE);
    SetTextColor(mdc, RGB(0xA8, 0xB0, 0xC0));
    RECT r2 = { 10, 22, W, 40 };
    DrawTextW(mdc, l2.c_str(), -1, &r2, DT_LEFT | DT_SINGLELINE);
    SelectObject(mdc, old);
    DeleteObject(f); DeleteObject(f2);
    POINT src = { 0, 0 };
    SIZE sz = { W, H };
    POINT dst = { r.left, r.top };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_overlay, sdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);
}

static void OverlayPosition() {
    if (!g_overlay) return;
    HWND q = nullptr;
    { std::lock_guard<std::mutex> lk(g_inMx);
      for (auto& kv : g_qhWnds) if (kv.second) { q = kv.second; break; } }
    if (!q || !IsWindowVisible(q)) { ShowWindow(g_overlay, SW_HIDE); return; }
    if (!IsIconic(q)) ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
    RECT qr; GetWindowRect(q, &qr);
    SetWindowPos(g_overlay, HWND_TOPMOST, qr.right - 210, qr.top + 52, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

static DWORD prevCrc = 0;
static long long prevTickMs = 0;
static int framesSince = 0;
static BYTE prevBuf[96 * 96 * 4];

static void MeasureFps() {
    // sample a small screen region over the qemu window; count visual changes
    HWND q = nullptr;
    { std::lock_guard<std::mutex> lk(g_inMx);
      for (auto& kv : g_qhWnds) if (kv.second) { q = kv.second; break; } }
    if (!q) return;
    RECT qr; GetWindowRect(q, &qr);
    int cx = (qr.left + qr.right) / 2, cy = (qr.top + qr.bottom) / 2;
    if (cx < 0 || cy < 0) return;
    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, 96, 96);
    HGDIOBJ old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, 96, 96, sdc, cx - 48, cy - 48, SRCCOPY);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 96; bi.bmiHeader.biHeight = -96; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    static BYTE buf[96 * 96 * 4];
    GetDIBits(mdc, bmp, 0, 96, buf, &bi, DIB_RGB_COLORS);
    SelectObject(mdc, old);
    DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, sdc);
    if (memcmp(buf, prevBuf, sizeof(buf)) != 0) framesSince++;
    memcpy(prevBuf, buf, sizeof(buf));
    long long now = GetTickCount64();
    if (prevTickMs && now - prevTickMs >= 1000) {
        float fps = (float)framesSince * 1000.0f / (float)(now - prevTickMs);
        if (fps > 240) fps = 240;
        g_dropped = (int)(framesSince % 60 == 0 ? 0 : 0);
        g_fps = g_fps * 0.5f + fps * 0.5f;
        g_frameMs = g_fps > 0.5f ? 1000.0f / g_fps : 0;
        framesSince = 0; prevTickMs = now;
    }
    if (!prevTickMs) prevTickMs = now;
}

void V3OverlayShow(const std::wstring& id, bool show) {
    if (show) {
        if (!g_overlay) {
            g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"STATIC", L"", WS_POPUP, 0, 0, 190, 44, nullptr, nullptr, g_hi, nullptr);
            ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        }
        g_overlayId = id;
        OverlayPosition(); OverlayRender();
    } else if (g_overlay) {
        ShowWindow(g_overlay, SW_HIDE);
    }
}

void V3OverlayHideAll() { V3OverlayShow(L"", false); }

float V3LastFps() { return g_fps; }

void V3OverlayTick() {
    if (!g_overlay || IsWindowVisible(g_overlay) == FALSE) return;
    MeasureFps();
    OverlayPosition();
    OverlayRender();
}

// ---------------------------------------------------------------- audio (TZ3 10)
bool V3AudioSetVolume(DWORD qemuPid, int percent, bool mute) {
    if (percent < 0) percent = 0; if (percent > 100) percent = 100;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        IMMDeviceEnumerator* de = nullptr;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), (void**)&de);
        if (SUCCEEDED(hr) && de) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(de->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
                IAudioSessionManager2* sm = nullptr;
                if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                                            nullptr, (void**)&sm)) && sm) {
                    IAudioSessionEnumerator* se = nullptr;
                    if (SUCCEEDED(sm->GetSessionEnumerator(&se)) && se) {
                        int n = 0; se->GetCount(&n);
                        for (int i = 0; i < n; ++i) {
                            IAudioSessionControl* sc = nullptr;
                            if (FAILED(se->GetSession(i, &sc)) || !sc) continue;
                            IAudioSessionControl2* sc2 = nullptr;
                            if (SUCCEEDED(sc->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&sc2)) && sc2) {
                                DWORD pid = 0; sc2->GetProcessId(&pid);
                                if (pid == qemuPid) {
                                    ISimpleAudioVolume* sv = nullptr;
                                    if (SUCCEEDED(sc2->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&sv)) && sv) {
                                        sv->SetMasterVolume(percent / 100.0f, nullptr);
                                        sv->SetMute(mute ? TRUE : FALSE, nullptr);
                                        sv->Release();
                                        ok = true;
                                    }
                                }
                                sc2->Release();
                            }
                            sc->Release();
                            if (ok) break;
                        }
                        se->Release();
                    }
                    sm->Release();
                }
                dev->Release();
            }
            de->Release();
        }
    }
    CoUninitialize();
    return ok;
}

bool V3AudioApply(const InstanceCfg& c) {
    std::lock_guard<std::mutex> lk(g_mx);
    Runtime* r = Rt(c.id);
    if (!r || !r->hProc || !r->pid) return false;
    bool mute = r->audioMuted;
    bool ok = V3AudioSetVolume(r->pid, c.soundVolume, mute);
    if (ok) ILog(c.id, L"launcher", Fmt(L"audio: volume %d%%, mute=%d", c.soundVolume, mute ? 1 : 0));
    return ok;
}

void V3AudioMinimizeTick() {
    struct Job { DWORD pid; std::wstring id; bool iconic; bool wantMute; };
    std::vector<Job> jobs;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        for (auto& kv : g_rt) {
            if (!kv.second.hProc || !kv.second.pid) continue;
            InstanceCfg* c = Inst(kv.first);
            if (!c || !c->soundEnabled) continue;
            HWND h = kv.second.qhWnd;
            if (!h || !IsWindow(h)) continue;
            bool iconic = IsIconic(h) != FALSE;
            bool wantMute = iconic && c->muteOnMinimize;
            if (wantMute != kv.second.audioMuted) {
                kv.second.audioMuted = wantMute;
                jobs.push_back({ kv.second.pid, kv.first, iconic, wantMute });
            }
        }
    }
    for (auto& j : jobs) {
        V3AudioSetVolume(j.pid, 100, j.wantMute);   // volume applied separately; mute toggles here
        ILog(j.id, L"launcher", j.wantMute ? L"audio muted (minimized)" : L"audio unmuted (restored)");
    }
}

// ---------------------------------------------------------------- profiles (TZ3 11.1)
const std::vector<PerfProfileDef>& V3Profiles() {
    static std::vector<PerfProfileDef> p = {
        { L"eco",      L"Экономный",       L"Economy",  2, 2048, 1280,  720, 240,  30, L"software",    false, false, false, false },
        { L"balanced", L"Сбалансированный", L"Balanced", 4, 4096, 1280,  720, 240,  60, L"auto",         true,  true,  false, false },
        { L"gaming",   L"Игровой",         L"Gaming",   6, 6144, 1920, 1080, 320,  60, L"host",         true,  true,  true,  false },
        { L"gaming2",  L"Игровой 120 FPS", L"Gaming 120 FPS", 6, 8192, 1920, 1080, 320, 120, L"host",      true,  true,  true,  false },
    };
    return p;
}

bool V3ApplyProfile(const std::wstring& instanceId, const std::wstring& profileId, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* c = Inst(instanceId);
    if (!c) { err = L"instance not found"; return false; }
    Runtime* r = Rt(instanceId);
    if (r && r->hProc && ProcAlive(r->hProc)) {
        err = T(S3_PROFILE_RUNNING);              // "остановите инстанс перед сменой профиля"
        return false;
    }
    for (auto& p : V3Profiles()) {
        if (profileId == p.id) {
            c->cpuCores = p.cpu; c->ramMb = p.ramMb;
            c->resW = p.resW; c->resH = p.resH; c->dpi = p.dpi;
            c->fpsLimit = p.fps; c->gpuMode = p.gpu;
            c->soundEnabled = p.sound; c->sharedFolderEnabled = p.shared;
            c->fullscreen = p.fullscreen; c->borderless = p.borderless;
            c->perfProfile = p.id;
            PersistInstance(*c);
            return true;
        }
    }
    err = L"profile not found";
    return false;
}
