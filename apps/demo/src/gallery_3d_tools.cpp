// GALLERIETS 3D-VERKTYG — fliken "3D-verktyg". Konstruktionseditorns
// förmågor byggda med ritytans VERKTYG — ramverket har med flit inget av
// dem (ett rutnät är bara linjer, och ritytan har linjeverktyget). Scenen
// är en stolptopp på ett arbetsplan, och proven är fyra:
//
//   rutnätet     ADAPTIVT i planet z = 0: linjer i rymden (`line3`, px-bredd)
//                i ett eget lager — den fina nivån ur `worldPerPixel` mitt i
//                ytan, den synliga delen ur `visibleRegion` — byggt om i
//                `onCameraChange`, och i `onTick` efter första ritningen och
//                medan kuben glider (sättarna och `easeTo` fyrar aldrig
//                `onCameraChange`; tickningen slår av sig själv när kameran
//                står still)
//   kuben        en NAVIGERINGSKUB i en del-vy (`View`, ortografisk,
//                `followRotation`): `box`, och `text3` på de sidor som vänder
//                sig mot kameran (följer kameran som rutnätet). Ett klick
//                plockar (`hitTest` routas till vyn), träffpunkten ger
//                REGIONEN — sida, kant eller hörn — och `easeTo` glider dit på
//                250 ms
//   kantläget    meshen med djupbias, och kanterna (modellens egen
//                kantextraktion: öppna kanter och veck) som linjer i rymden i
//                två lager utan djupskrivning — LessEqual mörka, Greater ljusa
//                och streckade (de dolda); kryssrutan slår av och på
//   miniatyren   `renderToBitmap(256, 256, kamera)` med en egen kamera
//                (45°, 30°, 2,5 radier bort), visad som bild på
//                en rityta utan kamera bredvid
//
// RUTNÄTET, KUBENS REGIONER, KANTEXTRAKTIONEN OCH MINIATYRENS KAMERA ÄR
// APPENS KOD och bor här, inte i ramverket (verktyg, inte
// modulspecifika metoder). Koordinaterna är meter kring arbetsplanets origo.
//
// FÄRGERNA är scenens data och följer inte temat; rutnätet är
// halvgenomskinligt grått och kanterna mörka respektive grå, så de syns mot
// både den ljusa och den mörka ytan bakom.
//
// REGEL 8: kubens vy och låda, lagrens placering och renderlägen, kameran och
// kontrollerna går att skriva i `.glazeui` — etiketterna också, som former
// (`text3`), men här byggs de om efter kameran. Modellens mesh och kanter
// räknas fram ur data (koddata, Bitmaps prejudikat),
// rutnätet och kubens etiketter följer kameran i handlers och miniatyren är
// en handling (`renderToBitmap`) — handlers kör previewn aldrig.

#include "widget_gallery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr double kPi = std::numbers::pi;

// Lagren, nedifrån och upp — rutnätet först: det skriver inget djup, och
// modellen täcker det där den står framför.
constexpr int kRutnat  = 0;
constexpr int kModell  = 1;
constexpr int kKanter  = 2;
constexpr int kDolda   = 3;
constexpr int kKub     = 4;
constexpr int kKubText = 5;

// RUTNÄTET (appens regler): den fina nivån är den minsta
// tiopotens vars linjer står minst kMinPx logiska px isär mitt i ytan, och
// den tonas in mellan kMinPx och tio gånger det, så att ingen nivå dyker upp
// eller försvinner med ett ryck; den grova är tio gånger den. Den fina når
// kRackvidd gånger avståndet från ögat till planets mitt och den grova tio
// gånger längre — längre bort hade linjerna flutit ihop. Högst kMaxLinjer per
// riktning och nivå kring mitten, och den synliga delen VIDGAS
// med kMarginal kring sin mitt: ett större fönster täcks till nästa
// kameraändring.
constexpr double kMinPx     = 10.0;
constexpr double kRackvidd  = 3.0;
constexpr double kMaxLinjer = 2000.0;
constexpr double kMarginal  = 1.5;
// KAMERANS BORTRE PLAN (m): fast, så att rutnätet når ut. Förvalet (0) tas
// ur scenens låda, och den låda hade varit rutnätets egen — det hade vuxit
// lite för varje ombygge.
constexpr double kBortre = 1000.0;

