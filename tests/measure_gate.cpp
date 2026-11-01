// measure_gate — MÄTVÄGENS ENHETSGRIND.
//
// Kör:  ./build/tests/measure_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// VAD DEN BEVISAR, exakt och inte mer: att `Widget::contentSize()` svarar i
// samma ENHET för alla typer som ligger i widget-REGISTRET. Den mäter
// DIREKT på `contentSize` via den statiska `Widget::measure`, alltså inte
// via `resolve()` — och det är hela poängen. `resolve`-utdata är ∝ skala
// både före och efter enhetsbytet, så `layout_gate` kan omöjligt skilja
// enheterna åt; den grinden fångar HALVKONVERTERING (en glömd widget ger
// `L·scale²`), den här fångar ENHETEN.
//
// AVSNITT 3 gör samma sak för `Widget::heightForWidth`, höjden
// för en bredd layouten ger: logiska px in och ut, samma svar i båda
// skalorna. SABOTAGEN den fäller: `× c.scale` i Labels krok, och en krok som
// alltid svarar `nullopt` (då svarar ingen typ). Radräkningen mäter den INTE
// — fonten här bryter aldrig; det gör layout_gate fall 20.
//
// AVSNITT 4 mäter MÄTCACHEN (`Widget::measure`/`measureHeightForWidth`): en
// räknande widget visar att ett oförändrat delträd mäts EN gång, och att varje
// del av nyckeln — generationen, miljön, de råa layoutfälten och handler-epoken
// — ger en ny mätning när den ändras. SABOTAGENA den fäller: en `measure` som
// anropar `contentSize` rakt av (K1–K10 räknar fel), en nyckel utan `env` (K6),
// utan `layoutHash` (K4), utan `epoch` (K5), en `invalidate()` som inte stegar
// förfäderna eller som stegar syskonens delträd (K2), ett cachat svar som
// glömmer `nullopt` (K3), och en adoption eller lossning som inte stegar
// kedjan (K8, K9).
//
// VAD DEN INTE TÄCKER — skrivet så anspråket inte överdrivs:
//   - TU-lokala implementationer registrerar sig inte: `DropdownRows`
//     (dropdown.cpp) och `MenuRows` (menu.cpp) returnerar båda `{0,0}` och
//     är statiskt granskade undantag; kontraktet är att de FÖRBLIR `{0,0}`.
//     Popupgeometri (dropdown_gate/menu_gate) är INTE täckning för dem —
//     popupstorleken räknas förbi deras `contentSize`.
//   - `StatusBadge` (message_box.cpp) mäts av `msgbox_gate` via den interna
//     `status_badge.h`.
//   - `StandInWidget` (GlazeBuilder) mäts av `preview_gate`.
//
// ANTALSKRAVET är en del av grinden: länkas `glaze_impl` direkt i stället
// för targetet `glaze` strippas widgetarnas självregistreringar, registret
// blir tomt och loopen hade varit "grön" på noll typer.

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void ok(bool cond, const std::string& vad)
{
    ++g_fall;
    if (cond) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s\n", vad.c_str());
    ++g_fel;
}

bool nära(float a, float b) { return (a > b ? a - b : b - a) < 0.01f; }

// ------------------------------------------------------------ falsk font
//
// Fast teckensteg per kodpunkt (samma som i layout_gate/msgbox_gate).
// Fonten talar FYSISKA px — den skapas per skala med `adv × scale` precis
// som `AppWindow::reloadFont()` gör. Det är det som gör grinden ärlig: en
// widget som multiplicerar sitt fontmått med skalan EN gång för mycket
// syns bara om fonten faktiskt växer med skalan.
class FalskFont final : public gfx::NullFont {
public:
    FalskFont(float adv, float h) : adv_(adv), h_(h) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv_ * static_cast<float>(kodpunkter(s)), h_};
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
    static std::size_t kodpunkter(const std::string& s)
    {
        std::size_t n = 0;
        for (char ch : s)
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
                ++n;
        return n;
    }
    float adv_;
    float h_;
};

