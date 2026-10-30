# CLAUDE.md — Glaze/gfx/include/gfx

Publika headers för **`common_gfx`** — **render-fasaden**. Egna POD-typer +
abstrakta interface; backends (`backend_d3d11` på Windows, `backend_metal`
på macOS, `backend_linux` på Linux) implementerar dem i
[../../src/](../../src/). Konsumeras av `glaze`-libbet, inte direkt av appar. Del av GlazeUI — se [root /CLAUDE.md](../../../../CLAUDE.md)
för lagerbilden och de hårda reglerna.

## Regeln som är hela poängen

**Inga backend-typer (Win32/D3D/DWrite, ObjC/Metal/AppKit, Wayland/Xlib/
EGL/FreeType) i dessa headers.** Fasaden är
abstraktionsgränsen: `glaze` och apparna ser BARA typerna här. Backend-typer
bor i `src/backend_*/` — läcker en typ hit är abstraktionen bruten.
(Root-`/CLAUDE.md`:s grep-grind ska ge 0 träffar i `Glaze/gfx/include`.)

## Filer

- **`color.h`** — `gfx::Color` (RGBA, 8 bitar/kanal), constexpr-vänlig POD
  med defaultad kanalvis `==`; `glaze::theme::` bygger på den.
- **`geometry.h`** — `Pointf`, `Sizef`, `Rectf` (+ `Rectf::contains(x, y)`). Fysiska px.
- **RITYTANS SCEN (CANVAS_2D etapp 1, 2026-10-09; 3D sedan CANVAS_3D)** —
  sju headers bredvid UI-pipelinen. Innehållet
  laddas upp EN gång; panorering, zoom, vridning och tilt är en ny matris.
  - **`mat4.h`** — `Mat4d` i DOUBLE, `toFloat` först vid uppladdningen.
    KONVENTIONEN är kontraktet mot tre shaderspråk och står i filen:
    kolumnordning (`m[kolumn * 4 + rad]`) och kolumnvektorer — läses rakt av
    som GLSL-`mat4`, HLSL-`float4x4` (column_major, `mul(M, v)`) och
    MSL-`float4x4` (`M * v`); `ortho`/`perspective` är högerhänta med djupet
    i [0, 1] (D3D11/Metal) — GL:s vertexshader räknar om z (2z − w).
    `perspective` med f = ∞ ger det oändliga bortre planet. **`rotationY`**
    och **`lookAt`** (CANVAS_3D etapp 2): ögat i origo och blicken längs −z,
    höger = blicken × upp; NOLLMATRISEN när blick och upp är parallella eller
    ögat står på målet — samma svar som en oanvändbar kameravy.
  - **`camera.h`** — `MapView {cx, cy, pxPerUnit, bearingDeg, pitchDeg, yUp,
    box}` i FYSISKA px (glaze räknar om sina logiska tal). Bäringen vrider
    innehållet MOTURS (MapLibre: 90° med yUp lägger öster uppåt), tilten
    klampas till `kMaxPitchDeg` (70°), synfältet är MapLibres `kMapFovRad`
    (2·atan(1/3) ⇒ kameran 1,5 × ytans höjd bort). **`viewProjection(vy,
    origoX, origoY)`** = chunk-lokalt → PIXELKLIPP: X/W och Y/W är MÅLETS
    fysiska px (samma rymd som `box` och klippstacken), så kameran behöver
    aldrig målets storlek — backenden gör sista steget till NDC i sin
    vertexshader. RTC: subtraktionen origo − mitt görs i double där.
    Pitch 0 ortografisk, > 0 perspektiv med närplanet på höjd/50 och det
    bortre i oändligheten (inget djuptest i 2D). **`project`** (false bakom kameran),
    **`unproject`** (strålen mot z = 0; false ovanför horisonten — träffen
    måste ligga FRAMFÖR kameran), **`pxPerUnitAt`** (skalan längs bildraden,
    0 ovanför horisonten) och **`anchorWorldAt`** (flyttar mitten så en
    världspunkt hamnar under en pixel — zoom mot pekaren och panorering,
    exakt även under tilt). Grind: `tests/scene_math_gate.cpp`.
    **3D-VYN (CANVAS_3D etapp 2, `src/camera3d.cpp`; MapView orörd):**
    `Vec3d`/`Ray3d`/`Plane3d`, `Projection3D {Perspective, Orthographic}`
    och **`View3D {eye, forward, up, projection, fovY, orthoHalfHeight,
    nearZ, farZ, box}`** — synfältet i RADIANER (som `perspective`),
    riktningarna varken enhetliga eller vinkelräta (upp räknas om, höger =
    blicken × upp), klipplanen ÄNDLIGA (perspektiv 0 < near < far,
    ortografiskt near < far och närplanet FÅR ligga bakom ögat); en
    oanvändbar vy ger nollmatrisen och falska svar. **`viewProjection(vy,
    ox, oy, oz)`** lämnar samma pixelklipp som kartans (RTC: origo − öga i
    double) — backenden gör NDC:n och GL:s 2z − w som för MapView.
    **`project`** (false bakom kameran, ortografiskt aldrig; `depth` = Z/W),
    **`rayThrough`** (enhetlig riktning — perspektiv ur ögat, ortografiskt
    från NÄRPLANET under pixeln, så allt som syns ligger framför),
    **`pxPerUnitAt(vy, punkt)`** (skalan vinkelrätt mot blicken VID punkten:
    brännvidden genom DJUPET, inte avståndet till ögat), **`intersect`**
    (stråle mot plan, t ≥ 0) och **`visiblePlaneRegion(vy, plan,
    maxDistance)`** (snittet med synpyramiden till det bortre planet eller
    djupet `maxDistance` — ett konvext polygon moturs sett från normalen,
    tomt när planet inte syns). **`toView3D(MapView, zMin, zMax)`** —
    KARTKAMERAN SOM 3D-VY ur KARTMATRISENS INVERS, så z = 0 hamnar där
    `project(MapView)` lägger det, per konstruktion (grindat inom 1e-6 px);
    djupet ur z-spannet ∪ 0 (perspektivets bortre plan där översta
    bildradens stråle når spannets bortersta plan × 1,01, MapLibres furthest
    distance; ortografiskt omsluter klipplanen spannet). **`FrameUniforms`** —
    GlazeFrame, std140, 256 byte med offseten `static_assert`ade, KONTRAKT
    mot tre språk (`program.h`); **`frameUniforms(vy, ox, oy, oz)`** fyller
    det vyn vet — matriserna, ögat chunk-lokalt, strålbasen, `rayCenter`
    (strålen = `rayCenter + ndc.x·right + ndc.y·up`; = `forward` i ett
    symmetriskt frustum) och världen per px — medan `target` skrivs av
    BACKENDEN och `scale` av ritytan. Grind: `scene_math_gate` del 3D.
  - **`scene.h`** — VERTEXFORMATEN ÄR KONTRAKT (`static_assert` på storlek
    och offset): **`LineVertex`** (40 byte: punkt, föregående, nästa,
    `dist`, bredd, färg, sida, slag, ändar — punkterna CHUNK-LOKALA, en
    punkt utan granne bär sig själv), **`LineKind`** (`SegmentStart`
    riktning mot nästa, `SegmentEnd` från föregående, `Round` en skiva,
    `Miter`/`Bevel` kilen på skarvens YTTERSIDA räknad i vertexshadern ur de
    PROJICERADE grannarna — `side` är för bitarna hörnet 0–3) och
    **`lineflag`** (segmentets `ends`, samma byte i dess fyra vertex: ändens
    form rak/rund/fyrkantig och om segmentets början/slut är LINJENS ände —
    en rak eller fyrkantig ände förlängs och kantutjämnas längs linjen, en
    skarv inte), `MeshProgram {Line, Fill, Glyph, Image, Line3, Glyph3,
    Custom}` (Line3/Glyph3 är Line och Glyph med GLAZE_POS3 — se "I RYMDEN"
    nedan; `Custom` SIST — se `program.h`),
    `BufferKind {Vertex, Index}` (index ALLTID R32), abstrakt `GpuBuffer`
    (`kind`/`size`/`valid`), `BufferSlice` (+ `slice(buf)`), `WidthSpace`
    (**Screen** = bredden × `pxPerWidthUnit` fysiska px, extruderad i
    vertexshadern EFTER projektionen — konstant under zoom och tilt;
    **World** = världsenheter, extruderad i planet FÖRE), `SceneUniforms`
    (matrisen + breddens enhet, och för Line `miterLimit` — SVG:s, förval 4
    — och strecken **`SceneDash`**: högst `kMaxDashes` = 8 KUMULATIVA slut,
    förskjutningen och `perWorldUnit`, mönsterenheter per världsenhet; läget
    längs linjen är `dist` × den, så px och mm mäts på skärmen med kamerans
    skala i MITTEN och förkortas under tilt — skriven begränsning — medan
    world följer zoomen) och `MeshDraw`. **mm är ingen egen gren**:
    för mm är `pxPerWidthUnit` skärmens EFFEKTIVA täthet
    (`DisplayMetrics::pxPerMm` med kalibreringen, i glaze `Ctx::pxPerMm`),
    för px är det uiScale — enheten ÄR faktorn. G3:s MM-fall injicerar en
    kalibrering och mäter 2 mm = 8 px.
    **YTORNA (etapp 4):** **`FillVertex`** (32 byte: punkt, ringens grannar,
    färg, `FillKind` och sida) — en ENSIDIG KANTREMSA per kant, en quad med
    fyra egna vertex som programmet extruderar 1 fysisk px UTÅT i skärmen
    (kanten är 1 px vid varje zoom; priset ≤ ½ px uppblåsning, T3); ytans
    inre trianglar använder kantquadarnas inre startvertex. Ytan ligger till
    VÄNSTER om varje kant med y uppåt (byggaren vänder ringarna) och
    programmet läser kamerans spegling ur matrisen. En quad PER KANT och
    inte en delad remsa: en punkt bakom närplanet flyttas längs SIN kant.
    **FÄRGEN (etapp 4):** `PaintKind {Solid, Linear, Radial}` +
    **`ScenePaint`** i `SceneUniforms::paint` — CHUNK-LOKAL geometri (från/
    till, eller mitt och r0 → r1); en gradient multiplicerar vertexfärgen
    med RAMPEN i `MeshDraw::image` (256 × 1 RGBA8) i punkten t som räknas
    per fragment ur världspunkten, pad-spridning. Fill OCH Line läser den,
    och båda samplar rampen LINJÄRT oavsett bildens filter; en gradient utan
    ramp avvisas av `meshDrawOk`.
    **TEXTEN (etapp 5):** **`GlyphVertex`** (52 byte: ankare, hörnets
    förskjutning i EM, UV, färg, gloria, storlek, vinkel, `haloWidth`,
    flaggor och `label` för kollisionen) — BILLBOARDS: förskjutningen läggs i
    skärmen, i storlekens enhet (`pxPerWidthUnit` — px eller mm som för
    linjebredden), vriden efter skärmen eller (`glyphflag::kMapAligned`)
    efter kartans projicerade riktning vid ankaret. `GlyphShape {Glyph,
    Plate, Circle, Square}` — formerna är avståndsfält i shadern;
    `glyphflag::kSyntheticBold` (tröskeln flyttas) och `kHaloPass`
    (glorians pass, före etikettens fyllningar). Glyph samplar atlassidan i
    `MeshDraw::image` LINJÄRT; ingen sida = bara former. **`kImage`** är
    BILDMARKÖREN: quaden samplar rit-anropets bild (RGBA, med dess
    premultiplikation, spegling och format) i u, v = 0–1, och programmet ger
    den BILDENS proportioner kring ankaret. **GOLVET OCH TAKET** (REQ3):
    `SceneUniforms::glyphMinPx`/`glyphMaxPx` — en em på skärmen (`size` ×
    `pxPerWidthUnit`) klampas i Glyph och Glyph3 till [min, max] FYSISKA px innan
    något läser den (golvet först, så taket vinner; 0 = ingen gräns).
    **ETIKETTERNAS SYNLIGHET (etapp 9):** `MeshDraw::labels` (en R8-bild med
    EN TEXEL PER ETIKETT, rad för rad) och `MeshDraw::labelBase` — Glyph-
    programmets VERTEXSHADER läser texeln `labelBase + GlyphVertex::label`
    (D3D11 vertexstegets t1 med Load, GL enhet 1 med texelFetch, Metal
    vertextextur 0 med read): 0 gallrar quaden, 255 är synlig, däremellan
    multipliceras alfan (toningen). nullptr = alla synliga — basen går då som
    −1 i uniformen. Basen är ett flyttal i shadern, exakt under 2²⁴, och
    `meshDrawOk` avvisar en bild som inte är R8 eller en större bas.
    **BILDERNA (etapp 3):** **`ImageVertex`** (20 byte: chunk-lokal punkt,
    u, v i bildens egen form, rad 0 överst, och den PROJEKTIVA VIKTEN `q`,
    förval 1 — CANVAS_TEXT_OCH_BILD) för `MeshProgram::Image` — världsquads
    med PERSPEKTIVKORREKT sampling av (u·q, v·q, q), delat per fragment och
    v speglad FÖRE multiplikationen: med vikterna ur `quadWeights` blir en
    bild på fyra fria hörn exakt projektiv, och en rektangel (q = 1) samplas
    som förut; `MeshDraw::image` (bilden programmet samplar, med sitt
    filter, format, sin premultiplikation och v-spegling; ur samma
    renderer), `SceneUniforms::opacity` (anropets opacitet 0–1) och (REQ8 3)
    FÄRGJUSTERINGEN `saturation`/`contrast`/`brightness` (`adjustFactor`).
    **KLIPPET (REQ8 1):** `SceneUniforms::clip` (sist) — `SceneClip`, högst
    8 normerade kantplan i målets px; de inbyggda programmen kantutjämnar mot
    närmaste plan och KASTAR fragmentet utanför (inget djup), de egna läser
    det inte. `meshDrawOk` avvisar fler plan och NaN.
    **RENDERLÄGET (CANVAS_3D etapp 0):** **`RenderState`** i `MeshDraw::state`
    — `DepthTest {Off, Less, LessEqual, Greater, GreaterEqual, Equal, Always}`
    med `depthWrite`, `depthBias`/`slopeBias`, `Blend {Alpha, Premultiplied,
    Additive, Opaque, DepthOnly}` och `Cull {None, Back, Front}`. FÖRVALET ÄR
    DAGENS BETEENDE (Off, rak alfa, ingen gallring), så inget 2D-anrop
    ändras; glaze-lagrets eget läge får 3D-förvalet. Tre regler är kontrakt
    och står vid typerna: `Off` är INGET djup (varken test eller skrivning —
    skriv utan att jämföra med `Always`), djupet verkar inte i ett mål utan
    djup (loggat en gång, ritat som Off), och FRAMSIDAN är moturs SETT FRÅN
    KAMERAN — som triangeln syns i målet — i alla tre backends, så en speglad
    matris vänder den. Biasen är i djupformatets minsta steg (D32F: 2^(e −
    23); D3D11 avrundar till heltal), negativt är närmare. Läget gäller BARA
    anropet: UI:t ritar alltid med sitt eget efteråt (grind: G9 A8).
    **DE SKÄRMVÄNDA HÖRNENS DJUP (spår G):** Line i skärmläget, Line3 och
    Glyph (utom liggande) ger ett hörn djupet av planet genom punkten
    parallellt med xy-planet där det är närmare än punktens — det som står
    på ritytans markdjup skärs aldrig av det; draget högst ¼ av djupet, inget
    nära ögat (REQ9 7 — förspelets `glaze_standingZ`; grind: G9 del J, J5).
    **I RYMDEN (CANVAS_3D etapp 3):** `Line3` och `Glyph3` är Line- och
    Glyph-programmen kompilerade med **GLAZE_POS3** — SAMMA källa, en input
    layout per format: **`LineVertex3`** (52 byte: LineVertex med punkterna
    i xyz, chunk-lokala mot chunkens origo i xyz; `dist` är 3D-längden) och
    **`GlyphVertex3`** (56 byte: ankaret i xyz). POS3-linjen extruderas
    ALLTID i skärmen — en linje i rymden ligger inte i något plan: en bredd
    i världen (`WidthSpace::World`) räknas om till px PER VERTEX med den
    lokala skalan (`pxPerUnitAt(View3D)`, ur matrisens w-rad), halva bredden
    interpoleras linjärt i skärmen mellan segmentets ändar, och skarvarna,
    ändarna och strecken är skärmlägets. Två avvikelser från 2D-grenen:
    ändarnas förlängning är EXAKT i skärmen (perspektivdivisionen löst för
    världssträckan, t = e·w²/(lin − e·w·q); den linjära med den lokala
    skalan kom till korta i den bortre änden), och ett fragment utanför
    bandet KASTAS, så kantutjämningens marginal inte skriver djup. **BÅDA
    linjeprogrammen** lämnar dessutom aldrig över en vertex som GPU:n måste
    klippa: segmentets ändar trimmas mot närplanet, det bortre planet och en
    skyddsram en målstorlek utanför målet, och ändarnas förlängning stannar
    innanför klippdjupet (G9 D2, G1); kartkamerans bortre plan nås aldrig. Ett
    segment HELT utanför ett plan (båda ändarna, t.ex. bakom ögat) kastas
    av programmet självt (G9 L1). Glyph3:s quad står upprätt i skärmen vid ankarets
    projektion, på ankarets djup, och gallras bakom närplanet.
    **`glyphflag::kGroundAligned`** (etapp 9, `TextRotation::Ground`) LÄGGER
    quaden i världsplanet vid ankaret — i Glyph OCH Glyph3, i alla tre
    backends: baslinjen är världsriktningen (cos, sin, 0), "nedåt" dess
    vinkelräta i planet, VÄND så att baslinjen och nedåt bildar skärmens ram
    (aldrig spegelvänd — en karta vriden 180° visar texten upp och ner), och
    en em är `size` × `pxPerWidthUnit` px vid ankaret mätt vinkelrätt mot
    blicken (skalan ur matrisen där: blicken är w-raden, ortografiskt
    djupraden), så quaden förkortas längs blicken; kantutjämningen med
    axlarnas geometriska medel. `kMapAligned` läses inte för en liggande
    quad. `meshDrawOk` prövar Line3 som Line (rampen, strecken). Grind: G9
    del D (D3D11 och GL) och del H (den liggande texten).
  - **`program.h`** (CANVAS_3D etapp 1) — EGNA PROGRAM: källa per backend
    (HLSL, GLSL 330, MSL — inget eget språk, ingen transpilering),
    kompilerad i körtid av `Renderer::createProgram` och ritad med
    `MeshProgram::Custom` (SIST i uppräkningen). **`ProgramDesc`** (namnet,
    `hlsl`, `glslVertex`/`glslFragment`, `msl`, `vertexEntry`/
    `fragmentEntry` = vsMain/fsMain — GLSL har main —, `defines` (en
    identifierare och ett värde på en rad), `userBytes` (GlazeUser: en
    multipel av 16, högst `kMaxProgramUserBytes` = 4096 — Metals
    setVertexBytes) och `textures` (≤ `kMaxProgramTextures` = 4)) och
    **`ShaderProgram`** (`valid`/`error` — ALLTID ett objekt; en saknad källa
    för backenden är ett läsbart fel, "programmet 'x' saknar MSL-källa
    (macOS)", loggat EN gång i createProgram — och det programmet
    FÖRKLARADE: `name`, `userBytes`, `textures`, plus REFLEKTIONEN `inputs`/
    `integerInputs` — bit N = platsen N som vertexsteget läser, och läser som
    heltal — som backenden lägger in med den skyddade `setInputs`).
    **`VertexLayout`** följer RIT-ANROPET och inte programmet (formbeslut 3):
    `VertexAttrib {location, format, offset, stream}` (ström 0 vertex, 1
    instans), `stride[2]` (≤ `kMaxVertexStride` = 2048, D3D11:s tak) och
    `key()` — FNV-1a över formen i ordning, backendarnas cachenyckel.
    **`AttribFormat`** {Float…Float4, UNorm8x4, SNorm8x4, UInt8x4, UInt} med
    `attribBytes` och `attribInteger` (ett heltal läses som heltal — GL:s
    glVertexAttribIPointer). PLATSKONVENTIONEN `attrib::` — kPosition 0,
    kNormal 1, kColor 2, kUv 3, kScalar 4, kUser0 5–8, kInstance0 9–14 och
    kCount 16 (GL 3.3:s tak). TRE KONTRAKT står i filhuvudet: FÖRSPELET
    (GlazeFrame = `FrameUniforms` som `glaze` — medlemmarna heter som fälten
    —, `glaze_toClip(c)` till backendens klipprymd, `glaze_colorAdjust` REQ8 3, `glaze_standingZ` REQ9 7,
    i MSL med blocket som andra argument, och sist `#line 1` — i HLSL och MSL
    `#line 1 "<namn>"` — så felraderna avser appens egen källa; i GLSL står
    `#version 330 core` och definitionerna först), PLATSERNA (HLSL ATTR<N>,
    GLSL layout(location = N), MSL [[attribute(N)]]) och BLOCKEN (GlazeFrame
    b3 / bindningspunkt 3 / buffer(2), GlazeUser b4 / 4 / buffer(3) i appens
    egen std140-form, texturerna t0–t3 / `GlazeTexture0`–`3` / texture(0–3),
    strömmarna slot 0–1 / buffer(0)–(1); båda stegen ser allt). Texturerna
    samplas med bildens filter och SOM DE ÄR — ett måls bild är
    premultiplicerad och på GL speglad. I GL tas fragmentets läge ur en
    varying, aldrig ur gl_FragCoord.y (målen är y-vända). FRAMSIDAN som
    fragmentsteget läser (SV_IsFrontFace/gl_FrontFacing/[[front_facing]]) är
    gallringens — moturs sett från kameran — i varje renderläge, också med
    Cull::None utan djup (D3D11:s 2D-rasteriserare fick `FrontCounterClockwise`
    för det, CANVAS_3D etapp 4); en kryssprodukt av ddx/ddy har däremot
    skärmens y-riktning i tecknet (D3D11/Metal nedåt, GL uppåt).
    **`MeshDraw`:s Custom-fält** (scene.h): `vertexCount` (icke-indexerat när
    indexCount är 0 — en TOM layout är helskärmspasset ur vertexindexet),
    `instances` + `instanceCount` (instansdelen binds med OFFSET, ingen
    baseInstance — GL 3.3 saknar den —, så delens första element är instans
    0), `shader` (heter INTE `program`: det namnet bär uppräkningen), `layout`,
    `frame` (GlazeFrame — `target` skriver backenden med det bundna målets
    storlek), `user`/`userBytes` (exakt programmets) och `textures` (platserna
    0 … textures − 1, inga fler). Reglerna är DELADE (renderer.cpp
    `customDrawOk`): ett giltigt program, en giltig layout som PASSAR
    programmet (varje plats vertexsteget läser, av samma slag), strömmar som
    rymmer hela vertex och hela instanser, och programmets GlazeUser och
    texturer. När programmet SKAPAS prövar D3D11:s och GL:s reflektion
    dessutom att källans GlazeUser är exakt `userBytes`, att texturerna
    ligger under `textures` och att inga lösa globaler/uniformer eller andra
    block finns (HLSL:s `$Globals` hade hamnat på UI:ts b0) — Metal
    reflekterar argumenten först i en PSO och kan inte. Grind: G9 del C
    (`tests/scene3d_pixel_gate.cpp`).
  - **`geom2d.h`** — `Point2d` (double), `LineChunk` (origo i double +
    vertex + index), `kMaxChunkVertices` (65 536), `LineCap {Butt, Round,
    Square}`, `LineJoin {Round, Miter, Bevel}`, `PolylineStyle {width,
    color, cap, join, closed}` och **`buildPolyline`**: per segment en quad,
    per skarv en bit av skarvens slag (i varje inre punkt, och för en SLUTEN
    linje också i den första — en sluten linje tappar en upprepad slutpunkt
    och kräver tre punkter), runda ändar som egna bitar, raka och fyrkantiga
    i segmentens `ends`; `dist` i double fortsatt över
    chunkgränser; upprepade och icke-ändliga punkter hoppas över; LÄGGER
    TILL i sista chunken så länge en hel bit ryms, annars ny chunk med
    origo i bitens punkt. Origot är chunkens FÖRSTA punkt — felet ≈ |punkt −
    origo| · 2⁻²⁴, så den som samlar linjer i en chunk håller dem nära.
    **YTORNA (etapp 4):** `FillChunk`, `PolygonStyle` och **`buildPolygon(
    rings, style, chunks)`** — ring 0 ytterringen, resten hål, orienteringen
    fri; rensas som linjerna, ringar utan area tas bort. Trianguleras EN gång
    med den EGNA öronklippningen (`src/earcut.cpp`) och läggs i sista chunken
    om HELA ytan ryms, annars i en ny med origo i ytterringens första punkt
    (en yta större än en chunk delas med samma origo). Svaret är false när
    ringarna inte är en enkel yta (självkorsning, hål utanför — upptäckt med
    en areakontroll): earcuts trianglar står kvar, resten stängs med en
    solfjäder, och det loggas EN gång per episod. **Gradienterna:**
    `GradientStop`, `buildGradientRamp(stops, GradientRamp&)` (256 × 4 byte,
    rak alfa, PREMULTIPLICERAD interpolering mellan stoppen, stabil sortering
    — två stopp på samma offset ger en skarp gräns) och `linearPaint`/
    `radialPaint` (världspunkterna minus chunkens origo, i double).
    **Strecken:** **`makeDash(mönster, förskjutning, SceneDash&)`** — SVG:s
    regler: ett udda antal upprepas en gång, tomt, negativt, icke-ändligt
    eller summan 0 är heldraget (true, `count` 0), och fler än `kMaxDashes`
    efter upprepningen svarar false med `out` heldragen (anroparen loggar);
    `perWorldUnit` sätter anroparen, den är kamerans.
    **Bilden på fyra hörn (CANVAS_TEXT_OCH_BILD):** **`quadWeights(ring,
    q)`** — hörnens projektiva vikter för en fyrhörning i RINGORDNING ur
    diagonalernas skärning (q0 = 1/(1 − t), q2 = 1/t, q1 = 1/(1 − s),
    q3 = 1/s, den största 1; rektangel och parallellogram ger ettor); false
    för allt som inte är strikt konvext (t eller s utanför (ε, 1 − ε),
    parallella diagonaler, ett hörn som inte är ändligt), `q` då orörd.
    Grind: `tests/geom_gate.cpp` (del Q mot en egen homografi).
  - **`geom3d.h`** (CANVAS_3D etapp 4) — 3D-GEOMETRIN som RENA DATA, inget
    vertexformat: **`MeshGeometry`** (lägen i VÄRLDEN i double — origot
    väljer konsumenten —, en enhetsnormal per läge och uint32-tripplar;
    framsidan MOTURS sett ifrån och varje generator vänder den UTÅT, så en
    sluten form har positiv volym). Generatorerna LÄGGER TILL (indexen
    förskjuts — flera former blir en mesh): **`box(a, b)`** (24 lägen, fyra
    per sida, skarpa kanter), **`sphere(c, r, slices = 24, stacks = 16)`**
    (UV-sfär med EN punkt per pol på ±z, klampad till minst 3 × 2),
    **`cylinder(bas, topp, r, sides = 16)`** (godtycklig axel, mjuk mantel,
    skarpa lock; läge 0 åt den världsaxel som står mest vinkelrätt mot
    axeln — x för en lodrät) och **`tube(väg, diameter, sides = 8)`**
    (GERINGSFOG: en ring per punkt i knäets halveringsplan, ramen förd med
    den minsta vridningen, lock i ändarna; ett knä skarpare än
    `kTubeMiterLimit` = 4 — SVG:s spetsgräns, ≈ 151° — delar röret i slutna
    bitar). **`triangulatePlanar(ringar, ut)`** — polygonen i ett godtyckligt
    plan med hål, `buildPolygon`s kontrakt för rensningen; planet ur Newells
    normal (framsidan = den sida ytterringen går moturs kring; en symmetrisk
    fluga utan tecknad area får planet ur sin största triangel), projicerad och
    triangulerad med den egna earcut, men lägena är INDATANS punkter; false
    för en självkorsning (ytan byggs ändå, loggas en gång per episod).
    **`buildPolyline3(punkter, PolylineStyle, LineChunk3-chunkar)`** (etapp
    3) — `buildPolyline`s bitar och regler med punkterna i xyz: origo i xyz
    per chunk, `dist` som 3D-längd, bredden i linjens enhet (världsenheter
    räknas om per vertex i programmet). Grind: `tests/geom_gate.cpp` del
    3D; linjerna i pixlar i G9 del D.
  - **`sdf.h`** (etapp 5) — RITYTANS TEXT OCH MARKÖRER. `kSdfRefSize` (32
    texlar per em), `kSdfRadius` (8 texlar), `kSdfSupersample` (glyferna
    rasteriseras i 2 × referensen), `kSdfPageSize` (1024), den syntetiska
    fetstilen (1/48 em per sida) och kursiven (skjuvning 0,2126).
    `anchorCorner` (REQ8 14) — ankarets hörn, EN regel för etiketterna, markörerna och ritytans förankrade barn.
    **`makeSdf(raster, radius, scale, SdfField&)`**: kanten är täckningen ½,
    avståndet EXAKT euklidiskt (Felzenszwalb–Huttenlocher) till gränsen
    mellan inne- och ute-pixlar, räknat i rastrets upplösning och taget ned
    `scale` gånger; värdet 0,5 + d/(2·radien), positivt inne. **`SdfAtlas`**
    — EN FAMILJ i EN renderer, alla fyra stilarna (CANVAS_TEXT_OCH_BILD):
    den vanliga stilens font i konstruktorn (`valid()`), de andra LAT
    (`font(stil)` — en pekare —, `style(stil)`, `ascent(stil)`,
    `lineHeight(stil)`; en stil som inte går att ladda ritas med den
    vanliga och syntetiseras), glyfnyckeln (stil, kodpunkt); rasteriserar
    (`Font::rasterGlyph`), gör fält och laddar upp varje glyf första gången
    (`updateImage`) på R8-sidor som VÄXER i antal — ingen vräkning, så det
    byggda gäller till `clear()` (bara mellan frames); `advance(cp, stil)`
    ger STEGET utan rasterisering (REQ2 7 — `Font::measure`). **Byggarna:**
    `buildText(atlas, delar, ankare, TextLayoutStyle, chunks)` → `TextBox`
    — DELARNA (`StyledText {utf8, color, style}`) efter varandra, färgen per
    vertex och syntesen per del (strängformen är EN del i `color`/
    `fontStyle`, som står SIST i `TextLayoutStyle`); nio ankare, rader med
    `\n` och `wrapWidth` (`gfx::wrapText`s regel glyf för glyf, varje glyfs
    EGET steg i sin stil), radhöjd och baslinje = de största bland stilarna
    som används, justering, förskjutning, vinkel Viewport/Map/Ground
    (`kGroundAligned`, ingen `kMapAligned`), gloria, platta med luft; plattan,
    alla glorior och alla fyllningar i den ordningen också ÖVER ATLASSIDOR
    (`chunkFor` med en undre gräns per etikett); bredden = `Font::measure`),
    `measureText(atlas, delar, stil)` → `TextMeasure` (SAMMA layout med
    `advance`s steg — ingen uppladdning, REQ2 7); **TAKET** (REQ3,
    `TextLayoutStyle::maxLines`/`maxHeight`, 0 = inget, minst en rad står) kortar
    med `Draw::elideLines` regel — sista raden som ryms bär resten av sitt stycke
    och ellipsen U+2026 i den sista glyfens färg och stil — och `lines` är de
    synliga raderna, `TextMeasure::truncated` att texten kortades; `buildMarkers(punkter,
    MarkerLayoutStyle, chunks)` → `TextBox` (cirkel/kvadrat med kontur
    UTANFÖR fyllningen, eller med `image` en BILDMARKÖR — `kImage`, rutan
    `size` × `size` utan kontur; textens nio ankare för båda, och lådan i em
    relativt punkten med konturen inräknad och ankaret pålagt). Ankarets
    hörn räknas på ETT ställe för text och markörer. `GlyphChunk::page` —
    ett rit-anrop binder EN atlassida (eller markörens bild).
    **I RYMDEN (CANVAS_3D etapp 3):** samma två byggare med `Vec3d`-ankare
    → `GlyphChunk3` (`GlyphVertex3`, origo i xyz): bygget görs i origo i
    planet och LYFTS till ankaret, så quadarna och lådan är 2D-byggets (i
    samma chunkordning — lyftet går aldrig bakåt); chunkens origo är dess
    FÖRSTA ankare, och de följande bär sitt ankare chunk-lokalt (också i z).
    `buildText` laddar upp via atlasen — UI-tråden. Grindar: geom_gate (fält,
    layout, delarna S5, mätningen S6) och scene_pixel_gate D8–D9 (pixlar).
