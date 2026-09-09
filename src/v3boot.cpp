// ============================================================================
//  NovaDroid - v3boot.cpp  Stage 3: full-package bootstrap (user requirement).
//  On first launch the launcher downloads the FULL package (QEMU + ADB +
//  Android x86_64 image + docs) from baked DIRECT vikingfile links,
//  verifies SHA-256 of every file, extracts via bsdtar and only then
//  allows the emulator to start. Offline ZIP path is also supported.
// ============================================================================
#include "v3.h"
#include <winhttp.h>

static JValue ParseJ(const std::wstring& text) {
    JValue v; std::wstring err;
    JsonParse(text, v, err);
    return v;
}

// ---------------------------------------------------------------- baked links
// NOTE: primary entry is filled with the direct /d/ link + SHA-256 + size of
// NovaDroid-0.3-GamingCore-Package.zip right after the upload (build step).
// The 0.2 Product Edition package (same layout: qemu/, adb/, images/, docs/)
// is kept as the fallback mirror.
// Filled at startup: downloads.json override (exeDir) or the baked list below.
static std::vector<BakedPkgUrl> g_activeUrls;
static bool g_urlsReady = false;

// Build-time constants for the primary 0.3 package source (filled by the
// release script; the baked defaults below are the live production values).
#ifndef PKG0_URL
#define PKG0_URL L"https://vikingfile.com/d/BpeVB2VoJd/NovaDroid-0.3-GamingCore-Package.zip"
#define PKG0_SIZE 1041410801LL
#define PKG0_SHA L"a9eda55f56e30e4c78efcbeb5ad527e78ae3380f6a18446487ab2e4b6151e397"
#endif

const BakedPkgUrl g_pkgUrls[] = {
    // [0] PRIMARY (active): permanent GitHub release asset (public repo ->
    // direct download, Range/resume). Whole-zip SHA-256 verified (strict).
    { PKG0_URL, PKG0_SIZE, PKG0_SHA },
    // [1] FALLBACK: vikingfile direct /d/ link of the 0.3 Gaming Core package
    { L"https://vikingfile.com/d/BpeVB2VoJd/NovaDroid-0.3-GamingCore-Package.zip",
      1041410801LL, L"" },
    // [2] FALLBACK: permanent vikingfile DIRECT link (user-provided) to the
    // 0.2 Product Edition package (same layout: qemu/, adb/, images/).
    { L"https://vikingfile.com/d/Wo92uuwHzN/NovaDroid-0.2-ProductEdition.zip",
      1057488473LL, L"" },
};
const int g_pkgUrlCount = (int)(sizeof(g_pkgUrls) / sizeof(g_pkgUrls[0]));
const wchar_t* g_pkgVersion = L"0.4.0";

// downloads.json next to the exe lets the owner plug in fresh direct links
// (e.g. a browser-generated vikingfile /d/ link) without rebuilding:
// { "urls": [ {"url":"https://...", "sizeBytes":1041410801, "sha256":"..."} ] }
static void InitActiveUrls() {
    if (g_urlsReady) return;
    g_urlsReady = true;
    g_activeUrls.assign(g_pkgUrls, g_pkgUrls + g_pkgUrlCount);
    std::wstring t;
    if (ReadText(g_p.exeDir + L"\\downloads.json", t)) {
        JValue j = ParseJ(t);
        auto* arr = j.find(L"urls");
        if (arr && arr->type == JValue::Arr && !arr->arr.empty()) {
            std::vector<BakedPkgUrl> list;
            for (auto& u : arr->arr) {
                auto* up = u.find(L"url");
                if (!up) continue;
                BakedPkgUrl b;
                static std::vector<std::wstring> keep;
                keep.push_back(up->asStr());
                b.url = keep.back().c_str();
                auto* sz = u.find(L"sizeBytes");
                long long szv = sz ? sz->asInt64(0) : 0;
                b.sizeBytes = szv;
                static std::vector<std::wstring> keepSha;
                keepSha.push_back(u.find(L"sha256") ? u.find(L"sha256")->asStr() : L"");
                b.sha256 = keepSha.back().c_str();
                if (FE(g_p.exeDir + L"\\downloads.json")) list.push_back(b);
            }
            if (!list.empty()) {
                g_activeUrls = list;
                LogW(L"pkg", L"downloads.json override active: %d urls", (int)list.size());
            }
        }
    }
}

// ---------------------------------------------------------------- state
static std::mutex g_pkgMx;
static PkgStateInfo g_pkg;
static std::atomic<bool> g_cancelFlg{ false };
static std::wstring g_markerVer;     // installed version from marker

