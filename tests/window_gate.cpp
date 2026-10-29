// window_gate — FÖNSTRETS grind: det som bor på `glaze::AppWindow` och
// går att mäta utan en pump.
//
// Kör:  ./build/tests/window_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Mäter INNEHÅLLSSTORLEKEN och STORLEKSGRÄNSERNA: `setContentSize` räknar
// baklänges genom det chrome fönstret FAKTISKT har, och storleken ska
// FÖLJA MED när chromet ändras — vid statusradens synlighet och vid
// stilbyte (`barHeight`/`statusBarH` är stilmått). Mätningen kräver inte
// show(), och det utnyttjas här på två sätt:
//
//   1. Uträkningen behöver INGET fönster. Tvåfasen gör att
//      `AppWindow`-konstruktorn bara skapar en proxy, och `setSize` före
//      `init()` är en no-op — så hela chrome-matematiken går att mäta rakt
//      av, på alla tre plattformarna.
//   2. Omräkningen vid STILBYTE går bara genom öppna fönster
//      (`App::applyContentSizeAll` itererar fönsterregistret), så den
//      delen kräver ett fönster — men ett DOLT duger
//      (`setOffscreen(true)`), och Linux har dessutom en surfacelös
//      headless-väg. Saknas display hoppas de fallen över med en skriven
//      rad i stället för att fälla grinden.
//
// Det är det här som fångar att omräkningen glöms på ett av
// anropsställena.
//
// Del R mäter STÄNGNINGENS SKÄL
// (`CloseArgs::reason`) med en pump — fallen, sabotagen och det den inte
// mäter står vid `fallStängningensSkäl` nedan.

#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <glaze/glaze.h>

#ifdef GLAZE_BACKEND_D3D11
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // R3–R5: PostQuitMessage, WM_CLOSE
#endif

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
    ++g_fails;
}

std::string sz(const gfx::Sizef& s)
{
    return std::to_string(static_cast<int>(s.w)) + "x" +
           std::to_string(static_cast<int>(s.h));
}

// Fönstret som mäts. Chrome::Custom med flit: det är där TopBar ÄR en
// widget inne i klientytan och alltså det enda läget där chromet räknas
// bort — Native lägger namnlisten utanför klientytan, None har ingen.
class MätWindow : public glaze::AppWindow {
public:
    MätWindow()
        : glaze::AppWindow("window_gate", 800, 600, gfx::Chrome::Custom)
    {
    }
};

// HANDLER-FORMERNA. Bygget ÄR grinden: de fem fallen
// nedan är kompileringsfixturer, och avfyrningen mäts på köpet.
// `clicked()` är protected på Widget, så knappen ärvs för att kunna
// avfyras utan mus.
class KnappSomKanKlickas : public glaze::Button {
public:
    using glaze::Button::Button;
    void klicka() { clicked(); }
};

// ---------------------------------- KOMPOSIT-STOPPET + `focusOrder()` (2a)
//
// Tab-vandringen rekurserade OVILLKORLIGT genom hela trädet: en spinner
// byggd av tre widgets blev tre tabbstopp. `Widget::focusesChildren()` är
// stoppet, och `AppWindow::focusOrder()` gör listan mätbar.
//
// TRE FALL, för det första ensamt bevisar ingenting — det visar bara att
// ett `continue` finns, inte att det står på rätt plats. De två andra är
// exakt de lägen där ett OVILLKORLIGT `continue` hade släckt hela grenen.

// En komposit vars fokuserbarhet går att styra utifrån, så samma träd kan
// mätas i alla tre lägena utan tre olika klasser.
class Komposit : public glaze::Panel {
public:
    bool fokuserbar = true;

    bool focusable() const override { return fokuserbar; }
    bool focusesChildren() const override { return true; }
};

class FokusWindow : public glaze::AppWindow {
public:
    FokusWindow() : glaze::AppWindow("focus_gate", 800, 600) {}
    using glaze::AppWindow::add;
    using glaze::AppWindow::focusOrder;
};

