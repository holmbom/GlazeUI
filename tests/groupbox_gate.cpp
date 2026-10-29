// groupbox_gate — GRUPPRAMENS grind.
//
// Kör:  ./build/tests/groupbox_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Headless: inga fönster, ingen pump, ingen `gfx::init`. Mäter de tre saker
// gruppramen gör som ingen annan widget gör, mot HANDRÄKNADE förväntade värden:
//
//   1. INNERYTAN. Barnen bor i ramen minus `groupPad`, under HELA
//      rubrikbandet — och ytan flyttar sig inte när rubriken töms. Klippet
//      är innerytan, och radien är KONCENTRISK (`groupRadius − groupPad`,
//      mätt med en egen stil så formeln och inte stilens förvalda tal grindas).
//   2. AUTOSTORLEKEN UR BARNEN — kompositstödets opt-in (`childrenExtent`),
//      första användaren: barnens utsträckning plus luften, minst så bred
//      som rubriken, PER AXEL, och oberoende av förälderns yta (en dockad
//      ram hade annars fått en remstjocklek som beror på frågeordningen).
//   3. LUCKAN I LINJEN — `Draw::frameGap`, spelad in ur en test-renderer:
//      konturen ritas i tre klipp, INGET av dem täcker överkanten inne i
//      luckan, och nederkanten under luckan ritas ändan. Utan rubrik: en
//      enda kontur och inget klipp.
//
// VAD DEN INTE MÄTER: hur det SER UT (pixlar — pixel_gate mäter att ramen
// ritar och att rubriken syns, ögat via glaze-render resten), och sömmen
// som snäppningen av luckans kanter tar bort (den syns bara i en riktig
// scissor; verifierad renderad i 2.5×, se draw.cpp).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
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

bool nära(float a, float b) { return std::fabs(a - b) < 0.01f; }

std::string rekt(const gfx::Rectf& r)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "{%g, %g, %g, %g}", r.x, r.y, r.w, r.h);
    return buf;
}

bool lika(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return nära(a.x, b.x) && nära(a.y, b.y) && nära(a.w, b.w) &&
           nära(a.h, b.h);
}

// Fast teckensteg och fast radhöjd — de förväntade värdena räknas i hela tal.
class FalskFont final : public gfx::NullFont {
public:
    FalskFont(float adv, float h) : adv_(adv), h_(h) {}
    gfx::Sizef measure(const std::string& s) const override
    {
        std::size_t n = 0;
        for (char ch : s)
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
                ++n;
        return gfx::Sizef{adv_ * static_cast<float>(n), h_};
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

struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

// Teckensteg 7, radhöjd 13 (fysiska px vid skala 1 = logiska).
glaze::Ctx ctxFör(const glaze::Style& st, gfx::Font& f,
                  gfx::Rectf area = {0.0f, 0.0f, 600.0f, 400.0f})
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = 1.0f;
    c.ui          = glaze::UI::Slate;
    c.style       = &st;
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = area;
    return c;
}

// ------------------------------------------------------------ 0. registret

void fallRegistret()
{
    auto w = glaze::makeWidget("groupbox");
    check(w && dynamic_cast<glaze::GroupBox*>(w.get()) != nullptr,
          "registret: \"groupbox\" ger en riktig glaze::GroupBox");
    check(w && !w->focusable(), "gruppramen är inget tabbstopp");
    check(w && w->clipsChildren(), "gruppramen klipper alltid sina barn");

    glaze::GroupBox g;
    check(g.setProp("text", "Rubrik") && g.text() == "Rubrik",
          "setProp: text");
    check(!g.setProp("title", "x"), "setProp: inget title-alias");
    check(!g.setProp("border", "true") && !g.setProp("clip", "true"),
          "setProp: Panels border/clip finns INTE — gruppramen är ingen Panel");
    check(g.setProp("visible", "false") && !g.isVisible(),
          "setProp: basens nycklar nås");
    glaze::setLanguage("sv");
    check(g.setTextToken("text", glaze::tr(glaze::str::Cancel)) &&
              g.textToken().id() == glaze::str::Cancel &&
              g.text() == "Avbryt",
          "setTextToken: rubriken bär TOKENET", g.text());
}

// --------------------------------------------------------- 1. innerytan

void fallInneryta()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    check(nära(st.groupPad, 8.0f) && nära(st.groupRadius, 4.0f),
          "facit: Slates gruppmått är de facit räknats för");
    FalskFont        f(7.0f, 13.0f);
    const glaze::Ctx c = ctxFör(st, f);

