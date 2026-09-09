// ============================================================================
//  NovaDroid - pages.cpp  APK library, Files, Keymapping, Performance,
//  Diagnostics, Logs, Settings, About pages.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v3.h"

// ---------------------------------------------------------------- helpers
std::vector<std::wstring> ListApks() {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_p.apks + L"\\*.apk").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(g_p.apks + L"\\" + fd.cFileName); }
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// generic row stepper: label left, "◀ value ▶" right
static void StepperRow(HDC dc, int y, int rowH, int x0, int x1, const std::wstring& label,
                       const std::wstring& value, int idDec, int idInc) {
    int m = MulDiv(PAD, g_dpi, 96);
    RECT lr = { x0, y, x0 + MulDiv(240, g_dpi, 96), y + rowH };
    Txt(dc, lr, label, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int bw = MulDiv(30, g_dpi, 96);
    int vw = MulDiv(170, g_dpi, 96);
    int bx = x1 - (bw * 2 + vw);
    RECT b1 = { bx, y + (rowH - MulDiv(30, g_dpi, 96)) / 2, bx + bw, y + (rowH + MulDiv(30, g_dpi, 96)) / 2 };
    Btn(dc, idDec, b1, L"◀", BSTY_SURF);
    RECT vv = { b1.right, y, b1.right + vw, y + rowH };
    FillRound(dc, vv, CL_SURF2, 8);
    Txt(dc, vv, value, Fnt(F_BODY), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT b2 = { vv.right, y + (rowH - MulDiv(30, g_dpi, 96)) / 2, vv.right + bw, y + (rowH + MulDiv(30, g_dpi, 96)) / 2 };
    Btn(dc, idInc, b2, L"▶", BSTY_SURF);
}

// ---------------------------------------------------------------- APK page
void PageApk(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_APK_TITLE), T(S_APK_SUB), rc, &y);

    int bh = MulDiv(38, g_dpi, 96);
    RECT ab = { rc.right - m - MulDiv(160, g_dpi, 96), y - bh, rc.right - m, y };
    Btn(dc, ID_APK_ADD, ab, L"＋ " + std::wstring(T(S_BTN_ADD)), BSTY_PRIMARY);
    RECT ob = { ab.left - m / 2 - MulDiv(170, g_dpi, 96), ab.top, ab.left - m / 2, ab.bottom };
    Btn(dc, ID_APK_OPEN, ob, T(S_APK_OPEN), BSTY_SURF);
    y += MulDiv(10, g_dpi, 96);

    std::vector<std::wstring> apks = ListApks();
    if (apks.empty()) {
        RECT er = { x0, y + MulDiv(24, g_dpi, 96), rc.right - m, y + MulDiv(90, g_dpi, 96) };
        Txt(dc, er, T(S_APK_EMPTY), Fnt(F_BODY), CL_SUB, DT_CENTER | DT_SINGLELINE);
        return;
    }
    int scroll = g_scroll.count((int)Pg::Apk) ? g_scroll[(int)Pg::Apk] : 0;
    SaveDC(dc);
    IntersectClipRect(dc, 0, y, rc.right, rc.bottom);
    int rowH = MulDiv(56, g_dpi, 96);
    for (size_t i = 0; i < apks.size(); ++i) {
        int top = y + (int)i * (rowH + m / 3) - scroll;
        if (top + rowH < y) continue;
        if (top > rc.bottom) break;
        RECT row = { x0, top, rc.right - m, top + rowH };
        FillRound(dc, row, CL_SURF, 10);
        FrameRound(dc, row, CL_BORDER, 10);
        std::wstring nm = BaseName(apks[i]);
        RECT nr = { row.left + m, row.top + m / 3, row.right - MulDiv(220, g_dpi, 96), row.top + rowH / 2 - m / 6 };
        Txt(dc, nr, nm, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT sr = { row.left + m, row.top + rowH / 2 - m / 6, row.right - MulDiv(220, g_dpi, 96), row.bottom - m / 3 };
        Txt(dc, sr, HumanSize(FileSizeOf(apks[i])), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        int bw = MulDiv(120, g_dpi, 96);
        int bx = row.right - m - bw;
        RECT ib = { bx, row.top + (rowH - MulDiv(32, g_dpi, 96)) / 2, bx + bw, row.top + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, ID_APK_ROW + (int)i, ib, T(S_APK_INSTALL), BSTY_PRIMARY);
    }
    RestoreDC(dc, -1);
}

// ---------------------------------------------------------------- Files page
void PageFiles(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_FILE_TITLE), T(S_FILE_SUB), rc, &y);

    InstanceCfg* c = Inst(g_selId);
    if (!c && !g_insts.empty()) c = &g_insts[0];

    // shared folder card
    int cardH = MulDiv(96, g_dpi, 96);
    RECT card = { x0, y, rc.right - m, y + cardH };
    FillRound(dc, card, CL_SURF, 14);
    FrameRound(dc, card, CL_BORDER, 14);
    RECT pr = { card.left + m, card.top + m / 2, card.right - m, card.top + m / 2 + MulDiv(20, g_dpi, 96) };
    Txt(dc, pr, T(S_FILE_PATH), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
    RECT vr = { card.left + m, card.top + m / 2 + MulDiv(22, g_dpi, 96), card.right - m - MulDiv(140, g_dpi, 96),
                card.top + m / 2 + MulDiv(46, g_dpi, 96) };
    Txt(dc, vr, c && !c->sharedFolderPath.empty() ? c->sharedFolderPath : L"-",
        Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    int bw = MulDiv(120, g_dpi, 96);
    RECT pb = { card.right - m - bw, card.top + m / 2 + MulDiv(20, g_dpi, 96), card.right - m, card.top + m / 2 + MulDiv(52, g_dpi, 96) };
    Btn(dc, ID_FILE_OPEN, pb, T(S_FILE_OPEN), BSTY_SURF);
    y += cardH + m;

    // action buttons
    int bh = MulDiv(40, g_dpi, 96);
    RECT b1 = { x0, y, x0 + MulDiv(240, g_dpi, 96), y + bh };
    Btn(dc, ID_FILE_PICK, b1, L"→ " + std::wstring(T(S_FILE_PUSH)), BSTY_PRIMARY);
    RECT b2 = { b1.right + m / 2, y, b1.right + m / 2 + MulDiv(260, g_dpi, 96), y + bh };
    Btn(dc, ID_FILE_PULL, b2, L"← " + std::wstring(T(S_FILE_PULL)), BSTY_SURF);
    y += bh + m;

    // operations log card
    RECT ops = { x0, y, rc.right - m, rc.bottom - m };
    FillRound(dc, ops, CL_SURF, 14);
    FrameRound(dc, ops, CL_BORDER, 14);
    RECT or_ = { ops.left + m, ops.top + m / 2, ops.right - m, ops.top + m / 2 + MulDiv(20, g_dpi, 96) };
    Txt(dc, or_, g_lang == 0 ? L"Журнал передач" : L"Transfer journal", Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    int oy = ops.top + m / 2 + MulDiv(28, g_dpi, 96);
    int lines = 0;
    for (int i = (int)g_fileOps.size() - 1; i >= 0 && oy < ops.bottom - m; --i, ++lines) {
        RECT lr = { ops.left + m, oy, ops.right - m, oy + MulDiv(20, g_dpi, 96) };
        Txt(dc, lr, g_fileOps[i], Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        oy += MulDiv(20, g_dpi, 96);
    }
    if (g_fileOps.empty()) {
        RECT lr = { ops.left + m, oy, ops.right - m, oy + MulDiv(20, g_dpi, 96) };
        Txt(dc, lr, T(S_FILE_EMPTY), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------- Keymapping page
void PageKeys(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_KEY_TITLE), T(S_KEY_SUB), rc, &y);
    int cardH = MulDiv(190, g_dpi, 96);
    RECT card = { x0, y, rc.right - m, y + cardH };
    FillRound(dc, card, CL_SURF, 14);
    FrameRound(dc, card, CL_BORDER, 14);
    int lines = 0;
    RECT tr = { card.left + m, card.top + m, card.right - m, card.bottom - m };
    TxtWrap(dc, tr, T(S_KEY_NOTE), Fnt(F_BODY), CL_SUB, &lines);
}

// ---------------------------------------------------------------- Performance page
struct PerfRowDef { int label; std::vector<std::wstring> opts; };
static std::vector<PerfRowDef> PerfRows() {
    return {
        { S_PERF_CPU,   { L"1", L"2", L"4", L"6", L"8" } },
        { S_PERF_RAM,   { L"1024", L"2048", L"4096", L"6144", L"8192", L"16384" } },
        { S_PERF_DISKGB,{ L"8", L"16", L"32", L"64", L"128" } },
        { S_PERF_RES,   { L"1280x720", L"1600x900", L"1920x1080", L"800x1280", L"720x1280", L"1080x1920" } },
        { S_PERF_DPI,   { L"160", L"240", L"320", L"480" } },
        { S_PERF_FPS,   { L"30", L"60", L"90", L"120" } },
        { S_PERF_GPU,   { T(S_DLG_GPU_AUTO), T(S_DLG_GPU_STD), T(S_DLG_GPU_VIRT) } },
        { S_PERF_PRIO,  { T(S_PRIO_N), T(S_PRIO_H) } },
    };
}
static int PerfIndex(const InstanceCfg& c, int row) {
    auto idxOf = [](const std::vector<std::wstring>& v, const std::wstring& s) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return (int)i;
        return 0;
    };
    auto rows = PerfRows();
    switch (row) {
        case 0: return idxOf(rows[0].opts, Fmt(L"%d", c.cpuCores));
        case 1: return idxOf(rows[1].opts, Fmt(L"%d", c.ramMb));
        case 2: return idxOf(rows[2].opts, Fmt(L"%d", c.diskGb));
        case 3: return idxOf(rows[3].opts, Fmt(L"%dx%d", c.resW, c.resH));
        case 4: return idxOf(rows[4].opts, Fmt(L"%d", c.dpi));
        case 5: return idxOf(rows[5].opts, Fmt(L"%d", c.fpsLimit));
        case 6: return c.gpuMode == L"virtio" ? 2 : c.gpuMode == L"std" ? 1 : 0;
        default: return c.priority == L"high" ? 1 : 0;
    }
}
static void PerfSet(InstanceCfg& c, int row, int v, const std::vector<std::wstring>& opts) {
    v = v < 0 ? 0 : v >= (int)opts.size() ? (int)opts.size() - 1 : v;
    switch (row) {
        case 0: c.cpuCores = _wtoi(opts[v].c_str()); break;
        case 1: c.ramMb = _wtoi(opts[v].c_str()); break;
        case 2: c.diskGb = _wtoi(opts[v].c_str()); break;
        case 3: swscanf(opts[v].c_str(), L"%dx%d", &c.resW, &c.resH); break;
        case 4: c.dpi = _wtoi(opts[v].c_str()); break;
        case 5: c.fpsLimit = _wtoi(opts[v].c_str()); break;
        case 6: c.gpuMode = v == 2 ? L"virtio" : v == 1 ? L"std" : L"auto"; break;
        default: c.priority = v == 1 ? L"high" : L"normal"; break;
    }
}

void PagePerf(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_PERF_TITLE), T(S_PERF_SUB), rc, &y);

    InstanceCfg* c = Inst(g_selId);
    if (!c) {
        RECT er = { x0, y + MulDiv(20, g_dpi, 96), rc.right - m, y + MulDiv(60, g_dpi, 96) };
        Txt(dc, er, g_lang == 0 ? L"Инстансов нет." : L"No instances.", Fnt(F_BODY), CL_SUB, DT_LEFT);
        return;
    }

    // profiles
    RECT pr = { x0, y, rc.right - m, y + MulDiv(24, g_dpi, 96) };
    Txt(dc, pr, T(S_PERF_PROFILE), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    y += MulDiv(28, g_dpi, 96);
    const wchar_t* profNames[4] = { T(S_PERF_ECO), T(S_PERF_BAL), T(S_PERF_HIGH), T(S_PERF_CUSTOM) };
    int pw = MulDiv(170, g_dpi, 96);
    for (int i = 0; i < 4; ++i) {
        RECT b = { x0 + i * (pw + m / 2), y, x0 + i * (pw + m / 2) + pw, y + MulDiv(36, g_dpi, 96) };
        // profile selection heuristic: check known combos
        int cur = 3;
        if (c->cpuCores == 2 && c->ramMb == 2048) cur = 0;
        else if (c->cpuCores == 4 && c->ramMb == 4096) cur = 1;
        else if (c->cpuCores >= 6 && c->ramMb >= 8192) cur = 2;
        Btn(dc, ID_PERF_PROFILE + i, b, profNames[i], i == cur ? BSTY_PRIMARY : BSTY_SURF);
    }
    y += MulDiv(44, g_dpi, 96);

    // rows
    auto rows = PerfRows();
    int rowH = MulDiv(44, g_dpi, 96);
    int x1 = rc.right - m - MulDiv(260, g_dpi, 96);
    for (int i = 0; i < (int)rows.size(); ++i) {
        int sel = PerfIndex(*c, i);
        StepperRow(dc, y, rowH, x0, x1 + MulDiv(260, g_dpi, 96), T(rows[i].label),
                   rows[i].opts[sel], ID_PERF_DEC + i * 2, ID_PERF_DEC + i * 2 + 1);
        y += rowH;
    }
    y += m / 2;
    RECT note = { x0, y, rc.right - m, y + MulDiv(20, g_dpi, 96) };
    Txt(dc, note, T(S_PERF_NOTE), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
    y += MulDiv(28, g_dpi, 96);
    RECT ab = { x0, y, x0 + MulDiv(220, g_dpi, 96), y + MulDiv(40, g_dpi, 96) };
    Btn(dc, ID_PERF_APPLY, ab, T(S_PERF_APPLY), BSTY_PRIMARY);
}

// ---------------------------------------------------------------- Diagnostics page
void PageDiag(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_DIAG_TITLE), T(S_DIAG_SUB), rc, &y);

    int bh = MulDiv(38, g_dpi, 96);
    RECT rb = { rc.right - m - MulDiv(190, g_dpi, 96), y - bh - MulDiv(6, g_dpi, 96), rc.right - m, y - MulDiv(6, g_dpi, 96) };
    Btn(dc, ID_DIAG_RUN, rb, g_diagBusy ? (g_lang == 0 ? L"Проверка…" : L"Checking…") : T(S_DIAG_RUN),
        BSTY_PRIMARY, !g_diagBusy);
    RECT cb = { rb.left - m / 2 - MulDiv(200, g_dpi, 96), rb.top, rb.left - m / 2, rb.bottom };
    Btn(dc, ID_DIAG_COPY, cb, T(S_DIAG_COPY), BSTY_SURF);
    y += MulDiv(10, g_dpi, 96);
    PageDiagGpuBlock(dc, rc, &y);          // stage 3: GPU status card (TZ3 14.1)

    int scroll = g_scroll.count((int)Pg::Diag) ? g_scroll[(int)Pg::Diag] : 0;
    SaveDC(dc);
    IntersectClipRect(dc, 0, y, rc.right, rc.bottom);
    for (size_t i = 0; i < g_diag.size(); ++i) {
        int lines = 0;
        RECT calc = { x0 + MulDiv(40, g_dpi, 96), 0, rc.right - m, 10000 };
        TxtWrap(dc, calc, g_diag[i].msg, Fnt(F_SMALL), CL_SUB, &lines);
        int rowH = MulDiv(30, g_dpi, 96) + lines * MulDiv(16, g_dpi, 96);
        int top = y + (int)i * (rowH + m / 2) - scroll;
        if (top + rowH < y) continue;
        if (top > rc.bottom) break;
        RECT card = { x0, top, rc.right - m, top + rowH };
        FillRound(dc, card, CL_SURF, 10);
        FrameRound(dc, card, CL_BORDER, 10);
        COLORREF col = g_diag[i].level == 0 ? CL_OK : g_diag[i].level == 1 ? CL_WARN :
                       g_diag[i].level == 2 ? CL_ERR : Accent();
        Dot(dc, x0 + m + MulDiv(8, g_dpi, 96), top + MulDiv(18, g_dpi, 96), MulDiv(5, g_dpi, 96), col);
        RECT nr = { x0 + m + MulDiv(26, g_dpi, 96), top + m / 3, rc.right - m - m, top + m / 3 + MulDiv(22, g_dpi, 96) };
        Txt(dc, nr, g_diag[i].name, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT mr = { x0 + MulDiv(40, g_dpi, 96), top + MulDiv(28, g_dpi, 96), rc.right - m - m, top + rowH - m / 4 };
        Txt(dc, mr, g_diag[i].msg, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    }
    RestoreDC(dc, -1);
    if (g_diag.empty() && !g_diagBusy) {
        RECT er = { x0, y + MulDiv(20, g_dpi, 96), rc.right - m, y + MulDiv(50, g_dpi, 96) };
        Txt(dc, er, g_lang == 0 ? L"Нажмите «Запустить проверку»." : L"Press 'Run check'.",
            Fnt(F_BODY), CL_SUB, DT_LEFT);
    }
}

// ---------------------------------------------------------------- Logs page
void ReloadLogView() {
    g_logLines.clear();
    g_logScroll = 0;
    auto load = [&](const std::wstring& p) {
        std::wstring t;
        ReadText(p, t);
        for (auto& ln : SplitW(t, L'\n')) if (!TrimW(ln).empty()) g_logLines.push_back(ln);
    };
    if (g_logTab == 0) {
        // launcher logs (today + yesterday concat)
        WIN32_FIND_DATAW fd;
        std::vector<std::wstring> files;
        HANDLE h = FindFirstFileW((g_p.logs + L"\\launcher-*.log").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { files.push_back(g_p.logs + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(files.begin(), files.end());
        if (files.size() > 2) files.erase(files.begin(), files.end() - 2);
        for (auto& f : files) load(f);
    } else {
        InstanceCfg* c = Inst(g_selId);
        if (c) {
            const wchar_t* kinds[3] = { L"qemu", L"adb", L"serial" };
            std::wstring dir = c->Dir() + L"\\logs";
            if (g_logTab == 3) { load(dir + L"\\serial.log"); return; }
            // pick latest file of kind
            WIN32_FIND_DATAW fd;
            std::wstring pat = dir + L"\\" + kinds[g_logTab - 1] + L"*.log";
            std::wstring best; FILETIME bt = {};
            HANDLE h = FindFirstFileW(pat.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (CompareFileTime(&fd.ftLastWriteTime, &bt) >= 0) {
                        bt = fd.ftLastWriteTime;
                        best = dir + L"\\" + fd.cFileName;
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (!best.empty()) load(best);
        }
    }
    if (g_logLines.size() > 2000) g_logLines.erase(g_logLines.begin(), g_logLines.end() - 2000);
}

void PageLogs(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_LOGS_TITLE), T(S_LOGS_SUB), rc, &y);

    // tabs
    const wchar_t* tabs[4] = { T(S_LOG_Launcher), T(S_LOG_QEMU), T(S_LOG_ADB), T(S_LOG_SERIAL) };
    int tw = MulDiv(120, g_dpi, 96);
    for (int i = 0; i < 4; ++i) {
        RECT b = { x0 + i * (tw + m / 3), y, x0 + i * (tw + m / 3) + tw, y + MulDiv(32, g_dpi, 96) };
        Reg(ID_LOG_TAB + i, b, K_TAB);
        bool sel = (g_logTab == i);
        FillRound(dc, b, sel ? Mix(Accent(), CL_SURF, 70) : CL_SURF, 10);
        if (sel) FrameRound(dc, b, Accent(), 10);
        Txt(dc, b, tabs[i], Fnt(F_SMALL), sel ? CL_TEXT : CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // action buttons (right)
    int bh = MulDiv(32, g_dpi, 96);
    int bw = MulDiv(160, g_dpi, 96);
    RECT b4 = { rc.right - m - bw, y, rc.right - m, y + bh };
    Btn(dc, ID_LOG_REFRESH, b4, T(S_LOG_REFRESH), BSTY_SURF);
    RECT b3 = { b4.left - m / 2 - bw, y, b4.left - m / 2, y + bh };
    Btn(dc, ID_LOG_FOLDER, b3, T(S_LOG_FOLDER), BSTY_SURF);
    RECT b2 = { b3.left - m / 2 - MulDiv(220, g_dpi, 96), y, b3.left - m / 2, y + bh };
    Btn(dc, ID_LOG_CLEAN, b2, T(S_LOG_CLEAN), BSTY_GHOST);
    y += bh + m / 2 + MulDiv(6, g_dpi, 96);

    // log view
    RECT view = { x0, y, rc.right - m, rc.bottom - m };
    FillRound(dc, view, CL_SURF, 12);
    FrameRound(dc, view, CL_BORDER, 12);
    SaveDC(dc);
    IntersectClipRect(dc, view.left + 2, view.top + 2, view.right - 2, view.bottom - 2);
    int lh = MulDiv(17, g_dpi, 96);
    int maxScroll = (int)g_logLines.size() * lh - (view.bottom - view.top - MulDiv(16, g_dpi, 96));
    if (maxScroll < 0) maxScroll = 0;
    if (g_logScroll > maxScroll) g_logScroll = maxScroll;
    int yy = view.top + MulDiv(8, g_dpi, 96) - g_logScroll;
    for (auto& ln : g_logLines) {
        if (yy + lh > view.top && yy < view.bottom) {
            RECT lr = { view.left + MulDiv(12, g_dpi, 96), yy, view.right - MulDiv(12, g_dpi, 96), yy + lh };
            Txt(dc, lr, ln, Fnt(F_MONO), CL_SUB, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        yy += lh;
        if (yy > view.bottom) break;
    }
    RestoreDC(dc, -1);
    if (g_logLines.empty()) {
        RECT er = view;
        Txt(dc, er, T(S_LOG_EMPTY), Fnt(F_BODY), CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ---------------------------------------------------------------- Settings page
void PageSettings(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_SET_TITLE), T(S_SET_SUB), rc, &y);

    int rowH = MulDiv(52, g_dpi, 96);
    auto rowCard = [&](int h) {
        RECT r = { x0, y, rc.right - m, y + h };
        FillRound(dc, r, CL_SURF, 12);
        FrameRound(dc, r, CL_BORDER, 12);
        return r;
    };
    // language
    {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.left + MulDiv(360, g_dpi, 96), y + rowH };
        Txt(dc, lr, T(S_SET_LANG), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bw = MulDiv(34, g_dpi, 96);
        int vw = MulDiv(130, g_dpi, 96);
        int bx = r.right - m - (bw * 2 + vw);
        RECT b1 = { bx, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, bx + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, ID_SET_LANGDEC, b1, L"◀", BSTY_SURF);
        RECT vv = { b1.right, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, b1.right + vw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        FillRound(dc, vv, CL_SURF2, 8);
        Txt(dc, vv, g_lang == 0 ? L"Русский" : L"English", Fnt(F_BODY), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT b2 = { vv.right, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, vv.right + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, ID_SET_LANGINC, b2, L"▶", BSTY_SURF);
        y += rowH + m / 2;
    }
    // accent colors
    {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.left + MulDiv(360, g_dpi, 96), y + rowH };
        Txt(dc, lr, T(S_SET_ACCENT), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t* acc[3] = { L"#6C63FF", L"#2FB7C6", L"#9B59B6" };
        int sw = MulDiv(30, g_dpi, 96);
        int sx = r.right - m - sw * 3 - m / 2;
        for (int i = 0; i < 3; ++i) {
            RECT s = { sx + i * (sw + m / 2), y + (rowH - sw) / 2, sx + i * (sw + m / 2) + sw, y + (rowH + sw) / 2 };
            FillRound(dc, s, CREF(wcstoul(acc[i] + 1, nullptr, 16)), 6);
            if (g_set.accentHex == acc[i]) FrameRound(dc, s, CL_TEXT, 6, 2);
            Reg(ID_SET_ACC0 + i, s, K_SWATCH);
        }
        y += rowH + m / 2;
    }
    // paths rows
    auto pathRow = [&](const wchar_t* label, const std::wstring& val, int idPick, int idReset) {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.left + MulDiv(360, g_dpi, 96), y + rowH };
        Txt(dc, lr, label, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT vr = { r.left + MulDiv(370, g_dpi, 96), y, r.right - m - MulDiv(180, g_dpi, 96), y + rowH };
        Txt(dc, vr, val.empty() ? (g_lang == 0 ? L"(авто)" : L"(auto)") : val, Fnt(F_SMALL), CL_SUB,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
        int bw = MulDiv(90, g_dpi, 96);
        RECT pb = { r.right - m - bw, y + (rowH - MulDiv(30, g_dpi, 96)) / 2, r.right - m, y + (rowH + MulDiv(30, g_dpi, 96)) / 2 };
        Btn(dc, idPick, pb, T(S_SET_PICK), BSTY_PRIMARY);
        if (idReset) {
            RECT rb2 = { pb.left - m / 2 - MulDiv(80, g_dpi, 96), pb.top, pb.left - m / 2, pb.bottom };
            Btn(dc, idReset, rb2, T(S_SET_RESET), BSTY_GHOST);
        }
        y += rowH + m / 2;
    };
    pathRow(T(S_SET_QEMU), g_set.qemuPath, ID_SET_QEMU, ID_SET_QEMURST);
    pathRow(T(S_SET_ADB), g_set.adbPath, ID_SET_ADB, 0);
    pathRow(T(S_SET_IMAGE), g_set.defaultImagePath, ID_SET_IMAGE, 0);
    pathRow(T(S_SET_DATA), g_set.dataRoot, ID_SET_DATA, 0);

    // base port
    {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.left + MulDiv(360, g_dpi, 96), y + rowH };
        Txt(dc, lr, T(S_SET_PORT), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bw = MulDiv(34, g_dpi, 96);
        int vw = MulDiv(110, g_dpi, 96);
        int bx = r.right - m - (bw * 2 + vw);
        RECT b1 = { bx, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, bx + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, ID_SET_PORTDEC, b1, L"◀", BSTY_SURF);
        RECT vv = { b1.right, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, b1.right + vw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        FillRound(dc, vv, CL_SURF2, 8);
        Txt(dc, vv, Fmt(L"%d", g_set.baseAdbPort), Fnt(F_BODY), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT b2 = { vv.right, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, vv.right + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, ID_SET_PORTINC, b2, L"▶", BSTY_SURF);
        y += rowH + m / 2;
    }
    // checkboxes
    auto checkRow = [&](const wchar_t* label, bool val, int id) {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.right - m, y + rowH };
        std::wstring s = (val ? L"[x]  " : L"[  ]  ") + std::wstring(label);
        Txt(dc, lr, s, Fnt(F_BODY), val ? CL_TEXT : CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Reg(id, r, K_BTN);
        y += rowH + m / 2;
    };
    checkRow(T(S_SET_CHECK), g_set.checkOnStart, ID_SET_CHECK);
    checkRow(T(S_SET_AUTORUN), g_set.autostartLast, ID_SET_AUTORUN);
    // ---- stage 2: concurrent instance limit (TZ 5.3)
    {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.left + MulDiv(360, g_dpi, 96), y + rowH };
        Txt(dc, lr, T(S_SET_MAXRUN), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bw = MulDiv(34, g_dpi, 96);
        int vw = MulDiv(110, g_dpi, 96);
        int bx = r.right - m - (bw * 2 + vw);
        RECT b1 = { bx, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, bx + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, 3883, b1, L"\u25c0", BSTY_SURF);
        RECT vv = { b1.right, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, b1.right + vw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        FillRound(dc, vv, CL_SURF2, 8);
        Txt(dc, vv, Fmt(L"%d", g_set.maxRunning), Fnt(F_BODY), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT b2 = { vv.right, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, vv.right + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, 3884, b2, L"\u25b6", BSTY_SURF);
        y += rowH + m / 2;
    }
    // ---- stage 2: update channel (TZ 14.3)
    {
        RECT r = rowCard(rowH);
        RECT lr = { r.left + m, y, r.left + MulDiv(360, g_dpi, 96), y + rowH };
        Txt(dc, lr, T(S_UPD_CHANNEL), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t* chans[3] = { T(S_UPD_STABLE), T(S_UPD_BETA), T(S_UPD_DEV) };
        int cw = MulDiv(110, g_dpi, 96);
        int cx = r.right - m - cw * 3 - m / 2;
        for (int i = 0; i < 3; ++i) {
            RECT c = { cx + i * (cw + m / 4), y + (rowH - MulDiv(30, g_dpi, 96)) / 2,
                       cx + i * (cw + m / 4) + cw, y + (rowH + MulDiv(30, g_dpi, 96)) / 2 };
            bool on = (i == 0 && g_set.updateChannel == L"stable") ||
                      (i == 1 && g_set.updateChannel == L"beta") ||
                      (i == 2 && g_set.updateChannel == L"development");
            FillRound(dc, c, on ? Mix(Accent(), CL_SURF, 55) : CL_SURF2, 8);
            FrameRound(dc, c, on ? Accent() : CL_BORDER, 8);
            Reg(3885 + i, c, K_BTN);
            Txt(dc, c, chans[i], Fnt(F_SMALL), on ? CL_TEXT : CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        y += rowH + m / 2;
    }
    // ---- stage 2: updates + diag export buttons
    {
        RECT r = rowCard(rowH);
        int bw = MulDiv(190, g_dpi, 96);
        int bx = r.left + m;
        RECT b1 = { bx, y + (rowH - MulDiv(32, g_dpi, 96)) / 2, bx + bw, y + (rowH + MulDiv(32, g_dpi, 96)) / 2 };
        Btn(dc, 3888, b1, T(S_UPD_CHECK), BSTY_PRIMARY);
        RECT b2 = { b1.right + m / 2, b1.top, b1.right + m / 2 + MulDiv(170, g_dpi, 96), b1.bottom };
        Btn(dc, 3889, b2, T(S_DIAG_EXPORT), BSTY_SURF);
        UpdateInfo u = UpdateState();
        if (u.checked) {
            std::wstring st = u.ok ? ((g_lang ? L"latest: " : L"актуальная: ") + u.latestVersion)
                                    : (g_lang ? L"offline build" : L"офлайн-сборка");
            RECT ur = { b2.right + m, y, r.right - m, y + rowH };
            Txt(dc, ur, st, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        y += rowH + m / 2;
    }
    PageSettingsPkgSection(dc, rc, &y);    // stage 3: full-package block
    RECT note = { x0, y, rc.right - m, y + MulDiv(20, g_dpi, 96) };
    Txt(dc, note, T(S_SET_RESTART), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
}

// ---------------------------------------------------------------- About page
void PageAbout(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_AB_TITLE), T(S_AB_VER), rc, &y);

    DrawAndroidHead(dc, x0 + MulDiv(40, g_dpi, 96), y + MulDiv(40, g_dpi, 96), MulDiv(64, g_dpi, 96), Accent());
    y += MulDiv(100, g_dpi, 96);

    int lines = 0;
    auto para = [&](const wchar_t* s) {
        RECT calc = { x0, 0, rc.right - m, 10000 };
        TxtWrap(dc, calc, s, Fnt(F_BODY), CL_TEXT, &lines);
        int h = lines * MulDiv(17, g_dpi, 96) + MulDiv(10, g_dpi, 96);
        RECT r = { x0, y, rc.right - m, y + h };
        Txt(dc, r, s, Fnt(F_BODY), CL_SUB, DT_LEFT | DT_WORDBREAK);
        y += h + m / 2;
    };
    para(T(S_AB_ABOUT));
    para(T(S_AB_COMP));
    para(T(S_AB_SAFE));

    RECT lr = { x0, y, rc.right - m, y + MulDiv(24, g_dpi, 96) };
    Txt(dc, lr, T(S_AB_LINKS), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    y += MulDiv(30, g_dpi, 96);
    const wchar_t* links[3] = { L"https://www.qemu.org", L"https://www.android-x86.org",
                                L"https://developer.android.com/tools/releases/platform-tools" };
    for (int i = 0; i < 3; ++i) {
        RECT r = { x0, y, x0 + MulDiv(560, g_dpi, 96), y + MulDiv(22, g_dpi, 96) };
        Txt(dc, r, links[i], Fnt(F_SMALL), Accent(), DT_LEFT | DT_SINGLELINE);
        Reg(ID_AB_LINK0 + i, r, K_LINK);
        y += MulDiv(26, g_dpi, 96);
    }
}
