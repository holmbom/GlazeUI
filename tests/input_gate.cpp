// input_gate — INMATNINGEN: högerknappens
// krokfamilj, dubbelklicken med alla tre knapparna, avbrottet `mouseCancel` och
// tangenternas `onKeyDown`/`onKeyUp` med fokusenheten och tangentminnet — och
// appens `onMouseDown`/`onMouseUp`/`onMouseCancel` på varje
// widget (del M).
//
// Kör:  ./build/tests/input_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde inte
//       köras (ingen display; på D3D11 att SetCapture inte tog i det dolda
//       fönstret) — det som KUNDE mätas är mätt, och skippet SYNS i ctest
//
// ROUTING, och därför matar grinden FÖNSTRETS ingång (`AppWindow::handleEvent`,
// som i middle_gate) i ett DOLT fönster med räknande testwidgets i
// stället för att anropa krokarna själv: att trycket bubblar, att släppet och
// avbrottet går till rätt widget och att tangenten når kedjan i rätt ordning
// bor i `handleEvent`/`dropPointer`/`detachTree`, och en grind som anropade
// krokarna direkt hade varit grön oavsett om fönstret någonsin frågade dem.
//
//   H  HÖGERKNAPPEN
//      H1 trycket bubblar: den inre frågas först, den yttre tar, och
//         `rightPressed()` blir sann — bara hos den som tog
//      H2 drag och släpp går till den som tog trycket, också utanför den och
//         fönstret; `rightUp` EN gång med `rightPressed()` släckt
//      H3 ett taget tryck fyrar inte `root().onContextMenu`; ett tryck ingen
//         tar fyrar den med `source` = den inre
//      H4 en inaktiverad hoppas över
//      H5 vid explicit capture frågas bara den fångande
//      H6 `unadopt` mitt i trycket: varken `rightUp` eller `mouseCancel`
//      H7 pekaren ägs av den som tog trycket, också över en granne
//   D  DUBBELKLICKEN
//      D1 två snabba mittentryck på den inre, som den yttre tar: exakt ett
//         `middleDoubleClick` hos den yttre, efter `middleDown`, inget hos den
//         inre; D1b ett tryck på den inre och ett på den yttre, inom tröskeln,
//         är ETT dubbelklick (räknat på den som tog, inte den under pekaren)
//      D2 det tredje trycket börjar en ny följd, det fjärde fullbordar den
//      D3 en paus på `doubleClickMs() + 150` ger inget dubbelklick
//      D4 längre bort än tröskeln ger inget
//      D5 som D1 för högerknappen
//      D6 vänsterknappen oförändrad
//   A  AVBROTTEN
//      A1 vänster, mitten och höger nere på var sin widget, sedan
//         `WindowFocusLost`: `mouseCancel` EN gång vardera med rätt knapp,
//         alla `…Pressed()` redan falska INNE i kroken, inga `…Up` efter
//         senare släpp
//      A2 explicit capture utan knapp ⇒ `mouseCancel(None)`, och den fångande
//         får ingen rörelse efteråt
//      A3 den fångande är en annan än den tryckta och vänsterknappen är nere:
//         båda får `Left`
//      A4 ett syntetiskt `MouseCaptureLost` som A1 — men fokus står kvar och
//         grab-toppen får fortfarande KeyDown först
//      A5 ett andra mittentryck utan släpp: den gamla får `mouseCancel(Middle)`
//         FÖRE det nya `middleDown`; ett andra vänstertryck släcker den gamla
//         `pressed()` och ger den `mouseCancel(Left)`
//      A6 en `mouseCancel` som lossar nästa mottagare: nästa anropas inte
//      A7 drag-and-drop tar över trycket: inget `mouseCancel` (källan får
//         `onDragEnd`)
//      A8 ett besked som svarar true ritar om hela fönstret (en orelaterad
//         widgets `damageStamp()` stegar); false gör det inte
//      A9 KNAPPMEDVETEN CAPTURE: en fångande som
//         tagit mitten- eller högertrycket får EN rörelse per rörelse och
//         knapp — `middleDrag`/`rightDrag` och ingen `mouseDrag` till; med
//         vänsterknappen nere också `mouseDrag`
//      A10 ett kvarhängande mittentryck vars
//         `mouseCancel` tar bort widgeten under pekaren: det nya trycket
//         frågar inte den borttagna (hit-testet görs om efter avbrottet)
//      A11 en modal dialog i `rightDown` och
//         fokusförlusten i dess NÄSTLADE pump (postad dit, som OS:ets
//         händelse — handleEvent-kontraktets väg): `rightPressed()` är sann i
//         kroken, den tillfrågade får `mouseCancel(Right)`, trycket
//         återställs inte när kroken svarar true (ingen `rightDrag`/`rightUp`
//         efteråt, ingen snabbmeny), och en krok som avböjer efter avbrottet
//         ger varken en fråga till förfadern eller en snabbmeny
//   T  TANGENTERNA
//      T1 ett fokuserat TextArea: Ctrl+Enter når `onKeyDown` före `keyDown`
//         (skicka-handlern: Ctrl/Cmd, inte Alt, inte `repeat`), och
//         en handler som tar vanlig Enter lämnar texten orörd; T1c AltGr
//         (Ctrl+Alt) och en upprepning skickar inte; T1d ångra: det skickade
//         (`setText("")`) kommer inte tillbaka med Ctrl+Z, och Ctrl+Z når
//         fältet; T1e en accelerator med samma genväg fyras inte när
//         handlern tog tangenten, men utan handlern
//      T2 utan `handled` lägger Enter till "\n"
//      T3 kompositens `onKeyDown` fyras för det inre barnets tangent, EFTER
//         barnets och FÖRE barnets `keyDown`, och `handled` stoppar `keyDown`
//      T4 en rityta med ett fokuserat barnfält: ritytans `onKeyDown` fyras
//         inte för W eller F5 (ritytan är ingen komposit)
//      T5 utan fokus fyras `root().onKeyDown`
//      T6 KeyDown i A, fokus till B, KeyUp: A får `onKeyUp` och `keyUp`, B
//         ingenting; T6b upprepningen med fokus i B går till A — A får
//         `onKeyDown` med repeat och släppet, B ingetdera;
//         T6c en upprepning UTAN post (trycket åts
//         av `event()`) börjar slaget — samma kedja får upprepningen och
//         släppet
//      T7 två KeyDown utan släpp: `repeat` på det andra; efter släppet är
//         nästa `repeat` falsk
//      T8 W hålls, sedan `WindowFocusLost`: ett enda `onKeyUp(W)` med
//         `cancelled` och mods 0, och `isKeyHeld(W)` falsk inne i handlern
//      T9 `handled` ger `keyTaken(End)` för `KP_End` (den vikta), en gång
//      T10 när `event()` äter KeyUp kommer inget `onKeyUp`
//      T11 KeyDown `KP_End` och KeyUp `KP_N1` paras — `onKeyUp` får `End`
//      T12 under ett aktivt drag-and-drop sväljs ett släpp utan minnespost
//      T13 en öppen popup i fokusenheten: handlarna hoppas över vid KeyDown
//      T14 `keyUp` går till den som TOG tangenten, inte till den
//         innersta
//      T8b `hide()` med en hållen tangent: ett `onKeyUp` med `cancelled`, och
//         fönstrets fokusförlust efteråt ger inget andra
//   M  APPENS MUSHANDLERS PÅ VARJE WIDGET
//      M0 (utan fönster) handlerformerna för en härledd args-typ: en fri
//         funktion, dess pekare, en noexcept-funktion och -pekare och en
//         `std::function` med `CanvasMouseArgs&` KOMPILERAR mot basens
//         `Handler<MouseArgs>` och körs bara med ritytans args
//      M1 `onMouseDown` med knappen, punkten i widgetens LOGISKA px och
//         modifierarna, EFTER fokusflytten och FÖRE `mouseDown`; `onMouseUp`
//         före `mouseUp`, och klicket som vanligt
//      M2 `handled` på ett vänstertryck: inget `mouseDown`/`mouseDrag`/
//         `mouseUp`, inget klick, `pressed()` falsk — rörelsen kommer som
//         `appPress(Move)` och släppet som `onMouseUp`
//      M3 `handled` på ett högertryck: ingen `rightDown`-bubbling, ingen
//         snabbmeny, och `onMouseUp` till målet
//      M4 ett otaget högertryck som blir snabbmenyn: `onMouseCancel(Right)`
//         vid trycket och inget `onMouseUp`
//      M5 `onMouseUp` med `handled` på widgetens eget vänstertryck:
//         `mouseCancel(Left)` i stället för `mouseUp`, inget klick
//      M6 `WindowFocusLost` mitt i ett taget tryck: `onMouseCancel` EN gång,
//         inget `onMouseUp` efteråt
//      M7 ingen bubbling: en inaktiverad widget och dess förälder får inget;
//         tom fönsteryta ger fönstrets rot som sender
//      M8 en handler som väntar sig `CanvasMouseArgs` körs inte på en vanlig
//         widget men på ritytan, med världspunkten
//      M9 ett nytt mittentryck utan släpp: `onMouseCancel` för det förra
//         FÖRE det nyas `onMouseDown`
//      M10 ett taget tryck: `appPress` Begin, Move (inne och utanför målet)
//         och DoubleClick på det andra trycket — och hover-kroken frågas inte
//         på målet under trycket, men efteråt
//      M11 målet lämnar trädet mitt i ett taget tryck: varken `onMouseUp`
//         eller `onMouseCancel`
//      M12 ett mittentryck på ritytans barnfält fyrar inte ritytans
//         `onMouseDown` (en skriven skillnad — ingen bubbling)
//   K  D3D11 (riktiga Win32-meddelanden till fönstrets HWND, `SendMessageW` —
//      samma tråd, WndProc synkront, händelsen nästa pumpvarv)
//      K1 WM_CAPTURECHANGED till ett eget STATIC-fönster mitt i ett tryck ⇒
//         den tryckta får `mouseCancel(Left)`
//      K2 lParam = fönstret självt ⇒ ingenting
//      K3 lParam = ett ANNAT glaze-fönster utan uttrycklig överlämning ⇒ ett
//         avbrott (dockningens undantag för egna fönster gäller INTE)
//      K4 villkorat på att SetCapture tog i det dolda fönstret:
//         `releaseMouseCapture()` mitt i ett tryck ⇒ inget avbrott; ett
//         `ReleaseCapture()` direkt från grinden ⇒ ett avbrott
//      K5 villkorat: explicit capture, tryck och WM_LBUTTONUP ⇒ den fångande
//         får `mouseUp` och ingen `mouseCancel` (vårt eget släpp)
//   KT TANGENTENS TEXT OCH TEXTREGELN, ett
//      eget fönster med menyradens M, Ctrl+M och Ctrl+Alt+2:
//      KT1 KeyDown med texten "å" ⇒ `onKeyDown` får den, upprepningen igen
//      KT2 ett m i ett fokuserat fält kör inte genvägen M; samma tangent utan
//          text gör det, och med fokus på en knapp gör m det
//      KT3 Ctrl+M utan text i ett fält körs · KT4 AltGr+2 ("@", Ctrl|Alt) i
//          ett fält kör ingen Ctrl+Alt+2 · KT5 ett skrivskyddat fält släpper
//          bokstavsgenvägen
//   AW AKTIVT FÖNSTER: AW1 WindowFocusGained/Lost ⇒ `isActive()`
//      och `app().activeWindow()`, `hide()` släcker · AW2 `focusedWidget()`,
//      talfältets inre fält · AW3 ett ägt verktygsfönsters reserv
//      (`acceleratorFallback` → `owner()->tryShortcut`) kör ägarens genväg
//   KW D3D11, WndProc: KW1 postat WM_CHAR å + WM_KEYDOWN ⇒ `onKeyDown`
//      får "å" och fältet tecknet EN gång · KW2 surrogatparet ⇒ ett tecken ·
//      KW3 ´ + x ⇒ "´x" och två tecken · KW4 WM_SYSKEYDOWN ⇒ ingen text
//   KP POPUPEN SOM TAR TANGENTBORDET — en apps egen popup (ett
//      vanligt `AppWindow` med `setPopup` + `setPopupTakesKeys` och ett
//      sökfält) över ett offscreen-fönster med ett eget fält i fokus, allt
//      genom ÄGARENS ingång: KP1 tryck, tecken och släpp ⇒ popupens fält får
//      texten, `onKeyDown` och `onKeyUp`, ägarens fält ingetdera och ägarens
//      `event()`-krok ser inget · KP3 ett tryck i ägaren INNAN popupen öppnas
//      släpps hos ägaren · KP4 Escape stänger popupen, och släppet sväljs ·
//      KP5 utan attributet går tecknet till ägarens fält
//
// SABOTAGEN den ska fälla (ett i taget, mot ett rent original):
//   * `forwardToKeyPopup` bort ur `handleEvent` (KP1) · vidarebefordran EFTER
//     appens `event()`-krok (KP1: kroken ser tangenterna) · Escape-steget för
//     popupen bort (KP4) · biten i `keysToPopup_` aldrig satt (KP4: släppet når
//     ägarens fält) · släppet av ägarens eget tryck skickat till popupen i
//     stället (KP3) · `openKeyPopup` utan `popupTakesKeys_` (KP5).
//   * textregeln flyttad efter `tryShortcut` (KT2) · utan `wantsTextInput`
//     (KT5) · texten kopieras inte till args (KT1, KW1)
//   * D3D11: WM_CHAR tas inte ur kön i WM_KEYDOWN (KW1 — två tecken);
//     surrogaten bryts i `charText` (KW2)
//   * `active_` nollas inte i `hide()` (AW1)
//   * högergrenen bort ur MouseButtonDown (H1)
//   * snabbmenyn fyras också när trycket togs (H3)
//   * `rightWidget_` nollas inte i `detachTree` (H6)
//   * `rightDrag` levereras inte (H2)
//   * dubbelklicket räknas på widgeten under pekaren (D1, D1b)
//   * räknaren nollas inte efter ett dubbelklick (D2)
//   * `dropPointer` anropas inte vid fokusförlust (A1)
//   * bokföringen nollas efter kroken (A1 — `pressed()` sann i kroken)
//   * den fångande får `Left` utan knapp (A2)
//   * ingen gren för `MouseCaptureLost` (A4)
//   * `MouseCaptureLost` tömmer grab-stacken (A4)
//   * ett nytt tryck avbryter inte det gamla (A5)
//   * `mouseCancel` vid dra-och-släppets övertagande (A7)
//   * ett besked som svarar true ritar inte om (A8)
//   * capture-dispatchen inte knappmedveten (A9)
//   * `routeAuxDown` räknar inte om widgeten under pekaren efter det
//     kvarhängande tryckets `mouseCancel` (A10)
//   * mottagaren registreras först EFTER kroken (A11: `rightPressed()` falsk
//     i kroken, inget `mouseCancel`, trycket återupplivas); sekvens-
//     identiteten prövas inte (A11: trycket återställs, eller förfadern
//     frågas och snabbmenyn fyras för ett tryck som tog slut)
//   * handlers efter `keyDown` i stället för före (T1, T3)
//   * enheten tar med förfäder som inte är kompositer (T4)
//   * släppet routas till aktuellt fokus (T6)
//   * upprepningen ersätter tryckets post, eller går till fokusenheten i
//     stället för tryckets kedja (T6b)
//   * en upprepning utan post skapar ingen post (T6c: inget släpp)
//   * `repeat` räknas efter `setKeyHeld` (T7)
//   * inget syntetiskt släpp vid fokusförlust, eller två (T8, T8b)
//   * `keyTaken` anropas inte (T9; canvas3d_gate T och WASD)
//   * posten slängs inte när `event()` äter släppet (T10)
//   * numpadtvillingen paras inte (T11)
//   * släppet utan post under drag-and-drop levereras (T12)
//   * popupregeln bort (T13)
//   * `keyUp` till den innersta i stället för mottagaren (T14)
//   * fönstret fyrar inte `onMouseDown`, eller fyrar den före fokusflytten
//     (M1; canvas_gate IN1)
//   * `handled` hindrar inte `mouseDown`, eller trycket blir ändå
//     `pressedWidget_` (M2)
//   * snabbmenyn fyras, eller `rightDown` bubblar, för ett taget högertryck
//     (M3; canvas_gate IN1)
//   * inget `appMouseCancel` efter snabbmenyn (M4)
//   * `onMouseUp`s `handled` ger ändå `mouseUp` och klicket (M5)
//   * `dropPointer` glömmer appens tryck (M6; canvas_gate IN4)
//   * `mouseTarget` bubblar till föräldern, eller ger nullptr på tom yta (M7)
//   * `dynamic_cast` i `Handler`s ättlingsform ersatt med `static_cast` (M8:
//     handlern körs på en vanlig widget med fel typ; M0 med basens args)
//   * `HandlerArgOf` med `remove_cvref_t` i stället för `decay_t`, eller utan
//     `noexcept`-pekarens `SecondParam` (M0 KOMPILERAR INTE)
//   * ett nytt tryck med samma knapp avbryter inte appens förra (M9)
//   * `appPress(Move)` levereras inte, eller bara inne i målet (M10;
//     canvas_gate gummibandet); hover-kroken frågas på målet under trycket
//     (M10)
//   * `forgetWidgets`/`detachTree` nollar inte appens tryck, eller fyrar
//     `onMouseCancel` under trädmutationen (M11)
//   * målet räknas som första förfader med handler (M12, M7)
//   * D3D11: grenen WM_CAPTURECHANGED bort (K1); filtret `ny != hwnd_` bort
//     (K2); varje eget fönster räknat som överlämning (K3);
//     `pressCapture_` nollas inte före vårt eget ReleaseCapture (K4, K5)
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   * riktig mus och riktiga OS-förlopp: att en annan app eller en
//     systemdialog tar capture, Aero Snap, Alt+Tab med händerna — K-delen når
//     WndProc men inte `SetCapture`-mekaniken i ett synligt förgrundsfönster
//     (capture_gate D mäter fler av filtren). Rad i doc/os/windows/TESTS.md.
//   * Waylands leave-regel och fokusgruppsundantaget mot en riktig kompositör,
//     och popupens förmedling av ett släpp till ägaren (bara Wayland ger en
//     popup tangentbordet) — rader i doc/os/linux/TESTS.md.
//   * macOS Cmd-släppet (rad i doc/os/macos/TESTS.md) och X11 överhuvudtaget.
//   * OS:ets riktiga autorepetition — bara ramverkets `repeat` ur mängden.
//   * att `doubleClickMs()` är användarens inställning.
//   * en riktig dropdown i T13 — en testwidget svarar `hasOpenPopup()` som
//     dropdownen gör när listan är öppen (popupens tangentväg mäts inte).
//   * ritytans del (`onMouseCancel`, `Right` i mushandlarna) — canvas_gate
//     avsnitt R.
//   * M: riktig mus, Metal/Linux och appens handlers på RIKTIGA widgets
//     (knappen, reglaget) — testytorna är paneler; att `MouseCaptureLost` och
//     ett annat fönsters övertagande ger `onMouseCancel` (samma
//     `dropPointer`-väg som M6, inte mätt separat); att loggraden för M8
//     skrivs EN gång (bara att handlern inte körs).
//   * KP: Waylands DIREKTA väg (popupens grab — samma `handleEvent` som
//     vidarebefordran anropar, protected utifrån), en tangent som hölls när
//     popupen öppnades och UPPREPAS där (släpps hos båda — inte skrivet), en
//     nästlad popup i popupen, och en riktig noActivate-popup på Metal/X11 —
//     doc/os/*/TESTS.md.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

