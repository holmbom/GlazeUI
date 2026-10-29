#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "glaze_assets/image.h"

// glaze_assets — en egen PNG-codec i REN STD. Den finns för
// VERKTYGET: `glazeassets icon` ska ge byte-identiska .ico/.icns/.png på
// alla OS, och det går inte genom OS:ets kodek (WIC, Image I/O och libpng
// komprimerar olika). I DRIFT avkodar appen genom gfx::decodeImage — codecen
// här är inte en andra avkodningsväg i ramverket.
//
// Avkodaren tar hela PNG-specen som en ikonkälla kan bära: färgtyp 0 (grå),
// 2 (RGB), 3 (palett), 4 (grå + alfa) och 6 (RGBA), bitdjup 1/2/4/8/16 där
// typen tillåter det, tRNS (palettens alfa, den genomskinliga grå- eller
// RGB-färgen), Adam7-interlace, och kontrollerar varje chunks CRC och
// zlib-strömmens adler32. Okända tilläggs-chunks hoppas över; en okänd
// KRITISK chunk (versal första bokstav) avvisas. 16 bitar avrundas till 8
// (v·255/65535, närmast). Utdata är alltid RGBA8 med rak alfa.
//
// Kodaren skriver färgtyp 6, bitdjup 8, utan interlace: filter per rad med
// minsta absoluta summa (den vanliga heuristiken) och en egen deflate (LZ77
// med fast Huffman-tabell, ett block). DETERMINISTISK — samma bild ger samma
// byte på alla plattformar och alla kompilatorer; inga tidsstämplar, inga
// tilläggs-chunks.

namespace glzassets {

// false + en orsak på svenska vid minsta fel (trasig signatur, CRC, zlib,
// orimlig storlek …); `out` är då tom. Största tillåtna bild: 16384 px per
// sida och 64 Mpx totalt — en ikonkälla är aldrig större, och taket stoppar
// en fientlig IHDR från att begära gigabyte.
bool decodePng(const std::uint8_t* data, std::size_t size, Rgba& out,
               std::string& err);

// Hela PNG-filen. Tom bild ⇒ tom vektor.
std::vector<std::uint8_t> encodePng(const Rgba& img);

} // namespace glzassets
