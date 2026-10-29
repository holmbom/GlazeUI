// canvas_gate — RITYTANS GESTER OCH TRÄFFPROV.
//
// Kör:  ./build/tests/canvas_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// GESTERNA ÄR ROUTING OCH KAMERA, och därför matar grinden FÖNSTRETS ingång
// (`AppWindow::handleEvent`, middle_gate-prejudikatet) i ett DOLT fönster med
// en riktig `glaze::Canvas` — trycket, draget, hjulet, tangenterna och
// fokusförlusten går exakt den väg ett OS-event går. Förväntat värde är KAMERANS EGEN
// GEOMETRI läst genom ritytans publika väg (`toWorld`/`toLocal`) och
// handräknade tal (grader per px, zoomsteg), aldrig gesternas formel:
//   * panoreringen: världspunkten under trycket står under pekaren efter
//     draget — också under tilt och vridning;
//   * hjulet: världspunkten under pekaren står kvar efter zoomen;
//   * träffprovet: punkter på ett KÄNT SKÄRMAVSTÅND från en form, räknat ur
//     formens projicerade punkter (`toLocal`) och ren plangeometri.
//
// VAD GRINDEN PÅSTÅR:
//   H   gesternas tal headless (camera_math): hjulets och tangenternas steg,
//       nordsnäppets gräns, och att trögheten STANNAR på en bestämd punkt.
//   A   av från start: varken drag, hjul eller tangent rör kameran, och
//       pekaren är pilen.
//   P   vänsterdrag panorerar — också med pitch 45° och bäring 30° — och
//       fyrar `onCameraChange`; mittendraget likaså.
//   U   `unitsPerPixelAt` — skalan där punkten står — mot en numerisk
//       derivata av `toWorld` längs bildraden under bäring 30° och pitch 50°
//       (fem punkter), lokal under tilt (nere < uppe), tom ovanför
//       horisonten och 1/zoom rakt uppifrån.
//   R   Ctrl+drag vrider 0,8°/px och tiltar 0,5°/px, tilten klampad till
//       maxPitch; Ctrl med mittenknappen likaså.
//   N   nordsnäppet: en vridning som släpps 5° från norr glider till exakt
//       0, en som släpps 20° från norr står kvar.
//   Z   hjulet zoomar mot pekaren (också under tilt), klampat till
//       `setZoomRange`; ett hjul utan zoom bubblar.
//   K   pilarna panorerar ett stilsteg, Shift+pilarna vrider 15° och tiltar
//       10°, + och − zoomar en nivå.
//   E   appens handler går FÖRST: `handled` på trycket, draget, hjulet och
//       tangenten gör att kameran står still. Och en rörelse under ritytans
//       eget drag når `onMouseMove` EN gång, med knappen (inte en gång till
//       knapplös genom fönstrets hover-krok).
//   S   sättarna fyrar ALDRIG `onCameraChange`.
//   F   fokusförlust mitt i ett drag: inget mer drag och ingen tröghet.
//   C   pekaren: hårkorset över en rityta med gester, flytta-pekaren under
//       vänster- och mittendraget, tillbaka efteråt.
//   O   overlay-lagret: `onPaintOverlay` ritas vid presenten (inte i
//       ritytans ritning), klippt till ritytan; `invalidateOverlay` ger en
//       ny present.
//   T   träffprovet: linje (px-bredd) inom/utom halva bredden + toleransen
//       vid zoom 4 och under tilt, ytan med hål, överst först, shapeAt,
//       en markör, en bild, borttagning (indexen flyttar, rutnätet byggs
//       om) och en text som kollisionen dolt — och en FÖRANKRAD markör
//       (ankaret Bottom: träffad ovanför punkten, inte under den) och en
//       BILDMARKÖR (hela rutan, också hörnet utanför en cirkel). Och
//       en BILD PÅ FYRA FRIA HÖRN: träffad i
//       fyrhörningen men inte i lådans hörn utanför den, och en fluga (en
//       fyrhörning som inte ritas) träffas aldrig. Sabotage som ska fälla
//       den: `imageHit` med lådan i stället för ringen.
//   I   TRÖGHETEN (asynkront, genom pumpen): en kastad panorering glider i
//       kastets riktning, fyrar `onCameraChange` från pump-svansen, tar slut
//       — och SLUTAR BE OM FRAMES: ingen ritning alls under ett fönster av
//       vila efteråt (räknat ur ritytans egna ritningar).
//   L   ETIKETTERNAS TONING (asynkront, samma mätning): en etikett som
//       kollisionen visar tonar in, landar — och ritytan slutar be om frames.
//   LO  LAGRENS ORDNING: tre överlappande ytor i lager 1–3,
//       `moveLayer(3, 1)` — träffprovet svarar med PLATSERNA (överst lager 3
//       med den gröna formen, underst lager 1 med den blå, ur `shape()`),
//       flytten ritar om ritytan EN gång, och till samma plats och utanför
//       ritar inte om.
//   M   TEXTENS MÅTT (`Canvas::measureText`): M1 nullopt utan
//       fönster; M2 mätningen före ritningen = efter den (1e-4) i fyra
//       stilar med en reservglyf (scene_pixel_gate del R:s kodpunkter); M3 =
//       `Painter::measureText` i onPaint; M4 enheterna — mm, px / zoom och
//       världsenheter oberoende av zoomen; M5 fet bredare, rader ur '\n' och
//       brytbredden, tom text en rad, baslinjen inom raden; M6 mätningen
//       utanför en frame laddar inte upp; M7 storleken 0 ger nollor; M8
//       maxLines/maxHeight ger raderna och `truncated`, bredden
//       inom brytbredden; M9 golvet och taket i px — bredden vid låg och hög
//       zoom är golvets respektive takets andel av den råa.
//   ZV  SYNLIGHETEN EFTER ZOOM: `hitTest` träffar varken en
//       text med intervall eller en markör utan kollision utanför intervallet,
//       men båda innanför. Sabotage som SKA fälla M9/ZV: mätningen
//       utan klampning (M9), rutnätet utan zoom (ZV — markören).
//   IN  INMATNINGEN:
//       IN1 ett högertryck handlern tar ger
//       `onMouseMove`/`onMouseUp` med Right, EN rörelse per rörelse, och
//       ingen snabbmeny; IN2 utan `handled` snabbmenyn och inget
//       `onMouseUp(Right)`; IN3 `onDoubleClick` med Middle och Right; IN4
//       fokusförlusten mitt i en vänsterpanorering ger `onMouseCancel(Left)`
//       och ingen tröghet, inte heller av ett senare släpp; IN5 `onKeyUp` vid
//       släppet och med `cancelled` vid fokusförlusten; IN6 basens `onKeyDown`
//       utan `handled` — pilen panorerar som vanligt. Fall E (en tangent appen
//       tog panorerar inte) mäter samma flytt från andra hållet.
//   MR  MÄTNINGEN I LAYOUTEN (`Canvas::measureText(c, …)`) i
//       ett eget dolt fönster (`Läsare`), allt före fönstrets första ritning.
//       Fixturen `Dokument` mäter en lång text i sin `heightForWidth` med
//       brytbredden = bredden och räknar DJUPET kroken anropas i (taket 8 —
//       där svarar den 10 och skriver djupet 99, så ett sabotage aldrig
//       spräcker stacken): MR1 i en Panel och MR2 direkt under roten, båda
//       sträckta vänster–höger — djupet 1, inga nollsvar, och `actualSize().h`
//       = formen UTAN Ctx mätt utanför layouten med den bredden (1e-3); MR3
//       formen utan Ctx i kroken — djupet 2, minst ett
//       nollsvar och aldrig taket; MR4 formen med Ctx (`behaviorCtx()`) =
//       formen utan för text i px, i mm (mätt i mm och px), i världsenheter
//       med `setCamera` zoom 4 och med en `setMapScale` satt före öppningen
//       (fixturen: zoomen är inte 1); MR5 en 3D-yta med AUTO-mått som aldrig
//       ritats — världsmåtten nullopt, px mäts — och ett Ctx utan
//       `canvas` nullopt; MR5b efter första ritningen är 3D-ytans världsmått
//       lika i båda formerna.
//   RT  TRÄFFPROVETS NYA FORMER, förväntat värde ur `toLocal` och
//       plangeometri: RT1 en yta med genomskinlig fyllning träffas i mitten
//       bara med `Stroke::hitInside`, och en OSYNLIG sluten linje med den
//       träffas inne men inte tol + 1 px utanför kanten; RT2 `HitOptions::
//       tolerance` px(12) och world(3) (= 12 px vid zoom 4) — linjen (2 px)
//       träffad på 12,5 px, inte på 13,5 — utan den stilens, och en linje i
//       rymden med samma tolerans; RT3 `element` i ett punktlager: den
//       närmaste av två överlappande punkter, vid lika den senare, `point` i
//       den träffade punkten, och en kolliderande markörform som etikett med
//       sin punkt; RT4 `hitTest(Rect, HitMode)` vriden 30° och tiltad 45° —
//       ytans projicerade låda ± 1 px i fönstret och korsningen (också dragen
//       baklänges), en ruta inne i ytan bara i korsningen, linjens ändar och
//       mitt, markörerna punkt för punkt, och en ruta inne i en osynlig ram
//       med `hitInside`. Sabotage som SKA fälla RT: `gridFor` utan
//       `insideCounts` (RT1, den osynliga linjen), `tolWorld` oläst i
//       `hitLayer` (RT2 world), `pk.lineTol` = stilens (RT2 rymden), `nearer`
//       utan avståndet (RT3 första), `point` ur `points().front()` (RT3),
//       `rectInsideRings` bort (RT4 rutan inne i ytan och ramen), fönstret
//       som korsningen (RT4 − 1 px).
//   D1  RITYTANS LÖPANDE HÄNDELSER RITAR OM RITYTAN, INTE FÖNSTRET
//       i ett eget dolt fönster med en etikett bredvid
//       ritytan som räknar sina ritningar: efter hjulets och nypningens
//       `onCameraChange`, efter `onGesture` och efter snäppets POSTADE
//       `onCameraChange` (genom pumpen) ritas ritytan om men inte etiketten;
//       och en `onCameraChange` som sätter etikettens text via
//       SÄTTAREN ritar om etiketten (sättarvägen räcker). Sabotage som SKA
//       fälla D1: `appWindow()->invalidate()`
//       tillbaka efter handlern i `mouseWheel` (hjulet), i `gesture`
//       (nypningen, onGesture) och i `postCameraChange` (snäppet);
//       `Label::setText` utan sin `invalidate()` (sättarfallet).
//   FL  EN INAKTIV FLIK eget
//       dolt fönster: ritytan står i en dockningsvärds inaktiva flik (synlig,
//       tom rekt) och ritas inte. FL1 `zoomToRect` FÖRE första ritningen
//       väntar och ramar in mot den riktiga ytan när fliken väljs; FL2 EFTER
//       första ritningen, åter inaktiv, ändrar den inte kameran (den cachade
//       ytan gäller inte) och ramar in när fliken väljs igen. FL3/FL4 en satt
//       kamera (2D resp. Camera3D) efter en väntande zoomToRect resp.
//       zoomToBox + panBy vinner vid aktiveringen. Sabotage (SKA fälla):
//       `zoomToRect` mot `knownBox` (FL1 — inramad mot
//       0 × 0; FL2 — kameran ändras direkt); `dropPendingFits` bort ur
//       `setCamera` (FL3) resp. ur `setCamera(Camera3D)` (FL4).
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * att backenderna levererar hjulets position, mittenknappen och
//     modifierarna — grinden matar ingången direkt (TESTS-rader);
//   * att OS:et VISAR pekaren — `cursor()` svarar med den BEGÄRDA formen;
//   * hur overlay-lagret SER UT (det ritas i backbuffern efter compositen,
//     som `captureFrame` inte läser) — bara att det ritas och var klippet är;
//   * etikettkollisionen i pixlar — det är scene_pixel_gate del G;
//   * trögheten i en riktig hand: farten här kommer ur sovande trådar, och
//     en belastad maskin kan glesa ut samplen (fallet kräver bara att en
//     tröghet startar och stannar, inte hur långt den går);
//   * att texten SER ut att ha det mått `measureText` svarar (etiketterna i
//     pixlar är scene_pixel_gate del D och G), etiketternas omplacering efter
//     en `moveLayer` i pixlar, och M2 på Metal och Linux förrän grinden körs
//     där;
//   * i MR: att texten SER ut så (scene_pixel_gate del D och G), en väntande
//     inramning (`zoomToRect`/`zoomToBox`/`panBy` — formen med Ctx räknar den
//     inte, med flit), loggraden vakten skriver, och reentrans mellan TVÅ
//     ritytor (en rityta som mäter en annan utan Ctx — vakten är per
//     rityta, en skriven gräns).
//   * i RT: rutan mot en form som närplanet skär av, mot etiketter (text),
//     bilder och terräng, och att formerna i rymden UTELÄMNAS av rutan;
//     `element` i 3D-målet med terräng;
//   * i D1: en annan widgets ritning som läser ett fält handlern ändrat UTAN
//     sättare — det är D1:s medvetna kontraktsbrott (canvas.h), och den
//     ritas inte om; trögheten (samma `postCameraChange` som snäppet) och
//     tangenternas kameraändring, som ritar om hela fönstret som vanligt.
//
// SABOTAGEN den fäller: zoom kring centrum (Z), trögheten som invaliderar för evigt
// (I), `setCenter` som fyrar eventet (S), mittengrenen bort (P), och
// träffprovet i världsavstånd (T); och
// markörens ankare ignorerat i träffprovet och bildmarkören träffad som en
// cirkel (T), `onMouseMove` två gånger per rörelse under draget (E), och
// `unitsPerPixelAt` med MITTENS skala eller per FYSISK px (U — det senare
// fälls bara där fönstrets skala inte är 1, som vid 125 % på Windows; i WSLg
// är logiska och fysiska px samma sak). För lager och mätning:
// `moveLayer` utan `invalidate()` och
// rotationen åt fel håll (LO), mätningen genom `buildText` (M6), `unit`
// ignorerad (M4), delarna ignorerade (M5 fet) och `SdfAtlas::advance` med
// fel stils font (M2 fet och kursiv, M5). För inmatningen:
// snabbmenyn fyras också när trycket togs och
// `Canvas::rightDrag` som inte levererar (IN1), `mouseMove`-vakten utan
// `rightPressed()` (IN1: en knapplös rörelse till), `mouseCancel` som lämnar
// gesten (IN4), `dropPointer` bort ur fokusförlusten (IN4, F), och ritytans
// eget `onKeyDown` kvar som skuggar basens (E, IN6 — handlern körs aldrig).
// För MR: sabotagen SKA fälla den:
// formen med Ctx implementerad som formen utan (behaviorCtx + resolve —
// MR1, MR2 → djupet 99, eller 2 med nollsvar om vakten följer med), formen
// med Ctx som frågar `knownBox` (MR1, MR2 — ingen yta före första
// ritningen, så den resolvar), vakten borttagen (MR3 → 99), `pendingScale_`
// ignorerad i `layoutPxPerUnit` (MR4d) och ett värde utan täthet — ingen
// `measureNeedsWorld`-prövning i `measureWith` (MR5).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <glaze/glaze.h>

