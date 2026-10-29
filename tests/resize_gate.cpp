// resize_gate — STORLEKSHÄNDELSEN `Widget::onResize` genom
// den RIKTIGA ritningen och pumpens svans.
//
// Kör:  ./build/tests/resize_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = inget fönster kunde
//       skapas (ingen display) — ctest räknar det som SKIPPED, aldrig grönt
//
// Ett OFFSCREEN-fönster med två widgets; stegen är en KEDJA av
// `glaze::app().post` i `w.run()`, och mellan två steg går pumpen några varv
// (`kVarv`), så svansen hinner leverera och en kaskad hinner sätta sig.
// `renderFrame()` driver bildrutorna där ett steg vill ha en. Förväntat värde är
// handräknat ur de satta fälten (logiska px), aldrig ur `resolve`. På Linux
// väntar ett steg dessutom in X11:s asynkrona svar på en storleksändring
// (`satt`, Z8).
//   Z1  den första leveransen — INTE under `renderFrame()` (räknaren orörd
//       direkt efter), en gång i svansen, `first`, `previous` {0, 0} och
//       `actualSize()` 120 × 40
//   Z2  oförändrat: tre ritningar och svansar ⇒ inga fler
//   Z3  `width = 200` + `invalidate()` ⇒ en händelse, `previous` 120 × 40
//   Z4  två ritningar (230, sedan 260) före svansen ⇒ EN händelse
//   Z5  260 → 300 → 260 före svansen ⇒ ingen
//   Z6  dold: ändrad medan dold ⇒ inget; visad i ny storlek ⇒ en; dold och
//       visad i samma ⇒ ingen
//   Z8  `root().onResize` vid `setContentSize`
//   Z9  handlern lossar sin widget; den andra köade får ändå sin
//   Z10 kaskad: a:s handler sätter b:s bredd ⇒ b i ett senare varv, var och
//       en en gång
//   Z11 KEDJESPÄRREN: en handler som gör `width += 1; invalidate();` ⇒
//       exakt 64 leveranser i en händelselös följd (grinden låter pumpen gå
//       200 varv), sedan inga; ett syntetiskt `MouseMotion` genom
//       `handleEvent` ⇒ leveranserna går igen
//   Z14 handlern stänger fönstret ⇒ resten av omgången levereras inte
//   Z13 (D3D11) ett riktigt fönster: den första `onResize` före `onShow` och
//       före den första ritningen
//
// SABOTAGEN som ska fälla den (prövas ett i taget mot ett rent original):
//   * handlern körs i `noteSize` (Z1 — körd under `renderFrame()`)
//   * ingen sammanfogning — varje ritning köar en egen post (Z4: två)
//   * jämförelsen mot framens storlek i stället för den levererade (Z5)
//   * `resizeSeen_` nollas inte vid öppning (Z13 utan `first`)
//   * spärren borttagen (Z11 — 200 leveranser)
//   * `eventSeq_` stegas inte i `handleEvent` (Z11 återupptas inte)
//   * roten noteras inte i `renderContent` (Z8)
//   * den första leveransen i `revealHidden` borttagen (Z13 — efter ritningen)
//   * livstecknet/stängningen frågas inte efter en handler (Z14)
//
// Mäter INTE: en inaktiv dockningsflik (Z7), ritytans `toWorld` i handlern
// (Z12), Windows/macOS modala fönsterdrag (OS:ets loop — handen), Metal och
// Linux, previewn.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <glaze/glaze.h>

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad)
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s\n", vad.c_str());
    ++g_fel;
}

bool lika(gfx::Sizef a, float w, float h)
{
    const auto nära = [](float x, float y) {
        return (x > y ? x - y : y - x) < 0.01f;
    };
    return nära(a.w, w) && nära(a.h, h);
}

struct Rad {
    gfx::Sizef storlek;
    gfx::Sizef previous;
    bool       first;
};

struct Ruta final : glaze::Widget {
    int  ritningar = 0;
    void draw(const glaze::Ctx&) override { ++ritningar; }
};