- **`events.h`** — `EventType`/`MouseButton`/`Key`/`kModCtrl|Shift|Alt|Cmd`
  (Cmd = Command/Win-tangenten/Super — egen bit så Cmd/Ctrl kan skiljas) +
  `WindowId` (uint32, 0 = ogiltigt — backends stämplar `Event::windowId`;
  `glaze::App` routar på det via `Window::id()`) + `struct Event`.
  **`Event::mods` BÄRS AV MUSEN, HJULET OCH GESTEN** — läget NÄR eventet
  hände (Win32 `GetKeyState` i meddelandets takt, X11 `state`/XI2
  `mods.effective`, Wayland den senaste `wl_keyboard.modifiers`, macOS
  `modifierFlags`): Ctrl-klick i en lista, Ctrl/Cmd + styrplattans drag som
  kretsar på ritytan. **En modifierartangents EGET event** (`Key::Unknown`)
  bär läget EFTER tangenten på alla plattformar — X11/Wayland lämnar läget
  FÖRE, så backenden lägger på tangentens bit, och macOS `flagsChanged:`
  blir KeyDown/KeyUp. Förbehållet står vid fältet: på Wayland når
  modifierarna bara en yta med TANGENTBORDSfokus (utan fokus är masken 0),
  så ett Ctrl-klick i ett inaktivt fönster kan komma utan Ctrl.
  **Stängningssemantiken:** `WindowCloseRequested` = DET fönstret begär
  stängning (kryss/Alt+F4, id stämplat); `Quit` är reserverat för
  app-nivå (WM_QUIT m.fl., id 0). Oöversatta backend-events =
  `EventType::None` (= "inget hände", ingen omritning).
  **Fönsterhändelse-listan måste täcka allt `AppWindow::handleEvent`
  invalidatar på** — se kommentaren i filen. **`WindowDisplayChanged`**
  (CANVAS_2D etapp 2) är SKÄRMENS byte — fönstret flyttade till en annan
  skärm eller skärmen bytte läge, så `displayMetrics()` svarar annorlunda;
  skalan har sitt eget `WindowDisplayScaleChanged`. **`MouseCaptureLost`**
  (DOCKNING_DRAG E3) = OS:et tog musens capture mitt i ett tryck, och
  släppet kommer aldrig: Windows `WM_CAPTURECHANGED`/`WM_CANCELMODE` — inte
  vid vårt eget släpp och inte vid ett UTTRYCKLIGT övertagande
  (`Window::captureMouse`; ett annat av appens fönster som tar capture på
  annat sätt är en förlust); Wayland `wl_pointer.leave` under ett levererat
  tryck, utom till vår egen popup (REQ2_INMATNING); macOS, X11, headless
  aldrig. Ingen input — passerar modalfiltret. **`Key`-KONTRAKTET står i filen
  och måste läsas innan man binder en genväg:** `Key` är **LEVEL-0-SYMBOLEN i
  användarens AKTIVA layout** — den symbol tangenten ger UTAN modifierare. Alltså
  varken den fysiska tangentpositionen eller den producerade symbolen
  efter Shift. Skälen: `Key` är för KOMMANDON (tecken kommer via
  `TextInput`), ett `Key`-värde får inte FLIMRA när Shift trycks
  (annars går `k == Key::N1 && (mods & kModShift)` inte att skriva), och
  alla tre plattformarna kan svara på just den frågan utan
  dead-key-minor. **Priset, utskrivet:** `Ctrl+Plus` på en layout där `+`
  är skiftat (US) kommer som `Key::Equals` + `kModShift`; en app som vill
  ha "zooma in" binder BÅDA. NUMPADEN är ett skrivet UNDANTAG (level 0 är
  navigationssymbolerna där) och löses ur tangentbordets FAKTISKA läge, så
  numpad-1 är skiljbar från 1; `KP_Enter` fälls MEDVETET ihop med
  `Return`. Enumet bär skiljetecknen, `KP_N0`–`KP_N9` med räknesätten, de
  dedikerade `PageUp`/`PageDown`/`Insert` och NUMPADENS NAVIGATIONSLÄGE som
  EGNA poster (`KP_Home` … `KP_Delete`, `KP_Begin`) — NumLock av på
  Windows/Linux, ALDRIG på macOS (tabellen per plattform står i filen).
  Två hjälpare hör till: **`kLastKey`** (sista posten, för den som itererar
  enumet — `Key` har MEDVETET ingen `Count`-post, den hade blivit ett
  giltigt värde i varje switch; en ny post läggs SIST och konstanten flyttas
  i samma ändring) och **`navigationKey(k)`** (`KP_End` → `End`, allt annat
  orört — `AppWindow` viker med den innan en widget får tangenten; appens
  `event()`-krok och acceleratorerna ser den oviktade). Grind: `tests/numpad_gate.cpp`.
  `SystemThemeChanged` = OS bytte ljus/mörkt (invalidate; värdet läses via
  `systemTheme()`). `TextInput` bär UTF-8 i fast buffert `text[32]`
  (`Event` hålls trivialt kopierbar — den köas per värde i backends, och en
  `static_assert` under structen fäller ett fält som bryter det), och sedan
  REQ3 bär **KeyDown TEXTEN trycket SKRIVER** i samma buffert — exakt det
  TextInput-event backenden köar direkt efter; tom för modifierare, navigering,
  funktionstangenter, styrtecken, en död tangent och Ctrl/Alt/Cmd — inte AltGr
  eller Option; upprepningen bär den igen, KeyUp aldrig, IME och Windows
  Alt+numpad aldrig (vägen per backend står vid KeyDown),
  `MouseWheel` bär rader i `wheelX/wheelY` + muspos, `Timer` bär `timerId`
  (64 bitar: `Window::startTimer`s id, eller med windowId 0 APPENS
  körnings-id ur `timer.h`). **`EventType::Tray`** (SYSTEMFÄLTET,
  SIST efter `Gesture`) bär `trayId` och `trayAction` (`TrayAction {Click,
  DoubleClick, ContextMenu, Enter, Leave, Tick}` — tabellen över OS:ens
  händelser står i filen) med windowId 0, som Quit och Wakeup; mouseX/mouseY
  är ANKARET i PLACERINGSRYMDEN för klicken och 0 för Enter, Leave och Tick.
  Backenden får skicka hover-dubbletter — mottagaren sållar (se `tray.h`).
  **STYRPLATTAN (TODO 53, 2026-10-10)** — kontraktet står vid
  `kPrecisePxPerRow` i filen. TVÅ sorters hjul: `precise == false` är ett
  MUSHJUL (rader), `precise == true` STYRPLATTANS TVÅFINGERSDRAG, vars rader
  är LOGISKA px / `kPrecisePxPerRow` (10) — en lista rullar på raderna som
  förut, ritytan räknar tillbaka exakta px och PANORERAR: innehållet flyttar
  (−wheelX, +wheelY) × raden, en rullad listas tecken, med OS:ets "naturliga rullning" redan tillämpad av backenden
  (appens vändning är RELATIV mot den, REQ9_INMATNING) och AXLARNA ENHETENS: Shift byter aldrig axel i fasaden
  (macOS byte görs om i backenden; glaze byter för det som rullar). `phase` (`GesturePhase` None/Begin/Update/End/
  Cancel/**Momentum** — OS:ets EGEN tröghet efter att fingrarna lyfts) när OS:et vet den. Och **`EventType::Gesture`** —
  NYPNING OCH VRIDNING som EN sekvens Begin → Update… → End/Cancel:
  `gestureScale` MULTIPLIKATIV sedan förra eventet (> 1 = zooma in),
  `gestureRotation` grader sedan förra, MEDURS +, mouseX/mouseY = pekaren
  (gestens ankare). Backenden SLÅR IHOP plattformens källor (macOS magnify +
  rotate, Windows Direct Manipulation + råa HID-kontakter) till en sekvens.
  Inga nya virtuella: eventen är data från varje backends väg (src/CLAUDE.md).
- **`renderer.h`** — abstrakt `gfx::Renderer`: `clear`/`fillRect`/`strokeRect`/
  `line`/`drawText` + de rikare primitiven `fillRoundedRect`/
  `strokeRoundedRect(…, width)`/`dropShadow`/`innerShadow` (fysiska px;
  radius klampas till halva kortsidan; D3D11 ritar dem analytiskt med SDF,
  backends utan SDF-stöd tesselerar) + font-fabrikerna `loadFont(path, …)`
  (godtycklig .ttf — stilen är filens egen) och `loadFont(FontFamily, …,
  FontStyle)` (systemfont via katalogen i den begärda stilen, eller den
  NÄRMASTE inom samma familj — `Font::style()` säger vilken; stilen är
  obligatorisk sedan CANVAS_2D etapp 5) — returnerar ALLTID ett objekt;
  `valid()` avgör. `loadDefaultFont` = icke-virtuell bekvämlighet för
  UI-familjen i vanlig stil. **Bilder:** `loadImage(path)`
  / `loadImage(data, size)` (BARA PNG sedan CANVAS_3D beslut 5 — genom
  `decodeImage`, se `image.h`; alltid ett objekt, `valid()` avgör) +
  **`drawImage(img, src, dst, opacity)`** — EN virtuell (käll-rect för
  sprites) med OPACITETEN obligatorisk, och helbildsformen
  `drawImage(img, dst, opacity = 1)` som icke-virtuell bekvämlighet i
  `renderer.cpp`. Opaciteten (0–1, klampas) multiplicerar bildens EGEN alfa
  — en inaktiv ikon tonas mot vilken yta som helst. **Klipp-stacken**
  `pushClip(Rectf)`/**`pushClip(Rectf, radius)`**/`popClip()`: icke-virtuella
  med delad stack-logik (intersektion, fysiska px, rundas UTÅT till hela
  pixlar — `src/renderer.cpp`); backends implementerar bara skyddade
  **`applyClip(const Rectf* scissor, const RoundedClip* masks, size_t
  count)`** (Metal klampar dessutom mot render-målet — krav). Balansera
  anropen. **`clipBounds(Rectf&)`** (CANVAS_2D etapp 8, icke-virtuell) är
  FRÅGAN om det klipp som gäller nu: stackens topp, alltså scissorn före
  backendens avrundning, utan maskerna; false när stacken är tom.
  Overlay-vandringens grind (`middle_gate`) mäter klippet med den.
  **RUNDAT BARNKLIPP:** `RoundedClip {rect, radius}` + `kMaxRoundedClips` (= 4).
  Varje stacknivå bär scissor-snittet, sin EGEN rect och radie (masken härleds
  ALDRIG ur snittet). `applyClip` får de fyra YTTERSTA rundade nivåerna ur hela
  stacken — ICKE-DESTRUKTIVT; en femte degraderar till sin rektangel (exakt i
  scissorn) + EN logg per episod, och rektangulära nivåer tar aldrig en
  maskplats. Scissorn är exakt och inne/ute per mask exakt; approximationen finns
  bara i AA-bandet (≤ 1 px) där två maskers band korsas. Masken är UNIFORM-state
  (`count == 0` kostar inget); hit-testen (glaze) följer inte radien.
  Ingen `present()` (Window::renderFrame presenterar). Fler UI-primitiv
  läggs till först när en widget behöver dem.
  **SCENEN (CANVAS_2D etapp 1, 2026-10-09)** — fyra RENA virtuella bredvid
  UI-pipelinen (tre backends + `NullRenderer` måste svara):
  **`createBuffer(kind, bytes, data)`** (en `GpuBuffer` som LEVER mellan
  frames; alltid ett objekt, `valid()` avgör, fontens livstidsregel),
  **`updateBuffer(buf, offset, data, bytes)`** (det som redan ritats i
  framen behåller det GAMLA innehållet — Metal väljer väg ur en stämpel per
  resurs sedan REQ7_RITYTAN: aldrig ritad ⇒ direkt, ritad i en tidigare frame
  ⇒ ett uppladdningspass före framen, ritad i SAMMA frame ⇒ ett encoder-brott;
  offset och bytes i hela FYRBYTES, annars false och inget skrivs — Metals
  blit kräver det, så alla tre kräver det), **`uploadTransient(kind, data,
  bytes)`** (renderarens transienta ring, inom en frame — delen gäller till
  framens slut; det omedelbara lägets väg) och **`drawMesh(MeshDraw)`**
  (i det AKTIVA målet, under klippstacken — scissorn OCH de rundade
  maskerna — och i målarordning med UI:t: det som batchats före ritas före;
  programmens filer står i backendarnas CLAUDE.md). Reglerna som är lika i
  alla backends bor i skyddade **`meshDrawOk`/`updateOk`** (renderer.cpp):
  giltiga delar, rätt buffertslag, indexCount inom delen, offset i hela
  fyrbytes, och för Line en ändlig spetsgräns ≥ 1 och streck i `makeDash`s
  form (jämnt antal, icke-minskande slut, period och skala över noll —
  shadern delar med dem); loggar en gång per orsak.
  **`sceneImageFlags(img)`** och **`sceneImageAdjust(u, ut)`** (skyddade, statiska)
  är bildens egenskaper som scenprogrammen läser dem — 1 premultiplicerad,
  2 speglad v, 4 R8 — delad av Image-programmet och Glyph-programmets
  bildmarkörer i alla tre backends, resp. Image-programmets färgjustering. **`RenderStats`** + icke-virtuella **`stats()`/`resetStats()`**
  (etapp 0): `drawCalls`, `programSwitches` (ett annat program togs i bruk
  mitt i en frame — UI-pipelinen räknas som ett), `buffersCreated`,
  `bytesUploaded` (BARA scenens uppladdningsvägar — UI-batcherns ström och
  kamerans uniformer räknas inte, det är det som gör "en kameraändring
  laddar inte upp något" mätbart), `vertices` (i ett instansierat anrop ×
  instanserna) och sedan CANVAS_3D `instances` — ritade instanser i
  INSTANSIERADE anrop (de egna programmen, etapp 1; ett vanligt anrop räknas
  inte) — och `programsBuilt` (lyckade `createProgram`; ett ogiltigt program
  och backendens varianter räknas inte) och sedan REQ7_RITYTAN
  `uploadBreaks` (uppladdningar som AVSLUTADE ett öppet render-pass — Metals
  brott-väg, stegad i `beginBlit`; D3D11 och GL har inget pass att bryta och
  svarar alltid 0) och sedan PRESTANDA A2 `presentNs` (presentationens
  väntan — D3D11 `Present`, Metal semaforen + `nextDrawable`, GL swappen +
  Waylands `beforeSwap`), `compileNs` (kompileringen; en träff i D3D11:s
  processcache kostar 0) och `targetsCreated`. Backendarna stegar dem i den
  skyddade `stats_` där arbetet görs; inget nollställs av sig självt
  (profileraren läser differenser). Tiderna mäts med den skyddade RAII-typen
  **`StatTimer(ns)`**. **`frameSerial()`** (icke-virtuellt, basens
  `frameSerial_`) stegas av backenden i beginFrame OCH `beginOffscreen` (0 =
  aldrig). **GPU-TIDEN:** icke-virtuella `setGpuTiming`/`gpuTiming`/
  `gpuTimingSupported` och `takeGpuTimes(out)` (LÄGGER TILL `GpuTime
  {serial, ns}` och tömmer); backenden lämnar svaren med skyddade
  `addGpuTime` i BREVLÅDAN `GpuTimeBox` — en `shared_ptr` med eget lås, så
  Metals completion handler kan svara efter att renderaren rivits — som
  håller de `kMaxGpuTimes` (64) senaste. `NullRenderer` mäter ingenting.
  **BILDER UR PIXLAR (CANVAS_2D etapp 3)** — två RENA virtuella:
  **`createImage(w, h, ImageFormat, px, stride, mipmaps)`** (5 arg = utan
  kedja; `px` nullptr = NOLLSTÄLLD; stride utan hela rader/pixlar eller mått över
  **`maxTextureSize()`** ⇒ ogiltig + logg; MIP-KEDJAN (REQ8 5) räknas på GPU:n
  vid skapandet och efter varje skrivning) och **`updateImage(img, x, y, w, h, px,
  stride)`** (pixlar i bildens format; det som redan ritats i framen
  behåller det GAMLA innehållet — D3D11 och GL flushar UI-batchen före
  UpdateSubresource/glTexSubImage2D, Metal går stämpelns väg som buffertarna;
  rektangel utanför, fel stride eller ett render-måls bild ⇒ false).
  `loadImage` går genom `decodeImage` + `createImage`, så `bytesUploaded`
  räknar bildernas byte också. **SAMPLERN ÄR BATCH-TILLSTÅND (E3):**
  `drawImage` samplar med bildens filter (Linear som förval), och ett
  filterbyte bryter UI-batchen som ett texturbyte; text, geometri och
  compositen tar Nearest UTTRYCKLIGEN, och varje ritväg binder sin egen
  sampler vid varje ritning — på GL skuggar ett bundet samplerobjekt
  texturens parametrar, så en väg som ärvde det förra hade samplat fel
  (scene_pixel_gate B2 mäter läckan åt båda håll). `drawImage` ritar en R8-bild
  som gråskala, av-premultiplicerar ett målets bild och speglar en `flippedV`-bild.
  **RENDER-MÅLEN (CANVAS_2D etapp 6)** — **`createRenderTarget(w, h,
  TargetDesc)`** (ren virtuell sedan CANVAS_3D etapp 0 — MSAA och djup, se
  `image.h`; nollställd bild, alltid ett objekt; mått ≤ 0 eller en
  provtagning som inte är 1/2/4/8 ger ett ogiltigt mål, `targetDescOk`) och
  den ICKE-virtuella bekvämligheten **`createRenderTarget(w, h)`** = 2D-målet
  (`TargetDesc{}`) — en backend som overridar den virtuella tar
  `using Renderer::createRenderTarget;`, annars skuggar den bekvämligheten.
  De ICKE-virtuella **`pushTarget(mål, rensfärg)`/`popTarget()`**:
  KLIPPSTACKEN HÖR TILL MÅLET — push SPARAR och TÖMMER den, pop binder det
  yttre målet (eller framens eget) och lägger tillbaka den, så klippet efter
  pop är exakt det före push. Backenden implementerar bara skyddade
  **`bindTarget(mål | nullptr)`** (flushar, binder målet med viewport och
  storlek — och LÖSER UPP ett MSAA-mål som lämnas, både i pop och när ett
  nästlat mål binds ovanpå) och kallar **`beginFrameTargets()`** i sin
  beginFrame (en obalanserad push från förra framen loggas och töms).
  **`imageDrawOk(img)`** är ritvägarnas vakt mot att sampla det BUNDNA målets
  egen bild. Mål nästlas; anropen sker inom en frame eller ett
  offscreen-block. **`clear()` lagrar färgen PREMULTIPLICERAD** i alla
  backends — varje mål bär premultiplicerat innehåll — och rensar ett måls
  djup till 1. Bildernas delade regler: skyddade **`imageDescOk`/
  `imageUpdateOk`** (renderer.cpp, loggar en gång per orsak som scenens).
  **3D-MÅLENS FASAD (CANVAS_3D etapp 0)** — fyra RENA virtuella till:
  **`supportedSamples(n)`** (den närmaste stödda ≤ n, minst 1; färgen och
  djupet prövas båda — D3D11 `CheckMultisampleQualityLevels`, GL
  `GL_MAX_SAMPLES`, Metal `supportsTextureSampleCount:`),
  **`readTarget(mål, rgba)`** (målets bild → RGBA8, rad 0 överst, RAK alfa —
  `captureFrame`s form; UTANFÖR en frame och med målet obundet, för Metal
  läser först när GPU:n kört klart; false för ett bundet eller ogiltigt mål)
  och **`beginOffscreen()`/`endOffscreen()`** (ritning UTANFÖR en frame, bara
  i render-mål — D3D11 binder UI-pipelinens tillstånd, som beginFrame annars
  gör och som saknas före första framen, GL gör kontexten aktuell, Metal
  öppnar en egen command buffer som `endOffscreen` committar och väntar in;
  false inuti en frame). Skyddade hjälpare i renderer.cpp: **`targetDescOk`**,
  **`readTargetOk`** (ett mål i målstacken läses inte), **`boundTarget()`**
  (toppen av stacken — under `bindTarget` redan det NYA målet) och
  **`targetInStack(mål)`** (ligger det kvar? Metal behåller provtagningen
  bara då). Och den icke-virtuella **`deviceName()`** (GPU:n: DXGI-adapterns
  beskrivning, `GL_RENDERER`, `MTLDevice.name` — backenden fyller skyddade
  `deviceName_`) och **`adapterId()`** (kortets `GpuAdapter::id`, `adapterId_`,
  tomt för mjukvara; grind: G9 K1–K4); varje backend loggar "ritar med <GPU>
  [<id>] …" när en enhet skapas på en ANNAN GPU än förra (PRESTANDA A1).
  **EGNA PROGRAM (CANVAS_3D etapp 1)** — en ren virtuell till:
  **`createProgram(ProgramDesc)`** (`program.h`; alltid ett objekt, ett
  ogiltigt loggas EN gång där och ritas aldrig, får anropas utanför en frame;
  programmet hör till renderern som en font). `drawMesh` tar
  `MeshProgram::Custom` under samma regler som de inbyggda — klippstacken,
  målarordningen, renderläget. Skyddade hjälpare i renderer.cpp, så att
  felen läses likadant i alla tre: **`programDescError`** (GlazeUser-
  blockets storlek, texturantalet, definitionernas form), **`programError`**
  ("programmet '<namn>' …") och **`programSourceName`** (namnet i `#line 1
  "<namn>"`, utan tecken utanför ASCII-identifierare, `-` och `.`); privata
  **`customDrawOk`** (Custom-reglerna, se `program.h` ovan) och **`meshVerdict`**
  (svansen den och `meshDrawOk` delar: djuploggen eller orsaken, en gång per bit).
- **`null_renderer.h`** (CANVAS_2D T11, etapp 0) — `NullRenderer`/
  `NullFont`/`NullImage`/`NullGpuBuffer`/`NullRenderTarget`: varje ren
  virtuell som no-op som HÅLLER kontraktet (fabrikerna ger ogiltiga objekt,
  aldrig nullptr; NullFonten svarar självkonsekvent med glyfer av bredd
  noll, stilen Regular och ingen rasterisering). Header-only och fri från
  backend-typer. Grindarnas test-renderare och testfonter ärver härifrån och
  överskuggar BARA det de spelar in — en ny ren virtuell kostar de tre
  backendarna plus EN rad här. Produktionskoden har den på ETT ställe:
  Linux-proxyns `renderer()` utan inre fönster (före init, efter `shutdown()`).
  CANVAS_3D etapp 0: `NullRenderTarget` svarar ett prov och inget djup,
  `supportedSamples` 1, `readTarget` och `beginOffscreen` false — och
  `using Renderer::createRenderTarget;` bär bekvämligheten vidare. Etapp 1:
  `createProgram` ger en `NullShaderProgram` — ogiltig, och felet säger att NullRenderer inte kompilerar.
- **`image.h`** — abstrakt `gfx::Image`: `width`/`height`/`valid` + sedan
  CANVAS_2D etapp 3 de rena virtuella **`format()`** (`ImageFormat {Rgba8,
  R8}` — R8 är data för scenens program och ritas som GRÅSKALA;
  `bytesPerPixel(f)`), **`premultiplied()`** (sant för ett render-måls bild)
  och **`flippedV()`** (sant för GL:s render-mål — FBO:n lägger bildens
  överkant på v = 1) och det ICKE-virtuella FILTRET `filter()`/`setFilter`
  (`ImageFilter {Linear, Nearest}`, Linear som förval — E3; batch-tillstånd,
  med kedja trilinjärt/närmaste nivå). Skapas av sin `Renderer` (create…) och
  följer FONTENS livstidsregler: texturen hör till renderern — släpp bilden
  före fönstret rivs. Mått i fysiska px. **`RenderTarget`** (etapp 6): en
  yta att rita i — `image()` är målets bild (premultiplicerad), som går
  till drawImage och Image-programmet; HAR en bild i stället för att VARA en,
  så varje backend kan ge sin egen bildtyp utan diamantarv. Sedan CANVAS_3D
  etapp 0 med **`samples()`** (provtagningen målet FAKTISKT fick — kan vara
  lägre än begärt) och **`hasDepth()`**, och formen **`TargetDesc {samples
  = 1, depth = false}`**: MSAA 1/2/4/8 (den närmaste stödda ≤ begärt, loggat
  en gång när den går lägre) och ett DJUP i D32F i alla tre backends (Apple
  silicon saknar Depth24Stencil8, och biasens enhet blir densamma),
  standard-Z rensat till 1. Med MSAA är `image()` den UPPLÖSTA bilden — ett
  prov per pixel, upplöst varje gång målet lämnas.
  Plus fria **`encodePng(path, rgba, w, h)`** (GLAZE_RENDER): rå RGBA8
  (rad 0 överst, täta rader, rak alfa) → PNG-fil — encode-spegeln av
  loadImage, per backend med samma OS-komponenter (WIC / Image I/O /
  libpng), ingen ny dependency; false + logg vid fel. Och
  **`decodeImage(data, size, rgba, w, h)`** (etapp 3): fildata → samma
  RGBA8-form UTAN renderer, TRÅDSÄKER (WIC med COM per anrop och tråd,
  Image I/O, libpng:s simplified API) — en arbetartråd avkodar plattor.
  **BARA PNG PÅ ALLA PLATTFORMAR (CANVAS_3D beslut 5):** de åtta första byten
  prövas mot PNG-signaturen INNAN WIC/Image I/O får datat (den delade
  `src/image_util.h`), så JPEG, BMP och GIF svarar false + logg också där OS:et
  hade avkodat dem. `loadImage(path)` läser filen och går samma väg. Bildavkodning utöver PNG
  är appens (grind: G9 P1–P3, med en riktig JPEG som WIC avkodar utan provet).
- **`text.h`** — font-systemet + abstrakt `gfx::Font`:
  - `FontFamily { UI, Mono }` — CSS-tänket: en familj är en fallback-kedja
    av systemfonter som backends löser via `src/font_catalog.cpp` (ENDA
    stället med fontsökvägar).
  - `fontsize::{kUi, kMono, kTitle, kUiCompact, kUiHeading, kUiHeadingCompact, kUiSubheading,
    kUiSubheadingCompact}` — standardstorlekarna (logiska px, × `uiScale()` vid laddning). Inga
    sifferstorlekar ute i koden — nya användningsfall får en namngiven konstant här. **`kUiHeading` (20)
    och `kUiHeadingCompact` (15)** är rubrikens (`Style::headingFontSize`) — två, för en familj med
    `kUiCompact` (10) i brödtexten kan inte ha 20 px rubrik; **`kUiSubheading` (16/12)** är Markdown-vyns H2.
  - `gfx::Font`: `measure()`/`height()`/`valid()` + **text-mätning/hit-test**:
    `prefixWidth(utf8, bytes)` (monoton prefixbredd), `byteIndexAtX(utf8, x)`
    (närmaste teckengräns — klipper aldrig mitt i ett UTF-8-tecken),
    `fitWidth(utf8, maxW)` (hur många hela tecken ryms). Skapas av sin
    `Renderer`, ritas via `Renderer::drawText` — fonten hör till renderern
    (glyf-texturer lever i backendens renderresurser).
  - **Ritytans text (CANVAS_2D etapp 5):** `FontStyle {Regular, Bold,
    Italic, BoldItalic}` + `isBold`/`isItalic`/`fontStyle(bold, italic)`,
    `GlyphRaster` (en glyf som täckning i fontens storlek: mått, hörnet
    relativt pennan på baslinjen — top NEGATIV ovanför — steget och rad 0
    överst) och tre rena virtuella i `Font`: **`ascent()`** (baslinjen under
    radens överkant, som `drawText` lägger den), **`style()`** (det filen
    FAKTISKT är — läst ur fonten, aldrig ur begäran) och
    **`rasterGlyph(cp, out)`** (DEN rasterisering fonten ritar sin text
    med, lyft ur atlasskrivningen; en saknad glyf tas ur en reservfont,
    annars U+FFFD och sist .notdef — som `drawText`; false bara för en
    ogiltig font). SDF-atlasen (`sdf.h`) är konsumenten.
  - **GLYFRESERVEN (CANVAS_TEXT_OCH_BILD)** — ren virtuell **`hasGlyph(cp)`**:
    en RIKTIG glyf (fontens egen eller en reservfonts) eller
    ersättningstecknet — läsbar degradering (appen kan välja "~" när "≈"
    saknas; `NullFont` svarar false). Varje backends `Font` slår upp en
    kodpunkt den saknar i OS:ets EGEN reservkedja (DirectWrite
    `MapCharacters`, Core Text `CTFontCreateForString`, fontconfigs
    `FcFontSort`) med EN regel (`src/font_fallback.h`): egen glyf → reserv →
    U+FFFD → .notdef; ett styrtecken söker ingen reserv och är aldrig
    riktigt. Svaret cachas per (font, kodpunkt), också negativt, och
    mätningen, `drawText` och `rasterGlyph` läser samma — de kan inte vara
    oense om en saknad glyf. Grind: `scene_pixel_gate` del R.
  - `wrapText(font, utf8, maxW) → vector<WrappedLine>` — radbrytning på
    ordgränser ('\n' bryter alltid; långa ord hårdbryts). DELAD
    backend-neutral logik (`src/text_util.cpp`) ovanpå `fitWidth`.
- **`window.h`** — `Chrome` (Native/Custom/**None** = fri yta utan
  titelrad/drag — transparenta fönster, popups)/`defaultChrome()`/
  `WindowDesc` (title/width/height — **LOGISKA px** sedan fasadändringen
  (BACKEND_LINUX_NATIVE): backends multiplicerar själva till sin fysiska
  enhet (Win32 × dpi/96, Mac punkter, Wayland ytenheter × textskalning,
  X11 × skala); även `setSize` tar logiska px — /chrome +
  **popup-kontraktet** `popupAnchorWindow`/`popupAnchorRect` (ankarrect i
  ägarens klient-FYSISKA px; backenden placerar fönstret under rectet och
  ÄGER flip-uppåt/klamp — Wayland via xdg_positioner, D3D11/Metal/X11 via
  sina clientToScreen/screenArea internt; ersätter skärmpositions-vägen
  för popups; **fokus-kontraktet**: popup + ankarfönster är EN fokusgrupp —
  öppning/stängning får ALDRIG ge ägaren WindowFocusLost/Gained, eftersom
  glaze::App stänger popups på ägarens fokusförlust. Windows/Mac/X11
  uppfyller det per konstruktion, Wayland maskerar det aktivt) +
  **fönster-attributen**: `owner` (Z-ordning/minimering — ägarskap, ALDRIG
  koordinater), `posX/posY` (skärm-px; `kWindowPosDefault` = OS väljer),
  `noActivate` (PERMANENT), **`showInactive`** (DOCKNING_DRAG L6 — bara den
  FÖRSTA visningen aktiverar inte; Wayland och headless är dokumenterade
  no-op, tabellen i headern), `toolWindow`, **`showInTaskbar`** (SYSTEMFÄLTET
  B8 — false = ingen knapp i aktivitetsfältet; ett STARTVÄRDE i init, aldrig
  växlat i drift — tabellen står i headern. `showInactive` och
  `showInTaskbar` är attribut och inga rena virtuella: proxyn skickar `desc`
  orört, så proxyfällan finns inte), `transparent` (per-pixel-alfa; klick i
  genomskinliga pixlar släpps IGENOM till fönstret under —
  alfa-hit-test-kontraktet, Windows/D3D11 är referensen). Fältet är en
  **BEGÄRAN**, inte ett faktum: X11 utan ARGB-visual degraderar till opakt
  och loggar, så den som ritar genomskinliga pixlar måste fråga
  `Window::transparent()` efter init. Linux-backenden behåller chromet för
  en transparent yta (ett RUNDAT fönster har namnlist); popups tvingas till
  None, och d3d11/metal tvingar en transparent yta till None. `hidden`
  (fönstret skapas DOLT — renderer + retained-RT finns ändå: offscreen-
  renderingens väg och — sedan TODO 80 — varje fönsters FÖRSTA visning)
  + **`startState`** (FONSTER_OCH_TIMER — läget vid FÖRSTA synligheten,
  också efter en dold start; Wayland aldrig minimerat) + **storleksgränserna**
  `minWidth`/`minHeight`/`maxWidth`/`maxHeight` (APP_API B1 — LOGISKA px,
  0 = obegränsat per axel; `min == max` LÅSER axeln, så "ej ändringsbar"
  behöver inget eget attribut. Desc-fälten är bara STARTVÄRDET för
  `setSizeLimits` nedan) + **`topmost`** (APP_API_3 A — alltid överst;
  startvärdet för `setTopmost` nedan)) + **`setSizeLimits(minW, minH, maxW, maxH)`**
  (REN VIRTUELL — alla SEX fönsterimplementationer svarar.
  TRE AV FYRA backends måste ÅTERAPPLICERA vid skalbyte: Mac räknar i
  punkter × user, Wayland i ytenheter, X11 i fysiska px — bara Windows
  räknar vid FRÅGAN, `WM_GETMINMAXINFO`, och får skalbyten gratis)
  + **`setTopmost(bool)`/`topmost()`** (APP_API_3 A — ALLTID ÖVERST, över
  andra appars fönster; `raise()` lyfter bara EN gång. REN VIRTUELL, SEX
  implementationer (proxyn LAGRAR pre-init och vidarebefordrar i `init()`);
  tabellen står i headern — **Wayland KAN INTE** (no-op som LOGGAR en gång,
  `topmost()` alltid false). Gettern svarar med vad som FAKTISKT gäller,
  aldrig med begäran — `transparent()`s prejudikat)
  + **`uiScale()`-kontraktet** (L12/US): fönstrets FULLA skala = osScale ×
  `gfx::userScale()`, faktorn in EXAKT en gång per backend, läst färskt per
  repaint — och **`userScaleChanged()`** (ren virtuell): räkna om fysisk
  storlek ur den lagrade LOGISKA (aldrig inkrementellt) och pusha
  `WindowDisplayScaleChanged` EFTER att renderern bytt storlek; maximerat ⇒
  bara eventet, och resize-kroken räknar ALDRIG tillbaka logisk storlek ur
  maximeringens eller återställningens rect (restore-flanken sätter om den
  fysiska, ETT försök per skuld); ingen polling (regel 1) + `ChromeColors`
  (chromets färger; defaults = klassiska mörka chromet — temasystemet i
  glaze trycker ner aktiva temats färger per repaint) + abstrakt
  `gfx::Window`: `init`/`shutdown`, `id()` (fönstrets `WindowId`; 0 före
  init), `raise()` (lyft + fokus; ett DOLT fönster lyfts inte utan förblir dolt — SYSTEMFÄLTET, Macs `makeKeyAndOrderFront` hade visat det; ett noActivate-fönster lyfts UTAN aktivering — TODO 87), `processEvent` (**false = "det här
  fönstret begär stängning"** — anroparen/glaze::App verkställer),
  `setRenderCallback` (frame-innehåll → den **persistenta retained-RT:n**;
  ritning under modal OS-resize), `setOverlayCallback` (ritas efter
  composite + chrome, direkt i backbuffern — rent informativt, självstädande
  lager), `renderFrame` (callback → retained RT · composite → backbuffer ·
  chrome · overlay · present), `retainedValid()` (sant när RT:n bär förra
  framens innehåll — callbacket får bara rita PARTIELLT då; falskt efter
  resize/i degraderat läge = rita allt), `contentArea`, `uiScale`,
  `takeDirty` (chrome-dirty för händelsestyrd rendering),
  **fönster-kontrollerna** `minimize()`/`maximize()`/`restore()` (+ läsaren
  `maximized()` och den rena virtuella **`placement()`** → `WindowPlacement`
  — NORMALLÄGETS plats och logiska klientyta, spårad genom max/min och satt
  före den första; platsen är KLIENTENS hörn för eget chrome också med
  Windows osynliga storleksram och Linux storleksmarginal (DOCKNING_DRAG
  K3/E2L — `setPosition(clientToScreen(0, 0))` är en identitet), Native på
  Windows ytterhörnet; `hasPosition` false på Wayland/headless; proxyn
  vidarebefordrar, geometry_gate W1/K1/KL) — programmatiska min/max/
  återställ (CAPTION_BARS: CaptionButton-vägen; `maximize()` är REN —
  växling görs av anroparen, `restore()` tar fönstret ur båda lägena,
  no-op-säkra före init; före den FÖRSTA synligheten byter de STARTLÄGET i
  stället för att visa fönstret — TODO 80, tabellen i window.h),
  **hit-test-kartan** `HitRole`/`HitRegion` + `setHitRegions(vector)` +
  `setCaptionHeight(logiska px)` — FÖNSTRET (glaze) är källan: kartan
  räknas ur widget-trädet och trycks ner per repaint, backenden slår upp
  i sin nativa hit-test (första träffen vinner; knapp-roller = Client i
  dag — HTMAXBUTTON/snap-layout uppskjutet); caption-höjden ur trädet
  styr `contentArea`/`titlebarHeight` (settbar barhöjd ⇒ rätt klientyta).
  **RESIZE-ROLLERNA ÄR ZONERNA INNANFÖR klientytan** och tar de yttersta
  px av allt innehåll vid kanten, så där OS:et har en storleksram UTANFÖR
  den synliga kanten hoppar backenden över dem (DOCKNING_DRAG K1–K2):
  D3D11 (Windows osynliga ram), Metal (AppKits kanter) och Linux med sin
  genomskinliga storleksmarginal (E2L); Linux läser dem bara där ingen
  marginal finns (X11 utan kompositör/`_GTK_FRAME_EXTENTS`, tiled Wayland),
  `releaseMouseCapture()` (släpp backendens mus-capture i förtid — popup
  öppnad MITT I ett tryck ser annars inga mus-events förrän släppet;
  D3D11 = ReleaseCapture om fönstret håller den — vårt eget släpp, inget
  `MouseCaptureLost` — Metal = dokumenterad no-op, implicit capture kan
  inte släppas), **`captureMouse()`**/**`hasMouseCapture()`** (DOCKNING_DRAG
  Codex 2 — RENA VIRTUELLA, sex implementationer: TAR ett pågående tryck från
  ett ANNAT av appens fönster, respektive FRÅGAR om fönstret håller det; true
  bara på D3D11, när hållaren har ett TRYCK (inte OS:ets flyttloop) och appen
  äger förgrunden — de andra svarar false; proxyn vidarebefordrar efter init;
  grind `tests/capture_gate.cpp`),
  **MUSPEKAREN** (post 15): `enum class PointerShape { Arrow, IBeam, Hand,
  ResizeWE, ResizeNS, NotAllowed, Crosshair, Move, ClosedHand, Hidden, Wait }`
  (`Move` är den stängda handen på macOS, som saknar en publik fyrpil;
  `ClosedHand` är VÅR tvåtonsbild på Windows och X11, `src/cursor_bitmaps.h`
  — undantaget från systempekarna; `Wait` är pilen på macOS) +
  `setCursor(shape)`/`cursor()` — den form KLIENTYTAN vill ha (chromets
  resize-zoner och en ram utanför klientytan vinner alltid och sköts av
  backenden). Heter `PointerShape` och inte `CursorShape` därför att X11:s
  `X.h` definierar `CursorShape` som MAKRO.
  `cursor()` svarar med den senast BEGÄRDA formen, inte vad OS:et visar
  (det kräver en riktig mus). No-op-säker före init — SEX implementationer,
  proxyn LAGRAR före `init()` och vidarebefordrar (splitter_gate F1). Inget
  "+"-märke: drag-and-drops märke ritas själv (`Draw::badge`),
  **SKÄRMENS FYSISKA UPPLÖSNING** (CANVAS_2D etapp 2, 2026-10-09):
  `DisplayMetrics { pxPerMm, source, widthMm, heightMm, id, reason }` +
  ren virtuell **`displayMetrics()`** — FYSISKA px (fönstrets buffert, de
  `uiScale` räknar till) per mm på skärmen fönstret står på, och VARIFRÅN:
  `DisplaySource::Measured` (EDID-mått mot pixlar, ur DIAGONALEN),
  `Assumed` (96 dpi i FYSISKA px = `kAssumedPxPerMm` vid varje skala, sedan
  REQ2_RITYTAN B4 — `reason` säger varför) eller `Calibrated` (läggs på av
  `gfx::effectiveDisplay`, platform.h — backenden svarar aldrig själv med
  den). Backenden mäter vid init och när fönstret kan ha bytt skärm eller
  skärmen läge, och köar då `EventType::WindowDisplayChanged`; frågan läser
  bara det mätta. `id` är tillverkare-produkt-serie ur EDID (samma form på
  Windows och macOS), annars utgångens namn — kalibreringens nyckel. SEX
  implementationer; proxyn (Linux) svarar med det inre fönstrets mätning
  efter init och har inget att lagra före (grind: `tests/display_gate.cpp`
  F2). Vägen per OS står vid deklarationen,
  **PROGRAMIKONEN** (PROGRAMIKON, 2026-10-11): `IconImage { width, height,
  rgba }` (RAK alfa, rad 0 överst — `captureFrame`s form) och `WindowIcon
  { images, programFirst }` + rena virtuella **`setIcon(icon)`/
  `iconApplied()`**. Ramarna KOPIERAS; en TOM lista = ingen ikon. Backenden
  väljer ram per storlek med valregeln (`src/icon_util.h`: minsta sida ≥
  målet, annars största) och skalar ALDRIG själv. `programFirst` =
  ramverkets förval: programfilens egen ikon (exe-resurs 1, paketets
  `CFBundleIconFile`) vinner. `iconApplied()` svarar med vad som FAKTISKT
  gäller (topmost-prejudikatet): Wayland utan `xdg-toplevel-icon-v1` och
  headless svarar false. DEGRADERINGSTABELLEN står i headern — macOS dockikon
  är APPENS. SEX implementationer; proxyn LAGRAR och vidarebefordrar,
  `setSize`/`setTitle`/`setVisible` (**no-op-säkra före init** — genererad
  kod sätter titel/storlek i fönsterklassens ctor, före show; setVisible bär
  `AppWindow::hide()`/åter-show), **skärmpositionen** — behålls för
  backends interna bruk men är DEGRADERAD på Wayland (fasadändringen —
  protokollet saknar skärmrymd): `setPosition(x, y)` (Wayland: no-op +
  logg; annars OS:ets placeringsenhet, eget chrome = klientens hörn),
  `clientToScreen(px, py)` (Wayland: klient-px/skala) och `screenArea()`
  (Wayland: tom rect). Popup-placering som ska fungera överallt går via
  popup-kontraktet i WindowDesc, aldrig via de här; `minimize()` är
  dessutom ENKELRIKTAT på Wayland (ingen query/programmatisk
  av-minimering), `setChromeColors` (alltid
  säker; slår igenom nästa drawChrome), `startTimer(id, ms)`/`stopTimer`
  (upprepande, väcker `waitEvent` — vakna på tid utan polling; id > 0,
  no-op-säkra före init), `setTextInputActive(bool)` (IME-kroken — no-op
  i alla nuvarande backends, tecken levereras alltid),
  **`captureFrame(rgba, w, h)`** (GLAZE_RENDER: senast renderade framen ur
  retained-RT:n — rå RGBA8, rad 0 överst, FYSISKA px = klientyta × native
  scale, 1:1 utan omsampling; anropas EFTER `renderFrame()`; false i
  degraderat läge/mitt i öppen frame — glaze-renders väg),
  **`transparent()`** (bär ytan FAKTISKT per-pixel-alfa? svaret på
  begäran ovan, giltigt efter init) och `renderer()`.
- **`platform.h`** — fria backend-funktioner: `init`/`shutdown`,
  `waitEvent`/`pollEvent` (händelsestyrd pump), **`postWakeup()`**
  (den ENDA fasadfunktionen som får kallas från en ANNAN tråd: väcker OS-pumpen
  OCH köar ett `EventType::Wakeup` — båda krävs, rot-CLAUDE.md; no-op före
  `init()` och efter `shutdown()`), `log` (printf-stil),
  `basePath`, **`isInProgramFile(adress)`** (DELAT_BIBLIOTEK R3 — i exe:n, inte
  i en DLL? programfilens ankare frågar), `ticksMs` (backend-neutral), **`doubleClickMs()`** (post 17 —
  dubbelklickets tidsfönster ur OS:ets ANVÄNDARINSTÄLLNING, en fråga till
  OS:et av `systemTheme`s skäl: Win32 `GetDoubleClickTime()`, macOS
  `NSEvent.doubleClickInterval`, Linux portalens
  `org.gnome.desktop.peripherals.mouse/double-click` (läst lat EN gång,
  hålls levande av SettingChanged) och annars 400 — GTK:s och KDE:s förval.
  `glaze::AppWindow` räknar dubbelklicken mot den), `clipboardText()`/
  `setClipboardText(utf8)` (klippbordet, alltid UTF-8 och RADSLUTEN LF åt
  båda hållen sedan post 23 — backenden översätter till OS:ets form:
  Windows skriver CRLF och gör CRLF till LF, macOS/Linux talar LF; ett
  ensamt CR lämnas åt mottagaren), **`openUrl(utf8)`** och **`openFile(path)`** (REQ8 11 — en adress resp. en lokal sökväg i
  systemets standardhanterare — en OS-HANDLING, därför fasadens jobb
  (glaze::Link); `ShellExecuteW`/`NSWorkspace openURL:`/fork+exec
  `xdg-open`, en delad intern hjälpare per backend; true = begäran skickad),
  **`userScale()`/`setUserScale(float)`** (L12/US — användarens
  multiplikator OVANPÅ OS-skalan: `uiScale() = osScale × userScale`,
  default 1.0, app-global som temat, klampas 0.5–2.0 + logg (NaN/≤ 0
  ignoreras); värdet BOR här (backend-neutralt, `src/platform.cpp`) och
  LÄSES av backendernas `uiScale()` exakt en gång; sättaren lagrar bara —
  omräkningsrundan över öppna fönster ägs av `glaze::App::setUserScale` via `Window::userScaleChanged`),
  **`setGpuPreference`/`gpuPreference`, `gpuAdapters()` (`GpuAdapter`,
  `GpuKind`), `setGpuAdapter(id)`/`gpuAdapter()` och `childEnvironmentRemovals()`**
  (CANVAS_3D beslut 1, REQ9_GRAFIKKORTET — lagrade i `src/platform.cpp`, listan
  och regeln i `src/gpu_util.h`; id:t `pci:vvvv:dddd:ssssssss:rr`, kortet vinner
  över preferensen, ett id som saknas eller inte är `selectable` loggas och
  preferensen väljer bland de valbara. Först i `main`, före trådar — kravet
  står i API-kommentaren. D3D11 och Metal vid varje ny renderare, Linux BARA i
  `gfx::init` via miljövariabler (NVIDIA-offload eller `DRI_PRIME=pci-…`,
  användarens vinner, barnen ärver dem inte). Läses i `Renderer::adapterId`),
  **`setDisplayCalibration(id, pxPerMm)`/`effectiveDisplay(m)`** (CANVAS_2D
  etapp 2 — appens kalibrering per skärm-id, app-global, appen äger
  lagringen; 0 tar bort, tomt id och värden utanför 50–800 dpi loggas och
  ignoreras. `effectiveDisplay` gör en mätning Calibrated när dess id har en
  kalibrering. Bor backend-neutralt i `src/display_util.cpp`; i en app går
  vägen genom `glaze::App::setDisplayCalibration`), **`screens()`** (`ScreenInfo
  {bounds, work, scale, primary}` i placeringsrymden; tom på Wayland och före init), `systemTheme()`
  (OS:ets ljus/mörkt, läses färskt per anrop — okänt → Light),
  **`systemLocale()`** (OS:ets SPRÅKVAL som BCP-47-tagg, TOM utan preferens;
  fasaden NORMALISERAR: `sv_SE.UTF-8` → `sv-SE`, `nb_NO@variant` → `nb-NO`,
  `C`/`POSIX` → tom. Linux `LC_ALL` → `LC_MESSAGES` → `LANG`, Windows
  `GetUserDefaultLocaleName`, macOS `NSLocale.preferredLanguages` — INTE
  `currentLocale`, som bär REGIONEN: "en_SE" för en svensk med engelskt system),
  **`setQuitMenuPrefix(prefix)`** (post 32 — macOS appmenys "Avsluta
  <app>": fasaden bär ingen egen text, så glaze trycker in det ÖVERSATTA
  prefixet efter `init()` och vid varje språkbyte, och backenden lägger
  processnamnet efter. No-op på Windows och Linux — ingen appmeny där),
  **`setAppId(id)`/`appId()`** (PROGRAMIKON — appens identitet för
  skrivbordet: Wayland `xdg_toplevel_set_app_id`, X11 `WM_CLASS`; Windows
  och macOS läser den aldrig. Före första fönstret; tom = PROGRAMFILENS
  NAMN normaliserat till `[A-Za-z0-9._-]` (varje annan byte blir '_',
  "glazeui" om namnet inte går att läsa), ogiltigt id loggas och ignoreras.
  Lagringen i `src/platform.cpp`, programnamnet per backend
  (`programName()`, deklarerad i `src/icon_util.h`)),
  **`setShowInDock(show)`/`showInDock()`** (SYSTEMFÄLTET B9 — DOCKAN,
  app-global och med flit skild från fönstrens knappar (`showInTaskbar`).
  macOS Regular/Accessory — före det första fönstret läser `init()` värdet,
  så dockikonen aldrig blinkar till, efter init byts policyn direkt; Windows
  och Linux lagrar och loggar false en gång. Gettern svarar med BEGÄRAN,
  förval true. Begäran lagras i `src/platform.cpp`; backenden gör sin del i `applyShowInDock` (`src/icon_util.h`)),
  **`knownPlaces()`** (post 18, 2026-10-08 — filväljarens vänsterkolumn:
  `std::vector<Place>` med `PlaceKind` Home/Desktop/Documents/Downloads/
  Pictures/Volume, UTF-8-sökvägen och för volymer en `label`. Det ENDA
  OS-specifika i filväljaren — katalogläsningen är ren `std::filesystem` i
  widget-lagret. KONTRAKTET står i headern: fast ordning, varje användarmapp
  FINNS och är inte hemmet självt, volymerna är de OS:et rapporterar redo, en
  Windows-NÄTVERKSENHET listas UTAN att kontaktas, och användarmapparna saknar
  etikett med flit — "Dokument" följer appens språk), **`isHiddenFile(utf8)`**
  (post 18 — döljer OS:ets egen filhanterare posten, UTÖVER punktnamnen?
  Windows `FILE_ATTRIBUTE_HIDDEN`, macOS `UF_HIDDEN`, Linux alltid false —
  läsningen är std, fasaden svarar bara på det std inte kan),
  `createWindow()` (fabriken).
- **`timer.h`** (FONSTER_OCH_TIMER) — APPENS timer: `startAppTimer(id, ms)`/
  `stopAppTimer(id)` (`AppTimerId` 64 bitar, glaze tar ett nytt per `start()`),
  levererad som `EventType::Timer` med windowId 0; upprepande, ms < 1 blir 1,
  false före init/efter shutdown/för id 0. App-globalt som `tray.h`: TRE
  implementationer (D3D11 `SetTimer`, Metal `NSTimer`, Linux timerfd).
- **`tray.h`** (SYSTEMFÄLTET, 2026-10-12) — appens ikoner i OS:ets
  systemfält: Windows meddelandefält, macOS menyradens statusposter;
  konsumeras av `glaze::TrayIcon`. APP-GLOBALT, alltså FRIA funktioner med
  ett id och inga rena virtuella på `Window`: ikonen hör till appen, och det
  ger TRE implementationer (en per backend) i stället för sex, utan
  proxyfälla. **`TrayId`** väljs av ANROPAREN (timer-id-mönstret), 1–
  **`kTrayIdMax`** (0xFFFF — Windows v4 bär id:t i 16 bitar; 0 är aldrig ett
  id). **`TrayIconDesc {image, toolTip, templateImage}`** är HELA innehållet:
  `WindowIcon`s ramar (rak alfa, `programFirst` = programfilens ikon vinner
  där den finns), tooltipen i UTF-8 (Windows kapar vid 127 UTF-16-enheter,
  på en teckengräns) och macOS mallbild. **`trayAvailable()`** — Windows och
  macOS true, Linux false, samma svar före som efter init. **`setTrayIcon(id,
  d)`** lägger till ELLER uppdaterar: true = registrerat, också när skalet
  inte kan ta emot ikonen än (Windows lägger till den vid `TaskbarCreated`);
  false = VÄGRAT och inte registrerat — fel id, ingen bild, inget fält —
  loggat utom på Linux. **`removeTrayIcon(id)`** (idempotent, stoppar
  timrarna), **`trayIconRect(id, icon, work)`** (ikonens rect och den
  skärmens ARBETSYTA i PLACERINGSRYMDEN — `setPosition`s; false = okänt och
  utvärdena orörda) och **`setTrayTimer(id, ms)`** (animationens tidskälla:
  `TrayAction::Tick` var ms:e millisekund, 0 = av, ett nytt anrop startar om
  perioden, ett oregistrerat id är en no-op). Ett event kan ligga kvar i kön
  efter `removeTrayIcon` — mottagaren slår upp id:t. UI-trådens och SÄKER
  före `init()` och efter `shutdown()`: `trayAvailable` svarar, resten
  svarar false och gör ingenting, så en `glaze::TrayIcon` som lever längre
  än appen inte kan krascha den. Det som RITAS är inte fasadens: menyn är
  glaze:s egen popup och tooltipen OS:ets.
- **`print.h`** (REQ8 10) — UTSKRIFTEN, app-global som `tray.h` (TRE implementationer): `PrintSettings` (papper i mm STÅENDE, orientering, kopior, intervall; ut skrivare, dpi, `printableMm` i den orienterade sidan, opakt `native`), `printAvailable`/`showPrintDialog` (SYSTEMETS dialog)/`openPrinter` (tomt namn = standard)
  och `PrintDocument` (`addPage(rgba)` över det utskrivbara området, `finish`/`abort`). Windows PrintDlgEx + GDI, macOS NSPrintOperation (BLINT), Linux false.

## Fabriks-/tvåfasmönstret

`gfx::createWindow()` ger ett fönsterobjekt UTAN OS-fönster; `init(desc)`
(i `AppWindow::show()`) skapar det. Därför kan fönsterklassens ctor
(genererad `initializeComponent`) köra före `show()` (`setSize`/`setTitle`
no-op:ar) och `AppWindow::window()` är alltid en giltig referens. Bara EN
backend kompileras in — valet sker i byggtid, ingen runtime-dispatch.

## Konventioner i dessa headers

- **`RenderFn`-callbacket ritar bara frame-innehåll** (clear + appens grejer),
  ALDRIG chrome eller present — det gör `renderFrame()`.
- **Logiska mått** exponeras rå; multiplicera med `uiScale()` vid användning.
- Håll headern beroendefri mot `glaze/` — beroendet går bara uppåt (widgets →
  gfx), aldrig tillbaka.
- Interface-tillägg = ren virtuell metod här + implementation i VARJE backend
  under [../../src/](../../src/) (backend_d3d11, backend_metal OCH
  backend_linux — alla tre ska kompilera). En ny ren virtuell i `Renderer`,
  `Font`, `Image` eller `GpuBuffer` får också sin no-op i `null_renderer.h`
  i SAMMA ändring — grindarnas test-renderare ärver därifrån.
