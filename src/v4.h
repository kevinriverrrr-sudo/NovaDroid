// ============================================================================
//  NovaDroid - v4.h  Stages 4-7 "Compatibility -> Commercial-grade" (TZ 4-7):
//  Stage 4: APK ABI analysis, network/DNS, sensors, device/app profiles.
//  Stage 5: system check, updates (SHA-256, rollback), logcat export,
//           first-run wizard, base apps auto-install, themes.
//  Stage 6: browser stream (localhost MJPEG+control), plugins, cloud backups,
//           macro/automation engine.
//  Stage 7: anti-lag/FPS stabilization, sync groups (multi-window),
//           Discord Rich Presence, Pro section.
// ============================================================================
#pragma once
#include "app.h"
#include "v2.h"
#include "v3.h"

// ---------------------------------------------------------------- Stage 4: APK analysis (TZ4 2.1)
struct ApkAbiInfo {
    bool parsed = false;
    std::wstring package, versionName;
    int versionCode = 0, minSdk = 0, targetSdk = 0;
    bool hasX86 = false, hasX86_64 = false, hasArm32 = false, hasArm64 = false;
    bool noLibs = false;                 // no lib/ entries -> pure java/kotlin, compatible
    int  level = 0;                      // 0 ok, 1 warn (arm-only), 2 unknown
    std::wstring verdict;                // user-facing text (already localized)
};
// Parse APK (zip central directory) + binary AndroidManifest.xml (AXML).
bool V4AnalyzeApk(const std::wstring& apkPath, ApkAbiInfo& out);
std::wstring V4ApkPackageOf(const std::wstring& apkPath);
// Fill missing ABI info for every APK-library entry (persisted afterwards).
void V4AnnotateApkLibrary();

// ---------------------------------------------------------------- Stage 4: network/DNS (TZ4 2.2)
struct NetDiagResult {
    bool gateway = false, dnsOk = false, internet = false;
    std::wstring detail;                 // combined adb output
};
NetDiagResult V4NetDiag(const std::wstring& instanceId);
bool V4NetFixDns(const std::wstring& instanceId, std::wstring& err);  // apply 8.8.8.8 via ndc/dns props

// ---------------------------------------------------------------- Stage 4: sensors (TZ4 2.3)
bool V4SensorGps(const std::wstring& id, double lat, double lon, std::wstring& err);
bool V4SensorOrient(const std::wstring& id, int preset, std::wstring& err);   // 0 portrait 1 landscape 2 lying
bool V4SensorBattery(const std::wstring& id, int level, int status, std::wstring& err); // status: 2 charge, 3 disch
bool V4SensorBatteryReset(const std::wstring& id, std::wstring& err);

// ---------------------------------------------------------------- Stage 4: profiles (TZ4 2.4)
struct DeviceProfileDef {
    const wchar_t* id; const wchar_t* nameRu; const wchar_t* nameEn;
    int w, h, dpi;
    const wchar_t* orient;               // "landscape" | "portrait"
};
const std::vector<DeviceProfileDef>& V4DeviceProfiles();
bool V4ApplyDeviceProfile(const std::wstring& instanceId, const std::wstring& profileId, std::wstring& err);

struct AppProfile {
    std::wstring package;                // com.example.game
    std::wstring title;                  // human label
    int w = 1280, h = 720, dpi = 240;
    std::wstring gpu = L"auto";          // auto|host|swiftshader|software
    int fps = 60;
    std::wstring notes;                  // known issues
};
std::vector<AppProfile>& V4AppProfiles();       // embedded defaults + dataRoot\app_profiles.json
bool V4AppProfilesSave();
const AppProfile* V4FindAppProfile(const std::wstring& package);
// Called after APK install and on app start: applies profile if known (asks nothing).
bool V4MaybeApplyAppProfile(const std::wstring& instanceId, const std::wstring& package);

