// PxbThumbnailHandler.cpp
//
// Windows shell thumbnail handler for .pxb files.
// Implements IThumbnailProvider + IInitializeWithStream. The stream bytes are
// handed to the shared pxb core (read_thumbnail_memory) which gzip-decodes the
// file, extracts the embedded thumbnail/frame PNG, and returns an RGBA image.
// That RGBA is converted to an HBITMAP via GDI. No extra imaging libraries are
// required (stb_image decodes the PNG, miniz decodes gzip).
//
// Build as a 64-bit COM DLL and register with regsvr32 (see install.bat).
//
// MinGW's uuid static lib does not ship IID_IThumbnailProvider, so we define
// the GUIDs locally via INITGUID (and must NOT link -luuid to avoid duplicate
// definitions).
#define INITGUID
#include <initguid.h>
#include <windows.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <objbase.h>
#include <new>

#include <vector>
#include <cstring>

// Shared pxb parsing core.
#include "pxb_reader.h"
// Shared registry contract (CLSID / ProgID / shellex key) — must match the app.
#include "pxb_reg_contract.h"

// ---- CLSID ---------------------------------------------------------------
// {A1B2C3D4-0001-4E5F-8A9B-112233445566} — canonical value lives in
// src/core/pxb_reg_contract.h; do not redefine here.
static const CLSID CLSID_PxbThumbnailProvider = {PXB_CLSID_BYTES};

// ---- Module ref counting --------------------------------------------------
static LONG g_refCount = 0;
static HINSTANCE g_hInst = nullptr;

// ---- Thumbnail provider ---------------------------------------------------
class PxbThumbnailProvider
    : public IThumbnailProvider,
      public IInitializeWithStream {
public:
    PxbThumbnailProvider() : ref_(1), stream_(nullptr) {
        InterlockedIncrement(&g_refCount);
    }
    ~PxbThumbnailProvider() {
        if (stream_) stream_->Release();
        InterlockedDecrement(&g_refCount);
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IThumbnailProvider))
            *ppv = static_cast<IThumbnailProvider*>(this);
        else if (IsEqualIID(riid, IID_IInitializeWithStream))
            *ppv = static_cast<IInitializeWithStream*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pstream, DWORD) {
        if (stream_) { stream_->Release(); stream_ = nullptr; }
        stream_ = pstream;
        if (stream_) stream_->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pType) {
        if (!phbmp || !pType) return E_POINTER;
        *phbmp = nullptr;
        *pType = WTSAT_ARGB;

        auto bytes = ReadStream();
        if (bytes.empty()) return E_FAIL;

        pxb::RgbaImage img = pxb::read_thumbnail_memory(bytes);
        if (img.empty()) return E_FAIL;

        HBITMAP hbmp = RgbaToHBITMAP(img, cx);
        if (!hbmp) return E_FAIL;
        *phbmp = hbmp;
        return S_OK;
    }

private:
    std::vector<uint8_t> ReadStream() {
        std::vector<uint8_t> out;
        if (!stream_) return out;
        STATSTG st;
        if (SUCCEEDED(stream_->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0) {
            out.resize((size_t)st.cbSize.QuadPart);
        } else {
            out.resize(1 << 16);
        }
        ULONG got = 0;
        SIZE_T total = 0;
        HRESULT hr = S_OK;
        while (true) {
            if (total >= out.size()) out.resize(out.size() * 2);
            hr = stream_->Read(out.data() + total, (ULONG)(out.size() - total), &got);
            if (FAILED(hr)) break;
            total += got;
            if (got == 0) break;
        }
        out.resize(total);
        return out;
    }

    // Convert RGBA -> 32bpp HBITMAP, scaling down to fit within cx.
    static HBITMAP RgbaToHBITMAP(const pxb::RgbaImage& img, UINT cx) {
        int w = img.width, h = img.height;
        if (cx && (w > (int)cx || h > (int)cx)) {
            float s = (float)cx / (float)(w > h ? w : h);
            w = (int)(w * s + 0.5f);
            h = (int)(h * s + 0.5f);
            if (w < 1) w = 1;
            if (h < 1) h = 1;
        }
        BITMAPINFOHEADER bi = {0};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = w;
        bi.biHeight = -h;            // top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        BITMAPINFO bmi = {0};
        bmi.bmiHeader = bi;

        void* bits = nullptr;
        HDC hdc = GetDC(nullptr);
        HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);
        if (!hbmp || !bits) return nullptr;

        // Nearest-neighbor scale from source to (w,h), BGRA for GDI.
        const uint8_t* src = img.pixels.data();
        int sw = img.width, sh = img.height;
        uint8_t* dst = (uint8_t*)bits;
        for (int y = 0; y < h; y++) {
            int sy = (h == sh) ? y : (int)((y * (long long)sh) / h);
            for (int x = 0; x < w; x++) {
                int sx = (w == sw) ? x : (int)((x * (long long)sw) / w);
                const uint8_t* p = src + ((size_t)sy * sw + sx) * 4;
                uint8_t* d = dst + ((size_t)y * w + x) * 4;
                d[0] = p[2];  // B
                d[1] = p[1];  // G
                d[2] = p[0];  // R
                d[3] = p[3];  // A
            }
        }
        return hbmp;
    }

    LONG ref_;
    IStream* stream_;
};

