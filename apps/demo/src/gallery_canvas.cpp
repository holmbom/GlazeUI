// GALLERIETS RITYTA — fliken "Rityta". Fyra delar, och
// var och en visar något ritytan KAN:
//
//   karta         en SCEN MED KAMERA i SWEREF 99 TM (meter, y uppåt):
//                 plattor ur pixlar, ytor med hål och gradient, vägar i mm
//                 med streck och alla tre skarvarna, etiketter med gloria,
//                 kollision och kartvridning, markörer (också med bild), en
//                 bild på FYRA FRIA HÖRN (parkens sneda översikt — rutorna
//                 förblir raka) och en etikett i DELAR (adressens nummer i
//                 fetstil och vägens färg) — och
//                 alla fyra gesterna påslagna, också styrplattans (
//                 två fingrar panorerar, nyper och vrider). En SKALSTOCK nere
//                 till vänster i overlay-lagret mäter där den står (också
//                 under tilt), en vallista uppe till höger väljer VRIDSNÄPPET,
//                 en LAGERLISTA uppe till vänster flyttar lagren med ▲ och ▼
//                 (`moveLayer` — inget byggs om), och ett klick
//                 på en form skriver träffprovet i statusraden.
//   graf          y uppåt, rutnätet och axlarnas tal (`onPaint`), ett
//                 GUMMIBAND i overlay-lagret (Skift+drag zoomar till rutan)
//                 och koordinaterna under pekaren.
//   inaktiverad   samma graf med `disable()` — innehållet tonas.
//   linjalprovet  en linje som ska mäta 100 mm, dragen eller stegad tills den
//                 stämmer med en linjal och sparad med
//                 `App::setDisplayCalibration`.
//
// FÄRGERNA: kartans och kurvornas är DATA och följer inte temat (en karta har
// sina egna färger — glaze/canvas/shape.h). Det som är GRÄNSSNITT på ytan —
// skalstocken, axlarnas tal, gummibandet, grafens koordinater och linjalen —
// tar fönstrets tema, så det följer ljust och mörkt.
//
// REGEL 8: varje FORM här går att skriva i `.glazeui` precis som i koden
// (`karta.addShape(2, glaze::Shape::polyline(…));` — se grupp 25 i
// tests/fixtures/gallery.glazeui). Det som inte gör det är det som räknas
// fram i drift — plattornas och nålens pixlar, loopen som bygger formerna —
// och handlerna, som previewn aldrig kör.

#include "widget_gallery.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = std::numbers::pi;

// Kartans mitt i SWEREF 99 TM (meter) — Stockholm. DOUBLE hela vägen: en
// koordinat på 6,6 miljoner meter får inte avrundas förrän ritytan gjort den
// relativ ett chunk-origo.
constexpr double kMittX = 674000.0;
constexpr double kMittY = 6580000.0;

// En kartpunkt i meter från mitten.
glaze::Point P(double dx, double dy)
{
    return glaze::Point{kMittX + dx, kMittY + dy};
}

// Kartans lager, nedifrån och upp.
constexpr int kPlattLager   = 0;
constexpr int kYtLager      = 1;
constexpr int kVagLager     = 2;
constexpr int kEtikettLager = 3;

// VRIDSNÄPPETS steg i vallistans ordning — av, bara norr, kvartsvarv och
// åttondelar (`Canvas::setRotationSnap`) — och förvalet: kvartsvarven.
constexpr float kSnappSteg[] = {0.0f, 360.0f, 90.0f, 45.0f};
constexpr int   kSnappForval = 2;

// Grafens data — rutan den startar i och som dubbelklicket visar igen. Den
// börjar till VÄNSTER om y-axeln, så axelns tal (som står till vänster om
// den) inte klipps av ytans kant.
constexpr glaze::Rect kGrafYta{-1.5, -3.2, 21.5, 6.4};

// Linjalens början och baslinje i ritytans LOKALA px (ingen kamera).
constexpr double kLinjalX = 12.0;
constexpr double kLinjalY = 52.0;

// KALIBRERINGENS SPANN — 50–800 dpi, det `gfx::setDisplayCalibration`
// godtar (platform.h), som px per mm: ett drag ska stanna vid gränsen i
// stället för att ge ett tal som avvisas.
constexpr double kMinPxPerMm = 50.0 / 25.4;
constexpr double kMaxPxPerMm = 800.0 / 25.4;
// Ett steg är EN fysisk pixel på linjens 100 mm.
constexpr double kKalSteg = 0.01;

// VARIFRÅN skärmens mm kommer — det är en del av svaret (gfx/window.h).
std::uint32_t mmKalla(gfx::DisplaySource s)
{
    switch (s) {
    case gfx::DisplaySource::Measured:   return demo::str::MmUppmatt;
    case gfx::DisplaySource::Calibrated: return demo::str::MmKalibrerad;
    case gfx::DisplaySource::Assumed:    break;
    }
    return demo::str::MmAntagen;
}

// Det minsta av 1, 2, 5, 10, 20 … (gånger en tiopotens) som är minst
// `minst` — rutnätets egen följd (src/canvas/scene.cpp `gridSpacing`).
double steg125(double minst)
{
    if (!std::isfinite(minst) || !(minst > 0.0))
        return 1.0;
    double dekad = std::pow(10.0, std::floor(std::log10(minst)));
    for (;;) {
        for (const double s : {1.0, 2.0, 5.0})
            if (s * dekad >= minst)
                return s * dekad;
        dekad *= 10.0;
    }
}

// Talet i 1-2-5-följden som ligger NÄRMAST `mal` räknat i kvoter (log-skala)
// — skalstockens längd: aldrig mer än en faktor √2,5 från målet.
double narmast125(double mal)
{
    if (!std::isfinite(mal) || !(mal > 0.0))
        return 1.0;
    const double dekad = std::pow(10.0, std::floor(std::log10(mal)));
    double       bast = dekad, fel = HUGE_VAL;
    for (const double s : {1.0, 2.0, 5.0, 10.0}) {
        const double f = std::abs(std::log(s * dekad / mal));
        if (f < fel) {
            fel  = f;
            bast = s * dekad;
        }
    }
    return bast;
}

// Ett 1-2-5-tal i meter som stockens etikett: "50 cm", "200 m", "2 km".
// Enheterna är tecken och översätts inte.
std::string meter(double v)
{
    if (v >= 1000.0)
        return std::to_string(std::lround(v / 1000.0)) + " km";
    if (v >= 1.0)
        return std::to_string(std::lround(v)) + " m";
    return std::to_string(std::lround(v * 100.0)) + " cm";
}

// ---------------------------------------------------------- plattorna

// VÄRDEBRUS — ett slumptal per heltalspunkt, mjukt interpolerat emellan.
// Deterministiskt: samma karta på varje plattform och i varje körning.
float gitter(int x, int y)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}

