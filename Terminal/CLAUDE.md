# Terminal — sidopaketet för en inbyggd terminal (lib glaze_terminal + glaze_terminal_ui)

**UTANFÖR ramverket.** `glaze` länkar aldrig paketet — pty, processer och
VT-tolkning är inte GUI, och en app som inte behöver terminalen får den inte
med sig. Två target, Threads-formen:

- **`glaze_terminal`** — KÄRNAN. Ren std + OS (kernel32 på Windows, libc på
  POSIX — ingen forkpty, alltså ingen `-lutil`). Bygger fristående
  (`cmake -S Terminal -B b`), och `tests/terminal_gate` länkar bara den.
- **`glaze_terminal_ui`** — WIDGETEN `glaze::Terminal : Panel`, tangent-
  kodningen och färgschemana. Skapas bara när `glaze` finns. INTERFACE-target
  som i statiskt läge länkar `glaze_terminal_ui_impl` WHOLE_ARCHIVE (widgetens
  `GLAZE_REGISTER_WIDGET("terminal", …)` hade annars strukits) och i delat läge
  länkar libglaze_terminal_ui plus ankaret `src/ui/terminal_anchor.cpp`.

`GLAZE_TERMINAL` (rot-CMakeLists, förval ON) stänger av paketet. Verktygen
(glaze-render, GlazeBuilder, `builder_preview` med `apply_terminal.cpp`) länkar
widgeten när targetet finns — utan det blir typen en stand-in. Demon KRÄVER
paketet (dockningsfönstrets flik).

**DELAT LÄGE** (`GLAZE_SHARED`): `glaze_terminal` och `glaze_terminal_ui_impl`
blir libglaze_terminal och libglaze_terminal_ui i SAMMA växel som ramverket.
Inget föränderligt statiskt tillstånd i en header (dll_gate läser `Terminal/`
och båda arkiven); tabellerna (teckenbredden, schemana, linjegrafiken) är
`constexpr`.

## Så använder en app det

```cmake
target_link_libraries(minapp PRIVATE glaze glaze_terminal_ui)
```

```cpp
#include <glaze_terminal/terminal.h>   // UTANFÖR GlazeBuilder-regionen

void StartWindow::onLoad(glaze::Widget*, glaze::EventArgs&)
{
    term.start();                      // tomt program = OS:ets skal
}
```

I `.glazeui`: setter-fälten (`setProgram`, `setArguments`,
`setWorkingDirectory`, `setColorMode`, `setColorScheme`, `setScrollback`,
`setAltAsMeta`, `setTextSize`), anropen `setEnvironmentVariable(namn, värde)`,
`unsetEnvironmentVariable(namn)`, `setSchemeColor(variant, roll, färg)`,
`clearSchemeColors()` och kopplingarna `onExit`/`onTitleChange` (båda
formerna). Previewn visar en TOM terminal och startar aldrig en process;
`start()`, `write(…)`, `kill()` … är opaka där.

## Filerna

Kärnan (`include/glaze_terminal/`, namnrymden `glaze::term`, PUBLIK — en app
kan köra ett program headless och tolka dess färgutskrift):

- **`command_line.h`** + `src/command_line.cpp` — `splitCommandLine`/
  `quoteArgument`, CommandLineToArgvW:s regler (2008-formen). Windows får
  argumentsträngen ORDAGRANT; POSIX delar den här, som .NET på Unix.
- **`text_width.h`** + `src/text_width.cpp` + **`src/text_width_table.h`** —
  `cellWidth`/`isPictographic` ur Unicode 15.1 (EAW W/F + Emoji_Presentation
  = 2; Mn/Me/Cf + U+1160–U+11FF = 0). Tabellen är GENERERAD EN gång av
  **`tools/width_table.cmake`** (`cmake -DUCD=… -DOUT=… -P`, för hand, mot
  EastAsianWidth.txt, emoji-data.txt och DerivedGeneralCategory.txt) och
  committad; UCD-filerna committas inte.
- **`parser.h`** + `src/parser.cpp` — Williams DEC ANSI-maskin, UTF-8 avkodat
  före (U+FFFD för ogiltigt), `:`-underparametrar, BEL eller ST avslutar OSC;
  DCS/SOS/PM/APC sväljs. Ingen allokering per tecken.
