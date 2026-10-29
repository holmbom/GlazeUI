// trackpad_gate — STYRPLATTAN: tvåfingersdraget, nypningen, vridningen och
// vridsnäppet på ritytan, och
// den backend-neutrala kontaktspåraren som gör råa kontakter till grader.
//
// Kör:  ./build/tests/trackpad_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// GESTERNA ÄR ROUTING OCH KAMERA, och därför matar grinden FÖNSTRETS ingång
// (`AppWindow::handleEvent`, canvas_gate-prejudikatet) i ett DOLT fönster med
// riktiga `glaze::Canvas` — ett precist hjul och en `Gesture`-sekvens går
// exakt den väg backenden lämnar dem. Förväntat värde är KAMERANS EGEN GEOMETRI läst
// genom ritytans publika väg (`toWorld`/`toLocal`) och handräknade tal —
// aldrig gesternas formel:
//   * panoreringen: världspunkten under pekaren står (−wheelX, +wheelY) ×
//     `gfx::kPrecisePxPerRow` logiska px bort efteråt — kontraktets mening i
//     gfx/events.h, skriven som ett tal och inte ur camera_math;
//   * nypningen och vridningen: världspunkten under pekaren står kvar, och en
//     punkt till höger om den hamnar NEDANFÖR efter en positiv vridning — det
//     ÄR medurs på skärmen, räknat med ren plangeometri;
//   * kontaktspåraren: vinklar och grader ur handplacerade punkter.
// Hela fönsterdelen körs vid TVÅ skalor, 1 och 1,5 (användarskalan räknas ur
// ett provfönsters OS-skala): en panorering som glömt fönstrets skala är rätt
// vid 1 och fel vid 1,5.
//
// VAD GRINDEN PÅSTÅR:
//   K   kontaktspåraren headless: två kontakter ⇒ vinkel (medurs med y
//       nedåt), tröskeln innan vridningen griper (det som samlats före den
//       levereras INTE), ±180°-omslaget, en handflata (utan Confidence)
//       räknas inte, hybridläget (ramen samlas tills Contact Count uppnåtts)
//       och NY BASLINJE när en kontakt kommer eller går (tröskeln igen).
//   V   vridsnäppet headless (camera_math): 88° → 90 med steget 90, 80° står
//       kvar med toleransen 7, 355° (också 715°) → norr, 268° → 270,
//       steget 0 snäpper inte, steget 360 bara mot norr, ett steg som inte
//       delar varvet, giltiga värden och förvalen.
//   H   precisePanPx: innehållet flyttar (−wheelX, +wheelY) × raden × skalan.
//   P   tvåfingersdraget panorerar EXAKT i px — platt och vridet/tiltat — och
//       fyrar onCameraChange; samma tecken som en rullad lista (en Panel som
//       får samma hjul flyttar sitt innehåll åt samma håll).
//   W   mushjulet (precise = false) zoomar fortfarande, och styrplattans drag
//       zoomar ALDRIG; onMouseWheel får `precise`, och appens `event()`-krok
//       ser `isPreciseWheel()`/`wheelPhase()` redan satta.
//   Z   nypningen zoomar med kvoternas PRODUKT och världspunkten under
//       pekaren står kvar — platt och tiltat; onCameraChange per steg.
//   R   vridningen vrider runt pekaren (punkten under den står kvar), MEDURS
//       på skärmen för en positiv vridning, bäringen −grader; tiltat likaså.
//   N   snäppet när en VRIDANDE gest släpps: förvalet glider till norr,
//       steget 90 tar 88° → 90° och lämnar 80°, steget 0 och steget 360
//       (88°) snäpper inte — och Ctrl+draget snäpper med samma steg.
//   F   pannable/zoomable/rotatable av: kameran står still och hjulet
//       BUBBLAR (den rullande föräldern tar det); en nypning utan zoom men
//       med vridning vrider bara.
//   M   gestmålet HÅLLS från Begin till End fast en annan widget kommer under
//       pekaren, en Update utan Begin väljer som en Begin, en inaktiverad
//       får inga Update — och målet SLÄPPS när det tas bort ur trädet.
//   A   inget animerar efter End (ingen tröghet, inget självinvaliderande)
//       utom snäppets glidning, som landar EXAKT och stannar.
//   O   ORBITLÄGET med en Camera3D, mot grindens egen
//       hålkamera: tvåfingersdraget panorerar (raden × px; sidled följer
//       målplanets punkt, fram/bak flyger längs blicken), Ctrl +
//       tvåfingersdrag orbitar som ett
//       vänsterdrag med Ctrl ur HJULETS EGEN MASK — efter ett tangenttryck
//       som inte bar den (X11-/Wayland-formen) — och efter släppet
//       panorerar draget igen fast tangenteventet lämnade Ctrl kvar;
//       hjulets och gestens mask är fönstrets `modifierKeys()` FÖRE appens
//       krok (och i onGesture); nypningen dollyar med kvoternas produkt
//       (och spannet hoppar inte), vridningen ger azimut kring målet utan
//       snäpp, och med gesterna av står kameran still och draget bubblar.
//   B   BACKENDENS MASK — där det går att köra: på
//       WINDOWS riktiga WM_MOUSEWHEEL/WM_MOUSEHWHEEL genom fönstrets WndProc
//       med trådens tangenttabell satt (`SetKeyboardState`): Ctrl+Shift nere
//       ⇒ masken bär båda och `modifierKeys()` har dem i kroken, uppe ⇒ 0.
//       På LINUX `gfx::xkbModsAfterKey` (modifierartangentens eget event bär
//       läget EFTER sig) mot RIKTIGA keymaps: Ctrl/Shift/Alt/Super vänster
//       och höger lägger till och tar bort sin bit, en annan hållen
//       modifierare står kvar, vanliga tangenter rör inget, och svensk AltGr
//       (`ISO_Level3_Shift`) är inte Alt.
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * att backenderna LEVERERAR eventen — Direct Manipulation och de råa
//     HID-rapporterna på Windows, macOS scrollWheel/magnify/rotate, Wayland
//     pointer-gestures och X11 XI2: grinden matar ingången direkt
//     (doc/os/*/TESTS.md);
//   * MASKEN på X11:s och Waylands hjul och nypning (XI2 `mods.effective`,
//     `mods_`) och på styrplattans väg genom Direct Manipulation — den
//     senare går genom samma `pushWheel`/`pushGesture` som WndProc:en (B),
//     men själva anropet kräver en riktig platta; macOS (`modifierFlags`,
//     `flagsChanged:`) mäts inte här. Rader i doc/os/*/TESTS.md;
//   * att Win32 uppdaterar trådens tangenttabell när en riktig Ctrl trycks
//     (OS:ets dokumenterade beteende — tabellen sätts här för hand), och att
//     en riktig Ctrl-TANGENT på Linux ger sitt event med biten (drivrutinens
//     anrop av hjälparen mäts inte, bara hjälparen);
//   * HidP_*-tolkningen och Direct Manipulations tillståndsmaskin
//     (d3d11_touchpad.cpp) — de kräver en riktig precisionsplatta;
//   * känslan: tvåfingersdragets fart i en lista, OS:ets tröghet.
//
// SABOTAGEN den fäller (var och en ensam):
// panoreringens tecken vänt (H, P), nypningen kring ytans mitt i
// stället för pekaren (Z), vridningen kring mitten (R), vridningen åt fel
// håll (R), `precise` ignorerat — styrplattan zoomar (P, W), panoreringen
// som glömmer fönstrets skala (H, P — bara vid skalan 1,5), Confidence
// ignorerad (K), gestmålet som inte släpps i `detachTree` (M), vinkeln utan
// modulo (V — 715°) och toleransen ignorerad (V, R, N). MASKEN
// (var och en ensam): D3D11:s `pushWheel` utan `currentMods`
// (B Win32), `AppWindow` som inte läser hjulets mask (O ×3, B Win32) eller
// gestens (O), och på Linux `xkbModsAfterKey` som ignorerar tangentens bit
// (B, 8 rader) eller ger Super Alt-biten (B). INTE fällt: `pushGesture` utan
// mask — den nås bara ur Direct Manipulation (se ovan).

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gfx/camera.h>
#include <glaze/glaze.h>

#include "canvas/camera_math.h"   // intern — vridsnäppet och panoreringen (V, H)
#include "orbit_fly.h"            // flygläget som bindningar (O)
#include "touchpad_rotation.h"    // intern (Glaze/gfx/src) — kontaktspåraren (K)

