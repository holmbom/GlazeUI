// plumbing_gate — RAMVERKETS RÖRMOKERI FÖR RITYTANS 3D: tre små ytor
// som ritytans 3D-del bygger på men som bor i `Widget`,
// `AppWindow` och `App`/`ImageCache`, inte i ritytan. Konsumenterna (3D-målets
// cache, tickningen, meshsegmenten) grindas för sig; den här grinden
// mäter att grunden håller vad den lovar.
//
// Kör:  ./build/tests/plumbing_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
//   S  DAMAGE-STÄMPELN (`Widget::damageStamp`). Widgetens egen `invalidate()`
//      ger +1, ett BARNS ger föräldern +0, fönstrets `invalidate()` ger alla
//      +1, och ett klick på en knapp med handler ger alla EXAKT +1 — genom
//      fönstrets riktiga ingång (`handleEvent`), så det är fönstrets egen
//      fulla invalidering efter handlern som mäts och inte ett anrop grinden
//      gör. `invalidateRect`, `invalidateOverlay` och den RITADE vägen
//      (animering = full omritning varje frame) ger +0.
//   K  DE HÅLLNA TANGENTERNA (`AppWindow::isKeyHeld`), genom `handleEvent`:
//      ned/upp, två tangenter, upprepning, ett event appens krok ÄTER, att
//      mängden redan är uppdaterad NÄR kroken körs, `Unknown` och ett värde
//      utanför enumet, numpaden (den ovikta posten exakt, den dedikerade som
//      en widget ser den — också inifrån en fokuserad widgets `keyDown`),
//      numpadens två lägen som en fysisk tangent, och tömningen vid
//      `WindowFocusLost`, `hide()` och stängningen.
//   B  UPPLADDNINGSBUDGETEN (`App::setCanvasUploadBudget`). Headless mot en
//      räknande renderer och en riktig `ImageCache`: högst budgeten per
//      frame, resten nästa frame, framens första uppladdning alltid igenom,
//      en ändrad bild som inte ryms och en ändrad budget. Med fönstret:
//      förvalet, avslagen (0 och negativt), och att App:ens värde når
//      bildcachen genom `renderContent` — det som väntar ritas nästa frame
//      utan att grinden ber om den, och därefter ritas ingenting (regel 1).
//   PX BILDEN SOM SLÄPPER SINA PIXLAR (`setKeepPixels`).
//      Headless — två bildcacher är två fönster och `releaseQueuedPixels`
//      pumpens släpp: PX1 efter släppet pixels() nullptr med bilden och måtten
//      kvar; PX2 en ny cache får ingen textur och laddar inte upp; PX3 två
//      cacher i samma varv får båda texturen; PX4 setPixels/fill efter
//      släppet ger ingen ny version och ingen uppladdning; PX5 över budgeten
//      står den släppta kvar och den vanliga vräks; PX6 förvalet släpper
//      aldrig; PX7 setKeepPixels(true) före släppet avbryter; PX8 en uppskjuten
//      bild släpps inte; PX9 en kopia av handtaget ser släppet. PX10 med
//      fönstret: släppt efter pumpvarvet (inte i ramen), fönstret ritar vidare,
//      och en ny bildcache mot samma renderare (ett fönster öppnat efteråt)
//      får ingen textur. Sabotage som SKA fälla dem (skrivna):
//      släppet direkt i `get` (PX3), LRU utan undantaget (PX5), en ny
//      post ur en tom buffert (PX2), `keepPixels` ignorerad (PX6), `setPixels`
//      utan vakt (PX4). MÄTER INTE en förlorad enhet, Metal och Linux eller
//      kapplöpningen mellan trådar, och PX10:s "nya fönster" är en ny cache,
//      inte ett riktigt andra fönster.
//   R  RENDERARENS RÄKNARE mot fönstrets riktiga renderare:
//      R1 tre `createRenderTarget` (ett 2D-mål, ett med djup, ett med MSAA)
//      ger `targetsCreated` +3; R2 ett nytt program ger `compileNs` > 0; R3
//      på D3D11, vars bytekod cachas per process, ger samma källa en gång
//      till `compileNs` +0. Sabotage som SKA fälla dem (skrivna):
//      räkna målen i `bindTarget` i stället för i
//      `createRenderTarget` (R1), tiden runt cache-uppslaget i stället för
//      runt `D3DCompile` (R3), ingen tid alls (R2). MÄTER INTE att tiden är
//      rätt, `presentNs` (ett dolt fönster väntar inte på vsync) eller Metal
//      och GL (doc/os/*/TESTS.md).
//
// SABOTAGEN den fäller (prövade i en privat kopia, alla tolv röda):
//   * stämpeln stegas inte vid full invalidering (S3, S4)
//   * stämpeln stegas också av ett barns invalidate (S2)
//   * invalidateRect stegar stämpeln (S0, S5)
//   * mängden töms inte vid fokusförlust (K10)
//   * mängden uppdateras EFTER appens krok (K1, K3, K4, K10)
//   * mängden töms inte vid hide/stängning (K12)
//   * frågan viker inte numpaden (K7, K11) · tvillingen släpps inte (K8)
//   * budgeten ignoreras — ett fast tak på 16 MiB, eller inget tak
//     alls (B1–B5, W4)
//   * renderContent lämnar inte App:ens budget till cachen (W4)
//   * sättaren validerar inte (W2)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * att backenderna LEVERERAR `KeyUp` för varje `KeyDown` med samma
//     `gfx::Key` — grinden matar ingången direkt. Windows Shift + numpad med
//     NumLock på (ett tryck som börjar som `KP_Up` och släpps som `KP_N8`)
//     är skälet till tvillingregeln; att OS:et faktiskt gör så är ett
//     antagande som går att pröva med ett riktigt tangentbord först när en
//     konsument finns (ritytans tickning).
//   * att en layoutväxling mitt i ett tryck lämnar en tangent kvar till
//     nästa fokusförlust — det är en skriven begränsning (app_window.h).
//   * konsumenterna: 3D-målets cache (`targetRenders()`), tickningen och
//     meshsegmentens del av budgeten grindas i sina egna
//     grindar.
//   * uppladdningens TID — budgeten räknas i byte, aldrig i millisekunder.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <glaze/glaze.h>

