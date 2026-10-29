// listview_gate — LISTVYNS grind.
//
// Kör:  ./build/tests/listview_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// TVÅDELAD:
//
//   HEADLESS     kollektionsvokabulären (index utanför ⇒ no-op), SORTERINGEN
//                (naturlig, stabil, båda riktningarna, nycklar, indexen är
//                appens), VALET (enkel-/flerval, användare mot program,
//                spannet i VISNINGSordningen), KORTNINGEN (`Draw::elide` mot
//                en falsk font med känt teckensteg) och RITNINGEN mot en
//                inspelande renderer (vilka rader som ritas, kortade celler,
//                sorteringspilen) — inga pixlar. Även
//                KOLUMNHUVUDET PÅ FLERA RADER: rubrikens rader
//                (`Draw::elideLines` — `\n`, ordgräns, det för breda ordet,
//                ellipsen, åäö, och att en rad utan `\n` är exakt `elide`),
//                huvudets HÖJD för 1/2/4 rader och AUTO (rad 0 under det,
//                underlinjen, avdelarna; AUTO följer en ny bredd, en ny
//                rubrik och ett språkbyte), och JUSTERINGEN — rubrikens och
//                cellernas Left/Center/Right per kolumn, Top/Center/Bottom i
//                ett huvud på fyra rader och sorteringspilens plats — som
//                TEXTENS och STRECKENS koordinater i den inspelande
//                renderern, mot förväntade värden ur kontraktet.
//   MED FÖNSTER  ett DOLT fönster (`setOffscreen`), och händelserna går genom
//                fönstrets RIKTIGA ingång (`AppWindow::handleEvent`, som i
//                dnd_gate): klick på en rad, i huvudet (sortering, och
//                INTE när släppet sker över en annan rubrik), dragning i en
//                avdelare (bredd + minsta bredd), tangenterna, DUBBELKLICKET
//                (ramverkets räkning — två snabba, ett tredje, ett långsamt),
//                Ctrl-/Shift-klick genom musens modifierare, hjulet och
//                rullningsytan under huvudet — och F11 samma sak
//                under ett huvud på fyra rader: rubrikklick och avdelardrag
//                i huvudets NEDRE del, rad 0 under det och spelrummet.
//
// RADENS FÄRG — ritad på en omarkerad rads
// alla celler i alla tre UI, markeringens färg på en vald rad, `textDim`
// inaktivt, färgen FÖLJER RADEN genom en sortering, `setRow` utan färg
// nollar och `setCell` lämnar den, massformen positionellt och designerat
// (C1–C5) — en BITMAP som radens ikon (C6, en riktig bildcache med en
// falsk bild) och i fönstret SOLO VID SLÄPPET: ett tryck på en vald rad i
// flerval väljer ensamt först vid släppet (F12), och det väntande valet
// nollas av `setRows`/`setRow` och av ett drag (F13).
//
// KRYSSEN — headless K1 (settrarna, utanför ⇒
// no-op, `setChecked` fyrar aldrig, `setProp`), K2 (`addRow` okryssat,
// `setRow` behåller, `setRows` nollar och släpper `changedRow`,
// `checkedRows` i visningsordning), K3 (krysset följer raden genom en
// sortering — bocken på den nya platsen) och K4 i alla tre UI (texten efter
// rutan, bocken i temats färg och aldrig i radens, markeringens färg på en
// vald rad, `textDim` inaktivt, inga streck på en okryssad rad); i fönstret
// F14 (zonens klick: onCheck + onCommit med raden, valet orört; textens klick
// väljer och ger −1), F15 (Space kryssar också i enkelval, Ctrl+Space vänder
// valet bara i flerval), F16 (dubbelklick i zonen = två vändningar, ingen
// aktivering), F17 (inget soloval vid ett kryssklick), F18 (`canDragFrom`:
// zonen och en avdelare blir inget drag fast appen fyller lasten, texten
// blir det), F19 (zonen följer sidledsrullningen) och F20 (zonen vänder
// RADEN på en sorterad plats).
//
// GEOMETRIN MÄTS MOT EN OBEROENDE REFERENS: klickpunkterna räknas i grinden ur
// KONTRAKTET (ramens indrag `panelInset`, huvudets `tableHeaderH`, radens
// `listRowH`, kolumnbredderna) — aldrig ur widgetens egna `rowRect`/
// `cellRect`. Hade grinden frågat widgeten var rad 3 ligger och därefter klickat
// där, hade den varit grön oavsett var raderna ritas.
//
// SABOTAGEN den fäller (prövade i en privat kopia):
//   * bytevis jämförelse i stället för naturlig (fall S1: "fil10" före "fil2")
//   * fallande riktning utan stabil ordning för lika rader (S2)
//   * valet lagrat per PLATS i stället för per rad (V5: valet följer inte
//     raden efter en sortering)
//   * `Panel::scrollRange` utan `scrollViewport` (F3: spelrummet blir
//     huvudets höjd för litet)
//   * sorteringen i `mouseUp` utan kontrollen "samma rubrik" (F4)
//   * avdelarens greppzon på fel sida (F5)
//   * ramverkets dubbelklick som aldrig slår till (F7)
//   * AppWindow som inte sparar musens modifierare (F8: Ctrl-klick väljer om)
// …och kolumnhuvudets (prövade i en privat kopia):
//   * `headerH` som inte bryr sig om radantalet (H, F11 — 90 fall)
//   * rubriken ritad i CELLERNAS justering (J1)
//   * höjdjusteringen ignorerad (J3, J6)
//   * ordet hårdbrutet som `wrapText` gör (B3, B4, B9)
//   * ingen påtvingad ellips på den sista synliga raden (B5, B10, J4)
//   * sorteringspilen alltid i huvudets mitt (J6)
// …och radfärgens och bildens (skrivna):
//   * `col.text` i stället för `appTextColor` i ritningen (C1, C3, P1)
//   * färgen lagrad per PLATS i stället för per rad (C3)
//   * markeringens färg som inte vinner över radens (C2)
//   * valet satt redan på trycket, utan väntande soloval (F12)
//   * `pendingSolo_` som inte nollas i `rowsReplaced`/`rowReplaced` (F13 —
//     det gamla indexet väljer en ny rad). Nollningen i `dragStart` FÄLLS
//     INTE: ett tryck som blev ett drag får ingen `mouseUp` (AppWindow
//     släpper den tryckta widgeten vid dragstarten), så den är ett skydd som
//     inte syns utifrån — F13 mäter bara att valet står kvar efter draget.
//   * AUTO som bara räknar `\n` (H) · AUTO-cachen som inte släpps vid en
//     ny bredd (H) eller ett språkbyte (H)
//   * huvudets träffyta en rad hög (H, F11)
// …och kryssens (skrivna):
//   * `onCheck` fyrat av `setChecked` (K1)
//   * kryssen lagrade per PLATS (K3, F20)
//   * `setRows` som inte nollar, eller `setRow` som nollar (K2)
//   * `leadW` glömd i `drawCell` (K4: texten på rutan)
//   * bocken i radens färg (K4)
//   * ett kryssklick som flyttar valet eller den aktuella raden (F14)
//   * `changedRow` inte −1 efter ett val (F14)
//   * Space som vänder valet (F15)
//   * dubbelklicket i zonen som aktiverar (F16)
//   * ett väntande soloval vid kryssklicket (F17)
//   * `canDragFrom` saknas — rutan eller huvudet blir ett drag (F18)
//   * zonen utan sidledsrullningen (F19)
//   * en listvy vars `leadW` inte är 0 UTAN rutor (J2: cellens text flyttar).
//     En `Table`-standard ≠ 0 fäller datagrid_gate R6 i stället — listvyn
//     overridar kroken, så J2 ser den inte
// …och cellfärgernas, radrubrikernas och breddens (P, R, A, D — skrivna):
//   * cellens text som inte slår radens (P1) · bakgrunden fylld på fel cell
//     eller i fel rect (P2) · en cellfärg som syns på en VALD rad eller i en
//     INAKTIV tabell (P4, P5) · färgerna lagrade per PLATS i stället för per
//     rad (P6) · `setRow` som inte nollar cellfärgerna, `setCell` som nollar
//     dem, en kolumn som måste finnas (P7)
//   * den fasta kolumnen som rullar med (R5) · `scrollViewport` som inte är
//     smalnad (R3) · `contentExtent` som inte är minskad med remsan (R4: spel-
//     rummet växer) · en rullande cell som ritas under remsan, alltså utan
//     klippet (R6) · remsan utan huvudets yta eller hårlinjer (R7) · träffen
//     som räknar remsan från rullningsytans kant (R8) · valet som tonar remsan
//     (R9)
//   * `columnBaseW` som ignorerar en negativ bredd (A1) · en cache som inte
//     släpps vid setCell/setRow/setRows/setIconSize/setShowChecks (A3, A6) ·
//     sorteringspilens plats som bara reserveras för den sorterade kolumnen
//     (A5) · zonen/ikonen som glöms i kolumn 0 (A6) · det strikta andrummet
//     (en hårlinje) som saknas (alla A/D — de förväntade värdena har det) · dubbelklicket som
//     inte avslutar draget det andra trycket började (D2) eller som sorterar
//     (D3) · en bredd som förblir `autoWidth` efter dubbelklicket (D4, D5)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * hur cellfärgerna och radrubrikerna SER UT (färgerna mäts som
//     inspelade ytor och text, remsans form som ytor och streck — pixel_gate
//     och ögat tar resten), klippet i en RIKTIG renderer (Inspelning läser
//     `clipBounds`, inte pixlar), `ensureColumnVisible` med en fast kolumn
//     (kräver ett öppet fönster), och att ett dubbelklick i ett RIKTIGT fönster
//     når `headerDoubleClick` (D-fallen anropar krokarna direkt, som Q5).
//   * UTSEENDET — färger och former per UI. Att tabellen RITAR något i varje
//     UI × tema × läge mäter `pixel_gate` (galleri-fixturen), och utseendet
//     är granskat renderat i alla tre UI, ljust och mörkt.
//   * backendernas egna mus-events — att Windows/macOS/Wayland/X11 faktiskt
//     levererar `mods` på musens knappar. Grinden matar ingången direkt; det
//     backend-specifika står i doc/os/*/TESTS.md.
//   * OS:ets dubbelklickstid — grinden läser `gfx::doubleClickMs()` och
//     väntar längre än den, men mäter inte att värdet är användarens.
//   * muspekarens FORM i fönstret (`cursorAt` mäts direkt; att AppWindow
//     sätter den på fönstret är splitter_gate:s).
//   * hur kryssrutan SER UT (K4 mäter bockens färg och
//     plats, inte formen; pixel_gate mäter att något ritas), Cmd på macOS och
//     backendernas riktiga mus-events (doc/os/*/TESTS.md).
//   * att `setHeaderLines`/`setHeaderVAlign` INVALIDERAR (regel 1 —
//     en ändring syns i nästa ritning i F11, men damage-ytan mäts inte), att
//     `setHeaderLines` klampar den lagrade scrollen (geometrin klampar ändå
//     vid varje läsning), och den RIKTIGA fontens radbrytning — raderna mäts
//     mot den falska fonten; fönsterfallen läser radhöjden ur
//     `Draw::lineHeight`. Utseendet per UI är granskat renderat.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

#include "image_cache.h"   // intern — en Bitmap-ikon behöver en riktig
                           // bildcache (som i toolbar_gate)

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

std::string fog(const std::vector<int>& v)
{
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? "," : "") + std::to_string(v[i]);
    return s + "}";
}

// Visningsordningen som appens radindex, plats för plats.
std::vector<int> ordning(const glaze::Table& t)
{
    std::vector<int> v;
    for (int p = 0; p < t.rowCount(); ++p)
        v.push_back(t.rowAtDisplay(p));
    return v;
}

// De skyddade vägarna anropbara — det en subklass får göra.
struct Prov : glaze::ListView {
    using glaze::ListView::selectFromUser;
    using glaze::ListView::activateFromUser;
    using glaze::ListView::toggleCheckFromUser;
    using glaze::ListView::draw;
    using glaze::Table::cursorAt;
    using glaze::Table::languageChanged;
    using glaze::Panel::scrollRange;
    using glaze::Panel::inScrollStrip;
    // Fyllkolumnens och ikonstorlekens geometri.
    using glaze::Table::columnW;
    using glaze::Table::headerLineCount;
    using glaze::Table::iconSide;
    using glaze::Table::rowH;
    // Radrubrikens geometri och träff, bredden efter innehållet och
    // dubbelklicket på avdelaren.
    using glaze::Table::cellRect;
    using glaze::Table::frozenW;
    using glaze::Table::hitPosition;
    using glaze::ColumnPanel::fittedW;
    using glaze::ColumnPanel::scrollViewport;
    using glaze::ListView::mouseDown;
    using glaze::ListView::mouseUp;
    using glaze::ListView::mouseDoubleClick;
    using glaze::ColumnPanel::mouseDrag;

    int ändringar   = 0;
    int aktiveringar = 0;
    Prov()
    {
        onChange   = [this] { ++ändringar; };
        onActivate = [this] { ++aktiveringar; };
    }
};

// ------------------------------------------------------ falsk font
//
// Fast teckensteg PER KODPUNKT i alla fyra mätningarna (measure_gate har
// ett bytesteg i prefixWidth, och kortningen ska mätas på åäö — två byte
// men ETT tecken). Talar FYSISKA px: steget skalas med Ctx:ts skala.
class StegFont final : public gfx::NullFont {
public:
    explicit StegFont(float steg) : steg_(steg) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{steg_ * float(tecken(s, s.size())), steg_ * 2.0f};
    }
    float height() const override { return steg_ * 2.0f; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return steg_ * float(tecken(s, std::min(bytes, s.size())));
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        return fitWidth(s, x + steg_ / 2.0f);
    }
    // Hela tecken som ryms inom maxW — svaret är alltid en teckengräns.
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size()) {
            std::size_t j = i + 1;
            while (j < s.size() &&
                   (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80)
                ++j;
            if (w + steg_ > maxW)
                break;
            w += steg_;
            i = j;
        }
        return i;
    }

private:
    static std::size_t tecken(const std::string& s, std::size_t bytes)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < bytes; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                ++n;
        return n;
    }
    float steg_;
};

// Bild som bara är ett mått — fönstrets cache får en sådan av renderern
// när den laddar upp en Bitmap (som i toolbar_gate).
struct FalskBild final : gfx::NullImage {
    int  width() const override { return 16; }
    int  height() const override { return 16; }
    bool valid() const override { return true; }
};

