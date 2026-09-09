// ============================================================================
//  NovaDroid - ui.cpp  Main window: custom dark UI (TZ 7.1), sidebar, Home and
//  Instances pages, message loop, async operations.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v3.h"

std::vector<std::wstring> ListApks();   // defined in pages.cpp

// ---------------------------------------------------------------- ui globals
int    g_dpi = 96;
POINT  g_mouse = { -1, -1 };
int    g_hover = -1, g_press = -1;
std::vector<Wid> g_wids;
std::map<int, int> g_scroll;
int  g_spin = 0;
unsigned long g_lastPaint = 0;
std::vector<Toast> g_toasts;
std::vector<std::wstring> g_fileOps;
int  g_logTab = 0;
std::vector<std::wstring> g_logLines;
int  g_logScroll = 0;
long long g_diagDoneTick = 0;
Pg   g_page = Pg::Home;

static HFONT g_fonts[F_COUNT_] = {};
bool  g_tracking = false;

// ---------------------------------------------------------------- color helpers
COLORREF Accent() {
    static COLORREF cached = CL_ACCENT;
    static std::wstring last;
    if (last != g_set.accentHex) {
        last = g_set.accentHex;
        std::wstring h = g_set.accentHex;
        if (!h.empty() && h[0] == L'#') h = h.substr(1);
        unsigned long v = wcstoul(h.c_str(), nullptr, 16);
        if (v) cached = CREF(v);
    }
    return cached;
}
COLORREF Lighten(COLORREF c, int amt) {
    int r = GetRValue(c) + amt, g = GetGValue(c) + amt, b = GetBValue(c) + amt;
    return RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}
COLORREF Mix(COLORREF a, COLORREF b, int t) {
    int r = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t / 100;
    int g = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t / 100;
    int bl = GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t / 100;
    return RGB(r, g, bl);
}