float brus(double x, double y)
{
    const double fx = std::floor(x), fy = std::floor(y);
    const int    ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float  tx = static_cast<float>(x - fx), ty = static_cast<float>(y - fy);
    const float  sx = tx * tx * (3.0f - 2.0f * tx);
    const float  sy = ty * ty * (3.0f - 2.0f * ty);
    const float  a = gitter(ix, iy), b = gitter(ix + 1, iy);
    const float  c = gitter(ix, iy + 1), d = gitter(ix + 1, iy + 1);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

std::uint8_t kanal(float v)
{
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

// EN KARTPLATTA ur pixlar — det en app hade hämtat ur en plattserver och
// avkodat med `Bitmap::decode`. Här räknas terrängen fram (ängar och skog ur
// brus), så demon inte bär på bilder. (dx, dy) är plattans sydvästra hörn i
// meter från kartans mitt; pixlarna ligger RÄTTVÄNDA, rad 0 är plattans
// NORRA kant (`Shape::image` med y uppåt).
glaze::Bitmap platta(double dx, double dy, double meter)
{
    constexpr int px = 128;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(px) * px * 4);
    const double steg = meter / px;
    std::size_t  i    = 0;
    for (int r = 0; r < px; ++r) {
        const double v = dy + meter - (r + 0.5) * steg;
        for (int k = 0; k < px; ++k) {
            const double u = dx + (k + 0.5) * steg;
            const float  n = 0.65f * brus(u / 70.0, v / 70.0) +
                            0.35f * brus(u / 23.0, v / 23.0);
            const float  skog = std::clamp((n - 0.48f) / 0.12f, 0.0f, 1.0f);
            const float  korn = (brus(u / 4.0, v / 4.0) - 0.5f) * 10.0f;
            rgba[i++] = kanal(236.0f + (196.0f - 236.0f) * skog + korn);
            rgba[i++] = kanal(239.0f + (221.0f - 239.0f) * skog + korn);
            rgba[i++] = kanal(220.0f + (177.0f - 220.0f) * skog + korn);
            rgba[i++] = 255;
        }
    }
    return glaze::Bitmap(px, px, rgba.data());
}

// PARKENS ÖVERSIKT — ett "flygfoto" ur pixlar: ett räknat rutmönster i
// parkens två gröna, som kartan lägger på FYRA FRIA HÖRN snett över parken
// (`Shape::image(öv, öh, nh, nv, …)`). Avbildningen är projektiv, så rutorna
// förblir RAKA också där fyrhörningens diagonal går — det är beviset.
glaze::Bitmap rutor()
{
    constexpr int    N = 64, R = 8;
    const gfx::Color mörk{62, 125, 50}, ljus{205, 232, 176};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(N) * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const gfx::Color c = (x / R + y / R) % 2 ? mörk : ljus;
            std::uint8_t*    p = &rgba[(static_cast<std::size_t>(y) * N + x) * 4];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    return glaze::Bitmap(N, N, rgba.data());
}

// KARTNÅLEN — en bildmarkör ur pixlar (`MarkerStyle::img` tar en Bitmap lika
// gärna som en asset): en röd droppe med vit prick, kantutjämnad med 4 × 4
// delprov per pixel. Spetsen sitter nederst mitt på, så ankaret Bottom ställer
// den PÅ punkten.
glaze::Bitmap nal()
{
    constexpr int   W = 24, H = 34;
    constexpr float cx = 12.0f, cy = 11.0f, r = 10.0f, spets = 33.0f;
    // Droppen: en skiva och en kon ned mot spetsen. `krymp` drar in kanten
    // (konturens insida).
    const auto inne = [&](float x, float y, float krymp) {
        const float rr = r - krymp;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr * rr)
            return true;
        const float s = spets - 2.0f * krymp;
        if (y < cy || y > s)
            return false;
        return std::abs(x - cx) <= rr * (s - y) / (s - cy);
    };
    const gfx::Color kropp{217, 48, 37}, kant{140, 24, 18}, prick{255, 255, 255};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4);
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            float sr = 0.0f, sg = 0.0f, sb = 0.0f, n = 0.0f;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    const float x = static_cast<float>(px) +
                                    (static_cast<float>(i) + 0.5f) / 4.0f;
                    const float y = static_cast<float>(py) +
                                    (static_cast<float>(j) + 0.5f) / 4.0f;
                    gfx::Color c;
                    if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= 3.6f * 3.6f)
                        c = prick;
                    else if (inne(x, y, 1.5f))
                        c = kropp;
                    else if (inne(x, y, 0.0f))
                        c = kant;
                    else
                        continue;
                    sr += c.r;
                    sg += c.g;
                    sb += c.b;
                    n += 1.0f;
                }
            std::uint8_t* p = &rgba[(static_cast<std::size_t>(py) * W + px) * 4];
            if (n > 0.0f) {   // rak alfa: färgen är delprovens medel
                p[0] = kanal(sr / n);
                p[1] = kanal(sg / n);
                p[2] = kanal(sb / n);
            }
            p[3] = kanal(255.0f * n / 16.0f);
        }
    return glaze::Bitmap(W, H, rgba.data());
}

// ------------------------------------------------------------- grafen

// GRAFENS FORMER — axlarna och två kurvor, den ena streckad och den andra
// med provpunkter. Samma lista ritas i den aktiva grafen och i den
// inaktiverade kopian.
std::vector<glaze::Shape> grafFormer()
{
    const gfx::Color axel{138, 138, 138};
    const gfx::Color bla{61, 139, 253};
    const gfx::Color orange{240, 140, 46};
    std::vector<glaze::Point> dampad, cosinus, prov;
    for (int i = 0; i <= 400; ++i) {
        const double x = i * 0.05;
        dampad.push_back({x, 3.0 * std::exp(-x / 9.0) * std::sin(1.4 * x)});
        cosinus.push_back({x, 2.2 * std::exp(-x / 14.0) * std::cos(0.7 * x)});
    }
    for (std::size_t i = 0; i <= 400; i += 20)
        prov.push_back(dampad[i]);   // x = 0, 1, 2 … 20

    std::vector<glaze::Shape> f;
    f.push_back(glaze::Shape::line({-1000, 0}, {1000, 0}, {.paint = axel}));
    f.push_back(glaze::Shape::line({0, -1000}, {0, 1000}, {.paint = axel}));
    f.push_back(glaze::Shape::polyline(
        cosinus, {.paint = orange, .width = 2, .dash = {6, 4}}));
    f.push_back(glaze::Shape::polyline(dampad, {.paint = bla, .width = 2}));
    f.push_back(glaze::Shape::points(prov, {.size = 6, .fill = bla}));
    return f;
}

} // namespace

// ------------------------------------------------------------------ bygget

