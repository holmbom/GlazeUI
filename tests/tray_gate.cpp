// tray_gate — SYSTEMFÄLTETS grind.
//
// Kör:  ./build-test/tests/tray_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett fönsterblock
//       kunde inte köras (ingen display) — ctest räknar den som SKIPPED,
//       aldrig grön
//
// GRINDEN LÄGGER ALDRIG EN IKON I UTVECKLARENS SYSTEMFÄLT. Windows
// minns varje programfil som någon gång visat en, och macOS ritar den i
// menyraden — så `TrayIcon::show()` anropas BARA med Linux-backenden, som
// saknar systemfält och där `show()` bara lagrar begäran (D1, D3, D5, D6).
// Resten mäts genom den rena modellen (src/tray_model.h), registret
// (src/tray_registry.h) och riktiga men DOLDA `TrayIcon`-objekt: en ikon som
// aldrig visats gör inte ett enda gfx-anrop (tray_icon.cpp).
//
//   M  ANIMATIONEN och MENYNS ANKARE, headless mot handräknat förväntat värde.
//      `TrayAnimation`: M1 inga bilder ⇒ ingen timer och den stilla ikonen
//      (−1) · M2 en bild ⇒ bild 0 utan timer · M3 tre bilder ⇒ tick ger 1,
//      2, 0, 1 · M4 dold ⇒ ingen timer och tick står still · M5 stop ⇒
//      index 0 och den stilla ikonen, nästa start på första bilden · M6
//      färre bilder ⇒ ett index utanför börjar om. `trayMenuAnchor`: M7
//      aktivitetsfältet nedtill, upptill, till vänster och till höger,
//      menyraden (macOS), en andra skärm med negativa koordinater, en ikon
//      inne i arbetsytan, en som sticker ut och en tom arbetsyta.
//   R  ROUTNINGEN (`routeTray`) i en handskriven tabell: R1 dold ⇒
//      ingenting och hovern glöms, i alla sex händelserna · R2 klick och
//      dubbelklick ⇒ handlern, under en modal ⇒ modalen lyfts i stället ·
//      R3 högerklick ⇒ handlern och menyn, under en modal ⇒ modalen lyfts ·
//      R4 hovern: Enter två gånger ger EN in, Leave utan Enter ingenting, och
//      en modal spärrar inte · R5 Tick tickar också under en modal, aldrig
//      dold. Routningen svarar bara OM händelsen går fram (`fire`); VILKEN
//      handler som fyras är händelsens egen, och den mäter D5.
//   I  IKONENS RAMAR (`detail::iconFrames`, src/window_icon.h), headless:
//      taket 64 ger systemfältets sju storlekar och fönstrets lista alla
//      tio, taket ändrar antalet men aldrig en ram, ett tak mellan två
//      storlekar, och CACHEN med taket i nyckeln — i BÅDA ordningarna (64
//      före 256 och 256 före 64), med var sitt bildpar så att båda börjar i
//      en tom cache.
//   V  VOKABULÄREN på en riktig, dold `TrayIcon`: V0 `isSupported()` per
//      backend · V1 bilderna i kollektionsvokabulären (addFrame × 3,
//      setFrame ersätter, utanför ⇒ no-op också vid index == antalet,
//      setFrames({}) tömmer) · V2 intervallet (förval 500, golvet 16) · V3
//      menyn med `{}` som rot, undermeny, `itemAtPath`, `setHandler` på rätt
//      post och okänd stig utan krasch · V4 tooltipen bär TOKENET och följer
//      språket · V5 animationen är en BEGÄRAN · V6 förvalen och bilderna ·
//      V7 `Handler<Args, TrayIcon>`: båda formerna, sender är `TrayIcon*`,
//      och en handler som vill ha `Widget*` tas inte emot · V8 bilder ur
//      pixlar: `setFrames` med Bitmaps och `frame(i)`,
//      `setIcon(samma bild)` efter `setPixels` är ett NYTT värde (så ikonen
//      läggs om), och menypostens tre platta mutatorer med `{}`-roten.
//   D  DISPATCHEN (`detail::trayDispatch`) och REGISTRET (`trayIdOf`): D1
//      okänt id ⇒ ingenting · D2 dold ikon ⇒ ingen handler och ingen meny,
//      inte heller med `openMenu()` när ett klicks ankare finns · D3 en
//      riven ikons id ⇒ ingenting · D4 id:t är det lägsta lediga och frigörs
//      när ikonen rivs. BARA med Linux-backenden (där kan ingen ikon synas):
//      D5 en synlig ikons handlers i rätt ordning med ikonen som sender,
//      modalen och hovern genom dölj/visa · D6 en handler som river ikonen —
//      och en som river den OCH skapar en ny som får samma id.
//   S  AKTIVITETSFÄLTET (bara D3D11; offscreen-fönster, HWND via
//      `FindWindowW` och en titel unik per process): S1 `setShowInTaskbar
//      (false)` ⇒ fönstret är ÄGT av ett osynligt, oägt fönster, har inget
//      WS_EX_APPWINDOW och är inget verktygsfönster (Alt+Tab kvar) · S2
//      förvalet ⇒ ingen ägare · S3 ett ÄGT fönster med `setShowInTaskbar
//      (false)` ⇒ ägaren är glaze-ägaren · S4 ägaren, läst FÖRE stängningen,
//      finns inte efter · S5 attributet satt efter `show()` växlar inget i
//      drift men gäller nästa öppning.
//   H  DOLD START (OS-oraklet `IsWindowVisible` bara med D3D11, glaze-sidan
//      överallt): H1 `hide()` + `run()` ⇒ öppet, dolt, onLoad 1, onShow 0,
//      onHide 0 — sedan `show()` ⇒ synligt, onShow 1 · H2 en MODAL med
//      `hide()` före ⇒ öppnas synlig · H3 begäran förbrukas: nästa öppning
//      är synlig (offscreen — glaze-sidan räcker). H1 och H2 VISAR ett
//      riktigt fönster ett ögonblick: det är vad de mäter.
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · tray_model.h: `frameCount >= 2` → `>= 1` i wantsTimer ........... M2
//   · tray_model.h: tick() utan `if (wantsTimer())` .................... M4
//   · tray_model.h: stop() utan `index = 0` ............................ M5
//   · tray_model.h: setFrameCount utan `index = 0` ..................... M6
//   · tray_model.h: y1 klämd mot [work.y, work.y] ...................... M7
//   · tray_model.h: trayMenuAnchor som svarar `icon` rakt av ........... M7
//   · tray_model.h: `if (!visible)`-grenen bort i routeTray ............ R1
//   · tray_model.h: `if (modalOpen)`-grenen bort ................... R2, R3
//   · tray_model.h: `r.fire = hovered != in;` → `r.fire = true;` ....... R4
//   · window_icon.cpp: `maxSide` bort ur cachenyckeln .................. I1
//   · window_icon.cpp: `if (sida > maxSide) break;` bort ............... I1
//   · tray_icon.cpp: setFrame som tar index == antalet (push_back) ..... V1
//   · tray_icon.cpp: setInterval utan klampning ........................ V2
//   · tray_icon.cpp: add-satserna stiger från led 1 (fel rot) .......... V3
//   · tray_icon.cpp: setToolTip lagrar `TrText(t.str())` ............... V4
//   · shape.cpp: ImageSource utan versionens snapshot .................. V8
//   · menu.cpp: setMenuItemChecked sätter `checkable` i stället ........ V8
//   · tray_icon.cpp: dispatch skickar `true` i stället för t.visible_ .. D2
//   · tray_icon.cpp: openMenu utan `!visible_` (Windows, macOS) ......... D2
//   · tray_icon.cpp: dispatch skickar `false` i stället för
//     `modalTop != nullptr` (bara Linux-backenden) ...................... D5
//   · tray_icon.cpp: dispatch ger routeTray en KOPIA av hovern (bara
//     Linux-backenden) .................................................. D5
//   · tray_icon.cpp: hide() nollar inte hovern (bara Linux-backenden) .. D5
//   · tray_icon.cpp: `levande` struntar i generationen ..... D6, men BARA
//     under ASan (Linux-backenden) — se "inte mäter" nedan
//   · tray_icon.cpp: withdraw tar inte bort posten ..................... D4
//   · tray_icon.cpp: enroll tar nästa i följd i stället för en lucka ... D4
//   · app_window.cpp: `desc.showInTaskbar = showInTaskbar_;` bort ...... S1
//   · d3d11_window.cpp: ägartricket bort / D3D11 läser inte desc ....... S1
//   · d3d11_window.cpp: `!desc.owner` bort ur villkoret ................ S3
//   · d3d11_window.cpp: destroyTaskbarOwner() bort i shutdown .......... S4
//   · app_window.cpp: `desc.hidden = offscreen_;` (dold start når inte
//     OS-fönstret) ..................................................... H1
//   · app_window.cpp: `fireLifecycle(onShow)` också vid dold start ..... H1
//   · app_window.cpp: `dold = hidden_;` (modalskyddet bort) ............ H2
//   · app.cpp executeClose: `w->hidden_ = false;` bort .................. H3
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · en RIKTIG ikon i Utforskaren eller menyraden: inga Shell_NotifyIcon-
//     eller NSStatusItem-anrop, ingen TaskbarCreated, ingen hovertimer,
//     ingen DPI och ingen tooltip i OS:et — doc/os/windows/TESTS.md och
//     doc/os/macos/TESTS.md.
//   · menyn: värdfönstrets aktivering, menyns placering vid ikonen, att ett
//     klick utanför stänger den, och att `openMenu()` öppnar den i
//     PUMP-SVANSEN — `isMenuOpen()` svarar true medan öppningen väntar, och
//     `closeMenu()`/`hide()` avbryter den. Allt det kräver en synlig ikon på
//     en plattform med systemfält, alltså `show()` på Windows eller macOS.
//   · dispatchen till en SYNLIG ikon på Windows och macOS — den mäts bara
//     med Linux-backenden (D5, D6), som inte körs här, och i
//     handtest. På Windows mäter D1 och D3 bara att ingenting kraschar: en
//     dold ikon fyrar ändå ingenting.
//   · D6:s GENERATION utan sanerare. Med Linux-backenden är det enda
//     dispatchen gör efter handlern att öppna menyn, och det gör ingenting
//     utan systemfält — en dispatch som struntade i generationen läser då
//     bara den rivna ikonens minne (en use-after-free som ASan fäller) och
//     passerar annars. Fallet mäter att ingenting kraschar, att handlern
//     körs en gång och att den nya ikonen varken nås av samma händelse
//     eller missas av nästa.
//   · tooltipens omtryckning vid språkbyte (`trayLanguageChanged`): den
//     anropar fasaden, och fasaden får ingen ikon här.
//   · macOS: Metal-delarna provas på macOS (doc/os/macos/TESTS.md).
//   · aktivitetsfältets KNAPP: S mäter Microsofts regel (ett ägt fönster utan
//     WS_EX_APPWINDOW får ingen knapp), inte vad Utforskaren ritar, och inte
//     den minimerade remsan.
//   · STARTUPINFO-regeln: startades processen med SW_HIDE byter Windows ut
//     kommandot i den första ShowWindow mot SW_HIDE. Grinden förbrukar den
//     regeln på ett eget fönster före H1 (`förbrukaStartvisningen`, utskrivet
//     när det sker) — att en DOLD START i en sådan process visas första
//     gången `show()` anropas är alltså inte mätt.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <gfx/tray.h>
#include <glaze/glaze.h>

