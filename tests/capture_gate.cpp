// capture_gate — MUSENS CAPTURE: förlusten, övertagandet över fönstergränsen
// och den inaktiva första visningen.
//
// Kör:  ./build/tests/capture_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde
//       inte köras (ingen display, ingen förgrund) — det som KUNDE mätas är
//       mätt, och skippet SYNS i ctest
//
// Fasaden och fönstret bär det som dockningens levande drag och inmatningen står på:
// händelsen `gfx::EventType::MouseCaptureLost`
// (OS:et tog musens capture mitt i ett tryck — släppet kommer aldrig),
// övertagandet `gfx::Window::captureMouse` + `Widget::takePointerCapture` (ett
// tryck lämnas över till ett annat av appens fönster) med frågan
// `gfx::Window::hasMouseCapture`, attributet `WindowDesc::showInactive` (den
// första visningen aktiverar inte), introspektionen `hasPointerCapture()` och
// avbrottets krok — `pointerCaptureLost()` är
// sammanslagen med `mouseCancel(knapp)`: EN krok och EN väg (`dropPointer`),
// och varje widget som hade fått ett släpp får den, den tryckta också (input_gate
// avsnitt A mäter knapparna). Grinden mäter dem i fyra lager:
//
//   HEADLESS   en fristående widget: `hasPointerCapture()` och
//              `takePointerCapture()` är false, `setPointerCapture` en no-op.
//   FÖNSTRET   (alla plattformar) fönstrets RIKTIGA ingång
//              (`AppWindow::handleEvent`, middle_gate-prejudikatet) i ett
//              DOLT fönster med räknande testwidgets:
//       C1  setPointerCapture/releasePointerCapture ↔ hasPointerCapture();
//           ett eget släpp ger ingen `mouseCancel`
//       C2  `MouseCaptureLost` mitt i ett tryck där en annan widget tagit
//           explicit capture (dockningens fall A): den fångande får
//           `mouseCancel(Left)` EN gång, och INNE i kroken är bokföringen
//           redan nollad; den tryckta får sin `mouseCancel(Left)`; ingen
//           `mouseUp`, varken då eller vid ett senare släpp, inget klick;
//           fokus och grab står kvar
//       C3  `MouseCaptureLost` mitt i ett mittentryck: `middlePressed()`
//           släckt, `mouseCancel(Middle)` och ingen `middleUp`
//       C4  fokusförlusten tar capturen samma väg — utan knapp nere
//           `mouseCancel(None)`
//       C5  borttagning ur trädet: ingen krok, och capturen är släppt också
//           efter återadoption
//       C6  `MouseCaptureLost` utan tryck och capture: ingen krok
//       C7  `takePointerCapture()` utan pågående tryck: false och ingen
//           capture — på varje plattform
//       C8  BOKFÖRINGEN FÖRE AVBROTTETS HANDLERS:
//           förlusten mitt i ett drag-and-drop, och källans `onDragEnd`
//           (C8a) sätter en NY capture — den står kvar, och den gamla
//           hållaren får sitt besked efteråt; (C8b) låter den gamla hållaren
//           ta capturen igen — den behåller den och får inget besked; (C8c)
//           tar bort den gamla hållaren ur trädet — inget besked
//   D3D11      riktiga Win32-meddelanden till fönstrens HWND (`SendMessageW`:
//              samma tråd, så WndProc körs SYNKRONT och händelsen levereras
//              nästa pumpvarv), avlästa i `event()`-kroken och i widgetarna.
//              Två DOLDA fönster, förgrunden är en annan process:
//       D1  WM_LBUTTONDOWN och sedan WM_CAPTURECHANGED med lParam NULL
//           (Alt+Tab, och WM_CANCELMODE via DefWindowProc) ⇒ ETT
//           `MouseCaptureLost` — i ett DOLT fönster — och den tryckta släcks
//           utan `mouseUp`
//       D2  vårt eget släpp (WM_LBUTTONUP utan knappar kvar) och
//           `releaseMouseCapture()` (dropdownens förtida släpp) mitt i ett
//           tryck ger INGET — trots att ReleaseCapture skickar ett riktigt
//           WM_CAPTURECHANGED
//       D3  WM_CAPTURECHANGED utan tryck (OS:ets flytt-/storleksloop) ger
//           inget
//       D4  capture till ett annat av appens fönster UTAN uttrycklig
//           överlämning (en OS-loop, ett vanligt SetCapture) ÄR en förlust;
//           capture till fönstret självt ger inget
//       D5  `captureMouse()` utan tryck: false
//       D8  ett tryck pågår men förgrunden tillhör en annan tråd:
//           `takePointerCapture()` false och ingenting ändrat — capturen,
//           trycket och den explicita capturen står som de stod
//   D3D11 MED FÖRGRUND  ett VISAT värdfönster som är förgrunden och två
//              dolda fönster det äger:
//       D9  capture utan tryck (som OS:ets flytt-/storleksloop håller den):
//           `captureMouse()` false i fönstret självt OCH i ett annat
//           fönster, som inte tar den, och `hasMouseCapture()` false
//       D6  tryck i värden och en widget i B tar över: true, GetCapture() ==
//           B och `hasMouseCapture()`, värdens tryckta släckt utan `mouseUp`
//           och värden utan `MouseCaptureLost` (den uttryckliga
//           överlämningen). Ett WM_LBUTTONUP SOM GRINDEN SJÄLV SKICKAR till B
//           dispatchas till B:s fångande widget och inte till värden.
//           D6b: ett övertaget tryck kan också förloras (WM_CAPTURECHANGED
//           till B ⇒ B:s `MouseCaptureLost` och widgetens krok)
//       D10 ÅTERINTRÄDE I ÖVERTAGANDET: värdens
//           fångande widgets förlustkrok (a) flyttar ut mottagaren ur B, (b)
//           tar trycket vidare till ett tredje fönster C, (c) stänger B — i
//           alla tre svarar B false, ett avbrutet övertagande släpper
//           OS-capturen, och C:s övertagande i (b) står kvar orört
//       D7  `showInactive` på ett råfönster ur `gfx::createWindow`: värden
//           behåller förgrunden när det visas första gången, och en senare
//           visning aktiverar
//
// VILLKORAT (skrivs `--` och räknas som överhoppat, aldrig som grönt): D2 och
// D8 kräver att SetCapture i ett DOLT fönster tar (GetCapture() == fönstret
// efter trycket — win32k kontrollerar tråden, inte synligheten); D8 också att
// förgrunden INTE är grindens tråd; D6, D9, D10 och D7 att värdfönstret blev
// förgrunden (ctest utan skrivbord ger inget).
//
// SABOTAGEN den ska fälla:
//   * `MouseCaptureLost`-grenen bort ur `handleEvent` (C2, C3, D1)
//   * `dropPointer` anropar inte `mouseCancel` (C2, C3, C4)
//   * `mouseCancel` FÖRE nollningen (C2: capturen syns i kroken)
//   * den fångande får `Left` också utan knapp nere (C4: `None`)
//   * `MouseCaptureLost` tömmer grab-stacken som fokusförlusten (C2)
//   * `dropPointer` nollar bokföringen EFTER `dragCancel` (C8a, C8b)
//   * `dropPointer` frågar inte vakten innan beskedet (C8c)
//   * `takePointerCapture` släpper inte källans tryck med `mouseCancel`
//     (D6: den tryckta i värden får inget besked)
//   * `takePointerCapture` sätter capturen innan `captureMouse` svarat (C7)
//   * D3D11: grenen WM_CAPTURECHANGED bort (D1, D6b); utan `hadPress` (D3);
//     varje eget fönster räknat som överlämning (D4);
//     `captureMouse` utan `g_handoverTo` (D6: värden får `MouseCaptureLost`);
//     `pressCapture_` inte nollad före ReleaseCapture i WM_xBUTTONUP eller i
//     `releaseMouseCapture` (D2); `captureMouse` sätter inte `pressCapture_`
//     (D6b); läser inte HÅLLARENS `pressCapture_` (D9); utan
//     förgrundskontrollen (D8); svarar alltid false (D6)
//   * `takePointerCapture` släpper inte de andra fönstrens tryck (D6)
//   * `takePointerCapture` utan återvalidering och städning (D10a, D10c),
//     eller utan avbrottet i slingan efter varje krok (D10b: C:s övertagande
//     släpps)
//   * `showFirst` läser inte `showInactive_`, eller `setVisible` läser den
//     också vid en senare visning (D7)
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   * RIKTIG mus och riktiga OS-förlopp: att Alt+Tab och en systemdialog
//     skickar WM_CANCELMODE/WM_CAPTURECHANGED, att ett `noActivate`-fönster
//     som aldrig är förgrunden bara får rörelserna över sin synliga yta, och
//     att OS:et faktiskt ROUTAR rörelserna och släppet till B efter
//     övertagandet — D6 mäter att capturen flyttade (GetCapture()) och att
//     ett släpp som når B dispatchas rätt, men släppet skickar grinden
//     själv. Rad i doc/os/windows/TESTS.md.
//   * en nästlad pump i avbrottets handlers (en modal i `onDragEnd`) som
//     levererar ett släpp — C8 mäter ordningen utan pump.
//   * macOS, X11 och Wayland utöver C-blocket: där skickas aldrig
//     `MouseCaptureLost` och `captureMouse()` svarar false (C7 mäter bara
//     svaret). X11:s `_NET_WM_USER_TIME` och Metals `orderFront:` för
//     `showInactive` är rader i doc/os/linux och doc/os/macos/TESTS.md.
//   * att `MouseCaptureLost` passerar modalfiltret (den står inte i
//     `isInputEvent`), att den inte stänger popuper, och pekarformen efter
//     förlusten (`refreshCursor`).
//   * `showInactive` genom `AppWindow` (fältet är internt och sätts av
//     `DockFloatWindow`) — E5:s dock_gate LV10 mäter den vägen.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gfx/platform.h>
#include <glaze/glaze.h>

