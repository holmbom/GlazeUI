// splitter_gate — DELARENS grind, och grinden för de
// två ramverksytor delaren bygger på: SLÄPPET (`Widget::mouseUp`) och
// MUSPEKAREN (`gfx::Window::setCursor` + `Widget::cursorAt`).
//
// Kör:  ./build/tests/splitter_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// TVÅDELAD:
//
//   HEADLESS     LAYOUTEN mot HANDRÄKNADE värden (layout_gate-riggen: en
//                naken container, en Ctx utan renderer): AUTO-delningen, den
//                fasta rutan i båda riktningarna och när delaren växer,
//                minimimåtten — också när båda inte ryms — det liggande
//                skiktet, skalan 2 (enhetsfel syns bara där) och att ett
//                tredje barn inte placeras. De förväntade värdena är TAL skrivna här och
//                aldrig något läst ur delaren; det enda som läses ur stilen
//                är skiktets tjocklek, som är indata och inte svar.
//   MED FÖNSTER  ett DOLT fönster utan chrome, och händelserna matas genom
//                fönstrets RIKTIGA ingång (`AppWindow::handleEvent`,
//                protected — som i dnd_gate). Draget: förhandsvisning
//                under trycket, `onChange` EN gång vid släppet — också när
//                släppet sker utanför fönstret — och inte alls utan rörelse
//                eller när fokusförlusten avbryter trycket (då går läget
//                tillbaka). Pekaren: ↔ över skiktet och KVAR under draget
//                fast pekaren står över en ruta, I-balk över ett fält, hand
//                över en länk, pil över tom yta och efter MouseLeave. Explicit
//                capture: `mouseUp` når den fångande widgeten även när
//                släppet sker utanför den. Och FASADENS lagring: en form satt
//                FÖRE `show()` gäller efter, och en satt efter går fram — på
//                Linux är det PROXYNS två vidarebefordringar, som bygget inte
//                fångar.
//
// SABOTAGEN den fäller (prövade i en privat kopia när grinden skrevs):
//   * proxyn vidarebefordrar inte formen i `init()`   (fall F1, Linux)
//   * proxyn vidarebefordrar inte `setCursor` efter init (fall F1, Linux)
//   * `MouseButtonUp` levererar ingen `mouseUp`       (fall F2, F3, F6)
//   * pekaren frågar widgeten under pekaren även under ett tryck (fall F2)
//   * fokusförlusten lämnar trycket kvar               (fall F5)
//   * `paneSize` mätt från vänster som WinForms SplitterDistance (fall H4)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * vad OS:et faktiskt VISAR. `cursor()` är den begärda formen; att
//     Windows `WM_SETCURSOR`, Wayland `wl_pointer_set_cursor`, X11
//     `XDefineCursor` och macOS `NSCursor` ritar rätt pil kräver en riktig
//     mus över ett synligt fönster — det är en rad i doc/os/*/TESTS.md.
//   * hur skiktet SER UT — `pixel_gate` mäter att delaren ritar något i
//     varje palett, utseendet är verifierat renderat i alla tre UI.
//   * backendernas egna mus-events (rörelse utanför fönstret medan knappen
//     hålls) — grinden matar ingången direkt.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/glaze.h>

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

std::string rekt(const gfx::Rectf& r)
{
    char b[96];
    std::snprintf(b, sizeof b, "{%.2f, %.2f, %.2f, %.2f}",
                  static_cast<double>(r.x), static_cast<double>(r.y),
                  static_cast<double>(r.w), static_cast<double>(r.h));
    return b;
}

bool nära(float x, float y)
{
    return x - y < 0.01f && y - x < 0.01f;
}

