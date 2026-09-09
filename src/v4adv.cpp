// ============================================================================
//  NovaDroid - v4adv.cpp  Stage 6 "Advanced" (TZ6):
//  localhost browser stream (MJPEG screen + control page), plugin system
//  (DLL + manifest + hash check), cloud backups (WebDAV/Yandex.Disk/Dropbox/
//  OneDrive via OAuth token, AES-256 encrypted).
// ============================================================================
#include "v4.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <gdiplus.h>
#include <winhttp.h>

// ================================================================ browser stream (TZ6 4.1)
// MJPEG over HTTP from `adb exec-out screencap` (PNG) transcoded to JPEG by GDI+
// plus a control page (tap/keys/text). Localhost only + token (TZ6 4.1).
namespace {

std::string FmtA(const char* fmt, ...) {
    char buf[8192];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

std::atomic<bool> g_srvRun{ false };
std::wstring g_srvInstance, g_srvToken, g_srvUrl;
unsigned short g_srvPort = 8080;
SOCKET g_srvSock = INVALID_SOCKET;
std::thread g_srvThread, g_capThread;
std::mutex g_jpegMx;
std::vector<uint8_t> g_jpeg;          // latest frame
long long g_jpegMs = 0;
static ULONG_PTR g_gdipToken = 0;

std::string WToUtf8(const std::wstring& w) { std::string s = W2U(w); return s; }

void SendAll(SOCKET s, const char* p, int n) {
    while (n > 0) {
        int w = send(s, p, n, 0);
        if (w <= 0) return;
        p += w; n -= w;
    }
}
void SendStr(SOCKET s, const std::string& t) { SendAll(s, t.data(), (int)t.size()); }

bool RecvLine(SOCKET s, std::string& line) {
    char c; line.clear();
    for (int i = 0; i < 8192; ++i) {
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return false;
        if (c == '\n') { if (!line.empty() && line.back() == '\r') line.pop_back(); return true; }
        line += c;
    }
    return true;
}

// GDI+ PNG -> JPEG (quality 62)
bool PngToJpeg(const std::vector<uint8_t>& png, std::vector<uint8_t>& jpeg) {
    using namespace Gdiplus;
    if (!g_gdipToken) {
        GdiplusStartupInput in;
        if (GdiplusStartup(&g_gdipToken, &in, nullptr) != Ok) return false;
    }
    IStream* src = SHCreateMemStream(png.data(), (UINT)png.size());
    if (!src) return false;
    Bitmap bmp(src, FALSE);
    src->Release();
    if (bmp.GetLastStatus() != Ok) return false;
    IStream* dst = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &dst) != S_OK) return false;
    // JPEG encoder clsid
    CLSID clsid;
    UINT n = 0, got = 0;
    GetImageEncodersSize(&n, nullptr);
    std::vector<ImageCodecInfo> ci(n);
    GetImageEncoders(n, n * sizeof(ImageCodecInfo), ci.data());
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(ci[i].MimeType, L"image/jpeg") == 0) { clsid = ci[i].Clsid; got = 1; }
    bool ok = false;
    if (got) {
        EncoderParameters ep = {};
        ep.Count = 1;
        ep.Parameter[0].Guid = EncoderQuality;
        ep.Parameter[0].Type = EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        LONG q = 62;
        ep.Parameter[0].Value = &q;
        Status st = bmp.Save(dst, &clsid, &ep);
        ok = (st == Ok);
    }
    if (ok) {
        HGLOBAL hg = nullptr;
        if (GetHGlobalFromStream(dst, &hg) == S_OK) {
            SIZE_T sz = GlobalSize(hg);
            void* p = GlobalLock(hg);
            if (p) {
                jpeg.assign((uint8_t*)p, (uint8_t*)p + sz);
                GlobalUnlock(hg);
            }
        }
    }
    dst->Release();
    return ok && !jpeg.empty();
}

void CaptureLoop() {
    std::wstring adb = AdbExe();
    std::wstring serial = g_srvInstance.empty() ? L"" : AdbSerial(*Inst(g_srvInstance));
    if (adb.empty()) return;
    while (g_srvRun.load()) {
        std::string png;
        DWORD ec = 0;
        // exec-out screencap writes binary PNG to stdout
        RunCapture(adb, Fmt(L"-s %s exec-out screencap -p", serial.c_str()), L"",
                   15000, &ec, &png, nullptr);
        if (png.size() > 8 && png[0] == (char)0x89 && png[1] == 'P') {
            std::vector<uint8_t> j;
            if (PngToJpeg(std::vector<uint8_t>(png.begin(), png.end()), j)) {
                std::lock_guard<std::mutex> lk(g_jpegMx);
                g_jpeg = std::move(j);
                g_jpegMs = GetTickCount64();
            }
        } else {
            SleepMs(800);                       // device busy / booting
        }
        SleepMs(g_srvRun.load() ? 120 : 0);     // ~6-8 fps
    }
}