std::string fog(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : " ") + x;
    return "[" + s + "]";
}

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

std::string unikTitel(const char* del)
{
#ifdef GLAZE_BACKEND_D3D11
    return std::string("input_gate-") + del + "-" +
           std::to_string(GetCurrentProcessId());
#else
    return std::string("input_gate-") + del;
#endif
}

// ================================================================ HEADLESS

void fallKeyArgs()
{
    const glaze::KeyArgs k;
    check(k.key == gfx::Key::Unknown && k.mods == 0 && !k.handled &&
              !k.repeat && !k.cancelled,
          "KeyArgs: Unknown, inga modifierare, varken hanterad, upprepning "
          "eller avbruten från början");
    glaze::Panel fri;
    check(!fri.rightPressed(),
          "rightPressed(): false för en fristående widget (inget fönster)");
}

// ---------------------------------------------------------------------- M0
//
// HANDLERFORMERNA FÖR EN HÄRLEDD ARGS-TYP:
// en ritytehandler som FRI FUNKTION — tilldelad som funktion och som pekare —,
// en `noexcept`-funktion, en `noexcept`-funktionspekare och en `std::function`
// med `glaze::CanvasMouseArgs&` ska KOMPILERA mot basens `Handler<MouseArgs>`,
// och de körs bara när argsen
// är ritytans. Raderna nedan är själva kompileringsbeviset.
int g_friaAnrop = 0;
void friRitytehandler(glaze::Widget*, glaze::CanvasMouseArgs&)
{
    ++g_friaAnrop;
}
void friNoexcept(glaze::Widget*, glaze::CanvasMouseArgs&) noexcept
{
    ++g_friaAnrop;
}

void fallHandlerFormer()
{
    glaze::Canvas karta;
    karta.onMouseDown   = friRitytehandler;    // funktionen själv
    karta.onMouseUp     = &friRitytehandler;   // pekaren
    karta.onMouseCancel = friNoexcept;         // en noexcept-funktion
    void (*pekare)(glaze::Widget*, glaze::CanvasMouseArgs&) noexcept =
        friNoexcept;
    glaze::Handler<glaze::MouseArgs> viaPekare;
    viaPekare = pekare;
    const std::function<void(glaze::Widget*, glaze::CanvasMouseArgs&)> fn =
        [](glaze::Widget*, glaze::CanvasMouseArgs&) { ++g_friaAnrop; };
    glaze::Handler<glaze::MouseArgs> viaFunction;
    viaFunction = fn;

    glaze::CanvasMouseArgs rityta;
    g_friaAnrop = 0;
    karta.onMouseDown(&karta, rityta);
    karta.onMouseUp(&karta, rityta);
    karta.onMouseCancel(&karta, rityta);
    viaPekare(&karta, rityta);
    viaFunction(&karta, rityta);
    check(g_friaAnrop == 5,
          "M0: fri funktion, funktionspekare, noexcept-funktion och -pekare och "
          "std::function med CanvasMouseArgs& körs med ritytans args",
          tal(g_friaAnrop));

    glaze::MouseArgs vanlig;
    g_friaAnrop = 0;
    karta.onMouseDown(&karta, vanlig);
    karta.onMouseCancel(&karta, vanlig);
    viaPekare(&karta, vanlig);
    viaFunction(&karta, vanlig);
    check(g_friaAnrop == 0,
          "M0: … och körs inte med basens args (fel typ loggas)",
          tal(g_friaAnrop));
}

// ============================================================= MED FÖNSTER

// En yta som RÄKNAR de nya krokarna och loggar ordningen i en gemensam logg.
// En Panel: träffbar, inte fokuserbar (ett vänstertryck flyttar inte fokus),
// och kan vara både förälder och löv.
struct Räknare : glaze::Panel {
    std::string               id;
    std::vector<std::string>* logg = nullptr;

    bool tarHöger   = false;   // rightDown svarar
    bool tarMitten  = false;   // middleDown svarar
    bool tarTangent = false;   // keyDown svarar
    bool svarAvbrott = false;  // mouseCancel svarar
    bool korsPekare = false;   // cursorAt: Move medan högertrycket hålls

    int hNer = 0, hDrag = 0, hUpp = 0, mNer = 0, mDrag = 0, mUpp = 0;
    int vNer = 0, vDrag = 0, vUpp = 0;
    int dubbelV = 0, dubbelM = 0, dubbelH = 0;
    int avbrott = 0, tangentNer = 0, tangentUpp = 0, taget = 0;
    int hover = 0;   // hover-kroken `mouseMove` (M10)
    gfx::MouseButton avbrottKnapp = gfx::MouseButton::None;
    gfx::Key         sistTaget    = gfx::Key::Unknown;
    gfx::Key         sistUpp      = gfx::Key::Unknown;
    // Vad fönstrets bokföring svarade INNE i `mouseCancel` — en sann rad är
    // ett fel (bokföringen ska vara nollad före kroken).
    bool tryckIAvbrott = false, mittIAvbrott = false, högerIAvbrott = false;
    bool fångadIAvbrott = false;
    bool högerIUpp = true;   // `rightPressed()` inne i rightUp
    bool högerIKroken = false;   // `rightPressed()` inne i rightDown (A11)
    // Körs i `mouseCancel` — EN gång per sättning (A6, A10).
    std::function<void()> vidAvbrott;
    // Körs i `rightDown` före svaret — EN gång per sättning (A11).
    std::function<void()> vidHöger;

    using glaze::Widget::damageStamp;

    void koppla(const std::string& namn, std::vector<std::string>& l)
    {
        id   = namn;
        name = namn;
        logg = &l;
    }
    void lägg(float x_, float y_, float w_, float h_)
    {
        x      = x_;
        y      = y_;
        width  = w_;
        height = h_;
    }
    void fånga() { setPointerCapture(); }
    void släpp() { releasePointerCapture(); }
    void greppa() { grabInput(); }
    void släppGrepp() { releaseInput(); }
    void nollaRäkning()
    {
        hNer = hDrag = hUpp = mNer = mDrag = mUpp = 0;
        vNer = vDrag = vUpp = 0;
        dubbelV = dubbelM = dubbelH = 0;
        avbrott = tangentNer = tangentUpp = taget = 0;
        hover   = 0;
        avbrottKnapp = gfx::MouseButton::None;
        sistTaget = sistUpp = gfx::Key::Unknown;
        tryckIAvbrott = mittIAvbrott = högerIAvbrott = fångadIAvbrott = false;
        högerIUpp    = true;
        högerIKroken = false;
    }

protected:
    void skriv(const std::string& vad)
    {
        if (logg)
            logg->push_back(id + ":" + vad);
    }
    void mouseDown(const glaze::Ctx&, float, float) override
    {
        ++vNer;
        skriv("vNer");
    }
    void mouseDrag(const glaze::Ctx&, float, float) override { ++vDrag; }
    bool mouseUp(const glaze::Ctx&, float, float) override
    {
        ++vUpp;
        skriv("vUpp");
        return false;
    }
    void mouseDoubleClick(const glaze::Ctx&, float, float) override
    {
        ++dubbelV;
        skriv("dubbelV");
    }
    bool middleDown(const glaze::Ctx&, float, float) override
    {
        ++mNer;
        skriv("mNer");
        return tarMitten;
    }
    void middleDrag(const glaze::Ctx&, float, float) override { ++mDrag; }
    bool middleUp(const glaze::Ctx&, float, float) override
    {
        ++mUpp;
        skriv("mUpp");
        return false;
    }
    void middleDoubleClick(const glaze::Ctx&, float, float) override
    {
        ++dubbelM;
        skriv("dubbelM");
    }
    bool rightDown(const glaze::Ctx&, float, float) override
    {
        ++hNer;
        skriv("hNer");
        högerIKroken = rightPressed();
        if (vidHöger) {
            const std::function<void()> f = std::move(vidHöger);
            vidHöger                      = nullptr;
            f();
        }
        return tarHöger;
    }
    void rightDrag(const glaze::Ctx&, float, float) override { ++hDrag; }
    bool rightUp(const glaze::Ctx&, float, float) override
    {
        ++hUpp;
        högerIUpp = rightPressed();
        skriv("hUpp");
        return false;
    }
    void rightDoubleClick(const glaze::Ctx&, float, float) override
    {
        ++dubbelH;
        skriv("dubbelH");
    }
    bool mouseCancel(gfx::MouseButton b) override
    {
        ++avbrott;
        avbrottKnapp   = b;
        tryckIAvbrott  = tryckIAvbrott || pressed();
        mittIAvbrott   = mittIAvbrott || middlePressed();
        högerIAvbrott  = högerIAvbrott || rightPressed();
        fångadIAvbrott = fångadIAvbrott || hasPointerCapture();
        skriv(std::string("avbrott:") + knappNamn(b));
        if (vidAvbrott) {
            const std::function<void()> f = std::move(vidAvbrott);
            vidAvbrott                    = nullptr;
            f();
        }
        return svarAvbrott;
    }
    bool keyDown(gfx::Key, std::uint32_t) override
    {
        ++tangentNer;
        skriv("keyDown");
        return tarTangent;
    }
    bool keyUp(gfx::Key k, std::uint32_t) override
    {
        ++tangentUpp;
        sistUpp = k;
        skriv("keyUp");
        return false;
    }
    void keyTaken(gfx::Key k) override
    {
        ++taget;
        sistTaget = k;
        skriv("taget");
    }
    gfx::PointerShape cursorAt(const glaze::Ctx&, float, float) const override
    {
        if (!korsPekare)
            return gfx::PointerShape::Arrow;
        return rightPressed() ? gfx::PointerShape::Move
                              : gfx::PointerShape::Crosshair;
    }
    // ETT TRYCK APPEN TOG (M10): stegen i loggen.
    void appPress(const glaze::Ctx&, float, float, gfx::MouseButton b,
                  AppPressStep steg) override
    {
        const char* n = steg == AppPressStep::Begin  ? "Begin"
                        : steg == AppPressStep::Move ? "Move"
                                                     : "DoubleClick";
        skriv(std::string("app:") + n + ":" + knappNamn(b));
    }
    void mouseMove(const glaze::Ctx& c, float x, float y) override
    {
        ++hover;
        glaze::Panel::mouseMove(c, x, y);
    }
};

