// geometry_gate — FÖNSTRETS PLATS, LÄGE OCH RAM.
//
// Kör:  ./build-test/tests/geometry_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett fönsterblock
//       kunde inte köras (ingen display) — ctest räknar den som SKIPPED,
//       aldrig grön
//
//   P  STARTPLATSEN, ren och headless: `src/window_placement.h` mot ett
//      HANDRÄKNADE värden i syntetiska skärmlayouter (värdena står vid varje fall
//      och är räknade för hand, inte ur implementationen).
//        Layout A — S0 primär (0,0,1920,1080), arbetsyta h 1040, skala 1;
//                   S1 (1920,0,2560,1440), arbetsyta h 1392, skala 1,5.
//          A1 CenterScreen 800×600 → (560, 220)
//          A2 CenterOwner, ägaren (2200,100,1200,900) på S1 → (2200, 100)
//          A3 Manual L(5000,3000) → (3280, 492) — närmaste skärm, klämd
//          A4 Manual L(−300,500) → (0, 440)
//          A5 CenterScreen 2400×1300 → (0, 0) — större än arbetsytan
//        Layout B — S0 primär (0,0,2560,1440), arbetsyta h 1392, skala 1,5;
//                   S1 (−1920,0,1920,1080), arbetsyta h 1040, skala 1.
//          toLogical(−1920, 0) = (−1280, 0)
//          B1 Manual L(−1200,100) → P(−1800, 150), orörd
//          B2 CenterScreen 800×600 → (680, 246)
//        Layout C — en skärm, arbetsyta (0,40,1920,1040): Manual (100,0) →
//                   (100, 40).
//        s₀ = 1,25: P(1000,500) ↔ L(800,400), och ett svep 0..3000 åt båda
//        hållen tål en avrundning inom ±1. Tom lista och `Default` ⇒ nullopt.
//   W  MED FÖNSTER, genom den riktiga backenden (D3D11 i det här passet;
//      Linux i ett eget):
//        W1 offscreen + `setLocation` inne i primärens arbetsyta +
//           `setWindowState(Maximized)` ⇒ `geometry()` x/y ±1 och
//           `hasPosition`, storleken = den begärda (på Linux är det PROXYNS
//           vidarebefordran av `placement()`), läget Normal — offscreen får
//           aldrig startläget; D3D11: OS-fönstret är varken synligt eller
//           zoomat. Ett fönster som aldrig visats svarar med en tom
//           `placement()`.
//        W2 `setLocation` efter öppning ⇒ `geometry()` följer (spårningen).
//        W3 (bara Windows, VISAR ett riktigt fönster ett ögonblick)
//           `setWindowState(Maximized)` före `run` ⇒ `windowState()` är
//           Maximized, `geometry()` bär NORMALrutan (den begärda storleken
//           och platsen, inte den maximerade ramen) — och efter `restore()`
//           Normal på samma plats.
//        W4 (bara Windows, visar ett fönster) normalt öppnat, sedan
//           `maximize()` och stängt ⇒ kortet: Maximized med normalstorleken;
//           öppnat igen ⇒ `clientSize()` är normalstorleken.
//        W5 `setChrome(None)` före öppning ⇒ `topBar() == nullptr` (D3D11:
//           WS_POPUP); ett fönster konstruerat med None och sedan
//           `setChrome(Custom)` ⇒ namnlisten byggs LATT, FÖRST i roten, och
//           programikonen står sist — trädet ett fönster konstruerat med
//           Custom har.
//        W6 CenterOwner: a) en DOLD ägare (offscreen) ⇒
//           barnet centreras på PRIMÄRSKÄRMEN · b) (bara Windows, visar
//           ett fönster) en SYNLIG ägare ⇒ barnets mitt = ägarens mitt ±1.
//           Båda fönstren ramlösa: deras ytterkant är klientytan.
//   L  `onLoad` FÖRE SYNLIGHETEN — riktiga
//      (inte offscreen) fönster, som tray_gate H; OS-oraklet med D3D11:
//        L1 `setLocation` i onLoad ⇒ fönstret är DOLT under onLoad
//           (`isHidden`; D3D11 `IsWindowVisible` falsk), syns efteråt DÄR
//           (`geometry()` ±1) och onShow kom EN gång; L1b en plats långt
//           utanför arbetsytan klämdes in i den.
//        L2 `setWindowState(Maximized)` i onLoad ⇒ läsarna svarar med begäran
//           under handlern; D3D11: dolt under onLoad och ZOOMAT när det
//           syns.
//        L3 `setContentSize` + `CenterScreen` i onLoad ⇒ centrerat med den NYA
//           storleken (ramlöst).
//        L4 `close()` i onLoad ⇒ stängt utan att ha synts, ingen onShow.
//        L4b `close()` i onLoad som onClose VETAR ⇒ öppet, SYNLIGT och
//           onShow EN gång (den första visningen fortsätter efter vetot);
//           L4c `hide()` i vetots handler ⇒ öppet och dolt, ingen onShow.
//        L5 `hide()` i onLoad ⇒ öppet och dolt, ingen onShow; `show()` visar.
//        L6 dold start + `maximize()` före `show()` ⇒ fortfarande dolt med
//           läget Maximized; D3D11: osynligt, och zoomat när det visas.
//        L7 `showInactive` vid den första visningen är dock_gate LV10 (en
//           panel som lossnar stjäl inte förgrunden) och capture_gate D7 —
//           båda går den uppskjutna visningens väg.
//        L8 (D3D11, villkorat på förgrunden) ett ägt fönster som onLoad
//           öppnar med `show(ägare)` är förgrunden efter ägarens första
//           visning (`loadActivated_`).
//   KM STORLEKSMARGINALENS KANTER, ren och headless:
//      `frame::marginRole` (Glaze/gfx/src/frame_margin.h — Linux pil och tryck
//      i marginalen) mot ett HANDRÄKNADE värden: klientytan 100 × 80, marginalen
//      8, hörnet 16 — mitten Client; (−3, 40) Left, (103, 40) Right, (50, −3)
//      Top, (50, 83) Bottom; (−3, −3), (−3, 10) och (10, −3) TopLeft;
//      (−3, 20) Left (utanför hörnets 16); (90, −3) TopRight; (103, 70)
//      BottomRight; (5, 83) BottomLeft; (100, 79) BottomRight; (−9, 40) och
//      (108, 40) Client (bortom ytan); (0, 0) och (99,5, 79,5) Client
//      (klientytans kantpixlar); m = 0 ⇒ Client. Ett 20 × 20-fönster kapar
//      hörnet till 10: (15, −3) TopRight och (5, −3) TopLeft. `isResizeRole`
//      sant för de åtta och falskt för Client, Caption och knapparna.
//   K  STORLEKSRAMEN UTANFÖR DEN SYNLIGA KANTEN (
//      bara D3D11, VISAR riktiga fönster ett ögonblick, W3:s mönster;
//      träffpunkterna skickas som WM_NCHITTEST i skärmens px):
//        K1 ett ändringsbart Custom-fönster: `placement()` x/y ==
//           `clientToScreen(0, 0)`, och `GetWindowRect` ligger utanför
//           klientytan till vänster, höger och nedåt men inte uppåt (ramen
//           finns, överkantens icke-klientyta är borta); klientytan är den
//           begärda storleken × fönstrets skala (±1).
//        K2 innanför vänster-, höger- och nederkanten (2 px in) HTCLIENT —
//           buggens kärna; utanför (3 px ut) HTLEFT, HTRIGHT, HTBOTTOM och
//           hörnet HTBOTTOMRIGHT; 2 px ned i klientytan HTTOP; under bandet
//           (nederramens tjocklek + 4 px) i namnlistens mitt HTCAPTION.
//        K3 ett FAST fönster (min == max): `GetWindowRect` == klientytan,
//           klientytan den begärda, 2 px in vid vänsterkanten HTCLIENT och
//           2 px ned INTE HTTOP.
//        K4 maximerat: 2 px ned INTE HTTOP, och WM_GETMINMAXINFO
//           lägger NORMALLÄGETS ram (mätt i normalläge före maximeringen)
//           på minsta storleken — inte den maximerade insetten; restore() ⇒
//           klientytan är den begärda igen.
//        K5 `setLocation` efter öppningen: `clientToScreen(0, 0)` ==
//           `toPlacement` ±1, och `placement()` lika.
//        K6 fast ↔ ändringsbart i ett ÖPPET fönster
//           (`applyResizeStyle`): klientytans plats och storlek står still åt
//           båda hållen, och ramen försvinner och kommer tillbaka.
//        K7 Chrome::None: ingen ram — `GetWindowRect` == klientytan,
//           klientytan den begärda, 2 px in vid vänsterkanten HTCLIENT.
//        K8 transparent: ingen ram, och efter en ritad frame släpper
//           mitten (ingenting ritat där) klicket — HTTRANSPARENT (alfa-
//           hit-testen står kvar). Degraderat till opakt ⇒ "..".
//        K9 popup med ankare: ingen ram, den begärda storleken och
//           klientytans hörn under ankaret — ägarens
//           `clientToScreen(a.x, a.y + a.h)` ±1.
//        K10 DWM:s SYNLIGA kant (`DWMWA_EXTENDED_FRAME_BOUNDS`) för K1:s
//           fönster ligger högst DWM-kanten (2 px, Win11:s
//           `DWMWA_VISIBLE_FRAME_BORDER_THICKNESS` när den är större) utanför
//           klientytan, och innanför `GetWindowRect`: ramen är OSYNLIG.
//   KL (Linux, X11 — Wayland och headless saknar skärmrymd) ett riktigt
//      (mappat) Custom-fönster: `placement()` x/y == `clientToScreen(0, 0)`
//      före och efter `setPosition`, och efter flytten står klientens hörn på
//      den begärda punkten — identiteten med storleksmarginalen. Varje
//      läsning väntar på WM:ens svar (`kSvarMs`).
//   SC FÖNSTRET FÖLJER INNEHÅLLET — offscreen och
//      ramlöst: SC1 `Height` med Top-raderna 30, 40, 50 och en Bottom-rad 36
//      ⇒ innehållets höjd 156 och begärans bredd 300; SC2 en dold rad ⇒ 116
//      efter svansen (postat), visad ⇒ 156; SC4 `setText` utan
//      synlighetsändring ⇒ ritningens fråga anpassar; SC9 fem ritningar och
//      svansar efter anpassningen ⇒ ingen `root().onResize`; SC8
//      `setContentSize(400, 300)` ⇒ bredden 400, höjden innehållets; SC5
//      `WidthAndHeight` ⇒ `isFixedSize()`, och `minimumSize()`/
//      `maximumSize()` svarar med appens begäran; SC6 `Manual` ⇒ en dold rad
//      ändrar inte storleken.
//   TW PUNKTEN I FÖNSTRET: TW1 `toWindow({5, 6})` för en widget på
//      (30, 40) i en behållare på (10, 20) under Top-raderna = innehållsytans
//      hörn + kedjan × fönstrets skala, och `fromWindow` är inversen; TW2
//      `toWindow(e.local)` i `onMouseDown` är tryckets position; TW3 utan
//      öppet fönster och efter stängningen ⇒ tomt.
//   SB RADER UPPIFRÅN OCH MOT NEDERKANTEN — offscreen och
//      ramlöst, `Height`: `a` 100 × 30 på (10, 10), `b` 100 × 40 syskonankrad
//      6 under, `ok` 60 × 26 nere till höger 10/10. SB1 ⇒ 86 + 36 = 122;
//      SB2 `b.bottom = 12` (luft) + `invalidate()` ⇒ 134 efter ritningens
//      fråga; SB3 `a.hide()` ⇒ 134 (hålet); SB4 `a.show()` + `WidthAndHeight`
//      ⇒ bredden 110 (raderna står över varandra — max, inte summa).
//   SG INNEHÅLLET SOM GOLV — `FoljFonster` (innehållet 156) med `Height` +
//      `GrowOnly`: SG1 konstruktorns 500 står kvar och fönstret är inte fast;
//      SG2 `setClientSize(300, 100)` ⇒ 156; SG3 `(300, 400)` ⇒ 400; SG4
//      `rad[1].hide()` ⇒ 400; SG5 `(300, 120)` ⇒ 120; SG6 `rad[1].show()` ⇒
//      156; SG7 `setMaximumSize(0, 100)` + `(300, 400)` ⇒ 156 och
//      `maximumSize().h` 100; SG8 utan max `(300, 400)` ⇒ 400, sedan
//      `setSizeToContentMode(GrowAndShrink)` ⇒ 156 och inte fast.
//   CM GRÄNSERNA I INNEHÅLLSMÅTT — offscreen och ramlöst, chromet
//      är statusraden (höjden ur stilen, här): CM1 före show utan raden ⇒
//      klientgränsen = innehållsgränsen, `statusBar.show()` ⇒ klientgränsen
//      + raden och innehållsgränsen orörd; CM2 `setFixedContentSize(260,
//      120)` ⇒ fast, innehållsytan 260 × 120 och klientytan + raden; CM3
//      raden dold ⇒ innehållsytan orörd, klientytan 120, fortfarande fast;
//      CM4 `(0, 50)` och ett negativt mått ignoreras; CM5 `(0, 0)` låser upp;
//      CM6 `setMinimumSize` efter innehållsformen ⇒ klientmått, i
//      innehållsmått minus raden.
//   BG BREDDGRUPPENS OMRITNING — två paneler med var sin etikett i
//      gruppen "g": BG1 lika breda, `A.setText(längre)` stegar B:s
//      `damageStamp()` och efter bildrutan är de lika breda igen (och
//      bredare); BG2 `A.hide()` stegar B:s stämpel och B blir smalare.
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · window_placement.h: Manual räknad med MÅLSKÄRMENS skala ........... B1
//   · window_placement.h: klämning mot `bounds` i stället för `work` ..... C
//   · window_placement.h: storleken × PRIMÄRENS skala (inte målskärmens) . A2
//   · window_placement.h: `screenFor` utan närmaste-regeln ............... A3
//   · d3d11_window.cpp: `trackNormalPosition` läser GetWindowRect också
//     när fönstret är zoomat ............................................ W3
//   · d3d11_window.cpp: `trackNormalPosition` bort ur WM_MOVE ............ W2
//   · app.cpp executeClose: kortet tas inte (`closedGeometry_`) .......... W4
//   · app.cpp executeClose: `initW_/initH_` inte återställda ............. W4
//   · app_window.cpp applyChrome: namnlisten inte flyttad FÖRST .......... W5
//   · app_window.cpp: `topBar()` svarar `topBar_` oavsett ram ............ W5
//   · app_window.cpp showOwned: ägarrektangeln tagen också för en dold
//     ägare ............................................................. W6a
//   · app_window.cpp showOwned: `startPlacement` bort (OS:et väljer) ..... W1
//   · linux_platform.cpp: proxyns `placement()` utan vidarebefordran
//     (bara Linux) ...................................................... W1
//   · frame_margin.h: hörnet utan kapningen till halva sidan ............. KM
//   · frame_margin.h: ingen gräns bortom marginalen (−9, 40 blir Left) ... KM
//   · d3d11_window.cpp: WM_NCCALCSIZE svarar 0 (ingen ram) och kartans
//     resize-roller används ........................................ K1, K2
//   · d3d11_window.cpp: WM_NCHITTEST utan DefWindowProc för punkter
//     utanför klientytan ................................................ K2
//   · d3d11_window.cpp: HTTOP-bandet bort ................................ K2
//   · d3d11_window.cpp: `setPosition` utan kompensation ............. K5, W2
//   · d3d11_window.cpp: `trackNormalPosition` utan kompensation ...... K1, W2
//   · d3d11_window.cpp: ramen också på ett fast fönster (`normalFrame`
//     utan `fixedSize()`) ............................................... K3
//   · d3d11_window.cpp: `setSize` utan ramen (`windowRectFor` utan kompensation) . K1
//   · d3d11_window.cpp: WM_GETMINMAXINFO utan ramen, eller med
//     `frameInsets()` i stället för normalramen ......................... K4
//   · d3d11_window.cpp: `applyResizeStyle` med SWP_NOMOVE | SWP_NOSIZE
//     (klientytan följer inte med) ............................ K6
//   · d3d11_window.cpp: popup-grenen i init borttagen (popupen faller till
//     setSize-vägen och hamnar inte under ankaret) ...................... K9
//   · x11_window.cpp: `clientToScreen`, `setPosition` eller
//     `trackNormalPosition` utan marginalen (bara Linux, och bara där
//     marginalen finns i X-sessionen) ................................... KL
//   · app_window.cpp showOwned: `desc.hidden` utan `skjutUpp` (fönstret
//     visas i init, före onLoad) .................................. L1, L2
//   · app_window.cpp revealHidden: startplatsen räknas inte om ........... L3
//   · app_window.cpp showOwned: `revealHidden` också när `pendingClose_` . L4
//   · app.cpp pumpUntilClosed: sovvillkoret utan den köade stängningen
//     (Linux: ett dolt fönster ger inga händelser — L4 hänger till taket)  L4
//   · x11_window.cpp onConfigure: `trackNormalPosition` också före den
//     första mappningen (bara under en XWM som inte flyttar ett omappat
//     fönster, som WSLg:s Weston) ........................................ W2
//   · app_window.cpp hide(): ingen gren för `loading_` (hide() i onLoad
//     ignoreras — fönstret har inte synts) ............................. L5
//   · d3d11_window.cpp: `maximize()` utan `firstShown_`-grenen (visar
//     ett fönster som aldrig visats) .................................... L6
//   · app_window.cpp: `setLocation` utan `firstShowPending_` (flyttar
//     OS-fönstret i stället för begäran — klämningen uteblir) ......... L1b
//   · app.cpp flushPendingCloses: ingen `revealHidden` efter vetot ...... L4b
//   · app_window.cpp hide(): pending-grenen bara för `loading_` igen ... L4c
//   · app_window.cpp revealHidden: barnets `raise()` bort, eller
//     `noteLoadActivation` som inte minns barnet ......................... L8
//   · app_window.cpp contentInsetsChanged: ingen anpassning vid
//     `setVisible` (bara `applyContentSize` som i Manual) .............. SC2
//   · app_window.cpp fitDue: ritningens fråga borttagen .................. SC4
//   · app_window.cpp refit: `fitWanted_` skrivs EFTER `setSize` .......... SC9
//   · app_window.cpp refit: den fria bredden ur `initW_` i stället för
//     begäran (en ny `setContentSize`) .................................. SC8
//   · app_window.h isFixedSize: läser `minW_` … i stället för de
//     effektiva gränserna ............................................... SC5
//   · widget.cpp toWindow: utan skalan ................................... TW1
//   · widget.cpp fromWindow: inte inversen (fel tecken på origo) ......... TW1
//   · widget.cpp extentOf: dolda hoppas över före kanterna ............... SB3
//   · app_window.cpp effMin*/effMax*: låser oavsett läge ............ SG1, SG3
//   · app_window.cpp refit: GrowOnly sätter innehållet, inte max ......... SG1
//   · app_window.cpp refit: GrowOnly krymper till innehållet ............. SG4
//   · app_window.cpp effMaxH: appens max vinner över innehållet .......... SG7
//   · app_window.cpp appMinH/appMaxH: utan chromets höjd ................. CM1
//   · app_window.cpp applyContentSize: gränserna inte FÖRE storleken (där
//     OS:et klämmer en programmatisk storlek till den gamla gränsen —
//     D3D11:s WM_WINDOWPOSCHANGING) ..................................... CM3
//   · app_window.cpp setFixedContentSize: bara `setContentSize` .......... CM2
//   · app_window.cpp setMinimumSize: `minInContent_` inte nollad ......... CM6
//   · label.h setText: bara `invalidate()` (inte `sizeMayChange`) ........ BG1
//   · label.cpp: ingen `shownChanged` ................................... BG2
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · SC: OS-pekaren på en låst kant, WM_GETMINMAXINFO för den låsta höjden
//     (SC10 är inte skriven), en radbruten etikett som bryts om när
//     dialogen görs bredare (handen — window_lab), Wayland/X11-WM:ens svar
//     och att dialogen syns växa för ögat.
//   · SG: OS:ets gräns vid ett RIKTIGT drag — fasaden har ingen getter för den
//     tillämpade gränsen (handen, doc/os/*/TESTS.md), Wayland/X11-WM:ens svar
//     och höjden vid den faktiska bredden i `WidthAndHeight` + `GrowOnly`.
//     BG: att etikettkolumnen syns för ögat (demons formulär).
//   · CM: OS:ets TILLÄMPADE gräns (fasaden har ingen getter — CM3 ser den
//     bara där OS:et klämmer en programmatisk storlek), resize-kanten på det
//     låsta fönstret med musen, namnlistens och menyradens bidrag (samma
//     `chromeInsetH`, mätt i preview_gate med namnlisten) och WM:ens svar.
//   · att offscreen-fönstret ALDRIG får startläget i backenden: D3D11
//     tillämpar startläget först vid den första synligheten, och ett
//     offscreen-fönster blir aldrig synligt — ett sabotage som skickade
//     startläget till ett offscreen-fönster syns alltså inte här (W1 mäter
//     bara att det inte visas och inte zoomas).
//   · riktiga fönsterhanterare: X11:s gravitation och USPosition, Wayland-
//     kompositörer, Aero Snap (en snappad yta räknas som normalgeometri) och
//     STARTUPINFO-heuristiken för en genväg i Utforskaren (W3 startas från en
//     grind) — doc/os/*/TESTS.md.
//   · riktiga skärmar med olika skala: P använder syntetiska layouter, W den
//     maskin grinden råkar köras på.
//   · macOS över huvud taget, och Linux i det här passet.
//   · L: att den första SYNLIGA bilden var den slutliga (inget blink för
//     ögat), en maximerad start under X11/Wayland (fönsterhanterarens
//     asynkrona svar), Waylands första visning utan en ny configure-runda
//     (doc/os/linux/TESTS.md) och Metal (doc/os/macos/TESTS.md).
//   · STORLEKSRAMEN (K): en riktig storleksändring med musen (WM_NCHITTEST
//     skickas, ingen modal OS-loop körs), DWM:s utseende — Windows 10:s 1 px-
//     kant mot vår ram (den möjliga dubbla linjen), Win11:s rundade hörn
//     (DWMWA_WINDOW_CORNER_PREFERENCE går inte att läsa tillbaka) och
//     SKUGGAN (inget API svarar med den; K10 mäter den synliga kanten, den
//     del av DWM:s ritning som går att läsa) — Aero Snap, ramens tjocklek vid
//     andra DPI än maskinens och blandad DPI (WM_DPICHANGED). Inget av
//     sabotagen i listan fäller K10: den vaktar att ramen förblir osynlig om
//     någon senare ändrar stilen eller WM_NCCALCSIZE.
//   · LINUX-MARGINALEN i drift: KM mäter bara kantvalet. Ytan (klient
//     + 2m), `set_window_geometry`, `_GTK_FRAME_EXTENTS`, compositens
//     förskjutning, indata −m och marginalens av- och påslag vid maximering
//     når ingen grind härifrån — och om X-sessionen har marginalen alls
//     (kompositör + WM-stöd) syns inte genom fasaden, så KL kan vara trivialt
//     grön. doc/os/linux/TESTS.md.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/platform.h>
#include <glaze/glaze.h>

