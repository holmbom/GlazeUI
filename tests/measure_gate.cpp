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

    std::printf("\n%d fall, %d fel\n", g_fall, g_fel);
    return g_fel == 0 ? 0 : 1;
}
