// pixel_gate — VARJE widget i galleri-fixturen RITAR NÅGOT
// SYNLIGT, i VARJE UI × tema × läge × stil — mätt i PIXLAR.
//
// En grind som bara mäter widget-TILLSTÅND ser inte pixlar:
// `preview_gate` verifierar att `.glazeui`-satserna når widgeten, men ritar
// aldrig, så en `draw()` som slutade rita passerade grönt. Den här grinden
// renderar `tests/fixtures/gallery.*` genom den RIKTIGA backenden, längs
// glaze-renders väg: modellen läses av `builder_core`, trädet byggs av
// `bld::buildWindowTree` (samma funktion verktyget använder, inte en kopia),
// fönstret är DOLT (`setOffscreen`) och bilden läses med `captureFrame`.
//
// VAD GRINDEN PÅSTÅR — och inget mer:
//
//   1. EGEN RITNING. Varje synlig widget ändrar pixlar i sin egen yta
//      jämfört med förälderns yta UNDER den. Mätt DIFFERENTIELLT, nivå för
//      nivå: bild A har widgetens nivå synlig och alla djupare dolda, bild B
//      har även den nivån dold. Barnen räknas alltså aldrig in — en Panel
//      vars `draw()` slutat rita fäller grinden även om barnen syns, och en
//      färgroll lika med ytan under fäller den precis som en tom `draw()`.
//   2. AKTIVERAT OCH INAKTIVERAT SKILJER SIG. Varje widget som tar
//      tangentfokus (`focusable()`) ser annorlunda ut när tillståndet byts —
//      och verktygsknappen, som inte tar fokus men går att klicka
//      (`måsteVisaAv()`; en ren ikonknapps enda inaktiva tecken är ikonens
//      opacitet, alltså backendens shader). Kriteriet är ramverkets eget:
//      det som går att MANÖVRERA måste visa när det inte går. Tillståndet VÄNDS för alla widgets i ett svep, så
//      en fixtur med inaktiverade noder mäts åt andra hållet.
//   3. TEXTEN SYNS. Varje nod med en `text`-sats i fixturen ändrar pixlar i
//      sin yta när texten töms — jämfört mot en andra fönsterinstans byggd
//      ur samma modell med tomma texter.
//   4. FIXTUREN ÄR UTFÖRD. Varje nod blev en RIKTIG widget (en stand-in
//      hade gjort att grinden mätte ställföreträdaren — regel 8:s "previewn
//      UTFÖR den" är förutsättningen), och varje TYP i fixturen mättes minst
//      en gång (en bortklippt komponent slipper inte tyst undan).
//
// Mätningarna gäller widgets som syns HELT — rect:en oklippt av förfäder
// och fönsterkant. En delvis bortklippt widget kan med rätta visa ingenting
// i sin synliga remsa (en knapp i en rullad panel vars text ligger under
// kanten), och den hade fällt grinden utan att något var fel.
//
// GENERISK: grinden känner inga nodnamn. Trädet vandras ur fönstrets rot,
// så en ny komponent i tests/fixtures/gallery.* täcks utan att en rad här
// ändras — med TVÅ skrivna undantag, båda med sitt skäl vid funktionen: en
// widget som MED FLIT inte ritar något själv (ren layout) står i
// `renLayout()`, och en widget vars `text` ritas av FÖRÄLDERN och inte i
// dess egen yta (fliksidans rubrik) står i `textenHosFörälder()`.
//
// KÄNDA FEL (`kKändaFel`) är fel grinden hittat och som hör
// till det senare utseendearbetet. De rapporteras som KÄNT och fäller inte — men
// listan är SJÄLVUTLÖPANDE: en rad som inte längre slår till är ett FEL,
// så den kan inte ruttna kvar efter att felet rättats.
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · UTSEENDET. Inga referensbilder, inga färgvärden, ingen kontrast utöver
//     "syns": det hör till utseendet och kan inte grindas
//     förrän designen är låst. Paletterna mäts av contrast_gate.
//   · DELAR av en widget. "Ritar något" gäller hela widgeten: en checkbox
//     vars ruta försvunnit men vars text syns passerar.
//   · delvis bortklippta widgets (se ovan) — typen mäts i en annan instans.
//   · hover, tryck, fokus och öppen lista — tillstånd som kräver musen.
//   · FÖNSTRETS INBYGGDA (TopBar, menyrad, statusrad, programikonen) i sin
//     EGEN ritning: att dölja dem flyttar chromet och därmed allt annat.
//     Programikonens ritning mäts i icon_gate (del G). Deras barn mäts
//     som alla andra, och fixturens nästlade TopBar/StatusBar täcker typerna.
//   · en Panels overlay-remsor (`drawAfterChildren`) ritas OVANPÅ barnen och
//     räknas inte till någon; syskon som överlappar varandra delar pixlar.
//   · pixlar närmare än `Style::overdraw` från en granne som MÄTBART ritar
//     utanför sin rect (knappens skugga) räknas inte för någon av dem — de
//     hade annars kunnat låna grannens skugga och passera.
//   · typer som SAKNAS i fixturen. De listas vid varje körning.
//   · bilder ur något annat än demons assets.bin (fixturens ikoner bor där:
//     galleriet ligger i tests/fixtures, inte i demon).
//   · FLIKRUBRIKERNA som text (kontroll 3): en fliksidas `text` ritas i
//     flikkontrollens flikrad, och att tömma den ändrar flikens BREDD — en
//     mätning i raden hade mätt geometri och inte text. Rubrikerna mäts som
//     logik i tab_gate; flikkontrollens egen ritning och dess inaktiva läge
//     mäts här (kontroll 1 och 2). Och en DOLD fliksidas innehåll syns
//     inte och mäts inte — varje komponent ska stå synlig någon annanstans
//     i fixturen.
//   · skalan: grinden kör maskinens egen. Ingen tröskel beror på den.
//
// INGEN TYST SKIP (samma beslut som clip_gate): utan
// fungerande headless-yta FALLER grinden. En pixelgrind som hoppar över
// bevisar ingenting, och en grind som tyst slutar köras är
// ingen grind.
//
//   pixel_gate [-v] [--png <katalog>]
//     -v     skriv varje mätning
//     --png  skriv normalbilden per palett, att ÖGNA (inte en del av grinden)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "cpp_reader.h"
#include "standin_widget.h"
#include "window_model.h"
#include "window_tree.h"

