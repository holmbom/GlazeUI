// scene3d_pixel_gate — DJUPET, MSAA och
// RENDERLÄGET mätta i PIXLAR genom den RIKTIGA backenden (del A och B), och två
// fasadkontrakt: PNG-kontraktet och GPU-preferensen
// (del K). EGNA PROGRAM och instansiering (del C, längre ned).
// LINE3, GLYPH3 och RITYTANS 3D-MÅL (del D och G, längst ned).
// DEL-VYERNA, 3D TILL BILD och MÅLAREN MED Z (del I).
// EGNA PROGRAM OCH HELSKÄRMSPASS PÅ RITYTAN (del C2).
// TERRÄNG, DRAPERING OCH TEXT I MARKEN (del H).
// MARKDJUPET — när planet z = 0 ritas, och att det som står på marken aldrig
// skärs av det (del J; förväntat värde, vad den inte mäter och sabotagen står vid
// delen). MÅLARENS KLIPP I 3D-MÅLET (del KL) — likaså.
//
// VÄGEN är scene_pixel_gates: ett DOLT fönster (`setOffscreen`) och en
// testwidget som i sin ritning binder render-mål och anropar `drawMesh` direkt
// mot renderaren. Målen läses med `readTarget` efter framen (del B3 prövar den
// vägen mot fönstrets egen, `captureFrame`). Före Line3 bär ingen
// vertex ett z: djupet styrs PER RIT-ANROP med matrisen — `pxMatris` lägger
// världens x, y på målets px och sätter z till ett tal i [0, 1] (w = 1).
//
// VAD GRINDEN PÅSTÅR:
//   A1  LESS: den närmare vinner OAVSETT ritordning, i alla fyra programmen
//       (Fill, Image, Line, Glyph) — och med Off vinner den sist ritade
//       (kontrollen som visar att fallet kan falla).
//   A2  GREATER: den bortre syns, den närmare inte.
//   A3  LESSEQUAL vid samma djup: den andra vinner; med Less den första.
//   A4  UTAN SKRIVNING: en senare ruta jämförs mot det GAMLA djupet (och den
//       oskrivna rutan själv ritades).
//   A5  BIAS −1 på en koplanar ruta vinner under Less; 0 och +1 gör det inte.
//   A6  GALLRINGEN: framsidan är moturs SETT FRÅN KAMERAN. Förväntat värde är grindens
//       egen vindning av de projicerade trianglarna: Back fäller samma data
//       under den raka matrisen som den släpper igenom under en SPEGLAD,
//       Front tvärtom, None ingen.
//   A7  BLANDNINGEN mot ett opakt blått mål — förväntat värde är blandningens formel,
//       räknad här: Alpha, Premultiplied, Additive och Opaque; DepthOnly
//       lämnar färgen orörd men skriver djupet (en senare ruta bakom döljs —
//       utan förpasset syns den).
//   A8  UI-ÅTERSTÄLLNINGEN: en UI-fillRect efter ett scenanrop med Greater,
//       Cull::Back, DepthOnly respektive Additive syns i sin egen färg — i ett
//       mål med djup, där D3D11:s förvalda djupläge är PÅ.
//   A9  ÅTERANVÄNT MÅL: pushTarget rensar djupet till 1 — i
//       andra framen syns en bortre ruta där den första ritade en närmare,
//       också när det senaste scenanropet (i ett annat mål) lämnade färg- och
//       djupmasken av, som GL:s glClear lyder.
//   B1  MSAA: en Image-quad vriden 30° har kantpixlar STRIKT mellan 0 och 255
//       med 4×, och bara 0/255 med 1×.
//   B2  supportedSamples: 8 ger 8, 4 eller 2 (svaret skrivs ut — llvmpipe har
//       4), 0 och 1 ger 1, 3 ger 2:s svar, svaren växer; ett mål som begär 8 får
//       supportedSamples(8); provtagningen 3 och måttet 0 ger ogiltiga mål.
//   B3  readTarget mot ett känt mönster: RÄTTVÄNT (fyra fält i ett 64 × 48-mål),
//       RAK alfa (en halvgenomskinlig remsa), också för ett MSAA-mål med djup —
//       och samma färger som fönstrets väg (drawImage + captureFrame).
//   B4  UTANFÖR EN FRAME (begin/endOffscreen) ger samma pixlar (±1) som i en
//       frame, också i ett NYTT fönster som ALDRIG ritat (ett krav — på D3D11
//       med användarskalan satt så att fönstrets skala blir 1, annars ritar
//       init en frame); ett block inuti en frame vägras, och ett bundet mål
//       läses inte.
//   B5  UPPLÖSNINGEN: MSAA-målets bild stämmer direkt efter popTarget, i
//       drawImage och i Image-programmet; och ett mål som lämnas för ett
//       NÄSTLAT mål är upplöst där (bilden ritas i det nästlade) och behåller
//       färg OCH djup när man kommer tillbaka.
//   P1–P3 PNG-KONTRAKTET: en riktig JPEG (FF D8 FF …) avkodas inte —
//       decodeImage, loadImage ur minnet och ur fil — medan en PNG ur
//       encodePng gör det, pixel för pixel; en avhuggen PNG ger false.
//   K1  GPU-PREFERENSEN: enheten skrivs ut för Default, HighPerformance och
//       LowPower (ett nytt dolt fönster per preferens — enheten väljs när
//       fönstret skapar sin renderare); på Windows jämförs den med DXGI:s egen
//       ordning för samma preferens.
//   K2  GRAFIKKORTEN: `gpuAdapters()` är inte tom, har
//       exakt ett isDefault och unika, icke-tomma id; Default-fönstret ritar
//       med förvalskortet (`adapterId`); på Windows lika många kort som
//       EnumAdapters1.
//   K3  varje VALBART kort: `setGpuAdapter(id)` → ett nytt dolt fönster ritar
//       med det (`adapterId`, `deviceName`). På Linux gäller valet bara vid
//       init, så där mäts bara att `adapterId` står i listan.
//   K4  ett okänt id: fönstret öppnas ändå, på preferensens kort.
//   Ett TOMT `adapterId` ger en SKIP-rad bara när MJUKVARAN är konstaterad ur
//   enhetens namn (llvmpipe, WARP …) — på identifierbar hårdvara FÄLLER det
//   K2–K4; ett rent mjukvarufall utan kort hoppar över K2:s listkrav. Att
//   Default ritar med `isDefault` krävs bara på D3D11 (DXGI:s index 0 —
//   fastställt); Linux och macOS uppskattar förvalet, så där mäts bara att
//   `adapterId` står i listan. En EGL utan EGL_EXT_device_query på riktig
//   hårdvara fäller K2 på Linux (grinden kan inte fråga EGL själv).
//   Sabotagen: id:t ignorerat i `chosenAdapter` (K3), förvalet på fel index
//   (K2), ett okänt id som får `init` att misslyckas (K4). INTE mätt: att
//   slaget stämmer på riktig hårdvara (`terrain_perf --gpus` för hand), och
//   Metal och Linux-valet (doc/os/*/TESTS.md).
//
// INTE MÄTT här: Metal (raderna i doc/os/macos/TESTS.md); slopeBias
// (bara den konstanta biasen, A5); GreaterEqual, Equal och Always var för sig
// (samma tabellväg som de mätta testen); djupklippningen mot när- och
// fjärrplanet; MSAA:s kvalitet utöver att kanterna får mellantoner, och 8×
// (llvmpipe saknar det — B2 skriver vad som finns); att ett MSAA-mål inte löses
// upp OFTARE än när det lämnas; B4:s nya fönster på en skärm över 200 %
// (användarskalans golv 0,5 når inte skalan 1 — fallet faller då i stället för
// att mätas); Linux-loggen om GPU-preferensen (den syns i stderr); och att
// HighPerformance VERKLIGEN är snabbare (K1 jämför med DXGI:s ordning, inte
// med en mätning).
//
// SABOTAGEN — alla fäller grinden (D3D11 och GL): renderläget
// ignorerat i alla program (17 fel — A1–A7, B4, B5) och bara i
// Glyph-programmet (A1 Glyph, båda backendarna), UI:ts läge utan djupet (A8;
// i GL också A3 och A9), bindUiPipeline utan renderläge (A8, alla fyra),
// upplösningen överhoppad (A9, B1, B3, B4, B5 — båda), provtagningen
// ignorerad (B1, B2 — båda), biasens tecken vänt (A5 — båda), framsidan
// medurs (A6 — båda), clear som inte rensar djupet (A9; i GL, vars nya djup
// inte är nollställt, 30 fel), GL:s vändning i readTarget borttagen (B3, B4),
// PNG-provet bort — WIC avkodar då JPEG:en (P1), GPU-preferensen ignorerad
// (K1 HighPerformance), Additive som Alpha och DepthOnly som skriver färg
// (A7), readTarget utan av-premultiplicering (B3 rak alfa) och targetDescOk
// som godtar 3 prov (B2).
// Även: beginOffscreen som ren no-op på
// D3D11 (B4 — det nya fönstret hade ritat en frame i init) och GL:s clear
// utan UI-läget, som då lyder masken ett scenanrop lämnat (A9 — varje
// rensning kom efter framens UI-läge). GRÖNA PER KONSTRUKTION, inte luckor:
// djuptestet utan krav på ett djupmål på D3D11 (utan DSV testar D3D11 inget
// djup; regeln finns för Metal) och GL:s beginOffscreen utan makeCurrent
// (bindTarget gör det själv, och blocket ritar bara i ett mål).
//
// DEL C — EGNA PROGRAM. Grinden skriver varje program själv i HLSL,
// GLSL och MSL; backenden kompilerar sitt (MSL på macOS). Ritningen sker i
// render-mål med pixelklippet rakt av (identitetsmatrisen i GlazeFrame), och
// förväntat värde är grindens egna tal:
//   C1  ett eget format — läge float3 + färg unorm8×4 på plats 2 — ritar rätt
//       färg där rutan ligger och inget utanför; reflektionen ser platserna 0
//       och 2 som flyttal, och programsBuilt stegas.
//   C1b HELTAL: UInt8x4 och UInt (ett id över 16 bitar) läses
//       som heltal — GL:s IPointer, D3D11:s _UINT — och reflektionen ser dem.
//   C2  GlazeUser: blockets ANDRA float4 når fragmentet, och nästa anrop i
//       samma frame får sitt eget block.
//   C3  textur på plats 2: övre högra texeln i en 2 × 2-bild (rad 0 överst),
//       med helt andra färger på platserna 0 och 1. C3b: en UI-bild efter
//       programmet ritas med sin egen textur (enhet 0 / t0 tillbaka).
//   C4  tre instanser på kända lägen och färger i ETT anrop (instances 3, 18
//       vertex); C4b delen börjar vid instans 1 — instans 0 ritas inte.
//   C5  icke-indexerat ur en del som börjar efter tre skräpvertex; C5b
//       icke-indexerat OCH instansierat.
//   C6  helskärmstriangeln utan buffert (tom layout) fyller hela målet.
//   C7  syntaxfel på appens rad 7: ogiltigt, felet anger rad 7 i kompilatorns
//       form (HLSL "(7,", GLSL ":7(", MSL ":7:") och språket — också med
//       definitioner —, programsBuilt stegas inte, och ett anrop med det
//       lämnar målet orört.
//   C8  tom källa för backenden: "saknar HLSL-/GLSL-/MSL-källa" med namnet (GL
//       också för ett tomt vertexsteg); bara backendens egen källa räcker.
//   C9  GlazeFrame: glaze.forward i mittpixeln = vyns blick, normerad av
//       grinden ur en View3D den själv byggde; glaze.target = det BUNDNA målets
//       mått (48, 32, 1/48).
//   C10 definitionerna väljer grenen OCH ger värdet; ett ogiltigt namn ger ett
//       läsbart fel.
//   C11 renderläget på ett eget program: Less med z ur vertexens float3 i båda
//       ordningarna, Off som kontroll, gallringen mot A6:s vindning, och Less
//       i ett MSAA-mål med djup.
//   C12 DE DELADE REGLERNA: nio anrop som inte stämmer med
//       programmet (ingen layout, en plats som saknas, heltal mot flyttal,
//       dubbla platser, för liten instansdel, instanceCount 0, fel GlazeUser,
//       för få texturer, för stort vertexCount) ritas inte — ritade
//       OGENOMSKINLIGT, så att också en omtolkad genomskinlig färg hade synts —
//       och userBytes 20, fem texturer, GlazeUser-storleken mot källan, en
//       texturplats utanför beskrivningen och en lös global/uniform utanför
//       GlazeUser ger läsbara fel (de tre sista inte på Metal).
// INTE MÄTT i del C: Metal; SNorm8x4 (GL 3.3:s och D3D11:s normering skiljer
// sig vid −128); en layout med FLER attribut än programmet läser; flera
// texturer med OLIKA filter; GlazeUser i vertexsteget; ett programs livstid
// (ett program rivet medan det är bundet); och prestandan (en input layout och
// ett block per anrop).
// SABOTAGEN för del C — alla fäller grinden.
// D3D11:
// GlazeUser i b5 (C2), instansströmmen per vertex — divisorns motsvarighet
// (C4, C4b, C5b), instansdelens offset 0 (C4b), `#line 1` borta (C7), forward
// och right omkastade i förspelet (C9), reflektionen utan heltal (C1b),
// layoutFits avstängd (C12 heltal mot flyttal — först efter att C12 ritar
// ogenomskinligt; den saknade platsen fäller CreateInputLayout ändå), ett
// helskärmspass utan input layout som inte ritas (C6, C9, C10), target utan
// bredd (C9), programsBuilt och instances ostegade (C1, C4), GlazeUser-
// storleken och texturplatserna oprövade mot källan (C12), strömmarnas
// storlek oprövad (C12) och $Globals godtagen (C12). GL (Wayland
// och X11): GlazeUser
// på bindningspunkt 5 (C2), divisorn 0 (C4, C4b, C5b), instansdelens
// pekar-offset borta (C4b), `#line 1` borta (C7 — rad 29), forward och right
// omkastade (C9), UInt8x4 med glVertexAttribPointer (C1b), enhet 0 inte aktiv
// igen (C3b — UI-bilden blev magenta), layoutFits avstängd (C12, båda
// fallen), GlazeUser-storleken och texturplatsen oprövade (C12),
// reflektionen utan heltal (C1b) och lösa uniformer godtagna (C12).
//
// DEL D — LINE3 OCH GLYPH3 genom fasaden, mot HÅLKAMERANS förväntat värde:
// grinden bygger vyn själv (ögat i origo, blicken +y, upp +z, brännvidden
// 128 px i ett 128 × 128-mål), så punkten (x, y, z) hamnar i
// (64 + 128·x/y, 64 − 128·z/y) och en världsenhet tvärs blicken är 128/y px.
//   D1  Line3 i px är lika bred nära och fjärran under perspektiv: en
//       golvlinje rakt fram, 6 px ±1 på djupet 1,1 och 8.
//   D2  en ände BAKOM kameran trimmas mot närplanet: delen framför ritas mot
//       nedre högra hörnet med sin bredd (6·√2 px i en rad) ända till målets
//       kant, och ingenting hamnar mot övre vänstra (där den speglade punkten
//       ligger).
//   D3  fyrkantiga ändar förlängs en halv bredd I SKÄRMEN: tvärs blicken (32
//       → 40 px) och längs golvet bort från kameran (24 → 36 px — 6 px i båda
//       ändarna fast en världsenhet är 16 px i den ena och 1 px i den andra).
//   D4  streck i världsenheter på en LODRÄT linje (längden i xy är noll):
//       tre streck om 8 px från linjens start — 3D-längden.
//   D5  två korsande Line3: den närmare vinner med Less i båda ordningarna;
//       utan djup den sist ritade (kontrollen), och båda ritas utanför
//       korsningen.
//   D6  Glyph3 på höjden z: två markörer i SAMMA chunk — den andra med
//       chunk-lokalt z 1 — hamnar där `gfx::project` (lika med hålkameran)
//       säger, och en markör bakom kameran syns inte. På RITYTAN: en
//       kolliderande text3 långt bortom 3D-kamerans mål syns — placerad med
//       3D-vyn och inte med ritytans 2D-skugga (kartvyn uppifrån, där den
//       ligger utanför ytan och hade dolts).
//   D7  världsbredd i POS3 = bredden × pxPerUnitAt vid punkten (0,1 → 6,4 px
//       på djupet 2 och 1,6 px på djupet 8, ±0,5).
// DEL G — RITYTANS 3D-MÅL genom en RIKTIG glaze::Canvas (ett fönster per fall;
// 3D-målets räknare `targetRenders()` ur den interna canvas/resources.h):
//   G1  bara 2D-innehåll i 3D-målet med setMultisample(1) = 2D-vägens pixlar
//       inom kantutjämningens avrundning, uppifrån och lutad (toleransen och
//       dess skäl står vid fallet).
//   G2  kompositen: hörnpixlarna utanför det rundade klippet visar fönstrets
//       yta (målets röda syns inte där), och rött över vitt (MSAA 4) har
//       mellantoner men röd kanal ≥ 245 — ingen mörk frans.
//   G3  cachen: en halvgenomskinlig barnwidget ovanpå ritytan som invaliderar
//       sig själv ger targetRenders +0 — ritytan ritas (målet syns under
//       lappen), inga programbyten, ingen uppladdning; canvas.invalidate()
//       ger +1, och App::invalidateAll (genom setTheme — den är privat) +1.
//   G4  `onPaint` läser en variabel som en knapphandler ändrar: klicket
//       (handleEvent ner + upp) ger fönstrets fulla invalidering, målet ritas
//       om och den nya färgen syns.
//   G5  inaktiverad: tonen (1 − iconFade) ligger i kompositen.
//   G6  Ground-lagrets etikett (blå platta) ritas EFTER World-lagrets breda
//       Line3 rakt genom den.
// D2 och G1 fäller GL-koden utan linjeprogrammens trimning mot
// klippytan: llvmpipe interpolerar de skärmlinjära värdena fel i ett band
// som GL klipper (en kil mot närplanet; en rutnätslinje borta bortom 3D-målets
// ändliga bortre plan). Se backend_linux/CLAUDE.md.
// INTE MÄTT i del D och G: Metal; Line3:s skarvar och runda ändar
// under perspektiv (skärmlägets kod — 2D-grindarna mäter den); Glyph3-TEXT
// genom fasaden (bara markörer; texten går genom ritytan i D6); streck i px
// och mm på en Line3 (skalan i mitten — skriven begränsning); MSAA:s kvalitet
// i målet; cachen när rutan, skalan eller mm-tätheten ändras (target3d.cpp
// jämför dem, inget fall ändrar dem); etikettoningen genom målet; att
// 2D-vägen släpper ett mål (dropTargets); markdjupets plan (en 2D-form i ett
// World-lager mot marken); Terrain-placeringen (del H);
// en uppladdning i `onPaint` mitt i målet; och skyddsramens trimning för sig
// (den är grön på D3D11 också utan den — bara GL:s klippning prövar den).
// SABOTAGEN för del D och G (D3D11 och GL):
// cachen som ignorerar fönstrets fulla invalideringar (G3
// invalidateAll, G4), cachen som ignorerar ritytans egna (G3), cachen som
// aldrig återanvänds (G3 — också barnwidgetens frame ritade om målet),
// etiketterna placerade med 2D-projektionen (D6 ritytan — 0 röda px),
// Ground-etiketterna före World (G6), GLAZE_POS3 borta ur Line-källan (D3, D4,
// D5, D7; på GL också D1) och ur Glyph-källan (D6 — den höga markören landade
// på den låga), kompositen utan av-premultiplicering (G1, G2 — lägsta röda
// 191; och A:s B3), världsbredden extruderad i planet (D7) och den linjära
// ändförlängningen (D3 — 34,78 mot 36; på GL försvann dessutom D1:s linje,
// ett llvmpipe-hörnfall vid exakt t = 4: ×1,0001 ritar den). painter_gate
// L3: Line3-chunkarna uppladdade varje frame (7424 byte på fyra
// kameraändringar). BARA PÅ GL (D3D11 klipper rätt och är grönt): förlängningen
// utan keepInside (D2) och trimningen bara mot närplanet (G1).
//
// DEL E OCH F — FÖREMÅLEN GENOM RITYTAN, en RIKTIG glaze::Canvas
// per fall (Rit3). Kamerorna är grindens egna ord för konventionen: en
// ortografisk vy rakt uppifrån (världen (x, y) i (mitten + x·k, mitten −
// y·k)) och en hålkamera från söder; ljusets förväntade värde är modellens definition
// räknad här.
//   E1  en låda (primitivens enhetsmesh, skalad) med översidan mot ljuset =
//       färg × (ambient + diffuse); N·L = 0,6 med exponenten 2; rimljuset.
//   E2  baksidan: tvåsidigt belyst som framsidan, ensidigt bara ambient.
//   E3  den platta normalen (`Mesh::triangles`, inga normaler): ljuset
//       ovanifrån ger ambient + diffuse, underifrån bara ambient.
//   E4  texturen (en 2 × 2-Bitmap, närmaste texel) rättvänd på en kvadrat med
//       uv.
//   E5  rampens tre stopp vid min, mitten och max — och klampad utanför.
//   E6  mättnaden 0 ger luminansens grå. FÄRGJUSTERINGEN genom
//       `MeshStyle`: kontrasten 0 mittgrått, ljusheten 0,5 halva färgen,
//       mättnaden 1,5 mättare, ordningen ljushet → kontrast → mättnad (två
//       kombinationer som klampar, så en annan ordning ger en annan färg) och
//       texturen — en 2 × 2-bild med mättnaden 0 ger texlarnas luminans.
//       Sabotage som SKA fälla dem: `contrast`
//       eller `brightness` inte fyllda i `meshUniforms`, mättnaden klampad
//       till 0–1, kontrasten före ljusheten i
//       `glaze_colorAdjust`, mättnaden före kontrasten, och justeringen före
//       texturen i Mesh-programmet.
//       E7 dimman bortom fogEnd ger
//       dimfärgen, och `MeshStyle::fog = false` ingen (kontrollen).
//   E8  RTC: samma låda och sfär vid origo och vid SWEREF — samma bild ±2.
//   E9  instanserna: läget, färgen (alfa 0 = stilens), giren
//       (medurs ovanifrån) med olikformig skala, och skalan.
//   E10 ändringsloggen: `setColors` och `setPositions` på en
//       synlig mesh syns i nästa ritning.
//   F1  en kvadratisk punkt om 6 px täcker (6 · skalan)², och
//       en om 1,5 mm (1,5 · px/mm)²; F2 en rund π · 3² (× skalan²),
//       ±5 %.
//   F3  rampens min, mitt och max; F4 djupet mot en mesh (punkten ovanför
//       syns, den under inte, oavsett ordning); F5 färgen per punkt.
//   F6  en sfär med radien mm(2) är 4 mm (4 × fönstrets px/mm) bred nära och
//       fjärran, ±1,5 px — fönstrets egen täthet i stället för en
//       injicerad 4 px/mm (ett dolt fönster har inte alltid en skärm att
//       kalibrera).
// INTE MÄTT i del E och F: Metal; rampen med NÄRMASTE texel — ritytans
// rampbild har 256 texlar, så felet är < ett steg och syns inte här
// (mesh_program_gate E5/F3 mäter det med en ramp om tre texlar); dimman i
// perspektiv mot ett avstånd; skärmstorleken för instanser; texturen ur ett
// render-mål genom ritytan; polygon3:s gradient; och residensen
// (canvas_perf3d G10).
// SABOTAGEN för del E och F (D3D11, alla fäller grinden): anropets origo i float
// (E8 — 217 steg, 1806 px), punktens enhet ignorerad (F1 mm — 4,5 mot 124
// px²), skärmstorlekens enhet ignorerad (F6 — 6 px mot 30), skärmstorleken
// utan djup (F6 — 0 px) och på ett fast djup (F6 — 61 och 15 px), tvåsidigt
// ignorerat i översättningen (E2) och dimman oöversatt (E7).
//
// DEL I — DEL-VYERNA, 3D TILL BILD OCH MÅLAREN MED Z genom en
// RIKTIG glaze::Canvas. Förväntat värde är grindens eget: vyns ruta ur style3d.h:s ord,
// den ortografiska vyns projektion med grindens egen trigonometri ur
// camera.h:s konvention, ritytans egen skärmbild för bilden och lagrens väg
// (mätt mot analytiska värden i del E/F) för målarens föremål.
//   I1  en blå stav i vyn uppe till höger skyms INTE av huvudvyns röda mark,
//       som står mycket närmare i sitt djup (≈ 0,1 mot vyns ≈ 0,5) — vyn har
//       eget djup; kontrollen: i huvudvyn skyms en stav under marken. Ett
//       lager utanför vyn ritas inte i den, och `removeView` tar vyn och
//       lagrets plats i den.
//   I1b vyns klipplan ur sina EGNA lager: med huvudvyn vid
//       SWEREF vinner den närmare av två staver 0,0005 enheter isär i vyn.
//   I2  `followRotation`: markören i vyn står där huvudkamerans azimut och
//       elevation lägger den (±2 px), också efter en ny huvudkamera och med
//       KARTKAMERAN (bäringen som azimut, 90 − tilten som elevation);
//       kontrollen utan följning; lagret i vyn ritas inte i huvudvyn.
//   I3  genomskinlig bakgrund visar huvudvyn; en ogenomskinlig fyller rutan.
//   I4  `renderToBitmap` med ritytans kamera i dess storlek = skärmens region
//       ±1 per kanal (de rundade hörnen undantagna), också med fönstret
//       dolt; inuti en ritning ingen bild.
//   I5  en bild med en annan kamera placerar sina etiketter
//       för sig — nästa skärmritning placerar inte om och ritar samma bild.
//   I6  målarens Line3 under marken: förvalet ovanpå, Less döljer, Greater
//       visar, `save()` återställer; en HUD i `screenSpace()` ritas alltid.
//   I7  vyernas kollektionsvokabulär: numren, avslagen,
//       lagrens vylistor vid `removeView` och `setViews`.
//   I8  huvudvyn och vyn placerar i var sin etikettplats.
//   I9  målarens föremål — `draw` med låda, instanser (också ett fält om 200
//       vars celler får olika detaljnivå), punkter och en yta i rymden med
//       kontur, `drawTriangles3`, `drawPoints3`, `translate`, `rotate` och
//       eget renderläge — = samma föremål i ett World-lager (±1; mätt 0 på
//       D3D11 och llvmpipe).
// INTE MÄTT i del I: Metal; vyns MSAA för sig (samma provtagning som
// huvudmålet, B1 mäter den); etikettkollisionen INOM en vy i pixlar (I8 mäter
// bara att platserna är skilda); ett ankare annat än TopRight (S11 fäller
// "alltid TopLeft", de övriga sju lägena är samma räkning); en vy som sticker
// ut ur ritytan; `renderToBitmap` med rutnät och en Bitmap som väntar på
// budgeten; målarens föremål med textur, ramp och skärmstorlek (lagrens del
// E/F mäter dem, målaren delar programmen och blocken) och i 2D-vägen; och
// träffprovets routning till vyerna.
// SABOTAGEN för del I (D3D11 och GL, alla 20 fäller grinden): vyn ritad i
// huvudmålet utan djuprensning (I1,
// I3), `followRotation` ignorerad (I2 — tre fall), bilden i skärmens
// etikettplats (I5), klipplanen ur hela scenens låda (I1b), vyns bakgrund
// ignorerad (I3), `layerDrawn` utan vyn (I1, I2, I3), `removeView` utan
// lagrens omnumrering (I1, I7), bilden rensad genomskinlig (I4), vyns ankare
// ignorerat (I1–I3), vyerna i huvudvyns etikettplats (I8 — +4
// placeringar), vyerna kompositerade under huvudmålet (I1, I3),
// `endOffscreen` borta (I4, I5), målarens renderläge ignorerat (I6),
// målarens transform ignorerad för föremålen (I9 — 5510 px), placeringens
// vridning och skala borta i målarens mesh (I9), punkternas chunkorigo borta
// (I9), föremålens renderläge ignorerat i målaren (I9), målarens instanser
// utan cellens detaljnivå (I9 — 1533 px) och instansföljden utan sin
// förskjutning i raderna (I9 — 685 px). GRÖNT PER
// KONSTRUKTION i bilden, inte en lucka: `renderToBitmap` utan hela
// offscreen-blocket fälls bara av I4:s "inuti en ritning" — D3D11:s
// omedelbara kontext och GL:s `bindTarget` (som gör kontexten aktuell) ritar
// och läser rätt ändå; blocket behövs för Metals egna command buffer.
//
// DEL C2 — EGNA PROGRAM OCH HELSKÄRMSPASS PÅ RITYTAN genom en
// RIKTIG glaze::Canvas. Grinden skriver programmen i HLSL, GLSL och MSL (MSL
// på macOS); förväntat värde är grindens egna tal och dess egen trigonometri:
//   C2a ett eget mesh-program: R ur meshens user0, G ur GlazeUser:s ANDRA
//       float4 (den första är skräp, så en förskjuten offset syns) och B ur
//       en 2 × 2-Bitmap (närmaste texel) på kvadratens fyra kvadranter,
//       uppifrån; placeringens vridning och skala invikta i
//       GlazeFrame.
//   C2b RESERVEN: ett program med syntaxfel på appens rad 7, ett utan källa
//       för backenden och ett okänt namn ritar meshen med det INBYGGDA
//       programmet (stilens färg); `programError` bär raden i kompilatorns
//       form och språket, "saknar <språk>-källa" med namnet respektive
//       "finns inte"; ett upprepat och ett tomt namn läggs inte till;
//       `setProgram` med en rättad källa kompilerar om och ritar.
//   C2c HIMMELSPASSET: en 3D-kamera 45° uppåt med synfältet 90° — varje rad
//       i mittkolumnen = horisont + (zenit − horisont) · sin(45° +
//       atan(ndc_y)) ±2 (strålbasen ur GlazeFrame): översta raden
//       zenitfärgen, nedersta horisontfärgen; ett pass
//       ensamt tar ritytan till 3D-målet.
//   C2d passets `fallback`: ett okänt och ett trasigt program fyller ytan
//       med reservfärgen, och alfa 0 (förvalet) ritar ingenting.
// INTE MÄTT i del C2: Metal; egna program på punktmoln (samma
// bindning som meshens, `bindCustom`) och på instanser (de faller tillbaka
// till det inbyggda — bara loggen säger det); texturer ur ett render-mål i
// ett eget program; att reserven loggas EN gång per rityta och namn; att
// kompileringen sker en gång per fönster och version (fönstrets räknare
// läses inte); och ett pass i en del-vy.
//
// DEL H — TERRÄNG, DRAPERING OCH TEXT I MARKEN genom en RIKTIG
// glaze::Canvas. Terrängen är en ANALYTISK LUTANDE PLATTA z = 0,25x + 0,15y +
// 2 över [−8, 8]² (9 × 9 hörn); förväntat värde är grindens egen hålkamera ur
// camera.h:s ord, kartans egen projektion rakt uppifrån och grindens egna
// mätningar av de ritade plattorna:
//   H1  en röd linje i ett Ground-lager draperad på plattan: fem punkter
//       (x, 2, h(x, 2)) har röda pixlar inom 2 px från projektionen — och den
//       plana projektionen (z = 0), minst 8 px bort, inga.
//   H2  en 2 × 2-Bitmap draperad 1:1 rakt uppifrån: kvadranterna i sina
//       kvadranter (rad 0 i norr) och mittlinjen vid x = 0 ±2 px.
//   H2b DRAPERINGEN UNDERIFRÅN: en plan platta i z = 0 genom
//       kartkameran med y uppåt OCH nedåt (ögat under planet, draperingens
//       rad 0 i söder) — bilden står upprätt och den draperade liggande
//       texten är inte spegelvänd i båda.
//   H3  ett HÅL (två saknade trianglar) visar en låda under plattan och
//       under z = 0 — inget markdjup, inget Ground-lager i z = 0; bredvid
//       hålet bilden. H3b plockningen som bilden: lådan
//       genom hålet med skymningen på, ingen bild i hålet, bilden bredvid på
//       terrängens höjd.
//   H4  en markör i ett Ground-lager står vid markhöjd (±2 px; den plana
//       punkten 8 px bort), och träffen bär ankaret där; plattan lyft 3
//       enheter med `Mesh::setPositions` — terrängens version i byggets
//       nyckel — och markören står vid den nya höjden.
//   H5  LIGGANDE TEXT (`Alignment::Ground`): bred uppifrån, på högkant vid
//       bäringen 90° (den följer kartan), och tiltad 50° höjden förkortad med
//       cos 50° (±12 %) och bredden kvar (±10 %) — i 2D-vägen, i 3D-målet och
//       som `text3` (Glyph3).
//   H5b dess KOLLISIONSRUTA är den projicerade quadens: två
//       liggande texter vars ritade plattor går fria under tilt syns båda,
//       fast en oförkortad ruta hade krockat (avståndet ur grindens egna
//       mätningar av plattorna); kontrollen — ritade plattor som krockar —
//       döljer den senare.
//   H5c den liggande texten blir aldrig spegelvänd: plattan
//       till höger om och under ankaret med y uppåt och nedåt, till vänster
//       om och ovanför med kartan vriden 180°.
//   H6  draperingens räknare: ingen omritning utan ändring eller vid en
//       panorering inom klassen; en ändrad linje ritar om plattan en gång, en
//       linje i en chunk som inte når den inte, och en zoom 4× (ny klass) en
//       gång; den liggande texten på terräng syns draperad.
//   H6b en del-vy med terrängen, ett annat lagerurval och en
//       annan zoom ritar inte om draperingen frame för frame — den är
//       huvudvyns, och del-vyn visar den.
//   H6c ett språkbyte på en liggande text med `tr(id)` ritar
//       om plattan en gång (lagrets `textVersion` i markstämpeln).
//   H7 en ändring som kräver flera bilders draperingsbudget
//       (8 × 8 rutor sedda lågt — rutorna vid ögat får taket 2048):
//       efter EN budgeterad skärmbild ritar `renderToBitmap` resten
//       (`DrapeMode::Complete`, premissen ≥ 2), och skärmen ritad klart är
//       pixellik bilden (±1 per kanal, de rundade hörnen undantagna).
//   H8 en bild som väntar på uppladdningsbudgeten (1 byte —
//       en bild per frame) ritas in i draperingen när den kommer (bildcachens
//       innehållsgeneration i markstämpeln).
// INTE MÄTT i del H: Metal; draperade px-linjers bredd mot skärmen
// (exakt bara där klassen motsvarar avtrycket);
// LRU-vräkningen mot minnesbudgeten och målens pool (ingen grind sänker
// budgeten); flera terrängmeshar som överlappar (den översta ytan — pick_gate mäter
// `heightAt`); detaljnivåer i en terrängmesh (etiketternas höjd ur den
// finaste, ritningen ur den ritade nivån — de kan skilja); terräng i
// `renderToBitmap`; och liggande markörer (`MarkerStyle::rotation =
// Ground`, samma flagga som texten).
//
// SABOTAGEN för del C2 och H — alla 21 fäller grinden på D3D11 och GL, med samma fall.
// Passets GlazeFrame tomt (C2c — hela himlen horisontens färg,
// avvikelsen 160), meshprogrammets GlazeFrame tomt (C2a, C2b — inget
// ritat), placeringen inte invikt i GlazeFrame (C2a placeringen), meshens
// reserv överhoppad (C2b — inget ritat), passets reservfärg överhoppad
// (C2d), felet svalt — tomt
// `programError` (C2b raden och den saknade källan) och GlazeUser förskjutet
// en float4 (C2a G 51 i stället för 153, C2b, C2c). Draperingens v
// speglad i `setDrape` (H1, H2), draperingens axlar omkastade i shadern
// (H1, H2, H2b), ingen hysteres (canvas_perf3d — 80 omritningar fram och
// tillbaka under tilt; del H är grön, den byter aldrig klass fram och
// tillbaka), omritning varje bildruta (H6; canvas_perf3d 2420 omritningar),
// etikettens z inte ur terrängen (H4 båda), den liggande texten som
// billboard — kartans flagga (H5 tiltad i alla tre vägarna, H5b),
// kollisionsrutan oförkortad (H5b — den blå dold), spegelregeln borta i
// Glyph-programmen (H2b y uppåt, H5c), draperingen alltid uppifrån (H2b y
// nedåt), terrängens version utanför byggets nyckel (H4 lyft), plockningen
// med Ground-formerna på z = 0 också med terräng (H3b — bild i hålet) och
// med markplanet också med terräng (H3b — ingen låda), markdjupet också med
// terräng (H3 — hålet vitt) och Ground-lagrens geometri i z = 0 också med
// terräng (H1 kontrollen). H6b: draperingens nyckel ur ANROPARENS
// vy (4 → 10 omritningar); därtill
// markstämpeln utan bildcachens innehållsgeneration (H8 — den andra
// bilden saknas), utan lagrets textVersion (H6c — ingen omritning) och
// `DrapeMode::Complete` ignorerad i bilden (H7 — premissen, en drapering, och
// pixlarna).
//
// INGEN TYST SKIP (som i clip_gate): utan headless-yta FALLER grinden.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <gfx/geom2d.h>
#include <gfx/geom3d.h>
#include <gfx/mat4.h>
#include <gfx/sdf.h>
#include <glaze/glaze.h>

#include "canvas/drape.h"       // intern — draperingens räknare (del H)
#include "canvas/resources.h"   // intern — 3D-målets räknare (del G)

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <dxgi1_6.h>
#endif

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;
bool g_ritfel  = false;   // en frame gick inte att läsa — ALDRIG en skip

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

// Tal i en TESTFIL är tillåtna (regel 3 gäller widget-filer) — de här är
// grindens färger, djup och mätpunkter.
const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kRöd{255, 0, 0, 255};
const gfx::Color kBlå{0, 0, 255, 255};
const gfx::Color kGrön{0, 200, 0, 255};
const gfx::Color kSvart{0, 0, 0, 255};
const gfx::Color kGenomskinlig{0, 0, 0, 0};
const gfx::Color kMålBlå{0, 0, 200, 255};   // A7:s opaka bakgrund

// --------------------------------------------------------------- bilderna

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;

    // Utanför bilden (eller en bild som inte gick att läsa) svarar den med en
    // pixel som ingen förväntan matchar — ett fel i MÄTNINGEN, aldrig en
    // läsning utanför bufferten.
    const std::uint8_t* at(int x, int y) const
    {
        static const std::uint8_t ingen[4] = {1, 2, 3, 4};
        if (x < 0 || y < 0 || x >= w || y >= h ||
            px.size() < static_cast<std::size_t>(w) * h * 4)
            return ingen;
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

bool ärFärg(const std::uint8_t* p, gfx::Color c, int tol = 2)
{
    return std::abs(p[0] - c.r) <= tol && std::abs(p[1] - c.g) <= tol &&
           std::abs(p[2] - c.b) <= tol;
}

std::string px(const std::uint8_t* p)
{
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
           std::to_string(p[2]) + "," + std::to_string(p[3]);
}

// ----------------------------------------------------------------- fönstret

using Ritning = std::function<void(gfx::Renderer&, const gfx::Rectf&)>;

// Ytan: vit, och sedan fallets egen ritning — render-mål och scenanrop
// direkt mot renderaren.
class Yta : public glaze::Widget {
public:
    Ritning rita;
    void draw(const glaze::Ctx& c) override
    {
        c.r->fillRect(c.box, kVit);
        if (rita)
            rita(*c.r, c.box);
    }
};

class Fönster : public glaze::AppWindow {
public:
    Fönster()
    {
        setTitle("scene3d_pixel_gate");
        setClientSize(320, 240);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        root().adopt(yta);
    }
    Yta yta;
};

// En frame med `rita` i ytan; fönstrets bild ut.
Bild frame(Fönster& f, Ritning rita)
{
    f.yta.rita = std::move(rita);
    f.yta.invalidate();
    f.window().renderFrame();
    f.yta.rita = nullptr;
    Bild b;
    if (!f.window().captureFrame(b.px, b.w, b.h)) {
        std::printf("  FEL  kunde inte rendera/läsa en frame — ingen "
                    "headless-yta?\n");
        g_ritfel = true;
        ++g_fel;
    }
    return b;
}

// En frame som bara ritar i render-mål.
void iMål(Fönster& f, const std::function<void(gfx::Renderer&)>& rita)
{
    frame(f, [&rita](gfx::Renderer& r, const gfx::Rectf&) { rita(r); });
}

// Målets bild genom readTarget (utanför framen). Ett misslyckat läs är ett
// FEL i fallet, och bilden blir tom (varje mätpunkt faller då).
Bild läs(gfx::Renderer& r, gfx::RenderTarget& t, const std::string& vad)
{
    Bild b;
    if (r.readTarget(t, b.px)) {
        b.w = t.image().width();
        b.h = t.image().height();
    } else {
        b.px.clear();
    }
    check(!b.px.empty(), vad + ": readTarget läser målet");
    return b;
}

std::unique_ptr<gfx::RenderTarget> mål(gfx::Renderer& r, int w, int h,
                                       int samples, bool djup)
{
    gfx::TargetDesc d;
    d.samples = samples;
    d.depth   = djup;
    auto t = r.createRenderTarget(w, h, d);
    t->image().setFilter(gfx::ImageFilter::Nearest);
    return t;
}

// ------------------------------------------------------------ scenanropen

// Världen → målets px: x och y rakt av (eller x speglat kring målets bredd
// W) och z = djup, w = 1 — pixelklipp, kamerans form (gfx/camera.h). Origot
// är chunkens: världen = vertexen + origo.
void pxMatris(float out[16], double ox, double oy, double djup,
              bool spegla = false, double W = 0.0)
{
    gfx::Mat4d m;   // kolumnordning: m[kolumn * 4 + rad]
    m.m[0]  = spegla ? -1.0 : 1.0;
    m.m[5]  = 1.0;
    m.m[12] = spegla ? W - ox : ox;
    m.m[13] = oy;
    m.m[14] = djup;
    m.m[15] = 1.0;
    m.toFloat(out);
}

// En form på GPU:n, ritad med ett av de fyra programmen.
struct Form {
    gfx::MeshProgram                program = gfx::MeshProgram::Fill;
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
    double                          ox = 0.0, oy = 0.0;
    const gfx::Image*               bild  = nullptr;   // Image-programmet
};

template <class V>
void ladda(gfx::Renderer& r, Form& f, const std::vector<V>& v,
           const std::vector<std::uint32_t>& idx, double ox, double oy)
{
    f.vb = r.createBuffer(gfx::BufferKind::Vertex, v.size() * sizeof(V),
                          v.data());
    f.ib = r.createBuffer(gfx::BufferKind::Index,
                          idx.size() * sizeof(std::uint32_t), idx.data());
    f.count = static_cast<std::uint32_t>(idx.size());
    f.ox    = ox;
    f.oy    = oy;
}

// Fill: en rektangel (världen = målets px).
Form fillRuta(gfx::Renderer& r, double x0, double y0, double x1, double y1,
              gfx::Color färg)
{
    const std::vector<std::vector<gfx::Point2d>> ringar = {
        {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
    gfx::PolygonStyle s;
    s.color = färg;
    std::vector<gfx::FillChunk> chunks;
    gfx::buildPolygon(ringar, s, chunks);
    Form f;
    f.program = gfx::MeshProgram::Fill;
    ladda(r, f, chunks.front().vertices, chunks.front().indices,
          chunks.front().originX, chunks.front().originY);
    return f;
}

// Image: fyra hörn i ordningen övre vänster, övre höger, nedre vänster,
// nedre höger (u, v följer dem) — trianglarna 0-1-2 och 1-3-2, platta-formen.
Form bildQuad(gfx::Renderer& r, const gfx::Point2d (&p)[4],
              const gfx::Image& img)
{
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    std::vector<gfx::ImageVertex> v(4);
    for (int i = 0; i < 4; ++i)
        v[i] = gfx::ImageVertex{static_cast<float>(p[i].x),
                                static_cast<float>(p[i].y), uv[i][0],
                                uv[i][1]};
    Form f;
    f.program = gfx::MeshProgram::Image;
    f.bild    = &img;
    ladda(r, f, v, std::vector<std::uint32_t>{0, 1, 2, 1, 3, 2}, 0.0, 0.0);
    return f;
}

Form bildRuta(gfx::Renderer& r, double x0, double y0, double x1, double y1,
              const gfx::Image& img)
{
    const gfx::Point2d p[4] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    return bildQuad(r, p, img);
}

// Line: en vågrät linje, bredden i px (pxPerWidthUnit 1).
Form linje(gfx::Renderer& r, double x0, double y, double x1, float bredd,
           gfx::Color färg)
{
    gfx::PolylineStyle s;
    s.width = bredd;
    s.color = färg;
    const gfx::Point2d p[2] = {{x0, y}, {x1, y}};
    std::vector<gfx::LineChunk> chunks;
    gfx::buildPolyline(p, s, chunks);
    Form f;
    f.program = gfx::MeshProgram::Line;
    ladda(r, f, chunks.front().vertices, chunks.front().indices,
          chunks.front().originX, chunks.front().originY);
    return f;
}

// Glyph: en kvadratisk markör, sidan i px.
Form markör(gfx::Renderer& r, double x, double y, float sida, gfx::Color färg)
{
    gfx::MarkerLayoutStyle s;
    s.shape = gfx::MarkerShape::Square;
    s.size  = sida;
    s.fill  = färg;
    const gfx::Point2d p[1] = {{x, y}};
    std::vector<gfx::GlyphChunk> chunks;
    gfx::buildMarkers(p, s, chunks);
    Form f;
    f.program = gfx::MeshProgram::Glyph;
    ladda(r, f, chunks.front().vertices, chunks.front().indices,
          chunks.front().originX, chunks.front().originY);
    return f;
}

void rita(gfx::Renderer& r, const Form& f, double djup,
          const gfx::RenderState& s = {}, bool spegla = false, double W = 0.0)
{
    gfx::MeshDraw d;
    d.program    = f.program;
    d.vertices   = gfx::slice(*f.vb);
    d.indices    = gfx::slice(*f.ib);
    d.indexCount = f.count;
    d.image      = f.bild;
    pxMatris(d.uniforms.viewProj, f.ox, f.oy, djup, spegla, W);
    d.uniforms.pxPerWidthUnit = 1.0f;
    d.state                   = s;
    r.drawMesh(d);
}

gfx::RenderState läge(gfx::DepthTest t, bool skriv = true)
{
    gfx::RenderState s;
    s.depth      = t;
    s.depthWrite = skriv;
    return s;
}

std::unique_ptr<gfx::Image> enfärgad(gfx::Renderer& r, gfx::Color c)
{
    const std::uint8_t p[4] = {c.r, c.g, c.b, c.a};
    auto img = r.createImage(1, 1, gfx::ImageFormat::Rgba8, p, 0);
    img->setFilter(gfx::ImageFilter::Nearest);
    return img;
}

// ------------------------------------------------------------------- A1
//
// Den närmare (röd, z 0,3) mot den bortre (blå, z 0,6) i BÅDA ordningarna, i
// alla fyra programmen — och utan djup (Off) den sist ritade.
void fallA1(Fönster& f)
{
    gfx::Renderer& r    = f.window().renderer();
    auto           rödB = enfärgad(r, kRöd);
    auto           blåB = enfärgad(r, kBlå);
    Form           former[4][2] = {
        {fillRuta(r, 16, 16, 48, 48, kRöd), fillRuta(r, 16, 16, 48, 48, kBlå)},
        {bildRuta(r, 16, 16, 48, 48, *rödB), bildRuta(r, 16, 16, 48, 48, *blåB)},
        {linje(r, 8, 32, 56, 16.0f, kRöd), linje(r, 8, 32, 56, 16.0f, kBlå)},
        {markör(r, 32, 32, 24.0f, kRöd), markör(r, 32, 32, 24.0f, kBlå)}};
    const char* namn[4] = {"Fill", "Image", "Line", "Glyph"};
    std::unique_ptr<gfx::RenderTarget> närFörst[4], fjärranFörst[4], utan[4];
    for (int i = 0; i < 4; ++i) {
        närFörst[i]     = mål(r, 64, 64, 1, true);
        fjärranFörst[i] = mål(r, 64, 64, 1, true);
        utan[i]         = mål(r, 64, 64, 1, true);
    }
    iMål(f, [&](gfx::Renderer& rr) {
        for (int i = 0; i < 4; ++i) {
            const Form& röd = former[i][0];
            const Form& blå = former[i][1];
            rr.pushTarget(*närFörst[i], kVit);
            rita(rr, röd, 0.3, läge(gfx::DepthTest::Less));
            rita(rr, blå, 0.6, läge(gfx::DepthTest::Less));
            rr.popTarget();
            rr.pushTarget(*fjärranFörst[i], kVit);
            rita(rr, blå, 0.6, läge(gfx::DepthTest::Less));
            rita(rr, röd, 0.3, läge(gfx::DepthTest::Less));
            rr.popTarget();
            rr.pushTarget(*utan[i], kVit);
            rita(rr, röd, 0.3);
            rita(rr, blå, 0.6);
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    for (int i = 0; i < 4; ++i) {
        const std::string p = std::string("A1 ") + namn[i];
        const Bild a = läs(r, *närFörst[i], p + " närmast först");
        const Bild b = läs(r, *fjärranFörst[i], p + " bortre först");
        const Bild c = läs(r, *utan[i], p + " Off");
        check(ärFärg(a.at(32, 32), kRöd),
              p + ": Less — den närmare vinner, ritad FÖRST", px(a.at(32, 32)));
        check(ärFärg(b.at(32, 32), kRöd),
              p + ": Less — den närmare vinner, ritad SIST", px(b.at(32, 32)));
        check(ärFärg(c.at(32, 32), kBlå),
              p + ": Off — den sist ritade vinner (kontrollen)",
              px(c.at(32, 32)));
    }
}

// ------------------------------------------------------------------- A2
void fallA2(Fönster& f)
{
    gfx::Renderer& r     = f.window().renderer();
    const Form     grön  = fillRuta(r, 16, 16, 48, 48, kGrön);
    const Form     röd   = fillRuta(r, 16, 16, 48, 48, kRöd);
    const Form     blå   = fillRuta(r, 16, 16, 48, 48, kBlå);
    auto           nära  = mål(r, 64, 64, 1, true);
    auto           bort  = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*nära, kVit);
        rita(rr, grön, 0.5, läge(gfx::DepthTest::Less));
        rita(rr, röd, 0.3, läge(gfx::DepthTest::Greater, false));
        rr.popTarget();
        rr.pushTarget(*bort, kVit);
        rita(rr, grön, 0.5, läge(gfx::DepthTest::Less));
        rita(rr, blå, 0.7, läge(gfx::DepthTest::Greater, false));
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild a = läs(r, *nära, "A2 närmare");
    const Bild b = läs(r, *bort, "A2 bortre");
    check(ärFärg(a.at(32, 32), kGrön),
          "A2 Greater: den NÄRMARE (z 0,3 mot 0,5) syns inte", px(a.at(32, 32)));
    check(ärFärg(b.at(32, 32), kBlå),
          "A2 Greater: den BORTRE (z 0,7 mot 0,5) syns", px(b.at(32, 32)));
}

// ------------------------------------------------------------------- A3
void fallA3(Fönster& f)
{
    gfx::Renderer& r    = f.window().renderer();
    const Form     röd  = fillRuta(r, 16, 16, 48, 48, kRöd);
    const Form     blå  = fillRuta(r, 16, 16, 48, 48, kBlå);
    auto           le   = mål(r, 64, 64, 1, true);
    auto           less = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*le, kVit);
        rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
        rita(rr, blå, 0.5, läge(gfx::DepthTest::LessEqual));
        rr.popTarget();
        rr.pushTarget(*less, kVit);
        rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
        rita(rr, blå, 0.5, läge(gfx::DepthTest::Less));
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild a = läs(r, *le, "A3 LessEqual");
    const Bild b = läs(r, *less, "A3 Less");
    check(ärFärg(a.at(32, 32), kBlå),
          "A3 LessEqual vid SAMMA djup: den andra vinner", px(a.at(32, 32)));
    check(ärFärg(b.at(32, 32), kRöd),
          "A3 Less vid samma djup: den första står kvar (kontrollen)",
          px(b.at(32, 32)));
}

// ------------------------------------------------------------------- A4
//
// Röd på 0,5 (skriven), grön på 0,3 UTAN skrivning och sedan en mindre blå
// på 0,4: den blå jämförs mot 0,5 och syns. Med skrivning hade den gröna
// lagt 0,3 och den blå fallit.
void fallA4(Fönster& f)
{
    gfx::Renderer& r       = f.window().renderer();
    const Form     röd     = fillRuta(r, 16, 16, 48, 48, kRöd);
    const Form     grön    = fillRuta(r, 16, 16, 48, 48, kGrön);
    const Form     blå     = fillRuta(r, 24, 24, 40, 40, kBlå);
    auto           oskriv  = mål(r, 64, 64, 1, true);
    auto           skriven = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*oskriv, kVit);
        rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
        rita(rr, grön, 0.3, läge(gfx::DepthTest::Less, false));
        rita(rr, blå, 0.4, läge(gfx::DepthTest::Less));
        rr.popTarget();
        rr.pushTarget(*skriven, kVit);
        rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
        rita(rr, grön, 0.3, läge(gfx::DepthTest::Less, true));
        rita(rr, blå, 0.4, läge(gfx::DepthTest::Less));
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild a = läs(r, *oskriv, "A4 utan skrivning");
    const Bild b = läs(r, *skriven, "A4 med skrivning");
    check(ärFärg(a.at(20, 20), kGrön),
          "A4 rutan UTAN skrivning ritades själv (testet släppte igenom den)",
          px(a.at(20, 20)));
    check(ärFärg(a.at(32, 32), kBlå),
          "A4 utan skrivning: en senare ruta jämförs mot det GAMLA djupet",
          px(a.at(32, 32)));
    check(ärFärg(b.at(32, 32), kGrön),
          "A4 med skrivning: den senare rutan faller (kontrollen)",
          px(b.at(32, 32)));
}

// ------------------------------------------------------------------- A5
void fallA5(Fönster& f)
{
    gfx::Renderer& r   = f.window().renderer();
    const Form     röd = fillRuta(r, 16, 16, 48, 48, kRöd);
    const Form     blå = fillRuta(r, 16, 16, 48, 48, kBlå);
    const float    bias[3] = {-1.0f, 0.0f, 1.0f};
    std::unique_ptr<gfx::RenderTarget> t[3];
    for (auto& m : t)
        m = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        for (int i = 0; i < 3; ++i) {
            rr.pushTarget(*t[i], kVit);
            rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
            gfx::RenderState s = läge(gfx::DepthTest::Less);
            s.depthBias        = bias[i];
            rita(rr, blå, 0.5, s);
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    const Bild a = läs(r, *t[0], "A5 bias −1");
    const Bild b = läs(r, *t[1], "A5 bias 0");
    const Bild c = läs(r, *t[2], "A5 bias +1");
    check(ärFärg(a.at(32, 32), kBlå),
          "A5 bias −1 på en KOPLANAR ruta: den vinner under Less",
          px(a.at(32, 32)));
    check(ärFärg(b.at(32, 32), kRöd), "A5 bias 0: den koplanara faller",
          px(b.at(32, 32)));
    check(ärFärg(c.at(32, 32), kRöd),
          "A5 bias +1: den koplanara skjuts bort och faller", px(c.at(32, 32)));
}

// ------------------------------------------------------------------- A6
//
// FÖRVÄNTAT VÄRDE ÄR GRINDENS EGEN VINDNING: kryssprodukten av den projicerade
// triangeln i målets px (y nedåt) — negativ är moturs för ögat, alltså
// framsidan (gfx/scene.h). Samma data under en speglad matris vänder den.
bool motursFörÖgat(bool spegla, const gfx::Point2d (&p)[4], int a, int b, int c)
{
    auto X = [spegla](double x) { return spegla ? 64.0 - x : x; };
    const double ax = X(p[a].x), ay = p[a].y;
    const double bx = X(p[b].x), by = p[b].y;
    const double cx = X(p[c].x), cy = p[c].y;
    return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay) < 0.0;
}

void fallA6(Fönster& f)
{
    gfx::Renderer&     r    = f.window().renderer();
    auto               rödB = enfärgad(r, kRöd);
    const gfx::Point2d hörn[4] = {{16, 16}, {48, 16}, {16, 48}, {48, 48}};
    const Form         ruta    = bildQuad(r, hörn, *rödB);
    // Quadens båda trianglar (0-1-2, 1-3-2) har samma vindning, och
    // speglingen vänder den — förutsättningen för det förväntade värdet.
    const bool rakFram   = motursFörÖgat(false, hörn, 0, 1, 2);
    const bool spegelFram = motursFörÖgat(true, hörn, 0, 1, 2);
    check(rakFram == motursFörÖgat(false, hörn, 1, 3, 2) &&
              spegelFram == motursFörÖgat(true, hörn, 1, 3, 2) &&
              rakFram != spegelFram,
          "A6 förutsättning: trianglarna delar vindning, och speglingen vänder "
          "den");
    const gfx::Cull cull[3] = {gfx::Cull::None, gfx::Cull::Back,
                               gfx::Cull::Front};
    const char*     namn[3] = {"None", "Back", "Front"};
    std::unique_ptr<gfx::RenderTarget> t[3][2];
    for (auto& par : t)
        for (auto& m : par)
            m = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        for (int c = 0; c < 3; ++c)
            for (int s = 0; s < 2; ++s) {
                rr.pushTarget(*t[c][s], kVit);
                gfx::RenderState st = läge(gfx::DepthTest::Less);
                st.cull             = cull[c];
                rita(rr, ruta, 0.5, st, s == 1, 64.0);
                rr.popTarget();
            }
    });
    if (g_ritfel)
        return;
    for (int c = 0; c < 3; ++c)
        for (int s = 0; s < 2; ++s) {
            const bool fram  = s == 1 ? spegelFram : rakFram;
            const bool syns  = cull[c] == gfx::Cull::None   ? true
                               : cull[c] == gfx::Cull::Back ? fram
                                                            : !fram;
            const Bild b     = läs(r, *t[c][s], "A6");
            const std::string vad =
                std::string("A6 Cull::") + namn[c] + ", " +
                (s == 1 ? "SPEGLAD" : "rak") + " matris (" +
                (fram ? "framsidan" : "baksidan") + " mot kameran): " +
                (syns ? "syns" : "gallras");
            check(ärFärg(b.at(32, 32), syns ? kRöd : kVit), vad,
                  px(b.at(32, 32)));
        }
}

// ------------------------------------------------------------------- A7
//
// Blandningens FORMEL, räknad här (gfx/scene.h `Blend`) — källan rak eller
// (Premultiplied) premultiplicerad, målet premultiplicerat och opakt, och
// svaret med rak alfa som readTarget ger det.
struct Rgba {
    double r, g, b, a;   // 0–1
};

Rgba blanda(const Rgba& src, const Rgba& dst, gfx::Blend b)
{
    auto sat = [](double v) { return std::min(1.0, std::max(0.0, v)); };
    Rgba o{};
    switch (b) {
    case gfx::Blend::Alpha:
        o = {src.r * src.a + dst.r * (1 - src.a),
             src.g * src.a + dst.g * (1 - src.a),
             src.b * src.a + dst.b * (1 - src.a), src.a + dst.a * (1 - src.a)};
        break;
    case gfx::Blend::Premultiplied:
        o = {src.r + dst.r * (1 - src.a), src.g + dst.g * (1 - src.a),
             src.b + dst.b * (1 - src.a), src.a + dst.a * (1 - src.a)};
        break;
    case gfx::Blend::Additive:
        o = {sat(src.r * src.a + dst.r), sat(src.g * src.a + dst.g),
             sat(src.b * src.a + dst.b), sat(src.a + dst.a)};
        break;
    case gfx::Blend::Opaque:
    case gfx::Blend::DepthOnly:
        o = src;
        break;
    }
    // Rak alfa ut, som readTarget (premultiplicerat lagrat → delat).
    if (o.a > 0.0)
        o = {std::min(1.0, o.r / o.a), std::min(1.0, o.g / o.a),
             std::min(1.0, o.b / o.a), o.a};
    return o;
}

Rgba rgba(gfx::Color c)
{
    return {c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0};
}

void fallA7(Fönster& f)
{
    gfx::Renderer&   r = f.window().renderer();
    const gfx::Color halvRöd{255, 0, 0, 128};
    const gfx::Color halvRödPre{128, 0, 0, 128};   // premultiplicerad
    struct Prov {
        const char* namn;
        gfx::Blend  blend;
        gfx::Color  färg;
    };
    const Prov prov[4] = {{"Alpha", gfx::Blend::Alpha, halvRöd},
                          {"Premultiplied", gfx::Blend::Premultiplied,
                           halvRödPre},
                          {"Additive", gfx::Blend::Additive, halvRöd},
                          {"Opaque", gfx::Blend::Opaque, halvRöd}};
    Form former[4] = {fillRuta(r, 16, 16, 48, 48, prov[0].färg),
                      fillRuta(r, 16, 16, 48, 48, prov[1].färg),
                      fillRuta(r, 16, 16, 48, 48, prov[2].färg),
                      fillRuta(r, 16, 16, 48, 48, prov[3].färg)};
    // Premultiplied-kontrollen: SAMMA premultiplicerade färg med Alpha
    // blandas annorlunda (rött 64 i stället för 128).
    std::unique_ptr<gfx::RenderTarget> t[4];
    for (auto& m : t)
        m = mål(r, 64, 64, 1, true);
    auto preKontroll = mål(r, 64, 64, 1, true);
    // DepthOnly: förpasset (rött, z 0,3) skriver bara djup; den gröna på 0,5
    // bakom det döljs — och syns utan förpasset.
    const Form djupRuta = fillRuta(r, 16, 16, 48, 48, kRöd);
    const Form grönBak  = fillRuta(r, 16, 16, 48, 48, kGrön);
    auto       djupOnly = mål(r, 64, 64, 1, true);
    auto       utanPass = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        for (int i = 0; i < 4; ++i) {
            rr.pushTarget(*t[i], kMålBlå);
            gfx::RenderState s;
            s.blend = prov[i].blend;
            rita(rr, former[i], 0.5, s);
            rr.popTarget();
        }
        rr.pushTarget(*preKontroll, kMålBlå);
        rita(rr, former[1], 0.5);   // Alpha
        rr.popTarget();
        rr.pushTarget(*djupOnly, kMålBlå);
        gfx::RenderState dp = läge(gfx::DepthTest::Less);
        dp.blend            = gfx::Blend::DepthOnly;
        rita(rr, djupRuta, 0.3, dp);
        rita(rr, grönBak, 0.5, läge(gfx::DepthTest::Less));
        rr.popTarget();
        rr.pushTarget(*utanPass, kMålBlå);
        rita(rr, grönBak, 0.5, läge(gfx::DepthTest::Less));
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    for (int i = 0; i < 4; ++i) {
        const Bild           b = läs(r, *t[i], std::string("A7 ") + prov[i].namn);
        const std::uint8_t* p = b.at(32, 32);
        const Rgba facit = blanda(rgba(prov[i].färg), rgba(kMålBlå),
                                  prov[i].blend);
        const int fr = static_cast<int>(std::lround(facit.r * 255.0));
        const int fg = static_cast<int>(std::lround(facit.g * 255.0));
        const int fb = static_cast<int>(std::lround(facit.b * 255.0));
        const int fa = static_cast<int>(std::lround(facit.a * 255.0));
        const bool ok = std::abs(p[0] - fr) <= 2 && std::abs(p[1] - fg) <= 2 &&
                        std::abs(p[2] - fb) <= 2 && std::abs(p[3] - fa) <= 2;
        check(ok,
              std::string("A7 ") + prov[i].namn +
                  ": blandningens formel mot det blå målet",
              px(p) + " mot " + std::to_string(fr) + "," + std::to_string(fg) +
                  "," + std::to_string(fb) + "," + std::to_string(fa));
    }
    const Bild pk = läs(r, *preKontroll, "A7 Premultiplied-kontrollen");
    check(std::abs(pk.at(32, 32)[0] - 64) <= 2,
          "A7 kontrollen: samma premultiplicerade färg med Alpha ger rött 64 — "
          "Premultiplied är en egen blandning",
          px(pk.at(32, 32)));
    const Bild dO = läs(r, *djupOnly, "A7 DepthOnly");
    const Bild uP = läs(r, *utanPass, "A7 utan förpass");
    check(ärFärg(dO.at(32, 32), kMålBlå),
          "A7 DepthOnly: färgen orörd, och djupet skrevs (rutan bakom döljs)",
          px(dO.at(32, 32)));
    check(ärFärg(uP.at(32, 32), kGrön),
          "A7 utan DepthOnly-passet syns rutan bakom (kontrollen)",
          px(uP.at(32, 32)));
}

// ------------------------------------------------------------------- A8
//
// UI-fillRect efter ett scenanrop vars läge UI:t INTE får ärva. Djupet: ett
// mål fullt av rött på z 0,9 och sedan ett anrop med Greater — ärvt hade UI:ts
// djup (0 i D3D11, 0,5 i GL) fallit mot 0,9. Gallringen: UI:ts quadar är
// medurs för ögat, och Back hade fällt dem. Färgmasken: DepthOnly. Blandningen:
// Additive hade gett gult.
void fallA8(Fönster& f)
{
    gfx::Renderer& r     = f.window().renderer();
    const Form     helt  = fillRuta(r, 0, 0, 64, 64, kRöd);
    const Form     liten = fillRuta(r, 0, 0, 8, 8, kBlå);
    struct Prov {
        const char*      namn;
        gfx::RenderState senaste;
    };
    gfx::RenderState greater = läge(gfx::DepthTest::Greater, false);
    gfx::RenderState back;
    back.cull = gfx::Cull::Back;
    gfx::RenderState djupOnly;
    djupOnly.blend = gfx::Blend::DepthOnly;
    gfx::RenderState additiv;
    additiv.blend = gfx::Blend::Additive;
    const Prov prov[4] = {{"djupet (Greater)", greater},
                          {"gallringen (Cull::Back)", back},
                          {"färgmasken (DepthOnly)", djupOnly},
                          {"blandningen (Additive)", additiv}};
    std::unique_ptr<gfx::RenderTarget> t[4];
    for (auto& m : t)
        m = mål(r, 64, 64, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        for (int i = 0; i < 4; ++i) {
            rr.pushTarget(*t[i], kVit);
            rita(rr, helt, 0.9, läge(gfx::DepthTest::Always));
            rita(rr, liten, 0.5, prov[i].senaste);
            rr.fillRect(gfx::Rectf{16.0f, 16.0f, 32.0f, 32.0f}, kGrön);
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    for (int i = 0; i < 4; ++i) {
        const Bild b = läs(r, *t[i], "A8");
        check(ärFärg(b.at(32, 32), kGrön),
              std::string("A8 UI-fillRect efter ett anrop som ändrade ") +
                  prov[i].namn + " ritas i sin egen färg",
              px(b.at(32, 32)));
    }
}

// ------------------------------------------------------------------- A9
//
// ÅTERANVÄNT MÅL: varje mål ovan ritas EN gång, och
// det nya målets djup är redan 1 — en pushTarget som inte rensade djupet
// hade gått igenom dem alla. Här samma mål i två frames: först en röd ruta
// på z 0,3, sedan en blå på 0,6 med Less. Rensas djupet syns den blå.
void fallA9(Fönster& f)
{
    gfx::Renderer& r   = f.window().renderer();
    const Form     röd = fillRuta(r, 16, 16, 48, 48, kRöd);
    const Form     blå = fillRuta(r, 16, 16, 48, 48, kBlå);
    auto           t1  = mål(r, 64, 64, 1, true);
    auto           t4  = mål(r, 64, 64, r.supportedSamples(4), true);
    // Före varje rensning i andra framen lämnar ett scenanrop i ett ANNAT mål
    // med djup både färg- och djupmasken av (DepthOnly utan skrivning). GL:s
    // glClear lyder maskerna, så rensningen måste sätta dem själv — utan det
    // mäter fallet bara rensningar efter framens eget UI-läge, och en GL-clear
    // utan UI-läget fälls inte.
    auto             maskMål  = mål(r, 64, 64, 1, true);
    gfx::RenderState utanMask = läge(gfx::DepthTest::Less, false);
    utanMask.blend            = gfx::Blend::DepthOnly;
    iMål(f, [&](gfx::Renderer& rr) {
        for (auto* t : {t1.get(), t4.get()}) {
            rr.pushTarget(*t, kVit);
            rita(rr, röd, 0.3, läge(gfx::DepthTest::Less));
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    iMål(f, [&](gfx::Renderer& rr) {
        for (auto* t : {t1.get(), t4.get()}) {
            rr.pushTarget(*maskMål, kVit);
            rita(rr, röd, 0.5, utanMask);
            rr.popTarget();
            rr.pushTarget(*t, kVit);
            rita(rr, blå, 0.6, läge(gfx::DepthTest::Less));
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    const Bild a = läs(r, *t1, "A9 1×");
    const Bild b = läs(r, *t4, "A9 MSAA");
    check(ärFärg(a.at(32, 32), kBlå) && ärFärg(b.at(32, 32), kBlå),
          "A9 återanvänt mål: pushTarget rensar djupet till 1 (den bortre "
          "rutan i andra framen syns), 1× och MSAA — också efter ett anrop "
          "som lämnade maskerna av",
          px(a.at(32, 32)) + " / " + px(b.at(32, 32)));
}

// ------------------------------------------------------------------- B1
int mellantoner(const Bild& b)
{
    int n = 0;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            const int v = b.at(x, y)[0];
            if (v > 0 && v < 255)
                ++n;
        }
    return n;
}

void fallB1(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    const int      s4 = r.supportedSamples(4);
    auto           t1 = mål(r, 64, 64, 1, true);
    auto           t4 = mål(r, 64, 64, s4, true);
    check(t1->image().valid() && t1->samples() == 1 && t1->hasDepth() &&
              t4->image().valid() && t4->samples() == s4 && s4 > 1 &&
              t4->hasDepth(),
          "B1 målen: 1 prov och " + std::to_string(s4) +
              " prov (MSAA finns — ingen skip utan), båda med djup",
          std::to_string(t1->samples()) + " / " + std::to_string(t4->samples()));
    // En vit quad vriden 30° kring mitten, halva sidan 14 px.
    auto         vit = enfärgad(r, kVit);
    const double c   = std::cos(30.0 * M_PI / 180.0);
    const double s   = std::sin(30.0 * M_PI / 180.0);
    auto hörn = [&](double dx, double dy) {
        return gfx::Point2d{32.0 + dx * c - dy * s, 32.0 + dx * s + dy * c};
    };
    const gfx::Point2d p[4] = {hörn(-14, -14), hörn(14, -14), hörn(-14, 14),
                               hörn(14, 14)};
    const Form quad = bildQuad(r, p, *vit);
    iMål(f, [&](gfx::Renderer& rr) {
        for (auto* t : {t1.get(), t4.get()}) {
            rr.pushTarget(*t, kSvart);
            rita(rr, quad, 0.5, läge(gfx::DepthTest::Less));
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    const Bild b1 = läs(r, *t1, "B1 1×");
    const Bild b4 = läs(r, *t4, "B1 MSAA");
    const int  m1 = mellantoner(b1);
    const int  m4 = mellantoner(b4);
    check(m1 == 0 && b1.at(32, 32)[0] == 255,
          "B1 1×: kanten bara 0/255 (Image-programmet kantutjämnar inte) och "
          "mitten 255",
          std::to_string(m1) + " mellantoner");
    check(m4 >= 20 && b4.at(32, 32)[0] == 255,
          "B1 " + std::to_string(s4) +
              "×: kantpixlarna STRIKT mellan 0 och 255, mitten 255",
          std::to_string(m4) + " mellantoner");
}

// ------------------------------------------------------------------- B2
void fallB2(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    const int      s0 = r.supportedSamples(0), s1 = r.supportedSamples(1);
    const int      s2 = r.supportedSamples(2), s3 = r.supportedSamples(3);
    const int      s4 = r.supportedSamples(4), s8 = r.supportedSamples(8);
    std::printf("  info supportedSamples: 1→%d 2→%d 4→%d 8→%d\n", s1, s2, s4,
                s8);
    check(s0 == 1 && s1 == 1, "B2 0 och 1 prov ger 1",
          std::to_string(s0) + " / " + std::to_string(s1));
    check(s8 == 8 || s8 == 4 || s8 == 2, "B2 supportedSamples(8) ∈ {8, 4, 2}",
          std::to_string(s8));
    check((s2 == 1 || s2 == 2) && s3 == s2 &&
              (s4 == 1 || s4 == 2 || s4 == 4) && s2 <= s4 && s4 <= s8,
          "B2 det närmaste stödda ≤ det begärda, och svaren växer",
          std::to_string(s2) + "/" + std::to_string(s3) + "/" +
              std::to_string(s4) + "/" + std::to_string(s8));
    auto t8 = mål(r, 16, 16, 8, false);
    check(t8->image().valid() && t8->samples() == s8 && !t8->hasDepth(),
          "B2 ett mål som begär 8 prov får supportedSamples(8)",
          std::to_string(t8->samples()));
    auto t3 = mål(r, 16, 16, 3, false);
    auto t0 = mål(r, 0, 16, 1, true);
    check(!t3->image().valid() && t3->samples() == 1 && !t0->image().valid() &&
              !t0->hasDepth(),
          "B2 provtagningen 3 och måttet 0 ger ogiltiga mål (targetDescOk)");
}

// ------------------------------------------------------------------- B3
//
// Fyra fält (rött, grönt, blått, vitt) och en halvgenomskinlig gul remsa i
// ett 64 × 48-mål — readTarget ska läsa dem RÄTTVÄNT och med RAK alfa, och
// fönstrets egen väg (drawImage + captureFrame) ska se samma färger.
void ritaMönster(gfx::Renderer& rr)
{
    rr.fillRect(gfx::Rectf{0, 0, 32, 16}, kRöd);
    rr.fillRect(gfx::Rectf{32, 0, 32, 16}, kGrön);
    rr.fillRect(gfx::Rectf{0, 16, 32, 16}, kBlå);
    rr.fillRect(gfx::Rectf{32, 16, 32, 16}, kVit);
    rr.fillRect(gfx::Rectf{0, 32, 64, 16}, gfx::Color{255, 255, 0, 128});
}

void fallB3(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    auto           tA = mål(r, 64, 48, 1, false);
    auto           tB = mål(r, 64, 48, r.supportedSamples(4), true);
    gfx::Rectf     dA{}, dB{};
    const Bild fönster = frame(f, [&](gfx::Renderer& rr, const gfx::Rectf& box) {
        for (auto* t : {tA.get(), tB.get()}) {
            rr.pushTarget(*t, kGenomskinlig);
            ritaMönster(rr);
            rr.popTarget();
        }
        dA = gfx::Rectf{std::ceil(box.x) + 20, std::ceil(box.y) + 20, 64, 48};
        dB = gfx::Rectf{std::ceil(box.x) + 120, std::ceil(box.y) + 20, 64, 48};
        rr.drawImage(tA->image(), dA);
        rr.drawImage(tB->image(), dB);
    });
    if (g_ritfel)
        return;
    struct Fält {
        int         x, y;
        gfx::Color  färg;
        const char* namn;
    };
    const Fält fält[4] = {{8, 8, kRöd, "övre vänster röd"},
                          {56, 8, kGrön, "övre höger grön"},
                          {8, 24, kBlå, "nedre vänster blå"},
                          {56, 24, kVit, "nedre höger vit"}};
    const char*       namn[2] = {"1×", "MSAA + djup"};
    gfx::RenderTarget* m[2]   = {tA.get(), tB.get()};
    const gfx::Rectf   d[2]   = {dA, dB};
    for (int i = 0; i < 2; ++i) {
        const Bild b = läs(r, *m[i], std::string("B3 ") + namn[i]);
        bool rätt = b.w == 64 && b.h == 48;
        std::string fick;
        for (const Fält& fl : fält) {
            rätt = rätt && ärFärg(b.at(fl.x, fl.y), fl.färg) &&
                   b.at(fl.x, fl.y)[3] == 255;
            fick += px(b.at(fl.x, fl.y)) + " ";
        }
        check(rätt,
              std::string("B3 ") + namn[i] +
                  ": readTarget RÄTTVÄNT — rad 0 överst, fälten där de ritades",
              fick);
        const std::uint8_t* gul = b.at(32, 40);
        check(ärFärg(gul, gfx::Color{255, 255, 0, 255}) &&
                  std::abs(gul[3] - 128) <= 2,
              std::string("B3 ") + namn[i] +
                  ": RAK alfa — den halvgenomskinliga remsan läses 255,255,0 "
                  "med alfa 128",
              px(gul));
        // Fönstrets väg: samma fält, och remsan över vitt.
        const int ox = static_cast<int>(d[i].x), oy = static_cast<int>(d[i].y);
        bool fönsterRätt = true;
        for (const Fält& fl : fält)
            fönsterRätt =
                fönsterRätt && ärFärg(fönster.at(ox + fl.x, oy + fl.y), fl.färg);
        check(fönsterRätt &&
                  ärFärg(fönster.at(ox + 32, oy + 40),
                         gfx::Color{255, 255, 127, 255}, 3),
              std::string("B3 ") + namn[i] +
                  ": fönstrets väg (drawImage + captureFrame) ser samma färger",
              px(fönster.at(ox + 8, oy + 8)) + " / " +
                  px(fönster.at(ox + 32, oy + 40)));
    }
}

// ------------------------------------------------------------------- B4
//
// Samma scen i en frame, utanför en (begin/endOffscreen) och i ett NYTT
// fönster — scenen har djup, en provtagning och UI.
struct B4Scen {
    Form röd, blå, grön;
    B4Scen(gfx::Renderer& r)
        : röd(fillRuta(r, 16, 16, 48, 48, kRöd)),
          blå(fillRuta(r, 24, 24, 56, 56, kBlå)),
          grön(fillRuta(r, 4, 4, 28, 28, kGrön))
    {
    }
    void rita(gfx::Renderer& rr, gfx::RenderTarget& t) const
    {
        rr.pushTarget(t, kVit);
        ::rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
        ::rita(rr, blå, 0.3, läge(gfx::DepthTest::Less));
        ::rita(rr, grön, 0.7, läge(gfx::DepthTest::Less));
        rr.fillRect(gfx::Rectf{0, 0, 64, 2}, kSvart);
        rr.popTarget();
    }
};

int störstaSkillnad(const Bild& a, const Bild& b)
{
    if (a.w != b.w || a.h != b.h || a.px.size() != b.px.size() || a.px.empty())
        return 256;
    int m = 0;
    for (std::size_t i = 0; i < a.px.size(); ++i)
        m = std::max(m, std::abs(a.px[i] - b.px[i]));
    return m;
}

void fallB4(Fönster& f)
{
    gfx::Renderer& r    = f.window().renderer();
    const int      s4   = r.supportedSamples(4);
    const B4Scen   scen(r);
    auto           tIn  = mål(r, 64, 64, s4, true);
    auto           tUt  = mål(r, 64, 64, s4, true);
    bool           vägradIFrame = false;
    iMål(f, [&](gfx::Renderer& rr) {
        scen.rita(rr, *tIn);
        // Ett block inuti en frame vägras (och öppnas alltså inte).
        vägradIFrame = !rr.beginOffscreen();
    });
    if (g_ritfel)
        return;
    check(vägradIFrame, "B4 beginOffscreen inuti en frame vägras");
    const Bild in = läs(r, *tIn, "B4 i en frame");
    check(ärFärg(in.at(20, 20), kRöd) && ärFärg(in.at(36, 36), kBlå) &&
              ärFärg(in.at(8, 8), kGrön) && ärFärg(in.at(52, 52), kBlå) &&
              ärFärg(in.at(32, 0), kSvart),
          "B4 scenen i en frame: djupets ordning och UI-remsan",
          px(in.at(20, 20)) + " / " + px(in.at(36, 36)) + " / " +
              px(in.at(8, 8)));

    bool                      bundenLästes = true;
    std::vector<std::uint8_t> tmp;
    check(r.beginOffscreen(), "B4 beginOffscreen öppnar ett block utanför en "
                              "frame");
    r.pushTarget(*tUt, kVit);
    bundenLästes = r.readTarget(*tUt, tmp);   // bundet — ska vägras
    r.popTarget();
    scen.rita(r, *tUt);
    r.endOffscreen();
    r.endOffscreen();   // ett andra endOffscreen loggas och gör ingenting
    check(!bundenLästes, "B4 ett BUNDET mål läses inte (readTarget svarar "
                         "false)");
    const Bild ut = läs(r, *tUt, "B4 utanför en frame");
    check(störstaSkillnad(in, ut) <= 1,
          "B4 utanför en frame: samma pixlar som i en frame (±1)",
          "största skillnad " + std::to_string(störstaSkillnad(in, ut)));

    // Ett NYTT fönster som ALDRIG ritat — egen renderare, egna buffertar och
    // mål, och inget UI-tillstånd bundet förrän blocket binder det. D3D11 sätter
    // storleken innan renderaren finns och ritar inget i init vid någon
    // skala; användarskalan sätts ändå så att det nya fönstrets skala blir 1
    // — 1/os eller en float-granne — och tillbaka så fort fönstret finns
    // (blocket ritar i målets egna px), så fallet inte vilar på den ordningen.
    // Att fönstret aldrig ritat är ett KRAV, och det mäts nedan.
    const float förr = gfx::userScale();
    const float os   = f.window().uiScale() / förr;
    float       u    = 1.0f / os;
    for (int i = 0; i < 4 && os * u != 1.0f; ++i)
        u = std::nextafter(u, os * u > 1.0f ? 0.0f : 2.0f);
    gfx::setUserScale(u);
    Fönster ny;
    ny.setOffscreen(true);
    ny.show();
    const float nySkala = ny.isOpen() ? ny.window().uiScale() : 0.0f;
    gfx::setUserScale(förr);
    check(ny.isOpen(), "B4 det nya dolda fönstret öppnades");
    if (!ny.isOpen())
        return;
    check(!ny.window().retainedValid(),
          "B4 det nya fönstret har aldrig ritat — blocket är det första som "
          "binder UI-tillståndet",
          "skala " + std::to_string(nySkala));
    {
        gfx::Renderer& r2 = ny.window().renderer();
        const B4Scen   scen2(r2);
        auto           tNy = mål(r2, 64, 64, r2.supportedSamples(4), true);
        check(r2.beginOffscreen(), "B4 beginOffscreen i det nya fönstret");
        scen2.rita(r2, *tNy);
        r2.endOffscreen();
        const Bild nyB = läs(r2, *tNy, "B4 nytt fönster");
        check(störstaSkillnad(in, nyB) <= 1,
              "B4 ett nytt fönster utan grindens frames: samma pixlar (±1)",
              "största skillnad " + std::to_string(störstaSkillnad(in, nyB)));
    }
}

// ------------------------------------------------------------------- B5
void fallB5(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    const int      s4 = r.supportedSamples(4);
    const Form     röd = fillRuta(r, 16, 16, 48, 48, kRöd);
    auto           t   = mål(r, 64, 64, s4, true);
    // Nästlat: A (MSAA) lämnas för B, som ritar A:s bild; tillbaka i A ritas
    // en blå ruta över HELA målet på z 0,6 — vänster halva (röd, z 0,5) står
    // kvar bara om A:s djup överlevde utflykten, höger halva blir blå.
    const Form vänster = fillRuta(r, 0, 0, 32, 64, kRöd);
    const Form heltBlå = fillRuta(r, 0, 0, 64, 64, kBlå);
    auto       a       = mål(r, 64, 64, s4, true);
    auto       b       = mål(r, 64, 64, 1, false);
    gfx::Rectf dDraw{}, dProg{}, dB{}, dA{};
    std::unique_ptr<Form> platta;   // Image-programmet i fönstret
    const Bild fönster = frame(f, [&](gfx::Renderer& rr, const gfx::Rectf& box) {
        const float x0 = std::ceil(box.x), y0 = std::ceil(box.y);
        dDraw = gfx::Rectf{x0 + 10, y0 + 10, 64, 64};
        dProg = gfx::Rectf{x0 + 90, y0 + 10, 64, 64};
        dB    = gfx::Rectf{x0 + 10, y0 + 90, 64, 64};
        dA    = gfx::Rectf{x0 + 90, y0 + 90, 64, 64};
        rr.pushTarget(*t, kGenomskinlig);
        rita(rr, röd, 0.5, läge(gfx::DepthTest::Less));
        rr.fillRect(gfx::Rectf{0, 56, 64, 8}, kBlå);
        rr.popTarget();
        rr.drawImage(t->image(), dDraw);
        if (!platta)
            platta = std::make_unique<Form>(bildRuta(
                rr, dProg.x, dProg.y, dProg.x + 64, dProg.y + 64, t->image()));
        rita(rr, *platta, 0.0);

        rr.pushTarget(*a, kGenomskinlig);
        rita(rr, vänster, 0.5, läge(gfx::DepthTest::Less));
        rr.pushTarget(*b, kVit);
        rr.drawImage(a->image(), gfx::Rectf{0, 0, 64, 64});
        rr.popTarget();
        rita(rr, heltBlå, 0.6, läge(gfx::DepthTest::Less));
        rr.popTarget();
        rr.drawImage(b->image(), dB);
        rr.drawImage(a->image(), dA);
    });
    if (g_ritfel)
        return;
    for (const auto& [d, väg] :
         {std::pair{dDraw, "drawImage"}, std::pair{dProg, "Image-programmet"}}) {
        const int x = static_cast<int>(d.x), y = static_cast<int>(d.y);
        check(ärFärg(fönster.at(x + 32, y + 32), kRöd) &&
                  ärFärg(fönster.at(x + 32, y + 60), kBlå) &&
                  ärFärg(fönster.at(x + 4, y + 4), kVit),
              std::string("B5 MSAA-målets bild stämmer direkt efter popTarget (") +
                  väg + ")",
              px(fönster.at(x + 32, y + 32)) + " / " +
                  px(fönster.at(x + 32, y + 60)) + " / " +
                  px(fönster.at(x + 4, y + 4)));
    }
    const int bx = static_cast<int>(dB.x), by = static_cast<int>(dB.y);
    const int ax = static_cast<int>(dA.x), ay = static_cast<int>(dA.y);
    check(ärFärg(fönster.at(bx + 16, by + 32), kRöd) &&
              ärFärg(fönster.at(bx + 48, by + 32), kVit),
          "B5 nästlat: målet som lämnades är upplöst i det nästlade (vänster "
          "röd, ingen blå än)",
          px(fönster.at(bx + 16, by + 32)) + " / " +
              px(fönster.at(bx + 48, by + 32)));
    check(ärFärg(fönster.at(ax + 16, ay + 32), kRöd) &&
              ärFärg(fönster.at(ax + 48, ay + 32), kBlå),
          "B5 nästlat: tillbaka i målet står färgen och DJUPET kvar (vänster "
          "röd, höger blå)",
          px(fönster.at(ax + 16, ay + 32)) + " / " +
              px(fönster.at(ax + 48, ay + 32)));
}

// ------------------------------------------------------------------- P
//
// En riktig, avkodningsbar JPEG: 1 × 1 grått (128) i baslinje-Huffman med
// standardtabellerna, skapad med libjpeg-turbos cjpeg. WIC och Image I/O
// avkodar den — utan PNG-provet hade P1 alltså fallit.
const std::uint8_t kJpeg[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
    0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
    0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04,
    0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0a, 0x07,
    0x07, 0x06, 0x08, 0x0c, 0x0a, 0x0c, 0x0c, 0x0b, 0x0a, 0x0b, 0x0b, 0x0d,
    0x0e, 0x12, 0x10, 0x0d, 0x0e, 0x11, 0x0e, 0x0b, 0x0b, 0x10, 0x16, 0x10,
    0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0c, 0x0f, 0x17, 0x18, 0x16, 0x14,
    0x18, 0x12, 0x14, 0x15, 0x14, 0xff, 0xc0, 0x00, 0x0b, 0x08, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00, 0x08, 0x01, 0x01,
    0x00, 0x00, 0x3f, 0x00, 0x2b, 0xff, 0xd9};

std::string utf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.c_str()), s.size());
}

std::vector<std::uint8_t> läsFil(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

void fallP(Fönster& f)
{
    gfx::Renderer&              r = f.window().renderer();
    const std::filesystem::path tmp = std::filesystem::temp_directory_path();
    const std::filesystem::path jpgFil = tmp / "scene3d_pixel_gate_p.jpg";
    const std::filesystem::path pngFil = tmp / "scene3d_pixel_gate_p.png";

    // P1 — JPEG:en, i alla tre vägarna.
    std::vector<std::uint8_t> px1;
    int                       w = 0, h = 0;
    check(kJpeg[0] == 0xFF && kJpeg[1] == 0xD8 && kJpeg[2] == 0xFF &&
              !gfx::decodeImage(kJpeg, sizeof kJpeg, px1, w, h) &&
              px1.empty() && w == 0 && h == 0,
          "P1 en riktig JPEG (FF D8 FF …) avkodas INTE — decodeImage svarar "
          "false och lämnar utdata orörda");
    check(!r.loadImage(kJpeg, sizeof kJpeg)->valid(),
          "P1 loadImage ur minnet: ingen bild ur JPEG");
    {
        std::ofstream ut(jpgFil, std::ios::binary);
        ut.write(reinterpret_cast<const char*>(kJpeg), sizeof kJpeg);
    }
    check(!r.loadImage(utf8(jpgFil))->valid(),
          "P1 loadImage ur fil: ingen bild ur JPEG");

    // P2 — en PNG ur encodePng, pixel för pixel.
    const std::vector<std::uint8_t> källa = {255, 0,   0,   255, 0,   200, 0,
                                             255, 0,   0,   255, 255, 255, 255,
                                             255, 128};
    const bool skrevs = gfx::encodePng(utf8(pngFil), källa, 2, 2);
    const std::vector<std::uint8_t> png = läsFil(pngFil);
    std::vector<std::uint8_t>       px2;
    w = h = 0;
    const bool avkodad = gfx::decodeImage(png.data(), png.size(), px2, w, h);
    check(skrevs && avkodad && w == 2 && h == 2 && px2 == källa,
          "P2 en PNG (encodePng) avkodas pixel för pixel");
    auto ur = r.loadImage(utf8(pngFil));
    check(ur->valid() && ur->width() == 2 && ur->height() == 2 &&
              r.loadImage(png.data(), png.size())->valid(),
          "P2 loadImage ur fil och ur minnet: PNG:en blir en bild");

    // P3 — signaturen räcker inte: en avhuggen PNG ger false.
    std::vector<std::uint8_t> px3;
    check(png.size() > 24 && !gfx::decodeImage(png.data(), 24, px3, w, h),
          "P3 en avhuggen PNG (signaturen men inte bilden) ger false");

    std::error_code ec;
    std::filesystem::remove(jpgFil, ec);
    std::filesystem::remove(pngFil, ec);
}

// ------------------------------------------------------------------- K1
#ifdef _WIN32
// DXGI:s egen ordning för en preferens — frågan ställs till DXGI i samma
// process, oberoende av backenden. Utan IDXGIFactory6 gäller uppräkningen.
std::string dxgiAdapter(DXGI_GPU_PREFERENCE pref)
{
    IDXGIFactory1* f1 = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void**>(&f1))))
        return "?";
    IDXGIAdapter1* a  = nullptr;
    IDXGIFactory6* f6 = nullptr;
    if (pref != DXGI_GPU_PREFERENCE_UNSPECIFIED &&
        SUCCEEDED(f1->QueryInterface(__uuidof(IDXGIFactory6),
                                     reinterpret_cast<void**>(&f6)))) {
        f6->EnumAdapterByGpuPreference(0, pref, __uuidof(IDXGIAdapter1),
                                       reinterpret_cast<void**>(&a));
        f6->Release();
    } else {
        f1->EnumAdapters1(0, &a);
    }
    std::string ut = "?";
    if (a) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        char namn[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, namn, sizeof namn,
                            nullptr, nullptr);
        ut = namn;
        a->Release();
    }
    f1->Release();
    return ut;
}
#endif

std::string enhetFör(gfx::GpuPreference p)
{
    gfx::setGpuPreference(p);
    Fönster g;
    g.setOffscreen(true);
    g.show();
    const std::string namn = g.isOpen() ? g.window().renderer().deviceName()
                                        : std::string();
    gfx::setGpuPreference(gfx::GpuPreference::Default);
    return namn;
}

void fallK1(Fönster& f)
{
    const std::string förval = f.window().renderer().deviceName();
    const std::string hög    = enhetFör(gfx::GpuPreference::HighPerformance);
    const std::string låg    = enhetFör(gfx::GpuPreference::LowPower);
    std::printf("  info gpu  Default: %s · HighPerformance: %s · LowPower: %s\n",
                förval.c_str(), hög.c_str(), låg.c_str());
    check(!förval.empty() && !hög.empty() && !låg.empty(),
          "K1 renderaren säger vilken GPU den ritar med (deviceName), för "
          "varje preferens");
#ifdef _WIN32
    const std::string dx0   = dxgiAdapter(DXGI_GPU_PREFERENCE_UNSPECIFIED);
    const std::string dxHög = dxgiAdapter(DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE);
    const std::string dxLåg = dxgiAdapter(DXGI_GPU_PREFERENCE_MINIMUM_POWER);
    check(förval == dx0, "K1 Default: DXGI:s första adapter, som förut",
          förval + " mot " + dx0);
    check(hög == dxHög,
          "K1 HighPerformance: DXGI:s första adapter i prestandaordning",
          hög + " mot " + dxHög);
    check(låg == dxLåg, "K1 LowPower: DXGI:s första adapter i strömordning",
          låg + " mot " + dxLåg);
#endif
}

// ---------------------------------------------------------------- K2–K4

// Kortet ett nytt dolt fönster ritar med när `id` är begärt: {adapterId,
// deviceName}, eller nullopt när fönstret inte öppnades.
std::optional<std::pair<std::string, std::string>> kortFör(const std::string& id)
{
    gfx::setGpuAdapter(id);
    Fönster g;
    g.setOffscreen(true);
    g.show();
    std::optional<std::pair<std::string, std::string>> ut;
    if (g.isOpen())
        ut = std::make_pair(g.window().renderer().adapterId(),
                            g.window().renderer().deviceName());
    gfx::setGpuAdapter("");
    return ut;
}

bool iListan(const std::vector<gfx::GpuAdapter>& kort, const std::string& id)
{
    return std::any_of(kort.begin(), kort.end(),
                       [&](const gfx::GpuAdapter& a) { return a.id == id; });
}

// MJUKVARURENDERING avgjord ur enhetens NAMN — oberoende av adapterId, så ett
// tomt id på riktig hårdvara aldrig kan gömma sig bakom en SKIP: llvmpipe,
// softpipe och lavapipe (Mesa), WARP ("Microsoft Basic Render Driver") och
// SwiftShader.
bool mjukvara(const std::string& enhet)
{
    for (const char* n : {"llvmpipe", "softpipe", "lavapipe", "Basic Render",
                          "WARP", "SwiftShader"})
        if (enhet.find(n) != std::string::npos)
            return true;
    return false;
}

// Ett TOMT adapterId: SKIP bara när mjukvaran är konstaterad ur namnet,
// annars FEL. Svaret är om id:t går att mäta vidare.
bool idFinns(const std::string& fall, const std::string& id,
             const std::string& enhet)
{
    if (!id.empty())
        return true;
    if (mjukvara(enhet))
        std::printf("  SKIP %s: adapterId är tomt (%s — mjukvarurendering)\n",
                    fall.c_str(), enhet.c_str());
    else
        check(false, fall + ": adapterId är inte tomt på hårdvara", enhet);
    return false;
}

void fallK2(Fönster& f, const std::vector<gfx::GpuAdapter>& kort)
{
    const std::string& enhet = f.window().renderer().deviceName();
    // Rent mjukvarufall (llvmpipe utan DRM-kort): ingen lista att ställa krav
    // på — avgjort ur enhetens namn, inte ur adapterId.
    if (kort.empty() && mjukvara(enhet)) {
        std::printf("  SKIP K2 mjukvarurendering (%s) utan grafikkort: "
                    "listkraven mäts inte\n",
                    enhet.c_str());
        return;
    }
    int         förval = 0;
    std::string förvalsId, alla;
    bool        unika = true;
    for (std::size_t i = 0; i < kort.size(); ++i) {
        alla += (i ? " · " : "") + kort[i].id;
        if (kort[i].isDefault) {
            ++förval;
            förvalsId = kort[i].id;
        }
        unika = unika && !kort[i].id.empty();
        for (std::size_t j = 0; j < i; ++j)
            unika = unika && kort[j].id != kort[i].id;
    }
    std::printf("  info kort %s\n", alla.c_str());
    check(!kort.empty(), "K2 gpuAdapters() är inte tom");
    check(förval == 1, "K2 exakt ett kort är isDefault", std::to_string(förval));
    check(unika, "K2 id:na är unika och icke-tomma", alla);
    const std::string& id = f.window().renderer().adapterId();
    if (idFinns("K2 Default-fönstret", id, enhet)) {
        check(iListan(kort, id), "K2 Default-fönstrets adapterId står i listan", id);
#if defined(GLAZE_BACKEND_D3D11)
        // FASTSTÄLLT bara här: DXGI:s index 0 är det D3D11CreateDevice(nullptr)
        // tar. Linux (boot_vga) och macOS (Apples regel) uppskattar förvalet.
        check(id == förvalsId, "K2 Default ritar med förvalskortet (DXGI:s första)",
              id + " mot " + förvalsId);
#endif
    }
#ifdef _WIN32
    UINT           antal = 0;
    IDXGIFactory1* f1    = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                     reinterpret_cast<void**>(&f1)))) {
        IDXGIAdapter1* a = nullptr;
        while (SUCCEEDED(f1->EnumAdapters1(antal, &a))) {
            a->Release();
            ++antal;
        }
        f1->Release();
    }
    check(kort.size() == antal, "K2 lika många kort som EnumAdapters1",
          std::to_string(kort.size()) + " mot " + std::to_string(antal));
#endif
}

void fallK3(const std::vector<gfx::GpuAdapter>& kort)
{
    for (const gfx::GpuAdapter& a : kort) {
        if (!a.selectable)
            continue;
        const auto fick = kortFör(a.id);
        check(fick.has_value(), "K3 " + a.id + ": fönstret öppnas");
        if (!fick || !idFinns("K3 " + a.id, fick->first, fick->second))
            continue;
#if defined(GLAZE_BACKEND_LINUX)
        check(iListan(kort, fick->first),
              "K3 " + a.id + ": adapterId står i listan (valet gäller bara vid "
              "init på Linux)",
              fick->first);
#else
        check(fick->first == a.id && fick->second == a.name,
              "K3 " + a.id + ": fönstret ritar med kortet",
              fick->first + " (" + fick->second + ")");
#endif
    }
}

void fallK4(Fönster& f)
{
    const auto fick = kortFör("pci:ffff:ffff:00000000:00");
    check(fick.has_value(), "K4 ett okänt id: fönstret öppnas ändå");
    // Två TOMMA id hade varit lika utan att mäta något — det tomma prövas
    // först, med samma regel som K2/K3.
    if (fick && idFinns("K4", fick->first, fick->second))
        check(fick->first == f.window().renderer().adapterId(),
              "K4 ett okänt id: preferensens kort",
              fick->first + " mot " + f.window().renderer().adapterId());
}

// ================================================================== DEL C
//
// EGNA PROGRAM. Grinden skriver varje program i ALLA TRE språken —
// HLSL, GLSL och MSL — och backenden kompilerar sitt (MSL prövas på
// macOS). Förväntat värde är grindens egna tal: färgerna den skickar, lägena den
// väljer och (C9) vyn den själv byggde.

#if defined(GLAZE_BACKEND_D3D11)
const char* const kSpråk = "HLSL";
#elif defined(GLAZE_BACKEND_METAL)
const char* const kSpråk = "MSL";
#else
const char* const kSpråk = "GLSL";
#endif

struct Källor {
    const char* hlsl;
    const char* glslVs;
    const char* glslFs;
    const char* msl;
};

std::unique_ptr<gfx::ShaderProgram>
program(gfx::Renderer& r, const char* namn, const Källor& k,
        std::uint32_t userBytes = 0, std::uint8_t texturer = 0,
        std::vector<std::pair<std::string, std::string>> defs = {})
{
    gfx::ProgramDesc d;
    d.name         = namn;
    d.hlsl         = k.hlsl;
    d.glslVertex   = k.glslVs;
    d.glslFragment = k.glslFs;
    d.msl          = k.msl;
    d.userBytes    = userBytes;
    d.textures     = texturer;
    d.defines      = std::move(defs);
    return r.createProgram(d);
}

// Pixelklippet rakt av — identiteten: chunk-lokala (x, y, z) är målets px
// och djupet z (w = 1). glaze_toClip i förspelet gör resten.
gfx::FrameUniforms pxRam()
{
    gfx::FrameUniforms f{};
    gfx::Mat4d::identity().toFloat(f.viewProj);
    gfx::Mat4d::identity().toFloat(f.invViewProj);
    return f;
}

// EGET FORMAT (C1): läge float3 och färg unorm8×4 — 16 byte, färgen på
// platsen kColor (2), så att platsen 1 står tom mellan dem.
struct PosFärg {
    float      x, y, z;
    gfx::Color c;
};
static_assert(sizeof(PosFärg) == 16, "grindens eget format");

gfx::VertexLayout posFärgLayout()
{
    gfx::VertexLayout l;
    l.attribs   = {{gfx::attrib::kPosition, gfx::AttribFormat::Float3, 0, 0},
                   {gfx::attrib::kColor, gfx::AttribFormat::UNorm8x4, 12, 0}};
    l.stride[0] = sizeof(PosFärg);
    return l;
}

// Bara ett läge float2 på plats 0 (C2, C3).
gfx::VertexLayout pos2Layout()
{
    gfx::VertexLayout l;
    l.attribs   = {{gfx::attrib::kPosition, gfx::AttribFormat::Float2, 0, 0}};
    l.stride[0] = 8;
    return l;
}

// INSTANSERNA (C4): hörnet float2 i ström 0, instansens läge float2 och
// färg unorm8×4 i ström 1 (12 byte per instans) på platserna kInstance0/+1.
gfx::VertexLayout instansLayout()
{
    gfx::VertexLayout l;
    l.attribs   = {{gfx::attrib::kPosition, gfx::AttribFormat::Float2, 0, 0},
                   {gfx::attrib::kInstance0, gfx::AttribFormat::Float2, 0, 1},
                   {static_cast<std::uint8_t>(gfx::attrib::kInstance0 + 1),
                    gfx::AttribFormat::UNorm8x4, 8, 1}};
    l.stride[0] = 8;
    l.stride[1] = 12;
    return l;
}

struct Instans {
    float      x, y;
    gfx::Color c;
};
static_assert(sizeof(Instans) == 12, "grindens instansformat");

// En ström på GPU:n: vertexbufferten och (indexerat) indexbufferten.
struct Ström {
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;   // index, eller vertex utan
};

template <class V>
Ström ström(gfx::Renderer& r, const std::vector<V>& v,
            const std::vector<std::uint32_t>& idx = {})
{
    Ström s;
    s.vb = r.createBuffer(gfx::BufferKind::Vertex, v.size() * sizeof(V),
                          v.data());
    if (!idx.empty())
        s.ib = r.createBuffer(gfx::BufferKind::Index,
                              idx.size() * sizeof(std::uint32_t), idx.data());
    s.count = static_cast<std::uint32_t>(idx.empty() ? v.size() : idx.size());
    return s;
}

// En rektangel på djupet z: fyra hörn, trianglarna 0-1-2 och 1-3-2.
Ström ruta(gfx::Renderer& r, float x0, float y0, float x1, float y1, float z,
           gfx::Color c)
{
    const std::vector<PosFärg> v = {
        {x0, y0, z, c}, {x1, y0, z, c}, {x0, y1, z, c}, {x1, y1, z, c}};
    return ström(r, v, {0, 1, 2, 1, 3, 2});
}

// Ett tomt Custom-anrop: programmet, layouten och pixelklippet.
gfx::MeshDraw custom(const gfx::ShaderProgram& p, const gfx::VertexLayout& l)
{
    gfx::MeshDraw d;
    d.program = gfx::MeshProgram::Custom;
    d.shader  = &p;
    d.layout  = &l;
    d.frame   = pxRam();
    return d;
}

void ritaRuta(gfx::Renderer& r, const gfx::ShaderProgram& p,
              const gfx::VertexLayout& l, const Ström& s,
              const gfx::RenderState& st = {})
{
    gfx::MeshDraw d = custom(p, l);
    d.vertices      = gfx::slice(*s.vb);
    d.indices       = gfx::slice(*s.ib);
    d.indexCount    = s.count;
    d.state         = st;
    r.drawMesh(d);
}

// Helskärmspasset: ingen ström alls, tre vertex ur vertexindexet.
void ritaHelskärm(gfx::Renderer& r, const gfx::ShaderProgram& p)
{
    static const gfx::VertexLayout tom;
    gfx::MeshDraw                  d = custom(p, tom);
    d.vertexCount                    = 3;
    r.drawMesh(d);
}

bool allaPixlar(const Bild& b, gfx::Color c)
{
    if (b.px.empty())
        return false;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            if (!ärFärg(b.at(x, y), c))
                return false;
    return true;
}

// ------------------------------------------------------------- källorna

// LÄGE + FÄRG (C1, C5, C11, C12): läget genom glaze.viewProj och
// glaze_toClip i vertexsteget, färgen rakt igenom.
const Källor kFärg = {
    R"hlsl(
struct VsIn  { float3 pos : ATTR0; float4 col : ATTR2; };
struct VsOut { float4 pos : SV_Position; float4 col : COLOR0; };
VsOut vsMain(VsIn i)
{
    VsOut o;
    o.pos = glaze_toClip(mul(glaze.viewProj, float4(i.pos, 1.0)));
    o.col = i.col;
    return o;
}
float4 fsMain(VsOut i) : SV_Target { return i.col; }
)hlsl",
    R"glsl(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 aCol;
out vec4 vCol;
void main()
{
    gl_Position = glaze_toClip(glaze.viewProj * vec4(aPos, 1.0));
    vCol        = aCol;
}
)glsl",
    R"glsl(
in vec4 vCol;
out vec4 frag;
void main() { frag = vCol; }
)glsl",
    R"msl(
struct VsIn  { float3 pos [[attribute(0)]]; float4 col [[attribute(2)]]; };
struct VsOut { float4 pos [[position]]; float4 col; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos = glaze_toClip(glaze.viewProj * float4(i.pos, 1.0), glaze);
    o.col = i.col;
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]]) { return i.col; }
)msl"};

// HELTALEN (C1b): färgen som uint4 (UInt8x4) och ett id som uint (UInt) —
// heltal som heltal, också över 16 bitar. Fel id ger magenta.
const Källor kHeltal = {
    R"hlsl(
struct VsIn  { float3 pos : ATTR0; uint4 col : ATTR2; uint id : ATTR4; };
struct VsOut { float4 pos : SV_Position; float4 col : COLOR0; };
VsOut vsMain(VsIn i)
{
    VsOut o;
    o.pos = glaze_toClip(mul(glaze.viewProj, float4(i.pos, 1.0)));
    o.col = i.id == 70000u ? float4(i.col) / 255.0 : float4(1.0, 0.0, 1.0, 1.0);
    return o;
}
float4 fsMain(VsOut i) : SV_Target { return i.col; }
)hlsl",
    R"glsl(
layout(location = 0) in vec3  aPos;
layout(location = 2) in uvec4 aCol;
layout(location = 4) in uint  aId;
out vec4 vCol;
void main()
{
    gl_Position = glaze_toClip(glaze.viewProj * vec4(aPos, 1.0));
    vCol = aId == 70000u ? vec4(aCol) / 255.0 : vec4(1.0, 0.0, 1.0, 1.0);
}
)glsl",
    R"glsl(
in vec4 vCol;
out vec4 frag;
void main() { frag = vCol; }
)glsl",
    R"msl(
struct VsIn  { float3 pos [[attribute(0)]]; uint4 col [[attribute(2)]];
               uint id [[attribute(4)]]; };
struct VsOut { float4 pos [[position]]; float4 col; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos = glaze_toClip(glaze.viewProj * float4(i.pos, 1.0), glaze);
    o.col = i.id == 70000u ? float4(i.col) / 255.0 : float4(1.0, 0.0, 1.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]]) { return i.col; }
)msl"};

// GLAZEUSER (C2): två float4 — färgen är den ANDRA, så hela blocket måste
// komma fram på sina platser.
const Källor kUser = {
    R"hlsl(
cbuffer GlazeUser : register(b4) { float4 uFirst; float4 uColor; };
float4 vsMain(float2 pos : ATTR0) : SV_Position
{
    return glaze_toClip(mul(glaze.viewProj, float4(pos, 0.5, 1.0)));
}
float4 fsMain(float4 p : SV_Position) : SV_Target { return uColor; }
)hlsl",
    R"glsl(
layout(location = 0) in vec2 aPos;
void main() { gl_Position = glaze_toClip(glaze.viewProj * vec4(aPos, 0.5, 1.0)); }
)glsl",
    R"glsl(
layout(std140) uniform GlazeUser { vec4 uFirst; vec4 uColor; };
out vec4 frag;
void main() { frag = uColor; }
)glsl",
    R"msl(
struct User  { float4 first; float4 color; };
struct VsIn  { float2 pos [[attribute(0)]]; };
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos = glaze_toClip(glaze.viewProj * float4(i.pos, 0.5, 1.0), glaze);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]], constant User& user [[buffer(3)]])
{
    return user.color;
}
)msl"};

// TEXTUREN PÅ PLATS 2 (C3): samplar (0,75, 0,25) — övre högra texeln i en
// 2 × 2-bild.
const Källor kTex = {
    R"hlsl(
Texture2D    t2 : register(t2);
SamplerState s2 : register(s2);
float4 vsMain(float2 pos : ATTR0) : SV_Position
{
    return glaze_toClip(mul(glaze.viewProj, float4(pos, 0.5, 1.0)));
}
float4 fsMain(float4 p : SV_Position) : SV_Target
{
    return t2.Sample(s2, float2(0.75, 0.25));
}
)hlsl",
    R"glsl(
layout(location = 0) in vec2 aPos;
void main() { gl_Position = glaze_toClip(glaze.viewProj * vec4(aPos, 0.5, 1.0)); }
)glsl",
    R"glsl(
uniform sampler2D GlazeTexture2;
out vec4 frag;
void main() { frag = texture(GlazeTexture2, vec2(0.75, 0.25)); }
)glsl",
    R"msl(
struct VsIn  { float2 pos [[attribute(0)]]; };
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos = glaze_toClip(glaze.viewProj * float4(i.pos, 0.5, 1.0), glaze);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]], texture2d<float> t2 [[texture(2)]],
                       sampler s2 [[sampler(2)]])
{
    return t2.sample(s2, float2(0.75, 0.25));
}
)msl"};

// INSTANSERNA (C4, C5b): hörnet + instansens läge, instansens färg.
const Källor kInst = {
    R"hlsl(
struct VsIn  { float2 corner : ATTR0; float2 at : ATTR9; float4 col : ATTR10; };
struct VsOut { float4 pos : SV_Position; float4 col : COLOR0; };
VsOut vsMain(VsIn i)
{
    VsOut o;
    o.pos = glaze_toClip(mul(glaze.viewProj, float4(i.corner + i.at, 0.5, 1.0)));
    o.col = i.col;
    return o;
}
float4 fsMain(VsOut i) : SV_Target { return i.col; }
)hlsl",
    R"glsl(
layout(location = 0)  in vec2 aCorner;
layout(location = 9)  in vec2 aAt;
layout(location = 10) in vec4 aCol;
out vec4 vCol;
void main()
{
    gl_Position = glaze_toClip(glaze.viewProj * vec4(aCorner + aAt, 0.5, 1.0));
    vCol        = aCol;
}
)glsl",
    R"glsl(
in vec4 vCol;
out vec4 frag;
void main() { frag = vCol; }
)glsl",
    R"msl(
struct VsIn  { float2 corner [[attribute(0)]]; float2 at [[attribute(9)]];
               float4 col [[attribute(10)]]; };
struct VsOut { float4 pos [[position]]; float4 col; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos = glaze_toClip(glaze.viewProj * float4(i.corner + i.at, 0.5, 1.0), glaze);
    o.col = i.col;
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]]) { return i.col; }
)msl"};

// HELSKÄRMSTRIANGELN (C6): tre vertex ur vertexindexet, ingen ström.
const Källor kHelskärm = {
    R"hlsl(
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 fsMain(float4 p : SV_Position) : SV_Target
{
    return float4(0.2, 0.4, 0.6, 1.0);
}
)hlsl",
    R"glsl(
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)glsl",
    R"glsl(
out vec4 frag;
void main() { frag = vec4(0.2, 0.4, 0.6, 1.0); }
)glsl",
    R"msl(
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]]) { return float4(0.2, 0.4, 0.6, 1.0); }
)msl"};

// SYNTAXFELET PÅ APPENS RAD 7 (C7) — raderna räknade för hand, och felet i
// fragmentsteget i GLSL (vertexsteget är giltigt).
const Källor kRadSju = {
    "// rad 1\n"
    "// rad 2\n"
    "float4 vsMain(uint id : SV_VertexID) : SV_Position\n"
    "{\n"
    "    return float4(0.0, 0.0, 0.0, 1.0);\n"
    "}\n"
    "float4 fsMain(float4 p : SV_Position) : SV_Target { float4 x = ; return x; }\n",
    "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n",
    "// rad 1\n"
    "// rad 2\n"
    "// rad 3\n"
    "// rad 4\n"
    "out vec4 frag;\n"
    "void main()\n"
    "{ vec4 x = ; frag = x; }\n",
    "// rad 1\n"
    "struct VsOut { float4 pos [[position]]; };\n"
    "vertex VsOut vsMain(uint id [[vertex_id]])\n"
    "{\n"
    "    VsOut o; o.pos = float4(0.0, 0.0, 0.0, 1.0); return o;\n"
    "}\n"
    "fragment float4 fsMain(VsOut i [[stage_in]]) { float4 x = ; return x; }\n"};

// GLAZEFRAME (C9): helskärmen färgkodar glaze.forward i fragmentsteget.
const Källor kFram = {
    R"hlsl(
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 fsMain(float4 p : SV_Position) : SV_Target
{
    return float4(glaze.forward.xyz * 0.5 + 0.5, 1.0);
}
)hlsl",
    R"glsl(
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)glsl",
    R"glsl(
out vec4 frag;
void main() { frag = vec4(glaze.forward.xyz * 0.5 + 0.5, 1.0); }
)glsl",
    R"msl(
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]])
{
    return float4(glaze.forward.xyz * 0.5 + 0.5, 1.0);
}
)msl"};

// GLAZEFRAMES MÅL (C9): `target` som backenden skriver — (w, h)/255 och
// w · (1/w).
const Källor kMål = {
    R"hlsl(
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 fsMain(float4 p : SV_Position) : SV_Target
{
    return float4(glaze.target.x / 255.0, glaze.target.y / 255.0,
                  glaze.target.x * glaze.target.z, 1.0);
}
)hlsl",
    R"glsl(
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)glsl",
    R"glsl(
out vec4 frag;
void main()
{
    frag = vec4(glaze.target.x / 255.0, glaze.target.y / 255.0,
                glaze.target.x * glaze.target.z, 1.0);
}
)glsl",
    R"msl(
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]])
{
    return float4(glaze.target.x / 255.0, glaze.target.y / 255.0,
                  glaze.target.x * glaze.target.z, 1.0);
}
)msl"};

// DEFINITIONERNA (C10): GATE_GREEN väljer grenen och GATE_LEVEL värdet.
const Källor kDefs = {
    R"hlsl(
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 fsMain(float4 p : SV_Position) : SV_Target
{
#ifdef GATE_GREEN
    return float4(0.0, GATE_LEVEL, 0.0, 1.0);
#else
    return float4(1.0, 0.0, 0.0, 1.0);
#endif
}
)hlsl",
    R"glsl(
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)glsl",
    R"glsl(
out vec4 frag;
void main()
{
#ifdef GATE_GREEN
    frag = vec4(0.0, GATE_LEVEL, 0.0, 1.0);
#else
    frag = vec4(1.0, 0.0, 0.0, 1.0);
#endif
}
)glsl",
    R"msl(
struct VsOut { float4 pos [[position]]; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]])
{
#ifdef GATE_GREEN
    return float4(0.0, GATE_LEVEL, 0.0, 1.0);
#else
    return float4(1.0, 0.0, 0.0, 1.0);
#endif
}
)msl"};

// Källorna med BARA den här backendens källa tom — resten står kvar.
Källor utanEgen(Källor k)
{
#if defined(GLAZE_BACKEND_D3D11)
    k.hlsl = "";
#elif defined(GLAZE_BACKEND_METAL)
    k.msl = "";
#else
    k.glslFs = "";
#endif
    return k;
}

// Källorna med BARA den här backendens källa — de andra tomma.
Källor baraEgen(const Källor& k)
{
#if defined(GLAZE_BACKEND_D3D11)
    return Källor{k.hlsl, "", "", ""};
#elif defined(GLAZE_BACKEND_METAL)
    return Källor{"", "", "", k.msl};
#else
    return Källor{"", k.glslVs, k.glslFs, ""};
#endif
}

// Felet anger appens rad 7 i kompilatorns egen form — och INTE raden med
// förspelet inräknat (C7-sabotaget: `#line 1` borta ger t.ex. rad 32).
bool radSju(const std::string& fel)
{
#if defined(GLAZE_BACKEND_D3D11)
    return fel.find("(7,") != std::string::npos;    // "rad_sju(7,57): error"
#elif defined(GLAZE_BACKEND_METAL)
    return fel.find(":7:") != std::string::npos;    // "program_source:7:54:"
#else
    return fel.find(":7(") != std::string::npos ||  // Mesa "0:7(12): error"
           fel.find("(7)") != std::string::npos;    // NVIDIA "0(7) : error"
#endif
}

std::string kort(const std::string& s)
{
    std::string ut = s.substr(0, 160);
    for (char& c : ut)
        if (c == '\n' || c == '\r')
            c = ' ';
    return ut;
}

// ------------------------------------------------------------------- C1
void fallC1(Fönster& f)
{
    gfx::Renderer&          r      = f.window().renderer();
    const std::uint32_t     byggda = r.stats().programsBuilt;
    auto                    p      = program(r, "c1_farg", kFärg);
    const gfx::VertexLayout l      = posFärgLayout();
    check(p->valid() && p->error().empty(),
          std::string("C1 programmet kompilerar (") + kSpråk + ")",
          kort(p->error()));
    check(r.stats().programsBuilt == byggda + 1,
          "C1 RenderStats::programsBuilt stegas för ett giltigt program");
    check(p->inputs() == ((1u << gfx::attrib::kPosition) |
                          (1u << gfx::attrib::kColor)) &&
              p->integerInputs() == 0,
          "C1 reflektionen: vertexsteget läser platserna 0 och 2, båda som "
          "flyttal",
          std::to_string(p->inputs()) + "/" +
              std::to_string(p->integerInputs()));
    if (!p->valid())
        return;
    const gfx::Color färg{200, 100, 50, 255};
    const Ström      s = ruta(r, 16, 16, 48, 48, 0.5f, färg);
    auto             t = mål(r, 64, 64, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        ritaRuta(rr, *p, l, s);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "C1");
    check(ärFärg(b.at(32, 32), färg) && ärFärg(b.at(17, 17), färg) &&
              ärFärg(b.at(46, 46), färg),
          "C1 eget format (läge float3 + färg unorm8×4 på plats 2): rätt färg "
          "där rutan ligger",
          px(b.at(32, 32)));
    check(ärFärg(b.at(8, 8), kVit) && ärFärg(b.at(56, 32), kVit),
          "C1 utanför rutan står målets färg", px(b.at(8, 8)));
}

// ------------------------------------------------------------------ C1b
//
// HELTALSATTRIBUTEN: färgen som UInt8x4 och ett id som UInt
// ska läsas som HELTAL — GL:s glVertexAttribIPointer
// och D3D11:s _UINT-format. Id:t är över 16 bitar.
struct PosFärgId {
    float         x, y, z;
    gfx::Color    c;
    std::uint32_t id;
};
static_assert(sizeof(PosFärgId) == 20, "grindens heltalsformat");

void fallC1b(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    auto           p = program(r, "c1b_heltal", kHeltal);
    check(p->valid(), "C1b programmet med heltalsattribut kompilerar",
          kort(p->error()));
    check(p->inputs() == 0b10101u && p->integerInputs() == 0b10100u,
          "C1b reflektionen: platserna 0, 2 och 4 — 2 och 4 som heltal",
          std::to_string(p->inputs()) + "/" +
              std::to_string(p->integerInputs()));
    if (!p->valid())
        return;
    gfx::VertexLayout l;
    l.attribs   = {{gfx::attrib::kPosition, gfx::AttribFormat::Float3, 0, 0},
                   {gfx::attrib::kColor, gfx::AttribFormat::UInt8x4, 12, 0},
                   {gfx::attrib::kScalar, gfx::AttribFormat::UInt, 16, 0}};
    l.stride[0] = sizeof(PosFärgId);
    const gfx::Color             färg{100, 150, 200, 255};
    const std::vector<PosFärgId> v = {{16, 16, 0.5f, färg, 70000},
                                      {48, 16, 0.5f, färg, 70000},
                                      {16, 48, 0.5f, färg, 70000},
                                      {48, 48, 0.5f, färg, 70000}};
    const Ström s = ström(r, v, {0, 1, 2, 1, 3, 2});
    auto        t = mål(r, 64, 64, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        ritaRuta(rr, *p, l, s);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "C1b");
    check(ärFärg(b.at(32, 32), färg),
          "C1b UInt8x4 och UInt läses som heltal — färgen rätt och id:t 70000 "
          "helt (annars magenta)",
          px(b.at(32, 32)));
}

// ------------------------------------------------------------------- C2
void fallC2(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    auto           p = program(r, "c2_user", kUser, 32);
    check(p->valid(), "C2 programmet med GlazeUser (32 byte) kompilerar",
          kort(p->error()));
    if (!p->valid())
        return;
    const gfx::VertexLayout     l = pos2Layout();
    const std::vector<float>    v = {16, 16, 48, 16, 16, 48, 48, 48};
    const Ström                 s = ström(r, v, {0, 1, 2, 1, 3, 2});
    // Två anrop i samma frame med var sin färg — blocket skrivs per anrop.
    const float a[8] = {1, 0, 1, 1, 0.2f, 0.6f, 0.8f, 1};
    const float b[8] = {1, 0, 1, 1, 0.9f, 0.3f, 0.1f, 1};
    auto        tA   = mål(r, 64, 64, 1, false);
    auto        tB   = mål(r, 64, 64, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        for (auto [t, u] : {std::pair{tA.get(), a}, std::pair{tB.get(), b}}) {
            rr.pushTarget(*t, kVit);
            gfx::MeshDraw d = custom(*p, l);
            d.vertices      = gfx::slice(*s.vb);
            d.indices       = gfx::slice(*s.ib);
            d.indexCount    = s.count;
            d.user          = u;
            d.userBytes     = 32;
            rr.drawMesh(d);
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    const Bild bA = läs(r, *tA, "C2 första");
    const Bild bB = läs(r, *tB, "C2 andra");
    check(ärFärg(bA.at(32, 32), gfx::Color{51, 153, 204, 255}),
          "C2 GlazeUser-färgen (blockets ANDRA float4) når fragmentet",
          px(bA.at(32, 32)));
    check(ärFärg(bB.at(32, 32), gfx::Color{230, 77, 26, 255}),
          "C2 nästa anrop i samma frame får sitt eget block",
          px(bB.at(32, 32)));
}

// ------------------------------------------------------------------- C3
void fallC3(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    auto           p = program(r, "c3_tex", kTex, 0, 3);
    check(p->valid(), "C3 programmet med tre texturer kompilerar",
          kort(p->error()));
    if (!p->valid())
        return;
    // Platserna 0 och 1 bär helt andra färger än plats 2 — en förväxling
    // syns. Plats 2 är 2 × 2: röd, grön / blå, vit (rad 0 överst).
    auto magenta = enfärgad(r, gfx::Color{255, 0, 255, 255});
    auto cyan    = enfärgad(r, gfx::Color{0, 255, 255, 255});
    const std::uint8_t fyra[16] = {255, 0, 0,   255, 0,   200, 0,   255,
                                   0,   0, 255, 255, 255, 255, 255, 255};
    auto tex2 = r.createImage(2, 2, gfx::ImageFormat::Rgba8, fyra, 0);
    tex2->setFilter(gfx::ImageFilter::Nearest);
    const gfx::VertexLayout  l = pos2Layout();
    const std::vector<float> v = {16, 16, 48, 16, 16, 48, 48, 48};
    const Ström              s = ström(r, v, {0, 1, 2, 1, 3, 2});
    auto                     t = mål(r, 64, 64, 1, false);
    // C3b: en UI-bild EFTER det egna programmet i samma mål — UI:t samplar
    // sin egen textur, inte den programmet lämnade på någon plats.
    auto gul = enfärgad(r, gfx::Color{255, 255, 0, 255});
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        gfx::MeshDraw d = custom(*p, l);
        d.vertices      = gfx::slice(*s.vb);
        d.indices       = gfx::slice(*s.ib);
        d.indexCount    = s.count;
        d.textures      = {magenta.get(), cyan.get(), tex2.get(), nullptr};
        rr.drawMesh(d);
        rr.drawImage(*gul, gfx::Rectf{0, 0, 8, 8});
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "C3");
    check(ärFärg(b.at(32, 32), kGrön),
          "C3 texturen på plats 2 samplas — övre högra texeln (grön), rad 0 "
          "överst",
          px(b.at(32, 32)));
    check(ärFärg(b.at(4, 4), gfx::Color{255, 255, 0, 255}),
          "C3b en UI-bild efter programmet ritas med sin egen textur",
          px(b.at(4, 4)));
}

// ------------------------------------------------------------- C4 och C4b
void fallC4(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    auto           p = program(r, "c4_inst", kInst);
    check(p->valid(), "C4 instansprogrammet kompilerar", kort(p->error()));
    if (!p->valid())
        return;
    const gfx::VertexLayout l = instansLayout();
    // Hörnen 8 × 8 och tre instanser på kända lägen.
    const std::vector<float>   hörn = {0, 0, 8, 0, 0, 8, 8, 8};
    const Ström                s    = ström(r, hörn, {0, 1, 2, 1, 3, 2});
    const std::vector<Instans> inst = {
        {4, 4, kRöd}, {24, 4, kGrön}, {44, 4, kBlå}};
    auto ib = r.createBuffer(gfx::BufferKind::Vertex,
                             inst.size() * sizeof(Instans), inst.data());
    auto t3 = mål(r, 64, 32, 1, false);
    auto t2 = mål(r, 64, 32, 1, false);
    gfx::RenderStats s3{}, s2{};
    iMål(f, [&](gfx::Renderer& rr) {
        gfx::MeshDraw d = custom(*p, l);
        d.vertices      = gfx::slice(*s.vb);
        d.indices       = gfx::slice(*s.ib);
        d.indexCount    = s.count;
        rr.pushTarget(*t3, kVit);
        d.instances     = gfx::slice(*ib);
        d.instanceCount = 3;
        rr.resetStats();
        rr.drawMesh(d);
        s3 = rr.stats();
        rr.popTarget();
        // C4b: delen börjar vid instans 1 och två ritas.
        rr.pushTarget(*t2, kVit);
        d.instances     = gfx::BufferSlice{ib.get(), sizeof(Instans),
                                       2 * sizeof(Instans)};
        d.instanceCount = 2;
        rr.resetStats();
        rr.drawMesh(d);
        s2 = rr.stats();
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b3 = läs(r, *t3, "C4");
    const Bild b2 = läs(r, *t2, "C4b");
    check(ärFärg(b3.at(8, 8), kRöd) && ärFärg(b3.at(28, 8), kGrön) &&
              ärFärg(b3.at(48, 8), kBlå),
          "C4 tre instanser på kända lägen, var och en i sin färg",
          px(b3.at(8, 8)) + " / " + px(b3.at(28, 8)) + " / " +
              px(b3.at(48, 8)));
    check(ärFärg(b3.at(18, 8), kVit) && ärFärg(b3.at(8, 20), kVit),
          "C4 mellan och under instanserna står målets färg",
          px(b3.at(18, 8)));
    check(s3.instances == 3 && s3.drawCalls == 1 && s3.vertices == 18,
          "C4 RenderStats: 3 instanser i ETT anrop, 6 index × 3 vertex",
          std::to_string(s3.instances) + " instanser, " +
              std::to_string(s3.drawCalls) + " anrop, " +
              std::to_string(s3.vertices) + " vertex");
    check(ärFärg(b2.at(8, 8), kVit) && ärFärg(b2.at(28, 8), kGrön) &&
              ärFärg(b2.at(48, 8), kBlå),
          "C4b delen börjar vid instans 1: instans 0 ritas inte, 1 och 2 där "
          "de ska",
          px(b2.at(8, 8)) + " / " + px(b2.at(28, 8)) + " / " +
              px(b2.at(48, 8)));
    check(s2.instances == 2, "C4b RenderStats: 2 instanser",
          std::to_string(s2.instances));
}

// ------------------------------------------------------------ C5 och C5b
void fallC5(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    auto           pf = program(r, "c5_farg", kFärg);
    auto           pi = program(r, "c5_inst", kInst);
    check(pf->valid() && pi->valid(), "C5 programmen kompilerar",
          kort(pf->error() + pi->error()));
    if (!pf->valid() || !pi->valid())
        return;
    // Tre vertex som skräp (en röd triangel över HELA målet) och sedan sex
    // som två trianglar — delen börjar efter skräpet.
    const gfx::Color           grön{0, 200, 0, 255};
    const std::vector<PosFärg> v = {
        {-100, -100, 0.5f, kRöd}, {300, -100, 0.5f, kRöd}, {-100, 300, 0.5f, kRöd},
        {16, 16, 0.5f, grön},     {48, 16, 0.5f, grön},    {16, 48, 0.5f, grön},
        {48, 16, 0.5f, grön},     {48, 48, 0.5f, grön},    {16, 48, 0.5f, grön}};
    auto vb = r.createBuffer(gfx::BufferKind::Vertex, v.size() * sizeof(PosFärg),
                             v.data());
    const gfx::VertexLayout lf = posFärgLayout();
    // C5b: hörnen som sex vertex utan index, två instanser.
    const std::vector<float>   hörn = {0, 0, 8, 0, 0, 8, 8, 0, 8, 8, 0, 8};
    auto                       hb   = r.createBuffer(gfx::BufferKind::Vertex,
                                                     hörn.size() * sizeof(float),
                                                     hörn.data());
    const std::vector<Instans> inst = {{4, 4, kRöd}, {24, 4, kBlå}};
    auto ib = r.createBuffer(gfx::BufferKind::Vertex,
                             inst.size() * sizeof(Instans), inst.data());
    const gfx::VertexLayout li = instansLayout();
    auto                    t  = mål(r, 64, 64, 1, false);
    auto                    ti = mål(r, 64, 32, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        gfx::MeshDraw d = custom(*pf, lf);
        d.vertices      = gfx::BufferSlice{vb.get(), 3 * sizeof(PosFärg),
                                      6 * sizeof(PosFärg)};
        d.vertexCount   = 6;
        rr.drawMesh(d);
        rr.popTarget();
        rr.pushTarget(*ti, kVit);
        gfx::MeshDraw di = custom(*pi, li);
        di.vertices      = gfx::slice(*hb);
        di.vertexCount   = 6;
        di.instances     = gfx::slice(*ib);
        di.instanceCount = 2;
        rr.drawMesh(di);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b  = läs(r, *t, "C5");
    const Bild bi = läs(r, *ti, "C5b");
    check(ärFärg(b.at(32, 32), grön) && ärFärg(b.at(8, 8), kVit),
          "C5 icke-indexerat: sex vertex ur delen (efter tre skräpvertex) "
          "ritar rutan och inget annat",
          px(b.at(32, 32)) + " / " + px(b.at(8, 8)));
    check(ärFärg(bi.at(8, 8), kRöd) && ärFärg(bi.at(28, 8), kBlå) &&
              ärFärg(bi.at(48, 8), kVit),
          "C5b icke-indexerat OCH instansierat: två instanser",
          px(bi.at(8, 8)) + " / " + px(bi.at(28, 8)));
}

// ------------------------------------------------------------------- C6
void fallC6(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    auto           p = program(r, "c6_helskarm", kHelskärm);
    check(p->valid() && p->inputs() == 0,
          "C6 helskärmsprogrammet kompilerar och läser ingen ström",
          kort(p->error()));
    if (!p->valid())
        return;
    auto t = mål(r, 40, 24, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        ritaHelskärm(rr, *p);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "C6");
    check(allaPixlar(b, gfx::Color{51, 102, 153, 255}),
          "C6 helskärmstriangeln utan buffert fyller HELA målet",
          px(b.at(0, 0)) + " / " + px(b.at(39, 23)));
}

// ------------------------------------------------------------------- C7
void fallC7(Fönster& f)
{
    gfx::Renderer&      r      = f.window().renderer();
    const std::uint32_t byggda = r.stats().programsBuilt;
    auto                p      = program(r, "rad_sju", kRadSju);
    // Samma fel med definitioner: de står före `#line 1` och flyttar inget.
    auto pd = program(r, "rad_sju_def", kRadSju, 0, 0, {{"GATE_X", "1"}});
    const std::string fel = p->error();
    check(!p->valid() && !fel.empty(),
          "C7 syntaxfelet ger ett ogiltigt program med ett fel");
    check(radSju(fel) && fel.find(kSpråk) != std::string::npos,
          std::string("C7 felet anger appens rad 7 och språket (") + kSpråk +
              ")",
          kort(fel));
    check(!pd->valid() && radSju(pd->error()),
          "C7 med definitioner: fortfarande rad 7", kort(pd->error()));
    check(r.stats().programsBuilt == byggda,
          "C7 ett ogiltigt program räknas inte i programsBuilt");
    const gfx::VertexLayout l = posFärgLayout();
    const Ström             s = ruta(r, 0, 0, 64, 64, 0.5f, kRöd);
    auto                    t = mål(r, 64, 64, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        ritaRuta(rr, *p, l, s);
        ritaHelskärm(rr, *p);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "C7");
    check(allaPixlar(b, kVit),
          "C7 ett rit-anrop med det ogiltiga programmet lämnar målet orört",
          px(b.at(32, 32)));
}

// ------------------------------------------------------------------- C8
void fallC8(Fönster& f)
{
    gfx::Renderer& r    = f.window().renderer();
    auto           tom  = program(r, "c8_tom", utanEgen(kFärg));
    auto           bara = program(r, "c8_bara", baraEgen(kFärg));
    const std::string väntat = std::string("saknar ") + kSpråk + "-källa";
    check(!tom->valid() &&
              tom->error().find(väntat) != std::string::npos &&
              tom->error().find("c8_tom") != std::string::npos,
          "C8 tom källa för backenden: ogiltigt, och felet säger \"" + väntat +
              "\" med programmets namn",
          kort(tom->error()));
#if defined(GLAZE_BACKEND_LINUX)
    Källor utanVs = kFärg;
    utanVs.glslVs = "";
    auto tomVs    = program(r, "c8_tom_vs", utanVs);
    check(!tomVs->valid() &&
              tomVs->error().find(väntat) != std::string::npos,
          "C8 GLSL: också ett tomt VERTEXsteg ger felet", kort(tomVs->error()));
#endif
    check(bara->valid(),
          "C8 bara backendens egen källa räcker — de andra får vara tomma",
          kort(bara->error()));
}

// ------------------------------------------------------------------- C9
void fallC9(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    auto           pf = program(r, "c9_fram", kFram);
    auto           pm = program(r, "c9_mal", kMål);
    check(pf->valid() && pm->valid(), "C9 programmen kompilerar",
          kort(pf->error() + pm->error()));
    if (!pf->valid() || !pm->valid())
        return;
    // Vyn — grindens egen: blicken är INTE enhetlig, och det förväntade värdet normerar den
    // här, oberoende av frameUniforms.
    gfx::View3D v;
    v.eye        = {3.0, -7.0, 4.0};
    v.forward    = {0.4, 0.7, -0.6};
    v.up         = {0.0, 0.0, 1.0};
    v.projection = gfx::Projection3D::Perspective;
    v.fovY       = 0.8;
    v.nearZ      = 0.5;
    v.farZ       = 100.0;
    v.box        = gfx::Rectf{0, 0, 48, 32};
    const double n = std::sqrt(0.4 * 0.4 + 0.7 * 0.7 + 0.6 * 0.6);
    const gfx::Color facit{
        static_cast<std::uint8_t>(std::lround((0.4 / n * 0.5 + 0.5) * 255.0)),
        static_cast<std::uint8_t>(std::lround((0.7 / n * 0.5 + 0.5) * 255.0)),
        static_cast<std::uint8_t>(std::lround((-0.6 / n * 0.5 + 0.5) * 255.0)),
        255};
    auto tf = mål(r, 48, 32, 1, false);
    auto tm = mål(r, 48, 32, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        static const gfx::VertexLayout tom;
        rr.pushTarget(*tf, kVit);
        gfx::MeshDraw d = custom(*pf, tom);
        d.frame         = gfx::frameUniforms(v, 1.0, 2.0, 3.0);
        d.vertexCount   = 3;
        rr.drawMesh(d);
        rr.popTarget();
        rr.pushTarget(*tm, kVit);
        ritaHelskärm(rr, *pm);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild bf = läs(r, *tf, "C9 forward");
    const Bild bm = läs(r, *tm, "C9 target");
    check(ärFärg(bf.at(24, 16), facit),
          "C9 GlazeFrame: glaze.forward i mittpixeln är vyns blick (normerad "
          "av grinden)",
          px(bf.at(24, 16)) + " mot " + std::to_string(facit.r) + "," +
              std::to_string(facit.g) + "," + std::to_string(facit.b));
    check(ärFärg(bm.at(24, 16), gfx::Color{48, 32, 255, 255}),
          "C9 GlazeFrame: glaze.target är det BUNDNA målets storlek (48, 32, "
          "1/48, …) — backenden skriver den",
          px(bm.at(24, 16)));
}

// ------------------------------------------------------------------ C10
void fallC10(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    auto           pa = program(r, "c10_utan", kDefs);
    auto           pb = program(r, "c10_med", kDefs, 0, 0,
                                {{"GATE_GREEN", ""}, {"GATE_LEVEL", "0.5"}});
    auto           pc = program(r, "c10_fel", kDefs, 0, 0, {{"1GATE", "1"}});
    check(pa->valid() && pb->valid(),
          "C10 samma källa kompilerar utan och med definitioner",
          kort(pa->error() + pb->error()));
    check(!pc->valid() &&
              pc->error().find("identifierare") != std::string::npos,
          "C10 en definition som inte är en identifierare ger ett läsbart fel",
          kort(pc->error()));
    if (!pa->valid() || !pb->valid())
        return;
    auto ta = mål(r, 16, 16, 1, false);
    auto tb = mål(r, 16, 16, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*ta, kVit);
        ritaHelskärm(rr, *pa);
        rr.popTarget();
        rr.pushTarget(*tb, kVit);
        ritaHelskärm(rr, *pb);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild ba = läs(r, *ta, "C10 utan");
    const Bild bb = läs(r, *tb, "C10 med");
    check(ärFärg(ba.at(8, 8), kRöd), "C10 utan definitionerna: den andra grenen",
          px(ba.at(8, 8)));
    check(ärFärg(bb.at(8, 8), gfx::Color{0, 128, 0, 255}),
          "C10 med GATE_GREEN (utan värde) och GATE_LEVEL 0.5: grenen OCH "
          "värdet",
          px(bb.at(8, 8)));
}

// ------------------------------------------------------------------ C11
void fallC11(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    auto           p = program(r, "c11_lage", kFärg);
    if (!p->valid()) {
        check(false, "C11 programmet kompilerar", kort(p->error()));
        return;
    }
    const gfx::VertexLayout l    = posFärgLayout();
    const Ström             röd  = ruta(r, 16, 16, 48, 48, 0.3f, kRöd);
    const Ström             blå  = ruta(r, 16, 16, 48, 48, 0.6f, kBlå);
    auto                    nära = mål(r, 64, 64, 1, true);
    auto                    bort = mål(r, 64, 64, 1, true);
    auto                    utan = mål(r, 64, 64, 1, true);
    // Gallringen: rutans trianglar är MEDURS för ögat (grindens egen
    // vindning, A6:s förväntade värde) — Back fäller dem, Front släpper igenom.
    const gfx::Point2d hörn[4] = {{16, 16}, {48, 16}, {16, 48}, {48, 48}};
    const bool         fram    = motursFörÖgat(false, hörn, 0, 1, 2);
    auto               back    = mål(r, 64, 64, 1, true);
    auto               front   = mål(r, 64, 64, 1, true);
    // Och i ett MSAA-mål med djup (Metal bakar provtagningen i PSO:n).
    auto msaa = mål(r, 64, 64, r.supportedSamples(4), true);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*msaa, kVit);
        ritaRuta(rr, *p, l, blå, läge(gfx::DepthTest::Less));
        ritaRuta(rr, *p, l, röd, läge(gfx::DepthTest::Less));
        rr.popTarget();
        rr.pushTarget(*nära, kVit);
        ritaRuta(rr, *p, l, röd, läge(gfx::DepthTest::Less));
        ritaRuta(rr, *p, l, blå, läge(gfx::DepthTest::Less));
        rr.popTarget();
        rr.pushTarget(*bort, kVit);
        ritaRuta(rr, *p, l, blå, läge(gfx::DepthTest::Less));
        ritaRuta(rr, *p, l, röd, läge(gfx::DepthTest::Less));
        rr.popTarget();
        rr.pushTarget(*utan, kVit);
        ritaRuta(rr, *p, l, röd);
        ritaRuta(rr, *p, l, blå);
        rr.popTarget();
        gfx::RenderState sb = läge(gfx::DepthTest::Less);
        sb.cull             = gfx::Cull::Back;
        rr.pushTarget(*back, kVit);
        ritaRuta(rr, *p, l, röd, sb);
        rr.popTarget();
        gfx::RenderState sf = sb;
        sf.cull             = gfx::Cull::Front;
        rr.pushTarget(*front, kVit);
        ritaRuta(rr, *p, l, röd, sf);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    const Bild a = läs(r, *nära, "C11 närmast först");
    const Bild b = läs(r, *bort, "C11 bortre först");
    const Bild c = läs(r, *utan, "C11 Off");
    check(ärFärg(a.at(32, 32), kRöd) && ärFärg(b.at(32, 32), kRöd),
          "C11 Less på ett eget program: den närmare (z ur vertexens float3) "
          "vinner i båda ordningarna",
          px(a.at(32, 32)) + " / " + px(b.at(32, 32)));
    check(ärFärg(c.at(32, 32), kBlå), "C11 Off: den sist ritade (kontrollen)",
          px(c.at(32, 32)));
    const Bild bb = läs(r, *back, "C11 Back");
    const Bild bf = läs(r, *front, "C11 Front");
    check(!fram && ärFärg(bb.at(32, 32), kVit) && ärFärg(bf.at(32, 32), kRöd),
          "C11 gallringen: Back fäller baksidan, Front släpper igenom den",
          px(bb.at(32, 32)) + " / " + px(bf.at(32, 32)));
    const Bild bm = läs(r, *msaa, "C11 MSAA");
    check(msaa->samples() > 1 && ärFärg(bm.at(32, 32), kRöd) &&
              ärFärg(bm.at(4, 4), kVit),
          "C11 i ett MSAA-mål med djup (" + std::to_string(msaa->samples()) +
              " prov): Less, och målet upplöst",
          px(bm.at(32, 32)));
}

// ------------------------------------------------------------------ C12
//
// DE DELADE REGLERNA: anrop som inte stämmer med
// programmet ritas INTE — målet står orört — och beskrivningar som inte går
// att bygga ger läsbara fel. Samma regler i alla backends (renderer.cpp).
void fallC12(Fönster& f)
{
    gfx::Renderer& r  = f.window().renderer();
    auto           pf = program(r, "c12_farg", kFärg);
    auto           pu = program(r, "c12_user", kUser, 32);
    auto           pt = program(r, "c12_tex", kTex, 0, 3);
    auto           pi = program(r, "c12_inst", kInst);
    if (!pf->valid() || !pu->valid() || !pt->valid() || !pi->valid()) {
        check(false, "C12 programmen kompilerar");
        return;
    }
    const Ström             s = ruta(r, 0, 0, 64, 64, 0.5f, kRöd);
    const gfx::VertexLayout l = posFärgLayout();
    // Layouten utan färgen (plats 2), och med färgen som heltal.
    gfx::VertexLayout utanFärg = l;
    utanFärg.attribs.pop_back();
    gfx::VertexLayout heltal      = l;
    heltal.attribs[1].format      = gfx::AttribFormat::UInt8x4;
    gfx::VertexLayout dubbel      = l;
    dubbel.attribs[1].location    = gfx::attrib::kPosition;
    const std::vector<float> v2   = {0, 0, 64, 0, 0, 64, 64, 64};
    const Ström              s2   = ström(r, v2, {0, 1, 2, 1, 3, 2});
    const gfx::VertexLayout  l2   = pos2Layout();
    const std::vector<Instans> in = {{4, 4, kRöd}, {24, 4, kRöd}};
    auto ib = r.createBuffer(gfx::BufferKind::Vertex,
                             in.size() * sizeof(Instans), in.data());
    const gfx::VertexLayout  li   = instansLayout();
    const std::vector<float> hörn = {0, 0, 8, 0, 0, 8, 8, 8};
    const Ström              sh   = ström(r, hörn, {0, 1, 2, 1, 3, 2});
    auto  bild = enfärgad(r, kRöd);
    const float user[8] = {1, 0, 0, 1, 1, 0, 0, 1};

    // Varje anrop ritas OGENOMSKINLIGT (Blend::Opaque): ritas det alls syns
    // det — också när bitarna omtolkats till en genomskinlig färg (D3D11
    // varnar bara för heltal mot flyttal och läser bitarna som de är).
    auto rutan = [&](const gfx::ShaderProgram& p, const gfx::VertexLayout& lay,
                     const Ström& st) {
        gfx::MeshDraw d = custom(p, lay);
        d.vertices      = gfx::slice(*st.vb);
        d.indices       = gfx::slice(*st.ib);
        d.indexCount    = st.count;
        return d;
    };
    struct Prov {
        const char*                   namn;
        std::function<gfx::MeshDraw()> anrop;
    };
    const Prov prov[] = {
        {"utan layout",
         [&] {
             gfx::MeshDraw d = rutan(*pf, l, s);
             d.layout        = nullptr;
             return d;
         }},
        {"layouten saknar en plats programmet läser (färgen, plats 2)",
         [&] { return rutan(*pf, utanFärg, s); }},
        {"färgen som heltal där programmet läser flyttal",
         [&] { return rutan(*pf, heltal, s); }},
        {"samma plats två gånger i layouten",
         [&] { return rutan(*pf, dubbel, s); }},
        {"instansdelen rymmer bara två av tre instanser",
         [&] {
             gfx::MeshDraw d = rutan(*pi, li, sh);
             d.instances     = gfx::slice(*ib);
             d.instanceCount = 3;
             return d;
         }},
        {"instansattribut men instanceCount 0",
         [&] {
             gfx::MeshDraw d = rutan(*pi, li, sh);
             d.instances     = gfx::slice(*ib);
             return d;
         }},
        {"GlazeUser 16 byte till ett program med 32",
         [&] {
             gfx::MeshDraw d = rutan(*pu, l2, s2);
             d.user          = user;
             d.userBytes     = 16;
             return d;
         }},
        {"två texturer till ett program med tre",
         [&] {
             gfx::MeshDraw d = rutan(*pt, l2, s2);
             d.textures      = {bild.get(), bild.get(), nullptr, nullptr};
             return d;
         }},
        {"vertexCount större än vertexdelen",
         [&] {
             gfx::MeshDraw d = custom(*pf, l);
             d.vertices      = gfx::slice(*s.vb);
             d.vertexCount   = 6;   // delen har fyra
             return d;
         }},
    };
    constexpr std::size_t kN = sizeof prov / sizeof prov[0];
    std::unique_ptr<gfx::RenderTarget> t[kN];
    for (auto& m : t)
        m = mål(r, 64, 64, 1, false);
    iMål(f, [&](gfx::Renderer& rr) {
        for (std::size_t i = 0; i < kN; ++i) {
            rr.pushTarget(*t[i], kVit);
            gfx::MeshDraw d = prov[i].anrop();
            d.state.blend   = gfx::Blend::Opaque;
            rr.drawMesh(d);
            rr.popTarget();
        }
    });
    if (g_ritfel)
        return;
    for (std::size_t i = 0; i < kN; ++i) {
        const Bild b = läs(r, *t[i], "C12");
        check(allaPixlar(b, kVit),
              std::string("C12 ritas inte: ") + prov[i].namn,
              px(b.at(32, 32)));
    }

    // Beskrivningar som inte går att bygga.
    auto ub = program(r, "c12_user20", kUser, 20);
    check(!ub->valid() && ub->error().find("16") != std::string::npos,
          "C12 userBytes 20 (inte float4-packat) ger ett läsbart fel",
          kort(ub->error()));
    auto tx = program(r, "c12_tex5", kTex, 0, 5);
    check(!tx->valid() && tx->error().find("texturer") != std::string::npos,
          "C12 fem texturer (högst fyra) ger ett läsbart fel", kort(tx->error()));
#if !defined(GLAZE_BACKEND_METAL)
    // Källans GlazeUser är 32 byte — beskrivningen säger 16 (D3D11:s
    // reflektion och GL:s blockstorlek prövar det; Metal kan inte).
    auto us = program(r, "c12_user16", kUser, 16);
    check(!us->valid() && us->error().find("32") != std::string::npos,
          "C12 GlazeUser i källan (32 byte) mot ProgramDesc::userBytes 16 ger "
          "ett läsbart fel",
          kort(us->error()));
    // Källan samplar plats 2 — beskrivningen säger två texturer (0 och 1).
    auto t2 = program(r, "c12_tex2", kTex, 0, 2);
    check(!t2->valid() && t2->error().find("textures") != std::string::npos,
          "C12 en textur på plats 2 med ProgramDesc::textures 2 ger ett läsbart "
          "fel",
          kort(t2->error()));
    // En LÖS global (HLSL: $Globals på b0, där UI:ts viewport sitter; GLSL:
    // en uniform som aldrig får ett värde) — MSL har ingen motsvarighet.
    const Källor kLös = {
        R"hlsl(
float4 tint;
float4 vsMain(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }
float4 fsMain(float4 p : SV_Position) : SV_Target { return tint; }
)hlsl",
        "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n",
        "uniform vec4 tint;\nout vec4 frag;\nvoid main() { frag = tint; }\n",
        ""};
    auto lös = program(r, "c12_los", kLös);
    check(!lös->valid() && lös->error().find("tint") != std::string::npos,
          "C12 en lös global/uniform utanför GlazeUser ger ett läsbart fel",
          kort(lös->error()));
#endif
}

// ============================================================ DEL D
//
// LINE3 OCH GLYPH3 genom fasaden. Grinden bygger kameran själv —
// ögat i origo, blicken längs +y, upp längs +z, brännvidden 128 px i ett
// 128 × 128-mål — och det FÖRVÄNTADE VÄRDET är HÅLKAMERAN: punkten (x, y, z) hamnar i
// px = 64 + 128·x/y, py = 64 − 128·z/y, och en världsenhet vinkelrätt mot
// blicken på djupet y är 128/y px. Ritytans vägar är inte med här; D6:s
// rityta och del G prövar dem.

constexpr int    kDSida  = 128;     // målets sida
constexpr double kDBränn = 128.0;   // brännvidden i px

gfx::View3D dVy()
{
    gfx::View3D v;
    v.forward    = gfx::Vec3d{0.0, 1.0, 0.0};
    v.up         = gfx::Vec3d{0.0, 0.0, 1.0};
    v.projection = gfx::Projection3D::Perspective;
    v.fovY       = 2.0 * std::atan(0.5 * kDSida / kDBränn);
    v.nearZ      = 0.5;
    v.farZ       = 100.0;
    v.box        = gfx::Rectf{0.0f, 0.0f, static_cast<float>(kDSida),
                              static_cast<float>(kDSida)};
    return v;
}
double hålX(double x, double y) { return kDSida / 2.0 + kDBränn * x / y; }
double hålY(double z, double y) { return kDSida / 2.0 - kDBränn * z / y; }

// En form i rymden på GPU:n — Line3 eller Glyph3, origot i xyz.
struct Form3 {
    gfx::MeshProgram                program = gfx::MeshProgram::Line3;
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
    double                          ox = 0.0, oy = 0.0, oz = 0.0;
};

// Byggarens första chunk på GPU:n (grindens former ryms i en). Ingen chunk
// ger en tom form — fallets mätpunkter faller då.
template <class Chunk>
Form3 ladda3(gfx::Renderer& r, gfx::MeshProgram p,
             const std::vector<Chunk>& chunks)
{
    Form3 f;
    f.program = p;
    if (chunks.empty())
        return f;
    const Chunk& c = chunks.front();
    f.vb    = r.createBuffer(gfx::BufferKind::Vertex,
                             c.vertices.size() * sizeof(c.vertices[0]),
                             c.vertices.data());
    f.ib    = r.createBuffer(gfx::BufferKind::Index,
                             c.indices.size() * sizeof(std::uint32_t),
                             c.indices.data());
    f.count = static_cast<std::uint32_t>(c.indices.size());
    f.ox    = c.originX;
    f.oy    = c.originY;
    f.oz    = c.originZ;
    return f;
}

Form3 linje3(gfx::Renderer& r, const std::vector<gfx::Vec3d>& p, float bredd,
             gfx::Color färg, gfx::LineCap ände = gfx::LineCap::Butt)
{
    gfx::PolylineStyle s;
    s.width = bredd;
    s.color = färg;
    s.cap   = ände;
    std::vector<gfx::LineChunk3> chunks;
    gfx::buildPolyline3(p, s, chunks);
    return ladda3(r, gfx::MeshProgram::Line3, chunks);
}

// Kvadratiska markörer i rymden, sidan i px — i EN chunk, vars origo är den
// första markörens ankare (sdf.cpp): de följande bär ankarets z
// chunk-lokalt.
Form3 markör3(gfx::Renderer& r, const std::vector<gfx::Vec3d>& at, float sida,
              gfx::Color färg)
{
    gfx::MarkerLayoutStyle s;
    s.shape = gfx::MarkerShape::Square;
    s.size  = sida;
    s.fill  = färg;
    std::vector<gfx::GlyphChunk3> chunks;
    gfx::buildMarkers(at, s, chunks);
    return ladda3(r, gfx::MeshProgram::Glyph3, chunks);
}

// Ritar formen med vyn. Bredden i px (pxPerWidthUnit 1) eller i världen.
void rita3(gfx::Renderer& r, const Form3& f, const gfx::View3D& v,
           const gfx::RenderState& s = {},
           gfx::WidthSpace rum = gfx::WidthSpace::Screen,
           const gfx::SceneDash& streck = {})
{
    if (!f.vb || !f.ib)
        return;
    gfx::MeshDraw d;
    d.program    = f.program;
    d.vertices   = gfx::slice(*f.vb);
    d.indices    = gfx::slice(*f.ib);
    d.indexCount = f.count;
    gfx::viewProjection(v, f.ox, f.oy, f.oz).toFloat(d.uniforms.viewProj);
    d.uniforms.pxPerWidthUnit = 1.0f;
    d.uniforms.widthSpace     = rum;
    d.uniforms.dash           = streck;
    d.state                   = s;
    r.drawMesh(d);
}

// Röd på vitt: täckningen ur gröna kanalen (G = 255 · (1 − täckning)).
double täck(const Bild& b, int x, int y)
{
    return 1.0 - b.at(x, y)[1] / 255.0;
}
// Täckningen summerad längs en rad respektive en kolumn — linjens BREDD i
// px när den korsar raden/kolumnen vinkelrätt (kantutjämningen räknas med).
double radSumma(const Bild& b, int y, int x0, int x1)
{
    double s = 0.0;
    for (int x = x0; x < x1; ++x)
        s += täck(b, x, y);
    return s;
}
double kolumnSumma(const Bild& b, int x, int y0, int y1)
{
    double s = 0.0;
    for (int y = y0; y < y1; ++y)
        s += täck(b, x, y);
    return s;
}
std::string tal(double v)
{
    char s[32];
    std::snprintf(s, sizeof s, "%.2f", v);
    return s;
}

// Ett nytt mål med djup, vitt, och formerna ritade i det med `rita`.
std::unique_ptr<gfx::RenderTarget>
dMål(Fönster& f, const std::function<void(gfx::Renderer&)>& rita)
{
    gfx::Renderer& r = f.window().renderer();
    auto           t = mål(r, kDSida, kDSida, 1, true);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, kVit);
        rita(rr);
        rr.popTarget();
    });
    return t;
}

// ------------------------------------------------------------------- D1
//
// En golvlinje rakt fram (z = −0,5, x = 0) från y = 1 till 16: på skärmen
// lodrät i kolumn 64, från rad 128 till 68. 6 px bred både nära (rad 120,
// y ≈ 1,1) och fjärran (rad 72, y = 8).
void fallD1(Fönster& f)
{
    gfx::Renderer&    r = f.window().renderer();
    const gfx::View3D v = dVy();
    const Form3       l = linje3(r, {{0, 1, -0.5}, {0, 16, -0.5}}, 6.0f, kRöd);
    auto t = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, l, v, läge(gfx::DepthTest::Less));
    });
    if (g_ritfel)
        return;
    const Bild   b       = läs(r, *t, "D1");
    const double nära    = radSumma(b, 120, 40, 88);
    const double fjärran = radSumma(b, 72, 40, 88);
    check(std::abs(nära - 6.0) <= 1.0 && std::abs(fjärran - 6.0) <= 1.0 &&
              std::abs(nära - fjärran) <= 1.0,
          "D1 Line3 i px är lika bred nära och fjärran under perspektiv (6 px "
          "±1)",
          "nära " + tal(nära) + ", fjärran " + tal(fjärran));
}

// ------------------------------------------------------------------- D2
//
// En linje från (0,5; 4; −0,5) — i (80, 80) — till (0,5; −4; −0,5) BAKOM
// kameran: på skärmen går den från (80, 80) mot nedre högra hörnet (där
// närplanet y = 0,5 ligger i (192, 192)), aldrig mot övre vänstra (där en
// punkt bakom kameran hamnar speglad, i (48, 48)).
void fallD2(Fönster& f)
{
    gfx::Renderer&    r = f.window().renderer();
    const gfx::View3D v = dVy();
    const Form3 l = linje3(r, {{0.5, 4, -0.5}, {0.5, -4, -0.5}}, 6.0f, kRöd);
    auto t = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, l, v, läge(gfx::DepthTest::Less));
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "D2");
    // 45° på skärmen: en rad korsar 6 px bredd på 6·√2 px. Rad 118 är den
    // sista där bandet ryms helt (dess högra kant når kolumn 123); bandet
    // fortsätter till målets kant (rad 126).
    const double bred = 6.0 * std::sqrt(2.0);
    const double r100 = radSumma(b, 100, 70, 128);
    const double r118 = radSumma(b, 118, 90, 128);
    const double kant = täck(b, 126, 126);
    double       framför = 0.0;   // övre vänstra delen, före början
    for (int y = 0; y < 78; ++y)
        framför += radSumma(b, y, 0, 78);
    check(std::abs(r100 - bred) <= 1.0 && std::abs(r118 - bred) <= 1.0 &&
              kant > 0.9,
          "D2 delen framför kameran ritas mot närplanet, med sin bredd, ända "
          "till målets kant (rad 100 och 118: 6·√2 px; hörnpixeln täckt)",
          tal(r100) + " / " + tal(r118) + ", hörnet " + tal(kant));
    check(framför < 0.5,
          "D2 delen bakom kameran syns inte — inget mot övre vänstra hörnet",
          "täckning " + tal(framför));
}

// ------------------------------------------------------------------- D3
//
// Fyrkantiga ändar förlängs en halv bredd i SKÄRMEN, i varje ände: tvärs
// blicken (y = 4, x ±0,5 → px 48–80, 8 px bred: rak 32 px lång, fyrkantig
// 40) och längs golvet bort från kameran (x = 0, y 2–8 → rad 96–72, 12 px
// bred: rak 24 px, fyrkantig 36 — 6 px i båda ändarna fast en världsenhet
// är 16 px i den närmare och 1 px i den bortre).
void fallD3(Fönster& f)
{
    gfx::Renderer&    r = f.window().renderer();
    const gfx::View3D v = dVy();
    const Form3 tvärsRak = linje3(r, {{-0.5, 4, 0}, {0.5, 4, 0}}, 8.0f, kRöd);
    const Form3 tvärsFyr = linje3(r, {{-0.5, 4, 0}, {0.5, 4, 0}}, 8.0f, kRöd,
                                  gfx::LineCap::Square);
    const Form3 bortRak = linje3(r, {{0, 2, -0.5}, {0, 8, -0.5}}, 12.0f, kRöd);
    const Form3 bortFyr = linje3(r, {{0, 2, -0.5}, {0, 8, -0.5}}, 12.0f, kRöd,
                                 gfx::LineCap::Square);
    const Form3* former[4] = {&tvärsRak, &tvärsFyr, &bortRak, &bortFyr};
    std::unique_ptr<gfx::RenderTarget> t[4];
    for (int i = 0; i < 4; ++i) {
        t[i] = dMål(f, [&](gfx::Renderer& rr) {
            rita3(rr, *former[i], v, läge(gfx::DepthTest::Less));
        });
        if (g_ritfel)
            return;
    }
    const double tr = radSumma(läs(r, *t[0], "D3 tvärs rak"), 64, 20, 108);
    const double tf = radSumma(läs(r, *t[1], "D3 tvärs fyrkantig"), 64, 20, 108);
    const double br = kolumnSumma(läs(r, *t[2], "D3 bort rak"), 64, 50, 120);
    const double bf = kolumnSumma(läs(r, *t[3], "D3 bort fyrkantig"), 64, 50, 120);
    check(std::abs(tr - 32.0) <= 1.0 && std::abs(tf - 40.0) <= 1.0,
          "D3 tvärs blicken: rak 32 px, fyrkantig 40 px (halva bredden i "
          "varje ände)",
          tal(tr) + " / " + tal(tf));
    check(std::abs(br - 24.0) <= 1.0 && std::abs(bf - 36.0) <= 1.0,
          "D3 bort från kameran: rak 24 px, fyrkantig 36 px — halva bredden i "
          "SKÄRMEN också i den bortre änden",
          tal(br) + " / " + tal(bf));
}

// ------------------------------------------------------------------- D4
//
// Streck i världsenheter (0,25 på, 0,25 av) på en LODRÄT linje (x = 0,
// y = 4, z −0,75 → 0,75: rad 88 → 40, 32 px per världsenhet). Längden i
// xy är noll — strecken följer 3D-längden: tre streck om 8 px, med början
// i linjens start (rad 80–88, 64–72, 48–56).
void fallD4(Fönster& f)
{
    gfx::Renderer&    r = f.window().renderer();
    const gfx::View3D v = dVy();
    const Form3 l = linje3(r, {{0, 4, -0.75}, {0, 4, 0.75}}, 4.0f, kRöd);
    gfx::SceneDash streck;
    const float    mönster[2] = {0.25f, 0.25f};
    check(gfx::makeDash(mönster, 0.0f, streck), "D4 mönstret går att bygga");
    streck.perWorldUnit = 1.0f;
    auto t = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, l, v, läge(gfx::DepthTest::Less), gfx::WidthSpace::Screen,
              streck);
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "D4");
    // Strecken nerifrån (linjens start): körningar av täckta rader.
    std::vector<std::pair<int, int>> körningar;   // [från, till) i rader
    int start = -1;
    for (int y = 100; y >= 28; --y) {
        const bool på = täck(b, 64, y) >= 0.5;
        if (på && start < 0)
            start = y;
        if (!på && start >= 0) {
            körningar.push_back({y + 1, start + 1});
            start = -1;
        }
    }
    std::string fick;
    bool        ok = körningar.size() == 3;
    const int   väntadSlut[3] = {88, 72, 56};
    for (std::size_t i = 0; i < körningar.size(); ++i) {
        const int från = körningar[i].first, till = körningar[i].second;
        fick += "[" + std::to_string(från) + "," + std::to_string(till) + ") ";
        if (i < 3)
            ok = ok && std::abs((till - från) - 8) <= 1 &&
                 std::abs(till - väntadSlut[i]) <= 1;
    }
    check(ok,
          "D4 streck i världsenheter följer 3D-längden: tre streck om 8 px "
          "från linjens start",
          fick);
}

// ------------------------------------------------------------------- D5
//
// Två korsande Line3 i (64, 64): den röda tvärs blicken på djupet 3, den
// blå lodrät på djupet 6. Med Less vinner den röda (närmare) i båda
// ordningarna; utan djup den sist ritade (kontrollen).
void fallD5(Fönster& f)
{
    gfx::Renderer&    r    = f.window().renderer();
    const gfx::View3D v    = dVy();
    const Form3       nära = linje3(r, {{-1, 3, 0}, {1, 3, 0}}, 10.0f, kRöd);
    const Form3       bort = linje3(r, {{0, 6, -1}, {0, 6, 1}}, 10.0f, kBlå);
    const gfx::RenderState less = läge(gfx::DepthTest::Less);
    auto a = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, nära, v, less);
        rita3(rr, bort, v, less);
    });
    auto b = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, bort, v, less);
        rita3(rr, nära, v, less);
    });
    auto c = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, nära, v);
        rita3(rr, bort, v);
    });
    if (g_ritfel)
        return;
    const Bild ba = läs(r, *a, "D5 närmast först");
    const Bild bb = läs(r, *b, "D5 bortre först");
    const Bild bc = läs(r, *c, "D5 Off");
    check(ärFärg(ba.at(64, 64), kRöd) && ärFärg(bb.at(64, 64), kRöd),
          "D5 Less: den närmare Line3 vinner i korsningen, i båda ordningarna",
          px(ba.at(64, 64)) + " / " + px(bb.at(64, 64)));
    check(ärFärg(bc.at(64, 64), kBlå) && ärFärg(ba.at(64, 50), kBlå) &&
              ärFärg(ba.at(30, 64), kRöd),
          "D5 Off: den sist ritade vinner (kontrollen) — och båda linjerna "
          "ritas utanför korsningen",
          px(bc.at(64, 64)) + " / " + px(ba.at(64, 50)) + " / " +
              px(ba.at(30, 64)));
}

// ------------------------------------------------------------------- D6
//
// Två kvadratiska Glyph3-markörer (8 px) i (0,5; 4) i SAMMA chunk: den
// första på marken z = −0,5 (chunkens origo, mitten i (80, 80)) och den
// andra på höjden z = 0,5 — dess z är chunk-lokalt 1 — med mitten där
// hålkameran och `gfx::project` säger, (80, 48). En markör BAKOM kameran
// (y = −4, speglad hade den hamnat i (48, 80)) syns inte alls.
void fallD6(Fönster& f)
{
    gfx::Renderer&    r      = f.window().renderer();
    const gfx::View3D v      = dVy();
    const Form3       framme = markör3(r, {{0.5, 4, -0.5}, {0.5, 4, 0.5}}, 8.0f, kRöd);
    const Form3       bakom  = markör3(r, {{0.5, -4, 0.5}}, 8.0f, kRöd);
    auto a = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, framme, v, läge(gfx::DepthTest::Less, false));
    });
    auto b = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, bakom, v, läge(gfx::DepthTest::Less, false));
    });
    if (g_ritfel)
        return;
    const Bild ba = läs(r, *a, "D6 framför");
    const Bild bb = läs(r, *b, "D6 bakom");
    // Övre halvan (rad < 64) bär den höga markören, den nedre markören på
    // marken.
    double sum = 0.0, sx = 0.0, sy = 0.0, nedre = 0.0, ny = 0.0;
    double bakomSum = 0.0;
    for (int y = 0; y < kDSida; ++y)
        for (int x = 0; x < kDSida; ++x) {
            const double t = täck(ba, x, y);
            if (y < kDSida / 2) {
                sum += t;
                sx += t * (x + 0.5);
                sy += t * (y + 0.5);
            } else {
                nedre += t;
                ny += t * (y + 0.5);
            }
            bakomSum += täck(bb, x, y);
        }
    const double cx = sum > 0.0 ? sx / sum : -1.0;
    const double cy = sum > 0.0 ? sy / sum : -1.0;
    const double cn = nedre > 0.0 ? ny / nedre : -1.0;
    double       ppx = 0.0, ppy = 0.0;
    const bool   synlig = gfx::project(v, gfx::Vec3d{0.5, 4.0, 0.5}, ppx, ppy);
    check(synlig && std::abs(ppx - hålX(0.5, 4.0)) < 1e-6 &&
              std::abs(ppy - hålY(0.5, 4.0)) < 1e-6,
          "D6 facit: gfx::project är hålkamerans (80, 48)",
          tal(ppx) + ", " + tal(ppy));
    check(std::abs(cx - ppx) <= 1.0 && std::abs(cy - ppy) <= 1.0 &&
              std::abs(sum - 64.0) <= 8.0 && std::abs(cn - hålY(-0.5, 4.0)) <= 1.0 &&
              std::abs(nedre - 64.0) <= 8.0,
          "D6 Glyph3 på höjden z = 0,5 (chunk-lokalt z 1) hamnar där project "
          "säger (±1), 8 × 8 px — och markören på marken i (80, 80)",
          "mitten " + tal(cx) + ", " + tal(cy) + " — yta " + tal(sum) +
              "; marken rad " + tal(cn) + ", yta " + tal(nedre));
    check(bakomSum < 0.5, "D6 Glyph3 bakom kameran syns inte",
          "täckning " + tal(bakomSum));
}

// ------------------------------------------------------------------- D7
//
// Bredden 0,1 världsenheter i POS3 = 0,1 × pxPerUnitAt vid punkten: 6,4 px på
// djupet 2 (64 px per enhet) och 1,6 px på djupet 8 (16 px per enhet) —
// linjer tvärs blicken på golvet z = −0,5 (rad 96 respektive 72).
void fallD7(Fönster& f)
{
    gfx::Renderer&    r    = f.window().renderer();
    const gfx::View3D v    = dVy();
    const Form3       nära = linje3(r, {{-0.3, 2, -0.5}, {0.3, 2, -0.5}}, 0.1f, kRöd);
    const Form3       bort = linje3(r, {{-0.3, 8, -0.5}, {0.3, 8, -0.5}}, 0.1f, kRöd);
    auto t = dMål(f, [&](gfx::Renderer& rr) {
        rita3(rr, nära, v, läge(gfx::DepthTest::Less), gfx::WidthSpace::World);
        rita3(rr, bort, v, läge(gfx::DepthTest::Less), gfx::WidthSpace::World);
    });
    if (g_ritfel)
        return;
    const Bild   b      = läs(r, *t, "D7");
    const double skalaN = gfx::pxPerUnitAt(v, gfx::Vec3d{0.0, 2.0, -0.5});
    const double skalaB = gfx::pxPerUnitAt(v, gfx::Vec3d{0.0, 8.0, -0.5});
    check(std::abs(skalaN - kDBränn / 2.0) < 1e-6 &&
              std::abs(skalaB - kDBränn / 8.0) < 1e-6,
          "D7 facit: pxPerUnitAt är hålkamerans 128/y",
          tal(skalaN) + " / " + tal(skalaB));
    const double n  = kolumnSumma(b, 64, 84, 108);
    const double bo = kolumnSumma(b, 64, 64, 80);
    check(std::abs(n - 0.1 * skalaN) <= 0.5 && std::abs(bo - 0.1 * skalaB) <= 0.5,
          "D7 världsbredd i POS3 = bredden × pxPerUnitAt vid punkten (±0,5 px)",
          tal(n) + " mot " + tal(0.1 * skalaN) + ", " + tal(bo) + " mot " +
              tal(0.1 * skalaB));
}

// ============================================================ DEL G
//
// RITYTANS 3D-MÅL genom en RIKTIG `glaze::Canvas` i ett dolt
// fönster — vägvalet, kompositen under det rundade klippet, cachen mot
// `targetRenders()` (internt, canvas/resources.h) och lagrens ordning.
// Ett nytt fönster per fall: ritytan har inget "nollställ".

// En halvgenomskinlig lapp OVANPÅ ritytan, som ett blinkande textfält —
// G3 invaliderar den. Dold tills fallet visar den.
const gfx::Color kLapp{0, 200, 0, 128};
class Lapp : public glaze::Widget {
public:
    void draw(const glaze::Ctx& c) override { c.r->fillRect(c.box, kLapp); }
};

// Ritytan räknar sina ritningar: G3 ska se att den RITADES (kompositen) i
// den frame där målet inte gjorde det.
class Rityta : public glaze::Canvas {
public:
    int ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Canvas::draw(c);
    }
};

class RitFönster3 : public glaze::AppWindow {
public:
    using glaze::AppWindow::handleEvent;
    RitFönster3()
    {
        setTitle("scene3d_pixel_gate G");
        setClientSize(360, 300);
        // Ritytan med marginal — fönstrets yta syns runt den (G2:s hörn).
        yta.name   = "yta";
        yta.x      = 12;
        yta.y      = 12;
        yta.width  = 300;
        yta.height = 220;
        yta.setBackground(kVit);
        yta.setBorder(false);
        root().adopt(yta);
        lapp.name   = "lapp";
        lapp.x      = 40;
        lapp.y      = 40;
        lapp.width  = 24;
        lapp.height = 24;
        lapp.hide();
        root().adopt(lapp);
        knapp.name = "knapp";
        knapp.setText("Byt");
        knapp.dock = glaze::Dock::Bottom;
        root().adopt(knapp);
    }
    Rityta        yta;
    Lapp          lapp;
    glaze::Button knapp;
};

struct Rit3 {
    RitFönster3 f;
    gfx::Rectf  box{};
    float       skala = 1.0f;

    bool öppna()
    {
        f.setOffscreen(true);
        f.show();
        if (!f.isOpen()) {
            check(false, "G: fönstret öppnas — ingen headless-yta?");
            g_ritfel = true;
            return false;
        }
        f.window().renderFrame();
        box   = f.yta.resolve(f.yta.behaviorCtx());
        skala = f.window().uiScale();
        return true;
    }
    // En frame UTAN att röra ritytan — bara det fönstret självt har skadat.
    Bild frame()
    {
        Bild b;
        f.window().renderFrame();
        if (!f.window().captureFrame(b.px, b.w, b.h)) {
            std::printf("  FEL  kunde inte läsa ritytans frame\n");
            g_ritfel = true;
            ++g_fel;
        }
        return b;
    }
    // En frame där ritytan invaliderats (nytt innehåll ska synas).
    Bild bild()
    {
        f.yta.invalidate();
        return frame();
    }
    // Ritytans lokala logiska px → bildens pixel.
    int x(double lx) const { return static_cast<int>(box.x + lx * skala); }
    int y(double ly) const { return static_cast<int>(box.y + ly * skala); }
    // Hur många gånger 3D-målet ritats i fönstret.
    std::uint64_t målRitningar()
    {
        const glaze::Ctx c = f.yta.behaviorCtx();
        return c.canvas ? c.canvas->targetRenders() : 0;
    }
    // Ett klick (ner och upp) mitt på en widget.
    void klicka(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        gfx::Event       e;
        e.windowId = f.window().id();
        e.mouseX   = r.x + r.w / 2.0f;
        e.mouseY   = r.y + r.h / 2.0f;
        e.button   = gfx::MouseButton::Left;
        e.type     = gfx::EventType::MouseButtonDown;
        f.handleEvent(e);
        e.type = gfx::EventType::MouseButtonUp;
        f.handleEvent(e);
    }
};

// Röda pixlar (ren röd: R hög, G och B låga) i en ruta kring (cx, cy).
int rödaKring(const Bild& b, int cx, int cy, int halv)
{
    int n = 0;
    for (int y = cy - halv; y <= cy + halv; ++y)
        for (int x = cx - halv; x <= cx + halv; ++x) {
            const std::uint8_t* p = b.at(x, y);
            n += p[0] > 200 && p[1] < 80 && p[2] < 80;
        }
    return n;
}

// ------------------------------------------------------- D6 (ritytan)
//
// ETIKETTENS PLACERING med 3D-vyn: en kolliderande `text3` i (0, 30, 0),
// långt bortom 3D-kamerans mål (0, 0, 0) längs blicken. Med kameran syns
// den ovanför mitten; ritytans 2D-skugga (kartvyn uppifrån kring målet,
// camera_math `mapViewAt`) lägger den långt utanför ytan — placerad med den
// hade kollisionen dolt den som "utanför".
void fallD6Rityta()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 10,
                                .elevation = 10});
    y.addShape(glaze::Shape::text3({0, 30, 0}, "HHHH",
                                   glaze::TextStyle{.size  = glaze::px(20.0f),
                                                    .color = kRöd}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const std::optional<glaze::Point> p = y.toLocal(glaze::Point{0, 30});
    const bool inne = p && p->x > 30 && p->y > 20 && p->x < 270 && p->y < 200;
    check(inne, "D6 rityta: etikettens ankare ligger på ytan (3D-kamerans "
                "projektion)",
          p ? tal(p->x) + ", " + tal(p->y) : "ingen");
    if (!inne)
        return;
    const int n = rödaKring(b, r.x(p->x), r.y(p->y), 30);
    check(n >= 20,
          "D6 rityta: den kolliderande text3-etiketten syns där 3D-kameran "
          "lägger den (placerad med 3D-vyn, inte 2D-skuggan)",
          std::to_string(n) + " röda px");
}

// ------------------------------------------------------------------- G1
//
// Bara 2D-innehåll — en yta med kontur i mm, en linje, en etikett med
// gloria, punkter, rutnätet och onPaint — ritat i 2D-vägen och sedan i
// 3D-målet (setDepth(true), setMultisample(1)): samma pixlar inom
// kantutjämningens avrundning. TOLERANSEN: högst 24 per kanal och högst
// 0,2 % av pixlarna mer än 2. Rutnätet är den enda källan till skillnad
// (utan det skiljer INGEN pixel): dess linjer går genom
// hela vyn och bortom 3D-vyns ändliga bortre plan, där de trimmas, och
// kartmatrisens plan ligger i oändligheten — avrundningen blir en annan.
// Största skillnaden: D3D11 högst 5 steg i en pixel lutad (1 uppifrån), GL
// (llvmpipe) högst 16 i tre pixlar. En halv pixels förskjutning, en mörk
// frans eller en saknad rutnätslinje ger hundratals pixlar (den saknade
// linjen före trimningen: 233 pixlar, 46 steg).
// Rakt uppifrån (kartkamerans 3D-vy är då ortografisk) och lutad.
void fallG1()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setYUp(true);
    y.setCenter(674000.0, 6580000.0);
    y.setZoom(1.5);
    y.setGridSize(10.0);
    y.addShape(glaze::Shape::polygon({{673950, 6579950}, {674050, 6579950},
                                      {674000, 6580040}},
                                     gfx::Color{0, 120, 255, 255},
                                     glaze::Stroke{.width = glaze::mm(0.5f)}));
    y.addShape(glaze::Shape::polyline({{673900, 6580000}, {674100, 6580010}},
                                      glaze::Stroke{.paint = kRöd, .width = 3.0f}));
    y.addShape(glaze::Shape::text({674000, 6580020}, "Storgatan",
                                  glaze::TextStyle{.size = glaze::mm(3.0f),
                                                   .halo = kVit}));
    y.addShape(glaze::Shape::points({{673980, 6579990}, {674020, 6579990}},
                                    glaze::MarkerStyle{.size = 8.0f}));
    y.onPaint = [](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.fillRect({673990, 6579960, 12, 9},
                           gfx::Color{0, 160, 0, 160});
    };
    y.setMultisample(1);
    const float lutning[2] = {0.0f, 35.0f};
    for (const float tilt : lutning) {
        y.setBearing(tilt > 0.0f ? 15.0f : 0.0f);
        y.setPitch(tilt);
        y.setDepth(false);
        const Bild          a    = r.bild();
        const std::uint64_t före = r.målRitningar();
        y.setDepth(true);
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::string namn = tilt > 0.0f ? "lutad" : "uppifrån";
        check(r.målRitningar() == före + 1,
              "G1 " + namn + ": setDepth(true) ritar genom 3D-målet",
              std::to_string(r.målRitningar() - före) + " ritningar");
        int störst = 0, olika = 0, antal = 0, sx = 0, sy = 0;
        for (int py = static_cast<int>(std::ceil(r.box.y));
             py < static_cast<int>(r.box.y + r.box.h); ++py)
            for (int px_ = static_cast<int>(std::ceil(r.box.x));
                 px_ < static_cast<int>(r.box.x + r.box.w); ++px_) {
                const std::uint8_t* p = a.at(px_, py);
                const std::uint8_t* q = b.at(px_, py);
                int d = 0;
                for (int k = 0; k < 3; ++k)
                    d = std::max(d, std::abs(p[k] - q[k]));
                if (d > störst) {
                    störst = d;
                    sx     = px_;
                    sy     = py;
                }
                olika += d > 2;
                ++antal;
            }
        check(antal > 0 && störst <= 24 && olika * 500 <= antal,
              "G1 " + namn + ": bara 2D-innehåll i 3D-målet med "
              "setMultisample(1) ger 2D-vägens pixlar (±24 per kanal, högst "
              "0,2 % mer än 2)",
              "störst " + std::to_string(störst) + " i (" + std::to_string(sx) +
                  ", " + std::to_string(sy) + "): " + px(a.at(sx, sy)) +
                  " mot " + px(b.at(sx, sy)) + ", " + std::to_string(olika) +
                  " av " + std::to_string(antal) + " px skiljer > 2");
    }
}

// ------------------------------------------------------------------- G2
//
// KOMPOSITEN: (a) en röd yta större än ritytan — hörnpixlarna utanför det
// rundade klippet visar fönstrets yta, inte målets röda; (b) en vriden röd
// triangel på vitt med förvalets MSAA — kantpixlarna har mellantoner men
// röd kanal 255 (rött över vitt), ingen mörk frans: målets premultiplicerade
// bild av-premultipliceras i kompositen.
void fallG2()
{
    {
        Rit3 r;
        if (!r.öppna())
            return;
        glaze::Canvas& y = r.f.yta;
        y.setDepth(true);
        y.addShape(glaze::Shape::rect({-50, -50, 400, 320}, kRöd));
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const int hx = static_cast<int>(std::floor(r.box.x));
        const int hy = static_cast<int>(std::floor(r.box.y));
        const int rad =
            static_cast<int>(std::ceil(r.f.style().cornerRadius * r.skala));
        const std::uint8_t* fönstret = b.at(hx - 3, hy + 40);
        const gfx::Color    yta{fönstret[0], fönstret[1], fönstret[2], 255};
        check(rad >= 2, "G2 stilens hörnradie ger rundade hörn",
              std::to_string(rad) + " px");
        check(!ärFärg(fönstret, kRöd, 40) && ärFärg(b.at(hx, hy), yta, 3) &&
                  ärFärg(b.at(hx + 2 * rad, hy + 2 * rad), kRöd),
              "G2 hörnpixeln efter kompositen visar fönstrets yta (genomskinlig "
              "i målet och utanför klippet), innanför hörnet är det rött",
              px(b.at(hx, hy)) + " mot " + px(fönstret) + " / " +
                  px(b.at(hx + 2 * rad, hy + 2 * rad)));
    }
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setDepth(true);
    y.addShape(glaze::Shape::polygon({{60, 40}, {220, 75}, {110, 190}}, kRöd));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    int lägsta = 255, mellan = 0;
    for (int py = r.y(4); py < r.y(216); ++py)
        for (int px_ = r.x(4); px_ < r.x(296); ++px_) {
            const std::uint8_t* p = b.at(px_, py);
            lägsta = std::min<int>(lägsta, p[0]);
            mellan += p[1] > 20 && p[1] < 235;
        }
    check(mellan >= 50 && lägsta >= 245,
          "G2 rött över vitt i 3D-målet (MSAA 4): kanterna har mellantoner "
          "utan mörk frans — röd kanal ≥ 245 överallt",
          std::to_string(mellan) + " kantpx, lägsta röda " +
              std::to_string(lägsta));
}

// ------------------------------------------------------------------- G3
//
// CACHEN: en barnwidget ovanpå ritytan som invaliderar sig själv ger +0 i
// targetRenders() — ritytan RITAS (kompositen syns under lappen) men inget
// program byts och ingenting laddas upp. canvas.invalidate() ger +1 och
// App::invalidateAll() ger +1 (genom setTheme — funktionen är privat).
void fallG3()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas&  y  = r.f.yta;
    gfx::Renderer&  rr = r.f.window().renderer();
    y.setDepth(true);
    y.addShape(glaze::Shape::rect({10, 10, 200, 150}, kRöd));
    r.f.lapp.show();
    r.bild();
    r.frame();   // det show() lämnade efter sig
    if (g_ritfel)
        return;

    const std::uint64_t n0 = r.målRitningar();
    const int           d0 = r.f.yta.ritningar;
    rr.resetStats();
    r.f.lapp.invalidate();
    const Bild b = r.frame();
    if (g_ritfel)
        return;
    const gfx::RenderStats s = rr.stats();
    // Lappens mitt: halvgenomskinligt grönt över RÖTT (kompositen), inte
    // över vitt.
    const gfx::Rectf    lr = r.f.lapp.resolve(r.f.lapp.behaviorCtx());
    const std::uint8_t* p  = b.at(static_cast<int>(lr.x + lr.w / 2.0f),
                                  static_cast<int>(lr.y + lr.h / 2.0f));
    check(r.målRitningar() == n0 && r.f.yta.ritningar > d0 &&
              s.programSwitches == 0 && s.bytesUploaded == 0 && p[2] < 40 &&
              p[0] > 90,
          "G3 en barnwidget som invaliderar sig själv: targetRenders +0, "
          "ritytan ritas med bara kompositen (inga programbyten, ingen "
          "uppladdning) och syns under lappen",
          "+" + std::to_string(r.målRitningar() - n0) + " mål, " +
              std::to_string(r.f.yta.ritningar - d0) + " ritningar, " +
              std::to_string(s.programSwitches) + " byten, " +
              std::to_string(s.bytesUploaded) + " byte, " +
              std::to_string(s.drawCalls) + " anrop, lappen " + px(p));

    rr.resetStats();
    y.invalidate();
    r.frame();
    const std::uint32_t byten = rr.stats().programSwitches;
    check(r.målRitningar() == n0 + 1 && byten >= 1,
          "G3 canvas.invalidate() ritar om målet (+1, scenens program)",
          "+" + std::to_string(r.målRitningar() - n0) + ", " +
              std::to_string(byten) + " byten");
    // App::invalidateAll är privat — temabytet är dess publika väg (samma
    // tema: ingen pixel ändras, bara invalideringen sker).
    r.f.setTheme(r.f.theme());
    r.frame();
    check(r.målRitningar() == n0 + 2,
          "G3 App::invalidateAll() (temabytets väg) ger +1",
          "+" + std::to_string(r.målRitningar() - n0));
}

// ------------------------------------------------------------------- G4
//
// `onPaint` läser en variabel som en KNAPPHANDLER ändrar. Handlern
// invaliderar ingenting själv — fönstrets fulla invalidering efter klicket
// ritar om målet, så den nya färgen syns.
void fallG4()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y    = r.f.yta;
    gfx::Color     färg = kRöd;
    y.setDepth(true);
    y.onPaint = [&färg](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.fillRect({60, 50, 80, 60}, färg);
    };
    r.f.knapp.onClick = [&färg] { färg = kBlå; };
    Bild b = r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(b.at(r.x(100), r.y(80)), kRöd), "G4 onPaint ritar i 3D-målet",
          px(b.at(r.x(100), r.y(80))));
    const std::uint64_t n0 = r.målRitningar();
    r.klicka(r.f.knapp);
    b = r.frame();
    if (g_ritfel)
        return;
    check(ärFärg(b.at(r.x(100), r.y(80)), kBlå) && r.målRitningar() > n0,
          "G4 knapphandlerns ändring syns: klicket ger en full invalidering och "
          "målet ritas om",
          px(b.at(r.x(100), r.y(80))) + ", +" +
              std::to_string(r.målRitningar() - n0) + " mål");
}

// ------------------------------------------------------------------- G5
//
// INAKTIVERAD: tonen ligger i kompositen — rött tonas med 1 − iconFade mot
// den vita ytan, som i 2D-vägen (scene_pixel_gate F1).
void fallG5()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setDepth(true);
    y.addShape(glaze::Shape::rect({20, 20, 200, 150}, kRöd));
    r.bild();
    y.disable();
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const double a = 1.0 - r.f.style().iconFade;
    const auto   v = static_cast<std::uint8_t>(std::lround(255.0 * (1.0 - a)));
    const gfx::Color väntad{255, v, v, 255};
    check(ärFärg(b.at(r.x(100), r.y(80)), väntad, 3),
          "G5 inaktiverad i 3D-målet: ytan tonas med 1 − iconFade (rött över "
          "vitt)",
          px(b.at(r.x(100), r.y(80))) + " mot " + px(&väntad.r));
}

// ------------------------------------------------------------------- G6
//
// GROUND-ETIKETTERNA RITAS EFTER WORLD: en etikett med blå platta i origo
// (lager 0 — bara 2D, alltså Ground) och en 30 px bred röd Line3 rakt genom
// den (lager 1 — World, 0,5 ovanför marken). Plattan ligger överst.
void fallG6()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 20,
                                .elevation = 50});
    const int världen = y.addLayer("Världen");
    y.addShape(0, glaze::Shape::text({0, 0}, "M",
                                     glaze::TextStyle{.size       = glaze::px(16.0f),
                                                      .color      = kSvart,
                                                      .background = kBlå,
                                                      .padding = glaze::px(8.0f)}));
    y.addShape(världen, glaze::Shape::line3({-6, 0, 0.5}, {6, 0, 0.5},
                                      glaze::Stroke{.paint = kRöd,
                                                    .width = glaze::px(30.0f)}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const std::optional<glaze::Point> o   = y.toLocal(glaze::Point{0, 0});
    const std::optional<glaze::Point> sid = y.toLocal(glaze::Point{-4, 0});
    if (!o || !sid) {
        check(false, "G6 origo och linjen ligger på ytan");
        return;
    }
    const int cx = r.x(o->x), cy = r.y(o->y);
    int       blå = 0;
    for (int py = cy - 12; py <= cy + 12; ++py)
        for (int px_ = cx - 12; px_ <= cx + 12; ++px_) {
            const std::uint8_t* p = b.at(px_, py);
            blå += p[2] > 200 && p[0] < 60 && p[1] < 60;
        }
    check(rödaKring(b, r.x(sid->x), r.y(sid->y), 2) >= 20,
          "G6 World-linjen ritas (kontrollen, utanför etiketten)",
          px(b.at(r.x(sid->x), r.y(sid->y))));
    check(blå >= 100 && rödaKring(b, cx, cy, 6) == 0,
          "G6 Ground-etiketten ritas EFTER World: plattan ligger över linjen",
          std::to_string(blå) + " blå px, " +
              std::to_string(rödaKring(b, cx, cy, 6)) + " röda i mitten");
}

// ======================================================== DEL E OCH F
//
// FÖREMÅLEN GENOM RITYTAN: meshar, instanser och punkter i en
// RIKTIG glaze::Canvas (Rit3-fönstren ovan) — det mesh_program_gate mäter i
// programmen direkt mäts här genom ritytans hela väg: formerna och
// primitiverna, residensen, `Lighting`/`MeshStyle` till blocket, gallringen
// och kompositen. FÖRVÄNTAT VÄRDE är grindens eget: ljusmodellens definition
// räknad här, och kamerorna med grindens egna ord för
// konventionen (camera.h) — en ORTOGRAFISK vy rakt uppifrån (elevation 90°,
// halva höjden H: världen (x, y) hamnar i (mitten + x·k, mitten − y·k) med
// k = halva ytans höjd / H fysiska px, norr uppåt) och en hålkamera från
// söder (azimut 0, elevation 0: punkten (x, y, z) i (mitten + f·x/d,
// mitten − f·z/d) med d = y + avståndet och f = halva höjden / tan(fov/2)).

// Den ortografiska vyn rakt uppifrån kring målet med halva höjden H.
glaze::Camera3D uppifrånKamera(const glaze::Point3& mål, double H)
{
    return glaze::Camera3D{.target      = mål,
                           .distance    = 20,
                           .elevation   = 90,
                           .projection  = glaze::Projection::Orthographic,
                           .orthoHeight = H};
}

// …och dess projektion i fönstrets fysiska px (målet i mitten).
struct Uppifrån {
    double cx = 0.0, cy = 0.0, k = 1.0;
    int    x(double wx) const { return static_cast<int>(std::floor(cx + wx * k)); }
    int    y(double wy) const { return static_cast<int>(std::floor(cy - wy * k)); }
};

Uppifrån uppifrån(const Rit3& r, double H)
{
    return Uppifrån{r.box.x + r.box.w / 2.0, r.box.y + r.box.h / 2.0,
                    r.box.h / 2.0 / H};
}

std::uint8_t ytKanal(double v)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
}

gfx::Color gånger(gfx::Color c, double f)
{
    return gfx::Color{ytKanal(c.r * f), ytKanal(c.g * f), ytKanal(c.b * f), 255};
}

std::string ytFärg(gfx::Color c)
{
    return std::to_string(c.r) + "," + std::to_string(c.g) + "," +
           std::to_string(c.b);
}

const gfx::Color kYtFärg{200, 100, 50, 255};   // ljusprovens färg
const gfx::Color kGrå{128, 128, 128, 255};

// En kvadrat i planet z — fyra hörn moturs sett ovanifrån (SV, SÖ, NÖ, NV),
// två trianglar.
glaze::MeshDesc kvadrat(double halv, double z)
{
    glaze::MeshDesc d;
    const float     h = static_cast<float>(halv), zz = static_cast<float>(z);
    d.positions = {{-h, -h, zz}, {h, -h, zz}, {h, h, zz}, {-h, h, zz}};
    d.indices   = {0, 1, 2, 0, 2, 3};
    return d;
}

// ------------------------------------------------------------------- E1
//
// LJUSET genom ritytan: en låda (primitivens enhetsmesh, skalad) vars
// översida pekar rakt upp, sedd uppifrån. Ljuset rakt nedåt ger färg ×
// (ambient + diffuse); N·L = 0,6 med exponenten 2 ger ambient + diffuse ·
// 0,36; ljuset underifrån med rimvektorn uppåt ger ambient + rim.
void fallE1()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    y.addShape(glaze::Shape::box({{-3, -3, -1}, {3, 3, 0}}, {.color = kYtFärg}));
    const auto prov = [&](const glaze::Lighting& l, double faktor,
                          const std::string& vad) {
        y.setLighting(l);
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::uint8_t* p      = b.at(u.x(0.4), u.y(0.4));
        const gfx::Color    väntad = gånger(kYtFärg, faktor);
        check(ärFärg(p, väntad, 3), "E1 " + vad,
              px(p) + " mot " + ytFärg(väntad));
    };
    prov({.direction = {0, 0, -1}, .ambient = 0.3f, .diffuse = 0.5f}, 0.8,
         "ytan rakt mot ljuset = färg × (ambient + diffuse)");
    prov({.direction = {0, -0.8, -0.6}, .ambient = 0.3f, .diffuse = 0.5f,
          .exponent = 2.0f},
         0.3 + 0.5 * 0.36, "N·L = 0,6 med exponenten 2: ambient + diffuse · 0,36");
    prov({.direction = {0, 0, 1}, .ambient = 0.3f, .diffuse = 0.5f,
          .rimDirection = {0, 0, 1}, .rim = 0.25f},
         0.55, "ljuset underifrån och rimvektorn uppåt: ambient + rim");
}

// ------------------------------------------------------------------- E2
//
// BAKSIDAN: en kvadrat med normalerna uppåt, sedd UNDERIFRÅN (elevation
// −90°), och ljuset underifrån. Tvåsidigt vänds baksidans normal och den
// belyses som framsidan (ambient + diffuse); ensidigt bara ambient.
void fallE2()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    glaze::Camera3D kam = uppifrånKamera({0, 0, 0}, 10);
    kam.elevation       = -90;
    y.setCamera(kam);
    const Uppifrån  u = uppifrån(r, 10);
    glaze::MeshDesc d = kvadrat(4, 0);
    d.normals.assign(4, glaze::Float3{0, 0, 1});
    y.addShape(glaze::Shape::mesh(glaze::Mesh(std::move(d)), {},
                                  {.color = kYtFärg}));
    for (const bool två : {true, false}) {
        y.setLighting({.direction = {0, 0, 1}, .ambient = 0.3f, .diffuse = 0.5f,
                       .twoSided = två});
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::uint8_t* p      = b.at(u.x(0.4), u.y(0.4));
        const gfx::Color    väntad = gånger(kYtFärg, två ? 0.8 : 0.3);
        check(ärFärg(p, väntad, 3),
              std::string("E2 baksidan ") +
                  (två ? "tvåsidigt: belyst som framsidan"
                       : "ensidigt: bara ambient"),
              px(p) + " mot " + ytFärg(väntad));
    }
}

// ------------------------------------------------------------------- E3
//
// DEN PLATTA NORMALEN: `Mesh::triangles` har inga normaler — ytan sedd
// uppifrån med ljuset rakt nedåt får sin framsidas normal ur derivatorna:
// färg × (ambient + diffuse); med ljuset underifrån bara ambient.
void fallE3()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    y.addShape(glaze::Shape::mesh(glaze::Mesh::triangles(
        {{-4, -4, 0}, {4, -4, 0}, {4, 4, 0}, {-4, -4, 0}, {4, 4, 0}, {-4, 4, 0}},
        kYtFärg)));
    for (const bool ned : {true, false}) {
        y.setLighting({.direction = {0, 0, ned ? -1.0 : 1.0}, .ambient = 0.3f,
                       .diffuse = 0.5f, .twoSided = false});
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::uint8_t* p      = b.at(u.x(0.4), u.y(0.4));
        const gfx::Color    väntad = gånger(kYtFärg, ned ? 0.8 : 0.3);
        check(ärFärg(p, väntad, 3),
              std::string("E3 den platta normalen: ljuset ") +
                  (ned ? "ovanifrån ger ambient + diffuse"
                       : "underifrån ger bara ambient"),
              px(p) + " mot " + ytFärg(väntad));
    }
}

// ------------------------------------------------------------------- E4
//
// TEXTUREN: en kvadrat med uv (NV hörnet u, v = 0, 0) och en 2 × 2-Bitmap
// med rad 0 överst — röd, grön / blå, gul — obelyst och med närmaste texel:
// kvadranterna i bildens ordning, rättvända.
void fallE4()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån     u = uppifrån(r, 10);
    const std::uint8_t px4[16] = {255, 0, 0, 255,   0,   200, 0, 255,
                                  0,   0, 255, 255, 255, 255, 0, 255};
    glaze::Bitmap bild(2, 2, px4, 8);
    bild.setFilter(gfx::ImageFilter::Nearest);
    glaze::MeshDesc d = kvadrat(4, 0);
    d.uvs             = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};   // SV, SÖ, NÖ, NV
    y.addShape(glaze::Shape::mesh(glaze::Mesh(std::move(d)), {},
                                  {.lit = false, .texture = bild}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const gfx::Color gul{255, 255, 0, 255};
    const bool ok = ärFärg(b.at(u.x(-2), u.y(2)), kRöd, 3) &&
                    ärFärg(b.at(u.x(2), u.y(2)), kGrön, 3) &&
                    ärFärg(b.at(u.x(-2), u.y(-2)), kBlå, 3) &&
                    ärFärg(b.at(u.x(2), u.y(-2)), gul, 3);
    check(ok, "E4 texturen rättvänd: rad 0 i norr, kolumn 0 i väster",
          px(b.at(u.x(-2), u.y(2))) + " / " + px(b.at(u.x(2), u.y(2))) + " / " +
              px(b.at(u.x(-2), u.y(-2))) + " / " + px(b.at(u.x(2), u.y(-2))));
}

// ------------------------------------------------------------------- E5
//
// RAMPEN: tre kvadrater med skalären 0, 0,5 och 1 och en ramp röd → grön →
// blå över [0, 1], obelysta: stoppens färger. Och utanför [min, max]
// klampas skalären (−1 ger rött, 3 blått).
void fallE5()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån  u = uppifrån(r, 10);
    glaze::MeshDesc d;
    const float     skalär[5] = {0.0f, 0.5f, 1.0f, -1.0f, 3.0f};
    for (int i = 0; i < 5; ++i) {
        const float x0 = -9.0f + 3.6f * i, x1 = x0 + 3.0f;
        const auto  bas = static_cast<std::uint32_t>(d.positions.size());
        d.positions.insert(d.positions.end(),
                           {{x0, -2, 0}, {x1, -2, 0}, {x1, 2, 0}, {x0, 2, 0}});
        d.scalars.insert(d.scalars.end(), 4, skalär[i]);
        d.indices.insert(d.indices.end(),
                         {bas, bas + 1, bas + 2, bas, bas + 2, bas + 3});
    }
    const gfx::Color grön{0, 255, 0, 255};
    y.addShape(glaze::Shape::mesh(
        glaze::Mesh(std::move(d)), {},
        {.lit = false,
         .ramp = {{{0.0f, kRöd}, {0.5f, grön}, {1.0f, kBlå}}, 0.0, 1.0}}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const gfx::Color väntad[5] = {kRöd, grön, kBlå, kRöd, kBlå};
    std::string      fick;
    bool             ok = true;
    for (int i = 0; i < 5; ++i) {
        const std::uint8_t* p = b.at(u.x(-7.5 + 3.6 * i), u.y(0));
        ok                    = ok && ärFärg(p, väntad[i], 4);
        fick += (i ? " / " : "") + px(p);
    }
    check(ok, "E5 rampens tre stopp vid min, mitten och max — och klampad "
              "utanför",
          fick);
}

// ------------------------------------------------------------------- E6
//
// MÄTTNADEN 0 ger luminansens grå (Rec. 709 på talen: 0,2126 R + 0,7152 G +
// 0,0722 B), obelyst. FÄRGJUSTERINGEN genom ritytan och
// `MeshStyle`: kontrasten 0 mittgrått, ljusheten 0,5 halva färgen, mättnaden
// 1,5 mättare (inte klampad till 1), och de tre tillsammans i ordningen
// ljushet, kontrast, mättnad — två kombinationer som ger ANDRA färger i en
// annan ordning. Och TEXTUREN: en 2 × 2-bild med mättnaden 0 ger varje texels
// luminans. Förväntat värde är definitionen räknad här (`justerad`).

// Färgjusteringens DEFINITION på 0–255: ljusheten multiplicerar, kontrasten
// sträcker kring mittgrått (127,5) och mättnaden blandar mot luminansen, i den
// ordningen och klampad efter varje steg.
gfx::Color justerad(gfx::Color c, double mättnad, double kontrast,
                    double ljushet)
{
    double k[3] = {static_cast<double>(c.r), static_cast<double>(c.g),
                   static_cast<double>(c.b)};
    for (double& v : k)
        v = std::clamp(v * ljushet, 0.0, 255.0);
    for (double& v : k)
        v = std::clamp(127.5 + (v - 127.5) * kontrast, 0.0, 255.0);
    const double l = 0.2126 * k[0] + 0.7152 * k[1] + 0.0722 * k[2];
    for (double& v : k)
        v = std::clamp(l + (v - l) * mättnad, 0.0, 255.0);
    return gfx::Color{ytKanal(k[0]), ytKanal(k[1]), ytKanal(k[2]), 255};
}

void fallE6()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    y.addShape(glaze::Shape::mesh(glaze::Mesh(kvadrat(4, 0)), {},
                                  {.color = kYtFärg, .lit = false,
                                   .saturation = 0.0f}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const double lum = 0.2126 * kYtFärg.r + 0.7152 * kYtFärg.g + 0.0722 * kYtFärg.b;
    const gfx::Color väntad{ytKanal(lum), ytKanal(lum), ytKanal(lum), 255};
    const std::uint8_t* p = b.at(u.x(0.4), u.y(0.4));
    check(ärFärg(p, väntad, 3), "E6 mättnaden 0 ger luminansens grå",
          px(p) + " mot " + ytFärg(väntad));

    // FÄRGJUSTERINGEN — samma kvadrat med en ny stil (`setShape`).
    struct Prov {
        float       mättnad, kontrast, ljushet;
        const char* vad;
    };
    const Prov prov[] = {
        {1.0f, 0.0f, 1.0f, "E6 kontrasten 0 ger mittgrått"},
        {1.0f, 1.0f, 0.5f, "E6 ljusheten 0,5 halverar färgen"},
        {1.5f, 1.0f, 1.0f, "E6 mättnaden 1,5 är mättare än bilden (inte klampad "
                           "till 1)"},
        {1.0f, 2.0f, 0.6f, "E6 ljusheten FÖRE kontrasten (0,6 och 2)"},
        // Kontrasten och mättnaden kommuterar utan klampning (båda är affina
        // och luminansens vikter summerar till 1) — 4 klampar två kanaler.
        {0.0f, 4.0f, 1.0f, "E6 kontrasten FÖRE mättnaden (4 och 0)"}};
    for (const Prov& pr : prov) {
        y.setShape(0, glaze::Shape::mesh(glaze::Mesh(kvadrat(4, 0)), {},
                                         {.color = kYtFärg, .lit = false,
                                          .saturation = pr.mättnad,
                                          .contrast = pr.kontrast,
                                          .brightness = pr.ljushet}));
        const Bild bj = r.bild();
        if (g_ritfel)
            return;
        const gfx::Color    v = justerad(kYtFärg, pr.mättnad, pr.kontrast, pr.ljushet);
        const std::uint8_t* q = bj.at(u.x(0.4), u.y(0.4));
        check(ärFärg(q, v, 3), pr.vad, px(q) + " mot " + ytFärg(v));
    }

    // TEXTUREN (MeshStyle::texture) justeras: mättnaden 0 på E4:s 2 × 2-bild
    // ger varje texels luminans.
    const std::uint8_t px4[16] = {255, 0, 0, 255,   0,   200, 0, 255,
                                  0,   0, 255, 255, 255, 255, 0, 255};
    glaze::Bitmap bild(2, 2, px4, 8);
    bild.setFilter(gfx::ImageFilter::Nearest);
    glaze::MeshDesc d = kvadrat(4, 0);
    d.uvs             = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};   // SV, SÖ, NÖ, NV
    y.setShape(0, glaze::Shape::mesh(glaze::Mesh(std::move(d)), {},
                                     {.lit = false, .saturation = 0.0f,
                                      .texture = bild}));
    const Bild bt = r.bild();
    if (g_ritfel)
        return;
    const gfx::Color texel[4] = {kRöd, kGrön, kBlå, gfx::Color{255, 255, 0, 255}};
    const double     hx[4] = {-2, 2, -2, 2}, hy[4] = {2, 2, -2, -2};
    bool             ok = true;
    std::string      fick;
    for (int i = 0; i < 4; ++i) {
        const gfx::Color    v = justerad(texel[i], 0.0, 1.0, 1.0);
        const std::uint8_t* q = bt.at(u.x(hx[i]), u.y(hy[i]));
        ok                    = ok && ärFärg(q, v, 3);
        fick += (i ? " / " : "") + px(q) + " mot " + ytFärg(v);
    }
    check(ok, "E6 texturen justeras: mättnaden 0 ger varje texels luminans", fick);
}

// ------------------------------------------------------------------- E7
//
// DIMMAN: med fogEnd strax framför ytan är andelen klampad till 1 — ytan får
// dimfärgen; med `MeshStyle::fog = false` sin egen (kontrollen).
void fallE7()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån   u = uppifrån(r, 10);
    const gfx::Color dimma{30, 60, 200, 255};
    y.setLighting({.fog = dimma, .fogStart = 0.0, .fogEnd = 0.001});
    for (const bool med : {true, false}) {
        y.setShapes({glaze::Shape::mesh(glaze::Mesh(kvadrat(4, 0)), {},
                                        {.color = kYtFärg, .lit = false,
                                         .fog = med})});
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::uint8_t* p      = b.at(u.x(0.4), u.y(0.4));
        const gfx::Color    väntad = med ? dimma : kYtFärg;
        check(ärFärg(p, väntad, 3),
              med ? "E7 dimman bortom fogEnd ger dimfärgen"
                  : "E7 utan MeshStyle::fog ingen dimma (kontrollen)",
              px(p) + " mot " + ytFärg(väntad));
    }
}

// ------------------------------------------------------------------- E8
//
// RTC: samma belysta låda med kameran kring origo och kring SWEREF
// (674 000, 6 580 000) ger samma bild ±2 per kanal — utan origot i double
// hade float-avrundningen (0,5 m vid 6,58·10⁶) flyttat kanterna flera px.
void fallE8()
{
    Bild bilder[2];
    for (int i = 0; i < 2; ++i) {
        Rit3 r;
        if (!r.öppna())
            return;
        glaze::Canvas&     y  = r.f.yta;
        const glaze::Point3 m = i == 0 ? glaze::Point3{0, 0, 0}
                                       : glaze::Point3{674000.0, 6580000.0, 0};
        y.setCamera(uppifrånKamera(m, 10));
        y.setLighting({.direction = {-0.3, -0.4, -0.866}, .ambient = 0.3f,
                       .diffuse = 0.6f});
        y.addShape(glaze::Shape::box({{m.x - 3.3, m.y - 2.1, -1},
                                      {m.x + 2.7, m.y + 3.9, 0}},
                                     {.color = kYtFärg}));
        y.addShape(glaze::Shape::sphere({m.x + 5.2, m.y - 1.3, 0.5},
                                        glaze::world(1.7f), {.color = kGrå}));
        bilder[i] = r.bild();
        if (g_ritfel)
            return;
    }
    int störst = 0, antal = 0;
    for (int py = 0; py < std::min(bilder[0].h, bilder[1].h); ++py)
        for (int px_ = 0; px_ < std::min(bilder[0].w, bilder[1].w); ++px_) {
            const std::uint8_t* a = bilder[0].at(px_, py);
            const std::uint8_t* b = bilder[1].at(px_, py);
            int d = 0;
            for (int k = 0; k < 3; ++k)
                d = std::max(d, std::abs(a[k] - b[k]));
            störst = std::max(störst, d);
            antal += d > 2;
        }
    check(bilder[0].w > 0 && störst <= 2,
          "E8 RTC: samma låda och sfär vid origo och vid SWEREF ger samma bild "
          "(±2 per kanal)",
          "störst " + std::to_string(störst) + ", " + std::to_string(antal) +
              " px skiljer > 2");
}

// ------------------------------------------------------------------- E9
//
// INSTANSERNA: en låda som prototyp på fyra platser, obelyst och
// vit, på en grå yta — A röd i (−6, 0); B grön i origo med giren 45° och
// skalan 3 × 1 (den långa axeln går SÖ–NV: giren är kompassens, medurs
// sett ovanifrån); C blå i (6, 3) med skalan 2; D med färgens alfa 0 i
// (−6, −4) — stilens vita.
void fallE9()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setBackground(kGrå);
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    y.addShape(glaze::Shape::instances(
        glaze::Shape::box({{-0.5, -0.5, 0}, {0.5, 0.5, 1}}),
        {{.at = {-6, 0, 0}, .color = kRöd},
         {.at = {0, 0, 0}, .yaw = 45, .scale = {3, 1, 1}, .color = kGrön},
         {.at = {6, 3, 0}, .scale = {2, 2, 2}, .color = kBlå},
         {.at = {-6, -4, 0}}},
        {.lit = false}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const auto vid = [&](double x, double yy) { return b.at(u.x(x), u.y(yy)); };
    check(ärFärg(vid(-6, 0), kRöd, 3) && ärFärg(vid(-6, -4), kVit, 3),
          "E9 instansens läge och färg; alfa 0 ger stilens färg",
          px(vid(-6, 0)) + " / " + px(vid(-6, -4)));
    // 1,2 längs den långa axeln (halva längden 1,5) mot SÖ, och 1,2 mot NÖ
    // (halva bredden 0,5).
    check(ärFärg(vid(0.85, -0.85), kGrön, 3) && ärFärg(vid(-0.85, 0.85), kGrön, 3) &&
              ärFärg(vid(0.85, 0.85), kGrå, 3),
          "E9 giren (45° medurs) och den olikformiga skalan: lång SÖ–NV, smal "
          "NÖ–SV",
          px(vid(0.85, -0.85)) + " / " + px(vid(-0.85, 0.85)) + " / " +
              px(vid(0.85, 0.85)));
    check(ärFärg(vid(6.8, 3.8), kBlå, 3) && ärFärg(vid(7.3, 3), kGrå, 3),
          "E9 skalan 2: lådan 2 × 2 kring (6, 3)",
          px(vid(6.8, 3.8)) + " / " + px(vid(7.3, 3)));
}

// ------------------------------------------------------------------- F
//
// PUNKTERNA uppifrån: täckningen (summan av alfan) kring en röd
// punkt på vitt ur grön- och blåkanalens fall.
double täckning(const Bild& b, int cx, int cy, int halv)
{
    double t = 0.0;
    for (int py = cy - halv; py <= cy + halv; ++py)
        for (int px_ = cx - halv; px_ <= cx + halv; ++px_) {
            const std::uint8_t* p = b.at(px_, py);
            t += (255.0 - p[1]) / 255.0;
        }
    return t;
}

// F1 en kvadrat om 6 logiska px täcker (6 · skalan)² px², F2 en cirkel
// π · (3 · skalan)², ochen kvadrat om 1,5 mm (1,5 ·
// fönstrets px/mm)² — enheten når programmet; ±5 %.
void fallF12()
{
    for (int fall = 0; fall < 3; ++fall) {
        Rit3 r;
        if (!r.öppna())
            return;
        glaze::Canvas& y = r.f.yta;
        y.setCamera(uppifrånKamera({0, 0, 0}, 10));
        const Uppifrån u    = uppifrån(r, 10);
        const bool     rund = fall == 1, mm = fall == 2;
        y.addShape(glaze::Shape::points3(
            {{0, 0, 0}},
            {.size  = mm ? glaze::mm(1.5f) : glaze::px(6.0f),
             .shape = rund ? glaze::MarkerShape::Circle : glaze::MarkerShape::Square,
             .color = kRöd}));
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const double s = mm ? 1.5 * r.f.yta.behaviorCtx().pxPerMm : 6.0 * r.skala;
        const double väntad = rund ? 3.14159265358979 * s * s / 4.0 : s * s;
        const double t      = täckning(b, u.x(0), u.y(0), 16);
        check(std::abs(t - väntad) <= 0.05 * väntad,
              rund ? "F2 en rund punkt om 6 px täcker π · 3² (× skalan²)"
              : mm ? "F1 en kvadratisk punkt om 1,5 mm täcker (1,5 · px/mm)²"
                   : "F1 en kvadratisk punkt om 6 px täcker 6² (× skalan²)",
              tal(t) + " px² mot " + tal(väntad));
    }
}

// ------------------------------------------------------------------ E10
//
// ÄNDRINGSLOGGEN genom ritytan: `Mesh::setColors` på
// en synlig mesh syns i nästa ritning, och `setPositions` flyttar den — utan
// att formen sätts om (appen invaliderar ritytan, mesh.h).
void fallE10()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    glaze::Mesh    m = glaze::Mesh::triangles(
        {{-4, -4, 0}, {4, -4, 0}, {4, 4, 0}, {-4, -4, 0}, {4, 4, 0}, {-4, 4, 0}},
        kRöd);
    y.addShape(glaze::Shape::mesh(m, {}, {.lit = false}));
    Bild b = r.bild();
    if (g_ritfel)
        return;
    const bool före = ärFärg(b.at(u.x(1), u.y(1)), kRöd, 3);
    const std::vector<gfx::Color> blått(6, kBlå);
    m.setColors(0, blått);
    b = r.bild();
    if (g_ritfel)
        return;
    check(före && ärFärg(b.at(u.x(1), u.y(1)), kBlå, 3),
          "E10 setColors på en synlig mesh syns i nästa ritning",
          px(b.at(u.x(1), u.y(1))));
    // Lägena 6 m österut (relativa meshens origo — lådans mitt).
    std::vector<glaze::Float3> österut;
    for (const glaze::Float3 p : {glaze::Float3{-4, -4, 0}, {4, -4, 0}, {4, 4, 0},
                                  {-4, -4, 0}, {4, 4, 0}, {-4, 4, 0}})
        österut.push_back({p.x + 6, p.y, p.z});
    m.setPositions(0, österut);
    b = r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(b.at(u.x(8), u.y(1)), kBlå, 3) &&
              ärFärg(b.at(u.x(-3), u.y(1)), kVit, 3),
          "E10 setPositions flyttar meshen (bara lägena laddas upp)",
          px(b.at(u.x(8), u.y(1))) + " / " + px(b.at(u.x(-3), u.y(1))));
}

// F3 rampen (min, mitt, max och klampad), F4 djuptestet mot en mesh, F5
// färgen per punkt — ett fönster.
void fallF345()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån   u = uppifrån(r, 10);
    const gfx::Color grön{0, 255, 0, 255};
    // Rampen: skalären 0, 0,5, 1 och 2 i en rad i norr.
    y.addShape(glaze::Shape::pointCloud(
        glaze::PointCloud({{-6, 6, 0}, {-2, 6, 0}, {2, 6, 0}, {6, 6, 0}},
                          {0, 0, 0}, {}, {0.0f, 0.5f, 1.0f, 2.0f}),
        {.size  = glaze::px(14.0f),
         .color = kVit,
         .ramp  = {{{0.0f, kRöd}, {0.5f, grön}, {1.0f, kBlå}}, 0.0, 1.0}}));
    // Färgen per punkt i söder.
    y.addShape(glaze::Shape::pointCloud(
        glaze::PointCloud({{-4, -6, 0}, {4, -6, 0}}, {0, 0, 0}, {kRöd, kBlå}),
        {.size = glaze::px(14.0f), .color = kVit}));
    // Djupet: en röd punkt OVANFÖR en grå ruta och en blå UNDER, punkterna
    // FÖRE rutan i lagret — djupet och inte ordningen avgör.
    y.addShape(glaze::Shape::points3({{-2, 0, 1}}, {.size = glaze::px(14.0f),
                                                    .color = kRöd}));
    y.addShape(glaze::Shape::points3({{2, 0, -1}}, {.size = glaze::px(14.0f),
                                                    .color = kBlå}));
    y.addShape(glaze::Shape::mesh(glaze::Mesh(kvadrat(4, 0)), {},
                                  {.color = kGrå, .lit = false}));
    // Rutan täcker (−4, 4) i y — skär norr- och söderraderna inte.
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const auto vid = [&](double x, double yy) { return b.at(u.x(x), u.y(yy)); };
    check(ärFärg(vid(-6, 6), kRöd, 4) && ärFärg(vid(-2, 6), grön, 4) &&
              ärFärg(vid(2, 6), kBlå, 4) && ärFärg(vid(6, 6), kBlå, 4),
          "F3 rampens min, mitt och max — och klampad över max",
          px(vid(-6, 6)) + " / " + px(vid(-2, 6)) + " / " + px(vid(2, 6)) +
              " / " + px(vid(6, 6)));
    check(ärFärg(vid(-2, 0), kRöd, 3) && ärFärg(vid(2, 0), kGrå, 3),
          "F4 djuptestet mot en mesh: punkten ovanför syns, den under inte",
          px(vid(-2, 0)) + " / " + px(vid(2, 0)));
    check(ärFärg(vid(-4, -6), kRöd, 3) && ärFärg(vid(4, -6), kBlå, 3),
          "F5 färgen per punkt", px(vid(-4, -6)) + " / " + px(vid(4, -6)));
}

// F6 SKÄRMSTORLEKEN: en sfär med radien mm(2) är 4 mm bred — 4 × fönstrets
// px/mm — nära (djupet 10) och fjärran (djupet 40), ±1,5 px, sedd genom en
// hålkamera från söder; en sfär med världsradien 0,5 krymper fyra gånger.
void fallF6()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 20,
                                .elevation = 0, .fov = 45});
    const glaze::MeshStyle röd{.color = kRöd, .lit = false};
    y.addShape(glaze::Shape::sphere({-2, -10, 0}, glaze::mm(2.0f), röd));
    y.addShape(glaze::Shape::sphere({3, 20, 0}, glaze::mm(2.0f), röd));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const double f  = r.box.h / 2.0 / std::tan(22.5 * 3.14159265358979 / 180.0);
    const double cx = r.box.x + r.box.w / 2.0, cy = r.box.y + r.box.h / 2.0;
    // Bredden i raden genom mitten: pixlar med minst halv täckning.
    const auto bredd = [&](double wx, double d) {
        const int sx = static_cast<int>(std::floor(cx + f * wx / d));
        const int sy = static_cast<int>(std::floor(cy));
        int       n  = 0;
        for (int px_ = sx - 40; px_ <= sx + 40; ++px_)
            n += b.at(px_, sy)[1] < 128;
        return n;
    };
    const double pxPerMm = r.f.yta.behaviorCtx().pxPerMm;
    const double väntad  = 4.0 * pxPerMm;
    const int    nära    = bredd(-2, 10);
    const int    fjärran = bredd(3, 40);
    check(std::abs(nära - väntad) <= 1.5 && std::abs(fjärran - väntad) <= 1.5,
          "F6 en sfär med radien mm(2) är 4 mm bred nära och fjärran",
          std::to_string(nära) + " och " + std::to_string(fjärran) + " px mot " +
              tal(väntad) + " (" + tal(pxPerMm) + " px/mm)");
}

// ============================================================ DEL I
//
// DEL-VYERNA, BILDEN OCH MÅLARENS RENDERLÄGE genom en RIKTIG
// glaze::Canvas i Rit3-fönstren ovan. Förväntat värde är grindens eget: vyns ruta ur
// style3d.h:s ord (inte target3d.cpp:s `viewRect`), den ortografiska vyns
// projektion med grindens egen trigonometri ur camera.h:s konvention, och
// ritytans egen skärmbild som förväntat värde för bilden.

// Vyns ruta i fönstrets fysiska px för ankaret TopRight: vyns övre högra
// hörn i ritytans, flyttat (dx, dy) logiska px — måtten w × h logiska px.
gfx::Rectf vyÖverstTillHöger(const Rit3& r, double dx, double dy, double w,
                             double h)
{
    return gfx::Rectf{static_cast<float>(r.box.x + r.box.w - w * r.skala +
                                         dx * r.skala),
                      static_cast<float>(r.box.y + dy * r.skala),
                      static_cast<float>(w * r.skala),
                      static_cast<float>(h * r.skala)};
}

// En vy ritytan kan ta emot: ortografisk från sidan (elevation 0) mot
// origo, halvhöjden 2 världsenheter.
const glaze::View kSidovy{
    .anchor = glaze::TextAnchor::TopRight, .offset = {-10, 10}, .width = 100,
    .height = 100,
    .camera = glaze::Camera3D{.target = {0, 0, 0}, .distance = 5,
                              .elevation = 0,
                              .projection = glaze::Projection::Orthographic,
                              .orthoHeight = 2}};

// Punkten p i en ORTOGRAFISK vy (halvhöjden h, målet i origo, ingen roll) med
// azimut och elevation — grindens egen trigonometri ur camera.h:s
// konvention: blicken (sin az · cos el, cos az · cos el, −sin el), upp längs
// elevationens tangent (sin az · sin el, cos az · sin el, cos el), och höger =
// blicken × upp = (cos az, −sin az, 0).
void ortoPunkt(const gfx::Rectf& v, double h, double azDeg, double elDeg,
               glaze::Point3 p, double& sx, double& sy)
{
    const double grad = 3.14159265358979323846 / 180.0;
    const double az = azDeg * grad, el = elDeg * grad;
    const double hx = std::cos(az), hy = -std::sin(az);
    const double ux = std::sin(az) * std::sin(el), uy = std::cos(az) * std::sin(el),
                 uz = std::cos(el);
    const double ppu = v.h / 2.0 / h;
    sx = v.x + v.w / 2.0 + (p.x * hx + p.y * hy) * ppu;
    sy = v.y + v.h / 2.0 - (p.x * ux + p.y * uy + p.z * uz) * ppu;
}

// Tyngdpunkten (pixelmitt) för de röda pixlarna i rutan; antalet i `n`.
void rödTyngdpunkt(const Bild& b, const gfx::Rectf& ruta, double& cx,
                   double& cy, int& n)
{
    double sx = 0.0, sy = 0.0;
    n = 0;
    for (int y = static_cast<int>(std::ceil(ruta.y));
         y < static_cast<int>(ruta.y + ruta.h); ++y)
        for (int x = static_cast<int>(std::ceil(ruta.x));
             x < static_cast<int>(ruta.x + ruta.w); ++x) {
            const std::uint8_t* p = b.at(x, y);
            if (p[0] > 200 && p[1] < 80 && p[2] < 80) {
                sx += x + 0.5;
                sy += y + 0.5;
                ++n;
            }
        }
    cx = n ? sx / n : 0.0;
    cy = n ? sy / n : 0.0;
}

// ------------------------------------------------------------- I1 och I3
//
// EGET DJUP: huvudvyn ser rakt ned på en röd mark (z = 0) med närplanet satt
// nära den — markens djup är ≈ 0,1 (perspektivets (1 − near/d) · far/(far −
// near)). Vyn uppe till höger ser en blå stav från sidan ortografiskt, mitt i
// sitt eget djupintervall (≈ 0,5). I ett gemensamt djup hade staven fallit på
// Less mot marken; med vyns eget syns den. Kontrollen: i huvudvyn skyms en
// stav UNDER marken (huvudvyns djup finns och verkar).
// GENOMSKINLIG BAKGRUND (I3): där vyn inte ritar syns huvudvyns röda; en
// ogenomskinlig grön bakgrund fyller rutan men staven står kvar.
void fallI1()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 10,
                                .elevation = 90, .nearPlane = 9,
                                .farPlane = 1000});
    y.setLayerPlacement(0, glaze::LayerPlacement::World);
    y.addShape(0, glaze::Shape::rect({-100, -100, 200, 200}, kRöd));
    // Huvudvyns stav under marken — vyn ser den från sidan (en halv enhet
    // under mitten), men den hör inte till vyn.
    const int under = y.addLayer("Under");
    y.addShape(under, glaze::Shape::line3({-1.8, 0, -0.5}, {1.8, 0, -0.5},
                                          glaze::Stroke{.paint = kBlå,
                                                        .width = glaze::px(30.0f)}));
    const int stav = y.addLayer("Stav");
    y.addShape(stav, glaze::Shape::line3({0, 0, -1}, {0, 0, 1},
                                         glaze::Stroke{.paint = kBlå,
                                                       .width = glaze::px(40.0f)}));
    const int nr = y.addView(kSidovy);
    y.setLayerViews(stav, {nr});
    check(nr == 1 && y.viewCount() == 1,
          "I1 addView svarar med vyns nummer 1", std::to_string(nr));
    Bild b = r.bild();
    if (g_ritfel)
        return;
    const gfx::Rectf v  = vyÖverstTillHöger(r, -10, 10, 100, 100);
    const int        cx = static_cast<int>(v.x + v.w / 2.0f);
    const int        cy = static_cast<int>(v.y + v.h / 2.0f);
    check(ärFärg(b.at(cx, cy), kBlå),
          "I1 staven i vyn uppe till höger skyms inte av huvudscenens röda mark "
          "— vyn har eget djup",
          px(b.at(cx, cy)));
    const std::optional<glaze::Point> o = y.toLocal(glaze::Point{0, 0});
    check(o && ärFärg(b.at(r.x(o->x), r.y(o->y)), kRöd),
          "I1 kontrollen: i huvudvyn skyms en stav under marken av den",
          o ? px(b.at(r.x(o->x), r.y(o->y))) : "ingen punkt");

    // Vyns vänstra del — utanför staven (halva bredden 20 px av vyns 50).
    const int sx = static_cast<int>(v.x + v.w * 0.15f);
    check(ärFärg(b.at(sx, cy), kRöd),
          "I3 genomskinlig bakgrund (förvalet): huvudvyn syns där vyn inte "
          "ritar",
          px(b.at(sx, cy)));
    // Huvudvyns stav under marken hade legat 1,4 enheter till höger och en
    // halv under vyns mitt (ortografiskt, v.h / 4 px per enhet) — utanför
    // vyns egen stav.
    const float ppu = v.h / 4.0f;
    const int   ux  = static_cast<int>(v.x + v.w / 2.0f + 1.4f * ppu);
    const int   uy  = static_cast<int>(v.y + v.h / 2.0f + 0.5f * ppu);
    check(ärFärg(b.at(ux, uy), kRöd),
          "I1 ett lager utanför vyn ritas inte i den (huvudvyns stav)",
          px(b.at(ux, uy)));
    glaze::View grön = kSidovy;
    grön.background  = kGrön;
    y.setView(nr, grön);
    b = r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(b.at(sx, cy), kGrön) && ärFärg(b.at(cx, cy), kBlå) &&
              ärFärg(b.at(static_cast<int>(v.x) - 6, cy), kRöd),
          "I3 ogenomskinlig bakgrund fyller vyns ruta, staven står kvar och "
          "huvudvyn ritar utanför rutan",
          px(b.at(sx, cy)) + " / " + px(b.at(cx, cy)) + " / " +
              px(b.at(static_cast<int>(v.x) - 6, cy)));

    // Vyn tas bort: lagret som bara låg i den ritas ingenstans.
    y.removeView(nr);
    b = r.bild();
    if (g_ritfel)
        return;
    check(y.viewCount() == 0 && y.layerViews(stav).empty() &&
              ärFärg(b.at(cx, cy), kRöd),
          "I1 removeView: vyn och lagrets plats i den är borta — huvudvyns "
          "mark syns där vyn låg",
          px(b.at(cx, cy)));
}

// ------------------------------------------------------------------ I1b
//
// VYNS KLIPPLAN UR SINA EGNA LAGER: huvudvyn står vid SWEREF
// och vyn vid origo. Två korsande Line3 i vyn 0,0005 världsenheter isär längs
// blicken, den BORTRE ritad först: med vyns egna lager som djupintervall
// (≈ 0,5 enheter) vinner den närmare med Less. Med hela scenens låda hade
// intervallet blivit miljontals enheter, de två djupen samma tal i D32F, och
// den bortre stått kvar (navigeringskuben i en SWEREF-karta).
void fallI1b()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {674000, 6580000, 0}, .distance = 50,
                                .elevation = 60});
    y.addShape(0, glaze::Shape::line3({673990, 6580000, 0}, {674010, 6580000, 0},
                                      glaze::Stroke{.paint = kGrön,
                                                    .width = glaze::px(8.0f)}));
    const int kors = y.addLayer("Kors");
    // Blicken är +y: den bortre (röd, lodrät) på y = 0,0005, den närmare
    // (blå, vågrät) på y = 0.
    y.addShape(kors, glaze::Shape::line3({0, 0.0005, -1}, {0, 0.0005, 1},
                                         glaze::Stroke{.paint = kRöd,
                                                       .width = glaze::px(24.0f)}));
    y.addShape(kors, glaze::Shape::line3({-1, 0, 0}, {1, 0, 0},
                                         glaze::Stroke{.paint = kBlå,
                                                       .width = glaze::px(24.0f)}));
    y.setLayerViews(kors, {y.addView(kSidovy)});
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const gfx::Rectf v  = vyÖverstTillHöger(r, -10, 10, 100, 100);
    const int        cx = static_cast<int>(v.x + v.w / 2.0f);
    const int        cy = static_cast<int>(v.y + v.h / 2.0f);
    // Staven når en världsenhet upp, en fjärdedel av vyns höjd över mitten;
    // den vågräta är 24 px bred.
    const int        ox = static_cast<int>(v.x + v.w / 2.0f);
    const int        oy = static_cast<int>(v.y + v.h * 0.3f);
    check(ärFärg(b.at(ox, oy), kRöd),
          "I1b kontrollen: den bortre staven ritas (utanför korsningen)",
          px(b.at(ox, oy)));
    check(ärFärg(b.at(cx, cy), kBlå),
          "I1b vyns klipplan ur dess egna lager: den närmare vinner i "
          "korsningen fast huvudvyn står vid SWEREF",
          px(b.at(cx, cy)));
}

// ------------------------------------------------------------------- I2
//
// FOLLOWROTATION: en röd markör i (0, 1,2, 0) i vyn, som är ortografisk med
// egen azimut 0 och elevation 0 — där ligger punkten längs blicken, mitt i
// vyn. Med `followRotation` står den där HUVUDKAMERANS vinklar lägger den
// (grindens egen projektion), också när huvudkameran vrids — och när
// huvudvyn är kartkameran (bäringen är azimuten, elevationen 90 − tilten).
void fallI2()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    // Huvudkameran siktar PÅ markören: huvudvyn har inga egna lager, så dess
    // klipplan räknas ur målet ensamt — och målet ligger alltid innanför dem.
    // Ritad i huvudvyn hade markören alltså synts mitt i ytan, utanför vyn.
    const glaze::Point3 p{0, 1.2, 0};
    y.setCamera(glaze::Camera3D{.target = p, .distance = 10, .azimuth = 40,
                                .elevation = 25});
    y.addShape(0, glaze::Shape::marker3(
                      p, glaze::MarkerStyle{.shape = glaze::MarkerShape::Square,
                                            .size  = glaze::px(10.0f),
                                            .fill  = kRöd}));
    glaze::View vy     = kSidovy;
    vy.followRotation  = true;
    y.setLayerViews(0, {y.addView(vy)});
    const gfx::Rectf v = vyÖverstTillHöger(r, -10, 10, 100, 100);

    const auto mät = [&](const std::string& namn, double az, double el) {
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        double ex = 0.0, ey = 0.0, cx = 0.0, cy = 0.0;
        int    n  = 0;
        ortoPunkt(v, 2.0, az, el, p, ex, ey);
        rödTyngdpunkt(b, v, cx, cy, n);
        check(n >= 40 && std::abs(cx - ex) <= 2.0 && std::abs(cy - ey) <= 2.0,
              "I2 " + namn, std::to_string(n) + " röda px i (" + tal(cx) + ", " +
                                tal(cy) + "), väntat (" + tal(ex) + ", " +
                                tal(ey) + ")");
    };
    mät("followRotation: markören står där huvudkamerans azimut 40° och "
        "elevation 25° lägger den",
        40, 25);
    // I8: huvudvyn och vyn placerar sina etiketter i var sin
    // plats — två ritningar i följd med samma kameror placerar inte om. Med
    // en delad plats hade varje ritning placerat två gånger (huvudvyns och
    // vyns indata turas om).
    const auto placeringar = [&] {
        const glaze::Ctx c = y.behaviorCtx();
        return c.canvas ? c.canvas->labelPlacements() : 0;
    };
    r.bild();
    const std::uint64_t p0 = placeringar();
    r.bild();
    r.bild();
    check(!g_ritfel && placeringar() == p0,
          "I8 huvudvyn och vyn har var sin etikettplats: två ritningar i följd "
          "placerar inte om",
          "+" + std::to_string(placeringar() - p0) + " placeringar");
    {
        // Markören är huvudkamerans mål — ritad i huvudvyn hade den synts
        // mitt i ytan, utanför vyns ruta.
        const Bild b       = r.bild();
        int        utanför = 0;
        for (int py = static_cast<int>(std::ceil(r.box.y));
             py < static_cast<int>(r.box.y + r.box.h); ++py)
            for (int px_ = static_cast<int>(std::ceil(r.box.x));
                 px_ < static_cast<int>(r.box.x + r.box.w); ++px_) {
                if (px_ >= v.x - 1 && px_ < v.x + v.w + 1 && py >= v.y - 1 &&
                    py < v.y + v.h + 1)
                    continue;
                const std::uint8_t* q = b.at(px_, py);
                utanför += q[0] > 200 && q[1] < 80 && q[2] < 80;
            }
        check(!g_ritfel && utanför == 0,
              "I2 lagret i vyn ritas inte i huvudvyn",
              std::to_string(utanför) + " röda px utanför vyn");
    }
    vy.followRotation = false;
    y.setView(1, vy);
    mät("kontrollen: utan followRotation står den där vyns egen kamera lägger "
        "den (mitt i vyn)",
        0, 0);
    vy.followRotation = true;
    y.setView(1, vy);
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 10,
                                .azimuth = 120, .elevation = 25});
    mät("followRotation följer en ny huvudkamera (azimut 120°)", 120, 25);
    y.setYUp(true);
    y.setCamera(glaze::Camera{.center = {0, 0}, .zoom = 20, .bearing = 30,
                              .pitch = 40});
    mät("followRotation med KARTKAMERAN: bäringen 30° som azimut, tilten 40° "
        "som elevationen 50°",
        30, 50);
}

// ---------------------------------------------------------- I4 och I5
//
// 3D-SCENEN TILL EN BILD: samma kamera i ritytans storlek ger SAMMA PIXLAR som
// ritytan på skärmen (±1 per kanal; de rundade hörnen undantagna — där är
// skärmens bild fönstrets yta), också med fönstret dolt, och inuti en ritning
// ger den ingen bild. Scenen har en yta i ett World-lager, en Line3, en
// kolliderande text3 och en Line3 ur `onPaint`. Förväntat värde är ritytans egen
// skärmbild — bilden ritas genom samma lager, så det som INTE mäts här är
// att lagren ritar rätt (det gör del D och G).
// SKÄRMENS PLACERING ORÖRD (I5): en bild med en ANNAN kamera
// placerar sina etiketter för sig — nästa skärmritning ser samma indata som
// vid förra ritningen och placerar inte om.
void fallI4()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas&        y      = r.f.yta;
    const glaze::Camera3D kamera{.target = {0, 0, 0.5}, .distance = 12,
                                 .azimuth = 30, .elevation = 35};
    y.setCamera(kamera);
    y.setLayerPlacement(0, glaze::LayerPlacement::World);
    y.addShape(0, glaze::Shape::polygon({{-3, -3}, {3, -3}, {3, 3}, {-3, 3}},
                                        kBlå));
    const int rymd = y.addLayer("Rymd");
    y.addShape(rymd, glaze::Shape::line3({-4, 0, 0.5}, {4, 0, 2},
                                         glaze::Stroke{.paint = kRöd,
                                                       .width = glaze::px(6.0f)}));
    y.addShape(rymd, glaze::Shape::text3({0, 0, 3}, "Hej",
                                         glaze::TextStyle{.size  = glaze::px(18.0f),
                                                          .color = kSvart,
                                                          .halo  = kVit}));
    bool                         provat = false;
    std::optional<glaze::Bitmap> inne;
    y.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        if (!provat) {
            provat = true;
            inne   = y.renderToBitmap(16, 16, kamera);   // inuti en ritning
        }
        e.painter.drawLine3({0, -4, 0}, {0, 4, 0},
                            glaze::Stroke{.paint = kGrön, .width = glaze::px(4.0f)});
    };
    const Bild s = r.bild();
    if (g_ritfel)
        return;
    check(provat && !inne, "I4 renderToBitmap inuti en ritning ger ingen bild");

    const int x0 = static_cast<int>(std::floor(r.box.x));
    const int y0 = static_cast<int>(std::floor(r.box.y));
    const int w  = static_cast<int>(std::ceil(r.box.x + r.box.w)) - x0;
    const int h  = static_cast<int>(std::ceil(r.box.y + r.box.h)) - y0;
    check(r.box.x == x0 && r.box.y == y0 && r.box.w == w && r.box.h == h,
          "I4 ritytans ruta ligger på hela px — bilden och skärmen har samma "
          "ruta",
          tal(r.box.x) + ", " + tal(r.box.y) + ", " + tal(r.box.w) + " × " +
              tal(r.box.h));
    // De rundade hörnen undantas: kvadrater om två radier.
    const int rad = static_cast<int>(std::ceil(r.f.style().cornerRadius * r.skala));
    const auto jämför = [&](const glaze::Bitmap& bm, int& störst, int& röd,
                            int& blå, int& grön) {
        störst = 0;
        röd = blå = grön = 0;
        const std::uint8_t* q = bm.pixels();
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const bool hörn = (i < 2 * rad || i >= w - 2 * rad) &&
                                  (j < 2 * rad || j >= h - 2 * rad);
                if (hörn)
                    continue;
                const std::uint8_t* p = s.at(x0 + i, y0 + j);
                const std::uint8_t* b = q + (static_cast<std::size_t>(j) * w + i) * 4;
                for (int k = 0; k < 3; ++k)
                    störst = std::max(störst, std::abs(p[k] - b[k]));
                röd += b[0] > 200 && b[1] < 80 && b[2] < 80;
                blå += b[2] > 200 && b[0] < 80 && b[1] < 80;
                grön += b[1] > 150 && b[0] < 80 && b[2] < 80;
            }
    };
    std::optional<glaze::Bitmap> bm = y.renderToBitmap(w, h, kamera);
    check(bm && bm->valid() && bm->width() == w && bm->height() == h,
          "I4 renderToBitmap ger en bild i ritytans storlek",
          std::to_string(w) + " × " + std::to_string(h));
    if (!bm || !bm->valid() || bm->width() != w || bm->height() != h)
        return;
    int störst = 0, röd = 0, blå = 0, grön = 0;
    jämför(*bm, störst, röd, blå, grön);
    check(röd >= 50 && blå >= 500 && grön >= 50,
          "I4 bilden bär scenen: ytan, Line3 och målarens linje",
          std::to_string(röd) + " röda, " + std::to_string(blå) + " blå, " +
              std::to_string(grön) + " gröna px");
    check(störst <= 1,
          "I4 renderToBitmap = samma region på skärmen med samma kamera (±1 per "
          "kanal, de rundade hörnen undantagna)",
          "största skillnad " + std::to_string(störst));

    // I5: en annan kamera — bilden placerar för sig, skärmen står orörd.
    const auto placeringar = [&] {
        const glaze::Ctx c = r.f.yta.behaviorCtx();
        return c.canvas ? c.canvas->labelPlacements() : 0;
    };
    const std::uint64_t p0 = placeringar();
    glaze::Camera3D     annan = kamera;
    annan.azimuth             = 120;
    const std::optional<glaze::Bitmap> bm3 = y.renderToBitmap(w, h, annan);
    const std::uint64_t p1 = placeringar();
    const Bild          s2 = r.bild();
    if (g_ritfel)
        return;
    const std::uint64_t p2 = placeringar();
    check(bm3 && p1 == p0 + 1 && p2 == p1,
          "I5 en bild med en annan kamera placerar sina etiketter för sig; "
          "nästa skärmritning placerar inte om",
          "bilden +" + std::to_string(p1 - p0) + ", skärmen +" +
              std::to_string(p2 - p1));
    int skärmskillnad = 0;
    for (std::size_t i = 0; i < s.px.size() && i < s2.px.size(); ++i)
        skärmskillnad = std::max(skärmskillnad, std::abs(s.px[i] - s2.px[i]));
    check(s.px.size() == s2.px.size() && skärmskillnad == 0,
          "I5 skärmens bild är densamma efter bilden med den andra kameran",
          "största skillnad " + std::to_string(skärmskillnad));

    // Också med fönstret DOLT (hide — fönstret och dess renderare finns kvar).
    r.f.hide();
    const std::optional<glaze::Bitmap> dold = y.renderToBitmap(w, h, kamera);
    check(dold && dold->valid() && dold->width() == w && dold->height() == h,
          "I4 renderToBitmap med fönstret dolt ger en bild");
    if (!dold || !dold->valid() || dold->width() != w || dold->height() != h)
        return;
    jämför(*dold, störst, röd, blå, grön);
    check(störst <= 1 && röd >= 50 && blå >= 500,
          "I4 med fönstret dolt: samma region som skärmen (±1 per kanal)",
          "största skillnad " + std::to_string(störst));
}

// ------------------------------------------------------------------- I6
//
// MÅLARENS LINE3 MED EGET RENDERLÄGE: rakt ovanifrån på en röd mark (z = 0)
// ritar `onPaint` en blå Line3 UNDER marken (z = −1). Förvalet (inget djup)
// lägger den ovanpå; Less med skrivning döljer den bakom marken; Greater utan
// skrivning visar den just för att den ligger bakom (dolda kanter);
// ett `save()` lägger tillbaka förvalet. En HUD i `screenSpace()` ritas
// oavsett läge.
void fallI6()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 10,
                                .elevation = 90, .nearPlane = 1,
                                .farPlane = 100});
    y.setLayerPlacement(0, glaze::LayerPlacement::World);
    y.addShape(0, glaze::Shape::rect({-100, -100, 200, 200}, kRöd));
    int fall = 0;
    y.onPaint = [&fall](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        if (fall == 1)
            p.setRenderState(glaze::RenderState{});
        if (fall == 2)
            p.setRenderState(glaze::RenderState{.depth = glaze::DepthTest::Greater,
                                                .depthWrite = false});
        {
            auto sparat = p.save();
            if (fall == 3)
                p.setRenderState(glaze::RenderState{});
        }
        p.drawLine3({-2, 0, -1}, {2, 0, -1},
                    glaze::Stroke{.paint = kBlå, .width = glaze::px(20.0f)});
        auto sparat = p.save();
        p.screenSpace();
        p.fillRect({10, 10, 20, 20}, kGrön);
    };
    const std::optional<glaze::Point> o = y.toLocal(glaze::Point{0, 0});
    if (!o) {
        check(false, "I6 marken mitt i ytan");
        return;
    }
    const char* namn[4] = {"förvalet (inget djup) ritar linjen ovanpå marken",
                           "Less med skrivning döljer linjen under marken",
                           "Greater utan skrivning visar linjen bakom marken",
                           "save() lägger tillbaka förvalet"};
    const gfx::Color väntat[4] = {kBlå, kRöd, kBlå, kBlå};
    for (fall = 0; fall < 4; ++fall) {
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::uint8_t* mitt = b.at(r.x(o->x), r.y(o->y));
        const std::uint8_t* hud  = b.at(r.x(20), r.y(20));
        check(ärFärg(mitt, väntat[fall]) && ärFärg(hud, kGrön),
              std::string("I6 målarens Line3: ") + namn[fall] +
                  " — HUD:en i screenSpace() ritas",
              px(mitt) + ", HUD " + px(hud));
    }
}

// ------------------------------------------------------------------- I7
//
// KOLLEKTIONSVOKABULÄREN: vyernas nummer, avslagen och
// lagrens vylistor när vyer tas bort eller fylls om — ritytans tillstånd,
// inga pixlar.
void fallI7()
{
    glaze::Canvas y;
    const int     a = y.addLayer("A");
    const int     m = y.addLayer("M");
    glaze::View   fel = kSidovy;
    fel.width         = -1;
    check(y.addView(fel) == -1 && y.viewCount() == 0,
          "I7 en vy med negativ bredd tas inte emot");
    fel           = kSidovy;
    fel.camera.distance = 0;
    check(y.addView(fel) == -1 && y.viewCount() == 0,
          "I7 en vy med en ogiltig kamera tas inte emot");
    check(y.addView(kSidovy) == 1 && y.addView(kSidovy) == 2 &&
              y.addView(kSidovy) == 3 && y.viewCount() == 3,
          "I7 addView numrerar vyerna 1, 2, 3");
    y.setLayerViews(a, {2, 0, 2});
    y.setLayerViews(m, {1});
    check(y.layerViews(a) == std::vector<int>{0, 2} &&
              y.layerViews(m) == std::vector<int>{1} &&
              y.layerViews(0) == std::vector<int>{0},
          "I7 setLayerViews: sorterat och unikt; huvudvyn från start");
    y.setLayerViews(a, {4});
    y.setLayerViews(a, {-1});
    check(y.layerViews(a) == std::vector<int>{0, 2},
          "I7 setLayerViews med en vy som inte finns ändrar ingenting");
    glaze::View flyttad = kSidovy;
    flyttad.offset      = {-20, 30};
    y.setView(0, flyttad);
    y.setView(4, flyttad);
    y.setView(2, flyttad);
    check(y.view(0) == nullptr && y.view(4) == nullptr && y.view(2) &&
              y.view(2)->offset.y == 30 && y.view(1)->offset.y == 10,
          "I7 setView ersätter vy 2; vy 0 och 4 avslås");
    y.removeView(1);
    check(y.viewCount() == 2 && y.layerViews(a) == std::vector<int>{0, 1} &&
              y.layerViews(m).empty() && y.view(1)->offset.y == 30,
          "I7 removeView(1): de senare flyttar ned ett nummer, och lagren "
          "följer med — lagret i den borttagna vyn lämnar den");
    y.removeView(5);
    y.removeView(0);
    check(y.viewCount() == 2, "I7 removeView utanför ändrar ingenting");
    y.setLayerViews(m, {2});
    y.setViews({kSidovy});
    check(y.viewCount() == 1 && y.layerViews(a) == std::vector<int>{0, 1} &&
              y.layerViews(m).empty(),
          "I7 setViews med färre vyer: de nummer som inte längre finns lämnar "
          "lagren");
    y.setViews({kSidovy, fel});
    check(y.viewCount() == 1,
          "I7 setViews med en ogiltig vy ändrar ingenting (atomiskt)");
}

// ------------------------------------------------------------------- I9
//
// MÅLARENS FÖREMÅL (`draw(Shape)` för rymdens föremål, `drawTriangles3`,
// `drawPoints3`): en låda flyttad med målarens `translate`, en låda vriden
// med `rotate`, instanser av en låda, punkter, trianglar och en lutande yta
// i rymden med kontur (Mesh- och Line3-delen) — ritade av
// MÅLAREN (ur CPU-kopian genom den transienta ringen) och av ett
// WORLD-LAGER (residensen) med samma renderläge ger samma pixlar. Lagrets väg
// mäts mot analytiska förväntat värde i del E och F; här mäts att målarens väg bär
// samma geometri, stil och ljus dit. Klipplanen är satta, så att den tomma
// scenen (målarens bild) har samma djupintervall som lagrets. TOLERANSEN ±1
// per kanal: mätt 0 på D3D11 och llvmpipe, men den vridna lådan
// når sina hörn genom en annan räkning (målarens vridning efter placeringens
// skala, mot lagrets egen placering) och får avrunda annorlunda på en annan
// GPU.
void fallI9()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target = {0, 0, 0.5}, .distance = 9,
                                .azimuth = 25, .elevation = 35, .nearPlane = 1,
                                .farPlane = 50});
    y.setLayerPlacement(0, glaze::LayerPlacement::World);
    const glaze::MeshStyle grå{.color = gfx::Color{170, 170, 190, 255}};
    const glaze::MeshStyle platt{.color = kGrön, .lit = false};
    const glaze::PointStyle    prickar{.size = glaze::px(8.0f), .color = kSvart};
    const std::vector<glaze::Point3> trianglar = {
        {-1, -2.5, 0.2}, {1, -2.5, 0.2}, {0, -1.8, 0.8}};
    const auto instanser = [&] {
        return glaze::Shape::instances(
            glaze::Shape::box({{-0.25, -0.25, 0}, {0.25, 0.25, 0.5}}, grå),
            {glaze::Instance{.at = {-2.2, 1, 0}, .color = kRöd},
             glaze::Instance{.at = {-2.2, -0.6, 0}, .yaw = 30, .color = kBlå}},
            grå);
    };
    // Ett FÄLT om 200 instanser av en prototyp med TVÅ NIVÅER — en kvadrat
    // nära, en triangel bortom 11 enheter: cellerna får olika nivåer, och
    // målaren ritar varje följd celler för sig ur samma uppladdning.
    glaze::MeshDesc nivåer;
    nivåer.positions = {{-0.1f, 0, 0},    {0.1f, 0, 0}, {0.1f, 0, 0.3f},
                        {-0.1f, 0, 0},    {0.1f, 0, 0.3f}, {-0.1f, 0, 0.3f},
                        {-0.1f, 0, 0},    {0.1f, 0, 0}, {0.0f, 0, 0.3f}};
    nivåer.lods      = {glaze::Lod{.first = 0, .count = 6, .maxDistance = 11},
                        glaze::Lod{.first = 6, .count = 3, .maxDistance = 0}};
    const glaze::Mesh            tvåNivåer(nivåer);
    const gfx::Color             fältfärg{0, 90, 200, 255};
    std::vector<glaze::Instance> fält;
    for (int i = 0; i < 200; ++i)
        fält.push_back(glaze::Instance{.at    = {3.0 + (i % 10) * 0.12,
                                                 -4.0 + (i / 10) * 0.4, 0},
                                       .color = fältfärg});
    const auto fältet = [&] {
        return glaze::Shape::instances(
            glaze::Shape::mesh(tvåNivåer), fält,
            glaze::MeshStyle{.color = kVit, .lit = false});
    };
    // En lutande yta i rymden med kontur — Mesh-delen OCH Line3-delen.
    const auto yta = [] {
        return glaze::Shape::polygon3(
            {{1.2, 1.5, 0.3}, {2.4, 1.5, 0.3}, {2.4, 2.4, 0.9}, {1.2, 2.4, 0.9}},
            gfx::Color{200, 120, 0, 255},
            glaze::Stroke{.paint = kSvart, .width = glaze::px(2.0f)});
    };
    // LAGRET: formerna i världen — lådorna där målarens transform lägger dem.
    // Trianglarna står FÖRST och framför den vridna lådan: utan djuptest hade
    // lådan ritats över dem.
    y.setShapes(0, {glaze::Shape::mesh(glaze::Mesh::fromWorld(trianglar), {},
                                       platt),
                    glaze::Shape::box({{1.5, -0.5, 0}, {2.5, 0.5, 1}}, grå),
                    glaze::Shape::box({{-0.5, -1, 0}, {0.5, 1, 0.6}}, grå),
                    instanser(),
                    glaze::Shape::points3({{0, 2, 0.5}, {0.5, 2, 1}}, prickar),
                    yta(), fältet()});
    bool målaren = false;
    y.onPaint    = [&](glaze::Widget*, glaze::PaintArgs& e) {
        if (!målaren)
            return;
        glaze::Painter& p = e.painter;
        p.setRenderState(glaze::RenderState{});   // lagrets förval
        p.drawTriangles3(trianglar, platt);
        {
            auto sparat = p.save();
            p.translate(2, 0);
            p.draw(glaze::Shape::box({{-0.5, -0.5, 0}, {0.5, 0.5, 1}}, grå));
        }
        {
            // 90° moturs: lådan längs x blir lagrets låda längs y.
            auto sparat = p.save();
            p.rotate(90);
            p.draw(glaze::Shape::box({{-1, -0.5, 0}, {1, 0.5, 0.6}}, grå));
        }
        p.draw(instanser());
        p.drawPoints3({{0, 2, 0.5}, {0.5, 2, 1}}, prickar);
        p.draw(yta());
        p.draw(fältet());
    };
    const Bild lager = r.bild();
    y.setShapes(0, {});
    målaren          = true;
    const Bild målad = r.bild();
    if (g_ritfel)
        return;
    int störst = 0, olika = 0, antal = 0, färgade = 0, röda = 0, blå = 0,
        gröna = 0, svarta = 0, orange = 0, fältets = 0;
    for (int py = static_cast<int>(std::ceil(r.box.y));
         py < static_cast<int>(r.box.y + r.box.h); ++py)
        for (int px_ = static_cast<int>(std::ceil(r.box.x));
             px_ < static_cast<int>(r.box.x + r.box.w); ++px_) {
            const std::uint8_t* a = lager.at(px_, py);
            const std::uint8_t* b = målad.at(px_, py);
            int                 d = 0;
            for (int k = 0; k < 3; ++k)
                d = std::max(d, std::abs(a[k] - b[k]));
            störst = std::max(störst, d);
            olika += d > 1;
            ++antal;
            färgade += !ärFärg(a, kVit, 8);
            röda += a[0] > 120 && a[1] < 40 && a[2] < 40;
            blå += a[2] > 120 && a[0] < 40 && a[1] < 40;
            gröna += ärFärg(a, kGrön, 8);
            svarta += a[0] < 40 && a[1] < 40 && a[2] < 40;
            orange += ärFärg(a, gfx::Color{200, 120, 0, 255}, 8);
            fältets += ärFärg(a, fältfärg, 8);
        }
    check(färgade >= 2000 && röda >= 20 && blå >= 20 && gröna >= 200 &&
              svarta >= 40 && orange >= 200 && fältets >= 200,
          "I9 lagrets bild bär föremålen: lådorna, de röda och blå "
          "instanserna, de gröna trianglarna, de svarta punkterna, den "
          "orange ytan i rymden och fältet",
          std::to_string(färgade) + " färgade, " + std::to_string(röda) +
              " röda, " + std::to_string(blå) + " blå, " +
              std::to_string(gröna) + " gröna, " + std::to_string(svarta) +
              " svarta, " + std::to_string(orange) + " orange, " +
              std::to_string(fältets) + " fältets px");
    check(antal > 0 && störst <= 1,
          "I9 målarens föremål (draw(Shape), drawTriangles3, drawPoints3, "
          "polygon3 med kontur, translate och rotate) = lagrets (±1 per "
          "kanal)",
          "störst " + std::to_string(störst) + ", " + std::to_string(olika) +
              " av " + std::to_string(antal) + " px skiljer > 1");
}

// =========================================================== DEL C2
//
// EGNA PROGRAM OCH HELSKÄRMSPASS PÅ RITYTAN genom en RIKTIG
// glaze::Canvas (Rit3-fönstren). Grinden skriver sina program i HLSL, GLSL och
// MSL (MSL på macOS); förväntat värde är grindens egna tal och dess egen
// trigonometri ur camera.h:s konvention.

// Ett glaze::Program ur grindens källor.
glaze::Program ritProgram(const char* namn, const Källor& k, int texturer = 0,
                          int flyttal = 0)
{
    glaze::Program p;
    p.name          = namn;
    p.hlsl          = k.hlsl;
    p.glslVertex    = k.glslVs;
    p.glslFragment  = k.glslFs;
    p.msl           = k.msl;
    p.textures      = texturer;
    p.uniformFloats = flyttal;
    return p;
}

// ETT EGET MESH-PROGRAM (C2a): läget, uv och user0 ur meshen, GlazeUser i
// två float4 och en textur på plats 0 — färgen är (user0.x, ANDRA
// float4:ans y, texturens blå), så varje del måste komma fram på sin plats.
const Källor kEgetMesh = {
    R"hlsl(
cbuffer GlazeUser : register(b4) { float4 uFirst; float4 uSecond; };
Texture2D    t0 : register(t0);
SamplerState s0 : register(s0);
struct VsIn  { float3 pos : ATTR0; float2 uv : ATTR3; float4 user0 : ATTR5; };
struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 user0 : TEXCOORD1; };
VsOut vsMain(VsIn i)
{
    VsOut o;
    o.pos   = glaze_toClip(mul(glaze.viewProj, float4(i.pos, 1.0)));
    o.uv    = i.uv;
    o.user0 = i.user0;
    return o;
}
float4 fsMain(VsOut i) : SV_Target
{
    float4 t = t0.Sample(s0, i.uv);
    return float4(i.user0.x, uSecond.y, t.b, 1.0);
}
)hlsl",
    R"glsl(
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec2 aUv;
layout(location = 5) in vec4 aUser0;
out vec2 vUv;
out vec4 vUser0;
void main()
{
    gl_Position = glaze_toClip(glaze.viewProj * vec4(aPos, 1.0));
    vUv    = aUv;
    vUser0 = aUser0;
}
)glsl",
    R"glsl(
layout(std140) uniform GlazeUser { vec4 uFirst; vec4 uSecond; };
uniform sampler2D GlazeTexture0;
in vec2 vUv;
in vec4 vUser0;
out vec4 frag;
void main()
{
    vec4 t = texture(GlazeTexture0, vUv);
    frag = vec4(vUser0.x, uSecond.y, t.b, 1.0);
}
)glsl",
    R"msl(
struct User  { float4 first; float4 second; };
struct VsIn  { float3 pos [[attribute(0)]]; float2 uv [[attribute(3)]];
               float4 user0 [[attribute(5)]]; };
struct VsOut { float4 pos [[position]]; float2 uv; float4 user0; };
vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]])
{
    VsOut o;
    o.pos   = glaze_toClip(glaze.viewProj * float4(i.pos, 1.0), glaze);
    o.uv    = i.uv;
    o.user0 = i.user0;
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]], constant User& user [[buffer(3)]],
                       texture2d<float> t0 [[texture(0)]],
                       sampler s0 [[sampler(0)]])
{
    float4 t = t0.sample(s0, i.uv);
    return float4(i.user0.x, user.second.y, t.b, 1.0);
}
)msl"};

// HIMMELN (C2d): helskärmstriangeln med NDC i en varying, och strålen ur
// GlazeFrame:s bas — dir = rayCenter + x · right + y · up — tonad efter dess
// normerade z från horisontens färg (GlazeUser:s första float4) till
// zenitens (den andra).
const Källor kHimmel = {
    R"hlsl(
cbuffer GlazeUser : register(b4) { float4 uHorisont; float4 uZenit; };
struct VsOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VsOut vsMain(uint id : SV_VertexID)
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float4 fsMain(VsOut i) : SV_Target
{
    float3 dir = glaze.rayCenter.xyz + i.ndc.x * glaze.right.xyz +
                 i.ndc.y * glaze.up.xyz;
    float  t   = saturate(normalize(dir).z);
    return float4(lerp(uHorisont.rgb, uZenit.rgb, t), 1.0);
}
)hlsl",
    R"glsl(
out vec2 vNdc;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc        = p * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
)glsl",
    R"glsl(
layout(std140) uniform GlazeUser { vec4 uHorisont; vec4 uZenit; };
in vec2 vNdc;
out vec4 frag;
void main()
{
    vec3  dir = glaze.rayCenter.xyz + vNdc.x * glaze.right.xyz + vNdc.y * glaze.up.xyz;
    float t   = clamp(normalize(dir).z, 0.0, 1.0);
    frag = vec4(mix(uHorisont.rgb, uZenit.rgb, t), 1.0);
}
)glsl",
    R"msl(
struct User  { float4 horisont; float4 zenit; };
struct VsOut { float4 pos [[position]]; float2 ndc; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]],
                       constant User& user [[buffer(3)]])
{
    float3 dir = glaze.rayCenter.xyz + i.ndc.x * glaze.right.xyz +
                 i.ndc.y * glaze.up.xyz;
    float  t   = saturate(normalize(dir).z);
    return float4(mix(user.horisont.rgb, user.zenit.rgb, t), 1.0);
}
)msl"};

const gfx::Color kHorisont{200, 220, 250, 255};
const gfx::Color kZenit{40, 90, 190, 255};

// Kvadraten ±halv i planet z = 0 med uv (NV = (0, 0)) och user0 = (0,8, 0, 0,
// 1) i varje hörn — C2a:s mesh.
glaze::Mesh egenKvadrat(double halv)
{
    glaze::MeshDesc d = kvadrat(halv, 0);
    d.uvs     = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};   // SV, SÖ, NÖ, NV
    d.user[0] = std::vector<std::array<float, 4>>(4, {0.8f, 0.0f, 0.0f, 1.0f});
    return glaze::Mesh(std::move(d));
}

// ------------------------------------------------------------------ C2a
//
// ETT EGET MESH-PROGRAM: user0 (R 0,8), GlazeUser:s andra float4 (G 0,6 —
// den första är skräp, så en förskjuten offset syns) och en 2 × 2-Bitmap
// (blå 255/0/100/200 i läsordning, närmaste texel) på kvadratens fyra
// kvadranter, uppifrån.PLACERINGEN är invikt i GlazeFrame:
// samma mesh vriden 90° (medurs, kompassen) och skalad 1,5 vid (6, 0) bär
// sin NV-kvadrant i NÖ och slutar vid 1,5 · 1.
void fallC2a()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u   = uppifrån(r, 10);
    const int      idx = y.addProgram(ritProgram("egen", kEgetMesh, 1, 8));
    check(idx == 0 && y.programCount() == 1 && y.program(0) &&
              y.program(0)->name == "egen",
          "C2a addProgram svarar med indexet och programmet står där");
    const std::uint8_t blå4[16] = {0, 0, 255, 255, 0, 0, 0,   255,
                                   0, 0, 100, 255, 0, 0, 200, 255};
    glaze::Bitmap      bild(2, 2, blå4, 8);
    bild.setFilter(gfx::ImageFilter::Nearest);
    const glaze::MeshStyle stil{.program  = "egen",
                                .uniforms = {0.1f, 0.2f, 0.3f, 0.4f, 0.9f, 0.6f,
                                             0.9f, 0.9f},
                                .textures = {bild}};
    const glaze::Mesh m = egenKvadrat(4);
    y.addShape(glaze::Shape::mesh(m, {}, stil));
    const glaze::Mesh liten = egenKvadrat(1);
    y.addShape(glaze::Shape::mesh(liten, {.at = {6, 0, 0}, .yaw = 90,
                                          .scale = {1.5f, 1.5f, 1.0f}},
                                  stil));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    check(y.programError("egen").empty(), "C2a programmet kompilerar",
          kort(y.programError("egen")));
    const auto väntad = [](std::uint8_t blått) {
        return gfx::Color{204, 153, blått, 255};
    };
    const bool ok = ärFärg(b.at(u.x(-2), u.y(2)), väntad(255)) &&
                    ärFärg(b.at(u.x(2), u.y(2)), väntad(0)) &&
                    ärFärg(b.at(u.x(-2), u.y(-2)), väntad(100)) &&
                    ärFärg(b.at(u.x(2), u.y(-2)), väntad(200));
    check(ok,
          "C2a eget mesh-program: R = user0.x, G = GlazeUser:s andra float4, B "
          "= Bitmap-texturen per kvadrant",
          px(b.at(u.x(-2), u.y(2))) + " / " + px(b.at(u.x(2), u.y(2))) + " / " +
              px(b.at(u.x(-2), u.y(-2))) + " / " + px(b.at(u.x(2), u.y(-2))));
    const bool placerad = ärFärg(b.at(u.x(6.75), u.y(0.75)), väntad(255)) &&
                          ärFärg(b.at(u.x(5.25), u.y(0.75)), väntad(100)) &&
                          ärFärg(b.at(u.x(7.8), u.y(0.0)), kVit) &&
                          ärFärg(b.at(u.x(6.0), u.y(1.8)), kVit);
    check(placerad,
          "C2a placeringen invikt i GlazeFrame: vriden 90° "
          "medurs (NV-kvadranten i NÖ) och skalad 1,5",
          px(b.at(u.x(6.75), u.y(0.75))) + " / " + px(b.at(u.x(5.25), u.y(0.75))) +
              " / " + px(b.at(u.x(7.8), u.y(0.0))));
}

// ------------------------------------------------------------------ C2b
//
// RESERVEN och FELEN: ett program med syntaxfel på appens rad 7 ritar meshen
// med det INBYGGDA programmet (stilens röda färg, obelyst) och
// `programError` bär raden i kompilatorns form och språket; ett program som
// saknar källa för backenden likaså, med "saknar <språk>-källa"; ett okänt
// namn ger "finns inte".`setProgram` med en rättad källa är
// en ny version — fönstret kompilerar om, felet är borta och programmet ritar.
void fallC2b()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    y.addProgram(ritProgram("trasig", kRadSju));
    y.addProgram(ritProgram("utan", utanEgen(kEgetMesh), 1, 8));
    const glaze::MeshStyle röd{.color = kRöd, .lit = false, .program = "trasig"};
    glaze::MeshStyle       grön{.color = kGrön, .lit = false, .program = "utan"};
    glaze::MeshStyle       blå{.color = kBlå, .lit = false, .program = "saknas"};
    y.addShape(glaze::Shape::mesh(egenKvadrat(1.5), {.at = {-5, 0, 0}}, röd));
    y.addShape(glaze::Shape::mesh(egenKvadrat(1.5), {.at = {0, 0, 0}}, grön));
    y.addShape(glaze::Shape::mesh(egenKvadrat(1.5), {.at = {5, 0, 0}}, blå));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(b.at(u.x(-5), u.y(0)), kRöd) && ärFärg(b.at(u.x(0), u.y(0)), kGrön) &&
              ärFärg(b.at(u.x(5), u.y(0)), kBlå),
          "C2b reserven: trasigt, källlöst och okänt program ritar meshen med "
          "det inbyggda programmet (stilens färg)",
          px(b.at(u.x(-5), u.y(0))) + " / " + px(b.at(u.x(0), u.y(0))) + " / " +
              px(b.at(u.x(5), u.y(0))));
    const std::string felRad = y.programError("trasig");
    check(radSju(felRad) && felRad.find(kSpråk) != std::string::npos,
          std::string("C2b programError bär appens rad 7 och ") + kSpråk,
          kort(felRad));
    const std::string felKälla = y.programError("utan");
    check(felKälla.find(std::string("saknar ") + kSpråk + "-källa") !=
                  std::string::npos &&
              felKälla.find("utan") != std::string::npos,
          std::string("C2b en saknad källa: \"saknar ") + kSpråk +
              "-källa\" med programmets namn",
          kort(felKälla));
    check(y.programError("saknas").find("finns inte") != std::string::npos,
          "C2b ett okänt namn: \"finns inte\"", kort(y.programError("saknas")));
    // Avslagen: ett upprepat namn och ett utan namn ändrar ingenting.
    check(y.addProgram(ritProgram("trasig", kEgetMesh, 1, 8)) == -1 &&
              y.addProgram(ritProgram("", kEgetMesh)) == -1 &&
              y.programCount() == 2,
          "C2b avslagen: ett upprepat och ett tomt namn läggs inte till");
    // En rättad källa på samma namn: ny version, omkompilerad, ritar.
    y.setProgram(1, ritProgram("utan", kEgetMesh, 1, 8));
    grön.uniforms = {0, 0, 0, 0, 0, 0.6f, 0, 0};
    const std::uint8_t vit4[16] = {0, 0, 50, 255, 0, 0, 50, 255,
                                   0, 0, 50, 255, 0, 0, 50, 255};
    glaze::Bitmap      bild(2, 2, vit4, 8);
    grön.textures = {bild};
    y.setShape(1, glaze::Shape::mesh(egenKvadrat(1.5), {.at = {0, 0, 0}}, grön));
    const Bild b2 = r.bild();
    check(y.programError("utan").empty() &&
              ärFärg(b2.at(u.x(0), u.y(0)), gfx::Color{204, 153, 50, 255}),
          "C2b setProgram med en rättad källa: omkompilerad, "
          "felet borta och programmet ritar",
          px(b2.at(u.x(0), u.y(0))) + " · " + kort(y.programError("utan")));
}

// ------------------------------------------------------------------ C2c
//
// HIMMELSPASSET: en 3D-kamera som tittar 45° UPPÅT med synfältet 90° — den
// översta raden ser rakt upp, den nedersta vågrätt. Förväntat värde per rad (mitten-
// kolumnen) är grindens egen trigonometri: strålens höjdvinkel är 45° +
// atan(ndc_y), och färgen horisont + (zenit − horisont) · sin(vinkeln).
// Översta raden zenitfärgen och nedersta horisontfärgen ±2, varje rad ±2.
// Ett pass ensamt tar ritytan till 3D-målet.
void fallC2c()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D{.target    = {0, 0, 0},
                                .distance  = 10,
                                .azimuth   = 0,
                                .elevation = -45,
                                .fov       = 90});
    const auto c = [](std::uint8_t v) { return v / 255.0f; };
    y.addProgram(ritProgram("himmel", kHimmel, 0, 8));
    const std::uint64_t före = r.målRitningar();
    y.addShape(glaze::Shape::screenPass(
        "himmel",
        {.uniforms = {c(kHorisont.r), c(kHorisont.g), c(kHorisont.b), 1.0f,
                      c(kZenit.r), c(kZenit.g), c(kZenit.b), 1.0f}}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    check(r.målRitningar() > före, "C2c passet tar ritytan till "
                                   "3D-målet");
    const int    x0 = r.x(150), y0 = static_cast<int>(r.box.y);
    const int    h  = static_cast<int>(r.box.h);
    const auto   facit = [&](int rad) {
        const double ndc = 1.0 - 2.0 * (rad + 0.5) / h;
        const double v   = (45.0 + std::atan(ndc) * 180.0 / 3.14159265358979) *
                         3.14159265358979 / 180.0;
        const double t = std::clamp(std::sin(v), 0.0, 1.0);
        return gfx::Color{
            static_cast<std::uint8_t>(std::lround(kHorisont.r + (kZenit.r - kHorisont.r) * t)),
            static_cast<std::uint8_t>(std::lround(kHorisont.g + (kZenit.g - kHorisont.g) * t)),
            static_cast<std::uint8_t>(std::lround(kHorisont.b + (kZenit.b - kHorisont.b) * t)),
            255};
    };
    check(ärFärg(b.at(x0, y0), kZenit),
          "C2c himlen: översta raden är zenitfärgen ±2", px(b.at(x0, y0)));
    check(ärFärg(b.at(x0, y0 + h - 1), kHorisont),
          "C2c himlen: nedersta raden är horisontfärgen ±2",
          px(b.at(x0, y0 + h - 1)));
    int sämst = 0, sämstRad = -1;
    for (int rad = 0; rad < h; ++rad) {
        const gfx::Color    f = facit(rad);
        const std::uint8_t* p = b.at(x0, y0 + rad);
        const int d = std::max({std::abs(p[0] - f.r), std::abs(p[1] - f.g),
                                std::abs(p[2] - f.b)});
        if (d > sämst) {
            sämst    = d;
            sämstRad = rad;
        }
    }
    check(sämst <= 2,
          "C2c himlen: varje rad = horisont + (zenit − horisont) · sin(45° + "
          "atan(ndc_y)) ±2 (strålbasen ur GlazeFrame)",
          "största avvikelsen " + std::to_string(sämst) + " på rad " +
              std::to_string(sämstRad));
}

// ------------------------------------------------------------------ C2d
//
// PASSETS RESERV: ett okänt program fyller ritytan med `fallback` (och
// `programError` säger att det inte finns), ett trasigt likaså; alfa 0 —
// förvalet — ritar ingenting, och ytan bakom syns.
void fallC2d()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    y.addShape(glaze::Shape::screenPass("okänt", {.fallback = kBlå}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(b.at(r.x(150), r.y(110)), kBlå) &&
              y.programError("okänt").find("finns inte") != std::string::npos,
          "C2d passets fallback när programmet är okänt",
          px(b.at(r.x(150), r.y(110))));
    y.addProgram(ritProgram("trasig", kRadSju));
    y.setShapes({glaze::Shape::screenPass("trasig", {.fallback = kGrön})});
    const Bild b2 = r.bild();
    check(ärFärg(b2.at(r.x(150), r.y(110)), kGrön),
          "C2d passets fallback när programmet inte kompilerar",
          px(b2.at(r.x(150), r.y(110))));
    y.setShapes({glaze::Shape::screenPass("okänt")});
    const Bild b3 = r.bild();
    check(ärFärg(b3.at(r.x(150), r.y(110)), kVit),
          "C2d en fallback med alfa 0 ritar ingenting (ritytans vita yta syns)",
          px(b3.at(r.x(150), r.y(110))));
}

// ============================================================ DEL H
//
// TERRÄNG, DRAPERING OCH TEXT I MARKEN genom en RIKTIG
// glaze::Canvas (Rit3). Terrängen är en ANALYTISK LUTANDE PLATTA z = 0,25x +
// 0,15y + 2 över [−8, 8]² (9 × 9 hörn), och det förväntade värdet är grindens egen
// hålkamera ur camera.h:s ord: blicken (sin az · cos el, cos az · cos el,
// −sin el), upp längs elevationens tangent (sin az · sin el, cos az · sin el,
// cos el), höger = blicken × upp, och punkten i (mitten + f · x/z, mitten −
// f · y/z) med f = halva höjden / tan(fov/2).

double plattH(double x, double y)
{
    return 0.25 * x + 0.15 * y + 2.0;
}

// Plattan som mesh: 9 × 9 hörn med 2 enheters steg, och HÅLET — cellen
// x ∈ [2, 4], y ∈ [−4, −2] — utan sina två trianglar när `hål` är satt.
glaze::Mesh platta(bool hål)
{
    glaze::MeshDesc d;
    for (int j = 0; j <= 8; ++j)
        for (int i = 0; i <= 8; ++i) {
            const double x = -8.0 + 2.0 * i, y = -8.0 + 2.0 * j;
            d.positions.push_back({static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(plattH(x, y))});
        }
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) {
            if (hål && i == 5 && j == 2)
                continue;
            const auto a = static_cast<std::uint32_t>(j * 9 + i);
            d.indices.insert(d.indices.end(), {a, a + 1, a + 10, a, a + 10, a + 9});
        }
    return glaze::Mesh(std::move(d));
}

// En PLAN platta i z = 0 över [−8, 8]² — H2b:s terräng.
glaze::Mesh plattPlan()
{
    glaze::MeshDesc d;
    d.positions = {{-8, -8, 0}, {8, -8, 0}, {8, 8, 0}, {-8, 8, 0}};
    d.indices   = {0, 1, 2, 0, 2, 3};
    return glaze::Mesh(std::move(d));
}

const gfx::Color kPlattGrå{60, 60, 64, 255};

// Grindens hålkamera (se avsnittets huvud), i fönstrets fysiska px.
struct Hålkamera {
    double e[3], f[3], r[3], u[3];
    double cx = 0.0, cy = 0.0, fpx = 1.0;
    bool   proj(double x, double y, double z, double& px, double& py) const
    {
        const double d[3] = {x - e[0], y - e[1], z - e[2]};
        const double zc   = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        if (!(zc > 0.0))
            return false;
        px = cx + fpx * (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / zc;
        py = cy - fpx * (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / zc;
        return true;
    }
};

Hålkamera hålkamera(const glaze::Camera3D& k, const gfx::Rectf& box)
{
    const double rad = 3.14159265358979323846 / 180.0;
    const double az = k.azimuth * rad, el = k.elevation * rad;
    Hålkamera    h;
    h.f[0] = std::sin(az) * std::cos(el);
    h.f[1] = std::cos(az) * std::cos(el);
    h.f[2] = -std::sin(el);
    h.u[0] = std::sin(az) * std::sin(el);
    h.u[1] = std::cos(az) * std::sin(el);
    h.u[2] = std::cos(el);
    h.r[0] = h.f[1] * h.u[2] - h.f[2] * h.u[1];
    h.r[1] = h.f[2] * h.u[0] - h.f[0] * h.u[2];
    h.r[2] = h.f[0] * h.u[1] - h.f[1] * h.u[0];
    for (int i = 0; i < 3; ++i)
        h.e[i] = (i == 0 ? k.target.x : i == 1 ? k.target.y : k.target.z) -
                 h.f[i] * k.distance;
    h.cx  = box.x + box.w / 2.0;
    h.cy  = box.y + box.h / 2.0;
    h.fpx = box.h / 2.0 / std::tan(k.fov * rad / 2.0);
    return h;
}

const glaze::Camera3D kLutKamera{.target    = {0, 0, 2},
                                 .distance  = 34,
                                 .azimuth   = 20,
                                 .elevation = 35,
                                 .fov       = 45};

// Röda pixlar inom `radie` från (px, py).
bool rödNära(const Bild& b, double px, double py, int radie)
{
    for (int y = static_cast<int>(py) - radie; y <= static_cast<int>(py) + radie; ++y)
        for (int x = static_cast<int>(px) - radie; x <= static_cast<int>(px) + radie;
             ++x) {
            const std::uint8_t* p = b.at(x, y);
            if (p[0] > 180 && p[1] < 90 && p[2] < 90)
                return true;
        }
    return false;
}

// Rutan kring en färgs pixlar (H2b, H5–H5c): storleken och mitten i
// bildens px.
struct RödRuta {
    double w = 0.0, h = 0.0;
    double cx = 0.0, cy = 0.0;   // rutans mitt i bildens px
    int    n = 0;
};

// Rutan kring pixlarna som `är` godtar, inom `box`.
template <class Är>
RödRuta färgRuta(const Bild& b, const gfx::Rectf& box, Är är)
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, n = 0;
    for (int y = static_cast<int>(box.y); y < static_cast<int>(box.y + box.h); ++y)
        for (int x = static_cast<int>(box.x); x < static_cast<int>(box.x + box.w); ++x) {
            if (är(b.at(x, y))) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
                ++n;
            }
        }
    RödRuta r;
    r.n = n;
    if (n > 0) {
        r.w  = x1 - x0 + 1;
        r.h  = y1 - y0 + 1;
        r.cx = (x0 + x1 + 1) / 2.0;
        r.cy = (y0 + y1 + 1) / 2.0;
    }
    return r;
}

RödRuta rödRuta(const Bild& b, const gfx::Rectf& box)
{
    return färgRuta(b, box, [](const std::uint8_t* p) {
        return p[0] > 180 && p[1] < 90 && p[2] < 90;
    });
}

RödRuta blåRuta(const Bild& b, const gfx::Rectf& box)
{
    return färgRuta(b, box, [](const std::uint8_t* p) {
        return p[2] > 180 && p[0] < 90 && p[1] < 90;
    });
}

// Draperingens räknare i fönstret (internt, canvas/drape.h).
std::uint64_t draperingar(Rit3& r)
{
    const glaze::Ctx c = r.f.yta.behaviorCtx();
    return c.canvas ? c.canvas->drapes().renders() : 0;
}

// ------------------------------------------------------------------- H1
//
// EN RÖD LINJE (y = 2, x −6…6, 5 px) i ett Ground-lager draperad på den
// lutande plattan: varje provpunkt (x, 2, h(x, 2)) har röda pixlar inom 2 px
// från grindens projektion — och den PLANA projektionen (z = 0), som ligger
// minst 8 px bort, inga (kontrollen: linjen ligger på ytan, inte i z = 0).
void fallH1()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(kLutKamera);
    const int T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    y.addShape(T, glaze::Shape::mesh(platta(false), {},
                                     {.color = kPlattGrå, .lit = false}));
    const int G = y.addLayer("vägar");
    y.addShape(G, glaze::Shape::polyline({{-6, 2}, {6, 2}},
                                         {.paint = kRöd, .width = glaze::px(5.0f)}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const Hålkamera h = hålkamera(kLutKamera, r.box);
    int             träff = 0, kontroll = 0, prov = 0;
    std::string     fick;
    for (const double x : {-5.0, -2.5, 0.0, 2.5, 5.0}) {
        double sx = 0.0, sy = 0.0, qx = 0.0, qy = 0.0;
        if (!h.proj(x, 2.0, plattH(x, 2.0), sx, sy) || !h.proj(x, 2.0, 0.0, qx, qy))
            continue;
        ++prov;
        träff += rödNära(b, sx, sy, 2);
        if (std::hypot(sx - qx, sy - qy) >= 8.0) {
            ++kontroll;
            if (rödNära(b, qx, qy, 2))
                fick += " plan(" + tal(x) + ")";
        }
        if (!rödNära(b, sx, sy, 2))
            fick += " miss(" + tal(x) + ": " +
                    px(b.at(static_cast<int>(sx), static_cast<int>(sy))) + ")";
    }
    check(prov == 5 && träff == 5,
          "H1 den röda linjen ligger där grindens projektion av den lutande "
          "ytan säger (±2 px, fem punkter)",
          std::to_string(träff) + " av " + std::to_string(prov) + fick);
    check(kontroll >= 3 && fick.find("plan(") == std::string::npos,
          "H1 kontrollen: ingen röd linje i den plana projektionen (z = 0)",
          std::to_string(kontroll) + " kontrollpunkter" + fick);
}

// ------------------------------------------------------------------- H2
//
// EN BILD DRAPERAS 1:1 och ett HÅL VISAR BAKGRUNDEN (H3): plattan sedd rakt
// uppifrån (ortografiskt, halva höjden 10), och en 2 × 2-Bitmap (röd, grön /
// blå, gul — rad 0 i norr, kartans y uppåt, närmaste texel) över hela
// plattan i ett Ground-lager. Kvadranternas färger i sina kvadranter, och
// gränsen röd → grön längs y = 4 vid x = 0 ±2 px. HÅLET (cellen x 2…4,
// y −4…−2) visar en blå låda som ligger UNDER plattan och under z = 0 — utan
// markdjup och utan Ground-lagret i z = 0 (draperingen är markens enda väg).
void fallH2()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setYUp(true);
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const Uppifrån u = uppifrån(r, 10);
    const int      T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    y.addShape(T, glaze::Shape::mesh(platta(true), {},
                                     {.color = kPlattGrå, .lit = false}));
    const int L = y.addLayer("grund");
    y.setLayerPlacement(L, glaze::LayerPlacement::World);
    const gfx::Color kHålBlå{30, 60, 220, 255};
    y.addShape(L, glaze::Shape::box({{1.5, -4.5, -3}, {4.5, -1.5, -2}},
                                    {.color = kHålBlå, .lit = false}));
    const std::uint8_t px4[16] = {255, 0, 0, 255,   0,   200, 0, 255,
                                  0,   0, 255, 255, 255, 255, 0, 255};
    glaze::Bitmap      bild(2, 2, px4, 8);
    bild.setFilter(gfx::ImageFilter::Nearest);
    const int G = y.addLayer("flygfoto");
    y.addShape(G, glaze::Shape::image({-8, -8, 16, 16}, bild));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const gfx::Color gul{255, 255, 0, 255};
    const bool       kvad = ärFärg(b.at(u.x(-4), u.y(4)), kRöd, 3) &&
                      ärFärg(b.at(u.x(4), u.y(4)), kGrön, 3) &&
                      ärFärg(b.at(u.x(-4), u.y(-6)), kBlå, 3) &&
                      ärFärg(b.at(u.x(6), u.y(-6)), gul, 3);
    check(kvad,
          "H2 bilden draperad 1:1: kvadranterna i sina kvadranter, rad 0 i "
          "norr",
          px(b.at(u.x(-4), u.y(4))) + " / " + px(b.at(u.x(4), u.y(4))) + " / " +
              px(b.at(u.x(-4), u.y(-6))) + " / " + px(b.at(u.x(6), u.y(-6))));
    // Gränsen röd → grön längs raden y = 4: den första kolumnen som inte är
    // röd, mot x = 0.
    const int rad = u.y(4);
    int       gräns = -1;
    for (int x = u.x(-4); x <= u.x(4); ++x)
        if (!ärFärg(b.at(x, rad), kRöd, 60)) {
            gräns = x;
            break;
        }
    check(gräns >= 0 && std::abs(gräns - u.x(0)) <= 2,
          "H2 bildens mittlinje ligger vid x = 0 ±2 px (1:1, ingen förskjutning "
          "eller spegling)",
          std::to_string(gräns) + " mot " + std::to_string(u.x(0)));
    check(ärFärg(b.at(u.x(3), u.y(-3)), kHålBlå, 3) &&
              ärFärg(b.at(u.x(1), u.y(-3)), gul, 3),
          "H3 hålet visar bakgrunden: lådan under plattan (och under z = 0) "
          "syns — inget markdjup, inget Ground-lager i z = 0; bredvid hålet "
          "bilden",
          px(b.at(u.x(3), u.y(-3))) + " / " + px(b.at(u.x(1), u.y(-3))));
    // PLOCKNINGEN FÖLJER BILDEN: genom hålet träffas lådan
    // under plattan med skymningen på (inget markdjup) men inte bilden, och
    // bredvid hålet träffas bilden på terrängens yta.
    const auto lokal = [&](double wx, double wy) {
        return glaze::Point{(u.x(wx) + 0.5 - r.box.x) / r.skala,
                            (u.y(wy) + 0.5 - r.box.y) / r.skala};
    };
    glaze::HitOptions sk;
    sk.occlusion          = true;
    bool       lådan      = false, bildenIHål = false, bilden = false;
    double     z          = -1e9;
    for (const glaze::HitResult& h : y.hitTest(lokal(3, -3), sk)) {
        lådan      = lådan || (h.layer == L && h.kind == glaze::HitKind::Surface);
        bildenIHål = bildenIHål || h.layer == G;
    }
    for (const glaze::HitResult& h : y.hitTest(lokal(1, -3), sk))
        if (h.layer == G && h.point) {
            bilden = true;
            z      = h.point->z;
        }
    check(lådan && !bildenIHål && bilden && std::abs(z - plattH(1, -3)) < 0.1,
          "H3b plockningen som bilden: lådan genom hålet "
          "(skymningen på), ingen bild i hålet, bilden bredvid på terrängens "
          "höjd",
          std::string(lådan ? "lådan" : "ingen låda") +
              (bildenIHål ? ", bild i hålet" : "") + ", z " + tal(z) + " mot " +
              tal(plattH(1, -3)));
}

// ------------------------------------------------------------------ H2b
//
// DRAPERINGEN SEDD UNDERIFRÅN (drape.h `DrapeView::northUp`):
// en PLAN terrängplatta i z = 0, H2:s 2 × 2-bild över den och en liggande
// text (röd platta, ankaret i textblockets övre vänstra hörn) i ett
// Ground-lager, sedda genom kartkameran rakt uppifrån (zoom 10) med kartans y
// uppåt OCH nedåt. Med y nedåt står 3D-vyns öga UNDER planet, och
// draperingen ritas med rad 0 i söder. Förväntat värde är kartans: bilden står upprätt
// i den ovridna kartan i båda lägena — rad 0 överst på skärmen — och den
// liggande texten är inte spegelvänd (plattan till höger om och under
// ankaret i grindens egen projektion, ±2 px och plattans luft).
void fallH2b()
{
    const std::uint8_t px4[16] = {255, 0, 0, 255,   0,   200, 0, 255,
                                  0,   0, 255, 255, 255, 255, 0, 255};
    for (const bool yUpp : {true, false}) {
        Rit3 r;
        if (!r.öppna())
            return;
        glaze::Canvas& y = r.f.yta;
        y.setYUp(yUpp);
        y.setCamera(glaze::Camera{.center = {0, 0}, .zoom = 10});
        const int T = y.addLayer("terräng");
        y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
        y.addShape(T, glaze::Shape::mesh(plattPlan(), {},
                                         {.color = kPlattGrå, .lit = false}));
        glaze::Bitmap bild(2, 2, px4, 8);
        bild.setFilter(gfx::ImageFilter::Nearest);
        const int G = y.addLayer("flygfoto");
        y.addShape(G, glaze::Shape::image({-8, -8, 16, 16}, bild));
        // Ankaret en enhet in i den nedre högra kvadranten PÅ SKÄRMEN.
        const double vy = yUpp ? -1.0 : 1.0;
        y.addShape(G, glaze::Shape::text({1, vy}, "MMM",
                                         {.size       = glaze::px(14.0f),
                                          .color      = gfx::Color{255, 255, 255, 255},
                                          .background = kRöd,
                                          .padding    = glaze::px(3.0f),
                                          .anchor     = glaze::TextAnchor::TopLeft,
                                          .rotation   = glaze::Alignment::Ground}));
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        // Kartans projektion rakt uppifrån: k fysiska px per enhet, skärmens y
        // nedåt — världens y speglas med y uppåt.
        const double cx = r.box.x + r.box.w / 2.0, cy = r.box.y + r.box.h / 2.0;
        const double k  = 10.0 * r.skala;
        const auto   sx = [&](double wx) { return static_cast<int>(cx + wx * k); };
        const auto   sy = [&](double wy) {
            return static_cast<int>(cy + (yUpp ? -wy : wy) * k);
        };
        const double     sv = yUpp ? 1.0 : -1.0;   // "överst på skärmen" i världens y
        const gfx::Color gul{255, 255, 0, 255};
        const bool       upprätt = ärFärg(b.at(sx(-4), sy(4 * sv)), kRöd, 3) &&
                             ärFärg(b.at(sx(4), sy(4 * sv)), kGrön, 3) &&
                             ärFärg(b.at(sx(-4), sy(-4 * sv)), kBlå, 3) &&
                             ärFärg(b.at(sx(6), sy(-6.5 * sv)), gul, 3);
        const std::string läge = yUpp ? "y uppåt" : "y nedåt (ögat under planet)";
        check(upprätt,
              "H2b " + läge + ": den draperade bilden står upprätt — rad 0 överst "
                              "på skärmen",
              px(b.at(sx(-4), sy(4 * sv))) + " / " + px(b.at(sx(4), sy(4 * sv))) +
                  " / " + px(b.at(sx(-4), sy(-4 * sv))) + " / " +
                  px(b.at(sx(6), sy(-6.5 * sv))));
        // Plattan i skärmens nedre högra fjärdedel (den röda kvadranten
        // ligger i den övre vänstra).
        const gfx::Rectf fjärdedel{static_cast<float>(cx), static_cast<float>(cy),
                                   r.box.w / 2.0f, r.box.h / 2.0f};
        const RödRuta    pl  = rödRuta(b, fjärdedel);
        const double     ax  = cx + 1.0 * k, ay = cy + 1.0 * k;
        const double     tol = 2.0 + 3.0 * r.skala;   // + plattans luft
        check(pl.n > 0 && pl.cx - pl.w / 2.0 >= ax - tol &&
                  pl.cy - pl.h / 2.0 >= ay - tol,
              "H2b " + läge + ": den draperade liggande texten är inte "
                              "spegelvänd (plattan till höger om och under ankaret)",
              "plattans hörn " + tal(pl.cx - pl.w / 2.0) + ", " +
                  tal(pl.cy - pl.h / 2.0) + " mot ankaret " + tal(ax) + ", " +
                  tal(ay) + " (" + std::to_string(pl.n) + " px)");
    }
}

// ------------------------------------------------------------------- H4
//
// EN ETIKETT VID MARKHÖJD: en röd kvadratisk markör (14 px, kolliderar inte)
// i ett Ground-lager vid (−4, 4) på plattan står där grindens projektion av
// (−4, 4, h(−4, 4)) säger — de röda pixlarnas mitt inom 2 px — och inte vid
// den plana punkten (z = 0), minst 8 px bort. TERRÄNGENS VERSION i byggets
// nyckel (resources.cpp): plattan lyft 3 enheter med
// `Mesh::setPositions` — samma formplats, bara meshens version ändras — och
// markören står vid den nya markhöjden.
void fallH4()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(kLutKamera);
    const int T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    glaze::Mesh mark = platta(false);
    y.addShape(T, glaze::Shape::mesh(mark, {}, {.color = kPlattGrå, .lit = false}));
    const int G = y.addLayer("etiketter");
    y.addShape(G, glaze::Shape::marker({-4, 4}, {.shape = glaze::MarkerShape::Square,
                                                 .size  = glaze::px(14.0f),
                                                 .fill  = kRöd}));
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const Hålkamera h  = hålkamera(kLutKamera, r.box);
    double          ax = 0.0, ay = 0.0, qx = 0.0, qy = 0.0;
    h.proj(-4.0, 4.0, plattH(-4.0, 4.0), ax, ay);
    h.proj(-4.0, 4.0, 0.0, qx, qy);
    // De röda pixlarnas mitt och antal.
    const auto röd = [&](const Bild& bild, double& mx, double& my) {
        double sx = 0.0, sy = 0.0;
        int    n  = 0;
        for (int yy = static_cast<int>(r.box.y); yy < static_cast<int>(r.box.y + r.box.h);
             ++yy)
            for (int xx = static_cast<int>(r.box.x);
                 xx < static_cast<int>(r.box.x + r.box.w); ++xx) {
                const std::uint8_t* p = bild.at(xx, yy);
                if (p[0] > 200 && p[1] < 60 && p[2] < 60) {
                    sx += xx + 0.5;
                    sy += yy + 0.5;
                    ++n;
                }
            }
        mx = n ? sx / n : -1e9;
        my = n ? sy / n : -1e9;
        return n;
    };
    double    mx = 0.0, my = 0.0;
    const int n  = röd(b, mx, my);
    check(n > 0 && std::hypot(mx - ax, my - ay) <= 2.0 &&
              std::hypot(qx - ax, qy - ay) >= 8.0,
          "H4 markören på terräng står vid markhöjd (grindens projektion ±2 "
          "px; den plana punkten 8 px bort)",
          "mitten " + tal(mx) + ", " + tal(my) + " mot " + tal(ax) + ", " + tal(ay) +
              " (plan " + tal(qx) + ", " + tal(qy) + ", " + std::to_string(n) + " px)");
    // Träffen på markören bär ankaret vid markhöjd.
    const glaze::Point lokal{(ax - r.box.x) / r.skala, (ay - r.box.y) / r.skala};
    double             z = -1e9;
    for (const glaze::HitResult& h : y.hitTest(lokal, glaze::HitOptions{}))
        if (h.layer == G && h.point)
            z = h.point->z;
    // Plattans hörn är float — höjden ur dem avviker ≈ 1e-7.
    check(std::abs(z - plattH(-4.0, 4.0)) < 1e-5,
          "H4 träffen på markören ligger vid markhöjd",
          tal(z) + " mot " + tal(plattH(-4.0, 4.0)));
    // Plattan lyfts 3 enheter i samma mesh.
    std::vector<glaze::Float3> lyft;
    for (int j = 0; j <= 8; ++j)
        for (int i = 0; i <= 8; ++i) {
            const double x = -8.0 + 2.0 * i, yv = -8.0 + 2.0 * j;
            lyft.push_back({static_cast<float>(x), static_cast<float>(yv),
                            static_cast<float>(plattH(x, yv) + 3.0)});
        }
    mark.setPositions(0, lyft);
    const Bild b2 = r.bild();
    double     lx = 0.0, ly = 0.0, mx2 = 0.0, my2 = 0.0;
    h.proj(-4.0, 4.0, plattH(-4.0, 4.0) + 3.0, lx, ly);
    const int n2 = röd(b2, mx2, my2);
    check(n2 > 0 && std::hypot(mx2 - lx, my2 - ly) <= 2.0 &&
              std::hypot(lx - ax, ly - ay) >= 8.0,
          "H4 terrängens version i byggets nyckel: plattan lyft 3 enheter "
          "(Mesh::setPositions) — markören står vid den nya markhöjden (±2 px)",
          "mitten " + tal(mx2) + ", " + tal(my2) + " mot " + tal(lx) + ", " + tal(ly) +
              " (förut " + tal(ax) + ", " + tal(ay) + ")");
}

// ------------------------------------------------------------------- H5
//
// LIGGANDE TEXT (`Alignment::Ground`): en röd platta bakom en text ("WWWWWW",
// 16 px, luft 3 px) mitt i en karta (kartkameran, y uppåt), mätt som de röda
// pixlarnas ruta. Rakt uppifrån och obeviken är den bred; vriden 90° står den
// på högkant (den följer kartan); tiltad 50° är dess höjd förkortad med
// cos 50° (±12 %) medan bredden står kvar (±10 %) — en stående etikett hade
// varit oförändrad. Samma i 3D-målet (setDepth) och för en `text3` i ett
// World-lager (Glyph3-programmet).
void fallH5()
{
    const glaze::TextStyle stil{.size       = glaze::px(16.0f),
                                .color      = gfx::Color{255, 255, 255, 255},
                                .background = kRöd,
                                .padding    = glaze::px(3.0f),
                                .rotation   = glaze::Alignment::Ground,
                                .collide    = false};
    const auto mät = [&](int väg, float bäring, float tilt) {
        Rit3 r;
        if (!r.öppna())
            return RödRuta{};
        glaze::Canvas& y = r.f.yta;
        y.setYUp(true);
        y.setCamera(glaze::Camera{.center = {0, 0}, .zoom = 4, .bearing = bäring,
                                  .pitch = tilt});
        y.setMaxPitch(60.0f);
        if (väg == 2) {
            y.addShape(glaze::Shape::text3({0, 0, 0}, "WWWWWW", stil));
        } else {
            y.addShape(glaze::Shape::text({0, 0}, "WWWWWW", stil));
            if (väg == 1)
                y.setDepth(true);
        }
        const Bild b = r.bild();
        return rödRuta(b, r.box);
    };
    const char* vägar[3] = {"2D-vägen", "3D-målet", "text3 (Glyph3)"};
    for (int väg = 0; väg < 3; ++väg) {
        const RödRuta platt = mät(väg, 0.0f, 0.0f);
        const RödRuta vriden = mät(väg, 90.0f, 0.0f);
        const RödRuta tiltad = mät(väg, 0.0f, 50.0f);
        if (g_ritfel)
            return;
        const std::string namn = std::string("H5 ") + vägar[väg];
        check(platt.n > 0 && platt.w > 2.0 * platt.h && vriden.h > 2.0 * vriden.w &&
                  std::abs(vriden.h - platt.w) <= 3.0,
              namn + ": den liggande texten vrids med kartan (bred uppifrån, på "
                     "högkant vid bäringen 90°)",
              tal(platt.w) + "×" + tal(platt.h) + " → " + tal(vriden.w) + "×" +
                  tal(vriden.h));
        const double kvot  = platt.h > 0.0 ? tiltad.h / platt.h : 0.0;
        const double bredd = platt.w > 0.0 ? tiltad.w / platt.w : 0.0;
        const double cos50 = std::cos(50.0 * 3.14159265358979 / 180.0);
        check(std::abs(kvot - cos50) <= 0.12 && std::abs(bredd - 1.0) <= 0.10,
              namn + ": tiltad 50° är höjden förkortad med cos 50° och bredden "
                     "kvar",
              "höjden × " + tal(kvot) + " (cos 50° = " + tal(cos50) +
                  "), bredden × " + tal(bredd));
    }
}

// ------------------------------------------------------------------ H5b
//
// DEN LIGGANDE TEXTENS KOLLISIONSRUTA ÄR DEN PROJICERADE
// QUADENS skärmruta (labels.cpp), inte den upprätta etikettens: två liggande
// texter med kollisionen på — en röd platta vid (0, 0) och en blå norr om den
// — tiltade 50°. Avståndet väljs ur grindens EGNA mätningar av de ritade
// plattorna: höjden uppifrån (h0) och tiltad (h50), och skärmavståndet per
// världsenhet under tilten (två texter 10 enheter isär, utan kollision —
// nära det avstånd som prövas, så perspektivets ändring längs dem är liten).
// Med mittavståndet (h0 + h50) / 2 krockar de RITADE plattorna inte — båda
// syns — fast en oförkortad ruta (höjden h0) hade krockat. Kontrollen:
// mittavståndet 0,7 · h50 — de ritade plattorna krockar, och den blå (den
// senare, lika prioritet) döljs. Samma tre vägar som H5.
void fallH5b()
{
    const auto stil = [](gfx::Color platta, bool kollision) {
        return glaze::TextStyle{.size       = glaze::px(16.0f),
                                .color      = gfx::Color{255, 255, 255, 255},
                                .background = platta,
                                .padding    = glaze::px(3.0f),
                                .rotation   = glaze::Alignment::Ground,
                                .collide    = kollision};
    };
    struct Par {
        RödRuta röd, blå;
    };
    // Den röda vid (0, 0) och, med `avstånd` > 0, den blå vid (0, avstånd).
    const auto mät = [&](int väg, float tilt, double avstånd, bool kollision) {
        Par  ut;
        Rit3 r;
        if (!r.öppna())
            return ut;
        glaze::Canvas& y = r.f.yta;
        y.setYUp(true);
        y.setCamera(glaze::Camera{.center = {0, 0}, .zoom = 4, .pitch = tilt});
        y.setMaxPitch(60.0f);
        const auto lägg = [&](double ny, gfx::Color c) {
            if (väg == 2)
                y.addShape(glaze::Shape::text3({0, ny, 0}, "WWWWWW", stil(c, kollision)));
            else
                y.addShape(glaze::Shape::text({0, ny}, "WWWWWW", stil(c, kollision)));
        };
        lägg(0.0, kRöd);
        if (avstånd > 0.0)
            lägg(avstånd, kBlå);
        if (väg == 1)
            y.setDepth(true);
        const Bild b = r.bild();
        ut.röd       = rödRuta(b, r.box);
        ut.blå       = blåRuta(b, r.box);
        return ut;
    };
    const char* vägar[3] = {"2D-vägen", "3D-målet", "text3 (Glyph3)"};
    for (int väg = 0; väg < 3; ++väg) {
        const double h0  = mät(väg, 0.0f, 0.0, false).röd.h;
        const double h50 = mät(väg, 50.0f, 0.0, false).röd.h;
        const Par    skala = mät(väg, 50.0f, 10.0, false);
        if (g_ritfel)
            return;
        const double per = std::abs(skala.röd.cy - skala.blå.cy) / 10.0;
        const std::string namn = std::string("H5b ") + vägar[väg];
        if (!(h0 > 0.0 && h50 > 0.0 && per > 0.0 && skala.blå.n > 0)) {
            check(false, namn + ": grindens mätningar (plattornas höjd och skalan)",
                  "h0 " + tal(h0) + ", h50 " + tal(h50) + ", " + tal(per) +
                      " px per enhet");
            continue;
        }
        // Mittavståndet (h0 + h50) / 2: de ritade plattorna går fria, en
        // oförkortad ruta hade krockat — premissen mäts i bilden.
        const Par    fria   = mät(väg, 50.0f, (h0 + h50) / 2.0 / per, true);
        const double mitt   = std::abs(fria.röd.cy - fria.blå.cy);
        const bool   premiss = fria.blå.n > 0 &&
                             mitt > (fria.röd.h + fria.blå.h) / 2.0 && mitt < h0;
        check(fria.röd.n > 0 && fria.blå.n > 0 && premiss,
              namn + ": två liggande texter vars ritade plattor går fria under "
                     "tilt syns båda (en oförkortad ruta hade krockat)",
              "mittavståndet " + tal(mitt) + " px, plattorna " + tal(fria.röd.h) +
                  " och " + tal(fria.blå.h) + ", h0 " + tal(h0) +
                  (fria.blå.n > 0 ? "" : " — den blå DOLD"));
        const Par krock = mät(väg, 50.0f, 0.7 * h50 / per, true);
        check(krock.röd.n > 0 && krock.blå.n == 0,
              namn + ": kontrollen — ritade plattor som krockar: den senare döljs",
              std::to_string(krock.röd.n) + " röda, " + std::to_string(krock.blå.n) +
                  " blå px");
    }
}

// ------------------------------------------------------------------ H5c
//
// DEN LIGGANDE TEXTEN BLIR ALDRIG SPEGELVÄND: en röd platta
// med textblockets övre vänstra hörn som ankare (`TextAnchor::TopLeft`) vid
// kartans mitt, tiltad 50°. Baslinjen och "nedåt" ska bilda skärmens ram (x
// höger, y ned) — plattan ligger alltså till HÖGER om och UNDER ankaret, med
// kartans y uppåt och med y nedåt (där världens y redan går nedåt på
// skärmen), och till VÄNSTER om och OVANFÖR det när kartan är vriden 180°
// (texten upp och ner, som tryckt på marken — vriden, inte speglad). Plattans
// luft (3 px) sticker ut på ankarets sida; toleransen är den + 2 px. Samma tre
// vägar.
void fallH5c()
{
    const glaze::TextStyle stil{.size       = glaze::px(16.0f),
                                .color      = gfx::Color{255, 255, 255, 255},
                                .background = kRöd,
                                .padding    = glaze::px(3.0f),
                                .anchor     = glaze::TextAnchor::TopLeft,
                                .rotation   = glaze::Alignment::Ground,
                                .collide    = false};
    struct Läge {
        bool        yUpp;
        float       bäring;
        bool        högerUnder;   // annars vänster och ovanför
        const char* namn;
    };
    const Läge lägen[3] = {{true, 0.0f, true, "y uppåt"},
                           {false, 0.0f, true, "y nedåt"},
                           {true, 180.0f, false, "vriden 180°"}};
    const char* vägar[3] = {"2D-vägen", "3D-målet", "text3 (Glyph3)"};
    for (int väg = 0; väg < 3; ++väg)
        for (const Läge& l : lägen) {
            Rit3 r;
            if (!r.öppna())
                return;
            glaze::Canvas& y = r.f.yta;
            y.setYUp(l.yUpp);
            y.setCamera(glaze::Camera{.center = {0, 0}, .zoom = 4, .bearing = l.bäring,
                                      .pitch = 50.0f});
            y.setMaxPitch(60.0f);
            if (väg == 2)
                y.addShape(glaze::Shape::text3({0, 0, 0}, "WWWWWW", stil));
            else
                y.addShape(glaze::Shape::text({0, 0}, "WWWWWW", stil));
            if (väg == 1)
                y.setDepth(true);
            const Bild b = r.bild();
            if (g_ritfel)
                return;
            const RödRuta pl = rödRuta(b, r.box);
            const double  ax = r.box.x + r.box.w / 2.0, ay = r.box.y + r.box.h / 2.0;
            const double  x0 = pl.cx - pl.w / 2.0, y0 = pl.cy - pl.h / 2.0;
            const double  x1 = pl.cx + pl.w / 2.0, y1 = pl.cy + pl.h / 2.0;
            const double  tol = 2.0 + 3.0 * r.skala;   // + plattans luft
            const bool    ok  = pl.n > 0 && (l.högerUnder
                                                 ? x0 >= ax - tol && y0 >= ay - tol
                                                 : x1 <= ax + tol && y1 <= ay + tol);
            check(ok,
                  std::string("H5c ") + vägar[väg] + ", " + l.namn +
                      ": den liggande texten är inte spegelvänd (plattan " +
                      (l.högerUnder ? "till höger om och under" : "till vänster om och ovanför") +
                      " ankaret)",
                  "plattan " + tal(x0) + "…" + tal(x1) + " × " + tal(y0) + "…" + tal(y1) +
                      ", ankaret " + tal(ax) + ", " + tal(ay));
        }
}

// ------------------------------------------------------------------- H6
//
// DRAPERINGEN RITAS OM BARA VID ÄNDRING (`DrapeCache::renders`, internt):
// plattan uppifrån med en linje i ett Ground-lager. En ny frame utan ändring
// och en panorering inom klassen (ortografiskt — avtrycket står still) ritar
// inte om; en ändrad linje ritar om plattan en gång, liksom en zoom 4 × (en ny
// klass); och en linje i en CHUNK som inte når plattan (långt bort — lagrets
// chunkar är nyckelns korn, drape.h) ritar inte om den.den
// liggande texten på terräng hamnar i draperingen och syns på plattan.
void fallH6()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setYUp(true);
    glaze::Camera3D kam = uppifrånKamera({0, 0, 0}, 10);
    y.setCamera(kam);
    const int T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    y.addShape(T, glaze::Shape::mesh(platta(false), {},
                                     {.color = kPlattGrå, .lit = false}));
    const int G = y.addLayer("vägar");
    y.addShape(G, glaze::Shape::polyline({{-6, 2}, {6, 2}},
                                         {.paint = kRöd, .width = glaze::px(4.0f)}));
    r.bild();
    if (g_ritfel)
        return;
    const std::uint64_t start = draperingar(r);
    r.bild();
    const std::uint64_t igen = draperingar(r);
    kam.target = {0.4, -0.3, 0};
    y.setCamera(kam);
    r.bild();
    const std::uint64_t panorerat = draperingar(r);
    check(start >= 1 && igen == start && panorerat == start,
          "H6 ingen omritning utan ändring eller vid en panorering inom klassen",
          std::to_string(start) + " → " + std::to_string(igen) + " → " +
              std::to_string(panorerat));
    y.setShape(G, 0, glaze::Shape::polyline({{-6, -2}, {6, -2}},
                                            {.paint = kRöd, .width = glaze::px(4.0f)}));
    r.bild();
    const std::uint64_t ändrat = draperingar(r);
    y.addShape(G, glaze::Shape::polyline({{9000, 30}, {9010, 30}}, {.paint = kRöd}));
    r.bild();
    const std::uint64_t utanför = draperingar(r);
    kam.orthoHeight = 2.5;
    y.setCamera(kam);
    r.bild();
    const std::uint64_t zoomat = draperingar(r);
    check(ändrat == panorerat + 1 && utanför == ändrat && zoomat == utanför + 1,
          "H6 en ändrad linje ritar om plattan en gång, en linje i en chunk "
          "som inte når den inte, och en zoom 4× (ny klass) en gång",
          std::to_string(panorerat) + " → " + std::to_string(ändrat) + " → " +
              std::to_string(utanför) + " → " + std::to_string(zoomat));
    // Den liggande texten på terräng — draperad.
    kam.orthoHeight = 10;
    y.setCamera(kam);
    y.setShapes(G, {glaze::Shape::text({-3, -5}, "MMMM",
                                       {.size       = glaze::px(14.0f),
                                        .color      = gfx::Color{255, 255, 255, 255},
                                        .background = kRöd,
                                        .rotation   = glaze::Alignment::Ground})});
    const Bild     b = r.bild();
    const Uppifrån u = uppifrån(r, 10);
    check(rödNära(b, u.x(-3), u.y(-5), 3),
          "H6 den liggande texten på terräng syns på plattan "
          "(draperad)",
          px(b.at(u.x(-3), u.y(-5))));
}

// ------------------------------------------------------------------ H6b
//
// EN DEL-VY MED TERRÄNG RITAR INTE OM DRAPERINGEN: plattan
// uppifrån med två Ground-lager — det ena i huvudvyn OCH en del-vy (uppe
// till höger, en egen kamera med halva skalan), det andra bara i huvudvyn —
// och terrängen i båda. Draperingen är HUVUDVYNS (drape.h): efter den första
// framen ritar ingen frame om den, fast vyn ser ett annat lagerurval och en
// annan zoom än huvudvyn — en nyckel per vy hade ritat om den två gånger
// varje frame. Del-vyn visar huvudvyns drapering: båda lagrens linjer.
void fallH6b()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setYUp(true);
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const int T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    y.addShape(T, glaze::Shape::mesh(platta(false), {},
                                     {.color = kPlattGrå, .lit = false}));
    // Linjerna en världsenhet breda — synliga också i vyns halva skala.
    const int G1 = y.addLayer("vägar");
    y.addShape(G1, glaze::Shape::polyline({{-6, 2}, {6, 2}},
                                          {.paint = kRöd, .width = glaze::world(1.0f)}));
    const int G2 = y.addLayer("stigar");
    y.addShape(G2, glaze::Shape::polyline({{-6, -2}, {6, -2}},
                                          {.paint = kBlå, .width = glaze::world(1.0f)}));
    const int v = y.addView(glaze::View{.anchor = gfx::TextAnchor::TopRight,
                                        .width  = 120,
                                        .height = 100,
                                        .camera = uppifrånKamera({0, 0, 0}, 20),
                                        .background = gfx::Color{255, 255, 255, 255}});
    y.setLayerViews(T, {0, v});
    y.setLayerViews(G1, {0, v});
    r.bild();
    if (g_ritfel)
        return;
    r.bild();
    const std::uint64_t före = draperingar(r);
    r.bild();
    r.bild();
    const Bild          b     = r.bild();
    const std::uint64_t efter = draperingar(r);
    check(före >= 1 && efter == före,
          "H6b en del-vy med terräng och ett annat lagerurval ritar inte om "
          "draperingen frame för frame",
          std::to_string(före) + " → " + std::to_string(efter) + " omritningar");
    // Del-vyns ruta (uppe till höger, 120 × 100 logiska px): båda linjerna.
    const gfx::Rectf vy{r.box.x + r.box.w - 120.0f * r.skala, r.box.y,
                        120.0f * r.skala, 100.0f * r.skala};
    check(rödRuta(b, vy).n > 0 && blåRuta(b, vy).n > 0,
          "H6b del-vyn visar huvudvyns drapering — också lagret utanför vyn",
          std::to_string(rödRuta(b, vy).n) + " röda, " +
              std::to_string(blåRuta(b, vy).n) + " blå px");
}

// ------------------------------------------------------------------ H6c
//
// ETT SPRÅKBYTE RITAR OM DRAPERINGEN: en liggande text med
// `tr(id)` på plattan, uppifrån. Språkbytet bygger om texten utan att formerna
// ändrats — lagrets `textVersion` står i draperingens markstämpel — och
// plattan ritas om en gång; en bild utan byte ritar inte om.
void fallH6c()
{
    const std::string förra = glaze::language();
    glaze::setLanguage("sv");
    {
        Rit3 r;
        if (r.öppna()) {
            glaze::Canvas& y = r.f.yta;
            y.setYUp(true);
            y.setCamera(uppifrånKamera({0, 0, 0}, 10));
            const int T = y.addLayer("terräng");
            y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
            y.addShape(T, glaze::Shape::mesh(platta(false), {},
                                             {.color = kPlattGrå, .lit = false}));
            const int G = y.addLayer("text");
            y.addShape(G, glaze::Shape::text({-3, -5}, glaze::tr(glaze::str::Cancel),
                                             {.size       = glaze::px(14.0f),
                                              .color      = gfx::Color{255, 255, 255, 255},
                                              .background = kRöd,
                                              .rotation   = glaze::Alignment::Ground}));
            r.bild();
            r.bild();
            const std::uint64_t före = draperingar(r);
            r.bild();
            const std::uint64_t still = draperingar(r);
            glaze::setLanguage("en");
            r.bild();
            const std::uint64_t bytt = draperingar(r);
            if (!g_ritfel)
                check(före >= 1 && still == före && bytt == still + 1,
                      "H6c ett språkbyte på en liggande text med tr(id) ritar om "
                      "plattan en gång; en bild utan byte ritar inte om",
                      std::to_string(före) + " → " + std::to_string(still) + " → " +
                          std::to_string(bytt));
        }
    }
    glaze::setLanguage(förra);
}

// ------------------------------------------------------------------- H7
//
// DRAPERINGEN MED BUDGET = BILDEN UTAN: 8 × 8 plana rutor om 2
// enheter och ett Ground-lager med en röd yta över alla, sedda lågt ur en
// förstapersonskamera — rutorna nära ögat är stora (ett hörn bakom närplanet
// ger taket, 2048), så en ändring som når alla kräver flera bilders budget.
// Ytan blir blå; EN skärmbild ritar inom budgeten, och `renderToBitmap`
// (`DrapeMode::Complete`) ritar resten — premissen: bilden ritade minst två.
// Skärmen ritar sedan klart, och dess bild är pixellik bildens (±1 per kanal,
// de rundade hörnen undantagna — I4:s mätning).
void fallH7()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas&        y   = r.f.yta;
    const glaze::Camera3D kam = glaze::Camera3D::lookFrom({0.3, -0.7, 0.9}, 0.0f, -30.0f);
    y.setCamera(kam);
    y.setMemoryBudget(std::size_t{1} << 30);   // rutorna vid ögat är 16 MB var
    const int T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) {
            glaze::MeshDesc d;
            d.origin    = {-8.0 + 2.0 * i, -8.0 + 2.0 * j, 0.0};
            d.positions = {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}};
            d.indices   = {0, 1, 2, 0, 2, 3};
            y.addShape(T, glaze::Shape::mesh(glaze::Mesh(std::move(d)), {},
                                             {.color = kPlattGrå, .lit = false}));
        }
    const int                       G = y.addLayer("yta");
    const std::vector<glaze::Point> ring{{-8, -8}, {8, -8}, {8, 8}, {-8, 8}};
    y.addShape(G, glaze::Shape::polygon(ring, kRöd));
    // Bilder tills draperingen står still.
    const auto stilla = [&]() {
        Bild b;
        for (int i = 0; i < 30; ++i) {
            const std::uint64_t f0 = draperingar(r);
            b = r.bild();
            if (g_ritfel || draperingar(r) == f0)
                break;
        }
        return b;
    };
    stilla();
    if (g_ritfel)
        return;
    y.setShape(G, 0, glaze::Shape::polygon(ring, kBlå));
    r.bild();   // EN bild inom budgeten
    const int x0 = static_cast<int>(std::floor(r.box.x));
    const int y0 = static_cast<int>(std::floor(r.box.y));
    const int w  = static_cast<int>(std::ceil(r.box.x + r.box.w)) - x0;
    const int h  = static_cast<int>(std::ceil(r.box.y + r.box.h)) - y0;
    const std::uint64_t          f1 = draperingar(r);
    std::optional<glaze::Bitmap> bm = y.renderToBitmap(w, h, kam);
    const std::uint64_t          iBilden = draperingar(r) - f1;
    const Bild                   s       = stilla();
    if (g_ritfel)
        return;
    check(bm && bm->valid() && bm->width() == w && bm->height() == h && iBilden >= 2,
          "H7 premissen: en skärmbild inom budgeten lämnade minst två draperingar åt "
          "bilden (renderToBitmap ritar komplett)",
          std::to_string(iBilden) + " ritade i bilden");
    if (!bm || !bm->valid() || bm->width() != w || bm->height() != h)
        return;
    const int rad = static_cast<int>(std::ceil(r.f.style().cornerRadius * r.skala));
    const std::uint8_t* q      = bm->pixels();
    int                 störst = 0, blå = 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            if ((i < 2 * rad || i >= w - 2 * rad) && (j < 2 * rad || j >= h - 2 * rad))
                continue;
            const std::uint8_t* p = s.at(x0 + i, y0 + j);
            const std::uint8_t* b = q + (static_cast<std::size_t>(j) * w + i) * 4;
            for (int k = 0; k < 3; ++k)
                störst = std::max(störst, std::abs(p[k] - b[k]));
            blå += b[2] > 200 && b[0] < 80 && b[1] < 80;
        }
    check(blå >= 500 && störst <= 1,
          "H7 draperingen ritad klart med budget är pixellik renderToBitmap "
          "(komplett) — ±1 per kanal, de rundade hörnen undantagna",
          "största skillnad " + std::to_string(störst) + ", " + std::to_string(blå) +
              " blå px i bilden");
}

// ------------------------------------------------------------------- H8
//
// EN BILD SOM VÄNTAR PÅ UPPLADDNINGSBUDGETEN RITAS IN I
// DRAPERINGEN NÄR DEN KOMMER: två bilder (röd och blå, 8 × 8) i ett
// Ground-lager över plattan, uppifrån. Plattan är resident först; sedan visas
// bilderna med budgeten 1 byte — framens första uppladdning går alltid
// igenom, så de kommer en per bild. Bildcachens innehållsgeneration i
// markstämpeln räknar om plattans nyckel när den andra kommer: efter några
// bilder syns båda. Premissen: den första bilden visade inte båda.
void fallH8()
{
    Rit3 r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setYUp(true);
    y.setCamera(uppifrånKamera({0, 0, 0}, 10));
    const int T = y.addLayer("terräng");
    y.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    y.addShape(T, glaze::Shape::mesh(platta(false), {}, {.color = kPlattGrå, .lit = false}));
    const auto enfärgad = [](gfx::Color c) {
        std::vector<std::uint8_t> pixlar;
        for (int i = 0; i < 64; ++i)
            pixlar.insert(pixlar.end(), {c.r, c.g, c.b, c.a});
        glaze::Bitmap b(8, 8, pixlar.data(), 32);
        b.setFilter(gfx::ImageFilter::Nearest);
        return b;
    };
    const glaze::Bitmap röd = enfärgad(kRöd), blå = enfärgad(kBlå);
    const int           G   = y.addLayer("flygfoto");
    y.addShape(G, glaze::Shape::image({-6, -6, 4, 4}, röd));
    y.addShape(G, glaze::Shape::image({2, 2, 4, 4}, blå));
    y.setLayerVisible(G, false);
    r.bild();
    r.bild();
    if (g_ritfel)
        return;
    const std::size_t förval = glaze::app().canvasUploadBudget();
    glaze::app().setCanvasUploadBudget(1);
    y.setLayerVisible(G, true);
    const Bild första = r.bild();
    Bild       sist;
    for (int i = 0; i < 6; ++i)
        sist = r.bild();
    glaze::app().setCanvasUploadBudget(förval);
    if (g_ritfel)
        return;
    const Uppifrån u          = uppifrån(r, 10);
    const bool     rödFörst   = ärFärg(första.at(u.x(-4), u.y(-4)), kRöd, 3);
    const bool     blåFörst   = ärFärg(första.at(u.x(4), u.y(4)), kBlå, 3);
    const bool     rödSist    = ärFärg(sist.at(u.x(-4), u.y(-4)), kRöd, 3);
    const bool     blåSist    = ärFärg(sist.at(u.x(4), u.y(4)), kBlå, 3);
    check(!(rödFörst && blåFörst),
          "H8 premissen: budgeten släppte inte båda bilderna i den första bilden",
          px(första.at(u.x(-4), u.y(-4))) + " / " + px(första.at(u.x(4), u.y(4))));
    check(rödSist && blåSist,
          "H8 en bild som väntade på uppladdningsbudgeten ritas in i draperingen "
          "när den kommer",
          px(sist.at(u.x(-4), u.y(-4))) + " / " + px(sist.at(u.x(4), u.y(4))));
}

} // namespace

// ============================================================ DEL J
//
// MARKDJUPET (target3d.h) genom en RIKTIG glaze::Canvas: planet
// z = 0 som bara skriver djup, så att marken döljer det som ligger under den.
// Planet ritas när ett RITAT Ground-lager har något som målas på marken
// (scene.h `paintsGround`) — ritytans lager 0 finns alltid och är tomt, och
// det räcker inte — och det som STÅR på marken skärs aldrig av det. Förväntat värde är
// grindens eget: en primitiv om s logiska px täcker lika många px under tilt
// som rakt uppifrån (skärmstorleken är konstant), symmetriskt kring sin mitt
// (`project`), och en låda under marken syns eller inte.
//   J1  punkter, en Line3 och markörer PÅ z = 0 i ett World-lager — och en
//       2D-markör och en 2D-linje i px i ett World-lager (z = 0, med djup) —
//       är HELA under tilt (elevation 60° och 30°): lika många röda px som
//       rakt uppifrån och den nedre halvan som den övre, med ett TOMT
//       Ground-lager, med ett Ground-lager med innehåll (planet ritas) och
//       utan Ground-lager — och rakt uppifrån med planet (biasen).
//   J2  planet RITAS med markinnehåll: en röd låda under marken är dold rakt
//       uppifrån (ortografiskt) och bakom en lutad blick; utan markinnehåll
//       (bara det tomma lagret 0) syns den — planet ritas inte; lådan OVANFÖR
//       marken syns med planet (kontrollen).
//   J3  INGET LÄCKER under tilt: en tunn röd skiva 0,25–0,6 enheter under
//       marken syns inte vid elevationen 30° (en djupbias som skjuter planet
//       så långt bakåt att punkterna klarade sig hade läckt den).
//   J4  en YTA PÅ MARKEN i ett World-lager (polygon3 på z = 0 — samma djup
//       som planet) är hel rakt uppifrån och under tilt: planets djupbias
//       bakåt.
// INTE MÄTT i del J: Metal; text3 (glyfprogrammet mäts med
// markörerna — samma hörnväg); en primitiv vars ankare står HÖGRE än marken
// (planet genom ankaret är då ovanför marken och skyddar den mer); en
// skärmvänd primitiv bakom en mesh nära marken (dess nedre del dras fram
// högst en fjärdedel av djupet, punkterna högst fyra radier — skriven gräns
// i target3d.h; J5 mäter horisonten och taket);
// och träffprovet (pick_gate R8b).
// SABOTAGEN — alla fäller grinden på D3D11 och GL: markdjupet
// varje gång ett Ground-lager finns (J2 det tomma lagret,
// J3 kontrollen), djupbiasen borta (J4 rakt uppifrån; i GL också J1 rakt
// uppifrån för alla fem slagen — på D3D11 vann de där ändå), hörnen utan
// planets djup i Points (J1 punkten), i Line-källan (J1 Line3 och 2D-linjen)
// och i Glyph-källan (J1 marker3 och 2D-markören). Träffprovets markregel
// utan `paintsGround` fälls av pick_gate R8b.

const gfx::Color kMarkGrå{200, 200, 200, 255};   // markens innehåll

enum class Mark { Tom, Innehåll, Ingen };

const char* markNamn(Mark m)
{
    return m == Mark::Tom ? "tomt Ground-lager"
           : m == Mark::Innehåll ? "Ground-lager med innehåll"
                                 : "inget Ground-lager";
}

// Ritytans mark: lager 0 tomt, med en grå ruta (planet ritas), eller World.
void sättMark(glaze::Canvas& y, Mark m)
{
    if (m == Mark::Innehåll)
        y.addShape(0, glaze::Shape::rect({-200, -200, 400, 400}, kMarkGrå));
    else if (m == Mark::Ingen)
        y.setLayerPlacement(0, glaze::LayerPlacement::World);
}

glaze::Camera3D markKamera(float elevation)
{
    return glaze::Camera3D{.target = {0, 0, 0}, .distance = 30,
                           .elevation = elevation};
}

// De röda pixlarnas TYNGDPUNKT i y (pixelmitten) i en ruta kring (cx, cy),
// fönstrets px — NaN utan röda.
double rödTyngdY(const Bild& b, int cx, int cy, int halv)
{
    double sum = 0.0;
    int    n   = 0;
    for (int y = cy - halv; y <= cy + halv; ++y)
        for (int x = cx - halv; x <= cx + halv; ++x) {
            const std::uint8_t* p = b.at(x, y);
            if (p[0] > 200 && p[1] < 80 && p[2] < 80) {
                sum += y + 0.5;
                ++n;
            }
        }
    return n > 0 ? sum / n : std::nan("");
}

// ------------------------------------------------------------------- J1
//
// DET SOM STÅR PÅ MARKEN: varje slag i ett eget fönster, mätt kring sitt
// ankare. Referensen är samma rityta rakt uppifrån med markinnehåll — där
// syns primitiven hel (skärmen och planet är parallella). Hel betyder lika
// många röda px som referensen (−8 %) och tyngdpunkten på ankaret (±1 px):
// en primitiv vars nedre halva skärs av planet har tyngdpunkten ≈ en fjärdedel
// av sin höjd ovanför.
enum class Stående { Punkt, Linje3, Markör3, Markör2D, Linje2D };

const char* ståendeNamn(Stående s)
{
    switch (s) {
    case Stående::Punkt:    return "punkten (Points)";
    case Stående::Linje3:   return "Line3 i px";
    case Stående::Markör3:  return "marker3 (Glyph3)";
    case Stående::Markör2D: return "2D-markören (Glyph) i ett World-lager";
    case Stående::Linje2D:  return "2D-linjen i px (Line) i ett World-lager";
    }
    return "";
}

void läggStående(glaze::Canvas& y, int L, Stående s)
{
    const glaze::MarkerStyle m{.shape = gfx::MarkerShape::Square,
                               .size  = glaze::px(10.0f),
                               .fill  = kRöd};
    switch (s) {
    case Stående::Punkt:
        y.addShape(L, glaze::Shape::points3({{0, 0, 0}}, {.size = glaze::px(10.0f),
                                                          .color = kRöd}));
        break;
    case Stående::Linje3:
        y.addShape(L, glaze::Shape::line3({-8, 0, 0}, {8, 0, 0},
                                          {.paint = kRöd, .width = glaze::px(10.0f)}));
        break;
    case Stående::Markör3:
        y.addShape(L, glaze::Shape::marker3({0, 0, 0}, m));
        break;
    case Stående::Markör2D:
        y.addShape(L, glaze::Shape::marker({0, 0}, m));
        break;
    case Stående::Linje2D:
        y.addShape(L, glaze::Shape::line({-8, 0}, {8, 0},
                                         {.paint = kRöd, .width = glaze::px(10.0f)}));
        break;
    }
}

struct StåendeMått {
    int    alla = 0;
    double dy   = 0.0;     // tyngdpunkten − ankaret, fysiska px (+ nedåt)
    bool   ok   = false;   // ankaret gick att projicera
};

StåendeMått mätStående(Stående s, Mark mark, float elevation)
{
    StåendeMått ut;
    Rit3        r;
    if (!r.öppna())
        return ut;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(markKamera(elevation));
    sättMark(y, mark);
    const int L = y.addLayer("står på marken");
    y.setLayerPlacement(L, glaze::LayerPlacement::World);
    läggStående(y, L, s);
    const Bild b = r.bild();
    if (g_ritfel)
        return ut;
    const std::optional<glaze::Point> p = y.project({0, 0, 0});
    if (!p)
        return ut;
    const double ay = r.box.y + p->y * r.skala;   // ankaret, fönstrets px
    const int    cx = r.x(p->x), cy = static_cast<int>(ay);
    ut.alla = rödaKring(b, cx, cy, 14);
    ut.dy   = rödTyngdY(b, cx, cy, 14) - ay;
    ut.ok   = ut.alla > 0;
    return ut;
}

std::string ståendeTal(const StåendeMått& t)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d röda, tyngdpunkten %+.2f px", t.alla, t.dy);
    return buf;
}

void fallJ1()
{
    for (const Stående s : {Stående::Punkt, Stående::Linje3, Stående::Markör3,
                            Stående::Markör2D, Stående::Linje2D}) {
        const StåendeMått ref = mätStående(s, Mark::Innehåll, 90.0f);
        if (g_ritfel)
            return;
        // Rakt uppifrån med planet: djupet lika med planets — biasen bär.
        check(ref.ok && ref.alla >= 60 && std::abs(ref.dy) <= 1.0,
              std::string("J1 ") + ståendeNamn(s) +
                  " på z = 0 rakt uppifrån med markinnehåll: hel, tyngdpunkten på "
                  "ankaret",
              ståendeTal(ref));
        for (const Mark m : {Mark::Tom, Mark::Innehåll, Mark::Ingen})
            for (const float el : {60.0f, 30.0f}) {
                const StåendeMått t = mätStående(s, m, el);
                if (g_ritfel)
                    return;
                check(t.ok && t.alla * 100 >= ref.alla * 92 && std::abs(t.dy) <= 1.0,
                      std::string("J1 ") + ståendeNamn(s) + " på z = 0, elevation " +
                          std::to_string(static_cast<int>(el)) + "°, " + markNamn(m) +
                          ": hel — lika många röda px som rakt uppifrån och "
                          "tyngdpunkten på ankaret",
                      ståendeTal(t) + " (rakt uppifrån " + std::to_string(ref.alla) + ")");
            }
    }
}

// ------------------------------------------------------------------- J2
//
// PLANET RITAS — eller inte: en röd låda 6 × 6 under marken (z −3 … −1) i
// ett World-lager, sedd ortografiskt rakt uppifrån och lutad.
int rödaUnderMarken(Mark mark, const glaze::Camera3D& kam, double z0, double z1)
{
    Rit3 r;
    if (!r.öppna())
        return -1;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(kam);
    sättMark(y, mark);
    const int L = y.addLayer("lådan");
    y.setLayerPlacement(L, glaze::LayerPlacement::World);
    y.addShape(L, glaze::Shape::box({{-3, -3, z0}, {3, 3, z1}},
                                    {.color = kRöd, .lit = false}));
    const Bild b = r.bild();
    if (g_ritfel)
        return -1;
    const std::optional<glaze::Point> p = y.project({0, 0, (z0 + z1) / 2});
    if (!p)
        return -1;
    return rödaKring(b, r.x(p->x), r.y(p->y), 6);
}

void fallJ2()
{
    const glaze::Camera3D uppifrån = uppifrånKamera({0, 0, 0}, 10);
    const glaze::Camera3D lutad    = markKamera(45.0f);
    const int under      = rödaUnderMarken(Mark::Innehåll, uppifrån, -3, -1);
    const int underLutad = rödaUnderMarken(Mark::Innehåll, lutad, -3, -1);
    const int tom        = rödaUnderMarken(Mark::Tom, uppifrån, -3, -1);
    const int tomLutad   = rödaUnderMarken(Mark::Tom, lutad, -3, -1);
    const int ovanför    = rödaUnderMarken(Mark::Innehåll, uppifrån, 1, 3);
    if (g_ritfel)
        return;
    check(under == 0 && underLutad == 0,
          "J2 en låda under marken är dold med markinnehåll — rakt uppifrån och "
          "lutad 45° (planet ritas)",
          std::to_string(under) + " och " + std::to_string(underLutad) + " röda px");
    check(tom >= 150 && tomLutad >= 150,
          "J2 med bara det tomma lagret 0 syns lådan under marken — planet ritas "
          "inte",
          std::to_string(tom) + " och " + std::to_string(tomLutad) + " röda px av 169");
    check(ovanför >= 150, "J2 kontrollen: lådan OVANFÖR marken syns med planet",
          std::to_string(ovanför) + " röda px av 169");
}

// ------------------------------------------------------------------- J3
//
// INGET LÄCKER: en tunn skiva strax under marken (z −0,6 … −0,25), sedd vid
// elevationen 30°. Planets lutningsbias är en px: i den här vyn är en px
// ≈ 0,1 enheter, så en bias som räckte för att punkterna skulle klara sig
// utan planet genom ankaret (≈ 7 px) hade lagt skivan i dagen.
void fallJ3()
{
    const int skivan = rödaUnderMarken(Mark::Innehåll, markKamera(30.0f), -0.6, -0.25);
    const int utan   = rödaUnderMarken(Mark::Tom, markKamera(30.0f), -0.6, -0.25);
    if (g_ritfel)
        return;
    check(skivan == 0 && utan >= 150,
          "J3 en tunn skiva 0,25–0,6 under marken läcker inte igenom planet vid "
          "30° (och syns utan markinnehåll)",
          std::to_string(skivan) + " röda px med planet, " + std::to_string(utan) +
              " utan");
}

// ------------------------------------------------------------------- J4
//
// EN YTA PÅ MARKEN i ett World-lager — polygon3 på z = 0, samma djup som
// planet i varje pixel: djupbiasen bakåt gör att ytan vinner Less.
void fallJ4()
{
    for (const float el : {90.0f, 60.0f, 30.0f}) {
        Rit3 r;
        if (!r.öppna())
            return;
        glaze::Canvas& y = r.f.yta;
        y.setCamera(el == 90.0f ? uppifrånKamera({0, 0, 0}, 10) : markKamera(el));
        sättMark(y, Mark::Innehåll);
        const int L = y.addLayer("ytan");
        y.setLayerPlacement(L, glaze::LayerPlacement::World);
        y.addShape(L, glaze::Shape::polygon3({{-4, -4, 0}, {4, -4, 0}, {4, 4, 0}, {-4, 4, 0}},
                                             kRöd));
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        const std::optional<glaze::Point> p = y.project({0, 0, 0});
        const int n = p ? rödaKring(b, r.x(p->x), r.y(p->y), 5) : -1;
        check(n == 121,
              "J4 en yta på z = 0 i ett World-lager är hel mot planet, elevation " +
                  std::to_string(static_cast<int>(el)) + "° (djupbiasen bakåt)",
              std::to_string(n) + " röda px av 121");
    }
}

// ------------------------------------------------------------------- J5
//
// PUNKTEN VID HORISONTEN: det som står på marken dras fram mot
// planet genom ankaret, och strax under ögats höjd går det planet nästan genom
// ögat — hörnen faller mot närplanet och den nedre halvan vinner djuptestet mot
// allt (en linje av punkter längs horisonten) om draget inte är en ANDEL av
// ankarets djup (inget från ½). Punkterna har dessutom ett TAK på fyra
// radier i världen. Förväntat värde är
// handskrivet: det som står framför skymmer, så inget rött får synas.
//   J5   ögat 0,05 över z = 0 och blicken vågrät (draget ≈ 0,9): varje slag
//        bakom en vit, obelyst låda — noll röda px kring ankaret.
//   J5n  NÄRA SKYMMARE: elevationen 6° (draget helt, ungefär elva radier i
//        hörnet), en vit skiva sex radier framför punkten (≈ 3 % av djupet) —
//        noll röda. Taket på fyra radier är det som håller den bakom.
//   J5t  TÄTT MOLN: ett rutnät vita punkter och bakom dem, tre radier längre
//        bort och en radie högre i bilden, ett rött — inga röda px inne i de
//        vita skivorna (de rödas nedre halvor ligger bakom de vitas övre).
//   J5k  kontrollerna: utan lådan, skivan eller de vita syns det röda.
// AVVIKELSE från granskningens 1,5 radier i J5t: taket släpper hörnet fyra
// radier fram, så en granne 1,5 radier bort skyddas bara av att också den
// dras — tre radier är det avstånd där taket ensamt avgör. MÄTS INTE:
// Metal, muren som reser sig genom planet strax framför en Line- eller
// Glyph-primitiv (bara andelen där, den skrivna gränsen i target3d.h),
// `UpAxis::Y` och ortografisk tilt.
// SABOTAGEN: slutet
// `min(z0, max(zq, 0))` i `kHlslStandSrc` (J5 alla fem i D3D11) och i
// `kGlslStandSrc` (J5 på GL); ½ och ¼ omkastade (J5); golvet i världen
// i stället för andelen (J1 vid 30° med markinnehåll); taket bort
// ur `pointZ` (J5n, J5t).

// Punktens radie (5 logiska px) i världsenheter vid origo.
double j5Radie(glaze::Canvas& y)
{
    const auto a = y.project({0, 0, 0}), b = y.project({1, 0, 0});
    return a && b && b->x != a->x ? 5.0 / std::abs(b->x - a->x) : 0.0;
}

// Röda px i cirkeln kring (cx, cy), fönstrets px.
int rödaICirkel(const Bild& b, double cx, double cy, double rad)
{
    int n = 0;
    for (int y = int(cy - rad) - 1; y <= int(cy + rad) + 1; ++y)
        for (int x = int(cx - rad) - 1; x <= int(cx + rad) + 1; ++x) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const std::uint8_t* p = b.at(x, y);
            n += dx * dx + dy * dy <= rad * rad && p[0] > 200 && p[1] < 80 &&
                 p[2] < 80;
        }
    return n;
}

int j5Horisont(Stående s, bool låda)
{
    Rit3 r;
    if (!r.öppna())
        return -1;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(glaze::Camera3D::lookFrom({0, -20, 0.05}, 0, 0));
    sättMark(y, Mark::Ingen);   // markdjupet utanför — bara lådan skymmer
    const int L = y.addLayer("horisonten");
    y.setLayerPlacement(L, glaze::LayerPlacement::World);
    if (låda)
        y.addShape(L, glaze::Shape::box({{-5, -11, -3}, {5, -9, 3}},
                                        {.color = kVit, .lit = false}));
    läggStående(y, L, s);
    const Bild b = r.bild();
    if (g_ritfel)
        return -1;
    const std::optional<glaze::Point> p = y.project({0, 0, 0});
    return p ? rödaKring(b, r.x(p->x), r.y(p->y), 14) : -1;
}

glaze::Camera3D j5Kamera()
{
    return glaze::Camera3D{.target = {0, 0, 0}, .distance = 20, .elevation = 6};
}

int j5Nära(bool skiva)
{
    Rit3 r;
    if (!r.öppna())
        return -1;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(j5Kamera());
    sättMark(y, Mark::Ingen);
    const double rv = j5Radie(y);
    const int    L  = y.addLayer("nära");
    y.setLayerPlacement(L, glaze::LayerPlacement::World);
    if (skiva)
        y.addShape(L, glaze::Shape::box({{-2, -6 * rv - 0.05, -2}, {2, -6 * rv, 2}},
                                        {.color = kVit, .lit = false}));
    y.addShape(L, glaze::Shape::points3({{0, 0, 0}},
                                        {.size = glaze::px(10.0f), .color = kRöd}));
    const Bild b = r.bild();
    if (g_ritfel || rv <= 0.0)
        return -1;
    const std::optional<glaze::Point> p = y.project({0, 0, 0});
    return p ? rödaKring(b, r.x(p->x), r.y(p->y), 14) : -1;
}

int j5Moln(bool vita)
{
    Rit3 r;
    if (!r.öppna())
        return -1;
    glaze::Canvas& y = r.f.yta;
    y.setCamera(j5Kamera());
    sättMark(y, Mark::Ingen);
    const double rv = j5Radie(y);
    const int    L  = y.addLayer("molnet");
    y.setLayerPlacement(L, glaze::LayerPlacement::World);
    std::vector<glaze::Point3> främre, bakre;
    for (int i = -1; i <= 1; ++i)
        for (int k = -1; k <= 1; ++k) {
            främre.push_back({3 * i * rv, 0.0, 3 * k * rv});
            // Längre bort och HÖGRE: den nedre halvan bakom den vitas övre.
            bakre.push_back({3 * i * rv, 3 * rv, (3 * k + 0.7) * rv});
        }
    if (vita)
        y.addShape(L, glaze::Shape::points3(främre, {.size  = glaze::px(10.0f),
                                                     .color = kVit}));
    y.addShape(L, glaze::Shape::points3(bakre, {.size = glaze::px(10.0f),
                                                .color = kRöd}));
    const Bild b = r.bild();
    if (g_ritfel || rv <= 0.0)
        return -1;
    int n = 0;
    for (const glaze::Point3& q : främre) {
        const std::optional<glaze::Point> p = y.project(q);
        if (!p)
            return -1;
        n += rödaICirkel(b, r.box.x + p->x * r.skala, r.box.y + p->y * r.skala,
                         5.0 * r.skala - 1.5);
    }
    return n;
}

void fallJ5()
{
    for (const Stående s : {Stående::Punkt, Stående::Linje3, Stående::Markör3,
                            Stående::Markör2D, Stående::Linje2D}) {
        const int bakom = j5Horisont(s, true);
        const int fri   = j5Horisont(s, false);
        if (g_ritfel)
            return;
        check(fri >= 30, std::string("J5k ") + ståendeNamn(s) +
                             " strax under ögats höjd syns utan lådan",
              std::to_string(fri) + " röda px");
        check(bakom == 0,
              std::string("J5 ") + ståendeNamn(s) +
                  " strax under ögats höjd skyms av lådan framför — ingen linje "
                  "vid horisonten",
              std::to_string(bakom) + " röda px");
    }
    const int nära = j5Nära(true), näraFri = j5Nära(false);
    const int moln = j5Moln(true), molnFri = j5Moln(false);
    if (g_ritfel)
        return;
    check(näraFri >= 30, "J5k punkten syns utan skivan",
          std::to_string(näraFri) + " röda px");
    check(nära == 0,
          "J5n en skiva sex radier framför punkten skymmer den — taket på fyra "
          "radier i världen",
          std::to_string(nära) + " röda px");
    check(molnFri >= 30, "J5k det röda molnet syns utan de vita",
          std::to_string(molnFri) + " röda px");
    check(moln == 0,
          "J5t ett tätt moln: inga röda punkter tre radier bakom syns inne i de "
          "vita",
          std::to_string(moln) + " röda px");
}

// ============================================================ DEL L
//
// INGET BAKOM ÖGAT RITAS: linor som
// går under och bakom kameran får inte synas som en SOLFJÄDER genom flyktpunkten.
// Ett segment med BÅDA ändarna bakom närplanet får inte lämnas orört av trimningen
// (den flyttar bara en ände när den andra ligger innanför), och förlängningen
// räknar då med w < 0: dess tak `tMax` blir negativt, vinner över noll och
// skjuter hörnet längs segmentet förbi ögat — framför det. Linjeprogrammen
// kastar därför ett segment med båda ändarna utanför samma plan, och en rund
// skarv eller ände bakom närplanet.
//   L1  scenen: rutnätet ±100 m var tionde meter och sex kedjelinjer
//       om 30 segment genom en stolpe (två spann med 20° brytvinkel), ögat
//       INNE i scenen — i ett World- och i ett Ground-lager, vid fyra kameror
//       där solfjädern annars syns (D3D11 och GL): INGA röda px
//       utanför linornas projektion, och linorna framför ögat syns. Förväntat värde är
//       grindens eget: tätt stickprov längs varje segment genom `project`
//       (null bakom ögat), ±5 px kring varje punkt.
// INTE MÄTT i del L: Metal; kameror där en lina passerar precis
// under ögat (stickprovet glesnar i projektionen och det förväntade värdet blir för smalt);
// segment bortom det bortre planet (samma regel, inget symptom).

std::vector<std::vector<glaze::Point3>> linorGenomStolpen()
{
    std::vector<std::vector<glaze::Point3>> ut;
    const double a = 20.0 * std::numbers::pi / 180.0;
    for (const double off : {-2.0, 0.0, 2.0}) {
        std::vector<glaze::Point3> v, h;
        for (int i = 0; i <= 30; ++i) {
            const double t = i / 30.0;
            v.push_back({-85.0 * (1 - t), off, 9 + 2 * t - 10 * t * (1 - t)});
            h.push_back({85 * t * std::cos(a) - off * std::sin(a),
                         85 * t * std::sin(a) + off * std::cos(a), 11 - 2 * t - 10 * t * (1 - t)});
        }
        ut.push_back(v);
        ut.push_back(h);
    }
    return ut;
}

struct LinFacit {
    int fel  = 0;   // röda px utanför linornas projektion
    int alla = 0;   // röda px i ytan
};

LinFacit mätLinor(Rit3& r, const std::vector<std::vector<glaze::Point3>>& linor, const Bild& b)
{
    glaze::Canvas&            y = r.f.yta;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(b.w) * b.h, 0);
    for (const auto& l : linor)
        for (std::size_t i = 0; i + 1 < l.size(); ++i)
            for (int s = 0; s <= 1000; ++s) {
                const double t = s / 1000.0;
                const auto   q = y.project({l[i].x + (l[i + 1].x - l[i].x) * t,
                                            l[i].y + (l[i + 1].y - l[i].y) * t,
                                            l[i].z + (l[i + 1].z - l[i].z) * t});
                if (!q)
                    continue;
                const double px = r.box.x + q->x * r.skala, py = r.box.y + q->y * r.skala;
                if (px < -10 || py < -10 || px > b.w + 10 || py > b.h + 10)
                    continue;
                for (int dy = -5; dy <= 5; ++dy)
                    for (int dx = -5; dx <= 5; ++dx) {
                        const int X = static_cast<int>(px) + dx, Y = static_cast<int>(py) + dy;
                        if (X >= 0 && Y >= 0 && X < b.w && Y < b.h)
                            mask[static_cast<std::size_t>(Y) * b.w + X] = 1;
                    }
            }
    LinFacit ut;
    for (int Y = static_cast<int>(r.box.y); Y < static_cast<int>(r.box.y + r.box.h); ++Y)
        for (int X = static_cast<int>(r.box.x); X < static_cast<int>(r.box.x + r.box.w); ++X) {
            const std::uint8_t* p = b.at(X, Y);
            if (p[0] > 200 && p[1] < 80 && p[2] < 80) {
                ++ut.alla;
                ut.fel += !mask[static_cast<std::size_t>(Y) * b.w + X];
            }
        }
    return ut;
}

void fallL1()
{
    const auto linor = linorGenomStolpen();
    for (const bool ground : {false, true}) {
        Rit3 r;
        if (!r.öppna())
            return;
        glaze::Canvas& y = r.f.yta;
        const glaze::Stroke grått{.paint = gfx::Color{128, 128, 128, 90}, .width = glaze::px(1.0f)};
        for (int k = -10; k <= 10; ++k) {
            y.addShape(0, glaze::Shape::line3({-100, k * 10.0, 0}, {100, k * 10.0, 0}, grått));
            y.addShape(0, glaze::Shape::line3({k * 10.0, -100, 0}, {k * 10.0, 100, 0}, grått));
        }
        const int L = y.addLayer("linor");
        y.setLayerPlacement(L, ground ? glaze::LayerPlacement::Ground
                                      : glaze::LayerPlacement::World);
        for (const auto& l : linor)
            y.addShape(L, glaze::Shape::polyline3(l, {.paint = kRöd, .width = glaze::px(2.0f)}));
        struct Kamera { float az, el; double avstånd; };
        for (const Kamera k : {Kamera{10, 8, 8}, Kamera{60, 3, 22}, Kamera{60, 8, 14},
                               Kamera{70, 8, 8}}) {
            y.setCamera(glaze::Camera3D{.target = {0, 0, 6}, .distance = k.avstånd,
                                        .azimuth = k.az, .elevation = k.el});
            const Bild b = r.bild();
            if (g_ritfel)
                return;
            const LinFacit f = mätLinor(r, linor, b);
            char kam[96];
            std::snprintf(kam, sizeof kam, "azimut %.0f°, elevation %.0f°, avstånd %.0f",
                          k.az, k.el, k.avstånd);
            check(f.alla > 100 && f.fel == 0,
                  std::string("L1 linor genom ögats plan i ett ") + (ground ? "Ground" : "World") +
                      "-lager, " + kam + ": inget bakom ögat ritas (ingen solfjäder)",
                  std::to_string(f.fel) + " röda px utanför linorna av " + std::to_string(f.alla));
        }
    }
}

// ================================================================= DEL KL
//
// MÅLARENS KLIPP I 3D-MÅLET (`Painter::save(Rect)`): rakt ovanifrån
// på en röd mark (z = 0, ett World-lager) klipper rektangeln x −2–2, y −1–1
// i världen. Förväntat värde är ritytans `toLocal` för rektangelns hörn (planet z = 0)
// — 4 logiska px innanför kanten ritas, 4 px utanför inte.
//   KL1 Line3 och Glyph3 (en linje och en markör i rymden, på marken) klipps
//       av klippets kanter: inne sin färg, ute marken.
//   KL2 DJUPET: med Less och skrivning skriver det bortklippta INTE djup — en
//       blå Line3 på z = 1 i klippet och efter läget en grön på z = 0,5 längs
//       samma väg: inne vinner den blå (närmare), ute syns den gröna. Hade
//       klippet bara nollat alfan hade den osynliga blå skymt den gröna.
//   KL3 FÖREMÅLEN (en mesh ur `drawTriangles3`) klipps till klippets
//       omskrivna rektangel — här, axelparallellt, klippet själv.
// Sabotage som SKA fälla dem: sceneClip bort ur
// Line- eller Glyph-skuggaren (KL1 för den varianten), `discard` bort ur
// sceneClip — bara alfan nollad (KL2: ute röd i stället för grön), scissorn
// inte pålagd i `clipTo` (KL3: meshen utanför). MÄTER INTE den vridna eller
// tiltade ramen i 3D-målet (scene_pixel_gate K2/K4 gör det i 2D-vägen), och
// inte att föremålen under ett VRIDET klipp når klippets omskrivna rutas hörn
// (det är den dokumenterade gränsen, inget löfte).

// En kant i de lokala px: dess punkt `p` och normalen INÅT (mot `inne`).
struct KlippKant {
    glaze::Point p, n;
};
KlippKant kantVid(glaze::Point a, glaze::Point b, glaze::Point p,
                  glaze::Point inne)
{
    const double l = std::hypot(b.x - a.x, b.y - a.y);
    glaze::Point n{-(b.y - a.y) / l, (b.x - a.x) / l};
    if (n.x * (inne.x - p.x) + n.y * (inne.y - p.y) < 0.0)
        n = {-n.x, -n.y};
    return {p, n};
}

// Pixeln d logiska px in (d < 0: ut) från kanten.
const std::uint8_t* vidKant(const Rit3& r, const Bild& b, const KlippKant& k,
                            double d)
{
    return b.at(r.x(k.p.x + k.n.x * d), r.y(k.p.y + k.n.y * d));
}

// Ritytan rakt ovanifrån med den röda marken — och klippets hörn och de två
// lodräta kanternas punkter i de lokala px (x = −2 och x = 2 vid världens y =
// `vidY`). false när något hörn saknar en bild.
struct KlippBänk {
    Rit3         r;
    glaze::Point mitt;
    KlippKant    vä, hö;

    bool öppna(double vidY)
    {
        if (!r.öppna())
            return false;
        glaze::Canvas& y = r.f.yta;
        y.setCamera(glaze::Camera3D{.target = {0, 0, 0}, .distance = 10,
                                    .elevation = 90, .nearPlane = 1,
                                    .farPlane = 100});
        y.setLayerPlacement(0, glaze::LayerPlacement::World);
        y.addShape(0, glaze::Shape::rect({-100, -100, 200, 200}, kRöd));
        r.bild();   // kameran och ytan kända för toLocal
        if (g_ritfel)
            return false;
        const auto l = [&](double x, double yy) { return y.toLocal(glaze::Point{x, yy}); };
        const auto m = l(0, vidY), a = l(-2, -1), b = l(-2, 1), c = l(2, -1),
                   d = l(2, 1), pv = l(-2, vidY), ph = l(2, vidY);
        if (!m || !a || !b || !c || !d || !pv || !ph) {
            check(false, "KL klippets hörn har en bild på ytan");
            return false;
        }
        mitt = *m;
        vä   = kantVid(*a, *b, *pv, mitt);
        hö   = kantVid(*c, *d, *ph, mitt);
        return true;
    }
};

void fallKL1()
{
    KlippBänk k;
    if (!k.öppna(0.0))
        return;
    glaze::Canvas& y = k.r.f.yta;
    y.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        auto            s = p.save(glaze::Rect{-2, -1, 4, 2});
        p.drawLine3({-6, 0, 0}, {6, 0, 0},
                    glaze::Stroke{.paint = kBlå, .width = glaze::px(16.0f)});
    };
    Bild b = k.r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(vidKant(k.r, b, k.vä, 4), kBlå, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, 4), kBlå, 8) &&
              ärFärg(vidKant(k.r, b, k.vä, -4), kRöd, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, -4), kRöd, 8),
          "KL1 målarens Line3 klipps: blå innanför kanterna, marken utanför",
          px(vidKant(k.r, b, k.vä, 4)) + " " + px(vidKant(k.r, b, k.hö, 4)) +
              " | " + px(vidKant(k.r, b, k.vä, -4)) + " " +
              px(vidKant(k.r, b, k.hö, -4)));
    // Markören i rymden (Glyph3), 60 px, mitt på den högra kanten.
    y.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        auto            s = p.save(glaze::Rect{-2, -1, 4, 2});
        p.drawMarker3({2, 0, 0},
                      glaze::MarkerStyle{.size = glaze::px(60.0f), .fill = kGrön});
    };
    b = k.r.bild();
    if (g_ritfel)
        return;
    check(ärFärg(vidKant(k.r, b, k.hö, 4), kGrön, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, 20), kGrön, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, -4), kRöd, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, -20), kRöd, 8),
          "KL1 målarens Glyph3 klipps: den halva markören innanför kanten",
          px(vidKant(k.r, b, k.hö, 4)) + " " + px(vidKant(k.r, b, k.hö, 20)) +
              " | " + px(vidKant(k.r, b, k.hö, -4)) + " " +
              px(vidKant(k.r, b, k.hö, -20)));
}

void fallKL2()
{
    KlippBänk k;
    if (!k.öppna(0.0))
        return;
    k.r.f.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        p.setRenderState(glaze::RenderState{});   // Less med skrivning
        {
            auto s = p.save(glaze::Rect{-2, -1, 4, 2});
            p.drawLine3({-6, 0, 1}, {6, 0, 1},
                        glaze::Stroke{.paint = kBlå, .width = glaze::px(24.0f)});
        }
        p.drawLine3({-6, 0, 0.5}, {6, 0, 0.5},
                    glaze::Stroke{.paint = kGrön, .width = glaze::px(16.0f)});
    };
    const Bild b = k.r.bild();
    if (g_ritfel)
        return;
    const std::uint8_t* inne = b.at(k.r.x(k.mitt.x), k.r.y(k.mitt.y));
    check(ärFärg(inne, kBlå, 8) && ärFärg(vidKant(k.r, b, k.vä, -4), kGrön, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, -4), kGrön, 8),
          "KL2 det bortklippta skriver inget djup: inne den närmare blå, ute "
          "den gröna bakom den",
          px(inne) + " | " + px(vidKant(k.r, b, k.vä, -4)) + " " +
              px(vidKant(k.r, b, k.hö, -4)));
}

void fallKL3()
{
    KlippBänk k;
    if (!k.öppna(0.0))
        return;
    const glaze::MeshStyle           blå{.color = kBlå, .lit = false};
    const std::vector<glaze::Point3> ruta = {{-3, -3, 0.5}, {3, -3, 0.5}, {3, 3, 0.5},
                                             {-3, -3, 0.5}, {3, 3, 0.5},  {-3, 3, 0.5}};
    k.r.f.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        auto s = e.painter.save(glaze::Rect{-2, -1, 4, 2});
        e.painter.drawTriangles3(ruta, blå);
    };
    const Bild b = k.r.bild();
    if (g_ritfel)
        return;
    const std::uint8_t* inne = b.at(k.r.x(k.mitt.x), k.r.y(k.mitt.y));
    check(ärFärg(inne, kBlå, 8) && ärFärg(vidKant(k.r, b, k.vä, 4), kBlå, 8) &&
              ärFärg(vidKant(k.r, b, k.vä, -4), kRöd, 8) &&
              ärFärg(vidKant(k.r, b, k.hö, -4), kRöd, 8),
          "KL3 målarens mesh klipps till klippets rektangel (scissorn)",
          px(inne) + " " + px(vidKant(k.r, b, k.vä, 4)) + " | " +
              px(vidKant(k.r, b, k.vä, -4)) + " " + px(vidKant(k.r, b, k.hö, -4)));
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== scene3d_pixel_gate — djup, MSAA och renderläget i pixlar "
                "===\n");

    Fönster f;
    f.setOffscreen(true);
    f.show();
    if (!f.isOpen()) {
        std::printf("  FEL  fönstret öppnades inte — ingen headless-yta?\n");
        std::printf("=== scene3d_pixel_gate: FALLERAR ===\n");
        return 1;   // ALDRIG skip — se filhuvudet
    }
    frame(f, nullptr);   // första framen — fönstret ritat en gång

    if (!g_ritfel) fallA1(f);
    if (!g_ritfel) fallA2(f);
    if (!g_ritfel) fallA3(f);
    if (!g_ritfel) fallA4(f);
    if (!g_ritfel) fallA5(f);
    if (!g_ritfel) fallA6(f);
    if (!g_ritfel) fallA7(f);
    if (!g_ritfel) fallA8(f);
    if (!g_ritfel) fallA9(f);
    if (!g_ritfel) fallB1(f);
    if (!g_ritfel) fallB2(f);
    if (!g_ritfel) fallB3(f);
    if (!g_ritfel) fallB4(f);
    if (!g_ritfel) fallB5(f);
    if (!g_ritfel) fallP(f);
    if (!g_ritfel) fallK1(f);
    if (!g_ritfel) {
        const std::vector<gfx::GpuAdapter> kort = gfx::gpuAdapters();
        fallK2(f, kort);
        fallK3(kort);
        fallK4(f);
    }
    // Del C — egna program.
    if (!g_ritfel) fallC1(f);
    if (!g_ritfel) fallC1b(f);
    if (!g_ritfel) fallC2(f);
    if (!g_ritfel) fallC3(f);
    if (!g_ritfel) fallC4(f);
    if (!g_ritfel) fallC5(f);
    if (!g_ritfel) fallC6(f);
    if (!g_ritfel) fallC7(f);
    if (!g_ritfel) fallC8(f);
    if (!g_ritfel) fallC9(f);
    if (!g_ritfel) fallC10(f);
    if (!g_ritfel) fallC11(f);
    if (!g_ritfel) fallC12(f);
    // Del D — Line3 och Glyph3.
    if (!g_ritfel) fallD1(f);
    if (!g_ritfel) fallD2(f);
    if (!g_ritfel) fallD3(f);
    if (!g_ritfel) fallD4(f);
    if (!g_ritfel) fallD5(f);
    if (!g_ritfel) fallD6(f);
    if (!g_ritfel) fallD6Rityta();
    if (!g_ritfel) fallD7(f);
    // Del G — ritytans 3D-mål, egna fönster.
    if (!g_ritfel) fallG1();
    if (!g_ritfel) fallG2();
    if (!g_ritfel) fallG3();
    if (!g_ritfel) fallG4();
    if (!g_ritfel) fallG5();
    if (!g_ritfel) fallG6();
    // Del E och F — föremålen genom ritytan, egna fönster.
    if (!g_ritfel) fallE1();
    if (!g_ritfel) fallE2();
    if (!g_ritfel) fallE3();
    if (!g_ritfel) fallE4();
    if (!g_ritfel) fallE5();
    if (!g_ritfel) fallE6();
    if (!g_ritfel) fallE7();
    if (!g_ritfel) fallE8();
    if (!g_ritfel) fallE9();
    if (!g_ritfel) fallE10();
    if (!g_ritfel) fallF12();
    if (!g_ritfel) fallF345();
    if (!g_ritfel) fallF6();
    // Del I — del-vyerna, bilden och målarens renderläge.
    if (!g_ritfel) fallI1();
    if (!g_ritfel) fallI1b();
    if (!g_ritfel) fallI2();
    if (!g_ritfel) fallI4();
    if (!g_ritfel) fallI6();
    if (!g_ritfel) fallI7();
    if (!g_ritfel) fallI9();
    // Del C2 — egna program och helskärmspass på ritytan.
    if (!g_ritfel) fallC2a();
    if (!g_ritfel) fallC2b();
    if (!g_ritfel) fallC2c();
    if (!g_ritfel) fallC2d();
    // Del H — terräng, drapering och text i marken.
    if (!g_ritfel) fallH1();
    if (!g_ritfel) fallH2();
    if (!g_ritfel) fallH2b();
    if (!g_ritfel) fallH4();
    if (!g_ritfel) fallH5();
    if (!g_ritfel) fallH5b();
    if (!g_ritfel) fallH5c();
    if (!g_ritfel) fallH6();
    if (!g_ritfel) fallH6b();
    if (!g_ritfel) fallH6c();
    if (!g_ritfel) fallH7();
    if (!g_ritfel) fallH8();

    // Del J — markdjupet (target3d.h).
    if (!g_ritfel) fallJ1();
    if (!g_ritfel) fallJ2();
    if (!g_ritfel) fallJ3();
    if (!g_ritfel) fallJ4();
    if (!g_ritfel) fallJ5();

    // Del L — inget bakom ögat ritas.
    if (!g_ritfel) fallL1();

    // Del KL — målarens klipp i 3D-målet.
    if (!g_ritfel) fallKL1();
    if (!g_ritfel) fallKL2();
    if (!g_ritfel) fallKL3();

    std::printf(g_fel ? "=== scene3d_pixel_gate: %d FEL av %d ===\n"
                      : "=== scene3d_pixel_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