#include "frame_margin.h"       // intern — Glaze/gfx/src (avsnitt KM)
#include "window_placement.h"   // intern — Glaze/widgets/src

#ifdef GLAZE_BACKEND_D3D11
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>   // K10: DWM:s synliga kant
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

void hoppa(const char* vad)
{
    std::printf("  --   %s hoppas över: inget fönster kunde skapas (ingen "
                "display)\n",
                vad);
    ++g_skips;
}

void inteHär(const char* vad)
{
    std::printf("  ..   %s\n", vad);
}

std::string tal(double v)
{
    char b[48];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

std::string pt(const std::optional<gfx::Pointf>& p)
{
    return p ? "(" + tal(p->x) + ", " + tal(p->y) + ")" : "(nullopt)";
}

bool nära(float a, float b, float e = 0.5f) { return std::fabs(a - b) <= e; }

bool är(const std::optional<gfx::Pointf>& p, float x, float y)
{
    return p && nära(p->x, x) && nära(p->y, y);
}

gfx::ScreenInfo skärm(gfx::Rectf bounds, gfx::Rectf work, float skala,
                      bool primär)
{
    gfx::ScreenInfo s;
    s.bounds  = bounds;
    s.work    = work;
    s.scale   = skala;
    s.primary = primär;
    return s;
}

glaze::detail::StartRequest begäran(glaze::StartPosition pos, float lx,
                                    float ly, float w, float h)
{
    glaze::detail::StartRequest r;
    r.pos         = pos;
    r.logical     = gfx::Pointf{lx, ly};
    r.logicalSize = gfx::Sizef{w, h};
    return r;
}

// =================================================== P STARTPLATSEN (ren)

void delP()
{
    std::printf("\n=== P. startplatsen, headless ===\n");
    using glaze::StartPosition;
    using glaze::detail::startPlacement;
    using glaze::detail::toLogical;
    using glaze::detail::toPlacement;

    // ---- Layout A
    const std::vector<gfx::ScreenInfo> A = {
        skärm({0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0f, true),
        skärm({1920, 0, 2560, 1440}, {1920, 0, 2560, 1392}, 1.5f, false),
    };
    {
        const auto p = startPlacement(
            begäran(StartPosition::CenterScreen, 0, 0, 800, 600), A);
        check(är(p, 560, 220), "A1 CenterScreen 800×600 → (560, 220)", pt(p));
    }
    {
        auto r      = begäran(StartPosition::CenterOwner, 0, 0, 800, 600);
        r.ownerRect = gfx::Rectf{2200, 100, 1200, 900};
        const auto p = startPlacement(r, A);
        check(är(p, 2200, 100),
              "A2 CenterOwner över ägaren på S1 (storleken × 1,5) → (2200, 100)",
              pt(p));
    }
    {
        const auto p = startPlacement(
            begäran(StartPosition::Manual, 5000, 3000, 800, 600), A);
        check(är(p, 3280, 492),
              "A3 Manual L(5000,3000) → närmaste skärmen S1, klämd → "
              "(3280, 492)",
              pt(p));
    }
    {
        const auto p = startPlacement(
            begäran(StartPosition::Manual, -300, 500, 800, 600), A);
        check(är(p, 0, 440), "A4 Manual L(−300,500) → (0, 440)", pt(p));
    }
    {
        const auto p = startPlacement(
            begäran(StartPosition::CenterScreen, 0, 0, 2400, 1300), A);
        check(är(p, 0, 0),
              "A5 CenterScreen 2400×1300 (större än arbetsytan) → (0, 0)",
              pt(p));
    }

    // ---- Layout B
    const std::vector<gfx::ScreenInfo> B = {
        skärm({0, 0, 2560, 1440}, {0, 0, 2560, 1392}, 1.5f, true),
        skärm({-1920, 0, 1920, 1080}, {-1920, 0, 1920, 1040}, 1.0f, false),
    };
    {
        const gfx::Pointf l =
            toLogical(gfx::Pointf{-1920, 0}, glaze::detail::primaryScale(B));
        check(nära(l.x, -1280) && nära(l.y, 0),
              "B toLogical(−1920, 0) med primärens skala 1,5 = (−1280, 0)",
              pt(l));
    }
    {
        const auto p = startPlacement(
            begäran(StartPosition::Manual, -1200, 100, 800, 600), B);
        check(är(p, -1800, 150),
              "B1 Manual L(−1200,100) → P(−1800, 150) på S1, orörd", pt(p));
    }
    {
        const auto p = startPlacement(
            begäran(StartPosition::CenterScreen, 0, 0, 800, 600), B);
        check(är(p, 680, 246), "B2 CenterScreen 800×600 → (680, 246)", pt(p));
    }

    // ---- Layout C
    {
        const std::vector<gfx::ScreenInfo> C = {
            skärm({0, 0, 1920, 1080}, {0, 40, 1920, 1040}, 1.0f, true)};
        const auto p = startPlacement(
            begäran(StartPosition::Manual, 100, 0, 800, 600), C);
        check(är(p, 100, 40),
              "C Manual (100, 0) klämd mot ARBETSYTAN → (100, 40)", pt(p));
    }

    // ---- s₀ = 1,25 och svepet
    {
        const gfx::Pointf l = toLogical(gfx::Pointf{1000, 500}, 1.25f);
        const gfx::Pointf p = toPlacement(gfx::Pointf{800, 400}, 1.25f);
        check(nära(l.x, 800) && nära(l.y, 400) && nära(p.x, 1000) &&
                  nära(p.y, 500),
              "s₀ 1,25: P(1000,500) ↔ L(800,400)");
        int värst = 0;
        for (int v = 0; v <= 3000; ++v) {
            // placering → logisk (avrundad, som `geometry()`) → placering
            const long l1 = std::lround(
                toLogical(gfx::Pointf{float(v), 0}, 1.25f).x);
            const long p1 = std::lround(
                toPlacement(gfx::Pointf{float(l1), 0}, 1.25f).x);
            // logisk → placering (avrundad, som `posX`) → logisk
            const long p2 = std::lround(
                toPlacement(gfx::Pointf{float(v), 0}, 1.25f).x);
            const long l2 = std::lround(
                toLogical(gfx::Pointf{float(p2), 0}, 1.25f).x);
            värst = std::max(värst, static_cast<int>(std::labs(p1 - v)));
            värst = std::max(värst, static_cast<int>(std::labs(l2 - v)));
        }
        check(värst <= 1, "s₀ 1,25: svepet 0..3000 åt båda hållen inom ±1",
              tal(värst) + " px");
    }

    // ---- nullopt
    {
        const auto tom = startPlacement(
            begäran(StartPosition::CenterScreen, 0, 0, 800, 600), {});
        const auto förval = startPlacement(
            begäran(StartPosition::Default, 0, 0, 800, 600), A);
        check(!tom && !förval,
              "tom skärmlista (Wayland) och Default ⇒ nullopt (OS:et väljer)");
    }
}

// ======================================= KM STORLEKSMARGINALENS KANTER (ren)

const char* rollNamn(gfx::HitRole r)
{
    using R = gfx::HitRole;
    switch (r) {
    case R::Client:            return "Client";
    case R::Caption:           return "Caption";
    case R::MinButton:         return "MinButton";
    case R::MaxButton:         return "MaxButton";
    case R::CloseButton:       return "CloseButton";
    case R::ResizeLeft:        return "ResizeLeft";
    case R::ResizeRight:       return "ResizeRight";
    case R::ResizeTop:         return "ResizeTop";
    case R::ResizeBottom:      return "ResizeBottom";
    case R::ResizeTopLeft:     return "ResizeTopLeft";
    case R::ResizeTopRight:    return "ResizeTopRight";
    case R::ResizeBottomLeft:  return "ResizeBottomLeft";
    case R::ResizeBottomRight: return "ResizeBottomRight";
    }
    return "?";
}

void delKM()
{
    std::printf("\n=== KM. storleksmarginalens kanter, headless (E2L) ===\n");
    using R = gfx::HitRole;
    using gfx::frame::marginRole;
    // Räknat för hand: klientytan 100 × 80, marginalen 8 runt om,
    // hörnets räckvidd 16 px längs kanten från klientens hörn.
    struct Fall {
        float       x, y;
        R           väntat;
        const char* vad;
    };
    const Fall fall[] = {
        {50, 40, R::Client, "KM mitten ⇒ Client (kartan avgör)"},
        {-3, 40, R::ResizeLeft, "KM (−3, 40) ⇒ Left"},
        {103, 40, R::ResizeRight, "KM (103, 40) ⇒ Right"},
        {50, -3, R::ResizeTop, "KM (50, −3) ⇒ Top"},
        {50, 83, R::ResizeBottom, "KM (50, 83) ⇒ Bottom"},
        {-3, -3, R::ResizeTopLeft, "KM (−3, −3) ⇒ TopLeft"},
        {-3, 10, R::ResizeTopLeft, "KM (−3, 10) ⇒ TopLeft (hörnet längs kanten)"},
        {10, -3, R::ResizeTopLeft, "KM (10, −3) ⇒ TopLeft"},
        {-3, 20, R::ResizeLeft, "KM (−3, 20) ⇒ Left (utanför hörnets 16)"},
        {90, -3, R::ResizeTopRight, "KM (90, −3) ⇒ TopRight"},
        {103, 70, R::ResizeBottomRight, "KM (103, 70) ⇒ BottomRight"},
        {5, 83, R::ResizeBottomLeft, "KM (5, 83) ⇒ BottomLeft"},
        {100, 79, R::ResizeBottomRight, "KM (100, 79) ⇒ BottomRight"},
        {-9, 40, R::Client, "KM (−9, 40) bortom marginalen ⇒ Client"},
        {108, 40, R::Client, "KM (108, 40) bortom marginalen ⇒ Client"},
        {0, 0, R::Client, "KM (0, 0) klientytans hörnpixel ⇒ Client"},
        {99.5f, 79.5f, R::Client, "KM (99,5, 79,5) klientytans sista pixel ⇒ Client"},
    };
    for (const Fall& f : fall) {
        const R fick = marginRole(f.x, f.y, 100.0f, 80.0f, 8.0f, 16.0f);
        check(fick == f.väntat, f.vad, rollNamn(fick));
    }
    {
        const R fick = marginRole(-3.0f, 40.0f, 100.0f, 80.0f, 0.0f, 16.0f);
        check(fick == R::Client, "KM marginalen 0 ⇒ Client (ingen marginal)",
              rollNamn(fick));
    }
    {
        // 20 × 20: hörnet kapas till halva sidan (10), så två hörn aldrig
        // överlappar.
        const R h = marginRole(15.0f, -3.0f, 20.0f, 20.0f, 8.0f, 16.0f);
        const R v = marginRole(5.0f, -3.0f, 20.0f, 20.0f, 8.0f, 16.0f);
        check(h == R::ResizeTopRight && v == R::ResizeTopLeft,
              "KM 20 × 20: hörnet kapat till 10 — (15, −3) TopRight, (5, −3) "
              "TopLeft",
              std::string(rollNamn(h)) + ", " + rollNamn(v));
    }
    {
        bool rätt = true;
        for (R r : {R::ResizeLeft, R::ResizeRight, R::ResizeTop,
                    R::ResizeBottom, R::ResizeTopLeft, R::ResizeTopRight,
                    R::ResizeBottomLeft, R::ResizeBottomRight})
            rätt = rätt && gfx::frame::isResizeRole(r);
        for (R r : {R::Client, R::Caption, R::MinButton, R::MaxButton,
                    R::CloseButton})
            rätt = rätt && !gfx::frame::isResizeRole(r);
        check(rätt, "KM isResizeRole: de åtta resize-rollerna och inga andra");
    }
}

// ======================================================= W MED FÖNSTER

#ifdef GLAZE_BACKEND_D3D11
HWND hwndFör(const std::string& titel)
{
    const std::wstring w(titel.begin(), titel.end());   // titlarna är ASCII
    return FindWindowW(nullptr, w.c_str());
}

// som i tray_gate: startades processen med SW_HIDE byter Windows ut
// den första ShowWindow mot SW_HIDE — förbrukas på ett eget fönster.
void förbrukaStartvisningen()
{
    STARTUPINFOW si{};
    si.cb = sizeof si;
    GetStartupInfoW(&si);
    if (!(si.dwFlags & STARTF_USESHOWWINDOW) || si.wShowWindow != SW_HIDE)
        return;
    inteHär("processen startades med SW_HIDE — den första ShowWindow-regeln "
            "förbrukas på ett eget fönster");
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

std::string unikTitel(const char* del)
{
#ifdef GLAZE_BACKEND_D3D11
    return std::string("geometry_gate-") + del + "-" +
           std::to_string(GetCurrentProcessId());
#else
    return std::string("geometry_gate-") + del;
#endif
}

// som i tray_gate: kör `steg` i fönstrets EGEN pump.
template <class F>
bool iPumpen(glaze::AppWindow& w, F steg)
{
    auto vakt   = std::make_shared<bool>(true);
    bool kördes = false;
    glaze::app().post([vakt, &kördes, steg] {
        if (!*vakt)
            return;
        kördes = true;
        steg();
    });
    w.run();
    *vakt = false;
    return kördes;
}

// X11 SVARAR ASYNKRONT på en mappning och en flytt: platsen kommer med en
// ConfigureNotify efter anropet, och Westons XWM (WSLg) parkerar dessutom ett
// nymappat fönster utanför skärmen (−32768) tills det placerats. På Linux körs
// `steg` därför efter en tick (W2:s mönster) — läst direkt hade ett mellanläge
// räknats som platsen. D3D11 svarar synkront: där körs det direkt.
#ifdef GLAZE_BACKEND_LINUX
constexpr int kSvarMs = 150;
#endif

template <class F>
bool iPumpenEfterSvaret(glaze::AppWindow& w, F steg)
{
#ifdef GLAZE_BACKEND_LINUX
    glaze::Timer t;
    t.setInterval(kSvarMs);
    t.setSingleShot(true);
    t.onTick = [&steg] { steg(); };
    return iPumpen(w, [&t] { t.start(); });
#else
    return iPumpen(w, steg);
#endif
}

std::string geo(const glaze::WindowGeometry& g)
{
    const char* läge = g.state == gfx::WindowState::Maximized   ? "Maximized"
                       : g.state == gfx::WindowState::Minimized ? "Minimized"
                                                                : "Normal";
    return (g.hasPosition ? "(" + tal(g.x) + ", " + tal(g.y) + ") "
                          : std::string("(ingen position) ")) +
           tal(g.width) + "×" + tal(g.height) + " " + läge;
}

// Primärens arbetsyta i LOGISKA px — en plats en bit in i den är "inne".
struct Primär {
    float s  = 1.0f;
    float wx = 0.0f, wy = 0.0f, ww = 0.0f, wh = 0.0f;   // logiska px
};

std::optional<Primär> läsPrimär()
{
    const std::vector<gfx::ScreenInfo> sk = gfx::screens();
    const gfx::ScreenInfo*             p  = glaze::detail::primaryOf(sk);
    if (!p)
        return std::nullopt;
    Primär r;
    r.s  = p->scale > 0.0f ? p->scale : 1.0f;
    r.wx = p->work.x / r.s;
    r.wy = p->work.y / r.s;
    r.ww = p->work.w / r.s;
    r.wh = p->work.h / r.s;
    return r;
}

void delW()
{
    std::printf("\n=== W. med fönster ===\n");
    (void)glaze::app();   // plattformen — `gfx::screens()` kräver den
    const std::optional<Primär> pri = läsPrimär();
    if (!pri)
        inteHär("gfx::screens() är tom (Wayland, eller ingen plattform) — "
                "positionsfallen hoppas över, storleken och läget mäts");
    const int lx = pri ? static_cast<int>(std::lround(pri->wx + 40)) : 0;
    const int ly = pri ? static_cast<int>(std::lround(pri->wy + 40)) : 0;

    // ---- W1 + proxyns tomma svar före init
    {
        glaze::AppWindow aldrig(unikTitel("W1-aldrig"), 300, 200);
        const gfx::WindowPlacement före = aldrig.window().placement();
        check(före.width == 0 && före.height == 0 && !före.hasPosition,
              "W1 ett fönster som aldrig visats svarar med en tom placement()");

        const std::string titel = unikTitel("W1");
        glaze::AppWindow  w(titel, 300, 200);
        w.setOffscreen(true);
        if (pri)
            w.setLocation(lx, ly);
        w.setWindowState(gfx::WindowState::Maximized);
        const bool kördes = iPumpen(w, [&] {
            const glaze::WindowGeometry g = w.geometry();
            if (pri && g.hasPosition)
                check(std::abs(g.x - lx) <= 1 && std::abs(g.y - ly) <= 1,
                      "W1 setLocation före öppningen ⇒ geometry() x/y ±1",
                      geo(g) + " mot (" + tal(lx) + ", " + tal(ly) + ")");
            else if (pri)
                check(false, "W1 hasPosition med en skärmlista", geo(g));
            check(g.width == 300 && g.height == 200,
                  "W1 storleken är den begärda (Linux: proxyns "
                  "vidarebefordran)",
                  geo(g));
            check(g.state == gfx::WindowState::Normal &&
                      w.windowState() == gfx::WindowState::Normal,
                  "W1 offscreen får aldrig startläget — Normal", geo(g));
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = hwndFör(titel);
            check(h && !IsWindowVisible(h) && !IsZoomed(h),
                  "W1 OS-fönstret är varken synligt eller zoomat");
#endif
            w.close();
        });
        if (!kördes)
            hoppa("W1");
    }

    // ---- W2 setLocation efter öppningen. Tiden: en X11-ConfigureNotify
    // kommer efter anropet, så kontrollen väntar på en tick.
    if (pri) {
        glaze::AppWindow w(unikTitel("W2"), 300, 200);
        w.setOffscreen(true);
        w.setLocation(lx, ly);
        glaze::Timer t;
        t.setInterval(150);
        t.setSingleShot(true);
        t.onTick = [&] {
            const glaze::WindowGeometry g = w.geometry();
            check(g.hasPosition && std::abs(g.x - (lx + 50)) <= 1 &&
                      std::abs(g.y - (ly + 30)) <= 1,
                  "W2 setLocation efter öppningen ⇒ geometry() följer",
                  geo(g));
            w.close();
        };
        const bool kördes = iPumpen(w, [&] {
            w.setLocation(lx + 50, ly + 30);
            t.start();
        });
        if (!kördes)
            hoppa("W2");
    } else {
        inteHär("W2 kräver en skärmrymd");
    }

#ifdef GLAZE_BACKEND_D3D11
    förbrukaStartvisningen();

    // ---- W3 maximerad start (VISAR ett fönster)
    {
        const std::string titel = unikTitel("W3");
        glaze::AppWindow  w(titel, 320, 220);
        w.setLocation(lx, ly);
        w.setWindowState(gfx::WindowState::Maximized);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            check(w.windowState() == gfx::WindowState::Maximized &&
                      h && IsZoomed(h),
                  "W3 setWindowState(Maximized) före run ⇒ maximerat");
            const glaze::WindowGeometry g = w.geometry();
            check(g.width == 320 && g.height == 220,
                  "W3 geometry() bär NORMALstorleken under maximeringen",
                  geo(g));
            check(std::abs(g.x - lx) <= 1 && std::abs(g.y - ly) <= 1,
                  "W3 … och NORMALplatsen — inte den maximerade ramen",
                  geo(g) + " mot (" + tal(lx) + ", " + tal(ly) + ")");
            w.restore();
            const glaze::WindowGeometry n = w.geometry();
            check(w.windowState() == gfx::WindowState::Normal &&
                      std::abs(n.x - lx) <= 1 && std::abs(n.y - ly) <= 1 &&
                      n.width == 320 && n.height == 220,
                  "W3 restore() ⇒ Normal på den begärda platsen", geo(n));
            w.close();
        });
        if (!kördes)
            hoppa("W3");
    }

    // ---- W4 kortet efter stängning, och nästa öppning (VISAR ett fönster)
    {
        const std::string titel = unikTitel("W4");
        glaze::AppWindow  w(titel, 340, 240);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            w.maximize();
            check(w.windowState() == gfx::WindowState::Maximized,
                  "W4 (fixturen) maximerat innan stängningen");
            w.close();
        });
        if (!kördes) {
            hoppa("W4");
        } else {
            const glaze::WindowGeometry g = w.geometry();
            check(g.state == gfx::WindowState::Maximized && g.width == 340 &&
                      g.height == 240,
                  "W4 kortet: Maximized med NORMALstorleken", geo(g));
            check(std::abs(g.x - lx) <= 1 && std::abs(g.y - ly) <= 1,
                  "W4 kortet bär normalplatsen", geo(g));
            w.setOffscreen(true);
            const bool igen = iPumpen(w, [&] {
                const gfx::Sizef cs = w.clientSize();
                check(nära(cs.w, 340) && nära(cs.h, 240),
                      "W4 nästa öppning har normalstorleken (B14)",
                      tal(cs.w) + "×" + tal(cs.h));
                w.close();
            });
            if (!igen)
                hoppa("W4 (andra öppningen)");
        }
    }