namespace {

// ------------------------------------------------------------- trösklarna
//
// Tal i en TESTFIL är tillåtna (regel 3 gäller widget-filer).
//
// kTol: en pixel "skiljer sig" först när någon kanal skiljer MER än så.
// Rendreringen är deterministisk — samma ritanrop ger samma bytes — så
// toleransen finns inte för brus utan för att "syns" ska betyda något: en
// yta ett par steg från bakgrunden är osynlig för ögat och räknas så.
constexpr int kTol = 6;
// kMinPx: så många skiljande pixlar krävs. Hellre en handfull än en enda:
// en ensam pixel kan vara kantutjämning som läcker förbi erosionen nedan.
constexpr int kMinPx = 4;
// kVisarAndel: konsekvenskravet för av/på (kontroll 2) gäller en widget som
// VISAR ett inaktivt läge, och det betyder att en märkbar del av den byter
// utseende — minst 1/kVisarAndel av ytan. Utan andelen räknades en tom
// Progress vars nollbreda fyllning lämnar en kantutjämnad flisa på tio
// pixlar som "visar sitt läge", och föll i de teman där flisan försvann.
constexpr int kVisarAndel = 50;

// KÄNDA FEL — se filhuvudet. `typ` är modellens C++-typ och `palett` ett
// PREFIX av palettnamnet ("<UI>-<tema>-<läge>"), så raden gäller felet och
// inte en viss nod i fixturen.
struct KäntFel {
    const char* kontroll;   // "egen" · "av" · "text"
    const char* typ;
    const char* palett;
    const char* skäl;
};
constexpr KäntFel kKändaFel[] = {
    // Minimalistics länk bär sitt tillstånd i FÄRGEN ensam (ingen
    // understrykning i vila), och i Neutral mörkt ligger `link` #adadad och
    // `textDim` #a8a8a8 fem steg isär: en inaktiverad länk ser aktiv ut.
    // Palettens sak, alltså utseendet.
    {"av", "glaze::Link", "Minimalistic-Neutral-mörkt",
     "link #adadad mot textDim #a8a8a8"},
    // Samma palettegenskap: Minimalistic-Neutral har `accent` fem steg från
    // `textDim` i BÅDA lägena (#666666/#616161 ljust, #adadad/#a8a8a8
    // mörkt), och den inaktiva Progress-fyllningen ÄR `textDim`.
    {"av", "glaze::Progress", "Minimalistic-Neutral-",
     "accent mot textDim, fem steg isär"},
};

int  g_fel     = 0;
bool g_verbose = false;

// --------------------------------------------------------------- bilden

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
};

// Skillnadsmasken mellan två lika stora bilder: 1 där någon kanal skiljer
// mer än kTol. Räknas EN gång per bildpar — varje kontroll läser den.
//
// PRESTANDA: grinden går över ~1 Mpx × sju bilder ×
// fyrtio paletter i ett Debug-bygge (-O0), där varje `vector::operator[]`
// och varje lambda är ett riktigt funktionsanrop. Med dem i pixelloopar
// tog grinden 70 s; därför RÅA pekare i alla pixelloopar nedan.
std::vector<std::uint8_t> skillnad(const Bild& a, const Bild& b)
{
    const std::size_t         n = static_cast<std::size_t>(a.w) * a.h;
    std::vector<std::uint8_t> m(n, 0);
    std::uint8_t*             ut = m.data();
    const std::uint8_t*       p  = a.px.data();
    const std::uint8_t*       q  = b.px.data();
    for (std::size_t i = 0; i < n; ++i, p += 4, q += 4) {
        const int d0 = p[0] > q[0] ? p[0] - q[0] : q[0] - p[0];
        const int d1 = p[1] > q[1] ? p[1] - q[1] : q[1] - p[1];
        const int d2 = p[2] > q[2] ? p[2] - q[2] : q[2] - p[2];
        ut[i]        = d0 > kTol || d1 > kTol || d2 > kTol;
    }
    return m;
}

// Pixelrutan en rect TÄCKER — pixelcentrum innanför, samma regel åt alla
// håll, så att två angränsande rects aldrig delar en pixel.
struct Ruta {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // [x0,x1) × [y0,y1)
};

Ruta ruta(const gfx::Rectf& r, int bildW, int bildH)
{
    Ruta q;
    q.x0 = std::max(0, static_cast<int>(std::ceil(r.x - 0.5f)));
    q.y0 = std::max(0, static_cast<int>(std::ceil(r.y - 0.5f)));
    q.x1 = std::min(bildW, static_cast<int>(std::ceil(r.x + r.w - 0.5f)));
    q.y1 = std::min(bildH, static_cast<int>(std::ceil(r.y + r.h - 0.5f)));
    return q;
}

gfx::Rectf snitt(const gfx::Rectf& a, const gfx::Rectf& b)
{
    const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w);
    const float y1 = std::min(a.y + a.h, b.y + b.h);
    return gfx::Rectf{x0, y0, std::max(0.0f, x1 - x0),
                      std::max(0.0f, y1 - y0)};
}

gfx::Rectf blåsUpp(const gfx::Rectf& r, float m)
{
    return gfx::Rectf{r.x - m, r.y - m, r.w + 2.0f * m, r.h + 2.0f * m};
}