#include "glaze_assets/image.h"   // kIconSizes — I0:s korskontroll
#include "tray_model.h"           // intern — den rena modellen (Glaze/widgets/src)
#include "tray_registry.h"        // intern — dispatchen och trayIdOf
#include "window_icon.h"          // intern — ikonens ramar

#ifdef GLAZE_BACKEND_D3D11
// Samma vakter som backendens d3d11_backend.h: MinGW:s libstdc++ definierar
// redan NOMINMAX, och en ovillkorlig omdefinition är en varning.
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

// Ett block som INTE kunde köras — ingen display. Räknas, så att grinden
// svarar 77 och ctest visar SKIPPED i stället för grönt.
void hoppa(const char* vad)
{
    std::printf("  --   %s hoppas över: inget fönster kunde skapas (ingen "
                "display)\n",
                vad);
    ++g_skips;
}

// Ett block som inte FINNS på den här plattformen — skrivet, aldrig tyst,
// men inget hopp: det är en avgränsning och inte miljöns.
void inteHär(const char* vad)
{
    std::printf("  ..   %s\n", vad);
}

std::string tal(long long v) { return std::to_string(v); }

std::string rekt(const gfx::Rectf& r)
{
    return "{" + std::to_string(r.x) + ", " + std::to_string(r.y) + ", " +
           std::to_string(r.w) + ", " + std::to_string(r.h) + "}";
}

std::string lista(const std::vector<int>& v)
{
    std::string s;
    for (const int x : v)
        s += (s.empty() ? "" : ",") + std::to_string(x);
    return "{" + s + "}";
}

std::string lista(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : " ") + x;
    return s.empty() ? "(inget)" : s;
}

// Ett unikt fönsternamn per process: på Windows är titeln FindWindowW-nyckeln,
// och två samtidiga körningar får inte hitta varandras fönster (pump_gate).
std::string unikTitel(const char* del)
{
#ifdef GLAZE_BACKEND_D3D11
    return std::string("tray_gate-") + del + "-" +
           std::to_string(GetCurrentProcessId());
#else
    return std::string("tray_gate-") + del;
#endif
}

// Kör `steg` i fönstrets EGEN pump och svarar om det kördes. `run()` ÄR
// öppnaren — aldrig show() först (dropdown_gate-läxan: `runInternal` avvisar
// ett öppet fönster och kedjan tystnar). VAKTEN: kom fönstret aldrig upp
// ligger steget kvar i App:s kö och körs i NÄSTA blocks pump, med referenser
// in i ett block som redan är slut — där gör det då ingenting.
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

// ======================================================= M MODELLEN (headless)

