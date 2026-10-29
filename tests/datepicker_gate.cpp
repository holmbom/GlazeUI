// datepicker_gate — DATUMVÄLJARENS grind.
//
// Kör:  ./build/tests/datepicker_gate [-v] [--png <katalog>]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt.
//
// VAD DEN MÄTER:
//
//   HEADLESS — den rena std-modellen (src/date_model.h) mot ett HANDSKRIVET
//   förväntat värde, aldrig mot sig själv:
//     · giltiga och ogiltiga datum (skottdagar, månadens längd, årens gränser
//       — också ett år som `std::chrono::year` inte rymmer)
//     · ISO-texten åt båda hållen: skrivningen och tolkningen av användarens
//       text (generös med blanksteg och ensiffrigt, strikt med året och
//       avskiljaren, ett datum som inte finns är ogiltigt)
//     · klampningen, månadsstegen med dagen KLAMPAD (31 jan + 1 = sista feb)
//     · MÅNADSRUTNÄTETS FÖRSTA RUTA för kända månader och veckostarter
//       (oktober 2026 börjar en torsdag — räknat för hand i kommentaren), och
//       att varje månad ryms i sex rader för VARJE veckostart
//     · dagens datum mot grindens EGEN läsning av klockan
//     · ÖVERSIKTERNA: decenniet ett år står i, årslägets tolv
//       rutor, och vilka månader och år som RÖR gränserna (också en övre
//       gräns under den undre, och år utanför formatet)
//     · språket: veckans första dag ("1" på alla fyra) och ett par namn
//   och widgetens API: dagens datum som standard, ogiltigt orört, klampning
//   vid LÄSNING (settrarnas ordning spelar ingen roll), användare mot program,
//   fältets tolkning, `dateText()` mot `text()`, setProp och inredningen som
//   inte går att lossa.
//
//   KLOCKSLAGET, headless mot ett handskrivet förväntat värde:
//     T1  modellen: `tid` (0–23, 0–59), `hhmm`, `läsTid` (en- eller tvåsiffrig
//         timme, exakt två minutsiffror, `24:00`/`12:60` avvisas) och
//         `läsIsoTid` (blanksteg eller `T`, bara datumet ⇒ ingen tid)
//     T2  `setTime` giltigt/ogiltigt, och fältets text med och utan tid
//     T3  fältets tolkning med tiden: `T` och blanksteg, bara datumet behåller
//         tiden, ett klockslag som inte finns lämnar värdet orört
//     T4  en commit som ÄNDRAR BARA TIDEN fyrar `onChange` + `onCommit`
//     T5  kalenderns val (`dateFromUser`) behåller tiden
//   Sabotagen för dem: `läsIsoTid` utan tidsgrenen (T3), `commitText` som
//   jämför bara datumet (T4), `dateFromUser` som nollar tiden (T5).
//
//   MED FÖNSTER — ett DOLT ägarfönster och popupen som RIKTIGT popupfönster,
//   allt genom ÄGARENS riktiga ingång (`AppWindow::handleEvent`):
//     · ett tabbstopp; fokus i fältet; Ned, F4 och knappen öppnar
//     · månadsvyn har fokus; markören börjar på datumet; pilarna (dag/vecka),
//       PageUp/PageDown (månad), Ctrl+PageUp/Down (år) och Home/End flyttar
//       markören UTAN event; Enter VÄLJER och stänger — ett event
//     · Escape och Tab stänger utan ändring (Tab flyttar inte fokus)
//     · gränserna: markören stannar inom dem, i alla riktningar
//     · fältet: COMMIT (inte LIVE): att skriva en hel giltig
//       text ändrar ingenting förrän Enter, som ger ett event och skriver om
//       texten; ett tecken som inte hör till ett datum sväljs
//     · HJULET över den STÄNGDA datumväljaren ändrar inte datumet (inte heller
//       med fokus) och bubblar till den rullande föräldern
//     · popupen TAR TANGENTBORDET (`AppWindow::setPopupTakesKeys`):
//       den bär attributet, och en pil i ÄGARENS ingång flyttar markören med
//       fältets fokus orört — popupens egen ingång, den Waylands grab
//       levererar till
//     · knappen TOGGLAR; en inaktiverad öppnar aldrig; öppen popup NEKAR
//       `unadopt`; programmatisk setDate stänger en öppen popup
//     · LÄGENA: Ctrl+Upp går upp till månaderna och åren (och står
//       kvar i det översta), pilarna/PageUp/PageDown (med och utan Ctrl)/
//       Home/End stegar i lägets enhet, Enter går NED ett läge i taget utan
//       event och väljer först bland dagarna, en ny öppning börjar bland
//       dagarna, Escape och Tab stänger i en översikt utan ändring,
//       markören stannar inom gränserna i varje läge, och 29 februari ett år
//       fram är den 28:e
//
// VAD DEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · MUSEN I POPUPEN — tryck på en dag, en pil, TITELN (upp ett läge) och en
//     månad eller ett år (ned ett läge), och hjulet som bläddrar en sida.
//     Popupens ingång är protected, och ett OS-klick vore plattformskod.
//     Tangentvägen bär samma `flytta`/`välj`/`upp`, ett tryck på en
//     översiktsruta flyttar markören och går sedan ENTERS väg
//     (`väljMarkören`), och varje rutas träff och ritning delar EN geometri
//     (`dagRuta`/`översiktRuta`/`titelKnapp`). Kvar omätt är bara rutans
//     index → markören, och tryckets träff.
//   · RITNINGEN — dagens markering, valet, de släckta dagarna och
//     veckodagarnas ordning i bild. `--png` skriver popupen per UI × läge att
//     ÖGNA; att kontrollen RITAR mäter pixel_gate (fixturens grupp 23).
//
// SABOTAGEN som ska fälla den: `dateFromUser` utan event ·
// ägarens vidarebefordran borttagen (`AppWindow::
// forwardToKeyPopup` ur `handleEvent`, eller popupens `setPopupTakesKeys(true)`
// bort) · `förstaRutan` som räknar från söndag · en markör
// som inte klampas till gränserna · ett hjul som stegar datumet. Översikterna:
// Ctrl+Upp som inte går upp · Enter bland åren rakt
// till dagarna · decenniet förskjutet ett år · `månadenInom` utan den övre
// gränsen.
//
// INTE HÄR: Waylands DIREKTA väg (popupens grab) — samma
// `handleEvent` som ägarens vidarebefordran anropar, protected utifrån.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "date_model.h"   // Glaze/widgets/src — den rena modellen