// MÄTAREN. `contentSize` är protected — rätt för API:t, opraktiskt för en
// grind. `Widget::measure` är en statisk protected basmedlem som tar en
// godtycklig `const Widget&`, så en `using`-deklaration i en subklass av
// BASEN når varje widget, oavsett om typen är `final` (`StatusBadge` är
// det, så `MättLabel`-mönstret med arv från widgetklassen bär inte här).
// Meter instansieras aldrig — `draw()` förblir ren virtuell.
struct Meter : glaze::Widget {
    using glaze::Widget::measure;
    using glaze::Widget::measureHeightForWidth;
};

// Ctx utan renderer. Varken `contentSize` eller `Draw::measure` rör `c.r`
// eller `c.box` — mätvägen körs INNE i `resolve()` och får per kontrakt
// inte läsa boxen. `c.images` är null, så `Draw::imageSize` svarar {0,0}.
glaze::Ctx ctxFör(const glaze::Style& st, gfx::Font& f, float scale)
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = scale;
    c.ui          = glaze::UI::Slate;
    c.style       = &st;
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 600.0f * scale, 400.0f * scale};
    return c;
}

// ------------------------------------------------------- enhetskontrollen
//
// KONTRAKTET SOM MÄTS STÅR HÄR, på ETT ställe.
//
// `contentSize()` svarar i LOGISKA px ⇒ svaret är SKALOBEROENDE.
//
// Grinden mäter ENHETEN och inte någon egenskap som råkar hålla: svaret ska
// vara DETSAMMA i skala 1 och 2. Ett halvkonverterat läge kompilerar och
// ljuger.
void krävEnhet(const gfx::Sizef& s1, const gfx::Sizef& s2,
               const std::string& vad)
{
    ok(nära(s2.w, s1.w) && nära(s2.h, s1.h),
       vad + " (skala 1: " + std::to_string(s1.w) + "×" +
           std::to_string(s1.h) + " · skala 2: " + std::to_string(s2.w) +
           "×" + std::to_string(s2.h) + ")");
}

// Mäter EN registrerad typ i EN uppsättning egenskaper, vid skala 1 och 2.
gfx::Sizef mät(const std::string& typ, float scale, bool explicitMått)
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f * scale, st.uiFontSize * scale);
    const glaze::Ctx    c = ctxFör(st, f, scale);

    std::unique_ptr<glaze::Widget> w = glaze::makeWidget(typ);
    if (!w)
        return gfx::Sizef{-1.0f, -1.0f};
    if (explicitMått) {
        // PASSTHROUGH-VÄGEN: satt storlek vinner i varje implementation, så
        // den här uppsättningen når de containers vars auto-svar är {0,0}
        // (Panel, ListBox, PropertyGrid) — utan den vore de fallen tomma i
        // båda enheterna och grinden hade "bevisat" något om dem den inte
        // mätt.
        w->width  = 120.0f;
        w->height = 44.0f;
    } else {
        // AUTO-VÄGEN: texten når fontmätningen i Label/Button/Checkbox/
        // Radio/Link, och osatta mått låter stilmåtten falla ut.
        w->setProp("text", "Mät mig");
    }
    return Meter::measure(*w, c);
}

// ------------------------------------------------------- 1. registerloopen

void registerLoopen()
{
    std::printf("\n=== 1. registrets alla typer — enheten ===\n");

    const std::vector<std::string> typer = glaze::registeredWidgetTypes();

    // WHOLE_ARCHIVE-vakten: en tom eller nästan tom lista betyder att
    // registreringarna strippats i länken, och då mäter loopen ingenting.
    ok(typer.size() >= 19,
       "registret bär minst 19 typer (fick " + std::to_string(typer.size()) +
           ")");
    if (g_verbose)
        for (const std::string& t : typer)
            std::printf("       registrerad: %s\n", t.c_str());

    for (const std::string& t : typer) {
        const gfx::Sizef auto1 = mät(t, 1.0f, false);
        const gfx::Sizef auto2 = mät(t, 2.0f, false);
        ok(auto1.w >= 0.0f, t + ": makeWidget gav en widget");
        krävEnhet(auto1, auto2, t + ": AUTO-måttet är skaloberoende");

        const gfx::Sizef fast1 = mät(t, 1.0f, true);
        const gfx::Sizef fast2 = mät(t, 2.0f, true);
        krävEnhet(fast1, fast2, t + ": SATT mått är skaloberoende");
    }
}