void delM()
{
    std::printf("\n=== M. animationen och menyns ankare ===\n");
    using glaze::detail::TrayAnimation;

    // M1 — inga bilder: ingenting att visa utöver den stilla ikonen.
    {
        TrayAnimation förval;
        check(!förval.wantsTimer() && förval.shownFrame() == -1,
              "M1 förvalet: ostartad, ingen timer, den stilla ikonen");
        TrayAnimation a;
        a.started = true;
        a.shown   = true;
        a.setFrameCount(0);
        check(!a.wantsTimer(), "M1 inga bilder ⇒ ingen timer");
        check(a.shownFrame() == -1, "M1 inga bilder ⇒ den stilla ikonen (−1)",
              tal(a.shownFrame()));
        a.tick();
        check(a.index == 0 && a.shownFrame() == -1,
              "M1 ett tick utan bilder flyttar ingenting", tal(a.index));
    }

    // M2 — en bild: den visas, men det finns inget att växla mellan.
    {
        TrayAnimation a;
        a.started = true;
        a.shown   = true;
        a.setFrameCount(1);
        check(!a.wantsTimer(), "M2 en bild ⇒ ingen timer");
        check(a.shownFrame() == 0, "M2 en startad animation med en bild visar "
              "bild 0", tal(a.shownFrame()));
        a.tick();
        check(a.shownFrame() == 0, "M2 … och ett tick flyttar den inte",
              tal(a.shownFrame()));
    }

    // M3 — tre bilder: varvet runt.
    {
        TrayAnimation a;
        a.started = true;
        a.shown   = true;
        a.setFrameCount(3);
        check(a.wantsTimer(), "M3 tre bilder, startad och synlig ⇒ timern går");
        check(a.shownFrame() == 0, "M3 börjar på bild 0", tal(a.shownFrame()));
        const int   facit[] = {1, 2, 0, 1};
        bool        ok      = true;
        std::string fick;
        for (const int f : facit) {
            a.tick();
            fick += tal(a.shownFrame()) + " ";
            ok = ok && a.shownFrame() == f;
        }
        check(ok, "M3 fyra tick ger bild 1, 2, 0, 1", fick);

        TrayAnimation ostartad;
        ostartad.shown = true;
        ostartad.setFrameCount(3);
        check(!ostartad.wantsTimer() && ostartad.shownFrame() == -1,
              "M3 ostartad med tre bilder ⇒ ingen timer, den stilla ikonen");
    }

    // M4 — dold: timern står, och ett sent tick (eventet låg i kön) flyttar
    // ingenting.
    {
        TrayAnimation a;
        a.started = true;
        a.shown   = true;
        a.setFrameCount(3);
        a.tick();   // bild 1
        a.shown = false;
        check(!a.wantsTimer(), "M4 dold ⇒ ingen timer");
        a.tick();
        a.tick();
        check(a.index == 1, "M4 tick medan ikonen är dold flyttar ingenting",
              tal(a.index));
        a.shown = true;
        a.tick();
        check(a.index == 2, "M4 synlig igen ⇒ fortsätter där den stod",
              tal(a.index));
    }

    // M5 — stop: tillbaka till den stilla ikonen, och nästa start börjar om.
    {
        TrayAnimation a;
        a.started = true;
        a.shown   = true;
        a.setFrameCount(3);
        a.tick();
        a.tick();   // bild 2
        a.stop();
        check(!a.started && a.index == 0, "M5 stop ⇒ ostartad och index 0",
              tal(a.index));
        check(a.shownFrame() == -1 && !a.wantsTimer(),
              "M5 stop ⇒ den stilla ikonen och ingen timer");
        a.tick();
        check(a.index == 0, "M5 ett tick efter stop flyttar ingenting",
              tal(a.index));
        a.started = true;
        check(a.shownFrame() == 0, "M5 nästa start börjar på den första bilden",
              tal(a.shownFrame()));
    }

    // M6 — antalet bilder ändras.
    {
        TrayAnimation a;
        a.started = true;
        a.shown   = true;
        a.setFrameCount(3);
        a.tick();
        a.tick();   // index 2
        a.setFrameCount(2);
        check(a.index == 0, "M6 två bilder vid index 2 ⇒ börjar om på 0",
              tal(a.index));
        a.tick();   // index 1
        a.setFrameCount(5);
        check(a.index == 1, "M6 fler bilder ⇒ indexet står kvar", tal(a.index));
        a.setFrameCount(-3);
        check(a.frameCount == 0 && a.index == 0,
              "M6 ett negativt antal är inga bilder");
    }

    // M7 — menyns ankare: ikonen inklämd i arbetsytan, per axel. Förväntat värde är
    // räknat för hand ur B4:s regel ("klämd blir den en kant med höjden 0").
    struct AnkarFall {
        const char* namn;
        gfx::Rectf  ikon, yta, facit;
    };
    const AnkarFall fall[] = {
        {"M7 aktivitetsfältet nedtill ⇒ arbetsytans nederkant, höjden 0",
         {1800, 1048, 24, 24}, {0, 0, 1920, 1040}, {1800, 1040, 24, 0}},
        {"M7 aktivitetsfältet upptill ⇒ arbetsytans överkant, höjden 0",
         {1800, 8, 24, 24}, {0, 40, 1920, 1040}, {1800, 40, 24, 0}},
        {"M7 aktivitetsfältet till vänster ⇒ arbetsytans vänsterkant, bredden 0",
         {18, 900, 24, 24}, {60, 0, 1860, 1080}, {60, 900, 0, 24}},
        {"M7 aktivitetsfältet till höger ⇒ arbetsytans högerkant, bredden 0",
         {1878, 900, 24, 24}, {0, 0, 1860, 1080}, {1860, 900, 0, 24}},
        {"M7 macOS menyrad ⇒ menyradens underkant, höjden 0",
         {1200, 0, 22, 24}, {0, 25, 1440, 875}, {1200, 25, 22, 0}},
        {"M7 en andra skärm med negativa x ⇒ samma regel",
         {-100, 1050, 24, 24}, {-1920, 0, 1920, 1040}, {-100, 1040, 24, 0}},
        {"M7 en ikon inne i arbetsytan står kvar",
         {500, 500, 24, 24}, {0, 0, 1920, 1040}, {500, 500, 24, 24}},
        {"M7 en ikon som sticker ut klipps vid varje kant",
         {1910, 1030, 24, 24}, {0, 0, 1920, 1040}, {1910, 1030, 10, 10}},
        {"M7 tom arbetsyta (OS:et kunde inte svara) ⇒ ikonen orörd",
         {1800, 1048, 24, 24}, {0, 0, 0, 0}, {1800, 1048, 24, 24}},
        {"M7 arbetsyta utan höjd ⇒ ikonen orörd",
         {1800, 1048, 24, 24}, {0, 0, 1920, 0}, {1800, 1048, 24, 24}},
    };
    for (const AnkarFall& f : fall) {
        const gfx::Rectf r = glaze::detail::trayMenuAnchor(f.ikon, f.yta);
        check(r.x == f.facit.x && r.y == f.facit.y && r.w == f.facit.w &&
                  r.h == f.facit.h,
              f.namn, rekt(r));
    }
}

// ====================================================== R ROUTNINGEN (headless)

// Routningens svar och hovern efteråt som text, för felraden.
std::string utfall(const glaze::detail::TrayRoute& r, bool hovrad)
{
    return std::string(r.fire ? "går fram" : "ingenting") +
           (r.openMenu ? " +meny" : "") + (r.raiseModal ? " +lyft" : "") +
           (hovrad ? " hovrad" : " ej hovrad");
}

constexpr gfx::TrayAction kAllaHändelser[] = {
    gfx::TrayAction::Click, gfx::TrayAction::DoubleClick,
    gfx::TrayAction::ContextMenu, gfx::TrayAction::Enter,
    gfx::TrayAction::Leave, gfx::TrayAction::Tick,
};

void delR()
{
    std::printf("\n=== R. routningen ===\n");
    using TA = gfx::TrayAction;

    // R1 — en DOLD ikon gör ingenting, vad som än händer, och glömmer att
    // pekaren stod på den.
    {
        bool ok = true;
        for (const TA a : kAllaHändelser)
            for (const bool modal : {false, true})
                for (const bool hovradFöre : {false, true}) {
                    bool hovrad = hovradFöre;
                    const glaze::detail::TrayRoute r =
                        glaze::detail::routeTray(a, false, modal, hovrad);
                    ok = ok && !r.fire && !r.openMenu && !r.raiseModal &&
                         !hovrad;
                }
        check(ok, "R1 dold ikon ⇒ ingen handler, ingen meny, ingen lyft och "
                  "hovern glömd — alla sex händelserna, med och utan modal");
    }

    // R2–R5 — den synliga ikonen, rad för rad. `fire` = händelsen går fram:
    // klicken till sin handler, Enter och Leave till `onHover` (in och ut),
    // Tick till animationen.
    struct Rad {
        const char* namn;
        TA          a;
        bool        modal;
        bool        hovradFöre;
        bool        fire;
        bool        meny;
        bool        lyft;
        bool        hovradEfter;
    };
    const Rad rader[] = {
        {"R2 klick ⇒ handlern", TA::Click, false, false, true, false, false,
         false},
        {"R2 klick med pekaren på ikonen rör inte hovern", TA::Click, false,
         true, true, false, false, true},
        {"R2 klick under en modal ⇒ ingen handler, modalen lyfts", TA::Click,
         true, false, false, false, true, false},
        {"R2 dubbelklick ⇒ handlern", TA::DoubleClick, false, false, true,
         false, false, false},
        {"R2 dubbelklick under en modal ⇒ modalen lyfts", TA::DoubleClick,
         true, true, false, false, true, true},
        {"R3 högerklick ⇒ handlern OCH menyn", TA::ContextMenu, false, false,
         true, true, false, false},
        {"R3 högerklick under en modal ⇒ modalen lyfts, ingen meny",
         TA::ContextMenu, true, false, false, false, true, false},
        {"R4 Enter ⇒ in", TA::Enter, false, false, true, false, false, true},
        {"R4 Enter en gång till ⇒ ingenting (backendens dubblett)", TA::Enter,
         false, true, false, false, false, true},
        {"R4 Leave ⇒ ut", TA::Leave, false, true, true, false, false, false},
        {"R4 Leave utan Enter ⇒ ingenting", TA::Leave, false, false, false,
         false, false, false},
        {"R4 Enter under en modal ⇒ in — hovern spärras inte", TA::Enter, true,
         false, true, false, false, true},
        {"R4 Leave under en modal ⇒ ut", TA::Leave, true, true, true, false,
         false, false},
        {"R5 Tick ⇒ tick", TA::Tick, false, false, true, false, false, false},
        {"R5 Tick under en modal ⇒ tick, ingen lyft", TA::Tick, true, true,
         true, false, false, true},
    };
    for (const Rad& rad : rader) {
        bool hovrad = rad.hovradFöre;
        const glaze::detail::TrayRoute r =
            glaze::detail::routeTray(rad.a, true, rad.modal, hovrad);
        check(r.fire == rad.fire && r.openMenu == rad.meny &&
                  r.raiseModal == rad.lyft && hovrad == rad.hovradEfter,
              rad.namn, utfall(r, hovrad));
    }

    // R4 — en följd med tillståndet trätt igenom, som dispatchen gör. Det
    // som går fram är `onHover` i händelsens riktning: Enter in, Leave ut.
    {
        bool        hovrad   = false;
        const TA    följd[]  = {TA::Enter, TA::Enter, TA::Leave, TA::Leave,
                                TA::Enter};
        const bool  fram[]   = {true, false, true, false, true};
        const bool  efter[]  = {true, true, false, false, true};
        bool        ok       = true;
        std::string fick;
        for (std::size_t i = 0; i < std::size(följd); ++i) {
            const glaze::detail::TrayRoute r =
                glaze::detail::routeTray(följd[i], true, false, hovrad);
            fick += std::string(!r.fire                ? "–"
                                : följd[i] == TA::Enter ? "in"
                                                        : "ut") +
                    (hovrad ? "/h " : "/- ");
            ok = ok && r.fire == fram[i] && hovrad == efter[i];
        }
        check(ok, "R4 Enter, Enter, Leave, Leave, Enter ⇒ in, –, ut, –, in",
              fick);
    }
}

