// ============================================================================
//  NovaDroid - v4ui.cpp  Stages 4-7 UI: pages "Compatibility", "Sensors&Network",
//  "Advanced" (updates/logs/stream/plugins/cloud/macros/sync/discord/antilag/pro)
//  in the existing NovaDroid design language (#111318 / #6C63FF).
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v2ui_common.h"
#include "v3.h"
#include "v4.h"

// ================================================================ helpers
static void Card(HDC dc, RECT r, const std::wstring& title, int* yOut, int* sc) {
    FillRound(dc, r, CL_SURF2, 12);
    FrameRound(dc, r, CL_BORDER, 12);
    int m = MulDiv(16, g_dpi, 96);
    Txt(dc, { r.left + m, r.top + 12 * *sc, r.right - m, r.top + 34 * *sc },
        title, Fnt(F_H2), CL_TEXT, DT_LEFT);
    if (yOut) *yOut = r.top + 44 * *sc;
}

static void StatusPill(HDC dc, RECT r, bool ok, const wchar_t* okTxt, const wchar_t* badTxt) {
    Pill(dc, r, ok ? okTxt : badTxt, ok ? CL_OK : CL_WARN);
}

// shared with v4dlg.cpp (click routing lives there)
ApkAbiInfo g_lastApk;
std::wstring g_lastApkName;
bool g_analyzed = false;
NetDiagResult g_net;
bool g_netDone = false;

static std::wstring L(int ru, const wchar_t* en) { return g_lang == 0 ? std::wstring(T(ru)) : std::wstring(en); }

// ================================================================ Page: Compatibility (TZ4)

