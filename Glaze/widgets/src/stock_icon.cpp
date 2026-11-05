// STANDARDIKONERNA — ritbeskrivningarna och
// rastreringen bakom `glaze::stockIcon`. Kontraktet utåt står i
// glaze/stock_icon.h; ritningen går genom `detail::stockBitmap`
// (image_cache.h), som `Draw::glyph`, fönstrets bildcache och OS-ikonen frågar.
//
// RITBESKRIVNINGEN är en sträng per ikon i en ruta på 24 × 24 (rad 0 överst,
// y nedåt), skriven för att vara lätt att ändra — talen är ikonernas form,
// precis som `Draw::icon`s enhetskvadrat:
//
//   M x y            ny linje från (x, y)        L x y    linje till
//   H x · V y        vågrätt / lodrätt till      Q kx ky x y   kurva till (x, y)
//   Z                sluter linjen               F        nästa M-linje FYLLS
//   C cx cy r        cirkel                      A cx cy r a0 a1   båge
//   E cx cy rx ry    ellips                      U cx cy rx ry a0 a1   ellipsbåge
//   R x y b h r      rektangel med hörnradie     B x y b h r   fylld rektangel
//   D cx cy r        fylld skiva                 @namn    en gemensam DEL (kDelar)
//   K x y b h TEXT   TEXTEN UTSTANSAD ur hela ikonen, centrerad i rutan
//                    (filtypernas etikett: A–Z och 0–9 i `kGlyfer`)
//
// Vinklarna är grader medurs från höger (0 höger, 90 nedåt) och en båge går
// medurs från a0 till a1 (a1 > a0, gärna över 360). Allt utom F/B/D är
// STRECK med EN bredd för alla ikoner (`ikonStreck`) och RAKA ändar och
// skarvar, så att två linjer i rät vinkel möts i ett skarpt hörn; en kurva
// (`Q`) har runda, så att den blir jämn. En rektangel med radie (`R`, `B`)
// ritas med SKURNA hörn: den yttersta pixeln i varje hörn lämnas ute.
//
// PIXELFÄSTNINGEN: beskrivningen avbildas på bildens fysiska px och FÄSTS mot
// pixelrutnätet innan den rastreras (`Avbildning`), så att det raka står på
// hela pixlar i varje storlek och bara det sneda och det runda kantutjämnas:
//   * strecket är ett HELT antal px (`ikonStreck`): 1 px upp till 31, 2 px i
//     32–63, 3 px i 64–95 …;
//   * varje punkt — linjernas ändar, kurvornas ändar och styrpunkter, ytornas
//     hörn, cirklarnas och bågarnas mittpunkter — flyttas till strecket mitt:
//     pixelns mitt för ett udda streck, pixelgränsen för ett jämnt. En lodrät
//     eller vågrät linje täcker då exakt sina kolumner eller rader;
//   * cirklarnas, ellipsernas och bågarnas radier är hela px, så att deras
//     yttersta punkter står på samma rutnät som linjerna;
//   * en FYLLD rektangel (`B`) har sina kanter på pixelgränser.
// Samma regel gäller den utstansade texten, med textens eget streck. Det som
// ligger mellan punkterna — kurvornas korda, de sneda linjerna — flyttas inte.
//
// RASTRERINGEN provar varje ritad form i `kProv` × `kProv` punkter per
// fysisk px och tar ned bilden med `glzassets::resizeRgba` (ytans medelvärde i
// förmultiplicerad rymd) — kantutjämningen är programikonens skalare, inte en
// egen. Konturen är temats textfärg, med rak alfa som `glaze::Bitmap` vill ha.
// FÄRGEN (postens andra sträng) lägger ytor och linjer i temats `stock*`-roller och
// papperet (`stockPaper`) bakom eller ovanpå konturen; varje prov får färgen
// från det översta lager det ligger i, och samma nedskalning blandar dem.
// `IconStyle::Color` ritar färgerna fullt, `Mono` samma bild NEDTONAD:
// ytorna blandade mot papperet (`kNedtonad`), linjerna i textfärgen. En
// UTSTANSAD text (`K`) är genomskinlig i sitt bildlager, så det under syns
// genom den — filtypernas etikett läses i varje tema och läge.
//
// BILDLAGREN: en ikon ritas i upp till fyra lager, nerifrån och upp — `|`
// skiljer dem åt i både konturen och färgen i `kDefs`. Ett lager ritas
// fritt, med färger, toner och gradienter (`ritaLager`), och läggs sedan på
// det under (`läggPå`): dess YTTERPIXLAR möter det de hamnar mot — de undre
// lagren, eller fönstrets bakgrund, panelen och namnlisten där inget ligger —
// och flyttas i ljushet bort från det tills de når lagrets krav, 3:1 som WCAG
// kräver av grafik (`contrast N` sänker det, en linje får textens 4,5:1).
// Hela kanten flyttas lika långt, så att den blir jämn; når det inte kravet
// VÄXER YTAN en ring runt lagret, ljusare eller mörkare, tills det gör det.
// Det första lagrets kontur speglas i mörkt läge, så att samma bild har en
// kant som syns mot båda bakgrunderna.
//
// APPENS IKONER (`registerIcon`) har SAMMA form som raderna i `kDefs` — namn,
// kontur och färg — och går genom samma rastrering; deras delar
// (`registerIconPart`) slås upp efter ramverkets, och de cachas i en egen
// tabell som töms när appen registrerar något.

#include "glaze/stock_icon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gfx/geometry.h>
#include <gfx/platform.h>

#include "glaze/color.h"          // mix — färgvariantens starka toner
#include "glaze_assets/image.h"   // resizeRgba — kantutjämningen
#include "image_cache.h"          // detail::stockBitmap
#include "prop_parse.h"           // parseDouble — talen, oberoende av lokalen

