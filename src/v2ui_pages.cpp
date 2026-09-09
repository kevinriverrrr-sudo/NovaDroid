// ============================================================================
//  NovaDroid - v2ui_pages.cpp  Stage 2 pages: Images, Backups, Snapshots,
//  Keymaps, Performance Center (TZ 8, 9, 10, 11, 12 + built-in images).
// ============================================================================
#include "ui.h"
#include "v2.h"
#include "v3.h"
extern std::wstring g_keymapSel;
extern std::wstring g_perfSel;


// ================================================================ Images page
void PageImages(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_IMG_TITLE), T(S_IMG_SUB), rc, &y);

    int scroll = g_scroll.count((int)Pg::Images) ? g_scroll[(int)Pg::Images] : 0;
    RECT clip = { 0, y, rc.right, rc.bottom };
    SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    int Y = y - scroll;

    // ---- local images
    RECT h1 = { x0, Y, rc.right - m, Y + MulDiv(26, g_dpi, 96) };
    Txt(dc, h1, T(S_IMG_LOCAL), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    Y += MulDiv(32, g_dpi, 96);
    auto locals = FindLocalImages();
    int rowH = MulDiv(64, g_dpi, 96);
    if (locals.empty()) {
        RECT er = { x0, Y, rc.right - m, Y + rowH };
        Txt(dc, er, g_lang ? L"No local images. Download one from the catalog below."
                           : L"Локальных образов нет. Скачайте образ из каталога ниже.",
            Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Y += rowH + m / 2;
    }
    for (size_t i = 0; i < locals.size() && i < 12; ++i) {
        std::wstring f = locals[i];
        RECT card = { x0, Y, rc.right - m, Y + rowH };
        bool isDef = _wcsicmp(g_set.defaultImagePath.c_str(), f.c_str()) == 0;
        FillRound(dc, card, CL_SURF2, 12);
        FrameRound(dc, card, isDef ? Accent() : CL_BORDER, 12);
        int im = MulDiv(12, g_dpi, 96);
        RECT nr = { card.left + im, Y + im, card.right - MulDiv(320, g_dpi, 96), Y + im + MulDiv(22, g_dpi, 96) };
        Txt(dc, nr, BaseName(f), Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT sr = { card.left + im, Y + im + MulDiv(22, g_dpi, 96), card.right - MulDiv(320, g_dpi, 96), Y + rowH - im };
        Txt(dc, sr, HumanSize(FileSizeOf(f)) + (isDef ? (g_lang ? L"  ·  default" : L"  ·  по умолчанию") : L""),
            Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int bw = MulDiv(96, g_dpi, 96), bh = MulDiv(30, g_dpi, 96);
        int bx = card.right - im - bw, by = Y + (rowH - bh) / 2;
        RECT b1 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_LOCIMG0 + i * 3 + 0), b1, T(S_IMG_USE), isDef ? BSTY_GHOST : BSTY_PRIMARY);
        bx -= (bw + m / 3);
        RECT b2 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_LOCIMG0 + i * 3 + 2), b2, T(S_IMG_SHA), BSTY_SURF);
        bx -= (bw + m / 3);
        RECT b3 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_LOCIMG0 + i * 3 + 1), b3, T(S_IMG_DELETE), BSTY_DANGER);
        Y += rowH + m / 2;
    }
    Y += m;

    // ---- download progress
    if (ImageDownloadBusy()) {
        RECT pc = { x0, Y, rc.right - m, Y + MulDiv(52, g_dpi, 96) };
        FillRound(dc, pc, CL_SURF, 12);
        FrameRound(dc, pc, CL_BORDER, 12);
        int im = MulDiv(12, g_dpi, 96);
        RECT pr = { pc.left + im, pc.top + im, pc.right - MulDiv(140, g_dpi, 96), pc.top + im + MulDiv(18, g_dpi, 96) };
        Txt(dc, pr, (g_lang ? L"Downloading " : L"Загрузка ") + ImageDownloadName() +
            Fmt(L"  %d%%", (int)(ImageDownloadProgress() * 100)), Fnt(F_SMALL), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT bar = { pc.left + im, pc.bottom - im - MulDiv(10, g_dpi, 96), pc.right - im - MulDiv(120, g_dpi, 96), pc.bottom - im };
        FillRound(dc, bar, CL_SURF, 5);
        int w = (int)((bar.right - bar.left) * ImageDownloadProgress());
        if (w > 2) { RECT fill = { bar.left, bar.top, bar.left + w, bar.bottom }; FillRound(dc, fill, Accent(), 5); }
        RECT cb = { pc.right - im - MulDiv(110, g_dpi, 96), pc.top + (pc.bottom - pc.top) / 2 - MulDiv(15, g_dpi, 96),
                    pc.right - im, pc.top + (pc.bottom - pc.top) / 2 + MulDiv(15, g_dpi, 96) };
        Btn(dc, 3899, cb, T(S_IMG_CANCEL), BSTY_DANGER);
        Y += MulDiv(52, g_dpi, 96) + m / 2;
    }

    // ---- catalog
    RECT h2 = { x0, Y, rc.right - m, Y + MulDiv(26, g_dpi, 96) };
    Txt(dc, h2, T(S_IMG_CATALOG), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    Y += MulDiv(32, g_dpi, 96);
    auto& cat = ImageCatalog();
    int cH = MulDiv(84, g_dpi, 96);
    for (size_t i = 0; i < cat.size(); ++i) {
        ImageCatalogEntry& e = cat[i];
        std::wstring localPath = g_p.images + L"\\" + e.fileName;
        bool have = FE(localPath);
        RECT card = { x0, Y, rc.right - m, Y + cH };
        FillRound(dc, card, CL_SURF, 14);
        FrameRound(dc, card, have ? Accent() : CL_BORDER, 14);
        int im = MulDiv(14, g_dpi, 96);
        RECT tr = { card.left + im, Y + im, card.right - im - MulDiv(330, g_dpi, 96), Y + im + MulDiv(24, g_dpi, 96) };
        Txt(dc, tr, e.title, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT dr = { card.left + im, Y + im + MulDiv(24, g_dpi, 96), card.right - im - MulDiv(330, g_dpi, 96), Y + cH - im };
        Txt(dc, dr, e.desc + L"  ·  " + HumanSize((uint64_t)e.sizeBytes), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
        // status pill
        RECT pill = { card.left + im, Y + cH - im - MulDiv(20, g_dpi, 96), card.left + im + MulDiv(120, g_dpi, 96),
                      Y + cH - im };
        Pill(dc, pill, have ? T(S_IMG_DOWNLOADED) : T(S_IMG_NOTDL), have ? CL_OK : CL_SUB);
        // buttons (right column)
        int bw = MulDiv(100, g_dpi, 96), bh = MulDiv(30, g_dpi, 96);
        int bx = card.right - im - bw, by = Y + (cH - bh) / 2;
        RECT b1 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_IMG0 + i * 5 + 0), b1, have ? T(S_IMG_USE) : (ImageDownloadBusy() ? L"…" : T(S_IMG_DOWNLOAD)),
            have ? BSTY_SURF : BSTY_PRIMARY, !ImageDownloadBusy() || have);
        bx -= (bw + m / 3);
        RECT b2 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_IMG0 + i * 5 + 1), b2, T(S_IMG_SHA), BSTY_SURF, have);
        bx -= (bw + m / 3);
        RECT b3 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_IMG0 + i * 5 + 3), b3, T(S_IMG_DELETE), BSTY_DANGER, have);
        bx -= (bw + m / 3);
        RECT b4 = { bx, by, bx + bw, by + bh };
        Btn(dc, (int)(ID_IMG0 + i * 5 + 4), b4, T(S_IMG_OPENMIRRORS), BSTY_GHOST);
        Y += cH + m / 2;
    }
    RECT hint = { x0, Y, rc.right - m, Y + MulDiv(40, g_dpi, 96) };
    Txt(dc, hint, T(S_IMG_HINT), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    RestoreDC(dc, -1);
}

