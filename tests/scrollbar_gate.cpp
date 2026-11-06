// scrollbar_gate — RULLNINGSLISTORNAS SYNLIGHET: arvet i tre nivåer
// (widget → förfäder → fönster → app), de tre lägena Auto, Visible och
// Invisible, och Auto-lägets tid — genom FÖNSTRETS ingång (`handleEvent`) i
// ett DOLT fönster med riktiga widgets, mot en klocka grinden styr själv
// (`detail::setScrollbarClock`) och fönstrets timer-id matat syntetiskt
// (`kScrollbarTimerId`, som tooltip_gate matar tooltipens).
//
// Kör:  ./build/tests/scrollbar_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönstret kunde inte
//       skapas (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// VAD GRINDEN PÅSTÅR:
//   A   arvet: utan något satt är läget Auto; appens gäller utan fönster- och
//       widgetval, fönstrets går före appens, en förfader före fönstret, en
//       widgets egna före allt, och `clear…` ärver igen; en popup (ett annat
//       fönster) ärver appens och inte det första fönstrets; ett byte ritar
//       om widgeten (appens: fönstret) och samma värde igen gör det inte;
//       Style bär en tid, och samma, i alla tre UI:n.
//   B   Visible ritar remsan (pixlarna i remsan skiljer sig från en tom yta)
//       och tar ett rännklick; Invisible ritar den aldrig, tar inget tryck
//       (rännklicket rullar inte) men hjulet rullar fortfarande.
//   C   Auto: listorna är dolda i vila; de dyker upp av en rullning (hjul,
//       setScroll), av pekarens rörelse över ytan — också när det är ett BARN
//       som ligger under pekaren —, av att överflödet uppstår och av att en
//       dold panel visas igen; de står kvar `scrollbarAutoHideMs`, en ny
//       aktivitet skjuter fram tiden, och efter tiden släcks de utan att
//       pekaren rört sig (panelen ritas om, pixlarna är tomma, timern
//       stoppad). En dold lista tar inget rännklick; en synlig tar det.
//   D   hållet: pekaren stilla på en remsa och en dragen tumme släcks inte av
//       tiden, men gör det en period efter att hållet släppts.
//   E   timern: sätts till den NÄRMASTE tidpunkten av flera paneler, sätts om
//       till nästa vid varje varv och är stoppad (0) när ingen lista väntar —
//       ingen ritloop.
//   F   dold-på-tid ändrar inte vad rullningen gör: `setScroll` och hjulet
//       läser samma värde med och utan synliga listor.
//   G   en RAD (`Bar` — verktygsfältet, statusraden, menyraden) har inga
//       lister: med knappar som svämmar över visar den ingen i Auto eller
//       Visible, sätter ingen timer, och ett tryck vid kanten går till knappen.
//
// VAD DEN INTE MÄTER: att OS-timern faktiskt väcker pumpen vid tidpunkten (en
// riktig timer matas aldrig här; grinden matar eventet och läser den satta
// tidpunkten, `AppWindow::scrollbarTimerAt`), och previewns utförande
// (preview_gate).
//
// SABOTAGEN som ska fälla den: `Widget::scrollbarVisibility` som hoppar över
// fönstrets rot eller appen (A), App-nivån utan ritning (A), `Style::
// scrollbarAutoHideMs` 0 eller olika i en UI (A), `drawAfterChildren` som ritar
// också i Invisible (B), `inScrollStrip` utan `barsShown` (B, C), `mouseWheel`
// som struntar i Invisible (B), `touchBars` borttagen ur `setScroll` eller
// `mouseWheel` (C), `AppWindow::scrollbarPointer` borttagen ur MouseMotion
// (C), förfädervandringen som bara anropar den träffade widgeten (C),
// `stampBars` vid att överflödet uppstår borttagen ur `drawAfterChildren`
// (C), `shownChanged` utan `touchBars` (C), `scrollbarFade` som inte ritar om
// panelen (C), `scrollbarFade` utan tidsjämförelsen (C), `holdsBars` borttagen
// ur `barsShown` eller `scrollbarFade` (D), `scrollbarTimer` som inte sätter om
// timern eller inte stoppar den (E), `scrollbarArm` som alltid skriver över
// den satta tidpunkten (E), `Bar::hasScrollbars` borttagen eller frågan
// borttagen ur `barsShown` (G).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "scrollbar_fade.h"   // intern — timer-id:t

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string tal(double v)
{
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

using glaze::ScrollbarVisibility;
constexpr ScrollbarVisibility Auto      = ScrollbarVisibility::Auto;
constexpr ScrollbarVisibility Visible   = ScrollbarVisibility::Visible;
constexpr ScrollbarVisibility Invisible = ScrollbarVisibility::Invisible;

// KLOCKAN grinden styr: ramverkets lista-tid läser den i stället för
// steady_clock, så varje tidpunkt är ett tal grinden bestämmer.
std::int64_t g_nu = 10'000'000;
std::int64_t klocka() { return g_nu; }

// En panel som visar vad som annars är privat: om listorna syns just nu, om en
// punkt ligger i en synlig remsa och vilken damage-stämpel panelen har.
struct Sond : glaze::Panel {
    using glaze::Widget::damageStamp;
    bool syns() const { return barsShown(); }
    bool remsa(float mx, float my) { return inScrollStrip(behaviorCtx(), mx, my); }
};

// En RAD (`Bar`) med samma insyn, och dess spelrum: att raden faktiskt svämmar
// över är förutsättningen för fall G.
struct RadSond : glaze::Bar {
    bool       syns() const { return barsShown(); }
    bool       remsa(float mx, float my) { return inScrollStrip(behaviorCtx(), mx, my); }
    gfx::Sizef spelrum() { return scrollRange(behaviorCtx()); }
};

struct Värd : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Sond           sond, sond2, vaxer;
    glaze::Button  knapp;
    glaze::Label   fyll, fyll2, kort;
    glaze::Panel   grupp, barn;
    RadSond        rad;
    glaze::Button  r1, r2, r3;
    int            radKlick = 0;

    Värd() : glaze::AppWindow("scrollbar_gate", 600, 420, gfx::Chrome::None)
    {
        const auto ställ = [](glaze::Widget& w, const char* n, float x, float y,
                              float bw, float h) {
            w.name   = n;
            w.x      = x;
            w.y      = y;
            w.width  = bw;
            w.height = h;
        };
        ställ(sond, "sond", 10, 10, 220, 120);
        sond.setBorder(true);
        root().adopt(sond);
        ställ(knapp, "knapp", 10, 10, 100, 24);
        knapp.setText("Knapp");
        sond.adopt(knapp);
        fyll.name = "fyll";   // långt ner: panelen har något att rulla
        fyll.x    = 10;
        fyll.y    = 400;
        fyll.setText("fyll");
        sond.adopt(fyll);

        ställ(sond2, "sond2", 250, 10, 220, 120);
        sond2.setBorder(true);
        root().adopt(sond2);
        fyll2.name = "fyll2";
        fyll2.x    = 10;
        fyll2.y    = 400;
        fyll2.setText("fyll");
        sond2.adopt(fyll2);

        ställ(vaxer, "vaxer", 10, 150, 220, 120);   // utan överflöd till en början
        vaxer.setBorder(true);
        root().adopt(vaxer);
        kort.name = "kort";
        kort.x    = 10;
        kort.y    = 10;
        kort.setText("kort");
        vaxer.adopt(kort);

        ställ(grupp, "grupp", 250, 150, 220, 120);
        root().adopt(grupp);
        ställ(barn, "barn", 10, 10, 100, 60);
        grupp.adopt(barn);

        ställ(rad, "rad", 10, 290, 100, 32);   // tre långa knappar i 100 px
        root().adopt(rad);
        for (glaze::Button* b : {&r1, &r2, &r3}) {
            b->setText("En lång knapptext");
            rad.adopt(*b);
        }
        r1.name    = "r1";
        r2.name    = "r2";
        r3.name    = "r3";
        r1.onClick = [this] { ++radKlick; };
    }

    // ---- ingångarna
    gfx::Pointf fys(glaze::Widget& w, double lx, double ly)
    {
        const gfx::Rectf b = w.resolve(w.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{static_cast<float>(b.x + lx * s),
                           static_cast<float>(b.y + ly * s)};
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
    void rör(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
    void hjul(gfx::Pointf p, float wy)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelY   = wy;
        handleEvent(e);
    }
    void tick()
    {
        gfx::Event e;
        e.type     = gfx::EventType::Timer;
        e.windowId = window().id();
        e.timerId  = glaze::kScrollbarTimerId;
        handleEvent(e);
    }
    void rita()
    {
        invalidate();
        window().renderFrame();
    }

    // Pekaren utanför allt: ingen remsa hålls och ingen panel rörs.
    gfx::Pointf fri() { return gfx::Pointf{560.0f, 400.0f}; }
    // En punkt i panelens lodräta remsa (x) på djupet `dy` logiska px.
    gfx::Pointf iRemsan(Sond& p, double dy)
    {
        return fys(p, 220.0 - 1.0 - style().scrollThick * 0.5, dy);
    }

    // Remsans pixlar (RGBA som tal) efter en ny ritning: området inuti
    // panelens ram där den lodräta listan ligger, utan de översta och nedersta
    // raderna (hörnradien) — tomt är EN färg, en ritad tumme är flera.
    std::vector<std::uint32_t> pixlar(Sond& p)
    {
        rita();
        std::vector<std::uint8_t> px;
        int                       w = 0, h = 0;
        std::vector<std::uint32_t> ut;
        if (!window().captureFrame(px, w, h))
            return ut;
        const gfx::Rectf b  = p.resolve(p.behaviorCtx());
        const float      s  = window().uiScale();
        const double     t  = style().scrollThick;
        const int        x0 = static_cast<int>(b.x + (220.0 - 1.0 - t + 1.0) * s);
        const int        x1 = static_cast<int>(b.x + (220.0 - 2.0) * s);
        const int        y0 = static_cast<int>(b.y + 8.0 * s);
        const int        y1 = static_cast<int>(b.y + 100.0 * s);
        for (int y = y0; y < y1 && y < h; ++y)
            for (int x = x0; x < x1 && x < w; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                ut.push_back(static_cast<std::uint32_t>(px[i]) |
                             static_cast<std::uint32_t>(px[i + 1]) << 8 |
                             static_cast<std::uint32_t>(px[i + 2]) << 16 |
                             static_cast<std::uint32_t>(px[i + 3]) << 24);
            }
        return ut;
    }
    static bool enfärgad(const std::vector<std::uint32_t>& v)
    {
        for (const std::uint32_t p : v)
            if (p != v.front())
                return false;
        return !v.empty();
    }

    // Alla lägen ur vägen, alla tider utlöpta: ingenting syns och ingen timer
    // väntar. Varje fall börjar här.
    void nollställ()
    {
        glaze::app().setScrollbarVisibility(Auto);
        clearScrollbarVisibility();
        for (glaze::Widget* w : std::initializer_list<glaze::Widget*>{
                 &sond, &sond2, &vaxer, &grupp, &barn, &knapp, &rad})
            w->clearScrollbarVisibility();
        kort.y = 10;
        sond.setScroll(0.0f, 0.0f);
        sond2.setScroll(0.0f, 0.0f);
        rör(fri());
        rita();
        g_nu += 100'000;
        tick();
        rita();
    }

    void kör();
};

void Värd::kör()
{
    glaze::detail::setScrollbarClock(&klocka);
    const std::int64_t tid = style().scrollbarAutoHideMs;

    // ---- A. ARVET
    nollställ();
    check(sond.scrollbarVisibility() == Auto && barn.scrollbarVisibility() == Auto,
          "A utan något satt är läget Auto");
    glaze::app().setScrollbarVisibility(Invisible);
    check(sond.scrollbarVisibility() == Invisible &&
              barn.scrollbarVisibility() == Invisible &&
              glaze::app().scrollbarVisibility() == Invisible,
          "A appens läge gäller en widget utan eget val");
    setScrollbarVisibility(Visible);
    check(barn.scrollbarVisibility() == Visible && sond.scrollbarVisibility() == Visible,
          "A fönstrets läge går före appens");
    grupp.setScrollbarVisibility(Auto);
    check(barn.scrollbarVisibility() == Auto && sond.scrollbarVisibility() == Visible,
          "A en förfader går före fönstret — och bara över sina barn");
    barn.setScrollbarVisibility(Invisible);
    check(barn.scrollbarVisibility() == Invisible && grupp.scrollbarVisibility() == Auto,
          "A widgetens egna går före allt");
    barn.clearScrollbarVisibility();
    check(barn.scrollbarVisibility() == Auto,
          "A clear ärver igen: förfaderns");
    grupp.clearScrollbarVisibility();
    check(barn.scrollbarVisibility() == Visible,
          "A … sedan fönstrets");
    clearScrollbarVisibility();
    check(barn.scrollbarVisibility() == Invisible,
          "A … sedan appens");
    glaze::app().setScrollbarVisibility(Auto);
    check(barn.scrollbarVisibility() == Auto, "A … och appen tillbaka på Auto");
    {
        glaze::AppWindow annat("popup", 200, 120, gfx::Chrome::None);
        annat.setPopup(true);
        glaze::Panel popupPanel;
        popupPanel.name = "popupPanel";
        annat.root().adopt(popupPanel);
        setScrollbarVisibility(Invisible);
        glaze::app().setScrollbarVisibility(Visible);
        check(popupPanel.scrollbarVisibility() == Visible &&
                  barn.scrollbarVisibility() == Invisible,
              "A en popup (ett annat fönster) ärver appens, inte det första "
              "fönstrets");
        annat.setScrollbarVisibility(Auto);
        check(popupPanel.scrollbarVisibility() == Auto, "A … och sitt eget fönsters");
    }
    nollställ();

    // Ritas om: widgetens egen stämpel stegar vid ett byte och står still vid
    // samma värde; appens byte stegar fönstrets (alla widgetars) stämpel.
    {
        rita();
        const std::uint64_t s0 = sond.damageStamp();
        sond.setScrollbarVisibility(Invisible);
        const std::uint64_t s1 = sond.damageStamp();
        sond.setScrollbarVisibility(Invisible);
        const std::uint64_t s2 = sond.damageStamp();
        sond.clearScrollbarVisibility();
        const std::uint64_t s3 = sond.damageStamp();
        sond.clearScrollbarVisibility();
        const std::uint64_t s4 = sond.damageStamp();
        check(s1 != s0 && s2 == s1 && s3 != s2 && s4 == s3,
              "A ett byte ritar om widgeten, samma värde igen gör det inte");
        const std::uint64_t a0 = sond.damageStamp();
        glaze::app().setScrollbarVisibility(Visible);
        const std::uint64_t a1 = sond.damageStamp();
        glaze::app().setScrollbarVisibility(Visible);
        const std::uint64_t a2 = sond.damageStamp();
        check(a1 != a0 && a2 == a1,
              "A appens byte ritar om fönstret, samma värde igen gör det inte");
        glaze::app().setScrollbarVisibility(Auto);
    }
    // Tiden är STILENS: varje UI bär en tid, och Glaze och Minimalistic ärver
    // Slates. Grinden läser värdet och upprepar det inte.
    const int slateTid = glaze::defaultStyle(glaze::UI::Slate).scrollbarAutoHideMs;
    for (const glaze::UI ui : {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic})
        check(slateTid > 0 && glaze::defaultStyle(ui).scrollbarAutoHideMs == slateTid,
              "A Style::scrollbarAutoHideMs är satt och lika i varje UI",
              tal(glaze::defaultStyle(ui).scrollbarAutoHideMs));

    // ---- B. VISIBLE OCH INVISIBLE
    nollställ();
    sond.setScrollbarVisibility(Visible);
    check(sond.syns(), "B Visible: listorna syns utan någon aktivitet");
    const std::vector<std::uint32_t> synlig = pixlar(sond);
    check(!synlig.empty() && !enfärgad(synlig),
          "B Visible ritar remsan (tumme mot ränna i remsans pixlar)",
          tal(synlig.size()));
    check(sond.remsa(iRemsan(sond, 60).x, iRemsan(sond, 60).y),
          "B Visible: en punkt i remsan är en remsa");
    sond.setScrollbarVisibility(Invisible);
    const std::vector<std::uint32_t> osynlig = pixlar(sond);
    check(!sond.syns() && !osynlig.empty() && enfärgad(osynlig),
          "B Invisible ritar ingen remsa (remsans pixlar är en enda färg)");
    check(!sond.remsa(iRemsan(sond, 60).x, iRemsan(sond, 60).y),
          "B Invisible: remsan tar inga tryck");

    // Rännklick: Visible rullar en sida, Invisible låter trycket gå till
    // innehållet (inget scroll).
    sond.setScrollbarVisibility(Visible);
    sond.setScroll(0.0f, 0.0f);
    rita();
    ner(iRemsan(sond, 110));
    upp(iRemsan(sond, 110));
    const float visibleRulle = sond.scrollY();
    sond.setScrollbarVisibility(Invisible);
    sond.setScroll(0.0f, 0.0f);
    rita();
    ner(iRemsan(sond, 110));
    upp(iRemsan(sond, 110));
    check(visibleRulle > 0.0f && sond.scrollY() == 0.0f,
          "B rännklicket rullar med Visible men inte med Invisible",
          tal(visibleRulle) + " / " + tal(sond.scrollY()));

    // Rullningen fungerar alltid: hjulet rullar i Invisible, listan förblir dold.
    sond.setScroll(0.0f, 0.0f);
    hjul(fys(sond, 150, 90), -1.0f);
    check(sond.scrollY() > 0.0f && !sond.syns(),
          "B Invisible: hjulet rullar ändå, och listan förblir dold", tal(sond.scrollY()));
    sond.setScroll(0.0f, 33.0f);
    check(sond.scrollY() == 33.0f && !sond.syns(),
          "F Invisible: setScroll läses tillbaka orört", tal(sond.scrollY()));

    // ---- C. AUTO
    nollställ();
    check(!sond.syns() && scrollbarTimerAt() == 0,
          "C Auto: listorna är dolda i vila och ingen timer väntar");
    {
        const std::vector<std::uint32_t> vila = pixlar(sond);
        check(enfärgad(vila), "C Auto: en dold lista ritas inte", tal(vila.size()));
    }

    // Rullning (hjul).
    hjul(fys(sond, 150, 90), -1.0f);
    check(sond.syns() && sond.scrollY() > 0.0f,
          "C en rullning med hjulet visar listorna");
    {
        const std::vector<std::uint32_t> syns = pixlar(sond);
        check(!enfärgad(syns), "C … och de ritas");
    }
    check(scrollbarTimerAt() == g_nu + tid,
          "C … och timern är satt tiden fram",
          tal(static_cast<double>(scrollbarTimerAt() - g_nu)));

    // Tiden: kvar strax före, borta vid tiden — utan att pekaren rört sig.
    g_nu += tid - 1;
    tick();
    check(sond.syns() && scrollbarTimerAt() == g_nu + 1,
          "C strax före tiden står listorna kvar och timern sätts om",
          tal(static_cast<double>(scrollbarTimerAt() - g_nu)));
    const std::uint64_t före = sond.damageStamp();
    g_nu += 1;
    tick();
    check(!sond.syns() && sond.damageStamp() != före,
          "C vid tiden släcks listorna och panelen ritas om");
    check(scrollbarTimerAt() == 0, "C … och timern är stoppad (ingen ritloop)",
          tal(static_cast<double>(scrollbarTimerAt())));
    {
        const std::vector<std::uint32_t> borta = pixlar(sond);
        check(enfärgad(borta), "C … och remsans pixlar är tomma igen");
    }
    check(sond.scrollY() > 0.0f, "F … men rullningen är orörd", tal(sond.scrollY()));

    // En dold lista tar inget rännklick.
    {
        sond.setScroll(0.0f, 0.0f);
        g_nu += tid;
        tick();   // setScroll ovan räknades som aktivitet
        const float y0 = sond.scrollY();
        ner(iRemsan(sond, 110));
        upp(iRemsan(sond, 110));
        check(!sond.syns() && sond.scrollY() == y0,
              "C en dold lista tar inget rännklick", tal(sond.scrollY()));
        // Pekarens rörelse ger listan tillbaka — och då tar den trycket.
        rör(iRemsan(sond, 110));
        ner(iRemsan(sond, 110));
        upp(iRemsan(sond, 110));
        check(sond.scrollY() > y0,
              "C … men efter pekarens rörelse över remsan tar den det",
              tal(sond.scrollY()));
    }

    // Pekarens rörelse över ytan — här över ett BARN (knappen).
    nollställ();
    rör(fys(knapp, 5, 5));
    check(sond.syns() && !sond2.syns(),
          "C pekarens rörelse över ett barn visar förfaderns listor (och bara "
          "dess)");
    // … och skjuter fram tiden.
    g_nu += 3000;
    rör(fys(knapp, 8, 8));
    g_nu += tid - 1000;   // 4000 efter första, 1000 efter andra rörelsen
    tick();
    check(sond.syns(), "C en ny rörelse skjuter fram tiden");
    g_nu += 1000;
    tick();
    check(!sond.syns(), "C … och tiden räknas från den senaste rörelsen");

    // Rörelse över en yta utan överflöd visar inget.
    nollställ();
    rör(fys(kort, 5, 5));
    check(scrollbarTimerAt() == 0,
          "C pekarens rörelse över en yta utan överflöd väcker ingen timer",
          tal(static_cast<double>(scrollbarTimerAt())));

    // Överflödet uppstår.
    nollställ();
    rita();
    check(scrollbarTimerAt() == 0, "C en panel utan överflöd väntar ingen tid");
    kort.y = 400;
    rita();
    check(vaxer.syns() && scrollbarTimerAt() == g_nu + tid,
          "C när överflödet uppstår visas listorna", tal(static_cast<double>(scrollbarTimerAt() - g_nu)));
    kort.y = 10;

    // En dold panel som visas igen med överflöd.
    nollställ();
    sond.hide();
    rita();
    g_nu += 100'000;
    tick();
    check(!sond.syns(), "C en dold panel visar inga listor");
    sond.show();
    check(sond.syns(), "C en panel som visas igen med överflöd visar listorna");
    nollställ();

    // setScroll är aktivitet.
    sond.setScroll(0.0f, 50.0f);
    check(sond.syns(), "C setScroll visar listorna");

    // ---- D. HÅLLET
    nollställ();
    rör(iRemsan(sond, 60));
    check(sond.syns(), "D pekaren på remsan visar listan");
    g_nu += 20 * tid;
    tick();
    check(sond.syns(), "D pekaren stilla på remsan: tiden släcker den inte");
    rör(fri());
    g_nu += tid - 1;
    tick();
    check(sond.syns(), "D … hållet släppt: tiden räknas från det senaste varvet");
    g_nu += 1;
    tick();
    check(!sond.syns(), "D … och listan släcks en period senare");

    nollställ();
    sond.setScroll(0.0f, 0.0f);
    rita();
    rör(iRemsan(sond, 8));      // tummen ligger överst
    ner(iRemsan(sond, 8));      // greppar den
    g_nu += 20 * tid;
    tick();
    check(sond.syns(), "D en dragen tumme släcks inte av tiden");
    upp(iRemsan(sond, 8));
    rör(fri());
    g_nu += tid;
    tick();
    g_nu += tid;
    tick();
    check(!sond.syns(), "D … men släcks en period efter att tummen släppts");

    // ---- E. TIMERN
    nollställ();
    const std::int64_t t0     = g_nu;
    const std::int64_t senare = tid / 2;   // den andra panelen rullar inom den förstas tid
    sond.setScroll(0.0f, 10.0f);
    g_nu += senare;
    sond2.setScroll(0.0f, 10.0f);
    check(scrollbarTimerAt() == t0 + tid,
          "E timern står vid den NÄRMASTE tidpunkten, inte den senaste",
          tal(static_cast<double>(scrollbarTimerAt() - t0)));
    g_nu = t0 + tid;
    tick();
    check(!sond.syns() && sond2.syns() && scrollbarTimerAt() == t0 + senare + tid,
          "E vid första tidpunkten släcks bara den första, och timern sätts om "
          "till nästa",
          tal(static_cast<double>(scrollbarTimerAt() - t0)));
    g_nu = t0 + senare + tid;
    tick();
    check(!sond2.syns() && scrollbarTimerAt() == 0,
          "E sist släcks den andra och timern stoppas");
    nollställ();

    // ---- G. EN RAD HAR INGA LISTER
    rita();
    check(rad.spelrum().w > 0.0f,
          "G förutsättning: radens knappar svämmar över i sidled",
          tal(rad.spelrum().w));
    const gfx::Rectf  radYta = rad.resolve(rad.behaviorCtx());
    const float       skala  = window().uiScale();
    const gfx::Pointf kant   = fys(rad, 4.0, radYta.h / skala - 2.0);   // nederkanten
    rör(kant);
    check(!rad.syns() && scrollbarTimerAt() == 0,
          "G Auto: pekaren över raden visar inga lister och sätter ingen timer");
    rad.setScrollbarVisibility(Visible);
    check(!rad.syns() && !rad.remsa(kant.x, kant.y),
          "G Visible: raden har ändå inga lister och ingen remsa");
    radKlick = 0;
    ner(kant);
    upp(kant);
    check(radKlick == 1, "G ett tryck vid radens kant går till knappen",
          tal(radKlick));
    nollställ();

    glaze::detail::setScrollbarClock(nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== scrollbar_gate — rullningslistornas synlighet ===\n");
    bool kördes = false;
    {
        Värd w;
        w.setOffscreen(true);
        glaze::app().post([&] {
            kördes = true;
            w.kör();
            w.close();
        });
        w.run();
    }
    if (!kördes) {
        std::printf("  --   inget fönster kunde skapas (ingen display)\n");
        return 77;
    }
    std::printf(g_fel ? "=== scrollbar_gate: %d FEL av %d ===\n"
                      : "=== scrollbar_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
