// ============================================================================
//  NovaDroid - backend.cpp  QEMU/WHPX control, ADB wrapper, diagnostics (TZ 6.10)
// ============================================================================
#include "app.h"
#include "v3.h"

// ---------------------------------------------------------------- tools
std::wstring FindTool(const std::wstring& configured, const wchar_t* subDir,
                      const wchar_t* exeName) {
    if (!configured.empty() && FE(configured)) return configured;
    std::wstring local = g_p.exeDir + L"\\" + subDir + L"\\" + exeName;
    if (FE(local)) return local;
    // stage 3: full package may be installed into dataRoot
    if (!g_p.dataRoot.empty()) {
        std::wstring dr = g_p.dataRoot + L"\\" + subDir + L"\\" + exeName;
        if (FE(dr)) return dr;
    }
    wchar_t found[MAX_PATH * 2] = {};
    if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH * 2, found, nullptr)) return found;
    return L"";
}
std::wstring QemuExe()  { return FindTool(g_set.qemuPath, L"qemu", L"qemu-system-x86_64.exe"); }
std::wstring QemuImg()  {
    std::wstring q = QemuExe();
    if (!q.empty()) {
        size_t s = q.find_last_of(L"\\");
        std::wstring img = q.substr(0, s + 1) + L"qemu-img.exe";
        if (FE(img)) return img;
    }
    return FindTool(L"", L"qemu", L"qemu-img.exe");
}
std::wstring AdbExe()   { return FindTool(g_set.adbPath, L"adb", L"adb.exe"); }

std::wstring AdbSerial(const InstanceCfg& c) {
    return Fmt(L"127.0.0.1:%d", c.adbPort);
}
std::wstring AdbDirOf(const InstanceCfg& c) { return c.Dir(); }

// ---------------------------------------------------------------- QEMU args
std::wstring BuildQemuArgs(const InstanceCfg& c, std::wstring& err) {
    GpuPlan gp = V3ResolveGpu(c);
    if (!gp.fallbackNote.empty())
        ILog(c.id, L"launcher", std::wstring(L"GPU: ") + gp.fallbackNote);
    ILog(c.id, L"launcher", L"GPU mode: requested=" + gp.requested + L" resolved=" + gp.resolved +
         L" gl=" + (gp.glDisplay ? L"on" : L"off") + L" display=" + gp.displayKind);
    return BuildQemuArgsV3(c, gp, err);
}

// ---------------------------------------------------------------- WHPX / virt
bool WhpxAvailable() {
    HMODULE h = LoadLibraryW(L"WinHvPlatform.dll");
    if (!h) return false;
    bool ok = GetProcAddress(h, "WHvCreateHandle") != nullptr ||
              GetProcAddress(h, "WHvGetCapability") != nullptr ||
              GetProcAddress(h, "WinHvCreateHandle") != nullptr;
    FreeLibrary(h);
    return ok;
}
bool VirtFirmwareEnabled() {
    return IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != 0;
}

