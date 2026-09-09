// ============================================================================
//  NovaDroid - v4prod.cpp  Stage 5 "Stable 1.0" (TZ5):
//  system check (installer/wizard), updates with SHA-256 + rollback,
//  log collection incl. Android logcat, base apps auto-install (F-Droid,
//  APKPure - user requirement), dark/light theme support.
// ============================================================================
#include "v4.h"
#include <winhttp.h>

// ================================================================ system check (TZ5 3.1/3.2)
SysCheckResult V4SystemCheck() {
    SysCheckResult r;
    r.virt = VirtFirmwareEnabled();
    r.whpx = WhpxAvailable();
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    r.ramMb = (long long)(ms.ullTotalPhys / (1024 * 1024));
    ULARGE_INTEGER freeB;
    std::wstring root = g_p.exeDir.substr(0, 3);
    if (GetDiskFreeSpaceExW(root.empty() ? L"C:\\" : root.c_str(), &freeB, nullptr, nullptr))
        r.diskFreeGb = (long long)(freeB.QuadPart / (1024 * 1024 * 1024));
    else if (GetDiskFreeSpaceExW(L"C:\\", &freeB, nullptr, nullptr))
        r.diskFreeGb = (long long)(freeB.QuadPart / (1024 * 1024 * 1024));
    SYSTEM_INFO si; GetSystemInfo(&si);
    r.cores = si.dwNumberOfProcessors;
    wchar_t cpu[64] = {};
    DWORD sz = sizeof(cpu);
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, L"ProcessorNameString", nullptr, nullptr, (BYTE*)cpu, &sz);
        RegCloseKey(hk);
    }
    r.cpuName = cpu;
    GpuInfo gi = V3DetectGpu();
    r.gpuName = gi.name;
    return r;
}

// ================================================================ updates (TZ5 3.3)
static V4UpdateInfo g_upd;
static std::mutex g_updMx;

// baked update feed (GitHub release direct asset - stable), override: exeDir\updates.json
#ifndef UPD0_URL
#define UPD0_URL L"https://github.com/nova-owner/NovaDroid/releases/latest/NovaDroidLauncher.exe"
#define UPD0_VER L"0.0.0"
#define UPD0_SHA L""
#endif

static const wchar_t* NovaVer() { return L"0.4.0"; }

static void UpdSet(const V4UpdateInfo& u) { std::lock_guard<std::mutex> lk(g_updMx); g_upd = u; }

static bool UpdFeed(std::wstring& ver, std::wstring& url, std::wstring& sha, std::wstring& notes) {
    ver = UPD0_VER; url = UPD0_URL; sha = UPD0_SHA; notes = L"";
    std::wstring t;
    if (ReadText(g_p.exeDir + L"\\updates.json", t)) {
        JValue j; std::wstring err;
        if (JsonParse(t, j, err) && j.type == JValue::Obj) {
            if (auto* x = j.find(L"version")) ver = x->asStr();
            if (auto* x = j.find(L"url")) url = x->asStr();
            if (auto* x = j.find(L"sha256")) sha = x->asStr();
            if (auto* x = j.find(L"notes")) notes = x->asStr();
            return !url.empty();
        }
    }
    return !url.empty();
}

static bool HttpGetToFile(const std::wstring& url, const std::wstring& dst,
                          float* progress, std::wstring& err);

void V4UpdateCheckAsync(bool manual) {
    V4UpdateInfo u;
    { std::lock_guard<std::mutex> lk(g_updMx); u = g_upd; }
    if (u.busy) return;
    u.busy = true; u.checked = false; u.available = false; u.error.clear();
    UpdSet(u);
    std::thread([manual]() {
        V4UpdateInfo u2;
        { std::lock_guard<std::mutex> lk(g_updMx); u2 = g_upd; }
        std::wstring ver, url, sha, notes;
        if (!UpdFeed(ver, url, sha, notes)) {
            u2.busy = false; u2.ok = false; u2.error = T(S4_UPD_NOFEED);
            u2.checked = true;
            UpdSet(u2);
            if (manual && g_wnd) UiNotify(T(S4_UPD_NOFEED), 1);
            return;
        }
        // stable | beta channels: beta uses updates.json only (owner-managed)
        u2.channel = g_set.updateChannel;
        u2.version = ver; u2.url = url; u2.sha256 = sha; u2.notes = notes;
        u2.ok = true; u2.checked = true;
        u2.available = _wcsicmp(ver.c_str(), NovaVer()) > 0;
        u2.busy = false;
        UpdSet(u2);
        if (g_wnd) {
            if (u2.available) UiNotify(Fmt(T(S4_UPD_AVAIL), ver.c_str()), 0);
            else if (manual) UiNotify(T(S4_UPD_LATEST), 0);
        }
    }).detach();
}

