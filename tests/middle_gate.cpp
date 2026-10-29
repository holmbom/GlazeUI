// middle_gate — MITTENKNAPPEN, OVERLAY-VANDRINGEN och PEKARENS NYA FORMER
// (grunden i AppWindow och gfx — före ritytans gester).
//
// Kör:  ./build/tests/middle_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// MITTENKNAPPEN ÄR ROUTING, och därför matar grinden FÖNSTRETS ingång
// (`AppWindow::handleEvent`, dnd_gate-prejudikatet) i ett DOLT fönster med
// räknande testwidgets i stället för att anropa krokarna själv: att trycket
// bubblar, att draget och släppet går till den som tog trycket och att
// fokusförlust och borttagning avbryter bor alla i `handleEvent`/
// `detachTree`, och en grind som anropade `middleDown` direkt hade varit
// grön oavsett om fönstret någonsin frågade den.
//
// TVÅDELAD:
//
//   HEADLESS     `Renderer::clipBounds` mot `NullRenderer` (stacken tom,
//                snittet, den rundade nivån, pop) och `KeyArgs` standardvärden.
//   MED FÖNSTER  trycket, draget, släppet, avbrotten, explicit capture,
//                pekarformen och overlay-vandringens box och klipp. Klippet
//                läses med `clipBounds` INNE I `drawOverlay` — alltså den
//                scissor renderern faktiskt har när widgeten ritar, inte en
//                uträkning av vad den borde ha. Förväntat värde räknas ur widgetarnas
//                egna publika krokar (`clipRect`, `resolve`, `behaviorCtx`).
//
// SABOTAGEN den fäller:
//   * mittengrenen bort ur MouseButtonDown (fall 1: ingen fick trycket)
//   * `detachTree` nollar inte mittentrycket (fall 6: den lånade widgeten
//     får drag efter unadopt; den ÄGDA är förstörd, så där blir felet
//     ett anrop på frigjort minne — en trolig krasch, ingen garanterad röd)
//   * fokusförlusten nollar inte (fall 5)
//   * bubblingen hoppar inte över en inaktiverad (fall 1b)
//   * overlay-vandringen utan förfädernas klipp (fall 9: lagerA:s scissor)
//   * en klippnivå som inte poppas (fall 9: lagerB ärver lagerA:s klipp)
//
// FALL 11, DEN TOMMA REKTEN (eget dolt fönster): en widget med bredd
// 0 och en med höjd 0 får varken `draw`, `drawAfterChildren` eller
// `drawOverlay` — i full (T1) och partiell bild (T2, partiell enligt
// profileraren, annars en SKIP-rad) — men `frameStep`; dess barn med egen yta
// ritas (T3); med bredd 40 ritas den röd, och tillbaka till 0 lämnar den
// inget rött spöke (T4); träffprovet i hörnet med och utan yta (T5).
// SABOTAGEN:
//   * spärren bort i `drawTree` ........................ T1
//   * spärren bort bara i `drawTreePartial` ............. T2
//   * spärren bort bara för `drawAfterChildren` ......... T1, T2
//   * spärren bort i overlay-vandringen ................. T1 (lagret)
//   * spärren som `continue` före popen i overlay-vandringen  T1 (klipp-
//     stacken tom efter presenten)
//   * `stepTree` bort ur renderingen .................... T1, T2 (steget)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * att backenderna LEVERERAR mittenknappen (D3D11 WM_MBUTTON*, X11
//     Button2, Wayland BTN_MIDDLE, AppKit otherMouse* med buttonNumber 2)
//     och håller mus-capture tills den släpps utanför fönstret — grinden
//     matar ingången direkt. Det står i doc/os/*/TESTS.md.
//   * att OS:et VISAR rätt pekare — `cursor()` svarar med den BEGÄRDA
//     formen (fasadens kontrakt). Systempekarnas utseende (IDC_CROSS/
//     IDC_SIZEALL, temanamnen, XC_crosshair/XC_fleur, NSCursor) är
//     TESTS-rader.
//   * de RUNDADE maskerna i overlay-klippet — `clipBounds` svarar med
//     scissorn. Radien går genom samma `pushClip(rect, radius)` som
//     ritvandringen, men ingen pixel mäts här.
//   * hur ett overlay-lager SER UT — det ritas i backbuffern efter
//     compositen, som `captureFrame` inte läser.
//   * fall 11: GlazeBuilders previewvandring (`drawPreviewTree`, samma
//     spärr — preview_gate) och en egen widget utanför ramverket.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <gfx/null_renderer.h>

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

