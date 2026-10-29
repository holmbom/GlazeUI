// icon_gate — PROGRAMIKONENS grind.
//
// Kör:  ./build-test/tests/icon_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Headless — inga fönster, ingen pump, ingen `gfx::init`. Sju delar, en per
// lager som ikonen går genom på väg från en PNG till namnlisten:
//
//   A  PNG-CODECEN i glaze_assets mot en OBEROENDE kodek. A1: vår kodare →
//      OS:ets avkodare (`gfx::decodeImage` — WIC / Image I/O / libpng) ger
//      samma pixlar. A2: OS:ets kodare (`gfx::encodePng`) → vår avkodare.
//      A3: PNG:er som grinden SJÄLV bygger (lagrad deflate, egen CRC och
//      adler) i färgtyperna 0/2/3/4/6, bitdjupen 1/4/8/16, tRNS och Adam7 —
//      mot ett förväntat värde räknat här OCH mot OS:ets avkodare. A4: trasig CRC,
//      avkortad fil och okänd kritisk chunk avvisas.
//   B  SKALAREN (`resizeRgba`/`fitSquare`): enfärgat förblir enfärgat vid
//      VARJE mål i kIconSizes och kHicolorSizes, uppåt och nedåt, ett
//      schackbräde blir sitt yt-medelvärde, FÖRMULTIPLICERAD rymd (en
//      genomskinlig röd pixel färgar inte en blå granne), samma storlek är
//      identiteten och en avlång bild passas in centrerad.
//   C  VALREGELN — `glzassets::pickIconSource` och fasadens
//      `gfx::pickIconImage` mot SAMMA handskrivna förväntade värde, två implementationer
//      av en regel.
//   D  FILERNA: `.ico` (ICONDIR, BMP med dubblad höjd och AND-mask, PNG för
//      256 — och på Windows läst av OS:ets egen `CreateIconFromResourceEx`),
//      `.icns` (längd, TOC, elementen per sida, is32-planen uppackade med
//      grindens egen RLE-läsare, s8mk-masken) och de COMMITTADE filerna i
//      Glaze/assets/icon/: ramarna, och att de är byte för byte vad
//      `glazeassets icon` (det RIKTIGA verktyget, `GLAZE_ASSETS_CLI`) ger ur
//      den committade framework_assets.bin.
//   E  INBÄDDNINGEN: `kFrameworkAssetsBin` är byte för byte den committade
//      framework_assets.bin, butiken bär glaze/icon (256) och glaze/icon16
//      (16), och ramverkets handtag har bit 31 och överlever en ny
//      `loadAssets` (demons assets.bin laddas mitt i fallet).
//   F  FASADEN: `gfx::setAppId` (tillåtna tecken, längdtaket, tom = programfilens
//      namn) och `gfx::pickIconImage` hoppar över ogiltiga ramar.
//   G  APPICON/APPWINDOW i ett okonstruerat (aldrig visat) fönster: förvalet
//      (synlig, egen bild saknas, inte fokuserbar, rotbarn), platsen ur
//      `reservedWidth` med `noIcon`/`hide`/den stora formen med och utan
//      menyrad, sidan per plattform, programmenyn som inte öppnas tom,
//      kollektions- och trädformen som samma tillstånd, acceleratorn i
//      programmenyn, den tomma stigen som rot och den underkända som aldrig
//      landar där, och OS-ikonens ramar (förvalets bilder, en ram per
//      kIconSizes, 16-ramen oskalad, cachen — mätt med `iconFrameCacheSize`;
//      ramarna svarar med värde)
//   G10 BILDER UR PIXLAR:
//      bildvärdets likhet (två ogiltiga lika, en ny version olika med `==`
//      men samma med `sameImage`, klass 0), OS-ikonen ur en Bitmap (16-ramen
//      är `fitSquare` av pixlarna byte för byte, `programFirst` false, en ny
//      ram efter `setPixels`, ingen post i processcachen, en RenderTexture ger
//      inga ramar), programikonen som ritar en Bitmap genom glyphResampled
//      (EN omräknad bild i rutans fysiska storlek) och de omräknade bildernas
//      BUDGET — samma Bitmap i två cacher med växlande storlekar håller
//      minnesbudgeten, LRU:n vräker, uppladdningsbudgeten skjuter upp den
//      andra nya storleken och en död bild släpps i nästa trim.
//   G11 EN BITMAP UTAN PIXLAR: ingen OS-ikon, ingen omräknad
//      bild i ett fönster utan den, och `Draw::glyphResampled` ritar den genom
//      `glyph` (en inspelad drawImage).
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · png.cpp: `<=` → `<` i paeth ......................... A1/A2 fäller
//   · png.cpp: `v*17` → `v*16` i 4-bitsskalningen ............. A3 fäller
//   · png.cpp: hoppa över tRNS ................................ A3 fäller
//   · png.cpp: hoppa över CRC-kontrollen ..................... A4 fäller
//   · linux_image.cpp: `png_set_scale_16` borttagen (Linux) .... A3 fäller
//   · image.cpp: medelvärde i RAK alfa i stället för förmultiplicerad . B3
//   · image.cpp: restkorrigeringen i axisTaps borttagen ........ B1 fäller
//   · image.cpp: pickIconSource väljer den STÖRSTA alltid ...... C fäller
//   · icon_file.cpp: BMP-höjden odubblad ...................... D1 fäller
//   · icon_file.cpp: s8mk ur fel kanal ........................ D2 fäller
//   · icon_file.cpp: RLE-längden räknad fel (c−0x80+2) ......... D2 fäller
//   · en ändrad skalare/kodare utan omgenererade ikonfiler ..... D3 fäller
//   · en committad framework_assets.bin som inte är inbäddad ... E1 fäller
//   · assets.cpp: `kBuiltinBit` borttagen ...................... E3 fäller
//   · platform.cpp: setAppId utan teckenkontroll ............. F1 fäller
//   · app_icon.cpp: reservedWidth ignorerar noIcon/hide ........ G2 fäller
//   · app_icon.cpp: stor form utan synlig menyrad ............. G3 fäller
//   · menu_path.h: `!path.valid()` borttagen i descendMenu ...... G8 fäller
//   · window_icon.cpp: pickIconSource → alltid den största ..... G9 fäller
//   · shape.cpp: versionens snapshot borttagen (version_ = 0) ..... G10
//   · window_icon.cpp: avkoda svarar tomt för en Bitmap .......... G10
//   · window_icon.cpp: Bitmap-ramarna cachade på id utan version . G10
//   · image_cache.cpp: bitmapResampled_ utanför LRU-kandidaterna .. G10
//   · image_cache.cpp: resampledBitmap utan mayUpload ............. G10
//   · image_cache.cpp: släppDöda(bitmapResampled_) borttagen ...... G10
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · att OS:et VISAR ikonen (aktivitetsfältet, Alt+Tab, dockan, Waylands
//     xdg-toplevel-icon, X11:s _NET_WM_ICON) — det kräver ett skrivbord och
//     ögon och står i doc/os/*/TESTS.md. `iconApplied()` mäts inte här: den
//     kräver ett riktigt fönster.
//   · att ikonen RITAS i namnlisten (pixlar) — pixel_gate räknar den som
//     inbyggd chrome och mäter den inte; utseendet hör till utseendepasset.
//   · CHROME-GEOMETRIN i ett visat fönster: att ikonen resolvas mot
//     chrome-ctx:t (de sex `isChromeRoot`-ställena), menyradens
//     `titleStart`, TopBars hårlinjeluckor (`innerEdgeInsets`), motvikten
//     `CaptionBalance` och träffytan (`HitRole::Client`) — de är privata
//     eller kräver ett fönster och mäts här bara genom `reservedWidth`.
//   · vilken `gfx::WindowIcon` (`programFirst`) som når OS-fönstret —
//     `AppWindow::applyWindowIcon` är privat; ramarna mäts (G9), beslutet inte.
//   · att menyradens genväg vinner över programmenyns vid samma genväg
//     (ordningen i `AppWindow::handleEvent`).
//   · klicket som öppnar programmenyn och plattan vid hover (kräver musen).
//   · programfilens ikon: att windres/`.icns`/`.desktop` hamnar i
//     programfilen (`glaze_app_icon` är CMake, bygget är beviset).
//   · att `.glazeui`-formerna utförs — det mäter preview_gate
//     (fallProgramikon) och roundtrip_gate (calls-fixturen).
//   · att ändrade Bitmap-pixlar NÅR OS:et: G10 mäter ramarna `osIcon` räknar
//     ur dem och att settrarna släpper igenom en ny version (`==`), men inte
//     att `AppIcon::setIcon` lägger om fönstrets ikon (`applyWindowIcon` är
//     privat, OS-ikonen går inte att läsa tillbaka) eller att systemfältet
//     visar dem (tray_gate lägger aldrig en ikon i fältet) —
//     doc/os/*/TESTS.md och handtestet.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include <gfx/image.h>
#include <gfx/null_renderer.h>   // G10: den inspelande renderaren
#include <gfx/platform.h>
#include <gfx/window.h>

#include "glaze_assets/assets_file.h"
#include "glaze_assets/icon_file.h"
#include "glaze_assets/image.h"
#include "glaze_assets/png.h"

#include "assets_builtin.h"   // Glaze/widgets/src — den inbäddade butiken
#include "canvas/image_data.h"   // Glaze/widgets/src — pumpens släpp (G11)
#include "icon_util.h"        // Glaze/gfx/src — pickIconImage
#include "image_cache.h"      // Glaze/widgets/src — detail::builtinAsset
#include "window_icon.h"      // Glaze/widgets/src — OS-ikonens ramar

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>          // D1: Windows egen ikonläsare som oberoende förväntat värde
#endif

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s — fick \"%s\"\n", what.c_str(), fick.c_str());
    ++g_fails;
}