void fallFokusOrder()
{
    // Trädet: knapp · komposit(knapp, knapp, textfält) · knapp.
    // Utan stoppet är listan sex poster; med stoppet tre.
    auto bygg = [](FokusWindow& w) {
        auto* före  = w.add<glaze::Button>("före");
        auto* komp  = w.add<Komposit>();
        auto* efter = w.add<glaze::Button>("efter");
        komp->add<glaze::Button>("inre 1");
        komp->add<glaze::Button>("inre 2");
        komp->add<glaze::TextInput>();
        return std::tuple<glaze::Widget*, Komposit*, glaze::Widget*>{före, komp,
                                                                     efter};
    };

    // 1. FOKUSERBAR + ENABLED ⇒ kompositen ERSÄTTER sina barn.
    {
        FokusWindow w;
        auto [före, komp, efter] = bygg(w);
        const auto ordning       = w.focusOrder();
        check(ordning.size() == 3,
              "komposit: fokuserbar + enabled ⇒ EN post för hela kompositen",
              std::to_string(ordning.size()));
        check(ordning.size() == 3 && ordning[0] == före && ordning[1] == komp &&
                  ordning[2] == efter,
              "komposit: ordningen är före · komposit · efter");
    }

    // 2. `!focusable()` ⇒ barnen syns. Ett ovillkorligt
    // `continue` hade gjort hela grenen otabbbar.
    {
        FokusWindow w;
        auto [före, komp, efter] = bygg(w);
        komp->fokuserbar         = false;
        const auto ordning       = w.focusOrder();
        check(ordning.size() == 5,
              "komposit: !focusable ⇒ grenen försvinner INTE (barnen syns)",
              std::to_string(ordning.size()));
        check(ordning.size() == 5 && ordning[0] == före &&
                  ordning[4] == efter,
              "komposit: !focusable ⇒ syskonen står kvar runt omkring");
    }

    // 3. DISABLED ⇒ barnen syns. Ancestor-enabled är en ANNAN
    // fråga (en disabled `Panel` har tabbbara barn) och ingår inte.
    {
        FokusWindow w;
        auto [före, komp, efter] = bygg(w);
        komp->disable();
        const auto ordning = w.focusOrder();
        check(ordning.size() == 5,
              "komposit: disabled ⇒ barnen syns som förut (ancestor-enabled "
              "ingår inte)",
              std::to_string(ordning.size()));
        check(ordning.size() == 5 && ordning[0] == före && ordning[4] == efter,
              "komposit: disabled ⇒ syskonen står kvar");
    }

    // 4. `tabIndex`-SORTERINGEN är ORÖRD — stoppet får inte råka byta
    // ordningsregel. Det förväntade följer det SKRIVNA kontraktet i widget.h: ett
    // satt värde jämförs mot de andras LISTPOSITIONER, så `0` betyder
    // "först bland dem som bär 0" och inte "före allt". Nycklarna blir
    // före=0 (auto, position 0), komposit=1 (auto), efter=0 (satt), och
    // sorteringen är STABIL ⇒ före · efter · komposit.
    {
        FokusWindow w;
        auto [före, komp, efter] = bygg(w);
        efter->tabIndex          = 0;
        const auto ordning       = w.focusOrder();
        check(ordning.size() == 3 && ordning[0] == före &&
                  ordning[1] == efter && ordning[2] == komp,
              "komposit: tabIndex-sorteringen är oförändrad (stabil, "
              "auto-nyckel = listposition)",
              std::to_string(ordning.size()));
    }
}