// Renderer som SPELAR IN texten den ombeds rita och räknar linjerna —
// resten är NullRendererns no-op. Med PLATSERNA i fysiska
// px (textens övre vänstra hörn, linjernas ändpunkter): justeringen och
// huvudets geometri mäts mot förväntade värden ur kontraktet, inte bara närvaron.
// Också textens FÄRG (radens färg) och bilderna
// med målrect (en Bitmap-ikon laddas upp genom `createImage`).
// Strecken med FÄRG (tree_gate-mönstret) — kryssrutans bock
// är två streck, och den får aldrig ritas i radens app-färg.
struct Inspelning : gfx::NullRenderer {
    // Också KLIPPET texten ritades under (`harKlipp`/`klipp`,
    // `Renderer::clipBounds`) och de fyllda ytorna med färg och klipp: en
    // cellbakgrund och radrubrikens remsa mäts som ytor, och att en rullande
    // cell aldrig ritas under remsan mäts som klippet.
    struct Text {
        std::string s;
        float       x = 0.0f, y = 0.0f;
        gfx::Color  färg{};
        bool        harKlipp = false;
        gfx::Rectf  klipp{};
    };
    struct Streck {
        float      x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
        gfx::Color färg{};
    };
    struct Yta {
        gfx::Rectf r{};
        gfx::Color färg{};
        bool       harKlipp = false;
        gfx::Rectf klipp{};
    };
    std::vector<std::string> texter;
    std::vector<Text>        platser;
    std::vector<Streck>      streck;
    std::vector<gfx::Rectf>  bilder;
    std::vector<Yta>         ytor;
    int                      linjer = 0;
    void fillRoundedRect(const gfx::Rectf& r, float, gfx::Color c) override
    {
        Yta y{r, c};
        y.harKlipp = clipBounds(y.klipp);
        ytor.push_back(y);
    }
    // Första ytan i färgen `c` — nullptr om ingen ritades. `ytorMed` ger alla.
    const Yta* yta(gfx::Color c) const
    {
        for (const Yta& y : ytor)
            if (y.färg == c)
                return &y;
        return nullptr;
    }
    int antalYtor(gfx::Color c) const
    {
        int n = 0;
        for (const Yta& y : ytor)
            n += y.färg == c ? 1 : 0;
        return n;
    }
    void line(float x1, float y1, float x2, float y2, gfx::Color c) override
    {
        ++linjer;
        streck.push_back({x1, y1, x2, y2, c});
    }
    // Streck HELT inom rutan `r` — i färgen `c`, eller i vilken färg som helst.
    int streckI(gfx::Color c, const gfx::Rectf& r) const
    {
        int n = 0;
        for (const Streck& l : streck)
            n += l.färg == c && r.contains(l.x1, l.y1) && r.contains(l.x2, l.y2)
                     ? 1
                     : 0;
        return n;
    }
    int streckI(const gfx::Rectf& r) const
    {
        int n = 0;
        for (const Streck& l : streck)
            n += r.contains(l.x1, l.y1) && r.contains(l.x2, l.y2) ? 1 : 0;
        return n;
    }
    int streckI(gfx::Color c) const
    {
        int n = 0;
        for (const Streck& l : streck)
            n += l.färg == c ? 1 : 0;
        return n;
    }
    float drawText(gfx::Font&, float x, float y, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back(s);
        Text t{s, x, y, c};
        t.harKlipp = clipBounds(t.klipp);
        platser.push_back(t);
        return 0.0f;
    }
    using gfx::NullRenderer::drawImage;
    void drawImage(gfx::Image&, const gfx::Rectf&, const gfx::Rectf& dst,
                   float) override
    {
        bilder.push_back(dst);
    }
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int, int, gfx::ImageFormat,
                                            const void*, std::size_t,
                                            bool) override
    {
        return std::make_unique<FalskBild>();
    }
    void rensa()
    {
        texter.clear();
        platser.clear();
        streck.clear();
        bilder.clear();
        ytor.clear();
        linjer = 0;
    }
    // Färgen texten `s` ritades i — nullopt om den aldrig ritades.
    std::optional<gfx::Color> färgPå(const std::string& s) const
    {
        for (const Text& t : platser)
            if (t.s == s)
                return t.färg;
        return std::nullopt;
    }
    // Texten `s` — första förekomsten, nullptr om den aldrig ritades.
    const Text* text(const std::string& s) const
    {
        for (const Text& t : platser)
            if (t.s == s)
                return &t;
        return nullptr;
    }
    // Ett LODRÄTT streck vid x från y0 till y1 (åt endera hållet)?
    bool lodratt(float x, float y0, float y1) const
    {
        for (const Streck& l : streck)
            if (std::abs(l.x1 - x) < 0.01f && std::abs(l.x2 - x) < 0.01f &&
                std::abs(std::min(l.y1, l.y2) - y0) < 0.01f &&
                std::abs(std::max(l.y1, l.y2) - y1) < 0.01f)
                return true;
        return false;
    }
    // Ett VÅGRÄTT streck vid y från x0 till x1?
    bool vagratt(float y, float x0, float x1) const
    {
        for (const Streck& l : streck)
            if (std::abs(l.y1 - y) < 0.01f && std::abs(l.y2 - y) < 0.01f &&
                std::abs(std::min(l.x1, l.x2) - x0) < 0.01f &&
                std::abs(std::max(l.x1, l.x2) - x1) < 0.01f)
                return true;
        return false;
    }
};

glaze::Ctx ctxFör(gfx::Font& f, gfx::Renderer* r, float scale,
                  const gfx::Rectf& area)
{
    glaze::Ctx c{};
    c.r           = r;
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = scale;
    c.ui          = glaze::UI::Slate;
    c.style       = &glaze::defaultStyle(glaze::UI::Slate);
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = area;
    return c;
}

bool har(const std::vector<std::string>& v, const std::string& s)
{
    for (const std::string& x : v)
        if (x == s)
            return true;
    return false;
}

std::string fog(const std::vector<std::string>& v)
{
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? ", \"" : "\"") + v[i] + "\"";
    return s + "}";
}

bool nara(float a, float b) { return std::abs(a - b) < 0.01f; }

