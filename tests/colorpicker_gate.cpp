// colorpicker_gate — FÄRGVÄLJARENS grind.
//
// Kör:  ./build/tests/colorpicker_gate [-v] [--png <katalog>]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt, och
//       skippet SYNS i ctest.
//
// VAD DEN MÄTER:
//
//   HEADLESS
//     · TEXTFORMEN `#RRGGBB` mot ett HANDSKRIVET förväntat värde — giltiga former,
//       ogiltiga som lämnar färgen ORÖRD, kanonisk skrivning (versaler) och
//       att värdet alltid är OPAKT. Förväntat värde är grindens egen tabell och dess
//       egen formatering, aldrig produktionens `hexColorText`.
//     · ANVÄNDARE mot PROGRAM: `setColor` fyrar aldrig, användarvägen
//       (`colorFromUser`) exakt en gång per faktisk ändring.
//     · `setProp`-vokabulären och att en färgväljare utan fönster inte öppnar.
//
//   MED FÖNSTER — ett DOLT ägarfönster och popupen som RIKTIGT popupfönster,
//   allt genom ÄGARENS riktiga ingång (`AppWindow::handleEvent`, protected):
//     · ett tabbstopp; Space öppnar och PALETTEN har fokus
//     · popupens innehåll ryms i den storlek som räknades FÖRE visningen
//       (varje del inom klientytan, raderna i ordning utan överlapp,
//       marginalen är `colorPopupPad`) — mätt som EGENSKAPER och inte som en
//       kopia av layoutformeln; och efter ett STILBYTE mellan två öppningar
//       är luften den nya stilens och paletten krymper med den
//     · palettens mått i LOGISKA px (samma i skala 1 och 2 — den interna
//       widgeten nås inte av `measure_gate`, som bara ser registret)
//     · pilarna flyttar markören utan event, Enter VÄLJER och STÄNGER — och
//       nedåtpilen hoppar en hel rad i rutnätet
//     · Tab vandrar till reglagen; en pil i ett reglage ändrar färgen —
//       onChange OCH onCommit för steget — värdet och hexfältet följer
//     · hexfältet COMMITTAS (inte LIVE): halvskrivet och en hel
//       färg ändrar ingenting medan man skriver, ett tecken som inte är hex
//       sväljs; Tab vidare committar (eventen POSTADE, färgen direkt), gemener
//       tolkas, och Enter committar, skriver om texten och stänger (att en
//       skriven färg committas också när popupen stängs med Escape mäts INTE
//       här utan i commit_gate, fall C3)
//     · Escape stänger utan ändring; programmatisk setColor medan popupen är
//       öppen synkar den UTAN event
//     · popupen TAR TANGENTBORDET (`AppWindow::setPopupTakesKeys`):
//       den bär attributet, och tangenterna i ÄGARENS ingång går till dess
//       egen ingång — samma `handleEvent` som Waylands grab levererar till
//     · tryck på en annan widget i ägaren stänger (fokusförlusten); tryck på
//       färgväljaren TOGGLAR; en inaktiverad öppnar aldrig; öppen popup NEKAR
//       `unadopt`
//
// VAD DEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · MUSEN I POPUPEN — tryck på en ruta, reglagedrag. Popupens ingång är
//     protected, och ett OS-klick vore plattformskod; tangentvägen bär samma
//     `pick` och samma `colorFromUser`, och rutornas träff och ritning delar
//     EN geometri (`cellRect`).
//   · App:s generella "tryck i ett annat fönster stänger popups"
//     (`App::closePopups`) — popupmaskineriets, grindat för dropdownen.
//   · UTSEENDET: `--png` skriver den stängda kontrollen och den öppna popupen
//     per UI × läge att ÖGNA; att den RITAR mäter pixel_gate (fixturens grupp
//     22).
//
// ALFAN OCH FÄRGDIALOGEN:
//   A1  `#RRGGBBAA` tas och lagras; med `allowAlpha` läses alfan
//   A2  satsordningen: färgen före och efter `setAllowAlpha` ger samma, och
//       utan flaggan läses färgen OPAK — det lagrade står kvar
//   A3  `colorText()` med åtta siffror bara med alfan
//   A4  `Label`s `color`-nyckel tar fortfarande BARA sex siffror
//   A5  FÄRGYTAN (den interna `color_panel.h`, headless): A-raden finns och
//       syns bara med alfan, och `sync` ställer A-reglaget
//   A6  FÄRGDIALOGEN (med fönster, `color_dialog_window.h` i en subklass som
//       når `handleEvent`): ytan visar förvalet, paletten har fokus och
//       pilarna VÄLJER (alfan står kvar), ett skrivet hexvärde + Enter ⇒ OK med
//       den färgen, Escape ⇒ Cancel och färgen orörd, utan alfan är A-raden
//       dold
// Sabotagen för dem: `parseHexColor` med åtta siffror också utan flaggan
// (A4), `color()` som läser alfan utan flaggan (A2), paletten som inte
// behåller alfan (A6: alfan 255 efter pilen), hexfältets Enter som tas av
// fältet i dialogen (A6: dialogen står kvar och svarar inte OK), OK utan
// `commitEdits` (A6:s sista steg — `performClick` med fokus KVAR i
// hexfältet ger förvalet i stället för det skrivna).
//
// SABOTAGEN som ska fälla den:
// `colorFromUser` utan event · ägarens vidarebefordran borttagen (
// `AppWindow::forwardToKeyPopup` ur `handleEvent`, eller popupens
// `setPopupTakesKeys(true)` bort) · en hexläsning som
// godtar sju siffror · en popup fyra px för låg (innehållet ryms ännu — det
// är MARGINALEN som fäller den) · en layout som mäter med förra öppningens
// satta mått (stilbytet fäller den).
//
// INTE HÄR: Waylands DIREKTA väg (popupens grab ger popupen
// tangenterna utan ägaren) — det är samma `handleEvent` som ägarens
// vidarebefordran anropar, och den nås inte utifrån (protected).

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "color_dialog_window.h"   // Glaze/widgets/src — färgdialogen (A6)
#include "color_panel.h"           // Glaze/widgets/src — färgytan (A5)