#include "canvas/image_data.h"   // intern — pumpens släpp (PX)
#include "image_cache.h"   // intern — fönstrets bildcache (Glaze/widgets/src)

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

std::string tal(std::uint64_t v) { return std::to_string(v); }

constexpr std::size_t kKiB    = 1024;
constexpr std::size_t kMiB    = kKiB * kKiB;
constexpr int         kSida   = 256;                     // plattans sida
constexpr std::size_t kPlatta = std::size_t{kSida} * kSida * 4;   // 256 KiB

// ================================================================ HEADLESS

// Placeringen i logiska px — layoutfälten är råa (widget.h).
void lägg(glaze::Widget& w, float x, float y, float b, float h)
{
    w.x      = x;
    w.y      = y;
    w.width  = b;
    w.height = h;
}

// En yta som visar sin skyddade stämpel och kan skada en del av sig själv.
// En Panel, så den kan bära barn och ritas av fönstret som vilken widget som
// helst.
struct Yta : glaze::Panel {
    using glaze::Widget::damageStamp;
    void skadaYta() { invalidateRect(gfx::Rectf{0.0f, 0.0f, 4.0f, 4.0f}); }
};

// S0 — en FRISTÅENDE widget (GlazeBuilders preview): bara den egna delen.
void fallStämpelFristående()
{
    Yta lös;
    const std::uint64_t s0 = lös.damageStamp();
    lös.invalidate();
    check(lös.damageStamp() == s0 + 1,
          "S0 fristående: invalidate() ger +1 utan fönster",
          tal(lös.damageStamp() - s0));
    lös.invalidate();
    lös.skadaYta();
    check(lös.damageStamp() == s0 + 2,
          "S0 fristående: två invalidate() = +2, invalidateRect = +0",
          tal(lös.damageStamp() - s0));
}

// En bild som bara är ett mått — den räknande renderaren ger en sådan.
class Bild : public gfx::Image {
public:
    Bild(int w, int h, gfx::ImageFormat f) : w_(w), h_(h), f_(f) {}
    int              width() const override { return w_; }
    int              height() const override { return h_; }
    bool             valid() const override { return true; }
    gfx::ImageFormat format() const override { return f_; }
    bool             premultiplied() const override { return false; }
    bool             flippedV() const override { return false; }

private:
    int              w_, h_;
    gfx::ImageFormat f_;
};

// Renderaren som RÄKNAR bildernas uppladdade byte, som backenderna gör
// (`RenderStats::bytesUploaded`: createImage med pixlar, updateImage).
class Räknare : public gfx::NullRenderer {
public:
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int w, int h, gfx::ImageFormat f,
                                            const void* px, std::size_t,
                                            bool) override
    {
        if (px)
            stats_.bytesUploaded +=
                static_cast<std::uint64_t>(w) * h * gfx::bytesPerPixel(f);
        return std::make_unique<Bild>(w, h, f);
    }
    bool updateImage(gfx::Image& img, int, int, int w, int h, const void*,
                     std::size_t) override
    {
        stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h *
                                gfx::bytesPerPixel(img.format());
        return true;
    }
};

std::vector<glaze::Bitmap> plattor(int n)
{
    std::vector<glaze::Bitmap> v;
    for (int i = 0; i < n; ++i)
        v.emplace_back(kSida, kSida);
    return v;
}

// Hur många av bilderna har en textur efter `get` i den här framen?
int medTextur(glaze::ImageCache& c, const std::vector<glaze::Bitmap>& v)
{
    int n = 0;
    for (const glaze::Bitmap& b : v)
        if (c.get(b))
            ++n;
    return n;
}