void WidgetGallery::ritytaFlik(glaze::TabPage* sida)
{
    // SIDAN: kartan och grafen i den övre raden, linjalprovet över HELA
    // bredden under — 100 mm är upp till ~740 logiska px på en tät skärm vid
    // 100 % skala och ryms inte i en halv sida. Underkanten (336 + 160 = 496)
    // är g3:s, så fönstrets mått gäller oförändrade.

    // === 25 · Canvas — karta ============================================
    {
        auto* g = grupp(sida, demo::str::G25Karta, "g25k", 12, 12, 620, 312);

        auto* k      = g->add<glaze::Canvas>();
        k->x         = 8;
        k->anchorTop = {"g25kSep", 10};
        k->anchors   = {true, true, true, false};
        k->right     = 8;
        k->height    = 233;
        k->name      = "karta";

        // KAMERAN: världen är SWEREF 99 TM i meter med y uppåt. Skalan
        // 1:4000 räknas ur skärmens mm — den väntar till första ritningen,
        // när fönstret vet vilken skärm det står på — så kartan visar MER av
        // sig på en gles skärm än på en tät: en centimeter är 40 meter på
        // båda. Y-riktningen sätts FÖRE formerna, för bilderna bakar in den
        // i sin geometri.
        k->setYUp(true);
        k->setCenter(kMittX, kMittY);
        k->setMapScale(4000);
        k->setZoomRange(0.2, 40);
        // ALLA FYRA gesterna (de är av från start): dra, hjulet och
        // Ctrl+drag som vrider och lutar.
        k->setPannable(true);
        k->setZoomable(true);
        k->setRotatable(true);
        k->setPitchable(true);
        // Etiketterna TONAR när kollisionen visar eller döljer dem.
        k->setLabelFade(true);

        // LAGREN följer kollektionsvokabulären, och namnen är id — de byter
        // språk med resten av appen (statusraden skriver dem).
        k->setLayers({glaze::tr(demo::str::LagerPlattor),
                      glaze::tr(demo::str::LagerYtor),
                      glaze::tr(demo::str::LagerVagar),
                      glaze::tr(demo::str::LagerEtiketter)});
        // Lagerlistans spegling (nedan): plats → det lager namnen hör till.
        kartOrdning_ = {kPlattLager, kYtLager, kVagLager, kEtikettLager};
        // Ytornas lager ritas PER SLAG (`DrawOrder::ByKind`): alla
        // fyllningar och sedan alla kanter. I indexordning hade varje hus
        // med kontur bytt program två gånger — ett rit-anrop per byte — och
        // kanterna ska ändå ligga ovanpå. Vägarna står kvar i indexordning:
        // där ÄR ordningen innehållet (kanterna under fyllningarna).
        k->setLayerDrawOrder(kYtLager, glaze::DrawOrder::ByKind);
        kartNamn_.assign(4, {});
        const auto lagg = [&](int lager, glaze::Shape f, glaze::TrText namn) {
            k->addShape(lager, std::move(f));
            kartNamn_[static_cast<std::size_t>(lager)].push_back(std::move(namn));
        };

        // PLATTORNA — 6 × 4 bilder ur pixlar, 128 m och 128 px var. Varje
        // platta är en egen textur och ett eget rit-anrop, uppladdad EN gång:
        // en panorering, zoom eller vridning laddar aldrig upp dem igen.
        // Plattorna BEHÅLLER sina pixlar (inget `setKeepPixels(false)`):
        // galleriet stängs och öppnas igen ur samma objekt, och ett nytt
        // fönster kan inte ladda upp en bild som släppt dem — plattorna hade
        // varit tomma vid andra öppningen.
        constexpr int    kKol = 6, kRad = 4;
        constexpr double kSida = 128.0;
        for (int j = 0; j < kRad; ++j)
            for (int i = 0; i < kKol; ++i) {
                const double      dx = (i - kKol / 2) * kSida;
                const double      dy = (j - kRad / 2) * kSida;
                const glaze::Point h = P(dx, dy);
                lagg(kPlattLager,
                     glaze::Shape::image({h.x, h.y, kSida, kSida},
                                         platta(dx, dy, kSida)),
                     std::to_string(i) + "/" + std::to_string(j));
            }

        // YTORNA. Sjön är en polygon med ett HÅL (ön) och en RADIELL
        // gradient från djupet mot stranden; parken en LINJÄR gradient med
        // streckad kant. Gradienterna ligger fast i världen och följer zoom,
        // vridning och tilt.
        std::vector<glaze::Point> strand, holme;
        for (int i = 0; i < 48; ++i) {
            const double a = i * 2.0 * kPi / 48.0;
            const double r = 45.0 + 6.0 * std::sin(3.0 * a) + 3.0 * std::cos(5.0 * a + 1.0);
            strand.push_back(P(-105.0 + r * std::cos(a), 25.0 + r * std::sin(a)));
        }
        for (int i = 0; i < 16; ++i) {
            const double a = i * 2.0 * kPi / 16.0;
            const double r = 9.0 + 2.0 * std::sin(2.0 * a);
            holme.push_back(P(-95.0 + r * std::cos(a), 35.0 + r * std::sin(a)));
        }
        lagg(kYtLager,
             glaze::Shape::polygon(
                 std::vector<std::vector<glaze::Point>>{strand, holme},
                 glaze::Paint::radial(P(-105, 25), 0, 50,
                                      {{0.0f, gfx::Color{52, 120, 186}},
                                       {1.0f, gfx::Color{150, 200, 236}}}),
                 glaze::Stroke{.paint = gfx::Color{92, 150, 205}}),
             "Lillsjön");
        lagg(kYtLager,
             glaze::Shape::polygon(
                 {P(95, 70), P(185, 62), P(195, 10), P(140, 4), P(100, 12)},
                 glaze::Paint::linear(P(95, 70), P(195, 4),
                                      {{0.0f, gfx::Color{205, 232, 176}},
                                       {1.0f, gfx::Color{150, 200, 120}}}),
                 glaze::Stroke{.paint = gfx::Color{96, 140, 72},
                               .width = 1.5f,
                               .dash  = {6, 3}}),
             "Ekparken");
        // Ekarna är ETT punktmoln — en form, ett rit-anrop. Fröet är fast,
        // så parken ser likadan ut varje gång.
        std::vector<glaze::Point> ekar;
        std::uint32_t frö = 2026;
        const auto slump = [&frö] {
            frö = frö * 1664525u + 1013904223u;
            return static_cast<double>(frö >> 8) / static_cast<double>(1u << 24);
        };
        for (int i = 0; i < 30; ++i) {
            const double u = slump();
            const double v = slump();
            ekar.push_back(P(110.0 + 70.0 * u, 18.0 + 40.0 * v));
        }
        lagg(kYtLager,
             glaze::Shape::points(ekar, {.size         = glaze::mm(1.6f),
                                         .fill         = gfx::Color{62, 125, 50},
                                         .outline      = gfx::Color{40, 90, 32},
                                         .outlineWidth = 1}),
             "Ekar");
        // Husen, och kyrkan som en korsformad polygon.
        const gfx::Color hus{217, 201, 182}, husKant{156, 138, 116};
        struct Hus {
            const char* namn;
            glaze::Rect r;   // meter från mitten
        };
        const Hus husen[] = {Hus{"Skolan", {-205, -92, 34, 24}},
                             Hus{"Biblioteket", {-160, -90, 44, 26}},
                             Hus{"Posten", {-105, -84, 30, 22}},
                             Hus{"Torghuset", {-60, -80, 36, 24}},
                             Hus{"Vårdcentralen", {100, -52, 40, 26}},
                             Hus{"Stationen", {155, -50, 32, 24}}};
        for (const Hus& h : husen)
            lagg(kYtLager,
                 glaze::Shape::rect({kMittX + h.r.x, kMittY + h.r.y, h.r.w, h.r.h},
                                    hus, glaze::Stroke{.paint = husKant}),
                 h.namn);
        lagg(kYtLager,
             glaze::Shape::polygon({P(-16, 8), P(-4, 8), P(-4, 30), P(6, 30),
                                    P(6, 40), P(-4, 40), P(-4, 52), P(-16, 52),
                                    P(-16, 40), P(-26, 40), P(-26, 30),
                                    P(-16, 30)},
                                   hus, glaze::Stroke{.paint = husKant}),
             "Kyrkan");
        // ANTECKNINGENS RUTA nordost om Ekparken — texten ligger i
        // etikettlagret, i rutan (nedan).
        lagg(kYtLager,
             glaze::Shape::rect({kMittX + 75, kMittY + 140, 110, 40},
                                gfx::Color{255, 250, 232},
                                glaze::Stroke{.paint = gfx::Color{180, 160, 120}}),
             glaze::tr(demo::str::KartaAnteckning));

        // VÄGARNA i MM — bredden är densamma på skärmen vid varje zoom,
        // vridning och tilt. En väg är två konturer, en mörkare kant under och
        // fyllningen ovanpå, och alla kanter ritas före alla fyllningar så att
        // korsningarna flyter ihop. Skarvarna visar alla tre slagen: runda
        // (Storgatan), spetsiga (Kyrkvägen) och avfasade (Backvägen).
        struct Vag {
            const char*               namn;
            std::vector<glaze::Point> p;
            float                     bredd;   // mm
            glaze::LineJoin           skarv;
            glaze::LineCap            ande;
            gfx::Color                fyll;
        };
        const std::vector<Vag> vagar = {
            {"Storgatan",
             {P(-300, -45), P(-170, -50), P(-60, -40), P(20, -20), P(120, -10),
              P(300, -20)},
             1.3f, glaze::LineJoin::Round, glaze::LineCap::Round,
             gfx::Color{255, 226, 150}},
            {"Kyrkvägen",
             {P(60, 150), P(35, 110), P(75, 55), P(20, -20), P(45, -110),
              P(30, -250)},
             1.0f, glaze::LineJoin::Miter, glaze::LineCap::Square,
             gfx::Color{255, 255, 255}},
            {"Backvägen",
             {P(-300, 95), P(-230, 120), P(-170, 90), P(-100, 118), P(-30, 92),
              P(35, 110)},
             0.9f, glaze::LineJoin::Bevel, glaze::LineCap::Butt,
             gfx::Color{255, 255, 255}},
        };
        // PARKENS ÖVERSIKT — en bild på FYRA FRIA HÖRN, georefererad snett
        // över parken (övre vänstra, övre högra, nedre högra, nedre vänstra:
        // nordväst, nordost, sydost, sydväst med y uppåt) med opaciteten 0,6.
        // Den ligger först i vägarnas lager, så den täcker parken men inte
        // vägarna, och rutorna förblir raka — avbildningen är projektiv.
        lagg(kVagLager,
             glaze::Shape::image(P(92, 76), P(199, 64), P(201, -1), P(96, 8),
                                 rutor(), {.opacity = 0.6f}),
             "Ekparken");

        const gfx::Color vagKant{120, 112, 98};
        for (const Vag& v : vagar)
            lagg(kVagLager,
                 glaze::Shape::polyline(v.p, {.paint = vagKant,
                                              .width = glaze::mm(v.bredd + 0.5f),
                                              .cap   = v.ande,
                                              .join  = v.skarv}),
                 v.namn);
        for (const Vag& v : vagar)
            lagg(kVagLager,
                 glaze::Shape::polyline(v.p, {.paint = v.fyll,
                                              .width = glaze::mm(v.bredd),
                                              .cap   = v.ande,
                                              .join  = v.skarv}),
                 v.namn);
        // Stigen runt sjön är STRECKAD i mm — mönstret står still på skärmen
        // när kartan zoomas, och de runda ändarna gör strecken till prickar
        // med svans.
        std::vector<glaze::Point> stig;
        for (int i = 0; i <= 16; ++i) {
            const double a = (190.0 + i * 10.0) * kPi / 180.0;
            stig.push_back(P(-105.0 + 58.0 * std::cos(a), 25.0 + 58.0 * std::sin(a)));
        }
        lagg(kVagLager,
             glaze::Shape::polyline(stig, {.paint    = gfx::Color{139, 90, 43},
                                           .width    = glaze::mm(0.5f),
                                           .cap      = glaze::LineCap::Round,
                                           .dash     = {1.2f, 0.9f},
                                           .dashUnit = glaze::Unit::Mm}),
             "Strandstigen");

        // ETIKETTERNA, i mm och med GLORIA. De KOLLIDERAR: zooma ut så döljs
        // de mindre viktiga (prioriteten först, sedan ordningen) och tonar
        // tillbaka när det finns plats. Gatunamnen följer KARTAN när den vrids
        // (`Alignment::Map`, vinkeln längs vägen); ytornas namn står still mot
        // skärmen.
        const gfx::Color gloria{255, 255, 255};
        const auto gata = [&](const char* namn, glaze::Point a, glaze::Point b,
                              int prio) {
            const float vinkel = static_cast<float>(
                std::atan2(b.y - a.y, b.x - a.x) * 180.0 / kPi);
            lagg(kEtikettLager,
                 glaze::Shape::text({(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, namn,
                                    {.size     = glaze::mm(2.4f),
                                     .color    = gfx::Color{51, 51, 51},
                                     .halo     = gloria,
                                     .angle    = vinkel,
                                     .rotation = glaze::Alignment::Map,
                                     .priority = prio}),
                 namn);
        };
        gata("Storgatan", P(-170, -50), P(-60, -40), 5);
        gata("Storgatan", P(120, -10), P(300, -20), 5);
        gata("Kyrkvägen", P(20, -20), P(45, -110), 5);
        gata("Backvägen", P(-230, 120), P(-170, 90), 4);
        gata("Strandstigen", stig[3], stig[4], 2);
        const auto namn = [&](glaze::Point p, const char* text, gfx::Color c,
                              float storlek, int prio) {
            lagg(kEtikettLager,
                 glaze::Shape::text(p, text, {.size     = glaze::mm(storlek),
                                              .italic   = true,
                                              .color    = c,
                                              .halo     = gloria,
                                              .priority = prio}),
                 text);
        };
        namn(P(-112, 10), "Lillsjön", gfx::Color{30, 95, 150}, 2.8f, 8);
        namn(P(-95, 35), "Ekudden", gfx::Color{46, 107, 31}, 1.9f, 2);
        namn(P(147, 37), "Ekparken", gfx::Color{46, 107, 31}, 2.6f, 6);
        lagg(kEtikettLager,
             glaze::Shape::text(P(-10, 5), "Kyrkan",
                                {.size     = glaze::mm(2.0f),
                                 .color    = gfx::Color{74, 74, 74},
                                 .halo     = gloria,
                                 .anchor   = glaze::TextAnchor::Top,
                                 .priority = 3}),
             "Kyrkan");
        // EN ADRESS I DELAR (`TextSpan`): gatan i etikettens stil och numret
        // i FETSTIL och vägkantens färg — en etikett, ett rit-anrop, och
        // radbrytningen och glorian gäller hela den.
        lagg(kEtikettLager,
             glaze::Shape::text(P(-42, -68),
                                {glaze::TextSpan{.text = "Storgatan "},
                                 {.text = "12", .color = vagKant, .bold = true}},
                                {.size     = glaze::mm(1.8f),
                                 .color    = gfx::Color{51, 51, 51},
                                 .halo     = gloria,
                                 .priority = 1}),
             "Storgatan 12");

        // TEXT MED TAK: anteckningen LIGGER i sin ruta (Ground — den
        // följer rutan under vridning och tilt), bryts vid rutans bredd och
        // kortas med "…" när den inte ryms i rutans höjd — i alla fyra språk.
        lagg(kEtikettLager,
             glaze::Shape::text(P(79, 176), glaze::tr(demo::str::KartaAnteckning),
                                {.size      = glaze::world(6.5f),
                                 .color     = gfx::Color{90, 70, 40},
                                 .anchor    = glaze::TextAnchor::TopLeft,
                                 .rotation  = glaze::Alignment::Ground,
                                 .wrapWidth = glaze::world(102),
                                 .align     = glaze::Align::Left,
                                 .collide   = false,
                                 .maxHeight = glaze::world(32)}),
             glaze::tr(demo::str::KartaAnteckning));
        // GOLV OCH TAK: ett namn i VÄRLDSENHETER växer med zoomen men
        // stannar vid 2 mm och 6 mm på skärmen (linjal).
        lagg(kEtikettLager,
             glaze::Shape::text(P(-10, 64), "Kyrkbacken",
                                {.size    = glaze::world(14),
                                 .italic  = true,
                                 .color   = gfx::Color{110, 90, 60},
                                 .halo    = gloria,
                                 .minSize = glaze::mm(2),
                                 .maxSize = glaze::mm(6)}),
             "Kyrkbacken");
        // SYNLIGHET EFTER ZOOM: husens namn syns först när kartan
        // zoomats in ungefär två gånger från start, och tonar in och ut.
        for (const Hus& h : husen)
            lagg(kEtikettLager,
                 glaze::Shape::text(P(h.r.x + h.r.w / 2.0, h.r.y + h.r.h / 2.0),
                                    h.namn,
                                    {.size    = glaze::mm(1.6f),
                                     .color   = gfx::Color{74, 74, 74},
                                     .halo    = gloria,
                                     .minZoom = 2.0}),
                 h.namn);

        // MARKÖRERNA. Hållplatserna är ett punktmoln; badplatsen en cirkel med
        // en ÖVERSATT etikett (ett id i formlistan — den byter språk); nålen
        // en bildmarkör ur pixlar, ställd PÅ punkten (ankaret Bottom); och
        // informationen en bild ur appens assets.
        const gfx::Color vit{255, 255, 255};
        lagg(kEtikettLager,
             glaze::Shape::points({P(-230, -48), P(-20, -30), P(170, -13)},
                                  {.size         = glaze::mm(2.2f),
                                   .fill         = gfx::Color{21, 101, 192},
                                   .outline      = vit,
                                   .outlineWidth = glaze::mm(0.4f)}),
             glaze::tr(demo::str::KartaHallplats));
        lagg(kEtikettLager,
             glaze::Shape::marker(P(-58, 42), {.size         = glaze::mm(2.4f),
                                               .fill         = gfx::Color{2, 136, 209},
                                               .outline      = vit,
                                               .outlineWidth = glaze::mm(0.4f)}),
             glaze::tr(demo::str::KartaBadplats));
        lagg(kEtikettLager,
             glaze::Shape::text(P(-58, 42), glaze::tr(demo::str::KartaBadplats),
                                {.size     = glaze::mm(2.0f),
                                 .color    = gfx::Color{2, 100, 160},
                                 .halo     = gloria,
                                 .anchor   = glaze::TextAnchor::Left,
                                 .offsetX  = glaze::mm(2.0f),
                                 .priority = 1}),
             glaze::tr(demo::str::KartaBadplats));
        lagg(kEtikettLager,
             glaze::Shape::marker(P(160, 95), {.size   = glaze::mm(6.0f),
                                               .anchor = glaze::TextAnchor::Bottom,
                                               .img    = nal()}),
             glaze::tr(demo::str::KartaUtsikt));
        lagg(kEtikettLager,
             glaze::Shape::text(P(160, 95), glaze::tr(demo::str::KartaUtsikt),
                                {.size     = glaze::mm(2.0f),
                                 .color    = gfx::Color{74, 74, 74},
                                 .halo     = gloria,
                                 .anchor   = glaze::TextAnchor::Left,
                                 .offsetX  = glaze::mm(2.5f),
                                 .offsetY  = glaze::mm(-4.0f),
                                 .priority = 1}),
             glaze::tr(demo::str::KartaUtsikt));
        lagg(kEtikettLager,
             glaze::Shape::marker(P(-215, -20),
                                  {.size = glaze::mm(4.5f),
                                   .img  = glaze::img("toolbar", "info")}),
             glaze::tr(demo::str::KartaInfo));

        // TRÄFFPROVET: trycket sparas, och ett släpp nära det är ett klick —
        // med VÄNSTERKNAPPEN: handlerna får också mittenknappens tryck och
        // släpp (`e.button`), och mittendraget är kartans panorering.
        k->onMouseDown = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                kartaNed_ = e.local;
        };
        k->onMouseUp = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                kartaSlapp(s, e);
        };
        // HUD:EN i overlay-lagret — skalan och varifrån skärmens mm kommer.
        k->onPaintOverlay = [this](glaze::Widget* s, glaze::PaintArgs& e) {
            kartaOverlay(s, e);
        };

        // VRIDSNÄPPET (ställbart och valbart — snäpp till hela grader gör att man
        // kan få tillbaka rotationen): en
        // vallista OVANPÅ kartans övre högra hörn, som en kartas kontroller.
        // En vridning som släpps nära en vinkel i steget glider dit —
        // Ctrl+draget och styrplattans tvåfingervridning. Förval 90°, så det
        // syns direkt; ritytans eget förval är bara norr. Lagd EFTER kartan,
        // så den ritas och träffas överst. Bredden rymmer den längsta raden på
        // de fyra språken ("Kierron kohdistus: pohjoinen").
        auto* snapp      = g->add<glaze::Dropdown>();
        snapp->anchorTop = {"g25kSep", 18};
        snapp->anchors   = {false, true, true, false};
        snapp->right     = 16;
        snapp->width     = 210;
        snapp->name      = "kartaSnapp";
        const std::string pre = trs(demo::str::SnappRubrik);
        snapp->setItems({pre + trs(demo::str::SnappAv),
                         pre + trs(demo::str::SnappNorr), pre + "90°",
                         pre + "45°"});
        snapp->setSelectedIndex(kSnappForval);
        k->setRotationSnap(kSnappSteg[kSnappForval]);
        snapp->onChange = [k](glaze::Widget* s, glaze::ChangeArgs&) {
            const int i = static_cast<glaze::Dropdown*>(s)->selectedIndex();
            if (i >= 0 && i < static_cast<int>(std::size(kSnappSteg)))
                k->setRotationSnap(kSnappSteg[i]);
        };

        // LAGERLISTAN uppe till vänster över kartan: lagren
        // ÖVERST FÖRST, och ▲/▼ flyttar det valda ett steg — `moveLayer`
        // byter bara ritordningen, inget byggs om eller laddas upp. Välj
        // Plattor och tryck ▲ tre gånger, så täcker plattorna allt. Listan
        // fylls inte om vid språkbyte (vridsnäppets prejudikat) — nästa flytt
        // skriver den på det nya språket.
        auto* lager      = g->add<glaze::Dropdown>();
        lager->anchorTop = {"g25kSep", 18};
        lager->x         = 16;
        lager->width     = 150;
        lager->name      = "kartaLager";
        kartLager_       = lager;
        kartaLagerLista(*k, k->layerCount() - 1);
        auto* upp       = g->add<glaze::Button>("▲");
        upp->anchorLeft = {"kartaLager", 4};
        upp->anchorTop  = {"g25kSep", 18};
        upp->name       = "kartaLagerUpp";
        upp->setToolTip(glaze::tr(demo::str::KartaLagerFram));
        upp->onClick    = [this, k] { kartaLagerSteg(*k, 1); };
        auto* ner       = g->add<glaze::Button>("▼");
        ner->anchorLeft = {"kartaLagerUpp", 4};
        ner->anchorTop  = {"g25kSep", 18};
        ner->name       = "kartaLagerNer";
        ner->setToolTip(glaze::tr(demo::str::KartaLagerBak));
        ner->onClick    = [this, k] { kartaLagerSteg(*k, -1); };

        fot(g, demo::str::KartaFot, "karta", 8);
    }

    // === 25 · Canvas — graf, och samma graf inaktiverad ==================
    {
        auto* g = grupp(sida, demo::str::G25Graf, "g25g", 644, 12, 620, 312);

        // Y UPPÅT och rutnätet var hel enhet (det glesas ut själv när man
        // zoomar ut). Panorering och hjulet är på; vridning och tilt är det
        // inte — en graf ska inte luta.
        auto* gr      = g->add<glaze::Canvas>();
        gr->x         = 8;
        gr->anchorTop = {"g25gSep", 10};
        gr->width     = 430;
        gr->height    = 213;
        gr->name      = "graf";
        gr->setYUp(true);
        gr->setGridSize(1);
        gr->setPannable(true);
        gr->setZoomable(true);
        gr->setZoomRange(2, 5000);
        gr->setShapes(grafFormer());
        gr->zoomToRect(kGrafYta, 12);   // väntar till första ritningen
        gr->onPaint = [this](glaze::Widget* s, glaze::PaintArgs& e) {
            grafPaint(s, e);
        };
        gr->onPaintOverlay = [this](glaze::Widget* s, glaze::PaintArgs& e) {
            grafOverlay(s, e);
        };
        gr->onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            grafNed(s, e);
        };
        gr->onMouseMove = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            grafRor(s, e);
        };
        gr->onMouseUp = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            grafSlapp(s, e);
        };
        // Bara VÄNSTERKNAPPENS dubbelklick återställer —
        // mitten- och högerknappens kommer också hit (`e.button`).
        gr->onDoubleClick = [](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                static_cast<glaze::Canvas*>(s)->zoomToRect(kGrafYta, 12);
        };
        // Pekaren lämnade ytan: koordinaterna försvinner ur HUD:en.
        gr->onHover = [this](glaze::Widget* s, glaze::HoverArgs& e) {
            if (e.entered)
                return;
            grafPekare_.reset();
            static_cast<glaze::Canvas*>(s)->invalidateOverlay();
        };

        // INAKTIVERAD — samma former, tonade till 1 − `iconFade`; gesterna
        // står still. Sträckt mellan grafen och gruppens högerkant.
        auto* av       = g->add<glaze::Canvas>();
        av->anchorLeft = {"graf", 8};
        av->anchorTop  = {"g25gSep", 10};
        av->anchors    = {true, true, true, false};
        av->right      = 8;
        av->height     = 213;
        av->name       = "grafAv";
        av->setYUp(true);
        av->setGridSize(1);
        av->setPannable(true);
        av->setZoomable(true);
        av->setShapes(grafFormer());
        av->zoomToRect(kGrafYta, 8);
        av->disable();

        auto* f = fot(g, demo::str::GrafFot, "graf", 8);
        fot(g, demo::str::GrafAvFot, f->name, 2);
    }

    // === 25 · Canvas — kalibrering (linjalprovet) =========================
    {
        auto* g = grupp(sida, demo::str::G25Kal, "g25c", 12, 336, 1252, 160);

        // LINJALEN ritas i `onPaint` utan kamera, alltså i ritytans lokala
        // logiska px. Ett drag (eller ett tryck) flyttar dess högra ände.
        auto* l      = g->add<glaze::Canvas>();
        l->x         = 8;
        l->anchorTop = {"g25cSep", 10};
        l->anchors   = {true, true, true, false};
        l->right     = 8;
        l->height    = 72;
        l->name      = "linjal";
        linjal_      = l;
        l->onPaint = [this](glaze::Widget* s, glaze::PaintArgs& e) {
            linjalPaint(s, e);
        };
        l->onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                linjalDra(s, e);
        };
        l->onMouseMove = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                linjalDra(s, e);
        };

        // STEGEN: en fysisk pixel på linjens 100 mm åt gången. ASCII-minus och
        // inte U+2212: UI-fonten har ingen glyfreserv, och Noto Sans (det
        // fontconfig väljer som sans-serif på Debian 13) saknar U+2212.
        auto* minus      = g->add<glaze::Button>("-");
        minus->x         = 8;
        minus->anchorTop = {"linjal", 8};
        minus->name      = "kalMinus";
        minus->onClick   = [this] { sattKandidat(kandidat() - kKalSteg); };
        auto* plus       = g->add<glaze::Button>("+");
        plus->anchorLeft = {"kalMinus", 6};
        plus->anchorTop  = {"linjal", 8};
        plus->name       = "kalPlus";
        plus->onClick    = [this] { sattKandidat(kandidat() + kKalSteg); };
        auto* spara       = g->add<glaze::Button>(glaze::tr(demo::str::TipSpara));
        spara->anchorLeft = {"kalPlus", 12};
        spara->anchorTop  = {"linjal", 8};
        spara->name       = "kalSpara";
        spara->onClick    = [this](glaze::Widget* s, glaze::ClickArgs& e) {
            kalSpara(s, e);
        };
        auto* ater = g->add<glaze::Button>(glaze::tr(demo::str::DockAterstall));
        ater->anchorLeft = {"kalSpara", 6};
        ater->anchorTop  = {"linjal", 8};
        ater->name       = "kalAter";
        ater->onClick    = [this](glaze::Widget* s, glaze::ClickArgs& e) {
            kalAterstall(s, e);
        };

        auto* f      = g->add<glaze::Label>(glaze::tr(demo::str::KalFot));
        f->anchorLeft = {"kalAter", 16};
        f->anchorTop  = {"linjal", 14};
        f->name       = "kalFot";
        f->setRole(glaze::Label::Role::Dim);
    }
}

