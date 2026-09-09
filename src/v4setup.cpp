// ============================================================================
//  NovaDroid - v4setup.cpp  Stage 5 "Installer / Uninstaller" (TZ5 3.1).
//  Self-extracting setup stub: payload (launcher exe) appended to this exe
//  with trailing [8-byte size]["NDSETUP10"]. Runs system checks (VT-x/WHPX/
//  RAM/disk), installs into Program Files, creates shortcuts, writes the
//  uninstall registry entry, offers "run after install". /uninstall mode
//  removes files and asks about keeping user data. /S = silent install.
//  Admin rights: resources/setup.manifest (requireAdministrator).
// ============================================================================
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>

#pragma comment(ignored)   // (no-op; mingw ignores)

static bool FE(const wchar_t* p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

struct SysCheck { bool virt, whpx; long long ramMb, diskGb; };

static SysCheck CheckSystem() {
    SysCheck r = {};
    r.virt = IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != 0;
    // WHPX: query via optional feature presence (winhvr.sys driver)
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\winhvr",
                      0, KEY_READ, &hk) == ERROR_SUCCESS) { r.whpx = true; RegCloseKey(hk); }
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    r.ramMb = (long long)(ms.ullTotalPhys >> 20);
    wchar_t winPath[MAX_PATH]; GetWindowsDirectoryW(winPath, MAX_PATH);
    ULARGE_INTEGER fr;
    winPath[3] = 0;
    if (GetDiskFreeSpaceExW(winPath, &fr, nullptr, nullptr)) r.diskGb = (long long)(fr.QuadPart >> 30);
    return r;
}

static bool ExtractPayload(const wchar_t* dst) {
    // find payload: read own file backwards
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    HANDLE h = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    char tail[12];
    LARGE_INTEGER pos; pos.QuadPart = sz.QuadPart - 12;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    DWORD rd = 0; ReadFile(h, tail, 12, &rd, nullptr);
    if (rd != 12 || memcmp(tail + 4, "NDSETUP10", 9) != 0) { CloseHandle(h); return false; }
    long long payload = *(long long*)tail;
    pos.QuadPart = sz.QuadPart - 12 - payload;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    HANDLE o = CreateFileW(dst, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (o == INVALID_HANDLE_VALUE) { CloseHandle(h); return false; }
    static char buf[1 << 20];
    long long left = payload;
    while (left > 0) {
        DWORD chunk = (DWORD)(left > (long long)sizeof(buf) ? sizeof(buf) : left);
        if (!ReadFile(h, buf, chunk, &rd, nullptr) || rd == 0) break;
        DWORD wr = 0;
        if (!WriteFile(o, buf, rd, &wr, nullptr) || wr != rd) break;
        left -= rd;
    }
    CloseHandle(o); CloseHandle(h);
    return left == 0;
}

static bool MakeLink(const wchar_t* dir, const wchar_t* name, const wchar_t* target,
                     const wchar_t* args, const wchar_t* workDir) {
    CreateDirectoryW(dir, nullptr);
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%s\\%s.lnk", dir, name);
    IShellLinkW* sl = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(target);
        sl->SetArguments(args);
        sl->SetWorkingDirectory(workDir);
        sl->SetDescription(L"NovaDroid Emulator");
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->Save(path, TRUE));
            pf->Release();
        }
        sl->Release();
    }
    return ok;
}

static void WriteRegValues(const wchar_t* installDir, const wchar_t* uninsPath) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NovaDroid",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        auto set = [&](const wchar_t* n, const wchar_t* v) {
            RegSetValueExW(hk, n, 0, REG_SZ, (const BYTE*)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
        };
        auto setD = [&](const wchar_t* n, DWORD v) {
            RegSetValueExW(hk, n, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        };
        set(L"DisplayName", L"NovaDroid Emulator");
        set(L"DisplayVersion", L"0.4.0");
        set(L"Publisher", L"NovaDroid Project");
        set(L"InstallLocation", installDir);
        wchar_t un[2 * MAX_PATH];
        swprintf(un, 2 * MAX_PATH, L"\"%s\" /uninstall", uninsPath);
        set(L"UninstallString", un);
        set(L"DisplayIcon", installDir);
        setD(L"NoModify", 1);
        setD(L"NoRepair", 1);
        RegCloseKey(hk);
    }
}