bool lika(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return nära(a.x, b.x) && nära(a.y, b.y) && nära(a.w, b.w) &&
           nära(a.h, b.h);
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
//
// layout_gate-riggen: en NAKEN container sträckt över ytan, så delarens
// area är exakt `c.area`; delaren själv är dubbelankrad över hela den.

struct Yta final : glaze::Widget {
    Yta() { anchors = {true, true, true, true}; }
    void draw(const glaze::Ctx&) override {}
};

struct Ruta final : glaze::Widget {
    void draw(const glaze::Ctx&) override {}
};

glaze::Ctx ctxFör(const glaze::Style& st, float scale, const gfx::Rectf& area)
{
    glaze::Ctx c{};
    c.scale = scale;
    c.ui    = glaze::UI::Slate;
    c.style = &st;
    c.theme = &glaze::Theme::slateDefault().light;
    c.area  = area;
    return c;
}

// Barnets Ctx: förälderns childArea — exakt `AppWindow::drawTree`s steg.
glaze::Ctx barnCtx(const glaze::Widget& förälder, const glaze::Ctx& c)
{
    glaze::Ctx cc = c;
    cc.area       = förälder.childArea(c);
    return cc;
}

struct Rigg {
    Yta             rot;
    glaze::Splitter split;
    Ruta            a, b, c;

    Rigg()
    {
        split.anchors = {true, true, true, true};
        rot.adopt(split);
        split.adopt(a);
        split.adopt(b);
        split.adopt(c);   // det TREDJE barnet — placeras inte
    }

    // Rutorna i FYSISKA px, ur den riktiga vägen: rotens Ctx → delarens
    // childArea → rutans resolve (som frågar delarens placeChild).
    gfx::Rectf ruta(const glaze::Widget& w, const glaze::Ctx& rc)
    {
        const glaze::Ctx sc = barnCtx(rot, rc);     // delarens Ctx
        return w.resolve(barnCtx(split, sc));       // rutans Ctx
    }
};

void fallLayout()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const float         t  = st.splitterThick;   // indata, inte svar

    // H1 — AUTO delar LIKA: (600 − t) / 2 på var sida om skiktet.
    {
        Rigg       r;
        const auto c   = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});
        const float h  = (600.0f - t) / 2.0f;
        check(lika(r.ruta(r.a, c), {0, 0, h, 400}),
              "H1: AUTO — första rutan är halva ytan minus skiktet",
              rekt(r.ruta(r.a, c)));
        check(lika(r.ruta(r.b, c), {h + t, 0, h, 400}),
              "H1: AUTO — andra rutan börjar efter skiktet",
              rekt(r.ruta(r.b, c)));
        check(lika(r.ruta(r.c, c), {0, 0, 0, 0}),
              "H1: ett TREDJE barn placeras inte (tom rekt i hörnet)",
              rekt(r.ruta(r.c, c)));
    }

    // H2 — FÖRSTA rutan FAST: 200 px, den andra tar resten…
    // H3 — …och när delaren VÄXER står den fasta still.
    {
        Rigg r;
        r.split.setPaneSize(200);
        auto c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});
        check(lika(r.ruta(r.a, c), {0, 0, 200, 400}) &&
                  lika(r.ruta(r.b, c), {200 + t, 0, 400 - t, 400}),
              "H2: första rutan fast på 200",
              rekt(r.ruta(r.a, c)) + " " + rekt(r.ruta(r.b, c)));
        c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 800, 400});
        check(lika(r.ruta(r.a, c), {0, 0, 200, 400}) &&
                  lika(r.ruta(r.b, c), {200 + t, 0, 600 - t, 400}),
              "H3: delaren växer — den FASTA rutan behåller 200",
              rekt(r.ruta(r.a, c)) + " " + rekt(r.ruta(r.b, c)));
    }

    // H4 — ANDRA rutan FAST: `paneSize` mäter den rutan, inte avståndet från
    // vänster (WinForms SplitterDistance). Delaren växer, andra står still.
    {
        Rigg r;
        r.split.setFixedPane(glaze::Splitter::Pane::Second);
        r.split.setPaneSize(150);
        auto c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 800, 400});
        check(lika(r.ruta(r.b, c), {650, 0, 150, 400}) &&
                  lika(r.ruta(r.a, c), {0, 0, 650 - t, 400}),
              "H4: andra rutan fast på 150 — mätt på DEN rutan",
              rekt(r.ruta(r.a, c)) + " " + rekt(r.ruta(r.b, c)));
        c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 1000, 400});
        check(lika(r.ruta(r.b, c), {850, 0, 150, 400}),
              "H4: delaren växer — andra rutan står still på 150",
              rekt(r.ruta(r.b, c)));
    }

    // H5 — MINIMUM: den fasta får inte tränga den andra under dess minimum.
    {
        Rigg r;
        r.split.setPaneSize(750);
        r.split.setSecondMin(100);
        const auto c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 800, 400});
        check(lika(r.ruta(r.b, c), {700, 0, 100, 400}),
              "H5: andra rutan hålls på sitt minimum 100",
              rekt(r.ruta(r.b, c)));
    }

    // H6 — RYMS INTE BÅDA: den fasta rutans eget minimum vinner.
    {
        Rigg r;
        r.split.setPaneSize(100);
        r.split.setFirstMin(200);
        r.split.setSecondMin(200);
        const auto  c     = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 300, 400});
        const float avail = 300.0f - t;
        check(lika(r.ruta(r.a, c), {0, 0, 200, 400}) &&
                  lika(r.ruta(r.b, c), {200 + t, 0, avail - 200, 400}),
              "H6: båda minimimåtten ryms inte — den fasta rutans vinner",
              rekt(r.ruta(r.a, c)) + " " + rekt(r.ruta(r.b, c)));
    }

    // H7 — LIGGANDE skikt: rutorna över varandra.
    {
        Rigg r;
        r.split.setVertical(false);
        const auto  c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});
        const float h = (400.0f - t) / 2.0f;
        check(lika(r.ruta(r.a, c), {0, 0, 600, h}) &&
                  lika(r.ruta(r.b, c), {0, h + t, 600, h}),
              "H7: liggande skikt delar höjden",
              rekt(r.ruta(r.a, c)) + " " + rekt(r.ruta(r.b, c)));
    }

    // H8 — SKALAN 2: samma LOGISKA layout, dubbla fysiska px. Ett glömt
    // `* c.scale` i projektionen syns bara här.
    {
        Rigg r;
        r.split.setPaneSize(200);
        const auto c = ctxFör(st, 2.0f, gfx::Rectf{0, 0, 1200, 800});
        check(lika(r.ruta(r.a, c), {0, 0, 400, 800}) &&
                  lika(r.ruta(r.b, c), {400 + 2 * t, 0, 800 - 2 * t, 800}),
              "H8: skala 2 — 200 logiska px blir 400 fysiska",
              rekt(r.ruta(r.a, c)) + " " + rekt(r.ruta(r.b, c)));
    }

    // H9 — DOLD RUTA lämnar ett HÅL; skiktet står kvar (headern).
    {
        Rigg r;
        r.split.setPaneSize(200);
        r.a.hide();
        const auto c = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});
        check(lika(r.ruta(r.b, c), {200 + t, 0, 400 - t, 400}),
              "H9: en dold första ruta flyttar inte den andra",
              rekt(r.ruta(r.b, c)));
    }
}

