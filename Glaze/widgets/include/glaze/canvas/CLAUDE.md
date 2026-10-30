# CLAUDE.md — Glaze/widgets/include/glaze/canvas

Publika headers för RITYTAN (CANVAS_2D, TODO 27) och dess 3D (CANVAS_3D,
TODO 50 — samma scen, inte en egen 3D-vy) — typerna ritytans scen byggs av,
bredvid widget-headerna i [../widgets/](../widgets/) (där widgeten `Canvas`
bor, `canvas.h` — dess API och kontrakt står i headern). Del av GlazeUI —
se [root /CLAUDE.md](../../../../../CLAUDE.md) för lagerbilden och de hårda
reglerna. Implementationerna bor i [../../../src/canvas/](../../../src/canvas/).

Allt här kommer in med paraplyet `glaze/glaze.h`.

## Filer

- **`geometry.h`** (etapp 7) — `Point`/`Rect` i DOUBLE (en SWEREF-koordinat
  får inte avrundas förrän den blivit relativ ett chunk-origo, T5),
  `Unit {Px, Mm, World}` och `Length` med `px()`/`mm()`/`world()`. **Ett
  naket tal är px** — konverteringen från float är implicit med flit, så
  `.padding = 4` läser som man menar. px är logiska (följer `uiScale`), mm är
  millimeter med linjal ur skärmens EDID och kalibreringen (oberoende av OS-
  och användarskalan, T9), world följer zoomen. **3D (CANVAS_3D etapp 2):**
  `Point3` (världen i double — appen anger alltid världskoordinater, RTC är
  ramverkets), `Float3` (lokala data relativt ett origo: meshar, punktmoln),
  `Vec3`, `Box3`, `Plane3` (punkt + normal, inte nödvändigtvis enhetlig;
  förval z upp) och `Ray3`. Världen är HÖGERHÄNT med z upp i kartvärlden.
  Egna namn med suffixet 3: `{x, y}` initierar också en `Point3`, så en
  överlagring på `Point` hade gjort befintlig kod med klammerlistor tvetydig.
