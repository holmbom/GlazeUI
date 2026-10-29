// layout_gate — PLACERINGENS grind.
//
// Kör:  ./build/tests/layout_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Grinden mäter GEOMETRI, och det går att göra helt
// headless: `Widget::resolve()` är `const`, stateless och kräver varken
// fönster eller renderer — bara en `Ctx` med area, scale, style och en
// font. Fonten är FALSK med känt teckensteg, och den skapas per STIL med
// höjden `style.uiFontSize × scale` precis som `AppWindow::reloadFont()`
// gör. Det är det som gör stilbytesfallet nedan ärligt: knapparna byter
// verkligen storlek när stilen byts, och gapet mäts mot det.
//
// FÖRSTA FALLET ÄR STRÖMMENS EXISTENSBERÄTTIGANDE:
// ett spikat `x = 210` är en POSITION och bara sann i EN Style; ett
// syskonankare är en RELATION och sann i alla. Faller det fallet har
// funktionen inget skäl att finnas.
//
// Vad som INTE mäts här: att raderna round-trippar (roundtrip_gate), att
// `bld::applyProp` sätter fälten (preview_gate) och att literalen
// kompilerar (calls_fixture). De tre är egna grindar med flit.
//
// FALL 20 mäter HÖJD FÖR BREDD — att en
// radbruten etikett som får sin bredd av layouten (sträckt, Top/Bottom-
// dockad) också får höjden för sina rader, i resolve OCH i syskonens
// carving. Det mäter INTE pixlarna (att ritningen bryter på samma ställe är
// kontraktet `c.bounds().w` = boxen / skalan, inte en grind här) och inte
// `childrenExtent(c)`, som har en skriven gräns: där är bredden obestämd.
//
// FALL 21 mäter `childrenExtent(c, bredd)` —
// höjden för en GIVEN bredd: en Top-dockad remsa tar kroken med sin
// remsbredd (bredden minus Left-remsorna FÖRE den), ett sträckt odockat barn
// med restbredden minus offseten, och formen utan bredd är oförändrad. Förväntat värde
// är handräknat ur kroken `6000 / bredd`. Sabotage som ska fälla det: kroken
// ignoreras i `extentOf` (a–c), Left-remsan dras inte av (b: 20 i stället
// för 30), offseten dras inte av (c: 20 i stället för 21,43). Det mäter INTE
// fönstrets anpassning (`setSizeToContent` — geometry_gate SC).
//
// FALL 22 mäter INVERSEN ÅT BÅDA HÅLLEN, och
// fall 19 har tre värden som beror på den: (8c) 50 (ett högerbundet barn
// ryms; ankaret mot dess högerkant hamnar utanför), (8d) 65 med ny
// text (`anchorRight` mot ett barn från början står före det) och (9) 40.
// Fallen, handräknade ur de satta fälten: (a) fält uppifrån och OK nere till
// höger ⇒ höjd 30 + 38 = 68, bredd max(110, 72) = 110 (skala 1 och 2); (b) +
// Avbryt kedjad från slutet ⇒ bredd 130; (c) luften `a.bottom = 6` ⇒ 74, och
// `ok.y = 300` ändrar inget (skala 1 och 2); (d) en lista spänd mellan a och ok
// ⇒ 88, med `height = 120` ⇒ 208; (e) "Namn" och "Hjälp" i samma rad ⇒ bredd
// 140, nedåt flyttad ⇒ 90 och höjd 60; (f) (d) i sidled ⇒ 110 (listan står
// över knapparna); (g) ett dolt led i en kedja ⇒ 82; (h) kroken mot ett
// syskon från slutet i bredden 300 ⇒ 76,571.
// SABOTAGEN som ska fälla fall 22 (ett i taget, mot ett rent original):
//   · de slutbundna ignoreras ............................ (a): 30
//   · flödena som max i stället för summa ................ (a): 38
//   · luften ignoreras ................................... (c): 68
//   · `x`/`y` räknas som luft på ett slutbundet .......... (c): 374
//   · det spända minsta måttet ignoreras ................. (d): 68
//   · stapling i sidled för alla par ..................... (a), (f): 182
//   · ingen stapling i sidled ............................ (e): 90
//   · regel (b) i `överVarandra` saknas .................. (f): 182
//   · dolda hoppas över före kanterna .................... (g): 30
//   · kroken bara mot förälderns högerkant ............... (h): 48
// Fall 22 mäter INTE fönstret (geometry_gate SB/SG), pixlar eller
// gruppramens ritning, och inte sidledsparen när höjden inte räcker (`Width`
// ensamt — skriven gräns, inte ett fall).

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
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

void okNära(float fick, float vänta, const std::string& vad)
{
    const bool nära = (fick > vänta ? fick - vänta : vänta - fick) < 0.01f;
    ++g_fall;
    if (nära) {
        if (g_verbose)
            std::printf("  ok   %s (%.2f)\n", vad.c_str(),
                        static_cast<double>(fick));
        return;
    }
    std::printf("  FEL  %s — fick %.3f, väntade %.3f\n", vad.c_str(),
                static_cast<double>(fick), static_cast<double>(vänta));
    ++g_fel;
}

// ------------------------------------------------------------ falsk font
//
// Ett FAST teckensteg per kodpunkt gör knappbredderna räknebara.
// `msgbox_gate`s prejudikat: fonten talar FYSISKA px, så steget skalas med
// DPI precis som en riktig atlas gör.
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

// ------------------------------------------------------------- riggen
//
// En NAKEN container: `childArea()` är basens (= den egna recten) och
// widgeten är sträckt över hela ytan, så barnens area är EXAKT `c.area`.
// En `Panel` hade fungerat lika bra men lagt på sin egen inramning, och
// den här grinden mäter ankarmatematiken — inte Panels marginaler.
struct Yta final : glaze::Widget {
    Yta() { anchors = {true, true, true, true}; }
    void draw(const glaze::Ctx&) override {}
};

// Ctx utan renderer. `resolve()`/`contentSize()` rör aldrig `c.r` eller
// `c.box` — mätvägen körs INNE i resolve och får per kontrakt inte läsa
// box (widget.h).
glaze::Ctx ctxFör(const glaze::Style& st, gfx::Font& f, float scale,
                  const gfx::Rectf& area)
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = scale;
    c.ui          = glaze::UI::Slate;
    c.style       = &st;
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = area;
    return c;
}

// Barnens Ctx: förälderns childArea, exakt som `AppWindow::drawTree` gör.
glaze::Ctx barnCtx(const Yta& rot, const glaze::Ctx& c)
{
    glaze::Ctx cc = c;
    cc.area       = rot.childArea(c);
    return cc;
}

// ============================================================ 1. STILBYTET
//
// SAMMA TRÄD, TVÅ STILAR. Gapet mellan knapparna ska vara 8 LOGISKA px i
// båda medan knapparnas egna mått skiljer sig — det är hela poängen.
void fallStilbyte()
{
    std::printf("\n=== 1. STILBYTET — gapet står stilla, måtten gör inte det "
                "===\n");

    struct Mätning {
        float gap1 = 0, gap2 = 0;   // logiska px mellan kant och kant
        float bredd = 0;            // första knappens bredd, logiska px
        float höjd  = 0;
    };

    auto mät = [](const glaze::Style& st) {
        const float scale = 1.0f;
        FalskFont   f(7.0f * scale, st.uiFontSize * scale);
        glaze::Ctx  c = ctxFör(st, f, scale, gfx::Rectf{0, 0, 600, 400});

        Yta rot;
        glaze::Button a("Ett"), b("Två"), d("Tre");
        a.name = "a";
        b.name = "b";
        d.name = "d";
        a.x = 10;
        a.y = 12;
        // KEDJAN: varje knapp hänger på den FÖREGÅENDE. Inget x, ingen
        // position — bara relationen.
        b.anchorLeft = {"a", 8};
        b.y          = 12;
        d.anchorLeft = {"b", 8};
        d.y          = 12;
        rot.adopt(a);
        rot.adopt(b);
        rot.adopt(d);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf ra = a.resolve(cc);
        const gfx::Rectf rb = b.resolve(cc);
        const gfx::Rectf rd = d.resolve(cc);
        return Mätning{(rb.x - (ra.x + ra.w)) / scale,
                       (rd.x - (rb.x + rb.w)) / scale, ra.w / scale,
                       ra.h / scale};
    };

    const Mätning slate = mät(glaze::defaultStyle(glaze::UI::Slate));
    const Mätning mini  = mät(glaze::defaultStyle(glaze::UI::Minimalistic));

    okNära(slate.gap1, 8.0f, "Slate: gap knapp1→knapp2 = 8");
    okNära(slate.gap2, 8.0f, "Slate: gap knapp2→knapp3 = 8");
    okNära(mini.gap1, 8.0f, "Minimalistic: gap knapp1→knapp2 = 8");
    okNära(mini.gap2, 8.0f, "Minimalistic: gap knapp2→knapp3 = 8");

    // ... OCH att måtten faktiskt skiljer sig. Utan den här raden hade
    // testet varit grönt även om båda stilarna gav samma knappstorlek,
    // alltså om riggen inte mätte något alls.
    ok(slate.bredd != mini.bredd,
       "knappbredden SKILJER sig mellan stilarna (annars mäter riggen inget)");
    ok(slate.höjd != mini.höjd, "knapphöjden SKILJER sig mellan stilarna");
    if (g_verbose)
        std::printf("       Slate %.2f×%.2f · Minimalistic %.2f×%.2f\n",
                    static_cast<double>(slate.bredd),
                    static_cast<double>(slate.höjd),
                    static_cast<double>(mini.bredd),
                    static_cast<double>(mini.höjd));
}

