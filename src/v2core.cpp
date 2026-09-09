// ============================================================================
//  NovaDroid - v2core.cpp  Stage 2 services: snapshots (qemu-img), clones
//  (full/linked), backups v2 (.novadroid-backup), resource guard (TZ 5-9).
// ============================================================================
#include "app.h"
#include "v2.h"

// ================================================================ snapshots
static std::wstring SnapStorePath(const std::wstring& instanceId) {
    return g_p.instances + L"\\" + instanceId + L"\\snapshots.json";
}

std::vector<Snapshot> LoadSnapshots(const std::wstring& instanceId) {
    std::vector<Snapshot> out;
    std::wstring txt;
    if (!ReadText(SnapStorePath(instanceId), txt)) return out;
    JValue v; std::wstring err;
    if (!JsonParse(txt, v, err)) return out;
    if (v.type != JValue::Arr) return out;
    for (auto& e : v.arr) {
        Snapshot s;
        s.id = e.find(L"id") ? e.find(L"id")->asStr() : L"";
        s.instanceId = e.find(L"instanceId") ? e.find(L"instanceId")->asStr() : instanceId;
        s.name = e.find(L"name") ? e.find(L"name")->asStr() : L"";
        s.displayName = e.find(L"displayName") ? e.find(L"displayName")->asStr() : s.name;
        s.description = e.find(L"description") ? e.find(L"description")->asStr() : L"";
        s.type = e.find(L"type") ? e.find(L"type")->asStr() : L"user";
        s.isProtected = e.find(L"isProtected") ? e.find(L"isProtected")->asBool() : false;
        s.createdAt = e.find(L"createdAt") ? e.find(L"createdAt")->asStr() : L"";
        s.diskPath = e.find(L"diskPath") ? e.find(L"diskPath")->asStr() : L"";
        s.sizeBytes = e.find(L"sizeBytes") ? e.find(L"sizeBytes")->asInt64() : 0;
        out.push_back(s);
    }
    return out;
}

bool SaveSnapshots(const std::wstring& instanceId, const std::vector<Snapshot>& list) {
    JValue v; v.type = JValue::Arr;
    for (auto& s : list) {
        JValue e; e.type = JValue::Obj;
        e.obj[L"id"] = JValue::MakeStr(s.id);
        e.obj[L"instanceId"] = JValue::MakeStr(s.instanceId);
        e.obj[L"name"] = JValue::MakeStr(s.name);
        e.obj[L"displayName"] = JValue::MakeStr(s.displayName);
        e.obj[L"description"] = JValue::MakeStr(s.description);
        e.obj[L"type"] = JValue::MakeStr(s.type);
        e.obj[L"isProtected"] = JValue::MakeBool(s.isProtected);
        e.obj[L"createdAt"] = JValue::MakeStr(s.createdAt);
        e.obj[L"diskPath"] = JValue::MakeStr(s.diskPath);
        e.obj[L"sizeBytes"] = JValue::MakeNum((double)s.sizeBytes);
        v.arr.push_back(e);
    }
    return WriteText(SnapStorePath(instanceId), JsonWrite(v));
}

// qemu-img snapshot tag: letters/digits/dash only
static std::wstring SnapTag(const std::wstring& name) {
    std::wstring t;
    for (wchar_t ch : name) {
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_')
            t += ch;
        else t += L'-';
    }
    if (t.empty()) t = L"snap";
    return t;
}

static std::wstring DiskOf(const InstanceCfg& c) {
    return c.Dir() + L"\\disk." + (c.diskFormat == L"raw" ? L"raw" : L"qcow2");
}

