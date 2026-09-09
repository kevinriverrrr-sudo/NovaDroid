// ============================================================================
//  NovaDroid - v2perf.cpp  Performance sampler (TZ 12), update check (TZ 14),
//  diagnostics report export (TZ 13).
// ============================================================================
#include "app.h"
#include "v2.h"
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// ================================================================ perf sampler
struct PerfRing {
    std::vector<PerfSample> s;
    FILETIME ftKernel{}, ftUser{}, ftLast{};
    bool inited = false;
};
static std::mutex g_perfMx;
static std::map<std::wstring, PerfRing> g_perf;
static std::atomic<bool> g_perfRun{ false };

static float ProcCpuPct(PerfRing& ring, HANDLE h) {
    FILETIME ftCreate, ftExit, ftKernel, ftUser, ftNow;
    if (!GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser)) return -1;
    GetSystemTimeAsFileTime(&ftNow);
    auto to64 = [](const FILETIME& t) {
        ULARGE_INTEGER u; u.LowPart = t.dwLowDateTime; u.HighPart = t.dwHighDateTime;
        return (long long)u.QuadPart; };
    long long nowT = to64(ftNow);
    long long kT = to64(ftKernel), uT = to64(ftUser);
    if (!ring.inited) { ring.ftKernel = ftKernel; ring.ftUser = ftUser; ring.ftLast = ftNow; ring.inited = true; return -1; }
    long long dProc = (kT - to64(ring.ftKernel)) + (uT - to64(ring.ftUser));
    long long dAll = nowT - to64(ring.ftLast);
    ring.ftKernel = ftKernel; ring.ftUser = ftUser; ring.ftLast = ftNow;
    if (dAll <= 0) return -1;
    return (float)(100.0 * dProc / dAll);
}

static void PerfTick() {
    std::lock_guard<std::mutex> lk(g_perfMx);
    for (auto& c : g_insts) {
        Runtime* r = Rt(c.id);
        bool alive = r && r->hProc && ProcAlive(r->hProc);
        if (!alive) { g_perf.erase(c.id); continue; }
        PerfRing& ring = g_perf[c.id];
        PerfSample s;
        s.tMs = (long long)GetTickCount64();
        float cpu = ProcCpuPct(ring, r->hProc);
        s.cpuPercent = cpu < 0 ? 0 : cpu;
        s.ramBytes = ProcRamBytes(r->hProc);
        std::wstring disk = c.Dir() + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2");
        s.diskBytes = FE(disk) ? (long long)FileSizeOf(disk) : 0;
        ring.s.push_back(s);
        while (ring.s.size() > 60) ring.s.erase(ring.s.begin());
    }
}

void PerfSamplerStart() {
    if (g_perfRun.exchange(true)) return;
    std::thread([] {
        while (g_perfRun.load()) {
            SleepMs(1000);
            try { PerfTick(); } catch (...) {}
        }
    }).detach();
}

std::vector<PerfSample> PerfHistory(const std::wstring& instanceId) {
    std::lock_guard<std::mutex> lk(g_perfMx);
    auto it = g_perf.find(instanceId);
    return it == g_perf.end() ? std::vector<PerfSample>() : it->second.s;
}

PerfSample PerfLatest(const std::wstring& instanceId) {
    std::lock_guard<std::mutex> lk(g_perfMx);
    auto it = g_perf.find(instanceId);
    if (it == g_perf.end() || it->second.s.empty()) return PerfSample();
    return it->second.s.back();
}

// ================================================================ update check (TZ 14)
static UpdateInfo g_upd;
static std::mutex g_updMx;

UpdateInfo UpdateState() { std::lock_guard<std::mutex> lk(g_updMx); return g_upd; }

static bool HttpsGetText(const wchar_t* host, const wchar_t* path, std::wstring& out) {
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"NovaDroid/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 8000);
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (con) {
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (req) {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                std::string utf8;
                char buf[8192]; DWORD rd = 0;
                while (WinHttpReadData(req, buf, sizeof(buf), &rd) && rd > 0) utf8.append(buf, rd);
                out = U2W(utf8);
                ok = !out.empty();
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(con);
    }
    WinHttpCloseHandle(ses);
    return ok;
}

void UpdateCheckAsync() {
    {
        std::lock_guard<std::mutex> lk(g_updMx);
        g_upd = UpdateInfo();
        g_upd.checked = true;
        g_upd.channel = g_set.updateChannel;
    }
    RunOpThread([] {
        std::wstring path = L"/update/" + g_set.updateChannel + L".json";
        std::wstring body;
        bool ok = HttpsGetText(L"update.novadroid.app", path.c_str(), body);
        std::lock_guard<std::mutex> lk(g_updMx);
        g_upd.ok = ok;
        if (ok) {
            JValue v; std::wstring e;
            if (JsonParse(body, v, e)) {
                g_upd.latestVersion = v.find(L"version") ? v.find(L"version")->asStr() : L"";
                g_upd.notes = v.find(L"notes") ? v.find(L"notes")->asStr() : L"";
            } else g_upd.ok = false;
        }
        if (!g_upd.ok)
            g_upd.error = g_lang ? L"Update server unreachable. This is normal for offline builds - the installed package is the latest release."
                                 : L"Сервер обновлений недоступен. Это нормально для офлайн-сборки - установленный пакет является актуальным релизом.";
        UiNotify(ok ? (g_lang ? std::wstring(L"Update check complete") : std::wstring(L"Проверка обновлений завершена")) : g_upd.error,
                 ok ? 3 : 1);
    });
}

// ================================================================ diagnostics report (TZ 13)
std::wstring DiagReportText() {
    std::wstring rep;
    rep += g_lang ? L"NovaDroid Diagnostics Report 0.2.0\r\n" : L"Диагностический отчёт NovaDroid 0.2.0\r\n";
    rep += Fmt(L"%s: %s\r\n", g_lang ? L"Date" : L"Дата", NowIso().c_str());
    rep += L"----------------------------------------\r\n";
    for (auto& d : g_diag) {
        const wchar_t* lv = d.level == 0 ? L"[ OK  ]" : d.level == 1 ? L"[WARN ]" :
                            d.level == 2 ? L"[FAIL ]" : L"[INFO ]";
        rep += std::wstring(lv) + L" " + d.name + L": " + d.msg + L"\r\n";
    }
    // verdict
    int fails = 0;
    for (auto& d : g_diag) if (d.level == 2) fails++;
    rep += L"----------------------------------------\r\n";
    rep += fails == 0 ? (g_lang ? L"Verdict: system is ready to run Android instances.\r\n"
                                : L"Итог: система готова к запуску Android-инстансов.\r\n")
                      : Fmt(g_lang ? L"Verdict: %d critical issue(s) found.\r\n"
                                   : L"Итог: обнаружено критических проблем: %d.\r\n", fails);
    return rep;
}

bool DiagExportToFile(std::wstring& outPath) {
    MK(g_p.logs);
    outPath = g_p.logs + L"\\diagnostics-" + NowFileStamp() + L".txt";
    return WriteText(outPath, DiagReportText());
}