// =================================================== I IKONENS RAMAR (headless)

std::vector<int> sidor(const std::vector<gfx::IconImage>& v)
{
    std::vector<int> s;
    for (const gfx::IconImage& r : v)
        s.push_back(r.width);
    return s;
}

bool helaKvadrater(const std::vector<gfx::IconImage>& v)
{
    for (const gfx::IconImage& r : v)
        if (r.width != r.height ||
            r.rgba.size() != static_cast<std::size_t>(r.width) *
                                 static_cast<std::size_t>(r.height) * 4u)
            return false;
    return true;
}

void delI()
{
    std::printf("\n=== I. ikonens ramar (fältets tak) ===\n");
    namespace d = glaze::detail;

    // FÖRVÄNTAT VÄRDE, handskrivet: systemfältet tar storlekarna till och med 64 (B12),
    // fönstret alla. Korskontrollen mot kIconSizes säger till om listan
    // ändras — då ska det förväntade värdet ändras medvetet, inte följa med av sig självt.
    const std::vector<int> kFält  = {16, 20, 24, 32, 40, 48, 64};
    const std::vector<int> kAlla  = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    const std::vector<int> kTill50 = {16, 20, 24, 32, 40, 48};
    check(std::vector<int>(std::begin(glzassets::kIconSizes),
                           std::end(glzassets::kIconSizes)) == kAlla,
          "I0 (facit) kIconSizes är de tio storlekar facit räknar med");

    const glaze::AssetHandle stor  = d::defaultIconLarge();
    const glaze::AssetHandle liten = d::defaultIconSmall();
    check(stor.valid() && liten.valid(),
          "I1 (fixturen) förvalets två bilder finns i ramverkets butik");

    // ORDNING 1 — taket 64 FÖRST, sedan alla (paret stor + liten). Utan taket
    // i cachenyckeln hade den andra frågan fått den första listan. Ramarna
    // svarar med VÄRDE (en Bitmaps ramar cachas
    // aldrig), så cachen mäts med sin räknare: delI är processens första
    // fråga (main), och varje nytt (par, tak) är EN ny post.
    const std::size_t poster = d::iconFrameCacheSize();
    const std::vector<gfx::IconImage> f64   = d::iconFrames(stor, liten, 64);
    const std::vector<gfx::IconImage> fAlla = d::iconFrames(stor, liten);
    check(sidor(f64) == kFält, "I1 taket 64 ⇒ systemfältets sju storlekar",
          lista(sidor(f64)));
    check(sidor(fAlla) == kAlla,
          "I1 förvalstaket EFTER taket 64 ⇒ alla tio storlekar",
          lista(sidor(fAlla)));
    check(helaKvadrater(f64) && helaKvadrater(fAlla),
          "I1 varje ram är kvadratisk och hel (w × h × 4 byte)");
    check(d::iconFrameCacheSize() == poster + 2,
          "I1 taket är en egen post i cachen",
          tal(int(d::iconFrameCacheSize() - poster)));
    const std::vector<gfx::IconImage> igen64 = d::iconFrames(stor, liten, 64);
    const std::vector<gfx::IconImage> igen256 =
        d::iconFrames(stor, liten, 256);
    check(d::iconFrameCacheSize() == poster + 2 && sidor(igen64) == kFält,
          "I1 samma tak en gång till ⇒ samma lista ur cachen (ingen ny post)");
    check(sidor(igen256) == kAlla && d::iconFrameCacheSize() == poster + 2,
          "I1 förvalstaket är 256");
    bool sammaRamar = f64.size() <= fAlla.size();
    for (std::size_t i = 0; sammaRamar && i < f64.size(); ++i)
        sammaRamar = f64[i].width == fAlla[i].width &&
                     f64[i].height == fAlla[i].height &&
                     f64[i].rgba == fAlla[i].rgba;
    check(sammaRamar,
          "I1 taket ändrar antalet ramar, aldrig en ram (byte för byte)");

    // ORDNING 2 — alla FÖRST, sedan taket 64 (paret stor + ingen liten, så
    // att också den här ordningen börjar i en tom cache).
    const std::vector<gfx::IconImage> gAlla =
        d::iconFrames(stor, glaze::AssetHandle{});
    const std::vector<gfx::IconImage> g64 =
        d::iconFrames(stor, glaze::AssetHandle{}, 64);
    check(sidor(gAlla) == kAlla, "I1 utan liten bild, alla FÖRST ⇒ tio",
          lista(sidor(gAlla)));
    check(sidor(g64) == kFält,
          "I1 … och taket 64 EFTER alla ⇒ sju, inte cachens tio",
          lista(sidor(g64)));

    // Ett tak MELLAN två storlekar: till och med taket, inget över.
    const std::vector<gfx::IconImage> f50 = d::iconFrames(stor, liten, 50);
    check(sidor(f50) == kTill50, "I1 taket 50 ⇒ storlekarna till och med 48",
          lista(sidor(f50)));
}

// ================================================ V VOKABULÄREN (dold ikon)