std::string rot()
{
    return std::string(GLAZE_TEST_ROOT) + "/";
}

std::vector<std::uint8_t> läsFil(const std::string& sökväg)
{
    std::ifstream f(sökväg, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f),
                                     std::istreambuf_iterator<char>());
}

// En deterministisk testbild med varierad färg OCH alfa — inga slumptal, så
// ett fel går att återskapa.
glzassets::Rgba testbild(int w, int h)
{
    glzassets::Rgba b;
    b.width  = w;
    b.height = h;
    b.px.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &b.px[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0] = static_cast<std::uint8_t>((x * 37 + y * 11) & 0xFF);
            p[1] = static_cast<std::uint8_t>((x * 5 + y * 71) & 0xFF);
            p[2] = static_cast<std::uint8_t>((x * y * 3 + 17) & 0xFF);
            p[3] = static_cast<std::uint8_t>(((x + y) % 5 == 0) ? 0
                                             : (x * 29 + y * 13) & 0xFF);
        }
    return b;
}

glzassets::Rgba enfärgad(int w, int h, std::uint8_t r, std::uint8_t g,
                         std::uint8_t b, std::uint8_t a)
{
    glzassets::Rgba ut;
    ut.width  = w;
    ut.height = h;
    ut.px.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < ut.px.size(); i += 4) {
        ut.px[i]     = r;
        ut.px[i + 1] = g;
        ut.px[i + 2] = b;
        ut.px[i + 3] = a;
    }
    return ut;
}

// ------------------------------------------------ grindens EGNA PNG-verktyg
//
// OBEROENDE av glaze_assets med flit: CRC32, adler32 och zlib-strömmen är
// skrivna här, och deflaten är LAGRAD (okomprimerad) — så en PNG som grinden
// bygger beror inte på kodaren den ska mäta.

std::uint32_t crc32(const std::uint8_t* d, std::size_t n,
                    std::uint32_t c = 0xFFFFFFFFu)
{
    for (std::size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return c;
}

void be32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

std::uint32_t läsBe32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

std::uint32_t läsLe32(const std::uint8_t* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint16_t läsLe16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

void chunk(std::vector<std::uint8_t>& png, const char* typ,
           const std::vector<std::uint8_t>& data)
{
    be32(png, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> td(typ, typ + 4);
    td.insert(td.end(), data.begin(), data.end());
    png.insert(png.end(), td.begin(), td.end());
    be32(png, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

std::vector<std::uint8_t> zlibLagrad(const std::vector<std::uint8_t>& rå)
{
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t               i = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, rå.size() - i);
        const bool        sist = i + n == rå.size();
        z.push_back(sist ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), rå.begin() + static_cast<std::ptrdiff_t>(i),
                 rå.begin() + static_cast<std::ptrdiff_t>(i + n));
        i += n;
    } while (i < rå.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : rå) {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    be32(z, (b << 16) | a);
    return z;
}

// En PNG-fixtur: SAMPLEN per pixel (en per kanal, i bitdjupets skala) och
// förväntat värde i RGBA8 räknas av anroparen.
struct PngFall {
    const char*               namn;
    int                       w = 0, h = 0;
    int                       djup = 8;
    int                       färgtyp = 6;
    bool                      adam7 = false;
    std::vector<std::uint8_t> plte;   // PLTE-chunkens bytes (typ 3)
    std::vector<std::uint8_t> trns;   // tRNS-chunkens bytes
    // samples[(y*w + x)*kanaler + k]
    std::vector<std::uint16_t> samples;
    std::vector<std::uint8_t>  facit;   // RGBA8, rak alfa
};

int kanaler(int färgtyp)
{
    switch (färgtyp) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    default: return 4;
    }
}

// En rad (eller del-rad i ett Adam7-pass) packad med filtret None.
void packaRad(const PngFall& f, int y, int x0, int dx,
              std::vector<std::uint8_t>& ut)
{
    ut.push_back(0);   // filter None
    const int    kan  = kanaler(f.färgtyp);
    unsigned     acc  = 0;
    int          bits = 0;
    for (int x = x0; x < f.w; x += dx)
        for (int k = 0; k < kan; ++k) {
            const std::uint16_t s =
                f.samples[(static_cast<std::size_t>(y) * f.w + x) * kan + k];
            if (f.djup == 16) {
                ut.push_back(static_cast<std::uint8_t>(s >> 8));
                ut.push_back(static_cast<std::uint8_t>(s & 0xFF));
            } else if (f.djup == 8) {
                ut.push_back(static_cast<std::uint8_t>(s));
            } else {
                acc  = (acc << f.djup) | s;
                bits += f.djup;
                if (bits == 8) {
                    ut.push_back(static_cast<std::uint8_t>(acc));
                    acc  = 0;
                    bits = 0;
                }
            }
        }
    if (bits > 0)
        ut.push_back(static_cast<std::uint8_t>(acc << (8 - bits)));
}

std::vector<std::uint8_t> byggPng(const PngFall& f)
{
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A,
                                     0x0A};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, static_cast<std::uint32_t>(f.w));
    be32(ihdr, static_cast<std::uint32_t>(f.h));
    ihdr.push_back(static_cast<std::uint8_t>(f.djup));
    ihdr.push_back(static_cast<std::uint8_t>(f.färgtyp));
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(f.adam7 ? 1 : 0);
    chunk(png, "IHDR", ihdr);
    if (!f.plte.empty())
        chunk(png, "PLTE", f.plte);
    if (!f.trns.empty())
        chunk(png, "tRNS", f.trns);

    std::vector<std::uint8_t> rå;
    if (!f.adam7) {
        for (int y = 0; y < f.h; ++y)
            packaRad(f, y, 0, 1, rå);
    } else {
        static const int kPass[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8},
                                        {0, 4, 4, 8}, {2, 0, 4, 4},
                                        {0, 2, 2, 4}, {1, 0, 2, 2},
                                        {0, 1, 1, 2}};
        for (const auto& p : kPass) {
            if (p[0] >= f.w || p[1] >= f.h)
                continue;   // tomt pass — inga rader alls
            for (int y = p[1]; y < f.h; y += p[3])
                packaRad(f, y, p[0], p[2], rå);
        }
    }
    chunk(png, "IDAT", zlibLagrad(rå));
    chunk(png, "IEND", {});
    return png;
}

bool sammaPixlar(const std::vector<std::uint8_t>& a,
                 const std::vector<std::uint8_t>& b, std::string& var)
{
    if (a.size() != b.size()) {
        var = "storlek " + std::to_string(a.size()) + " mot " +
              std::to_string(b.size());
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) {
            var = "byte " + std::to_string(i) + ": " + std::to_string(a[i]) +
                  " mot " + std::to_string(b[i]);
            return false;
        }
    return true;
}

// Förväntat värde för en helt genomskinlig pixel jämförs bara i alfa: färgen under
// alfa 0 är odefinierad i en kodek som förmultiplicerar internt.
bool sammaSynligt(const std::vector<std::uint8_t>& a,
                  const std::vector<std::uint8_t>& b, std::string& var)
{
    if (a.size() != b.size()) {
        var = "storlek";
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i += 4) {
        const bool genom = a[i + 3] == 0 && b[i + 3] == 0;
        for (int k = genom ? 3 : 0; k < 4; ++k)
            if (a[i + k] != b[i + k]) {
                var = "pixel " + std::to_string(i / 4) + " kanal " +
                      std::to_string(k) + ": " + std::to_string(a[i + k]) +
                      " mot " + std::to_string(b[i + k]);
                return false;
            }
    }
    return true;
}

// ---------------------------------------------------------------- A codec

