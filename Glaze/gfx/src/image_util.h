#pragma once
// Backend-neutralt och internt: det de tre backendarnas
// bildvägar DELAR, så att reglerna är EN sanning — inte tre kopior som kan
// glida isär.
//   * PNG-signaturen, som `gfx::decodeImage` prövar FÖRST på alla plattformar
//     (bara PNG — gfx/image.h).
//   * Av-premultipliceringen `Renderer::readTarget` lämnar målens innehåll
//     med (målen bär premultiplicerad färg, readTarget svarar med rak alfa).
// Header-only, bara standardtyper — dras in av backendarnas bildfiler.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gfx {

// PNG-filens åtta första byte (PNG-specifikationen, avsnitt 5.2): 0x89 "PNG" CR LF
// SUB LF. CR LF/LF-paret fångar en fil som förvanskats av en textöverföring.
inline constexpr std::uint8_t kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47,
                                                  0x0D, 0x0A, 0x1A, 0x0A};

// Börjar datat med PNG-signaturen? Kortare än åtta byte är det inte en PNG.
inline bool hasPngSignature(const void* data, std::size_t size)
{
    return data && size >= sizeof kPngSignature &&
           std::memcmp(data, kPngSignature, sizeof kPngSignature) == 0;
}

// `count` pixlar RGBA8 med PREMULTIPLICERAD färg → RAK alfa, på plats:
// färg · 255 / alfa avrundat (och klampat — ett mål som skrivits med
// Blend::Opaque och alfa under 1 kan bära färg större än alfan), alfa 0 ger
// svart. Samma avrundning i alla backends, så readTarget svarar likadant.
inline void unpremultiplyRgba(std::uint8_t* px, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i, px += 4) {
        const unsigned a = px[3];
        if (a == 0) {
            px[0] = px[1] = px[2] = 0;
        } else if (a < 255) {
            for (int c = 0; c < 3; ++c) {
                const unsigned v = (px[c] * 255u + a / 2u) / a;
                px[c] = static_cast<std::uint8_t>(v > 255u ? 255u : v);
            }
        }
    }
}

} // namespace gfx