#ifdef GLAZE_BACKEND_D3D11
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

void hoppa(const std::string& vad)
{
    std::printf("  --   %s\n", vad.c_str());
    ++g_skips;
}

std::string tal(long long v)
{
    return std::to_string(v);
}

std::string unikTitel(const char* del)
{
#ifdef GLAZE_BACKEND_D3D11
    return std::string("capture_gate-") + del + "-" +
           std::to_string(GetCurrentProcessId());
#else
    return std::string("capture_gate-") + del;
#endif
}

// En yta som RÄKNAR sina krokar. En Panel, som i middle_gate: träffbar och
// inte fokuserbar, så ett tryck på den flyttar inte fokus.
struct Yta : glaze::Panel {
    int  ner = 0, upp = 0, mittNer = 0, mittUpp = 0, tangenter = 0;
    int  förlorad = 0, klick = 0;   // förlorad = antal `mouseCancel`
    bool tarMitten = false;   // middleDown svarar
    // Knappen i det SENASTE `mouseCancel`.
    gfx::MouseButton förloradKnapp = gfx::MouseButton::None;

    // Vad fönstrets bokföring svarade INNE i `mouseCancel`.
    const glaze::Widget* granne      = nullptr;
    bool                 fångadIKrok = true;   // hasPointerCapture()
    bool                 trycktIKrok = true;   // granne->pressed()

