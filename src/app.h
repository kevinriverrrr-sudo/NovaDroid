// ============================================================================
//  NovaDroid Emulator Launcher - app.h
//  Central header: types, globals, cross-module API.
//  Target: Windows 10/11 x64, MinGW-w64 (C++17), Win32 + GDI (no external deps)
// ============================================================================
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <commdlg.h>
#include <psapi.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <chrono>
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstdint>

namespace fs = std::filesystem;   // (after <filesystem> in json.h)

// ---------------------------------------------------------------- DWM fallbacks
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef PF_VIRT_FIRMWARE_ENABLED
#define PF_VIRT_FIRMWARE_ENABLED 21
#endif

// ---------------------------------------------------------------- JSON (json.h/cpp)
#include "json.h"

// ---------------------------------------------------------------- Palette (TZ 2.0 §16.4; stage 5: dark/light themes)
constexpr inline COLORREF CREF(unsigned long rgb) {
    return COLORREF(((rgb & 0xFFUL) << 16) | (rgb & 0xFF00UL) | ((rgb >> 16) & 0xFFUL));
}
// base surfaces are theme-dependent (v4prod.cpp NovaPal*, TZ5 3.5)
COLORREF NovaPalBg();
COLORREF NovaPalSurf();
COLORREF NovaPalSurf2();
COLORREF NovaPalBorder();
COLORREF NovaPalText();
COLORREF NovaPalSub();
#define CL_BG      NovaPalBg()      // main background
#define CL_SURF    NovaPalSurf()    // surface
#define CL_SURF2   NovaPalSurf2()   // cards
#define CL_BORDER  NovaPalBorder()
#define CL_TEXT    NovaPalText()
#define CL_SUB     NovaPalSub()
#define CL_ACCENT  CREF(0x6C63FF)
#define CL_ACCENT2 CREF(0x23C4F8)   // secondary accent
#define CL_OK      CREF(0x38C976)
#define CL_WARN    CREF(0xF3A93C)
#define CL_ERR     CREF(0xED5D64)

// ---------------------------------------------------------------- Instance status (TZ 6.2)
enum class St { Stopped, Starting, Running, Paused, Stopping, StartError, Recovery, Updating };
// stage 2 statuses (TZ 5.4), appended after the base enum values
#define ST2_PREPARING   ((St)8)   // Подготовка
#define ST2_WAITBOOT    ((St)9)   // Ожидание загрузки Android
#define ST2_CLONING     ((St)10)  // Создаётся клон
#define ST2_BACKUP      ((St)11)  // Создаётся резервная копия
#define ST2_RESTORE     ((St)12)  // Восстанавливается
#define ST2_CORRUPT     ((St)13)  // Повреждён

// ---------------------------------------------------------------- Instance config (TZ 8.2)
struct InstanceCfg {
    std::wstring id;                 // instance-001
    std::wstring name;               // Игры
    std::wstring androidVersion;     // detected via ADB, e.g. "9 (x86_64)"
    std::wstring imagePath;          // ISO or base disk image (full path)
    std::wstring bootMode = L"iso";  // "iso" | "disk"
    std::wstring diskFormat = L"qcow2";
    int cpuCores = 4;
    int ramMb     = 4096;
    int diskGb    = 32;
    int resW      = 1280;
    int resH      = 720;
    int dpi       = 240;
    int fpsLimit  = 60;
    std::wstring gpuMode = L"auto";      // auto | std | virtio
    std::wstring priority = L"normal";   // normal | high
    bool soundEnabled = true;
    bool fullscreen   = false;
    std::wstring networkMode = L"nat";
    bool adbEnabled = true;
    int  adbPort    = 0;             // 0 = auto-assign
    bool sharedFolderEnabled = true;
    std::wstring sharedFolderPath;
    std::wstring createdAt;
    std::wstring lastLaunchAt;       // L"" = null
    bool wantShortcut = false;       // transient (dialog only)
    // ---- stage 2 (TZ 17.1)
    std::wstring icon = L"android";
    std::wstring templateName = L"balanced";
    std::wstring adbHost = L"127.0.0.1";
    std::wstring lastShutdownAt;
    std::wstring parentInstanceId;   // for linked clones
    std::wstring cloneType = L"full";
    std::wstring keymapProfileId;
    // ---- stage 3 (TZ 3.0 §6.1, §8, §9, §10)
    std::wstring gpuBackend = L"auto";       // auto | opengl | vulkan
    bool borderless    = false;              // borderless window mode
    bool topMost       = false;              // always on top
    std::wstring scaleMode = L"fit";         // stretch | fit | fill | native
    bool vsync         = true;
    int  soundVolume   = 80;                 // 0..100
    bool muteOnMinimize = true;
    bool fpsOverlay    = true;               // show FPS counter
    bool quickStart    = true;               // fast start (TZ3 §12)
    std::wstring perfProfile = L"balanced";  // eco | balanced | gaming | custom
    int  captureHotkey = VK_RCONTROL;        // mouse capture toggle
    int  sensX = 100;                        // mouse sens, %
    int  sensY = 100;
    bool invertY = false;
    int  mouseAccel = 0;                     // 0 off, 1 low, 2 med, 3 high
    // ---- stage 4-7 (TZ 4-7)
    std::wstring dnsServer = L"";            // user net DNS override (qemu dns=, TZ4 2.2)
    bool baseAppsInstalled = false;          // F-Droid/APKPure auto-install done (user req.)
    std::wstring antiLag = L"on";            // anti-lag: on | off (TZ7 5.2)
    std::wstring syncRole = L"";             // "" | master:<id> | slave:<id> (TZ7 5.3)

