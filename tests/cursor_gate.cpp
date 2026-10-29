// cursor_gate — PEKARNA.
//
// Kör:  ./build-test/tests/cursor_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterblocket kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
//   C1  DEN STÄNGDA HANDENS BILD (`gfx/src/cursor_bitmaps.h`), headless:
//       `expandCursor(kClosedHand, 2)` ger 32×32 RGBA8 med hotspot (16, 16),
//       och VARJE pixel jämförs med mönstret (förväntat värde läses ur mönstrets egna
//       tecken, inte ur expansionen): `.` ⇒ alfa 0, `X` ⇒ opak svart, `o` ⇒
//       opak vit. Skala 1 ger 16×16 och hotspot (8, 8). Mönstret själv: 16
//       rader om 16 tecken, bara de tre tecknen, och hotspoten inne i bilden.
//   C2  OMPRÖVNINGEN i ett offscreen-fönster med en knapp, genom fönstrets
//       riktiga ingång (`handleEvent`) med EN syntetisk rörelse över knappen:
//       `setCursor(Hand)` ⇒ handen UTAN ny rörelse · `resetCursor()` ⇒ pilen
//       · `setCursor(Hand)` IGEN och sedan `disable()` ⇒ pilen (fallet
//       bevisar omprövningen, inte bara återställningen) ·
//       `enable()` ⇒ handen igen · `hide()` ⇒ pilen · `show()` +
//       `setUseWaitCursor(true)` ⇒ väntepekaren direkt, false ⇒ tillbaka ·
//       knappen LOSSAD ur trädet (`unadopt`) ⇒ pilen.
//   C3  RITYTANS GESTER: en panorerbar och vridbar `Canvas` — tryck +
//       drag ⇒ den STÄNGDA HANDEN, Ctrl + drag (vridningen) ⇒ flytta-pekaren,
//       och hårkorset igen efter släppet.
//   C4  `setProp("cursor", …)`, headless: `gfx::PointerShape::ClosedHand`,
//       `PointerShape::ClosedHand`, `ClosedHand` och previewns gemena
//       `closedhand` ger alla `cursor() == ClosedHand`; `Blaha` och ett ANNAT
//       prefix (`glaze::Dock::Hand`) svarar true (känd nyckel) och ändrar
//       ingenting.
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · cursor_bitmaps.h expandCursor: hotspoten inte skalad (`hotX =
//     b.hotX`) ........................................................ C1
//   · cursor_bitmaps.h expandCursor: `o` och `X` byter färg ............. C1
//   · app_window.cpp updateCursor: läser inte `cursor_` ................. C2
//   · app_window.cpp setUseWaitCursor: utan `refreshCursor()` ........... C2
//     (direktkontrollen)
//   · widget.cpp setCursor: utan `refreshPointer()` ..................... C2
//   · widget.cpp setEnabled: utan `refreshPointer()` .................... C2
//     (disable-fallet efter den nya `setCursor(Hand)`)
//   · widget.cpp detach: utan `refreshCursor()` ......................... C2
//   · app_window.cpp updateCursor: `w->isEnabled()`-villkoret bort ...... C2
//   · canvas.cpp cursorAt: `Pan` ⇒ `Move` ............................... C3
//   · widget.cpp setProp: bara den bara token godtas .................... C4
//   · widget.cpp setProp: valfritt prefix godtas (`rfind("::")`) ........ C4
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · hur pekarna SER UT på skärmen — OS-pekaren byggd av bilden
//     (`CreateIconIndirect`, `XCreatePixmapCursor`), att `Hidden` verkligen
//     döljer och att väntepekaren animeras: `gfx::Window::cursor()` svarar
//     med BEGÄRAN (doc/os/windows/TESTS.md rad 3).
//   · storleken per DPI (`SM_CXCURSOR` / 16, X11:s skala) — C1 mäter
//     expansionen vid en given skala, inte vilken skala backenden väljer.
//   · Metal (`closedHandCursor`, den genomskinliga pekaren, pilen för
//     `Wait`) och Wayland (`grabbing`/`wait`, `NULL`-ytan) — de pekarna mäts inte här.
//   · en RIKTIG mus: rörelserna är syntetiska, och `MouseLeave` från OS:et
//     är inte med.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "cursor_bitmaps.h"   // intern — Glaze/gfx/src

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

std::string tal(long long v) { return std::to_string(v); }

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

// ===================================================== C1 BILDEN (headless)

