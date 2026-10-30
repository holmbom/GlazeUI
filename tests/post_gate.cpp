// post_gate — TRÅDSÄKER POST TILL UI-TRÅDEN.
//
// Kör:  ./build/tests/post_gate
// Exit: 0 = grönt · 1 = ett fall föll · ctest-timeouten fångar hängningen
//
// Grinden mäter exakt den bugg ett första utkast föll på: ett rent
// OS-wakeup RÄCKER INTE. `waitEvent` loopar
// `for(;;) { if (popQueued(e)) return true; <blockera>; }` i alla tre
// backends, så ett meddelande som bara dispatchas skickar loopen rakt
// tillbaka i blockering — pumpen når aldrig sin svans och postkön dräneras
// aldrig. Varje backend måste alltså BÅDE väcka OS-pumpen OCH köa ett
// `gfx::Event{EventType::Wakeup}`.
//
// Testet: ett DOLT fönster körs med `run()` (pumpen sover i `waitEvent`),
// och en BAKGRUNDSTRÅD postar ett `close()`. Fungerar väckningen
// returnerar `run()`; fungerar den inte HÄNGER testet — och det är därför
// ctest-timeouten är en del av grinden och inte en formalitet.
//
// DEL 3 — POSTKÖNS BUDGET (`App::setPostBudget`). Ett nytt
// dolt fönster genom `run()`; funktionerna ARBETAR i 2 ms var (en aktiv
// väntan — en sömn avrundas till systemets timerupplösning). Varvet en
// funktion körs i läses ur profilerarens pumpring: ett steg tömmer den, och
// ett varv läggs dit när det är klart, så under k:e varvet efter steget
// står k poster där.
//   B1 förvalet är 4 ms, och ett negativt tal ändrar ingenting
//   B2 tio funktioner om 2 ms med budgeten 4: det första varvet kör 1–3,
//      alla tio körs inom 6 varv i ordningen 0–9, och en funktion som
//      postas inifrån nummer 0 körs efter nummer 9 (resten läggs FÖRST i
//      kön igen)
//   B3 budgeten 0 kör alla tio i ett varv
//   B4 RAMKÖN: en tick (`detail::postFrameWork`) postad BAKOM tio långa
//      funktioner körs i det FÖRSTA varvet, före dem
// Hänger pumpen med rest i kön (somnar trots `hasPosted`) fångar
// ctest-timeouten det.
//
// SABOTAGEN som ska fälla del 3: resten
// tappas (B2 — färre än elva körda), resten läggs SIST i kön (B2 —
// nummer 0:s funktion före 2–9), budgeten ignoreras (B2 — tio i första
// varvet), budgeten 0 tolkas som "ingen tid" (B3 — en per varv), och
// `postFrameWork` lägger ticken i appens kö (B4 — sist, i femte varvet).
//
// VAD DEL 3 INTE MÄTER: en enskild funktion som är längre än budgeten (den
// kan inte delas — profilerarens `postedMax`); trådarnas poster medan
// resten läggs tillbaka (låset och ordningen prövas bara av en tråd);
// ordningen mot en nästlad pump (en modal i en postad funktion — som före
// budgeten); att ritytans tick och kamerans händelse faktiskt går i ramkön
// (canvas3d_gate T och canvas_gate kör dem genom pumpen). B2:s "inom 6 varv"
// tål ett varv med bara en funktion; en maskin så belastad att två varv
// tappar 2 ms fäller den.
//
// DEL 4 — EXTERNA BILDKÄLLOR (`detail::registerExternalFrameSource`):
//   E1 ett källjobb körs i svansen FÖRE ramkön och appens kö (postade i
//      samma varv: källan, ramen, appen → körs i den ordningen); högst ETT
//      väntar per källa (det andra `postExternalFrame` svarar false), och
//      platsen är fri igen när jobbet börjat köra
//   E2 `postExternalFrame` från en ANNAN tråd väcker en sovande pump, och
//      jobbet körs i UI-tråden
//   E3 en avregistrering stryker det väntande jobbet — också när ett jobb
//      före avregistrerar källan i samma varv — och en avregistrerad eller
//      okänd källa tar inte emot fler
//   E4 regel 1: en pump med en källa utan jobb, och en pump efter sista
//      källan, sover (inga varv medan inget händer)
//   E5 skrivbordet stryps till `desktopHz` medan en källa finns — pumpen
//      snurrar inte på en väntande ritning — och takten är full igen när
//      sista källan avregistrerats
//   E6 en ändring som väntar ritas vid nästa tillåtna tillfälle (efter
//      perioden), inte genast och inte aldrig
//   E7 platsen frigörs INNAN jobbet körs: en post från en annan tråd medan
//      jobbet kör tas emot och körs därefter
//
// SABOTAGEN som ska fälla del 4: källjobbet körs efter ramkön (E1),
// `postExternalFrame` köar ett andra jobb (E1), jobbet körs trots
// avregistrering (E3), ingen väckning vid post (E2 hänger), pumpen sover
// aldrig/snurrar medan en ritning väntar (E5/E6 — varven), strypningen
// ignoreras (E5 — för många bilder) och takten återställs aldrig (E5 — för
// få bilder efter sista källan).
//
// Saknas display (headless-CI utan surfacelös EGL) hoppas fallet över med
// en skriven rad, precis som window_gate gör.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <glaze/glaze.h>

