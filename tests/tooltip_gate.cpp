// tooltip_gate — TOOLTIPENS grind.
//
// Kör:  ./build/tests/tooltip_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// TVÅDELAD, som dnd_gate:
//
//   HEADLESS     basens API (`setToolTip`/`toolTip`/`toolTipToken`), textformen
//                via `setProp`, och att VARJE REGISTRERAD typs `setTextToken`
//                når basen — annars hade ett översättnings-id i en tooltip
//                blivit opakt just på de widgets som har en egen text.
//   MED FÖNSTER  ett DOLT fönster matat genom den RIKTIGA ingången
//                (`AppWindow::handleEvent`, protected), och
//                popupen som RIKTIGT fönster. Fördröjningens utlösning matas
//                som ett syntetiskt `Timer`-event med ramverkets id (den
//                interna `tooltip.h`) för LOGIKEN — och TRE fall väntar på den
//                RIKTIGA timern, så att vägen OS-timer → event → tooltip också
//                är mätt, liksom att en VARM väntan är kortare än en kall.
//                TOOLTIP PER DEL (fall 20–22): dockningsytans
//                handtag är DELAR av en widget (`Widget::toolTipAt`), och
//                källan är widget OCH text — två handtag är två källor. Fall
//                23: en flik som byts medan
//                väntan pågår — timerns leverans gör ett nytt träffprov och
//                källuppslag och visar inget ur den inaktiva fliken. Fall
//                24: panelrubrikens KNAPPAR är delar av panelen —
//                knappnålen på en fäst panel säger "Dölj automatiskt"
//                (`str::DockAutoHide`), stängknappen "Stäng".
//
// Kedjan körs som `App::post`-steg (dnd_gate-mönstret): stängningen är
// UPPSKJUTEN till pump-svansen, så ett steg som gömmer tooltipen ser
// popupen stängd först i nästa steg.
//
// YTTRE HÄNDELSER: fönsterdelen kör mot en RIKTIG kompositör, och mellan
// två steg dispatchar pumpen det OS:et skickat. En verklig fokusförlust
// eller `MouseLeave` där gömmer en väntande tooltip — enligt kontraktet —
// och kan fälla fall 6 på WSLg (Wayland), direkt efter att ett
// X11-fönster dödats. De steg som väntar ÖVER en pumpgräns (3 och 6) rör
// därför pekaren inom källan igen före vilan: samma källa medan visningen
// väntar är bara en omstart av tiden, så logiken som mäts är densamma.
// Fall 2 (popupen överlever ett varv) går inte att skydda så och kan i
// princip falla av samma orsak.
//
// SABOTAGEN den fäller (prövade i en privat kopia):
//   * rörelse INOM källan som startar om/gömmer en visad tooltip (fall 2)
//   * ingen tystnad efter ett klick (fall 6)
//   * ingen routad uppslagning till förfadern (fall 5)
//   * en tooltip som visas MEDAN en annan popup lever (fall 14 — den hade
//     stängt den öppna listan)
//   * värmen glömd: nästa tooltip väntar hela fördröjningen (fall 19)
//   * en textwidgets `setTextToken` som inte kedjar till basen (fall H2)
//   * `Widget::toolTipAt` ignoreras — dockningsytans tomma egna text ger
//     ingen tooltip över ett handtag (fall 20)
//   * källan jämförs utan TEXTEN — pekaren från ett handtag till nästa i
//     samma widget lämnar den första titeln stående (fall 21)
//   * timerns leverans utan nytt träffprov och utan förfädernas
//     `showsChildren` — knappen i den inaktiva fliken visar sin tooltip
//     (fall 23)
//   * `DockPanel::toolTipAt` borttagen, eller knappnålens text omvänd (fall
//     24: panelen har ingen egen tooltip, respektive "Fäst" på en fäst panel)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * VAR popupen hamnar på skärmen — fasaden har ingen läsare för ett
//     fönsters skärmposition (och Wayland ingen skärmrymd). Ankaret följer
//     popup-kontraktet. På Windows är placeringen mätt med ett
//     skratchprogram (Win32 `GetWindowRect` mot ägarens klientorigo:
//     exakt `toolTipOffsetY` under pekaren, och vid skärmens nederkant
//     vänd med `toolTipOffsetUp` luft ovanför spetsen); macOS står som rad i
//     doc/os/macos/TESTS.md.
//   * hur plattan SER UT — verifierad renderad i alla tre UI × ljust/mörkt.
//   * ett tryck MITT I tooltipen (pekaren uppflyttad på den): den vägen går
//     genom tooltipfönstrets egen ingång, som grinden inte når.
//   * timerleveransens två skydd VAR FÖR SIG (fall 23): det nya träffprovet
//     och förfädernas `showsChildren` räcker vart och ett för den inaktiva
//     fliken, så bara ett sabotage som tar bort BÅDA fälls här.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "tooltip.h"   // kToolTipTimerId, ToolTipWindow, ToolTipBody (interna)

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
    std::fflush(stdout);
    ++g_fails;
}

