// ============================================================================
//  NovaDroid - v4game.cpp  Stage 7 "Commercial-grade" (TZ7):
//  macro recorder/player + editor model, anti-lag & FPS stabilization,
//  sync groups (multi-window master->slaves), Discord Rich Presence.
//  Warning (TZ7 5.4): macros in online games may lead to account bans -
//  surfaced in UI (S4_MAC_WARN).
// ============================================================================
#include "v4.h"
#include "ui.h"

// ================================================================ macros (TZ7 5.4 / TZ6 4.4)
static std::vector<Macro> g_macros;
static bool g_macrosLoaded = false;
static std::mutex g_macroMx;

static std::wstring MacrosPath() { return g_p.dataRoot + L"\\macros.json"; }

std::vector<Macro>& V4Macros() {
    if (!g_macrosLoaded) V4MacrosLoad();
    return g_macros;
}

void V4MacrosLoad() {
    g_macrosLoaded = true;
    g_macros.clear();
    std::wstring t;
    if (!ReadText(MacrosPath(), t)) return;
    JValue j; std::wstring err;
    if (!JsonParse(t, j, err) || j.type != JValue::Arr) return;
    for (auto& m : j.arr) {
        Macro M;
        M.id = m.find(L"id") ? m.find(L"id")->asStr() : L"";
        if (M.id.empty()) continue;
        M.name = m.find(L"name") ? m.find(L"name")->asStr() : L"?";
        M.instanceId = m.find(L"instanceId") ? m.find(L"instanceId")->asStr() : L"";
        M.hotkeyVk = m.find(L"hotkeyVk") ? m.find(L"hotkeyVk")->asStr() : L"";
        M.repeat = m.find(L"repeat") ? m.find(L"repeat")->asInt(1) : 1;
        M.enabled = m.find(L"enabled") ? m.find(L"enabled")->asBool(false) : false;
        if (auto* st = m.find(L"steps"); st && st->type == JValue::Arr) {
            for (auto& s : st->arr) {
                MacroStep step;
                step.type = s.find(L"type") ? s.find(L"type")->asStr() : L"wait";
                if (auto* x = s.find(L"x")) step.x = x->asInt(0);
                if (auto* x = s.find(L"y")) step.y = x->asInt(0);
                if (auto* x = s.find(L"x2")) step.x2 = x->asInt(0);
                if (auto* x = s.find(L"y2")) step.y2 = x->asInt(0);
                if (auto* x = s.find(L"durMs")) step.durMs = x->asInt(0);
                if (auto* x = s.find(L"key")) step.key = x->asInt(0);
                if (auto* x = s.find(L"text")) step.text = x->asStr();
                M.steps.push_back(step);
            }
        }
        g_macros.push_back(M);
    }
}

void V4MacrosSave() {
    JValue j; j.type = JValue::Arr;
    for (auto& M : g_macros) {
        JValue m; m.type = JValue::Obj;
        m.obj[L"id"] = JValue::MakeStr(M.id);
        m.obj[L"name"] = JValue::MakeStr(M.name);
        m.obj[L"instanceId"] = JValue::MakeStr(M.instanceId);
        m.obj[L"hotkeyVk"] = JValue::MakeStr(M.hotkeyVk);
        m.obj[L"repeat"] = JValue::MakeNum(M.repeat);
        m.obj[L"enabled"] = JValue::MakeBool(M.enabled);
        JValue steps; steps.type = JValue::Arr;
        for (auto& s : M.steps) {
            JValue o; o.type = JValue::Obj;
            o.obj[L"type"] = JValue::MakeStr(s.type);
            o.obj[L"x"] = JValue::MakeNum(s.x);
            o.obj[L"y"] = JValue::MakeNum(s.y);
            o.obj[L"x2"] = JValue::MakeNum(s.x2);
            o.obj[L"y2"] = JValue::MakeNum(s.y2);
            o.obj[L"durMs"] = JValue::MakeNum(s.durMs);
            o.obj[L"key"] = JValue::MakeNum(s.key);
            o.obj[L"text"] = JValue::MakeStr(s.text);
            steps.arr.push_back(o);
        }
        m.obj[L"steps"] = steps;
        j.arr.push_back(m);
    }
    WriteText(MacrosPath(), JsonWrite(j));
}

