// dnd_gate — DRAG-AND-DROPS grind.
//
// Kör:  ./build/tests/dnd_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// DRAG-AND-DROP ÄR ROUTING, och det är därför grinden matar FÖNSTRETS
// händelseingång (`AppWindow::handleEvent`, protected —
// WinForms `WndProc`-prejudikatet) i stället för att anropa krokar direkt
// som listbox_gate/dropdown_gate gör. Tröskeln, målvalet, att klicket
// uteblir efter ett drag och att Escape når sessionen före allt annat bor i
// routingen; en grind som bara anropade `dragBegin` hade varit grön oavsett
// om `handleEvent` någonsin frågade den.
//
// TVÅDELAD:
//
//   HEADLESS     lasten (`DragData`), effektmasken och basens
//                `setProp`-nycklar.
//   MED FÖNSTER  ett DOLT fönster (`setOffscreen`, dropdown_gate-
//                prejudikatet) utan chrome, med instrumenterade paneler som
//                loggar varje krok och handler i ordning. Händelserna är
//                riktiga `gfx::Event` genom den riktiga vägen, och
//                omritningen mäts med `window().renderFrame()` — samma väg
//                pumpen tar — så "ingenting ritas om" är ett räknat antal
//                `draw()`-anrop och inte ett antagande.
//
// SABOTAGEN den fäller:
//   * tröskeln `>=` i stället för `>` (fall 2: exakt tröskeln startar inte)
//   * ett klick efter ett drag (fall 2 och 4)
//   * fel ordning mellan leave och enter (fall 2)
//   * Escape som inte når sessionen (fall 4)
//   * en full omritning efter `onDragOver` (fall 7)
//   * ett mål utanför klientytan (fall 5b — roten tar emot släpp)
//   * att källans `unadopt` mitt i ett drag lämnar en dinglande pekare
//     (fall 14 — kraschar i stället för att bli röd)
//   * att MÅLETS `unadopt` mitt i ett drag lämnar sessionens pekare kvar
//     (fall 15 — målet är lånat och lever, så felet syns som ett andra
//     leave och en apphandler på en widget som inte längre sitter i trädet)
//
// DRAGETS ORDNING I SESSIONEN. Varje `X:enter` i
// loggarna följs av `X:over` (`onDragOver` fyras direkt efter
// `onDragEnter`), och fall 16–18:
//   16  inträdet möter over: enter och over i SAMMA rörelse, och ett mål som
//       säger Copy i enter men None i over är avvisat efter EN rörelse — ett
//       släpp på samma punkt ger leave och end:None, inget drop
//   17  släppet frågar appen FÖRST mot ett mål med en drop-KROK:
//       (a) onDrop → kroken → end, (b) `handled` hoppar över kroken, (c)
//       `effect = None` i onDrop hoppar över kroken och ger end:None, (d)
//       kroken sänker till None, (e) kroken får effekten VALIDERAD, (f)
//       MÅLET rivet i onDrop — utan `handled` end:None, med end:Move, (g)
//       KÄLLAN riven i onDrop — kroken ser `source == nullptr`, inget end
//   18  `canDragFrom`: en källa som säger nej i sin vänstra halva — där
//       blir trycket ett klick och ingen `onDragStart`; höger halva drar
// SABOTAGEN som ska fälla dem:
// ingen `dragOverTarget` efter enter (16 och varje
// förväntat värde), kroken före `onDrop` (17a), `handled` ignorerad (17b),
// kroken körd trots None (17c), kroken med en ovaliderad effekt (17e — den
// loggar "k:drop:?"), utfallet kvar på det föreslagna när målet rivits (17f —
// "end:Move:-"), None oavsett `handled` när målet rivits (17f andra delen —
// "end:None:-"), `e.source` inte omläst efter onDrop (17g — "k:drop:Move:satt"),
// `canDragFrom` som inte frågas eller frågas efter `onDragStart` (18 —
// "start" i loggen).
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * hur ÅTERKOPPLINGEN ser ut — ramen, märket och dragbilden ritas i
//     overlay-lagret, som `captureFrame` inte läser (den tar retained-ytan
//     före compositen). Att overlayn RITAS mäts (räknaren), inte vad den
//     ritar; utseendet är verifierat renderat i alla tre UI.
//   * backendernas egna mus-events — att Windows/macOS/Wayland/X11 faktiskt
//     levererar rörelse utanför fönstret medan knappen hålls. Grinden matar
//     ingången direkt.
//   * `TreeView`s inbyggda last och omordning — de mäts i tree_gate.
//   * AUTOSKROLLENS TAKT och den riktiga timern (tree_gate Q9) — Q1–Q3 här
//     injicerar ticket och mäter kedjan, zonerna och vakterna.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "drag_scroll.h"   // intern — autoskrollens timer-id

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

std::string fog(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : " ") + x;
    return "[" + s + "]";
}

const char* namn(glaze::DragEffect e)
{
    switch (e) {
    case glaze::DragEffect::Copy: return "Copy";
    case glaze::DragEffect::Move: return "Move";
    case glaze::DragEffect::Link: return "Link";
    case glaze::DragEffect::None: return "None";
    default:                      return "?";
    }
}

// ================================================================ HEADLESS

void fallLasten()
{
    glaze::DragData d;
    check(d.empty() && d.formats().empty(), "last: tom från början");
    check(d.get("text/plain").empty() && !d.has("text/plain"),
          "last: saknat format ger tom sträng och has() = false");

    d.setText("hej");
    d.set("application/x-nod", "0/2");
    d.set("text/uri-list", "file:///tmp/a");
    check(d.text() == "hej" && d.get(glaze::DragData::kText) == "hej",
          "last: setText/text går via text/plain", d.text());
    check(d.formats() ==
              std::vector<std::string>{"text/plain", "application/x-nod",
                                       "text/uri-list"},
          "last: formaten i PREFERENSordning (insättningsordning)",
          fog(d.formats()));

    // Ersättning sker PÅ PLATS — ordningen är källans preferens.
    d.setText("nytt");
    check(d.formats().front() == "text/plain" && d.text() == "nytt",
          "last: set på befintligt format ersätter PÅ PLATS",
          fog(d.formats()));

    // Ett TOMT värde är skilt från frånvaro.
    d.set("application/x-tom", "");
    check(d.has("application/x-tom") && d.get("application/x-tom").empty(),
          "last: tomt värde är skilt från saknat format");

    d.clear();
    check(d.empty(), "last: clear tömmer");
}