void delA()
{
    std::printf("\n=== A. PNG-codecen mot en oberoende kodek ===\n");

    // A1: vår kodare → OS:ets avkodare.
    {
        const glzassets::Rgba     bild = testbild(37, 23);
        std::vector<std::uint8_t> png  = glzassets::encodePng(bild);
        std::vector<std::uint8_t> rgba;
        int                       w = 0, h = 0;
        const bool ok = gfx::decodeImage(png.data(), png.size(), rgba, w, h);
        std::string var;
        check(ok && w == 37 && h == 23 && sammaSynligt(rgba, bild.px, var),
              "A1: glzassets::encodePng → gfx::decodeImage ger samma pixlar",
              var);
        check(glzassets::encodePng(bild) == png,
              "A1: kodaren är DETERMINISTISK (två körningar, samma byte)");
    }

    // A2: OS:ets kodare → vår avkodare.
    {
        const glzassets::Rgba bild = testbild(29, 31);
        const std::string     sökväg =
            (std::filesystem::temp_directory_path() / "icon_gate_a2.png")
                .string();
        const bool skriven =
            gfx::encodePng(sökväg, bild.px, bild.width, bild.height);
        const std::vector<std::uint8_t> fil = läsFil(sökväg);
        std::filesystem::remove(sökväg);
        glzassets::Rgba ut;
        std::string     err, var;
        const bool ok = skriven && glzassets::decodePng(fil.data(), fil.size(),
                                                        ut, err);
        check(ok && ut.width == 29 && ut.height == 31 &&
                  sammaSynligt(ut.px, bild.px, var),
              "A2: gfx::encodePng → glzassets::decodePng ger samma pixlar",
              err + var);
    }

    // A3: grindens egna PNG:er i varje färgtyp.
    std::vector<PngFall> fall;
    {   // grå 1 bit, 13 px bred (rad-utfyllnad mitt i en byte)
        PngFall f{"grå 1 bit"};
        f.w = 13; f.h = 3; f.djup = 1; f.färgtyp = 0;
        for (int i = 0; i < f.w * f.h; ++i) {
            const std::uint16_t s = static_cast<std::uint16_t>((i * 7) % 2);
            f.samples.push_back(s);
            const std::uint8_t v = s ? 255 : 0;
            f.facit.insert(f.facit.end(), {v, v, v, 255});
        }
        fall.push_back(f);
    }
    {   // grå 4 bit med tRNS-nyckel 5
        PngFall f{"grå 4 bit + tRNS"};
        f.w = 9; f.h = 4; f.djup = 4; f.färgtyp = 0;
        f.trns = {0, 5};
        for (int i = 0; i < f.w * f.h; ++i) {
            const std::uint16_t s = static_cast<std::uint16_t>(i % 16);
            f.samples.push_back(s);
            const std::uint8_t v = static_cast<std::uint8_t>(s * 17);
            f.facit.insert(f.facit.end(),
                           {v, v, v, static_cast<std::uint8_t>(s == 5 ? 0 : 255)});
        }
        fall.push_back(f);
    }
    {   // grå 16 bit (samplen v·257 ⇒ exakt v efter avrundningen)
        PngFall f{"grå 16 bit"};
        f.w = 5; f.h = 5; f.djup = 16; f.färgtyp = 0;
        for (int i = 0; i < f.w * f.h; ++i) {
            const std::uint8_t v = static_cast<std::uint8_t>(i * 10);
            f.samples.push_back(static_cast<std::uint16_t>(v * 257));
            f.facit.insert(f.facit.end(), {v, v, v, 255});
        }
        fall.push_back(f);
    }
    {   // RGB 8 bit med tRNS-färg (10, 20, 30)
        PngFall f{"RGB 8 bit + tRNS"};
        f.w = 6; f.h = 4; f.djup = 8; f.färgtyp = 2;
        f.trns = {0, 10, 0, 20, 0, 30};
        for (int i = 0; i < f.w * f.h; ++i) {
            const bool nyckel = i % 4 == 1;
            const std::uint8_t r = nyckel ? 10 : static_cast<std::uint8_t>(i * 9);
            const std::uint8_t g = nyckel ? 20 : static_cast<std::uint8_t>(200 - i);
            const std::uint8_t b = nyckel ? 30 : static_cast<std::uint8_t>(i * 3 + 1);
            f.samples.insert(f.samples.end(), {r, g, b});
            f.facit.insert(f.facit.end(),
                           {r, g, b, static_cast<std::uint8_t>(nyckel ? 0 : 255)});
        }
        fall.push_back(f);
    }
    {   // palett 4 bit med tRNS kortare än paletten
        PngFall f{"palett 4 bit + tRNS"};
        f.w = 7; f.h = 3; f.djup = 4; f.färgtyp = 3;
        for (int k = 0; k < 6; ++k)
            f.plte.insert(f.plte.end(),
                          {static_cast<std::uint8_t>(k * 40),
                           static_cast<std::uint8_t>(255 - k * 30),
                           static_cast<std::uint8_t>(k * 7)});
        f.trns = {0, 128, 255};   // index 0–2; 3–5 är ogenomskinliga
        for (int i = 0; i < f.w * f.h; ++i) {
            const int k = i % 6;
            f.samples.push_back(static_cast<std::uint16_t>(k));
            const std::uint8_t a =
                k < 3 ? f.trns[static_cast<std::size_t>(k)] : 255;
            f.facit.insert(f.facit.end(),
                           {f.plte[static_cast<std::size_t>(k * 3)],
                            f.plte[static_cast<std::size_t>(k * 3 + 1)],
                            f.plte[static_cast<std::size_t>(k * 3 + 2)], a});
        }
        fall.push_back(f);
    }
    {   // grå + alfa 8 bit
        PngFall f{"grå + alfa 8 bit"};
        f.w = 4; f.h = 6; f.djup = 8; f.färgtyp = 4;
        for (int i = 0; i < f.w * f.h; ++i) {
            const std::uint8_t v = static_cast<std::uint8_t>(i * 11);
            const std::uint8_t a = static_cast<std::uint8_t>(255 - i * 9);
            f.samples.insert(f.samples.end(), {v, a});
            f.facit.insert(f.facit.end(), {v, v, v, a});
        }
        fall.push_back(f);
    }
    {   // RGBA 16 bit, Adam7 (10×9 — varje pass har pixlar)
        PngFall f{"RGBA 16 bit Adam7"};
        f.w = 10; f.h = 9; f.djup = 16; f.färgtyp = 6; f.adam7 = true;
        for (int i = 0; i < f.w * f.h; ++i) {
            const std::uint8_t r = static_cast<std::uint8_t>(i * 3);
            const std::uint8_t g = static_cast<std::uint8_t>(255 - i);
            const std::uint8_t b = static_cast<std::uint8_t>(i * 7);
            const std::uint8_t a = static_cast<std::uint8_t>(i % 3 ? 255 : 90);
            for (std::uint8_t v : {r, g, b, a})
                f.samples.push_back(static_cast<std::uint16_t>(v * 257));
            f.facit.insert(f.facit.end(), {r, g, b, a});
        }
        fall.push_back(f);
    }
    {   // RGBA 8 bit, Adam7, 3×2 — de flesta passen är TOMMA
        PngFall f{"RGBA 8 bit Adam7 med tomma pass"};
        f.w = 3; f.h = 2; f.djup = 8; f.färgtyp = 6; f.adam7 = true;
        for (int i = 0; i < f.w * f.h; ++i) {
            const std::uint8_t v = static_cast<std::uint8_t>(40 + i * 30);
            f.samples.insert(f.samples.end(), {v, 0, 255, 255});
            f.facit.insert(f.facit.end(), {v, 0, 255, 255});
        }
        fall.push_back(f);
    }
    for (const PngFall& f : fall) {
        const std::vector<std::uint8_t> png = byggPng(f);
        glzassets::Rgba                 ut;
        std::string                     err, var;
        const bool ok = glzassets::decodePng(png.data(), png.size(), ut, err);
        check(ok && ut.width == f.w && ut.height == f.h &&
                  sammaSynligt(ut.px, f.facit, var),
              std::string("A3: glzassets avkodar ") + f.namn + " mot facit",
              err + var);
        // OS-JÄMFÖRELSEN GÄLLER ÄVEN LINUX:
        // libpng:s förenklade API räknar fel på 16-bitars RGBA, och
        // avkodaren går därför genom det fulla API:t med en rundad
        // 16 → 8-skalning. SABOTAGE (Linux): `png_set_scale_16` borttagen i
        // linux_image.cpp ⇒ raderna blir 8 byte per pixel, avkodningen
        // avvisas och "RGBA 16 bit Adam7" faller här.
        std::vector<std::uint8_t> os;
        int                       w = 0, h = 0;
        const bool osOk = gfx::decodeImage(png.data(), png.size(), os, w, h);
        std::string var2;
        check(osOk && w == f.w && h == f.h && sammaSynligt(os, f.facit, var2),
              std::string("A3: OS:ets avkodare håller med facit (") + f.namn +
                  ") — fixturen är giltig PNG",
              var2);
    }

    // A4: avslagen.
    {
        PngFall f = fall.back();
        std::vector<std::uint8_t> png = byggPng(f);
        glzassets::Rgba           ut;
        std::string               err;
        std::vector<std::uint8_t> trasig = png;
        trasig[8 + 8 + 13 + 2] ^= 0x01;   // en byte i IHDR-chunkens CRC
        check(!glzassets::decodePng(trasig.data(), trasig.size(), ut, err) &&
                  ut.px.empty(),
              "A4: trasig CRC avvisas och lämnar utdata tom", err);
        std::vector<std::uint8_t> kort(png.begin(), png.end() - 20);
        check(!glzassets::decodePng(kort.data(), kort.size(), ut, err),
              "A4: avkortad fil avvisas");
        // En okänd KRITISK chunk (versal första bokstav) före IDAT.
        std::vector<std::uint8_t> okänd(png.begin(), png.begin() + 33);
        chunk(okänd, "ZZZZ", {1, 2, 3});
        okänd.insert(okänd.end(), png.begin() + 33, png.end());
        check(!glzassets::decodePng(okänd.data(), okänd.size(), ut, err),
              "A4: okänd kritisk chunk avvisas");
        // …medan en okänd TILLÄGGS-chunk hoppas över.
        std::vector<std::uint8_t> tillägg(png.begin(), png.begin() + 33);
        chunk(tillägg, "zzZz", {1, 2, 3});
        tillägg.insert(tillägg.end(), png.begin() + 33, png.end());
        check(glzassets::decodePng(tillägg.data(), tillägg.size(), ut, err),
              "A4: okänd tilläggs-chunk hoppas över", err);
    }
}

// --------------------------------------------------------------- B skalaren