const char* ControlPageHtml =
"<!doctype html><html><head><meta charset='utf-8'><title>NovaDroid Stream</title>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>body{background:#111318;color:#f6f8fc;font-family:sans-serif;text-align:center;margin:0;padding:8px}"
"#v{max-width:100%;border-radius:8px;cursor:crosshair;touch-action:none}"
"button{background:#6C63FF;border:0;color:#fff;padding:8px 14px;border-radius:8px;margin:3px;font-size:14px}"
"input{padding:8px;border-radius:8px;border:1px solid #2a3342;background:#202735;color:#fff;width:60%%}</style></head>"
"<body><h3>NovaDroid</h3>"
"<img id='v' src='/stream?token=%s'>"
"<div><button onclick=\"k(4)\">Back</button><button onclick=\"k(3)\">Home</button>"
"<button onclick=\"k(187)\">Rec</button><button onclick=\"k(24)\">Vol+</button><button onclick=\"k(25)\">Vol-</button></div>"
"<div><input id='t' placeholder='text'><button onclick=\"tx()\">Send</button></div>"
"<script>var T='%s';"
"function req(u){var x=new XMLHttpRequest();x.open('GET',u);x.send();}"
"function k(c){req('/input?token='+T+'&action=key&key='+c);}"
"function tx(){req('/input?token='+T+'&action=text&text='+encodeURIComponent(document.getElementById('t').value));}"
"var v=document.getElementById('v');"
"function pos(e){var r=v.getBoundingClientRect();return [Math.round((e.clientX-r.left)/r.width*10000),Math.round((e.clientY-r.top)/r.height*10000)];}"
"v.onpointerdown=function(e){var p=pos(e);req('/input?token='+T+'&action=down&x='+p[0]+'&y='+p[1]);};"
"v.onpointerup=function(e){var p=pos(e);req('/input?token='+T+'&action=up&x='+p[0]+'&y='+p[1]);};"
"</script></body></html>";

std::string UrlParam(const std::string& q, const char* key) {
    size_t p = q.find(key);
    if (p == std::wstring::npos && p == std::string::npos) return "";
    if (p == std::string::npos) return "";
    p += strlen(key);
    if (p >= q.size() || q[p] != '=') return "";
    size_t e = q.find('&', p);
    if (e == std::string::npos) e = q.size();
    std::string v = q.substr(p + 1, e - p - 1);
    // percent decode
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '%' && i + 2 < v.size()) {
            auto hx = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
            out += (char)((hx(v[i + 1]) << 4) | hx(v[i + 2]));
            i += 2;
        } else if (v[i] == '+') out += ' ';
        else out += v[i];
    }
    return out;
}

void AdbInput(const std::string& action, const std::string& x, const std::string& y,
              const std::string& key, const std::string& text) {
    if (g_srvInstance.empty()) return;
    std::wstring adb = AdbExe();
    if (adb.empty()) return;
    std::wstring serial = AdbSerial(*Inst(g_srvInstance));
    std::wstring cmd;
    // coords arrive as 1/10000 of screen; rescale to guest resolution
    InstanceCfg* c = Inst(g_srvInstance);
    int gw = c ? c->resW : 1280, gh = c ? c->resH : 720;
    if (action == "down" || action == "up") {
        int px = atoi(x.c_str()) * gw / 10000;
        int py = atoi(y.c_str()) * gh / 10000;
        if (action == "down") cmd = Fmt(L"-s %s shell input swipe %d %d %d %d 80", serial.c_str(), px, py, px, py);
        else cmd = Fmt(L"-s %s shell input swipe %d %d %d %d 40", serial.c_str(), px, py, px, py);
    } else if (action == "key") {
        cmd = Fmt(L"-s %s shell input keyevent %d", serial.c_str(), atoi(key.c_str()));
    } else if (action == "text") {
        std::wstring t = U2W(text);
        for (wchar_t ch : t) {
            if (iswalnum(ch)) cmd += ch; else cmd += Fmt(L"\\u%04x ", ch);
        }
        cmd = Fmt(L"-s %s shell input text \"%s\"", serial.c_str(), cmd.c_str());
    }
    if (!cmd.empty()) {
        DWORD ec = 0; std::string so;
        RunCapture(adb, cmd, L"", 8000, &ec, &so, nullptr);
    }
}