namespace {

namespace dm = glaze::datemodel;
using Datum  = std::chrono::year_month_day;

int         g_fails   = 0;
int         g_skips   = 0;
int         g_cases   = 0;
bool        g_verbose = false;
std::string g_png;

// Fältets redigeringsgenvägar (Ctrl+A) är plattformens — Cmd på macOS, Ctrl
// annars. Kalenderns Ctrl+Upp/PageUp tar båda och rörs inte.
#ifdef __APPLE__
constexpr std::uint32_t kRedigera = gfx::kModCmd;
#else
constexpr std::uint32_t kRedigera = gfx::kModCtrl;
#endif

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

// Grindens EGNA verktyg — förväntat värde får aldrig vara produktionens.
Datum D(int y, unsigned m, unsigned d)
{
    return Datum{std::chrono::year{y}, std::chrono::month{m},
                 std::chrono::day{d}};
}
std::string txt(const Datum& d)
{
    if (!d.ok())
        return "(ogiltigt)";
    char b[24];
    std::snprintf(b, sizeof b, "%d-%u-%u", static_cast<int>(d.year()),
                  static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return b;
}
// Dagens datum, läst av grinden SJÄLV ur C-bibliotekets lokala tid.
Datum grindensIdag()
{
    const std::time_t nu = std::time(nullptr);
    const std::tm     t  = *std::localtime(&nu);
    return D(t.tm_year + 1900, static_cast<unsigned>(t.tm_mon + 1),
             static_cast<unsigned>(t.tm_mday));
}

struct Prov : glaze::DatePicker {
    using glaze::DatePicker::dateFromUser;
    using glaze::DatePicker::textFromUser;
};

glaze::TextInput* fältet(glaze::DatePicker& p)
{
    for (glaze::Widget* b : p.children())
        if (auto* t = dynamic_cast<glaze::TextInput*>(b))
            return t;
    return nullptr;
}

// ============================================================== MODELLEN

void fallModell()
{
    // GILTIGHETEN.
    struct G {
        int  y, m, d;
        bool ok;
    };
    const G g[] = {
        {2026, 10, 8, true},  {2028, 2, 29, true}, {2000, 2, 29, true},
        {1, 1, 1, true},      {9999, 12, 31, true}, {2026, 2, 29, false},
        {1900, 2, 29, false}, {2026, 13, 1, false}, {2026, 0, 1, false},
        {2026, 4, 31, false}, {2026, 1, 0, false},  {0, 1, 1, false},
        {10000, 1, 1, false}, {-5, 1, 1, false},    {40000, 1, 1, false},
        {65537, 1, 1, false},
    };
    for (const G& f : g) {
        Datum      ut = D(1999, 9, 9);
        const bool ok = dm::datum(f.y, f.m, f.d, ut);
        check(ok == f.ok && (ok ? ut == D(f.y, unsigned(f.m), unsigned(f.d))
                                : ut == D(1999, 9, 9)),
              "datum(" + std::to_string(f.y) + ", " + std::to_string(f.m) +
                  ", " + std::to_string(f.d) + ") är " +
                  (f.ok ? "giltigt" : "OGILTIGT och lämnar ut orörd"),
              txt(ut));
    }

    // ISO-SKRIVNINGEN.
    check(dm::iso(D(2026, 10, 8)) == "2026-10-08" &&
              dm::iso(D(5, 1, 2)) == "0005-01-02",
          "iso: fyra siffror, nollutfyllt", dm::iso(D(5, 1, 2)));

    // ISO-TOLKNINGEN.
    struct L {
        const char* text;
        bool        ok;
        int         y, m, d;
    };
    const L l[] = {
        {"2026-10-08", true, 2026, 10, 8}, {"2026-1-8", true, 2026, 1, 8},
        {" 2026-10-08\t", true, 2026, 10, 8}, {"0005-01-02", true, 5, 1, 2},
        {"", false, 0, 0, 0},           {"2026-10", false, 0, 0, 0},
        {"26-10-08", false, 0, 0, 0},   {"2026/10/08", false, 0, 0, 0},
        {"2026-10-08x", false, 0, 0, 0}, {"2026-02-30", false, 0, 0, 0},
        {"2026--10-08", false, 0, 0, 0}, {"202610-08", false, 0, 0, 0},
        {"2026-100-08", false, 0, 0, 0}, {"+2026-10-08", false, 0, 0, 0},
        {"2026-10-8 9", false, 0, 0, 0}, {"20261-10-08", false, 0, 0, 0},
    };
    for (const L& f : l) {
        Datum      ut = D(1999, 9, 9);
        const bool ok = dm::läsIso(f.text, ut);
        check(ok == f.ok &&
                  (ok ? ut == D(f.y, unsigned(f.m), unsigned(f.d))
                      : ut == D(1999, 9, 9)),
              std::string("läsIso(\"") + f.text + "\") " +
                  (f.ok ? "tolkas" : "är ogiltig och lämnar ut orörd"),
              txt(ut));
    }

    // KLAMPNINGEN.
    const Datum lo = D(2026, 3, 1), hi = D(2026, 3, 31);
    check(dm::klampa(D(2026, 3, 15), lo, hi) == D(2026, 3, 15) &&
              dm::klampa(D(2026, 2, 1), lo, hi) == lo &&
              dm::klampa(D(2027, 1, 1), lo, hi) == hi &&
              dm::klampa(D(2026, 5, 5), hi, lo) == hi,
          "klampa: inom, under, över — och en övre gräns under den undre "
          "räknas som den undre");

    // STEGEN.
    check(dm::plusMånader(D(2026, 1, 31), 1) == D(2026, 2, 28) &&
              dm::plusMånader(D(2028, 1, 31), 1) == D(2028, 2, 29) &&
              dm::plusMånader(D(2026, 3, 31), -1) == D(2026, 2, 28) &&
              dm::plusMånader(D(2026, 12, 15), 1) == D(2027, 1, 15) &&
              dm::plusMånader(D(2026, 10, 8), -12) == D(2025, 10, 8),
          "plusMånader: dagen klampas till månadens sista, årsskiftet bärs");
    check(dm::plusDagar(D(2026, 12, 31), 1) == D(2027, 1, 1) &&
              dm::plusDagar(D(2028, 3, 1), -1) == D(2028, 2, 29),
          "plusDagar: över års- och skottdagsgränsen");
    check(dm::sistaIMånaden(D(2026, 2, 10)) == D(2026, 2, 28) &&
              dm::sistaIMånaden(D(2028, 2, 10)) == D(2028, 2, 29),
          "sistaIMånaden: februari, med och utan skottdag");

    // VECKANS FÖRSTA DAG ur språkets text.
    check(dm::läsFörstaVeckodag("1") == 1 && dm::läsFörstaVeckodag("7") == 7 &&
              dm::läsFörstaVeckodag("0") == 0 &&
              dm::läsFörstaVeckodag("8") == 0 &&
              dm::läsFörstaVeckodag("") == 0 &&
              dm::läsFörstaVeckodag("11") == 0 &&
              dm::läsFörstaVeckodag("m") == 0,
          "läsFörstaVeckodag: \"1\"–\"7\", allt annat är 0");

    // MÅNADSRUTNÄTETS FÖRSTA RUTA — FÖRVÄNTAT VÄRDE RÄKNAT FÖR HAND: 1 januari 2026 är
    // en torsdag (2025-01-01 var en onsdag, 2025 är inget skottår). Fram till
    // 1 oktober är det 273 dagar = 39 veckor, alltså torsdag igen; till
    // 1 juni 151 dagar = 21 veckor + 4 ⇒ måndag; till 1 februari 31 dagar =
    // 4 veckor + 3 ⇒ söndag.
    using std::chrono::year_month;
    const year_month okt{std::chrono::year{2026}, std::chrono::October};
    const year_month jun{std::chrono::year{2026}, std::chrono::June};
    const year_month feb{std::chrono::year{2026}, std::chrono::February};
    check(dm::förstaRutan(okt, 1) == D(2026, 9, 28),
          "förstaRutan: oktober 2026, måndagsvecka ⇒ 28 september",
          txt(dm::förstaRutan(okt, 1)));
    check(dm::förstaRutan(okt, 7) == D(2026, 9, 27),
          "förstaRutan: oktober 2026, söndagsvecka ⇒ 27 september",
          txt(dm::förstaRutan(okt, 7)));
    check(dm::förstaRutan(okt, 4) == D(2026, 10, 1),
          "förstaRutan: oktober 2026, torsdagsvecka ⇒ 1 oktober",
          txt(dm::förstaRutan(okt, 4)));
    check(dm::förstaRutan(jun, 1) == D(2026, 6, 1),
          "förstaRutan: juni 2026 börjar en måndag ⇒ 1 juni",
          txt(dm::förstaRutan(jun, 1)));
    check(dm::förstaRutan(feb, 1) == D(2026, 1, 26),
          "förstaRutan: februari 2026 börjar en söndag ⇒ 26 januari",
          txt(dm::förstaRutan(feb, 1)));

    // SEX RADER RÄCKER — för varje månad i två år och varje veckostart står
    // månadens första i första raden och dess sista före ruta 42.
    int brott = 0;
    for (int år : {2026, 2027})
        for (unsigned mån = 1; mån <= 12; ++mån)
            for (int vs = 1; vs <= 7; ++vs) {
                const year_month ym{std::chrono::year{år},
                                    std::chrono::month{mån}};
                const auto start = std::chrono::sys_days(dm::förstaRutan(ym, vs));
                const auto första = std::chrono::sys_days(D(år, mån, 1));
                const auto sista =
                    std::chrono::sys_days(dm::sistaIMånaden(D(år, mån, 1)));
                if ((första - start).count() > 6 ||
                    (sista - start).count() > 41)
                    ++brott;
            }
    check(brott == 0,
          "rutnätet: varje månad 2026–2027 ryms i sex rader för varje "
          "veckostart", std::to_string(brott));

    // ÖVERSIKTERNA — FÖRVÄNTAT VÄRDE HANDSKRIVET: decenniet ett år står i,
    // årslägets tolv rutor (året före, decenniet, året efter), och vilka
    // månader och år som RÖR gränserna.
    check(dm::decennium(2026) == 2020 && dm::decennium(2020) == 2020 &&
              dm::decennium(2029) == 2020 && dm::decennium(2030) == 2030 &&
              dm::decennium(1) == 0 && dm::decennium(9999) == 9990,
          "decennium: 2026/2020/2029 ⇒ 2020, 2030 ⇒ 2030, 1 ⇒ 0, 9999 ⇒ 9990");
    check(dm::åretIRuta(2020, 0) == 2019 && dm::åretIRuta(2020, 1) == 2020 &&
              dm::åretIRuta(2020, 10) == 2029 && dm::åretIRuta(2020, 11) == 2030,
          "åretIRuta: 2019 · 2020 … 2029 · 2030");
    check(!dm::åretFinns(0) && dm::åretFinns(1) && dm::åretFinns(9999) &&
              !dm::åretFinns(10000),
          "åretFinns: formatets år 1–9999");
    {
        using std::chrono::year;
        using std::chrono::month;
        const Datum lo = D(2026, 9, 15), hi = D(2026, 10, 15);
        auto ym = [](int y, unsigned m) {
            return std::chrono::year_month{year{y}, month{m}};
        };
        check(!dm::månadenInom(ym(2026, 8), lo, hi) &&
                  dm::månadenInom(ym(2026, 9), lo, hi) &&
                  dm::månadenInom(ym(2026, 10), lo, hi) &&
                  !dm::månadenInom(ym(2026, 11), lo, hi) &&
                  !dm::månadenInom(ym(2025, 10), lo, hi),
              "månadenInom: en månad där bara en del ryms räknas, en hel "
              "utanför inte");
        check(dm::månadenInom(ym(2026, 5), D(2026, 5, 10), D(2026, 1, 1)) &&
                  !dm::månadenInom(ym(2026, 4), D(2026, 5, 10), D(2026, 1, 1)),
              "månadenInom: en övre gräns under den undre räknas som den undre");
        check(!dm::åretInom(2024, D(2025, 12, 31), D(2027, 1, 1)) &&
                  dm::åretInom(2025, D(2025, 12, 31), D(2027, 1, 1)) &&
                  dm::åretInom(2027, D(2025, 12, 31), D(2027, 1, 1)) &&
                  !dm::åretInom(2028, D(2025, 12, 31), D(2027, 1, 1)) &&
                  !dm::åretInom(0, D(1, 1, 1), D(9999, 12, 31)) &&
                  !dm::åretInom(10000, D(1, 1, 1), D(9999, 12, 31)),
              "åretInom: årets första och sista dag mot gränserna, och aldrig "
              "ett år utanför formatet");
    }

    check(dm::idag() == grindensIdag(),
          "idag: lokal tid — samma dag som grindens egen läsning",
          txt(dm::idag()) + " mot " + txt(grindensIdag()));
}

void fallSpråk()
{
    struct S {
        const char* tagg;
        const char* oktober;
        const char* måndag;
    };
    // FÖRVÄNTAT VÄRDE — handskrivet.
    const S s[] = {{"sv", "oktober", "må"},
                   {"en", "October", "Mo"},
                   {"nb", "oktober", "ma"},
                   {"fi", "lokakuu", "ma"}};
    for (const S& f : s) {
        glaze::setLanguage(f.tagg);
        const glaze::TrText förste = glaze::tr(glaze::str::FirstWeekday);
        const glaze::TrText okt    = glaze::tr(glaze::str::MonthOctober);
        const glaze::TrText mån    = glaze::tr(glaze::str::DayMonday);
        check(förste.str() == "1" && okt.str() == f.oktober &&
                  mån.str() == f.måndag,
              std::string("språket ") + f.tagg +
                  ": veckan börjar på måndag, oktober och måndag stämmer",
              förste.str() + " / " + okt.str() + " / " + mån.str());
    }
    glaze::setLanguage("sv");
}

// ============================================================ WIDGETEN

void fallVärde()
{
    {
        glaze::DatePicker p;
        check(p.date() == grindensIdag() &&
                  p.text() == dm::iso(grindensIdag()),
              "standard: DAGENS datum, i ISO-form i fältet", p.text());
        check(!p.provisionalDate().ok(),
              "stängd: ingen markör (provisionalDate är ogiltigt)");
    }

    int  n = 0;
    Prov p;
    p.onChange = [&n] { ++n; };
    p.setDate(2026, 10, 8);
    check(p.date() == D(2026, 10, 8) && p.text() == "2026-10-08" &&
              p.dateText() == "2026-10-08",
          "setDate(2026, 10, 8) — fältet och dateText i ISO-form", p.text());
    p.setDate(2026, 2, 30);
    p.setDate(Datum{});
    check(p.date() == D(2026, 10, 8),
          "setDate med ett datum som inte finns lämnar datumet orört",
          p.text());

    // KLAMPNING VID LÄSNING — ordningen spelar ingen roll.
    Prov a, b;
    a.setDate(2030, 1, 1);
    a.setMaxDate(2029, 12, 31);
    b.setMaxDate(2029, 12, 31);
    b.setDate(2030, 1, 1);
    check(a.date() == D(2029, 12, 31) && b.date() == D(2029, 12, 31) &&
              a.text() == "2029-12-31",
          "klampningen sker vid läsning — settrarnas ordning spelar ingen roll",
          a.text() + " / " + b.text());
    a.setMaxDate(2031, 1, 1);
    check(a.date() == D(2030, 1, 1),
          "en vidgad gräns släpper fram det RÅA datumet", a.text());
    a.setMinDate(2032, 1, 1);
    check(a.date() == D(2032, 1, 1),
          "en undre gräns över den övre: den undre gäller", a.text());

    p.setMinDate(2026, 1, 1);
    p.setMaxDate(2026, 12, 31);
    check(n == 0, "programmatisk sättning fyrar INTE onChange",
          std::to_string(n));

    // ANVÄNDARVÄGARNA.
    p.dateFromUser(D(2026, 11, 1));
    check(p.date() == D(2026, 11, 1) && n == 1 && p.text() == "2026-11-01",
          "dateFromUser: nytt datum ⇒ ett event, fältet skrivs om",
          std::to_string(n));
    p.dateFromUser(D(2026, 11, 1));
    check(n == 1, "dateFromUser: samma datum fyrar inte", std::to_string(n));
    p.dateFromUser(D(2027, 6, 1));
    check(p.date() == D(2026, 12, 31) && n == 2,
          "dateFromUser: utanför gränserna ⇒ klampat (och ett event)",
          p.text());

    glaze::TextInput* f = fältet(p);
    check(f != nullptr, "fältet är en riktig TextInput");
    if (!f)
        return;
    auto skriv = [&](const char* t) {
        f->setText(t);
        p.textFromUser();
    };
    // `skriv` = texten som användaren skrev + Enter (`textFromUser`): sedan
    // 0.1.2 committas fältet och skrivs om till det datum som gäller.
    skriv("2026-03");
    check(p.date() == D(2026, 12, 31) && n == 2 && p.text() == "2026-12-31",
          "fältet: halvskrivet ändrar ingenting — och skrivs tillbaka",
          p.text());
    skriv("2026-3-5");
    check(p.date() == D(2026, 3, 5) && n == 3 && p.text() == "2026-03-05" &&
              p.dateText() == "2026-03-05",
          "fältet: en hel giltig text blir datumet vid Enter, och texten "
          "skrivs om till ISO-formen", p.text() + " / " + p.dateText());
    skriv("2026-02-30");
    check(p.date() == D(2026, 3, 5) && n == 3,
          "fältet: ett datum som inte finns ändrar ingenting");
    skriv("2025-06-01");
    check(p.date() == D(2026, 1, 1) && n == 4 && p.text() == "2026-01-01" &&
              p.dateText() == "2026-01-01",
          "fältet: utanför gränserna ⇒ KLAMPAT, och texten skrivs om till "
          "det som gäller", txt(p.date()) + " / " + p.text());
    f->setText("2026-05-05");   // skrivet utan Enter
    check(p.date() == D(2026, 1, 1) && n == 4,
          "fältet: en skriven text UTAN Enter ändrar inte datumet (inte LIVE)",
          txt(p.date()));

    check(p.setProp("date", "2026-07-04") && p.date() == D(2026, 7, 4),
          "setProp: date i ISO-form");
    check(p.setProp("date", "4 juli") && p.date() == D(2026, 7, 4),
          "setProp: ogiltigt — nyckeln känd, datumet orört");
    check(p.setProp("minDate", "2026-07-10") && p.date() == D(2026, 7, 10),
          "setProp: minDate");
    check(!p.setProp("text", "x"), "setProp: ingen text-nyckel");

    check(!p.canReleaseChild(*f) && !p.unadopt(*f) && fältet(p) == f,
          "inredningen: fältet går inte att lossa");
    p.disable();
    check(!f->isEnabled(), "disable() släcker fältet");
    p.enable();
}

// ============================================================ KLOCKSLAGET

void fallKlockslag()
{
    using std::chrono::minutes;
    // T1 — modellen.
    minutes t{0};
    check(dm::tid(14, 30, t) && t == minutes{870}, "T1: tid(14, 30) = 870 min");
    check(!dm::tid(24, 0, t) && !dm::tid(12, 60, t) && !dm::tid(-1, 0, t) &&
              t == minutes{870},
          "T1: tid utanför dygnet avvisas och lämnar värdet");
    check(dm::hhmm(minutes{65}) == "01:05" && dm::hhmm(minutes{0}) == "00:00" &&
              dm::hhmm(minutes{1439}) == "23:59",
          "T1: hhmm skriver två siffror var", dm::hhmm(minutes{65}));
    struct L {
        const char* text;
        bool        ok;
        int         min;
    };
    for (const L& l : {L{"9:05", true, 545}, L{"09:05", true, 545},
                       L{"23:59", true, 1439}, L{"0:00", true, 0},
                       L{"24:00", false, 0}, L{"12:60", false, 0},
                       L{"12:5", false, 0}, L{"123:00", false, 0},
                       L{"12.30", false, 0}, L{"12:30 ", false, 0},
                       L{"", false, 0}}) {
        minutes ut{7};
        const bool svar = dm::läsTid(l.text, ut);
        check(svar == l.ok && (l.ok ? ut == minutes{l.min} : ut == minutes{7}),
              std::string("T1: läsTid(\"") + l.text + "\")");
    }
    Datum                  d{};
    std::optional<minutes> mt;
    check(dm::läsIsoTid("2026-10-13 14:30", d, mt) && d == D(2026, 10, 13) &&
              mt && *mt == minutes{870},
          "T1: läsIsoTid med blanksteg");
    check(dm::läsIsoTid(" 2026-10-13T9:05 ", d, mt) && mt &&
              *mt == minutes{545},
          "T1: läsIsoTid med T och blanksteg runt om");
    check(dm::läsIsoTid("2026-10-14", d, mt) && d == D(2026, 10, 14) && !mt,
          "T1: läsIsoTid med bara datumet ⇒ ingen tid");
    check(!dm::läsIsoTid("2026-10-14 25:00", d, mt) &&
              !dm::läsIsoTid("2026-10-14 x", d, mt) &&
              !dm::läsIsoTid("2026-13-01 10:00", d, mt) &&
              d == D(2026, 10, 14),
          "T1: läsIsoTid avvisar ett klockslag eller datum som inte finns");

    // T2 — widgetens API och fältets text.
    int  byten = 0, klara = 0;
    Prov p;
    p.onChange = [&byten] { ++byten; };
    p.onCommit = [&klara] { ++klara; };
    p.setDate(2026, 10, 13);
    p.setTime(14, 30);
    check(p.time() == minutes{870} && p.timeText() == "14:30" &&
              p.text() == "2026-10-13",
          "T2: setTime(14, 30) — fältet utan tid visar bara datumet", p.text());
    p.setShowTime(true);
    check(p.showsTime() && p.text() == "2026-10-13 14:30",
          "T2: med showTime visar fältet datum och tid", p.text());
    p.setTime(25, 0);
    p.setTime(minutes{24 * 60});
    check(p.time() == minutes{870}, "T2: ett klockslag utanför dygnet ⇒ orört");
    check(p.setProp("time", "8:15") && p.time() == minutes{495} &&
              p.setProp("time", "8.15") && p.time() == minutes{495},
          "T2: setProp time — giltigt sätts, ogiltigt lämnas (nyckeln känd)");
    check(byten == 0, "T2: programmatisk sättning fyrar inget");

    // T3/T4 — fältets tolkning med tiden.
    glaze::TextInput* f = fältet(p);
    if (!f)
        return;
    auto skriv = [&](const char* t) {
        f->setText(t);
        p.textFromUser();
    };
    skriv("2026-10-13 16:45");
    check(p.time() == minutes{1005} && p.date() == D(2026, 10, 13) &&
              byten == 1 && klara == 1 && p.text() == "2026-10-13 16:45",
          "T4: en commit som ändrar BARA tiden fyrar onChange + onCommit",
          std::to_string(byten) + "/" + std::to_string(klara));
    skriv("2026-10-20");
    check(p.date() == D(2026, 10, 20) && p.time() == minutes{1005} &&
              p.text() == "2026-10-20 16:45",
          "T3: bara datumet behåller tiden", p.text());
    skriv("2026-10-21T7:00");
    check(p.date() == D(2026, 10, 21) && p.time() == minutes{420} &&
              p.text() == "2026-10-21 07:00",
          "T3: T som avskiljare, och texten skrivs om", p.text());
    const int före = byten;
    skriv("2026-10-21 24:00");
    check(p.time() == minutes{420} && byten == före &&
              p.text() == "2026-10-21 07:00",
          "T3: ett klockslag som inte finns lämnar värdet — texten tillbaka",
          p.text());

    // T5 — kalenderns val behåller tiden.
    p.dateFromUser(D(2026, 11, 2));
    check(p.date() == D(2026, 11, 2) && p.time() == minutes{420} &&
              p.text() == "2026-11-02 07:00",
          "T5: kalenderns val behåller tiden", p.text());
    p.setShowTime(false);
    check(p.text() == "2026-11-02", "T2: utan showTime bara datumet igen",
          p.text());
}

// ============================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Panel  sida;                // rullar — hjulets fall
    glaze::Button före{"före"};
    Prov          datum;
    Prov          av;
    glaze::Button efter{"efter"};
    glaze::Button botten{"botten"};    // långt ner: sidan har något att rulla
    int           ändringar = 0;

    Vard() : glaze::AppWindow("datepicker_gate", 420, 240)
    {
        sida.dock   = glaze::Dock::Fill;
        före.x      = 10;
        före.y      = 10;
        före.name   = "före";
        datum.x     = 10;
        datum.y     = 50;
        datum.name  = "datum";
        av.x        = 180;
        av.y        = 50;
        av.name     = "av";
        av.setDate(2026, 1, 15);
        av.disable();
        efter.x     = 10;
        efter.y     = 100;
        efter.name  = "efter";
        botten.x    = 10;
        botten.y    = 700;
        botten.name = "botten";
        datum.onChange = [this] { ++ändringar; };
        root().adopt(sida);
        sida.adopt(före);
        sida.adopt(datum);
        sida.adopt(av);
        sida.adopt(efter);
        sida.adopt(botten);
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
    void text(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, s, sizeof e.text - 1);
        handleEvent(e);
    }
    // Mitt på datumväljarens KNAPP (den högra `dateButtonW`), fysiska px.
    gfx::Pointf knappen()
    {
        const gfx::Rectf r = datum.resolve(datum.behaviorCtx());
        const float      w = style().dateButtonW * window().uiScale();
        return gfx::Pointf{r.x + r.w - w / 2.0f, r.y + r.h / 2.0f};
    }
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    void tryck(gfx::Pointf p)
    {
        gfx::Event e;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        e.type     = gfx::EventType::MouseButtonDown;
        handleEvent(e);
        e.type = gfx::EventType::MouseButtonUp;
        handleEvent(e);
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
};

glaze::AppWindow* popupen(const glaze::AppWindow& ägare)
{
    for (glaze::AppWindow* w : glaze::app().windows())
        if (w != &ägare)
            return w;
    return nullptr;
}

void fallMedFönster()
{
    glaze::setLanguage("sv");
    Vard w;
    w.setOffscreen(true);

    bool kedjanKördes = false;
    int  steg         = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kedjanKördes = true;
            if (!w.isOpen()) {
                std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                            "aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            const auto ordning = w.focusOrder();
            bool       ettStopp = false;
            for (std::size_t i = 0; i + 1 < ordning.size(); ++i)
                if (ordning[i] == &w.före && ordning[i + 1] == &w.datum &&
                    i + 2 < ordning.size() && ordning[i + 2] == &w.efter)
                    ettStopp = true;
            check(ettStopp, "tabbordningen: före · datumväljare · efter (ETT "
                            "stopp, den inaktiverade står inte med)",
                  std::to_string(ordning.size()));
            w.datum.setDate(2026, 10, 8);
            w.datum.focus();
            glaze::TextInput* f = fältet(w.datum);
            check(f && f->focused(), "focus() ⇒ FÄLTET har fokus (ombudet)");

            // HJULET över den stängda: datumet står, sidan rullar.
            const float y0 = w.sida.scrollY();
            w.hjul(w.mitt(w.datum), -1.0f);
            w.hjul(w.knappen(), -1.0f);
            check(w.datum.date() == D(2026, 10, 8) && w.ändringar == 0 &&
                      w.sida.scrollY() != y0,
                  "hjulet över den stängda: datumet står (också med fokus), "
                  "sidan rullar",
                  txt(w.datum.date()));
            w.sida.setScroll(0.0f, 0.0f);

            // ÖPPNA med Ned.
            w.tangent(gfx::Key::Down);
            check(w.datum.isOpen(), "Ned i fältet öppnar kalendern");
            glaze::AppWindow* p = popupen(w);
            check(p && p->popup() && p->popupTakesKeys(),
                  "popupen är ett eget popupfönster i App-registret som tar "
                  "tangentbordet (setPopupTakesKeys)");
            glaze::Widget* vy = p ? p->find<glaze::Widget>("calendar") : nullptr;
            check(vy && vy->focused(), "kalendervyn har fokus");
            check(w.datum.provisionalDate() == D(2026, 10, 8),
                  "markören börjar på datumet",
                  txt(w.datum.provisionalDate()));

            // MARKÖREN — utan event.
            auto mark = [&] { return w.datum.provisionalDate(); };
            w.tangent(gfx::Key::Right);
            const bool r1 = mark() == D(2026, 10, 9);
            w.tangent(gfx::Key::Down);
            const bool r2 = mark() == D(2026, 10, 16);
            w.tangent(gfx::Key::Up);
            w.tangent(gfx::Key::Left);
            w.tangent(gfx::Key::Left);
            const bool r3 = mark() == D(2026, 10, 7);
            w.tangent(gfx::Key::PageDown);
            const bool r4 = mark() == D(2026, 11, 7);
            w.tangent(gfx::Key::PageUp);
            w.tangent(gfx::Key::PageUp);
            const bool r5 = mark() == D(2026, 9, 7);
            w.tangent(gfx::Key::PageDown, gfx::kModCtrl);
            const bool r6 = mark() == D(2027, 9, 7);
            w.tangent(gfx::Key::PageUp, gfx::kModCtrl);
            w.tangent(gfx::Key::Home);
            const bool r7 = mark() == D(2026, 9, 1);
            w.tangent(gfx::Key::End);
            const bool r8 = mark() == D(2026, 9, 30);
            check(r1 && r2 && r3 && r4 && r5 && r6 && r7 && r8,
                  "markören: höger/ner/upp/vänster, PageDown/Up, Ctrl+PageDown/"
                  "Up och Home/End flyttar som facit",
                  std::string(r1 ? "" : "→ ") + (r2 ? "" : "↓ ") +
                      (r3 ? "" : "←← ") + (r4 ? "" : "PgDn ") +
                      (r5 ? "" : "PgUp ") + (r6 ? "" : "Ctrl ") +
                      (r7 ? "" : "Home ") + (r8 ? "" : "End ") + txt(mark()));
            check(w.datum.date() == D(2026, 10, 8) && w.ändringar == 0,
                  "markören ändrar INTE datumet och fyrar inget");
            w.tangent(gfx::Key::Return);
            check(w.datum.date() == D(2026, 9, 30) && w.ändringar == 1 &&
                      w.datum.text() == "2026-09-30",
                  "Enter VÄLJER markören — ett event, fältet skrivs om",
                  w.datum.text());
            break;
        }
        case 1:
            check(!w.datum.isOpen(), "valet STÄNGER kalendern");
            // F4 öppnar, Escape stänger utan ändring.
            w.tangent(gfx::Key::F4);
            check(w.datum.isOpen(), "F4 öppnar");
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Escape);
            check(w.datum.date() == D(2026, 9, 30) && w.ändringar == 1,
                  "Escape: ingen ändring, inget event");
            break;
        case 2: {
            check(!w.datum.isOpen(), "Escape stänger");
            // Alt+Ned öppnar, Tab stänger — och flyttar INTE fokus.
            w.tangent(gfx::Key::Down, gfx::kModAlt);
            check(w.datum.isOpen(), "Alt+Ned öppnar");
            w.tangent(gfx::Key::Tab);
            glaze::TextInput* f = fältet(w.datum);
            check(f && f->focused() && w.ändringar == 1,
                  "Tab i kalendern: fokus står kvar i fältet, inget event");
            break;
        }
        case 3: {
            check(!w.datum.isOpen(), "Tab stänger kalendern");
            // GRÄNSERNA: markören stannar inom dem.
            w.datum.setMinDate(2026, 9, 15);
            w.datum.setMaxDate(2026, 10, 15);
            w.tangent(gfx::Key::Down);   // datumet är 2026-09-30
            for (int i = 0; i < 3; ++i)
                w.tangent(gfx::Key::PageDown);
            const bool g1 = w.datum.provisionalDate() == D(2026, 10, 15);
            w.tangent(gfx::Key::Home);
            const bool g2 = w.datum.provisionalDate() == D(2026, 10, 1);
            w.tangent(gfx::Key::PageUp);
            const bool g3 = w.datum.provisionalDate() == D(2026, 9, 15);
            w.tangent(gfx::Key::Left);
            const bool g4 = w.datum.provisionalDate() == D(2026, 9, 15);
            check(g1 && g2 && g3 && g4,
                  "gränserna: markören stannar vid 15 oktober framåt och vid "
                  "15 september bakåt",
                  txt(w.datum.provisionalDate()));
            w.tangent(gfx::Key::Return);
            check(w.datum.date() == D(2026, 9, 15) && w.ändringar == 2,
                  "gränserna: Enter väljer den klampade dagen",
                  w.datum.text());
            break;
        }
        case 4: {
            check(!w.datum.isOpen(), "stängd efter valet");
            // FÄLTET — skrivning genom ägarens ingång.
            glaze::TextInput* f = fältet(w.datum);
            w.tangent(gfx::Key::A, kRedigera);
            const int innan = w.ändringar;
            for (const char* t : {"2", "0", "2", "6", "-", "1", "0", "-", "0"})
                w.text(t);
            check(w.datum.date() == D(2026, 9, 15) && w.ändringar == innan,
                  "fältet: \"2026-10-0\" är halvskrivet — ingenting ändras",
                  f ? f->text() : "");
            w.text("x");
            check(f && f->text() == "2026-10-0", "fältet: 'x' sväljs",
                  f ? f->text() : "");
            w.text("2");
            check(w.datum.date() == D(2026, 9, 15) && w.ändringar == innan,
                  "fältet: \"2026-10-02\" skrivet — datumet står kvar tills "
                  "texten committas (0.1.2)", txt(w.datum.date()));
            w.tangent(gfx::Key::Return);
            check(w.datum.date() == D(2026, 10, 2) && w.ändringar == innan + 1,
                  "fältet: Enter committar \"2026-10-02\" — ett event",
                  txt(w.datum.date()));
            w.tangent(gfx::Key::A, kRedigera);
            w.text("2026-1-3");
            w.tangent(gfx::Key::Return);
            check(w.datum.date() == D(2026, 9, 15) &&
                      w.datum.text() == "2026-09-15",
                  "fältet: utanför gränserna klampas, och Enter skriver om "
                  "texten", w.datum.text());

            // ÄGARENS INGÅNG — in i popupens egen ingång, den
            // Waylands grab levererar till: pilen flyttar markören, och
            // fältet i ägaren behåller fokus och sin text (ägaren routade
            // inte tangenten själv).
            w.tangent(gfx::Key::Down);
            if (!popupen(w)) {
                check(false, "kalendern öppnades igen");
                w.close();
                return;
            }
            const std::string fältet = w.datum.text();
            w.tangent(gfx::Key::Right);
            check(w.datum.provisionalDate() == D(2026, 9, 16) &&
                      w.datum.text() == fältet,
                  "ägarens ingång: pilen går till popupen, fältet är orört",
                  txt(w.datum.provisionalDate()));
            // Programmatisk setDate stänger en öppen kalender.
            w.datum.setDate(2026, 10, 1);
            break;
        }
        case 5:
            check(!w.datum.isOpen() && w.datum.date() == D(2026, 10, 1),
                  "setDate medan kalendern är öppen stänger den");
            // KNAPPEN TOGGLAR.
            w.tryck(w.knappen());
            check(w.datum.isOpen(), "tryck på knappen öppnar");
            check(!w.sida.unadopt(w.datum) && w.datum.parent() == &w.sida,
                  "öppen kalender: unadopt NEKAS och trädet är orört");
            w.tryck(w.knappen());
            break;
        case 6: {
            check(!w.datum.isOpen(), "tryck igen: stänger (ingen återöppning)");
            check(glaze::app().windowCount() == 1, "ingen popup lever kvar",
                  std::to_string(glaze::app().windowCount()));
            w.av.open();
            check(!w.av.isOpen(), "inaktiverad: open() öppnar ingenting");

            // ---- LÄGENA: Ctrl+Upp går UPP, Enter går NED, och
            //      pilarna, PageUp/Down och Home/End stegar i lägets enhet.
            w.datum.setMinDate(1, 1, 1);
            w.datum.setMaxDate(9999, 12, 31);
            w.datum.setDate(2026, 10, 8);
            const int innan = w.ändringar;
            auto mark = [&] { return w.datum.provisionalDate(); };
            w.tangent(gfx::Key::Down);                   // öppna — dagarna
            w.tangent(gfx::Key::Up, gfx::kModCtrl);      // upp till månaderna
            w.tangent(gfx::Key::Right);
            const bool m1 = mark() == D(2026, 11, 8);    // en månad
            w.tangent(gfx::Key::Down);
            const bool m2 = mark() == D(2027, 2, 8);     // ett kvartal (en rad)
            w.tangent(gfx::Key::PageUp);
            const bool m3 = mark() == D(2026, 2, 8);     // ett år
            w.tangent(gfx::Key::PageDown, gfx::kModCtrl);
            const bool m4 = mark() == D(2036, 2, 8);     // ett decennium
            w.tangent(gfx::Key::End);
            const bool m5 = mark() == D(2036, 12, 8);    // december
            w.tangent(gfx::Key::Home);
            const bool m6 = mark() == D(2036, 1, 8);     // januari
            check(m1 && m2 && m3 && m4 && m5 && m6,
                  "månaderna: höger en månad, ner ett kvartal, PageUp ett år, "
                  "Ctrl+PageDown ett decennium, End/Home december/januari",
                  std::string(m1 ? "" : "→ ") + (m2 ? "" : "↓ ") +
                      (m3 ? "" : "PgUp ") + (m4 ? "" : "Ctrl ") +
                      (m5 ? "" : "End ") + (m6 ? "" : "Home ") + txt(mark()));
            w.tangent(gfx::Key::Return);
            check(w.datum.isOpen() && mark() == D(2036, 1, 8) &&
                      w.datum.date() == D(2026, 10, 8) && w.ändringar == innan,
                  "Enter bland månaderna går NED till dagarna — inget val, "
                  "inget event", txt(mark()));
            w.tangent(gfx::Key::Right);
            check(mark() == D(2036, 1, 9), "…där pilen stegar en DAG igen",
                  txt(mark()));

            w.tangent(gfx::Key::Up, gfx::kModCtrl);      // månaderna
            w.tangent(gfx::Key::Up, gfx::kModCtrl);      // åren
            w.tangent(gfx::Key::Right);
            const bool å1 = mark() == D(2037, 1, 9);     // ett år
            w.tangent(gfx::Key::Up);
            const bool å2 = mark() == D(2034, 1, 9);     // tre år (en rad)
            w.tangent(gfx::Key::Home);
            const bool å3 = mark() == D(2030, 1, 9);     // decenniets första
            w.tangent(gfx::Key::End);
            const bool å4 = mark() == D(2039, 1, 9);     // …och sista
            w.tangent(gfx::Key::PageUp);
            const bool å5 = mark() == D(2029, 1, 9);     // ett decennium
            w.tangent(gfx::Key::PageUp, gfx::kModCtrl);
            const bool å6 = mark() == D(1929, 1, 9);     // ett sekel
            w.tangent(gfx::Key::Up, gfx::kModCtrl);      // det översta läget
            w.tangent(gfx::Key::Right);
            const bool å7 = mark() == D(1930, 1, 9);     // fortfarande åren
            check(å1 && å2 && å3 && å4 && å5 && å6 && å7,
                  "åren: höger ett år, upp tre år, Home/End decenniets första "
                  "och sista, PageUp ett decennium, Ctrl+PageUp ett sekel — och "
                  "Ctrl+Upp i det översta läget står kvar",
                  std::string(å1 ? "" : "→ ") + (å2 ? "" : "↑ ") +
                      (å3 ? "" : "Home ") + (å4 ? "" : "End ") +
                      (å5 ? "" : "PgUp ") + (å6 ? "" : "Ctrl ") +
                      (å7 ? "" : "topp ") + txt(mark()));
            w.tangent(gfx::Key::Return);                 // ned till månaderna
            w.tangent(gfx::Key::Right);
            const bool n1 = mark() == D(1930, 2, 9);
            w.tangent(gfx::Key::Return);                 // ned till dagarna
            w.tangent(gfx::Key::Right);
            const bool n2 = mark() == D(1930, 2, 10);
            check(n1 && n2 && w.datum.isOpen() && w.ändringar == innan,
                  "Enter bland åren går NED till månaderna och därifrån till "
                  "dagarna — ett läge i taget, inget event", txt(mark()));
            w.tangent(gfx::Key::Return);
            check(w.datum.date() == D(1930, 2, 10) && w.ändringar == innan + 1,
                  "Enter bland dagarna VÄLJER — ett event", w.datum.text());
            break;
        }
        case 7: {
            check(!w.datum.isOpen(), "valet bland dagarna stänger kalendern");
            // En NY öppning börjar bland dagarna; Escape stänger i en översikt
            // utan ändring.
            const int innan = w.ändringar;
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Right);
            check(w.datum.provisionalDate() == D(1930, 2, 11),
                  "en ny öppning börjar bland DAGARNA",
                  txt(w.datum.provisionalDate()));
            w.tangent(gfx::Key::Up, gfx::kModCtrl);
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Escape);
            check(w.datum.date() == D(1930, 2, 10) && w.ändringar == innan,
                  "Escape bland månaderna: ingen ändring, inget event");
            break;
        }
        case 8: {
            check(!w.datum.isOpen(), "Escape stänger — också bland månaderna");
            // GRÄNSERNA i översikterna — markören stannar i en månad och ett
            // år som går att välja.
            w.datum.setMinDate(2026, 9, 15);
            w.datum.setMaxDate(2026, 10, 15);
            w.datum.setDate(2026, 10, 1);
            auto mark = [&] { return w.datum.provisionalDate(); };
            const int innan = w.ändringar;
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Up, gfx::kModCtrl);      // månaderna
            w.tangent(gfx::Key::Left);
            const bool g1 = mark() == D(2026, 9, 15);    // klampad till gränsen
            w.tangent(gfx::Key::Left);
            const bool g2 = mark() == D(2026, 9, 15);    // augusti är släckt
            w.tangent(gfx::Key::End);
            const bool g3 = mark() == D(2026, 10, 15);   // december → oktober
            w.tangent(gfx::Key::Up, gfx::kModCtrl);      // åren
            w.tangent(gfx::Key::Right);
            const bool g4 = mark() == D(2026, 10, 15);   // 2027 är släckt
            check(g1 && g2 && g3 && g4,
                  "gränserna bland månaderna och åren: markören stannar i det "
                  "som går att välja", txt(mark()));
            w.tangent(gfx::Key::Return);                 // månaderna
            w.tangent(gfx::Key::Return);                 // dagarna
            w.tangent(gfx::Key::Return);                 // väljer
            check(w.datum.date() == D(2026, 10, 15) && w.ändringar == innan + 1,
                  "gränserna: ned genom lägena och vald — ett event",
                  w.datum.text());
            break;
        }
        case 9: {
            check(!w.datum.isOpen(), "valet stänger");
            // SKOTTDAGEN: ett år fram från 29 februari är den 28:e.
            w.datum.setMinDate(1, 1, 1);
            w.datum.setMaxDate(9999, 12, 31);
            w.datum.setDate(2028, 2, 29);
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Up, gfx::kModCtrl);
            w.tangent(gfx::Key::Up, gfx::kModCtrl);
            w.tangent(gfx::Key::Right);
            check(w.datum.provisionalDate() == D(2029, 2, 28),
                  "åren: 29 februari ett år fram är 28 februari",
                  txt(w.datum.provisionalDate()));
            w.tangent(gfx::Key::Tab);
            break;
        }
        case 10:
            check(!w.datum.isOpen() && w.datum.date() == D(2028, 2, 29),
                  "Tab bland åren stänger utan ändring");
            check(glaze::app().windowCount() == 1, "ingen popup lever kvar",
                  std::to_string(glaze::app().windowCount()));
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

    if (!kedjanKördes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ================================================================ --png
//
// ATT ÖGNA: kontrollen (normal, med fokus, inaktiverad) och kalendern per UI
// × läge. Datumet är dagens + 2, markören tre dagar längre fram och den
// undre gränsen den 3:e, så valet, markören, dagens markering och de släckta
// dagarna syns i samma bild.
void skrivPng()
{
    struct Ögon : glaze::AppWindow {
        using glaze::AppWindow::handleEvent;
        glaze::DatePicker n, a;
        // En tangent genom ägarens ingång — popupen tar den.
        void tangent(gfx::Key k, std::uint32_t mods = 0)
        {
            gfx::Event e;
            e.type     = gfx::EventType::KeyDown;
            e.windowId = window().id();
            e.key      = k;
            e.mods     = mods;
            handleEvent(e);
        }
        Ögon() : glaze::AppWindow("datepicker_gate --png", 280, 60)
        {
            setContentSize(280, 56);
            n.x = 10;
            n.y = 14;
            a.x = 146;
            a.y = 14;
            a.setDate(2026, 1, 15);
            a.disable();
            root().adopt(n);
            root().adopt(a);
        }
    } w;
    w.setOffscreen(true);

    const Datum idag = dm::idag();
    const Datum vald = dm::plusDagar(idag, 2);
    w.n.setMinDate(Datum{idag.year(), idag.month(), std::chrono::day{3}});
    w.n.setDate(vald);

    const glaze::UI       ui[]    = {glaze::UI::Glaze, glaze::UI::Slate,
                                     glaze::UI::Minimalistic};
    const char*           uiN[]   = {"Glaze", "Slate", "Minimalistic"};
    const glaze::ThemeMode läge[]  = {glaze::ThemeMode::Light,
                                      glaze::ThemeMode::Dark};
    const char*           lägeN[] = {"ljust", "morkt"};
    int                   i       = 0;
    std::function<void()> nästa;
    nästa = [&] {
        if (!w.isOpen() || i >= 6) {
            w.close();
            return;
        }
        w.setUI(ui[i / 2]);
        w.setThemeMode(läge[i % 2]);
        const std::string bas =
            g_png + "/datepicker_" + uiN[i / 2] + "_" + lägeN[i % 2];
        w.n.focus();
        w.n.open();
        if (w.n.isOpen())
            for (int k = 0; k < 3; ++k)
                w.tangent(gfx::Key::Right);   // ägarens ingång → popupen
        std::vector<std::uint8_t> px;
        int                       pw = 0, ph = 0;
        w.invalidate();
        w.window().renderFrame();
        if (w.window().captureFrame(px, pw, ph))
            gfx::encodePng(bas + "_kontroll.png", px, pw, ph);
        // Popupen i alla tre lägen: dagarna, månaderna, åren.
        if (glaze::AppWindow* p = popupen(w)) {
            for (const char* läget : {"", "_manader", "_ar"}) {
                p->invalidate();
                p->window().renderFrame();
                if (p->window().captureFrame(px, pw, ph))
                    gfx::encodePng(bas + "_popup" + läget + ".png", px, pw, ph);
                w.tangent(gfx::Key::Up, gfx::kModCtrl);
            }
        }
        w.n.close();
        ++i;
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();
    std::printf("     --png: bilderna i %s\n", g_png.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
            g_png = argv[++i];
    }

    std::printf("datepicker_gate\n");
    fallModell();
    fallSpråk();
    fallVärde();
    fallKlockslag();
    fallMedFönster();
    if (!g_png.empty())
        skrivPng();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