#else
    inteHär("W3/W4 körs bara med D3D11 (de visar ett riktigt fönster och "
            "läser IsZoomed)");
#endif

    // ---- W5 ramen
    {
        const std::string titel = unikTitel("W5");
        glaze::AppWindow  w(titel, 300, 200);
        w.setOffscreen(true);
        w.setChrome(gfx::Chrome::None);
        check(w.topBar() == nullptr && w.chrome() == gfx::Chrome::None,
              "W5 setChrome(None) före öppning ⇒ ingen namnlist");
        const bool kördes = iPumpen(w, [&] {
            check(w.topBar() == nullptr, "W5 … också öppet");
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = hwndFör(titel);
            check(h && (GetWindowLongPtrW(h, GWL_STYLE) & WS_POPUP) != 0,
                  "W5 OS-fönstret är WS_POPUP");
#endif
            w.close();
        });
        if (!kördes)
            hoppa("W5");

        glaze::AppWindow lat(unikTitel("W5-lat"), 300, 200, gfx::Chrome::None);
        const std::size_t före = lat.root().children().size();
        check(lat.topBar() == nullptr, "W5 (fixturen) konstruerat utan namnlist");
        lat.setChrome(gfx::Chrome::Custom);
        const auto& barn = lat.root().children();
        check(lat.topBar() != nullptr && lat.chrome() == gfx::Chrome::Custom,
              "W5 setChrome(Custom) bygger namnlisten LATT");
        check(barn.size() == före + 1 && !barn.empty() &&
                  barn.front() == lat.topBar(),
              "W5 namnlisten står FÖRST i roten, som konstruktorn lagt den",
              tal(static_cast<double>(barn.size())));
        check(!barn.empty() && barn.back() == &lat.appIcon,
              "W5 programikonen är rotens SISTA barn — över namnlisten");
        glaze::AppWindow custom(unikTitel("W5-custom"), 300, 200,
                                gfx::Chrome::Custom);
        const auto& facit = custom.root().children();
        bool sammaOrdning = facit.size() == barn.size();
        for (std::size_t i = 0; sammaOrdning && i < facit.size(); ++i)
            sammaOrdning = facit[i]->name == barn[i]->name &&
                           (facit[i] == custom.topBar()) ==
                               (barn[i] == lat.topBar());
        check(sammaOrdning,
              "W5 rotordningen är densamma som för ett fönster konstruerat "
              "med Custom");
    }

    // ---- W6a dold ägare ⇒ primärskärmen
    if (pri) {
        glaze::AppWindow ägare(unikTitel("W6a-agare"), 400, 300,
                               gfx::Chrome::None);
        ägare.setOffscreen(true);
        glaze::AppWindow barn(unikTitel("W6a-barn"), 200, 100,
                              gfx::Chrome::None);
        barn.setOffscreen(true);
        barn.setStartPosition(glaze::StartPosition::CenterOwner);
        const bool kördes = iPumpen(ägare, [&] {
            barn.show(ägare);
            const glaze::WindowGeometry g = barn.geometry();
            const float ex = pri->wx + (pri->ww - 200.0f) * 0.5f;
            const float ey = pri->wy + (pri->wh - 100.0f) * 0.5f;
            check(g.hasPosition && std::fabs(g.x - ex) <= 1.0f &&
                      std::fabs(g.y - ey) <= 1.0f,
                  "W6a en DOLD ägare ⇒ barnet mitt på primärskärmen (B12)",
                  geo(g) + " mot (" + tal(ex) + ", " + tal(ey) + ")");
            barn.close();
            ägare.close();
        });
        if (!kördes)
            hoppa("W6a");
    } else {
        inteHär("W6a kräver en skärmrymd");
    }