#ifdef GLAZE_BACKEND_D3D11
// Samma vakter som backendens d3d11_backend.h (pump_gate-prejudikatet):
// MinGW:s libstdc++ definierar redan NOMINMAX.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef GLAZE_BACKEND_LINUX
// Backendens INTERNA masköversättning (del B) — Glaze/gfx/src står redan på
// sökvägen; xkb_keys.h drar in xkbcommon.h (numpad_gate-prejudikatet).
#include "backend_linux/xkb_keys.h"
#endif

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string tal(double v)
{
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

std::string pt(const glaze::Point& p)
{
    return "(" + tal(p.x) + ", " + tal(p.y) + ")";
}

bool nära(double a, double b, double e)
{
    return std::abs(a - b) <= e;
}

bool näraP(const glaze::Point& a, const glaze::Point& b, double e)
{
    return nära(a.x, b.x, e) && nära(a.y, b.y, e);
}

std::string svar(const std::optional<float>& v)
{
    return v ? tal(*v) : std::string("tomt");
}

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------- grindens hålkamera (del O, orbiten)
// Skriven ur konventionen i glaze/canvas/camera.h, aldrig ur camera_math:
// azimuten är kompassen blicken pekar mot, elevationen hur högt ögat står,
// upp är riktningen ögat rör sig när elevationen ökar. Z upp, ingen roll.
struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Hal {
    V3         eye, f, r, u;
    double     brännvidd = 0.0;   // px: halva höjden / tan(fov/2)
    gfx::Rectf box{};
};

Hal hal(const glaze::Camera3D& k, gfx::Rectf box)
{
    const double az = k.azimuth * kPi / 180.0, el = k.elevation * kPi / 180.0;
    Hal          h;
    h.f   = {std::sin(az) * std::cos(el), std::cos(az) * std::cos(el), -std::sin(el)};
    h.u   = {std::sin(az) * std::sin(el), std::cos(az) * std::sin(el), std::cos(el)};
    h.r   = cross(h.f, h.u);
    h.eye = V3{k.target.x, k.target.y, k.target.z} - h.f * k.distance;
    h.brännvidd = (box.h / 2.0) / std::tan(k.fov * kPi / 360.0);
    h.box       = box;
    return h;
}

// Perspektivets skärmläge (fysiska px).
void halProj(const Hal& h, V3 p, double& px, double& py)
{
    const V3     q = p - h.eye;
    const double s = h.brännvidd / dot(q, h.f);
    px             = h.box.x + h.box.w / 2.0 + s * dot(q, h.r);
    py             = h.box.y + h.box.h / 2.0 - s * dot(q, h.u);
}

// Målplanets punkt (genom målet, vinkelrätt mot blicken) under en pixel.
V3 målplanspunkt(const Hal& h, V3 mål, double px, double py)
{
    const double cx = h.box.x + h.box.w / 2.0, cy = h.box.y + h.box.h / 2.0;
    const V3     d  = h.f + h.r * ((px - cx) / h.brännvidd) -
                h.u * ((py - cy) / h.brännvidd);
    return h.eye + d * (dot(mål - h.eye, h.f) / dot(d, h.f));
}

// ================================================================ HEADLESS

// K — KONTAKTSPÅRAREN. Ett finger på radien 100 från origo, i vinkeln
// `grader` medurs (y nedåt): (100 cos, 100 sin).
gfx::touchpad::Contact finger(std::uint32_t id, double x, double y,
                              bool tip = true, bool conf = true)
{
    gfx::touchpad::Contact c;
    c.id         = id;
    c.x          = x;
    c.y          = y;
    c.tip        = tip;
    c.confidence = conf;
    return c;
}

gfx::touchpad::Contact påRadien(std::uint32_t id, double grader)
{
    const double r = grader * kPi / 180.0;
    return finger(id, 100.0 * std::cos(r), 100.0 * std::sin(r));
}

void fallK()
{
    using namespace gfx::touchpad;
    // K1 — vinkeln: medurs med y nedåt.
    double v1 = 0.0, v2 = 0.0, v3 = 0.0;
    const bool a1 = angleDeg(finger(1, 0, 0), finger(2, 10, 10), v1);
    const bool a2 = angleDeg(finger(1, 0, 0), finger(2, 0, 10), v2);
    const bool a3 = angleDeg(finger(1, 5, 5), finger(2, 5, 5), v3);
    check(a1 && nära(v1, 45.0, 1e-9) && a2 && nära(v2, 90.0, 1e-9) && !a3,
          "K1 vinkeln ur två kontakter: (10, 10) är 45°, rakt NED är +90° "
          "(medurs), sammanfallande punkter har ingen",
          tal(v1) + " / " + tal(v2));
    check(nära(wrapDeltaDeg(170.0, -170.0), 20.0, 1e-9) &&
              nära(wrapDeltaDeg(-170.0, 170.0), -20.0, 1e-9) &&
              nära(wrapDeltaDeg(10.0, 30.0), 20.0, 1e-9),
          "K1 skillnaden viks till (−180°, 180°]");

    // K2 — TRÖSKELN: 1° per ram; vridningen griper vid 3,5° (≥ 3°) och den
    // ramen HINNER IKAPP (3,5°) — därefter varje ram.
    RotationTracker t;
    std::vector<Contact> ram{finger(1, 0, 0), påRadien(2, 0)};
    RotationStep s = t.update(ram);
    bool tystFöre  = !s.began && s.degrees == 0.0 && !t.gripped();
    for (double g : {1.0, 2.0}) {
        ram[1] = påRadien(2, g);
        s      = t.update(ram);
        tystFöre = tystFöre && !s.began && s.degrees == 0.0;
    }
    ram[1] = påRadien(2, 3.5);
    s      = t.update(ram);
    const bool   grep      = s.began && nära(s.degrees, 3.5, 1e-9) && t.gripped();
    const double greppRam  = s.degrees;
    double       levererat = 0.0;
    for (double g : {8.0, 12.0}) {
        ram[1] = påRadien(2, g);
        s      = t.update(ram);
        levererat += s.degrees;
    }
    check(tystFöre, "K2 under tröskeln levereras ingenting");
    check(grep, "K2 vridningen GRIPER vid 3,5° — och greppets ram hinner ikapp "
                "fingrarna (3,5°)", tal(greppRam));
    check(nära(levererat, 8.5, 1e-9),
          "K2 efter greppet levereras varje ram: 3,5° → 12° ger 8,5°",
          tal(levererat));

    // K3 — ±180°-OMSLAGET: linjen passerar 180° — små positiva steg, inget
    // varvs hopp.
    t.reset();
    ram = {finger(1, 0, 0), påRadien(2, 170.0)};
    t.update(ram);
    for (double g : {176.0, 182.0}) {
        ram[1] = påRadien(2, g);
        t.update(ram);
    }
    ram[1]             = påRadien(2, 188.0);   // atan2 svarar −172°
    s                  = t.update(ram);
    check(t.gripped() && nära(s.degrees, 6.0, 1e-9),
          "K3 vridningen över ±180° är +6°, inte −354°", tal(s.degrees));

    // K4 — HANDFLATAN: en kontakt utan Confidence räknas inte. Två fingrar
    // och en vandrande handflata vrider som två fingrar …
    t.reset();
    ram = {finger(1, 0, 0), påRadien(2, 0), finger(3, -40, 30, true, false)};
    t.update(ram);
    for (double g : {6.0, 12.0}) {
        ram[1] = påRadien(2, g);
        ram[2] = finger(3, -40 + g, 30 - g, true, false);
        t.update(ram);
    }
    ram[1] = påRadien(2, 15.0);
    s      = t.update(ram);
    check(t.gripped() && nära(s.degrees, 3.0, 1e-9),
          "K4 två fingrar och en handflata (utan Confidence) vrider som två "
          "fingrar", tal(s.degrees));
    // … och ett finger med en handflata är ingen vridning alls.
    RotationTracker h;
    bool aldrig = true;
    for (double g = 0.0; g <= 60.0; g += 6.0) {
        const std::vector<Contact> r{finger(1, 0, 0),
                                     [&] {
                                         Contact c = påRadien(2, g);
                                         c.confidence = false;
                                         return c;
                                     }()};
        const RotationStep st = h.update(r);
        aldrig = aldrig && !st.began && st.degrees == 0.0;
    }
    check(aldrig && !h.gripped(),
          "K4 ett finger och en handflata vrider aldrig, hur långt den än går");

    // K5 — HYBRIDLÄGET: plattan har två platser per rapport och tre fingrar.
    FrameAssembler fa;
    const std::vector<Contact> r1{finger(1, 1, 1), finger(2, 2, 2)};
    const std::vector<Contact> r2{finger(3, 3, 3), finger(99, 9, 9, false)};
    const bool färdig1 = fa.add(3, r1);
    const bool färdig2 = fa.add(0, r2);
    const auto f       = fa.frame();
    check(!färdig1 && färdig2 && f.size() == 3 && f[0].id == 1 && f[2].id == 3,
          "K5 hybridläget: räknaren 3 i första rapporten och 0 i nästa — ramen "
          "är komplett med tre kontakter, och nästa rapports tomma plats tas "
          "inte med",
          std::to_string(f.size()) + " kontakter");
    const std::vector<Contact> parallell{finger(1, 1, 1), finger(2, 2, 2),
                                         finger(7, 0, 0, false),
                                         finger(8, 0, 0, false)};
    const bool p1 = fa.add(2, parallell);
    check(p1 && fa.frame().size() == 2,
          "K5 parallellt läge: räknaren 2 av fyra platser — två kontakter");
    const bool tom = fa.add(0, r1);
    check(tom && fa.frame().empty(),
          "K5 räknaren 0 utan påbörjad ram är en ram UTAN kontakter");
    fa.add(3, r1);   // en ram som aldrig blir klar …
    const bool omstart = fa.add(2, r1);   // … och en ny börjar
    check(omstart && fa.frame().size() == 2,
          "K5 en ny räknare mitt i en ofärdig ram börjar om");

    // K6 — NY BASLINJE när en kontakt kommer eller går.
    t.reset();
    ram = {finger(1, 0, 0), påRadien(2, 0)};
    t.update(ram);
    ram[1] = påRadien(2, 12.0);
    t.update(ram);   // gripen
    std::vector<Contact> tre = ram;
    tre.push_back(finger(3, 50, 50));
    s = t.update(tre);
    const bool slutTre = s.ended && !t.gripped() && s.degrees == 0.0;
    ram[1] = påRadien(2, 40.0);   // fingret har flyttat medan det var tre
    s      = t.update(ram);
    const bool baslinje = !s.began && s.degrees == 0.0 && !s.ended;
    ram[1] = påRadien(2, 42.0);
    s      = t.update(ram);
    check(slutTre, "K6 ett tredje finger: den gripna vridningen tar slut");
    check(baslinje && !s.began && s.degrees == 0.0,
          "K6 tillbaka till två: ny baslinje där fingrarna står nu, och "
          "tröskeln gäller igen (2° griper inte)");
    std::vector<Contact> byte{finger(1, 0, 0), påRadien(3, 50.0)};
    ram[1] = påRadien(2, 60.0);
    t.update(ram);   // nu gripen igen (40 → 60 = 20°)
    const bool gripenIgen = t.gripped();
    s = t.update(byte);   // finger 2 lyft och finger 3 nedsatt i SAMMA ram
    check(gripenIgen && s.ended && !t.gripped(),
          "K6 ett finger byts mot ett annat i samma ram: slut och ny baslinje");
    // K7 — ett finger som lyfts (tip = false) är borta.
    t.reset();
    ram = {finger(1, 0, 0), påRadien(2, 0)};
    t.update(ram);
    ram[1] = påRadien(2, 12.0);
    t.update(ram);
    ram[1].tip = false;
    s          = t.update(ram);
    check(s.ended && !t.gripped(), "K7 ett finger som lyfts avslutar vridningen");
    // K8 — rapportens ordning vänder inte vinkeln.
    t.reset();
    t.update(std::vector<Contact>{påRadien(2, 0), finger(1, 0, 0)});
    t.update(std::vector<Contact>{finger(1, 0, 0), påRadien(2, 12.0)});
    s = t.update(std::vector<Contact>{påRadien(2, 14.0), finger(1, 0, 0)});
    check(t.gripped() && nära(s.degrees, 2.0, 1e-9),
          "K8 vinkeln räknas från det lägre id:t — ordningen i rapporten "
          "spelar ingen roll", tal(s.degrees));

    // K9 — TUR OCH RETUR (snäppet måste fånga efter
    // "vrid lite och tillbaka"): fingrarna 0° → 25° → 0° lämnar kartan EXAKT
    // där den började, och vidare till −25° vrider den −25°.
    t.reset();
    ram = {finger(1, 0, 0), påRadien(2, 0)};
    t.update(ram);
    double summa = 0.0, toppen = 0.0;
    for (double g = 5.0; g <= 25.0; g += 5.0) {
        ram[1] = påRadien(2, g);
        summa += t.update(ram).degrees;
    }
    toppen = summa;
    for (double g = 20.0; g >= 0.0; g -= 5.0) {
        ram[1] = påRadien(2, g);
        summa += t.update(ram).degrees;
    }
    const double tillbaka = summa;
    for (double g = -5.0; g >= -25.0; g -= 5.0) {
        ram[1] = påRadien(2, g);
        summa += t.update(ram).degrees;
    }
    check(nära(toppen, 25.0, 1e-9) && nära(tillbaka, 0.0, 1e-9) &&
              nära(summa, -25.0, 1e-9) && t.gripped(),
          "K9 kartan följer fingrarnas totala vridning: 0° → 25° vrider 25°, "
          "tillbaka till 0° står den EXAKT där den började, och −25° vrider −25°",
          tal(toppen) + " / " + tal(tillbaka) + " / " + tal(summa));

    // K10 — EN LITEN VRIDNING NÅR FRAM ("även om jag bara vrider 5
    // grader … så snäpp"): 0° → 5° i grader om 1 levererar 5°.
    t.reset();
    ram = {finger(1, 0, 0), påRadien(2, 0)};
    t.update(ram);
    double liten = 0.0;
    for (double g = 1.0; g <= 5.0; g += 1.0) {
        ram[1] = påRadien(2, g);
        liten += t.update(ram).degrees;
    }
    check(nära(liten, 5.0, 1e-9) && t.gripped(),
          "K10 en vridning på 5° når kartan som 5°", tal(liten));

    // K11 — KLASSAREN (panorering stänger zoom och vridning,
    // och zoom och vridning sker aldrig samtidigt). Fingrarna börjar 40 isär;
    // beslutet faller vid 6 (kDecideFraction 0,15).
    using M = TwoFingerMode;
    const auto kör = [](std::vector<std::pair<Contact, Contact>> steg) {
        TwoFingerClassifier kl;
        ClassifyStep        s;
        bool                beslut = false;
        for (const auto& [a, b] : steg) {
            s      = kl.update(std::vector<Contact>{a, b});
            beslut = beslut || s.decided;
        }
        return std::pair{kl.mode(), beslut};
    };
    const auto par = [](double ax, double ay, double bx, double by) {
        return std::pair{finger(1, ax, ay), finger(2, bx, by)};
    };
    // Ren panorering: båda fingrarna 8 åt höger (avståndet darrar 1).
    const auto [pan, panB] = kör({par(0, 0, 40, 0), par(4, 0, 44.5, 0),
                                  par(8, 0, 49, 0)});
    // Nypning kring mitten: 40 → 56 isär.
    const auto [nyp, nypB] = kör({par(0, 0, 40, 0), par(-8, 0, 48, 0)});
    // Nypning med ETT finger stilla: det andra drar ut 15 — mittpunkten rör
    // sig lika långt som zoomen (7,5 och 7,5), och gesten menar zoom.
    const auto [ankar, ankarB] = kör({par(0, 0, 40, 0), par(0, 0, 55, 0)});
    // NYPNING SOM VRIDER LITE ("svårt att få till zoom"): 40 → 56
    // isär och 10° vridning — zoomen 8 mot bågen 4,2. Zoom.
    const double r10 = 10.0 * kPi / 180.0;
    const auto [nypVrid, nvB] =
        kör({par(0, 0, 40, 0), par(20 - 28 * std::cos(r10), -28 * std::sin(r10),
                                   20 + 28 * std::cos(r10), 28 * std::sin(r10))});
    // Vridning kring ett stilla finger: det andra går 20° runt det.
    const double r20 = 20.0 * kPi / 180.0;
    const auto [pivot, pivotB] =
        kör({par(0, 0, 40, 0), par(0, 0, 40 * std::cos(r20), 40 * std::sin(r20))});
    // Vridning kring mitten: 20°, avståndet oförändrat (bågen 7).
    const auto [vrid, vridB] =
        kör({par(0, 0, 40, 0), par(20 - 20 * std::cos(r20), -20 * std::sin(r20),
                                   20 + 20 * std::cos(r20), 20 * std::sin(r20))});
    // Panorering med en liten vridning och spridning — panoreringen dominerar.
    const auto [panVrid, pvB] = kör({par(0, 0, 40, 0), par(10, 0, 50, 2)});
    // Under tröskeln: 5 åt höger — inte bestämt.
    const auto [små, småB] = kör({par(0, 0, 40, 0), par(5, 0, 45, 0)});
    check(pan == M::Pan && panB && nyp == M::Zoom && ankar == M::Zoom &&
              nypVrid == M::Zoom && pivot == M::Rotate && vrid == M::Rotate &&
              panVrid == M::Pan && små == M::Undecided && !småB,
          "K11 klassaren: panorering → Pan; nypning (kring mitten, kring ett "
          "stilla finger, och med 10° vridning) → Zoom; vridning (kring ett "
          "finger och kring mitten) → Rotate; panorering med lite vridning → "
          "Pan; under tröskeln obestämt");
    // LÅST: efter Pan blir en senare nypning i samma beröring ingenting …
    TwoFingerClassifier kl;
    kl.update(std::vector<Contact>{finger(1, 0, 0), finger(2, 40, 0)});
    kl.update(std::vector<Contact>{finger(1, 10, 0), finger(2, 50, 0)});
    kl.update(std::vector<Contact>{finger(1, 0, 0), finger(2, 80, 0)});
    const bool låst = kl.mode() == M::Pan;
    // … men ett lyft och en ny beröring börjar om.
    const ClassifyStep lyft = kl.update(std::vector<Contact>{finger(1, 0, 0)});
    kl.update(std::vector<Contact>{finger(3, 0, 0), finger(4, 40, 0)});
    const bool omIgen = kl.mode() == M::Undecided;
    kl.update(std::vector<Contact>{finger(3, -8, 0), finger(4, 48, 0)});
    // K12 — IKAPP UTAN HOPP (Windows-backendens skuld): skulden 3 betalas med
    // lika mycket som varje steg — 1 → 2 (skuld 2), 1 → 2 (skuld 1), 1 → 2
    // (skuld 0), 1 → 1; ett steg bakåt betalar också (−1 → 0).
    double skuld = 3.0;
    const double s1 = catchUp(1.0, skuld), s2 = catchUp(1.0, skuld),
                 s3 = catchUp(1.0, skuld), s4 = catchUp(1.0, skuld);
    double skuld2 = 3.0;
    const double bak = catchUp(-1.0, skuld2);
    check(s1 == 2.0 && s2 == 2.0 && s3 == 2.0 && s4 == 1.0 && skuld == 0.0 &&
              bak == 0.0 && skuld2 == 2.0,
          "K12 skulden betalas av i takt med stegen (dubbel fart tills ikapp)");
    check(låst && lyft.ended && omIgen && kl.mode() == M::Zoom,
          "K11 beslutet låses för beröringen; ett lyft börjar om, och nästa "
          "beröring kan bli en nypning");
}

// V — VRIDSNÄPPET och H — panoreringens tecken.
void fallV()
{
    using namespace glaze::detail;
    check(defaultRotationSnap() == 360.0f &&
              defaultRotationSnapTolerance() == 7.0f,
          "V förvalet: steget 360 (bara norr) och toleransen 7°");
    check(snapBearing(88.0f, 90.0f, 7.0f) == 90.0f,
          "V 88° med steget 90 snäpper till 90°", svar(snapBearing(88.0f, 90.0f, 7.0f)));
    check(!snapBearing(80.0f, 90.0f, 7.0f),
          "V 80° står kvar — 10° är utanför toleransen 7°",
          svar(snapBearing(80.0f, 90.0f, 7.0f)));
    check(snapBearing(-5.0f, 90.0f, 7.0f) == 0.0f &&
              snapBearing(355.0f, 90.0f, 7.0f) == 0.0f &&
              snapBearing(715.0f, 90.0f, 7.0f) == 0.0f,
          "V 355° (och −5°, 715°) snäpper till NORR — vinkeln modulo 360",
          svar(snapBearing(715.0f, 90.0f, 7.0f)));
    check(snapBearing(268.0f, 90.0f, 7.0f) == -90.0f &&
              snapBearing(-92.0f, 90.0f, 7.0f) == -90.0f,
          "V 268° snäpper till 270° (som −90° i bäringens form)",
          svar(snapBearing(268.0f, 90.0f, 7.0f)));
    check(!snapBearing(88.0f, 0.0f, 7.0f) && !snapBearing(1.0f, 0.0f, 7.0f),
          "V steget 0 är AV");
    check(!snapBearing(88.0f, 360.0f, 7.0f) &&
              snapBearing(5.0f, 360.0f, 7.0f) == 0.0f,
          "V steget 360 snäpper bara mot norr (88° står kvar)");
    check(!snapBearing(90.0f, 90.0f, 7.0f) && !snapBearing(0.0f, 360.0f, 7.0f),
          "V en bäring som redan står på vinkeln har ingenting att glida");
    check(snapBearing(43.0f, 45.0f, 7.0f) == 45.0f &&
              snapBearing(-136.0f, 45.0f, 7.0f) == -135.0f,
          "V steget 45: åttondelarna");
    check(snapBearing(357.0f, 100.0f, 7.0f) == 0.0f &&
              snapBearing(303.0f, 100.0f, 7.0f) == -60.0f,
          "V ett steg som inte delar varvet: 0/100/200/300, och mellan 300 "
          "och 360 snäpper det mot norr");
    check(validRotationSnap(0.0f) && validRotationSnap(90.0f) &&
              validRotationSnap(360.0f) && !validRotationSnap(-1.0f) &&
              !validRotationSnap(361.0f) && !validRotationSnap(std::nanf("")),
          "V giltiga steg 0–360");
    check(validRotationSnapTolerance(0.0f) && validRotationSnapTolerance(180.0f) &&
              !validRotationSnapTolerance(-0.5f) &&
              !validRotationSnapTolerance(181.0f) &&
              !validRotationSnapTolerance(std::nanf("")),
          "V giltiga toleranser 0–180");

    double dx = 0.0, dy = 0.0;
    precisePanPx(1.0f, 2.0f, 1.5f, dx, dy);
    check(nära(dx, -1.0 * gfx::kPrecisePxPerRow * 1.5, 1e-9) &&
              nära(dy, 2.0 * gfx::kPrecisePxPerRow * 1.5, 1e-9),
          "H innehållet flyttar (−wheelX, +wheelY) × raden × skalan",
          tal(dx) + ", " + tal(dy));
}

// ============================================================= MED FÖNSTER

using P = gfx::GesturePhase;

struct Värd : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Panel  panel;   // en RULLANDE förälder — bubblingens mål
    glaze::Label  fyll;    // gör panelen högre än sin yta
    glaze::Canvas yta;     // i panelen
    glaze::Canvas yta2;    // bredvid — gestmålets granne
    int           ändringar = 0, ändringar2 = 0;
    // Det appens `event()`-krok såg (W, O): hjulets sort och — för hjulet och
    // gesten — fönstrets modifierare, som ska vara satta FÖRE kroken.
    bool              sågPrecise = false;
    gfx::GesturePhase sågFas     = P::None;
    std::uint32_t     sågMods    = ~0u;

    Värd() : glaze::AppWindow("trackpad_gate", 700, 520, gfx::Chrome::None)
    {
        panel.name   = "panel";
        panel.x      = 10;
        panel.y      = 10;
        panel.width  = 380;
        panel.height = 320;
        root().adopt(panel);
        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 20;
        yta.width  = 300;
        yta.height = 240;
        yta.setBorder(false);
        panel.adopt(yta);
        fyll.name = "fyll";
        fyll.x    = 10;
        fyll.y    = 900;
        fyll.setText("fyll");
        panel.adopt(fyll);
        yta2.name   = "yta2";
        yta2.x      = 420;
        yta2.y      = 30;
        yta2.width  = 240;
        yta2.height = 200;
        yta2.setBorder(false);
        root().adopt(yta2);
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::MouseWheel) {
            sågPrecise = isPreciseWheel();
            sågFas     = wheelPhase();
        }
        if (e.type == gfx::EventType::MouseWheel ||
            e.type == gfx::EventType::Gesture)
            sågMods = modifierKeys();
        return false;
    }

    // Ritytans lokala logiska px → fönstrets fysiska.
    gfx::Pointf fys(glaze::Canvas& c, double lx, double ly)
    {
        const gfx::Rectf b = c.resolve(c.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{static_cast<float>(b.x + lx * s),
                           static_cast<float>(b.y + ly * s)};
    }
    // Hjulet och gesten bär modifierarna som backenden lämnar dem (fasadens
    // kontrakt vid `kModCtrl`) — 0 när ingen tangent hålls.
    void hjul(gfx::Pointf p, float wx, float wy, bool precise,
              gfx::GesturePhase fas = P::None, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelX   = wx;
        e.wheelY   = wy;
        e.precise  = precise;
        e.phase    = fas;
        e.mods     = mods;
        handleEvent(e);
    }
    void gest(gfx::Pointf p, gfx::GesturePhase fas, float skala, float grader,
              std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type            = gfx::EventType::Gesture;
        e.windowId        = window().id();
        e.mouseX          = p.x;
        e.mouseY          = p.y;
        e.phase           = fas;
        e.gestureScale    = skala;
        e.gestureRotation = grader;
        e.mods            = mods;
        handleEvent(e);
    }
    // En tangent genom samma ingång — `Key::Unknown` är en modifierartangent
    // (enumet har inga poster för dem).
    void tangent(gfx::EventType t, gfx::Key k, std::uint32_t mods)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void mus(gfx::EventType t, gfx::Pointf p, std::uint32_t mods)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = t == gfx::EventType::MouseMotion ? gfx::MouseButton::None
                                                      : gfx::MouseButton::Left;
        e.mods     = mods;
        handleEvent(e);
    }
    void rita() { window().renderFrame(); }

    glaze::Point värld(glaze::Canvas& c, double lx, double ly)
    {
        const auto w = c.toWorld(glaze::Point{lx, ly});
        return w ? *w : glaze::Point{NAN, NAN};
    }
    glaze::Point lokal(glaze::Canvas& c, const glaze::Point& w)
    {
        const auto p = c.toLocal(w);
        return p ? *p : glaze::Point{NAN, NAN};
    }

    // Varje fall börjar från samma läge: gesterna av, snäppet förvalt.
    void nollställ(glaze::Canvas& c, const glaze::Camera& k)
    {
        c.setPannable(false);
        c.setZoomable(false);
        c.setRotatable(false);
        c.setPitchable(false);
        c.setInertia(false);
        c.setZoomRange(0.0, 0.0);
        c.setMaxPitch(60.0f);
        c.setRotationSnap(glaze::detail::defaultRotationSnap());
        c.setRotationSnapTolerance(glaze::detail::defaultRotationSnapTolerance());
        c.setCamera(k);
        // O mäter flygläget — bindningar
        // (tests/orbit_fly.h); förvalet mäts i canvas3d_gate PS.
        gate::orbitFly(c);
        c.onMouseWheel = nullptr;
        panel.setScroll(0.0f, 0.0f);
        invalidate();
        rita();   // ytan och kameran som de ritas — toWorld läser den
    }

    // Kartkameran rakt uppifrån — varje fall i O lämnar ritytan med den.
    static glaze::Camera platt0() { return glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f}; }

    void kör();
    void orbit();
};

