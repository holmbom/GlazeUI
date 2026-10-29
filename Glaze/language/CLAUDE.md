# Glaze/language — language.bin-formatet (lib glaze_language)

REN STD (`<cstdint> <fstream> <filesystem> <random> …`), **noll beroenden,
ingen koppling till glaze/gfx/builder_core** — headless-kompilerbar precis
som `glaze_assets`. Konsumeras av `apps/glazelanguage/` (CLI:t),
`glaze_impl` (PRIVATE — apparna ser bara `glaze/language.h`) och
`tests/lang_gate.cpp` del 1.
Förblir STATISK också i delat läge (`GLAZE_SHARED`), då positionsoberoende och
inlänkad PRIVATE i libglaze, medan CLI:t och lang_gate länkar egna kopior —
det håller bara för att libben är TILLSTÅNDSLÖS. Språkbutiken (det
processvida) bor i libglaze:s `language.cpp`, aldrig här.

**`builder_core` får ALDRIG länka den här libben** — grammatiken är ren
std utan ens den kopplingen, och det är därför `.glazeui` bär NUMRET och
inte konstantnamnet: `builder_core` kan omöjligt slå upp `glaze::str::Ok`.

## Filerna

- **`include/glaze_language/language_file.h`** — hela API:t: `File`
  (läsaren: `openFile`/`openBytes` + `langs()`/`entries()`/`bytes()`/
  `find(språkindex, id)`/`findLang(tagg)`/`allIds()`), `packBytes`/`pack`
  (skrivaren), `writeAtomic` (den specificerade ersättningen),
  `requireAppScope`/`requireFrameworkScope` (låset på 1–999),
  `parseManifest`/`exportManifest`/`dump`/`headerText`,
  `normaliseraTagg`.
- **`src/language_file.cpp`** — implementationen + formatets fältlayout
  (dokumenterad i filhuvudet, fält för fält).
- **`framework.langtxt`** — RAMVERKETS 96 strängar × fyra språk. KÄLLAN.
  Id 24 (decimaltecknet, post 12) är den första som inte är en etikett
  utan ett SPRÅKFAKTUM — samma mekanism, för tecknet följer språket.
  Id 25–28 (TODO-post 31) är navigationsklustrets tangentnamn — PgUp, PgDn,
  Ins och numpadens mitttangent ("Mitt"/"Begin"/"Midt"/"Keski"); de
  översatta genvägsnamnen grindas mot handskrivet facit i
  `tests/numpad_gate.cpp`. Id 29–56 (post 18/25) är FILVÄLJARENS: titlar och
  knappar, kolumner, etiketter, typtexten (ändelsen + id 41 — suffixet bär
  sin skarv, "-fil" eller " file", som `ModCtrl` bär sitt plustecken),
  överskrivningsfrågan (filnamnet står på egen rad FÖRE meningen, så ingen
  översättning behöver en platshållare — formatet har inga), felmeningarna,
  de kända platserna, fältets "…" och (id 56, post 46) kryssrutan för dolda
  filer. Id 57–60 (post 20) är FÄRGVÄLJARENS etiketter — Röd, Grön, Blå och
  "Hex" (samma ord på alla fyra, men en etikett som "…").
  Id 61–80 (post 26) är DATUMVÄLJARENS: månadernas namn (61–72, gemener
  där språket skriver dem så), veckodagarnas korta namn i ISO-ordning
  måndag–söndag (73–79) och id 80, VECKANS FÖRSTA DAG som ISO-nummer — det
  ANDRA språkfaktumet efter decimaltecknet. `"1"` på alla fyra, och för
  engelskan ett skrivet VAL: språket bär ingen region (USA:s söndag är en
  regionsfråga), och ISO 8601 är samma regel som datumfältets form följer.
  Ett ogiltigt värde ger måndag och en loggrad, aldrig ett trasigt rutnät.
  Id 81–83 (post 32, täckningspasset) är meddelanderutans "Abort" (ett EGET
  id: "Avbryt" på sv/nb men "Abort"/"Keskeytä" på en/fi), filstorlekens
  byte-enhet ("B", finskans tavu "t" — "12 kt") och macOS appmenys
  "Avsluta "-prefix (`gfx::setQuitMenuPrefix`). Blocket 160–199 är
  DOCKNINGENS (TODO 77, luckigt per etapp): 162–165 (E2) är panelrubrikens
  knappnål ("Dölj automatiskt"/"Fäst" — VS:s Auto Hide/Pin), "Stäng" och
  menyknappens "Fönsterläge"; 160/161 (E3) rubrikmenyns "Flytande"/"Docka"
  (VS:s Float/Dock); 166 (DOCKNING_DRAG, TODO 81) det flytande
  verktygsfältets rubrik utan egen text ("Verktygsfält", VS:s Toolbar).
  Blocket 255–269 är FAS1_B_WIDGETS (post 63/54): 255 färgväljarens fjärde
  reglage "Alfa" ("Alpha" på engelska) och 256 färgdialogens titel när appen
  inte satt någon ("Färg" — WinForms ColorDialog säger "Color").
  Blocket 420–429 är VERKTYGSFÄLTETS ÖVERFLÖDE (REQ8 9): 420 överflödsknappens
  tooltip "Visa mer" (Windows kommandofälts "See more") och 421 posten för en
  inbäddad kontroll utan eget verktygstips ("Kontroll").
  Granskningen av nb/fi/en
  och källorna står i filens huvud. **`lang_gate` del 4** fäller en NY
  hårdkodad användartext i `Glaze/widgets/` — se grindens huvud för
  reglerna och undantagsmarkörerna.