// ==================================================== 2. de fyra kanterna
void fallFyraKanter()
{
    std::printf("\n=== 2. de fyra kanterna — riktningen följer av fältet "
                "===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    // HÖGER OM och UNDER — anchorLeft/anchorTop.
    {
        Yta rot;
        glaze::Button a("Mål");
        glaze::Button höger("H"), under("U");
        a.name   = "mal";
        a.x      = 40;
        a.y      = 30;
        a.width  = 100;
        a.height = 24;
        höger.anchorLeft = {"mal", 8};
        höger.y          = 30;
        höger.width      = 50;
        höger.height     = 24;
        under.anchorTop  = {"mal", 6};
        under.x          = 40;
        under.width      = 50;
        under.height     = 24;
        rot.adopt(a);
        rot.adopt(höger);
        rot.adopt(under);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf ra = a.resolve(cc);
        const gfx::Rectf rh = höger.resolve(cc);
        const gfx::Rectf ru = under.resolve(cc);
        okNära(rh.x, ra.x + ra.w + 8.0f, "anchorLeft: vänsterkant = mål.höger + 8");
        okNära(rh.w, 50.0f, "anchorLeft: bredden är widgetens egen");
        okNära(ru.y, ra.y + ra.h + 6.0f, "anchorTop: överkant = mål.under + 6");
        okNära(ru.h, 24.0f, "anchorTop: höjden är widgetens egen");
    }

    // VÄNSTER OM och ÖVER — anchorRight/anchorBottom. Målet måste ligga
    // TIDIGARE i barnordningen även här; kanten är den motstående.
    {
        Yta rot;
        glaze::Button a("Mål");
        glaze::Button vänster("V"), över("Ö");
        a.name   = "mal";
        a.x      = 200;
        a.y      = 200;
        a.width  = 100;
        a.height = 24;
        vänster.anchorRight = {"mal", 8};
        vänster.y           = 200;
        vänster.width       = 50;
        vänster.height      = 24;
        över.anchorBottom   = {"mal", 6};
        över.x              = 200;
        över.width          = 50;
        över.height         = 24;
        rot.adopt(a);
        rot.adopt(vänster);
        rot.adopt(över);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf ra = a.resolve(cc);
        const gfx::Rectf rv = vänster.resolve(cc);
        const gfx::Rectf rö = över.resolve(cc);
        okNära(rv.x + rv.w, ra.x - 8.0f,
               "anchorRight: högerkant = mål.vänster − 8");
        okNära(rö.y + rö.h, ra.y - 6.0f,
               "anchorBottom: underkant = mål.över − 6");
    }
}

// ======================================== 3. gapet följer målets storlek
void fallMåletVäxer()
{
    std::printf("\n=== 3. gapet följer med när målet byter storlek ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    Yta rot;
    glaze::Button a("Mål"), b("Följare");
    a.name   = "mal";
    a.x      = 20;
    a.width  = 80;
    a.height = 24;
    b.anchorLeft = {"mal", 8};
    b.width      = 40;
    b.height     = 24;
    rot.adopt(a);
    rot.adopt(b);

    const glaze::Ctx cc = barnCtx(rot, c);
    const float      innan = b.resolve(cc).x;
    okNära(innan, 20.0f + 80.0f + 8.0f, "före: 20 + 80 + 8");

    a.width = 140.0f;   // layoutfält är RÅA — ändras direkt (widget.h)
    const gfx::Rectf efter = b.resolve(cc);
    okNära(efter.x, 20.0f + 140.0f + 8.0f, "efter: 20 + 140 + 8");
    okNära(efter.x - innan, 60.0f, "följaren flyttade sig exakt lika mycket");
}

// ================================================== 4. sträckning gratis
void fallSträckning()
{
    std::printf("\n=== 4. sträckning — två bundna kanter på samma axel ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    // SYSKON ↔ SYSKON: mellan två knappar. Bredden är differensen, och
    // widgetens egen `width` ignoreras precis som vid dubbelt förälderankare.
    {
        Yta rot;
        glaze::Button v("V"), h("H"), mitt("M");
        v.name   = "v";
        h.name   = "h";
        v.x      = 20;
        v.width  = 60;
        v.height = 24;
        h.x      = 400;
        h.width  = 60;
        h.height = 24;
        mitt.anchorLeft  = {"v", 10};
        mitt.anchorRight = {"h", 10};
        mitt.width       = 5;   // ska IGNORERAS
        mitt.height      = 24;
        rot.adopt(v);
        rot.adopt(h);
        rot.adopt(mitt);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rm = mitt.resolve(cc);
        okNära(rm.x, 80.0f + 10.0f, "syskon↔syskon: vänsterkant 90");
        okNära(rm.w, 400.0f - 10.0f - 90.0f,
               "syskon↔syskon: bredden är differensen (egen width ignoreras)");
    }

    // SYSKON ↔ FÖRÄLDER: vänster mot ett syskon, höger mot fönsterkanten.
    {
        Yta rot;
        glaze::Button v("V"), sträckt("S");
        v.name   = "v";
        v.x      = 20;
        v.width  = 60;
        v.height = 24;
        sträckt.anchorLeft = {"v", 10};
        sträckt.anchors    = {true, true, true, false};
        sträckt.right      = 16;
        sträckt.height     = 24;
        rot.adopt(v);
        rot.adopt(sträckt);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rs = sträckt.resolve(cc);
        okNära(rs.x, 90.0f, "syskon↔förälder: vänsterkant 90");
        okNära(rs.x + rs.w, 600.0f - 16.0f,
               "syskon↔förälder: högerkanten hålls av förälderankaret");
    }

    // NEGATIV differens klampas till 0 — aldrig en rect med negativ bredd.
    {
        Yta rot;
        glaze::Button v("V"), h("H"), mitt("M");
        v.name   = "v";
        h.name   = "h";
        v.x      = 300;
        v.width  = 60;
        v.height = 24;
        h.x      = 20;
        h.width  = 60;
        h.height = 24;
        mitt.anchorLeft  = {"v", 0};
        mitt.anchorRight = {"h", 0};
        mitt.height      = 24;
        rot.adopt(v);
        rot.adopt(h);
        rot.adopt(mitt);

        const glaze::Ctx cc = barnCtx(rot, c);
        okNära(mitt.resolve(cc).w, 0.0f, "korsade kanter ⇒ bredd klampas 0");
    }
}

// ============================= 5. miss ⇒ TYST fallback, aldrig ett häng
void fallMiss()
{
    std::printf("\n=== 5. framåt-/själv-/okänd referens ⇒ fallback ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    Yta rot;
    glaze::Button a("A"), b("B"), själv("S"), okänd("O");
    a.name   = "a";
    b.name   = "b";
    själv.name = "sjalv";
    okänd.name = "okand";
    for (glaze::Button* w : {&a, &b, &själv, &okänd}) {
        w->width  = 50;
        w->height = 24;
    }
    // FRAMÅTREFERENS: `a` pekar på `b`, som ligger EFTER i barnordningen.
    a.x          = 30;
    a.y          = 10;
    a.anchorLeft = {"b", 8};
    b.x          = 30;
    b.y          = 50;
    // SJÄLVREFERENS.
    själv.x          = 30;
    själv.y          = 90;
    själv.anchorLeft = {"sjalv", 8};
    // OKÄNT ID.
    okänd.x          = 30;
    okänd.y          = 130;
    okänd.anchorLeft = {"finns-inte", 8};
    rot.adopt(a);
    rot.adopt(b);
    rot.adopt(själv);
    rot.adopt(okänd);

    const glaze::Ctx cc = barnCtx(rot, c);
    okNära(a.resolve(cc).x, 30.0f, "framåtreferens ⇒ x gäller (30)");
    okNära(själv.resolve(cc).x, 30.0f, "självreferens ⇒ x gäller (30)");
    okNära(okänd.resolve(cc).x, 30.0f, "okänt id ⇒ x gäller (30)");

    // ÖMSESIDIG "cykel": A→B och B→A. Den ena riktningen är ALLTID en
    // framåtreferens, så uppslaget kan inte snurra. Att det här fallet
    // överhuvudtaget returnerar är beviset — hänger det tar ctest-timeouten
    // testet, vilket är avsikten.
    {
        Yta      rot2;
        glaze::Button p("P"), q("Q");
        p.name = "p";
        q.name = "q";
        p.width = q.width = 50;
        p.height = q.height = 24;
        p.x = 10;
        q.x = 10;
        p.anchorLeft = {"q", 8};   // framåt — ignoreras
        q.anchorLeft = {"p", 8};   // bakåt  — gäller
        rot2.adopt(p);
        rot2.adopt(q);

        const glaze::Ctx c2 = barnCtx(rot2, c);
        const gfx::Rectf rp = p.resolve(c2);
        okNära(rp.x, 10.0f, "cykelförsök: P faller tillbaka på x");
        okNära(q.resolve(c2).x, rp.x + rp.w + 8.0f,
               "cykelförsök: Q hänger på P — ingen loop");
    }
}

// ======================================================== 6. dockat mål
void fallDockatMål()
{
    std::printf("\n=== 6. dockat mål — den enda korrekta vägen intill en "
                "remsa ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    Yta rot;
    glaze::Panel  sido;
    glaze::Button intill("I");
    sido.name    = "sido";
    sido.dock    = glaze::Dock::Left;
    sido.width   = 120;
    intill.anchorLeft = {"sido", 12};
    intill.y          = 20;
    intill.width      = 60;
    intill.height     = 24;
    rot.adopt(sido);
    rot.adopt(intill);

    const glaze::Ctx cc = barnCtx(rot, c);
    const gfx::Rectf rs = sido.resolve(cc);
    okNära(rs.w, 120.0f, "dockad remsa är 120 bred");
    okNära(intill.resolve(cc).x, rs.x + rs.w + 12.0f,
           "följaren ligger 12 px från remsans högerkant");
}

// ==================================================== 7. inne i en Bar
void fallBar()
{
    std::printf("\n=== 7. inne i en Bar IGNORERAS ankaret (gren 0 vinner) "
                "===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    glaze::Bar    bar;
    glaze::Button a("Ett"), b("Två");
    a.name = "a";
    // Ett ankare som INTE ska få någon effekt: `placesChildren()` körs före
    // dock, anchors och syskonankare. Ingen specialgren finns i koden —
    // det här fallet mäter att ingen behövs.
    b.anchorLeft = {"a", 200};
    bar.adopt(a);
    bar.adopt(b);

    // `Panel` (och därmed `Bar`) SNÄVAR IN container-krokarna till
    // protected, så åtkomsten går via Widget-referensen — exakt vad
    // `drawPreviewTree` gör, som vandrar med `glaze::Widget&`.
    glaze::Ctx cc = c;
    cc.area       = static_cast<const glaze::Widget&>(bar).childArea(c);
    const gfx::Rectf ra = a.resolve(cc);
    const gfx::Rectf rb = b.resolve(cc);
    okNära(rb.x, ra.x + ra.w, "Bar packar tätt — ankarets 200 px ignoreras");
    okNära(rb.y, ra.y, "Bar ger barnen samma överkant");
}

// ======================================================== 8. skala ≠ 1
void fallSkala()
{
    std::printf("\n=== 8. skala ≠ 1.0 — gapet är LOGISKA px ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);

    for (float scale : {1.0f, 1.5f, 2.0f}) {
        FalskFont  f(7.0f * scale, st.uiFontSize * scale);
        glaze::Ctx c = ctxFör(st, f, scale,
                              gfx::Rectf{0, 0, 600 * scale, 400 * scale});

        Yta rot;
        glaze::Button a("Mål"), b("Följare");
        a.name   = "mal";
        a.x      = 20;
        a.width  = 80;
        a.height = 24;
        b.anchorLeft = {"mal", 8};
        b.width      = 40;
        b.height     = 24;
        rot.adopt(a);
        rot.adopt(b);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf ra = a.resolve(cc);
        const gfx::Rectf rb = b.resolve(cc);
        const std::string s =
            "skala " + std::to_string(static_cast<double>(scale)).substr(0, 3);
        okNära((rb.x - (ra.x + ra.w)) / scale, 8.0f, s + ": gapet = 8 logiska");
        okNära(rb.x, (20.0f + 80.0f + 8.0f) * scale,
               s + ": vänsterkanten i fysiska px");
    }
}

// ================================= 9. dolt mål räknas ändå (skrivet beslut)
void fallDoltMål()
{
    std::printf("\n=== 9. dolt mål — ankaret följer rect:en ändå ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    Yta rot;
    glaze::Button a("Mål"), b("Följare");
    a.name   = "mal";
    a.x      = 20;
    a.width  = 80;
    a.height = 24;
    b.anchorLeft = {"mal", 8};
    b.width      = 40;
    b.height     = 24;
    rot.adopt(a);
    rot.adopt(b);

    const glaze::Ctx cc = barnCtx(rot, c);
    const float      synligt = b.resolve(cc).x;
    a.hide();
    okNära(b.resolve(cc).x, synligt,
           "följaren står kvar där grannen skulle ha varit");
}

// ==================================== 10. INGEN REGRESSION utan syskonankare
//
// Anchors-grenen. Det här fallet mäter att de fyra
// utfallen är BIT-identiska: en tyst förskjutning i dubbelankaret hade
// annars flyttat varenda sträckt widget i varenda app.
void fallGammalSemantik()
{
    std::printf("\n=== 10. anchors-semantiken oförändrad (regressionen) "
                "===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{10, 20, 600, 400});

    Yta rot;
    glaze::Button vänster("V"), höger("H"), sträck("S"), mitt("M");
    for (glaze::Button* w : {&vänster, &höger, &sträck, &mitt}) {
        w->width  = 50;
        w->height = 24;
    }
    vänster.anchors = {true, true, false, false};
    vänster.x       = 30;
    vänster.y       = 40;

    höger.anchors = {false, false, true, true};
    höger.right   = 16;
    höger.bottom  = 12;

    sträck.anchors = {true, true, true, true};
    sträck.x       = 8;
    sträck.y       = 8;
    sträck.right   = 8;
    sträck.bottom  = 8;

    mitt.anchors = {false, false, false, false};
    mitt.x       = 5;
    mitt.y       = -5;

    rot.adopt(vänster);
    rot.adopt(höger);
    rot.adopt(sträck);
    rot.adopt(mitt);

    const glaze::Ctx cc = barnCtx(rot, c);
    const gfx::Rectf rv = vänster.resolve(cc);
    okNära(rv.x, 10.0f + 30.0f, "left+top: x från areans vänsterkant");
    okNära(rv.y, 20.0f + 40.0f, "left+top: y från areans överkant");

    const gfx::Rectf rh = höger.resolve(cc);
    okNära(rh.x, 10.0f + 600.0f - 16.0f - 50.0f, "right+bottom: högerkanten");
    okNära(rh.y, 20.0f + 400.0f - 12.0f - 24.0f, "right+bottom: underkanten");

    const gfx::Rectf rs = sträck.resolve(cc);
    okNära(rs.x, 18.0f, "dubbelankare: x");
    okNära(rs.w, 600.0f - 16.0f, "dubbelankare: bredd = area.w − (x+right)");
    okNära(rs.h, 400.0f - 16.0f, "dubbelankare: höjd = area.h − (y+bottom)");

    const gfx::Rectf rm = mitt.resolve(cc);
    okNära(rm.x, 10.0f + (600.0f - 50.0f) / 2.0f + 5.0f, "utan ankare: centrerad + x");
    okNära(rm.y, 20.0f + (400.0f - 24.0f) / 2.0f - 5.0f, "utan ankare: centrerad + y");
}

// ============================ 11. DOCKADE REMSOR SKÄR FÖR ALLA SYSKON
//
// `resolve()` spelar upp dock-skärningen också för widgets som inte själva är
// dockade (gren 1 gäller de dockade); annars läser ett
// icke-dockat barn `c.area` OSKUREN och lägger sig därför RAKT ÖVER
// menyraden och statusraden.
//
// GEOMETRIN: area = [0,100] i y-led,
// ordningen Top(10) → Bottom(20) → Fill → odockad ger
// Top [0,10] · Bottom [80,100] · Fill [10,80] · odockad [10,80].
void fallDockCarving()
{
    std::printf("\n=== 11. dockade remsor skär för ALLA syskon ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 200, 100});

    // ---- ordning A: Top → Bottom → Fill → odockad
    {
        Yta rot;
        glaze::Panel topp, botten, fyll, lös;
        topp.dock   = glaze::Dock::Top;
        topp.height = 10;
        botten.dock   = glaze::Dock::Bottom;
        botten.height = 20;
        fyll.dock = glaze::Dock::Fill;
        lös.anchors = {true, true, true, true};   // sträckt över "resten"
        rot.adopt(topp);
        rot.adopt(botten);
        rot.adopt(fyll);
        rot.adopt(lös);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rt = topp.resolve(cc);
        const gfx::Rectf rb = botten.resolve(cc);
        const gfx::Rectf rf = fyll.resolve(cc);
        const gfx::Rectf rl = lös.resolve(cc);
        okNära(rt.y, 0.0f, "A: Top-remsan börjar på 0");
        okNära(rt.h, 10.0f, "A: Top-remsan är 10 hög");
        okNära(rb.y, 80.0f, "A: Bottom-remsan börjar på 80");
        okNära(rf.y, 10.0f, "A: Fill börjar under Top");
        okNära(rf.h, 70.0f, "A: Fill är 70 hög");
        okNära(rl.y, 10.0f, "A: ODOCKAD börjar under Top-remsan (buggen)");
        okNära(rl.h, 70.0f, "A: ODOCKAD slutar ovanför Bottom-remsan");
    }

    // ---- ordning B: Top → Fill → Bottom → odockad. Fill och Bottom
    // ÖVERLAPPAR här — det är det SKRIVNA Fill-kontraktet ("Fill tar
    // resten UTAN att förbruka den") och inget fel. Den ODOCKADE
    // ytan är däremot IDENTISK med ordning A: ett icke-dockat barn bor i
    // "resten", och att låta adopt-ordningen styra det hade gjort
    // layouten ordningskänslig utan vinst.
    {
        Yta rot;
        glaze::Panel topp, fyll, botten, lös;
        topp.dock   = glaze::Dock::Top;
        topp.height = 10;
        fyll.dock = glaze::Dock::Fill;
        botten.dock   = glaze::Dock::Bottom;
        botten.height = 20;
        lös.anchors = {true, true, true, true};
        rot.adopt(topp);
        rot.adopt(fyll);
        rot.adopt(botten);
        rot.adopt(lös);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rf = fyll.resolve(cc);
        const gfx::Rectf rl = lös.resolve(cc);
        okNära(rf.h, 90.0f, "B: Fill tar resten efter Top (och Bottom över den)");
        okNära(rl.y, 10.0f, "B: ODOCKAD börjar under Top — ordningen spelar ingen roll");
        okNära(rl.h, 70.0f, "B: ODOCKAD höjd oförändrad mot ordning A");
    }

    // ---- alla fyra kanterna på en gång.
    {
        Yta rot;
        glaze::Panel v, h, t, b, lös;
        v.dock  = glaze::Dock::Left;   v.width  = 30;
        h.dock  = glaze::Dock::Right;  h.width  = 20;
        t.dock  = glaze::Dock::Top;    t.height = 10;
        b.dock  = glaze::Dock::Bottom; b.height = 15;
        lös.anchors = {true, true, true, true};
        rot.adopt(v);
        rot.adopt(h);
        rot.adopt(t);
        rot.adopt(b);
        rot.adopt(lös);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rl = lös.resolve(cc);
        okNära(rl.x, 30.0f, "fyra kanter: vänsterkant efter Left-remsan");
        okNära(rl.w, 200.0f - 30.0f - 20.0f, "fyra kanter: bredden");
        okNära(rl.y, 10.0f, "fyra kanter: överkant efter Top-remsan");
        okNära(rl.h, 100.0f - 10.0f - 15.0f, "fyra kanter: höjden");
    }
}

// ================================================= 12. DOLDA remsor skär inget
//
// Samma `visible_`-kontroll som gren 1 och som `AppWindow::chromeInsetH`.
// Allt annat hade gjort `statusBar.show()` layoutmässigt osynkad med
// fönstrets storleksräkning.
void fallDockDold()
{
    std::printf("\n=== 12. dold remsa skär ingenting ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 200, 100});

    Yta rot;
    glaze::Panel remsa, lös;
    remsa.dock   = glaze::Dock::Top;
    remsa.height = 24;
    lös.anchors  = {true, true, true, true};
    rot.adopt(remsa);
    rot.adopt(lös);

    const glaze::Ctx cc = barnCtx(rot, c);
    okNära(lös.resolve(cc).y, 24.0f, "synlig remsa ⇒ ytan börjar på 24");
    remsa.hide();
    okNära(lös.resolve(cc).y, 0.0f, "DOLD remsa ⇒ ytan börjar på 0 igen");
    okNära(lös.resolve(cc).h, 100.0f, "DOLD remsa ⇒ full höjd");
    remsa.show();
    okNära(lös.resolve(cc).y, 24.0f, "show() igen ⇒ remsan skär igen");
}

// ============================== 13. syskonankare × den skurna arean
//
// `c.area` ändras ALDRIG mellan syskon — skärningen sker INUTI varje
// widgets egen `resolve()`. Målet resolvas därför med SAMMA Ctx och räknar
// fram sin egen identiska skurna area.
void fallDockAnkare()
{
    std::printf("\n=== 13. syskonankare mot skuren area ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 400, 200});

    // (a) DOCKAT MÅL, ODOCKAD FÖLJARE — målets gren 1 räknar sin remsa.
    {
        Yta rot;
        glaze::Panel  meny;
        glaze::Button följare("F");
        meny.name   = "meny";
        meny.dock   = glaze::Dock::Top;
        meny.height = 24;
        följare.anchorTop = {"meny", 6};
        följare.x         = 10;
        följare.width     = 60;
        följare.height    = 20;
        rot.adopt(meny);
        rot.adopt(följare);

        const glaze::Ctx cc = barnCtx(rot, c);
        okNära(följare.resolve(cc).y, 24.0f + 6.0f,
               "dockat mål: följaren 6 px under remsan");
        // x går genom FÖRÄLDERankaret och läser alltså den SKURNA arean —
        // Top skär inget i x-led, så talet är oförändrat.
        okNära(följare.resolve(cc).x, 10.0f, "dockat mål: x oförändrat");
    }

    // (b) ODOCKAT MÅL, ODOCKAD FÖLJARE — båda går genom hjälparen och
    // måste få SAMMA skurna area, annars glider kedjan isär.
    {
        Yta rot;
        glaze::Panel  remsa;
        glaze::Button mål("M"), följare("F");
        remsa.dock   = glaze::Dock::Top;
        remsa.height = 24;
        mål.name   = "mal";
        mål.y      = 8;
        mål.x      = 10;
        mål.width  = 60;
        mål.height = 20;
        följare.anchorTop = {"mal", 6};
        följare.x         = 10;
        följare.width     = 60;
        följare.height    = 20;
        rot.adopt(remsa);
        rot.adopt(mål);
        rot.adopt(följare);

        const glaze::Ctx cc = barnCtx(rot, c);
        okNära(mål.resolve(cc).y, 24.0f + 8.0f, "odockat mål: y mot skuren area");
        okNära(följare.resolve(cc).y, 24.0f + 8.0f + 20.0f + 6.0f,
               "odockat mål: följaren hänger på målets skurna rect");
    }

    // (c) DOCKAD FÖLJARE med ankarfält — ankaret IGNORERAS HELT, eftersom
    // gren 1 returnerar före ankargrenen. Skrivet beslut, inte en miss:
    // "syskonankare är immunt" gäller widgets som NÅR gren 2.
    {
        Yta rot;
        glaze::Panel mål, dockadFöljare;
        mål.name   = "mal";
        mål.y      = 40;
        mål.height = 20;
        mål.width  = 50;
        dockadFöljare.dock      = glaze::Dock::Bottom;
        dockadFöljare.height    = 30;
        dockadFöljare.anchorTop = {"mal", 500};   // ska inte göra något
        rot.adopt(mål);
        rot.adopt(dockadFöljare);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rd = dockadFöljare.resolve(cc);
        okNära(rd.y, 200.0f - 30.0f, "dockad följare: docken vinner");
        okNära(rd.h, 30.0f, "dockad följare: ankaret är DÖTT (dokumenterat)");
    }
}

// ======================= 14. `contentSize`-kontraktet för DOCKADE widgets
//
// Hjälparen mäter varje dockat syskon. Kontraktet (
// skrivet i widget.h) är därför PRECIST: remstjockleken får inte bero på
// `c.area` LÄNGS SKÄRAXELN. Två fall, inte ett — en anropsräknare utan
// förväntat värde mäter bara att kostnaden existerar.
namespace {

// Kontraktsenlig remsa: tjockleken är ett FAST logiskt mått, breddens
// beroende av tväraxeln är legitimt.
//
// SVARAR I LOGISKA PX — `tj` går ut rakt
// av, utan `c.scale`. Förväntat värde i fallen nedan: `resolve()`
// multiplicerar på konsumtionssidan, så remsan är 12
// FYSISKA px vid skala 1.
struct Remsa final : glaze::Widget {
    explicit Remsa(float tjocklek) : tj(tjocklek) {}
    void  draw(const glaze::Ctx&) override {}
    float tj      = 0;
    mutable int mätningar = 0;

protected:
    gfx::Sizef contentSize(const glaze::Ctx&) const override
    {
        ++mätningar;
        return gfx::Sizef{tj, tj};
    }
};

} // namespace

void fallDockKontrakt()
{
    std::printf("\n=== 14. contentSize-kontraktet för dockade widgets ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);

    // (1) SAMMA remsa, TVÅ olika areor på skäraxeln ⇒ samma tjocklek, och
    // därmed samma skurna yta räknat från kanten.
    {
        Yta rot;
        Remsa remsa(12.0f);
        glaze::Panel lös;
        remsa.dock  = glaze::Dock::Top;
        lös.anchors = {true, true, true, true};
        rot.adopt(remsa);
        rot.adopt(lös);

        for (float h : {100.0f, 400.0f}) {
            glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 200, h});
            const glaze::Ctx cc = barnCtx(rot, c);
            okNära(lös.resolve(cc).y, 12.0f,
                   "kontrakt: remsans tjocklek oberoende av area.h (h=" +
                       std::to_string(static_cast<int>(h)) + ")");
            okNära(lös.resolve(cc).h, h - 12.0f,
                   "kontrakt: den lösa ytan är resten (h=" +
                       std::to_string(static_cast<int>(h)) + ")");
        }
    }

    // (2) ANROPSRÄKNARE MED FÖRVÄNTAT VÄRDE: ETT resolve av ett odockat barn mäter
    // varje synlig remsa HÖGST EN gång i hjälparpasset. Utan ett förväntat värde mäter
    // en räknare bara att kostnaden finns.
    {
        Yta rot;
        Remsa a(10.0f), b(10.0f), dold(10.0f);
        glaze::Panel lös;
        a.dock    = glaze::Dock::Top;
        b.dock    = glaze::Dock::Bottom;
        dold.dock = glaze::Dock::Left;
        dold.hide();
        lös.anchors = {true, true, true, true};
        rot.adopt(a);
        rot.adopt(b);
        rot.adopt(dold);
        rot.adopt(lös);

        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 200, 100});
        const glaze::Ctx cc = barnCtx(rot, c);
        a.mätningar = b.mätningar = dold.mätningar = 0;
        (void)lös.resolve(cc);
        ok(a.mätningar == 1, "räknare: synlig remsa 1 mäts exakt EN gång (fick " +
                                 std::to_string(a.mätningar) + ")");
        ok(b.mätningar == 1, "räknare: synlig remsa 2 mäts exakt EN gång (fick " +
                                 std::to_string(b.mätningar) + ")");
        ok(dold.mätningar == 0, "räknare: DOLD remsa mäts inte alls (fick " +
                                    std::to_string(dold.mätningar) + ")");
    }
}

// ============================= 15. skala ≠ 1 och sträckning mot skuren area
void fallDockSkala()
{
    std::printf("\n=== 15. skuren area i skala ≠ 1 ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);

    for (float scale : {1.0f, 2.0f}) {
        FalskFont  f(7.0f * scale, st.uiFontSize * scale);
        glaze::Ctx c = ctxFör(st, f, scale,
                              gfx::Rectf{0, 0, 300 * scale, 200 * scale});

        Yta rot;
        glaze::Panel remsa, sträckt;
        remsa.dock    = glaze::Dock::Top;
        remsa.height  = 24;
        sträckt.anchors = {true, true, true, true};
        sträckt.x = 8;
        sträckt.y = 4;
        sträckt.right  = 8;
        sträckt.bottom = 4;
        rot.adopt(remsa);
        rot.adopt(sträckt);

        const glaze::Ctx cc = barnCtx(rot, c);
        const gfx::Rectf rs = sträckt.resolve(cc);
        const std::string s =
            "skala " + std::to_string(static_cast<double>(scale)).substr(0, 3);
        okNära(rs.y / scale, 24.0f + 4.0f, s + ": sträckt överkant under remsan");
        okNära(rs.h / scale, 200.0f - 24.0f - 4.0f - 4.0f,
               s + ": sträckt höjd mot den SKURNA arean");
        okNära(rs.x / scale, 8.0f, s + ": x oförändrat (Top skär inte i x-led)");
    }
}

// ================================ 16. INVERSEN — builderns fyra formler
//
// GlazeBuilder skriver `x`/`y`/`right`/`bottom`
// BAKLÄNGES ur en muspunkt och måste räkna mot den
// SKURNA arean — annars hoppar en släppt widget exakt en remstjocklek vid
// mouse-up. Grinden kör formlerna som står i `dragTo`/`resizeTo` med
// `Widget::carveSiblingDocks` som förväntat värde och stänger cirkeln med
// `resolve()`: begärd vänsterkant in, samma vänsterkant ut.
//
// VAD GRINDEN INTE ÄR: den kör inte GlazeBuilders egen kod (den bor i ett
// fönsterbundet target). Den mäter FORMLERNA — att en area som skurits en
// gång och en invers räknad mot samma area går ihop.
void fallInvers()
{
    std::printf("\n=== 16. builderns invers mot den skurna arean ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);

    for (float scale : {1.0f, 2.0f}) {
        glaze::Ctx c = ctxFör(st, f, scale,
                              gfx::Rectf{5 * scale, 7 * scale, 400 * scale,
                                         300 * scale});

        Yta rot;
        glaze::Panel meny, status, mål;
        meny.dock     = glaze::Dock::Top;
        meny.height   = 24;
        status.dock   = glaze::Dock::Bottom;
        status.height = 18;
        mål.width     = 60;
        mål.height    = 30;
        rot.adopt(meny);
        rot.adopt(status);
        rot.adopt(mål);

        const glaze::Ctx cc = barnCtx(rot, c);
        // FÖRVÄNTAT VÄRDE: exakt anropet buildern gör (`layoutArea`), med `except`
        // satt till widgeten som frågar.
        const gfx::Rectf pa =
            glaze::Widget::carveSiblingDocks(rot, &mål, cc);
        const float areaW = pa.w / scale;
        const float areaH = pa.h / scale;
        const float wid   = 60.0f;
        const float hei   = 30.0f;

        // Begärd placering i LOGISKA px relativt den SKURNA arean.
        const float nx = 40.0f, ny = 24.0f;
        const std::string s =
            "skala " + std::to_string(static_cast<double>(scale)).substr(0, 3);

        struct Fall {
            bool        left, top, right, bottom;
            const char* namn;
        };
        const Fall fall[] = {
            {true,  true,  false, false, "left+top"},
            {false, false, true,  true,  "right+bottom"},
            {true,  true,  true,  true,  "sträckt (alla fyra)"},
            {false, false, false, false, "utan ankare (centrerad)"},
        };

        for (const Fall& fa : fall) {
            mål.anchors = {fa.left, fa.top, fa.right, fa.bottom};
            // `dragTo`s formler, ordagrant.
            if (fa.left)
                mål.x = nx;
            if (fa.right)
                mål.right = areaW - nx - wid;
            if (!fa.left && !fa.right)
                mål.x = nx - (areaW - wid) / 2.0f;
            if (fa.top)
                mål.y = ny;
            if (fa.bottom)
                mål.bottom = areaH - ny - hei;
            if (!fa.top && !fa.bottom)
                mål.y = ny - (areaH - hei) / 2.0f;

            const gfx::Rectf r = mål.resolve(cc);
            okNära((r.x - pa.x) / scale, nx,
                   s + " · " + fa.namn + ": vänsterkanten hamnar där den bads");
            okNära((r.y - pa.y) / scale, ny,
                   s + " · " + fa.namn + ": överkanten hamnar där den bads");
            okNära(r.w / scale, wid, s + " · " + fa.namn + ": bredden bevaras");
            okNära(r.h / scale, hei, s + " · " + fa.namn + ": höjden bevaras");
        }

        // …och att det förväntade värdet VERKLIGEN skiljer sig från förälderns oskurna
        // area. Utan raden hade fallen ovan varit gröna även om carvingen
        // inte gjorde något — precis den sortens test som mäter noll.
        ok(pa.y > cc.area.y && pa.h < cc.area.h,
           s + ": den skurna arean SKILJER sig från förälderns");
    }

    // NÄSTLAT: samma sak ett led ner. `ctxForNode` vandrar hela kedjan,
    // och carvingen sker i det INNERSTA ledet — en panel med en egen
    // dockad remsa skär för sina egna barn, inte för fönstrets.
    {
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 400, 300});
        Yta rot;
        glaze::Panel yttre;
        yttre.x = 20; yttre.y = 10; yttre.width = 300; yttre.height = 200;
        yttre.setBorder(false);
        rot.adopt(yttre);

        glaze::Panel innreMeny, barn;
        innreMeny.dock   = glaze::Dock::Top;
        innreMeny.height = 16;
        barn.width  = 40;
        barn.height = 20;
        yttre.adopt(innreMeny);
        yttre.adopt(barn);

        const glaze::Ctx cc = barnCtx(rot, c);
        glaze::Ctx       ic = cc;
        ic.area = static_cast<const glaze::Widget&>(yttre).childArea(cc);
        const gfx::Rectf pa =
            glaze::Widget::carveSiblingDocks(yttre, &barn, ic);
        barn.anchors = {true, true, false, false};
        barn.x = 12;
        barn.y = 8;
        const gfx::Rectf r = barn.resolve(ic);
        okNära(r.x - pa.x, 12.0f, "nästlad: x mot panelens SKURNA area");
        okNära(r.y - pa.y, 8.0f, "nästlad: y mot panelens SKURNA area");
        okNära(pa.y - ic.area.y, 16.0f,
               "nästlad: panelens egen remsa skär 16, fönstrets noll");
    }
}

// ================================= 17. BUILTINS bär sina RUNTIME-NAMN
//
// `menuBar`/`statusBar` har ingen
// medlemsdeklaration och får därför ingen `name`-rad i `.glazeui` — men de
// är just de TIDIGARE syskon man oftast vill ankra mot. Utan namnen satta
// av `AppWindow`-konstruktorn kunde ett handkodat
// `rubrik.anchorTop = {"menuBar", 8};` ALDRIG träffa: `earlierSibling`
// jämför mot `Widget::name`, och namnet vore tomt.
//
// Grinden konstruerar ett `AppWindow` UTAN att visa det — tvåfas-init gör
// konstruktorn ofarlig (window_gate-prejudikatet) — och mäter både namnen
// och att ankaret FAKTISKT binder.
void fallBuiltinNamn()
{
    std::printf("\n=== 17. menuBar/statusBar bär sina runtime-namn ===\n");

    glaze::AppWindow win("grind", 400, 300);
    ok(win.menuBar.name == "menuBar",
       "menuBar.name == \"menuBar\" (fick '" + win.menuBar.name + "')");
    ok(win.statusBar.name == "statusBar",
       "statusBar.name == \"statusBar\" (fick '" + win.statusBar.name + "')");

    // …och ankaret binder. Menyraden måste vara SYNLIG för att ha en rect
    // som betyder något, och den är dold som default.
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 400, 300});

    win.menuBar.show();
    glaze::Label under("Rubrik");
    under.anchorTop = {"menuBar", 8};
    under.x         = 16;
    win.root().adopt(under);

    const gfx::Rectf rm = win.menuBar.resolve(c);
    const gfx::Rectf ru = under.resolve(c);
    okNära(ru.y, rm.y + rm.h + 8.0f,
           "handkodat ankare mot \"menuBar\" träffar (8 px under remsan)");
    ok(rm.h > 0.0f, "menyradens remsa har en höjd att ankra mot");
}

// 18. TopBar-UNDANTAGET ÄR EN INSTANS, INTE EN TYP.
//
// Namnlisten står utanför dock-flödet därför att `AppWindow` ger just DEN
// instansen ett chrome-Ctx = hela klientytan, medan menyrad och statusrad
// skär i INNEHÅLLSytan. Första versionen uttryckte det som en virtual på
// klassen `TopBar` — och då hoppade VARJE TopBar över carvingen, också en
// helt vanlig nästlad sådan. Galleriets grupp 8 har precis en sådan, så
// det var inte ett hypotetiskt fall.
//
// Grinden mäter båda riktningarna.
void fallTopBarInstans()
{
    std::printf("\n=== 18. TopBar utanför dock-flödet: INSTANS, inte typ ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 400, 300});

    // ---- a) fönstrets EGEN namnlist står stilla när menyraden visas
    glaze::AppWindow win("grind", 400, 300);
    if (glaze::TopBar* tb = win.topBar()) {
        const gfx::Rectf före = tb->resolve(c);
        win.menuBar.show();
        const gfx::Rectf efter = tb->resolve(c);
        okNära(efter.y, före.y,
               "AppWindows namnlist rör sig INTE av menyraden");
        ok(!tb->inDockFlow(), "fönstrets topBar_ står utanför dock-flödet");
    } else {
        ok(false, "fönstret saknar TopBar (Chrome::Custom förväntades)");
    }

    // ---- b) en VANLIG nästlad TopBar är ett vanligt barn och SKA skäras
    glaze::Panel   p;
    glaze::MenuBar remsa;
    glaze::TopBar  nästlad;
    remsa.dock = glaze::Dock::Top;
    remsa.show();
    p.adopt(remsa);
    p.adopt(nästlad);

    ok(nästlad.inDockFlow(),
       "en nästlad TopBar deltar i dock-flödet (typen är inte undantagen)");

    const gfx::Rectf rr = remsa.resolve(c);
    const gfx::Rectf rn = nästlad.resolve(c);
    ok(rr.h > 0.0f, "den dockade remsan har en höjd");
    okNära(rn.y, rr.y + rr.h,
           "den nästlade TopBaren börjar UNDER remsan, inte ovanpå");
}

// ================= 19. AUTOSTORLEK UR BARNEN — `Widget::childrenExtent`
//
// Regeln är en ORDNINGSKÄNSLIG INVERS av dock-carvingen, och den har två
// fällor som båda fällde utkast under planeringen:
//
//   TVÄRKRAVET   ett ensamt `Dock::Top 100×20` behöver `100×20`, inte
//                `0×20`. En remsa förbrukar tjocklek på SIN axel men
//                kräver fortfarande plats på den andra.
//   ORDNINGEN    `Fill` förbrukar ingen area, så ett `Fill` FÖRE en remsa
//                får hela den då-aktuella ytan och remsan efter överlappar
//                det. `Left, Fill` ⇒ 80×40 men `Fill, Left` ⇒ 50×40 —
//                SAMMA barn, olika svar. De två raderna nedan är
//                OBLIGATORISKA och måste asserta OLIKA värden; utan dem
//                grindas ordningskänsligheten inte alls.
//
// Svaret är LOGISKA px och därför SKALOBEROENDE — varje fall mäts i skala
// 1 OCH 2 med samma förväntat värde.
namespace {

// Ett barn med ett känt, fast mått. Basens `contentSize` svarar med
// `{width, height}` rakt av, så ingen override behövs.
struct Kloss final : glaze::Widget {
    Kloss(float w, float h)
    {
        width  = w;
        height = h;
    }
    void draw(const glaze::Ctx&) override {}
};

// TESTKOMPOSITEN: en Panel-ättling vars AUTO-axlar kommer ur barnen. Precis
// den form doc-kommentaren i widget.h beskriver för en komposit-författare.
struct Komposit final : glaze::Panel {
    // `contentSize` är protected — grinden når den via en publik metod på
    // den EGNA typen (samma trick som `MättLabel` i msgbox_gate).
    gfx::Sizef mät(const glaze::Ctx& c) const { return contentSize(c); }

protected:
    gfx::Sizef contentSize(const glaze::Ctx& c) const override
    {
        gfx::Sizef       s{width, height};
        const gfx::Sizef e = childrenExtent(c);
        if (s.w <= 0.0f) s.w = e.w;
        if (s.h <= 0.0f) s.h = e.h;
        return s;
    }
};

} // namespace

void fallKompositExtent()
{
    std::printf("\n=== 19. childrenExtent — autostorlek ur barnen ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);

    // Bygger ett fall, mäter i BÅDA skalorna och jämför mot ETT förväntat värde.
    auto mät = [&st](const std::string& vad,
                     const std::vector<std::pair<glaze::Dock, gfx::Sizef>>& barn,
                     float väntaW, float väntaH) {
        for (float scale : {1.0f, 2.0f}) {
            FalskFont  f(7.0f * scale, st.uiFontSize * scale);
            glaze::Ctx c = ctxFör(st, f, scale,
                                  gfx::Rectf{0, 0, 600 * scale, 400 * scale});

            Komposit k;
            std::vector<std::unique_ptr<Kloss>> ägda;
            for (const auto& [d, m] : barn) {
                ägda.push_back(std::make_unique<Kloss>(m.w, m.h));
                ägda.back()->dock = d;
                k.adopt(*ägda.back());
            }

            const gfx::Sizef s = k.mät(c);
            const std::string tag =
                vad + " (skala " + std::to_string(static_cast<int>(scale)) + ")";
            okNära(s.w, väntaW, tag + ": bredd");
            okNära(s.h, väntaH, tag + ": höjd");
        }
    };

    using D = glaze::Dock;

    // (1) TVÄRKRAVET — den naiva "summa av tjocklekar" ger 0×20 här.
    mät("Top 100×20", {{D::Top, {100, 20}}}, 100.0f, 20.0f);

    // (2) Samma sak på den andra axeln.
    mät("Left 30×40", {{D::Left, {30, 40}}}, 30.0f, 40.0f);

    // (3) + (4) ORDNINGEN. Samma två barn, omvänd ordning, OLIKA svar.
    mät("Left 30×10 · Fill 50×40", {{D::Left, {30, 10}}, {D::Fill, {50, 40}}},
        80.0f, 40.0f);
    mät("Fill 50×40 · Left 30×10", {{D::Fill, {50, 40}}, {D::Left, {30, 10}}},
        50.0f, 40.0f);

    // (5) Top + Fill: remsan läggs på Fillens höjd, bredden är den bredaste.
    mät("Top 100×20 · Fill 50×40", {{D::Top, {100, 20}}, {D::Fill, {50, 40}}},
        100.0f, 60.0f);

    // (6) FLERA Fill delar restytan — var och en vid sin position, alltså
    // max och inte summa.
    mät("Fill 50×40 · Fill 30×70", {{D::Fill, {50, 40}}, {D::Fill, {30, 70}}},
        50.0f, 70.0f);

    // (7) BLANDAD dockordning, baklänges: Bottom (h=15, w=80) → Left
    // (w=80+30=110, h=max(15,10)=15) → Top (h=15+20=35, w=max(110,100)=110).
    mät("Top 100×20 · Left 30×10 · Bottom 80×15",
        {{D::Top, {100, 20}}, {D::Left, {30, 10}}, {D::Bottom, {80, 15}}},
        110.0f, 35.0f);

    // (8) ODOCKADE barn mäts från sitt x/y — och ett DOLT barn räknas inte,
    // exakt som det inte skär någon remsa.
    {
        FalskFont  f(7.0f, st.uiFontSize);
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

        Komposit k;
        Kloss    lös(40, 20), dold(500, 500);
        lös.x = 10;
        lös.y = 5;
        dold.hide();
        k.adopt(lös);
        k.adopt(dold);

        const gfx::Sizef s = k.mät(c);
        okNära(s.w, 50.0f, "odockat barn: x + mätt bredd");
        okNära(s.h, 25.0f, "odockat barn: y + mätt höjd");
        okNära(s.w, 50.0f, "DOLT barn räknas inte (som remsan)");
    }

    // (8b) SYSKONANKARE — kedjan måste följas, annars är hjälparen ingen
    // invers. `b.anchorLeft = {"a", 8}` placerar b vid
    // a:s HÖGERKANT + 8, inte vid `b.x`. Ett utkast mätte alla odockade som
    // `x + mätt` och gav 100 här i stället för 158 — kompositens högra
    // 58 px hade klippts.
    {
        FalskFont  f(7.0f, st.uiFontSize);
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

        Komposit k;
        Kloss    a(100, 20), b(40, 20), cc(25, 20);
        a.name = "a";
        a.x    = 10;
        b.name = "b";
        b.x    = 0;                       // ignoreras — syskonankaret vinner
        b.anchorLeft = {"a", 8};
        cc.name = "c";
        cc.anchorLeft = {"b", 4};         // KEDJA a → b → c
        k.adopt(a);
        k.adopt(b);
        k.adopt(cc);

        const gfx::Sizef s = k.mät(c);
        // a: 10+100 = 110 · b: 110+8+40 = 158 · c: 158+4+25 = 187
        okNära(s.w, 187.0f, "syskonankarKEDJA a→b→c mäts hela vägen ut");
        okNära(s.h, 20.0f, "…och höjden är oförändrad");
    }

    // (8c) Ett HÖGERBUNDET barn ryms:
    // närkanten 0 + 50 från slutet. Ett barn
    // ankrat mot dess HÖGERKANT hamnar UTANFÖR (6 px förbi förälderns slut)
    // och bidrar inte.
    {
        FalskFont  f(7.0f, st.uiFontSize);
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

        Komposit k;
        Kloss    hoger(50, 20), efter(30, 20);
        hoger.name    = "hoger";
        hoger.anchors = {false, true, true, false};   // högerbunden: från slutet
        efter.name    = "efter";
        efter.anchorLeft = {"hoger", 6};             // utanför slutet
        k.adopt(hoger);
        k.adopt(efter);

        const gfx::Sizef s = k.mät(c);
        okNära(s.w, 50.0f, "högerbundet barn ryms; ankaret mot dess högerkant "
                           "hamnar utanför och bidrar inte");
        okNära(s.h, 20.0f, "…och y-axeln är opåverkad");
    }

    // (8d) `anchorRight` mot ett barn FRÅN BÖRJAN står FÖRE det: b är bundet
    // från vänster (`anchors.left`, x 0) och med högerkanten 4 px före a — ett
    // fast intervall [0, 1], så bara a:s 5 + 60 syns i bredden.
    {
        FalskFont  f(7.0f, st.uiFontSize);
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

        Komposit k;
        Kloss    a(60, 20), b(30, 20);
        a.name = "a";
        a.x    = 5;
        b.name = "b";
        b.anchorRight = {"a", 4};        // b:s HÖGERkant mot a:s vänstra
        k.adopt(a);
        k.adopt(b);

        const gfx::Sizef s = k.mät(c);
        okNära(s.w, 65.0f, "anchorRight mot ett barn från början står före "
                           "det — bara a räknas (5+60)");
    }

    // (9) Ett HÖGERANKRAT barn bidrar med sin närkant från slutet:
    // right 0 +
    // bredden 40.
    {
        FalskFont  f(7.0f, st.uiFontSize);
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

        Komposit k;
        Kloss    ankrad(40, 20);
        ankrad.anchors = {false, true, true, false};   // höger + topp
        ankrad.y       = 5;
        k.adopt(ankrad);

        const gfx::Sizef s = k.mät(c);
        okNära(s.w, 40.0f, "högerankrat barn bidrar i x-led med sin närkant "
                           "från slutet");
        okNära(s.h, 25.0f, "…och i y-led som förut");
    }

    // (10) SATT AXEL VINNER — axelregeln gäller kompositen precis som varje
    // annan widget; hjälparen fyller bara AUTO-axlarna.
    {
        FalskFont  f(7.0f, st.uiFontSize);
        glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

        Komposit k;
        Kloss    remsa(100, 20);
        remsa.dock = glaze::Dock::Top;
        k.adopt(remsa);
        k.width = 300.0f;

        const gfx::Sizef s = k.mät(c);
        okNära(s.w, 300.0f, "satt bredd vinner över extenten");
        okNära(s.h, 20.0f, "höjden är fortfarande AUTO ur barnen");
    }
}

// ============================ 20. HÖJD FÖR BREDD
//
// En radbruten etikett vars bredd kommer ur LAYOUTEN — sträckt eller
// Top/Bottom-dockad — ska få höjden för de rader den bryts i vid just den
// bredden (`Widget::heightForWidth`), och syskonen ska se samma remsa.
// En sådan etikett ska inte mätas som EN rad (mätordningen frågar `contentSize`
// innan bredden finns) och klippas.
//
// FÖRVÄNTAT VÄRDE ÄR RÄKNAT FÖR HAND ur ordbrytningens regel med teckensteget 10
// logiska px (fonten nedan) — aldrig ur produktionens `wrapText`. Texten
// "aaa bbb ccc ddd eee fff ggg hhh" är 31 tecken:
//   400 → 1 rad · 300, 250, 200 → 2 rader · 100 → 4 rader
// (vid 200: 20 tecken ryms, bryt vid blanksteget före "fff" → "aaa … eee" /
// "fff ggg hhh"; vid 100: "aaa bbb" / "ccc ddd" / "eee fff" / "ggg hhh").
// Radhöjden är 20. Allt i LOGISKA px och mätt i skala 1 OCH 2.
namespace {

// En font som BRYTER: fast teckensteg per kodpunkt, `fitWidth` som räknar
// hela tecken som ryms (msgbox_gates `FakeFont`). `FalskFont` ovan bryter
// aldrig och duger därför inte här. Fonten talar FYSISKA px.
class BrytFont final : public gfx::NullFont {
public:
    BrytFont(float adv, float h) : adv_(adv), h_(h) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv_ * static_cast<float>(tecken(s, s.size())), h_};
    }
    float height() const override { return h_; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return adv_ * static_cast<float>(tecken(s, bytes));
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size() && w + adv_ / 2.0f < x) {
            i = nästa(s, i);
            w += adv_;
        }
        return i;
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size() && w + adv_ <= maxW) {
            i = nästa(s, i);
            w += adv_;
        }
        return i;
    }

private:
    static std::size_t nästa(const std::string& s, std::size_t i)
    {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            ++i;
        return i;
    }
    static std::size_t tecken(const std::string& s, std::size_t bytes)
    {
        if (bytes > s.size())
            bytes = s.size();
        std::size_t n = 0;
        for (std::size_t i = 0; i < bytes; i = nästa(s, i))
            ++n;
        return n;
    }
    float adv_;
    float h_;
};

const char* const kBrytText = "aaa bbb ccc ddd eee fff ggg hhh";
constexpr float   kRadH     = 20.0f;

void brytEtikett(glaze::Label& l, const char* namn)
{
    l.setText(kBrytText);
    l.setWrap(true);
    l.name = namn;
}

} // namespace

void fallHojdForBredd()
{
    std::printf("\n=== 20. höjd för bredd — radbruten etikett följer layoutens "
                "bredd ===\n");

    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);

    for (float scale : {1.0f, 2.0f}) {
        BrytFont    f(10.0f * scale, kRadH * scale);
        const auto  ctx = [&](float w, float h) {
            return ctxFör(st, f, scale, gfx::Rectf{0, 0, w * scale, h * scale});
        };
        const std::string sk =
            " (skala " + std::to_string(static_cast<int>(scale)) + ")";

        // (a) + (b) STRÄCKT etikett: höjden = rader × radhöjd vid den bredd
        // layouten GER, och ett syskon med anchorTop flyttar med.
        {
            Yta          rot;
            glaze::Label l;
            brytEtikett(l, "etikett");
            l.anchors = {true, true, true, false};
            Kloss under(50, 10);
            under.anchorTop = {"etikett", 4};
            rot.adopt(l);
            rot.adopt(under);
            const struct {
                float bredd;
                int   rader;
            } fall[] = {{200, 2}, {400, 1}, {100, 4}};
            for (const auto& k : fall) {
                const glaze::Ctx cc = barnCtx(rot, ctx(k.bredd, 300));
                const gfx::Rectf r  = l.resolve(cc);
                const std::string b =
                    " vid " + std::to_string(static_cast<int>(k.bredd)) + sk;
                okNära(r.w / scale, k.bredd, "(a) sträckt: bredden ur layouten" + b);
                okNära(r.h / scale, float(k.rader) * kRadH,
                       "(a) sträckt: höjden = " + std::to_string(k.rader) +
                           " rader" + b);
                okNära(under.resolve(cc).y / scale, float(k.rader) * kRadH + 4.0f,
                       "(b) syskonet med anchorTop flyttar med" + b);
            }
            // BARA FÖRÄLDERNS HÖJD ändras: höjden får inte ändras.
            const float h1 = l.resolve(barnCtx(rot, ctx(200, 300))).h;
            const float h2 = l.resolve(barnCtx(rot, ctx(200, 600))).h;
            okNära(h2 / scale, h1 / scale,
                   "(a) sträckt: förälderns höjd ändrar inte etikettens" + sk);
        }

        // (c) TOP-DOCKAD: remsan = kroken mot area.w, och ett odockat syskon
        // börjar under den. Samma remsa i en högre förälder.
        {
            Yta          rot;
            glaze::Label l;
            brytEtikett(l, "topp");
            l.dock = glaze::Dock::Top;
            glaze::Panel lös;
            lös.anchors = {true, true, true, true};
            rot.adopt(l);
            rot.adopt(lös);
            for (float h : {300.0f, 600.0f}) {
                const glaze::Ctx cc = barnCtx(rot, ctx(200, h));
                const std::string b =
                    " (höjd " + std::to_string(static_cast<int>(h)) + ")" + sk;
                okNära(l.resolve(cc).h / scale, 2.0f * kRadH,
                       "(c) Top-dockad: remsan är två rader vid 200" + b);
                okNära(lös.resolve(cc).y / scale, 2.0f * kRadH,
                       "(c) det odockade syskonet börjar under remsan" + b);
                okNära(lös.resolve(cc).h / scale, h - 2.0f * kRadH,
                       "(c) …och tar resten" + b);
            }
        }

        // (d) en LEFT-remsa FÖRE Top-etiketten: krokens bredd = arean minus
        // remsan (400 − 100 = 300 → två rader; hela arean hade gett en).
        {
            Yta          rot;
            Kloss        vänster(100, 10);
            glaze::Label l;
            glaze::Panel lös;
            vänster.dock = glaze::Dock::Left;
            brytEtikett(l, "topp");
            l.dock      = glaze::Dock::Top;
            lös.anchors = {true, true, true, true};
            rot.adopt(vänster);
            rot.adopt(l);
            rot.adopt(lös);
            const glaze::Ctx cc = barnCtx(rot, ctx(400, 300));
            const gfx::Rectf r  = l.resolve(cc);
            okNära(r.x / scale, 100.0f, "(d) Top-etiketten börjar efter Left-remsan" + sk);
            okNära(r.w / scale, 300.0f, "(d) …och är 300 bred" + sk);
            okNära(r.h / scale, 2.0f * kRadH,
                   "(d) krokens bredd är arean MINUS remsan — två rader" + sk);
            okNära(lös.resolve(cc).y / scale, 2.0f * kRadH,
                   "(d) det odockade syskonet ser samma remsa" + sk);
        }

        // (e) SATT height vinner; (f) wrap av ger oförändrat beteende.
        {
            Yta          rot;
            glaze::Label satt, av;
            brytEtikett(satt, "satt");
            satt.anchors = {true, true, true, false};
            satt.height  = 50;
            av.setText(kBrytText);
            av.name    = "av";
            av.anchors = {true, true, true, false};
            av.y       = 100;
            rot.adopt(satt);
            rot.adopt(av);
            const glaze::Ctx cc = barnCtx(rot, ctx(100, 300));
            okNära(satt.resolve(cc).h / scale, 50.0f,
                   "(e) satt height vinner över de fyra raderna" + sk);
            okNära(av.resolve(cc).h / scale, kRadH,
                   "(f) utan wrap: en rad, som förut" + sk);
        }

        // (h) BOTTENBUNDEN sträckt etikett: den växer UPPÅT från sin kant —
        // py = B − h, med h ur kroken.
        {
            Yta          rot;
            glaze::Label l;
            brytEtikett(l, "botten");
            l.anchors = {true, false, true, true};
            l.bottom  = 10;
            rot.adopt(l);
            const glaze::Ctx cc = barnCtx(rot, ctx(200, 300));
            const gfx::Rectf r  = l.resolve(cc);
            okNära(r.h / scale, 2.0f * kRadH, "(h) bottenbunden: två rader" + sk);
            okNära(r.y / scale, 300.0f - 10.0f - 2.0f * kRadH,
                   "(h) bottenbunden: py = B − h" + sk);
        }

        // BOTTOM-DOCKAD: remsan längst ned, lika hög som raderna.
        {
            Yta          rot;
            glaze::Label l;
            glaze::Panel lös;
            brytEtikett(l, "fot");
            l.dock      = glaze::Dock::Bottom;
            lös.anchors = {true, true, true, true};
            rot.adopt(l);
            rot.adopt(lös);
            const glaze::Ctx cc = barnCtx(rot, ctx(200, 300));
            const gfx::Rectf r  = l.resolve(cc);
            okNära(r.h / scale, 2.0f * kRadH, "Bottom-dockad: två rader" + sk);
            okNära(r.y / scale, 300.0f - 2.0f * kRadH,
                   "Bottom-dockad: remsan i nederkanten" + sk);
            okNära(lös.resolve(cc).h / scale, 300.0f - 2.0f * kRadH,
                   "Bottom-dockad: det odockade slutar ovanför den" + sk);
        }

        // BLANDADE REMSOR: Left 100 · Top-etikett · Right 100 ·
        // Bottom-etikett · Fill · odockad, i en 400 × 400-area. Topp-
        // etiketten bryts mot 300 (två rader), bottenetiketten mot 200 (två
        // rader) — och Fill och det odockade syskonet är SAMMA yta, för
        // samma carving gäller för båda.
        {
            Yta          rot;
            Kloss        vänster(100, 10), höger(100, 10);
            glaze::Label topp, botten;
            glaze::Panel fyll, lös;
            vänster.dock = glaze::Dock::Left;
            höger.dock   = glaze::Dock::Right;
            brytEtikett(topp, "topp");
            brytEtikett(botten, "botten");
            topp.dock   = glaze::Dock::Top;
            botten.dock = glaze::Dock::Bottom;
            fyll.dock   = glaze::Dock::Fill;
            lös.anchors = {true, true, true, true};
            rot.adopt(vänster);
            rot.adopt(topp);
            rot.adopt(höger);
            rot.adopt(botten);
            rot.adopt(fyll);
            rot.adopt(lös);
            const glaze::Ctx cc = barnCtx(rot, ctx(400, 400));
            const gfx::Rectf rt = topp.resolve(cc);
            const gfx::Rectf rb = botten.resolve(cc);
            const gfx::Rectf rf = fyll.resolve(cc);
            const gfx::Rectf rl = lös.resolve(cc);
            okNära(rt.w / scale, 300.0f, "blandat: topp-etiketten 300 bred" + sk);
            okNära(rt.h / scale, 2.0f * kRadH, "blandat: topp-etiketten två rader" + sk);
            okNära(rb.x / scale, 100.0f, "blandat: botten-etiketten efter Left" + sk);
            okNära(rb.w / scale, 200.0f, "blandat: botten-etiketten 200 bred" + sk);
            okNära(rb.y / scale, 400.0f - 2.0f * kRadH,
                   "blandat: botten-etiketten i nederkanten, två rader" + sk);
            okNära(rf.x / scale, 100.0f, "blandat: Fill x" + sk);
            okNära(rf.y / scale, 2.0f * kRadH, "blandat: Fill y" + sk);
            okNära(rf.w / scale, 200.0f, "blandat: Fill bredd" + sk);
            okNära(rf.h / scale, 400.0f - 4.0f * kRadH, "blandat: Fill höjd" + sk);
            const auto lika = [](float a, float b) {
                return (a > b ? a - b : b - a) < 0.01f;
            };
            ok(lika(rl.x, rf.x) && lika(rl.y, rf.y) && lika(rl.w, rf.w) &&
                   lika(rl.h, rf.h),
               "blandat: det odockade syskonet = Fill — samma carving" + sk);
        }
    }
}

} // namespace

// ===================== 21. AUTOSTORLEK MED BREDDEN GIVEN
//
// En widget vars höjd är `6000 / bredd` — kroken svarar alltid, och måttet
// utan kroken är 0 × 0, så varje tal nedan kommer ur kroken.
struct HojdAvBredd final : glaze::Widget {
    std::optional<float> heightForWidth(const glaze::Ctx&,
                                        float w) const override
    {
        return w > 0.0f ? std::optional<float>(6000.0f / w) : std::nullopt;
    }
    void draw(const glaze::Ctx&) override {}
};

// `childrenExtent` är skyddad — grinden når båda formerna härifrån.
struct Matare final : glaze::Widget {
    gfx::Sizef med(const glaze::Ctx& c, float w) const
    {
        return childrenExtent(c, w);
    }
    gfx::Sizef utan(const glaze::Ctx& c) const { return childrenExtent(c); }
    void       draw(const glaze::Ctx&) override {}
};

void fallExtentMedBredd()
{
    std::printf("\n=== 21. childrenExtent med bredden given ===\n");
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(8.0f, 16.0f);
    const glaze::Ctx    c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 300, 400});

    {   // (a) Top-dockad i bredden 300 → 20; utan bredd 0 (oförändrat)
        Matare      m;
        HojdAvBredd h;
        h.dock = glaze::Dock::Top;
        m.adopt(h);
        okNära(m.med(c, 300.0f).h, 20.0f, "(a) Top-dockad: 6000 / 300");
        okNära(m.utan(c).h, 0.0f, "(a) utan bredd: kroken frågas inte");
    }
    {   // (b) en Left-remsa på 100 FÖRE den → remsbredden 200 → 30
        Matare      m;
        Kloss       v(100, 10);
        HojdAvBredd h;
        v.dock = glaze::Dock::Left;
        h.dock = glaze::Dock::Top;
        m.adopt(v);
        m.adopt(h);
        const gfx::Sizef e = m.med(c, 300.0f);
        okNära(e.h, 30.0f, "(b) Left-remsan före dras av: 6000 / 200");
        okNära(e.w, 100.0f, "(b) bredden: remsan");
    }
    {   // (c) sträckt odockad, x 10 och right 10 → 6000 / 280
        Matare      m;
        HojdAvBredd h;
        h.anchors = {true, true, true, false};
        h.x       = 10;
        h.right   = 10;
        m.adopt(h);
        okNära(m.med(c, 300.0f).h, 6000.0f / 280.0f,
               "(c) sträckt odockad: restbredden minus offseten");
        okNära(m.utan(c).h, 0.0f, "(c) utan bredd: oförändrat");
    }
}

// ============== 22. INVERSEN ÅT BÅDA HÅLLEN
//
// Kanterna räknas FRÅN BÖRJAN och FRÅN SLUTET: rader mot nederkanten står
// UNDER det som flödar uppifrån, ett spänt barn bidrar med sitt mätta mått
// som minsta, `right`/`bottom` på ett barn bundet bara i början är LUFT, i
// sidled läggs bara de par ihop som inte står över varandra, och ett dolt led
// i en kedja lämnar sitt hål. Förväntat värde är HANDRÄKNAT ur de satta fälten, aldrig
// ur `resolve`. Klossarna svarar med sina satta mått, så svaret är LOGISKT och
// skaloberoende: (a) och (c) mäts i skala 1 och 2 med samma förväntat värde.
namespace {

// Fall 22:s FIXTUR: `a` 100×20 på (10, 10) och `ok` 60×26 nere till höger
// med right/bottom 12. `ok` adopteras före det som ankras mot den — ett
// syskonankare pekar bara bakåt. Matare först, så barnen dör före den.
struct Dialog {
    Matare m;
    Kloss  a{100, 20};
    Kloss  ok{60, 26};
    Dialog()
    {
        a.name     = "a";
        a.x        = 10;
        a.y        = 10;
        ok.name    = "ok";
        ok.anchors = {false, false, true, true};
        ok.right   = 12;
        ok.bottom  = 12;
        m.adopt(a);
        m.adopt(ok);
    }
};

} // namespace

void fallInversBadaHallen()
{
    std::printf("\n=== 22. inversen åt båda hållen ===\n");
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const auto skala = [](float s) {
        return " (skala " + std::to_string(static_cast<int>(s)) + ")";
    };

    for (float s : {1.0f, 2.0f}) {
        FalskFont        f(7.0f * s, st.uiFontSize * s);
        const glaze::Ctx c = ctxFör(st, f, s, gfx::Rectf{0, 0, 600 * s, 400 * s});

        {   // (a) fälten uppifrån, OK nere till höger: höjden 30 + 38, bredden
            // max(110, 72) — de står i olika rader
            Dialog           d;
            const gfx::Sizef e = d.m.utan(c);
            okNära(e.h, 68.0f, "(a) höjd: flödet uppifrån 30 + nedifrån 38" + skala(s));
            okNära(e.w, 110.0f, "(a) bredd: max(110, 72) — olika rader" + skala(s));
        }
        {   // (c) luften a.bottom = 6 ⇒ 74; ett gammalt y på den slutbundna
            // knappen räknas inte
            Dialog d;
            d.a.bottom = 6;
            okNära(d.m.utan(c).h, 74.0f, "(c) luften a.bottom = 6: 36 + 38" + skala(s));
            d.ok.y = 300;
            okNära(d.m.utan(c).h, 74.0f,
                   "(c) ok.y = 300 på en slutbunden räknas inte" + skala(s));
        }
    }

    FalskFont        f(7.0f, st.uiFontSize);
    const glaze::Ctx c = ctxFör(st, f, 1.0f, gfx::Rectf{0, 0, 600, 400});

    {   // (b) + avbryt 50×26 till vänster om ok (8 px), nedåt 12: närkanten
        // 12 + 60 + 8 + 50 = 130 från slutet
        Dialog d;
        Kloss  avbryt(50, 26);
        avbryt.name        = "avbryt";
        avbryt.anchors     = {false, false, false, true};
        avbryt.anchorRight = {"ok", 8};
        avbryt.bottom      = 12;
        d.m.adopt(avbryt);
        const gfx::Sizef e = d.m.utan(c);
        okNära(e.w, 130.0f, "(b) bredd: avbryt kedjad från slutet, 130");
        okNära(e.h, 68.0f, "(b) höjd oförändrad 68");
    }
    {   // (d) en lista SPÄND mellan a (+ 8) och ok (+ 12): 38 + mått + 50;
        // (f) och i sidled står den ÖVER knapparna (regel b) — 110, inte 182
        Dialog d;
        Kloss  lista(100, 0);
        lista.name         = "lista";
        lista.x            = 10;
        lista.anchors      = {true, true, false, true};
        lista.anchorTop    = {"a", 8};
        lista.anchorBottom = {"ok", 12};
        d.m.adopt(lista);
        const gfx::Sizef e = d.m.utan(c);
        okNära(e.h, 88.0f, "(d) spänd utan höjd: 38 + 0 + 50");
        okNära(e.w, 110.0f, "(f) listan står över knapparna — max, inte 182");
        lista.height = 120;
        okNära(d.m.utan(c).h, 208.0f, "(d) spänd med height 120: 38 + 120 + 50");
    }
    {   // (e) "Namn" uppe till vänster och "Hjälp" uppe till höger delar rad:
        // bredden är summan; nedåt flyttad står den i en annan rad
        Matare m;
        Kloss  lbl(80, 20), hjalp(40, 20);
        lbl.name      = "lbl";
        lbl.x         = 10;
        lbl.y         = 10;
        hjalp.name    = "hjalp";
        hjalp.anchors = {false, true, true, false};
        hjalp.right   = 10;
        hjalp.y       = 10;
        m.adopt(lbl);
        m.adopt(hjalp);
        gfx::Sizef e = m.utan(c);
        okNära(e.w, 140.0f, "(e) samma rad: 90 + 50");
        okNära(e.h, 30.0f, "(e) höjd 30");
        hjalp.anchors = {false, false, true, true};
        hjalp.bottom  = 10;
        e = m.utan(c);
        okNära(e.w, 90.0f, "(e) nedåt: olika rader — max(90, 50)");
        okNära(e.h, 60.0f, "(e) nedåt: 30 + 30");
    }
    {   // (g) a → b (DOLD) → c: kedjan går genom hålet, som `resolve`
        Matare m;
        Kloss  a(100, 20), b(100, 20), cc(100, 20);
        a.name       = "a";
        a.x          = 10;
        a.y          = 10;
        b.name       = "b";
        b.anchorTop  = {"a", 6};
        cc.name      = "c";
        cc.anchorTop = {"b", 6};
        b.hide();
        m.adopt(a);
        m.adopt(b);
        m.adopt(cc);
        okNära(m.utan(c).h, 82.0f, "(g) det dolda ledet lämnar sitt hål: 82");
    }
    {   // (h) bredden 300: höjd för bredd också mot ett syskon FRÅN SLUTET —
        // pw = 300 − 10 − 80 = 210, höjd 10 + 6000/210 + 38
        Matare      m;
        Kloss       ok(60, 26);
        HojdAvBredd text;
        ok.name          = "ok";
        ok.anchors       = {false, false, true, true};
        ok.right         = 12;
        ok.bottom        = 12;
        text.name        = "text";
        text.x           = 10;
        text.y           = 10;
        text.anchors     = {true, true, false, false};
        text.anchorRight = {"ok", 8};
        m.adopt(ok);
        m.adopt(text);
        okNära(m.med(c, 300.0f).h, 10.0f + 6000.0f / 210.0f + 38.0f,
               "(h) kroken mot ett syskon från slutet: 76,571");
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("layout_gate — placeringens geometri (headless)\n");

    fallStilbyte();
    fallFyraKanter();
    fallMåletVäxer();
    fallSträckning();
    fallMiss();
    fallDockatMål();
    fallBar();
    fallSkala();
    fallDoltMål();
    fallGammalSemantik();
    fallDockCarving();
    fallDockDold();
    fallDockAnkare();
    fallDockKontrakt();
    fallDockSkala();
    fallInvers();
    fallBuiltinNamn();
    fallTopBarInstans();
    fallKompositExtent();
    fallHojdForBredd();
    fallExtentMedBredd();
    fallInversBadaHallen();

    std::printf("\n%d fall, %d fel\n", g_fall, g_fel);
    return g_fel == 0 ? 0 : 1;
}