namespace glaze {
namespace {

constexpr float kRuta   = 24.0f;   // ritrutans sida
constexpr int   kProv   = 4;       // prov per fysisk px och axel
constexpr int   kMaxPx  = 512;     // största rastrering (sida, fysiska px)
constexpr std::size_t kMaxLager = 4;   // bildlager per ikon

// Den utstansade TEXTEN (`K`): glyferna står i en ruta på 3 × 5 med ett
// tunnare streck än ikonerna och 1.5 mellan glyfernas rutor; texten skalas
// ned i sin helhet (streck och mellanrum med) när den inte ryms.
constexpr float kGlyfB      = 3.0f;
constexpr float kGlyfH      = 5.0f;
constexpr float kGlyfSteg   = 1.5f;
constexpr float kTextStreck = 0.9f;

// Färgvariantens STARKA ton (`strong`): rollen blandad så här långt mot
// textfärgen — mörkare i ljust läge och ljusare i mörkt, så en utstansad
// etikett på grönt eller bärnsten håller kontrasten mot bakgrunden.
constexpr float kStark = 0.35f;

// MONO är samma bild NEDTONAD: färgytorna blandade så här långt mot ytan
// ikonen står på — papperet i ljust läge, fönstrets bakgrund i mörkt —,
// linjerna och konturen i textfärgen.
constexpr float kNedtonad = 0.70f;

// TONERNA av en färgroll (`light`, `dark`, `deep`): rollens LJUSHET flyttad
// så här mycket med kulören och mättnaden kvar — en mörk gul är djupare gul,
// inte grå.
//
// DJUPET, i hela pixlar och utan släppskugga — ljuset faller uppifrån till
// vänster:
//   * en gradient (`gradient`) går från rollen + `kGradLjus` uppe till rollen
//     − `kGradMörk` nere, mest lodrätt (`kGradLodrätt`);
//   * en gradientyta har en HÖGDAGER: dess översta pixelrad `kGlans` ljusare;
//   * en kontur i en ton är ytans FASADE kant: över- och vänsterkanten
//     `kKantLjus` ljusare, nederkanten skuggad (`kSkugga` mot svart).
constexpr float      kLjus        = 0.14f;
constexpr float      kMörk        = 0.14f;
constexpr float      kDjup        = 0.32f;
constexpr float      kBläck       = 0.75f;   // `ink`: tecken som är mörka i båda lägena
constexpr float      kGradLjus    = 0.16f;
constexpr float      kGradMörk    = 0.14f;
constexpr float      kGradLodrätt = 0.75f;   // lodrätt mot diagonalt
constexpr float      kGlans       = 0.14f;
constexpr float      kKantLjus    = 0.14f;
constexpr float      kSkugga      = 0.25f;
constexpr gfx::Color kSvart{0, 0, 0, 255};   // skuggans ändpunkt

// KANTEN MOT BAKGRUNDEN: varje pixel som gränsar mot det genomskinliga ska
// nå `kKantKontrast` (WCAG:s krav på grafik) mot ytorna en ikon står på —
// fönstrets bakgrund, panelen och namnlisten. Den som inte gör det flyttas i
// ljushet bort från bakgrunden, `kKantSteg` i taget (`läggPå`).
constexpr float kKantKontrast = 3.0f;
constexpr float kKantSteg     = 0.02f;
constexpr float kKantAndel    = 0.75f;   // det gemensamma steget räcker för så många
constexpr float kTäckning     = 0.6f;    // kantutjämningens förstärkning (alfa^k)
// En LINJE — ett lager av bara streck, utan en enda yta — läses som text och
// får textens krav (WCAG 4,5:1): en tunn linje på 3:1 ser svag ut.
constexpr float kLinjeKontrast = 4.5f;

// En STANDARDIKON i SAMMA form som appens (`GLAZE_REGISTER_ICON`): namnet,
// konturen och färgen, med lagren åtskilda av `|` i båda och i samma ordning.
// Ett lager utan färg (tomt mellan två `|`) ritas i textfärgen, likadant i
// båda stilarna.
struct Def {
    StockIcon   icon;
    const char* name;
    const char* kontur;
    const char* färg;
};

// lang_gate:av ritbeskrivningar och ikonernas API-namn — ingen användartext
// DELAR som flera ikoner ritar — `@namn` i en beskrivning.
struct Del {
    const char* name;
    const char* path;
};

constexpr Del kDelar[] = {
    {"fil", "M 14 2 H 4 V 22 H 20 V 8 Z M 14 2 V 8 H 20"},
    {"vikt", "M 14 2 V 8 H 20 Z"},          // filens vikta hörn som yta
    {"mapp", "M 2 20 H 22 V 6 H 12 L 10 3 H 2 Z"},
    {"mappfram", "M 2 10 H 22 V 20 H 2 Z"},  // mappens framsida som yta
    // Den öppna mappen: bakstycket och den lutade framsidan.
    {"oppen", "M 20 10 V 6 H 10 L 8 3 H 2 V 20 H 18 L 22 10 H 6 L 2 20"},
    {"oppenfram", "M 6 10 H 22 L 18 20 H 2 Z"},
    {"disk", "M 3 3 H 16 L 21 8 V 21 H 3 Z M 17 21 V 13 H 7 V 21 M 7 3 V 8 H 15"},
    {"kopia", "R 8 8 14 14 2 M 8 16 H 2 V 2 H 16 V 8"},
    {"lupp", "C 11 11 8 M 21 21 L 16.7 16.7"},
    {"oga", "A 12 15.6 10.6 200 340 A 12 8.4 10.6 20 160"},
    {"nal", "M 12 17 V 22 M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 "
            "R 6 2 12 4 1"},
    {"las", "R 3 11 18 11 2 M 7 11 V 7 A 12 7 5 180 360 M 17 7 V 11"},
    {"hogtalare", "M 11 4.7 L 6.4 8 H 2 V 16 H 6.4 L 11 19.3 Z"},
    {"kryss", "M 18 6 L 6 18 M 6 6 L 18 18"},
    {"rader", "M 3 6 H 13 M 3 12 H 13 M 3 18 H 13"},
    {"veck", "M 12 22 V 16 M 12 8 V 2 M 2 12 H 4 M 8 12 H 10 M 14 12 H 16 "
             "M 20 12 H 22"},
    // Den isometriska kuben: sexhörningen och de tre synliga kanterna inåt.
    {"kub", "M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z "
            "M 4.2 7.5 L 12 12 L 19.8 7.5 M 12 12 V 21"},
    // En yta i perspektiv — markeringslägenas punkt, kant och yta.
    {"yta", "M 3 18 L 7 6 H 21 L 17 18 Z"},
    // Kuben sedd UNDIFRÅN: botten och de två främre sidorna synliga.
    {"kubunder", "M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z "
                 "M 4.2 16.5 L 12 12 L 19.8 16.5 M 12 12 V 3"},
    // FILTYPERNAS små symboler i sidans övre vänstra hörn (ovanför etiketten,
    // till vänster om vecket).
    {"fpin", "C 9.15 6 2 M 7.5 7.2 L 9.15 10.2 L 10.8 7.2"},
    {"fglob", "C 9.15 7.15 2.9 E 9.15 7.15 1.2 2.9 M 6.25 7.15 H 12.05"},
    {"fdb", "E 9.15 5 2.6 1 M 6.55 5 V 9.3 M 11.75 5 V 9.3 U 9.15 9.3 2.6 1 0 180"},
    {"fvinkel", "M 6.5 10.3 V 4.2 L 11.8 10.3 Z"},
    {"fkub", "M 9.15 4.1 L 11.83 5.65 V 8.75 L 9.15 10.3 L 6.47 8.75 V 5.65 Z "
             "M 6.47 5.65 L 9.15 7.2 L 11.83 5.65 M 9.15 7.2 V 10.3"},
    {"fmoln", "D 7 5 0.7 D 9.6 4.3 0.7 D 11.4 5.8 0.7 D 8.2 7.3 0.7 D 10.6 7.9 0.7 "
              "D 6.9 9.6 0.7 D 9.3 10 0.7 D 11.5 9.8 0.7"},
    {"fbild", "M 6.5 10.3 L 8.6 7.2 L 10 9 L 10.9 8 L 11.9 10.3 Z D 10.6 4.9 1"},
    {"ftext", "M 6.5 4.6 H 11.8 M 6.5 7.3 H 11.8 M 6.5 10 H 10"},
    {"ftabell", "R 6.5 4.2 5.3 6.1 0.6 M 6.5 7.25 H 11.8 M 9.15 4.2 V 10.3"},
    {"fslide", "R 6.5 4.2 5.3 4.2 0.5 M 9.15 8.4 V 10.4"},
    {"fklammer", "M 7.9 4.2 Q 7.1 4.2 7.1 5 V 6.4 Q 7.1 7.2 6.4 7.2 Q 7.1 7.2 7.1 8 "
                 "V 9.5 Q 7.1 10.3 7.9 10.3 M 10.4 4.2 Q 11.2 4.2 11.2 5 V 6.4 "
                 "Q 11.2 7.2 11.9 7.2 Q 11.2 7.2 11.2 8 V 9.5 Q 11.2 10.3 10.4 10.3"},
    {"fkod", "M 8 4.6 L 6.5 7.25 L 8 9.9 M 10.3 4.6 L 11.8 7.25 L 10.3 9.9"},
    {"fdragkedja", "M 8.5 3.8 V 4.3 M 9.8 5.3 V 5.8 M 8.5 6.8 V 7.3 B 7.9 8.4 2.5 2 0.5"},
    {"fnot", "M 8.2 9.6 V 5 L 11.6 4.2 V 8.8 D 7.3 9.6 1.1 D 10.7 8.8 1.1"},
    {"fspela", "F M 7.3 4.5 L 11.3 7.25 L 7.3 10 Z"},
};

// FÄRGDELAR som flera ikoner färgar likadant — `#namn` i en färg, inlagda
// ord för ord. Ytan är formen ovan med samma namn.
constexpr Del kFärgDelar[] = {
    // Sidan: vit med vikt hörn i mörk ton och en grå, fasad kant.
    {"fil", "all deep paper gradient paper F @fil dark paper F @vikt"},
    // Mappen: bakstycket i mörk ton, papperet som sticker upp och framsidan i
    // en gradient.
    {"mapp", "all dark yellow dark yellow F @mapp paper B 4 7 16 6 0 "
             "gradient yellow F @mappfram"},
    {"oppen", "all dark yellow dark yellow F M 2 3 H 8 L 10 6 H 20 V 20 H 2 Z "
              "paper B 5 7 13 6 0 gradient yellow F @oppenfram"},
    // Den runda knappen: en blå skiva (ett tecken på den ritas `over paper`).
    {"rund", "all deep blue gradient blue D 12 12 10"},
    // Disketten: blå, med luckan i mörkt papper och etiketten i papper.
    {"disk", "all deep blue gradient blue F @disk dark paper B 7 3 8 5 0 "
             "paper B 7 13 10 8 0"},
    // Förstoringsglaset: ljusblått glas.
    {"lupp", "all deep blue gradient light blue D 11 11 8"},
    // Två sidor: den bakre i mörkt papper och inte huvudsaken, den främre vit.
    {"sidabak", "all deep paper gradient dark paper B 2 2 14 14 1 contrast 2"},
    {"sidafram", "all deep paper gradient paper B 8 8 14 14 1"},
    // Kuben: ljusblå kropp (den sedda ytan läggs i blått ovanpå).
    {"kub", "all deep blue gradient light blue "
            "F M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z"},
};

// APPENS IKONER OCH DELAR (`registerIcon`, `registerIconPart`), i
// registreringsordning — processens, i UI-tråden eller vid statisk
// initiering, som widgetregistret.
struct AppRecept {
    std::string namn, kontur, färg;
};

std::vector<AppRecept>& appIkoner()
{
    static std::vector<AppRecept> v;
    return v;
}

std::vector<AppRecept>& appDelar()
{
    static std::vector<AppRecept> v;
    return v;
}

// IKONEN SOM RASTRERAS — namnet ett fel i dess beskrivning loggas under, och
// de namn som redan har loggats: ett fel loggas EN gång per ikon, inte en gång
// per storlek, stil och palett.
std::string& ritasNu()
{
    static std::string s;
    return s;
}

std::vector<std::string>& felLoggade()
{
    static std::vector<std::string> v;
    return v;
}

bool förstaFelet()
{
    auto& v = felLoggade();
    if (std::find(v.begin(), v.end(), ritasNu()) != v.end())
        return false;
    v.push_back(ritasNu());
    return true;
}

const AppRecept* hittaApp(const std::vector<AppRecept>& v, std::string_view namn)
{
    for (const AppRecept& r : v)
        if (r.namn == namn)
            return &r;
    return nullptr;
}

// En DEL: ramverkets först, sedan appens — konturen (`@namn`) eller färgen
// (`#namn`). Null när ingen finns.
const char* delKontur(std::string_view namn)
{
    for (const Del& d : kDelar)
        if (namn == d.name)
            return d.path;
    const AppRecept* r = hittaApp(appDelar(), namn);
    return r ? r->kontur.c_str() : nullptr;
}

const char* delFärg(std::string_view namn)
{
    for (const Del& d : kFärgDelar)
        if (namn == d.name)
            return d.path;
    const AppRecept* r = hittaApp(appDelar(), namn);
    return r && !r->färg.empty() ? r->färg.c_str() : nullptr;
}

// En post per ikon, i UPPRÄKNINGENS ordning (static_assert nedan): konturen
// och färgen, ett lager per `|`. Färgen är `IconStyle::Color`s — konturen
// ritas som i mono, och färgen lägger till: ett FÄRGORD (`blue`, `green`,
// `red`, `amber`, `yellow`, `paper` — `Theme::Colors::stock*`) börjar ett
// lager i den färgen, `under` (förvalet: bakom konturen, för ytor) eller
// `over` (ovanpå, för en linje som byter färg), och `all <färg>` färgar hela
// konturen. En DETALJ som ska skiljas från det den ligger på — pilen på
// skivan, glaset på sidan, pennan på disketten — är ett eget lager efter `|`.
constexpr Def kDefs[] = {
    // ----------------------------------------------------------- Navigering
    // Skivan, och tecknet på den som ett eget lager.
    {StockIcon::Back, "Back", "C 12 12 10 | M 16 12 H 8 M 12 8 L 8 12 L 12 16",
     "#rund | all paper"},
    {StockIcon::Forward, "Forward", "C 12 12 10 | M 8 12 H 16 M 12 8 L 16 12 L 12 16",
     "#rund | all paper"},
    {StockIcon::Up, "Up", "C 12 12 10 | M 12 16 V 8 M 8 12 L 12 8 L 16 12",
     "#rund | all paper"},
    {StockIcon::Down, "Down", "C 12 12 10 | M 12 8 V 16 M 8 12 L 12 16 L 16 12",
     "#rund | all paper"},
    {StockIcon::Home, "Home", "M 3 11 L 12 3 L 21 11 M 5 9.5 V 21 H 19 V 9.5 "
                              "M 10 21 V 15 H 14 V 21",
     "all deep blue gradient blue F M 5 9.5 L 12 3.6 L 19 9.5 V 21 H 5 Z "
     "paper B 10 15 4 6 0"},
    {StockIcon::First, "First", "M 17 18 L 11 12 L 17 6 M 7 6 V 18", "all blue"},
    {StockIcon::Previous, "Previous", "C 12 12 10 | M 14 16 L 10 12 L 14 8",
     "#rund | all paper"},
    {StockIcon::Next, "Next", "C 12 12 10 | M 10 8 L 14 12 L 10 16", "#rund | all paper"},
    {StockIcon::Last, "Last", "M 7 18 L 13 12 L 7 6 M 17 6 V 18", "all blue"},
    {StockIcon::Refresh, "Refresh", "A 12 12 9 200 333 M 20 3 V 8 H 15 "
                                    "A 12 12 9 20 153 M 4 21 V 16 H 9",
     "all green"},
    {StockIcon::Stop, "Stop", "M 8.3 3 H 15.7 L 21 8.3 V 15.7 L 15.7 21 H 8.3 "
                              "L 3 15.7 V 8.3 Z | M 9 9 L 15 15 M 15 9 L 9 15",
     "all deep red gradient red F M 8.3 3 H 15.7 L 21 8.3 V 15.7 "
     "L 15.7 21 H 8.3 L 3 15.7 V 8.3 Z | all paper"},
    {StockIcon::GoTo, "GoTo", "M 3 11 L 21 3 L 13 21 L 11 13 Z",
     "all deep blue gradient blue F M 3 11 L 21 3 L 13 21 L 11 13 Z"},
    // ---------------------------------------------------------------- Pilar
    {StockIcon::ArrowLeft, "ArrowLeft", "M 19 12 H 5 M 12 5 L 5 12 L 12 19", "all blue"},
    {StockIcon::ArrowRight, "ArrowRight", "M 5 12 H 19 M 12 5 L 19 12 L 12 19", "all blue"},
    {StockIcon::ArrowUp, "ArrowUp", "M 12 19 V 5 M 5 12 L 12 5 L 19 12", "all blue"},
    {StockIcon::ArrowDown, "ArrowDown", "M 12 5 V 19 M 5 12 L 12 19 L 19 12", "all blue"},
    {StockIcon::ChevronLeft, "ChevronLeft", "M 15 6 L 9 12 L 15 18", "all blue"},
    {StockIcon::ChevronRight, "ChevronRight", "M 9 6 L 15 12 L 9 18", "all blue"},
    {StockIcon::ChevronUp, "ChevronUp", "M 6 15 L 12 9 L 18 15", "all blue"},
    {StockIcon::ChevronDown, "ChevronDown", "M 6 9 L 12 15 L 18 9", "all blue"},
    {StockIcon::ChevronsLeft, "ChevronsLeft",
     "M 11 17 L 6 12 L 11 7 M 18 17 L 13 12 L 18 7", "all blue"},
    {StockIcon::ChevronsRight, "ChevronsRight",
     "M 6 17 L 11 12 L 6 7 M 13 17 L 18 12 L 13 7", "all blue"},
    {StockIcon::ChevronsUp, "ChevronsUp",
     "M 17 11 L 12 6 L 7 11 M 17 18 L 12 13 L 7 18", "all blue"},
    {StockIcon::ChevronsDown, "ChevronsDown",
     "M 7 6 L 12 11 L 17 6 M 7 13 L 12 18 L 17 13", "all blue"},
    {StockIcon::Flip, "Flip", "M 3 7 L 8 12 L 3 17 Z M 21 7 L 16 12 L 21 17 Z "
                              "M 12 2 V 4 M 12 8 V 10 M 12 14 V 16 M 12 20 V 22",
     "all deep blue gradient blue F M 3 7 L 8 12 L 3 17 Z "
     "F M 21 7 L 16 12 L 21 17 Z"},
    {StockIcon::RotateClockwise, "RotateClockwise",
     "A 12 12 9 0 315 M 18.4 5.6 L 21 8 M 21 3 V 8 H 16", "all blue"},
    {StockIcon::RotateCounterClockwise, "RotateCounterClockwise",
     "A 12 12 9 225 540 M 5.6 5.6 L 3 8 M 3 3 V 8 H 8", "all blue"},
    {StockIcon::Swap, "Swap", "M 8 3 L 4 7 L 8 11 M 4 7 H 20 "
                              "M 16 13 L 20 17 L 16 21 M 20 17 H 4",
     "all blue"},
    // ----------------------------------------------------- Fil och dokument
    {StockIcon::New, "New", "@fil | M 9 15 H 15 M 12 12 V 18", "#fil | all green"},
    {StockIcon::Open, "Open", "@oppen", "#oppen"},
    {StockIcon::Save, "Save", "@disk", "#disk"},
    // Pennan framför disketten.
    {StockIcon::SaveAs, "SaveAs", "@disk | M 12 22 L 13 18.5 L 19.5 12 L 22 14.5 L 15.5 21 Z",
     "#disk | all deep amber gradient amber "
     "F M 12 22 L 13 18.5 L 19.5 12 L 22 14.5 L 15.5 21 Z"},
    // Den bakre disketten i en ljusare ton — den är inte huvudsaken — och den
    // främre på den.
    {StockIcon::SaveAll, "SaveAll",
     "M 2 6 H 13 L 18 11 V 22 H 2 Z | M 6 2 H 17 L 22 7 V 18 H 6 Z M 10 2 V 6 H 16 "
     "M 10 18 V 12 H 18 V 18",
     "all deep blue gradient light blue F M 2 6 H 13 L 18 11 V 22 H 2 Z contrast 2 | "
     "all deep blue gradient blue F M 6 2 H 17 L 22 7 V 18 H 6 Z "
     "dark paper B 10 2 6 4 0 paper B 10 12 8 6 0"},
    {StockIcon::Close, "Close", "@fil | M 9.5 12.5 L 14.5 17.5 M 14.5 12.5 L 9.5 17.5",
     "#fil | all red"},
    {StockIcon::Print, "Print",
     "M 6 18 H 2 V 9 H 22 V 18 H 18 M 6 9 V 2 H 18 V 9 R 6 14 12 8 1",
     "all deep blue gradient blue F M 2 9 H 22 V 18 H 18 V 14 H 6 "
     "V 18 H 2 Z paper B 6 2 12 7 0 paper B 6 14 12 8 1"},
    // Glaset framför sidan.
    {StockIcon::PrintPreview, "PrintPreview", "@fil | C 15 15 3.5 M 17.6 17.6 L 21.5 21.5",
     "#fil | all blue light blue D 15 15 3.5"},
    {StockIcon::Import, "Import",
     "M 8 5 H 2 V 19 H 22 V 5 H 16 | M 12 3 V 15 M 8 11 L 12 15 L 16 11",
     "all deep paper gradient paper F M 2 5 H 22 V 19 H 2 Z | all green"},
    {StockIcon::Export, "Export",
     "M 8 9 H 4 V 22 H 20 V 9 H 16 | M 12 15 V 3 M 8 7 L 12 3 L 16 7",
     "all deep paper gradient paper F M 4 9 H 20 V 22 H 4 Z | all blue"},
    {StockIcon::Properties, "Properties",
     "M 21 4 H 14 M 10 4 H 3 M 21 12 H 12 M 8 12 H 3 M 21 20 H 16 M 12 20 H 3 "
     "M 14 2 V 6 M 8 10 V 14 M 16 18 V 22",
     "over blue M 14 2 V 6 M 8 10 V 14 M 16 18 V 22"},
    {StockIcon::File, "File", "@fil", "#fil"},
    {StockIcon::Document, "Document", "@fil | M 8 13 H 16 M 8 17 H 16 M 8 9 H 10",
     "#fil | all blue"},
    {StockIcon::Folder, "Folder", "@mapp", "#mapp"},
    {StockIcon::FolderOpen, "FolderOpen", "@oppen", "#oppen"},
    {StockIcon::NewFolder, "NewFolder", "@mapp | M 12 12 V 18 M 9 15 H 15",
     "#mapp | all green"},
    // Klockan framför sidan.
    {StockIcon::Recent, "Recent", "@fil | C 15.5 16.5 5 M 15.5 14 V 16.5 L 17 18",
     "#fil | all blue light blue D 15.5 16.5 5"},
    // ---------------------------------------------------- Lagring och plats
    {StockIcon::Drive, "Drive",
     "M 22 12 H 2 M 6 4 L 2 12 V 20 H 22 V 12 L 18 4 Z | D 6 16 1 D 10 16 1",
     "all deep paper gradient paper F M 6 4 H 18 L 22 12 H 2 Z "
     "gradient dark paper F M 2 12 H 22 V 20 H 2 Z | all green"},
    {StockIcon::Network, "Network",
     "R 9 2 6 6 1 R 2 16 6 6 1 R 16 16 6 6 1 M 5 16 V 12 H 19 V 16 M 12 12 V 8",
     "all deep blue gradient blue B 9 2 6 6 1 B 2 16 6 6 1 B 16 16 6 6 1"},
    {StockIcon::Cloud, "Cloud",
     "A 9 12 7 90 343.4 M 15.7 10 H 17.5 A 17.5 14.5 4.5 270 450 M 17.5 19 H 9",
     "all deep blue gradient light blue D 9 12 7 D 17.5 14.5 4.5 B 9 10 8.5 9 0"},
    {StockIcon::HomeFolder, "HomeFolder",
     "@mapp | M 8.5 15.5 L 12 12.5 L 15.5 15.5 M 10 14.5 V 18 H 14 V 14.5",
     "#mapp | all blue"},
    {StockIcon::Desktop, "Desktop", "R 2 3 20 14 2 M 8 21 H 16 M 12 17 V 21",
     "all deep blue gradient blue B 2 3 20 14 2"},
    {StockIcon::Trash, "Trash",
     "M 3 6 H 21 M 19 6 V 22 H 5 V 6 M 8 6 V 2 H 16 V 6 | M 10 11 V 17 M 14 11 V 17",
     "all deep red gradient red F M 5 6 H 19 V 22 H 5 Z | all paper"},
    {StockIcon::Download, "Download",
     "M 21 15 V 21 H 3 V 15 | M 7 10 L 12 15 L 17 10 M 12 15 V 3",
     "all deep paper gradient paper F M 3 15 H 21 V 21 H 3 Z | all green"},
    {StockIcon::Upload, "Upload",
     "M 21 15 V 21 H 3 V 15 | M 17 8 L 12 3 L 7 8 M 12 3 V 15",
     "all deep paper gradient paper F M 3 15 H 21 V 21 H 3 Z | all blue"},
    {StockIcon::Sync, "Sync",
     "A 12 12 7 180 360 M 16.5 9.5 L 19 12 L 21.5 9.5 "
     "A 12 12 7 0 180 M 2.5 14.5 L 5 12 L 7.5 14.5",
     "all blue"},
    // ------------------------------------------------------------ Redigering
    {StockIcon::Undo, "Undo", "M 9 14 L 4 9 L 9 4 M 4 9 H 14.5 "
                              "A 14.5 14.5 5.5 270 450 M 14.5 20 H 11",
     "all blue"},
    {StockIcon::Redo, "Redo", "M 15 14 L 20 9 L 15 4 M 20 9 H 9.5 "
                              "A 9.5 14.5 5.5 90 270 M 9.5 20 H 13",
     "all blue"},
    // Bladen, och handtagen som ett eget lager.
    {StockIcon::Cut, "Cut",
     "M 20 4 L 8.1 15.9 M 14.5 14.5 L 20 20 M 8.1 8.1 L 12 12 | C 6 6 3 C 6 18 3",
     "all deep paper | all deep red gradient red D 6 6 3 D 6 18 3"},
    // Den bakre sidan och den främre.
    {StockIcon::Copy, "Copy", "R 2 2 14 14 1 | R 8 8 14 14 1", "#sidabak | #sidafram"},
    // Skivan, papperet och klämman.
    {StockIcon::Paste, "Paste", "M 4 4 H 20 V 22 H 4 Z | M 7 7 H 17 V 19 H 7 Z | R 8 2 8 4 1",
     "all deep amber gradient amber F M 4 4 H 20 V 22 H 4 Z | "
     "all deep paper gradient paper F M 7 7 H 17 V 19 H 7 Z | "
     "all deep paper gradient dark paper B 8 2 8 4 1"},
    {StockIcon::Delete, "Delete",
     "M 10 5 L 2.5 12 L 10 19 H 22 V 5 Z | M 12 9 L 18 15 M 18 9 L 12 15",
     "all deep red gradient red F M 10 5 L 2.5 12 L 10 19 H 22 V 5 Z | all paper"},
    {StockIcon::SelectAll, "SelectAll",
     "M 6 3 H 3 V 6 M 18 3 H 21 V 6 M 21 18 V 21 H 18 M 6 21 H 3 V 18 "
     "M 9 3 H 10 M 14 3 H 15 M 9 21 H 10 M 14 21 H 15 M 3 9 V 10 M 3 14 V 15 "
     "M 21 9 V 10 M 21 14 V 15 | R 8 8 8 8 1",
     "all blue | all deep blue gradient light blue B 8 8 8 8 1"},
    {StockIcon::Search, "Search", "@lupp", "#lupp"},
    {StockIcon::Replace, "Replace",
     "M 14 4 H 20 V 10 M 17.5 7.5 L 20 10 L 22.5 7.5 "
     "M 10 20 H 4 V 14 M 1.5 16.5 L 4 14 L 6.5 16.5 | R 2 2 8 8 1 | R 14 14 8 8 1",
     "all blue | all deep blue gradient blue B 2 2 8 8 1 | "
     "all deep green gradient green B 14 14 8 8 1"},
    // Skaftet, och borsten som ett eget lager.
    {StockIcon::Clear, "Clear",
     "M 10 2 H 14 V 10 H 20 V 14 H 4 V 10 H 10 Z | "
     "M 5 14 L 3 22 H 21 L 19 14 Z M 9 22 L 10 18 M 15 22 L 14 18",
     "all deep amber gradient dark amber F M 10 2 H 14 V 10 H 20 V 14 H 4 V 10 H 10 Z | "
     "all deep amber gradient amber F M 5 14 L 3 22 H 21 L 19 14 Z"},
    {StockIcon::Edit, "Edit", "M 3 21 L 4 16.5 L 17 3.5 L 20.5 7 L 7.5 20 Z "
                              "M 14.5 6 L 18 9.5",
     "all deep amber gradient amber F M 3 21 L 4 16.5 L 17 3.5 L 20.5 7 L 7.5 20 Z"},
    {StockIcon::Rename, "Rename",
     "M 5 8 H 2 V 16 H 5 M 13 8 H 22 V 16 H 13 | M 9 5 V 19 M 7 4 Q 9 4 9 6 Q 9 4 11 4 "
     "M 7 20 Q 9 20 9 18 Q 9 20 11 20",
     "all deep paper paper B 2 8 20 8 0 | all blue"},
    {StockIcon::Duplicate, "Duplicate",
     "R 2 2 14 14 1 | R 8 8 14 14 1 | M 15 12 V 18 M 12 15 H 18",
     "#sidabak | #sidafram | all green"},
    // ------------------------------------------------------ Listor och objekt
    {StockIcon::Add, "Add", "M 5 12 H 19 M 12 5 V 19", "all green"},
    {StockIcon::Remove, "Remove", "M 5 12 H 19", "all red"},
    // Raderna i textfärgen, och pilen i färg som ett eget lager.
    {StockIcon::MoveUp, "MoveUp", "@rader | M 18 20 V 4 M 15 7 L 18 4 L 21 7", "| all blue"},
    {StockIcon::MoveDown, "MoveDown", "@rader | M 18 4 V 20 M 15 17 L 18 20 L 21 17",
     "| all blue"},
    {StockIcon::MoveFirst, "MoveFirst", "M 5 3 H 19 | M 12 21 V 8 M 7 13 L 12 8 L 17 13",
     "| all blue"},
    {StockIcon::MoveLast, "MoveLast", "M 5 21 H 19 | M 12 3 V 16 M 7 11 L 12 16 L 17 11",
     "| all blue"},
    {StockIcon::SortAscending, "SortAscending",
     "M 11 12 H 15 M 11 16 H 18 M 11 20 H 21 | M 3 8 L 7 4 L 11 8 M 7 4 V 20", "| all blue"},
    {StockIcon::SortDescending, "SortDescending",
     "M 11 4 H 21 M 11 8 H 18 M 11 12 H 15 | M 3 16 L 7 20 L 11 16 M 7 20 V 4", "| all blue"},
    {StockIcon::Filter, "Filter", "M 3 6 H 21 M 7 12 H 17 M 10 18 H 14", "all blue"},
    {StockIcon::Group, "Group",
     "M 3 7 V 3 H 7 M 17 3 H 21 V 7 M 21 17 V 21 H 17 M 7 21 H 3 V 17 | R 7 7 7 5 1 | "
     "R 10 12 7 5 1",
     "all blue | all deep blue gradient blue B 7 7 7 5 1 | "
     "all deep blue gradient light blue B 10 12 7 5 1"},
    {StockIcon::Expand, "Expand", "R 3 3 18 18 2 | M 8 12 H 16 M 12 8 V 16",
     "all deep paper gradient paper B 3 3 18 18 2 | all blue"},
    {StockIcon::Collapse, "Collapse", "R 3 3 18 18 2 | M 8 12 H 16",
     "all deep paper gradient paper B 3 3 18 18 2 | all blue"},
    {StockIcon::ExpandAll, "ExpandAll", "@veck | M 15 19 L 12 22 L 9 19 M 15 5 L 12 2 L 9 5",
     "| all blue"},
    {StockIcon::CollapseAll, "CollapseAll",
     "@veck | M 15 19 L 12 16 L 9 19 M 15 5 L 12 8 L 9 5", "| all blue"},
    // ----------------------------------------------------------------- Vyer
    {StockIcon::ListView, "ListView",
     "M 14 4 H 21 M 14 9 H 21 M 14 15 H 21 M 14 20 H 21 | R 3 3 7 7 1 R 3 14 7 7 1",
     "| all deep blue gradient blue B 3 3 7 7 1 B 3 14 7 7 1"},
    {StockIcon::DetailsView, "DetailsView",
     "M 9 5.5 H 14 M 17 5.5 H 21 M 9 12 H 14 M 17 12 H 21 M 9 18.5 H 14 M 17 18.5 H 21 | "
     "B 3 4 3 3 0.5 B 3 10.5 3 3 0.5 B 3 17 3 3 0.5",
     "| all blue"},
    {StockIcon::IconView, "IconView",
     "R 3 3 7 7 1 R 14 3 7 7 1 R 3 14 7 7 1 R 14 14 7 7 1",
     "all deep blue gradient blue B 3 3 7 7 1 B 14 3 7 7 1 "
     "B 3 14 7 7 1 B 14 14 7 7 1"},
    {StockIcon::Grid, "Grid", "R 3 3 18 18 2 M 3 9 H 21 M 3 15 H 21 M 9 3 V 21 "
                              "M 15 3 V 21",
     "all deep blue gradient light blue B 3 3 18 18 2"},
    {StockIcon::SplitView, "SplitView", "R 3 3 18 18 2 M 12 3 V 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 12 3 9 18 2"},
    {StockIcon::Sidebar, "Sidebar", "R 3 3 18 18 2 M 9 3 V 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 3 3 6 18 2"},
    {StockIcon::FullScreen, "FullScreen",
     "M 8 3 H 3 V 8 M 21 8 V 3 H 16 M 3 16 V 21 H 8 M 16 21 H 21 V 16", "all blue"},
    {StockIcon::ExitFullScreen, "ExitFullScreen",
     "M 8 3 V 8 H 3 M 21 8 H 16 V 3 M 3 16 H 8 V 21 M 16 21 V 16 H 21", "all blue"},
    // ------------------------------------------------------ Zoom och visning
    {StockIcon::ZoomIn, "ZoomIn", "@lupp | M 11 8 V 14 M 8 11 H 14", "#lupp | all blue"},
    {StockIcon::ZoomOut, "ZoomOut", "@lupp | M 8 11 H 14", "#lupp | all blue"},
    {StockIcon::ZoomToFit, "ZoomToFit",
     "M 21 21 L 15 15 M 21 16.2 V 21 H 16.2 M 3 16.2 V 21 H 7.8 M 3 21 L 9 15 "
     "M 21 7.8 V 3 H 16.2 M 21 3 L 15 9 M 3 7.8 V 3 H 7.8 M 3 3 L 9 9",
     "all blue"},
    {StockIcon::ActualSize, "ActualSize",
     "R 2 3 20 18 2 | M 6 9 L 8 7.5 V 16.5 M 16 9 L 18 7.5 V 16.5 D 12 10 0.9 D 12 14 0.9",
     "all deep blue gradient light blue B 2 3 20 18 2 | all deep blue"},
    // Ögat: vitan, och irisen som ett eget lager.
    {StockIcon::Show, "Show", "@oga | C 12 12 3",
     "all deep blue paper F M 2 12 Q 12 0 22 12 Q 12 24 2 12 Z | "
     "all deep blue gradient blue D 12 12 3"},
    {StockIcon::Hide, "Hide", "@oga | C 12 12 3 | M 3 3 L 21 21",
     "all deep blue paper F M 2 12 Q 12 0 22 12 Q 12 24 2 12 Z | "
     "all deep blue gradient blue D 12 12 3 | all red"},
    // Byglen, och kroppen framför som ett eget lager.
    {StockIcon::Lock, "Lock", "M 7 11 V 7 A 12 7 5 180 360 M 17 7 V 11 | R 3 11 18 11 2",
     "all deep paper | all deep amber gradient amber B 3 11 18 11 2"},
    {StockIcon::Unlock, "Unlock", "M 7 11 V 7 A 12 7 5 180 348 | R 3 11 18 11 2",
     "all deep paper | all deep amber gradient amber B 3 11 18 11 2"},
    // Nålen, och huvudet framför.
    {StockIcon::Pin, "Pin",
     "M 12 17 V 22 | M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 R 6 2 12 4 1",
     "all deep paper | all deep red gradient red "
     "F M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 Z B 6 2 12 4 1"},
    {StockIcon::Unpin, "Unpin",
     "M 12 17 V 22 | M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 R 6 2 12 4 1 | "
     "M 2 2 L 22 22",
     "all deep paper | all deep red gradient red "
     "F M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 Z B 6 2 12 4 1 |"},
    // ------------------------------------------------- Text och formatering
    {StockIcon::Bold, "Bold", "M 6 4 V 20 H 15 M 6 12 H 15 M 6 4 H 14 "
                              "A 15 16 4 270 450 A 14 8 4 270 450",
     "all deep blue"},
    {StockIcon::Italic, "Italic", "M 19 4 H 10 M 14 20 H 5 M 15 4 L 9 20", "all deep blue"},
    {StockIcon::Underline, "Underline",
     "M 6 4 V 10 M 18 4 V 10 A 12 10 6 0 180 | M 4 20 H 20", "all deep blue | all blue"},
    {StockIcon::AlignLeft, "AlignLeft", "M 3 5 H 21 M 3 10 H 15 M 3 15 H 21 M 3 20 H 13",
     "all blue"},
    {StockIcon::AlignCenter, "AlignCenter",
     "M 3 5 H 21 M 7 10 H 17 M 3 15 H 21 M 8 20 H 16", "all blue"},
    {StockIcon::AlignRight, "AlignRight",
     "M 3 5 H 21 M 9 10 H 21 M 3 15 H 21 M 11 20 H 21", "all blue"},
    {StockIcon::AlignJustify, "AlignJustify",
     "M 3 5 H 21 M 3 10 H 21 M 3 15 H 21 M 3 20 H 21", "all blue"},
    {StockIcon::BulletList, "BulletList",
     "M 8 6 H 21 M 8 12 H 21 M 8 18 H 21 | D 4 6 1 D 4 12 1 D 4 18 1", "| all blue"},
    {StockIcon::NumberedList, "NumberedList",
     "M 10 6 H 21 M 10 12 H 21 M 10 18 H 21 | M 4 6 H 5 V 10 M 4 10 H 6 "
     "M 4 14 Q 6 13.5 6 15 Q 6 16 4 18 H 6",
     "| all blue"},
    {StockIcon::Font, "Font", "M 3 16 L 7.5 7 L 12 16 M 4.5 13 H 10.5 M 21 14 H 16 "
                              "M 16 16 V 12.5 A 18.5 12.5 2.5 180 360 M 21 12.5 V 16",
     "all deep blue"},
    {StockIcon::TextColor, "TextColor", "M 6 16 L 12 4 L 18 16 M 8 12 H 16 | B 4 18.5 16 3 0",
     "all deep blue | all red"},
    {StockIcon::FillColor, "FillColor",
     "M 19 11 L 11 3 L 2 12 L 8 18 Z M 5 2 L 10 7 | "
     "M 20 16 Q 18 18.5 18 20 M 22 20 Q 22 18.5 20 16 A 20 20 2 0 180",
     "all deep paper gradient paper F M 19 11 L 11 3 L 2 12 L 8 18 Z | "
     "all deep blue gradient blue D 20 20 2 F M 20 16 L 18.3 19.5 H 21.7 Z"},
    // ---------------------------------------------------- Status och dialog
    // Ytan, och tecknet på den som ett eget lager.
    {StockIcon::Information, "Information", "C 12 12 10 | M 12 16 V 11 D 12 7.8 1",
     "#rund | all paper"},
    {StockIcon::Warning, "Warning", "M 12 2.5 L 22 20.5 H 2 Z | M 12 9 V 13 D 12 17 1",
     "all deep amber gradient amber F M 12 2.5 L 22 20.5 H 2 Z | all deep amber"},
    {StockIcon::Error, "Error", "C 12 12 10 | M 15 9 L 9 15 M 9 9 L 15 15",
     "all deep red gradient red D 12 12 10 | all paper"},
    {StockIcon::Question, "Question",
     "C 12 12 10 | A 12 9.5 3 190 370 M 15 10 Q 15 12 12 13 D 12 17 1",
     "#rund | all paper"},
    {StockIcon::Done, "Done", "M 20 6 L 9 17 L 4 12", "all green"},
    {StockIcon::Cancel, "Cancel", "@kryss", "all red"},
    {StockIcon::Help, "Help",
     "C 12 12 10 C 12 12 4 M 4.9 4.9 L 9.2 9.2 M 14.8 14.8 L 19.1 19.1 "
     "M 14.8 9.2 L 19.1 4.9 M 9.2 14.8 L 4.9 19.1",
     "all blue"},
    {StockIcon::Tip, "Tip",
     "M 15 14 Q 15.3 12.8 16.5 11.5 Q 18 10 18 8 A 12 8 6 180 360 "
     "M 6 8 Q 6 10 7.5 11.5 Q 8.7 12.8 9 14 M 9 14 V 18 M 15 14 V 18 "
     "M 9 18 H 15 M 10 22 H 14",
     "all deep amber gradient yellow F M 15 14 Q 15.3 12.8 16.5 11.5 "
     "Q 18 10 18 8 Q 18 2 12 2 Q 6 2 6 8 Q 6 10 7.5 11.5 Q 8.7 12.8 9 14 "
     "V 18 H 15 Z"},
    {StockIcon::Loading, "Loading", "A 12 12 9 0 288", "all blue"},
    // ------------------------------------------------------ Fönster och meny
    {StockIcon::Menu, "Menu", "M 4 6 H 20 M 4 12 H 20 M 4 18 H 20", "all blue"},
    {StockIcon::MoreHorizontal, "MoreHorizontal", "D 5 12 1.5 D 12 12 1.5 D 19 12 1.5",
     "all blue"},
    {StockIcon::MoreVertical, "MoreVertical", "D 12 5 1.5 D 12 12 1.5 D 12 19 1.5",
     "all blue"},
    {StockIcon::Settings, "Settings",
     "C 12 12 6 C 12 12 2.5 B 10.3 2.5 3.4 4 0.5 B 10.3 17.5 3.4 4 0.5 "
     "B 2.5 10.3 4 3.4 0.5 B 17.5 10.3 4 3.4 0.5 "
     "F M 15 17.4 L 17.4 15 L 19.9 17.5 L 17.5 19.9 Z "
     "F M 9 17.4 L 6.6 15 L 4.1 17.5 L 6.5 19.9 Z "
     "F M 15 6.6 L 17.4 9 L 19.9 6.5 L 17.5 4.1 Z "
     "F M 9 6.6 L 6.6 9 L 4.1 6.5 L 6.5 4.1 Z",
     "all deep blue gradient blue D 12 12 6 paper D 12 12 2.5"},
    {StockIcon::CloseWindow, "CloseWindow", "@kryss", "all red"},
    {StockIcon::Minimize, "Minimize", "M 5 17 H 19", "all blue"},
    {StockIcon::Maximize, "Maximize", "R 4 4 16 16 1", "all blue"},
    {StockIcon::Restore, "Restore", "R 4 8 12 12 1 M 8 4 H 20 V 16", "all blue"},
    {StockIcon::NewWindow, "NewWindow", "R 2 4 20 16 2 M 2 8 H 22 | M 12 11 V 17 M 9 14 H 15",
     "all deep blue paper B 2 4 20 16 2 gradient blue "
     "F M 2 4 H 22 V 8 H 2 Z | all green"},
    {StockIcon::ExternalLink, "ExternalLink",
     "M 18 13 V 21 H 3 V 6 H 11 | M 15 3 H 21 V 9 M 10 14 L 21 3", "all deep paper | all blue"},
    // --------------------------------------------------- Verktyg och ritning
    {StockIcon::Pointer, "Pointer", "M 4 4 L 20.5 10.7 L 13.5 13.5 L 10.7 20.5 Z",
     "all deep paper gradient paper "
     "F M 4 4 L 20.5 10.7 L 13.5 13.5 L 10.7 20.5 Z"},
    {StockIcon::Pan, "Pan",
     "M 18 11 V 6 A 16 6 2 180 360 M 14 10 V 4 A 12 4 2 180 360 M 10 4 V 10.5 "
     "A 8 6 2 180 360 M 6 6 V 14 A 20 8 2 180 360 M 22 8 V 14 A 14 14 8 0 90 "
     "M 14 22 H 12 Q 9 22 6 19.7 L 2.4 16.1 Q 1 14.5 2.4 13.2 Q 3.8 12 5.2 13.2 "
     "L 7 15",
     "all blue"},
    {StockIcon::Measure, "Measure",
     "M 22 17 L 17 22 L 2 7 L 7 2 Z M 14.5 12.5 L 16.5 10.5 "
     "M 11.5 9.5 L 13.5 7.5 M 8.5 6.5 L 10.5 4.5 M 17.5 15.5 L 19.5 13.5",
     "all deep amber gradient amber F M 22 17 L 17 22 L 2 7 L 7 2 Z"},
    {StockIcon::Pen, "Pen",
     "M 15 21.3 L 12.7 19 L 18.7 13 L 21 15.3 Z M 18 13 L 16.6 6.1 L 3.2 2 "
     "L 2 3.2 L 6.1 16.6 L 13 18 M 2.3 2.3 L 9.6 9.6 C 11 11 2",
     "all deep blue gradient blue "
     "F M 18 13 L 16.6 6.1 L 3.2 2 L 2 3.2 L 6.1 16.6 L 13 18 Z"},
    {StockIcon::Eraser, "Eraser",
     "M 21 21 H 8 L 2 15 L 14.4 2.6 L 22 10.2 L 11.2 21 M 5.1 11.9 L 13.9 19.9",
     "all deep red paper F M 5.1 11.9 L 13.9 19.9 L 11.2 21 H 8 L 2 15 Z "
     "gradient red F M 14.4 2.6 L 22 10.2 L 13.9 19.9 L 5.1 11.9 Z"},
    {StockIcon::Eyedropper, "Eyedropper",
     "M 2 22 L 3 21 H 6 L 15 12 M 3 21 V 18 L 12 9 M 15 6 L 18.4 2.6 "
     "Q 19.9 1.1 21.4 2.6 Q 22.9 4.1 21.4 5.6 L 18 9 L 18.4 9.4 "
     "Q 19.9 10.9 18.4 12.4 Q 16.9 13.9 15.4 12.4 L 11.6 8.6 Q 10.1 7.1 11.6 5.6 "
     "Q 13.1 4.1 14.6 5.6 Z",
     "all deep blue gradient blue F M 15 6 L 18.4 2.6 "
     "Q 19.9 1.1 21.4 2.6 Q 22.9 4.1 21.4 5.6 L 18 9 Z"},
    // Triangeln, och fyrkanten och cirkeln framför som egna lager.
    {StockIcon::Shape, "Shape", "M 8 10 L 12 3 L 16 10 Z | R 3 14 7 7 1 | C 17.5 17.5 3.5",
     "all deep red gradient red F M 8 10 L 12 3 L 16 10 Z | "
     "all deep blue gradient blue B 3 14 7 7 1 | "
     "all deep green gradient green D 17.5 17.5 3.5"},
    {StockIcon::Line, "Line", "M 4 20 L 20 4", "all blue"},
    {StockIcon::Rectangle, "Rectangle", "R 3 5 18 14 2",
     "all deep blue gradient light blue B 3 5 18 14 2"},
    {StockIcon::Ellipse, "Ellipse", "E 12 12 10 7", "all blue"},
    {StockIcon::Polygon, "Polygon", "M 12 2.5 L 21.5 9.4 L 17.9 20.5 H 6.1 L 2.5 9.4 Z",
     "all deep blue gradient light blue "
     "F M 12 2.5 L 21.5 9.4 L 17.9 20.5 H 6.1 L 2.5 9.4 Z"},
    {StockIcon::Text, "Text", "M 12 4 V 20 M 4 7 V 4 H 20 V 7 M 9 20 H 15", "all deep blue"},
    // Skikten på varandra, det översta ljusast.
    {StockIcon::Layers, "Layers",
     "M 2 17 L 12 12 L 22 17 L 12 22 Z | M 2 12 L 12 7 L 22 12 L 12 17 Z | "
     "M 2 7 L 12 2 L 22 7 L 12 12 Z",
     "all deep blue gradient light blue F M 2 17 L 12 12 L 22 17 L 12 22 Z | "
     "all deep blue gradient blue F M 2 12 L 12 7 L 22 12 L 12 17 Z | "
     "all deep blue gradient light blue F M 2 7 L 12 2 L 22 7 L 12 12 Z"},
    {StockIcon::Anchor, "Anchor",
     "M 12 22 V 8 C 12 5 3 M 5 12 H 2 A 12 12 10 0 180 M 22 12 H 19", "all blue"},
    {StockIcon::Magnet, "Magnet",
     "M 4 3 V 12 M 20 12 V 3 A 12 12 8 0 180 M 9 3 V 12 M 15 12 V 3 "
     "A 12 12 3 0 180 M 4 3 H 9 M 15 3 H 20 M 4 7 H 9 M 15 7 H 20",
     "all deep paper red B 4 3 5 4 0 B 15 3 5 4 0"},
    // ----------------------------------------------------------------- Media
    {StockIcon::Play, "Play", "M 6 3 L 20 12 L 6 21 Z",
     "all deep green gradient green F M 6 3 L 20 12 L 6 21 Z"},
    {StockIcon::Pause, "Pause", "R 6 4 4 16 1 R 14 4 4 16 1",
     "all deep blue gradient blue B 6 4 4 16 1 B 14 4 4 16 1"},
    {StockIcon::MediaStop, "MediaStop", "R 6 6 12 12 2",
     "all deep red gradient red B 6 6 12 12 2"},
    {StockIcon::Record, "Record", "C 12 12 8 | C 12 12 4",
     "all deep paper gradient paper D 12 12 8 | all deep red gradient red D 12 12 4"},
    {StockIcon::PreviousTrack, "PreviousTrack", "M 19 20 L 9 12 L 19 4 Z M 5 19 V 5",
     "all deep blue gradient blue F M 19 20 L 9 12 L 19 4 Z"},
    {StockIcon::NextTrack, "NextTrack", "M 5 4 L 15 12 L 5 20 Z M 19 5 V 19",
     "all deep blue gradient blue F M 5 4 L 15 12 L 5 20 Z"},
    {StockIcon::Repeat, "Repeat",
     "M 17 2 L 21 6 L 17 10 M 3 11 V 6 H 21 M 7 22 L 3 18 L 7 14 M 21 13 V 18 H 3",
     "all blue"},
    {StockIcon::Shuffle, "Shuffle",
     "M 18 14 L 22 18 L 18 22 M 18 2 L 22 6 L 18 10 M 2 18 H 4 Q 6 18 7.3 16.3 "
     "L 12.7 8.7 Q 14 6 16 6 H 22 M 2 6 H 4 Q 6.4 6 7.6 8.2 M 22 18 H 16 "
     "Q 14 18 12.7 16.2 L 12.3 15.7",
     "all blue"},
    {StockIcon::SoundOn, "SoundOn", "@hogtalare | A 12 12 5 323 397 A 12 12 9 315 405",
     "all deep paper gradient paper F @hogtalare | all blue"},
    {StockIcon::SoundOff, "SoundOff", "@hogtalare | M 16 9 L 22 15 M 22 9 L 16 15",
     "all deep paper gradient paper F @hogtalare | all red"},
    // -------------------------------------------- Kommunikation och person
    {StockIcon::User, "User", "C 12 7 4 M 19 21 V 19 Q 19 15 15 15 H 9 Q 5 15 5 19 V 21",
     "all deep blue gradient blue D 12 7 4 "
     "F M 19 21 V 19 Q 19 15 15 15 H 9 Q 5 15 5 19 V 21 Z"},
    {StockIcon::Users, "Users",
     "C 9 7 4 M 16 21 V 19 Q 16 15 12 15 H 6 Q 2 15 2 19 V 21 "
     "M 22 21 V 19 Q 22 16 19 15.1 A 15 7 4 284.5 435.5",
     "all deep blue gradient blue D 9 7 4 "
     "F M 16 21 V 19 Q 16 15 12 15 H 6 Q 2 15 2 19 V 21 Z"},
    {StockIcon::Mail, "Mail", "R 2 4 20 16 2 M 22 7 L 13 12.7 Q 12 13.3 11 12.7 L 2 7",
     "all deep paper gradient paper B 2 4 20 16 2"},
    {StockIcon::Message, "Message", "M 21 17 H 7 L 3 21 V 3 H 21 Z",
     "all deep blue gradient blue F M 21 17 H 7 L 3 21 V 3 H 21 Z"},
    {StockIcon::Notification, "Notification",
     "M 4 17 H 20 M 20 17 Q 18 14 18 8 A 12 8 6 180 360 M 4 17 Q 6 14 6 8 "
     "M 10.3 21 Q 12 23 13.7 21",
     "all deep amber gradient amber F M 4 17 Q 6 14 6 8 "
     "Q 6 2 12 2 Q 18 2 18 8 Q 18 14 20 17 Z"},
    {StockIcon::Share, "Share",
     "C 18 5 3 C 6 12 3 C 18 19 3 M 8.6 13.5 L 15.4 17.5 M 15.4 6.5 L 8.6 10.5",
     "all deep blue gradient blue D 18 5 3 D 6 12 3 D 18 19 3"},
    {StockIcon::Link, "Link",
     "M 9 17 H 7 A 7 12 5 90 270 M 7 7 H 9 M 15 7 H 17 A 17 12 5 270 450 "
     "M 17 17 H 15 M 8 12 H 16",
     "all blue"},
    {StockIcon::Attach, "Attach",
     "M 16 6 V 17 A 12 17 4 0 180 M 8 17 V 5 A 10.5 5 2.5 180 360 M 13 5 V 16",
     "all blue"},
    // --------------------------------------------------------- Tid och data
    {StockIcon::Calendar, "Calendar", "R 3 4 18 18 2 M 8 2 V 6 M 16 2 V 6 M 3 10 H 21",
     "all deep paper gradient paper B 3 4 18 18 2 "
     "gradient red B 3 4 18 6 0"},
    {StockIcon::Clock, "Clock", "C 12 12 10 | M 12 6 V 12 L 16 14",
     "all deep blue gradient paper D 12 12 10 | all deep blue"},
    {StockIcon::History, "History",
     "A 12 12 9 270 540 M 12 3 Q 8 3 5.3 5.7 L 3 8 M 3 3 V 8 H 8 | M 12 7 V 12 L 16 14",
     "all blue | all deep blue"},
    // Axlarna, och staplarna som egna lager.
    {StockIcon::Chart, "Chart", "M 3 3 V 21 H 21 | R 16 9 4 8 0 | R 11 5 4 12 0 | R 6 13 4 4 0",
     "all deep paper | all deep blue gradient blue B 16 9 4 8 0 | "
     "all deep green gradient green B 11 5 4 12 0 | "
     "all deep red gradient red B 6 13 4 4 0"},
    {StockIcon::Table, "Table", "R 3 3 18 18 2 M 3 9 H 21 M 3 15 H 21 M 12 3 V 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 3 3 18 6 0"},
    {StockIcon::Database, "Database",
     "E 12 5 9 3 M 3 5 V 19 M 21 5 V 19 U 12 12 9 3 0 180 U 12 19 9 3 0 180",
     "all deep blue gradient blue "
     "F M 3 5 Q 3 2 12 2 Q 21 2 21 5 V 19 Q 21 22 12 22 Q 3 22 3 19 Z"},
    {StockIcon::Funnel, "Funnel", "M 3 3 H 21 L 14 11.5 V 21 L 10 19 V 11.5 Z",
     "all deep blue gradient blue F M 3 3 H 21 L 14 11.5 V 21 L 10 19 "
     "V 11.5 Z"},
    // Lappen, och hålet som ett eget lager.
    {StockIcon::Tag, "Tag", "M 12 2 H 2 V 12 L 12 22 L 22 12 Z | D 7.5 7.5 1.4",
     "all deep amber gradient amber F M 12 2 H 2 V 12 L 12 22 L 22 12 Z | all paper"},
    {StockIcon::Bookmark, "Bookmark", "M 19 21 L 12 17 L 5 21 V 3 H 19 Z",
     "all deep red gradient red F M 19 21 L 12 17 L 5 21 V 3 H 19 Z"},
    {StockIcon::Star, "Star",
     "M 12 2 L 15.1 8.3 L 22 9.3 L 17 14.1 L 18.2 21 L 12 17.8 L 5.8 21 L 7 14.1 "
     "L 2 9.3 L 8.9 8.3 Z",
     "all deep amber gradient yellow F M 12 2 L 15.1 8.3 L 22 9.3 "
     "L 17 14.1 L 18.2 21 L 12 17.8 L 5.8 21 L 7 14.1 L 2 9.3 L 8.9 8.3 Z"},
    // Stången, och duken framför.
    {StockIcon::Flag, "Flag",
     "M 4 22 V 3 | M 4 3 Q 8 1 12 3 Q 16 5 20 3 V 15 Q 16 17 12 15 Q 8 13 4 15 Z",
     "all deep paper | all deep red gradient red "
     "F M 4 3 Q 8 1 12 3 Q 16 5 20 3 V 15 Q 16 17 12 15 Q 8 13 4 15 Z"},
    // -------------------------------------------------------------- Säkerhet
    {StockIcon::SecurityLock, "SecurityLock",
     "M 7 11 V 7 A 12 7 5 180 360 M 17 7 V 11 | R 3 11 18 11 2 | "
     "D 12 15.5 1.5 M 12 16 V 18.5",
     "all deep paper | all deep amber gradient amber B 3 11 18 11 2 | all deep amber"},
    {StockIcon::Key, "Key",
     "C 7.5 15.5 5.5 M 21 2 L 11.4 11.6 M 15.5 7.5 L 18.5 10.5 L 22 7 L 19 4",
     "all deep amber gradient amber D 7.5 15.5 5.5"},
    {StockIcon::Shield, "Shield",
     "M 20 13 Q 20 19 12 22 Q 4 19 4 13 V 5.5 Q 8 5 12 2 Q 16 5 20 5.5 Z",
     "all deep blue gradient blue F M 20 13 Q 20 19 12 22 Q 4 19 4 13 "
     "V 5.5 Q 8 5 12 2 Q 16 5 20 5.5 Z"},
    // Ramen, och pilen som ett eget lager.
    {StockIcon::LogIn, "LogIn", "M 15 3 H 21 V 21 H 15 | M 10 17 L 15 12 L 10 7 M 15 12 H 3",
     "all deep paper | all green"},
    {StockIcon::LogOut, "LogOut", "M 9 21 H 3 V 3 H 9 | M 16 17 L 21 12 L 16 7 M 21 12 H 9",
     "all deep paper | all red"},
    // ------------------------------------------------------------ CAD — rita
    // Greppen (de små fyllda rutorna) är CAD-programmens: där en form går att
    // ta i. Samma ruta, 3.2 i sida, på varje ritad form som har dem. Formen
    // är blå som de andra ritverktygen, och det som MÄTS blått ovanpå en svart
    // hjälplinje.
    // Pekarna mot punkten, och punkten som ett eget lager.
    {StockIcon::Point, "Point",
     "M 5 5 L 8.5 8.5 M 19 5 L 15.5 8.5 M 5 19 L 8.5 15.5 "
     "M 19 19 L 15.5 15.5 | D 12 12 2.2",
     "| all blue"},
    {StockIcon::Polyline, "Polyline",
     "M 3 19 L 9 7 L 15 15 L 21 5 B 1.4 17.4 3.2 3.2 0.4 B 7.4 5.4 3.2 3.2 0.4 "
     "B 13.4 13.4 3.2 3.2 0.4 B 19.4 3.4 3.2 3.2 0.4",
     "all blue"},
    {StockIcon::Arc, "Arc",
     "A 12 17 9 180 360 B 1.4 15.4 3.2 3.2 0.4 B 19.4 15.4 3.2 3.2 0.4 "
     "B 10.4 6.4 3.2 3.2 0.4",
     "all blue"},
    {StockIcon::Circle, "Circle", "C 12 12 9 M 10 12 H 14 M 12 10 V 14", "all blue"},
    {StockIcon::Spline, "Spline",
     "M 3 18 Q 7 4 12 12 Q 17 20 21 6 B 1.4 16.4 3.2 3.2 0.4 "
     "B 19.4 4.4 3.2 3.2 0.4",
     "all blue"},
    {StockIcon::Hatch, "Hatch",
     "R 3 3 18 18 2 M 3 11 L 11 3 M 3 19 L 19 3 M 9 21 L 21 9 M 17 21 L 21 17",
     "all deep paper paper B 3 3 18 18 2 "
     "over blue M 3 11 L 11 3 M 3 19 L 19 3 M 9 21 L 21 9 M 17 21 L 21 17"},
    {StockIcon::Dimension, "Dimension",
     "M 4 5 V 19 M 20 5 V 19 M 6 12 H 18 F M 4.5 12 L 8.5 9.5 V 14.5 Z "
     "F M 19.5 12 L 15.5 9.5 V 14.5 Z",
     "over blue M 6 12 H 18 F M 4.5 12 L 8.5 9.5 V 14.5 Z "
     "F M 19.5 12 L 15.5 9.5 V 14.5 Z"},
    {StockIcon::AngleDimension, "AngleDimension",
     "M 4 20 H 21 M 4 20 L 14 6.3 A 4 20 11 306 360", "over blue A 4 20 11 306 360"},
    // ----------------------------------------------------------- CAD — ändra
    // RESULTATET blått, det som tas bort rött.
    {StockIcon::Move, "Move",
     "M 12 2 V 22 M 2 12 H 22 M 9 5 L 12 2 L 15 5 M 9 19 L 12 22 L 15 19 "
     "M 5 9 L 2 12 L 5 15 M 19 9 L 22 12 L 19 15",
     "all blue"},
    {StockIcon::Rotate, "Rotate",
     "R 8.5 8.5 7 7 1 A 12 12 9 0 315 M 18.4 5.6 L 21 8 M 21 3 V 8 H 16",
     "all deep blue gradient blue B 8.5 8.5 7 7 1"},
    {StockIcon::Scale, "Scale",
     "R 3 13 8 8 1 M 3 9 V 3 H 9 M 15 3 H 21 V 9 M 21 15 V 21 H 15 "
     "M 13 11 L 17 7 M 13 7 H 17 V 11",
     "all deep blue gradient blue B 3 13 8 8 1"},
    {StockIcon::Offset, "Offset",
     "M 3 21 V 13 Q 3 5 11 5 H 21 M 9 21 V 13 Q 9 11 11 11 H 21",
     "over blue M 9 21 V 13 Q 9 11 11 11 H 21"},
    {StockIcon::Trim, "Trim",
     "M 14 3 V 21 M 3 12 H 14 M 16.5 12 H 17.5 M 20 12 H 21",
     "over red M 16.5 12 H 17.5 M 20 12 H 21"},
    {StockIcon::Extend, "Extend",
     "M 20 3 V 21 M 3 12 H 17 M 14 9 L 17 12 L 14 15",
     "over blue M 10 12 H 17 M 14 9 L 17 12 L 14 15"},
    {StockIcon::Fillet, "Fillet", "M 4 21 V 15 M 14 5 H 21 A 14 15 10 180 270",
     "over blue A 14 15 10 180 270"},
    {StockIcon::Chamfer, "Chamfer", "M 4 21 V 11 L 10 5 H 21", "over blue M 4 11 L 10 5"},
    {StockIcon::Array, "Array",
     "R 3 3 4 4 0.8 R 10 3 4 4 0.8 R 17 3 4 4 0.8 R 3 10 4 4 0.8 "
     "R 10 10 4 4 0.8 R 17 10 4 4 0.8 R 3 17 4 4 0.8 R 10 17 4 4 0.8 "
     "R 17 17 4 4 0.8",
     "all deep blue gradient light blue B 3 3 4 4 0.8 B 10 3 4 4 0.8 "
     "B 17 3 4 4 0.8 B 3 10 4 4 0.8 B 10 10 4 4 0.8 B 17 10 4 4 0.8 "
     "B 3 17 4 4 0.8 B 10 17 4 4 0.8 B 17 17 4 4 0.8 "
     "gradient blue B 3 3 4 4 0.8"},
    // ----------------------------------------- CAD — fästpunkter och hjälp
    // Fästpunkternas märken är CAD-programmens: ruta vid änden, triangel mitt
    // på, ring i centrum, kryss i skärningen, vinkel vid normalen — gröna, som
    // markören i ett CAD-program.
    {StockIcon::SnapEndpoint, "SnapEndpoint", "M 4 20 L 17 7 R 14 4 6 6 0",
     "over green R 14 4 6 6 0"},
    {StockIcon::SnapMidpoint, "SnapMidpoint",
     "M 3 16 H 21 M 12 9.5 L 17.5 19 H 6.5 Z", "over green M 12 9.5 L 17.5 19 H 6.5 Z"},
    {StockIcon::SnapCenter, "SnapCenter", "A 12 12 9 160 380 C 12 12 3",
     "over green C 12 12 3"},
    {StockIcon::SnapIntersection, "SnapIntersection",
     "M 3 15 H 21 M 9 3 V 21 M 5.5 11.5 L 12.5 18.5 M 12.5 11.5 L 5.5 18.5",
     "over green M 5.5 11.5 L 12.5 18.5 "
     "M 12.5 11.5 L 5.5 18.5"},
    {StockIcon::SnapPerpendicular, "SnapPerpendicular",
     "M 3 19 H 21 M 10 19 V 4 M 10 14 H 15 V 19", "over green M 10 14 H 15 V 19"},
    {StockIcon::SnapGrid, "SnapGrid",
     "D 4 4 1.1 D 9.3 4 1.1 D 14.7 4 1.1 D 20 4 1.1 D 4 9.3 1.1 D 9.3 9.3 1.1 "
     "D 14.7 9.3 1.1 D 20 9.3 1.1 D 4 14.7 1.1 D 9.3 14.7 1.1 D 14.7 14.7 1.1 "
     "D 20 14.7 1.1 D 4 20 1.1 D 9.3 20 1.1 D 14.7 20 1.1 D 20 20 1.1 "
     "C 14.7 9.3 2.8",
     "over green C 14.7 9.3 2.8"},
    {StockIcon::Ortho, "Ortho",
     "M 5 4 V 19 H 20 M 2 7 L 5 4 L 8 7 M 17 16 L 20 19 L 17 22", "all blue"},
    {StockIcon::Crosshair, "Crosshair",
     "M 12 2 V 9 M 12 15 V 22 M 2 12 H 9 M 15 12 H 22 R 9 9 6 6 0",
     "over blue R 9 9 6 6 0"},
    // --------------------------------------------------- 3D — vyer och kamera
    // Vyerna är kuben med den yta man ser rakt på FYLLD och blå; den
    // isometriska har hörnet mot betraktaren markerat. Axlarna X röd, Y grön,
    // Z blå.
    {StockIcon::ViewTop, "ViewTop", "@kub F M 12 3 L 19.8 7.5 L 12 12 L 4.2 7.5 Z",
     "#kub gradient blue F M 12 3 L 19.8 7.5 L 12 12 L 4.2 7.5 Z"},
    {StockIcon::ViewFront, "ViewFront", "@kub F M 4.2 7.5 L 12 12 V 21 L 4.2 16.5 Z",
     "#kub gradient blue F M 4.2 7.5 L 12 12 V 21 L 4.2 16.5 Z"},
    {StockIcon::ViewRight, "ViewRight", "@kub F M 12 12 L 19.8 7.5 V 16.5 L 12 21 Z",
     "#kub gradient blue F M 12 12 L 19.8 7.5 V 16.5 L 12 21 Z"},
    {StockIcon::ViewIsometric, "ViewIsometric", "@kub D 12 12 2.2",
     "#kub over blue D 12 12 2.2"},
    {StockIcon::Perspective, "Perspective",
     "M 3 20 L 8 5 H 16 L 21 20 Z M 12 5 V 20 M 5.5 12.5 H 18.5",
     "all deep blue gradient light blue F M 3 20 L 8 5 H 16 L 21 20 Z"},
    {StockIcon::Orthographic, "Orthographic",
     "M 3 19 L 8 5 H 21 L 16 19 Z M 14.5 5 L 9.5 19 M 5.5 12 H 18.5",
     "all deep blue gradient light blue "
     "F M 3 19 L 8 5 H 21 L 16 19 Z"},
    {StockIcon::Orbit, "Orbit",
     "D 12 12 2.5 U 12 12 10 5 30 330 M 19.8 6.1 L 20.7 9.5 L 17.2 9.1",
     "over blue U 12 12 10 5 30 330 "
     "M 19.8 6.1 L 20.7 9.5 L 17.2 9.1"},
    {StockIcon::Camera, "Camera",
     "R 2 6 20 15 2 C 12 13.5 4 M 8 6 L 9.5 3 H 14.5 L 16 6",
     "all deep paper gradient dark paper B 2 6 20 15 2 "
     "F M 8 6 L 9.5 3 H 14.5 L 16 6 Z gradient blue D 12 13.5 4"},
    {StockIcon::Light, "Light",
     "C 12 12 4 M 12 2 V 4 M 12 20 V 22 M 2 12 H 4 M 20 12 H 22 "
     "M 4.9 4.9 L 6.3 6.3 M 17.7 17.7 L 19.1 19.1 M 4.9 19.1 L 6.3 17.7 "
     "M 17.7 6.3 L 19.1 4.9",
     "all deep amber gradient yellow D 12 12 4"},
    {StockIcon::Axes, "Axes",
     "M 12 14 V 4 M 12 14 L 4 18.5 M 12 14 L 20 18.5 D 12 4 2 D 4 18.5 2 "
     "D 20 18.5 2",
     "over red M 12 14 L 4 18.5 D 4 18.5 2 "
     "green M 12 14 L 20 18.5 D 20 18.5 2 "
     "blue M 12 14 V 4 D 12 4 2"},
    {StockIcon::ZoomWindow, "ZoomWindow",
     "M 2 6 V 2 H 6 M 10 2 H 14 V 6 M 2 10 V 14 H 6 | C 15.5 15.5 4.5 M 22 22 L 18.7 18.7",
     "all blue | all deep blue gradient light blue D 15.5 15.5 4.5"},
    // ---------------------------------------------------------- 3D — former
    // Kroppen i blått, ljuset uppifrån — överytan ljusast, högersidan mörkast.
    {StockIcon::Box, "Box", "@kub",
     "all deep blue gradient blue "
     "F M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z "
     "light blue F M 12 3 L 19.8 7.5 L 12 12 L 4.2 7.5 Z "
     "dark blue F M 12 12 L 19.8 7.5 V 16.5 L 12 21 Z"},
    {StockIcon::Sphere, "Sphere",
     "C 12 12 9 U 12 12 9 3.5 0 180 U 12 12 3.5 9 270 450",
     "all deep blue gradient blue D 12 12 9"},
    {StockIcon::Cylinder, "Cylinder",
     "E 12 5 7 2.5 M 5 5 V 19 M 19 5 V 19 U 12 19 7 2.5 0 180",
     "all deep blue gradient blue "
     "F M 5 5 V 19 Q 12 24 19 19 V 5 Q 12 0 5 5 Z "
     "light blue F M 5 5 Q 12 0 19 5 Q 12 10 5 5 Z"},
    {StockIcon::Cone, "Cone", "M 12 3 L 4 18 M 12 3 L 20 18 E 12 18 8 3",
     "all deep blue gradient blue F M 12 3 L 4 18 Q 12 24 20 18 Z"},
    {StockIcon::Torus, "Torus",
     "E 12 12 10 6 M 6 10.5 Q 12 15.5 18 10.5 M 8.2 12 Q 12 9 15.8 12", "all blue"},
    {StockIcon::Plane, "Plane",
     "M 2 13 L 12 7 L 22 13 L 12 19 Z M 7 10 L 17 16 M 17 10 L 7 16",
     "all deep blue gradient light blue F M 2 13 L 12 7 L 22 13 L 12 19 Z"},
    {StockIcon::Mesh, "Mesh",
     "R 3 5 18 14 0 M 9 5 V 19 M 15 5 V 19 M 3 12 H 21 M 3 5 L 15 19 "
     "M 9 5 L 21 19 M 15 5 L 21 12 M 3 12 L 9 19",
     "all blue"},
    // ------------------------------------------ 3D — visning och modellering
    // Visningslägena är klotet: tråd, fyllt och renderat med en glans. De
    // booleska är två cirklar; det som tas bort står streckat. Det markerade
    // och resultatet blått, det som tas bort rött.
    {StockIcon::Wireframe, "Wireframe", "C 12 12 9 E 12 12 9 3.5 E 12 12 3.5 9", "all blue"},
    {StockIcon::Solid, "Solid", "C 12 12 9", "all deep blue gradient blue D 12 12 9"},
    {StockIcon::Rendered, "Rendered", "C 12 12 9 | A 12 12 5.5 195 255",
     "all deep blue gradient blue D 12 12 9 | all paper"},
    {StockIcon::SelectVertex, "SelectVertex",
     "@yta D 3 18 2 D 7 6 2 D 21 6 2 D 17 18 2",
     "over blue D 3 18 2 D 7 6 2 D 21 6 2 D 17 18 2"},
    {StockIcon::SelectEdge, "SelectEdge", "@yta D 7 6 2 D 21 6 2",
     "over blue M 7 6 H 21 D 7 6 2 D 21 6 2"},
    {StockIcon::SelectFace, "SelectFace", "@yta F @yta", "over blue F @yta"},
    {StockIcon::Extrude, "Extrude",
     "M 2 17 L 12 12 L 22 17 L 12 22 Z M 12 17 V 3 M 8.5 6.5 L 12 3 L 15.5 6.5",
     "all deep blue gradient blue F M 2 17 L 12 12 L 22 17 L 12 22 Z "
     "over green M 12 17 V 3 M 8.5 6.5 L 12 3 L 15.5 6.5"},
    {StockIcon::Union, "Union", "A 8.5 12 6.5 57.4 302.6 A 15.5 12 6.5 237.4 482.6",
     "all deep blue gradient blue D 8.5 12 6.5 D 15.5 12 6.5"},
    {StockIcon::Subtract, "Subtract",
     "A 8.5 12 6.5 57.4 302.6 A 15.5 12 6.5 122.6 237.4 "
     "A 15.5 12 6.5 266.4 291.4 A 15.5 12 6.5 320.5 345.5 "
     "A 15.5 12 6.5 374.5 399.5 A 15.5 12 6.5 428.6 453.6",
     "over blue A 8.5 12 6.5 57.4 302.6 "
     "A 15.5 12 6.5 122.6 237.4 "
     "red A 15.5 12 6.5 266.4 291.4 A 15.5 12 6.5 320.5 345.5 "
     "A 15.5 12 6.5 374.5 399.5 A 15.5 12 6.5 428.6 453.6"},
    {StockIcon::Intersect, "Intersect",
     "A 8.5 12 6.5 302.6 417.4 A 15.5 12 6.5 122.6 237.4 "
     "A 8.5 12 6.5 86.4 111.4 A 8.5 12 6.5 140.5 165.5 "
     "A 8.5 12 6.5 194.5 219.5 A 8.5 12 6.5 248.6 273.6 "
     "A 15.5 12 6.5 266.4 291.4 A 15.5 12 6.5 320.5 345.5 "
     "A 15.5 12 6.5 374.5 399.5 A 15.5 12 6.5 428.6 453.6",
     "over blue A 8.5 12 6.5 302.6 417.4 "
     "A 15.5 12 6.5 122.6 237.4"},
    // -------------------------------------------------------------- Bild
    {StockIcon::Image, "Image",
     "R 3 4 18 16 2 M 3 17 L 9 11 L 14 16 M 12.5 14.5 L 15 12 L 21 18 C 15.5 "
     "8.5 1.8",
     "all deep blue gradient light blue B 3 4 18 16 2 "
     "over green M 3 17 L 9 11 L 14 16 M 12.5 14.5 L 15 12 L 21 18 yellow D "
     "15.5 8.5 1.8"},
    {StockIcon::Crop, "Crop", "M 6 2 V 18 H 22 M 2 6 H 18 V 22", "all blue"},
    {StockIcon::Resize, "Resize",
     "R 2 2 20 20 2 M 7 17 L 17 7 M 12 7 H 17 V 12 M 7 12 V 17 H 12",
     "all deep paper paper B 2 2 20 20 2 "
     "over blue M 7 17 L 17 7 M 12 7 H 17 V 12 M 7 12 V 17 H 12"},
    // Solen, och den mörka halvan i en fast ton, så att den är mörk i båda
    // lägena.
    {StockIcon::Brightness, "Brightness",
     "C 12 12 4.5 M 12 2 "
     "V 4.5 M 12 19.5 V 22 M 2 12 H 4.5 M 19.5 12 H 22 M 4.9 4.9 L 6.7 6.7 M "
     "17.3 17.3 L 19.1 19.1 M 4.9 19.1 L 6.7 17.3 M 17.3 6.7 L 19.1 4.9 | "
     "F M 12 7.5 Q 16.5 7.5 16.5 12 Q 16.5 16.5 12 16.5 Z",
     "all deep amber gradient yellow D 12 12 4.5 | all deep amber"},
    // Skivan, och den mörka halvan innanför dess kant i en fast ton.
    {StockIcon::Contrast, "Contrast", "C 12 12 9 | F M 12 4.5 Q 4.5 4.5 4.5 12 Q 4.5 19.5 12 19.5 Z",
     "all deep paper paper D 12 12 9 | all deep blue"},
    // ---------------------------------------------- Dockning och fönster
    {StockIcon::PanelLeft, "PanelLeft", "R 3 3 18 18 2 M 9 3 V 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 3 3 6 18 2"},
    {StockIcon::PanelRight, "PanelRight", "R 3 3 18 18 2 M 15 3 V 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 15 3 6 18 2"},
    {StockIcon::PanelBottom, "PanelBottom", "R 3 3 18 18 2 M 3 15 H 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 3 15 18 6 2"},
    // Det dockade fönstret bakom, och det flytande framför.
    {StockIcon::Float, "Float", "M 6 9 H 3 V 21 H 15 V 16 | R 9 3 12 10 1.5 M 9 6.5 H 21",
     "all deep paper paper F M 3 9 H 15 V 21 H 3 Z contrast 2 | "
     "all deep blue paper B 9 3 12 10 1.5 gradient blue "
     "F M 9 3 H 21 V 6.5 H 9 Z"},
    {StockIcon::Tabs, "Tabs", "M 3 21 V 3 H 12 V 8 H 21 V 21 Z M 14 8 V 3 H 21 V 8",
     "all deep blue paper F M 3 21 V 8 H 21 V 21 Z "
     "gradient blue F M 3 3 H 12 V 8 H 3 Z "
     "gradient light blue F M 14 3 H 21 V 8 H 14 Z"},
    {StockIcon::SplitHorizontal, "SplitHorizontal",
     "R 3 3 18 18 2 M 3 12 H 21",
     "all deep blue paper B 3 3 18 18 2 gradient blue B 3 12 18 9 2"},
    // ---------------------------------------------------- Kod och system
    {StockIcon::Terminal, "Terminal", "R 2 4 20 16 2 | M 6 9 L 9 12 L 6 15 M 11 15 H 17",
     "all deep paper gradient dark paper B 2 4 20 16 2 | all green"},
    {StockIcon::Code, "Code",
     "M 8 6 L 2 12 L 8 18 M 16 6 L 22 12 L 16 18 M 14 4 L 10 20", "all blue"},
    {StockIcon::Bug, "Bug",
     "E 12 15 5 5.5 A 12 9.5 3 180 360 M 12 11 V 20 M 10 6.8 L 8 4 M 14 6.8 "
     "L 16 4 M 7.4 12 L 4 10 M 16.6 12 L 20 10 M 7 15.5 H 3.5 M 17 15.5 H "
     "20.5 M 7.6 18.6 L 4.5 21 M 16.4 18.6 L 19.5 21",
     "all deep red gradient red F M 12 9.5 Q 17 9.5 17 15 Q 17 20.5 12 20.5 "
     "Q 7 20.5 7 15 Q 7 9.5 12 9.5 Z"},
    {StockIcon::Package, "Package",
     "@kub M 8.1 5.25 L 15.9 9.75 V 18.75",
     "all deep amber gradient amber "
     "F M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z "
     "light amber F M 12 3 L 19.8 7.5 L 12 12 L 4.2 7.5 Z "
     "dark amber F M 12 12 L 19.8 7.5 V 16.5 L 12 21 Z"},
    // --------------------------------------------------- Karta och plats
    {StockIcon::MapPin, "MapPin",
     "M 12 22 Q 5 14.5 5 9.5 A 12 9.5 7 180 360 M 19 9.5 Q 19 14.5 12 22 | D 12 9.5 2.5",
     "all deep red gradient red F M 12 22 Q 5 14.5 5 9.5 Q 5 2.5 12 2.5 "
     "Q 19 2.5 19 9.5 Q 19 14.5 12 22 Z | all paper"},
    {StockIcon::MyLocation, "MyLocation",
     "C 12 12 7 M 12 2 V 5 M 12 19 V 22 M 2 12 H 5 M 19 12 H 22 | C 12 12 3",
     "all blue | all deep blue gradient blue D 12 12 3"},
    // Skivan, och nålens två halvor som egna lager.
    {StockIcon::Compass, "Compass",
     "C 12 12 10 | M 12 4.5 L 15 12 H 9 Z | M 9 12 L 12 19.5 L 15 12 Z",
     "all deep paper gradient paper D 12 12 10 | "
     "all deep red gradient red F M 12 4.5 L 15 12 H 9 Z | "
     "all deep paper gradient dark paper F M 9 12 L 12 19.5 L 15 12 Z"},
    {StockIcon::Globe, "Globe",
     "C 12 12 10 E 12 12 4.5 10 M 2 12 H 22 M 3.3 7 H 20.7 M 3.3 17 H 20.7",
     "all deep blue gradient blue D 12 12 10"},
    // Vägen, och ändarna som egna lager.
    {StockIcon::Route, "Route", "M 5.5 16 V 12 H 18.5 V 8 | C 5.5 18.5 2.5 | C 18.5 5.5 2.5",
     "all blue | all deep green gradient green D 5.5 18.5 2.5 | "
     "all deep red gradient red D 18.5 5.5 2.5"},
    {StockIcon::Terrain, "Terrain",
     "M 2 20 L 8.5 8 L 13 15.5 L 16 11.5 L 22 20 Z M 6.6 11.5 L 8.5 13 L "
     "10.6 11.5",
     "all deep green gradient green "
     "F M 2 20 L 8.5 8 L 13 15.5 L 16 11.5 L 22 20 Z"},
    // ------------------------ Ordna objekt — det fyllda är det markerade
    // Ordna: de bakre i ljus ton, det markerade framför i full färg — varje
    // ruta ett eget lager.
    {StockIcon::BringToFront, "BringToFront", "R 2 2 10 10 1 | R 12 12 10 10 1 | R 7 7 10 10 1",
     "all deep blue gradient light blue B 2 2 10 10 1 contrast 2 | "
     "all deep blue gradient light blue B 12 12 10 10 1 contrast 2 | "
     "all deep blue gradient blue B 7 7 10 10 1"},
    {StockIcon::SendToBack, "SendToBack", "R 2 2 10 10 1 | R 7 7 10 10 1 | R 12 12 10 10 1",
     "all deep blue gradient blue B 2 2 10 10 1 | "
     "all deep blue gradient light blue B 7 7 10 10 1 contrast 2 | "
     "all deep blue gradient light blue B 12 12 10 10 1 contrast 2"},
    {StockIcon::BringForward, "BringForward", "R 3 3 12 12 1 | R 9 9 12 12 1",
     "all deep blue gradient light blue B 3 3 12 12 1 contrast 2 | "
     "all deep blue gradient blue B 9 9 12 12 1"},
    {StockIcon::SendBackward, "SendBackward", "R 3 3 12 12 1 | R 9 9 12 12 1",
     "all deep blue gradient blue B 3 3 12 12 1 | "
     "all deep blue gradient light blue B 9 9 12 12 1 contrast 2"},
    {StockIcon::Ungroup, "Ungroup",
     "M 17 3 H 21 V 7 M 3 17 V 21 H 7 | R 3 3 9 7 1 | R 12 14 9 7 1",
     "all blue | all deep blue gradient blue B 3 3 9 7 1 | "
     "all deep blue gradient light blue B 12 14 9 7 1"},
    {StockIcon::AlignObjectsLeft, "AlignObjectsLeft",
     "M 3 2 V 22 | R 6 5 12 5 1 | R 6 14 7 5 1",
     "all deep paper | all deep blue gradient blue B 6 5 12 5 1 | "
     "all deep blue gradient light blue B 6 14 7 5 1"},
    {StockIcon::AlignObjectsCenter, "AlignObjectsCenter",
     "M 12 2 V 22 | R 5 5 14 5 1 | R 8 14 8 5 1",
     "all deep paper | all deep blue gradient blue B 5 5 14 5 1 | "
     "all deep blue gradient light blue B 8 14 8 5 1"},
    {StockIcon::AlignObjectsRight, "AlignObjectsRight",
     "M 21 2 V 22 | R 6 5 12 5 1 | R 11 14 7 5 1",
     "all deep paper | all deep blue gradient blue B 6 5 12 5 1 | "
     "all deep blue gradient light blue B 11 14 7 5 1"},
    {StockIcon::AlignObjectsTop, "AlignObjectsTop",
     "M 2 3 H 22 | R 5 6 5 12 1 | R 14 6 5 7 1",
     "all deep paper | all deep blue gradient blue B 5 6 5 12 1 | "
     "all deep blue gradient light blue B 14 6 5 7 1"},
    {StockIcon::AlignObjectsMiddle, "AlignObjectsMiddle",
     "M 2 12 H 22 | R 5 5 5 14 1 | R 14 8 5 8 1",
     "all deep paper | all deep blue gradient blue B 5 5 5 14 1 | "
     "all deep blue gradient light blue B 14 8 5 8 1"},
    {StockIcon::AlignObjectsBottom, "AlignObjectsBottom",
     "M 2 21 H 22 | R 5 6 5 12 1 | R 14 11 5 7 1",
     "all deep paper | all deep blue gradient blue B 5 6 5 12 1 | "
     "all deep blue gradient light blue B 14 11 5 7 1"},
    {StockIcon::DistributeHorizontal, "DistributeHorizontal",
     "M 3 2 V 22 M 21 2 V 22 | R 9 6 6 12 1",
     "all deep paper | all deep blue gradient blue B 9 6 6 12 1"},
    {StockIcon::DistributeVertical, "DistributeVertical",
     "M 2 3 H 22 M 2 21 H 22 | R 6 9 12 6 1",
     "all deep paper | all deep blue gradient blue B 6 9 12 6 1"},
    // --- 3D — fler vyer och verktyg (de dolda sidornas kanter streckade)
    {StockIcon::ViewBottom, "ViewBottom",
     "@kub M 10.44 12.9 L 9.4 13.5 M 8.19 14.2 L 7.15 14.8 M 5.93 15.5 L "
     "4.89 16.1 M 13.56 12.9 L 14.6 13.5 M 15.81 14.2 L 16.85 14.8 M 18.07 "
     "15.5 L 19.11 16.1",
     "#kub gradient blue F M 12 12 L 19.8 16.5 L 12 21 L 4.2 16.5 Z"},
    {StockIcon::ViewLeft, "ViewLeft",
     "@kub M 10.44 12.9 L 9.4 13.5 M 8.19 14.2 L 7.15 14.8 M 5.93 15.5 L "
     "4.89 16.1 M 12 10.2 V 9 M 12 7.6 V 6.4 M 12 5 V 3.8",
     "#kub gradient blue F M 12 3 L 4.2 7.5 V 16.5 L 12 12 Z"},
    {StockIcon::ViewBack, "ViewBack",
     "@kub M 13.56 12.9 L 14.6 13.5 M 15.81 14.2 L 16.85 14.8 M 18.07 15.5 L "
     "19.11 16.1 M 12 10.2 V 9 M 12 7.6 V 6.4 M 12 5 V 3.8",
     "#kub gradient blue F M 12 3 L 19.8 7.5 V 16.5 L 12 12 Z"},
    {StockIcon::Walk, "Walk",
     "F M 8.5 3.8 Q 11 3.8 11 7 Q 11 10.2 8.5 10.2 Q 6 10.2 6 7 Q 6 3.8 8.5 "
     "3.8 Z D 8.5 13 1.7 F M 15.5 9.8 Q 18 9.8 18 13 Q 18 16.2 15.5 16.2 Q "
     "13 16.2 13 13 Q 13 9.8 15.5 9.8 Z D 15.5 19 1.7",
     "all blue"},
    {StockIcon::Section, "Section",
     "M 12 7.5 L 19.8 12 L 12 16.5 L 4.2 12 Z M 4.2 12 V 16.5 L 12 21 L 19.8 "
     "16.5 V 12 M 12 16.5 V 21 M 9.4 9 L 17.2 13.5 M 6.8 10.5 L 14.6 15",
     "all deep blue gradient light blue "
     "F M 4.2 12 L 12 7.5 L 19.8 12 V 16.5 L 12 21 L 4.2 16.5 Z "
     "gradient blue F M 12 7.5 L 19.8 12 L 12 16.5 L 4.2 12 Z"},
    // -------------------------------------------------------- CAD — fler
    {StockIcon::Explode, "Explode",
     "M 3 8 V 3 H 8 M 16 3 H 21 V 8 M 21 16 V 21 H 16 M 8 21 H 3 V 16 M 10 "
     "10 L 8 8 M 14 10 L 16 8 M 10 14 L 8 16 M 14 14 L 16 16 D 12 12 1.3",
     "all blue over amber M 10 10 L 8 8 M 14 10 L 16 8 M 10 14 L 8 16 "
     "M 14 14 L 16 16 D 12 12 1.3"},
    // Blocket, och insättningen som ett eget lager.
    {StockIcon::Block, "Block",
     "R 9 3 12 12 2 C 15 9 2.5 | M 3 21 L 10.5 13.5 M 5.5 13.5 H 10.5 V 18.5",
     "all deep blue gradient blue B 9 3 12 12 2 paper D 15 9 2.5 | all green"},
    {StockIcon::Note, "Note",
     "M 3 3 H 21 V 15 L 15 21 H 3 Z M 21 15 H 15 V 21 | M 7 8 H 17 M 7 12 H 13",
     "all deep amber gradient yellow F M 3 3 H 21 V 15 L 15 21 H 3 Z "
     "dark yellow F M 21 15 H 15 V 21 Z | all deep amber"},
    {StockIcon::RevisionCloud, "RevisionCloud",
     "A 7 6 2.5 180 360 A 12 6 2.5 180 360 A 17 6 2.5 180 360 A 19.5 9 3 270 "
     "450 A 19.5 15 3 270 450 A 17 18 2.5 0 180 A 12 18 2.5 0 180 A 7 18 2.5 "
     "0 180 A 4.5 15 3 90 270 A 4.5 9 3 90 270",
     "all red"},
    {StockIcon::Area, "Area",
     "M 3 19 L 5 6 L 14 3 L 21 10 L 17 21 Z M 4.45 9.55 L 9.5 4.5 M 3.2 17.8 "
     "L 16 5 M 8.25 19.75 L 19.5 8.5 M 14.4 20.6 L 18.7 16.3",
     "all deep blue gradient light blue "
     "F M 3 19 L 5 6 L 14 3 L 21 10 L 17 21 Z"},
    // ------------------------------------------------------- Text — fler
    {StockIcon::Strikethrough, "Strikethrough",
     "M 17 6.5 Q 16 4 12 4 Q 7 4 7 7.5 Q 7 10 12 11 M 12 13 Q 17 14 17 16.5 "
     "Q 17 20 12 20 Q 8 20 7 17.5 M 4 12 H 20",
     "all deep blue"},
    {StockIcon::Indent, "Indent",
     "M 3 4 H 21 M 11 9.5 H 21 M 11 14.5 H 21 M 3 20 H 21 M 3 9 L 6.5 12 L 3 "
     "15",
     "all blue"},
    {StockIcon::Outdent, "Outdent",
     "M 3 4 H 21 M 11 9.5 H 21 M 11 14.5 H 21 M 3 20 H 21 M 6.5 9 L 3 12 L "
     "6.5 15",
     "all blue"},
    {StockIcon::Heading, "Heading",
     "M 4 4 V 20 M 14 4 V 20 M 4 12 H 14 M 17.5 11 L 20 9 V 20", "all deep blue"},
    {StockIcon::Quote, "Quote",
     "B 3.5 10 6.5 7 1 M 4.25 11 Q 4.25 6 9 5 B 13.5 10 6.5 7 1 M 14.25 11 Q "
     "14.25 6 19 5",
     "all blue"},
    // Rutan, och tecknen som ett eget lager.
    {StockIcon::CodeBlock, "CodeBlock",
     "R 2 4 20 16 2 | M 9 9 L 6 12 L 9 15 M 15 9 L 18 12 L 15 15",
     "all deep paper gradient dark paper B 2 4 20 16 2 | all blue"},
    // ----------------------------------------------------------- Diagram
    {StockIcon::LineChart, "LineChart", "M 3 3 V 21 H 21 | M 7 16 L 11 11 L 14 14 L 20 6",
     "all deep paper | all blue"},
    {StockIcon::PieChart, "PieChart", "C 12 12 9 M 12 3 V 12 L 19.8 16.5",
     "all deep blue gradient light blue D 12 12 9 "
     "gradient blue F M 12 12 V 3 Q 17.2 3 19.8 7.5 Q 22.4 12 19.8 16.5 Z"},
    {StockIcon::Sum, "Sum",
     "M 18 4 H 6 L 12.5 12 L 6 20 H 18", "all blue"},
    // --------------------------------------------------- Status — i ring
    // Skivan i färg, och tecknet vitt ovanpå som ett eget lager.
    {StockIcon::AddCircle, "AddCircle", "C 12 12 10 | M 12 7 V 17 M 7 12 H 17",
     "all deep green gradient green D 12 12 10 | all paper"},
    {StockIcon::RemoveCircle, "RemoveCircle", "C 12 12 10 | M 7 12 H 17",
     "all deep red gradient red D 12 12 10 | all paper"},
    {StockIcon::CheckCircle, "CheckCircle", "C 12 12 10 | M 16.5 8.5 L 10.5 15 L 7.5 12",
     "all deep green gradient green D 12 12 10 | all paper"},
    {StockIcon::Blocked, "Blocked",
     "C 12 12 10 M 4.9 4.9 L 19.1 19.1", "all red"},
    {StockIcon::Online, "Online", "C 12 12 6", "all deep green gradient green D 12 12 6"},
    {StockIcon::Offline, "Offline",
     "C 12 12 6", "all deep paper"},
    // ---------------------------------------------------------- Hårdvara
    {StockIcon::Laptop, "Laptop",
     "R 4 4 16 11 1.5 M 2 19 H 22", "all deep blue gradient blue B 4 4 16 11 1.5"},
    {StockIcon::Phone, "Phone",
     "R 6 2 12 20 2.5 M 10.5 18 H 13.5", "all deep paper gradient dark paper B 6 2 12 20 2.5"},
    // Plattan, och tangenterna som ett eget lager.
    {StockIcon::Keyboard, "Keyboard",
     "R 2 6 20 12 2 | D 6 10 0.9 D 10 10 0.9 D 14 10 0.9 D 18 10 0.9 M 7 14.5 H 17",
     "all deep paper gradient paper B 2 6 20 12 2 | all deep paper"},
    {StockIcon::Mouse, "Mouse",
     "R 6 2 12 20 6 M 12 6 V 10", "all deep paper gradient paper B 6 2 12 20 6"},
    {StockIcon::Cpu, "Cpu",
     "R 6 6 12 12 1.5 R 9.5 9.5 5 5 0.5 M 9 2 V 6 M 15 2 V 6 M 9 18 V 22 M "
     "15 18 V 22 M 2 9 H 6 M 2 15 H 6 M 18 9 H 22 M 18 15 H 22 | R 9.5 9.5 5 5 0.5",
     "all deep paper gradient dark paper B 6 6 12 12 1.5 | "
     "all deep blue gradient blue B 9.5 9.5 5 5 0.5"},
    // Batteriet, och laddningen som ett eget lager.
    {StockIcon::Battery, "Battery", "R 2 7 17 10 2 M 22 10.5 V 13.5 | B 5 10 7 4 0.5",
     "all deep paper paper B 2 7 17 10 2 | all green"},
    {StockIcon::Power, "Power",
     "A 12 13 8 300 600 M 12 3 V 12", "all blue"},
    {StockIcon::Wifi, "Wifi",
     "D 12 19 1.3 A 12 19 4.5 225 315 A 12 19 9 225 315 A 12 19 13.5 225 315",
     "all blue"},
    {StockIcon::Usb, "Usb",
     "M 12 3 V 18.5 M 9.5 5.5 L 12 3 L 14.5 5.5 M 12 15 L 7 12 V 9.6 M 12 "
     "12.5 L 17 9.5 V 7.6 C 7 8.3 1.3 R 15.7 5 2.6 2.6 0.3 D 12 19.5 2",
     "all blue"},
    // --------------------------------------------------------------- Tid
    {StockIcon::Timer, "Timer",
     "M 6 3 H 18 M 6 21 H 18 M 7 3 V 6 Q 7 9 12 12 Q 17 15 17 18 V 21 M 17 3 "
     "V 6 Q 17 9 12 12 Q 7 15 7 18 V 21",
     "all deep amber over amber F M 9 19 Q 12 16 15 19 V 21 H 9 Z"},
    {StockIcon::Stopwatch, "Stopwatch",
     "C 12 13.5 8 M 10 2 H 14 M 12 2 V 5.5 M 12 13.5 V 9 M 18.5 6 L 20 4.5",
     "all deep paper gradient paper D 12 13.5 8 over red M 12 13.5 V 9"},
    // --------------------------------------------------- Diagonala pilar
    {StockIcon::ArrowUpLeft, "ArrowUpLeft",
     "M 17 17 L 7 7 M 16 7 H 7 V 16", "all blue"},
    {StockIcon::ArrowUpRight, "ArrowUpRight",
     "M 7 17 L 17 7 M 8 7 H 17 V 16", "all blue"},
    {StockIcon::ArrowDownLeft, "ArrowDownLeft",
     "M 17 7 L 7 17 M 7 8 V 17 H 16", "all blue"},
    {StockIcon::ArrowDownRight, "ArrowDownRight",
     "M 7 7 L 17 17 M 17 8 V 17 H 8", "all blue"},
    // ------------------- Filtyper — kategorier (sidan med symbolen stor)
    // Sidan, och symbolen som ett eget lager.
    {StockIcon::FileImage, "FileImage",
     "@fil | M 7 19 L 10.5 14 L 13 17 L 14.5 15.5 L 17 19 Z C 9.5 11 1.4",
     "#fil | all deep green green "
     "F M 7 19 L 10.5 14 L 13 17 L 14.5 15.5 L 17 19 Z yellow D 9.5 11 1.4"},
    {StockIcon::FileVideo, "FileVideo",
     "@fil | F M 10 12 L 15.5 15.5 L 10 19 Z", "#fil | all deep red"},
    {StockIcon::FileAudio, "FileAudio",
     "@fil | M 10.5 18.5 V 12 L 15.5 11 V 17.5 D 9.2 18.5 1.6 D 14.2 17.5 1.6",
     "#fil | all deep blue"},
    {StockIcon::FileArchive, "FileArchive",
     "@fil | M 11 5 V 6 M 12.8 7.5 V 8.5 M 11 10 V 11 M 12.8 12.5 V 13.5 R 10 "
     "15.5 4 3.5 0.8",
     "#fil | all ink paper amber B 10 15.5 4 3.5 0.8"},
    {StockIcon::FileCode, "FileCode",
     "@fil | M 10 12 L 7.5 15.5 L 10 19 M 14 12 L 16.5 15.5 L 14 19", "#fil | all blue"},
    {StockIcon::FileSpreadsheet, "FileSpreadsheet",
     "@fil | R 7.5 11 9 8 0.8 M 7.5 15 H 16.5 M 12 11 V 19",
     "#fil | all deep green gradient green B 7.5 11 9 8 0.8"},
    {StockIcon::FilePresentation, "FilePresentation",
     "@fil | R 7.5 11 9 6 0.8 M 12 17 V 19.5",
     "#fil | all deep amber gradient amber B 7.5 11 9 6 0.8"},
    {StockIcon::FileGeo, "FileGeo",
     "@fil | C 12 13.5 2.8 M 9.8 15.2 L 12 19 L 14.2 15.2", "#fil | all green"},
    {StockIcon::FileCad, "FileCad",
     "@fil | M 8 19 V 11 L 16 19 Z M 8 13.5 H 9.5 M 8 16 H 9.5",
     "#fil | all deep red gradient red F M 8 19 V 11 L 16 19 Z"},
    {StockIcon::FileModel, "FileModel",
     "@fil | M 12 11 L 15.5 13 V 17 L 12 19 L 8.5 17 V 13 Z M 8.5 13 L 12 15 L "
     "15.5 13 M 12 15 V 19",
     "#fil | all deep amber gradient amber "
     "F M 12 11 L 15.5 13 V 17 L 12 19 L 8.5 17 V 13 Z"},
    {StockIcon::FilePointCloud, "FilePointCloud",
     "@fil | D 8.5 12 0.9 D 12 11 0.9 D 15.5 12.5 0.9 D 10 15 0.9 D 13.8 15.5 "
     "0.9 D 8 18 0.9 D 11.5 19 0.9 D 15.5 18.5 0.9",
     "#fil | all amber"},
    {StockIcon::FileDatabase, "FileDatabase",
     "@fil | E 12 11.8 4 1.5 M 8 11.8 V 17.5 M 16 11.8 V 17.5 U 12 17.5 4 1.5 "
     "0 180",
     "#fil | all deep blue gradient blue "
     "F M 8 11.8 V 17.5 Q 12 20.5 16 17.5 V 11.8 Q 12 8.8 8 11.8 Z"},
    // --------------------------------- Filtyper — geodata (grön etikett)
    // Sidan, och symbolen med etiketten — ändelsen utstansad — som ett eget
    // lager.
    {StockIcon::FileShp, "FileShp",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 SHP",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileGeoJson, "FileGeoJson",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 GEOJSON",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileKml, "FileKml",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 KML",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileKmz, "FileKmz",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 KMZ",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileGpx, "FileGpx",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 GPX",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileGpkg, "FileGpkg",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 GPKG",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileGml, "FileGml",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 GML",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileGdb, "FileGdb",
     "@fil | @fdb B 4 12 16 8 1.5 K 4.5 13.5 15 5 GDB",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileOsm, "FileOsm",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 OSM",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileGeoTiff, "FileGeoTiff",
     "@fil | @fglob B 4 12 16 8 1.5 K 4.5 13.5 15 5 GEOTIFF",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileMbTiles, "FileMbTiles",
     "@fil | @fglob B 4 12 16 8 1.5 K 4.5 13.5 15 5 MBTILES",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileTab, "FileTab",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 TAB",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileQgz, "FileQgz",
     "@fil | @fpin B 4 12 16 8 1.5 K 4.5 13.5 15 5 QGZ",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    // ------------------------------ Filtyper — CAD och BIM (röd etikett)
    {StockIcon::FileDwg, "FileDwg",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 DWG",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileDxf, "FileDxf",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 DXF",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileDgn, "FileDgn",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 DGN",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileIfc, "FileIfc",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 IFC",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileStep, "FileStep",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 STEP",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileIges, "FileIges",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 IGES",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileRvt, "FileRvt",
     "@fil | @fvinkel B 4 12 16 8 1.5 K 4.5 13.5 15 5 RVT",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    // --------------------------------- Filtyper — 3D-modeller (bärnsten)
    {StockIcon::FileObj, "FileObj",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 OBJ",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileFbx, "FileFbx",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 FBX",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileGltf, "FileGltf",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 GLTF",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileGlb, "FileGlb",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 GLB",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileStl, "FileStl",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 STL",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::File3mf, "File3mf",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 3MF",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileDae, "FileDae",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 DAE",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileSkp, "FileSkp",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 SKP",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileUsd, "FileUsd",
     "@fil | @fkub B 4 12 16 8 1.5 K 4.5 13.5 15 5 USD",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    // ----------------------------------- Filtyper — punktmoln (bärnsten)
    {StockIcon::FileLas, "FileLas",
     "@fil | @fmoln B 4 12 16 8 1.5 K 4.5 13.5 15 5 LAS",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileLaz, "FileLaz",
     "@fil | @fmoln B 4 12 16 8 1.5 K 4.5 13.5 15 5 LAZ",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileE57, "FileE57",
     "@fil | @fmoln B 4 12 16 8 1.5 K 4.5 13.5 15 5 E57",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FilePly, "FilePly",
     "@fil | @fmoln B 4 12 16 8 1.5 K 4.5 13.5 15 5 PLY",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileXyz, "FileXyz",
     "@fil | @fmoln B 4 12 16 8 1.5 K 4.5 13.5 15 5 XYZ",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    // ------------------------------------------- Filtyper — bilder (gul)
    {StockIcon::FilePng, "FilePng",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 PNG",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileJpg, "FileJpg",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 JPG",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileGif, "FileGif",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 GIF",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileBmp, "FileBmp",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 BMP",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileTiff, "FileTiff",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 TIFF",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileWebp, "FileWebp",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 WEBP",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileSvg, "FileSvg",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 SVG",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileHeic, "FileHeic",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 HEIC",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileIco, "FileIco",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 ICO",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FilePsd, "FilePsd",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 PSD",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    {StockIcon::FileRaw, "FileRaw",
     "@fil | @fbild B 4 12 16 8 1.5 K 4.5 13.5 15 5 RAW",
     "#fil | all deep yellow over deep yellow B 4 12 16 8 1.5"},
    // --- Filtyper — dokument (text blå, kalkyl grön, presentation bärnsten, PDF röd)
    {StockIcon::FilePdf, "FilePdf",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 PDF",
     "#fil | all deep red over dark red B 4 12 16 8 1.5"},
    {StockIcon::FileDoc, "FileDoc",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 DOC",
     "#fil | all deep blue over dark blue B 4 12 16 8 1.5"},
    {StockIcon::FileDocx, "FileDocx",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 DOCX",
     "#fil | all deep blue over dark blue B 4 12 16 8 1.5"},
    {StockIcon::FileOdt, "FileOdt",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 ODT",
     "#fil | all deep blue over dark blue B 4 12 16 8 1.5"},
    {StockIcon::FileRtf, "FileRtf",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 RTF",
     "#fil | all deep blue over dark blue B 4 12 16 8 1.5"},
    {StockIcon::FileTxt, "FileTxt",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 TXT", "#fil | all ink paper"},
    {StockIcon::FileMd, "FileMd",
     "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 MD", "#fil | all ink paper"},
    {StockIcon::FileXls, "FileXls",
     "@fil | @ftabell B 4 12 16 8 1.5 K 4.5 13.5 15 5 XLS",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileXlsx, "FileXlsx",
     "@fil | @ftabell B 4 12 16 8 1.5 K 4.5 13.5 15 5 XLSX",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileOds, "FileOds",
     "@fil | @ftabell B 4 12 16 8 1.5 K 4.5 13.5 15 5 ODS",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FileCsv, "FileCsv",
     "@fil | @ftabell B 4 12 16 8 1.5 K 4.5 13.5 15 5 CSV",
     "#fil | all deep green over dark green B 4 12 16 8 1.5"},
    {StockIcon::FilePpt, "FilePpt",
     "@fil | @fslide B 4 12 16 8 1.5 K 4.5 13.5 15 5 PPT",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FilePptx, "FilePptx",
     "@fil | @fslide B 4 12 16 8 1.5 K 4.5 13.5 15 5 PPTX",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    {StockIcon::FileOdp, "FileOdp",
     "@fil | @fslide B 4 12 16 8 1.5 K 4.5 13.5 15 5 ODP",
     "#fil | all deep amber over dark amber B 4 12 16 8 1.5"},
    // ------------------- Filtyper — data och webb (neutral: en mörk etikett)
    {StockIcon::FileJson, "FileJson",
     "@fil | @fklammer B 4 12 16 8 1.5 K 4.5 13.5 15 5 JSON", "#fil | all ink paper"},
    {StockIcon::FileYaml, "FileYaml",
     "@fil | @fklammer B 4 12 16 8 1.5 K 4.5 13.5 15 5 YAML", "#fil | all ink paper"},
    {StockIcon::FileXml, "FileXml",
     "@fil | @fkod B 4 12 16 8 1.5 K 4.5 13.5 15 5 XML", "#fil | all ink paper"},
    {StockIcon::FileHtml, "FileHtml",
     "@fil | @fkod B 4 12 16 8 1.5 K 4.5 13.5 15 5 HTML", "#fil | all ink paper"},
    {StockIcon::FileSql, "FileSql",
     "@fil | @fdb B 4 12 16 8 1.5 K 4.5 13.5 15 5 SQL", "#fil | all ink paper"},
    {StockIcon::FileSqlite, "FileSqlite",
     "@fil | @fdb B 4 12 16 8 1.5 K 4.5 13.5 15 5 SQLITE", "#fil | all ink paper"},
    // ---------------------------------------- Filtyper — arkiv (neutral)
    {StockIcon::FileZip, "FileZip",
     "@fil | @fdragkedja B 4 12 16 8 1.5 K 4.5 13.5 15 5 ZIP", "#fil | all ink paper"},
    {StockIcon::File7z, "File7z",
     "@fil | @fdragkedja B 4 12 16 8 1.5 K 4.5 13.5 15 5 7Z", "#fil | all ink paper"},
    {StockIcon::FileRar, "FileRar",
     "@fil | @fdragkedja B 4 12 16 8 1.5 K 4.5 13.5 15 5 RAR", "#fil | all ink paper"},
    {StockIcon::FileTar, "FileTar",
     "@fil | @fdragkedja B 4 12 16 8 1.5 K 4.5 13.5 15 5 TAR", "#fil | all ink paper"},
    {StockIcon::FileGz, "FileGz",
     "@fil | @fdragkedja B 4 12 16 8 1.5 K 4.5 13.5 15 5 GZ", "#fil | all ink paper"},
    // ------------------------------- Filtyper — ljud och video (neutral)
    {StockIcon::FileMp3, "FileMp3",
     "@fil | @fnot B 4 12 16 8 1.5 K 4.5 13.5 15 5 MP3", "#fil | all ink paper"},
    {StockIcon::FileWav, "FileWav",
     "@fil | @fnot B 4 12 16 8 1.5 K 4.5 13.5 15 5 WAV", "#fil | all ink paper"},
    {StockIcon::FileMp4, "FileMp4",
     "@fil | @fspela B 4 12 16 8 1.5 K 4.5 13.5 15 5 MP4", "#fil | all ink paper"},
    {StockIcon::FileMov, "FileMov",
     "@fil | @fspela B 4 12 16 8 1.5 K 4.5 13.5 15 5 MOV", "#fil | all ink paper"},
    // ---------------------------------------------------- Utseendets läge
    {StockIcon::ThemeSystem, "ThemeSystem",
     "R 2 3 20 14 2 M 8 21 H 16 M 12 17 V 21 F M 12 3 H 20 Q 22 3 22 5 V 15 Q 22 "
     "17 20 17 H 12 Z",
     "over blue F M 12 3 H 20 Q 22 3 22 5 V 15 Q 22 17 20 17 H 12 Z"},
    {StockIcon::ThemeLight, "ThemeLight",
     "C 12 12 5 M 12 2 V 4 M 12 20 V 22 M 2 12 H 4 M 20 12 H 22 M 4.9 4.9 L 6.2 "
     "6.2 M 17.8 17.8 L 19.1 19.1 M 4.9 19.1 L 6.2 17.8 M 17.8 6.2 L 19.1 4.9",
     "yellow D 12 12 5"},
    {StockIcon::ThemeDark, "ThemeDark",
     "A 12 12 9 0 270 A 16.5 7.5 6.364 45 225 F M 21 12 Q 21 15.73 18.36 18.36 Q "
     "15.73 21 12 21 Q 8.27 21 5.64 18.36 Q 3 15.73 3 12 Q 3 8.27 5.64 5.64 Q 8.27 "
     "3 12 3 Q 10.14 4.86 10.14 7.5 Q 10.14 10.14 12 12 Q 13.86 13.86 16.5 13.86 Q "
     "19.14 13.86 21 12 Z",
     "over yellow F M 21 12 Q 21 15.73 18.36 18.36 Q 15.73 21 12 21 Q 8.27 21 5.64 "
     "18.36 Q 3 15.73 3 12 Q 3 8.27 5.64 5.64 Q 8.27 3 12 3 Q 10.14 4.86 10.14 7.5 "
     "Q 10.14 10.14 12 12 Q 13.86 13.86 16.5 13.86 Q 19.14 13.86 21 12 Z"},
};

constexpr int kAntal = static_cast<int>(sizeof(kDefs) / sizeof(kDefs[0]));

// Tabellen i uppräkningens ordning och komplett — ett tillägg i enum:en utan
// en rad här, eller en rad på fel plats, kompilerar inte.
constexpr bool iOrdning()
{
    for (int i = 0; i < kAntal; ++i)
        if (static_cast<int>(kDefs[i].icon) != i)
            return false;
    return kAntal == static_cast<int>(StockIcon::ThemeDark) + 1;
}
static_assert(iOrdning(), "kDefs ska ha en rad per StockIcon, i ordning");


// GLYFERNA för den utstansade texten — streck i en ruta på 3 × 5 (rad 0
// överst), samma språk som ikonerna men bara M L H V Q Z. Siffran 0 är O.
struct Glyf {
    char        tecken;
    const char* path;
};

constexpr Glyf kGlyfer[] = {
    {'A', "M 0 5 L 1.5 0 L 3 5 M 0.55 3.4 H 2.45"},
    {'B', "M 0 2.5 H 1.8 Q 2.7 2.5 2.7 1.25 Q 2.7 0 1.8 0 H 0 V 5 H 1.9 "
          "Q 3 5 3 3.75 Q 3 2.5 1.9 2.5"},
    {'C', "M 3 0.8 Q 2.6 0 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 2.6 5 3 4.2"},
    {'D', "M 0 0 V 5 H 1.2 Q 3 5 3 2.5 Q 3 0 1.2 0 Z"},
    {'E', "M 3 0 H 0 V 5 H 3 M 0 2.5 H 2.2"},
    {'F', "M 3 0 H 0 V 5 M 0 2.5 H 2.2"},
    {'G', "M 3 0.8 Q 2.6 0 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 3.2 V 2.7 H 1.7"},
    {'H', "M 0 0 V 5 M 3 0 V 5 M 0 2.5 H 3"},
    {'I', "M 1.5 0 V 5 M 0.5 0 H 2.5 M 0.5 5 H 2.5"},
    {'J', "M 3 0 V 3.5 Q 3 5 1.5 5 Q 0 5 0 3.6"},
    {'K', "M 0 0 V 5 M 3 0 L 0 3.2 M 1.1 2 L 3 5"},
    {'L', "M 0 0 V 5 H 3"},
    {'M', "M 0 5 V 0 L 1.5 2.8 L 3 0 V 5"},
    {'N', "M 0 5 V 0 L 3 5 V 0"},
    {'O', "M 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 2.5 Q 3 0 1.5 0 Z"},
    {'P', "M 0 5 V 0 H 1.8 Q 3 0 3 1.4 Q 3 2.8 1.8 2.8 H 0"},
    {'Q', "M 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 2.5 Q 3 0 1.5 0 Z "
          "M 1.9 3.7 L 3 5"},
    {'R', "M 0 5 V 0 H 1.8 Q 3 0 3 1.4 Q 3 2.8 1.8 2.8 H 0 M 1.6 2.8 L 3 5"},
    {'S', "M 3 0.7 Q 2.5 0 1.5 0 Q 0 0 0 1.3 Q 0 2.4 1.5 2.5 Q 3 2.6 3 3.7 "
          "Q 3 5 1.5 5 Q 0.5 5 0 4.3"},
    {'T', "M 0 0 H 3 M 1.5 0 V 5"},
    {'U', "M 0 0 V 3.5 Q 0 5 1.5 5 Q 3 5 3 3.5 V 0"},
    {'V', "M 0 0 L 1.5 5 L 3 0"},
    {'W', "M 0 0 L 0.7 5 L 1.5 1.8 L 2.3 5 L 3 0"},
    {'X', "M 0 0 L 3 5 M 3 0 L 0 5"},
    {'Y', "M 0 0 L 1.5 2.5 L 3 0 M 1.5 2.5 V 5"},
    {'Z', "M 0 0 H 3 L 0 5 H 3"},
    {'0', "M 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 2.5 Q 3 0 1.5 0 Z"},
    {'1', "M 0.6 1.1 L 1.9 0 V 5"},
    {'2', "M 0 1 Q 0.4 0 1.5 0 Q 3 0 3 1.5 Q 3 2.5 0 5 H 3"},
    {'3', "M 0 0 H 3 L 1.4 2 Q 3 2 3 3.5 Q 3 5 1.5 5 Q 0.5 5 0 4.3"},
    {'4', "M 2.2 5 V 0 L 0 3.5 H 3"},
    {'5', "M 3 0 H 0.3 L 0 2.3 Q 0.6 2 1.5 2 Q 3 2 3 3.5 Q 3 5 1.5 5 Q 0.5 5 0 4.3"},
    {'6', "M 2.7 0.3 Q 2.2 0 1.5 0 Q 0 0 0 2.8 V 3.4 Q 0 5 1.5 5 Q 3 5 3 3.4 "
          "Q 3 2 1.5 2 Q 0.3 2 0 3"},
    {'7', "M 0 0 H 3 L 1 5"},
    {'8', "M 1.5 2.4 Q 0.2 2.4 0.2 1.2 Q 0.2 0 1.5 0 Q 2.8 0 2.8 1.2 "
          "Q 2.8 2.4 1.5 2.4 Q 0 2.4 0 3.7 Q 0 5 1.5 5 Q 3 5 3 3.7 Q 3 2.4 1.5 2.4"},
    {'9', "M 0.3 4.7 Q 0.8 5 1.5 5 Q 3 5 3 2.2 V 1.6 Q 3 0 1.5 0 Q 0 0 0 1.6 "
          "Q 0 3 1.5 3 Q 2.7 3 3 2"},
};
// lang_gate:på

// ------------------------------------------------------------- ritningen

// En tolkad beskrivning i bildens fysiska px, fäst mot pixelrutnätet.
struct Ritning {
    struct Båge {   // ellipsens kontur; en cirkel har rx == ry
        float cx, cy, rx, ry, a0, a1;
        bool  hel;
    };
    struct Ruta {
        float x, y, w, h, r;
        bool  fylld;
    };
    struct Skiva {
        float cx, cy, r;
    };
    std::vector<gfx::Pointf>              streck;   // par: segmentens ändar
    std::vector<std::uint8_t>             rund;     // per par: en kurvas korda
    std::vector<Båge>                     bågar;
    std::vector<Ruta>                     rutor;
    std::vector<Skiva>                    skivor;
    std::vector<std::vector<gfx::Pointf>> ytor;     // fyllda polygoner
    std::vector<gfx::Pointf>              hål;      // par: utstansad text
    std::vector<float>                    hålH;     // halva strecket per par
};

// Strecket i HELA fysiska px för ett streck på `enheter` i rutan, i en bild
// på `px` — avrundat, aldrig under en px.
float helaPx(float enheter, int px)
{
    return std::max(1.0f, std::round(enheter * static_cast<float>(px) / kRuta));
}

// Ikonens streck: 1 px upp till 31, 2 px i 32–63, 3 px i 64–95 … — tunt
// också i de stora storlekarna.
float ikonStreck(int px)
{
    return static_cast<float>(1 + px / 32);
}

// AVBILDNINGEN — beskrivningens tal i bildens fysiska px, fästa mot
// pixelrutnätet (filhuvudet). `s`/`dx`/`dy` lägger beskrivningen i rutan
// (glyferna i en text, förskjutna och skalade; ikonen själv står i rutan som
// den är), `k` är px per rutans enhet och `streck` den bredd punkterna fästs
// för: ett udda streck står mitt i en pixel, ett jämnt på en pixelgräns. En
// punkt hålls så långt in att strecket inte klipps av bildens kant.
struct Avbildning {
    float s = 1.0f, dx = 0.0f, dy = 0.0f;
    float k      = 1.0f;
    float sida   = kRuta;   // bildens sida i px
    float streck = 1.0f;    // px

    float x(float v) const { return fäst((dx + v * s) * k); }
    float y(float v) const { return fäst((dy + v * s) * k); }
    // En FYLLD rektangels kant: pixelgränsen.
    float kant(float p) const { return std::clamp(std::round(p * k), 0.0f, sida); }
    float kantX(float v) const { return kant(dx + v * s); }
    float kantY(float v) const { return kant(dy + v * s); }
    // En cirkels, ellipsens eller bågens radie: hela px.
    float radie(float v) const
    {
        return v > 0.0f ? std::max(1.0f, std::round(v * s * k)) : 0.0f;
    }
    // Ett mått som inte fästs — en skivas radie, ett hörns rundning.
    float mått(float v) const { return v * s * k; }

private:
    float fäst(float p) const
    {
        const float mitt = std::fmod(streck, 2.0f) == 1.0f ? 0.5f : 0.0f;
        const float halv = streck / 2.0f;
        return std::clamp(std::round(p - mitt) + mitt, halv, sida - halv);
    }
};

void tolka(const char* text, Ritning& ut, int djup, bool fyllAlla,
           const Avbildning& a);

// `K`: texten, glyf för glyf, som utstansade streck i rutan (x, y, b, h — i
// rutans enheter) — centrerad, och nedskalad i sin helhet när den inte ryms.
// Textens streck är ett helt antal px, och glyferna fästs för det. Ett tecken
// utan glyf lämnar en lucka.
void stansa(const std::string& text, float x, float y, float b, float h,
            const Avbildning& a, Ritning& ut)
{
    if (text.empty())
        return;
    const float n     = static_cast<float>(text.size());
    const float bredd = n * kGlyfB + (n - 1.0f) * kGlyfSteg;
    const float s     = std::min({1.0f, h / kGlyfH, b / bredd});
    const float x0    = x + (b - bredd * s) / 2.0f;
    const float y0    = y + (h - kGlyfH * s) / 2.0f;
    Avbildning  g     = a;
    g.s               = s;
    g.dy              = y0;
    g.streck          = helaPx(kTextStreck * s, static_cast<int>(a.sida));
    for (std::size_t t = 0; t < text.size(); ++t) {
        char c = text[t];
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        for (const Glyf& gl : kGlyfer)
            if (gl.tecken == c) {
                Ritning glyf;
                g.dx = x0 + static_cast<float>(t) * (kGlyfB + kGlyfSteg) * s;
                tolka(gl.path, glyf, 1, false, g);
                ut.hål.insert(ut.hål.end(), glyf.streck.begin(), glyf.streck.end());
                ut.hålH.resize(ut.hål.size() / 2, g.streck / 2.0f);
                break;
            }
    }
}

// Beskrivningens ord, åtskilda av mellanslag.
std::vector<std::string> ord(const char* text)
{
    std::vector<std::string> ut;
    std::string              o;
    for (const char* p = text; ; ++p) {
        if (*p == ' ' || *p == '\0') {
            if (!o.empty())
                ut.push_back(std::move(o));
            o.clear();
            if (*p == '\0')
                break;
        } else {
            o += *p;
        }
    }
    return ut;
}

// `fyllAlla` fyller varje linje — en DEL efter `F` (`F @mapp`).
void tolka(const char* text, Ritning& ut, int djup, bool fyllAlla,
           const Avbildning& a)
{
    const std::vector<std::string> o = ord(text);
    std::size_t                    i = 0;
    bool                           fel = false;
    const auto tal = [&]() {
        double v = 0.0;
        if (i >= o.size() || !propparse::parseDouble(o[i], v)) {
            fel = true;
            return 0.0f;
        }
        ++i;
        return static_cast<float>(v);
    };

    std::vector<gfx::Pointf> väg;    // linjen som byggs
    std::vector<std::uint8_t> böjd;  // per punkt: biten dit är en kurvas korda
    bool fylls = false, nästaFylls = false;
    const auto avsluta = [&] {
        if (väg.size() >= 2) {
            if (fylls) {
                ut.ytor.push_back(väg);
            } else {
                for (std::size_t k = 1; k < väg.size(); ++k) {
                    ut.streck.push_back(väg[k - 1]);
                    ut.streck.push_back(väg[k]);
                    ut.rund.push_back(böjd[k]);
                }
            }
        }
        väg.clear();
        böjd.clear();
        fylls = false;
    };
    const auto till = [&](float x, float y, bool kurva = false) {
        if (väg.empty()) {
            fel = true;   // L/H/V/Q utan ett M före
        } else {
            väg.push_back({x, y});
            böjd.push_back(kurva ? 1 : 0);
        }
    };

    while (i < o.size() && !fel) {
        const std::string& k = o[i++];
        if (k[0] == '@') {
            avsluta();
            const char* del = delKontur(std::string_view(k).substr(1));
            if (!del || djup > 0) {   // okänd del, eller en del i en del
                fel = true;
                break;
            }
            tolka(del, ut, djup + 1, fyllAlla || nästaFylls, a);
            nästaFylls = false;
            continue;
        }
        if (k.size() != 1) {
            fel = true;
            break;
        }
        switch (k[0]) {
        case 'F':
            avsluta();
            nästaFylls = true;
            break;
        case 'M': {
            avsluta();
            const float x = a.x(tal()), y = a.y(tal());
            väg   = {{x, y}};
            böjd  = {0};
            fylls      = fyllAlla || nästaFylls;
            nästaFylls = false;
            break;
        }
        case 'L': {
            const float x = a.x(tal()), y = a.y(tal());
            till(x, y);
            break;
        }
        case 'H':
            if (!väg.empty()) {
                const float x = a.x(tal());
                till(x, väg.back().y);
            } else {
                fel = true;
            }
            break;
        case 'V':
            if (!väg.empty()) {
                const float y = a.y(tal());
                till(väg.back().x, y);
            } else {
                fel = true;
            }
            break;
        case 'Q': {
            const float kx = a.x(tal()), ky = a.y(tal());
            const float x = a.x(tal()), y = a.y(tal());
            if (väg.empty()) {
                fel = true;
                break;
            }
            // Kurvan som korda: åtta bitar räcker i en ruta på 24. Ändarna
            // och styrpunkten är fästa, kordans punkter mellan dem inte.
            const gfx::Pointf p0 = väg.back();
            for (int s = 1; s <= 8; ++s) {
                const float t = static_cast<float>(s) / 8.0f, u = 1.0f - t;
                till(u * u * p0.x + 2.0f * u * t * kx + t * t * x,
                     u * u * p0.y + 2.0f * u * t * ky + t * t * y, true);
            }
            break;
        }
        case 'Z':
            if (!väg.empty())
                till(väg.front().x, väg.front().y);
            avsluta();
            break;
        case 'C': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal()), r = a.radie(tal());
            ut.bågar.push_back({cx, cy, r, r, 0.0f, 360.0f, true});
            break;
        }
        case 'A': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal()), r = a.radie(tal());
            const float a0 = tal(), a1 = tal();
            ut.bågar.push_back({cx, cy, r, r, a0, a1, false});
            break;
        }
        case 'E': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal());
            const float rx = a.radie(tal()), ry = a.radie(tal());
            ut.bågar.push_back({cx, cy, rx, ry, 0.0f, 360.0f, true});
            break;
        }
        case 'U': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal());
            const float rx = a.radie(tal()), ry = a.radie(tal());
            const float a0 = tal(), a1 = tal();
            ut.bågar.push_back({cx, cy, rx, ry, a0, a1, false});
            break;
        }
        case 'R':
        case 'B': {
            // Hörnen fästs, inte bredden och höjden: en konturerad ruta har
            // sina sidor mitt i strecket, en fylld sina kanter på
            // pixelgränser.
            avsluta();
            const float x = tal(), y = tal(), w = tal(), h = tal();
            const float r = a.mått(tal());
            const bool  fylld = k[0] == 'B';
            const float x0 = fylld ? a.kantX(x) : a.x(x);
            const float y0 = fylld ? a.kantY(y) : a.y(y);
            const float x1 = fylld ? a.kantX(x + w) : a.x(x + w);
            const float y1 = fylld ? a.kantY(y + h) : a.y(y + h);
            ut.rutor.push_back({x0, y0, x1 - x0, y1 - y0, r, fylld});
            break;
        }
        case 'D': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal()), r = a.mått(tal());
            ut.skivor.push_back({cx, cy, r});
            break;
        }
        case 'K': {
            avsluta();
            const float x = tal(), y = tal(), w = tal(), h = tal();
            if (fel || i >= o.size()) {
                fel = true;
                break;
            }
            stansa(o[i++], a.dx + x * a.s, a.dy + y * a.s, w * a.s, h * a.s, a,
                   ut);
            break;
        }
        default:
            fel = true;
            break;
        }
    }
    avsluta();
    if (fel && förstaFelet())
        gfx::log("ikonen \"%s\": beskrivningen går inte att läsa vid ord %d i \"%s\" "
                 "— resten av lagret ritas inte",
                 ritasNu().c_str(), static_cast<int>(i), text);
}