PkgStateInfo PkgInfo() { std::lock_guard<std::mutex> lk(g_pkgMx); PkgStateInfo s = g_pkg; return s; }
static void PkgSet(int st, float pr, const std::wstring& txt) {
    {
        std::lock_guard<std::mutex> lk(g_pkgMx);
        g_pkg.state = st;
        if (st == 1) { g_pkg.progress = pr; g_pkg.stageText = txt; }
        else { g_pkg.stageText = txt; if (st == 0) g_pkg.progress = 0; }
        if (st >= 2) g_pkg.progress = 0;
    }
    if (g_wnd) PostMessageW(g_wnd, WM_APP_PKG, 0, 0);
}
static void PkgSetErr(const std::wstring& e) {
    {
        std::lock_guard<std::mutex> lk(g_pkgMx);
        g_pkg.state = 5; g_pkg.error = e; g_pkg.stageText = T(S3_PKG_ERROR);
    }
    if (g_wnd) PostMessageW(g_wnd, WM_APP_PKG, 0, 0);
}

// ---------------------------------------------------------------- paths
std::wstring PkgRoot() {
    // exe dir preferred (FindTool looks next to the exe)
    std::wstring probe = g_p.exeDir + L"\\.__nd_probe";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); DeleteFileW(probe.c_str()); return g_p.exeDir; }
    return g_p.dataRoot;
}

static std::wstring PkgMarkerPath() { return PkgRoot() + L"\\.novadroid-pkg.json"; }

void PkgLoadMarker() {
    std::wstring t;
    if (ReadText(PkgMarkerPath(), t)) {
        JValue j = ParseJ(t);
        if (auto* v = j.find(L"version")) g_markerVer = v->asStr();
    }
}
std::wstring PkgInstalledVersion() { return g_markerVer; }

bool PkgMissingCritical(std::vector<std::wstring>& missing) {
    std::wstring root = PkgRoot();
    missing.clear();
    if (!FE(root + L"\\qemu\\qemu-system-x86_64.exe")) missing.push_back(L"qemu\\qemu-system-x86_64.exe");
    if (!FE(root + L"\\adb\\adb.exe")) missing.push_back(L"adb\\adb.exe");
    bool anyIso = false;
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((root + L"\\images\\*.iso").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { anyIso = true; FindClose(h); }
        HANDLE h2 = FindFirstFileW((root + L"\\images\\*.qcow2").c_str(), &fd);
        if (h2 != INVALID_HANDLE_VALUE) { anyIso = true; FindClose(h2); }
    }
    if (!anyIso) missing.push_back(L"images\\<Android x86_64 image>");
    return !missing.empty();
}

bool PkgInstalled() {
    std::vector<std::wstring> m;
    if (PkgMissingCritical(m)) return false;
    if (g_markerVer.empty()) return false;
    return true;
}

bool PkgVerifyAll(std::wstring& firstBad, int* okCount, int* totalCount) {
    std::wstring root = PkgRoot();
    std::wstring t;
    if (!ReadText(root + L"\\package.json", t)) { firstBad = L"package.json missing"; return false; }
    JValue j = ParseJ(t);
    auto* files = j.find(L"files");
    int ok = 0, total = 0;
    if (files && files->type == JValue::Arr) {
        for (auto& f : files->arr) {
            auto* p = f.find(L"path"); auto* s = f.find(L"sha256");
            if (!p || !s) continue;
            total++;
            std::wstring fp = root + L"\\" + p->asStr();
            if (!FE(fp)) { if (firstBad.empty()) firstBad = p->asStr() + L" (missing)"; continue; }
            std::wstring h = Sha256OfFile(fp);
            if (!_wcsicmp(h.c_str(), s->asStr().c_str())) ok++;
            else if (firstBad.empty()) firstBad = p->asStr() + L" (sha256 mismatch)";
        }
    }
    if (okCount) *okCount = ok;
    if (totalCount) *totalCount = total;
    return total > 0 && ok == total;
}