void delC1()
{
    std::printf("\n=== C1. den stängda handens bild ===\n");
    const gfx::CursorBitmap& b = gfx::kClosedHand;

    // Mönstret självt — indata till expansionen, kontrollerad för sig.
    bool form = b.size == 16;
    for (int r = 0; r < 16; ++r) {
        const char* rad = b.rows[r];
        form = form && rad && std::strlen(rad) == 16;
        for (int k = 0; form && k < 16; ++k)
            form = rad[k] == '.' || rad[k] == 'X' || rad[k] == 'o';
    }
    check(form, "C1 mönstret: 16 rader om 16 tecken, bara '.', 'X' och 'o'");
    check(b.hotX >= 0 && b.hotX < 16 && b.hotY >= 0 && b.hotY < 16 &&
              b.rows[b.hotY][b.hotX] != '.',
          "C1 hotspoten ligger inne i bilden, på handen och inte i luften",
          tal(b.hotX) + "," + tal(b.hotY));
    if (!form)
        return;

    for (const int skala : {2, 1}) {
        int                             hx = -1, hy = -1;
        const std::vector<std::uint8_t> px =
            gfx::expandCursor(b, skala, hx, hy);
        const int sida = 16 * skala;
        check(static_cast<int>(px.size()) == sida * sida * 4,
              "C1 skala " + tal(skala) + ": " + tal(sida) + "×" + tal(sida) +
                  " RGBA8",
              tal(static_cast<long long>(px.size())) + " byte");
        check(hx == b.hotX * skala && hy == b.hotY * skala,
              "C1 skala " + tal(skala) + ": hotspoten skalas med bilden",
              tal(hx) + "," + tal(hy));
        if (static_cast<int>(px.size()) != sida * sida * 4)
            continue;
        int fel = 0;
        for (int y = 0; y < sida; ++y)
            for (int x = 0; x < sida; ++x) {
                const char           t = b.rows[y / skala][x / skala];
                const std::uint8_t*  p = &px[(y * sida + x) * 4];
                const bool ok =
                    t == '.' ? p[3] == 0
                    : t == 'X'
                        ? (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255)
                        : (p[0] == 255 && p[1] == 255 && p[2] == 255 &&
                           p[3] == 255);
                fel += ok ? 0 : 1;
            }
        check(fel == 0,
              "C1 skala " + tal(skala) +
                  ": varje pixel följer mönstret ('.' alfa 0, 'X' svart, "
                  "'o' vit)",
              tal(fel) + " pixlar fel");
    }
    std::printf("  ..   C1 facit: hotspot (%d, %d) vid skala 2 = (%d, %d)\n",
                b.hotX, b.hotY, b.hotX * 2, b.hotY * 2);
}

// ===================================================== C4 setProp (headless)

void delC4()
{
    std::printf("\n=== C4. setProp(\"cursor\", …) ===\n");
    for (const char* v : {"gfx::PointerShape::ClosedHand",
                          "PointerShape::ClosedHand", "ClosedHand",
                          "closedhand"}) {
        glaze::Button b;
        const bool    känd = b.setProp("cursor", v);
        check(känd && b.cursor() == gfx::PointerShape::ClosedHand,
              std::string("C4 '") + v + "' ⇒ ClosedHand",
              b.cursor() ? formNamn(*b.cursor()) : "(ingen)");
    }
    for (const char* v : {"Blaha", "glaze::Dock::Hand"}) {
        glaze::Button b;
        b.setCursor(gfx::PointerShape::IBeam);
        const bool känd = b.setProp("cursor", v);
        check(känd && b.cursor() == gfx::PointerShape::IBeam,
              std::string("C4 '") + v +
                  "' ⇒ true (känd nyckel) och ingen ändring",
              b.cursor() ? formNamn(*b.cursor()) : "(ingen)");
    }
    glaze::Button b;
    check(!b.cursor().has_value(), "C4 förvalet: ingen satt form");
    b.setCursor(gfx::PointerShape::Wait);
    b.resetCursor();
    check(!b.cursor().has_value(), "C4 resetCursor() ⇒ ingen satt form igen");
}

// ================================================= C2 + C3 med fönster

class Värd : public glaze::AppWindow {
public:
    Värd() : glaze::AppWindow("cursor_gate", 520, 360, gfx::Chrome::None)
    {
        setOffscreen(true);
        knapp.name   = "knapp";
        knapp.x      = 20;
        knapp.y      = 20;
        knapp.width  = 120;
        knapp.height = 40;
        root().adopt(knapp);

        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 100;
        yta.width  = 300;
        yta.height = 200;
        yta.setBorder(false);
        yta.setPannable(true);
        yta.setRotatable(true);
        root().adopt(yta);
    }

    glaze::Button knapp{"Knapp"};
    glaze::Canvas yta;

    // Mitten av en widget i fönstrets FYSISKA px.
    gfx::Pointf mitt(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + 0.5f * r.w, r.y + 0.5f * r.h};
    }
    void mus(gfx::EventType t, gfx::Pointf p,
             gfx::MouseButton b = gfx::MouseButton::None, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = b;
        e.mods     = mods;
        handleEvent(e);
    }
    gfx::PointerShape pekare() { return window().cursor(); }

    void steg();
};

