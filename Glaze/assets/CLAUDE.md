# Glaze/assets — assets.bin-formatet och bildverktyget (lib glaze_assets)

REN STD (`<cstdint> <fstream> <filesystem> …`), **noll beroenden, ingen
koppling till glaze/gfx** — headless-kompilerbar precis som builderns
`cpp_reader`/`cpp_writer`. Konsumeras av `apps/glazeassets/` (CLI:t),
GlazeBuilder (asset-läget) och `glaze_impl` (PRIVATE — apparna ser bara
`glaze/assets.h`).
Förblir STATISK också i delat läge (`GLAZE_SHARED`), då positionsoberoende och
inlänkad PRIVATE i libglaze — medan GlazeBuilder och CLI:t länkar egna kopior.
Det håller bara för att libben är TILLSTÅNDSLÖS: inget processvitt tillstånd
får läggas här (en räknare för temporära filnamn och en oföränderlig
inflate-tabell är allt som finns).

## Filerna

- **`include/glaze_assets/assets_file.h`** — hela API:t: `File`
  (läsaren: `openFile`/`openBytes` + `lists()`/`entries()`/`data()`/
  `find(lista, nyckel)`), `pack(lists, entries, path, err)` (skrivaren —
  ATOMÄR: temp + rename), `extract` (exporten), `dump` (rapporten),
  typ-/density-konstanterna.
- **`src/assets_file.cpp`** — implementationen + formatets fältlayout
  (dokumenterad i filhuvudet, fält för fält).
- **`src/crc32.h`** — CRC32 (IEEE, reflekterad) som delad intern header:
  lyft ur `assets_file.cpp` när PNG-codecen behövde samma kontrollsumma
  (PROGRAMIKON steg 1). En implementation, två konsumenter.

BILDVERKTYGET (PROGRAMIKON, 2026-10-11) — programikonens väg från EN bild
till varje storlek:

- **`include/glaze_assets/image.h`** + `src/image.cpp` — `Rgba` (RGBA8, RAK
  alfa, rad 0 överst), `resizeRgba` (yt-medelvärde nedåt, bilinjärt uppåt,
  i FÖRMULTIPLICERAD rymd — en genomskinlig pixels färg blöder inte),
  `fitSquare` (inpassning i en kvadrat, genomskinlig centrerad marginal),
  valregeln `pickIconSource` (minsta källa med sida ≥ målet, annars den
  största, −1 utan källa) och storlekslistorna `kIconSizes` (fönstret och
  `.ico`) och `kHicolorSizes` (Linux). **`glaze_impl` använder `image.h` i
  DRIFT** (`src/window_icon.cpp`, `ImageCache::getResampled`) — samma
  skalare och valregel som verktyget.
- **`include/glaze_assets/png.h`** + `src/png.cpp` — en egen PNG-codec i ren
  std: avkodaren tar färgtyp 0/2/3/4/6, bitdjup 1–16, tRNS och Adam7 och
  kontrollerar CRC och adler32; kodaren skriver färgtyp 6/8 bitar utan
  interlace med fast Huffman och är DETERMINISTISK. Den finns för VERKTYGET
  (byte-identiska filer på alla OS) — i drift avkodar appen genom
  `gfx::decodeImage`.
- **`include/glaze_assets/icon_file.h`** + `src/icon_file.cpp` — `writeIco`
  (< 256 som 32-bitars BMP med dubblad höjd och AND-mask, 256 som PNG) och
  `writeIcns` (`is32`/`s8mk`, `il32`/`l8mk` med Apples RLE, resten PNG, med
  `TOC `), `kIcnsSizes`.

CLI:t (`apps/glazeassets/`) har två kommandon på dem: **`glazeassets icon
<fil.bin> <lista>:<nyckel> [--small <lista>:<nyckel>] <utkatalog> <bas>`**
⇒ `<bas>.ico`, `<bas>.icns`, `<bas>-<N>.png` (ingen ram skalas upp förbi
max(256, största källan)), och **`glazeassets embed <fil.bin> <utbas>
[--ns] [--symbol]`** ⇒ en C-array i `.h`/`.cpp` (`glazelanguage embed`s
form).

**Committat i mappen:** `framework_assets.bin` (ramverkets egna bilder —
listan `glaze` med `icon` 256×256 och `icon16`; inbäddad i libglaze som
`Glaze/widgets/src/assets_builtin.cpp`, symbolen `kFrameworkAssetsBin`) och
`icon/` (GlazeUI-ikonens `glazeui.ico`, `glazeui.icns` och
`glazeui-<N>.png` — det `glaze_app_icon(<target>)` utan argument lägger i
programfilen). Allt genereras med CLI:t och committas; käll-PNG:erna gör
det inte, och bygget genererar ingenting.

## Kontraktet

- **Filen ÄR källan** (som `.resx`): committas; käll-PNG:erna gör det
  inte. Exporten är därför garanterad **per konstruktion** — posterna bär
  den oförändrade källfilen + `origName`; extract = skriv bytes.
- **Little-endian, byte för byte** via `rd16/rd32` — aldrig `memcpy` av
  packad struct. Sektioner: Header(64) → Listtabell(32/post) →
  Entrytabell(40/post) → Strängtabell (NUL-terminerad UTF-8) → Blob
  (4-byte-alignade råa källfiler; identiskt innehåll delas).
- **Keys unika PER LISTA** — referensen är paret (lista, nyckel); listan
  bär storleksklassen i LOGISKA px. Dubbletter (nycklar eller listnamn)
  vägras — tyst "först vinner" vore värre.
- **Versionsregeln (hård regel 4:s anda):** fel magic/major ⇒ VÄGRA;
  nyare minor ⇒ acceptera och hoppa okänt på DEKLARERAD längd
  (`headerSize`/`listRecSize`/`entryRecSize`) — aldrig gissa.
- **Hela valideringen körs vid VARJE laddning** (offsets, bakpekare,
  NUL-term, dubbletter, CRC32 över blobben) — minsta fel ⇒ ladda
  ingenting, distinkt felsträng i `err`. CRC:n är inte valfri: filen är
  committad binär som git varken diffar eller reparerar.
- `typeTag` 2 (Audio) och `density` (100/200) är **reserverade** — plats
  i formatet, ingen kod (GLAZE_ASSETS "utanför scope"). @2x-källor packas
  som husstandard (skarpa ikoner på Retina — beslut 6).

## Fallgropar

- Ny fält-/postutökning = bumpa `versionMinor` och skriv längre poster —
  äldre läsare hoppar det okända på längd. Layoutbrott = `versionMajor`.
- `pack` tar bytes I MINNET (`PackEntry::bytes`) — den läser aldrig
  källfiler själv (anroparen äger I/O:t; CLI:t/buildern läser).
- `origName` utan sökvägsled är ett pack-krav OCH ett extract-krav
  (fientlig främmande fil får inte skriva utanför målkatalogen).
- **Ändras skalaren, kodaren eller valregeln måste `icon/` och
  `framework_assets.bin`/`assets_builtin.cpp` genereras om** med CLI:t —
  `icon_gate` D3 kör `glazeassets icon` och jämför byte för byte med de
  committade filerna, och E1 jämför den inbäddade arrayen med `.bin`-filen.
- PNG-codecen är INTE en andra avkodningsväg i ramverket: drift går genom
  `gfx::decodeImage` (bara PNG, på alla OS). En ny bildfunktion i drift
  avkodar där och skalar här.