void delB()
{
    std::printf("\n=== B. Skalaren ===\n");
    {
        // VARJE mål i båda storlekslistorna, ur en 256- och en 16-källa —
        // uppåt, nedåt och de udda kvoterna (256 → 20, 256 → 22) där en
        // vikt som inte summerar till 1 hade gett en kant eller en ton.
        std::vector<int> mål(std::begin(glzassets::kIconSizes),
                             std::end(glzassets::kIconSizes));
        mål.insert(mål.end(), std::begin(glzassets::kHicolorSizes),
                   std::end(glzassets::kHicolorSizes));
        std::string fel;
        for (int källa : {256, 16})
            for (int m : mål) {
                const glzassets::Rgba src =
                    enfärgad(källa, källa, 200, 100, 50, 255);
                const glzassets::Rgba ut = glzassets::resizeRgba(src, m, m);
                if (ut.width != m || ut.height != m ||
                    ut.px != enfärgad(m, m, 200, 100, 50, 255).px)
                    fel += " " + std::to_string(källa) + "→" +
                           std::to_string(m);
            }
        check(fel.empty(),
              "B1: enfärgat förblir enfärgat (färg och alfa 255) vid varje "
              "mål, uppåt och nedåt", fel);
    }
    {
        glzassets::Rgba bräde = enfärgad(2, 2, 0, 0, 0, 255);
        for (int i : {0, 3})
            for (int k = 0; k < 3; ++k)
                bräde.px[static_cast<std::size_t>(i * 4 + k)] = 255;
        const glzassets::Rgba ett = glzassets::resizeRgba(bräde, 1, 1);
        check(ett.px.size() == 4 && ett.px[0] >= 127 && ett.px[0] <= 128 &&
                  ett.px[0] == ett.px[1] && ett.px[1] == ett.px[2] &&
                  ett.px[3] == 255,
              "B2: schackbrädet blir sitt yt-medelvärde",
              ett.px.empty() ? "" : std::to_string(ett.px[0]));
    }
    {
        // En GENOMSKINLIG röd pixel bredvid en blå: i förmultiplicerad rymd
        // bidrar den röda inget till färgen, bara till alfan.
        glzassets::Rgba par = enfärgad(2, 1, 255, 0, 0, 0);
        par.px[4] = 0; par.px[5] = 0; par.px[6] = 255; par.px[7] = 255;
        const glzassets::Rgba ett = glzassets::resizeRgba(par, 1, 1);
        check(ett.px.size() == 4 && ett.px[0] <= 1 && ett.px[2] >= 254 &&
                  ett.px[3] >= 127 && ett.px[3] <= 128,
              "B3: förmultiplicerad rymd — ingen röd kant av en osynlig pixel",
              ett.px.size() == 4 ? std::to_string(ett.px[0]) + "," +
                                       std::to_string(ett.px[2]) + "," +
                                       std::to_string(ett.px[3])
                                 : "");
    }
    {
        const glzassets::Rgba bild = testbild(20, 20);
        const glzassets::Rgba same = glzassets::resizeRgba(bild, 20, 20);
        std::string var;
        check(sammaSynligt(same.px, bild.px, var),
              "B4: samma storlek är identiteten", var);
        const glzassets::Rgba avlång = enfärgad(32, 16, 9, 9, 9, 255);
        const glzassets::Rgba ruta   = glzassets::fitSquare(avlång, 32);
        bool ok = ruta.width == 32 && ruta.height == 32;
        // Rad 0–7 och 24–31 är marginal (genomskinlig), 8–23 bilden.
        for (int y = 0; ok && y < 32; ++y) {
            const std::uint8_t a =
                ruta.px[(static_cast<std::size_t>(y) * 32 + 16) * 4 + 3];
            ok = (y >= 8 && y < 24) ? a == 255 : a == 0;
        }
        check(ok, "B4: fitSquare passar in en avlång bild centrerad");
        check(glzassets::resizeRgba(glzassets::Rgba{}, 16, 16).empty(),
              "B4: tom källa ger tom bild");
    }
}

// ---------------------------------------------------------------- C valregeln

struct ValFall {
    std::vector<int> sidor;
    int              mål;
    int              facit;   // index, -1 = ingen
};

const ValFall kValFall[] = {
    {{16, 256}, 16, 0},     // exakt träff
    {{16, 256}, 17, 1},     // minsta som räcker
    {{16, 256}, 512, 1},    // ingen räcker ⇒ den största
    {{256, 16}, 16, 1},     // ordningen spelar ingen roll
    {{256, 16}, 8, 1},      // minsta som räcker även under alla
    {{}, 16, -1},           // ingen källa
    {{0, 32}, 16, 1},       // en saknad källa väljs aldrig
    {{32, 32}, 16, 0},      // lika sidor ⇒ den första
    {{0}, 16, -1},          // bara en saknad källa
    {{16, 32, 256}, 24, 1}, // mellan två källor ⇒ den större av dem
    {{16, 32, 256}, 300, 2},// över alla ⇒ den största
};

void delC()
{
    std::printf("\n=== C. Valregeln ===\n");
    for (const ValFall& f : kValFall) {
        const int fick = glzassets::pickIconSource(f.sidor, f.mål);
        std::string namn = "C: pickIconSource({";
        for (int s : f.sidor)
            namn += std::to_string(s) + " ";
        namn += "}, " + std::to_string(f.mål) + ") = " +
                std::to_string(f.facit);
        check(fick == f.facit, namn, std::to_string(fick));

        // Fasadens tvilling mot SAMMA förväntat värde.
        gfx::WindowIcon ikon;
        for (int s : f.sidor) {
            gfx::IconImage im;
            if (s > 0) {
                im.width  = s;
                im.height = s;
                im.rgba.assign(static_cast<std::size_t>(s) * s * 4, 0);
            }
            ikon.images.push_back(std::move(im));
        }
        const gfx::IconImage* val = gfx::pickIconImage(ikon, f.mål);
        const int index =
            val ? static_cast<int>(val - ikon.images.data()) : -1;
        check(index == f.facit,
              "C: gfx::pickIconImage håller med (" + namn.substr(3) + ")",
              std::to_string(index));
    }
}

// ---------------------------------------------------------------- D filerna