void fallHandler()
{
    KnappSomKanKlickas b("Tryck");
    int typad = 0, kort = 0;

    // 1. TYPAD lambda — fullformen, kontraktet i regel 6.
    b.onClick = [&](glaze::Widget* s, glaze::ClickArgs&) {
        typad += (s == &b) ? 1 : 0;
    };
    b.klicka();
    check(typad == 1, "handler: typad lambda avfyras med sender",
          std::to_string(typad));

    // 2. PARAMETERLÖS lambda — den korta formen.
    b.onClick = [&] { ++kort; };
    b.klicka();
    check(kort == 1 && typad == 1,
          "handler: parameterlös lambda ersätter och avfyras",
          std::to_string(kort));

    // 3. En befintlig `std::function` i fullformen.
    std::function<void(glaze::Widget*, glaze::ClickArgs&)> fn =
        [&](glaze::Widget*, glaze::ClickArgs&) { ++typad; };
    b.onClick = fn;
    b.klicka();
    check(typad == 2, "handler: std::function tas emot", std::to_string(typad));

    // 4. KOPIERING och FLYTT av wrappern (den är ett vanligt fält och
    // måste bete sig som ett).
    glaze::Handler<glaze::ClickArgs> kopia = b.onClick;
    check(static_cast<bool>(kopia), "handler: kopierbar", "");
    glaze::Handler<glaze::ClickArgs> flyttad = std::move(kopia);
    check(static_cast<bool>(flyttad), "handler: flyttbar", "");

    // 5. NOLLSTÄLLNING — `= nullptr`, aldrig `= {}` (tvetydig).
    b.onClick = nullptr;
    check(!static_cast<bool>(b.onClick), "handler: nollställs med nullptr", "");
    b.klicka();
    check(typad == 2, "handler: nollställd avfyras inte",
          std::to_string(typad));

    // LIVSCYKEL-EVENTEN. De fyra tar den korta formen som varje annat
    // event:
    // `btn.onClick = [this]{…};` och
    // `glaze::AppWindow::onLoad = [this]{…};` ska båda kompilera. Bygget
    // fångar TYPfelet; det här mäter att formerna också BÄR.
    {
        MätWindow w;
        int kort = 0;

        // 1. PARAMETERLÖS `onLoad` — den form som inte gick att skriva.
        w.onLoad = [&] { ++kort; };
        check(static_cast<bool>(w.onLoad),
              "livscykel: onLoad tar parameterlös lambda");

        // …och den AVFYRAS på riktigt. Kräver ett fönster (`showOwned`
        // fyrar efter init), så ett DOLT duger — samma undantag som
        // stilbytet ovan när ingen display finns.
        w.setOffscreen(true);
        w.show();
        if (!w.isOpen()) {
            std::printf("  --   onLoad-avfyrningen hoppas över: inget "
                        "fönster kunde skapas (ingen display)\n");
        } else {
            check(kort == 1, "livscykel: parameterlös onLoad AVFYRAS en gång",
                  std::to_string(kort));
            w.close();
        }
    }
    {
        MätWindow w;

        // 2. FULLFORMAD `onClose` kan sätta `e.cancel` — vetot är
        // event-UTDATA (regel 6) och måste överleva `Handler`-wrappern.
        // Anropas direkt: `flushPendingCloses` är privat i App och en
        // vetande handler i en riktig pump hade hängt testet.
        w.onClose = [](glaze::Widget*, glaze::CloseArgs& e) {
            e.cancel = true;
        };
        glaze::CloseArgs ce;
        w.onClose(&w.root(), ce);
        check(ce.cancel, "livscykel: fullformad onClose sätter e.cancel");

        // 3. PARAMETERLÖS `onClose` KOMPILERAR — den kan naturligtvis
        // inte veta något att veto:a, och det är avsiktligt: vill man
        // stoppa stängningen skriver man fullformen.
        int stängd = 0;
        w.onClose  = [&] { ++stängd; };
        glaze::CloseArgs ce2;
        w.onClose(&w.root(), ce2);
        check(stängd == 1 && !ce2.cancel,
              "livscykel: parameterlös onClose kompilerar och avfyras "
              "(inget veto)");

        // Nollställningen är `= nullptr` här också.
        w.onClose = nullptr;
        check(!static_cast<bool>(w.onClose),
              "livscykel: onClose nollställs med nullptr");
    }

    // Samma former på `onChange` och `onHover`.
    glaze::Checkbox cb;
    int             ändrad = 0;
    cb.onChange = [&] { ++ändrad; };
    check(static_cast<bool>(cb.onChange), "handler: onChange tar kort form", "");
    b.onHover = [](glaze::Widget*, glaze::HoverArgs&) {};
    check(static_cast<bool>(b.onHover), "handler: onHover tar full form", "");
}

