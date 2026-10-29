// tests/perf/harness — MÄTSELEN för prestandaprogrammen som kör en APP på
// riktigt: app_perf och terrain_perf delar den, så att
// båda mäter med SAMMA klocka, samma minne, samma energi, samma arbetstrådar
// och samma session — och så att ett tredje program inte börjar med en kopia.
//
// Här står det som inte är en scen:
//   verktygen   klockan, felräkningen, slumpen (eget frö och egen avbildning
//               — scenerna ska vara desamma med libstdc++ och libc++),
//               serierna, minnet och energin per process
//   plattorna   64 flygfotolika PNG om 256 × 256, kodade en gång, avkodade
//               per platta i arbetstrådarna med nätets fördröjning
//   trådarna    `Arbetare` (en FIFO-kö som UI-tråden kan rensa) och
//               `postaTillUi`, som bara kör svaret medan sessionen lever
//   föremålen   gallerstolpen (canvas_perf3d:s, i 1–4 detaljnivåer) och
//               trädets kon (i valfritt antal nivåer)
//   sessionen   skriptade STEG över N MÄTTA FÖNSTER — varje steg drivs en
//               tick i taget av sitt fönsters rityta, eller av en timer när
//               ingenting ska röra sig — med frametiden ur `onTick`,
//               renderarens anrop och uppladdning, strömningen, minnet och
//               energin per steg och fönster, och (`Session::profilera`)
//               PROFILERARENS bilder per steg och fönster, pumpvarven och
//               verktygens etiketter (glaze/profiler.h)
//
// INTE HÄR: scenerna. Vad ett fönster visar, vad som strömmar och vad ett
// steg gör är programmets.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <glaze/glaze.h>

namespace perf {

// ------------------------------------------------------------- verktygen

extern bool        g_verbose;   // -v
extern bool        g_synlig;    // --synlig: riktiga fönster med vsync
extern int         g_fel;       // fel() räknar hit
extern const char* const kBygge;   // "Release" / "Debug"

using Klocka = std::chrono::steady_clock;

double msSedan(Klocka::time_point t0);

inline constexpr double kPi = 3.14159265358979323846;

// Ett FEL i sessionen: skrivs och räknas — exit-koden är programmets.
void fel(const std::string& vad);

// EGET frö och egen avbildning (canvas_perf:s skäl): scenen ska vara
// densamma med libstdc++ och libc++.
struct Slump {
    std::uint64_t s;
    std::uint64_t nästa()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double enhet() { return static_cast<double>(nästa() >> 11) * (1.0 / 9007199254740992.0); }
    double mellan(double a, double b) { return a + (b - a) * enhet(); }
};

std::uint64_t blanda(std::uint64_t x);

// ---------------------------------------------------------------- mätetal

struct Serie {
    std::vector<double> v;
    void   add(double x) { v.push_back(x); }
    bool   tom() const { return v.empty(); }
    double kvantil(double q) const;
    double median() const { return kvantil(0.5); }
    double max() const;
    int    över(double gräns) const;
};

// ------------------------------------------------------------------ minnet

struct Minne {
    double process = -1.0;   // MB — macOS phys_footprint, Windows PrivateUsage, Linux VmRSS
    double topp    = -1.0;   // MB — processens största hittills
    double arbete  = -1.0;   // MB — Windows arbetsmängden, macOS resident
    double gpu     = -1.0;   // MB — Windows DXGI (lokalt + delat) för processen
};

Minne läsMinne();

// ------------------------------------------------------------------ energin
//
// Processens CPU-tid (alla trådar, också arbetstrådarna) och — på macOS —
// väckningarna, det Aktivitetskontrollens energipåverkan bygger på.
struct Energi {
    double cpuSek     = -1.0;
    double väckningar = -1.0;
};

Energi läsEnergi();

// "123 MB", eller "—" för ett okänt värde.
std::string mb(double v);

// ------------------------------------------------------------ plattornas PNG
//
// 64 bilder om 256 × 256 — flygfotolika: mjuka färgfält, brus, en väg —
// kodade EN gång. En platta väljer sin ur nyckeln; varje avkodning blir en
// egen Bitmap (och en egen textur), som från nätet.
inline constexpr int kPngAntal = 64;

const std::vector<std::vector<std::uint8_t>>& pngPool();

// Plattans bild ur nyckeln, avkodad nu — utan kvarhållna pixlar (släpps när
// bilden laddats upp, som appen gör före posten till UI-tråden).
glaze::Bitmap avkoda(std::uint64_t nyckel);

// Nätets fördröjning: 25–90 ms, deterministiskt ur nyckeln.
void nätet(std::uint64_t nyckel);

// Plattornas RUTNÄT: nivå L har plattor om 64 · 2^(19 − L) m (0,25 m per px
// på nivå 19).
double plattM(int nivå);

// EN PLATTAS HÖRN i världen — rutan (x0, y0)–(x0 + sida, y0 + sida) vriden en
// knapp grad kring sin mitt (projektionens vridning, så att bilden ligger på
// fyra fria hörn): övre vänster, övre höger, nedre höger, nedre vänster.
std::array<glaze::Point, 4> plattHörn(double x0, double y0, double sida);

// ------------------------------------------------------------ arbetstrådarna
//
// En FIFO-kö som UI-tråden kan RENSA (en platta som inte längre syns ska
// inte hämtas). Svaren postas med `glaze::app().post` — och körs bara om
// sessionen lever (`g_liv`).
extern std::shared_ptr<std::atomic<bool>> g_liv;

class Arbetare {
public:
    explicit Arbetare(int trådar);
    ~Arbetare();

