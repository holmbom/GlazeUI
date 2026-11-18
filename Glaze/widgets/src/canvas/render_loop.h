#pragma once
// RITYTANS RENDERINGSTRÅD — INTERN. En tråd per fönster ritar fönstrets
// ritytor (frame_job.h) i delade bilder (gfx/scene_context.h), och fönstrets
// tråd lägger bara den senast färdiga bilden på plats. Fönstrets tråd väntar
// aldrig på renderingstråden: den lämnar ett jobb, och den tar en bild om en
// finns.
//
//   fönstret:  submit(jobb)            → kön (det senaste jobbet per rityta)
//   tråden:    renderCanvasFrame       → SharedTarget::publish → Ready
//              väckningen (jobbets `wake`, i fönstertrådens ramkö)
//   fönstret:  present(rityta)         → den nyaste Ready-bilden blir Shown
//
// TRÅDEN ÄGER scen-kontexten, en egen bildcache med liggare och egna
// ritytsresurser — allt skapat och rivet på tråden. Ingenting där rör en
// widget, appen eller fönstrets renderare.
//
// BREVLÅDAN har `kCanvasFrameBuffers` bilder per rityta: en som visas, en
// som är färdig och en att rita i — så ingen sida väntar på den andra. En
// bild i fel storlek ersätts när den är ledig.
//
// LIVSTIDEN: loopen rivs av fönstertråden (`App::executeClose`) FÖRE
// fönstrets egna resurser och dess renderare. Destruktorn lämnar tillbaka de
// visade bilderna, stoppar tråden och väntar in den.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gfx/scene_context.h>

#include "frame_job.h"

namespace gfx {
class Renderer;
}

namespace glaze::detail {

// Bilderna i växling per rityta: visad, färdig, under ritning.
inline constexpr int kCanvasFrameBuffers = 3;

class CanvasRenderLoop {
public:
    // Startar tråden och scen-kontexten på `window`s grafikkort. nullptr
    // (loggat av fasaden) när backenden inte har någon scen-kontext —
    // fönstret ritar då sina ritytor själv. Fönstertråden.
    static std::unique_ptr<CanvasRenderLoop> start(gfx::Renderer& window);
    ~CanvasRenderLoop();
    CanvasRenderLoop(const CanvasRenderLoop&)            = delete;
    CanvasRenderLoop& operator=(const CanvasRenderLoop&) = delete;

    // FÖNSTERTRÅDEN ───────────────────────────────────────────────────────

    // Lämnar ett jobb för ritytan `job.scene->id`. Ett jobb som väntar för
    // samma rityta ersätts — bara det senaste ritas. `wake` körs i
    // fönstertrådens ramkö när bilden är färdig. Väntar aldrig.
    void submit(CanvasFrameJob job, std::function<void()> wake);

    // Den bild ritytan visar: tar den nyaste färdiga om en finns (den förra
    // lämnas tillbaka), annars den som redan visas. nullptr före den första.
    // Pekarna gäller till nästa `present` för ritytan eller till `forget`.
    struct Shown {
        gfx::Image*              image = nullptr;
        int                      width = 0, height = 0;   // bildens px
        const CanvasFrameResult* result = nullptr;
        bool                     fresh  = false;   // togs i det här anropet
    };
    Shown present(std::uint64_t canvas);

    // BESTÄLLER EN BILD AV PIXLAR (`Canvas::renderToBitmap`): jobbet ritas av
    // tråden med `renderCanvasBitmap` och `done` körs i fönstertrådens ramkö
    // med bilden — eller utan, när den inte gick att rita. Beställningarna
    // ritas i den ordning de lämnades, före ritytornas bilder. EXAKT ETT
    // svar per beställning: det som inte hann ritas när loopen rivs svaras
    // tomt. Väntar aldrig.
    // `redo` (får vara tom) körs i fönstertråden i stället för att svara
    // med trådens bild när jobbet hade något bara UI-tråden kan hämta (en
    // `RenderTexture`): den ritar bilden där och dess svar går till `done`.
    using BitmapDone = std::function<void(std::optional<Bitmap>)>;
    using BitmapRedo = std::function<std::optional<Bitmap>()>;
    void order(std::shared_ptr<const CanvasFrameJob> job, gfx::Color background,
               BitmapDone done, BitmapRedo redo);

