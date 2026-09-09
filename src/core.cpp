// ============================================================================
//  NovaDroid - core.cpp  Settings, logging, instance manager (config.json).
// ============================================================================
#include "app.h"

// ---------------------------------------------------------------- globals
HINSTANCE g_hi = nullptr;
HWND      g_wnd = nullptr;
Settings  g_set;
Paths     g_p;
std::vector<InstanceCfg> g_insts;
std::map<std::wstring, Runtime> g_rt;
std::mutex g_mx;
std::wstring g_selId;
std::vector<DiagItem> g_diag;
std::atomic<bool> g_diagBusy{ false };
DWORD g_startTick = 0;

// ---------------------------------------------------------------- paths
void InitPaths() {
    g_p.exeDir = ExeDir();
    std::wstring root = g_set.dataRoot.empty() ? (LocalAppData() + L"\\NovaDroid") : g_set.dataRoot;
    g_p.dataRoot  = root;
    g_p.instances = root + L"\\instances";
    g_p.images    = root + L"\\images";
    g_p.backups   = root + L"\\backups";
    g_p.logs      = root + L"\\logs";
    g_p.cache     = root + L"\\cache";
    g_p.db        = root + L"\\database";
    g_p.apks      = root + L"\\apks";
    g_p.shots     = root + L"\\screenshots";
    g_p.configs   = root + L"\\configs";
    MK(g_p.instances); MK(g_p.images); MK(g_p.backups); MK(g_p.logs);
    MK(g_p.cache); MK(g_p.db); MK(g_p.apks); MK(g_p.shots); MK(g_p.configs);
    MK(g_p.instances + L"\\instance-001"); // placeholder removed later if empty
    // remove placeholder if nothing appeared inside
    if (DE(g_p.instances + L"\\instance-001")) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((g_p.instances + L"\\instance-001\\*").c_str(), &fd);
        bool empty = true;
        if (h != INVALID_HANDLE_VALUE) {
            do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { empty = false; break; } }
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (empty) RemoveDirectoryW((g_p.instances + L"\\instance-001").c_str());
    }
}