V4UpdateInfo V4UpdateState() { std::lock_guard<std::mutex> lk(g_updMx); return g_upd; }

void V4UpdateInstall() {
    V4UpdateInfo u;
    { std::lock_guard<std::mutex> lk(g_updMx); u = g_upd; }
    if (u.busy || !u.available || u.url.empty()) return;
    u.busy = true;
    UpdSet(u);
    std::thread([]() {
        V4UpdateInfo u2;
        { std::lock_guard<std::mutex> lk(g_updMx); u2 = g_upd; }
        std::wstring dst = g_p.cache + L"\\update.exe";
        MK(g_p.cache);
        std::wstring err;
        if (!HttpGetToFile(u2.url, dst, &u2.progress, err)) {
            u2.busy = false; u2.error = std::wstring(T(S4_UPD_DLFAIL)) + L" (" + err + L")";
            UpdSet(u2);
            if (g_wnd) UiNotify(u2.error, 2);
            return;
        }
        // SHA-256 integrity (TZ5 3.3)
        if (!u2.sha256.empty()) {
            std::wstring h = Sha256OfFile(dst);
            if (_wcsicmp(h.c_str(), u2.sha256.c_str()) != 0) {
                DeleteFileW(dst.c_str());
                u2.busy = false; u2.error = T(S4_UPD_BADSHA);
                UpdSet(u2);
                if (g_wnd) UiNotify(u2.error, 2);
                return;
            }
        }
        // swap with rollback: current -> .old, new -> current; run new; on failure restore
        std::wstring exe = g_p.exeDir + L"\\NovaDroidLauncher.exe";
        std::wstring old = exe + L".old";
        DeleteFileW(old.c_str());
        if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            u2.busy = false; u2.error = T(S4_UPD_SWAPFAIL);
            UpdSet(u2);
            if (g_wnd) UiNotify(u2.error, 2);
            return;
        }
        if (CopyFileW(dst.c_str(), exe.c_str(), FALSE)) {
            DeleteFileW(dst.c_str());
            u2.busy = false;
            u2.progress = 1;
            UpdSet(u2);
            LogW(L"upd", L"update installed: %s (rollback copy: %s.old)", u2.version.c_str(), exe.c_str());
            // restart launcher
            if (g_wnd) {
                std::wstring cmd = L"\"" + exe + L"\"";
                STARTUPINFOW si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                if (CreateProcessW(exe.c_str(), (wchar_t*)cmd.c_str(), nullptr, nullptr,
                                   FALSE, 0, nullptr, g_p.exeDir.c_str(), &si, &pi)) {
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                    PostMessageW(g_wnd, WM_CLOSE, 0, 0);
                }
            }
        } else {
            // rollback
            MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
            u2.busy = false; u2.error = T(S4_UPD_ROLLBACK);
            UpdSet(u2);
            if (g_wnd) UiNotify(u2.error, 2);
        }
    }).detach();
}