    // En krok som körs i `mouseCancel` — EN gång per sättning, för att
    // simulera en krok som gör något elakt (D10).
    std::function<void()> vidFörlust;

    Yta()
    {
        onClick = [this] { ++klick; };
    }
    void lägg(const char* namn, float x_, float y_, float w_, float h_)
    {
        name   = namn;
        x      = x_;
        y      = y_;
        width  = w_;
        height = h_;
    }
    // Capture och grab är skyddade på Widget — ytan ber om dem själv.
    void fånga() { setPointerCapture(); }
    void släpp() { releasePointerCapture(); }
    bool ta() { return takePointerCapture(); }
    void greppa() { grabInput(); }
    void släppGrepp() { releaseInput(); }

protected:
    void mouseDown(const glaze::Ctx&, float, float) override { ++ner; }
    bool mouseUp(const glaze::Ctx&, float, float) override
    {
        ++upp;
        return false;
    }
    bool middleDown(const glaze::Ctx&, float, float) override
    {
        ++mittNer;
        return tarMitten;
    }
    bool middleUp(const glaze::Ctx&, float, float) override
    {
        ++mittUpp;
        return false;
    }
    bool keyDown(gfx::Key, std::uint32_t) override
    {
        ++tangenter;
        return true;
    }
    bool mouseCancel(gfx::MouseButton b) override
    {
        ++förlorad;
        förloradKnapp = b;
        fångadIKrok   = hasPointerCapture();
        trycktIKrok   = granne && granne->pressed();
        if (vidFörlust) {
            const std::function<void()> f = std::move(vidFörlust);
            vidFörlust                    = nullptr;
            f();
        }
        return false;
    }
};

const char* knappNamn(gfx::MouseButton b)
{
    switch (b) {
    case gfx::MouseButton::None:   return "None";
    case gfx::MouseButton::Left:   return "Left";
    case gfx::MouseButton::Middle: return "Middle";
    case gfx::MouseButton::Right:  return "Right";
    }
    return "?";
}

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Yta              yta, annan, rörlig, källa;
    glaze::TextInput falt;
    int              förluster = 0;   // MouseCaptureLost i event()-kroken
    // Körs i källans `onDragEnd` — EN gång per sättning (C8).
    std::function<void()> vidDragSlut;

    explicit Vard(const std::string& titel)
        : glaze::AppWindow(titel, 400, 300, gfx::Chrome::None)
    {
        yta.lägg("yta", 20, 20, 120, 80);
        annan.lägg("annan", 200, 20, 120, 80);
        rörlig.lägg("rorlig", 20, 150, 80, 40);
        källa.lägg("kalla", 20, 220, 120, 40);
        annan.tarMitten = true;
        yta.granne      = &annan;
        källa.setAllowDrag(true);
        källa.onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("last");
        };
        källa.onDragEnd = [this] {
            if (vidDragSlut) {
                const std::function<void()> f = std::move(vidDragSlut);
                vidDragSlut                   = nullptr;
                f();
            }
        };
        falt.name  = "falt";
        falt.x     = 200;
        falt.y     = 150;
        falt.width = 120;
        root().adopt(yta);
        root().adopt(annan);
        root().adopt(rörlig);
        root().adopt(källa);
        root().adopt(falt);
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::MouseCaptureLost)
            ++förluster;
        return false;   // ät aldrig — fönstrets egen gren ska köras
    }

    // ---- händelserna, genom den RIKTIGA ingången
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    void mus(gfx::EventType t, gfx::Pointf p,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = b;
        handleEvent(e);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    float tröskel() { return style().dragThreshold * window().uiScale(); }
    // Ett drag-and-drop från källan: tryck och en rörelse förbi tröskeln.
    bool startaDrag()
    {
        const gfx::Pointf K = mitt(källa);
        mus(gfx::EventType::MouseButtonDown, K);
        mus(gfx::EventType::MouseMotion,
            gfx::Pointf{K.x + tröskel() + 2.0f, K.y}, gfx::MouseButton::None);
        return isDragging();
    }
};

// ================================================================ HEADLESS

void fallFristående()
{
    Yta fri;
    check(!fri.hasPointerCapture(),
          "H: en fristående widget håller ingen capture");
    fri.fånga();
    check(!fri.hasPointerCapture(),
          "H: setPointerCapture utan ägarfönster är en no-op");
    check(!fri.ta() && !fri.hasPointerCapture(),
          "H: takePointerCapture() utan ägarfönster: false");
}

// ===================================================== C — ALLA PLATTFORMAR

