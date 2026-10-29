# Threads — sidopaketet för trådar (lib glaze_threads + glaze_threads_ui)

**UTANFÖR ramverket, innanför v1.0-grinden.** `glaze` länkar aldrig paketet —
ett GUI-bibliotek som drar in en trådmodell blir svårare att konsumera. Två
target:

- **`glaze_threads`** — KÄRNAN. Ren std + systemets trådbibliotek, inga
  externa beroenden på något OS. Bygger fristående (`cmake -S Threads -B b`),
  och `tests/threads_gate` länkar bara den.
- **`glaze_threads_ui`** — UI-BRYGGAN. `glaze::app().post` inslagen i en
  `Context`; det ENDA i paketet som länkar `glaze`, och skapas därför bara när
  `glaze` finns (byggt inifrån GlazeUI-roten). INTERFACE-target som i
  statiskt läge länkar `glaze_threads_ui_impl` WHOLE_ARCHIVE och i delat läge
  länkar libglaze_threads_ui plus ankaret `src/ui_anchor.cpp` — se Fallgropar.

**DELAT LÄGE** (`GLAZE_SHARED`, TODO 73 — `cmake/GlazeShared.cmake`, som
CMakeLists inkluderar också fristående): `glaze_threads` och
`glaze_threads_ui_impl` blir libglaze_threads och libglaze_threads_ui
(DLL/.so/.dylib) i SAMMA växel som ramverket — en delad glaze med statisk
threads hade gett `defaultPool`/`defaultContext` per modul. Allt
processvitt tillstånd (`g_default`, `workerContext`, `defaultPool`,
`uiContext`, självregistreringen, `t_inuti`) ligger i cpp-filer och ska
fortsätta göra det (`tests/dll_gate.cpp`).

## Så använder en app det

```cmake
target_link_libraries(minapp PRIVATE glaze glaze_threads_ui)
```

```cpp
#include <glaze_threads/threads.h>

class StartWindow : public glaze::AppWindow {
    ...
    glaze::threads::Worker bakgrund;          // MEDLEM — dör med fönstret
};

void StartWindow::räknaClicked(glaze::Widget*, glaze::ClickArgs&)
{
    bakgrund.cancel();                        // "senaste vinner" (valfritt)
    bakgrund.run([n = antal.value()](std::stop_token stop) {   // POOLTRÅD
        return räknaUt(n, stop);                               // bara värden in!
    }, [this](glaze::threads::Result<Rapport>& r) {            // UI-TRÅDEN
        status.setText(r ? r->text : r.error());
    });
}
```

Inget mer. Bryggan registrerar sig själv som default när den länkas, så
fortsättningen hamnar i UI-tråden utan att appen nämner en kontext.

## Filerna

- **`include/glaze_threads/result.h`** — `Result<T>` (värde ELLER fel, aldrig
  båda; `Result<void>` default = lyckat), `Error` (std::error_code + text),
  `Code` (Ok/Cancelled/Exception) med egen `std::error_category`. SAMMA typ i
  Networking — därför `std::error_code`: varje paket äger sin kategori.
- **`include/glaze_threads/context.h`** — `Context` (prepare/post/runsInline),
  `workerContext()`, `defaultContext()`, `setDefaultContext()`.
- **`include/glaze_threads/pool.h`** — `Pool` (fast trådantal, FIFO,
  rivningens tre steg) + `defaultPool()`.
- **`include/glaze_threads/worker.h`** — `Worker` (run/cancel/busy/wait/post)
  och dess mallmaskineri (`detail::Run`, utplattningen av `Result`).
- **`include/glaze_threads/threads.h`** — paraplyet för kärnan.
- **`include/glaze_threads/ui.h`** — `uiContext()` (kräver `glaze_threads_ui`)
  och `detail::uiBridgeAnchor()`, ankarets no-op.
- **`src/result.cpp`** — felkategorin. **`src/context.cpp`** — pooltrådens
  kontext + den atomiska defaulten. **`src/pool.cpp`** — poolen och den
  AVSIKTLIGT LÄCKTA `defaultPool()`. **`src/worker.cpp`** — räknarna, låsen,
  leveransen. **`src/ui.cpp`** — bryggan + självregistreringen.
- **`src/ui_anchor.cpp`** — INGEN del av bryggan: en INTERFACE-källa på
  `glaze_threads_ui` i DELAT läge, inkompilerad i varje program och DLL som
  länkar bryggan. Den anropar `detail::uiBridgeAnchor()` vid statisk
  initiering — referensen håller bryggans DLL kvar (PE-länkaren och GNU ld:s
  `--as-needed` stryker annars importen, och registreringen körs aldrig).
  Den registrerar INGENTING själv: `setDefaultContext` är en ovillkorlig
  skrivning och hade skrivit över appens kontext.

## Kontraktet

- **Arbetet fångar bara VÄRDEN** — det körs i en pooltråd och får aldrig röra
  fönstret eller en widget. Fortsättningen (`done`) körs i kontexten och får
  röra allt. Undantag i arbetet blir `Code::Exception` med `what()` i texten.