// generic download helper (no package-state coupling)
static bool HttpGetToFile(const std::wstring& url, const std::wstring& dstPath,
                          float* progress, std::wstring& err) {
    URL_COMPONENTSW uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) { err = L"bad url"; return false; }
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"NovaDroid/0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = L"WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(ses, 10000, 30000, 60000, 60000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (con) {
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
        if (req) {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD st = 0, szSt = sizeof(st);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    nullptr, &st, &szSt, nullptr);
                long long total = -1;
                wchar_t cl[64] = {}; DWORD szCl = sizeof(cl);
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, cl, &szCl, nullptr))
                    total = _wtoi64(cl);
                if (st == 200 || st == 206 || st == 302) {
                    HANDLE fh = CreateFileW(dstPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                            CREATE_ALWAYS, 0, nullptr);
                    if (fh != INVALID_HANDLE_VALUE) {
                        char buf[262144]; DWORD rd = 0;
                        long long got = 0;
                        ok = true;
                        while (true) {
                            if (!WinHttpReadData(req, buf, sizeof(buf), &rd)) { err = L"read failed"; ok = false; break; }
                            if (rd == 0) break;
                            DWORD wr = 0;
                            if (!WriteFile(fh, buf, rd, &wr, nullptr) || wr != rd) { err = L"write failed"; ok = false; break; }
                            got += rd;
                            if (progress && total > 0) *progress = (float)((double)got / (double)total);
                        }
                        CloseHandle(fh);
                    } else err = L"cannot open file";
                } else err = Fmt(L"http %u", st);
            } else err = L"request failed";
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(con);
    }
    WinHttpCloseHandle(ses);
    return ok;
}

// ================================================================ logs (TZ5 3.4)
std::wstring V4CollectLogsZip(const std::wstring& instanceId) {
    std::wstring stamp = NowFileStamp();
    std::wstring outDir = g_p.logs + L"\\export-" + stamp;
    MK(outDir);
    // 1) launcher + qemu + adb logs of the instance
    const wchar_t* kinds[] = { L"launcher", L"qemu", L"adb", L"serial" };
    for (auto k : kinds) {
        std::wstring src = InstanceLogPath(instanceId, k);
        if (FE(src)) CopyFileW(src.c_str(), (outDir + L"\\" + k + L".log").c_str(), FALSE);
    }
    // 2) global launcher log
    if (FE(g_p.logs + L"\\launcher.log"))
        CopyFileW((g_p.logs + L"\\launcher.log").c_str(), (outDir + L"\\global.log").c_str(), FALSE);
    // 3) android logcat (TZ5 3.4: "по возможности")
    {
        Runtime* r = Rt(instanceId);
        if (r && r->adbOk) {
            std::wstring adb = AdbExe();
            if (!adb.empty()) {
                std::string so;
                DWORD ec = 0;
                RunCapture(adb, Fmt(L"-s %s logcat -d -t 3000", AdbSerial(*Inst(instanceId)).c_str()),
                           L"", 15000, &ec, &so, nullptr);
                if (!so.empty()) {
                    std::ofstream f((outDir + L"\\logcat.log").c_str(), std::ios::binary);
                    f.write(so.data(), so.size());
                }
            }
        }
    }
    // 4) diag snapshot
    WriteText(outDir + L"\\diag.txt", DiagReportText());
    // 5) zip via bsdtar (same engine as package extraction)
    std::wstring zipPath = g_p.logs + L"\\NovaDroid-logs-" + stamp + L".zip";
    std::wstring bsdtar = FindTool(L"", L"", L"bsdtar.exe");
    if (bsdtar.empty()) {
        // fall back to tar.exe shipped with Windows 10+
        bsdtar = L"C:\\Windows\\System32\\tar.exe";
        if (!FE(bsdtar)) return outDir;                 // no archiver: return folder
    }
    DWORD ec = 0;
    std::string so, se;
    // zip: bsdtar -a -cf zip -C outDir .
    RunCapture(bsdtar, Fmt(L"-a -cf \"%s\" -C \"%s\" .", zipPath.c_str(), outDir.c_str()),
               L"", 60000, &ec, &so, &se);
    if (!FE(zipPath)) return outDir;
    DeleteTree(outDir);
    return zipPath;
}