void Värd::kör()
{
    const std::string vid = " [skala " + tal(window().uiScale()) + "]";
    yta.onCameraChange  = [this] { ++ändringar; };
    yta2.onCameraChange = [this] { ++ändringar2; };
    yta.setYUp(true);
    const glaze::Camera platt{{0.0, 0.0}, 2.0, 0.0f, 0.0f};
    const glaze::Camera tiltad{{674000.0, 6580000.0}, 3.0, 30.0f, 45.0f};
    const double        k = gfx::kPrecisePxPerRow;
    // En fyrtiondels logisk px (pekarens läge går som float genom händelsen).
    const double tol = 0.03;

    // ---- P. TVÅFINGERSDRAGET PANORERAR EXAKT
    const auto panorera = [&](const char* vad) {
        const glaze::Point L{150.0, 110.0};
        const glaze::Point w     = värld(yta, L.x, L.y);
        const double       zFöre = yta.zoom();
        const int          ä     = ändringar;
        hjul(fys(yta, L.x, L.y), 2.5f, -1.5f, true, P::Begin);
        const glaze::Point efter = lokal(yta, w);
        const glaze::Point facit{L.x - 2.5 * k, L.y - 1.5 * k};
        check(näraP(efter, facit, tol) && yta.zoom() == zFöre && ändringar == ä + 1,
              std::string("P ") + vad +
                  ": världspunkten under pekaren flyttar (−wheelX, +wheelY) × "
                  "raden, exakt, utan zoom, och onCameraChange" + vid,
              pt(efter) + " mot " + pt(facit));
        // OS:ets tröghet (Momentum) panorerar likadant; End utan förflyttning
        // rör ingenting.
        const glaze::Point w2 = värld(yta, L.x, L.y);
        hjul(fys(yta, L.x, L.y), -1.0f, 0.5f, true, P::Momentum);
        const glaze::Point ef2 = lokal(yta, w2);
        const glaze::Camera före = yta.camera();
        hjul(fys(yta, L.x, L.y), 0.0f, 0.0f, true, P::End);
        check(näraP(ef2, glaze::Point{L.x + 1.0 * k, L.y + 0.5 * k}, tol) &&
                  yta.camera().center.x == före.center.x &&
                  yta.camera().center.y == före.center.y && !yta.isAnimating(),
              std::string("P ") + vad +
                  ": OS:ets tröghet panorerar likadant, End rör ingenting och "
                  "ritytans egen tröghet startar inte" + vid,
              pt(ef2));
    };
    nollställ(yta, platt);
    yta.setPannable(true);
    yta.setZoomable(true);   // styrplattans drag ska panorera ÄNDÅ
    panorera("platt");
    nollställ(yta, tiltad);
    yta.setPannable(true);
    yta.setZoomable(true);
    panorera("vriden 30° och tiltad 45° (SWEREF)");

    // SAMMA TECKEN SOM EN RULLAD LISTA: panelen får samma hjul utanför
    // ritytan — dess innehåll flyttar NEDÅT (rullningen minskar) när wheelY
    // är positivt, och ritytans innehåll gör detsamma.
    nollställ(yta, platt);
    yta.setPannable(true);
    panel.setScroll(0.0f, 100.0f);
    rita();
    const float rullFöre = panel.scrollY();
    {
        // En punkt i panelen under ritytan: panelens lokala (200, 290).
        const gfx::Rectf pb = panel.resolve(panel.behaviorCtx());
        const float      s  = window().uiScale();
        hjul(gfx::Pointf{pb.x + 200.0f * s, pb.y + 290.0f * s}, 0.0f, 0.5f, true,
             P::Update);
    }
    const float rullEfter = panel.scrollY();
    const bool  listanNer = rullEfter < rullFöre;
    panel.setScroll(0.0f, 0.0f);
    rita();
    const glaze::Point L{150.0, 110.0};
    const glaze::Point w = värld(yta, L.x, L.y);
    hjul(fys(yta, L.x, L.y), 0.0f, 0.5f, true, P::Update);
    const glaze::Point efter = lokal(yta, w);
    check(listanNer && efter.y > L.y,
          "P samma tecken som en rullad lista: positivt wheelY flyttar både "
          "panelens och ritytans innehåll NEDÅT" + vid,
          "rullningen " + tal(rullFöre) + " → " + tal(rullEfter) + ", " +
              pt(efter));

    // ---- R. RADER → RÖRELSE i det som RULLAR (`Widget::wheelScrollPx`):
    // styrplattans rader är px, så panelens innehåll följer fingrarna 1:1;
    // mushjulets rader stegar `wheelStep`.
    {
        const gfx::Rectf pb = panel.resolve(panel.behaviorCtx());
        const float      s  = window().uiScale();
        const float      st = panel.behaviorCtx().style->wheelStep;
        const gfx::Pointf ip{pb.x + 200.0f * s, pb.y + 290.0f * s};
        panel.setScroll(0.0f, 200.0f);
        rita();
        hjul(ip, 0.0f, 3.0f, true, P::Update);
        const float precis = 200.0f - panel.scrollY();
        panel.setScroll(0.0f, 200.0f);
        rita();
        hjul(ip, 0.0f, 1.0f, false);
        const float klick = 200.0f - panel.scrollY();
        check(nära(precis, 3.0 * k, 1e-4) && nära(klick, st, 1e-4),
              "R panelen rullar 1:1 med styrplattan (3 rader = " + tal(3.0 * k) +
                  " px) och wheelStep per mushjulsklick" + vid,
              tal(precis) + " / " + tal(klick));
        panel.setScroll(0.0f, 0.0f);
        rita();
    }

    // ---- W. MUSHJULET zoomar fortfarande; appens krok och handlern ser sorten
    nollställ(yta, platt);
    yta.setPannable(true);
    yta.setZoomable(true);
    bool handlerPrecise = true;
    yta.onMouseWheel = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        handlerPrecise = e.precise;
    };
    {
        const glaze::Point Lz{210.0, 80.0};
        const glaze::Point wz    = värld(yta, Lz.x, Lz.y);
        const double       zFöre = yta.zoom();
        hjul(fys(yta, Lz.x, Lz.y), 0.0f, 1.0f, false);
        const glaze::Point ez = lokal(yta, wz);
        check(nära(yta.zoom(), zFöre * std::sqrt(2.0), 1e-9 * zFöre) &&
                  näraP(ez, Lz, tol) && !handlerPrecise && !sågPrecise &&
                  sågFas == P::None,
              "W ett mushjul zoomar som förut (√2 per rad, mot pekaren), och "
              "handlern och appens krok ser precise = false" + vid,
              tal(yta.zoom() / zFöre));
        hjul(fys(yta, Lz.x, Lz.y), 0.0f, 1.0f, true, P::Update);
        check(handlerPrecise && sågPrecise && sågFas == P::Update &&
                  nära(yta.zoom(), zFöre * std::sqrt(2.0), 1e-9 * zFöre),
              "W ett precist hjul zoomar aldrig, och handlern (CanvasMouseArgs::"
              "precise) och appens krok (isPreciseWheel/wheelPhase) ser det",
              tal(yta.zoom() / zFöre));
    }
    yta.onMouseWheel = nullptr;

    // ---- Z. NYPNINGEN zoomar mot pekaren med kvoternas produkt
    const auto nyp = [&](const char* vad) {
        const glaze::Point Ln{200.0, 120.0};
        const glaze::Point wn    = värld(yta, Ln.x, Ln.y);
        const double       zFöre = yta.zoom();
        const int          ä     = ändringar;
        gest(fys(yta, Ln.x, Ln.y), P::Begin, 1.1f, 0.0f);
        gest(fys(yta, Ln.x, Ln.y), P::Update, 1.2f, 0.0f);
        gest(fys(yta, Ln.x, Ln.y), P::End, 1.0f, 0.0f);
        const glaze::Point en = lokal(yta, wn);
        const double facit = zFöre * static_cast<double>(1.1f) *
                             static_cast<double>(1.2f);
        check(nära(yta.zoom(), facit, 1e-9 * facit) && näraP(en, Ln, tol),
              std::string("Z ") + vad +
                  ": zoomen är kvoternas produkt och världspunkten under "
                  "pekaren står kvar" + vid,
              tal(yta.zoom() / zFöre) + ", " + pt(en));
        check(ändringar == ä + 2 && !yta.isAnimating() && !animating(),
              std::string("Z ") + vad +
                  ": onCameraChange per steg som zoomade, och inget animerar "
                  "efter End",
              std::to_string(ändringar - ä));
    };
    nollställ(yta, platt);
    yta.setZoomable(true);
    nyp("platt");
    nollställ(yta, glaze::Camera{{500.0, -300.0}, 1.5, -20.0f, 40.0f});
    yta.setZoomable(true);
    nyp("tiltad 40° och vriden −20°");

    // ---- R. VRIDNINGEN runt pekaren, medurs = medurs
    nollställ(yta, platt);
    yta.setRotatable(true);
    {
        const glaze::Point Lr{180.0, 130.0};
        const glaze::Point Q{Lr.x + 60.0, Lr.y};   // 60 px till höger
        const glaze::Point wr = värld(yta, Lr.x, Lr.y);
        const glaze::Point wq = värld(yta, Q.x, Q.y);
        const int          ä  = ändringar;
        gest(fys(yta, Lr.x, Lr.y), P::Begin, 1.0f, 10.0f);
        gest(fys(yta, Lr.x, Lr.y), P::End, 1.0f, 0.0f);
        const glaze::Point er = lokal(yta, wr);
        const glaze::Point eq = lokal(yta, wq);
        // Medurs på skärmen (y nedåt): (60, 0) vriden 10° blir
        // (60 cos 10°, 60 sin 10°) — punkten går NED.
        const double       v = 10.0 * kPi / 180.0;
        const glaze::Point facit{Lr.x + 60.0 * std::cos(v), Lr.y + 60.0 * std::sin(v)};
        check(näraP(er, Lr, tol),
              "R världspunkten under pekaren står kvar" + vid, pt(er));
        check(näraP(eq, facit, 0.05) && eq.y > Q.y,
              "R +10° är MEDURS på skärmen: punkten till höger om pekaren går "
              "ned, kartan vrids med fingrarna" + vid,
              pt(eq) + " mot " + pt(facit));
        check(nära(yta.bearing(), -10.0, 1e-4) && ändringar == ä + 1 &&
                  !yta.isAnimating(),
              "R bäringen −10° (bäringen vrider innehållet moturs), en "
              "onCameraChange och inget snäpp 10° från norr",
              tal(yta.bearing()));
    }
    nollställ(yta, tiltad);
    yta.setRotatable(true);
    {
        const glaze::Point Lr{120.0, 160.0};
        const glaze::Point wr = värld(yta, Lr.x, Lr.y);
        const float        b0 = yta.bearing();
        gest(fys(yta, Lr.x, Lr.y), P::Begin, 1.0f, -25.0f);
        gest(fys(yta, Lr.x, Lr.y), P::End, 1.0f, 0.0f);
        check(näraP(lokal(yta, wr), Lr, tol) &&
                  nära(yta.bearing(), b0 + 25.0, 1e-4),
              "R tiltad 45°: punkten under pekaren står kvar och bäringen följer"
                  + vid,
              pt(lokal(yta, wr)) + ", " + tal(yta.bearing()));
    }
    // Grannen har y NEDÅT (som Draw): medurs är medurs också där.
    nollställ(yta2, platt);
    yta2.setRotatable(true);
    {
        const glaze::Point Lr{80.0, 90.0};
        const glaze::Point wq = värld(yta2, Lr.x + 60.0, Lr.y);
        gest(fys(yta2, Lr.x, Lr.y), P::Begin, 1.0f, 10.0f);
        gest(fys(yta2, Lr.x, Lr.y), P::End, 1.0f, 0.0f);
        const double       v = 10.0 * kPi / 180.0;
        const glaze::Point facit{Lr.x + 60.0 * std::cos(v), Lr.y + 60.0 * std::sin(v)};
        check(näraP(lokal(yta2, wq), facit, 0.05),
              "R med y nedåt i världen är +10° också medurs på skärmen" + vid,
              pt(lokal(yta2, wq)) + " mot " + pt(facit));
    }

    // ---- Z. ZOOM ELLER VRIDNING — aldrig båda i samma gest.
    // Före beslutet hålls båda och den valda släpps ikapp.
    {
        const glaze::Point zp{150.0, 120.0};
        // Z1 — EN NYPNING SOM VRIDER LITE MER ÄN DEN ZOOMAR:
        // 1,03 och 3° per steg — i första steget
        // är vridningen störst (0,052 mot 0,030) men under beslutströskeln;
        // i andra (0,105 mot 0,059) är den inte DUBBELT så stor. Zoom, ikapp,
        // och en vridning efteråt kastas.
        nollställ(yta, platt);
        yta.setZoomable(true);
        yta.setRotatable(true);
        double z0 = yta.zoom();
        gest(fys(yta, zp.x, zp.y), P::Begin, 1.03f, 3.0f);
        const bool höll = yta.zoom() == z0 && yta.bearing() == 0.0f;
        gest(fys(yta, zp.x, zp.y), P::Update, 1.03f, 3.0f);
        gest(fys(yta, zp.x, zp.y), P::Update, 1.0f, 10.0f);
        gest(fys(yta, zp.x, zp.y), P::End, 1.0f, 0.0f);
        const double facit = z0 * 1.03 * 1.03;
        check(höll && nära(yta.zoom(), facit, 1e-5 * facit) &&
                  yta.bearing() == 0.0f,
              "Z1 en nypning som vrider lite mer än den zoomar blir ZOOM: hålls "
              "tills beslutet, släpps ikapp (×1,0609) och vridningen kastas",
              tal(yta.zoom() / z0) + " / " + tal(yta.bearing()));
        // Z2 — vridningen dominerar: 20° (0,35 rad) mot ln 1,01; en nypning
        // efteråt kastas.
        nollställ(yta, platt);
        yta.setZoomable(true);
        yta.setRotatable(true);
        z0 = yta.zoom();
        gest(fys(yta, zp.x, zp.y), P::Begin, 1.01f, -20.0f);
        gest(fys(yta, zp.x, zp.y), P::Update, 1.3f, 0.0f);
        gest(fys(yta, zp.x, zp.y), P::End, 1.0f, 0.0f);
        check(yta.zoom() == z0 && nära(yta.bearing(), 20.0, 1e-4),
              "Z2 vridningen dominerar: kartan vrids och nypningen kastas",
              tal(yta.zoom() / z0) + " / " + tal(yta.bearing()));
        // Z3 — släppt UTAN beslut: den största av de hållna utförs.
        nollställ(yta, platt);
        yta.setZoomable(true);
        yta.setRotatable(true);
        z0 = yta.zoom();
        gest(fys(yta, zp.x, zp.y), P::Begin, 1.01f, 0.25f);
        gest(fys(yta, zp.x, zp.y), P::End, 1.0f, 0.0f);
        check(yta.zoom() == z0 && yta.bearing() == 0.0f,
              "Z3 en gest som släpps obestämd gör ingenting — inget hopp vid "
              "släppet",
              tal(yta.zoom() / z0) + " / " + tal(yta.bearing()));
        // Z4 — BARA EN SORT (Windows skickar bara den fingrarna valt): ett
        // vridsteg på 1,5° vrider DIREKT, utan att vänta på 4,6°-tröskeln.
        nollställ(yta, platt);
        yta.setZoomable(true);
        yta.setRotatable(true);
        z0 = yta.zoom();
        gest(fys(yta, zp.x, zp.y), P::Begin, 1.0f, -1.5f);
        const bool direkt = nära(yta.bearing(), 1.5, 1e-4) && yta.zoom() == z0;
        gest(fys(yta, zp.x, zp.y), P::End, 1.0f, 0.0f);
        check(direkt, "Z4 bara vridning har kommit: den avgörs direkt (1,5°)",
              tal(yta.bearing()));
        // Z5 — IKAPP UTAN HOPP: tre vridsteg om 0,5° — de
        // två första hålls (under 0,02 rad), det tredje fattar beslutet och
        // vrider 0,5° + 0,5° av skulden (1°, inte 1,5°); det fjärde betalar
        // resten, och kartan står där fingrarna står (2°).
        nollställ(yta, platt);
        yta.setRotatable(true);
        gest(fys(yta, zp.x, zp.y), P::Begin, 1.0f, -0.5f);
        gest(fys(yta, zp.x, zp.y), P::Update, 1.0f, -0.5f);
        const bool hållen = yta.bearing() == 0.0f;
        gest(fys(yta, zp.x, zp.y), P::Update, 1.0f, -0.5f);
        const float tredje = yta.bearing();
        gest(fys(yta, zp.x, zp.y), P::Update, 1.0f, -0.5f);
        const float fjärde = yta.bearing();
        gest(fys(yta, zp.x, zp.y), P::Cancel, 1.0f, 0.0f);
        check(hållen && nära(tredje, 1.0, 1e-4) && nära(fjärde, 2.0, 1e-4),
              "Z5 det hållna hinner ikapp med fingrarna: 0 → 1° → 2° (dubbel "
              "fart tills ikapp), aldrig ett hopp",
              tal(tredje) + " / " + tal(fjärde));
    }

    // ---- N. SNÄPPET när en vridande gest släpps
    const auto vänta = [this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        rita();
    };
    const glaze::Point mitt{150.0, 120.0};
    // N1 — förvalet: 4° från norr glider till EXAKT norr.
    nollställ(yta, platt);
    yta.setRotatable(true);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, -4.0f);
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    const bool glider = yta.isEasing() && nära(yta.bearing(), 4.0, 1e-4);
    vänta();
    check(glider && !yta.isEasing() && yta.bearing() == 0.0f && !yta.isAnimating(),
          "N förvalet: en styrplattevridning som släpps 4° från norr glider "
          "till exakt norr och stannar",
          tal(yta.bearing()));
    // N1b — FALL: kartan norrut, vrid 25° och släpp (för
    // långt från norr — står kvar), vrid tillbaka 30° till −5° och släpp. OS:ets
    // TRÖGHET panorerar vidare efter släppet (fingrarna drog också lite), och
    // det får inte stoppa snäppets glidning — den landar ändå på exakt norr.
    nollställ(yta, platt);
    yta.setRotatable(true);
    yta.setPannable(true);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, -25.0f);
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    const bool stårKvar = !yta.isEasing() && nära(yta.bearing(), 25.0, 1e-4);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, 30.0f);
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    const bool snäpper = yta.isEasing() && nära(yta.bearing(), -5.0, 1e-4);
    const glaze::Point c0 = yta.camera().center;
    hjul(fys(yta, mitt.x, mitt.y), 0.4f, -0.3f, true, P::Momentum);
    rita();
    hjul(fys(yta, mitt.x, mitt.y), 0.2f, -0.1f, true, P::Momentum);
    const bool pannade   = yta.camera().center.x != c0.x;
    const bool gliderKvar = yta.isEasing();
    vänta();
    check(stårKvar && snäpper && pannade && gliderKvar && !yta.isEasing() &&
              yta.bearing() == 0.0f && !yta.isAnimating(),
          "N vrid 25° och släpp, vrid tillbaka till −5°: snäppet glider till "
          "exakt norr ÄVEN när trögheten panorerar under glidningen",
          tal(yta.bearing()));
    // N2 — steget 90: 88° → 90°.
    nollställ(yta, glaze::Camera{{0.0, 0.0}, 2.0, 80.0f, 0.0f});
    yta.setRotatable(true);
    yta.setRotationSnap(90.0f);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, -8.0f);   // 80 → 88
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    vänta();
    check(yta.bearing() == 90.0f && !yta.isEasing(),
          "N steget 90: släppt vid 88° glider den till 90°", tal(yta.bearing()));
    // N3 — 80° står kvar (toleransen 7).
    nollställ(yta, glaze::Camera{{0.0, 0.0}, 2.0, 70.0f, 0.0f});
    yta.setRotatable(true);
    yta.setRotationSnap(90.0f);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, -10.0f);   // 70 → 80
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    check(!yta.isEasing() && nära(yta.bearing(), 80.0, 1e-4),
          "N steget 90: släppt vid 80° står den kvar (10° > 7°)",
          tal(yta.bearing()));
    // N4 — steget 0 är av, steget 360 bara norr.
    yta.setRotationSnap(0.0f);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, -8.0f);   // 80 → 88
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    const bool avStår = !yta.isEasing() && nära(yta.bearing(), 88.0, 1e-4);
    yta.setRotationSnap(360.0f);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, 2.0f);   // 88 → 86
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    check(avStår && !yta.isEasing() && nära(yta.bearing(), 86.0, 1e-4),
          "N steget 0 snäpper inte, och steget 360 lämnar 86° (bara norr)",
          tal(yta.bearing()));
    // N5 — 355° med steget 90 glider till norr (modulo).
    nollställ(yta, glaze::Camera{{0.0, 0.0}, 2.0, -10.0f, 0.0f});
    yta.setRotatable(true);
    yta.setRotationSnap(90.0f);
    gest(fys(yta, mitt.x, mitt.y), P::Begin, 1.0f, -5.0f);   // 350 → 355
    gest(fys(yta, mitt.x, mitt.y), P::End, 1.0f, 0.0f);
    vänta();
    check(yta.bearing() == 0.0f,
          "N steget 90: släppt vid 355° glider den till norr", tal(yta.bearing()));
    // N6 — Ctrl+DRAGET snäpper med samma steg (0,8° per logisk px).
    nollställ(yta, glaze::Camera{{0.0, 0.0}, 2.0, 100.0f, 0.0f});
    yta.setRotatable(true);
    yta.setRotationSnap(90.0f);
    {
        const gfx::Pointf a = fys(yta, 150.0, 120.0);
        const gfx::Pointf b = fys(yta, 150.0 - 10.0, 120.0);   // −8° ⇒ 92°
        mus(gfx::EventType::MouseButtonDown, a, gfx::kModCtrl);
        mus(gfx::EventType::MouseMotion, b, gfx::kModCtrl);
        mus(gfx::EventType::MouseButtonUp, b, gfx::kModCtrl);
    }
    const bool dragGlider = yta.isEasing();
    vänta();
    check(dragGlider && yta.bearing() == 90.0f,
          "N Ctrl+draget släppt vid 92° snäpper också — till 90° med steget 90",
          tal(yta.bearing()));
    // N7 — Shift+pilarna och setBearing snäpps inte.
    yta.setBearing(88.0f);
    rita();
    yta.focus();
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = gfx::Key::Left;
        e.mods     = gfx::kModShift;
        handleEvent(e);   // 88 − 15 = 73
    }
    check(!yta.isEasing() && nära(yta.bearing(), 73.0, 1e-4),
          "N setBearing och Shift+pilarna snäpps inte (88° och 73° står)",
          tal(yta.bearing()));

    // ---- F. GESTERNA AV: kameran står still och hjulet BUBBLAR
    nollställ(yta, platt);
    panel.setScroll(0.0f, 100.0f);
    rita();
    {
        const glaze::Camera k0    = yta.camera();
        const float         r0    = panel.scrollY();
        hjul(fys(yta, 150.0, 110.0), 0.0f, 0.5f, true, P::Update);
        const float         r1    = panel.scrollY();
        check(yta.camera().center.x == k0.center.x &&
                  yta.camera().center.y == k0.center.y && r1 < r0,
              "F utan panorering: ritytan står still och den rullande föräldern "
              "tar styrplattans drag" + vid,
              tal(r0) + " → " + tal(r1));
        yta.setPannable(true);
        hjul(fys(yta, 150.0, 110.0), 0.0f, 0.5f, true, P::Update);
        check(panel.scrollY() == r1 && yta.camera().center.y != k0.center.y,
              "F med panorering tar ritytan draget och föräldern står still");
    }
    nollställ(yta, platt);
    {
        const glaze::Camera k0 = yta.camera();
        gest(fys(yta, 150.0, 110.0), P::Begin, 1.3f, 12.0f);
        gest(fys(yta, 150.0, 110.0), P::Update, 1.3f, 12.0f);
        gest(fys(yta, 150.0, 110.0), P::End, 1.0f, 0.0f);
        check(yta.camera().zoom == k0.zoom && yta.camera().bearing == k0.bearing,
              "F utan zoom och vridning tar ritytan ingen gest");
        yta.setRotatable(true);   // vridning men ingen zoom
        gest(fys(yta, 150.0, 110.0), P::Begin, 1.3f, 12.0f);
        gest(fys(yta, 150.0, 110.0), P::End, 1.0f, 0.0f);
        check(yta.camera().zoom == k0.zoom && nära(yta.bearing(), -12.0, 1e-4),
              "F vridning utan zoom: nypningen ignoreras, vridningen vrider",
              tal(yta.camera().zoom) + " / " + tal(yta.bearing()));
        nollställ(yta, platt);
        yta.setZoomable(true);    // zoom men ingen vridning
        gest(fys(yta, 150.0, 110.0), P::Begin, 1.25f, 12.0f);
        gest(fys(yta, 150.0, 110.0), P::End, 1.0f, 0.0f);
        check(nära(yta.zoom(), k0.zoom * 1.25, 1e-6) && yta.bearing() == 0.0f,
              "F zoom utan vridning: vridningen ignoreras, nypningen zoomar");
    }

    // ---- M. GESTMÅLET hålls och släpps
    nollställ(yta, platt);
    nollställ(yta2, platt);
    yta.setZoomable(true);
    yta2.setZoomable(true);
    {
        const double z1 = yta.zoom(), z2 = yta2.zoom();
        gest(fys(yta, 150.0, 110.0), P::Begin, 1.1f, 0.0f);
        // Pekaren står över grannen nu — sekvensen är ändå den förstas.
        gest(fys(yta2, 100.0, 100.0), P::Update, 1.2f, 0.0f);
        gest(fys(yta2, 100.0, 100.0), P::End, 1.0f, 0.0f);
        check(nära(yta.zoom(), z1 * 1.1 * 1.2, 1e-5 * z1) && yta2.zoom() == z2,
              "M målet hålls från Begin till End fast grannen kommit under "
              "pekaren",
              tal(yta.zoom() / z1) + " / " + tal(yta2.zoom() / z2));
        gest(fys(yta2, 100.0, 100.0), P::Begin, 1.1f, 0.0f);
        gest(fys(yta2, 100.0, 100.0), P::End, 1.0f, 0.0f);
        check(nära(yta2.zoom(), z2 * 1.1, 1e-5 * z2),
              "M efter End väljer nästa Begin om — grannen tar den");
        // UPDATE UTAN BEGIN väljer som en Begin — och håller.
        const double z1b = yta.zoom(), z2b = yta2.zoom();
        gest(fys(yta, 150.0, 110.0), P::Update, 1.25f, 0.0f);
        gest(fys(yta2, 100.0, 100.0), P::Update, 1.2f, 0.0f);
        gest(fys(yta2, 100.0, 100.0), P::End, 1.0f, 0.0f);
        check(nära(yta.zoom(), z1b * 1.25 * 1.2, 1e-5 * z1b) && yta2.zoom() == z2b,
              "M en Update utan Begin väljer mål som en Begin och håller "
              "sekvensen",
              tal(yta.zoom() / z1b));
        // En INAKTIVERAD får inga Update — men sitt End.
        const double z1c = yta.zoom();
        gest(fys(yta, 150.0, 110.0), P::Begin, 1.1f, 0.0f);
        yta.disable();
        gest(fys(yta, 150.0, 110.0), P::Update, 1.5f, 0.0f);
        gest(fys(yta, 150.0, 110.0), P::End, 1.0f, 0.0f);
        yta.enable();
        check(nära(yta.zoom(), z1c * 1.1, 1e-5 * z1c),
              "M en rityta som inaktiveras mitt i sekvensen får inga fler "
              "Update", tal(yta.zoom() / z1c));
        // SLÄPPS NÄR MÅLET TAS BORT: grannen håller sekvensen, lämnar trädet
        // (lånad — den lever kvar) och nästa Update når den inte.
        const double z2d = yta2.zoom();
        const gfx::Pointf över2 = fys(yta2, 100.0, 100.0);
        gest(över2, P::Begin, 1.1f, 0.0f);
        root().unadopt(yta2);
        gest(över2, P::Update, 1.5f, 0.0f);
        gest(över2, P::End, 1.0f, 0.0f);
        check(nära(yta2.zoom(), z2d * 1.1, 1e-5 * z2d),
              "M målet släpps när det lämnar trädet — nästa Update når det inte",
              tal(yta2.zoom() / z2d));
        root().adopt(yta2);
    }
    nollställ(yta2, platt);
    rita();

    orbit();
}