bool sammaRect(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// --------------------------------------------------------------- fönstret

// Fixturfönstret: titel/storlek ur modellen och trädet ur
// `bld::buildWindowTree` — exakt glaze-renders `RenderWindow`.
class Fixtur : public glaze::AppWindow {
public:
    explicit Fixtur(const bld::WindowModel& m)
        : glaze::AppWindow(m.title(), m.clientWidth(), m.clientHeight())
    {
        perNod = bld::buildWindowTree(*this, m, "pixel_gate");
    }

    std::vector<glaze::Widget*> perNod;   // index = modellens nodindex
};

bool rendera(Fixtur& win, Bild& ut)
{
    win.invalidate();   // FULL omritning — inget ärvs ur förra bilden
    win.window().renderFrame();
    return win.window().captureFrame(ut.px, ut.w, ut.h);
}

// --------------------------------------------------------------- trädet

// En widget i fönstret, i RITORDNING: djupet först och barnen i z-ordning,
// exakt `AppWindow::drawTree`s ordning.
struct Nod {
    glaze::Widget* w        = nullptr;
    int            förälder = -1;   // index i listan, -1 = fönsterroten
    int            nivå     = 0;    // 0 = rotens barn
    bool           inbyggd  = false;
    std::string    namn;            // för felraderna
    std::string    typ;             // modellens C++-typ; tom för inredning
};

// RENA LAYOUTWIDGETAR — de enda som MED FLIT inte ritar något själva.
// Listan är grindens enda typkunskap och ska förbli kort: en komponent som
// hamnar här har bestämt att den är osynlig, och det beslutet ska stå
// skrivet. En komposit vars utseende ÄR dess barn och som själv inte ritar
// något hör också hemma här — med skälet utskrivet.
bool renLayout(const glaze::Widget& w)
{
    // Spacer: flex-luckan i en Bar. Ritar ingenting i någon UI — det är
    // hela widgeten.
    // TabPage: fliksidans YTA är flikkontrollens — den fyller och
    // ramar in visningsytan, och sidan ritar bara sin ram när `setBorder`
    // är satt (tab_page.cpp). Sidans pixlar är dess barns.
    // FilePicker: kompositen ÄR sina två barn — ett riktigt
    // textfält och en riktig knapp, som ritar sig själva och mäts som
    // inredning; fältet har med flit ingen egen yta (file_picker.cpp).
    return dynamic_cast<const glaze::Spacer*>(&w) != nullptr ||
           dynamic_cast<const glaze::TabPage*>(&w) != nullptr ||
           dynamic_cast<const glaze::FilePicker*>(&w) != nullptr;
}

// TEXTEN RITAS AV FÖRÄLDERN (kontroll 3) — en fliksidas `text` är flikens
// RUBRIK och står i flikkontrollens flikrad, utanför sidans egen yta, där
// kontrollen räknar. Skälet att den inte mäts i raden står i filhuvudet.
bool textenHosFörälder(const glaze::Widget& w)
{
    return dynamic_cast<const glaze::TabPage*>(&w) != nullptr;
}

// MÅSTE VISA SITT INAKTIVA LÄGE (kontroll 2) — det som går att MANÖVRERA.
// `focusable()` är ramverkets mått på det, med ETT skrivet undantag: en
// VERKTYGSKNAPP tar aldrig fokus (ett klick på den ska inte flytta caret ur
// ett fält) men går att klicka, och en ren ikonknapp har ingen text som
// dimmas — dess enda inaktiva tecken är ikonens OPACITET
// (`Renderer::drawImage`). Utan raden hade en backend som
// tappar opaciteten i shadern passerat: varje palett hade visat "inget
// inaktivt utseende", och konsekvenskravet hade varit uppfyllt.
bool måsteVisaAv(const glaze::Widget& w)
{
    return w.focusable() ||
           dynamic_cast<const glaze::ToolButton*>(&w) != nullptr;
}

std::vector<Nod> samla(Fixtur& win, const bld::WindowModel& m)
{
    std::map<const glaze::Widget*, std::string> typAv;
    for (std::size_t i = 0; i < win.perNod.size(); ++i)
        if (win.perNod[i])
            typAv[win.perNod[i]] = m.nodes[i].type;

    // Programikonen är chrome som TopBar: att dölja den
    // flyttar namnlistens motvikt och — i den stora formen — menyradens
    // början, alltså geometrin nivåmätningen förutsätter står still.
    const glaze::Widget* inbyggda[] = {win.topBar(), &win.menuBar,
                                       &win.statusBar, &win.appIcon};
    struct Steg {
        glaze::Widget* w;
        int            förälder;
        int            nivå;
        int            plats;
    };
    std::vector<Nod>  ut;
    std::vector<Steg> stack;
    const auto&       rot = win.root().children();
    for (int i = static_cast<int>(rot.size()) - 1; i >= 0; --i)
        stack.push_back({rot[static_cast<std::size_t>(i)], -1, 0, i});
    while (!stack.empty()) {
        const Steg s = stack.back();
        stack.pop_back();
        Nod n;
        n.w        = s.w;
        n.förälder = s.förälder;
        n.nivå     = s.nivå;
        n.inbyggd  = std::find(std::begin(inbyggda), std::end(inbyggda),
                               s.w) != std::end(inbyggda);
        const auto t = typAv.find(s.w);
        n.typ        = t != typAv.end() ? t->second : std::string{};
        const std::string fNamn =
            s.förälder >= 0 ? ut[static_cast<std::size_t>(s.förälder)].namn
                            : std::string{"root"};
        n.namn = !s.w->name.empty()
                     ? s.w->name
                     : fNamn + "[" + std::to_string(s.plats) + "]";
        ut.push_back(n);
        const int   mig = static_cast<int>(ut.size()) - 1;
        const auto& b   = s.w->children();
        for (int i = static_cast<int>(b.size()) - 1; i >= 0; --i)
            stack.push_back({b[static_cast<std::size_t>(i)], mig, s.nivå + 1,
                             i});
    }
    return ut;
}

// FÖRFÄDERNA som en nxn-tabell: [i*n + a] = 1 när a är en förfader till i.
// Räknad en gång per palett — erosionen och textkontrollen frågar för
// varje pixel, och en vandring uppåt per pixel hade kostat mer än bilden.
std::vector<std::uint8_t> förfäder(const std::vector<Nod>& t)
{
    const std::size_t         n = t.size();
    std::vector<std::uint8_t> f(n * n, 0);
    for (std::size_t i = 0; i < n; ++i)
        for (int p = t[i].förälder; p >= 0;
             p = t[static_cast<std::size_t>(p)].förälder)
            f[i * n + static_cast<std::size_t>(p)] = 1;
    return f;
}

// Geometrin i ETT tillstånd: per nod den effektiva synligheten, rect:en
// (`resolve` mot widgetens `behaviorCtx` — samma ctxFor-vandring som
// ritningen) och den SYNLIGA delen (rect ∩ förfädernas klipp ∩ bilden).
struct Läge {
    std::vector<bool>       synlig;
    std::vector<gfx::Rectf> rect;
    std::vector<gfx::Rectf> klippt;

    bool helt(std::size_t i) const   // syns HELT — se filhuvudet
    {
        return synlig[i] && rect[i].w > 0.0f && rect[i].h > 0.0f &&
               sammaRect(klippt[i], rect[i]);
    }
};

Läge mät(const std::vector<Nod>& t, int bildW, int bildH)
{
    Läge l;
    l.synlig.assign(t.size(), false);
    l.rect.assign(t.size(), gfx::Rectf{});
    l.klippt.assign(t.size(), gfx::Rectf{});
    std::vector<gfx::Rectf> barnKlipp(t.size());
    const gfx::Rectf        bild{0.0f, 0.0f, static_cast<float>(bildW),
                                 static_cast<float>(bildH)};
    for (std::size_t i = 0; i < t.size(); ++i) {
        const int  f    = t[i].förälder;
        const bool fSyn = f < 0 || l.synlig[static_cast<std::size_t>(f)];
        l.synlig[i]     = fSyn && t[i].w->isVisible();
        if (!l.synlig[i])
            continue;
        const glaze::Ctx c    = t[i].w->behaviorCtx();
        l.rect[i]             = t[i].w->resolve(c);
        const gfx::Rectf ärvt =
            f < 0 ? bild : barnKlipp[static_cast<std::size_t>(f)];
        l.klippt[i]  = snitt(l.rect[i], ärvt);
        barnKlipp[i] = t[i].w->clipsChildren()
                           ? snitt(ärvt, t[i].w->clipRect(c))
                           : ärvt;
    }
    return l;
}

// Vem äger varje pixel: den SIST ritade synliga noden (nivå ≤ maxNivå) vars
// klippta rect täcker den. -1 = fönsterbakgrunden.
std::vector<int> ägare(const std::vector<Nod>& t, const Läge& l, int maxNivå,
                       int w, int h)
{
    std::vector<int> o(static_cast<std::size_t>(w) * h, -1);
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (!l.synlig[i] || t[i].nivå > maxNivå)
            continue;
        const Ruta r = ruta(l.klippt[i], w, h);
        for (int y = r.y0; y < r.y1; ++y) {
            int* rad = o.data() + static_cast<std::size_t>(y) * w;
            std::fill(rad + r.x0, rad + r.x1, static_cast<int>(i));
        }
    }
    return o;
}