void fallIngången(Vard& w)
{
    // ---- C1. Introspektionen och det egna släppet.
    check(!w.yta.hasPointerCapture(), "C1: ingen capture från början");
    w.yta.fånga();
    check(w.yta.hasPointerCapture() && !w.annan.hasPointerCapture(),
          "C1: setPointerCapture ⇒ hasPointerCapture() — bara för den som "
          "tog den");
    w.yta.släpp();
    check(!w.yta.hasPointerCapture(), "C1: releasePointerCapture ⇒ false");
    check(w.yta.förlorad == 0,
          "C1: ett eget släpp ger ingen mouseCancel",
          tal(w.yta.förlorad));

    // ---- C2. MouseCaptureLost mitt i ett tryck där en ANNAN widget tagit
    //          explicit capture — dockningsvärdens fall A.
    w.falt.focus();
    const gfx::Pointf A = w.mitt(w.annan);
    w.mus(gfx::EventType::MouseButtonDown, A);
    check(w.annan.pressed() && w.falt.focused(),
          "C2 (fixturen) trycket hålls och flyttade inte fokus");
    w.yta.fånga();
    w.yta.greppa();
    const int förlusterFöre = w.förluster;
    const int annanFöre     = w.annan.förlorad;
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(w.förluster == förlusterFöre + 1,
          "C2 (fixturen) händelsen nådde fönstret");
    check(w.yta.förlorad == 1 &&
              w.yta.förloradKnapp == gfx::MouseButton::Left,
          "C2: MouseCaptureLost ⇒ den fångande får mouseCancel(Left) EN gång "
          "(vänsterknappen är nere)",
          tal(w.yta.förlorad) + " " + knappNamn(w.yta.förloradKnapp));
    check(w.annan.förlorad == annanFöre + 1 &&
              w.annan.förloradKnapp == gfx::MouseButton::Left,
          "C2: den tryckta får sin egen mouseCancel(Left) — EN krok för "
          "trycket och capturen",
          tal(w.annan.förlorad - annanFöre) + " " +
              knappNamn(w.annan.förloradKnapp));
    check(!w.yta.fångadIKrok && !w.yta.trycktIKrok,
          "C2: inne i kroken är bokföringen redan nollad — "
          "hasPointerCapture() och den tryckta pressed() är falska");
    check(!w.yta.hasPointerCapture() && !w.annan.pressed(),
          "C2: efteråt är capturen släppt och trycket släckt");
    check(w.annan.upp == 0 && w.yta.upp == 0,
          "C2: ingen mouseUp — inget släpp har skett",
          tal(w.annan.upp) + "/" + tal(w.yta.upp));
    check(w.falt.focused(),
          "C2: fokus står kvar (förlusten är ingen fokusförlust)");
    const int tangenterFöre = w.yta.tangenter;
    w.tangent(gfx::Key::A);
    check(w.yta.tangenter == tangenterFöre + 1,
          "C2: grabben står kvar — tangenten går till grabbens topp",
          tal(w.yta.tangenter - tangenterFöre));
    w.yta.släppGrepp();
    w.mus(gfx::EventType::MouseButtonUp, A);
    check(w.annan.upp == 0 && w.yta.upp == 0 && w.annan.klick == 0,
          "C2: ett senare släpp når ingen — varken mouseUp eller klick",
          tal(w.annan.upp) + "/" + tal(w.annan.klick));

    // ---- C3. Mittentrycket.
    w.mus(gfx::EventType::MouseButtonDown, A, gfx::MouseButton::Middle);
    check(w.annan.middlePressed(), "C3 (fixturen) mittentrycket hålls");
    const int c3 = w.annan.förlorad;
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(!w.annan.middlePressed(),
          "C3: MouseCaptureLost släcker middlePressed()");
    check(w.annan.förlorad == c3 + 1 &&
              w.annan.förloradKnapp == gfx::MouseButton::Middle,
          "C3: den som tog mittentrycket får mouseCancel(Middle)",
          tal(w.annan.förlorad - c3) + " " + knappNamn(w.annan.förloradKnapp));
    w.mus(gfx::EventType::MouseButtonUp, A, gfx::MouseButton::Middle);
    check(w.annan.mittUpp == 0,
          "C3: ingen middleUp — varken vid förlusten eller vid släppet",
          tal(w.annan.mittUpp));

    // ---- C4. Fokusförlusten går samma väg — utan knapp nere är beskedet
    //          capturens egen, `None`.
    const int c4 = w.yta.förlorad;
    w.yta.fånga();
    w.enkel(gfx::EventType::WindowFocusLost);
    check(w.yta.förlorad == c4 + 1 && !w.yta.hasPointerCapture() &&
              w.yta.förloradKnapp == gfx::MouseButton::None,
          "C4: fokusförlusten tar capturen — mouseCancel(None) utan knapp nere",
          tal(w.yta.förlorad - c4) + " " + knappNamn(w.yta.förloradKnapp));
    w.enkel(gfx::EventType::WindowFocusGained);

    // ---- C5. Borttagningen: ingen widgetkod under en trädmutation.
    w.rörlig.fånga();
    check(w.rörlig.hasPointerCapture(), "C5 (fixturen) den rörliga fångar");
    check(w.root().unadopt(w.rörlig), "C5 (fixturen) unadopt");
    check(w.rörlig.förlorad == 0,
          "C5: borttagningen ur trädet ger ingen mouseCancel",
          tal(w.rörlig.förlorad));
    w.root().adopt(w.rörlig);
    check(!w.rörlig.hasPointerCapture(),
          "C5: capturen är släppt — också efter återadoption");

    // ---- C6. Förlust utan tryck och utan capture.
    const int c6 = w.yta.förlorad + w.annan.förlorad + w.rörlig.förlorad;
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(w.isOpen() &&
              w.yta.förlorad + w.annan.förlorad + w.rörlig.förlorad == c6,
          "C6: MouseCaptureLost utan tryck och capture — ingen krok");

    // ---- C7. Övertagande utan pågående tryck.
    check(!w.yta.ta() && !w.yta.hasPointerCapture(),
          "C7: takePointerCapture() utan pågående tryck: false, och ingen "
          "capture");

    // ---- C8. BOKFÖRINGEN FÖRE AVBROTTETS HANDLERS:
    //          ett drag-and-drop pågår, en widget håller explicit capture, och
    //          källans onDragEnd körs mitt i förlusten.
    const gfx::Pointf K = w.mitt(w.källa);

    // C8a — handlern sätter en NY capture.
    check(w.startaDrag(), "C8a (fixturen) draget är igång");
    w.yta.fånga();
    const int c8a  = w.yta.förlorad;
    w.vidDragSlut  = [&w] { w.annan.fånga(); };
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(!w.isDragging(), "C8a (fixturen) förlusten avbröt draget");
    check(w.annan.hasPointerCapture(),
          "C8a: en capture som avbrottets handler sätter står kvar efter "
          "förlusten");
    check(w.yta.förlorad == c8a + 1 && !w.yta.hasPointerCapture(),
          "C8a: den gamla hållaren får sitt besked — efter handlern",
          tal(w.yta.förlorad - c8a));
    w.annan.släpp();
    w.mus(gfx::EventType::MouseButtonUp, K);

    // C8b — den gamla hållaren tar capturen IGEN i handlern.
    check(w.startaDrag(), "C8b (fixturen) draget är igång");
    w.yta.fånga();
    const int c8b = w.yta.förlorad;
    w.vidDragSlut = [&w] { w.yta.fånga(); };
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(w.yta.hasPointerCapture() && w.yta.förlorad == c8b,
          "C8b: en hållare som tog capturen igen i handlern behåller den — "
          "och får inget besked",
          tal(w.yta.förlorad - c8b));
    w.yta.släpp();
    w.mus(gfx::EventType::MouseButtonUp, K);

    // C8c — handlern tar bort den gamla hållaren ur trädet.
    check(w.startaDrag(), "C8c (fixturen) draget är igång");
    w.rörlig.fånga();
    const int c8c = w.rörlig.förlorad;
    bool      lossad = false;
    w.vidDragSlut    = [&w, &lossad] { lossad = w.root().unadopt(w.rörlig); };
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(lossad, "C8c (fixturen) handlern lossade hållaren");
    check(w.rörlig.förlorad == c8c,
          "C8c: en hållare som lämnade trädet i handlern får inget besked",
          tal(w.rörlig.förlorad - c8c));
    if (lossad)
        w.root().adopt(w.rörlig);
    w.mus(gfx::EventType::MouseButtonUp, K);
}

