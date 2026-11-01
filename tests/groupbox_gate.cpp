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
//   4. MÄTCACHEN — en ram i en ram mäter sina barn EN gång per nyckel, och en
//      ändring i delträdet (`invalidate()`, text, synlighet) når den yttre
//      ramen. SABOTAGE: en `measure` som går förbi cachen (räknaren i 4 blir 3
//      i stället för 1), en `invalidate()` som inte stegar förfäderna (den
//      yttre ramens mått ligger kvar), och en `setText` som inte
//      invaliderar (rubrikbredden uteblir).
//
//   5. RADBRYTNINGEN — ett radbrutet barn räknas med sina rader när ramens
//      bredd är känd: en SATT bredd (innerbredden = bredden − 2·groupPad), och
//      `heightForWidth` för en bredd layouten ger (sträckt, Top-dockad, ram i
//      ram). Förväntat värde är handräknat ur ordbrytningens regel med
//      teckensteget 10 och radhöjden 20 (texten "aaa bbb ccc ddd eee fff ggg
//      hhh": 200 → 2 rader, 100 → 4, 400 → 1). Auto-bredd (bredden okänd) är
//      EN rad, som förut; en satt `height` vinner; ram i ram följer
//      innerbredden genom båda nivåerna; och mätcachen återanvänder svaret per
//      bredd men mäter om vid ett textbyte. SABOTAGE: en `contentSize` som
//      mäter barnen utan bredd (5a: 56 i stället för 76), en `heightForWidth`
//      som glömmer luften eller bandet (5e), en som inte ger nullopt vid satt
//      höjd (5d), en innerbredd utan `− 2·groupPad` (5a: bredden 116 ger tre
//      rader i stället för fyra, 96 i stället för 116), en nyckel i mätcachen utan delträdets
//      generation (5g: gammal höjd efter textbytet) och en `childrenExtent` som
//      inte tar kroken för ett spänt barn (5f).
//
// VAD DEN INTE MÄTER: hur det SER UT (pixlar — pixel_gate mäter att ramen
// ritar och att rubriken syns, ögat via glaze-render resten), och sömmen
// som snäppningen av luckans kanter tar bort (den syns bara i en riktig
// scissor; verifierad renderad i 2.5×, se draw.cpp).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
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
    using glaze::Widget::measureHeightForWidth;
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

// ------------------------------------------------------ 4. mätcachen

// Ett löv med ett fast mått och en räknare: facit för hur många gånger
// `contentSize` faktiskt kördes.
struct Räknande final : glaze::Widget {
    Räknande(float w, float h) : bw(w), bh(h) {}
    void        draw(const glaze::Ctx&) override {}
    float       bw, bh;
    mutable int mätningar = 0;

protected:
    gfx::Sizef contentSize(const glaze::Ctx&) const override
    {
        ++mätningar;
        return gfx::Sizef{bw, bh};
    }
};

void fallMatcache()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, 13.0f);
    const glaze::Ctx    c = ctxFör(st, f);

    // Yttre ram ⊃ inre ram ⊃ löv 40×20. Inre: 40 + 16 = 56 × 20 + 13 + 16 = 49.
    // Yttre: 56 + 16 = 72 × 49 + 13 + 16 = 78.
    glaze::GroupBox yttre, inre;
    Räknande        löv(40.0f, 20.0f);
    inre.adopt(löv);
    yttre.adopt(inre);

    gfx::Sizef s = Meter::measure(yttre, c);
    Meter::measure(yttre, c);
    Meter::measure(yttre, c);
    check(nära(s.w, 72.0f) && nära(s.h, 78.0f),
          "cache: ram i ram — barnens utsträckning plus luften två gånger",
          std::to_string(s.w) + "×" + std::to_string(s.h));
    check(löv.mätningar == 1,
          "cache: tre frågor till den yttre ramen mäter lövet EN gång",
          std::to_string(löv.mätningar));

    // Lövet ändras och invalideras: båda ramarna ser det.
    löv.bw = 100.0f;
    löv.invalidate();
    s = Meter::measure(yttre, c);
    check(nära(s.w, 132.0f) && nära(s.h, 78.0f),
          "cache: invalidate() på lövet når den yttre ramens mått",
          std::to_string(s.w) + "×" + std::to_string(s.h));
    check(löv.mätningar == 2, "cache: …och lövet mäts om en gång",
          std::to_string(löv.mätningar));

    // Rubriken på den YTTRE ramen: ramen mäts om, lövet i delträdet inte.
    yttre.setText(std::string(40, 'x'));
    s = Meter::measure(yttre, c);
    check(nära(s.w, 280.0f + 2.0f * (st.groupTitleIndent + st.groupTitleGap)),
          "cache: ny rubrik på den yttre ramen — rubriken är bredast",
          std::to_string(s.w));
    check(löv.mätningar == 2,
          "cache: …utan att delträdet mäts om", std::to_string(löv.mätningar));

    // Ett dolt löv tar inte plats: den inre ramen är tom (16×29), den yttre
    // 16 + 16 = 32 hög 29 + 13 + 16 = 58 (bredden är rubrikens).
    löv.hide();
    s = Meter::measure(yttre, c);
    check(nära(s.h, 58.0f), "cache: ett dolt löv räknas inte",
          std::to_string(s.h));
    löv.show();
    s = Meter::measure(yttre, c);
    check(nära(s.h, 78.0f), "cache: visat igen räknas lövet",
          std::to_string(s.h));
}