// SKUGGBANDET för en spillande nod: rect:en uppblåst med det UPPMÄTTA
// spillet plus en pixel kantutjämning. Mätt och inte `Style::overdraw`
// rakt av: overdraw är TAKET för vad någon widget får rita utanför sig, och
// med taket som band åt en verktygsknapps enpixelskant upp skiljelinjen
// bredvid den — grinden föll på en linje som syntes.
gfx::Rectf skuggband(const gfx::Rectf& r, float spill)
{
    return blåsUpp(r, std::ceil(spill) + 1.0f);
}

// SKUGGZONERNA: per pixel den spillande nod vars skuggband täcker den —
// bandet MINUS rect:en själv, för spill är per definition det som hamnar
// UTANFÖR. Inne i sin rect äger noden sina pixlar ändå, och ett band över
// hela ytan hade gjort varje pixel i en spillande panel till en zonpixel.
// -1 = ingen, -2 = flera (då avgörs pixeln av den långsamma vägen).
// `spill[j]` är nodens uppmätta spillavstånd i px, 0 = spiller inte.
std::vector<int> skuggzon(const std::vector<Nod>& t, const Läge& l,
                          const std::vector<float>& spill, int w, int h)
{
    std::vector<int> z(static_cast<std::size_t>(w) * h, -1);
    for (std::size_t j = 0; j < t.size(); ++j) {
        if (spill[j] <= 0.0f || !l.synlig[j])
            continue;
        const Ruta yttre = ruta(skuggband(l.rect[j], spill[j]), w, h);
        const Ruta inre  = ruta(l.rect[j], w, h);
        for (int y = yttre.y0; y < yttre.y1; ++y)
            for (int x = yttre.x0; x < yttre.x1; ++x) {
                if (x >= inre.x0 && x < inre.x1 && y >= inre.y0 &&
                    y < inre.y1)
                    continue;
                int& v = z[static_cast<std::size_t>(y) * w + x];
                v      = v == -1 ? static_cast<int>(j) : -2;
            }
    }
    return z;
}

// ------------------------------------------------------- bokföringen

// Fel aggregeras per (kontroll, nod): en trasig widget ger EN rad med
// antalet paletter, inte fyrtio.
struct Fynd {
    std::vector<std::string> paletter;
    std::string              detalj;   // ur första förekomsten
};
std::map<std::string, Fynd> g_fynd;    // fäller grinden
std::map<std::string, Fynd> g_kända;   // kKändaFel som slog till
std::vector<int>            g_kändTräff(std::size(kKändaFel), 0);

void fynd(const std::string& nyckel, const std::string& palett,
          const std::string& detalj)
{
    Fynd& f = g_fynd[nyckel];
    if (f.paletter.empty())
        f.detalj = detalj;
    f.paletter.push_back(palett);
    ++g_fel;
}

// Ett mätfel — KÄNT om en rad i kKändaFel täcker det, annars ett FEL.
void mätfel(const char* kontroll, const std::string& typ,
            const std::string& nod, const std::string& palett,
            const std::string& detalj)
{
    for (std::size_t k = 0; k < std::size(kKändaFel); ++k) {
        const KäntFel& kf = kKändaFel[k];
        if (std::strcmp(kf.kontroll, kontroll) == 0 && typ == kf.typ &&
            palett.rfind(kf.palett, 0) == 0) {
            ++g_kändTräff[k];
            Fynd& f = g_kända[std::string(kontroll) + ":" + nod];
            if (f.paletter.empty())
                f.detalj = detalj + " — KÄNT (" + kf.skäl + ")";
            f.paletter.push_back(palett);
            return;
        }
    }
    fynd(std::string(kontroll) + ":" + nod, palett, detalj);
}

// Minsta marginal per kontroll — hur nära tröskeln det GODKÄNDA ligger.
struct Marginal {
    int         px = -1;
    std::string vem;
};
std::map<std::string, Marginal> g_marginal;
std::map<std::string, int>      g_antal;   // godkända mätningar

void godkänd(const std::string& kontroll, int px, const std::string& vem)
{
    ++g_antal[kontroll];
    Marginal& m = g_marginal[kontroll];
    if (m.px < 0 || px < m.px) {
        m.px  = px;
        m.vem = vem;
    }
}

std::set<std::string> g_typerIFixturen;
std::set<std::string> g_typerMätta;
std::set<std::string> g_typerLayout;   // renLayout() — mäts med flit inte

// AV/PÅ för det som INTE tar fokus, per (UI, nod) och palett — avgörs i
// main när alla teman är mätta (konsekvenskravet, se kontroll 2).
struct AvPå {
    std::string palett, typ, beskrivning;
    int         olika = 0, yta = 0;
};
std::map<std::string, std::vector<AvPå>> g_avPå;

// Widgets vars ritning beror på TIDEN (stabilitetskontrollen) — mäts inte,
// räknas per palett och skrivs ut.
std::map<std::string, int> g_animerade;

const char* uiNamn(glaze::UI u)
{
    switch (u) {
    case glaze::UI::Glaze:        return "Glaze";
    case glaze::UI::Slate:        return "Slate";
    case glaze::UI::Minimalistic: return "Minimalistic";
    }
    return "?";
}

// Utfallet för en nod i en kontroll: yta = de pixlar som räknas, olika =
// de av dem som skiljer sig.
struct Räkning {
    std::vector<int> yta, olika;
    explicit Räkning(std::size_t n) : yta(n, 0), olika(n, 0) {}
};

// -------------------------------------------------------- en palett

