// ============================================================================
//  NovaDroid - v4dlg.cpp  Stages 4-7 dialogs & glue: first-run wizard (TZ5 3.2),
//  macro editor, cloud setup dialog, V4Init/Shutdown/Click/TimerTick.
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v2ui_common.h"
#include "v3.h"
#include "v4.h"

#define WM_APP_V4WIZ (WM_APP + 9)

// shared with v4ui.cpp
extern ApkAbiInfo g_lastApk;
extern std::wstring g_lastApkName;
extern bool g_analyzed;
extern NetDiagResult g_net;
extern bool g_netDone;

// ================================================================ first-run wizard (TZ5 3.2)
namespace {

int g_wizStep = 0;                       // 0 welcome 1 syscheck 2 language 3 theme 4 instance 5 finish
SysCheckResult g_wizSys;
InstanceCfg g_wizInst;
int g_wizTemplate = 1;                   // 0 eco 1 balanced 2 gaming
int g_wizTheme = 0;                      // 0 dark 1 light 2 system
int g_wizLang = 0;                       // 0 ru 1 en

enum { WZ_NEXT = 1, WZ_PREV = 2, WZ_LANG0 = 10, WZ_THEME0 = 20, WZ_TPL0 = 30, WZ_NAME = 40,
       WZ_LAUNCH = 90, WZ_DOCS = 91, WZ_CLOSE = 92 };

void WizApply() {
    g_set.language = g_wizLang ? L"en" : L"ru";
    g_lang = g_wizLang;
    g_set.theme = g_wizTheme == 0 ? L"dark" : g_wizTheme == 1 ? L"light" : L"system";
    V4ApplyTheme();
    g_set.firstRunDone = true;
    SaveSettings();
}

void WizDraw(HDC dc, RECT rc) {
    int sc = g_dpi / 96, m = MulDiv(28, g_dpi, 96);
    int y = m + 10 * sc;
    const wchar_t* titles[] = {
        T(S4_WIZ_WELCOME), T(S4_WIZ_CHECK), T(S4_WIZ_LANG), T(S4_WIZ_THEME),
        T(S4_WIZ_INST), T(S4_WIZ_DONE)
    };
    Txt(dc, { m, y, rc.right - m, y + 34 * sc }, titles[g_wizStep], Fnt(F_TITLE), CL_TEXT, DT_LEFT);
    y += 44 * sc;
    Txt(dc, { m, y, rc.right - m, y + 22 * sc },
        Fmt(L"%s %d / 6", T(S4_WIZ_STEP), g_wizStep + 1), Fnt(F_SMALL), CL_SUB, DT_LEFT);
    y += 34 * sc;
    int W = rc.right - rc.left - m * 2;

    if (g_wizStep == 0) {
        Txt(dc, { m, y, rc.right - m, y + 60 * sc }, T(S4_WIZ_WELCOMETXT), Fnt(F_BODY),
            CL_SUB, DT_LEFT | DT_WORDBREAK);
        y += 70 * sc;
        Txt(dc, { m, y, rc.right - m, y + 24 * sc }, L"NovaDroid 0.4.0", Fnt(F_H2), CL_ACCENT2, DT_LEFT);
    } else if (g_wizStep == 1) {
        g_wizSys = V4SystemCheck();
        struct Row { std::wstring k; bool ok; std::wstring v; };
        std::vector<Row> rows = {
            { T(S_D_VIRT), g_wizSys.virt, g_wizSys.virt ? L"VT-x / AMD-V" : T(S4_WIZ_NOVIRT) },
            { T(S_D_WHPX), g_wizSys.whpx, g_wizSys.whpx ? L"WHPX / Hyper-V" : T(S4_WIZ_NOWHPX) },
            { L"RAM", g_wizSys.ramMb >= 3800, Fmt(L"%lld MB", g_wizSys.ramMb) },
            { T(S4_WIZ_DISK), g_wizSys.diskFreeGb >= 12, Fmt(L"%lld GB", g_wizSys.diskFreeGb) },
            { L"GPU", g_wizSys.gpuName != L"?", g_wizSys.gpuName },
        };
        for (auto& r : rows) {
            FillRound(dc, { m, y, rc.right - m, y + 36 * sc }, CL_SURF2, 8);
            Dot(dc, m + 18 * sc, y + 18 * sc, 5 * sc, r.ok ? CL_OK : CL_WARN);
            Txt(dc, { m + 34 * sc, y + 6 * sc, m + 250 * sc, y + 30 * sc }, r.k, Fnt(F_BODY), CL_TEXT, DT_LEFT);
            Txt(dc, { m + 260 * sc, y + 6 * sc, rc.right - m - 14 * sc, y + 30 * sc }, r.v, Fnt(F_SMALL), CL_SUB, DT_LEFT);
            y += 42 * sc;
        }
        if (!g_wizSys.allOk())
            Txt(dc, { m, y + 4 * sc, rc.right - m, y + 44 * sc }, T(S4_WIZ_SYSWARN),
                Fnt(F_SMALL), CL_WARN, DT_LEFT | DT_WORDBREAK);
    } else if (g_wizStep == 2) {
        const wchar_t* langs[] = { L"Русский", L"English" };
        for (int i = 0; i < 2; ++i)
            Btn(dc, WZ_LANG0 + i, { m, y + i * 46 * sc, m + 300 * sc, y + i * 46 * sc + 38 * sc },
                langs[i], g_wizLang == i ? BSTY_PRIMARY : BSTY_SURF);
    } else if (g_wizStep == 3) {
        const wchar_t* th[] = { T(S4_WIZ_DARK), T(S4_WIZ_LIGHT), T(S4_WIZ_SYS) };
        for (int i = 0; i < 3; ++i)
            Btn(dc, WZ_THEME0 + i, { m, y + i * 46 * sc, m + 300 * sc, y + i * 46 * sc + 38 * sc },
                th[i], g_wizTheme == i ? BSTY_PRIMARY : BSTY_SURF);
    } else if (g_wizStep == 4) {
        Txt(dc, { m, y, rc.right - m, y + 22 * sc }, T(S_DLG_NAME), Fnt(F_SMALL), CL_SUB, DT_LEFT);
        y += 26 * sc;
        FillRound(dc, { m, y, rc.right - m, y + 38 * sc }, CL_BG, 8);
        FrameRound(dc, { m, y, rc.right - m, y + 38 * sc }, CL_BORDER, 8);
        Txt(dc, { m + 12 * sc, y + 7 * sc, rc.right - m - 12 * sc, y + 31 * sc },
            g_wizInst.name, Fnt(F_BODY), CL_TEXT, DT_LEFT);
        Reg(WZ_NAME, { m, y, rc.right, y + 38 * sc }, K_LINK);
        y += 52 * sc;
        Txt(dc, { m, y, rc.right - m, y + 22 * sc }, T(S_WIZ_TEMPLATE), Fnt(F_SMALL), CL_SUB, DT_LEFT);
        y += 26 * sc;
        const wchar_t* tplRu[] = { T(S3_PROF_TITLE), L"", L"" };
        const wchar_t* tplNames[] = { L"Экономный", L"Сбалансированный", L"Игровой" };
        const wchar_t* tplNamesEn[] = { L"Economy", L"Balanced", L"Gaming" };
        for (int i = 0; i < 3; ++i)
            Btn(dc, WZ_TPL0 + i, { m + i * 200 * sc, y, m + i * 200 * sc + 190 * sc, y + 40 * sc },
                g_lang == 0 ? tplNames[i] : tplNamesEn[i], g_wizTemplate == i ? BSTY_PRIMARY : BSTY_SURF);
    } else if (g_wizStep == 5) {
        Txt(dc, { m, y, rc.right - m, y + 40 * sc }, T(S4_WIZ_DONETXT), Fnt(F_BODY), CL_SUB,
            DT_LEFT | DT_WORDBREAK);
    }

    // buttons
    int bh = 38 * sc, by = rc.bottom - MulDiv(20, g_dpi, 96) - bh;
    if (g_wizStep > 0)
        Btn(dc, WZ_PREV, { m, by, m + 130 * sc, by + bh }, T(S_WIZ_PREV), BSTY_GHOST);
    if (g_wizStep == 5) {
        Btn(dc, WZ_LAUNCH, { m + 150 * sc, by, m + 400 * sc, by + bh }, T(S4_WIZ_LAUNCH), BSTY_PRIMARY);
        Btn(dc, WZ_DOCS, { m + 410 * sc, by, m + 640 * sc, by + bh }, T(S4_WIZ_DOCS), BSTY_SURF);
        Btn(dc, WZ_CLOSE, { rc.right - m - 130 * sc, by, rc.right - m, by + bh }, T(S_BTN_SAVE), BSTY_OK);
    } else {
        Btn(dc, WZ_NEXT, { rc.right - m - 130 * sc, by, rc.right - m, by + bh }, T(S_WIZ_NEXT), BSTY_PRIMARY);
    }
}

bool WizClick(int id) {
    switch (id) {
    case WZ_NEXT: ++g_wizStep; return false;
    case WZ_PREV: --g_wizStep; return false;
    case WZ_LANG0: case WZ_LANG0 + 1: g_wizLang = id - WZ_LANG0; return false;
    case WZ_THEME0: case WZ_THEME0 + 1: case WZ_THEME0 + 2: g_wizTheme = id - WZ_THEME0; return false;
    case WZ_TPL0: case WZ_TPL0 + 1: case WZ_TPL0 + 2: g_wizTemplate = id - WZ_TPL0; return false;
    case WZ_NAME: {
        std::wstring v = DialogInput(g_wnd, T(S_DLG_NAME), g_wizInst.name);
        if (!v.empty()) g_wizInst.name = v;
        return false;
    }
    case WZ_LAUNCH: {
        WizApply();
        // create the first instance (TZ5 3.2 screen 5)
        std::wstring err;
        g_wizInst.templateName = g_wizTemplate == 0 ? L"eco" : g_wizTemplate == 2 ? L"gaming" : L"balanced";
        g_wizInst.imagePath = g_set.defaultImagePath;
        std::wstring id = CreateInstance(g_wizInst, err);
        if (!id.empty()) {
            if (g_wizTemplate == 0) V3ApplyProfile(id, L"eco", err);
            else if (g_wizTemplate == 2) V3ApplyProfile(id, L"gaming", err);
            DoStart(id);
        } else UiNotify(err, 2);
        return true;
    }
    case WZ_DOCS:
        WizApply();
        ShellOpen(g_p.exeDir + L"\\docs\\README.md");
        return true;
    case WZ_CLOSE:
        WizApply();
        return true;
    }
    return false;
}

void RunWizard() {
    g_wizStep = 0;
    g_wizSys = {};
    g_wizInst = InstanceCfg();
    g_wizInst.name = g_lang ? L"My Android" : L"Мой Android";
    g_wizLang = g_lang;
    V2ModalRun(g_wnd, MulDiv(760, g_dpi, 96), MulDiv(560, g_dpi, 96),
               WizDraw, [](int id) { return WizClick(id); }, nullptr);
}

} // namespace