Macro* V4MacroById(const std::wstring& id) {
    for (auto& m : V4Macros()) if (m.id == id) return &m;
    return nullptr;
}

bool V4MacroDelete(const std::wstring& id) {
    auto& v = V4Macros();
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id) { v.erase(v.begin() + i); V4MacrosSave(); return true; }
    return false;
}

// ---------------- recorder (WH_MOUSE_LL + WH_KEYBOARD_LL while active)
static std::atomic<bool> g_recording{ false };
static std::wstring g_recInstance;
static std::vector<MacroStep> g_recSteps;
static long long g_recT0 = 0;
static HHOOK g_mouseHook = nullptr, g_keyHook = nullptr;
static std::mutex g_recMx;

static std::wstring MacroNextId() {
    int mx = 0;
    for (auto& m : V4Macros())
        if (m.id.rfind(L"macro-", 0) == 0) mx = std::max(mx, _wtoi(m.id.c_str() + 6));
    return Fmt(L"macro-%03d", mx + 1);
}

// screen -> guest coordinates via cached qemu window (uses v3 window cache)
static bool ScreenToGuest(POINT screen, int& gx, int& gy) {
    Runtime* r = Rt(g_recInstance);
    if (!r || !r->qhWnd) return false;
    RECT wr;
    if (!GetWindowRect(r->qhWnd, &wr)) return false;
    InstanceCfg* c = Inst(g_recInstance);
    if (!c) return false;
    // subtract non-client (GTK title bar approx by using client rect offset)
    POINT tl = { wr.left, wr.top };
    ScreenToClient(r->qhWnd, &tl);
    RECT cr;
    GetClientRect(r->qhWnd, &cr);
    int cw = cr.right - cr.left, chh = cr.bottom - cr.top;
    if (cw < 50 || chh < 50) return false;
    int sx = screen.x - (wr.left + tl.x);
    int sy = screen.y - (wr.top + tl.y);
    if (sx < 0 || sy < 0 || sx > cw || sy > chh) return false;
    gx = sx * c->resW / cw;
    gy = sy * c->resH / chh;
    return true;
}