// ---------------------------------------------------------------- handlers

void WidgetGallery::kartaSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e)
{
    // Ett KLICK och inte ett drag: släppet ligger inom stilens dragtröskel
    // från trycket. Ett drag har redan panorerat — då är träffen inte frågan.
    const double dx = e.local.x - kartaNed_.x, dy = e.local.y - kartaNed_.y;
    const double grans = style().dragThreshold;
    if (dx * dx + dy * dy > grans * grans)
        return;
    // ALLA träffar under punkten, ÖVERST FÖRST — den översta skrivs ut och
    // antalet under den efter plustecknet.
    const auto* k = static_cast<const glaze::Canvas*>(sender);
    const std::vector<glaze::HitResult> h = k->hitTest(e.local);
    if (h.empty()) {
        statusTxt_->setText(glaze::tr(demo::str::KartaIngen));
        return;
    }
    const glaze::HitResult& f = h.front();
    // Träffens lager är en PLATS — lagerlistan kan ha flyttat det; namnen
    // står i det ursprungliga lagrets ordning (`kartOrdning_`).
    const auto plats = static_cast<std::size_t>(f.layer);
    const auto form  = static_cast<std::size_t>(f.index);
    if (plats >= kartOrdning_.size())
        return;   // varje lager speglas — kan inte hända
    const auto lager = static_cast<std::size_t>(kartOrdning_[plats]);
    if (lager >= kartNamn_.size() || form >= kartNamn_[lager].size())
        return;   // varje form läggs till med ett namn — kan inte hända
    std::string t = trs(demo::str::KartaPrefix) + kartNamn_[lager][form].str() +
                    " (" + k->layerName(f.layer) + " #" +
                    std::to_string(f.index) + ")";
    if (h.size() > 1)
        t += " +" + std::to_string(h.size() - 1);
    statusTxt_->setText(t);
}