std::string rekt(const gfx::Rectf& r)
{
    char b[96];
    std::snprintf(b, sizeof b, "{%.2f, %.2f, %.2f, %.2f}", r.x, r.y, r.w, r.h);
    return b;
}

bool lika(const gfx::Rectf& a, const gfx::Rectf& b)
{
    constexpr float e = 0.01f;
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e &&
           std::fabs(a.w - b.w) < e && std::fabs(a.h - b.h) < e;
}

gfx::Rectf snitt(const gfx::Rectf& a, const gfx::Rectf& b)
{
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    return gfx::Rectf{x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
}

const char* formNamn(gfx::PointerShape s)
{
    switch (s) {
    case gfx::PointerShape::Arrow:      return "Arrow";
    case gfx::PointerShape::IBeam:      return "IBeam";
    case gfx::PointerShape::Hand:       return "Hand";
    case gfx::PointerShape::ResizeWE:   return "ResizeWE";
    case gfx::PointerShape::ResizeNS:   return "ResizeNS";
    case gfx::PointerShape::NotAllowed: return "NotAllowed";
    case gfx::PointerShape::Crosshair:  return "Crosshair";
    case gfx::PointerShape::Move:       return "Move";
    case gfx::PointerShape::ClosedHand: return "ClosedHand";
    case gfx::PointerShape::Hidden:     return "Hidden";
    case gfx::PointerShape::Wait:       return "Wait";
    }
    return "?";
}

// ================================================================ HEADLESS

// `clipBounds` är stackens topp — snittet av ALLA nivåers rektanglar, också
// den rundades (scissorn), och false när stacken är tom.
void fallKlippfragan()
{
    gfx::NullRenderer r;
    gfx::Rectf        ut{};
    check(!r.clipBounds(ut), "clipBounds: tom stack ⇒ false (ingen klippning)");

    r.pushClip(gfx::Rectf{10, 20, 100, 50});
    check(r.clipBounds(ut) && lika(ut, {10, 20, 100, 50}),
          "clipBounds: en nivå ⇒ dess rektangel", rekt(ut));

    r.pushClip(gfx::Rectf{50, 0, 100, 40}, 6.0f);   // rundad nivå
    check(r.clipBounds(ut) && lika(ut, {50, 20, 60, 20}),
          "clipBounds: en RUNDAD nivå snittar som en rak (scissorn)",
          rekt(ut));

    r.pushClip(gfx::Rectf{500, 500, 10, 10});   // disjunkt
    check(r.clipBounds(ut) && ut.w == 0.0f && ut.h == 0.0f,
          "clipBounds: disjunkt nivå ⇒ tom rektangel (klipper allt)",
          rekt(ut));

    r.popClip();
    r.popClip();
    check(r.clipBounds(ut) && lika(ut, {10, 20, 100, 50}),
          "clipBounds: pop återställer den yttre nivån", rekt(ut));
    r.popClip();
    check(!r.clipBounds(ut), "clipBounds: tom igen efter sista pop");
}

void fallKeyArgs()
{
    glaze::KeyArgs k;
    check(k.key == gfx::Key::Unknown && k.mods == 0 && !k.handled,
          "KeyArgs: Unknown, inga modifierare, inte hanterad från början");
}

// ============================================================= MED FÖNSTER

// En yta som RÄKNAR mittenknappens krokar, sitt overlay-lager och sina
// ritningar, och loggar trycken i en gemensam logg (så bubblingens ORDNING
// syns). En Panel, så den kan vara både förälder och löv — och klipper sina
// barn (Panels förval), vilket overlay-fallet bygger på.
struct Yta : glaze::Panel {
    std::string               id;
    std::vector<std::string>* logg = nullptr;

    bool tar        = false;   // middleDown svarar
    bool svarUpp    = false;   // middleUp svarar
    bool medLager   = false;   // hasOverlay
    bool korsPekare = false;   // cursorAt: Crosshair, Move under mittentrycket

    int   ner = 0, drag = 0, upp = 0, lager = 0, ritningar = 0, klick = 0;
    float dragX = 0.0f, dragY = 0.0f;
    bool  tryckIUpp = true;    // `middlePressed()` INNE i middleUp

    gfx::Rectf lagerBox{}, lagerKlipp{};
    bool       lagerHarKlipp = false;

    void koppla(const std::string& namn, std::vector<std::string>& l)
    {
        id   = namn;
        name = namn;
        logg = &l;
        onClick = [this] { ++klick; };
    }
    void lägg(float x_, float y_, float w_, float h_)
    {
        x      = x_;
        y      = y_;
        width  = w_;
        height = h_;
    }
    // Explicit capture är skyddad på Widget — ytan får be om den själv.
    void fånga() { setPointerCapture(); }
    void släpp() { releasePointerCapture(); }

protected:
    bool middleDown(const glaze::Ctx&, float, float) override
    {
        ++ner;
        if (logg)
            logg->push_back(id + ":ner");
        return tar;
    }
    void middleDrag(const glaze::Ctx&, float mx, float my) override
    {
        ++drag;
        dragX = mx;
        dragY = my;
    }
    bool middleUp(const glaze::Ctx&, float, float) override
    {
        ++upp;
        tryckIUpp = middlePressed();
        if (logg)
            logg->push_back(id + ":upp");
        return svarUpp;
    }
    gfx::PointerShape cursorAt(const glaze::Ctx&, float, float) const override
    {
        if (!korsPekare)
            return gfx::PointerShape::Arrow;
        return middlePressed() ? gfx::PointerShape::Move
                               : gfx::PointerShape::Crosshair;
    }
    bool hasOverlay() const override { return medLager; }
    void drawOverlay(const glaze::Ctx& c) override
    {
        ++lager;
        lagerBox      = c.box;
        lagerHarKlipp = c.r->clipBounds(lagerKlipp);
        if (logg)
            logg->push_back(id + ":lager");
    }
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Panel::draw(c);
    }
};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    using glaze::AppWindow::makeCtx;

    std::vector<std::string> logg;
    Yta ytter, inre, av;          // bubblingen: inre avstår, av är inaktiverad
    Yta fri, granne, ingen;       // fri tar, granne och ingen avstår
    Yta rörlig;                   // fall 6: lämnar trädet mitt i draget
    Yta klipp, lagerA, lagerB;    // fall 9: overlay-lagren
    Yta dold, doldFar, doldBarn;  // fall 9: dolda frågas inte
    glaze::TextInput falt;        // fall 10: fokus flyttas inte av mitten

    Vard() : glaze::AppWindow("middle_gate", 600, 400, gfx::Chrome::None)
    {
        ytter.koppla("ytter", logg);
        inre.koppla("inre", logg);
        av.koppla("av", logg);
        fri.koppla("fri", logg);
        granne.koppla("granne", logg);
        ingen.koppla("ingen", logg);
        rörlig.koppla("rorlig", logg);
        klipp.koppla("klipp", logg);
        lagerA.koppla("lagerA", logg);
        lagerB.koppla("lagerB", logg);
        dold.koppla("dold", logg);
        doldFar.koppla("doldFar", logg);
        doldBarn.koppla("doldBarn", logg);

        ytter.lägg(20, 20, 200, 150);
        inre.lägg(10, 10, 80, 60);
        av.lägg(100, 10, 80, 60);
        fri.lägg(250, 20, 120, 80);
        granne.lägg(250, 130, 120, 60);
        ingen.lägg(400, 20, 60, 40);
        rörlig.lägg(400, 80, 60, 40);
        klipp.lägg(20, 200, 150, 100);
        lagerA.lägg(100, 40, 120, 100);   // sticker ut ur klipp till höger/ned
        lagerB.lägg(400, 200, 100, 80);
        dold.lägg(250, 220, 60, 40);
        doldFar.lägg(320, 220, 60, 60);
        doldBarn.lägg(5, 5, 30, 30);

        ytter.tar = true;
        av.tar    = true;
        av.disable();
        fri.tar        = true;
        fri.korsPekare = true;
        rörlig.tar     = true;
        lagerA.medLager = true;
        lagerB.medLager = true;
        dold.medLager   = true;
        dold.hide();
        doldBarn.medLager = true;
        doldFar.hide();

        root().adopt(ytter);
        ytter.adopt(inre);
        ytter.adopt(av);
        root().adopt(fri);
        root().adopt(granne);
        root().adopt(ingen);
        root().adopt(klipp);
        klipp.adopt(lagerA);
        root().adopt(lagerB);
        root().adopt(dold);
        root().adopt(doldFar);
        doldFar.adopt(doldBarn);

        falt.name  = "falt";
        falt.x     = 400;
        falt.y     = 320;
        falt.width = 100;
        root().adopt(falt);
    }

    // ---- händelserna, genom den RIKTIGA ingången
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    void mus(gfx::EventType t, gfx::Pointf p,
             gfx::MouseButton b = gfx::MouseButton::Middle)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = b;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
    void flytta(gfx::Pointf p)
    {
        mus(gfx::EventType::MouseMotion, p, gfx::MouseButton::None);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    gfx::PointerShape pekare() { return window().cursor(); }
};

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);

    // `run()` ÄR öppnaren — aldrig show() först (dropdown_gate-läxan).
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const gfx::Pointf ute{-30.0f, -30.0f};   // utanför fönstret

        // ---- 1. TRYCKET BUBBLAR: inre avstår, ytter tar.
        w.logg.clear();
        w.ner(w.mitt(w.inre));
        check(w.logg == std::vector<std::string>{"inre:ner", "ytter:ner"},
              "1: trycket frågar widgeten under pekaren, sedan förfadern",
              fog(w.logg));
        check(w.ytter.middlePressed() && !w.inre.middlePressed(),
              "1: den som svarade true HÅLLER trycket — inte den som avstod");

        // ---- 2. DRAGET går till samma widget, var pekaren än står.
        const gfx::Pointf F = w.mitt(w.fri);
        w.flytta(F);
        check(w.ytter.drag == 1 && w.fri.drag == 0 && w.inre.drag == 0 &&
                  w.ytter.dragX == F.x && w.ytter.dragY == F.y,
              "2: draget över en annan widget går till den som tog trycket",
              std::to_string(w.ytter.drag) + "/" + std::to_string(w.fri.drag));
        w.flytta(ute);
        check(w.ytter.drag == 2 && w.ytter.dragX == ute.x &&
                  w.ytter.dragY == ute.y,
              "2: …och utanför fönstret", std::to_string(w.ytter.drag));

        // ---- 3. SLÄPPET: en gång, middlePressed() släckt i kroken, och
        //         efteråt är musen bara en mus.
        w.logg.clear();
        w.upp(ute);
        check(w.logg == std::vector<std::string>{"ytter:upp"} &&
                  w.ytter.upp == 1,
              "3: släppet utanför fönstret når den som tog trycket, EN gång",
              fog(w.logg));
        check(!w.ytter.tryckIUpp,
              "3: middlePressed() är redan släckt inne i middleUp");
        check(!w.ytter.middlePressed(), "3: …och efteråt");
        w.flytta(w.mitt(w.ytter));
        check(w.ytter.drag == 2,
              "3: en rörelse efter släppet är inget drag",
              std::to_string(w.ytter.drag));
        w.upp(w.mitt(w.ytter));
        check(w.ytter.upp == 1, "3: ett släpp utan tryck når ingen",
              std::to_string(w.ytter.upp));

        // ---- 1b. En INAKTIVERAD hoppas över i bubblingen.
        w.logg.clear();
        w.ner(w.mitt(w.av));
        check(w.logg == std::vector<std::string>{"ytter:ner"} &&
                  w.ytter.middlePressed() && !w.av.middlePressed(),
              "1b: den inaktiverade frågas inte — förfadern tar trycket",
              fog(w.logg));
        w.upp(w.mitt(w.av));

        // ---- 1c. INGEN tar trycket: ingenting händer, roten frågas inte.
        w.logg.clear();
        w.ner(w.mitt(w.ingen));
        check(w.logg == std::vector<std::string>{"ingen:ner"} &&
                  !w.ingen.middlePressed(),
              "1c: avstår alla händer ingenting (roten är ingen yta)",
              fog(w.logg));
        w.flytta(w.mitt(w.fri));
        w.upp(w.mitt(w.fri));
        check(w.ingen.drag == 0 && w.ingen.upp == 0 && w.fri.drag == 0 &&
                  w.fri.upp == 0,
              "1c: …varken drag eller släpp till någon");
        w.ner(gfx::Pointf{590.0f * w.window().uiScale(),
                          390.0f * w.window().uiScale()});   // tom yta
        check(w.logg == std::vector<std::string>{"ingen:ner"},
              "1c: ett tryck på tom fönsteryta frågar ingen", fog(w.logg));
        w.upp(gfx::Pointf{590.0f * w.window().uiScale(),
                          390.0f * w.window().uiScale()});

        // ---- 3b. SVARET ur middleUp: false ritar bara det widgeten själv
        //          invaliderar, true ritar om hela fönstret (mouseUp-formen).
        w.window().renderFrame();   // töm all damage
        w.granne.ritningar = 0;
        w.ner(F);
        w.upp(F);
        w.window().renderFrame();
        check(w.fri.upp == 1 && w.granne.ritningar == 0,
              "3b: middleUp = false ⇒ ingen omritning av en granne",
              std::to_string(w.granne.ritningar));
        w.fri.svarUpp = true;
        w.ner(F);
        w.upp(F);
        w.window().renderFrame();
        check(w.fri.upp == 2 && w.granne.ritningar >= 1,
              "3b: middleUp = true ⇒ hela fönstret ritas om (en handler kördes)",
              std::to_string(w.granne.ritningar));
        w.fri.svarUpp = false;

        // ---- 3c. Ett NYTT tryck medan ett hålls (släppet tappades) ersätter
        //          det gamla utan middleUp.
        w.ner(F);
        w.ner(w.mitt(w.ytter));
        check(w.ytter.middlePressed() && !w.fri.middlePressed() &&
                  w.fri.upp == 2,
              "3c: ett nytt tryck ersätter ett tappat släpp — utan middleUp",
              std::to_string(w.fri.upp));
        w.upp(w.mitt(w.ytter));

        // ---- 4. INAKTIVERAD UNDER TRYCKET: inga drag, men släppet kommer.
        w.ner(F);
        w.fri.disable();
        const int dragFöre = w.fri.drag;
        w.flytta(w.mitt(w.granne));
        check(w.fri.drag == dragFöre,
              "4: en inaktiverad får inga drag (mouseDrag-regeln)",
              std::to_string(w.fri.drag - dragFöre));
        w.upp(w.mitt(w.granne));
        check(w.fri.upp == 3 && !w.fri.middlePressed(),
              "4: …men släppet, så ett påbörjat drag kan avslutas",
              std::to_string(w.fri.upp));
        w.fri.enable();

        // ---- 5. FOKUSFÖRLUST avbryter UTAN middleUp.
        w.ner(F);
        check(w.fri.middlePressed(), "5: trycket hålls");
        w.enkel(gfx::EventType::WindowFocusLost);
        check(!w.fri.middlePressed(),
              "5: fokusförlusten släcker middlePressed()");
        const int drag5 = w.fri.drag;
        w.flytta(w.mitt(w.granne));
        w.upp(w.mitt(w.granne));
        check(w.fri.drag == drag5 && w.fri.upp == 3,
              "5: …utan middleUp, och efteråt varken drag eller släpp",
              std::to_string(w.fri.upp));
        w.enkel(gfx::EventType::WindowFocusGained);

        // ---- 6. BORTTAGNING mitt i draget nollar trycket. Den LÅNADE
        //         widgeten lever efter unadopt, så en kvarglömd pekare syns
        //         som ett drag på den; den ÄGDA förstörs.
        w.root().adopt(w.rörlig);
        const gfx::Pointf R = w.mitt(w.rörlig);
        w.ner(R);
        check(w.rörlig.middlePressed(), "6: den rörliga håller trycket");
        check(w.root().unadopt(w.rörlig), "6: unadopt mitt i draget");
        check(!w.rörlig.middlePressed(),
              "6: borttagningen släcker trycket");
        w.flytta(F);
        w.upp(F);
        check(w.rörlig.drag == 0 && w.rörlig.upp == 0,
              "6: …varken drag eller middleUp till en widget utanför trädet",
              std::to_string(w.rörlig.drag) + "/" +
                  std::to_string(w.rörlig.upp));

        auto* ägd = w.root().add<Yta>();
        ägd->koppla("agd", w.logg);
        ägd->lägg(400, 140, 60, 40);
        ägd->tar = true;
        w.ner(w.mitt(*ägd));
        check(ägd->middlePressed(), "6: den ägda håller trycket");
        check(w.root().unadopt(*ägd), "6: den ägda unadopteras (förstörs)");
        w.logg.clear();
        w.flytta(F);   // en kvarglömd pekare hade anropat en förstörd widget
        w.upp(F);
        check(w.logg.empty(), "6: efteråt når trycket ingen", fog(w.logg));

        // ---- 7. EXPLICIT CAPTURE: bara den fångande frågas.
        w.fri.fånga();
        w.logg.clear();
        w.ner(w.mitt(w.ytter));
        check(w.logg == std::vector<std::string>{"fri:ner"} &&
                  w.fri.middlePressed(),
              "7: under explicit capture frågas den fångande, inte den under "
              "pekaren", fog(w.logg));
        w.upp(w.mitt(w.ytter));
        w.fri.släpp();

        // ---- 8. PEKAREN: Crosshair över ytan, Move medan trycket hålls —
        //         också över en granne och efter MouseLeave — och tillbaka.
        w.flytta(F);
        check(w.pekare() == gfx::PointerShape::Crosshair,
              "8: hårkorset över ytan", formNamn(w.pekare()));
        w.ner(F);
        check(w.pekare() == gfx::PointerShape::Move,
              "8: flytta-pekaren när trycket tas", formNamn(w.pekare()));
        const gfx::Pointf G = w.mitt(w.granne);
        w.flytta(G);
        check(w.pekare() == gfx::PointerShape::Move,
              "8: trycket äger pekaren över grannen", formNamn(w.pekare()));
        w.enkel(gfx::EventType::MouseLeave);
        check(w.pekare() == gfx::PointerShape::Move,
              "8: MouseLeave återställer inte ett hållet tryck",
              formNamn(w.pekare()));
        w.upp(G);
        check(w.pekare() == gfx::PointerShape::Arrow,
              "8: efter släppet är det grannens pil", formNamn(w.pekare()));
        w.flytta(F);
        w.ner(F);
        w.enkel(gfx::EventType::WindowFocusLost);
        w.flytta(F);
        check(w.pekare() == gfx::PointerShape::Crosshair,
              "8: efter fokusförlusten är trycket släckt — hårkorset igen",
              formNamn(w.pekare()));
        w.enkel(gfx::EventType::WindowFocusGained);

        // ---- 9. OVERLAY-VANDRINGEN: box, klipp, ordning, dolda.
        w.logg.clear();
        w.window().renderFrame();
        check(w.lagerA.lager >= 1 && w.lagerB.lager >= 1,
              "9: drawOverlay anropas för de synliga lagren",
              std::to_string(w.lagerA.lager) + "/" +
                  std::to_string(w.lagerB.lager));
        check(w.dold.lager == 0 && w.doldBarn.lager == 0,
              "9: en dold widget och ett dolt delträd frågas inte");
        std::vector<std::string> lagren;
        for (const std::string& s : w.logg)
            if (s.find(":lager") != std::string::npos)
                lagren.push_back(s);
        check(lagren == std::vector<std::string>{"lagerA:lager",
                                                 "lagerB:lager"},
              "9: i trädordning, en gång per present", fog(lagren));

        const gfx::Rectf boxA = w.lagerA.resolve(w.lagerA.behaviorCtx());
        check(lika(w.lagerA.lagerBox, boxA),
              "9: lagret får widgetens egen rect som box",
              rekt(w.lagerA.lagerBox) + " mot " + rekt(boxA));
        const gfx::Rectf innehåll = w.makeCtx().area;
        const glaze::Widget& kw  = w.klipp;
        const gfx::Rectf     kr  = kw.clipRect(w.klipp.behaviorCtx());
        const gfx::Rectf     vänt = snitt(innehåll, kr);
        check(boxA.x + boxA.w > kr.x + kr.w && boxA.y + boxA.h > kr.y + kr.h,
              "9: (fixturen) lagerA sticker ut ur förälderns klipp");
        check(w.lagerA.lagerHarKlipp && lika(w.lagerA.lagerKlipp, vänt),
              "9: lagret klipps mot FÖRFÄDERNAS klipp inom innehållsytan",
              rekt(w.lagerA.lagerKlipp) + " mot " + rekt(vänt));
        check(w.lagerB.lagerHarKlipp && lika(w.lagerB.lagerKlipp, innehåll),
              "9: ett lager direkt under roten klipps mot innehållsytan — "
              "och ärver inget klipp från lagret före",
              rekt(w.lagerB.lagerKlipp) + " mot " + rekt(innehåll));
        gfx::Rectf efter{};
        check(!w.window().renderer().clipBounds(efter),
              "9: klippstacken är tom efter presenten (vandringen balanserar)",
              rekt(efter));

        // ---- 10. VÄNSTERKNAPPENS BOKFÖRING ORÖRD: inget fokus, ingen
        //          pressed(), inget klick av mitten — och vänster frågar
        //          inte middleDown.
        w.ner(w.mitt(w.falt));
        check(!w.falt.focused() && !w.falt.pressed(),
              "10: ett mittentryck flyttar inte fokus och trycker inte ner");
        w.upp(w.mitt(w.falt));
        const int klickFöre = w.fri.klick;
        w.ner(F);
        check(!w.fri.pressed(), "10: den som tar mittentrycket är inte "
                                "pressed()");
        w.upp(F);
        check(w.fri.klick == klickFöre, "10: mitten ner och upp är inget klick",
              std::to_string(w.fri.klick - klickFöre));
        const int nerFöre = w.fri.ner;
        w.mus(gfx::EventType::MouseButtonDown, F, gfx::MouseButton::Left);
        w.mus(gfx::EventType::MouseButtonUp, F, gfx::MouseButton::Left);
        check(w.fri.ner == nerFöre && w.fri.klick == klickFöre + 1,
              "10: vänsterknappen frågar inte middleDown — och klickar",
              std::to_string(w.fri.ner - nerFöre));

        w.close();
    });
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ============================================== 11. DEN TOMMA REKTEN