void fallEffekter()
{
    using E = glaze::DragEffect;
    const E alla = E::Copy | E::Move | E::Link;
    check(glaze::allows(alla, E::Copy) && glaze::allows(alla, E::Move) &&
              glaze::allows(alla, E::Link),
          "effekt: masken tillåter sina bitar");
    check(!glaze::allows(alla, E::None),
          "effekt: None tillåts ALDRIG (frånvaron av ett släpp)");
    check(!glaze::allows(E::Move, E::Copy) && glaze::allows(E::Move, E::Move),
          "effekt: en enbitsmask tillåter bara sin bit");
    check((alla & E::Move) == E::Move && (E::Copy & E::Move) == E::None,
          "effekt: & snittar maskerna");

    glaze::DragStartArgs s;
    check(s.allowed == alla, "effekt: källan tillåter ALLA som default",
          namn(s.allowed));
}

void fallEgenskaper()
{
    glaze::Panel p;
    check(!p.allowDrop() && !p.allowDrag(), "egenskap: båda av som default");
    check(p.setProp("allowDrop", "true") && p.allowDrop(),
          "egenskap: setProp(allowDrop) känns igen och sätter");
    check(p.setProp("allowDrag", "true") && p.allowDrag(),
          "egenskap: setProp(allowDrag) känns igen och sätter");
    check(p.setProp("allowDrop", "false") && !p.allowDrop(),
          "egenskap: setProp(allowDrop, false) släcker");
    // Basen ÄGER nycklarna — en subklass med egen setProp måste nå dem.
    glaze::ListBox l;
    check(l.setProp("allowDrop", "true") && l.allowDrop(),
          "egenskap: nyckeln når basen genom en subklass (ListBox)");
}

// ============================================================= MED FÖNSTER

// En panel som RÄKNAR sina ritningar och LOGGAR varje drag-krok och
// -handler i en gemensam logg. Krokarna loggas med en "k:"-prefix så att
// ordningen krok → handler syns.
struct Ruta : glaze::Panel {
    // Widgetens EGEN läsväg (protected — den finns för `draw()`), här
    // gjord anropbar som en subklass får göra.
    using glaze::Panel::dropEffect;

    std::string               id;
    std::vector<std::string>* logg      = nullptr;
    int                       ritningar = 0;
    int                       leaveKrok = 0;
    glaze::DragEffect         svar      = glaze::DragEffect::None;
    bool                      svaraIOver = true;
    // Fall 16: enter svarar `svar`, men over säger NEJ — en regel per plats
    // som bara står i `onDragOver`.
    bool                      nejIOver   = false;

    void koppla(const std::string& namn_, std::vector<std::string>& l)
    {
        id   = namn_;
        name = namn_;
        logg = &l;
        setBorder(true);
        onDragEnter = [this](glaze::Widget*, glaze::DragArgs& e) {
            logg->push_back(id + ":enter");
            e.effect = svar;
        };
        onDragOver = [this](glaze::Widget*, glaze::DragArgs& e) {
            logg->push_back(id + ":over");
            if (nejIOver)
                e.effect = glaze::DragEffect::None;
            else if (svaraIOver)
                e.effect = svar;
        };
        onDragLeave = [this](glaze::Widget*, glaze::DragArgs&) {
            logg->push_back(id + ":leave");
        };
        onDrop = [this](glaze::Widget*, glaze::DragArgs& e) {
            logg->push_back(id + ":drop:" + namn(e.effect) + ":" +
                            e.data.text());
        };
    }

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Panel::draw(c);
    }
    void dragLeave() override { ++leaveKrok; }
};

// Fall 17: ett mål med ett INBYGGT släpp (`TreeView`s sort). Kroken loggar
// effekten den FÅR med "k:"-prefixet — efter appens `onDrop`, validerad — och
// kan sänka den till None, som ett inbyggt släpp som inte gick. Med
// `visaKälla` loggar den också OM `e.source` är satt ("satt"/"-") — utan att
// läsa pekaren, för en dinglande källa är just det fall 17g fångar.
struct Inbyggd : Ruta {
    bool sänk      = false;
    bool visaKälla = false;

protected:
    void drop(const glaze::Ctx&, glaze::DragArgs& e) override
    {
        std::string rad = std::string("k:drop:") + namn(e.effect);
        if (visaKälla)
            rad += e.source ? ":satt" : ":-";
        logg->push_back(rad);
        if (sänk)
            e.effect = glaze::DragEffect::None;
    }
};

