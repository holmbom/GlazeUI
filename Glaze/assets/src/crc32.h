#pragma once
#include <cstddef>
#include <cstdint>

// INTERN header i glaze_assets — CRC32 (polynom 0xEDB88320, den i zlib och
// PNG), delad av assets.bin-formatet (blobbens kontrollsumma) och
// PNG-codecen (varje chunk). Tabellfri: assets och ikoner är små,
// hastigheten spelar ingen roll; noll statiskt data.
//
// `crc32Update` fortsätter en löpande summa (startvärdet 0xFFFFFFFF, avsluta
// med XOR 0xFFFFFFFF) — PNG räknar över chunkens typ OCH data, två
// separata områden.

namespace glzassets::detail {

inline std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* p,
                                 std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

inline std::uint32_t crc32(const std::uint8_t* p, std::size_t n)
{
    return crc32Update(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
}

} // namespace glzassets::detail