// Ett tabbstopp som räknar som `Räknare`.
struct Fokusbar : Räknare {
    bool focusable() const override { return true; }
};

// En widget i fokusenheten som svarar `hasOpenPopup()` — dropdownens svar när
// listan är öppen (T13).
struct MedPopup : Fokusbar {
    bool öppen = false;

protected:
    bool hasOpenPopup() const override { return öppen; }
};

// En KOMPOSIT i Tab-mening (talfältets form): fokuserbar, ersätter sina barn
// som tabbstopp och lämnar fokus till ett inre barn.
struct Komposit : Räknare {
    Fokusbar inre;
    Widget*  ombud = &inre;

    bool    focusable() const override { return true; }
    bool    focusesChildren() const override { return true; }
    Widget* focusProxy() const override { return ombud; }
};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    std::vector<std::string> logg;
    Räknare  yttre, inre, av, fri, granne, ingen, rörlig, källa, grabbare;
    Räknare  ltryck, mtryck, htryck;   // A1: var sin knapp
    Räknare  lös;                      // M11: lämnar trädet mitt i trycket
    glaze::TextArea meddelande;
    Fokusbar a, b;
    Komposit komp;
    glaze::Canvas    yta;
    glaze::TextInput ytFält;
    MedPopup pop;

    int  snabbmenyer = 0;
    glaze::Widget* snabbKälla = nullptr;
    bool ätSläpp = false;   // T10: event() äter KeyUp
    bool ätTryck = false;   // T6c: event() äter KeyDown
    int  dragSlut = 0;      // A7: källans onDragEnd

    explicit Vard(const std::string& titel)
        : glaze::AppWindow(titel, 800, 640, gfx::Chrome::None)
    {
        yttre.koppla("yttre", logg);
        inre.koppla("inre", logg);
        av.koppla("av", logg);
        fri.koppla("fri", logg);
        granne.koppla("granne", logg);
        ingen.koppla("ingen", logg);
        rörlig.koppla("rorlig", logg);
        källa.koppla("kalla", logg);
        grabbare.koppla("grabbare", logg);
        ltryck.koppla("ltryck", logg);
        mtryck.koppla("mtryck", logg);
        htryck.koppla("htryck", logg);
        lös.koppla("los", logg);
        a.koppla("a", logg);
        b.koppla("b", logg);
        komp.koppla("komp", logg);
        komp.inre.koppla("kompInre", logg);
        pop.koppla("pop", logg);

        yttre.lägg(20, 20, 200, 150);
        inre.lägg(10, 10, 80, 60);
        av.lägg(100, 10, 80, 60);
        fri.lägg(250, 20, 120, 80);
        granne.lägg(250, 130, 120, 60);
        ingen.lägg(400, 20, 60, 40);
        rörlig.lägg(400, 80, 60, 40);
        grabbare.lägg(480, 20, 60, 40);
        ltryck.lägg(560, 20, 60, 40);
        mtryck.lägg(560, 80, 60, 40);
        htryck.lägg(560, 140, 60, 40);
        lös.lägg(650, 500, 60, 40);
        källa.lägg(20, 200, 120, 40);
        a.lägg(350, 260, 60, 30);
        b.lägg(420, 260, 60, 30);
        komp.lägg(500, 260, 120, 40);
        komp.inre.lägg(5, 5, 50, 30);
        pop.lägg(350, 320, 60, 30);

        yttre.tarHöger  = true;
        yttre.tarMitten = true;
        av.tarHöger     = true;
        av.tarMitten    = true;
        av.disable();
        fri.tarHöger    = true;
        fri.tarMitten   = true;
        rörlig.tarHöger = true;
        rörlig.tarMitten = true;
        mtryck.tarMitten = true;
        htryck.tarHöger  = true;
        grabbare.tarTangent = true;

        meddelande.name   = "meddelande";
        meddelande.x      = 20;
        meddelande.y      = 260;
        meddelande.width  = 300;
        meddelande.height = 80;

        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 360;
        yta.width  = 300;
        yta.height = 150;
        ytFält.name  = "ytFalt";
        ytFält.x     = 10;
        ytFält.y     = 10;
        ytFält.width = 120;

        källa.setAllowDrag(true);
        källa.onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("last");
        };
        källa.onDragEnd = [this] { ++dragSlut; };

        root().onContextMenu = [this](glaze::Widget*,
                                      glaze::ContextMenuArgs& e) {
            ++snabbmenyer;
            snabbKälla = e.source;
        };

        root().adopt(yttre);
        yttre.adopt(inre);
        yttre.adopt(av);
        root().adopt(fri);
        root().adopt(granne);
        root().adopt(ingen);
        root().adopt(rörlig);
        root().adopt(grabbare);
        root().adopt(ltryck);
        root().adopt(mtryck);
        root().adopt(htryck);
        root().adopt(källa);
        root().adopt(meddelande);
        root().adopt(a);
        root().adopt(b);
        root().adopt(komp);
        komp.adopt(komp.inre);
        root().adopt(yta);
        yta.adopt(ytFält);
        root().adopt(pop);
        root().adopt(lös);
    }

    bool event(const gfx::Event& e) override
    {
        return (ätSläpp && e.type == gfx::EventType::KeyUp) ||
               (ätTryck && e.type == gfx::EventType::KeyDown);
    }

    // ---- händelserna, genom den RIKTIGA ingången
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    gfx::Rectf rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    void mus(gfx::EventType t, gfx::Pointf p, gfx::MouseButton knapp,
             std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = knapp;
        e.mods     = mods;
        handleEvent(e);
    }
    void ner(gfx::Pointf p, gfx::MouseButton k)
    {
        mus(gfx::EventType::MouseButtonDown, p, k);
    }
    void upp(gfx::Pointf p, gfx::MouseButton k)
    {
        mus(gfx::EventType::MouseButtonUp, p, k);
    }
    void flytta(gfx::Pointf p)
    {
        mus(gfx::EventType::MouseMotion, p, gfx::MouseButton::None);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void tangent(gfx::EventType t, gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void tNer(gfx::Key k, std::uint32_t mods = 0)
    {
        tangent(gfx::EventType::KeyDown, k, mods);
    }
    void tUpp(gfx::Key k, std::uint32_t mods = 0)
    {
        tangent(gfx::EventType::KeyUp, k, mods);
    }
    void skrivText(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::snprintf(e.text, sizeof e.text, "%s", s);
        handleEvent(e);
    }
    float tröskel() { return style().dragThreshold * window().uiScale(); }
};

// En dialog att pumpa NÄSTLAT i (A11) — som i `commit_gate` (O1).
struct Dialog : glaze::AppWindow {
    Dialog() : glaze::AppWindow(unikTitel("dialog"), 200, 100, gfx::Chrome::None)
    {
    }
};

// ---------------------------------------------------------------------- H

void fallHöger(Vard& w)
{
    const gfx::Pointf ute{-30.0f, -30.0f};   // utanför fönstret
    const auto H = gfx::MouseButton::Right;

    // ---- H1. Trycket bubblar.
    w.logg.clear();
    w.ner(w.mitt(w.inre), H);
    check(w.logg == std::vector<std::string>{"inre:hNer", "yttre:hNer"},
          "H1: högertrycket frågar widgeten under pekaren, sedan förfadern",
          fog(w.logg));
    check(w.yttre.rightPressed() && !w.inre.rightPressed(),
          "H1: den som svarade true HÅLLER trycket — rightPressed() bara där");

    // ---- H2. Drag och släpp till den som tog, var pekaren än står.
    w.flytta(w.mitt(w.fri));
    w.flytta(ute);
    check(w.yttre.hDrag == 2 && w.fri.hDrag == 0 && w.inre.hDrag == 0,
          "H2: draget över en annan widget och utanför fönstret går till den "
          "som tog trycket",
          tal(w.yttre.hDrag) + "/" + tal(w.fri.hDrag));
    w.logg.clear();
    w.upp(ute, H);
    check(w.logg == std::vector<std::string>{"yttre:hUpp"} && w.yttre.hUpp == 1,
          "H2: släppet utanför fönstret når den som tog trycket, EN gång",
          fog(w.logg));
    check(!w.yttre.högerIUpp && !w.yttre.rightPressed(),
          "H2: rightPressed() är släckt inne i rightUp och efteråt");
    w.upp(w.mitt(w.yttre), H);
    check(w.yttre.hUpp == 1, "H2: ett släpp utan tryck når ingen",
          tal(w.yttre.hUpp));

    // ---- H3. Snabbmenyn: inte för ett taget tryck, men för ett otaget.
    check(w.snabbmenyer == 0,
          "H3: ett tryck en widget tog fyrar inte onContextMenu",
          tal(w.snabbmenyer));
    w.yttre.tarHöger = false;
    w.ner(w.mitt(w.inre), H);
    check(w.snabbmenyer == 1 && w.snabbKälla == &w.inre,
          "H3: ett tryck ingen tar fyrar onContextMenu med source = den inre",
          tal(w.snabbmenyer));
    w.upp(w.mitt(w.inre), H);
    w.yttre.tarHöger = true;

    // ---- H4. En inaktiverad hoppas över.
    w.logg.clear();
    w.ner(w.mitt(w.av), H);
    check(w.logg == std::vector<std::string>{"yttre:hNer"} &&
              w.yttre.rightPressed() && !w.av.rightPressed(),
          "H4: den inaktiverade frågas inte — förfadern tar trycket",
          fog(w.logg));
    w.upp(w.mitt(w.av), H);

    // ---- H5. Explicit capture: bara den fångande frågas.
    w.fri.fånga();
    w.logg.clear();
    w.ner(w.mitt(w.yttre), H);
    check(w.logg == std::vector<std::string>{"fri:hNer"} && w.fri.rightPressed(),
          "H5: under explicit capture frågas den fångande, inte den under "
          "pekaren",
          fog(w.logg));
    w.upp(w.mitt(w.yttre), H);
    w.fri.släpp();

    // ---- H6. unadopt mitt i trycket: varken rightUp eller mouseCancel.
    const gfx::Pointf R = w.mitt(w.rörlig);
    w.ner(R, H);
    check(w.rörlig.rightPressed(), "H6 (fixturen) den rörliga håller trycket");
    check(w.root().unadopt(w.rörlig), "H6 (fixturen) unadopt mitt i trycket");
    check(!w.rörlig.rightPressed(), "H6: borttagningen släcker trycket");
    w.flytta(w.mitt(w.fri));
    w.upp(w.mitt(w.fri), H);
    w.enkel(gfx::EventType::WindowFocusLost);
    w.enkel(gfx::EventType::WindowFocusGained);
    check(w.rörlig.hDrag == 0 && w.rörlig.hUpp == 0 && w.rörlig.avbrott == 0,
          "H6: varken drag, rightUp eller mouseCancel till en widget utanför "
          "trädet",
          tal(w.rörlig.hDrag) + "/" + tal(w.rörlig.hUpp) + "/" +
              tal(w.rörlig.avbrott));
    w.root().adopt(w.rörlig);

    // ---- H7. Pekaren ägs av den som tog trycket.
    w.yttre.korsPekare = true;
    w.flytta(w.mitt(w.yttre));
    w.ner(w.mitt(w.yttre), H);
    check(w.window().cursor() == gfx::PointerShape::Move,
          "H7: den som tog trycket bestämmer pekaren",
          formNamn(w.window().cursor()));
    w.flytta(w.mitt(w.granne));
    check(w.window().cursor() == gfx::PointerShape::Move,
          "H7: … också över en granne", formNamn(w.window().cursor()));
    w.upp(w.mitt(w.granne), H);
    check(w.window().cursor() == gfx::PointerShape::Arrow,
          "H7: efter släppet är det grannens pil", formNamn(w.window().cursor()));
    w.yttre.korsPekare = false;
}

// ---------------------------------------------------------------------- D

void fallDubbel(Vard& w)
{
    const auto M = gfx::MouseButton::Middle;
    const auto H = gfx::MouseButton::Right;
    const auto V = gfx::MouseButton::Left;
    const auto tryck = [&w](gfx::Pointf p, gfx::MouseButton k) {
        w.ner(p, k);
        w.upp(p, k);
    };
    // En ny följd: en paus längre än OS:ets dubbelklickstid.
    const auto vila = [] {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(gfx::doubleClickMs() + 150));
    };

    // ---- D1. Två snabba mittentryck på den inre, som den yttre tar.
    w.yttre.nollaRäkning();
    w.inre.nollaRäkning();
    w.logg.clear();
    const gfx::Pointf I = w.mitt(w.inre);
    tryck(I, M);
    tryck(I, M);
    check(w.yttre.dubbelM == 1 && w.inre.dubbelM == 0,
          "D1: exakt ett middleDoubleClick — hos den som tog trycket, inte hos "
          "den under pekaren",
          tal(w.yttre.dubbelM) + "/" + tal(w.inre.dubbelM));
    {
        std::vector<std::string> y;
        for (const std::string& s : w.logg)
            if (s.rfind("yttre:", 0) == 0)
                y.push_back(s);
        check(y == std::vector<std::string>{"yttre:mNer", "yttre:mUpp",
                                            "yttre:mNer", "yttre:dubbelM",
                                            "yttre:mUpp"},
              "D1: dubbelklicket kommer EFTER det andra middleDown", fog(y));
    }

    // ---- D1b. Ett tryck på den inre och nästa på den yttre, inom tröskeln:
    //           samma widget TOG båda, alltså ett dubbelklick.
    vila();
    w.yttre.nollaRäkning();
    const gfx::Rectf ir = w.rekt(w.inre);
    const float      d  = w.tröskel() / 4.0f;
    const gfx::Pointf innanför{ir.x + ir.w - d, ir.y + ir.h / 2.0f};
    const gfx::Pointf utanför{ir.x + ir.w + d, ir.y + ir.h / 2.0f};
    tryck(innanför, M);
    tryck(utanför, M);
    check(w.yttre.dubbelM == 1,
          "D1b: tryck på den inre och sedan på den yttre — båda tagna av den "
          "yttre — är ETT dubbelklick",
          tal(w.yttre.dubbelM));

    // ---- D2. Det tredje trycket börjar en ny följd, det fjärde fullbordar.
    vila();
    w.fri.nollaRäkning();
    const gfx::Pointf F = w.mitt(w.fri);
    tryck(F, M);
    tryck(F, M);
    tryck(F, M);
    check(w.fri.dubbelM == 1,
          "D2: tre snabba tryck är ETT dubbelklick", tal(w.fri.dubbelM));
    tryck(F, M);
    check(w.fri.dubbelM == 2,
          "D2: det fjärde fullbordar den nya följden", tal(w.fri.dubbelM));

    // ---- D3. En paus längre än OS:ets tid ger inget.
    vila();
    w.fri.nollaRäkning();
    tryck(F, M);
    vila();
    tryck(F, M);
    check(w.fri.dubbelM == 0,
          "D3: en paus på doubleClickMs() + 150 ger inget dubbelklick",
          tal(w.fri.dubbelM));

    // ---- D4. Längre bort än tröskeln ger inget.
    vila();
    w.fri.nollaRäkning();
    tryck(F, M);
    tryck(gfx::Pointf{F.x + w.tröskel() + 2.0f, F.y}, M);
    check(w.fri.dubbelM == 0,
          "D4: längre bort än dragtröskeln ger inget dubbelklick",
          tal(w.fri.dubbelM));

    // ---- D5. Högerknappen som D1.
    vila();
    w.yttre.nollaRäkning();
    w.inre.nollaRäkning();
    tryck(I, H);
    tryck(I, H);
    check(w.yttre.dubbelH == 1 && w.inre.dubbelH == 0 && w.snabbmenyer == 1,
          "D5: två snabba högertryck ger ett rightDoubleClick hos den som tog "
          "— och ingen ny snabbmeny",
          tal(w.yttre.dubbelH) + "/" + tal(w.inre.dubbelH) + "/" +
              tal(w.snabbmenyer));

    // ---- D6. Vänsterknappen oförändrad.
    vila();
    w.granne.nollaRäkning();
    const gfx::Pointf G = w.mitt(w.granne);
    tryck(G, V);
    tryck(G, V);
    check(w.granne.dubbelV == 1,
          "D6: två snabba vänstertryck ger ett mouseDoubleClick",
          tal(w.granne.dubbelV));
    vila();
}

