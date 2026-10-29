#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "glaze_assets/image.h"

// glaze_assets — programfilens ikonfiler: Windows .ico
// och macOS .icns ur färdigskalade ramar. REN STD och deterministisk — samma
// ramar ger samma byte på alla OS (ramarnas PNG:er går genom codecen i
// png.h), så de committade filerna aldrig byter innehåll för att någon
// byggde på en annan plattform.
//
// Ramarna är KVADRATISKA RGBA8 med rak alfa (skalade av anroparen — CLI:t
// `glazeassets icon` gör det med resizeRgba och valregeln). Skrivarna väljer
// inte storlekar själva: de skriver de ramar de får, i stigande ordning.

namespace glzassets {

// .ico: ICONDIR + en post per ram. Ramar under 256 px skrivs som 32-bitars
// BMP (BITMAPINFOHEADER med dubblad höjd, BGRA nerifrån och upp, och en
// AND-mask där en helt genomskinlig pixel är satt); 256 px skrivs som PNG,
// som Windows Vista och senare läser. false + orsak vid en tom, icke-
// kvadratisk eller för stor (> 256) ram, eller två ramar med samma storlek.
bool writeIco(const std::vector<Rgba>& frames, std::vector<std::uint8_t>& out,
              std::string& err);

// .icns: 'icns' + en TOC + elementen. Sidan avgör typerna:
//    16 ⇒ is32 (RGB med Apples RLE per kanal) + s8mk (alfan)
//    32 ⇒ il32 + l8mk, och ic11 (16@2x, PNG)
//    64 ⇒ ic12 (32@2x)          128 ⇒ ic07
//   256 ⇒ ic13 (128@2x) + ic08  512 ⇒ ic14 (256@2x) + ic09
//  1024 ⇒ ic10 (512@2x)
// En ram med en annan sida har ingen typ och avvisas (false + orsak), liksom
// en tom eller icke-kvadratisk ram, två ramar med samma sida och en tom lista.
bool writeIcns(const std::vector<Rgba>& frames, std::vector<std::uint8_t>& out,
               std::string& err);

// Sidorna .icns har en typ för, stigande — det CLI:t skalar fram.
inline constexpr int kIcnsSizes[] = {16, 32, 64, 128, 256, 512, 1024};

} // namespace glzassets