    // BESTÄLLER EN SIDA (utskriften): jobbet ritas av tråden med
    // `renderPageJob`, och `done` körs i fönstertrådens ramkö med utfallet
    // (`ok` false = ett fel, också när loopen revs innan sidan ritades),
    // om bilden är komplett, pixlarna och `needsUiThread` — sidan hade något
    // bara UI-tråden kan hämta, och anroparen ritar då om den där. Samma kö
    // och löfte som `order`.
    using PageDone = std::function<void(bool ok, bool complete,
                                        std::vector<std::uint8_t> rgba,
                                        bool needsUiThread)>;
    void orderPage(std::shared_ptr<const PageJob> job, PageDone done);

    // Ritytan visar inget mer här (den revs eller bytte fönster): dess visade
    // bild lämnas tillbaka och brevlådan släpps av tråden.
    void forget(std::uint64_t canvas);

    // Loopens löpnummer — aldrig återanvänt, så en rityta ser när fönstret
    // fått en NY loop (en adress kan komma tillbaka).
    std::uint64_t id() const { return id_; }

    // Jobb som ritats — grindarnas räknare.
    std::uint64_t framesRendered() const;

    // GRINDARNAS SPÄRR: medan den är satt ritar tråden ingenting (jobben
    // väntar i kön).
    void setPaused(bool on);

private:
    enum class SlotState : std::uint8_t { Free, Writing, Ready, Shown };
    struct Slot {
        std::unique_ptr<gfx::SharedTarget> target;   // trådens
        SlotState                          state = SlotState::Free;
        CanvasFrameResult                  result;
        gfx::Image*                        image = nullptr;   // medan Shown
    };
    struct Mailbox {
        Slot slots[kCanvasFrameBuffers];
        // Ritytans livstecken (scenens): när det gått ut släpps brevlådan.
        std::weak_ptr<const char> life;
        bool                      forgotten = false;
    };
    struct Pending {
        CanvasFrameJob        job;
        std::function<void()> wake;
    };
    // En beställning: arbetet på tråden, som svarar med det som ska köras i
    // fönstertrådens ramkö, och svaret när arbetet aldrig blev gjort.
    struct Order {
        std::function<std::function<void()>(const Ctx&)> work;
        std::function<void()>                            dropped;
    };
    void enqueue(Order o);

    CanvasRenderLoop();
    void run(gfx::Renderer& window);
    void render(Pending& p, const Ctx& base);
    // Under `lock_`: lämnar tillbaka brevlådans visade bild och märker den.
    void forgetLocked(Mailbox& m);
    // Under `lock_`: brevlådorna vars rityta dött märks som glömda.
    void forgetDead();

    const std::uint64_t id_;

    mutable std::mutex      lock_;
    std::condition_variable wakeup_;
    std::condition_variable started_;
    // Under `lock_`:
    std::map<std::uint64_t, Pending>                  queue_;      // per rityta
    std::vector<std::uint64_t>                        order_;      // köns ordning
    std::deque<Order>                                 orders_;     // beställda bilder
    std::map<std::uint64_t, std::unique_ptr<Mailbox>> boxes_;
    bool          stop_    = false;
    bool          paused_  = false;
    bool          ready_   = false;   // tråden har svarat på starten
    bool          ok_      = false;   // …och kontexten gick att ta i bruk
    bool          forgotten_ = false; // en glömd brevlåda väntar på tråden
    std::uint64_t frames_  = 0;

    // Trådens — rörs aldrig av fönstertråden.
    std::unique_ptr<gfx::SceneContext> context_;
    std::thread                        thread_;
};

// TRÅDLÄGET för hela processen — på som förval; miljövariabeln
// `GLAZE_CANVAS_THREAD=0` stänger av det. Av ritar varje fönster sina ritytor
// själv (grindar som räknar fönsterrenderarens anrop). Läses när ett fönster
// frågar efter sin loop; ett fönster som redan har en behåller den.
bool canvasThreading();
void setCanvasThreading(bool on);

// GRINDARNAS KROK: också ett offscreen-fönster får en renderingstråd (av som
// förval — ett offscreen-fönster ritar EN bild som läses direkt, så scenen
// ritas i samma bild).
bool canvasThreadingOffscreen();
void setCanvasThreadingOffscreen(bool on);

// RITAR bilden `present` gav i fönstret: 1:1 med övre vänstra hörnet i
// (`x`, `y`) fysiska px, med ritytans ton. Under det klipp som gäller.
void drawCanvasFrame(const Ctx& c, const CanvasRenderLoop::Shown& bild, float x,
                     float y, float opacity);

} // namespace glaze::detail