// ---------------------------------------------------------------------- A

void fallAvbrott(Vard& w)
{
    const auto V = gfx::MouseButton::Left;
    const auto M = gfx::MouseButton::Middle;
    const auto H = gfx::MouseButton::Right;
    const gfx::Pointf L  = w.mitt(w.ltryck);
    const gfx::Pointf MP = w.mitt(w.mtryck);
    const gfx::Pointf HP = w.mitt(w.htryck);
    const auto nolla = [&w] {
        w.ltryck.nollaRäkning();
        w.mtryck.nollaRäkning();
        w.htryck.nollaRäkning();
    };
    const auto allaTre = [&w] {
        return w.ltryck.avbrott == 1 &&
               w.ltryck.avbrottKnapp == gfx::MouseButton::Left &&
               w.mtryck.avbrott == 1 &&
               w.mtryck.avbrottKnapp == gfx::MouseButton::Middle &&
               w.htryck.avbrott == 1 &&
               w.htryck.avbrottKnapp == gfx::MouseButton::Right;
    };
    const auto räkning = [&w] {
        return tal(w.ltryck.avbrott) + knappNamn(w.ltryck.avbrottKnapp) + "/" +
               tal(w.mtryck.avbrott) + knappNamn(w.mtryck.avbrottKnapp) + "/" +
               tal(w.htryck.avbrott) + knappNamn(w.htryck.avbrottKnapp);
    };
    const auto rentIKroken = [&w] {
        const Räknare* r[] = {&w.ltryck, &w.mtryck, &w.htryck};
        for (const Räknare* x : r)
            if (x->tryckIAvbrott || x->mittIAvbrott || x->högerIAvbrott)
                return false;
        return true;
    };

    // ---- A1. Tre knappar nere, därefter fokusförlust.
    nolla();
    w.ner(L, V);
    w.ner(MP, M);
    w.ner(HP, H);
    check(w.ltryck.pressed() && w.mtryck.middlePressed() &&
              w.htryck.rightPressed(),
          "A1 (fixturen) tre tryck hålls");
    w.enkel(gfx::EventType::WindowFocusLost);
    check(allaTre(),
          "A1: fokusförlusten ger mouseCancel EN gång per tryck, med knappen",
          räkning());
    check(rentIKroken(),
          "A1: pressed()/middlePressed()/rightPressed() är redan släckta inne "
          "i kroken");
    w.upp(L, V);
    w.upp(MP, M);
    w.upp(HP, H);
    check(w.ltryck.vUpp == 0 && w.mtryck.mUpp == 0 && w.htryck.hUpp == 0 &&
              w.ltryck.avbrott == 1,
          "A1: inga …Up efter senare släpp — och inga nya avbrott");
    w.enkel(gfx::EventType::WindowFocusGained);

    // ---- A2. Explicit capture utan knapp.
    w.grabbare.nollaRäkning();
    w.grabbare.fånga();
    w.enkel(gfx::EventType::WindowFocusLost);
    check(w.grabbare.avbrott == 1 &&
              w.grabbare.avbrottKnapp == gfx::MouseButton::None &&
              !w.grabbare.fångadIAvbrott,
          "A2: en capture utan knapp ger mouseCancel(None), med capturen "
          "redan släppt i kroken",
          tal(w.grabbare.avbrott) + knappNamn(w.grabbare.avbrottKnapp));
    w.flytta(w.mitt(w.fri));
    check(w.grabbare.vDrag == 0, "A2: ingen rörelse till den fångande efteråt",
          tal(w.grabbare.vDrag));
    w.enkel(gfx::EventType::WindowFocusGained);

    // ---- A3. Den fångande är en annan än den tryckta, vänster nere.
    nolla();
    w.grabbare.nollaRäkning();
    w.ner(L, V);
    w.grabbare.fånga();
    w.enkel(gfx::EventType::WindowFocusLost);
    check(w.ltryck.avbrott == 1 && w.ltryck.avbrottKnapp == V &&
              w.grabbare.avbrott == 1 && w.grabbare.avbrottKnapp == V,
          "A3: den tryckta och den fångande får båda mouseCancel(Left)",
          tal(w.ltryck.avbrott) + knappNamn(w.ltryck.avbrottKnapp) + "/" +
              tal(w.grabbare.avbrott) + knappNamn(w.grabbare.avbrottKnapp));
    w.upp(L, V);
    w.enkel(gfx::EventType::WindowFocusGained);

    // ---- A4. MouseCaptureLost: som A1, men fokus och grab står kvar.
    nolla();
    w.a.focus();
    w.grabbare.nollaRäkning();
    w.grabbare.greppa();
    w.ner(L, V);
    w.ner(MP, M);
    w.ner(HP, H);
    w.enkel(gfx::EventType::MouseCaptureLost);
    check(allaTre(), "A4: MouseCaptureLost ger mouseCancel per tryck",
          räkning());
    check(rentIKroken(), "A4: bokföringen är nollad inne i kroken");
    check(w.a.focused(), "A4: fokus står kvar (ingen fokusförlust)");
    w.tNer(gfx::Key::F1);
    check(w.grabbare.tangentNer == 1,
          "A4: grabben står kvar — grab-toppen får tangenten först",
          tal(w.grabbare.tangentNer));
    w.tUpp(gfx::Key::F1);
    w.grabbare.släppGrepp();
    w.upp(L, V);
    w.upp(MP, M);
    w.upp(HP, H);

    // ---- A5. Ett nytt tryck med samma knapp avbryter det gamla.
    w.fri.nollaRäkning();
    w.yttre.nollaRäkning();
    w.ner(w.mitt(w.fri), M);
    w.logg.clear();
    w.ner(w.mitt(w.yttre), M);
    check(w.logg == std::vector<std::string>{"fri:avbrott:Middle",
                                             "yttre:mNer"} &&
              w.yttre.middlePressed() && !w.fri.middlePressed(),
          "A5: det gamla mittentrycket får mouseCancel(Middle) FÖRE det nya "
          "middleDown",
          fog(w.logg));
    w.upp(w.mitt(w.yttre), M);
    nolla();
    w.granne.nollaRäkning();
    w.ner(L, V);   // släppet "tappas"
    w.ner(w.mitt(w.granne), V);
    check(!w.ltryck.pressed() && w.ltryck.avbrott == 1 &&
              w.ltryck.avbrottKnapp == V && w.granne.pressed(),
          "A5: ett nytt vänstertryck släcker den gamla pressed() och ger den "
          "mouseCancel(Left)",
          tal(w.ltryck.avbrott) + knappNamn(w.ltryck.avbrottKnapp));
    w.upp(w.mitt(w.granne), V);

    // ---- A6. En mouseCancel som lossar nästa mottagare.
    nolla();
    bool lossad = false;
    w.ltryck.vidAvbrott = [&w, &lossad] {
        lossad = w.root().unadopt(w.mtryck);
    };
    w.ner(L, V);
    w.ner(MP, M);
    w.enkel(gfx::EventType::WindowFocusLost);
    check(lossad, "A6 (fixturen) den tryckta lossade mittentryckets widget");
    check(w.ltryck.avbrott == 1 && w.mtryck.avbrott == 0,
          "A6: nästa mottagare anropas inte när den lämnat trädet (vakten)",
          tal(w.ltryck.avbrott) + "/" + tal(w.mtryck.avbrott));
    if (lossad)
        w.root().adopt(w.mtryck);
    w.upp(L, V);
    w.upp(MP, M);
    w.enkel(gfx::EventType::WindowFocusGained);

    // ---- A7. Drag-and-drop tar över trycket.
    w.källa.nollaRäkning();
    const int slutFöre = w.dragSlut;
    const gfx::Pointf K = w.mitt(w.källa);
    w.ner(K, V);
    w.flytta(gfx::Pointf{K.x + w.tröskel() + 2.0f, K.y});
    check(w.isDragging(), "A7 (fixturen) draget är igång");
    w.enkel(gfx::EventType::WindowFocusLost);
    check(!w.isDragging() && w.dragSlut == slutFöre + 1 &&
              w.källa.avbrott == 0,
          "A7: drag-and-drop tog trycket — källan får onDragEnd och ingen "
          "mouseCancel",
          tal(w.källa.avbrott));
    w.upp(K, V);
    w.enkel(gfx::EventType::WindowFocusGained);

    // ---- A8. Svaret ur kroken: true ritar om hela fönstret.
    // Utlösaren är ett NYTT mittentryck — fokus- och capture-förlusten ritar
    // om hela fönstret själva och hade inte kunnat skilja svaren åt.
    w.fri.nollaRäkning();
    w.fri.svarAvbrott = false;
    w.ner(w.mitt(w.fri), M);
    std::uint64_t före = w.granne.damageStamp();
    w.ner(w.mitt(w.fri), M);   // det gamla avbryts — false
    check(w.fri.avbrott == 1 && w.granne.damageStamp() == före,
          "A8: ett besked som svarar false ritar inte om grannen",
          tal(w.fri.avbrott));
    w.fri.svarAvbrott = true;
    före = w.granne.damageStamp();
    w.ner(w.mitt(w.fri), M);   // det gamla avbryts — true
    check(w.fri.avbrott == 2 && w.granne.damageStamp() != före,
          "A8: ett besked som svarar true ritar om hela fönstret",
          tal(w.fri.avbrott));
    w.fri.svarAvbrott = false;
    w.upp(w.mitt(w.fri), M);

    // ---- A9. Knappmedveten capture: en rörelse per rörelse och knapp.
    w.fri.nollaRäkning();
    w.fri.fånga();
    w.ner(w.mitt(w.fri), M);
    w.flytta(w.mitt(w.granne));
    check(w.fri.mDrag == 1 && w.fri.vDrag == 0,
          "A9: en fångande som tog mittentrycket får middleDrag och ingen "
          "mouseDrag till för samma rörelse",
          tal(w.fri.mDrag) + "/" + tal(w.fri.vDrag));
    w.upp(w.mitt(w.granne), M);
    w.ner(w.mitt(w.fri), H);
    w.flytta(w.mitt(w.yttre));
    check(w.fri.hDrag == 1 && w.fri.vDrag == 0,
          "A9: … likadant för högerdraget",
          tal(w.fri.hDrag) + "/" + tal(w.fri.vDrag));
    w.ner(w.mitt(w.fri), V);   // vänster nere också (går till den fångande)
    w.flytta(w.mitt(w.granne));
    check(w.fri.hDrag == 2 && w.fri.vDrag == 1,
          "A9: med vänsterknappen nere kommer också mouseDrag — en per knapp",
          tal(w.fri.hDrag) + "/" + tal(w.fri.vDrag));
    w.upp(w.mitt(w.granne), V);
    w.upp(w.mitt(w.granne), H);
    w.fri.släpp();
    w.flytta(w.mitt(w.yttre));
    check(w.fri.vDrag == 1, "A9: efter släppet ingen rörelse till den förra "
                            "fångande", tal(w.fri.vDrag));

    // ---- A10. Ett kvarhängande mittentryck vars avbrott TAR BORT widgeten
    //           under pekaren: det nya trycket
    //           routas mot det som står där EFTER avbrottet. Grannen är lånad
    //           och lever, så en fråga till den efter borttagningen syns.
    w.fri.nollaRäkning();
    w.granne.nollaRäkning();
    const gfx::Pointf GR = w.mitt(w.granne);
    w.ner(w.mitt(w.fri), M);   // fri tar trycket; släppet "tappas"
    bool bort = false;
    w.fri.vidAvbrott = [&w, &bort] { bort = w.root().unadopt(w.granne); };
    w.ner(GR, M);
    check(bort, "A10 (fixturen) avbrottet tog bort grannen");
    check(w.fri.avbrott == 1 && w.granne.mNer == 0,
          "A10: widgeten under pekaren räknas om efter avbrottet — den "
          "borttagna frågas inte",
          tal(w.fri.avbrott) + "/" + tal(w.granne.mNer));
    w.upp(GR, M);
    if (bort)
        w.root().adopt(w.granne);

    // ---- A11. Ett avbrott MEDAN kroken kör: en
    //           modal dialog i `rightDown`, och i dess NÄSTLADE pump
    //           fönstrets fokusförlust — postad, som OS:ets händelse kommer
    //           dit (handleEvent-kontraktets väg).
    const auto modalMedFokusförlust = [&w] {
        Dialog d;
        d.setOffscreen(true);
        glaze::app().post([&w, &d] {
            w.enkel(gfx::EventType::WindowFocusLost);
            d.close();
        });
        d.run(w);
    };
    w.htryck.nollaRäkning();
    const int menyerFöre = w.snabbmenyer;
    w.htryck.vidHöger = modalMedFokusförlust;
    w.ner(HP, H);
    check(w.htryck.högerIKroken,
          "A11: den tillfrågade är PRELIMINÄR mottagare under sin krok "
          "(rightPressed() sann i rightDown)");
    check(w.htryck.avbrott == 1 && w.htryck.avbrottKnapp == H &&
              !w.htryck.rightPressed() && w.snabbmenyer == menyerFöre,
          "A11: avbrottet i den nästlade pumpen ger mouseCancel(Right), och "
          "trycket ÅTERSTÄLLS inte när kroken svarar true — ingen snabbmeny",
          tal(w.htryck.avbrott) + knappNamn(w.htryck.avbrottKnapp) + "/" +
              tal(w.snabbmenyer - menyerFöre));
    w.flytta(w.mitt(w.granne));
    w.upp(w.mitt(w.granne), H);
    check(w.htryck.hDrag == 0 && w.htryck.hUpp == 0,
          "A11: varken rightDrag eller rightUp efter det avbrutna trycket",
          tal(w.htryck.hDrag) + "/" + tal(w.htryck.hUpp));
    w.enkel(gfx::EventType::WindowFocusGained);
    // Kroken AVBÖJER ett tryck som tog slut under den: ingen förfader frågas
    // och ingen snabbmeny fyras.
    w.inre.nollaRäkning();
    w.yttre.nollaRäkning();
    w.inre.vidHöger = modalMedFokusförlust;
    w.ner(w.mitt(w.inre), H);
    check(w.inre.avbrott == 1 && w.yttre.hNer == 0 &&
              !w.yttre.rightPressed() && w.snabbmenyer == menyerFöre,
          "A11: en krok som avböjer efter avbrottet — förfadern frågas inte "
          "och ingen snabbmeny fyras",
          tal(w.inre.avbrott) + "/" + tal(w.yttre.hNer) + "/" +
              tal(w.snabbmenyer - menyerFöre));
    w.upp(w.mitt(w.inre), H);
    w.enkel(gfx::EventType::WindowFocusGained);
}