struct Fonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Ruta             a, b;
    std::vector<Rad> aRader, bRader, rotRader;
    // Lägena handlarna kör i — satta av stegen.
    bool lossaA  = false;   // Z9
    bool kaskad  = false;   // Z10
    bool vaxer   = false;   // Z11
    bool stangsB = false;   // Z14

    Fonster() : glaze::AppWindow("resize_gate", 400, 300, gfx::Chrome::None)
    {
        a.name    = "a";
        a.x       = 10;
        a.y       = 10;
        a.width   = 120;
        a.height  = 40;
        a.anchors = {true, true, false, false};
        b.name    = "b";
        b.x       = 10;
        b.y       = 80;
        b.width   = 50;
        b.height  = 20;
        b.anchors = {true, true, false, false};
        root().adopt(a);
        root().adopt(b);
        a.onResize = [this](glaze::Widget* s, glaze::ResizeArgs& e) {
            aRader.push_back({s->actualSize(), e.previous, e.first});
            if (lossaA)
                root().unadopt(a);
            if (kaskad) {
                b.width += 10;
                b.invalidate();
            }
            if (vaxer) {
                a.width += 1;
                a.invalidate();
            }
        };
        b.onResize = [this](glaze::Widget* s, glaze::ResizeArgs& e) {
            bRader.push_back({s->actualSize(), e.previous, e.first});
            if (stangsB)
                close();
        };
        root().onResize = [this](glaze::Widget* s, glaze::ResizeArgs& e) {
            rotRader.push_back({s->actualSize(), e.previous, e.first});
        };
    }
};

// STEGEN — varje steg körs i postkön, och mellan två steg går pumpen `kVarv`
// varv (ett tomt postat jobb per varv), så svansens leveranser och en
// kaskads ritningar hinner köras. Ett steg som vill vänta längre ber om fler
// varv (`vänta`).
constexpr int kVarv = 4;

// X11 SVARAR ASYNKRONT på en storleksändring (`XResizeWindow` — ytan får sin
// storlek först med ConfigureNotify, ett serverbesök senare), så på Linux
// väntar nästa steg dessutom tills roten står i den klientstorlek fönstret
// begärt (`satt`) — högst 2 s, sedan mäter stegen det som blev — och ger
// svaret `kVarv` varv till att ritas och levereras. Ramlöst: roten ÄR
// klientytan.
#ifdef GLAZE_BACKEND_LINUX
bool satt(glaze::AppWindow& w)
{
    if (!w.isOpen())
        return true;
    const gfx::Sizef a = w.root().actualSize();
    const gfx::Sizef k = w.clientSize();
    const auto nära = [](float x, float y) {
        return (x > y ? x - y : y - x) <= 0.5f;
    };
    return nära(a.w, k.w) && nära(a.h, k.h);
}
#endif

struct Kedja {
    std::vector<std::function<void()>> steg;
    std::size_t                        i     = 0;
    int                                kvar  = 0;
    int                                vänta = 0;
    glaze::AppWindow*                     fönster = nullptr;   // `satt`
    std::chrono::steady_clock::time_point gräns{};
    bool                                  väntade = false;

    void kör()
    {
        if (kvar > 0) {
            --kvar;
            glaze::app().post([this] { kör(); });
            return;
        }
        // Efter det sista steget rörs fönstret inte (se `fallOffscreen`).
        if (i >= steg.size())
            return;
#ifdef GLAZE_BACKEND_LINUX
        if (fönster && !satt(*fönster) &&
            std::chrono::steady_clock::now() < gräns) {
            väntade = true;
            glaze::app().post([this] { kör(); });
            return;
        }
        if (väntade) {
            väntade = false;
            kvar    = kVarv;
            glaze::app().post([this] { kör(); });
            return;
        }
#endif
        vänta = kVarv;
        steg[i++]();
        kvar  = vänta;
        gräns = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        glaze::app().post([this] { kör(); });
    }
};