std::wstring SnapshotCreate(const std::wstring& instanceId, const std::wstring& displayName,
                            const std::wstring& desc, const wchar_t* type, bool protect,
                            std::wstring& err) {
    InstanceCfg* c = Inst(instanceId);
    if (!c) { err = L"not found"; return L""; }
    Runtime* r = Rt(instanceId);
    if (r && r->hProc && ProcAlive(r->hProc)) {
        err = g_lang ? L"Stop the instance before taking a snapshot"
                     : L"Остановите инстанс перед созданием снимка";
        return L"";
    }
    std::wstring qi = QemuImg();
    if (qi.empty()) { err = T(S_MB_QEMUMISS); return L""; }
    std::wstring disk = DiskOf(*c);
    if (!FE(disk)) { err = g_lang ? L"Disk image not found" : L"Файл диска не найден"; return L""; }

    // unique tag
    auto existing = LoadSnapshots(instanceId);
    std::wstring tag = SnapTag(displayName);
    std::wstring full = tag;
    int n = 2;
    while (true) {
        bool clash = false;
        for (auto& s : existing) if (s.name == full) { clash = true; break; }
        if (!clash) break;
        full = tag + Fmt(L"-%d", n++);
    }
    MK(c->Dir() + L"\\snapshots");

    DWORD ec = 0; std::string so, se;
    RunCapture(qi, L"snapshot -c " + Qn(full) + L" " + Qn(disk), c->Dir(), 300000, &ec, &so, &se);
    if (!FE(disk) || ec != 0) {
        err = g_lang ? L"qemu-img snapshot failed: " : L"Ошибка qemu-img snapshot: ";
        err += U2W(se).empty() ? U2W(so) : U2W(se);
        LogW(L"snap", L"create failed %s: %s", instanceId.c_str(), err.c_str());
        return L"";
    }
    // real tag qemu applied may differ (already-exists auto-rename) - list and take newest
    std::wstring realTag = full;
    RunCapture(qi, L"snapshot -l " + Qn(disk), c->Dir(), 60000, &ec, &so, &se);
    std::wstring lst = U2W(so);
    size_t p = lst.find(full);
    if (p != std::wstring::npos) {
        // qemu renames to tag1.. if collision; extract token from that line
        size_t e = lst.find(L'\n', p);
        std::wstring line = lst.substr(p, e == std::wstring::npos ? std::wstring::npos : e - p);
        size_t t0 = line.find_first_not_of(L" \t");
        size_t t1 = line.find_first_of(L" \t", t0);
        if (t0 != std::wstring::npos && t1 != std::wstring::npos) realTag = line.substr(t0, t1 - t0);
    }

    Snapshot s;
    s.id = Fmt(L"snap-%03d", (int)existing.size() + 1);
    s.instanceId = instanceId;
    s.name = realTag;
    s.displayName = displayName;
    s.description = desc;
    s.type = type;
    s.isProtected = protect;
    s.createdAt = NowIso();
    s.diskPath = std::wstring(L"disk.") + (c->diskFormat == L"raw" ? L"raw" : L"qcow2");
    s.sizeBytes = FileSizeOf(disk);
    existing.push_back(s);
    SaveSnapshots(instanceId, existing);
    ILog(instanceId, L"launcher", L"snapshot created: " + realTag);
    LogW(L"snap", L"created %s (%s)", realTag.c_str(), instanceId.c_str());
    return s.id;
}

bool SnapshotRestore(const std::wstring& id, const std::wstring& snapId, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    Runtime* r = Rt(id);
    if (r && r->hProc && ProcAlive(r->hProc)) {
        err = g_lang ? L"Stop the instance before restoring a snapshot"
                     : L"Остановите инстанс перед восстановлением снимка";
        return false;
    }
    auto list = LoadSnapshots(id);
    Snapshot* s = nullptr;
    for (auto& x : list) if (x.id == snapId) { s = &x; break; }
    if (!s) { err = L"snapshot not found"; return false; }
    std::wstring qi = QemuImg();
    if (qi.empty()) { err = T(S_MB_QEMUMISS); return false; }
    std::wstring disk = DiskOf(*c);

    // TZ 8.3: auto emergency backup of current state before restore
    ULARGE_INTEGER freeB, totB;
    GetDiskFreeSpaceExW(g_p.dataRoot.c_str(), &freeB, &totB, nullptr);
    long long need = FileSizeOf(disk);
    if ((long long)freeB.QuadPart > need + 512LL * 1024 * 1024) {
        std::wstring e2;
        if (BackupInstanceV2(id, true, false, e2))
            ILog(id, L"launcher", L"auto-backup before snapshot restore: OK");
        else
            ILog(id, L"launcher", L"auto-backup before restore skipped: " + e2);
    } else {
        ILog(id, L"launcher", L"auto-backup skipped: not enough disk space");
    }

    DWORD ec = 0; std::string so, se;
    RunCapture(qi, L"snapshot -a " + Qn(s->name) + L" " + Qn(disk), c->Dir(), 300000, &ec, &so, &se);
    if (ec != 0) {
        err = g_lang ? L"qemu-img restore failed: " : L"Ошибка восстановления qemu-img: ";
        err += U2W(se).empty() ? U2W(so) : U2W(se);
        LogW(L"snap", L"restore failed %s: %s", id.c_str(), err.c_str());
        return false;
    }
    ILog(id, L"launcher", L"snapshot restored: " + s->name);
    LogW(L"snap", L"restored %s -> %s", s->name.c_str(), id.c_str());
    return true;
}

