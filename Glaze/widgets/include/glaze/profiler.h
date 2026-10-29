#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// PROFILERAREN — var tiden i en bild går, mätt av
// ramverket självt och läst av appen när den vill:
//
//     glaze::profiler().setEnabled(true);        // eller GLAZE_PROFILE=1
//     logg.setInterval(1000);                    // en glaze::Timer
//     logg.onTick = [this] { minLogg(glaze::profiler().report(1.0)); };
//     logg.start();
//
// ALLTID INKOMPILERAD OCH AV SOM FÖRVAL — också i Release, så att en app mäts
// där den körs. `setEnabled(true)` eller miljövariabeln `GLAZE_PROFILE` (satt
// till något annat än tomt eller 0, läst vid första `profiler()` och när App
// skapas) slår på den. Avstängd spelar den in ingenting: pumpens krokar och
// en `ProfileScope` kostar ett funktionsanrop och en atomisk läsning.
//
// LÄSES GENOM HÄMTNING, aldrig en callback: `frames`, `pump`, `summary`,
// `scopes` och `report`. Den ber aldrig själv om en bild och väcker aldrig
// pumpen (regel 1) — en app som loggar en gång per sekund gör det med en
// `glaze::Timer`, som ovan.
//
// TVÅ SLAGS DATA:
//   * BILDERNA OCH PUMPVARVEN — UI-trådens, med fasta faser och räknare,
//     exakta per bild, i en ring per fönster och en för pumpen. Ett fönsters
//     ring släpps när fönstret stängs (popuperna kommer och går). Läses i
//     UI-tråden, som all annan glaze.
//   * ETIKETTERNA — appens egna (`ProfileLabel`, `ProfileScope`,
//     `profileCount`) och ramverkets verktyg (`glaze.hitTest`,
//     `glaze.castRay`, `glaze.heightAt`), från VILKEN TRÅD SOM HELST. Varje
//     tråd har ett eget block med ett eget lås, som läsningen tar kort.
//     Tiderna ligger i ett logaritmiskt histogram med 8 fack per fördubbling:
//     en percentil kan vara högst ~9 % för hög, medan antalet, summan och
//     maxvärdet är exakta. Etikettnamnen kopieras in, och registret bor i
//     libglaze — en plugin delar det med programmet.
//
// Klockan är `std::chrono::steady_clock` (QPC på Windows), och ingenting
// klampas: en bild på 300 ms står som 300 ms.
//
// UTANFÖR REGEL 8 MED FLIT: profileraren har inget utseende, ingen plats i
// ett fönster och mäter hela pumpen — infrastruktur som `App::post`, inte en
// komponent. Den har ingen `.glazeui`-form.

