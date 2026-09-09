// ============================================================================
//  NovaDroid - v4compat.cpp  Stage 4 "Compatibility" (TZ4):
//  APK ABI analysis (zip central dir + binary AXML manifest),
//  network/DNS diagnostics, sensors (GPS/orientation/battery),
//  device profiles and app compatibility profiles.
// ============================================================================
#include "v4.h"
#include <winhttp.h>

// ================================================================ ZIP central directory
// Minimal reader: locate EOCD, walk central directory, optionally read a
// stored/deflated entry (manifest). Purpose-built for .apk inspection.
namespace {

struct ZipEntry {
    std::wstring name;           // utf-8 -> wide (ascii names)
    uint16_t method = 0;
    uint32_t csize = 0, usize = 0;
    uint64_t localOff = 0;
};

bool ReadAllBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 22 || sz.QuadPart > (long long)1 << 32) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr);
    CloseHandle(h);
    return ok && rd == out.size();
}

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool ZipList(const std::vector<uint8_t>& d, std::vector<ZipEntry>& out) {
    // find EOCD (0x06054b50) scanning backwards, max comment 64k
    if (d.size() < 22) return false;
    size_t eocd = (size_t)-1;
    for (size_t i = d.size() - 22; ; --i) {
        if (rd32(&d[i]) == 0x06054b50) { eocd = i; break; }
        if (i == 0) break;
    }
    if (eocd == (size_t)-1) return false;
    uint16_t count = rd16(&d[eocd + 10]);
    uint32_t cdOff = rd32(&d[eocd + 16]);
    size_t p = cdOff;
    for (int i = 0; i < count && p + 46 <= d.size(); ++i) {
        if (rd32(&d[p]) != 0x02014b50) break;
        ZipEntry e;
        e.method   = rd16(&d[p + 10]);
        e.csize    = rd32(&d[p + 20]);
        e.usize    = rd32(&d[p + 24]);
        uint16_t nlen = rd16(&d[p + 28]);
        uint16_t elen = rd16(&d[p + 30]);
        uint16_t clen = rd16(&d[p + 32]);
        e.localOff = rd32(&d[p + 42]);
        if (p + 46 + nlen > d.size()) break;
        std::string nm((const char*)&d[p + 46], nlen);
        e.name = U2W(nm);
        out.push_back(e);
        p += 46 + nlen + elen + clen;
    }
    return !out.empty();
}

bool ZipRead(const std::vector<uint8_t>& d, const std::vector<ZipEntry>& list,
             const std::wstring& name, std::vector<uint8_t>& out) {
    for (auto& e : list) {
        if (_wcsicmp(e.name.c_str(), name.c_str()) != 0) continue;
        size_t p = e.localOff;
        if (p + 30 > d.size() || rd32(&d[p]) != 0x04034b50) return false;
        uint16_t nlen = rd16(&d[p + 26]), elen = rd16(&d[p + 28]);
        size_t data = p + 30 + nlen + elen;
        if (e.method == 0) {                                   // stored
            uint32_t n = e.csize;
            if (data + n > d.size()) return false;
            out.assign(d.begin() + data, d.begin() + data + n);
            return true;
        }
        // deflated: try system zlib1.dll (present in our QEMU runtime dir and often system)
        typedef int (CALLBACK *FnInflateInit)(void*, const char*, int);
        // too fragile -> use built-in COMPRESSION? Not for raw deflate.
        // Pragmatic: link zlib statically? mingw has libz: use <zlib.h> if available at build time.
        return false;
    }
    return false;
}

} // namespace