// KUBEN: halva sidan, etiketternas lyft ut ur sin sida, hur mycket en sida
// måste vända sig mot kameran för att få sin etikett (cosinus för vinkeln
// mellan sidans normal och riktningen mot ögat — en sida på kant får ingen),
// gränsen för en region (0,6 av halva sidan) och glidningen
// (250 ms).
constexpr double kKubHalv = 1.0;
constexpr double kKubLyft = 0.06;
constexpr double kKubVand = 0.2;
constexpr double kRegion  = 0.6;
constexpr int    kGlidMs  = 250;

// KANTERNA: veckgränsen i grader — 30°. Går den ned till 1° ritas också
// cylindrarnas facetter.
constexpr double kVeck = 30.0;

// MINIATYREN: bildens sida i bildpunkter, ytan den visas på i
// logiska px, kamerans vinklar och avståndet i lådans radier.
constexpr int    kMiniPx     = 256;
constexpr double kMiniYta    = 256.0;
constexpr float  kMiniAzimut = 45.0f;
constexpr float  kMiniElev   = 30.0f;
constexpr double kMiniRadier = 2.5;

// Modellens material — scenens data.
const gfx::Color kTra{180, 142, 100, 255};
const gfx::Color kStal{198, 200, 206, 255};
const gfx::Color kGlas{120, 172, 224, 255};

using Index = std::uint32_t;

// --------------------------------------------------------------- vektorerna

