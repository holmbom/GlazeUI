// label_gate — ETIKETTENS KORTNING, HÖJDLED OCH GRANNE och KNAPPARNAS
// TEXTFÄRG.
//
// Kör:  ./build/tests/label_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// HEADLESS mot en INSPELANDE renderer och en falsk font med fast teckensteg
// per kodpunkt (som i listview_gate), så förväntade värden räknas ur kontraktet och
// inte ur widgetens geometri:
//
//   L1  en smal etikett med `elide` ritar `Draw::elide(…)` — med "…" — och en
//       bred ritar hela texten
//   L2  `wrap` + `elide`: brytningen gäller, ingen rad kortas
//   L3  `toolTipAt` ger tokenet när texten faktiskt kortats, tomt när den
//       ryms (också EXAKT), och en egen tooltip vinner
//   L4  `vAlign` Top/Bottom lägger texten mot kanten i en hög etikett; Center
//       ritar som utan anropet
//   L5  `buddy` mot ett textfält: höjden == fältets mätta; en satt `height`
//       vinner
//   L6  två etiketter som pekar på varandra: mätningen tar slut, egen höjd
//   L7  en DOLD granne räknas
//   L8  färgen på Button/Checkbox/Radio: appens på en aktiv, `textDim` på en
//       inaktiv, `onAccent` på fönstrets acceptknapp, temat efter
//       `setColor(std::nullopt)`
//   L9  rutan, ringen och pricken ritas aldrig i appens färg
//
// BREDDGRUPPEN, de förväntade bredderna i teckensteg (8 px):
//   W1  l1 "ab" i p1, l2 "abcdef" och l3 "abc" i p2, alla `setWidthGroup
//       ("form")` ⇒ alla tre sex steg; en fjärde utan grupp ⇒ ett steg
//   W2  `l2.hide()` ⇒ tre steg; `p2.hide()` ⇒ l1 två steg
//   W3  `l3.width = 100` ⇒ gruppen minst 100
//   W4  ett annat namn och ett annat träd ⇒ opåverkade
//   W5  en `Role::Heading`-medlem mäts med `headingFont` (dubbelt steg)
//   W6  skala 2 ⇒ samma logiska bredd
//   W7  `f1.anchorLeft {"l1", 8}` i p1 och `f2.anchorLeft {"l3", 8}` i p2
//       (samma inre vänsterkant) ⇒ samma x, gruppens bredd + 8
//   W8  `setProp("widthGroup", "form")`
//
// GLORIAN — tillståndet, inte pixlarna:
//   H1  förval: ingen gloria, bredden 0      H4  negativ bredd ⇒ no-op
//   H2  `setHalo(gfx::Color{…})` lagras      H5  `setHaloWidth(2.5)`
//   H3  `setHalo({})` ⇒ ingen gloria         H6  `setProp("haloWidth", "2")`
//
// SABOTAGEN som ska fälla den (skrivna):
//   * `setHalo` som tar en `gfx::Color` och inte en valfri (H3 kompilerar inte)
//   * `setHaloWidth` utan vakt mot negativa tal (H4)
//   * tooltipens jämförelse mot `measure` och inte `elide` (L3 gränsfallet)
//   * en Label tillåten som granne (L6 — rekursionen tar aldrig slut)
//   * appens färg som vinner över `onAccent` (L8)
//   * `labelColor` i rutans/ringens ritning (L9)
//   * `groupWidth` söker bara bland syskonen (W1: l1 två steg)
//   * dolda medlemmar räknas (W2)
//   * en satt bredd ignoreras i `naturalWidth` (W3)
//   * gruppnamnet jämförs inte (W4)
//   * medlemmen mäts med anroparens font i stället för sin roll (W5: sex steg)
//
// VAD DEN INTE MÄTER: hur "…" och den röda texten SER UT som pixlar (pixel_gate
// ritar fixturen), kontrasten i appens färg (appens ansvar), tooltipens
// riktiga routing och timer (tooltip_gate Q1) och breddgruppens OMRITNING
// (geometry_gate BG).

#include <cmath>
#include <cstdio>
#include <cstring>
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
    std::fflush(stdout);
    ++g_fails;
}

const std::string kEll = "\xE2\x80\xA6";   // ellipsen