// ------------------------------------------------------ SPRÅKBYTETS KROK
//
// `App::languageChangedAll()` är mekanismen bakom
// meddelanderutans omlayout, och den mäts HÄR eftersom den kräver ett
// ÖPPET fönster — `msgbox_gate` är headless och `MessageBoxWindow` är
// dessutom TU-lokal.
//
// TVÅ saker, båda sådana som annars bara syns i drift:
//   1. `onLanguageChanged()` fyras EXAKT en gång per öppet fönster.
//   2. TITELN trycks om — och App gör det SEPARAT, före kroken, så en
//      override som glömmer basanropet ändå inte tappar titeln. Fönstret
//      nedan overridar utan att kalla någon bas, vilket ÄR det fallet.
void fallSprakbyte()
{
    struct SpråkWindow : glaze::AppWindow {
        SpråkWindow() : glaze::AppWindow(glaze::tr(glaze::str::Yes), 200, 120)
        {
        }
        void onLanguageChanged() override { ++krokar; }
        int  krokar = 0;
    };

    glaze::setLanguage("sv");
    SpråkWindow w;
    w.setOffscreen(true);
    w.show();
    if (!w.isOpen()) {
        std::printf("  --   språkbytets krok hoppas över: inget fönster "
                    "kunde skapas (ingen display)\n");
        return;
    }
    check(w.title() == "Ja", "språk: titeln är den UPPLÖSTA texten", w.title());
    check(w.titleToken().id() == glaze::str::Yes,
          "språk: titleToken bär ID:t");
    check(w.krokar == 0, "språk: kroken har inte fyrats ännu");

    glaze::setLanguage("fi");
    check(w.krokar == 1, "språk: onLanguageChanged fyras EXAKT en gång",
          std::to_string(w.krokar));
    check(w.title() == "Kyllä", "språk: titeln följde språkbytet", w.title());

    // Ett byte till SAMMA språk är en no-op — regel 1: inget ändrades.
    glaze::setLanguage("fi");
    check(w.krokar == 1, "språk: samma språk igen ⇒ ingen ny runda",
          std::to_string(w.krokar));

    w.close();
    // …och ett byte EFTER stängning når inte ett rivet fönster.
    glaze::setLanguage("sv");
    check(w.krokar == 1, "språk: ett STÄNGT fönster får ingen krok",
          std::to_string(w.krokar));
}

// ------------------------------------- KÖAD STÄNGNING + RIVNING
//
// Ett fönster som dör med sin `close()` köad (`~AppWindow` → `executeClose`)
// får inte lämna pekaren kvar i `App::pendingClose_`: nästa pump-svans
// skulle läsa ett lik — och stänga FEL fönster när ett nytt hamnat på
// samma adress. Mönstret är vardagligt: ett fönster på stacken som stängs
// i slutet av sitt block, precis som flera fall ovan gör.
//
// Grinden tvingar fram samma adress med placement new i en buffert, och
// avgör VEM som stängde det nya fönstret med en postkedja över två pumpvarv:
// varv 1 postar varv 2, och varv 2 är det enda som anropar `close()`. Stänger
// den kvarlämnade posten fönstret redan i varv 1 har varv 2 aldrig körts.
//
// INTE mätt: samma kvarlämning genom `closePopupsNow` (popup-kön), och en
// rivning som sker mitt i en pågående `flushPendingCloses` — båda går genom
// samma `executeClose`, men ingen av dem framkallas här.
void fallKöadStängningDör()
{
    alignas(MätWindow) unsigned char minne[sizeof(MätWindow)];

    auto* gammal = new (minne) MätWindow;
    gammal->setOffscreen(true);
    gammal->show();
    if (!gammal->isOpen()) {
        gammal->~MätWindow();
        std::printf("  --   köad stängning + rivning hoppas över: inget "
                    "fönster kunde skapas (ingen display)\n");
        return;
    }
    gammal->close();        // köad — verkställs först i pump-svansen …
    gammal->~MätWindow();   // … men objektet dör innan dess

    auto* ny = new (minne) MätWindow;   // SAMMA adress, per konstruktion
    bool varv2            = false;
    bool stängdEfterVarv2 = false;
    ny->onClose = [&] { stängdEfterVarv2 = varv2; };
    ny->setOffscreen(true);
    glaze::app().post([&] {
        glaze::app().post([&] {
            varv2 = true;
            ny->close();
        });
    });
    ny->run();
    check(stängdEfterVarv2,
          "köad stängning: ett fönster som dör med close() köad stänger "
          "INTE nästa fönster på samma adress");
    ny->~MätWindow();
}