// ---- O. ORBITLÄGET: med en Camera3D betyder samma
// gester orbitens panorering, dolly och azimut. Förväntat värde är grindens hålkamera.
void Värd::orbit()
{
    const std::string vid = " [skala " + tal(window().uiScale()) + "]";
    const double      k   = gfx::kPrecisePxPerRow;
    const float       s   = window().uiScale();
    glaze::Camera3D   k0;
    k0.target    = {674000.0, 6580000.0, 12.0};
    k0.distance  = 90.0;
    k0.azimuth   = 40.0f;
    k0.elevation = 35.0f;
    // Ingen nollställning av fönstrets modifierare: hjulet och gesten bär sin
    // egen mask (fasadens kontrakt vid `kModCtrl`), och det är den som gäller.
    const auto nollställ3D = [&] {
        nollställ(yta, platt0());
        yta.setCamera(k0);
        yta.setDistanceRange(0.0, 0.0);
        rita();
    };
    const auto box = [&] { return yta.resolve(yta.behaviorCtx()); };

    // O1 — TVÅFINGERSDRAGET PANORERAR: draget går (−wheelX, +wheelY) × raden
    // logiska px. Det är MITTENDRAGETS
    // panorering: målplanets punkt följer i SIDLED, och den lodräta delen
    // FLYGER längs blicken med målplanets belopp (utan Shift) — punkten under
    // pekaren följer alltså inte lodrätt, och det förväntade värdet är canvas3d_gate
    // P:s, ur grindens hålkamera.
    nollställ3D();
    yta.setPannable(true);
    yta.setZoomable(true);   // draget ska panorera ändå
    // Kamerans väntade mål efter ett panoreringsdrag från p till q utan Shift.
    const auto flygning = [&](const glaze::Camera3D& före, gfx::Pointf p,
                              double qx, double qy) {
        const Hal h = hal(före, box());
        const V3  t{före.target.x, före.target.y, före.target.z};
        const V3  D = målplanspunkt(h, t, p.x, p.y) - målplanspunkt(h, t, qx, qy);
        return t + h.r * dot(D, h.r) + h.f * dot(D, h.u);
    };
    const auto avvikelse = [&](V3 väntad) {
        const glaze::Camera3D e = yta.camera3D();
        const V3 d = V3{e.target.x, e.target.y, e.target.z} - väntad;
        return std::sqrt(dot(d, d));
    };
    {
        const gfx::Pointf    p    = fys(yta, 150.0, 110.0);
        const glaze::Camera3D före = yta.camera3D();
        const V3 väntad = flygning(före, p, p.x - 2.5 * k * s, p.y - 1.5 * k * s);
        const int ä = ändringar;
        hjul(p, 2.5f, -1.5f, true, P::Begin);
        const double fel = avvikelse(väntad);
        check(fel < 1e-6 && yta.camera3D().distance == före.distance &&
                  yta.camera3D().azimuth == före.azimuth &&
                  yta.camera3D().elevation == före.elevation && ändringar == ä + 1,
              "O tvåfingersdraget panorerar i orbit: (−wheelX, +wheelY) × raden, "
              "sidled följer målplanets punkt och fram/bak flyger längs blicken, "
              "inget dollyar" + vid,
              tal(fel) + " fel");
    }
    // O2 — CTRL + TVÅFINGERSDRAG ORBITAR: fingrarnas väg som ett
    // vänsterdrag, 0,8° azimut och 0,5° elevation per logisk px. Ctrl kommer
    // ur HJULETS EGEN MASK (fasadens kontrakt vid `kModCtrl`): ett drag direkt
    // efter ett Ctrl-tryck har ingen mushändelse emellan, och tangenteventet
    // får inte vara det som bär den — här bär det läget FÖRE trycket, utan
    // Ctrl, så som X11:s `state` och Waylands ordning (`modifiers` efter
    // `key`) lämnar det rått. Backenderna rättar tangentens egen bit,
    // men hjulet får inte bero på det: innan hjulet bar masken panorerade
    // draget här.
    nollställ3D();
    yta.setRotatable(true);
    yta.setPitchable(true);
    yta.setPannable(true);
    {
        const gfx::Pointf p = fys(yta, 150.0, 110.0);
        tangent(gfx::EventType::KeyDown, gfx::Key::Unknown, 0);
        sågMods = ~0u;
        // Innehållet +2k åt höger och +k nedåt.
        hjul(p, -2.0f, 1.0f, true, P::Update, gfx::kModCtrl);
        const glaze::Camera3D e = yta.camera3D();
        check(nära(e.azimuth, 40.0 + 2.0 * k * 0.8, 1e-3) &&
                  nära(e.elevation, 35.0 + 1.0 * k * 0.5, 1e-3) &&
                  e.target.x == k0.target.x && e.target.y == k0.target.y &&
                  e.distance == k0.distance,
              "O Ctrl + tvåfingersdrag orbitar som ett vänsterdrag (målet och "
              "avståndet orörda) — Ctrl ur hjulets mask, tangenttrycket bar "
              "den inte" + vid,
              tal(e.azimuth) + "° / " + tal(e.elevation) + "°");
        check(modifierKeys() == gfx::kModCtrl && sågMods == gfx::kModCtrl,
              "O hjulets mask är fönstrets modifierare, satta FÖRE appens "
              "event()-krok" + vid,
              std::to_string(modifierKeys()) + " / " + std::to_string(sågMods));
        // SLÄPPET: tangenteventet bär läget FÖRE släppet (med Ctrl), och
        // hjulet efter det är utan — draget PANORERAR, inget Ctrl står kvar.
        tangent(gfx::EventType::KeyUp, gfx::Key::Unknown, gfx::kModCtrl);
        const glaze::Camera3D före = yta.camera3D();
        const V3 väntad = flygning(före, p, p.x - 2.5 * k * s, p.y - 1.5 * k * s);
        hjul(p, 2.5f, -1.5f, true, P::Update);
        const glaze::Camera3D efter = yta.camera3D();
        const double          fel   = avvikelse(väntad);   // O1:s förväntade värde
        check(efter.azimuth == före.azimuth && efter.elevation == före.elevation &&
                  fel < 1e-6 && modifierKeys() == 0,
              "O efter släppet panorerar draget: hjulet utan Ctrl vinner över "
              "ett Ctrl som tangenteventet lämnade kvar" + vid,
              tal(efter.azimuth) + "°, " + tal(fel) + " fel");
    }
    // O3 — NYPNINGEN DOLLYAR mot målet: avståndet / kvoternas produkt, och
    // spannet gäller.
    nollställ3D();
    yta.setZoomable(true);
    {
        const gfx::Pointf p = fys(yta, 200.0, 120.0);
        const int         ä = ändringar;
        gest(p, P::Begin, 1.1f, 0.0f);
        gest(p, P::Update, 1.2f, 0.0f);
        gest(p, P::End, 1.0f, 0.0f);
        const double facit = 90.0 / (static_cast<double>(1.1f) * static_cast<double>(1.2f));
        const glaze::Camera3D e = yta.camera3D();
        check(nära(e.distance, facit, 1e-9 * facit) && e.target.x == k0.target.x &&
                  e.azimuth == k0.azimuth && ändringar == ä + 2 && !yta.isAnimating(),
              "O nypningen dollyar: avståndet / kvoternas produkt, målet orört, "
              "onCameraChange per steg, inget animerar efter End" + vid,
              tal(e.distance));
        yta.setDistanceRange(70.0, 0.0);
        gest(p, P::Begin, 1.5f, 0.0f);
        gest(p, P::End, 1.0f, 0.0f);
        check(yta.camera3D().distance == e.distance,
              "O ett avstånd redan under spannet går inte längre in — och hoppar "
              "inte till gränsen" + vid,
              tal(yta.camera3D().distance));
        yta.setDistanceRange(0.0, 0.0);
    }
    // O4 — VRIDNINGEN GER AZIMUT: fingrarna medurs vrider scenen medurs —
    // azimuten minus graderna — kring målet; inget snäpp efteråt.
    nollställ3D();
    yta.setRotatable(true);
    {
        const gfx::Pointf p = fys(yta, 100.0, 80.0);
        gest(p, P::Begin, 1.0f, 10.0f);
        gest(p, P::End, 1.0f, 0.0f);
        const glaze::Camera3D e = yta.camera3D();
        check(nära(e.azimuth, 30.0, 1e-4) && e.elevation == k0.elevation &&
                  e.target.x == k0.target.x && e.distance == k0.distance &&
                  !yta.isEasing(),
              "O +10° vridning är azimut −10° kring målet, utan snäpp" + vid,
              tal(e.azimuth));
    }
    // O5 — GESTEN BÄR MASKEN som hjulet: en onGesture-handler läser Ctrl ur
    // fönstrets modifierare, satta FÖRE appens krok — utan en mushändelse
    // emellan och med ett tangenttryck som inte bar den.
    nollställ3D();
    yta.setZoomable(true);
    {
        std::uint32_t handlern = ~0u;
        yta.onGesture = [&](glaze::Widget*, glaze::GestureArgs&) {
            handlern = modifierKeys();
        };
        const gfx::Pointf p = fys(yta, 200.0, 120.0);
        tangent(gfx::EventType::KeyDown, gfx::Key::Unknown, 0);
        sågMods = ~0u;
        gest(p, P::Begin, 1.1f, 0.0f, gfx::kModCtrl);
        const bool medCtrl = handlern == gfx::kModCtrl && sågMods == gfx::kModCtrl;
        const std::string fick = std::to_string(handlern) + "/" +
                                 std::to_string(sågMods);
        gest(p, P::End, 1.0f, 0.0f);   // Ctrl släpptes under gesten
        check(medCtrl && handlern == 0 && sågMods == 0 && modifierKeys() == 0,
              "O gesten bär masken: onGesture och appens krok ser Ctrl ur "
              "modifierKeys() i steget med Ctrl och 0 i steget utan" + vid,
              fick + " → " + std::to_string(handlern) + "/" +
                  std::to_string(sågMods));
        yta.onGesture = nullptr;
    }
    // O6 — GESTERNA AV i orbit: kameran står still och tvåfingersdraget
    // bubblar till den rullande föräldern.
    nollställ3D();
    panel.setScroll(0.0f, 100.0f);
    rita();
    {
        const float r0 = panel.scrollY();
        hjul(fys(yta, 150.0, 110.0), 0.0f, 0.5f, true, P::Update);
        gest(fys(yta, 150.0, 110.0), P::Begin, 1.3f, 0.0f);
        gest(fys(yta, 150.0, 110.0), P::End, 1.0f, 0.0f);
        const glaze::Camera3D e = yta.camera3D();
        check(panel.scrollY() < r0 && e.target.y == k0.target.y &&
                  e.distance == k0.distance,
              "O utan gester i orbit står kameran still och draget bubblar" + vid);
    }
    panel.setScroll(0.0f, 0.0f);
    nollställ(yta, platt0());   // kartkameran tillbaka för nästa skala
}