    JValue ToJ() const;
    static InstanceCfg FromJ(const JValue& v);
    std::wstring Dir() const;        // dataRoot\instances\<id>
};

// ---------------------------------------------------------------- Runtime state
struct Runtime {
    St st = St::Stopped;
    HANDLE hProc = nullptr;
    DWORD pid = 0;
    bool adbOk = false;
    bool busy = false;               // async op in progress (install/clone/...)
    long long startedAtMs = 0;       // epoch ms
    std::wstring verDetected;        // android version via getprop
    // ---- stage 3 (TZ3 §13 crash recovery)
    bool hung = false;               // VM window not responding
    int  hangTicks = 0;              // consecutive unresponsive watchdog ticks
    DWORD lastExitCode = 0;
    bool crash = false;              // unexpected exit
    bool audioMuted = false;         // applied mute state
    HWND qhWnd = nullptr;            // cached qemu window handle
};

// ---------------------------------------------------------------- Settings
struct Settings {
    std::wstring language = L"ru";       // ru | en
    std::wstring accentHex = L"#6C63FF"; // TZ accent
    std::wstring qemuPath;               // explicit path or empty -> auto
    std::wstring adbPath;
    std::wstring defaultImagePath;
    std::wstring dataRoot;               // default %LOCALAPPDATA%\NovaDroid
    int  baseAdbPort = 5555;
    bool checkOnStart = true;
    bool autostartLast = false;
    // ---- stage 2 (TZ 5.3, 14.3)
    int  maxRunning = 2;                 // concurrent instances limit
    std::wstring updateChannel = L"stable"; // stable | beta | development
    bool ecoPerf = false;                // disable graphs in eco mode
    // ---- stage 4-7 (TZ 5, 6, 7)
    std::wstring theme = L"dark";            // dark | light | system (TZ5 3.5)
    bool firstRunDone = false;           // TZ5 3.2 wizard
    bool analyticsConsent = false;       // local-only usage stats, opt-in (TZ7 5.7)
    std::wstring cloudProvider, cloudEndpoint, cloudToken;  // TZ6 4.3
    bool cloudEncrypt = true;
    bool discordEnabled = false;         // TZ7 5.5
    std::wstring discordClientId;        // user-registered Discord app id
    std::wstring proKey;                 // NovaDroid Pro license key (TZ7 5.8)
    int streamPort = 8080;               // browser stream port (TZ6 4.1)
};

struct Paths {
    std::wstring exeDir, dataRoot;
    std::wstring instances, images, backups, logs, cache, db, apks, shots, configs;
};

// ---------------------------------------------------------------- Diagnostics item
struct DiagItem {
    std::wstring name, msg;
    int level;                       // 0 ok, 1 warn, 2 fail, 3 info
};

// ---------------------------------------------------------------- Globals (core.cpp)
extern HINSTANCE g_hi;
extern HWND      g_wnd;
extern Settings  g_set;
extern Paths     g_p;
extern std::vector<InstanceCfg> g_insts;
extern std::map<std::wstring, Runtime> g_rt;
extern std::mutex g_mx;
extern int  g_lang;                  // 0 ru, 1 en
extern std::wstring g_selId;
extern std::vector<DiagItem> g_diag;
extern std::atomic<bool> g_diagBusy;
extern DWORD g_startTick;