namespace {

int g_fails = 0;

void check(bool ok, const char* vad)
{
    std::printf("%-6s %s\n", ok ? "OK" : "FEL", vad);
    if (!ok)
        ++g_fails;
}

// Med det grinden fick, när fallet föll.
void check(bool ok, const char* vad, const std::string& fick)
{
    check(ok, vad);
    if (!ok)
        std::printf("       fick %s\n", fick.c_str());
}

class DoltFönster : public glaze::AppWindow {
public:
    DoltFönster() : glaze::AppWindow("post_gate", 200, 120) {}
};

// ---------------------------------------------- DEL 3: POSTKÖNS BUDGET (D2)

constexpr int  kFunktioner = 10;
constexpr int  kInifrån    = 100;   // postad inifrån nummer 0 (B2)
constexpr int  kTick       = 200;   // ramköns post (B4)
constexpr int  kBudget     = 4;     // ms — förvalet
constexpr auto kArbete     = std::chrono::milliseconds(2);

// En funktion som ARBETAR — en aktiv väntan mot klockan. En sömn avrundas
// till systemets timerupplösning (15,6 ms på Windows) och hade gjort varven
// omätbara.
void arbeta()
{
    const auto slut = std::chrono::steady_clock::now() + kArbete;
    while (std::chrono::steady_clock::now() < slut) {
    }
}

// Varvet just nu, räknat från steget som senast tömde profileraren: ett
// pumpvarv läggs i ringen när det är KLART, så under k:e varvet efter steget
// står k poster där.
std::size_t varvNu()
{
    return glaze::profiler().pump().size();
}

struct Körning {
    int         nr;
    std::size_t varv;
};

// "nr@varv …" — det grinden fick.
std::string text(const std::vector<Körning>& v)
{
    std::string s;
    for (const Körning& k : v)
        s += std::to_string(k.nr) + "@" + std::to_string(k.varv) + " ";
    return s;
}

// STEGEN, en kedja genom postkön i ett dolt fönster: varje steg postas av
// det sista som ska mätas, prövar, och startar nästa fall.
struct Budgetprov {
    DoltFönster&         w;
    std::vector<Körning> körda;
    int                  facit = 0;   // stegen som hann köras

    void kör(int nr) { körda.push_back({nr, varvNu()}); }

    // Loggen och profileraren töms — varven räknas härifrån.
    void nollställ()
    {
        körda.clear();
        glaze::profiler().clear();
    }

    // Tio funktioner om 2 ms; nummer 0 postar `inifrån` och den sista
    // `sedan`, när de finns.
    void tio(std::function<void()> sedan, std::function<void()> inifrån = {})
    {
        for (int i = 0; i < kFunktioner; ++i)
            glaze::app().post([this, i, sedan, inifrån] {
                arbeta();
                kör(i);
                if (i == 0 && inifrån)
                    glaze::app().post(inifrån);
                if (i == kFunktioner - 1 && sedan)
                    glaze::app().post(sedan);
            });
    }

    // B2 — budgeten 4 ms. Funktionen som nummer 0 postar körs SIST, efter
    // nummer 9, och postar det förväntade värdet.
    void b2()
    {
        nollställ();
        tio({}, [this] {
            kör(kInifrån);
            glaze::app().post([this] { b2Facit(); });
        });
    }