void delV()
{
    std::printf("\n=== V. vokabulären (en riktig, dold ikon) ===\n");
    const glaze::AssetHandle a{101}, b{102}, c{103}, d{104};

    // V0 — plattformens svar.
#if defined(GLAZE_BACKEND_D3D11) || defined(GLAZE_BACKEND_METAL)
    check(glaze::TrayIcon::isSupported(),
          "V0 isSupported(): Windows och macOS har ett systemfält");
#else
    check(!glaze::TrayIcon::isSupported(),
          "V0 isSupported(): Linux har inget systemfält än (dummyn)");
#endif

    // V1 — bilderna i kollektionsvokabulären.
    {
        glaze::TrayIcon t;
        t.addFrame(a);
        t.addFrame(b);
        t.addFrame(c);
        check(t.frameCount() == 3 && t.frame(0) == a && t.frame(1) == b &&
                  t.frame(2) == c,
              "V1 addFrame × 3 ⇒ tre bilder i ordning", tal(t.frameCount()));
        t.setFrame(1, d);
        check(t.frameCount() == 3 && t.frame(0) == a && t.frame(1) == d &&
                  t.frame(2) == c,
              "V1 setFrame(1) ersätter bild 1 och rör inget annat");
        t.setFrame(5, a);
        t.setFrame(3, a);   // index == antalet: ingen tyst tillväxt
        t.setFrame(-1, a);
        check(t.frameCount() == 3 && t.frame(0) == a && t.frame(1) == d &&
                  t.frame(2) == c,
              "V1 setFrame utanför (5, 3 = antalet, −1) ⇒ no-op",
              tal(t.frameCount()));
        check(!t.frame(3).valid() && !t.frame(-1).valid(),
              "V1 frame() utanför ⇒ ett ogiltigt handtag");
        t.setFrames({c, a});
        check(t.frameCount() == 2 && t.frame(0) == c && t.frame(1) == a,
              "V1 setFrames fyller om allt", tal(t.frameCount()));
        t.setFrames({});
        check(t.frameCount() == 0, "V1 setFrames({}) ⇒ inga bilder",
              tal(t.frameCount()));
    }

    // V2 — intervallet: förvalet och golvet (500 och 16 ms).
    {
        check(glaze::TrayIcon::kDefaultIntervalMs == 500 &&
                  glaze::TrayIcon::kMinIntervalMs == 16,
              "V2 konstanterna: förval 500 ms, golv 16 ms");
        glaze::TrayIcon t;
        check(t.interval() == 500, "V2 förvalet är 500 ms", tal(t.interval()));
        t.setInterval(5);
        check(t.interval() == 16, "V2 setInterval(5) ⇒ 16", tal(t.interval()));
        t.setInterval(17);
        check(t.interval() == 17, "V2 17 står kvar", tal(t.interval()));
        t.setInterval(16);
        check(t.interval() == 16, "V2 golvet själv står kvar",
              tal(t.interval()));
        t.setInterval(0);
        check(t.interval() == 16, "V2 0 ⇒ 16", tal(t.interval()));
        t.setInterval(-40);
        check(t.interval() == 16, "V2 negativt ⇒ 16", tal(t.interval()));
        t.setInterval(400);
        check(t.interval() == 400, "V2 400 står kvar", tal(t.interval()));
    }

    // V3 — menyn: `AppIcon`s platta form med den TOMMA stigen som roten.
    {
        glaze::TrayIcon t;
        t.addItem({}, "Visa fönstret");
        t.addSubmenu({}, "Läge");
        t.addRadio("Läge", "Tyst", true);
        t.addRadio("Läge", "Högt");
        t.addCheck({}, "Animera", true);
        t.addSeparator({});
        t.addItem({}, "Avsluta");
        t.addItem({"Läge", "Finns inte"}, "X");   // okänd stig ⇒ no-op
        t.addItem("Finns inte", "Y");

        const std::vector<glaze::MenuItem>& rot = t.menu().items;
        check(rot.size() == 5, "V3 fem poster i roten — okända stigar lade "
              "inget", tal(static_cast<long long>(rot.size())));
        if (rot.size() == 5) {
            check(rot[0].text == glaze::TrText("Visa fönstret") &&
                      !rot[0].checkable && !rot[0].separator &&
                      rot[0].submenu.empty(),
                  "V3 {} ⇒ en vanlig post i ROTEN");
            check(rot[1].text == glaze::TrText("Läge") &&
                      rot[1].submenu.size() == 2,
                  "V3 undermenyn bär sina två poster",
                  tal(static_cast<long long>(rot[1].submenu.size())));
            if (rot[1].submenu.size() == 2)
                check(rot[1].submenu[0].text == glaze::TrText("Tyst") &&
                          rot[1].submenu[0].radio &&
                          rot[1].submenu[0].checked &&
                          rot[1].submenu[1].text == glaze::TrText("Högt") &&
                          rot[1].submenu[1].radio &&
                          !rot[1].submenu[1].checked,
                      "V3 radioposterna i undermenyn, den första vald");
            check(rot[2].checkable && rot[2].checked && !rot[2].radio,
                  "V3 kryssposten är ikryssad");
            check(rot[3].separator, "V3 avdelaren");
            check(rot[4].text == glaze::TrText("Avsluta"), "V3 sista posten");
        }

        check(t.itemAtPath({}) == nullptr,
              "V3 itemAtPath({}) ⇒ nullptr — roten är menyn, ingen post");
        check(rot.size() == 5 && t.itemAtPath("Visa fönstret") == &rot[0],
              "V3 itemAtPath på en rotpost");
        check(rot.size() == 5 && rot[1].submenu.size() == 2 &&
                  t.itemAtPath({"Läge", "Högt"}) == &rot[1].submenu[1],
              "V3 itemAtPath två led ned");
        check(t.itemAtPath("Finns inte") == nullptr,
              "V3 itemAtPath på en okänd stig ⇒ nullptr");

        int avsluta = 0;
        t.setHandler("Avsluta", [&avsluta] { ++avsluta; });
        int fel = 0;
        t.setHandler("Finns inte", [&fel] { ++fel; });   // loggas, ingen krasch
        t.setHandler({}, [&fel] { ++fel; });             // roten är ingen post
        bool baraAvsluta = rot.size() == 5;
        for (std::size_t i = 0; baraAvsluta && i < rot.size(); ++i)
            baraAvsluta = static_cast<bool>(rot[i].onClick) == (i == 4);
        check(baraAvsluta, "V3 setHandler satte handlern på RÄTT post — och "
                           "bara där, okänd stig och roten inräknade");
        if (rot.size() == 5) {
            glaze::ClickArgs e;
            rot[4].onClick(nullptr, e);   // menyposternas sender är värdens rot
        }
        check(avsluta == 1 && fel == 0, "V3 handlern på stigen är den som körs",
              tal(avsluta));
    }

    // V4 — tooltipen är ett TOKEN: id:t följer språket utan att appen rör
    // ikonen. Förväntat värde ur Glaze/language/framework.langtxt (id 2).
    {
        const std::string förut = glaze::language();
        glaze::TrayIcon   t;
        glaze::setLanguage("en");
        t.setToolTip(glaze::tr(glaze::str::Cancel));
        check(t.toolTipToken() == glaze::tr(glaze::str::Cancel) &&
                  t.toolTipToken().id() == glaze::str::Cancel,
              "V4 setToolTip(tr(id)) bär id:t", tal(t.toolTipToken().id()));
        check(t.toolTip() == "Cancel", "V4 … upplöst på engelska",
              t.toolTip());
        glaze::setLanguage("sv");
        check(t.toolTip() == "Avbryt",
              "V4 språkbytet slår igenom utan att appen rör ikonen",
              t.toolTip());
        t.setToolTip("Avbryt");
        check(t.toolTipToken().id() == 0 &&
                  !(t.toolTipToken() == glaze::tr(glaze::str::Cancel)),
              "V4 en literal är en literal — också med samma text (TrText:s "
              "tokensemantik)");
        glaze::setLanguage(förut);
    }

    // V5 — animationen är en BEGÄRAN, inte vad som syns.
    {
        glaze::TrayIcon t;
        check(!t.isAnimating(), "V5 förvalet: ingen animation");
        t.startAnimation();
        check(t.isAnimating(),
              "V5 startAnimation() ⇒ isAnimating() redan utan bilder och dold");
        t.addFrame(a);
        t.addFrame(b);
        check(t.isAnimating(), "V5 … och med bilder");
        t.stopAnimation();
        check(!t.isAnimating(), "V5 stopAnimation() ⇒ ingen animation");
    }

    // V6 — förvalen och bilderna.
    {
        glaze::TrayIcon t;
        check(!t.isVisible() && !t.icon().valid() && !t.smallIcon().valid() &&
                  !t.isTemplateImage() && t.frameCount() == 0 &&
                  t.toolTip().empty() && t.name.empty() && !t.isMenuOpen(),
              "V6 förvalen: dold, förvalsikonen, ingen mallbild, inga bilder, "
              "ingen tooltip, inget namn, ingen meny");
        t.setIcon(a);
        t.setSmallIcon(b);
        t.setTemplateImage(true);
        t.name = "ikonen";
        check(t.icon() == a && t.smallIcon() == b && t.isTemplateImage(),
              "V6 setIcon, setSmallIcon och setTemplateImage lagras");
        t.setIcon(glaze::AssetHandle{});
        check(!t.icon().valid() && t.smallIcon() == b,
              "V6 ett ogiltigt handtag är förvalet igen");
        t.hide();
        check(!t.isVisible(), "V6 hide() på en ikon som aldrig visats är "
                              "ofarligt");
    }

    // V7 — `Handler<Args, TrayIcon>`: sender är ikonen och inte en widget.
    {
        using H = glaze::Handler<glaze::ClickArgs, glaze::TrayIcon>;
        check(std::is_same_v<decltype(glaze::TrayIcon::onClick), H> &&
                  std::is_same_v<decltype(glaze::TrayIcon::onHover),
                                 glaze::Handler<glaze::HoverArgs,
                                                glaze::TrayIcon>> &&
                  std::is_same_v<decltype(glaze::TrayIcon::onContextMenu),
                                 glaze::Handler<glaze::TrayMenuArgs,
                                                glaze::TrayIcon>>,
              "V7 ikonens events är Handler<…, TrayIcon>");
        check(std::is_same_v<glaze::Handler<glaze::ClickArgs>,
                             glaze::Handler<glaze::ClickArgs, glaze::Widget>>,
              "V7 förvalet är Widget — varje befintlig Handler<X> oförändrad");
        check(std::is_invocable_v<const H&, glaze::TrayIcon*,
                                  glaze::ClickArgs&> &&
                  !std::is_invocable_v<const H&, glaze::Widget*,
                                       glaze::ClickArgs&>,
              "V7 avfyras med TrayIcon* och aldrig med Widget*");
        using WidgetForm = void (*)(glaze::Widget*, glaze::ClickArgs&);
        check(!std::is_assignable_v<H&, WidgetForm>,
              "V7 en handler som vill ha Widget* tas inte emot");

        glaze::TrayIcon        t;
        const glaze::TrayIcon* fick = nullptr;
        int                    n    = 0;
        t.onClick = [&](glaze::TrayIcon* s, glaze::ClickArgs&) {
            fick = s;
            ++n;
        };
        glaze::ClickArgs e;
        t.onClick(&t, e);
        check(n == 1 && fick == &t,
              "V7 fullformen tar emot ikonen som TrayIcon*");
        int m = 0;
        t.onDoubleClick = [&m] { ++m; };
        t.onDoubleClick(&t, e);
        check(m == 1, "V7 den parameterlösa formen");
        bool in = false;
        t.onHover = [&](glaze::TrayIcon* s, glaze::HoverArgs& h) {
            in = h.entered && s == &t;
        };
        glaze::HoverArgs hv;
        hv.entered = true;
        t.onHover(&t, hv);
        check(in, "V7 onHover bär HoverArgs::entered");
        t.onClick = nullptr;
        check(!t.onClick, "V7 nollställning med nullptr");
    }

    // V8 — BILDER UR PIXLAR: rutorna och ikonen är
    // `ImageSource`, en ny version av samma Bitmap är en NY bild (så den läggs
    // om i fältet), och menypostens tillstånd har de tre platta mutatorerna.
    {
        std::vector<std::uint8_t> röd(8 * 8 * 4), grön(8 * 8 * 4);
        for (std::size_t i = 0; i < röd.size(); i += 4) {
            röd[i]      = 220;
            röd[i + 3]  = 255;
            grön[i + 1] = 200;
            grön[i + 3] = 255;
        }
        glaze::Bitmap r1(8, 8, röd.data()), r2(8, 8, grön.data());
        glaze::TrayIcon t;
        t.setFrames({r1, r2, a});
        check(t.frameCount() == 3 && t.frame(0) == glaze::ImageSource(r1) &&
                  t.frame(1) == glaze::ImageSource(r2) && t.frame(2) == a &&
                  t.frame(0).kind() == glaze::ImageSource::Kind::Bitmap,
              "V8 setFrames med Bitmaps — frame(i) är samma bild");
        t.setIcon(r1);
        const glaze::ImageSource gammal = t.icon();
        r1.setPixels(0, 0, 8, 8, grön.data());
        check(gammal == t.icon(),
              "V8 setPixels ensam ändrar inte det lagrade värdet (snapshot)");
        t.setIcon(r1);
        check(!(gammal == t.icon()) && gammal.sameImage(t.icon()),
              "V8 setIcon(samma bild) efter setPixels — ett NYTT värde (som "
              "lägger om ikonen), samma bild");

        t.addItem({}, "Visa");
        t.addCheck({}, "Tyst");
        t.setItemIcon("Visa", r2);
        t.setItemEnabled("Visa", false);
        t.setItemChecked("Tyst", true);
        t.setItemChecked("Finns inte", true);   // okänd stig ⇒ logg + no-op
        const glaze::MenuItem* visa = t.itemAtPath("Visa");
        const glaze::MenuItem* tyst = t.itemAtPath("Tyst");
        check(visa && tyst && visa->icon == glaze::ImageSource(r2) &&
                  !visa->enabled && tyst->checked && t.menu().items.size() == 2,
              "V8 setItemIcon/setItemEnabled/setItemChecked med {}-roten — "
              "samma tillstånd som trädformen, okänd stig no-op");
    }
}