// Fast teckensteg PER KODPUNKT i alla mätningarna — FYSISKA px.
class StegFont final : public gfx::NullFont {
public:
    explicit StegFont(float steg) : steg_(steg) {}
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{steg_ * float(tecken(s, s.size())), steg_ * 2.0f};
    }
    float height() const override { return steg_ * 2.0f; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return steg_ * float(tecken(s, std::min(bytes, s.size())));
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        return fitWidth(s, x + steg_ / 2.0f);
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size()) {
            std::size_t j = i + 1;
            while (j < s.size() &&
                   (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80)
                ++j;
            if (w + steg_ > maxW)
                break;
            w += steg_;
            i = j;
        }
        return i;
    }

private:
    static std::size_t tecken(const std::string& s, std::size_t bytes)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < bytes; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                ++n;
        return n;
    }
    float steg_;
};

// Texten (med plats och färg) och FÄRGERNA på varje annan primitiv.
struct Inspelning : gfx::NullRenderer {
    struct Text {
        std::string s;
        float       x = 0.0f, y = 0.0f;
        gfx::Color  färg{};
    };
    std::vector<Text>       texter;
    std::vector<gfx::Color> former;   // allt som inte är text
    float drawText(gfx::Font&, float x, float y, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back({s, x, y, c});
        return 0.0f;
    }
    void fillRect(const gfx::Rectf&, gfx::Color c) override { former.push_back(c); }
    void strokeRect(const gfx::Rectf&, gfx::Color c) override { former.push_back(c); }
    void line(float, float, float, float, gfx::Color c) override { former.push_back(c); }
    void fillRoundedRect(const gfx::Rectf&, float, gfx::Color c) override
    {
        former.push_back(c);
    }
    void strokeRoundedRect(const gfx::Rectf&, float, gfx::Color c, float) override
    {
        former.push_back(c);
    }
    void rensa()
    {
        texter.clear();
        former.clear();
    }
    int iFärg(gfx::Color c) const
    {
        int n = 0;
        for (const gfx::Color& f : former)
            n += f == c ? 1 : 0;
        return n;
    }
};

glaze::Ctx ctxFör(gfx::Font& f, gfx::Renderer* r)
{
    glaze::Ctx c{};
    c.r           = r;
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = 1.0f;
    c.ui          = glaze::UI::Slate;
    c.style       = &glaze::defaultStyle(glaze::UI::Slate);
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 600.0f, 400.0f};
    return c;
}

struct Etikett : glaze::Label {
    using glaze::Label::Label;
    using glaze::Label::draw;
    using glaze::Label::toolTipAt;
};

struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

// En widget ritad i sitt eget Ctx (boxen satt, som AppWindow gör).
void rita(glaze::Widget& w, glaze::Ctx c)
{
    c.box = w.resolve(c);
    w.render(c);
}

// =============================================================== L1–L4

void fallKortning()
{
    StegFont   f(8.0f);
    Inspelning rec;
    glaze::Ctx c = ctxFör(f, &rec);
    const std::string text = "Abcdefghij";   // tio tecken = 80 px

    // ---- L1.
    Etikett smal(text);
    smal.width = 40.0f;
    smal.setElide(true);
    rita(smal, c);
    const std::string kortad = glaze::Draw::elide(c, text, 40.0f);
    check(rec.texter.size() == 1 && rec.texter[0].s == kortad &&
              kortad.size() >= kEll.size() &&
              kortad.compare(kortad.size() - kEll.size(), kEll.size(), kEll) == 0,
          "L1: smal etikett ritar Draw::elide — med \"…\"",
          rec.texter.empty() ? std::string("inget") : rec.texter[0].s);
    rec.rensa();
    Etikett bred(text);
    bred.width = 200.0f;
    bred.setElide(true);
    rita(bred, c);
    check(rec.texter.size() == 1 && rec.texter[0].s == text,
          "L1: bred etikett ritar hela texten");

    // ---- L2. wrap + elide: brytningen gäller.
    rec.rensa();
    Etikett bruten("Ab cd ef gh");
    bruten.width = 40.0f;
    bruten.setWrap(true);
    bruten.setElide(true);
    rita(bruten, c);
    bool utanEllips = rec.texter.size() >= 2;
    for (const Inspelning::Text& t : rec.texter)
        utanEllips = utanEllips && t.s.find(kEll) == std::string::npos;
    check(utanEllips, "L2: wrap + elide — raderna bryts och ingen kortas",
          std::to_string(rec.texter.size()) + " rader");

    // ---- L3. toolTipAt.
    const glaze::TrText tipKort = smal.toolTipAt(c, 0.0f, 0.0f);
    check(tipKort.str() == text, "L3: kortad — hela texten som tooltip");
    check(bred.toolTipAt(c, 0.0f, 0.0f).empty(), "L3: ryms — ingen tooltip");
    Etikett exakt(text);
    exakt.width = 80.0f;   // EXAKT textens bredd — ryms
    exakt.setElide(true);
    check(exakt.toolTipAt(c, 0.0f, 0.0f).empty() &&
              glaze::Draw::elide(c, text, 80.0f) == text,
          "L3: exakt lika bred som texten — ingen tooltip (elide, inte measure)");
    smal.setToolTip("Eget tips");
    const glaze::TrText tipEget = smal.toolTipAt(c, 0.0f, 0.0f);
    check(tipEget.str() == "Eget tips", "L3: en egen tooltip vinner");

    // ---- L4. vAlign i en hög etikett.
    const auto yFör = [&](std::optional<glaze::VAlign> v) {
        rec.rensa();
        Etikett l("Hej");
        l.width  = 100.0f;
        l.height = 100.0f;
        if (v)
            l.setVAlign(*v);
        rita(l, c);
        return rec.texter.empty() ? -1.0f : rec.texter[0].y;
    };
    const float förval = yFör(std::nullopt);
    const float topp   = yFör(glaze::VAlign::Top);
    const float mitt   = yFör(glaze::VAlign::Center);
    const float botten = yFör(glaze::VAlign::Bottom);
    const float rad    = glaze::Draw::lineHeight(c);
    check(mitt == förval, "L4: Center ritar som utan anropet");
    check(std::abs((förval - topp) - (100.0f - rad) / 2.0f) < 0.01f &&
              std::abs((botten - förval) - (100.0f - rad) / 2.0f) < 0.01f,
          "L4: Top och Bottom — texten mot kanten",
          std::to_string(topp) + " / " + std::to_string(förval) + " / " +
              std::to_string(botten));
}