void fallMedFönster()
{
    Värd w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.kör();
        w.close();
    });
    w.run();
    if (!kördes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ================================================ B. BACKENDENS MASK

#if defined(GLAZE_BACKEND_D3D11) || defined(GLAZE_BACKEND_LINUX)
std::string mask(std::uint32_t m)
{
    std::string s;
    const auto lägg = [&](std::uint32_t bit, const char* namn) {
        if (m & bit)
            s += (s.empty() ? "" : "+") + std::string(namn);
    };
    lägg(gfx::kModCtrl, "Ctrl");
    lägg(gfx::kModShift, "Shift");
    lägg(gfx::kModAlt, "Alt");
    lägg(gfx::kModCmd, "Cmd");
    return s.empty() ? "0" : s;
}
#endif

#ifdef GLAZE_BACKEND_D3D11
// WIN32 — riktiga WM_MOUSEWHEEL/WM_MOUSEHWHEEL genom fönstrets WndProc
// (numpad_gate-prejudikatet), avlästa i appens `event()`-krok: det är
// lParam/wParam-vägen ända fram (`pushWheel` → `currentMods`) som mäts, och
// fönstrets `modifierKeys()` i kroken. `GetKeyState` läser trådens
// tangenttabell och `SetKeyboardState` skriver den, så Ctrl och Shift är
// NERE för det första hjulet och UPPE för det andra utan en riktig tangent.
struct Mottagare : glaze::AppWindow {
    struct Sett {
        std::uint32_t mods     = 0;   // eventets
        std::uint32_t fönstret = 0;   // modifierKeys() i kroken
    };
    std::vector<Sett> sett;

    explicit Mottagare(const std::string& titel)
        : glaze::AppWindow(titel, 320, 200, gfx::Chrome::None)
    {
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type != gfx::EventType::MouseWheel)
            return false;
        sett.push_back({e.mods, modifierKeys()});
        return true;   // ät — inget annat i fönstret ska röra sig
    }
};

