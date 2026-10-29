#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

// DATUMVÄLJARENS MODELL — INTERN header och REN STD:
// texten, giltigheten, klampningen och månadsrutnätets räkning, utan en
// widget eller ett fönster. `file_dialog_model.h`s prejudikat: det som går
// att räkna utan pixlar grindas HEADLESS (tests/datepicker_gate.cpp når den
// här filen) mot handskrivna förväntade värden. `DatePicker` och dess månadsvy läser
// samma funktioner, så det som visas och det som grindas aldrig kan glida
// isär.
//
// DATUMET är `std::chrono::year_month_day` (C++20) — kalendern är standardens,
// skottåren med, och ingenting här räknar dagar för hand.
//
// ÅREN 1–9999 och inget annat: textformen är ISO 8601 med FYRA siffror, och
// ett år utanför går varken att skriva eller att läsa i den. Gränserna är
// formatets, inte valda.

namespace glaze::datemodel {

using Datum = std::chrono::year_month_day;

inline constexpr int kForstaAr = 1;      // ISO-formens fyra siffror —
inline constexpr int kSistaAr  = 9999;   // formatets gränser, inte valda

// KALENDERNS egna tal — veckans dagar, månadsrutnätets rader (ALLTID sex, så
// rutnätet har samma storlek varje månad) och årets månader.
inline constexpr int kVeckodagar   = 7;
inline constexpr int kRader        = 6;
inline constexpr int kManaderPerAr = 12;

// ÖVERSIKTERNA — månadsläget och årsläget delar ETT rutnät om tolv
// rutor, tre i bredd och fyra i höjd: årets månader med ett KVARTAL per rad,
// och ett DECENNIUM med ett år före och ett efter (Windows kalendervy — som
// grannmånadernas dagar i dagsläget). Ett decennium är årslägets SIDA.
inline constexpr int kOversiktKolumner = 3;
inline constexpr int kOversiktRader    = 4;
inline constexpr int kOversiktRutor    = kOversiktKolumner * kOversiktRader;
inline constexpr int kArPerSida        = 10;

// Ett giltigt datum inom formatets år? (`ok()` säger att dagen finns i
// månaden — 29 februari bara ett skottår.)
inline bool giltigt(const Datum& d)
{
    return d.ok() && static_cast<int>(d.year()) >= kForstaAr &&
           static_cast<int>(d.year()) <= kSistaAr;
}

// Tre heltal → datum. `false` och `ut` orörd när datumet inte finns.
inline bool datum(int år, int månad, int dag, Datum& ut)
{
    // Året prövas FÖRE konstruktionen: `std::chrono::year` bär bara
    // ±32767, och ett större tal hade kunnat landa innanför gränserna.
    if (år < kForstaAr || år > kSistaAr || månad < 1 ||
        månad > kManaderPerAr || dag < 1 || dag > 31)
        return false;
    const Datum d{std::chrono::year{år},
                  std::chrono::month{static_cast<unsigned>(månad)},
                  std::chrono::day{static_cast<unsigned>(dag)}};
    if (!giltigt(d))
        return false;
    ut = d;
    return true;
}

// TEXTFORMEN: ISO 8601, `ÅÅÅÅ-MM-DD` — den filväljaren redan visar, och den
// enda som läses likadant på varje språk.
inline std::string iso(const Datum& d)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u",
                  static_cast<int>(d.year()),
                  static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

// ANVÄNDARENS TEXT → datum. Generös där det är ofarligt (blanksteg runt om,
// en- eller tvåsiffrig månad och dag), strikt där det inte är det: året har
// FYRA siffror, avskiljaren är `-`, och inget får stå efter. Allt annat —
// också ett datum som inte finns (`2026-02-30`) — är `false` med `ut` orörd.
// Ingen lokal format läses: en tolkning som berodde på språket hade gett
// samma text två betydelser.
inline bool läsIso(std::string_view s, Datum& ut)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string_view::npos)
        return false;
    const auto b = s.find_last_not_of(" \t");
    s            = s.substr(a, b - a + 1);

    auto tal = [&s](std::size_t& i, std::size_t minst, std::size_t högst,
                    int& v) {
        std::size_t n = 0;
        v             = 0;
        while (i < s.size() && n < högst && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            ++i;
            ++n;
        }
        return n >= minst;
    };
    std::size_t i = 0;
    int         år = 0, månad = 0, dag = 0;
    if (!tal(i, 4, 4, år) || i >= s.size() || s[i++] != '-')
        return false;
    if (!tal(i, 1, 2, månad) || i >= s.size() || s[i++] != '-')
        return false;
    if (!tal(i, 1, 2, dag) || i != s.size())
        return false;
    return datum(år, månad, dag, ut);
}

// KLAMPNINGEN — det effektiva datumet inom [lo, hi]. En övre gräns UNDER den
// undre räknas som den undre (talfältets regel: settrarnas ordning ska aldrig
// spela roll).
inline Datum klampa(const Datum& d, const Datum& lo, const Datum& hi)
{
    const std::chrono::sys_days l = lo;
    const std::chrono::sys_days h = std::max(l, std::chrono::sys_days(hi));
    return Datum{std::clamp(std::chrono::sys_days(d), l, h)};
}