// =============================================================== L5–L7

void fallGrannen()
{
    StegFont   f(8.0f);
    Inspelning rec;
    glaze::Ctx c = ctxFör(f, &rec);

    glaze::Panel     p;
    glaze::Label     namn("Namn");
    glaze::TextInput fält;
    fält.name = "falt";
    namn.setBuddy("falt");
    p.adopt(namn);
    p.adopt(fält);
    const float fältH = Meter::measure(fält, c).h;
    check(fältH > Meter::measure(glaze::Label("Namn"), c).h &&
              Meter::measure(namn, c).h == fältH,
          "L5: grannens höjd — lika hög som fältet",
          std::to_string(Meter::measure(namn, c).h) + " mot " +
              std::to_string(fältH));
    namn.height = 10.0f;
    check(Meter::measure(namn, c).h == 10.0f, "L5: en satt height vinner");
    namn.height = 0.0f;

    // ---- L7. En DOLD granne räknas (ett syskonankares mål gör det också).
    fält.hide();
    check(Meter::measure(namn, c).h == fältH, "L7: en dold granne räknas");
    fält.show();

    // ---- L6. Två etiketter som pekar på varandra — mätningen tar slut.
    glaze::Panel q;
    glaze::Label a("A"), b("B");
    a.name = "a";
    b.name = "b";
    a.setBuddy("b");
    b.setBuddy("a");
    q.adopt(a);
    q.adopt(b);
    check(Meter::measure(a, c).h == Meter::measure(glaze::Label("A"), c).h,
          "L6: två etiketter som grannar — egen höjd, och mätningen tar slut");
}

// =============================================================== L8–L9

template <class W>
std::optional<gfx::Color> textFärg(W& w, const glaze::Ctx& c, Inspelning& rec)
{
    rec.rensa();
    rita(w, c);
    return rec.texter.empty() ? std::nullopt
                              : std::optional<gfx::Color>(rec.texter[0].färg);
}