// ---------------------------------------------------------------------- T

void fallTangenter(Vard& w)
{
    using K = gfx::Key;
    const std::uint32_t ctrl = gfx::kModCtrl;
    const std::uint32_t alt  = gfx::kModAlt;
    // Fältets ångra är plattformens genväg — Cmd på macOS, Ctrl annars
    // (`textfield::editShortcut`); skicka-handlern tar Ctrl ELLER Cmd.
#ifdef __APPLE__
    const std::uint32_t redigera = gfx::kModCmd;
#else
    const std::uint32_t redigera = ctrl;
#endif

    // ---- T1. Skicka-handlern på ett TextArea (meddelandefältet).
    int  skickade  = 0;
    bool tarEnter  = false;   // T1b: handlern tar vanlig Enter
    bool rensa     = false;   // T1d: skicka tömmer fältet
    std::string skickat;
    w.meddelande.onKeyDown = [&](glaze::Widget*, glaze::KeyArgs& e) {
        if (e.key != K::Return)
            return;
        const bool kort = (e.mods & (gfx::kModCtrl | gfx::kModCmd)) != 0 &&
                          (e.mods & gfx::kModAlt) == 0;
        if (kort && !e.repeat) {
            ++skickade;
            skickat = w.meddelande.text();
            if (rensa)
                w.meddelande.setText("");
            e.handled = true;
        } else if (tarEnter && e.mods == 0) {
            e.handled = true;
        }
    };
    w.meddelande.setText("");
    w.meddelande.focus();
    check(w.meddelande.focused(), "T1 (fixturen) meddelandefältet har fokus");
    w.skrivText("rad");
    w.tNer(K::Return, ctrl);
    w.tUpp(K::Return, ctrl);
    check(skickade == 1 && skickat == "rad" && w.meddelande.text() == "rad",
          "T1: Ctrl+Enter når fältets onKeyDown FÖRE keyDown — skickat, ingen "
          "radbrytning",
          tal(skickade) + " \"" + w.meddelande.text() + "\"");
    tarEnter = true;
    w.tNer(K::Return);
    w.tUpp(K::Return);
    check(w.meddelande.text() == "rad",
          "T1b: en handler som tar vanlig Enter lämnar texten orörd",
          "\"" + w.meddelande.text() + "\"");
    tarEnter = false;

    // ---- T2. Utan handled lägger Enter till en rad.
    w.tNer(K::Return);
    w.tUpp(K::Return);
    check(w.meddelande.text() == "rad\n",
          "T2: utan handled ger Enter en ny rad", "\"" + w.meddelande.text() + "\"");

    // ---- T1c. AltGr (Ctrl+Alt) och upprepningen skickar inte.
    const int t1c = skickade;
    w.tNer(K::Return, ctrl | alt);
    w.tUpp(K::Return, ctrl | alt);
    w.tNer(K::Return, ctrl);   // skickar
    w.tNer(K::Return, ctrl);   // OS:ets upprepning
    w.tUpp(K::Return, ctrl);
    check(skickade == t1c + 1,
          "T1c: Ctrl+Alt+Enter (AltGr) och en upprepning skickar inte — "
          "bara det första trycket",
          tal(skickade - t1c));

    // ---- T1d. Ångra: det skickade kommer inte tillbaka.
    rensa = true;
    w.meddelande.setText("");
    w.skrivText("hej");
    w.tNer(K::Return, ctrl);
    w.tUpp(K::Return, ctrl);
    check(skickat == "hej" && w.meddelande.text().empty(),
          "T1d (fixturen) skickat och tömt", "\"" + skickat + "\"");
    w.tNer(K::Z, redigera);
    w.tUpp(K::Z, redigera);
    check(w.meddelande.text().empty(),
          "T1d: Ctrl+Z når fältet, och det skickade kommer inte tillbaka "
          "(`setText` tömde historiken)",
          "\"" + w.meddelande.text() + "\"");
    w.skrivText("ab");
    w.tNer(K::Z, redigera);
    w.tUpp(K::Z, redigera);
    check(w.meddelande.text().empty(),
          "T1d: … och ångra fungerar som förut på det som skrivs efteråt",
          "\"" + w.meddelande.text() + "\"");
    rensa = false;

    // ---- T1e. En accelerator med samma genväg.
    int acc = 0;
    w.menuBar.addMenu("Arkiv");
    w.menuBar.addItem("Arkiv", "Skicka", "Ctrl+Enter");
    w.menuBar.setHandler({"Arkiv", "Skicka"}, [&acc] { ++acc; });
    w.menuBar.show();
    w.meddelande.focus();
    const int t1e = skickade;
    w.tNer(K::Return, ctrl);
    w.tUpp(K::Return, ctrl);
    check(skickade == t1e + 1 && acc == 0,
          "T1e: handlern tog Ctrl+Enter — acceleratorn med samma genväg fyras "
          "inte",
          tal(skickade - t1e) + "/" + tal(acc));
    w.meddelande.onKeyDown = nullptr;
    w.tNer(K::Return, ctrl);
    w.tUpp(K::Return, ctrl);
    check(acc == 1,
          "T1e: utan handlern avböjer fältet Ctrl+Enter och acceleratorn fyras",
          tal(acc));
    w.menuBar.hide();

    // ---- T3. Kompositens handler: innerst först, före keyDown.
    w.logg.clear();
    w.komp.nollaRäkning();
    w.komp.inre.nollaRäkning();
    w.komp.inre.onKeyDown = [&w](glaze::Widget*, glaze::KeyArgs&) {
        w.logg.push_back("kompInre:onKeyDown");
    };
    bool kompTar = false;
    w.komp.onKeyDown = [&w, &kompTar](glaze::Widget*, glaze::KeyArgs& e) {
        w.logg.push_back("komp:onKeyDown");
        e.handled = kompTar;
    };
    w.komp.focus();
    check(w.komp.inre.focused(),
          "T3 (fixturen) fokusombudet gav det inre barnet fokus");
    w.tNer(K::F3);
    w.tUpp(K::F3);
    check(w.logg.size() >= 3 && w.logg[0] == "kompInre:onKeyDown" &&
              w.logg[1] == "komp:onKeyDown" && w.logg[2] == "kompInre:keyDown",
          "T3: barnets handler, sedan kompositens, sedan barnets keyDown",
          fog(w.logg));
    kompTar = true;
    w.logg.clear();
    w.tNer(K::F4);
    check(w.komp.inre.tangentNer == 1 && w.komp.taget == 1 &&
              w.komp.sistTaget == K::F4,
          "T3: kompositens handled stoppar barnets keyDown — och kompositen "
          "får keyTaken",
          tal(w.komp.inre.tangentNer) + "/" + tal(w.komp.taget));

    // ---- T14. keyUp går till den som TOG tangenten.
    w.tUpp(K::F4);
    check(w.komp.tangentUpp == 1 && w.komp.sistUpp == K::F4 &&
              w.komp.inre.tangentUpp == 1,
          "T14: kompositen som tog F4 får släppets keyUp — inte den innersta "
          "(som fick sitt för F3)",
          tal(w.komp.tangentUpp) + "/" + tal(w.komp.inre.tangentUpp));
    w.komp.onKeyDown      = nullptr;
    w.komp.inre.onKeyDown = nullptr;

    // ---- T4. Ritytan med ett fokuserat barnfält är ingen komposit.
    int ytTangenter = 0, fältTangenter = 0;
    w.yta.onKeyDown = [&ytTangenter](glaze::Widget*, glaze::KeyArgs&) {
        ++ytTangenter;
    };
    w.ytFält.onKeyDown = [&fältTangenter](glaze::Widget*, glaze::KeyArgs&) {
        ++fältTangenter;
    };
    w.ytFält.focus();
    w.tNer(K::W);
    w.tUpp(K::W);
    w.tNer(K::F5);
    w.tUpp(K::F5);
    check(ytTangenter == 0 && fältTangenter == 2,
          "T4: ritytans onKeyDown fyras inte för W eller F5 i barnfältet — "
          "fältets gör det",
          tal(ytTangenter) + "/" + tal(fältTangenter));
    w.yta.onKeyDown    = nullptr;
    w.ytFält.onKeyDown = nullptr;

    // ---- T5. Utan fokus: fönstrets rot.
    int rotTangenter = 0;
    w.root().onKeyDown = [&rotTangenter](glaze::Widget*, glaze::KeyArgs&) {
        ++rotTangenter;
    };
    w.clearFocus();
    w.tNer(K::F6);
    w.tUpp(K::F6);
    check(rotTangenter == 1, "T5: utan fokus fyras root().onKeyDown",
          tal(rotTangenter));
    w.root().onKeyDown = nullptr;

    // ---- T6. Släppet till tryckets kedja.
    w.a.nollaRäkning();
    w.b.nollaRäkning();
    int aUpp = 0, bUpp = 0, bNer = 0;
    bool bUpprepning = false;
    gfx::Key aUppNyckel = K::Unknown;
    bool aHölls = true;
    w.a.onKeyUp = [&](glaze::Widget*, glaze::KeyArgs& e) {
        ++aUpp;
        aUppNyckel = e.key;
        aHölls     = w.isKeyHeld(e.key);
    };
    w.b.onKeyUp = [&](glaze::Widget*, glaze::KeyArgs&) { ++bUpp; };
    w.b.onKeyDown = [&](glaze::Widget*, glaze::KeyArgs& e) {
        ++bNer;
        bUpprepning = e.repeat;
    };
    w.a.focus();
    w.tNer(K::F2);
    w.b.focus();
    w.tUpp(K::F2);
    check(aUpp == 1 && w.a.tangentUpp == 1 && bUpp == 0 &&
              w.b.tangentUpp == 0,
          "T6: tryckets kedja får släppet — onKeyUp och keyUp hos A, inget hos B",
          tal(aUpp) + "/" + tal(w.a.tangentUpp) + "/" + tal(bUpp));

    // ---- T6b. Upprepningen går till TRYCKETS kedja:
    //           den som får en upprepning får
    //           släppet — B, som har fokus, får ingetdera.
    int  aNer        = 0;
    bool aUpprepning = false;
    w.a.onKeyDown = [&](glaze::Widget*, glaze::KeyArgs& e) {
        ++aNer;
        aUpprepning = e.repeat;
    };
    w.a.focus();
    w.tNer(K::F9);
    w.b.focus();
    w.tNer(K::F9);   // OS:ets upprepning, med fokus i B
    w.tUpp(K::F9);
    check(aNer == 2 && aUpprepning && bNer == 0 && aUpp == 2 &&
              aUppNyckel == K::F9 && !aHölls && bUpp == 0,
          "T6b: upprepningen med fokus i B går till A (repeat), och släppet "
          "likaså — B får varken onKeyDown eller onKeyUp; isKeyHeld redan "
          "falsk i släppet",
          tal(aNer) + "/" + tal(bNer) + "/" + tal(aUpp) + "/" + tal(bUpp));
    w.a.onKeyDown = nullptr;

    // ---- T6c. En upprepning UTAN post (trycket åts av event()) börjar
    //           slaget: samma kedja får upprepningen och släppet.
    w.b.focus();
    w.ätTryck = true;
    w.tNer(K::F12);   // trycket åts — ingen post, men tangenten hålls
    w.ätTryck = false;
    w.tNer(K::F12);   // OS:ets upprepning
    w.tUpp(K::F12);
    check(bNer == 1 && bUpprepning && bUpp == 1,
          "T6c: en upprepning utan post börjar slaget — B får upprepningen "
          "(repeat) OCH släppet",
          tal(bNer) + "/" + tal(bUpp));
    w.b.onKeyDown = nullptr;
    w.b.onKeyUp   = nullptr;

    // ---- T7. repeat ur mängden.
    std::vector<bool> upprepningar;
    w.a.onKeyDown = [&upprepningar](glaze::Widget*, glaze::KeyArgs& e) {
        upprepningar.push_back(e.repeat);
    };
    w.a.focus();
    w.tNer(K::F7);
    w.tNer(K::F7);
    w.tUpp(K::F7);
    w.tNer(K::F7);
    w.tUpp(K::F7);
    check(upprepningar == std::vector<bool>{false, true, false},
          "T7: andra KeyDown utan släpp är en upprepning; efter släppet inte",
          tal(static_cast<long long>(upprepningar.size())));

    // ---- T8. Fokusförlustens släpp.
    int t8 = 0;
    bool avbrutet = false, hölls = true;
    std::uint32_t t8mods = 1;
    w.a.onKeyDown = nullptr;
    w.a.onKeyUp = [&](glaze::Widget*, glaze::KeyArgs& e) {
        ++t8;
        avbrutet = e.cancelled;
        hölls    = w.isKeyHeld(K::W);
        t8mods   = e.mods;
    };
    w.a.focus();
    w.tNer(K::W, gfx::kModShift);
    w.enkel(gfx::EventType::WindowFocusLost);
    check(t8 == 1 && avbrutet && !hölls && t8mods == 0,
          "T8: fokusförlusten ger ETT onKeyUp(W) med cancelled och mods 0, och "
          "isKeyHeld(W) är falsk inne i handlern",
          tal(t8));
    w.tUpp(K::W);   // det riktiga släppet kom till ett annat fönster
    w.enkel(gfx::EventType::WindowFocusGained);
    check(t8 == 2 && !avbrutet,
          "T8: ett senare släpp utan post går till fokusenheten, utan "
          "cancelled (ett onKeyUp utan onKeyDown är tillåtet)",
          tal(t8));

    // ---- T9. keyTaken med den vikta tangenten.
    w.a.nollaRäkning();
    w.a.onKeyUp   = nullptr;
    w.a.onKeyDown = [](glaze::Widget*, glaze::KeyArgs& e) { e.handled = true; };
    w.a.focus();
    w.tNer(K::KP_End);
    check(w.a.taget == 1 && w.a.sistTaget == K::End && w.a.tangentNer == 0,
          "T9: handled ger keyTaken(End) för KP_End EN gång, och ingen keyDown",
          tal(w.a.taget));
    w.tUpp(K::KP_End);

    // ---- T10. event() äter släppet.
    int t10 = 0;
    w.a.onKeyDown = nullptr;
    w.a.onKeyUp = [&t10](glaze::Widget*, glaze::KeyArgs&) { ++t10; };
    w.tNer(K::F10);
    w.ätSläpp = true;
    w.tUpp(K::F10);
    w.ätSläpp = false;
    w.enkel(gfx::EventType::WindowFocusLost);
    w.enkel(gfx::EventType::WindowFocusGained);
    check(t10 == 0,
          "T10: event() åt KeyUp — inget onKeyUp, varken då eller som "
          "fokusförlustens släpp (posten slängdes med släppet)",
          tal(t10));

    // ---- T11. Numpadens två lägen paras.
    gfx::Key t11 = K::Unknown;
    int      t11n = 0;
    w.a.onKeyUp = [&](glaze::Widget*, glaze::KeyArgs& e) {
        ++t11n;
        t11 = e.key;
    };
    w.a.focus();
    w.tNer(K::KP_End);
    w.tUpp(K::KP_N1);
    check(t11n == 1 && t11 == K::End && !w.isKeyHeld(K::End),
          "T11: KeyDown KP_End och KeyUp KP_N1 är ETT tryck — onKeyUp får End",
          tal(t11n));

    // ---- T12. Under ett drag sväljs ett släpp utan post.
    int t12 = 0;
    w.a.onKeyUp = [&t12](glaze::Widget*, glaze::KeyArgs&) { ++t12; };
    w.a.focus();
    const gfx::Pointf KÄ = w.mitt(w.källa);
    w.ner(KÄ, gfx::MouseButton::Left);
    w.flytta(gfx::Pointf{KÄ.x + w.tröskel() + 2.0f, KÄ.y});
    check(w.isDragging(), "T12 (fixturen) draget är igång");
    w.tUpp(K::F11);
    check(t12 == 0, "T12: ett släpp utan post under drag-and-drop sväljs",
          tal(t12));
    w.upp(KÄ, gfx::MouseButton::Left);
    w.a.onKeyUp = nullptr;

    // ---- T13. En öppen popup i enheten.
    int popTangenter = 0;
    w.pop.nollaRäkning();
    w.pop.onKeyDown = [&popTangenter](glaze::Widget*, glaze::KeyArgs&) {
        ++popTangenter;
    };
    w.pop.öppen = true;
    w.pop.focus();
    w.tNer(K::F8);
    w.tUpp(K::F8);
    check(popTangenter == 0 && w.pop.tangentNer == 1,
          "T13: med en öppen popup i enheten hoppas handlarna över — keyDown "
          "får tangenten som förut",
          tal(popTangenter) + "/" + tal(w.pop.tangentNer));
    w.pop.öppen = false;
    w.tNer(K::F8);
    w.tUpp(K::F8);
    check(popTangenter == 1, "T13: stängd popup — handlern får tangenten",
          tal(popTangenter));
    w.pop.onKeyDown = nullptr;
}