void ClientThread(SOCKET cs) {
    std::string req;
    if (!RecvLine(cs, req)) { closesocket(cs); return; }
    // drain headers
    std::string line;
    while (RecvLine(cs, line) && !line.empty()) {}
    // parse: GET /path?query HTTP/1.1
    std::string path, query;
    {
        size_t sp1 = req.find(' '), sp2 = req.rfind(' ');
        if (sp1 == std::string::npos || sp2 == std::string::npos || sp2 <= sp1) { closesocket(cs); return; }
        std::string uri = req.substr(sp1 + 1, sp2 - sp1 - 1);
        size_t q = uri.find('?');
        path = q == std::string::npos ? uri : uri.substr(0, q);
        query = q == std::string::npos ? "" : uri.substr(q + 1);
    }
    if (UrlParam(query, "token") != WToUtf8(g_srvToken)) {
        SendStr(cs, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
        closesocket(cs); return;
    }
    if (path == "/" || path == "/index.html") {
        char buf[4096];
        std::string html = FmtA(ControlPageHtml, WToUtf8(g_srvToken).c_str(), WToUtf8(g_srvToken).c_str());
        int n = _snprintf(buf, sizeof(buf),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
            (int)html.size());
        SendAll(cs, buf, n);
        SendStr(cs, html);
    } else if (path == "/stream") {
        SendStr(cs, "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=novabnd\r\nCache-Control: no-cache\r\n\r\n");
        while (g_srvRun.load()) {
            std::vector<uint8_t> j;
            {
                std::lock_guard<std::mutex> lk(g_jpegMx);
                j = g_jpeg;
            }
            if (!j.empty()) {
                char hdr[128];
                int n = _snprintf(hdr, sizeof(hdr), "--novabnd\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", (int)j.size());
                SendAll(cs, hdr, n);
                SendAll(cs, (const char*)j.data(), (int)j.size());
                SendStr(cs, "\r\n");
            }
            Sleep(150);
        }
    } else if (path == "/input") {
        AdbInput(UrlParam(query, "action"), UrlParam(query, "x"), UrlParam(query, "y"),
                 UrlParam(query, "key"), UrlParam(query, "text"));
        SendStr(cs, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
    } else {
        SendStr(cs, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    }
    closesocket(cs);
}

void ServerLoop() {
    while (g_srvRun.load()) {
        sockaddr_in ca; int cl = sizeof(ca);
        SOCKET cs = accept(g_srvSock, (sockaddr*)&ca, &cl);
        if (cs == INVALID_SOCKET) { Sleep(200); continue; }
        std::thread(ClientThread, cs).detach();
    }
}

} // namespace

bool V4StreamStart(const std::wstring& instanceId, unsigned short port, std::wstring& err) {
    if (g_srvRun.load()) { err = T(S4_STR_BUSY); return false; }
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { err = L"WSAStartup failed"; return false; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = L"socket failed"; WSACleanup(); return false; }
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);          // localhost only (TZ6 4.1)
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 8) != 0) {
        closesocket(s); WSACleanup();
        err = Fmt(T(S4_STR_PORTBUSY), (int)port);
        return false;
    }
    g_srvSock = s;
    g_srvPort = port;
    g_srvInstance = instanceId;
    wchar_t tok[17];
    for (int i = 0; i < 16; ++i) {
        int c = rand() % 36;
        tok[i] = c < 10 ? L'0' + c : L'a' + c - 10;
    }
    tok[16] = 0;
    g_srvToken = tok;
    g_srvUrl = Fmt(L"http://localhost:%d/?token=%s", (int)port, tok);
    g_srvRun = true;
    g_srvThread = std::thread(ServerLoop);
    g_capThread = std::thread(CaptureLoop);
    LogW(L"stream", L"started for %s on port %d", instanceId.c_str(), (int)port);
    return true;
}

void V4StreamStop() {
    if (!g_srvRun.load()) return;
    g_srvRun = false;
    if (g_srvSock != INVALID_SOCKET) { closesocket(g_srvSock); g_srvSock = INVALID_SOCKET; }
    if (g_srvThread.joinable()) g_srvThread.join();
    if (g_capThread.joinable()) g_capThread.join();
    WSACleanup();
    LogW(L"stream", L"stopped");
}

bool V4StreamRunning() { return g_srvRun.load(); }
std::wstring V4StreamUrl() { return g_srvUrl; }
std::wstring V4StreamInstanceId() { return g_srvInstance; }

// ================================================================ plugins (TZ6 4.2)
static std::vector<PluginEntry> g_plugins;
static bool g_pluginsScanned = false;
static std::mutex g_pluginMx;

typedef void (*NovaPluginEventFn)(const wchar_t* event, const wchar_t* json);