// Avståndet från (px, py) till segmentet a–b, i kvadrat.
float avstånd2(float px, float py, gfx::Pointf a, gfx::Pointf b)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float l2 = dx * dx + dy * dy;
    float       t  = l2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0.0f;
    t              = std::clamp(t, 0.0f, 1.0f);
    const float ex = a.x + t * dx - px, ey = a.y + t * dy - py;
    return ex * ex + ey * ey;
}

// Ligger punkten (px, py) i strecket a–b med halva bredden `h`? Ett rakt
// streck har RAKA ändar — förlängt med halva bredden förbi varje ände, så
// två streck som möts i rät vinkel bildar ett skarpt hörn. En kurvas korda
// har runda (`rund`), så att kurvan blir jämn.
bool iStreck(float px, float py, gfx::Pointf a, gfx::Pointf b, float h, bool rund)
{
    if (rund)
        return avstånd2(px, py, a, b) <= h * h;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float l  = std::hypot(dx, dy);
    if (l <= 0.0f)
        return std::abs(px - a.x) <= h && std::abs(py - a.y) <= h;
    const float längs = ((px - a.x) * dx + (py - a.y) * dy) / l;
    const float tvärs = ((px - a.x) * dy - (py - a.y) * dx) / l;
    return std::abs(tvärs) <= h && längs >= -h && längs <= l + h;
}