// T8b — `hide()` med en hållen tangent. SIST i fönsterdelen: fönstret står
// därefter dolt (ett `show()` hade visat ett offscreen-fönster på riktigt).
void fallDöljning(Vard& w)
{
    int  n        = 0;
    bool avbrutet = false;
    w.a.onKeyUp   = [&](glaze::Widget*, glaze::KeyArgs& e) {
        ++n;
        avbrutet = e.cancelled;
    };
    w.a.focus();
    w.tNer(gfx::Key::Q);
    w.hide();
    check(n == 1 && avbrutet,
          "T8b: hide() med en hållen tangent ger ETT onKeyUp med cancelled",
          tal(n));
    w.enkel(gfx::EventType::WindowFocusLost);
    check(n == 1, "T8b: fönstrets fokusförlust efteråt ger inget andra",
          tal(n));
    w.a.onKeyUp = nullptr;
}

// ---------------------------------------------------------------------- K

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
#endif

// ---------------------------------------------------------------------- M

// Appens tre mushandlers på `r`, skrivna i fönstrets logg —
// "id:onDown:Left" … — och `handled` i `onMouseDown`/`onMouseUp` efter
// `taNer`/`taUpp`.
void kopplaAppMus(Räknare& r, bool taNer, bool taUpp)
{
    r.onMouseDown = [&r, taNer](glaze::Widget*, glaze::MouseArgs& e) {
        r.logg->push_back(r.id + ":onDown:" + knappNamn(e.button));
        e.handled = taNer;
    };
    r.onMouseUp = [&r, taUpp](glaze::Widget*, glaze::MouseArgs& e) {
        r.logg->push_back(r.id + ":onUp:" + knappNamn(e.button));
        e.handled = taUpp;
    };
    r.onMouseCancel = [&r](glaze::Widget*, glaze::MouseArgs& e) {
        r.logg->push_back(r.id + ":onCancel:" + knappNamn(e.button));
    };
}

void lossaAppMus(glaze::Widget& w)
{
    w.onMouseDown   = nullptr;
    w.onMouseUp     = nullptr;
    w.onMouseCancel = nullptr;
}