// Fall 18: en källa som bara låter ett drag BÖRJA i sin högra halva — frågan
// gäller TRYCKETS punkt (fysiska px, `resolve` i samma Ctx som kroken får).
struct HalvKalla : Ruta {
protected:
    bool canDragFrom(const glaze::Ctx& c, float x, float) const override
    {
        const gfx::Rectf r = resolve(c);
        return x >= r.x + r.w / 2.0f;
    }
};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    std::vector<std::string> logg;
    Ruta  kalla, malA, malB, avvisare, grupp, tom;
    Ruta  barn, barnAv;   // i `grupp`: barn utan allowDrop, mål som är av
    Ruta  rörlig;         // fall 15: ett mål som lämnar trädet mitt i draget
    Inbyggd   inbyggd;    // fall 17: ett mål med drop-krok
    HalvKalla halv;       // fall 18: en källa med canDragFrom
    // Två fokuserbara fält — ett drag ska vara MODALT för tangentbordet,
    // och det syns bara om en Tab annars HADE flyttat fokus någonstans.
    glaze::TextInput falt1, falt2;
    int   klick      = 0;
    int   overlays   = 0;
    int   kontext    = 0;
    glaze::DragEffect tillatet = glaze::DragEffect::Copy |
                                 glaze::DragEffect::Move |
                                 glaze::DragEffect::Link;
    bool  ladda      = true;   // false = källan avstår (tom last)

    Vard() : glaze::AppWindow("dnd_gate", 420, 300, gfx::Chrome::None)
    {
        auto lägg = [](Ruta& r, float x, float y, float w, float h) {
            r.x = x;
            r.y = y;
            r.width = w;
            r.height = h;
        };
        lägg(kalla, 10, 10, 60, 40);
        lägg(malA, 100, 10, 80, 60);
        lägg(malB, 200, 10, 80, 60);
        lägg(avvisare, 100, 100, 80, 60);
        lägg(grupp, 200, 100, 140, 100);
        lägg(tom, 10, 200, 60, 40);
        lägg(barn, 10, 10, 50, 40);
        lägg(barnAv, 70, 10, 50, 40);

        kalla.koppla("K", logg);
        malA.koppla("A", logg);
        malB.koppla("B", logg);
        avvisare.koppla("R", logg);
        grupp.koppla("G", logg);
        tom.koppla("T", logg);
        barn.koppla("b", logg);
        barnAv.koppla("x", logg);

        kalla.setAllowDrag(true);
        kalla.onDragStart = [this](glaze::Widget*, glaze::DragStartArgs& e) {
            logg.push_back("start");
            if (ladda)
                e.data.setText("lasten");
            e.allowed = tillatet;
        };
        kalla.onDragEnd = [this](glaze::Widget*, glaze::DragEndArgs& e) {
            logg.push_back(std::string("end:") + namn(e.effect) + ":" +
                           (e.target ? e.target->name : "-"));
        };
        kalla.onClick = [this] { ++klick; };

        malA.setAllowDrop(true);
        malA.svar = glaze::DragEffect::Copy;
        malB.setAllowDrop(true);
        malB.svar = glaze::DragEffect::Move;
        avvisare.setAllowDrop(true);   // tar emot — men svarar aldrig ja
        grupp.setAllowDrop(true);
        grupp.svar = glaze::DragEffect::Link;
        barnAv.setAllowDrop(true);
        barnAv.svar = glaze::DragEffect::Copy;
        barnAv.disable();              // ett AV-mål hoppas över, uppåt
        // `barn` och `tom` har INTE allowDrop.

        root().adopt(kalla);
        root().adopt(malA);
        root().adopt(malB);
        root().adopt(avvisare);
        root().adopt(grupp);
        grupp.adopt(barn);
        grupp.adopt(barnAv);
        root().adopt(tom);

        falt1.x     = 100;
        falt1.y     = 210;
        falt1.width = 80;
        falt1.name  = "falt1";
        falt2.x     = 200;
        falt2.y     = 210;
        falt2.width = 80;
        falt2.name  = "falt2";
        root().adopt(falt1);
        root().adopt(falt2);

        root().onContextMenu = [this] { ++kontext; };
    }

    void overlay(const glaze::Ctx&) override { ++overlays; }

    // ---- händelserna, genom den RIKTIGA ingången
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    void mus(gfx::EventType t, float x, float y,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = b;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p.x, p.y); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p.x, p.y); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p.x, p.y); }
    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void skriv(const char* utf8)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::snprintf(e.text, sizeof e.text, "%s", utf8);
        handleEvent(e);
    }

    // Tröskeln i FYSISKA px — det `handleEvent` jämför i.
    float troskel() { return style().dragThreshold * window().uiScale(); }

    // Starta ett drag från källan: tryck i mitten, rör en bit FÖRBI
    // tröskeln. Loggen töms först så varje fall läser sin egen sekvens.
    void starta()
    {
        logg.clear();
        const gfx::Pointf k = mitt(kalla);
        ner(k);
        flytta({k.x + troskel() + 2.0f, k.y});
    }

    int ritningarTotalt() const
    {
        return kalla.ritningar + malA.ritningar + malB.ritningar +
               avvisare.ritningar + grupp.ritningar + tom.ritningar +
               barn.ritningar + barnAv.ritningar;
    }
    void nollaRitningar()
    {
        for (Ruta* r : {&kalla, &malA, &malB, &avvisare, &grupp, &tom, &barn,
                        &barnAv})
            r->ritningar = 0;
        overlays = 0;
    }
};

// ---- AUTOSKROLLEN (Q1–Q3) — GENERISKT: en
//      rullande `Panel`, en rullande lista i den, en inaktiv rullande panel
//      och en rot med innehåll utanför fönstret. Draget går från en vanlig
//      källa, och ticket INJICERAS (`kDragScrollTimerId`) — tree_gate Q9 mäter
//      den riktiga timern. SABOTAGEN: kedjan utan bubbling (Q2 — den yttre
//      rullar inte), utan vakt för inaktiverade eller med roten (Q3), zonen
//      räknad mot widgetens rect i stället för rullningsytan (Q1 sidled).
//      MÄTER INTE en klippt, nästlad panels synliga kant.
struct RullVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Ruta           kalla, golv;
    glaze::Panel   yttre, innehall, av, avInnehall, langt;
    glaze::ListBox inre;
    std::vector<std::string> logg;

    RullVard() : glaze::AppWindow("dnd_gate_rull", 420, 300, gfx::Chrome::None)
    {
        const auto lägg = [](glaze::Widget& r, float x, float y, float w, float h) {
            r.x = x; r.y = y; r.width = w; r.height = h;
        };
        lägg(kalla, 10, 10, 60, 40);
        kalla.koppla("K", logg);
        kalla.setAllowDrag(true);
        kalla.onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("lasten");
        };
        lägg(yttre, 100, 10, 150, 150);
        lägg(innehall, 0, 0, 400, 600);   // spelrum i båda axlarna
        lägg(inre, 0, 10, 120, 140);      // nederkanten = den yttres
        std::vector<std::string> rader;
        for (int i = 0; i < 30; ++i)
            rader.push_back("r" + std::to_string(i));
        inre.setItems(rader);
        lägg(av, 270, 10, 120, 120);
        lägg(avInnehall, 0, 0, 300, 300);
        av.disable();
        lägg(golv, 10, 250, 400, 40);
        golv.koppla("G", logg);
        lägg(langt, 10, 1000, 40, 40);    // rotens innehåll når utanför

        root().adopt(kalla);
        root().adopt(yttre);
        yttre.adopt(innehall);
        yttre.adopt(inre);
        root().adopt(av);
        av.adopt(avInnehall);
        root().adopt(golv);
        root().adopt(langt);
    }

    gfx::Pointf punkt(glaze::Widget& w, float lx, float ly)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{r.x + lx * s, r.y + ly * s};
    }
    void mus(gfx::EventType t, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void tick()
    {
        gfx::Event e;
        e.type     = gfx::EventType::Timer;
        e.windowId = window().id();
        e.timerId  = glaze::kDragScrollTimerId;
        handleEvent(e);
    }
};