// Ligger punkten (x, y) — bildens px — i någon av ritningens former? `h` är
// halva strecket.
bool inne(const Ritning& r, float x, float y, float h)
{
    const float h2 = h * h;
    for (std::size_t k = 0; k + 1 < r.streck.size(); k += 2)
        if (iStreck(x, y, r.streck[k], r.streck[k + 1], h, r.rund[k / 2] != 0))
            return true;
    for (const Ritning::Skiva& s : r.skivor)
        if ((x - s.cx) * (x - s.cx) + (y - s.cy) * (y - s.cy) <= s.r * s.r)
            return true;
    for (const Ritning::Ruta& q : r.rutor) {
        // Rutan har SKARPA hörn. En rundad ruta (radie > 0) får i stället
        // hörnet SKURET: den yttersta pixeln i varje hörn lämnas ute.
        const float ut = q.fylld ? 0.0f : h;   // ytterkanten utanför sidan
        const float x0 = q.x - ut, y0 = q.y - ut;
        const float x1 = q.x + q.w + ut, y1 = q.y + q.h + ut;
        if (x < x0 || x > x1 || y < y0 || y > y1)
            continue;
        if (q.r > 0.0f && (x < x0 + 1.0f || x > x1 - 1.0f) &&
            (y < y0 + 1.0f || y > y1 - 1.0f))
            continue;
        if (q.fylld)
            return true;
        const bool inuti = x > q.x + h && x < q.x + q.w - h && y > q.y + h &&
                           y < q.y + q.h - h;
        if (!inuti)
            return true;
    }
    constexpr float kGrad = 3.14159265358979f / 180.0f;
    for (const Ritning::Båge& b : r.bågar) {
        const float nx = (x - b.cx) / b.rx, ny = (y - b.cy) / b.ry;
        const float q  = std::hypot(nx, ny);
        if (q <= 0.0f)
            continue;
        // Avståndet till ellipsen i första ordningen: (|n| − 1) / |∇|n||.
        const float grad = std::hypot(nx / b.rx, ny / b.ry) / q;
        const float d    = std::abs(q - 1.0f) / grad;
        if (b.hel) {
            if (d <= h)
                return true;
            continue;
        }
        float vinkel = std::atan2(ny, nx) / kGrad;   // −180 … 180
        if (vinkel < 0.0f)
            vinkel += 360.0f;
        const float start = std::fmod(b.a0, 360.0f) < 0.0f
                                ? std::fmod(b.a0, 360.0f) + 360.0f
                                : std::fmod(b.a0, 360.0f);
        const float rel = std::fmod(vinkel - start + 720.0f, 360.0f);
        if (rel <= b.a1 - b.a0 && d <= h)
            return true;
        // De runda ändarna.
        for (const float a : {b.a0, b.a1}) {
            const gfx::Pointf p{b.cx + b.rx * std::cos(a * kGrad),
                                b.cy + b.ry * std::sin(a * kGrad)};
            if ((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) <= h2)
                return true;
        }
    }
    for (const std::vector<gfx::Pointf>& yta : r.ytor) {
        bool udda = false;   // jämn-udda-regeln
        for (std::size_t a = 0, b = yta.size() - 1; a < yta.size(); b = a++) {
            const gfx::Pointf p = yta[a], q = yta[b];
            if ((p.y > y) != (q.y > y) &&
                x < (q.x - p.x) * (y - p.y) / (q.y - p.y) + p.x)
                udda = !udda;
        }
        if (udda)
            return true;
    }
    return false;
}