void fallBudgetHeadless()
{
    constexpr std::size_t kAllt = ~std::size_t{0};   // ingen minnesvräkning
    Räknare r;

    // B1/B2 — 1 MiB per frame: fyra plattor nu, fyra nästa frame.
    {
        glaze::ImageCache c(r);
        const auto p = plattor(8);
        c.trim(kAllt, kMiB);
        r.resetStats();
        const int första = medTextur(c, p);
        check(första == 4 && r.stats().bytesUploaded == kMiB &&
                  c.uploadsDeferred(),
              "B1 budgeten 1 MiB: fyra plattor om 256 KiB laddas upp, resten "
              "väntar",
              std::to_string(första) + " plattor, " +
                  tal(r.stats().bytesUploaded) + " byte");
        c.trim(kAllt, kMiB);
        r.resetStats();
        const int andra = medTextur(c, p);
        check(andra == 8 && r.stats().bytesUploaded == kMiB &&
                  !c.uploadsDeferred(),
              "B2 nästa frame: de fyra som väntade, inget mer",
              std::to_string(andra) + " plattor, " +
                  tal(r.stats().bytesUploaded) + " byte");
    }

    // B3 — framens FÖRSTA uppladdning går igenom fast den är större än
    // budgeten, och därefter ryms ingenting mer i framen.
    {
        glaze::ImageCache c(r);
        glaze::Bitmap     stor(512, 1024), liten(8, 8);   // 2 MiB och 256 B
        c.trim(kAllt, kMiB);
        r.resetStats();
        const bool storUppe  = c.get(stor) != nullptr;
        const bool litenUppe = c.get(liten) != nullptr;
        check(storUppe && !litenUppe && c.uploadsDeferred() &&
                  r.stats().bytesUploaded == 2 * kMiB,
              "B3 framens första uppladdning går igenom (2 MiB mot 1 MiB), "
              "nästa väntar",
              tal(r.stats().bytesUploaded) + " byte");
        c.trim(kAllt, kMiB);
        check(c.get(liten) != nullptr && !c.uploadsDeferred(),
              "B3 …och kommer upp i nästa frame");
    }

    // B4 — en ÄNDRAD budget gäller från nästa frame.
    {
        glaze::ImageCache c(r);
        const auto p = plattor(8);
        c.trim(kAllt, 512 * kKiB);
        const int liten = medTextur(c, p);
        c.trim(kAllt, 2 * kMiB);
        r.resetStats();
        const int stor = medTextur(c, p);
        check(liten == 2 && stor == 8 &&
                  r.stats().bytesUploaded == 6 * kPlatta,
              "B4 512 KiB ger två plattor, 2 MiB nästa frame resten sex",
              std::to_string(liten) + " / " + std::to_string(stor) + ", " +
                  tal(r.stats().bytesUploaded) + " byte");
    }

    // B5 — en ÄNDRAD bild som inte ryms ritas med sitt förra innehåll, och
    // ändringen (bara rektangeln) kommer upp nästa frame.
    {
        glaze::ImageCache c(r);
        glaze::Bitmap     gammal(kSida, kSida), stor(512, 1024);
        c.trim(kAllt, kMiB);
        c.get(gammal);
        const std::vector<std::uint8_t> röd(16 * 16 * 4, 255);
        gammal.setPixels(0, 0, 16, 16, röd.data());
        c.trim(kAllt, kMiB);
        r.resetStats();
        c.get(stor);   // framens första — fyller budgeten
        const bool kvar = c.get(gammal) != nullptr;
        check(kvar && c.uploadsDeferred() &&
                  r.stats().bytesUploaded == 2 * kMiB,
              "B5 en ändrad bild som inte ryms har kvar sin textur — "
              "ändringen väntar",
              tal(r.stats().bytesUploaded) + " byte");
        c.trim(kAllt, kMiB);
        r.resetStats();
        c.get(gammal);
        check(r.stats().bytesUploaded == 16 * 16 * 4,
              "B5 …och nästa frame laddar upp bara rektangeln",
              tal(r.stats().bytesUploaded) + " byte");
    }
}

