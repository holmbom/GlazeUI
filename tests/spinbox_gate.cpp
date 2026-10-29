// spinbox_gate — TALFÄLTETS grind, och därmed
// KOMPOSITSTÖDETS första riktiga prov.
//
// Kör:  ./build/tests/spinbox_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt
//
// Grinden är TVÅDELAD (som dropdown_gate):
//
//   HEADLESS      VÄRDET (klampning + avrundning vid LÄSNING, settrarnas
//                 ordning spelar ingen roll, aldrig −0) · TOLKNINGEN av
//                 användarens text mot en tabell med förväntade värden · ANVÄNDARVÄGARNA
//                 (`stepFromUser`/`textFromUser` fyrar `onChange` och
//                 `onCommit` EXAKT när det effektiva värdet ändras; texten
//                 committas — den är inte LIVE — och skrivs om;
//                 ett steg utgår från en tolkbar skriven text; settrarna
//                 aldrig; hjulet UTAN fokus svarar false) · FORMATERINGEN
//                 följer språket när talet sätts, och RITNINGEN ändrar
//                 ingenting · MÄTNINGEN · att inredningen inte kan lossas.
//   MED FÖNSTER   ett DOLT fönster (`setOffscreen`) — fokus kräver en font,
//                 och fonten skapas i `show()`. Mäter de TRE HÅL talfältet
//                 visade i kompositstödet, stängda i Widget/AppWindow:
//                 ETT tabbstopp (`focusOrder`) · FOKUSOMBUDET (`focus()` på
//                 talfältet fokuserar FÄLTET) · Tab INIFRÅN fältet landar på
//                 nästa stopp efter talfältet (`nextFocus`) — plus att
//                 `disable()` når fältet och att fokusförlusten COMMITTAR
//                 texten (värdet sätts, texten skrivs om).
//                 Dessutom PILARNA och
//                 STEGARENS två halvor genom fönstrets riktiga ingång
//                 (`handleEvent`, protected — numpaden vikt),
//                 och SPRÅKBYTETS tre vägar in (`Widget::languageChanged`):
//                 ett öppet fönster, ett delträd som adopteras in, och ett
//                 fönster som visas efter bytet. I ett EGET fönster:
//                 HJULET KRÄVER FOKUS — för talfältet
//                 OCH reglaget (`Slider`), i samma rullande panel genom
//                 fönstrets riktiga ingång: utan fokus står värdet och
//                 panelen RULLAR (hjulet bubblade), med fokus stegar värdet
//                 och panelen står still; hjul upp är uppåt i värde; fokus
//                 på den ANDRA värdewidgeten räknas inte; hela steg, och
//                 pekplattans bråkdelar samlas. Reglaget har ingen egen
//                 grind — regeln är gemensam, så dess hjulfall bor här.
//
// SABOTAGEN hjulblocket fäller (prövade ett i taget):
//   * fokusvakten borttagen i `SpinBox::mouseWheel` (det headless
//     hjulfallet, H1, H2, H8)
//   * fokusvakten borttagen i `Slider::mouseWheel` (H0, H3, H8)
//   * talfältet som frågar SITT EGET `focused()` i stället för fältets —
//     kompositen är aldrig fokuserad, så hjulet slutar stega (H4, H5, H7, H9)
//   * en vakt i talfältet som frågar om FÖNSTRET har något fokuserat
//     (H1, H2, H8)
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   - backendernas egna tangent- och mus-events — grinden matar ingången
//     direkt (numpad_gate mäter översättningen på Windows och Linux).
//   - hur det SER UT (pixel_gate och ögat via glaze-render).
//   - reglagets övriga beteende (drag, tangenter, kvantisering) — bara
//     dess hjul mäts här.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

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

