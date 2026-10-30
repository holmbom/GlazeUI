#pragma once

// Sessionens egen D3D11-enhet och uppladdningen av CPU-pixlar till
// swapchain-bilderna. Enheten skapas på exakt det grafikkort runtimen kräver
// (LUID) och används bara av XR-tråden.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace glaze::openxr::detail {

class D3D11Upload {
public:
    // Enheten på DXGI-adaptern med exakt `adapterLuid`, med funktionsnivå
    // minst `minimumLevel`. Fel (inget kort med LUID:en, enheten kunde inte
    // skapas) lämnas som svensk text i `error`.
    static std::unique_ptr<D3D11Upload> create(LUID adapterLuid,
                                               D3D_FEATURE_LEVEL minimumLevel,
                                               std::string& error);
    ~D3D11Upload();

    D3D11Upload(const D3D11Upload&)            = delete;
    D3D11Upload& operator=(const D3D11Upload&) = delete;

    ID3D11Device* device() const noexcept { return device_; }

    // Skriver `width` × `height` RGBA8-pixlar (täta rader, rad 0 överst) till
    // undre mip, första lagret av `texture`. Med `swapRedBlue` byts R och B
    // (för en BGRA-textur). Texturens mått måste stämma med bilden.
    bool upload(ID3D11Texture2D* texture, const std::uint8_t* rgba,
                int width, int height, bool swapRedBlue, std::string& error);

    // Väntar tills grafikkortet kört klart allt som skickats på enheten —
    // före rivningen av swapchains som det skrivit till. Ger upp efter
    // `timeoutMs` eller när enheten förlorats.
    void waitIdle(unsigned timeoutMs) noexcept;

private:
    D3D11Upload() = default;

    ID3D11Device*        device_  = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    std::vector<std::uint8_t> scratch_;   // R/B-bytta pixlar
};

} // namespace glaze::openxr::detail