// ---------------------------------------------------------------- download (WinHTTP)
static bool HttpDownload(const std::wstring& url, const std::wstring& dstPath,
                         std::wstring& err, std::wstring* shaOut) {
    URL_COMPONENTSW uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) {
        err = L"bad url"; return false;
    }
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"NovaDroid/0.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = L"WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(ses, 10000, 30000, 30000, 30000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (con) {
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
        if (req) {
            // resume support
            long long have = 0;
            HANDLE fh = CreateFileW(dstPath.c_str(), GENERIC_READ, FILE_SHARE_WRITE, nullptr,
                                    OPEN_ALWAYS, 0, nullptr);
            if (fh != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz;
                if (GetFileSizeEx(fh, &sz)) have = sz.QuadPart;
                CloseHandle(fh);
            }
            std::wstring hdrs;
            if (have > 0) hdrs = L"Range: bytes=" + std::to_wstring(have) + L"-";
            if (WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
                                   (DWORD)hdrs.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD st = 0, szSt = sizeof(st);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    nullptr, &st, &szSt, nullptr);
                long long total = -1;
                wchar_t cl[64] = {}; DWORD szCl = sizeof(cl);
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, cl, &szCl, nullptr))
                    total = _wtoi64(cl);
                if (st == 200 || st == 206) {
                    if (st == 200 && have > 0) have = 0;      // server ignored range
                    {
                        std::lock_guard<std::mutex> lk(g_pkgMx);
                        g_pkg.totalBytes = total;
                        g_pkg.gotBytes = have;
                    }
                    fh = CreateFileW(dstPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     OPEN_ALWAYS, 0, nullptr);
                    if (fh == INVALID_HANDLE_VALUE) { err = L"cannot open file"; }
                    else {
                        LARGE_INTEGER off; off.QuadPart = have;
                        SetFilePointerEx(fh, off, nullptr, FILE_BEGIN);
                        SetEndOfFile(fh);
                        char buf[262144];
                        DWORD rd = 0;
                        long long lastUi = 0;
                        bool io = true;
                        while (io && !g_cancelFlg.load()) {
                            if (!WinHttpReadData(req, buf, sizeof(buf), &rd)) { err = L"read failed"; io = false; break; }
                            if (rd == 0) break;
                            DWORD wr = 0;
                            if (!WriteFile(fh, buf, rd, &wr, nullptr) || wr != rd) { err = L"write failed"; io = false; break; }
                            long long got;
                            {
                                std::lock_guard<std::mutex> lk(g_pkgMx);
                                g_pkg.gotBytes += wr;
                                got = g_pkg.gotBytes;
                            }
                            long long now = GetTickCount64();
                            if (now - lastUi > 250) {
                                lastUi = now;
                                float pr = total > 0 ? (float)((double)got / (double)total) : 0;
                                PkgSet(1, pr, T(S3_PKG_DOWNLOAD));
                            }
                        }
                        CloseHandle(fh);
                        if (io) ok = true;
                    }
                } else {
                    err = Fmt(L"HTTP status %lu", (unsigned long)st);
                }
            } else {
                err = L"send/receive failed";
            }
            WinHttpCloseHandle(req);
        } else err = L"open request failed";
        WinHttpCloseHandle(con);
    } else err = L"connect failed";
    WinHttpCloseHandle(ses);
    return ok;
}

// ---------------------------------------------------------------- extraction
static bool ExtractPackage(const std::wstring& zip, const std::wstring& root, std::wstring& err) {
    std::wstring tar = L"C:\\Windows\\System32\\tar.exe";
    if (!FE(tar)) { err = L"tar.exe not found"; return false; }
    MK(root);
    std::wstring args = L"-xf " + Qn(zip) + L" -C " + Qn(root) +
                        L" --exclude=NovaDroidLauncher.exe --exclude=NovaDroid-Launcher.exe";
    DWORD ec = 0; std::string so, se;
    PkgSet(2, 0, T(S3_PKG_EXTRACT));
    RunCapture(tar, args, root, 15 * 60000, &ec, &so, &se);
    // sanity: at least qemu exe must appear
    if (!FE(root + L"\\qemu\\qemu-system-x86_64.exe")) {
        err = std::wstring(L"extraction failed: qemu-system-x86_64.exe not found")
              + (se.size() ? (L" (" + U2W(se) + L")") : L"");
        return false;
    }
    return true;
}

static bool FinalizeInstall(const std::wstring& root, bool strictSha, std::wstring& err) {
    // verify (TZ: "всё должно скачиваться и проверяться программой")
    PkgSet(3, 0, T(S3_PKG_VERIFY));
    std::wstring bad; int okC = 0, totC = 0;
    if (!PkgVerifyAll(bad, &okC, &totC)) {
        if (strictSha || totC > 0) {
            err = std::wstring(T(S3_PKG_BADSHA)) + L" " + bad;
            return false;
        }
    }
    // write marker
    std::wstring ver = g_pkgVersion;
    {
        std::wstring t;
        if (ReadText(root + L"\\package.json", t)) {
            JValue j = ParseJ(t);
            if (auto* v = j.find(L"version")) ver = v->asStr();
        }
    }
    std::wstring marker = Fmt(L"{\"version\":\"%s\",\"installedAt\":\"%s\",\"root\":\"%s\",\"verified\":true}",
                              ver.c_str(), NowIso().c_str(), root.c_str());
    if (!WriteText(root + L"\\.novadroid-pkg.json", marker)) {
        err = L"cannot write package marker";
        return false;
    }
    g_markerVer = ver;
    // default image: register first local iso if user has none
    auto imgs = FindLocalImages();
    if (!imgs.empty() && g_set.defaultImagePath.empty()) {
        std::lock_guard<std::mutex> lk(g_mx);
        g_set.defaultImagePath = imgs[0];
        SaveSettings();
    }
    PkgSet(4, 1, T(S3_PKG_DONE));
    return true;
}

