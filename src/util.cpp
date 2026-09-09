// ============================================================================
//  NovaDroid - util.cpp  Strings, filesystem, process execution, shell helpers.
// ============================================================================
#include "app.h"
#include <shobjidl.h>
#include <string.h>

// ---------------------------------------------------------------- string utils
std::wstring U2W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n);
    return r;
}
std::string W2U(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr);
    return r;
}

std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2048, fmt, ap);
    va_end(ap);
    return buf;
}

std::wstring TrimW(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::wstring LowerW(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
std::wstring RepAll(std::wstring s, const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(a, pos)) != std::wstring::npos) {
        s.replace(pos, a.size(), b);
        pos += b.size();
    }
    return s;
}
std::vector<std::wstring> SplitW(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
std::wstring BaseName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}
std::wstring FileExt(const std::wstring& path) {
    std::wstring b = BaseName(path);
    size_t p = b.find_last_of(L'.');
    return p == std::wstring::npos ? L"" : LowerW(b.substr(p));
}

// ---------------------------------------------------------------- time
static void FillSysTime(SYSTEMTIME& st) { GetLocalTime(&st); }
std::wstring NowIso() {
    SYSTEMTIME st; FillSysTime(st);
    return Fmt(L"%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
}
std::wstring NowFileStamp() {
    SYSTEMTIME st; FillSysTime(st);
    return Fmt(L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
}
std::wstring EpochToIso(long long ms) {
    if (ms <= 0) return L"";
    FILETIME ft;
    long long ll = (ms + 11644473600000LL) * 10000LL;
    ft.dwLowDateTime = (DWORD)ll; ft.dwHighDateTime = (DWORD)(ll >> 32);
    SYSTEMTIME st; FileTimeToSystemTime(&ft, &st);
    SYSTEMTIME loc; SystemTimeToTzSpecificLocalTime(nullptr, &st, &loc);
    return Fmt(L"%02d.%02d.%04d %02d:%02d", loc.wDay, loc.wMonth, loc.wYear, loc.wHour, loc.wMinute);
}
void SleepMs(int ms) { Sleep((DWORD)ms); }

// ---------------------------------------------------------------- fs
bool FE(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DE(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool MK(const std::wstring& path) {
    if (DE(path)) return true;
    std::error_code ec;
    fs::create_directories(fs::path(path), ec);
    return !ec && DE(path);
}
uint64_t FileSizeOf(const std::wstring& path) {
    std::error_code ec;
    auto sz = fs::file_size(fs::path(path), ec);
    return ec ? 0 : (uint64_t)sz;
}
std::wstring HumanSize(uint64_t b) {
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int ui = 0; double v = (double)b;
    while (v >= 1024.0 && ui < 4) { v /= 1024.0; ++ui; }
    if (ui == 0) return Fmt(L"%llu B", (unsigned long long)b);
    return Fmt(L"%.1f %s", v, u[ui]);
}
bool ReadText(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return false; }
    std::string data((size_t)sz.QuadPart, 0);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, &data[0], (DWORD)data.size(), &rd, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    data.resize(rd);
    // strip BOM
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) data.erase(0, 3);
    out = U2W(data);
    return true;
}
bool WriteText(const std::wstring& path, const std::wstring& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string u = W2U(data);
    DWORD wr = 0;
    BOOL ok = WriteFile(h, u.data(), (DWORD)u.size(), &wr, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}
bool CopyTree(const std::wstring& src, const std::wstring& dst) {
    std::error_code ec;
    fs::path s(src), d(dst);
    if (!fs::exists(s, ec)) return false;
    fs::create_directories(d, ec);
    // copy files
    for (auto& e : fs::recursive_directory_iterator(s, ec)) {
        if (ec) break;
        auto rel = fs::relative(e.path(), s, ec);
        if (ec) break;
        auto to = d / rel;
        if (e.is_directory(ec) && !ec) fs::create_directories(to, ec);
        else { std::error_code ec2; fs::copy_file(e.path(), to, fs::copy_options::overwrite_existing, ec2); }
    }
    return !ec;
}
bool DeleteTree(const std::wstring& path) {
    std::error_code ec;
    fs::remove_all(fs::path(path), ec);
    return !ec && !DE(path);
}

// ---------------------------------------------------------------- processes
std::wstring Qn(const std::wstring& s) {
    std::wstring r = L"\"";
    for (wchar_t c : s) { if (c == L'"') r += L'\\'; r += c; }
    r += L'"';
    return r;
}

bool RunCapture(const std::wstring& exe, const std::wstring& args,
                const std::wstring& cwd, DWORD timeoutMs,
                DWORD* exitCode, std::string* so, std::string* se) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE orR = nullptr, orW = nullptr, erR = nullptr, erW = nullptr;
    if (so && !CreatePipe(&orR, &orW, &sa, 0)) return false;
    if (se && !CreatePipe(&erR, &erW, &sa, 0)) { if (orR) { CloseHandle(orR); CloseHandle(orW); } return false; }
    if (orW) SetHandleInformation(orW, HANDLE_FLAG_INHERIT, 0);
    if (erW) SetHandleInformation(erW, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = Qn(exe) + L" " + args;
    std::wstring buf = cmd;
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = orW ? orW : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = erW ? erW : GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &buf[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (orW) CloseHandle(orW);
    if (erW) CloseHandle(erW);
    if (!ok) {
        if (orR) CloseHandle(orR);
        if (erR) CloseHandle(erR);
        return false;
    }

    std::thread tr;
    std::thread tw;
    if (orR) tr = std::thread([orR, so] {
        char tmp[4096]; DWORD rd;
        while (ReadFile(orR, tmp, sizeof(tmp), &rd, nullptr) && rd) { if (so) so->append(tmp, rd); }
        CloseHandle(orR);
    });
    if (erR) tw = std::thread([erR, se] {
        char tmp[4096]; DWORD rd;
        while (ReadFile(erR, tmp, sizeof(tmp), &rd, nullptr) && rd) { if (se) se->append(tmp, rd); }
        CloseHandle(erR);
    });

    DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs == 0 ? INFINITE : timeoutMs);
    if (w == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, (UINT)-1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    if (exitCode) {
        DWORD ec2 = 0;
        GetExitCodeProcess(pi.hProcess, &ec2);
        *exitCode = ec2;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (tr.joinable()) tr.join();
    if (tw.joinable()) tw.join();
    return true;
}

bool LaunchWithLogs(const std::wstring& exe, const std::wstring& args,
                    const std::wstring& cwd, const std::wstring& outFile,
                    const std::wstring& errFile, HANDLE* hProc, DWORD* pid) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE fo = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            &sa, CREATE_ALWAYS, 0, nullptr);
    HANDLE fe = CreateFileW(errFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            &sa, CREATE_ALWAYS, 0, nullptr);
    if (fo == INVALID_HANDLE_VALUE || fe == INVALID_HANDLE_VALUE) {
        if (fo != INVALID_HANDLE_VALUE) CloseHandle(fo);
        if (fe != INVALID_HANDLE_VALUE) CloseHandle(fe);
        return false;
    }
    std::wstring cmd = Qn(exe) + L" " + args;
    std::wstring buf = cmd;
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = nullptr;
    si.hStdOutput = fo;
    si.hStdError  = fe;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &buf[0], nullptr, nullptr, TRUE,
                             0, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(fo);
    CloseHandle(fe);
    if (!ok) return false;
    if (hProc) *hProc = pi.hProcess; else CloseHandle(pi.hProcess);
    if (pid) *pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    return true;
}

bool RunToFile(const std::wstring& exe, const std::wstring& args,
               const std::wstring& outFile, const std::wstring& errFile,
               const std::wstring& cwd, DWORD timeoutMs, int* exitCode) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE fo = CreateFileW(outFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            &sa, CREATE_ALWAYS, 0, nullptr);
    if (fo == INVALID_HANDLE_VALUE) return false;
    HANDLE fe = CreateFileW(errFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            &sa, CREATE_ALWAYS, 0, nullptr);
    std::wstring cmd = Qn(exe) + L" " + args;
    std::wstring buf = cmd;
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = fo;
    si.hStdError  = fe != INVALID_HANDLE_VALUE ? fe : fo;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &buf[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(fo);
    if (fe != INVALID_HANDLE_VALUE) CloseHandle(fe);
    if (!ok) return false;
    DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs == 0 ? INFINITE : timeoutMs);
    if (w == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, (UINT)-1); WaitForSingleObject(pi.hProcess, 5000); }
    DWORD ec2 = 0;
    GetExitCodeProcess(pi.hProcess, &ec2);
    if (exitCode) *exitCode = (int)ec2;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

bool ProcAlive(HANDLE h) {
    if (!h) return false;
    DWORD c = 0;
    if (!GetExitCodeProcess(h, &c)) return false;
    return c == STILL_ACTIVE;
}
uint64_t ProcRamBytes(HANDLE h) {
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS_EX pm = { sizeof(pm) };
    if (!GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pm, sizeof(pm))) return 0;
    return (uint64_t)pm.WorkingSetSize;
}

// ---------------------------------------------------------------- shell
bool ShellOpen(const std::wstring& pathOrUrl) {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", pathOrUrl.c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}
void OpenInExplorer(const std::wstring& path) {
    if (FE(path)) ShellExecuteW(nullptr, L"select", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(nullptr, L"explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH + 2] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}
std::wstring LocalAppData() {
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
        std::wstring r(p);
        CoTaskMemFree(p);
        return r;
    }
    return ExeDir();
}
std::wstring DesktopDir() {
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p))) {
        std::wstring r(p);
        CoTaskMemFree(p);
        return r;
    }
    return ExeDir();
}
std::wstring WinVerString() {
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW oi = { sizeof(oi) };
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) {
        auto f = (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion");
        if (f && f(&oi) == 0) {
            if (oi.dwBuildNumber >= 22000) return Fmt(L"Windows 11 (build %lu)", oi.dwBuildNumber);
            return Fmt(L"Windows %lu.%lu (build %lu)", oi.dwMajorVersion, oi.dwMinorVersion, oi.dwBuildNumber);
        }
    }
    return L"Windows";
}

bool MakeShortcut(const std::wstring& nameNoExt, const std::wstring& exe,
                  const std::wstring& args, const std::wstring& workDir) {
    static const CLSID c_CLSID_ShellLink = { 0x00021401, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
    static const IID c_IID_IShellLinkW  = { 0x000214F9, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
    static const IID c_IID_IPersistFile = { 0x0000010B, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
    IShellLinkW* sl = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(c_CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   c_IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(exe.c_str());
        sl->SetArguments(args.c_str());
        sl->SetWorkingDirectory(workDir.c_str());
        sl->SetDescription(L"NovaDroid Emulator");
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(c_IID_IPersistFile, (void**)&pf))) {
            std::wstring lnk = DesktopDir() + L"\\" + nameNoExt + L".lnk";
            ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
            pf->Release();
        }
        sl->Release();
    }
    return ok;
}

void CopyToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* p = GlobalLock(g);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

std::wstring PickFile(HWND hwnd, const wchar_t* filter, const wchar_t* title, const wchar_t* defExt) {
    wchar_t buf[MAX_PATH * 4] = {};
    OPENFILENAMEW of = { sizeof(of) };
    of.hwndOwner = hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 4;
    of.lpstrTitle = title;
    of.lpstrDefExt = defExt;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return L"";
    return buf;
}
std::wstring PickFolder(HWND hwnd, const wchar_t* title) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = {};
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? std::wstring(path) : L"";
}