namespace glaze {

class AppWindow;

// FASERNA I EN BILD, i ms. INNEHÅLLET (Content) är innehållspasset och
// omfattar ritytans fyra faser och LAYOUTEN — placeringen görs i
// ritvandringen och har inget eget pass att mäta. Ritytans faser: lagren,
// texten och rutnätet förbereds (CanvasPrepare), terrängens draperingar ritas
// om (CanvasDrapes), scenen ritas — med lagrens målare, som ritar i lagrens
// ordning — UTAN `onPaint` (CanvasDraw), och `onPaint` målar
// (CanvasPaint); en rityta med del-vyer lägger ihop vyerna. OVERLAYEN är
// overlay-lagret. PRESENTATIONEN (Present) är
// tiden från overlayen till bildens slut och omfattar dess VÄNTAN
// (PresentWait — vsync och en ledig drawable; ett dolt fönster väntar inte).
// GPU:N (Gpu) är GPU:ns egen tid för bilden, som kommer i efterhand (2–3
// bilder senare) och saknas (0) i en bild utan svar. KOMPILERINGEN (Compile)
// är tiden att bygga program och varianter under bilden.
enum class ProfilePhase : std::uint8_t {
    Content, CanvasPrepare, CanvasDrapes, CanvasDraw, CanvasPaint,
    Overlay, Present, PresentWait, Gpu, Compile, Count_ };

// RÄKNARNA I EN BILD. De sex första är renderarens (`gfx::RenderStats` som en
// differens över bilden — `resetStats` anropas aldrig): rit-anropen, de
// uppladdade byten, de skapade buffertarna, uppladdningarna som bröt ett pass
// (Metal), de byggda programmen och de skapade målen. Sedan ritytans: 3D-målen
// som ritades om, draperingarna som ritades om, deras pixlar och nycklarna som
// räknades, lagren som byggdes om och textbyggena. Sist skadan: ytorna en
// partiell omritning ritade (1 för en full), och 1 för en full omritning.
enum class ProfileCount : std::uint8_t {
    DrawCalls, BytesUploaded, BuffersCreated, UploadBreaks, ProgramsBuilt,
    TargetsCreated, TargetRenders, DrapeRenders, DrapePixels, DrapeKeys,
    LayerRebuilds, GlyphBuilds, DamageRects, FullDamage, Count_ };

// EN BILD i ett fönster.
struct FrameProfile {
    std::uint64_t window    = 0;   // fönstrets id (`gfx::Window::id`)
    std::uint64_t frame     = 0;   // renderarens löpnummer (`frameSerial`)
    std::uint64_t iteration = 0;   // pumpvarvet som ritade den (0 = ingen pump)
    double at           = 0;   // början, ms sedan profilerarens start
    double interval     = 0;   // ms sedan fönstrets förra bild (0 = den första)
    double total        = 0;   // ms från början till slut
    // Ms från den FÖRSTA inmatningen till fönstret sedan dess förra bild till
    // den här bildens slut — input → present. 0 = ingen inmatning.
    double inputLatency = 0;
    // Nästa bild var begärd när den här var klar (fönstret ville rita igen,
    // eller något låg postat) — intervallet till nästa bild är då takten och
    // inte en paus. `summary` räknar bara sådana intervall.
    bool   continued    = false;
    // Ritad UTANFÖR pumpen — D3D11:s modala storleksloop (`WM_SIZE`/`WM_PAINT`
    // ritar direkt). `total` är då innehåll + overlay; presentationen ligger
    // utanför det som går att mäta.
    bool   outsidePump  = false;
    std::array<float, std::size_t(ProfilePhase::Count_)>          ms{};
    std::array<std::uint64_t, std::size_t(ProfileCount::Count_)> count{};
};

// ETT PUMPVARV. Delarnas tid i ms, i varvets ordning: väntan i `waitEvent`,
// händelserna (dispatch), de postade funktionerna (och den längsta av dem),
// appens timrar, commit-köerna och stängningarna. Bilderna står i
// `FrameProfile` med varvets `iteration`. En NÄSTLAD pump — en modal dialog
// öppnad i en handler eller en postad funktion — räknas inte in i den yttre
// delens tid: den har egna poster med `depth` + 1.
struct PumpProfile {
    std::uint64_t iteration = 0;   // löpnumret, över alla djup
    double        at        = 0;   // början, ms sedan profilerarens start
    float wait = 0, events = 0, posted = 0, postedMax = 0, timers = 0,
          commits = 0, closes = 0;
    std::uint32_t eventCount = 0, postedCount = 0;
    std::uint32_t depth = 0;   // nästlingsdjupet — 1 = den yttersta pumpen
};

// EN ETIKETTS statistik. `count` är antalet scopes plus `profileCount`s n;
// tiderna gäller scopes (0 för en etikett som bara räknas).
struct LabelStats {
    std::string   name;
    std::uint64_t count = 0;
    double totalMs = 0, p50 = 0, p95 = 0, p99 = 0, maxMs = 0;
};

// ETT FÖNSTERS SAMMANFATTNING över ett tidsfönster (`Profiler::summary`).
// Intervallen räknas bara för bilder vars FÖREGÅNGARE hade `continued` —
// takten, inte pauserna mellan två händelser.
struct WindowSummary {
    std::uint64_t window = 0;    // fönstrets id
    std::string   deviceName;    // GPU:n som ritar fönstret
    std::size_t   frames = 0;    // bilderna i tidsfönstret
    std::size_t   paced  = 0;    // av dem: intervallens underlag (se ovan)
    double intervalP50 = 0, intervalP95 = 0, intervalP99 = 0,
           intervalMax = 0;      // ms
    std::size_t missed50Hz  = 0;   // intervall över 20 ms
    std::size_t missed150Hz = 0;   // intervall över 6,7 ms
    // Fasernas p50 och p95 över bilderna — Gpu bara över bilderna som fått
    // sitt svar.
    std::array<float, std::size_t(ProfilePhase::Count_)> phaseP50{}, phaseP95{};
    // Räknarnas medel per bild.
    std::array<double, std::size_t(ProfileCount::Count_)> perFrame{};
};

class Profiler {
public:
    Profiler(const Profiler&)            = delete;
    Profiler& operator=(const Profiler&) = delete;