// ============================================ D DISPATCHEN OCH REGISTRET

// Handlernas logg, och avsändaren de ska få: ikonen själv (E6).
struct Logg {
    std::vector<std::string> rader;
    const glaze::TrayIcon*   ikon      = nullptr;
    int                      felSender = 0;

    void rad(const char* vad, const glaze::TrayIcon* sender)
    {
        rader.emplace_back(vad);
        if (sender != ikon)
            ++felSender;
    }
};

void koppla(glaze::TrayIcon& t, Logg& l)
{
    l.ikon          = &t;
    t.onClick       = [&l](glaze::TrayIcon* s, glaze::ClickArgs&) {
        l.rad("klick", s);
    };
    t.onDoubleClick = [&l](glaze::TrayIcon* s, glaze::ClickArgs&) {
        l.rad("dubbel", s);
    };
    t.onContextMenu = [&l](glaze::TrayIcon* s, glaze::TrayMenuArgs&) {
        l.rad("meny", s);
    };
    t.onHover = [&l](glaze::TrayIcon* s, glaze::HoverArgs& h) {
        l.rad(h.entered ? "in" : "ut", s);
    };
}

// Ett `EventType::Tray` som backenden hade köat: windowId 0, ikonens id och
// ankaret i placeringsrymden.
void skicka(std::uint32_t id, gfx::TrayAction a,
            glaze::AppWindow* modal = nullptr, float x = 0.0f, float y = 0.0f)
{
    gfx::Event e;
    e.type       = gfx::EventType::Tray;
    e.windowId   = 0;
    e.trayId     = id;
    e.trayAction = a;
    e.mouseX     = x;
    e.mouseY     = y;
    glaze::detail::trayDispatch(e, modal);
}

// D4 — det LÄGSTA lediga id:t (B13), frigjort när ikonen rivs. Körs FÖRST
// av allt som skapar en ikon: förväntat värde förutsätter ett tomt register.
void fallD4()
{
    using glaze::detail::trayIdOf;
    auto a = std::make_unique<glaze::TrayIcon>();
    check(trayIdOf(*a) == 1,
          "D4 den första ikonen får id 1 (registret är tomt)",
          tal(trayIdOf(*a)));
    auto b = std::make_unique<glaze::TrayIcon>();
    auto c = std::make_unique<glaze::TrayIcon>();
    check(trayIdOf(*b) == 2 && trayIdOf(*c) == 3,
          "D4 nästa två får 2 och 3",
          tal(trayIdOf(*b)) + ", " + tal(trayIdOf(*c)));
    b.reset();
    auto d = std::make_unique<glaze::TrayIcon>();
    check(trayIdOf(*d) == 2,
          "D4 en riven ikons id är ledigt igen — det lägsta lediga tas",
          tal(trayIdOf(*d)));
    a.reset();
    auto e = std::make_unique<glaze::TrayIcon>();
    check(trayIdOf(*e) == 1, "D4 luckan längst ned tas först",
          tal(trayIdOf(*e)));
    auto f = std::make_unique<glaze::TrayIcon>();
    check(trayIdOf(*f) == 4, "D4 utan lucka: nästa efter det högsta",
          tal(trayIdOf(*f)));
    c.reset();
    d.reset();
    e.reset();
    f.reset();
    auto g = std::make_unique<glaze::TrayIcon>();
    check(trayIdOf(*g) == 1, "D4 ett tömt register börjar om på 1",
          tal(trayIdOf(*g)));
}