#include "canvas/camera_math.h"   // intern — gesternas tal (del H)

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

const char* formNamn(gfx::PointerShape s)
{
    switch (s) {
    case gfx::PointerShape::Arrow:      return "Arrow";
    case gfx::PointerShape::IBeam:      return "IBeam";
    case gfx::PointerShape::Hand:       return "Hand";
    case gfx::PointerShape::ResizeWE:   return "ResizeWE";
    case gfx::PointerShape::ResizeNS:   return "ResizeNS";
    case gfx::PointerShape::NotAllowed: return "NotAllowed";
    case gfx::PointerShape::Crosshair:  return "Crosshair";
    case gfx::PointerShape::Move:       return "Move";
    case gfx::PointerShape::ClosedHand: return "ClosedHand";
    case gfx::PointerShape::Hidden:     return "Hidden";
    case gfx::PointerShape::Wait:       return "Wait";
    }
    return "?";
}

// ================================================================ HEADLESS

void fallH()
{
    using namespace glaze::detail;
    check(nära(wheelZoomFactor(1.0f) * wheelZoomFactor(-1.0f), 1.0, 1e-12) &&
              nära(wheelZoomFactor(2.0f), 2.0, 1e-12) &&
              wheelZoomFactor(0.25f) > 1.0,
          "H hjulet: två rader är en zoomnivå, upp och ner tar ut varandra, "
          "en pekplattas bråkdel zoomar lite",
          tal(wheelZoomFactor(1.0f)));
    check(keyZoomFactor(true) == 2.0 && keyZoomFactor(false) == 0.5,
          "H + och − är en hel zoomnivå");
    // Förvalet är NORDSNÄPPET (steget 360, 7°); de andra stegen och
    // toleranserna mäter trackpad_gate del V.
    const float steg = defaultRotationSnap(), tol = defaultRotationSnapTolerance();
    check(steg == 360.0f && tol == 7.0f && snapBearing(5.0f, steg, tol) == 0.0f &&
              snapBearing(-6.9f, steg, tol) == 0.0f &&
              !snapBearing(7.5f, steg, tol) && !snapBearing(0.0f, steg, tol) &&
              !snapBearing(-170.0f, steg, tol),
          "H vridsnäppets förval: inom 7° från norr (inte norr själv)");

    // TRÖGHETEN: 20 px per 20 ms (1 px/ms) — en plan; den stannar.
    std::vector<PanSample> s;
    for (int i = 0; i < 6; ++i)
        s.push_back(PanSample{1000u + 20u * static_cast<unsigned>(i),
                              100.0f + 20.0f * i, 50.0f});
    InertiaPlan p;
    const bool  start = inertiaStart(s, 1100u, 1.0f, p);
    check(start && nära(p.vx, 1.0, 1e-9) && p.vy == 0.0 && p.durationMs > 0.0,
          "H trögheten: farten ur de sista samplen", tal(p.vx));
    double x0 = 0, y0 = 0, xh = 0, yh = 0, xt = 0, yt = 0, x2 = 0, y2 = 0;
    inertiaOffset(p, 0.0, x0, y0);
    inertiaOffset(p, p.durationMs / 2.0, xh, yh);
    inertiaOffset(p, p.durationMs, xt, yt);
    inertiaOffset(p, p.durationMs * 2.0, x2, y2);
    check(x0 == 0.0 && xt > 0.0 && x2 == xt && xh > 0.5 * xt && xh < xt,
          "H trögheten börjar i släppet, saktar in och STANNAR på en bestämd "
          "punkt (efter slutet rör den sig inte)",
          tal(xh) + " / " + tal(xt) + " / " + tal(x2));
    check(!inertiaStart(s, 1200u, 1.0f, p),
          "H ingen tröghet när handen stannade före släppet");
    // BUFFERTEN ÄR TIDSBASERAD: tusen sampel på en sekund (en mus på 1000 Hz)
    // behåller hela fönstret bakåt, inte bara de sista sextonde delarna.
    std::vector<PanSample> tät;
    for (unsigned i = 0; i <= 1000; ++i)
        addPanSample(tät, PanSample{5000u + i, static_cast<float>(i), 0.0f});
    check(!tät.empty() && tät.back().ms == 6000u && tät.front().ms <= 5900u &&
              tät.front().ms >= 5800u,
          "H trögheten: sampelbufferten täcker mätfönstret i TID, också vid "
          "1000 Hz",
          std::to_string(tät.size()) + " sampel, äldst " +
              std::to_string(6000u - tät.front().ms) + " ms bak");
    InertiaPlan tp;
    const bool  tätStart = inertiaStart(tät, 6000u, 1.0f, tp);
    check(tätStart && nära(tp.vx, 1.0, 1e-9),
          "H …och farten ur den bufferten är rörelsens (1 px/ms)", tal(tp.vx));
    std::vector<PanSample> långsam;
    for (int i = 0; i < 6; ++i)
        långsam.push_back(PanSample{1000u + 20u * static_cast<unsigned>(i),
                                    100.0f + 0.5f * i, 50.0f});
    check(!inertiaStart(långsam, 1100u, 1.0f, p),
          "H ingen tröghet för en långsam rörelse");
}

// ============================================================= MED FÖNSTER

constexpr std::uint32_t kTimer = 7311;   // grindens egen (appens intervall)

struct Yta : glaze::Canvas {
    int           ritningar = 0;
    std::uint64_t senast    = 0;   // ticksMs vid senaste ritningen

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        senast = gfx::ticksMs();
        glaze::Canvas::draw(c);
    }
};

struct Värd : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Yta                   yta;
    std::function<void()> vidTimer;   // fortsättningen när timern slår
    // onCameraChange räknas i fönstret och inte i en lokal: händelserna ur
    // ritningen (snäppet, trögheten) kommer i pump-svansen, efter steget.
    int ändringar = 0;

    Värd() : glaze::AppWindow("canvas_gate", 640, 480, gfx::Chrome::None)
    {
        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 20;
        yta.width  = 400;
        yta.height = 300;
        yta.setBorder(false);
        root().adopt(yta);
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::Timer && e.timerId == kTimer && vidTimer) {
            window().stopTimer(kTimer);
            auto f   = std::move(vidTimer);
            vidTimer = nullptr;
            f();
        }
        return false;
    }

    gfx::Rectf box() { return yta.resolve(yta.behaviorCtx()); }
    float      skala() { return window().uiScale(); }
    // Ritytans lokala logiska px → fönstrets fysiska.
    gfx::Pointf fys(double lx, double ly)
    {
        const gfx::Rectf b = box();
        return gfx::Pointf{static_cast<float>(b.x + lx * skala()),
                           static_cast<float>(b.y + ly * skala())};
    }
    void mus(gfx::EventType t, gfx::Pointf p, gfx::MouseButton b,
             std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = b;
        e.mods     = mods;
        handleEvent(e);
    }
    void ner(gfx::Pointf p, std::uint32_t mods = 0,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        mus(gfx::EventType::MouseButtonDown, p, b, mods);
    }
    void upp(gfx::Pointf p, std::uint32_t mods = 0,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        mus(gfx::EventType::MouseButtonUp, p, b, mods);
    }
    void flytta(gfx::Pointf p, std::uint32_t mods = 0)
    {
        mus(gfx::EventType::MouseMotion, p, gfx::MouseButton::None, mods);
    }
    void hjul(gfx::Pointf p, float rader)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelY   = rader;
        handleEvent(e);
    }
    void tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void rita() { window().renderFrame(); }
    gfx::PointerShape pekare() { return window().cursor(); }

    // Ny kamera och inga gester — varje fall börjar från samma läge.
    void nollställ()
    {
        yta.setPannable(false);
        yta.setZoomable(false);
        yta.setRotatable(false);
        yta.setPitchable(false);
        yta.setInertia(false);
        yta.setZoomRange(0.0, 0.0);
        yta.setMaxPitch(60.0f);
        yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f});
        yta.onMouseDown  = nullptr;
        yta.onMouseMove  = nullptr;
        yta.onMouseUp    = nullptr;
        yta.onMouseWheel = nullptr;
        yta.onKeyDown    = nullptr;
        rita();   // ytan och kameran som de ritas — toWorld läser den
    }
    glaze::Point värld(double lx, double ly)
    {
        const auto w = yta.toWorld(glaze::Point{lx, ly});
        return w ? *w : glaze::Point{NAN, NAN};
    }
    glaze::Point världTillLokal(double wx, double wy)
    {
        const auto p = yta.toLocal(glaze::Point{wx, wy});
        return p ? *p : glaze::Point{NAN, NAN};
    }

    void synkront();
    void lagerordning();   // LO
    void mätning();        // M
    void träffprovRT();    // RT
    void inmatning();      // IN
    void tröghet();
    void toning();
    void klar() { close(); }
};