namespace {

int         g_fails   = 0;
int         g_skips   = 0;
int         g_cases   = 0;
bool        g_verbose = false;
std::string g_png;

// Hexfältets redigeringsgenvägar är plattformens — Cmd på macOS, Ctrl annars
// (`textfield::editShortcut`).
#ifdef __APPLE__
constexpr std::uint32_t kRedigera = gfx::kModCmd;
#else
constexpr std::uint32_t kRedigera = gfx::kModCtrl;
#endif

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

// Grindens EGEN formatering — förväntat värde får aldrig vara produktionens.
std::string hex(gfx::Color c)
{
    char b[16];
    std::snprintf(b, sizeof b, "#%02X%02X%02X/%u", unsigned(c.r), unsigned(c.g),
                  unsigned(c.b), unsigned(c.a));
    return b;
}

bool lika(gfx::Color c, int r, int g, int b)
{
    return c.r == r && c.g == g && c.b == b && c.a == 255;
}
bool lika(gfx::Color a, gfx::Color b)
{
    return lika(a, b.r, b.g, b.b);
}

// De skyddade krokarna anropbara — en subklass får det.
struct Prov : glaze::ColorPicker {
    using glaze::ColorPicker::colorFromUser;
    using glaze::ColorPicker::keyDown;
    using glaze::ColorPicker::textInput;
};

// `Widget::measure` är protected och statisk — en subklass av BASEN når den
// (msgbox_gate-prejudikatet).
struct Mätare : glaze::Widget {
    using glaze::Widget::measure;
    void draw(const glaze::Ctx&) override {}
};

// ================================================================ HEADLESS

void fallTextform()
{
    struct Rad {
        const char* text;
        bool        giltig;
        int         r, g, b;
    };
    // FÖRVÄNTAT VÄRDE — handskrivet. Giltigt: valfritt `#`, exakt sex hexsiffror,
    // versaler eller gemener, blanksteg runt om.
    const Rad tabell[] = {
        {"#3366FF", true, 0x33, 0x66, 0xff},
        {"#3366ff", true, 0x33, 0x66, 0xff},
        {"3366FF", true, 0x33, 0x66, 0xff},
        {"  #a0B1c2\t", true, 0xa0, 0xb1, 0xc2},
        {"#000000", true, 0, 0, 0},
        {"#FFFFFF", true, 255, 255, 255},
        {"", false, 0, 0, 0},
        {"#", false, 0, 0, 0},
        {"#36F", false, 0, 0, 0},          // ingen kortform
        {"#3366F", false, 0, 0, 0},
        {"#3366FFF", false, 0, 0, 0},
        // ÅTTA SIFFROR TAS — alfan lagras och läses OPAK utan
        // `allowAlpha` (fallAlfa nedan).
        {"#3366FF80", true, 0x33, 0x66, 0xff},
        {"#3366FF8", false, 0, 0, 0},      // sju siffror
        {"##3366FF", false, 0, 0, 0},
        {"#33 66FF", false, 0, 0, 0},
        {"#3366FG", false, 0, 0, 0},
        {"# 12345", false, 0, 0, 0},       // strtoul-fällan (Labels gamla)
        {"#+12345", false, 0, 0, 0},
        {"röd", false, 0, 0, 0},
        {"rgb(1, 2, 3)", false, 0, 0, 0},
    };
    for (const Rad& rad : tabell) {
        glaze::ColorPicker p;
        p.setColor(gfx::Color{9, 9, 9, 255});
        p.setColor(std::string_view(rad.text));
        const std::string vad = std::string("textform: \"") + rad.text + "\" ";
        if (rad.giltig)
            check(lika(p.color(), rad.r, rad.g, rad.b), vad + "tolkas",
                  hex(p.color()));
        else
            check(lika(p.color(), 9, 9, 9), vad + "lämnar färgen ORÖRD",
                  hex(p.color()));
    }

    glaze::ColorPicker p;
    p.setColor("#a0b1c2");
    check(p.colorText() == "#A0B1C2",
          "colorText: kanonisk form — # och VERSALER", p.colorText());
    p.setColor(gfx::Color{1, 2, 3, 4});
    check(p.color().a == 255 && p.colorText() == "#010203",
          "opak: setColor(gfx::Color) med alfa 4 ger alfa 255",
          hex(p.color()));
    check(lika(glaze::ColorPicker().color(), 0, 0, 0),
          "standardvärdet är svart (wx/WinForms)");
}

void fallProgrammatisk()
{
    int  n = 0;
    Prov p;
    p.onChange = [&n] { ++n; };
    p.setColor("#112233");
    p.setColor(gfx::Color{4, 5, 6, 255});
    p.setProp("color", "#778899");
    check(n == 0, "programmatisk sättning fyrar INTE onChange",
          std::to_string(n));
    p.colorFromUser(gfx::Color{10, 20, 30, 255});
    check(n == 1 && lika(p.color(), 10, 20, 30),
          "användarvägen: ny färg ⇒ EXAKT ett onChange", std::to_string(n));
    p.colorFromUser(gfx::Color{10, 20, 30, 255});
    p.colorFromUser(gfx::Color{10, 20, 30, 0});   // samma färg — alfan räknas inte
    check(n == 1, "användarvägen: samma färg (också med annan alfa) fyrar inte",
          std::to_string(n));
}

void fallSetProp()
{
    glaze::ColorPicker p;
    check(p.setProp("color", "#102030") && lika(p.color(), 0x10, 0x20, 0x30),
          "setProp: color känns igen och sätts");
    check(p.setProp("color", "inte en färg") &&
              lika(p.color(), 0x10, 0x20, 0x30),
          "setProp: ogiltig färg — nyckeln känd, färgen orörd");
    check(!p.setProp("colour", "#000000"), "setProp: okänd nyckel ⇒ false");
    check(!p.setProp("text", "x"), "setProp: ingen text-nyckel");
}

void fallAlfa()
{
    const gfx::Color väntad{0x33, 0x66, 0xFF, 0x80};
    // A1
    glaze::ColorPicker a;
    a.setAllowAlpha(true);
    a.setColor("#3366FF80");
    check(a.color() == väntad, "A1: #RRGGBBAA med alfan", hex(a.color()));
    // A2 — ordningen, och läsningen utan flaggan.
    glaze::ColorPicker b;
    b.setColor("#3366ff80");
    check(b.color().a == 255 && lika(b.color(), 0x33, 0x66, 0xff),
          "A2: utan allowAlpha läses färgen opak", hex(b.color()));
    b.setAllowAlpha(true);
    check(b.color() == väntad, "A2: setColor FÖRE setAllowAlpha ger alfan",
          hex(b.color()));
    b.setAllowAlpha(false);
    b.setAllowAlpha(true);
    check(b.color() == väntad, "A2: det lagrade står kvar genom ett byte");
    b.setColor(gfx::Color{1, 2, 3, 4});
    check(b.color() == gfx::Color{1, 2, 3, 4},
          "A2: setColor(gfx::Color) bär alfan med flaggan", hex(b.color()));
    // A3
    check(b.colorText() == "#01020304", "A3: colorText med åtta siffror",
          b.colorText());
    b.setAllowAlpha(false);
    check(b.colorText() == "#010203", "A3: och sex utan alfan", b.colorText());
    // A4 — Labels nyckel är oförändrad. `setProp` svarar TRUE för en KÄND
    // nyckel också när värdet är ogiltigt (loggat och ignorerat — setProp-
    // konventionen, Labels gren): det som mäts är att färgen
    // står KVAR, inte svaret.
    glaze::Label l;
    l.setColor(gfx::Color{9, 9, 9, 255});
    l.setProp("color", "#11223344");
    check(l.color() == gfx::Color{9, 9, 9},
          "A4: Labels color tar inte åtta siffror", hex(l.color()));
    // setProp
    glaze::ColorPicker p;
    check(p.setProp("allowAlpha", "true") && p.allowAlpha(),
          "A: setProp allowAlpha");
}

// A5 — färgytan utan fönster: barnen hittas på sina namn.
glaze::Widget* barnet(glaze::Widget& rot, const std::string& namn)
{
    for (glaze::Widget* b : rot.children())
        if (b->name == namn)
            return b;
    return nullptr;
}

void fallFargytan()
{
    glaze::ColorPanel yta;
    auto* a  = dynamic_cast<glaze::Slider*>(barnet(yta, "alpha"));
    auto* at = barnet(yta, "alphaValue");
    check(a && at && !a->isVisible() && !at->isVisible(),
          "A5: A-raden finns och är dold utan alfan");
    yta.setAllowAlpha(true);
    check(a && a->isVisible() && at && at->isVisible(),
          "A5: med alfan syns A-raden");
    yta.sync(gfx::Color{10, 20, 30, 40});
    auto* hx = dynamic_cast<glaze::TextInput*>(barnet(yta, "hex"));
    check(a && a->value() == 40.0f && hx && hx->text() == "#0A141E28",
          "A5: sync ställer A-reglaget och hexfältets åtta siffror",
          hx ? hx->text() : "");
    yta.setAllowAlpha(false);
    check(hx && hx->text() == "#0A141E", "A5: utan alfan sex siffror",
          hx ? hx->text() : "");
}

void fallUtanFonster()
{
    Prov p;
    p.open();
    check(!p.isOpen(), "open(): utan fönster = no-op");
    check(p.keyDown(gfx::Key::Space, 0) && !p.isOpen(),
          "Space utan fönster: tas emot men öppnar ingenting");
    check(!p.textInput("a"), "stängd: tecken tas inte emot");
    check(!p.keyDown(gfx::Key::A, 0), "stängd: okänd tangent bubblar");
}

// ============================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Button före{"före"};
    Prov          farg;
    Prov          av;
    glaze::Button efter{"efter"};
    int           ändringar = 0;
    int           commits   = 0;   // onCommit