// PX — BILDEN SOM SLÄPPER SINA PIXLAR, headless: två
// bildcacher är två fönster, och `releaseQueuedPixels` är pumpens släpp
// efter varvets ritloop.
void fallPixlarHeadless()
{
    constexpr std::size_t kAllt = ~std::size_t{0};
    Räknare r;

    // PX1–PX3 — A och B får texturen i samma varv; efter släppet är
    // pixlarna borta men bilden och måtten står kvar, och en NY cache får
    // ingen textur och laddar inte upp.
    {
        glaze::ImageCache a(r), b(r), ny(r);
        glaze::Bitmap     bm(kSida, kSida);
        bm.setKeepPixels(false);
        a.trim(kAllt, 16 * kMiB);
        b.trim(kAllt, 16 * kMiB);
        const bool iA = a.get(bm) != nullptr;
        const bool iB = b.get(bm) != nullptr;   // samma varv: pixlarna finns
        glaze::detail::releaseQueuedPixels();
        check(iA && iB, "PX3 två fönster i samma varv får båda texturen");
        check(bm.pixels() == nullptr && bm.valid() && bm.width() == kSida &&
                  bm.height() == kSida && !bm.keepPixels(),
              "PX1 efter varvets släpp: pixels() är nullptr, bilden och måtten "
              "står kvar");
        ny.trim(kAllt, 16 * kMiB);
        r.resetStats();
        const bool ingen = ny.get(bm) == nullptr;
        check(ingen && r.stats().bytesUploaded == 0 && a.get(bm) != nullptr,
              "PX2 ett nytt fönster får ingen textur och laddar inte upp — det "
              "gamla ritar vidare",
              tal(r.stats().bytesUploaded) + " byte");

        // PX4 — setPixels och fill efter släppet ändrar varken versionen i
        // fönstret eller laddar upp.
        const std::uint64_t v0 = a.contentVersion(bm);
        const std::vector<std::uint8_t> röd(16 * 16 * 4, 255);
        bm.setPixels(0, 0, 16, 16, röd.data());
        bm.fill(gfx::Color{0, 0, 255, 255});
        a.trim(kAllt, 16 * kMiB);
        r.resetStats();
        a.get(bm);
        check(a.contentVersion(bm) == v0 && r.stats().bytesUploaded == 0 &&
                  bm.pixels() == nullptr,
              "PX4 setPixels och fill efter släppet: ingen ny version, ingen "
              "uppladdning",
              tal(r.stats().bytesUploaded) + " byte");

        // PX9 — en kopia av handtaget ser släppet.
        const glaze::Bitmap kopia = bm;
        check(kopia.pixels() == nullptr && !kopia.keepPixels(),
              "PX9 en kopia av handtaget ser släppet");
    }

    // PX5 — över budgeten (en platta) står den släppta kvar och den andra
    // vräks.
    {
        glaze::ImageCache c(r);
        glaze::Bitmap     släppt(kSida, kSida), vanlig(kSida, kSida);
        släppt.setKeepPixels(false);
        c.trim(kAllt, 16 * kMiB);
        c.get(släppt);
        glaze::detail::releaseQueuedPixels();
        c.trim(kAllt, 16 * kMiB);
        c.get(vanlig);
        c.trim(kAllt, 16 * kMiB);   // ingenting ritas i den här framen
        c.trim(kPlatta, 16 * kMiB); // två plattor mot en platta
        r.resetStats();
        const bool kvar = c.get(släppt) != nullptr;
        check(kvar && c.canvasCount() == 1 && r.stats().bytesUploaded == 0,
              "PX5 över budgeten vräks den vanliga — den släppta (som inte går "
              "att ladda upp igen) står kvar",
              std::to_string(c.canvasCount()) + " poster");
    }

    // PX6–PX8 — förvalet släpper aldrig; setKeepPixels(true) före släppet
    // avbryter det; en bild som väntar på uppladdningsbudgeten släpps inte.
    {
        glaze::ImageCache c(r);
        glaze::Bitmap     förval(kSida, kSida), ångrad(kSida, kSida),
            stor(512, 1024), väntar(kSida, kSida);
        ångrad.setKeepPixels(false);
        väntar.setKeepPixels(false);
        c.trim(kAllt, kMiB);
        c.get(förval);
        c.get(ångrad);
        ångrad.setKeepPixels(true);
        c.trim(kAllt, kMiB);
        c.get(stor);                              // framens första fyller budgeten
        const bool sköts = c.get(väntar) == nullptr;   // väntar till nästa frame
        glaze::detail::releaseQueuedPixels();
        check(förval.pixels() != nullptr, "PX6 förvalet släpper aldrig pixlarna");
        check(ångrad.pixels() != nullptr && ångrad.keepPixels(),
              "PX7 setKeepPixels(true) före släppet avbryter det");
        check(sköts && väntar.pixels() != nullptr,
              "PX8 en bild som väntar på uppladdningsbudgeten släpps inte");
    }
}

// ============================================================= MED FÖNSTER

// Fångar tangenter som en fokuserad widget: sparar tangenten den fick (den
// VIKTA) och vad `isKeyHeld` svarade på den just då.
struct Fångare : glaze::Panel {
    gfx::Key senast   = gfx::Key::Unknown;
    bool     hölls    = false;
    int      tangenter = 0;
    bool focusable() const override { return true; }

protected:
    bool keyDown(gfx::Key k, std::uint32_t) override
    {
        ++tangenter;
        senast = k;
        hölls  = appWindow() && appWindow()->isKeyHeld(k);
        return false;
    }
};

