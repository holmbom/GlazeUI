#include "d3d11_upload.h"

#include <d3d11_4.h>

#include <cstdio>

namespace glaze::openxr::detail {

namespace {

template <class T>
void release(T*& p) noexcept {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::string hex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string luidText(LUID luid) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08lX-%08lX",
                  static_cast<unsigned long>(luid.HighPart),
                  static_cast<unsigned long>(luid.LowPart));
    return buf;
}

// DXGI-adaptern med exakt LUID:en, eller nullptr. Ingen reserv: ett annat
// kort än runtimens ger bilder som headsetet inte kan läsa.
IDXGIAdapter1* findAdapter(LUID luid) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return nullptr;
    }
    IDXGIAdapter1* found = nullptr;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (adapter && SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.AdapterLuid.LowPart == luid.LowPart &&
            desc.AdapterLuid.HighPart == luid.HighPart) {
            found = adapter;
            break;
        }
        release(adapter);
    }
    release(factory);
    return found;
}

} // namespace

std::unique_ptr<D3D11Upload> D3D11Upload::create(LUID adapterLuid,
                                                 D3D_FEATURE_LEVEL minimumLevel,
                                                 std::string& error) {
    IDXGIAdapter1* adapter = findAdapter(adapterLuid);
    if (!adapter) {
        error = "Grafikkortet som OpenXR-runtimen kräver (LUID " + luidText(adapterLuid) +
                ") hittades inte bland datorns DXGI-adaptrar.";
        return nullptr;
    }

    // Funktionsnivåerna från högsta ned till kravet.
    const D3D_FEATURE_LEVEL all[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                     D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                     D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL levels[6];
    UINT levelCount = 0;
    for (const D3D_FEATURE_LEVEL level : all) {
        if (level >= minimumLevel) levels[levelCount++] = level;
    }
    if (levelCount == 0) {
        release(adapter);
        error = "OpenXR-runtimen kräver en D3D11-funktionsnivå som enheten inte kan skapas med.";
        return nullptr;
    }

    std::unique_ptr<D3D11Upload> self(new D3D11Upload());
    D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_10_0;
    const HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                         levels, levelCount, D3D11_SDK_VERSION,
                                         &self->device_, &achieved, &self->context_);
    release(adapter);
    if (FAILED(hr) || !self->device_ || !self->context_) {
        error = "D3D11-enheten kunde inte skapas på runtimens grafikkort (HRESULT " + hex(hr) + ").";
        return nullptr;
    }
    if (achieved < minimumLevel) {
        error = "Grafikkortet når inte den D3D11-funktionsnivå OpenXR-runtimen kräver.";
        return nullptr;
    }

    // Runtimen kan röra enheten från egna trådar; skydda den.
    ID3D11Multithread* multithread = nullptr;
    if (SUCCEEDED(self->device_->QueryInterface(__uuidof(ID3D11Multithread),
                                                reinterpret_cast<void**>(&multithread)))) {
        multithread->SetMultithreadProtected(TRUE);
        release(multithread);
    }
    return self;
}

D3D11Upload::~D3D11Upload() {
    if (context_) context_->ClearState();
    release(context_);
    release(device_);
}

void D3D11Upload::waitIdle(unsigned timeoutMs) noexcept {
    if (!device_ || !context_) return;
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    ID3D11Query* query = nullptr;
    if (FAILED(device_->CreateQuery(&desc, &query))) return;
    context_->End(query);
    context_->Flush();
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        BOOL done = FALSE;
        const HRESULT hr = context_->GetData(query, &done, sizeof(done), 0);
        if (FAILED(hr) || (hr == S_OK && done) || GetTickCount64() >= deadline) break;
        Sleep(1);
    }
    release(query);
}

bool D3D11Upload::upload(ID3D11Texture2D* texture, const std::uint8_t* rgba,
                         int width, int height, bool swapRedBlue, std::string& error) {
    if (!texture || !rgba || width <= 0 || height <= 0) {
        error = "Uppladdningen fick en tom bild eller textur.";
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Width != static_cast<UINT>(width) || desc.Height != static_cast<UINT>(height)) {
        error = "Bildens mått stämmer inte med swapchain-bildens.";
        return false;
    }

    const std::uint8_t* source = rgba;
    if (swapRedBlue) {
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        scratch_.resize(count * 4);
        for (std::size_t i = 0; i < count; ++i) {
            scratch_[i * 4 + 0] = rgba[i * 4 + 2];
            scratch_[i * 4 + 1] = rgba[i * 4 + 1];
            scratch_[i * 4 + 2] = rgba[i * 4 + 0];
            scratch_[i * 4 + 3] = rgba[i * 4 + 3];
        }
        source = scratch_.data();
    }
    context_->UpdateSubresource(texture, 0, nullptr, source,
                                static_cast<UINT>(width) * 4u, 0);
    // Se till att kommandona nått grafikkortet innan runtimen läser bilden.
    context_->Flush();
    return true;
}

} // namespace glaze::openxr::detail
