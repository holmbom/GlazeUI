# CLAUDE.md — tools/vscode-glazeui

VS Code-extensionen för `.glazeui`-filer — fristående TypeScript/Node-produkt
(GlazeUI:s C++-hårdregler gäller inte koden här, men "återanvänd i stället
för att reimplementera"-andan gäller).

**Grundregeln:** ALL rendering går via **glaze-render** (buntad generisk
binär i `bin/<target>/`, eller `glazeui.renderPath`-override) — extensionen
parsar ALDRIG tripletter/användarkod själv. Den parsar glaze-renders
stdout-KONTRAKT `"<path>: <w>x<h> px (scale <s>)"` + exit 0/1/2 med
stderr-diagnos ([apps/glaze-render/CLAUDE.md](../../apps/glaze-render/CLAUDE.md)).

## Filer

| Fil | Roll |
|-----|------|
| `src/extension.ts` | Aktivering: registrerar custom-editorn, kommandona (lägesväxling a/b/c + Öppna i GlazeBuilder), context-nyckeln `glazeui.builderAvailable` |
| `src/renderCore.ts` | **vscode-FRI** spawn + stdout-parse + exit-kodstolkning — röktestas med ren node mot riktiga binären |
| `src/scheduler.ts` | **vscode-FRI** debounce + en-render-åt-gången-per-nyckel (koalescerat, aldrig kö) — mot render-stormar |
| `src/renderer.ts` | vscode-limmet: hittar binären (`bin/<platform-arch>/` eller `glazeui.renderPath`), PNG per (dokument, session) under `globalStorageUri/previews/` — ALDRIG i arbetsytan, reparerar exekverbar bit defensivt |
| `src/previewSession.ts` | En webview som visar EN fil — delas av bildläget och sidopreviewn: HTML-mall, bild/fel-postMessage, filbevakning. HiDPI: CSS-bredd = fysiska px / scale |
| `src/previewProvider.ts` | `CustomReadonlyEditorProvider` (viewType `glazeui.preview`, priority default) — läge a |
| `src/sidePreview.ts` | WebviewPanel i ViewColumn.Beside per dokument (toggle) — läge c |
| `src/tripletWatcher.ts` | FSWatcher på `<bas>.{h,glazeui}` + närmaste assets.bin (glaze-renders fyra-nivåers uppåtvandring) + onDidSaveTextDocument. `.cpp` bevakas INTE (påverkar aldrig bilden) |
| `src/builderLocator.ts` | GlazeBuilder-lokalisering i ordning `glazeui.builderPath` → projektkonventionen `build/apps/GlazeBuilder/…` (uppåt från filen + arbetsytans rötter) → **buntad** `bin/<target>/GlazeBuilder`; detached spawn, aktiv-flik-URI via tabGroups. Buntad fallback ⇒ knappen i praktiken alltid tillgänglig |
| `media/preview.html/.css` | Webview-mallen (platshållare %CSP_SOURCE%/%NONCE%/%CSS_URI%) — CSP `img-src` = webview-källan, script bara med nonce; svensk felvy med stderr-diagnos + Öppna-som-text |
| `media/icons/*.svg` | Rosa (#FF2E97) editor/title-ikoner för de fyra kommandona (text/preview/side/builder) — egna SVG:er så knapparna syns (codicons går inte att färga via JSON) |
| `scripts/copy-bin.mjs` | repo-`build/` → `bin/<target>/` båda binärerna (glaze-render + GlazeBuilder; `.app`-bundle rekursivt på Mac), exekverbar bit bevaras. VÄGRAR (exit 1) ett `build/` med `GLAZE_SHARED:BOOL=ON` i `CMakeCache.txt` — VSIX:en buntar statiska binärer, och DLL:erna hade inte följt med |
| `scripts/package.mjs` | per-target `vsce package/publish`: appendar temporärt `!bin/<target>/**` till `.vscodeignore` (som utesluter `bin/**`) så EN binär buntas, återställer alltid. Kör vsce via node:s JS-entry (aldrig `.cmd`-spawn — Node ≥24 vägrar det, EINVAL) |
| `install-ext.bat` | Windows-installer (frågar först): Node-koll/winget → `npm install` → copy-bin → package → `code --install-extension`. Buntas ej i VSIX (`.vscodeignore`) |
| `bin/<target>/` | buntade binärer (glaze-render + GlazeBuilder) — byggprodukter, committas inte ([bin/README.md](bin/README.md)) |

## Konventioner & fallgropar

- **Svenska** i kommentarer OCH användarsträngar (matchar GlazeUI).
- esbuild buntar allt → `dist/extension.js`; inga runtime-beroenden
  (`vsce package --no-dependencies`). `npm run typecheck` = tsc --noEmit.
- `@types/vscode` är PINNAD till engines-versionen (1.90.0) — vsce vägrar
  om types > engines.
- Custom-editor + texteditor är olika världar: aktiv flik hämtas via
  `window.tabGroups` (`activeTextEditor` ser inte custom-editorn), och
  knapparnas when-klausuler använder `activeCustomEditorId`.
- Två vyer av samma dokument (editor + sidopreview) har OLIKA
  PNG-utfiler och scheduler-nycklar (`kind`-suffixet) — samma utfil hade
  gett kapplöpning om två renderingar skrev samtidigt.
- Webviews cachar bild-URI:er — `?v=N`-cache-bustern är bärande för
  om-rendern.
- Testning utan F5 sker med scratchpad-projekt (repo-regeln: inga
  test-targets i repot): renderCore/scheduler buntas fristående och körs
  med ren node; hela F5-flödet automatiseras med `@vscode/test-electron`
  (`npm install --no-save`) — OBS `env -u ELECTRON_RUN_AS_NODE` och KORT
  `--user-data-dir` (macOS unix-socket-gräns ~104 tecken).
- Remote/headless (SSH/WSL/Codespaces) fungerar inte — glaze-render kräver
  skärmsession och binären buntas per plattform (dokumenterad begränsning).