// Mäter ALLT i den palett som är satt just nu. `N` är fixturen, `T` samma
// modell med tomma texter; `medText` bär modellindex för noderna med text.
void mätPalett(Fixtur& N, Fixtur& T, const bld::WindowModel& m,
               const std::vector<std::size_t>& medText, glaze::UI ui,
               const std::string& palett, const std::string& pngKatalog)
{
    const std::vector<Nod> t = samla(N, m);
    const std::size_t      n = t.size();

    Bild normal;
    if (!rendera(N, normal)) {
        fynd("render", palett, "captureFrame misslyckades");
        return;
    }
    const int         W = normal.w, H = normal.h;
    const std::size_t A = static_cast<std::size_t>(W) * H;
    if (!pngKatalog.empty())
        gfx::encodePng(pngKatalog + "/" + palett + ".png", normal.px, W, H);

    const Läge  g     = mät(t, W, H);
    const float scale = N.window().uiScale();
    const float ov    = N.style().overdraw * scale;
    const std::vector<std::uint8_t> förf = förfäder(t);
    // SLÄKT = noden själv, en förfader eller en ättling.
    auto släkt = [&](std::size_t i, std::size_t j) {
        return i == j || förf[i * n + j] || förf[j * n + i];
    };

    int maxNivå = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (g.synlig[i])
            maxNivå = std::max(maxNivå, t[i].nivå);
    const std::size_t nivåer = static_cast<std::size_t>(maxNivå) + 1;

    // ---- nivåbilderna, TVÅ per nivå k och tagna i ordning från djupast:
    //   V[k]  nivå ≤ k synlig, nivå k med VÄNT aktiverat/inaktiverat
    //   I[k]  bara nivå < k synlig (I[maxNivå+1] är normalbilden)
    // Nivå k döljs ovanpå de djupare, så varje steg döljer bara det som
    // fortfarande syns — och ALLT som dolts visas igen efteråt. Vändningen
    // görs nivåvis av samma skäl som dolningen: barnen är dolda och
    // förfäderna orörda, så ingenting annat än nivån själv kan ändra pixlar
    // i dess yta (en knapp som tappar sin skugga när den inaktiveras hade
    // annars ändrat panelen runt den).
    //
    // Fokuserbarheten frågas där widgeten är AKTIVERAD: före vändningen för
    // de aktiva, efter för de inaktiva — Radio svarar ur gruppens tillstånd,
    // och den frågan har bara svar för en aktiv radio.
    std::vector<Bild> I(nivåer + 1), V(nivåer);
    I.back() = normal;
    std::vector<std::uint8_t>   fokus(n, 0);
    std::vector<glaze::Widget*> dolda;
    bool                        nivåOk = true;
    // FÖRUTSÄTTNINGEN: varken dolning eller vändning får flytta det som
    // syns. Gör de det jämför bilderna olika geometrier, och då mäter
    // grinden ingenting — det ska synas som ett fel, inte som grönt.
    auto geometrinStår = [&](int k, const char* vad) {
        const Läge l = mät(t, W, H);
        for (std::size_t i = 0; i < n; ++i)
            if (l.synlig[i] && t[i].nivå <= k &&
                !sammaRect(l.rect[i], g.rect[i]))
                fynd("geometri:" + t[i].namn, palett,
                     t[i].namn + ": rect:en ändras när nivå " +
                         std::to_string(k) + " " + vad +
                         " — nivåmätningen förutsätter att den inte gör det");
    };
    for (int k = maxNivå; k >= 0 && nivåOk; --k) {
        std::vector<glaze::Widget*> vända;
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i].nivå != k || !g.synlig[i] || t[i].inbyggd)
                continue;
            if (t[i].w->isEnabled()) {
                fokus[i] = måsteVisaAv(*t[i].w);
                t[i].w->disable();
            } else {
                t[i].w->enable();
            }
            vända.push_back(t[i].w);
        }
        for (glaze::Widget* w : vända)
            for (std::size_t i = 0; i < n; ++i)
                if (t[i].w == w && w->isEnabled())
                    fokus[i] = måsteVisaAv(*w);
        Bild&      v    = V[static_cast<std::size_t>(k)];
        const bool vOk  = rendera(N, v) && v.w == W && v.h == H;
        geometrinStår(k, "vänds");
        for (glaze::Widget* w : vända) {
            if (w->isEnabled())
                w->disable();
            else
                w->enable();
        }
        if (!vOk) {
            fynd("render", palett, "vänd nivåbild " + std::to_string(k));
            nivåOk = false;
            break;
        }

        for (std::size_t i = 0; i < n; ++i)
            if (t[i].nivå == k && g.synlig[i] && !t[i].inbyggd) {
                t[i].w->hide();
                dolda.push_back(t[i].w);
            }
        Bild& b = I[static_cast<std::size_t>(k)];
        if (!rendera(N, b) || b.w != W || b.h != H) {
            fynd("render", palett, "nivåbild " + std::to_string(k));
            nivåOk = false;
            break;
        }
        geometrinStår(k - 1, "döljs");
    }
    for (auto it = dolda.rbegin(); it != dolda.rend(); ++it)
        (*it)->show();
    if (!nivåOk)
        return;

    // Per nivå L: ägarkartan när nivå ≤ L syns, skillnadsmasken mellan
    // I[L+1] (nivå L synlig) och I[L] (dold) — nivå L:s egen ritning — och
    // mellan V[L] och I[L+1] — nivå L:s tillståndsbyte.
    std::vector<std::vector<int>>          äg(nivåer);
    std::vector<std::vector<std::uint8_t>> D(nivåer), DV(nivåer);
    for (std::size_t L = 0; L < nivåer; ++L) {
        äg[L] = ägare(t, g, static_cast<int>(L), W, H);
        D[L]  = skillnad(I[L + 1], I[L]);
        DV[L] = skillnad(V[L], I[L + 1]);
    }
    const std::vector<int>& ägAlla = äg.back();

    // ---- STABILITETEN: samma tillstånd en gång till. Allt som skiljer är
    // TIDSBEROENDE ritning (en obestämd Progress räknar sin position ur
    // klockan), och för en sådan widget mäter bildparen tiden och inte
    // ritningen. Den mäts därför inte alls — hellre en utskriven lucka än
    // en grind som slumpvis passerar eller faller.
    std::vector<std::uint8_t> animerad(n, 0);
    {
        Bild igen;
        if (!rendera(N, igen) || igen.w != W || igen.h != H) {
            fynd("render", palett, "stabilitetsbilden");
            return;
        }
        const std::vector<std::uint8_t> Ds = skillnad(normal, igen);
        for (std::size_t p = 0; p < A; ++p)
            if (Ds[p] && ägAlla[p] >= 0)
                animerad[static_cast<std::size_t>(ägAlla[p])] = 1;
        for (std::size_t i = 0; i < n; ++i)
            if (animerad[i]) {
                ++g_animerade[t[i].namn];
                if (g_verbose)
                    std::printf("    animerad %s — mäts inte\n",
                                t[i].namn.c_str());
            }
    }

    // ---- SPILL: vem ritar MÄTBART utanför sin rect, och hur långt? En
    // skiljande pixel mellan I[L+1] och I[L] som ägs av en LÄGRE nivå kan
    // bara komma från en nivå-L-widget som ritat utanför sig själv (skugga,
    // kantutjämning). Den tillskrivs den närmaste nivå-L-noden inom
    // `Style::overdraw` — taket för vad någon widget får rita utanför sig —
    // och nodens spill blir det STÖRSTA avståndet den tillskrivits.
    //
    // Spill som landar i en GRANNES rect går inte att se här (pixeln ägs av
    // grannen). Bandet är därför symmetriskt: en skugga som mäts två pixlar
    // nedåt antas nå två pixlar åt alla håll.
    std::vector<int> nivåAv(n);
    for (std::size_t i = 0; i < n; ++i)
        nivåAv[i] = t[i].nivå;
    const int* nivå = nivåAv.data();

    std::vector<float> spill(n, 0.0f);
    for (std::size_t L = 0; L < nivåer; ++L) {
        const int*          o = äg[L].data();
        const std::uint8_t* d = D[L].data();
        for (std::size_t p = 0; p < A; ++p) {
            if (!d[p] || (o[p] >= 0 && nivå[o[p]] == static_cast<int>(L)))
                continue;
            const float px = static_cast<float>(p % W) + 0.5f;
            const float py = static_cast<float>(p / W) + 0.5f;
            int         bäst  = -1;
            float       bästD = 0.0f;
            for (std::size_t j = 0; j < n; ++j) {
                if (t[j].nivå != static_cast<int>(L) || !g.synlig[j])
                    continue;
                const gfx::Rectf& r = g.rect[j];
                if (!blåsUpp(r, ov).contains(px, py))
                    continue;
                const float dx = std::max({r.x - px, 0.0f, px - (r.x + r.w)});
                const float dy = std::max({r.y - py, 0.0f, py - (r.y + r.h)});
                const float dd = std::max(dx, dy);
                if (bäst < 0 || dd < bästD) {
                    bäst  = static_cast<int>(j);
                    bästD = dd;
                }
            }
            if (bäst >= 0) {
                float& s = spill[static_cast<std::size_t>(bäst)];
                s        = std::max(s, std::max(bästD, 0.5f));
            }
        }
    }

    // Ligger pixeln i en SPILLANDE grannes skuggband? Grannen = inte SLÄKT
    // med noden (varken den själv, en förfader eller en ättling) och högst
    // nivå `maxN`. Zonkartan avgör nästan alltid direkt; bara där två zoner
    // överlappar (-2) prövas grannarna en och en.
    auto grannzon = [&](const std::vector<int>& zon, const Läge& l,
                        std::size_t i, std::size_t p, int maxN) {
        auto granne = [&](std::size_t j) {
            return spill[j] > 0.0f && !släkt(i, j) && t[j].nivå <= maxN;
        };
        const int z = zon[p];
        if (z == -1)
            return false;
        if (z >= 0)
            return granne(static_cast<std::size_t>(z));
        const float px = static_cast<float>(p % W) + 0.5f;
        const float py = static_cast<float>(p / W) + 0.5f;
        for (std::size_t j = 0; j < n; ++j)
            if (granne(j) && l.synlig[j] &&
                skuggband(l.rect[j], spill[j]).contains(px, py) &&
                !l.rect[j].contains(px, py))
                return true;
        return false;
    };
    const std::vector<int> zonN = skuggzon(t, g, spill, W, H);

    auto beskriv = [&](std::size_t i) {
        return t[i].namn + (t[i].typ.empty() ? "" : " (" + t[i].typ + ")");
    };

    // ---- 1. EGEN RITNING — ett svep per nivå över hela bilden: varje pixel
    // räknas för den nivå-L-nod som äger den.
    Räkning                   egen(n);
    std::vector<std::uint8_t> mäts(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!g.helt(i) || t[i].inbyggd || animerad[i])
            continue;
        if (renLayout(*t[i].w)) {
            if (!t[i].typ.empty())
                g_typerLayout.insert(t[i].typ);
            continue;
        }
        mäts[i] = 1;
    }
    const int* zN = zonN.data();
    for (std::size_t L = 0; L < nivåer; ++L) {
        const int*          o  = äg[L].data();
        const std::uint8_t* d  = D[L].data();
        const std::uint8_t* mi = mäts.data();
        int*                yt = egen.yta.data();
        int*                ol = egen.olika.data();
        for (std::size_t p = 0; p < A; ++p) {
            const int ä = o[p];
            if (ä < 0 || nivå[ä] != static_cast<int>(L) || !mi[ä] ||
                (zN[p] != -1 &&
                 grannzon(zonN, g, static_cast<std::size_t>(ä), p,
                          static_cast<int>(L))))
                continue;
            ++yt[ä];
            ol[ä] += d[p];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!mäts[i] || egen.yta[i] == 0)
            continue;   // täckt av ett senare syskon — täckningskollen i
                        // main ser till att TYPEN ändå mäts någonstans
        if (!t[i].typ.empty())
            g_typerMätta.insert(t[i].typ);
        if (g_verbose)
            std::printf("    egen   %-28s %6d/%-6d px\n", t[i].namn.c_str(),
                        egen.olika[i], egen.yta[i]);
        if (egen.olika[i] < kMinPx)
            mätfel("egen", t[i].typ, t[i].namn, palett,
                   beskriv(i) + " ritar ingenting synligt över ytan under: " +
                       std::to_string(egen.olika[i]) + " av " +
                       std::to_string(egen.yta[i]) + " px skiljer");
        else
            godkänd("egen ritning", egen.olika[i], t[i].namn + " i " + palett);
    }

    // ---- 2. AKTIVERAT/INAKTIVERAT — V[L] mot I[L+1], nivåvis (se
    // nivåbilderna). TVÅ KRAV:
    //   · det som tar FOKUS ska ALLTID skilja sig;
    //   · allt annat ska vara KONSEKVENT inom sin UI: skiljer det sig i ett
    //     av UI:ts teman ska det skilja sig i alla. Det fångar musens
    //     kontroller (fönster- och verktygsknappar tar inget fokus) utan
    //     att kräva ett inaktivt utseende av en skiljelinje, som med flit
    //     saknar ett. Det avgörs i main när alla teman är mätta.
    Räkning av(n);
    for (std::size_t L = 0; L < nivåer; ++L) {
        const int*          o  = äg[L].data();
        const std::uint8_t* d  = DV[L].data();
        const std::uint8_t* mi = mäts.data();
        int*                yt = av.yta.data();
        int*                ol = av.olika.data();
        for (std::size_t p = 0; p < A; ++p) {
            const int ä = o[p];
            if (ä < 0 || nivå[ä] != static_cast<int>(L) || !mi[ä] ||
                (zN[p] != -1 &&
                 grannzon(zonN, g, static_cast<std::size_t>(ä), p,
                          static_cast<int>(L))))
                continue;
            ++yt[ä];
            ol[ä] += d[p];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!mäts[i] || av.yta[i] == 0)
            continue;
        if (g_verbose)
            std::printf("    av/på  %-28s %6d/%-6d px%s\n", t[i].namn.c_str(),
                        av.olika[i], av.yta[i], fokus[i] ? " (måste)" : "");
        if (!fokus[i]) {
            g_avPå[std::string(uiNamn(ui)) + "\x1f" + t[i].namn].push_back(
                {palett, t[i].typ, beskriv(i), av.olika[i], av.yta[i]});
            continue;
        }
        if (av.olika[i] < kMinPx)
            mätfel("av", t[i].typ, t[i].namn, palett,
                   beskriv(i) +
                       " ser LIKADAN ut aktiverad som inaktiverad: " +
                       std::to_string(av.olika[i]) + " av " +
                       std::to_string(av.yta[i]) + " px skiljer");
        else
            godkänd("av/på skiljer sig", av.olika[i],
                    t[i].namn + " i " + palett);
    }

    // ---- 3. TEXTEN SYNS — mot T, samma modell med tomma texter. Texten kan
    // ändra en autostorlek, så pixeln räknas bara om den i T ägs av noden
    // SJÄLV, en förfader eller bakgrunden: en granne som flyttat in får
    // aldrig låna ut sina pixlar.
    Bild tom;
    if (!rendera(T, tom) || tom.w != W || tom.h != H) {
        fynd("render", palett, "den textlösa bilden");
        return;
    }
    // T byggs ur SAMMA modell, så trädet ska vara nod för nod detsamma —
    // det är det som gör att index i betyder samma widget i båda.
    const std::vector<Nod> tt = samla(T, m);
    bool                   sammaTräd = tt.size() == n;
    for (std::size_t i = 0; sammaTräd && i < n; ++i)
        sammaTräd = tt[i].namn == t[i].namn && tt[i].förälder == t[i].förälder;
    if (!sammaTräd) {
        fynd("struktur", palett,
             "den textlösa instansen har ett annat träd än fixturen");
        return;
    }
    const Läge                      gt    = mät(tt, W, H);
    const std::vector<int>          ägTom = ägare(tt, gt, maxNivå, W, H);
    const std::vector<int>          zonT  = skuggzon(tt, gt, spill, W, H);
    const std::vector<std::uint8_t> Dt    = skillnad(normal, tom);
    std::vector<std::uint8_t>       harText(n, 0);
    for (std::size_t k : medText)
        for (std::size_t i = 0; i < n; ++i)
            if (t[i].w == N.perNod[k] && g.helt(i) && !animerad[i] &&
                !textenHosFörälder(*t[i].w))
                harText[i] = 1;
    Räkning text(n);
    {
        const int*          o  = ägAlla.data();
        const int*          oT = ägTom.data();
        const int*          zT = zonT.data();
        const std::uint8_t* d  = Dt.data();
        const std::uint8_t* hi = harText.data();
        const std::uint8_t* fö = förf.data();
        int*                yt = text.yta.data();
        int*                ol = text.olika.data();
        for (std::size_t p = 0; p < A; ++p) {
            const int ä = o[p];
            if (ä < 0 || !hi[ä])
                continue;
            const std::size_t i  = static_cast<std::size_t>(ä);
            const int         äT = oT[p];
            if (äT >= 0 && äT != ä &&
                !fö[i * n + static_cast<std::size_t>(äT)])
                continue;   // någon ANNAN än noden eller en förfader äger
                            // pixeln i T
            if ((zN[p] != -1 && grannzon(zonN, g, i, p, maxNivå)) ||
                (zT[p] != -1 && grannzon(zonT, gt, i, p, maxNivå)))
                continue;
            ++yt[ä];
            ol[ä] += d[p];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!harText[i] || text.yta[i] == 0)
            continue;
        if (g_verbose)
            std::printf("    text   %-28s %6d/%-6d px\n", t[i].namn.c_str(),
                        text.olika[i], text.yta[i]);
        if (text.olika[i] < kMinPx)
            mätfel("text", t[i].typ, t[i].namn, palett,
                   beskriv(i) + ": texten syns inte — " +
                       std::to_string(text.olika[i]) + " av " +
                       std::to_string(text.yta[i]) +
                       " px skiljer när den töms");
        else
            godkänd("texten syns", text.olika[i], t[i].namn + " i " + palett);
    }
}