std::string tal(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

// De skyddade ANVÄNDARvägarna och hjulet — det en subklass får nå
// (som i listbox_gate).
struct Prov : glaze::SpinBox {
    using glaze::SpinBox::mouseWheel;
    using glaze::SpinBox::stepFromUser;
    using glaze::SpinBox::textFromUser;
};

// Det inre fältet — ett RIKTIGT barn, och `children()` är publikt. Att
// skriva i det med `setText` står för användarens tangenter (talfältet
// lyssnar inte på fältets onChange — att skriva är inte att
// spara), och att därefter anropa `textFromUser` är exakt vad Enter gör.
glaze::TextInput* fältet(glaze::SpinBox& s)
{
    return s.children().empty()
               ? nullptr
               : dynamic_cast<glaze::TextInput*>(s.children()[0]);
}

void skriv(Prov& s, const char* text)
{
    if (glaze::TextInput* f = fältet(s)) {
        f->setText(text);
        s.textFromUser();
    }
}

class FalskFont final : public gfx::NullFont {
public:
    FalskFont(float adv, float h) : adv_(adv), h_(h) {}
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv_ * static_cast<float>(s.size()), h_};
    }
    float height() const override { return h_; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string&, std::size_t bytes) const override
    {
        return adv_ * static_cast<float>(bytes);
    }
    std::size_t byteIndexAtX(const std::string& s, float) const override
    {
        return s.size();
    }
    std::size_t fitWidth(const std::string& s, float) const override
    {
        return s.size();
    }

private:
    float adv_;
    float h_;
};

// Renderer som inte ritar något — `draw()` ska bara kunna KÖRAS (synken
// sker i den), utan ett fönster. Fasadens no-op räcker rakt av.
using Tyst = gfx::NullRenderer;

glaze::Ctx ctxFör(gfx::Font& f, glaze::UI ui = glaze::UI::Slate)
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = 1.0f;
    c.ui          = ui;
    c.style       = &glaze::defaultStyle(ui);
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 400.0f, 300.0f};
    return c;
}

struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

// ============================================================== HEADLESS

void fallRegistret()
{
    auto w = glaze::makeWidget("spinbox");
    check(w && dynamic_cast<glaze::SpinBox*>(w.get()) != nullptr,
          "registret: \"spinbox\" ger en riktig glaze::SpinBox");
    glaze::SpinBox s;
    check(fältet(s) != nullptr, "fältet är ett RIKTIGT barn (en TextInput)");
    check(s.focusable() && s.focusesChildren(),
          "talfältet är ETT stopp (focusable + focusesChildren)");
    check(s.focusProxy() == fältet(s), "fokusombudet är fältet");
}

void fallVardet()
{
    glaze::setLanguage("sv");
    glaze::SpinBox s;
    check(s.value() == 0.0 && s.min() == 0.0 && s.max() == 100.0 &&
              s.step() == 1.0 && s.decimals() == 0,
          "default: 0 i [0, 100], steg 1, inga decimaler");

    // KLAMPNING VID LÄSNING — ordningen spelar ingen roll.
    s.setValue(150);
    check(s.value() == 100.0, "setValue(150) med max 100 läses som 100",
          tal(s.value()));
    s.setMax(200);
    check(s.value() == 150.0,
          "…och setMax(200) EFTERÅT ger 150 — ordningen spelar ingen roll",
          tal(s.value()));

    glaze::SpinBox t;
    t.setMax(200);
    t.setValue(150);
    check(t.value() == 150.0, "samma två satser i omvänd ordning: 150",
          tal(t.value()));

    // AVRUNDNING till decimals, också vid läsning.
    glaze::SpinBox d;
    d.setValue(3.14159);
    check(d.value() == 3.0, "decimals 0: 3.14159 läses som 3",
          tal(d.value()));
    d.setDecimals(2);
    check(std::fabs(d.value() - 3.14) < 1e-12, "decimals 2: 3.14",
          tal(d.value()));
    check(d.text() == "3,14", "texten följer: \"3,14\" (svenska)", d.text());

    // Aldrig −0, och NaN blir min.
    glaze::SpinBox z;
    z.setMin(-10);
    z.setDecimals(1);
    z.setValue(-0.04);
    check(z.text() == "0,0", "−0,04 med en decimal visas \"0,0\", aldrig "
          "\"-0,0\"", z.text());
    z.setValue(std::nan(""));
    check(z.value() == -10.0, "NaN läses som min", tal(z.value()));

    // min > max: intervallet är [min, min] (Sliderns regel).
    glaze::SpinBox v;
    v.setMin(50);
    v.setMax(10);
    v.setValue(70);
    check(v.value() == 50.0, "min > max ⇒ [min, min]", tal(v.value()));
}