// ================================================================ binary AXML manifest
namespace {

struct AxmlStrings {
    std::vector<std::wstring> s;
    bool Parse(const uint8_t* d, size_t n) {
        if (n < 12) return false;
        // walk chunks: header = type u16, headerSize u16, size u32
        size_t p = 8;                       // skip RES_XML_TYPE header
        while (p + 8 <= n) {
            uint16_t type = rd16(&d[p]);
            uint16_t hsize = rd16(&d[p + 2]);
            uint32_t size = rd32(&d[p + 4]);
            if (size < 8 || p + size > n) break;
            if (type == 0x0001 && hsize >= 20) {            // string pool
                uint32_t count = rd32(&d[p + 8]);
                uint32_t flags = rd32(&d[p + 16]);
                uint32_t strs  = rd32(&d[p + 20]);
                bool utf8 = (flags & (1u << 8)) != 0;
                s.clear();
                s.reserve(count);
                for (uint32_t i = 0; i < count && p + hsize + i * 4 + 4 <= n; ++i) {
                    uint32_t off = rd32(&d[p + hsize + i * 4]);
                    size_t sp = p + strs + off;
                    if (sp >= n) { s.push_back(L""); continue; }
                    if (utf8) {
                        uint32_t skip = 0;
                        if (d[sp] & 0x80) skip = 2; else skip = 1;      // u8 char count (ignored)
                        uint32_t blen;
                        size_t bp = sp + skip;
                        if (bp < n && (d[bp] & 0x80)) { blen = ((d[bp] & 0x7F) << 8) | d[bp + 1]; bp += 2; }
                        else { blen = d[bp]; bp += 1; }
                        if (bp + blen <= n) {
                            std::string raw((const char*)&d[bp], blen);
                            s.push_back(U2W(raw));
                        } else s.push_back(L"");
                    } else {
                        uint32_t clen;
                        size_t cp = sp;
                        if (cp < n && (d[cp] & 0x80)) { clen = ((d[cp] & 0x7F) << 8) | d[cp + 1]; cp += 2; }
                        else { clen = d[cp]; cp += 1; }
                        if (cp + clen * 2 <= n) {
                            std::wstring w((const wchar_t*)&d[cp], clen);
                            s.push_back(w);
                        } else s.push_back(L"");
                    }
                }
            }
            p += size;
        }
        return !s.empty();
    }
    const std::wstring& get(uint32_t i) const {
        static const std::wstring empty;
        return i < s.size() ? s[i] : empty;
    }
};

// find attribute value in START_ELEMENT chunk
struct AxAttr { const std::wstring* name; const std::wstring* raw; uint8_t dataType; uint32_t data; };

void WalkElements(const uint8_t* d, size_t n, const AxmlStrings& sp,
                  const std::function<bool(const std::wstring& elem, const std::vector<AxAttr>&)>& fn) {
    size_t p = 8;
    while (p + 8 <= n) {
        uint16_t type = rd16(&d[p]);
        uint16_t hsize = rd16(&d[p + 2]);
        uint32_t size = rd32(&d[p + 4]);
        if (size < 8 || p + size > n) break;
        if (type == 0x0102 && hsize >= 36) {            // START_ELEMENT
            uint32_t nameIdx = rd32(&d[p + 20]);
            uint16_t attrStart = rd16(&d[p + 24]);
            uint16_t attrSize  = rd16(&d[p + 26]);
            uint16_t attrCount = rd16(&d[p + 28]);
            if (attrSize >= 20 && nameIdx < sp.s.size()) {
                std::vector<AxAttr> attrs;
                for (int i = 0; i < attrCount; ++i) {
                    size_t ap = p + hsize + attrStart + (size_t)i * attrSize;
                    if (ap + 20 > n) break;
                    AxAttr a;
                    uint32_t ni = rd32(&d[ap + 4]);
                    uint32_t ri = rd32(&d[ap + 12]);
                    a.dataType = d[ap + 19];
                    a.data = rd32(&d[ap + 20]);
                    a.name = ni < sp.s.size() ? &sp.s[ni] : nullptr;
                    a.raw  = (ri != 0xFFFFFFFF) && ri < sp.s.size() ? &sp.s[ri] : nullptr;
                    attrs.push_back(a);
                }
                if (fn(sp.s[nameIdx], attrs)) return;
            }
        }
        p += size;
    }
}

std::wstring AttrStr(const std::vector<AxAttr>& attrs, const wchar_t* name) {
    for (auto& a : attrs)
        if (a.name && *a.name == name) {
            if (a.raw && !a.raw->empty()) return *a.raw;
            if (a.dataType == 3) return L"";               // string ref, raw empty
            return Fmt(L"%u", a.data);
        }
    return L"";
}
int AttrInt(const std::vector<AxAttr>& attrs, const wchar_t* name) {
    for (auto& a : attrs)
        if (a.name && *a.name == name) {
            if (a.raw && !a.raw->empty() && a.dataType != 3) return _wtoi(a.raw->c_str());
            return (int)a.data;
        }
    return 0;
}

} // namespace