// ---------------------------------------------------------------- i18n (strings.cpp)
enum SI {
    S_APP_NAME = 0, S_APP_SUB,
    S_NAV_HOME, S_NAV_INST, S_NAV_APK, S_NAV_FILES, S_NAV_KEYS, S_NAV_PERF,
    S_NAV_DIAG, S_NAV_LOGS, S_NAV_SET, S_NAV_ABOUT,
    S_WELCOME, S_WELCOME_SUB,
    S_BTN_LAUNCH, S_BTN_STOP, S_BTN_CREATE, S_BTN_QUICK, S_BTN_SAVE, S_BTN_CANCEL,
    S_BTN_ADD, S_BTN_IMPORT, S_BTN_EXPORT, S_BTN_BACKUP, S_BTN_RESTORE, S_BTN_DELETE,
    S_BTN_RENAME, S_BTN_CLONE, S_BTN_CONFIG, S_BTN_OPENFOLDER, S_BTN_RUN, S_BTN_APPLY,
    S_ST_STOPPED, S_ST_STARTING, S_ST_RUNNING, S_ST_PAUSED, S_ST_STOPPING,
    S_ST_ERROR, S_ST_RECOVERY, S_ST_UPDATING,
    S_SYS_STATE, S_D_VIRT, S_D_WHPX, S_D_QEMU, S_D_ADB, S_D_IMAGE, S_D_RAM, S_D_DISK,
    S_TB_TITLE, S_TB_BACK, S_TB_HOME, S_TB_REC, S_TB_VOLUP, S_TB_VOLDN, S_TB_SHOT,
    S_TB_ROT, S_TB_APK, S_TB_STOP, S_TB_UPTIME, S_TB_RAMDROP,
    S_LAST_APK, S_OPEN_APKLIB,
    S_INST_TITLE, S_INST_SUB, S_BTN_FORCESTOP, S_BTN_RESTART,
    S_CTX_START, S_CTX_STOP, S_CTX_RESTART, S_CTX_CONFIG, S_CTX_RENAME, S_CTX_CLONE,
    S_CTX_BACKUP, S_CTX_RESTORE, S_CTX_EXPORT, S_CTX_OPENF, S_CTX_DELETE,
    S_APK_TITLE, S_APK_SUB, S_APK_INSTALL, S_APK_OPEN, S_APK_EMPTY, S_APK_DND,
    S_APK_NORUN,
    S_FILE_TITLE, S_FILE_SUB, S_FILE_PICK, S_FILE_PUSH, S_FILE_PULL, S_FILE_OPEN,
    S_FILE_PATH, S_FILE_EMPTY,
    S_KEY_TITLE, S_KEY_SUB, S_KEY_NOTE,
    S_PERF_TITLE, S_PERF_SUB, S_PERF_PROFILE, S_PERF_ECO, S_PERF_BAL, S_PERF_HIGH,
    S_PERF_CUSTOM, S_PERF_CPU, S_PERF_RAM, S_PERF_DISKGB, S_PERF_RES, S_PERF_DPI,
    S_PERF_FPS, S_PERF_GPU, S_PERF_PRIO, S_PERF_APPLY, S_PERF_NOTE,
    S_PRIO_N, S_PRIO_H,
    S_DIAG_TITLE, S_DIAG_SUB, S_DIAG_RUN, S_DIAG_COPY, S_DIAG_OPENLOGS,
    S_LOGS_TITLE, S_LOGS_SUB, S_LOG_REFRESH, S_LOG_FOLDER, S_LOG_CLEAN, S_LOG_COPY,
    S_LOG_Launcher, S_LOG_QEMU, S_LOG_ADB, S_LOG_SERIAL, S_LOG_EMPTY,
    S_SET_TITLE, S_SET_SUB, S_SET_LANG, S_SET_ACCENT, S_SET_QEMU, S_SET_ADB,
    S_SET_IMAGE, S_SET_DATA, S_SET_CHECK, S_SET_PORT, S_SET_RESET, S_SET_PICK,
    S_SET_RESTART, S_SET_AUTORUN,
    S_AB_TITLE, S_AB_VER, S_AB_ABOUT, S_AB_COMP, S_AB_SAFE, S_AB_LINKS,
    S_DLG_CREATE, S_DLG_EDIT, S_DLG_NAME, S_DLG_IMAGE, S_DLG_BROWSE, S_DLG_BOOT,
    S_DLG_BOOT_ISO, S_DLG_BOOT_DISK, S_DLG_CPU, S_DLG_RAM, S_DLG_DISK, S_DLG_RES,
    S_DLG_DPI, S_DLG_FPS, S_DLG_GPU, S_DLG_GPU_AUTO, S_DLG_GPU_STD, S_DLG_GPU_VIRT,
    S_DLG_NET, S_DLG_ADB, S_DLG_SHARED, S_DLG_SHORTCUT, S_DLG_SOUND, S_DLG_FULL,
    S_MB_DEL, S_MB_DELT, S_MB_BAK, S_MB_RUNERR, S_MB_STOPQ, S_MB_STOPT,
    S_MB_IMGMISS, S_MB_QEMUMISS, S_MB_ADBMISS, S_MB_DISKBUSY, S_MB_INSTALLED,
    S_MB_INSTFAIL, S_MB_COPIED, S_MB_RESTORED, S_MB_BADAPK,
    S_YES, S_NO, S_OK2, S_ERR2,
    // ---- stage 2 strings (TZ 2.0)
    S_NAV_IMG, S_NAV_BACKUPS, S_NAV_SNAPS,
    S_ST2_PREP, S_ST2_WAIT, S_ST2_CLONING, S_ST2_BAK, S_ST2_REST, S_ST2_CORRUPT,
    S_IMG_TITLE, S_IMG_SUB, S_IMG_DOWNLOAD, S_IMG_SHA, S_IMG_USE, S_IMG_DELETE,
    S_IMG_LOCAL, S_IMG_CATALOG, S_IMG_DOWNLOADED, S_IMG_NOTDL, S_IMG_OK, S_IMG_FAIL,
    S_IMG_CANCEL, S_IMG_USEDDEF, S_IMG_HINT, S_IMG_SHACHECK, S_IMG_OPENMIRRORS,
    S_BK_TITLE, S_BK_SUB, S_BK_CREATE, S_BK_IMPORT, S_BK_RESTORE2, S_BK_EXPORT,
    S_BK_OPEN, S_BK_EMPTY, S_BK_MANIFEST, S_BK_NOMAN, S_BK_WITHSNAP, S_BK_ASK,
    S_SNAP_TITLE, S_SNAP_SUB, S_SNAP_CREATE, S_SNAP_RESTORE, S_SNAP_DELETE,
    S_SNAP_PROT, S_SNAP_SYSTEM, S_SNAP_USER, S_SNAP_AUTO, S_SNAP_EMPTY,
    S_SNAP_WARNREST, S_SNAP_NAME, S_SNAP_DESC, S_SNAP_PROT2,
    S_KM_TITLE, S_KM_SUB, S_KM_ADDPROFILE, S_KM_ADDBIND, S_KM_DELETE, S_KM_EXPORT,
    S_KM_IMPORT, S_KM_TOGGLE, S_KM_ON, S_KM_OFF, S_KM_ENGINE, S_KM_HOTKEY,
    S_KM_BINDS, S_KM_NOBINDS, S_KM_PRESSKEY, S_KM_COORD, S_KM_ACTION, S_KM_TAP,
    S_KM_LONGTAP, S_KM_SWIPE, S_KM_BACKB, S_KM_HOMEB, S_KM_RECB, S_KM_VOLUP,
    S_KM_VOLDN, S_KM_ROT, S_KM_NOTICE, S_KM_NAME, S_KM_PICK, S_KM_HINTCLICK,
    S_KM_X, S_KM_Y, S_KM_X2, S_KM_Y2, S_KM_SHOT,
    S_PERF2_TITLE, S_PERF2_SUB, S_PERF2_CPU, S_PERF2_RAM, S_PERF2_DISK,
    S_PERF2_NOINST, S_PERF2_REC, S_PERF2_ECO, S_PERF2_UPTIME, S_PERF2_GPU,
    S_WIZ_TITLE, S_WIZ_STEP, S_WIZ_NEXT, S_WIZ_PREV, S_WIZ_FINISH, S_WIZ_NAME,
    S_WIZ_TEMPLATE, S_WIZ_T_CLEAN, S_WIZ_T_BAL, S_WIZ_T_PERF, S_WIZ_T_ECO,
    S_WIZ_T_TEST, S_WIZ_T_PORT, S_WIZ_T_TAB, S_WIZ_T_CUSTOM, S_WIZ_PERF,
    S_WIZ_SCREEN, S_WIZ_INTEG, S_WIZ_REVIEW, S_WIZ_ADBLOCK, S_WIZ_SHARED,
    S_WIZ_SHORTCUT, S_WIZ_OPEN, S_WIZ_SNAP, S_WIZ_PATH, S_WIZ_SPACE, S_WIZ_CREATE,
    S_WIZ_ORIENT, S_WIZ_LAND, S_WIZ_PORT, S_CLONE_TITLE, S_CLONE_FULL,
    S_CLONE_LINKED, S_CLONE_FULLD, S_CLONE_LINKEDD, S_CLONE_KEYMAPS, S_CLONE_SHAREDC,
    S_APKMI_TITLE, S_APKMI_SUB, S_APKMI_INSTALL, S_APKMI_SNAPFIRST, S_APKMI_RESULT,
    S_APKMI_TIME, S_APKMI_ERR, S_APKMI_DONE, S_APK_ADDLIB, S_APK_WARN, S_APK_DELETE2,
    S_UPD_CHECK, S_UPD_CHANNEL, S_UPD_STABLE, S_UPD_BETA, S_UPD_DEV, S_SET_MAXRUN,
    S_MB_PARENTLINK, S_MB_IMPORTOK, S_DIAG_EXPORT, S_AB_VER2,
    // ---- stage 3 strings (TZ 3.0)
    S3_GPU_FB_HOST, S3_GPU_FB_DRIVER, S3_GPU_FB_NOVIRGL, S3_GPU_FB_NOWHPX,
    S3_PROFILE_RUNNING,
    S3_PKG_ERROR, S3_PKG_DOWNLOAD, S3_PKG_EXTRACT, S3_PKG_VERIFY, S3_PKG_DONE,
    S3_PKG_BADSHA, S3_PKG_BADSIZE, S3_PKG_NET, S3_PKG_SMALL,
    S3_REC_TITLE, S3_REC_QUESTION, S3_REC_SNAP, S3_REC_LOGS, S3_REC_REPORT, S3_REC_SAVED,
    S3_WIZ_TITLE, S3_WIZ_SUB, S3_WIZ_TOTAL, S3_WIZ_READY, S3_WIZ_DOWNLOAD,
    S3_WIZ_OFFZIP, S3_WIZ_VERIFY, S3_WIZ_PICKZIP, S3_WIZ_HIDE,
    S3_PKG_TITLE, S3_PKG_OK, S3_PKG_PARTIAL, S3_PKG_NO, S3_PKG_REDOWNLOAD,
    S3_PKG_STARTED, S3_PKG_BUSY, S3_PKG_VOK, S3_PKG_VFAIL,
    S3_PROF_TITLE, S3_PROF_HINT, S3_PROF_APPLIED, S3_GPU_TITLE,
    S3_G_TITLE, S3_G_GPUMODE, S3_G_BACKEND, S3_G_SCALE, S3_G_PROFILE,
    S3_G_BORDERLESS, S3_G_TOPMOST, S3_G_VSYNC, S3_G_OVERLAY, S3_G_QUICKSTART,
    S3_G_VOLUME, S3_G_MUTEMIN, S3_G_INVY, S3_G_HOTKEY, S3_G_PROBE, S3_G_RESOLVED,
    S3_GPU_AUTO, S3_GPU_HOST, S3_GPU_SWIFT, S3_GPU_SOFT, S3_BK_AUTO, S3_FPS_OFF,
    S3_SC_FIT, S3_SC_STRETCH, S3_SC_FILL, S3_SC_NATIVE,
    S3_TB_FULL, S3_TB_CAP, S3_TB_MUTE,
    // ---- stage 4-7 strings (TZ 4-7)
    S4_ABI_OK, S4_ABI_X86, S4_ABI_ARM, S4_ABI_UNIVERSAL, S4_ABI_UNKNOWN, S4_ABI_NOLIBS,
    S4_NET_RESULT, S4_NET_FIXFAIL, S4_NET_INETOK, S4_NET_INETFAIL, S4_NET_DIAG,
    S4_NET_FIXDNS, S4_NET_RESETDNS, S4_NET_RUNNING, S4_NET_FIXED, S4_NET_RESETOK,
    S4_SENS_FAIL, S4_SENS_OK, S4_GPS_BADFMT, S4_PROF_OK,
    S4_APP_PUBG, S4_APP_APPLIED,
    S4_COM_TITLE, S4_COM_SUB, S4_COM_APK, S4_COM_APKHINT, S4_COM_ANALYZE, S4_COM_RESCAN,
    S4_COM_RESCANNED, S4_COM_PKG, S4_NOSEL, S4_COM_DEV, S4_COM_DEVSUB, S4_COM_APP,
    S4_COM_APPSUB, S4_COM_APPLY, S4_COM_WARN,
    S4_SENS_TITLE, S4_SENS_SUB, S4_NET_TITLE, S4_NET_HINT,
    S4_GPS_TITLE, S4_GPS_HINT, S4_GPS_SET, S4_GPS_CUSTOM,
    S4_ORI_TITLE, S4_ORI_HINT,
    S4_BAT_TITLE, S4_BAT_HINT, S4_BAT_SET, S4_BAT_RESET, S4_BAT_CUSTOM,
    S4_ADV_TITLE, S4_ADV_SUB, S4_UPD_TITLE, S4_UPD_CUR, S4_UPD_CHANNEL,
    S4_UPD_AVAIL, S4_UPD_LATEST, S4_UPD_INSTALL, S4_UPD_CHANGETO,
    S4_UPD_NOFEED, S4_UPD_DLFAIL, S4_UPD_BADSHA, S4_UPD_SWAPFAIL, S4_UPD_ROLLBACK,
    S4_LOG_TITLE, S4_LOG_HINT, S4_LOG_EXPORT, S4_LOG_ZIPPING, S4_LOG_DONE, S4_LOG_FAIL,
    S4_BASEPART, S4_BASEDONE,
    S4_STR_TITLE, S4_STR_HINT, S4_STR_START, S4_STR_STOP, S4_STR_OPEN, S4_STR_LOCAL,
    S4_STR_BUSY, S4_STR_PORTBUSY,
    S4_PLG_TITLE, S4_PLG_HINT, S4_PLG_EMPTY, S4_PLG_INVALID, S4_PLG_ON, S4_PLG_OFF,
    S4_PLG_RESCAN, S4_PLG_RESCANNED,
    S4_CLD_TITLE, S4_CLD_PROV, S4_CLD_NOTSET, S4_CLD_ENC, S4_CLD_NOENC, S4_CLD_SETUP,
    S4_CLD_NOPROV, S4_CLD_NOTSET2, S4_CLD_NOFILES, S4_CLD_PICKFILE, S4_CLD_NOTHING,
    S4_CLD_UPOK, S4_CLD_UPFAIL, S4_CLD_RESOK, S4_CLD_RESFAIL,
    S4_CLD_SETUPT, S4_CLD_SERVER, S4_CLD_TOKEN, S4_CLD_ENC_ON, S4_CLD_ENC_OFF, S4_CLD_NOTE,
    S4_MAC_TITLE, S4_MAC_WARN, S4_MAC_EMPTY, S4_MAC_STEPS, S4_MAC_EDIT, S4_MAC_PLAY,
    S4_MAC_STOP, S4_MAC_DEL, S4_MAC_REC, S4_MAC_STOPREC, S4_MAC_RECON, S4_MAC_SAVED,
    S4_MAC_DEFNAME, S4_MAC_NORUN, S4_MAC_PLAYING, S4_MAC_PLAYDONE, S4_MAC_DELETED,
    S4_MAC_EDITORT, S4_MAC_NOHOT, S4_MAC_REPEAT, S4_MAC_ADDTAP, S4_MAC_ADDKEY,
    S4_MAC_ADDWAIT, S4_MAC_ADDTEXT, S4_MAC_DELSTEP,
    S4_SYN_TITLE, S4_SYN_HINT, S4_SYN_NONE, S4_SYN_ADD, S4_SYN_REMOVE, S4_SYN_ISMASTER,
    S4_SYN_NOSLAVES, S4_SYN_ADDED, S4_SYN_REMOVED,
    S4_ALG_TITLE, S4_ALG_HINT, S4_ALG_ON, S4_ALG_OFF,
    S4_DSC_TITLE, S4_DSC_HINT, S4_DSC_ON, S4_DSC_OFF,
    S4_PRO_HINT, S4_PRO_ACT, S4_PRO_ACTIVE, S4_PRO_ISACTIVE, S4_PRO_ENTER, S4_PRO_OK, S4_PRO_BAD,
    S4_NAV_COMPAT, S4_NAV_SENSORS, S4_NAV_ADVANCED,
    S4_WIZ_WELCOME, S4_WIZ_CHECK, S4_WIZ_LANG, S4_WIZ_THEME, S4_WIZ_INST, S4_WIZ_DONE,
    S4_WIZ_STEP, S4_WIZ_WELCOMETXT, S4_WIZ_NOVIRT, S4_WIZ_NOWHPX, S4_WIZ_DISK,
    S4_WIZ_SYSWARN, S4_WIZ_DARK, S4_WIZ_LIGHT, S4_WIZ_SYS, S4_WIZ_LAUNCH, S4_WIZ_DOCS,
    S4_WIZ_DONETXT,
    S_COUNT_
};
const wchar_t* T(int id);
inline const wchar_t* T(SI id) { return T((int)id); }