void Värd::steg()
{
    window().renderFrame();   // ytan och kameran som de ritas

    // ---- C2 omprövningen — EN rörelse, därefter bara settrar.
    const gfx::Pointf över = mitt(knapp);
    mus(gfx::EventType::MouseMotion, över);
    const gfx::PointerShape egen = pekare();
    check(egen == gfx::PointerShape::Arrow,
          "C2 (fixturen) knappens egen pekare är pilen", formNamn(egen));

    knapp.setCursor(gfx::PointerShape::Hand);
    check(pekare() == gfx::PointerShape::Hand,
          "C2 setCursor(Hand) ⇒ handen UTAN ny rörelse", formNamn(pekare()));
    knapp.resetCursor();
    check(pekare() == gfx::PointerShape::Arrow,
          "C2 resetCursor() ⇒ pilen", formNamn(pekare()));

    knapp.setCursor(gfx::PointerShape::Hand);
    check(pekare() == gfx::PointerShape::Hand,
          "C2 (fixturen) handen igen före disable()", formNamn(pekare()));
    knapp.disable();
    check(pekare() == gfx::PointerShape::Arrow,
          "C2 disable() ⇒ pilen — en inaktiv widget visar ingen egen form",
          formNamn(pekare()));
    knapp.enable();
    check(pekare() == gfx::PointerShape::Hand,
          "C2 enable() ⇒ handen igen", formNamn(pekare()));
    knapp.hide();
    check(pekare() == gfx::PointerShape::Arrow,
          "C2 hide() ⇒ pilen (pekaren står över fönstrets yta)",
          formNamn(pekare()));
    knapp.show();
    check(pekare() == gfx::PointerShape::Hand,
          "C2 show() ⇒ handen", formNamn(pekare()));

    setUseWaitCursor(true);
    check(pekare() == gfx::PointerShape::Wait,
          "C2 setUseWaitCursor(true) ⇒ väntepekaren direkt", formNamn(pekare()));
    setUseWaitCursor(false);
    check(pekare() == gfx::PointerShape::Hand,
          "C2 setUseWaitCursor(false) ⇒ tillbaka till knappens hand",
          formNamn(pekare()));

    // ---- C3 ritytans gester. Musen dit först — därefter tryck och drag.
    const gfx::Pointf p0 = mitt(yta);
    const gfx::Pointf p1{p0.x + 30.0f, p0.y + 10.0f};
    mus(gfx::EventType::MouseMotion, p0);
    check(pekare() == gfx::PointerShape::Crosshair,
          "C3 (fixturen) hårkorset över en rityta med gester",
          formNamn(pekare()));
    mus(gfx::EventType::MouseButtonDown, p0, gfx::MouseButton::Left);
    mus(gfx::EventType::MouseMotion, p1);
    check(pekare() == gfx::PointerShape::ClosedHand,
          "C3 panoreringen ⇒ den stängda handen", formNamn(pekare()));
    mus(gfx::EventType::MouseButtonUp, p1, gfx::MouseButton::Left);
    check(pekare() == gfx::PointerShape::Crosshair,
          "C3 hårkorset igen efter släppet", formNamn(pekare()));
    mus(gfx::EventType::MouseButtonDown, p0, gfx::MouseButton::Left,
        gfx::kModCtrl);
    mus(gfx::EventType::MouseMotion, p1, gfx::MouseButton::None,
        gfx::kModCtrl);
    check(pekare() == gfx::PointerShape::Move,
          "C3 Ctrl + drag (vridningen) ⇒ flytta-pekaren", formNamn(pekare()));
    mus(gfx::EventType::MouseButtonUp, p1, gfx::MouseButton::Left,
        gfx::kModCtrl);

    // ---- C2 sist: knappen LOSSAS medan pekaren står över den.
    mus(gfx::EventType::MouseMotion, över);
    check(pekare() == gfx::PointerShape::Hand,
          "C2 (fixturen) handen över knappen före lossningen",
          formNamn(pekare()));
    root().unadopt(knapp);
    check(pekare() == gfx::PointerShape::Arrow,
          "C2 knappen lossad ur trädet ⇒ pilen (omprövad efter mutationen)",
          formNamn(pekare()));

    close();
}

void delFönster()
{
    std::printf("\n=== C2 + C3. omprövningen och ritytans gester ===\n");
    Värd w;
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.steg();
    });
    w.run();
    if (!kördes) {
        std::printf("  --   C2/C3 hoppas över: inget fönster kunde skapas "
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

    std::printf("cursor_gate — pekarna\n");
    delC1();
    delC4();
    delFönster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "CURSOR_GATE GRÖN" : "CURSOR_GATE FÄLLD",
                g_cases, g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