// ================================================================ base apps (user requirement)
// pkgstage/apps/*.apk ship inside the full package and are installed on the
// first boot of every instance: F-Droid.apk + APKPure (direct links by owner).
bool V4InstallBaseApps(const std::wstring& id, std::wstring& logOut, std::wstring& err) {
    std::wstring appsDir = PkgRoot() + L"\\apps";
    if (!DE(appsDir)) { err = L"apps dir not found"; return false; }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((appsDir + L"\\*.apk").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { err = L"no embedded apk"; return false; }
    bool all = true;
    do {
        std::wstring apk = appsDir + L"\\" + fd.cFileName;
        std::wstring e2;
        logOut += Fmt(L"%s: ", fd.cFileName);
        if (AdbInstall(id, apk, e2)) logOut += L"OK\n";
        else { logOut += e2 + L"\n"; all = false; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!all) err = T(S4_BASEPART);
    return all;
}

void V4OnAdbUp(const std::wstring& id) {
    std::thread([id]() {
        InstanceCfg* c = Inst(id);
        if (!c) return;
        if (c->baseAppsInstalled) return;
        // give Android time to finish booting
        for (int i = 0; i < 12; ++i) {
            SleepMs(5000);
            Runtime* r = Rt(id);
            if (!r || !r->hProc || !ProcAlive(r->hProc)) return;
            std::wstring o, e;
            if (AdbShell(id, L"getprop sys.boot_completed", o, e) && o.find(L"1") != std::wstring::npos) break;
        }
        InstanceCfg* c2 = Inst(id);
        if (!c2 || c2->baseAppsInstalled) return;
        std::wstring log, err;
        if (V4InstallBaseApps(id, log, err)) {
            std::lock_guard<std::mutex> lk(g_mx);
            InstanceCfg* c3 = Inst(id);
            if (c3) { c3->baseAppsInstalled = true; PersistInstance(*c3); }
            LogW(L"baseapps", L"installed into %s:\n%s", id.c_str(), log.c_str());
            if (g_wnd) UiNotify(T(S4_BASEDONE), 0);
        } else {
            LogW(L"baseapps", L"partial install into %s: %s", id.c_str(), log.c_str());
        }
        // plugin event (TZ6 4.2)
        JValue ev; ev.type = JValue::Obj;
        ev.obj[L"instance"] = JValue::MakeStr(id);
        ev.obj[L"apps"] = JValue::MakeStr(L"base");
        V4PluginsDispatch(L"base_apps_installed", JsonWrite(ev));
    }).detach();
}

// ================================================================ theme (TZ5 3.5)
namespace {
struct ThemePal {
    COLORREF bg, surf, surf2, border, text, sub, navText;
};
ThemePal DarkPal()  { return { CREF(0x111318), CREF(0x1A1F29), CREF(0x202735), CREF(0x2A3342), CREF(0xF6F8FC), CREF(0xA8B0C0), CREF(0x8B93A5) }; }
ThemePal LightPal() { return { CREF(0xF2F4F8), CREF(0xFFFFFF), CREF(0xE8ECF3), CREF(0xD3D9E4), CREF(0x1A1F29), CREF(0x5A6472), CREF(0x3A4250) } ; }
ThemePal g_pal = DarkPal();
}

void V4ApplyTheme() {
    std::wstring t = g_set.theme;
    if (t == L"system") {
        HKEY hk;
        DWORD val = 1, sz = sizeof(val);
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          0, KEY_READ, &hk) == ERROR_SUCCESS) {
            RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (BYTE*)&val, &sz);
            RegCloseKey(hk);
        }
        g_pal = val ? LightPal() : DarkPal();
    } else {
        g_pal = (t == L"light") ? LightPal() : DarkPal();
    }
    if (g_wnd) {
        BOOL dark = (g_set.theme == L"light") ? FALSE : TRUE;
        if (g_set.theme == L"system") {
            HKEY hk; DWORD val = 1, sz = sizeof(val);
            if (RegOpenKeyExW(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                              0, KEY_READ, &hk) == ERROR_SUCCESS) {
                RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (BYTE*)&val, &sz);
                RegCloseKey(hk);
                dark = val ? FALSE : TRUE;
            }
        }
        DwmSetWindowAttribute(g_wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        InvalidateRect(g_wnd, nullptr, FALSE);
    }
}

// palette accessors used by CL_* macros (app.h)
COLORREF NovaPalBg()     { return g_pal.bg; }
COLORREF NovaPalSurf()   { return g_pal.surf; }
COLORREF NovaPalSurf2()  { return g_pal.surf2; }
COLORREF NovaPalBorder() { return g_pal.border; }
COLORREF NovaPalText()   { return g_pal.text; }
COLORREF NovaPalSub()    { return g_pal.sub; }
