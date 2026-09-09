// ============================================================================
//  NovaDroid - ui.h  UI framework internals shared between ui.cpp / pages.cpp
// ============================================================================
#pragma once
#include "app.h"

// ---------------------------------------------------------------- widget ids
enum {
    ID_NONE = 0,
    ID_NAV0 = 100,                       // +0..9
    ID_BTN_LAUNCH = 200, ID_BTN_STOPMAIN, ID_BTN_CREATE, ID_BTN_QUICK, ID_BTN_IMPORT,
    ID_CARD0 = 300,                      // +i  select card
    ID_INSTBTN = 1000,                   // +i*10 + slot ; slot0=start/stop, slot1=menu, slot2=restart
    ID_TB0 = 2000,                       // +0..8 toolbar
    ID_APK_ADD = 2100, ID_APK_OPEN, ID_APK_ROW = 2200,   // +i install
    ID_FILE_PICK = 2300, ID_FILE_PULL, ID_FILE_OPEN,
    ID_PERF_PROFILE = 2400,              // +0..3
    ID_PERF_DEC = 2500,                  // row r: dec = +r*2, inc = +r*2+1
    ID_PERF_APPLY = 2600,
    ID_DIAG_RUN = 2700, ID_DIAG_COPY, ID_DIAG_LOGS,
    ID_LOG_TAB = 2800,                   // +0..3
    ID_LOG_REFRESH = 2810, ID_LOG_FOLDER, ID_LOG_CLEAN, ID_LOG_COPY,
    ID_SET_LANGDEC = 2900, ID_SET_LANGINC,
    ID_SET_QEMU = 2910, ID_SET_QEMURST,
    ID_SET_ADB = 2920, ID_SET_IMAGE = 2930, ID_SET_DATA = 2940,
    ID_SET_CHECK = 2950, ID_SET_AUTORUN = 2952,
    ID_SET_PORTDEC = 2960, ID_SET_PORTINC,
    ID_SET_ACC0 = 2970,                  // +0..2
    ID_AB_LINK0 = 3000,                  // +0..2
    // ---- stage 2 ranges (v2ui_*.cpp)
    ID_IMG0 = 3100,                      // catalog row: i*5 + {0=download/cancel,1=sha,2=use,3=delete,4=mirror}
    ID_LOCIMG0 = 3250,                   // local image row: i*3 + {0=use,1=delete,2=sha}
    ID_BK0 = 3300,                       // backup row: i*4 + {0=restore,1=export,2=delete,3=open}
    ID_SNAP0 = 3500,                     // snapshot row: i*3 + {0=restore,1=delete,2=rename}
    ID_KM0 = 3400,                       // keymap row: i*4 + {0=toggle,1=add,2=delete,3=export}
    ID_KMBIND0 = 3450,                   // binding row: i*2 + {0=edit,1=delete}
    ID_KM_ADD = 3480, ID_KM_IMPORT, ID_KM_APPLY, ID_KM_SHOT,
    ID_KM_ACT0 = 3490,                   // action select: +0..8
    ID_APKMI0 = 3600,                    // multi-install dlg: +i checkboxes; 3890=install,3891=close,3892=snapfirst
    ID_WIZ0 = 3700,                      // wizard control space
};

// ---------------------------------------------------------------- widget kinds
enum { K_BTN = 1, K_NAV, K_CARD, K_LINK, K_SWATCH, K_TAB };

struct Wid { int id; RECT r; int kind; };

// ---------------------------------------------------------------- ui globals (ui.cpp)
extern int    g_dpi;
extern POINT  g_mouse;
extern int    g_hover, g_press;
extern std::vector<Wid> g_wids;
extern std::map<int, int> g_scroll;      // per-page scroll offset
extern int  g_spin;
extern unsigned long g_lastPaint;
struct Toast { std::wstring text; int type; long long t0; };
extern std::vector<Toast> g_toasts;
extern std::vector<std::wstring> g_fileOps;   // files page operation log
extern int g_logTab;                          // 0 launcher 1 qemu 2 adb 3 serial
extern std::vector<std::wstring> g_logLines;
extern int g_logScroll;
extern long long g_diagDoneTick;

// ---------------------------------------------------------------- drawing helpers (ui.cpp)
COLORREF Accent();
void     FillRound(HDC dc, RECT r, COLORREF c, int rad);
void     FrameRound(HDC dc, RECT r, COLORREF c, int rad, int w = 1);
void     Txt(HDC dc, RECT r, const std::wstring& s, HFONT f, COLORREF c, UINT fl);
void     TxtWrap(HDC dc, RECT r, const std::wstring& s, HFONT f, COLORREF c, int* linesOut);
HFONT    Fnt(int id);
enum FontId { F_TITLE = 0, F_H2, F_BODY, F_SMALL, F_NAV, F_NAVSEL, F_BIG, F_MONO, F_GLYPH, F_COUNT_ };
void     Reg(int id, RECT r, int kind);
void     Btn(HDC dc, int id, RECT r, const std::wstring& label, int style, bool enabled = true);
enum { BSTY_PRIMARY, BSTY_SURF, BSTY_GHOST, BSTY_DANGER, BSTY_OK };
void     Pill(HDC dc, RECT r, const std::wstring& s, COLORREF c);
void     Dot(HDC dc, int x, int y, int rad, COLORREF c);
void     DrawAndroidHead(HDC dc, int cx, int cy, int size, COLORREF body);
void     NavIcon(HDC dc, int idx, int x, int y, int size, COLORREF c);
void     GradientRect(HDC dc, RECT r, COLORREF top, COLORREF bottom);
COLORREF Lighten(COLORREF c, int amt);
COLORREF Mix(COLORREF a, COLORREF b, int t);   // t=0..100 toward b

// layout constants
constexpr int SIDEBAR_W = 232;
constexpr int PAD = 20;

// ---------------------------------------------------------------- pages (pages.cpp)
void PageHome(HDC dc, RECT rc);
void PageInstances(HDC dc, RECT rc);
void PageApk(HDC dc, RECT rc);
void PageFiles(HDC dc, RECT rc);
void PageKeys(HDC dc, RECT rc);
void PagePerf(HDC dc, RECT rc);
void PageDiag(HDC dc, RECT rc);
void PageLogs(HDC dc, RECT rc);
void PageSettings(HDC dc, RECT rc);
void PageAbout(HDC dc, RECT rc);

// page header helper
void PageHeader(HDC dc, const wchar_t* title, const wchar_t* sub, RECT rc, int* yOut);

// ---------------------------------------------------------------- actions (ui.cpp)
void OnClick(int id);
void HandleDrop(HDROP hd);
void DoStart(const std::wstring& id);
void DoInstallApk(const std::wstring& id, const std::wstring& apk);
void RunOpThread(std::function<void()> f);
std::wstring SelInst();                 // selected instance id or ""
void AppendFileOp(const std::wstring& s);
void ReloadLogView();