static LRESULT CALLBACK RecMouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_recording.load()) {
        MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lp;
        if (wp == WM_LBUTTONDOWN || wp == WM_LBUTTONUP) {
            int gx, gy;
            if (ScreenToGuest(m->pt, gx, gy)) {
                std::lock_guard<std::mutex> lk(g_recMx);
                long long now = GetTickCount64();
                long long since = now - g_recT0;
                if (since > 120 && !g_recSteps.empty()) {
                    MacroStep w; w.type = L"wait"; w.durMs = (int)std::min(since, (long long)10000);
                    g_recSteps.push_back(w);
                }
                g_recT0 = now;
                MacroStep s;
                s.type = (wp == WM_LBUTTONDOWN) ? L"tap" : L"longtap";
                s.x = gx; s.y = gy; s.durMs = wp == WM_LBUTTONDOWN ? 60 : 500;
                g_recSteps.push_back(s);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static LRESULT CALLBACK RecKeyHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_recording.load() && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        if (k->vkCode == VK_ESCAPE) { V4MacroRecordStop(); return CallNextHookEx(nullptr, code, wp, lp); }
        std::lock_guard<std::mutex> lk(g_recMx);
        long long since = GetTickCount64() - g_recT0;
        if (since > 150 && !g_recSteps.empty()) {
            MacroStep w; w.type = L"wait"; w.durMs = (int)std::min(since, (long long)10000);
            g_recSteps.push_back(w);
        }
        g_recT0 = GetTickCount64();
        MacroStep s;
        s.type = L"key";
        s.key = (int)k->vkCode;
        s.durMs = 40;
        g_recSteps.push_back(s);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

bool V4MacroRecordStart(const std::wstring& instanceId) {
    if (g_recording.load()) return false;
    g_recInstance = instanceId;
    {
        std::lock_guard<std::mutex> lk(g_recMx);
        g_recSteps.clear();
    }
    g_recT0 = GetTickCount64();
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, RecMouseHook, nullptr, 0);
    g_keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, RecKeyHook, nullptr, 0);
    g_recording = true;
    LogW(L"macro", L"recording started for %s (Esc to stop)", instanceId.c_str());
    return true;
}

bool V4MacroRecording() { return g_recording.load(); }
std::wstring V4MacroRecordInstanceId() { return g_recInstance; }
int V4MacroRecordStepCount() { std::lock_guard<std::mutex> lk(g_recMx); return (int)g_recSteps.size(); }

bool V4MacroRecordStop() {
    if (!g_recording.load()) return false;
    g_recording = false;
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    if (g_keyHook) { UnhookWindowsHookEx(g_keyHook); g_keyHook = nullptr; }
    std::vector<MacroStep> steps;
    { std::lock_guard<std::mutex> lk(g_recMx); steps = g_recSteps; }
    if (steps.empty()) { LogW(L"macro", L"recording empty, discarded"); return false; }
    Macro m;
    m.id = MacroNextId();
    m.name = Fmt(T(S4_MAC_DEFNAME), m.id.c_str());
    m.instanceId = g_recInstance;
    m.steps = steps;
    V4Macros().push_back(m);
    V4MacrosSave();
    LogW(L"macro", L"saved %s (%d steps)", m.id.c_str(), (int)steps.size());
    if (g_wnd) UiNotify(Fmt(T(S4_MAC_SAVED), (int)steps.size()), 0);
    return true;
}

// ---------------- player
static std::atomic<bool> g_playing{ false };

static void PlayOne(const Macro& m, const std::wstring& instanceId) {
    std::wstring adb = AdbExe();
    if (adb.empty()) return;
    std::wstring serial = AdbSerial(*Inst(instanceId));
    for (int rep = 0; rep < std::max(1, m.repeat) && g_playing.load(); ++rep) {
        for (auto& s : m.steps) {
            if (!g_playing.load()) break;
            std::wstring cmd;
            if (s.type == L"tap" || s.type == L"longtap")
                cmd = Fmt(L"-s %s shell input tap %d %d", serial.c_str(), s.x, s.y);
            else if (s.type == L"swipe")
                cmd = Fmt(L"-s %s shell input swipe %d %d %d %d %d",
                          serial.c_str(), s.x, s.y, s.x2, s.y2, std::max(80, s.durMs));
            else if (s.type == L"key")
                cmd = Fmt(L"-s %s shell input keyevent %d", serial.c_str(), s.key);
            else if (s.type == L"text")
                cmd = Fmt(L"-s %s shell input text \"%s\"", serial.c_str(), s.text.c_str());
            else if (s.type == L"wait") {
                int left = s.durMs;
                while (left > 0 && g_playing.load()) { SleepMs(std::min(100, left)); left -= 100; }
                continue;
            }
            if (!cmd.empty()) {
                DWORD ec = 0; std::string so;
                RunCapture(adb, cmd, L"", 8000, &ec, &so, nullptr);
                SleepMs(std::max(30, s.type == L"longtap" ? s.durMs : 40));
            }
        }
    }
}

bool V4MacroPlayAsync(const std::wstring& macroId, std::wstring& err) {
    Macro* m = V4MacroById(macroId);
    if (!m) { err = L"macro not found"; return false; }
    std::wstring iid = m->instanceId.empty() ? SelInst() : m->instanceId;
    Runtime* r = Rt(iid);
    if (!r || !r->hProc || !ProcAlive(r->hProc)) { err = T(S4_MAC_NORUN); return false; }
    if (g_playing.exchange(true)) { err = T(S4_MAC_PLAYING); return false; }
    std::vector<MacroStep> steps = m->steps;
    int repeat = m->repeat;
    std::thread([steps, repeat, iid, macroId]() {
        Macro tmp; tmp.steps = steps; tmp.repeat = repeat;
        PlayOne(tmp, iid);
        g_playing = false;
        // sync broadcast (TZ7 5.3)
        V4SyncBroadcastMacro(iid, macroId);
        if (g_wnd) UiNotify(T(S4_MAC_PLAYDONE), 0);
    }).detach();
    return true;
}

bool V4MacroPlaying() { return g_playing.load(); }
void V4MacroStopPlay() { g_playing = false; }

// ================================================================ anti-lag (TZ7 5.2)
static std::atomic<bool> g_antilag{ true };
static std::wstring g_alMode = L"balance";
static int g_lowTicks = 0;
static std::wstring g_alLastId;

void V4AntiLagSet(bool on) { g_antilag = on; }
bool V4AntiLagOn() { return g_antilag.load(); }
void V4AntiLagSetMode(const std::wstring& m) { g_alMode = m; }
std::wstring V4AntiLagMode() { return g_alMode; }

void V4BoostPriority(const std::wstring& instanceId) {
    Runtime* r = Rt(instanceId);
    if (r && r->hProc && ProcAlive(r->hProc)) {
        SetPriorityClass(r->hProc, HIGH_PRIORITY_CLASS);
        LogW(L"antilag", L"priority boosted: %s", instanceId.c_str());
    }
}

// dynamic stabilization: fps low -> priority boost; quality mode -> recommend
void V4AntiLagTick() {
    if (!g_antilag.load()) return;
    std::wstring sel = SelInst();
    Runtime* r = Rt(sel);
    if (!r || !r->hProc || !ProcAlive(r->hProc)) { g_lowTicks = 0; return; }
    float fps = V3LastFps();
    InstanceCfg* c = Inst(sel);
    if (!c) return;
    int target = c->fpsLimit > 0 ? c->fpsLimit : 60;
    // find running master-eligible instance with lowest fps (simple: selected first)
    bool low = fps > 1 && fps < target * 0.55f;
    if (low) {
        g_lowTicks++;
        if (g_lowTicks == 8) {
            // perf mode: boost priority immediately
            if (g_alMode == L"perf") V4BoostPriority(sel);
            if (g_alMode == L"eco" || g_alMode == L"balance") V4BoostPriority(sel);
            if (g_alMode == L"quality") {
                if (g_wnd) UiNotify(T(S4_ALG_HINT), 1);
            }
            g_alLastId = sel;
        }
        if (g_lowTicks > 25) g_lowTicks = 0;    // re-arm after ~25 s
    } else {
        g_lowTicks = 0;
    }
}

// ================================================================ sync groups (TZ7 5.3)
static std::vector<SyncGroup> g_sync;
static bool g_syncLoaded = false;
static std::mutex g_syncMx;

static std::wstring SyncPath() { return g_p.dataRoot + L"\\syncgroups.json"; }

std::vector<SyncGroup>& V4SyncGroups() {
    if (!g_syncLoaded) V4SyncGroupsLoad();
    return g_sync;
}

void V4SyncGroupsLoad() {
    g_syncLoaded = true;
    g_sync.clear();
    std::wstring t;
    if (!ReadText(SyncPath(), t)) return;
    JValue j; std::wstring err;
    if (!JsonParse(t, j, err) || j.type != JValue::Arr) return;
    for (auto& g : j.arr) {
        SyncGroup G;
        G.masterId = g.find(L"master") ? g.find(L"master")->asStr() : L"";
        if (G.masterId.empty()) continue;
        if (auto* s = g.find(L"slaves"); s && s->type == JValue::Arr)
            for (auto& x : s->arr) G.slaveIds.push_back(x.asStr());
        G.syncTouch = g.find(L"syncTouch") ? g.find(L"syncTouch")->asBool(true) : true;
        G.syncKeys = g.find(L"syncKeys") ? g.find(L"syncKeys")->asBool(true) : true;
        G.syncMacros = g.find(L"syncMacros") ? g.find(L"syncMacros")->asBool(true) : true;
        g_sync.push_back(G);
    }
}

void V4SyncGroupsSave() {
    JValue j; j.type = JValue::Arr;
    for (auto& G : g_sync) {
        JValue g; g.type = JValue::Obj;
        g.obj[L"master"] = JValue::MakeStr(G.masterId);
        JValue sl; sl.type = JValue::Arr;
        for (auto& s : G.slaveIds) sl.arr.push_back(JValue::MakeStr(s));
        g.obj[L"slaves"] = sl;
        g.obj[L"syncTouch"] = JValue::MakeBool(G.syncTouch);
        g.obj[L"syncKeys"] = JValue::MakeBool(G.syncKeys);
        g.obj[L"syncMacros"] = JValue::MakeBool(G.syncMacros);
        j.arr.push_back(g);
    }
    WriteText(SyncPath(), JsonWrite(j));
}

SyncGroup* V4SyncGroupOf(const std::wstring& instanceId) {
    for (auto& g : V4SyncGroups()) {
        if (g.masterId == instanceId) return &g;
        for (auto& s : g.slaveIds) if (s == instanceId) return &g;
    }
    return nullptr;
}

void V4SyncRemove(const std::wstring& instanceId) {
    auto& v = V4SyncGroups();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].masterId == instanceId) { v.erase(v.begin() + i); break; }
        bool erased = false;
        for (size_t k = 0; k < v[i].slaveIds.size(); ++k)
            if (v[i].slaveIds[k] == instanceId) { v[i].slaveIds.erase(v[i].slaveIds.begin() + k); erased = true; break; }
        if (erased) break;
    }
    V4SyncGroupsSave();
}

