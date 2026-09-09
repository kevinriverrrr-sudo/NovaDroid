// ============================================================================
//  NovaDroid - v3.h  Stage 3 "Gaming Core" (TZ 3.0):
//  GPU acceleration (Auto/Host/SwiftShader/Software), FPS counter & limiter,
//  fullscreen/borderless, mouse capture, audio, perf profiles, fast start,
//  GPU diagnostics, crash recovery, full-package bootstrap (direct links).
// ============================================================================
#pragma once
#include "app.h"
#include "v2.h"

// ---------------------------------------------------------------- GPU detection (TZ3 6.3)
struct GpuInfo {
    bool dxgiOk = false;
    std::wstring name = L"?";            // adapter description
    std::wstring driverVer = L"?";       // registry DriverVersion
    long long vramMb = 0;
    bool whql = false;
    bool whpx = false;                   // Windows Hypervisor Platform
    bool virtFirmware = false;           // VT-x/AMD-V enabled
    bool vulkan = false;                 // vulkan-1.dll present
    bool openglUser = false;             // vendor OpenGL ICD present
    bool gtkDisplay = false;             // qemu has gtk display
    bool sdlDisplay = false;             // qemu has sdl display
    bool virtioGl = false;               // qemu has virtio-gpu-gl-pci (virgl)
    std::wstring problem;                // known problematic driver note
};

// resolved GPU plan (TZ3 6.2/6.4)
struct GpuPlan {
    std::wstring requested;              // auto | host | swiftshader | software
    std::wstring resolved;               // host | swiftshader | software
    bool glDisplay = false;              // -display ...,gl=on
    std::wstring displayKind = L"gtk";   // gtk | sdl
    bool virtioGlDevice = false;         // -device virtio-gpu-gl-pci
    std::wstring fallbackNote;           // user-facing note (RU/EN) or ""
};

GpuInfo        V3DetectGpu();                    // cached after first call
void           V3GpuProbeQemu();                 // async qemu -device/-display help scan
GpuPlan        V3ResolveGpu(const InstanceCfg& c);
std::vector<DiagItem> V3GpuDiag();               // GPU section for diagnostics center

// ---------------------------------------------------------------- audio (TZ3 10)
bool V3AudioApply(const InstanceCfg& c);         // volume+mute for running qemu pid
bool V3AudioSetVolume(DWORD qemuPid, int percent, bool mute);
void V3AudioMinimizeTick();                      // mute-on-minimize watchdog

// shared window cache refresh (v3input.cpp)
void RefreshQemuWindows();

// ---------------------------------------------------------------- window/input (TZ3 8, 9)
HWND V3FindQemuWindow(DWORD pid);                // top-level qemu window
void V3FullscreenToggle(const std::wstring& id); // F11 / button
void V3BorderlessApply(const std::wstring& id);  // borderless / topmost apply
bool V3CaptureToggle(const std::wstring& id);    // mouse grab on/off, returns new state
bool V3CaptureActive();
void V3InputHookStart();                         // LL keyboard hook (F11/Alt+Enter/Esc/hotkey)
void V3InputHookStop();
void V3RestoreAllWindows();                      // exit fullscreen on quit

// ---------------------------------------------------------------- FPS overlay (TZ3 7.1)
void V3OverlayShow(const std::wstring& id, bool show);
void V3OverlayTick();                            // 500 ms: measure + repaint
void V3OverlayHideAll();
float V3LastFps();                               // smoothed fps of the measured window (stage 7 anti-lag)

// ---------------------------------------------------------------- performance profiles (TZ3 11)
struct PerfProfileDef {
    const wchar_t* id; const wchar_t* nameRu; const wchar_t* nameEn;
    int cpu, ramMb, resW, resH, dpi, fps;
    const wchar_t* gpu;                          // auto|host|swiftshader|software
    bool sound, shared, fullscreen, borderless;
};
const std::vector<PerfProfileDef>& V3Profiles();
bool V3ApplyProfile(const std::wstring& instanceId, const std::wstring& profileId,
                    std::wstring& err);          // apply + persist + stop-if-running check

// ---------------------------------------------------------------- fast start (TZ3 12)
void V3ApplyFpsLimitAsync(const std::wstring& id);   // after ADB up: settings put / setprop

// ---------------------------------------------------------------- crash recovery (TZ3 13)
void V3OnQemuExit(const std::wstring& id, HANDLE hp, DWORD pid);  // from WM_APP_QEXIT
void V3WatchdogTick();                           // 2 s: hung-window detection
bool V3RecoveryDialog(HWND parent, const std::wstring& id, const std::wstring& reason);
std::wstring V3SaveCrashReport(const std::wstring& id);   // zip path or ""
void V3PendingCrash(const std::wstring& id);     // queue for UI-thread dialog
void V3RecoveryPump(HWND parent);                // show queued dialogs

// ---------------------------------------------------------------- full-package bootstrap (user req.)
struct PkgComponent { std::wstring relPath; long long sizeBytes; std::wstring sha256; };
struct PkgStateInfo {
    int  state = 0;                              // 0 idle 1 download 2 extract 3 verify 4 done 5 error
    float progress = 0;                          // 0..1 (current url)
    std::wstring stageText;                      // human-readable
    std::wstring error;
    std::wstring activeUrl;
    long long gotBytes = 0, totalBytes = 0;
};
struct BakedPkgUrl { const wchar_t* url; long long sizeBytes; const wchar_t* sha256; };
extern const BakedPkgUrl g_pkgUrls[];            // baked direct links (vikingfile)
extern const int      g_pkgUrlCount;
extern const wchar_t* g_pkgVersion;

bool        PkgInstalled();                      // marker + critical files present
std::wstring PkgRoot();                          // extraction root (exeDir, fallback dataRoot)
bool        PkgMissingCritical(std::vector<std::wstring>& missing);
bool        PkgVerifyAll(std::wstring& firstBad, int* okCount, int* totalCount);
bool        PkgStartDownloadAsync();             // baked urls, sequential mirrors
bool        PkgUseOfflineZip(const std::wstring& zipPath, std::wstring& err);
PkgStateInfo PkgInfo();
void        PkgLoadMarker();                     // read .novadroid-pkg.json at startup
std::wstring PkgInstalledVersion();
void        V3DownloadWizard(HWND parent);       // modal download/verify wizard

// ---------------------------------------------------------------- qemu args v3 (TZ3 6.2)
std::wstring BuildQemuArgsV3(const InstanceCfg& c, const GpuPlan& gp, std::wstring& err);

// ---------------------------------------------------------------- UI glue
void V3Init();                                   // load pkg marker, start hooks, preflight
void V3Shutdown();
void V3TimerTick();                              // from WM_TIMER (1 s base tick)
bool V3Click(int id);                            // consume stage-3 widget ids
void V3Notify(const std::wstring& text, int type);

// gaming settings dialog ("Graphics and performance" tab of instance settings, TZ3 14.2)
bool DialogGamingSettings(HWND parent, InstanceCfg& c);
// settings-page package block + click routing
void PageSettingsPkgSection(HDC dc, RECT rc, int* yOut);
// perf-page gaming profiles row
void PagePerfProfilesRow(HDC dc, RECT rc, int* yOut);
// diag-page gpu block
void PageDiagGpuBlock(HDC dc, RECT rc, int* yOut);

// stage-3 widget id ranges (ui.h owns base ranges)
enum {
    ID3_PKG0 = 5200,                 // 0=download 1=offline 2=verify 3=folder
    ID3_PROF0 = 5300,                // 0..2 eco/bal/gaming
    ID3_GPU_TEST = 5400,             // gpu self-test button on diag page
};