void fallAppMus(Vard& w)
{
    using B = gfx::MouseButton;
    using V = std::vector<std::string>;
    // Inga halva följder: ett nytt tryck på samma widget efter pausen är
    // aldrig ett dubbelklick (D3:s paus).
    const auto paus = [] {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(gfx::doubleClickMs() + 150));
    };
    paus();

    // ---- M1. `onMouseDown` FÖRE `mouseDown` och EFTER fokusflytten, med
    // knappen, den lokala punkten och modifierarna; `onMouseUp` före
    // `mouseUp`, och klicket som vanligt (kontrollen för M2 och M5).
    {
        w.logg.clear();
        w.a.nollaRäkning();
        w.b.focus();
        check(w.b.focused(), "M1 (fixturen) fokus står på grannen");
        int              klick     = 0;
        bool             fokusVid  = false;
        glaze::Widget*   avsändare = nullptr;
        glaze::MouseArgs fick;
        w.a.onClick     = [&] { ++klick; };
        w.a.onMouseDown = [&](glaze::Widget* s, glaze::MouseArgs& e) {
            w.logg.push_back(std::string("a:onDown:") + knappNamn(e.button));
            fokusVid  = w.a.focused();
            avsändare = s;
            fick      = e;
        };
        w.a.onMouseUp = [&](glaze::Widget*, glaze::MouseArgs& e) {
            w.logg.push_back(std::string("a:onUp:") + knappNamn(e.button));
        };
        const gfx::Rectf  r = w.rekt(w.a);
        const gfx::Pointf p{r.x + 7.0f, r.y + 5.0f};
        w.mus(gfx::EventType::MouseButtonDown, p, B::Left, gfx::kModShift);
        w.mus(gfx::EventType::MouseButtonUp, p, B::Left, gfx::kModShift);
        const double s = w.window().uiScale();
        check(w.logg == V{"a:onDown:Left", "a:vNer", "a:onUp:Left", "a:vUpp"},
              "M1: onMouseDown före mouseDown, onMouseUp före mouseUp",
              fog(w.logg));
        check(fokusVid, "M1: fokus har redan flyttats när onMouseDown körs");
        check(klick == 1, "M1: klicket kommer som vanligt", tal(klick));
        check(avsändare == &w.a && fick.button == B::Left &&
                  std::fabs(fick.local.x - 7.0 / s) < 0.01 &&
                  std::fabs(fick.local.y - 5.0 / s) < 0.01 &&
                  fick.mods == gfx::kModShift,
              "M1: sender, knappen, punkten i widgetens LOGISKA px och "
              "modifierarna",
              tal(static_cast<long long>(fick.local.x * 100)) + "/" +
                  tal(static_cast<long long>(fick.local.y * 100)));
        w.a.onClick = nullptr;
        lossaAppMus(w.a);
    }

    // ---- M2. `handled` på ett vänstertryck: widgeten får INGENTING av det —
    // och rörelsen under trycket kommer som `appPress(Move)`.
    {
        paus();
        w.logg.clear();
        w.a.nollaRäkning();
        int  klick  = 0;
        bool tryckt = true;
        w.a.onClick     = [&] { ++klick; };
        w.a.onMouseDown = [](glaze::Widget*, glaze::MouseArgs& e) {
            e.handled = true;
        };
        w.a.onMouseUp = [&](glaze::Widget*, glaze::MouseArgs& e) {
            w.logg.push_back(std::string("a:onUp:") + knappNamn(e.button));
        };
        const gfx::Pointf p = w.mitt(w.a);
        w.ner(p, B::Left);
        tryckt = w.a.pressed();
        w.flytta({p.x + 3.0f, p.y});
        w.upp(p, B::Left);
        check(w.a.vNer == 0 && w.a.vDrag == 0 && w.a.vUpp == 0 && klick == 0 &&
                  !tryckt &&
                  w.logg == V{"a:app:Begin:Left", "a:app:Move:Left",
                              "a:onUp:Left"},
              "M2: handled — inget mouseDown/mouseDrag/mouseUp, inget klick, "
              "inte nedtryckt, och onMouseUp vid släppet",
              fog(w.logg) + " " + tal(klick));
        w.a.onClick = nullptr;
        lossaAppMus(w.a);
    }

    // ---- M3. `handled` på ett högertryck: ingen bubbling, ingen snabbmeny,
    // och släppet till målet.
    {
        w.logg.clear();
        w.inre.nollaRäkning();
        w.yttre.nollaRäkning();
        const int menyer = w.snabbmenyer;
        w.inre.onMouseDown = [](glaze::Widget*, glaze::MouseArgs& e) {
            e.handled = e.button == gfx::MouseButton::Right;
        };
        w.inre.onMouseUp = [&](glaze::Widget*, glaze::MouseArgs& e) {
            w.logg.push_back(std::string("inre:onUp:") + knappNamn(e.button));
        };
        const gfx::Pointf p     = w.mitt(w.inre);
        w.ner(p, B::Right);
        const bool        tagen = w.yttre.rightPressed() || w.inre.rightPressed();
        w.upp(p, B::Right);
        check(w.inre.hNer == 0 && w.yttre.hNer == 0 && !tagen &&
                  w.snabbmenyer == menyer &&
                  w.logg == V{"inre:app:Begin:Right", "inre:onUp:Right"},
              "M3: handled höger — ingen rightDown-bubbling, ingen snabbmeny, "
              "onMouseUp till målet",
              fog(w.logg));
        lossaAppMus(w.inre);
    }

    // ---- M4. Ett otaget högertryck som blir snabbmenyn ⇒ onMouseCancel(Right)
    // direkt, och inget onMouseUp.
    {
        w.logg.clear();
        w.ingen.nollaRäkning();
        const int menyer = w.snabbmenyer;
        kopplaAppMus(w.ingen, false, false);
        const gfx::Pointf p = w.mitt(w.ingen);
        w.ner(p, B::Right);
        const V efterNer = w.logg;
        w.upp(p, B::Right);
        check(w.snabbmenyer == menyer + 1 &&
                  efterNer == V{"ingen:onDown:Right", "ingen:hNer",
                                "ingen:onCancel:Right"} &&
                  w.logg == efterNer,
              "M4: snabbmenyn tog trycket — onMouseCancel(Right) vid trycket, "
              "inget onMouseUp",
              fog(w.logg));
        lossaAppMus(w.ingen);
    }

    // ---- M5. `onMouseUp` med handled på ett EGET vänstertryck ⇒ widgetens
    // trycket slutar som ett avbrott: `mouseCancel(Left)`, inget `mouseUp`,
    // inget klick.
    {
        paus();
        w.logg.clear();
        w.a.nollaRäkning();
        int klick   = 0;
        w.a.onClick = [&] { ++klick; };
        kopplaAppMus(w.a, false, true);
        const gfx::Pointf p = w.mitt(w.a);
        w.ner(p, B::Left);
        w.upp(p, B::Left);
        check(w.logg == V{"a:onDown:Left", "a:vNer", "a:onUp:Left",
                          "a:avbrott:Left"} &&
                  w.a.vUpp == 0 && klick == 0,
              "M5: appen tog släppet — mouseCancel(Left) i stället för mouseUp, "
              "inget klick",
              fog(w.logg));
        w.a.onClick = nullptr;
        lossaAppMus(w.a);
    }

    // ---- M6. Fokusförlusten mitt i ett TAGET tryck ⇒ onMouseCancel EN gång,
    // inget onMouseUp efteråt.
    {
        w.logg.clear();
        kopplaAppMus(w.granne, true, false);
        const gfx::Pointf p = w.mitt(w.granne);
        w.ner(p, B::Left);
        w.enkel(gfx::EventType::WindowFocusLost);
        w.upp(p, B::Left);
        w.enkel(gfx::EventType::WindowFocusGained);
        check(w.logg == V{"granne:onDown:Left", "granne:app:Begin:Left",
                          "granne:onCancel:Left"},
              "M6: fokusförlusten ⇒ onMouseCancel EN gång, inget onMouseUp",
              fog(w.logg));
        lossaAppMus(w.granne);
    }

    // ---- M7. Ingen bubbling: en inaktiverad widget och dess förälder får
    // inget; tom fönsteryta ger fönstrets rot.
    {
        w.logg.clear();
        kopplaAppMus(w.av, false, false);
        kopplaAppMus(w.yttre, false, false);
        const gfx::Pointf p = w.mitt(w.av);
        w.ner(p, B::Left);
        w.upp(p, B::Left);
        const V inaktiv = w.logg;
        lossaAppMus(w.av);
        lossaAppMus(w.yttre);
        int            rot       = 0;
        glaze::Widget* rotSender = nullptr;
        w.root().onMouseDown = [&](glaze::Widget* s, glaze::MouseArgs&) {
            ++rot;
            rotSender = s;
        };
        // Fönstrets nedre högra hörn (800 × 640 logiska px): ingen widget där.
        const float       s = w.window().uiScale();
        const gfx::Pointf tomt{770.0f * s, 610.0f * s};
        check(!w.av.isEnabled(), "M7 (fixturen) widgeten är inaktiverad");
        w.ner(tomt, B::Left);
        w.upp(tomt, B::Left);
        w.root().onMouseDown = nullptr;
        check(inaktiv.empty(),
              "M7: en inaktiverad widget och dess förälder får inget",
              fog(inaktiv));
        check(rot == 1 && rotSender == &w.root(),
              "M7: tom fönsteryta ⇒ fönstrets rot är målet", tal(rot));
    }

    // ---- M8. En handler som väntar sig RITYTANS args: på en vanlig widget
    // körs den inte (och loggar), på ritytan körs den med världspunkten.
    {
        bool påFri = false, påYta = false, värld = false;
        w.fri.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs&) {
            påFri = true;
        };
        w.yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            påYta = true;
            värld = e.world.has_value();
        };
        const gfx::Rectf  f = w.rekt(w.fri);
        const gfx::Pointf pf{f.x + 8.0f, f.y + 8.0f};
        w.ner(pf, B::Left);
        w.upp(pf, B::Left);
        const gfx::Rectf  y = w.rekt(w.yta);
        const gfx::Pointf py{y.x + y.w - 20.0f, y.y + y.h - 20.0f};   // inte fältet
        w.ner(py, B::Left);
        w.upp(py, B::Left);
        check(!påFri,
              "M8: en CanvasMouseArgs-handler på en vanlig widget körs inte");
        check(påYta && värld,
              "M8: … på ritytan körs den, med världspunkten");
        lossaAppMus(w.fri);
        lossaAppMus(w.yta);
    }

    // ---- M9. Ett nytt tryck med samma knapp avbryter appens förra (släppet
    // kom aldrig).
    {
        w.logg.clear();
        kopplaAppMus(w.mtryck, true, false);
        const gfx::Rectf r = w.rekt(w.mtryck);
        w.ner({r.x + 5.0f, r.y + 5.0f}, B::Middle);
        w.ner({r.x + 45.0f, r.y + 32.0f}, B::Middle);   // inget släpp emellan
        w.upp({r.x + 45.0f, r.y + 32.0f}, B::Middle);
        check(w.logg == V{"mtryck:onDown:Middle", "mtryck:app:Begin:Middle",
                          "mtryck:onCancel:Middle", "mtryck:onDown:Middle",
                          "mtryck:app:Begin:Middle", "mtryck:onUp:Middle"},
              "M9: ett nytt mittentryck ⇒ onMouseCancel för det förra, sedan "
              "det nya",
              fog(w.logg));
        lossaAppMus(w.mtryck);
    }

    // ---- M10. `appPress` för ett taget tryck: Begin, Move (också utanför
    // målet) och DoubleClick — och hover-kroken frågas inte på målet under
    // trycket.
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(gfx::doubleClickMs() + 150));
        w.logg.clear();
        w.fri.nollaRäkning();
        w.fri.onMouseDown = [](glaze::Widget*, glaze::MouseArgs& e) {
            e.handled = true;
        };
        const gfx::Pointf p = w.mitt(w.fri);
        w.ner(p, B::Left);
        w.flytta({p.x + 2.0f, p.y});     // inne i målet
        w.flytta({p.x + 500.0f, p.y});   // utanför målet (och fönstret)
        const int underTrycket = w.fri.hover;
        w.upp({p.x + 500.0f, p.y}, B::Left);
        w.ner(p, B::Left);   // det andra trycket i följden
        w.upp(p, B::Left);
        w.flytta({p.x + 1.0f, p.y});     // efter trycket: hover-kroken igen
        check(w.logg == V{"fri:app:Begin:Left", "fri:app:Move:Left",
                          "fri:app:Move:Left", "fri:app:Begin:Left",
                          "fri:app:DoubleClick:Left"},
              "M10: Begin, Move inne och ute, och DoubleClick på det andra "
              "trycket",
              fog(w.logg));
        check(underTrycket == 0 && w.fri.hover >= 1,
              "M10: hover-kroken frågas inte på målet under trycket — men "
              "efteråt",
              tal(underTrycket) + "/" + tal(w.fri.hover));
        lossaAppMus(w.fri);
    }

    // ---- M11. Målet lämnar trädet mitt i ett taget tryck ⇒ varken
    // onMouseUp eller onMouseCancel.
    {
        w.logg.clear();
        kopplaAppMus(w.lös, true, false);
        const gfx::Pointf p = w.mitt(w.lös);
        w.ner(p, B::Left);
        const bool lossad = w.root().unadopt(w.lös);
        w.upp(p, B::Left);
        w.enkel(gfx::EventType::WindowFocusLost);
        w.enkel(gfx::EventType::WindowFocusGained);
        check(lossad, "M11 (fixturen) unadopt mitt i trycket");
        check(w.logg == V{"los:onDown:Left", "los:app:Begin:Left"},
              "M11: borttagen mitt i trycket — varken onMouseUp eller "
              "onMouseCancel",
              fog(w.logg));
        lossaAppMus(w.lös);
        if (lossad)
            w.root().adopt(w.lös);
    }

    // ---- M12. Ett mittentryck på ritytans BARN fyrar inte ritytans
    // onMouseDown — basens fält bubblar inte (en skriven skillnad).
    {
        int påYta = 0;
        w.yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs&) {
            ++påYta;
        };
        const gfx::Pointf p = w.mitt(w.ytFält);
        w.ner(p, B::Middle);
        w.upp(p, B::Middle);
        check(påYta == 0,
              "M12: ett mittentryck på ritytans fält når inte ritytans "
              "onMouseDown",
              tal(påYta));
        lossaAppMus(w.yta);
    }
}

void fallMedFönster()
{
    const std::string titelA = unikTitel("A");
    Vard              a(titelA);
    a.setOffscreen(true);
#ifdef GLAZE_BACKEND_D3D11
    const std::string     titelB = unikTitel("B");
    std::unique_ptr<Vard> b;
    HWND                  hA = nullptr, hB = nullptr, hS = nullptr;
    gfx::Pointf           P{};
    bool                  fångade = false;
    int                   före    = 0;
#endif

    // `run()` ÄR öppnaren — aldrig show() först (som i dropdown_gate).
    bool                  kordes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0:
            kordes = true;
            fallHöger(a);
            fallDubbel(a);
            fallAvbrott(a);
            fallTangenter(a);
            fallAppMus(a);
#ifdef GLAZE_BACKEND_D3D11
            hA = hwndFör(titelA);
            b  = std::make_unique<Vard>(titelB);
            b->setOffscreen(true);
            b->show(a);
            hB = hwndFör(titelB);
            hS = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                                 nullptr, nullptr, GetModuleHandleW(nullptr),
                                 nullptr);
            check(hA && hB && hS && b->isOpen(),
                  "K (fixturen) fönstren och STATIC-fönstret finns");
            if (!hA || !hB || !hS || !b->isOpen()) {
                fallDöljning(a);
                a.close();
                return;
            }
            P = a.mitt(a.ltryck);

            // ---- K1: STATIC-fönstret tar capturen mitt i ett tryck.
            a.ltryck.nollaRäkning();
            vänsterNer(hA, P);
            captureTill(hA, hS);
#endif
            break;
#ifdef GLAZE_BACKEND_D3D11
        case 1:
            check(a.ltryck.vNer == 1, "K1 (fixturen) WM_LBUTTONDOWN nådde widgeten");
            check(a.ltryck.avbrott == 1 &&
                      a.ltryck.avbrottKnapp == gfx::MouseButton::Left &&
                      !a.ltryck.pressed(),
                  "K1: WM_CAPTURECHANGED till ett eget STATIC-fönster mitt i "
                  "ett tryck ⇒ mouseCancel(Left) efter ett pumpvarv",
                  tal(a.ltryck.avbrott));
            vänsterUpp(hA, P);

            // ---- K2: capturen till fönstret självt.
            a.ltryck.nollaRäkning();
            vänsterNer(hA, P);
            captureTill(hA, hA);
            break;
        case 2:
            check(a.ltryck.avbrott == 0 && a.ltryck.pressed(),
                  "K2: capture till fönstret självt ger inget avbrott",
                  tal(a.ltryck.avbrott));
            vänsterUpp(hA, P);

            // ---- K3: ett ANNAT glaze-fönster utan uttrycklig överlämning.
            a.ltryck.nollaRäkning();
            vänsterNer(hA, P);
            captureTill(hA, hB);
            break;
        case 3:
            check(a.ltryck.avbrott == 1,
                  "K3: capture till ett annat av appens fönster UTAN uttrycklig "
                  "överlämning är ett avbrott (E3:s filter — inget undantag "
                  "för egna fönster)",
                  tal(a.ltryck.avbrott));
            vänsterUpp(hA, P);

            // ---- K4a: vårt eget förtida släpp.
            a.ltryck.nollaRäkning();
            vänsterNer(hA, P);
            fångade = GetCapture() == hA;
            a.window().releaseMouseCapture();
            break;
        case 4:
            if (fångade)
                check(a.ltryck.avbrott == 0,
                      "K4: releaseMouseCapture() mitt i ett tryck ger inget "
                      "avbrott",
                      tal(a.ltryck.avbrott));
            else
                hoppa("K4 releaseMouseCapture: SetCapture tog inte i det "
                      "dolda fönstret");
            vänsterUpp(hA, P);

            // ---- K4b: ett ReleaseCapture som inte är vårt.
            a.ltryck.nollaRäkning();
            vänsterNer(hA, P);
            fångade = GetCapture() == hA;
            if (fångade)
                ReleaseCapture();
            break;
        case 5:
            if (fångade)
                check(a.ltryck.avbrott == 1,
                      "K4: ett ReleaseCapture direkt från grinden mitt i ett "
                      "tryck är ett avbrott",
                      tal(a.ltryck.avbrott));
            else
                hoppa("K4 ReleaseCapture: SetCapture tog inte i det dolda "
                      "fönstret");
            // Städningens släpp levereras FÖRE K5 — annars hade det nått den
            // fångande där.
            vänsterUpp(hA, P);
            break;
        case 6:
            // ---- K5: explicit capture och vårt eget släpp.
            a.grabbare.nollaRäkning();
            a.grabbare.fånga();
            före = a.grabbare.vUpp;
            vänsterNer(hA, P);
            fångade = GetCapture() == hA;
            vänsterUpp(hA, P);
            break;
        case 7:
            if (fångade)
                check(a.grabbare.vUpp == före + 1 && a.grabbare.avbrott == 0,
                      "K5: explicit capture, tryck och WM_LBUTTONUP — den "
                      "fångande får mouseUp och ingen mouseCancel (vårt eget "
                      "släpp)",
                      tal(a.grabbare.vUpp - före) + "/" +
                          tal(a.grabbare.avbrott));
            else
                hoppa("K5: SetCapture tog inte i det dolda fönstret");
            a.grabbare.släpp();
            DestroyWindow(hS);
            hS = nullptr;
            fallDöljning(a);
            a.close();
            return;
#endif
        default:
            fallDöljning(a);
            a.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    a.run();
#ifdef GLAZE_BACKEND_D3D11
    if (hS)
        DestroyWindow(hS);
#endif

    // VAKTEN: utan den kan hela den asynkrona halvan tystna och grinden ändå
    // rapportera grönt.
    if (!kordes)
        hoppa("fönsterfallen: inget fönster kunde skapas (ingen display)");
}