- **Fortsättningen är OBLIGATORISK** och tar `Result<T>&`. Ett arbete som
  returnerar `Result<U>` PLATTAS UT till `Result<U>` (aldrig `Result<Result<U>>`)
  — det är det som gör nätverksanrop raka i en Worker.
- **Avbrutet levererar ALDRIG.** `cancel()` stoppar alla körningar som
  startats (deras `std::stop_token`), nollar `busy()` direkt, och lovar att
  varken deras fortsättningar eller mellanbesked körs — vattentätt genom
  GENERATIONEN (kontrollerad under låset vid leveransen), inte bara genom
  token. Skälet: ett "avbrutet" som droppar in efter en ny körning hade
  skrivit över den.
- **Fortsättningen körs aldrig efter ägaren.** Workerns dtor sätter
  `alive = false` och väntar in KÖRANDE arbete (jthread-semantik); köade
  körningar startar aldrig. En leverans som redan ligger i UI-kön ser
  `alive == false` och släpps. Därför är `[this]` i `done` säkert.
- **Flera körningar samtidigt** är normalt; `busy()` = något oavbrutet som
  inte levererats. `wait()` blockerar tills allt lämnat pooltrådarna — för
  verktyg och grindar, aldrig på UI-tråden.
- **Poolen**: kapaciteten = trådantalet (0 = hardware_concurrency, minst 2),
  FIFO. Vid rivning körs varje köat jobb ändå EXAKT en gång med en stoppad
  token — ägaren ska alltid få veta att jobbet tog slut. Ett jobb som kastar
  är `std::terminate` (högljutt, aldrig tyst). En egen tråd för en långlivad
  loop = en egen `Pool{1}` + en Worker på den.
- **Kontexten**: `prepare()` körs på den STARTANDE tråden — UI-bryggan skapar
  där `glaze::App` om den inte finns, så att App aldrig föds i en pooltråd
  (D3D11 fångar UI-trådens id i `gfx::init`). `post()` får aldrig vänta på
  att fn körs; en kontext som kör fn själv svarar `runsInline()`.

## Fallgropar

- **WHOLE_ARCHIVE är bärande för bryggan i statiskt läge**: självregistreringen
  är ett statiskt objekt i ui.cpp som ingen app refererar — länkas `_impl`
  direkt stryker länkaren TU:n och svaren hamnar tyst i pooltråden. Länka
  alltid `glaze_threads_ui` (samma fälla som `glaze_impl`). I delat läge är
  ANKARET bärande på samma sätt — och det följer bara med till ett mål som
  länkar `glaze_threads_ui` DIREKT: ett statiskt mellanbibliotek som länkar
  bryggan PRIVATE bär inga INTERFACE-källor (`$<LINK_ONLY>`), och ankarobjektet
  i det statiska biblioteket stryks. Länka bryggan till varje program och
  DLL som ska ha den.
- **`defaultPool()` rivs ALDRIG** (`*new Pool`, i båda lägena): i delat läge
  hade `~Pool` körts i libglaze_threads `DLL_PROCESS_DETACH`, efter att
  `ExitProcess` dödat trådarna — dtorn tar `m_` och begär stopp före join, och
  en tråd som dödades med låset hänger processen. Ägarna (varje Worker) har
  dött före `main` returnerat, köade fristående jobb överges, OS:et tar
  trådarna.
- **En DLL som använt paketet avlastas aldrig** (DELAT_BIBLIOTEK B10): Worker-
  fortsättningar, en egen `Context` och postade funktioner pekar in i dess
  kod.
- **En Worker får inte vara global/statisk.** Den måste dö före `glaze::App`
  (app.h: postande trådar klara innan App rivs) — det gör den bara som medlem
  eller lokal. Samma för en egen `Pool`: den ska leva längre än varje Worker
  som använder den (deklarera poolen FÖRE workern).
- **`prepare()` före `defaultPool()`** i `submit`: App skapas på den startande
  tråden (UI-tråden) innan poolen finns. Poolen rivs aldrig, så vid
  processlut väntar dess trådar i kön — ingen av dem rör App.
- **`request_stop` körs alltid UTANFÖR Workerns lås** (`cancel`, dtorn):
  stop-callbacks — även de ett arbete själv registrerat, t.ex. nätverkets
  väckare — körs synkront i den tråden, och en som rör Workern hade låst sig.
- **`wait()` och dtorn inifrån det egna arbetet** väntar på allt UTOM sig
  själva (trådlokal `t_inuti`) — annars ett tyst dödläge.
- **Ett mellanbesked ska skickas med arbetets EGEN token** (`post(stop, …)`):
  kopplingen till generationen lever lika länge som körningen, så ett besked
  i UI-kön släpps om `cancel()` hinner före.
- **P0660 (`std::stop_token`/`std::jthread`) i libc++** finns utan
  `-fexperimental-library` först från LLVM 20 — paketet är det första i repot
  som kräver den. CMakeLists provkompilerar (`GLAZE_THREADS_STOP_TOKEN`),
  faller tillbaka på flaggan (PUBLIC, för headrarna använder typerna) och
  stoppar annars med ett begripligt FATAL_ERROR. Fallbacken är bara körd som
  sabotage (provet bröts med flit) — på Apple clang är den blind.