// ---------------------------------------------------------------- util.cpp
std::wstring U2W(const std::string& s);
std::string  W2U(const std::wstring& s);
std::wstring Fmt(const wchar_t* fmt, ...);
bool  FE(const std::wstring& path);                 // file exists
bool  DE(const std::wstring& path);                 // dir exists
bool  MK(const std::wstring& path);                 // mkdir -p
std::wstring TrimW(std::wstring s);
std::wstring LowerW(std::wstring s);
std::wstring RepAll(std::wstring s, const std::wstring& a, const std::wstring& b);
std::vector<std::wstring> SplitW(const std::wstring& s, wchar_t sep);
std::wstring BaseName(const std::wstring& path);
std::wstring FileExt(const std::wstring& path);     // lower ".apk"
std::wstring NowIso();
std::wstring NowFileStamp();
std::wstring HumanSize(uint64_t b);
bool  ReadText(const std::wstring& path, std::wstring& out);
bool  WriteText(const std::wstring& path, const std::wstring& data);
bool  CopyTree(const std::wstring& src, const std::wstring& dst);
bool  DeleteTree(const std::wstring& path);
uint64_t FileSizeOf(const std::wstring& path);
std::wstring Qn(const std::wstring& s);             // quote for cmdline
bool  RunCapture(const std::wstring& exe, const std::wstring& args,
                 const std::wstring& cwd, DWORD timeoutMs,
                 DWORD* exitCode, std::string* so, std::string* se);