#ifdef GLAZE_BACKEND_D3D11
    // ---- W6b synlig ägare ⇒ mitt över den (VISAR ett fönster)
    {
        glaze::AppWindow ägare(unikTitel("W6b-agare"), 400, 300,
                               gfx::Chrome::None);
        ägare.setLocation(lx, ly);
        glaze::AppWindow barn(unikTitel("W6b-barn"), 200, 100,
                              gfx::Chrome::None);
        barn.setOffscreen(true);
        barn.setStartPosition(glaze::StartPosition::CenterOwner);
        const bool kördes = iPumpen(ägare, [&] {
            barn.show(ägare);
            const glaze::WindowGeometry ä = ägare.geometry();
            const glaze::WindowGeometry b = barn.geometry();
            const float äx = ä.x + ä.width * 0.5f, äy = ä.y + ä.height * 0.5f;
            const float bx = b.x + b.width * 0.5f, by = b.y + b.height * 0.5f;
            check(std::fabs(äx - bx) <= 1.0f && std::fabs(äy - by) <= 1.0f,
                  "W6b en SYNLIG ägare ⇒ barnets mitt = ägarens mitt ±1",
                  "ägaren " + geo(ä) + ", barnet " + geo(b));
            barn.close();
            ägare.close();
        });
        if (!kördes)
            hoppa("W6b");
    }
#else
    inteHär("W6b körs bara med D3D11 (den visar ett riktigt fönster)");
#endif
}

// ================================= L onLoad FÖRE SYNLIGHETEN

// Ett fönster vars `onLoad` kör `iLoad` och minns hur fönstret såg ut MEDAN
// handlern kördes — glaze-sidan (`isHidden`) och, med D3D11, OS-fönstret
// (`IsWindowVisible`, grindens eget orakel och inte produktionens).
class LaddFönster : public glaze::AppWindow {
public:
    LaddFönster(const std::string& titel, int b, int h,
                gfx::Chrome c = gfx::defaultChrome())
        : glaze::AppWindow(titel, b, h, c), titel_(titel)
    {
        onLoad = [this] {
            ++laddningar;
            doltILoad = isHidden();
#ifdef GLAZE_BACKEND_D3D11
            const HWND hw  = hwndFör(titel_);
            osFanns        = hw != nullptr;
            osSynligtILoad = hw && IsWindowVisible(hw);
#endif
            if (iLoad)
                iLoad();
        };
        onShow = [this] { ++visningar; };
    }
    std::function<void()> iLoad;
    int                   laddningar = 0, visningar = 0;
    bool                  doltILoad = false, osFanns = false;
    bool                  osSynligtILoad = true;
#ifdef GLAZE_BACKEND_D3D11
    HWND hwnd() const { return hwndFör(titel_); }
#endif

private:
    std::string titel_;
};