// ------------------------------------------------------------- KT, AW, KW
//
// TANGENTENS TEXT OCH FÖNSTRET. Ett eget
// offscreen-fönster med menyradens genvägar M (utan modifierare), Ctrl+M och
// Ctrl+Alt+2, ett fält, ett skrivskyddat fält, en knapp och ett talfält.
struct TextVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    glaze::TextInput fält, låst;
    glaze::Button    knapp{"Knapp"};
    glaze::SpinBox   tal;
    int              m = 0, ctrlM = 0, altGr = 0;
    std::vector<glaze::KeyArgs> tryck;   // fältets onKeyDown

    explicit TextVard(const std::string& titel)
        : glaze::AppWindow(titel, 400, 300, gfx::Chrome::None)
    {
        menuBar.show();
        menuBar.addMenu("Arkiv");
        menuBar.addItem("Arkiv", "Mät", "M");
        menuBar.addItem("Arkiv", "Ctrl", "Ctrl+M");
        menuBar.addItem("Arkiv", "AltGr", "Ctrl+Alt+2");
        menuBar.setHandler({"Arkiv", "Mät"}, [this] { ++m; });
        menuBar.setHandler({"Arkiv", "Ctrl"}, [this] { ++ctrlM; });
        menuBar.setHandler({"Arkiv", "AltGr"}, [this] { ++altGr; });
        fält.name  = "falt";
        fält.x     = 10;
        fält.y     = 40;
        fält.width = 150;
        låst.name  = "last";
        låst.x     = 10;
        låst.y     = 80;
        låst.width = 150;
        låst.setReadOnly(true);
        knapp.name = "knapp";
        knapp.x    = 10;
        knapp.y    = 120;
        tal.name   = "tal";
        tal.x      = 10;
        tal.y      = 160;
        root().adopt(fält);
        root().adopt(låst);
        root().adopt(knapp);
        root().adopt(tal);
        fält.onKeyDown = [this](glaze::Widget*, glaze::KeyArgs& e) {
            tryck.push_back(e);
        };
    }
    void tangent(gfx::EventType t, gfx::Key k, std::uint32_t mods,
                 const char* text)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        std::snprintf(e.text, sizeof e.text, "%s", text);
        handleEvent(e);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
};

// Ett ägt verktygsfönster som låter ägarens genvägar svara — som
// `DockFloatWindow`.
struct Verktyg : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    explicit Verktyg(const std::string& titel)
        : glaze::AppWindow(titel, 120, 80, gfx::Chrome::None)
    {
    }
    bool acceleratorFallback(gfx::Key k, std::uint32_t mods) override
    {
        return owner() && owner()->isOpen() && owner()->tryShortcut(k, mods);
    }
};

void fallTextOchFonster()
{
    const std::string titel = unikTitel("KT");
    TextVard          w(titel);
    w.setOffscreen(true);
    Verktyg verktyg(unikTitel("KT-verktyg"));
    verktyg.setOffscreen(true);
    bool                  kordes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
#ifdef GLAZE_BACKEND_D3D11
    HWND h = nullptr;
#endif
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            // ---- KT1 texten till args, tom i släppet, igen i upprepningen
            w.fält.focus();
            w.tangent(gfx::EventType::KeyDown, gfx::Key::A, 0, "å");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::A, 0, "");
            check(!w.tryck.empty() && w.tryck.back().text == "å",
                  "KT1 onKeyDown får tangentens text");
            w.tangent(gfx::EventType::KeyDown, gfx::Key::A, 0, "å");
            w.tangent(gfx::EventType::KeyDown, gfx::Key::A, 0, "å");
            check(w.tryck.size() == 3 && w.tryck.back().repeat &&
                      w.tryck.back().text == "å",
                  "KT1 upprepningen bär texten igen");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::A, 0, "");

            // ---- KT2 text vinner över genvägen i ett fält
            w.tangent(gfx::EventType::KeyDown, gfx::Key::M, 0, "m");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::M, 0, "");
            check(w.m == 0, "KT2 ett skrivet m i ett fält kör inte genvägen M");
            w.tangent(gfx::EventType::KeyDown, gfx::Key::M, 0, "");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::M, 0, "");
            check(w.m == 1, "KT2 samma tangent utan text kör genvägen");
            w.knapp.focus();
            w.tangent(gfx::EventType::KeyDown, gfx::Key::M, 0, "m");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::M, 0, "");
            check(w.m == 2, "KT2 med fokus på en knapp kör m genvägen");

            // ---- KT3 Ctrl+M utan text i ett fält kör
            w.fält.focus();
            w.tangent(gfx::EventType::KeyDown, gfx::Key::M, gfx::kModCtrl, "");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::M, gfx::kModCtrl, "");
            check(w.ctrlM == 1, "KT3 Ctrl+M utan text i ett fält körs");

            // ---- KT4 AltGr+2 (`@`) i ett fält kör ingen Ctrl+Alt+2
            w.tangent(gfx::EventType::KeyDown, gfx::Key::N2,
                      gfx::kModCtrl | gfx::kModAlt, "@");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::N2,
                      gfx::kModCtrl | gfx::kModAlt, "");
            check(w.altGr == 0, "KT4 AltGr+2 i ett fält skriver, ingen genväg");

            // ---- KT5 ett skrivskyddat fält tar inte text — genvägen körs
            w.låst.focus();
            w.tangent(gfx::EventType::KeyDown, gfx::Key::M, 0, "m");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::M, 0, "");
            check(w.m == 3, "KT5 skrivskyddat fält: bokstavsgenvägen körs");

            // ---- AW1 aktivt fönster
            w.enkel(gfx::EventType::WindowFocusGained);
            check(w.isActive() && glaze::app().activeWindow() == &w,
                  "AW1 WindowFocusGained ⇒ isActive och activeWindow()");
            w.enkel(gfx::EventType::WindowFocusLost);
            check(!w.isActive() && glaze::app().activeWindow() == nullptr,
                  "AW1 WindowFocusLost ⇒ inte aktivt, activeWindow() tomt");
            // ---- AW2 den fokuserade widgeten — talfältets inre fält
            w.fält.focus();
            check(w.focusedWidget() == &w.fält, "AW2 focusedWidget() efter focus()");
            w.tal.focus();
            check(w.focusedWidget() && w.focusedWidget() != &w.tal &&
                      w.focusedWidget()->wantsTextInput(),
                  "AW2 talfältet: dess inre fält har fokus och tar text");
            // ---- AW3 ägarens genvägar genom reserven
            verktyg.show(w);
            break;
        }
        case 1: {
            check(verktyg.isOpen(), "AW3 (fixturen) verktygsfönstret öppnades");
            const int före = w.m;
            gfx::Event e;
            e.type     = gfx::EventType::KeyDown;
            e.windowId = verktyg.window().id();
            e.key      = gfx::Key::M;
            verktyg.handleEvent(e);
            e.type = gfx::EventType::KeyUp;
            verktyg.handleEvent(e);
            check(w.m == före + 1,
                  "AW3 ägarens genväg körs genom acceleratorFallback → "
                  "tryShortcut");
            // ---- AW1 hide() släcker
            w.enkel(gfx::EventType::WindowFocusGained);
            w.hide();
            check(!w.isActive(), "AW1 hide() ⇒ inte aktivt");
            w.show();
        }
#ifdef GLAZE_BACKEND_D3D11
            // ---- KW1 WM_CHAR i kön + WM_KEYDOWN genom WndProc
            h = hwndFör(titel);
            if (!h) {
                hoppa("KW: fönstrets HWND hittades inte");
                w.close();
                return;
            }
            w.fält.setText("");
            w.fält.focus();
            w.tryck.clear();
            PostMessageW(h, WM_CHAR, 0x00E5, 0);   // å
            SendMessageW(h, WM_KEYDOWN, 'A', 1);
            SendMessageW(h, WM_KEYUP, 'A', 1);
            break;
        case 2:
            check(!w.tryck.empty() && w.tryck.back().text == "\xC3\xA5",
                  "KW1 WM_KEYDOWN hämtar sitt WM_CHAR: onKeyDown får \"å\"");
            check(w.fält.text() == "\xC3\xA5",
                  "KW1 …och fältet får tecknet EN gång", w.fält.text());
            // ---- KW2 surrogatparet
            w.fält.setText("");
            w.tryck.clear();
            PostMessageW(h, WM_CHAR, 0xD83D, 0);
            PostMessageW(h, WM_CHAR, 0xDE00, 0);
            SendMessageW(h, WM_KEYDOWN, 'A', 1);
            SendMessageW(h, WM_KEYUP, 'A', 1);
            break;
        case 3:
            check(!w.tryck.empty() && w.tryck.back().text == "\xF0\x9F\x98\x80",
                  "KW2 surrogatparet blir ett tecken i KeyDown");
            check(w.fält.text() == "\xF0\x9F\x98\x80", "KW2 …och i fältet",
                  w.fält.text());
            // ---- KW3 den döda tangenten: ´ följt av x ger två tecken
            w.fält.setText("");
            w.tryck.clear();
            PostMessageW(h, WM_CHAR, 0x00B4, 0);
            PostMessageW(h, WM_CHAR, 'x', 0);
            SendMessageW(h, WM_KEYDOWN, 'X', 1);
            SendMessageW(h, WM_KEYUP, 'X', 1);
            break;
        case 4:
            check(!w.tryck.empty() && w.tryck.back().text == "\xC2\xB4x",
                  "KW3 ´ + x: KeyDown bär \"´x\"");
            check(w.fält.text() == "\xC2\xB4x", "KW3 …och två tecken i fältet",
                  w.fält.text());
            // ---- KW4 Alt + tangent: WM_SYSKEYDOWN ger ingen text
            w.tryck.clear();
            SendMessageW(h, WM_SYSKEYDOWN, 'A', 1 | (1 << 29));
            SendMessageW(h, WM_SYSKEYUP, 'A', 1 | (1 << 29));
            break;
        case 5:
            check(!w.tryck.empty() && w.tryck.back().text.empty(),
                  "KW4 WM_SYSKEYDOWN ger ingen text");
#endif
            w.close();
            return;
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();
    if (!kordes)
        hoppa("KT/AW: inget fönster kunde skapas (ingen display)");
}

// ------------------------------------------------------------------- KP
//
// POPUPEN SOM TAR TANGENTBORDET — en APPS egen popup: ett vanligt
// `AppWindow` med `setPopup(true)` + `setPopupTakesKeys(true)` och ett
// sökfält, inga interna klasser. Ägaren är offscreen med ett EGET fält i
// fokus, och tangenterna matas genom ÄGARENS ingång — Windows/macOS/X11:s
// väg, där en noActivate-popup aldrig får OS-fokus.
struct Palett : glaze::AppWindow {
    glaze::TextInput sök;
    int              ner = 0, upp = 0;
    explicit Palett(const std::string& titel)
        : glaze::AppWindow(titel, 200, 40, gfx::Chrome::None)
    {
        setPopup(true);
        setPopupTakesKeys(true);
        sök.name = "sok";
        sök.dock = glaze::Dock::Fill;
        root().adopt(sök);
        sök.onKeyDown = [this] { ++ner; };
        sök.onKeyUp   = [this] { ++upp; };
    }
};

struct PalettVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    glaze::TextInput fält;
    int              krok = 0;   // tangenter och tecken `event()` såg
    int              ner = 0, upp = 0;
    explicit PalettVard(const std::string& titel)
        : glaze::AppWindow(titel, 400, 200, gfx::Chrome::None)
    {
        fält.name  = "falt";
        fält.x     = 10;
        fält.y     = 10;
        fält.width = 150;
        root().adopt(fält);
        fält.onKeyDown = [this] { ++ner; };
        fält.onKeyUp   = [this] { ++upp; };
    }
    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::KeyDown ||
            e.type == gfx::EventType::KeyUp ||
            e.type == gfx::EventType::TextInput)
            ++krok;
        return false;
    }
    void tangent(gfx::EventType t, gfx::Key k, const char* text = "")
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.key      = k;
        std::snprintf(e.text, sizeof e.text, "%s", text);
        handleEvent(e);
    }
    void skriv(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::snprintf(e.text, sizeof e.text, "%s", s);
        handleEvent(e);
    }
};

void fallPopupTangenter()
{
    PalettVard w(unikTitel("KP"));
    w.setOffscreen(true);
    Palett                palett(unikTitel("KP-palett"));
    bool                  kordes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
    const auto öppna = [&] {
        palett.setPopupAnchor(gfx::Rectf{0.0f, 0.0f, 10.0f, 10.0f});
        palett.show(w);
        palett.sök.focus();
    };
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            w.fält.focus();
            // ---- KP3 ett tryck i ägaren INNAN popupen öppnas släpps där
            w.tangent(gfx::EventType::KeyDown, gfx::Key::B, "b");
            öppna();
            check(palett.isOpen() && palett.sök.focused() && w.fält.focused(),
                  "KP (fixturen) popupen öppen med sökfältet i fokus, ägarens "
                  "fält kvar i fokus");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::B);
            check(w.upp == 1 && palett.upp == 0,
                  "KP3 släppet av ägarens tryck går till ägarens fält, inte "
                  "till popupen",
                  tal(w.upp) + " / " + tal(palett.upp));
            // ---- KP1 tryck, tecken och släpp genom ägarens ingång
            const int krokFöre = w.krok;
            w.tangent(gfx::EventType::KeyDown, gfx::Key::A, "a");
            w.skriv("a");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::A);
            check(palett.sök.text() == "a" && w.fält.text().empty(),
                  "KP1 tecknet hamnar i popupens fält, inte i ägarens",
                  palett.sök.text() + " / " + w.fält.text());
            check(palett.ner == 1 && palett.upp == 1 && w.ner == 1 &&
                      w.upp == 1,
                  "KP1 popupens fält får onKeyDown och onKeyUp, ägarens "
                  "ingetdera",
                  tal(palett.ner) + "/" + tal(palett.upp) + " mot " +
                      tal(w.ner) + "/" + tal(w.upp));
            check(w.krok == krokFöre,
                  "KP1 ägarens event()-krok ser inte popupens tangenter",
                  tal(w.krok - krokFöre));
            // ---- KP4 Escape som ingenting i popupen tog stänger den
            w.tangent(gfx::EventType::KeyDown, gfx::Key::Escape);
            break;
        }
        case 1:
            check(!palett.isOpen(), "KP4 Escape stänger popupen (svansen)");
            w.tangent(gfx::EventType::KeyUp, gfx::Key::Escape);
            check(w.upp == 1,
                  "KP4 släppet av Escape sväljs — ägarens fält får inget",
                  tal(w.upp));
            // ---- KP5 utan attributet stannar tangenterna hos ägaren
            palett.setPopupTakesKeys(false);
            öppna();
            w.skriv("c");
            check(w.fält.text() == "c" && palett.sök.text() == "a",
                  "KP5 utan setPopupTakesKeys: tecknet går till ägarens fält",
                  w.fält.text() + " / " + palett.sök.text());
            palett.close();
            break;
        case 2:
            check(!palett.isOpen(), "KP (fixturen) popupen stängd");
            w.close();
            return;
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();
    if (!kordes)
        hoppa("KP: inget fönster kunde skapas (ingen display)");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("input_gate\n");
    fallKeyArgs();
    fallHandlerFormer();
    fallMedFönster();
    fallTextOchFonster();
    fallPopupTangenter();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