    Vard() : glaze::AppWindow("colorpicker_gate", 420, 260)
    {
        före.x    = 10;
        före.y    = 10;
        före.name = "före";
        farg.x    = 10;
        farg.y    = 50;
        farg.name = "farg";
        av.x      = 120;
        av.y      = 50;
        av.name   = "av";
        av.setColor("#FF9800");
        av.disable();
        efter.x    = 10;
        efter.y    = 100;
        efter.name = "efter";
        farg.onChange = [this] { ++ändringar; };
        farg.onCommit = [this] { ++commits; };
        root().adopt(före);
        root().adopt(farg);
        root().adopt(av);
        root().adopt(efter);
    }

    void tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void text(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, s, sizeof e.text - 1);
        handleEvent(e);
    }
    // Ett tryck mitt på en widget, i fönstrets fysiska px.
    void tryck(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        gfx::Event       e;
        e.windowId = window().id();
        e.mouseX   = r.x + r.w / 2.0f;
        e.mouseY   = r.y + r.h / 2.0f;
        e.button   = gfx::MouseButton::Left;
        e.type     = gfx::EventType::MouseButtonDown;
        handleEvent(e);
        e.type = gfx::EventType::MouseButtonUp;
        handleEvent(e);
    }
};

glaze::AppWindow* popupen(const glaze::AppWindow& ägare)
{
    for (glaze::AppWindow* w : glaze::app().windows())
        if (w != &ägare)
            return w;
    return nullptr;
}