void fallTolkning()
{
    glaze::setLanguage("sv");
    struct Fall {
        const char* text;
        bool        tolkas;
        double      värde;
    };
    const Fall tabell[] = {
        {"42", true, 42.0},       {"3,5", true, 3.5},
        {"3.5", true, 3.5},       {"  7 ", true, 7.0},
        {"-2", true, -2.0},       {"+1", true, 1.0},
        {",5", true, 0.5},        {"5,", true, 5.0},
        {"", false, 0.0},         {"-", false, 0.0},
        {",", false, 0.0},        {"1,000.5", false, 0.0},
        {"1e3", false, 0.0},      {"abc", false, 0.0},
        {"1 000", false, 0.0},    {"--1", false, 0.0},
    };
    for (const Fall& f : tabell) {
        Prov s;
        s.setMin(-100);
        s.setDecimals(2);
        s.setValue(11);   // ett värde som inte står i tabellen
        skriv(s, f.text);
        const bool   ändrad = s.value() != 11.0;
        const double väntat = f.tolkas ? f.värde : 11.0;
        check(std::fabs(s.value() - väntat) < 1e-9 && ändrad == f.tolkas,
              std::string("tolkning: \"") + f.text + "\" " +
                  (f.tolkas ? "tolkas" : "tolkas INTE (värdet står)"),
              tal(s.value()));
    }
}

void fallAnvandarvagarna()
{
    glaze::setLanguage("sv");
    Prov s;
    int  n = 0;   // onChange
    int  k = 0;   // onCommit — kommer ALLTID tillsammans med onChange (0.1.2)
    s.onChange = [&n] { ++n; };
    s.onCommit = [&k] { ++k; };

    // Programmatiskt: aldrig ett event.
    s.setValue(5);
    s.setMin(-5);
    s.setMax(10);
    s.setStep(2);
    s.setDecimals(1);
    check(n == 0 && k == 0, "settrarna fyrar aldrig onChange eller onCommit",
          std::to_string(n) + "/" + std::to_string(k));

    // Stegaren: ett steg, klampat, EN gång per faktisk ändring.
    s.stepFromUser(+1);
    check(s.value() == 7.0 && n == 1 && k == 1,
          "steg upp: 5 + 2 = 7, ett onChange och ett onCommit", tal(s.value()));
    s.stepFromUser(+5);
    check(s.value() == 10.0 && n == 2 && k == 2,
          "fem steg upp klampas till max 10", tal(s.value()));
    s.stepFromUser(+1);
    check(s.value() == 10.0 && n == 2 && k == 2,
          "steg upp VID max: ingen ändring, inget event", std::to_string(n));
    check(s.text() == "10,0", "stegaren skriver om texten", s.text());

    // Texten: COMMITTAD vid Enter (`textFromUser`), klampad, bara vid faktisk
    // ändring — och skriven om till värdets form.
    skriv(s, "3,5");
    check(s.value() == 3.5 && n == 3 && k == 3,
          "Enter på \"3,5\": värdet, ett onChange och ett onCommit",
          tal(s.value()));
    skriv(s, "3,50");
    check(n == 3 && k == 3 && s.text() == "3,5",
          "samma värde i annan form: inget event, texten i värdets form",
          s.text());
    skriv(s, "99");
    check(s.value() == 10.0 && n == 4 && k == 4 && s.text() == "10,0",
          "över max klampas till 10, och texten skrivs om", s.text());
    skriv(s, "-");
    check(s.value() == 10.0 && n == 4 && k == 4 && s.text() == "10,0",
          "en text som inte går att tolka (\"-\") lämnar värdet orört och "
          "skrivs tillbaka", s.text());

    // ATT SKRIVA UTAN ENTER ändrar ingenting — talfältet var LIVE före 0.1.2.
    glaze::TextInput* f = fältet(s);
    if (f) {
        f->setText("7");
        check(s.value() == 10.0 && n == 4 && k == 4,
              "en skriven text utan Enter ändrar inte värdet (inte LIVE)",
              tal(s.value()));
        s.stepFromUser(-1);
        check(s.value() == 5.0 && s.text() == "5,0" && n == 5 && k == 5,
              "ett steg utgår från det SKRIVNA talet (\"7\" − 2 = 5) och "
              "skriver om texten — NumericUpDown-modellen", s.text());
        f->setText("-");
        s.stepFromUser(-1);
        check(s.value() == 3.0 && s.text() == "3,0",
              "…och från VÄRDET när texten inte går att tolka (5 − 2 = 3)",
              s.text());
    }

    // step <= 0 ⇒ minsta visade steget.
    s.setStep(0);
    s.stepFromUser(+1);
    check(std::fabs(s.value() - 3.1) < 1e-9,
          "step 0 med en decimal: steget blir 0,1", tal(s.value()));

    // Hjulet UTAN FOKUS: kroken svarar false — det är svaret som
    // låter hjulet bubbla — och varken värdet eller onChange rörs. Ett
    // fristående talfält kan aldrig ha fokus; med fokus mäts hjulet i
    // fönsterblocket `fallHjulet`, och där bor också bråkdelens fall.
    Prov h;
    h.setValue(50);
    int        hn = 0;
    h.onChange    = [&hn] { ++hn; };
    glaze::Ctx c{};
    const bool tog = h.mouseWheel(c, 0.0f, 0.0f, 0.0f, 1.0f);
    check(!tog && h.value() == 50.0 && hn == 0,
          "hjul utan fokus: kroken svarar false, värdet står, inget onChange",
          tal(h.value()));
}