void V4FirstRunWizard(HWND parent) { RunWizard(); }

// ================================================================ macro editor (TZ7 5.4)
namespace {

enum { ME_CLOSE = 1, ME_ADDTAP = 2, ME_ADDKEY = 3, ME_ADDWAIT = 4, ME_ADDTEXT = 5,
       ME_DELLAST = 6, ME_NAME = 7, ME_REP = 8, ME_HOTKEY = 9, ME_PLAY = 10 };

int g_meSel = -1;

std::wstring StepLabel(const MacroStep& s) {
    if (s.type == L"tap") return Fmt(L"Tap %d,%d", s.x, s.y);
    if (s.type == L"longtap") return Fmt(L"Long tap %d,%d", s.x, s.y);
    if (s.type == L"swipe") return Fmt(L"Swipe %d,%d -> %d,%d", s.x, s.y, s.x2, s.y2);
    if (s.type == L"key") return Fmt(L"Key %d (%s)", s.key, KeymapVkName(s.key).c_str());
    if (s.type == L"text") return Fmt(L"Text \"%s\"", s.text.c_str());
    return Fmt(L"Wait %d ms", s.durMs);
}

void Medraw(HDC dc, RECT rc, Macro& m) {
    int sc = g_dpi / 96, m2 = MulDiv(16, g_dpi, 96);
    int y = m2;
    Txt(dc, { m2, y, rc.right - m2, y + 28 * sc }, T(S4_MAC_EDITORT), Fnt(F_H2), CL_TEXT, DT_LEFT);
    y += 34 * sc;
    // name / repeat / hotkey row
    Btn(dc, ME_NAME, { m2, y, m2 + 200 * sc, y + 32 * sc }, m.name, BSTY_SURF);
    Btn(dc, ME_REP, { m2 + 210 * sc, y, m2 + 340 * sc, y + 32 * sc },
        Fmt(L"x%d", m.repeat), BSTY_SURF);
    Btn(dc, ME_HOTKEY, { m2 + 350 * sc, y, m2 + 540 * sc, y + 32 * sc },
        m.hotkeyVk.empty() ? std::wstring(T(S4_MAC_NOHOT)) : (L"VK " + m.hotkeyVk), BSTY_SURF);
    y += 42 * sc;
    // steps list
    int listH = rc.bottom - y - 56 * sc;
    RECT list = { m2, y, rc.right - m2, y + listH };
    FillRound(dc, list, CL_BG, 8);
    FrameRound(dc, list, CL_BORDER, 8);
    int ry = y + 8 * sc;
    int i = 0;
    for (auto& s : m.steps) {
        if (ry + 26 * sc > list.bottom) break;
        RECT row = { list.left + 6 * sc, ry, list.right - 6 * sc, ry + 24 * sc };
        if (i == g_meSel) FillRound(dc, row, CL_SURF2, 6);
        Txt(dc, { row.left + 8 * sc, ry + 3 * sc, row.right - 10 * sc, ry + 21 * sc },
            Fmt(L"%d. ", i + 1) + StepLabel(s), Fnt(F_SMALL), CL_TEXT, DT_LEFT);
        Reg(ID4_MAC0 + 900 + i, row, K_LINK);          // select row
        ry += 26 * sc; ++i;
    }
    // add buttons
    int by = rc.bottom - m2 - 36 * sc;
    const int ids[5] = { ME_ADDTAP, ME_ADDKEY, ME_ADDWAIT, ME_ADDTEXT, ME_DELLAST };
    const wchar_t* lbl[5] = { T(S4_MAC_ADDTAP), T(S4_MAC_ADDKEY), T(S4_MAC_ADDWAIT),
                              T(S4_MAC_ADDTEXT), T(S4_MAC_DELSTEP) };
    int bx = m2;
    for (int k = 0; k < 5; ++k) {
        Btn(dc, ids[k], { bx, by, bx + 110 * sc, by + 34 * sc }, lbl[k], k == 4 ? BSTY_DANGER : BSTY_GHOST);
        bx += 118 * sc;
    }
    Btn(dc, ME_PLAY, { rc.right - m2 - 120 * sc, by, rc.right - m2, by + 34 * sc }, T(S4_MAC_PLAY), BSTY_PRIMARY);
    Btn(dc, ME_CLOSE, { rc.right - m2 - 120 * sc, m2, rc.right - m2, m2 + 30 * sc }, T(S_BTN_SAVE), BSTY_OK);
}

} // namespace