void fallFärgen()
{
    StegFont          f(8.0f);
    Inspelning        rec;
    glaze::Ctx        c   = ctxFör(f, &rec);
    const auto&       col = c.theme->colors;
    const gfx::Color  app{201, 13, 77, 255};   // ingen temafärg

    glaze::Button   knapp("Ta bort");
    glaze::Checkbox kryss("Kryss");
    glaze::Radio    radio("Radio");
    kryss.setChecked(true);
    radio.setChecked(true);
    const auto alla = [&](auto&& gör) {
        gör(knapp, "Button");
        gör(kryss, "Checkbox");
        gör(radio, "Radio");
    };
    alla([&](auto& w, const char* n) {
        w.setColor(app);
        check(textFärg(w, c, rec) == app, std::string("L8: ") + n + " — appens färg");
        // ---- L9. Bara texten: rutan, ringen och pricken följer temat.
        check(rec.iFärg(app) == 0,
              std::string("L9: ") + n + " — inget annat i appens färg",
              std::to_string(rec.iFärg(app)));
        w.disable();
        check(textFärg(w, c, rec) == col.textDim,
              std::string("L8: ") + n + " — inaktiv i textDim");
        w.enable();
        w.setColor(std::nullopt);
        check(textFärg(w, c, rec) == col.text,
              std::string("L8: ") + n + " — temat efter setColor(std::nullopt)");
    });

    // Fönstrets ACCEPTKNAPP: accentytan med `onAccent` vinner över appens.
    glaze::AppWindow fönster("label_gate", 300, 200);
    glaze::Button    ok("OK");
    fönster.root().adopt(ok);
    fönster.setAcceptButton(ok);
    ok.setColor(app);
    check(textFärg(ok, c, rec) == col.onAccent,
          "L8: acceptknappen — onAccent vinner över appens färg");
}

// =============================================================== W1–W8
//
// BREDDGRUPPEN: etiketter med samma gruppnamn får den
// bredaste SYNLIGA medlemmens bredd som AUTO-bredd, över hela trädet. Förväntat värde i
// teckensteg (8 logiska px) ur texterna — aldrig ur widgetens egen mätning.

// En naken container vars barnyta är den egna recten (layout_gate:s `Yta`).
struct Yta final : glaze::Widget {
    Yta() { anchors = {true, true, true, true}; }
    void draw(const glaze::Ctx&) override {}
};