void fallSprak()
{
    // FORMATERINGEN följer språket när talet sätts. Att ett språkbyte når
    // ett tal som REDAN visas mäts med fönster (fall 7–9): det sker i
    // händelseflödet (`Widget::languageChanged`), och RITNINGEN ändrar
    // ingenting — det mäts här, med en tyst renderer. Synkades fältet i
    // `draw()` skulle det fristående talfältet nedan
    // byta tecken vid ritningen.
    FalskFont        f(7.0f, 13.0f);
    Tyst             r;
    glaze::Ctx       c = ctxFör(f);
    c.r                = &r;
    c.box              = gfx::Rectf{0.0f, 0.0f, 96.0f, 26.0f};

    glaze::setLanguage("sv");
    glaze::SpinBox s;
    s.setDecimals(2);
    s.setValue(3.14159);
    check(s.text() == "3,14", "svenska: \"3,14\"", s.text());

    glaze::setLanguage("en");
    glaze::SpinBox e;
    e.setDecimals(2);
    e.setValue(3.14159);
    check(e.text() == "3.14", "engelska: \"3.14\" (sätts efter bytet)",
          e.text());
    s.render(c);
    check(s.text() == "3,14",
          "RITNINGEN ändrar inget: ett FRISTÅENDE talfält behåller sin text "
          "efter ett byte (ingen synk i draw())", s.text());
    glaze::setLanguage("fi");
    glaze::SpinBox fi;
    fi.setDecimals(2);
    fi.setValue(3.14159);
    check(fi.text() == "3,14", "finska: \"3,14\"", fi.text());
    glaze::setLanguage("sv");
}

void fallMatning()
{
    FalskFont        f(7.0f, 13.0f);
    const glaze::Ctx c  = ctxFör(f);
    const auto&      st = glaze::defaultStyle(glaze::UI::Slate);

    glaze::SpinBox s;
    const gfx::Sizef m = Meter::measure(s, c);
    // Fältets auto-höjd: radhöjden (13) + 2 × fieldPadY (6) = 25.
    check(m.w == st.spinDefaultW && m.h == 13.0f + 2.0f * st.fieldPadY,
          "auto: bredden spinDefaultW, höjden FÄLTETS",
          tal(m.w) + "×" + tal(m.h));
    s.width = 150;
    check(Meter::measure(s, c).w == 150.0f,
          "satt bredd vinner, höjden är fortfarande auto");

    // Fältets yta: kompositen minus stegaren och luften.
    glaze::SpinBox t;
    t.width  = 120;
    t.height = 30;
    const gfx::Rectf a = static_cast<const glaze::Widget&>(t).childArea(c);
    check(a.x == 0.0f && a.w == 120.0f - st.spinStepW - st.spinGap &&
              a.h == 30.0f,
          "fältets yta: hela höjden, bredden minus stegaren och luften",
          tal(a.w));
}

void fallInredningen()
{
    glaze::SpinBox s;
    glaze::Widget* f = fältet(s);
    check(f && !s.canReleaseChild(*f), "canReleaseChild nekar fältet");
    check(f && !s.unadopt(*f), "unadopt(fältet) nekas");
    check(f && !s.releaseOwned(*f), "releaseOwned(fältet) nekas");
    check(fältet(s) == f, "fältet sitter kvar efter de nekade försöken");

    // `disable()` når fältet — `enabled` kaskadar inte annars.
    s.disable();
    check(f && !f->isEnabled(), "disable() släcker fältet");
    s.enable();
    check(f && f->isEnabled(), "enable() tänder det igen");
}