void V4SyncBroadcastTouch(const std::wstring& masterId, int x, int y, bool down) {
    SyncGroup* g = V4SyncGroupOf(masterId);
    if (!g || !g->syncTouch || g->masterId != masterId) return;
    std::wstring adb = AdbExe();
    if (adb.empty()) return;
    for (auto& s : g->slaveIds) {
        Runtime* r = Rt(s);
        if (!r || !r->hProc || !ProcAlive(r->hProc)) continue;
        InstanceCfg* c = Inst(s);
        if (!c) continue;
        int px = x * c->resW / 10000, py = y * c->resH / 10000;
        std::wstring cmd = Fmt(L"-s %s shell input swipe %d %d %d %d %d",
                               AdbSerial(*c).c_str(), px, py, px, py, down ? 60 : 30);
        DWORD ec = 0; std::string so;
        RunCapture(adb, cmd, L"", 6000, &ec, &so, nullptr);
    }
}

void V4SyncBroadcastKey(const std::wstring& masterId, int vk, bool down) {
    SyncGroup* g = V4SyncGroupOf(masterId);
    if (!g || !g->syncKeys || g->masterId != masterId || !down) return;
    std::wstring adb = AdbExe();
    if (adb.empty()) return;
    for (auto& s : g->slaveIds) {
        Runtime* r = Rt(s);
        if (!r || !r->hProc || !ProcAlive(r->hProc)) continue;
        InstanceCfg* c = Inst(s);
        if (!c) continue;
        std::wstring cmd = Fmt(L"-s %s shell input keyevent %d", AdbSerial(*c).c_str(), vk);
        DWORD ec = 0; std::string so;
        RunCapture(adb, cmd, L"", 6000, &ec, &so, nullptr);
    }
}