// ------------------------------------------------- 2. basens egen default
//
// `Spacer` använder basens implementation och `TopBar` använder `Bar`s —
// de 21 registrerade typerna är alltså inte 21 implementationer. Basen
// mäts ändå direkt här, för den är den mall varje EGEN widget ärver när
// den inte överrider något alls.

void basensDefault()
{
    std::printf("\n=== 2. Widget::contentSize (basens default) ===\n");

    struct Naken final : glaze::Widget {
        void draw(const glaze::Ctx&) override {}
    };

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    Naken               n;
    n.width  = 90.0f;
    n.height = 30.0f;

    gfx::Sizef s[2];
    for (int i = 0; i < 2; ++i) {
        const float scale = i == 0 ? 1.0f : 2.0f;
        FalskFont   f(7.0f * scale, st.uiFontSize * scale);
        s[i] = Meter::measure(n, ctxFör(st, f, scale));
    }
    ok(nära(s[0].w, 90.0f) && nära(s[0].h, 30.0f),
       "basen svarar med de LOGISKA fälten rakt av");
    krävEnhet(s[0], s[1], "basen: skaloberoende");
}

// ------------------------------------------- 3. höjd för bredd
//
// `Widget::heightForWidth` talar samma enhet som `contentSize`: LOGISKA px in
// (bredden) och ut (höjden), alltså SAMMA svar i skala 1 och 2 — eller
// `nullopt` i båda. Varje registrerad typ får en text och `wrap` (de typer
// som inte känner nyckeln ignorerar den), och minst en typ — etiketten —
// måste SVARA, annars mäter loopen ingenting. Fonten här bryter aldrig, så
// svaret är en radhöjd: det är ENHETEN som mäts, radräkningen mäter
// layout_gate fall 20.
void hojdForBredd()
{
    std::printf("\n=== 3. heightForWidth — enheten ===\n");

    const glaze::Style& st     = glaze::defaultStyle(glaze::UI::Slate);
    int                 svarar = 0;
    for (const std::string& t : glaze::registeredWidgetTypes()) {
        std::optional<float> svar[2];
        for (int i = 0; i < 2; ++i) {
            const float scale = i == 0 ? 1.0f : 2.0f;
            FalskFont   f(7.0f * scale, st.uiFontSize * scale);
            std::unique_ptr<glaze::Widget> w = glaze::makeWidget(t);
            if (!w)
                break;
            w->setProp("text", "Mät mig, gärna på flera rader");
            w->setProp("wrap", "true");
            svar[i] = Meter::measureHeightForWidth(*w, ctxFör(st, f, scale),
                                                   160.0f);
        }
        if (svar[0].has_value() != svar[1].has_value()) {
            ok(false, t + ": heightForWidth svarar i en skala men inte i den "
                          "andra");
            continue;
        }
        if (!svar[0])
            continue;
        ++svarar;
        ok(nära(*svar[0], *svar[1]),
           t + ": heightForWidth är skaloberoende (skala 1: " +
               std::to_string(*svar[0]) + " · skala 2: " +
               std::to_string(*svar[1]) + ")");
    }
    ok(svarar >= 1, "minst en registrerad typ svarar på heightForWidth (fick " +
                        std::to_string(svarar) + ")");

    // Etiketten SJÄLV: svarar med wrap, tiger utan, och tiger med en satt
    // höjd (axelregeln — den satta vinner).
    FalskFont f(7.0f, st.uiFontSize);
    const glaze::Ctx c = ctxFör(st, f, 1.0f);
    glaze::Label l("Mät mig");
    ok(!Meter::measureHeightForWidth(l, c, 160.0f),
       "label utan wrap: ingen höjd för bredd");
    l.setWrap(true);
    ok(Meter::measureHeightForWidth(l, c, 160.0f).has_value(),
       "label med wrap: svarar");
    l.height = 40.0f;
    ok(!Meter::measureHeightForWidth(l, c, 160.0f),
       "label med satt height: ingen höjd för bredd (den satta vinner)");
}