// ============================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    // Fönstrets RIKTIGA ingång (protected) — pilarna och
    // stegaren mäts genom routingen, inte genom att anropa krokarna.
    using glaze::AppWindow::handleEvent;

    glaze::Button  före{"före"};
    Prov           spin;
    glaze::Button  efter{"efter"};
    Prov           lös;   // fall 8: står UTANFÖR trädet under ett språkbyte
    int            ändringar = 0;
    Vard() : glaze::AppWindow("spinbox_gate", 400, 200)
    {
        före.x  = 10;
        före.y  = 10;
        spin.x  = 10;
        spin.y  = 50;
        efter.x = 10;
        efter.y = 90;
        lös.x   = 200;
        lös.y   = 50;
        spin.onChange = [this] { ++ändringar; };
        root().adopt(före);
        root().adopt(spin);
        root().adopt(efter);
    }

    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    void klick(float x, float y)
    {
        gfx::Event e;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        e.type     = gfx::EventType::MouseButtonDown;
        handleEvent(e);
        e.type = gfx::EventType::MouseButtonUp;
        handleEvent(e);
    }
};

// Ett fönster som INTE är öppet när språket byts (fall 9).
struct Stangd : glaze::AppWindow {
    Prov spin;
    Stangd() : glaze::AppWindow("spinbox_gate, stängt", 200, 100)
    {
        spin.x = 10;
        spin.y = 10;
        root().adopt(spin);
    }
};