// ============================================================ D — D3D11

#ifdef GLAZE_BACKEND_D3D11
HWND hwndFör(const std::string& titel)
{
    const std::wstring w(titel.begin(), titel.end());   // titlarna är ASCII
    return FindWindowW(nullptr, w.c_str());
}

LPARAM punkt(gfx::Pointf p)
{
    return MAKELPARAM(static_cast<int>(p.x), static_cast<int>(p.y));
}

// Samma tråd ⇒ WndProc körs synkront; händelsen hamnar i backendens kö och
// levereras nästa pumpvarv.
void vänsterNer(HWND h, gfx::Pointf p)
{
    SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, punkt(p));
}
void vänsterUpp(HWND h, gfx::Pointf p)
{
    SendMessageW(h, WM_LBUTTONUP, 0, punkt(p));   // inga knappar kvar
}
void captureTill(HWND h, HWND ny)
{
    SendMessageW(h, WM_CAPTURECHANGED, 0, reinterpret_cast<LPARAM>(ny));
}

// Äger grindens tråd förgrunden? Bara då får ett övertagande hela trycket
// (`captureMouse`).
bool förgrundÄrVår()
{
    const HWND f = GetForegroundWindow();
    return f && GetWindowThreadProcessId(f, nullptr) == GetCurrentThreadId();
}

// tray_gate-prejudikatet: startades processen med SW_HIDE byter Windows ut
// den första ShowWindow mot SW_HIDE — förbrukas på ett eget fönster.
void förbrukaStartvisningen()
{
    STARTUPINFOW si{};
    si.cb = sizeof si;
    GetStartupInfoW(&si);
    if (!(si.dwFlags & STARTF_USESHOWWINDOW) || si.wShowWindow != SW_HIDE)
        return;
    const HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"",
                                   WS_OVERLAPPEDWINDOW, -32000, -32000, 1, 1,
                                   nullptr, nullptr, GetModuleHandleW(nullptr),
                                   nullptr);
    if (!h)
        return;
    ShowWindow(h, SW_SHOW);
    DestroyWindow(h);
}
#endif