// ---------------------------------------------------------------- Stage 5: system check (TZ5 3.1/3.2)
struct SysCheckResult {
    bool virt = false, whpx = false;
    long long ramMb = 0, diskFreeGb = 0;
    std::wstring cpuName, gpuName;
    int cores = 0;
    bool allOk() const { return virt && whpx && ramMb >= 3800 && diskFreeGb >= 12; }
};
SysCheckResult V4SystemCheck();

// ---------------------------------------------------------------- Stage 5: updates (TZ5 3.3)
struct V4UpdateInfo {
    bool checked = false, ok = false, available = false, busy = false;
    std::wstring version, channel, url, sha256, notes, error;
    float progress = 0;
};
void V4UpdateCheckAsync(bool manual);        // channel from g_set.updateChannel
V4UpdateInfo V4UpdateState();
void V4UpdateInstall();                      // download -> sha verify -> swap exe (rollback-safe)
// updates source: baked GitHub direct link, overridden by exeDir\updates.json:
// { "version":"0.5.0", "url":"https://...", "sha256":"...", "notes":"..." }

// ---------------------------------------------------------------- Stage 5: logs (TZ5 3.4)
std::wstring V4CollectLogsZip(const std::wstring& instanceId);   // qemu+launcher+logcat -> logs ZIP, path or ""

// ---------------------------------------------------------------- Stage 5: base apps (user req.)
void V4OnAdbUp(const std::wstring& id);      // async: install F-Droid + APKPure once per instance
bool V4InstallBaseApps(const std::wstring& id, std::wstring& logOut, std::wstring& err);

// ---------------------------------------------------------------- Stage 5: theme (TZ5 3.5)
void V4ApplyTheme();                         // reads g_set.theme: dark | light | system

// ---------------------------------------------------------------- Stage 6: browser stream (TZ6 4.1)
bool V4StreamStart(const std::wstring& instanceId, unsigned short port, std::wstring& err);
void V4StreamStop();
bool V4StreamRunning();
std::wstring V4StreamUrl();
std::wstring V4StreamInstanceId();

// ---------------------------------------------------------------- Stage 6: plugins (TZ6 4.2)
struct PluginEntry {
    std::wstring dir, name, version, desc, dllPath, manifestPath, sha256;
    bool enabled = false, loaded = false, valid = false;
};
std::vector<PluginEntry>& V4Plugins();
void V4PluginsScan();                        // exeDir\plugins\*\manifest.json
bool V4PluginToggle(const std::wstring& name, bool enable);
void V4PluginsDispatch(const wchar_t* event, const std::wstring& json);

// ---------------------------------------------------------------- Stage 6: cloud backups (TZ6 4.3)
struct CloudCfg {
    std::wstring provider = L"";             // "" | webdav | yadisk | dropbox | onedrive
    std::wstring endpoint;                   // webdav base url (provider specific)
    std::wstring token;                      // OAuth token (or user:pass for webdav)
    bool encrypt = true;
};
CloudCfg& V4Cloud();
bool V4CloudBackupAsync(std::wstring* errOut);    // zip configs/keymaps/apklist -> AES -> upload
bool V4CloudRestoreAsync(const std::wstring& fileName, std::wstring* errOut);
std::vector<std::wstring> V4CloudList(std::wstring& err);      // remote file names

// ---------------------------------------------------------------- Stage 6: macros (TZ6 4.4)
struct MacroStep {
    std::wstring type;                       // tap | longtap | swipe | key | text | wait
    int x = 0, y = 0, x2 = 0, y2 = 0, durMs = 0, key = 0;
    std::wstring text;
};
struct Macro {
    std::wstring id, name, instanceId, hotkeyVk;    // hotkeyVk: vk code as string, "" = none
    int repeat = 1;
    bool enabled = false;
    std::vector<MacroStep> steps;
};
std::vector<Macro>& V4Macros();
void V4MacrosLoad();
void V4MacrosSave();
Macro* V4MacroById(const std::wstring& id);
bool V4MacroDelete(const std::wstring& id);
bool V4MacroRecordStart(const std::wstring& instanceId);
bool V4MacroRecordStop();                    // creates new Macro entry
bool V4MacroRecording();
std::wstring V4MacroRecordInstanceId();
int  V4MacroRecordStepCount();
bool V4MacroPlayAsync(const std::wstring& macroId, std::wstring& err);
bool V4MacroPlaying();
void V4MacroStopPlay();