// ---- R. STÄNGNINGENS SKÄL — `CloseArgs::reason`
// genom de riktiga vägarna, i DOLDA fönster (`setOffscreen`) med en pump:
//   R1 `close()` ⇒ UserClosing
//   R2 ett syntetiskt `WindowCloseRequested` genom fönstrets ingång
//      (`handleEvent` — krysset, Alt+F4) ⇒ UserClosing
//   R3 (Windows) appavslutet — `PostQuitMessage`, som backenden gör till
//      `EventType::Quit` — ⇒ roten AppQuit och det ÄGDA fönstret OwnerClosing,
//      barnet frågat FÖRE roten
//   R4 (Windows) fönstrets egen stängbegäran (WM_CLOSE) och appavslutet i
//      SAMMA pumpvarv ⇒ AppQuit (uppgraderingen)
//   R5 ett veto lämnar fönstret öppet, och nästa `close()` är UserClosing
//      igen — också efter ett vetat appavslut (Windows)
// Sabotagen: `confirmClose` ger barnen förälderns skäl (R3), Quit genom
// `close()` (R3: UserClosing), ingen uppgradering i `requestClose` (R4), och
// `requestClose` som bara sätter skälet vid en uppgradering (R5). Att vetot
// NOLLAR skälet syns inte utifrån — nästa begäran skriver det — och mäts inte.
// Mäter INTE: macOS Cmd+Q och appmenyns Avsluta, och Linux — ingen portabel
// väg in i `EventType::Quit` från en grind (raden i doc/os/macos/TESTS.md) —
// och utloggningen (NSTerminateCancel och WM_ENDSESSION, som
// inte når appen som `Quit`).

const char* skälNamn(glaze::CloseReason r)
{
    switch (r) {
    case glaze::CloseReason::UserClosing:  return "UserClosing";
    case glaze::CloseReason::AppQuit:      return "AppQuit";
    case glaze::CloseReason::OwnerClosing: return "OwnerClosing";
    }
    return "?";
}

std::string fog(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : " ") + x;
    return "[" + s + "]";
}

class SkälWindow : public glaze::AppWindow {
public:
    using glaze::AppWindow::handleEvent;
    SkälWindow(const std::string& titel, const char* id,
               std::vector<std::string>& logg)
        : glaze::AppWindow(titel, 300, 200, gfx::Chrome::Custom)
    {
        onClose = [this, id, &logg](glaze::Widget*, glaze::CloseArgs& e) {
            logg.push_back(std::string(id) + ":" + skälNamn(e.reason));
            e.cancel = veto;
        };
    }
    bool veto = false;
};

std::string skälTitel(const char* del)
{
#ifdef GLAZE_BACKEND_D3D11
    return std::string("window_gate-") + del + "-" +
           std::to_string(GetCurrentProcessId());
#else
    return std::string("window_gate-") + del;
#endif
}