// ================================================================ Backups page
void PageBackups(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_BK_TITLE), T(S_BK_SUB), rc, &y);
    // top buttons
    int bh = MulDiv(38, g_dpi, 96);
    RECT b1 = { rc.right - m - MulDiv(150, g_dpi, 96), y, rc.right - m, y + bh };
    Btn(dc, 3850, b1, T(S_BK_CREATE), BSTY_PRIMARY);
    RECT b2 = { b1.left - m / 2 - MulDiv(160, g_dpi, 96), y, b1.left - m / 2, y + bh };
    Btn(dc, 3851, b2, T(S_BK_IMPORT), BSTY_SURF);
    RECT b3 = { b2.left - m / 2 - MulDiv(140, g_dpi, 96), y, b2.left - m / 2, y + bh };
    Btn(dc, 3852, b3, T(S_BK_OPEN), BSTY_GHOST);
    y += bh + m;

    int scroll = g_scroll.count((int)Pg::Backups) ? g_scroll[(int)Pg::Backups] : 0;
    RECT clip = { 0, y, rc.right, rc.bottom };
    SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    int Y = y - scroll;

    auto list = ListBackups();
    if (list.empty()) {
        RECT er = { x0, Y + MulDiv(20, g_dpi, 96), rc.right - m, Y + MulDiv(60, g_dpi, 96) };
        Txt(dc, er, T(S_BK_EMPTY), Fnt(F_BODY), CL_SUB, DT_CENTER | DT_SINGLELINE);
    }
    int rowH = MulDiv(74, g_dpi, 96);
    for (size_t i = 0; i < list.size() && i < 40; ++i) {
        BackupInfo& b = list[i];
        RECT card = { x0, Y, rc.right - m, Y + rowH };
        FillRound(dc, card, CL_SURF2, 12);
        FrameRound(dc, card, CL_BORDER, 12);
        int im = MulDiv(12, g_dpi, 96);
        RECT nr = { card.left + im, Y + im, card.right - MulDiv(460, g_dpi, 96), Y + im + MulDiv(22, g_dpi, 96) };
        Txt(dc, nr, b.name, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT sr = { card.left + im, Y + im + MulDiv(22, g_dpi, 96), card.right - MulDiv(460, g_dpi, 96), Y + rowH - im };
        std::wstring meta = Fmt(L"%s · %s · %s", b.id.c_str(), b.created.c_str(), HumanSize((uint64_t)b.bytes).c_str());
        Txt(dc, sr, meta, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        RECT pill = { card.left + im, Y + rowH - im - MulDiv(18, g_dpi, 96),
                      card.left + im + MulDiv(150, g_dpi, 96), Y + rowH - im };
        Pill(dc, pill, b.hasManifest ? T(S_BK_MANIFEST) : T(S_BK_NOMAN),
             b.hasManifest ? CL_OK : CL_WARN);
        int bw = MulDiv(100, g_dpi, 96), bh2 = MulDiv(30, g_dpi, 96);
        int bx = card.right - im - bw, by = Y + (rowH - bh2) / 2;
        RECT r1 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_BK0 + i * 4 + 0), r1, T(S_BK_RESTORE2), BSTY_PRIMARY);
        bx -= (bw + m / 3);
        RECT r2 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_BK0 + i * 4 + 1), r2, T(S_BK_EXPORT), BSTY_SURF);
        bx -= (bw + m / 3);
        RECT r3 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_BK0 + i * 4 + 2), r3, T(S_IMG_DELETE), BSTY_DANGER);
        Y += rowH + m / 2;
    }
    RestoreDC(dc, -1);
}