// En RIKTIG händelse genom fönstrets ingång — kedjespärrens räkneverk.
void rörelse(Fonster& w)
{
    gfx::Event m;
    m.type   = gfx::EventType::MouseMotion;
    m.mouseX = 390.0f;
    m.mouseY = 290.0f;
    w.handleEvent(m);
}

void fallOffscreen()
{
    Fonster w;
    w.setOffscreen(true);
    bool kordes = false;
    // STATISK: kedjans tomma varv kan ligga kvar i postkön när fönstret
    // stängts och funktionen lämnats — `kör` rör bara kedjan, aldrig `w`.
    static Kedja k;
    std::size_t  före = 0;

    k.steg = {
        // Z1 — den första: inte under ritningen, en gång i svansen
        [&] {
            kordes = true;
            const std::size_t n = w.aRader.size();
            w.window().renderFrame();
            check(w.aRader.size() == n,
                  "Z1 handlern körs inte under renderFrame()");
        },
        [&] {
            check(w.aRader.size() == 1, "Z1 en leverans efter svansen");
            if (!w.aRader.empty()) {
                check(w.aRader[0].first, "Z1 first");
                check(lika(w.aRader[0].previous, 0, 0), "Z1 previous {0, 0}");
                check(lika(w.aRader[0].storlek, 120, 40),
                      "Z1 actualSize() 120 × 40");
            }
            före = w.aRader.size();
            w.window().renderFrame();
        },
        // Z2 — oförändrat: tre ritningar och svansar
        [&] { w.window().renderFrame(); },
        [&] { w.window().renderFrame(); },
        [&] {
            check(w.aRader.size() == före, "Z2 oförändrat ger ingen händelse");
            // Z3
            w.a.width = 200;
            w.a.invalidate();
        },
        [&] {
            check(w.aRader.size() == före + 1, "Z3 en händelse vid ny bredd");
            if (w.aRader.size() == före + 1) {
                check(lika(w.aRader.back().previous, 120, 40),
                      "Z3 previous 120 × 40");
                check(!w.aRader.back().first, "Z3 inte first");
            }
            // Z4 — två ritningar före svansen
            w.a.width = 230;
            w.a.invalidate();
            w.window().renderFrame();
            w.a.width = 260;
            w.a.invalidate();
            w.window().renderFrame();
            före = w.aRader.size();
        },
        [&] {
            check(w.aRader.size() == före + 1, "Z4 två ritningar ger EN händelse");
            if (!w.aRader.empty()) {
                check(lika(w.aRader.back().storlek, 260, 40), "Z4 den senaste");
                check(lika(w.aRader.back().previous, 200, 40),
                      "Z4 previous är den förra leveransen");
            }
            // Z5 — 260 → 300 → 260 före svansen
            före      = w.aRader.size();
            w.a.width = 300;
            w.a.invalidate();
            w.window().renderFrame();
            w.a.width = 260;
            w.a.invalidate();
            w.window().renderFrame();
        },
        [&] {
            check(w.aRader.size() == före, "Z5 A → B → A ger ingen händelse");
            // Z6 — dold
            w.a.hide();
            w.a.width = 280;
            w.invalidate();
        },
        [&] {
            check(w.aRader.size() == före, "Z6 en dold widget får ingen händelse");
            w.a.show();
        },
        [&] {
            check(w.aRader.size() == före + 1, "Z6 visad i ny storlek ger en");
            före = w.aRader.size();
            w.a.hide();
        },
        [&] { w.a.show(); },
        [&] {
            check(w.aRader.size() == före, "Z6 visad i samma storlek ger ingen");
            // Z8 — roten
            före = w.rotRader.size();
            w.setContentSize(420, 320);
            w.invalidate();
        },
        [&] {
            check(w.rotRader.size() == före + 1,
                  "Z8 root().onResize vid setContentSize");
            if (!w.rotRader.empty())
                check(lika(w.rotRader.back().storlek, 420, 320),
                      "Z8 rotens storlek är innehållsytan");
            // Z9 — a:s handler lossar a, b ändras i samma ritning
            w.lossaA  = true;
            w.a.width = 150;
            w.b.width = 60;
            w.invalidate();
            före = w.bRader.size();
        },
        [&] {
            check(w.a.parent() == nullptr, "Z9 (fixturen) a lossades");
            check(w.bRader.size() == före + 1,
                  "Z9 den andra köade widgeten får ändå sin händelse");
            w.lossaA = false;
            w.root().adopt(w.a);
        },
        // Z10 — kaskad: a:s handler sätter b:s bredd
        [&] {
            w.kaskad = true;
            före     = w.bRader.size();
            w.a.width += 5;
            w.a.invalidate();
        },
        [&] {
            check(w.bRader.size() == före + 1,
                  "Z10 b får sin händelse i ett senare varv, en gång");
            w.kaskad = false;
            // Z11 — den växande handlern, 200 varv. Kedjan börjar om med en
            // riktig händelse (de tidigare fallens leveranser räknas annars).
            rörelse(w);
            före     = w.aRader.size();
            w.vaxer  = true;
            w.a.width += 1;
            w.a.invalidate();
            k.vänta = 200;
        },
        [&] {
            check(w.aRader.size() - före == 64,
                  "Z11 spärren: exakt 64 leveranser i en händelselös följd (fick " +
                      std::to_string(w.aRader.size() - före) + ")");
            // …och en riktig händelse släpper den
            före = w.aRader.size();
            rörelse(w);
            w.invalidate();
            k.vänta = 10;
        },
        [&] {
            check(w.aRader.size() > före,
                  "Z11 en händelse genom handleEvent släpper spärren");
            w.vaxer = false;
        },
        // Z14 — b:s handler stänger fönstret, a i samma omgång. b ritas
        // först sedan Z9 (a adopterades tillbaka sist), så a står efter b i
        // kön.
        [&] {
            w.stangsB = true;
            före      = w.aRader.size();
            w.a.width += 3;
            w.b.width += 3;
            w.invalidate();
        },
    };
    k.fönster = &w;
    glaze::app().post([] { k.kör(); });
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        std::exit(77);
    }
    check(!w.isOpen(), "Z14 (fixturen) handlern stängde fönstret");
    check(w.aRader.size() == före,
          "Z14 resten av omgången levereras inte efter en stängning");
}