std::string tal(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

const std::string kEll = "\xE2\x80\xA6";   // ellipsen

// ============================================================== HEADLESS

void fallRegistret()
{
    auto w = glaze::makeWidget("listview");
    check(w && dynamic_cast<glaze::ListView*>(w.get()) != nullptr,
          "registret: \"listview\" ger en riktig glaze::ListView");
    check(w && dynamic_cast<glaze::Table*>(w.get()) != nullptr &&
              dynamic_cast<glaze::Panel*>(w.get()) != nullptr,
          "listvyn är en Table och en Panel (clip/border/scroll ärvs)");
    glaze::ListView l;
    check(l.focusable(), "listvyn är ETT tabbstopp (focusable)");
    check(l.isSortable() && l.sortColumn() == -1 && l.isSortAscending() &&
              !l.isMultiSelect() && l.selectedIndex() == -1 &&
              l.currentRow() == -1,
          "default: sorterbar, ingen sortering, stigande, enkelval, inget val");
}

void fallSetProp()
{
    glaze::ListView l;
    l.addColumn("A");
    l.addColumn("B");
    for (int i = 0; i < 4; ++i)
        l.addRow({std::to_string(i)});
    check(l.setProp("multiSelect", "true") && l.isMultiSelect(),
          "setProp: multiSelect");
    check(l.setProp("sortColumn", "1") && l.sortColumn() == 1,
          "setProp: sortColumn");
    check(l.setProp("sortAscending", "false") && !l.isSortAscending(),
          "setProp: sortAscending");
    check(l.setProp("sortable", "false") && !l.isSortable(),
          "setProp: sortable");
    check(l.setProp("selectedIndex", "2") && l.selectedIndex() == 2,
          "setProp: selectedIndex", std::to_string(l.selectedIndex()));
    check(!l.setProp("dettaFinnsInte", "1"),
          "setProp: okänd nyckel svarar false (opak rad)");
    check(l.setProp("border", "false") && l.setProp("visible", "true"),
          "setProp: Panels och Widgets nycklar når basen (anropad SIST)");
}

void fallKollektionen()
{
    glaze::ListView l;
    l.addColumn("Namn", 120);
    l.addColumn("Storlek", 70, glaze::Align::Right);
    check(l.columnCount() == 2 && l.columns()[1].width == 70.0f &&
              l.columns()[1].align == glaze::Align::Right &&
              l.columns()[0].text.str() == "Namn",
          "addColumn: rubrik, bredd och justering");
    l.setColumn(0, "Fil");
    check(l.columns()[0].text.str() == "Fil" && l.columns()[0].width == 0.0f,
          "setColumn ERSÄTTER hela kolumnen (utelämnad bredd ⇒ standard)");
    l.setColumn(5, "X");
    l.setColumn(-1, "X");
    check(l.columnCount() == 2, "setColumn utanför: no-op (ingen tillväxt)");
    l.setColumnWidth(1, 90);
    check(l.columnWidth(1) == 90.0f, "setColumnWidth sätter bredden");
    l.setColumnWidth(7, 90);
    check(l.columnWidth(7) == 0.0f && l.columnCount() == 2,
          "setColumnWidth utanför: no-op");
    l.setColumns({{"A", 10}, {"B"}, {glaze::tr(1), 30, glaze::Align::Center}});
    check(l.columnCount() == 3 && l.columns()[2].text.id() == 1 &&
              l.columns()[2].align == glaze::Align::Center,
          "setColumns: massformen, rubriken kan vara ett översättnings-id");

    l.addRow({"a", "1"});
    l.addRow({"b"});
    check(l.rowCount() == 2 && l.cell(0, 1) == "1" && l.cell(1, 1).empty(),
          "addRow: celler; en kort rad svarar tomt för kolumnen den saknar");
    l.setRow(1, {"B", "2"});
    check(l.cell(1, 0) == "B" && l.cell(1, 1) == "2", "setRow ersätter raden");
    l.setRow(9, {"x"});
    l.setRow(-1, {"x"});
    check(l.rowCount() == 2, "setRow utanför: no-op (ingen tillväxt)");
    l.setCell(0, 4, "fem");
    check(l.cell(0, 4) == "fem" && l.rows()[0].cells.size() == 5,
          "setCell bortom radens celler fyller ut med tomma");
    l.setCell(7, 0, "x");
    check(l.rowCount() == 2, "setCell på en rad som inte finns: no-op");
    check(l.cell(-1, 0).empty() && l.cell(0, -1).empty() &&
              l.cell(99, 0).empty(),
          "cell() utanför tabellen svarar en TOM sträng");
    l.setRows({{{"x", "y"}}, {{"z"}}, {}});
    check(l.rowCount() == 3 && l.cell(0, 1) == "y" && l.cell(2, 0).empty(),
          "setRows: massformen, `{}` är en tom rad");
    l.setRows({});
    check(l.rowCount() == 0, "setRows({}) tömmer");
}

void fallSorteringen()
{
    glaze::ListView l;
    l.addColumn("Namn");
    l.addColumn("Storlek");
    // rad:     0        1         2       3           4
    l.addRow({"fil10", "12 kB"});
    l.addRow({"fil2", "1,2 MB"});
    l.addRow({"Fil1", "900 B"});
    l.addRow({"b", "1 kB"});
    l.addRow({"B", "1 kB"});

    check(ordning(l) == std::vector<int>{0, 1, 2, 3, 4},
          "S0: utan sortering står raderna i appens ordning", fog(ordning(l)));

    l.setSortColumn(0);
    check(ordning(l) == std::vector<int>{3, 4, 2, 1, 0},
          "S1: NATURLIG stigande — b/B (lika, appens ordning), Fil1, fil2, "
          "fil10", fog(ordning(l)));
    l.setSortAscending(false);
    check(ordning(l) == std::vector<int>{0, 1, 2, 3, 4},
          "S2: fallande — och de LIKA b/B behåller appens ordning ändå",
          fog(ordning(l)));
    l.setSortAscending(true);

    // NYCKLAR: storleken sorteras på bytes, inte på texten.
    l.setSortColumn(1);
    l.setSortKey(0, 1, 12288);
    l.setSortKey(1, 1, 1258291);
    l.setSortKey(2, 1, 900);
    check(ordning(l) == std::vector<int>{2, 0, 1, 3, 4},
          "S3: nycklar numeriskt (900 B, 12 kB, 1,2 MB) — en cell MED nyckel "
          "före de utan", fog(ordning(l)));
    l.setSortKey(3, 1, 1024);
    l.setSortKey(4, 1, 1024);
    check(ordning(l) == std::vector<int>{2, 3, 4, 0, 1},
          "S4: lika nycklar behåller appens ordning", fog(ordning(l)));

    // INDEXEN ÄR APPENS: setCell/setRow träffar samma rad efter sorteringen.
    l.setCell(2, 0, "ändrad");
    check(l.cell(2, 0) == "ändrad" && l.rowAtDisplay(0) == 2,
          "S5: setCell(2, …) träffar RAD 2 — inte platsen 2");
    l.setCell(2, 1, "inget mer");
    check(ordning(l) == std::vector<int>{3, 4, 0, 1, 2},
          "S6: setCell släpper cellens NYCKEL (raden sorteras på texten nu)",
          fog(ordning(l)));
    l.setRow(0, {"fil10", "12 kB"});
    check(ordning(l) == std::vector<int>{3, 4, 1, 0, 2},
          "S7: setRow släpper radens nycklar", fog(ordning(l)));
    l.setSortKey(1, 1, std::numeric_limits<double>::quiet_NaN());
    // Rad 1 bär nu texten "1,2 MB" utan nyckel, och naturligt är "1" < "12".
    check(ordning(l) == std::vector<int>{3, 4, 1, 0, 2},
          "S8: en NaN-nyckel räknas som INGEN nyckel (sorteringen förblir "
          "definierad)", fog(ordning(l)));

    // ORDNINGSOBEROENDE: en sortering som står före kolumnerna gäller när de
    // kommer — satserna i en `.glazeui` körs i källordning.
    glaze::ListView m;
    m.setSortColumn(1);
    m.setSortAscending(false);
    m.addRow({"x", "1"});
    m.addRow({"y", "3"});
    m.addRow({"z", "2"});
    check(ordning(m) == std::vector<int>{0, 1, 2},
          "S9: sorteringskolumnen finns inte än — ingen sortering");
    m.addColumn("A");
    m.addColumn("B");
    check(ordning(m) == std::vector<int>{1, 2, 0},
          "S10: …och när kolumnen kommer gäller den", fog(ordning(m)));
    check(m.displayRow(0) == 2 && m.displayRow(1) == 0 &&
              m.displayRow(-1) == -1 && m.rowAtDisplay(9) == -1,
          "S11: displayRow/rowAtDisplay är varandras invers, −1 utanför");
}

void fallValet()
{
    Prov l;
    l.addColumn("A");
    for (int i = 0; i < 6; ++i)
        l.addRow({std::string(1, char('f' - i))});   // f e d c b a

    // PROGRAMMATISKT fyrar aldrig.
    l.setSelectedIndex(2);
    l.setSelected(3, true);
    check(l.ändringar == 0 && l.selectedIndex() == 3 && !l.isSelected(2),
          "V1: programmatiskt — inget event, och enkelval väljer bort de andra");

    // ANVÄNDAREN fyrar exakt en gång per ändring.
    l.selectFromUser(0, 0, false);
    check(l.ändringar == 1 && l.selectedIndex() == 0 && l.currentRow() == 0,
          "V2: klick väljer raden på platsen — EXAKT ett onChange");
    l.selectFromUser(0, 0, false);
    check(l.ändringar == 1, "V2: samma rad igen — ingen ändring, inget event");
    l.selectFromUser(1, gfx::kModCtrl, false);
    check(l.selectedRows() == std::vector<int>{1},
          "V3: Ctrl utan flerval väljer om (modifieraren betyder ingenting)",
          fog(l.selectedRows()));

    l.setMultiSelect(true);
    l.selectFromUser(1, 0, false);
    l.selectFromUser(3, gfx::kModCtrl, false);
    check(l.selectedRows() == std::vector<int>{1, 3},
          "V4: flerval — Ctrl+klick VÄNDER raden", fog(l.selectedRows()));
    l.selectFromUser(3, gfx::kModCmd, false);
    check(l.selectedRows() == std::vector<int>{1},
          "V4: Cmd vänder också (macOS)", fog(l.selectedRows()));

    // SORTERA — valet följer RADEN, spannet går i VISNINGSORDNINGEN.
    l.setSortColumn(0);   // a b c d e f  ⇒ platserna visar rad 5 4 3 2 1 0
    check(ordning(l) == std::vector<int>{5, 4, 3, 2, 1, 0},
          "V5: sorterad", fog(ordning(l)));
    check(l.selectedRows() == std::vector<int>{1} && l.isSelected(1),
          "V5: valet följer RADEN genom sorteringen", fog(l.selectedRows()));
    l.selectFromUser(4, 0, false);                // rad 1 (plats 4) = ankaret
    l.selectFromUser(1, gfx::kModShift, false);   // plats 1 = rad 4
    check(l.selectedRows() == std::vector<int>{4, 3, 2, 1},
          "V6: Shift+klick — spannet plats 1…4 i VISNINGSORDNINGEN",
          fog(l.selectedRows()));
    const int före = l.ändringar;
    l.selectFromUser(5, gfx::kModShift | gfx::kModCtrl, false);
    check(l.selectedRows() == std::vector<int>{4, 3, 2, 1, 0} &&
              l.ändringar == före + 1,
          "V7: Ctrl+Shift+klick LÄGGER TILL spannet", fog(l.selectedRows()));
    l.selectFromUser(2, gfx::kModCtrl, true);   // Ctrl+pil
    check(l.currentRow() == 3 && l.selectedRows().size() == 5 &&
              l.ändringar == före + 1,
          "V8: Ctrl+tangent flyttar BARA den aktuella raden");
    check(l.selectedIndex() == 3,
          "V9: selectedIndex = den aktuella när den är vald",
          std::to_string(l.selectedIndex()));
    l.selectFromUser(2, gfx::kModCtrl, false);   // vänd bort rad 3
    check(!l.isSelected(3) && l.selectedIndex() == 4,
          "V9: …annars den första valda i visningsordningen",
          std::to_string(l.selectedIndex()));

    l.setMultiSelect(false);
    check(l.selectedRows().size() == 1,
          "V10: flervalet av — bara en vald rad står kvar",
          fog(l.selectedRows()));

    l.setMultiSelect(true);
    l.selectAll();
    check(l.selectedRows().size() == 6, "V11: selectAll (med flerval)");
    l.clearSelection();
    check(l.selectedRows().empty() && l.selectedIndex() == -1,
          "V11: clearSelection");
    l.addRow({"g"});
    check(!l.isSelected(6), "V12: en ny rad är aldrig vald");
    l.setSelected(6, true);
    l.setRows({{{"x"}}, {{"y"}}});
    check(l.selectedRows().empty() && l.currentRow() == -1,
          "V12: setRows nollar valet och den aktuella raden");
    l.setSelected(5, true);
    check(l.selectedRows().empty(), "V12: setSelected utanför: no-op");

    // AKTIVERINGEN — bara med en aktuell rad, aldrig programmatiskt.
    l.activateFromUser();
    check(l.aktiveringar == 0, "V13: ingen aktuell rad — ingen aktivering");
    l.selectFromUser(1, 0, false);
    l.activateFromUser();
    check(l.aktiveringar == 1, "V13: aktiveringen fyrar onActivate");
}

void fallKortningen()
{
    for (const float skala : {1.0f, 2.0f}) {
        StegFont         f(8.0f * skala);   // 8 logiska px per tecken
        const glaze::Ctx c = ctxFör(f, nullptr, skala, {0, 0, 400, 300});
        const std::string tag = " (skala " + std::to_string(int(skala)) + ")";
        check(glaze::Draw::elide(c, "Rapport", 56.0f) == "Rapport",
              "E1: texten ryms (7 × 8 = 56) — orörd" + tag);
        // 40 px: ellipsen 8, rum 32 ⇒ 4 tecken + …
        check(glaze::Draw::elide(c, "Rapport", 40.0f) == "Rapp\xE2\x80\xA6",
              "E2: kortad till 4 tecken + ellips" + tag,
              glaze::Draw::elide(c, "Rapport", 40.0f));
        check(glaze::Draw::elide(c, "Åäöåäö", 32.0f) == "Åäö\xE2\x80\xA6",
              "E3: åäö är ETT tecken var — kortningen klyver aldrig" + tag,
              glaze::Draw::elide(c, "Åäöåäö", 32.0f));
        check(glaze::Draw::elide(c, "Ab cdefgh", 32.0f) == "Ab\xE2\x80\xA6",
              "E4: blanksteget närmast ellipsen tas bort" + tag,
              glaze::Draw::elide(c, "Ab cdefgh", 32.0f));
        check(glaze::Draw::elide(c, "Rapport", 4.0f).empty(),
              "E5: inte ens ellipsen ryms — ingenting" + tag);
    }
}

// RITNINGEN mot en inspelande renderer — vilka rader och vilken text.
void fallRitningen()
{
    StegFont   f(8.0f);
    Inspelning rec;
    Prov       l;
    l.width  = 200;
    l.height = 26 * 4 + 26 + 2;   // huvud + fyra rader + ramen (Slate)
    l.addColumn("Namn", 80);
    l.addColumn("Storlek", 80, glaze::Align::Right);
    for (int i = 0; i < 10; ++i)
        l.addRow({"rad" + std::to_string(i), std::to_string(i * 100) + " B"});
    l.addRow({"Ett mycket långt filnamn.txt", "1 B"});

    glaze::Ctx c = ctxFör(f, &rec, 1.0f, {0, 0, 400, 300});
    c.box        = l.resolve(c);
    l.draw(c);
    check(har(rec.texter, "Namn") && har(rec.texter, "Storlek"),
          "R1: huvudet ritar rubrikerna");
    check(har(rec.texter, "rad0") && har(rec.texter, "rad3") &&
              !har(rec.texter, "rad4"),
          "R2: bara de SYNLIGA raderna ritas (O(synliga)) — inte ens raden "
          "som börjar exakt vid nederkanten");

    // Sortera fallande på storlek och rulla till botten — den långa raden
    // (naturligt "1 B" minst) hamnar sist och syns KORTAD.
    l.setSortColumn(1);
    l.setSortAscending(false);
    l.setScroll(0, 1000);
    rec.texter.clear();
    l.draw(c);
    bool kortad = false;
    for (const std::string& t : rec.texter)
        if (t.rfind("Ett ", 0) == 0 &&
            t.find("\xE2\x80\xA6") != std::string::npos &&
            t.size() < std::string("Ett mycket långt filnamn.txt").size())
            kortad = true;
    check(kortad, "R3: en cell bredare än kolumnen KORTAS med ellips");
    check(!har(rec.texter, "rad9"),
          "R4: rullad till botten (scrollen klampas) — översta raden borta");
}

// ======================================== KOLUMNHUVUDET PÅ FLERA RADER
//
// RADERNA — `Draw::elideLines` mot den falska fonten (8 logiska px per
// tecken), i två skalor. Förväntat värde är RÄKNAT för hand ur teckenstegen; ingen
// rad frågar produktionen var den bröt.
void fallRubrikraderna()
{
    for (const float skala : {1.0f, 2.0f}) {
        StegFont          f(8.0f * skala);
        const glaze::Ctx  c   = ctxFör(f, nullptr, skala, {0, 0, 400, 300});
        const std::string tag = " (skala " + std::to_string(int(skala)) + ")";
        using V = std::vector<std::string>;
        const auto rader = [&](const std::string& s, float w, int n) {
            return glaze::Draw::elideLines(c, s, w, n);
        };

        check(rader("Namn", 100, 1) == V{"Namn"},
              "B1: en kort rubrik är EN rad, orörd" + tag);
        check(rader("Nedhäng\nvertikal\nb\n(m)", 100, 0) ==
                  V{"Nedhäng", "vertikal", "b", "(m)"},
              "B2: \\n bryter alltid" + tag,
              fog(rader("Nedhäng\nvertikal\nb\n(m)", 100, 0)));
        // 80 px = 10 tecken: "Load cases" ryms precis, "for calculation"
        // bryts på ordgränsen, och "calculation" (11) ryms inte ens ensamt.
        const V lc = rader("Load cases for calculation", 80, 0);
        check(lc == V{"Load cases", "for", "calculati" + kEll},
              "B3: automatisk brytning på ORDGRÄNS, och ett ord bredare än "
              "kolumnen KORTAS på sin egen rad" + tag, fog(lc));
        check(lc.size() == 3,
              "B4: ordet klyvs ALDRIG (wrapTexts hårdbrytning hade gett fyra "
              "rader)" + tag, std::to_string(lc.size()));
        check(rader("Nedhäng\nvertikal\nprojektion\nb\n(m)", 100, 3) ==
                  V{"Nedhäng", "vertikal", "projektion" + kEll},
              "B5: fler rader än huvudet rymmer — den sista synliga får en "
              "ellips även när den själv ryms" + tag,
              fog(rader("Nedhäng\nvertikal\nprojektion\nb\n(m)", 100, 3)));
        check(rader("Lastfall enligt standarden", 64, 2) ==
                  V{"Lastfall", "enligt" + kEll},
              "B6: den sista synliga bär RESTEN av sitt stycke, kortad" + tag,
              fog(rader("Lastfall enligt standarden", 64, 2)));
        // EN rad utan \n ÄR den vanliga kortningen — också när brytningen hade
        // ätit ett avslutande blanksteg ("Storlek " i 56 px).
        bool samma = true;
        std::string avvik;
        const std::pair<const char*, float> prov[] = {
            {"Rapport", 40}, {"Ab cdefgh", 32}, {"Storlek ", 56},
            {"Ett mycket långt filnamn.txt", 80}, {"", 50},
            {"Åäöåäö", 32}, {"Rapport", 56}};
        for (const auto& [s, w] : prov) {
            const V           r = rader(s, w, 1);
            const std::string e = glaze::Draw::elide(c, s, w);
            if (r != V{e}) {
                samma = false;
                avvik += std::string(s) + " → " + fog(r) + " mot \"" + e + "\" ";
            }
        }
        check(samma, "B7: en rad utan \\n är EXAKT `elide` — ett enradshuvud "
                     "ritar samma text som förut" + tag, avvik);
        check(rader("", 50, 0) == V{""}, "B8: tom text är en tom rad" + tag);
        check(rader("Åäöåäö Åäö", 32, 0) == V{"Åäö" + kEll, "Åäö"},
              "B9: åäö — kortningen och brytningen klyver inget tecken" + tag,
              fog(rader("Åäöåäö Åäö", 32, 0)));
        check(rader("A\n", 50, 0) == V{"A", ""} &&
                  rader("A\n", 50, 1) == V{"A" + kEll},
              "B10: ett avslutande \\n är en tom rad — och utan plats för den "
              "en ellips" + tag, fog(rader("A\n", 50, 1)));
    }
}

// HÖJDEN — mätt i det som RITAS: rad 0:s text, huvudets underlinje och
// avdelaren. Förväntat ur kontraktet: ramen `panelInset`, huvudet
// `tableHeaderH` + (n − 1) radhöjder (fontens 16 logiska px), raden
// `listRowH` och textens v-centrering (fontens höjd 16).
void fallHuvudetsHojd()
{
    const glaze::Style& st  = glaze::defaultStyle(glaze::UI::Slate);
    const float         in  = st.panelInset;
    const float         th  = st.tableHeaderH;
    const float         lh  = 16.0f;
    const float         rh  = st.listRowH;
    for (const float skala : {1.0f, 2.0f}) {
        StegFont          f(8.0f * skala);
        Inspelning        rec;
        const std::string tag = " (skala " + std::to_string(int(skala)) + ")";
        // Huvudets höjd `hh` mätt i ritningen: rad 0 under det, underlinjen
        // i dess kant och den första avdelaren (kolumn 0:s högra kant,
        // `kant` från ramen) över hela höjden.
        const auto mät = [&](Prov& l, float hh, const std::string& vad,
                             float kant = 100.0f) {
            rec.rensa();
            glaze::Ctx c = ctxFör(f, &rec, skala, {0, 0, 600, 800});
            c.box        = l.resolve(c);
            l.draw(c);
            const Inspelning::Text* r0 = rec.text("rad0");
            const float             y0 = skala * (in + hh + (rh - lh) / 2.0f);
            check(r0 && nara(r0->y, y0),
                  "H: " + vad + " — rad 0 börjar UNDER huvudet" + tag,
                  r0 ? tal(r0->y) + " mot " + tal(y0) : "rad0 ritades inte");
            check(rec.vagratt(skala * (in + hh), skala * in,
                              skala * (300.0f - in)),
                  "H: " + vad + " — huvudets underlinje i dess kant" + tag);
            check(rec.lodratt(skala * (in + kant), skala * in,
                              skala * (in + hh)),
                  "H: " + vad + " — avdelaren över HELA huvudets höjd" + tag);
        };
        for (const int n : {1, 2, 4}) {
            Prov l;
            l.width  = 300;
            l.height = 400;
            l.addColumn("Namn", 100);
            l.addColumn("Storlek", 100, glaze::Align::Right);
            for (int i = 0; i < 10; ++i)
                l.addRow({"rad" + std::to_string(i), "x"});
            l.setHeaderLines(n);
            mät(l, th + float(n - 1) * lh, std::to_string(n) + " rad(er)");
        }

        // AUTO — den rubrik som behöver flest rader, och den FÖLJER med när
        // en bredd, en rubrik eller språket ändras (cachen släpps).
        Prov a;
        a.width  = 300;
        a.height = 400;
        a.addColumn("Load cases for calculation", 96);   // 80 px text ⇒ 3 rader
        a.addColumn("A\nB", 100);                        // 2 rader
        for (int i = 0; i < 10; ++i)
            a.addRow({"rad" + std::to_string(i)});
        a.setHeaderLines(0);
        check(a.headerLines() == 0, "H: setHeaderLines(0) läses tillbaka som "
                                    "0 (AUTO)" + tag);
        mät(a, th + 2.0f * lh, "AUTO, tre rader ur brytningen", 96.0f);
        a.setColumnWidth(0, 160);   // 144 px ⇒ "Load cases for" / "calculation"
        mät(a, th + lh, "AUTO efter en bredare kolumn — två rader", 160.0f);
        a.setColumn(1, "A\nB\nC\nD", 100);
        mät(a, th + 3.0f * lh, "AUTO efter en ny rubrik — fyra rader", 160.0f);
        a.setColumns({{"Kort", 100}, {"Kort", 100}});
        mät(a, th, "AUTO med enradsrubriker — ett vanligt huvud");

        // Språket: ramverkets "Alla filer" är tio tecken på svenska och
        // sexton på finska ("Kaikki tiedostot") — i 80 px en rad mot två.
        glaze::setLanguage("sv");
        Prov s;
        s.width  = 300;
        s.height = 400;
        s.addColumn(glaze::tr(glaze::str::FileAllFiles), 96);
        for (int i = 0; i < 10; ++i)
            s.addRow({"rad" + std::to_string(i)});
        s.setHeaderLines(0);
        mät(s, th, "AUTO på svenska — en rad", 96.0f);
        glaze::setLanguage("fi");
        s.languageChanged();   // det App gör med varje widget i ett öppet fönster
        mät(s, th + lh, "AUTO efter språkbyte till finska — två rader", 96.0f);
        glaze::setLanguage("sv");
        s.languageChanged();
    }
}

// JUSTERINGEN — rubrikens och cellernas, i sidled per kolumn och i höjdled
// per tabell, mätt som TEXTENS PLATS mot förväntade värden ur kontraktet (kolumnens
// kant, cellens luft `listPadX`, textens bredd 8 px per tecken). Rubrikerna
// har med flit en ANNAN justering än sina celler, så att ingen av dem kan
// passera på den andras.
void fallJusteringen()
{
    const glaze::Style& st  = glaze::defaultStyle(glaze::UI::Slate);
    const float         in  = st.panelInset;
    const float         th  = st.tableHeaderH;
    const float         pad = st.listPadX;
    const float         lh  = 16.0f;
    const float         kw  = 80.0f;   // kolumnbredden
    const auto x0 = [&](int k) { return in + kw * float(k); };
    // Textens vänsterkant för en text på `tecken` tecken i kolumn k.
    const auto vänster = [&](int k, int) { return x0(k) + pad; };
    const auto mitten  = [&](int k, int tecken) {
        return x0(k) + pad + (kw - 2.0f * pad - 8.0f * float(tecken)) / 2.0f;
    };
    const auto höger   = [&](int k, int tecken) {
        return x0(k) + kw - pad - 8.0f * float(tecken);
    };

    for (const float skala : {1.0f, 2.0f}) {
        StegFont          f(8.0f * skala);
        Inspelning        rec;
        const std::string tag = " (skala " + std::to_string(int(skala)) + ")";
        const auto rita = [&](Prov& l) {
            rec.rensa();
            glaze::Ctx c = ctxFör(f, &rec, skala, {0, 0, 600, 800});
            c.box        = l.resolve(c);
            l.draw(c);
        };
        const auto vidX = [&](const std::string& s, float facit,
                              const std::string& vad) {
            const Inspelning::Text* t = rec.text(s);
            check(t && nara(t->x, skala * facit), vad + tag,
                  t ? tal(t->x) + " mot " + tal(skala * facit)
                    : "\"" + s + "\" ritades inte");
        };

        // ---- SIDLED. Cellerna Left/Center/Right/Right; rubrikerna
        //      Right/Left/Center och den fjärde UTAN egen — den ärver Right.
        Prov l;
        l.width  = 4.0f * kw + 2.0f * in;
        l.height = 300;
        l.setColumns({{"A", kw, glaze::Align::Left, glaze::Align::Right},
                      {"BB", kw, glaze::Align::Center, glaze::Align::Left},
                      {"CCC", kw, glaze::Align::Right, glaze::Align::Center},
                      {"DDDD", kw, glaze::Align::Right}});
        l.addRow({"a", "bb", "ccc", "dddd"});
        rita(l);
        vidX("A", höger(0, 1), "J1: rubrik Right (cellerna Left)");
        vidX("BB", vänster(1, 2), "J1: rubrik Left (cellerna Center)");
        vidX("CCC", mitten(2, 3), "J1: rubrik Center (cellerna Right)");
        vidX("DDDD", höger(3, 4),
             "J1: rubrik UTAN headerAlign följer cellerna (Right)");
        vidX("a", vänster(0, 1), "J2: cell Left");
        vidX("bb", mitten(1, 2), "J2: cell Center");
        vidX("ccc", höger(2, 3), "J2: cell Right");
        vidX("dddd", höger(3, 4), "J2: cell Right (rubriken ärver)");

        // ---- HÖJDLED. Ett huvud på fyra rader; rubriker på 1, 2 och 4
        //      rader, och en på fem (den fjärde synliga får ellipsen).
        //      Plats p står `th/2 + p · radhöjd` under huvudets överkant, och
        //      texten ritas en halv fonthöjd (8) ovanför sin mitt.
        Prov v;
        v.width  = 4.0f * kw + 2.0f * in;
        v.height = 300;
        v.setColumns({{"A", kw},
                      {"B1\nB2", kw},
                      {"C1\nC2\nC3\nC4", kw},
                      {"E1\nE2\nE3\nE4\nE5", kw}});
        v.setHeaderLines(4);
        const float hh = th + 3.0f * lh;
        const auto  platsY = [&](float p) {
            return skala * (in + th / 2.0f + p * lh) - 8.0f * skala;
        };
        const auto vidY = [&](const std::string& s, float p,
                              const std::string& vad) {
            const Inspelning::Text* t = rec.text(s);
            check(t && nara(t->y, platsY(p)), vad + tag,
                  t ? tal(t->y) + " mot " + tal(platsY(p))
                    : "\"" + s + "\" ritades inte");
        };
        struct Fall {
            glaze::VAlign v;
            const char*   namn;
            float         förstaEn, förstaTvå;   // en- och tvåradsrubrikens plats
            float         pil;                   // pilens plats
        };
        const Fall fall[] = {{glaze::VAlign::Top, "Top", 0.0f, 0.0f, 0.0f},
                             {glaze::VAlign::Center, "Center", 1.5f, 1.0f, 1.5f},
                             {glaze::VAlign::Bottom, "Bottom", 3.0f, 2.0f, 3.0f}};
        for (const Fall& fl : fall) {
            v.setHeaderVAlign(fl.v);
            v.setSortColumn(1);
            rita(v);
            const std::string n = std::string(" — ") + fl.namn;
            vidY("A", fl.förstaEn, "J3: enradsrubriken på sin plats" + n);
            vidY("B1", fl.förstaTvå, "J3: tvåradsrubrikens första rad" + n);
            vidY("B2", fl.förstaTvå + 1.0f,
                 "J3: tvåradsrubrikens andra rad, en radhöjd under" + n);
            vidY("C1", 0.0f, "J3: en FULL rubrik fyller huvudet (rad 1)" + n);
            vidY("C4", 3.0f, "J3: en FULL rubrik fyller huvudet (rad 4)" + n);
            vidY("E4" + kEll, 3.0f,
                 "J4: femte raden ryms inte — den fjärde bär ellipsen" + n);
            check(!rec.text("E5") && !rec.text("E4"),
                  "J4: varken \"E5\" eller ett okortat \"E4\" ritas" + n + tag);
            check(rec.lodratt(skala * x0(1), skala * in, skala * (in + hh)) &&
                      rec.lodratt(skala * x0(4), skala * in, skala * (in + hh)),
                  "J5: avdelarna går över hela det höga huvudet" + n + tag);
            // PILEN — dess två snedstreck ligger i kolumnens högerkant; mitten
            // av deras höjd ska stå där en enradsrubrik står.
            const float px0 = skala * (x0(2) - pad - 2.0f * st.chevronW);
            const float px1 = skala * (x0(2) - pad);
            float       lo = 1e9f, hi = -1e9f;
            int         snett = 0;
            for (const Inspelning::Streck& s : rec.streck)
                if (s.x1 != s.x2 && s.y1 != s.y2 &&
                    std::min(s.x1, s.x2) >= px0 - 0.01f &&
                    std::max(s.x1, s.x2) <= px1 + 0.01f) {
                    ++snett;
                    lo = std::min({lo, s.y1, s.y2});
                    hi = std::max({hi, s.y1, s.y2});
                }
            const float pilFacit = skala * (in + th / 2.0f + fl.pil * lh);
            check(snett == 2 && nara((lo + hi) / 2.0f, pilFacit),
                  "J6: sorteringspilen står där en enradsrubrik står" + n + tag,
                  std::to_string(snett) + " streck, mitt " +
                      tal((lo + hi) / 2.0f) + " mot " + tal(pilFacit));
        }
    }
}

// ================================= RADENS FÄRG OCH BILDEN

std::string färgText(std::optional<gfx::Color> c)
{
    if (!c)
        return "(inte ritad)";
    return "{" + std::to_string(c->r) + "," + std::to_string(c->g) + "," +
           std::to_string(c->b) + "}";
}

// MARKERINGENS textfärg per familj — ListBoxens recept, skrivet här ur
// kontraktet och inte läst ur listvyn.
gfx::Color valdFärg(glaze::UI ui, const glaze::Theme::Colors& col)
{
    return ui == glaze::UI::Slate   ? col.windowBg
           : ui == glaze::UI::Glaze ? col.onAccent
                                    : col.accent;
}

// C1–C5. RADENS FÄRG ritas på en OMARKERAD rad i alla tre UI; en
// vald rad tar markeringens färg och ett inaktivt läge `textDim`; färgen
// FÖLJER RADEN genom en sortering; `setRow` utan färg nollar, `setCell`
// lämnar den; massformen bär den positionellt och designerat.
void fallRadfargen()
{
    const gfx::Color röd{201, 13, 77, 255};   // ingen temafärg
    const gfx::Color grön{0, 140, 0, 255};
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string n = ui == glaze::UI::Slate   ? " (Slate)"
                              : ui == glaze::UI::Glaze ? " (Glaze)"
                                                       : " (Minimalistic)";
        StegFont   f(8.0f);
        Inspelning rec;
        glaze::Ctx c = ctxFör(f, &rec, 1.0f, {0, 0, 400, 300});
        c.ui         = ui;
        c.style      = &glaze::defaultStyle(ui);
        c.theme      = &glaze::defaultTheme(ui).light;
        const auto& col = c.theme->colors;

        Prov l;
        l.width  = 240;
        l.height = 200;
        l.addColumn("Namn", 120);
        l.addColumn("Nr", 60);
        l.addRow({"röd", "1"}, {}, röd);
        l.addRow({"vanlig", "2"});
        l.addRow({"grön", "3"}, {}, grön);
        c.box = l.resolve(c);
        l.draw(c);
        check(rec.färgPå("röd") == röd && rec.färgPå("1") == röd &&
                  rec.färgPå("grön") == grön,
              "C1: radens färg på VARJE cell i en omarkerad rad" + n,
              färgText(rec.färgPå("röd")));
        check(rec.färgPå("vanlig") == col.text,
              "C1: en rad utan färg — temats textfärg" + n,
              färgText(rec.färgPå("vanlig")));

        rec.rensa();
        l.setSelectedIndex(0);
        l.draw(c);
        check(rec.färgPå("röd") == valdFärg(ui, col) &&
                  rec.färgPå("grön") == grön,
              "C2: VALD rad — markeringens färg vinner" + n,
              färgText(rec.färgPå("röd")));
        l.setSelectedIndex(-1);

        rec.rensa();
        l.disable();
        l.draw(c);
        check(rec.färgPå("röd") == col.textDim &&
                  rec.färgPå("vanlig") == col.textDim,
              "C2: INAKTIV lista — allt i textDim" + n,
              färgText(rec.färgPå("röd")));
        l.enable();

        // Sorterad på namnet: grön · röd · vanlig. Färgen ligger i RADEN —
        // per plats hade "grön" (plats 0) ritats röd.
        rec.rensa();
        l.setSortColumn(0);
        l.draw(c);
        check(rec.färgPå("grön") == grön && rec.färgPå("röd") == röd &&
                  rec.färgPå("vanlig") == col.text && l.rowColor(0) == röd,
              "C3: färgen FÖLJER raden genom sorteringen" + n,
              färgText(rec.färgPå("grön")));
        l.setSortColumn(-1);
    }

    Prov l;
    l.addColumn("Namn");
    l.addRow({"a"}, {}, röd);
    l.addRow({"b"});
    l.setRow(0, {"a2"});
    check(!l.rowColor(0), "C4: setRow utan färg NOLLAR färgen (tilldelning)");
    l.setRow(0, {"a3"}, {}, grön);
    check(l.rowColor(0) == grön, "C4: setRow med färg sätter den");
    l.setCell(0, 0, "a4");
    check(l.rowColor(0) == grön && l.cell(0, 0) == "a4",
          "C4: setCell lämnar färgen");
    l.setRowColor(1, röd);
    check(l.rowColor(1) == röd, "C4: setRowColor");
    l.setRowColor(1, std::nullopt);
    check(!l.rowColor(1), "C4: setRowColor(nullopt) nollar");
    l.setRowColor(7, röd);
    check(!l.rowColor(7) && l.rowCount() == 2,
          "C4: setRowColor utanför — no-op, och rowColor är tom utanför");
    l.setRows({{{"x"}, {}, röd}, {.cells = {"y"}, .color = grön}, {{"z"}}});
    check(l.rowColor(0) == röd && l.rowColor(1) == grön && !l.rowColor(2),
          "C5: massformen — positionellt och designerat, utan färg = tom");
}