// LAGERLISTAN: rad r är platsen n − 1 − r — lagren överst först, som i ett
// ritprogram (en lista byggd på ett träd skickar samma plats vidare).
void WidgetGallery::kartaLagerLista(const glaze::Canvas& k, int plats)
{
    const int                n = k.layerCount();
    std::vector<std::string> rader;
    for (int p = n - 1; p >= 0; --p)
        rader.push_back(trs(demo::str::KartaLagerPrefix) + k.layerName(p));
    kartLager_->setItems(std::move(rader));
    kartLager_->setSelectedIndex(n - 1 - plats);
}

// ▲ och ▼: det valda lagret ett steg upp eller ned. `moveLayer`s plats
// räknas FÖRE flytten — ett steg upp är plats + 2 (ovanför grannen), ett
// steg ned plats − 1 — och demons spegling följer med: två grannar byter
// plats. Statusraden visar den nya ordningen överst först.
void WidgetGallery::kartaLagerSteg(glaze::Canvas& k, int steg)
{
    const int n    = k.layerCount();
    const int rad  = kartLager_->selectedIndex();
    const int fran = n - 1 - rad;
    const int till = fran + steg;
    if (rad < 0 || till < 0 || till >= n ||
        static_cast<int>(kartOrdning_.size()) != n)
        return;   // överst eller underst redan — eller inget valt
    if (!k.moveLayer(fran, steg > 0 ? fran + 2 : fran - 1))
        return;
    std::swap(kartOrdning_[static_cast<std::size_t>(fran)],
              kartOrdning_[static_cast<std::size_t>(till)]);
    kartaLagerLista(k, till);
    std::string t = trs(demo::str::KartaLagerOrdning);
    for (int p = n - 1; p >= 0; --p)
        t += k.layerName(p) + (p > 0 ? ", " : "");
    statusTxt_->setText(t);
}

