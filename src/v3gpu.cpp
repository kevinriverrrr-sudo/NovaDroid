// ============================================================================
//  NovaDroid - v3gpu.cpp  Stage 3 "Gaming Core": GPU detection (DXGI + registry),
//  graphics modes Auto/Host/SwiftShader/Software with automatic fallback (TZ3 6),
//  QEMU command line v3 (virtio-gpu-gl + virgl + gtk/sdl display), FPS limiter.
// ============================================================================
#include "v3.h"
#include <dxgi.h>
#include <atomic>

// ---------------------------------------------------------------- cached state
static std::atomic<int> g_gpuCached{ 0 };       // 0 none, 1 ready
static GpuInfo g_gpu;
static std::atomic<bool> g_qemuProbed{ false };
static std::atomic<bool> g_probeBusy{ false };
static std::mutex g_gpuMx;

// ---------------------------------------------------------------- helpers
static std::wstring RegGetStr(HKEY root, const wchar_t* path, const wchar_t* value) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &h) != ERROR_SUCCESS) return L"";
    wchar_t buf[512] = {}; DWORD sz = sizeof(buf) - sizeof(wchar_t), tp = 0;
    LSTATUS r = RegQueryValueExW(h, value, nullptr, &tp, (BYTE*)buf, &sz);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || (tp != REG_SZ && tp != REG_EXPAND_SZ)) return L"";
    buf[sz / sizeof(wchar_t)] = 0;
    return std::wstring(buf);
}

static bool FileInSystem(const wchar_t* dll) {
    wchar_t p[MAX_PATH] = {};
    GetSystemDirectoryW(p, MAX_PATH);
    wcsncat_s(p, L"\\", MAX_PATH);
    wcsncat_s(p, dll, MAX_PATH);
    return FE(p);
}

// ---------------------------------------------------------------- DXGI detection
static void DetectViaDxgi(GpuInfo& g) {
    HMODULE dx = LoadLibraryW(L"dxgi.dll");
    if (!dx) { g.problem = L"dxgi.dll not loaded"; return; }
    using PFN_CDF1 = HRESULT(WINAPI*)(const IID&, void**);
    auto create = (PFN_CDF1)GetProcAddress(dx, "CreateDXGIFactory1");
    if (!create) { FreeLibrary(dx); return; }
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = create(__uuidof(IDXGIFactory1), (void**)&factory);
    if (FAILED(hr) || !factory) { FreeLibrary(dx); return; }
    IDXGIAdapter1* ad = nullptr;
    // pick the adapter with the largest VRAM that is not a WARP/software adapter
    for (UINT i = 0; factory->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d = {};
        if (SUCCEEDED(ad->GetDesc1(&d))) {
            bool software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            long long mb = (long long)d.DedicatedVideoMemory / (1024 * 1024);
            if (!software) {
                g.dxgiOk = true;
                g.name = d.Description;
                g.vramMb = mb;
                // WHQL flag not exposed per-adapter in DXGI 1.1; assume driver store ok
                g.whql = true;
                ad->Release();
                break;
            }
        }
        ad->Release(); ad = nullptr;
    }
    factory->Release();
    FreeLibrary(dx);
}

// registry GPU driver info (display class)
static void DetectViaRegistry(GpuInfo& g) {
    const wchar_t* cls = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    for (int i = 0; i < 4; ++i) {
        std::wstring path = std::wstring(cls) + L"\\000" + std::to_wstring(i);
        std::wstring desc = RegGetStr(HKEY_LOCAL_MACHINE, path.c_str(), L"MatchingDeviceId");
        std::wstring ver  = RegGetStr(HKEY_LOCAL_MACHINE, path.c_str(), L"DriverVersion");
        std::wstring prv  = RegGetStr(HKEY_LOCAL_MACHINE, path.c_str(), L"ProviderName");
        if (ver.empty()) continue;
        g.driverVer = ver;
        // known-problem detection (TZ3 6.3): very old Intel HD Graphics 2000/3000
        // ship without usable GL 3.3; virgl needs GL 3.3+ on host
        std::wstring low = LowerW(prv) + L" " + LowerW(g.name);
        if (low.find(L"intel") != std::wstring::npos) {
            if (g.name.find(L"HD Graphics 2000") != std::wstring::npos ||
                g.name.find(L"HD Graphics 3000") != std::wstring::npos)
                g.problem = L"intel-hd-2/3k";
        }
        break;
    }
    // vendor OpenGL ICD presence
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGL Drivers", 0,
        KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD subs = 0; RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subs, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        g.openglUser = subs > 0;
        RegCloseKey(k);
    }
}