    void stoppa();
    void lägg(std::uint64_t nyckel, std::function<void()> jobb);

    // Behåll bara jobben vars nyckel `behåll` svarar ja för; svarar med de
    // rensade nycklarna.
    template <class F>
    std::vector<std::uint64_t> rensa(F behåll)
    {
        std::vector<std::uint64_t>  bort;
        std::lock_guard<std::mutex> lås(m_);
        for (auto it = kö_.begin(); it != kö_.end();)
            if (!behåll(it->first)) {
                bort.push_back(it->first);
                it = kö_.erase(it);
            } else {
                ++it;
            }
        return bort;
    }

    std::size_t köade();

private:
    void slinga();

    std::mutex                                                   m_;
    std::condition_variable                                      cv_;
    std::deque<std::pair<std::uint64_t, std::function<void()>>> kö_;
    std::vector<std::thread>                                     trådar_;
    bool                                                         stopp_ = false;
};

template <class F>
void postaTillUi(F f)
{
    glaze::app().post([liv = g_liv, f = std::move(f)]() mutable {
        if (liv->load())
            f();
    });
}

// ------------------------------------------------------------- föremålen
//
// GALLERSTOLPEN (canvas_perf3d:s — rör mellan lådans hörn) som PLATTA,
// icke-indexerade trianglar: läge, normal och färg per hörn (84 byte per
// triangel). `nivåer` 1–4 detaljnivåer i EN mesh, med andelarna 1 · 0,4 ·
// 0,15 · 0,05 av `trianglar` och den sista nivån obegränsad; `cpuKopia`
// är `MeshDesc::keepCpuCopy`.
glaze::Mesh byggStolpe(int trianglar, std::uint64_t frö, int nivåer = 4, bool cpuKopia = true);

// TRÄDETS KON i en nivå per post: sidornas antal och nivåns största avstånd
// (0 = obegränsat, bara sist).
glaze::Mesh byggKon(const std::vector<std::pair<int, double>>& nivåer);

// ================================================================ FÖNSTREN
//
// Ett fönster vars input sessionen INJICERAR genom fönstrets egen väg
// (`AppWindow::handleEvent` — gesterna, träffprovet, statusraden och fokus
// är ramverkets).
class Fönster : public glaze::AppWindow {
public:
    // MUSEN vid (lx, ly) i ritytans lokala logiska px — med knappen, hjulet
    // (rader) och modifierarna. Antecknar tiden (`input`).
    void injicera(const glaze::Canvas& yta, gfx::EventType t, double lx, double ly,
                  gfx::MouseButton b = gfx::MouseButton::None, float hjul = 0.0f,
                  std::uint32_t mods = 0);
    // EN TANGENT ned eller upp.
    void tangent(gfx::Key k, bool ner);
    // INPUT → RITNING: `input()` antecknar tiden, och nästa `ritning` lägger
    // tiden därefter i serien.
    void input();
    void ritning(Serie& latens);

private:
    Klocka::time_point input_{};
    bool               väntar_ = false;
};

// Ritytans yta i fönstrets fysiska px och skalan, och ytans storlek i
// logiska px.
gfx::Rectf ytansRuta(const glaze::Canvas& yta, float& skala);
gfx::Sizef ytansStorlek(const glaze::Canvas& yta);

// PEKARENS VÄG: en lissajous över ytan — punkten i steg `i`, lokala logiska px.
glaze::Point lissajous(int i, gfx::Sizef yta);

// ETT PANORERINGSDRAG i taget med mittenknappen: `drag` drag om 40 rörelser
// på (dx, dy) logiska px kring ytans mitt — tryck, rörelserna, släpp och tre
// tickar luft, `kTickPerDrag` tickar per drag. Svarar sant när alla är gjorda.
inline constexpr int kTickPerDrag = 45;
bool panorera(Fönster& f, const glaze::Canvas& yta, int i, int drag, double dx, double dy);

// FÖRSTAPERSONEN: W/S fram och bak i `fart` m/s,
// A/D i sidled i halva farten och Q/E vrider 67,5°/s — svarar om ögat eller
// blicken rördes.
bool förstaperson(const glaze::AppWindow& w, double sek, double fart, glaze::Point& öga,
                  double& gir);

// 3D-VYNS HUD i `onPaintOverlay`: texten uppe till vänster, hårkorset och
// kompassen med blicken `gir` (grader, kompass).
void ritaHud(glaze::Painter& p, const std::string& text, double gir);

// ------------------------------------------------------- stegens mätning
//
// Ett steg i sessionen samlar sina tal per MÄTT FÖNSTER (sessionens ordning).
struct FönsterMått {
    Serie         frame;     // ms mellan tickarna
    Serie         latens;    // ms från input till nästa onPaint
    int           ritningar = 0;
    std::uint64_t anrop     = 0, uppByte = 0, bilder = 0, buffertar = 0;
    std::uint64_t brott     = 0;   // uppladdningar som bröt render-passet
    // PROFILERAREN (när sessionen profilerar): stegets bilder, den äldsta först.
    std::vector<glaze::FrameProfile> profilBilder;
};

// En STRÖMNINGS räknare i steget: det som kom in och det som släpptes.
struct Ström {
    std::string namn;
    int         in = 0, ut = 0;
};

struct StegMått {
    std::string              namn;
    double                   sekunder = 0.0;
    std::vector<FönsterMått> f;        // per mätt fönster
    std::vector<Ström>       ström;    // sessionens strömmar, i dess ordning
    Serie                    bygg;     // ms för setShapes/setShape i UI-tråden
    std::size_t              maxKö = 0;
    Minne                    minne;
    double                   toppSteg = -1.0;   // MB — största samplade under steget
    Energi                   energiFöre;
    double                   cpuProcent = -1.0;   // av EN kärna, under steget
    double                   väckPerSek = -1.0;
    std::size_t              bildcache = 0;
    std::size_t              chunkar   = 0;   // ritytornas chunkar på GPU:n efter steget
    std::uint64_t            uppTotalt = 0;
    bool                     rörelse = false;   // stegets `Steg::rörelse`
    std::vector<bool>        tickar;            // stegets `Steg::tickar`
    // PROFILERAREN (när sessionen profilerar): stegets pumpvarv och verktygens
    // etiketter (`glaze.hitTest`, `glaze.castRay`, `glaze.heightAt` …).
    std::vector<glaze::PumpProfile> pump;
    std::vector<glaze::LabelStats>  etiketter;
};

extern StegMått* g_steg;   // det pågående steget

// PROFILERARENS MÅTT för ett fönster i ett steg — räknade ur `profilBilder`, bara
// genom profilerarens API: intervallen ur `FrameProfile::interval` för de
// bilder vars FÖREGÅNGARE hade `continued` (takten, inte pauserna mellan två
// händelser — stegets första bild har ingen föregångare i steget), faserna ur
// `ms[]` (GPU-tiden bara för bilderna som fått sitt svar) och räknarna ur
// `count[]`.
inline constexpr std::size_t kFaser   = std::size_t(glaze::ProfilePhase::Count_);
inline constexpr std::size_t kRäknare = std::size_t(glaze::ProfileCount::Count_);

struct Profil {
    std::size_t                  bilder = 0;
    Serie                        intervall;   // ms
    Serie                        latens;      // input → present, ms (bilderna med inmatning)
    std::array<Serie, kFaser>    fas;         // ms per bild
    std::array<double, kRäknare> perBild{};   // räknarnas medel per bild
};

Profil profil(const FönsterMått& f);

// Processens minne samplas i varje ritning och tick — stegets egen topp.
void sampla(const char* var);

// =============================================================== SESSIONEN
//
// Ett MÄTT FÖNSTER: fönstret, ritytan som tickar i det och köns djup (det
// som väntar i strömningen).
struct MättFönster {
    std::string                  namn;   // i utskriften ("kartan", "3D")
    glaze::AppWindow*            fönster = nullptr;
    glaze::Canvas*               yta     = nullptr;
    std::function<std::size_t()> kö;
};

// Ett steg körs en tick i taget av sitt DRIVANDE fönster och svarar sant när
// det är klart — eller av en timer (`timerMs`, `frågaMs`), och då tickar
// ingenting. Vilosteg väntar tills strömningen kommit ikapp.
struct Steg {
    std::string              namn;
    int                      driver;   // det mätta fönstrets plats
    std::vector<bool>        tickar;   // per mätt fönster: tickar ritytan i steget?
    double                   tak;      // sekunder innan steget fäller
    std::function<bool(int)> kör;
    bool                     vila = false;
    // > 0: steget drivs av en TIMER i stället för tickar — inget tickar, och
    // `kör(0)` körs en gång vid starten. Så mäts appen när ingenting händer
    // (hård regel 1: ~0 % CPU) och när ett fönster stängts.
    int                      timerMs = 0;
    // > 0 (med `timerMs` 0): steget drivs av en timer som FRÅGAR `kör(i)` var
    // `frågaMs` tills den svarar sant — inget tickar, så stegets bilder är
    // bara de appen själv orsakar (det som landar medan kameran står still,
    // en glidning).
    int                      frågaMs = 0;
    // Ett RÖRELSESTEG: användaren rör kameran eller pekaren, och takten är
    // det som mäts (terrain_perf `--mål`).
    bool                     rörelse = false;
};

class Session {
public:
    Session(std::vector<MättFönster> fönster, std::vector<std::string> strömmar);