glaze::Vec3 minus(const glaze::Point3& a, const glaze::Point3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

glaze::Vec3 kryss(const glaze::Vec3& a, const glaze::Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double prick(const glaze::Vec3& a, const glaze::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

glaze::Vec3 enhet(const glaze::Vec3& v)
{
    const double l = std::sqrt(prick(v, v));
    return l > 0.0 ? glaze::Vec3{v.x / l, v.y / l, v.z / l} : glaze::Vec3{};
}

glaze::Point3 lage(const glaze::MeshDesc& d, Index i)
{
    const glaze::Float3& p = d.positions[i];
    return {p.x, p.y, p.z};
}

Index nyttLage(glaze::MeshDesc& d, const glaze::Point3& p, gfx::Color farg)
{
    d.positions.push_back({static_cast<float>(p.x), static_cast<float>(p.y),
                           static_cast<float>(p.z)});
    d.colors.push_back(farg);
    return static_cast<Index>(d.positions.size() - 1);
}

// ------------------------------------------------------------------ modellen
//
// ETT SVETSAT NÄT: lägena DELAS mellan trianglarna som möts i dem, så att
// kanterna hittas ur grannskapet (dubbletter svetsas annars ihop först — här
// delas lägena redan i bygget). Inga normaler: Mesh-programmet tar den platta
// normalen ur derivatorna, och varje triangel blir plan.

// Ett PRISMA med `sidor` sidor och radien r längs axeln a → b, med lock —
// sidorna moturs sedda utifrån, locken som solfjädrar.
void prisma(glaze::MeshDesc& d, glaze::Point3 a, glaze::Point3 b, double r,
            int sidor, gfx::Color farg)
{
    const glaze::Vec3 w = enhet(minus(b, a));
    const glaze::Vec3 u =
        enhet(kryss(std::abs(w.z) < 0.9 ? glaze::Vec3{0, 0, 1} : glaze::Vec3{1, 0, 0}, w));
    const glaze::Vec3 v = kryss(w, u);   // (u, v, w) högerhänt
    const auto        bas = static_cast<Index>(d.positions.size());
    for (int k = 0; k < sidor; ++k) {
        const double      t = 2.0 * kPi * (k + 0.5) / sidor;
        const glaze::Vec3 o{r * (std::cos(t) * u.x + std::sin(t) * v.x),
                            r * (std::cos(t) * u.y + std::sin(t) * v.y),
                            r * (std::cos(t) * u.z + std::sin(t) * v.z)};
        nyttLage(d, {a.x + o.x, a.y + o.y, a.z + o.z}, farg);   // 2k   nere
        nyttLage(d, {b.x + o.x, b.y + o.y, b.z + o.z}, farg);   // 2k+1 uppe
    }
    const auto n = static_cast<Index>(sidor);
    for (Index k = 0; k < n; ++k) {
        const Index a0 = bas + 2 * k, a1 = bas + 2 * ((k + 1) % n);
        d.indices.insert(d.indices.end(), {a0, a1, a1 + 1, a0, a1 + 1, a0 + 1});
    }
    for (Index k = 1; k + 1 < n; ++k) {
        d.indices.insert(d.indices.end(), {bas, bas + 2 * (k + 1), bas + 2 * k});
        d.indices.insert(d.indices.end(),
                         {bas + 1, bas + 2 * k + 1, bas + 2 * (k + 1) + 1});
    }
}

// En LÅDA — åtta delade hörn och sex sidor, var och en vänd UTÅT (moturs sedd
// utifrån, prövad mot lådans mitt).
void lada(glaze::MeshDesc& d, glaze::Point3 lo, glaze::Point3 hi, gfx::Color farg)
{
    const auto bas = static_cast<Index>(d.positions.size());
    for (int i = 0; i < 8; ++i)
        nyttLage(d, {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z},
                 farg);
    const glaze::Point3 mitt{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    const Index sidor[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1},
                               {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
    for (const auto& s : sidor) {
        Index q[4] = {bas + s[0], bas + s[1], bas + s[2], bas + s[3]};
        const glaze::Point3 p0 = lage(d, q[0]);
        if (prick(kryss(minus(lage(d, q[1]), p0), minus(lage(d, q[2]), p0)),
                  minus(p0, mitt)) < 0.0)
            std::swap(q[1], q[3]);
        d.indices.insert(d.indices.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
    }
}

// En ISOLATOR på foten: en kärna och tre skärmar.
void isolator(glaze::MeshDesc& d, glaze::Point3 fot)
{
    prisma(d, fot, {fot.x, fot.y, fot.z + 0.34}, 0.028, 16, kGlas);
    for (const double h : {0.08, 0.16, 0.24})
        prisma(d, {fot.x, fot.y, fot.z + h}, {fot.x, fot.y, fot.z + h + 0.02}, 0.07,
               16, kGlas);
}

// STOLPTOPPEN — stolpen, traversen, två strävor och tre isolatorer, i ETT nät.
glaze::MeshDesc stolptopp()
{
    glaze::MeshDesc d;
    prisma(d, {0, 0, 0}, {0, 0, 3.2}, 0.14, 16, kTra);
    lada(d, {-1.3, -0.06, 2.72}, {1.3, 0.06, 2.86}, kStal);
    for (const double s : {-1.0, 1.0})
        prisma(d, {0.08 * s, 0, 2.05}, {0.85 * s, 0, 2.74}, 0.03, 4, kStal);
    for (const double x : {-1.1, 1.1})
        isolator(d, {x, 0, 2.86});
    isolator(d, {0, 0, 3.2});
    return d;
}

// MODELLENS KANTER — modulens kantextraktion: en
// kant som bara EN triangel har (öppen) och en kant där två trianglar möts i
// en vinkel över kVeck (ett veck) — aldrig diagonalen i en plan yta. Svaret
// är kanternas ändpunkter, två och två.
std::vector<glaze::Point3> kanter(const glaze::MeshDesc& d)
{
    struct Kant {
        Index a, b, tri;
    };
    std::vector<Kant>        k;
    std::vector<glaze::Vec3> normal;
    for (std::size_t t = 0; t + 2 < d.indices.size(); t += 3) {
        const Index         i[3] = {d.indices[t], d.indices[t + 1], d.indices[t + 2]};
        const glaze::Point3 p0   = lage(d, i[0]);
        normal.push_back(
            enhet(kryss(minus(lage(d, i[1]), p0), minus(lage(d, i[2]), p0))));
        const auto tri = static_cast<Index>(t / 3);
        for (int e = 0; e < 3; ++e)
            k.push_back({std::min(i[e], i[(e + 1) % 3]), std::max(i[e], i[(e + 1) % 3]),
                         tri});
    }
    std::sort(k.begin(), k.end(), [](const Kant& x, const Kant& y) {
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    });
    const double               cosVeck = std::cos(kVeck * kPi / 180.0);
    std::vector<glaze::Point3> ut;
    for (std::size_t s = 0; s < k.size();) {
        std::size_t e = s + 1;
        while (e < k.size() && k[e].a == k[s].a && k[e].b == k[s].b)
            ++e;
        // Två trianglar: ett veck bara när de inte ligger i samma plan; en
        // eller fler än två: en öppen eller förgrenad kant, alltid en kant.
        if (e - s != 2 || prick(normal[k[s].tri], normal[k[s + 1].tri]) < cosVeck) {
            ut.push_back(lage(d, k[s].a));
            ut.push_back(lage(d, k[s].b));
        }
        s = e;
    }
    return ut;
}

// Lådan kring nätets lägen (miniatyrens kamera ramar in den).
glaze::Box3 omslutning(const glaze::MeshDesc& d)
{
    glaze::Box3 b{lage(d, 0), lage(d, 0)};
    for (Index i = 1; i < d.positions.size(); ++i) {
        const glaze::Point3 p = lage(d, i);
        b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
        b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
    }
    return b;
}

// ------------------------------------------------------------------ rutnätet

// Sträckan a–b klippt mot det konvexa polygonet (moturs i xy, som
// `visibleRegion` svarar) — falskt när ingenting av den ligger innanför.
bool klipp(const std::vector<glaze::Point3>& poly, glaze::Point& a, glaze::Point& b)
{
    double       t0 = 0.0, t1 = 1.0;
    const double dx = b.x - a.x, dy = b.y - a.y;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const glaze::Point3& p  = poly[i];
        const glaze::Point3& q  = poly[(i + 1) % poly.size()];
        const double         ex = q.x - p.x, ey = q.y - p.y;
        // Innanför kanten p→q: kryssprodukten ≥ 0, längs sträckan f0 + t·df.
        const double f0 = ex * (a.y - p.y) - ey * (a.x - p.x);
        const double df = ex * dy - ey * dx;
        if (df == 0.0) {
            if (f0 < 0.0)
                return false;
            continue;
        }
        if (df > 0.0)
            t0 = std::max(t0, -f0 / df);
        else
            t1 = std::min(t1, -f0 / df);
        if (t0 > t1)
            return false;
    }
    b = {a.x + t1 * dx, a.y + t1 * dy};
    a = {a.x + t0 * dx, a.y + t0 * dy};
    return true;
}

// RUTNÄTET för kameran som den står nu (med
// ritytans verktyg): planets punkt mitt i ytan, världsenheter per px där, och
// för varje nivå den del av planet kameran ser — vidgad — med en linje per
// steg klippt mot den. Ser kameran inte planet (eller har ritytan ingen yta
// än) blir lagret tomt.
std::vector<glaze::Shape> rutnat(const glaze::Canvas& k)
{
    std::vector<glaze::Shape> ut;
    const glaze::Plane3       plan{};   // z = 0, normalen uppåt
    const glaze::Camera3D     cam  = k.camera3D();
    // Orbitens mål står alltid mitt i ytan — strålen genom det når planet där.
    const std::optional<glaze::Point>  mitt = k.project(cam.target);
    const std::optional<glaze::Point3> c = mitt ? k.toWorld(*mitt, plan) : std::nullopt;
    const std::optional<double>        wpp = c ? k.worldPerPixel(*c) : std::nullopt;
    if (!wpp)
        return ut;
    const double fin = std::pow(10.0, std::ceil(std::log10(kMinPx * *wpp)));
    // Den fina nivåns täthet: kMinPx … 10 · kMinPx px isär ger 0 … 1.
    const double        ton  = std::clamp(std::log10(fin / *wpp / kMinPx), 0.0, 1.0);
    const glaze::Point3 oga  = cam.eye();
    const double        avst = std::sqrt(prick(minus(*c, oga), minus(*c, oga)));

    struct Niva {
        double     steg, rackvidd;
        gfx::Color farg;
        bool       grov;
    };
    const Niva nivaer[] = {
        {fin, kRackvidd * avst,
         gfx::Color{128, 128, 128, static_cast<std::uint8_t>(90.0 * ton)}, false},
        {10.0 * fin, 10.0 * kRackvidd * avst, gfx::Color{128, 128, 128, 150}, true}};
    // Axlarna (CAD-färgerna): x-axeln röd, y-axeln grön.
    const gfx::Color xAxel{210, 70, 60, 220}, yAxel{60, 160, 80, 220};

    for (const Niva& n : nivaer) {
        if (n.farg.a == 0)
            continue;
        std::vector<glaze::Point3> poly = k.visibleRegion(plan, n.rackvidd);
        if (poly.size() < 3)
            continue;
        glaze::Point3 m{};
        for (const glaze::Point3& p : poly)
            m = {m.x + p.x / poly.size(), m.y + p.y / poly.size(), 0.0};
        double x0 = m.x, x1 = m.x, y0 = m.y, y1 = m.y;
        for (glaze::Point3& p : poly) {
            p  = {m.x + kMarginal * (p.x - m.x), m.y + kMarginal * (p.y - m.y), 0.0};
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        const double tak = 0.5 * kMaxLinjer * n.steg;
        x0 = std::max(x0, c->x - tak);
        x1 = std::min(x1, c->x + tak);
        y0 = std::max(y0, c->y - tak);
        y1 = std::min(y1, c->y + tak);
        // En linje per steg i båda riktningarna; den fina nivån hoppar över
        // var tionde — den ritas i den grova.
        for (const bool langsY : {true, false}) {
            const double lo = langsY ? x0 : y0, hi = langsY ? x1 : y1;
            for (auto i = static_cast<long long>(std::ceil(lo / n.steg));
                 i <= static_cast<long long>(std::floor(hi / n.steg)); ++i) {
                if (!n.grov && i % 10 == 0)
                    continue;
                const double s = static_cast<double>(i) * n.steg;
                glaze::Point a = langsY ? glaze::Point{s, y0} : glaze::Point{x0, s};
                glaze::Point b = langsY ? glaze::Point{s, y1} : glaze::Point{x1, s};
                if (!klipp(poly, a, b))
                    continue;
                const glaze::Stroke linje{
                    .paint = n.grov && i == 0 ? (langsY ? yAxel : xAxel) : n.farg,
                    .width = glaze::px(1.0f)};
                ut.push_back(glaze::Shape::line3({a.x, a.y, 0.0}, {b.x, b.y, 0.0}, linje));
            }
        }
    }
    return ut;
}

// ------------------------------------------------------------------- kuben

// KUBENS REGION ur träffpunkten: en axel där punkten
// ligger nära sidan räknas med sitt tecken — en axel är en sida, två en kant
// och tre ett hörn (26 regioner).
std::array<int, 3> region(const glaze::Point3& p)
{
    const auto axel = [](double v) {
        return std::abs(v) >= kRegion * kKubHalv ? (v > 0.0 ? 1 : -1) : 0;
    };
    return {axel(p.x), axel(p.y), axel(p.z)};
}

// KAMERAN FÖR REGIONEN: ögat på regionens sida och blicken mot kubens mitt —
// azimut och elevation ur riktningen, målet och avståndet orörda. Rakt
// uppifrån och underifrån står norr uppåt.
glaze::Camera3D standardvy(glaze::Camera3D k, const std::array<int, 3>& r)
{
    const glaze::Vec3 n = enhet({double(r[0]), double(r[1]), double(r[2])});
    // Blicken är −n; azimuten är dess kompassriktning (0 = norr, medurs).
    k.azimuth   = r[0] == 0 && r[1] == 0
                      ? 0.0f
                      : static_cast<float>(std::atan2(-n.x, -n.y) * 180.0 / kPi);
    k.elevation = static_cast<float>(std::asin(n.z) * 180.0 / kPi);
    return k;
}

// KUBENS ETIKETTER för kameran: bara sidorna som VÄNDER SIG MOT ögat — en
// etikett står upprätt i skärmen och hade annars stuckit ut över kanten mot
// grannsidan, eller lyst igenom kuben från baksidan. Lagret ligger på MARKEN
// i vyn, så etiketterna ritas överst (canvas.h) och ingen sida skymmer dem.
std::vector<glaze::Shape> kubEtiketter(const glaze::Camera3D& k)
{
    const struct {
        glaze::Vec3   n;
        std::uint32_t text;
    } sidor[] = {{{0, 0, 1}, demo::str::KubOvan},     {{0, 0, -1}, demo::str::KubUnder},
                 {{0, -1, 0}, demo::str::KubFram},    {{0, 1, 0}, demo::str::KubBak},
                 {{-1, 0, 0}, demo::str::KubVanster}, {{1, 0, 0}, demo::str::KubHoger}};
    // Riktningen mot ögat ur kompassens azimut och elevationen (camera.h).
    const double      az = k.azimuth * kPi / 180.0, el = k.elevation * kPi / 180.0;
    const glaze::Vec3 mot{-std::sin(az) * std::cos(el), -std::cos(az) * std::cos(el),
                          std::sin(el)};
    const glaze::TextStyle stil{.size    = glaze::px(11.0f),
                                .bold    = true,
                                .color   = gfx::Color{30, 32, 38, 255},
                                .halo    = gfx::Color{255, 255, 255, 255},
                                .collide = false};
    const double              ut = kKubHalv + kKubLyft;
    std::vector<glaze::Shape> former;
    for (const auto& s : sidor)
        if (prick(s.n, mot) > kKubVand)
            former.push_back(glaze::Shape::text3({s.n.x * ut, s.n.y * ut, s.n.z * ut},
                                                 glaze::tr(s.text), stil));
    return former;
}

// ALLT SOM FÖLJER KAMERAN: rutnätet och kubens etiketter.
void foljKameran(glaze::Canvas& k)
{
    k.setShapes(kRutnat, rutnat(k));
    k.setShapes(kKubText, kubEtiketter(k.camera3D()));
}

} // namespace

void WidgetGallery::tredeVerktygFlik(glaze::TabPage* sida)
{
    // === 28 · Canvas — 3D-verktyg ==================
    auto* g = grupp(sida, demo::str::G28Verktyg, "g28", 12, 12, 1252, 484);

    auto* k      = g->add<glaze::Canvas>();
    k->x         = 8;
    k->anchorTop = {"g28Sep", 10};
    k->anchors   = {true, true, true, false};
    k->right     = 8 + kMiniYta + 8;   // miniatyrens kolumn till höger
    k->height    = 405;
    k->name      = "verktyg3d";
    verktyg3d_   = k;

    // KAMERAN: orbit med FRI TUMLING kring stolptoppen.
    k->setCamera(glaze::Camera3D{.target    = {0, 0, 1.7},
                                 .distance  = 7.5,
                                 .azimuth   = 35,
                                 .elevation = 22,
                                 .fov       = 40,
                                 .farPlane  = kBortre});
    k->setPannable(true);
    k->setZoomable(true);
    k->setRotatable(true);
    k->setPitchable(true);
    k->setOrbitTumble(true);
    k->setDistanceRange(0.5, 400);
    // LJUSET snett ovanifrån från VÄSTSYDVÄST, bakom kameran: de tre sidor
    // man ser från start får var sin ton. Högre omgivning än förvalet —
    // konstruktionens baksidor ska gå att läsa.
    k->setLighting({.direction = {0.45, 0.3, -0.84}, .ambient = 0.5f, .diffuse = 0.6f});

    k->setLayers({glaze::tr(demo::str::Lager3dRutnat), glaze::tr(demo::str::Lager3dModell),
                  glaze::tr(demo::str::Lager3dKanter), glaze::tr(demo::str::Lager3dDolda),
                  glaze::tr(demo::str::Lager3dKub), glaze::tr(demo::str::LagerEtiketter)});

    // RUTNÄTET skriver inget djup: modellen skyms aldrig av det, men täcker
    // det där den står framför. Linjerna kommer i `verktygKamera`/`verktygTick`.
    k->setLayerRenderState(kRutnat, glaze::RenderState{.depthWrite = false});

    // MODELLEN — med djupbias bakåt (som glPolygonOffset(1, 1)), så att de
    // synliga kanterna i samma djup vinner LessEqual.
    glaze::MeshDesc d = stolptopp();
    modellLada_       = omslutning(d);
    const std::vector<glaze::Point3> kant = kanter(d);
    k->setLayerRenderState(kModell, glaze::RenderState{.depthBias = 1, .slopeBias = 1});
    k->addShape(kModell, glaze::Shape::mesh(glaze::Mesh(std::move(d))));

    // KANTLÄGET: samma kanter två gånger, utan djupskrivning — de synliga
    // (LessEqual) mörka och de dolda bakom fyllningen (Greater) ljusa och
    // streckade.
    const glaze::RenderState framfor{.depth      = glaze::DepthTest::LessEqual,
                                     .depthWrite = false};
    const glaze::RenderState bakom{.depth = glaze::DepthTest::Greater, .depthWrite = false};
    k->setLayerRenderState(kKanter, framfor);
    k->setLayerRenderState(kDolda, bakom);
    const glaze::Stroke synlig{.paint = gfx::Color{24, 24, 30, 255},
                               .width = glaze::px(1.5f),
                               .cap   = glaze::LineCap::Round};
    const glaze::Stroke dold{.paint = gfx::Color{120, 124, 136, 200},
                             .width = glaze::px(1.0f),
                             .dash  = {4.0f, 3.0f}};
    for (std::size_t i = 0; i + 1 < kant.size(); i += 2) {
        k->addShape(kKanter, glaze::Shape::line3(kant[i], kant[i + 1], synlig));
        k->addShape(kDolda, glaze::Shape::line3(kant[i], kant[i + 1], dold));
    }

    // NAVIGERINGSKUBEN — en låda och dess etiketter i två egna lager, som
    // bara ritas i del-vyn uppe till höger: ortografisk, och vriden som
    // huvudvyn. Etiketterna står på MARKEN (överst) och följer kameran.
    k->addShape(kKub, glaze::Shape::box({{-kKubHalv, -kKubHalv, -kKubHalv},
                                         {kKubHalv, kKubHalv, kKubHalv}},
                                        {.color = gfx::Color{200, 208, 222, 255}}));
    k->setLayerPlacement(kKubText, glaze::LayerPlacement::Ground);
    k->setShapes(kKubText, kubEtiketter(k->camera3D()));
    const int kub = k->addView(glaze::View{
        .anchor         = glaze::TextAnchor::TopRight,
        .offset         = {-8, 8},
        .width          = 120,
        .height         = 120,
        .camera         = glaze::Camera3D{.distance    = 10,
                                          .projection  = glaze::Projection::Orthographic,
                                          .orthoHeight = 1.9},
        .followRotation = true});
    k->setLayerViews(kKub, {kub});
    k->setLayerViews(kKubText, {kub});

    k->onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
        verktygNed(s, e);
    };
    k->onCameraChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
        verktygKamera(s, e);
    };
    k->onTick = [this](glaze::Widget* s, glaze::TickArgs& e) { verktygTick(s, e); };
    // Rutnätet behöver ytan, som ritytan känner först när den ritats: den
    // första ritningen tickar, och ticken bygger det (verktygTick).
    k->setTicking(true);

    // KONTROLLERNA i kolumnen till höger: kantläget och miniatyren.
    auto* kl       = g->add<glaze::Checkbox>(glaze::tr(demo::str::Kantlage));
    kl->anchorLeft = {"verktyg3d", 8};
    kl->anchorTop  = {"g28Sep", 10};
    kl->name       = "kantlage";
    kl->setChecked(true);
    kl->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { kantlageAndrat(s, e); };

    auto* mb       = g->add<glaze::Button>(glaze::tr(demo::str::Miniatyr));
    mb->anchorLeft = {"verktyg3d", 8};
    mb->anchorTop  = {"kantlage", 10};
    mb->name       = "miniatyrBtn";
    mb->onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { miniatyrKlickad(s, e); };

    // MINIATYREN visas på en rityta UTAN kamera — ritytans lokala logiska px.
    auto* mini       = g->add<glaze::Canvas>();
    mini->anchorLeft = {"verktyg3d", 8};
    mini->anchorTop  = {"miniatyrBtn", 8};
    mini->width      = kMiniYta;
    mini->height     = kMiniYta;
    mini->name       = "miniatyr";
    miniatyr_        = mini;

    fot(g, demo::str::FotVerktyg, "verktyg3d", 8);
}

// ---------------------------------------------------------------- handlers

void WidgetGallery::verktygNed(glaze::Widget* sender, glaze::CanvasMouseArgs& e)
{
    if (e.button != gfx::MouseButton::Left)
        return;
    // KUBEN: plockningen routas till den översta vyn under pekaren, och det
    // närmaste den träffar — en sida eller en etikett på kuben — ger
    // regionen. Trycket TAS, så ingen orbit startar; utanför kuben orbitar
    // draget som vanligt.
    auto* k = static_cast<glaze::Canvas*>(sender);
    for (const glaze::HitResult& h : k->hitTest(e.local, glaze::HitOptions{})) {
        if (h.view == 0 || !h.point ||
            (h.kind != glaze::HitKind::Surface && h.kind != glaze::HitKind::Label))
            continue;
        k->easeTo(standardvy(k->camera3D(), region(*h.point)), kGlidMs);
        k->setTicking(true);   // rutnätet följer glidningen (verktygTick)
        e.handled = true;
        return;
    }
}

void WidgetGallery::verktygKamera(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Användaren flyttade kameran — rutnätet och kubens etiketter följer.
    foljKameran(*static_cast<glaze::Canvas*>(sender));
}

void WidgetGallery::verktygTick(glaze::Widget* sender, glaze::TickArgs&)
{
    // Efter första ritningen och medan kuben glider. När kameran står still
    // slutar tickningen, och ritytan ber inte om fler bildrutor (regel 1).
    auto* k = static_cast<glaze::Canvas*>(sender);
    foljKameran(*k);
    if (!k->isEasing())
        k->setTicking(false);
}

void WidgetGallery::kantlageAndrat(glaze::Widget* sender, glaze::ChangeArgs&)
{
    const bool pa = static_cast<glaze::Checkbox*>(sender)->isChecked();
    verktyg3d_->setLayerVisible(kKanter, pa);
    verktyg3d_->setLayerVisible(kDolda, pa);
}

void WidgetGallery::miniatyrKlickad(glaze::Widget*, glaze::ClickArgs&)
{
    // MINIATYRENS KAMERA: lådans mitt, 45° och 30°, 2,5 radier bort — och
    // utan rutnätet, som hör till arbetsytan och inte till modellen.
    const glaze::Box3&  b    = modellLada_;
    const glaze::Point3 mitt = {(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2,
                                (b.min.z + b.max.z) / 2};
    const double        radie = 0.5 * std::sqrt(prick(minus(b.max, b.min),
                                                       minus(b.max, b.min)));
    const glaze::Camera3D kamera{.target    = mitt,
                                 .distance  = kMiniRadier * radie,
                                 .azimuth   = kMiniAzimut,
                                 .elevation = kMiniElev};
    verktyg3d_->setLayerVisible(kRutnat, false);
    const std::optional<glaze::Bitmap> bild =
        verktyg3d_->renderToBitmap(kMiniPx, kMiniPx, kamera);
    verktyg3d_->setLayerVisible(kRutnat, true);
    if (!bild) {
        statusTxt_->setText(glaze::tr(demo::str::MiniatyrFel));
        return;
    }
    miniatyr_->setShapes({glaze::Shape::image({0, 0, kMiniYta, kMiniYta}, *bild)});
    statusTxt_->setText(trs(demo::str::MiniatyrKlar) + std::to_string(bild->width()) +
                        " × " + std::to_string(bild->height()) + " px");
}