void fallWin32Mask()
{
    // Titeln är FindWindowW-nyckeln — unik per process, så två samtidiga
    // körningar inte hittar varandras fönster (pump_gate).
    const std::string  titel = "trackpad_gate-" + std::to_string(GetCurrentProcessId());
    const std::wstring wtitel(titel.begin(), titel.end());
    Mottagare          w(titel);
    w.setOffscreen(true);

    // ANROPA ALDRIG show() HÄR — `run()` ÄR öppnaren.
    bool                  kördes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kördes = true;
            if (!w.isOpen()) {
                std::printf("  --   B Win32 hoppas över: fönstret kom aldrig "
                            "upp\n");
                ++g_skips;
                w.close();
                return;
            }
            const HWND hwnd = FindWindowW(nullptr, wtitel.c_str());
            check(hwnd != nullptr, "B Win32: fönstret hittas via sin titel");
            if (hwnd) {
                BYTE före[256] = {};
                GetKeyboardState(före);
                BYTE nu[256];
                std::memcpy(nu, före, sizeof nu);
                const auto sätt = [&](bool ctrl, bool shift) {
                    nu[VK_CONTROL] = nu[VK_LCONTROL] = ctrl ? 0x80 : 0;
                    nu[VK_SHIFT] = nu[VK_LSHIFT] = shift ? 0x80 : 0;
                    nu[VK_RCONTROL] = nu[VK_RSHIFT] = 0;
                    nu[VK_MENU] = nu[VK_LMENU] = nu[VK_RMENU] = 0;
                    nu[VK_LWIN] = nu[VK_RWIN] = 0;
                    SetKeyboardState(nu);
                };
                // Samma tråd ⇒ WndProc körs SYNKRONT och eventen hamnar i
                // backendens kö — de levereras i nästa pumpvarv. lParam är
                // skärmkoordinater (hjulets), kroken bryr sig inte om var.
                const LPARAM lp = MAKELPARAM(10, 10);
                sätt(true, true);
                SendMessageW(hwnd, WM_MOUSEWHEEL,
                             MAKEWPARAM(MK_CONTROL | MK_SHIFT, WHEEL_DELTA), lp);
                sätt(false, false);
                SendMessageW(hwnd, WM_MOUSEHWHEEL,
                             MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), lp);
                SetKeyboardState(före);   // tråden tillbaka som den var
            }
            break;
        }
        default: {
            check(w.sett.size() == 2,
                  "B Win32: varje hjulmeddelande blev ETT MouseWheel",
                  std::to_string(w.sett.size()));
            if (w.sett.size() == 2) {
                const std::uint32_t båda = gfx::kModCtrl | gfx::kModShift;
                check(w.sett[0].mods == båda && w.sett[0].fönstret == båda,
                      "B Win32: WM_MOUSEWHEEL med Ctrl och Shift nere bär båda "
                      "i masken, och fönstrets modifierKeys() har dem i appens "
                      "krok",
                      mask(w.sett[0].mods) + " / " + mask(w.sett[0].fönstret));
                check(w.sett[1].mods == 0 && w.sett[1].fönstret == 0,
                      "B Win32: WM_MOUSEHWHEEL med tangenterna uppe bär 0 — "
                      "inget Ctrl står kvar i fönstret",
                      mask(w.sett[1].mods) + " / " + mask(w.sett[1].fönstret));
            }
            w.close();
            return;
        }
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();

    // VAKTEN: utan den kan hela den asynkrona halvan tystna och grinden
    // ändå rapportera grönt.
    if (!kördes) {
        std::printf("  --   B Win32 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
    }
}
#endif