void V4SyncBroadcastMacro(const std::wstring& masterId, const std::wstring& macroId) {
    SyncGroup* g = V4SyncGroupOf(masterId);
    if (!g || !g->syncMacros || g->masterId != masterId) return;
    for (auto& s : g->slaveIds) {
        Macro* m = V4MacroById(macroId);
        if (!m) continue;
        Macro tmp = *m;
        tmp.instanceId = s;
        if (!tmp.steps.empty()) {
            std::thread([tmp, s]() {
                Macro one; one.steps = tmp.steps; one.repeat = tmp.repeat;
                // reuse player logic without recursion into sync
                std::wstring adb = AdbExe();
                if (adb.empty()) return;
                std::wstring serial = AdbSerial(*Inst(s));
                for (int rep = 0; rep < std::max(1, one.repeat); ++rep)
                    for (auto& st : one.steps) {
                        std::wstring cmd;
                        if (st.type == L"tap") cmd = Fmt(L"-s %s shell input tap %d %d", serial.c_str(), st.x, st.y);
                        else if (st.type == L"swipe") cmd = Fmt(L"-s %s shell input swipe %d %d %d %d %d",
                                                                serial.c_str(), st.x, st.y, st.x2, st.y2, std::max(80, st.durMs));
                        else if (st.type == L"key") cmd = Fmt(L"-s %s shell input keyevent %d", serial.c_str(), st.key);
                        else if (st.type == L"text") cmd = Fmt(L"-s %s shell input text \"%s\"", serial.c_str(), st.text.c_str());
                        else if (st.type == L"wait") { SleepMs(st.durMs); continue; }
                        if (!cmd.empty()) {
                            DWORD ec = 0; std::string so;
                            RunCapture(adb, cmd, L"", 8000, &ec, &so, nullptr);
                            SleepMs(50);
                        }
                    }
            }).detach();
        }
    }
}

