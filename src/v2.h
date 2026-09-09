// ============================================================================
//  NovaDroid - v2.h  Stage 2 "Product Edition" (TZ 2.0): multi-instance,
//  clones, snapshots, backups, APK library, keymaps, perf center, images.
// ============================================================================
#pragma once
#include "app.h"


// ---------------------------------------------------------------- snapshot model (TZ 17.2)
struct Snapshot {
    std::wstring id;             // snapshot-001
    std::wstring instanceId;
    std::wstring name;           // qemu-img internal tag (no spaces!)
    std::wstring displayName;    // user label
    std::wstring description;
    std::wstring type;           // system | user | auto
    bool isProtected = false;
    std::wstring createdAt;
    std::wstring diskPath;       // relative, informational
    long long sizeBytes = 0;
};

// ---------------------------------------------------------------- APK library model (TZ 17.3)
struct ApkEntry {
    std::wstring id;             // apk-001
    std::wstring fileName;
    std::wstring filePath;
    long long fileSizeBytes = 0;
    std::wstring sha256;
    std::wstring packageName;
    std::wstring versionName;
    int versionCode = 0;
    std::wstring source;
    std::wstring addedAt;
};

// ---------------------------------------------------------------- keymap models (TZ 17.4)
struct KeyBinding {
    int    vk = 0;               // Windows virtual key
    std::wstring action;         // tap | longtap | swipe | back | home | rec | volup | voldn | rotate
    int    x = 0, y = 0;         // tap point (guest px)
    int    x2 = 0, y2 = 0;       // swipe end
};

struct KeymapProfile {
    std::wstring id;             // keymap-001
    std::wstring name;
    std::wstring packageName;    // optional
    int  resW = 1280, resH = 720;
    std::wstring orientation = L"landscape";
    std::wstring activationHotkey = L"F1";
    bool showOverlay = true;
    bool enabled = false;        // runtime toggle (not persisted as true)
    std::wstring createdAt, updatedAt;
    std::vector<KeyBinding> bindings;
};

// ---------------------------------------------------------------- image catalog (built-in Android images)
struct ImageCatalogEntry {
    std::wstring fileName;       // android-x86_64-9.0-r2.iso
    std::wstring title;
    std::wstring desc;
    long long sizeBytes = 0;
    std::wstring sha256;
    std::vector<std::wstring> urls;   // mirrors, tried in order
};

// ---------------------------------------------------------------- perf sample (TZ 12)
struct PerfSample {
    float cpuPercent = 0;        // qemu process cpu, 0..100*cores
    uint64_t ramBytes = 0;
    long long diskBytes = 0;
    long long tMs = 0;
};

// ---------------------------------------------------------------- update check (TZ 14)
struct UpdateInfo {
    bool checked = false;
    bool ok = false;             // network succeeded
    std::wstring latestVersion;
    std::wstring channel;
    std::wstring notes;
    std::wstring error;
};

// ---------------------------------------------------------------- services (v2core.cpp)
// snapshots
std::vector<Snapshot> LoadSnapshots(const std::wstring& instanceId);
bool SaveSnapshots(const std::wstring& instanceId, const std::vector<Snapshot>& list);
std::wstring SnapshotCreate(const std::wstring& instanceId, const std::wstring& displayName,
                            const std::wstring& desc, const wchar_t* type, bool protect,
                            std::wstring& err);
bool SnapshotRestore(const std::wstring& instanceId, const std::wstring& snapId, std::wstring& err);
bool SnapshotDelete(const std::wstring& instanceId, const std::wstring& snapId, std::wstring& err);
void SnapshotEnsureCleanInstall(const std::wstring& instanceId);

// clone (TZ 7)
bool CloneInstanceV2(const std::wstring& id, const std::wstring& newName, bool linked,
                     bool copyKeymaps, bool copyShared, std::wstring& newId, std::wstring& err);
std::vector<std::wstring> LinkedChildren(const std::wstring& id);

// backups v2 (TZ 9)
struct BackupInfo { std::wstring dir, id, name, created, ver; bool hasDisk, hasManifest; long long bytes; };
std::vector<BackupInfo> ListBackups();
bool BackupInstanceV2(const std::wstring& id, bool withDisk, bool withSnapshots, std::wstring& err);
bool ExportBackupFile(const std::wstring& id, const std::wstring& dstPath, std::wstring& err); // .novadroid-backup
bool ImportBackupFile(const std::wstring& path, std::wstring& newId, std::wstring& err);
bool DeleteBackupDir(const std::wstring& dir, std::wstring& err);
std::wstring Sha256OfFile(const std::wstring& path);   // hex, "" on error