void fallEgenskaper()
{
    glaze::Splitter s;
    check(s.isVertical() && s.fixedPane() == glaze::Splitter::Pane::First &&
              s.paneSize() == 0.0f && s.firstMin() == 0.0f &&
              s.secondMin() == 0.0f,
          "egenskaper: defaults — stående, första fast, AUTO, inga minimum");
    check(s.setProp("fixedPane", "second") &&
              s.fixedPane() == glaze::Splitter::Pane::Second,
          "egenskaper: setProp(fixedPane, second)");
    check(s.setProp("fixedPane", "mitten") &&
              s.fixedPane() == glaze::Splitter::Pane::Second,
          "egenskaper: okänd ruta loggas och IGNORERAS (nyckeln är känd)");
    check(s.setProp("paneSize", "220.5f") && s.paneSize() == 220.5f,
          "egenskaper: paneSize tar en C++-literal", std::to_string(s.paneSize()));
    check(s.setProp("paneSize", "12abc") && s.paneSize() == 220.5f,
          "egenskaper: skräp efter talet avvisas helt",
          std::to_string(s.paneSize()));
    check(!s.focusable(), "egenskaper: delaren är inget tabbstopp");
}

// ============================================================ MED FÖNSTER

// En widget med EXPLICIT capture: den fångar i första trycket och släpper i
// det andra släppet — loggen visar att `mouseUp` når den båda gångerna,
// också när pekaren står utanför den.
struct Fangare final : glaze::Widget {
    std::vector<std::string>* logg = nullptr;
    int                       upp  = 0;