- **`screen.h`** + `src/screen.cpp` — skärmmodellen: primär och alternativ
  buffert, historiken (deque, taket `setScrollback`, `linesDropped()` för
  stabila radindex), cellerna med GRAFEMKLUSTER (en kodpunkt inline, resten i
  radens överflöd; VS16 breddar, ZWJ/hudton/flagga fogas ihop), lägena, de
  smutsiga raderna och svaren (DSR, DA1/DA2, DECRQM, OSC 10/11/12-frågorna,
  kittyprotokollets fråga). Ingen omflödning vid storleksändring.
- **`pty.h`** + `src/pty_impl.h` (intern: miljöns sammanslagning) +
  `src/pty_win.cpp` / `src/pty_posix.cpp` — OS-lagret, blockerande, utan
  trådar. `StartInfo`, `EnvChange`, `Pty`.
- **`session.h`** + `src/session.cpp` — `Session`: Pty + läsar-, skrivar- och
  väntartråden, inkorgen med taket, utkorgen med taket, `take` med
  `kTakeBudget`, rivningsordningen.

Widgeten (`glaze::Terminal`, `include/glaze_terminal/terminal.h` +
`scheme.h`; källorna i `src/ui/`):

- **`terminal.cpp`** — widgeten och registreringen: leveransen (`drain` i
  pump-svansen), ritningen, tangenterna, markeringen, rullningen och
  musrapporteringen. Inga tal och inga färger (regel 3). `start()` lägger
  `gfx::childEnvironmentRemovals()` FÖRE appens miljöändringar: skalet ärver
  inte grafikkortsvalet GlazeUI satte på Linux (REQ9_GRAFIKKORTET E3) — kärnan
  (`pty_posix.cpp`) länkar aldrig gfx, så borttagningen går genom widgeten.
- **`keys.h`/`keys.cpp`** (INTERNA, grindade headless) — `encodeKey`,
  `encodePaste`, `clipboardKey`. Tabellen står i keys.h.
- **`scheme.cpp`** — schemafilen: härledningen ur temat (`panel`, `text`,
  `accent`/`onAccent`, `accentSurface`; svart/vitt som blandningar, kulörerna
  ur `color::hueDegrees`) och Solarized/One/Tango som data. Talen står HÄR
  med skäl (inte i widgetfilen).
- **`terminal_anchor.cpp`** — INGEN del av widgeten: delade lägets ankare.

## Kontraktet

- **Trådarna.** `Session::onOutput`/`onExit` anropas i ARBETARTRÅDAR och får
  bara posta. Widgeten fångar livstecknet (`lifeToken()`) och en ny
  64-bitars GENERATION per `start()` på UI-tråden; en post för en gammal
  session eller till en död widget är en no-op. Händelserna fyras med
  `postEvent` + `fireEvent` (en handler får riva widgeten).
- **Leveransen.** EN `onOutput` åt gången (atomisk flagga); ägaren hämtar
  högst `kTakeBudget` (256 KiB) per varv och postar sig själv för resten.
  Invalideringen är per smutsig rad, hela ytan efter en rullning. Medan 2026
  (synkroniserad utskrift) är satt invalideras ingenting; släppet eller
  `Screen::kSyncTimeoutMs` (en `glaze::Timer`, startad bara när läget sätts)
  ritar.
- **Taket.** Inkorgen överstiger aldrig `kInboxMax` (1 MiB): läsaren väntar
  tills den tömts till hälften, så ett program som skriver obegränsat
  blockeras i sin egen skrivning. Utkorgen kapas vid `kOutboxMax` (8 MiB),
  med en notis.
- **Rivningen** (`kill()`, destruktorn) — ordningen ÄR kontraktet: (1)
  `closing` sätts och läsaren väcks — den SLÄNGER sedan i stället för att
  vänta på taket; (2) processkedjan dödas (jobbobjektet på Windows; SIGHUP
  till GRUPPEN och SIGKILL efter `kGraceMs` om gruppen lever, `Pty::alive`,
  också efter att huvudprocessen skördats); (3) `ClosePseudoConsole` på den
  ÄGANDE tråden medan läsaren tömmer — en påbörjad stängning väntas ut
  (öppen → påbörjad → avslutad); (4) väckaren och CancelSynchronousIo, först
  när stängningen RETURNERAT; (5) join ×3. Vid ett naturligt slut stänger
  VÄNTARTRÅDEN pseudokonsolen (utdatan tar annars aldrig slut på Windows).
- **Slutet**: `onExit` först när processen avslutats OCH läsaren nått EOF
  (`finishExit`); widgeten tömmer inkorgen med budgeten innan den fyrar
  händelsen, och händelserna bär sessionens generation. Titelns händelse
  väntar på 2026:s släpp.