GpuInfo V3DetectGpu() {
    if (g_gpuCached.load() == 1) { std::lock_guard<std::mutex> lk(g_gpuMx); return g_gpu; }
    GpuInfo g;
    DetectViaDxgi(g);
    DetectViaRegistry(g);
    g.whpx = WhpxAvailable();
    g.virtFirmware = VirtFirmwareEnabled();
    g.vulkan = FileInSystem(L"vulkan-1.dll") || FE(g_p.exeDir + L"\\qemu\\vulkan-1.dll");
    // qemu capabilities (filled by V3GpuProbeQemu if it ran)
    g.gtkDisplay = g_qemuProbed.load() ? g.gtkDisplay : false;
    {
        std::lock_guard<std::mutex> lk(g_gpuMx);
        g_gpu = g;
        g_gpuCached.store(1);
        return g_gpu;
    }
}

// ---------------------------------------------------------------- qemu probe
static void ProbeQemuCaps() {
    std::wstring q = QemuExe();
    if (q.empty()) { g_qemuProbed.store(true); g_probeBusy.store(false); return; }
    DWORD ec = 0; std::string so, se;
    // device list (may exit non-zero on some builds; output still printed)
    RunCapture(q, L"-device help", L"", 15000, &ec, &so, &se);
    std::wstring devs = U2W(so) + U2W(se);
    so.clear(); se.clear();
    RunCapture(q, L"-display help", L"", 15000, &ec, &so, &se);
    std::wstring disp = U2W(so) + U2W(se);
    {
        std::lock_guard<std::mutex> lk(g_gpuMx);
        g_gpu.virtioGl  = devs.find(L"virtio-gpu-gl") != std::wstring::npos;
        bool vg         = devs.find(L"virtio-gpu") != std::wstring::npos || g_gpu.virtioGl;
        g_gpu.gtkDisplay= disp.find(L"gtk") != std::wstring::npos;
        g_gpu.sdlDisplay= disp.find(L"sdl") != std::wstring::npos;
        if (!vg) g_gpu.problem = g_gpu.problem.empty() ? L"no-virtio-gpu" : g_gpu.problem;
        g_qemuProbed.store(true);
    }
    g_probeBusy.store(false);
}

void V3GpuProbeQemu() {
    bool expected = false;
    if (g_probeBusy.compare_exchange_strong(expected, true)) {
        std::thread(ProbeQemuCaps).detach();
    }
}

// ---------------------------------------------------------------- mode resolution (TZ3 6.1, 6.4)
GpuPlan V3ResolveGpu(const InstanceCfg& c) {
    GpuInfo gi = V3DetectGpu();
    GpuPlan p;
    p.requested = LowerW(c.gpuMode);
    if (p.requested != L"host" && p.requested != L"swiftshader" && p.requested != L"software")
        p.requested = L"auto";

    std::wstring req = p.requested;
    auto hostPossible = [&]() {
        return gi.whpx && gi.virtFirmware && gi.virtioGl && (gi.gtkDisplay || gi.sdlDisplay) && gi.dxgiOk;
    };
    auto swPossible = [&]() { return gi.virtioGl || true; };  // virtio-gpu non-gl always available

    if (req == L"host") {
        if (hostPossible()) { p.resolved = L"host"; }
        else if (swPossible()) {
            p.resolved = L"swiftshader";
            p.fallbackNote = T(S3_GPU_FB_HOST);          // "Host недоступен, использован SwiftShader..."
        } else p.resolved = L"software";
    } else if (req == L"auto") {
        if (hostPossible() && gi.problem.empty()) p.resolved = L"host";
        else if (hostPossible() && !gi.problem.empty()) {
            p.resolved = L"swiftshader";
            p.fallbackNote = T(S3_GPU_FB_DRIVER);
        } else if (swPossible()) {
            p.resolved = L"swiftshader";
            p.fallbackNote = gi.whpx ? T(S3_GPU_FB_NOVIRGL) : T(S3_GPU_FB_NOWHPX);
        } else p.resolved = L"software";
    } else if (req == L"swiftshader") {
        p.resolved = L"swiftshader";
    } else {
        p.resolved = L"software";
    }

    if (p.resolved == L"host") {
        p.glDisplay = true;
        p.virtioGlDevice = true;
        p.displayKind = gi.gtkDisplay ? L"gtk" : L"sdl";
        // vulkan backend requested: QEMU virgl exposes Venus only on newer stacks;
        // keep GL unless user forced opengl/vulkan (vulkan -> try gl=on anyway, note)
    } else if (p.resolved == L"swiftshader") {
        p.glDisplay = false;
        p.virtioGlDevice = gi.virtioGl;      // virtio-gpu (non-gl) if device exists
        p.displayKind = gi.gtkDisplay ? L"gtk" : L"sdl";
    } else {
        p.glDisplay = false;
        p.virtioGlDevice = false;
        p.displayKind = gi.gtkDisplay ? L"gtk" : L"sdl";
    }
    return p;
}