    void draw(const glaze::Ctx&) override {}

protected:
    void mouseDown(const glaze::Ctx&, float, float) override
    {
        logg->push_back("F:ner");
        setPointerCapture();
    }
    bool mouseUp(const glaze::Ctx&, float, float) override
    {
        logg->push_back("F:upp");
        if (++upp == 2)
            releasePointerCapture();
        return false;
    }
};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    std::vector<std::string> logg;
    glaze::Splitter          split;
    glaze::Panel             vänster, höger;
    glaze::TextInput         fält;
    glaze::Link              länk;
    Fangare                  fångare;
    int                      ändringar = 0;

    Vard() : glaze::AppWindow("splitter_gate", 420, 300, gfx::Chrome::None)
    {
        split.name   = "split";
        split.x      = 10;
        split.y      = 10;
        split.width  = 300;
        split.height = 150;
        split.setPaneSize(100);
        split.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            ++ändringar;
            logg.push_back(
                "change:" +
                std::to_string(static_cast<int>(
                    static_cast<glaze::Splitter*>(s)->paneSize())));
        };
        root().adopt(split);
        split.adopt(vänster);
        split.adopt(höger);

        fält.x     = 10;
        fält.y     = 180;
        fält.width = 120;
        root().adopt(fält);

        länk.setText("länk");
        länk.x = 160;
        länk.y = 180;
        root().adopt(länk);

        fångare.logg   = &logg;
        fångare.x      = 330;
        fångare.y      = 10;
        fångare.width  = 60;
        fångare.height = 60;
        root().adopt(fångare);
    }

    gfx::Rectf rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = rekt(w);
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    // Skiktets mitt: mellan rutorna, mitt på höjden.
    gfx::Pointf skikt()
    {
        const gfx::Rectf a = rekt(vänster);
        const gfx::Rectf b = rekt(höger);
        return gfx::Pointf{(a.x + a.w + b.x) / 2.0f, a.y + a.h / 2.0f};
    }

    void mus(gfx::EventType t, float x, float y)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p.x, p.y); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p.x, p.y); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p.x, p.y); }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    gfx::PointerShape form() { return window().cursor(); }
};

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);

    // F1 (del 1) — FASADENS LAGRING: en form satt FÖRE show() ska gälla
    // efter. På Linux är fönstret en proxy, och en tappad vidarebefordran i
    // `init()` syns här som pilen ur det inre fönstret.
    w.window().setCursor(gfx::PointerShape::Hand);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const float s = w.window().uiScale();

        check(w.form() == gfx::PointerShape::Hand,
              "F1: formen satt FÖRE show() gäller efter (proxyns init)",
              formNamn(w.form()));
        w.window().setCursor(gfx::PointerShape::NotAllowed);
        check(w.form() == gfx::PointerShape::NotAllowed,
              "F1: formen satt EFTER show() går fram (proxyns sättare)",
              formNamn(w.form()));

        // ---- F2. PEKAREN och DRAGET.
        const gfx::Pointf S = w.skikt();
        const gfx::Pointf H = w.mitt(w.höger);
        w.flytta(S);
        check(w.form() == gfx::PointerShape::ResizeWE,
              "F2: ↔ över skiktet", formNamn(w.form()));
        w.flytta(H);
        check(w.form() == gfx::PointerShape::Arrow,
              "F2: pilen över en ruta", formNamn(w.form()));
        w.flytta(w.mitt(w.fält));
        check(w.form() == gfx::PointerShape::IBeam,
              "F2: I-balken över ett textfält", formNamn(w.form()));
        w.flytta(w.mitt(w.länk));
        check(w.form() == gfx::PointerShape::Hand,
              "F2: handen över en länk", formNamn(w.form()));

        w.logg.clear();
        const float före = w.rekt(w.vänster).w;
        w.ner(S);
        w.flytta({S.x + 40.0f * s, S.y});
        check(nära(w.rekt(w.vänster).w, före + 40.0f * s),
              "F2: rutan följer handen UNDER draget (förhandsvisning)",
              std::to_string(w.rekt(w.vänster).w));
        check(nära(w.split.paneSize(), 100.0f),
              "F2: paneSize() är det GAMLA läget tills släppet",
              std::to_string(w.split.paneSize()));
        // Pekaren lämnar delaren — ner över textfältet. Skiktet följer
        // handen i sidled, men pekaren står över FÄLTET, och trycket ska
        // ändå äga den: utan den regeln hade pekaren blivit en I-balk mitt
        // i ett drag. (Inom delaren går det inte att mäta — skiktet följer
        // ju pekaren och står alltid under den.)
        w.flytta(w.mitt(w.fält));
        check(w.form() == gfx::PointerShape::ResizeWE,
              "F2: ↔ står KVAR under draget fast pekaren står över ett fält",
              formNamn(w.form()));
        check(w.ändringar == 0, "F2: inget onChange före släppet");
        w.flytta({S.x + 40.0f * s, S.y});
        w.upp({S.x + 40.0f * s, S.y});
        check(w.ändringar == 1 && nära(w.split.paneSize(), 140.0f),
              "F2: släppet skriver läget och fyrar onChange EN gång",
              std::to_string(w.ändringar) + " / " +
                  std::to_string(w.split.paneSize()));
        check(!w.split.pressed(), "F2: trycket är släppt");

        // ---- F3. SLÄPP UTANFÖR FÖNSTRET — `mouseUp` kommer ändå.
        w.ändringar = 0;
        const gfx::Pointf S2 = w.skikt();
        w.ner(S2);
        w.flytta({S2.x - 30.0f * s, S2.y});
        w.upp({-500.0f, -500.0f});
        check(w.ändringar == 1 && nära(w.split.paneSize(), 110.0f),
              "F3: släppt UTANFÖR fönstret — onChange och nytt läge ändå",
              std::to_string(w.ändringar) + " / " +
                  std::to_string(w.split.paneSize()));

        // ---- F4. TRYCK UTAN RÖRELSE ändrar ingenting.
        w.ändringar = 0;
        w.ner(w.skikt());
        w.upp(w.skikt());
        check(w.ändringar == 0 && nära(w.split.paneSize(), 110.0f),
              "F4: tryck och släpp utan rörelse — inget onChange",
              std::to_string(w.ändringar));

        // ---- F5. FOKUSFÖRLUST mitt i draget: trycket slocknar, läget går
        // TILLBAKA och ett sent släpp gör ingenting.
        w.ändringar = 0;
        const gfx::Pointf S3   = w.skikt();
        const float       bred = w.rekt(w.vänster).w;
        w.ner(S3);
        w.flytta({S3.x + 50.0f * s, S3.y});
        check(nära(w.rekt(w.vänster).w, bred + 50.0f * s),
              "F5: förhandsvisningen följer handen",
              std::to_string(w.rekt(w.vänster).w));
        w.enkel(gfx::EventType::WindowFocusLost);
        check(!w.split.pressed(), "F5: fokusförlusten släcker trycket");
        check(nära(w.rekt(w.vänster).w, bred) &&
                  nära(w.split.paneSize(), 110.0f),
              "F5: läget går TILLBAKA när draget avbryts",
              std::to_string(w.rekt(w.vänster).w));
        w.upp({S3.x + 50.0f * s, S3.y});
        check(w.ändringar == 0, "F5: ett sent släpp gör ingenting",
              std::to_string(w.ändringar));
        w.enkel(gfx::EventType::WindowFocusGained);

        // ---- F6. EXPLICIT CAPTURE: `mouseUp` når den fångande widgeten,
        // också när släppet sker utanför den, och capture överlever det
        // första släppet.
        w.logg.clear();
        const gfx::Pointf F = w.mitt(w.fångare);
        w.ner(F);
        w.upp({F.x + 200.0f, F.y + 200.0f});
        w.ner(H);   // fångad: trycket går till fångaren, inte till rutan
        w.upp(H);
        const std::vector<std::string> väntat{"F:ner", "F:upp", "F:ner",
                                              "F:upp"};
        check(w.logg == väntat, "F6: mouseUp till den FÅNGANDE widgeten, "
                                "två gånger", std::to_string(w.logg.size()));

        // ---- F7. MOUSELEAVE ger pilen tillbaka.
        w.flytta(w.mitt(w.fält));
        w.enkel(gfx::EventType::MouseLeave);
        check(w.form() == gfx::PointerShape::Arrow,
              "F7: MouseLeave ger pilen", formNamn(w.form()));

        // ---- F8. INAKTIVERAD delare: inget ↔, och den går inte att dra.
        w.ändringar = 0;
        w.split.disable();
        const gfx::Pointf S4 = w.skikt();
        w.flytta(S4);
        check(w.form() == gfx::PointerShape::Arrow,
              "F8: inaktiverad delare — pilen, inget ↔", formNamn(w.form()));
        w.ner(S4);
        w.flytta({S4.x + 40.0f * s, S4.y});
        w.upp({S4.x + 40.0f * s, S4.y});
        check(w.ändringar == 0 && nära(w.split.paneSize(), 110.0f),
              "F8: inaktiverad delare — draget gör ingenting",
              std::to_string(w.split.paneSize()));

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

    std::printf("splitter_gate\n");
    fallLayout();
    fallEgenskaper();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