- **`shape.h`** (etapp 7, T6) — EN innehållsmodell: formlistan i `.glazeui`
  och lagrens data är samma sak. **`Paint`** (en färg — implicit ur
  `gfx::Color` — eller `linear`/`radial` med `GradientStop`, fast i världen,
  pad-spridning; varje gradientform är ett eget rit-anrop, enfärgade delar
  anrop), **`Stroke`** (`paint`, `width` som `Length`, `cap` Butt/Round/
  Square, `join` Round/Miter/Bevel — förval Round, `miterLimit` SVG:s 4,
  och STRECKEN: `dash` (på, av … — ett udda antal upprepas, högst 8 efter
  upprepningen, annars loggat och heldraget), `dashUnit` (px och mm mäts på
  SKÄRMEN, world följer zoomen) och `dashOffset`; strecken har linjens
  ändform, och under tilt förkortas de med djupet — skriven begränsning — och
  sist `hitInside` (REQ8 17: insidan räknas som yta i träffprovet, också
  utan fyllning och med en osynlig kontur; ritningen läser den inte),
  **`TextStyle`** (storlek som `Length`, familj,
  fet/kursiv, färg, gloria + bredd, platta + luft, ankare, vinkel,
  `Alignment` Viewport/Map/**Ground** (CANVAS_3D etapp 9: LIGGANDE i
  markplanet vid ankaret — kartans vinkel, förkortad under tilt, storleken
  gäller vid ankaret, aldrig spegelvänd: en karta vriden 180° visar den upp
  och ner; den kolliderar med den PROJICERADE quadens skärmruta, och på
  terräng DRAPERAS den — varken kollision eller träff), förskjutning,
  radbrytning, `Align`; måtten FÅR blandas — de räknas om till storlekens
  enhet när etiketten byggs), **`TextSpan`** (CANVAS_TEXT_OCH_BILD — EN DEL
  av en etikett: `TrText text` och de valfria `color`/`bold`/`italic`, där
  ett osatt fält ärver etikettens `TextStyle`; storleken, familjen, glorian,
  plattan och ankaret är etikettens. Fältordningen är kontrakt —
  designerade initierare och `apply_props`-parsern läser den),
  **`TextMetrics`** (REQ2_RITYTAN — `Canvas::measureText`s svar: textblockets
  `width`, `height`, `lineHeight`, `baseline` (första radens, under
  överkanten) och `lines` i den begärda enheten, utan platta, luft, ankare
  och vinkel; en tom text är en rad, storleken ≤ 0 ger `lines` 0) och
  **`MarkerStyle`** (cirkel/kvadrat, storlek, fyllning, kontur utanför,
  vinkel, **`anchor`** — textens nio lägen, var punkten sitter på markören:
  Bottom är en nål PÅ punkten — och **`img`**, en `ImageSource`: BILDMARKÖREN,
  bilden upprätt i rutan `size` × `size` med bildens proportioner, utan
  form, fyllning och kontur, linjärt samplad; markörer med olika bilder
  ritas i var sitt anrop, en ogiltig bild ritar ingenting). Båda stilarna
  bär ETIKETTKOLLISIONENS fält (etapp 9):
  `collide` (text på, markör av från start — false = alltid synlig, tar
  ingen plats), `priority` (högre vinner) och `collisionPadding` (luft runt
  lådan, i valfri enhet); en bildmarkörs låda och träffyta är hela rutan.
  REQ3_RITYTAN lägger SIST i deklarationsordning: i `TextStyle` TAKET
  (`maxLines`, `maxHeight` — kortat med "…" efter `Draw::elideLines`, och
  `TextMetrics::truncated` sist), och i BÅDA stilarna golvet och taket för en
  em på skärmen (`minSize`, `maxSize` — Glyph-programmets uniform, hela
  etiketten skalas) och synligheten efter zoom (`minZoom`, `maxZoom` —
  lagrets regel, genom synlighetsbilden).
  `ImageSource` står sedan CANVAS_3D etapp 4 i `image_source.h` (nedan).
  **`Shape`** — ett VÄRDE med fabrikerna `line`/`polyline`/`arc` (Stroke),
  `polygon` (yttre ring eller ringar med hål)/`rect`/`ellipse`/`circle`/
  `path` (Paint + valfri `std::optional<Stroke>`), `path(p, stroke)` (bara
  kontur), `text` (`TrText` — översättningar följer språket), `marker`,
  `points` (ETT punktmoln är EN form) och `image(rect, källa, ImageStyle)`.
  **`ImageStyle`** (REQ8 3) ersatte det nakna opacitetstalet: `opacity`,
  `saturation`, `contrast`, `brightness` i den ordningen — MeshStyles namn och
  inbördes ordning, så de designerade initierarna skrivs likadant —
  normerade i fabriken (opaciteten 0–1, justeringen `gfx::adjustFactor`) och
  lästa med `imageStyle()`; justeringen är Image-programmets uniformer.
  **Sedan CANVAS_TEXT_OCH_BILD** också `text(p, std::vector<TextSpan>,
  stil)` (en etikett i DELAR — en atlas och ett rit-anrop per familj,
  radbrytningen går över delarna; `text()` är då TOM och `spans()` bär
  delarna) och `image(övreVänster, övreHöger, nedreHöger, nedreVänster,
  källa, ImageStyle)` — bilden PROJEKTIVT på fyra fria hörn i ringordning
  (spegelvänt går); fyrhörningen måste vara strikt konvex med ändliga hörn,
  annars ritar den ingenting, träffas inte och loggas EN gång per process.
  Fyra `Point` och inte en `std::array`, för `{x, y, b, h}` initierar också
  en array med fyra punkter. `isImageQuad()` skiljer formerna (fyra punkter
  i `points()` mot rektangelns två). **FALLGROPEN:** `{}` och en lista med
  EN bar del `{{"abc"}}` är tvetydiga mellan `TrText` och delvektorn och
  kompilerar inte — `{"abc"}` är text, `{glaze::TextSpan{"abc"}}` och
  `{{.text = "abc"}}` är delar.
  Läsvägen (`kind`, `contours`, `fill`, `stroke`, `points`, `textStyle`,
  `spans` …) är vad lagret och målaren bygger ur. **Färgerna följer inte temat** — en
  standardfärg är svart, som en penna; ytan bakom är ritytans och följer
  temat. **I RYMDEN (CANVAS_3D etapp 3):** `line3`/`polyline3` (Stroke —
  bredden i px, mm eller världsenheter, alltid extruderad mot kameran),
  `text3` (också med delar) och `marker3` med `Point3`-punkter. Rymddelen ligger i en
  `shared_ptr<const Shape3DData>`, så 2D-formerna växer inte; `is3D()` och
  `points3()` läser den. En form i rymden gör sitt lager till World
  (`LayerPlacement::Auto`, style3d.h) och tar ritytan till 3D-målet.
  **FÖREMÅLEN (CANVAS_3D etapp 4–6)**, `Kind::Mesh/Area3/Instances/Points3`:
  `mesh(Mesh, Placement, MeshStyle)`; primitiverna `box`/`sphere`/`cylinder`
  som en DELAD ENHETSMESH per process (lådan [−½, ½]³, sfären med diametern
  1, cylindern r 1 × h 1 per sidantal — en statisk cache bakom ett lås, så
  fabrikerna går i vilken tråd som helst) som placeringen skalar och flyttar
  — tusen lådor är EN mesh på GPU:n och en form byggd om varje bildruta
  laddar inget; `sphere` med radien i px/mm sätter `MeshStyle::size` =
  diametern; `cylinder` står lodrätt (negativ höjd vänds, så skalan aldrig
  speglar); `tube` och `polygon3` får EGNA meshar (origot i lådans mitt) —
  rörets diameter är data per form; `polygon3` är obelyst i fyllningens färg
  med en sluten Line3-kontur, en linjär gradient blir skalär + ramp (exakt,
  t är affint i xy) och en radiell ritas med första stoppet (loggat en gång);
  `instances(prototyp, instanser, stil)` bygger cellerna EN gång i fabriken
  (`detail::InstanceSet`, src/canvas/instances.h) — en icke-mesh som
  prototyp ger en tom form + logg, och prototypens skärmstorlek ärvs när
  stilen saknar en; `points3`/`pointCloud`. Sidantalen klampas till 3–256.
  Läsvägen `mesh()`/`placement()`/`meshStyle()`/`instances()`/
  `pointCloud()`/`pointStyle()`/`instanceSet()` — plockningen (etapp 7)
  läser meshen GENOM placeringen. **HELSKÄRMSPASSET (CANVAS_3D etapp 8)**,
  `Kind::Pass`: `screenPass(programnamn, PassStyle)` — en form UTAN
  geometri som ritytans EGNA program (`Canvas::addProgram`) ritar över hela
  ytan FÖRST i 3D-målet (steg 1, utan djup — himlen, en bakgrund); den tar
  ritytan till 3D-målet men gör inte lagret till World (`is3D()` är false),
  har ingen låda och träffas aldrig. Läsvägen `passProgram()`/`passStyle()`.
- **`image_source.h`** (CANVAS_3D etapp 4, utbruten ur shape.h) —
  **`ImageSource`**: en bild ur `AssetHandle`, `Bitmap` eller `RenderTexture`
  (implicita konstruktorer; `sameImage` jämför BILDEN, så kopior av en Bitmap
  är samma bild). En egen header för att både shape.h (`MarkerStyle::img`)
  och style3d.h (`MeshStyle::texture`) bär den, och shape.h inkluderar
  style3d.h. Implementationen står kvar i src/canvas/shape.cpp.
  **Sedan TRAD_LISTOR_BILDER RAMVERKETS bildvärde** — varje widget som visar
  en bild tar det (Picture/Button/ToolButton, trädets noder, tabellraderna,
  `MenuItem::icon`, `messageBox`, dragbilden, AppIcon, TrayIcon), och
  `widget.h` inkluderar den här headern. `operator==` är VÄRDELIKHET: samma
  källa OCH samma `version_` — en snapshot av bildens version, tagen i
  Bitmap-/RenderTexture-konstruktorn — så `setImg(sammaBild)` efter
  `setPixels` är en ändring (en widgets likhetsvakt invaliderar, TrayIcon
  lägger om ikonen); två ogiltiga är lika. `sameImage` frågar bara om källan.
  `explicit operator bool` = `valid()`, och `sizeClass()` är assetens
  storleksklass och 0 för pixlar (widgetarna faller då till `iconFallback`).
- **`style3d.h`** (CANVAS_3D etapp 3) — HUR ett lager ritas i 3D-målet:
  **`RenderState`** (`depth`, `depthWrite`, `depthBias`, `slopeBias`,
  `blend`, `cull` — fasadens typer, men APPENS förval är 3D: Less med
  skrivning; fältordningen är kontrakt för designerade initierare) och
  **`LayerPlacement {Auto, Ground, World, Terrain}`** (Auto = World av en
  form i rymden, annars Ground; **Terrain** (etapp 9) — lagrets meshar och
  ytor i rymden ÄR marken: ritade med lagrets läge i 3D-målets steg 2, och
  Ground-lagrens bilder, ytor, linjer och liggande text DRAPERAS på dem — då
  finns Ground-lagrens geometri BARA genom draperingen, ett hål visar det
  bakom, markdjupet uteblir och de stående etiketterna står vid markhöjd;
  px och mm i draperingen mäts i dess mål, skriven begränsning, och
  `Canvas::heightAt` svarar med terrängens höjd). Etiketter i ett World-lager djuptestas med lagrets läge
  men SKRIVER ALDRIG DJUP — gloria och fyllning ligger på samma djup.
  `Canvas::setLayerPlacement`/`setLayerRenderState` tar dem. **Etapp 4–6:**
  **`ColorRamp`** (2D-gradientens stopp + `min`/`max` — skalären genom
  rampen, multiplicerar färgen), **`Lighting`** (ett riktat ljus fast i
  världen — `direction` är FÄRDRIKTNINGEN —, ambient, diffuse, exponent,
  rimvektorn och rimmet, dimfärgen med alfan som styrka, `fogStart`/`fogEnd`/
  `fogPower` i världsenheter och `twoSided`; förvalen är facits ljus i stort;
  `Canvas::setLighting`), **`Placement`** och **`Instance`** (`at` + yaw/
  pitch/roll i grader + skala: världen = origo + at + R · (skala · läget),
  R = Rz(−yaw) · Rx(pitch) · Ry(roll) — yaw medurs ovanifrån som kompassen;
  instansens färg MULTIPLICERAR, alfa 0 = ingen), **`MeshStyle`** (färg,
  opacitet, `lit`, `fog`, mättnad, `texture`, `ramp`, `range` och `size` —
  > 0 = konstant skärmstorlek för meshens LOKALA enhet) och **`PointStyle`**
  (diametern som `Length`, formen, färgen, rampen, `fog`). De egna
  programmens fält (`program`, `uniforms`, `textures`, etapp 8) läggs SIST i
  `MeshStyle`/`PointStyle` — ordningen är kontrakt — och efter dem i
  `MeshStyle` färgjusteringens `contrast` och `brightness` (REQ8 3; med
  `saturation` samma regel som `ImageStyle`, och mättnaden får nu gå över 1).
  **EGNA PROGRAM (etapp
  8):** **`Program`** — `name` (ritytans nyckel) och källorna i HLSL,
  GLSL (vertex + fragment) och MSL, ingångarna (`vsMain`/`fsMain`),
  `textures` och `uniformFloats` (GlazeUser, float4-packat); förspelet med
  GlazeFrame och platskonventionen är fasadens (gfx/program.h). Ett program
  kompileras PER FÖNSTER OCH VERSION (`setProgram` med samma namn är en ny
  version), och felet med appens radnummer står i `Canvas::programError`.
  Ett program som saknas, inte kompilerar, läser en ström formen saknar
  eller samplar fler texturer än stilen ger ger RESERVEN: det inbyggda
  programmet för en mesh/ytan/punkterna och passets `fallback`-färg —
  loggat en gång per rityta och namn. Egna program ritar inte instanser
  (det inbyggda tar dem, loggat), och en meshs placering är invikt i
  GlazeFrame. **`PassStyle`** — `uniforms`, `textures` och `fallback` (alfa
  0 = ingenting, förvalet). **`View`**
  (etapp 10) — en DEL-VY: `anchor` (textens nio lägen — samma punkt på
  ritytan och på vyn), `offset` och `width`/`height` i ritytans LOKALA
  LOGISKA px (0 = ritytans hela mått), en egen `Camera3D`, `followRotation`
  (huvudvyns rotation — en 3D-kameras azimut, elevation, roll och
  upp-axel, kartkamerans bäring och tilt — medan mål, avstånd, projektion och
  synfält är vyns) och `background` (alfa 0 = huvudvyn syns igenom). Fältet
  `anchor` är `gfx::TextAnchor` och inte `glaze::TextAnchor`-aliaset: shape.h
  inkluderar style3d.h, så style3d.h kan inte luta sig mot shape.h. Vyerna
  tas emot av `Canvas::addView`/`setView`/`setViews`/`removeView` (vy 1, 2 …;
  0 är huvudvyn) och lagren läggs i dem med `setLayerViews`.
- **`path.h`** (etapp 7) — `Path` i TVÅ likvärdiga former (regel 8): kedjan
  `moveTo`/`lineTo`/`quadTo`/`cubicTo`/`close` för koden och listan
  `Path{{PathVerb::Move, {0, 0}}, …}` (`PathSegment`, punkterna i SVG:s
  ordning) för `.glazeui` — samma segmentlista. `flatten()` plattar kurvorna
  med en tolerans RELATIV kurvans storlek (zoomen är okänd när formen
  byggs). Som yta är första delvägen ytterring och resten hål.
- **`camera.h`** (etapp 7, T8) — `Camera {center, zoom, bearing, pitch}` i
  GLAZES mått: zoom i logiska px per världsenhet, grader. Widgeten räknar om
  till fasadens `gfx::MapView` (fysiska px). **3D (CANVAS_3D etapp 2):**
  `Projection {Perspective, Orthographic}`, `UpAxis {Z, Y}` och
  **`Camera3D {target, distance, azimuth, elevation, roll, projection, fov,
  orthoHeight, nearPlane, farPlane, up}`** — ORBITPARAMETRISERAD, grader
  och världsenheter. Azimuten är KOMPASSEN kameran tittar mot (0 = norr,
  medurs, som bäringen — ögat står SÖDER om målet vid 0), elevationen hur
  högt ögat står (90 = rakt uppifrån; upp följer elevationens TANGENT, så
  polen passeras utan gimbal lock), rollen vrider kameran medurs och bilden
  moturs, `orthoHeight = 0` är MATCHAD mot perspektivet vid målet, klipplan
  0 räknas ur scenens låda, och `UpAxis::Y` ger golvet XZ med norr −z — en
  rotation, ingen spegling. FÖRSTAPERSONEN är `lookFrom(öga, yaw, pitch)`
  (pitch UPPÅT; målet en enhet fram) och `eye()` ger
  ögat. Fältordningen är kontrakt för designerade initierare.
  `Canvas::setCamera(Camera3D)` (etapp 3 — en överlagring, så en literal
  bär sin typ) ritar med den i 3D-målet; räkningen bor i
  `src/canvas/camera_math.*`. Ritytans ORBIT (etapp 11) rör bara
  `azimuth`/`elevation` (inom `setElevationRange`, förval ±85°, eller
  förbi polen med `setOrbitTumble`), `target` (panoreringen i målplanet)
  och `distance` (dollyn inom `setDistanceRange`; en satt `orthoHeight`
  följer med i samma skala) — `roll`, `projection`, `fov` och klipplanen
  är appens. Förstapersonen har ingen inbyggd gest: den är appens
  `onTick` med `isKeyHeld`, och `lookFrom` i varje tick.
- **`painter.h`** (etapp 7, T7) — `Painter` och `PaintArgs`. Det OMEDELBARA
  läget i `Canvas::onPaint`/`onPaintOverlay`: samma byggare som lagren in i
  renderarens TRANSIENTA ring, satsvis (programbyte tömmer). `draw(Shape)` +
  bekvämligheterna (sedan CANVAS_3D etapp 3 också `drawLine3`/
  `drawPolyline3`/`drawText3`/`drawMarker3`), `save()` (RAII `Saved`), `translate`/`rotate`/`scale`
  (på punkterna, inte måtten), `screenSpace()`, `measureText()` (logiska px,
  UTAN uppladdning sedan REQ2_RITYTAN — `Canvas::measureText` är samma
  mätning utanför en ritning, i valfri enhet) och `size()`. Sedan CANVAS_TEXT_OCH_BILD tar `drawText`/`drawText3`/
  `measureText` också en `std::vector<TextSpan>` och `drawImage` fyra hörn —
  samma vertex som formerna i ett lager (painter_gate TD); sedan REQ8 3 tar
  `drawImage` en `ImageStyle` i stället för opaciteten. **`setRenderState`** (etapp 10, style3d.h `RenderState`):
  målaren BÖRJAR UTAN DJUP (`{.depth = Off, .depthWrite = false}` — ovanpå
  lagren, som före renderläget) och läget hör till det `save()` sparar; det
  verkar bara i 3D-målet och i världen — i 2D-vägen, i `screenSpace()` och i
  `onPaintOverlay` finns inget djup att testa mot. Text och markörer skriver
  aldrig djup. **Föremålen** (etapp 10): `draw` tar också rymdens meshar,
  primitiver, ytor, instanser och punkter, och `drawTriangles3`/`drawPoints3`
  ligger ovanpå — ur formens CPU-kopia genom den transienta ringen VARJE
  ritning, med lagrens program, stil och ljus (det som ligger still hör
  hemma i ett lager); en mesh större än ett segment (64 MiB) ritas inte här
  (loggas). `onPaint` börjar i världen, `onPaintOverlay` i de lokala px
  — och ritas sedan etapp 8 i fönstrets OVERLAY-LAGER (vid varje present,
  klippt till ritytan), inte i ritytans egen ritning. **`onPaintLayer`**
  (REQ8 2) börjar i världen som `onPaint` men ritas i varje lager med
  `Canvas::setLayerPainter`, efter lagrets former, och **`PaintArgs::layer`**
  bär lagrets plats (−1 i de två andra); i ett World- eller Terrain-lager i
  3D-målet börjar målaren med LAGRETS renderläge i stället för utan djup.
  **`save(Rect)`** (REQ8 1) — KLIPPET: samma sparade läge, och det som ritas
  till lägets slut klipps till rektangeln i den transform och rymd som gäller
  vid anropet (vriden en fyrhörning, under tilt dess bild), snittad med
  föräldern; därefter en yta på skärmen som ingen transform flyttar. Linjer,
  ytor, text, markörer och bilder klipps i varje pixel (kantplanen i
  `gfx::SceneUniforms::clip`, utanför kastat — inget djup), föremålen till
  den omskrivna rektangeln (scissorn). Högst 8 kanter — fler loggas en gång
  och de minsta hörnen skärs av; en rektangel utan yta eller med NaN klipper
  bort allt.
  Kontraktet: DETERMINISTISK ritning, inga trädändringar.
- **`bitmap.h`** — `glaze::Bitmap` (etapp 3): appens egna RGBA8-pixlar (rak
  alfa, rad 0 överst, täta rader). Ett HANDTAG — kopior DELAR pixlarna
  (`shared_ptr` till `detail::BitmapData`), så en platta i två lager är en
  bild och en textur per fönster. `Bitmap(w, h)` (genomskinlig),
  `Bitmap(w, h, rgba, stride)`, **`decode(data, size)`** (genom
  `gfx::decodeImage` — TRÅDSÄKER; konstruktorerna likaså, id:t är
  atomiskt), `setPixels(x, y, w, h, rgba, stride)` (logg + no-op utanför),
  `fill(färg)`, `setFilter` (`gfx::ImageFilter`, Linear som förval — E3).
  **ÄNDRINGSLOGGEN** är det som gör flera fönster rätt: varje `setPixels`
  antecknar sin rektangel med en version, och varje fönsters textur laddar
  upp de rektanglar som tillkommit sedan DESS version — eller hela bilden
  om loggen inte räcker dit (`kMaxBitmapChanges` = 32; en ändring av hela
  bilden tömmer loggen). Bilden är DATA, inte en widget: en app som ändrar
  pixlar i en synlig bild invaliderar det som ritar den — eller, i en
  widget, sätter bilden igen (versionen gör det till en ändring). Ingen
  `.glazeui`-form — pixlar räknas fram i drift (regel 8 gäller formerna,
  och en asset-bild heter `glaze::img`). `ImageSource` är `friend` (samma
  bild-jämförelsen). Visas sedan TRAD_LISTOR_BILDER också i WIDGETARNA
  (`ImageSource`, ovan): i en widget är bildens logiska storlek pixlarna /
  skalan, och en OS-ikon ur den räknas om på CPU:n ur de levande pixlarna
  (src/window_icon.cpp). **`setKeepPixels(false)`** (REQ3_RITYTAN 9) släpper
  pixlarna i slutet av det pumpvarv där bilden laddats upp hel och aktuell i
  ett fönster; kontraktet efter släppet (B7) står i headern — `pixels()`
  nullptr, `setPixels`/`fill` loggar, fönstren med texturen ritar vidare och
  LRU:n vräker den aldrig, ett fönster UTAN textur ritar ingenting, ingen
  OS-ikon. Namnet är inte Meshens `keepCpuCopy`, som INTE släpper.
- **`render_texture.h`** — `glaze::RenderTexture` (etapp 6): en yta på
  FYSISKA px att rita i och sedan rita som bild. Handtag som Bitmap.
  **`paint(fn, clear)` ritar INTE** — den lagrar funktionen och stegar
  versionen; varje fönster målar i NÄSTA frame som ritar texturen
  (`ImageCache::get(const RenderTexture&, const Ctx&)`), och igen först
  efter `paint`/`repaint` eller när budgeten vräkt texturen. `fn` får ett
  Ctx vars `box` är texturens yta från (0, 0) — `Draw::*` i logiska px med
  fönstrets tema och fonter — och måste därför vara DETERMINISTISK.
  Innehållet är PREMULTIPLICERAT (gfx/image.h). En textur som ritar sig
  själv i sin målning får nullptr där (loggas, rekursionen bryts).
- **`external_pass.h`** — `glaze::detail::ExternalCanvasPass`: ritytans scen i
  ett EXTERNT mål (`gfx::Renderer::importRenderTarget`) med en uttrycklig
  `gfx::View3D` per mål, för ett sidopaket som visar en Canvas utanför
  fönstret. INTERN: undantaget från "allt här kommer in med `glaze.h`" — den
  står inte i paraplyet, har ingen `.glazeui`-form, och `Canvas` ger den
  tillträde med en `friend`. `create(canvas, error)` kräver ett ÖPPET
  fönster; `render(views, error)` ritar HUVUDVYNS lager (rutnätet, terrängen,
  djupet, världen, etiketterna, `onPaint`/`onPaintLayer`) i alla mål i ETT
  offscreen-block utan att röra skrivbordets kamera, mål eller cache, och
  utan återläsning; `castRay(ray, options, view)` är `Canvas::castRay` med
  en uttrycklig vy för skärmstorlekar, LOD och toleranser; `renderer()` är
  fönstrets renderare, som skapar målen. UI-trådens, och släpps före ritytan.
  Del-vyerna, kanten och fokusringen ingår inte. Implementationen:
  [../../../src/canvas/CLAUDE.md](../../../src/canvas/CLAUDE.md).
- **`mesh.h`** (CANVAS_3D etapp 4, 6 och 7) — 3D-INNEHÅLL UR DATA, koddata
  som `Bitmap`: ingen `.glazeui`-form utom literalerna `Mesh::triangles`/
  `Mesh::indexed` (regel 8, §2.4). **`Mesh`** — ett HANDTAG (kopior delar
  datan), id som aldrig återanvänds. **`MeshDesc`**: lägen som `Float3`
  RELATIVT `origin` (double — RTC), normaler, färger (rak alfa), uv,
  skalärer och user0–3, varje ström TOM eller en per läge; uint32-`indices`
  (tomt = icke-indexerat, varje tre lägen en triangel), nivåerna och
  `keepCpuCopy` (CPU-kopian är PLOCKNINGENS — av ⇒ ingen plockning, loggat en
  gång). Ogiltig data ⇒ ingen mesh + logg (fel längd, index utanför, inte
  hela trianglar, ett läge eller origo som inte är ändligt, nivåer i fel
  ordning); tomt ⇒ ingen mesh utan logg. `fromWorld` lägger origot i lådans
  mitt; `bounds()` är lådan i världen före formens placering.
  **`IndexRange`/`Lod`** räknas i ELEMENT (index, eller lägen utan index) och
  hela trianglar, och `count` KLAMPAS till meshens slut som `substr`:
  förvalet **`kRangeToEnd`** är resten och 0 är tomt. Nivåerna står i växande
  `maxDistance`, 0 = obegränsat och bara sist, och bortom den sista ritas
  ingenting (en mesh utan obegränsad nivå försvinner på avstånd).
  `setPositions`/`setColors` antecknar i ÄNDRINGSLOGGEN (`kMaxMeshChanges` =
  32; en ändring av HELA strömmen gör äldre ändringar av samma ström
  överflödiga), och `setPositions` räknar om lådan och kastar plockningens
  BVH. **`preparePicking()`** bygger BVH:n i förväg — i en arbetartråd för
  en stor mesh (planens osäkerhet nr 3; `pick_gate --stolpe` mäter den).
  **`PointCloud`** — handtag; lägen, valfria färger och skalärer, delat
  RUMSLIGT i chunkar om högst **`kPointChunkSize`** (65 536) med eget origo
  — gallringen fungerar oavsett i vilken ordning punkterna kom, och ritytan
  svarar ändå med punktens plats i INDATAN. TRÅDARNA: konstruktorerna och
  `preparePicking` i valfri tråd, ändringar och plockning i UI-tråden, och
  ingen ändring medan en annan tråd kör `preparePicking`. Inga GPU-objekt
  här: residensen (segmenten om 64 MiB, uppladdningen) är fönstrets
  (src/canvas/residency.h). En app som ändrar en SYNLIG mesh
  (`setPositions`/`setColors`) invaliderar ritytan, som för en Bitmap — bara
  de ändrade lägena laddas upp, allt i en frame. `keepCpuCopy = false`
  släpper INTE CPU-kopian: varje fönster laddar upp ur den (skriven gräns).

## Konventioner

- **Inga backend-typer, inga GPU-objekt i handtagen och formerna.** Pixlarna,
  målningen och formens punkter bor i värdena; texturerna och buffertarna
  bor per FÖNSTER — bildernas i `ImageCache`, lagrens i `CanvasResources`
  (fontens livstidsregel: rivs i `App::executeClose` före fonten).
- **Minnesbudgeten är PER RITYTA, uppladdningen per fönster** (REQ3_RITYTAN
  10). Minnet (`Canvas::setMemoryBudget`, förval `App::setCanvasMemoryBudget`
  256 MB) räknar bildernas, render-texturernas och de omräknade bildernas
  texturer på den rityta som ritade dem SENAST — KONTON i fönstrets
  `ImageCache`, det utanför ritytorna på fönstret — och ritytans
  draperingsmål mot samma summa (src/canvas/drape.h); uppladdningen per frame
  (`App::setCanvasUploadBudget`, 16 MiB) delas av bilderna och föremålens
  residens (meshar, instanser, punktmoln). Något SLÄPPS bara mellan frames:
  `ImageCache::trim()` och `CanvasResources::trim()` överst i
  `AppWindow::renderContent`.
- **Regel 8:** formerna är värden med platta fabriker och aggregatliteraler
  — den form grammatiken kan bära, på EN rad per sats. Previewn utför dem
  sedan etapp 10 (`apply_props`s ritytsgren: formerna, stilarna som
  designerade initierare i DEKLARATIONSORDNING, kameran och lagren — atomiskt,
  en form utanför delmängden är en opak rad). Ett NYTT fält i en stil eller
  formens fabriker kräver alltså en rad i parsern och i
  `tests/fixtures/calls.*` (som kompilerar varje form). Ordningen i
  structarna är därmed kontrakt: en designerad initierare i fel ordning
  kompilerar inte, och parsern avvisar den likadant. En ytas valfria kontur
  är `glaze::Stroke{…}` eller `std::nullopt` — en bar klammerlista
  kompilerar inte till en `std::optional`. **RYMDEN HAR SAMMA FORM sedan
  CANVAS_3D etapp 12** (planens §2.4) — previewn UTFÖR: alla fabriker i
  rymden (`instances` med en form som prototyp, `mesh` med
  `Mesh::triangles`/`indexed`, `screenPass` med programmets NAMN), varje
  stil i style3d.h som literal (`MeshStyle`, `PointStyle`, `PassStyle`,
  `ColorRamp`, `Placement`, `Instance` — `IndexRange` med
  `glaze::kRangeToEnd`), `setCamera(glaze::Camera3D{…})` och
  `Camera3D::lookFrom(…)`, `setLighting`, `setLayerPlacement`,
  `setLayerRenderState`, `setLayerViews`, vyernas fyra, orbitens spann,
  `setTicking`, `zoomToBox` och setter-fälten `multisample`/`depth`/
  `orbitTumble`; `onTick`/`onGesture` är kopplingar. **`setCamera` är
  ÖVERLAGRAD**: en bar POSITIONELL klammerlista och `{}` är tvetydiga och
  kompilerar inte (parsern avvisar dem), medan en bar DESIGNERAD lista väljer
  den typ som HAR namnen — `Camera` och `Camera3D` har disjunkta fältnamn, så
  `setCamera({.distance = 80})` är 3D-kameran precis som i C++. **Ett NYTT
  fält i camera.h eller style3d.h läggs SIST** och kräver en rad i parserns
  fältlista (`apply_props.cpp`, rymdens literalavsnitt) — annars avvisas den
  designerade raden som okänd. **KODDATA, ingen form:** `Mesh` och
  `PointCloud` ur arrayer (`MeshDesc`, `fromWorld`, `pointCloud` — `points3`
  är literalformen), `addProgram(Program{…})` och programmens källor (previewn
  kör aldrig användarkod — ett namngivet program ritas där med reserven: det
  inbyggda programmet respektive passets `fallback`), bilder ur
  `Bitmap`/`RenderTexture`. **HANDLINGAR, inget tillstånd:** `orbitBy`/
  `panBy`/`dollyBy`, `easeTo` och `renderToBitmap` bevaras opakt, som
  `easeTo` i 2D.
- Grindar: `tests/scene_math_gate.cpp` del 3D (`Camera3D` headless mot en
  egen hålkamera — konventionen, matchad orto, tumlingen, UpAxis::Y och
  kamerans verktyg), `tests/painter_gate.cpp` (lagren och målaren headless, Y:
  etiketternas bas och placeringens cache, LO: `moveLayer` med samma
  buffertar och 0 byte), `tests/geom_gate.cpp` S6 och `tests/canvas_gate.cpp`
  M (textens mått utan uppladdning — M2 mot rastret i den riktiga backenden
  — och LO, flytten genom fönstret), `tests/scene_pixel_gate.cpp`
  del B (B5: bildernas budget), E (E7: render-texturerna), F (ritytan i
  pixlar), G (etikettkollisionen i pixlar) och H (konturen: skarvar, ändar,
  streck i px och mm, under tilt — och bildmarkören), `tests/canvas_gate.cpp`
  (gesterna och träffprovet genom fönstrets ingång, också förankrade
  markörer och bildmarkörer, och skalan vid en punkt — `unitsPerPixelAt`
  mot `toWorld`s derivata under tilt), `tests/preview_gate.cpp` `fallCanvas`,
  `fall3D` och `fallCallsRityta` (regel 8 — 2D, rymden och att varje
  karta-sats i `tests/fixtures/calls.*` utförs) med `pixel_gate`s grupp 25
  och 27 (ritytan i 2D och i 3D-målet i alla paletter) och
  `tests/canvas_perf.cpp` (G7, etapp 11 — en miljon vertex och
  10 000 etiketter: kameraändringen laddar upp högst synligheten, bygger
  inget om och håller rit-anropen till det innehållet kräver; tiderna och
  GPU-adaptern skrivs ut). Rymdens former och 3D-målet:
  `tests/scene3d_pixel_gate.cpp` del D (Line3/Glyph3 mot en hålkamera), G
  (ritytans 3D-mål — vägvalet, kompositen, cachen, Ground-etiketterna
  överst), C2 (etapp 8: ett eget mesh-program med user0, GlazeUser och en
  Bitmap-textur, placeringen invikt, reserven och `programError` med raden,
  himmelspasset mot grindens trigonometri och passets `fallback`), H
  (etapp 9: draperingen på en analytisk lutande platta mot en egen
  hålkamera, bilden 1:1 också underifrån, hålet, etiketten vid markhöjd och
  terrängens version, den liggande texten — förkortad, vriden, aldrig
  spegelvänd, kolliderande med sin projicerade ruta — och omritningen bara
  vid ändring) och I (etapp 10: vyns eget djup och klipplan, `followRotation`
  mot grindens egen ortografiska projektion — också med kartkameran —, den
  genomskinliga bakgrunden, `renderToBitmap` = skärmens region ±1 också med
  fönstret dolt, skärmens etikettplacering orörd av bilden, målarens
  renderläge och vyernas kollektionsvokabulär) och `painter_gate` L3 (Line3
  laddar inte upp per kameraändring). Föremålen (etapp 4–6):
  `scene3d_pixel_gate` del E och F (ljuset, baksidan, den platta normalen,
  texturen, rampen, mättnaden, dimman, RTC, instanserna, ändringsloggen,
  punkternas yta i px och mm, rampen, djupet, färgen och skärmstorleken —
  GENOM ritytan) och `tests/canvas_perf3d.cpp` (G10: 0 byte per
  kameraändring, inget ombyggt, strömningen inom budgeten med den gamla
  meshen tills den nya är uppe, detaljnivåerna och gallringen mot grindens
  egen synsektor och pyramid, TERRÄNGSERIEN — 121 rutor med draperade
  vägar: panorering inom klassen ritar inte om, tilt fram och tillbaka håller
  klasserna, zoom 4× ritar om bara de bytta, 0 byte — och anropstaket (D,
  etapp 13: rit-anropen ≤ det innehållet kräver + 8); `--full` med facits
  storlekar).
  `Mesh`/`PointCloud`: `tests/geom_gate.cpp` del 3D
  (segmenten, datan, ändringsloggen, chunkarna) och `tests/pick_gate.cpp`
  (G11 — plockningens kärna mot brute force, originalindex, filtren; och
  sedan etapp 7 RITYTANS del R1–R13 i ett dolt fönster mot grindens egen
  hålkamera: `rayAt`∘`project`, `worldPerPixel` mot en derivata,
  `visibleRegion` mot en trapets, hörn och kant inom N px men inte
  N + 0,3, punkterna med `maxDistance`, etiketten i 3D, `heightAt` på en
  lutande terräng, skymningen längs synlinjen, routningen till del-vyn och
  den ritade detaljnivån — exit 77 utan display). Interaktionen (etapp 11):
  `tests/canvas3d_gate.cpp` (G12 — orbit, panorering, dolly, tumlingen,
  spannen, verktygen, mittenknappen och `onGesture`, `onTick` med
  tangenterna som hålls, och widgetarna på ritytan genom fönstrets ingång;
  `--hand` är testpassets synliga fönster med förstapersonen och grindar
  ingenting) och `tests/trackpad_gate.cpp` del O (styrplattan i orbitläge).