bool DialogMacroEditor(HWND parent, Macro& m) {
    g_meSel = -1;
    bool done = false;
    V2ModalRun(parent, MulDiv(720, g_dpi, 96), MulDiv(520, g_dpi, 96),
        [&](HDC dc, RECT rc) { Medraw(dc, rc, m); },
        [&](int id) -> bool {
            switch (id) {
            case ME_CLOSE: V4MacrosSave(); done = true; return true;
            case ME_NAME: {
                std::wstring v = DialogInput(parent, T(S_KM_NAME), m.name);
                if (!v.empty()) m.name = v;
                return false;
            }
            case ME_REP: {
                std::wstring v = DialogInput(parent, T(S4_MAC_REPEAT), Fmt(L"%d", m.repeat));
                int n = _wtoi(v.c_str());
                if (n > 0 && n <= 999) m.repeat = n;
                return false;
            }
            case ME_HOTKEY: {
                int vk = 0;
                if (DialogKeyCapture(parent, vk)) m.hotkeyVk = Fmt(L"%d", vk);
                return false;
            }
            case ME_ADDTAP: {
                MacroStep s; s.type = L"tap";
                std::wstring v = DialogInput(parent, L"x,y", L"640,360");
                swscanf(v.c_str(), L"%d,%d", &s.x, &s.y);
                m.steps.push_back(s); return false;
            }
            case ME_ADDKEY: {
                int vk = 0;
                if (DialogKeyCapture(parent, vk)) {
                    MacroStep s; s.type = L"key"; s.key = vk;
                    m.steps.push_back(s);
                }
                return false;
            }
            case ME_ADDWAIT: {
                MacroStep s; s.type = L"wait"; s.durMs = 500;
                std::wstring v = DialogInput(parent, L"ms", L"500");
                s.durMs = _wtoi(v.c_str());
                m.steps.push_back(s); return false;
            }
            case ME_ADDTEXT: {
                MacroStep s; s.type = L"text";
                s.text = DialogInput(parent, T(S4_MAC_ADDTEXT), L"");
                if (!s.text.empty()) m.steps.push_back(s);
                return false;
            }
            case ME_DELLAST:
                if (!m.steps.empty()) m.steps.pop_back();
                return false;
            case ME_PLAY:
                return false;
            default:
                if (id >= ID4_MAC0 + 900 && id < ID4_MAC0 + 1900) {
                    g_meSel = id - ID4_MAC0 - 900;
                    return false;
                }
            }
            return false;
        }, nullptr);
    return done;
}

