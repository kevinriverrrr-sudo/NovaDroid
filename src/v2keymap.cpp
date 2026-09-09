// ============================================================================
//  NovaDroid - v2keymap.cpp  Keymap profiles (TZ 11) + injection engine:
//  low-level keyboard hook translates physical keys into ADB input taps.
//  No macros, no automation, no anti-cheat bypass - only manual key remap
//  of on-screen actions the user would perform by hand (TZ 11.5).
// ============================================================================
#include "app.h"
#include "v2.h"

static std::vector<KeymapProfile> g_kms;
static std::mutex g_kmMx;

static std::wstring KeymapLibPath() { return g_p.dataRoot + L"\\keymaps.json"; }

std::vector<KeymapProfile>& Keymaps() { return g_kms; }

static JValue BindingJ(const KeyBinding& b) {
    JValue e; e.type = JValue::Obj;
    e.obj[L"vk"] = JValue::MakeNum(b.vk);
    e.obj[L"action"] = JValue::MakeStr(b.action);
    e.obj[L"x"] = JValue::MakeNum(b.x);
    e.obj[L"y"] = JValue::MakeNum(b.y);
    e.obj[L"x2"] = JValue::MakeNum(b.x2);
    e.obj[L"y2"] = JValue::MakeNum(b.y2);
    return e;
}

void KeymapsLoad() {
    std::lock_guard<std::mutex> lk(g_kmMx);
    g_kms.clear();
    std::wstring txt;
    if (!ReadText(KeymapLibPath(), txt)) return;
    JValue v; std::wstring err;
    if (!JsonParse(txt, v, err) || v.type != JValue::Arr) return;
    for (auto& e : v.arr) {
        KeymapProfile p;
        p.id = e.find(L"id") ? e.find(L"id")->asStr() : L"";
        p.name = e.find(L"name") ? e.find(L"name")->asStr() : L"";
        p.packageName = e.find(L"packageName") ? e.find(L"packageName")->asStr() : L"";
        p.resW = e.find(L"resolutionWidth") ? e.find(L"resolutionWidth")->asInt(1280) : 1280;
        p.resH = e.find(L"resolutionHeight") ? e.find(L"resolutionHeight")->asInt(720) : 720;
        p.orientation = e.find(L"orientation") ? e.find(L"orientation")->asStr(L"landscape") : L"landscape";
        p.activationHotkey = e.find(L"activationHotkey") ? e.find(L"activationHotkey")->asStr(L"F1") : L"F1";
        p.showOverlay = e.find(L"showOverlay") ? e.find(L"showOverlay")->asBool(true) : true;
        p.createdAt = e.find(L"createdAt") ? e.find(L"createdAt")->asStr() : L"";
        p.updatedAt = e.find(L"updatedAt") ? e.find(L"updatedAt")->asStr() : L"";
        if (auto* bs = e.find(L"bindings")) {
            for (auto& b : bs->arr) {
                KeyBinding kb;
                kb.vk = b.find(L"vk") ? b.find(L"vk")->asInt(0) : 0;
                kb.action = b.find(L"action") ? b.find(L"action")->asStr(L"tap") : L"tap";
                kb.x = b.find(L"x") ? b.find(L"x")->asInt(0) : 0;
                kb.y = b.find(L"y") ? b.find(L"y")->asInt(0) : 0;
                kb.x2 = b.find(L"x2") ? b.find(L"x2")->asInt(0) : 0;
                kb.y2 = b.find(L"y2") ? b.find(L"y2")->asInt(0) : 0;
                p.bindings.push_back(kb);
            }
        }
        if (!p.id.empty()) g_kms.push_back(p);
    }
}