// Ligger punkten i en UTSTANSAD text? Varje par har sitt eget streck.
bool iHål(const Ritning& r, float x, float y)
{
    for (std::size_t k = 0; k + 1 < r.hål.size(); k += 2) {
        const float h = r.hålH[k / 2];
        if (avstånd2(x, y, r.hål[k], r.hål[k + 1]) <= h * h)
            return true;
    }
    return false;
}

// FÄRGROLLERNA — konturen, färgvariantens fem och papperet (färgens ord).
enum class Roll : std::uint8_t { Text, Blue, Green, Red, Amber, Yellow, Paper };

bool rollAv(const std::string& ord, Roll& ut)
{
    static constexpr std::pair<const char*, Roll> kRoller[] = {
        {"blue", Roll::Blue},   {"green", Roll::Green},   {"red", Roll::Red},
        {"amber", Roll::Amber}, {"yellow", Roll::Yellow}, {"paper", Roll::Paper},
    };
    for (const auto& [namn, roll] : kRoller)
        if (ord == namn) {
            ut = roll;
            return true;
        }
    return false;
}

gfx::Color färgAv(Roll roll, const Theme::Colors& p)
{
    switch (roll) {
    case Roll::Blue:   return p.stockBlue;
    case Roll::Green:  return p.stockGreen;
    case Roll::Red:    return p.stockRed;
    case Roll::Amber:  return p.stockAmber;
    case Roll::Yellow: return p.stockYellow;
    case Roll::Paper:  return p.stockPaper;
    case Roll::Text:   break;
    }
    return p.text;
}