// ================================================================ HEADLESS

void fallBasen()
{
    // ---- H1. API:t på basen.
    glaze::Panel p;
    check(p.toolTip().empty() && p.toolTipToken().id() == 0,
          "H1: ingen tooltip som default");
    p.setToolTip("Hej");
    check(p.toolTip() == "Hej", "H1: setToolTip/toolTip", p.toolTip());
    check(p.setProp("toolTip", "Via setProp") && p.toolTip() == "Via setProp",
          "H1: textformen via basens setProp", p.toolTip());
    glaze::setLanguage("sv");
    p.setToolTip(glaze::tr(2));
    check(p.toolTipToken().id() == 2 && p.toolTip() == "Avbryt",
          "H1: tooltipen är ett TOKEN och följer språket", p.toolTip());
    glaze::setLanguage("fi");
    check(p.toolTip() == "Peruuta", "H1: …också efter ett språkbyte",
          p.toolTip());
    glaze::setLanguage("sv");

    // ---- H2. VARJE registrerad typ: setTextToken når basens toolTip.
    // REGISTRET och inte en handskriven lista — en widget med egen text som
    // läggs till senare och glömmer kedja till basen fälls här utan att
    // någon behöver komma ihåg grinden.
    for (const std::string& typ : glaze::registeredWidgetTypes()) {
        std::unique_ptr<glaze::Widget> w = glaze::makeWidget(typ);
        check(w && w->setTextToken("toolTip", glaze::tr(3)) &&
                  w->toolTipToken().id() == 3,
              "H2: " + typ + ": setTextToken(\"toolTip\", tr) når basen");
    }
    glaze::Button b;
    b.setTextToken("toolTip", glaze::tr(3));
    check(b.setTextToken("text", glaze::tr(4)) && b.textToken().id() == 4 &&
              b.toolTipToken().id() == 3,
          "H2: …och den egna texten går fortfarande sin väg");
    check(!p.setTextToken("finnsInte", glaze::tr(1)),
          "H2: en okänd nyckel är fortfarande false (opak rad)");
}

// ============================================================= MED FÖNSTER