// ---------------------------------------------------------------- diagnostics (TZ3 6.3)
std::vector<DiagItem> V3GpuDiag() {
    GpuInfo g = V3DetectGpu();
    std::vector<DiagItem> v;
    auto add = [&v](const wchar_t* n, const std::wstring& msg, int lvl) {
        DiagItem it; it.name = n; it.msg = msg; it.level = lvl; v.push_back(it);
    };
    if (g.dxgiOk) {
        add(L"GPU", g.name + Fmt(L" · %lld MB", g.vramMb), 0);
        add(L"GPU driver", g.driverVer, 0);
    } else {
        add(L"GPU", L"DXGI enumeration failed", 2);
    }
    add(L"WHPX", g.whpx ? L"Windows Hypervisor Platform available" : L"WHPX not available (TCG fallback)", g.whpx ? 0 : 1);
    add(L"VT-x / AMD-V", g.virtFirmware ? L"enabled" : L"disabled (check BIOS)", g.virtFirmware ? 0 : 2);
    add(L"Vulkan", g.vulkan ? L"vulkan-1.dll found" : L"vulkan-1.dll not found (host GL path used)", g.vulkan ? 0 : 1);
    add(L"OpenGL ICD", g.openglUser ? L"vendor OpenGL driver present" : L"no vendor OpenGL ICD", g.openglUser ? 0 : 1);
    if (g_qemuProbed.load()) {
        add(L"QEMU virgl", g.virtioGl ? L"virtio-gpu-gl-pci supported" : L"virtio-gpu-gl not supported by this QEMU build", g.virtioGl ? 0 : 2);
        add(L"QEMU display", (g.gtkDisplay ? std::wstring(L"gtk") : std::wstring(L"-")) +
            (g.sdlDisplay ? L" + sdl" : L""), g.gtkDisplay || g.sdlDisplay ? 0 : 2);
    } else {
        add(L"QEMU virgl", L"probing...", 3);
    }
    GpuPlan p = V3ResolveGpu(InstanceCfg{});
    add(L"GPU mode (Auto)", p.resolved + (p.fallbackNote.empty() ? L"" : L" · fallback"), p.resolved == L"host" ? 0 : 1);
    if (!g.problem.empty())
        add(L"Known issues", g.problem == L"intel-hd-2/3k" ?
            std::wstring(L"Intel HD 2000/3000 lacks GL 3.3 - SwiftShader recommended") : g.problem, 1);
    return v;
}