    void b2Facit()
    {
        ++facit;
        std::vector<int> ordning, väntad;
        std::size_t      iFörsta = 0, sista = 0;
        for (const Körning& k : körda) {
            ordning.push_back(k.nr);
            if (k.nr >= kFunktioner)
                continue;
            if (k.varv == 1)
                ++iFörsta;
            sista = std::max(sista, k.varv);
        }
        for (int i = 0; i < kFunktioner; ++i)
            väntad.push_back(i);
        väntad.push_back(kInifrån);
        check(ordning == väntad,
              "B2 resten läggs FÖRST i kön igen: ordningen 0–9, och den som "
              "nummer 0 postade efter nummer 9",
              text(körda));
        check(!körda.empty() && körda.front().varv == 1 && iFörsta >= 1 &&
                  iFörsta <= 3,
              "B2 budgeten 4 ms: det första varvet kör 1–3 av tio funktioner "
              "om 2 ms",
              text(körda));
        check(sista >= 1 && sista <= 6, "B2 alla tio körs inom 6 varv",
              text(körda));

        // B3 — budgeten 0 är obegränsad.
        glaze::app().setPostBudget(0);
        nollställ();
        tio([this] { b3Facit(); });
    }

    void b3Facit()
    {
        ++facit;
        bool ettVarv = körda.size() == std::size_t(kFunktioner);
        for (const Körning& k : körda)
            ettVarv = ettVarv && k.varv == 1;
        check(ettVarv, "B3 budgeten 0 kör alla tio i ett varv", text(körda));

        // B4 — ramköns tick postad BAKOM tio långa funktioner.
        glaze::app().setPostBudget(kBudget);
        nollställ();
        tio([this] { b4Facit(); });
        glaze::detail::postFrameWork([this] { kör(kTick); });
    }

    void b4Facit()
    {
        ++facit;
        check(!körda.empty() && körda.front().nr == kTick &&
                  körda.front().varv == 1,
              "B4 ramköns tick bakom tio långa funktioner körs i det FÖRSTA "
              "varvet, före dem",
              text(körda));
        w.close();
    }
};

// ------------------------------------- DEL 4: EXTERNA BILDKÄLLOR (E1–E6)

using Klocka = std::chrono::steady_clock;

double msMellan(Klocka::time_point a, Klocka::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// Ett fönster som räknar sina ritningar (`frame` körs en gång per full
// omritning: `invalidate` och en animering).
class Taktfönster : public glaze::AppWindow {
public:
    Taktfönster() : glaze::AppWindow("post_gate_takt", 200, 120)
    {
        setOffscreen(true);
    }
    int                             bilder = 0;
    std::vector<Klocka::time_point> tider;
    std::function<void()>           påBild;   // efter varje bild, i ritningen

protected:
    void frame(const glaze::Ctx&) override
    {
        ++bilder;
        tider.push_back(Klocka::now());
        // En lokal kopia: callbacken får nollställa `påBild` (E6 gör det)
        // och funktorn ska ändå leva tills anropet återvänt.
        if (const std::function<void()> jobb = påBild)
            jobb();
    }
};

// E1 — ordningen, högst ett jobb, platsen fri när jobbet börjat; sedan E2
// (en annan tråd) i samma fönster.
void externaEtt()
{
    Taktfönster                          w;
    std::vector<int>                     ordning;
    std::thread                          tråd;
    std::atomic<bool>                    frånAnnanTråd{false};
    std::atomic<bool>                    post2{false};
    const std::thread::id                ui = std::this_thread::get_id();
    glaze::detail::ExternalFrameSourceId s  = 0;

    glaze::app().post([&] {
        s = glaze::detail::registerExternalFrameSource(30);
        check(s != 0, "E1 en källa registreras (id != 0)");
        glaze::app().post([&] { ordning.push_back(3); });
        glaze::detail::postFrameWork([&] { ordning.push_back(2); });
        const bool a = glaze::detail::postExternalFrame(
            s, [&] { ordning.push_back(1); });
        const bool b = glaze::detail::postExternalFrame(
            s, [&] { ordning.push_back(9); });
        check(a, "E1 det första jobbet köas");
        check(!b, "E1 högst ETT väntande jobb per källa: det andra nekas");
        glaze::app().post([&] {
            check(ordning == std::vector<int>{1, 2, 3},
                  "E1 källjobbet körs FÖRE ramkön och appens kö (1, 2, 3)",
                  std::to_string(ordning.size()) + " körda");
            // Platsen är fri: jobbet har körts.
            const bool igen = glaze::detail::postExternalFrame(
                s, [&] { ordning.push_back(4); });
            check(igen, "E1 platsen är fri igen när jobbet körts");
            // E2 — en annan tråd postar medan pumpen sover.
            tråd = std::thread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                post2 = glaze::detail::postExternalFrame(s, [&] {
                    frånAnnanTråd = std::this_thread::get_id() != ui;
                    glaze::detail::unregisterExternalFrameSource(s);
                    w.close();
                });
            });
        });
    });
    w.run();   // HÄNGER om en post från en annan tråd inte väcker pumpen
    tråd.join();
    check(post2, "E2 en annan tråd får köa ett jobb");
    check(!frånAnnanTråd, "E2 jobbet kördes i UI-tråden");
    check(ordning == std::vector<int>{1, 2, 3, 4},
          "E2 jobbet från föregående steg kördes också, före tråden",
          std::to_string(ordning.size()) + " körda");
}