bool SnapshotDelete(const std::wstring& id, const std::wstring& snapId, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    auto list = LoadSnapshots(id);
    Snapshot* s = nullptr;
    for (auto& x : list) if (x.id == snapId) { s = &x; break; }
    if (!s) { err = L"snapshot not found"; return false; }
    if (s->isProtected) {
        err = g_lang ? L"Protected snapshot: confirm deletion in the dialog"
                     : L"Защищённый снимок: подтвердите удаление в диалоге";
    }
    std::wstring qi = QemuImg();
    if (qi.empty()) { err = T(S_MB_QEMUMISS); return false; }
    std::wstring disk = DiskOf(*c);
    DWORD ec = 0; std::string so, se;
    RunCapture(qi, L"snapshot -d " + Qn(s->name) + L" " + Qn(disk), c->Dir(), 300000, &ec, &so, &se);
    if (ec != 0) {
        err = g_lang ? L"qemu-img delete failed: " : L"Ошибка удаления qemu-img: ";
        err += U2W(se).empty() ? U2W(so) : U2W(se);
        return false;
    }
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == snapId) { list.erase(list.begin() + i); break; }
    SaveSnapshots(id, list);
    ILog(id, L"launcher", L"snapshot deleted: " + s->name);
    return true;
}

// TZ 6.2: "Clean Install" protected system snapshot right after creation
void SnapshotEnsureCleanInstall(const std::wstring& instanceId) {
    auto list = LoadSnapshots(instanceId);
    for (auto& s : list)
        if (s.type == L"system") return;               // already there
    std::wstring err;
    SnapshotCreate(instanceId,
                   g_lang ? L"Clean Install" : L"Чистая установка",
                   g_lang ? L"Clean Android state right after instance creation."
                          : L"Чистое состояние Android сразу после создания инстанса.",
                   L"system", true, err);
    if (!err.empty()) LogW(L"snap", L"clean install snapshot deferred: %s", err.c_str());
}

// ================================================================ clone v2 (TZ 7)
std::vector<std::wstring> LinkedChildren(const std::wstring& id) {
    std::vector<std::wstring> out;
    std::wstring myDisk = g_p.instances + L"\\" + id + L"\\disk.qcow2";
    for (auto& c : g_insts) {
        if (c.id == id) continue;
        if (c.cloneType == L"linked" && c.parentInstanceId == id) out.push_back(c.id);
    }
    return out;
}