#ifdef GLAZE_BACKEND_LINUX
// LINUX — en modifierartangents eget event bär läget EFTER sig
// (`gfx::xkbModsAfterKey`, delad av X11- och Wayland-drivrutinerna, som
// annars bara har läget FÖRE), mot RIKTIGA keymaps ur systemets
// xkeyboard-config: tangenterna slagna upp på sina xkb-NAMN och symbolen den
// PRODUCERADE, så som drivrutinerna läser den — tabellen mäts alltså mot
// verkliga layouter och inte mot sina egna antaganden.
void fallXkbMask()
{
    xkb_context* ctx    = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const auto   keymap = [ctx](const char* layout) -> xkb_keymap* {
        const xkb_rule_names namnen{"evdev", "pc105", layout, "", ""};
        return ctx ? xkb_keymap_new_from_names(ctx, &namnen,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS)
                   : nullptr;
    };
    xkb_keymap* us   = keymap("us");
    xkb_keymap* se   = keymap("se");
    xkb_state*  stUs = us ? xkb_state_new(us) : nullptr;
    xkb_state*  stSe = se ? xkb_state_new(se) : nullptr;
    const auto  städa = [&] {
        if (stUs) xkb_state_unref(stUs);
        if (stSe) xkb_state_unref(stSe);
        if (us) xkb_keymap_unref(us);
        if (se) xkb_keymap_unref(se);
        if (ctx) xkb_context_unref(ctx);
    };
    if (!stUs || !stSe) {
        std::printf("  --   B xkb hoppas över: ingen keymap kunde kompileras "
                    "(saknas xkeyboard-config?)\n");
        ++g_skips;
        städa();
        return;
    }
    const auto symbol = [](xkb_keymap* km, xkb_state* st, const char* namn) {
        const xkb_keycode_t kod = xkb_keymap_key_by_name(km, namn);
        return kod == XKB_KEYCODE_INVALID
                   ? static_cast<xkb_keysym_t>(XKB_KEY_NoSymbol)
                   : xkb_state_key_get_one_sym(st, kod);
    };

    struct Rad {
        const char*   tangent;   // xkb-namnet (evdev)
        std::uint32_t bit;       // tangentens egen bit, 0 = ingen modifierare
    };
    const Rad facit[] = {
        {"LCTL", gfx::kModCtrl}, {"RCTL", gfx::kModCtrl},
        {"LFSH", gfx::kModShift}, {"RTSH", gfx::kModShift},
        {"LALT", gfx::kModAlt},   {"RALT", gfx::kModAlt},   // us: Alt_R
        {"LWIN", gfx::kModCmd},   {"RWIN", gfx::kModCmd},
        {"AC01", 0},              {"SPCE", 0},
        {"CAPS", 0},              {"RTRN", 0},
    };
    for (const Rad& r : facit) {
        const xkb_keysym_t s = symbol(us, stUs, r.tangent);
        // En ANNAN modifierare hålls redan — den ska stå kvar genom båda.
        const std::uint32_t annan =
            r.bit == gfx::kModShift ? gfx::kModCtrl : gfx::kModShift;
        const std::uint32_t trycket = gfx::xkbModsAfterKey(annan, s, true);
        const std::uint32_t släppet =
            gfx::xkbModsAfterKey(annan | r.bit, s, false);
        check(s != XKB_KEY_NoSymbol && trycket == (annan | r.bit) &&
                  släppet == annan &&
                  gfx::xkbModsAfterKey(0, s, true) == r.bit,
              std::string("B xkb (us): ") + r.tangent +
                  (r.bit ? " — trycket lägger till " + mask(r.bit) +
                               ", släppet tar bort det"
                         : std::string(" — ingen modifierare, masken orörd")),
              mask(trycket) + " / " + mask(släppet));
    }
    // AltGr är INTE Alt: på svensk layout är höger Alt `ISO_Level3_Shift`.
    {
        const xkb_keysym_t s = symbol(se, stSe, "RALT");
        check(s == XKB_KEY_ISO_Level3_Shift &&
                  gfx::xkbModsAfterKey(gfx::kModCtrl, s, true) == gfx::kModCtrl &&
                  gfx::xkbModsAfterKey(0, s, false) == 0,
              "B xkb (se): AltGr (ISO_Level3_Shift) rör inte masken",
              mask(gfx::xkbModsAfterKey(gfx::kModCtrl, s, true)));
    }
    städa();
}
#endif

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== trackpad_gate — styrplattan på ritytan ===\n");
    fallK();
    fallV();
#ifdef GLAZE_BACKEND_LINUX
    fallXkbMask();   // headless — tangentens egen bit i masken (B)
#endif

    // OS-skalan ur ett PROVFÖNSTER — användarskalan är 1 när grinden börjar.
    float os = 0.0f;
    {
        Värd p;
        p.setOffscreen(true);
        p.show();
        if (p.isOpen())
            os = p.window().uiScale() / gfx::userScale();
    }
    if (!(os > 0.0f)) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    } else {
        // Två skalor: 1 och 1,5 — användarskalan klampas till 0,5–2, så en
        // ovanlig OS-skala ger närmaste möjliga (skalan står i varje rad).
        for (const float mål : {1.0f, 1.5f}) {
            gfx::setUserScale(mål / os);
            fallMedFönster();
        }
        gfx::setUserScale(1.0f);
#ifdef GLAZE_BACKEND_D3D11
        fallWin32Mask();   // hjulets mask genom den riktiga WndProc:en (B)
#endif
    }
    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf(g_fel ? "=== trackpad_gate: %d FEL av %d ===\n"
                      : "=== trackpad_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    if (g_fel)
        return 1;
    return g_skips ? 77 : 0;
}