#ifdef GLAZE_BACKEND_D3D11
// Z13 — ett RIKTIGT fönster (inte offscreen): den första leveransen kommer
// före den första synliga bildrutan (`revealHidden`), alltså före `onShow`
// och innan widgeten ritats.
void fallForstaVisningen()
{
    struct F : glaze::AppWindow {
        Ruta                     r;
        std::vector<std::string> logg;
        int                      ritadeVidResize = -1;
        bool                     first           = false;
        F() : glaze::AppWindow("resize_gate Z13", 300, 200, gfx::Chrome::None)
        {
            r.name    = "r";
            r.x       = 5;
            r.y       = 5;
            r.width   = 70;
            r.height  = 30;
            r.anchors = {true, true, false, false};
            root().adopt(r);
            r.onResize = [this](glaze::Widget*, glaze::ResizeArgs& e) {
                logg.push_back("resize");
                if (ritadeVidResize < 0)
                    ritadeVidResize = r.ritningar;
                first = first || e.first;
            };
            onShow = [this] {
                logg.push_back("show");
                close();
            };
        }
    };
    F f;
    f.run();
    check(!f.logg.empty() && f.logg.front() == "resize",
          "Z13 den första onResize före onShow");
    check(f.ritadeVidResize == 0, "Z13 …och före den första ritningen");
    check(f.first, "Z13 first i ett nytt liv");
}
#endif

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("resize_gate — onResize: upptäckten i ritningen, leveransen i "
                "svansen\n");
    fallOffscreen();
#ifdef GLAZE_BACKEND_D3D11
    fallForstaVisningen();
#endif
    std::printf("\n%d fall, %d fel\n", g_fall, g_fel);
    return g_fel == 0 ? 0 : 1;
}