void Värd::synkront()
{
    yta.onCameraChange = [this] { ++ändringar; };
    yta.setYUp(true);

    // ---- A. AV FRÅN START (en ny rityta — `nollställ` slår av trögheten)
    const glaze::Canvas ny;
    check(!ny.pannable() && !ny.zoomable() && !ny.rotatable() &&
              !ny.pitchable() && ny.inertia() && !ny.labelFade(),
          "A gesterna och toningen är av från start, trögheten på");
    nollställ();
    const glaze::Camera k0 = yta.camera();
    ner(fys(100, 100));
    flytta(fys(180, 140));
    upp(fys(180, 140));
    hjul(fys(100, 100), 1.0f);
    yta.focus();
    tangent(gfx::Key::Right);
    tangent(gfx::Key::Plus);
    const glaze::Camera k1 = yta.camera();
    check(k1.center.x == k0.center.x && k1.center.y == k0.center.y &&
              k1.zoom == k0.zoom && ändringar == 0,
          "A utan gester rör varken drag, hjul eller tangent kameran",
          pt(k1.center) + " ×" + tal(k1.zoom));
    flytta(fys(50, 50));
    check(pekare() == gfx::PointerShape::Arrow,
          "A pekaren över en rityta utan gester är pilen", formNamn(pekare()));

    // ---- P. PANORERINGEN — rakt uppifrån, under tilt och med mittenknappen
    // Toleransen: en fyrtiondels logisk px i världen (pekarens läge går som
    // float genom händelsen).
    const auto tolerans = [this] { return 0.025 / yta.zoom(); };
    const auto panorera = [&](const char* vad, gfx::MouseButton knapp) {
        const glaze::Point grepp = värld(100, 100);
        const int          före  = ändringar;
        ner(fys(100, 100), 0, knapp);
        flytta(fys(170, 60));
        flytta(fys(230, 140));
        const glaze::Point under = värld(230, 140);
        upp(fys(230, 140), 0, knapp);
        check(näraP(under, grepp, tolerans()) && ändringar > före,
              std::string("P ") + vad +
                  ": världspunkten under trycket står under pekaren",
              pt(grepp) + " → " + pt(under) + ", " +
                  std::to_string(ändringar - före) + " onCameraChange");
    };
    nollställ();
    yta.setPannable(true);
    panorera("vänsterdrag rakt uppifrån", gfx::MouseButton::Left);
    yta.setCamera(glaze::Camera{{674000.0, 6580000.0}, 3.0, 30.0f, 45.0f});
    rita();
    panorera("vänsterdrag med bäring 30° och pitch 45° (SWEREF)",
             gfx::MouseButton::Left);
    nollställ();
    yta.setPannable(true);
    panorera("MITTENDRAG", gfx::MouseButton::Middle);
    check(!yta.middlePressed(), "P mittentrycket är släppt efteråt");

    // ---- U. SKALAN DÄR PUNKTEN STÅR (`unitsPerPixelAt` — demons skalstock)
    // Förväntat värde är en NUMERISK DERIVATA av toWorld längs bildraden — världs-
    // avståndet mellan två lokala punkter en logisk px åt var sitt håll,
    // delat med två — och inte fasadens `pxPerUnitAt`, som frågan bygger på.
    // Längs en bildrad har punkterna samma djup och avbildningen är affin,
    // så derivatan är exakt utom avrundningen (de absoluta SWEREF-talen).
    nollställ();
    yta.setCamera(glaze::Camera{{674000.0, 6580000.0}, 3.0, 30.0f, 50.0f});
    rita();
    {
        int         fel = 0;
        std::string fick;
        for (const glaze::Point p :
             {glaze::Point{20, 20}, glaze::Point{200, 150},
              glaze::Point{380, 280}, glaze::Point{100, 250},
              glaze::Point{300, 40}}) {
            const auto         u = yta.unitsPerPixelAt(p);
            const glaze::Point a = värld(p.x - 1.0, p.y);
            const glaze::Point b = värld(p.x + 1.0, p.y);
            const double facit = std::hypot(b.x - a.x, b.y - a.y) / 2.0;
            if (!u || !nära(*u, facit, 1e-6 * facit)) {
                ++fel;
                fick += pt(p) + ": " + (u ? tal(*u) : "tomt") + " mot " +
                        tal(facit) + " ";
            }
        }
        check(fel == 0,
              "U unitsPerPixelAt = toWorlds derivata längs raden, bäring 30° "
              "och pitch 50°, fem punkter",
              fick);
        const auto nere = yta.unitsPerPixelAt(glaze::Point{200, 280});
        const auto uppe = yta.unitsPerPixelAt(glaze::Point{200, 20});
        check(nere && uppe && *nere < *uppe,
              "U under tilt mäter den lokalt: färre enheter per px nere "
              "(närmare kameran) än uppe",
              (nere ? tal(*nere) : "tomt") + " / " +
                  (uppe ? tal(*uppe) : "tomt"));
        // Horisonten ligger ~230 px ovanför ytan vid pitch 50° (fov 36,87°
        // på 300 px höjd) — en punkt långt ovanför den har ingen världspunkt.
        check(!yta.unitsPerPixelAt(glaze::Point{200, -1000}) &&
                  !yta.toWorld(glaze::Point{200, -1000}),
              "U ovanför horisonten: tomt, som toWorld");
        yta.setCamera(glaze::Camera{{0.0, 0.0}, 4.0, 40.0f, 0.0f});
        rita();
        const auto platt = yta.unitsPerPixelAt(glaze::Point{50, 60});
        check(platt && nära(*platt, 0.25, 1e-12),
              "U rakt uppifrån: 1/zoom överallt, också vriden 40°",
              platt ? tal(*platt) : "tomt");
    }

    // ---- R. CTRL+DRAG vrider och tiltar
    nollställ();
    yta.setRotatable(true);
    yta.setPitchable(true);
    ner(fys(200, 150), gfx::kModCtrl);
    flytta(fys(250, 130), gfx::kModCtrl);   // +50 vågrätt, 20 uppåt
    upp(fys(250, 130), gfx::kModCtrl);
    // Förväntat värde i LOGISKA px: draget går i fysiska, gesten räknar om med skalan.
    const glaze::Camera k = yta.camera();
    check(nära(k.bearing, 50.0 * 0.8, 0.05) && nära(k.pitch, 20.0 * 0.5, 0.05),
          "R Ctrl+drag: 0,8° bäring per logisk px vågrätt, 0,5° tilt per px "
          "uppåt",
          tal(k.bearing) + "° / " + tal(k.pitch) + "°");
    ner(fys(200, 250), gfx::kModCtrl);
    flytta(fys(200, 20), gfx::kModCtrl);    // 230 px uppåt — långt förbi taket
    upp(fys(200, 20), gfx::kModCtrl);
    check(yta.pitch() == yta.maxPitch(),
          "R tilten klampas till maxPitch", tal(yta.pitch()));
    nollställ();
    yta.setRotatable(true);
    ner(fys(200, 150), gfx::kModCtrl, gfx::MouseButton::Middle);
    flytta(fys(150, 150), gfx::kModCtrl);
    upp(fys(150, 150), gfx::kModCtrl, gfx::MouseButton::Middle);
    check(nära(yta.bearing(), -50.0 * 0.8, 0.05) && yta.pitch() == 0.0f,
          "R Ctrl+mittendrag vrider (tilten av — ingen tilt)",
          tal(yta.bearing()));

    // ---- N. NORDSNÄPPET
    nollställ();
    yta.setRotatable(true);
    yta.setBearing(20.0f);
    rita();
    ner(fys(200, 150), gfx::kModCtrl);
    flytta(fys(200 - 15.0 / 0.8, 150), gfx::kModCtrl);   // −15° ⇒ 5°
    upp(fys(200 - 15.0 / 0.8, 150), gfx::kModCtrl);
    check(yta.isEasing() && nära(yta.bearing(), 5.0, 0.05),
          "N släppt 5° från norr: en glidning börjar", tal(yta.bearing()));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    rita();
    check(!yta.isEasing() && yta.bearing() == 0.0f,
          "N …och landar EXAKT på norr", tal(yta.bearing()));
    yta.setBearing(20.0f);
    rita();
    ner(fys(200, 150), gfx::kModCtrl);
    flytta(fys(201, 150), gfx::kModCtrl);
    upp(fys(201, 150), gfx::kModCtrl);
    check(!yta.isEasing() && nära(yta.bearing(), 20.8, 0.05),
          "N släppt 20° från norr: står kvar", tal(yta.bearing()));

    // ---- Z. HJULET mot pekaren
    const auto zooma = [&](const char* vad) {
        const glaze::Point   före  = värld(310, 70);
        const double         zFöre = yta.zoom();
        const int            ä     = ändringar;
        hjul(fys(310, 70), 1.0f);
        rita();
        const glaze::Point efter = värld(310, 70);
        check(nära(yta.zoom(), zFöre * std::sqrt(2.0), 1e-9 * zFöre) &&
                  näraP(efter, före, tolerans()) && ändringar == ä + 1,
              std::string("Z ") + vad +
                  ": en rad är √2, och världspunkten under pekaren står kvar",
              pt(före) + " → " + pt(efter) + " ×" + tal(yta.zoom() / zFöre));
    };
    nollställ();
    yta.setZoomable(true);
    zooma("rakt uppifrån");
    yta.setCamera(glaze::Camera{{500.0, -300.0}, 1.5, -20.0f, 40.0f});
    rita();
    zooma("under tilt 40° och bäring −20°");
    yta.setZoomRange(0.5, 2.0);
    for (int i = 0; i < 6; ++i)
        hjul(fys(200, 150), 1.0f);
    check(yta.zoom() == 2.0, "Z klampas till setZoomRange", tal(yta.zoom()));
    yta.setZoomable(false);
    const double zFöre = yta.zoom();
    hjul(fys(200, 150), -1.0f);
    check(yta.zoom() == zFöre, "Z utan zoom: hjulet rör inte kameran");

    // ---- K. TANGENTERNA
    nollställ();
    yta.setPannable(true);
    yta.setZoomable(true);
    yta.setRotatable(true);
    yta.setPitchable(true);
    yta.focus();
    const glaze::Style& st   = style();
    const glaze::Point  mål  = värld(200 + st.canvasPanStep, 150);
    tangent(gfx::Key::Right);
    rita();
    check(näraP(värld(200, 150), mål, tolerans()),
          "K högerpilen: världspunkten ett stilsteg till höger hamnar i mitten",
          pt(värld(200, 150)) + " mot " + pt(mål));
    tangent(gfx::Key::Left, gfx::kModShift);
    tangent(gfx::Key::Up, gfx::kModShift);
    check(nära(yta.bearing(), -15.0, 1e-4) && nära(yta.pitch(), 10.0, 1e-4),
          "K Shift+vänster vrider −15°, Shift+upp tiltar +10°",
          tal(yta.bearing()) + "° / " + tal(yta.pitch()) + "°");
    const double z0 = yta.zoom();
    tangent(gfx::Key::Plus);
    check(nära(yta.zoom(), 2.0 * z0, 1e-9), "K + zoomar in en nivå",
          tal(yta.zoom()));
    tangent(gfx::Key::Minus);
    tangent(gfx::Key::Minus);
    check(nära(yta.zoom(), 0.5 * z0, 1e-9), "K − zoomar ut en nivå",
          tal(yta.zoom()));

    // ---- E. APPENS HANDLER FÖRST
    nollställ();
    yta.setPannable(true);
    yta.setZoomable(true);
    yta.onMouseDown = [](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        e.handled = true;
    };
    const glaze::Camera e0 = yta.camera();
    ner(fys(100, 100));
    flytta(fys(200, 200));
    upp(fys(200, 200));
    check(yta.camera().center.x == e0.center.x &&
              yta.camera().center.y == e0.center.y,
          "E ett tryck appen tog startar ingen panorering");
    yta.onMouseDown = nullptr;
    yta.onMouseMove = [](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        e.handled = true;
    };
    ner(fys(100, 100));
    flytta(fys(200, 200));
    upp(fys(200, 200));
    check(yta.camera().center.x == e0.center.x,
          "E ett drag appen tog flyttar inte kameran");
    // EN rörelse ger EN `onMouseMove`: under ritytans eget drag kommer den
    // genom draget, MED knappen — fönstrets hover-krok frågar widgeten under
    // pekaren också, och den vägen får inte ge appen samma rörelse en gång
    // till utan knapp (ett gummiband hade släppt sitt drag; demons graf).
    // Handlern TAR rörelserna, så kameran står kvar för fallen nedan.
    int rörelser = 0, utanKnapp = 0;
    yta.onMouseMove = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        ++rörelser;
        if (e.button != gfx::MouseButton::Left)
            ++utanKnapp;
        e.handled = true;
    };
    ner(fys(100, 100));
    flytta(fys(150, 150));
    flytta(fys(200, 200));
    upp(fys(200, 200));
    check(rörelser == 2 && utanKnapp == 0,
          "E en rörelse under ritytans drag når onMouseMove EN gång, med "
          "knappen");
    flytta(fys(210, 210));
    check(rörelser == 3 && utanKnapp == 1,
          "E en rörelse utan tryck når onMouseMove utan knapp");
    yta.onMouseMove  = nullptr;
    yta.onMouseWheel = [](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        e.handled = true;
    };
    hjul(fys(100, 100), 1.0f);
    check(yta.zoom() == e0.zoom, "E ett hjulsteg appen tog zoomar inte");
    yta.onMouseWheel = nullptr;
    yta.onKeyDown    = [](glaze::Widget*, glaze::KeyArgs& e) { e.handled = true; };
    yta.focus();
    tangent(gfx::Key::Right);
    check(yta.camera().center.x == e0.center.x,
          "E en tangent appen tog panorerar inte");
    yta.onKeyDown = nullptr;

    // ---- S. SÄTTARNA FYRAR ALDRIG onCameraChange
    const int s0 = ändringar;
    yta.setCenter(12.0, 34.0);
    yta.setZoom(3.0);
    yta.setBearing(10.0f);
    yta.setPitch(20.0f);
    yta.setCamera(glaze::Camera{{1.0, 2.0}, 1.0, 0.0f, 0.0f});
    yta.zoomToRect(glaze::Rect{0, 0, 100, 50});
    yta.easeTo(glaze::Camera{{9.0, 9.0}, 2.0, 5.0f, 0.0f}, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    rita();
    rita();
    check(ändringar == s0, "S sättarna och easeTo fyrar inte onCameraChange",
          std::to_string(ändringar - s0));

    // ---- F. FOKUSFÖRLUST mitt i ett drag
    nollställ();
    yta.setPannable(true);
    yta.setInertia(true);
    ner(fys(100, 100));
    flytta(fys(130, 100));
    const glaze::Camera f0 = yta.camera();
    enkel(gfx::EventType::WindowFocusLost);
    flytta(fys(250, 200));
    upp(fys(250, 200));
    check(yta.camera().center.x == f0.center.x &&
              yta.camera().center.y == f0.center.y && !yta.isAnimating(),
          "F efter fokusförlusten: inget drag, inget släpp, ingen tröghet");
    enkel(gfx::EventType::WindowFocusGained);
    yta.setInertia(false);

    // ---- C. PEKAREN
    nollställ();
    yta.setPannable(true);
    flytta(fys(150, 150));
    check(pekare() == gfx::PointerShape::Crosshair,
          "C hårkorset över en rityta med gester", formNamn(pekare()));
    ner(fys(150, 150));
    flytta(fys(160, 150));
    // Panoreringen visar den STÄNGDA HANDEN;
    // vridningen behåller flytta-pekaren — den mäter cursor_gate C3.
    check(pekare() == gfx::PointerShape::ClosedHand,
          "C den stängda handen medan vänsterdraget panorerar",
          formNamn(pekare()));
    upp(fys(160, 150));
    check(pekare() == gfx::PointerShape::Crosshair,
          "C hårkorset igen efter släppet", formNamn(pekare()));
    ner(fys(150, 150), 0, gfx::MouseButton::Middle);
    flytta(fys(450, 150));   // utanför ritytan (400 bred), inne i fönstret
    check(pekare() == gfx::PointerShape::ClosedHand,
          "C den stängda handen under mittendraget, också utanför ytan",
          formNamn(pekare()));
    upp(fys(450, 150), 0, gfx::MouseButton::Middle);

    // ---- O. OVERLAY-LAGRET
    nollställ();
    int        hud = 0, målning = 0;
    gfx::Rectf klipp{};
    bool       harKlipp = false;
    bool       skärm    = false;
    yta.onPaint         = [&målning](glaze::Widget*, glaze::PaintArgs&) {
        ++målning;
    };
    yta.onPaintOverlay = [&](glaze::Widget*, glaze::PaintArgs& e) {
        ++hud;
        skärm    = e.painter.inScreenSpace();
        harKlipp = window().renderer().clipBounds(klipp);
    };
    yta.invalidate();
    rita();
    const gfx::Rectf b = box();
    check(hud == 1 && skärm && målning == 1,
          "O onPaintOverlay ritas vid presenten, i de lokala px",
          std::to_string(hud));
    check(harKlipp && nära(klipp.x, b.x, 0.01) && nära(klipp.y, b.y, 0.01) &&
              nära(klipp.w, b.w, 0.01) && nära(klipp.h, b.h, 0.01),
          "O overlay-lagret klipps till ritytan",
          tal(klipp.x) + "," + tal(klipp.y) + " " + tal(klipp.w) + "×" +
              tal(klipp.h));
    // En ren present (ingen damage — fönstret har inget rundat chrome och
    // ritar partiellt): lagret ritas, ritytans innehåll inte.
    hud     = 0;
    målning = 0;
    yta.invalidateOverlay();
    rita();
    check(hud == 1 && målning == 0,
          "O invalidateOverlay ger en present av lagret utan att rita om "
          "ritytan (lagret är inte ritytans ritning)",
          std::to_string(hud) + " / " + std::to_string(målning));
    yta.onPaint        = nullptr;
    yta.onPaintOverlay = nullptr;

    // ---- T. TRÄFFPROVET
    nollställ();
    yta.setShapes({});
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 4.0, 0.0f, 0.0f});
    const float tol = st.canvasHitTolerance;   // logiska px
    // 0: en linje y = 0 med bredden 2 px (halva bredden 1). 1: en BRED linje
    // långt bort — den gör rutnätets grovsållning generösare än den tunna
    // linjens gräns, så det är det EXAKTA provet som avgör träff och miss
    // nedan (utan den hade sållningen ensam gett rätt svar för en enda
    // linje rakt uppifrån).
    yta.addShape(glaze::Shape::line({-40, 0}, {40, 0},
                                    glaze::Stroke{.width = 2.0f}));
    yta.addShape(glaze::Shape::line({-40, 40}, {40, 40},
                                    glaze::Stroke{.width = 40.0f}));
    rita();
    // Zoom 4: en världsenhet är 4 logiska px. Gränsen är 1 + tol logiska px.
    const auto lokal = [&](double wx, double wy) {
        const auto p = yta.toLocal(glaze::Point{wx, wy});
        return p ? *p : glaze::Point{NAN, NAN};
    };
    const double  gräns = 1.0 + tol;
    glaze::Point  m     = lokal(10.0, 0.0);
    check(yta.shapeAt(glaze::Point{m.x, m.y - (gräns - 0.3)}) == 0 &&
              yta.shapeAt(glaze::Point{m.x, m.y - (gräns + 0.3)}) == -1,
          "T linjen vid zoom 4: träff inom halva bredden + toleransen (SKÄRMens "
          "px), miss utanför",
          tal(gräns));
    // Under tilt: punkten på ett känt SKÄRMavstånd vinkelrätt från den
    // projicerade linjen (toLocal av ändarna och ren plangeometri).
    yta.setCamera(glaze::Camera{{0.0, 5.0}, 4.0, 25.0f, 50.0f});
    rita();
    const glaze::Point a = lokal(-10.0, 0.0), c2 = lokal(10.0, 0.0);
    const double dx = c2.x - a.x, dy = c2.y - a.y, l = std::hypot(dx, dy);
    const glaze::Point mitt{(a.x + c2.x) / 2.0, (a.y + c2.y) / 2.0};
    const glaze::Point nIn{mitt.x - dy / l * (gräns - 0.3),
                           mitt.y + dx / l * (gräns - 0.3)};
    const glaze::Point nUt{mitt.x - dy / l * (gräns + 0.3),
                           mitt.y + dx / l * (gräns + 0.3)};
    check(yta.shapeAt(nIn) == 0 && yta.shapeAt(nUt) == -1,
          "T under tilt 50° och bäring 25°: träff och miss på skärmens avstånd",
          pt(nIn) + " / " + pt(nUt));

    // Ytan med HÅL, överlapp och lagren.
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 30.0f});
    yta.setShapes({});
    yta.addShape(glaze::Shape::polygon(
        std::vector<std::vector<glaze::Point>>{
            {{-30, -30}, {30, -30}, {30, 30}, {-30, 30}},
            {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}},
        gfx::Color{0, 0, 255, 255}));                                    // 0
    yta.addShape(glaze::Shape::rect({15, 15, 30, 30},
                                    gfx::Color{255, 0, 0, 255}));        // 1
    const int övre = yta.addLayer("över");
    yta.addShape(övre, glaze::Shape::circle({25, 25}, 3,
                                            gfx::Color{0, 255, 0, 255})); // över 0
    rita();
    check(yta.shapeAt(lokal(0.0, 0.0)) == -1 && yta.shapeAt(lokal(-20.0, 0.0)) == 0,
          "T ytan: hålet träffas inte, ringen träffas");
    const std::vector<glaze::HitResult> h = yta.hitTest(lokal(25.0, 25.0));
    check(h.size() == 3 && h[0].layer == övre && h[0].index == 0 &&
              h[1].layer == 0 && h[1].index == 1 && h[2].layer == 0 &&
              h[2].index == 0,
          "T överst först: det övre lagret, sedan det senare indexet",
          std::to_string(h.size()) + " träffar");
    check(yta.shapeAt(lokal(25.0, 25.0)) == 1 &&
              yta.shapeAt(övre, lokal(25.0, 25.0)) == 0,
          "T shapeAt: den översta i lagret (utan lager: lager 0)");
    yta.removeShape(0, 0);
    rita();
    check(yta.shapeAt(lokal(25.0, 25.0)) == 0 && yta.shapeAt(lokal(-20.0, 0.0)) == -1,
          "T efter borttagningen flyttar indexen — rutnätet byggs om");
    yta.setLayerVisible(övre, false);
    check(yta.hitTest(lokal(25.0, 25.0)).size() == 1,
          "T ett dolt lager träffas inte");
    yta.removeLayer(övre);
    // setShapes fyller om lagret — träffprovet gäller de nya formerna direkt,
    // också före nästa ritning (rutnätet byggs ur formerna, inte ur bygget).
    yta.setShapes({glaze::Shape::rect({-60, -60, 10, 10}, gfx::Color{0, 0, 0, 255}),
                   glaze::Shape::rect({20, 20, 10, 10}, gfx::Color{0, 0, 0, 255})});
    check(yta.shapeAt(lokal(25.0, 25.0)) == 1 && yta.shapeAt(lokal(-55.0, -55.0)) == 0,
          "T setShapes: de nya formerna träffas före nästa ritning");

    // En markör (kolliderar inte) och en bild.
    yta.setShapes({});
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 1.0, 0.0f, 0.0f});
    yta.addShape(glaze::Shape::marker({0, 0}, glaze::MarkerStyle{.size = 10.0f}));
    glaze::Bitmap bild(4, 4);
    yta.addShape(glaze::Shape::image({50, 50, 40, 20}, bild));
    rita();
    const glaze::Point mp = lokal(0.0, 0.0);
    check(yta.shapeAt(glaze::Point{mp.x + 5.0 + tol - 0.3, mp.y}) == 0 &&
              yta.shapeAt(glaze::Point{mp.x + 5.0 + tol + 0.3, mp.y}) == -1,
          "T markören: radien + toleransen");
    check(yta.shapeAt(lokal(70.0, 60.0)) == 1 && yta.shapeAt(lokal(95.0 + tol + 1.0, 60.0)) == -1,
          "T bilden: inne träffas, utanför kanten + toleransen inte");

    // EN BILD PÅ FYRA FRIA HÖRN: träffad i
    // fyrhörningen, inte i LÅDANS hörn utanför den (punkten (−36, 26) ligger
    // ~18 px utanför trapetsens vänstra kant men inne i lådan) — och en FLUGA
    // (kanterna korsar) ritas inte och träffas aldrig, inte ens i korsningen.
    yta.setShapes({glaze::Shape::image({-40, -30}, {40, -30}, {15, 30}, {-15, 30},
                                       bild)});
    rita();
    check(yta.shapeAt(lokal(0.0, 0.0)) == 0 && yta.shapeAt(lokal(-36.0, 26.0)) == -1,
          "T en bild på fyra hörn: träffad i fyrhörningen, inte i lådans hörn "
          "utanför den");
    yta.setShapes({glaze::Shape::image({-40, -30}, {40, 30}, {40, -30}, {-40, 30},
                                       bild)});
    rita();
    check(yta.hitTest(lokal(0.0, 0.0)).empty() &&
              yta.hitTest(lokal(30.0, 0.0)).empty(),
          "T en fluga (kanterna korsar) träffas aldrig");

    // EN FÖRANKRAD MARKÖR (ankaret Bottom, 10 px): rutan står OVANFÖR punkten
    // på skärmen (y nedåt i de lokala px) — träffad strax ovanför sin överkant
    // inom toleransen, inte strax under punkten (en cirkel kring punkten hade
    // gjort tvärtom på båda). En BILDMARKÖR träffas i hela rutan: punkten
    // (8,5, 8,5) från nålens mitt ligger 12 px därifrån — utanför cirkeln
    // radien + toleransen (9) men inom rutan + toleransen (±9).
    yta.setShapes({glaze::Shape::marker(
        {0, 0}, glaze::MarkerStyle{.size = 10.0f, .anchor = glaze::TextAnchor::Bottom})});
    rita();
    check(yta.shapeAt(glaze::Point{mp.x, mp.y - (10.0 + tol - 0.5)}) == 0 &&
              yta.shapeAt(glaze::Point{mp.x, mp.y - (10.0 + tol + 0.5)}) == -1 &&
              yta.shapeAt(glaze::Point{mp.x, mp.y + tol + 0.5}) == -1,
          "T en förankrad markör (Bottom) träffas ovanför punkten, inte under den");
    glaze::Bitmap nålbild(2, 2);
    yta.setShape(0, glaze::Shape::marker(
                        {0, 0}, glaze::MarkerStyle{.size   = 10.0f,
                                                   .anchor = glaze::TextAnchor::Bottom,
                                                   .img    = nålbild}));
    rita();
    check(yta.shapeAt(glaze::Point{mp.x + 8.5, mp.y - 5.0 + 8.5}) == 0,
          "T en bildmarkör träffas i hela sin ruta (hörnet utanför en cirkel)");

    // Två krockande etiketter: den dolda träffas inte.
    yta.setShapes({});
    yta.addShape(glaze::Shape::text({0, 0}, "HHHHHHHH",
                                    glaze::TextStyle{.size = 20.0f}));          // 0
    yta.addShape(glaze::Shape::text({0, 0}, "HHHHHHHH",
                                    glaze::TextStyle{.size = 20.0f, .priority = 5})); // 1
    rita();
    const std::vector<glaze::HitResult> ht = yta.hitTest(lokal(0.0, 0.0));
    check(ht.size() == 1 && ht[0].index == 1,
          "T etiketterna: bara den synliga (högst prioritet) träffas",
          std::to_string(ht.size()) + " träffar");
    yta.setShapes({});
    yta.setYUp(false);
}