// APK library (TZ 10)
std::vector<ApkEntry>& ApkLib();
void ApkLibLoad();
void ApkLibSave();
ApkEntry* ApkAdd(const std::wstring& srcPath, const std::wstring& source, std::wstring& err);
bool ApkRemove(const std::wstring& apkId, bool deleteFile, std::wstring& err);
ApkEntry* ApkById(const std::wstring& apkId);
struct ApkInstallResult { std::wstring instanceId, instanceName, result, error; int seconds; };
void ApkInstallMulti(const std::vector<std::wstring>& instanceIds, const std::wstring& apkId,
                     bool snapshotFirst, std::vector<ApkInstallResult>& results);

// keymaps (TZ 11)
std::vector<KeymapProfile>& Keymaps();
void KeymapsLoad();
void KeymapsSave();
KeymapProfile* KeymapById(const std::wstring& id);
std::wstring KeymapAddDefault();
bool KeymapDelete(const std::wstring& id);
std::wstring KeymapVkName(int vk);

// keymap injection engine (low-level keyboard hook -> adb input)
void KeymapEngineStart();          // install hook (idempotent)
void KeymapEngineStop();
bool KeymapEngineActive();
void KeymapEngineSetInstance(const std::wstring& instanceId, const std::wstring& profileId);
std::wstring KeymapEngineStatus(); // for UI

// resource guard (TZ 5.3)
struct ResourceVerdict { bool allowed; std::wstring msg; int runningCount; };
ResourceVerdict CanStartInstance(const InstanceCfg& c);

// perf sampler (TZ 12)
void PerfSamplerStart();
std::vector<PerfSample> PerfHistory(const std::wstring& instanceId);
PerfSample PerfLatest(const std::wstring& instanceId);

// image manager (built-in images, TZ 8.1 of user's request)
std::vector<ImageCatalogEntry>& ImageCatalog();
std::wstring ImagesDirLocal();                       // dataRoot\images
std::vector<std::wstring> FindLocalImages();         // dataRoot\images + exeDir\images
bool ImageDownloadAsync(size_t catalogIdx);          // one download at a time
bool ImageDownloadBusy();
float ImageDownloadProgress();                       // 0..1
std::wstring ImageDownloadName();
void ImageCancelDownload();
std::wstring ImageUseAsDefault(const std::wstring& path);

// updates (TZ 14)
void UpdateCheckAsync();
UpdateInfo UpdateState();

// diagnostics export (TZ 13)
std::wstring DiagReportText();
bool DiagExportToFile(std::wstring& outPath);

void RunOpThread(std::function<void()> f);             // async worker (ui.cpp)

// ---------------------------------------------------------------- UI (v2ui.cpp)
void PageImages(HDC dc, RECT rc);
void PageBackups(HDC dc, RECT rc);
void PageKeysV2(HDC dc, RECT rc);
void PagePerfV2(HDC dc, RECT rc);
void PageApkV2(HDC dc, RECT rc);
void PageSnapshots(HDC dc, RECT rc);                 // snapshots of selected instance

bool WizardRun(InstanceCfg& out);                    // 5-step creation wizard (TZ 6)
void WizardFinishCreate(InstanceCfg& c);             // post-create actions
bool DialogApkMultiInstall(const std::wstring& apkId);// pick instances + install (TZ 10.4)
bool DialogKeyCapture(HWND parent, int& vkOut);      // "press a key" modal
bool DialogSnapshotAdd(HWND parent, std::wstring& name, std::wstring& desc, bool& protect);
bool DialogCloneOptions(HWND parent, InstanceCfg& c); // full/linked clone dialog (TZ 7.3)

void V2Init();                                       // load libs, start sampler/hook
void V2Shutdown();

// widget id ranges (ui.h owns base constants; ranges here)
bool V2Click(int id);                                // returns true if consumed
void V2TimerTick();
void V2ModalRun(HWND parent, int w, int h,
                 std::function<void(HDC, RECT)> draw,
                 std::function<bool(int)> click,
                 std::function<bool(MSG*)> key);                                  // 500ms UI tick from WndProc