void fallStängningensSkäl()
{
    std::printf("\n=== R. stängningens skäl ===\n");
    std::vector<std::string> logg;

    // R1 — close().
    {
        SkälWindow w(skälTitel("R1"), "w", logg);
        w.setOffscreen(true);
        glaze::app().post([&] { w.close(); });
        logg.clear();
        w.run();
        if (logg.empty())
            std::printf("  --   R1 hoppas över: inget fönster kunde skapas\n");
        else
            check(logg == std::vector<std::string>{"w:UserClosing"},
                  "R1: close() ⇒ UserClosing", fog(logg));
    }

    // R2 — fönstrets stängbegäran genom ingången (krysset, Alt+F4).
    {
        SkälWindow w(skälTitel("R2"), "w", logg);
        w.setOffscreen(true);
        glaze::app().post([&] {
            gfx::Event e;
            e.type     = gfx::EventType::WindowCloseRequested;
            e.windowId = w.window().id();
            w.handleEvent(e);
        });
        logg.clear();
        w.run();
        if (logg.empty())
            std::printf("  --   R2 hoppas över: inget fönster kunde skapas\n");
        else
            check(logg == std::vector<std::string>{"w:UserClosing"},
                  "R2: WindowCloseRequested (krysset, Alt+F4) ⇒ UserClosing",
                  fog(logg));
    }

#ifdef GLAZE_BACKEND_D3D11
    // R3 — appavslutet: roten AppQuit, det ägda OwnerClosing, barnet först.
    {
        SkälWindow rot(skälTitel("R3-rot"), "rot", logg);
        SkälWindow barn(skälTitel("R3-barn"), "barn", logg);
        rot.setOffscreen(true);
        barn.setOffscreen(true);
        glaze::app().post([&] {
            barn.show(rot);
            PostQuitMessage(0);
        });
        logg.clear();
        rot.run();
        if (logg.empty())
            std::printf("  --   R3 hoppas över: inget fönster kunde skapas\n");
        else
            check(logg == std::vector<std::string>{"barn:OwnerClosing",
                                                   "rot:AppQuit"},
                  "R3: appavslutet ⇒ det ägda OwnerClosing FÖRE roten AppQuit",
                  fog(logg));
    }

    // R4 — WM_CLOSE och appavslutet i SAMMA pumpvarv: WM_QUIT hämtas först när
    // kön är tom, så stängbegäran är redan köad när avslutet kommer — och
    // uppgraderar skälet.
    {
        const std::string titel = skälTitel("R4");
        SkälWindow        w(titel, "w", logg);
        w.setOffscreen(true);
        glaze::app().post([&] {
            const std::wstring t(titel.begin(), titel.end());   // ASCII
            if (const HWND h = FindWindowW(nullptr, t.c_str()))
                PostMessageW(h, WM_CLOSE, 0, 0);
            PostQuitMessage(0);
        });
        logg.clear();
        w.run();
        if (logg.empty())
            std::printf("  --   R4 hoppas över: inget fönster kunde skapas\n");
        else
            check(logg == std::vector<std::string>{"w:AppQuit"},
                  "R4: krysset och appavslutet i samma varv ⇒ AppQuit "
                  "(uppgraderingen)",
                  fog(logg));
    }
#else
    std::printf("  ..   R3/R4: appavslutet (EventType::Quit) går bara att "
                "framkalla från en grind på Windows (PostQuitMessage)\n");
#endif

    // R5 — ett veto, och nästa stängning börjar om som fönstrets egen.
    {
        SkälWindow w(skälTitel("R5"), "w", logg);
        w.setOffscreen(true);
        w.veto = true;
        glaze::app().post([&] {
#ifdef GLAZE_BACKEND_D3D11
            PostQuitMessage(0);   // ett appavslut som vetas …
#else
            w.close();            // … eller fönstrets egen stängning
#endif
            glaze::app().post([&] {
                glaze::app().post([&] {
                    w.veto = false;
                    w.close();
                });
            });
        });
        logg.clear();
        w.run();
#ifdef GLAZE_BACKEND_D3D11
        const std::vector<std::string> facit{"w:AppQuit", "w:UserClosing"};
#else
        const std::vector<std::string> facit{"w:UserClosing", "w:UserClosing"};
#endif
        if (logg.empty())
            std::printf("  --   R5 hoppas över: inget fönster kunde skapas\n");
        else
            check(logg == facit,
                  "R5: ett veto lämnar fönstret öppet, och nästa close() är "
                  "UserClosing",
                  fog(logg));
    }
}

} // namespace