    glaze::GroupBox g("Rubrik");
    g.width  = 200;
    g.height = 100;
    const glaze::Widget& w = g;

    // Ramen 0..200 × 0..100; rubrikbandet 13 högt; luften 8 — innerytan
    // börjar under bandet + luften (21) och slutar luften ovanför botten
    // (92), alltså 71 hög.
    const gfx::Rectf inre = w.childArea(c);
    check(lika(inre, {8.0f, 21.0f, 184.0f, 71.0f}),
          "innerytan: luften på sidorna och i botten, HELA bandet upptill",
          rekt(inre));
    check(lika(w.clipRect(c), inre), "klippet ÄR innerytan",
          rekt(w.clipRect(c)));
    check(nära(w.clipRadius(c), 0.0f),
          "klippradien med Slates tal: luften > radien ⇒ rakt klipp");

    // Tom rubrik flyttar INTE innehållet.
    g.setText("");
    check(lika(w.childArea(c), inre),
          "tom rubrik: innerytan står kvar på samma ställe",
          rekt(w.childArea(c)));

    // Ett barn i (0,0) hamnar i innerytans hörn.
    auto* l = g.add<glaze::Label>("Barn");
    glaze::Ctx cc = c;
    cc.area       = w.childArea(c);
    const gfx::Rectf lr = l->resolve(cc);
    check(nära(lr.x, 8.0f) && nära(lr.y, 21.0f),
          "barnet i (0,0) sitter i innerytans hörn", rekt(lr));

    // KONCENTRISK radie — mätt med en EGEN stil, så det är formeln och
    // inte stilens förvalda tal som grindas: 12 − 4 = 8, i FYSISKA px (skala 2).
    glaze::Style egen = st;
    egen.groupRadius  = 12.0f;
    egen.groupPad     = 4.0f;
    FalskFont  f2(14.0f, 26.0f);
    glaze::Ctx c2 = ctxFör(egen, f2);
    c2.scale      = 2.0f;
    check(nära(w.clipRadius(c2), 16.0f),
          "klippradien är koncentrisk: (groupRadius − groupPad) × skala",
          std::to_string(w.clipRadius(c2)));
}

// ------------------------------------------------- 2. autostorlek ur barnen

void fallAutostorlek()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, 13.0f);
    const glaze::Ctx    c = ctxFör(st, f);

    // Knapp "OK": 2·7 + 2·12 = 38 × (13 + 2·6) = 25, i (0,0).
    // Kryssruta "Ja" i (0,30): 18 + 8 + 14 = 40 × max(18, 13) = 18.
    // Utsträckning: 40 × 48. Ramen: 40 + 2·8 = 56 × 48 + 13 + 2·8 = 77.
    glaze::GroupBox g;
    g.add<glaze::Button>("OK");
    auto* kryss = g.add<glaze::Checkbox>("Ja");
    kryss->y    = 30;

    gfx::Sizef s = Meter::measure(g, c);
    check(nära(s.w, 56.0f) && nära(s.h, 77.0f),
          "auto: barnens utsträckning + luften (utan rubrik)",
          std::to_string(s.w) + "×" + std::to_string(s.h));

    // En LÅNG rubrik vinner bredden: 18 tecken · 7 + 2·(8 + 4) = 150.
    g.setText("Rubriken är längre");
    s = Meter::measure(g, c);
    check(nära(s.w, 150.0f) && nära(s.h, 77.0f),
          "auto: minst så bred som rubriken med lucka och indrag",
          std::to_string(s.w) + "×" + std::to_string(s.h));

    // PER AXEL: satt bredd vinner, höjden är fortfarande auto.
    g.width = 300;
    s       = Meter::measure(g, c);
    check(nära(s.w, 300.0f) && nära(s.h, 77.0f),
          "per axel: satt bredd vinner, höjden är auto",
          std::to_string(s.w) + "×" + std::to_string(s.h));
    g.width = 0;

    // Tom ram: aldrig noll — bandet och luften finns alltid.
    glaze::GroupBox tom;
    const gfx::Sizef t = Meter::measure(tom, c);
    check(nära(t.w, 16.0f) && nära(t.h, 29.0f),
          "tom ram: luften och bandet, aldrig 0×0",
          std::to_string(t.w) + "×" + std::to_string(t.h));

    // OBEROENDE AV FÖRÄLDERNS YTA — kontraktet för en dockad widget: en
    // remstjocklek får inte bero på arean längs skäraxeln.
    FalskFont        f2(7.0f, 13.0f);
    const glaze::Ctx smal = ctxFör(st, f2, {0.0f, 0.0f, 90.0f, 60.0f});
    const gfx::Sizef s2   = Meter::measure(g, smal);
    check(nära(s2.w, 150.0f) && nära(s2.h, 77.0f),
          "auto-måttet beror inte på förälderns yta",
          std::to_string(s2.w) + "×" + std::to_string(s2.h));
}