enum class Ton : std::uint8_t { Bas, Ljus, Mörk, Djup, Bläck };

bool tonAv(const std::string& ord, Ton& ut)
{
    static constexpr std::pair<const char*, Ton> kToner[] = {
        {"light", Ton::Ljus}, {"dark", Ton::Mörk}, {"deep", Ton::Djup}, {"ink", Ton::Bläck},
    };
    for (const auto& [namn, ton] : kToner)
        if (ord == namn) {
            ut = ton;
            return true;
        }
    return false;
}

// Färgen med ljusheten flyttad `d` (−1 … 1) i HSL — kulören och mättnaden
// står kvar, alfan följer med.
gfx::Color ljushet(gfx::Color c, float d)
{
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const float l  = (mx + mn) / 2.0f;
    float       h = 0.0f, s = 0.0f;
    if (mx > mn) {
        const float k = mx - mn;
        s = l > 0.5f ? k / (2.0f - mx - mn) : k / (mx + mn);
        if (mx == r)
            h = (g - b) / k + (g < b ? 6.0f : 0.0f);
        else if (mx == g)
            h = (b - r) / k + 2.0f;
        else
            h = (r - g) / k + 4.0f;
        h *= 60.0f;
    }
    gfx::Color ut = color::hsl(h, s, std::clamp(l + d, 0.0f, 1.0f));
    ut.a          = c.a;
    return ut;
}