bool CloneInstanceV2(const std::wstring& id, const std::wstring& newName, bool linked,
                     bool copyKeymaps, bool copyShared, std::wstring& newId, std::wstring& err) {
    std::lock_guard<std::mutex> lk(g_mx);
    InstanceCfg* src = Inst(id);
    Runtime* r = Rt(id);
    if (!src) { err = L"not found"; return false; }
    if (r && (r->hProc && ProcAlive(r->hProc))) {
        err = g_lang ? L"Stop the source instance before cloning (consistent clone)."
                     : L"Остановите исходный инстанс перед клонированием (консистентный клон).";
        return false;
    }
    if (src->cloneType == L"linked" && linked) {
        // linked of linked -> rebase onto same parent, fine, but keep rule: parent must exist
    }
    InstanceCfg c = *src;
    c.id = NextInstanceId();
    c.name = newName;
    c.createdAt = NowIso();
    c.lastLaunchAt = L"";
    c.lastShutdownAt = L"";
    c.adbPort = NextFreePort();
    c.cloneType = linked ? L"linked" : L"full";
    c.parentInstanceId = linked ? id : L"";
    EnsureDefaultSharedFolder(c);
    MK(c.Dir()); MK(c.Dir() + L"\\logs"); MK(c.Dir() + L"\\snapshots"); MK(c.Dir() + L"\\shared");

    std::wstring srcDisk = DiskOf(*src);
    std::wstring dstDisk = DiskOf(c);
    std::wstring qi = QemuImg();
    if (FE(srcDisk)) {
        if (!linked) {
            // full clone: qemu-img convert (safe copy) with fallback to CopyFile
            if (!qi.empty()) {
                DWORD ec = 0; std::string so, se;
                RunCapture(qi, L"convert -O " + c.diskFormat + L" " + Qn(srcDisk) + L" " + Qn(dstDisk),
                           c.Dir(), 900000, &ec, &so, &se);
            }
            if (!FE(dstDisk)) CopyFileW(srcDisk.c_str(), dstDisk.c_str(), FALSE);
            if (!FE(dstDisk)) { err = g_lang ? L"Disk copy failed" : L"Не удалось скопировать диск"; return false; }
        } else {
            if (qi.empty()) { err = T(S_MB_QEMUMISS); return false; }
            if (src->diskFormat != L"qcow2") {
                err = g_lang ? L"Linked clone requires qcow2 disk format."
                             : L"Связанный клон требует формат диска qcow2.";
                return false;
            }
            DWORD ec = 0; std::string so, se;
            RunCapture(qi, L"create -f qcow2 -b " + Qn(srcDisk) + L" -F qcow2 " + Qn(dstDisk),
                       c.Dir(), 120000, &ec, &so, &se);
            if (!FE(dstDisk)) { err = g_lang ? L"Linked clone creation failed: " : L"Ошибка создания связанного клона: " +
                                 U2W(se); return false; }
        }
    } else if (linked) {
        err = g_lang ? L"Source disk missing - cannot create linked clone."
                     : L"Диск исходного инстанса отсутствует - связанный клон невозможен.";
        return false;
    }
    // copy config artifacts
    if (copyShared && src->sharedFolderPath.size() > src->Dir().size()) {
        CopyTree(src->sharedFolderPath, c.sharedFolderPath);
    }
    PersistInstance(c);
    g_insts.push_back(c);
    g_rt[c.id] = Runtime();
    LogW(L"inst", L"cloned %s -> %s (linked=%d)", id.c_str(), c.id.c_str(), linked ? 1 : 0);
    ILog(c.id, L"launcher", L"cloned from " + id + (linked ? L" (linked)" : L" (full)"));
    newId = c.id;
    return true;
}

// ================================================================ backups v2 (TZ 9)
std::vector<BackupInfo> ListBackups() {
    std::vector<BackupInfo> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_p.backups + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        BackupInfo bi;
        bi.dir = g_p.backups + L"\\" + fd.cFileName;
        bi.name = fd.cFileName;
        bi.hasDisk = FE(bi.dir + L"\\disk.qcow2") || FE(bi.dir + L"\\disk.raw");
        bi.hasManifest = FE(bi.dir + L"\\manifest.json");
        bi.bytes = 0;
        // size walk (top level only, fast enough)
        WIN32_FIND_DATAW f2;
        HANDLE h2 = FindFirstFileW((bi.dir + L"\\*").c_str(), &f2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do { if (!(f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                     bi.bytes += ((long long)f2.nFileSizeHigh << 32) | f2.nFileSizeLow; }
            while (FindNextFileW(h2, &f2));
            FindClose(h2);
        }
        std::wstring txt;
        if (ReadText(bi.dir + L"\\manifest.json", txt)) {
            JValue v; std::wstring e;
            if (JsonParse(txt, v, e)) {
                bi.id = v.find(L"instanceId") ? v.find(L"instanceId")->asStr() : L"";
                std::wstring nm = v.find(L"instanceName") ? v.find(L"instanceName")->asStr() : L"";
                bi.created = v.find(L"createdAt") ? v.find(L"createdAt")->asStr() : L"";
                bi.ver = v.find(L"novaDroidVersion") ? v.find(L"novaDroidVersion")->asStr() : L"";
                bi.name = nm.empty() ? bi.name : nm + L" (" + fd.cFileName + L")";
            }
        }
        out.push_back(bi);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.dir > b.dir; });
    return out;
}