void delD()
{
    std::printf("\n=== D. Filerna ===\n");
    const glzassets::Rgba r16  = testbild(16, 16);
    const glzassets::Rgba r32  = testbild(32, 32);
    const glzassets::Rgba r128 = testbild(128, 128);
    const glzassets::Rgba r256 = testbild(256, 256);

    // D1: .ico
    {
        std::vector<std::uint8_t> ico;
        std::string               err;
        const bool ok = glzassets::writeIco({r16, r32, r256}, ico, err);
        check(ok && ico.size() > 6 + 3 * 16, "D1: writeIco lyckas", err);
        if (ok && ico.size() > 6 + 3 * 16) {
            check(läsLe16(&ico[0]) == 0 && läsLe16(&ico[2]) == 1 &&
                      läsLe16(&ico[4]) == 3,
                  "D1: ICONDIR (reserverad 0, typ 1, tre poster)");
            bool fältOk = true;
            for (int i = 0; i < 3; ++i) {
                const std::uint8_t* e  = &ico[6 + static_cast<std::size_t>(i) * 16];
                const int           sv = i == 0 ? 16 : i == 1 ? 32 : 0;
                const std::uint32_t storlek = läsLe32(e + 8);
                const std::uint32_t offset  = läsLe32(e + 12);
                fältOk = fältOk && e[0] == sv && e[1] == sv &&
                         offset + storlek <= ico.size();
            }
            check(fältOk, "D1: posterna bär sidan (0 = 256) och ryms i filen");

            // 256-ramen är en PNG som avkodas till ramen.
            const std::uint8_t* e2  = &ico[6 + 2 * 16];
            const std::uint32_t o2  = läsLe32(e2 + 12);
            const std::uint32_t n2  = läsLe32(e2 + 8);
            glzassets::Rgba     png;
            std::string         perr, var;
            const bool          pOk = o2 + n2 <= ico.size() && ico[o2] == 0x89 &&
                             glzassets::decodePng(&ico[o2], n2, png, perr);
            check(pOk && sammaSynligt(png.px, r256.px, var),
                  "D1: 256-ramen är en PNG med ramens pixlar", perr + var);

            // 16-ramen: BITMAPINFOHEADER med DUBBLAD höjd, 32 bpp, BGRA
            // nerifrån och upp, och AND-masken satt för genomskinligt.
            const std::uint8_t* e0 = &ico[6];
            const std::uint32_t o0 = läsLe32(e0 + 12);
            const std::uint8_t* bmp = &ico[o0];
            check(läsLe32(bmp) == 40 && läsLe32(bmp + 4) == 16 &&
                      läsLe32(bmp + 8) == 32 && läsLe16(bmp + 14) == 32,
                  "D1: BMP-ramen har dubblad höjd och 32 bitar per pixel");
            const std::uint8_t* pix = bmp + 40;
            // Pixel (x=3, y=0) överst i bilden = sista raden i BMP:n.
            const std::uint8_t* p   = pix + (15 * 16 + 3) * 4;
            const std::uint8_t* fac = &r16.px[3 * 4];
            check(p[0] == fac[2] && p[1] == fac[1] && p[2] == fac[0] &&
                      p[3] == fac[3],
                  "D1: BMP-ramen är BGRA nerifrån och upp");
            // AND-masken: 16 px = 2 byte, radbredd utfylld till 4 byte.
            const std::uint8_t* mask = pix + 16 * 16 * 4;
            bool maskOk = true;
            for (int y = 0; y < 16 && maskOk; ++y)
                for (int x = 0; x < 16 && maskOk; ++x) {
                    const bool genom =
                        r16.px[(static_cast<std::size_t>(y) * 16 + x) * 4 + 3] == 0;
                    const std::uint8_t byte =
                        mask[(15 - y) * 4 + x / 8];
                    const bool satt = (byte >> (7 - x % 8)) & 1;
                    maskOk = satt == genom;
                }
            check(maskOk, "D1: AND-masken är satt exakt för genomskinliga pixlar");
        }
#ifdef _WIN32
        // EN OBEROENDE LÄSARE: Windows egen ikonavkodning. Posten väljs ur
        // filens ICONDIR (offset + storlek — en .ico-FIL har inte resursens
        // katalogform, så `LookupIconIdFromDirectoryEx` går inte att använda)
        // och bilddatat går till `CreateIconFromResourceEx`, som läser både
        // BMP-ramen och PNG-ramen; bitmappens mått bekräftar att Windows
        // tolkade den dubblade höjden som bild + mask.
        if (ok && ico.size() > 6 + 3 * 16) {
            int post = 0;
            for (int sida : {16, 32, 256}) {
                const std::uint8_t* e =
                    &ico[6 + static_cast<std::size_t>(post++) * 16];
                const std::uint32_t storlek = läsLe32(e + 8);
                const std::uint32_t offset  = läsLe32(e + 12);
                bool  winOk = offset + storlek <= ico.size();
                HICON h     = nullptr;
                if (winOk)
                    h = CreateIconFromResourceEx(
                        ico.data() + offset, storlek, TRUE,
                        0x00030000, sida, sida, LR_DEFAULTCOLOR);
                ICONINFO info{};
                BITMAP   bm{};
                winOk = h && GetIconInfo(h, &info) && info.hbmColor &&
                        GetObjectW(info.hbmColor, sizeof(bm), &bm) &&
                        bm.bmWidth == sida && bm.bmHeight == sida;
                if (info.hbmColor)
                    DeleteObject(info.hbmColor);
                if (info.hbmMask)
                    DeleteObject(info.hbmMask);
                if (h)
                    DestroyIcon(h);
                check(winOk, "D1: Windows läser " + std::to_string(sida) +
                                 "-ramen (CreateIconFromResourceEx)");
            }
        }
#endif
        std::vector<std::uint8_t> igen;
        std::string               err2;
        glzassets::writeIco({r16, r32, r256}, igen, err2);
        check(igen == ico, "D1: writeIco är deterministisk");
        std::vector<std::uint8_t> dubbel;
        check(!glzassets::writeIco({r16, r16}, dubbel, err2),
              "D1: två ramar med samma sida avvisas");
        check(!glzassets::writeIco({testbild(16, 8)}, dubbel, err2),
              "D1: en icke-kvadratisk ram avvisas");
    }

    // D2: .icns
    {
        std::vector<std::uint8_t> icns;
        std::string               err;
        const bool ok = glzassets::writeIcns({r16, r32, r128, r256}, icns, err);
        check(ok && icns.size() > 8 && std::memcmp(icns.data(), "icns", 4) == 0 &&
                  läsBe32(&icns[4]) == icns.size(),
              "D2: writeIcns ger 'icns' med rätt total längd", err);
        std::vector<std::string> typer;
        const std::uint8_t*      s8mk = nullptr;
        const std::uint8_t*      is32 = nullptr;
        std::uint32_t            is32n = 0;
        const std::uint8_t*      ic07 = nullptr;
        std::uint32_t            ic07n = 0;
        bool                     strukturOk = ok;
        for (std::size_t i = 8; strukturOk && i + 8 <= icns.size();) {
            const std::uint32_t n = läsBe32(&icns[i + 4]);
            if (n < 8 || i + n > icns.size()) {
                strukturOk = false;
                break;
            }
            const std::string typ(reinterpret_cast<const char*>(&icns[i]), 4);
            typer.push_back(typ);
            if (typ == "s8mk" && n == 8 + 256)
                s8mk = &icns[i + 8];
            if (typ == "is32") {
                is32  = &icns[i + 8];
                is32n = n - 8;
            }
            if (typ == "ic07") {
                ic07  = &icns[i + 8];
                ic07n = n - 8;
            }
            i += n;
        }
        check(strukturOk, "D2: elementen går jämnt upp i filen");
        auto har = [&](const char* t) {
            return std::find(typer.begin(), typer.end(), std::string(t)) !=
                   typer.end();
        };
        check(!typer.empty() && typer.front() == "TOC ",
              "D2: TOC står först");
        check(har("is32") && har("s8mk") && har("il32") && har("l8mk") &&
                  har("ic11") && har("ic07") && har("ic13") && har("ic08") &&
                  !har("ic12") && !har("ic09"),
              "D2: elementen per sida (16 ⇒ is32/s8mk, 32 ⇒ il32/l8mk/ic11, "
              "128 ⇒ ic07, 256 ⇒ ic13/ic08)");
        bool maskOk = s8mk != nullptr;
        for (int i = 0; maskOk && i < 256; ++i)
            maskOk = s8mk[i] == r16.px[static_cast<std::size_t>(i) * 4 + 3];
        check(maskOk, "D2: s8mk är 16-ramens alfakanal");
        // is32: Apples RLE per kanal (R-planet, G-planet, B-planet), packad
        // upp HÄR med grindens egen läsare — kontroll < 0x80 ⇒ c+1 literala
        // byte, annars nästa byte (c − 0x80 + 3) gånger.
        {
            std::vector<std::uint8_t> plan;
            std::uint32_t             i = 0;
            bool                      rleOk = is32 != nullptr;
            while (rleOk && i < is32n && plan.size() < 3 * 256) {
                const std::uint8_t c = is32[i++];
                if (c < 0x80) {
                    const std::uint32_t n = c + 1u;
                    rleOk = i + n <= is32n;
                    if (rleOk)
                        plan.insert(plan.end(), is32 + i, is32 + i + n);
                    i += n;
                } else {
                    rleOk = i < is32n;
                    if (rleOk)
                        plan.insert(plan.end(), c - 0x80u + 3u, is32[i]);
                    ++i;
                }
            }
            rleOk = rleOk && plan.size() == 3 * 256 && i == is32n;
            for (int k = 0; rleOk && k < 3; ++k)
                for (int p = 0; rleOk && p < 256; ++p)
                    rleOk = plan[static_cast<std::size_t>(k * 256 + p)] ==
                            r16.px[static_cast<std::size_t>(p) * 4 +
                                   static_cast<std::size_t>(k)];
            check(rleOk, "D2: is32-planen packas upp till 16-ramens R, G och B",
                  std::to_string(plan.size()));
        }
        glzassets::Rgba png;
        std::string     perr, var;
        check(ic07 && glzassets::decodePng(ic07, ic07n, png, perr) &&
                  sammaSynligt(png.px, r128.px, var),
              "D2: ic07 är en PNG med 128-ramens pixlar", perr + var);
        std::vector<std::uint8_t> fel;
        check(!glzassets::writeIcns({testbild(20, 20)}, fel, err),
              "D2: en sida utan icns-typ avvisas");
    }

    // D3: de committade filerna.
    {
        const std::string               bas = rot() + "Glaze/assets/icon/";
        const std::vector<std::uint8_t> ico = läsFil(bas + "glazeui.ico");
        bool ok = ico.size() > 6 && läsLe16(&ico[2]) == 1;
        const int antal = ok ? läsLe16(&ico[4]) : 0;
        check(ok && antal == static_cast<int>(std::size(glzassets::kIconSizes)),
              "D3: glazeui.ico bär en ram per kIconSizes",
              std::to_string(antal));
        bool sidorOk = ok && ico.size() >= 6 + static_cast<std::size_t>(antal) * 16;
        for (int i = 0; sidorOk && i < antal; ++i) {
            const int sida = ico[6 + static_cast<std::size_t>(i) * 16];
            sidorOk = (sida == 0 ? 256 : sida) ==
                      glzassets::kIconSizes[static_cast<std::size_t>(i)];
        }
        check(sidorOk, "D3: glazeui.ico:s ramar står i kIconSizes ordning");

        const std::vector<std::uint8_t> icns = läsFil(bas + "glazeui.icns");
        check(icns.size() > 8 && std::memcmp(icns.data(), "icns", 4) == 0 &&
                  läsBe32(&icns[4]) == icns.size(),
              "D3: glazeui.icns har rätt magi och längd");

        bool pngOk = true;
        std::string vilken;
        for (int n : {16, 22, 24, 32, 48, 64, 128, 256}) {
            const std::vector<std::uint8_t> fil =
                läsFil(bas + "glazeui-" + std::to_string(n) + ".png");
            glzassets::Rgba bild;
            std::string     err;
            if (!glzassets::decodePng(fil.data(), fil.size(), bild, err) ||
                bild.width != n || bild.height != n) {
                pngOk  = false;
                vilken = std::to_string(n) + " " + err;
                break;
            }
        }
        check(pngOk, "D3: glazeui-N.png är N×N", vilken);

        // DE COMMITTADE FILERNA ÄR VERKTYGETS UTDATA: kör `glazeassets icon`
        // på den committade framework_assets.bin och jämför byte för byte.
        // En ändrad skalare, kodare eller valregel utan omgenererade filer
        // fäller här — och verktyget är det RIKTIGA, inte en kopia i grinden.
        const std::filesystem::path ut =
            std::filesystem::temp_directory_path() / "icon_gate_d3";
        std::error_code ec;
        std::filesystem::remove_all(ut, ec);
        std::filesystem::create_directories(ut, ec);
        std::string kommando = std::string("\"") + GLAZE_ASSETS_CLI +
                               "\" icon \"" + rot() +
                               "Glaze/assets/framework_assets.bin\" "
                               "glaze:icon --small glaze:icon16 \"" +
                               ut.string() + "\" glazeui";
#ifdef _WIN32
        kommando = "\"" + kommando + "\"";   // cmd /c strippar ytterparet
#endif
        const int kod = std::system(kommando.c_str());
        check(kod == 0, "D3: glazeassets icon kör på framework_assets.bin",
              std::to_string(kod));
        std::vector<std::string> filer = {"glazeui.ico", "glazeui.icns"};
        for (int n : glzassets::kHicolorSizes)
            if (std::filesystem::exists(bas + "glazeui-" + std::to_string(n) +
                                        ".png") ||
                std::filesystem::exists(ut / ("glazeui-" + std::to_string(n) +
                                              ".png")))
                filer.push_back("glazeui-" + std::to_string(n) + ".png");
        for (const std::string& namn : filer) {
            const std::vector<std::uint8_t> nu  = läsFil((ut / namn).string());
            const std::vector<std::uint8_t> com = läsFil(bas + namn);
            check(!nu.empty() && nu == com,
                  "D3: " + namn + " är byte för byte verktygets utdata",
                  std::to_string(nu.size()) + " mot " +
                      std::to_string(com.size()));
        }
        std::filesystem::remove_all(ut, ec);
    }
}