// C6. En BITMAP som radens ikon ritas: EN bild, i `iconFallback`-storlek
// (en bild ur pixlar har ingen storleksklass), i första kolumnen.
void fallRadbilden()
{
    for (const float skala : {1.0f, 2.0f}) {
        const std::string tag = " (skala " + std::to_string(int(skala)) + ")";
        StegFont          f(8.0f * skala);
        Inspelning        rec;
        glaze::ImageCache bilder(rec);
        glaze::Ctx        c = ctxFör(f, &rec, skala, {0, 0, 600, 400});
        c.images            = &bilder;
        const float       ic = c.style->iconFallback * skala;

        Prov l;
        l.width  = 240;
        l.height = 120;
        l.addColumn("Namn", 120);
        l.addColumn("Nr", 60);
        l.addRow({"bild", "1"}, glaze::Bitmap(8, 8));
        l.addRow({"ingen", "2"});
        c.box = l.resolve(c);
        l.draw(c);
        const bool iFörsta = rec.bilder.size() == 1 &&
                             rec.bilder[0].x >= c.box.x &&
                             rec.bilder[0].x + rec.bilder[0].w <=
                                 c.box.x + 120.0f * skala + 0.01f;
        check(iFörsta && nara(rec.bilder[0].w, ic) && nara(rec.bilder[0].h, ic),
              "C6: Bitmap-ikonen ritas EN gång, iconFallback stor, i första "
              "kolumnen" + tag,
              std::to_string(rec.bilder.size()) + " bilder");
    }
}

// ======================================== KRYSSEN
//
// K1–K2 tillståndet och läsvägarna, K3–K4 ritningen mot den inspelande
// renderern. Rutans plats räknas ur KONTRAKTET — ramens `panelInset`,
// huvudets `tableHeaderH`, radens `listRowH`, cellens `listPadX` och rutans
// `checkSize` — aldrig ur listvyns `checkRect`.