// LO. LAGRENS ORDNING (`Canvas::moveLayer`) genom fönstret: tre
// överlappande ytor i lager 1–3, och lager 3 flyttas till plats 1 — en flytt
// ÖVER ett lager, så att en rotation åt fel håll ger en annan ordning (två
// grannar hade bytt plats åt båda hållen). Träffprovet svarar med PLATSERNA
// efter flytten, och formen på varje plats läses ur `shape()`. Flytten ritar om
// ritytan EN gång (räknat som i del I); till samma plats och ett avslag ritar
// inte om (regel 1).
void Värd::lagerordning()
{
    nollställ();
    yta.setShapes({});
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f});
    const gfx::Color röd{220, 40, 40, 255}, grön{40, 180, 60, 255},
        blå{40, 60, 220, 255};
    yta.setLayers({"Grund", "Röd", "Grön", "Blå"});
    yta.addShape(1, glaze::Shape::rect({-30, -30, 40, 40}, röd));
    yta.addShape(2, glaze::Shape::rect({-20, -20, 40, 40}, grön));
    yta.addShape(3, glaze::Shape::rect({-10, -10, 40, 40}, blå));
    rita();
    const auto lika = [](gfx::Color a, gfx::Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    // Färgerna överst först under punkten — formen på varje träffs plats.
    const auto färger = [&](glaze::Point p) {
        std::vector<gfx::Color> ut;
        for (const glaze::HitResult& h : yta.hitTest(p))
            if (const glaze::Shape* s = yta.shape(h.layer, h.index))
                ut.push_back(s->fill().color());
        return ut;
    };
    const auto ordning = [&](const std::vector<gfx::Color>& f,
                             std::initializer_list<gfx::Color> facit) {
        if (f.size() != facit.size())
            return false;
        std::size_t i = 0;
        for (const gfx::Color& c : facit)
            if (!lika(f[i++], c))
                return false;
        return true;
    };
    const glaze::Point mitt = världTillLokal(0.0, 0.0);   // i alla tre
    const auto         h0   = yta.hitTest(mitt);
    check(h0.size() == 3 && h0[0].layer == 3 && ordning(färger(mitt), {blå, grön, röd}),
          "LO (fixturen) tre ytor över varandra — den blå i lager 3 överst",
          std::to_string(h0.size()) + " träffar");

    yta.ritningar      = 0;
    const bool flyttad = yta.moveLayer(3, 1);   // Grund, Blå, Röd, Grön
    rita();
    const int  ritade = yta.ritningar;
    const auto h1     = yta.hitTest(mitt);
    check(flyttad && h1.size() == 3 && h1[0].layer == 3 && h1[2].layer == 1 &&
              ordning(färger(mitt), {grön, röd, blå}) && yta.layerName(1) == "Blå",
          "LO moveLayer(3, 1): träffarna är platserna — överst lager 3 med den "
          "gröna formen, underst lager 1 med den blå");
    check(ritade == 1, "LO flytten ritar om ritytan en gång",
          std::to_string(ritade) + " ritningar");

    yta.ritningar     = 0;
    const bool kvar   = yta.moveLayer(1, 2) && yta.moveLayer(2, 2);
    const bool avslag = !yta.moveLayer(4, 0) && !yta.moveLayer(0, 5);
    rita();
    check(kvar && avslag && yta.ritningar == 0 && yta.layerName(1) == "Blå",
          "LO till samma plats och utanför: ingen omritning, ordningen står kvar",
          std::to_string(yta.ritningar) + " ritningar");
    yta.setLayers({""});
}

