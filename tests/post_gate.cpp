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

    std::printf("\npost_gate: %s (%d fel)\n", g_fails ? "FÄLLD" : "GRÖN",
                g_fails);
    return g_fails ? 1 : 0;
}