int main(int argc, char** argv)
{
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    std::printf("=== window_gate — innehållsytan ===\n");

    const float barH    = glaze::defaultStyle(glaze::UI::Slate).barHeight;
    const float statusH = glaze::defaultStyle(glaze::UI::Slate).statusBarH;

    // ---- 1. Ren uträkning, INGET fönster (portabelt).
    {
        MätWindow w;
        w.setContentSize(400, 300);
        check(w.contentSize().w == 400.0f && w.contentSize().h == 300.0f,
              "innehållsytan är den appen bad om", sz(w.contentSize()));
        check(w.clientSize().h == 300.0f + barH,
              "klientytan = innehåll + namnlist", sz(w.clientSize()));

        // STATUSRADEN: `show()` gör innehållsytan lägre — och en app som
        // bett om en innehållsstorlek ska då få fönstret omräknat i
        // stället för att tappa statusradens höjd. Det är det tredje
        // anropsstället (Widget::notifyContentInsets).
        w.statusBar.show();
        check(w.contentSize().h == 300.0f,
              "statusrad PÅ: innehållsytan är oförändrad",
              sz(w.contentSize()));
        check(w.clientSize().h == 300.0f + barH + statusH,
              "statusrad PÅ: klientytan växte med statusradens höjd",
              sz(w.clientSize()));

        w.statusBar.hide();
        check(w.clientSize().h == 300.0f + barH,
              "statusrad AV: klientytan krympte tillbaka",
              sz(w.clientSize()));

        // De två sättarna utesluter varandra: den som sätts SIST gäller.
        w.setClientSize(500, 500);
        check(w.clientSize().h == 500.0f,
              "setClientSize efter setContentSize: rå klientstorlek vinner",
              sz(w.clientSize()));
        w.statusBar.show();
        check(w.clientSize().h == 500.0f,
              "…och följer INTE med chromet längre (ingen begäran kvar)",
              sz(w.clientSize()));
    }

    // ---- 2. STILBYTET — kräver ett öppet (men dolt) fönster.
    {
        MätWindow w;
        w.setContentSize(400, 300);
        w.setOffscreen(true);
        w.show();
        if (!w.isOpen()) {
            std::printf("  --   stilbytet hoppas över: inget fönster kunde "
                        "skapas (ingen display)\n");
        } else {
            check(w.clientSize().h == 300.0f + barH,
                  "efter show(): klientytan löstes ur chromet",
                  sz(w.clientSize()));

            // En stil med ANNAN barHeight. `Style` är ett värde, så en
            // egen tabell är en kopia med ett fält ändrat — precis så en
            // app skulle göra det.
            glaze::Style s = glaze::defaultStyle(glaze::UI::Slate);
            s.barHeight    = barH + 16.0f;
            w.setStyle(s);
            check(w.clientSize().h == 300.0f + barH + 16.0f,
                  "stilbyte: klientytan räknades om ur NYA barHeight",
                  sz(w.clientSize()));
            check(w.contentSize().h == 300.0f,
                  "stilbyte: innehållsytan är fortfarande den appen bad om",
                  sz(w.contentSize()));

            // …och tillbaka, så nästa körning inte ärver stilen (den är
            // APP-GLOBAL).
            w.setStyle(glaze::defaultStyle(glaze::UI::Slate));
            check(w.clientSize().h == 300.0f + barH,
                  "stilbyte tillbaka: klientytan följde med",
                  sz(w.clientSize()));
            w.close();
        }
    }

    // ---- 3. STORLEKSGRÄNSERNA. Glaze-nivån går att mäta
    // headless: `isFixedSize()` är EN sanning som både backendens
    // stilmaskning och `pushHitRegions` läser, och det är just den
    // delningen som gör att chromet aldrig kan visa en resize-pekare på
    // ett fönster OS:et vägrar resiza.
    {
        MätWindow w;
        check(!w.isFixedSize(), "gränser: obegränsat är inte fast");

        w.setMinimumSize(400, 300);
        check(w.minimumSize().w == 400.0f && w.minimumSize().h == 300.0f,
              "gränser: minimum lagras", sz(w.minimumSize()));
        check(!w.isFixedSize(),
              "gränser: bara minimum ⇒ fortfarande ändringsbart");

        w.setMaximumSize(400, 300);
        check(w.isFixedSize(),
              "gränser: min == max på BÅDA axlarna ⇒ fast fönster");

        // En axel låst räcker INTE — fönstret går fortfarande att dra i
        // den andra, och då ska resize-greppen finnas kvar.
        w.setMaximumSize(400, 900);
        check(!w.isFixedSize(),
              "gränser: en låst axel ⇒ inte fast");

        // 0 = obegränsat, per axel.
        w.setMinimumSize(0, 0);
        w.setMaximumSize(0, 0);
        check(!w.isFixedSize(), "gränser: 0 är obegränsat, inte 'lås på 0'");
    }

    // ---- 4. KNAPPARNA följer gränserna. Ett LÅST
    // fönster ska inte visa maximera-knappen: `isFixedSize()`
    // läses både av resize-greppen och av `applyCaptionBoxes`, annars
    // erbjuder chromet något OS:et vägrar. Namnlisten är en widget och byggs i
    // konstruktorn, alltså mätbart utan fönster.
    {
        MätWindow w;
        glaze::TopBar* tb = w.topBar();
        check(tb && tb->maxButton(), "chrome: namnlisten har en maxknapp");
        if (tb && tb->maxButton()) {
            check(tb->maxButton()->isVisible(),
                  "chrome: obegränsat fönster visar maximera");

            w.setMinimumSize(400, 300);
            w.setMaximumSize(400, 300);
            check(!tb->maxButton()->isVisible(),
                  "chrome: LÅST fönster (min == max) döljer maximera");

            // Lås upp igen — knappen ska tillbaka.
            w.setMaximumSize(0, 0);
            check(tb->maxButton()->isVisible(),
                  "chrome: upplåst fönster visar maximera igen");

            // Ingen regression: setMaximizeBox(false) väger fortfarande
            // tyngst på ett olåst fönster.
            w.setMaximizeBox(false);
            check(!tb->maxButton()->isVisible(),
                  "chrome: setMaximizeBox(false) döljer även utan lås");
            w.setMaximizeBox(true);
        }
    }

    // ---- 5. ALLTID ÖVERST. Grinden är ÄRLIGT avgränsad:
    // lagringen FÖRE `show()` mäts utan display, och vidarebefordran +
    // gettern mäts på den backend testet RÅKAR köra (miljön väljer den —
    // testet injicerar ingen). Wayland-degraderingen (alltid false +
    // engångsloggen) går alltså INTE att grinda här och verifieras
    // manuellt; därför står svaret nedan som en utskriven rad och inte som
    // ett `check` när fönstret är öppet.
    {
        MätWindow w;
        check(!w.topmost(), "topmost: default är av");

        w.setTopmost(true);
        check(w.topmost(),
              "topmost: begäran före show() lagras och LÄSES tillbaka");
        w.setTopmost(false);
        check(!w.topmost(), "topmost: kan stängas av igen före show()");

        // Efter show() ska gettern läsa OS-FÖNSTRET och inte begäran —
        // det är hela poängen med att degraderingen ska gå att upptäcka.
        w.setTopmost(true);
        w.setOffscreen(true);
        w.show();
        if (!w.isOpen()) {
            std::printf("  --   topmost efter show() hoppas över: inget "
                        "fönster kunde skapas (ingen display)\n");
        } else {
            std::printf("  --   topmost efter show() på den backend miljön "
                        "valde: %s (Wayland svarar alltid false — "
                        "dokumenterad degradering)\n",
                        w.topmost() ? "true" : "false");
            // Det som ÄR mätbart överallt: gettern går genom fönstret
            // utan att krascha, och `setTopmost` efter show når fram utan
            // att röra den lagrade begäran.
            w.setTopmost(false);
            check(!w.topmost(),
                  "topmost: avstängning efter show() svarar false på ALLA "
                  "backends");
            w.close();
        }
    }

    fallFokusOrder();
    fallHandler();
    fallSprakbyte();
    fallKöadStängningDör();
    fallStängningensSkäl();

    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "WINDOW_GATE GRÖN" : "WINDOW_GATE FÄLLD",
                g_cases, g_fails);
    return g_fails == 0 ? 0 : 1;
}