static int RunUninstall() {
    wchar_t self[MAX_PATH], dir[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wcscpy(dir, self);
    wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    int mb = MessageBoxW(nullptr,
        L"Удалить NovaDroid Emulator?\n\nUninstall NovaDroid Emulator?",
        L"NovaDroid - Uninstall", MB_YESNO | MB_ICONQUESTION);
    if (mb != IDYES) return 0;
    // keep data?
    int keep = MessageBoxW(nullptr,
        L"Сохранить данные пользователя (инстансы, бэкапы, логи) в %LOCALAPPDATA%\\NovaDroid?\n\n"
        L"Keep user data (instances, backups, logs)?",
        L"NovaDroid - Uninstall", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (keep == IDCANCEL) return 0;
    // stop launcher if running
    HWND w = FindWindowW(L"NovaDroidMain", nullptr);
    if (w) { PostMessageW(w, WM_CLOSE, 0, 0); Sleep(2500); }
    // remove registry + shortcuts
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NovaDroid");
    wchar_t desk[MAX_PATH], startm[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk);
    SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startm);
    DeleteFileW((std::wstring(desk) + L"\\NovaDroid Emulator.lnk").c_str());
    DeleteFileW((std::wstring(startm) + L"\\NovaDroid\\NovaDroid Emulator.lnk").c_str());
    RemoveDirectoryW((std::wstring(startm) + L"\\NovaDroid").c_str());
    // delete program files (self-delete via cmd delay)
    wchar_t cmd[MAX_PATH * 2];
    swprintf(cmd, MAX_PATH * 2,
        L"/C timeout /t 2 /nobreak >nul & rmdir /s /q \"%s\"", dir);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    GetSystemDirectoryW((wchar_t*)cmd, 0); // no-op
    wchar_t sysCmd[MAX_PATH * 3];
    swprintf(sysCmd, MAX_PATH * 3, L"cmd.exe %s", cmd);
    if (CreateProcessW(nullptr, sysCmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, dir, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    if (keep == IDNO) {
        wchar_t appData[MAX_PATH];
        SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
        swprintf(cmd, MAX_PATH * 2, L"/C timeout /t 3 /nobreak >nul & rmdir /s /q \"%s\\NovaDroid\"", appData);
        swprintf(sysCmd, MAX_PATH * 3, L"cmd.exe %s", cmd);
        if (CreateProcessW(nullptr, sysCmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, dir, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    MessageBoxW(nullptr, L"NovaDroid удалён. / NovaDroid uninstalled.",
                L"NovaDroid - Uninstall", MB_OK | MB_ICONINFORMATION);
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false, uninstall = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (!wcscmp(argv[i], L"/S") || !wcscmp(argv[i], L"/s")) silent = true;
        if (!wcscmp(argv[i], L"/uninstall")) uninstall = true;
    }
    if (uninstall) return RunUninstall();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ---- system check (TZ5 3.1)
    SysCheck sys = CheckSystem();
    if (!silent) {
        wchar_t msg[1024];
        swprintf(msg, 1024,
            L"Проверка системы / System check:\n\n"
            L"  Виртуализация VT-x/AMD-V: %s\n"
            L"  Windows Hypervisor Platform: %s\n"
            L"  RAM: %lld MB\n"
            L"  Свободно на диске: %lld GB\n\n"
            L"%s\n\nПродолжить установку? / Continue?",
            sys.virt ? L"OK" : L"НЕ НАЙДЕНА (включите в BIOS)",
            sys.whpx ? L"OK" : L"не включён (добавьте компонент Windows)",
            sys.ramMb, sys.diskGb,
            (sys.virt && sys.whpx) ? L"Требования выполнены."
                                   : L"ВНИМАНИЕ: без виртуализации эмулятор будет работать медленно или не запустится.");
        if (MessageBoxW(nullptr, msg, L"NovaDroid 0.4 - Setup",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return 1;
    }

    // ---- install dir
    wchar_t dir[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, dir);
    wcscat(dir, L"\\NovaDroid");
    if (!silent) {
        BROWSEINFOW bi = { nullptr, nullptr, nullptr,
                           (LPCWSTR)L"Выберите папку установки / Choose install folder",
                           BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE, nullptr, 0, 0 };
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            SHGetPathFromIDListW(pidl, dir);
            CoTaskMemFree(pidl);
        }
    }
    CreateDirectoryW(dir, nullptr);

    // ---- extract launcher
    wchar_t exePath[MAX_PATH];
    swprintf(exePath, MAX_PATH, L"%s\\NovaDroidLauncher.exe", dir);
    if (!ExtractPayload(exePath)) {
        if (!silent) MessageBoxW(nullptr, L"Ошибка извлечения файлов! / Payload extraction failed!",
                                 L"NovaDroid Setup", MB_OK | MB_ICONERROR);
        return 2;
    }

    // ---- uninstaller copy of self
    wchar_t self[MAX_PATH], unins[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    swprintf(unins, MAX_PATH, L"%s\\unins000.exe", dir);
    CopyFileW(self, unins, FALSE);

    // ---- shortcuts (desktop + start menu)
    wchar_t desk[MAX_PATH], startm[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk);
    SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startm);
    MakeLink(desk, L"NovaDroid Emulator", exePath, L"", dir);
    MakeLink(startm, L"NovaDroid Emulator", exePath, L"", dir);

    // ---- registry uninstall entry
    WriteRegValues(dir, unins);

    // ---- full package hint
    if (!silent)
        MessageBoxW(nullptr,
            L"Установка завершена!\n\nПри первом запуске NovaDroid автоматически скачает полный пакет "
            L"(QEMU + образ Android x86_64 + OpenGL Mesa + приложения F-Droid и APKPure) по прямой ссылке, "
            L"проверит SHA-256 и распакует.\n\nInstallation complete! On first launch the full package will be "
            L"downloaded from the direct link, verified and installed automatically.",
            L"NovaDroid 0.4 - Setup", MB_OK | MB_ICONINFORMATION);

    // ---- run after install?
    if (!silent && MessageBoxW(nullptr, L"Запустить NovaDroid сейчас? / Launch NovaDroid now?",
                               L"NovaDroid Setup", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(exePath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, dir, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    return 0;
}