// ================================================================ cloud setup (TZ6 4.3)
namespace {
enum { CS_SAVE = 1, CS_CANCEL = 2, CS_PROV0 = 10, CS_ENDPOINT = 20, CS_TOKEN = 21, CS_ENC = 22 };

int g_csProv = 0;                        // 0 webdav 1 yadisk 2 dropbox 3 onedrive
std::wstring g_csEndpoint, g_csToken;
bool g_csEnc = true;
const wchar_t* g_csProvIds[] = { L"webdav", L"yadisk", L"dropbox", L"onedrive" };

void CsDraw(HDC dc, RECT rc) {
    int sc = g_dpi / 96, m = MulDiv(16, g_dpi, 96);
    int y = m;
    Txt(dc, { m, y, rc.right - m, y + 28 * sc }, T(S4_CLD_SETUPT), Fnt(F_H2), CL_TEXT, DT_LEFT);
    y += 36 * sc;
    const wchar_t* provs[] = { L"WebDAV", L"Яндекс.Диск", L"Dropbox", L"OneDrive" };
    for (int i = 0; i < 4; ++i)
        Btn(dc, CS_PROV0 + i, { m + i * 160 * sc, y, m + i * 160 * sc + 152 * sc, y + 34 * sc },
            provs[i], g_csProv == i ? BSTY_PRIMARY : BSTY_SURF);
    y += 46 * sc;
    Btn(dc, CS_ENDPOINT, { m, y, rc.right - m, y + 34 * sc },
        std::wstring(T(S4_CLD_SERVER)) + L": " + (g_csEndpoint.empty() ? std::wstring(L"https://dav.example.com/dav") : g_csEndpoint), BSTY_SURF);
    y += 42 * sc;
    Btn(dc, CS_TOKEN, { m, y, rc.right - m, y + 34 * sc },
        std::wstring(T(S4_CLD_TOKEN)) + L": " + (g_csToken.empty() ? std::wstring(L"-") : std::wstring(L"***")), BSTY_SURF);
    y += 42 * sc;
    Btn(dc, CS_ENC, { m, y, m + 320 * sc, y + 34 * sc },
        g_csEnc ? T(S4_CLD_ENC_ON) : T(S4_CLD_ENC_OFF), g_csEnc ? BSTY_OK : BSTY_SURF);
    y += 46 * sc;
    Txt(dc, { m, y, rc.right - m, y + 40 * sc }, T(S4_CLD_NOTE), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    Btn(dc, CS_SAVE, { rc.right - m - 220 * sc, rc.bottom - m - 36 * sc, rc.right - m, rc.bottom - m },
        T(S_BTN_SAVE), BSTY_PRIMARY);
    Btn(dc, CS_CANCEL, { rc.right - m - 450 * sc, rc.bottom - m - 36 * sc, rc.right - m - 230 * sc, rc.bottom - m },
        T(S_BTN_CANCEL), BSTY_GHOST);
}
} // namespace

bool DialogCloudSetup(HWND parent) {
    g_csProv = 0;
    for (int i = 0; i < 4; ++i)
        if (g_set.cloudProvider == g_csProvIds[i]) g_csProv = i;
    g_csEndpoint = g_set.cloudEndpoint;
    g_csToken = g_set.cloudToken;
    g_csEnc = g_set.cloudEncrypt;
    bool done = false;
    V2ModalRun(parent, MulDiv(700, g_dpi, 96), MulDiv(420, g_dpi, 96),
        [&](HDC dc, RECT rc) { CsDraw(dc, rc); },
        [&](int id) -> bool {
            switch (id) {
            case CS_SAVE:
                g_set.cloudProvider = g_csProvIds[g_csProv];
                g_set.cloudEndpoint = g_csEndpoint;
                g_set.cloudToken = g_csToken;
                g_set.cloudEncrypt = g_csEnc;
                SaveSettings();
                done = true; return true;
            case CS_CANCEL: done = true; return true;
            case CS_PROV0: case CS_PROV0 + 1: case CS_PROV0 + 2: case CS_PROV0 + 3:
                g_csProv = id - CS_PROV0;
                if (g_csProv == 1) g_csEndpoint = L"https://webdav.yandex.ru";
                if (g_csProv == 0 && g_csEndpoint.empty()) g_csEndpoint = L"https://dav.example.com/dav";
                return false;
            case CS_ENDPOINT: {
                std::wstring v = DialogInput(parent, T(S4_CLD_SERVER), g_csEndpoint);
                if (!v.empty()) g_csEndpoint = v;
                return false;
            }
            case CS_TOKEN: {
                std::wstring v = DialogInput(parent, T(S4_CLD_TOKEN), L"");
                if (!v.empty()) g_csToken = v;
                return false;
            }
            case CS_ENC: g_csEnc = !g_csEnc; return false;
            }
            return false;
        }, nullptr);
    return done;
}

// ================================================================ V4 glue
void V4Init() {
    V4ApplyTheme();
    V4PluginsScan();
    V4MacrosLoad();
    V4SyncGroupsLoad();
    V4AppProfiles();                       // load embedded + user overrides
    srand((unsigned)GetTickCount64());
    if (!g_set.firstRunDone && g_wnd)
        PostMessageW(g_wnd, WM_APP_V4WIZ, 0, 0);
    LogW(L"v4", L"stages 4-7 initialized");
}

void V4Shutdown() {
    V4StreamStop();
    if (V4MacroRecording()) V4MacroRecordStop();
    V4DiscordClear();
    V4MacrosSave();
    V4SyncGroupsSave();
}

void V4TimerTick() {
    static int slowTick = 0;
    slowTick++;
    V4AntiLagTick();
    V4DiscordTick();
    // auto-stop stream if its instance died
    if (V4StreamRunning()) {
        Runtime* r = Rt(V4StreamInstanceId());
        if (!r || !r->hProc || !ProcAlive(r->hProc)) V4StreamStop();
    }
    // one-time update check ~20 s after start (TZ5 3.3)
    static bool updChecked = false;
    if (!updChecked && slowTick > 40) {
        updChecked = true;
        V4UpdateCheckAsync(false);
    }
}

static void ClickAnalyze() {
    std::wstring apk = PickFile(g_wnd, L"Android package (.apk)\0*.apk\0All files\0*.*\0",
                                T(S4_COM_ANALYZE), L"apk");
    if (apk.empty()) return;
    g_lastApkName = BaseName(apk);
    if (V4AnalyzeApk(apk, g_lastApk)) {
        g_analyzed = true;
        UiNotify(g_lastApk.verdict, g_lastApk.level == 0 ? 0 : 1);
    } else {
        g_analyzed = false;
        UiNotify(T(S_MB_BADAPK), 2);
    }
}

bool V4Click(int id) {
    if (id >= ID4_ANA0 && id < ID4_DEV0) {
        if (id == ID4_ANA0) ClickAnalyze();
        else if (id == ID4_ANA0 + 1) { V4AnnotateApkLibrary(); UiNotify(T(S4_COM_RESCANNED), 0); }
        return true;
    }
    if (id >= ID4_DEV0 && id < ID4_APP0) {
        int i = (id - ID4_DEV0) / 2;
        if (i < (int)V4DeviceProfiles().size()) {
            std::wstring sel = SelInst();
            if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
            std::wstring err;
            if (V4ApplyDeviceProfile(sel, V4DeviceProfiles()[i].id, err)) UiNotify(T(S4_PROF_OK), 0);
            else UiNotify(err, 2);
        }
        return true;
    }
    if (id >= ID4_APP0 && id < ID4_GPS0) {
        int i = (id - ID4_APP0) / 2;
        auto& apps = V4AppProfiles();
        if (i < (int)apps.size()) {
            std::wstring sel = SelInst();
            if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
            V4MaybeApplyAppProfile(sel, apps[i].package);
        }
        return true;
    }
    if (id >= ID4_GPS0 && id < ID4_NET0) {
        std::wstring sel = SelInst();
        if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
        std::wstring err;
        bool ok = false;
        switch (id - ID4_GPS0) {
        case 0: {                                     // custom GPS
            std::wstring v = DialogInput(g_wnd, T(S4_GPS_CUSTOM), L"55.7558,37.6173");
            double la = 0, lo = 0;
            if (swscanf(v.c_str(), L"%lf,%lf", &la, &lo) == 2) ok = V4SensorGps(sel, la, lo, err);
            else err = T(S4_GPS_BADFMT);
            break;
        }
        case 1: ok = V4SensorGps(sel, 55.7558, 37.6173, err); break;      // Moscow
        case 2: ok = V4SensorGps(sel, 40.7128, -74.0060, err); break;     // New York
        case 3: ok = V4SensorGps(sel, 51.5074, -0.1278, err); break;      // London
        case 4: ok = V4SensorOrient(sel, 0, err); break;
        case 5: ok = V4SensorOrient(sel, 1, err); break;
        case 6: ok = V4SensorOrient(sel, 2, err); break;
        case 7: {                                     // battery
            std::wstring v = DialogInput(g_wnd, T(S4_BAT_CUSTOM), L"80");
            ok = V4SensorBattery(sel, _wtoi(v.c_str()), 2, err);
            break;
        }
        case 8: ok = V4SensorBatteryReset(sel, err); break;
        }
        if (ok) UiNotify(T(S4_SENS_OK), 0);
        else if (!err.empty()) UiNotify(err, 2);
        return true;
    }
    if (id >= ID4_NET0 && id < ID4_UPD0) {
        std::wstring sel = SelInst();
        if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
        switch (id - ID4_NET0) {
        case 0: {
            UiNotify(T(S4_NET_RUNNING), 0);
            RunOpThread([sel]() {
                NetDiagResult r = V4NetDiag(sel);
                g_net = r; g_netDone = true;
                if (g_wnd) UiNotify(r.detail, r.internet ? 0 : 1);
            });
            break;
        }
        case 1: {
            std::wstring err;
            if (V4NetFixDns(sel, err)) { UiNotify(T(S4_NET_FIXED), 0);
                InstanceCfg* c = Inst(sel);
                if (c) { c->dnsServer = L"8.8.8.8"; PersistInstance(*c); } }
            else UiNotify(err, 2);
            break;
        }
        case 2: {
            InstanceCfg* c = Inst(sel);
            if (c) { c->dnsServer = L""; PersistInstance(*c); }
            UiNotify(T(S4_NET_RESETOK), 0);
            break;
        }
        }
        return true;
    }
    if (id >= ID4_UPD0 && id < ID4_LOG0) {
        switch (id - ID4_UPD0) {
        case 0: V4UpdateCheckAsync(true); break;
        case 1: V4UpdateInstall(); break;
        case 2:
            g_set.updateChannel = (g_set.updateChannel == L"stable") ? L"beta" : L"stable";
            SaveSettings();
            break;
        }
        return true;
    }
    if (id >= ID4_LOG0 && id < ID4_STR0) {
        if (id == ID4_LOG0) {
            std::wstring sel = SelInst();
            if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
            UiNotify(T(S4_LOG_ZIPPING), 0);
            RunOpThread([sel]() {
                std::wstring zip = V4CollectLogsZip(sel);
                if (!zip.empty()) {
                    UiNotify(Fmt(T(S4_LOG_DONE), zip.c_str()), 0);
                    OpenInExplorer(zip);
                } else UiNotify(T(S4_LOG_FAIL), 2);
            });
        }
        return true;
    }
    if (id >= ID4_STR0 && id < ID4_PLG0) {
        switch (id - ID4_STR0) {
        case 0: {
            if (V4StreamRunning()) V4StreamStop();
            else {
                std::wstring sel = SelInst();
                if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
                Runtime* r = Rt(sel);
                if (!r || !r->hProc || !ProcAlive(r->hProc)) { UiNotify(T(S4_MAC_NORUN), 2); return true; }
                std::wstring err;
                if (!V4StreamStart(sel, (unsigned short)g_set.streamPort, err)) UiNotify(err, 2);
            }
            break;
        }
        case 1: if (V4StreamRunning()) ShellOpen(V4StreamUrl()); break;
        }
        return true;
    }
    if (id >= ID4_PLG0 && id < ID4_CLD0) {
        if (id == ID4_PLG0 + 20) { V4PluginsScan(); UiNotify(T(S4_PLG_RESCANNED), 0); return true; }
        int i = (id - ID4_PLG0) / 2;
        auto& plg = V4Plugins();
        if (i < (int)plg.size()) {
            V4PluginToggle(plg[i].name, !plg[i].enabled);
        }
        return true;
    }
    if (id >= ID4_CLD0 && id < ID4_MAC0) {
        switch (id - ID4_CLD0) {
        case 0: DialogCloudSetup(g_wnd); break;
        case 1:
            if (g_set.cloudProvider.empty()) { UiNotify(T(S4_CLD_NOTSET2), 1); break; }
            V4CloudBackupAsync(nullptr);
            break;
        case 2: {
            std::wstring err;
            auto files = V4CloudList(err);
            if (files.empty()) { UiNotify(err.empty() ? T(S4_CLD_NOFILES) : err, 1); break; }
            // restore the most recent (pick via input)
            std::wstring list;
            for (auto& f : files) list += f + L"\n";
            std::wstring pick = DialogInput(g_wnd, T(S4_CLD_PICKFILE), files.back());
            if (!pick.empty()) V4CloudRestoreAsync(pick, nullptr);
            break;
        }
        }
        return true;
    }
    if (id >= ID4_MAC0 && id < ID4_ALG0) {
        if (id == ID4_MAC0) {                                 // record start/stop
            if (V4MacroRecording()) V4MacroRecordStop();
            else {
                std::wstring sel = SelInst();
                if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
                V4MacroRecordStart(sel);
                UiNotify(T(S4_MAC_RECON), 0);
            }
            return true;
        }
        if (id >= ID4_MAC0 + 900) return true;                // editor row select
        int ii = (id - ID4_MAC0 - 1);
        int i = ii / 5;
        int k = ii % 5 + 1;                                   // 1 edit 2 play 3 stop 4 delete
        auto& mac = V4Macros();
        if (i < 0 || i >= (int)mac.size()) return true;
        std::wstring mid = mac[i].id;
        switch (k) {
        case 1: {                                             // edit
            Macro* m = V4MacroById(mid);
            if (m) { Macro copy = *m; DialogMacroEditor(g_wnd, copy); *m = copy; V4MacrosSave(); }
            break;
        }
        case 2: {
            std::wstring err;
            if (!V4MacroPlayAsync(mid, err)) UiNotify(err, 2);
            break;
        }
        case 3: V4MacroStopPlay(); break;
        case 4:
            V4MacroDelete(mid);
            UiNotify(T(S4_MAC_DELETED), 0);
            break;
        }
        return true;
    }
    if (id >= ID4_ALG0 && id < ID4_SYN0) {
        if (id == ID4_ALG0) V4AntiLagSet(!V4AntiLagOn());
        else {
            const wchar_t* ids[] = { L"perf", L"balance", L"quality", L"eco" };
            V4AntiLagSetMode(ids[id - ID4_ALG0 - 1]);
        }
        return true;
    }
    if (id >= ID4_SYN0 && id < ID4_DSC0) {
        switch (id - ID4_SYN0) {
        case 0: {
            std::wstring sel = SelInst();
            if (sel.empty()) { UiNotify(T(S4_NOSEL), 1); return true; }
            Runtime* r = Rt(sel);
            if (!r || !r->hProc || !ProcAlive(r->hProc)) { UiNotify(T(S4_MAC_NORUN), 2); return true; }
            auto& sg = V4SyncGroups();
            for (auto& g : sg)
                if (g.masterId == sel) { UiNotify(T(S4_SYN_ISMASTER), 1); return true; }
            // slaves = other running instances
            SyncGroup g;
            g.masterId = sel;
            {
                std::lock_guard<std::mutex> lk(g_mx);
                for (auto& kv : g_rt)
                    if (kv.first != sel && kv.second.hProc && ProcAlive(kv.second.hProc))
                        g.slaveIds.push_back(kv.first);
            }
            if (g.slaveIds.empty()) { UiNotify(T(S4_SYN_NOSLAVES), 1); return true; }
            sg.push_back(g);
            V4SyncGroupsSave();
            UiNotify(Fmt(T(S4_SYN_ADDED), (int)g.slaveIds.size()), 0);
            break;
        }
        case 1: {
            std::wstring sel = SelInst();
            if (!sel.empty()) V4SyncRemove(sel);
            UiNotify(T(S4_SYN_REMOVED), 0);
            break;
        }
        }
        return true;
    }
    if (id >= ID4_DSC0 && id < ID4_PRO0) {
        if (id == ID4_DSC0) {
            g_set.discordEnabled = !g_set.discordEnabled;
            SaveSettings();
            if (g_set.discordEnabled) V4DiscordTick();
            else V4DiscordClear();
        }
        return true;
    }
    if (id >= ID4_PRO0 && id < ID4_WIZ0) {
        if (id == ID4_PRO0) {
            if (!g_set.proKey.empty()) { UiNotify(Fmt(T(S4_PRO_ISACTIVE), g_set.proKey.c_str()), 0); return true; }
            std::wstring k = DialogInput(g_wnd, T(S4_PRO_ENTER), L"NOVA-XXXX-XXXX");
            if (k.empty()) return true;
            // offline check: NOVA- prefix + length
            if (k.rfind(L"NOVA-", 0) == 0 && k.size() >= 12) {
                g_set.proKey = k;
                SaveSettings();
                UiNotify(T(S4_PRO_OK), 0);
            } else UiNotify(T(S4_PRO_BAD), 2);
        }
        return true;
    }
    return false;
}
