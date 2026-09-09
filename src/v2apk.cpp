// ============================================================================
//  NovaDroid - v2apk.cpp  APK library service + multi-instance install (TZ 10).
// ============================================================================
#include "app.h"
#include "v2.h"

static std::vector<ApkEntry> g_apks;
static std::mutex g_apkMx;

static std::wstring ApkLibPath() { return g_p.dataRoot + L"\\apks.json"; }

std::vector<ApkEntry>& ApkLib() { return g_apks; }

void ApkLibLoad() {
    std::lock_guard<std::mutex> lk(g_apkMx);
    g_apks.clear();
    std::wstring txt;
    if (!ReadText(ApkLibPath(), txt)) return;
    JValue v; std::wstring err;
    if (!JsonParse(txt, v, err) || v.type != JValue::Arr) return;
    for (auto& e : v.arr) {
        ApkEntry a;
        a.id = e.find(L"id") ? e.find(L"id")->asStr() : L"";
        a.fileName = e.find(L"fileName") ? e.find(L"fileName")->asStr() : L"";
        a.filePath = e.find(L"filePath") ? e.find(L"filePath")->asStr() : L"";
        a.fileSizeBytes = e.find(L"fileSizeBytes") ? e.find(L"fileSizeBytes")->asInt64() : 0;
        a.sha256 = e.find(L"sha256") ? e.find(L"sha256")->asStr() : L"";
        a.packageName = e.find(L"packageName") ? e.find(L"packageName")->asStr() : L"";
        a.versionName = e.find(L"versionName") ? e.find(L"versionName")->asStr() : L"";
        a.versionCode = e.find(L"versionCode") ? e.find(L"versionCode")->asInt(0) : 0;
        a.source = e.find(L"source") ? e.find(L"source")->asStr() : L"";
        a.addedAt = e.find(L"addedAt") ? e.find(L"addedAt")->asStr() : L"";
        if (!a.id.empty()) g_apks.push_back(a);
    }
}

void ApkLibSave() {
    std::lock_guard<std::mutex> lk(g_apkMx);
    JValue v; v.type = JValue::Arr;
    for (auto& a : g_apks) {
        JValue e; e.type = JValue::Obj;
        e.obj[L"id"] = JValue::MakeStr(a.id);
        e.obj[L"fileName"] = JValue::MakeStr(a.fileName);
        e.obj[L"filePath"] = JValue::MakeStr(a.filePath);
        e.obj[L"fileSizeBytes"] = JValue::MakeNum((double)a.fileSizeBytes);
        e.obj[L"sha256"] = JValue::MakeStr(a.sha256);
        e.obj[L"packageName"] = JValue::MakeStr(a.packageName);
        e.obj[L"versionName"] = JValue::MakeStr(a.versionName);
        e.obj[L"versionCode"] = JValue::MakeNum(a.versionCode);
        e.obj[L"source"] = JValue::MakeStr(a.source);
        e.obj[L"addedAt"] = JValue::MakeStr(a.addedAt);
        v.arr.push_back(e);
    }
    WriteText(ApkLibPath(), JsonWrite(v));
}

ApkEntry* ApkById(const std::wstring& apkId) {
    for (auto& a : g_apks) if (a.id == apkId) return &a;
    return nullptr;
}

ApkEntry* ApkAdd(const std::wstring& srcPath, const std::wstring& source, std::wstring& err) {
    if (FileExt(srcPath) != L".apk") { err = T(S_MB_BADAPK); return nullptr; }
    if (!FE(srcPath)) { err = L"file not found"; return nullptr; }
    MK(g_p.apks);
    std::wstring dst = g_p.apks + L"\\" + BaseName(srcPath);
    // avoid overwrite: add numeric suffix
    if (_wcsicmp(srcPath.c_str(), dst.c_str()) != 0) {
        if (FE(dst)) {
            std::wstring base = BaseName(srcPath), ext = FileExt(srcPath);
            std::wstring stem = base.substr(0, base.size() - ext.size());
            int n = 1;
            do { dst = g_p.apks + L"\\" + stem + Fmt(L"-%d", n++) + ext; } while (FE(dst));
        }
        if (!CopyFileW(srcPath.c_str(), dst.c_str(), FALSE)) {
            err = g_lang ? L"Copy failed" : L"Не удалось скопировать файл";
            return nullptr;
        }
    }
    ApkEntry a;
    a.id = Fmt(L"apk-%03d", (int)g_apks.size() + 1);
    a.fileName = BaseName(dst);
    a.filePath = dst;
    a.fileSizeBytes = (long long)FileSizeOf(dst);
    a.sha256 = Sha256OfFile(dst);
    a.source = source;
    a.addedAt = NowIso();
    g_apks.push_back(a);
    ApkLibSave();
    LogW(L"apk", L"added %s (%s, sha256 ok)", a.fileName.c_str(), HumanSize((uint64_t)a.fileSizeBytes).c_str());
    return &g_apks.back();
}

bool ApkRemove(const std::wstring& apkId, bool deleteFile, std::wstring& err) {
    for (size_t i = 0; i < g_apks.size(); ++i) {
        if (g_apks[i].id == apkId) {
            if (deleteFile && FE(g_apks[i].filePath)) DeleteFileW(g_apks[i].filePath.c_str());
            LogW(L"apk", L"removed %s", g_apks[i].fileName.c_str());
            g_apks.erase(g_apks.begin() + i);
            ApkLibSave();
            return true;
        }
    }
    err = L"not found";
    return false;
}

// ---------------------------------------------------------------- multi install (TZ 10.4)
void ApkInstallMulti(const std::vector<std::wstring>& instanceIds, const std::wstring& apkId,
                     bool snapshotFirst, std::vector<ApkInstallResult>& results) {
    ApkEntry* a = ApkById(apkId);
    if (!a) return;
    for (auto& iid : instanceIds) {
        ApkInstallResult res;
        res.instanceId = iid;
        InstanceCfg* c = Inst(iid);
        res.instanceName = c ? c->name : iid;
        long long t0 = (long long)GetTickCount64();
        std::wstring err;
        bool ok = false;
        do {
            Runtime* r = Rt(iid);
            if (!c) { err = L"not found"; break; }
            if (!r || !(r->st == St::Running && r->adbOk)) {
                err = g_lang ? L"Instance is not running or ADB is not connected"
                             : L"Инстанс не запущен или ADB не подключён";
                break;
            }
            if (snapshotFirst) {
                std::wstring e2;
                SnapshotCreate(iid,
                    g_lang ? L"Before APK install" : L"Перед установкой APK",
                    (g_lang ? L"Auto snapshot before installing " : L"Автоснимок перед установкой ") + a->fileName,
                    L"auto", false, e2);
                if (!e2.empty()) ILog(iid, L"launcher", L"pre-install snapshot skipped: " + e2);
            }
            ok = AdbInstall(iid, a->filePath, err);
            // try to detect package name on success
            if (ok && a->packageName.empty()) {
                std::wstring out, e3;
                if (AdbShell(iid, L"pm list packages | grep -i " + a->fileName.substr(0, 6), out, e3))
                    (void)out; // best-effort only
            }
        } while (0);
        res.seconds = (int)(((long long)GetTickCount64() - t0) / 1000);
        res.result = ok ? (g_lang ? L"Success" : L"Успешно") : (g_lang ? L"Error" : L"Ошибка");
        res.error = ok ? L"-" : err;
        results.push_back(res);
        ILog(iid, L"adb", (ok ? L"multi-install OK: " : L"multi-install FAIL: ") + a->fileName);
    }
}