bool  LaunchWithLogs(const std::wstring& exe, const std::wstring& args,
                     const std::wstring& cwd, const std::wstring& outFile,
                     const std::wstring& errFile, HANDLE* hProc, DWORD* pid);
bool  RunToFile(const std::wstring& exe, const std::wstring& args,
                const std::wstring& outFile, const std::wstring& errFile,
                const std::wstring& cwd, DWORD timeoutMs, int* exitCode);
bool  ShellOpen(const std::wstring& pathOrUrl);
void  OpenInExplorer(const std::wstring& path);
std::wstring ExeDir();
std::wstring LocalAppData();
std::wstring DesktopDir();
bool  MakeShortcut(const std::wstring& nameNoExt, const std::wstring& exe,
                   const std::wstring& args, const std::wstring& workDir);
bool  ProcAlive(HANDLE h);
uint64_t ProcRamBytes(HANDLE h);
void  CopyToClipboard(HWND hwnd, const std::wstring& text);
std::wstring PickFile(HWND hwnd, const wchar_t* filter, const wchar_t* title, const wchar_t* defExt);
std::wstring PickFolder(HWND hwnd, const wchar_t* title);
std::wstring WinVerString();
std::wstring EpochToIso(long long ms);
void  SleepMs(int ms);

// ---------------------------------------------------------------- core.cpp (settings/log/instances)
void InitPaths();
void LoadSettings();
bool SaveSettings();
void LoadInstances();
void PersistInstance(const InstanceCfg& c);          // write config.json
InstanceCfg* Inst(const std::wstring& id);
Runtime*     Rt(const std::wstring& id);
std::wstring CreateInstance(const InstanceCfg& tpl, std::wstring& err);
bool DeleteInstance(const std::wstring& id, bool keepBackup, std::wstring& err);
bool CloneInstance(const std::wstring& id, const std::wstring& newName, std::wstring& err);
bool RenameInstance(const std::wstring& id, const std::wstring& newName, std::wstring& err);
bool BackupInstance(const std::wstring& id, bool withDisk, std::wstring& err);
bool RestoreBackup(const std::wstring& id, std::wstring& err);
bool ExportInstance(const std::wstring& id, const std::wstring& dstDir, bool withDisk, std::wstring& err);
bool ImportInstance(const std::wstring& cfgPath, std::wstring& newId, std::wstring& err);
void SetStatus(const std::wstring& id, St st);
void NotifyStatus(const std::wstring& id);           // post WM_APP + invalidate
void LogW(const wchar_t* tag, const wchar_t* fmt, ...);
void ILog(const std::wstring& id, const wchar_t* kind, const std::wstring& line);
std::wstring InstanceLogPath(const std::wstring& id, const wchar_t* kind);
void CleanupOldLogs();
std::wstring NextInstanceId();
int  NextFreePort();
void EnsureDefaultSharedFolder(InstanceCfg& c);
std::wstring StatusText(St st);
COLORREF StatusColor(St st);