void fallAutoskroll()
{
    RullVard w;
    w.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const float steg = w.style().dragScrollStep;
        const float zon  = w.style().dragScrollZone;
        const gfx::Pointf k = w.punkt(w.kalla, 30, 20);
        w.mus(gfx::EventType::MouseButtonDown, k);
        w.mus(gfx::EventType::MouseMotion,
              {k.x + w.style().dragThreshold * w.window().uiScale() + 2.0f, k.y});
        check(w.isDragging(), "Q: draget från källan pågår");

        // ---- Q1. Den rullande panelen: botten-, topp- och sidzonen rullar,
        //          mitten gör det inte.
        w.yttre.setScroll(0.0f, 50.0f);
        const auto vid = [&](float lx, float ly) {
            w.mus(gfx::EventType::MouseMotion, w.punkt(w.yttre, lx, ly));
            w.tick();
        };
        vid(127.0f, 75.0f);
        check(w.yttre.scrollY() == 50.0f && w.yttre.scrollX() == 0.0f,
              "Q1: mitten — inget steg");
        vid(127.0f, 150.0f - zon / 2.0f);
        check(std::abs(w.yttre.scrollY() - (50.0f + steg)) < 1e-3f,
              "Q1: nederzonen — ett steg nedåt", std::to_string(w.yttre.scrollY()));
        vid(127.0f, zon / 2.0f);
        check(std::abs(w.yttre.scrollY() - 50.0f) < 1e-3f,
              "Q1: toppzonen — ett steg uppåt", std::to_string(w.yttre.scrollY()));
        vid(150.0f - zon / 2.0f, 75.0f);
        check(std::abs(w.yttre.scrollX() - steg) < 1e-3f,
              "Q1: sidzonen — ett steg i sidled", std::to_string(w.yttre.scrollX()));

        // ---- Q2. BUBBLINGEN: listan rullar först, och vid sitt slut lämnar
        //          den till den yttre panelen.
        w.yttre.setScroll(0.0f, 0.0f);
        w.inre.setScroll(0.0f, 0.0f);
        vid(60.0f, 150.0f - zon / 2.0f);
        check(w.inre.scrollY() > 0.0f && w.yttre.scrollY() == 0.0f,
              "Q2: i listans nederzon rullar listan, inte panelen");
        w.yttre.setScroll(0.0f, 0.0f);
        w.inre.setScroll(0.0f, 1.0e6f);   // klampas till listans slut
        const float inreFöre = w.inre.scrollY();
        vid(60.0f, 150.0f - zon / 2.0f);
        check(w.inre.scrollY() == inreFöre &&
                  std::abs(w.yttre.scrollY() - steg) < 1e-3f,
              "Q2: listan vid sitt slut — den yttre panelen rullar",
              std::to_string(w.yttre.scrollY()));

        // ---- Q3. Den INAKTIVA panelen och ROTEN rullar aldrig.
        w.mus(gfx::EventType::MouseMotion, w.punkt(w.av, 60.0f, 120.0f - zon / 2.0f));
        w.tick();
        check(w.av.scrollY() == 0.0f, "Q3: en inaktiv panel rullar inte");
        w.mus(gfx::EventType::MouseMotion, w.punkt(w.golv, 200.0f, 40.0f - 2.0f));
        w.tick();
        check(static_cast<glaze::Panel&>(w.root()).scrollY() == 0.0f,
              "Q3: roten rullar inte, fast dess innehåll når utanför");

        gfx::Event esc;
        esc.type     = gfx::EventType::KeyDown;
        esc.windowId = w.window().id();
        esc.key      = gfx::Key::Escape;
        w.handleEvent(esc);
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   autoskrollen hoppades över: inget fönster kunde "
                    "skapas\n");
        ++g_skips;
    }
}

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);

    // `run()` ÄR öppnaren — aldrig show() först (dropdown_gate-läxan:
    // `runInternal` avvisar ett öppet fönster och hela kedjan tystnar).
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const gfx::Pointf K = w.mitt(w.kalla);
        const gfx::Pointf A = w.mitt(w.malA);
        const gfx::Pointf B = w.mitt(w.malB);
        const gfx::Pointf R = w.mitt(w.avvisare);
        const gfx::Pointf T = w.mitt(w.tom);
        const float       t = w.troskel();

        // ---- 1. UNDER TRÖSKELN är ett tryck ett tryck.
        w.logg.clear();
        w.ner(K);
        w.flytta({K.x + t * 0.5f, K.y});
        check(!w.isDragging() && w.logg.empty(),
              "1: rörelse under tröskeln startar inget drag", fog(w.logg));
        w.upp({K.x + t * 0.5f, K.y});
        check(w.klick == 1, "1: släppt under tröskeln är ett KLICK",
              std::to_string(w.klick));

        // ---- 2. TRÖSKELN, och hela livscykeln över två mål.
        w.logg.clear();
        w.ner(K);
        w.flytta({K.x + t, K.y});   // EXAKT tröskeln — "mer än" krävs
        check(!w.isDragging() && w.logg.empty(),
              "2: exakt tröskeln startar INTE (mer än krävs)", fog(w.logg));
        w.flytta({K.x + t + 1.0f, K.y});
        check(w.isDragging() && w.logg == std::vector<std::string>{"start"},
              "2: förbi tröskeln startar draget — onDragStart en gång",
              fog(w.logg));
        w.flytta(A);
        w.flytta({A.x + 3.0f, A.y + 2.0f});
        check(w.logg == std::vector<std::string>{"start", "A:enter", "A:over",
                                                 "A:over"},
              "2: enter OCH over vid första rörelsen in (B9), over därefter",
              fog(w.logg));
        check(w.malA.dropEffect() == glaze::DragEffect::Copy,
              "2: målets dropEffect speglar det accepterade valet",
              namn(w.malA.dropEffect()));
        w.flytta(B);
        check(w.logg == std::vector<std::string>{"start", "A:enter", "A:over",
                                                 "A:over", "A:leave",
                                                 "B:enter", "B:over"},
              "2: byte av mål — LEAVE före ENTER", fog(w.logg));
        check(w.malA.dropEffect() == glaze::DragEffect::None &&
                  w.malA.leaveKrok == 1,
              "2: det lämnade målet nollas och får sin dragLeave-krok",
              namn(w.malA.dropEffect()));
        w.upp(B);
        check(w.logg ==
                  std::vector<std::string>{"start", "A:enter", "A:over",
                                           "A:over", "A:leave", "B:enter",
                                           "B:over", "B:drop:Move:lasten",
                                           "end:Move:B"},
              "2: släpp — onDrop på målet, SIST onDragEnd på källan",
              fog(w.logg));
        check(!w.isDragging() && w.klick == 1,
              "2: draget är slut, och ett drag ger INGET klick",
              std::to_string(w.klick));
        check(w.malB.dropEffect() == glaze::DragEffect::None &&
                  w.malB.leaveKrok == 1,
              "2: målet städas efter släppet (kroken, inte handlern)",
              std::to_string(w.malB.leaveKrok));

        // ---- 3. AVVISAT SLÄPP — leave i stället för drop (OLE-ordningen).
        w.starta();
        w.flytta(R);
        w.upp(R);
        check(w.logg == std::vector<std::string>{"start", "R:enter", "R:over",
                                                 "R:leave", "end:None:-"},
              "3: ett mål som aldrig sa ja får LEAVE och källan None",
              fog(w.logg));

        // ---- 4. ESCAPE avbryter, och trycket är förbrukat.
        w.starta();
        w.flytta(A);
        w.tangent(gfx::Key::A);   // andra tangenter sväljs, avbryter inte
        check(w.isDragging(), "4: en annan tangent avbryter inte");
        w.tangent(gfx::Key::Escape);
        check(!w.isDragging() &&
                  w.logg == std::vector<std::string>{"start", "A:enter",
                                                     "A:over", "A:leave",
                                                     "end:None:-"},
              "4: Escape — leave på målet, None till källan", fog(w.logg));
        w.flytta({A.x + 10.0f, A.y});
        w.upp(K);
        check(w.logg.size() == 5 && w.klick == 1,
              "4: efter Escape: inget nytt drag och inget klick",
              fog(w.logg) + " klick " + std::to_string(w.klick));

        // 4b. TANGENTBORDET ÄR MODALT under draget: en Tab flyttar inte
        //     fokus och ett tecken når inte det fokuserade fältet. Källan
        //     är ingen fokuserbar widget, så trycket på den lämnar fokus
        //     kvar i fältet — precis det läge där en läckande tangent syns.
        const gfx::Pointf F = w.mitt(w.falt1);
        w.ner(F);
        w.upp(F);
        const std::string före = w.falt1.text();
        check(w.falt1.focused(), "4b: fältet har fokus före draget");
        w.starta();
        w.tangent(gfx::Key::Tab);
        w.skriv("x");
        check(w.isDragging() && w.falt1.focused() && !w.falt2.focused() &&
                  w.falt1.text() == före,
              "4b: Tab och text sväljs under draget",
              "fokus " + std::string(w.falt1.focused() ? "falt1" : "?") +
                  " text \"" + w.falt1.text() + "\"");
        w.tangent(gfx::Key::Escape);
        w.upp(K);
        w.tangent(gfx::Key::Tab);   // kontroll: utan drag FLYTTAR Tab
        check(w.falt2.focused(), "4b: …och efter draget flyttar Tab igen");

        // ---- 5. SLÄPP UTANFÖR varje mål gör ingenting.
        w.starta();
        w.flytta(T);   // `tom` har inte allowDrop — och roten inte heller
        w.upp(T);
        check(w.logg == std::vector<std::string>{"start", "end:None:-"},
              "5: släpp över en yta utan mål — bara källans slut",
              fog(w.logg));

        // 5b. UTANFÖR FÖNSTRET — även när ROTEN är ett mål. Draget börjar
        //     över källan, som ligger i roten, så roten blir målet direkt;
        //     ute i (-40, -40) ska den lämnas och släppet bli None. Utan
        //     klientytans gräns hade `widgetAt`s nullptr routats till
        //     roten och släppet gått fram (end:Copy).
        w.root().setAllowDrop(true);
        w.root().onDragEnter = [&](glaze::Widget*, glaze::DragArgs& e) {
            w.logg.push_back("W:enter");
            e.effect = glaze::DragEffect::Copy;
        };
        w.root().onDragLeave = [&] { w.logg.push_back("W:leave"); };
        w.starta();
        w.flytta({-40.0f, -40.0f});
        w.upp({-40.0f, -40.0f});
        check(w.logg == std::vector<std::string>{"start", "W:enter", "W:leave",
                                                 "end:None:-"},
              "5b: utanför klientytan finns inget mål (inte ens roten)",
              fog(w.logg));
        w.root().setAllowDrop(false);
        w.root().onDragEnter = nullptr;
        w.root().onDragLeave = nullptr;

        // ---- 6. ROUTING uppåt, och ett AV-mål hoppas över.
        w.starta();
        w.flytta(w.mitt(w.barn));
        w.flytta(w.mitt(w.barnAv));
        check(w.logg == std::vector<std::string>{"start", "G:enter", "G:over",
                                                 "G:over"},
              "6: barnet utan allowDrop ger förälderns mål; AV-målet "
              "hoppas över",
              fog(w.logg));
        w.tangent(gfx::Key::Escape);

        // ---- 7. INGENTING RITAS OM när ingenting ändras.
        w.starta();
        w.flytta(A);                       // enter: handler ⇒ full omritning
        w.window().renderFrame();          // töm all damage
        w.nollaRitningar();
        w.flytta({A.x + 4.0f, A.y + 1.0f});   // over, samma svar
        w.window().renderFrame();
        check(w.ritningarTotalt() == 0,
              "7: en rörelse inom samma mål ritar ingen widget",
              std::to_string(w.ritningarTotalt()));
        check(w.overlays >= 1,
              "7: …men overlayn (dragbilden) komponeras om",
              std::to_string(w.overlays));
        // Byter målet SVAR ändras dess dropEffect — då och bara då
        // invalideras det.
        w.malA.svar = glaze::DragEffect::None;
        w.nollaRitningar();
        w.flytta({A.x + 6.0f, A.y + 1.0f});
        w.window().renderFrame();
        check(w.malA.ritningar >= 1 &&
                  w.malA.dropEffect() == glaze::DragEffect::None,
              "7: ett ÄNDRAT svar invaliderar målet",
              std::to_string(w.malA.ritningar));
        w.malA.svar = glaze::DragEffect::Copy;
        w.tangent(gfx::Key::Escape);

        // ---- 8. TOM LAST = INGET DRAG — trycket förblir ett tryck.
        w.ladda = false;
        w.starta();
        check(!w.isDragging() && w.logg == std::vector<std::string>{"start"},
              "8: en källa som inte fyller lasten startar inget drag",
              fog(w.logg));
        w.flytta({K.x + t * 3.0f, K.y});
        check(w.logg.size() == 1,
              "8: tröskeln frågas inte igen för samma tryck", fog(w.logg));
        const int klickFöre = w.klick;
        w.upp(K);
        check(w.klick == klickFöre + 1,
              "8: släppt över källan är ett vanligt klick",
              std::to_string(w.klick - klickFöre));
        w.ladda = true;

        // ---- 9. allowDrag AV — onDragStart frågas aldrig.
        w.kalla.setAllowDrag(false);
        w.starta();
        check(w.logg.empty() && !w.isDragging(),
              "9: utan allowDrag är onDragStart död", fog(w.logg));
        w.upp(K);
        w.kalla.setAllowDrag(true);

        // ---- 10. EFFEKTEN valideras mot källans mask.
        w.tillatet = glaze::DragEffect::Move;
        w.starta();
        w.flytta(A);   // A vill Copy — källan tillåter bara Move
        check(w.malA.dropEffect() == glaze::DragEffect::None,
              "10: ett val utanför masken räknas som None",
              namn(w.malA.dropEffect()));
        w.upp(A);
        check(w.logg == std::vector<std::string>{"start", "A:enter", "A:over",
                                                 "A:leave", "end:None:-"},
              "10: …och släppet blir ett avvisat släpp", fog(w.logg));
        // FLERA bitar väljs ner till EN, Copy först — men bara bland de
        // tillåtna.
        w.malA.svar = glaze::DragEffect::Copy | glaze::DragEffect::Move;
        w.starta();
        w.flytta(A);
        check(w.malA.dropEffect() == glaze::DragEffect::Move,
              "10: flera bitar → den tillåtna (Move)",
              namn(w.malA.dropEffect()));
        w.tangent(gfx::Key::Escape);
        w.tillatet = glaze::DragEffect::Copy | glaze::DragEffect::Move |
                     glaze::DragEffect::Link;
        w.starta();
        w.flytta(A);
        check(w.malA.dropEffect() == glaze::DragEffect::Copy,
              "10: flera bitar, allt tillåtet → Copy (förstör ingenting)",
              namn(w.malA.dropEffect()));
        w.tangent(gfx::Key::Escape);
        w.malA.svar = glaze::DragEffect::Copy;

        // ---- 11. FOKUSFÖRLUST avbryter som Escape.
        w.starta();
        w.flytta(B);
        w.enkel(gfx::EventType::WindowFocusLost);
        check(!w.isDragging() &&
                  w.logg == std::vector<std::string>{"start", "B:enter",
                                                     "B:over", "B:leave",
                                                     "end:None:-"},
              "11: WindowFocusLost avbryter — leave + None", fog(w.logg));
        w.upp(B);

        // ---- 12. HÖGERKLICK mitt i ett drag sväljs.
        w.starta();
        w.mus(gfx::EventType::MouseButtonDown, A.x, A.y,
              gfx::MouseButton::Right);
        check(w.kontext == 0 && w.isDragging(),
              "12: högertryck under drag — ingen snabbmeny, draget lever",
              std::to_string(w.kontext));
        w.tangent(gfx::Key::Escape);
        w.upp(K);

        // ---- 13. PEKAREN LÄMNAR FÖNSTRET (MouseLeave) — målet lämnas,
        //          draget lever, och släppet utanför är None.
        w.starta();
        w.flytta(A);
        w.enkel(gfx::EventType::MouseLeave);
        check(w.isDragging() &&
                  w.logg == std::vector<std::string>{"start", "A:enter",
                                                     "A:over", "A:leave"},
              "13: MouseLeave lämnar målet men avbryter inte draget",
              fog(w.logg));
        w.flytta(A);   // tillbaka in
        w.upp(A);
        check(w.logg.size() == 8 && w.logg[4] == "A:enter" &&
                  w.logg[7] == "end:Copy:A",
              "13: tillbaka in — enter igen, och släppet går fram",
              fog(w.logg));

        // ---- 14. KÄLLAN LÄMNAR TRÄDET mitt i ett drag: tyst slut, ingen
        //          dinglande pekare. En ÄGD källa förstörs av unadopt, så en
        //          kvarglömd pekare hade kraschat här i stället för att bli
        //          röd.
        auto* ägd = w.root().add<Ruta>();
        ägd->x      = 10;
        ägd->y      = 100;
        ägd->width  = 60;
        ägd->height = 40;
        ägd->name   = "agd";
        ägd->setAllowDrag(true);
        ägd->onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("flyktig");
        };
        int slut = 0;
        ägd->onDragEnd = [&slut] { ++slut; };
        w.logg.clear();
        const gfx::Pointf Q = w.mitt(*ägd);
        const int leaveFöre = w.malA.leaveKrok;
        w.ner(Q);
        w.flytta({Q.x + t + 2.0f, Q.y});
        w.flytta(A);
        check(w.isDragging(), "14: draget från den ägda källan pågår");
        check(w.root().unadopt(*ägd), "14: källan unadopteras mitt i draget");
        check(!w.isDragging() && slut == 0,
              "14: draget slutar TYST — ingen onDragEnd på en förstörd källa",
              std::to_string(slut));
        check(w.malA.leaveKrok == leaveFöre + 1 &&
                  w.malA.dropEffect() == glaze::DragEffect::None,
              "14: målet får sin STÄDkrok (dragLeave) och nollas",
              std::to_string(w.malA.leaveKrok - leaveFöre));
        check(w.logg == std::vector<std::string>{"A:enter", "A:over"},
              "14: …men ingen APPhandler körs från unadopt", fog(w.logg));
        w.flytta(B);   // ingenting ska hända, och inget ska krascha
        w.upp(B);
        check(w.logg == std::vector<std::string>{"A:enter", "A:over"},
              "14: efteråt är musen bara en mus", fog(w.logg));

        // ---- 15. MÅLET LÄMNAR TRÄDET mitt i ett drag — det andra hålet i
        //          `detachTree`. Målet är LÅNAT, så det LEVER efter
        //          unadopt: en kvarglömd sessionspekare blir då inte en
        //          krasch utan ett leave på fel widget i NÄSTA rörelse, och
        //          det syns i loggen. Draget självt ska leva vidare — det
        //          är källan som äger det, och den sitter kvar.
        w.rörlig.koppla("X", w.logg);
        w.rörlig.x      = 350;
        w.rörlig.y      = 10;
        w.rörlig.width  = 60;
        w.rörlig.height = 60;
        w.rörlig.setAllowDrop(true);
        w.rörlig.svar = glaze::DragEffect::Copy;
        w.root().adopt(w.rörlig);
        const gfx::Pointf X = w.mitt(w.rörlig);
        w.starta();
        w.flytta(X);
        check(w.logg == std::vector<std::string>{"start", "X:enter",
                                                 "X:over"} &&
                  w.rörlig.dropEffect() == glaze::DragEffect::Copy,
              "15: draget står över det rörliga målet", fog(w.logg));
        check(w.root().unadopt(w.rörlig),
              "15: målet unadopteras mitt i draget");
        check(w.isDragging(),
              "15: draget lever — källan sitter kvar och äger det");
        check(w.rörlig.leaveKrok == 1 &&
                  w.rörlig.dropEffect() == glaze::DragEffect::None,
              "15: målet får sin STÄDkrok (dragLeave) och nollas",
              std::to_string(w.rörlig.leaveKrok));
        check(w.logg == std::vector<std::string>{"start", "X:enter",
                                                 "X:over"},
              "15: …men ingen APPhandler körs från unadopt", fog(w.logg));
        w.flytta(B);
        check(w.logg == std::vector<std::string>{"start", "X:enter", "X:over",
                                                 "B:enter", "B:over"} &&
                  w.rörlig.leaveKrok == 1,
              "15: nästa rörelse lämnar INTE det gamla målet en gång till",
              fog(w.logg));
        w.upp(B);
        check(w.logg.size() == 7 && w.logg[5] == "B:drop:Move:lasten" &&
                  w.logg[6] == "end:Move:B",
              "15: släppet på det nya målet går fram", fog(w.logg));

        // ---- 16. INTRÄDET MÖTER onDragOver: enter
        //          och over i SAMMA rörelse, och det är overs svar som
        //          gäller — en regel som bara står i `onDragOver` håller för
        //          ett släpp utan en enda rörelse efter inträdet.
        w.starta();
        w.flytta(A);
        check(w.logg ==
                  std::vector<std::string>{"start", "A:enter", "A:over"},
              "16: EN rörelse in i målet — enter OCH over", fog(w.logg));
        w.tangent(gfx::Key::Escape);
        w.upp(A);
        w.malA.nejIOver = true;   // enter säger Copy, over säger nej
        w.starta();
        w.flytta(A);
        check(w.malA.dropEffect() == glaze::DragEffect::None,
              "16: enter Copy, over None — vetot gäller efter EN rörelse",
              namn(w.malA.dropEffect()));
        w.upp(A);
        check(w.logg == std::vector<std::string>{"start", "A:enter", "A:over",
                                                 "A:leave", "end:None:-"},
              "16: släppt på samma punkt — leave och None, inget drop",
              fog(w.logg));
        w.malA.nejIOver = false;

        // ---- 17. SLÄPPET FRÅGAR APPEN FÖRST —
        //          mot ett mål med en drop-KROK som loggar vad den får.
        w.inbyggd.koppla("D", w.logg);
        w.inbyggd.x      = 350;
        w.inbyggd.y      = 100;
        w.inbyggd.width  = 60;
        w.inbyggd.height = 60;
        w.inbyggd.setAllowDrop(true);
        w.inbyggd.svar = glaze::DragEffect::Move;
        w.root().adopt(w.inbyggd);
        const gfx::Pointf D = w.mitt(w.inbyggd);
        // (a) ORDNINGEN: appens onDrop, sedan kroken, sist källans slut.
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop:Move:lasten",
                                                 "k:drop:Move", "end:Move:D"},
              "17a: onDrop FÖRE det inbyggda släppet, källans slut sist",
              fog(w.logg));
        // (b) `handled` — appen utförde släppet själv: ingen krok, och
        //     effekten är utfallet källan får.
        w.inbyggd.onDrop = [&w](glaze::Widget*, glaze::DragArgs& e) {
            w.logg.push_back("D:drop");
            e.handled = true;
        };
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "end:Move:D"},
              "17b: handled — det inbyggda hoppas över, källan får Move",
              fog(w.logg));
        // (c) `effect = None` — appen nekar: ingen krok, och None till
        //     källan. Utan `handled`, så det är nekandet ensamt som håller
        //     kroken borta; sedan med `handled` också — nekandet VINNER.
        w.inbyggd.onDrop = [&w](glaze::Widget*, glaze::DragArgs& e) {
            w.logg.push_back("D:drop");
            e.effect = glaze::DragEffect::None;
        };
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "end:None:-"},
              "17c: effect = None i onDrop — ingen krok, källan får None",
              fog(w.logg));
        w.inbyggd.onDrop = [&w](glaze::Widget*, glaze::DragArgs& e) {
            w.logg.push_back("D:drop");
            e.effect  = glaze::DragEffect::None;
            e.handled = true;
        };
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "end:None:-"},
              "17c: None OCH handled — nekandet vinner, källan får None",
              fog(w.logg));
        // (d) kroken SÄNKER till None — släppet gick inte.
        w.inbyggd.koppla("D", w.logg);
        w.inbyggd.sänk = true;
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop:Move:lasten",
                                                 "k:drop:Move", "end:None:-"},
              "17d: kroken sänker till None — källan får None",
              fog(w.logg));
        w.inbyggd.sänk = false;
        // (e) kroken får effekten VALIDERAD mot källans mask: appen sätter
        //     Copy|Move, källan tillåter bara Move.
        w.tillatet = glaze::DragEffect::Move;
        w.inbyggd.onDrop = [&w](glaze::Widget*, glaze::DragArgs& e) {
            w.logg.push_back("D:drop");
            e.effect = glaze::DragEffect::Copy | glaze::DragEffect::Move;
        };
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "k:drop:Move",
                                                 "end:Move:D"},
              "17e: kroken ser den VALIDERADE effekten (Move), aldrig "
              "Copy|Move",
              fog(w.logg));
        w.tillatet = glaze::DragEffect::Copy | glaze::DragEffect::Move |
                     glaze::DragEffect::Link;
        // (f) appen tar MÅLET ur trädet i onDrop. Utan `handled` utförde
        //     ingen släppet: ingen krok, och källan får None — inte det
        //     föreslagna Move, som hade fått en TreeView-källa att ta bort
        //     det som drogs. MED `handled` utförde appen det, och Move står.
        //     Målet är LÅNAT och lever, så det adopteras tillbaka efteråt.
        const int leaveFöreF = w.inbyggd.leaveKrok;
        w.inbyggd.onDrop = [&w](glaze::Widget*, glaze::DragArgs&) {
            w.logg.push_back("D:drop");
            w.root().unadopt(w.inbyggd);
        };
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "end:None:-"},
              "17f: målet rivet i onDrop utan handled — ingen krok, källan "
              "får None",
              fog(w.logg));
        check(w.inbyggd.leaveKrok == leaveFöreF + 1,
              "17f: …och målet fick sin städkrok EN gång",
              std::to_string(w.inbyggd.leaveKrok - leaveFöreF));
        w.root().adopt(w.inbyggd);
        w.inbyggd.onDrop = [&w](glaze::Widget*, glaze::DragArgs& e) {
            w.logg.push_back("D:drop");
            e.handled = true;
            w.root().unadopt(w.inbyggd);
        };
        w.starta();
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "end:Move:-"},
              "17f: målet rivet i onDrop MED handled — appen utförde "
              "släppet, källan får Move",
              fog(w.logg));
        w.root().adopt(w.inbyggd);
        // (g) appen river KÄLLAN i onDrop: kroken får `e.source == nullptr`
        //     och aldrig den gamla pekaren, och ingen onDragEnd körs på en
        //     förstörd källa. Källan är ÄGD, så `unadopt` förstör den (fall
        //     14:s form) — en kvarglömd pekare loggas som "satt".
        auto* flykt   = w.root().add<Ruta>();
        flykt->x      = 10;
        flykt->y      = 100;
        flykt->width  = 60;
        flykt->height = 40;
        flykt->name   = "flykt";
        flykt->setAllowDrag(true);
        flykt->onDragStart = [&w](glaze::Widget*, glaze::DragStartArgs& e) {
            w.logg.push_back("start");
            e.data.setText("flyktig");
            e.allowed = glaze::DragEffect::Move;
        };
        flykt->onDragEnd = [&w] { w.logg.push_back("end"); };
        w.inbyggd.onDrop = [&w, flykt](glaze::Widget*, glaze::DragArgs&) {
            w.logg.push_back("D:drop");
            w.root().unadopt(*flykt);
        };
        w.inbyggd.visaKälla = true;
        w.logg.clear();
        const gfx::Pointf FK = w.mitt(*flykt);
        w.ner(FK);
        w.flytta({FK.x + t + 2.0f, FK.y});
        w.flytta(D);
        w.upp(D);
        check(w.logg == std::vector<std::string>{"start", "D:enter", "D:over",
                                                 "D:drop", "k:drop:Move:-"},
              "17g: källan riven i onDrop — kroken ser source == nullptr, "
              "och ingen onDragEnd",
              fog(w.logg));
        w.inbyggd.visaKälla = false;
        w.inbyggd.onDrop    = nullptr;   // pekaren till den rivna källan
        w.root().unadopt(w.inbyggd);

        // ---- 18. VAR DRAGET FÅR BÖRJA: källan säger
        //          nej i sin VÄNSTRA halva. Där förblir trycket ett tryck —
        //          ingen onDragStart, fast den hade fyllt lasten, och ett
        //          klick vid släppet. Höger halva startar draget.
        w.halv.koppla("H", w.logg);
        w.halv.x      = 10;
        w.halv.y      = 100;
        w.halv.width  = 60;
        w.halv.height = 40;
        w.halv.setAllowDrag(true);
        w.halv.onDragStart = [&w](glaze::Widget*, glaze::DragStartArgs& e) {
            w.logg.push_back("start");
            e.data.setText("halv");
        };
        int halvKlick = 0;
        w.halv.onClick = [&halvKlick] { ++halvKlick; };
        w.root().adopt(w.halv);
        const gfx::Rectf  hr = w.halv.resolve(w.halv.behaviorCtx());
        const gfx::Pointf HV{hr.x + hr.w * 0.25f, hr.y + hr.h / 2.0f};
        const gfx::Pointf HH{hr.x + hr.w * 0.75f, hr.y + hr.h / 2.0f};
        w.logg.clear();
        w.ner(HV);
        w.flytta({HV.x, HV.y + t + 2.0f});   // förbi tröskeln, kvar i halvan
        check(!w.isDragging() && w.logg.empty(),
              "18: tryck i den VÄNSTRA halvan + rörelse — inget drag och "
              "ingen onDragStart",
              fog(w.logg));
        w.upp({HV.x, HV.y + t + 2.0f});
        check(halvKlick == 1,
              "18: …och trycket förblev ett tryck — släppet är ett klick",
              std::to_string(halvKlick));
        w.logg.clear();
        w.ner(HH);
        w.flytta({HH.x, HH.y + t + 2.0f});
        check(w.isDragging() && w.logg == std::vector<std::string>{"start"},
              "18: tryck i den HÖGRA halvan + rörelse startar draget",
              fog(w.logg));
        w.tangent(gfx::Key::Escape);
        w.upp({HH.x, HH.y + t + 2.0f});
        check(halvKlick == 1, "18: ett drag ger inget klick",
              std::to_string(halvKlick));
        w.root().unadopt(w.halv);

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

    std::printf("dnd_gate\n");
    fallLasten();
    fallEffekter();
    fallEgenskaper();
    fallMedFonster();
    fallAutoskroll();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