gfx::Color tonad(gfx::Color c, Ton ton)
{
    switch (ton) {
    case Ton::Ljus: return ljushet(c, kLjus);
    case Ton::Mörk: return ljushet(c, -kMörk);
    case Ton::Djup:  return ljushet(c, -kDjup);
    case Ton::Bläck: return ljushet(c, -kBläck);
    case Ton::Bas:   break;
    }
    return c;
}

// Har ritningen någon YTA — en fylld form?
bool harYta(const Ritning& r)
{
    return !r.ytor.empty() || !r.skivor.empty() ||
           std::any_of(r.rutor.begin(), r.rutor.end(),
                       [](const Ritning::Ruta& q) { return q.fylld; });
}

// Är ritningen bara YTOR — fyllda former och inga streck? Ett lager `over`
// som är en yta är en DETALJ FRAMFÖR (glaset, klockan), ett med streck ett
// TECKEN (pilen, krysset).
bool ärYta(const Ritning& r)
{
    return r.streck.empty() && r.bågar.empty() &&
           std::all_of(r.rutor.begin(), r.rutor.end(),
                       [](const Ritning::Ruta& q) { return q.fylld; });
}

// Ett LAGER: formerna i en färgroll och ton, bakom eller ovanpå konturen.
struct Lager {
    Roll    roll     = Roll::Text;
    Ton     ton      = Ton::Bas;
    bool    över     = false;
    bool    stark    = false;   // `strong`: tonen blandad mot texten
    bool    gradient = false;   // `gradient`: ljus → mörk ton diagonalt
    float   krav     = kKantKontrast;   // `contrast N`: bildlagrets krav
    Ritning former;
};

// Färgbeskrivningen i lager (ett lagers färg): ett färgord eller
// `over`/`under` börjar ett nytt lager, och `all [ton] <färg>` färgar
// konturen. `strong`, en ton (`light`/`dark`/`deep`) och `gradient` före ett
// färgord gäller det lagret.
void tolkaFärg(const char* text, const Avbildning& a, std::vector<Lager>& ut,
               Lager& kontur)
{
    // En FÄRGDEL (`#namn`, `kFärgDelar`) läggs in ord för ord där den står.
    std::vector<std::string> o;
    for (std::string& w : ord(text)) {
        if (w[0] != '#') {
            o.push_back(std::move(w));
            continue;
        }
        const char* del = delFärg(std::string_view(w).substr(1));
        if (!del) {
            if (förstaFelet())
                gfx::log("ikonen \"%s\": färgdelen \"%s\" finns inte", ritasNu().c_str(),
                         w.c_str());
            continue;
        }
        for (std::string& dw : ord(del))
            o.push_back(std::move(dw));
    }
    Roll        roll = Roll::Text;
    Ton         ton = Ton::Bas, nästaTon = Ton::Bas;
    bool        över = false, stark = false, nästaStark = false;
    bool        gradient = false, nästaGradient = false;
    std::string del;
    const auto  spara = [&] {
        if (del.empty())
            return;
        Lager l;
        l.roll     = roll;
        l.ton      = ton;
        l.över     = över;
        l.stark    = stark;
        l.gradient = gradient;
        tolka(del.c_str(), l.former, 0, false, a);
        ut.push_back(std::move(l));
        del.clear();
    };
    for (std::size_t i = 0; i < o.size(); ++i) {
        Roll r = Roll::Text;
        Ton  t = Ton::Bas;
        double kravtal = 0.0;
        if (o[i] == "all") {
            std::size_t j = i + 1;
            if (j < o.size() && tonAv(o[j], t))
                ++j;
            if (j < o.size() && rollAv(o[j], kontur.roll)) {
                kontur.ton = t;
                i          = j;
            }
        } else if (o[i] == "contrast" && i + 1 < o.size() &&
                   propparse::parseDouble(o[i + 1], kravtal)) {
            kontur.krav = static_cast<float>(kravtal);
            ++i;
        } else if (rollAv(o[i], r)) {
            spara();
            roll          = r;
            ton           = nästaTon;
            stark         = nästaStark;
            gradient      = nästaGradient;
            nästaTon      = Ton::Bas;
            nästaStark    = false;
            nästaGradient = false;
        } else if (tonAv(o[i], t)) {
            spara();
            nästaTon = t;
        } else if (o[i] == "gradient") {
            spara();
            nästaGradient = true;
        } else if (o[i] == "strong") {
            spara();
            nästaStark = true;
        } else if (o[i] == "over" || o[i] == "under") {
            spara();
            över = o[i] == "over";
        } else {
            del += o[i];
            del += ' ';
        }
    }
    spara();
}

// Läget ur paletten: texten ljusare än bakgrunden är mörkt läge.
bool mörktLäge(const Theme::Colors& p)
{
    return color::luminance(p.text) > color::luminance(p.windowBg);
}

// Kontrasten mellan `c` och den sämsta av `mot`.
float sämstaKontrast(gfx::Color c, const std::vector<gfx::Color>& mot)
{
    float v = std::numeric_limits<float>::max();
    for (const gfx::Color m : mot)
        v = std::min(v, color::contrast(c, m));
    return v;
}

// `c` flyttad i ljushet åt `riktning` (+ ljusare, − mörkare) tills den når
// `krav` mot var och en av `mot`, eller så långt det går.
gfx::Color flytta(gfx::Color c, float riktning, const std::vector<gfx::Color>& mot,
                  float krav)
{
    gfx::Color ny = c;
    for (int steg = 1; steg * kKantSteg <= 1.0f && sämstaKontrast(ny, mot) < krav; ++steg)
        ny = ljushet(c, riktning * kKantSteg * static_cast<float>(steg));
    return ny;
}

// Hur många steg (`kKantSteg`) `c` måste flyttas åt `riktning` för att nå
// `krav` mot `mot` — eller −1 när det inte går.
int stegTill(gfx::Color c, float riktning, const std::vector<gfx::Color>& mot, float krav)
{
    for (int steg = 0; steg * kKantSteg <= 1.0f; ++steg)
        if (sämstaKontrast(ljushet(c, riktning * kKantSteg * static_cast<float>(steg)), mot) >=
            krav)
            return steg;
    return -1;
}