// helper (localized date)
static std::wstring IsoToDisplay2(const std::wstring& iso) {
    int Y2 = 0, M = 0, D = 0, h = 0, mi = 0;
    if (swscanf(iso.c_str(), L"%d-%d-%dT%d:%d", &Y2, &M, &D, &h, &mi) >= 5)
        return Fmt(L"%02d.%02d.%04d %02d:%02d", D, M, Y2, h, mi);
    return iso;
}

// ================================================================ Snapshots page
void PageSnapshots(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    InstanceCfg* c = Inst(g_selId);
    std::wstring title = c ? (std::wstring(T(S_SNAP_TITLE)) + L" · " + c->name) : T(S_SNAP_TITLE);
    PageHeader(dc, title.c_str(), T(S_SNAP_SUB), rc, &y);
    if (!c) return;
    int bh = MulDiv(38, g_dpi, 96);
    RECT b1 = { rc.right - m - MulDiv(160, g_dpi, 96), y, rc.right - m, y + bh };
    Btn(dc, 3860, b1, T(S_SNAP_CREATE), BSTY_PRIMARY);
    y += bh + m;

    int scroll = g_scroll.count((int)Pg::Snapshots) ? g_scroll[(int)Pg::Snapshots] : 0;
    RECT clip = { 0, y, rc.right, rc.bottom };
    SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    int Y = y - scroll;

    auto list = LoadSnapshots(c->id);
    if (list.empty()) {
        RECT er = { x0, Y + MulDiv(20, g_dpi, 96), rc.right - m, Y + MulDiv(60, g_dpi, 96) };
        Txt(dc, er, T(S_SNAP_EMPTY), Fnt(F_BODY), CL_SUB, DT_CENTER | DT_SINGLELINE);
    }
    int rowH = MulDiv(78, g_dpi, 96);
    for (size_t i = 0; i < list.size() && i < 40; ++i) {
        Snapshot& s = list[i];
        RECT card = { x0, Y, rc.right - m, Y + rowH };
        FillRound(dc, card, CL_SURF2, 12);
        FrameRound(dc, card, s.isProtected ? Mix(Accent(), CL_SURF2, 60) : CL_BORDER, 12);
        int im = MulDiv(12, g_dpi, 96);
        RECT nr = { card.left + im, Y + im, card.right - MulDiv(360, g_dpi, 96), Y + im + MulDiv(22, g_dpi, 96) };
        Txt(dc, nr, s.displayName, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT sr = { card.left + im, Y + im + MulDiv(22, g_dpi, 96), card.right - MulDiv(360, g_dpi, 96), Y + rowH - im };
        std::wstring tp = s.type == L"system" ? T(S_SNAP_SYSTEM) : s.type == L"auto" ? T(S_SNAP_AUTO) : T(S_SNAP_USER);
        Txt(dc, sr, IsoToDisplay2(s.createdAt) + L" · " + tp + L" · " + HumanSize((uint64_t)s.sizeBytes) +
            (s.description.empty() ? L"" : L" · " + s.description), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        RECT pill = { card.right - MulDiv(200, g_dpi, 96), Y + im, card.right - im, Y + im + MulDiv(20, g_dpi, 96) };
        Pill(dc, pill, s.isProtected ? T(S_SNAP_PROT) : tp, s.isProtected ? Accent() : CL_SUB);
        int bw = MulDiv(100, g_dpi, 96), bh2 = MulDiv(30, g_dpi, 96);
        int bx = card.right - im - bw, by = Y + (rowH - bh2) / 2;
        RECT r1 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_SNAP0 + i * 3 + 0), r1, T(S_SNAP_RESTORE), BSTY_PRIMARY);
        bx -= (bw + m / 3);
        RECT r2 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_SNAP0 + i * 3 + 1), r2, T(S_SNAP_DELETE), BSTY_DANGER);
        Y += rowH + m / 2;
    }
    RestoreDC(dc, -1);
}