// ---------------------------------------------------------------- backend.cpp (qemu/adb/diag)
std::wstring FindTool(const std::wstring& configured, const wchar_t* subDir,
                      const wchar_t* exeName);
std::wstring QemuExe();
std::wstring QemuImg();
std::wstring AdbExe();
std::wstring AdbSerial(const InstanceCfg& c);
std::wstring AdbDirOf(const InstanceCfg& c);         // instance dir
std::wstring BuildQemuArgs(const InstanceCfg& c, std::wstring& err);
bool StartInstance(const std::wstring& id, std::wstring& err);
void StopInstance(const std::wstring& id, bool force);
void RestartInstance(const std::wstring& id);
bool AdbConnect(const InstanceCfg& c, std::wstring& err);
bool AdbInstall(const std::wstring& id, const std::wstring& apk, std::wstring& err);
bool AdbPush(const std::wstring& id, const std::wstring& file, std::wstring& err);
bool AdbPullDownloads(const std::wstring& id, const std::wstring& localDir, std::wstring& err);
bool AdbScreenshot(const std::wstring& id, std::wstring& outFile, std::wstring& err);
bool AdbKey(const std::wstring& id, int keycode, std::wstring& err);
bool AdbGetProp(const std::wstring& id, const wchar_t* prop, std::wstring& val);
bool AdbShell(const std::wstring& id, const std::wstring& cmd, std::wstring& out, std::wstring& err);
bool WhpxAvailable();
bool VirtFirmwareEnabled();
std::vector<DiagItem> RunDiagnostics();
bool CreateDiskImage(const InstanceCfg& c, std::wstring& err);
// Android keycodes
enum { K_BACK=4, K_HOME=3, K_RECENT=187, K_VOLUP=24, K_VOLDN=25, K_POWER=26, K_ROT=82 };