// ---------------------------------------------------------------- Stage 7: anti-lag (TZ7 5.2)
void V4AntiLagSet(bool on);
bool V4AntiLagOn();
void V4AntiLagSetMode(const std::wstring& m); // perf | balance | quality | eco
std::wstring V4AntiLagMode();
void V4AntiLagTick();                        // 1 s: fps watch, priority boost, recommendations
void V4BoostPriority(const std::wstring& instanceId);

// ---------------------------------------------------------------- Stage 7: sync groups (TZ7 5.3)
struct SyncGroup {
    std::wstring masterId;
    std::vector<std::wstring> slaveIds;
    bool syncTouch = true, syncKeys = true, syncMacros = true;
};
std::vector<SyncGroup>& V4SyncGroups();
void V4SyncGroupsLoad();
void V4SyncGroupsSave();
SyncGroup* V4SyncGroupOf(const std::wstring& instanceId);       // as master or slave
void V4SyncRemove(const std::wstring& instanceId);
void V4SyncBroadcastTouch(const std::wstring& masterId, int x, int y, bool down);
void V4SyncBroadcastKey(const std::wstring& masterId, int vk, bool down);
void V4SyncBroadcastMacro(const std::wstring& masterId, const std::wstring& macroId);

// ---------------------------------------------------------------- Stage 7: Discord RPC (TZ7 5.5)
bool V4DiscordSet(const std::wstring& appLabel);    // connect (once) + set activity; false = no pipe
void V4DiscordClear();                              // clear activity
bool V4DiscordActive();
void V4DiscordTick();                               // keepalive/flush from timer

// ---------------------------------------------------------------- UI glue (v4ui.cpp)
void V4Init();
void V4Shutdown();
bool V4Click(int id);
void V4TimerTick();
void PageCompat(HDC dc, RECT rc);            // ABI analyzer + device/app profiles
void PageSensors(HDC dc, RECT rc);           // GPS / orientation / battery
void PageAdvanced(HDC dc, RECT rc);          // stream/plugins/cloud/macros/sync/discord/anti-lag/pro
bool DialogMacroEditor(HWND parent, Macro& m);
bool DialogCloudSetup(HWND parent);
void V4FirstRunWizard(HWND parent);          // TZ5 3.2 (runs once when firstRunDone == false)

// widget id ranges (stage 4-7)
enum {
    ID4_ANA0 = 6000,                 // compat page: 0=analyze pick, 1=annotate lib, 2=copy
    ID4_DEV0 = 6020,                 // device profile rows: i*2+0 apply
    ID4_APP0 = 6060,                 // app profile rows: i*2+0 apply-to-selected
    ID4_GPS0 = 6100,                 // sensors: 0=set gps,1..3 presets,4=orient p,5=orient l,6=orient lying,7=batt apply,8=batt reset
    ID4_NET0 = 6120,                 // 0=diag,1=fix dns,2=reset dns
    ID4_UPD0 = 6140,                 // 0=check,1=install,2=channel-
    ID4_LOG0 = 6150,                 // 0=export zip
    ID4_STR0 = 6160,                 // 0=start/stop stream,1=open browser,2=port-
    ID4_PLG0 = 6180,                 // plugin rows: i*2+0 toggle, +20 rescan
    ID4_CLD0 = 6220,                 // 0=setup,1=backup,2=restore,3=refresh list
    ID4_MAC0 = 6260,                 // macro rows: i*5+0 rec,1 play,2 stop,3 edit,4 delete; 6290 add
    ID4_ALG0 = 6300,                 // anti-lag: 0=toggle,1..4 modes
    ID4_SYN0 = 6320,                 // sync: 0=add master->selected,1=remove, rows i*2
    ID4_DSC0 = 6340,                 // 0=discord toggle, 1=test
    ID4_PRO0 = 6360,                 // pro: 0=activate key
    ID4_WIZ0 = 6380,                 // first-run wizard controls
};