// Räknar de tre ritkrokarna och bildens steg — och fyller sin yta röd när
// `röd` är satt, så ett spöke syns i pixlarna. Ingen Panel: föräldern till
// barnet ska INTE klippa (barnen vandras ändå).
struct Tom : glaze::Widget {
    int  ritad = 0, efter = 0, lager = 0, steg = 0;
    bool röd   = false;

    void drawAfterChildren(const glaze::Ctx&) override { ++efter; }
    void frameStep() override { ++steg; }
    std::string tal() const
    {
        return std::to_string(ritad) + "/" + std::to_string(efter) + "/" +
               std::to_string(lager);
    }

protected:
    bool hasOverlay() const override { return true; }
    void drawOverlay(const glaze::Ctx&) override { ++lager; }
    void draw(const glaze::Ctx& c) override
    {
        ++ritad;
        if (röd)
            glaze::Draw::rect(c, c.bounds(), 0.0f, gfx::Color{255, 0, 0, 255});
    }
};

struct TomVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Yta syskon;            // täcker hörnen; dess omritning är en PARTIELL bild
    Tom bredd0, höjd0;     // bredd 0 resp. höjd 0
    Tom barn;              // bredd0:s barn, med en egen yta

    TomVard() : glaze::AppWindow("middle_gate tom rekt", 600, 400, gfx::Chrome::None)
    {
        syskon.lägg(0, 0, 300, 200);
        bredd0.x      = 20;
        bredd0.y      = 20;
        bredd0.height = 40;
        höjd0.x       = 60;
        höjd0.y       = 20;
        höjd0.width   = 40;
        barn.x        = 200;
        barn.y        = 100;
        barn.width    = 30;
        barn.height   = 30;
        root().adopt(syskon);
        root().adopt(bredd0);
        root().adopt(höjd0);
        bredd0.adopt(barn);
    }

    gfx::Rectf rektAv(glaze::Widget& x) { return x.resolve(x.behaviorCtx()); }
    void       flytta(gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseMotion;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        handleEvent(e);
    }
    // Röda pixlar i rekten `r` ur en färsk bild; −1 när ytan inte kan läsas.
    int röda(const gfx::Rectf& r)
    {
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, H = 0;
        if (!window().captureFrame(rgba, W, H))
            return -1;
        int n = 0;
        for (int y = std::max(0, int(r.y)); y < std::min(H, int(r.y + r.h)); ++y)
            for (int x = std::max(0, int(r.x)); x < std::min(W, int(r.x + r.w));
                 ++x) {
                const std::size_t i = (std::size_t(y) * std::size_t(W) +
                                       std::size_t(x)) * 4u;
                if (rgba[i] > 200 && rgba[i + 1] < 60 && rgba[i + 2] < 60)
                    ++n;
            }
        return n;
    }
};