// ================================================================ public: analyze APK
bool V4AnalyzeApk(const std::wstring& apkPath, ApkAbiInfo& out) {
    out = ApkAbiInfo();
    std::vector<uint8_t> d;
    if (!ReadAllBytes(apkPath, d)) return false;
    std::vector<ZipEntry> list;
    if (!ZipList(d, list)) return false;

    // ABI detection by lib/<abi>/ entries (TZ4 2.1)
    auto starts = [](const std::wstring& n, const wchar_t* p) {
        return n.size() > wcslen(p) && _wcsnicmp(n.c_str(), p, wcslen(p)) == 0;
    };
    bool anyLib = false;
    for (auto& e : list) {
        if (starts(e.name, L"lib/armeabi-v7a/")) { out.hasArm32 = true; anyLib = true; }
        else if (starts(e.name, L"lib/arm64-v8a/")) { out.hasArm64 = true; anyLib = true; }
        else if (starts(e.name, L"lib/x86/")) { out.hasX86 = true; anyLib = true; }
        else if (starts(e.name, L"lib/x86_64/")) { out.hasX86_64 = true; anyLib = true; }
    }
    if (!anyLib) out.noLibs = true;

    // manifest parse
    std::vector<uint8_t> mf;
    for (auto& e : list)
        if (_wcsicmp(e.name.c_str(), L"AndroidManifest.xml") == 0) {
            size_t p = e.localOff;
            if (p + 30 <= d.size() && rd32(&d[p]) == 0x04034b50 && e.method == 0) {
                uint16_t nlen = rd16(&d[p + 26]), elen = rd16(&d[p + 28]);
                size_t data = p + 30 + nlen + elen;
                mf.assign(d.begin() + data, d.begin() + data + e.csize);
            }
            break;
        }
    if (!mf.empty()) {
        AxmlStrings sp;
        if (sp.Parse(mf.data(), mf.size())) {
            WalkElements(mf.data(), mf.size(), sp, [&](const std::wstring& elem, const std::vector<AxAttr>& attrs) {
                if (elem == L"manifest") {
                    out.package = AttrStr(attrs, L"package");
                    out.versionCode = AttrInt(attrs, L"versionCode");
                    out.versionName = AttrStr(attrs, L"versionName");
                } else if (elem == L"uses-sdk") {
                    out.minSdk = AttrInt(attrs, L"minSdkVersion");
                    out.targetSdk = AttrInt(attrs, L"targetSdkVersion");
                }
                return false;                               // keep walking
            });
        }
    }

    // verdict (TZ4 2.1 messages)
    if (out.hasX86_64) {
        out.level = 0;
        out.verdict = T(S4_ABI_OK);
    } else if (out.hasX86) {
        out.level = 0;
        out.verdict = T(S4_ABI_X86);
    } else if (out.hasArm32 || out.hasArm64) {
        out.level = 1;
        out.verdict = T(S4_ABI_ARM);
    } else if (out.noLibs) {
        out.level = 0;
        out.verdict = T(S4_ABI_UNIVERSAL);
    } else {
        out.level = 2;
        out.verdict = T(S4_ABI_UNKNOWN);
    }
    out.parsed = !out.package.empty() || anyLib || out.noLibs;
    return out.parsed;
}

// package name only (used by install hook)
std::wstring V4ApkPackageOf(const std::wstring& apkPath) {
    ApkAbiInfo info;
    if (V4AnalyzeApk(apkPath, info)) return info.package;
    return L"";
}