// RT. TRÄFFPROVETS NYA FORMER genom fönstret. Förväntat värde är formernas
// PROJICERADE punkter (`toLocal`) och ren plangeometri — aldrig träffprovets
// egna prov. Delarna står i filhuvudet.
void Värd::träffprovRT()
{
    nollställ();
    yta.setShapes({});
    const float      tol = style().canvasHitTolerance;   // logiska px
    const gfx::Color genom{0, 0, 0, 0}, svart{0, 0, 0, 255};
    const auto har = [](const std::vector<glaze::HitResult>& h, int lager,
                        int index, std::int64_t element = -1) {
        for (const glaze::HitResult& r : h)
            if (r.layer == lager && r.index == index && r.element == element)
                return true;
        return false;
    };
    const auto bara = [](const std::vector<glaze::HitResult>& h, int index,
                         std::int64_t element = -1) {
        return h.size() == 1 && h[0].layer == 0 && h[0].index == index &&
               h[0].element == element;
    };

    // ---- RT1 INSIDAN — zoom 2, kvadraten ±20.
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f});
    const std::vector<glaze::Point> ring{{-20, -20}, {20, -20}, {20, 20}, {-20, 20}};
    yta.addShape(glaze::Shape::polygon(ring, genom, glaze::Stroke{.width = 1.0f}));
    rita();
    const glaze::Point mitt = världTillLokal(0.0, 0.0);
    const bool         utan = yta.shapeAt(mitt) == -1;
    yta.setShape(0, glaze::Shape::polygon(ring, genom,
                                          glaze::Stroke{.width = 1.0f, .hitInside = true}));
    rita();
    check(utan && yta.shapeAt(mitt) == 0,
          "RT1 en yta med genomskinlig fyllning träffas i mitten bara med hitInside");
    std::vector<glaze::Point> sluten = ring;
    sluten.push_back(ring.front());
    yta.setShape(0, glaze::Shape::polyline(
                        sluten, glaze::Stroke{.paint = genom, .hitInside = true}));
    rita();
    const glaze::Point ute = världTillLokal(20.0 + (tol + 1.0) / 2.0, 0.0);
    check(yta.shapeAt(mitt) == 0 && yta.shapeAt(ute) == -1,
          "RT1 en OSYNLIG sluten linje med hitInside träffas inne, inte utanför "
          "kanten + toleransen");

    // ---- RT2 TOLERANSEN PER ANROP — zoom 4: en världsenhet är 4 logiska px,
    // och linjen (2 px) har halva bredden 1.
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 4.0, 0.0f, 0.0f});
    yta.setShapes({glaze::Shape::line({-40, 0}, {40, 0}, glaze::Stroke{.width = 2.0f})});
    rita();
    const glaze::Point p0 = världTillLokal(10.0, 0.0);
    const auto         på = [&](glaze::Point p, double d) {
        return glaze::Point{p.x, p.y - d};
    };
    const glaze::HitOptions px12{.tolerance = glaze::px(12.0f)};
    const glaze::HitOptions w3{.tolerance = glaze::world(3.0f)};
    check(har(yta.hitTest(på(p0, 12.5), px12), 0, 0) &&
              !har(yta.hitTest(på(p0, 13.5), px12), 0, 0),
          "RT2 tolerance px(12): träff på 1 + 11,5 px, miss på 1 + 12,5");
    check(har(yta.hitTest(på(p0, 12.5), w3), 0, 0) &&
              !har(yta.hitTest(på(p0, 13.5), w3), 0, 0),
          "RT2 tolerance world(3) = 12 px vid zoom 4, omräknad vid pekaren");
    check(har(yta.hitTest(på(p0, 1.0 + tol - 0.3), glaze::HitOptions{}), 0, 0) &&
              !har(yta.hitTest(på(p0, 1.0 + tol + 0.3), glaze::HitOptions{}), 0, 0),
          "RT2 utan tolerans gäller stilens");
    const int rymd = yta.addLayer("rymden");
    yta.addShape(rymd, glaze::Shape::line3({-40, 20, 0}, {40, 20, 0},
                                           glaze::Stroke{.width = 2.0f}));
    rita();
    const glaze::Point p3 = världTillLokal(10.0, 20.0);
    check(har(yta.hitTest(på(p3, 12.5), px12), rymd, 0, 0) &&
              !har(yta.hitTest(på(p3, 13.5), px12), rymd, 0, 0),
          "RT2 en linje i rymden följer anropets tolerans");
    yta.removeLayer(rymd);

    // ---- RT3 PUNKTEN — markörerna 20 px (radien 10) och 8 px isär: de två
    // första överlappar, och pekaren står närmare den ena.
    yta.setShapes({glaze::Shape::points({{0, 0}, {2, 0}, {40, 0}},
                                        glaze::MarkerStyle{.size = 20.0f})});
    rita();
    const auto element = [&](glaze::Point p) {
        const std::vector<glaze::HitResult> h = yta.hitTest(p);
        return h.size() == 1 ? h[0].element : std::int64_t{-2};
    };
    check(element(världTillLokal(0.5, 0.0)) == 0 &&
              element(världTillLokal(1.5, 0.0)) == 1 &&
              element(världTillLokal(40.0, 0.0)) == 2,
          "RT3 punktlagret som EN form: den träffade punkten närmast pekaren");
    yta.setShapes({glaze::Shape::points({{5, 5}, {5, 5}, {30, 0}},
                                        glaze::MarkerStyle{.size = 10.0f})});
    rita();
    std::optional<glaze::Point3> punkt;
    for (const glaze::HitResult& h :
         yta.hitTest(världTillLokal(30.0, 0.0), glaze::HitOptions{}))
        if (h.layer == 0 && h.index == 0 && h.element == 2)
            punkt = h.point;
    check(element(världTillLokal(5.0, 5.0)) == 1 && punkt && punkt->x == 30.0 &&
              punkt->y == 0.0,
          "RT3 två punkter på samma plats: den senare (ritad överst); `point` "
          "är den träffade punktens");
    yta.setShapes({glaze::Shape::points(
        {{-30, 0}, {0, 0}, {30, 0}},
        glaze::MarkerStyle{.size = 10.0f, .collide = true})});
    rita();
    bool etikett = false;
    for (const glaze::HitResult& h :
         yta.hitTest(världTillLokal(30.0, 0.0), glaze::HitOptions{}))
        etikett = etikett || (h.kind == glaze::HitKind::Label && h.index == 0 &&
                              h.element == 2);
    check(etikett, "RT3 en kolliderande markörform: etiketten med sin punkt");

    // ---- RT4 RUTAN — vriden 30° och tiltad 45°; formerna långt isär.
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 30.0f, 45.0f});
    yta.setShapes({glaze::Shape::rect({-10, -10, 20, 20}, svart),                 // 0
                   glaze::Shape::line({60, 0}, {80, 0}, glaze::Stroke{.width = 1.0f}),   // 1
                   glaze::Shape::points({{-60, 0}, {-60, 40}},
                                        glaze::MarkerStyle{.size = 4.0f})});      // 2
    rita();
    // De projicerade punkternas låda, med luften runt om (negativ krymper).
    const auto låda = [&](std::initializer_list<glaze::Point> världen, double luft) {
        double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        for (const glaze::Point& w : världen) {
            const glaze::Point p = världTillLokal(w.x, w.y);
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
        return glaze::Rect{x0 - luft, y0 - luft, x1 - x0 + 2.0 * luft,
                           y1 - y0 + 2.0 * luft};
    };
    const auto runt = [&](glaze::Point världen, double halv) {
        const glaze::Point p = världTillLokal(världen.x, världen.y);
        return glaze::Rect{p.x - halv, p.y - halv, 2.0 * halv, 2.0 * halv};
    };
    const glaze::HitMode fönster = glaze::HitMode::Window,
                         korsning = glaze::HitMode::Crossing;
    const glaze::Rect ytan = låda({{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, 1.0);
    const glaze::Rect baklänges{ytan.x + ytan.w, ytan.y + ytan.h, -ytan.w, -ytan.h};
    check(bara(yta.hitTest(ytan, fönster), 0) && bara(yta.hitTest(ytan, korsning), 0) &&
              bara(yta.hitTest(baklänges, fönster), 0),
          "RT4 ytans projicerade låda + 1 px: ytan i fönstret och korsningen "
          "(också dragen baklänges)");
    const glaze::Rect krympt = låda({{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, -1.0);
    check(yta.hitTest(krympt, fönster).empty() && bara(yta.hitTest(krympt, korsning), 0),
          "RT4 lådan − 1 px: ytan bara i korsningen");
    const glaze::Rect inne = runt({0, 0}, 2.0);
    check(yta.hitTest(inne, fönster).empty() && bara(yta.hitTest(inne, korsning), 0),
          "RT4 en ruta inne i ytan: bara i korsningen (insidan)");
    const glaze::Rect linjen = låda({{60, 0}, {80, 0}}, 1.0);
    const glaze::Rect mitten = runt({70, 0}, 2.0);
    check(bara(yta.hitTest(linjen, fönster), 1) &&
              yta.hitTest(mitten, fönster).empty() &&
              bara(yta.hitTest(mitten, korsning), 1),
          "RT4 linjen: i fönstret med båda ändarna, en ruta kring mitten bara i "
          "korsningen");
    const glaze::Point m0 = världTillLokal(-60.0, 0.0);
    const glaze::Rect  kant{m0.x + 1.5, m0.y - 0.5, 2.0, 1.0};   // radien 2
    check(bara(yta.hitTest(runt({-60, 0}, 3.5), fönster), 2, 0) &&
              yta.hitTest(kant, fönster).empty() &&
              bara(yta.hitTest(kant, korsning), 2, 0),
          "RT4 markörerna punkt för punkt: punkten 0 i rutan, kanten av dess "
          "skiva bara i korsningen");
    std::vector<glaze::Point> ram = ring;
    ram.push_back(ring.front());
    yta.setShapes({glaze::Shape::polyline(
        ram, glaze::Stroke{.paint = genom, .hitInside = true})});
    rita();
    const bool osynlig = bara(yta.hitTest(inne, korsning), 0) &&
                         yta.hitTest(inne, fönster).empty();
    yta.setShape(0, glaze::Shape::polyline(ram, glaze::Stroke{.width = 1.0f}));
    rita();
    check(osynlig && yta.hitTest(inne, korsning).empty(),
          "RT4 en ruta inne i en osynlig ram med hitInside korsar den, inne i "
          "en ram utan inte");
    yta.setShapes({});
}

// M. TEXTENS MÅTT (`Canvas::measureText`) genom den RIKTIGA
// backenden — det enda stället kontraktet mellan mätning och rasterisering
// (`Font::measure` = `rasterGlyph`s steg) prövas på en riktig font. Förväntat värde är
// mätningen EFTER att texten ritats (stegen ur de byggda glyferna) för M2,
// målarens mätning för M3 och enheternas omräkning ur skärmens täthet och
// zoomen för M4 — aldrig mätningens egen formel.
void Värd::mätning()
{
    nollställ();
    yta.setShapes({});

    // ---- M1. Utan öppet fönster finns ingen font: nullopt; i fönstret ett
    // värde.
    const glaze::Canvas fri;
    const auto          utan = fri.measureText("abc");
    const auto          med  = yta.measureText("abc");
    check(!utan && med && med->width > 0.0 && med->lines == 1,
          "M1 utan fönster nullopt, i ett öppet fönster ett mått",
          med ? tal(med->width) : "tomt");

    // ---- M2 och M6. En ALDRIG ritad text i fyra stilar, med en kodpunkt
    // primärfonten saknar — samma val som scene_pixel_gate del R: U+4E2D
    // saknas i Segoe UI och SF, U+2248 i Noto Sans. Reserven prövas först:
    // en riktig glyf, inte ersättningstecknet.
#if defined(_WIN32) || defined(__APPLE__)
    constexpr char32_t kReserv = 0x4E2D;
#else
    constexpr char32_t kReserv = 0x2248;
#endif
    const auto font = window().renderer().loadFont(gfx::FontFamily::UI, 32.0f,
                                                   gfx::FontStyle::Regular);
    gfx::GlyphRaster reserv, ersättning;
    const bool riktig = font && font->valid() && font->hasGlyph(kReserv) &&
                        font->rasterGlyph(kReserv, reserv) &&
                        font->rasterGlyph(0xFFFD, ersättning) &&
                        !(reserv.width == ersättning.width &&
                          reserv.height == ersättning.height &&
                          reserv.coverage == ersättning.coverage);
    check(riktig,
          "M2 (fixturen) reservens kodpunkt är en riktig glyf, inte "
          "ersättningstecknet");
    const glaze::TrText text = "å≈中";
    struct Stil {
        bool        fet, kursiv;
        const char* namn;
    };
    const Stil stilar[] = {{false, false, "Regular"},
                           {true, false, "Bold"},
                           {false, true, "Italic"},
                           {true, true, "BoldItalic"}};
    const auto stilAv = [](const Stil& s) {
        return glaze::TextStyle{.size = 24.0f, .bold = s.fet, .italic = s.kursiv};
    };
    std::optional<glaze::TextMetrics> före[4];
    window().renderer().resetStats();
    for (int i = 0; i < 4; ++i)
        före[i] = yta.measureText(text, stilAv(stilar[i]));
    const std::uint64_t mättByte = window().renderer().stats().bytesUploaded;
    bool alla = true;
    for (const auto& m : före)
        alla = alla && m && m->width > 0.0;
    check(alla && mättByte == 0,
          "M6 mätningen av en aldrig ritad text i fyra stilar utanför en frame "
          "laddar inte upp något",
          std::to_string(mättByte) + " byte");

    std::vector<glaze::TextSpan> delar;
    for (const Stil& s : stilar)
        delar.push_back(glaze::TextSpan{.text = text, .bold = s.fet, .italic = s.kursiv});
    yta.addShape(glaze::Shape::text({0, 0}, delar,
                                    glaze::TextStyle{.size = 24.0f, .collide = false}));
    rita();   // nu är glyferna byggda — stegen ur rastret
    std::string skillnad;
    for (int i = 0; i < 4; ++i) {
        const auto efter = yta.measureText(text, stilAv(stilar[i]));
        const bool lika  = efter && före[i] && nära(efter->width, före[i]->width, 1e-4) &&
                          nära(efter->height, före[i]->height, 1e-4) &&
                          nära(efter->baseline, före[i]->baseline, 1e-4) &&
                          efter->lines == före[i]->lines;
        if (!lika)
            skillnad += std::string(" ") + stilar[i].namn + ": " +
                        (före[i] ? tal(före[i]->width) : "tomt") + " → " +
                        (efter ? tal(efter->width) : "tomt");
    }
    check(skillnad.empty(),
          "M2 mätningen FÖRE ritningen = EFTER den (Font::measure = rastrets "
          "steg), i fyra stilar och med en reservglyf",
          skillnad);
    yta.setShapes({});

    // ---- M3. = målarens mätning i onPaint, för samma stil.
    gfx::Sizef             målad{};
    const glaze::TextStyle hud{.size = glaze::mm(3.0f), .bold = true};
    yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        målad = e.painter.measureText("Lunden 1:4", hud);
    };
    yta.invalidate();
    rita();
    yta.onPaint   = nullptr;
    const auto m3 = yta.measureText("Lunden 1:4", hud);
    check(m3 && målad.w > 0.0f && nära(m3->width, målad.w, 1e-3) &&
              nära(m3->height, målad.h, 1e-3),
          "M3 Canvas::measureText = Painter::measureText i onPaint",
          (m3 ? tal(m3->width) : "tomt") + " mot " + tal(målad.w));

    // ---- M4. ENHETERNA. Text i mm mätt i mm är mm; i px är den mm × skärmens
    // täthet / skalan. Text i px mätt i världsenheter är px / zoom (kartkameran
    // utan tilt). Text i världsenheter mätt i världsenheter står still med
    // zoomen.
    const glaze::TextStyle mmStil{.size = glaze::mm(3.0f)};
    const auto             iMm   = yta.measureText("Skala", mmStil, glaze::Unit::Mm);
    const auto             iPx   = yta.measureText("Skala", mmStil, glaze::Unit::Px);
    const double           perMm = yta.display().pxPerMm / window().uiScale();
    check(iMm && iPx && iPx->width > 0.0 &&
              nära(iMm->width * perMm, iPx->width, 1e-4 * iPx->width),
          "M4 text i mm: mätt i mm × täthet / skala = mätt i px",
          (iMm ? tal(iMm->width) : "tomt") + " mm, " +
              (iPx ? tal(iPx->width) : "tomt") + " px");
    yta.setCamera(glaze::Camera{{0.0, 0.0}, 4.0, 0.0f, 0.0f});
    rita();
    const glaze::TextStyle pxStil{.size = 18.0f};
    const auto             wPx = yta.measureText("Skala", pxStil);
    const auto             wW  = yta.measureText("Skala", pxStil, glaze::Unit::World);
    check(wPx && wW && nära(wW->width, wPx->width / 4.0, 1e-5 * wPx->width),
          "M4 text i px mätt i världsenheter = px / zoom (zoom 4, utan tilt)",
          (wW ? tal(wW->width) : "tomt") + " mot " +
              (wPx ? tal(wPx->width / 4.0) : "tomt"));
    const glaze::TextStyle världStil{.size = glaze::world(5.0f)};
    yta.setZoom(1.0);
    rita();
    const auto z1 = yta.measureText("Skala", världStil, glaze::Unit::World);
    yta.setZoom(4.0);
    rita();
    const auto z4 = yta.measureText("Skala", världStil, glaze::Unit::World);
    check(z1 && z4 && z1->width > 0.0 && z1->width == z4->width,
          "M4 text i världsenheter mätt i världsenheter är densamma vid zoom 1 "
          "och 4",
          (z1 ? tal(z1->width) : "tomt") + " / " + (z4 ? tal(z4->width) : "tomt"));

    // ---- M5. INNEHÅLLET: en fet del är bredare, '\n' och brytbredden ger
    // rader, en tom text är en rad, och baslinjen ligger inom raden.
    const glaze::TextStyle st20{.size = 20.0f};
    const auto             vanlig = yta.measureText("Ekparken", st20);
    const auto             fet    = yta.measureText(
        std::vector<glaze::TextSpan>{{.text = "Ekparken", .bold = true}}, st20);
    check(vanlig && fet && fet->width > vanlig->width,
          "M5 en fet del är bredare än samma text vanlig",
          (fet ? tal(fet->width) : "tomt") + " mot " +
              (vanlig ? tal(vanlig->width) : "tomt"));
    const auto två = yta.measureText("a\nb", st20);
    check(två && två->lines == 2 && nära(två->height, 2.0 * två->lineHeight, 1e-9),
          "M5 \"a\\nb\" är två rader");
    const auto bruten = yta.measureText(
        "aaa bbbb cc dddd", glaze::TextStyle{.size = 20.0f, .wrapWidth = 60.0f});
    check(bruten && bruten->lines >= 2 && bruten->width <= 60.0 + 1e-6,
          "M5 brytbredden 60 px ger flera rader, ingen bredare än den",
          bruten ? std::to_string(bruten->lines) + " rader, " + tal(bruten->width)
                 : "tomt");
    const auto tom = yta.measureText("", st20);
    check(tom && tom->lines == 1 && tom->width == 0.0 && tom->height > 0.0,
          "M5 en tom text är en rad med bredden 0");
    check(vanlig && vanlig->baseline > 0.0 && vanlig->baseline <= vanlig->lineHeight,
          "M5 baslinjen ligger inom raden (0 < baseline ≤ lineHeight)",
          vanlig ? tal(vanlig->baseline) + " / " + tal(vanlig->lineHeight) : "tomt");

    // ---- M7. Storleken 0 mäter ingenting.
    const auto noll = yta.measureText("abc", glaze::TextStyle{.size = 0.0f});
    check(noll && noll->lines == 0 && noll->width == 0.0 && noll->height == 0.0,
          "M7 storleken 0 ger nollor och inga rader");

    // ---- M8. TAKET: maxLines 2 på fem rader ger två rader
    // och `truncated`; brytbredden 60 px med maxHeight 1,5 rader ger EN rad,
    // kortad och högst 60 px bred.
    const auto tak = yta.measureText(
        "a\nb\nc\nd\ne", glaze::TextStyle{.size = 20.0f, .maxLines = 2});
    const auto höjd = yta.measureText(
        "aaa bbbb cc dddd eee ffff",
        glaze::TextStyle{.size      = 20.0f,
                         .wrapWidth = 60.0f,
                         .maxHeight = glaze::px(static_cast<float>(
                             1.5 * (två ? två->lineHeight : 0.0)))});
    check(två && !två->truncated && tak && tak->lines == 2 && tak->truncated &&
              höjd && höjd->lines == 1 && höjd->truncated &&
              höjd->width <= 60.0 + 1e-6,
          "M8 maxLines och maxHeight ger raderna och truncated, bredden inom "
          "brytbredden",
          (tak ? std::to_string(tak->lines) : "tomt") + " / " +
              (höjd ? std::to_string(höjd->lines) + ", " + tal(höjd->width) : "tomt"));

    // ---- M9. GOLVET OCH TAKET: en världstext på 10 enheter med
    // golvet 20 px och taket 40 px, mätt i px — golvets andel av den råa vid
    // zoom 1 (10 → 20 px) och takets vid zoom 6 (60 → 40 px).
    const glaze::TextStyle rå{.size = glaze::world(10.0f)};
    const glaze::TextStyle gränsad{.size    = glaze::world(10.0f),
                                   .minSize = glaze::px(20.0f),
                                   .maxSize = glaze::px(40.0f)};
    yta.setZoom(1.0);
    rita();
    const auto r1 = yta.measureText("Kyrkbacken", rå);
    const auto g1 = yta.measureText("Kyrkbacken", gränsad);
    yta.setZoom(6.0);
    rita();
    const auto r6 = yta.measureText("Kyrkbacken", rå);
    const auto g6 = yta.measureText("Kyrkbacken", gränsad);
    check(r1 && g1 && r6 && g6 && nära(g1->width, 2.0 * r1->width, 1e-4 * g1->width) &&
              nära(g6->width, r6->width * 40.0 / 60.0, 1e-4 * g6->width),
          "M9 golvet vid zoom 1 och taket vid zoom 6 — andelen av den råa bredden",
          (g1 && r1 ? tal(g1->width / r1->width) : "tomt") + " / " +
              (g6 && r6 ? tal(g6->width / r6->width) : "tomt"));

    // ---- ZV. SYNLIGHETEN EFTER ZOOM i träffprovet: en text och en
    // markör UTAN kollision med intervallet [2, ∞) träffas inte vid zoom 1
    // men vid zoom 3.
    yta.setShapes({glaze::Shape::text({0, 0}, "Skolan",
                                      glaze::TextStyle{.size = 20.0f, .minZoom = 2.0}),
                   glaze::Shape::marker({0, 40}, glaze::MarkerStyle{.size    = 12.0f,
                                                                    .minZoom = 2.0})});
    const auto punkt = [&](double wx, double wy) {
        const auto p = yta.toLocal(glaze::Point{wx, wy});
        return p ? *p : glaze::Point{NAN, NAN};
    };
    yta.setCenter(0.0, 20.0);
    yta.setZoom(1.0);
    rita();
    const bool ute =
        yta.hitTest(punkt(0.0, 0.0)).empty() && yta.hitTest(punkt(0.0, 40.0)).empty();
    yta.setZoom(3.0);
    rita();
    const bool inne = !yta.hitTest(punkt(0.0, 0.0)).empty() &&
                      !yta.hitTest(punkt(0.0, 40.0)).empty();
    check(ute && inne,
          "ZV text och markör utanför sitt zoomintervall träffas inte, innanför "
          "träffas de",
          std::string(ute ? "" : "träff utanför ") + (inne ? "" : "miss innanför"));
    yta.setShapes({});
}

// IN. INMATNINGEN: högerknappen och dubbelklicken i
// mushandlarna, snabbmenyn mot ett taget tryck, avbrottet `onMouseCancel` och
// tangentsläppet `onKeyUp` — och att `onKeyDown`, som nu är basens, lämnar
// ritytans pilar i fred när den inte tar tangenten.
void Värd::inmatning()
{
    using B = gfx::MouseButton;
    using K = gfx::Key;
    const auto namn = [](B b) -> std::string {
        return b == B::Left ? "V" : b == B::Middle ? "M" : b == B::Right ? "H"
                                                                        : "-";
    };
    const auto släpp = [this](K k) {
        gfx::Event e;
        e.type     = gfx::EventType::KeyUp;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    };
    int menyer = 0;
    root().onContextMenu = [&menyer] { ++menyer; };

    // ---- IN1. Ett högertryck handlern TAR: draget och släppet kommer hit,
    //           och ingen snabbmeny.
    nollställ();
    std::vector<std::string> mus;
    yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        mus.push_back("ner:" + namn(e.button));
        e.handled = e.button == B::Right;
    };
    yta.onMouseMove = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        mus.push_back("drag:" + namn(e.button));
    };
    yta.onMouseUp = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        mus.push_back("upp:" + namn(e.button));
    };
    ner(fys(100, 100), 0, B::Right);
    flytta(fys(150, 100));
    upp(fys(150, 100), 0, B::Right);
    check(menyer == 0 &&
              mus == std::vector<std::string>{"ner:H", "drag:H", "upp:H"},
          "IN1 ett högertryck handlern tog: onMouseMove och onMouseUp får "
          "Right, EN rörelse per rörelse, och ingen snabbmeny",
          std::to_string(menyer) + " menyer, " + std::to_string(mus.size()) +
              " händelser");

    // ---- IN2. Utan handled: snabbmenyn, och inget onMouseUp(Right).
    mus.clear();
    yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        mus.push_back("ner:" + namn(e.button));
    };
    ner(fys(100, 100), 0, B::Right);
    upp(fys(100, 100), 0, B::Right);
    check(menyer == 1 && mus == std::vector<std::string>{"ner:H"},
          "IN2 ett högertryck ingen tog: snabbmenyn vid trycket och inget "
          "onMouseUp(Right)",
          std::to_string(menyer) + " menyer, " + std::to_string(mus.size()) +
              " händelser");

    // ---- IN3. onDoubleClick med Middle och med Right (en paus först: en
    //           tidigare mittentryckföljd får inte bli det första klicket).
    std::this_thread::sleep_for(
        std::chrono::milliseconds(gfx::doubleClickMs() + 150));
    std::vector<std::string> dubbel;
    yta.onMouseDown = [](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        e.handled = e.button != B::Left;
    };
    yta.onMouseMove   = nullptr;
    yta.onMouseUp     = nullptr;
    yta.onDoubleClick = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        dubbel.push_back(namn(e.button));
    };
    for (B b : {B::Middle, B::Middle, B::Right, B::Right}) {
        ner(fys(120, 120), 0, b);
        upp(fys(120, 120), 0, b);
    }
    check(dubbel == std::vector<std::string>{"M", "H"},
          "IN3 onDoubleClick med Middle och med Right — ett per par",
          std::to_string(dubbel.size()));
    yta.onDoubleClick = nullptr;

    // ---- IN4. Fokusförlust mitt i en vänsterpanorering: onMouseCancel(Left),
    //           och ingen tröghet — inte heller av ett senare släpp.
    nollställ();
    yta.setPannable(true);
    yta.setInertia(true);
    int avbrott = 0;
    B   knapp   = B::None;
    yta.onMouseCancel = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
        ++avbrott;
        knapp = e.button;
    };
    ner(fys(100, 100));
    flytta(fys(120, 100));
    flytta(fys(140, 100));
    flytta(fys(160, 100));
    const glaze::Camera i0 = yta.camera();
    enkel(gfx::EventType::WindowFocusLost);
    upp(fys(260, 100));
    rita();
    rita();
    rita();
    check(avbrott == 1 && knapp == B::Left,
          "IN4 fokusförlusten mitt i panoreringen ger onMouseCancel(Left) EN "
          "gång",
          std::to_string(avbrott) + " " + namn(knapp));
    check(yta.camera().center.x == i0.center.x &&
              yta.camera().center.y == i0.center.y && !yta.isAnimating(),
          "IN4 kameran står still efter tre ritningar — ingen tröghet");
    enkel(gfx::EventType::WindowFocusGained);
    yta.setInertia(false);
    yta.onMouseCancel = nullptr;

    // ---- IN5. onKeyUp vid släppet och vid fokusförlusten.
    nollställ();
    std::vector<std::string> släppen;
    yta.onKeyUp = [&](glaze::Widget*, glaze::KeyArgs& e) {
        släppen.push_back(e.cancelled ? "avbrutet" : "upp");
    };
    yta.focus();
    tangent(K::Space);
    släpp(K::Space);
    tangent(K::Space);
    enkel(gfx::EventType::WindowFocusLost);
    check(släppen == std::vector<std::string>{"upp", "avbrutet"},
          "IN5 onKeyUp vid släppet, och ett med cancelled vid fokusförlusten",
          std::to_string(släppen.size()));
    enkel(gfx::EventType::WindowFocusGained);
    yta.onKeyUp = nullptr;

    // ---- IN6. onKeyDown (basens) utan handled: pilen panorerar.
    nollställ();
    yta.setPannable(true);
    int sett = 0;
    yta.onKeyDown = [&sett](glaze::Widget*, glaze::KeyArgs&) { ++sett; };
    const glaze::Camera k0 = yta.camera();
    yta.focus();
    tangent(K::Right);
    släpp(K::Right);
    check(sett == 1 && yta.camera().center.x != k0.center.x,
          "IN6 onKeyDown utan handled ser pilen — och ritytan panorerar ändå",
          std::to_string(sett));

    root().onContextMenu = nullptr;
    nollställ();
}