void fallKryssen()
{
    // ---- K1. Settrarna och läsvägarna: av som standard, `setChecked` fyrar
    //          ALDRIG, en rad utanför är en no-op, och `showChecks` når
    //          widgeten genom `setProp`. Tillståndet finns oavsett rutorna.
    Prov l;
    int  kryss = 0, klar = 0;
    l.onCheck  = [&kryss] { ++kryss; };
    l.onCommit = [&klar] { ++klar; };
    l.addColumn("Namn");
    for (const char* s : {"a", "b", "c", "d"})
        l.addRow({s});
    check(!l.showsChecks() && l.checkedRows().empty() && l.changedRow() == -1,
          "K1: rutorna AV som standard, inget kryssat, changedRow −1");
    l.setShowChecks(true);
    l.setChecked(2, true);
    l.setChecked(0, true);
    check(l.showsChecks() && l.isChecked(0) && !l.isChecked(1) &&
              l.isChecked(2) && kryss == 0 && klar == 0 && l.changedRow() == -1,
          "K1: setChecked kryssar — och fyrar ALDRIG onCheck/onCommit");
    l.setChecked(9, true);
    l.setChecked(-1, true);
    check(l.checkedRows() == std::vector<int>{0, 2} && !l.isChecked(9) &&
              !l.isChecked(-1),
          "K1: en rad utanför — no-op, och isChecked är false utanför",
          fog(l.checkedRows()));
    check(l.setProp("showChecks", "false") && !l.showsChecks() &&
              l.isChecked(0) && l.setProp("showChecks", "true") &&
              l.showsChecks(),
          "K1: setProp(showChecks) — och kryssen står kvar när rutorna döljs");

    // ---- K2. Kollektionen: `addRow` ger okryssat, `setRow` BEHÅLLER (vyns
    //          tillstånd, inte radens data), `setRows` NOLLAR — också
    //          `changedRow` — och `checkedRows` går i VISNINGSORDNINGEN.
    l.addRow({"e"});
    check(!l.isChecked(4), "K2: addRow ger en okryssad rad");
    l.setRow(2, {"C"});
    check(l.isChecked(2), "K2: setRow behåller krysset");
    check(l.toggleCheckFromUser(1) && l.isChecked(1) && l.changedRow() == 1 &&
              kryss == 1 && klar == 1,
          "K2: (användarvägen kryssar rad 1 — changedRow 1)");
    l.setSortColumn(0);
    l.setSortAscending(false);   // e d C b a
    check(l.checkedRows() == std::vector<int>{2, 1, 0},
          "K2: checkedRows i visningsordningen (fallande: C b a)",
          fog(l.checkedRows()));
    l.setRows({{{"x"}}, {{"y"}}});
    check(l.checkedRows().empty() && l.changedRow() == -1 && !l.isChecked(0),
          "K2: setRows NOLLAR kryssen och changedRow");
    check(kryss == 1 && klar == 1, "K2: inget av det fyrade något");

    // ---- K3 och K4 — ritningen i alla tre UI.
    const gfx::Color appens{201, 13, 77, 255};   // ingen temafärg
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string n = ui == glaze::UI::Slate   ? " (Slate)"
                              : ui == glaze::UI::Glaze ? " (Glaze)"
                                                       : " (Minimalistic)";
        StegFont   f(8.0f);
        Inspelning rec;
        glaze::Ctx c = ctxFör(f, &rec, 1.0f, {0, 0, 400, 300});
        c.ui         = ui;
        c.style      = &glaze::defaultStyle(ui);
        c.theme      = &glaze::defaultTheme(ui).light;
        const auto& col = c.theme->colors;
        const auto& m   = *c.style;

        Prov t;
        t.width  = 240;
        t.height = 200;
        t.addColumn("Namn", 120);
        t.addRow({"b"}, {}, appens);   // rad 0 — appens färg
        t.addRow({"a"});               // rad 1
        t.addRow({"c"});               // rad 2
        t.setShowChecks(true);
        t.setChecked(0, true);
        t.setChecked(2, true);
        c.box = t.resolve(c);
        // RUTAN på plats p, en px större åt alla håll (bockens streck är
        // kantutjämnade och ligger innanför).
        const auto ruta = [&](int p) {
            return gfx::Rectf{
                c.box.x + m.panelInset + m.listPadX - 1.0f,
                c.box.y + m.panelInset + m.tableHeaderH +
                    float(p) * m.listRowH + (m.listRowH - m.checkSize) / 2.0f -
                    1.0f,
                m.checkSize + 2.0f, m.checkSize + 2.0f};
        };

        // K4: texten börjar efter rutan och `checkGap`.
        t.draw(c);
        const Inspelning::Text* tb = rec.text("b");
        const float tx = c.box.x + m.panelInset + m.listPadX + m.checkSize +
                         m.checkGap;
        check(tb && nara(tb->x, tx),
              "K4: kolumn 0:s text börjar efter rutan och checkGap" + n,
              tb ? tal(tb->x) + " mot " + tal(tx) : "\"b\" ritades inte");
        check(rec.streckI(col.text, ruta(0)) > 0 && rec.streckI(appens) == 0,
              "K4: bocken i TEMATS textfärg — aldrig i radens app-färg" + n,
              std::to_string(rec.streckI(appens)) + " streck i appens färg");
        check(rec.streckI(ruta(1)) == 0,
              "K4: en okryssad rad har inga bockstreck" + n);

        // K3: sorterad a b c — rad 0 ("b") står på plats 1, och bocken följer.
        rec.rensa();
        t.setSortColumn(0);
        t.draw(c);
        check(t.isChecked(0) && !t.isChecked(1) &&
                  rec.streckI(col.text, ruta(1)) > 0 &&
                  rec.streckI(ruta(0)) == 0,
              "K3: krysset följer RADEN — bocken på dess nya plats 1" + n);

        // K4: en VALD rad — bocken i markeringens färg.
        rec.rensa();
        t.setSelectedIndex(2);   // "c", plats 2
        t.draw(c);
        check(rec.streckI(valdFärg(ui, col), ruta(2)) > 0,
              "K4: på en vald rad i markeringens färg" + n,
              färgText(valdFärg(ui, col)));
        t.setSelectedIndex(-1);

        // K4: INAKTIV — bocken i textDim.
        rec.rensa();
        t.disable();
        t.draw(c);
        check(rec.streckI(col.textDim, ruta(1)) > 0 &&
                  rec.streckI(col.text, ruta(1)) == 0,
              "K4: en inaktiv lista — bocken i textDim" + n);
        t.enable();
    }
}

// ====================== FYLLKOLUMNEN OCH IKONSTORLEKEN
//
// Q4–Q8, headless mot den inspelande renderern. Förväntat ur KONTRAKTET —
// innehållets bredd är `innerBounds`, radhöjden max(`listRowH`, storleken +
// `listRowH` − `iconFallback`). SABOTAGEN: `dividerAt` utan hoppet (Q5 ↔ på
// fyllkolumnens kant), cachen utan `fillW` (Q6 — samma radantal smal och
// bred), ingen reserverad ikonkolumn (Q7 text-x skiljer), `rowH` utan max
// (Q7). MÄTER INTE dragningen med musen — Q5 drar med `setColumnWidth`, den
// programmatiska tvillingen.
void fallFyllOchIkon()
{
    StegFont          f(8.0f);
    Inspelning        rec;
    glaze::ImageCache bilder(rec);
    glaze::Ctx        c = ctxFör(f, &rec, 1.0f, {0, 0, 800, 400});
    c.images            = &bilder;
    const auto& m       = *c.style;

    Prov l;
    l.width  = 300;
    l.height = 200;
    l.addColumn("Namn", 80);
    l.addColumn("B", 60);
    l.addColumn("C", 50);
    l.addRow({"a", "1", "x"});
    l.setFillColumn(0);
    const auto summa = [&] {
        float s = 0.0f;
        for (int i = 0; i < l.columnCount(); ++i)
            s += l.columnW(c, i);
        return s;
    };
    // ---- Q4. Σ kolumner == innehållets bredd, och fyllkolumnen ≥ sin egen.
    check(nara(summa(), l.innerBounds(c).w) && l.columnW(c, 0) >= 80.0f,
          "Q4: fyllkolumnen tar upp resten — Σ == innehållets bredd",
          tal(summa()) + " mot " + tal(l.innerBounds(c).w));
    l.width = 150;
    check(nara(l.columnW(c, 0), 80.0f) && summa() > l.innerBounds(c).w,
          "Q4: smalare än minimum — den satta bredden, och tabellen rullar",
          tal(l.columnW(c, 0)));
    l.width = 300;

    // ---- Q5. Fyllkolumnens kant är ingen avdelare; en annan avdelare
    //          dragen tar fyllkolumnen upp skillnaden.
    c.box = l.resolve(c);
    const float hy = c.box.y + m.panelInset + m.tableHeaderH / 2.0f;
    const float x0 = c.box.x + m.panelInset;
    check(l.cursorAt(c, x0 + l.columnW(c, 0), hy) != gfx::PointerShape::ResizeWE &&
              l.cursorAt(c, x0 + l.columnW(c, 0) + l.columnW(c, 1), hy) ==
                  gfx::PointerShape::ResizeWE,
          "Q5: ↔ på en annan avdelare, aldrig på fyllkolumnens kant");
    const float fyllFöre = l.columnW(c, 0);
    l.setColumnWidth(1, 80.0f);
    check(nara(fyllFöre - l.columnW(c, 0), 20.0f),
          "Q5: en bredare kolumn — fyllkolumnen krymper lika mycket",
          tal(fyllFöre - l.columnW(c, 0)));
    l.setColumnWidth(1, 60.0f);

    // ---- Q6. AUTO-huvudet följer fyllkolumnens bredd (cachens nyckel).
    l.setColumn(0, "Ett mycket långt namn", 80);
    l.setHeaderLines(0);
    l.width = 700;
    const int bred = l.headerLineCount(c);
    l.width = 250;
    const int smal = l.headerLineCount(c);
    check(bred == 1 && smal >= 2,
          "Q6: en smalare lista ger fler rubrikrader",
          std::to_string(bred) + "/" + std::to_string(smal));
    l.setHeaderLines(1);
    l.width = 300;

    // ---- Q7. setIconSize(48): radhöjden, ikonrutan och textens x.
    Prov ik;
    ik.width  = 300;
    ik.height = 200;
    ik.addColumn("Namn", 160);
    ik.addRow({"bild"}, glaze::Bitmap(8, 8));
    ik.addRow({"ingen"});
    ik.setIconSize(48.0f);
    const float väntadH =
        std::max(m.listRowH, 48.0f + m.listRowH - m.iconFallback);
    check(nara(ik.rowH(c), väntadH) && nara(ik.iconSide(c, 1), 48.0f),
          "Q7: radhöjden är max-regeln, och raden UTAN bild reserverar 48",
          tal(ik.rowH(c)) + " mot " + tal(väntadH));
    rec.rensa();
    c.box = ik.resolve(c);
    ik.draw(c);
    const Inspelning::Text* tb = rec.text("bild");
    const Inspelning::Text* ti = rec.text("ingen");
    check(rec.bilder.size() == 1 && nara(rec.bilder[0].w, 48.0f) &&
              nara(rec.bilder[0].h, 48.0f) && tb && ti && nara(tb->x, ti->x),
          "Q7: ikonrutan är 48 och texten står i linje på båda raderna",
          std::to_string(rec.bilder.size()) + " bilder");

    // ---- Q8. setIconSize(0) — samma inspelning som utan anropet.
    Prov orörd;
    orörd.width  = 300;
    orörd.height = 200;
    orörd.addColumn("Namn", 160);
    orörd.addRow({"bild"}, glaze::Bitmap(8, 8));
    orörd.addRow({"ingen"});
    rec.rensa();
    c.box = orörd.resolve(c);
    orörd.draw(c);
    const std::vector<Inspelning::Text> facit = rec.platser;
    ik.setIconSize(0.0f);
    rec.rensa();
    c.box = ik.resolve(c);
    ik.draw(c);
    bool lika = facit.size() == rec.platser.size();
    for (std::size_t i = 0; lika && i < facit.size(); ++i)
        lika = facit[i].s == rec.platser[i].s && nara(facit[i].x, rec.platser[i].x) &&
               nara(facit[i].y, rec.platser[i].y);
    check(lika, "Q8: setIconSize(0) ritar som utan anropet (AUTO)");
}

// ============= CELLFÄRGER, RADRUBRIKER, BREDD EFTER INNEHÅLLET
//
// Positionerna räknas ur KONTRAKTET (ramens indrag `panelInset`, huvudets
// `tableHeaderH`, radens `listRowH`, kolumnbredderna) och bredderna ur
// teckensteget (8 px per tecken) och stilens luft — aldrig ur widgetens egna
// `cellRect`/`columnW`.

const char* uiNamn(glaze::UI ui)
{
    return ui == glaze::UI::Slate   ? " (Slate)"
           : ui == glaze::UI::Glaze ? " (Glaze)"
                                    : " (Minimalistic)";
}

// P1–P7. CELLENS FÄRGER: cellens text slår radens som slår temats,
// bakgrunden fyller EXAKT cellen, en vald rad visar bara markeringens färger,
// en inaktiv tabell inga appfärger, och färgerna följer RADEN genom en
// sortering; sättaren är tilldelning och logg + no-op utanför.
void fallCellfarger()
{
    const gfx::Color röd{201, 13, 77, 255};
    const gfx::Color grön{0, 140, 0, 255};
    const gfx::Color blå{12, 34, 200, 255};
    const gfx::Color gul{240, 220, 10, 255};
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string n = uiNamn(ui);
        StegFont          f(8.0f);
        Inspelning        rec;
        glaze::Ctx        c = ctxFör(f, &rec, 1.0f, {0, 0, 400, 300});
        c.ui                = ui;
        c.style             = &glaze::defaultStyle(ui);
        c.theme             = &glaze::defaultTheme(ui).light;
        const auto& m       = *c.style;
        const auto& col     = c.theme->colors;

        Prov l;
        l.width  = 240;
        l.height = 200;
        l.addColumn("Namn", 120);
        l.addColumn("Nr", 60);
        l.addRow({"a", "1"}, {}, röd);
        l.addRow({"b", "2"});
        l.addRow({"c", "3"});
        l.setCellColor(0, 1, blå, grön);          // bakgrund + text
        l.setCellColor(0, 0, gul, std::nullopt);  // bara bakgrund
        c.box          = l.resolve(c);
        const float x0 = c.box.x + m.panelInset;
        const float y0 = c.box.y + m.panelInset + m.tableHeaderH;
        l.draw(c);

        check(rec.färgPå("1") == grön && rec.färgPå("a") == röd &&
                  rec.färgPå("b") == col.text && rec.färgPå("2") == col.text,
              "P1: cellens textfärg slår radens, radens slår temats" + n,
              färgText(rec.färgPå("1")));
        const Inspelning::Yta* yb = rec.yta(blå);
        const Inspelning::Yta* yg = rec.yta(gul);
        check(yb && nara(yb->r.x, x0 + 120.0f) && nara(yb->r.y, y0) &&
                  nara(yb->r.w, 60.0f) && nara(yb->r.h, m.listRowH) &&
                  rec.antalYtor(blå) == 1,
              "P2: bakgrunden fyller EXAKT cellen (rad 0, kolumn 1)" + n);
        check(yg && nara(yg->r.x, x0) && nara(yg->r.y, y0) &&
                  nara(yg->r.w, 120.0f) && nara(yg->r.h, m.listRowH),
              "P2: en cell med bara bakgrund — rad 0, kolumn 0" + n);
        check(rec.färgPå("a") == röd,
              "P3: cellens utelämnade text är radens färg" + n);

        rec.rensa();
        l.setSelectedIndex(0);
        l.draw(c);
        check(!rec.yta(blå) && !rec.yta(gul) &&
                  rec.färgPå("1") == valdFärg(ui, col) &&
                  rec.färgPå("a") == valdFärg(ui, col),
              "P4: en VALD rad visar bara markeringens färger — markeringen "
              "vinner över cellens bakgrund och text" + n,
              färgText(rec.färgPå("1")));
        l.setSelectedIndex(-1);

        rec.rensa();
        l.disable();
        l.draw(c);
        check(!rec.yta(blå) && !rec.yta(gul) &&
                  rec.färgPå("1") == col.textDim,
              "P5: en INAKTIV tabell visar inga appfärger" + n,
              färgText(rec.färgPå("1")));
        l.enable();

        // Sorterad fallande: c · b · a — raden med färgerna ligger sist.
        rec.rensa();
        l.setSortColumn(0);
        l.setSortAscending(false);
        l.draw(c);
        const Inspelning::Yta* yb2 = rec.yta(blå);
        const Inspelning::Yta* yg2 = rec.yta(gul);
        check(yb2 && yg2 && nara(yb2->r.y, y0 + 2.0f * m.listRowH) &&
                  nara(yg2->r.y, y0 + 2.0f * m.listRowH) &&
                  rec.färgPå("1") == grön,
              "P6: cellfärgerna följer RADEN genom sorteringen" + n);
        l.setSortColumn(-1);
    }

    Prov l;
    l.addColumn("Namn");
    l.addRow({"a"});
    l.addRow({"b"});
    l.setCellColor(0, 0, blå, grön);
    check(l.cellBackground(0, 0) == blå && l.cellTextColor(0, 0) == grön,
          "P7: getterna ger de satta färgerna");
    l.setCellColor(0, 0, gul, std::nullopt);
    check(l.cellBackground(0, 0) == gul && !l.cellTextColor(0, 0),
          "P7: tilldelning — en utelämnad färg nollas");
    l.setCell(0, 0, "a2");
    check(l.cellBackground(0, 0) == gul, "P7: setCell lämnar cellfärgerna");
    l.setRow(0, {"a3"});
    check(!l.cellBackground(0, 0) && !l.cellTextColor(0, 0),
          "P7: setRow är tilldelning — cellfärgerna nollas");
    l.setCellColor(1, 3, blå, std::nullopt);   // kolumn 3 finns inte än
    check(l.cellBackground(1, 3) == blå,
          "P7: kolumnen behöver inte finnas (som setCell)");
    l.setCellColor(5, 0, blå, grön);
    l.setCellColor(0, -1, blå, grön);
    check(l.rowCount() == 2 && !l.cellBackground(5, 0) &&
              !l.cellTextColor(0, -1),
          "P7: raden eller kolumnen utanför — no-op, och getterna är tomma");
    l.setCellColor(1, 9, std::nullopt, std::nullopt);
    check(!l.cellBackground(1, 9), "P7: tomt på en cell utan färg ändrar inget");
    l.setRows({{{"x"}}, {{"y"}}});
    check(!l.cellBackground(1, 3), "P7: setRows nollar cellfärgerna");
}