void WidgetGallery::kartaOverlay(glaze::Widget* sender, glaze::PaintArgs& e)
{
    // SKALSTOCKEN nere till vänster, i overlay-lagret: den står fast på
    // skärmen och vrids eller lutas aldrig med kartan. Den MÄTER DÄR DEN
    // STÅR — under tilt varierar skalan över ytan (större närmare kameran),
    // och mittens skala (`mapScale()`) hade varit fel vid underkanten.
    // `unitsPerPixelAt` ger meter per logisk px längs skärmens vågräta axel
    // på stockens rad, och längs en bildrad är skalan lika också under
    // vridning och tilt — så en vågrät stock är exakt där den står. Bredvid:
    // "1:N" mätt på samma plats ("≈" när skärmens mm är ANTAGNA — 96 dpi i
    // fysiska px, oberoende av skalan) och varifrån mm kommer. Måtten i mm,
    // färgerna temats.
    const auto* k   = static_cast<const glaze::Canvas*>(sender);
    const auto& col = theme().resolve(themeMode()).colors;
    // Logiska px per mm på skärmen — mm är fysiska, px följer skalan.
    const double perMm = k->display().pxPerMm / window().uiScale();
    const double luft  = 1.5 * perMm;   // plattans
    const double band  = 1.4 * perMm;   // stockens höjd
    const double kant  = 8.0;           // från ytans hörn, logiska px
    glaze::TextStyle stil{.size   = glaze::mm(2.4f),
                          .color  = col.text,
                          .anchor = glaze::TextAnchor::Bottom,
                          .align  = glaze::Align::Left};
    // Plattan är OGENOMSKINLIG: en halvgenomskinlig lät kartans ljusa
    // glorior lysa igenom i mörkt läge.
    const gfx::Color platta = col.panel;

    // Raderna nerifrån: stocken på plattans innerbotten, siffrorna ovanför.
    const double botten = e.painter.size().h - kant - luft;
    const double topp   = botten - band;
    const double radH   = e.painter.measureText("0", stil).h;
    const double noll   = e.painter.measureText("0", stil).w;
    const double x0     = kant + luft + noll / 2.0;
    const auto   u      = k->unitsPerPixelAt({x0, topp + band / 2.0});
    std::string  skala  = "–";
    double       L = 0.0, D = 0.0, slutW = 0.0;
    if (u) {
        // Ett runt tal (1-2-5) som blir en stock på ungefär 25–40 mm.
        D     = narmast125(31.6 * perMm * *u);
        L     = D / *u;
        slutW = e.painter.measureText(meter(D), stil).w;
        // "≈" (U+2248) är formen för en antagen skala. Noto Sans —
        // fontconfigs sans-serif på Debian 13, också i WSLg — saknar den;
        // fontens glyfreserv tar den ur en annan
        // systemfont, och bara utan någon sådan syns ersättningsglyfen.
        skala = (k->mapScaleIsExact() ? "1:" : "≈1:") +
                std::to_string(std::lround(*u * perMm * 1000.0));
    }
    const std::string text = skala + "\n" + trs(mmKalla(k->display().source));
    const gfx::Sizef  textM = e.painter.measureText(text, stil);
    const double vansterH = radH + 0.6 * perMm + band;
    const double innerH   = std::max(vansterH, double(textM.h));
    const double textX    = u ? x0 + L + slutW / 2.0 + 2.0 * perMm : kant + luft;
    e.painter.fillRect({kant, botten - innerH - luft,
                        textX + textM.w + luft - kant, innerH + 2.0 * luft},
                       platta);
    if (u) {
        // Bandet: växlande fyllda och tomma delar (plattan) med en kontur —
        // fem delar för 1 och 5, fyra för 2, så varje del är ett runt tal.
        const double dekad = std::pow(10.0, std::floor(std::log10(D) + 1e-9));
        const int    delar = D / dekad < 1.5 || D / dekad > 3.5 ? 5 : 4;
        for (int i = 0; i < delar; i += 2)
            e.painter.fillRect({x0 + L * i / delar, topp, L / delar, band},
                               col.text);
        e.painter.drawRect({x0, topp, L, band}, {.paint = col.text});
        const double siffrorna = topp - 0.6 * perMm;
        e.painter.drawText({x0, siffrorna}, "0", stil);
        e.painter.drawText({x0 + L, siffrorna}, meter(D), stil);
    }
    stil.anchor = glaze::TextAnchor::Left;
    e.painter.drawText({textX, botten - innerH / 2.0}, text, stil);
}