inline Datum plusDagar(const Datum& d, int n)
{
    return Datum{std::chrono::sys_days(d) + std::chrono::days{n}};
}

// N månader fram (eller bak), med dagen KLAMPAD till den nya månadens sista
// — 31 januari + en månad är den sista februari, aldrig 3 mars.
inline Datum plusMånader(const Datum& d, int n)
{
    using namespace std::chrono;
    const year_month ym = year_month{d.year(), d.month()} + months{n};
    const day        sista = year_month_day_last{ym.year(),
                                          month_day_last{ym.month()}}.day();
    return Datum{ym.year(), ym.month(), std::min(d.day(), sista)};
}

// VECKANS FÖRSTA DAG ur språkets text (`glaze::str::FirstWeekday`): ISO-
// numret "1" (måndag) till "7" (söndag). 0 = inte ett sådant tal — anroparen
// faller då tillbaka på ISO-veckans måndag.
inline int läsFörstaVeckodag(std::string_view s)
{
    if (s.size() != 1 || s[0] < '1' || s[0] > '7')
        return 0;
    return s[0] - '0';
}

// Månadens SISTA dag.
inline Datum sistaIMånaden(const Datum& d)
{
    using namespace std::chrono;
    return Datum{year_month_day_last{d.year(), month_day_last{d.month()}}};
}

// ------------------------------------------- KLOCKSLAGET
//
// Tiden är MINUTER SEDAN MIDNATT (`std::chrono::minutes`, [0, ett dygn)) och
// ingen tidpunkt: datumväljarens värde är paret datum + tid utan klocka,
// precis som `year_month_day` inte har någon. Appen lägger ihop dem med den
// klocka den använder (`local_days{d} + t` eller `sys_days{d} + t`).
// Textformen är ISO 8601:s `HH:MM` — 24-timmarsklockan, som på alla fyra
// språken, och ingen sekund.

inline constexpr int kTimmarPerDygn   = 24;   // klockans egna tal
inline constexpr int kMinuterPerTimme = 60;

// Ett klockslag inom dygnet?
inline bool giltigTid(std::chrono::minutes t)
{
    return t >= std::chrono::minutes{0} &&
           t < std::chrono::hours{kTimmarPerDygn};
}

// Två heltal → klockslag. `false` och `ut` orörd utanför 0–23 och 0–59.
inline bool tid(int timme, int minut, std::chrono::minutes& ut)
{
    if (timme < 0 || timme >= kTimmarPerDygn || minut < 0 ||
        minut >= kMinuterPerTimme)
        return false;
    ut = std::chrono::hours{timme} + std::chrono::minutes{minut};
    return true;
}

// TEXTFORMEN `HH:MM`, två siffror var.
inline std::string hhmm(std::chrono::minutes t)
{
    const auto m = t.count();
    char       buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d",
                  static_cast<int>(m / kMinuterPerTimme),
                  static_cast<int>(m % kMinuterPerTimme));
    return buf;
}

// ANVÄNDARENS KLOCKSLAG → minuter. Timmen får ha en eller två siffror
// (`9:05`), minuten EXAKT två, avskiljaren är `:` och inget får stå efter.
// `false` och `ut` orörd för allt annat — också `24:00` och `12:60`.
inline bool läsTid(std::string_view s, std::chrono::minutes& ut)
{
    std::size_t i = 0;
    int         timme = 0, minut = 0, n = 0;
    while (i < s.size() && n < 2 && s[i] >= '0' && s[i] <= '9') {
        timme = timme * 10 + (s[i] - '0');
        ++i;
        ++n;
    }
    if (n == 0 || i >= s.size() || s[i++] != ':')
        return false;
    n = 0;
    while (i < s.size() && n < 2 && s[i] >= '0' && s[i] <= '9') {
        minut = minut * 10 + (s[i] - '0');
        ++i;
        ++n;
    }
    if (n != 2 || i != s.size())
        return false;
    return tid(timme, minut, ut);
}

// DATUMET MED VALFRITT KLOCKSLAG: `ÅÅÅÅ-MM-DD`, `ÅÅÅÅ-MM-DD HH:MM` eller
// `ÅÅÅÅ-MM-DDTHH:MM` (ISO 8601 med blanksteg eller `T`), blanksteg runt om
// tålt. `t` är tomt när texten bara bär datumet — anroparen behåller då sin
// tid. Allt annat är `false` med `d` och `t` orörda.
inline bool läsIsoTid(std::string_view s, Datum& d,
                      std::optional<std::chrono::minutes>& t);