// ------------------------------------------------------ 3. luckan i linjen

// Renderer som spelar in konturerna OCH vilket klipp som gällde när de
// ritades. `applyClip` är den virtuella kroken klippstacken går genom; resten
// är NullRendererns no-op.
struct Inspelning : gfx::NullRenderer {
    struct Kontur {
        gfx::Rectf r;
        bool       klippt = false;
        gfx::Rectf klipp;
    };
    std::vector<Kontur> konturer;
    bool                klippt = false;
    gfx::Rectf          klipp;

    void strokeRoundedRect(const gfx::Rectf& r, float, gfx::Color,
                           float) override
    {
        konturer.push_back({r, klippt, klipp});
    }

protected:
    void applyClip(const gfx::Rectf* r, const gfx::RoundedClip*,
                   std::size_t) override
    {
        klippt = r != nullptr;
        if (r)
            klipp = *r;
    }
};

bool innehåller(const gfx::Rectf& r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void fallLuckan()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, 13.0f);

    auto rita = [&](glaze::GroupBox& g) {
        Inspelning rec;
        glaze::Ctx c = ctxFör(st, f);
        c.r          = &rec;
        c.box        = gfx::Rectf{0.0f, 0.0f, 200.0f, 100.0f};
        g.render(c);
        return rec.konturer;
    };

    // Ramen går från bandets mitt (6.5) till botten (100). Luckan är
    // [8, 8 + 6·7 + 2·4) = [8, 58) — rubriken "Rubrik" är sex tecken.
    glaze::GroupBox g("Rubrik");
    const auto k = rita(g);
    check(k.size() == 3, "med rubrik: konturen ritas i TRE klipp",
          std::to_string(k.size()));
    bool överkantILuckan = false, nederkantUnderLuckan = false,
         alltKlippt      = true;
    for (const auto& x : k) {
        alltKlippt = alltKlippt && x.klippt;
        if (!x.klippt)
            continue;
        if (innehåller(x.klipp, 33.0f, 6.5f))
            överkantILuckan = true;
        if (innehåller(x.klipp, 33.0f, 99.5f))
            nederkantUnderLuckan = true;
    }
    check(alltKlippt, "varje del av konturen ritas under ett klipp");
    check(!överkantILuckan,
          "INGET klipp täcker överkanten inne i luckan — linjen är bruten");
    check(nederkantUnderLuckan,
          "nederkanten UNDER luckan ritas ändå (luckan rör bara överkanten)");
    bool överkantUtanför = false;
    for (const auto& x : k)
        if (x.klippt && innehåller(x.klipp, 100.0f, 6.5f))
            överkantUtanför = true;
    check(överkantUtanför, "överkanten till höger om luckan ritas");

    // Utan rubrik: en vanlig ram — ett anrop, inget klipp.
    glaze::GroupBox utan;
    const auto u = rita(utan);
    check(u.size() == 1 && !u[0].klippt,
          "utan rubrik: EN kontur och inget klipp",
          std::to_string(u.size()));
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("groupbox_gate — gruppramen (headless)\n");

    fallRegistret();
    fallInneryta();
    fallAutostorlek();
    fallLuckan();

    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "GROUPBOX_GATE GRÖN" : "GROUPBOX_GATE FÄLLD",
                g_cases, g_fails);
    return g_fails == 0 ? 0 : 1;
}