void delL()
{
    std::printf("\n=== L. onLoad före synligheten ===\n");
    (void)glaze::app();
    const std::optional<Primär> pri = läsPrimär();
#ifdef GLAZE_BACKEND_D3D11
    förbrukaStartvisningen();
#else
    inteHär("L: OS-oraklet (IsWindowVisible/IsZoomed) finns bara med D3D11 — "
            "här mäts glaze-sidan (isHidden, livscykeln och geometrin)");
#endif

    // ---- L1 setLocation i onLoad: dolt under handlern, och där efteråt.
    if (pri) {
        const int lx = static_cast<int>(std::lround(pri->wx + 60));
        const int ly = static_cast<int>(std::lround(pri->wy + 50));
        LaddFönster w(unikTitel("L1"), 320, 220);
        w.iLoad = [&] { w.setLocation(lx, ly); };
        const bool kördes = iPumpenEfterSvaret(w, [&] {
            check(w.laddningar == 1 && w.doltILoad,
                  "L1 onLoad körs med fönstret DOLT (isHidden)");
#ifdef GLAZE_BACKEND_D3D11
            check(w.osFanns && !w.osSynligtILoad,
                  "L1 … och OS-fönstret är osynligt under onLoad");
            const HWND h = w.hwnd();
            check(h && IsWindowVisible(h), "L1 OS-fönstret syns efteråt");
#endif
            const glaze::WindowGeometry g = w.geometry();
            check(!w.isHidden() && w.visningar == 1 &&
                      std::abs(g.x - lx) <= 1 && std::abs(g.y - ly) <= 1,
                  "L1 setLocation i onLoad ⇒ fönstret visas DÄR (geometry() "
                  "±1), och onShow EN gång",
                  geo(g) + " mot (" + tal(lx) + ", " + tal(ly) + ")");
            w.close();
        });
        if (!kördes)
            hoppa("L1");

        // L1b — långt utanför arbetsytan ⇒ klämd in i den, som före show().
        LaddFönster u(unikTitel("L1b"), 320, 220);
        u.iLoad = [&] { u.setLocation(100000, 100000); };
        const bool kördesU = iPumpenEfterSvaret(u, [&] {
            const glaze::WindowGeometry g = u.geometry();
            check(g.hasPosition && g.x + g.width <= pri->wx + pri->ww + 1.0f &&
                      g.y + g.height <= pri->wy + pri->wh + 1.0f,
                  "L1b en plats utanför arbetsytan i onLoad klämdes in i den",
                  geo(g));
            u.close();
        });
        if (!kördesU)
            hoppa("L1b");
    } else {
        inteHär("L1 kräver en skärmrymd");
    }

    // ---- L2 setWindowState(Maximized) i onLoad: startläget, inte ett hopp.
    // Under handlern svarar läsarna med BEGÄRAN.
    {
        LaddFönster w(unikTitel("L2"), 320, 220);
        gfx::WindowState iLoadLäge = gfx::WindowState::Normal;
        bool             iLoadMax  = false;
        w.iLoad = [&] {
            w.setWindowState(gfx::WindowState::Maximized);
            iLoadLäge = w.windowState();
            iLoadMax  = w.isMaximized() &&
                       w.geometry().state == gfx::WindowState::Maximized;
        };
        const bool kördes = iPumpen(w, [&] {
            check(iLoadLäge == gfx::WindowState::Maximized && iLoadMax,
                  "L2 i onLoad svarar windowState()/isMaximized()/geometry() "
                  "med det begärda läget");
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = w.hwnd();
            check(!w.osSynligtILoad && h && IsZoomed(h) && IsWindowVisible(h),
                  "L2 dolt under onLoad, MAXIMERAT när det visas");
            check(w.windowState() == gfx::WindowState::Maximized,
                  "L2 windowState() efter visningen: Maximized");
#else
            inteHär("L2 maximeringen efter visningen är fönsterhanterarens "
                    "(X11/Wayland) — läses inte här");
#endif
            w.close();
        });
        if (!kördes)
            hoppa("L2");
    }

    // ---- L3 setContentSize + CenterScreen i onLoad ⇒ centrerat med den NYA
    // storleken (startplatsen räknas om precis före visningen). Ramlöst: dess
    // ytterkant är klientytan (W6b:s skäl).
    if (pri) {
        LaddFönster w(unikTitel("L3"), 300, 200, gfx::Chrome::None);
        w.iLoad = [&] {
            w.setContentSize(600, 400);
            w.setStartPosition(glaze::StartPosition::CenterScreen);
        };
        const bool kördes = iPumpenEfterSvaret(w, [&] {
            const glaze::WindowGeometry g = w.geometry();
            const float ex = pri->wx + (pri->ww - 600.0f) * 0.5f;
            const float ey = pri->wy + (pri->wh - 400.0f) * 0.5f;
            check(g.width == 600 && g.height == 400 && g.hasPosition &&
                      std::fabs(g.x - ex) <= 1.0f && std::fabs(g.y - ey) <= 1.0f,
                  "L3 storleken ur onLoad centreras — platsen räknades OM före "
                  "visningen",
                  geo(g) + " mot (" + tal(ex) + ", " + tal(ey) + ")");
            w.close();
        });
        if (!kördes)
            hoppa("L3");
    } else {
        inteHär("L3 kräver en skärmrymd");
    }

    // ---- L4 close() i onLoad ⇒ aldrig synligt, ingen onShow.
    {
        LaddFönster w(unikTitel("L4"), 240, 160);
        w.iLoad = [&] { w.close(); };
        w.run();
        if (w.laddningar == 0) {
            hoppa("L4");
        } else {
            check(!w.isOpen() && w.visningar == 0 && w.doltILoad,
                  "L4 close() i onLoad ⇒ stängt utan att ha synts — ingen onShow",
                  tal(w.visningar));
        }
    }

    // ---- L5 hide() i onLoad ⇒ en dold start; show() tar fram det.
    {
        LaddFönster w(unikTitel("L5"), 240, 160);
        w.iLoad = [&] { w.hide(); };
        const bool kördes = iPumpen(w, [&] {
            check(w.isOpen() && w.isHidden() && w.visningar == 0,
                  "L5 hide() i onLoad ⇒ öppet och dolt, ingen onShow");
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = w.hwnd();
            check(h && !IsWindowVisible(h), "L5 … och OS-fönstret är osynligt");
#endif
            w.show();
            check(!w.isHidden() && w.visningar == 1,
                  "L5 show() tar fram det och fyrar onShow", tal(w.visningar));
#ifdef GLAZE_BACKEND_D3D11
            check(h && IsWindowVisible(h), "L5 … och OS-fönstret syns");
#endif
            w.close();
        });
        if (!kördes)
            hoppa("L5");
    }

    // ---- L6 dold start + maximize() FÖRE show() ⇒ fortfarande dolt, och
    // maximerat när det visas (före den första visningen byter
    // fönsterkontrollerna startläget i stället för att visa fönstret).
    {
        LaddFönster w(unikTitel("L6"), 240, 160);
        w.hide();
        const bool kördes = iPumpen(w, [&] {
            w.maximize();
            check(w.isHidden() &&
                      w.windowState() == gfx::WindowState::Maximized,
                  "L6 maximize() i en dold start: fortfarande dolt, läget är "
                  "det begärda");
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = w.hwnd();
            check(h && !IsWindowVisible(h),
                  "L6 … och OS-fönstret är osynligt");
#endif
            w.show();
#ifdef GLAZE_BACKEND_D3D11
            check(h && IsWindowVisible(h) && IsZoomed(h),
                  "L6 show() ⇒ synligt och MAXIMERAT");
#endif
            check(!w.isHidden() && w.visningar == 1,
                  "L6 show() fyrar onShow", tal(w.visningar));
            w.close();
        });
        if (!kördes)
            hoppa("L6");
    }

    // ---- L4b close() i onLoad som VETAS ⇒ fönstret lever vidare och
    // VISAS — den första visningen
    // fortsätter efter vetot i stället för att lämna det öppet och osynligt.
    // Kontrollen postas ett varv till: vetorundan går i svansen EFTER de
    // postade (iPumpen-steget hade sett fönstret före vetot).
    {
        LaddFönster w(unikTitel("L4b"), 240, 160);
        int         frågor = 0;
        w.onClose = [&frågor](glaze::Widget*, glaze::CloseArgs& e) {
            e.cancel = ++frågor == 1;   // bara den första stängningen vetas
        };
        w.iLoad = [&] { w.close(); };
        const bool kördes = iPumpen(w, [&] {
            glaze::app().post([&] {
                check(frågor == 1 && w.isOpen() && !w.isHidden() &&
                          w.visningar == 1,
                      "L4b close() i onLoad + veto ⇒ öppet, SYNLIGT och onShow "
                      "EN gång",
                      tal(frågor) + " frågor, " + tal(w.visningar) +
                          " visningar");
#ifdef GLAZE_BACKEND_D3D11
                const HWND h = w.hwnd();
                check(h && IsWindowVisible(h),
                      "L4b … och OS-fönstret syns efter vetot");
#endif
                w.close();   // den andra frågan vetas inte
            });
        });
        if (!kördes)
            hoppa("L4b");
    }

    // ---- L4c hide() i vetots handler ⇒ en DOLD START: fönstret visas inte
    // efter vetot (den begärda dolda starten står kvar).
    {
        LaddFönster w(unikTitel("L4c"), 240, 160);
        int         frågor = 0;
        w.onClose = [&](glaze::Widget*, glaze::CloseArgs& e) {
            if (++frågor == 1) {
                e.cancel = true;
                w.hide();
            }
        };
        w.iLoad = [&] { w.close(); };
        const bool kördes = iPumpen(w, [&] {
            glaze::app().post([&] {
                check(frågor == 1 && w.isOpen() && w.isHidden() &&
                          w.visningar == 0,
                      "L4c hide() i vetot ⇒ öppet och dolt, ingen onShow",
                      tal(w.visningar));
                w.close();
            });
        });
        if (!kördes)
            hoppa("L4c");
    }

#ifdef GLAZE_BACKEND_D3D11
    // ---- L8 ett ÄGT fönster som onLoad öppnar med show(ägare) BEHÅLLER
    // aktiveringen: ägarens första
    // visning efter onLoad aktiverar ägaren, och barnet får den tillbaka.
    // Villkorat på förgrunden: tar inget av fönstren den (en session utan
    // förgrundsrätt) hoppas fallet över.
    {
        LaddFönster ägare(unikTitel("L8"), 320, 220);
        LaddFönster barn(unikTitel("L8-barn"), 200, 120);
        ägare.iLoad = [&] { barn.show(ägare); };
        const bool kördes = iPumpen(ägare, [&] {
            const HWND hÄgare = ägare.hwnd();
            const HWND hBarn  = barn.hwnd();
            const HWND fg     = GetForegroundWindow();
            if (fg != hÄgare && fg != hBarn) {
                hoppa("L8 inget av fönstren fick förgrunden");
            } else {
                check(fg == hBarn && IsWindowVisible(hÄgare) &&
                          IsWindowVisible(hBarn),
                      "L8 barnet som onLoad öppnade är förgrunden efter "
                      "ägarens första visning");
            }
            ägare.close();   // kaskaden tar barnet
        });
        if (!kördes)
            hoppa("L8");
    }
#else
    inteHär("L8 förgrunden mäts bara med D3D11");
#endif
}

// ============================ K STORLEKSRAMEN UTANFÖR DEN SYNLIGA KANTEN

#ifdef GLAZE_BACKEND_D3D11
// Klientytan och fönstret i SKÄRMENS fysiska px.
RECT klientytan(HWND h)
{
    RECT  r{};
    POINT o{0, 0};
    GetClientRect(h, &r);
    ClientToScreen(h, &o);
    return RECT{o.x, o.y, o.x + r.right, o.y + r.bottom};
}

RECT fönstret(HWND h)
{
    RECT r{};
    GetWindowRect(h, &r);
    return r;
}

LRESULT träff(HWND h, LONG sx, LONG sy)
{
    return SendMessageW(h, WM_NCHITTEST, 0, MAKELPARAM(sx, sy));
}

std::string htNamn(LRESULT v)
{
    switch (v) {
    case HTCLIENT:      return "HTCLIENT";
    case HTCAPTION:     return "HTCAPTION";
    case HTLEFT:        return "HTLEFT";
    case HTRIGHT:       return "HTRIGHT";
    case HTTOP:         return "HTTOP";
    case HTBOTTOM:      return "HTBOTTOM";
    case HTTOPLEFT:     return "HTTOPLEFT";
    case HTTOPRIGHT:    return "HTTOPRIGHT";
    case HTBOTTOMLEFT:  return "HTBOTTOMLEFT";
    case HTBOTTOMRIGHT: return "HTBOTTOMRIGHT";
    case HTTRANSPARENT: return "HTTRANSPARENT";
    case HTNOWHERE:     return "HTNOWHERE";
    default:            return tal(static_cast<double>(v));
    }
}

std::string rekt(const RECT& r)
{
    return "(" + tal(r.left) + ", " + tal(r.top) + ")–(" + tal(r.right) +
           ", " + tal(r.bottom) + ")";
}