void KeymapsSave() {
    std::lock_guard<std::mutex> lk(g_kmMx);
    JValue v; v.type = JValue::Arr;
    for (auto& p : g_kms) {
        JValue e; e.type = JValue::Obj;
        e.obj[L"id"] = JValue::MakeStr(p.id);
        e.obj[L"name"] = JValue::MakeStr(p.name);
        e.obj[L"packageName"] = JValue::MakeStr(p.packageName);
        e.obj[L"resolutionWidth"] = JValue::MakeNum(p.resW);
        e.obj[L"resolutionHeight"] = JValue::MakeNum(p.resH);
        e.obj[L"orientation"] = JValue::MakeStr(p.orientation);
        e.obj[L"activationHotkey"] = JValue::MakeStr(p.activationHotkey);
        e.obj[L"showOverlay"] = JValue::MakeBool(p.showOverlay);
        e.obj[L"createdAt"] = JValue::MakeStr(p.createdAt);
        e.obj[L"updatedAt"] = JValue::MakeStr(p.updatedAt);
        JValue bs; bs.type = JValue::Arr;
        for (auto& b : p.bindings) bs.arr.push_back(BindingJ(b));
        e.obj[L"bindings"] = bs;
        v.arr.push_back(e);
    }
    WriteText(KeymapLibPath(), JsonWrite(v));
}

KeymapProfile* KeymapById(const std::wstring& id) {
    for (auto& p : g_kms) if (p.id == id) return &p;
    return nullptr;
}

std::wstring KeymapAddDefault() {
    KeymapProfile p;
    p.id = Fmt(L"keymap-%03d", (int)g_kms.size() + 1);
    p.name = g_lang ? L"New layout" : L"Новая раскладка";
    p.createdAt = p.updatedAt = NowIso();
    g_kms.push_back(p);
    KeymapsSave();
    return p.id;
}

bool KeymapDelete(const std::wstring& id) {
    for (size_t i = 0; i < g_kms.size(); ++i) {
        if (g_kms[i].id == id) {
            g_kms.erase(g_kms.begin() + i);
            KeymapsSave();
            return true;
        }
    }
    return false;
}

std::wstring KeymapVkName(int vk) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t buf[32] = {};
    if (GetKeyNameTextW((LONG)sc << 16, buf, 32) && buf[0]) return buf;
    switch (vk) {
        case VK_SPACE: return L"Space"; case VK_RETURN: return L"Enter";
        case VK_ESCAPE: return L"Esc";  case VK_TAB: return L"Tab";
        case VK_UP: return L"Up"; case VK_DOWN: return L"Down";
        case VK_LEFT: return L"Left"; case VK_RIGHT: return L"Right";
        default:
            if (vk >= 'A' && vk <= 'Z') { wchar_t b[2] = { (wchar_t)vk, 0 }; return b; }
            if (vk >= '0' && vk <= '9') { wchar_t b[2] = { (wchar_t)vk, 0 }; return b; }
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return Fmt(L"Num %d", vk - VK_NUMPAD0);
            if (vk >= VK_F1 && vk <= VK_F24) return Fmt(L"F%d", vk - VK_F1 + 1);
            return Fmt(L"0x%02X", vk);
    }
}

// ================================================================ injection engine
static std::mutex g_engMx;
static std::wstring g_engInst, g_engProf;
static bool g_engActive = false;
static HHOOK g_hook = nullptr;
static long long g_lastSentMs = 0;
static std::atomic<int> g_busyAdb{ 0 };

static bool EngMatchHotkey(const KeymapProfile& p, int vk) {
    // hotkey like "F1".."F12"
    if (p.activationHotkey.size() > 0 && p.activationHotkey[0] == L'F') {
        int f = _wtoi(p.activationHotkey.c_str() + 1);
        if (f >= 1 && f <= 12 && vk == VK_F1 + f - 1) return true;
    }
    return false;
}

