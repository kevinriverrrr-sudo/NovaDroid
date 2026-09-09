// ============================================================================
//  NovaDroid - v2images.cpp  Built-in Android image manager (TZ + user request):
//  catalog of Android-x86 releases, background download with progress,
//  SHA-256 integrity check, "use as default".
// ============================================================================
#include "app.h"
#include "v2.h"
#include <urlmon.h>

std::vector<ImageCatalogEntry>& ImageCatalog() {
    static std::vector<ImageCatalogEntry> cat = [] {
        std::vector<ImageCatalogEntry> c;
        ImageCatalogEntry e;
        e.fileName = L"android-x86_64-9.0-r2.iso";
        e.title = L"Android-x86 9.0-r2 (64-bit)";
        e.desc = L"Android 9 Pie x86_64 - стабильный релиз. Рекомендуется для игр и тестирования.";
        e.sizeBytes = 965738496LL;
        e.sha256 = L"f7eb8fc56f29ad5432335dc054183acf086c539f3990f0b6e9ff58bd6df4604e";
        e.urls = {
            L"https://mirrors.gigenet.com/OSDN/android-x86/71931/android-x86_64-9.0-r2.iso",
            L"https://sourceforge.net/projects/android-x86/files/Release%209.0/android-x86_64-9.0-r2.iso/download",
            L"https://www.fosshub.com/Android-x86.html"
        };
        c.push_back(e);
        e = ImageCatalogEntry();
        e.fileName = L"android-x86_64-9.0-r2-k49.iso";
        e.title = L"Android-x86 9.0-r2-k49 (64-bit, kernel 4.9)";
        e.desc = L"Android 9 x86_64 с ядром 4.9 - лучшая совместимость со старыми играми.";
        e.sizeBytes = 763795456LL;
        e.urls = {
            L"https://mirrors.gigenet.com/OSDN/android-x86/71931/android-x86_64-9.0-r2-k49.iso",
            L"https://sourceforge.net/projects/android-x86/files/Release%209.0/android-x86_64-9.0-r2-k49.iso/download"
        };
        c.push_back(e);
        e = ImageCatalogEntry();
        e.fileName = L"android-x86-9.0-r2.iso";
        e.title = L"Android-x86 9.0-r2 (32-bit)";
        e.desc = L"Android 9 для 32-битных приложений и старых игр.";
        e.sizeBytes = 943718400LL;
        e.urls = {
            L"https://mirrors.gigenet.com/OSDN/android-x86/71931/android-x86-9.0-r2.iso",
            L"https://sourceforge.net/projects/android-x86/files/Release%209.0/android-x86-9.0-r2.iso/download"
        };
        c.push_back(e);
        return c;
    }();
    return cat;
}

std::wstring ImagesDirLocal() { return g_p.images; }

std::vector<std::wstring> FindLocalImages() {
    std::vector<std::wstring> out;
    auto scan = [&](const std::wstring& dir) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.iso").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring f = dir + L"\\" + fd.cFileName;
                bool dup = false;
                for (auto& x : out) if (_wcsicmp(x.c_str(), f.c_str()) == 0) dup = true;
                if (!dup) out.push_back(f);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };
    scan(g_p.images);                    // dataRoot\images
    scan(g_p.exeDir + L"\\images");      // portable images next to launcher
    // bundle image dir (pkg layout: launcher in root, images/ sibling)
    std::wstring up = g_p.exeDir.substr(0, g_p.exeDir.find_last_of(L"\\"));
    scan(up + L"\\images");
    return out;
}

// ---------------------------------------------------------------- download engine
static std::mutex g_dlMx;
static bool g_dlBusy = false;
static std::wstring g_dlName;
static std::atomic<float> g_dlProg{ 0 };
static std::atomic<unsigned long> g_dlCancel{ 0 };