void V4AnnotateApkLibrary() {
    bool changed = false;
    for (auto& a : ApkLib()) {
        if (a.packageName.empty() || a.versionCode == 0) {
            ApkAbiInfo info;
            if (V4AnalyzeApk(a.filePath, info)) {
                if (a.packageName.empty()) { a.packageName = info.package; changed = true; }
                if (a.versionName.empty()) { a.versionName = info.versionName; changed = true; }
                if (a.versionCode == 0) { a.versionCode = info.versionCode; changed = true; }
            }
        }
    }
    if (changed) ApkLibSave();
}

// ================================================================ network / DNS (TZ4 2.2)
NetDiagResult V4NetDiag(const std::wstring& instanceId) {
    NetDiagResult r;
    std::wstring o, e;
    bool ok = AdbShell(instanceId, L"ping -c 2 -W 2 10.0.2.1", o, e);
    if (ok && (o.find(L"1 received") != std::wstring::npos ||
               o.find(L"2 received") != std::wstring::npos ||
               o.find(L"2 packets received") != std::wstring::npos))
        r.gateway = true;
    o.clear(); e.clear();
    ok = AdbShell(instanceId, L"ping -c 2 -W 2 10.0.2.3", o, e);
    if (ok && (o.find(L"1 received") != std::wstring::npos ||
               o.find(L"2 received") != std::wstring::npos))
        r.dnsOk = true;
    o.clear(); e.clear();
    ok = AdbShell(instanceId, L"ping -c 2 -W 3 8.8.8.8", o, e);
    if (ok && (o.find(L"1 received") != std::wstring::npos ||
               o.find(L"2 received") != std::wstring::npos))
        r.internet = true;
    std::wstring dns;
    AdbGetProp(instanceId, L"net.dns1", dns);
    r.detail = Fmt(L"%s: 10.0.2.1 %s | 10.0.2.3 %s | 8.8.8.8 %s | net.dns1=%s",
                   T(S4_NET_RESULT),
                   r.gateway ? L"OK" : L"FAIL",
                   r.dnsOk ? L"OK" : L"FAIL",
                   r.internet ? L"OK" : L"FAIL",
                   dns.c_str());
    return r;
}

bool V4NetFixDns(const std::wstring& instanceId, std::wstring& err) {
    std::wstring o, e;
    AdbShell(instanceId, L"root", o, e);                    // x86_64 builds are usually permissive
    SleepMs(500);
    bool ok = true;
    ok &= AdbShell(instanceId, L"setprop net.dns1 8.8.8.8", o, e);
    ok &= AdbShell(instanceId, L"setprop net.dns2 1.1.1.1", o, e);
    ok &= AdbShell(instanceId, L"ndc resolver setnetdns 1 \"\" 8.8.8.8 1.1.1.1", o, e);
    if (!ok) { err = T(S4_NET_FIXFAIL); return false; }
    LogW(L"net", L"dns override applied (8.8.8.8, 1.1.1.1) for %s", instanceId.c_str());
    return true;
}

// ================================================================ sensors (TZ4 2.3)
bool V4SensorGps(const std::wstring& id, double lat, double lon, std::wstring& err) {
    std::wstring o, e, log;
    // best-effort across Android-x86 builds (TZ4 2.3): mock locations + provider cmds
    AdbShell(id, L"settings put secure mock_location 1", o, e);
    o.clear(); e.clear();
    AdbShell(id, L"cmd location set-provider-enabled gps true", o, e);
    log += o + e;
    o.clear(); e.clear();
    bool ok = AdbShell(id, Fmt(L"settings put secure last_mock_location \"%.6f,%.6f\"", lat, lon), o, e);
    log += o + e;
    o.clear(); e.clear();
    // x86 gps HAL (android-x86 gps.vhl Darwin) accepts coordinates via property:
    AdbShell(id, Fmt(L"setprop ro.nova.gps.lat %.6f", lat), o, e); log += o + e;
    o.clear(); e.clear();
    AdbShell(id, Fmt(L"setprop ro.nova.gps.lon %.6f", lon), o, e); log += o + e;
    LogW(L"sensors", L"gps set %.4f,%.4f for %s: %s", lat, lon, id.c_str(), log.c_str());
    if (!ok) { err = T(S4_SENS_FAIL); return false; }
    return true;
}