// I. TRÖGHETEN genom pumpen: ett kast, och sedan vila. Timern ger
// fortsättningen; under tiden ritar pumpen så länge ritytan ber om det.
void Värd::tröghet()
{
    nollställ();
    yta.setPannable(true);
    yta.setInertia(true);
    ändringar          = 0;
    yta.onCameraChange = [this] { ++ändringar; };
    // 20 logiska px per 20 ms åt höger — ett tydligt kast.
    ner(fys(80, 150));
    for (int i = 1; i <= 6; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        flytta(fys(80 + 20.0 * i, 150));
    }
    upp(fys(200, 150));
    const glaze::Point vidSläpp = yta.center();
    const std::uint64_t släpp   = gfx::ticksMs();
    const bool          glider  = yta.isAnimating();
    yta.ritningar               = 0;
    check(glider, "I en kastad panorering glider (trögheten startar)");
    vidTimer = [this, vidSläpp, släpp] {
        const glaze::Point slut = yta.center();
        // Kastet gick åt höger: världen följer handen, kameran går åt vänster.
        check(slut.x < vidSläpp.x - 1e-6 && nära(slut.y, vidSläpp.y, 1e-6),
              "I trögheten fortsätter i kastets riktning",
              pt(vidSläpp) + " → " + pt(slut));
        check(!yta.isAnimating(), "I …och tar slut");
        check(ändringar > 0,
              "I tröghetens steg fyrar onCameraChange (från pump-svansen)",
              std::to_string(ändringar));
        // Trögheten tar högst 1,2 s (camera_math.cpp); marginalen täcker en
        // sista frame och den postade onCameraChange-omritningen.
        const bool ritade = yta.ritningar >= 2;
        const bool vilar  = yta.senast < släpp + 1700;
        check(ritade, "I trögheten ritar sina steg (ritytan bad om frames)",
              std::to_string(yta.ritningar));
        check(vilar,
              "I efter slutet ber ritytan inte om fler frames (regel 1): ingen "
              "ritning i vilan",
              std::to_string(yta.senast - släpp) + " ms efter släppet");
        yta.onCameraChange = nullptr;
        toning();
    };
    window().startTimer(kTimer, 2600);
}