// ------------------------------------------------------------ E inbäddningen

void delE()
{
    std::printf("\n=== E. Inbäddningen ===\n");
    const std::vector<std::uint8_t> fil =
        läsFil(rot() + "Glaze/assets/framework_assets.bin");
    const std::vector<std::uint8_t> inbäddad(
        glaze::kFrameworkAssetsBin,
        glaze::kFrameworkAssetsBin + glaze::kFrameworkAssetsBinSize);
    check(!fil.empty() && fil == inbäddad,
          "E1: kFrameworkAssetsBin är byte för byte framework_assets.bin",
          std::to_string(fil.size()) + " mot " +
              std::to_string(inbäddad.size()));

    glzassets::File f;
    std::string     err;
    const bool      öppen = f.openBytes(inbäddad, err);
    check(öppen, "E2: den inbäddade butiken öppnas", err);
    if (öppen) {
        for (const auto& [nyckel, sida] :
             {std::pair<const char*, int>{"icon", 256}, {"icon16", 16}}) {
            const glzassets::EntryView* e = f.find("glaze", nyckel);
            glzassets::Rgba             bild;
            std::string                 perr;
            const bool ok = e && glzassets::decodePng(f.data(*e), e->dataSize,
                                                      bild, perr);
            check(ok && bild.width == sida && bild.height == sida,
                  std::string("E2: glaze/") + nyckel + " är " +
                      std::to_string(sida) + "×" + std::to_string(sida),
                  perr);
        }
    }

    const glaze::AssetHandle före = glaze::detail::builtinAsset("glaze", "icon");
    check(före.valid() && (före.id & 0x80000000u) != 0,
          "E3: ramverkets handtag har bit 31 satt",
          std::to_string(före.id));
    check(!glaze::detail::builtinAsset("glaze", "saknas").valid(),
          "E3: en okänd nyckel ger ett ogiltigt handtag");
    const std::uint64_t gen = glaze::detail::assetGeneration();
    const bool laddad = glaze::loadAssets(rot() + "apps/demo/assets.bin");
    check(laddad && glaze::detail::assetGeneration() != gen,
          "E3: loadAssets stegar generationen");
    const glaze::AssetHandle app = glaze::img("toolbar", "gear");
    check(app.valid() && (app.id & 0x80000000u) == 0,
          "E3: appens handtag har INTE bit 31");
    const glaze::AssetHandle efter = glaze::detail::builtinAsset("glaze", "icon");
    const std::uint8_t* data = nullptr;
    std::size_t         n    = 0;
    check(efter == före && glaze::detail::assetBytes(efter, &data, &n) &&
              n > 8 && data[0] == 0x89,
          "E3: ramverkets handtag överlever loadAssets och routas till "
          "den inbäddade butiken");
}

// ---------------------------------------------------------------- F fasaden

void delF()
{
    std::printf("\n=== F. Fasaden ===\n");
    gfx::setAppId("");
    const std::string förval = gfx::appId();
    check(förval == "icon_gate",
          "F1: tom app_id = programfilens namn utan ändelse", förval);
    gfx::setAppId("se.glaze-ui_Demo.2");
    check(gfx::appId() == "se.glaze-ui_Demo.2",
          "F1: [A-Za-z0-9._-] godtas", gfx::appId());
    gfx::setAppId("fel id/");
    check(gfx::appId() == "se.glaze-ui_Demo.2",
          "F1: ett ogiltigt tecken ignoreras (det föregående gäller)",
          gfx::appId());
    gfx::setAppId(std::string(256, 'a'));
    check(gfx::appId() == "se.glaze-ui_Demo.2",
          "F1: mer än 255 tecken ignoreras", gfx::appId());
    gfx::setAppId(std::string(255, 'a'));
    check(gfx::appId() == std::string(255, 'a'),
          "F1: exakt 255 tecken godtas");
    gfx::setAppId("");
    check(gfx::appId() == förval, "F1: tom sträng återställer förvalet");

    // F2: ogiltiga ramar (fel längd på pixeldatat) väljs aldrig.
    gfx::WindowIcon ikon;
    gfx::IconImage  trasig;
    trasig.width  = 64;
    trasig.height = 64;
    trasig.rgba.assign(10, 0);
    gfx::IconImage hel;
    hel.width  = 16;
    hel.height = 16;
    hel.rgba.assign(16 * 16 * 4, 0);
    ikon.images = {trasig, hel};
    const gfx::IconImage* val = gfx::pickIconImage(ikon, 32);
    check(val == &ikon.images[1],
          "F2: pickIconImage hoppar över en ram med fel pixellängd");
    check(gfx::pickIconImage(gfx::WindowIcon{}, 16) == nullptr,
          "F2: tom lista ger nullptr");
}

// --------------------------------------------------- G AppIcon i AppWindow

class Vard : public glaze::AppWindow {
public:
    Vard() : glaze::AppWindow("icon_gate", 640, 400) {}
    using glaze::AppWindow::makeCtx;
};

void delG()
{
    std::printf("\n=== G. AppIcon och AppWindow ===\n");
    Vard w;
    glaze::AppIcon& ai = w.appIcon;
    // Fönstret ÖPPNAS (dolt): utan backendfönster finns varken font eller
    // skala, och makeCtx() är odefinierad — på Linux ett segfault.
    w.setOffscreen(true);
    w.show();
    if (!w.isOpen()) {
        check(false, "G: fönstret öppnas — ingen display?");
        return;
    }
    w.window().renderFrame();

    // G1: förvalet.
    check(ai.name == "appIcon", "G1: medlemmen heter appIcon", ai.name);
    check(ai.isVisible() && !ai.isNoIcon() && !ai.isLarge() &&
              !ai.icon().valid() && !ai.smallIcon().valid(),
          "G1: förvalet är synligt, liten, utan egen bild (= GlazeUI-ikonen)");
    check(!ai.focusable(), "G1: ikonen är inget tabbstopp");
    check(ai.parent() == &w.root(), "G1: ikonen är ett ROTbarn (chrome)");

    // G2: platsen.
    const glaze::Ctx c     = w.makeCtx();
    const float      liten = ai.reservedWidth(c, glaze::AppIcon::Row::Title);
    check(liten > 0.0f, "G2: förvalet reserverar plats i namnlisten",
          std::to_string(liten));
    ai.setNoIcon(true);
    check(ai.reservedWidth(c, glaze::AppIcon::Row::Title) == 0.0f &&
              ai.reservedWidth(c, glaze::AppIcon::Row::Menu) == 0.0f,
          "G2: noIcon ⇒ ingen plats");
    ai.setNoIcon(false);
    ai.hide();
    check(ai.reservedWidth(c, glaze::AppIcon::Row::Title) == 0.0f,
          "G2: dold ikon ⇒ ingen plats i fönstret");
    ai.show();
    check(ai.reservedWidth(c, glaze::AppIcon::Row::Title) == liten,
          "G2: visad igen ⇒ samma plats");

    // G3: den stora formen kräver en SYNLIG menyrad.
    ai.setLarge(true);
    check(ai.reservedWidth(c, glaze::AppIcon::Row::Title) == liten &&
              ai.reservedWidth(c, glaze::AppIcon::Row::Menu) == 0.0f,
          "G3: stor utan menyrad ⇒ den lilla platsen");
    w.menuBar.show();
    const float storTitel = ai.reservedWidth(c, glaze::AppIcon::Row::Title);
    const float storMeny  = ai.reservedWidth(c, glaze::AppIcon::Row::Menu);
    check(storTitel > liten,
          "G3: stor med menyrad ⇒ större plats i namnlisten",
          std::to_string(storTitel));
    if (!ai.onRightSide())
        check(storMeny > 0.0f && storMeny == storTitel,
              "G3: menyraden börjar BREDVID den stora ikonen",
              std::to_string(storMeny));
    ai.setLarge(false);
    check(ai.reservedWidth(c, glaze::AppIcon::Row::Menu) == 0.0f,
          "G3: liten med menyrad ⇒ menyraden rörs inte");
    w.menuBar.hide();

    // G4: sidan per plattform.
#ifdef __APPLE__
    check(ai.onRightSide(), "G4: macOS ⇒ höger");
#else
    check(!ai.onRightSide(), "G4: Windows/Linux ⇒ vänster");
#endif

    // G5: ingen meny ⇒ ingenting.
    ai.openMenu();
    check(!ai.isMenuOpen(), "G5: en tom programmeny öppnas inte");

    // G6: programmenyn i kollektions- och trädform är SAMMA tillstånd.
    ai.addItem({}, "Nytt", "Ctrl+N");
    ai.addSubmenu({}, "Exportera");
    ai.addItem("Exportera", "PDF");
    ai.menu().addItem("Avsluta");
    check(ai.menu().items.size() == 3 &&
              ai.itemAtPath({"Exportera", "PDF"}) != nullptr &&
              ai.itemAtPath("Avsluta") != nullptr,
          "G6: addItem({}, …) och menu().addItem(…) bygger samma meny",
          std::to_string(ai.menu().items.size()));

    // G7: acceleratorn i programmenyn — genvägen läses ur posten, så fallet
    // mäter utlösningen och inte genvägstolkningen.
    int        klick = 0;
    ai.setHandler("Nytt", [&klick] { ++klick; });
    const glaze::MenuItem* nytt = ai.itemAtPath("Nytt");
    const bool träff = nytt && nytt->shortcut.set() &&
                       ai.tryShortcut(nytt->shortcut.key, nytt->shortcut.mods);
    check(träff && klick == 1, "G7: tryShortcut kör postens handler",
          std::to_string(klick));
    check(nytt && !ai.tryShortcut(nytt->shortcut.key, 0),
          "G7: fel modifierare träffar inte");

    // G8: stigarna. Den TOMMA stigen är roten och giltig; en UNDERKÄND stig
    // (snedstreck i ett segment) är ogiltig och får ALDRIG landa i roten.
    check(glaze::MenuPath{}.empty() && glaze::MenuPath{}.valid(),
          "G8: MenuPath{} är den tomma, giltiga roten");
    check(!glaze::MenuPath("a/b").valid(),
          "G8: ett snedstreck i ett segment underkänner stigen");
    const std::size_t före = ai.menu().items.size();
    ai.addItem("a/b", "Fel");
    ai.addSeparator(glaze::MenuPath("x/y"));
    check(ai.menu().items.size() == före,
          "G8: en underkänd stig lämnar programmenyns rot ORÖRD",
          std::to_string(ai.menu().items.size()));

    // G9: OS-ikonens RAMAR (src/window_icon.h) — förvalet är ramverkets
    // inbäddade bilder, en ram per kIconSizes, och 16-ramen är den egna
    // lilla bilden OSKALAD (valregeln: minsta källa som räcker).
    const glaze::AssetHandle stor  = glaze::detail::defaultIconLarge();
    const glaze::AssetHandle lilla = glaze::detail::defaultIconSmall();
    check(stor == glaze::detail::builtinAsset("glaze", "icon") &&
              lilla == glaze::detail::builtinAsset("glaze", "icon16"),
          "G9: förvalet är ramverkets glaze/icon och glaze/icon16");
    const std::size_t                 poster = glaze::detail::iconFrameCacheSize();
    const std::vector<gfx::IconImage> ramar =
        glaze::detail::iconFrames(stor, lilla);
    bool sidorOk = ramar.size() == std::size(glzassets::kIconSizes);
    for (std::size_t i = 0; sidorOk && i < ramar.size(); ++i)
        sidorOk = ramar[i].width == glzassets::kIconSizes[i] &&
                  ramar[i].height == glzassets::kIconSizes[i] &&
                  ramar[i].rgba.size() ==
                      static_cast<std::size_t>(ramar[i].width) *
                          static_cast<std::size_t>(ramar[i].height) * 4u;
    check(sidorOk, "G9: en ram per kIconSizes, kvadratisk och hel",
          std::to_string(ramar.size()));
    const std::uint8_t* data = nullptr;
    std::size_t         n    = 0;
    std::vector<std::uint8_t> källa16;
    int                       w16 = 0, h16 = 0;
    const bool avkodad =
        glaze::detail::assetBytes(lilla, &data, &n) &&
        gfx::decodeImage(data, n, källa16, w16, h16);
    std::string var;
    check(avkodad && w16 == 16 && !ramar.empty() &&
              sammaSynligt(ramar[0].rgba, källa16, var),
          "G9: 16-ramen är den lilla bilden oskalad", var);
    // Ramarna svarar med VÄRDE (en Bitmaps ramar
    // cachas aldrig) — cachen mäts med räknaren: förvalets par låg redan där
    // eller kom till nu (fönstret lade sin ikon i `show()`), och en fråga
    // till lägger ingen post.
    const std::size_t                 efter = glaze::detail::iconFrameCacheSize();
    const std::vector<gfx::IconImage> igen  =
        glaze::detail::iconFrames(stor, lilla);
    bool sammaRamar = igen.size() == ramar.size();
    for (std::size_t i = 0; sammaRamar && i < igen.size(); ++i)
        sammaRamar = igen[i].rgba == ramar[i].rgba;
    check(efter <= poster + 1 &&
              glaze::detail::iconFrameCacheSize() == efter && sammaRamar,
          "G9: ramarna cachas (samma par en gång till: ingen ny post, samma "
          "ramar)",
          std::to_string(glaze::detail::iconFrameCacheSize()) + " poster");
}