// Ritar BILDER UR PIXLAR genom fönstrets bildcache, som ritytan gör, och
// räknar hur många som fick en textur i den senaste ritningen.
struct Bildyta : glaze::Panel {
    const std::vector<glaze::Bitmap>* bilder = nullptr;
    int  uppe      = 0;
    bool väntade   = false;
    int  ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        glaze::Panel::draw(c);
        ++ritningar;
        uppe    = 0;
        väntade = false;
        if (!bilder || !c.images)
            return;
        for (const glaze::Bitmap& b : *bilder)
            if (c.images->get(b))
                ++uppe;
        väntade = c.images->uploadsDeferred();
    }
};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Yta           ytan, barn, granne;
    glaze::Button knapp{"Knapp"};
    Fångare       fångare;
    Bildyta       bildyta;
    int           klick = 0;

    // Appens krok: vad `isKeyHeld` svarade på eventets tangent NÄR kroken
    // kördes, och om kroken ska äta eventet.
    bool ät          = false;
    bool kroksvar    = false;
    int  krokanrop   = 0;
    bool krokVHölls  = true;   // vid WindowFocusLost: hölls W fortfarande?

    Vard() : glaze::AppWindow("plumbing_gate", 600, 400, gfx::Chrome::None)
    {
        ytan.name    = "ytan";
        barn.name    = "barn";
        granne.name  = "granne";
        knapp.name   = "knapp";
        fångare.name = "fangare";
        bildyta.name = "bildyta";
        lägg(ytan, 20, 20, 200, 150);
        lägg(barn, 10, 10, 80, 60);
        lägg(granne, 250, 20, 120, 80);
        knapp.x = 250;
        knapp.y = 130;
        lägg(fångare, 400, 20, 80, 60);
        lägg(bildyta, 20, 200, 200, 150);
        knapp.onClick = [this] { ++klick; };
        root().adopt(ytan);
        ytan.adopt(barn);
        root().adopt(granne);
        root().adopt(knapp);
        root().adopt(fångare);
        root().adopt(bildyta);
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::KeyDown ||
            e.type == gfx::EventType::KeyUp) {
            ++krokanrop;
            kroksvar = isKeyHeld(e.key);
            return ät;
        }
        if (e.type == gfx::EventType::WindowFocusLost)
            krokVHölls = isKeyHeld(gfx::Key::W);
        return false;
    }

    // ---- händelserna, genom den RIKTIGA ingången
    void tangent(gfx::EventType t, gfx::Key k)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    void ned(gfx::Key k) { tangent(gfx::EventType::KeyDown, k); }
    void upp(gfx::Key k) { tangent(gfx::EventType::KeyUp, k); }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void klicka(glaze::Widget& w)
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

// Stämplarna före ett steg, och vad varje widget fick av steget.
struct Stämplar {
    std::uint64_t ytan, barn, granne;
};
Stämplar läs(Vard& w)
{
    return Stämplar{w.ytan.damageStamp(), w.barn.damageStamp(),
                    w.granne.damageStamp()};
}
std::string delta(const Stämplar& före, const Stämplar& efter)
{
    return "ytan +" + tal(efter.ytan - före.ytan) + ", barn +" +
           tal(efter.barn - före.barn) + ", granne +" +
           tal(efter.granne - före.granne);
}
bool steg(const Stämplar& före, const Stämplar& efter, std::uint64_t y,
          std::uint64_t b, std::uint64_t g)
{
    return efter.ytan - före.ytan == y && efter.barn - före.barn == b &&
           efter.granne - före.granne == g;
}

void fallStämpel(Vard& w)
{
    w.window().renderFrame();   // töm all damage

    Stämplar f = läs(w);
    w.ytan.invalidate();
    Stämplar e = läs(w);
    check(steg(f, e, 1, 0, 0),
          "S1 widgetens egen invalidate() ger den +1 — barnet och grannen +0",
          delta(f, e));

    f = läs(w);
    w.barn.invalidate();
    e = läs(w);
    check(steg(f, e, 0, 1, 0),
          "S2 ett BARNS egen invalidate() ger föräldern +0", delta(f, e));

    f = läs(w);
    w.invalidate();
    e = läs(w);
    check(steg(f, e, 1, 1, 1),
          "S3 fönstrets invalidate() (full) ger varje widget +1", delta(f, e));

    f = läs(w);
    const int klickFöre = w.klick;
    w.klicka(w.knapp);
    e = läs(w);
    check(w.klick == klickFöre + 1, "S4 (fixturen) knappens handler kördes",
          std::to_string(w.klick - klickFöre));
    check(steg(f, e, 1, 1, 1),
          "S4 ett klick med handler ger EXAKT en full invalidering — +1 för "
          "varje widget, också de klicket inte rörde",
          delta(f, e));

    f = läs(w);
    w.ytan.skadaYta();
    w.invalidateOverlay();
    e = läs(w);
    check(steg(f, e, 0, 0, 0),
          "S5 invalidateRect och invalidateOverlay stegar ingenting",
          delta(f, e));

    // Den RITADE vägen: animering är full omritning varje frame, och det är
    // ingen ändring av innehållet.
    w.window().renderFrame();
    f = läs(w);
    w.setAnimating(true);
    w.window().renderFrame();
    w.window().renderFrame();
    w.setAnimating(false);
    e = läs(w);
    check(steg(f, e, 0, 0, 0),
          "S6 två fulla omritningar (animering) utan invalidering ger +0",
          delta(f, e));
}

