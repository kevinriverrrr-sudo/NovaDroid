// ============================================================================
//  NovaDroid - v2ui_wizard.cpp  5-step instance creation wizard (TZ 6):
//  1 name/template  2 performance  3 screen  4 integration  5 review.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v2ui_common.h"

// wizard ids (base ID_WIZ0 = 3700)
enum {
    W_NEXT = 3800, W_PREV, W_CLOSE,
    W_NAME = 3810,
    W_TPL0 = 3820,                       // +0..7
    W_CPU0 = 3830, W_RAM0, W_DISK0, W_FPS0, W_GPU0, W_PRIO0,   // steppers: +0 dec, +1 inc
    W_RES0 = 3840,                       // +0..4 preset chips
    W_DPI0 = 3846,                       // +0 dec, +1 inc
    W_ORIENT = 3849,
    W_SHARED = 3853, W_SHORTCUT, W_OPEN, W_SNAPINIT,
    W_IMG0 = 3858,                       // +i image choice
};

struct WizardState {
    int step = 1;
    InstanceCfg c;
    TextInput name;
    int tpl = 1;
    int cpuIdx = 2, ramIdx = 2, diskIdx = 2, fpsIdx = 1, gpuIdx = 0, prioIdx = 0, dpiIdx = 1;
    int resIdx = 0, imgIdx = 0;
    bool landscape = true;
    bool shared = true, shortcut = false, openAfter = true, snapInit = true;
};
static bool g_wizOpenAfter = true;

static const wchar_t* CpuOpts[] = { L"1", L"2", L"4", L"6", L"8" };
static const wchar_t* RamOpts[] = { L"1024", L"2048", L"4096", L"6144", L"8192", L"16384" };
static const wchar_t* DiskOpts[] = { L"8", L"16", L"32", L"64", L"128" };
static const wchar_t* FpsOpts[] = { L"30", L"60", L"90", L"120" };
static const int DpiOpts[] = { 160, 240, 320, 480 };

struct ResPreset { int w, h, dpi; const wchar_t* label; };
static const ResPreset ResPresets[] = {
    { 1280, 720,  240, L"1280x720 HD" },
    { 1920, 1080, 320, L"1920x1080 FHD" },
    { 1920, 1200, 240, L"1920x1200 Tablet" },
    { 720,  1280, 240, L"720x1280 Portrait" },
    { 1600, 900,  240, L"1600x900" },
};

static void WizApplyTemplate(WizardState& s) {
    switch (s.tpl) {
        case 0: s.c.cpuCores = 2; s.c.ramMb = 2048; s.c.diskGb = 16; s.c.fpsLimit = 30; s.c.resW = 1280; s.c.resH = 720; s.c.dpi = 240; break;
        case 1: s.c.cpuCores = 4; s.c.ramMb = 4096; s.c.diskGb = 32; s.c.fpsLimit = 60; s.c.resW = 1280; s.c.resH = 720; s.c.dpi = 240; break;
        case 2: s.c.cpuCores = 6; s.c.ramMb = 8192; s.c.diskGb = 64; s.c.fpsLimit = 120; s.c.resW = 1920; s.c.resH = 1080; s.c.dpi = 320; break;
        case 3: s.c.cpuCores = 2; s.c.ramMb = 2048; s.c.diskGb = 16; s.c.fpsLimit = 30; s.c.resW = 1280; s.c.resH = 720; s.c.dpi = 240; break;
        case 4: s.c.cpuCores = 2; s.c.ramMb = 2048; s.c.diskGb = 32; s.c.fpsLimit = 60; s.c.resW = 1280; s.c.resH = 720; s.c.dpi = 240; break;
        case 5: s.c.cpuCores = 4; s.c.ramMb = 4096; s.c.diskGb = 32; s.c.fpsLimit = 60; s.c.resW = 720; s.c.resH = 1280; s.c.dpi = 240; s.landscape = false; break;
        case 6: s.c.cpuCores = 4; s.c.ramMb = 4096; s.c.diskGb = 32; s.c.fpsLimit = 60; s.c.resW = 1920; s.c.resH = 1200; s.c.dpi = 240; s.landscape = true; break;
    }
    s.c.templateName = s.tpl == 0 ? L"clean" : s.tpl == 1 ? L"balanced" : s.tpl == 2 ? L"performance" :
                       s.tpl == 3 ? L"eco" : s.tpl == 4 ? L"apk-test" : s.tpl == 5 ? L"portrait" :
                       s.tpl == 6 ? L"tablet" : L"custom";
    if (s.tpl == 2) s.c.priority = L"high"; else s.c.priority = L"normal";
    // sync steppers
    auto idxOf = [](const wchar_t** arr, int n, int v) {
        for (int i = 0; i < n; ++i) if (_wtoi(arr[i]) == v) return i;
        return n > 2 ? 2 : 0;
    };
    s.cpuIdx = idxOf(CpuOpts, 5, s.c.cpuCores);
    s.ramIdx = idxOf(RamOpts, 6, s.c.ramMb);
    s.diskIdx = idxOf(DiskOpts, 5, s.c.diskGb);
    s.fpsIdx = idxOf(FpsOpts, 4, s.c.fpsLimit);
}