// ---------------------------------------------------------------- start / stop
bool CreateDiskImage(const InstanceCfg& c, std::wstring& err) {
    std::wstring diskFile = c.Dir() + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2");
    if (FE(diskFile)) return true;
    if (c.bootMode == L"iso" && !FE(diskFile)) {
        // create data disk for ISO mode (optional, non-fatal)
    }
    std::wstring qi = QemuImg();
    if (qi.empty()) {
        // fallback: sparse raw file
        HANDLE h = CreateFileW((c.Dir() + L"\\disk.raw").c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { err = L"cannot create disk"; return false; }
        LARGE_INTEGER sz; sz.QuadPart = (long long)c.diskGb * 1024LL * 1024LL * 1024LL;
        SetFilePointerEx(h, sz, nullptr, FILE_BEGIN);
        SetEndOfFile(h);
        CloseHandle(h);
        return true;
    }
    DWORD ec = 0; std::string so, se;
    std::wstring fmt = c.diskFormat == L"raw" ? L"raw" : L"qcow2";
    RunCapture(qi, L"create -f " + fmt + L" " + Qn(diskFile) + Fmt(L" %dG", c.diskGb),
               c.Dir(), 120000, &ec, &so, &se);
    if (!FE(diskFile)) { err = L"qemu-img create failed"; return false; }
    return true;
}

bool StartInstance(const std::wstring& id, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* c = Inst(id);
    Runtime* r = Rt(id);
    if (!c || !r) { err = L"not found"; return false; }
    if (r->st == St::Running || r->st == St::Starting || r->busy) {
        err = L"busy"; return false;
    }
    if (c->imagePath.empty() && c->bootMode != L"disk") { err = T(S_MB_IMGMISS); return false; }
    std::wstring qemu = QemuExe();
    if (qemu.empty()) { err = T(S_MB_QEMUMISS); return false; }
    if (c->bootMode == L"iso" && !FE(c->imagePath)) { err = T(S_MB_IMGMISS); return false; }

    MK(c->Dir() + L"\\logs"); MK(c->Dir() + L"\\snapshots"); MK(c->Dir() + L"\\shared");
    if (!CreateDiskImage(*c, err)) return false;

    std::wstring args = BuildQemuArgs(*c, err);
    if (args.empty()) return false;

    std::wstring outLog = InstanceLogPath(id, L"qemu");
    std::wstring errLog = InstanceLogPath(id, L"qemu-err");
    HANDLE hp = nullptr; DWORD pid = 0;
    if (!LaunchWithLogs(qemu, args, c->Dir(), outLog, errLog, &hp, &pid)) {
        err = L"CreateProcess failed";
        LogW(L"qemu", L"launch failed for %s", id.c_str());
        return false;
    }
    r->hProc = hp;
    r->pid = pid;
    r->adbOk = false;
    r->startedAtMs = (long long)GetTickCount64();
    r->st = St::Starting;
    c->lastLaunchAt = NowIso();
    PersistInstance(*c);
    LogW(L"qemu", L"started %s pid=%lu whpx=%d accel args built", id.c_str(), (unsigned long)pid, WhpxAvailable() ? 1 : 0);
    ILog(id, L"launcher", L"QEMU started (pid " + std::to_wstring((long long)pid) + L", WHPX=" +
         (WhpxAvailable() ? L"yes" : L"no/TCG") + L")");

    // QEMU exit monitor (single owner of the process handle)
    std::wstring id2 = id;
    HANDLE hpc = hp;
    std::thread([hpc, id2] {
        WaitForSingleObject(hpc, INFINITE);
        QExitMsg* q = new QExitMsg{ id2, hpc };
        if (g_wnd) PostMessageW(g_wnd, WM_APP_QEXIT, 0, (LPARAM)q);
    }).detach();

    // ADB readiness watcher
    if (c->adbEnabled && !AdbExe().empty()) {
        std::thread([id2] {
            std::wstring adb = AdbExe();
            for (int i = 0; i < 60; ++i) {
                SleepMs(2000);
                std::lock_guard<std::mutex> lk2(g_mx);
                Runtime* rr = Rt(id2);
                InstanceCfg* cc = Inst(id2);
                if (!rr || !rr->hProc) return;               // died already
                if (rr->st != St::Starting) return;          // stopped meanwhile
                std::string so, se; DWORD ec = 0;
                std::wstring serial = AdbSerial(*cc);
                RunCapture(adb, L"-s " + serial + L" get-state", L"", 4000, &ec, &so, &se);
                if (ec == 0 && TrimW(U2W(so)) == L"device") {
                    rr->adbOk = true;
                    std::wstring ver;
                    std::string v1, v2;
                    RunCapture(adb, L"-s " + serial + L" shell getprop ro.build.version.release",
                               L"", 5000, &ec, &v1, &v2);
                    ver = TrimW(U2W(v1));
                    std::string a1, a2;
                    RunCapture(adb, L"-s " + serial + L" shell getprop ro.product.cpu.abi",
                               L"", 5000, &ec, &a1, &a2);
                    std::wstring abi = TrimW(U2W(a1));
                    if (!ver.empty()) {
                        rr->verDetected = ver;
                        cc->androidVersion = L"Android " + ver + (abi == L"x86_64" ? L" (x86_64)" : L"");
                        PersistInstance(*cc);
                        ILog(id2, L"adb", L"connected " + serial + L", Android " + ver);
                    }
                    rr->st = St::Running;
                    if (g_wnd) PostMessageW(g_wnd, WM_APP_ADBOK, 0, (LPARAM)new std::wstring(id2));
                    return;
                }
            }
            // adb never came up - leave status Starting; qemu monitor will catch exits
            std::lock_guard<std::mutex> lk3(g_mx);
            Runtime* rr3 = Rt(id2);
            if (rr3 && rr3->st == St::Starting && rr3->hProc && ProcAlive(rr3->hProc)) {
                rr3->st = St::Running;   // VM is alive; ADB just not reachable yet
                if (g_wnd) PostMessageW(g_wnd, WM_APP_STATUS, 0, 0);
            }
        }).detach();
    } else {
        // no ADB: mark running after short grace period
        std::wstring id3 = id;
        std::thread([id3] {
            SleepMs(8000);
            std::lock_guard<std::mutex> lk(g_mx);
            Runtime* rr = Rt(id3);
            if (rr && rr->st == St::Starting && rr->hProc && ProcAlive(rr->hProc)) {
                rr->st = St::Running;
                if (g_wnd) PostMessageW(g_wnd, WM_APP_STATUS, 0, 0);
            }
        }).detach();
    }
    NotifyStatus(id);
    return true;
}

void StopInstance(const std::wstring& id, bool force) {
    HANDLE hp = nullptr;
    bool adbOk = false;
    std::wstring serial;
    std::wstring adb = AdbExe();
    {
        std::lock_guard<std::mutex> lk(g_mx);
        Runtime* r = Rt(id);
        InstanceCfg* c = Inst(id);
        if (!r || !r->hProc) return;
        if (r->st == St::Stopped || r->st == St::Stopping) return;
        hp = r->hProc;
        adbOk = r->adbOk;
        if (c) serial = AdbSerial(*c);
        r->st = St::Stopping;
        r->busy = false;
    }
    NotifyStatus(id);
    ILog(id, L"launcher", force ? L"force stop requested" : L"graceful stop requested");
    std::thread([id, hp, adbOk, serial, adb, force] {
        if (!force && adbOk && !adb.empty()) {
            DWORD ec = 0; std::string so, se;
            RunCapture(adb, L"-s " + serial + L" shell reboot -p", L"", 6000, &ec, &so, &se);
        }
        DWORD w = WaitForSingleObject(hp, force ? 2000 : 20000);
        if (w != WAIT_OBJECT_0) {
            TerminateProcess(hp, 0);
            WaitForSingleObject(hp, 5000);
        }
        // note: process handle is closed by the QEMU exit monitor (single owner)
        {
            std::lock_guard<std::mutex> lk(g_mx);
            Runtime* r = Rt(id);
            if (r) {
                r->adbOk = false;
                r->st = St::Stopped;
            }
        }
        ILog(id, L"launcher", L"stopped");
        NotifyStatus(id);
    }).detach();
}

void RestartInstance(const std::wstring& id) {
    std::thread([id] {
        // graceful adb reboot of the guest
        std::wstring adb = AdbExe(), serial;
        bool adbOk = false;
        {
            std::lock_guard<std::mutex> lk(g_mx);
            Runtime* r = Rt(id);
            InstanceCfg* c = Inst(id);
            if (r && c) { adbOk = r->adbOk; serial = AdbSerial(*c); }
        }
        if (adbOk && !adb.empty()) {
            DWORD ec; std::string so, se;
            RunCapture(adb, L"-s " + serial + L" shell reboot", L"", 6000, &ec, &so, &se);
        } else {
            StopInstance(id, false);
            SleepMs(3000);
            StartInstance(id, serial);
        }
    }).detach();
}

// ---------------------------------------------------------------- ADB
bool AdbConnect(const InstanceCfg& c, std::wstring& err) {
    std::wstring adb = AdbExe();
    if (adb.empty()) { err = T(S_MB_ADBMISS); return false; }
    DWORD ec = 0; std::string so, se;
    for (int i = 0; i < 3; ++i) {
        RunCapture(adb, L"connect " + AdbSerial(c), L"", 5000, &ec, &so, &se);
        std::wstring out = U2W(so) + U2W(se);
        if (out.find(L"connected") != std::wstring::npos ||
            out.find(L"already") != std::wstring::npos) return true;
        SleepMs(800);
    }
    err = L"adb connect failed: " + U2W(se) + U2W(so);
    return false;
}

bool AdbInstall(const std::wstring& id, const std::wstring& apk, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    if (FileExt(apk) != L".apk") { err = T(S_MB_BADAPK); return false; }
    std::wstring adb = AdbExe();
    if (adb.empty()) { err = T(S_MB_ADBMISS); return false; }
    if (!c->adbEnabled) { err = L"ADB disabled for this instance"; return false; }
    std::wstring cerr;
    if (!AdbConnect(*c, cerr)) { err = cerr; return false; }
    ILog(id, L"adb", L"install " + apk);
    DWORD ec = 0; std::string so, se;
    RunCapture(adb, L"-s " + AdbSerial(*c) + L" install -r -g " + Qn(apk),
               L"", 600000, &ec, &so, &se);
    std::wstring out = U2W(so) + U2W(se);
    ILog(id, L"adb", L"install exit=" + std::to_wstring((long long)ec) + L" " + out);
    if (out.find(L"Success") != std::wstring::npos) return true;
    err = out.empty() ? L"adb install failed" : out;
    return false;
}

bool AdbPush(const std::wstring& id, const std::wstring& file, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring adb = AdbExe();
    if (adb.empty()) { err = T(S_MB_ADBMISS); return false; }
    std::wstring cerr;
    if (!AdbConnect(*c, cerr)) { err = cerr; return false; }
    ILog(id, L"adb", L"push " + file);
    DWORD ec = 0; std::string so, se;
    RunCapture(adb, L"-s " + AdbSerial(*c) + L" push " + Qn(file) + L" /sdcard/Download/",
               L"", 600000, &ec, &so, &se);
    std::wstring out = U2W(so) + U2W(se);
    ILog(id, L"adb", L"push exit=" + std::to_wstring((long long)ec) + L" " + out);
    if (out.find(L"pushed") != std::wstring::npos || ec == 0) return true;
    err = out.empty() ? L"adb push failed" : out;
    return false;
}

bool AdbPullDownloads(const std::wstring& id, const std::wstring& localDir, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring adb = AdbExe();
    if (adb.empty()) { err = T(S_MB_ADBMISS); return false; }
    std::wstring cerr;
    if (!AdbConnect(*c, cerr)) { err = cerr; return false; }
    MK(localDir);
    ILog(id, L"adb", L"pull /sdcard/Download/ -> " + localDir);
    DWORD ec = 0; std::string so, se;
    RunCapture(adb, L"-s " + AdbSerial(*c) + L" pull /sdcard/Download/ " + Qn(localDir),
               L"", 600000, &ec, &so, &se);
    std::wstring out = U2W(so) + U2W(se);
    ILog(id, L"adb", L"pull exit=" + std::to_wstring((long long)ec) + L" " + out);
    if (out.find(L"pulled") != std::wstring::npos || ec == 0) return true;
    err = out.empty() ? L"adb pull failed" : out;
    return false;
}

bool AdbScreenshot(const std::wstring& id, std::wstring& outFile, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring adb = AdbExe();
    if (adb.empty()) { err = T(S_MB_ADBMISS); return false; }
    std::wstring cerr;
    if (!AdbConnect(*c, cerr)) { err = cerr; return false; }
    MK(g_p.shots);
    outFile = g_p.shots + L"\\screenshot-" + BaseName(c->id) + L"-" + NowFileStamp() + L".png";
    int ec = 0;
    // exec-out screencap -p streams binary PNG to stdout -> file (binary-safe)
    RunToFile(adb, L"-s " + AdbSerial(*c) + L" exec-out screencap -p",
              outFile, g_p.cache + L"\\adb-err.log", L"", 30000, &ec);
    if (ec == 0 && FE(outFile) && FileSizeOf(outFile) > 100) {
        ILog(id, L"adb", L"screenshot " + outFile);
        return true;
    }
    err = L"screencap failed";
    return false;
}

bool AdbShell(const std::wstring& id, const std::wstring& cmd, std::wstring& out, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    std::wstring adb = AdbExe();
    if (adb.empty()) { err = T(S_MB_ADBMISS); return false; }
    DWORD ec = 0; std::string so, se;
    RunCapture(adb, L"-s " + AdbSerial(*c) + L" shell " + cmd, L"", 15000, &ec, &so, &se);
    out = U2W(so); err = U2W(se);
    return ec == 0;
}
bool AdbGetProp(const std::wstring& id, const wchar_t* prop, std::wstring& val) {
    std::wstring out, err;
    if (!AdbShell(id, std::wstring(L"getprop ") + prop, out, err)) return false;
    val = TrimW(out);
    return !val.empty();
}
bool AdbKey(const std::wstring& id, int keycode, std::wstring& err) {
    std::wstring out;
    return AdbShell(id, Fmt(L"input keyevent %d", keycode), out, err);
}

// ---------------------------------------------------------------- diagnostics
std::vector<DiagItem> RunDiagnostics() {
    std::vector<DiagItem> d;
    auto ok   = [&d](const wchar_t* n, const std::wstring& m) { d.push_back({ n, m, 0 }); };
    auto warn = [&d](const wchar_t* n, const std::wstring& m) { d.push_back({ n, m, 1 }); };
    auto fail = [&d](const wchar_t* n, const std::wstring& m) { d.push_back({ n, m, 2 }); };
    auto info = [&d](const wchar_t* n, const std::wstring& m) { d.push_back({ n, m, 3 }); };

    // Windows version
    info(L"Windows", WinVerString());

    // RAM (TZ: 16 GB recommended, 8 GB min)
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    uint64_t totalGb = ms.ullTotalPhys / (1024ULL * 1024 * 1024);
    if (totalGb >= 16) ok(L"RAM", Fmt(L"%llu GB - OK", (unsigned long long)totalGb));
    else if (totalGb >= 8) warn(L"RAM", Fmt(L"%llu GB - мало для комфортной работы (рекомендуется 16 GB)", (unsigned long long)totalGb));
    else fail(L"RAM", Fmt(L"%llu GB - недостаточно (минимум 8 GB, рекомендуется 16 GB)", (unsigned long long)totalGb));

    // Disk free on data root
    ULARGE_INTEGER freeB, totalB;
    GetDiskFreeSpaceExW(g_p.dataRoot.c_str(), &freeB, &totalB, nullptr);
    uint64_t freeGb = freeB.QuadPart / (1024ULL * 1024 * 1024);
    if (freeGb >= 40) ok(L"Диск", Fmt(L"свободно %llu GB - OK", (unsigned long long)freeGb));
    else if (freeGb >= 20) warn(L"Диск", Fmt(L"свободно %llu GB - рекомендуется не менее 40 GB", (unsigned long long)freeGb));
    else fail(L"Диск", Fmt(L"свободно %llu GB - недостаточно (нужно минимум 40 GB)", (unsigned long long)freeGb));

    // CPU virtualization in firmware
    if (VirtFirmwareEnabled())
        ok(L"Виртуализация CPU", L"VT-x / AMD SVM включена в BIOS/UEFI");
    else
        fail(L"Виртуализация CPU",
             L"Аппаратная виртуализация не обнаружена. Включите Intel VT-x или AMD SVM в BIOS/UEFI и перезагрузите ПК.");

    // WHPX
    if (WhpxAvailable())
        ok(L"WHPX", L"Windows Hypervisor Platform доступна");
    else
        fail(L"WHPX",
             L"Windows Hypervisor Platform не активна. Включите компонент «Windows Hypervisor Platform» "
             L"(DISM /online /enable-feature /featurename:HypervisorPlatform) и перезагрузите ПК.");

    // Hyper-V service (informational)
    {
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm) {
            SC_HANDLE svc = OpenServiceW(scm, L"vmms", SERVICE_QUERY_STATUS);
            if (svc) { info(L"Hyper-V", L"служба управления Hyper-V установлена"); CloseServiceHandle(svc); }
            else info(L"Hyper-V", L"Hyper-V не установлен (не обязателен, WHPX достаточно)");
            CloseServiceHandle(scm);
        }
    }

    // QEMU
    std::wstring qemu = QemuExe();
    if (qemu.empty()) {
        fail(L"QEMU", T(S_MB_QEMUMISS));
    } else {
        DWORD ec = 0; std::string so, se;
        RunCapture(qemu, L"--version", L"", 10000, &ec, &so, &se);
        std::wstring v = TrimW(U2W(so));
        size_t nl = v.find(L'\n');
        if (nl != std::wstring::npos) v = v.substr(0, nl);
        ok(L"QEMU", v.empty() ? qemu : v);
        // accelerator support inside qemu build
        RunCapture(qemu, L"-accel help", L"", 10000, &ec, &so, &se);
        std::wstring acc = LowerW(U2W(so) + U2W(se));
        if (acc.find(L"whpx") != std::wstring::npos)
            info(L"QEMU accel", L"сборка QEMU поддерживает WHPX");
        else
            warn(L"QEMU accel", L"в этой сборке QEMU нет WHPX - будет использован медленный TCG");
    }

    // ADB
    std::wstring adb = AdbExe();
    if (adb.empty()) {
        fail(L"ADB", T(S_MB_ADBMISS));
    } else {
        DWORD ec = 0; std::string so, se;
        RunCapture(adb, L"version", L"", 10000, &ec, &so, &se);
        std::wstring v = TrimW(U2W(so));
        size_t nl = v.find(L'\n');
        if (nl != std::wstring::npos) v = v.substr(0, nl);
        ok(L"ADB", v.empty() ? adb : v);
    }

    // Android image
    std::wstring img = g_set.defaultImagePath;
    for (auto& c : g_insts) if (!c.imagePath.empty()) { img = c.imagePath; break; }
    if (!img.empty() && FE(img))
        ok(L"Android-образ", BaseName(img) + L" (" + HumanSize(FileSizeOf(img)) + L")");
    else if (img.empty())
        warn(L"Android-образ", L"не выбран. Скачайте Android-x86 ISO и укажите его в настройках инстанса.");
    else
        fail(L"Android-образ", L"файл не найден: " + img);

    // GPU
    {
        DISPLAY_DEVICEW dd = { sizeof(dd) };
        if (EnumDisplayDevicesW(nullptr, 0, &dd, 0)) {
            std::wstring name = dd.DeviceString;
            DISPLAY_DEVICEW mon = { sizeof(mon) };
            std::wstring drvVer, drvDate;
            if (EnumDisplayDevicesW(dd.DeviceName, 0, &mon, EDD_GET_DEVICE_INTERFACE_NAME) ||
                EnumDisplayDevicesW(dd.DeviceName, 1, &mon, 0)) {
                // registry key with driver version
                HKEY k;
                std::wstring key = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
                    wchar_t buf[256] = {}; DWORD sz = sizeof(buf);
                    if (RegQueryValueExW(k, L"DriverVersion", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
                        drvVer = buf;
                    sz = sizeof(buf);
                    if (RegQueryValueExW(k, L"DriverDate", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
                        drvDate = buf;
                    RegCloseKey(k);
                }
            }
            std::wstring m = name + (drvVer.empty() ? L"" : L" - драйвер " + drvVer);
            if (!drvDate.empty()) m += L" (" + drvDate + L")";
            info(L"GPU", m);
        }
    }
    // stage 3 GPU diagnostics (TZ3 6.3)
    {
        auto gpu = V3GpuDiag();
        for (auto& it : gpu) d.push_back(it);
        V3GpuProbeQemu();
    }
    return d;
}