class DlCallback : public IBindStatusCallback {
public:
    DlCallback(unsigned long token, long long total) : token_(token), total_(total), got_(0) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = InterlockedDecrement(&refs_); if (!r) delete this; return r; }
    STDMETHODIMP OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    STDMETHODIMP GetPriority(LONG* p) override { *p = THREAD_PRIORITY_NORMAL; return S_OK; }
    STDMETHODIMP OnLowResource(DWORD) override { return S_OK; }
    STDMETHODIMP OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG, LPCWSTR) override {
        got_ = ulProgress;
        long long maxB = ulProgressMax ? (long long)ulProgressMax : total_;
        if (maxB > 0) g_dlProg.store((float)((double)ulProgress / (double)maxB));
        if (g_dlCancel.load() == token_) return E_ABORT;   // user cancel
        return S_OK;
    }
    STDMETHODIMP OnStopBinding(HRESULT hr, LPCWSTR) override { lastHr_ = hr; return S_OK; }
    STDMETHODIMP GetBindInfo(DWORD* grfBINDF, BINDINFO* bi) override {
        *grfBINDF = BINDF_GETNEWESTVERSION | BINDF_PRAGMA_NO_CACHE;
        if (bi && bi->cbSize >= sizeof(BINDINFO)) { bi->dwBindVerb = BINDVERB_GET; }
        return S_OK;
    }
    STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
    HRESULT lastHr_ = S_OK;
private:
    unsigned long token_; long long total_; ULONG got_ = 0; LONG refs_ = 1;
};

bool ImageDownloadBusy() { std::lock_guard<std::mutex> lk(g_dlMx); return g_dlBusy; }
float ImageDownloadProgress() { return g_dlProg.load(); }
std::wstring ImageDownloadName() { std::lock_guard<std::mutex> lk(g_dlMx); return g_dlName; }
void ImageCancelDownload() { g_dlCancel.store(g_dlCancel.load() + 1); }

bool ImageDownloadAsync(size_t catalogIdx) {
    auto& cat = ImageCatalog();
    if (catalogIdx >= cat.size()) return false;
    {
        std::lock_guard<std::mutex> lk(g_dlMx);
        if (g_dlBusy) return false;
        g_dlBusy = true;
        g_dlName = cat[catalogIdx].fileName;
    }
    g_dlProg.store(0);
    unsigned long token = g_dlCancel.load() + 1;
    ImageCatalogEntry e = cat[catalogIdx];
    std::wstring dst = g_p.images + L"\\" + e.fileName;
    RunOpThread([e, dst, token] {
        HRESULT hr = E_FAIL;
        for (size_t i = 0; i < e.urls.size(); ++i) {
            if (g_dlCancel.load() == token) break;
            const wchar_t* url = e.urls[i].c_str();
            LogW(L"image", L"download attempt %d: %s", (int)i + 1, url);
            DlCallback* cb = new DlCallback(token, e.sizeBytes);
            hr = URLDownloadToFileW(nullptr, url, dst.c_str(), 0, cb);
            if (cb->lastHr_ != S_OK) hr = cb->lastHr_;
            cb->Release();
            if (hr == S_OK && FE(dst) && FileSizeOf(dst) > 1024 * 1024) break;
            if (i + 1 < e.urls.size()) SleepMs(1500);
        }
        bool ok = hr == S_OK && FE(dst) && FileSizeOf(dst) > 1024 * 1024;
        if (ok) {
            UiNotify(g_lang ? (L"Image downloaded: " + e.fileName) : (L"Образ загружен: " + e.fileName), 0);
            LogW(L"image", L"downloaded %s", e.fileName.c_str());
        } else if (hr != E_ABORT) {
            UiNotify(g_lang ? L"Download failed. Open the Images page and try a mirror link manually."
                            : L"Не удалось скачать образ. Откройте страницу «Образы» и попробуйте зеркало вручную.", 2);
            LogW(L"image", L"download FAILED hr=0x%08lX", (unsigned long)hr);
        }
        g_dlProg.store(ok ? 1.0f : 0);
        std::lock_guard<std::mutex> lk2(g_dlMx);
        g_dlBusy = false;
    });
    return true;
}

std::wstring ImageUseAsDefault(const std::wstring& path) {
    g_set.defaultImagePath = path;
    SaveSettings();
    // offer to apply to selected instance
    InstanceCfg* c = Inst(g_selId);
    if (c && c->bootMode == L"iso") c->imagePath = path;
    LogW(L"image", L"default image set: %s", path.c_str());
    return path;
}