void fallMedFonster()
{
    glaze::setLanguage("sv");
    Vard w;
    w.setOffscreen(true);
    Stangd w2;
    w2.setOffscreen(true);
    w2.spin.setDecimals(1);
    w2.spin.setValue(2.5);

    bool kedjanKordes = false;
    glaze::app().post([&] {
        kedjanKordes = true;
        if (!w.isOpen()) {
            std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                        "aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }
        glaze::TextInput* f = fältet(w.spin);

        // 1. ETT tabbstopp — och fältet står inte själv i listan.
        const auto ordning = w.focusOrder();
        check(ordning.size() == 3 && ordning[1] == &w.spin,
              "Tab-listan: före · talfält · efter (fältet är inredning)",
              std::to_string(ordning.size()));

        // 2. FOKUSOMBUDET: focus() på talfältet fokuserar FÄLTET.
        w.spin.focus();
        check(f && f->focused(), "focus() på talfältet ⇒ FÄLTET har fokus");
        check(!w.spin.focused(), "…och talfältet självt står inte som fokuserat");

        // 3. Tab INIFRÅN fältet: nästa stopp efter talfältet, inte det första.
        check(w.nextFocus(+1) == &w.efter,
              "Tab inifrån fältet ⇒ stoppet EFTER talfältet");
        check(w.nextFocus(-1) == &w.före,
              "Shift+Tab inifrån fältet ⇒ stoppet FÖRE talfältet");

        // 4. Under redigering är texten ANVÄNDARENS — ritningen rör den inte,
        //    och värdet är det förra; fokusförlusten COMMITTAR den (0.1.2).
        if (f) {
            const double förr = w.spin.value();
            f->setText("12,");
            w.window().renderFrame();
            check(w.spin.value() == förr && f->text() == "12,",
                  "under redigering står användarens text kvar (\"12,\") och "
                  "värdet är det förra", f->text());
            w.efter.focus();
            check(w.spin.value() == 12.0 && f->text() == "12",
                  "fokusförlusten committar: värdet 12, texten omskriven "
                  "(\"12\")", f->text());
        }

        // 5. disable() når fältet — och talfältet lämnar Tab-listan.
        w.spin.disable();
        check(f && !f->isEnabled(), "disable() på talfältet släcker fältet");
        check(w.focusOrder().size() == 2,
              "inaktiverat talfält är inget stopp — och fältet blir inget "
              "heller", std::to_string(w.focusOrder().size()));
        w.spin.enable();

        // 6. PILARNA OCH STEGAREN genom fönstrets riktiga ingång. Pilarna
        //    går till den FOKUSERADE widgeten — fältet, via fokusombudet —
        //    och numpadens navigationsläge kommer VIKT.
        w.spin.setValue(50);
        w.spin.focus();
        const int n0 = w.ändringar;
        w.tangent(gfx::Key::Up);
        check(w.spin.value() == 51.0 && w.ändringar == n0 + 1,
              "6: Up i fältet stegar upp, ett onChange", tal(w.spin.value()));
        w.tangent(gfx::Key::KP_Down);
        check(w.spin.value() == 50.0 && w.ändringar == n0 + 2,
              "6: numpadens Down (NumLock av) stegar ner — vikt av AppWindow",
              tal(w.spin.value()));
        // Stegarens HALVOR: plattan är den högra `spinStepW`, övre halvan
        // upp och nedre ner. Punkterna ligger mitt i varje halva.
        const gfx::Rectf r     = w.spin.resolve(w.spin.behaviorCtx());
        const float      stegW = w.style().spinStepW * w.window().uiScale();
        const float      sx    = r.x + r.w - stegW / 2.0f;
        w.klick(sx, r.y + r.h / 4.0f);
        check(w.spin.value() == 51.0 && w.ändringar == n0 + 3,
              "6: klick på stegarens övre halva stegar upp",
              tal(w.spin.value()));
        w.klick(sx, r.y + r.h * 3.0f / 4.0f);
        check(w.spin.value() == 50.0 && w.ändringar == n0 + 4,
              "6: klick på nedre halvan stegar ner", tal(w.spin.value()));
        check(f && f->focused(),
              "6: efter klicken på stegaren har FÄLTET fortfarande fokus");

        // 7. SPRÅKBYTET med fönstret ÖPPET: ett vilande fält skrivs om
        //    DIREKT, i händelseflödet — ingen frame behövs. Ett fält som
        //    REDIGERAS behåller användarens text.
        w.efter.focus();
        w.spin.setDecimals(2);
        w.spin.setValue(3.14159);
        check(w.spin.text() == "3,14", "7: svenska \"3,14\"", w.spin.text());
        glaze::setLanguage("en");
        check(w.spin.text() == "3.14",
              "7: setLanguage(\"en\") skriver om ett VILANDE fält direkt",
              w.spin.text());
        if (f) {
            w.spin.focus();
            f->setText("12.");   // användaren skriver — inget committat än
            glaze::setLanguage("sv");
            check(f->text() == "12.",
                  "7: ett fält som REDIGERAS behåller användarens text",
                  f->text());
            w.efter.focus();
            check(f->text() == "12,00",
                  "7: …och fokusförlusten committar den i det nya språket",
                  f->text());
        }

        // 8. ADOPTIONEN: ett talfält som stod utanför trädet under bytet
        //    (en flik som lossats) får kroken när det kommer in igen.
        w.lös.setDecimals(1);
        w.lös.setValue(2.5);
        check(w.lös.text() == "2,5", "8: utanför trädet, svenska \"2,5\"",
              w.lös.text());
        glaze::setLanguage("en");
        check(w.lös.text() == "2,5",
              "8: ett byte når inte ett talfält UTANFÖR trädet",
              w.lös.text());
        w.root().adopt(w.lös);
        check(w.lös.text() == "2.5",
              "8: …men adoptionen in i det öppna fönstret gör det",
              w.lös.text());

        // 9. ETT STÄNGT FÖNSTER: bytet ovan nådde det inte, visningen gör.
        check(w2.spin.text() == "2,5",
              "9: talfältet i det STÄNGDA fönstret står kvar på svenska",
              w2.spin.text());
        w2.show();
        check(w2.isOpen() && w2.spin.text() == "2.5",
              "9: …och får språket när fönstret visas", w2.spin.text());
        // Båda stängningarna är uppskjutna och verkställs i samma pumpsvans.
        w2.close();
        glaze::setLanguage("sv");
        w.close();
    });
    w.run();

    if (!kedjanKordes) {
        std::printf("  --   fönsterfallen kördes ALDRIG (pumpen startade "
                    "inte)\n");
        ++g_skips;
    }
}

// ============================================================ HJULET
//
// HJULET KRÄVER FOKUS — för BÅDA värdewidgetarna, mätt i
// SAMMA rullande panel genom fönstrets riktiga ingång (`handleEvent` med ett
// MouseWheel): utan fokus står värdet, inget onChange fyras och panelen
// RULLAR — hjulet bubblade förbi värdewidgeten; med fokus stegar värdet och
// panelen står STILL. Reglagets fall bor här och inte i en egen grind: regeln
// är gemensam, och samma panel visar att de två är överens.
//
// Varje fall vrider NEDÅT från panelens överkant. Där hade panelen rullat om
// hjulet nått den, så "panelen står still" mäter något — uppåt hade den
// stått still ändå, för överkanten är redan nådd.

struct ProvReglage : glaze::Slider {
    using glaze::Slider::mouseWheel;
};

struct Rull : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Panel  sida;
    glaze::Button knapp{"knapp"};     // fokus NÅGON ANNANSTANS
    Prov          spin;
    glaze::Slider reglage;
    glaze::Button botten{"botten"};   // långt ner: panelen har något att rulla
    int           spinÄndringar    = 0;
    int           reglageÄndringar = 0;

    Rull() : glaze::AppWindow("spinbox_gate, hjulet", 400, 200)
    {
        sida.dock = glaze::Dock::Fill;
        knapp.x   = 10;
        knapp.y   = 10;
        spin.x    = 10;
        spin.y    = 50;
        reglage.x = 10;
        reglage.y = 90;
        botten.x  = 10;
        botten.y  = 600;
        spin.onChange    = [this] { ++spinÄndringar; };
        reglage.onChange = [this] { ++reglageÄndringar; };
        root().adopt(sida);
        sida.adopt(knapp);
        sida.adopt(spin);
        sida.adopt(reglage);
        sida.adopt(botten);
    }

    // En punkt `fx` av bredden in i widgeten, mitt på höjden (FYSISKA px) —
    // räknad om vid varje fråga, för panelen kan ha rullat.
    gfx::Pointf punkt(glaze::Widget& w, float fx)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w * fx, r.y + r.h / 2.0f};
    }
    // Mitt på talfältets STEGARE — den högra `spinStepW`.
    gfx::Pointf stegaren()
    {
        const gfx::Rectf r = spin.resolve(spin.behaviorCtx());
        const float      s = style().spinStepW * window().uiScale();
        return gfx::Pointf{r.x + r.w - s / 2.0f, r.y + r.h / 2.0f};
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

void fallHjulet()
{
    // HEADLESS först — mäts även när fönstret inte kommer upp. Ett
    // fristående reglage har aldrig fokus, och då svarar kroken false.
    {
        ProvReglage r;
        r.setValue(50);
        int n      = 0;
        r.onChange = [&n] { ++n; };
        glaze::Ctx c{};
        const bool tog = r.mouseWheel(c, 0.0f, 0.0f, 0.0f, 1.0f);
        check(!tog && r.value() == 50.0f && n == 0,
              "H0: reglaget utan fokus — kroken svarar false, värdet står",
              tal(r.value()));
    }

    glaze::setLanguage("sv");
    Rull w;
    w.setOffscreen(true);
    bool kedjan = false;
    glaze::app().post([&] {
        kedjan = true;
        if (!w.isOpen()) {
            std::printf("  --   hjulfallen hoppas över: fönstret kom aldrig "
                        "upp\n");
            ++g_skips;
            w.close();
            return;
        }

        // ETT FALL: utgångsläget (panelen i överkanten, båda värdena 50,
        // räknarna nollade), fokus på `fokus`, ett hjul över punkten `var`.
        auto vrid = [&](glaze::Widget& fokus,
                        const std::function<gfx::Pointf()>& var, float rader) {
            w.sida.setScroll(0.0f, 0.0f);
            w.spin.setValue(50);
            w.reglage.setValue(50);
            w.spinÄndringar    = 0;
            w.reglageÄndringar = 0;
            fokus.focus();
            w.hjul(var(), rader);
        };
        const auto fältet  = [&] { return w.punkt(w.spin, 0.25f); };
        const auto stegare = [&] { return w.stegaren(); };
        const auto reglage = [&] { return w.punkt(w.reglage, 0.5f); };
        const auto läge    = [&] {
            return tal(w.spin.value()) + " / " + tal(w.reglage.value()) +
                   ", rullat " + tal(w.sida.scrollY());
        };

        // H1–H3. UTAN FOKUS (det står på knappen): värdet står, inget
        // onChange, och PANELEN RULLAR.
        vrid(w.knapp, fältet, -1.0f);
        check(w.spin.value() == 50.0 && w.spinÄndringar == 0 &&
                  w.sida.scrollY() > 0.0f,
              "H1: talfältet utan fokus, hjul över FÄLTET — värdet står, "
              "panelen rullar",
              läge());
        vrid(w.knapp, stegare, -1.0f);
        check(w.spin.value() == 50.0 && w.spinÄndringar == 0 &&
                  w.sida.scrollY() > 0.0f,
              "H2: talfältet utan fokus, hjul över STEGAREN — värdet står, "
              "panelen rullar",
              läge());
        vrid(w.knapp, reglage, -1.0f);
        check(w.reglage.value() == 50.0f && w.reglageÄndringar == 0 &&
                  w.sida.scrollY() > 0.0f,
              "H3: reglaget utan fokus — värdet står, panelen rullar", läge());

        // H4–H6. MED FOKUS: ett steg ner, ett onChange, panelen står still.
        vrid(w.spin, fältet, -1.0f);
        check(w.spin.value() == 49.0 && w.spinÄndringar == 1 &&
                  w.sida.scrollY() == 0.0f,
              "H4: talfältet med fokus, hjul över fältet — ett steg ner, "
              "panelen står still",
              läge());
        vrid(w.spin, stegare, -1.0f);
        check(w.spin.value() == 49.0 && w.spinÄndringar == 1 &&
                  w.sida.scrollY() == 0.0f,
              "H5: talfältet med fokus, hjul över stegaren — ett steg ner",
              läge());
        vrid(w.reglage, reglage, -1.0f);
        check(w.reglage.value() == 49.0f && w.reglageÄndringar == 1 &&
                  w.sida.scrollY() == 0.0f,
              "H6: reglaget med fokus — ett steg ner, panelen står still",
              läge());

        // H7. RIKTNINGEN: hjul UPP är uppåt i värde, för båda.
        vrid(w.spin, fältet, 1.0f);
        const double spinUpp = w.spin.value();
        vrid(w.reglage, reglage, 1.0f);
        check(spinUpp == 51.0 && w.reglage.value() == 51.0f,
              "H7: hjul upp = uppåt i värde, i båda", läge());

        // H8. Det är widgetens EGET fokus som krävs, inte fönstrets: fokus
        // på den ANDRA värdewidgeten räknas inte.
        vrid(w.reglage, fältet, -1.0f);
        check(w.spin.value() == 50.0 && w.spinÄndringar == 0 &&
                  w.sida.scrollY() > 0.0f,
              "H8: fokus på reglaget, hjul över talfältet — talfältet står, "
              "panelen rullar",
              läge());
        vrid(w.spin, reglage, -1.0f);
        check(w.reglage.value() == 50.0f && w.reglageÄndringar == 0 &&
                  w.sida.scrollY() > 0.0f,
              "H8: fokus i talfältet, hjul över reglaget — reglaget står, "
              "panelen rullar",
              läge());

        // H9. HELA STEG med fokus: ett hjul på −3 är tre steg och ETT
        // onChange; pekplattans bråkdelar samlas tills de blivit ett steg.
        vrid(w.spin, fältet, -3.0f);
        check(w.spin.value() == 47.0 && w.spinÄndringar == 1,
              "H9: hjul −3 — tre hela steg ner, ett onChange", läge());
        vrid(w.spin, fältet, 0.4f);
        w.hjul(fältet(), 0.4f);
        check(w.spin.value() == 50.0 && w.spinÄndringar == 0,
              "H9: hjul 0,4 + 0,4 — inget helt steg än, inget onChange",
              läge());
        w.hjul(fältet(), 0.4f);
        check(w.spin.value() == 51.0 && w.spinÄndringar == 1,
              "H9: …0,4 till — ett helt steg", läge());
        // Bråkdelen som blev över (0,2) följer med: −3 blir −2,8 ⇒ två steg.
        w.hjul(fältet(), -3.0f);
        check(w.spin.value() == 49.0,
              "H9: hjul −3 med 0,2 kvar — två hela steg ner", läge());

        w.close();
    });
    w.run();

    if (!kedjan) {
        std::printf("  --   hjulfallen kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("spinbox_gate — talfältet och kompositstödet\n");

    fallRegistret();
    fallVardet();
    fallTolkning();
    fallAnvandarvagarna();
    fallSprak();
    fallMatning();
    fallInredningen();
    fallMedFonster();
    fallHjulet();

    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "SPINBOX_GATE GRÖN" : "SPINBOX_GATE FÄLLD",
                g_cases, g_fails, g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