bool lika(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

// Klientytan är den begärda logiska storleken i fönstrets FULLA skala (±1).
bool begärd(const RECT& kl, glaze::AppWindow& w, int lw, int lh)
{
    const float s = w.window().uiScale();
    return std::labs((kl.right - kl.left) - std::lround(lw * s)) <= 1 &&
           std::labs((kl.bottom - kl.top) - std::lround(lh * s)) <= 1;
}

void delK(int lx, int ly)
{
    std::printf("\n=== K. storleksramen utanför den synliga kanten (D3D11) "
                "===\n");

    // ---- K1, K2, K10, K5 — ett ändringsbart Custom-fönster (VISAR det)
    {
        const std::string titel = unikTitel("K1");
        glaze::AppWindow  w(titel, 400, 300);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            if (!h) {
                check(false, "K1 (fixturen) OS-fönstret finns");
                w.close();
                return;
            }
            const RECT                 kl = klientytan(h);
            const RECT                 wr = fönstret(h);
            const gfx::WindowPlacement p  = w.window().placement();
            const gfx::Pointf          o  = w.window().clientToScreen(0, 0);
            check(p.hasPosition && p.x == std::lround(o.x) &&
                      p.y == std::lround(o.y),
                  "K1 placement() x/y == clientToScreen(0, 0) — klientens hörn",
                  "(" + tal(p.x) + ", " + tal(p.y) + ") mot (" + tal(o.x) +
                      ", " + tal(o.y) + ")");
            check(wr.left < kl.left && wr.right > kl.right &&
                      wr.bottom > kl.bottom && wr.top == kl.top,
                  "K1 ramen ligger utanför klientytan till vänster, höger och "
                  "nedåt — inte uppåt",
                  "fönstret " + rekt(wr) + ", klienten " + rekt(kl));
            check(begärd(kl, w, 400, 300),
                  "K1 klientytan är den begärda storleken × fönstrets skala",
                  rekt(kl));

            // K2 — träffpunkterna i skärmens px.
            const LONG b    = kl.right - kl.left;
            const LONG hh   = kl.bottom - kl.top;
            const LONG mx   = kl.left + b / 2;
            const LONG my   = kl.top + hh / 2;
            const LONG band = wr.bottom - kl.bottom;   // ramens tjocklek
            struct Punkt {
                LONG        x, y;
                LRESULT     väntat;
                const char* vad;
            };
            const Punkt punkter[] = {
                {kl.left + 2, my, HTCLIENT,
                 "K2 2 px in vid vänsterkanten ⇒ HTCLIENT (buggens kärna)"},
                {kl.right - 3, my, HTCLIENT,
                 "K2 2 px in vid högerkanten ⇒ HTCLIENT"},
                {mx, kl.bottom - 3, HTCLIENT,
                 "K2 2 px in vid nederkanten ⇒ HTCLIENT"},
                {kl.left - 3, my, HTLEFT,
                 "K2 3 px utanför vänsterkanten ⇒ HTLEFT"},
                {kl.right + 2, my, HTRIGHT,
                 "K2 3 px utanför högerkanten ⇒ HTRIGHT"},
                {mx, kl.bottom + 2, HTBOTTOM,
                 "K2 3 px under nederkanten ⇒ HTBOTTOM"},
                {kl.right + 2, kl.bottom + 2, HTBOTTOMRIGHT,
                 "K2 utanför hörnet ⇒ HTBOTTOMRIGHT"},
                {mx, kl.top + 2, HTTOP,
                 "K2 2 px ned i klientytan ⇒ HTTOP (bandet)"},
                {mx, kl.top + band + 4, HTCAPTION,
                 "K2 namnlistens mitt under bandet ⇒ HTCAPTION"},
            };
            for (const Punkt& pt : punkter) {
                const LRESULT fick = träff(h, pt.x, pt.y);
                check(fick == pt.väntat, pt.vad, htNamn(fick));
            }

            // K10 — DWM:s synliga kant.
            RECT ext{};
            if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS,
                                                &ext, sizeof ext))) {
                LONG tol  = 2;
                UINT kant = 0;
                if (SUCCEEDED(DwmGetWindowAttribute(
                        h, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &kant,
                        sizeof kant)))
                    tol = std::max<LONG>(tol, static_cast<LONG>(kant));
                auto inom = [tol](LONG d) { return d >= 0 && d <= tol; };
                check(inom(kl.left - ext.left) && inom(ext.right - kl.right) &&
                          inom(ext.bottom - kl.bottom) &&
                          std::labs(ext.top - kl.top) <= tol &&
                          ext.left > wr.left && ext.right < wr.right &&
                          ext.bottom < wr.bottom,
                      "K10 DWM:s synliga kant ligger vid klientytan, innanför "
                      "GetWindowRect — ramen är osynlig",
                      "synlig " + rekt(ext) + ", klienten " + rekt(kl) +
                          ", fönstret " + rekt(wr));
            } else {
                inteHär("K10 DWMWA_EXTENDED_FRAME_BOUNDS svarar inte — ingen "
                        "DWM-kant att mäta");
            }

            // K5 — setLocation efter öppningen. SetWindowPos är synkron.
            const int nx = lx + 30, ny = ly + 20;
            w.setLocation(nx, ny);
            const gfx::Pointf väntat = glaze::detail::toPlacement(
                gfx::Pointf{static_cast<float>(nx), static_cast<float>(ny)},
                glaze::detail::primaryScale(gfx::screens()));
            const gfx::Pointf          o2 = w.window().clientToScreen(0, 0);
            const gfx::WindowPlacement p2 = w.window().placement();
            check(nära(o2.x, väntat.x, 1.0f) && nära(o2.y, väntat.y, 1.0f) &&
                      p2.x == std::lround(o2.x) && p2.y == std::lround(o2.y),
                  "K5 setLocation efter öppningen ⇒ klientens hörn på "
                  "toPlacement ±1, placement() lika",
                  "(" + tal(o2.x) + ", " + tal(o2.y) + ") mot (" +
                      tal(väntat.x) + ", " + tal(väntat.y) + "), placement (" +
                      tal(p2.x) + ", " + tal(p2.y) + ")");
            w.close();
        });
        if (!kördes)
            hoppa("K1");
    }

    // ---- K3 ett FAST fönster (VISAR det)
    {
        const std::string titel = unikTitel("K3");
        glaze::AppWindow  w(titel, 300, 200);
        w.setMinimumSize(300, 200);
        w.setMaximumSize(300, 200);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            if (!h) {
                check(false, "K3 (fixturen) OS-fönstret finns");
                w.close();
                return;
            }
            const RECT kl = klientytan(h);
            const RECT wr = fönstret(h);
            check(lika(wr, kl),
                  "K3 ett fast fönster har ingen ram — GetWindowRect == "
                  "klientytan",
                  "fönstret " + rekt(wr) + ", klienten " + rekt(kl));
            check(begärd(kl, w, 300, 200), "K3 … och klientytan är den begärda",
                  rekt(kl));
            const LONG    mx = kl.left + (kl.right - kl.left) / 2;
            const LONG    my = kl.top + (kl.bottom - kl.top) / 2;
            const LRESULT in = träff(h, kl.left + 2, my);
            const LRESULT up = träff(h, mx, kl.top + 2);
            check(in == HTCLIENT, "K3 2 px in vid vänsterkanten ⇒ HTCLIENT",
                  htNamn(in));
            check(up != HTTOP, "K3 2 px ned är INTE HTTOP — inget band utan ram",
                  htNamn(up));
            w.close();
        });
        if (!kördes)
            hoppa("K3");
    }

    // ---- K4 maximerat, och WM_GETMINMAXINFO med normalramen (VISAR det)
    {
        const std::string titel = unikTitel("K4");
        glaze::AppWindow  w(titel, 400, 300);
        w.setMinimumSize(320, 220);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            if (!h) {
                check(false, "K4 (fixturen) OS-fönstret finns");
                w.close();
                return;
            }
            // NORMALLÄGETS ram, mätt innan fönstret maximeras.
            const RECT kl = klientytan(h);
            const RECT wr = fönstret(h);
            const LONG fL = kl.left - wr.left;
            const LONG fR = wr.right - kl.right;
            const LONG fB = wr.bottom - kl.bottom;
            w.maximize();
            check(IsZoomed(h) != 0, "K4 (fixturen) maximerat");
            const RECT    km = klientytan(h);
            const LRESULT up = träff(h, km.left + (km.right - km.left) / 2,
                                     km.top + 2);
            check(up != HTTOP, "K4 maximerat: 2 px ned är INTE HTTOP",
                  htNamn(up));
            MINMAXINFO mmi{};
            SendMessageW(h, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
            const float s  = w.window().uiScale();
            const LONG  vx = std::lround(320 * s) + fL + fR;
            const LONG  vy = std::lround(220 * s) + fB;
            check(mmi.ptMinTrackSize.x == vx && mmi.ptMinTrackSize.y == vy,
                  "K4 WM_GETMINMAXINFO maximerat: minsta storleken + "
                  "NORMALLÄGETS ram",
                  tal(mmi.ptMinTrackSize.x) + "×" + tal(mmi.ptMinTrackSize.y) +
                      " mot " + tal(vx) + "×" + tal(vy));
            w.restore();
            const RECT kr = klientytan(h);
            check(!IsZoomed(h) && begärd(kr, w, 400, 300),
                  "K4 restore() ⇒ den begärda klientytan igen", rekt(kr));
            w.close();
        });
        if (!kördes)
            hoppa("K4");
    }

    // ---- K6 fast ↔ ändringsbart i ett öppet fönster (VISAR det)
    {
        const std::string titel = unikTitel("K6");
        glaze::AppWindow  w(titel, 400, 300);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            if (!h) {
                check(false, "K6 (fixturen) OS-fönstret finns");
                w.close();
                return;
            }
            const RECT före = klientytan(h);
            w.setMinimumSize(400, 300);
            w.setMaximumSize(400, 300);
            const RECT fast  = klientytan(h);
            const RECT fastF = fönstret(h);
            check(lika(fast, före),
                  "K6 ändringsbart → fast: klientytans plats och storlek står "
                  "still",
                  rekt(fast) + " mot " + rekt(före));
            check(lika(fastF, fast), "K6 … och ramen är borta",
                  "fönstret " + rekt(fastF));
            w.setMaximumSize(0, 0);
            w.setMinimumSize(0, 0);
            const RECT igen  = klientytan(h);
            const RECT igenF = fönstret(h);
            check(lika(igen, före),
                  "K6 fast → ändringsbart: klientytan står still",
                  rekt(igen) + " mot " + rekt(före));
            check(igenF.left < igen.left && igenF.right > igen.right &&
                      igenF.bottom > igen.bottom && igenF.top == igen.top,
                  "K6 … och ramen är tillbaka", "fönstret " + rekt(igenF));
            w.close();
        });
        if (!kördes)
            hoppa("K6");
    }

    // ---- K7 Chrome::None (VISAR det)
    {
        const std::string titel = unikTitel("K7");
        glaze::AppWindow  w(titel, 300, 200, gfx::Chrome::None);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            if (!h) {
                check(false, "K7 (fixturen) OS-fönstret finns");
                w.close();
                return;
            }
            const RECT kl = klientytan(h);
            const RECT wr = fönstret(h);
            check(lika(wr, kl), "K7 Chrome::None har ingen ram",
                  "fönstret " + rekt(wr) + ", klienten " + rekt(kl));
            check(begärd(kl, w, 300, 200), "K7 … och klientytan är den begärda",
                  rekt(kl));
            const LRESULT in =
                träff(h, kl.left + 2, kl.top + (kl.bottom - kl.top) / 2);
            check(in == HTCLIENT, "K7 2 px in vid vänsterkanten ⇒ HTCLIENT",
                  htNamn(in));
            w.close();
        });
        if (!kördes)
            hoppa("K7");
    }

    // ---- K8 transparent (VISAR det)
    {
        const std::string titel = unikTitel("K8");
        glaze::AppWindow  w(titel, 300, 200);
        w.setTransparent(true);
        w.setLocation(lx, ly);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            if (!h) {
                check(false, "K8 (fixturen) OS-fönstret finns");
                w.close();
                return;
            }
            const RECT kl = klientytan(h);
            const RECT wr = fönstret(h);
            check(lika(wr, kl), "K8 ett transparent fönster har ingen ram",
                  "fönstret " + rekt(wr) + ", klienten " + rekt(kl));
            if (!w.window().transparent()) {
                inteHär("K8 composition-vägen degraderade till opakt — "
                        "alfa-hit-testen mäts inte");
            } else {
                // En ritad och presenterad frame fyller alfa-spegeln, som
                // WM_NCHITTEST läser — mitten har ingenting ritat.
                w.window().renderFrame();
                const LRESULT mitt =
                    träff(h, kl.left + (kl.right - kl.left) / 2,
                          kl.top + (kl.bottom - kl.top) / 2);
                check(mitt == HTTRANSPARENT,
                      "K8 mitten (ingenting ritat) släpper klicket — "
                      "HTTRANSPARENT",
                      htNamn(mitt));
            }
            w.close();
        });
        if (!kördes)
            hoppa("K8");
    }

    // ---- K9 popup med ankare (VISAR ägaren och popupen)
    {
        const std::string titelÄ = unikTitel("K9-agare");
        const std::string titelP = unikTitel("K9-popup");
        glaze::AppWindow  ägare(titelÄ, 400, 300);
        ägare.setLocation(lx, ly);
        glaze::AppWindow popup(titelP, 120, 80, gfx::Chrome::None);
        popup.setPopup(true);
        const gfx::Rectf a{10.0f, 40.0f, 60.0f, 20.0f};   // ägarens klient-px
        popup.setPopupAnchor(a);
        const bool kördes = iPumpen(ägare, [&] {
            popup.show(ägare);
            const HWND hp = hwndFör(titelP);
            if (!hp) {
                check(false, "K9 (fixturen) popupens OS-fönster finns");
                popup.close();
                ägare.close();
                return;
            }
            const RECT  kl = klientytan(hp);
            const RECT  wr = fönstret(hp);
            const float so = ägare.window().uiScale();
            check(lika(wr, kl), "K9 popupen har ingen ram",
                  "fönstret " + rekt(wr) + ", klienten " + rekt(kl));
            check(std::labs((kl.right - kl.left) - std::lround(120 * so)) <= 1 &&
                      std::labs((kl.bottom - kl.top) - std::lround(80 * so)) <=
                          1,
                  "K9 popupen har den begärda storleken (ägarens skala)",
                  rekt(kl));
            const gfx::Pointf v = ägare.window().clientToScreen(a.x, a.y + a.h);
            check(nära(static_cast<float>(kl.left), v.x, 1.0f) &&
                      nära(static_cast<float>(kl.top), v.y, 1.0f),
                  "K9 popupens hörn under ankaret ±1",
                  rekt(kl) + " mot (" + tal(v.x) + ", " + tal(v.y) + ")");
            popup.close();
            ägare.close();
        });
        if (!kördes)
            hoppa("K9");
    }
}
#endif   // GLAZE_BACKEND_D3D11