// ---------------------------------------------------------------- QEMU args v3 (TZ3 6.2)
std::wstring BuildQemuArgsV3(const InstanceCfg& c, const GpuPlan& gp, std::wstring& err) {
    std::wstring a;
    auto add = [&a](const std::wstring& s) { a += L" " + s; };

    bool whpx = WhpxAvailable();
    add(L"-machine q35");
    add(whpx ? L"-accel whpx" : L"-accel tcg,thread=multi");
    add(whpx ? L"-cpu host" : L"-cpu max");
    add(Fmt(L"-smp %d", c.cpuCores));
    add(Fmt(L"-m %d", c.ramMb));

    std::wstring diskFile = c.Dir() + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2");
    if (c.bootMode == L"iso") {
        if (c.imagePath.empty() || !FE(c.imagePath)) { err = L"iso missing"; return L""; }
        add(L"-drive file=" + Qn(c.imagePath) + L",media=cdrom,if=ide");
        add(L"-boot d");
        if (FE(diskFile))
            add(L"-drive file=" + Qn(diskFile) + L",format=" + c.diskFormat + L",if=ide,index=1");
    } else {
        if (!FE(diskFile)) { err = L"disk missing"; return L""; }
        add(L"-drive file=" + Qn(diskFile) + L",format=" + c.diskFormat + L",if=ide,index=0");
        add(L"-boot c");
    }

    // network: NAT + hostfwd on localhost only (TZ2 9 kept); user DNS (TZ4 2.2)
    if (!c.dnsServer.empty())
        add(Fmt(L"-netdev user,id=net0,dns=%s,hostfwd=tcp:127.0.0.1:%d-:5555",
                c.dnsServer.c_str(), c.adbPort));
    else
        add(Fmt(L"-netdev user,id=net0,hostfwd=tcp:127.0.0.1:%d-:5555", c.adbPort));
    add(L"-device virtio-net-pci,netdev=net0");

    // ---- GPU mode (TZ3 6.1/6.2)
    if (gp.resolved == L"host") {
        add(L"-device virtio-gpu-gl-pci");
        add(gp.displayKind == L"sdl" ? L"-display sdl,gl=on" : L"-display gtk,gl=on");
        add(L"-vga none");
    } else if (gp.resolved == L"swiftshader") {
        if (gp.virtioGlDevice) add(L"-device virtio-gpu-pci");
        else add(Fmt(L"-device VGA,edid=on,xres=%d,yres=%d", c.resW, c.resH));
        add(gp.displayKind == L"sdl" ? L"-display sdl,gl=off" : L"-display gtk,gl=off");
    } else { // software
        add(Fmt(L"-device VGA,edid=on,xres=%d,yres=%d", c.resW, c.resH));
        add(gp.displayKind == L"sdl" ? L"-display sdl,gl=off" : L"-display gtk,gl=off");
    }

    // ---- audio (TZ3 10.2)
    if (c.soundEnabled) {
        add(L"-audiodev dsound,id=snd0");
        add(L"-device intel-hda");
        add(L"-device hda-duplex,audiodev=snd0");
    }

    // input
    add(L"-device qemu-xhci");
    add(L"-device usb-tablet");
    add(L"-rtc base=localtime");
    add(L"-serial file:" + Qn(InstanceLogPath(c.id, L"serial")));

    if (c.fullscreen && gp.displayKind == L"sdl") add(L"-full-screen");
    add(L"-name \"NovaDroid - " + c.name + L"\"");
    return a;
}

// ---------------------------------------------------------------- fps limiter via ADB (TZ3 7.2)
void V3ApplyFpsLimitAsync(const std::wstring& id) {
    std::thread([id]() {
        for (int i = 0; i < 90; ++i) {
            SleepMs(2000);
            std::lock_guard<std::mutex> lk(g_mx);
            Runtime* r = Rt(id);
            InstanceCfg* c = Inst(id);
            if (!r || !r->hProc || !r->adbOk || !c) return;
            int fps = c->fpsLimit;
            if (fps != 30 && fps != 45 && fps != 60 && fps != 90 && fps != 120) return; // off
            std::string so, se; DWORD ec = 0;
            std::wstring adb = AdbExe();
            if (adb.empty()) return;
            std::wstring ser = AdbSerial(*c);
            std::wstring out;
            RunCapture(adb, L"-s " + ser + L" shell settings put system peak_refresh_rate " + std::to_wstring(fps), L"", 5000, &ec, &so, &se);
            RunCapture(adb, L"-s " + ser + L" shell settings put system min_refresh_rate " + std::to_wstring(fps), L"", 5000, &ec, &so, &se);
            RunCapture(adb, L"-s " + ser + L" shell settings put system mode_refresh_rate " + std::to_wstring(fps), L"", 5000, &ec, &so, &se);
            ILog(id, L"launcher", L"FPS limiter: host target " + std::to_wstring(fps) +
                 L" fps (vsync " + std::wstring(c->vsync ? L"on" : L"off") + L") applied via refresh-rate settings");
            return;
        }
    }).detach();
}