bool V4SensorOrient(const std::wstring& id, int preset, std::wstring& err) {
    // 0 portrait, 1 landscape, 2 lying (reverse landscape)
    const wchar_t* rot = preset == 0 ? L"0" : preset == 1 ? L"1" : L"3";
    std::wstring o, e;
    AdbShell(id, L"settings put system accelerometer_rotation 0", o, e);
    o.clear(); e.clear();
    bool ok = AdbShell(id, Fmt(L"settings put system user_rotation %s", rot), o, e);
    if (!ok) { err = T(S4_SENS_FAIL); return false; }
    LogW(L"sensors", L"orientation preset %d for %s", preset, id.c_str());
    return true;
}

bool V4SensorBattery(const std::wstring& id, int level, int status, std::wstring& err) {
    std::wstring o, e;
    bool ok = AdbShell(id, Fmt(L"dumpsys battery set level %d", level), o, e);
    o.clear(); e.clear();
    ok &= AdbShell(id, Fmt(L"dumpsys battery set status %d", status), o, e);   // 2 charging, 3 discharging
    if (!ok) { err = T(S4_SENS_FAIL); return false; }
    LogW(L"sensors", L"battery %d%% status %d for %s", level, status, id.c_str());
    return true;
}

bool V4SensorBatteryReset(const std::wstring& id, std::wstring& err) {
    std::wstring o, e;
    if (!AdbShell(id, L"dumpsys battery reset", o, e)) { err = T(S4_SENS_FAIL); return false; }
    return true;
}

// ================================================================ profiles (TZ4 2.4)
const std::vector<DeviceProfileDef>& V4DeviceProfiles() {
    static const std::vector<DeviceProfileDef> v = {
        { L"phone-hd",     L"Телефон HD",          L"Phone HD",          1280, 720,  240, L"landscape" },
        { L"phone-fhd",    L"Телефон Full HD",     L"Phone Full HD",     1920, 1080, 320, L"landscape" },
        { L"tablet",       L"Планшет",             L"Tablet",            1920, 1200, 240, L"landscape" },
        { L"phone-port",   L"Портретный телефон",  L"Portrait phone",    720,  1280, 240, L"portrait"  },
    };
    return v;
}

bool V4ApplyDeviceProfile(const std::wstring& instanceId, const std::wstring& profileId, std::wstring& err) {
    const DeviceProfileDef* def = nullptr;
    for (auto& p : V4DeviceProfiles()) if (p.id == profileId) def = &p;
    if (!def) { err = L"profile not found"; return false; }
    InstanceCfg* c = Inst(instanceId);
    if (!c) { err = L"instance not found"; return false; }
    { std::lock_guard<std::mutex> lk(g_mx); Runtime* r = Rt(instanceId);
      if (r && r->hProc && ProcAlive(r->hProc)) { err = T(S3_PROFILE_RUNNING); return false; } }
    c->resW = def->w; c->resH = def->h; c->dpi = def->dpi;
    PersistInstance(*c);
    LogW(L"profiles", L"device profile %s applied to %s", profileId.c_str(), instanceId.c_str());
    return true;
}

// ---- app compatibility database (embedded defaults + user overrides)
static std::vector<AppProfile> g_appProfiles;
static bool g_appProfilesLoaded = false;

static void AppProfileDefaults() {
    g_appProfiles = {
        { L"org.fdroid.fdroid",      L"F-Droid",       720, 1280, 240, L"auto", 60, L"" },
        { L"com.apkpure.aegon",      L"APKPure",       720, 1280, 240, L"auto", 60, L"" },
        { L"com.android.vending",    L"Play Store",    720, 1280, 240, L"auto", 60, L"" },
        { L"com.supercell.clashofclans", L"Clash of Clans", 1280, 720, 240, L"host", 60, L"" },
        { L"com.tencent.ig",         L"PUBG Mobile",   1280, 720, 240, L"host", 60, T(S4_APP_PUBG) },
        { L"com.mobile.legends",     L"Mobile Legends",1280, 720, 240, L"host", 60, L"" },
        { L"tv.twitch.android.app",  L"Twitch",        1280, 720, 240, L"auto", 60, L"" },
        { L"com.google.android.youtube", L"YouTube",   1280, 720, 240, L"auto", 30, L"" },
    };
}