// ------------------------------------------------------- 4. mätcachen
//
// RÄKNANDE WIDGETS. `Räknande` är ett löv med ett fast mått och en räknare per
// fråga; `Behållare` är en komposit vars mått är barnens utsträckning
// (`childrenExtent`, den form en komposit-författare skriver). Räknarna är
// FACIT: en cache som fungerar syns som ETT anrop per nyckel.
struct Räknande final : glaze::Widget {
    Räknande(float w, float h) : bw(w), bh(h) {}
    void draw(const glaze::Ctx&) override {}

    float                bw, bh;
    std::optional<float> hojd;   // svaret på heightForWidth; nullopt = tiger
    mutable int          mätningar  = 0;
    mutable int          höjdfrågor = 0;

protected:
    gfx::Sizef contentSize(const glaze::Ctx&) const override
    {
        ++mätningar;
        return gfx::Sizef{bw, bh};
    }
    std::optional<float> heightForWidth(const glaze::Ctx&,
                                        float) const override
    {
        ++höjdfrågor;
        return hojd;
    }
};

struct Behållare final : glaze::Widget {
    void        draw(const glaze::Ctx&) override {}
    mutable int mätningar = 0;

protected:
    gfx::Sizef contentSize(const glaze::Ctx& c) const override
    {
        ++mätningar;
        return childrenExtent(c);
    }
};