// ---------------------------------------------------------------- async pipeline
static void PkgPipeline(size_t urlIdx) {
    std::wstring root = PkgRoot();
    std::wstring cache = g_p.dataRoot + L"\\cache";
    MK(cache);
    std::wstring url = g_activeUrls[urlIdx].url;
    if (url.empty()) { LogW(L"pkg", L"mirror %d: empty url, skipped", (int)urlIdx); return; }
    std::wstring name = BaseName(url);
    std::wstring dst = cache + L"\\" + name;
    {
        std::lock_guard<std::mutex> lk(g_pkgMx);
        g_pkg.error.clear();
        g_pkg.activeUrl = url;
    }
    PkgSet(1, 0, T(S3_PKG_DOWNLOAD));
    LogW(L"pkg", L"downloading package from %s", url.c_str());

    std::wstring bakedSha = g_activeUrls[urlIdx].sha256 ? g_activeUrls[urlIdx].sha256 : L"";
    bool strict = !bakedSha.empty();
    for (int attempt = 1; attempt <= 3 && !g_cancelFlg.load(); ++attempt) {
        std::wstring err;
        if (HttpDownload(url, dst, err, nullptr)) break;
        if (g_cancelFlg.load()) { PkgSet(0, 0, L""); return; }
        LogW(L"pkg", L"download attempt %d failed: %s", attempt, err.c_str());
        if (attempt == 3) { PkgSetErr(std::wstring(T(S3_PKG_NET)) + L" (" + err + L")"); return; }
        SleepMs(2000);
        if (g_activeUrls[urlIdx].sizeBytes > 0) {
            // hard size mismatch -> restart clean
            if (FileSizeOf(dst) > g_activeUrls[urlIdx].sizeBytes) DeleteFileW(dst.c_str());
        }
    }
    if (g_cancelFlg.load()) { PkgSet(0, 0, L""); return; }

    // baked sha check of the whole zip (primary link only)
    if (strict) {
        PkgSet(3, 0, T(S3_PKG_VERIFY));
        std::wstring h = Sha256OfFile(dst);
        if (_wcsicmp(h.c_str(), bakedSha.c_str()) != 0) {
            PkgSetErr(std::wstring(T(S3_PKG_BADSHA)) + L" (zip)");
            return;
        }
    } else if (g_activeUrls[urlIdx].sizeBytes > 0) {
        if (FileSizeOf(dst) != g_activeUrls[urlIdx].sizeBytes) {
            PkgSetErr(T(S3_PKG_BADSIZE));
            return;
        }
    }

    std::wstring err;
    if (!ExtractPackage(dst, root, err)) { PkgSetErr(err); return; }
    if (!FinalizeInstall(root, strict, err)) { PkgSetErr(err); return; }
    LogW(L"pkg", L"package installed, version %s", g_markerVer.c_str());
    // keep the zip cached for offline re-installs
}

bool PkgStartDownloadAsync() {
    {
        std::lock_guard<std::mutex> lk(g_pkgMx);
        if (g_pkg.state == 1 || g_pkg.state == 2 || g_pkg.state == 3) return false;
    }
    InitActiveUrls();
    g_cancelFlg.store(false);
    std::thread([]() {
        for (size_t i = 0; i < g_activeUrls.size(); ++i) {
            PkgPipeline(i);
            if (PkgInfo().state == 4) return;               // success
            if (g_cancelFlg.load()) return;
            // else: try next mirror
        }
    }).detach();
    return true;
}

bool PkgUseOfflineZip(const std::wstring& zipPath, std::wstring& err) {
    if (!FE(zipPath)) { err = L"file not found"; return false; }
    if (FileSizeOf(zipPath) < 100 * 1024 * 1024) { err = T(S3_PKG_SMALL); return false; }
    std::thread([zipPath]() {
        std::wstring root = PkgRoot();
        std::wstring e2;
        if (!ExtractPackage(zipPath, root, e2)) { PkgSetErr(e2); return; }
        if (!FinalizeInstall(root, false, e2)) { PkgSetErr(e2); return; }
    }).detach();
    return true;
}

void PkgCancel() { g_cancelFlg.store(true); }