static JValue ManifestFor(const InstanceCfg& c, bool withSnapshots) {
    JValue m; m.type = JValue::Obj;
    m.obj[L"formatVersion"] = JValue::MakeNum(1);
    m.obj[L"instanceId"] = JValue::MakeStr(c.id);
    m.obj[L"instanceName"] = JValue::MakeStr(c.name);
    m.obj[L"createdAt"] = JValue::MakeStr(NowIso());
    m.obj[L"androidVersion"] = JValue::MakeStr(c.androidVersion);
    m.obj[L"novaDroidVersion"] = JValue::MakeStr(L"0.2.0");
    m.obj[L"includesSnapshots"] = JValue::MakeBool(withSnapshots);
    return m;
}

bool BackupInstanceV2(const std::wstring& id, bool withDisk, bool withSnapshots, std::wstring& err) {
    InstanceCfg* c = Inst(id);
    if (!c) { err = L"not found"; return false; }
    Runtime* r = Rt(id);
    if (r && (r->st == ST2_BACKUP)) { err = L"busy"; return false; }
    std::wstring bdir = g_p.backups + L"\\" + id + L"-" + NowFileStamp();
    MK(bdir);
    JValue m = ManifestFor(*c, withSnapshots);
    JValue sums; sums.type = JValue::Obj;

    auto reg = [&](const std::wstring& rel) {
        std::wstring f = bdir + L"\\" + rel;
        if (FE(f)) sums.obj[rel] = JValue::MakeStr(Sha256OfFile(f));
    };

    CopyFileW((c->Dir() + L"\\config.json").c_str(), (bdir + L"\\config.json").c_str(), FALSE);
    reg(L"config.json");

    if (withDisk) {
        std::wstring disk = DiskOf(*c);
        if (FE(disk)) {
            std::wstring dn = std::wstring(L"disk.") + (c->diskFormat == L"raw" ? L"raw" : L"qcow2");
            CopyFileW(disk.c_str(), (bdir + L"\\" + dn).c_str(), FALSE);
            reg(dn);
        }
    }
    if (withSnapshots && FE(c->Dir() + L"\\snapshots.json"))
        CopyFileW((c->Dir() + L"\\snapshots.json").c_str(), (bdir + L"\\snapshots.json").c_str(), FALSE),
        reg(L"snapshots.json");
    // keymap profile of this instance
    if (!c->keymapProfileId.empty() && FE(g_p.dataRoot + L"\\keymaps.json"))
        CopyFileW((g_p.dataRoot + L"\\keymaps.json").c_str(), (bdir + L"\\keymaps.json").c_str(), FALSE),
        reg(L"keymaps.json");

    m.obj[L"sha256"] = sums;
    WriteText(bdir + L"\\manifest.json", JsonWrite(m));
    LogW(L"inst", L"backup v2 %s -> %s (disk=%d snaps=%d)", id.c_str(), bdir.c_str(),
         withDisk ? 1 : 0, withSnapshots ? 1 : 0);
    ILog(id, L"launcher", L"backup created: " + BaseName(bdir));
    return true;
}

// pack a backup dir into single .novadroid-backup using Windows tar.exe (bsdtar)
static std::wstring TarExe() {
    wchar_t p[MAX_PATH];
    if (GetSystemDirectoryW(p, MAX_PATH)) {
        std::wstring t = std::wstring(p) + L"\\tar.exe";
        if (FE(t)) return t;
    }
    return L"";
}

bool ExportBackupFile(const std::wstring& idOrDir, const std::wstring& dstPath, std::wstring& err) {
    // accept either instance id or a full backup dir path
    std::wstring best;
    if (FE(idOrDir + L"\\manifest.json")) best = idOrDir;
    if (best.empty())
        for (auto& b : ListBackups())
            if (b.id == idOrDir) { best = b.dir; break; }   // list sorted newest first
    if (best.empty()) { BackupInstanceV2(idOrDir, true, false, err); if (!err.empty()) return false;
        for (auto& b : ListBackups()) if (b.id == idOrDir) { best = b.dir; break; } }
    if (best.empty()) { err = L"no backup"; return false; }
    std::wstring tar = TarExe();
    if (tar.empty()) { err = g_lang ? L"tar.exe not found; export as folder instead." :
                                       L"tar.exe не найден; экспортируйте папкой."; return false; }
    std::wstring parent = best.substr(0, best.find_last_of(L"\\"));
    std::wstring name = BaseName(best);
    if (FE(dstPath)) DeleteFileW(dstPath.c_str());
    DWORD ec = 0; std::string so, se;
    RunCapture(tar, L"-cf " + Qn(dstPath) + L" -C " + Qn(parent) + L" " + Qn(name),
               parent, 1800000, &ec, &so, &se);
    if (!FE(dstPath)) { err = g_lang ? L"Pack failed" : L"Ошибка упаковки"; return false; }
    LogW(L"inst", L"exported %s -> %s", idOrDir.c_str(), dstPath.c_str());
    return true;
}