// ---------------------------------------------------------------- settings
void LoadSettings() {
    std::wstring root = LocalAppData() + L"\\NovaDroid";
    MK(root);
    std::wstring txt;
    if (ReadText(root + L"\\settings.json", txt)) {
        JValue v; std::wstring err;
        if (JsonParse(txt, v, err)) {
            if (auto* x = v.find(L"language")) g_set.language = x->asStr(L"ru");
            if (auto* x = v.find(L"accentHex")) g_set.accentHex = x->asStr(L"#6C63FF");
            if (auto* x = v.find(L"qemuPath")) g_set.qemuPath = x->asStr();
            if (auto* x = v.find(L"adbPath")) g_set.adbPath = x->asStr();
            if (auto* x = v.find(L"defaultImagePath")) g_set.defaultImagePath = x->asStr();
            if (auto* x = v.find(L"dataRoot")) g_set.dataRoot = x->asStr();
            if (auto* x = v.find(L"baseAdbPort")) g_set.baseAdbPort = x->asInt(5555);
            if (auto* x = v.find(L"checkOnStart")) g_set.checkOnStart = x->asBool(true);
            if (auto* x = v.find(L"autostartLast")) g_set.autostartLast = x->asBool(false);
            if (auto* x = v.find(L"maxRunning")) g_set.maxRunning = x->asInt(2);
            if (auto* x = v.find(L"updateChannel")) g_set.updateChannel = x->asStr(L"stable");
            if (auto* x = v.find(L"ecoPerf")) g_set.ecoPerf = x->asBool(false);
            // stage 4-7 (TZ 4-7)
            if (auto* x = v.find(L"theme")) g_set.theme = x->asStr(L"dark");
            if (auto* x = v.find(L"firstRunDone")) g_set.firstRunDone = x->asBool(false);
            if (auto* x = v.find(L"analyticsConsent")) g_set.analyticsConsent = x->asBool(false);
            if (auto* x = v.find(L"cloudProvider")) g_set.cloudProvider = x->asStr();
            if (auto* x = v.find(L"cloudEndpoint")) g_set.cloudEndpoint = x->asStr();
            if (auto* x = v.find(L"cloudToken")) g_set.cloudToken = x->asStr();
            if (auto* x = v.find(L"cloudEncrypt")) g_set.cloudEncrypt = x->asBool(true);
            if (auto* x = v.find(L"discordEnabled")) g_set.discordEnabled = x->asBool(false);
            if (auto* x = v.find(L"discordClientId")) g_set.discordClientId = x->asStr();
            if (auto* x = v.find(L"proKey")) g_set.proKey = x->asStr();
            if (auto* x = v.find(L"streamPort")) g_set.streamPort = x->asInt(8080);
            if (g_set.baseAdbPort < 1024 || g_set.baseAdbPort > 50000) g_set.baseAdbPort = 5555;
            if (g_set.maxRunning < 1 || g_set.maxRunning > 16) g_set.maxRunning = 2;
        }
    }
    // apply dataRoot BEFORE paths resolve (settings loaded into temp root var)
    if (!g_set.dataRoot.empty()) root = g_set.dataRoot;
}
bool SaveSettings() {
    JValue v;
    v.type = JValue::Obj;
    v.obj[L"language"] = JValue::MakeStr(g_set.language);
    v.obj[L"accentHex"] = JValue::MakeStr(g_set.accentHex);
    v.obj[L"qemuPath"] = JValue::MakeStr(g_set.qemuPath);
    v.obj[L"adbPath"] = JValue::MakeStr(g_set.adbPath);
    v.obj[L"defaultImagePath"] = JValue::MakeStr(g_set.defaultImagePath);
    v.obj[L"dataRoot"] = JValue::MakeStr(g_set.dataRoot);
    v.obj[L"baseAdbPort"] = JValue::MakeNum(g_set.baseAdbPort);
    v.obj[L"checkOnStart"] = JValue::MakeBool(g_set.checkOnStart);
    v.obj[L"autostartLast"] = JValue::MakeBool(g_set.autostartLast);
    v.obj[L"maxRunning"] = JValue::MakeNum(g_set.maxRunning);
    v.obj[L"updateChannel"] = JValue::MakeStr(g_set.updateChannel);
    v.obj[L"ecoPerf"] = JValue::MakeBool(g_set.ecoPerf);
    // stage 4-7 (TZ 4-7)
    v.obj[L"theme"] = JValue::MakeStr(g_set.theme);
    v.obj[L"firstRunDone"] = JValue::MakeBool(g_set.firstRunDone);
    v.obj[L"analyticsConsent"] = JValue::MakeBool(g_set.analyticsConsent);
    v.obj[L"cloudProvider"] = JValue::MakeStr(g_set.cloudProvider);
    v.obj[L"cloudEndpoint"] = JValue::MakeStr(g_set.cloudEndpoint);
    v.obj[L"cloudToken"] = JValue::MakeStr(g_set.cloudToken);
    v.obj[L"cloudEncrypt"] = JValue::MakeBool(g_set.cloudEncrypt);
    v.obj[L"discordEnabled"] = JValue::MakeBool(g_set.discordEnabled);
    v.obj[L"discordClientId"] = JValue::MakeStr(g_set.discordClientId);
    v.obj[L"proKey"] = JValue::MakeStr(g_set.proKey);
    v.obj[L"streamPort"] = JValue::MakeNum(g_set.streamPort);
    return WriteText(g_p.dataRoot + L"\\settings.json", JsonWrite(v));
}