// ---- Class factory -------------------------------------------------------
class PxbClassFactory : public IClassFactory {
public:
    PxbClassFactory() : ref_(1) { InterlockedIncrement(&g_refCount); }
    ~PxbClassFactory() { InterlockedDecrement(&g_refCount); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
            *ppv = static_cast<IClassFactory*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        PxbThumbnailProvider* p = new (std::nothrow) PxbThumbnailProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL fLock) {
        if (fLock) InterlockedIncrement(&g_refCount);
        else InterlockedDecrement(&g_refCount);
        return S_OK;
    }
private:
    LONG ref_;
};

// ---- DLL exports ---------------------------------------------------------
extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD, LPVOID) {
    g_hInst = hinst;
    return TRUE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!IsEqualCLSID(rclsid, CLSID_PxbThumbnailProvider)) return CLASS_E_CLASSNOTAVAILABLE;
    PxbClassFactory* cf = new (std::nothrow) PxbClassFactory();
    if (!cf) return E_OUTOFMEMORY;
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return (InterlockedCompareExchange(&g_refCount, 0, 0) == 0) ? S_OK : S_FALSE;
}

static void SetRegKey(HKEY root, const wchar_t* sub, const wchar_t* value, const wchar_t* data) {
    HKEY hk;
    if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hk, value, 0, REG_SZ, (const BYTE*)data, (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
        RegCloseKey(hk);
    }
}

// Helper: best-effort delete of a registry sub-tree. Failure is ignored so a
// missing key (e.g. already removed) does not abort the rest of unregistration.
static void DeleteRegTree(HKEY root, const wchar_t* sub) {
    RegDeleteTreeW(root, sub);
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    wchar_t dll[MAX_PATH];
    GetModuleFileNameW(g_hInst, dll, MAX_PATH);
    wchar_t clsid[64];
    StringFromGUID2(CLSID_PxbThumbnailProvider, clsid, 64);

    std::wstring base = std::wstring(L"Software\\Classes\\CLSID\\") + clsid;
    SetRegKey(HKEY_LOCAL_MACHINE, (base + L"\\InprocServer32").c_str(), nullptr, dll);
    SetRegKey(HKEY_LOCAL_MACHINE, (base + L"\\InprocServer32").c_str(), L"ThreadingModel", L"Apartment");
    SetRegKey(HKEY_LOCAL_MACHINE, (base).c_str(), nullptr, L"PXB Thumbnail Handler");

    // Register the thumbnail shell-extension hook only. We hook BOTH:
    //   * SystemFileAssociations\.pxb\shellex — the ProgID-independent,
    //     canonical path that works even when no ProgID is assigned to .pxb.
    //   * <ProgID>\shellex — reached when .pxb resolves to our ProgID (the app
    //     writes that .pxb -> ProgID mapping under HKCU, no elevation needed).
    // This DLL deliberately does NOT write the .pxb ProgID default nor the
    // "open" verb: double-click association is owned by the app, so we never
    // reintroduce the per-machine elevation requirement and never drift from
    // the two-tier contract documented in regutil.cpp / pxb_reg_contract.h.
    std::wstring pa = std::wstring(L"Software\\Classes\\SystemFileAssociations\\.pxb\\shellex\\") + PXB_SHELLEX_KEY_W;
    SetRegKey(HKEY_LOCAL_MACHINE, pa.c_str(), nullptr, clsid);
    std::wstring paf = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W + L"\\shellex\\" + PXB_SHELLEX_KEY_W;
    SetRegKey(HKEY_LOCAL_MACHINE, paf.c_str(), nullptr, clsid);

    return S_OK;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    wchar_t clsid[64];
    StringFromGUID2(CLSID_PxbThumbnailProvider, clsid, 64);
    std::wstring base = std::wstring(L"Software\\Classes\\CLSID\\") + clsid;

    // 1) Remove our CLSID registration entirely (the thumbnail handler COM object).
    DeleteRegTree(HKEY_LOCAL_MACHINE, base.c_str());

    // 2) Remove the thumbnail shellex hook only -- under both the user-facing
    //    ProgID and SystemFileAssociations. Do NOT delete the ProgID branch,
    //    because the main app's double-click "open" verb lives there; nuking it
    //    would break launching pxb-preview.exe on .pxb files after unregister.
    std::wstring paf = std::wstring(L"Software\\Classes\\") + PXB_PROGID_W + L"\\shellex\\" + PXB_SHELLEX_KEY_W;
    DeleteRegTree(HKEY_LOCAL_MACHINE, paf.c_str());
    std::wstring pa = std::wstring(L"Software\\Classes\\SystemFileAssociations\\.pxb\\shellex\\") + PXB_SHELLEX_KEY_W;
    DeleteRegTree(HKEY_LOCAL_MACHINE, pa.c_str());

    return S_OK;
}