bool ImportBackupFile(const std::wstring& path, std::wstring& newId, std::wstring& err) {
    std::wstring ext = FileExt(path);
    std::wstring workDir;
    if (ext == L".novadroid-backup" || ext == L".tar") {
        std::wstring tar = TarExe();
        if (tar.empty()) { err = g_lang ? L"tar.exe not found" : L"tar.exe не найден"; return false; }
        workDir = g_p.cache + L"\\import-" + NowFileStamp();
        MK(workDir);
        DWORD ec = 0; std::string so, se;
        RunCapture(tar, L"-xf " + Qn(path) + L" -C " + Qn(workDir), workDir, 1800000, &ec, &so, &se);
        // either extracted dir or flat files
        if (FE(workDir + L"\\manifest.json")) { /* flat */ }
        else {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((workDir + L"\\*").c_str(), &fd);
            bool found = false;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                        wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
                        std::wstring cand = workDir + L"\\" + fd.cFileName;
                        if (FE(cand + L"\\manifest.json")) { workDir = cand; found = true; break; }
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (!found) { err = g_lang ? L"Invalid backup archive" : L"Некорректный архив копии"; DeleteTree(workDir); return false; }
        }
    } else if (DE(path)) {
        workDir = path;   // folder import
    } else {
        err = L"unsupported file"; return false;
    }

    std::wstring txt;
    if (!ReadText(workDir + L"\\manifest.json", txt)) {
        err = g_lang ? L"manifest.json missing" : L"manifest.json отсутствует";
        if (FileExt(path) != L".json" && !DE(path)) DeleteTree(workDir);
        return false;
    }
    JValue m; std::wstring je;
    if (!JsonParse(txt, m, je)) { err = je; return false; }
    int fmtVer = m.find(L"formatVersion") ? m.find(L"formatVersion")->asInt(0) : 0;
    if (fmtVer > 1) {
        err = g_lang ? L"Backup format is newer than this NovaDroid version." :
                       L"Формат копии новее этой версии NovaDroid.";
        return false;
    }
    // verify checksums (TZ 15.2)
    const JValue* sums = m.find(L"sha256");
    if (sums && sums->type == JValue::Obj) {
        for (auto& kv : sums->obj) {
            std::wstring f = workDir + L"\\" + kv.first;
            if (!FE(f)) { err = L"missing file: " + kv.first; return false; }
            std::wstring real = Sha256OfFile(f);
            std::wstring want = kv.second.asStr();
            if (_wcsicmp(real.c_str(), want.c_str()) != 0) {
                err = g_lang ? L"Checksum mismatch: " : L"Контрольная сумма не совпала: ";
                err += kv.first;
                return false;
            }
        }
    }
    // free space check
    long long need = 0;
    WIN32_FIND_DATAW fd2;
    HANDLE h2 = FindFirstFileW((workDir + L"\\*").c_str(), &fd2);
    if (h2 != INVALID_HANDLE_VALUE) {
        do { if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                 need += ((long long)fd2.nFileSizeHigh << 32) | fd2.nFileSizeLow; }
        while (FindNextFileW(h2, &fd2));
        FindClose(h2);
    }
    ULARGE_INTEGER freeB;
    GetDiskFreeSpaceExW(g_p.dataRoot.c_str(), &freeB, nullptr, nullptr);
    if ((long long)freeB.QuadPart < need + 1024LL * 1024 * 1024) {
        err = g_lang ? L"Not enough disk space for import." : L"Недостаточно места на диске для импорта.";
        return false;
    }
    // import as new instance
    std::wstring cfgTxt;
    if (!ReadText(workDir + L"\\config.json", cfgTxt)) { err = L"config.json missing"; return false; }
    JValue cv; std::wstring ce;
    if (!JsonParse(cfgTxt, cv, ce)) { err = ce; return false; }
    InstanceCfg c = InstanceCfg::FromJ(cv);
    if (c.name.empty()) { err = L"invalid config"; return false; }
    std::lock_guard<std::mutex> lk(g_mx);
    c.id = NextInstanceId();
    c.createdAt = NowIso();
    c.lastLaunchAt = L"";
    c.adbPort = NextFreePort();
    EnsureDefaultSharedFolder(c);
    MK(c.Dir()); MK(c.Dir() + L"\\logs"); MK(c.Dir() + L"\\snapshots"); MK(c.Dir() + L"\\shared");
    for (const wchar_t* f : { L"disk.qcow2", L"disk.raw", L"snapshots.json", L"keymaps.json" }) {
        if (FE(workDir + L"\\" + f))
            CopyFileW((workDir + L"\\" + f).c_str(), (c.Dir() + L"\\" + f).c_str(), FALSE);
    }
    PersistInstance(c);
    g_insts.push_back(c);
    g_rt[c.id] = Runtime();
    g_selId = c.id;
    newId = c.id;
    LogW(L"inst", L"imported backup -> %s", c.id.c_str());
    if (!workDir.empty() && workDir.find(g_p.cache) == 0) DeleteTree(workDir);
    return true;
}