void PageCompat(HDC dc, RECT rc) {
    int sc = g_dpi / 96;
    int m = MulDiv(28, g_dpi, 96), y = m;
    PageHeader(dc, T(S4_COM_TITLE), T(S4_COM_SUB), rc, &y);
    int& scro = g_scroll[(int)Pg::Compat];
    int W = rc.right - rc.left - m * 2;

    // ---- APK ABI analyzer (TZ4 2.1)
    int cardH = (g_analyzed ? 210 : 120) * sc;
    RECT c1 = { m, y - scro, m + W, y - scro + cardH };
    int cy = 0; Card(dc, c1, T(S4_COM_APK), &cy, &sc);
    Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 34 * sc },
        T(S4_COM_APKHINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 40 * sc;
    Btn(dc, ID4_ANA0 + 0, { c1.left + 16 * sc, cy, c1.left + 216 * sc, cy + 34 * sc },
        T(S4_COM_ANALYZE), BSTY_PRIMARY);
    Btn(dc, ID4_ANA0 + 1, { c1.left + 226 * sc, cy, c1.left + 446 * sc, cy + 34 * sc },
        T(S4_COM_RESCAN), BSTY_SURF);
    cy += 44 * sc;
    if (g_analyzed) {
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
            g_lastApkName, Fnt(F_BODY), CL_ACCENT2, DT_LEFT);
        cy += 26 * sc;
        std::wstring info = Fmt(L"%s: %s v%s (%d)  minSdk=%d targetSdk=%d",
                                T(S4_COM_PKG), g_lastApk.package.c_str(),
                                g_lastApk.versionName.c_str(), g_lastApk.versionCode,
                                g_lastApk.minSdk, g_lastApk.targetSdk);
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
            info, Fnt(F_SMALL), CL_SUB, DT_LEFT);
        cy += 26 * sc;
        // ABI flags
        std::wstring abi;
        auto tag = [&](bool on, const wchar_t* s) { if (on) { if (!abi.empty()) abi += L", "; abi += s; } };
        tag(g_lastApk.hasX86_64, L"x86_64"); tag(g_lastApk.hasX86, L"x86");
        tag(g_lastApk.hasArm64, L"arm64-v8a"); tag(g_lastApk.hasArm32, L"armeabi-v7a");
        if (g_lastApk.noLibs) abi = T(S4_ABI_NOLIBS);
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
            Fmt(L"ABI: %s", abi.c_str()), Fnt(F_SMALL), CL_TEXT, DT_LEFT);
        cy += 26 * sc;
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 40 * sc },
            g_lastApk.verdict, Fnt(F_BODY),
            g_lastApk.level == 0 ? CL_OK : g_lastApk.level == 1 ? CL_WARN : CL_ERR,
            DT_LEFT | DT_WORDBREAK);
    }
    y += cardH + 14 * sc;

    // ---- device profiles (TZ4 2.4)
    int devH = 190 * sc;
    RECT c2 = { m, y - scro, m + W, y - scro + devH };
    Card(dc, c2, T(S4_COM_DEV), &cy, &sc);
    Txt(dc, { c2.left + 16 * sc, cy, c2.right - 16 * sc, cy + 22 * sc },
        Fmt(T(S4_COM_DEVSUB), SelInst().empty() ? T(S4_NOSEL) : Inst(SelInst())->name.c_str()),
        Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 30 * sc;
    int bw = (W - 32 * sc - 3 * 10 * sc) / 4, bh = 64 * sc;
    for (int i = 0; i < (int)V4DeviceProfiles().size(); ++i) {
        auto& p = V4DeviceProfiles()[i];
        RECT rb = { c2.left + 16 * sc + i * (bw + 10 * sc), cy,
                    c2.left + 16 * sc + i * (bw + 10 * sc) + bw, cy + bh };
        Btn(dc, ID4_DEV0 + i * 2, rb,
            std::wstring(g_lang == 0 ? p.nameRu : p.nameEn) + L"\n" +
            Fmt(L"%dx%d · %d", p.w, p.h, p.dpi), BSTY_SURF);
    }
    y += devH + 14 * sc;

    // ---- app profiles (TZ4 2.4)
    auto& apps = V4AppProfiles();
    int rowH = 40 * sc;
    int appH = (60 + (int)apps.size() * rowH) * sc;
    RECT c3 = { m, y - scro, m + W, y - scro + appH };
    Card(dc, c3, T(S4_COM_APP), &cy, &sc);
    Txt(dc, { c3.left + 16 * sc, cy, c3.right - 16 * sc, cy + 22 * sc },
        T(S4_COM_APPSUB), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 26 * sc;
    for (int i = 0; i < (int)apps.size(); ++i) {
        auto& a = apps[i];
        RECT row = { c3.left + 10 * sc, cy - 4 * sc, c3.right - 10 * sc, cy + rowH - 8 * sc };
        if (i % 2 == 0) FillRound(dc, row, CL_SURF, 8);
        Txt(dc, { c3.left + 16 * sc, cy, c3.left + 300 * sc, cy + 20 * sc },
            a.title, Fnt(F_SMALL), CL_TEXT, DT_LEFT);
        Txt(dc, { c3.left + 16 * sc, cy + 18 * sc, c3.left + 380 * sc, cy + 36 * sc },
            a.package, Fnt(F_SMALL), CL_SUB, DT_LEFT);
        Txt(dc, { c3.left + 390 * sc, cy, c3.right - 190 * sc, cy + 34 * sc },
            Fmt(L"%dx%d · %ddpi · %dfps · %s", a.w, a.h, a.dpi, a.fps, a.gpu.c_str()),
            Fnt(F_SMALL), CL_SUB, DT_LEFT);
        RECT ba = { c3.right - 176 * sc, cy - 2 * sc, c3.right - 16 * sc, cy + 28 * sc };
        Btn(dc, ID4_APP0 + i * 2, ba, T(S4_COM_APPLY), BSTY_GHOST);
        cy += rowH;
    }
    y += appH + 14 * sc;

    // warn card (principles)
    int wh = 66 * sc;
    RECT c4 = { m, y - scro, m + W, y - scro + wh };
    FillRound(dc, c4, CL_SURF, 12);
    FrameRound(dc, c4, CL_BORDER, 12);
    Txt(dc, { c4.left + 16 * sc, y - scro + 12 * sc, c4.right - 16 * sc, y - scro + wh - 12 * sc },
        T(S4_COM_WARN), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    y += wh;
}

// ================================================================ Page: Sensors & Network (TZ4 2.2/2.3)

void PageSensors(HDC dc, RECT rc) {
    int sc = g_dpi / 96;
    int m = MulDiv(28, g_dpi, 96), y = m;
    PageHeader(dc, T(S4_SENS_TITLE), T(S4_SENS_SUB), rc, &y);
    int& scro = g_scroll[(int)Pg::Sensors];
    int W = rc.right - rc.left - m * 2;
    std::wstring sel = SelInst();
    bool running = false;
    { Runtime* r = Rt(sel); running = r && r->hProc && ProcAlive(r->hProc) && r->adbOk; }

    // ---- network (TZ4 2.2)
    int nh = 240 * sc;
    RECT c1 = { m, y - scro, m + W, y - scro + nh };
    int cy = 0; Card(dc, c1, T(S4_NET_TITLE), &cy, &sc);
    Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 34 * sc },
        T(S4_NET_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 40 * sc;
    // 10.0.2.x model pills
    const wchar_t* modelRu[] = { L"Шлюз 10.0.2.1", L"Host 10.0.2.2", L"DNS 10.0.2.3" };
    const wchar_t* modelEn[] = { L"Gateway 10.0.2.1", L"Host 10.0.2.2", L"DNS 10.0.2.3" };
    for (int i = 0; i < 3; ++i) {
        Pill(dc, { c1.left + 16 * sc + i * 150 * sc, cy,
                   c1.left + 150 * sc + i * 150 * sc, cy + 26 * sc },
             g_lang == 0 ? modelRu[i] : modelEn[i], CL_SURF);
    }
    cy += 36 * sc;
    if (g_netDone) {
        StatusPill(dc, { c1.left + 16 * sc, cy, c1.left + 190 * sc, cy + 26 * sc },
                   g_net.gateway, L"10.0.2.1 OK", L"10.0.2.1 FAIL");
        StatusPill(dc, { c1.left + 200 * sc, cy, c1.left + 380 * sc, cy + 26 * sc },
                   g_net.dnsOk, L"DNS OK", L"DNS FAIL");
        StatusPill(dc, { c1.left + 390 * sc, cy, c1.left + 580 * sc, cy + 26 * sc },
                   g_net.internet, T(S4_NET_INETOK), T(S4_NET_INETFAIL));
        cy += 32 * sc;
    }
    Btn(dc, ID4_NET0 + 0, { c1.left + 16 * sc, cy, c1.left + 216 * sc, cy + 34 * sc },
        T(S4_NET_DIAG), BSTY_PRIMARY, running);
    Btn(dc, ID4_NET0 + 1, { c1.left + 226 * sc, cy, c1.left + 426 * sc, cy + 34 * sc },
        T(S4_NET_FIXDNS), BSTY_SURF, running);
    Btn(dc, ID4_NET0 + 2, { c1.left + 436 * sc, cy, c1.left + 636 * sc, cy + 34 * sc },
        T(S4_NET_RESETDNS), BSTY_GHOST, running);
    y += nh + 14 * sc;

    // ---- GPS (TZ4 2.3)
    int gh = 190 * sc;
    RECT c2 = { m, y - scro, m + W, y - scro + gh };
    Card(dc, c2, T(S4_GPS_TITLE), &cy, &sc);
    Txt(dc, { c2.left + 16 * sc, cy, c2.right - 16 * sc, cy + 22 * sc },
        T(S4_GPS_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 30 * sc;
    const wchar_t* citiesRu[] = { L"Москва", L"Нью-Йорк", L"Лондон" };
    const wchar_t* citiesEn[] = { L"Moscow", L"New York", L"London" };
    for (int i = 0; i < 3; ++i)
        Btn(dc, ID4_GPS0 + 1 + i, { c2.left + 16 * sc + i * 150 * sc, cy,
                                   c2.left + 156 * sc + i * 150 * sc, cy + 34 * sc },
            g_lang == 0 ? citiesRu[i] : citiesEn[i], BSTY_SURF, running);
    RECT setb = { c2.right - 216 * sc, cy, c2.right - 16 * sc, cy + 34 * sc };
    Btn(dc, ID4_GPS0 + 0, setb, T(S4_GPS_SET), BSTY_PRIMARY, running);
    cy += 42 * sc;
    Txt(dc, { c2.left + 16 * sc, cy, c2.right - 16 * sc, cy + 22 * sc },
        T(S4_GPS_CUSTOM), Fnt(F_SMALL), CL_SUB, DT_LEFT);
    y += gh + 14 * sc;

    // ---- orientation / accelerometer (TZ4 2.3)
    int oh = 150 * sc;
    RECT c3 = { m, y - scro, m + W, y - scro + oh };
    Card(dc, c3, T(S4_ORI_TITLE), &cy, &sc);
    Txt(dc, { c3.left + 16 * sc, cy, c3.right - 16 * sc, cy + 22 * sc },
        T(S4_ORI_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 30 * sc;
    const wchar_t* oriRu[] = { L"Портрет", L"Альбом", L"Лёжа" };
    const wchar_t* oriEn[] = { L"Portrait", L"Landscape", L"Lying" };
    for (int i = 0; i < 3; ++i)
        Btn(dc, ID4_GPS0 + 4 + i, { c3.left + 16 * sc + i * 170 * sc, cy,
                                   c3.left + 176 * sc + i * 170 * sc, cy + 34 * sc },
            g_lang == 0 ? oriRu[i] : oriEn[i], BSTY_SURF, running);
    y += oh + 14 * sc;

    // ---- battery (TZ4 2.3)
    int bh2 = 170 * sc;
    RECT c4 = { m, y - scro, m + W, y - scro + bh2 };
    Card(dc, c4, T(S4_BAT_TITLE), &cy, &sc);
    Txt(dc, { c4.left + 16 * sc, cy, c4.right - 16 * sc, cy + 22 * sc },
        T(S4_BAT_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 30 * sc;
    Btn(dc, ID4_GPS0 + 7, { c4.left + 16 * sc, cy, c4.left + 216 * sc, cy + 34 * sc },
        T(S4_BAT_SET), BSTY_PRIMARY, running);
    Btn(dc, ID4_GPS0 + 8, { c4.left + 226 * sc, cy, c4.left + 426 * sc, cy + 34 * sc },
        T(S4_BAT_RESET), BSTY_GHOST, running);
    cy += 42 * sc;
    Txt(dc, { c4.left + 16 * sc, cy, c4.right - 16 * sc, cy + 22 * sc },
        T(S4_BAT_CUSTOM), Fnt(F_SMALL), CL_SUB, DT_LEFT);
    y += bh2 + 14 * sc;
}

// ================================================================ Page: Advanced (TZ5-7)
static std::vector<std::wstring> g_cloudFiles;

void PageAdvanced(HDC dc, RECT rc) {
    int sc = g_dpi / 96;
    int m = MulDiv(28, g_dpi, 96), y = m;
    PageHeader(dc, T(S4_ADV_TITLE), T(S4_ADV_SUB), rc, &y);
    int& scro = g_scroll[(int)Pg::Advanced];
    int W = rc.right - rc.left - m * 2;

    // ---- updates (TZ5 3.3)
    V4UpdateInfo u = V4UpdateState();
    int uh = 170 * sc;
    RECT c1 = { m, y - scro, m + W, y - scro + uh };
    int cy = 0; Card(dc, c1, T(S4_UPD_TITLE), &cy, &sc);
    Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
        Fmt(L"%s: %s · %s: %s", T(S4_UPD_CUR), L"0.4.0", T(S4_UPD_CHANNEL),
            g_set.updateChannel.c_str()), Fnt(F_SMALL), CL_SUB, DT_LEFT);
    cy += 28 * sc;
    if (u.checked && u.available)
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
            Fmt(T(S4_UPD_AVAIL), u.version.c_str()), Fnt(F_SMALL), CL_OK, DT_LEFT);
    else if (u.checked && !u.available && u.ok)
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
            T(S4_UPD_LATEST), Fnt(F_SMALL), CL_OK, DT_LEFT);
    else if (!u.error.empty())
        Txt(dc, { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 22 * sc },
            u.error, Fnt(F_SMALL), CL_ERR, DT_LEFT);
    cy += 28 * sc;
    Btn(dc, ID4_UPD0 + 0, { c1.left + 16 * sc, cy, c1.left + 216 * sc, cy + 34 * sc },
        T(S_UPD_CHECK), BSTY_PRIMARY, !u.busy);
    Btn(dc, ID4_UPD0 + 1, { c1.left + 226 * sc, cy, c1.left + 426 * sc, cy + 34 * sc },
        T(S4_UPD_INSTALL), BSTY_OK, u.checked && u.available && !u.busy);
    Btn(dc, ID4_UPD0 + 2, { c1.left + 436 * sc, cy, c1.left + 680 * sc, cy + 34 * sc },
        std::wstring(T(S4_UPD_CHANGETO)) + L": " + (g_set.updateChannel == L"stable" ? L"Beta" : L"Stable"), BSTY_GHOST);
    if (u.busy && u.progress > 0) {
        cy += 40 * sc;
        RECT bar = { c1.left + 16 * sc, cy, c1.right - 16 * sc, cy + 12 * sc };
        RECT fill = bar; fill.right = bar.left + LONG((bar.right - bar.left) * u.progress);
        GradientRect(dc, fill, Accent(), CL_ACCENT2);
        FrameRound(dc, bar, CL_BORDER, 6);
    }
    y += uh + 14 * sc;

    // ---- logs export (TZ5 3.4)
    int lh = 120 * sc;
    RECT c2 = { m, y - scro, m + W, y - scro + lh };
    Card(dc, c2, T(S4_LOG_TITLE), &cy, &sc);
    Txt(dc, { c2.left + 16 * sc, cy, c2.right - 16 * sc, cy + 34 * sc },
        T(S4_LOG_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 40 * sc;
    Btn(dc, ID4_LOG0 + 0, { c2.left + 16 * sc, cy, c2.left + 266 * sc, cy + 34 * sc },
        T(S4_LOG_EXPORT), BSTY_PRIMARY, !SelInst().empty());
    y += lh + 14 * sc;

    // ---- browser stream (TZ6 4.1)
    bool strRun = V4StreamRunning();
    int sh = 190 * sc;
    RECT c3 = { m, y - scro, m + W, y - scro + sh };
    Card(dc, c3, T(S4_STR_TITLE), &cy, &sc);
    Txt(dc, { c3.left + 16 * sc, cy, c3.right - 16 * sc, cy + 34 * sc },
        T(S4_STR_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 40 * sc;
    Btn(dc, ID4_STR0 + 0, { c3.left + 16 * sc, cy, c3.left + 266 * sc, cy + 34 * sc },
        strRun ? T(S4_STR_STOP) : T(S4_STR_START), strRun ? BSTY_DANGER : BSTY_PRIMARY);
    Btn(dc, ID4_STR0 + 1, { c3.left + 276 * sc, cy, c3.left + 526 * sc, cy + 34 * sc },
        T(S4_STR_OPEN), BSTY_SURF, strRun);
    if (strRun) {
        cy += 42 * sc;
        Txt(dc, { c3.left + 16 * sc, cy, c3.right - 16 * sc, cy + 22 * sc },
            V4StreamUrl() + L"  (" + T(S4_STR_LOCAL) + L")", Fnt(F_MONO), CL_ACCENT2, DT_LEFT);
    }
    y += sh + 14 * sc;

    // ---- plugins (TZ6 4.2)
    auto& plg = V4Plugins();
    int ph = (60 + (int)plg.size() * 44 + 40) * sc;
    RECT c4 = { m, y - scro, m + W, y - scro + ph };
    Card(dc, c4, T(S4_PLG_TITLE), &cy, &sc);
    Txt(dc, { c4.left + 16 * sc, cy, c4.right - 16 * sc, cy + 34 * sc },
        T(S4_PLG_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 38 * sc;
    if (plg.empty()) {
        Txt(dc, { c4.left + 16 * sc, cy, c4.right - 16 * sc, cy + 22 * sc },
            Fmt(T(S4_PLG_EMPTY), L"plugins\\<name>\\manifest.json"), Fnt(F_SMALL), CL_SUB, DT_LEFT);
        cy += 28 * sc;
    }
    for (int i = 0; i < (int)plg.size(); ++i) {
        auto& p = plg[i];
        Txt(dc, { c4.left + 16 * sc, cy, c4.left + 320 * sc, cy + 20 * sc },
            p.name + L" v" + p.version, Fnt(F_SMALL), CL_TEXT, DT_LEFT);
        Txt(dc, { c4.left + 16 * sc, cy + 18 * sc, c4.left + 420 * sc, cy + 36 * sc },
            p.valid ? p.desc : T(S4_PLG_INVALID), Fnt(F_SMALL), p.valid ? CL_SUB : CL_ERR, DT_LEFT);
        RECT tb = { c4.right - 176 * sc, cy - 2 * sc, c4.right - 16 * sc, cy + 28 * sc };
        Btn(dc, ID4_PLG0 + i * 2, tb, p.enabled ? T(S4_PLG_OFF) : T(S4_PLG_ON),
            p.enabled ? BSTY_SURF : BSTY_GHOST, p.valid);
        cy += 44 * sc;
    }
    Btn(dc, ID4_PLG0 + 20, { c4.left + 16 * sc, cy, c4.left + 216 * sc, cy + 32 * sc },
        T(S4_PLG_RESCAN), BSTY_GHOST);
    y += ph + 14 * sc;

    // ---- cloud backups (TZ6 4.3)
    int ch = 190 * sc;
    RECT c5 = { m, y - scro, m + W, y - scro + ch };
    Card(dc, c5, T(S4_CLD_TITLE), &cy, &sc);
    std::wstring prov = g_set.cloudProvider.empty() ? T(S4_CLD_NOTSET) : g_set.cloudProvider;
    Txt(dc, { c5.left + 16 * sc, cy, c5.right - 16 * sc, cy + 34 * sc },
        Fmt(L"%s: %s · %s", T(S4_CLD_PROV), prov.c_str(),
            g_set.cloudEncrypt ? T(S4_CLD_ENC) : T(S4_CLD_NOENC)),
        Fnt(F_SMALL), CL_SUB, DT_LEFT);
    cy += 40 * sc;
    Btn(dc, ID4_CLD0 + 0, { c5.left + 16 * sc, cy, c5.left + 216 * sc, cy + 34 * sc },
        T(S4_CLD_SETUP), BSTY_SURF);
    Btn(dc, ID4_CLD0 + 1, { c5.left + 226 * sc, cy, c5.left + 426 * sc, cy + 34 * sc },
        T(S_BTN_BACKUP), BSTY_PRIMARY, !g_set.cloudProvider.empty());
    Btn(dc, ID4_CLD0 + 2, { c5.left + 436 * sc, cy, c5.left + 636 * sc, cy + 34 * sc },
        T(S_BTN_RESTORE), BSTY_SURF, !g_set.cloudProvider.empty());
    y += ch + 14 * sc;

    // ---- macros (TZ7 5.4)
    auto& mac = V4Macros();
    int mh = (60 + (int)mac.size() * 44 + 46) * sc;
    RECT c6 = { m, y - scro, m + W, y - scro + mh };
    Card(dc, c6, T(S4_MAC_TITLE), &cy, &sc);
    Txt(dc, { c6.left + 16 * sc, cy, c6.right - 16 * sc, cy + 34 * sc },
        T(S4_MAC_WARN), Fnt(F_SMALL), CL_WARN, DT_LEFT | DT_WORDBREAK);
    cy += 40 * sc;
    if (mac.empty()) {
        Txt(dc, { c6.left + 16 * sc, cy, c6.right - 16 * sc, cy + 22 * sc },
            T(S4_MAC_EMPTY), Fnt(F_SMALL), CL_SUB, DT_LEFT);
        cy += 28 * sc;
    }
    for (int i = 0; i < (int)mac.size(); ++i) {
        auto& mm = mac[i];
        Txt(dc, { c6.left + 16 * sc, cy, c6.left + 360 * sc, cy + 20 * sc },
            mm.name, Fnt(F_SMALL), CL_TEXT, DT_LEFT);
        Txt(dc, { c6.left + 16 * sc, cy + 18 * sc, c6.left + 560 * sc, cy + 36 * sc },
            Fmt(L"%d %s · x%d", (int)mm.steps.size(), T(S4_MAC_STEPS), mm.repeat),
            Fnt(F_SMALL), CL_SUB, DT_LEFT);
        int bx = c6.right - 16 * sc;
        const int bw2[4] = { 70, 70, 80, 80 };
        const wchar_t* lbl[4] = { T(S4_MAC_EDIT), T(S4_MAC_PLAY), T(S4_MAC_STOP), T(S4_MAC_DEL) };
        for (int k = 3; k >= 0; --k) {
            RECT rb = { bx - bw2[k] * sc, cy - 2 * sc, bx, cy + 28 * sc };
            Btn(dc, ID4_MAC0 + i * 5 + k + 1, rb, lbl[k],
                k == 3 ? BSTY_DANGER : BSTY_GHOST,
                k == 1 ? true : true);
            bx -= (bw2[k] + 6) * sc;
        }
        cy += 44 * sc;
    }
    RECT rec = { c6.left + 16 * sc, cy, c6.left + 266 * sc, cy + 34 * sc };
    Btn(dc, ID4_MAC0 + 0, rec,
        V4MacroRecording() ? Fmt(L"%s (%d)", T(S4_MAC_STOPREC), V4MacroRecordStepCount())
                           : T(S4_MAC_REC),
        V4MacroRecording() ? BSTY_DANGER : BSTY_PRIMARY, !SelInst().empty());
    y += mh + 14 * sc;

    // ---- sync groups (TZ7 5.3)
    int syh = 170 * sc;
    RECT c7 = { m, y - scro, m + W, y - scro + syh };
    Card(dc, c7, T(S4_SYN_TITLE), &cy, &sc);
    Txt(dc, { c7.left + 16 * sc, cy, c7.right - 16 * sc, cy + 34 * sc },
        T(S4_SYN_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 38 * sc;
    std::wstring synInfo;
    auto& sg = V4SyncGroups();
    if (sg.empty()) synInfo = T(S4_SYN_NONE);
    else {
        for (auto& g : sg) {
            synInfo += Inst(g.masterId) ? Inst(g.masterId)->name : g.masterId;
            synInfo += L" -> ";
            for (auto& s : g.slaveIds) synInfo += (Inst(s) ? Inst(s)->name : s) + L" ";
            synInfo += L";  ";
        }
    }
    Txt(dc, { c7.left + 16 * sc, cy, c7.right - 16 * sc, cy + 40 * sc },
        synInfo, Fnt(F_SMALL), CL_TEXT, DT_LEFT | DT_WORDBREAK);
    cy += 44 * sc;
    Btn(dc, ID4_SYN0 + 0, { c7.left + 16 * sc, cy, c7.left + 316 * sc, cy + 34 * sc },
        T(S4_SYN_ADD), BSTY_SURF, !SelInst().empty());
    Btn(dc, ID4_SYN0 + 1, { c7.left + 326 * sc, cy, c7.left + 626 * sc, cy + 34 * sc },
        T(S4_SYN_REMOVE), BSTY_GHOST, !sg.empty());
    y += syh + 14 * sc;

    // ---- anti-lag (TZ7 5.2)
    int ah = 170 * sc;
    RECT c8 = { m, y - scro, m + W, y - scro + ah };
    Card(dc, c8, T(S4_ALG_TITLE), &cy, &sc);
    Txt(dc, { c8.left + 16 * sc, cy, c8.right - 16 * sc, cy + 22 * sc },
        T(S4_ALG_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 28 * sc;
    Btn(dc, ID4_ALG0 + 0, { c8.left + 16 * sc, cy, c8.left + 216 * sc, cy + 34 * sc },
        V4AntiLagOn() ? T(S4_ALG_ON) : T(S4_ALG_OFF), V4AntiLagOn() ? BSTY_OK : BSTY_SURF);
    cy += 42 * sc;
    const wchar_t* modesRu[] = { L"Производительность", L"Баланс", L"Качество", L"Экономный" };
    const wchar_t* modesEn[] = { L"Performance", L"Balance", L"Quality", L"Economy" };
    const wchar_t* modeIds[] = { L"perf", L"balance", L"quality", L"eco" };
    for (int i = 0; i < 4; ++i)
        Btn(dc, ID4_ALG0 + 1 + i, { c8.left + 16 * sc + i * 170 * sc, cy,
                                   c8.left + 176 * sc + i * 170 * sc, cy + 32 * sc },
            g_lang == 0 ? modesRu[i] : modesEn[i],
            V4AntiLagMode() == modeIds[i] ? BSTY_PRIMARY : BSTY_GHOST);
    y += ah + 14 * sc;

    // ---- integrations: Discord (TZ7 5.5)
    int dh = 130 * sc;
    RECT c9 = { m, y - scro, m + W, y - scro + dh };
    Card(dc, c9, T(S4_DSC_TITLE), &cy, &sc);
    Txt(dc, { c9.left + 16 * sc, cy, c9.right - 16 * sc, cy + 34 * sc },
        T(S4_DSC_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    cy += 40 * sc;
    Btn(dc, ID4_DSC0 + 0, { c9.left + 16 * sc, cy, c9.left + 266 * sc, cy + 34 * sc },
        g_set.discordEnabled ? T(S4_DSC_OFF) : T(S4_DSC_ON),
        g_set.discordEnabled ? BSTY_OK : BSTY_SURF);
    y += dh + 14 * sc;

    // ---- Pro (TZ7 5.8)
    int prh = 150 * sc;
    RECT ca = { m, y - scro, m + W, y - scro + prh };
    FillRound(dc, ca, CL_SURF, 12);
    FrameRound(dc, ca, Accent(), 12, 2);
    Txt(dc, { ca.left + 16 * sc, ca.top + 12 * sc, ca.right - 16 * sc, ca.top + 40 * sc },
        L"NovaDroid Pro", Fnt(F_H2), CL_ACCENT2, DT_LEFT);
    Txt(dc, { ca.left + 16 * sc, ca.top + 44 * sc, ca.right - 16 * sc, ca.top + 92 * sc },
        T(S4_PRO_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    Btn(dc, ID4_PRO0 + 0, { ca.left + 16 * sc, ca.top + 96 * sc, ca.left + 266 * sc, ca.top + 130 * sc },
        g_set.proKey.empty() ? T(S4_PRO_ACT) : T(S4_PRO_ACTIVE), BSTY_PRIMARY);
}