void delD()
{
    std::printf("\n=== D. dispatchen och registret ===\n");
    using TA = gfx::TrayAction;
    fallD4();

    // D1 — okänt id: ingenting, och ingen annan ikon nås. Med
    // Linux-backenden är ikonen SYNLIG (show() lagrar bara begäran där), så
    // ett uppslag som inte gick på id:t hade fyrat dess handler.
    {
        Logg            l;
        glaze::TrayIcon t;
        koppla(t, l);
#ifdef GLAZE_BACKEND_LINUX
        t.show();
#endif
        const std::uint32_t id = glaze::detail::trayIdOf(t);
        for (const std::uint32_t okänt :
             {0u, id + 1000u, gfx::kTrayIdMax + 1u})
            for (const TA a : kAllaHändelser)
                skicka(okänt, a, nullptr, 30.0f, 40.0f);
        check(l.rader.empty(), "D1 okänt id (0, ett ledigt, över 16 bitar) ⇒ "
              "ingen handler någonstans", lista(l.rader));
#ifdef GLAZE_BACKEND_LINUX
        skicka(id, TA::Click);
        check(l.rader == std::vector<std::string>{"klick"},
              "D1 (kontroll) ikonens eget id når den", lista(l.rader));
#endif
    }

    // D2 — en DOLD ikon: ingen handler, och ingen meny — inte heller ur
    // openMenu(), fast klicken nu har lämnat ett ankare att lägga den vid.
    {
        Logg            l;
        glaze::TrayIcon t;
        koppla(t, l);
        t.addItem({}, "Post");
        const std::uint32_t id = glaze::detail::trayIdOf(t);
        for (const TA a : kAllaHändelser)
            skicka(id, a, nullptr, 30.0f, 40.0f);
        check(l.rader.empty(), "D2 dold ikon ⇒ ingen handler, inte heller "
              "hovern", lista(l.rader));
        check(!t.isMenuOpen(), "D2 högerklicket på en dold ikon öppnar ingen "
                               "meny");
        t.openMenu();
        check(!t.isMenuOpen(), "D2 openMenu() på en dold ikon öppnar ingenting");
    }

    // D3 — en RIVEN ikons id: ett event som låg kvar i kön når ingenting.
    {
        Logg l;
        auto t = std::make_unique<glaze::TrayIcon>();
        koppla(*t, l);
#ifdef GLAZE_BACKEND_LINUX
        t->show();
#endif
        const std::uint32_t id = glaze::detail::trayIdOf(*t);
        t.reset();
        for (const TA a : kAllaHändelser)
            skicka(id, a);
        check(l.rader.empty(), "D3 en riven ikons id ⇒ ingenting",
              lista(l.rader));
    }

#ifdef GLAZE_BACKEND_LINUX
    // D5 — en SYNLIG ikon (Linux: show() lagrar bara begäran — ingen ikon
    // syns): handlerna i händelsernas ordning, ikonen som sender i var och en.
    {
        Logg            l;
        glaze::TrayIcon t;
        koppla(t, l);
        t.show();
        check(t.isVisible(),
              "D5 (fixturen) show() lagrar begäran också utan systemfält");
        const std::uint32_t id = glaze::detail::trayIdOf(t);
        const TA följd[] = {TA::Enter, TA::Enter, TA::Click, TA::DoubleClick,
                            TA::ContextMenu, TA::Tick, TA::Leave, TA::Leave};
        for (const TA a : följd)
            skicka(id, a, nullptr, 30.0f, 40.0f);
        check(l.rader == std::vector<std::string>{"in", "klick", "dubbel",
                                                  "meny", "ut"},
              "D5 in, klick, dubbel, meny, ut — dubbletterna släppta, tick "
              "utan handler", lista(l.rader));
        check(l.felSender == 0, "D5 sender är ikonen i varje handler",
              tal(l.felSender));

        // Under en MODAL: klicken fyras inte (modalen lyfts — ett aldrig
        // visat fönsters raise() är en no-op), hovern går igenom.
        glaze::AppWindow modal(unikTitel("D5-modal"), 200, 120);
        l.rader.clear();
        skicka(id, TA::Click, &modal);
        skicka(id, TA::DoubleClick, &modal);
        skicka(id, TA::ContextMenu, &modal);
        check(l.rader.empty(), "D5 under en modal fyras inga klick",
              lista(l.rader));
        skicka(id, TA::Enter, &modal);
        skicka(id, TA::Tick, &modal);
        skicka(id, TA::Leave, &modal);
        check(l.rader == std::vector<std::string>{"in", "ut"},
              "D5 hovern går igenom en modal", lista(l.rader));

        // HOVERN GENOM DÖLJ/VISA: en dold ikon glömmer att pekaren stod på
        // den, så nästa Enter efter show() är en ny in. Först med händelser
        // medan den är dold (routningen glömmer) …
        l.rader.clear();
        skicka(id, TA::Enter);
        t.hide();
        skicka(id, TA::Leave);
        skicka(id, TA::Click);
        t.show();
        skicka(id, TA::Enter);
        skicka(id, TA::Leave);
        check(l.rader == std::vector<std::string>{"in", "in", "ut"},
              "D5 dold mitt i en hovring ⇒ inget ut, och efter show() en ny in",
              lista(l.rader));
        // … och utan någon händelse emellan: ikonen togs ur OS:ets fält, så
        // hide() själv glömmer hovern — annars sväljs nästa Enter.
        l.rader.clear();
        skicka(id, TA::Enter);
        t.hide();
        t.show();
        skicka(id, TA::Enter);
        skicka(id, TA::Leave);
        check(l.rader == std::vector<std::string>{"in", "in", "ut"},
              "D5 hide() glömmer hovern också utan en händelse emellan",
              lista(l.rader));
    }

    // D6 — en handler som RIVER ikonen mitt i dispatchen.
    {
        int  körningar = 0;
        auto t         = std::make_unique<glaze::TrayIcon>();
        t->show();
        t->addItem({}, "Post");   // menyn hade öppnats efter handlern
        const std::uint32_t id = glaze::detail::trayIdOf(*t);
        t->onContextMenu = [&] {
            ++körningar;
            t.reset();
        };
        skicka(id, TA::ContextMenu);
        check(körningar == 1 && !t,
              "D6 en handler som river ikonen kör färdigt utan krasch");
        skicka(id, TA::ContextMenu);
        check(körningar == 1, "D6 nästa händelse till det rivna id:t når "
                              "ingenting", tal(körningar));
    }
    // … och en som river den OCH skapar en ny, som får SAMMA id (det lägsta
    // lediga): bara generationen skiljer de två registreringarna åt.
    {
        int                              gamla = 0, nyMeny = 0, nyKlick = 0;
        std::unique_ptr<glaze::TrayIcon> ny;
        auto                             t = std::make_unique<glaze::TrayIcon>();
        t->show();
        t->addItem({}, "Post");
        const std::uint32_t id = glaze::detail::trayIdOf(*t);
        t->onContextMenu = [&] {
            ++gamla;
            t.reset();
            ny = std::make_unique<glaze::TrayIcon>();
            ny->show();
            ny->addItem({}, "Annan");
            ny->onContextMenu = [&nyMeny] { ++nyMeny; };
            ny->onClick       = [&nyKlick] { ++nyKlick; };
        };
        skicka(id, TA::ContextMenu);
        check(gamla == 1 && !t && ny, "D6 handlern rev ikonen och skapade en ny");
        check(ny && glaze::detail::trayIdOf(*ny) == id,
              "D6 (fixturen) den nya ikonen fick samma id",
              ny ? tal(glaze::detail::trayIdOf(*ny)) : "ingen");
        check(nyMeny == 0, "D6 samma händelse når inte den nya ikonen",
              tal(nyMeny));
        skicka(id, TA::Click);
        check(nyKlick == 1, "D6 nästa händelse till id:t når den nya ikonen",
              tal(nyKlick));
    }
#else
    inteHär("D5/D6 körs bara med Linux-backenden: en synlig ikon kräver show(), "
            "och show() lägger en riktig ikon i systemfältet på Windows och "
            "macOS (B15)");
#endif
}

// ============================================== S AKTIVITETSFÄLTET (D3D11)

#ifdef GLAZE_BACKEND_D3D11

HWND hwndFör(const std::string& titel)
{
    const std::wstring w(titel.begin(), titel.end());   // titlarna är ASCII
    return FindWindowW(nullptr, w.c_str());
}

bool harExStil(HWND h, LONG_PTR stil)
{
    return (GetWindowLongPtrW(h, GWL_EXSTYLE) & stil) != 0;
}

void delS()
{
    std::printf("\n=== S. aktivitetsfältet (D3D11) ===\n");

    // S1 + S4 — ett OFÄGT fönster utan knapp får ett eget dolt ägarfönster,
    // och ägaren rivs med fönstret.
    {
        const std::string titel = unikTitel("S1");
        glaze::AppWindow  w(titel, 200, 120);
        w.setOffscreen(true);
        w.setShowInTaskbar(false);
        HWND h     = nullptr;
        HWND ägare = nullptr;
        const bool kördes = iPumpen(w, [&] {
            h = hwndFör(titel);
            check(h != nullptr, "S1 (fixturen) fönstret hittas via sin titel");
            if (h) {
                ägare = GetWindow(h, GW_OWNER);
                check(ägare != nullptr,
                      "S1 setShowInTaskbar(false) ⇒ fönstret är ÄGT (ett ägt "
                      "toppnivåfönster har ingen knapp)");
                check(!harExStil(h, WS_EX_APPWINDOW),
                      "S1 inget WS_EX_APPWINDOW — det hade gett knappen "
                      "tillbaka");
                check(!harExStil(h, WS_EX_TOOLWINDOW),
                      "S1 inget verktygsfönster — fönstret står kvar i "
                      "Alt+Tab");
            }
            if (ägare) {
                check(ägare != h && !IsWindowVisible(ägare),
                      "S1 ägaren är ett eget, OSYNLIGT fönster");
                check(GetWindow(ägare, GW_OWNER) == nullptr,
                      "S1 ägaren är själv oägd — inget annat fönster i "
                      "kedjan");
                check(IsWindow(ägare) != FALSE,
                      "S4 (fixturen) ägaren finns medan fönstret är öppet");
            }
            w.close();
        });
        if (!kördes) {
            hoppa("S1/S4");
        } else {
            check(!w.isOpen() && h && !IsWindow(h),
                  "S4 (fixturen) fönstret är stängt och rivet");
            check(ägare && !IsWindow(ägare),
                  "S4 ägarfönstret rivs med fönstret — det läcker inte");
        }
    }

    // S2 — förvalet: ingen ägare, alltså en knapp.
    {
        const std::string titel = unikTitel("S2");
        glaze::AppWindow  w(titel, 200, 120);
        w.setOffscreen(true);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            check(h != nullptr, "S2 (fixturen) fönstret hittas via sin titel");
            check(w.showInTaskbar(), "S2 förvalet är true");
            check(h && GetWindow(h, GW_OWNER) == nullptr,
                  "S2 förvalet ⇒ ingen ägare — fönstret har sin knapp");
            w.close();
        });
        if (!kördes)
            hoppa("S2");
    }

    // S3 — ett ÄGT fönster med setShowInTaskbar(false): det har ingen knapp
    // ändå, och ägaren ska förbli glaze-ägaren (Z-ordningen, minimeringen).
    {
        const std::string titelÄgare = unikTitel("S3-agare");
        const std::string titelBarn  = unikTitel("S3-barn");
        glaze::AppWindow  ä(titelÄgare, 200, 120);
        ä.setOffscreen(true);
        glaze::AppWindow b(titelBarn, 160, 100);
        b.setOffscreen(true);
        b.setShowInTaskbar(false);
        const bool kördes = iPumpen(ä, [&] {
            b.show(ä);
            const HWND hÄ = hwndFör(titelÄgare);
            const HWND hB = hwndFör(titelBarn);
            check(hÄ && hB, "S3 (fixturen) båda fönstren hittas");
            check(hÄ && hB && GetWindow(hB, GW_OWNER) == hÄ,
                  "S3 det ägda fönstret behåller glaze-ägaren — inget eget "
                  "ägarfönster");
            b.close();
            ä.close();
        });
        if (!kördes)
            hoppa("S3");
    }

    // S5 — attributet efter show(): lagras, växlar inget i drift (Windows
    // kräver dölj–byt–visa) och gäller nästa öppning.
    {
        const std::string titel = unikTitel("S5");
        glaze::AppWindow  w(titel, 200, 120);
        w.setOffscreen(true);
        const bool kördes = iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            check(h && GetWindow(h, GW_OWNER) == nullptr,
                  "S5 (fixturen) öppnat med förvalet: ingen ägare");
            w.setShowInTaskbar(false);
            check(!w.showInTaskbar(),
                  "S5 attributet lagras direkt — också på ett öppet fönster");
            check(h && GetWindow(h, GW_OWNER) == nullptr,
                  "S5 ett ÖPPET fönster växlar inte i drift");
            w.close();
        });
        const bool igen = kördes && iPumpen(w, [&] {
            const HWND h = hwndFör(titel);
            check(h && GetWindow(h, GW_OWNER) != nullptr,
                  "S5 nästa öppning har ägarfönstret");
            w.close();
        });
        if (!igen)
            hoppa("S5");
    }
}