bool DeleteBackupDir(const std::wstring& dir, std::wstring& err) {
    if (dir.find(g_p.backups) != 0) { err = L"invalid path"; return false; }
    if (!DeleteTree(dir)) { err = L"delete failed"; return false; }
    LogW(L"inst", L"backup dir deleted: %s", dir.c_str());
    return true;
}

// ================================================================ resource guard (TZ 5.3)
ResourceVerdict CanStartInstance(const InstanceCfg& c) {
    ResourceVerdict v; v.allowed = true; v.runningCount = 0;
    int runningRam = 0;
    for (auto& kv : g_rt) {
        if (kv.first == c.id) continue;
        if (kv.second.hProc && ProcAlive(kv.second.hProc)) {
            v.runningCount++;
            InstanceCfg* o = Inst(kv.first);
            if (o) runningRam += o->ramMb;
        }
    }
    // limit
    int limit = g_set.maxRunning > 0 ? g_set.maxRunning : 2;
    if (v.runningCount >= limit) {
        v.allowed = false;
        v.msg = g_lang ? Fmt(L"Concurrent instance limit reached (%d). Change it in Settings at your own risk.",
                             limit)
                       : Fmt(L"Достигнут лимит одновременных инстансов (%d). Измените его в Настройках на свой риск.",
                             limit);
        return v;
    }
    // RAM check: 80% rule
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    long long availMb = (long long)(ms.ullAvailPhys / (1024 * 1024));
    long long totalMb = (long long)(ms.ullTotalPhys / (1024 * 1024));
    long long committed = runningRam + c.ramMb;
    if (committed * 100 > totalMb * 80) {
        v.allowed = false;
        v.msg = g_lang ?
            Fmt(L"To start \"%s\" %d MB RAM is required. Available now: %lld MB. Stop another instance or reduce allocated memory in settings.",
                c.name.c_str(), c.ramMb, availMb) :
            Fmt(L"Для запуска инстанса «%s» требуется %d MB RAM. Сейчас доступно %lld MB. Остановите другой инстанс или уменьшите выделенную память в настройках.",
                c.name.c_str(), c.ramMb, availMb);
        return v;
    }
    if (availMb < c.ramMb) {
        v.allowed = false;
        v.msg = g_lang ?
            Fmt(L"Available RAM (%lld MB) is less than required (%d MB). Close other programs and try again.",
                availMb, c.ramMb) :
            Fmt(L"Доступной памяти (%lld MB) меньше, чем требуется (%d MB). Закройте другие программы и повторите.",
                availMb, c.ramMb);
        return v;
    }
    // disk space check
    ULARGE_INTEGER freeB;
    GetDiskFreeSpaceExW(c.Dir().c_str(), &freeB, nullptr, nullptr);
    long long freeMb = (long long)(freeB.QuadPart / (1024 * 1024));
    if (freeMb < 2048) {
        v.allowed = false;
        v.msg = g_lang ? L"Less than 2 GB free on the instance disk. Free up space first."
                       : L"Менее 2 ГБ свободно на диске инстанса. Освободите место.";
        return v;
    }
    return v;
}