void fallTangenter(Vard& w)
{
    using K = gfx::Key;
    w.enkel(gfx::EventType::WindowFocusGained);

    // K1 — ned: hållen, och redan hållen när appens krok kör.
    w.ned(K::W);
    check(w.isKeyHeld(K::W) && !w.isKeyHeld(K::A),
          "K1 KeyDown W: W hålls, A inte");
    check(w.kroksvar, "K1 appens krok ser W som hållen i sitt KeyDown");

    // K2 — två tangenter samtidigt.
    w.ned(K::A);
    check(w.isKeyHeld(K::W) && w.isKeyHeld(K::A), "K2 W och A hålls båda");

    // K3 — upp: släppt, den andra står kvar, och kroken ser släppet.
    w.upp(K::W);
    check(!w.isKeyHeld(K::W) && w.isKeyHeld(K::A),
          "K3 KeyUp W: W släppt, A hålls fortfarande");
    check(!w.kroksvar, "K3 appens krok ser W som släppt i sitt KeyUp");
    w.upp(K::A);

    // K4 — ett event appens krok ÄTER uppdaterar ändå mängden.
    w.ät = true;
    const int anrop = w.krokanrop;
    w.ned(K::S);
    const bool åtNed = w.isKeyHeld(K::S);
    w.upp(K::S);
    const bool åtUpp = w.isKeyHeld(K::S);
    w.ät = false;
    check(w.krokanrop == anrop + 2 && åtNed && !åtUpp,
          "K4 kroken åt KeyDown och KeyUp — mängden följde ändå");

    // K5 — upprepningen (OS:ets och Waylands egen) är en mängd, inte en
    // räknare: ett släpp efter två KeyDown släpper.
    w.ned(K::D);
    w.ned(K::D);
    w.upp(K::D);
    check(!w.isKeyHeld(K::D), "K5 två KeyDown (upprepning), ett KeyUp ⇒ släppt");

    // K6 — Unknown hålls aldrig, och ett värde utanför enumet är ingen
    // tangent.
    w.ned(K::Unknown);
    const K påhittad = static_cast<K>(10000);
    w.ned(påhittad);
    check(!w.isKeyHeld(K::Unknown) && !w.isKeyHeld(påhittad),
          "K6 Unknown och ett värde utanför enumet hålls aldrig");
    w.upp(påhittad);

    // K7 — numpaden: den OVIKTA posten exakt, den DEDIKERADE som en widget
    // ser den.
    w.ned(K::KP_End);
    check(w.isKeyHeld(K::KP_End) && w.isKeyHeld(K::End),
          "K7 numpadens End: hållen som KP_End och — som en widget ser den — "
          "som End");
    w.upp(K::KP_End);
    check(!w.isKeyHeld(K::KP_End) && !w.isKeyHeld(K::End),
          "K7 …och släppt som båda");
    w.ned(K::End);
    check(w.isKeyHeld(K::End) && !w.isKeyHeld(K::KP_End),
          "K7 den dedikerade End hålls bara som End — KP_End är exakt");
    w.upp(K::End);
    w.ned(K::KP_N1);
    check(w.isKeyHeld(K::KP_N1) && !w.isKeyHeld(K::End) &&
              !w.isKeyHeld(K::N1),
          "K7 numpadens SIFFRA viks inte — varken End eller siffer-radens 1");
    w.upp(K::KP_N1);

    // K8 — numpadens två lägen är EN fysisk tangent: ett tryck som börjar i
    // navigationsläget och släpps som siffra (Windows Shift + NumLock) —
    // och tvärtom.
    w.ned(K::KP_Up);
    w.upp(K::KP_N8);
    check(!w.isKeyHeld(K::KP_Up) && !w.isKeyHeld(K::Up),
          "K8 KP_Up ned, KP_N8 upp ⇒ släppt (samma fysiska tangent)");
    w.ned(K::KP_N8);
    w.upp(K::KP_Up);
    check(!w.isKeyHeld(K::KP_N8), "K8 KP_N8 ned, KP_Up upp ⇒ släppt");
    w.ned(K::KP_Up);
    w.upp(K::Up);
    check(w.isKeyHeld(K::KP_Up),
          "K8 den DEDIKERADE pilens släpp rör inte numpadens — olika tangenter");
    w.upp(K::KP_Up);

    // K9 — en post per `Key`: ett släpp av den andra ändrar inte den första.
    w.ned(K::Q);
    w.upp(K::E);
    check(w.isKeyHeld(K::Q), "K9 ett släpp av en annan tangent rör inte Q");
    w.upp(K::Q);

    // K10 — FOKUSFÖRLUSTEN tömmer, och appens krok ser redan den tomma.
    w.ned(K::W);
    w.ned(K::A);
    w.ned(K::KP_End);
    w.krokVHölls = true;
    w.enkel(gfx::EventType::WindowFocusLost);
    check(!w.isKeyHeld(K::W) && !w.isKeyHeld(K::A) &&
              !w.isKeyHeld(K::KP_End) && !w.isKeyHeld(K::End),
          "K10 WindowFocusLost tömmer mängden");
    check(!w.krokVHölls,
          "K10 …redan när appens krok får WindowFocusLost");
    w.enkel(gfx::EventType::WindowFocusGained);

    // K11 — den FOKUSERADE widgeten får den VIKTA tangenten (End för
    // numpadens End), och `isKeyHeld` svarar ja på just den i dess keyDown —
    // ritytans fall: den minns tangenten appens onKeyDown tog.
    w.fångare.focus();
    w.ned(K::KP_End);
    check(w.fångare.tangenter >= 1 && w.fångare.senast == K::End &&
              w.fångare.hölls,
          "K11 en widget fick End (vikt ur KP_End) och isKeyHeld(End) var "
          "sann i dess keyDown");
    w.upp(K::KP_End);
    check(!w.isKeyHeld(K::End),
          "K11 …och släppet gör den osann för widgetens tangent");
}