// E3 — avregistreringen stryker det väntande jobbet.
void externaTre()
{
    Taktfönster                          w;
    bool                                 kördEtt = false, kördTvå = false,
                                         kördTre = false;
    bool                                 nekadEfter = false, nekadOkänd = false;
    glaze::detail::ExternalFrameSourceId s1 = 0, s2 = 0, s3 = 0;

    glaze::app().post([&] {
        s1 = glaze::detail::registerExternalFrameSource(30);
        s2 = glaze::detail::registerExternalFrameSource(30);
        s3 = glaze::detail::registerExternalFrameSource(30);
        // Källorna körs i id-ordning: s1:s jobb avregistrerar s2 innan s2:s
        // körs (båda väntade när varvet började).
        glaze::detail::postExternalFrame(s1, [&] {
            kördEtt = true;
            glaze::detail::unregisterExternalFrameSource(s2);
        });
        glaze::detail::postExternalFrame(s2, [&] { kördTvå = true; });
        // Ett jobb som avregistreras i samma steg som det köades.
        const bool köad =
            glaze::detail::postExternalFrame(s3, [&] { kördTre = true; });
        glaze::detail::unregisterExternalFrameSource(s3);
        check(köad, "E3 jobbet köades före avregistreringen");
        nekadEfter = !glaze::detail::postExternalFrame(s3, [] {});
        glaze::app().post([&] {
            glaze::detail::unregisterExternalFrameSource(s1);
            nekadOkänd = !glaze::detail::postExternalFrame(s1, [] {});
            // Ytterligare ett varv, så ett felaktigt kvarlämnat jobb hunnit
            // köra.
            glaze::app().post([&] { w.close(); });
        });
    });
    w.run();
    check(kördEtt, "E3 första källans jobb kördes");
    check(!kördTvå,
          "E3 ett jobb vars källa avregistrerats av ett jobb före körs inte");
    check(!kördTre, "E3 ett jobb som väntade vid avregistreringen stryks");
    check(nekadEfter, "E3 en avregistrerad källa tar inte emot jobb");
    check(nekadOkänd,
          "E3 en källa avregistrerad efter sitt jobb tar inte emot jobb");
    check(!glaze::detail::postExternalFrame(0, [] {}),
          "E3 id 0 är aldrig giltigt");
}

// E4 — pumpen sover med en källa utan jobb, och efter sista källan.
void externaFyra()
{
    Taktfönster                          w;
    std::size_t                          medKälla = 99, utanKälla = 99;
    glaze::detail::ExternalFrameSourceId s = 0;
    std::thread                          tråd;

    glaze::app().post([&] {
        s = glaze::detail::registerExternalFrameSource(30);
        glaze::profiler().setEnabled(true);
        glaze::profiler().clear();
        tråd = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            glaze::app().post([&] {
                medKälla = varvNu();
                glaze::detail::unregisterExternalFrameSource(s);
                glaze::profiler().clear();
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            glaze::app().post([&] {
                utanKälla = varvNu();
                w.close();
            });
        });
    });
    w.run();
    tråd.join();
    glaze::profiler().setEnabled(false);
    check(medKälla <= 8,
          "E4 med en källa utan jobb sover pumpen (högst 8 varv under 300 ms)",
          std::to_string(medKälla));
    check(utanKälla <= 8,
          "E4 efter sista källan sover pumpen (högst 8 varv under 300 ms)",
          std::to_string(utanKälla));
}