bool WizardRun(InstanceCfg& out) {
    WizardState s;
    s.c = out;
    s.c.name = g_lang ? L"New instance" : L"Новый инстанс";
    if (!g_set.defaultImagePath.empty()) s.c.imagePath = g_set.defaultImagePath;
    s.name.text = s.c.name;
    // pick first local image by default
    auto locals = FindLocalImages();
    if (s.c.imagePath.empty() && !locals.empty()) { s.c.imagePath = locals[0]; s.imgIdx = 0; }

    auto stepper = [&](HDC dc, int id, int y, int x0, int x1, const std::wstring& label,
                       const std::wstring& val) {
        int bh = MulDiv(30, g_dpi, 96);
        Txt(dc, { x0, y, x1 - MulDiv(170, g_dpi, 96), y + bh }, label, Fnt(F_BODY), CL_TEXT,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bx = x1 - MulDiv(150, g_dpi, 96);
        RECT dec = { bx, y, bx + MulDiv(36, g_dpi, 96), y + bh };
        Btn(dc, id, dec, L"−", BSTY_SURF);
        RECT mid = { dec.right + 4, y, dec.right + MulDiv(70, g_dpi, 96), y + bh };
        FillRound(dc, mid, CL_BG, 8);
        Txt(dc, mid, val, Fnt(F_BODY), CL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT inc = { mid.right + 4, y, mid.right + MulDiv(40, g_dpi, 96), y + bh };
        Btn(dc, id + 1, inc, L"＋", BSTY_SURF);
    };

    auto chipRow = [&](HDC dc, int y, int x0, int x1, int baseId, const std::vector<std::wstring>& labels,
                       int sel) {
        int cx = x0;
        int ch = MulDiv(30, g_dpi, 96);
        for (int i = 0; i < (int)labels.size(); ++i) {
            int cw = MulDiv(30 + 11 * (int)labels[i].size(), g_dpi, 96);
            RECT r = { cx, y, cx + cw, y + ch };
            bool on = (i == sel);
            FillRound(dc, r, on ? Mix(Accent(), CL_SURF, 55) : CL_SURF2, ch / 2);
            FrameRound(dc, r, on ? Accent() : CL_BORDER, ch / 2);
            Reg(baseId + i, r, K_BTN);
            Txt(dc, r, labels[i], Fnt(F_SMALL), on ? CL_TEXT : CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            cx += cw + MulDiv(8, g_dpi, 96);
        }
    };

    bool created = false;
    V2ModalRun(g_wnd, MulDiv(760, g_dpi, 96), MulDiv(620, g_dpi, 96),
        [&](HDC dc, RECT rc) {
            int m = MulDiv(20, g_dpi, 96);
            RECT t = { m, m, rc.right - m, m + MulDiv(28, g_dpi, 96) };
            Txt(dc, t, T(S_WIZ_TITLE), Fnt(F_TITLE), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            RECT st = { m, m + MulDiv(28, g_dpi, 96), rc.right - m, m + MulDiv(46, g_dpi, 96) };
            Txt(dc, st, Fmt(L"%s %d / 5 - %s", T(S_WIZ_STEP), s.step,
                            s.step == 1 ? T(S_WIZ_NAME) : s.step == 2 ? T(S_WIZ_PERF) :
                            s.step == 3 ? T(S_WIZ_SCREEN) : s.step == 4 ? T(S_WIZ_INTEG) : T(S_WIZ_REVIEW)),
                Fnt(F_SMALL), Accent(), DT_LEFT | DT_SINGLELINE);
            // progress bar
            RECT pb = { m, m + MulDiv(50, g_dpi, 96), rc.right - m, m + MulDiv(54, g_dpi, 96) };
            FillRound(dc, pb, CL_SURF2, 2);
            RECT pbf = { pb.left, pb.top, pb.left + (int)((pb.right - pb.left) * s.step / 5.0), pb.bottom };
            FillRound(dc, pbf, Accent(), 2);
            int y = m + MulDiv(66, g_dpi, 96);
            int x0 = m, x1 = rc.right - m;

            if (s.step == 1) {
                RECT r1 = { x0, y, x1, y + MulDiv(48, g_dpi, 96) };
                s.name.Draw(dc, r1, T(S_WIZ_NAME), W_NAME);
                y += MulDiv(58, g_dpi, 96);
                Txt(dc, { x0, y, x1, y + MulDiv(20, g_dpi, 96) }, T(S_WIZ_TEMPLATE), Fnt(F_H2), CL_TEXT, DT_LEFT);
                y += MulDiv(26, g_dpi, 96);
                const wchar_t* tpls[] = { T(S_WIZ_T_CLEAN), T(S_WIZ_T_BAL), T(S_WIZ_T_PERF), T(S_WIZ_T_ECO),
                                          T(S_WIZ_T_TEST), T(S_WIZ_T_PORT), T(S_WIZ_T_TAB), T(S_WIZ_T_CUSTOM) };
                int cw = (x1 - x0 - 3 * m / 2) / 4, chh = MulDiv(64, g_dpi, 96);
                for (int i = 0; i < 8; ++i) {
                    int col = i % 4, row = i / 4;
                    RECT r = { x0 + col * (cw + m / 2), y + row * (chh + m / 2),
                               x0 + col * (cw + m / 2) + cw, y + row * (chh + m / 2) + chh };
                    bool on = (s.tpl == i);
                    FillRound(dc, r, on ? Mix(Accent(), CL_SURF, 60) : CL_SURF2, 10);
                    FrameRound(dc, r, on ? Accent() : CL_BORDER, 10);
                    Reg(W_TPL0 + i, r, K_BTN);
                    Txt(dc, r, tpls[i], Fnt(F_SMALL), on ? CL_TEXT : CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                y += 2 * (chh + m / 2) + m / 2;
                // image choice
                Txt(dc, { x0, y, x1, y + MulDiv(20, g_dpi, 96) }, T(S_IMG_LOCAL), Fnt(F_H2), CL_TEXT, DT_LEFT);
                y += MulDiv(24, g_dpi, 96);
                if (locals.empty()) {
                    Txt(dc, { x0, y, x1, y + MulDiv(36, g_dpi, 96) },
                        g_lang ? L"No images found. Download one from the Images page first."
                               : L"Образы не найдены. Сначала скачайте образ на странице «Образы».",
                        Fnt(F_SMALL), CL_WARN, DT_LEFT | DT_WORDBREAK);
                }
                for (size_t i = 0; i < locals.size() && i < 4; ++i) {
                    RECT r = { x0, y, x1, y + MulDiv(28, g_dpi, 96) };
                    bool on = (s.imgIdx == (int)i);
                    FillRound(dc, r, on ? Mix(Accent(), CL_SURF, 70) : CL_SURF, 8);
                    FrameRound(dc, r, on ? Accent() : CL_BORDER, 8);
                    Reg((int)(W_IMG0 + i), r, K_BTN);
                    Txt(dc, { r.left + 10, y, r.right - 10, y + MulDiv(28, g_dpi, 96) },
                        BaseName(locals[i]) + L"  ·  " + HumanSize(FileSizeOf(locals[i])),
                        Fnt(F_SMALL), on ? CL_TEXT : CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    y += MulDiv(32, g_dpi, 96);
                }
            } else if (s.step == 2) {
                stepper(dc, W_CPU0, y, x0, x1, T(S_PERF_CPU), CpuOpts[s.cpuIdx]); y += MulDiv(40, g_dpi, 96);
                stepper(dc, W_RAM0, y, x0, x1, T(S_PERF_RAM), RamOpts[s.ramIdx]); y += MulDiv(40, g_dpi, 96);
                stepper(dc, W_DISK0, y, x0, x1, T(S_PERF_DISKGB), DiskOpts[s.diskIdx]); y += MulDiv(40, g_dpi, 96);
                stepper(dc, W_FPS0, y, x0, x1, T(S_PERF_FPS), FpsOpts[s.fpsIdx]); y += MulDiv(40, g_dpi, 96);
                stepper(dc, W_GPU0, y, x0, x1, T(S_PERF_GPU),
                        s.gpuIdx == 0 ? T(S_DLG_GPU_AUTO) : s.gpuIdx == 1 ? T(S_DLG_GPU_STD) : T(S_DLG_GPU_VIRT));
                y += MulDiv(40, g_dpi, 96);
                stepper(dc, W_PRIO0, y, x0, x1, T(S_PERF_PRIO), s.prioIdx ? T(S_PRIO_H) : T(S_PRIO_N));
                y += MulDiv(48, g_dpi, 96);
                MEMORYSTATUSEX ms = { sizeof(ms) };
                GlobalMemoryStatusEx(&ms);
                Txt(dc, { x0, y, x1, y + MulDiv(40, g_dpi, 96) },
                    Fmt(g_lang ? L"PC RAM: %llu MB total · %llu MB available"
                               : L"RAM ПК: %llu MB всего · %llu MB доступно",
                        (unsigned long long)(ms.ullTotalPhys / (1024 * 1024)),
                        (unsigned long long)(ms.ullAvailPhys / (1024 * 1024))),
                    Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
            } else if (s.step == 3) {
                Txt(dc, { x0, y, x1, y + MulDiv(20, g_dpi, 96) }, T(S_PERF_RES), Fnt(F_H2), CL_TEXT, DT_LEFT);
                y += MulDiv(26, g_dpi, 96);
                std::vector<std::wstring> rl;
                for (auto& p : ResPresets) rl.push_back(p.label);
                chipRow(dc, y, x0, x1, W_RES0, rl, s.resIdx);
                y += MulDiv(44, g_dpi, 96);
                Txt(dc, { x0, y, x1, y + MulDiv(20, g_dpi, 96) }, T(S_PERF_DPI), Fnt(F_H2), CL_TEXT, DT_LEFT);
                y += MulDiv(26, g_dpi, 96);
                std::vector<std::wstring> dl;
                for (int d : DpiOpts) dl.push_back(std::to_wstring(d));
                chipRow(dc, y, x0, x1, W_DPI0, dl, s.dpiIdx);
                y += MulDiv(44, g_dpi, 96);
                Txt(dc, { x0, y, x1, y + MulDiv(20, g_dpi, 96) }, T(S_WIZ_ORIENT), Fnt(F_H2), CL_TEXT, DT_LEFT);
                y += MulDiv(26, g_dpi, 96);
                chipRow(dc, y, x0, x1, W_ORIENT, { T(S_WIZ_LAND), T(S_WIZ_PORT) }, s.landscape ? 0 : 1);
                y += MulDiv(48, g_dpi, 96);
                RECT pv = { x0, y, x0 + MulDiv(240, g_dpi, 96), y + MulDiv(140, g_dpi, 96) };
                float ar = (float)s.c.resW / (float)s.c.resH;
                float boxAr = 240.0f / 140.0f;
                RECT scr = pv;
                if (ar > boxAr) { int hh = (int)(240 / ar); scr = { pv.left, pv.top + (140 - hh) / 2, pv.right, pv.top + (140 - hh) / 2 + hh }; }
                else { int ww = (int)(140 * ar); scr = { pv.left + (240 - ww) / 2, pv.top, pv.left + (240 - ww) / 2 + ww, pv.bottom }; }
                FillRound(dc, scr, CL_BG, 6);
                FrameRound(dc, scr, Accent(), 6);
                Txt(dc, scr, Fmt(L"%dx%d", ResPresets[s.resIdx].w, ResPresets[s.resIdx].h),
                    Fnt(F_SMALL), CL_SUB, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else if (s.step == 4) {
                // ADB locked on localhost
                int sz = MulDiv(18, g_dpi, 96);
                RECT box = { x0, y, x0 + sz, y + sz };
                FillRound(dc, box, CL_OK, 4);
                HPEN p = CreatePen(PS_SOLID, 2, CL_TEXT);
                HPEN op = (HPEN)SelectObject(dc, p);
                MoveToEx(dc, box.left + 4, box.top + sz / 2, nullptr);
                LineTo(dc, box.left + sz / 2 - 1, box.bottom - 4);
                LineTo(dc, box.right - 4, box.top + 4);
                SelectObject(dc, op); DeleteObject(p);
                Txt(dc, { box.right + MulDiv(10, g_dpi, 96), y, x1, y + sz },
                    T(S_WIZ_ADBLOCK), Fnt(F_BODY), CL_OK, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                y += MulDiv(34, g_dpi, 96);
                CheckBoxV2(dc, { x0, y, x1, y + MulDiv(22, g_dpi, 96) }, s.shared, T(S_WIZ_SHARED), W_SHARED);
                y += MulDiv(32, g_dpi, 96);
                CheckBoxV2(dc, { x0, y, x1, y + MulDiv(22, g_dpi, 96) }, s.shortcut, T(S_WIZ_SHORTCUT), W_SHORTCUT);
                y += MulDiv(32, g_dpi, 96);
                CheckBoxV2(dc, { x0, y, x1, y + MulDiv(22, g_dpi, 96) }, s.openAfter, T(S_WIZ_OPEN), W_OPEN);
                y += MulDiv(32, g_dpi, 96);
                CheckBoxV2(dc, { x0, y, x1, y + MulDiv(22, g_dpi, 96) }, s.snapInit, T(S_WIZ_SNAP), W_SNAPINIT);
            } else {
                // review (TZ 6.1 screen 5)
                auto row = [&](const wchar_t* k, const std::wstring& v) {
                    Txt(dc, { x0, y, x0 + MulDiv(220, g_dpi, 96), y + MulDiv(24, g_dpi, 96) }, k,
                        Fnt(F_BODY), CL_SUB, DT_LEFT | DT_SINGLELINE);
                    Txt(dc, { x0 + MulDiv(230, g_dpi, 96), y, x1, y + MulDiv(24, g_dpi, 96) }, v,
                        Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
                    y += MulDiv(26, g_dpi, 96);
                };
                row(T(S_WIZ_NAME), s.name.text);
                row(L"Android", L"Android x86_64");
                row(T(S_PERF_CPU), Fmt(L"%d", s.c.cpuCores));
                row(T(S_PERF_RAM), Fmt(L"%d MB", s.c.ramMb));
                row(T(S_PERF_DISKGB), Fmt(L"%d GB", s.c.diskGb));
                row(T(S_PERF_RES), Fmt(L"%dx%d", s.c.resW, s.c.resH));
                row(T(S_PERF_DPI), std::to_wstring(s.c.dpi));
                row(T(S_PERF_FPS), std::to_wstring(s.c.fpsLimit));
                row(L"ADB", L"127.0.0.1:" + std::to_wstring(NextFreePort()));
                row(T(S_WIZ_PATH), g_p.instances + L"\\instance-NNN");
                ULARGE_INTEGER freeB;
                GetDiskFreeSpaceExW(g_p.dataRoot.c_str(), &freeB, nullptr, nullptr);
                row(T(S_WIZ_SPACE), HumanSize((uint64_t)freeB.QuadPart) + (g_lang ? L" available" : L" доступно"));
            }

            // bottom buttons
            int bh = MulDiv(38, g_dpi, 96);
            int by = rc.bottom - m - bh;
            RECT nx = { rc.right - m - MulDiv(150, g_dpi, 96), by, rc.right - m, by + bh };
            if (s.step < 5) Btn(dc, W_NEXT, nx, T(S_WIZ_NEXT), BSTY_PRIMARY,
                                s.step > 1 || (!locals.empty() && !s.name.text.empty()));
            else Btn(dc, W_NEXT, nx, T(S_WIZ_CREATE), BSTY_PRIMARY, !locals.empty() && !s.name.text.empty());
            if (s.step > 1) {
                RECT pv2 = { nx.left - m / 2 - MulDiv(120, g_dpi, 96), by, nx.left - m / 2, by + bh };
                Btn(dc, W_PREV, pv2, T(S_WIZ_PREV), BSTY_SURF);
            }
            RECT cc = { m, by, m + MulDiv(120, g_dpi, 96), by + bh };
            Btn(dc, W_CLOSE, cc, T(S_BTN_CANCEL), BSTY_GHOST);
        },
        [&](int id) {
            if (s.name.Click(id, W_NAME)) return false;
            if (id >= W_TPL0 && id < W_TPL0 + 8) {
                s.tpl = id - W_TPL0;
                WizApplyTemplate(s);
                return false;
            }
            if (id >= W_IMG0 && id < W_IMG0 + 8) {
                s.imgIdx = id - W_IMG0;
                if (s.imgIdx < (int)locals.size()) s.c.imagePath = locals[s.imgIdx];
                return false;
            }
            auto decInc = [&](int base, int& idx, int n, bool blockDec, bool blockInc) {
                if (id == base) { if (!blockDec && idx > 0) idx--; }
                if (id == base + 1) { if (!blockInc && idx < n - 1) idx++; }
            };
            decInc(W_CPU0, s.cpuIdx, 5, false, false);
            decInc(W_RAM0, s.ramIdx, 6, false, false);
            decInc(W_DISK0, s.diskIdx, 5, false, false);
            decInc(W_FPS0, s.fpsIdx, 4, false, false);
            decInc(W_GPU0, s.gpuIdx, 3, false, false);
            decInc(W_PRIO0, s.prioIdx, 2, false, false);
            s.c.cpuCores = _wtoi(CpuOpts[s.cpuIdx]);
            s.c.ramMb = _wtoi(RamOpts[s.ramIdx]);
            s.c.diskGb = _wtoi(DiskOpts[s.diskIdx]);
            s.c.fpsLimit = _wtoi(FpsOpts[s.fpsIdx]);
            s.c.gpuMode = s.gpuIdx == 2 ? L"virtio" : s.gpuIdx == 1 ? L"std" : L"auto";
            s.c.priority = s.prioIdx ? L"high" : L"normal";
            if (s.step == 3) {
                if (id >= W_RES0 && id < W_RES0 + 5) {
                    s.resIdx = id - W_RES0;
                    if (s.landscape) { s.c.resW = ResPresets[s.resIdx].w; s.c.resH = ResPresets[s.resIdx].h; }
                    else { s.c.resW = ResPresets[s.resIdx].h; s.c.resH = ResPresets[s.resIdx].w; }
                    s.c.dpi = ResPresets[s.resIdx].dpi;
                }
                if (id >= W_DPI0 && id < W_DPI0 + 4) { s.dpiIdx = id - W_DPI0; s.c.dpi = DpiOpts[s.dpiIdx]; }
                if (id == W_ORIENT || id == W_ORIENT + 1) {
                    s.landscape = (id == W_ORIENT);
                    if (s.landscape) { s.c.resW = ResPresets[s.resIdx].w; s.c.resH = ResPresets[s.resIdx].h; }
                    else { s.c.resW = ResPresets[s.resIdx].h; s.c.resH = ResPresets[s.resIdx].w; }
                }
            }
            if (s.step == 4) {
                if (id == W_SHARED) s.shared = !s.shared;
                if (id == W_SHORTCUT) s.shortcut = !s.shortcut;
                if (id == W_OPEN) s.openAfter = !s.openAfter;
                if (id == W_SNAPINIT) s.snapInit = !s.snapInit;
            }
            if (id == W_CLOSE) return true;
            if (id == W_PREV && s.step > 1) { s.step--; return false; }
            if (id == W_NEXT) {
                if (s.step == 1 && (locals.empty() || s.name.text.empty())) return false;
                if (s.step == 1) {
                    s.c.name = s.name.text;
                    if (s.tpl == 7) { /* custom: keep current values */ }
                }
                if (s.step == 5) {
                    // finalize
                    out = s.c;
                    out.wantShortcut = s.shortcut;
                    s.c.icon = L"android";
                    created = true;
                    return true;
                }
                s.step++;
                return false;
            }
            return false;
        },
        [&](MSG* m) { return s.name.Key(m); });
    if (created) {
        out.wantShortcut = s.shortcut;
        out.soundEnabled = s.shared;       // transient stash: shared folder
        out.fullscreen = s.snapInit;       // transient stash: initial snapshot
        out.templateName = s.c.templateName;
        g_wizOpenAfter = s.openAfter;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- wizard post-create
void WizardFinishCreate(InstanceCfg& c) {
    bool snapInit = c.fullscreen;       // transient stash
    c.fullscreen = false;               // reset to real default
    std::wstring err;
    std::wstring id = CreateInstance(c, err);
    if (id.empty()) { UiNotify(err, 2); return; }
    UiNotify((g_lang ? std::wstring(L"Instance created: ") : std::wstring(L"Инстанс создан: ")) + c.name, 0);
    if (c.wantShortcut)
        MakeShortcut(L"NovaDroid - " + c.name, g_p.exeDir + L"\\NovaDroidLauncher.exe",
                     L"--launch " + id, g_p.exeDir);
    if (snapInit) {
        RunOpThread([id, c] {
            std::wstring e1;
            CreateDiskImage(c, e1);            // ensure data disk exists first
            SnapshotEnsureCleanInstall(id);
            UiInvalidate();
        });
    }
    if (g_wizOpenAfter) DoStart(id);   // open Android right away (TZ 6.1 screen 4)
}