void fallBudgetFönster(Vard& w)
{
    glaze::App& app = glaze::app();
    check(app.canvasUploadBudget() == 16 * kMiB,
          "W1 förvalet är 16 MiB per frame", tal(app.canvasUploadBudget()));

    app.setCanvasUploadBudget(0);
    const long long negativ = -1;
    app.setCanvasUploadBudget(static_cast<std::size_t>(negativ));
    check(app.canvasUploadBudget() == 16 * kMiB,
          "W2 0 och ett negativt tal avvisas — budgeten står kvar",
          tal(app.canvasUploadBudget()));

    // W3/W4 — 512 KiB genom fönstrets RIKTIGA väg: två plattor i första
    // framen, resten i nästa (som fönstret begär själv), därefter ingenting.
    app.setCanvasUploadBudget(512 * kKiB);
    check(app.canvasUploadBudget() == 512 * kKiB, "W3 sättaren tar 512 KiB");
    w.window().renderFrame();   // töm all damage
    const auto fyra = plattor(4);
    w.bildyta.bilder    = &fyra;
    w.bildyta.ritningar = 0;
    w.bildyta.invalidate();
    w.window().renderFrame();
    check(w.bildyta.ritningar == 1 && w.bildyta.uppe == 2 &&
              w.bildyta.väntade,
          "W4 fönstrets frame laddar upp två plattor om 256 KiB — två väntar",
          std::to_string(w.bildyta.ritningar) + " ritningar, " +
              std::to_string(w.bildyta.uppe) + " uppe");
    w.bildyta.ritningar = 0;
    w.window().renderFrame();   // ingen invalidering från grinden
    check(w.bildyta.ritningar >= 1 && w.bildyta.uppe == 4 &&
              !w.bildyta.väntade,
          "W4 nästa frame — som fönstret begärde — tar resten",
          std::to_string(w.bildyta.ritningar) + " ritningar, " +
              std::to_string(w.bildyta.uppe) + " uppe");
    w.bildyta.ritningar = 0;
    w.window().renderFrame();
    check(w.bildyta.ritningar == 0,
          "W4 sedan ritas ingenting mer (regel 1)",
          std::to_string(w.bildyta.ritningar));

    // W5 — en ÄNDRAD budget gäller i fönstrets nästa frame.
    app.setCanvasUploadBudget(2 * kMiB);
    const auto åtta = plattor(8);
    w.bildyta.bilder = &åtta;
    w.bildyta.invalidate();
    w.window().renderFrame();
    check(w.bildyta.uppe == 8 && !w.bildyta.väntade,
          "W5 budgeten 2 MiB: åtta plattor i EN frame",
          std::to_string(w.bildyta.uppe) + " uppe");

    w.bildyta.bilder = nullptr;
    app.setCanvasUploadBudget(16 * kMiB);   // förvalet tillbaka
}

// R — ett program i alla tre språken: läget genom glaze.viewProj och vit
// färg (scene3d_pixel_gate:s kFärg utan färgattributet).
gfx::ProgramDesc programR()
{
    gfx::ProgramDesc d;
    d.name = "plumbing_gate.R";
    d.hlsl = R"hlsl(
struct VsIn  { float3 pos : ATTR0; };
struct VsOut { float4 pos : SV_Position; };
VsOut vsMain(VsIn i)
{
    VsOut o;
    o.pos = glaze_toClip(mul(glaze.viewProj, float4(i.pos, 1.0)));
    return o;
}
float4 fsMain(VsOut i) : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)hlsl";
    d.glslVertex = R"glsl(