- **`language.bin`** — packad ur den, committad, och inbäddad i
  `Glaze/widgets/src/lang_builtin.cpp` av `glazelanguage embed`.

## Kontraktet

- **Filen ÄR källan** (som `assets.bin` och `.resx`): den committas.
  `.langtxt`-manifestet committas också — det är den läsbara formen och
  det enda som går att diffa i en pull request.
- **NYCKELN ÄR ETT NUMERISKT ID, språket är formatets YTTRE DIMENSION.**
  "Index 1 är Ja på alla språk, och uppslaget tar det valda språkets
  index 1" (Erik). Ett femte språk är därför ett rent TILLÄGG — en
  språkpost, dess entrysvit, dess texter — medan en platt
  (id, språk)-tabell hade krävt omsortering av HELA entrytabellen.
- **Little-endian, byte för byte** via `rd16/rd32` — aldrig `memcpy` av
  packad struct. Sektioner: Header(64) → Språktabell(24/post) →
  Entrytabell(16/post) → Strängtabell (NUL-terminerad UTF-8).
- **Versionsregeln (hård regel 4:s anda):** fel magic/major ⇒ VÄGRA;
  nyare minor ⇒ acceptera och hoppa okänt på DEKLARERAD längd
  (`headerSize`/`langRecSize`/`entryRecSize`) — aldrig gissa.
- **TVÅ AVVIKELSER FRÅN `assets_file.cpp`, båda medvetna** (och båda
  skrivna i cpp:ns filhuvud):
  1. **CRC:n täcker HELA FILEN** med CRC-fältets fyra bytes räknade som
     noll — inte bara en blob. Därmed skyddas magic, samtliga offsets,
     recordstorlekarna, golvet, `defaultLang`, flaggorna, `fileSize`,
     tabellerna OCH strängarna, och en framtida större `headerSize` i sin
     helhet utan kodändring.
  2. **CRC:n kontrolleras FÖRST**, inte sist. Assets rapporterar
     strukturfel före CRC-fel; här gör hela-filen-täckningen varje muterad
     byte till ett CRC-fel, så en härledd strukturdiagnos hade dolt
     orsaken. Filen är KORRUPT, inte felformad. `lang_gate` sabotage-delen
     mäter exakt det: fyra olika muterade bytes ⇒ CRC-avslag i alla fyra.
- **Hela valideringen körs vid VARJE laddning** — monotona och disjunkta
  sektioner, overflow-säker aritmetik i 64 bitar, DEKLARERADE
  recordstorlekar, NUL-terminering, BCP-47-formvaliderade och unika
  taggar, `id != 0`, stigande och unika id per språk, disjunkta och
  luckfria språksviter, `langIndex`-bakpekaren, `defaultLang < langCount`,
  flagg- och golvkontrollen, CRC och `fileSize`.
- **TOM STRÄNG är en GILTIG text** och betyder INTE "saknas". Ett saknat
  id är FRÅNVARO av en post i språkets svit. Grindad distinktion.
- **`appIdFloor` måste vara exakt 1000** i version 1, och låset bärs av
  ROUTINGEN: `requireAppScope` kräver `flags == 0` och varje `id >= 1000`,
  `requireFrameworkScope` kräver exakt ramverksbiten och varje
  `id < 1000` — men det BÄRANDE lagret ligger i runtime, där `tr(id)` med
  `id < 1000` aldrig slår i appbutiken. `pack --ramverk` är en
  bekvämlighet i det här repot, inte en säkerhetsgräns.
- **Skrivningen är ATOMÄR och SPECIFICERAD.** `std::filesystem::rename` är
  specificerad "as if by POSIX `rename`", alltså atomisk ersättning av en
  befintlig icke-katalog — vi gör ALDRIG "ta bort destinationen, sedan
  rename". Tempnamnet byggs av `std::random_device`-entropi plus en
  räknare i MÅLKATALOGEN med `exists()`-kontroll: **praktiskt unikt, inte
  formellt kollisionsfritt mellan processer** — std har varken process-id
  eller `O_EXCL`, och att låtsas om en garanti som inte finns vore värre
  än att sakna den. KÄND WINDOWS-AVVIKELSE: ersättningen misslyckas om
  destinationen är öppen av en annan process.

## Fallgropar

- Ny fält-/postutökning = bumpa `versionMinor` och skriv längre poster —
  äldre läsare hoppar det okända på längd. Layoutbrott = `versionMajor`.
  **Och `appIdFloor` kan bara höjas i ett versionMajor-brott.**
- `packBytes` tar allt I MINNET; `pack` är den + `writeAtomic`. CLI:t
  packar till bytes, VALIDERAR och skriver sedan — en fil som avvisas ska
  aldrig hinna ersätta en som fungerar.
- **KONSTANTNAMNEN lagras ALDRIG i .bin.** De bor i manifestets
  `id <tal> <NAMN> "text"` och används bara av `pack --header`.
  Ramverkets header (`glaze/strings.h`) är HANDSKRIVEN och får aldrig
  genereras ovanpå; appens genereras och committas (regel 7).
- Efter varje ändring i `framework.langtxt` måste BÅDE `pack --ramverk`
  OCH `embed` köras — inget i bygget gör det, och `lang_gate` del 2 fäller
  bygget om `lang_builtin.cpp` inte är byte-identisk med `language.bin`.
