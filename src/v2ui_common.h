// ============================================================================
//  NovaDroid - v2ui_common.h  Widgets shared between stage-2 dialog files.
//  Requires ui.h to be included first (drawing helpers).
// ============================================================================
#pragma once
#include "ui.h"

// one-line text input helper
struct TextInput {
    std::wstring text;
    bool focus = false;
    void Draw(HDC dc, RECT r, const std::wstring& label, int id) {
        Txt(dc, r, label, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        RECT f = { r.left, r.bottom - MulDiv(30, g_dpi, 96), r.right, r.bottom };
        FillRound(dc, f, CL_BG, 8);
        FrameRound(dc, f, focus ? Accent() : CL_BORDER, 8);
        Reg(id, f, K_BTN);
        std::wstring show = text;
        if (focus && (GetTickCount() / 500) % 2 == 0) show += L"|";
        RECT tr = { f.left + MulDiv(10, g_dpi, 96), f.top, f.right - MulDiv(10, g_dpi, 96), f.bottom };
        Txt(dc, tr, show, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    bool Click(int id, int self) { if (id == self) { focus = true; return true; } focus = false; return false; }
    bool Key(MSG* m) {
        if (!focus) return false;
        if (m->message == WM_CHAR) {
            wchar_t ch = (wchar_t)m->wParam;
            if (ch == L'\b') { if (!text.empty()) text.pop_back(); }
            else if (ch >= 32 && ch != 127) text += ch;
            return true;
        }
        return false;
    }
};

// checkbox helper (draws + registers; click handler toggles the flag)
inline void CheckBoxV2(HDC dc, RECT r, bool on, const std::wstring& label, int id) {
    int sz = MulDiv(18, g_dpi, 96);
    RECT box = { r.left, r.top, r.left + sz, r.top + sz };
    FillRound(dc, box, on ? Accent() : CL_BG, 4);
    FrameRound(dc, box, on ? Accent() : CL_BORDER, 4);
    if (on) {
        HPEN p = CreatePen(PS_SOLID, 2, CL_TEXT);
        HPEN op = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, box.left + 4, box.top + sz / 2, nullptr);
        LineTo(dc, box.left + sz / 2 - 1, box.bottom - 4);
        LineTo(dc, box.right - 4, box.top + 4);
        SelectObject(dc, op); DeleteObject(p);
    }
    RECT tr = { box.right + MulDiv(10, g_dpi, 96), r.top, r.right, r.bottom };
    Txt(dc, tr, label, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT full = { r.left, r.top, r.right, r.bottom };
    Reg(id, full, K_BTN);
}