layout(location = 0) in vec3 aPos;
void main() { gl_Position = glaze_toClip(glaze.viewProj * vec4(aPos, 1.0)); }
)glsl";
    d.glslFragment = R"glsl(
out vec4 frag;
void main() { frag = vec4(1.0); }
)glsl";
    d.msl = R"msl(
struct VsIn  { float3 pos [[attribute(0)]]; };
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos = glaze_toClip(glaze.viewProj * float4(i.pos, 1.0), glaze);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]]) { return float4(1.0); }
)msl";
    return d;
}

void fallRenderaren(Vard& w)
{
    gfx::Renderer&         r    = w.window().renderer();
    const gfx::RenderStats före = r.stats();
    {
        gfx::TargetDesc djup, msaa;
        djup.depth   = true;
        msaa.samples = r.supportedSamples(4);
        const auto a = r.createRenderTarget(16, 16);
        const auto b = r.createRenderTarget(16, 16, djup);
        const auto c = r.createRenderTarget(16, 16, msaa);
        check(a->image().valid() && b->image().valid() && c->image().valid() &&
                  r.stats().targetsCreated == före.targetsCreated + 3,
              "R1 tre createRenderTarget ger targetsCreated +3",
              tal(r.stats().targetsCreated - före.targetsCreated));
    }
    const std::uint64_t c0 = r.stats().compileNs;
    const auto          p1 = r.createProgram(programR());
    const std::uint64_t c1 = r.stats().compileNs;
    check(p1->valid() && c1 > c0, "R2 ett nytt program ger compileNs > 0",
          p1->valid() ? tal(c1 - c0) + " ns" : p1->error());
#if defined(GLAZE_BACKEND_D3D11)
    const auto p2 = r.createProgram(programR());
    check(p2->valid() && r.stats().compileNs == c1,
          "R3 D3D11: samma källa en gång till ger compileNs +0 (processens "
          "cache)",
          tal(r.stats().compileNs - c1) + " ns");
#endif
}

void fallMedFönster()
{
    Vard w;
    w.setOffscreen(true);

    // `run()` ÄR öppnaren — aldrig show() först (som i dropdown_gate).
    bool                       kordes = false;
    std::vector<glaze::Bitmap> släppt;   // PX10
    glaze::app().post([&] {
        kordes = true;
        fallStämpel(w);
        fallTangenter(w);
        fallBudgetFönster(w);
        fallRenderaren(w);

        // PX10 — en bild med keepPixels(false) ritas i fönstrets frame; dess
        // pixlar släpps först i PUMPENS svans efter ritloopen, så kontrollen
        // ligger i nästa varv (en postad funktion körs först där).
        släppt.emplace_back(kSida, kSida);
        släppt[0].setKeepPixels(false);
        w.bildyta.bilder = &släppt;
        w.bildyta.invalidate();
        w.window().renderFrame();
        const bool iRamen = släppt[0].pixels() != nullptr && w.bildyta.uppe == 1;
        glaze::app().post([&w, &släppt, iRamen] {
            const bool borta = släppt[0].pixels() == nullptr && släppt[0].valid();
            w.bildyta.invalidate();
            w.window().renderFrame();
            const bool ritar = w.bildyta.uppe == 1;
            // Ett fönster öppnat EFTERÅT är en ny bildcache mot samma renderare.
            glaze::ImageCache ny(w.window().renderer());
            ny.trim(~std::size_t{0}, 16 * kMiB);
            w.window().renderer().resetStats();
            const bool ingen = ny.get(släppt[0]) == nullptr &&
                               w.window().renderer().stats().bytesUploaded == 0;
            check(iRamen && borta && ritar && ingen,
                  "PX10 med fönstret: pixlarna finns i ramen och är släppta "
                  "efter pumpvarvet; fönstret ritar vidare, en ny bildcache "
                  "får ingen textur och laddar inte upp",
                  std::string(iRamen ? "" : "inte i ramen ") +
                      (borta ? "" : "inte släppta ") + (ritar ? "" : "ritar inte ") +
                      (ingen ? "" : "ny cache laddade upp"));
            w.bildyta.bilder = nullptr;

            // K12 — hide() tömmer; och stängningen (kontrolleras efter run()).
            w.ned(gfx::Key::W);
            w.hide();
            check(!w.isKeyHeld(gfx::Key::W), "K12 hide() tömmer mängden");
            w.ned(gfx::Key::Q);   // ingången matas direkt — också dold
            check(w.isKeyHeld(gfx::Key::Q),
                  "K12 (fixturen) Q hålls före stängningen");
            w.close();
        });
    });
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
        return;
    }
    check(!w.isOpen() && !w.isKeyHeld(gfx::Key::Q),
          "K12 stängningen tömmer mängden");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("plumbing_gate\n");
    fallStämpelFristående();
    fallBudgetHeadless();
    fallPixlarHeadless();
    fallMedFönster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