// E5 — strypningen och den återställda takten.
void externaFem()
{
    constexpr unsigned kHz       = 20;   // en period om 50 ms
    constexpr double   kPeriodMs = 1000.0 / kHz;
    Taktfönster                          w;
    glaze::detail::ExternalFrameSourceId s = 0;
    std::thread                          tråd;
    int                                  bilder0 = 0, bilder1 = 0, bilder2 = 0,
                                         bilder3 = 0;
    Klocka::time_point                   t0, t1, t2, t3;
    std::size_t                          varv = 0;

    glaze::app().post([&] {
        w.setAnimating(true);   // vill ha en bild varje varv
        glaze::profiler().setEnabled(true);
        glaze::profiler().clear();
        bilder0 = w.bilder;
        t0      = Klocka::now();
        s       = glaze::detail::registerExternalFrameSource(kHz);
        tråd    = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            glaze::app().post([&] {
                bilder1 = w.bilder;
                t1      = Klocka::now();
                varv    = varvNu();
                glaze::detail::unregisterExternalFrameSource(s);
                bilder2 = w.bilder;
                t2      = Klocka::now();
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            glaze::app().post([&] {
                bilder3 = w.bilder;
                t3      = Klocka::now();
                w.setAnimating(false);
                w.close();
            });
        });
    });
    w.run();
    tråd.join();
    glaze::profiler().setEnabled(false);

    const int    stryptaBilder = bilder1 - bilder0;
    const double stryptMs      = msMellan(t0, t1);
    const int    tak = static_cast<int>(stryptMs / kPeriodMs) + 2;
    check(stryptaBilder <= tak,
          "E5 skrivbordet ritas högst `desktopHz` gånger per sekund medan en "
          "källa finns",
          std::to_string(stryptaBilder) + " bilder på " +
              std::to_string(stryptMs) + " ms (tak " + std::to_string(tak) + ")");
    check(stryptaBilder >= 3,
          "E5 skrivbordet ritas ändå (en väntande ritning får sin tur)",
          std::to_string(stryptaBilder));
    // Pumpen sover mellan perioderna: några varv per bild, inte en loop.
    check(varv <= static_cast<std::size_t>(stryptaBilder) * 4 + 10,
          "E5 pumpen snurrar inte medan en ritning väntar på sin period",
          std::to_string(varv) + " varv, " + std::to_string(stryptaBilder) +
              " bilder");
    const int fullaBilder = bilder3 - bilder2;
    check(fullaBilder >= 10,
          "E5 efter sista källan är takten full igen (minst 10 bilder på "
          "400 ms)",
          std::to_string(fullaBilder) + " bilder på " +
              std::to_string(msMellan(t2, t3)) + " ms");
}

// E6 — en ändring som väntar ritas efter perioden.
void externaSex()
{
    constexpr unsigned kHz = 5;   // en period om 200 ms
    Taktfönster                          w;
    glaze::detail::ExternalFrameSourceId s = 0;
    std::size_t                          varvVid2 = 0;
    int                                  räknade  = 0;

    glaze::app().post([&] {
        s = glaze::detail::registerExternalFrameSource(kHz);
        glaze::profiler().setEnabled(true);
        w.påBild = [&] {
            ++räknade;
            if (räknade == 1) {
                glaze::profiler().clear();
                // Ändringen kommer strax efter bilden: den ska vänta.
                glaze::app().post([&w] { w.invalidate(); });
            } else if (räknade == 2) {
                varvVid2 = varvNu();
                w.påBild = nullptr;
                glaze::detail::unregisterExternalFrameSource(s);
                w.close();
            }
        };
        w.invalidate();
    });
    w.run();
    glaze::profiler().setEnabled(false);
    check(w.tider.size() >= 2,
          "E6 en andra bild ritades (ändringen väntade, den föll inte bort)");
    if (w.tider.size() >= 2) {
        const double mellan =
            msMellan(w.tider[w.tider.size() - 2], w.tider.back());
        check(mellan >= 150.0 && mellan <= 1500.0,
              "E6 den väntande ändringen ritas efter perioden (150–1500 ms)",
              std::to_string(mellan) + " ms");
    }
    check(varvVid2 <= 12,
          "E6 pumpen sover medan ändringen väntar (högst 12 varv)",
          std::to_string(varvVid2));
}