void fallMedFönster()
{
    const std::string titelA = unikTitel("A");
    Vard              a(titelA);
    a.setOffscreen(true);
#ifdef GLAZE_BACKEND_D3D11
    const std::string     titelB = unikTitel("B");
    std::unique_ptr<Vard> b;
    HWND                  hA = nullptr, hB = nullptr;
    gfx::Pointf           P{};               // A:s tryckpunkt
    bool                  fångade = false;   // tog SetCapture i det dolda?
    int                   aFöre = 0, nerFöre = 0, uppFöre = 0;
#endif

    // `run()` ÄR öppnaren — aldrig show() först (dropdown_gate-läxan).
    bool                  kordes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0:
            kordes = true;
            fallIngången(a);
#ifdef GLAZE_BACKEND_D3D11
            hA = hwndFör(titelA);
            b  = std::make_unique<Vard>(titelB);
            b->setOffscreen(true);
            b->show(a);
            hB = hwndFör(titelB);
            check(hA && hB && b->isOpen(),
                  "D (fixturen) fönstren A och B är öppna och hittas");
            if (!hA || !hB || !b->isOpen()) {
                a.close();
                return;
            }
            P = a.mitt(a.annan);

            // ---- D1: tryck i A, sedan tar OS:et capturen (lParam NULL).
            aFöre   = a.förluster;
            nerFöre = a.annan.ner;
            uppFöre = a.annan.upp;
            vänsterNer(hA, P);
            captureTill(hA, nullptr);
#endif
            break;
#ifdef GLAZE_BACKEND_D3D11
        case 1:
            check(a.annan.ner == nerFöre + 1,
                  "D1 (fixturen) WM_LBUTTONDOWN nådde widgeten");
            check(a.förluster == aFöre + 1,
                  "D1: WM_CAPTURECHANGED mitt i ett tryck ⇒ ETT "
                  "MouseCaptureLost — också i ett dolt fönster",
                  tal(a.förluster - aFöre));
            check(!a.annan.pressed() && a.annan.upp == uppFöre,
                  "D1: den tryckta släcks utan mouseUp");
            vänsterUpp(hA, P);   // städning: knappen upp, capturen släpps

            // ---- D2: vårt eget släpp mitt i ett tryck.
            aFöre   = a.förluster;
            uppFöre = a.annan.upp;
            vänsterNer(hA, P);
            fångade = GetCapture() == hA;
            vänsterUpp(hA, P);
            break;
        case 2:
            check(a.annan.upp == uppFöre + 1,
                  "D2 (fixturen) släppet nådde den tryckta");
            if (fångade)
                check(a.förluster == aFöre && GetCapture() != hA,
                      "D2: vårt eget släpp (WM_LBUTTONUP) ger inget "
                      "MouseCaptureLost — trots ReleaseCaptures riktiga "
                      "WM_CAPTURECHANGED",
                      tal(a.förluster - aFöre));
            else
                hoppa("D2 eget släpp: SetCapture tog inte i det dolda "
                      "fönstret");

            // ---- D2b: dropdownens förtida släpp mitt i ett tryck.
            aFöre = a.förluster;
            vänsterNer(hA, P);
            fångade = GetCapture() == hA;
            a.window().releaseMouseCapture();
            if (fångade)
                check(GetCapture() != hA,
                      "D2b (fixturen) releaseMouseCapture släppte capturen");
            vänsterUpp(hA, P);
            break;
        case 3:
            if (fångade)
                check(a.förluster == aFöre,
                      "D2b: releaseMouseCapture() mitt i ett tryck ger inget "
                      "MouseCaptureLost",
                      tal(a.förluster - aFöre));
            else
                hoppa("D2b releaseMouseCapture: SetCapture tog inte i det "
                      "dolda fönstret");

            // ---- D3: WM_CAPTURECHANGED utan tryck (flytt-/storleksloopen).
            aFöre = a.förluster;
            captureTill(hA, nullptr);
            break;
        case 4:
            check(a.förluster == aFöre,
                  "D3: WM_CAPTURECHANGED utan pågående tryck ger inget "
                  "(OS:ets flytt-/storleksloop)",
                  tal(a.förluster - aFöre));

            // ---- D4a: capture till ett ANNAT av appens fönster, UTAN den
            //           uttryckliga överlämningen.
            aFöre = a.förluster;
            vänsterNer(hA, P);
            captureTill(hA, hB);
            break;
        case 5:
            check(a.förluster == aFöre + 1 && !a.annan.pressed(),
                  "D4: capture till ett annat av appens fönster UTAN "
                  "uttrycklig överlämning är en förlust — trycket släcks",
                  tal(a.förluster - aFöre));

            // ---- D4b: capture till fönstret SJÄLVT.
            aFöre = a.förluster;
            vänsterNer(hA, P);
            captureTill(hA, hA);
            vänsterUpp(hA, P);
            break;
        case 6:
            check(a.förluster == aFöre,
                  "D4: capture till fönstret självt ger inget",
                  tal(a.förluster - aFöre));

            // ---- D5: inget tryck pågår.
            check(!a.window().captureMouse() && !b->window().captureMouse(),
                  "D5: captureMouse() utan pågående tryck: false");

            // ---- D8: ett tryck pågår, förgrunden är en annan process.
            vänsterNer(hA, P);
            fångade = GetCapture() == hA;
            break;
        case 7: {
            if (!fångade) {
                hoppa("D8 förgrunden: SetCapture tog inte i det dolda "
                      "fönstret");
            } else if (förgrundÄrVår()) {
                hoppa("D8 förgrunden: grindens tråd äger förgrunden — "
                      "fallet kräver en annan");
            } else {
                check(a.annan.pressed(), "D8 (fixturen) A håller trycket");
                const bool tog = b->yta.ta();
                check(!tog,
                      "D8: takePointerCapture() false när förgrunden tillhör "
                      "en annan tråd");
                check(GetCapture() == hA && a.annan.pressed() &&
                          !b->yta.hasPointerCapture() &&
                          !b->window().hasMouseCapture(),
                      "D8: ingenting ändrat — capturen, trycket och den "
                      "explicita capturen står som de stod");
            }
            vänsterUpp(hA, P);
            a.close();
            return;
        }
#endif
        default:
            a.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    a.run();

    // VAKTEN: utan den kan hela den asynkrona halvan tystna och grinden
    // ändå rapportera grönt.
    if (!kordes)
        hoppa("fönsterfallen: inget fönster kunde skapas (ingen display)");
}