#ifdef GLAZE_BACKEND_LINUX
// KL — identiteten `placement() == clientToScreen(0, 0)` med storleks-
// marginalen. Ett RIKTIGT fönster, som visas ett ögonblick (L:s
// mönster): ett omappat fönster placerar och flyttar WM:en inte — Westons XWM
// (WSLg) svarar på en flytt av det med en syntetisk configure och låter det
// stå. Tiden: X11:s ConfigureNotify kommer efter mappningen och efter
// `setPosition`, så båda kontrollerna väntar på en tick (W2:s mönster).
void delKL()
{
    std::printf("\n=== KL. storleksmarginalen och placeringen (Linux) ===\n");
    glaze::AppWindow w(unikTitel("KL"), 300, 200);
    auto identitet = [&](const char* vad) {
        const gfx::WindowPlacement p = w.window().placement();
        const gfx::Pointf          o = w.window().clientToScreen(0, 0);
        check(p.x == std::lround(o.x) && p.y == std::lround(o.y), vad,
              "(" + tal(p.x) + ", " + tal(p.y) + ") mot (" + tal(o.x) + ", " +
                  tal(o.y) + ")");
    };
    int          målX = 0, målY = 0;
    glaze::Timer t;
    t.setInterval(kSvarMs);
    t.setSingleShot(true);
    t.onTick = [&] {
        identitet("KL efter setPosition: placement() x/y == "
                  "clientToScreen(0, 0)");
        const gfx::WindowPlacement p = w.window().placement();
        check(p.x == målX && p.y == målY,
              "KL setPosition(x, y) lägger KLIENTENS hörn på (x, y)",
              "(" + tal(p.x) + ", " + tal(p.y) + ") mot (" + tal(målX) +
                  ", " + tal(målY) + ")");
        w.close();
    };
    const bool kördes = iPumpenEfterSvaret(w, [&] {
        if (!w.window().placement().hasPosition) {
            inteHär("KL ingen skärmrymd (Wayland, headless) — identiteten "
                    "mäts inte");
            w.close();
            return;
        }
        identitet("KL placement() x/y == clientToScreen(0, 0) — klientens "
                  "hörn");
        const gfx::WindowPlacement p = w.window().placement();
        målX = p.x + 40;
        målY = p.y + 30;
        w.window().setPosition(målX, målY);
        t.start();
    });
    if (!kördes)
        hoppa("KL");
}
#endif   // GLAZE_BACKEND_LINUX

// ======================= SC/TW — FÖNSTRET FÖLJER INNEHÅLLET OCH PUNKTEN I
// FÖNSTRET. Offscreen och ramlöst, så
// chromets höjd är 0 och klientytan ÄR innehållsytan. Stegen körs i
// fönstrets pump med några varv emellan (`Varv`), så en postad anpassning
// och ritningens fråga hinner köras.
struct Behallare final : glaze::Widget {   // childArea = den egna recten
    void draw(const glaze::Ctx&) override {}
};

// X11 SVARAR ASYNKRONT på en storleksändring (`XResizeWindow` — ytan får sin
// storlek först med ConfigureNotify, ett serverbesök senare), så på Linux
// väntar nästa steg dessutom tills roten står i den klientstorlek fönstret
// begärt (`satt`) — högst 2 s, därefter mäter stegen det som blev — och ger
// svaret fyra varv till att ritas och levereras. Ramlöst: roten ÄR klientytan.
#ifdef GLAZE_BACKEND_LINUX
bool satt(glaze::AppWindow& w)
{
    if (!w.isOpen())
        return true;
    const gfx::Sizef a = w.root().actualSize();
    const gfx::Sizef k = w.clientSize();
    return nära(a.w, k.w) && nära(a.h, k.h);
}
#endif

struct Varv {
    std::vector<std::function<void()>> steg;
    std::size_t                        i    = 0;
    int                                kvar = 0;
    glaze::AppWindow*                     fönster = nullptr;   // `satt`
    std::chrono::steady_clock::time_point gräns{};
    bool                                  väntade = false;
    void kör()
    {
        if (kvar > 0) {
            --kvar;
            glaze::app().post([this] { kör(); });
            return;
        }
        // Efter det sista steget rörs fönstret inte: de tomma varven kan
        // ligga kvar i postkön när det redan är rivet (`static Varv`).
        if (i >= steg.size())
            return;
#ifdef GLAZE_BACKEND_LINUX
        if (fönster && !satt(*fönster) &&
            std::chrono::steady_clock::now() < gräns) {
            väntade = true;
            glaze::app().post([this] { kör(); });
            return;
        }
        if (väntade) {
            väntade = false;
            kvar    = 4;
            glaze::app().post([this] { kör(); });
            return;
        }
#endif
        steg[i++]();
        kvar  = 4;
        gräns = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        glaze::app().post([this] { kör(); });
    }
};

struct FoljFonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    glaze::Panel rad[3];
    glaze::Panel fot;
    glaze::Label text;
    Behallare    ram;
    Behallare    inre;
    glaze::Point tryckt{};
    explicit FoljFonster(const std::string& titel)
        : glaze::AppWindow(titel, 300, 500, gfx::Chrome::None)
    {
        const float h[3] = {30, 40, 50};
        for (int i = 0; i < 3; ++i) {
            rad[i].name   = "rad" + std::to_string(i);
            rad[i].dock   = glaze::Dock::Top;
            rad[i].height = h[i];
            root().adopt(rad[i]);
        }
        fot.name   = "fot";
        fot.dock   = glaze::Dock::Bottom;
        fot.height = 36;
        root().adopt(fot);
    }
};

void delSC()
{
    std::printf("\n=== SC/TW. fönstret följer innehållet, punkten i fönstret "
                "===\n");
    // ---- SC1–SC5, SC8, SC9: Height med tre Top-rader och en Bottom-rad
    {
        FoljFonster w(unikTitel("SC"));
        w.setOffscreen(true);
        w.setSizeToContent(glaze::SizeToContent::Height);
        int   rotHandelser = 0;
        float textFöre     = 0.0f;
        static Varv v;
        v         = Varv{};
        v.fönster = &w;
        v.steg = {
            [&] {
                check(nära(w.contentSize().h, 156.0f, 1.0f),
                      "SC1 Height: innehållets höjd 30 + 40 + 50 + 36",
                      tal(static_cast<int>(w.contentSize().h)));
                check(nära(w.clientSize().w, 300.0f, 0.5f),
                      "SC1 bredden är begärans");
                w.rad[1].hide();
            },
            [&] {
                check(nära(w.contentSize().h, 116.0f, 1.0f),
                      "SC2 en dold rad krymper fönstret (postat)",
                      tal(static_cast<int>(w.contentSize().h)));
                w.rad[1].show();
            },
            [&] {
                check(nära(w.contentSize().h, 156.0f, 1.0f),
                      "SC2 visad igen växer det tillbaka");
                // SC4 — ett mått ändras utan synlighetsändring: en rad med
                // en etikett som får två rader text. `setWrap(true)` är
                // fixturens: en Label UTAN brytning är EN rad också med `\n`
                // i texten (label.h, tabellen vid `setWrap`), så höjden stod
                // still och fallet mätte aldrig ritningens fråga.
                w.text.name = "text";
                w.text.dock = glaze::Dock::Top;
                w.text.setWrap(true);
                w.text.setText("en");
                w.root().adopt(w.text);
                w.invalidate();
            },
            [&] {
                textFöre = w.contentSize().h;
                w.text.setText("en\ntvå\ntre");
                w.invalidate();
            },
            [&] {
                check(w.contentSize().h > textFöre + 1.0f,
                      "SC4 setText utan synlighetsändring anpassar efter "
                      "ritningens fråga",
                      tal(static_cast<int>(w.contentSize().h)) + " mot " +
                          tal(static_cast<int>(textFöre)));
            },
            [&] {
                // SC9 — stabilitet: inga storleksändringar efter anpassningen
                w.root().onResize = [&rotHandelser] { ++rotHandelser; };
                w.window().renderFrame();
            },
            [&] {
                rotHandelser = 0;
                for (int i = 0; i < 5; ++i)
                    w.window().renderFrame();
            },
            [&] {
                check(rotHandelser == 0,
                      "SC9 stabilt: fem ritningar och svansar ger ingen "
                      "onResize", tal(rotHandelser));
                check(!w.isFixedSize(), "SC5 Height är inte fast");
                // SC8 — setContentSize ger bredden, höjden är innehållets
                w.setContentSize(400, 300);
            },
            [&] {
                check(nära(w.clientSize().w, 400.0f, 0.5f),
                      "SC8 setContentSize ger den fria axeln (bredden 400)");
                check(w.contentSize().h > 150.0f && w.contentSize().h < 250.0f,
                      "SC8 …och höjden är innehållets, inte 300",
                      tal(static_cast<int>(w.contentSize().h)));
                // SC5 — WidthAndHeight är fast
                w.setSizeToContent(glaze::SizeToContent::WidthAndHeight);
            },
            [&] {
                check(w.isFixedSize(), "SC5 WidthAndHeight ⇒ isFixedSize()");
                check(w.minimumSize().w == 0.0f && w.maximumSize().w == 0.0f,
                      "SC5 minimumSize()/maximumSize() svarar med appens "
                      "begäran");
                w.close();
            },
        };
        glaze::app().post([] { v.kör(); });
        w.run();
        if (v.i == 0)
            hoppa("SC");
    }
    // ---- SC6 — Manual (förvalet): en dold rad ändrar inte storleken
    {
        FoljFonster w(unikTitel("SC6"));
        w.setOffscreen(true);
        static Varv v;
        v         = Varv{};
        v.fönster = &w;
        float före = 0.0f;
        v.steg = {
            [&] {
                före = w.clientSize().h;
                w.rad[1].hide();
            },
            [&] {
                check(nära(w.clientSize().h, före, 0.5f),
                      "SC6 Manual: en dold rad ändrar inte storleken");
                w.close();
            },
        };
        glaze::app().post([] { v.kör(); });
        w.run();
    }
    // ---- TW1–TW3 — punkten i fönstret
    {
        FoljFonster w(unikTitel("TW"));
        w.setOffscreen(true);
        w.ram.name    = "ram";
        w.ram.x       = 10;
        w.ram.y       = 20;
        w.ram.width   = 200;
        w.ram.height  = 100;
        w.ram.anchors = {true, true, false, false};
        w.inre.name    = "inre";
        w.inre.x       = 30;
        w.inre.y       = 40;
        w.inre.width   = 60;
        w.inre.height  = 30;
        w.inre.anchors = {true, true, false, false};
        w.ram.adopt(w.inre);
        w.root().adopt(w.ram);
        w.inre.onMouseDown = [&w](glaze::Widget*, glaze::MouseArgs& e) {
            w.tryckt = e.local;
        };
        check(!w.inre.toWindow(glaze::Point{0, 0}),
              "TW3 utan öppet fönster ⇒ tomt");
        static Varv v;
        v         = Varv{};
        v.fönster = &w;
        v.steg = {
            [&] {
                const float      s = w.window().uiScale();
                const gfx::Rectf a = w.window().contentArea();
                // `ram` ligger under raderna i innehållsytan — men den är
                // ODOCKAD och bor i resten, alltså under Top-raderna.
                const float y0 = a.y + (30.0f + 40.0f + 50.0f) * s;
                const std::optional<gfx::Pointf> p =
                    w.inre.toWindow(glaze::Point{5, 6});
                check(p && nära(p->x, a.x + (10 + 30 + 5) * s, 1e-3f) &&
                          nära(p->y, y0 + (20 + 40 + 6) * s, 1e-3f),
                      "TW1 toWindow = innehållsytans hörn + (x-kedjan) × skalan");
                if (p) {
                    const std::optional<glaze::Point> q = w.inre.fromWindow(*p);
                    check(q && std::fabs(q->x - 5.0) < 1e-4 &&
                              std::fabs(q->y - 6.0) < 1e-4,
                          "TW1 fromWindow(toWindow(p)) == p");
                }
                check(nära(w.inre.actualSize().w, 60.0f, 1e-3f) &&
                          nära(w.inre.actualSize().h, 30.0f, 1e-3f),
                      "TW actualSize() 60 × 30");
                // TW2 — trycket genom fönstrets ingång
                gfx::Event e;
                e.type   = gfx::EventType::MouseButtonDown;
                e.button = gfx::MouseButton::Left;
                e.mouseX = a.x + (10 + 30 + 12) * s;
                e.mouseY = y0 + (20 + 40 + 9) * s;
                w.handleEvent(e);
                e.type = gfx::EventType::MouseButtonUp;
                w.handleEvent(e);
                const std::optional<gfx::Pointf> t = w.inre.toWindow(w.tryckt);
                check(t && nära(t->x, a.x + (10 + 30 + 12) * s, 1e-3f) &&
                          nära(t->y, y0 + (20 + 40 + 9) * s, 1e-3f),
                      "TW2 toWindow(e.local) är tryckets position");
                w.close();
            },
        };
        glaze::app().post([] { v.kör(); });
        w.run();
        check(!w.inre.toWindow(glaze::Point{0, 0}) &&
                  w.inre.actualSize().w == 0.0f,
              "TW3 efter stängningen ⇒ tomt");
    }
}