void fallBreddgrupp()
{
    StegFont   f(8.0f);
    Inspelning rec;
    glaze::Ctx c = ctxFör(f, &rec);
    const auto bredd = [&c](const glaze::Widget& w) {
        return Meter::measure(w, c).w;
    };
    const auto steg = [](float w) { return std::to_string(w / 8.0f) + " steg"; };

    // Två paneler med samma inre vänsterkant under en gemensam rot: l1 i p1,
    // l2 och l3 i p2 — gruppen spänner över föräldrarna.
    Yta          rot;
    glaze::Panel p1, p2;
    glaze::Label l1("ab"), l2("abcdef"), l3("abc"), l4("a");
    p1.name   = "p1";
    p1.width  = 300.0f;
    p1.height = 80.0f;
    p2.name   = "p2";
    p2.width  = 300.0f;
    p2.height = 80.0f;
    p2.y      = 100.0f;
    l1.name   = "l1";
    l2.name   = "l2";
    l3.name   = "l3";
    l4.name   = "l4";
    for (glaze::Label* l : {&l1, &l2, &l3})
        l->setWidthGroup("form");
    rot.adopt(p1);
    rot.adopt(p2);
    p1.adopt(l1);
    p2.adopt(l2);
    p2.adopt(l3);
    p2.adopt(l4);

    // ---- W1. Alla tre får sex steg, en etikett utan grupp sin egen.
    check(bredd(l1) == 48.0f && bredd(l2) == 48.0f && bredd(l3) == 48.0f,
          "W1: tre medlemmar i två paneler — alla sex steg",
          steg(bredd(l1)) + ", " + steg(bredd(l2)) + ", " + steg(bredd(l3)));
    check(bredd(l4) == 8.0f, "W1: en etikett utan grupp — ett steg",
          steg(bredd(l4)));

    // ---- W2. Dolda medlemmar räknas inte — inte heller ett dolt delträd.
    l2.hide();
    check(bredd(l1) == 24.0f && bredd(l3) == 24.0f,
          "W2: l2 dold — tre steg", steg(bredd(l1)));
    l2.show();
    p2.hide();
    check(bredd(l1) == 16.0f, "W2: p2 dold — l1 ensam, två steg",
          steg(bredd(l1)));
    p2.show();

    // ---- W3. En satt bredd vinner för medlemmen och ger kolumnen ett golv.
    l3.width = 100.0f;
    check(bredd(l1) == 100.0f && bredd(l3) == 100.0f,
          "W3: l3.width = 100 — gruppen minst 100", steg(bredd(l1)));
    l3.width = 0.0f;

    // ---- W4. Ett annat namn och ett annat träd påverkar inte gruppen.
    glaze::Label annan("abcdefghijkl");
    annan.setWidthGroup("annan");
    p1.adopt(annan);
    Yta          rot2;
    glaze::Label främling("abcdefghij");
    främling.setWidthGroup("form");
    rot2.adopt(främling);
    check(bredd(l1) == 48.0f && bredd(annan) == 96.0f,
          "W4: annat namn och annat träd — opåverkade", steg(bredd(l1)));
    check(bredd(främling) == 80.0f,
          "W4: medlemmen i det andra trädet ser bara sitt träd",
          steg(bredd(främling)));

    // ---- W5. En RUBRIK i gruppen mäts med SIN font (dubbelt steg).
    StegFont     rubrikFont(16.0f);
    glaze::Label rubrik("abcd");
    rubrik.setRole(glaze::Label::Role::Heading);
    rubrik.setWidthGroup("form");
    p1.adopt(rubrik);
    c.headingFont = &rubrikFont;
    check(bredd(l1) == 64.0f, "W5: rubriken mäts med headingFont — 4 × 16",
          std::to_string(bredd(l1)));
    rubrik.hide();
    c.headingFont = nullptr;

    // ---- W6. Skala 2: fonten talar fysiska px, förväntat värde är detsamma i logiska.
    {
        StegFont   f2(16.0f);
        glaze::Ctx c2 = c;
        c2.font       = &f2;
        c2.scale      = 2.0f;
        check(Meter::measure(l1, c2).w == 48.0f,
              "W6: skala 2 — samma logiska bredd",
              std::to_string(Meter::measure(l1, c2).w));
    }

    // ---- W7. Fälten ankrade efter etiketterna står i linje över panelerna.
    glaze::Panel f1, f2;
    f1.name       = "f1";
    f1.width      = 50.0f;
    f1.height     = 20.0f;
    f1.anchorLeft = {"l1", 8};
    f2.name       = "f2";
    f2.width      = 50.0f;
    f2.height     = 20.0f;
    f2.anchorLeft = {"l3", 8};
    p1.adopt(f1);
    p2.adopt(f2);
    {
        // Containerkrokarna är PUBLIKA på `Widget` (Panels override är
        // protected — åtkomsten prövas mot typen man anropar genom).
        const glaze::Widget& w1 = p1;
        const glaze::Widget& w2 = p2;
        glaze::Ctx           cr = c;
        cr.area                 = rot.childArea(c);
        glaze::Ctx c1 = cr;
        c1.area       = w1.childArea(cr);
        glaze::Ctx c2 = cr;
        c2.area       = w2.childArea(cr);
        const float x1 = f1.resolve(c1).x, x2 = f2.resolve(c2).x;
        check(c1.area.x == c2.area.x && x1 == x2 && x1 == c1.area.x + 56.0f,
              "W7: f1 och f2 på samma x — gruppens bredd + 8",
              std::to_string(x1) + " mot " + std::to_string(x2));
    }

    // ---- W8. Textvägen.
    glaze::Label viaProp("x");
    check(viaProp.setProp("widthGroup", "form") && viaProp.widthGroup() == "form",
          "W8: setProp(\"widthGroup\", \"form\")", viaProp.widthGroup());
}

// GLORIAN — tillståndet och textvägen. Konturens PIXLAR mäts inte här.
void fallGloria()
{
    std::printf("\n=== H. gloria ===\n");
    glaze::Label l("x");
    check(!l.halo().has_value() && l.haloWidth() == 0.0f,
          "H1: förval — ingen gloria, bredden 0 (stilens)");

    l.setHalo(gfx::Color{10, 20, 30, 255});
    check(l.halo().has_value() && l.halo()->r == 10 && l.halo()->g == 20 &&
              l.halo()->b == 30 && l.halo()->a == 255,
          "H2: setHalo(gfx::Color{…}) lagras");

    l.setHalo({});
    check(!l.halo().has_value(),
          "H3: setHalo({}) är INGEN gloria (till skillnad från setColor({}))");

    l.setHaloWidth(-1.0f);   // loggar och ignoreras
    check(l.haloWidth() == 0.0f, "H4: en negativ bredd är en no-op");
    l.setHaloWidth(2.5f);
    check(l.haloWidth() == 2.5f, "H5: setHaloWidth(2.5)");

    glaze::Label viaProp("x");
    check(viaProp.setProp("haloWidth", "2") && viaProp.haloWidth() == 2.0f,
          "H6: setProp(\"haloWidth\", \"2\")", std::to_string(viaProp.haloWidth()));
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("label_gate\n");
    fallKortning();
    fallGrannen();
    fallFärgen();
    fallBreddgrupp();
    fallGloria();

    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