// ---------------------------------------------------------------- ui.cpp / pages.cpp / dialogs.cpp
enum class Pg { Home, Instances, Images, Apk, Backups, Snapshots, Keys, Perf, Files, Logs, Diag, Settings, About,
                Compat, Sensors, Advanced };   // stage 4-7 pages
extern Pg g_page;

// shared ui internals
void  MakeFonts();
void  DrawSidebar(HDC dc, RECT rc);
extern bool g_tracking;
std::vector<std::wstring> ListApks();

void UiNotify(const std::wstring& text, int type);   // 0 ok, 1 warn, 2 err (thread-safe)
void UiInvalidate();
int  UiRun(HINSTANCE hInst, int nCmdShow);
bool DialogInstance(HWND parent, InstanceCfg& c, bool isNew);
std::wstring DialogInput(HWND parent, const std::wstring& title, const std::wstring& def);
void OpenInstanceDialog(const std::wstring& id);     // edit existing (ui helper)

// app messages
#define WM_APP_STATUS   (WM_APP + 1)   // instance status changed
#define WM_APP_NOTIFY   (WM_APP + 2)   // lParam = new std::wstring*, wParam = type
#define WM_APP_QEXIT    (WM_APP + 3)   // lParam = new QExitMsg*
#define WM_APP_ADBOK    (WM_APP + 4)   // lParam = new std::wstring* (id)
#define WM_APP_DIAG     (WM_APP + 5)   // diagnostics done
#define WM_APP_OPDONE   (WM_APP + 6)   // lParam = new std::wstring* (id), wParam = 0 ok/1 err
#define WM_APP_CRASH    (WM_APP + 7)   // stage3: lParam = new std::wstring* (id) - show recovery dialog
#define WM_APP_PKG      (WM_APP + 8)   // stage3: package bootstrap progress -> repaint

struct QExitMsg { std::wstring id; HANDLE hp; };
int WinDpi(HWND w);