// DEN FÖRSTA ShowWindow I PROCESSEN ÄR SÄRSKILD (STARTUPINFO, `wShowWindow`):
// startade föräldern processen med STARTF_USESHOWWINDOW och SW_HIDE, byter
// Windows ut kommandot i det första ShowWindow-anropet på ett fönster med
// namnlist mot SW_HIDE. H1 och H2 mäter VÅR visning och inte förälderns
// önskan, så regeln förbrukas då först på ett eget fönster med namnlist, som
// aldrig syns (kommandot blir ju SW_HIDE). Utan den begäran görs ingenting.
void förbrukaStartvisningen()
{
    STARTUPINFOW si{};
    si.cb = sizeof si;
    GetStartupInfoW(&si);
    if (!(si.dwFlags & STARTF_USESHOWWINDOW) || si.wShowWindow != SW_HIDE)
        return;
    inteHär("processen startades med SW_HIDE — den första ShowWindow-regeln "
            "förbrukas på ett eget fönster före H1");
    const HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"",
                                   WS_OVERLAPPEDWINDOW, -32000, -32000, 1, 1,
                                   nullptr, nullptr, GetModuleHandleW(nullptr),
                                   nullptr);
    if (!h)
        return;
    ShowWindow(h, SW_SHOW);   // blir SW_HIDE — och regeln är förbrukad
    DestroyWindow(h);
}

#endif // GLAZE_BACKEND_D3D11

// ====================================================== H DOLD START

// Livscykeln räknad: hur många gånger varje event fyrats.
class LivFönster : public glaze::AppWindow {
public:
    explicit LivFönster(const std::string& titel)
        : glaze::AppWindow(titel, 240, 160)
    {
        onLoad = [this] { ++laddningar; };
        onShow = [this] { ++visningar; };
        onHide = [this] { ++döljningar; };
    }
    int laddningar = 0;
    int visningar  = 0;
    int döljningar = 0;
};

void delH()
{
    std::printf("\n=== H. dold start ===\n");
#ifdef GLAZE_BACKEND_D3D11
    förbrukaStartvisningen();
#else
    inteHär("H: OS-oraklet (IsWindowVisible) finns bara med D3D11 — här mäts "
            "glaze-sidan (isHidden och livscykeln)");
#endif

    // H1 — hide() FÖRE run(): öppet och dolt, pumpen lever, onLoad men inte
    // onShow; show() visar det sedan och fyrar onShow. Fönstret är INTE
    // offscreen: det är OS-fönstrets synlighet som mäts.
    {
        const std::string titel = unikTitel("H1");
        LivFönster        w(titel);
        w.hide();
        const bool kördes = iPumpen(w, [&] {
            check(w.isOpen(), "H1 hide() + run(): fönstret är öppet och "
                              "pumpen lever med det dolt");
            check(w.isHidden(), "H1 … och det är dolt");
            check(w.laddningar == 1, "H1 onLoad har fyrats",
                  tal(w.laddningar));
            check(w.visningar == 0,
                  "H1 onShow har INTE fyrats — fönstret har inte synts",
                  tal(w.visningar));
            check(w.döljningar == 0,
                  "H1 inte heller onHide — det har aldrig synts",
                  tal(w.döljningar));
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = hwndFör(titel);
            check(h != nullptr, "H1 (fixturen) OS-fönstret finns");
            check(h && !IsWindowVisible(h),
                  "H1 OS-fönstret skapades DOLT — det blinkar aldrig förbi");
#endif
            w.show();
            check(!w.isHidden() && w.visningar == 1 && w.laddningar == 1,
                  "H1 show() tar fram det och fyrar onShow — onLoad en gång",
                  tal(w.visningar));
#ifdef GLAZE_BACKEND_D3D11
            check(h && IsWindowVisible(h), "H1 OS-fönstret är synligt efter "
                                           "show()");
#endif
            w.close();
        });
        if (!kördes)
            hoppa("H1");
    }

    // H2 — en MODAL dialog startar aldrig dold: den hade blockerat all input
    // utan att synas. Ägaren är offscreen, dialogen inte.
    {
        const std::string titelÄgare  = unikTitel("H2-agare");
        const std::string titelDialog = unikTitel("H2-dialog");
        glaze::AppWindow  ä(titelÄgare, 200, 120);
        ä.setOffscreen(true);
        LivFönster dlg(titelDialog);
        dlg.setModal(true);
        dlg.hide();   // ignoreras (loggas) — dialogen är modal
        bool                inne = false;
        glaze::DialogResult svar = glaze::DialogResult::None;
        auto                vakt = std::make_shared<bool>(true);
        const bool          kördes = iPumpen(ä, [&] {
            // Steget i DIALOGENS nästlade pump — postat före run(ägare).
            glaze::app().post([&, vakt] {
                if (!*vakt)
                    return;
                inne = true;
                check(dlg.isOpen() && !dlg.isHidden(),
                      "H2 en MODAL med hide() före öppnas ändå synlig");
                check(dlg.visningar == 1, "H2 … och fyrar onShow",
                      tal(dlg.visningar));
#ifdef GLAZE_BACKEND_D3D11
                const HWND h = hwndFör(titelDialog);
                check(h && IsWindowVisible(h),
                      "H2 OS-fönstret är synligt — en dold modal hade låst "
                      "appen");
#endif
                dlg.closeWithResult(glaze::DialogResult::OK);
            });
            svar = dlg.run(ä);
            ä.close();
        });
        *vakt = false;
        if (!kördes)
            hoppa("H2");
        else
            check(inne && svar == glaze::DialogResult::OK,
                  "H2 (fixturen) dialogen pumpade och stängdes av kedjan");
    }

    // H3 — begäran gäller NÄSTA öppning och nollas när fönstret stängs:
    // efter en dold start öppnas fönstret synligt nästa gång. Offscreen — isHidden() och
    // livscykeln är glaze-sidan, och den räcker här.
    {
        LivFönster w(unikTitel("H3"));
        w.setOffscreen(true);
        w.hide();
        const bool kördes = iPumpen(w, [&] {
            check(w.isHidden() && w.visningar == 0,
                  "H3 (fixturen) den första öppningen är dold");
            w.close();
        });
        const bool igen = kördes && iPumpen(w, [&] {
            check(w.isOpen() && !w.isHidden(),
                  "H3 begäran är förbrukad: nästa öppning är synlig");
            check(w.visningar == 1, "H3 … och fyrar onShow",
                  tal(w.visningar));
            check(w.laddningar == 1, "H3 onLoad bara en gång per objekt",
                  tal(w.laddningar));
            w.close();
        });
        if (!igen)
            hoppa("H3");
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("tray_gate — systemfältet\n");
    delM();
    delR();
    delI();   // före fönstren: ett synligt fönster fyller ramcachen (256)
    delD();   // före V — D4 räknar med ett tomt register
    delV();
#ifdef GLAZE_BACKEND_D3D11
    delS();
#else
    std::printf("\n=== S. aktivitetsfältet ===\n");
    inteHär("S körs bara med D3D11: macOS har ingen knapp per fönster och "
            "Linux är en dummy (gfx/window.h)");
#endif
    delH();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "TRAY_GATE GRÖN" : "TRAY_GATE FÄLLD", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