#ifdef GLAZE_BACKEND_D3D11
// D7 — den inaktiva första visningen, på fasadens nivå. Ett råfönster ur
// `gfx::createWindow` (App:en släpper dess händelser som ett okänt fönsters
// — med en loggrad), för attributet är internt i `AppWindow`. Körs SIST i
// förgrundsdelen: den senare visningen tar förgrunden från värden.
void fallInaktivVisning(HWND hh)
{
    if (GetForegroundWindow() != hh) {
        hoppa("D7 showInactive: värdfönstret är inte längre förgrunden");
        return;
    }
    gfx::WindowDesc d;
    d.title        = unikTitel("R");
    d.width        = 240;
    d.height       = 160;
    d.showInactive = true;
    std::unique_ptr<gfx::Window> rå = gfx::createWindow();
    if (!rå || !rå->init(d)) {
        check(false, "D7 (fixturen) råfönstret skapas");
        return;
    }
    const HWND hr = hwndFör(d.title);
    check(hr && IsWindowVisible(hr), "D7 (fixturen) råfönstret är visat");
    check(GetForegroundWindow() == hh,
          "D7: showInactive — första visningen tar inte förgrunden från "
          "värden");
    rå->setVisible(false);
    rå->setVisible(true);
    check(hr && GetForegroundWindow() == hr,
          "D7: en senare visning aktiverar som vanligt — attributet gäller "
          "bara den första");
}