// L. ETIKETTERNAS TONING genom pumpen: en etikett som kollisionen visar
// tonar in över frames — och sedan ber ritytan inte om fler (regel 1).
void Värd::toning()
{
    nollställ();
    yta.setShapes({});
    // Sida vid sida krockar de (texten är bredare än 60 px); vid bäring 90°
    // står ankarna över varandra och den dolda visas.
    yta.addShape(glaze::Shape::text({-15, 0}, "HHHHHH",
                                    glaze::TextStyle{.size = 22.0f}));
    yta.addShape(glaze::Shape::text({15, 0}, "HHHHHH",
                                    glaze::TextStyle{.size = 22.0f}));
    yta.setLabelFade(true);
    rita();
    check(yta.shapeAt(världTillLokal(15, 0)) == -1,
          "L (fixturen) den andra etiketten är dold av kollisionen");
    yta.setBearing(90.0f);
    const std::uint64_t start = gfx::ticksMs();
    yta.ritningar             = 0;
    vidTimer = [this, start] {
        check(!yta.isAnimating() && yta.shapeAt(världTillLokal(15, 0)) == 1,
              "L toningen har landat: etiketten syns och inget animerar");
        check(yta.ritningar >= 2, "L toningen ritar sina steg",
              std::to_string(yta.ritningar));
        check(yta.senast < start + 700,
              "L efter toningen ber ritytan inte om fler frames (regel 1)",
              std::to_string(yta.senast - start) + " ms");
        yta.setLabelFade(false);
        yta.setShapes({});
        klar();
    };
    window().startTimer(kTimer, 1200);
}

// ============================================== MR. MÄTNINGEN I LAYOUTEN
//
// `Canvas::measureText(c, …)`: en rityta som är ett DOKUMENT —
// höjden följer bredden, mätt i `heightForWidth` med layoutens Ctx. Formen
// utan Ctx frågar layouten, och anropad därifrån gick den runt i sig själv
// (stackspill); vakten bryter varvet. Förväntat värde är DJUPET kroken anropas i, och
// höjden mätt med formen utan Ctx UTANFÖR layouten — aldrig mätningens egen
// väg. Allt före fönstrets första ritning (ett dolt fönster ritas av pumpen
// först efter de postade funktionerna), så kartskalan väntar och ingen yta
// är känd.

// Grindens tak: djupet där ett sabotage (ingen vakt, `knownBox` i formen med
// Ctx) hade spräckt stacken. Där svarar kroken med en höjd utan att mäta.
constexpr int kMaxDjup = 8;

struct Dokument : Yta {
    bool             utanCtx = false;   // MR3: formen utan Ctx i kroken
    glaze::TrText    text    = "Ett dokument på en rityta, brutet efter den "
                               "bredd layouten ger det — tillräckligt många "
                               "ord för att bli flera rader i varje bredd.";
    glaze::TextStyle stil{.size = 14.0f};
    mutable int      djup = 0, maxDjup = 0, nullsvar = 0;

    void nollaRäknarna()
    {
        djup = maxDjup = nullsvar = 0;
    }
    // Stilen med brytbredden — kroken och det förväntade värdet bryter likadant.
    glaze::TextStyle bruten(float bredd) const
    {
        glaze::TextStyle s = stil;
        s.wrapWidth        = glaze::px(bredd);
        return s;
    }

protected:
    std::optional<float> heightForWidth(const glaze::Ctx& c,
                                        float             bredd) const override
    {
        if (height > 0)
            return std::nullopt;
        ++djup;
        maxDjup = std::max(maxDjup, djup);
        struct Ut {
            int& d;
            ~Ut() { --d; }
        } ut{djup};
        if (djup > kMaxDjup) {
            maxDjup = 99;
            return 10.0f;
        }
        const auto m = utanCtx ? measureText(text, bruten(bredd))
                               : measureText(c, text, bruten(bredd));
        if (!m) {
            ++nullsvar;
            return std::nullopt;
        }
        return static_cast<float>(m->height);
    }
};

struct Läsare : glaze::AppWindow {
    glaze::Panel panel;
    Dokument     iPanel;     // MR1, MR3a — sträckt i en Panel
    Dokument     underRot;   // MR2, MR3b — sträckt direkt under roten
    Yta          karta;      // MR4a–c — fasta mått
    Yta          skala;      // MR4d — kartskalan satt före öppningen
    Yta          rymd;       // MR5 — en 3D-kamera, AUTO-mått, aldrig ritad

    Läsare() : glaze::AppWindow("canvas_gate MR", 640, 480, gfx::Chrome::None)
    {
        panel.name   = "panel";
        panel.width  = 300;
        panel.height = 300;
        root().adopt(panel);
        // Sträckta vänster–höger men inte lodrätt — `resolve` frågar
        // `heightForWidth` med bredden det gav.
        iPanel.name    = "iPanel";
        iPanel.anchors = glaze::Anchors{true, true, true, false};
        iPanel.x       = 10;
        iPanel.right   = 10;
        iPanel.y       = 10;
        panel.adopt(iPanel);
        underRot.name    = "underRot";
        underRot.anchors = glaze::Anchors{true, true, true, false};
        underRot.x       = 320;
        underRot.right   = 10;
        underRot.y       = 10;
        root().adopt(underRot);
        for (Yta* r : {&karta, &skala, &rymd})
            r->y = 320;
        karta.name   = "karta";
        karta.x      = 10;
        karta.width  = 150;
        karta.height = 120;
        root().adopt(karta);
        skala.name   = "skala";
        skala.x      = 170;
        skala.width  = 150;
        skala.height = 120;
        skala.setMapScale(500.0);   // före öppningen: den väntar på skärmen
        root().adopt(skala);
        rymd.name = "rymd";
        rymd.x    = 330;
        rymd.setCamera(glaze::Camera3D{});
        root().adopt(rymd);
    }

    void fall();
};

// Två mått lika (samma fält, samma rader) inom e.
bool lika(const std::optional<glaze::TextMetrics>& a,
          const std::optional<glaze::TextMetrics>& b, double e)
{
    return a && b && a->lines == b->lines && a->truncated == b->truncated &&
           nära(a->width, b->width, e) && nära(a->height, b->height, e) &&
           nära(a->lineHeight, b->lineHeight, e) &&
           nära(a->baseline, b->baseline, e);
}

std::string mått(const std::optional<glaze::TextMetrics>& m)
{
    return m ? tal(m->width) + " × " + tal(m->height) + " (" +
                   std::to_string(m->lines) + " rader)"
             : "tomt";
}

void Läsare::fall()
{
    // Premissen: ingen av ritytorna har ritats — kartskalan väntar, ingen yta
    // är känd och `knownBox` hade gått till layouten (MR1/MR2:s sabotage).
    check(iPanel.ritningar == 0 && underRot.ritningar == 0 &&
              karta.ritningar == 0 && skala.ritningar == 0 &&
              rymd.ritningar == 0,
          "MR (fixturen) inget är ritat när fallen börjar");

    // ---- MR1 och MR2. Formen MED Ctx i kroken: djupet 1 (aldrig ett varv
    // genom layouten), inga nollsvar, och höjden layouten gav = formen utan
    // Ctx mätt UTANFÖR layouten med samma bredd.
    for (Dokument* d : {&iPanel, &underRot}) {
        const std::string fall = d == &iPanel ? "MR1 i en Panel"
                                              : "MR2 direkt under roten";
        d->nollaRäknarna();
        const gfx::Sizef s = d->actualSize();
        const int        djupet = d->maxDjup, noll = d->nullsvar;
        const auto       facit  = d->measureText(d->text, d->bruten(s.w));
        check(djupet == 1 && noll == 0,
              fall + ": kroken mäter med formen med Ctx utan att gå runt "
                     "(djupet 1, inga nollsvar)",
              "djupet " + std::to_string(djupet) + ", " + std::to_string(noll) +
                  " nollsvar");
        check(facit && facit->lines >= 2 && s.w > 0.0f &&
                  nära(s.h, facit->height, 1e-3),
              fall + ": höjden = formen utan Ctx mätt utanför layouten med "
                     "samma bredd (flera rader)",
              tal(s.h) + " mot " + mått(facit));
    }

    // ---- MR3. Formen UTAN Ctx i kroken: vakten svarar
    // nullopt i det inre anropet — djupet 2, aldrig taket — och den yttre
    // mätningen blir klar.
    for (Dokument* d : {&iPanel, &underRot}) {
        const std::string fall = d == &iPanel ? "MR3a i en Panel"
                                              : "MR3b direkt under roten";
        d->utanCtx = true;
        d->nollaRäknarna();
        const gfx::Sizef s = d->actualSize();
        check(d->maxDjup == 2 && d->nullsvar >= 1 && s.h > 0.0f,
              fall + ": formen utan Ctx ur kroken svarar nullopt inifrån — "
                     "djupet 2, inte ett varv till taket",
              "djupet " + std::to_string(d->maxDjup) + ", " +
                  std::to_string(d->nullsvar) + " nollsvar");
        d->utanCtx = false;
    }

    // ---- MR4. De två formerna är LIKA med `behaviorCtx()` som Ctx.
    const auto båda = [](const Yta& y, const glaze::TrText& t,
                         const glaze::TextStyle& st, glaze::Unit u) {
        return std::pair{y.measureText(y.behaviorCtx(), t, st, u),
                         y.measureText(t, st, u)};
    };
    {
        const auto [med, utan] =
            båda(karta, "Dokument", glaze::TextStyle{.size = 16.0f},
                 glaze::Unit::Px);
        check(lika(med, utan, 1e-9) && med->width > 0.0,
              "MR4a text i px: formen med Ctx = formen utan",
              mått(med) + " / " + mått(utan));
    }
    {
        const glaze::TextStyle st{.size = glaze::mm(4.0f)};
        const auto [mm, mmUtan] = båda(karta, "Skala", st, glaze::Unit::Mm);
        const auto [px, pxUtan] = båda(karta, "Skala", st, glaze::Unit::Px);
        check(lika(mm, mmUtan, 1e-9) && lika(px, pxUtan, 1e-9) &&
                  px->width > 0.0,
              "MR4b text i mm, mätt i mm och i px: formerna lika",
              mått(mm) + " / " + mått(px));
    }
    {
        karta.setCamera(glaze::Camera{{0.0, 0.0}, 4.0, 0.0f, 0.0f});
        const glaze::TextStyle st{.size = glaze::world(5.0f)};
        const auto [px, pxUtan] = båda(karta, "Kyrkan", st, glaze::Unit::Px);
        const auto [w, wUtan]   = båda(karta, "Kyrkan", st, glaze::Unit::World);
        check(lika(px, pxUtan, 1e-6 * (px ? px->width : 1.0)) &&
                  lika(w, wUtan, 1e-9) && nära(px->width, 4.0 * w->width,
                                                1e-5 * px->width),
              "MR4c världstext med setCamera zoom 4: formerna lika (px = 4 × "
              "världen)",
              mått(px) + " / " + mått(w));
    }
    {
        const glaze::TextStyle st{.size = glaze::world(2.0f)};
        const auto [px, pxUtan] = båda(skala, "Stationen", st, glaze::Unit::Px);
        const auto [w, wUtan]   = båda(skala, "Stationen", st, glaze::Unit::World);
        // Förväntat värde att fallet prövar något: kartskalans zoom är inte 1.
        const double zoom = px && w && w->width > 0.0 ? px->width / w->width : 0.0;
        check(skala.ritningar == 0 && std::abs(zoom - 1.0) > 0.01,
              "MR4d (fixturen) kartskalan 1:500 väntar och ger en annan zoom "
              "än 1",
              "zoom " + tal(zoom));
        check(lika(px, pxUtan, 1e-6 * (px ? px->width : 1.0)) &&
                  lika(w, wUtan, 1e-9),
              "MR4d setMapScale satt före öppningen, mätt före första "
              "ritningen: formen med Ctx räknar den väntande skalan",
              mått(px) + " / " + mått(pxUtan));
    }

    // ---- MR5. En 3D-yta som aldrig ritats har ingen känd yta: ett mått som
    // behöver världsenheten svarar nullopt, ett i px mäts. Ett Ctx utan
    // fönstrets resurser svarar nullopt.
    {
        const glaze::Ctx c       = rymd.behaviorCtx();
        const auto       iVärlden = rymd.measureText(
            c, "Höjd", glaze::TextStyle{.size = 14.0f}, glaze::Unit::World);
        const auto       världStorlek = rymd.measureText(
            c, "Höjd", glaze::TextStyle{.size = glaze::world(2.0f)});
        const auto       iPx = rymd.measureText(c, "Höjd",
                                                glaze::TextStyle{.size = 14.0f});
        glaze::Ctx       utan = karta.behaviorCtx();
        utan.canvas           = nullptr;
        const auto       tom  = karta.measureText(utan, "abc");
        check(rymd.is3D() && rymd.ritningar == 0,
              "MR5 (fixturen) 3D-ytan har en 3D-kamera och har aldrig ritats");
        check(!iVärlden && !världStorlek && iPx && iPx->width > 0.0,
              "MR5 en aldrig ritad 3D-yta: världsmåtten nullopt (ut i världen, "
              "storleken i världen), px mäts",
              std::string(iVärlden ? "mätt i världen " : "") +
                  (världStorlek ? "storleken i världen " : "") + mått(iPx));
        check(!tom, "MR5 ett Ctx utan fönstrets resurser svarar nullopt");
    }

    // ---- MR5b. Efter första ritningen är ytan känd: världsmåttet i 3D =
    // formen utan Ctx (båda vid den ritade ytan).
    window().renderFrame();
    {
        const glaze::TextStyle st{.size = 14.0f};
        const auto med  = rymd.measureText(rymd.behaviorCtx(), "Höjd", st,
                                           glaze::Unit::World);
        const auto utan = rymd.measureText("Höjd", st, glaze::Unit::World);
        check(rymd.ritningar > 0 &&
                  lika(med, utan, 1e-6 * (med ? med->width : 1.0)) &&
                  med->width > 0.0,
              "MR5b efter första ritningen: 3D-ytans världsmått med Ctx = utan",
              mått(med) + " / " + mått(utan));
    }
}