// ---------------------------------------------------------------- logging
static std::mutex g_logMx;
static std::wstring LogFilePath() {
    return g_p.logs + L"\\launcher-" + NowFileStamp().substr(0, 8) + L".log";
}
void LogW(const wchar_t* tag, const wchar_t* fmt, ...) {
    wchar_t msg[2048];
    va_list ap; va_start(ap, fmt);
    vswprintf(msg, 2048, fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring line = Fmt(L"[%02d:%02d:%02d.%03d] [%s] %s\r\n",
                            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag, msg);
    std::lock_guard<std::mutex> lk(g_logMx);
    HANDLE h = CreateFileW(LogFilePath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        std::string u = W2U(line);
        DWORD wr; WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
    OutputDebugStringW(line.c_str());
}
std::wstring InstanceLogPath(const std::wstring& id, const wchar_t* kind) {
    std::wstring dir = g_p.instances + L"\\" + id + L"\\logs";
    MK(dir);
    if (!wcscmp(kind, L"serial")) return dir + L"\\serial.log";
    return dir + L"\\" + std::wstring(kind) + L"-" + NowFileStamp().substr(0, 8) + L".log";
}
void ILog(const std::wstring& id, const wchar_t* kind, const std::wstring& line) {
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring full = Fmt(L"[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, line.c_str());
    std::lock_guard<std::mutex> lk(g_logMx);
    HANDLE h = CreateFileW(InstanceLogPath(id, kind).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        std::string u = W2U(full);
        DWORD wr; WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
        CloseHandle(h);
    }
}
void CleanupOldLogs() {
    auto clean = [](const std::wstring& dir) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.log").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            std::wstring f = dir + L"\\" + fd.cFileName;
            FILETIME ft; GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &fd);
            ft = fd.ftLastWriteTime;
            ULARGE_INTEGER now, t;
            GetSystemTimeAsFileTime((FILETIME*)&now);
            t.LowPart = ft.dwLowDateTime; t.HighPart = ft.dwHighDateTime;
            if (now.QuadPart - t.QuadPart > 14LL * 24 * 3600 * 10000000LL) DeleteFileW(f.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };
    clean(g_p.logs);
    for (auto& c : g_insts) clean(g_p.instances + L"\\" + c.id + L"\\logs");
}

// ---------------------------------------------------------------- instance cfg <-> json
JValue InstanceCfg::ToJ() const {
    JValue v; v.type = JValue::Obj;
    v.obj[L"id"] = JValue::MakeStr(id);
    v.obj[L"name"] = JValue::MakeStr(name);
    v.obj[L"androidVersion"] = JValue::MakeStr(androidVersion);
    v.obj[L"imagePath"] = JValue::MakeStr(imagePath);
    v.obj[L"bootMode"] = JValue::MakeStr(bootMode);
    v.obj[L"diskFormat"] = JValue::MakeStr(diskFormat);
    v.obj[L"cpuCores"] = JValue::MakeNum(cpuCores);
    v.obj[L"ramMb"] = JValue::MakeNum(ramMb);
    v.obj[L"diskGb"] = JValue::MakeNum(diskGb);
    v.obj[L"resolutionWidth"] = JValue::MakeNum(resW);
    v.obj[L"resolutionHeight"] = JValue::MakeNum(resH);
    v.obj[L"dpi"] = JValue::MakeNum(dpi);
    v.obj[L"fpsLimit"] = JValue::MakeNum(fpsLimit);
    v.obj[L"gpuMode"] = JValue::MakeStr(gpuMode);
    v.obj[L"priority"] = JValue::MakeStr(priority);
    v.obj[L"soundEnabled"] = JValue::MakeBool(soundEnabled);
    v.obj[L"fullscreen"] = JValue::MakeBool(fullscreen);
    v.obj[L"networkMode"] = JValue::MakeStr(networkMode);
    v.obj[L"adbEnabled"] = JValue::MakeBool(adbEnabled);
    v.obj[L"adbPort"] = JValue::MakeNum(adbPort);
    v.obj[L"sharedFolderEnabled"] = JValue::MakeBool(sharedFolderEnabled);
    v.obj[L"sharedFolderPath"] = JValue::MakeStr(sharedFolderPath);
    v.obj[L"createdAt"] = JValue::MakeStr(createdAt);
    v.obj[L"lastLaunchAt"] = lastLaunchAt.empty() ? JValue() : JValue::MakeStr(lastLaunchAt);
    // stage 2 (TZ 17.1)
    v.obj[L"icon"] = JValue::MakeStr(icon);
    v.obj[L"template"] = JValue::MakeStr(templateName);
    v.obj[L"adbHost"] = JValue::MakeStr(adbHost.empty() ? std::wstring(L"127.0.0.1") : adbHost);
    v.obj[L"lastShutdownAt"] = lastShutdownAt.empty() ? JValue() : JValue::MakeStr(lastShutdownAt);
    v.obj[L"parentInstanceId"] = parentInstanceId.empty() ? JValue() : JValue::MakeStr(parentInstanceId);
    v.obj[L"cloneType"] = JValue::MakeStr(cloneType);
    v.obj[L"keymappingProfileId"] = JValue::MakeStr(keymapProfileId);
    // stage 3 (TZ 3.0)
    v.obj[L"gpuBackend"] = JValue::MakeStr(gpuBackend);
    v.obj[L"borderless"] = JValue::MakeBool(borderless);
    v.obj[L"topMost"] = JValue::MakeBool(topMost);
    v.obj[L"scaleMode"] = JValue::MakeStr(scaleMode);
    v.obj[L"vsync"] = JValue::MakeBool(vsync);
    v.obj[L"soundVolume"] = JValue::MakeNum(soundVolume);
    v.obj[L"muteOnMinimize"] = JValue::MakeBool(muteOnMinimize);
    v.obj[L"fpsOverlay"] = JValue::MakeBool(fpsOverlay);
    v.obj[L"quickStart"] = JValue::MakeBool(quickStart);
    v.obj[L"perfProfile"] = JValue::MakeStr(perfProfile);
    v.obj[L"captureHotkey"] = JValue::MakeNum(captureHotkey);
    v.obj[L"sensX"] = JValue::MakeNum(sensX);
    v.obj[L"sensY"] = JValue::MakeNum(sensY);
    v.obj[L"invertY"] = JValue::MakeBool(invertY);
    v.obj[L"mouseAccel"] = JValue::MakeNum(mouseAccel);
    // stage 4-7 (TZ 4-7)
    v.obj[L"dnsServer"] = JValue::MakeStr(dnsServer);
    v.obj[L"baseAppsInstalled"] = JValue::MakeBool(baseAppsInstalled);
    v.obj[L"antiLag"] = JValue::MakeStr(antiLag);
    v.obj[L"syncRole"] = JValue::MakeStr(syncRole);
    return v;
}
InstanceCfg InstanceCfg::FromJ(const JValue& v) {
    InstanceCfg c;
    if (auto* x = v.find(L"id")) c.id = x->asStr();
    if (auto* x = v.find(L"name")) c.name = x->asStr();
    if (auto* x = v.find(L"androidVersion")) c.androidVersion = x->asStr();
    if (auto* x = v.find(L"imagePath")) c.imagePath = x->asStr();
    if (auto* x = v.find(L"bootMode")) c.bootMode = x->asStr(L"iso");
    if (auto* x = v.find(L"diskFormat")) c.diskFormat = x->asStr(L"qcow2");
    if (auto* x = v.find(L"cpuCores")) c.cpuCores = x->asInt(4);
    if (auto* x = v.find(L"ramMb")) c.ramMb = x->asInt(4096);
    if (auto* x = v.find(L"diskGb")) c.diskGb = x->asInt(32);
    if (auto* x = v.find(L"resolutionWidth")) c.resW = x->asInt(1280);
    if (auto* x = v.find(L"resolutionHeight")) c.resH = x->asInt(720);
    if (auto* x = v.find(L"dpi")) c.dpi = x->asInt(240);
    if (auto* x = v.find(L"fpsLimit")) c.fpsLimit = x->asInt(60);
    if (auto* x = v.find(L"gpuMode")) c.gpuMode = x->asStr(L"auto");
    if (auto* x = v.find(L"priority")) c.priority = x->asStr(L"normal");
    if (auto* x = v.find(L"soundEnabled")) c.soundEnabled = x->asBool(true);
    if (auto* x = v.find(L"fullscreen")) c.fullscreen = x->asBool(false);
    if (auto* x = v.find(L"networkMode")) c.networkMode = x->asStr(L"nat");
    if (auto* x = v.find(L"adbEnabled")) c.adbEnabled = x->asBool(true);
    if (auto* x = v.find(L"adbPort")) c.adbPort = x->asInt(0);
    if (auto* x = v.find(L"sharedFolderEnabled")) c.sharedFolderEnabled = x->asBool(true);
    if (auto* x = v.find(L"sharedFolderPath")) c.sharedFolderPath = x->asStr();
    if (auto* x = v.find(L"createdAt")) c.createdAt = x->asStr();
    if (auto* x = v.find(L"lastLaunchAt")) c.lastLaunchAt = x->asStr();
    // stage 2 (TZ 17.1)
    if (auto* x = v.find(L"icon")) c.icon = x->asStr(L"android");
    if (auto* x = v.find(L"template")) c.templateName = x->asStr(L"balanced");
    if (auto* x = v.find(L"adbHost")) c.adbHost = x->asStr(L"127.0.0.1");
    if (auto* x = v.find(L"lastShutdownAt")) c.lastShutdownAt = x->asStr();
    if (auto* x = v.find(L"parentInstanceId")) c.parentInstanceId = x->asStr();
    if (auto* x = v.find(L"cloneType")) c.cloneType = x->asStr(L"full");
    if (auto* x = v.find(L"keymappingProfileId")) c.keymapProfileId = x->asStr();
    // stage 3 (TZ 3.0)
    if (auto* x = v.find(L"gpuBackend")) c.gpuBackend = x->asStr(L"auto");
    if (auto* x = v.find(L"borderless")) c.borderless = x->asBool(false);
    if (auto* x = v.find(L"topMost")) c.topMost = x->asBool(false);
    if (auto* x = v.find(L"scaleMode")) c.scaleMode = x->asStr(L"fit");
    if (auto* x = v.find(L"vsync")) c.vsync = x->asBool(true);
    if (auto* x = v.find(L"soundVolume")) c.soundVolume = x->asInt(80);
    if (auto* x = v.find(L"muteOnMinimize")) c.muteOnMinimize = x->asBool(true);
    if (auto* x = v.find(L"fpsOverlay")) c.fpsOverlay = x->asBool(true);
    if (auto* x = v.find(L"quickStart")) c.quickStart = x->asBool(true);
    if (auto* x = v.find(L"perfProfile")) c.perfProfile = x->asStr(L"balanced");
    if (auto* x = v.find(L"captureHotkey")) c.captureHotkey = x->asInt(VK_RCONTROL);
    if (auto* x = v.find(L"sensX")) c.sensX = x->asInt(100);
    if (auto* x = v.find(L"sensY")) c.sensY = x->asInt(100);
    if (auto* x = v.find(L"invertY")) c.invertY = x->asBool(false);
    if (auto* x = v.find(L"mouseAccel")) c.mouseAccel = x->asInt(0);
    // stage 4-7 (TZ 4-7)
    if (auto* x = v.find(L"dnsServer")) c.dnsServer = x->asStr();
    if (auto* x = v.find(L"baseAppsInstalled")) c.baseAppsInstalled = x->asBool(false);
    if (auto* x = v.find(L"antiLag")) c.antiLag = x->asStr(L"on");
    if (auto* x = v.find(L"syncRole")) c.syncRole = x->asStr();
    if (c.adbHost.empty()) c.adbHost = L"127.0.0.1";
    return c;
}
std::wstring InstanceCfg::Dir() const {
    return g_p.instances + L"\\" + id;
}

void PersistInstance(const InstanceCfg& c) {
    std::wstring dir = c.Dir();
    MK(dir); MK(dir + L"\\logs"); MK(dir + L"\\snapshots"); MK(dir + L"\\shared");
    // backup config.json before rewrite (TZ 11.2)
    std::wstring cfgPath = dir + L"\\config.json";
    if (FE(cfgPath)) CopyFileW(cfgPath.c_str(), (dir + L"\\config.json.bak").c_str(), FALSE);
    WriteText(cfgPath, JsonWrite(c.ToJ()));
}

InstanceCfg* Inst(const std::wstring& id) {
    for (auto& c : g_insts) if (c.id == id) return &c;
    return nullptr;
}
Runtime* Rt(const std::wstring& id) {
    auto it = g_rt.find(id);
    return it == g_rt.end() ? nullptr : &it->second;
}
void SetStatus(const std::wstring& id, St st) {
    std::lock_guard<std::mutex> lk(g_mx);
    Runtime* r = Rt(id);
    if (r && r->st != st) {
        r->st = st;
        if (st == St::Running || st == St::Stopped) r->busy = false;
    }
    if (g_wnd) PostMessageW(g_wnd, WM_APP_STATUS, 0, 0);
}
void NotifyStatus(const std::wstring& id) {
    if (g_wnd) PostMessageW(g_wnd, WM_APP_STATUS, 0, 0);
}

// ---------------------------------------------------------------- instance list
void LoadInstances() {
    g_insts.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_p.instances + L"\\instance-*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring id = fd.cFileName;
        std::wstring cfgPath = g_p.instances + L"\\" + id + L"\\config.json";
        std::wstring txt;
        if (!ReadText(cfgPath, txt)) continue;
        JValue v; std::wstring err;
        if (!JsonParse(txt, v, err)) {
            LogW(L"cfg", L"bad config.json for %s: %s", id.c_str(), err.c_str());
            continue;
        }
        InstanceCfg c = InstanceCfg::FromJ(v);
        if (c.id.empty()) c.id = id;
        g_insts.push_back(c);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(g_insts.begin(), g_insts.end(),
              [](const InstanceCfg& a, const InstanceCfg& b) { return a.id < b.id; });
    if (g_selId.empty() && !g_insts.empty()) g_selId = g_insts[0].id;
}

std::wstring NextInstanceId() {
    int maxN = 0;
    for (auto& c : g_insts) {
        if (c.id.rfind(L"instance-", 0) == 0) {
            int n = _wtoi(c.id.c_str() + 9);
            if (n > maxN) maxN = n;
        }
    }
    return Fmt(L"instance-%03d", maxN + 1);
}
int NextFreePort() {
    int port = g_set.baseAdbPort;
    for (auto& c : g_insts) if (c.adbPort >= port) port = c.adbPort + 1;
    return port;
}
void EnsureDefaultSharedFolder(InstanceCfg& c) {
    if (c.sharedFolderPath.empty())
        c.sharedFolderPath = c.Dir() + L"\\shared";
}

std::wstring CreateInstance(const InstanceCfg& tpl, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg c = tpl;
    c.id = NextInstanceId();
    c.createdAt = NowIso();
    c.androidVersion = L"";
    c.adbPort = NextFreePort();
    EnsureDefaultSharedFolder(c);
    MK(c.Dir()); MK(c.Dir() + L"\\logs"); MK(c.Dir() + L"\\snapshots"); MK(c.Dir() + L"\\shared");
    PersistInstance(c);
    g_insts.push_back(c);
    g_rt[c.id] = Runtime();
    g_selId = c.id;
    LogW(L"inst", L"created %s (name='%s', %d CPU, %d MB RAM)", c.id.c_str(), c.name.c_str(), c.cpuCores, c.ramMb);
    return c.id;
}

bool DeleteInstance(const std::wstring& id, bool keepBackup, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    Runtime* r = Rt(id);
    if (r && (r->st == St::Running || r->st == St::Starting)) {
        err = L"instance is running";
        return false;
    }
    if (keepBackup) {
        std::wstring bdir = g_p.backups + L"\\" + id + L"-deleted-" + NowFileStamp();
        MK(bdir);
        CopyTree(g_p.instances + L"\\" + id, bdir);
    }
    DeleteTree(g_p.instances + L"\\" + id);
    for (size_t i = 0; i < g_insts.size(); ++i)
        if (g_insts[i].id == id) { g_insts.erase(g_insts.begin() + i); break; }
    g_rt.erase(id);
    if (g_selId == id) g_selId = g_insts.empty() ? L"" : g_insts[0].id;
    LogW(L"inst", L"deleted %s (backup=%d)", id.c_str(), keepBackup ? 1 : 0);
    return true;
}

bool CloneInstance(const std::wstring& id, const std::wstring& newName, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* src = Inst(id);
    Runtime* r = Rt(id);
    if (!src) { err = L"not found"; return false; }
    if (r && (r->st == St::Running || r->st == St::Starting)) { err = L"instance is running"; return false; }
    InstanceCfg c = *src;
    c.id = NextInstanceId();
    c.name = newName;
    c.createdAt = NowIso();
    c.lastLaunchAt = L"";
    c.androidVersion = src->androidVersion;
    c.adbPort = NextFreePort();
    EnsureDefaultSharedFolder(c);
    MK(c.Dir()); MK(c.Dir() + L"\\logs"); MK(c.Dir() + L"\\snapshots"); MK(c.Dir() + L"\\shared");
    // copy disk if exists
    std::wstring srcDisk = src->Dir() + L"\\disk." + (src->diskFormat == L"raw" ? L"raw" : L"qcow2");
    if (FE(srcDisk)) {
        std::wstring dstDisk = c.Dir() + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2");
        CopyFileW(srcDisk.c_str(), dstDisk.c_str(), FALSE);
    }
    PersistInstance(c);
    g_insts.push_back(c);
    g_rt[c.id] = Runtime();
    LogW(L"inst", L"cloned %s -> %s", id.c_str(), c.id.c_str());
    return true;
}

bool RenameInstance(const std::wstring& id, const std::wstring& newName, std::wstring& err) {
    (void)err;
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* c = Inst(id);
    if (!c) return false;
    c->name = newName;
    PersistInstance(*c);
    LogW(L"inst", L"renamed %s -> '%s'", id.c_str(), newName.c_str());
    return true;
}

bool BackupInstance(const std::wstring& id, bool withDisk, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring bdir = g_p.backups + L"\\" + id + L"-" + NowFileStamp();
    MK(bdir);
    CopyFileW((c->Dir() + L"\\config.json").c_str(), (bdir + L"\\config.json").c_str(), FALSE);
    CopyTree(c->Dir() + L"\\logs", bdir + L"\\logs");
    if (withDisk) {
        std::wstring disk = c->Dir() + L"\\disk." + (c->diskFormat == L"raw" ? L"raw" : L"qcow2");
        if (FE(disk)) CopyFileW(disk.c_str(), (bdir + L"\\disk." + (c->diskFormat == L"raw" ? L"raw" : L"qcow2")).c_str(), FALSE);
    }
    LogW(L"inst", L"backup %s -> %s (disk=%d)", id.c_str(), bdir.c_str(), withDisk ? 1 : 0);
    return true;
}

bool RestoreBackup(const std::wstring& id, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    Runtime* r = Rt(id);
    if (r && (r->st == St::Running || r->st == St::Starting)) { err = L"instance is running"; return false; }
    // find latest backup for id
    std::wstring best;
    ULONGLONG bestT = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_p.backups + L"\\" + id + L"-*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { err = L"no backups"; return false; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        ULARGE_INTEGER t;
        t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        if (t.QuadPart >= bestT) { bestT = t.QuadPart; best = fd.cFileName; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (best.empty()) { err = L"no backups"; return false; }
    std::wstring bdir = g_p.backups + L"\\" + best;
    std::wstring cfgBak = bdir + L"\\config.json";
    if (!FE(cfgBak)) { err = L"backup has no config.json"; return false; }
    std::wstring txt;
    if (!ReadText(cfgBak, txt)) { err = L"cannot read backup"; return false; }
    JValue v; std::wstring jerr;
    if (!JsonParse(txt, v, jerr)) { err = jerr; return false; }
    InstanceCfg bc = InstanceCfg::FromJ(v);
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring diskFile = bc.diskFormat == L"raw" ? L"disk.raw" : L"disk.qcow2";
    if (FE(bdir + L"\\" + diskFile))
        CopyFileW((bdir + L"\\" + diskFile).c_str(), (c->Dir() + L"\\" + diskFile).c_str(), FALSE);
    std::wstring keep = c->id, nm = bc.name;
    bc.id = keep;
    *c = bc;
    PersistInstance(*c);
    LogW(L"inst", L"restored %s from %s", id.c_str(), best.c_str());
    return true;
}

bool ExportInstance(const std::wstring& id, const std::wstring& dstDir, bool withDisk, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring edir = dstDir + L"\\NovaDroid-" + id;
    MK(edir);
    CopyFileW((c->Dir() + L"\\config.json").c_str(), (edir + L"\\config.json").c_str(), FALSE);
    CopyTree(c->Dir() + L"\\logs", edir + L"\\logs");
    if (withDisk) {
        std::wstring disk = c->Dir() + L"\\disk." + (c->diskFormat == L"raw" ? L"raw" : L"qcow2");
        if (FE(disk)) CopyFileW(disk.c_str(), (edir + L"\\disk." + (c->diskFormat == L"raw" ? L"raw" : L"qcow2")).c_str(), FALSE);
    }
    LogW(L"inst", L"exported %s -> %s", id.c_str(), edir.c_str());
    return true;
}

bool ImportInstance(const std::wstring& cfgPath, std::wstring& newId, std::wstring& err) {
    std::wstring txt;
    if (!ReadText(cfgPath, txt)) { err = L"cannot read file"; return false; }
    JValue v; std::wstring jerr;
    if (!JsonParse(txt, v, jerr)) { err = jerr; return false; }
    InstanceCfg c = InstanceCfg::FromJ(v);
    if (c.name.empty()) { err = L"invalid config"; return false; }
    std::lock_guard<std::mutex> lk(g_mx);
    std::wstring srcDir = cfgPath.substr(0, cfgPath.find_last_of(L"\\"));
    c.id = NextInstanceId();
    c.createdAt = NowIso();
    c.lastLaunchAt = L"";
    c.adbPort = NextFreePort();
    EnsureDefaultSharedFolder(c);
    MK(c.Dir()); MK(c.Dir() + L"\\logs"); MK(c.Dir() + L"\\snapshots"); MK(c.Dir() + L"\\shared");
    std::wstring disk = srcDir + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2");
    if (FE(disk)) CopyFileW(disk.c_str(), (c.Dir() + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2")).c_str(), FALSE);
    PersistInstance(c);
    g_insts.push_back(c);
    g_rt[c.id] = Runtime();
    g_selId = c.id;
    newId = c.id;
    LogW(L"inst", L"imported -> %s", c.id.c_str());
    return true;
}

// ---------------------------------------------------------------- status helpers
std::wstring StatusText(St st) {
    switch (st) {
        case St::Stopped:   return T(S_ST_STOPPED);
        case St::Starting:  return T(S_ST_STARTING);
        case St::Running:   return T(S_ST_RUNNING);
        case St::Paused:    return T(S_ST_PAUSED);
        case St::Stopping:  return T(S_ST_STOPPING);
        case St::StartError:return T(S_ST_ERROR);
        case St::Recovery:  return T(S_ST_RECOVERY);
        case St::Updating:  return T(S_ST_UPDATING);
        case ST2_PREPARING: return T(S_ST2_PREP);
        case ST2_WAITBOOT:  return T(S_ST2_WAIT);
        case ST2_CLONING:   return T(S_ST2_CLONING);
        case ST2_BACKUP:    return T(S_ST2_BAK);
        case ST2_RESTORE:   return T(S_ST2_REST);
        case ST2_CORRUPT:   return T(S_ST2_CORRUPT);
    }
    return L"?";
}
COLORREF StatusColor(St st) {
    switch (st) {
        case St::Running:    return CL_OK;
        case St::Starting:
        case St::Stopping:
        case ST2_PREPARING:
        case ST2_WAITBOOT:
        case ST2_CLONING:
        case ST2_BACKUP:
        case ST2_RESTORE:    return CL_WARN;
        case St::StartError:
        case St::Recovery:
        case ST2_CORRUPT:    return CL_ERR;
        case St::Paused:
        case St::Updating:   return CREF(0x4A7DFF);
        default:             return CL_SUB;
    }
}