// ================================================================ Keymaps page
void PageKeysV2(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_KM_TITLE), T(S_KM_SUB), rc, &y);
    int bh = MulDiv(38, g_dpi, 96);
    RECT b1 = { rc.right - m - MulDiv(150, g_dpi, 96), y, rc.right - m, y + bh };
    Btn(dc, ID_KM_ADD, b1, T(S_KM_ADDPROFILE), BSTY_PRIMARY);
    RECT b2 = { b1.left - m / 2 - MulDiv(140, g_dpi, 96), y, b1.left - m / 2, y + bh };
    Btn(dc, ID_KM_IMPORT, b2, T(S_KM_IMPORT), BSTY_SURF);
    y += bh + m;

    int scroll = g_scroll.count((int)Pg::Keys) ? g_scroll[(int)Pg::Keys] : 0;
    RECT clip = { 0, y, rc.right, rc.bottom };
    SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    int Y = y - scroll;

    auto& kms = Keymaps();
    int rowH = MulDiv(84, g_dpi, 96);
    for (size_t i = 0; i < kms.size() && i < 24; ++i) {
        KeymapProfile& p = kms[i];
        RECT card = { x0, Y, rc.right - m, Y + rowH };
        FillRound(dc, card, CL_SURF2, 12);
        FrameRound(dc, card, p.enabled ? Accent() : CL_BORDER, 12);
        int im = MulDiv(12, g_dpi, 96);
        RECT nr = { card.left + im, Y + im, card.right - MulDiv(430, g_dpi, 96), Y + im + MulDiv(22, g_dpi, 96) };
        Txt(dc, nr, p.name, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        RECT sr = { card.left + im, Y + im + MulDiv(22, g_dpi, 96), card.right - MulDiv(430, g_dpi, 96), Y + rowH - im };
        Txt(dc, sr, Fmt(L"%dx%d · %s · %d %s · %s", p.resW, p.resH,
                        (p.orientation == L"landscape" ? T(S_WIZ_LAND) : T(S_WIZ_PORT)),
                        (int)p.bindings.size(), T(S_KM_BINDS),
                        (p.packageName.empty() ? L"—" : p.packageName.c_str())),
            Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        RECT pill = { card.right - MulDiv(330, g_dpi, 96), Y + im, card.right - MulDiv(260, g_dpi, 96), Y + im + MulDiv(20, g_dpi, 96) };
        Pill(dc, pill, p.enabled ? T(S_KM_ON) : T(S_KM_OFF), p.enabled ? CL_OK : CL_SUB);
        int bw = MulDiv(100, g_dpi, 96), bh2 = MulDiv(30, g_dpi, 96);
        int bx = card.right - im - bw, by = Y + (rowH - bh2) / 2;
        RECT r1 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_KM0 + i * 4 + 0), r1, T(S_KM_TOGGLE), p.enabled ? BSTY_OK : BSTY_PRIMARY);
        bx -= (bw + m / 3);
        RECT r2 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_KM0 + i * 4 + 1), r2, T(S_KM_ADDBIND), BSTY_SURF);
        bx -= (bw + m / 3);
        RECT r3 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_KM0 + i * 4 + 3), r3, T(S_KM_EXPORT), BSTY_GHOST);
        bx -= (bw + m / 3);
        RECT r4 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(ID_KM0 + i * 4 + 2), r4, T(S_KM_DELETE), BSTY_DANGER);
        Y += rowH + m / 2;
    }
    // bindings of selected profile
    if (!g_keymapSel.empty()) {
        KeymapProfile* p = KeymapById(g_keymapSel);
        if (p && !p->bindings.empty()) {
            Y += m / 2;
            RECT hb = { x0, Y, rc.right - m, Y + MulDiv(24, g_dpi, 96) };
            Txt(dc, hb, std::wstring(T(S_KM_BINDS)) + L" · " + p->name, Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
            Y += MulDiv(30, g_dpi, 96);
            int bRowH = MulDiv(40, g_dpi, 96);
            for (size_t i = 0; i < p->bindings.size() && i < 20; ++i) {
                KeyBinding& b = p->bindings[i];
                RECT row = { x0, Y, rc.right - m, Y + bRowH };
                FillRound(dc, row, CL_SURF, 10);
                const wchar_t* act = b.action == L"tap" ? T(S_KM_TAP) : b.action == L"longtap" ? T(S_KM_LONGTAP) :
                                     b.action == L"swipe" ? T(S_KM_SWIPE) : b.action == L"back" ? T(S_KM_BACKB) :
                                     b.action == L"home" ? T(S_KM_HOMEB) : b.action == L"rec" ? T(S_KM_RECB) :
                                     b.action == L"volup" ? T(S_KM_VOLUP) : b.action == L"voldn" ? T(S_KM_VOLDN) :
                                     T(S_KM_ROT);
                std::wstring line = L"[" + KeymapVkName(b.vk) + L"]  " + act;
                if (b.action == L"tap" || b.action == L"longtap")
                    line += Fmt(L"  →  (%d, %d)", b.x, b.y);
                else if (b.action == L"swipe")
                    line += Fmt(L"  →  (%d,%d) → (%d,%d)", b.x, b.y, b.x2, b.y2);
                RECT tr = { row.left + MulDiv(12, g_dpi, 96), Y, row.right - MulDiv(220, g_dpi, 96), Y + bRowH };
                Txt(dc, tr, line, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                int bw = MulDiv(90, g_dpi, 96), bh2 = MulDiv(26, g_dpi, 96);
                int bx = row.right - MulDiv(12, g_dpi, 96) - bw, by = Y + (bRowH - bh2) / 2;
                RECT r1 = { bx, by, bx + bw, by + bh2 };
                Btn(dc, (int)(ID_KMBIND0 + i * 2 + 0), r1, T(S_BTN_RENAME), BSTY_SURF);
                bx -= (bw + m / 3);
                RECT r2 = { bx, by, bx + bw, by + bh2 };
                Btn(dc, (int)(ID_KMBIND0 + i * 2 + 1), r2, T(S_KM_DELETE), BSTY_DANGER);
                Y += bRowH + m / 3;
            }
        }
    }
    // notice
    RECT nt = { x0, Y + m, rc.right - m, Y + m + MulDiv(44, g_dpi, 96) };
    Txt(dc, nt, T(S_KM_NOTICE), Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
    RestoreDC(dc, -1);
}

// ================================================================ Performance page
static void DrawGraph(HDC dc, RECT r, const std::vector<PerfSample>& hist,
                      std::function<float(const PerfSample&)> get, float maxV,
                      COLORREF lineCol, const std::wstring& label, const std::wstring& value) {
    FillRound(dc, r, CL_SURF2, 12);
    FrameRound(dc, r, CL_BORDER, 12);
    int im = MulDiv(12, g_dpi, 96);
    RECT lr = { r.left + im, r.top + im, r.right - im, r.top + im + MulDiv(20, g_dpi, 96) };
    Txt(dc, lr, label, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
    RECT vr = { r.left + im, r.top + im, r.right - im, r.top + im + MulDiv(22, g_dpi, 96) };
    Txt(dc, vr, value, Fnt(F_BIG), lineCol, DT_RIGHT | DT_SINGLELINE);
    RECT plot = { r.left + im, r.top + im + MulDiv(28, g_dpi, 96), r.right - im, r.bottom - im };
    // grid
    HPEN gp = CreatePen(PS_SOLID, 1, Mix(CL_SURF2, CL_BORDER, 60));
    HPEN og = (HPEN)SelectObject(dc, gp);
    for (int i = 1; i < 4; ++i) {
        int gy = plot.top + (plot.bottom - plot.top) * i / 4;
        MoveToEx(dc, plot.left, gy, nullptr); LineTo(dc, plot.right, gy);
    }
    SelectObject(dc, og); DeleteObject(gp);
    if (hist.size() >= 2) {
        HPEN p = CreatePen(PS_SOLID, 2, lineCol);
        HPEN op = (HPEN)SelectObject(dc, p);
        POINT pts[60];
        int n = 0;
        int W = plot.right - plot.left, H = plot.bottom - plot.top;
        for (size_t i = 0; i < hist.size() && n < 60; ++i) {
            float v = get(hist[i]);
            if (v < 0) v = 0;
            float frac = maxV > 0 ? v / maxV : 0;
            if (frac > 1) frac = 1;
            pts[n].x = plot.left + (int)((float)W * (float)i / (float)(hist.size() - 1));
            pts[n].y = plot.bottom - (int)(H * frac);
            n++;
        }
        Polyline(dc, pts, n);
        SelectObject(dc, op); DeleteObject(p);
    } else {
        RECT er = plot;
        Txt(dc, er, g_lang ? L"collecting data…" : L"сбор данных…", Fnt(F_SMALL), CL_SUB,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void PagePerfV2(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_PERF2_TITLE), T(S_PERF2_SUB), rc, &y);

    // running instance chips
    std::vector<std::wstring> running;
    for (auto& c : g_insts) {
        Runtime* r = Rt(c.id);
        if (r && r->hProc && ProcAlive(r->hProc)) running.push_back(c.id);
    }
    if (g_perfSel.empty() && !running.empty()) g_perfSel = running[0];
    bool selAlive = false;
    for (auto& r0 : running) if (r0 == g_perfSel) selAlive = true;
    if (!selAlive && !running.empty()) g_perfSel = running[0];

    int cy = y;
    for (size_t i = 0; i < g_insts.size(); ++i) {
        Runtime* r = Rt(g_insts[i].id);
        bool alive = r && r->hProc && ProcAlive(r->hProc);
        if (!alive) continue;
        int cw = MulDiv(170, g_dpi, 96), ch = MulDiv(34, g_dpi, 96);
        RECT chip = { x0 + (int)i * (cw + m / 2), cy, x0 + (int)i * (cw + m / 2) + cw, cy + ch };
        bool sel = (g_perfSel == g_insts[i].id);
        FillRound(dc, chip, sel ? Mix(Accent(), CL_SURF, 60) : CL_SURF2, ch / 2);
        FrameRound(dc, chip, sel ? Accent() : CL_BORDER, ch / 2);
        Reg(3500 + (int)i, chip, K_BTN);
        Txt(dc, chip, g_insts[i].name, Fnt(F_SMALL), sel ? CL_TEXT : CL_SUB,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    y += MulDiv(44, g_dpi, 96);

    if (running.empty() || g_perfSel.empty()) {
        RECT er = { x0, y + MulDiv(30, g_dpi, 96), rc.right - m, y + MulDiv(70, g_dpi, 96) };
        Txt(dc, er, T(S_PERF2_NOINST), Fnt(F_BODY), CL_SUB, DT_CENTER | DT_SINGLELINE);
        return;
    }
    InstanceCfg* c = Inst(g_perfSel);
    if (!c) return;
    PagePerfProfilesRow(dc, rc, &y);       // stage 3: gaming profiles (TZ3 11)
    auto hist = PerfHistory(g_perfSel);
    PerfSample last = hist.empty() ? PerfSample() : hist.back();

    // three graphs
    int gw = (rc.right - m - x0 - 2 * m / 2) / 3;
    int gh = MulDiv(180, g_dpi, 96);
    float ramMb = (float)(last.ramBytes / (1024 * 1024));
    float diskGb = (float)(last.diskBytes / (1024 * 1024 * 1024));
    DrawGraph(dc, { x0, y, x0 + gw, y + gh }, hist,
              [](const PerfSample& s) { return s.cpuPercent; }, 100, Accent(),
              T(S_PERF2_CPU), Fmt(L"%.0f%%", last.cpuPercent));
    DrawGraph(dc, { x0 + gw + m / 2, y, x0 + 2 * gw + m / 2, y + gh }, hist,
              [](const PerfSample& s) { return (float)(s.ramBytes / (1024 * 1024)); },
              (float)c->ramMb, CL_ACCENT2, T(S_PERF2_RAM),
              Fmt(L"%d / %d MB", (int)ramMb, c->ramMb));
    DrawGraph(dc, { x0 + 2 * (gw + m / 2), y, x0 + 3 * gw + m, y + gh }, hist,
              [](const PerfSample& s) { return (float)(s.diskBytes / (1024 * 1024 * 1024)); },
              (float)c->diskGb, CL_OK, T(S_PERF2_DISK),
              Fmt(L"%.1f / %d GB", diskGb, c->diskGb));
    y += gh + m;

    // info card
    RECT ic = { x0, y, rc.right - m, y + MulDiv(56, g_dpi, 96) };
    FillRound(dc, ic, CL_SURF, 14);
    FrameRound(dc, ic, CL_BORDER, 14);
    Runtime* r = Rt(g_perfSel);
    long long up = r ? ((long long)GetTickCount64() - r->startedAtMs) / 1000 : 0;
    std::wstring info = Fmt(L"%s: %02d:%02d:%02d   ·   %s: %s   ·   ADB: %s:%d",
                            T(S_PERF2_UPTIME), (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60),
                            T(S_PERF2_GPU), c->gpuMode.c_str(), c->adbHost.c_str(), c->adbPort);
    RECT ir = { ic.left + MulDiv(16, g_dpi, 96), y, ic.right - MulDiv(16, g_dpi, 96), y + MulDiv(56, g_dpi, 96) };
    Txt(dc, ir, info, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    y += MulDiv(56, g_dpi, 96) + m;

    // recommendations card (TZ 12.3)
    RECT rc2 = { x0, y, rc.right - m, y + MulDiv(84, g_dpi, 96) };
    FillRound(dc, rc2, CL_SURF, 14);
    FrameRound(dc, rc2, CL_BORDER, 14);
    std::vector<std::wstring> recs;
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    long long availMb = (long long)(ms.ullAvailPhys / (1024 * 1024));
    if (c->ramMb >= 8192 && availMb < 4096)
        recs.push_back(g_lang ? L"The instance is allocated 8192 MB RAM but the PC has little free memory. Close other programs or reduce RAM to 4096 MB."
                              : L"Инстансу выделено 8192 MB RAM, но на ПК мало свободной памяти. Закройте другие программы или снизьте RAM до 4096 MB.");
    if (!WhpxAvailable())
        recs.push_back(g_lang ? L"WHPX is not active. Performance may be low. Open Diagnostics to check Windows components."
                              : L"WHPX не активен. Производительность может быть низкой. Откройте диагностику для проверки компонентов Windows.");
    long long diskBytes = (long long)last.diskBytes;
    if (c->diskGb > 0 && diskBytes > (long long)c->diskGb * 1024 * 1024 * 1024 * 90 / 100)
        recs.push_back(g_lang ? L"The instance disk is almost full. Free up space or increase the virtual disk."
                              : L"Диск инстанса почти заполнен. Освободите место или увеличьте виртуальный диск.");
    if (last.cpuPercent > 90)
        recs.push_back(g_lang ? L"QEMU CPU load is very high. Reduce CPU cores of other instances or lower graphics settings in the game."
                              : L"Очень высокая загрузка CPU QEMU. Снизьте число ядер других инстансов или настройки графики в игре.");
    if (recs.empty())
        recs.push_back(g_lang ? L"All indicators are normal. No action required."
                              : L"Все показатели в норме. Действий не требуется.");
    RECT hr = { rc2.left + MulDiv(16, g_dpi, 96), y + MulDiv(8, g_dpi, 96), rc2.right - MulDiv(16, g_dpi, 96), y + MulDiv(28, g_dpi, 96) };
    Txt(dc, hr, T(S_PERF2_REC), Fnt(F_H2), CL_TEXT, DT_LEFT | DT_SINGLELINE);
    RECT br = { rc2.left + MulDiv(16, g_dpi, 96), y + MulDiv(30, g_dpi, 96), rc2.right - MulDiv(16, g_dpi, 96), y + MulDiv(80, g_dpi, 96) };
    std::wstring all;
    for (size_t i = 0; i < recs.size(); ++i) { if (i) all += L"\r\n"; all += L"• " + recs[i]; }
    Txt(dc, br, all, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_WORDBREAK);
}

// ================================================================ APK library page v2 (TZ 10)
void PageApkV2(HDC dc, RECT rc) {
    int m = MulDiv(PAD, g_dpi, 96);
    int x0 = SIDEBAR_W + m;
    int y = m;
    PageHeader(dc, T(S_APK_TITLE), T(S_APK_WARN), rc, &y);
    int bh = MulDiv(38, g_dpi, 96);
    RECT b1 = { rc.right - m - MulDiv(160, g_dpi, 96), y, rc.right - m, y + bh };
    Btn(dc, 3871, b1, T(S_APK_ADDLIB), BSTY_PRIMARY);
    RECT b2 = { b1.left - m / 2 - MulDiv(150, g_dpi, 96), y, b1.left - m / 2, y + bh };
    Btn(dc, 3872, b2, T(S_BTN_OPENFOLDER), BSTY_GHOST);
    y += bh + m;

    int scroll = g_scroll.count((int)Pg::Apk) ? g_scroll[(int)Pg::Apk] : 0;
    RECT clip = { 0, y, rc.right, rc.bottom };
    SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    int Y = y - scroll;

    auto& lib = ApkLib();
    if (lib.empty()) {
        RECT er = { x0, Y + MulDiv(20, g_dpi, 96), rc.right - m, Y + MulDiv(60, g_dpi, 96) };
        Txt(dc, er, T(S_APK_EMPTY), Fnt(F_BODY), CL_SUB, DT_CENTER | DT_SINGLELINE);
        RECT dnd = { x0, Y + MulDiv(70, g_dpi, 96), rc.right - m, Y + MulDiv(110, g_dpi, 96) };
        Txt(dc, dnd, T(S_APK_DND), Fnt(F_SMALL), CL_SUB, DT_CENTER | DT_SINGLELINE);
    }
    int rowH = MulDiv(70, g_dpi, 96);
    for (size_t i = 0; i < lib.size() && i < 60; ++i) {
        ApkEntry& a = lib[i];
        RECT card = { x0, Y, rc.right - m, Y + rowH };
        FillRound(dc, card, CL_SURF2, 12);
        FrameRound(dc, card, CL_BORDER, 12);
        int im = MulDiv(12, g_dpi, 96);
        RECT nr = { card.left + im, Y + im, card.right - MulDiv(420, g_dpi, 96), Y + im + MulDiv(22, g_dpi, 96) };
        Txt(dc, nr, a.fileName, Fnt(F_BODY), CL_TEXT, DT_LEFT | DT_SINGLELINE);
        std::wstring meta = HumanSize((uint64_t)a.fileSizeBytes) + L" · " + IsoToDisplay2(a.addedAt);
        if (!a.sha256.empty()) meta += L" · sha256: " + a.sha256.substr(0, 12) + L"…";
        if (!a.packageName.empty()) meta += L" · " + a.packageName;
        RECT sr = { card.left + im, Y + im + MulDiv(22, g_dpi, 96), card.right - MulDiv(420, g_dpi, 96), Y + rowH - im };
        Txt(dc, sr, meta, Fnt(F_SMALL), CL_SUB, DT_LEFT | DT_SINGLELINE);
        int bw = MulDiv(140, g_dpi, 96), bh2 = MulDiv(30, g_dpi, 96);
        int bx = card.right - im - bw, by = Y + (rowH - bh2) / 2;
        RECT r1 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(3880 + i), r1, T(S_APK_INSTALL), BSTY_PRIMARY);
        bx -= (bw + m / 3);
        RECT r2 = { bx, by, bx + bw, by + bh2 };
        Btn(dc, (int)(3880 + 500 + i), r2, T(S_IMG_DELETE), BSTY_DANGER);
        Y += rowH + m / 2;
    }
    RestoreDC(dc, -1);
}