constexpr std::uint32_t kVantTimer = 7;   // grindens EGEN timer (låga id:n)

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Button   knappA, knappB, knappAv, sprak;
    glaze::Label    etikett, lang;
    // Q1: en KORTAD etikett och en som ryms — samma text.
    glaze::Label    kortad, ryms;
    glaze::Panel    panel;
    glaze::Button   knappC;   // i panelen, UTAN egen tooltip
    glaze::Dropdown lista;
    // TOOLTIP PER DEL: två infällda paneler är två HANDTAG i
    // samma värd — värden är källan, texten är delens (panelens titel).
    glaze::DockHost  dockVärd;
    glaze::DockPanel dockEtt{"Panel ett"}, dockTvå{"Panel två"};
    // EN FLIKGRUPP: två paneler i samma
    // cell, och en knapp med egen tooltip i den första. När fliken byts
    // medan väntan pågår är knappen synlig men ligger i en inaktiv flik.
    glaze::DockHost  flikVärd;
    glaze::DockPanel flikA{"Flik A"}, flikB{"Flik B"};
    glaze::Button    flikKnapp;
    std::function<void()> efterVantan;   // kör när grindens timer löser ut

    Vard() : glaze::AppWindow("tooltip_gate", 460, 320, gfx::Chrome::None)
    {
        auto lägg = [](glaze::Widget& w, const char* n, float x, float y) {
            w.name = n;
            w.x    = x;
            w.y    = y;
        };
        knappA.setText("A");
        knappA.setToolTip("Tips A");
        lägg(knappA, "knappA", 10, 10);
        knappB.setText("B");
        knappB.setToolTip("Tips B");
        lägg(knappB, "knappB", 10, 60);
        etikett.setText("ingen tooltip");
        lägg(etikett, "etikett", 10, 110);
        knappAv.setText("Av");
        knappAv.setToolTip("Kräver en öppen fil");
        knappAv.disable();
        lägg(knappAv, "knappAv", 10, 160);
        sprak.setText("Språk");
        sprak.setToolTip(glaze::tr(2));
        lägg(sprak, "sprak", 10, 210);
        lang.setText("lång");
        lang.setToolTip("En lång förklaring som inte ryms på en enda rad i en "
                        "tooltip och därför måste brytas på ordgräns — flera "
                        "gånger om, faktiskt, för att det ska synas tydligt.");
        lägg(lang, "lang", 10, 260);

        lägg(panel, "panel", 200, 10);
        panel.width  = 150;
        panel.height = 100;
        panel.setBorder(true);
        panel.setToolTip("Panelens tips");
        knappC.setText("C");
        lägg(knappC, "knappC", 10, 10);

        lista.setItems({"Ett", "Två", "Tre"});
        lägg(lista, "lista", 200, 160);

        kortad.setText("Lång text");
        kortad.setElide(true);
        lägg(kortad, "kortad", 110, 60);
        kortad.width = 30;    // smalare än texten — kortas med "…"
        ryms.setText("Lång text");
        ryms.setElide(true);
        lägg(ryms, "ryms", 110, 260);
        ryms.width = 85;      // bredare än texten — kortas inte
        root().adopt(kortad);
        root().adopt(ryms);

        root().adopt(knappA);
        root().adopt(knappB);
        root().adopt(etikett);
        root().adopt(knappAv);
        root().adopt(sprak);
        root().adopt(lang);
        root().adopt(panel);
        panel.adopt(knappC);
        root().adopt(lista);

        lägg(dockVärd, "dockVard", 370, 10);
        dockVärd.width  = 80;
        dockVärd.height = 200;
        dockEtt.name    = "dockEtt";
        dockTvå.name    = "dockTva";
        dockEtt.setCollapsed(true);   // vänsterkantens handtag, uppifrån
        dockTvå.setCollapsed(true);
        root().adopt(dockVärd);
        dockVärd.adopt(dockEtt);
        dockVärd.adopt(dockTvå);

        lägg(flikVärd, "flikVard", 200, 205);
        flikVärd.width  = 150;
        flikVärd.height = 110;
        flikA.name      = "flikA";
        flikB.name      = "flikB";
        flikKnapp.setText("K");
        flikKnapp.setToolTip("Knapp i A");
        flikKnapp.name = "flikKnapp";
        flikKnapp.dock = glaze::Dock::Top;
        root().adopt(flikVärd);
        flikVärd.adopt(flikA);
        flikVärd.adopt(flikB);
        flikA.adopt(flikKnapp);
        flikVärd.dockPanel(flikB, flikA, glaze::Dock::Fill);   // en flikgrupp
        flikA.activate();                                      // A visas
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::Timer && e.timerId == kVantTimer) {
            window().stopTimer(kVantTimer);
            if (efterVantan) {
                auto f      = std::move(efterVantan);
                efterVantan = nullptr;
                f();
            }
            return true;
        }
        return false;
    }

    gfx::Pointf mitt(glaze::Widget& w, float dx = 0.0f)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w / 2.0f + dx, r.y + r.h / 2.0f};
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
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    // FÖRDRÖJNINGEN LÖSER UT — syntetiskt, med ramverkets egen timer-id.
    void vila()
    {
        gfx::Event e;
        e.type     = gfx::EventType::Timer;
        e.windowId = window().id();
        e.timerId  = glaze::kToolTipTimerId;
        handleEvent(e);
    }
    // Tooltipens popup, om den finns och är öppen (ägd av oss).
    glaze::ToolTipWindow* popup()
    {
        for (glaze::AppWindow* w : glaze::app().windows())
            if (auto* t = dynamic_cast<glaze::ToolTipWindow*>(w))
                if (t->isOpen() && t->owner() == this)
                    return t;
        return nullptr;
    }
    // Mitten av handtag nummer `i` vid värdens vänsterkant (stående handtag
    // är kvadrater med `dockHandleH` som sida, `dockHandleGap` isär).
    gfx::Pointf handtag(int i, float dy = 0.0f)
    {
        const gfx::Rectf r  = dockVärd.resolve(dockVärd.behaviorCtx());
        const float      s  = window().uiScale();
        const float      HH = style().dockHandleH;
        return gfx::Pointf{r.x + HH * s / 2.0f,
                           r.y + (static_cast<float>(i) *
                                      (HH + style().dockHandleGap) +
                                  HH / 2.0f) * s + dy};
    }
    std::string visad()
    {
        glaze::Widget* w = toolTipWidget();
        return w ? w->name : std::string("(ingen)");
    }
};

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);

    bool kordes = false;
    // STEGEN körs ett per pumpvarv: en uppskjuten stängning verkställs i
    // svansen efter stegets `drainPosted`, så nästa steg ser den.
    std::vector<std::function<void()>> steg;
    std::size_t                        i = 0;
    std::function<void()>              nästa;
    nästa = [&] {
        if (i < steg.size())
            glaze::app().post([&] { steg[i++](); });
    };

    // ---- 1. VILA visar tooltipen — men inte förrän fördröjningen löst ut.
    steg.push_back([&] {
        kordes = true;
        w.flytta(w.mitt(w.knappA));
        check(w.toolTipWidget() == nullptr && !w.popup(),
              "1: rörelsen ensam visar ingenting", w.visad());
        w.vila();
        check(w.toolTipWidget() == &w.knappA, "1: vila visar A:s tooltip",
              w.visad());
        glaze::ToolTipWindow* p = w.popup();
        check(p && p->body().text() == "Tips A",
              "1: popupen är uppe och bär texten",
              p ? p->body().text() : "(ingen popup)");
        if (p) {
            const glaze::Ctx  c = w.knappA.behaviorCtx();
            const gfx::Sizef  m = glaze::ToolTipBody::measure(c, "Tips A");
            check(p->clientSize().w == std::ceil(m.w) &&
                      p->clientSize().h == std::ceil(m.h),
                  "1: fönstret har plattans mätta storlek (uppåt avrundad)",
                  std::to_string(p->clientSize().w) + "x" +
                      std::to_string(p->clientSize().h));
        }
        nästa();
    });
    // ---- 2. rörelse INOM källan låter den stå.
    steg.push_back([&] {
        glaze::ToolTipWindow* före = w.popup();
        w.flytta(w.mitt(w.knappA, 3.0f));
        w.vila();   // en tidtagare som (felaktigt) startats om ska inte synas
        check(w.toolTipWidget() == &w.knappA && w.popup() == före && före,
              "2: rörelse inom A — tooltipen står kvar, samma popup",
              w.visad());
        nästa();
    });
    // ---- 3. DIREKT till en annan källa: den gamla göms, den nya väntar.
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappB));
        check(w.toolTipWidget() == nullptr,
              "3: A göms DIREKT när pekaren går till B", w.visad());
        nästa();
    });
    steg.push_back([&] {
        check(!w.popup(), "3: …och A:s popup är stängd ett varv senare");
        w.flytta(w.mitt(w.knappB, 1.0f));   // inom B — se "YTTRE HÄNDELSER"
        w.vila();
        check(w.toolTipWidget() == &w.knappB && w.popup() &&
                  w.popup()->body().text() == "Tips B",
              "3: B:s tooltip visas efter vila", w.visad());
        nästa();
    });
    // ---- 4. en yta UTAN tooltip.
    steg.push_back([&] {
        w.flytta(w.mitt(w.etikett));
        w.vila();
        check(w.toolTipWidget() == nullptr,
              "4: en etikett utan tooltip visar ingenting", w.visad());
        nästa();
    });
    // ---- 5. ROUTED LOOKUP — knappen i panelen saknar egen, panelens gäller.
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappC));
        w.vila();
        check(w.toolTipWidget() == &w.panel && w.popup() &&
                  w.popup()->body().text() == "Panelens tips",
              "5: ett barn utan tooltip visar förälderns", w.visad());
        nästa();
    });
    // ---- 6. KLICK gömmer och TYSTAR tills pekaren byter källa. Rörelsen
    //         och vilan i VAR SITT steg: panelens tooltip visas, och dess
    //         popup stängs först i pump-svansen — en riktig timer kan aldrig
    //         lösa ut i samma varv som rörelsen som startade den.
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappA));
        nästa();
    });
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappA, 1.0f));   // inom A — se "YTTRE HÄNDELSER"
        w.vila();
        check(w.toolTipWidget() == &w.knappA, "6: A visas", w.visad());
        w.ner(w.mitt(w.knappA));
        w.upp(w.mitt(w.knappA));
        check(w.toolTipWidget() == nullptr, "6: klicket gömmer", w.visad());
        nästa();
    });
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappA, 2.0f));
        w.vila();
        check(w.toolTipWidget() == nullptr,
              "6: …och A förblir tyst medan pekaren står kvar", w.visad());
        w.flytta(w.mitt(w.etikett));
        w.flytta(w.mitt(w.knappA));
        w.vila();
        check(w.toolTipWidget() == &w.knappA,
              "6: ut och tillbaka släpper tystnaden", w.visad());
        nästa();
    });
    // ---- 7. TANGENT gömmer och tystar.
    steg.push_back([&] {
        w.tangent(gfx::Key::A);
        check(w.toolTipWidget() == nullptr, "7: en tangent gömmer", w.visad());
        nästa();
    });
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappA, 1.0f));
        w.vila();
        check(w.toolTipWidget() == nullptr, "7: …och tystar", w.visad());
        nästa();
    });
    // ---- 8. PEKAREN UT gömmer — men tystar inte.
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappB));
        w.vila();
        check(w.toolTipWidget() == &w.knappB, "8: B visas", w.visad());
        w.enkel(gfx::EventType::MouseLeave);
        check(w.toolTipWidget() == nullptr, "8: pekaren ut gömmer", w.visad());
        nästa();
    });
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappB));
        w.vila();
        check(w.toolTipWidget() == &w.knappB,
              "8: …men tystar inte — tillbaka och vila visar igen", w.visad());
        // ---- 9. FOKUSFÖRLUST gömmer.
        w.enkel(gfx::EventType::WindowFocusLost);
        check(w.toolTipWidget() == nullptr, "9: fokusförlust gömmer",
              w.visad());
        w.enkel(gfx::EventType::WindowFocusGained);
        nästa();
    });
    // ---- 10. en INAKTIVERAD widget visar sin tooltip.
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappAv));
        w.vila();
        check(w.toolTipWidget() == &w.knappAv,
              "10: en inaktiverad knapp förklarar sig", w.visad());
        nästa();
    });
    // ---- 11/12. TEXTEN BYTS medan den visas.
    steg.push_back([&] {
        w.knappAv.setToolTip("Ny förklaring");
        check(w.toolTipWidget() == nullptr,
              "12: ny text medan den visas ⇒ den gamla stängs", w.visad());
        nästa();
    });
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappAv, 1.0f));
        w.vila();
        check(w.toolTipWidget() == &w.knappAv && w.popup() &&
                  w.popup()->body().text() == "Ny förklaring",
              "12: …och nästa vila visar den nya", w.visad());
        w.knappAv.setToolTip("");
        check(w.toolTipWidget() == nullptr, "11: tom text gömmer", w.visad());
        nästa();
    });
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappAv));
        w.vila();
        check(w.toolTipWidget() == nullptr,
              "11: …och en tom tooltip visas aldrig", w.visad());
        w.knappAv.setToolTip("Kräver en öppen fil");
        nästa();
    });
    // ---- 13. SPRÅKET: tokenet löses vid visningen, och ett byte stänger.
    steg.push_back([&] {
        glaze::setLanguage("sv");
        w.flytta(w.mitt(w.sprak));
        w.vila();
        check(w.popup() && w.popup()->body().text() == "Avbryt",
              "13: tooltipen visar tokenets text (sv)",
              w.popup() ? w.popup()->body().text() : "(ingen)");
        glaze::setLanguage("fi");
        check(w.toolTipWidget() == nullptr && !w.popup(),
              "13: språkbytet stänger popupen (synkront)", w.visad());
        w.flytta(w.mitt(w.sprak, 2.0f));
        w.vila();
        check(w.popup() && w.popup()->body().text() == "Peruuta",
              "13: nästa vila visar den finska texten",
              w.popup() ? w.popup()->body().text() : "(ingen)");
        glaze::setLanguage("sv");
        nästa();
    });
    // ---- 14. EN ANNAN POPUP LEVER — tooltipen hade stängt den.
    steg.push_back([&] {
        w.lista.open();
        check(w.lista.isOpen(), "14: listan är öppen");
        w.flytta(w.mitt(w.knappA));
        w.vila();
        check(w.toolTipWidget() == nullptr && w.lista.isOpen(),
              "14: ingen tooltip medan listan är öppen — och listan står kvar",
              w.visad());
        w.lista.close();
        nästa();
    });
    // ---- 15. KÄLLAN LÄMNAR TRÄDET medan tooltipen visas.
    steg.push_back([&] {
        auto* lös = w.root().add<glaze::Button>("Lös");
        lös->name = "lös";
        lös->x    = 120;
        lös->y    = 10;
        lös->setToolTip("Försvinner");
        w.flytta(w.mitt(*lös));
        w.vila();
        check(w.toolTipWidget() == lös, "15: den lösa knappens tooltip visas",
              w.visad());
        check(w.root().unadopt(*lös), "15: knappen tas bort (ägd ⇒ förstörs)");
        check(w.toolTipWidget() == nullptr,
              "15: tooltipen släpptes med källan — ingen dinglande pekare",
              w.visad());
        w.flytta(w.mitt(w.etikett));
        w.vila();
        check(w.toolTipWidget() == nullptr, "15: och ingenting följer efter");
        nästa();
    });
    // ---- 16. ETT TRYCK PÅGÅR — ingen tooltip.
    steg.push_back([&] {
        w.ner(w.mitt(w.knappB));
        w.flytta(w.mitt(w.knappB, 1.0f));
        w.vila();
        check(w.toolTipWidget() == nullptr,
              "16: ingen tooltip medan knappen hålls nere", w.visad());
        w.upp(w.mitt(w.knappB, 1.0f));
        nästa();
    });
    // ---- 17. LÅNG TEXT bryts mot `toolTipMaxW`.
    steg.push_back([&] {
        w.flytta(w.mitt(w.lang));
        w.vila();
        glaze::ToolTipWindow* p = w.popup();
        check(w.toolTipWidget() == &w.lang && p, "17: den långa visas",
              w.visad());
        if (p) {
            const glaze::Ctx   c = w.lang.behaviorCtx();
            const glaze::Style& s = w.style();
            check(p->clientSize().w <= std::ceil(s.toolTipMaxW +
                                                 2.0f * s.toolTipPadX),
                  "17: bredden håller sig inom brytbredden + luften",
                  std::to_string(p->clientSize().w));
            check(p->clientSize().h >
                      2.0f * glaze::Draw::lineHeight(c) + 2.0f * s.toolTipPadY,
                  "17: texten bröts på flera rader",
                  std::to_string(p->clientSize().h));
        }
        w.flytta(w.mitt(w.etikett));
        nästa();
    });
    // ---- 20. TOOLTIP PER DEL: ett handtag visar panelens
    //          titel — källan är värden, texten delens (`toolTipAt`).
    steg.push_back([&] {
        w.flytta(w.handtag(0));
        w.vila();
        check(w.toolTipWidget() == &w.dockVärd && w.popup() &&
                  w.popup()->body().text() == "Panel ett",
              "20: handtaget visar panelens titel",
              w.popup() ? w.popup()->body().text() : w.visad());
        nästa();
    });
    // ---- 21. ETT ANNAT HANDTAG i samma widget: samma källa, en ANNAN text ⇒
    //          den visade göms direkt, och nästa vila visar den nya titeln.
    steg.push_back([&] {
        w.flytta(w.handtag(1));
        check(w.toolTipWidget() == nullptr,
              "21: till nästa handtag — den första titeln göms direkt",
              w.visad());
        nästa();
    });
    steg.push_back([&] {
        check(!w.popup(), "21: …och dess popup är stängd ett varv senare");
        w.flytta(w.handtag(1, 1.0f));   // inom handtaget — se "YTTRE HÄNDELSER"
        w.vila();
        check(w.toolTipWidget() == &w.dockVärd && w.popup() &&
                  w.popup()->body().text() == "Panel två",
              "21: nästa vila visar det andra handtagets titel",
              w.popup() ? w.popup()->body().text() : w.visad());
        nästa();
    });
    // ---- 22. DEN RIKTIGA TIMERN mellan två delar: väntan är VARM — den nya
    //          titeln kommer efter `toolTipReshowMs`, långt före den kalla
    //          fördröjningen.
    steg.push_back([&] {
        w.flytta(w.handtag(0));
        w.efterVantan = [&] {
            check(w.toolTipWidget() == &w.dockVärd && w.popup() &&
                      w.popup()->body().text() == "Panel ett",
                  "22: VARM väntan mellan två handtag — titeln kom efter den "
                  "korta tiden",
                  w.popup() ? w.popup()->body().text() : w.visad());
            w.flytta(w.mitt(w.etikett));   // kall igen inför fall 18
            nästa();
        };
        const glaze::Style& s = w.style();
        w.window().startTimer(
            kVantTimer, static_cast<std::uint32_t>(
                            (s.toolTipReshowMs + s.toolTipDelayMs) / 2));
    });
    // ---- 23. TIMERN OCH EN INAKTIV FLIK:
    //          väntan börjar över knappen i flik A, och fliken byts innan
    //          timern löser ut. Knappen och alla dess förfäder är fortfarande
    //          `isVisible()`, men flik A visar inte sina barn — leveransen gör
    //          ett nytt träffprov och ett nytt källuppslag, och visar ingenting.
    //          I ett eget steg: steg 22:s popup stängs i pump-svansen, och en
    //          ny tooltip hade väntat på den (se 23b).
    steg.push_back([&] {
        w.flytta(w.mitt(w.flikKnapp));
        check(w.toolTipWidget() == nullptr, "23: rörelsen ensam visar ingenting",
              w.visad());
        w.flikB.activate();   // fliken byts medan väntan pågår
        w.vila();
        check(w.toolTipWidget() == nullptr && !w.flikA.showsChildren(),
              "23: fliken byttes under väntan — den inaktiva flikens knapp "
              "visar ingen tooltip",
              w.visad());
        nästa();
    });
    // ---- 23b. KONTROLLEN: samma knapp i den VISADE fliken visar sin tooltip
    //           — fallet ovan är inte tomt för att knappen saknar en.
    steg.push_back([&] {
        w.flikA.activate();
        w.flytta(w.mitt(w.flikKnapp, 1.0f));
        w.vila();
        check(w.toolTipWidget() == &w.flikKnapp && w.popup() &&
                  w.popup()->body().text() == "Knapp i A",
              "23: i den visade fliken visar knappen sin tooltip", w.visad());
        w.flytta(w.mitt(w.etikett));   // kall igen inför fall 18
        nästa();
    });
    // ---- 24. RUBRIKKNAPPARNA: panelens knappar är DELAR av
    //          panelen (`toolTipAt`) — knappnålen på den fästa flik A säger
    //          vad ett tryck gör, "Dölj automatiskt", och stängknappen
    //          "Stäng". Ramverkets egna texter (`glaze::tr`), så det förväntade är
    //          tokenets upplösta text. Knapparna står längst till höger i
    //          rubriken: stäng ytterst, knappnålen innanför.
    steg.push_back([&] {
        const gfx::Rectf r = w.flikA.resolve(w.flikA.behaviorCtx());
        const float      h = w.style().dockHeaderH * w.window().uiScale();
        w.flytta({r.x + r.w - 1.5f * h, r.y + h / 2.0f});
        w.vila();
        const glaze::TrText nålT = glaze::tr(glaze::str::DockAutoHide);
        const std::string   nål  = nålT.str();
        check(w.toolTipWidget() == &w.flikA && w.popup() &&
                  w.popup()->body().text() == nål,
              "24: knappnålen på en fäst panel säger \"" + nål + "\"",
              w.popup() ? w.popup()->body().text() : w.visad());
        nästa();
    });
    steg.push_back([&] {
        const gfx::Rectf r = w.flikA.resolve(w.flikA.behaviorCtx());
        const float      h = w.style().dockHeaderH * w.window().uiScale();
        w.flytta({r.x + r.w - h / 2.0f, r.y + h / 2.0f});
        check(w.toolTipWidget() == nullptr,
              "24: till stängknappen — samma källa, en annan text: den förra "
              "göms direkt",
              w.visad());
        nästa();
    });
    steg.push_back([&] {
        const gfx::Rectf r = w.flikA.resolve(w.flikA.behaviorCtx());
        const float      h = w.style().dockHeaderH * w.window().uiScale();
        w.flytta({r.x + r.w - h / 2.0f, r.y + h / 2.0f + 1.0f});
        w.vila();
        const glaze::TrText stängT = glaze::tr(glaze::str::DockClose);
        const std::string   stäng  = stängT.str();
        check(w.toolTipWidget() == &w.flikA && w.popup() &&
                  w.popup()->body().text() == stäng,
              "24: stängknappen säger \"" + stäng + "\"",
              w.popup() ? w.popup()->body().text() : w.visad());
        w.flytta(w.mitt(w.etikett));   // kall igen inför fall 18
        nästa();
    });
    // ---- Q1. Den riktiga routingen visar en
    //      KORTAD etiketts hela text — och en som ryms ger ingen tooltip.
    //      SABOTAGE: `toolTipAt` som jämför mot `measure` i stället för
    //      `elide` (gränsfallet), eller som alltid svarar med texten (den
    //      breda ger då en tooltip).
    steg.push_back([&] {
        w.flytta(w.mitt(w.kortad));
        w.vila();
        check(w.toolTipWidget() == &w.kortad && w.popup() &&
                  w.popup()->body().text() == "Lång text",
              "Q1: en kortad etikett visar hela texten som tooltip", w.visad());
        w.flytta(w.mitt(w.ryms));
        w.vila();
        check(w.toolTipWidget() == nullptr,
              "Q1: en etikett som ryms ger ingen tooltip", w.visad());
        w.flytta(w.mitt(w.etikett));
        nästa();
    });
    // ---- 18. DEN RIKTIGA TIMERN — kall väntan.
    steg.push_back([&] {
        w.flytta(w.mitt(w.knappB));
        check(w.toolTipWidget() == nullptr, "18: inget direkt");
        w.efterVantan = [&] {
            check(w.toolTipWidget() == &w.knappB,
                  "18: den RIKTIGA timern visade tooltipen efter vila",
                  w.visad());
            // ---- 19. VARM: direkt till nästa källa ⇒ kort väntan. Vid
            //          kontrollen har den korta tiden gått men inte den
            //          långa — en kall väntan hade inte hunnit.
            w.flytta(w.mitt(w.knappA));
            w.efterVantan = [&] {
                check(w.toolTipWidget() == &w.knappA,
                      "19: VARM väntan — nästa tooltip kom efter den korta "
                      "tiden", w.visad());
                w.flytta(w.mitt(w.etikett));
                nästa();
            };
            const glaze::Style& s = w.style();
            w.window().startTimer(
                kVantTimer, static_cast<std::uint32_t>(
                                (s.toolTipReshowMs + s.toolTipDelayMs) / 2));
        };
        w.window().startTimer(
            kVantTimer,
            static_cast<std::uint32_t>(w.style().toolTipDelayMs * 2));
    });
    steg.push_back([&] { w.close(); });

    nästa();
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    } else if (i < steg.size()) {
        std::printf("  FEL  kedjan stannade efter steg %zu av %zu\n", i,
                    steg.size());
        ++g_fails;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("tooltip_gate\n");
    fallBasen();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