static void PluginDispatchOne(const PluginEntry& p, const wchar_t* event, const std::wstring& json) {
    if (!p.enabled || !p.loaded) return;
    HMODULE h = GetModuleHandleW(p.name.c_str());
    if (!h) h = LoadLibraryExW(p.dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) return;
    NovaPluginEventFn fn = (NovaPluginEventFn)GetProcAddress(h, "NovaPlugin_Event");
    if (fn) fn(event, json.c_str());        // hash-checked; exceptions logged by host SEH elsewhere
}

void V4PluginsDispatch(const wchar_t* event, const std::wstring& json) {
    std::lock_guard<std::mutex> lk(g_pluginMx);
    for (auto& p : g_plugins) PluginDispatchOne(p, event, json);
}

void V4PluginsScan() {
    std::lock_guard<std::mutex> lk(g_pluginMx);
    g_plugins.clear();
    // state: dataRoot\plugins.json
    std::map<std::wstring, bool> en;
    std::wstring t;
    if (ReadText(g_p.dataRoot + L"\\plugins.json", t)) {
        JValue j; std::wstring err;
        if (JsonParse(t, j, err) && j.type == JValue::Arr)
            for (auto& e : j.arr)
                if (auto* n = e.find(L"name"))
                    en[n->asStr()] = e.find(L"enabled") ? e.find(L"enabled")->asBool(false) : false;
    }
    std::wstring root = g_p.exeDir + L"\\plugins";
    if (!DE(root)) { g_pluginsScanned = true; return; }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { g_pluginsScanned = true; return; }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        std::wstring dir = root + L"\\" + fd.cFileName;
        std::wstring mfPath = dir + L"\\manifest.json";
        std::wstring mft;
        if (!ReadText(mfPath, mft)) continue;
        JValue j; std::wstring err;
        if (!JsonParse(mft, j, err) || j.type != JValue::Obj) continue;
        PluginEntry p;
        p.dir = dir;
        p.manifestPath = mfPath;
        p.name = j.find(L"name") ? j.find(L"name")->asStr() : fd.cFileName;
        p.version = j.find(L"version") ? j.find(L"version")->asStr() : L"1.0";
        p.desc = j.find(L"description") ? j.find(L"description")->asStr() : L"";
        std::wstring dll = j.find(L"library") ? j.find(L"library")->asStr() : L"plugin.dll";
        p.dllPath = dir + L"\\" + dll;
        p.valid = FE(p.dllPath);
        // hash check (TZ6 4.2): optional <dll>.sha256 must match
        std::wstring shaPath = p.dllPath + L".sha256";
        if (FE(shaPath)) {
            std::wstring want;
            ReadText(shaPath, want);
            std::wstring got = Sha256OfFile(p.dllPath);
            std::wstring w = TrimW(want);
            // tolerate "hash  filename" format
            if (w.size() > 64) w = w.substr(0, 64);
            if (_wcsicmp(w.c_str(), got.c_str()) != 0) { p.valid = false; LogW(L"plugin", L"%s: sha mismatch", p.name.c_str()); }
        } else p.sha256 = Sha256OfFile(p.dllPath);
        p.enabled = en.count(p.name) ? en[p.name] : false;
        p.loaded = p.valid && p.enabled;
        g_plugins.push_back(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    g_pluginsScanned = true;
}

std::vector<PluginEntry>& V4Plugins() {
    if (!g_pluginsScanned) V4PluginsScan();
    return g_plugins;
}

bool V4PluginToggle(const std::wstring& name, bool enable) {
    {
        std::lock_guard<std::mutex> lk(g_pluginMx);
        for (auto& p : g_plugins)
            if (p.name == name) {
                if (enable && !p.valid) return false;
                p.enabled = enable;
                p.loaded = enable;
            }
    }
    // persist
    JValue j; j.type = JValue::Arr;
    for (auto& p : g_plugins) {
        JValue o; o.type = JValue::Obj;
        o.obj[L"name"] = JValue::MakeStr(p.name);
        o.obj[L"enabled"] = JValue::MakeBool(p.enabled);
        j.arr.push_back(o);
    }
    WriteText(g_p.dataRoot + L"\\plugins.json", JsonWrite(j));
    LogW(L"plugin", L"%s -> %s", name.c_str(), enable ? L"enabled" : L"disabled");
    return true;
}

// ================================================================ cloud backups (TZ6 4.3)
static CloudCfg g_cloud;
static std::mutex g_cloudMx;
static std::atomic<bool> g_cloudBusy{ false };
static std::wstring g_cloudStatus;

CloudCfg& V4Cloud() {
    std::lock_guard<std::mutex> lk(g_cloudMx);
    g_cloud.provider = g_set.cloudProvider;
    g_cloud.endpoint = g_set.cloudEndpoint;
    g_cloud.token = g_set.cloudToken;
    g_cloud.encrypt = g_set.cloudEncrypt;
    return g_cloud;
}

// AES-256-CBC encryption via BCrypt (key = PBKDF2(SHA256, token+salt))
static bool AesWrapFile(const std::wstring& src, const std::wstring& dst, bool encrypt, std::wstring& err) {
    BCRYPT_ALG_HANDLE alg = nullptr, kdf = nullptr;
    bool ok = false;
    std::vector<uint8_t> data;
    HANDLE fh = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (fh == INVALID_HANDLE_VALUE) { err = L"open failed"; return false; }
    LARGE_INTEGER sz; GetFileSizeEx(fh, &sz);
    data.resize((size_t)sz.QuadPart);
    DWORD rd = 0; ReadFile(fh, data.data(), (DWORD)data.size(), &rd, nullptr);
    CloseHandle(fh);
    std::wstring pass = g_set.cloudToken + L"|NovaDroid-v1";
    std::string passU = W2U(pass);
    uint8_t salt[16] = { 'N','o','v','a','D','r','o','i','d','S','a','l','t','0','0','1' };
    uint8_t key[32] = {};
    do {
        if (BCryptOpenAlgorithmProvider(&kdf, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) break;
        if (BCryptDeriveKeyPBKDF2(kdf, (PUCHAR)passU.data(), (ULONG)passU.size(),
                                  salt, sizeof(salt), 10000, key, sizeof(key), 0) != 0) break;
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) break;
        BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                          sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        BCRYPT_KEY_HANDLE kh;
        if (BCryptGenerateSymmetricKey(alg, &kh, nullptr, 0, key, sizeof(key), 0) != 0) break;
        uint8_t iv[16];
        memcpy(iv, salt, 16);
        ULONG outSz = 0;
        std::vector<uint8_t> out(data.size() + 32);
        NTSTATUS st;
        if (encrypt)
            st = BCryptEncrypt(kh, data.data(), (ULONG)data.size(), nullptr, iv, 16,
                               out.data(), (ULONG)out.size(), &outSz, BCRYPT_BLOCK_PADDING);
        else
            st = BCryptDecrypt(kh, data.data(), (ULONG)data.size(), nullptr, iv, 16,
                               out.data(), (ULONG)out.size(), &outSz, BCRYPT_BLOCK_PADDING);
        if (st == 0) {
            out.resize(outSz);
            HANDLE fo = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (fo != INVALID_HANDLE_VALUE) {
                DWORD wr = 0;
                ok = WriteFile(fo, out.data(), (DWORD)out.size(), &wr, nullptr) && wr == out.size();
                CloseHandle(fo);
            }
        } else err = L"bcrypt failed";
        BCryptDestroyKey(kh);
    } while (0);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (kdf) BCryptCloseAlgorithmProvider(kdf, 0);
    return ok;
}

static bool CloudUpload(const std::wstring& file, const std::wstring& remoteName, std::wstring& err) {
    CloudCfg c = V4Cloud();
    // read file
    HANDLE fh = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (fh == INVALID_HANDLE_VALUE) { err = L"open failed"; return false; }
    LARGE_INTEGER sz; GetFileSizeEx(fh, &sz);
    std::vector<char> body((size_t)sz.QuadPart);
    DWORD rd = 0; ReadFile(fh, body.data(), (DWORD)body.size(), &rd, nullptr);
    CloseHandle(fh);

    URL_COMPONENTSW uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;

    std::wstring method = L"PUT", url = c.endpoint, hdrs;
    if (c.provider == L"webdav" || c.provider == L"yadisk") {
        if (c.provider == L"yadisk")
            url = L"https://webdav.yandex.ru/" + remoteName;
        url += (url.back() == L'/' ? L"" : L"/") + remoteName;
        hdrs = c.provider == L"yadisk" ? (L"Authorization: OAuth " + c.token)
                                       : std::wstring(L"");   // basic handled below
        if (c.provider == L"webdav") {
            // token = user:pass -> base64
            std::string up = W2U(c.token);
            DWORD b64 = 0;
            CryptBinaryToStringA((const BYTE*)up.data(), (DWORD)up.size(),
                                 CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64);
            std::vector<char> b(b64);
            CryptBinaryToStringA((const BYTE*)up.data(), (DWORD)up.size(),
                                 CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b.data(), &b64);
            hdrs = L"Authorization: Basic " + U2W(std::string(b.data(), b64));
        }
    } else if (c.provider == L"dropbox") {
        method = L"POST";
        url = L"https://content.dropboxapi.com/2/files/upload";
        std::wstring arg = Fmt(L"{\"path\":\"/%s\",\"mode\":\"overwrite\"}", remoteName.c_str());
        hdrs = L"Authorization: Bearer " + c.token + L"\r\nDropbox-API-Arg: " + arg +
               L"\r\nContent-Type: application/octet-stream";
    } else if (c.provider == L"onedrive") {
        url = L"https://graph.microsoft.com/v1.0/me/drive/root:/" + remoteName + L":/content";
        hdrs = L"Authorization: Bearer " + c.token + L"\r\nContent-Type: application/octet-stream";
    } else {
        err = T(S4_CLD_NOPROV);
        return false;
    }
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) { err = L"bad url"; return false; }
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"NovaDroid/0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    WinHttpSetTimeouts(ses, 10000, 30000, 120000, 120000);
    HINTERNET con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
    if (con) {
        HINTERNET req = WinHttpOpenRequest(con, method.c_str(),
            path[0] ? path : L"/", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
        if (req) {
            // note: Dropbox-Arg header contains JSON; WinHTTP may need CR-safe headers
            std::wstring h2 = hdrs + Fmt(L"\r\nContent-Length: %lld", (long long)body.size());
            if (WinHttpSendRequest(req, h2.c_str(), (DWORD)h2.size(), body.data(),
                                   (DWORD)body.size(), (DWORD)body.size(), 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD st = 0, szSt = sizeof(st);
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    nullptr, &st, &szSt, nullptr);
                ok = (st >= 200 && st < 300);
                if (!ok) err = Fmt(L"HTTP %u", st);
            } else err = L"send failed";
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(con);
    }
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

static bool CloudBuildBackup(std::wstring& zipPath, std::wstring& err) {
    std::wstring stamp = NowFileStamp();
    std::wstring tmp = g_p.cache + L"\\cloud-" + stamp;
    MK(tmp);
    // TZ6 4.3: configs, keymaps, apk list (+ app profiles, settings w/o tokens)
    std::vector<std::pair<std::wstring, std::wstring>> items = {
        { g_p.dataRoot + L"\\settings.json", L"settings.json" },
        { g_p.dataRoot + L"\\apks.json", L"apks.json" },
        { g_p.dataRoot + L"\\keymaps.json", L"keymaps.json" },
        { g_p.dataRoot + L"\\app_profiles.json", L"app_profiles.json" },
        { g_p.dataRoot + L"\\macros.json", L"macros.json" },
    };
    bool any = false;
    for (auto& it : items)
        if (FE(it.first)) { CopyFileW(it.first.c_str(), (tmp + L"\\" + it.second).c_str(), FALSE); any = true; }
    if (!any) { err = T(S4_CLD_NOTHING); return false; }
    // strip tokens from settings copy
    std::wstring st;
    if (ReadText(tmp + L"\\settings.json", st)) {
        size_t p = st.find(L"cloudToken");
        if (p != std::wstring::npos) {
            size_t e = st.find(L",", p);
            if (e != std::wstring::npos) st = st.substr(0, p) + L"cloudToken:\"\"" + st.substr(e);
        }
        WriteText(tmp + L"\\settings.json", st);
    }
    zipPath = g_p.cache + L"\\NovaDroid-cloud-" + stamp + L".zip";
    std::wstring bsdtar = FindTool(L"", L"", L"bsdtar.exe");
    if (bsdtar.empty()) bsdtar = L"C:\\Windows\\System32\\tar.exe";
    if (!FE(bsdtar)) { err = L"tar.exe not found"; return false; }
    DWORD ec = 0; std::string so, se;
    RunCapture(bsdtar, Fmt(L"-a -cf \"%s\" -C \"%s\" .", zipPath.c_str(), tmp.c_str()),
               L"", 120000, &ec, &so, &se);
    DeleteTree(tmp);
    if (!FE(zipPath)) { err = L"zip failed"; return false; }
    return true;
}

bool V4CloudBackupAsync(std::wstring* errOut) {
    if (g_cloudBusy.exchange(true)) return false;
    std::thread([errOut]() {
        std::wstring err, zip;
        do {
            if (!CloudBuildBackup(zip, err)) break;
            if (V4Cloud().encrypt) {
                std::wstring enc = zip + L".enc";
                if (!AesWrapFile(zip, enc, true, err)) break;
                DeleteFileW(zip.c_str());
                zip = enc;
            }
            std::wstring name = BaseName(zip);
            if (!CloudUpload(zip, name, err)) break;
            g_cloudStatus = T(S4_CLD_UPOK);
            LogW(L"cloud", L"backup uploaded: %s", name.c_str());
        } while (0);
        if (!err.empty()) {
            g_cloudStatus = std::wstring(T(S4_CLD_UPFAIL)) + L" (" + err + L")";
            LogW(L"cloud", L"backup failed: %s", err.c_str());
        }
        DeleteFileW(zip.c_str());
        g_cloudBusy = false;
        if (g_wnd) UiNotify(err.empty() ? std::wstring(T(S4_CLD_UPOK))
                                        : std::wstring(T(S4_CLD_UPFAIL)) + L" (" + err + L")",
                            err.empty() ? 0 : 2);
    }).detach();
    return true;
}

std::vector<std::wstring> V4CloudList(std::wstring& err) {
    // minimal listing via provider REST (best-effort; used by restore dialog)
    std::vector<std::wstring> out;
    CloudCfg c = V4Cloud();
    if (c.provider.empty()) { err = T(S4_CLD_NOPROV); return out; }
    std::wstring url, hdrs;
    if (c.provider == L"dropbox") {
        url = L"https://api.dropboxapi.com/2/files/list_folder";
        hdrs = L"Authorization: Bearer " + c.token + L"\r\nContent-Type: application/json";
        // body via POST
        // (compact: use /2/files/list_folder with empty path)
        HINTERNET ses = WinHttpOpen(L"NovaDroid/0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        URL_COMPONENTSW uc = { sizeof(uc) };
        wchar_t host[256] = {}, path[1024] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
        if (WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc) && ses) {
            HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
            if (con) {
                HINTERNET req = WinHttpOpenRequest(con, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                std::string body = "{\"path\":\"\"}";
                std::wstring h2 = hdrs + Fmt(L"\r\nContent-Length: %d", (int)body.size());
                if (WinHttpSendRequest(req, h2.c_str(), (DWORD)h2.size(), (void*)body.data(),
                                       (DWORD)body.size(), (DWORD)body.size(), 0) &&
                    WinHttpReceiveResponse(req, nullptr)) {
                    std::string resp; char buf[16384]; DWORD rd = 0;
                    while (WinHttpReadData(req, buf, sizeof(buf), &rd) && rd) resp.append(buf, rd);
                    std::wstring w = U2W(resp);
                    size_t p = 0;
                    while ((p = w.find(L"\"name\":\"", p)) != std::wstring::npos) {
                        size_t e = w.find(L"\"", p + 8);
                        if (e == std::wstring::npos) break;
                        std::wstring n = w.substr(p + 8, e - p - 8);
                        if (n.find(L".zip") != std::wstring::npos || n.find(L".enc") != std::wstring::npos)
                            out.push_back(n);
                        p = e;
                    }
                } else err = L"request failed";
                WinHttpCloseHandle(req);
            }
            WinHttpCloseHandle(con);
        }
        if (ses) WinHttpCloseHandle(ses);
    } else if (c.provider == L"webdav" || c.provider == L"yadisk") {
        // PROPFIND Depth 1; parse <D:displayname>
        std::wstring base = c.provider == L"yadisk" ? L"https://webdav.yandex.ru/" : c.endpoint;
        url = base;
        HINTERNET ses = WinHttpOpen(L"NovaDroid/0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        URL_COMPONENTSW uc = { sizeof(uc) };
        wchar_t host[256] = {}, path[1024] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
        if (WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc) && ses) {
            HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
            if (con) {
                HINTERNET req = WinHttpOpenRequest(con, L"PROPFIND", path[0] ? path : L"/", nullptr,
                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                std::wstring auth;
                if (c.provider == L"yadisk") auth = L"Authorization: OAuth " + c.token;
                else {
                    std::string up = W2U(c.token);
                    DWORD b64 = 0;
                    CryptBinaryToStringA((const BYTE*)up.data(), (DWORD)up.size(),
                                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64);
                    std::vector<char> b(b64);
                    CryptBinaryToStringA((const BYTE*)up.data(), (DWORD)up.size(),
                                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b.data(), &b64);
                    auth = L"Authorization: Basic " + U2W(std::string(b.data(), b64));
                }
                std::wstring h2 = auth + L"\r\nDepth: 1\r\nContent-Length: 0";
                if (WinHttpSendRequest(req, h2.c_str(), (DWORD)h2.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(req, nullptr)) {
                    std::string resp; char buf[16384]; DWORD rd = 0;
                    while (WinHttpReadData(req, buf, sizeof(buf), &rd) && rd) resp.append(buf, rd);
                    std::wstring w = U2W(resp);
                    size_t p = 0;
                    while ((p = w.find(L"displayname>", p)) != std::wstring::npos) {
                        size_t s2 = w.find(L">", p) + 1, e = w.find(L"<", s2);
                        if (e == std::wstring::npos || s2 >= e) { p += 12; continue; }
                        std::wstring n = w.substr(s2, e - s2);
                        if ((n.find(L".zip") != std::wstring::npos || n.find(L".enc") != std::wstring::npos)
                            && n != BaseName(url))
                            out.push_back(n);
                        p = e;
                    }
                } else err = L"request failed";
                WinHttpCloseHandle(req);
            }
            WinHttpCloseHandle(con);
        }
        if (ses) WinHttpCloseHandle(ses);
    } else err = T(S4_CLD_NOPROV);
    return out;
}

bool V4CloudRestoreAsync(const std::wstring& fileName, std::wstring* errOut) {
    if (g_cloudBusy.exchange(true)) return false;
    std::thread([fileName, errOut]() {
        std::wstring err;
        CloudCfg c = V4Cloud();
        do {
            // download
            std::wstring dst = g_p.cache + L"\\" + fileName;
            std::wstring url, hdrs, method = L"GET";
            if (c.provider == L"dropbox") {
                url = L"https://content.dropboxapi.com/2/files/download";
                method = L"POST";
                hdrs = L"Authorization: Bearer " + c.token +
                       Fmt(L"\r\nDropbox-API-Arg: {\"path\":\"/%s\"}", fileName.c_str());
            } else if (c.provider == L"onedrive") {
                url = L"https://graph.microsoft.com/v1.0/me/drive/root:/" + fileName + L":/content";
                hdrs = L"Authorization: Bearer " + c.token;
            } else {
                url = (c.provider == L"yadisk" ? L"https://webdav.yandex.ru/" : c.endpoint) + L"/" + fileName;
                if (c.provider == L"yadisk") hdrs = L"Authorization: OAuth " + c.token;
                else {
                    std::string up = W2U(c.token);
                    DWORD b64 = 0;
                    CryptBinaryToStringA((const BYTE*)up.data(), (DWORD)up.size(),
                                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64);
                    std::vector<char> b(b64);
                    CryptBinaryToStringA((const BYTE*)up.data(), (DWORD)up.size(),
                                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b.data(), &b64);
                    hdrs = L"Authorization: Basic " + U2W(std::string(b.data(), b64));
                }
            }
            // reuse simple GET/POST downloader
            URL_COMPONENTSW uc = { sizeof(uc) };
            wchar_t host[256] = {}, path[1024] = {};
            uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
            if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) { err = L"bad url"; break; }
            bool got = false;
            HINTERNET ses = WinHttpOpen(L"NovaDroid/0.4", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            HINTERNET con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
            if (con) {
                HINTERNET req = WinHttpOpenRequest(con, method.c_str(), path[0] ? path : L"/", nullptr,
                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                std::wstring h2 = hdrs;
                if (WinHttpSendRequest(req, h2.c_str(), (DWORD)h2.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(req, nullptr)) {
                    HANDLE fh = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
                    char buf[262144]; DWORD rd = 0;
                    got = fh != INVALID_HANDLE_VALUE;
                    while (got && WinHttpReadData(req, buf, sizeof(buf), &rd) && rd) {
                        DWORD wr = 0;
                        if (!WriteFile(fh, buf, rd, &wr, nullptr) || wr != rd) { got = false; break; }
                    }
                    if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
                } else err = L"download failed";
                WinHttpCloseHandle(req);
            }
            if (con) WinHttpCloseHandle(con);
            if (ses) WinHttpCloseHandle(ses);
            if (!got) { if (err.empty()) err = L"download failed"; break; }
            // decrypt if .enc
            if (fileName.size() > 4 && fileName.rfind(L".enc") == fileName.size() - 4) {
                std::wstring dec = dst.substr(0, dst.size() - 4);
                if (!AesWrapFile(dst, dec, false, err)) break;
                DeleteFileW(dst.c_str());
                dst = dec;
            }
            // extract + merge
            std::wstring bsdtar = FindTool(L"", L"", L"bsdtar.exe");
            if (bsdtar.empty()) bsdtar = L"C:\\Windows\\System32\\tar.exe";
            std::wstring tmp = g_p.cache + L"\\cloudrest";
            DeleteTree(tmp); MK(tmp);
            DWORD ec = 0; std::string so, se;
            RunCapture(bsdtar, Fmt(L"-xf \"%s\" -C \"%s\"", dst.c_str(), tmp.c_str()), L"", 120000, &ec, &so, &se);
            // copy json files back
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((tmp + L"\\*.json").c_str(), &fd);
            int restored = 0;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    CopyFileW((tmp + L"\\" + fd.cFileName).c_str(),
                              (g_p.dataRoot + L"\\" + fd.cFileName).c_str(), FALSE);
                    restored++;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            DeleteTree(tmp);
            DeleteFileW(dst.c_str());
            LogW(L"cloud", L"restore done: %d files from %s", restored, fileName.c_str());
        } while (0);
        g_cloudBusy = false;
        if (g_wnd) UiNotify(err.empty() ? std::wstring(T(S4_CLD_RESOK))
                                        : std::wstring(T(S4_CLD_RESFAIL)) + L" (" + err + L")",
                            err.empty() ? 0 : 2);
    }).detach();
    return true;
}