// DAGENS DATUM i LOKAL tid, ur C-bibliotekets kalender (`localtime`) —
// `std::chrono::current_zone` hade krävt tidszonsdatabasen, som inte finns
// överallt där Glaze byggs. UI-trådens funktion (`localtime` delar sin buffert).
// Faller C-biblioteket bort (kan inte hända på en riktig klocka) blir det
// UTC-dagen ur `system_clock`.
inline Datum idag()
{
    const std::time_t nu = std::time(nullptr);
    if (const std::tm* t = std::localtime(&nu)) {
        Datum d{};
        if (datum(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, d))
            return d;
    }
    return Datum{std::chrono::floor<std::chrono::days>(
        std::chrono::system_clock::now())};
}

// KLOCKSLAGET NU i LOKAL tid, hela minuter — `idag()`s källa och regel.
inline std::chrono::minutes nuMinuter()
{
    const std::time_t nu = std::time(nullptr);
    if (const std::tm* t = std::localtime(&nu)) {
        std::chrono::minutes ut{0};
        if (tid(t->tm_hour, t->tm_min, ut))
            return ut;
    }
    const auto sedan = std::chrono::system_clock::now() -
                       std::chrono::floor<std::chrono::days>(
                           std::chrono::system_clock::now());
    return std::chrono::floor<std::chrono::minutes>(sedan);
}

inline bool läsIsoTid(std::string_view s, Datum& d,
                      std::optional<std::chrono::minutes>& t)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string_view::npos)
        return false;
    const auto b = s.find_last_not_of(" \t");
    s            = s.substr(a, b - a + 1);
    // Datumet slutar vid första blanksteget eller `T` — tidens avskiljare.
    const auto sep = s.find_first_of(" Tt");   // lang_gate:rad avskiljartecknen, ingen användartext
    Datum      dag{};
    if (!läsIso(s.substr(0, sep), dag))
        return false;
    if (sep == std::string_view::npos) {
        d = dag;
        t.reset();
        return true;
    }
    std::string_view rest = s.substr(sep + 1);
    const auto       r    = rest.find_first_not_of(' ');
    if (r == std::string_view::npos)
        return false;
    std::chrono::minutes kl{0};
    if (!läsTid(rest.substr(r), kl))
        return false;
    d = dag;
    t = kl;
    return true;
}

// ------------------------------------------------ ÖVERSIKTERNA

static_assert(kOversiktRutor == kManaderPerAr,
              "månadsläget visar årets månader i rutnätet");
static_assert(kOversiktRutor == kArPerSida + 2,
              "årsläget visar decenniet med ett år före och ett efter");

// Ett år formatet kan skriva (ISO-formens fyra siffror). Årslägets yttersta
// rutor kan hamna utanför — år 0 och år 10000 — och ritas då tomma.
inline bool åretFinns(int år)
{
    return år >= kForstaAr && år <= kSistaAr;
}

// ÅRSLÄGETS SIDA: decenniet året står i (2026 ⇒ 2020, 2030 ⇒ 2030).
inline int decennium(int år)
{
    return år - år % kArPerSida;
}

// Årslägets ruta `i` (0–11). Decenniets första år står i ruta 1, så ruta 0
// är året FÖRE och ruta 11 året EFTER — 2019, 2020 … 2029, 2030.
inline int åretIRuta(int decenniumStart, int i)
{
    return decenniumStart - 1 + i;
}

// RÖR månaden gränserna [lo, hi]? Bara då går den att välja i månadsläget —
// också en månad där bara en del av dagarna är tillåtna (valet klampar dagen).
// En övre gräns under den undre räknas som den undre, klampningens regel.
inline bool månadenInom(std::chrono::year_month ym, const Datum& lo,
                        const Datum& hi)
{
    using namespace std::chrono;
    const sys_days l     = lo;
    const sys_days h     = std::max(l, sys_days(hi));
    const sys_days första = year_month_day{ym.year(), ym.month(), day{1}};
    const sys_days sista =
        year_month_day_last{ym.year(), month_day_last{ym.month()}};
    return första <= h && sista >= l;
}

// …och samma fråga för ett helt år i årsläget. Ett år formatet inte kan
// skriva är aldrig inom.
inline bool åretInom(int år, const Datum& lo, const Datum& hi)
{
    using namespace std::chrono;
    if (!åretFinns(år))
        return false;
    const sys_days l = lo;
    const sys_days h = std::max(l, sys_days(hi));
    return sys_days(year_month_day{year{år}, January, day{1}}) <= h &&
           sys_days(year_month_day{year{år}, December, day{31}}) >= l;
}

// MÅNADSRUTNÄTETS FÖRSTA RUTA — den dag på eller FÖRE månadens första som
// är veckans första (`förstaIso` 1 = måndag … 7 = söndag). Rutnätet är alltid
// 6 × 7 och börjar här, så månadens första står i första raden och de sista
// dagarna ryms också när månaden börjar sent i veckan.
inline Datum förstaRutan(const std::chrono::year_month& ym, int förstaIso)
{
    using namespace std::chrono;
    const sys_days   första = year_month_day{ym.year(), ym.month(), day{1}};
    const int        vd     = static_cast<int>(weekday{första}.iso_encoding());
    const int        bakåt  = (vd - förstaIso + kVeckodagar) % kVeckodagar;
    return Datum{första - days{bakåt}};
}

} // namespace glaze::datemodel