// FÖRGRUNDSDELEN — ett VISAT värdfönster H (förgrunden) och två dolda
// fönster det äger, B och C. Övertagandet kräver att grindens tråd äger
// förgrunden (`captureMouse`), så D6, D9 och D10 bor här.
void fallMedFörgrund()
{
    förbrukaStartvisningen();
    const std::string     titelH = unikTitel("H");
    const std::string     titelB = unikTitel("FB");
    const std::string     titelC = unikTitel("FC");
    Vard                  h(titelH);   // VISAT
    std::unique_ptr<Vard> b, c;
    HWND                  hH = nullptr, hB = nullptr, hC = nullptr;
    gfx::Pointf           P{}, Q{}, R{};   // H:s, B:s och C:s tryckpunkter
    int  hFöre = 0, bFöre = 0, cFöre = 0, uppFöre = 0, förlFöre = 0;
    bool nästlatTog = false, lossad = false;

    bool                  kordes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            hH     = hwndFör(titelH);
            if (!hH || GetForegroundWindow() != hH) {
                hoppa("D6, D9, D10 och D7: värdfönstret blev inte förgrunden "
                      "(ingen förgrund i den här sessionen)");
                h.close();
                return;
            }
            b = std::make_unique<Vard>(titelB);
            b->setOffscreen(true);
            b->show(h);
            c = std::make_unique<Vard>(titelC);
            c->setOffscreen(true);
            c->show(h);
            hB = hwndFör(titelB);
            hC = hwndFör(titelC);
            check(hB && hC && b->isOpen() && c->isOpen(),
                  "F (fixturen) fönstren B och C är öppna och hittas");
            if (!hB || !hC || !b->isOpen() || !c->isOpen()) {
                h.close();
                return;
            }
            P = h.mitt(h.annan);
            Q = b->mitt(b->annan);
            R = c->mitt(c->annan);

            // ---- D9: capture utan tryck, som OS:ets flytt-/storleksloop.
            SetCapture(hH);
            if (GetCapture() == hH) {
                check(!h.window().captureMouse() &&
                          !h.window().hasMouseCapture(),
                      "D9: capture utan tryck i fönstret självt — "
                      "captureMouse() och hasMouseCapture() false");
                check(!b->window().captureMouse() && GetCapture() == hH,
                      "D9: ett annat fönster tar inte en capture utan tryck");
                ReleaseCapture();
            } else {
                hoppa("D9: SetCapture tog inte i värdfönstret");
            }

            // ---- D6: tryck i värden …
            hFöre = h.förluster;
            vänsterNer(hH, P);
            break;
        }
        case 1: {
            check(h.annan.pressed() && GetCapture() == hH,
                  "D6 (fixturen) värden håller trycket och OS-capturen");
            // … och en widget i B tar över det.
            uppFöre             = h.annan.upp;
            bFöre               = b->förluster;
            const int annanFörl = h.annan.förlorad;
            const bool tog      = b->yta.ta();
            check(tog, "D6: en widget i B tar över trycket — "
                       "takePointerCapture() true");
            check(GetCapture() == hB && b->window().hasMouseCapture(),
                  "D6: OS-capturen är B:s — och hasMouseCapture() svarar ja");
            check(b->yta.hasPointerCapture(),
                  "D6: B:s widget håller den explicita capturen");
            check(!h.annan.pressed() && h.annan.upp == uppFöre,
                  "D6: värdens tryckta släcks utan mouseUp");
            check(h.annan.förlorad == annanFörl + 1 &&
                      h.annan.förloradKnapp == gfx::MouseButton::Left,
                  "D6: … och får mouseCancel(Left) — övertagandet är ett "
                  "avbrott i värden (EN väg)",
                  tal(h.annan.förlorad - annanFörl) + " " +
                      knappNamn(h.annan.förloradKnapp));
            vänsterUpp(hB, Q);   // grinden skickar släppet själv
            break;
        }
        case 2:
            check(h.förluster == hFöre,
                  "D6: värden fick inget MouseCaptureLost — den uttryckliga "
                  "överlämningen",
                  tal(h.förluster - hFöre));
            check(b->yta.upp == 1 && h.annan.upp == uppFöre,
                  "D6: ett släpp som når B dispatchas till B:s fångande "
                  "widget, inte till värden",
                  tal(b->yta.upp));
            check(b->förluster == bFöre && GetCapture() != hB,
                  "D6: släppet i B släpper OS-capturen utan "
                  "MouseCaptureLost");
            b->yta.släpp();

            // ---- D6b: ett övertaget tryck kan också förloras.
            vänsterNer(hH, P);
            break;
        case 3: {
            const bool tog = b->yta.ta();
            check(tog, "D6b (fixturen) övertagandet lyckas igen");
            if (!tog) {
                vänsterUpp(hH, P);
                h.close();
                return;
            }
            bFöre    = b->förluster;
            förlFöre = b->yta.förlorad;
            captureTill(hB, nullptr);
            break;
        }
        case 4:
            check(b->förluster == bFöre + 1 &&
                      b->yta.förlorad == förlFöre + 1 &&
                      !b->yta.hasPointerCapture(),
                  "D6b: WM_CAPTURECHANGED till B efter övertagandet ⇒ B:s "
                  "MouseCaptureLost och widgetens mouseCancel",
                  tal(b->förluster - bFöre) + "/" +
                      tal(b->yta.förlorad - förlFöre));
            vänsterUpp(hB, Q);   // städning

            // ---- D10a: värdens förlustkrok flyttar ut mottagaren ur B.
            vänsterNer(hH, P);
            break;
        case 5: {
            h.yta.fånga();
            lossad          = false;
            h.yta.vidFörlust = [&] { lossad = b->root().unadopt(b->yta); };
            hFöre           = h.förluster;
            bFöre           = b->förluster;
            const bool tog  = b->yta.ta();
            check(lossad, "D10a (fixturen) värdens förlustkrok lossade B:s "
                          "widget");
            check(!tog, "D10a: takePointerCapture() false när en krok "
                        "flyttade ut mottagaren");
            check(GetCapture() != hB && !b->window().hasMouseCapture(),
                  "D10a: det avbrutna övertagandet städades — OS-capturen är "
                  "släppt");
            check(!b->yta.hasPointerCapture(),
                  "D10a: mottagaren håller ingen capture");
            if (lossad)
                b->root().adopt(b->yta);
            break;
        }
        case 6:
            check(h.förluster == hFöre && b->förluster == bFöre,
                  "D10a: varken överlämningen eller städningen gav "
                  "MouseCaptureLost");

            // ---- D10b: kroken tar trycket vidare till ett tredje fönster.
            vänsterNer(hH, P);
            break;
        case 7: {
            h.yta.fånga();
            nästlatTog       = false;
            h.yta.vidFörlust = [&] { nästlatTog = c->yta.ta(); };
            hFöre            = h.förluster;
            bFöre            = b->förluster;
            cFöre            = c->förluster;
            const int  bFörl = b->yta.förlorad;
            const bool tog   = b->yta.ta();
            check(nästlatTog, "D10b (fixturen) kroken tog trycket vidare "
                              "till C");
            check(!tog, "D10b: B:s takePointerCapture() false — trycket gick "
                        "vidare");
            check(GetCapture() == hC && c->window().hasMouseCapture() &&
                      c->yta.hasPointerCapture(),
                  "D10b: C:s övertagande står kvar — B:s slinga släppte det "
                  "inte");
            check(!b->yta.hasPointerCapture() &&
                      b->yta.förlorad == bFörl + 1,
                  "D10b: B:s widget tappade capturen till C, med besked",
                  tal(b->yta.förlorad - bFörl));
            vänsterUpp(hC, R);
            break;
        }
        case 8:
            check(h.förluster == hFöre && b->förluster == bFöre &&
                      c->förluster == cFöre,
                  "D10b: båda överlämningarna var uttryckliga — inget "
                  "MouseCaptureLost");
            check(c->yta.upp == 1,
                  "D10b (fixturen) släppet dispatchades till C:s widget",
                  tal(c->yta.upp));
            c->yta.släpp();

            // ---- D10c: kroken stänger mottagarfönstret.
            vänsterNer(hH, P);
            break;
        case 9: {
            h.yta.fånga();
            h.yta.vidFörlust = [&] { b->close(); };
            const bool tog   = b->yta.ta();
            check(!tog, "D10c: takePointerCapture() false när en krok "
                        "stängde mottagarfönstret");
            check(GetCapture() != hB,
                  "D10c: det avbrutna övertagandet släppte OS-capturen");
            check(!b->yta.hasPointerCapture(),
                  "D10c: mottagaren håller ingen capture");
            break;
        }
        case 10:
            fallInaktivVisning(hH);   // SIST — den tar förgrunden
            h.close();
            return;
        default:
            h.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    h.run();
    if (!kordes)
        hoppa("förgrundsdelen: inget fönster kunde skapas (ingen display)");
}
#endif

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("capture_gate\n");
    fallFristående();
    fallMedFönster();
#ifdef GLAZE_BACKEND_D3D11
    fallMedFörgrund();
#endif

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