void mätcachen()
{
    std::printf("\n=== 4. mätcachen ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    const glaze::Ctx    c = ctxFör(st, f, 1.0f);

    // ---- K1. Ett oförändrat widget mäts EN gång, per nyckel.
    {
        Räknande r(30.0f, 20.0f);
        for (int i = 0; i < 3; ++i) {
            const gfx::Sizef s = Meter::measure(r, c);
            ok(nära(s.w, 30.0f) && nära(s.h, 20.0f),
               "K1: cachat svar är samma svar (varv " + std::to_string(i) + ")");
        }
        ok(r.mätningar == 1,
           "K1: tre frågor ger EN contentSize (fick " +
               std::to_string(r.mätningar) + ")");

        // Ytan är INTE med i nyckeln — kontraktet för contentSize är att den
        // inte läser den.
        glaze::Ctx annan = c;
        annan.area       = gfx::Rectf{0.0f, 0.0f, 50.0f, 50.0f};
        Meter::measure(r, annan);
        ok(r.mätningar == 1, "K1: en annan c.area är samma nyckel");
    }

    // ---- K2. invalidate() ommäter kedjan uppåt men inte syskonens delträd.
    {
        Behållare p, a, b;
        Räknande  a1(10.0f, 10.0f), b1(20.0f, 20.0f);
        p.adopt(a);
        p.adopt(b);
        a.adopt(a1);
        b.adopt(b1);

        Meter::measure(p, c);
        Meter::measure(p, c);
        ok(p.mätningar == 1 && a.mätningar == 1 && b.mätningar == 1 &&
               a1.mätningar == 1 && b1.mätningar == 1,
           "K2: ett träd mäts en gång per widget, två frågor till roten");

        a1.invalidate();
        Meter::measure(p, c);
        ok(a1.mätningar == 2 && a.mätningar == 2 && p.mätningar == 2,
           "K2: invalidate() på ett löv ommäter lövet och förfäderna (" +
               std::to_string(a1.mätningar) + "," + std::to_string(a.mätningar) +
               "," + std::to_string(p.mätningar) + ")");
        ok(b.mätningar == 1 && b1.mätningar == 1,
           "K2: …men inte syskonets delträd (" + std::to_string(b.mätningar) +
               "," + std::to_string(b1.mätningar) + ")");
    }

    // ---- K3. Höjd för bredd: en post per bredd, nullopt cachas, ringen tar
    // de fyra senaste.
    {
        Räknande r(10.0f, 10.0f);
        r.hojd = 33.0f;
        for (float w : {160.0f, 200.0f, 160.0f, 200.0f}) {
            const auto h = Meter::measureHeightForWidth(r, c, w);
            ok(h && nära(*h, 33.0f), "K3: svaret är krokens svar");
        }
        ok(r.höjdfrågor == 2,
           "K3: två bredder återanvänds (fick " + std::to_string(r.höjdfrågor) +
               " frågor)");
        Meter::measureHeightForWidth(r, c, 240.0f);
        Meter::measureHeightForWidth(r, c, 280.0f);
        Meter::measureHeightForWidth(r, c, 160.0f);   // fortfarande kvar
        ok(r.höjdfrågor == 4, "K3: fyra bredder ryms (fick " +
                                  std::to_string(r.höjdfrågor) + ")");
        Meter::measureHeightForWidth(r, c, 320.0f);   // knuffar ut den äldsta
        Meter::measureHeightForWidth(r, c, 160.0f);
        ok(r.höjdfrågor == 6,
           "K3: den femte bredden knuffar ut den äldsta (fick " +
               std::to_string(r.höjdfrågor) + ")");

        Räknande tyst(10.0f, 10.0f);   // hojd = nullopt
        ok(!Meter::measureHeightForWidth(tyst, c, 160.0f) &&
               !Meter::measureHeightForWidth(tyst, c, 160.0f),
           "K3: tiger båda gångerna");
        ok(tyst.höjdfrågor == 1, "K3: nullopt cachas (fick " +
                                     std::to_string(tyst.höjdfrågor) + ")");
        tyst.invalidate();
        Meter::measureHeightForWidth(tyst, c, 160.0f);
        ok(tyst.höjdfrågor == 2, "K3: invalidate() gäller höjdposterna också");
    }

    // ---- K4. De råa layoutfälten är med i nyckeln.
    {
        Räknande r(10.0f, 10.0f);
        Meter::measure(r, c);
        int väntat = 1;
        const auto ändrad = [&](const char* vad) {
            Meter::measure(r, c);
            ++väntat;
            ok(r.mätningar == väntat,
               std::string("K4: ändrad ") + vad + " ger en ny mätning (fick " +
                   std::to_string(r.mätningar) + ", väntat " +
                   std::to_string(väntat) + ")");
        };
        r.width = 50.0f;
        ändrad("width");
        r.height = 20.0f;
        ändrad("height");
        r.x = 3.0f;
        ändrad("x");
        r.y = 4.0f;
        ändrad("y");
        r.right = 5.0f;
        ändrad("right");
        r.bottom = 6.0f;
        ändrad("bottom");
        r.dock = glaze::Dock::Top;
        ändrad("dock");
        r.anchors.right = true;
        ändrad("anchors");
        r.anchorLeft = {"granne", 8.0f};
        ändrad("anchorLeft");
        r.anchorBottom = {"x", 2.0f};
        ändrad("anchorBottom");
        Meter::measure(r, c);
        ok(r.mätningar == väntat, "K4: oförändrade fält ger ingen ny mätning");
    }

    // ---- K5. Handler-epoken.
    {
        Räknande r(10.0f, 10.0f);
        Meter::measure(r, c);
        glaze::detail::bumpHandlerEpoch();
        Meter::measure(r, c);
        ok(r.mätningar == 2, "K5: en ny handler-epok ger en ny mätning (fick " +
                                 std::to_string(r.mätningar) + ")");
        Meter::measure(r, c);
        ok(r.mätningar == 2, "K5: …som sedan cachas");
    }

    // ---- K6. Miljön: UI, stil, tema, skala, mm-täthet och fonter.
    {
        glaze::Style annanStil = st;
        FalskFont    annanFont(7.0f, st.uiFontSize);
        const auto   prova = [&](const char* vad, auto&& ändra) {
            Räknande r(10.0f, 10.0f);
            Meter::measure(r, c);
            glaze::Ctx c2 = c;
            ändra(c2);
            Meter::measure(r, c2);
            ok(r.mätningar == 2, std::string("K6: ") + vad +
                                     " ger en ny mätning (fick " +
                                     std::to_string(r.mätningar) + ")");
            Meter::measure(r, c2);
            ok(r.mätningar == 2, std::string("K6: ") + vad + " cachas igen");
        };
        prova("annat UI", [](glaze::Ctx& x) { x.ui = glaze::UI::Glaze; });
        prova("annan stil", [&](glaze::Ctx& x) { x.style = &annanStil; });
        prova("annat tema", [](glaze::Ctx& x) {
            x.theme = &glaze::Theme::slateDefault().dark;
        });
        prova("annan skala", [](glaze::Ctx& x) { x.scale = 2.0f; });
        prova("annan mm-täthet", [](glaze::Ctx& x) { x.pxPerMm += 1.0f; });
        prova("annan font", [&](glaze::Ctx& x) { x.font = &annanFont; });
    }

    // ---- K7. Text och synlighet når ett cachat svar.
    {
        glaze::Label l("ab");
        const float  smal = Meter::measure(l, c).w;
        l.setText("abcdefgh");
        const float bred = Meter::measure(l, c).w;
        ok(nära(bred, smal * 4.0f), "K7: ändrad text mäts om (" +
                                        std::to_string(smal) + " → " +
                                        std::to_string(bred) + ")");

        Behållare p;
        Räknande  a(10.0f, 10.0f), b(30.0f, 5.0f);
        a.dock = glaze::Dock::Top;
        b.dock = glaze::Dock::Top;
        p.adopt(a);
        p.adopt(b);
        const gfx::Sizef alla = Meter::measure(p, c);
        b.hide();
        const gfx::Sizef utan = Meter::measure(p, c);
        b.show();
        const gfx::Sizef igen = Meter::measure(p, c);
        ok(nära(alla.w, 30.0f) && nära(alla.h, 15.0f),
           "K7: två Top-remsor — 30×15");
        ok(nära(utan.w, 10.0f) && nära(utan.h, 10.0f),
           "K7: den dolda remsan räknas inte — 10×10");
        ok(nära(igen.w, 30.0f) && nära(igen.h, 15.0f),
           "K7: visad igen — 30×15");
    }

    // ---- K8. Barnordningen: Top före Left och Left före Top ger olika mått.
    {
        Behållare p;
        Räknande  topp(20.0f, 10.0f), vänster(5.0f, 30.0f);
        topp.dock    = glaze::Dock::Top;
        vänster.dock = glaze::Dock::Left;
        p.adopt(topp);
        p.adopt(vänster);
        const gfx::Sizef a = Meter::measure(p, c);
        ok(nära(a.w, 20.0f) && nära(a.h, 40.0f),
           "K8: Top, Left — 20×40 (" + std::to_string(a.w) + "×" +
               std::to_string(a.h) + ")");
        ok(p.unadopt(topp), "K8: unadopt(topp)");
        p.adopt(topp);   // nu sist: Left, Top
        const gfx::Sizef b = Meter::measure(p, c);
        ok(nära(b.w, 25.0f) && nära(b.h, 30.0f),
           "K8: Left, Top — 25×30 (" + std::to_string(b.w) + "×" +
               std::to_string(b.h) + ")");
    }

    // ---- K9. Adoption och lossning når både den gamla och den nya kedjan.
    {
        Behållare k1, k2;
        Räknande  barn(12.0f, 8.0f);
        k1.adopt(barn);
        const gfx::Sizef före1 = Meter::measure(k1, c);
        const gfx::Sizef före2 = Meter::measure(k2, c);
        ok(nära(före1.w, 12.0f) && nära(före2.w, 0.0f),
           "K9: barnet i k1, k2 tom");
        ok(k1.unadopt(barn), "K9: unadopt");
        k2.adopt(barn);
        const gfx::Sizef efter1 = Meter::measure(k1, c);
        const gfx::Sizef efter2 = Meter::measure(k2, c);
        ok(nära(efter1.w, 0.0f) && nära(efter1.h, 0.0f),
           "K9: gamla kedjan ommäts — k1 är tom (" + std::to_string(efter1.w) +
               ")");
        ok(nära(efter2.w, 12.0f) && nära(efter2.h, 8.0f),
           "K9: nya kedjan ommäts — k2 bär barnet (" +
               std::to_string(efter2.w) + ")");
    }

    // ---- K10. En fristående widget (inget fönster) cachar också.
    {
        Räknande r(10.0f, 10.0f);
        ok(!r.appWindow(), "K10: fristående, utan fönster");
        Meter::measure(r, c);
        Meter::measure(r, c);
        ok(r.mätningar == 1, "K10: fristående widget cachar (fick " +
                                 std::to_string(r.mätningar) + ")");
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("measure_gate — mätvägens enhet (headless)\n");

    registerLoopen();
    basensDefault();
    hojdForBredd();
    mätcachen();

    std::printf("\n%d fall, %d fel\n", g_fall, g_fel);
    return g_fel == 0 ? 0 : 1;
}