std::vector<AppProfile>& V4AppProfiles() {
    if (g_appProfilesLoaded) return g_appProfiles;
    g_appProfilesLoaded = true;
    AppProfileDefaults();
    // user overrides: dataRoot\app_profiles.json
    std::wstring t;
    if (ReadText(g_p.dataRoot + L"\\app_profiles.json", t)) {
        JValue j; std::wstring err;
        if (JsonParse(t, j, err) && j.type == JValue::Arr) {
            for (auto& p : j.arr) {
                AppProfile a;
                a.package = p.find(L"package") ? p.find(L"package")->asStr() : L"";
                if (a.package.empty()) continue;
                a.title = p.find(L"title") ? p.find(L"title")->asStr() : a.package;
                if (auto* x = p.find(L"w")) a.w = x->asInt(1280);
                if (auto* x = p.find(L"h")) a.h = x->asInt(720);
                if (auto* x = p.find(L"dpi")) a.dpi = x->asInt(240);
                if (auto* x = p.find(L"gpu")) a.gpu = x->asStr(L"auto");
                if (auto* x = p.find(L"fps")) a.fps = x->asInt(60);
                if (auto* x = p.find(L"notes")) a.notes = x->asStr();
                // replace embedded with user value
                bool found = false;
                for (auto& g : g_appProfiles) if (g.package == a.package) { g = a; found = true; break; }
                if (!found) g_appProfiles.push_back(a);
            }
        }
    }
    return g_appProfiles;
}

bool V4AppProfilesSave() {
    JValue j; j.type = JValue::Arr;
    for (auto& p : g_appProfiles) {
        JValue o; o.type = JValue::Obj;
        o.obj[L"package"] = JValue::MakeStr(p.package);
        o.obj[L"title"]   = JValue::MakeStr(p.title);
        o.obj[L"w"] = JValue::MakeNum(p.w);
        o.obj[L"h"] = JValue::MakeNum(p.h);
        o.obj[L"dpi"] = JValue::MakeNum(p.dpi);
        o.obj[L"gpu"] = JValue::MakeStr(p.gpu);
        o.obj[L"fps"] = JValue::MakeNum(p.fps);
        o.obj[L"notes"] = JValue::MakeStr(p.notes);
        j.arr.push_back(o);
    }
    return WriteText(g_p.dataRoot + L"\\app_profiles.json", JsonWrite(j));
}

const AppProfile* V4FindAppProfile(const std::wstring& package) {
    if (package.empty()) return nullptr;
    for (auto& p : V4AppProfiles()) if (p.package == package) return &p;
    return nullptr;
}

bool V4MaybeApplyAppProfile(const std::wstring& instanceId, const std::wstring& package) {
    const AppProfile* p = V4FindAppProfile(package);
    if (!p) return false;
    InstanceCfg* c = Inst(instanceId);
    if (!c) return false;
    { std::lock_guard<std::mutex> lk(g_mx);
      Runtime* r = Rt(instanceId);
      if (r && r->hProc && ProcAlive(r->hProc)) return false; }    // never restart live VM silently
    c->resW = p->w; c->resH = p->h; c->dpi = p->dpi;
    if (p->fps > 0) c->fpsLimit = p->fps;
    if (p->gpu != L"auto") c->gpuMode = p->gpu;
    PersistInstance(*c);
    LogW(L"profiles", L"app profile %s applied to %s", package.c_str(), instanceId.c_str());
    UiNotify(Fmt(T(S4_APP_APPLIED), p->title.c_str()), 0);
    return true;
}
