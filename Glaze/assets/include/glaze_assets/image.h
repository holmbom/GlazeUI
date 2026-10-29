#pragma once
#include <cstdint>
#include <vector>

// glaze_assets — bildverktyget: en RGBA-bild i minnet,
// skalaren som gör om EN bild till alla ikonstorlekar och valregeln som
// väljer källa för en målstorlek. REN STD som resten av libben — konsumeras
// av glazeassets-CLI:t (`icon`) OCH av glaze_impl i drift (programikonens
// ramar och namnlistens skarpa nedskalning), så skalningen blir densamma i
// verktyget och i appen.
//
// Avkodningen i DRIFT går genom gfx::decodeImage (OS-kodeken); PNG-codecen
// i png.h är verktygets, för byte-identiska filer på alla OS.

namespace glzassets {

// RGBA8 med RAK (icke-förmultiplicerad) alfa, rad 0 överst, tätt packad:
// px.size() == width * height * 4. Samma layout som gfx::IconImage och
// gfx::decodeImage, så en ram går rakt över fasaden utan omkodning.
struct Rgba {
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> px;

    bool empty() const { return width <= 0 || height <= 0; }
};

// Ikonens storlekar i Windows-filen (.ico) och fönsterramarna — de storlekar
// skalet frågar efter vid 100–200 % (16/20/24/32/40/48) plus utforskarens
// stora vyer. Stigande.
inline constexpr int kIconSizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};

// freedesktops hicolor-tema (Linux `.desktop`-ikonen): katalogerna
// share/icons/hicolor/<N>x<N>/apps. Stigande.
inline constexpr int kHicolorSizes[] = {16, 22, 24, 32, 48, 64, 128, 256, 512};

// Skalar `src` till w×h. NEDÅT per axel: ytans medelvärde — varje
// målpixel är det exakta yt-viktade snittet av källpixlarna den täcker;
// UPPÅT per axel: bilinjärt (pixelcentrum mot pixelcentrum, kanten
// klampad). Båda räknas i FÖRMULTIPLICERAD rymd och delas tillbaka till rak
// alfa, så en genomskinlig pixels färg aldrig blöder in i kanten (den
// klassiska mörka ringen runt en ikon). Deterministisk — samma indata ger
// samma byte på alla plattformar (bara heltal och float i fast ordning).
// Tom källa eller w/h ≤ 0 ⇒ tom bild. Samma storlek ⇒ en kopia.
Rgba resizeRgba(const Rgba& src, int w, int h);

// En IKONRAM: `src` inpassad i en kvadrat side×side — den längsta sidan blir
// `side`, bildförhållandet behålls och marginalen är genomskinlig och
// centrerad (udda överskott till höger/nedåt). En kvadratisk källa är
// precis resizeRgba(src, side, side). Tom källa eller side ≤ 0 ⇒ tom bild.
Rgba fitSquare(const Rgba& src, int side);

// VALREGELN: för målstorleken `target` (px), index till den MINSTA
// källan vars sida är ≥ target, annars den STÖRSTA. `sides` är källornas
// sidor i px — för en icke-kvadratisk bild den LÄNGSTA sidan, för det är
// den som passas in i rutan. Sidor ≤ 0 (en källa som saknas) väljs aldrig.
// -1 när ingen källa finns. Lika sidor ⇒ den första.
int pickIconSource(const std::vector<int>& sides, int target);

} // namespace glzassets