// Popupens delar — namnen är kontraktet mellan widgeten och grinden
// (filväljardialogens "path"/"files"-prejudikat), ingen testkrok.
struct Delar {
    glaze::Widget*    palett = nullptr;
    glaze::Slider*    röd    = nullptr;
    glaze::Slider*    grön   = nullptr;
    glaze::Slider*    blå    = nullptr;
    glaze::Label*     grönTal = nullptr;
    glaze::TextInput* hex    = nullptr;
    bool              hela() const
    {
        return palett && röd && grön && blå && grönTal && hex;
    }
};

Delar delar(glaze::AppWindow* p)
{
    Delar d;
    if (!p)
        return d;
    d.palett  = p->find<glaze::Widget>("palette");
    d.röd     = p->find<glaze::Slider>("red");
    d.grön    = p->find<glaze::Slider>("green");
    d.blå     = p->find<glaze::Slider>("blue");
    d.grönTal = p->find<glaze::Label>("greenValue");
    d.hex     = p->find<glaze::TextInput>("hex");
    return d;
}

// RYMS INNEHÅLLET? Varje del inom klientytan, och raderna i ordning utan
// överlapp — en EGENSKAP hos layouten, inte en kopia av dess formel.
void fallRyms(glaze::AppWindow* p, const Delar& d)
{
    const gfx::Sizef  klient = p->clientSize();
    const glaze::Ctx  c      = d.palett->behaviorCtx();
    const float       s      = c.scale;
    const float       W = klient.w * s, H = klient.h * s;
    std::vector<gfx::Rectf> rad;
    for (glaze::Widget* w : std::initializer_list<glaze::Widget*>{
             d.palett, d.röd, d.grön, d.blå, d.hex}) {
        const gfx::Rectf r = w->resolve(w->behaviorCtx());
        rad.push_back(r);
        check(r.w > 0.0f && r.h > 0.0f && r.x >= 0.0f && r.y >= 0.0f &&
                  r.x + r.w <= W + 0.5f && r.y + r.h <= H + 0.5f,
              "popupen: '" + w->name + "' ryms i klientytan",
              std::to_string(int(r.x)) + "," + std::to_string(int(r.y)) + " " +
                  std::to_string(int(r.w)) + "×" + std::to_string(int(r.h)) +
                  " i " + std::to_string(int(W)) + "×" + std::to_string(int(H)));
    }
    for (std::size_t i = 1; i < rad.size(); ++i)
        check(rad[i].y >= rad[i - 1].y + rad[i - 1].h,
              "popupen: rad " + std::to_string(i) +
                  " står under den förra, utan överlapp");
    // MARGINALEN är stilens `colorPopupPad` — "luften runt allt", enligt sin
    // definition — under hexfältet och till höger om det. En popup som räknats
    // för låg eller för smal syns här innan den klipper något.
    const float pad = p->style().colorPopupPad;
    const float under = (H - (rad.back().y + rad.back().h)) / s;
    const float höger = (W - (rad.back().x + rad.back().w)) / s;
    check(under > pad - 0.75f && under < pad + 1.0f && höger > pad - 0.75f &&
              höger < pad + 1.0f,
          "popupen: marginalen under och till höger om hexfältet är "
          "colorPopupPad",
          std::to_string(under) + " / " + std::to_string(höger));

    // Palettens mått i LOGISKA px: samma tal i skala 1 och 2.
    glaze::Ctx c1 = c, c2 = c;
    c1.scale = 1.0f;
    c2.scale = 2.0f;
    const gfx::Sizef m1 = Mätare::measure(*d.palett, c1);
    const gfx::Sizef m2 = Mätare::measure(*d.palett, c2);
    check(m1.w > 0.0f && m1.h > 0.0f && m1.w == m2.w && m1.h == m2.h,
          "paletten: contentSize i LOGISKA px (skala 1 = skala 2)",
          std::to_string(m1.w) + " mot " + std::to_string(m2.w));
}