    void lägg(Steg s) { steg_.push_back(std::move(s)); }
    std::vector<StegMått>& mått() { return mått_; }
    bool klar() const { return klar_; }

    void börja();

    // En tick från fönstret `vem`.
    void tick(int vem, double sek);

    // mått_ får inte reallokera under ett steg (g_steg pekar in i den).
    void reservera() { mått_.reserve(steg_.size() + 1); }

    // PROFILERAREN: på, med en ring som rymmer ett steg (`kProfilRing` bilder
    // per fönster) — och varje steg mäts med den: ringarna töms när steget
    // börjar och läses när det slutar, och profilerarens rader skrivs efter
    // stegets.
    void profilera();
    static constexpr std::size_t kProfilRing = std::size_t{1} << 14;

private:
    // Stegets drivning: tidsgränsen, och nästa steg när `kör` svarar sant.
    void driv();
    void nästa();
    void avsluta();
    void skrivSteg(const StegMått& m) const;
    void skrivProfil(const StegMått& m) const;

    std::vector<MättFönster> fönster_;
    std::vector<std::string> strömmar_;
    glaze::Timer             timer_;
    glaze::Timer             fråga_;   // `Steg::frågaMs`
    std::vector<Steg>        steg_;
    std::vector<StegMått>    mått_;
    int                      index_ = -1;
    int                      i_     = 0;
    Klocka::time_point       start_{}, stegStart_{};
    std::uint64_t            uppTotalt_ = 0;
    bool                     klar_  = false;
    bool                     profil_ = false;
};

} // namespace perf