// E7 — platsen frigörs INNAN jobbet körs: en post från en annan tråd medan
// jobbet kör tas emot och körs därefter, och jobbet får själv posta nästa.
void externaSju()
{
    Taktfönster                          w;
    std::vector<int>                     ordning;
    std::atomic<bool>                    kör{false};
    std::atomic<bool>                    underKörning{false};
    bool                                 inifrån = false;
    std::thread                          tråd;
    glaze::detail::ExternalFrameSourceId s = 0;

    glaze::app().post([&] {
        s = glaze::detail::registerExternalFrameSource(30);
        tråd = std::thread([&] {
            while (!kör)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // Jobbet kör nu (och sover i UI-tråden).
            underKörning = glaze::detail::postExternalFrame(s, [&] {
                ordning.push_back(2);
                inifrån = glaze::detail::postExternalFrame(s, [&] {
                    ordning.push_back(3);
                    glaze::detail::unregisterExternalFrameSource(s);
                    w.close();
                });
            });
        });
        glaze::detail::postExternalFrame(s, [&] {
            ordning.push_back(1);
            kör = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        });
    });
    w.run();
    tråd.join();
    check(underKörning,
          "E7 en post från en annan tråd medan jobbet kör tas emot");
    check(inifrån, "E7 ett jobb får posta nästa bildruta inifrån sig självt");
    check(ordning == std::vector<int>{1, 2, 3},
          "E7 jobben körs därefter, ett per varv, i ordning (1, 2, 3)",
          std::to_string(ordning.size()) + " körda");
}

} // namespace

int main()
{
    std::printf("=== post_gate — glaze::app().post (R17) ===\n");

    // ---- 1. Kön utan pump: funktionen ligger kvar, inget kraschar.
    // Avsiktligt beteende (regel 1 — det finns ingen loop som kör den).
    std::atomic<int> körd{0};
    glaze::app().post([&] { ++körd; });
    check(körd == 0, "post utan pump: körs inte av sig själv");

    // Displaykollen görs på ett EGET fönster: `run()` vägrar på ett redan
    // öppnat fönster ("fönstret är redan öppet"), så probe-fönstret får
    // inte vara det som pumpas.
    {
        DoltFönster probe;
        probe.setOffscreen(true);
        probe.show();
        const bool harDisplay = probe.isOpen();
        probe.close();
        if (!harDisplay) {
            std::printf("  --   pumpfallet hoppas över: inget fönster kunde "
                        "skapas (ingen display)\n");
            std::printf("\npost_gate: %d fel\n", g_fails);
            return g_fails ? 1 : 0;
        }
    }

    DoltFönster w;
    w.setOffscreen(true);

    // ---- 2. En BAKGRUNDSTRÅD postar medan pumpen sover i waitEvent.
    // Sömnen är det som gör fallet skarpt: utan den hade det första
    // pumpvarvet råkat dränera kön ändå.
    std::atomic<bool> tråden_klar{false};
    std::thread       arbetare([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        glaze::app().post([&] { ++körd; });
        // Postad `close()` ska tas i SAMMA pumpvarv — dräneringen ligger
        // FÖRE flushPendingCloses().
        glaze::app().post([&w] { w.close(); });
        tråden_klar = true;
    });

    const int ut = w.run();   // HÄNGER om väckningen inte når pumpsvansen
    arbetare.join();

    check(tråden_klar, "bakgrundstråden hann posta");
    // Två inkrement: den som postades FÖRE pumpen (den låg kvar och kördes
    // i första svansen) och den från bakgrundstråden.
    check(körd == 2, "båda de postade funktionerna kördes i UI-tråden");
    check(!w.isOpen(), "postad close() verkställdes");
    check(ut == 0, "run() returnerade normalt");

    // ---- 3. POSTKÖNS BUDGET.
    check(glaze::app().postBudget() == kBudget, "B1 förvalet är 4 ms");
    glaze::app().setPostBudget(-1);   // loggas
    check(glaze::app().postBudget() == kBudget,
          "B1 ett negativt tal ändrar ingenting");
    {
        DoltFönster b;
        b.setOffscreen(true);
        Budgetprov prov{b, {}};
        // PÅ FÖRE PUMPEN, så att varvet som kör det första steget också
        // läggs i ringen — varven räknas från det.
        glaze::profiler().setEnabled(true);
        glaze::app().post([&prov] { prov.b2(); });
        b.run();   // förväntat värde i B4 stänger fönstret
        glaze::profiler().setEnabled(false);
        check(prov.facit == 3, "B2–B4 alla tre facitstegen kördes",
              std::to_string(prov.facit));
    }

    // ---- 4. EXTERNA BILDKÄLLOR.
    externaEtt();
    externaTre();
    externaFyra();
    externaFem();
    externaSex();
    externaSju();

    std::printf("\npost_gate: %s (%d fel)\n", g_fails ? "FÄLLD" : "GRÖN",
                g_fails);
    return g_fails ? 1 : 0;
}