// ---------------------------------- G10 bilder ur pixlar

// Renderer som SPELAR IN de bilder bildcachen skapar (måtten) och räknar de
// ritade — resten är NullRendererns no-op. `createImage` ger en bild med de
// mått den bads om, så cachen ser en giltig textur.
struct BildInspelning : gfx::NullRenderer {
    struct Bild final : gfx::NullImage {
        Bild(int w, int h) : w_(w), h_(h) {}
        int  width() const override { return w_; }
        int  height() const override { return h_; }
        bool valid() const override { return true; }
        int  w_, h_;
    };
    struct Mått {
        int w, h;
    };
    std::vector<Mått> skapade;
    int               ritade = 0;
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int w, int h, gfx::ImageFormat,
                                            const void*, std::size_t,
                                            bool) override
    {
        skapade.push_back({w, h});
        return std::make_unique<Bild>(w, h);
    }
    bool updateImage(gfx::Image&, int, int, int, int, const void*,
                     std::size_t) override
    {
        return true;
    }
    using gfx::NullRenderer::drawImage;
    void drawImage(gfx::Image&, const gfx::Rectf&, const gfx::Rectf&,
                   float) override
    {
        ++ritade;
    }
};

// En Bitmap ur grindens deterministiska testbild — samma form som
// `decodeImage` (RGBA8, rak alfa, rad 0 överst, täta rader).
glaze::Bitmap bitmapAv(const glzassets::Rgba& b)
{
    return glaze::Bitmap(b.width, b.height, b.px.data());
}

void delG10()
{
    std::printf("\n=== G10. bilder ur pixlar ===\n");
    namespace d = glaze::detail;

    // ---- BILDVÄRDET (B4): två ogiltiga är lika, en ny version är en annan
    //      bild för `==` men samma för `sameImage`, och storleksklassen är 0.
    check(glaze::ImageSource{} == glaze::ImageSource(glaze::AssetHandle{}),
          "G10: {} och AssetHandle{} är lika (två ogiltiga)");
    glaze::Bitmap bild = bitmapAv(testbild(40, 40));
    const glaze::ImageSource före(bild);
    check(före == glaze::ImageSource(bild) && före.sizeClass() == 0.0f &&
              bool(före),
          "G10: samma Bitmap i samma version är lika, klass 0, giltig");
    const glzassets::Rgba ny = enfärgad(40, 40, 10, 200, 30, 255);
    bild.setPixels(0, 0, 40, 40, ny.px.data());
    const glaze::ImageSource efter(bild);
    check(!(före == efter) && före.sameImage(efter),
          "G10: efter setPixels skiljer sig värdet (==) men är samma bild "
          "(sameImage)");

    // ---- OS-IKONEN ur en Bitmap: 16-ramen är fitSquare av de LEVANDE
    //      pixlarna, och programFirst är false (en egen bild).
    glaze::Bitmap   ikon = bitmapAv(testbild(64, 48));
    gfx::WindowIcon wi   = d::osIcon(glaze::ImageSource(ikon), {});
    const glzassets::Rgba facit16 =
        glzassets::fitSquare(testbild(64, 48), 16);
    check(!wi.programFirst && !wi.images.empty() &&
              wi.images[0].width == 16 && wi.images[0].rgba == facit16.px,
          "G10: osIcon(bitmap) — 16-ramen är fitSquare(pixlarna, 16)",
          std::to_string(wi.images.size()) + " ramar");
    check(wi.images.size() == std::size(glzassets::kIconSizes),
          "G10: en ram per kIconSizes ur en bild på 64 px",
          std::to_string(wi.images.size()));
    const std::size_t poster = d::iconFrameCacheSize();
    const glzassets::Rgba blå = enfärgad(64, 48, 20, 40, 220, 255);
    ikon.setPixels(0, 0, 64, 48, blå.px.data());
    const gfx::WindowIcon wi2 = d::osIcon(glaze::ImageSource(ikon), {});
    check(!wi2.images.empty() &&
              wi2.images[0].rgba == glzassets::fitSquare(blå, 16).px &&
              wi2.images[0].rgba != wi.images[0].rgba,
          "G10: setPixels följt av en ny osIcon ger en NY 16-ram");
    check(d::iconFrameCacheSize() == poster,
          "G10: ramar ur en Bitmap cachas aldrig (processcachen växer inte)",
          std::to_string(d::iconFrameCacheSize() - poster));
    const glaze::RenderTexture yta(32, 32);
    check(d::osIcon(glaze::ImageSource(yta), {}).images.empty(),
          "G10: en RenderTexture ger ingen ram (avvisad med logg)");

    // ---- PROGRAMIKONEN ritar en Bitmap via glyphResampled: bildcachen
    //      räknar om den till RUTANS fysiska storlek och laddar upp den.
    {
        BildInspelning       rec;
        glaze::ImageCache    bilder(rec);
        glaze::AppIcon       ai;   // fristående: namnlistens kvadrat
        glaze::Bitmap        pix = bitmapAv(testbild(96, 96));
        ai.setIcon(pix);
        const glaze::Style&  st = glaze::defaultStyle(glaze::UI::Slate);
        glaze::Ctx           c{};
        c.r      = &rec;
        c.scale  = 2.0f;
        c.ui     = glaze::UI::Slate;
        c.style  = &st;
        c.theme  = &glaze::Theme::slateDefault().light;
        c.images = &bilder;
        bilder.trim(std::size_t(1) << 28, std::size_t(1) << 26);
        const float sida = st.barHeight;   // fristående AppIcon (app_icon.cpp)
        c.box  = gfx::Rectf{0.0f, 0.0f, sida * c.scale, sida * c.scale};
        c.area = c.box;
        ai.render(c);
        const int fysisk = static_cast<int>(
            std::lround((sida - 2.0f * st.appIconInset) * c.scale));
        check(rec.skapade.size() == 1 && rec.skapade[0].w == fysisk &&
                  rec.skapade[0].h == fysisk && rec.ritade == 1,
              "G10: AppIcon med Bitmap — EN omräknad bild i rutans fysiska "
              "storlek, ritad en gång",
              rec.skapade.empty()
                  ? std::string("ingen")
                  : std::to_string(rec.skapade[0].w) + " mot " +
                        std::to_string(fysisk));
    }

    // ---- BUDGETEN: de omräknade bilderna räknas i bytebokföringen,
    //      uppladdningsbudgeten och LRU:n — också för SAMMA Bitmap i två
    //      fönster (två cacher) med återkommande storleksbyten.
    {
        BildInspelning     rec1, rec2;
        glaze::ImageCache  f1(rec1), f2(rec2);
        glaze::Bitmap      pix = bitmapAv(testbild(128, 128));
        glaze::ImageSource k(pix);
        // Budgeten rymmer 48 och 32 men inte alla fyra storlekarna (16², 24²,
        // 32², 48² × 4 byte = 16 640) — LRU:n måste vräka.
        const std::size_t budget = std::size_t(48 * 48 + 32 * 32) * 4;
        bool hålls = true;
        for (const int s : {16, 24, 32, 48, 16, 24, 32, 48}) {
            for (glaze::ImageCache* f : {&f1, &f2}) {
                f->trim(budget, std::size_t(1) << 26);
                hålls = hålls && f->canvasBytes() <= budget;
                hålls = hålls && f->getResampled(k, s, s) != nullptr;
            }
        }
        f1.trim(budget, std::size_t(1) << 26);
        f2.trim(budget, std::size_t(1) << 26);
        check(hålls && f1.canvasBytes() <= budget && f2.canvasBytes() <= budget,
              "G10: samma Bitmap i två fönster, storlekar som växlar — "
              "budgeten hålls efter varje trim",
              std::to_string(f1.canvasBytes()) + "/" +
                  std::to_string(f2.canvasBytes()));
        check(f1.canvasCount() < 4 && f2.canvasCount() < 4,
              "G10: LRU:n vräker gamla storlekar (alla fyra ryms inte)",
              std::to_string(f1.canvasCount()));
        // Uppladdningsbudgeten: framens första går igenom, nästa NYA storlek
        // väntar på nästa frame.
        f1.trim(budget, 64);
        const bool första = f1.getResampled(k, 20, 20) != nullptr;
        const bool andra  = f1.getResampled(k, 40, 40) != nullptr;
        check(första && !andra && f1.uploadsDeferred(),
              "G10: uppladdningsbudgeten — den andra nya storleken väntar");
        // En död bild släpps i nästa trim — sista handtaget är borta.
        pix = glaze::Bitmap{};
        k   = glaze::ImageSource{};
        f1.trim(budget, std::size_t(1) << 26);
        f2.trim(budget, std::size_t(1) << 26);
        check(f1.canvasCount() == 0 && f2.canvasCount() == 0 &&
                  f1.canvasBytes() == 0 && f2.canvasBytes() == 0,
              "G10: en död Bitmap släpps i nästa trim — i båda fönstren",
              std::to_string(f1.canvasCount()));
    }
}

