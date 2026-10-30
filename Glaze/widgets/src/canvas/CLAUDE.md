# CLAUDE.md — Glaze/widgets/src/canvas

Implementationen av RITYTANS typer och motor (CANVAS_2D, TODO 27) och dess
3D (CANVAS_3D, TODO 50 — samma scen och samma lager) — headrarna och
kontrakten står i [../../include/glaze/canvas/](../../include/glaze/canvas/CLAUDE.md),
widgeten i [../widgets/canvas.cpp](../widgets/canvas.cpp). Del av GlazeUI — se
[root /CLAUDE.md](../../../../CLAUDE.md) för lagerbilden och de hårda reglerna.
Bildkällornas texturer bor INTE här utan i [../image_cache.cpp](../image_cache.cpp)
(se [../CLAUDE.md](../CLAUDE.md)).

Här FÅR `c.r` stå: scenens GPU-arbete (`drawMesh`, `uploadTransient`,
buffertarna) bor här så att widget-filen kan hålla sig fri från den (grinden
`grep "constexpr float k\|c\.r->" Glaze/widgets/src/widgets` = 0).

## Filer

- **`image_data.h`** — INTERN: det handtagen pekar på. `BitmapData` (id som
  aldrig återanvänds — cachens nyckel —, mått, pixlar, filter och
  ÄNDRINGSLOGGEN: `version`, `base` och `changes`, där varje ändring med
  version > `base` finns kvar; `markChanged` antecknar, en ändring av hela
  bilden tömmer loggen och en full logg ger upp sin äldre halva) och
  `RenderTextureData` (id, mått, målningen, rensfärgen, filtret och
  `version`, som paint/repaint stegar). Bara src/canvas/*.cpp och
  src/image_cache.cpp drar in den. REQ3_RITYTAN 9: `keepPixels`/`released`/
  `releaseQueued`/`missLogged` och SLÄPPKÖN `queuePixelRelease` (bilden och
  den uppladdade versionen) + `releaseQueuedPixels` (App:s pump efter ritloopen).
- **`bitmap.cpp`** — `glaze::Bitmap`: konstruktorerna (storleksvakten mot
  size_t-spill), `decode` genom `gfx::decodeImage`, `setPixels`/`fill`
  (kopierar raderna och antecknar i loggen) och filtret. Id:t ur ett
  ATOMISKT löpnummer, för konstruktorerna och `decode` körs i vilken tråd
  som helst. `setKeepPixels` och släppkön (UI-trådens, en funktionslokal i
  cpp:n): släppet bara vid samma version och `keepPixels` false.
- **`render_texture.cpp`** — `glaze::RenderTexture`: måtten, `paint`
  (lagrar funktion + rensfärg, stegar versionen), `repaint` och filtret.
  Ingen renderer — målningen sker i fönstrets cache.
- **`shape.cpp`** (etapp 7) — `Shape`s fabriker, `Paint` (jämförelsen — en
  körnings nyckel jämför gradienten på VÄRDE) och `ImageSource` (`sameImage`
  via `friend` till handtagen; sedan TRAD_LISTOR_BILDER tar Bitmap- och
  RenderTexture-konstruktorerna en SNAPSHOT av `d_->version` — `image_data.h`
  — som `operator==` jämför, och `sizeClass()` är `assetSizeClass` för en
  asset, 0 annars). Cirklar, ellipser och bågar plattas till 128
  punkter per varv (zoomen är okänd när formen byggs; felet ≈ 0,0003 · r).
  **CANVAS_TEXT_OCH_BILD:** etiketten i delar lagrar `spans_` (sist i
  klassen) och lämnar `text_` tom; bilden på fyra hörn lagrar hörnen i
  RINGORDNING i `points_` och LÅDAN kring dem i `rect_` (gallringen och
  rutnätet läser den), och fabriken prövar fyrhörningen med
  `gfx::quadWeights` och loggar en ogiltig EN gång per process (atomic —
  fabrikerna går i vilken tråd som helst). Vikterna lagras inte: byggaren
  räknar dem ur hörnen den får.
  **FÖREMÅLEN (CANVAS_3D etapp 4–6):** `meshOf` (gfx/geom3d-geometri →
  `Mesh` med origot i lådans mitt), ENHETSMESHARNA `unitBox`/`unitSphere`
  (statiska, initierade en gång) och `unitCylinder(sidor)` (en cache bakom
  ett lås — fabrikerna går i vilken tråd som helst), `polygon3` (öron-
  klippningen i ringens plan; linjär gradient → skalär t + ramp, radiell →
  första stoppet + en logg per process), `instances` (prototypens låda i
  instansens ram → `buildInstanceSet`) och läsvägen.
- **`path.cpp`** (etapp 7) — `Path`: segmenten och `flatten()` (De
  Casteljau tills kontrollpunkterna ligger inom 1/1000 av kurvans
  kontrollpolygon, högst 2¹² segment; Quad som Cubic).
- **`build.h/.cpp`** (etapp 7) — INTERN: en FORM till byggarnas geometri,
  DELAD av lagren och målaren. `partKeys` (formens delar i ritordning med
  deras `RunKey` — en yta är Fill och sedan Line; en bildmarkör bär sin bild
  i nyckeln, och text och markörer sitt golv och tak, REQ3 8), `shapeBounds`,
  **`glyphLimits`/`clampGlyphPx`/`glyphReachEm`** (golvet och taket i fysiska
  px — uniformerna, placeringen, träffprovet, gallringen och mätningen) och
  **`visibleAtZoom`** (textens och markörens zoomintervall, scene.h `zoomInRange`),
  `buildLinePart` (en SLUTEN kontur byggs sluten, med en riktig skarv i
  startpunkten), `linePartHalfWidth` (räckvidden — spetsgränsen × halva
  bredden, √2 × för en fyrkantig ände), **`strokeKey`** (spetsgränsen och
  strecken via `gfx::makeDash` — för många längder loggas EN gång och ritas
  heldragna) och **`strokeUniforms`** (strecken i skärmrymd; world ⇒ 1),
  `buildFillPart`, `imageUniforms` (färgjusteringen, REQ8 3),
  `imageCorners`/`buildImageQuad` (rättvänd efter y-riktningen), och
  Glyph-delarnas `GlyphUnits`/`toUnit` (alla mått i storlekens enhet),
  `textLayout`/`markerLayout` och `glyphReach`. **CANVAS_TEXT_OCH_BILD:**
  `imageCornersOf(form)` (rektangelns hörn, eller de fyra fria hörnen i
  z-ordning {p0, p1, p3, p2} — lagren, målaren och scene.cpp:s 3D-väg läser
  den), `imageQuadOk` (en rektangel alltid, fyra hörn bara strikt konvexa —
  `partKeys` ger ingen Image-del annars, och träffprovet frågar samma), och
  `buildImageQuad` räknar de PROJEKTIVA vikterna ur hörnen SOM DE RITAS
  (efter målarens transform; ingen yta ⇒ ettor); `splitImageQuads` (REQ8 5)
  delar dem på en bilds RUTOR genom plattans homografi (döda hoppas över). `fontKey` är 1 + familj
  (Mono 2, annars 1) och `fontFamilyOf` vägen tillbaka — stilen står inte i
  nyckeln, så vanliga och feta etiketter delar körning. `textParts` gör
  etiketten till `gfx::StyledText`-delar (en vanlig text är en del, en etikett
  i delar en per `TextSpan` med de osatta fälten ur stilen); strängarna LÅNAS
  ur formens `TrText`. **`measureLabel`** (REQ2_RITYTAN): textblocket utan
  platta, luft, ankare och förskjutning genom `gfx::measureText` — samma
  layout som bygget, inget laddas upp — omräknat från storlekens enhet till
  den begärda med tätheterna i `GlyphUnits`; målaren (`measureParts`, px) och
  `Canvas::measureText` mäter med den, så de två aldrig går isär.
  **`measureNeedsWorld(stil, ut)`** (REQ7 4): sant när `ut`, storleken,
  brytbredden, `maxHeight`, golvet eller taket är World och > 0 (inte glorian
  — mätningen läser den inte); `Canvas::measureWith` svarar då nullopt utan
  täthet. `Xform` är målarens affina transform
  (lagren bygger med identiteten). **I RYMDEN (CANVAS_3D etapp 3):**
  `partKeys` ger Line3 och Glyph3 för formerna i rymden, `buildLine3Part`
  (Line3-chunkarna ur `Stroke`, samma översättning som 2D-konturen),
  `shapeZRange` (formens z-spann — 0 för en plan form), `glyphReach` också
  för en `GlyphChunk3`, och `Xform::apply3` — ett EGET namn, för `{x, y}`
  initierar också en `Point3` och en överlagring blev tvetydig.
  **Etapp 4–6:** `partKeys` ger `PartKind::Mesh` för mesh, yta i rymden och
  instanser (nyckeln är bara slaget — varje föremål är sitt eget anrop) och
  `Points` för punkterna; ytan i rymden också Line3 för sin SLUTNA kontur
  (`buildLine3Part` sluter den för `Area3`). **`objectBox(form)`** —
  föremålets låda i världen med datan NU (meshens låda genom placeringen,
  mängdens, molnets; en skärmkonstant mesh svarar med sitt origo) —
  används av `shapeBounds`/`shapeZRange`, `sceneBox` och 2D-vägens 3D-vy.
  **Etapp 8–9:** helskärmspasset är `PartKind::Pass` (programmet Custom,
  ingen låda), och `RunKey::ground` skiljer LIGGANDE text och markörer
  (`Alignment::Ground`) från de stående — en egen körning, för på terräng
  draperas den ena och den andra står vid markhöjd.
- **`scene.h/.cpp`** (etapp 7) — INTERN: LAGREN. ORDNINGEN är lagrets plats
  i `SceneData::layers`, och `Canvas::moveLayer` (REQ2_RITYTAN) roterar bara
  vektorn: körningarna, chunkarna och fönstrets GPU-kopia (nycklad på id)
  står kvar. Det som beror på PLATSEN räknas om varje ritning —
  `LayerFrame::placement[li]`, etiketternas `LabelSource::layer`
  (placeringens "överst vinner") och draperingens innehållsnyckel — medan
  terrängens regionnyckel är ordningsoberoende (drape.cpp). REQ3_RITYTAN:
  `RunKey::minSize`/`maxSize` (glyfslagen), `SceneData::memoryBudget`, RAII
  `ImageAccount` (ritytans konto i bildcachen under drawScene/drawOverlay/
  renderToBitmap) och `zoomInRange` (lagrens OCH etiketternas regel). `SceneData` →
  `LayerData` (`slots`: formen + var dess geometri ligger) → `RunData` (en KÖRNING: delar
  med samma `RunKey` — program, enhet, gradient, bild (med opaciteten och färgjusteringen), font, och för Line
  spetsgränsen, strecken och deras enhet; en Glyph-körning med en
  markörbild binder bilden i stället för atlassidan) → `ChunkData`
  (≤ 65 536 vertex med ett origo i double, världens AABB, marginalen och
  ÄNDRINGSLOGGEN fönstren synkar mot). **Strict** följer formernas index
  exakt (ny körning vid nyckelbyte, ny chunk när en form ligger mer än
  `kChunkCell` = 4096 världsenheter från chunkens origo); **ByKind** ritar
  bild, yta, linje, text i den ordningen, grupperat på nyckel och i RUTOR om
  `kChunkCell`. Bygget: `appendShape` (inkrementellt — en ny form läggs i
  målchunken om den ryms där, via en förfylld tom chunk med målets origo),
  `removeShapeAt` (DÖD data — indexen degenereras, bara intervallet laddas
  upp; över `kCompactDeadShare` = 25 % märks lagret och byggs om före nästa
  ritning), `replaceShapeAt` (på plats när delarna har samma nycklar och
  exakt lika många vertex, annars ombyggnad) och `rebuildLayer`. Ritningen:
  `prepareLayers` + `drawGround`/`drawWorld` (förr `drawLayers`, se
  "I RYMDEN" nedan; dolt lager hoppas över; ett lager utanför sitt zoomintervall
  hålls ändå RESIDENT — varje chunk synkas FÖRE gallringen, så en panorering
  eller zoom ritar och laddar aldrig upp; sedan etapp 9 ett FÖRPASS som
  bygger varje Glyph-körning för fönstret och samlar de RITADE i ritordning
  åt etiketternas placering (`labels.h`), som körs före den första
  Glyph-körningen — svaret true betyder att en toning pågår och ritytan
  ritar nästa frame också), `boxVisible` (gallringen: AABB:ns
  fyra hörn projicerade, ett hörn bakom kameran ritar alltid), `drawGrid`
  (statisk mesh × skala, origot i kamerans närmaste rutnätspunkt, glesare
  ×1/2/5/10 under `canvasGridMin`) och `pxPerUnit` (px = skalan, mm =
  `Ctx::pxPerMm`, world = kamerans skala i mitten). `PaintContext` är en
  ritnings delade läge — också målarens. **`LayerData::shapesVersion`**
  (etapp 8) stegas av VARJE ändring av formerna — `appendShape`,
  `removeShapeAt`, `replaceShapeAt`, `rebuildLayer` och widgetens
  `setShapes` — och träffprovets rutnät (`LayerData::hit`) gäller bara för
  den version det byggdes ur; `rebuildLayer` räknar profilerarens
  LayerRebuilds. **PRESTANDA C2/C3/C6:** `KeyHash` (FNV-1a — cachenycklarna),
  `LayerData::textVersion` (stegad av `touchText`, språkbytet — som INTE
  stegar `shapesVersion`), `LayerData::snap` (C5), `ShapeEntry::version`
  (stegad av `replaceShapeAt`) och **`objectBox(entry)`** — föremålets låda
  CACHAD i posten med formens och meshens version (`mutable`, UI-trådens;
  scenens låda, terrängens rutnät, draperingarna och pick3d:s skymmare
  frågar den). `SceneData::layerSetVersion` stegas av VARJE sättare som
  ändrar lagrens mängd, ordning, synlighet, placering eller vyer, och
  **`sceneBox`** är CACHAD per vy (`BoxCache`) med en nyckel som läser
  lagren SJÄLV (id, synlighet i vyn, `shapesVersion`) plus
  `meshGeneration()` — för `removeView`/`setViews` numrerar om utan att
  stega `layerSetVersion`. `writeInPlace` växer Glyph-körningens ankarlåda
  (`growRunBox`) — en text flyttad med `setShape` hamnade förut utanför.
  UPPLADDNINGEN SYNLIGT FÖRST: **`prepareUploads(f, scen)`** samlar föremålen
  (`queueObjects`) och bildkörningarna med en synlig chunk (`chunkVisible`,
  ritningens gallring) närmast först, sedan resten med det som blir kvar av
  framens budget; 3D-målets `prepareLayers` kör den, 2D-vägen efter sin
  ritning. Chunkarna har ingen budget och synkas alla.
  **I RYMDEN (CANVAS_3D etapp 3):** `PartKind {Image, Fill, Line, Line3,
  Glyph, Glyph3}` (`isLineKind`/`isGlyphKind`), `ChunkData` med origo och
  låda också i z och `lines3`, `RunData::box`, och `LayerData` med
  `placement`, `state` och `shapes3D` (antalet former i rymden — Auto blir
  World av en; `effectivePlacement`, `hasWorldLayer`). RITNINGEN ÄR DELAD:
  **`prepareLayers(f, scen, toning, residentFirst)`** gör allt som laddar upp
  — ombyggnaden, chunkarna, bilderna, ramperna, textbyggena och
  etikettplaceringen — och svarar med en `LayerFrame` (labels.h: lagrens
  avgjorda placering, `groundDrawn`, etiketterna); **`drawGround(f, scen, lf,
  GroundPart::Geometry | Labels | All, målare)`** och **`drawWorld(f, scen, lf,
  målare)`** ritar (World med lagrets renderläge — `gfxState(s, etikett)`: en
  etikett skriver aldrig djup; `målare`, REQ8 2 `LayerPaint`, är lagrens
  `onPaintLayer` efter deras former, i World med lagrets renderläge — aldrig i
  Labels eller Drape). 2D-vägen ritar `drawGround(All)` i dagens ordning (samma
  pixlar); i 3D-målet sker förberedelsen före `pushTarget` (`residentFirst`).
  `PaintContext::view3D` (`std::optional<gfx::View3D>`) väljer
  projektionen: satt går `chunkMatrix(f, ox, oy, oz)` och gallringen
  (`boxVisible`, lådans åtta hörn) genom 3D-vyn. `sceneBox` (de synliga
  lagrens låda — 3D-vyns klipplan) och `prepareGrid` (rutnätets mesh före
  målet). **VYERNA (etapp 10):** `kMainView` (0 — ritytan själv; del-vyerna
  är 1, 2 …), `LayerData::views` (sorterad och unik, `{0}` från start) och
  `layerInView`; `PaintContext::view` (vyn som ritas) och `labelSlot`
  (etikettplaceringens plats — vyns nummer, eller `kBitmapLabels` = −1 för
  renderToBitmap). `layerDrawn` frågar vyn FÖRST, så ett lager utanför vyn
  behandlas som ett utanför sitt zoomintervall: resident men inte ritat —
  `drawGround`, `drawWorld` och förberedelsens etikettkällor och
  `groundDrawn` följer med utan egna grenar. En del-vys `prepareLayers` hoppar
  över lagren utanför vyn (huvudvyns har gjort dem residenta). `sceneBox(scen,
  vy)` räknar bara vyns lager, så en navigeringskub vid origo aldrig drar ut
  en SWEREF-kartas djupintervall. `gfxState` (lagrets renderläge i fasadens
  form, etiketterna utan skrivning) och `validRenderState` står i headern
  sedan etapp 10 — målaren och `Canvas::setLayerRenderState` delar dem.
  **FÖREMÅLEN (CANVAS_3D etapp 4–6):** `PartKind {Image, Fill, Mesh, Points,
  Line, Line3, Glyph, Glyph3}` — det ogenomskinliga före linjerna i ByKind
  (`isObjectKind`); en Mesh- eller Points-körning bär FORMERNA i
  `RunData::objects` (som Glyph-körningen sina), och sceneBox räknar deras
  NUVARANDE lådor (`objectBox` — en mesh kan ha flyttat sina lägen).
  `ShapeEntry::id` (`nextSceneId`) är FORMPLATSENS id: det överlever
  `setShape` och ombyggnaden, och residensen dubbelbuffrar per plats.
  `ShapeSlot::objectRun`, `LayerData::totalObjects`/`deadObjects`
  (kompakteringen räknar dem som Glyph-formerna), och `writeInPlace` hoppar
  över föremålen — `setShape` på ett föremål är alltid på plats.
  `prepareRun` lämnar föremålens körningar till `prepareUploads`,
  `drawGround`/`drawWorld` till `drawObjects` (scene3d.h);
  `PaintContext::lighting` bär ritytans ljus. **EGNA PROGRAM (etapp 8):**
  `SceneData::programs` — `ProgramSlot` (programmet, en VERSION ur
  `nextSceneId` som varje `setProgram` byter, och en `life`-token som
  fönstrens kompilering lever på) — `findProgram(scen, namn)` och
  `fallbackLogged` (namnen vars reserv redan loggats; `mutable`, för
  ritningen ser scenen som const). `PartKind::Pass` står FÖRST i ByKind,
  `LayerData::passes` räknar passen och `hasScreenPass` tar ritytan till
  3D-målet; **`drawPasses`** ritar dem i 3D-målets steg 1. **TERRÄNGEN
  (etapp 9):** `prepareLayers` sätter `LayerFrame::terrain`
  (drape.h) och lämnar de LIGGANDE textkörningarna på terräng utanför
  etikettplaceringen (de draperas); `glyphEnv` ger körningarna i ett
  Ground-lager terrängen (`onTerrain`), så de stående etiketterna byggs vid
  markhöjd; `drawGround(…, GroundPart::Drape)` ritar markens geometri och
  den liggande texten i ett draperingsmål, och med terräng ritar Geometry
  ingen 2D-geometri — den finns bara i draperingen; **`drawTerrain`** ritar
  Terrain-lagren (draperade) i steg 2, och `drawWorld` hoppar över dem.
  `layerDrawn` står i headern (draperingen och träffprovet frågar den).
  **MARKENS INNEHÅLL (spår G):** `paintsGround(form)` — allt utom ett pass
  och en STÅENDE etikett eller markör — räknas i `LayerData::groundShapes`
  på samma fem ställen som `shapes3D`, och `paintsGround(lager)` är
  markdjupets villkor i `prepareLayers` (`LayerFrame::groundDrawn`) OCH i
  träffprovet (canvas.cpp): ett tomt Ground-lager gör ingen mark.
- **`resources.h/.cpp`** (etapp 7) — INTERN: `CanvasResources`, ritytans
  GPU-resurser per FÖNSTER (`Ctx::canvas`). `sync(chunk)` (buffertar med
  kapacitet i tvåpotenser ≥ 4 KiB; loggen ⇒ bara ändringarna, annars allt),
  `glyphs(run, env)` (Glyph-körningens text byggd mot fönstrets atlas —
  grupperad som chunkarna, en etikett = ett `label`-index i bygget, med
  `LabelInfo` för kollisionen — vinkeln där som `cosA`/`sinA`, räknade en
  gång i bygget (etapp 11) — och sedan etapp 8–9 `shape`/`sub`, formens
  plats i körningens `glyphShapes` och markörens punkt, så träffprovet hittar
  tillbaka till formen; byggets `serial` är ett löpnummer: ett nytt bygge är
  nya etiketter; ombyggd när körningen, tätheten eller atlasepoken ändrats),
  **`findGlyphs(run)`** (bygget SOM DET RITADES, bara om det byggdes ur
  körningens nuvarande version — BYGGER ALDRIG: träffprovet frågar utanför
  en frame, där en atlas inte får laddas upp), `atlas(familj)` (EN
  `gfx::SdfAtlas` per familj sedan CANVAS_TEXT_OCH_BILD, med alla stilarna
  i sig; nyckeln är byggarens `fontKey`),
  `ramp(stopp)` (LRU, tak 256), `grid(färg, bredd)`, **ETIKETTERNA**
  (etapp 9: `LabelState` per rityta — placeringens indata (kameran,
  tätheten, körningarna som `LabelRun` med bas och löpnummer), ordningen
  (`order`, etapp 11), målet och det
  bilden bär (tonas mot målet) och synlighetsbilden, R8 med
  `kLabelTexWidth` = 256 texlar per rad; `labelState`/`findLabelState`,
  `uploadLabels(s, lo, hi)` — EXAKT `hi − lo` byte: en bit av första raden,
  hela rader, en bit av den sista; en saknad eller för liten bild ersätts
  och laddas upp hel — och `dropLabels`) och `trim()` (döda
  chunkar/körningar och rivna ritytors placering, atlaserna över 32 sidor
  töms, och det som ersattes mitt i förra framen — `retired*` — släpps först
  här). **3D-MÅLEN (CANVAS_3D etapp 3):** `target3D(scen, vy, w, h, prov)` —
  ett mål per (scen-id, vy) med djup och MSAA, ersatt när måtten eller
  provtagningen ändras (det gamla i `retiredTargets_`), med cachens nyckel
  (`stamp`, rutan, skalan, mm-tätheten, `drawn`); räknaren
  `targetRenders()`/`noteTargetRender()` (grindarna läser den — varje vys
  mål räknas) och `groundPlane()` (markdjupets kvadrat). Glyph3:s
  byggen bär origo och z-spann (`GlyphGpu`), och `LabelInfo::z` ankarets
  höjd. **Etapp 10:** ETIKETTPLACERINGEN är per (rityta, PLATS) —
  `labelState(scen, plats)`/`findLabelState(id, plats)`, platsen förvald 0 —
  så varje del-vy och bilden placerar med sin kamera utan att röra skärmens;
  `dropViews(scen-id, förstaVy)` (ersätter `dropTargets`) släpper målen för
  vyerna från `förstaVy` och del-vyernas placeringar — 2D-vägen anropar den
  med 0, 3D-målet varje ritning med antalet vyer + 1 (en borttagen vy).
  Huvudvyns placering (2D-vägens också) och bildens står kvar.
  **Etapp 4–6:** `objects()` — föremålens residens (residency.h), ägd här,
  trimmad i `trim()` och riven med resten. **Etapp 8:** de EGNA PROGRAMMEN
  per fönster och VERSION — `program(slot)` (nullptr för ett ogiltigt; det
  sparas också, så felet kompileras en gång), `programError(slot)` och
  `programCount()`; `Program` → `gfx::ProgramDesc` med GlazeUser i hela
  float4 (`uniformFloats` uppåt till 16 byte); en post släpps i `trim()` när
  dess `life` gått ut (programmet bortplockat eller ersatt, ritytan riven).
  **Etapp 9:** `drapes()` — draperingsmålen (drape.h), ägda och trimmade
  här; `GlyphEnv::terrain`/`terrainKey` — en körning på terräng byggs
  med ankarnas markhöjd (`terrainHeight`, EN gång per punkt) i
  Glyph3-chunkar (`GlyphGpu::program`), och REGIONEN under körningens
  ankarlåda (`terrainRegionKey`, PRESTANDA C3) står i byggets nyckel — en ny
  platta bygger om bara körningarna den når. **`atlasEpoch()`** (publik för
  markstämpeln) stegas när atlaserna töms; `glyphs()` räknar profilerarens
  GlyphBuilds (TargetRenders räknas i target3d.cpp).
- **`target3d.h/.cpp`** (CANVAS_3D etapp 3) — INTERN: RITYTANS 3D-MÅL.
  `drawTarget3D(fönstrets läge, målets läge, scen, Target3DArgs)`: FÖRBERED
  utanför målet (planens §3.12 — chunkarna, bilderna, texten, etiketterna
  med 3D-vyn, rutnätet, markplanet), RITA i målet bara när cachen är
  inaktuell — rutnätet och Ground-lagrens geometri, MARKDJUPET (ett plan
  z = 0 kring ögat, 2 × far, som bara skriver djup med Always och en
  djupbias BAKÅT, +4/+1 — det som ligger på marken i ett World-lager vinner
  Less mot det; bara när ett ritat Ground-lager MÅLAR MARKEN, scene.h
  `paintsGround` — lager 0 finns alltid och är tomt —, och utan terräng;
  det som STÅR på marken, de skärmvända primitiverna, skärs aldrig av det:
  deras hörn tar planets djup genom ankaret, förspelets `glaze_standingZ` —
  högst ¼ av djupet, inget nära ögat (REQ9 7) — och Points `pointZ` med taket
  fyra radier (`kPointStandK`), gfx/scene.h `MeshProgram`), World-lagren med sitt
  renderläge, Ground-etiketterna ÖVERST och `onPaint` — och KOMPOSITEN varje
  gång: målets bild 1:1 under ritytans rundade klipp med tonen som opacitet
  (`drawImage` av-premultiplicerar målets bild). CACHEN är
  `Widget::damageStamp()` + rutan, skalan och mm-tätheten. MÅLETS PIXLAR
  börjar på rutans heltalshörn (`targetBox`), så kompositen är en ren kopia
  av det 2D-vägen hade ritat. `defaultMultisample()` (4×, planens beslut 4)
  och `validMultisample` (1, 2, 4, 8); talen står i filens anonyma
  namnrymd. **BILDRUTANS ORDNING sedan etapp 8–9** (planens §3.2): steg 1
  helskärmspassen (`drawPasses`, utan djup); steg 2 rutnätet, TERRÄNGEN
  draperad (`drawTerrain` — draperingarna förbereds före målet,
  `prepareDrapes`) och Ground-lagrens geometri (utan terräng); steg 3
  markdjupet — bara UTAN terräng (då finns inget markplan, och ett hål
  visar det bakom); steg 4 World-lagren; steg 5 Ground-etiketterna överst;
  steg 6 `onPaint`. `renderView(…, DrapeMode)`: huvudvyn räknar
  draperingarnas klass ur sitt avtryck, del-vyerna behåller den, och bilden
  (`renderBitmap3D`) ritar dem `Complete`. `drawTarget3D` svarar
  **`Target3DState`** (etiketterna tonar, draperingar väntar) — båda gör att
  ritytan invaliderar sig själv. PROFILERARENS faser mäts i `renderView`:
  CanvasPrepare, CanvasDrapes, CanvasDraw (minus `onPaint`) och CanvasPaint.
  **ETAPP 10 — DEL-VYERNA OCH BILDEN:**
  en vy i sitt mål är `renderView` (förberedelsen utanför målet och
  bildrutans ordning i det; rutnätet och `onPaint` bara för huvudvyn och
  bilden) med ritläget ur `cameraContext` (3D-vyn, kamerans kartskugga,
  vyn och etikettplatsen — och ur ritytans ritläge det som är ritytans:
  resurserna, meshernas LJUS och y-riktningen; ett nytt fält i
  `PaintContext` som är ritytans måste föras över där, annars ritar vyerna
  och bilden utan det). `drawTarget3D` ritar huvudmålet och sedan varje
  del-vy i ETT EGET MÅL (`target3D(scen, vy, …)` — eget djup och MSAA,
  planens §3.9) med vyns ruta ∩ ritytans i hela px (`Pixels`), samma cache
  per mål med vyns ruta, och kompositar huvudmålet och vyerna i
  nummerordning. `mainView3D` (huvudvyns 3D-vy — 3D-kameran eller
  kartkamerans `toView3D`; canvas.cpp:s `projection` går genom den),
  `validView`, `viewRect` (ankaret, förskjutningen, måtten — 0 = ritytans
  hela), `viewCamera` (`followRotation`: en 3D-kameras vinklar och
  upp-axel rakt av, kartkamerans genom camera_math `orientedAs`), `viewAt`
  (den översta vyn under en fysisk punkt) och `viewView3D` (klipplanen ur
  vyns lager) är EN definition för ritningen och träffprovets routning
  (canvas.cpp `viewUnder`/`viewProjection`, åt etapp 7). `renderBitmap3D`
  (renderToBitmap): `beginOffscreen` → ett eget mål w × h med ritytans
  ritläge (fönstrets Ctx med rutan från (0, 0)), rensat till ritytans
  bakgrund → `renderView` med
  bildens kamera och etikettplatsen `kBitmapLabels` → `endOffscreen` →
  `readTarget` → `Bitmap`; en bild som väntar på bildcachens budget loggas.
  `canvasTarget3DArgs` (provtagning, rutnät, `onPaint`, målarna) delas av
  skärmen, bilden och **`renderExternal3D`** (åt `ExternalCanvasPass`, i
  `external_pass.cpp`): `renderView` per mål med en uttrycklig vy
  (`viewContext`, kartskuggan `mapViewAtView`) i ETT offscreen-block, med EN
  `rebuildDirty` och en förnyad resursbudget (`trim`) för alla mål, KOMPLETTA
  draperingar och ingen återläsning; skrivbordets kamera, mål och cache orörda.
- **`drape.h/.cpp`** (CANVAS_3D etapp 9, planens §3.2; REQ3: posten bär sin scen, budgeten är RITYTANS) — INTERN: TERRÄNGEN
  OCH DRAPERINGEN. `hasTerrain` (ett synligt, ritat Terrain-lager med en
  mesh eller yta i rymden), **`terrainHeight(scen, x, y)`** — den ENDA
  definitionen av markhöjden: den översta ytan rakt under punkten på den
  finaste nivån, en lodrät stråle genom plockningens BVH (pick3d.h
  `verticalRayDown`); `Canvas::heightAt`, etiketternas bygge och
  träffprovets ankare frågar den. **TERRÄNGENS RUTNÄT** (PRESTANDA C3, den
  interna `TerrainIndex` i `SceneData::terrainIndex`): Terrain-lagrens former
  i ett jämnt XY-rutnät (cellen = medianformens sida) med RÅA pekare som
  NYCKELN validerar (`layerSetVersion`, `meshGeneration()` och
  Terrain-lagrens `shapesVersion`; ett ersatt lager märks genom sitt
  livstecken), byggt lat och om när nyckeln ändrats — en fråga är cellen,
  lådan och BVH:n, utan allokering. **`terrainRegionKey(scen, låda)`** —
  id, formens och meshens version för terrängformerna som når lådan,
  SUMMERADE (ordningsoberoende, LO7) — ersätter den borttagna globala
  `terrainVersion`; `terrainIndexStats()` (byggen, kandidater) är
  grindarnas. **`DrapeCache`** (per fönster,
  `CanvasResources::drapes()`): ett RGBA8-mål per terrängform
  (formplatsens id), KVADRATISKT över dess XY-låda, ritat UPPIFRÅN med en
  `MapView` genom `drawGround(…, Drape)` — eller med rad 0 i söder när
  ögat står under plattan (`DrapeView::northUp`: en kartkamera med y nedåt
  ser planet underifrån, och den liggande texten blir då aldrig
  spegelvänd). KLASSEN `drapeClass(nu, avtryck)` — en tvåpotens 64–2048
  ur den projicerade lådans största skärmsida (ett hörn bakom närplanet
  ger taket), UPP så snart avtrycket växer förbi, NED först vid en
  fjärdedel (hysteres 2×); bara en synlig platta räknas om. OMRITNINGEN
  när innehållsnyckeln eller klassen ändras OCH plattan syns: nyckeln
  (`contentKey`) är en FNV-hash (`KeyHash`, scene.h) över Ground-lagrens
  tillstånd och, för varje CHUNK och liggande textkörning som når lådan (en
  linjes px-/mm-marginal omräknad till målets världsenheter), dess id och
  version — och bildernas innehållsversion i fönstrets bildcache, så en bild
  som väntar på budgeten ritas om när den kommer. **MARKSTÄMPELN**
  (PRESTANDA C2, `groundStamp`, O(lager) per förberedelse: lagrens id,
  synlighet och placering, Ground-lagrens `shapesVersion`/`textVersion`/
  opacitet/ordning/`layerDrawn`, skalan, mm-tätheten, y-riktningen,
  `ImageCache::contentGeneration`, `atlasEpoch()` och temats stock-färger)
  ligger i `Entry::keyInput` med klassen och lådan — oförändrad ⇒ nyckeln
  räknas inte (`keysComputed()`). **BUDGETEN** (PRESTANDA C1): en DOLD platta
  ritas aldrig om (den gamla står kvar); de synliga som behöver ritas
  sorteras — utan drapering (eller med en ur en annan låda: en flyttad
  formplats, som ritas utan tills den hinns), ändrad nyckel, bytt klass,
  störst först inom varje — och ritas inom `drapeFrameBudget()` (8 Mi px per
  FÖNSTERBILD, räknad från `trim`), minst en per förberedelse (per rityta och
  vy); resten är `DrapeFrame::pending`. KLASSBYTET ritas i ett NYTT mål medan
  det gamla visas. `DrapeMode {Main, View, Complete}`: huvudvyn räknar
  klassen, en del-vy behåller den, bilden ritar KOMPLETT utan budget.
  **POOLEN**: ett mål som lämnar en drapering går i nästa `trim` till en pool
  per klass (`kClasses` 6, högst `kDrapePoolBytes`, räknad i senaste ritytans
  budget) och tas därifrån (`takeTarget`) före `createRenderTarget`. MINNET
  räknas i ritytans budget ihop med bildcachens (`canvasBytes`): över den
  släpps först ritytans mål i poolen (`dropPooled`), sedan vräks
  draperingarna som inte användes i framen, äldst först, till `retired_`
  (släppt i nästa `trim`); en synlig vräks aldrig (loggat en gång).
  Räknarna `renders()`/`pixels()`/`keysComputed()`/`count()` (bara
  draperingar MED mål)/`bytes()`/`classOf()`/`classes()` är grindarnas och
  profilerarens (DrapeRenders, DrapePixels, DrapeKeys). `prepareDrapes`
  körs före målet och svarar med framens `DrapeFrame` (formplatsens id →
  `DrapeView`), som scene3d.cpp binder i Mesh-programmets DRAPED-variant.
- **`camera_math.h/.cpp`** (etapp 7) — INTERN: kameraräkningen — `mapView`/`localView` (glazes kamera ↔ fasadens
  `MapView`), `wrapBearing`, `easeCamera` (kubisk in–ut — kurvan är `../ease.h`, delad med dockningens glidning —
  zoomen geometriskt, bäringen den korta vägen) och `fitZoom`. Här och inte i widget-filen, som håller sig fri från
  tal. **Etapp 8–9:** `Projection` (en vys projektion på CPU:n i DOUBLE — en MATRIS, en RUTA och ett ORIGO: kamerans
  mitt för en `MapView`, ögat för en `View3D` sedan CANVAS_3D etapp 2 — `clip`, `toScreen` med programmens gallring
  (klippdjup < 0 eller w ≤ 0 har ingen skärmpunkt) och `direction`, perspektivdivisionens derivata, alltså
  Glyph-programmets Map-rotation; formerna utan z räknar i z = 0, bit för bit som före 3D-vyn, och `map()` är
  kartkameran — nullptr för en 3D-vy — som 2D-frågorna mot planet ställs till) och GESTERNAS TAL, MapLibres där det
  finns ett (namnen och talen i headern): hjulets och tangenternas zoomsteg, Ctrl+dragets och Shift+pilarnas grader,
  styrplattans panorering (`precisePanPx`, fasadens tecken), toningen (`labelFadeMs`), VRIDSNÄPPET (`snapBearing` —
  vinklarna 0, steget, 2 × steget … MODULO 360, där 360 är norr också när steget inte delar varvet; steget 0 är av,
  och en bäring som redan står på vinkeln ger tomt; glidningen `easeBearing`, bara bäringen) och TRÖGHETEN
  (`PanSample` i en TIDSBASERAD buffert, oberoende av musens takt; `inertiaStart` bara om pekaren rörde sig strax före
  släppet; `inertiaOffset` NORMERAD så att hela sträckan nås exakt vid slutet — trögheten stannar på en bestämd
  punkt). De två gester som är MÅTT i logiska px — träffens tolerans och piltangentens steg — är stilens. **3D-KAMERAN
  (CANVAS_3D etapp 2):** `validCamera3D`, `cameraBasis` (upp längs elevationens TANGENT och vriden med rollen;
  `UpAxis::Y` är (öster, upp, −norr)), **`view3D(kamera, ruta, scenens låda)`** → `gfx::View3D` (ögat i double;
  klipplanen ur lådan och målet — perspektivets near ur far, ORTOGRAFISKT med närplanet FRITT bakom ögat; ett satt
  plan som krockar flyttar det härledda), `orbitCamera3D`, `panCamera3D` (målplanets punkt under pekaren följer i
  SIDLED, och fram/bak efter **`PanVertical`** — `Plane` för `panBy` och PanY, `Fly` och `Lift` för bindningarnas
  handlingar), `dollyCamera3D`, `easeCamera3D` (målet linjärt, avståndet geometriskt, vinklarna den korta vägen, de
  diskreta fälten vid slutet) och `fitBox`; `Camera3D::lookFrom` och `eye()` bor också här, och talen står i filens
  anonyma namnrymd. **Etapp 3:** `Projection::ground()`/ `groundScale()` — planfrågorna (z = 0) för BÅDA kamerorna,
  där `map()` bara hade kartkamerans — och **`mapViewAt(Camera3D, ruta, yUp)`**, 3D-kamerans SKUGGA som kartvy
  (uppifrån, mitten i målet, skalan vid målet): måtten "i mitten" (världsenhetstext, strecken, lagrens zoomintervall)
  räknas mot den, och etiketterna placeras ALDRIG med den (G9 D6). **Etapp 10:** `orientedAs(kamera, blick, upp)` —
  kamerans azimut, elevation och roll (Z upp) ur en vys bas: del-vyns `followRotation` när huvudvyn är kartkameran.
  Elevationen i [−90, 90]; rakt upp- eller nedåt (blickens horisontella del under `kStraightDown`) bär upp-vektorn
  azimuten och rollen blir 0. G9 I2 mäter den mot kartans bäring och tilt. **Etapp 11 (ORBITEN):** gesternas tal är
  KARTANS (`orbitAzimuthDelta`, `orbitElevationDelta`, `wheelDollyFactor`/`keyDollyFactor`), och `tickSeconds` (tiden
  mellan två ticks, högst 0,1 s, i hela ms — PERFORMANCE anm. 15). INGET HOPP: `orbitCamera3D` och `dollyCamera3D`
  vidgar spannet till det nuvarande värdet, så en kamera koden satt UTANFÖR dras mot spannet men aldrig in med ett
  ryck (Eriks regel för gesterna). **REQ9_INMATNING:** `bindingAmount(handling, källa, rå, MotionKind, wheelStep,
  panStep)` — EN funktion för mängden: hjulsteget är valutan, och dragets 0,8°/0,5° per px, pilens 15°/10°, tangentens
  hela zoomnivå och nypningens ln/ln 2 används exakt, så förvalen ger dagens kamera;
  `zoomLevelsFactor`/`dollyLevelsFactor` (2^±nivåer — `wheelDollyFactor`s identitet), `preciseScroll` (tvåfingersdragets logiska px — `precisePanPx` utan
  skalan); FÖRSTAPERSONEN `lookAround` (gir
  och tilt kring ÖGAT — målet läggs om framför det) och `moveEye` (framåt längs blickens vågräta del, höger,
  upp-axeln, kamerans upp).
- **`input_map.h/.cpp`** (REQ9_INMATNING) — INTERN, ren std: BINDNINGSTABELLEN. `InputMap` (rader med nyckeln
  (läge, källa, normaliserade modifierare), vändningarna per källa och per (enhet, handling) som osatt/av/på,
  `defaults()` = B5:s förval som `constexpr`-data) och uppslaget: `lookupBinding` (delmängd, flest bitar, Alt >
  Shift > Ctrl, ritytans rad före appens med SAMMA nyckel), `groupMode` (ett eget läge utan träff för GRUPPEN —
  dragets två axlar, hjulets två … — faller till det automatiska), `resolveDrag` (paret av modifierare där minst
  en axel är påslagen), `resolveEvent` och `bindingFactor` (fart × riktning × vändningarna). `None` skuggar och
  är obunden. Canvas äger sin tabell bakom en pekare, appens bor i App (`detail::canvasInput()`). `binding_gate` T.
- **`painter.cpp`** (etapp 7) — `Painter`: `PainterState` med satsen
  (nyckel + rymd + första punkten; en del långt därifrån tömmer satsen —
  RTC), `drawTransient` (uppladdning i den transienta ringen + `drawMesh`,
  med `strokeUniforms` för Line och markörbilden — ur fönstrets
  `ImageCache` — för en bildmarkör), transformen och `measureText`
  (CANVAS_TEXT_OCH_BILD: båda formerna går genom `measureParts`, som mäter
  delarna med familjens atlas — sedan REQ2_RITYTAN genom build.h
  `measureLabel`, alltså UTAN uppladdning och med `Canvas::measureText`s
  kod; `addGlyph` bygger ur `textParts` och `addImage` ur `imageCornersOf`).
  **Etapp 3:** `drawLine3`/`drawPolyline3`/`drawText3`/`drawMarker3` — Line3-
  och Glyph3-satser med chunkens origo i xyz, genom `chunkMatrix`.
  **Etapp 10:** RENDERLÄGET (`setRenderState`, förvalet `kNoDepth`) är en
  del av satsens villkor: ett byte — också genom `restore()` — tömmer satsen
  först, så varje rit-anrop bär läget det samlades med. Det sätts på
  anropet bara i 3D-målet och i världen (`ctx.view3D` och inte
  `keyScreen`), genom scene.h:s `gfxState` (glyfprogrammen utan skrivning).
  FÖREMÅLEN (`drawObject` — Mesh, Area3:s yta, Instances, Points3; och
  `drawTriangles3`/`drawPoints3` som former ovanpå) ritas GENAST och aldrig i
  en sats: satsen töms först. Varje föremål ur sin CPU-KOPIA (`MeshData`,
  `PointCloudData`, `InstanceSet`) genom den TRANSIENTA ringen — residensen
  (residency.h) laddar upp i förberedelsen och kan inte nås inne i målet
  (§3.12) — med lagrens program (`objects().meshProgram/pointProgram`),
  `meshUniforms` och ritytans ljus (scene3d.h), och detaljnivån och stilens
  intervall med lagrens regel (`meshElements` ≡ scene3d.cpp:s
  `drawnRange`). Målarens transform: vridningen och skalan i modellen (och
  i instansernas rader och punkternas lägen), förflyttningen i anropets
  origo i double. Vyn är 3D-målets, annars `toView3D` över formens höjder
  (som lagrens 2D-väg). En mesh vars data inte ryms i ett segment (64 MiB)
  ritas inte (`meshTooBig`, loggat en gång). G9 I9 mäter målarens väg mot
  lagrets, pixel för pixel.
  **REQ8 1:** KLIPPET — `save(Rect)` → `clipTo`: hörnen genom transformen och
  rymdens matris (`matrixOf`), klippta mot ögats plan i det homogena
  (`projectQuad`), snittade med föräldern (`intersectClip`, Sutherland–
  Hodgman) och skurna till 8 hörn (`cutCorners`). Planen går med varje
  `drawTransient`, och den omskrivna rektangeln ligger som scissor på
  renderarens stack tills `restore` (föremålen klipps av den). Byte tömmer satsen.
- **`placement.h/.cpp`** (etapp 9, E9) — INTERN: ETIKETTPLACERINGENS
  algoritm, en REN funktion utan renderer, kamera eller widget:
  `detail::placeLabels(span<const LabelBox>, screen)` → en flagga per
  etikett. `LabelBox` är en låda i SKÄRMRYMD (mitt, halva mått, luft,
  prioritet, lager, `collide` och baslinjens RIKTNING `dirX`/`dirY` — en
  enhetsvektor ur projektionen, ingen vinkel); en vriden låda räknas som sin
  OMSLUTANDE (`labelBounds`). Ordningen är prioritet → lagret som ritas
  ÖVERST → indexet, girigt och deterministiskt; den finns också som
  PERMUTATION (`placementOrder`), och `placeLabelsInOrder` placerar lådor som
  REDAN står i den — ritytan sorterar när etiketterna ändras, inte per
  kameraändring. Rutnätet (64 px, högst 512 celler per axel) är bara
  grovsållning, så `screen` påverkar farten men aldrig svaret. Grind:
  `tests/placement_gate.cpp` (G8, headless). Inkopplad i `labels.cpp`.
- **`labels.h/.cpp`** (etapp 9, E9) — INTERN: KOLLISIONEN INKOPPLAD. (REQ3: ZOOMINTERVALLET först — texel 0, ingen plats — och lådan i den klampade storleken.)
  `updateLabels(f, scene, källor, toning)` får de RITADE Glyph-körningarna i
  ritordning (`LabelSource`: bygget, lagrets plats, lagrets kollision) och
  ger en `LabelFrame` (synlighetsbilden eller nullptr, baserna, om en toning
  pågår). Den PROJICERAR ankarna i double (`Projection`), lägger varje låda
  på skärmen ur glyflayouten (`labelGeometry` — exakt det Glyph-programmet
  ritar, delad med träffprovet), placerar med `placeLabelsInOrder` och
  laddar upp BARA de texlar som ändrats. **En låda HELT UTANFÖR ytan
  placeras inte** (etapp 11, MapLibres regel): den döljs och tränger inte
  undan någon som syns, och en etikett som panoreras in får sitt mål då.
  ORDNINGEN (`LabelState::order`) räknas bara när körningarna ändrats
  (`ordna`). INDEXET ÄR GLOBALT PER RITYTA OCH FÖNSTER: körningens BAS
  (dess plats i ritordningen) + etikettens index i bygget — basen går som
  uniform per rit-anrop (`MeshDraw::labelBase`), så en körning som byggs om
  flyttar ingen annan körnings vertex. SAMMA INDATA PLACERAR INTE OM
  (kameran, tätheten och körningarna med byggets löpnummer jämförs). Nya
  etiketter (en körning byggd om, eller baserna bytta av en `moveLayer`)
  får målet direkt utan toning, med en uppladdning av hela
  synlighetsbilden. Ingen kolliderande etikett = ingen bild, alla synliga.
  **Etapp 3:** med `PaintContext::view3D` satt projiceras ankarna med
  3D-vyn (`Projection(View3D)`, ankarets z ur `LabelInfo::z`), och vyn är
  en del av cachens jämförelse (`sameView3D`). `LayerFrame` bor här.
  **Etapp 10:** placeringen sparas i ritningens plats
  (`PaintContext::labelSlot`): en del-vy placerar sina etiketter för sig,
  och bilden ur renderToBitmap i sin egen plats; ett externt mål n har plats
  `kExternalLabels − n` (scene.h). **CANVAS_3D etapp 9:** en
  LIGGANDE etiketts låda (`groundGeometry`) är den PROJICERADE quadens
  axelparallella skärmruta — Glyph-programmets `glyphGround` räknat i double
  (baslinjen, "nedåt" vänd så att ramen aldrig speglas, em-storleken vid
  ankaret vinkelrätt mot blicken). `LayerFrame` bär `terrain` och framens
  draperingar (`drapes`).
- **`hit_index.h/.cpp`** (etapp 8) — INTERN: TRÄFFPROVET (REQ3: `HitQuery::zoom`, 0 = okänd; golvets räckvidd i rutnätet). `hitLayer(L, q,
  res, labels)` → `LayerHit` (formens index och, för en MARKÖRFORM, PUNKTEN
  — REQ8 20: den träffade vars mitt ligger närmast pekaren, vid lika den
  senare, `nearer`), ÖVERST FÖRST (i ByKind rangen — text över linjer över
  ytor över bilder — sedan det senare indexet). Avståndet räknas i SKÄRMENS
  fysiska px med `Projection`: en världskant skärs mot närplanet i
  klipprymden innan den projiceras, en yta prövas jämn–udda i världen där
  pekarens stråle når planet (hålen ute), en linjebredd i VÄRLDEN i planet
  med toleransen omräknad vid pekaren (`pointerScale`), markörer och bilder
  som de ritas — en markör kring sin FÖRANKRADE mitt (`forEachMarker`), en
  bildmarkör som hela sin ruta och konturen bara för en formmarkör; en bild
  som sin RING (`imageRing` ur `imageCornersOf`, `imageQuadOk` först) med
  samma inuti- och kantprov som en yta. INSIDAN (REQ8 17, `insideCounts`) är
  en synlig fyllning eller `Stroke::hitInside` — också på en linje och utan
  synlig del: rutnätet tar formen och rangen är Fill. En KONTUR träffas inom
  halva bredden från sina segment (runda skarvar och ändar, heldragna streck
  — skriven gräns). Text och kolliderande markörer går genom ETIKETTERNA
  (`forEachShownLabel`): lådan ur det RITADE bygget (`findGlyphs`) och bara
  när placeringen visar den (`LabelState::shown`). Det SPATIALA RUTNÄTET
  (`HitGrid`, hit_index.h) byggs lat per lager och gäller för
  `shapesVersion`; `candidates` blåser upp en SKÄRMRUTA (pekarens punkt eller
  rutan) med toleransen och `reach`, projicerar den till planet och prövar
  de celler den täcker — alla när den når ovanför horisonten. Planfrågorna
  går till `Projection::ground()`/`groundScale()` (båda kamerorna); `marker3`
  och `text3` prövas med ankarets z, och formerna i rymden är `unhittable`
  här — de plockas i pick3d.cpp. **Etapp 7:** `HitQuery::hasGround` —
  TERRÄNGENS träffpunkt i stället för planet z = 0: ytorna och bilderna i
  världens xy där, linjerna och kanterna som ett xy-avstånd med skärmmåtten
  omräknade vid punkten (`groundPxPerUnit`); markörerna och etiketterna står
  kvar där de ritas. **PRESTANDA C5:** `HitQuery::noGround` — terräng ritas
  men inte under pekaren: `hitLayer` hoppar över linjer, ytor och bilder
  (de finns bara i draperingen); markörerna och etiketterna prövas. Och
  markörens element är `std::int64_t{li.sub}` — `? li.sub : -1` blev
  osignerat och gav en text 4294967295 (kraschen vid hover, 0.1.9).
  **REQ8 18:** `HitQuery::tolWorld` — anropets tolerans i
  världsenheter, lagd till `tolPx` vid pekaren. **REQ8 19, RUTAN:**
  `hitLayerRect(L, q, ScreenRect, fönster, …)` — konturerna med MITTLINJEN
  (fönstret: varje ändlig punkt framför närplanet och i rutan; korsningen:
  en avskuren kant som når den, Liang–Barsky), insidan genom rutans mitt i
  världen (`rectInsideRings`), markörerna PUNKT FÖR PUNKT och etiketterna
  som skiva eller vriden låda (`discInRect`/`boxInRect`).
- **`mesh_data.h`** (CANVAS_3D etapp 4, 6 och 7) — INTERN: det `Mesh` och
  `PointCloud` pekar på. **`MeshData`**: CPU-kopian i ORIGINALETS ordning
  (lägen relativt `origin`), nivåerna med `count` KLAMPAT (aldrig
  `kRangeToEnd`), lådan i världen, SEGMENTEN, ändringsloggen (`version`,
  `base`, `changes` med ström och lägesintervall — `markChanged`), plockningens
  post `MeshPicking` och per-mesh-loggflaggor. **`MeshSegment`**: ett
  SAMMANHÄNGANDE stycke av originalets element om högst `kMeshSegmentBytes`
  (64 MiB) vertexdata OCH lika mycket index — utan index ett intervall av
  lägen; med index odelat meshens egna lägen och index; delat `vertices`
  (globala lägen VÄXANDE, lokal → global — en ändrad följd globala lägen är
  då en följd lokala) och LOKALA `indices` — och meshens nivåer i segmentets
  lokala element. Tabellen tillbaka är `firstTriangle()`. **`meshGeneration()`**
  (PRESTANDA C3) — en atomisk räknare för hela processen, stegad bara när en
  meshs LÄGEN ändras (färgerna flyttar ingen låda); scenens låda och
  terrängens rutnät har den i nyckeln i stället för en vandring. **`MeshAccess`**
  (handtagens data för ramverket och grindarna), `vertexBytes` (CPU-
  strömmarnas byte — GPU:ns packning är aldrig större), `segmentMesh(d,
  maxBytes)`, `localRange`, **`lodLevel`** (`kWholeMesh` utan nivåer,
  `kNoLevel` bortom den sista — ritningen och plockningen väljer med SAMMA
  regel), `meshBvh`, och `PointChunk`/`PointCloudData` (lägen relativt SIN
  chunks origo, `order` tillbaka till indatan — tom när molnet inte delats).
- **`mesh.cpp`** — `Mesh`: prövningen (`descOk` — varje avslag loggat där det
  upptäcks),
  segmenten (utan index på triangelgränser; med index GIRIGT i
  triangelordning — ett nytt segment när nästa triangels NYA lägen eller dess
  index inte ryms — och varje delat segment ommappat med växande globala
  lägen), ändringsloggen, nivåvalet och den lata BVH:n (`std::call_once` i
  `MeshPicking`, som `setPositions` BYTER UT).
- **`point_cloud.cpp`** — `PointCloud`: k-d-delningen (mitt i längs lådans
  längsta axel med `nth_element`, lika läge i indexordning — delningen blir
  densamma i varje standardbibliotek — tills ≤ 65 536 punkter), varje chunk i
  indatans ordning med origot i sin låda.
- **`bvh.h/.cpp`** (etapp 7) — plockningens BVH: BINNAD SAH (16 fack per
  axel, löv ≤ 8, traverseringskostnad ett triangelprov), lådorna exakt lägenas
  min/max i float, och djupet begränsat — efter `kBvhSahDepth` (64) delas
  resten mitt i på antalet, så `kBvhMaxDepth` (128, traversalens FASTA stack)
  aldrig nås, hur patologiskt lägena än ligger. Det ROBUSTA plattprovet
  `rayHitsNode` (en gräns som blir NaN — 0 · ∞, strålen I lådans sidoplan —
  begränsar inget, och utgången skjuts ut med 1 + 2γ₃, Ize) och
  `traverseRay` (närmaste barnet först, besökaren KRYMPER tMax, en låda som
  börjar exakt på tMax besöks — lika avstånd). **`traverseNear`** (etapp 7,
  skärmtoleranserna): varje löv vars låda, uppblåst med `reach(r)` — r det
  största avståndet från strålens början till lådan — strålen når; en
  toleranskon kring strålen ryms då i den uppblåsta lådan (KONSERVATIV,
  besökaren prövar exakt). Uppblåsningen avrundas utåt till float, och
  stacken är traversalens fasta.
- **`pick3d.h/.cpp`** (etapp 7) — plockningens KÄRNA utan ritytan:
  **`pickMesh(mesh, stråle, MeshPick)`** → `MeshHit` (ORIGINALETS
  triangelindex, t, avstånd, punkt, normal efter vindningen, framsida och
  barycentriska) — Möller–Trumbore i double i meshens LOKALA rymd (strålens
  början minus origot, i double), båda sidorna, närmast vinner och lika
  avstånd lägsta index; `opaqueOnly` (alfa < 255 i något hörn; utan
  färgström ogenomskinligt), `maxDistance`, den RITADE nivån och stilens
  intervall (snittade till ett triangelintervall). **`rayPlane`** genom
  fasadens `gfx::intersect`. `castRayInScene(scen, stråle, options, vy*, pxPerPx,
  pxPerMm)` är `Canvas::castRay`s kärna, med vyn (fönstrets eller ett externt
  måls) som skärmstorlek och LOD räknas mot. **RITYTANS PLOCKNING (etapp 7)** ovanpå:
  **`pickShape3D(form, PickContext, ut)`** för Mesh, Area3 (ytan och
  konturen), Instances, Points3 och Lines3 → `SceneHit` (sort, element,
  instans, punkt, normal, avstånd längs strålen, SKÄRMAVSTÅNDET och om ytan
  SKYMMER). Meshens RAM är scene3d.cpp:s — origo + at + R · (skala · läget),
  en skärmkonstant mesh skalad med storleken × världen per px vid origot
  (SCREEN_SIZED), instansens ram utanpå prototypens (transform3d.h) — och
  strålen förs IN i den (t är detsamma); normalen tar invers-transponatet
  och vänds under en speglande ram. DEN RITADE NIVÅN: avståndet från ögat
  till den placerade lådan (origot för en skärmkonstant), per cell för
  instanserna — samma regel som ritningen (`lodLevel`); `finest` tar nivå 0
  (`heightAt`). Världslådan sållar varje mesh före BVH:n (ett lat bygge per
  mesh annars). SKÄRMTOLERANSERNA (`ScreenTol`: px/mm som fysiska px,
  världsenheter omräknade vid punkten) prövas i double på skärmen: hörnen,
  kanterna (avskurna mot närplanet i klipprymden, punkten PERSPEKTIVKORREKT
  ur skärmens parameter), punkterna (toleransen + halva storleken) och
  linjesegmenten (halva bredden + toleransen) — konen sållar BVH:n
  (`reachWorld`: perspektivets världsenheter per px växer med djupet ≤ r)
  och chunkarna. ALLA inom toleransen svaras med sitt skärmavstånd —
  ritytan väljer det närmaste SYNLIGA per form och sort. **`nearestOccluder`**
  är skymningens synlinje: den närmaste ogenomskinliga ytan (`opaqueOnly` —
  en genomskinlig triangel framför får inte dölja en ogenomskinlig bakom).
  PRESTANDA C5: `Occluder` bär formens LÅDA (`occluder(post, opacitet)` ur
  `objectBox(post)`, en gång per fråga), skymmarna prövas i ordning efter
  var strålen går in i lådan (`rayBoxEntry`), bästa träff hittills blir
  `maxDistance` — som når `pickMesh`s `tMax` och lådgrinden `reaches` — och
  sökningen SLUTAR vid den första låda som börjar bortom den + marginalen.
  `pickShape3D(post, …)` sållar Mesh och Instances i världens enhet på den
  cachade lådan FÖRE placeringen, `PickContext::surface = false` plockar bara
  hörn, kanter, punkter och linjer, och `occlusionReach(d)` är taket bakom
  en skymmare (`castRay`). `occlusionSlack` (det som ligger PÅ skymmaren skyms inte) och
  `verticalRayDown` (`heightAt`s stråle) bär plockningens tal, så att
  widgetfilen inte gör det. Ingen kamera, inget fönster — ritytan (canvas.cpp) samlar lagren, vyn,
  2D-formerna, sorteringen och skymningen.
- **`programs3d.h/.cpp`** (CANVAS_3D etapp 4–6, planens §3.7) — INTERN: de
  INBYGGDA 3D-programmen MESH och POINTS i HLSL, GLSL 330 och MSL (Metal
  BLINT), kompilerade genom fasadens EGEN väg (`createProgram` +
  `MeshProgram::Custom`) — inga nya `MeshProgram`-poster. En FABRIK utan
  renderer: `meshProgramDesc`/`pointProgramDesc` (källorna, definitionerna,
  `userBytes`, texturantalet — namnet bär varianten, "glaze_mesh.normal.uv"),
  `meshLayout`/`pointLayout`, `meshSlots`/`pointSlots` (texturen, rampen,
  draperingen i den ordningen, platserna 0 … count − 1 utan lucka),
  `packMeshVertex`/`packPoint` (ETT element — varje packningsloop går genom
  samma form) och GlazeUser-blocken `MeshUniforms` (288 byte) och
  `PointUniforms` (80 byte) med NEUTRALA förval och sina fyllare. Varianterna
  är definitioner: Mesh `HAS_NORMAL/COLOR/UV/SCALAR`, `INSTANCED`,
  `SCREEN_SIZED` och `DRAPED` (draperingen över basen, uv ur världens xy mot
  plattans låda) = 128 (`MeshVariant::key()`/`fromKey`, cachens nyckel),
  Points `HAS_COLOR/SCALAR` = 4; texturens register/samplernamn/plats
  definieras av fabriken per språk (`TEX_*`, likadant RAMP och DRAPE).
  STRÖMMARNA (meshens vertex sammanflätad i ström 0, instansen 52 byte i
  ström 1 — färgen MULTIPLICERAR, alfa 0 = ingen; en PUNKT är ett element i
  INSTANSSTRÖMMEN, ritad med 6 vertex utan vertexström) och MODELLEN per
  fragment (basen → justeringen `glaze_colorAdjust` → ljuset → dimman →
  alfan) står i headern. Utan normaler en PLATT normal ur derivatorna;
  SCREEN_SIZED skalar till storleken på ankarets djup; punkterna bakom
  närplanet och bortom det bortre ritas inte (explicit — D3D11 klipper inte
  z utan djuptest). RAMPEN samplas med u = (½ + t·(n − 1)) / n och bildens
  EGET filter, så rampbilden ska ha Linear. Grind:
  `tests/mesh_program_gate.cpp` (G9 del E och F utan ritytan). Ritningen,
  kompileringen per fönster och översättningen av `Lighting`/`MeshStyle`/
  `PointStyle` till blocken bor i scene3d och residency nedan.
- **`transform3d.h/.cpp`** (CANVAS_3D etapp 4–5) — INTERN: en placerings
  transform som `Affine3` i double — `rotationScale(yaw, pitch, roll, skala,
  t)` med R = Rz(−yaw) · Rx(pitch) · Ry(roll) och skalan först (style3d.h:s
  konvention, EN definition för meshens modell, instansernas rader,
  cellernas lådor och plockningen), `compose`, `invert`, `transformBox` (de
  åtta hörnen) och `distanceToBox` (detaljnivåns mått), och `toRows` till
  programmets tre rader.
- **`instances.h/.cpp`** (CANVAS_3D etapp 5) — INTERN: INSTANSERNAS CELLER,
  byggda EN gång i `Shape::instances` (`buildInstanceSet`): BLOCK om
  `kChunkCell` med ett origo i double var (RTC) och CELLER inom blocken —
  sidan s ur utbredningen (de utsträckta axlarnas produkt / antalet celler,
  ~`kInstancesPerCell` = 64 per cell; en axel kortare än s räknas inte),
  Morton-ordnade inom blocket, block i koordinatordning, lika lägen i
  indatans ordning. Raderna (`MeshInstance`, 52 byte) i cellordning
  relativa sitt blocks origo, `order` tillbaka till indatan, cellens låda
  (prototypen placerad) och `anchors` (lägena — en skärmkonstant prototyp),
  `maxScale`. En instans som inte är ändlig hoppas över och loggas.
- **`residency.h/.cpp`** (CANVAS_3D etapp 4–6) — INTERN: FÖREMÅLENS RESIDENS
  per fönster (`CanvasResources::objects()`). EN KOPIA PER DATA (meshens,
  mängdens, molnets id): meshens segment som var sin vertex- och
  indexbuffert i strömmarna meshen BÄR (läge, normal, färg, uv, skalär —
  layouten är de strömmarna; programvarianten får läsa färre), molnets
  chunkar som var sin buffert. Uppladdningen STRÖMMAR i hela element inom
  fönstrets budget (`ImageCache::uploadRoom`/`spendUpload`/`deferUpload` —
  delad med bilderna; framens första bit får hela budgeten), och det som
  väntar gör att fönstret ritar om nästa frame. PLATSEN (`ShapeEntry::id`)
  VISAR en kopia OCH FORMEN den hör till (`Shown`): byts datan ritas den
  gamla formen med sin gamla kopia tills ALL ny data är komplett —
  dubbelbuffringen — och den gamla går till `retired*`. ÄNDRINGSLOGGEN
  (`catchUp`): bara de ändrade lägena, omsorterade per segment (ett delat
  segments globala lägen är växande, så de lokala är ett intervall), hela
  vertex, allt i EN frame (annars nästa), och kopians låda följer med;
  räcker loggen inte blir det en ny kopia. Programmen per variant (latent,
  ett ogiltigt sparas också) och `precompile()` — de vanligaste en gång per
  fönster. Räknarna `copiesMade`/`meshCount`/… är grindarnas. **Etapp 8:**
  meshens user0–3 packas med i kopian (`GpuMesh::user`, platserna
  kUser0–3) — de EGNA programmens strömmar; de inbyggda läser dem inte.
- **`scene3d.h/.cpp`** (CANVAS_3D etapp 4–6) — INTERN: FÖREMÅLEN I RITYTAN.
  Förberedelsen (residensen för varje föremål i ett synligt lager, också
  utanför bild, programmen förkompilerade första gången, och texturerna och
  ramperna för ett ritat lager — allt före målet) är sedan PRESTANDA C6
  `queueObjects` (körningens föremål i kön `UploadItem` — de som syns i vyn,
  `objectInView` med samma låda som ritningen, med avståndet; resten för
  sig) och `prepareObject` per post, i den ordning scene.h `prepareUploads`
  ger. En skärmkonstant mesh räknas som synlig. Och `drawObjects`: per
  plats det den visar, med GALLRINGEN — de åtta hörnen homogent mot
  synpyramidens sex plan, gallrat när alla ligger utanför SAMMA plan (inte
  boxVisible:s "ett hörn bakom ritar alltid": då hade allt bakom kameran
  ritats) —, DETALJNIVÅN ur avståndet ögat–lådan (samma `lodLevel` som
  plockningen, snittad med stilens `range`), ett anrop per segment med
  anropets origo i double, och INSTANSERNA cell för cell, sammanslagna per
  block och nivå. **`meshUniforms`** är `MeshStyle` + `Lighting` → blocket
  (opaciteten klampad, justeringen genom `gfx::adjustFactor`). I 2D-vägen (ett Ground-lager med
  föremål) ritas de genom `toView3D` över körningens höjder och laddas upp i
  ritningen. **EGNA PROGRAM (etapp 8):** `bindCustom` — programmet för
  fönstret, `layoutFits` (fasadens platsregel prövad FÖRE anropet), texturerna
  (en som väntar på budgeten väntar en frame) och GlazeUser ur stilens tal
  (`userBlock`, utfyllt med nollor) — svarar Ready, Wait eller Fallback;
  RESERVEN är det inbyggda programmet, loggad en gång per rityta och namn
  (`firstFallback`). `customFrame` viker in placeringens vridning och skala i
  GlazeFrame:s matriser och för ögat och strålbasen in i meshens ram; egna
  program på INSTANSER faller tillbaka (prototypens placering bor i det
  inbyggda blocket). `prepareScreenPass`/`drawScreenPass`: tre vertex utan
  ström, GlazeFrame med origo i ögat — och reserven är `fallback`-färgen
  över ritytans ruta. **Etapp 9:** `drawMeshObject(…, entryId)` binder
  terrängformens drapering (`DrapeFrame`) i DRAPED-varianten.

## Fallgropar

- **Golvet och taket är ANROPETS uniform, inte vertexens** (REQ3 8): olika
  gränser är olika körningar, och allt som mäter en etikett på skärmen
  (labels, hit_index, gallringen, `measureLabel`) klampar med `glyphLimits`.
  **En bild utan pixlar (`setKeepPixels(false)`) går inte att ladda upp i ett
  nytt fönster** — `get` ger en negativ post, och LRU:n vräker den aldrig.
- **`Projection` är två saker.** I `glaze::detail` är det KLASSEN i
  camera_math.h, i `glaze` ENUMET på `Camera3D` — och inne i `detail`
  skuggar klassen enumet, så där skrivs `glaze::Projection::Perspective`.
  Ett okvalificerat `Projection::Perspective` i detail-kod kompilerar inte.
- **3D-kamerans upp är elevationens TANGENT, inte världens z.** En
  upp-vektor ur världens z (den klassiska `lookAt`) är parallell med blicken
  rakt uppifrån och vänder bilden förbi polen — tumlingen blir en kullerbytta.
  Inom ±90° ger de två samma bild, så bara elevationer vid och förbi 90°
  ser skillnaden — scene_math_gate fall 15 (tumlingen) och facitfallet rakt
  uppifrån fäller den.
- **`toView3D` och `Projection` ur en MapView är två olika vägar till
  samma bild** — x/y lika inom 1e-6 px och närplanet detsamma i perspektiv,
  men djupet skiljer: 2D-vägen gallrar med kartmatrisen (oändligt bortre
  plan, ortografiskt ±d), 3D-målet med vyns ändliga plan ur z-spannet.

- **Placeringens överlapp är ytans** (kanter som möts krockar inte). Cellnumret
  klampas i flyttal FÖRE heltalet (en låda 10¹² px bort spräcker annars en
  int), och monotont — så två krockande lådor alltid delar en cell.
- **Id, aldrig adress.** En död bilds post släpps i nästa `trim` (dess
  `weak_ptr` har gått ut); en ny bild på samma adress har ett annat id och
  kan aldrig ta den gamla texturen. Samma regel för chunkarna och
  körningarna i `CanvasResources`.
- **Ändringsloggen ska räcka för det LÅNGSAMMASTE fönstret** — därför en
  logg och inte en enda smutsrektangel: ett fönster som inte ritat på två
  versioner måste få båda rektanglarna (B5 mäter det). Chunkarnas logg
  likadant (32 poster).
- **Resident före gallring.** `drawChunks` synkar varje chunk innan den
  prövar synligheten; vänds ordningen laddar en panorering in över en
  oritad chunk upp den (painter_gate R fäller det).
- **Text byggs per fönster, inte vid `addShape`.** Fontens rasterisering och
  atlasen är rendererns — en avvikelse från planens "CPU-geometrin byggs vid
  addShape", som gäller linjerna, ytorna och bilderna.
- **Bara mellan frames släpps något.** Ett ombyggt textbygge, en buffert som
  växte och en ramp som föll ur taket läggs i `retired*` och släpps i nästa `trim()`.
- **En post släpps när dess KÄLLA dött** — en rityta som byter fönster (en
  flytande dockningspanel, DOCKNING E3) laddas upp på nytt i det nya och
  lämnar sina buffertar i det gamla tills det stängs eller ritytan rivs
  (skriven gräns i resources.h).
- **Etikettens index är byggets, texeln ritytans.** `GlyphVertex::label`
  är indexet i KÖRNINGENS bygge; texeln i synlighetsbilden är körningens
  bas + det. Den som ändrar ordningen körningarna samlas i
  (`prepareLayers`) ändrar baserna — ritningen och placeringen läser samma
  `LabelState::runs`, så de kan inte glida isär så länge båda går genom
  `updateLabels` i samma ritning.
- **3D-målet ritas om bara när `damageStamp()` ändras** (eller rutan,
  skalan, mm-tätheten). Något som ändrar vad ritytan ritar UTAN att
  invalidera den — en resurs som blir klar i efterhand — måste invalidera
  ritytan, annars visar kompositen den gamla bilden; och `onPaint` måste
  vara deterministisk. Toningen och väntande draperingar invaliderar själva. En
  KAMERARÖRELSE SOM STEGAS I RITNINGEN (glidningarna, trögheten, snäppet)
  invaliderar inte på sitt SISTA steg — då hade målet stått kvar på förra
  stegets kamera; `Canvas::cameraLanded` stegar stämpeln där (bara i
  3D-målet — en frame till, ren komposit). canvas3d_gate V mäter det.
- **Varje ritväg över lagren frågar `layerDrawn`** — vyn ingår i den sedan
  etapp 10. En ny väg som bara prövar `visible` och zoomintervallet ritar ett
  lager i en vy det inte hör till (navigeringskuben i huvudvyn), och en ny
  förberedelse som inte respekterar `f.view` gör en del-vys lager residenta
  för sent (inne i målet, §3.12).
- **En ritning med en annan kamera får aldrig placera i skärmens plats.**
  Etikettplaceringen (`LabelState`) är också träffprovets svar och toningens
  läge: renderToBitmap i plats 0 hade fått nästa skärmritning att placera om
  — och tona från bildens placering — och träffprovet att svara mot bildens
  kamera fram till dess. Därför en plats per vy och en för bilden (G9 I5
  mäter att skärmen inte placerar om).
- **En vys klipplan ur hela scenens låda förstör dess djup.** En kub vid
  origo i en karta vid SWEREF fick då ett djupintervall på miljontals
  enheter, och två ytor en tusendel isär blev samma tal i D32F — därför
  `sceneBox(scen, vy)` (G9 I1b mäter det).
- **Vyns nummer flyttar vid `removeView`** — lagrens vylistor följer med
  (canvas.cpp), målen och placeringarna gör det inte: de nycklas på numret
  och ritas om eftersom sättaren invaliderar (stämpeln), och de som blev över
  släpps av `dropViews`.
- **En etikett i ett World-lager skriver aldrig djup** (`gfxState`):
  gloria och fyllning ligger på samma djup, och med skrivning hade glorian
  skymt fyllningen. Etiketterna djuptestas mot World men skymmer inget.
- **Träffprovet bygger aldrig.** Det kan anropas mitt i en händelse, där
  en atlas inte får laddas upp (GL kräver en aktuell kontext, Metal en
  öppen frame) — därför `findGlyphs` och inte `glyphs`, och därför träffas
  en text först när den ritats i sin nuvarande form.
- **Varje väg som ändrar formerna stegar `shapesVersion`** — glöms den
  pekar rutnätet in i `slots` med gamla index (rutnätet kontrollerar
  dessutom antalet, som ett skydd).
- **3D-PRESTANDAN ÄR GRINDAD — `tests/canvas_perf3d.cpp` (G10).** U, R, S,
  L, C, T, P, Ä, M (terrängen — sedan PRESTANDA M5–M8, M10, M12 och zoomen
  räknad tills det står still) och D: rit-anropen per frame ≤ det innehållet
  kräver + 8, räknat ur scenens former med byggarnas kontrakt. Punkterna är fragmentbundna på
  UHD, och en kantmarginal om ½ px gjorde dem LÅNGSAMMARE — ingen trimning.
- **PRESTANDAN ÄR GRINDAD — `tests/canvas_perf.cpp` (G7).** En
  kameraändring får ladda upp högst en byte per etikett och ingen buffert,
  inget byggs om, och rit-anropen ≤ innehållets + 8. Lätt att råka ta
  tillbaka (etapp 11): (1) placeringen ser BARA lådorna som når ytan; (2)
  ordningen sorteras när etiketterna ändras, inte per kameraändring; (3)
  ingen trigonometri per etikett och kameraändring (`cosA`/`sinA` i bygget).
- **Strict med växlande nycklar är ett anrop per del.** En yta med kontur
  eller en väg följd av sitt namn byter körning två gånger per form — G7:s
  karta i Strict: 10 663 anrop mot 60 i ByKind. Det är Strict:s löfte (exakt
  målarordning), inte ett fel; ett kartlager ska vara ByKind (canvas.h).
- **Plockningen läser CPU-kopian, inte segmenten.** Kopian står i
  originalets ordning, så träffens triangelindex är originalets utan
  omväg; segmenten är uppladdningens vyer. Den som flyttar plockningen till
  segmenten måste lägga på `firstTriangle()` — pick_gate P1 kör ett DELAT mesh,
  så ett glömt tillägg syns där.
- **Segmentgränsen gäller index också.** D3D11:s IA-gräns gäller varje
  buffert IA:n läser, alltså också indexbufferten: ett rutnät med stor
  återanvändning ryms i 64 MiB lägen men inte i 64 MiB index (geom_gate M3).
- **`setPositions` BYTER plockningens post, den nollställer den inte.** En
  `std::once_flag` kan inte återställas; den som håller den gamla posten (en
  pågående plockning) läser färdigt i den gamla BVH:n. Och ingen ändring får
  pågå medan en arbetartråd kör `preparePicking` — lägena läses utan lås.
- **BVH:ns djup är traversalens stack.** Bygget lovar högst
  `kBvhSahDepth` + 32 nivåer genom att dela mitt i efter SAH-djupet; den som
  höjer `kBvhSahDepth` utan `kBvhMaxDepth` spräcker stacken på en
  patologisk mesh (en SAH-kedja som skalar av en triangel per nivå).
- **Ett avslag loggas DÄR det upptäcks, med texten direkt i `gfx::log`**
  (`descOk`, `countsOk`, pick3d.cpp): lang_gate del 4 undantar bara
  literaler som står som argument till `gfx::log(...)` — en orsak som
  returneras som `const char*` och loggas via `%s` fälls som användartext.
- **NaN i plattprovet är rätt svar, inte ett fel.** En stråle som LIGGER i en
  lådas sidoplan ger 0 · ∞; jämförelserna i `rayHitsNode` är skrivna så att
  NaN-gränsen inte begränsar något. `std::max(tn, t0)`/`std::min(tf, t1)` i
  den ordningen sprider NaN, och eftersom z prövas SIST missar då en vågrät
  stråle i en nods z-sidoplan (en ortografisk sidovy) — bara pick_gate B2:s
  STÅENDE rutnät fäller det sabotaget.
- **Shaderkällorna står i ett `lang_gate:av`-block** (programs3d.cpp):
  lang_gate del 4 läser också råa literaler, och "float4 model" (två ord)
  och "HAS_NORMAL"/"GlazeTexture" (versal först) hade fällts som
  användartext. Blocket bär skälet; en ny källa eller ett nytt
  definitionsnamn hör hemma INNANFÖR det.
- **Reserverade ord i tre språk.** `point` är en primitivtyp i HLSL
  (geometrishaderns) — GLSL kompilerade, D3D11 vägrade; Points-blockets fält
  heter därför `marker`. Undvik också `line`, `triangle`, `sample`,
  `texture`, `vector`, `matrix` (HLSL) och `filter`, `input`, `output`,
  `common`, `active` (GLSL:s reserverade) som namn i en delad källa.
- **Den platta normalens tecken är språkets, framsidan fasadens.**
  `cross(ddy, ddx)` i HLSL och MSL men `cross(dFdx, dFdy)` i GLSL (y åt olika
  håll); vindningen är densamma i alla tre — på D3D11 genom 2D-rasteriserarens
  `FrontCounterClockwise` (mesh_program_gate E2/E3 "utan djup").
- **Versalerna i en kommentar kan fälla bakändegrinden.** Rot-CLAUDE.md:s
  grep efter backend-typer är skiftlägeskänslig, och ett svenskt ord i
  VERSALER kan bära ett av typnamnen inuti sig (speglande gjorde det —
  grafikgränssnittets trebokstavsnamn står mitt i). Skriv sådana ord med
  gemener, och citera inte grindens mönster här.
- **Dubbelbuffringen är per FORMPLATS, inte per form.** `setShape` behåller
  platsen (`ShapeEntry::id`), så den gamla meshen ritas tills den nya är
  uppe; `setShapes` och `removeShape` + `addShape` ger NYA platser, och en
  ny plats ritar ingenting förrän dess data är komplett. Och platsen visar
  den gamla FORMEN med sin gamla data — en ny placering eller stil på samma
  mesh syns direkt (ingen data att vänta på).
- **Budgeten är bildcachens.** Residensen frågar `Ctx::images`
  (`uploadRoom`/`spendUpload`/`deferUpload`); en Ctx utan bildcache laddar
  upp utan budget. Det som strömmar gör att FÖNSTRET ritar om (bildcachens
  `uploadsDeferred` → `AppWindow::invalidate`), och 3D-målet ritas då om
  eftersom `damageStamp` räknar fönstrets fulla invalideringar — tar man
  bort den kopplingen fastnar en halv uppladdning tills nästa
  invalidering (canvas_perf3d S).
- **Föremålens uppladdningar sker i förberedelsen**, före målet (§3.12):
  `drawObjects` laddar bara upp i 2D-vägen (`!f.view3D`). Ett föremål som
  inte hann upp ritas inte (eller den gamla datan) — aldrig en halv kopia.
- **En skärmkonstant mesh har ingen världslåda.** `objectBox` svarar med
  origot, så scenens låda — och därmed 3D-kamerans bortre plan, 1,05 × det
  bortersta djupet — vet inte hur stor den blir; den bortersta sfären i en
  scen av bara skärmkonstanta sfärer kan klippas av det bortre planet om den
  är större än marginalen (skriven gräns). Gallringen mäter den i px kring origot.
- **Layouten är kopians strömmar, varianten kan läsa färre.** En mesh med
  skalärer ritad utan ramp, eller med uv utan textur, binder en layout med
  ett attribut programmet inte läser — fasadens regel godtar det (D3D11:s
  input layout och GL:s attributpekare likaså); på Metal är det ett
  antagande (doc/os/macos/TESTS.md).
- **Plockningen följer RITNINGENS regler, inte sina egna** (etapp 7): den
  ritade detaljnivån, lagren i vyn (`layerDrawn`s villkor), skymmarna = ytor
  i djupskrivande lager i 3D-målet, och MARKDJUPET — ett ritat Ground-lager
  som målar marken (`paintsGround`; inte ett tomt) gör att planet z = 0
  skymmer det under sig (pick_gate R8b). Ändras
  en regel i ritningen (terrängen stänger markdjupet i etapp 9) måste
  `Canvas::hitTest` följa med; pick_gate R8b och R10 fäller en glidning.
- **Det närmaste på skärmen kan vara skymt.** Hörn, kanter, punkter och
  linjesegment svaras ALLA inom toleransen (`SceneHit::screen`), och ritytan
  prövar synlinjerna i skärmavståndets ordning tills ett syns — att välja
  det närmaste FÖRE skymningen tappar ett synligt hörn bredvid ett skymt (pick_gate R12).
- **Med terräng finns Ground-lagrens geometri BARA i draperingen** (etapp
  9) — i ritningen (`drawGround` Geometry ritar ingen 2D-geometri, och
  markdjupet uteblir) OCH i plockningen (`Canvas::hitTest`: 2D-formerna bara
  där terrängen ligger under pekaren, inget markplan, och den liggande
  texten på terräng träffas inte). En ny ritväg eller fråga som glömmer det
  ritar eller träffar planet z = 0 under terrängen — G9 H3/H3b fäller det.
- **Draperingens nyckel är CHUNKENS, inte formens.** En ändrad form i en
  chunk som når plattan ritar om plattan, också om formen själv ligger
  utanför; en chunk som inte når den ritar inte om (G9 H6). Ett kornigare
  test hade krävt formernas lådor per frame.
- **px och mm i draperingen är MÅLETS px.** En linje i px blir lika bred
  på skärmen bara där målets klass motsvarar plattans avtryck; under tilt
  och mellan klasser skiljer den (planens osäkerhet nr 2, MapLibres
  begränsning). Den liggande textens storlek likaså.
- **En reserv loggas en gång per rityta OCH NAMN** (`fallbackLogged`) — ett
  program som går sönder efter `setProgram` loggar alltså inte igen under
  samma namn; felet står i `programError`.
- **Lagrets PLATS är ingen identitet** (REQ2_RITYTAN, `moveLayer`). Ingen
  cache får nycklas på `li`: fönstrets kopior nycklas på chunkens,
  körningens och lagrets id, och det som ÄR platsberoende (placeringen i
  `LayerFrame`, etiketternas lager och baser, draperingens nyckel) räknas om
  i varje ritning. En ny nyckel som blandar in lagren i vektorns ordning gör
  en flytt till en ombyggnad — därför är TERRÄNGENS REGIONNYCKEL en SUMMA
  och inte en hash i följd (painter_gate LO7); draperingens nyckel och
  markstämpeln tar ordningen med flit.
- **En ny indata i draperingens `contentKey` ska också in i `groundStamp`**
  (PRESTANDA C2): oförändrad stämpel ⇒ nyckeln räknas inte, och plattan
  ritas aldrig om när den nya indatan ändras. Grindarna (H6c `textVersion`,
  H8 `contentGeneration`) ser bara dagens indata.
- **2D-vägen hämtar en bildkörnings bild vid dess FÖRSTA SYNLIGA chunk**
  (PRESTANDA C6) och resten i `prepareUploads` efter de synliga. En ny väg
  som hämtar bilder i chunk- eller listordning tar tillbaka det osynliga
  först (canvas_perf3d M12).
- **Lådan räknas i VÄRLDEN, träffen i meshens RAM** (PRESTANDA C5): en grind
  mot ett tak (`maxDistance`, skymmarnas utgång, `reaches`) behöver
  marginalen (`occlusionSlack`, `kLimitSlack`), annars faller en träff precis
  vid taket bort på avrundningen. `objectBox(post)` är UI-trådens cache —
  pick3d och rutnätet läser den där; en arbetstråd frågar `objectBox(form)`.
- **`layerSetVersion` stegas av VARJE sättare som ändrar lagrens mängd,
  ordning, synlighet, placering eller vyer** (canvas.cpp) — en glömd ger ett
  inaktuellt terrängrutnät (pick_gate HI). `setLayer` stegar den inte; ett
  ersatt Terrain-lager märks på sitt livstecken.
- **Textens mått och ritning delar layout, inte glyfer** (REQ2_RITYTAN):
  `gfx::measureText` tar stegen ur `SdfAtlas::advance` (`Font::measure`)
  och bygget ur de rasteriserade glyferna — EN siffra bara så länge
  backendens `measure` och `rasterGlyph` läser samma upplösta glyf och steg
  (alla tre gör det, genom reservens cache). canvas_gate M2 mäter det på
  Windows; på Metal och Linux är det ett antagande tills grinden körts där.