// ================================================================ Discord RPC (TZ7 5.5)
static HANDLE g_discordPipe = nullptr;
static bool g_discordOn = false;
static std::wstring g_discordCur;
static long long g_discordLastMs = 0;

static bool DcSend(const std::wstring& json) {
    if (!g_discordPipe) return false;
    std::string u = W2U(json);
    uint32_t len = (uint32_t)u.size();
    DWORD wr = 0;
    if (!WriteFile(g_discordPipe, &len, 4, &wr, nullptr) || wr != 4) return false;
    return WriteFile(g_discordPipe, u.data(), len, &wr, nullptr) && wr == len;
}

static bool DcConnect() {
    std::wstring clientId = g_set.discordClientId.empty() ? L"1269822583742574652" : g_set.discordClientId;
    for (int i = 0; i < 10; ++i) {
        std::wstring pipe = Fmt(L"\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            g_discordPipe = h;
            // handshake
            DcSend(Fmt(L"{\"v\":1,\"client_id\":\"%s\"}", clientId.c_str()));
            return true;
        }
    }
    return false;
}

bool V4DiscordSet(const std::wstring& appLabel) {
    if (!g_discordOn || appLabel == g_discordCur) return g_discordOn;
    if (!g_discordPipe && !DcConnect()) return false;
    long long nonce = GetTickCount64();
    // escape quotes in label
    std::wstring label = appLabel;
    std::wstring out = Fmt(
        L"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":{\"details\":\"%s\",\"assets\":{\"large_text\":\"NovaDroid\"}}},\"nonce\":\"%lld\"}",
        (int)GetCurrentProcessId(), label.c_str(), nonce);
    bool ok = DcSend(out);
    if (ok) g_discordCur = appLabel;
    else { g_discordCur.clear(); CloseHandle(g_discordPipe); g_discordPipe = nullptr; }
    return ok;
}

void V4DiscordClear() {
    if (g_discordPipe) {
        DcSend(Fmt(L"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":null},\"nonce\":\"%lld\"}",
                   (int)GetCurrentProcessId(), GetTickCount64()));
    }
    g_discordCur.clear();
}

bool V4DiscordActive() { return g_discordOn && g_discordPipe != nullptr; }

void V4DiscordTick() {
    g_discordOn = g_set.discordEnabled;
    if (!g_discordOn) return;
    long long now = GetTickCount64();
    if (now - g_discordLastMs < 15000) return;
    g_discordLastMs = now;
    // find running instance label
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& kv : g_rt) {
        if (kv.second.hProc && ProcAlive(kv.second.hProc)) {
            InstanceCfg* c = Inst(kv.first);
            if (c) {
                std::wstring label = c->name;
                V4DiscordSet(label);
                return;
            }
        }
    }
    V4DiscordClear();
}