// LÄGG PÅ ETT LAGER — `lager` hamnar ovanpå `bild` (det som redan ritats,
// genomskinligt där inget ligger), och dess YTTERPIXLAR möter det de hamnar
// mot: bildens pixlar där de täcker, appens tre ytor (fönstrets bakgrund,
// panelen och namnlisten) där inget ligger. En ytterpixel är lagrets
// kantutjämnade pixlar och de täckande som har en granne (av åtta) som lagret
// inte täcker. De som inte når `krav` mot det de möter flyttas i ljushet BORT
// från det — en ljus detalj blir ljusare, en mörk mörkare, så lagret behåller
// sin karaktär — och HELA kanten flyttas lika långt, det längsta som behövs,
// så att ringen blir jämn. Räcker inte det VÄXER YTAN: grannarna rakt
// utanför lagret ljusas upp eller mörkas, bort från lagret, tills kravet nås
// — den vita pilen på en för ljus skiva får en mörkare ring i skivan. Vid
// bildens kant, där ingen ring får plats, flyttas pixeln själv. Sist läggs
// lagret på, i förmultiplicerad rymd.
void läggPå(glzassets::Rgba& bild, glzassets::Rgba& lager, const Theme::Colors& p,
            float krav, bool linje)
{
    const int  n     = bild.width;
    const auto index = [&](int x, int y) { return (static_cast<std::size_t>(y) * n + x) * 4; };
    const auto iBild = [&](int x, int y) { return x >= 0 && y >= 0 && x < n && y < n; };
    const auto alfaL = [&](int x, int y) {
        return iBild(x, y) ? int{lager.px[index(x, y) + 3]} : 0;
    };
    const auto färgAv = [](const glzassets::Rgba& b, std::size_t i) {
        return gfx::Color{b.px[i], b.px[i + 1], b.px[i + 2], 255};
    };
    const std::vector<gfx::Color> appen = {p.windowBg, p.panel, p.titlebar};
    // Det som ligger bakom i (x, y): bildens pixel där den täcker, annars appens ytor.
    const auto bakom = [&](int x, int y, std::vector<gfx::Color>& ut) {
        if (iBild(x, y) && bild.px[index(x, y) + 3] >= 128)
            ut.push_back(färgAv(bild, index(x, y)));
        else
            ut.insert(ut.end(), appen.begin(), appen.end());
    };

    // Ringen utanför lagret där ytan växer: färgen per pixel, och hur långt
    // den flyttats (den som flyttats längst vinner, när flera vill ha den).
    std::vector<gfx::Color> ring(static_cast<std::size_t>(n) * n);
    std::vector<float>      ringFlytt(static_cast<std::size_t>(n) * n, -1.0f);

    // Ytterpixlarna: färgen, det de möter, grannarna utanför och riktningen
    // bort från det de möter.
    struct Ytter {
        std::size_t                      i;
        gfx::Color                       c;
        std::vector<gfx::Color>          mot;
        std::vector<std::pair<int, int>> ute;               // rakt intill, i bilden
        bool                             vidKanten = false;  // rakt intill bildens kant
        float                            riktning;
    };
    // Kanten är ett STRECK bred: en pixel inom strecket från det lagret inte
    // täcker hör till den, så att hela konturen följer med och inte bara dess
    // yttersta rad.
    const int          r = static_cast<int>(ikonStreck(n));
    std::vector<Ytter> ytter;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const int a = alfaL(x, y);
            if (a == 0)
                continue;
            Ytter u;
            if (a < 255)
                bakom(x, y, u.mot);
            // Ringen växer bara RAKT ut (inte på diagonalen): ett hörn som
            // lagret bara rör snett — det skurna hörnet — är inte en kant.
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if ((dx || dy) && alfaL(x + dx, y + dy) < 128) {
                        bakom(x + dx, y + dy, u.mot);
                        const bool rakt = std::abs(dx) + std::abs(dy) == 1;
                        if (rakt && iBild(x + dx, y + dy))
                            u.ute.push_back({x + dx, y + dy});
                        else if (rakt)
                            u.vidKanten = true;
                    }
            if (u.mot.empty())
                continue;
            u.i = index(x, y);
            u.c = färgAv(lager, u.i);
            ytter.push_back(std::move(u));
        }
    if (ytter.empty())
        return;
    // En linje får textens krav.
    if (linje)
        krav = std::max(krav, kLinjeKontrast);
    // Lagrets TON är dess ytterpixlars median: riktningen bort från det en
    // pixel möter avgörs av lagret och inte av pixeln, så att en skuggad
    // nederkant inte vänder åt fel håll.
    std::vector<float> ljus;
    for (const Ytter& u : ytter)
        ljus.push_back(color::luminance(u.c));
    std::nth_element(ljus.begin(), ljus.begin() + static_cast<std::ptrdiff_t>(ljus.size() / 2),
                     ljus.end());
    const float ton = ljus[ljus.size() / 2];
    // Det gemensamma steget per håll: det som `kKantAndel` av ytterpixlarna
    // behöver — inte den värsta, så att några få kontaktpixlar (en etikett
    // mot sidans kontur) inte drar hela lagret till svart. De som inte når
    // kravet med det får ringen utanför.
    // En LINJE har inga kontaktpixlar att ta hänsyn till: steget räcker för
    // var och en av dess täckande pixlar.
    std::vector<int> upp, ner;
    for (Ytter& u : ytter) {
        float medel = 0.0f;
        for (const gfx::Color m : u.mot)
            medel += color::luminance(m);
        medel /= static_cast<float>(u.mot.size());
        u.riktning  = ton >= medel ? 1.0f : -1.0f;
        if (linje && lager.px[u.i + 3] < 128)
            continue;
        const int s = stegTill(u.c, u.riktning, u.mot, krav);
        const int behov = s >= 0 ? s : static_cast<int>(1.0f / kKantSteg);
        (u.riktning > 0.0f ? upp : ner).push_back(behov);
    }
    const float andelen = linje ? 1.0f : kKantAndel;
    const auto  andel   = [andelen](std::vector<int>& v) {
        if (v.empty())
            return 0;
        const std::size_t i = std::min(
            v.size() - 1, static_cast<std::size_t>(andelen * static_cast<float>(v.size())));
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(i), v.end());
        return v[i];
    };
    const int stegUpp = andel(upp), stegNer = andel(ner);
    for (const Ytter& u : ytter) {
        const int  steg = u.riktning > 0.0f ? stegUpp : stegNer;
        gfx::Color ny   = ljushet(u.c, u.riktning * kKantSteg * static_cast<float>(steg));
        // Vid BILDENS KANT finns ingen plats för en ring: där flyttas pixeln
        // själv så långt kravet kräver.
        if (u.vidKanten && sämstaKontrast(ny, u.mot) < krav)
            ny = flytta(ny, u.riktning, u.mot, krav);
        lager.px[u.i]     = ny.r;
        lager.px[u.i + 1] = ny.g;
        lager.px[u.i + 2] = ny.b;
        if (sämstaKontrast(ny, u.mot) >= krav)
            continue;
        // Ytan växer: grannarna utanför, bort från lagret.
        for (const auto& [gx, gy] : u.ute) {
            std::vector<gfx::Color> bas;
            bakom(gx, gy, bas);
            // Den av ytorna som ligger närmast lagret är den som måste flyttas längst.
            gfx::Color b = bas.front();
            for (const gfx::Color k : bas)
                if (color::contrast(k, ny) < color::contrast(b, ny))
                    b = k;
            const gfx::Color  hb = flytta(b, -u.riktning, {ny}, krav);
            const float       d  = std::abs(color::luminance(hb) - color::luminance(b));
            const std::size_t j  = static_cast<std::size_t>(gy) * n + gx;
            if (d > ringFlytt[j]) {
                ring[j]      = hb;
                ringFlytt[j] = d;
            }
        }
    }
    for (std::size_t j = 0; j < ring.size(); ++j)
        if (ringFlytt[j] >= 0.0f) {
            bild.px[j * 4]     = ring[j].r;
            bild.px[j * 4 + 1] = ring[j].g;
            bild.px[j * 4 + 2] = ring[j].b;
            bild.px[j * 4 + 3] = 255;
        }
    // KANTUTJÄMNINGEN FÖRSTÄRKS: en halvtäckt pixel ser genomskinlig ut, och
    // en tunn sned linje är bara sådana — den blir blek mot den tjocka. Som
    // fonter förstärker tunna streck höjs täckningen i varje kantutjämnad
    // pixel (`kTäckning`, en exponent under 1), i båda lägena.
    for (std::size_t i = 3; i < lager.px.size(); i += 4) {
        const int a8 = lager.px[i];
        if (a8 == 0 || a8 == 255)
            continue;
        const float a = std::pow(static_cast<float>(a8) / 255.0f, kTäckning);
        lager.px[i]   = static_cast<std::uint8_t>(std::lround(a * 255.0f));
    }
    // Lagret ovanpå, förmultiplicerat.
    for (std::size_t i = 0; i < bild.px.size(); i += 4) {
        const float la = lager.px[i + 3] / 255.0f, ba = bild.px[i + 3] / 255.0f;
        const float oa = la + ba * (1.0f - la);
        if (oa <= 0.0f)
            continue;
        for (int k = 0; k < 3; ++k)
            bild.px[i + k] = static_cast<std::uint8_t>(std::lround(
                (lager.px[i + k] * la + bild.px[i + k] * ba * (1.0f - la)) / oa));
        bild.px[i + 3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
    }
}

// ETT LAGER — konturen `konturtext` och färgbeskrivningen `färgtext` (får
// vara null: bara konturen) rastrerade i en bild på `px`, UTAN kontrastkrav:
// färgerna, tonerna, gradienterna, högdagern och den fasade kanten. Lagrets
// krav (`contrast N`, förval `kKantKontrast`) lämnas i `krav` till
// sammanslagningen (`läggPå`).
glzassets::Rgba ritaLager(const char* konturtext, const char* färgtext, int px,
                          IconStyle style, const Theme::Colors& p, bool första,
                          float& krav, bool& linje)
{
    // Beskrivningen i bildens px, fäst för strecket.
    Avbildning a;
    a.k      = static_cast<float>(px) / kRuta;
    a.sida   = static_cast<float>(px);
    a.streck = ikonStreck(px);

    // Färgens lager ritas i BÅDA stilarna. Mono är samma bild nedtonad:
    // ytorna blandade mot ytan ikonen står på, tecknen (`over` med streck)
    // och konturen i textfärgen — `all <färg>` och gradienten gäller bara
    // Color.
    const bool mono = style == IconStyle::Mono;
    Lager      kontur;
    tolka(konturtext, kontur.former, 0, false, a);
    std::vector<Lager> färg;
    krav = kKantKontrast;
    if (färgtext) {
        Lager k;
        tolkaFärg(färgtext, a, färg, k);
        if (!mono) {
            kontur.roll = k.roll;
            kontur.ton  = k.ton;
        }
        krav = k.krav;
    }
    // Ett lager av bara streck, utan en enda yta, är en LINJE.
    linje = !harYta(kontur.former) &&
            std::none_of(färg.begin(), färg.end(),
                         [](const Lager& l) { return harYta(l.former); });
    const gfx::Color mål = mörktLäge(p) ? p.windowBg : p.stockPaper;   // monos nedtoning

    // Ordningen ett prov frågar lagren i, det ÖVERSTA först: färgens `over`
    // (det senast skrivna överst), konturen, färgens `under` (likaså).
    // Det första lager provet ligger i ger dess färg.
    std::vector<const Lager*> ordning;
    for (auto it = färg.rbegin(); it != färg.rend(); ++it)
        if (it->över)
            ordning.push_back(&*it);
    ordning.push_back(&kontur);
    for (auto it = färg.rbegin(); it != färg.rend(); ++it)
        if (!it->över)
            ordning.push_back(&*it);
    // Lagrets färg: EN, eller en gradient från `a` till `b`.
    struct Färgning {
        gfx::Color a, b;
        bool       gradient;
    };
    std::vector<Färgning> färger;
    // KONTUREN är det enda som skiljer lägena: det FÖRSTA lagrets kant — den
    // som möter bakgrunden — speglas i mörkt läge, så att en djup kant blir
    // lika ljus som den annars är mörk. Ytorna är desamma, och ett lager
    // ovanpå (utropstecknet på varningen) möter det under sig och speglas inte.
    const bool spegla = första && mörktLäge(p);
    for (const Lager* l : ordning) {
        gfx::Color c = färgAv(l->roll, p);
        if (l->stark)
            c = color::mix(c, p.text, kStark);
        if (!mono && spegla && l == &kontur && l->ton != Ton::Bas) {
            const float d = l->ton == Ton::Ljus ? -kLjus : l->ton == Ton::Mörk ? kMörk : kDjup;
            c             = ljushet(c, d);
            färger.push_back({c, c, false});
            continue;
        }
        if (mono && l != &kontur) {
            c = l->över && !ärYta(l->former) ? p.text
                                              : color::mix(tonad(c, l->ton), mål, kNedtonad);
            färger.push_back({c, c, false});
        } else if (!mono && l->gradient) {
            c = tonad(c, l->ton);
            färger.push_back({ljushet(c, kGradLjus), ljushet(c, -kGradMörk), true});
        } else {
            c = tonad(c, l->ton);
            färger.push_back({c, c, false});
        }
    }
    // En kontur i en TON är ytans kant: nederkanten skuggas.
    const bool skuggad = !mono && kontur.roll != Roll::Text && kontur.ton != Ton::Bas;
    const float h      = a.streck / 2.0f;
    const auto iNågot  = [&](float x, float y, float hh) {
        for (const Lager* l : ordning)
            if (inne(l->former, x, y, hh))
                return true;
        return false;
    };
    // HÖGDAGERN på en gradientyta: dess översta rad — punkten en px ovanför
    // ligger utanför ytan, eller i konturen där den är ytterkant (ingenting
    // ett streck ovanför). En linje inne i ytan (pilen på skivan) ger ingen.
    const auto glans = [&](const Lager& l, float x, float y) {
        if (!inne(l.former, x, y - 1.0f, h))
            return true;
        return inne(kontur.former, x, y - 1.0f, h) &&
               !iNågot(x, y - 1.0f - a.streck, h);
    };

    // Proven i `kProv` gånger målets upplösning, ned till målet med
    // programikonens skalare (ytans medelvärde i förmultiplicerad rymd, så
    // två färger möts utan en mörk söm).
    const int       n = px * kProv;
    glzassets::Rgba stor;
    stor.width  = n;
    stor.height = n;
    stor.px.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 4);
    const float   enhet = 1.0f / static_cast<float>(kProv);   // px per prov
    std::uint8_t* ut    = stor.px.data();
    for (int y = 0; y < n; ++y) {
        const float uy = (static_cast<float>(y) + 0.5f) * enhet;
        for (int x = 0; x < n; ++x, ut += 4) {
            const float ux = (static_cast<float>(x) + 0.5f) * enhet;
            if (iHål(kontur.former, ux, uy))
                continue;   // utstansat: genomskinligt i alla lager
            for (std::size_t k = 0; k < ordning.size(); ++k)
                if (inne(ordning[k]->former, ux, uy, h)) {
                    const Färgning& f = färger[k];
                    const Lager&    l = *ordning[k];
                    gfx::Color      c = f.a;
                    if (f.gradient) {
                        const float t = ((1.0f - kGradLodrätt) * ux + kGradLodrätt * uy) /
                                        a.sida;
                        c = color::mix(f.a, f.b, std::clamp(t, 0.0f, 1.0f));
                        if (glans(l, ux, uy))
                            c = ljushet(c, kGlans);
                    }
                    if (skuggad && &l == &kontur) {
                        if (!iNågot(ux, uy + 1.0f, h))
                            c = color::mix(c, kSvart, kSkugga);
                        else if (!iNågot(ux, uy - 1.0f, h) || !iNågot(ux - 1.0f, uy, h))
                            c = ljushet(c, kKantLjus);
                    }
                    ut[0] = c.r;
                    ut[1] = c.g;
                    ut[2] = c.b;
                    ut[3] = c.a;
                    break;
                }
        }
    }
    return glzassets::resizeRgba(stor, px, px);
}

// En beskrivning delad i LAGER vid `|` (tom = inga).
std::vector<std::string> lagerdelar(const char* text)
{
    std::vector<std::string> ut;
    if (!text)
        return ut;
    std::string del;
    for (const char* c = text;; ++c) {
        if (*c == '|' || *c == '\0') {
            ut.push_back(del);
            del.clear();
            if (*c == '\0')
                break;
        } else {
            del += *c;
        }
    }
    return ut;
}

using Lagerlista = std::vector<std::pair<std::string, std::string>>;

// Konturens och färgens lager parvis (`|` skiljer lager åt i båda, i samma
// ordning) efter de som redan står i `ut`.
void läggTillLager(const char* kontur, const char* färg, Lagerlista& ut)
{
    const std::vector<std::string> konturer = lagerdelar(kontur);
    const std::vector<std::string> färger   = lagerdelar(färg);
    for (std::size_t i = 0; i < konturer.size(); ++i)
        ut.emplace_back(konturer[i], i < färger.size() ? färger[i] : std::string());
}

// IKONEN — upp till fyra lager, nerifrån och upp. Varje lager ritas fritt
// (`ritaLager`) och läggs på det som redan ritats med sitt kontrastkrav
// (`läggPå`). EN väg för standardikonerna och appens.
Bitmap rastrera(const Lagerlista& lager, int px, IconStyle style,
                const Theme::Colors& p)
{
    glzassets::Rgba bild;
    bild.width  = px;
    bild.height = px;
    bild.px.assign(static_cast<std::size_t>(px) * static_cast<std::size_t>(px) * 4, 0);
    float krav  = kKantKontrast;
    bool  linje = false;
    for (std::size_t i = 0; i < std::min(lager.size(), kMaxLager); ++i) {
        const std::string& f = lager[i].second;
        glzassets::Rgba    b = ritaLager(lager[i].first.c_str(), f.empty() ? nullptr : f.c_str(),
                                         px, style, p, i == 0, krav, linje);
        läggPå(bild, b, p, krav, linje);
    }
    return Bitmap(px, px, bild.px.data());
}

// En STANDARDIKONS lager: dess rad i `kDefs`, i samma form som appens.
Lagerlista standardLager(StockIcon icon)
{
    const Def& d = kDefs[static_cast<int>(icon)];
    Lagerlista lager;
    läggTillLager(d.kontur, d.färg, lager);
    return lager;
}

std::uint32_t packa(gfx::Color c)
{
    return (std::uint32_t{c.r} << 24) | (std::uint32_t{c.g} << 16) |
           (std::uint32_t{c.b} << 8) | std::uint32_t{c.a};
}

// Cachens färger: båda stilarna läser alla — mono tonar ned samma ytor —, och
// kanten mäts mot ytorna ikonen står på.
std::array<std::uint32_t, 10> färgnyckel(const Theme::Colors& c)
{
    return {packa(c.text),       packa(c.windowBg),   packa(c.panel),
            packa(c.titlebar),   packa(c.stockBlue),  packa(c.stockGreen),
            packa(c.stockRed),   packa(c.stockAmber), packa(c.stockYellow),
            packa(c.stockPaper)};
}

// Appens ikoner har en EGEN cache, som töms när appen registrerar något — en
// ikon eller en del den använder kan ha ändrats.
using AppNyckel = std::tuple<std::string, int, bool, std::array<std::uint32_t, 10>>;
std::map<AppNyckel, Bitmap>& appCache()
{
    static std::map<AppNyckel, Bitmap> cache;
    return cache;
}

// Namnen som redan har loggats som okända, så en ritning per bildruta inte
// fyller loggen.
std::vector<std::string>& okändaLoggade()
{
    static std::vector<std::string> v;
    return v;
}

bool registrera(std::vector<AppRecept>& v, const char* vad, std::string_view namn,
                std::string_view kontur, std::string_view färg)
{
    if (namn.empty() || kontur.empty()) {
        gfx::log("%s: tomt namn eller tom kontur (\"%.*s\") — registreras inte", vad,
                 static_cast<int>(namn.size()), namn.data());
        return false;
    }
    const std::size_t lager = static_cast<std::size_t>(
        std::count(kontur.begin(), kontur.end(), '|') + 1);
    if (lager > kMaxLager)
        gfx::log("%s \"%.*s\": %zu lager — de fyra första ritas", vad,
                 static_cast<int>(namn.size()), namn.data(), lager);
    // Ett nytt recept (eller en ny del) kan ha nya fel: de loggas igen.
    appCache().clear();
    felLoggade().clear();
    AppRecept r{std::string(namn), std::string(kontur), std::string(färg)};
    for (AppRecept& f : v)
        if (f.namn == r.namn) {
            f = std::move(r);
            return true;
        }
    v.push_back(std::move(r));
    std::erase(okändaLoggade(), std::string(namn));
    return true;
}

// `Auto` utan ett tillstånd är den aktiva bilden.
bool färgad(IconStyle style)
{
    return style != IconStyle::Mono;
}

} // namespace

ImageSource stockIcon(StockIcon icon, int sizeClass, IconStyle style)
{
    if (sizeClass <= 0) {
        gfx::log("stockIcon: storleksklassen %d är inte positiv — 16 används",
                 sizeClass);
        sizeClass = 16;
    }
    return ImageSource(icon, std::string(), sizeClass, style);
}

ImageSource drawnIcon(std::string_view name, int sizeClass, IconStyle style)
{
    if (sizeClass <= 0) {
        gfx::log("drawnIcon: storleksklassen %d är inte positiv — 16 används",
                 sizeClass);
        sizeClass = 16;
    }
    if (name.empty()) {
        gfx::log("drawnIcon: tomt namn — ingen bild");
        return ImageSource{};
    }
    return ImageSource(StockIcon{}, std::string(name), sizeClass, style);
}

bool registerIcon(std::string_view name, std::string_view shapes,
                  std::string_view colors)
{
    return registrera(appIkoner(), "registerIcon", name, shapes, colors);
}

bool registerIconPart(std::string_view name, std::string_view shapes,
                      std::string_view colors)
{
    return registrera(appDelar(), "registerIconPart", name, shapes, colors);
}

std::vector<std::string> drawnIconNames()
{
    std::vector<std::string> ut;
    for (const AppRecept& r : appIkoner())
        ut.push_back(r.namn);
    return ut;
}

int stockIconCount()
{
    return kAntal;
}

std::string_view stockIconName(StockIcon icon)
{
    const int i = static_cast<int>(icon);
    return i >= 0 && i < kAntal ? std::string_view(kDefs[i].name)
                                : std::string_view();
}

std::optional<StockIcon> stockIconByName(std::string_view name)
{
    for (const Def& d : kDefs)
        if (name == d.name)
            return d.icon;
    return std::nullopt;
}

namespace detail {

Bitmap stockBitmap(StockIcon icon, int px, IconStyle style,
                   const Theme::Colors& colors)
{
    const int i = static_cast<int>(icon);
    if (i < 0 || i >= kAntal || px <= 0)
        return Bitmap{};
    px = std::min(px, kMaxPx);
    // EN bild per (ikon, px, stil, färgerna): fönstrens texturer delar den,
    // och en ny färg (ett temabyte) är en ny bild.
    const bool färg = färgad(style);
    using Nyckel    = std::tuple<int, int, bool, std::array<std::uint32_t, 10>>;
    static std::map<Nyckel, Bitmap> cache;
    const Nyckel nyckel{i, px, färg, färgnyckel(colors)};
    auto it = cache.find(nyckel);
    if (it == cache.end()) {
        ritasNu() = kDefs[i].name;
        it = cache.emplace(nyckel, rastrera(standardLager(icon), px,
                                            färg ? IconStyle::Color : IconStyle::Mono,
                                            colors))
                 .first;
    }
    return it->second;
}

Bitmap drawnBitmap(const std::string& name, int px, IconStyle style,
                   const Theme::Colors& colors)
{
    if (px <= 0)
        return Bitmap{};
    const AppRecept* r = hittaApp(appIkoner(), name);
    if (!r) {
        auto& loggade = okändaLoggade();
        if (std::find(loggade.begin(), loggade.end(), name) == loggade.end()) {
            gfx::log("drawnIcon: ikonen \"%s\" är inte registrerad — StockIcon::Image "
                     "ritas i dess ställe",
                     name.c_str());
            loggade.push_back(name);
        }
        return stockBitmap(StockIcon::Image, px, style, colors);
    }
    px              = std::min(px, kMaxPx);
    const bool färg = färgad(style);
    const AppNyckel nyckel{name, px, färg, färgnyckel(colors)};
    auto&           cache = appCache();
    auto            it    = cache.find(nyckel);
    if (it == cache.end()) {
        ritasNu() = name;
        Lagerlista lager;
        läggTillLager(r->kontur.c_str(), r->färg.c_str(), lager);
        it = cache.emplace(nyckel, rastrera(lager, px,
                                            färg ? IconStyle::Color : IconStyle::Mono,
                                            colors))
                 .first;
    }
    return it->second;
}

std::vector<std::string> iconErrorNames()
{
    return felLoggade();
}

Bitmap iconBitmap(const ImageSource& s, int px, const Theme::Colors& colors)
{
    if (s.kind() != ImageSource::Kind::Icon)
        return Bitmap{};
    return s.drawnName().empty()
               ? stockBitmap(s.stock(), px, s.iconStyle(), colors)
               : drawnBitmap(s.drawnName(), px, s.iconStyle(), colors);
}

} // namespace detail
} // namespace glaze