// R1–R9. RADRUBRIKER: första kolumnen är fast — geometrin, klippet
// som håller de rullande cellerna borta från remsan, remsans yta och
// hårlinjer, träffen, och att valet aldrig tonar remsan.
void fallRadrubriker()
{
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string n = uiNamn(ui);
        StegFont          f(8.0f);
        Inspelning        rec;
        glaze::Ctx        c = ctxFör(f, &rec, 1.0f, {0, 0, 400, 300});
        c.ui                = ui;
        c.style             = &glaze::defaultStyle(ui);
        c.theme             = &glaze::defaultTheme(ui).light;
        const auto& m       = *c.style;
        const auto& col     = c.theme->colors;

        Prov l;
        l.width  = 140;
        l.height = 200;
        l.addColumn("Rad", 60);
        l.addColumn("A", 60);
        l.addColumn("B", 60);
        l.addRow({"rad1", "x1", "y1"});
        l.addRow({"rad2", "x2", "y2"});
        c.box              = l.resolve(c);
        const float inW    = 140.0f - 2.0f * m.panelInset;
        const float x0     = c.box.x + m.panelInset;
        const float y0     = c.box.y + m.panelInset + m.tableHeaderH;
        const float botten = c.box.y + 200.0f - m.panelInset;
        const float pad    = m.listPadX;

        check(nara(l.frozenW(c), 0.0f),
              "R1: utan radrubriker är ingen kolumn fast" + n);
        const gfx::Sizef utan = l.scrollRange(c);
        l.setRowHeaders(true);
        check(l.rowHeaders() && nara(l.frozenW(c), 60.0f),
              "R2: den fasta kolumnen är kolumn 0, hela dess bredd" + n);
        const gfx::Rectf vp = l.scrollViewport(c);
        check(nara(vp.x, m.panelInset + 60.0f) && nara(vp.w, inW - 60.0f),
              "R3: rullningsytan börjar till höger om remsan" + n,
              tal(vp.x) + " / " + tal(vp.w));
        const gfx::Sizef med = l.scrollRange(c);
        check(nara(med.w, 180.0f - inW) && nara(utan.w, med.w),
              "R4: spelrummet i sidled är oförändrat (kolumnerna − bredden)" +
                  n,
              tal(med.w) + " mot " + tal(utan.w));

        l.setScroll(1000.0f, 0.0f);
        const float s = l.scrollRange(c).w;
        check(nara(l.cellRect(c, 0, 0).x, m.panelInset) &&
                  nara(l.cellRect(c, 0, 2).x, m.panelInset + 120.0f - s),
              "R5: rullad till slutet står kolumn 0 kvar medan kolumn 2 rullar" +
                  n,
              tal(l.cellRect(c, 0, 0).x));

        l.draw(c);
        const Inspelning::Text* r1 = rec.text("rad1");
        const Inspelning::Text* x1 = rec.text("x1");
        const Inspelning::Text* hA = rec.text("A");
        const Inspelning::Text* hR = rec.text("Rad");
        check(r1 && nara(r1->x, x0 + pad),
              "R6: radnamnet står kvar vid rullning" + n);
        check(x1 && x1->harKlipp && x1->klipp.x >= x0 + 60.0f - 0.01f,
              "R6: en rullande cell klipps mot ytan till höger om remsan" + n);
        check(hA && hA->harKlipp && hA->klipp.x >= x0 + 60.0f - 0.01f &&
                  hR && nara(hR->x, x0 + pad),
              "R6: en rullande rubrik klipps likadant, den fasta står kvar" + n);

        bool remsaYta = false;
        for (const Inspelning::Yta& y : rec.ytor)
            remsaYta = remsaYta || (y.färg == col.windowBg &&
                                    nara(y.r.x, x0) && nara(y.r.w, 60.0f));
        check(remsaYta == (ui != glaze::UI::Minimalistic),
              "R7: remsan har huvudets yta — windowBg i Slate och Glaze, ingen i "
              "Minimalistic" + n);
        const gfx::Color linje =
            ui == glaze::UI::Glaze ? col.buttonBorder : col.border;
        check(rec.vagratt(y0 + m.listRowH, x0, x0 + 60.0f) &&
                  rec.vagratt(y0 + 2.0f * m.listRowH, x0, x0 + 60.0f) &&
                  rec.lodratt(x0 + 60.0f, y0, botten) &&
                  rec.streckI(linje) > 0,
              "R7: en hårlinje under varje radnamn och en vid remsans högerkant, "
              "i huvudets linjefärg" + n);

        check(l.hitPosition(c, x0 + 10.0f, y0 + 5.0f) == 0 &&
                  l.hitPosition(c, x0 + 10.0f, y0 + m.listRowH + 5.0f) == 1,
              "R8: remsan är radens del — ett klick på namnet träffar raden "
              "också rullad i sidled" + n);

        rec.rensa();
        l.setSelectedIndex(0);
        l.draw(c);
        bool bandIRemsan = false, streck = false;
        for (const Inspelning::Yta& y : rec.ytor) {
            if (y.färg == col.accent && y.harKlipp &&
                y.klipp.x < x0 + 60.0f - 0.01f && !nara(y.r.w, m.rowMarkerW))
                bandIRemsan = true;   // ett valband som syns i remsan
            if (y.färg == col.accent && nara(y.r.x, x0) &&
                nara(y.r.w, m.rowMarkerW) && y.harKlipp &&
                y.klipp.x < x0 + 0.01f)
                streck = true;        // Minimalistics streck, synligt i remsan
        }
        check(rec.färgPå("rad1") == col.text && !bandIRemsan &&
                  streck == (ui == glaze::UI::Minimalistic),
              "R9: valet tonar aldrig remsan — Minimalistics streck står kvar "
              "vid vänsterkanten" + n);
    }
}

// A1–A8 och D1–D5. BREDD EFTER INNEHÅLLET: en autoWidth-kolumn
// följer rubriken (bredaste `\n`-raden, sorteringspilens plats alltid
// reserverad) och cellerna (kolumn 0 med zonen och ikonen), aldrig smalare än
// `columnMinW`; en fast bredd rörs inte; dubbelklicket på avdelaren skriver
// den anpassade bredden som en fast bredd och avslutar draget.
void fallBreddEfterInnehall()
{
    StegFont   f(8.0f);
    Inspelning rec;
    glaze::Ctx c   = ctxFör(f, &rec, 1.0f, {0, 0, 800, 400});
    const auto& m  = *c.style;
    const float pad = m.listPadX;
    const float pil = pad + 2.0f * m.chevronW;
    const float automatisk = glaze::ColumnPanel::Column::autoWidth;
    // Andrummet: en hårlinje, 1 FYSISK px — ritningens kortning jämför strikt,
    // och (mått + luft) − luft ger inte alltid måttet tillbaka i flyttal.
    const float lucka = 1.0f / c.scale;
    // Förväntat: rubrikens tecken, cellernas tecken (0 = inga), extra före cellens
    // text, och om pilen är reserverad.
    const auto facit = [&](int rubrik, int celler, float extra, bool pilen) {
        const float r = 8.0f * float(rubrik) + 2.0f * pad +
                        (pilen ? pil : 0.0f) + lucka;
        const float k = celler > 0
                            ? 8.0f * float(celler) + extra + 2.0f * pad + lucka
                            : 0.0f;
        return std::max({m.columnMinW, r, k});
    };

    Prov l;
    l.width  = 400;
    l.height = 200;
    l.addColumn("Namn", automatisk);
    l.addColumn("B", 70);
    l.addColumn("C");
    l.addRow({"kort", "x", "y"});
    l.addRow({"ett längre namn", "y", "z"});   // 15 tecken
    check(nara(l.columnW(c, 0), facit(4, 15, 0.0f, true)),
          "A1: autoWidth = det bredaste av rubriken och cellerna, med luft",
          tal(l.columnW(c, 0)) + " mot " + tal(facit(4, 15, 0.0f, true)));
    check(nara(l.columnW(c, 1), 70.0f) &&
              nara(l.columnW(c, 2), m.columnDefaultW),
          "A2: en fast bredd och standardbredden rörs inte");
    check(l.columnWidth(0) == automatisk, "A2: columnWidth ger det SATTA värdet");

    l.setCell(0, 0, "ett mycket mycket längre namn!");   // 30 tecken
    check(nara(l.columnW(c, 0), facit(4, 30, 0.0f, true)),
          "A3: bredden följer en ändrad cell");
    l.addRow({"x", "y", "z"});
    l.setRow(0, {"ett namn", "x", "y"});                // 8 tecken
    check(nara(l.columnW(c, 0), facit(4, 15, 0.0f, true)),
          "A3: bredden följer setRow (den längsta är nu 15)");
    l.setRows({});
    check(nara(l.columnW(c, 0), facit(4, 0, 0.0f, true)),
          "A3: utan rader följer bredden rubriken");

    l.setColumn(0, "ab\nlång rubrik", automatisk);   // bredaste raden 11
    check(nara(l.columnW(c, 0), facit(11, 0, 0.0f, true)),
          "A4: en rubrik på flera rader — den bredaste raden räknas");
    l.setColumn(0, "Namn", automatisk);

    l.setSortColumn(0);
    check(nara(l.columnW(c, 0), facit(4, 0, 0.0f, true)),
          "A5: ett sorterat huvud ändrar ingen bredd (pilen är reserverad)");
    l.setSortColumn(-1);
    l.setSortable(false);
    check(nara(l.columnW(c, 0), facit(4, 0, 0.0f, false)),
          "A5: utan sorteringsbart huvud reserveras ingen pil");
    l.setSortColumn(0);
    check(nara(l.columnW(c, 0), facit(4, 0, 0.0f, true)),
          "A5: men en programmatiskt sorterad kolumn visar pilen");
    l.setSortColumn(-1);
    l.setSortable(true);

    // Kolumn 0:s innehåll: kryssrutans zon och ikonen före texten.
    l.addRow({"ett längre namn", "", ""});   // 15 tecken
    l.setShowChecks(true);
    check(nara(l.columnW(c, 0),
               facit(4, 15, m.checkSize + m.checkGap, true)),
          "A6: kryssrutans zon räknas i kolumn 0:s innehåll");
    l.setShowChecks(false);
    l.setIconSize(16.0f);
    check(nara(l.columnW(c, 0), facit(4, 15, 16.0f + m.iconGap, true)),
          "A6: den satta ikonstorleken räknas i kolumn 0:s innehåll");
    l.setIconSize(0.0f);
    check(nara(l.columnW(c, 0), facit(4, 15, 0.0f, true)),
          "A6: och försvinner igen med ikonstorleken");

    // Fyllkolumnen kan vara auto: den anpassade bredden är dess minsta.
    l.setFillColumn(0);
    l.width = 150;
    c.box   = l.resolve(c);
    check(nara(l.columnW(c, 0), facit(4, 15, 0.0f, true)),
          "A7: en auto-fyllkolumn i en smal lista — den anpassade bredden är "
          "minimum");
    l.width = 700;
    c.box   = l.resolve(c);
    check(l.columnW(c, 0) > facit(4, 15, 0.0f, true) + 1.0f,
          "A7: …och i en bred lista tar den upp resten");
    l.setFillColumn(-1);

    // ---- D. Dubbelklicket på avdelaren.
    Prov d;
    d.width  = 400;
    d.height = 200;
    d.addColumn("Namn", 80);
    d.addColumn("B", 60);
    d.addRow({"ett mycket långt innehåll", "x"});   // 25 tecken
    c.box          = d.resolve(c);
    const float hy = c.box.y + m.panelInset + m.tableHeaderH / 2.0f;
    const float kant = c.box.x + m.panelInset + 80.0f;
    const float fast = facit(4, 25, 0.0f, true);
    d.mouseDown(c, kant, hy);
    d.mouseDoubleClick(c, kant, hy);
    check(nara(d.columnWidth(0), fast),
          "D1: dubbelklick på avdelaren skriver den anpassade bredden",
          tal(d.columnWidth(0)) + " mot " + tal(fast));
    d.mouseDrag(c, kant + 40.0f, hy);
    check(nara(d.columnWidth(0), fast),
          "D2: draget som det andra trycket började är avslutat — en "
          "musrörelse skriver inte över bredden");
    d.mouseUp(c, kant + 40.0f, hy);
    check(d.sortColumn() == -1,
          "D3: ett tryck på en avdelare sorterar inte");
    d.addRow({"ett mycket mycket mycket långt innehåll alls", "x"});
    check(nara(d.columnWidth(0), fast) && nara(d.columnW(c, 0), fast),
          "D4: bredden är FAST efteråt — den följer inte längre innehållet");

    Prov e;
    e.width  = 400;
    e.height = 200;
    e.addColumn("Namn", automatisk);
    e.addRow({"abcdefgh"});
    c.box = e.resolve(c);
    const float ekant = c.box.x + m.panelInset + e.columnW(c, 0);
    e.mouseDown(c, ekant, hy);
    e.mouseDoubleClick(c, ekant, hy);
    e.mouseUp(c, ekant, hy);
    e.addRow({"abcdefghijklmnopqrstuvwxyz"});
    check(e.columnWidth(0) > 0.0f &&
              nara(e.columnWidth(0), facit(4, 8, 0.0f, true)),
          "D5: en autoWidth-kolumn blir en FAST bredd vid dubbelklicket");

    Prov g;
    g.width  = 400;
    g.height = 200;
    g.addColumn("Namn", 80);
    g.addRow({"ett mycket långt innehåll"});
    c.box = g.resolve(c);
    g.mouseDown(c, c.box.x + m.panelInset + 20.0f, hy);
    g.mouseDoubleClick(c, c.box.x + m.panelInset + 20.0f, hy);
    g.mouseUp(c, c.box.x + m.panelInset + 20.0f, hy);
    check(nara(g.columnWidth(0), 80.0f),
          "D6: dubbelklick på en rubrik (inte en avdelare) ändrar ingen bredd");
}