void WidgetGallery::grafNed(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    // SKIFT+DRAG är gummibandet: handlern TAR trycket (`handled`), så ritytan
    // startar ingen panorering. Utan Skift — och med mittenknappen — panorerar
    // den som vanligt. Ett tryck med en ANNAN knapp rör inte bandet: ett
    // högertryck mitt i ett band (det kommer hit) hade
    // annars släckt det.
    if (e.button != gfx::MouseButton::Left)
        return;
    band_ = (e.mods & gfx::kModShift) != 0;
    if (!band_)
        return;
    bandFran_ = bandTill_ = e.local;
    e.handled = true;
}

void WidgetGallery::grafRor(glaze::Widget* sender, glaze::CanvasMouseArgs& e)
{
    grafPekare_ = e.world;
    if (band_) {
        if (e.button == gfx::MouseButton::Left) {
            bandTill_ = e.local;
            e.handled = true;
        } else if (e.button == gfx::MouseButton::None) {
            band_ = false;   // trycket släpptes där ritytan inte såg det
        }
    }
    // Bara overlay-lagret har ändrats — en ny present, ingen omritning.
    static_cast<glaze::Canvas*>(sender)->invalidateOverlay();
}

void WidgetGallery::grafSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e)
{
    if (!band_ || e.button != gfx::MouseButton::Left)
        return;
    band_     = false;
    e.handled = true;
    // Rutan i världen, ur bandets två hörn — mindre än en dragtröskel åt
    // något håll är ett klick och zoomar ingenting.
    auto*        gr    = static_cast<glaze::Canvas*>(sender);
    const double grans = style().dragThreshold;
    const auto   a     = gr->toWorld(bandFran_);
    const auto   b     = gr->toWorld(e.local);
    if (a && b && std::abs(e.local.x - bandFran_.x) > grans &&
        std::abs(e.local.y - bandFran_.y) > grans)
        gr->zoomToRect({std::min(a->x, b->x), std::min(a->y, b->y),
                        std::abs(b->x - a->x), std::abs(b->y - a->y)});
    gr->invalidateOverlay();
}