// Är satsen en TEXT-sats? Sättaren normaliseras av läsaren till fältet
// `text` för ramverkets typer; en egen widget behåller anropsformen
// `setText(…)`. Båda räknas — grinden ska inte känna typerna.
bool ärTextSats(const bld::Stmt& s)
{
    return (s.kind == bld::Stmt::Kind::Prop && s.name == "text") ||
           (s.kind == bld::Stmt::Kind::Call && s.name == "setText");
}

// "glaze::CaptionButton" → "captionbutton" — registrets namnform.
std::string registerNamn(const std::string& cppTyp)
{
    std::string kort = cppTyp.substr(cppTyp.rfind(':') + 1);
    std::transform(kort.begin(), kort.end(), kort.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return kort;
}

} // namespace

int main(int argc, char** argv)
{
    std::string pngKatalog;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
            pngKatalog = argv[++i];
    }
    std::printf("=== pixel_gate — varje widget ritar något synligt, i varje "
                "UI × tema × läge × stil ===\n");

    const std::string     rot = std::string(GLAZE_TEST_ROOT) + "/";
    const bld::ReadResult r =
        bld::readWindow(rot + "tests/fixtures/gallery.h");
    if (!r.ok) {
        std::printf("  FEL  fixturen kunde inte läsas: %s\n",
                    r.error.c_str());
        std::printf("=== pixel_gate: FALLERAR ===\n");
        return 1;
    }
    if (!glaze::loadAssets(rot + "apps/demo/assets.bin")) {
        std::printf("  FEL  apps/demo/assets.bin kunde inte laddas — "
                    "fixturens bilder hade ritat ingenting\n");
        std::printf("=== pixel_gate: FALLERAR ===\n");
        return 1;
    }

    // TEXTLÖS modell: samma satser, varje textsats tömd. Modellindex med en
    // icke-tom text sparas — de noderna ska visa sin text.
    bld::WindowModel         utanText = r.model;
    std::vector<std::size_t> medText;
    for (bld::Stmt& s : utanText.stmts) {
        if (!ärTextSats(s))
            continue;
        if (!s.value.empty() && s.value != "\"\"")
            for (std::size_t k = 0; k < r.model.nodes.size(); ++k)
                if (r.model.nodes[k].id == s.target &&
                    std::find(medText.begin(), medText.end(), k) ==
                        medText.end())
                    medText.push_back(k);
        s.value = "\"\"";
    }

    int  paletter          = 0;
    bool förutsättningKlar = false;
    for (const glaze::UI ui :
         {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic}) {
        for (int s = 0; s < glaze::styleCount(ui); ++s) {
            // NYA fönster per (UI, stil): måtten styr chromets höjd och
            // därmed fönstrets storlek, och utseendet sätts FÖRE show() —
            // glaze-renders ordning. Tema och läge byts sedan i drift; de
            // rör bara färgerna.
            Fixtur N(r.model);
            Fixtur T(utanText);
            N.setUI(ui);
            N.setStyle(glaze::styleAt(ui, s));
            N.setOffscreen(true);
            T.setOffscreen(true);
            N.show();
            T.show();
            if (!N.isOpen() || !T.isOpen()) {
                std::printf("  FEL  fixturfönstret kunde inte skapas — ingen "
                            "headless-yta? (ALDRIG skip, se filhuvudet)\n");
                std::printf("=== pixel_gate: FALLERAR ===\n");
                return 1;
            }

            // ---- 4. FÖRUTSÄTTNINGEN: fixturen ÄR utförd.
            if (!förutsättningKlar) {
                förutsättningKlar = true;
                for (std::size_t k = 0; k < r.model.nodes.size(); ++k) {
                    glaze::Widget* w = N.perNod[k];
                    if (!w)
                        continue;
                    if (!r.model.nodes[k].builtin)
                        g_typerIFixturen.insert(r.model.nodes[k].type);
                    if (dynamic_cast<StandInWidget*>(w))
                        fynd("standin:" + r.model.nodes[k].id, "alla",
                             r.model.nodes[k].id + " (" +
                                 r.model.nodes[k].type +
                                 ") byggdes som STAND-IN — previewn kan inte "
                                 "utföra typen (palettrad saknas), och "
                                 "grinden hade mätt ställföreträdaren");
                }
            }

            for (int ti = 0; ti < glaze::themeCount(ui); ++ti) {
                for (const glaze::ThemeMode läge :
                     {glaze::ThemeMode::Light, glaze::ThemeMode::Dark}) {
                    N.setTheme(glaze::themeAt(ui, ti));
                    N.setThemeMode(läge);
                    const std::string palett =
                        std::string(uiNamn(ui)) + "-" +
                        glaze::themeAt(ui, ti).name + "-" +
                        (läge == glaze::ThemeMode::Light ? "ljust"
                                                         : "mörkt") +
                        "-" + glaze::styleAt(ui, s).name;
                    if (g_verbose)
                        std::printf("  -- %s\n", palett.c_str());
                    mätPalett(N, T, r.model, medText, ui, palett, pngKatalog);
                    ++paletter;
                }
            }
        }
    }

    // TÄCKNINGEN: varje typ i fixturen ska ha mätts minst en gång. En typ
    // som aldrig fick en mätbar yta (bortklippt, dold, täckt) hade annars
    // passerat utan att ha ritats.
    for (const std::string& typ : g_typerIFixturen)
        if (!g_typerLayout.count(typ) && !g_typerMätta.count(typ))
            fynd("täckning:" + typ, "alla",
                 typ + " finns i fixturen men fick aldrig en mätbar yta");

    // AV/PÅ, KONSEKVENSKRAVET (kontroll 2): det som inte tar fokus och som
    // visar sitt inaktiva läge i NÅGOT av sin UI:s teman ska visa det i
    // ALLA. Ett tema där tillståndet försvinner är temats fel.
    for (const auto& [nyckel, mätningar] : g_avPå) {
        const AvPå* syns = nullptr;
        for (const AvPå& a : mätningar)
            if (a.olika >= std::max(kMinPx, a.yta / kVisarAndel)) {
                syns = &a;
                break;
            }
        if (!syns)
            continue;   // inget inaktivt utseende alls i den här UI:n
        const std::string nod = nyckel.substr(nyckel.find('\x1f') + 1);
        for (const AvPå& a : mätningar) {
            if (a.olika >= kMinPx) {
                godkänd("av/på konsekvent", a.olika, nod + " i " + a.palett);
                continue;
            }
            mätfel("av", a.typ, nod, a.palett,
                   a.beskrivning + " visar sitt inaktiva läge i " +
                       syns->palett + " men INTE här: " +
                       std::to_string(a.olika) + " av " +
                       std::to_string(a.yta) + " px skiljer");
        }
    }

    // KÄNDA FEL är självutlöpande: en rad som inte slog till en enda gång
    // ska bort — annars ruttnar listan kvar efter att felet rättats.
    for (std::size_t k = 0; k < std::size(kKändaFel); ++k)
        if (g_kändTräff[k] == 0)
            fynd(std::string("utlöpt:") + kKändaFel[k].skäl, "alla",
                 std::string("kKändaFel-raden '") + kKändaFel[k].skäl +
                     "' slår inte längre till — felet är rättat, ta bort "
                     "raden");

    // Registrerade typer som SAKNAS i fixturen — inte ett fel (de mäts bara
    // inte), men utskrivna vid varje körning så att luckan syns.
    std::set<std::string> iFixturen;
    for (const std::string& typ : g_typerIFixturen)
        iFixturen.insert(registerNamn(typ));
    std::vector<std::string> saknas;
    for (const std::string& reg : glaze::registeredWidgetTypes())
        if (!iFixturen.count(reg))
            saknas.push_back(reg);

    for (const auto& [nyckel, f] : g_kända) {
        std::printf("  KÄNT %s\n", f.detalj.c_str());
        std::printf("       i %zu av %d paletter (först %s)\n",
                    f.paletter.size(), paletter, f.paletter[0].c_str());
    }
    for (const auto& [nyckel, f] : g_fynd) {
        std::printf("  FEL  %s\n", f.detalj.c_str());
        std::printf("       i %zu av %d paletter (först %s)\n",
                    f.paletter.size(), paletter, f.paletter[0].c_str());
    }
    for (const auto& [kontroll, mg] : g_marginal)
        std::printf("  %-18s %5d mätningar, minsta marginal %5d px (%s)\n",
                    kontroll.c_str(), g_antal[kontroll], mg.px,
                    mg.vem.c_str());
    std::printf("  %d paletter · %zu typer i fixturen, %zu mätta, %zu ren "
                "layout",
                paletter, g_typerIFixturen.size(), g_typerMätta.size(),
                g_typerLayout.size());
    if (!saknas.empty()) {
        std::printf(" · EJ I FIXTUREN (mäts inte):");
        for (const std::string& s : saknas)
            std::printf(" %s", s.c_str());
    }
    std::printf("\n");
    if (!g_animerade.empty()) {
        std::printf("  ANIMERADE (ritningen beror på tiden — mäts inte):");
        for (const auto& [namn, antal] : g_animerade)
            std::printf(" %s (%d)", namn.c_str(), antal);
        std::printf("\n");
    }

    std::printf(g_fel ? "=== pixel_gate: %d FEL ===\n"
                      : "=== pixel_gate: allt grönt (%d fel) ===\n",
                g_fel);
    return g_fel ? 1 : 0;
}