    // På eller av — från vilken tråd som helst. Avstängningen tömmer
    // ingenting: det inspelade står kvar tills `clear()`.
    void setEnabled(bool on);
    bool enabled() const;

    // Ringarnas storlek i poster — varje fönsters och pumpens; förval 4096.
    // En mindre ring behåller de senaste, och 0 räknas som 1.
    void        setCapacity(std::size_t frames);
    std::size_t capacity() const;

    // Fönstrets bilder, den äldsta först — de `max` senaste (0 = hela
    // ringen). Tomt för ett fönster som inte ritat medan profileraren var på.
    std::vector<FrameProfile> frames(const AppWindow& w,
                                     std::size_t max = 0) const;
    // Pumpvarven, det äldsta först — de `max` senaste (0 = hela ringen).
    std::vector<PumpProfile> pump(std::size_t max = 0) const;
    // Fönstrets bilder de senaste `seconds` sekunderna (0 = hela ringen).
    WindowSummary summary(const AppWindow& w, double seconds = 0) const;
    // Etiketterna som använts, i namnordning; `reset` nollar dem efter
    // läsningen. Från vilken tråd som helst.
    std::vector<LabelStats> scopes(bool reset = false) const;
    // Allt ovan som TEXT för appens logg: varje fönster med sin titel och
    // GPU, pumpen och etiketterna, de senaste `seconds` sekunderna (0 = hela
    // ringarna). Avstängd: en rad som säger det.
    std::string report(double seconds = 0) const;
    // Tömmer ringarna och etiketterna.
    void clear();

private:
    Profiler() = default;
    friend Profiler& profiler();
};

// Den enda profileraren — läser `GLAZE_PROFILE` vid första anropet.
Profiler& profiler();

// Fasens och räknarens namn i `report()` — identifierare i gemener ASCII
// (`content`, `wait`, `draw_calls` …), för en app som skriver egna tabeller.
const char* profilePhaseName(ProfilePhase p);
const char* profileCountName(ProfileCount c);

// EN ETIKETT — appens namn på något den mäter. Samma namn ger samma id i
// varje tråd och modul; namnet kopieras in. Skapa den en gång (en medlem,
// eller en statisk i en funktion) — varje konstruktion slår upp namnet under
// registrets lås.
class ProfileLabel {
public:
    explicit ProfileLabel(std::string_view name);
    std::uint32_t id() const { return id_; }

private:
    std::uint32_t id_;
};

// TIDEN FÖR ETT BLOCK under etiketten, från vilken tråd som helst:
//
//     static const glaze::ProfileLabel kLäs{"app.läsPlatta"};
//     { glaze::ProfileScope mät(kLäs); läsPlatta(); }
//
// Avstängd vid konstruktionen spelar den in ingenting.
class ProfileScope {
public:
    explicit ProfileScope(const ProfileLabel& label) noexcept;
    ~ProfileScope()
    {
        if (id_ != kOff)
            finish();
    }
    ProfileScope(const ProfileScope&)            = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    static constexpr std::uint32_t kOff = 0xffffffffu;   // spelar inte in
    void          finish() noexcept;
    std::uint32_t id_;
    std::int64_t  start_ = 0;   // ns
};

// RÄKNAR `n` under etiketten — utan tid. Ett negativt n drar ifrån (antalet
// i `LabelStats` är aldrig under 0). Avstängd räknas ingenting.
void profileCount(const ProfileLabel& label, std::int64_t n = 1) noexcept;

} // namespace glaze