void WidgetGallery::grafPaint(glaze::Widget* sender, glaze::PaintArgs& e)
{
    // AXLARNAS TAL, i världen och i temats dämpade textfärg. Steget är
    // rutnätets följd med minst fem rutnätslinjer emellan, så talen står på
    // linjerna och glesas ut med dem när man zoomar ut.
    const auto* gr = static_cast<const glaze::Canvas*>(sender);
    const gfx::Sizef yta = e.painter.size();
    const auto a = gr->toWorld({0, 0});
    const auto b = gr->toWorld({yta.w, yta.h});
    if (!a || !b)
        return;
    const auto&  col  = theme().resolve(themeMode()).colors;
    const double steg = steg125(5.0 * style().canvasGridMin / gr->zoom());
    const int    dec  = std::max(0, static_cast<int>(-std::floor(std::log10(steg) + 1e-9)));
    glaze::TextStyle st{.size    = glaze::px(gfx::fontsize::kUiCompact),
                        .color   = col.textDim,
                        .anchor  = glaze::TextAnchor::Top,
                        .offsetY = 3};
    const auto rita = [&](double lo, double hi, bool xled) {
        const auto forsta = static_cast<long>(std::ceil(std::min(lo, hi) / steg));
        const auto sista  = static_cast<long>(std::floor(std::max(lo, hi) / steg));
        for (long i = forsta; i <= sista; ++i)
            if (i != 0)
                e.painter.drawText(xled ? glaze::Point{i * steg, 0.0}
                                        : glaze::Point{0.0, i * steg},
                                   tal(i * steg, dec), st);
    };
    rita(a->x, b->x, true);
    st.anchor  = glaze::TextAnchor::Right;
    st.offsetX = -4;
    st.offsetY = 0;
    rita(a->y, b->y, false);
    st.anchor  = glaze::TextAnchor::TopRight;
    st.offsetY = 3;
    e.painter.drawText({0.0, 0.0}, "0", st);
}

void WidgetGallery::grafOverlay(glaze::Widget* sender, glaze::PaintArgs& e)
{
    // Overlay-lagret ritas i ritytans LOKALA px — gummibandet ligger där
    // pekaren drog, och koordinaterna i hörnet. Båda är gränssnitt: temats
    // accent och text.
    const auto& col = theme().resolve(themeMode()).colors;
    if (band_) {
        const glaze::Rect r{std::min(bandFran_.x, bandTill_.x),
                            std::min(bandFran_.y, bandTill_.y),
                            std::abs(bandTill_.x - bandFran_.x),
                            std::abs(bandTill_.y - bandFran_.y)};
        gfx::Color fyll = col.accent;
        fyll.a          = 48;
        e.painter.fillRect(r, fyll);
        e.painter.drawRect(r, {.paint = col.accent});
    }
    if (grafPekare_) {
        // Decimalerna följer zoomen: en pixel ska synas i sista siffran.
        const auto* gr  = static_cast<const glaze::Canvas*>(sender);
        const int   dec = std::clamp(
            static_cast<int>(std::ceil(std::log10(gr->zoom()))), 0, 6);
        gfx::Color platta = col.panel;
        platta.a          = 230;
        e.painter.drawText({8, 8},
                           "x = " + tal(grafPekare_->x, dec) +
                               "   y = " + tal(grafPekare_->y, dec),
                           {.size       = glaze::px(style().uiFontSize),
                            .color      = col.text,
                            .background = platta,
                            .padding    = 4,
                            .anchor     = glaze::TextAnchor::TopLeft});
    }
}

double WidgetGallery::kandidat() const
{
    // Linjens tal gäller skärmen det provades på; annars skärmens eget —
    // det mätta, antagna eller redan kalibrerade.
    const gfx::DisplayMetrics& d = display();
    return kand_ > 0.0 && kandId_ == d.id ? kand_ : d.pxPerMm;
}

void WidgetGallery::sattKandidat(double pxPerMm)
{
    kand_   = std::clamp(pxPerMm, kMinPxPerMm, kMaxPxPerMm);
    kandId_ = display().id;
    if (linjal_)
        linjal_->invalidate();
}

void WidgetGallery::linjalPaint(glaze::Widget*, glaze::PaintArgs& e)
{
    // SKÄRMEN överst: id:t (kalibreringens nyckel), varifrån dess mm kommer
    // och talet som gäller — och linjens tal, det som sparas.
    const gfx::DisplayMetrics& d    = display();
    const double               k    = kandidat();
    const auto&                col  = theme().resolve(themeMode()).colors;
    const std::string          info = trs(demo::str::KalSkarm) +
                             (d.id.empty() ? std::string("—") : d.id) + " · " +
                             trs(mmKalla(d.source)) + " · " +
                             tal(d.pxPerMm, 2) + " px/mm (" +
                             tal(d.pxPerMm * 25.4, 0) + " dpi) · " +
                             trs(demo::str::KalLinjen) + tal(k, 2) + " px/mm";
    e.painter.drawText({kLinjalX, 6}, info,
                       {.size   = glaze::px(style().uiFontSize),
                        .color  = col.text,
                        .anchor = glaze::TextAnchor::TopLeft});

    // LINJALEN: 100 mm är 100 × talet FYSISKA px, och ritytan utan kamera
    // talar logiska — fönstrets skala emellan. Ett streck per mm, längre
    // var femte och tionde, och siffror var tionde.
    const double       L = 100.0 * k / window().uiScale();
    const glaze::Stroke penna{.paint = col.text};
    glaze::TextStyle   siffra{.size   = glaze::px(gfx::fontsize::kUiCompact),
                              .color  = col.textDim,
                              .anchor = glaze::TextAnchor::Top};
    e.painter.drawLine({kLinjalX, kLinjalY}, {kLinjalX + L, kLinjalY}, penna);
    for (int mm = 0; mm <= 100; ++mm) {
        const double x = kLinjalX + L * mm / 100.0;
        const double h = mm % 10 == 0 ? 12.0 : mm % 5 == 0 ? 8.0 : 4.0;
        e.painter.drawLine({x, kLinjalY}, {x, kLinjalY - h}, penna);
        if (mm % 10 == 0)
            e.painter.drawText({x, kLinjalY + 3}, std::to_string(mm), siffra);
    }
    siffra.anchor = glaze::TextAnchor::Left;
    e.painter.drawText({kLinjalX + L + 14, kLinjalY}, "mm", siffra);
    // Handtaget i änden — det man drar i.
    e.painter.drawMarker({kLinjalX + L, kLinjalY}, {.size = 10, .fill = col.accent});
}

void WidgetGallery::linjalDra(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    // Änden följer pekaren: längden i LOGISKA px × fönstrets skala är FYSISKA
    // px, och en hundradel av dem är talet per mm.
    const double langd = e.local.x - kLinjalX;
    if (langd > 0.0)
        sattKandidat(langd * window().uiScale() / 100.0);
    e.handled = true;
}

void WidgetGallery::kalSpara(glaze::Widget*, glaze::ClickArgs&)
{
    // KALIBRERINGEN är app-global per skärm-id, och varje öppet fönster mäts
    // om och ritas om: det som ritas i mm — kartans vägar, dess skala — byter
    // storlek direkt. Appen äger LAGRINGEN; en riktig app skriver talet i
    // sina inställningar och sätter det igen vid start (demon gör det inte).
    const std::string id = display().id;
    if (id.empty()) {
        statusTxt_->setText(glaze::tr(demo::str::KalIngetId));
        return;
    }
    const double v = kandidat();
    glaze::app().setDisplayCalibration(id, static_cast<float>(v));
    statusTxt_->setText(trs(demo::str::KalSparad) + id + ": " + tal(v, 2) +
                        " px/mm");
}

void WidgetGallery::kalAterstall(glaze::Widget*, glaze::ClickArgs&)
{
    // 0 tar bort kalibreringen — skärmen går tillbaka till sitt mätta (eller
    // antagna) tal, och linjen följer det.
    const std::string id = display().id;
    if (id.empty()) {
        statusTxt_->setText(glaze::tr(demo::str::KalIngetId));
        return;
    }
    glaze::app().setDisplayCalibration(id, 0.0f);
    kand_ = 0.0;
    if (linjal_)
        linjal_->invalidate();
    statusTxt_->setText(trs(demo::str::KalAterstalld) + id);
}
