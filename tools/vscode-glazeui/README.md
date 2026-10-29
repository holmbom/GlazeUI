# GlazeUI för VS Code

Förhandsvisar **GlazeUI-fönsterfiler** (`.glazeui`) som renderade
WYSIWYG-bilder — bilden ritas av en buntad **glaze-render**-binär med exakt
samma renderingsbackend som appen kör med på din maskin (Mac = Metal,
Windows = D3D11, Linux = Wayland/X11), i native skala och med fönster-chrome.

## Tre lägen

| Läge | Hur |
|------|-----|
| **Bild** (default) | Öppna en `.glazeui` — den renderas som en bild av fönstret. |
| **Text** | Knappen *Öppna som text* (eller högerklick → *Öppna med… → Texteditor*). Filen är genererad C++ och får C++-highlighting. |
| **Text + preview** | I text-läget: knappen *Förhandsvisning vid sidan* — en live-preview i kolumnen bredvid som om-renderas när du sparar. |

Previewn om-renderas också när **GlazeBuilder** (eller något annat utanför
VS Code) sparar tripletten — `.h`, `.glazeui` och närmaste `assets.bin`
bevakas.

## Fönstrets designtema

Bilden visar fönstret i **sitt eget designtema**. Bär `.glazeui` en rad

```
// glazeui-design: ui=Slate theme=slate(Teal) style=0
```

ritas previewn med den ritningen, paletten och de måtten — annars med
GlazeUI:s defaults. Raden är en **kommentar**: den kompileras inte och
påverkar inte appen när den körs, den talar bara med verktygen. Ljust/mörkt
står inte i raden med flit — det följer din egen OS-inställning, precis som
appen gör i drift.

Raden skrivs för hand tills GlazeBuilder får en väljare för den.
Extensionen behöver ingen inställning: `glaze-render` läser taggen själv.

## Öppna i GlazeBuilder

Knappen *Öppna i GlazeBuilder* startar GlazeBuilder med filen. En generisk
GlazeBuilder **buntas med** i VSIX:en, så knappen fungerar direkt utan att du
byggt något. Vill du ha projektets egna widgets riktigt ritade i editorn
väljs i stället, om det finns: (1) inställningen `glazeui.builderPath`, annars
(2) projektets eget bygge enligt konventionen `build/apps/GlazeBuilder/…`
(uppåt från filen och i arbetsytans rötter). Den buntade binären är fallback.

## Inställningar

| Inställning | Betydelse |
|-------------|-----------|
| `glazeui.renderPath` | Egen glaze-render-binär (t.ex. ett projektbygge med egna widgets). Tom = den buntade generiska. |
| `glazeui.builderPath` | Egen GlazeBuilder-binär. Tom = projektets eget bygge (konvention) om det finns, annars den buntade generiska. |

## Bra att veta

- **Egna projekt-widgets** (`GLAZE_REGISTER_WIDGET` i ditt projekt) känner
  den buntade binären inte till — de ritas som **stand-in-rutor** med
  typnamnet (medvetet: extensionen fungerar utan GlazeUI-repot). Vill du ha
  dem pixelriktiga: bygg glaze-render i ditt projekt och peka
  `glazeui.renderPath` på den. Det görs med `-DGLAZE_TOOLS=ON` (verktygen
  byggs då också när GlazeUI är ett beroende) och
  `target_link_libraries(glaze-render PRIVATE <dina widgets som ett
  OBJECT-bibliotek>)` — se MANUAL.md, avsnittet `cmake`. Medlemsraden i
  regionen ska skriva klassen med sin namnrymd (`app::Matare level;`), för
  verktygen slår upp den i registret på det fulla namnet. Samma sak gäller
  `GlazeBuilder` och `glazeui.builderPath`.
- `.glazeui` ensam räcker inte — syskon-`.h` måste ligga bredvid
  (tre-fils-modellen). Saknas den visar felvyn diagnosen.
- Saknad `assets.bin` är inget fel — bilder/ikoner utelämnas bara.
- **Remote/headless fungerar inte** (SSH-remote, Codespaces, WSL):
  glaze-render behöver en skärmsession och binären buntas per plattform.
  Desktop-VS Code på Mac/Windows/Linux är målet.

## Bygga extensionen (utvecklare)

```sh
cd tools/vscode-glazeui
npm install
npm run bin:host        # kopierar glaze-render + GlazeBuilder → bin/<host>/
npm run compile         # esbuild → dist/extension.js
# F5 i VS Code = Extension Development Host
```

### Paketera per plattform

VSIX:erna är per-plattform (binärerna buntas). På respektive plattform:

```sh
cmake --build build                       # bygger glaze-render + GlazeBuilder (repo-roten)
cd tools/vscode-glazeui && npm install
node scripts/copy-bin.mjs <target>        # lägger båda binärerna i bin/<target>/
npm run package:<target>                  # vsce package --target <target>
```

Targets: `darwin-arm64` · `darwin-x64` · `win32-x64` · `linux-x64`.
Saknas någon av binärerna (`glaze-render`/`GlazeBuilder`) i `bin/<target>/`
vägrar scriptet — **den VSIX:en byggs när båda finns** (CI-matris eller
manuellt på den plattformen).
Publicering: `npm run publish:<target>` (kräver marketplace-publisher).