// ============================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Prov             lista;
    glaze::TextInput annat;   // ett andra stopp — Tab ska kunna lämna listan

    Vard() : glaze::AppWindow("listview_gate", 480, 340, gfx::Chrome::None)
    {
        lista.x      = 10;
        lista.y      = 10;
        lista.width  = 300;
        lista.height = 200;
        lista.name   = "lista";
        lista.addColumn("Namn", 120);
        lista.addColumn("Storlek", 70, glaze::Align::Right);
        lista.addColumn("Typ", 80);
        for (int i = 0; i < 30; ++i)
            lista.addRow({"fil" + std::to_string(i), std::to_string(i),
                          i % 2 ? "Text" : "Bild"});
        root().adopt(lista);

        annat.x     = 330;
        annat.y     = 10;
        annat.width = 120;
        annat.name  = "annat";
        root().adopt(annat);
    }

    // ---- KONTRAKTET, i grindens egen aritmetik (se filhuvudet).
    float skala() { return window().uiScale(); }
    gfx::Rectf yta() { return lista.resolve(lista.behaviorCtx()); }
    gfx::Rectf inre()
    {
        const gfx::Rectf r = yta();
        const float      d = style().panelInset * skala();   // ramen
        return gfx::Rectf{r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d};
    }
    // Huvudets höjd för `rader` rader: `tableHeaderH` och en
    // radhöjd till per rad därutöver — FONTENS radhöjd ur fönstrets egen
    // font, aldrig ur tabellens geometri.
    int   rader = 1;
    float huvudH()
    {
        const float extra =
            rader > 1 ? float(rader - 1) *
                            glaze::Draw::lineHeight(lista.behaviorCtx())
                      : 0.0f;
        return (style().tableHeaderH + extra) * skala();
    }
    float radH() { return style().listRowH * skala(); }
    // Mitten av PLATS p (oscrollat).
    gfx::Pointf rad(int p, float x = 30.0f)
    {
        const gfx::Rectf in = inre();
        return gfx::Pointf{in.x + x * skala(),
                           in.y + huvudH() + (float(p) + 0.5f) * radH()};
    }
    // Mitten av rubriken för kolumn k, och kolumnens HÖGRA kant.
    float kantX(int k)
    {
        float x = inre().x;
        for (int i = 0; i <= k; ++i)
            x += lista.columnWidth(i) * skala();
        return x;
    }
    gfx::Pointf rubrik(int k)
    {
        const float v = k == 0 ? inre().x : kantX(k - 1);
        return gfx::Pointf{(v + kantX(k)) / 2.0f, inre().y + huvudH() / 2.0f};
    }
    // KRYSSRUTANS mitt och en punkt i TEXTEN på plats p när rutorna syns
    // ur stilen (`listPadX`, `checkSize`, `checkGap`),
    // aldrig ur listvyns `checkRect`. Oscrollat, som `rad`.
    gfx::Pointf zon(int p)
    {
        return rad(p, style().listPadX + style().checkSize / 2.0f);
    }
    gfx::Pointf radText(int p)
    {
        return rad(p, style().listPadX + style().checkSize +
                          style().checkGap + 10.0f);
    }

    void mus(gfx::EventType t, float x, float y, std::uint32_t mods = 0,
             float hjul = 0.0f)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        e.mods     = mods;
        e.wheelY   = hjul;
        handleEvent(e);
    }
    void klick(gfx::Pointf p, std::uint32_t mods = 0)
    {
        mus(gfx::EventType::MouseButtonDown, p.x, p.y, mods);
        mus(gfx::EventType::MouseButtonUp, p.x, p.y, mods);
    }
    // HÖGERTRYCKET — fönstrets riktiga väg.
    void hoger(gfx::Pointf p)
    {
        for (const gfx::EventType typ :
             {gfx::EventType::MouseButtonDown, gfx::EventType::MouseButtonUp}) {
            gfx::Event e;
            e.type     = typ;
            e.windowId = window().id();
            e.mouseX   = p.x;
            e.mouseY   = p.y;
            e.button   = gfx::MouseButton::Right;
            handleEvent(e);
        }
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
};

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        Prov& l = w.lista;

        // ---- F1. KLICK på en rad väljer raden på den PLATSEN (förväntat ur
        //          kontraktet: ram + huvud + p × radhöjd).
        w.klick(w.rad(3));
        check(l.selectedIndex() == 3 && l.currentRow() == 3 &&
                  l.ändringar == 1 && l.focused(),
              "F1: klick på plats 3 väljer rad 3, fokus till listan",
              std::to_string(l.selectedIndex()));

        // ---- F2. HUVUDET väljer inget.
        w.klick(w.rubrik(2));
        check(l.selectedIndex() == 3 && l.ändringar == 1,
              "F2: klick i huvudet rör inte valet");
        check(l.sortColumn() == 2 && l.isSortAscending(),
              "F2: …men sorterar på kolumnen (stigande)",
              std::to_string(l.sortColumn()));
        w.klick(w.rubrik(2));
        check(l.sortColumn() == 2 && !l.isSortAscending(),
              "F2: samma rubrik igen vänder riktningen");
        check(l.selectedIndex() == 3,
              "F2: valet följer RADEN genom sorteringen");

        // ---- F3. RULLNINGSYTAN är raderna under huvudet (Panel-kroken).
        const glaze::Ctx  lc     = l.behaviorCtx();
        const float       vyH    = (w.inre().h - w.huvudH()) / w.skala();
        const float       väntat = 30.0f * w.style().listRowH - vyH;
        check(std::abs(l.scrollRange(lc).h - väntat) < 0.01f,
              "F3: spelrummet = raderna − ytan UNDER huvudet",
              std::to_string(l.scrollRange(lc).h) + " mot " +
                  std::to_string(väntat));
        const gfx::Rectf in = w.inre();
        check(!l.inScrollStrip(lc, in.x + in.w - 2.0f,
                               in.y + w.huvudH() / 2.0f) &&
                  l.inScrollStrip(lc, in.x + in.w - 2.0f,
                                  in.y + w.huvudH() + w.radH()),
              "F3: den lodräta remsan börjar UNDER huvudet");

        // ---- F4. SLÄPP över en ANNAN rubrik sorterar inte (dra bort =
        //          ångra, en knapps semantik).
        const gfx::Pointf r0 = w.rubrik(0), r1 = w.rubrik(1);
        w.mus(gfx::EventType::MouseButtonDown, r0.x, r0.y);
        w.mus(gfx::EventType::MouseMotion, r1.x, r1.y);
        w.mus(gfx::EventType::MouseButtonUp, r1.x, r1.y);
        check(l.sortColumn() == 2,
              "F4: tryck på en rubrik, släpp på en annan — ingen sortering",
              std::to_string(l.sortColumn()));
        l.setSortable(false);
        w.klick(w.rubrik(0));
        check(l.sortColumn() == 2, "F4: sortable av — klick sorterar inte");
        l.setSortable(true);
        l.setSortColumn(-1);

        // ---- F5. AVDELAREN: dra kolumn 0:s högra kant.
        const float kx = w.kantX(0);
        const float hy = w.inre().y + w.huvudH() / 2.0f;
        check(l.cursorAt(lc, kx + 1.0f, hy) == gfx::PointerShape::ResizeWE &&
                  l.cursorAt(lc, w.rubrik(0).x, hy) ==
                      gfx::PointerShape::Arrow,
              "F5: ↔ över avdelaren, pil över rubriken");
        w.mus(gfx::EventType::MouseButtonDown, kx + 1.0f, hy);
        w.mus(gfx::EventType::MouseMotion, kx + 1.0f + 30.0f * w.skala(), hy);
        check(std::abs(l.columnWidth(0) - 150.0f) < 0.01f,
              "F5: dragen 30 px — bredden följer (120 → 150)",
              std::to_string(l.columnWidth(0)));
        check(l.cursorAt(lc, w.rubrik(1).x, hy) == gfx::PointerShape::ResizeWE,
              "F5: ↔ under HELA draget, även när pekaren hunnit före");
        w.mus(gfx::EventType::MouseMotion, kx - 500.0f, hy);
        w.mus(gfx::EventType::MouseButtonUp, kx - 500.0f, hy);
        check(std::abs(l.columnWidth(0) - w.style().columnMinW) < 0.01f,
              "F5: aldrig smalare än columnMinW",
              std::to_string(l.columnWidth(0)));
        check(l.sortColumn() == -1, "F5: ett avdelardrag sorterar inte");
        l.setColumnWidth(0, 120);

        // ---- F6. TANGENTERNA (listan har fokus sedan F1).
        l.setSelectedIndex(3);
        w.tangent(gfx::Key::Down);
        check(l.selectedIndex() == 4, "F6: Ned");
        w.tangent(gfx::Key::Up);
        w.tangent(gfx::Key::Up);
        check(l.selectedIndex() == 2, "F6: Upp ×2");
        w.tangent(gfx::Key::End);
        check(l.selectedIndex() == 29 &&
                  std::abs(l.scrollY() - l.scrollRange(lc).h) < 0.01f,
              "F6: End — sista raden, rullad i synfältet",
              std::to_string(l.scrollY()));
        w.tangent(gfx::Key::Home);
        check(l.selectedIndex() == 0 && l.scrollY() == 0.0f,
              "F6: Home — första raden, rullad tillbaka");
        const int sida = int(vyH / w.style().listRowH);
        w.tangent(gfx::Key::PageDown);
        check(l.selectedIndex() == sida,
              "F6: PageDown flyttar en SIDA (" + std::to_string(sida) +
                  " hela rader)", std::to_string(l.selectedIndex()));
        w.tangent(gfx::Key::PageUp);
        check(l.selectedIndex() == 0, "F6: PageUp tillbaka");
        w.tangent(gfx::Key::KP_End);
        check(l.selectedIndex() == 29,
              "F6: numpadens End (viks av AppWindow) når listan");
        const int före = l.ändringar;
        w.tangent(gfx::Key::Down, gfx::kModCtrl);
        check(l.selectedIndex() == 29 && l.ändringar == före,
              "F6: Ctrl+Ned utan flerval tas inte av listan");
        w.tangent(gfx::Key::Return);
        check(l.aktiveringar == 1, "F6: Enter AKTIVERAR den aktuella raden");

        // ---- F7. DUBBELKLICKET — ramverkets räkning (AppWindow).
        l.setScroll(0, 0);
        w.klick(w.rad(5));
        w.klick(w.rad(5));
        check(l.aktiveringar == 2 && l.currentRow() == 5,
              "F7: två snabba klick på samma rad — ETT dubbelklick, aktiverat",
              std::to_string(l.aktiveringar));
        w.klick(w.rad(5));
        check(l.aktiveringar == 2,
              "F7: det tredje klicket börjar en NY följd (inget andra "
              "dubbelklick)");
        w.klick(w.rad(5));
        check(l.aktiveringar == 3, "F7: …och det fjärde fullbordar den");
        w.klick(w.rad(6));
        std::this_thread::sleep_for(
            std::chrono::milliseconds(gfx::doubleClickMs() + 150));
        w.klick(w.rad(6));
        check(l.aktiveringar == 3,
              "F7: två klick med en paus LÄNGRE än OS:ets dubbelklickstid — "
              "inget dubbelklick");
        w.klick(w.rad(7));
        w.klick(w.rad(8));
        check(l.aktiveringar == 3,
              "F7: två snabba klick på OLIKA rader — inget dubbelklick");
        w.klick(w.rubrik(0));
        w.klick(w.rubrik(0));
        check(l.aktiveringar == 3,
              "F7: dubbelklick i huvudet aktiverar ingen rad");
        l.setSortColumn(-1);

        // ---- F8. FLERVAL med musens MODIFIERARE (fasaden → AppWindow::
        //          modifierKeys → listvyn).
        l.setMultiSelect(true);
        // Sorteringen i F7 rullade den aktuella raden i synfältet — de förväntade värdena
        // räknar oscrollat.
        l.setScroll(0, 0);
        w.klick(w.rad(1));
        w.klick(w.rad(3), gfx::kModCtrl);
        check(l.selectedRows() == std::vector<int>{1, 3},
              "F8: Ctrl+klick lägger till raden (musens modifierare)",
              fog(l.selectedRows()));
        w.klick(w.rad(5), gfx::kModShift);
        check(l.selectedRows() == std::vector<int>{3, 4, 5},
              "F8: Shift+klick — spannet från ankaret (rad 3)",
              fog(l.selectedRows()));
        w.tangent(gfx::Key::Down, gfx::kModShift);
        check(l.selectedRows() == std::vector<int>{3, 4, 5, 6},
              "F8: Shift+Ned utökar spannet", fog(l.selectedRows()));
        w.tangent(gfx::Key::Down, gfx::kModCtrl);
        w.tangent(gfx::Key::Space, gfx::kModCtrl);
        check(l.selectedRows() == std::vector<int>{3, 4, 5, 6, 7} &&
                  l.currentRow() == 7,
              "F8: Ctrl+Ned flyttar, Ctrl+Space vänder", fog(l.selectedRows()));
        w.tangent(gfx::Key::A, gfx::kModCtrl);
        check(l.selectedRows().size() == 30, "F8: Ctrl+A väljer alla");
        l.setMultiSelect(false);

        // ---- F9. HJULET rullar i RADER och bubblar i kanten.
        l.setScroll(0, 0);
        const gfx::Pointf mitt = w.rad(2);
        w.mus(gfx::EventType::MouseWheel, mitt.x, mitt.y, 0, -1.0f);
        check(std::abs(l.scrollY() - w.style().listRowH) < 0.01f,
              "F9: ett hjulsteg ner = en rad", std::to_string(l.scrollY()));

        // ---- F10. INAKTIVERAD — ingen rad väljs.
        l.setSelectedIndex(0);
        l.disable();
        w.klick(w.rad(2));
        check(l.selectedIndex() == 0, "F10: inaktiverad lista tar inga klick");
        l.enable();

        // ---- F11. DET HÖGA HUVUDET — fyra rader. De förväntade värdena följer
        //           med (`w.rader`): rubrikklicket, avdelarens grepp och ↔
        //           ska gälla över HELA huvudets höjd, och rad 0 börjar
        //           under det.
        l.setScroll(0, 0);
        l.setSortColumn(-1);
        l.setSelectedIndex(5);   // programmatiskt: klicket på rad 0 ÄNDRAR
        l.setHeaderLines(4);
        w.rader                = 4;
        const gfx::Rectf in4   = w.inre();
        const float      lågt  = in4.y + w.huvudH() - 3.0f;   // nära underkanten
        w.klick(gfx::Pointf{w.rubrik(1).x, lågt});
        check(l.sortColumn() == 1,
              "F11: ett klick i huvudets NEDRE del sorterar på kolumnen",
              std::to_string(l.sortColumn()));
        const float kx11 = w.kantX(0);
        check(l.cursorAt(lc, kx11 + 1.0f, lågt) ==
                  gfx::PointerShape::ResizeWE,
              "F11: ↔ över avdelaren även längst ner i huvudet");
        w.mus(gfx::EventType::MouseButtonDown, kx11 + 1.0f, lågt);
        w.mus(gfx::EventType::MouseMotion, kx11 + 1.0f + 20.0f * w.skala(),
              lågt);
        w.mus(gfx::EventType::MouseButtonUp, kx11 + 1.0f + 20.0f * w.skala(),
              lågt);
        check(std::abs(l.columnWidth(0) - 140.0f) < 0.01f &&
                  l.sortColumn() == 1,
              "F11: avdelaren dras från huvudets nedre del (120 → 140), "
              "utan att sortera", std::to_string(l.columnWidth(0)));
        l.setColumnWidth(0, 120);
        const int före11 = l.ändringar;
        l.setSortable(false);   // klicket nedan är i huvudet — inget sorterat
        w.klick(gfx::Pointf{w.rad(0).x, in4.y + w.huvudH() - 1.0f});
        check(l.ändringar == före11,
              "F11: strax ovanför huvudets underkant väljs ingen rad");
        l.setSortable(true);
        l.setSortColumn(-1);
        // Sorteringen ovan rullade den aktuella raden i synfältet (det höga
        // huvudet ger färre synliga rader) — de förväntade värdena räknar oscrollat.
        l.setScroll(0, 0);
        w.klick(w.rad(0));
        check(l.selectedIndex() == 0 && l.ändringar == före11 + 1,
              "F11: rad 0 börjar UNDER det höga huvudet (facit ur kontraktet)",
              std::to_string(l.selectedIndex()));
        w.klick(w.rad(2));
        check(l.selectedIndex() == 2, "F11: och rad 2 två radhöjder längre ned",
              std::to_string(l.selectedIndex()));
        const float vyH4   = (w.inre().h - w.huvudH()) / w.skala();
        const float väntat4 = 30.0f * w.style().listRowH - vyH4;
        check(std::abs(l.scrollRange(lc).h - väntat4) < 0.01f,
              "F11: spelrummet räknas mot ytan under det HÖGA huvudet",
              std::to_string(l.scrollRange(lc).h) + " mot " +
                  std::to_string(väntat4));
        l.setHeaderLines(1);
        w.rader = 1;

        // ---- F12. TRYCK PÅ EN VALD RAD i flerval:
        //           inget vid trycket — solo och ETT onChange vid släppet.
        //           Plats 3, inte 2: F11:s sista klick var på plats 2, och ett
        //           nytt tryck där hade varit ett dubbelklick.
        l.setMultiSelect(true);
        l.setSortColumn(-1);
        l.setScroll(0, 0);
        l.setSelectedIndex(1);
        l.setSelected(2, true);
        l.setSelected(3, true);
        const int före12 = l.ändringar;
        w.mus(gfx::EventType::MouseButtonDown, w.rad(3).x, w.rad(3).y);
        check(l.selectedRows() == std::vector<int>{1, 2, 3} &&
                  l.currentRow() == 3 && l.ändringar == före12,
              "F12: trycket på en vald rad — raden blir aktuell, valet står "
              "kvar, inget event",
              fog(l.selectedRows()));
        w.mus(gfx::EventType::MouseButtonUp, w.rad(3).x, w.rad(3).y);
        check(l.selectedRows() == std::vector<int>{3} &&
                  l.ändringar == före12 + 1,
              "F12: släppet utan drag — raden ensam, ETT onChange",
              fog(l.selectedRows()));

        // ---- F13. DET VÄNTANDE SOLOVALET NOLLAS: ett gammalt
        //           radindex väljer aldrig en rad efter `setRows`/`setRow`,
        //           och ett tryck som blev ett DRAG väljer inte ensamt.
        l.setSelectedIndex(1);
        l.setSelected(5, true);
        const int före13 = l.ändringar;
        w.mus(gfx::EventType::MouseButtonDown, w.rad(5).x, w.rad(5).y);
        std::vector<glaze::Table::Row> nya;
        for (int i = 0; i < 30; ++i)
            nya.push_back({{"ny" + std::to_string(i), std::to_string(i),
                            "Text"}});
        l.setRows(nya);
        w.mus(gfx::EventType::MouseButtonUp, w.rad(5).x, w.rad(5).y);
        check(l.selectedRows().empty() && l.ändringar == före13,
              "F13: setRows mellan tryck och släpp — släppet väljer INGET",
              fog(l.selectedRows()));
        // Plats 4 och 2 och inte 7 och 9: ytan är 200 px hög och visar 6,6
        // rader under huvudet (26 + 26 px i Slate), så ett tryck på plats 7
        // eller 9 hade hamnat UNDER listan — fallet hade varit grönt utan att
        // trycka på någon rad, och draget nedan hade aldrig startat.
        l.setSelectedIndex(1);
        l.setSelected(4, true);
        w.mus(gfx::EventType::MouseButtonDown, w.rad(4).x, w.rad(4).y);
        l.setRow(4, {"ersatt", "4", "Text"});
        w.mus(gfx::EventType::MouseButtonUp, w.rad(4).x, w.rad(4).y);
        check(l.selectedRows() == std::vector<int>{1, 4} &&
                  l.ändringar == före13,
              "F13: setRow mellan tryck och släpp — inget soloval",
              fog(l.selectedRows()));
        // Ett DRAG av hela valet: appen fyller lasten, och trycket som blev
        // ett drag får ingen mouseUp — valet står kvar.
        l.setAllowDrag(true);
        l.onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("rader");
        };
        l.setSelectedIndex(1);
        l.setSelected(2, true);
        const gfx::Pointf tryck = w.rad(2);
        w.mus(gfx::EventType::MouseButtonDown, tryck.x, tryck.y);
        w.mus(gfx::EventType::MouseMotion,
              tryck.x + w.style().dragThreshold * w.skala() + 4.0f, tryck.y);
        const bool drogs = w.isDragging();
        w.tangent(gfx::Key::Escape);
        w.mus(gfx::EventType::MouseButtonUp,
              tryck.x + w.style().dragThreshold * w.skala() + 4.0f, tryck.y);
        check(drogs && l.selectedRows() == std::vector<int>{1, 2} &&
                  l.ändringar == före13,
              "F13: trycket blev ett drag — inget soloval efteråt",
              std::string(drogs ? "" : "inget drag · ") +
                  fog(l.selectedRows()));
        l.onDragStart = nullptr;
        l.setAllowDrag(false);
        l.setMultiSelect(false);

        // ---- F14. KRYSSEN MED MUSEN: ett klick i
        //           rutans ZON vänder krysset — onCheck och onCommit med
        //           raden i changedRow() — och rör varken valet eller den
        //           aktuella raden. Ett klick i texten väljer och kryssar inte.
        l.setSortColumn(-1);
        l.setScroll(0, 0);
        l.setShowChecks(true);
        int              kryss = 0;
        std::vector<int> kryssRad, klarRad;
        l.onCheck = [&] {
            ++kryss;
            kryssRad.push_back(l.changedRow());
        };
        l.onCommit = [&] { klarRad.push_back(l.changedRow()); };
        l.setSelectedIndex(1);
        const int före14 = l.ändringar;
        w.klick(w.zon(4));
        check(l.isChecked(4) && kryss == 1 &&
                  kryssRad == std::vector<int>{4} &&
                  klarRad == std::vector<int>{4},
              "F14: klick i zonen — onCheck och onCommit med changedRow() 4",
              fog(kryssRad) + " / " + fog(klarRad));
        check(l.selectedRows() == std::vector<int>{1} && l.currentRow() == 1 &&
                  l.ändringar == före14,
              "F14: …och valet och den aktuella raden står kvar",
              fog(l.selectedRows()));
        w.klick(w.radText(2));
        check(!l.isChecked(2) && l.selectedIndex() == 2 &&
                  l.changedRow() == -1 && kryss == 1 &&
                  l.ändringar == före14 + 1 && klarRad.size() == 2 &&
                  klarRad.back() == -1,
              "F14: klick i texten väljer, kryssar inte — changedRow() −1, "
              "också i valets onCommit",
              std::to_string(l.changedRow()));

        // ---- F15. TANGENTERNA (B5 — trädets regel; listan har fokus): Space
        //           vänder den AKTUELLA radens kryss också i enkelval och före
        //           valet, Ctrl+Space vänder valet i flerval och gör ingenting
        //           i enkelval.
        w.tangent(gfx::Key::Space);
        check(l.isChecked(2) && kryss == 2 && l.selectedIndex() == 2 &&
                  l.ändringar == före14 + 1,
              "F15: Space kryssar den aktuella raden i ENKELVAL — valet rörs "
              "inte");
        w.tangent(gfx::Key::Space, gfx::kModCtrl);
        check(l.isChecked(2) && kryss == 2 && l.selectedIndex() == 2 &&
                  l.ändringar == före14 + 1,
              "F15: Ctrl+Space i enkelval ändrar ingenting");
        l.setMultiSelect(true);
        w.tangent(gfx::Key::Space, gfx::kModCtrl);
        check(!l.isSelected(2) && l.isChecked(2) && kryss == 2 &&
                  l.ändringar == före14 + 2,
              "F15: Ctrl+Space i flerval vänder VALET — inte krysset");
        w.tangent(gfx::Key::Space);
        check(!l.isChecked(2) && kryss == 3 && l.ändringar == före14 + 2,
              "F15: Space vänder krysset också i flerval");
        l.setMultiSelect(false);

        // ---- F16. DUBBELKLICK I ZONEN — två vändningar och ingen
        //           aktivering, också när raden är den aktuella (ett
        //           dubbelklick på raden HADE aktiverat den).
        l.setSelectedIndex(5);
        const int akt16 = l.aktiveringar;
        w.klick(w.zon(5));
        w.klick(w.zon(5));   // andra trycket inom OS:ets tid ⇒ dubbelklick
        check(!l.isChecked(5) && kryss == 5 && l.aktiveringar == akt16,
              "F16: dubbelklick i zonen — två vändningar, ingen aktivering",
              std::to_string(l.aktiveringar - akt16) + " aktiveringar");

        // ---- F17. FLERVAL: ett tryck i zonen på en VALD rad vänder krysset
        //           och sätter inget väntande soloval — släppet ändrar inte
        //           valet (`pendingSolo_` nollas överst i trycket).
        l.setMultiSelect(true);
        l.setSelectedIndex(0);
        l.setSelected(1, true);
        l.setSelected(3, true);
        const int ä17 = l.ändringar;
        const gfx::Pointf z17 = w.zon(1);
        w.mus(gfx::EventType::MouseButtonDown, z17.x, z17.y);
        check(l.isChecked(1) && kryss == 6 &&
                  l.selectedRows() == std::vector<int>{0, 1, 3},
              "F17: trycket i zonen på en vald rad — krysset vände, valet "
              "står kvar",
              fog(l.selectedRows()));
        w.mus(gfx::EventType::MouseButtonUp, z17.x, z17.y);
        check(l.selectedRows() == std::vector<int>{0, 1, 3} &&
                  l.ändringar == ä17,
              "F17: …och släppet väljer INTE raden ensam",
              fog(l.selectedRows()));
        l.setMultiSelect(false);

        // ---- F18. VAR DRAGET FÅR BÖRJA (B6, `canDragFrom`): appens
        //           onDragStart fyller lasten och sätter `allowed` — ändå blir
        //           zonen och en kolumnavdelare aldrig ett drag. Texten blir
        //           det.
        l.setAllowDrag(true);
        l.onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("rader");
            e.allowed = glaze::DragEffect::Copy | glaze::DragEffect::Move;
        };
        const float       dra = w.style().dragThreshold * w.skala() + 4.0f;
        const gfx::Pointf z18 = w.zon(3);
        w.mus(gfx::EventType::MouseButtonDown, z18.x, z18.y);
        w.mus(gfx::EventType::MouseMotion, z18.x, z18.y + dra);
        check(!w.isDragging(), "F18: tryck i ZONEN + rörelse — inget drag");
        w.mus(gfx::EventType::MouseButtonUp, z18.x, z18.y + dra);
        const float kx18 = w.kantX(0);
        const float hy18 = w.inre().y + w.huvudH() / 2.0f;
        w.mus(gfx::EventType::MouseButtonDown, kx18 + 1.0f, hy18);
        w.mus(gfx::EventType::MouseMotion, kx18 + 1.0f + 30.0f * w.skala(),
              hy18);
        check(!w.isDragging() && std::abs(l.columnWidth(0) - 150.0f) < 0.01f,
              "F18: en kolumnAVDELARE + rörelse — inget drag, och bredden "
              "följde pekaren (120 → 150)",
              std::to_string(l.columnWidth(0)));
        w.mus(gfx::EventType::MouseButtonUp, kx18 + 1.0f + 30.0f * w.skala(),
              hy18);
        l.setColumnWidth(0, 120);
        const gfx::Pointf t18 = w.radText(4);
        w.mus(gfx::EventType::MouseButtonDown, t18.x, t18.y);
        w.mus(gfx::EventType::MouseMotion, t18.x + dra, t18.y);
        check(w.isDragging(), "F18: tryck i TEXTEN + rörelse — draget startar");
        w.tangent(gfx::Key::Escape);
        w.mus(gfx::EventType::MouseButtonUp, t18.x + dra, t18.y);
        l.onDragStart = nullptr;
        l.setAllowDrag(false);

        // ---- F19. RULLAD I SIDLED: zonen följer kolumn 0 (`cellRect` bär
        //           scrollen) och är klippt till radernas yta. Rullningen
        //           väljs så att zonens orullade mitt hamnar UTANFÖR den
        //           rullade zonen (mer än halva rutan och checkGap), och
        //           klicket på den rullade zonen tas i dess synliga del.
        const float s19 =
            w.style().checkSize / 2.0f + w.style().checkGap + 3.0f;
        const float zonSlut =
            w.style().listPadX + w.style().checkSize + w.style().checkGap;
        l.setColumnWidth(0, 300);   // bredare än ytan — sidledsscroll
        l.setScroll(s19, 0.0f);
        const int k19 = kryss;
        w.klick(w.rad(2, (zonSlut - s19) / 2.0f));
        check(l.isChecked(2) && kryss == k19 + 1,
              "F19: klick på zonens RULLADE plats kryssar raden");
        w.klick(w.zon(2));   // där zonen stod orullad
        check(l.isChecked(2) && kryss == k19 + 1 && l.selectedIndex() == 2,
              "F19: klick där zonen stod orullad VÄLJER — och kryssar inte",
              std::to_string(kryss - k19) + " vändningar");
        l.setScroll(0.0f, 0.0f);
        l.setColumnWidth(0, 120);

        // ---- F20. SORTERAD: efter två klick i huvudet (fallande) står rad
        //           29 på plats 0, och ett klick i zonen där vänder RADEN —
        //           inte index 0.
        l.setSortColumn(-1);
        w.klick(w.rubrik(0));   // stigande — naturligt samma ordning
        w.klick(w.rubrik(0));   // fallande — "ny29" överst
        l.setScroll(0, 0);      // sorteringen rullade den aktuella raden fram
        const int r20 = l.rowAtDisplay(0);
        w.klick(w.zon(0));
        check(r20 == 29 && l.isChecked(29) && !l.isChecked(0),
              "F20: klick i zonen på plats 0 vänder rowAtDisplay(0) (rad 29)",
              std::to_string(r20));
        l.setSortColumn(-1);
        l.onCheck  = nullptr;
        l.onCommit = nullptr;
        l.setShowChecks(false);

        // ---- Q1–Q3 — SNABBMENYN. `rowAt` i LOGISKA
        //       px från listans hörn ger APPENS index, −1 i huvudet; ett
        //       högertryck väljer en ovald rad ensam och lämnar ett val.
        //       SABOTAGE: `rightDown` som svarar true (Q2 — ingen meny).
        {
            l.setScroll(0.0f, 0.0f);
            const auto lokal = [&](gfx::Pointf p) {
                const gfx::Rectf r = w.yta();
                return gfx::Pointf{(p.x - r.x) / w.skala(), (p.y - r.y) / w.skala()};
            };
            const gfx::Pointf p2 = lokal(w.rad(2));
            const gfx::Pointf hu = lokal(w.rubrik(0));
            check(l.rowAt(p2.x, p2.y) == 2 && l.rowAt(hu.x, hu.y) == -1,
                  "Q1: rowAt ger platsens rad, −1 i huvudet",
                  std::to_string(l.rowAt(p2.x, p2.y)));
            l.setSortColumn(0);
            l.setSortAscending(false);
            l.setScroll(0.0f, 0.0f);
            check(l.rowAt(p2.x, p2.y) == l.rowAtDisplay(2) &&
                      l.rowAt(p2.x, p2.y) != 2,
                  "Q1: sorterad — samma punkt ger APPENS index",
                  std::to_string(l.rowAt(p2.x, p2.y)));
            l.setSortColumn(-1);
            l.setScroll(0.0f, 0.0f);

            int menyer = 0, menyRad = -2;
            glaze::Widget* sender = nullptr;
            l.onContextMenu = [&](glaze::Widget* s, glaze::ContextMenuArgs&) {
                ++menyer;
                sender  = s;
                menyRad = l.contextRow();
            };
            l.setMultiSelect(true);
            l.setSelectedIndex(0);
            l.setSelected(1, true);   // 0 och 1 valda
            l.ändringar = 0;
            w.hoger(w.rad(4));
            check(l.selectedRows() == std::vector<int>{4} && l.ändringar == 1 &&
                      menyer == 1 && sender == &l && menyRad == 4,
                  "Q2: en ovald rad väljs ensam, och menyn ser contextRow()",
                  fog({std::to_string(l.selectedRows().size())}));
            l.setSelectedIndex(0);
            l.setSelected(1, true);
            l.ändringar = 0;
            w.hoger(w.rad(1));
            check(l.selectedRows() == std::vector<int>{0, 1} && l.ändringar == 0 &&
                      l.currentRow() == 1 && l.contextRow() == 1,
                  "Q3: en vald rad — valet står, raden blir aktuell");
            w.hoger(w.rubrik(0));
            check(l.contextRow() == -1, "Q3: i huvudet — contextRow() −1");
            l.onContextMenu = nullptr;
            l.setMultiSelect(false);
        }

        w.close();
    });
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("listview_gate\n");
    fallRegistret();
    fallSetProp();
    fallKollektionen();
    fallSorteringen();
    fallValet();
    fallKortningen();
    fallRitningen();
    fallRubrikraderna();
    fallHuvudetsHojd();
    fallJusteringen();
    fallRadfargen();
    fallRadbilden();
    fallKryssen();
    fallFyllOchIkon();
    fallCellfarger();
    fallRadrubriker();
    fallBreddEfterInnehall();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