// ------------------------------------ G11 en Bitmap utan pixlar
//
// En Bitmap med `setKeepPixels(false)`, uppladdad i ett fönster och släppt av
// pumpens `releaseQueuedPixels`: ingen OS-ikon kan göras av den, ett fönster
// utan omräkning får ingen, och `Draw::glyphResampled` ritar den med GPU:ns
// filter (`glyph` — fönstrets fulla textur) i stället för ingenting.
// Sabotaget som ska fälla den: `avkoda` utan vakten (läser nullptr) och
// glyphResampled utan reservvägen (ingen drawImage).
void delG11()
{
    std::printf("\n=== G11. en Bitmap utan pixlar ===\n");
    namespace d = glaze::detail;
    BildInspelning    rec;
    glaze::ImageCache fönster(rec), nytt(rec);
    glaze::Bitmap     pix = bitmapAv(testbild(64, 64));
    pix.setKeepPixels(false);
    fönster.trim(std::size_t(1) << 28, std::size_t(1) << 26);
    const bool uppe = fönster.get(pix) != nullptr;
    d::releaseQueuedPixels();
    check(uppe && pix.pixels() == nullptr && pix.valid(),
          "G11: (fixturen) bilden är uppladdad och har släppt sina pixlar");
    check(d::osIcon(glaze::ImageSource(pix), {}).images.empty(),
          "G11: en Bitmap utan pixlar ger ingen OS-ikon (inga ramar)");
    nytt.trim(std::size_t(1) << 28, std::size_t(1) << 26);
    check(nytt.getResampled(glaze::ImageSource(pix), 16, 16) == nullptr,
          "G11: ett fönster utan omräkning får ingen omräknad bild");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    glaze::Ctx          c{};
    c.r      = &rec;
    c.scale  = 1.0f;
    c.ui     = glaze::UI::Slate;
    c.style  = &st;
    c.theme  = &glaze::Theme::slateDefault().light;
    c.images = &fönster;
    c.box    = gfx::Rectf{0.0f, 0.0f, 32.0f, 32.0f};
    c.area   = c.box;
    rec.ritade = 0;
    glaze::Draw::glyphResampled(c, gfx::Rectf{0.0f, 0.0f, 16.0f, 16.0f},
                                glaze::ImageSource(pix), 1.0f);
    check(rec.ritade == 1,
          "G11: glyphResampled ritar bilden utan pixlar genom glyph (fönstrets "
          "textur)",
          std::to_string(rec.ritade) + " ritade");
}

// ---------------------- Knappens ikonstorlek
//
// Q1: en Button med en Bitmap ritar sin bild i `iconFallback`-rutan utan
// `setIconSize` och i en 32-ruta med `setIconSize(32)`; med en asset är AUTO
// storleksklassen (16) och den satta storleken överstyr. Ritrutan läses ur en
// inspelande renderer — inte ur knappens geometri. SABOTAGE: `content()` som
// ignorerar `iconSize_` (Q1 — rutan står kvar i AUTO). MÄTER INTE skärpan
// (GPU:ns filter skalar). Efter delE: demons assets.bin är laddad.
void delReq3()
{
    std::printf("\n=== knappens ikonstorlek ===\n");
    struct Rutor : BildInspelning {
        std::vector<gfx::Rectf> mål;
        // En ASSET laddas med `loadImage(bytes)` (ImageCache::get(handle)), och
        // NullRenderers svar är en OGILTIG NullImage — cachen gör den till en
        // negativ post och knappen ritar ingenting. Grinden mäter RITRUTAN, så
        // en giltig bild (måtten spelar ingen roll) är fixturens sak; utan den
        // föll asset-fallet med 0 / 0 på varje plattform.
        using BildInspelning::loadImage;
        std::unique_ptr<gfx::Image> loadImage(const void*, std::size_t) override
        {
            return std::make_unique<Bild>(16, 16);
        }
        using BildInspelning::drawImage;
        void drawImage(gfx::Image&, const gfx::Rectf&, const gfx::Rectf& dst,
                       float) override
        {
            mål.push_back(dst);
            ++ritade;
        }
    };
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const auto rita = [&](glaze::Button& b) -> gfx::Rectf {
        Rutor             rec;
        glaze::ImageCache bilder(rec);
        gfx::NullFont     font;
        glaze::Ctx        c{};
        c.r      = &rec;
        c.font   = &font;
        c.scale  = 1.0f;
        c.ui     = glaze::UI::Slate;
        c.style  = &st;
        c.theme  = &glaze::Theme::slateDefault().light;
        c.images = &bilder;
        c.area   = gfx::Rectf{0.0f, 0.0f, 300.0f, 200.0f};
        c.box    = b.resolve(c);
        b.render(c);
        return rec.mål.empty() ? gfx::Rectf{} : rec.mål.back();
    };
    glaze::Button pix;
    pix.setImg(bitmapAv(testbild(40, 40)));
    const gfx::Rectf auto1 = rita(pix);
    pix.setIconSize(32.0f);
    const gfx::Rectf satt = rita(pix);
    check(std::abs(auto1.w - st.iconFallback) < 0.01f &&
              std::abs(satt.w - 32.0f) < 0.01f && std::abs(satt.h - 32.0f) < 0.01f,
          "Q1: Bitmap — iconFallback utan anropet, 32-rutan med setIconSize(32)",
          std::to_string(auto1.w) + " / " + std::to_string(satt.w));
    const glaze::AssetHandle ok = glaze::img("toolbar", "ok");
    if (ok.valid()) {
        glaze::Button tillgång;
        tillgång.setImg(ok);
        const gfx::Rectf klass = rita(tillgång);
        tillgång.setIconSize(32.0f);
        const gfx::Rectf över = rita(tillgång);
        check(std::abs(klass.w - glaze::ImageSource(ok).sizeClass()) < 0.01f &&
                  std::abs(över.w - 32.0f) < 0.01f,
              "Q1: asset — storleksklassen som AUTO, den satta storleken "
              "överstyr",
              std::to_string(klass.w) + " / " + std::to_string(över.w));
    } else {
        check(false, "Q1: demons assets.bin bär toolbar/ok");
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("icon_gate — programikonen\n");
    delA();
    delB();
    delC();
    delD();
    delE();   // laddar demons assets.bin (E3) — efter de rena delarna
    delF();
    delG();
    delG10();   // efter G: ritar med en egen renderer och egna cacher
    delG11();
    delReq3();   // efter E: demons assets.bin är laddad

    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "ICON_GATE GRÖN" : "ICON_GATE FÄLLD", g_cases,
                g_fails);
    return g_fails == 0 ? 0 : 1;
}