void fallTomRekt()
{
    TomVard w;
    w.setOffscreen(true);
    glaze::profiler().setEnabled(true);   // T2: var bilden partiell?

    bool kordes = false;
    glaze::app().post([&] {
        kordes     = true;
        auto nolla = [&w] {
            for (Tom* t : {&w.bredd0, &w.höjd0, &w.barn})
                t->ritad = t->efter = t->lager = t->steg = 0;
        };

        // ---- T1. FULL bild: ingen av de tre krokarna — men bildens steg.
        nolla();
        w.invalidate();
        w.window().renderFrame();
        check(w.bredd0.tal() == "0/0/0" && w.höjd0.tal() == "0/0/0",
              "11 T1: full bild — draw, drawAfterChildren och drawOverlay "
              "anropas inte med tom rekt (bredd 0, höjd 0)",
              w.bredd0.tal() + " " + w.höjd0.tal());
        check(w.bredd0.steg >= 1 && w.höjd0.steg >= 1,
              "11 T1: …men frameStep körs också utan yta");
        gfx::Rectf kvar{};
        check(!w.window().renderer().clipBounds(kvar),
              "11 T1: klippstacken är tom efter presenten — ett lager utan yta "
              "poppar ändå sina nivåer", rekt(kvar));
        // ---- T3. Barnen vandras.
        check(w.barn.ritad >= 1 && w.barn.efter >= 1 && w.barn.lager >= 1,
              "11 T3: barnet till den tomma, med egen yta, ritas", w.barn.tal());

        // ---- T2. PARTIELL bild över hörnet.
        nolla();
        w.syskon.invalidate();
        w.window().renderFrame();
        const std::vector<glaze::FrameProfile> f = glaze::profiler().frames(w, 1);
        if (f.empty() ||
            f.back().count[std::size_t(glaze::ProfileCount::FullDamage)] != 0) {
            std::printf("  --   11 T2: bilden blev inte partiell — fallet "
                        "hoppas över\n");
            ++g_skips;
        } else {
            check(w.bredd0.tal() == "0/0/0" && w.höjd0.tal() == "0/0/0",
                  "11 T2: partiell bild — inte heller där",
                  w.bredd0.tal() + " " + w.höjd0.tal());
            check(w.bredd0.steg >= 1,
                  "11 T2: …och frameStep körs i den partiella vägen");
        }

        // ---- T4. Med en yta ritas den, och tillbaka till tom lämnar den
        //          inget spöke (den gamla ytan ingår i skadan).
        nolla();
        w.bredd0.röd   = true;
        w.bredd0.width = 40;
        w.bredd0.invalidate();
        const gfx::Rectf ytan = w.rektAv(w.bredd0);
        const int        före = w.röda(ytan);
        check(w.bredd0.ritad >= 1 && w.bredd0.efter >= 1 && w.bredd0.lager >= 1,
              "11 T4: med bredd 40 ritas alla tre", w.bredd0.tal());
        // ---- T5. Träffprovet i hörnet: med yta träffas den, utan inte.
        const gfx::Pointf hörn{ytan.x, ytan.y + ytan.h / 2.0f};
        w.flytta(hörn);
        check(w.bredd0.hovered(), "11 T5: (kontroll) med yta träffas hörnet");
        w.bredd0.width = 0;
        w.bredd0.invalidate();
        const int efter = w.röda(ytan);
        w.flytta(hörn);
        check(!w.bredd0.hovered(), "11 T5: utan yta träffas den inte");
        if (före < 0 || efter < 0) {
            std::printf("  --   11 T4: pixlarna kunde inte läsas "
                        "(captureFrame) — spökkontrollen hoppas över\n");
            ++g_skips;
        } else {
            check(före > 0, "11 T4: (kontroll) den röda ytan syns",
                  std::to_string(före));
            check(efter == 0, "11 T4: tillbaka till tom — inget rött spöke",
                  std::to_string(efter));
        }
        w.close();
    });
    w.run();
    glaze::profiler().setEnabled(false);

    if (!kordes) {
        std::printf("  --   fall 11 hoppades över: inget fönster kunde skapas "
                    "(ingen display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("middle_gate\n");
    fallKlippfragan();
    fallKeyArgs();
    fallMedFonster();
    fallTomRekt();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