// ============= SB/SG/BG — RADERNA, golvet och BREDDGRUPPEN
//
// SB: rader uppifrån och mot nederkanten i ett fönster som följer innehållet
// (`childrenExtent`s nya regel genom `fitContent`). Ramlöst och offscreen som
// SC: klientytan ÄR innehållsytan. `a` 100 × 30 på (10, 10), `b` 100 × 40
// syskonankrad 6 under `a`, `ok` 60 × 26 nere till höger 10/10,
// handräknat: 10 + 30 + 6 + 40 = 86 uppifrån, 10 + 26 = 36 nedifrån.
struct RadFonster : glaze::AppWindow {
    glaze::Panel a, b, ok;
    explicit RadFonster(const std::string& titel)
        : glaze::AppWindow(titel, 300, 500, gfx::Chrome::None)
    {
        a.name       = "a";
        a.x          = 10;
        a.y          = 10;
        a.width      = 100;
        a.height     = 30;
        b.name       = "b";
        b.anchorTop  = {"a", 6};
        b.width      = 100;
        b.height     = 40;
        ok.name      = "ok";
        ok.anchors   = {false, false, true, true};
        ok.right     = 10;
        ok.bottom    = 10;
        ok.width     = 60;
        ok.height    = 26;
        root().adopt(a);
        root().adopt(b);
        root().adopt(ok);
    }
};

void delSB()
{
    std::printf("\n=== SB. rader uppifrån och mot nederkanten ===\n");
    RadFonster w(unikTitel("SB"));
    w.setOffscreen(true);
    w.setSizeToContent(glaze::SizeToContent::Height);
    static Varv v;
    v         = Varv{};
    v.fönster = &w;
    v.steg = {
        [&] {
            check(nära(w.contentSize().h, 122.0f, 1.0f),
                  "SB1 Height: 86 uppifrån + 36 nedifrån",
                  tal(static_cast<int>(w.contentSize().h)));
            // Luften under b — ett råfält: ritningens fråga anpassar.
            w.b.bottom = 12;
            w.invalidate();
        },
        [&] {
            check(nära(w.contentSize().h, 134.0f, 1.0f),
                  "SB2 b.bottom = 12 är luft ovanför raden mot nederkanten",
                  tal(static_cast<int>(w.contentSize().h)));
            w.a.hide();
        },
        [&] {
            check(nära(w.contentSize().h, 134.0f, 1.0f),
                  "SB3 dolt a — b står kvar efter hålet (postat)",
                  tal(static_cast<int>(w.contentSize().h)));
            w.a.show();
            w.setSizeToContent(glaze::SizeToContent::WidthAndHeight);
        },
        [&] {
            check(nära(w.clientSize().w, 110.0f, 1.0f),
                  "SB4 WidthAndHeight: bredden max(110, 100, 70) — raderna "
                  "står över varandra",
                  tal(static_cast<int>(w.clientSize().w)));
            w.close();
        },
    };
    glaze::app().post([] { v.kör(); });
    w.run();
    if (v.i == 0)
        hoppa("SB");
}

// SG: INNEHÅLLET SOM GOLV — `FoljFonster` (innehållet 156: Top-raderna 30, 40,
// 50 och fotraden 36) med `Height` + `GrowOnly`. Storleken är max(begäran
// eller aktuell, innehållet) och krymper aldrig själv.
void delSG()
{
    std::printf("\n=== SG. innehållet som golv (GrowOnly) ===\n");
    FoljFonster w(unikTitel("SG"));
    w.setOffscreen(true);
    w.setSizeToContent(glaze::SizeToContent::Height);
    w.setSizeToContentMode(glaze::SizeToContentMode::GrowOnly);
    static Varv v;
    v         = Varv{};
    v.fönster = &w;
    const auto höjd = [&w] { return tal(static_cast<int>(w.clientSize().h)); };
    v.steg = {
        [&] {
            check(nära(w.clientSize().h, 500.0f, 0.5f),
                  "SG1 konstruktorns 500 över innehållets 156 står kvar", höjd());
            check(!w.isFixedSize(), "SG1 GrowOnly är inte fast");
            w.setClientSize(300, 100);
        },
        [&] {
            check(nära(w.clientSize().h, 156.0f, 0.5f),
                  "SG2 begäran 100 under golvet ⇒ 156", höjd());
            w.setClientSize(300, 400);
        },
        [&] {
            check(nära(w.clientSize().h, 400.0f, 0.5f),
                  "SG3 begäran 400 över golvet ⇒ 400", höjd());
            w.rad[1].hide();
        },
        [&] {
            check(nära(w.clientSize().h, 400.0f, 0.5f),
                  "SG4 en dold rad sänker golvet men krymper inte fönstret",
                  höjd());
            w.setClientSize(300, 120);
        },
        [&] {
            check(nära(w.clientSize().h, 120.0f, 0.5f),
                  "SG5 det sänkta golvet (116) släpper 120", höjd());
            w.rad[1].show();
        },
        [&] {
            check(nära(w.clientSize().h, 156.0f, 0.5f),
                  "SG6 raden visad igen ⇒ fönstret växer till 156", höjd());
            // Appens max UNDER innehållet: innehållet vinner, och begäran 400
            // stannar vid golvet — appens max lyfts aldrig över innehållet.
            w.setMaximumSize(0, 100);
            w.setClientSize(300, 400);
        },
        [&] {
            check(nära(w.clientSize().h, 156.0f, 0.5f),
                  "SG7 appens max 100 under innehållet ⇒ 156", höjd());
            check(w.maximumSize().h == 100.0f,
                  "SG7 maximumSize() svarar med appens begäran");
            w.setMaximumSize(0, 0);
            w.setClientSize(300, 400);
        },
        [&] {
            check(nära(w.clientSize().h, 400.0f, 0.5f),
                  "SG8 utan max: begäran 400", höjd());
            w.setSizeToContentMode(glaze::SizeToContentMode::GrowAndShrink);
        },
        [&] {
            check(nära(w.clientSize().h, 156.0f, 0.5f),
                  "SG8 GrowAndShrink låser till innehållet direkt", höjd());
            check(!w.isFixedSize(),
                  "SG8 Height är inte fast (bara höjden låst)");
            w.close();
        },
    };
    glaze::app().post([] { v.kör(); });
    w.run();
    if (v.i == 0)
        hoppa("SG");
}

// CM: GRÄNSERNA OCH DEN LÅSTA STORLEKEN I INNEHÅLLSMÅTT — offscreen
// och ramlöst, så chromet är bara STATUSRADEN; dess höjd läses ur stilen HÄR
// och avrundas till hela px (innehållsstorlekens regel), aldrig ur
// produktionens `chromeInsetPx`.
void delCM()
{
    std::printf("\n=== CM. gränserna och den låsta storleken i innehållsmått "
                "===\n");
    glaze::AppWindow w(unikTitel("CM"), 400, 300, gfx::Chrome::None);
    w.setOffscreen(true);
    const float rad = std::floor(w.style().statusBarH + 0.5f);
    w.setMinimumContentSize(200, 150);
    check(w.minimumSize().h == 150.0f && w.minimumContentSize().h == 150.0f &&
              w.minimumSize().w == 200.0f,
          "CM1 utan chrome: innehållsgränsen är klientgränsen",
          tal(static_cast<int>(w.minimumSize().h)));
    w.statusBar.show();
    check(w.minimumSize().h == 150.0f + rad &&
              w.minimumContentSize().h == 150.0f,
          "CM1 statusraden läggs på klientgränsen, innehållsgränsen står "
          "still (före show)",
          tal(static_cast<int>(w.minimumSize().h)));
    static Varv v;
    v         = Varv{};
    v.fönster = &w;
    const auto mått = [&w] {
        return tal(static_cast<int>(w.clientSize().w)) + " × " +
               tal(static_cast<int>(w.clientSize().h));
    };
    v.steg = {
        [&] { w.setFixedContentSize(260, 120); },
        [&] {
            check(w.isFixedSize(), "CM2 setFixedContentSize ⇒ isFixedSize()");
            check(nära(w.contentSize().w, 260.0f) &&
                      nära(w.contentSize().h, 120.0f),
                  "CM2 innehållsytan 260 × 120", mått());
            check(nära(w.clientSize().h, 120.0f + rad),
                  "CM2 klientytan bär statusraden", mått());
            w.statusBar.hide();
        },
        [&] {
            check(nära(w.contentSize().h, 120.0f),
                  "CM3 statusraden dold — innehållsytan står still", mått());
            check(nära(w.clientSize().h, 120.0f),
                  "CM3 klientytan krymper med raden (gränsen först)", mått());
            check(w.isFixedSize(), "CM3 fortfarande fast");
            w.setFixedContentSize(0, 50);   // loggas och ändrar ingenting
            check(w.minimumContentSize().h == 120.0f && w.isFixedSize(),
                  "CM4 setFixedContentSize(0, 50) ignoreras");
            w.setMinimumContentSize(-1, 10);   // likaså
            check(w.minimumContentSize().h == 120.0f,
                  "CM4 ett negativt innehållsmått ignoreras");
            w.setMinimumContentSize(0, 0);
            w.setMaximumContentSize(0, 0);
            check(!w.isFixedSize() && w.maximumSize().h == 0.0f,
                  "CM5 upplåst med (0, 0)");
            // Klientformen satt SIST gäller — och läses i innehållsmått.
            w.setMinimumSize(300, 200);
            w.statusBar.show();
            check(w.minimumSize().h == 200.0f &&
                      w.minimumContentSize().h == 200.0f - rad,
                  "CM6 setMinimumSize efter innehållsformen: klientmått",
                  tal(static_cast<int>(w.minimumContentSize().h)));
            w.close();
        },
    };
    glaze::app().post([] { v.kör(); });
    w.run();
    if (v.i == 0)
        hoppa("CM");
}

// BG: BREDDGRUPPENS OMRITNING — två paneler med var sin etikett i gruppen
// "g". En text eller en synlighet i den ena ändrar den andras bredd, och den
// andra är inte dirty: fönstret ritas om, och den andras stämpel stegas.
struct MätLabel : glaze::Label {
    using glaze::Label::Label;
    using glaze::Widget::damageStamp;
};

struct GruppFonster : glaze::AppWindow {
    glaze::Panel p1, p2;
    MätLabel     A{"ab"}, B{"abcd"};
    explicit GruppFonster(const std::string& titel)
        : glaze::AppWindow(titel, 300, 200, gfx::Chrome::None)
    {
        p1.name   = "p1";
        p1.width  = 200;
        p1.height = 40;
        p2.name   = "p2";
        p2.y      = 60;
        p2.width  = 200;
        p2.height = 40;
        A.name    = "A";
        B.name    = "B";
        A.setWidthGroup("g");
        B.setWidthGroup("g");
        p1.adopt(A);
        p2.adopt(B);
        root().adopt(p1);
        root().adopt(p2);
    }
};

void delBG()
{
    std::printf("\n=== BG. breddgruppens omritning ===\n");
    GruppFonster w(unikTitel("BG"));
    w.setOffscreen(true);
    static Varv v;
    v         = Varv{};
    v.fönster = &w;
    float före = 0.0f;
    v.steg = {
        [&] {
            w.window().renderFrame();
            före = w.B.actualSize().w;
            check(före > 0.0f && w.A.actualSize().w == före,
                  "BG1 lika breda efter en bildruta",
                  tal(w.A.actualSize().w) + " mot " + tal(före));
            const std::uint64_t s = w.B.damageStamp();
            w.A.setText("abcdefghij");
            check(w.B.damageStamp() != s,
                  "BG1 A.setText stegar B:s stämpel (fönstret ritas om)");
        },
        [&] {
            w.window().renderFrame();
            check(w.A.actualSize().w == w.B.actualSize().w &&
                      w.B.actualSize().w > före,
                  "BG1 efter bildruta och svans lika breda igen, och bredare",
                  tal(w.B.actualSize().w));
            före = w.B.actualSize().w;
            const std::uint64_t s = w.B.damageStamp();
            w.A.hide();
            check(w.B.damageStamp() != s,
                  "BG2 A.hide() stegar B:s stämpel (shownChanged)");
        },
        [&] {
            w.window().renderFrame();
            check(w.B.actualSize().w < före,
                  "BG2 B smalare när A är dold",
                  tal(w.B.actualSize().w) + " mot " + tal(före));
            w.close();
        },
    };
    glaze::app().post([] { v.kör(); });
    w.run();
    if (v.i == 0)
        hoppa("BG");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("geometry_gate — fönstrets plats, läge och ram\n");
    delP();
    delKM();
    delW();
    delL();   // visar riktiga fönster ett ögonblick, som W3
#ifdef GLAZE_BACKEND_D3D11
    // K visar riktiga fönster, efter W — där är startvisningen redan
    // förbrukad (`förbrukaStartvisningen`). Platsen: en bit in i primärens
    // arbetsyta, som W:s.
    if (const std::optional<Primär> pri = läsPrimär())
        delK(static_cast<int>(std::lround(pri->wx + 40)),
             static_cast<int>(std::lround(pri->wy + 40)));
    else
        inteHär("K kräver en skärmrymd (gfx::screens() är tom)");
#endif
#ifdef GLAZE_BACKEND_LINUX
    delKL();
#endif
    delSC();   // offscreen
    delSB();   // offscreen, likaså de två nedan
    delSG();
    delCM();   // offscreen
    delBG();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "GEOMETRY_GATE GRÖN" : "GEOMETRY_GATE FÄLLD",
                g_cases, g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