void fallMedFönster()
{
    glaze::setLanguage("sv");
    Vard w;
    w.setOffscreen(true);

    const auto& pal = glaze::standardPalette();
    bool  kedjanKördes   = false;
    int   steg           = 0;
    float palettHöjdFörr = 0.0f;   // palettens höjd (logiska px) i Glaze
    int   hexInnan       = 0;      // räknarna före hexfältets fall — det
    int   hexCommits     = 0;      // postade eventet räknas ett steg senare
    auto  palettHöjd     = [](glaze::Widget* palett) {
        const glaze::Ctx c = palett->behaviorCtx();
        return palett->resolve(c).h / c.scale;
    };
    std::function<void()> nästa;
    nästa = [&] {
        glaze::AppWindow* p = nullptr;
        Delar             d;
        switch (steg++) {
        case 0: {
            kedjanKördes = true;
            if (!w.isOpen()) {
                std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                            "aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            const auto ordning = w.focusOrder();
            check(ordning.size() == 3 && ordning[1] == &w.farg,
                  "tabbordningen: före · färgväljare · efter (den inaktiverade "
                  "står inte med)",
                  std::to_string(ordning.size()));
            w.farg.setColor("#3366FF");   // finns inte i paletten
            w.farg.focus();
            w.tangent(gfx::Key::Space);
            check(w.farg.isOpen(), "Space öppnar popupen");
            p = popupen(w);
            check(p && p->popup() && p->popupTakesKeys(),
                  "popupen är ett eget popupfönster i App-registret som tar "
                  "tangentbordet (setPopupTakesKeys)");
            d = delar(p);
            check(d.hela(), "popupens delar: palette · red · green · blue · "
                            "greenValue · hex");
            if (!d.hela()) {
                w.close();
                return;
            }
            check(d.palett->focused(), "paletten har fokus när popupen öppnas");
            palettHöjdFörr = palettHöjd(d.palett);
            check(d.röd->value() == 0x33 && d.grön->value() == 0x66 &&
                      d.blå->value() == 0xff && d.hex->text() == "#3366FF" &&
                      d.grönTal->text() == "102",
                  "popupen visar färgen: reglagen, värdet och hexfältet",
                  d.hex->text());
            fallRyms(p, d);

            // PALETTEN: markören först, valet sedan.
            w.tangent(gfx::Key::Right);
            check(lika(w.farg.color(), 0x33, 0x66, 0xff) && w.ändringar == 0,
                  "paletten: pilen flyttar markören — färgen orörd, inget event",
                  hex(w.farg.color()));
            w.tangent(gfx::Key::Return);
            check(lika(w.farg.color(), pal.colors[0]) && w.ändringar == 1 &&
                      w.commits == 1,
                  "paletten: Enter VÄLJER rutan (första pilen gick till ruta 0) "
                  "— ett onChange och ett onCommit",
                  hex(w.farg.color()) + " / " + std::to_string(w.ändringar) +
                      "/" + std::to_string(w.commits));
            break;
        }
        case 1:
            check(!w.farg.isOpen(), "paletten: valet STÄNGER popupen");
            w.tangent(gfx::Key::Space);
            // Markören börjar på den färg kontrollen har (ruta 0): höger ett
            // steg, ner en hel rad.
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Return);
            check(lika(w.farg.color(),
                       pal.colors[1 + static_cast<std::size_t>(pal.columns)]) &&
                      w.ändringar == 2,
                  "paletten: markören börjar på kontrollens färg, Ner hoppar en "
                  "rad (" + std::to_string(pal.columns) + " rutor)",
                  hex(w.farg.color()));
            break;
        case 2: {
            check(!w.farg.isOpen(), "paletten: stängd igen");
            w.farg.setColor("#FFCC80");
            const int före = w.ändringar;
            w.tangent(gfx::Key::Space);
            p = popupen(w);
            d = delar(p);
            if (!d.hela()) {
                check(false, "popupen öppnades igen");
                w.close();
                return;
            }
            // Tab vandrar I POPUPEN — ägaren skickar den vidare.
            w.tangent(gfx::Key::Tab);
            w.tangent(gfx::Key::Tab);
            check(d.grön->focused(), "Tab ×2: GRÖNT reglage har fokus");
            const int förC = w.commits;
            w.tangent(gfx::Key::Right);
            check(lika(w.farg.color(), 0xff, 0xcd, 0x80) &&
                      w.ändringar == före + 1 && w.commits == förC + 1,
                  "reglaget: en pil ändrar färgen — ett onChange och ett "
                  "onCommit (ett steg är en hel handling)",
                  hex(w.farg.color()));
            check(d.hex->text() == "#FFCD80" && d.grönTal->text() == "205",
                  "reglaget: hexfältet och värdet följer med", d.hex->text());

            // PROGRAMMATISK sättning medan popupen är öppen: synkad, tyst.
            w.farg.setColor("#00FF00");
            check(d.grön->value() == 255 && d.hex->text() == "#00FF00" &&
                      w.ändringar == före + 1 && w.commits == förC + 1,
                  "setColor medan popupen är öppen synkar den — utan event",
                  d.hex->text());

            // HEXFÄLTET — COMMITTAT: att skriva ändrar ingenting.
            w.tangent(gfx::Key::Tab);
            w.tangent(gfx::Key::Tab);
            check(d.hex->focused(), "Tab ×2 till: hexfältet har fokus");
            w.tangent(gfx::Key::A, kRedigera);   // markera allt
            hexInnan   = w.ändringar;
            hexCommits = w.commits;
            for (const char* t : {"#", "1", "2", "3", "4", "5"})
                w.text(t);
            check(lika(w.farg.color(), 0, 0xff, 0) && w.ändringar == hexInnan,
                  "hexfältet: halvskrivet (\"#12345\") ändrar ingenting",
                  hex(w.farg.color()));
            w.text("6");
            check(lika(w.farg.color(), 0, 0xff, 0) && w.ändringar == hexInnan &&
                      d.hex->text() == "#123456",
                  "hexfältet: en hel färg ändrar INGENTING medan man skriver — "
                  "texten är användarens", hex(w.farg.color()));
            w.text("z");
            check(d.hex->text() == "#123456", "hexfältet: 'z' sväljs",
                  d.hex->text());
            // TAB VIDARE committar: färgen och reglagen DIREKT, eventen
            // postade (fokusbytet pågår) — de räknas i nästa steg.
            w.tangent(gfx::Key::Tab);
            check(d.palett->focused() &&
                      lika(w.farg.color(), 0x12, 0x34, 0x56) &&
                      d.röd->value() == 0x12 && d.hex->text() == "#123456" &&
                      w.ändringar == hexInnan,
                  "hexfältet: Tab vidare committar — färgen och reglagen "
                  "direkt, eventen INTE i fokusbytet", hex(w.farg.color()));
            break;
        }
        case 3: {
            p = popupen(w);
            d = delar(p);
            if (!d.hela()) {
                check(false, "popupen står kvar efter hexfältets Tab");
                w.close();
                return;
            }
            check(w.ändringar == hexInnan + 1 && w.commits == hexCommits + 1,
                  "hexfältet: …det POSTADE onChange och onCommit kom i "
                  "pump-svansen", std::to_string(w.ändringar - hexInnan) + "/" +
                                      std::to_string(w.commits - hexCommits));
            // Tillbaka till hexfältet (Shift+Tab från paletten slår runt).
            w.tangent(gfx::Key::Tab, gfx::kModShift);
            check(d.hex->focused(), "Shift+Tab: hexfältet igen");
            w.tangent(gfx::Key::A, kRedigera);
            w.text("abcdef");
            check(lika(w.farg.color(), 0x12, 0x34, 0x56) &&
                      w.ändringar == hexInnan + 1 && d.hex->text() == "abcdef",
                  "hexfältet: gemener utan # skrivna — texten står som skriven",
                  d.hex->text());
            w.tangent(gfx::Key::Return);
            check(lika(w.farg.color(), 0xab, 0xcd, 0xef) &&
                      w.ändringar == hexInnan + 2 &&
                      w.commits == hexCommits + 2 &&
                      d.hex->text() == "#ABCDEF",
                  "hexfältet: Enter committar (gemener tolkas) — ett event "
                  "direkt — och skriver om texten till kanonisk form",
                  d.hex->text());
            break;
        }
        case 4: {
            check(!w.farg.isOpen(), "hexfältet: Enter STÄNGER popupen");
            const int innan = w.ändringar;
            w.tangent(gfx::Key::Space);
            w.tangent(gfx::Key::Escape);
            check(lika(w.farg.color(), 0xab, 0xcd, 0xef) && w.ändringar == innan,
                  "Escape: ingen ändring, inget event", hex(w.farg.color()));
            break;
        }
        case 5: {
            check(!w.farg.isOpen(), "Escape stänger popupen");
            // ÄGARENS INGÅNG — Windows/macOS/X11:s väg, in i
            // popupens egen ingång, den Waylands grab levererar till: Tab +
            // pil går till popupen, och ÄGARENS fokus står kvar (ägaren
            // routade inte Tab:en själv).
            w.tangent(gfx::Key::Space);
            p = popupen(w);
            d = delar(p);
            if (!p || !d.hela()) {
                check(false, "popupen öppnades igen");
                w.close();
                return;
            }
            const int innan = w.ändringar;
            w.tangent(gfx::Key::Tab);
            w.tangent(gfx::Key::Left);
            check(d.röd->focused() && w.farg.focused() &&
                      lika(w.farg.color(), 0xaa, 0xcd, 0xef) &&
                      w.ändringar == innan + 1,
                  "ägarens ingång: Tab + pil går till popupen, ägarens fokus "
                  "står kvar",
                  hex(w.farg.color()));
            // Ett tryck på en annan widget i ägaren tar fokus — och stänger.
            w.tryck(w.före);
            check(w.före.focused(), "trycket utanför flyttar fokus");
            break;
        }
        case 6:
            check(!w.farg.isOpen(), "tryck utanför stänger popupen");
            // TOGGLE med musen: öppnar, och ett tryck till stänger.
            w.tryck(w.farg);
            check(w.farg.isOpen() && w.farg.focused(),
                  "tryck på färgväljaren: fokus och öppen popup");
            // Borttagningen nekas medan popupen lever.
            check(!w.root().unadopt(w.farg) && w.farg.parent() == &w.root(),
                  "öppen popup: unadopt NEKAS och trädet är orört");
            w.tryck(w.farg);
            break;
        case 7: {
            check(!w.farg.isOpen(),
                  "tryck igen: stänger (ingen återöppning)");
            check(glaze::app().windowCount() == 1,
                  "ingen popup lever kvar",
                  std::to_string(glaze::app().windowCount()));
            w.av.open();
            check(!w.av.isOpen(), "inaktiverad: open() öppnar ingenting");

            // STILBYTE MELLAN ÖPPNINGAR: popupen har lagts ut i Glaze flera
            // gånger, och nu i Minimalistic ska luften mellan paletten och
            // första raden vara DEN stilens `colorPopupGap` — en layout som
            // mätt med förra öppningens satta mått hade gett Glazes palett.
            w.setUI(glaze::UI::Minimalistic);
            w.farg.open();
            d = delar(popupen(w));
            if (d.hela()) {
                const glaze::Ctx c   = d.palett->behaviorCtx();
                const gfx::Rectf pr  = d.palett->resolve(c);
                const gfx::Rectf rr  = d.röd->resolve(d.röd->behaviorCtx());
                const float      gap = (rr.y - (pr.y + pr.h)) / c.scale;
                check(gap > w.style().colorPopupGap - 0.5f &&
                          gap < w.style().colorPopupGap + 0.5f,
                      "stilbyte: luften palett → första raden är den NYA "
                      "stilens colorPopupGap",
                      std::to_string(gap));
                // Minimalistics rutor är mindre än Glazes — en palett som
                // står kvar i förra öppningens storlek är det felet.
                const float h = palettHöjd(d.palett);
                check(palettHöjdFörr > 0.0f && h < palettHöjdFörr,
                      "stilbyte: paletten krymper med den tätare stilen",
                      std::to_string(h) + " mot " +
                          std::to_string(palettHöjdFörr));
            } else {
                check(false, "stilbyte: popupen öppnades");
            }
            w.farg.close();
            break;
        }
        case 8:
            w.setUI(glaze::UI::Glaze);
            w.close();
            return;
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();

    if (!kedjanKördes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ================================================================ --png
//
// ATT ÖGNA, inte en del av grinden: den stängda kontrollen (normal, med
// fokus, inaktiverad) och den öppna popupen per UI × läge.
void skrivPng()
{
    struct Ögon : glaze::AppWindow {
        using glaze::AppWindow::handleEvent;
        glaze::ColorPicker n, v, a;
        // En tangent genom ägarens ingång — popupen tar den.
        void tangent(gfx::Key k)
        {
            gfx::Event e;
            e.type     = gfx::EventType::KeyDown;
            e.windowId = window().id();
            e.key      = k;
            handleEvent(e);
        }
        Ögon() : glaze::AppWindow("colorpicker_gate --png", 260, 60)
        {
            setContentSize(260, 56);
            n.x = 10;  n.y = 14; n.setColor("#3366FF");
            v.x = 92;  v.y = 14; v.setColor("#FFFFFF");
            a.x = 174; a.y = 14; a.setColor("#E53935"); a.disable();
            root().adopt(n);
            root().adopt(v);
            root().adopt(a);
        }
    } w;
    w.setOffscreen(true);

    const glaze::UI       ui[]   = {glaze::UI::Glaze, glaze::UI::Slate,
                                    glaze::UI::Minimalistic};
    const char*           uiN[]  = {"Glaze", "Slate", "Minimalistic"};
    const glaze::ThemeMode läge[] = {glaze::ThemeMode::Light,
                                     glaze::ThemeMode::Dark};
    const char*           lägeN[] = {"ljust", "morkt"};
    int                   i       = 0;
    std::function<void()> nästa;
    nästa = [&] {
        if (!w.isOpen() || i >= 6) {
            w.close();
            return;
        }
        w.setUI(ui[i / 2]);
        w.setThemeMode(läge[i % 2]);
        const std::string bas =
            g_png + "/colorpicker_" + uiN[i / 2] + "_" + lägeN[i % 2];
        // En färg UR paletten och markören ett steg till höger, så både
        // den valda rutans ring och markörens syns.
        w.n.setColor(glaze::standardPalette().colors[27]);
        w.n.focus();
        w.n.open();
        if (w.n.isOpen())
            w.tangent(gfx::Key::Right);   // ägarens ingång → popupen
        std::vector<std::uint8_t> px;
        int                       pw = 0, ph = 0;
        w.invalidate();
        w.window().renderFrame();
        if (w.window().captureFrame(px, pw, ph))
            gfx::encodePng(bas + "_kontroll.png", px, pw, ph);
        if (glaze::AppWindow* p = popupen(w)) {
            p->invalidate();
            p->window().renderFrame();
            if (p->window().captureFrame(px, pw, ph))
                gfx::encodePng(bas + "_popup.png", px, pw, ph);
        }
        w.n.close();
        ++i;
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();
    std::printf("     --png: bilderna i %s\n", g_png.c_str());
}

// ------------------------------------------------------- A6 FÄRGDIALOGEN

struct DialogProv : glaze::ColorDialogWindow {
    using glaze::ColorDialogWindow::ColorDialogWindow;
    using glaze::AppWindow::handleEvent;

    void tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void text(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, s, sizeof e.text - 1);
        handleEvent(e);
    }
};

void fallFargdialog()
{
    glaze::setLanguage("sv");
    const auto& pal = glaze::standardPalette();

    // OK genom Enter i hexfältet — med alfan.
    {
        glaze::ColorDialog cfg;
        cfg.setAllowAlpha(true);
        cfg.setColor(gfx::Color{0x33, 0x66, 0xFF, 0x80});
        DialogProv w(cfg);
        w.setOffscreen(true);
        bool kordes = false;
        glaze::app().post([&] {
            kordes = true;
            if (!w.isOpen()) {
                std::printf("  --   A6 hoppas över: dialogen kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            auto* hx    = w.find<glaze::TextInput>("hex");
            auto* alfa  = w.find<glaze::Slider>("alpha");
            auto* palet = w.find<glaze::Widget>("palette");
            check(hx && hx->text() == "#3366FF80" && alfa &&
                      alfa->isVisible() && alfa->value() == 128.0f,
                  "A6: dialogen visar förvalet med alfan",
                  hx ? hx->text() : "");
            check(palet && palet->focused() && w.acceptButton() &&
                      w.cancelButton(),
                  "A6: paletten har fokus, OK och Avbryt är fönstrets knappar");
            w.tangent(gfx::Key::Right);   // markören till första rutan — VALD
            const gfx::Color f0 = pal.colors.front();
            check(w.selected() == gfx::Color{f0.r, f0.g, f0.b, 0x80},
                  "A6: pilen VÄLJER i dialogen och alfan står kvar",
                  hex(w.selected()));
            check(w.isOpen(), "A6: ett val stänger inte dialogen");
            if (hx) {
                hx->focus();
                hx->selectAll();
                w.text("#11223344");
                w.tangent(gfx::Key::Return);
            }
        });
        w.run();
        check(kordes && w.accepted() &&
                  cfg.color() == gfx::Color{0x11, 0x22, 0x33, 0x44},
              "A6: ett skrivet hexvärde + Enter ⇒ OK med den färgen",
              hex(cfg.color()));
    }

    // Escape ⇒ Cancel och färgen orörd — utan alfan är A-raden dold.
    {
        glaze::ColorDialog cfg;
        cfg.setColor(gfx::Color{10, 20, 30, 255});
        DialogProv w(cfg);
        w.setOffscreen(true);
        bool kordes = false;
        glaze::app().post([&] {
            kordes = true;
            if (!w.isOpen()) {
                ++g_skips;
                w.close();
                return;
            }
            auto* alfa = w.find<glaze::Slider>("alpha");
            check(alfa && !alfa->isVisible(), "A6: utan alfan är A-raden dold");
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Escape);
        });
        w.run();
        check(kordes && !w.accepted() && cfg.color() == gfx::Color{10, 20, 30},
              "A6: Escape ⇒ Cancel och färgen orörd", hex(cfg.color()));
    }

    // OK genom `performClick` med fokus KVAR i hexfältet — det skrivna ska
    // committas av OK själv (`commitEdits`), inte av en Enter.
    {
        glaze::ColorDialog cfg;
        DialogProv         w(cfg);
        w.setOffscreen(true);
        glaze::app().post([&] {
            if (!w.isOpen()) {
                ++g_skips;
                w.close();
                return;
            }
            if (auto* hx = w.find<glaze::TextInput>("hex")) {
                hx->focus();
                hx->selectAll();
                w.text("#ABCDEF");
            }
            if (glaze::Button* ok = w.acceptButton())
                ok->performClick();
        });
        w.run();
        check(w.accepted() && cfg.color() == gfx::Color{0xAB, 0xCD, 0xEF},
              "A6: OK committar ett skrivet hexvärde som inte bekräftats",
              hex(cfg.color()));
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
            g_png = argv[++i];
    }

    std::printf("colorpicker_gate\n");
    fallTextform();
    fallProgrammatisk();
    fallSetProp();
    fallAlfa();
    fallFargytan();
    fallUtanFonster();
    fallMedFönster();
    fallFargdialog();
    if (!g_png.empty())
        skrivPng();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