- **Jobbobjektet är ett krav** på Windows: går det inte att skapa eller
  tilldela misslyckas `start()` och den suspenderade processen termineras.
- **Svaren** till programmet (DSR, DA, OSC-frågorna) är skärmens utdata;
  widgeten skriver dem till pty:n. `feedOutput` (utdata utan program) går
  samma väg.
- **Fokushändelserna** (1004) följer widgetfokus OCH fönstrets aktivering —
  caret-kroken anropas vid varje aktiveringsbyte och räcker som väg.

## Fallgropar

- **`fork()` i en flertrådad process** (pty_posix.cpp): allt förbereds före
  fork, och barnet gör bara async-signal-säkra anrop (setsid → open →
  `TIOCSCTTY` → dup2 → signalerna → chdir → execve). Ingen getenv, malloc
  eller PATH-sökning i barnet. Exec-felet kommer tillbaka genom ett
  CLOEXEC-rör: EOF = exec lyckades, och först då vet föräldern att gruppen
  finns. `ioctl(TIOCSCTTY)` är PRAXIS på Linux och macOS, ingen POSIX-garanti.
- **Storleken sätts på SLAVEN i barnet** (`pty_posix.cpp`, uppmätt 2026-10-21):
  på macOS fastnar `TIOCSWINSZ` på mastern inte förrän slaven är öppen
  (läses tillbaka som 0 0), och en stängd slav tappar den igen — därför en
  andra `ioctl` på slaven direkt efter `open` i barnet. terminal_gate S1.
- **macOS saknar `pipe2`**: röret görs CLOEXEC med fcntl efteråt, och en fork
  i en annan tråd emellan kan ärva änden — skriven gräns. `environ` nås med
  `_NSGetEnviron()` (en dylib ser den inte).
- **ConPTY:s stängning** väntar (före Windows 11 24H2) tills utdatan tömts —
  anropas den på läsartråden, eller medan läsaren väntar på taket, låser den
  sig. Därav `closing` FÖRST.
- **`PSEUDOCONSOLE_INHERIT_CURSOR` används inte**: ConPTY frågar då efter
  markören vid starten, och en värd som inte svarat i tid kan låsa
  stängningen.
- **MinGW deklarerar ConPTY bara med `NTDDI_VERSION >= NTDDI_WIN10_RS5`**:
  funktionerna slås upp med GetProcAddress, och
  `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` står som värde i pty_win.cpp.
- **`STARTF_USESTDHANDLES` med tomma handtag**: annars ärver ett konsolprogram
  förälderns omdirigerade standardhandtag (glaze-render, en grind under
  ctest) i stället för pseudokonsolens.
- **SIGCHLD = SIG_IGN hos appen** ger `waitpid` ECHILD: slutkoden är −1 och en
  notis loggas.
- **WHOLE_ARCHIVE och ankaret** — samma fälla som Threads: länka alltid
  `glaze_terminal_ui` DIREKT till varje program och plugin (rot-CLAUDE.md,
  DELAT LÄGE (5)). Länkas `_impl` direkt försvinner "terminal" ur registret.
- **Breda tecken** där ConPTY och vår tabell räknar olika ger förskjutna rader
  på Windows (B13, en känd ConPTY-egenhet). En ZWJ-sekvens har rätt cellbredd
  men ritas glyf för glyf (ingen klusterformning — en 💡), och ett kluster har
  högst `kClusterMax` (32) kodpunkter.
- **Historiken när ringen är full**: `draw` flyttar en uppåtrullad vy lika
  många rader som föll bort (`linesDropped`), så att den står still.
- **Fokus**: terminalen TAR Tab och Escape. Vägarna ut är Ctrl+Tab, musen och
  appens `onKeyDown` med `handled`.

## Grindarna

- **`tests/terminal_gate`** — kärnan headless (P W C H) och sessionen mot
  `tests/terminal_probe` (S1–S6: storleken, rått läge, taket och den delade
  hämtningen, kill() inom 2 s, miljön).
- **`tests/terminal_ui_gate`** — registret, tangentkodningen, inklistringen,
  kontrasten i alla tjugo paletter × två lägen, och i ett dolt fönster
  routingen mot proben, markeringen, följningen och 2026.
- **`roundtrip_gate`** (blocket "terminal"), **`preview_gate`**
  (`fallTerminal`, matrisen, Panel-oraklet), **`terminal_fixture`**
  (kompilerar `tests/fixtures/terminal.glazeui`), **`dll_gate`** och
  **`shared_gate`** (D13: bara pluginen länkar paketet, och dess ankare
  laddar DLL:en).