static void EngSend(const std::wstring& action, int x, int y, int x2, int y2) {
    std::wstring inst, prof;
    {
        std::lock_guard<std::mutex> lk(g_engMx);
        if (!g_engActive) return;
        inst = g_engInst; prof = g_engProf;
    }
    InstanceCfg* c = Inst(inst);
    if (!c) return;
    KeymapProfile* p = KeymapById(prof);
    if (!p) return;
    long long now = (long long)GetTickCount64();
    if (now - g_lastSentMs < 70) return;     // light throttle
    g_lastSentMs = now;

    // scale profile coords to current guest resolution
    double sx = (double)c->resW / (p->resW ? p->resW : c->resW);
    double sy = (double)c->resH / (p->resH ? p->resH : c->resH);
    int tx = (int)(x * sx), ty = (int)(y * sy);
    int tx2 = (int)(x2 * sx), ty2 = (int)(y2 * sy);

    if (g_busyAdb.load() > 3) return;        // don't pile up adb processes
    g_busyAdb++;
    RunOpThread([inst, action, tx, ty, tx2, ty2] {
        std::wstring err, cmd;
        if (action == L"tap")       cmd = Fmt(L"input tap %d %d", tx, ty);
        else if (action == L"longtap") cmd = Fmt(L"input swipe %d %d %d %d 600", tx, ty, tx, ty);
        else if (action == L"swipe")   cmd = Fmt(L"input swipe %d %d %d %d 300", tx, ty, tx2, ty2);
        else if (action == L"back")    cmd = L"input keyevent 4";
        else if (action == L"home")    cmd = L"input keyevent 3";
        else if (action == L"rec")     cmd = L"input keyevent 187";
        else if (action == L"volup")   cmd = L"input keyevent 24";
        else if (action == L"voldn")   cmd = L"input keyevent 25";
        else if (action == L"rotate")  cmd = L"input keyevent 82";
        if (!cmd.empty()) AdbShell(inst, cmd, err, err);
        g_busyAdb--;
    });
}

static LRESULT CALLBACK KeyHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        if (!(k->flags & LLKHF_INJECTED)) {
            int vk = (int)k->vkCode;
            std::wstring inst, prof;
            bool active = false;
            {
                std::lock_guard<std::mutex> lk(g_engMx);
                active = g_engActive;
                inst = g_engInst; prof = g_engProf;
            }
            if (active) {
                KeymapProfile* p = KeymapById(prof);
                Runtime* r = Rt(inst);
                bool vmAlive = r && r->hProc && ProcAlive(r->hProc);
                if (p && vmAlive) {
                    if (EngMatchHotkey(*p, vk)) {
                        std::lock_guard<std::mutex> lk(g_engMx);
                        g_engActive = false;      // hotkey toggles the layout OFF
                        UiNotify(g_lang ? L"Keymap OFF" : L"Раскладка ВЫКЛ", 3);
                        return 1;
                    }
                    for (auto& b : p->bindings) {
                        if (b.vk == vk) {
                            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) EngSend(b.action, b.x, b.y, b.x2, b.y2);
                            return 1;             // swallow original key
                        }
                    }
                } else {
                    std::lock_guard<std::mutex> lk(g_engMx);
                    g_engActive = false;
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void KeymapEngineStart() {
    if (g_hook) return;
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHookProc, nullptr, 0);
    if (g_hook) LogW(L"keymap", L"keyboard hook installed");
}

void KeymapEngineStop() {
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
}

bool KeymapEngineActive() {
    std::lock_guard<std::mutex> lk(g_engMx);
    return g_engActive;
}

void KeymapEngineSetInstance(const std::wstring& instanceId, const std::wstring& profileId) {
    std::lock_guard<std::mutex> lk(g_engMx);
    g_engInst = instanceId;
    g_engProf = profileId;
    g_engActive = !instanceId.empty() && !profileId.empty();
}

std::wstring KeymapEngineStatus() {
    std::lock_guard<std::mutex> lk(g_engMx);
    if (!g_engActive) return g_lang ? L"Off" : L"Выключена";
    InstanceCfg* c = Inst(g_engInst);
    return Fmt(L"%s · %s", (c ? c->name.c_str() : g_engInst.c_str()),
               (g_lang ? L"ON" : L"ВКЛ"));
}