void fallLäsning()
{
    Läsare w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.fall();
        w.close();
    });
    w.run();
    if (!kördes) {
        std::printf("  --   MR hoppades över: inget fönster kunde skapas (ingen "
                    "display)\n");
        ++g_skips;
    }
}

void fallMedFönster()
{
    Värd w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.synkront();
        w.lagerordning();
        w.mätning();
        w.träffprovRT();
        w.inmatning();
        // TVÅ steg: händelserna det synkrona steget postade (snäppets
        // onCameraChange) körs före tröghetens — de räknas inte in där.
        glaze::app().post([&w] { w.tröghet(); });   // stänger fönstret
    });
    w.run();
    if (!kördes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ===================================================== D1
//
// RITYTANS LÖPANDE HÄNDELSER RITAR OM RITYTAN, INTE FÖNSTRET,
// i ett eget dolt fönster med en etikett BREDVID ritytan som
// räknar sina ritningar.

struct Etikett : glaze::Label {
    int ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Label::draw(c);
    }
};

struct EtikettVärd : Värd {
    Etikett etikett;   // bredvid ritytan — en full omritning syns i den
    int     e0 = 0, y0 = 0, ä0 = 0;   // snäppets utgångsläge

    EtikettVärd()
    {
        etikett.name = "etikett";
        etikett.x    = 440;
        etikett.y    = 40;
        etikett.setText("etikett");
        root().adopt(etikett);
    }

    void gest(gfx::Pointf p, gfx::GesturePhase fas, float skala, float grader)
    {
        gfx::Event e;
        e.type            = gfx::EventType::Gesture;
        e.windowId        = window().id();
        e.mouseX          = p.x;
        e.mouseY          = p.y;
        e.phase           = fas;
        e.gestureScale    = skala;
        e.gestureRotation = grader;
        handleEvent(e);
    }

    void fall();
};

void EtikettVärd::fall()
{
    nollställ();
    yta.setZoomable(true);
    yta.onCameraChange = [this] { ++ändringar; };
    const gfx::Pointf p = fys(200, 150);

    // Efter hjulets och nypningens onCameraChange: ritytan ritas om, inte
    // etiketten.
    const auto prov = [&](const char* vad, const std::function<void()>& händelse) {
        rita();
        const int e = etikett.ritningar, y = yta.ritningar, ä = ändringar;
        händelse();
        rita();
        check(ändringar > ä && yta.ritningar > y && etikett.ritningar == e, vad,
              std::to_string(ändringar - ä) + " ändringar, ritytan " +
                  std::to_string(yta.ritningar - y) + " och etiketten " +
                  std::to_string(etikett.ritningar - e) + " ritningar");
    };
    prov("D1 hjulets onCameraChange ritar om ritytan, inte fönstret (etiketten "
         "bredvid står kvar)",
         [&] { hjul(p, 1.0f); });
    prov("D1 nypningens onCameraChange ritar om ritytan, inte fönstret", [&] {
        gest(p, gfx::GesturePhase::Begin, 1.2f, 0.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
    });

    // Efter onGesture (en handler som tar gesten — kameran står still).
    {
        int gester    = 0;
        yta.onGesture = [&gester](glaze::Widget*, glaze::GestureArgs& g) {
            ++gester;
            g.handled = true;
        };
        rita();
        const int e = etikett.ritningar, y = yta.ritningar;
        gest(p, gfx::GesturePhase::Begin, 1.2f, 0.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
        rita();
        check(gester == 2 && yta.ritningar > y && etikett.ritningar == e,
              "D1 efter onGesture ritas ritytan om, inte fönstret",
              std::to_string(gester) + " steg, etiketten " +
                  std::to_string(etikett.ritningar - e) + " ritningar");
        yta.onGesture = nullptr;
    }

    // Fallet: en onCameraChange som sätter etikettens text via
    // SÄTTAREN ritar om etiketten — sättarvägen räcker.
    yta.onCameraChange = [this] {
        ++ändringar;
        etikett.setText("zoom " + std::to_string(ändringar));
    };
    rita();
    {
        const int e = etikett.ritningar;
        hjul(p, 1.0f);
        rita();
        check(etikett.ritningar > e &&
                  etikett.text() == "zoom " + std::to_string(ändringar),
              "D1 en onCameraChange som sätter etikettens text via sättaren "
              "ritar om etiketten (sättarvägen räcker)",
              "\"" + etikett.text() + "\", " +
                  std::to_string(etikett.ritningar - e) + " ritningar");
    }

    // Snäppets POSTADE onCameraChange (ur ritningen, i ramkön): släppt 5°
    // från norr glider bäringen, och varje steg kommer i pump-svansen.
    // Timern mäter efter glidningens 200 ms: etiketten ritades aldrig. (Att
    // glidningen LANDAR mäter N.)
    yta.onCameraChange = [this] { ++ändringar; };
    yta.setZoomable(false);
    yta.setRotatable(true);
    yta.setBearing(20.0f);
    rita();
    ner(fys(200, 150), gfx::kModCtrl);
    flytta(fys(200 - 15.0 / 0.8, 150), gfx::kModCtrl);   // −15° ⇒ 5°
    upp(fys(200 - 15.0 / 0.8, 150), gfx::kModCtrl);
    rita();   // klickets omritning förbrukad — snäppets första steg är postat
    e0 = etikett.ritningar;
    y0 = yta.ritningar;
    ä0 = ändringar;
    const bool glider = yta.isEasing();
    vidTimer = [this, glider] {
        check(glider && ändringar > ä0 && yta.ritningar > y0 &&
                  etikett.ritningar == e0,
              "D1 snäppets postade onCameraChange ritar om ritytan, inte "
              "fönstret",
              std::string(glider ? "" : "INGEN GLIDNING, ") +
                  std::to_string(ändringar - ä0) + " ändringar, ritytan " +
                  std::to_string(yta.ritningar - y0) + " och etiketten " +
                  std::to_string(etikett.ritningar - e0) + " ritningar");
        yta.onCameraChange = nullptr;
        close();
    };
    window().startTimer(kTimer, 400);
}

void fallLöpande()
{
    EtikettVärd w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.fall();   // timern stänger fönstret
    });
    w.run();
    if (!kördes) {
        std::printf("  --   D1 hoppades över: inget fönster kunde skapas (ingen "
                    "display)\n");
        ++g_skips;
    }
}

// ============================================== FL (en inaktiv flik)

struct RäknadYta : glaze::Canvas {
    int ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Canvas::draw(c);
    }
};

struct FlikVärd : glaze::AppWindow {
    glaze::DockHost  host;
    glaze::DockPanel a{"A"}, b{"B"};
    RäknadYta        yta;   // i a

    FlikVärd() : glaze::AppWindow("canvas_gate flik", 640, 420, gfx::Chrome::None)
    {
        host.dock = glaze::Dock::Fill;
        for (glaze::DockPanel* p : {&a, &b}) {
            p->dock  = glaze::Dock::Right;
            p->width = 300;
        }
        a.name   = "a";
        b.name   = "b";
        yta.dock = glaze::Dock::Fill;
        root().adopt(host);
        host.adopt(a);
        host.adopt(b);
        a.adopt(yta);
        host.dockPanel(b, a, glaze::Dock::Fill);   // b aktiv — ritytans flik inte
    }
    void rita()
    {
        invalidate();
        window().renderFrame();
    }
    // Ramar kameran in rektangeln `r` i ritytan: ryms den, och fyller den
    // minst halva ytan på någon axel?
    bool inramad(const glaze::Rect& r, std::string& fick)
    {
        const auto       p0 = yta.toLocal({r.x, r.y});
        const auto       p1 = yta.toLocal({r.x + r.w, r.y + r.h});
        const gfx::Sizef s  = yta.actualSize();
        if (!p0 || !p1)
            return false;
        const double sx = std::abs(p1->x - p0->x), sy = std::abs(p1->y - p0->y);
        fick = tal(sx) + " × " + tal(sy) + " i " + tal(s.w) + " × " + tal(s.h);
        return sx <= s.w + 0.5 && sy <= s.h + 0.5 &&
               (sx >= 0.5 * s.w || sy >= 0.5 * s.h);
    }
};

void fallFlik()
{
    FlikVärd w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.rita();
        check(w.yta.ritningar == 0 && w.yta.isVisible(),
              "FL (fixturen) ritytan i den inaktiva fliken är synlig men ritas "
              "inte");

        // FL1. Före första ritningen.
        const glaze::Rect r1{0, 0, 1000, 500};
        w.yta.zoomToRect(r1);
        w.a.activate();
        w.rita();
        std::string fick;
        check(w.yta.ritningar > 0 && w.inramad(r1, fick),
              "FL1 zoomToRect i en aldrig ritad inaktiv flik väntar och ramar in "
              "mot den riktiga ytan när fliken väljs",
              fick);

        // FL2. Efter första ritningen, åter inaktiv.
        w.b.activate();
        w.rita();
        const glaze::Camera före = w.yta.camera();
        const glaze::Rect   r2{0, 0, 10, 10};
        w.yta.zoomToRect(r2);
        const glaze::Camera under = w.yta.camera();
        check(under.zoom == före.zoom && under.center.x == före.center.x &&
                  under.center.y == före.center.y,
              "FL2 i en inaktiv flik efter första ritningen ändrar zoomToRect "
              "inte kameran — den cachade ytan gäller inte",
              tal(före.zoom) + " → " + tal(under.zoom));
        w.a.activate();
        w.rita();
        check(w.inramad(r2, fick),
              "FL2 …och ramar in när fliken väljs igen", fick);

        // FL3. En SATT kamera efter en väntande inramning vinner.
        w.b.activate();
        w.rita();
        w.yta.zoomToRect(r2);
        const glaze::Camera satt{{123.0, 45.0}, 2.0, 0.0f, 0.0f};
        w.yta.setCamera(satt);
        w.a.activate();
        w.rita();
        const glaze::Camera fick3 = w.yta.camera();
        check(fick3.center.x == 123.0 && fick3.center.y == 45.0 &&
                  fick3.zoom == 2.0,
              "FL3 setCamera efter en väntande zoomToRect vinner när fliken "
              "väljs",
              pt(fick3.center) + " zoom " + tal(fick3.zoom));

        // FL4. Detsamma i 3D: en väntande zoomToBox och panBy, sedan en satt
        // Camera3D.
        w.b.activate();
        w.rita();
        w.yta.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 10});
        w.yta.zoomToBox(glaze::Box3{{-1, -1, -1}, {1, 1, 1}});
        w.yta.panBy({30, 0});
        const glaze::Camera3D satt3{.target = {5, 6, 7}, .distance = 42,
                                    .azimuth = 10, .elevation = 20};
        w.yta.setCamera(satt3);
        w.a.activate();
        w.rita();
        const glaze::Camera3D fick4 = w.yta.camera3D();
        check(fick4.target.x == 5 && fick4.target.y == 6 && fick4.target.z == 7 &&
                  fick4.distance == 42,
              "FL4 setCamera(Camera3D) efter en väntande zoomToBox och panBy "
              "vinner när fliken väljs",
              "mål (" + tal(fick4.target.x) + ", " + tal(fick4.target.y) + ", " +
                  tal(fick4.target.z) + "), avstånd " + tal(fick4.distance));
        w.close();
    });
    w.run();
    if (!kördes) {
        std::printf("  --   FL hoppades över: inget fönster kunde skapas (ingen "
                    "display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== canvas_gate — ritytans gester och träffprov ===\n");
    fallH();
    fallMedFönster();
    fallLäsning();
    fallLöpande();
    fallFlik();
    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf(g_fel ? "=== canvas_gate: %d FEL av %d ===\n"
                      : "=== canvas_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    if (g_fel)
        return 1;
    return g_skips ? 77 : 0;
}