// ---------------------------------------------------------------- fonts
static HFONT MkFont(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}
void MakeFonts() {
    for (HFONT& f : g_fonts) if (f) { DeleteObject(f); f = nullptr; }
    g_fonts[F_TITLE]  = MkFont(21, FW_SEMIBOLD);
    g_fonts[F_H2]     = MkFont(14, FW_SEMIBOLD);
    g_fonts[F_BODY]   = MkFont(10, FW_NORMAL);
    g_fonts[F_SMALL]  = MkFont(9, FW_NORMAL);
    g_fonts[F_NAV]    = MkFont(11, FW_NORMAL);
    g_fonts[F_NAVSEL] = MkFont(11, FW_SEMIBOLD);
    g_fonts[F_BIG]    = MkFont(13, FW_SEMIBOLD);
    g_fonts[F_MONO]   = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    g_fonts[F_GLYPH]  = CreateFontW(-MulDiv(12, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
}
HFONT Fnt(int id) { return g_fonts[id]; }

// ---------------------------------------------------------------- draw prims
void FillRound(HDC dc, RECT r, COLORREF c, int rad) {
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    HPEN op = (HPEN)SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}
void FrameRound(HDC dc, RECT r, COLORREF c, int rad, int w) {
    HPEN p = CreatePen(PS_SOLID, w, c);
    HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN op = (HPEN)SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right - 1, r.bottom - 1, rad * 2, rad * 2);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(p);
}
void GradientRect(HDC dc, RECT r, COLORREF top, COLORREF bottom) {
    TRIVERTEX tv[2];
    tv[0] = { r.left, r.top, (USHORT)(GetRValue(top) << 8), (USHORT)(GetGValue(top) << 8),
              (USHORT)(GetBValue(top) << 8), 0xFF00 };
    tv[1] = { r.right, r.bottom, (USHORT)(GetRValue(bottom) << 8), (USHORT)(GetGValue(bottom) << 8),
              (USHORT)(GetBValue(bottom) << 8), 0xFF00 };
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(dc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}
void Txt(HDC dc, RECT r, const std::wstring& s, HFONT f, COLORREF c, UINT fl) {
    HFONT of = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &r, fl | DT_NOPREFIX | DT_WORDBREAK);
    SelectObject(dc, of);
}
int CountLines(HDC dc, RECT r, const std::wstring& s, HFONT f) {
    HFONT of = (HFONT)SelectObject(dc, f);
    RECT calc = r;
    int h = DrawTextW(dc, s.c_str(), -1, &calc, DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(dc, of);
    RECT one = { 0, 0, 100, 100 };
    UINT fl = DT_NOPREFIX | DT_CALCRECT;
    DrawTextW(dc, L"Xg", -1, &one, fl);
    return one.bottom > 0 ? h / one.bottom : 1;
}
void TxtWrap(HDC dc, RECT r, const std::wstring& s, HFONT f, COLORREF c, int* linesOut) {
    if (linesOut) *linesOut = CountLines(dc, r, s, f);
    Txt(dc, r, s, f, c, 0);
}
void Reg(int id, RECT r, int kind) { g_wids.push_back({ id, r, kind }); }

void Btn(HDC dc, int id, RECT r, const std::wstring& label, int style, bool enabled) {
    Reg(id, r, K_BTN);
    bool hov = (g_hover == id && enabled);
    bool prs = (g_press == id && hov);
    int rad = (r.bottom - r.top) / 2;
    rad = rad > 14 ? 14 : rad;
    if (style == BSTY_PRIMARY) {
        if (!enabled) FillRound(dc, r, CL_SURF, rad);
        else if (prs) GradientRect(dc, r, Lighten(Accent(), -30), Lighten(Accent(), -45));
        else if (hov) GradientRect(dc, r, Lighten(Accent(), 24), Lighten(Accent(), 6));
        else GradientRect(dc, r, Lighten(Accent(), 12), Accent());
        Txt(dc, r, label, Fnt(F_BIG), enabled ? CL_TEXT : CL_SUB,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (style == BSTY_DANGER) {
        FillRound(dc, r, enabled ? (prs ? Lighten(CL_ERR, -30) : hov ? Lighten(CL_ERR, 20) : CL_ERR) : CL_SURF, rad);
        Txt(dc, r, label, Fnt(F_BODY), enabled ? CL_TEXT : CL_SUB,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (style == BSTY_OK) {
        FillRound(dc, r, enabled ? (prs ? Lighten(CL_OK, -30) : hov ? Lighten(CL_OK, 20) : CL_OK) : CL_SURF, rad);
        Txt(dc, r, label, Fnt(F_BODY), enabled ? CL_TEXT : CL_SUB,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        COLORREF bg = style == BSTY_SURF ? CL_SURF2 : CL_SURF;
        if (prs) bg = Lighten(bg, -14);
        else if (hov) bg = Lighten(bg, 12);
        FillRound(dc, r, bg, rad);
        if (style == BSTY_SURF) FrameRound(dc, r, CL_BORDER, rad);
        Txt(dc, r, label, Fnt(F_BODY), enabled ? CL_TEXT : CL_SUB,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
void Pill(HDC dc, RECT r, const std::wstring& s, COLORREF c) {
    FillRound(dc, r, Mix(CL_BG, c, 22), (r.bottom - r.top) / 2);
    Txt(dc, r, s, Fnt(F_SMALL), c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
void Dot(HDC dc, int x, int y, int rad, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    HPEN op = (HPEN)SelectObject(dc, p);
    Ellipse(dc, x - rad, y - rad, x + rad, y + rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}
void DrawAndroidHead(HDC dc, int cx, int cy, int size, COLORREF body) {
    int w = size, h = size * 2 / 3;
    int l = cx - w / 2, t = cy - h / 2;
    // antennae
    HPEN p = CreatePen(PS_SOLID, 2, body);
    HPEN op = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, l + w / 4, t - size / 8, nullptr); LineTo(dc, l + w / 4 + size / 6, t + 4);
    MoveToEx(dc, l + 3 * w / 4, t - size / 8, nullptr); LineTo(dc, l + 3 * w / 4 - size / 6, t + 4);
    SelectObject(dc, op); DeleteObject(p);
    // head (upper half ellipse + rect)
    HBRUSH b = CreateSolidBrush(body);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, l, t, l + w, t + h * 2);
    RECT clip = { l, t + h / 2 + 1, l + w, t + h * 2 };
    FillRect(dc, &clip, b);
    // eyes
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    int er = size / 14 + 1;
    Ellipse(dc, l + w / 4 - er, t + h / 3 - er, l + w / 4 + er, t + h / 3 + er);
    Ellipse(dc, l + 3 * w / 4 - er, t + h / 3 - er, l + 3 * w / 4 + er, t + h / 3 + er);
    SelectObject(dc, ob);
    DeleteObject(b);
}
void NavIcon(HDC dc, int idx, int x, int y, int s, COLORREF c) {
    HPEN p = CreatePen(PS_SOLID, 2, c);
    HPEN op = (HPEN)SelectObject(dc, p);
    HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    // remap page order (TZ 2.0 §16.1) to icon drawing routines
    int m2 = idx;   // icon selector
    MoveToEx(dc, x, y, nullptr);
    switch (m2) {
        case 0:  // home: house
            MoveToEx(dc, x + s / 2, y, nullptr); LineTo(dc, x, y + s / 3);
            LineTo(dc, x, y + s); MoveToEx(dc, x + s / 2, y, nullptr);
            LineTo(dc, x + s, y + s / 3); LineTo(dc, x + s, y + s);
            MoveToEx(dc, x, y + s, nullptr); LineTo(dc, x + s, y + s);
            break;
        case 1:  // instances: two rounded rects
            Rectangle(dc, x, y + s / 4, x + s / 2, y + 3 * s / 4);
            Rectangle(dc, x + s / 2, y + s / 4, x + s, y + 3 * s / 4);
            break;
        case 2:  // images: CD disc
            Ellipse(dc, x, y, x + s, y + s);
            Ellipse(dc, x + 2 * s / 5, y + 2 * s / 5, x + 3 * s / 5, y + 3 * s / 5);
            break;
        case 3:  // apk: box + down arrow
            Rectangle(dc, x, y + s / 2, x + s, y + s);
            MoveToEx(dc, x + s / 2, y, nullptr); LineTo(dc, x + s / 2, y + 2 * s / 3);
            MoveToEx(dc, x + s / 4, y + s / 3, nullptr); LineTo(dc, x + s / 2, y + 2 * s / 3);
            LineTo(dc, x + 3 * s / 4, y + s / 3);
            break;
        case 4:  // backups: box with lid
            Rectangle(dc, x, y + s / 3, x + s, y + s);
            MoveToEx(dc, x, y + s / 2, nullptr); LineTo(dc, x + s, y + s / 2);
            Rectangle(dc, x + s / 3, y, x + 2 * s / 3, y + s / 3);
            break;
        case 5:  // snapshots: camera
            Rectangle(dc, x, y + s / 4, x + s, y + s);
            Rectangle(dc, x + s / 3, y, x + 2 * s / 3, y + s / 4);
            Ellipse(dc, x + s / 3, y + 2 * s / 5, x + 2 * s / 3, y + 4 * s / 5);
            break;
        case 6:  // keymapping: keyboard
            Rectangle(dc, x, y + s / 4, x + s, y + 3 * s / 4);
            MoveToEx(dc, x + s / 5, y + s / 2, nullptr); LineTo(dc, x + 4 * s / 5, y + s / 2);
            break;
        case 7:  // perf: gauge
            Arc(dc, x, y + s / 5, x + s, y + s + s / 5, 0, 0, 0, 0);
            MoveToEx(dc, x + s / 2, y + 3 * s / 5, nullptr); LineTo(dc, x + 3 * s / 4, y + s / 3);
            break;
        case 8:  // files: folder
            Rectangle(dc, x, y + s / 4, x + s, y + s);
            MoveToEx(dc, x, y + s / 4, nullptr); LineTo(dc, x + s / 3, y + s / 4);
            break;
        case 9:  // logs: document lines
            Rectangle(dc, x + s / 5, y, x + 4 * s / 5, y + s);
            MoveToEx(dc, x + s / 3, y + s / 4, nullptr); LineTo(dc, x + 2 * s / 3, y + s / 4);
            MoveToEx(dc, x + s / 3, y + s / 2, nullptr); LineTo(dc, x + 2 * s / 3, y + s / 2);
            MoveToEx(dc, x + s / 3, y + 3 * s / 4, nullptr); LineTo(dc, x + 2 * s / 3, y + 3 * s / 4);
            break;
        case 10: // diag: magnifier
            Ellipse(dc, x, y, x + 2 * s / 3, y + 2 * s / 3);
            MoveToEx(dc, x + 2 * s / 3, y + 2 * s / 3, nullptr); LineTo(dc, x + s, y + s);
            break;
        case 11: // settings: gear (circle + stubs)
            Ellipse(dc, x + s / 4, y + s / 4, x + 3 * s / 4, y + 3 * s / 4);
            for (int i = 0; i < 6; ++i) {
                double a = 3.14159 * i / 3;
                int x1 = x + s / 2 + (int)(cos(a) * s * 0.32), y1 = y + s / 2 + (int)(sin(a) * s * 0.32);
                int x2 = x + s / 2 + (int)(cos(a) * s * 0.48), y2 = y + s / 2 + (int)(sin(a) * s * 0.48);
                MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2);
            }
            break;
        case 12: // about: circle + i
            Ellipse(dc, x, y, x + s, y + s);
            MoveToEx(dc, x + s / 2, y + s / 3, nullptr); LineTo(dc, x + s / 2, y + 3 * s / 4);
            Ellipse(dc, x + s / 2 - 2, y + s / 5 - 2, x + s / 2 + 2, y + s / 5 + 2);
            break;
        case 13: // compatibility: shield + check (TZ4)
            MoveToEx(dc, x + s / 2, y, nullptr); LineTo(dc, x + s, y + s / 5);
            LineTo(dc, x + s, y + 3 * s / 5); LineTo(dc, x + s / 2, y + s);
            LineTo(dc, x, y + 3 * s / 5); LineTo(dc, x, y + s / 5); LineTo(dc, x + s / 2, y);
            MoveToEx(dc, x + s / 4, y + s / 2, nullptr); LineTo(dc, x + s / 2 - 2, y + 3 * s / 4);
            LineTo(dc, x + 3 * s / 4, y + s / 3);
            break;
        case 14: // sensors: antenna wave (TZ4)
            Ellipse(dc, x + 3 * s / 8, y + 3 * s / 8, x + 5 * s / 8, y + 5 * s / 8);
            Arc(dc, x, y, x + s, y + s, 600, 600, 3600, 1200);
            Arc(dc, x + s / 4, y + s / 4, x + 3 * s / 4, y + 3 * s / 4, 600, 600, 3600, 1200);
            break;
        case 15: // advanced: bolt (TZ6-7)
            MoveToEx(dc, x + 3 * s / 5, y, nullptr); LineTo(dc, x + s / 5, y + 3 * s / 5);
            LineTo(dc, x + s / 2, y + 3 * s / 5); LineTo(dc, x + 2 * s / 5, y + s);
            LineTo(dc, x + 4 * s / 5, y + 2 * s / 5); LineTo(dc, x + s / 2, y + 2 * s / 5);
            LineTo(dc, x + 3 * s / 5, y);
            break;
        default: // fallback: circle + i
            Ellipse(dc, x, y, x + s, y + s);
            MoveToEx(dc, x + s / 2, y + s / 3, nullptr); LineTo(dc, x + s / 2, y + 3 * s / 4);
            Ellipse(dc, x + s / 2 - 2, y + s / 5 - 2, x + s / 2 + 2, y + s / 5 + 2);
            break;
    }
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(p);
}

// ---------------------------------------------------------------- sidebar
void DrawSidebar(HDC dc, RECT rc) {
    RECT sb = { 0, 0, SIDEBAR_W, rc.bottom };
    FillRect(dc, &sb, (HBRUSH)GetStockObject(BLACK_BRUSH)); // will repaint below
    HBRUSH b = CreateSolidBrush(Mix(CL_BG, CL_SURF, 45));
    FillRect(dc, &sb, b);
    DeleteObject(b);

    // logo
    int m = MulDiv(20, g_dpi, 96);
    RECT logo = { m, m, m + MulDiv(40, g_dpi, 96), m + MulDiv(40, g_dpi, 96) };
    HFONT bigL = CreateFontW(-MulDiv(24, g_dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    // draw rounded logo
    HRGN rg = CreateRoundRectRgn(logo.left, logo.top, logo.right, logo.bottom, 12, 12);
    SelectClipRgn(dc, rg);
    GradientRect(dc, logo, Lighten(Accent(), 20), Accent());
    SelectClipRgn(dc, nullptr);
    DeleteObject(rg);
    wchar_t L2[2] = { 'N', 0 };
    RECT lr2 = logo;
    Txt(dc, lr2, L2, bigL, CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(bigL);
    HFONT of = (HFONT)SelectObject(dc, Fnt(F_H2));
    SetTextColor(dc, CL_TEXT); SetBkMode(dc, TRANSPARENT);
    RECT rt = { logo.right + m / 2, m + MulDiv(2, g_dpi, 96), SIDEBAR_W, m + MulDiv(24, g_dpi, 96) };
    DrawTextW(dc, T(S_APP_NAME), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    HFONT os = (HFONT)SelectObject(dc, Fnt(F_SMALL));
    SetTextColor(dc, CL_SUB);
    RECT rs = { logo.right + m / 2, m + MulDiv(26, g_dpi, 96), SIDEBAR_W, m + MulDiv(42, g_dpi, 96) };
    DrawTextW(dc, T(S_APP_SUB), -1, &rs, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, of); SelectObject(dc, os);
    (void)os; (void)of;

    // nav items (TZ 2.0 §16.1 + TZ4-7 pages)
    static const int navIds[] = { S_NAV_HOME, S_NAV_INST, S_NAV_IMG, S_NAV_APK, S_NAV_BACKUPS,
                                  S_NAV_SNAPS, S_NAV_KEYS, S_NAV_PERF, S_NAV_FILES, S_NAV_LOGS,
                                  S_NAV_DIAG, S_NAV_SET, S_NAV_ABOUT,
                                  S4_NAV_COMPAT, S4_NAV_SENSORS, S4_NAV_ADVANCED };
    int y = m + MulDiv(64, g_dpi, 96);
    int ih = MulDiv(36, g_dpi, 96);
    for (int i = 0; i < 16; ++i) {
        RECT r = { m, y, SIDEBAR_W - m, y + ih };
        bool sel = (g_page == (Pg)i);
        bool hov = (g_hover == ID_NAV0 + i);
        if (sel) {
            RECT hl = { m, y, SIDEBAR_W - m / 2, y + ih };
            FillRound(dc, hl, Mix(Accent(), CL_SURF, 78), 10);
            HBRUSH ab = CreateSolidBrush(Accent());
            RECT bar2 = { m, y + 8, m + 3, y + ih - 8 };
            FillRect(dc, &bar2, ab);
            DeleteObject(ab);
        } else if (hov) {
            RECT hl = { m, y, SIDEBAR_W - m / 2, y + ih };
            FillRound(dc, hl, Mix(CL_BG, CL_SURF, 80), 10);
        }
        NavIcon(dc, i, m + MulDiv(10, g_dpi, 96), y + ih / 2 - MulDiv(9, g_dpi, 96),
                MulDiv(18, g_dpi, 96), sel ? Accent() : CL_SUB);
        RECT tr = { m + MulDiv(40, g_dpi, 96), y, SIDEBAR_W - m, y + ih };
        Txt(dc, tr, T(navIds[i]), sel ? Fnt(F_NAVSEL) : Fnt(F_NAV), sel ? CL_TEXT : CL_SUB,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Reg(ID_NAV0 + i, r, K_NAV);
    y += ih + MulDiv(3, g_dpi, 96);
    }

    // footer
    RECT ft = { m, rc.bottom - MulDiv(52, g_dpi, 96), SIDEBAR_W - m, rc.bottom - MulDiv(30, g_dpi, 96) };
    Txt(dc, ft, WinVerString(), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
    RECT ft2 = { m, rc.bottom - MulDiv(30, g_dpi, 96), SIDEBAR_W - m, rc.bottom - MulDiv(8, g_dpi, 96) };
    Txt(dc, ft2, L"© NovaDroid Project · MIT", Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
}

// ---------------------------------------------------------------- Home page
void FillRoundSafe(HDC, RECT, COLORREF, int) {}

static std::wstring IsoToDisplay(const std::wstring& iso) {
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, s = 0;
    if (swscanf(iso.c_str(), L"%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &s) == 6)
        return Fmt(L"%02d.%02d.%04d %02d:%02d", D, M, Y, h, mi);
    return iso;
}

static void DrawInstanceCardHome(HDC dc, RECT r, InstanceCfg* c) {
    Runtime* rt = Rt(c->id);
    St st = rt ? rt->st : St::Stopped;
    FillRound(dc, r, CL_SURF, 14);
    FrameRound(dc, r, CL_BORDER, 14);

    int m = MulDiv(18, g_dpi, 96);
    int iconSize = MulDiv(56, g_dpi, 96);
    DrawAndroidHead(dc, r.left + m + iconSize / 2, r.top + m + iconSize / 2, iconSize,
                    st == St::Running ? CL_OK : Accent());

    RECT nr = { r.left + m + iconSize + m, r.top + m - MulDiv(2, g_dpi, 96), r.right - m, r.top + m + MulDiv(26, g_dpi, 96) };
    Txt(dc, nr, c->name, Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    std::wstring verline = c->androidVersion.empty() ? std::wstring(L"Android x86_64") : c->androidVersion;
    RECT vr = { r.left + m + iconSize + m, r.top + m + MulDiv(28, g_dpi, 96), r.right - m, r.top + m + MulDiv(46, g_dpi, 96) };
    Txt(dc, vr, verline, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);

    // status pill
    RECT pill = { r.right - m - MulDiv(120, g_dpi, 96), r.top + m, r.right - m, r.top + m + MulDiv(24, g_dpi, 96) };
    Pill(dc, pill, StatusText(st), StatusColor(st));

    // spec chips
    int cy = r.top + m + iconSize + MulDiv(10, g_dpi, 96);
    std::wstring specs = Fmt(L"CPU: %d   ·   RAM: %d MB   ·   Диск: %d GB   ·   %dx%d   ·   DPI %d   ·   %d FPS",
                             c->cpuCores, c->ramMb, c->diskGb, c->resW, c->resH, c->dpi, c->fpsLimit);
    RECT sr = { r.left + m, cy, r.right - m, cy + MulDiv(22, g_dpi, 96) };
    Txt(dc, sr, specs, Fnt(F_BODY), CL_SUB, DT_LEFT | DT_SINGLELINE);
    RECT lr = { r.left + m, cy + MulDiv(24, g_dpi, 96), r.right - m, cy + MulDiv(42, g_dpi, 96) };
    std::wstring last = c->lastLaunchAt.empty()
        ? (g_lang == 0 ? L"Инстанс ещё не запускался" : L"Instance never launched")
        : ((g_lang == 0 ? L"Последний запуск: " : L"Last launch: ") + IsoToDisplay(c->lastLaunchAt));
    Txt(dc, lr, last, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
}

void PageHome(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int rightW = MulDiv(320, g_dpi, 96);
    int leftW = rc.right - x0 - m - rightW - m;
    if (leftW < MulDiv(420, g_dpi, 96)) { leftW = rc.right - x0 - m; rightW = 0; }

    // header
    RECT h1 = { x0, m, x0 + leftW, m + MulDiv(40, g_dpi, 96) };
    Txt(dc, h1, T(S_WELCOME), Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    RECT h2 = { x0, m + MulDiv(40, g_dpi, 96), x0 + leftW, m + MulDiv(60, g_dpi, 96) };
    Txt(dc, h2, T(S_WELCOME_SUB), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);

    // main instance card
    InstanceCfg* c = Inst(g_selId);
    if (!c && !g_insts.empty()) { g_selId = g_insts[0].id; c = Inst(g_selId); }
    int cardTop = m + MulDiv(76, g_dpi, 96);
    int cardH = MulDiv(190, g_dpi, 96);
    RECT card = { x0, cardTop, x0 + leftW, cardTop + cardH };
    if (c) {
        DrawInstanceCardHome(dc, card, c);
        Reg(ID_CARD0, card, K_CARD);
    } else {
        FillRound(dc, card, CL_SURF, 14);
        FrameRound(dc, card, CL_BORDER, 14);
        Txt(dc, card, g_lang == 0 ? L"Инстансов пока нет. Создайте первый инстанс!" :
                                     L"No instances yet. Create your first one!",
            Fnt(F_H2), CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // buttons row
    int by = card.bottom + m;
    int bh = MulDiv(50, g_dpi, 96);
    bool hasC = c != nullptr;
    Runtime* rt = c ? Rt(c->id) : nullptr;
    St st = rt ? rt->st : St::Stopped;
    bool busy = rt && rt->busy;
    bool running = (st == St::Running);
    bool starting = (st == St::Starting || st == St::Stopping);
    int bw = MulDiv(210, g_dpi, 96);
    RECT lb = { x0, by, x0 + bw, by + bh };
    if (starting) {
        static const wchar_t* sp = L"|/-\\";
        wchar_t buf[64];
        swprintf(buf, 64, L"%c %s", sp[g_spin % 4], st == St::Stopping ? T(S_BTN_STOP) : T(S_ST_STARTING));
        Btn(dc, 0, lb, buf, BSTY_SURF, false);
    } else if (running) {
        Btn(dc, ID_BTN_STOPMAIN, lb, T(S_BTN_STOP), BSTY_DANGER, !busy);
    } else {
        Btn(dc, ID_BTN_LAUNCH, lb, T(S_BTN_LAUNCH), BSTY_PRIMARY, hasC && !busy);
    }
    RECT cb = { x0 + bw + m / 2, by, x0 + bw + m / 2 + MulDiv(180, g_dpi, 96), by + MulDiv(40, g_dpi, 96) };
    Btn(dc, ID_BTN_CREATE, cb, L"＋ " + std::wstring(T(S_BTN_CREATE)), BSTY_SURF);
    RECT qb = { cb.right + m / 2, by, cb.right + m / 2 + MulDiv(170, g_dpi, 96), by + MulDiv(40, g_dpi, 96) };
    Btn(dc, ID_BTN_QUICK, qb, T(S_BTN_QUICK), BSTY_GHOST);

    // controls card (running instance toolbar)
    int ty = by + bh + m;
    if (c && (running || starting)) {
        RECT tb = { x0, ty, x0 + leftW, ty + MulDiv(196, g_dpi, 96) };
        FillRound(dc, tb, CL_SURF, 14);
        FrameRound(dc, tb, CL_BORDER, 14);
        RECT tt = { tb.left + m, tb.top + m / 2, tb.right - m, tb.top + m / 2 + MulDiv(20, g_dpi, 96) };
        Txt(dc, tt, T(S_TB_TITLE), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        // grid of buttons 5 x 2
        const int ids[12] = { ID_TB0 + 0, ID_TB0 + 1, ID_TB0 + 2, ID_TB0 + 3, ID_TB0 + 4,
                              ID_TB0 + 5, ID_TB0 + 6, ID_TB0 + 7, ID_TB0 + 8,
                              ID_TB0 + 9, ID_TB0 + 10, ID_TB0 + 11 };
        const wchar_t* labels[12] = { T(S_TB_BACK), T(S_TB_HOME), T(S_TB_REC), T(S_TB_VOLUP), T(S_TB_VOLDN),
                                      T(S_TB_SHOT), T(S_TB_ROT), T(S_TB_APK), T(S_TB_STOP),
                                      T(S3_TB_FULL), T(S3_TB_CAP), T(S3_TB_MUTE) };
        int gw = (tb.right - tb.left - 2 * m - 3 * m / 2) / 4;
        int gh = MulDiv(34, g_dpi, 96);
        for (int i = 0; i < 12; ++i) {
            int col = i % 4, row = i / 4;
            RECT r = { tb.left + m + col * (gw + m / 2),
                       tb.top + m / 2 + MulDiv(24, g_dpi, 96) + row * (gh + m / 2),
                       tb.left + m + col * (gw + m / 2) + gw,
                       tb.top + m / 2 + MulDiv(24, g_dpi, 96) + row * (gh + m / 2) + gh };
            Btn(dc, ids[i], r, labels[i], i == 8 ? BSTY_DANGER : (i >= 9 ? BSTY_OK : BSTY_SURF), running && !busy);
        }
        ty = tb.bottom + m;
    }

    // right column: system state
    if (rightW > 0) {
        int rx = rc.right - m - rightW;
        RECT sys = { rx, cardTop, rc.right - m, cardTop + MulDiv(240, g_dpi, 96) };
        FillRound(dc, sys, CL_SURF, 14);
        FrameRound(dc, sys, CL_BORDER, 14);
        RECT st1 = { sys.left + m, sys.top + m / 2, sys.right - m, sys.top + m / 2 + MulDiv(22, g_dpi, 96) };
        Txt(dc, st1, T(S_SYS_STATE), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        int ry = sys.top + m / 2 + MulDiv(30, g_dpi, 96);
        int rowH = MulDiv(26, g_dpi, 96);
        // mini-diag from cached g_diag (8 rows)
        for (size_t i = 0; i < g_diag.size() && i < 8; ++i) {
            int dotX = sys.left + m + MulDiv(6, g_dpi, 96);
            int dotY = ry + rowH / 2;
            COLORREF col = g_diag[i].level == 0 ? CL_OK : g_diag[i].level == 1 ? CL_WARN :
                           g_diag[i].level == 2 ? CL_ERR : CL_ACCENT;
            Dot(dc, dotX, dotY, MulDiv(4, g_dpi, 96), col);
            RECT rr = { sys.left + m + MulDiv(18, g_dpi, 96), ry, sys.right - m, ry + rowH };
            Txt(dc, rr, g_diag[i].name, Fnt(F_SMALL), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT vr = { sys.left + m + MulDiv(150, g_dpi, 96), ry, sys.right - m, ry + rowH };
            Txt(dc, vr, g_diag[i].msg, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            ry += rowH;
        }
        if (g_diag.empty()) {
            RECT wr = { sys.left + m, ry, sys.right - m, ry + rowH };
            Txt(dc, wr, g_lang == 0 ? L"Проверка компонентов…" : L"Checking components…",
                Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // qemu usage card
        if (c && (running || starting)) {
            RECT us = { rx, sys.bottom + m, rc.right - m, sys.bottom + m + MulDiv(92, g_dpi, 96) };
            FillRound(dc, us, CL_SURF, 14);
            FrameRound(dc, us, CL_BORDER, 14);
            RECT ur = { us.left + m, us.top + m / 2, us.right - m, us.top + m / 2 + MulDiv(20, g_dpi, 96) };
            Txt(dc, ur, T(S_TB_RAMDROP), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
            uint64_t ram = rt && rt->hProc ? ProcRamBytes(rt->hProc) : 0;
            RECT u2 = { us.left + m, us.top + m / 2 + MulDiv(20, g_dpi, 96), us.right - m, us.top + m / 2 + MulDiv(44, g_dpi, 96) };
            Txt(dc, u2, HumanSize(ram), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            long long up = rt ? ((long long)GetTickCount64() - rt->startedAtMs) / 1000 : 0;
            RECT u3 = { us.left + m, us.top + m / 2 + MulDiv(46, g_dpi, 96), us.right - m, us.top + m / 2 + MulDiv(66, g_dpi, 96) };
            Txt(dc, u3, Fmt(L"%s: %02d:%02d:%02d", T(S_TB_UPTIME), (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60)),
                Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        }
    }

    // recent APK chips
    int ay = ty + MulDiv(4, g_dpi, 96);
    if (ay < rc.bottom - MulDiv(60, g_dpi, 96)) {
        RECT ar = { x0, ay, x0 + leftW, ay + MulDiv(24, g_dpi, 96) };
        Txt(dc, ar, T(S_LAST_APK), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT al = { ar.left, ar.top, ar.right + MulDiv(200, g_dpi, 96), ar.bottom };
        Txt(dc, al, T(S_OPEN_APKLIB), Fnt(F_SMALL), Accent(), DT_RIGHT | DT_SINGLELINE);
        Reg(ID_NONE + 77, al, K_LINK); // link -> handled as apk lib shortcut below
        int cy2 = ay + MulDiv(30, g_dpi, 96);
        std::vector<std::wstring> apks = ListApks();
        int cx = x0;
        int chipH = MulDiv(28, g_dpi, 96);
        for (size_t i = 0; i < apks.size() && i < 4; ++i) {
            std::wstring nm = BaseName(apks[i]);
            if (nm.size() > 28) nm = nm.substr(0, 27) + L"…";
            int cw = MulDiv(180, g_dpi, 96);
            RECT chip = { cx, cy2, cx + cw, cy2 + chipH };
            FillRound(dc, chip, CL_SURF2, chipH / 2);
            FrameRound(dc, chip, CL_BORDER, chipH / 2);
            Txt(dc, chip, nm, Fnt(F_SMALL), CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            cx += cw + m / 2;
        }
        if (apks.empty()) {
            RECT chip = { cx, cy2, cx + MulDiv(320, g_dpi, 96), cy2 + chipH };
            FillRound(dc, chip, CL_SURF, chipH / 2);
            Txt(dc, chip, T(S_APK_EMPTY), Fnt(F_SMALL), CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

// helper declared in pages.cpp
std::vector<std::wstring> ListApks();

// ---------------------------------------------------------------- instances page
void PageInstances(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_INST_TITLE), T(S_INST_SUB), rc, &y);
    y += MulDiv(6, g_dpi, 96);

    // header buttons
    int bh = MulDiv(38, g_dpi, 96);
    RECT nb = { rc.right - m - MulDiv(180, g_dpi, 96), y - bh - MulDiv(4, g_dpi, 96), rc.right - m, y - MulDiv(4, g_dpi, 96) };
    Btn(dc, ID_BTN_CREATE, nb, L"＋ " + std::wstring(T(S_BTN_CREATE)), BSTY_PRIMARY);
    RECT ib = { nb.left - m / 2 - MulDiv(120, g_dpi, 96), nb.top, nb.left - m / 2, nb.bottom };
    Btn(dc, ID_BTN_IMPORT, ib, L"⤓ " + std::wstring(T(S_BTN_IMPORT)), BSTY_SURF);
    y += MulDiv(8, g_dpi, 96);

    int scroll = g_scroll.count((int)Pg::Instances) ? g_scroll[(int)Pg::Instances] : 0;
    RECT clip = { 0, y, rc.right, rc.bottom };
    SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);

    int cardH = MulDiv(104, g_dpi, 96);
    for (size_t i = 0; i < g_insts.size(); ++i) {
        InstanceCfg& c = g_insts[i];
        Runtime* rt = Rt(c.id);
        St st = rt ? rt->st : St::Stopped;
        int top = y + (int)i * (cardH + m / 2) - scroll;
        if (top + cardH < y) continue;
        if (top > rc.bottom) break;
        RECT card = { x0, top, rc.right - m, top + cardH };
        bool sel = (g_selId == c.id);
        FillRound(dc, card, sel ? Mix(CL_SURF, Accent(), 10) : CL_SURF, 14);
        FrameRound(dc, card, sel ? Accent() : CL_BORDER, 14);
        Reg(ID_CARD0 + (int)i, card, K_CARD);

        int im = MulDiv(16, g_dpi, 96);
        DrawAndroidHead(dc, card.left + im + MulDiv(22, g_dpi, 96), card.top + cardH / 2,
                        MulDiv(44, g_dpi, 96), st == St::Running ? CL_OK : Accent());
        int tx = card.left + im + MulDiv(56, g_dpi, 96);
        RECT nr = { tx, card.top + im, tx + MulDiv(400, g_dpi, 96), card.top + im + MulDiv(24, g_dpi, 96) };
        Txt(dc, nr, c.name, Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        std::wstring verline = c.androidVersion.empty() ? std::wstring(L"Android x86_64") : c.androidVersion;
        RECT vr = { tx, card.top + im + MulDiv(24, g_dpi, 96), tx + MulDiv(500, g_dpi, 96), card.top + im + MulDiv(42, g_dpi, 96) };
        Txt(dc, vr, verline, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        std::wstring specs = Fmt(L"%d CPU · %d MB · %dx%d · ADB :%d", c.cpuCores, c.ramMb, c.resW, c.resH, c.adbPort);
        RECT sr = { tx, card.bottom - im - MulDiv(18, g_dpi, 96), tx + MulDiv(500, g_dpi, 96), card.bottom - im };
        Txt(dc, sr, specs, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);

        RECT pill = { card.right - MulDiv(400, g_dpi, 96), card.top + im, card.right - MulDiv(280, g_dpi, 96), card.top + im + MulDiv(24, g_dpi, 96) };
        Pill(dc, pill, StatusText(st), StatusColor(st));

        bool busy = rt && rt->busy;
        bool canStart = (st == St::Stopped || st == St::StartError) && !busy;
        bool canStop = (st == St::Running || st == St::Starting) && !busy;
        int bw = MulDiv(110, g_dpi, 96);
        int bh2 = MulDiv(34, g_dpi, 96);
        int bx = card.right - im - bw;
        int by2 = card.top + (cardH - bh2) / 2;
        RECT sb = { bx, by2, bx + bw, by2 + bh2 };
        if (canStop) Btn(dc, ID_INSTBTN + (int)i * 10 + 0, sb, T(S_BTN_STOP), BSTY_DANGER);
        else Btn(dc, ID_INSTBTN + (int)i * 10 + 0, sb, T(S_BTN_RUN), BSTY_PRIMARY, canStart);
        bx -= (bw + m / 2);
        RECT rb = { bx, by2, bx + bw, by2 + bh2 };
        Btn(dc, ID_INSTBTN + (int)i * 10 + 2, rb, T(S_BTN_RESTART), BSTY_SURF, st == St::Running && !busy);
        RECT mb = { card.right - im - MulDiv(36, g_dpi, 96), by2, card.right - im, by2 + bh2 };
        Btn(dc, ID_INSTBTN + (int)i * 10 + 1, mb, L"⋯", BSTY_SURF);
    }
    RestoreDC(dc, -1);

    if (g_insts.empty()) {
        RECT er = { x0, y + MulDiv(20, g_dpi, 96), rc.right - m, y + MulDiv(80, g_dpi, 96) };
        Txt(dc, er, g_lang == 0 ? L"Нет инстансов. Нажмите «Создать инстанс»." :
                                   L"No instances. Press 'Create instance'.",
            Fnt(F_BODY), CL_SUB, DT_CENTER | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------- page header
void PageHeader(HDC dc, const wchar_t* title, const wchar_t* sub, RECT rc, int* yOut) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    RECT t = { x0, *yOut, rc.right - m, *yOut + MulDiv(34, g_dpi, 96) };
    Txt(dc, t, title, Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    RECT s = { x0, *yOut + MulDiv(34, g_dpi, 96), rc.right - m, *yOut + MulDiv(52, g_dpi, 96) };
    Txt(dc, s, sub, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
    *yOut += MulDiv(64, g_dpi, 96);
}

// ---------------------------------------------------------------- interaction helpers
std::wstring SelInst() {
    if (!g_selId.empty() && Inst(g_selId)) return g_selId;
    return g_insts.empty() ? L"" : g_insts[0].id;
}
void AppendFileOp(const std::wstring& s) {
    g_fileOps.push_back(NowIso().substr(11) + L"  " + s);
    if (g_fileOps.size() > 200) g_fileOps.erase(g_fileOps.begin());
}
void RunOpThread(std::function<void()> f) {
    std::thread([f] {
        try { f(); } catch (...) { LogW(L"ui", L"op thread exception"); }
    }).detach();
}
void UiNotify(const std::wstring& text, int type) {
    if (g_wnd) PostMessageW(g_wnd, WM_APP_NOTIFY, (WPARAM)type, (LPARAM)new std::wstring(text));
}

void DoStart(const std::wstring& id) {
    // stage 3: full-package bootstrap gate - emulator starts only after
    // the package has been downloaded, verified (SHA-256) and extracted
    if (!PkgInstalled()) {
        UiNotify(T(S3_PKG_NO), 1);
        V3DownloadWizard(g_wnd);
        return;
    }
    RunOpThread([id] {
        // TZ 5.3: resource guard before launch
        {
            std::lock_guard<std::mutex> lk(g_mx);
            InstanceCfg* cg = Inst(id);
            if (cg) {
                ResourceVerdict v = CanStartInstance(*cg);
                if (!v.allowed) {
                    UiNotify(v.msg, 1);
                    return;
                }
            }
        }
        std::wstring err;
        if (!StartInstance(id, err)) {
            LogW(L"ui", L"start failed: %s", err.c_str());
            UiNotify(err.empty() ? T(S_MB_RUNERR) : err, 2);
            SetStatus(id, St::StartError);
        } else {
            UiNotify(g_lang == 0 ? L"Запуск Android…" : L"Starting Android…", 3);
        }
        UiInvalidate();
    });
}
void DoInstallApk(const std::wstring& id, const std::wstring& apk) {
    Runtime* r = Rt(id);
    if (!r || r->busy) { UiNotify(T(S_MB_DISKBUSY), 1); return; }
    r->busy = true;
    UiInvalidate();
    RunOpThread([id, apk] {
        std::wstring err;
        bool ok = AdbInstall(id, apk, err);
        Runtime* r2 = Rt(id);
        if (r2) r2->busy = false;
        UiNotify(ok ? std::wstring(T(S_MB_INSTALLED)) : (std::wstring(T(S_MB_INSTFAIL)) + L": " + err), ok ? 0 : 2);
        ILog(id, L"launcher", ok ? (L"APK installed: " + apk) : (L"APK FAILED: " + apk));
        if (ok) {
            // TZ4 2.4: apply known app profile if present
            extern bool V4MaybeApplyAppProfile(const std::wstring&, const std::wstring&);
            extern void V4PluginsDispatch(const wchar_t*, const std::wstring&);
            extern std::wstring V4ApkPackageOf(const std::wstring&);
            V4MaybeApplyAppProfile(id, V4ApkPackageOf(apk));
            // TZ6 4.2: plugin event
            V4PluginsDispatch(L"apk_installed", L"{\"instance\":\"" + id + L"\",\"apk\":\"" + apk + L"\"}");
        }
        UiInvalidate();
    });
}
void UiInvalidate() { if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE); }
void PostOpDone(const std::wstring& id, bool err) {
    if (g_wnd) PostMessageW(g_wnd, WM_APP_OPDONE, err ? 1 : 0, (LPARAM)new std::wstring(id));
}