// ------------------------------------------------------ 5. radbrytningen

// En font som BRYTER: fast teckensteg per kodpunkt, `fitWidth` som räknar
// hela tecken som ryms. `FalskFont` ovan bryter aldrig och duger inte här.
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

// En SPÄND radbruten etikett: båda sidokanterna bundna (x = 0, right = 0), så
// ramen kan ge den en bredd.
glaze::Label* brytBarn(glaze::Widget& förälder, const char* namn)
{
    auto* l = förälder.add<glaze::Label>(kBrytText);
    l->setWrap(true);
    l->name    = namn;
    l->anchors = {true, true, true, false};
    return l;
}

// Ett löv vars höjd beror på bredden (6000 / bredd) och som räknar kroken.
struct Bredd final : glaze::Widget {
    void draw(const glaze::Ctx&) override {}
    mutable int krokar = 0;

protected:
    std::optional<float> heightForWidth(const glaze::Ctx&,
                                        float w) const override
    {
        ++krokar;
        return w > 0.0f ? std::optional<float>(6000.0f / w) : std::nullopt;
    }
};

void fallRadbrytning()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    BrytFont            f(10.0f, 20.0f);
    const glaze::Ctx    c = ctxFör(st, f);
    // Luften är 8 och bandet 20: ramen är barnens höjd + 36.

    // 5a. SATT bredd 216 ⇒ innerbredd 200 ⇒ två rader ⇒ 40 + 36 = 76. Utan
    // bredd hade etiketten räknats som en rad (56).
    {
        glaze::GroupBox g;
        g.width = 216;
        brytBarn(g, "text");
        const gfx::Sizef s = Meter::measure(g, c);
        check(nära(s.w, 216.0f) && nära(s.h, 76.0f),
              "5a: satt bredd 216 — etiketten bryts i innerbredden 200, två rader",
              std::to_string(s.w) + "×" + std::to_string(s.h));
        g.width                = 116;
        const gfx::Sizef smal = Meter::measure(g, c);
        check(nära(smal.h, 116.0f),
              "5a: satt bredd 116 — innerbredd 100, fyra rader: 80 + 36",
              std::to_string(smal.h));
        g.width               = 416;
        const gfx::Sizef bred = Meter::measure(g, c);
        check(nära(bred.h, 56.0f),
              "5a: satt bredd 416 — innerbredd 400, en rad: 20 + 36",
              std::to_string(bred.h));
    }

    // 5b. AUTO-bredd: bredden är okänd och etiketten räknas som EN rad, som
    // förut — 310 + 16 bred, 20 + 36 hög.
    {
        glaze::GroupBox g;
        brytBarn(g, "text");
        const gfx::Sizef s = Meter::measure(g, c);
        check(nära(s.w, 326.0f) && nära(s.h, 56.0f),
              "5b: auto-bredd — en rad, oförändrat",
              std::to_string(s.w) + "×" + std::to_string(s.h));
    }

    // 5c. Ett efterföljande syskon (anchorTop mot etiketten) flyttar med de två
    // raderna och räknas i ramens höjd: 40 + 4 + 10 + 36 = 90.
    {
        glaze::GroupBox g;
        g.width = 216;
        brytBarn(g, "text");
        auto* under      = g.add<glaze::Panel>();
        under->name      = "under";
        under->width     = 50;
        under->height    = 10;
        under->anchorTop = {"text", 4};
        const gfx::Sizef s = Meter::measure(g, c);
        check(nära(s.h, 90.0f),
              "5c: ett syskon under den radbrutna etiketten ryms i ramen",
              std::to_string(s.h));
    }

    // 5d. SATT höjd vinner: ramens mått är de satta och kroken tiger.
    {
        glaze::GroupBox g;
        g.width  = 216;
        g.height = 50;
        brytBarn(g, "text");
        const gfx::Sizef s = Meter::measure(g, c);
        check(nära(s.w, 216.0f) && nära(s.h, 50.0f),
              "5d: satt höjd vinner över de två raderna",
              std::to_string(s.w) + "×" + std::to_string(s.h));
        check(!Meter::measureHeightForWidth(g, c, 216.0f),
              "5d: satt height ⇒ heightForWidth svarar nullopt");
    }

    // 5e. heightForWidth: barnens höjd i innerbredden + bandet + luften.
    {
        glaze::GroupBox g;
        brytBarn(g, "text");
        const auto h216 = Meter::measureHeightForWidth(g, c, 216.0f);
        const auto h116 = Meter::measureHeightForWidth(g, c, 116.0f);
        const auto h416 = Meter::measureHeightForWidth(g, c, 416.0f);
        check(h216 && nära(*h216, 76.0f),
              "5e: heightForWidth(216) — två rader: 40 + 20 + 16",
              h216 ? std::to_string(*h216) : "nullopt");
        check(h116 && nära(*h116, 116.0f),
              "5e: heightForWidth(116) — fyra rader",
              h116 ? std::to_string(*h116) : "nullopt");
        check(h416 && nära(*h416, 56.0f),
              "5e: heightForWidth(416) — en rad",
              h416 ? std::to_string(*h416) : "nullopt");

        glaze::GroupBox tom;
        const auto      ht = Meter::measureHeightForWidth(tom, c, 100.0f);
        check(ht && nära(*ht, 36.0f),
              "5e: en tom ram är bandet plus luften, 20 + 16",
              ht ? std::to_string(*ht) : "nullopt");
    }

    // 5f. RAM I RAM: den inre ramen är spänd i den yttre. Bredd 232 ⇒ den inre
    // får 216 ⇒ etiketten 200 ⇒ två rader ⇒ inre 76 ⇒ yttre 76 + 36 = 112.
    // Bredd 132 ⇒ 116 ⇒ 100 ⇒ fyra rader ⇒ inre 116 ⇒ yttre 152.
    {
        glaze::GroupBox yttre;
        auto*           inre = yttre.add<glaze::GroupBox>();
        inre->name           = "inre";
        inre->anchors        = {true, true, true, false};
        brytBarn(*inre, "text");

        yttre.width  = 232;
        gfx::Sizef s = Meter::measure(yttre, c);
        check(nära(s.h, 112.0f),
              "5f: ram i ram, bredd 232 — innerbredden går genom båda nivåerna",
              std::to_string(s.h));
        yttre.width = 132;
        s           = Meter::measure(yttre, c);
        check(nära(s.h, 152.0f), "5f: ram i ram, bredd 132 — fyra rader",
              std::to_string(s.h));
        yttre.width  = 0;
        const auto h = Meter::measureHeightForWidth(yttre, c, 232.0f);
        check(h && nära(*h, 112.0f),
              "5f: ram i ram — heightForWidth(232) ger samma höjd",
              h ? std::to_string(*h) : "nullopt");
    }

    // 5g. MÄTCACHEN: svaret återanvänds per bredd (kroken frågas en gång) och
    // ett textbyte på etiketten mäts om — ramens cachade höjd ligger inte kvar.
    {
        glaze::GroupBox g;
        auto*           b = g.add<Bredd>();
        b->anchors        = {true, true, true, false};
        for (int i = 0; i < 3; ++i) {
            const auto h = Meter::measureHeightForWidth(g, c, 216.0f);
            check(h && nära(*h, 6000.0f / 200.0f + 36.0f),
                  "5g: 6000 / 200 + 36 = 66 vid bredden 216",
                  h ? std::to_string(*h) : "nullopt");
        }
        check(b->krokar == 1,
              "5g: tre frågor vid samma bredd frågar barnet EN gång",
              std::to_string(b->krokar));
        Meter::measureHeightForWidth(g, c, 116.0f);
        check(b->krokar == 2, "5g: en ny bredd frågar barnet igen",
              std::to_string(b->krokar));

        glaze::GroupBox g2;
        auto*           l  = brytBarn(g2, "text");
        auto            h2 = Meter::measureHeightForWidth(g2, c, 216.0f);
        check(h2 && nära(*h2, 76.0f), "5g: två rader före textbytet",
              h2 ? std::to_string(*h2) : "nullopt");
        l->setText("aaa");
        h2 = Meter::measureHeightForWidth(g2, c, 216.0f);
        check(h2 && nära(*h2, 56.0f),
              "5g: ett textbyte mäts om — en rad: 20 + 36",
              h2 ? std::to_string(*h2) : "nullopt");
    }
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
    fallMatcache();
    fallRadbrytning();

    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "GROUPBOX_GATE GRÖN" : "GROUPBOX_GATE FÄLLD",
                g_cases, g_fails);
    return g_fails == 0 ? 0 : 1;
}
