// scene_pixel_gate — SCENENS PIPELINE, mätt i PIXLAR genom den RIKTIGA
// backenden (del A–F).
//
// VÄGEN är glaze-renders och clip_gates: ett DOLT fönster (`setOffscreen`)
// → `renderFrame` → `captureFrame`, som läser retained-ytan FÖRE compositen.
// En testwidget ritar en vit yta och anropar `drawMesh` direkt mot
// renderaren med buffertar ur `buildPolyline` — det ritytans widget
// gör bakom sin Painter. Röd linje på vitt ger täckningen
// rakt ur grönkanalen: G = 255 · (1 − täckning).
//
// VAD GRINDEN PÅSTÅR:
//   A1  KONSTANT SKÄRMBREDD: en 6 px bred linje är 6 px (täckningens summa
//       tvärs linjen) vid zoom ×1 och ×8. I VÄRLDSLÄGET växer den med
//       zoomen: 6 världsenheter är 6 px vid ×1 och 48 px vid ×8.
//   A2  BÄRING 30°: linjen ligger 30° moturs och är fortfarande 6 px bred
//       vinkelrätt mot sig själv (6 / cos 30° lodrätt).
//   A3  PITCH 45°: en linje som går rakt in i bilden — och bakom kameran —
//       är 6 px bred både nära, i mitten och långt bort. I VÄRLDSLÄGET
//       smalnar den i stället mot horisonten.
//   A4  AA-KANTEN: det finns delvis täckta pixlar på båda sidor, högst två
//       per sida, och en hel kärna.
//   A5  KLIPPET: scissorn skär linjen, och en RUNDAD mask tar hörnet men
//       inte kantens mitt (kontroll med radie 0 först). Det är Line-
//       programmets EGEN clipCoverage som mäts.
//   A6  SWEREF-PRECISIONEN: en linje på x = 674000,37 vid 100 px/m och på
//       x = 674000,137 vid 1000 px/m hamnar på rätt kolumn ±1, och samma i
//       y — RTC hela vägen genom GPU:n.
//   A7  INGEN UPPLADDNING VID KAMERAÄNDRING: tio ändringar av mitt, zoom,
//       bäring och tilt ger `bytesUploaded == 0` och `buffersCreated == 0`,
//       medan linjen ritas varje gång och till slut ligger där `project`
//       säger. (Räknarna stegas av backenden vid själva uppladdningarna —
//       grinden litar på att de sitter där.)
//   A8  TRANSIENT RING OCH UPPDATERING: en linje ur `uploadTransient` ritas
//       och räknas exakt som sina byte; `updateBuffer` byter färg på en
//       statisk linje till nästa frame — och mitt i en frame behåller det
//       som redan ritats det gamla innehållet, och uppdateringen bryter
//       passet EN gång på Metal och aldrig på D3D11/GL
//       (`RenderStats::uploadBreaks`).
//   A9  (uppladdningarnas vägar) en NY buffert —
//       `createBuffer(nullptr)` + `updateBuffer` + rita, allt i framen: linjen
//       syns och inget pass bryts (Metal: direkt).
//   A10 ritad i FÖRRA framen (rött) och uppdaterad (grönt) FÖRE ritningen:
//       linjen är grön och inget pass bryts (Metal: uppladdningspasset).
//   A11 fyra frames i rad med en sådan uppdatering var, i fyra färger: varje
//       bild har sin färg, och inget pass bryts.
//   MM  MILLIMETERN hela vägen: en injicerad kalibrering (4 px/mm) för
//       fönstrets skärm når `Ctx::pxPerMm`, och en 2 mm bred linje med
//       `pxPerWidthUnit = pxPerMm` är 8 px. Kräver ett skärm-id — saknas
//       det FALLER fallet med orsaken (ingen tyst skip).
//
// DEL B — BILDERNA. Referensen är bildens egna texlar och kamerans
// `project`, aldrig backendens formel.
//   B1  PERSPEKTIVKORREKT: en platta 1 × 2 texlar (Nearest) under pitch 45°
//       — gränsen v = 0,5 ligger på världens y = 0 (`project`), inte mitt
//       emellan kanterna som en affin interpolering lägger den (fallet
//       kräver att de två skiljer > 10 px).
//   B2  FILTRET som BATCH-TILLSTÅND: samma bild två gånger i en frame,
//       Nearest och därefter Linear — hård gräns resp. ramp vid förstoring, i
//       BÅDA ritvägarna; och LÄCKAN åt båda håll (scenens Linear före UI:ts
//       Nearest och tvärtom) — på GL skuggar ett bundet samplerobjekt
//       texturens parametrar.
//   B3  createImage/updateImage: delrektet ur en buffert med luft i raderna,
//       avvisningarna, nollställd utan pixlar, och MITT I EN FRAME behåller
//       det som redan ritats det gamla innehållet — och uppdateringen bryter
//       passet EN gång på Metal, aldrig på D3D11/GL.
//   B12 BILDERNAS VÄGAR: (a) en NY bild uppdaterad i framen
//       före ritningen, (b) ritad i förra framen och uppdaterad före
//       ritningen, (c) två ÖVERLAPPANDE uppdateringar av samma bild i en
//       frame (hela blå, därefter 4 × 4 grönt) — den nya färgen, grönt på blått
//       pixel för pixel, och inget pass bryts.
//   B13 RITYTANS BILDLAGER BYGGT OM: 40 bilder 8 × 8 som `Shape::image`, sedan
//       `setShapes` med 40 NYA — framen efter bryter inget pass, skapar 80
//       buffertar (två per chunk, en chunk per bild) och visar varje bild i
//       sin nya färg.
//   B4  R8 ritas som gråskala, täckande, i båda vägarna.
//   B5  FÖNSTRETS BILDCACHE (`ImageCache`, intern): budget och LRU, det
//       synliga vräks inte, döda bilder släpps, taket på 16 MiB per frame,
//       ändringsloggen för två fönster i olika takt, och FÖNSTRETS väg — en
//       bild som väntat ritas i nästa frame och därefter ritas ingenting mer.
//   B6  Image-programmets egen KLIPPMASK, rakt uppifrån och under tilt.
//   B7  OPACITETEN i Image-programmet och i drawImage.
//   B8  BILDEN PÅ FYRA FRIA HÖRN (genom en riktig
//       glaze::Canvas): en Bitmap med ett lodrätt och ett vågrätt streck på
//       2 texlar vid u = 0,5 och v = 0,5 på en trapets rakt uppifrån —
//       strecken där GRINDENS homografi (DLT) och `toLocal` lägger dem, ±1 px
//       på nio punkter var (en affin uv per triangel avviker tiotals px vid
//       diagonalen).
//   B9  samma på en allmän fyrhörning under bäring 30° och tilt 45°, ±1,5 px.
//   B10 `image(Rect)` och `image` med rektangelns fyra hörn ger identiska
//       pixlar (under tilt).
//   B11 en RenderTexture (premultiplicerad, på GL speglad) och en Bitmap på
//       fyrhörningen står rättvända: rött vid hörnet ÖV, blått vid NH.
//   B14 FÄRGJUSTERINGEN i Image-programmet (`SceneUniforms::
//       saturation`/`contrast`/`brightness`): förvalen ger bildens exakta
//       texel (±0), mättnaden 0 luminansens grå, kontrasten 0 mittgrått,
//       ljusheten 0 svart och 0,5 halva färgen, alla tre i ordningen
//       ljushet → kontrast → mättnad, och fasadens normering (ett tal som
//       inte är ändligt är 1, ett negativt 0). Referens: `justerad`, definitionen
//       räknad här.
//   B15 samma genom RITYTAN (`ImageStyle`): samma Bitmap i två stilar i ett
//       lager ritas var för sig (körningens nyckel), en ny stil med
//       `setShape` syns i nästa ritning utan att bilden laddas upp igen, och
//       målarens `drawImage` tar stilen.
//       Sabotage som SKA fälla dem (skrivna): stilen
//       borta ur `RunKey::operator==` (B15 — den högra blir grå),
//       `imageUniforms` inte anropad i `drawChunks` (B15) eller i målaren
//       (B15 målaren), `sceneImageAdjust` med mättnaden och ljusheten
//       förväxlade (B14), kontrasten före ljusheten i `glaze_colorAdjust`
//       (B14 alla tre), `adjustFactor` utan NaN-grenen (B14 normeringen) och
//       justeringen FÖRE av-premultipliceringen (fångas INTE — se nedan).
//
// DEL C — YTORNA OCH GRADIENTERNA. Referensen är ytans egna mått,
// stoppens färger och kamerans `project`, aldrig programmets formel.
//   C1  HÅLET: en kvadrat med ett hål visar bakgrunden i hålet (mitten och
//       nära hålets hörn), täckning i ringen och bakgrund utanför.
//   C2  KANTREMSAN ÄR EN PX: en yta W px bred täcker W + 1 px längs en rad
//       vid zoom ×1 och ×16 med kanterna på bråkdelar av en px, inne helt
//       täckt och ingenting mer än 1 px utanför kanten; och under tilt 45°
//       med y uppåt — en SPEGLAD kamera, där "utåt" måste vändas — är
//       kolumnens summa avståndet mellan de projicerade kanterna + 1.
//   C3  GRADIENTERNA: linjär med pad före starten och efter slutet och
//       mitten mitt emellan; radiell med startfärgen i mitten och slutfärgen
//       bortom r1; fast i världen under tilt 40° och bäring 25°; SAMMA BLOCK
//       i Line-programmet, med anropets opacitet; rampen samplas LINJÄRT i
//       båda programmen också när bildens eget filter är Nearest; och en
//       gradient utan ramp avvisas.
//   C4  Fill-programmets egen RUNDADE MASK (kontroll med radie 0 först).
//
// DEL D — TEXTEN OCH MARKÖRERNA, med backendens RIKTIGA UI-font i
// SDF-atlasen. Referensen är geometrisk (bläckets låda före och efter en
// kameraändring, en form med känd area) och relativ (fet mot vanlig).
//   D1  Lådans bredd = `Font::measure` i samma storlek; texten står UPPRÄTT
//       under bäring 30° (samma låda och mitt) och under tilt 45° (samma
//       mått).
//   D2  MAP-ROTATIONEN följer bäringen: vid 90° står raden lodrätt, vid 30°
//       är lådan den vridna radens.
//   D3  GLORIAN: röda px bara med den, och den ligger UNDER fyllningen
//       (lika många svarta px som utan).
//   D4  STORLEKEN I MM: 6 mm vid en injicerad kalibrering på 4 px/mm ger
//       samma låda som 24 px.
//   D5  FET ≠ VANLIG: plattformens UI-familj har en riktig fet fil
//       (`style()` säger Bold) som ger > 15 % mer bläck, och den syntetiska
//       feta (flaggan på den vanliga fontens quadar) > 8 %.
//   D6  MARKÖRERNA: cirkeln 20 px täcker π · 10² och kvadraten 400 (±4 %),
//       konturen ligger utanför fyllningen, plattan bakom texten, och
//       Glyph-programmets egen RUNDADE MASK.
//   D7  Atlasen samplas LINJÄRT också när sidans eget filter är Nearest
//       (≥ 30 grånivåer över en quad med två texlar).
//   D8  TEXTDELARNA: "HH" vanlig svart + "HH" fet röd
//       i EN etikett är EN chunk, rött bläck bara till höger om delgränsen
//       (den vanliga fontens `measure`) och svart bara till vänster, den feta
//       delen bredare, och lådans bredd = summan av de två fonternas measure
//       (±1 %).
//   D9  en stor gloria (8 px): den vanliga delens högra stam vid gränsen är
//       lika svart som utan gloria — den feta delens gloria ritas aldrig
//       över den vanliga delens fyllning.
//   D10 GOLV OCH TAK i Glyph-programmet: "H" i storleken
//       10 med golvet 20 px och taket 40 px vid tätheten 1, 3 och 6 har samma
//       bläckhöjd (±1 px) som "H" UTAN gränser vid em 20, 30 och 40 px.
//   D11 SYNLIGHETEN EFTER ZOOM genom en riktig rityta: två etiketter med
//       intervallet [2, ∞), med och utan kollision, saknar bläck vid zoom 1
//       och har det vid zoom 3.
//   D12 TAKET I TEXTEN: maxLines 2 på fem rader lämnar inget bläck
//       under två radhöjder.
//       Sabotage som SKA fälla dem (skrivna):
//       klampningen bort ur skuggaren (D10, också på GL), bilden släppt utan
//       kollision (D11 — den okolliderande), taket ignorerat (D12). MÄTER INTE
//       Metal förrän grinden körs där, draperingen eller skärpan.
//
// DEL R — GLYFRESERVEN, med den riktiga backendens
// UI-font. Kodpunkterna saknas i OS:ets förvalda UI-font: U+4E2D i Segoe UI
// och SF (bara Windows och macOS), U+2212 och U+2248 i Noto Sans (prövas
// överallt — Segoe UI och SF har dem själva).
//   R1  hasGlyph, ett annat raster än U+FFFD och measure = rastrets steg.
//   R2  drawText ritar annat bläck än U+FFFD och svarar med measure.
//   R3  en otilldelad kodpunkt (U+0378): hasGlyph false, ersättningens raster
//       och steg.
//   R4  ett styrtecken (tabben, U+0085): hasGlyph false.
//   R5  SDF-atlasens glyf har ett fält, och steget × 2 = atlasfontens
//       measure.
//
// DEL E — RENDER-MÅLEN.
//   E1  ORIENTERINGEN: ett "F" ritat i ett mål läses rättvänt i båda
//       ritvägarna (GL:s FBO-flipp).
//   E2  PREMULTIPLIKATIONEN: en kantutjämnad cirkel i ett genomskinligt mål
//       över vitt har ingen mörk frans (R kvar på 255 i varje delvis täckt
//       pixel).
//   E3  KLIPPET: ett yttre klipp gäller inte i målet och står kvar efter
//       popTarget.  E4  ett klipp INNE i målet räknas mot MÅLETS höjd.
//   E5  NÄSTLADE mål.  E6  målets egen bild ritas inte i målet.
//   E7  glaze::RenderTexture: lat målning, repaint, självritning och budget.
//   E8  rensfärgen lagras premultiplicerad.  E9  Image- och Line-programmen
//       I ett mål räknar mot målets storlek (fönstrets hade skalat dem).
//
// DEL F — RITYTAN, en riktig glaze::Canvas i ett dolt fönster.
//   F1  INAKTIVERAD TONAS: en röd yta över vitt ritas med 1 − iconFade
//       (rött med alfan stilens ton — referens ur stilen, inte ur programmet).
//   F2  RUTNÄTET: linjer i världens 0 och 20 och ingenting mitt emellan;
//       utzoomat glesas det ut till var 40:e när 20 hamnar under stilens
//       minsta avstånd.
//   F3  onPaint OVANPÅ lagren och följer kameran; onPaintOverlay ritas vid
//       presenten i de lokala px — i overlay-lagret, inte i innehållet.
//   F4  INGA UPPLADDNINGAR VID KAMERAÄNDRING genom widgeten: tio ändringar
//       av mitt, zoom, bäring och tilt över yta, linje, text, markörer och
//       rutnät i SWEREF med y uppåt ger 0 buffertar och ingen geometri —
//       högst etikettens synlighetstexel, 1 byte per ändring (etiketten
//       lämnar ytan längs vägen och döljs då).
//   F5  TEXTEN I MM genom ritytan: 6 mm vid 4 px/mm har samma bläck som 24
//       fysiska px.
//   F6  GLIDNINGEN (`easeTo`): kameran står still tills ritytan ritas, varje
//       frame tar ett steg UTAN en invalidate utifrån, framme är kameran
//       exakt målet och glidningen slut, och en sättare mitt i den avbryter.
//       Självinvalideringen mäts bara där fönstret ritar PARTIELLT (D3D11):
//       Linux rundade fönster ritas alltid fullt, och där mäts stegningen och
//       ankomsten men inte att ritytan själv bad om framen. Att den SLUTAR
//       be om frames när den är framme mäts inte (fönstrets `wantsFrame` är
//       privat) — bara att glidningen är slut.
//
// DEL G — ETIKETTKOLLISIONEN, en riktig glaze::Canvas. Varje
// etiketts bläck läses ur den kanal bara den andra färgen lämnar orörd.
//   G1  två krockande etiketter: BARA EN ritas, prioriteten avgör, vid lika
//       prioritet den första och det övre lagret; `setLayerCollision(false)`
//       och stilens `collide = false` låter båda synas.
//   G2  VRIDNINGEN ändrar lådorna: sida vid sida krockar två Viewport-
//       etiketter, vid bäring 90° står ankarna över varandra och båda syns;
//       med `setLabelFade` tonar den in över frames och STANNAR framme.
//   G3  en kameraändring laddar upp HÖGST en byte per etikett — aldrig
//       geometri — och skapar ingen buffert (48 etiketter, sju zoomar).
//
// DEL H — KONTUREN KOMPLETT OCH BILDMARKÖREN (MarkerStyle::img).
// Referensen är formernas geometri — hörnets kvadrat, skivan, fasens linje,
// mönstrets intervall från linjens början projicerade med `project` — och en
// pixels täckning i ett streck är dess överlapp med strecket.
//   H1  SKARVARNA i en rät vinkel, 40 px: (16, −16) täcks bara av den
//       spetsiga, (12, −12) av den spetsiga och den runda, (6, −6) av alla.
//   H2  SPETSGRÄNSEN: 30° (spetsen 3,86 × bredden) är spetsig med gränsen 4
//       och avfasad med 3; 20° (5,76) avfasad med förvalet och spetsig med 10.
//   H3  FASEN ÄR PLATT: pixlarna längs fasens linje har samma delvisa
//       täckning, 2,8 px utanför ingen, 2,8 px innanför full.
//   H4  ÄNDARNA: 200 px med raka, 220 med fyrkantiga (också 8 px från
//       mittlinjen), 220 med runda längs mittlinjen och skivans korda bredvid.
//   H5  STRECKEN {10, 5} px pixel för pixel vid zoom ×1 och ×4 (står still i
//       skärmen), förskjutna 5 px, och i världsenheter vid ×2 (20/10 px).
//   H6  STRECKENS ÄNDFORM: {0, 18} med runda ändar är prickar (π · 4²), med
//       fyrkantiga kvadrater 8 × 8.
//   H7  STRECKEN I MM genom ritytan: {2, 1} mm vid 4 px/mm är 8/4 px, också
//       efter en zoomändring.
//   H8  STRECKEN UNDER TILT 45°: varje streck där `project` lägger det —
//       längre nära, kortare bort (den skrivna begränsningen, mätt).
//   H9  BILDMARKÖREN i Glyph-programmet: röd över blå, ankaret Bottom PÅ
//       punkten, upprätt under bäring och tilt, bildens proportioner (8 × 4 ⇒
//       20 × 10 px), ett render-mål rättvänt (GL:s spegling) och rakt
//       (av-premultiplicerat).
//   H10 BILDMARKÖREN genom ritytan: en Bitmap 5 mm vid 4 px/mm är 20 px med
//       spetsen på punkten; en ogiltig bild ritar ingenting.
//
// DEL K — MÅLARENS KLIPP ( `Painter::save(Rect)`), en riktig rityta.
// Referensen är rektanglarnas geometri (målarens `rotate`, ritytans `toLocal`): 3
// logiska px innanför ritas, 3 px utanför inte.
//   K1  VARJE PROGRAM klipps av ett axelparallellt klipp: ytan (Fill), en
//       tjock linje (Line), en markör och en text (Glyph) och en bild (Image).
//   K2  VRIDET 30°: fyrhörningen — också vitt i hörnet av dess omskrivna
//       ruta, där scissorn ensam hade släppt in.
//   K3  NÄSTLAT: bara snittet av en rektangel och en 45° vriden kvadrat, och
//       efter lägena ritas utan klipp.
//   K4  I VÄRLDEN under bäring, tilt och zoom: rektangelns bild på ytan.
//       Sabotage som SKA fälla dem (skrivna): sceneClip
//       bort ur en av skuggarna (K1 för just det programmet — på GL fäller
//       uniformkontrollen programmet först), uppladdningen bort
//       (`bindSceneClip` på D3D11, `setSceneClip` på GL — klippet står kvar
//       på inget klipp: K1–K4), klippplanet utan rotationen (K2), snittet bort
//       (K3), skärmens matris i
//       världen (K4). MÄTER INTE djupet (scene3d_pixel_gate KL), föremålen i
//       rymden eller kantutjämningens profil — bara att bandet är under 3 px.
//
// INTE MÄTT här: Metal (rader i doc/os/macos/TESTS.md),
// färgjusteringen (B14/B15) på ett RENDER-MÅLS halvgenomskinliga texel — att
// den sker EFTER av-premultipliceringen syns bara där, och B14 ritar en
// ogenomskinlig bild — och färgjusteringen av en draperad bild, den runda
// skarvens och ändens form utöver kordan och arean (H4/H6), den spetsiga
// skarvens kantband runt spetsen, halvgenomskinliga linjers dubbla täckning
// i skarvarna (skriven begränsning), strecken vid extrem zoom (flyttalens
// upplösning i mönstrets fas), en MÄTT skärms täthet (MM, H7 och H10
// injicerar kalibreringen — mätningen är G4:s),
// kantutjämningens kvalitet utöver bandets bredd, bildfiltrens KVALITET
// (bara hård gräns mot ramp), Linear vid FÖRMINSKNING (inga mipmaps —
// ingen påstår något där), `decodeImage` från en annan tråd (bara att
// loadImage går genom den), text och composite under fel sampler (de ritas
// 1:1 på hela pixlar och är därför okänsliga för filtret — det som mäts är
// att varje väg binder sin egen, B2), fönsterbudgeten i flera fönster
// samtidigt (den är per fönster), SDF-textens skärpa och vikt vid små
// storlekar (under ~10 px mjukare och ljusare än den hintade UI-texten —
// ögat, inte en siffra), kursivens skjuvning i pixlar (den är geometri och
// mäts i geom_gate S3), gradientens färger mellan stoppen utöver mitten,
// remsans skarvar där två kanter möts (bara bandet längs en rad), texten
// över flera atlassidor (ordningen där mäts headless i geom_gate S5g), och i
// del G etiketternas LÅDOR på pixeln (bara vilken etikett som syns) och
// toningens kurva (bara att den är delvis synlig halvvägs och framme vid
// slutet). I del R: VILKEN reservfont OS:et väljer och glyfens riktighet
// utöver "inte ersättningstecknet", att den valda kodpunkten FAKTISKT
// saknas i primärfonten på maskinen (en ovanlig fontinstallation kan göra
// fallet grönt utan att reserven körts), CJK på Linux utan CJK-fonter,
// färgemoji och shaping. Och att fyrhörningen ser rätt ut för ögat (testas
// för hand i demon) — B8/B9 mäter strecken, inte bilden i övrigt.
//
// SABOTAGEN den fäller (prövade i en privat kopia
// på D3D11 och på GL under Wayland och X11): extrudering FÖRE projektionen
// (A1 — 8 px vid ×1 och 50 vid ×8), perspektivinterpolerad sida (A3 — 1–3
// px i stället för 6), FS utan clipCoverage (A5; på GL fäller redan
// Line-programmets uniformkontroll den rakaste formen — uniformerna
// optimeras bort, programmet byggs inte och inget ritas — och A5 den form
// som håller uniformerna levande men ignorerar masken), origo 0 (alla
// fall: linjen hamnar utanför ytan) och — på D3D11 — breddfaktorn
// ignorerad i skärmläget (MM: 2 px i stället för 8; A1 står kvar, där är
// faktorn 1).
// Del B och E (prövade i en privat kopia, D3D11 och GL under
// Wayland och X11): uv noperspective (B1: 262 mot 245 — exakt den affina
// gränsen), filtret ignorerat i UI-batchen resp. Image-programmet (B2),
// UI-flushen som inte binder sin sampler på GL (B2 läckan), updateImage
// utan flush (B3), R8 utan gråskala (B4), LRU omvänd, taket bort, ingen
// invalidate efter väntan (B5), maskens läge utan w-divisionen på GL och
// Image-programmets PS utan clipCoverage (B6), opaciteten ignorerad i
// Image-programmet (B7), GL-FLIPPEN BORT (E1, E9),
// AV-PREMULTIPLICERINGEN BORT i UI-shadern resp. Image-programmet (E2),
// popTarget utan klippet, pushTarget som behåller det, pop som binder
// fönstret i stället för det yttre målet (E3/E5), SCISSORN MOT height_ på
// GL (E4), RenderTexture som målar varje frame (E7), rak rensfärg (E8),
// målets viewport/b0 bort på D3D11, fönstrets storlek i Image- resp.
// Line-programmet och UI-programmets vp som inte följer målet på GL (E9,
// E1).
// Del C och D (prövade i en privat kopia, D3D11 och GL under
// Wayland): remsan extruderad i VÄRLDEN och remsan INÅT (C2), gradienten
// ignorerad (C3), rampen och atlasen samplade med Nearest (C3, D7),
// Map-rotationen ignorerad (D2), glorians utvidgning borta (D3), och Fill-
// och Glyph-programmens PS utan clipCoverage (C4, D6) liksom Line-
// programmets färg ignorerad (C3) — de tre sista fäller fallen själva på
// D3D11, medan uniformkontrollen fäller dem på GL redan innan (uniformerna
// optimeras bort och programmet byggs inte), så där är fallen inte prövade
// mot en form som håller uniformerna levande. Bara på D3D11: Viewport-
// vinkeln som följer kartan (D1), storlekens enhet ignorerad (D4) och den
// syntetiska feta utan tröskelförskjutning (D5).
// Del F (prövade i en privat kopia på D3D11): tonen bort (F1),
// utglesningen bort (F2), `onPaint` före lagren och HUD:en i världen (F3),
// synken som laddar upp varje ritning och rutnätets mesh byggd varje ritning
// (F4), mm-enheten bort (F5), och glidningen utan självinvalidering eller
// utan slut (F6). Residensen EFTER gallringen fäller inte F4 — innehållet
// ligger nära kameran; painter_gate R gör det.
// Del H (prövade i en privat kopia, D3D11 — och de med * också på
// GL under Wayland eller X11): spetsgränsen ignorerad* (H2), fasen bort (H3 —
// H1 fångar den inte, quaden klipper där), den fyrkantiga förlängningen
// bort* (H4), mönstrets skala ignorerad* (H5, H7), mm som px i lagret (H7),
// `dist` nollad (H5, H6), runda streckändar som raka (H6), förlängningen
// bara i skärmen med ett felaktigt w* (H8),
// bildmarkörens proportioner* och av-premultiplicering* bort
// (H9), GL-speglingen bort (H9, bara GL), markörbilden obunden i lagret
// (H10) och ankaret ignorerat i byggaren (H9, H10).
// För B8–B11, D8–D9 och R (skrivna) SKA sabotagen nedan fälla dem:
// q ignorerat i shadern
// (B8, B9), speglingen efter multiplikationen med q (B11, bara GL — bara där
// är ett måls bild speglad), fel offset för q i input layouten (B8), GL:s
// attribut 2 avslaget (B8 på GL); atlas per stil (D8 två chunkar, D9) och
// delens färg ignorerad (D8); reserven bort ur `resolveGlyph` (R1),
// `rasterize` på primärens face med reservens index (R1:s mätning),
// `getGlyph` förbi `resolve` (R2) och `advanceOf` förbi `resolve` (R1).
// För A8–A11, B3 och B12–B13 (skrivna) SKA sabotagen fälla dem på Metal:
// Direct borttagen så att allt
// aldrig ritat bryter (A9, B12a, B13 → brott > 0), uppladdningspasset
// skickat EFTER framens commandbuffer (A10, B12b → den gamla färgen), Pass
// skickad till Break (A10 → ett brott), Break skickad till Pass (A8, B3 —
// anropet FÖRE uppdateringen ritar det nya) och ingen nollning av
// stagingringen per slot (A11 — fångas bara om förskjutningen landar i en
// region som skrivs om). MÄTER INTE en direkt skrivning i en buffert GPU:n
// fortfarande läser — `captureFrame` väntar in GPU:n, så kapplöpningen
// uppstår aldrig här; B12c mäter ordningen som den blir på maskinen, inte
// regeln "ett nytt blit-pass vid överlapp"; och på D3D11 och GL mäter
// brottfallen ingenting (alltid 0) — pixelfallen prövar där
// `UpdateSubresource`/`glBufferSubData` mitt i en frame.
//
// INGEN TYST SKIP (som clip_gate): utan headless-yta FALLER grinden.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gfx/camera.h>
#include <gfx/geom2d.h>
#include <gfx/sdf.h>
#include <glaze/glaze.h>

#include "image_cache.h"   // del B:s budget (intern, Glaze/widgets/src)

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(),
                        fick.empty() ? "" : " — ", fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string tal(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return buf;
}

// Tal i en TESTFIL är tillåtna (regel 3 gäller widget-filer) — de här är
// grindens färger och mätpunkter.
const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kRöd{255, 0, 0, 255};
const gfx::Color kGrön{0, 200, 0, 255};

// En uppladdning MITT I FRAMEN till något som redan ritats i den bryter
// render-passet — bara på Metal, där bliten kräver ett encoder-brott; D3D11
// och GL ordnar uppladdningen i kontexten och har inget pass att bryta
// (RenderStats::uploadBreaks).
#if defined(GLAZE_BACKEND_METAL)
constexpr std::uint32_t kBrottMittIFramen = 1;
#else
constexpr std::uint32_t kBrottMittIFramen = 0;
#endif

// ------------------------------------------------------------- scenen

// En chunk på GPU:n — byggd EN gång, ritad med kamerans matris för sitt origo.
struct GpuChunk {
    double                          ox = 0.0, oy = 0.0;
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
};

std::vector<GpuChunk> ladda(gfx::Renderer&                     r,
                            const std::vector<gfx::LineChunk>& chunks)
{
    std::vector<GpuChunk> ut;
    for (const gfx::LineChunk& c : chunks) {
        GpuChunk g;
        g.ox = c.originX;
        g.oy = c.originY;
        g.vb = r.createBuffer(gfx::BufferKind::Vertex,
                              c.vertices.size() * sizeof(gfx::LineVertex),
                              c.vertices.data());
        g.ib = r.createBuffer(gfx::BufferKind::Index,
                              c.indices.size() * sizeof(std::uint32_t),
                              c.indices.data());
        g.count = static_cast<std::uint32_t>(c.indices.size());
        ut.push_back(std::move(g));
    }
    return ut;
}

std::vector<gfx::LineChunk> linje(std::vector<gfx::Point2d> p, float bredd,
                                  gfx::Color färg = kRöd)
{
    gfx::PolylineStyle s;
    s.width = bredd;
    s.color = färg;
    std::vector<gfx::LineChunk> c;
    gfx::buildPolyline(p, s, c);
    return c;
}

// Del H: linjen med en hel stil — skarvar, ändar, sluten.
std::vector<gfx::LineChunk> linje(std::vector<gfx::Point2d> p,
                                  const gfx::PolylineStyle& s)
{
    std::vector<gfx::LineChunk> c;
    gfx::buildPolyline(p, s, c);
    return c;
}

// Widgeten som ritar scenen: vit yta, sitt eget klipp (boxen eller ett
// valt, ev. rundat) och chunkarna med kameran.
class Scen : public glaze::Widget {
public:
    gfx::MapView                vy;
    std::vector<GpuChunk>       chunkar;
    std::vector<gfx::LineChunk> transienta;   // laddas upp i VARJE ritning
    gfx::WidthSpace             rum        = gfx::WidthSpace::Screen;
    float                       spetsgräns = 4.0f;   // del H: miterLimit
    gfx::SceneDash              streck;              // del H: strecken
    float                       breddEnhet = 1.0f;   // px per breddenhet
    bool                        breddIMm   = false;  // breddenheten = mm:
                                                     // Ctx::pxPerMm
    float                       pxPerMm    = 0.0f;   // senast ritade Ctx
    bool                        egetKlipp  = false;
    gfx::Rectf                  klipp{};      // relativt boxen
    float                       klippRadie = 0.0f;
    gfx::Rectf                  box{};        // senast ritade (målets px)
    // Ett fall som ritar själv (efter ytan och klippet, i stället för
    // chunkarna) — A8:s uppdatering mitt i en frame.
    std::function<void(gfx::Renderer&)> egenRitning;

    void draw(const glaze::Ctx& c) override
    {
        box     = c.box;
        vy.box  = c.box;
        pxPerMm = c.pxPerMm;
        c.r->fillRect(c.box, kVit);
        const gfx::Rectf k =
            egetKlipp ? gfx::Rectf{c.box.x + klipp.x, c.box.y + klipp.y,
                                   klipp.w, klipp.h}
                      : c.box;
        c.r->pushClip(k, egetKlipp ? klippRadie : 0.0f);
        if (egenRitning) {
            egenRitning(*c.r);
            c.r->popClip();
            return;
        }
        for (const GpuChunk& g : chunkar)
            rita(*c.r, gfx::slice(*g.vb), gfx::slice(*g.ib), g.count, g.ox,
                 g.oy);
        for (const gfx::LineChunk& t : transienta) {
            const gfx::BufferSlice vs = c.r->uploadTransient(
                gfx::BufferKind::Vertex, t.vertices.data(),
                t.vertices.size() * sizeof(gfx::LineVertex));
            const gfx::BufferSlice is = c.r->uploadTransient(
                gfx::BufferKind::Index, t.indices.data(),
                t.indices.size() * sizeof(std::uint32_t));
            rita(*c.r, vs, is, static_cast<std::uint32_t>(t.indices.size()),
                 t.originX, t.originY);
        }
        c.r->popClip();
    }

    void rita(gfx::Renderer& r, const gfx::BufferSlice& v,
              const gfx::BufferSlice& i, std::uint32_t n, double ox,
              double oy) const
    {
        gfx::MeshDraw d;
        d.program    = gfx::MeshProgram::Line;
        d.vertices   = v;
        d.indices    = i;
        d.indexCount = n;
        gfx::viewProjection(vy, ox, oy).toFloat(d.uniforms.viewProj);
        d.uniforms.pxPerWidthUnit = breddIMm ? pxPerMm : breddEnhet;
        d.uniforms.widthSpace     = rum;
        d.uniforms.miterLimit     = spetsgräns;
        d.uniforms.dash           = streck;
        r.drawMesh(d);
    }
};

class Fönster : public glaze::AppWindow {
public:
    Fönster()
    {
        setTitle("scene_pixel_gate");
        setClientSize(480, 360);
        scen.name = "scen";
        scen.dock = glaze::Dock::Fill;
        root().adopt(scen);
    }
    Scen scen;
};

// ------------------------------------------------------------- mätningen

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;

    // Utanför bilden svarar den med en VIT pixel: ett sabotage som skickar
    // linjen till fel ände av världen ska ge ett FEL i mätningen, inte en
    // läsning utanför bufferten.
    const std::uint8_t* at(int x, int y) const
    {
        static const std::uint8_t vit[4] = {255, 255, 255, 255};
        if (x < 0 || y < 0 || x >= w || y >= h ||
            px.size() < static_cast<std::size_t>(w) * h * 4)
            return vit;
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
    // En röd linje på vitt: G = 255 · (1 − täckning).
    double täck(int x, int y) const { return 1.0 - at(x, y)[1] / 255.0; }
};

bool rendera(Fönster& f, Bild& b)
{
    f.scen.invalidate();
    f.window().renderFrame();
    return f.window().captureFrame(b.px, b.w, b.h);
}

// Ytans inre i hela pixlar, med marginal mot kanterna.
struct Yta {
    int    x0, y0, x1, y1;   // [x0, x1) × [y0, y1)
    double cx, cy;           // mitten i fysiska px
};
Yta ytan(const gfx::Rectf& b, int marginal = 2)
{
    return Yta{static_cast<int>(std::ceil(b.x)) + marginal,
               static_cast<int>(std::ceil(b.y)) + marginal,
               static_cast<int>(std::floor(b.x + b.w)) - marginal,
               static_cast<int>(std::floor(b.y + b.h)) - marginal,
               b.x + b.w / 2.0, b.y + b.h / 2.0};
}

double kolumn(const Bild& b, int x, int y0, int y1)
{
    double s = 0.0;
    for (int y = y0; y < y1; ++y)
        s += b.täck(x, y);
    return s;
}
double rad(const Bild& b, int y, int x0, int x1)
{
    double s = 0.0;
    for (int x = x0; x < x1; ++x)
        s += b.täck(x, y);
    return s;
}
// Täckningens tyngdpunkt i pixelcentrum-koordinater.
double tyngdY(const Bild& b, int x, int y0, int y1)
{
    double s = 0.0, m = 0.0;
    for (int y = y0; y < y1; ++y) {
        s += b.täck(x, y);
        m += b.täck(x, y) * (y + 0.5);
    }
    return s > 0.0 ? m / s : -1.0;
}
double tyngdX(const Bild& b, int y, int x0, int x1)
{
    double s = 0.0, m = 0.0;
    for (int x = x0; x < x1; ++x) {
        s += b.täck(x, y);
        m += b.täck(x, y) * (x + 0.5);
    }
    return s > 0.0 ? m / s : -1.0;
}

// Ett nytt fall: tomt klipp, inga linjer, kameran i (0, 0) med y uppåt.
void nollställ(Scen& s)
{
    s.chunkar.clear();
    s.transienta.clear();
    s.vy         = gfx::MapView{};
    s.vy.yUp     = true;
    s.rum        = gfx::WidthSpace::Screen;
    s.spetsgräns = 4.0f;
    s.streck     = gfx::SceneDash{};
    s.breddEnhet = 1.0f;
    s.breddIMm   = false;
    s.egetKlipp  = false;
    s.klippRadie = 0.0f;
    s.egenRitning = nullptr;
}

// Ett fall som sätter `egenRitning` med lambdor mot sina lokala nollar den
// när det lämnas — även vid en tidig retur.
struct EgenRitning {
    Scen& s;
    ~EgenRitning() { s.egenRitning = nullptr; }
};

bool g_ritfel = false;   // en frame gick inte att läsa — ALDRIG en skip

Bild bild(Fönster& f)
{
    Bild b;
    if (!rendera(f, b)) {
        std::printf("  FEL  kunde inte rendera/läsa en frame — ingen "
                    "headless-yta?\n");
        g_ritfel = true;
        ++g_fel;
    }
    return b;
}

// ------------------------------------------------------------------ A1
void fallA1(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    for (double zoom : {1.0, 8.0}) {
        nollställ(s);
        s.vy.pxPerUnit = zoom;
        s.chunkar      = ladda(r, linje({{-5000.0, 0.3}, {5000.0, 0.3}}, 6.0f));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const Yta    y  = ytan(s.box);
        const double fy = y.cy - 0.3 * zoom;   // y uppåt
        const std::string z = "zoom ×" + tal(zoom);
        for (int x : {static_cast<int>(y.cx) - 100, static_cast<int>(y.cx),
                      static_cast<int>(y.cx) + 100}) {
            const double sum = kolumn(b, x, y.y0, y.y1);
            check(std::abs(sum - 6.0) < 0.25,
                  "A1 " + z + ": skärmbredden är 6 px", tal(sum));
        }
        const double ty = tyngdY(b, static_cast<int>(y.cx), y.y0, y.y1);
        check(std::abs(ty - fy) < 0.2, "A1 " + z + ": linjen ligger där kameran "
              "säger", tal(ty) + " mot " + tal(fy));

        // VÄRLDSLÄGET: 6 världsenheter = 6 · zoom px.
        s.rum     = gfx::WidthSpace::World;
        s.chunkar = ladda(r, linje({{-5000.0, 0.3}, {5000.0, 0.3}}, 6.0f));
        const Bild w   = bild(f);
        const double ws = kolumn(w, static_cast<int>(y.cx), y.y0, y.y1);
        check(std::abs(ws - 6.0 * zoom) < 0.3 + 0.01 * zoom,
              "A1 " + z + ": i världsläget följer bredden zoomen",
              tal(ws) + " mot " + tal(6.0 * zoom));
    }
}

// ------------------------------------------------------------------ A2
void fallA2(Fönster& f)
{
    Scen& s = f.scen;
    nollställ(s);
    s.vy.bearingDeg = 30.0;
    s.chunkar = ladda(f.window().renderer(),
                      linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f));
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const Yta y  = ytan(s.box);
    const int xa = static_cast<int>(y.cx) - 60;
    const int xb = static_cast<int>(y.cx) + 60;
    const double lodrätt = 6.0 / std::cos(30.0 * M_PI / 180.0);
    for (int x : {xa, xb}) {
        const double sum = kolumn(b, x, y.y0, y.y1);
        check(std::abs(sum - lodrätt) < 0.3,
              "A2 bäring 30°: bredden är 6 px vinkelrätt (6 / cos 30° lodrätt)",
              tal(sum) + " mot " + tal(lodrätt));
    }
    // Moturs: högra änden ligger HÖGRE (mindre y) — lutningen −tan 30°.
    const double dy = tyngdY(b, xb, y.y0, y.y1) - tyngdY(b, xa, y.y0, y.y1);
    const double fdy = -(xb - xa) * std::tan(30.0 * M_PI / 180.0);
    check(std::abs(dy - fdy) < 0.5, "A2 bäring 30°: linjen vriden MOTURS",
          tal(dy) + " mot " + tal(fdy));
}

// ------------------------------------------------------------------ A3
void fallA3(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    s.vy.pitchDeg = 45.0;
    // Rakt in i bilden: norr bort mot horisonten, söder BAKOM kameran —
    // vertexshaderns närplanstrimning är en del av fallet.
    s.chunkar = ladda(r, linje({{0.3, -5000.0}, {0.3, 5000.0}}, 6.0f));
    Bild b    = bild(f);
    if (g_ritfel)
        return;
    const Yta y  = ytan(s.box);
    const int bh = y.y1 - y.y0;
    for (int ry : {y.y0 + bh / 10, static_cast<int>(y.cy), y.y1 - bh / 10}) {
        const double sum = rad(b, ry, y.x0, y.x1);
        check(std::abs(sum - 6.0) < 0.3,
              "A3 pitch 45°: 6 px bred på rad " + std::to_string(ry - y.y0) +
                  " av " + std::to_string(bh),
              tal(sum));
    }

    // En öst–västlig linje norr om mitten: lika bred lodrätt.
    s.chunkar = ladda(r, linje({{-5000.0, 40.0}, {5000.0, 40.0}}, 6.0f));
    b         = bild(f);
    const double ew = kolumn(b, static_cast<int>(y.cx), y.y0, y.y1);
    check(std::abs(ew - 6.0) < 0.3,
          "A3 pitch 45°: en tvärgående linje bortom mitten är 6 px", tal(ew));

    // VÄRLDSLÄGET under tilt: zoomens bredd på mittraden, smalare bort mot
    // horisonten och bredare närmast.
    s.rum     = gfx::WidthSpace::World;
    s.chunkar = ladda(r, linje({{0.3, -5000.0}, {0.3, 5000.0}}, 6.0f));
    b         = bild(f);
    const double topp = rad(b, y.y0 + bh / 10, y.x0, y.x1);
    const double mitt = rad(b, static_cast<int>(y.cy), y.x0, y.x1);
    const double botten = rad(b, y.y1 - bh / 10, y.x0, y.x1);
    check(std::abs(mitt - 6.0) < 0.4 && topp < mitt - 0.5 &&
              botten > mitt + 0.5,
          "A3 världsläget: smalnar mot horisonten (topp < mitt < botten)",
          tal(topp) + " / " + tal(mitt) + " / " + tal(botten));
}

// ------------------------------------------------------------------ A4
void fallA4(Fönster& f)
{
    Scen& s = f.scen;
    nollställ(s);
    s.chunkar = ladda(f.window().renderer(),
                      linje({{-5000.0, 0.3}, {5000.0, 0.3}}, 6.0f));
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const Yta y = ytan(s.box);
    const int x = static_cast<int>(y.cx);
    int ovan = 0, under = 0, hel = 0;
    const double mitt = y.cy - 0.3;
    for (int ry = y.y0; ry < y.y1; ++ry) {
        const double a = b.täck(x, ry);
        if (a >= 0.97)
            ++hel;
        else if (a > 0.03)
            (ry + 0.5 < mitt ? ovan : under) += 1;
    }
    check(ovan >= 1 && ovan <= 2 && under >= 1 && under <= 2,
          "A4 AA-kanten: 1–2 delvis täckta pixlar på VARJE sida",
          std::to_string(ovan) + " ovan, " + std::to_string(under) + " under");
    check(hel >= 4, "A4 AA-kanten: en hel kärna (≥ 4 helt täckta)",
          std::to_string(hel));
}

// ------------------------------------------------------------------ A5
void fallA5(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();

    // SCISSORN: klippet är ytans vänstra halva.
    nollställ(s);
    s.chunkar = ladda(r, linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f));
    Bild b    = bild(f);   // första gången: boxen blir känd
    if (g_ritfel)
        return;
    s.egetKlipp = true;
    s.klipp     = gfx::Rectf{0.0f, 0.0f, s.box.w / 2.0f, s.box.h};
    b           = bild(f);
    const Yta y = ytan(s.box);
    const double vänster = kolumn(b, static_cast<int>(y.cx) - 100, y.y0, y.y1);
    const double höger   = kolumn(b, static_cast<int>(y.cx) + 100, y.y0, y.y1);
    check(std::abs(vänster - 6.0) < 0.25 && höger < 0.05,
          "A5 scissorn: linjen ritas i klippet och INTE utanför",
          tal(vänster) + " / " + tal(höger));

    // DEN RUNDADE MASKEN: en 30 px bred linje längs klippets överkant. Med y
    // nedåt är världen = px från ytans mitt.
    nollställ(s);
    s.vy.yUp       = false;
    s.egetKlipp    = true;
    const float kw = 240.0f, kh = 160.0f;
    s.klipp = gfx::Rectf{s.box.w / 2.0f - kw / 2.0f, s.box.h / 2.0f - kh / 2.0f,
                         kw, kh};
    s.chunkar = ladda(r, linje({{-5000.0, -kh / 2.0 + 15.0},
                                {5000.0, -kh / 2.0 + 15.0}},
                               30.0f));
    const int hx = static_cast<int>(std::ceil(s.box.x + s.klipp.x)) + 2;
    const int hy = static_cast<int>(std::ceil(s.box.y + s.klipp.y)) + 2;
    const int mx = static_cast<int>(y.cx);
    s.klippRadie = 0.0f;   // KONTROLLEN först
    b            = bild(f);
    check(b.täck(hx, hy) > 0.9 && b.täck(mx, hy) > 0.9,
          "A5 kontroll (radie 0): hörnet och kantens mitt är röda",
          tal(b.täck(hx, hy)) + " / " + tal(b.täck(mx, hy)));
    s.klippRadie = 40.0f;
    b            = bild(f);
    check(b.täck(hx, hy) < 0.1,
          "A5 RUNDAD MASK: Line-programmet maskar hörnet",
          tal(b.täck(hx, hy)));
    check(b.täck(mx, hy) > 0.9, "A5 rundad mask: kantens mitt står kvar",
          tal(b.täck(mx, hy)));
}

// ------------------------------------------------------------------ A6
void fallA6(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    struct Prov {
        double zoom, dx;
    };
    for (const Prov p : {Prov{100.0, 0.37}, Prov{1000.0, 0.137}}) {
        nollställ(s);
        s.vy.cx        = 674000.0;
        s.vy.cy        = 6580000.0;
        s.vy.pxPerUnit = p.zoom;
        const double sp = 2000.0 / p.zoom;   // ±2000 px
        // Lodrät linje på x = mitt + dx och vågrät på y = mitt + dx.
        auto lodrät = linje({{674000.0 + p.dx, 6580000.0 - sp},
                             {674000.0 + p.dx, 6580000.0 + sp}},
                            3.0f);
        auto vågrät = linje({{674000.0 - sp, 6580000.0 + p.dx},
                             {674000.0 + sp, 6580000.0 + p.dx}},
                            3.0f);
        lodrät.insert(lodrät.end(), vågrät.begin(), vågrät.end());
        s.chunkar = ladda(r, lodrät);
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const Yta y  = ytan(s.box);
        const std::string z = tal(p.zoom) + " px/m";
        // Mät en bit bort från korsningen.
        const double tx = tyngdX(b, static_cast<int>(y.cy) + 60, y.x0, y.x1);
        const double ty = tyngdY(b, static_cast<int>(y.cx) - 60, y.y0, y.y1);
        const double fx = y.cx + p.dx * p.zoom;
        const double fy = y.cy - p.dx * p.zoom;   // y uppåt
        check(std::abs(tx - fx) <= 1.0,
              "A6 SWEREF " + z + ": kolumnen ±1 (x = 674000 + " + tal(p.dx) + ")",
              tal(tx) + " mot " + tal(fx));
        check(std::abs(ty - fy) <= 1.0,
              "A6 SWEREF " + z + ": raden ±1 (y = 6580000 + " + tal(p.dx) + ")",
              tal(ty) + " mot " + tal(fy));
    }
}

// ------------------------------------------------------------------ A7
void fallA7(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    s.vy.cx        = 674000.0;
    s.vy.cy        = 6580000.0;
    s.vy.pxPerUnit = 4.0;
    s.chunkar = ladda(r, linje({{673950.0, 6579990.0}, {674050.0, 6580010.0}},
                               5.0f));
    Bild b = bild(f);   // den första framen — buffertarna redan uppe
    if (g_ritfel)
        return;

    r.resetStats();
    struct Kamera {
        double dx, dy, zoom, bäring, tilt;
    };
    const Kamera k[10] = {{5, 0, 4, 0, 0},     {5, 3, 5, 0, 0},
                          {0, 3, 6, 15, 0},    {-4, 2, 6, 30, 10},
                          {-4, -2, 3, 45, 20}, {2, -2, 3, 60, 35},
                          {2, 1, 8, 75, 45},   {0, 1, 8, 90, 55},
                          {1, 0, 2, 120, 60},  {3, 2, 6, 33, 41}};
    for (const Kamera& c : k) {
        s.vy.cx         = 674000.0 + c.dx;
        s.vy.cy         = 6580000.0 + c.dy;
        s.vy.pxPerUnit  = c.zoom;
        s.vy.bearingDeg = c.bäring;
        s.vy.pitchDeg   = c.tilt;
        b               = bild(f);
        if (g_ritfel)
            return;
    }
    const gfx::RenderStats st = r.stats();
    check(st.bytesUploaded == 0,
          "A7 tio kameraändringar: bytesUploaded == 0",
          std::to_string(st.bytesUploaded));
    check(st.buffersCreated == 0, "A7 tio kameraändringar: buffersCreated == 0",
          std::to_string(st.buffersCreated));
    // Ett byte TILL linjen per frame (framens början nollar programmet) —
    // bytet tillbaka till UI:t kommer med compositen, som ett DOLT fönster
    // inte gör på Metal (`setPresenting`). Därför ≥ 10, inte 20.
    check(st.drawCalls >= 20 && st.programSwitches >= 10,
          "A7 linjen ritades i varje frame (rit-anrop och programbyten)",
          std::to_string(st.drawCalls) + " anrop, " +
              std::to_string(st.programSwitches) + " byten");

    // Och den ritades med den SISTA kameran: linjens mittpunkt ligger där
    // `project` säger.
    double px = 0.0, py = 0.0;
    const bool inne = gfx::project(s.vy, 674000.0, 6580000.0, px, py) &&
                      px >= 0.0 && py >= 0.0 && px < b.w && py < b.h;
    double bäst = 0.0;
    for (int dy = -2; inne && dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            bäst = std::max(bäst, b.täck(static_cast<int>(px) + dx,
                                         static_cast<int>(py) + dy));
    check(inne && bäst > 0.9,
          "A7 sista framen: linjen ligger där kameran säger", tal(bäst));
}

// ------------------------------------------------------------------ A8
void fallA8(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();

    // TRANSIENT: byggs om och laddas upp i varje ritning.
    nollställ(s);
    s.transienta = linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f);
    bild(f);   // ringen skapas
    if (g_ritfel)
        return;
    r.resetStats();
    Bild b = bild(f);
    if (g_ritfel)
        return;
    std::uint64_t byte = 0;
    for (const gfx::LineChunk& t : s.transienta)
        byte += t.vertices.size() * sizeof(gfx::LineVertex) +
                t.indices.size() * sizeof(std::uint32_t);
    const Yta y = ytan(s.box);
    check(std::abs(kolumn(b, static_cast<int>(y.cx), y.y0, y.y1) - 6.0) < 0.25,
          "A8 transient: linjen ur ringen ritas");
    check(r.stats().bytesUploaded == byte,
          "A8 transient: en frame laddar upp exakt linjens byte",
          std::to_string(r.stats().bytesUploaded) + " mot " +
              std::to_string(byte));

    // UPPDATERING: samma statiska linje får ny färg till nästa frame.
    nollställ(s);
    auto ch   = linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f);
    s.chunkar = ladda(r, ch);
    b         = bild(f);
    const int mx = static_cast<int>(y.cx);
    const int my = static_cast<int>(std::floor(y.cy));
    check(b.at(mx, my)[0] > 200 && b.at(mx, my)[1] < 60,
          "A8 före uppdateringen: linjen är röd");
    for (gfx::LineVertex& v : ch[0].vertices)
        v.color = kGrön;
    r.resetStats();
    const bool ok = r.updateBuffer(*s.chunkar[0].vb, 0,
                                   ch[0].vertices.data(),
                                   ch[0].vertices.size() *
                                       sizeof(gfx::LineVertex));
    b = bild(f);
    check(ok && b.at(mx, my)[1] > 150 && b.at(mx, my)[0] < 60,
          "A8 updateBuffer: linjen är grön i nästa frame");
    check(r.stats().bytesUploaded ==
              ch[0].vertices.size() * sizeof(gfx::LineVertex),
          "A8 updateBuffer: räknas som exakt sina byte");
    check(!r.updateBuffer(*s.chunkar[0].vb, 2, ch[0].vertices.data(), 40),
          "A8 updateBuffer: en offset som inte är en hel fyrbyte avvisas");

    // MITT I EN FRAME: rita linjen, uppdatera bufferten, rita den igen 40 px
    // längre ned. Det som redan ritats i framen behåller det GAMLA
    // innehållet (renderer.h) — på Metal är det encoder-brottet som mäts.
    nollställ(s);
    s.vy.yUp  = false;
    auto röd  = linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f);
    auto grön = röd;
    for (gfx::LineVertex& v : grön[0].vertices)
        v.color = kGrön;
    s.chunkar = ladda(r, röd);
    s.egenRitning = [&s, &grön](gfx::Renderer& rr) {
        const GpuChunk& g = s.chunkar[0];
        s.rita(rr, gfx::slice(*g.vb), gfx::slice(*g.ib), g.count, g.ox, g.oy);
        rr.updateBuffer(*g.vb, 0, grön[0].vertices.data(),
                        grön[0].vertices.size() * sizeof(gfx::LineVertex));
        const gfx::MapView sparad = s.vy;
        s.vy.cy -= 40.0;   // världen 40 px längre ned på skärmen
        s.rita(rr, gfx::slice(*g.vb), gfx::slice(*g.ib), g.count, g.ox, g.oy);
        s.vy = sparad;
    };
    r.resetStats();
    b = bild(f);
    if (g_ritfel)
        return;
    const int y0 = static_cast<int>(std::floor(y.cy));
    const int y1 = static_cast<int>(std::floor(y.cy + 40.0));
    check(b.at(mx, y0)[0] > 200 && b.at(mx, y0)[1] < 60,
          "A8 mitt i framen: anropet FÖRE uppdateringen ritar det gamla (rött)");
    check(b.at(mx, y1)[1] > 150 && b.at(mx, y1)[0] < 60,
          "A8 mitt i framen: anropet EFTER uppdateringen ritar det nya (grönt)");
    check(r.stats().uploadBreaks == kBrottMittIFramen,
          "A8 mitt i framen: uppdateringen av en buffert som redan ritats i "
          "framen bryter passet EN gång på Metal, aldrig på D3D11/GL",
          std::to_string(r.stats().uploadBreaks) + " brott");
    s.egenRitning = nullptr;   // lambdan pekar på det här fallets lokala
}

// ---------------------------------------------------------------- A9–A11
//
// UPPLADDNINGARNAS VÄGAR. På Metal väljer bufferns STÄMPEL
// väg — aldrig ritad: direkt, ritad i en tidigare frame: uppladdningspasset
// före framen, ritad i den här framen: ett brott (A8 ovan). Referensen är
// pixlarna och brott-räknaren; på D3D11 och GL prövar fallen
// `UpdateSubresource`/`glBufferSubData` mitt i en frame, och brotten är
// alltid 0 där.

// En linje med ny färg på varje vertex.
std::vector<gfx::LineChunk> omfärgad(std::vector<gfx::LineChunk> c, gfx::Color f)
{
    for (gfx::LineChunk& ch : c)
        for (gfx::LineVertex& v : ch.vertices)
            v.color = f;
    return c;
}

// Linjens färg i mittpunkten (hel täckning — linjen är 6 px bred).
bool linjenÄr(const Bild& b, const Yta& y, gfx::Color f)
{
    const std::uint8_t* p = b.at(static_cast<int>(y.cx),
                                 static_cast<int>(std::floor(y.cy)));
    return std::abs(p[0] - f.r) <= 8 && std::abs(p[1] - f.g) <= 8 &&
           std::abs(p[2] - f.b) <= 8;
}

std::string mitten(const Bild& b, const Yta& y)
{
    const std::uint8_t* p = b.at(static_cast<int>(y.cx),
                                 static_cast<int>(std::floor(y.cy)));
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
           std::to_string(p[2]);
}

// ------------------------------------------------------------------ A9
// En NY buffert i framen — `createBuffer(nullptr)` + `updateBuffer` + rita,
// allt inne i framen (ritytans chunkar gör så i varje ritning): linjen syns
// och inget pass bryts (Metal: aldrig ritad ⇒ direkt).
void fallA9(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    s.vy.yUp = false;
    bild(f);   // boxen blir känd
    if (g_ritfel)
        return;
    const EgenRitning               vakt{s};
    const auto                      ch = linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f);
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    s.egenRitning = [&](gfx::Renderer& rr) {
        const gfx::LineChunk& c      = ch[0];
        const std::size_t     vbytes = c.vertices.size() * sizeof(gfx::LineVertex);
        const std::size_t     ibytes = c.indices.size() * sizeof(std::uint32_t);
        vb = rr.createBuffer(gfx::BufferKind::Vertex, vbytes, nullptr);
        ib = rr.createBuffer(gfx::BufferKind::Index, ibytes, nullptr);
        rr.updateBuffer(*vb, 0, c.vertices.data(), vbytes);
        rr.updateBuffer(*ib, 0, c.indices.data(), ibytes);
        s.rita(rr, gfx::slice(*vb), gfx::slice(*ib),
               static_cast<std::uint32_t>(c.indices.size()), c.originX, c.originY);
    };
    r.resetStats();
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const Yta y = ytan(s.box);
    check(linjenÄr(b, y, kRöd) && r.stats().uploadBreaks == 0,
          "A9 en ny buffert fylld och ritad i framen: linjen syns, och inget "
          "pass bryts",
          mitten(b, y) + ", " + std::to_string(r.stats().uploadBreaks) + " brott");
}

// ------------------------------------------------------------------ A10
// Ritad i FÖRRA framen: rött; framen efter uppdateras den till grönt FÖRE
// ritningen — linjen är grön och inget pass bryts (Metal: passet före
// framen).
void fallA10(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    s.vy.yUp       = false;
    const auto röd  = linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f);
    const auto grön = omfärgad(röd, kGrön);
    s.chunkar       = ladda(r, röd);
    Bild b          = bild(f);   // framen som ritar den
    if (g_ritfel)
        return;
    const Yta y = ytan(s.box);
    check(linjenÄr(b, y, kRöd), "A10 (fixturen) framen före ritar rött",
          mitten(b, y));
    const EgenRitning vakt{s};
    s.egenRitning = [&](gfx::Renderer& rr) {
        const GpuChunk& g = s.chunkar[0];
        rr.updateBuffer(*g.vb, 0, grön[0].vertices.data(),
                        grön[0].vertices.size() * sizeof(gfx::LineVertex));
        s.rita(rr, gfx::slice(*g.vb), gfx::slice(*g.ib), g.count, g.ox, g.oy);
    };
    r.resetStats();
    b = bild(f);
    if (g_ritfel)
        return;
    check(linjenÄr(b, y, kGrön) && r.stats().uploadBreaks == 0,
          "A10 ritad i förra framen och uppdaterad före ritningen: det nya "
          "innehållet, inget pass bryts",
          mitten(b, y) + ", " + std::to_string(r.stats().uploadBreaks) + " brott");
}

// ------------------------------------------------------------------ A11
// FYRA FRAMES I RAD, var och en med en uppdatering före ritningen i en ny
// färg (Metal: passet i varje frame, staging ur ringens slot): varje bild har
// sin färg.
void fallA11(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    s.vy.yUp       = false;
    const auto röd = linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 6.0f);
    s.chunkar      = ladda(r, röd);
    bild(f);   // ritad en gång — varje uppdatering nedan går till en ritad
    if (g_ritfel)
        return;
    const Yta         y = ytan(s.box);
    const EgenRitning vakt{s};
    const gfx::Color  färger[4] = {kGrön, {0, 0, 255, 255}, {200, 0, 200, 255},
                                   {0, 160, 160, 255}};
    std::string       fel;
    r.resetStats();
    for (int i = 0; i < 4; ++i) {
        const auto ny = omfärgad(röd, färger[i]);
        s.egenRitning = [&](gfx::Renderer& rr) {
            const GpuChunk& g = s.chunkar[0];
            rr.updateBuffer(*g.vb, 0, ny[0].vertices.data(),
                            ny[0].vertices.size() * sizeof(gfx::LineVertex));
            s.rita(rr, gfx::slice(*g.vb), gfx::slice(*g.ib), g.count, g.ox, g.oy);
        };
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        if (!linjenÄr(b, y, färger[i]))
            fel += " frame " + std::to_string(i + 1) + ": " + mitten(b, y);
    }
    check(fel.empty() && r.stats().uploadBreaks == 0,
          "A11 fyra frames i rad med en uppdatering var: varje bild har sin "
          "färg, och inget pass bryts",
          fel + " " + std::to_string(r.stats().uploadBreaks) + " brott");
}

// ============================================================ DEL B
//
// BILDERNA. Pixlarna läses ur samma retained-yta som
// del A. En bild ritad 1:1 på HELA pixlar samplas mitt i varje texel, så där
// spelar filtret ingen roll — de fallen mäter innehållet. Filtret mäts med
// förstoring (B2).

// RGBA8-pixlar i en färg, w × h, med `stride` byte per rad (luften fylls
// med ett skräpvärde — en uppladdning som läser in luften syns).
std::vector<std::uint8_t> pixlar(int w, int h, gfx::Color c,
                                 std::size_t stride = 0)
{
    const std::size_t rad = static_cast<std::size_t>(w) * 4;
    if (!stride)
        stride = rad;
    std::vector<std::uint8_t> p(stride * static_cast<std::size_t>(h), 0x5a);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* q = &p[static_cast<std::size_t>(y) * stride +
                                 static_cast<std::size_t>(x) * 4];
            q[0] = c.r;
            q[1] = c.g;
            q[2] = c.b;
            q[3] = c.a;
        }
    return p;
}

bool ärFärg(const std::uint8_t* p, gfx::Color c, int tol = 2)
{
    return std::abs(p[0] - c.r) <= tol && std::abs(p[1] - c.g) <= tol &&
           std::abs(p[2] - c.b) <= tol;
}

std::string px(const std::uint8_t* p)
{
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
           std::to_string(p[2]);
}

// Bildens övre vänstra hörn på en HEL pixel i ytan.
gfx::Rectf hel(const gfx::Rectf& box, int dx, int dy, float w, float h)
{
    return gfx::Rectf{std::ceil(box.x) + static_cast<float>(dx),
                      std::ceil(box.y) + static_cast<float>(dy), w, h};
}

// En PLATTA på GPU:n: fyra ImageVertex (världen, chunk-lokalt mot origo 0)
// och sex index, ritad med Image-programmet.
struct Platta {
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
};

// Plattan över världsrektangeln (x0, y0)–(x1, y1); u, v = 0 i (x0, y0).
Platta platta(gfx::Renderer& r, double x0, double y0, double x1, double y1)
{
    const gfx::ImageVertex v[4] = {
        {static_cast<float>(x0), static_cast<float>(y0), 0.0f, 0.0f},
        {static_cast<float>(x1), static_cast<float>(y0), 1.0f, 0.0f},
        {static_cast<float>(x0), static_cast<float>(y1), 0.0f, 1.0f},
        {static_cast<float>(x1), static_cast<float>(y1), 1.0f, 1.0f}};
    const std::uint32_t idx[6] = {0, 1, 2, 1, 3, 2};
    return Platta{r.createBuffer(gfx::BufferKind::Vertex, sizeof v, v),
                  r.createBuffer(gfx::BufferKind::Index, sizeof idx, idx)};
}

// FÄRGJUSTERINGEN som rit-anropet bär den — förvalen orörda.
struct Justering {
    float mättnad = 1.0f, kontrast = 1.0f, ljushet = 1.0f;
};

void ritaPlatta(gfx::Renderer& r, const Platta& p, const gfx::MapView& vy,
                const gfx::Image& img, float opacitet = 1.0f,
                const Justering& j = {})
{
    gfx::MeshDraw d;
    d.program    = gfx::MeshProgram::Image;
    d.vertices   = gfx::slice(*p.vb);
    d.indices    = gfx::slice(*p.ib);
    d.indexCount = 6;
    d.image      = &img;
    gfx::viewProjection(vy, 0.0, 0.0).toFloat(d.uniforms.viewProj);
    d.uniforms.opacity    = opacitet;
    d.uniforms.saturation = j.mättnad;
    d.uniforms.contrast   = j.kontrast;
    d.uniforms.brightness = j.ljushet;
    r.drawMesh(d);
}

// Färgjusteringens DEFINITION på 0–255 (referens för B14/B15):
// ljusheten multiplicerar, kontrasten sträcker kring mittgrått (127,5) och
// mättnaden blandar mot luminansen (Rec. 709), i den ordningen och klampad
// efter varje steg.
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
    const auto   kanal = [](double v) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
    };
    return gfx::Color{kanal(l + (k[0] - l) * mättnad),
                      kanal(l + (k[1] - l) * mättnad),
                      kanal(l + (k[2] - l) * mättnad), 255};
}

// Bildens färg i B14/B15 — tre olika kanaler, så mättnaden och kontrasten
// syns i varje.
const gfx::Color kTegel{200, 100, 50, 255};

// Kameran rakt uppifrån med världen = px från ytans mitt (y nedåt).
gfx::MapView pxVy(const gfx::Rectf& box)
{
    gfx::MapView vy;
    vy.box = box;
    return vy;
}

// Antal MELLANTONER (en kanal strikt mellan 10 och 245) längs en rad.
int mellantoner(const Bild& b, int y, int x0, int x1)
{
    int n = 0;
    for (int x = x0; x < x1; ++x) {
        const int g = b.at(x, y)[1];
        if (g > 10 && g < 245)
            ++n;
    }
    return n;
}

// ------------------------------------------------------------------ B1
void fallB1(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);   // boxen blir känd
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const gfx::Color kBlå{0, 0, 255, 255};

    // 1 × 2 texlar: övre röd, nedre blå, NEAREST — gränsen ligger exakt på
    // v = 0,5. Plattan går från norr (v = 0, långt bort under tilten) till
    // söder (v = 1, nära), så v = 0,5 är världens y = 0 — kamerans mitt.
    const std::vector<std::uint8_t> texlar = {255, 0, 0, 255, 0, 0, 255, 255};
    auto img = r.createImage(1, 2, gfx::ImageFormat::Rgba8, texlar.data(), 0);
    img->setFilter(gfx::ImageFilter::Nearest);
    const double W = s.box.w * 0.3, Y = s.box.h * 0.35;
    const Platta p = platta(r, -W, Y, W, -Y);   // y uppåt: (−W, +Y) = NV

    gfx::MapView vy = pxVy(s.box);
    vy.yUp      = true;
    vy.pitchDeg = 45.0;
    s.egenRitning = [&](gfx::Renderer& rr) { ritaPlatta(rr, p, vy, *img); };
    const Bild b = bild(f);
    if (g_ritfel)
        return;

    // Referens ur kameran: världens y = 0 hamnar på mittraden; de två kanterna
    // där `project` säger. En AFFIN (skärmlinjär) interpolering hade lagt
    // gränsen mitt emellan kanterna i stället.
    double fx = 0, yMitt = 0, yFjärran = 0, yNära = 0;
    const bool ok = gfx::project(vy, 0.0, 0.0, fx, yMitt) &&
                    gfx::project(vy, 0.0, Y, fx, yFjärran) &&
                    gfx::project(vy, 0.0, -Y, fx, yNära);
    const double affin = (yFjärran + yNära) / 2.0;
    check(ok && yFjärran > s.box.y + 2 && yNära < s.box.y + s.box.h - 2 &&
              std::abs(affin - yMitt) > 10.0,
          "B1 förutsättning: plattan syns hel och perspektivet skiljer sig "
          "MÄTBART från en affin interpolering",
          "mitt " + tal(yMitt) + ", affin " + tal(affin));

    // Gränsen: första raden där blått tar över, längs mittkolumnen.
    const int x  = static_cast<int>(s.box.x + s.box.w / 2.0f);
    int       gr = -1;
    for (int y = static_cast<int>(yFjärran) + 2;
         y < static_cast<int>(yNära) - 2; ++y)
        if (b.at(x, y)[2] > b.at(x, y)[0]) {
            gr = y;
            break;
        }
    check(gr >= 0 && std::abs(gr - yMitt) <= 1.5,
          "B1 PERSPEKTIVKORREKT: gränsen v = 0,5 ligger på världens y = 0 "
          "under pitch 45°",
          std::to_string(gr) + " mot " + tal(yMitt) + " (affin " +
              tal(affin) + ")");
}

// ------------------------------------------------------------------ B2
void fallB2(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};

    // 2 × 1 texlar, svart och vit, FÖRSTORAD till 80 × 20 px: Nearest ger
    // en hård gräns (inga mellantoner), Linear en ramp över ~40 px.
    const std::vector<std::uint8_t> sv = {0, 0, 0, 255, 255, 255, 255, 255};
    auto a = r.createImage(2, 1, gfx::ImageFormat::Rgba8, sv.data(), 0);
    auto b = r.createImage(2, 1, gfx::ImageFormat::Rgba8, sv.data(), 0);
    const gfx::Rectf d1 = hel(s.box, 20, 20, 80.0f, 20.0f);
    const gfx::Rectf d2 = hel(s.box, 20, 50, 80.0f, 20.0f);
    const int x0 = static_cast<int>(d1.x) + 2, x1 = static_cast<int>(d1.x) + 78;
    const int r1 = static_cast<int>(d1.y) + 10, r2 = static_cast<int>(d2.y) + 10;

    // UI-VÄGEN, filtret är BATCH-TILLSTÅND: samma bild två gånger i SAMMA
    // frame, Nearest och därefter Linear — båda ska synas som de är.
    s.egenRitning = [&](gfx::Renderer& rr) {
        a->setFilter(gfx::ImageFilter::Nearest);
        rr.drawImage(*a, d1);
        a->setFilter(gfx::ImageFilter::Linear);
        rr.drawImage(*a, d2);
    };
    Bild bd = bild(f);
    if (g_ritfel)
        return;
    const int n1 = mellantoner(bd, r1, x0, x1);
    const int n2 = mellantoner(bd, r2, x0, x1);
    check(n1 <= 1, "B2 drawImage Nearest: hård gräns i förstoringen",
          std::to_string(n1) + " mellantoner");
    check(n2 >= 20, "B2 drawImage Linear (samma bild, samma frame): en ramp",
          std::to_string(n2) + " mellantoner");

    // SCENENS VÄG: Image-programmet med plattan 80 × 20 px rakt uppifrån.
    const Platta p = platta(r, -40.0, -10.0, 40.0, 10.0);
    const gfx::MapView vy = pxVy(s.box);
    const int cx = static_cast<int>(s.box.x + s.box.w / 2.0f);
    const int cy = static_cast<int>(s.box.y + s.box.h / 2.0f);
    for (gfx::ImageFilter fl :
         {gfx::ImageFilter::Nearest, gfx::ImageFilter::Linear}) {
        a->setFilter(fl);
        s.egenRitning = [&](gfx::Renderer& rr) { ritaPlatta(rr, p, vy, *a); };
        bd = bild(f);
        const int n = mellantoner(bd, cy, cx - 38, cx + 38);
        const bool lin = fl == gfx::ImageFilter::Linear;
        check(lin ? n >= 20 : n <= 1,
              std::string("B2 Image-programmet ") +
                  (lin ? "Linear: en ramp" : "Nearest: hård gräns"),
              std::to_string(n) + " mellantoner");
    }

    // LÄCKAN åt båda håll i SAMMA frame — på GL skuggar ett bundet
    // samplerobjekt texturens parametrar, så en väg som inte binder sitt
    // eget ärver det förra: scenen Linear, därefter UI:t Nearest; och UI:t
    // Linear, därefter scenen Nearest.
    a->setFilter(gfx::ImageFilter::Linear);
    b->setFilter(gfx::ImageFilter::Nearest);
    s.egenRitning = [&](gfx::Renderer& rr) {
        ritaPlatta(rr, p, vy, *a);
        rr.drawImage(*b, d1);
    };
    bd = bild(f);
    check(mellantoner(bd, r1, x0, x1) <= 1,
          "B2 läckan: UI:ts Nearest efter scenens Linear förblir hård",
          std::to_string(mellantoner(bd, r1, x0, x1)) + " mellantoner");
    a->setFilter(gfx::ImageFilter::Nearest);
    b->setFilter(gfx::ImageFilter::Linear);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.drawImage(*b, d1);
        ritaPlatta(rr, p, vy, *a);
    };
    bd = bild(f);
    check(mellantoner(bd, cy, cx - 38, cx + 38) <= 1,
          "B2 läckan: scenens Nearest efter UI:ts Linear förblir hård",
          std::to_string(mellantoner(bd, cy, cx - 38, cx + 38)) +
              " mellantoner");
}

// ------------------------------------------------------------------ B4
void fallB4(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};

    // R8 är GRÅSKALA, helt täckande (gfx/image.h) — i båda vägarna.
    const std::uint8_t g[2] = {64, 192};
    auto img = r.createImage(2, 1, gfx::ImageFormat::R8, g, 0);
    img->setFilter(gfx::ImageFilter::Nearest);
    check(img->valid() && img->format() == gfx::ImageFormat::R8,
          "B4 createImage R8: giltig och R8");
    const gfx::Rectf  d = hel(s.box, 20, 20, 40.0f, 20.0f);
    const Platta      p = platta(r, -20.0, -10.0, 20.0, 10.0);
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.drawImage(*img, d);
        ritaPlatta(rr, p, vy, *img);
    };
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const gfx::Color mörk{64, 64, 64, 255}, ljus{192, 192, 192, 255};
    const std::uint8_t* u0 = b.at(static_cast<int>(d.x) + 5,
                                  static_cast<int>(d.y) + 10);
    const std::uint8_t* u1 = b.at(static_cast<int>(d.x) + 35,
                                  static_cast<int>(d.y) + 10);
    const int cx = static_cast<int>(s.box.x + s.box.w / 2.0f);
    const int cy = static_cast<int>(s.box.y + s.box.h / 2.0f);
    const std::uint8_t* s0 = b.at(cx - 15, cy);
    const std::uint8_t* s1 = b.at(cx + 15, cy);
    check(ärFärg(u0, mörk, 1) && ärFärg(u1, ljus, 1),
          "B4 drawImage R8: gråskala 64 och 192, täckande",
          px(u0) + " / " + px(u1));
    check(ärFärg(s0, mörk, 1) && ärFärg(s1, ljus, 1),
          "B4 Image-programmet R8: gråskala 64 och 192, täckande",
          px(s0) + " / " + px(s1));
}

// ------------------------------------------------------------------ B6
//
// KLIPPMASKEN i Image-programmet: en röd platta som täcker hela ytan, under
// ett RUNDAT klipp — hörnet maskat, mitten kvar. Rakt uppifrån OCH under
// tilt: på GL kommer fragmentets läge ur ett perspektivkorrekt (x, y, w),
// och utan divisionen hamnar masken fel just under tilt.
void fallB6(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const std::uint8_t rött[4] = {255, 0, 0, 255};
    auto       img = r.createImage(1, 1, gfx::ImageFormat::Rgba8, rött, 0);
    const Platta p = platta(r, -2000.0, -2000.0, 2000.0, 2000.0);
    const float kw = 160.0f, kh = 120.0f;
    s.egetKlipp  = true;
    s.klipp      = gfx::Rectf{s.box.w / 2.0f - kw / 2.0f,
                              s.box.h / 2.0f - kh / 2.0f, kw, kh};
    const int hx = static_cast<int>(std::ceil(s.box.x + s.klipp.x)) + 2;
    const int hy = static_cast<int>(std::ceil(s.box.y + s.klipp.y)) + 2;
    const int mx = static_cast<int>(s.box.x + s.box.w / 2.0f);
    const int my = static_cast<int>(s.box.y + s.box.h / 2.0f);
    for (double tilt : {0.0, 45.0}) {
        gfx::MapView vy = pxVy(s.box);
        vy.pitchDeg     = tilt;
        s.egenRitning   = [&](gfx::Renderer& rr) {
            ritaPlatta(rr, p, vy, *img);
        };
        s.klippRadie = 0.0f;   // kontrollen först
        Bild b       = bild(f);
        const bool kontroll = b.täck(hx, hy) > 0.9 && b.täck(mx, my) > 0.9;
        s.klippRadie = 40.0f;
        b            = bild(f);
        check(kontroll && b.täck(hx, hy) < 0.1 && b.täck(mx, my) > 0.9,
              "B6 Image-programmet under en RUNDAD mask (pitch " + tal(tilt) +
                  "°): hörnet maskat, mitten kvar",
              tal(b.täck(hx, hy)) + " / " + tal(b.täck(mx, my)) +
                  (kontroll ? "" : " (kontrollen med radie 0 föll)"));
    }
}

// ------------------------------------------------------------------ B7
//
// OPACITETEN: en röd bild med opacitet 0,5 över vitt är (255, 128, 128) —
// i Image-programmet (`SceneUniforms::opacity`) och i drawImage.
void fallB7(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const std::uint8_t rött[4] = {255, 0, 0, 255};
    auto img = r.createImage(1, 1, gfx::ImageFormat::Rgba8, rött, 0);
    const Platta       p  = platta(r, -20.0, -20.0, 20.0, 20.0);
    const gfx::MapView vy = pxVy(s.box);
    const gfx::Rectf   d  = hel(s.box, 20, 20, 40.0f, 40.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        ritaPlatta(rr, p, vy, *img, 0.5f);
        rr.drawImage(*img, d, 0.5f);
    };
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const gfx::Color halv{255, 128, 128, 255};
    const std::uint8_t* sp = b.at(static_cast<int>(s.box.x + s.box.w / 2.0f),
                                  static_cast<int>(s.box.y + s.box.h / 2.0f));
    const std::uint8_t* up = b.at(static_cast<int>(d.x) + 20,
                                  static_cast<int>(d.y) + 20);
    check(ärFärg(sp, halv, 3),
          "B7 Image-programmet: opaciteten 0,5 halverar bildens alfa", px(sp));
    check(ärFärg(up, halv, 3),
          "B7 drawImage: opaciteten 0,5 halverar bildens alfa", px(up));
}

// ------------------------------------------------------------------ B14
//
// FÄRGJUSTERINGEN i Image-programmet (`SceneUniforms::saturation`,
// `contrast` och `brightness`): en bild 1 × 1 i tegel (200, 100, 50) över
// vitt, referens `justerad` — förvalen ger bildens EXAKTA texel, mättnaden 0
// luminansens grå, kontrasten 0 mittgrått, ljusheten 0 svart och 0,5 halva,
// en kombination i ordningen ljushet → kontrast → mättnad, och fasadens
// normering: ett tal som inte är ändligt är 1 och ett negativt 0.
void fallB14(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning  vakt{s};
    const std::uint8_t tegel[4] = {kTegel.r, kTegel.g, kTegel.b, 255};
    auto               img = r.createImage(1, 1, gfx::ImageFormat::Rgba8, tegel, 0);
    img->setFilter(gfx::ImageFilter::Nearest);   // texeln rakt av — exakt
    const Platta       p   = platta(r, -20.0, -20.0, 20.0, 20.0);
    const gfx::MapView vy  = pxVy(s.box);
    const float        nan = std::numeric_limits<float>::quiet_NaN();
    struct Prov {
        Justering   j;
        gfx::Color  väntat;
        int         tol;
        const char* vad;
    };
    const Prov prov[] = {
        {{}, kTegel, 0, "B14 förvalen ritar bildens exakta texel"},
        {{0.0f, 1.0f, 1.0f}, justerad(kTegel, 0, 1, 1), 2,
         "B14 mättnaden 0 ger luminansens grå (GRÅSKALA)"},
        {{1.0f, 0.0f, 1.0f}, justerad(kTegel, 1, 0, 1), 2,
         "B14 kontrasten 0 ger mittgrått"},
        {{1.0f, 1.0f, 0.0f}, justerad(kTegel, 1, 1, 0), 2,
         "B14 ljusheten 0 ger svart"},
        {{1.0f, 1.0f, 0.5f}, justerad(kTegel, 1, 1, 0.5), 2,
         "B14 ljusheten 0,5 halverar färgen"},
        {{0.5f, 1.5f, 0.8f}, justerad(kTegel, 0.5, 1.5, 0.8), 2,
         "B14 alla tre: ljusheten, kontrasten och mättnaden i den ordningen"},
        {{1.0f, nan, 1.0f}, kTegel, 2,
         "B14 en kontrast som inte är ändlig räknas som 1"},
        {{1.0f, 1.0f, -2.0f}, justerad(kTegel, 1, 1, 0), 2,
         "B14 en negativ ljushet räknas som 0"}};
    for (const Prov& pr : prov) {
        s.egenRitning = [&](gfx::Renderer& rr) {
            ritaPlatta(rr, p, vy, *img, 1.0f, pr.j);
        };
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const std::uint8_t* sp = b.at(static_cast<int>(s.box.x + s.box.w / 2.0f),
                                      static_cast<int>(s.box.y + s.box.h / 2.0f));
        check(ärFärg(sp, pr.väntat, pr.tol), pr.vad,
              px(sp) + " mot " + px(&pr.väntat.r));
    }
}

// ------------------------------------------------------------------ B3
void fallB3(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    const gfx::Color kBlå{0, 0, 255, 255};

    nollställ(s);
    bild(f);   // boxen blir känd
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};

    // 16 × 16 röd; ett delrekt (4, 2, 6 × 5) grönt ur en buffert med 11 px
    // per rad — luften får inte läsas in.
    const auto röd  = pixlar(16, 16, kRöd);
    auto       img  = r.createImage(16, 16, gfx::ImageFormat::Rgba8,
                                    röd.data(), 0);
    check(img->valid() && img->width() == 16 && img->height() == 16 &&
              img->format() == gfx::ImageFormat::Rgba8 &&
              !img->premultiplied() && !img->flippedV() &&
              img->filter() == gfx::ImageFilter::Linear,
          "B3 createImage: 16 × 16 RGBA8, rak, rättvänd, linjär som förval");
    const auto grön = pixlar(6, 5, kGrön, 11 * 4);
    r.resetStats();
    check(r.updateImage(*img, 4, 2, 6, 5, grön.data(), 11 * 4),
          "B3 updateImage: delrektet med 11 px stride tas emot");
    check(r.stats().bytesUploaded == 6 * 5 * 4,
          "B3 updateImage: räknas som exakt delrektets byte",
          std::to_string(r.stats().bytesUploaded));

    const gfx::Rectf dst = hel(s.box, 20, 20, 16.0f, 16.0f);
    s.egenRitning = [&img, dst](gfx::Renderer& rr) { rr.drawImage(*img, dst); };
    Bild b = bild(f);
    if (g_ritfel)
        return;
    const int x0 = static_cast<int>(dst.x), y0 = static_cast<int>(dst.y);
    int fel = 0;
    std::string första;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const bool inne = x >= 4 && x < 10 && y >= 2 && y < 7;
            const std::uint8_t* p = b.at(x0 + x, y0 + y);
            if (!ärFärg(p, inne ? kGrön : kRöd)) {
                if (!fel)
                    första = "(" + std::to_string(x) + "," +
                             std::to_string(y) + ") = " + px(p);
                ++fel;
            }
        }
    check(fel == 0, "B3 delrektet: grönt exakt i (4, 2, 6 × 5), rött runt om",
          std::to_string(fel) + " fel" + (fel ? ", första " + första : ""));

    // AVVISNINGARNA: utanför, tom, stride som inte rymmer en rad, stride
    // som inte är hela pixlar, och ett anrop utan pixlar.
    const auto rad = pixlar(16, 16, kBlå);
    check(!r.updateImage(*img, 12, 0, 6, 1, rad.data(), 0) &&
              !r.updateImage(*img, 0, 15, 1, 2, rad.data(), 0) &&
              !r.updateImage(*img, -1, 0, 1, 1, rad.data(), 0) &&
              !r.updateImage(*img, 0, 0, 0, 1, rad.data(), 0) &&
              !r.updateImage(*img, 0, 0, 4, 1, rad.data(), 12) &&
              !r.updateImage(*img, 0, 0, 2, 1, rad.data(), 10) &&
              !r.updateImage(*img, 0, 0, 1, 1, nullptr, 0),
          "B3 updateImage: utanför, tomt, kort stride, halv pixel och "
          "nullptr avvisas");
    check(!r.createImage(0, 4, gfx::ImageFormat::Rgba8, nullptr, 0)->valid() &&
              !r.createImage(4, 4, gfx::ImageFormat::Rgba8, rad.data(), 6)
                   ->valid(),
          "B3 createImage: tom bild och fel stride ger en ogiltig bild");

    // NOLLSTÄLLD utan pixlar: genomskinlig över vitt.
    auto tom = r.createImage(8, 8, gfx::ImageFormat::Rgba8, nullptr, 0);
    const gfx::Rectf dt = hel(s.box, 60, 20, 8.0f, 8.0f);
    s.egenRitning = [&tom, dt](gfx::Renderer& rr) { rr.drawImage(*tom, dt); };
    b = bild(f);
    check(tom->valid() && ärFärg(b.at(static_cast<int>(dt.x) + 3,
                                      static_cast<int>(dt.y) + 3),
                                 kVit),
          "B3 createImage(nullptr): nollställd — ytan syns igenom",
          px(b.at(static_cast<int>(dt.x) + 3, static_cast<int>(dt.y) + 3)));

    // MITT I EN FRAME: rita bilden, skriv om den helt i blått, rita den
    // igen 40 px längre ned. Det första anropet behåller det gamla
    // innehållet (renderer.h) — på D3D11 och GL för att uppdateringen
    // flushar batchen först, på Metal för encoder-brottet.
    const gfx::Rectf da = hel(s.box, 100, 20, 16.0f, 16.0f);
    const gfx::Rectf db = hel(s.box, 100, 60, 16.0f, 16.0f);
    s.egenRitning = [&img, &rad, da, db](gfx::Renderer& rr) {
        rr.drawImage(*img, da);
        rr.updateImage(*img, 0, 0, 16, 16, rad.data(), 0);
        rr.drawImage(*img, db);
    };
    r.resetStats();
    b = bild(f);
    const std::uint8_t* pa = b.at(static_cast<int>(da.x) + 1,
                                  static_cast<int>(da.y) + 1);
    const std::uint8_t* pb = b.at(static_cast<int>(db.x) + 1,
                                  static_cast<int>(db.y) + 1);
    check(ärFärg(pa, kRöd) && ärFärg(pb, kBlå),
          "B3 mitt i framen: anropet FÖRE uppdateringen ritar det gamla, "
          "anropet EFTER det nya",
          px(pa) + " / " + px(pb));
    check(r.stats().uploadBreaks == kBrottMittIFramen,
          "B3 mitt i framen: uppdateringen av en bild som redan ritats i "
          "framen bryter passet EN gång på Metal, aldrig på D3D11/GL",
          std::to_string(r.stats().uploadBreaks) + " brott");
}

// ------------------------------------------------------------------ B12
//
// BILDERNAS VÄGAR — stämpelns tre vägar på Metal, mätt i
// pixlar och brott (B3 ovan är det fjärde: mitt i framen): (a) en NY bild
// uppdaterad i framen före ritningen — direkt, (b) ritad i förra framen och
// uppdaterad före ritningen — uppladdningspasset, (c) två ÖVERLAPPANDE
// uppdateringar av samma ritade bild i en frame (hela blå, därefter 4 × 4
// grönt) — grönt på blått, i anropens ordning. Inget av dem bryter passet.
// (c) mäter ordningen som den blir på maskinen, inte regeln "ett nytt
// blit-pass vid överlapp" (ordningen inom ett pass är odokumenterad).
void fallB12(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    const gfx::Color kBlå{0, 0, 255, 255};
    nollställ(s);
    bild(f);   // boxen blir känd
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const auto       röd  = pixlar(16, 16, kRöd);
    const auto       blå  = pixlar(16, 16, kBlå);
    const auto       grön = pixlar(4, 4, kGrön);
    const gfx::Rectf dst  = hel(s.box, 20, 20, 16.0f, 16.0f);
    const auto       at   = [&dst](const Bild& b, int x, int y) {
        return b.at(static_cast<int>(dst.x) + x, static_cast<int>(dst.y) + y);
    };

    // (a) en NY bild — skapad röd utanför framen, aldrig ritad.
    auto img = r.createImage(16, 16, gfx::ImageFormat::Rgba8, röd.data(), 0);
    img->setFilter(gfx::ImageFilter::Nearest);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.updateImage(*img, 0, 0, 16, 16, blå.data(), 0);
        rr.drawImage(*img, dst);
    };
    r.resetStats();
    Bild b = bild(f);
    if (g_ritfel)
        return;
    check(ärFärg(at(b, 8, 8), kBlå) && r.stats().uploadBreaks == 0,
          "B12a en ny bild uppdaterad i framen före ritningen: den nya färgen, "
          "inget pass bryts",
          px(at(b, 8, 8)) + ", " + std::to_string(r.stats().uploadBreaks) +
              " brott");

    // (b) ritad i förra framen (a) — uppdaterad till röd före ritningen.
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.updateImage(*img, 0, 0, 16, 16, röd.data(), 0);
        rr.drawImage(*img, dst);
    };
    r.resetStats();
    b = bild(f);
    if (g_ritfel)
        return;
    check(ärFärg(at(b, 8, 8), kRöd) && r.stats().uploadBreaks == 0,
          "B12b ritad i förra framen och uppdaterad före ritningen: den nya "
          "färgen, inget pass bryts",
          px(at(b, 8, 8)) + ", " + std::to_string(r.stats().uploadBreaks) +
              " brott");

    // (c) ritad i förra framen (b) — två överlappande uppdateringar: hela
    // bilden blå, därefter 4 × 4 grönt i (2, 2).
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.updateImage(*img, 0, 0, 16, 16, blå.data(), 0);
        rr.updateImage(*img, 2, 2, 4, 4, grön.data(), 0);
        rr.drawImage(*img, dst);
    };
    r.resetStats();
    b = bild(f);
    if (g_ritfel)
        return;
    int         fel = 0;
    std::string första;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const bool inne = x >= 2 && x < 6 && y >= 2 && y < 6;
            if (!ärFärg(at(b, x, y), inne ? kGrön : kBlå)) {
                if (!fel)
                    första = "(" + std::to_string(x) + "," + std::to_string(y) +
                             ") = " + px(at(b, x, y));
                ++fel;
            }
        }
    check(fel == 0 && r.stats().uploadBreaks == 0,
          "B12c två överlappande uppdateringar i en frame: grönt i (2, 2, "
          "4 × 4) på blått — i anropens ordning — och inget pass bryts",
          std::to_string(fel) + " fel" + (fel ? ", första " + första : "") +
              ", " + std::to_string(r.stats().uploadBreaks) + " brott");
}

// ------------------------------------------------------------------ B5
//
// FÖNSTRETS BILDER UR PIXLAR: budgeten, LRU, döda bilder, taket per frame
// och ändringsloggen — mätt på `ImageCache` direkt (egna instanser mot
// fönstrets renderer; `trim()` ÄR en frames början) och, för taket, genom
// fönstrets RIKTIGA väg (renderContent): det som väntar ritas i nästa frame,
// och därefter ritas ingenting mer.

// Ett eget fönster UTAN chrome för fönstervägen: ett rundat fönster (Linux)
// ritar ALLT varje frame, och då går "ingenting ritas om" inte att mäta.
class BildYta : public glaze::Widget {
public:
    std::function<void(const glaze::Ctx&)> rita;
    int                                     ritningar = 0;
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        c.r->fillRect(c.box, kVit);
        if (rita)
            rita(c);
    }
};

class BildFönster : public glaze::AppWindow {
public:
    BildFönster()
        : glaze::AppWindow("scene_pixel_gate B5", 320, 240, gfx::Chrome::None)
    {
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        root().adopt(yta);
    }
    BildYta yta;
};

void fallB5(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    constexpr std::size_t kBild = 64 * 64 * 4;   // en 64 × 64-bild
    // Uppladdningsbudgeten per frame: fönstrets förval (16 MiB), samma tal
    // som `renderContent` lämnar till `trim` — taket nedan mäter det.
    const std::size_t kUpp = glaze::app().canvasUploadBudget();

    // --- BUDGET och LRU: fyra bilder över tre frames, budget för tre.
    {
        glaze::ImageCache  c(r);
        std::vector<glaze::Bitmap> b(4, glaze::Bitmap{});
        for (glaze::Bitmap& x : b)
            x = glaze::Bitmap(64, 64);
        c.trim(~std::size_t{0}, kUpp);
        c.get(b[0]);         // frame 1
        c.trim(~std::size_t{0}, kUpp);
        c.get(b[1]);         // frame 2
        c.trim(~std::size_t{0}, kUpp);
        c.get(b[2]);         // frame 3
        c.get(b[3]);
        check(c.canvasCount() == 4 && c.canvasBytes() == 4 * kBild,
              "B5 fyra bilder ger fyra texturer",
              std::to_string(c.canvasBytes()));
        c.trim(3 * kBild, kUpp);   // b[0] äldst, b[1] näst äldst
        r.resetStats();
        c.get(b[1]);         // kvar — ingen uppladdning
        const auto kvar = r.stats().bytesUploaded;
        c.get(b[0]);         // vräkt — laddas upp igen
        check(kvar == 0 && r.stats().bytesUploaded == kBild,
              "B5 LRU: över budgeten vräks den som legat oritad LÄNGST, "
              "inte den näst äldsta",
              std::to_string(kvar) + " / " +
                  std::to_string(r.stats().bytesUploaded));

        // Det som ritades i förra framen vräks ALDRIG, inte ens över
        // budgeten.
        c.trim(~std::size_t{0}, kUpp);
        for (const glaze::Bitmap& x : b)
            c.get(x);
        c.trim(kBild, kUpp);
        check(c.canvasCount() == 4,
              "B5 det SYNLIGA vräks inte, inte ens över budgeten",
              std::to_string(c.canvasCount()));

        // En DÖD bild (sista handtaget släppt) går i nästa trim.
        b[3] = glaze::Bitmap{};
        c.trim(~std::size_t{0}, kUpp);
        check(c.canvasCount() == 3 && c.canvasBytes() == 3 * kBild,
              "B5 en död bild släpps i nästa trim, budgeten oavsett",
              std::to_string(c.canvasCount()));
    }

    // --- TAKET: en bild på 16 MiB fyller framen; nästa väntar.
    {
        glaze::ImageCache c(r);
        glaze::Bitmap     stor(2048, 2048), liten(8, 8);
        c.trim(~std::size_t{0}, kUpp);
        const bool storUppe = c.get(stor) != nullptr;
        const bool litenUppe = c.get(liten) != nullptr;
        check(storUppe && !litenUppe && c.uploadsDeferred(),
              "B5 taket: 16 MiB i framen — nästa nya bild väntar");
        c.trim(~std::size_t{0}, kUpp);
        check(!c.uploadsDeferred() && c.get(liten) != nullptr,
              "B5 taket: nästa frame laddar upp den som väntade");
    }

    // --- ÄNDRINGSLOGGEN: två fönster (två cacher) i olika takt.
    {
        glaze::ImageCache a(r), bb(r);
        glaze::Bitmap     bm(64, 64);
        bm.fill(kRöd);
        a.trim(~std::size_t{0}, kUpp);
        bb.trim(~std::size_t{0}, kUpp);
        a.get(bm);
        bb.get(bm);
        const auto grön = pixlar(4, 4, kGrön);
        const auto blå  = pixlar(6, 2, gfx::Color{0, 0, 255, 255});
        bm.setPixels(8, 8, 4, 4, grön.data());    // version 1
        bb.trim(~std::size_t{0}, kUpp);
        r.resetStats();
        bb.get(bm);
        check(r.stats().bytesUploaded == 4 * 4 * 4,
              "B5 loggen: en ändrad rektangel laddar upp BARA den",
              std::to_string(r.stats().bytesUploaded));
        bm.setPixels(40, 20, 6, 2, blå.data());   // version 2
        a.trim(~std::size_t{0}, kUpp);
        r.resetStats();
        gfx::Image* ia = a.get(bm);               // två versioner efter
        check(r.stats().bytesUploaded == 4 * 4 * 4 + 6 * 2 * 4,
              "B5 loggen: ett fönster två versioner efter laddar upp båda "
              "rektanglarna",
              std::to_string(r.stats().bytesUploaded));
        const gfx::Rectf dst = hel(s.box, 20, 20, 64.0f, 64.0f);
        s.egenRitning = [ia, dst](gfx::Renderer& rr) {
            rr.drawImage(*ia, dst);
        };
        const Bild b = bild(f);
        const int  x0 = static_cast<int>(dst.x), y0 = static_cast<int>(dst.y);
        check(ia && ärFärg(b.at(x0 + 9, y0 + 9), kGrön) &&
                  ärFärg(b.at(x0 + 42, y0 + 21), gfx::Color{0, 0, 255, 255}) &&
                  ärFärg(b.at(x0 + 2, y0 + 2), kRöd),
              "B5 loggen: fönstret som kom ikapp visar båda ändringarna",
              px(b.at(x0 + 9, y0 + 9)) + " / " + px(b.at(x0 + 42, y0 + 21)));
        s.egenRitning = nullptr;

        // En LÅNG logg ger upp: fler ändringar än loggen bär ⇒ hela bilden.
        for (int i = 0; i < static_cast<int>(glaze::kMaxBitmapChanges) + 8; ++i)
            bm.setPixels(i % 60, 0, 1, 1, grön.data());
        a.trim(~std::size_t{0}, kUpp);
        r.resetStats();
        a.get(bm);
        check(r.stats().bytesUploaded == kBild,
              "B5 loggen: fler ändringar än loggen bär laddar upp hela bilden",
              std::to_string(r.stats().bytesUploaded));
    }

    // --- FÖNSTRETS VÄG: det som väntar ritas i nästa frame, därefter inget.
    {
        BildFönster w;
        w.setOffscreen(true);
        w.show();
        w.window().renderFrame();   // första framen — allt ritas
        glaze::Bitmap stor(2048, 2048), liten(8, 8);
        liten.fill(kGrön);
        liten.setFilter(gfx::ImageFilter::Nearest);
        gfx::Rectf ruta{};
        w.yta.rita = [&](const glaze::Ctx& c) {
            if (gfx::Image* i = c.images->get(stor))
                c.r->drawImage(*i, gfx::Rectf{c.box.x, c.box.y, 32.0f, 32.0f});
            ruta = hel(c.box, 60, 20, 32.0f, 32.0f);
            if (gfx::Image* i = c.images->get(liten))
                c.r->drawImage(*i, ruta);
        };
        w.yta.invalidate();
        w.yta.ritningar = 0;
        Bild b;
        w.window().renderFrame();
        w.window().captureFrame(b.px, b.w, b.h);
        const int rx = static_cast<int>(ruta.x) + 16;
        const int ry = static_cast<int>(ruta.y) + 16;
        check(w.yta.ritningar == 1 && ärFärg(b.at(rx, ry), kVit),
              "B5 fönstret: den lilla bilden väntar under taket",
              px(b.at(rx, ry)));
        w.yta.ritningar = 0;
        w.window().renderFrame();   // ingen ny damage — fönstret begärde den
        w.window().captureFrame(b.px, b.w, b.h);
        check(w.yta.ritningar == 1 && ärFärg(b.at(rx, ry), kGrön),
              "B5 fönstret: nästa frame ritar om och visar den som väntade",
              std::to_string(w.yta.ritningar) + " ritningar, " +
                  px(b.at(rx, ry)));
        w.yta.ritningar = 0;
        w.window().renderFrame();
        check(w.yta.ritningar == 0,
              "B5 fönstret: sedan ritas ingenting mer (regel 1)",
              std::to_string(w.yta.ritningar));
        w.yta.rita = nullptr;
    }
}

// ============================================================ DEL E
//
// RENDER-MÅLEN: att rita i en bild och därefter rita
// bilden — i båda ritvägarna (UI:ts drawImage och Image-programmet), 1:1
// med Nearest så att varje pixel läses ur en enda texel.

// Ett "F" i målets pixlar: lodrät stapel till vänster, lång överstapel och
// kortare mittstapel. En spegling i v lägger den långa stapeln NEDERST.
void ritaF(gfx::Renderer& r)
{
    r.fillRect(gfx::Rectf{8.0f, 6.0f, 6.0f, 36.0f}, kRöd);    // stapeln
    r.fillRect(gfx::Rectf{8.0f, 6.0f, 36.0f, 6.0f}, kRöd);    // överst
    r.fillRect(gfx::Rectf{8.0f, 22.0f, 26.0f, 6.0f}, kRöd);   // mitten
}

void fallE(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const gfx::Color kBlå{0, 0, 255, 255};

    auto mål = [&r](int w, int h) {
        auto t = r.createRenderTarget(w, h);
        t->image().setFilter(gfx::ImageFilter::Nearest);
        return t;
    };
    auto fMål = mål(64, 48);
    check(fMål->image().valid() && fMål->image().width() == 64 &&
              fMål->image().height() == 48 && fMål->image().premultiplied(),
          "E createRenderTarget: 64 × 48, premultiplicerad bild");

    // --- E1 ORIENTERINGEN: "F" läses rättvänt i båda ritvägarna.
    const gfx::Rectf  dUi = hel(s.box, 20, 20, 64.0f, 48.0f);
    const Platta      p   = platta(r, -32.0, -24.0, 32.0, 24.0);
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*fMål, gfx::Color{0, 0, 0, 0});
        ritaF(rr);
        rr.popTarget();
        rr.drawImage(fMål->image(), dUi);
        ritaPlatta(rr, p, vy, fMål->image());
    };
    Bild b = bild(f);
    if (g_ritfel)
        return;
    struct Väg {
        const char* namn;
        int         x0, y0;
    };
    // Plattan ligger mitt i ytan; med origo på en halv pixel hamnar den på
    // hela pixlar ±0,5 — mätpunkterna ligger mitt i staplarna.
    const Väg vägar[2] = {
        {"drawImage", static_cast<int>(dUi.x), static_cast<int>(dUi.y)},
        {"Image-programmet",
         static_cast<int>(std::lround(s.box.x + s.box.w / 2.0f - 32.0f)),
         static_cast<int>(std::lround(s.box.y + s.box.h / 2.0f - 24.0f))}};
    for (const Väg& v : vägar) {
        const std::uint8_t* stapel = b.at(v.x0 + 10, v.y0 + 38);
        const std::uint8_t* över   = b.at(v.x0 + 40, v.y0 + 8);
        const std::uint8_t* under  = b.at(v.x0 + 40, v.y0 + 38);
        const std::uint8_t* mitt   = b.at(v.x0 + 30, v.y0 + 25);
        check(ärFärg(stapel, kRöd) && ärFärg(över, kRöd) &&
                  ärFärg(under, kVit) && ärFärg(mitt, kRöd),
              std::string("E1 ") + v.namn +
                  ": F läses rättvänt (långa stapeln ÖVERST)",
              px(över) + " överst, " + px(under) + " nederst");
    }

    // --- E2 PREMULTIPLIKATIONEN: en kantutjämnad röd cirkel i ett
    //     genomskinligt mål, ritad över vitt. Varje delvis täckt pixel ska
    //     vara rött blandat med vitt — R kvar på 255. Utan
    //     av-premultipliceringen blir kanten MÖRK (R ner mot 191).
    auto cirkel = mål(64, 64);
    const gfx::Rectf dC   = hel(s.box, 100, 20, 64.0f, 64.0f);
    const Platta     pC   = platta(r, -32.0, -32.0, 32.0, 32.0);
    gfx::MapView     vyC  = pxVy(s.box);
    vyC.cx = 120.0;   // kameran åt höger ⇒ plattan till vänster om mitten
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*cirkel, gfx::Color{0, 0, 0, 0});
        rr.fillRoundedRect(gfx::Rectf{8.3f, 8.3f, 47.4f, 47.4f}, 23.7f, kRöd);
        rr.popTarget();
        rr.drawImage(cirkel->image(), dC);
        ritaPlatta(rr, pC, vyC, cirkel->image());
    };
    b = bild(f);
    const int pcx = static_cast<int>(std::lround(s.box.x + s.box.w / 2.0f -
                                                 120.0f - 32.0f));
    const int pcy = static_cast<int>(std::lround(s.box.y + s.box.h / 2.0f -
                                                 32.0f));
    const Väg cvägar[2] = {
        {"drawImage", static_cast<int>(dC.x), static_cast<int>(dC.y)},
        {"Image-programmet", pcx, pcy}};
    for (const Väg& v : cvägar) {
        int delvis = 0, mörka = 0, minR = 255;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const std::uint8_t* q = b.at(v.x0 + x, v.y0 + y);
                if (q[1] < 40 || q[1] > 215)
                    continue;
                ++delvis;
                minR = std::min(minR, static_cast<int>(q[0]));
                if (q[0] < 248)
                    ++mörka;
            }
        check(delvis >= 10 && mörka == 0,
              std::string("E2 ") + v.namn +
                  ": ingen mörk frans över vitt (premultiplicerat ut)",
              std::to_string(delvis) + " delvis täckta, minsta R " +
                  std::to_string(minR));
    }

    // --- E3 KLIPPET: ett yttre klipp gäller INTE i målet, och det står
    //     kvar efter popTarget.
    auto litet = mål(32, 32);
    const gfx::Rectf ytter{s.box.x, s.box.y, 40.0f, 40.0f};
    const gfx::Rectf dL = hel(s.box, 100, 100, 32.0f, 32.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushClip(ytter);
        rr.pushTarget(*litet, gfx::Color{0, 0, 0, 0});
        rr.fillRect(gfx::Rectf{0.0f, 0.0f, 32.0f, 32.0f}, kBlå);
        rr.popTarget();
        rr.fillRect(gfx::Rectf{s.box.x, s.box.y + 20.0f, s.box.w, 4.0f}, kRöd);
        rr.popClip();
        rr.drawImage(litet->image(), dL);
    };
    b = bild(f);
    const int sx = static_cast<int>(s.box.x), sy = static_cast<int>(s.box.y);
    check(ärFärg(b.at(sx + 20, sy + 21), kRöd) &&
              ärFärg(b.at(sx + 200, sy + 21), kVit),
          "E3 klippet efter popTarget är det som gällde före",
          px(b.at(sx + 20, sy + 21)) + " / " + px(b.at(sx + 200, sy + 21)));
    check(ärFärg(b.at(static_cast<int>(dL.x) + 2, static_cast<int>(dL.y) + 2),
                 kBlå) &&
              ärFärg(b.at(static_cast<int>(dL.x) + 29,
                          static_cast<int>(dL.y) + 29),
                     kBlå),
          "E3 fönstrets klipp gäller inte i målet (hela målet blått)",
          px(b.at(static_cast<int>(dL.x) + 2, static_cast<int>(dL.y) + 2)));

    // --- E4 SCISSORN I MÅLET: ett klipp INNE i målet räknas mot MÅLETS
    //     höjd (på GL nerifrån mot det bundna målet — inte fönstret).
    auto halv = mål(64, 48);
    const gfx::Rectf dH = hel(s.box, 180, 100, 64.0f, 48.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*halv, gfx::Color{0, 0, 0, 0});
        rr.pushClip(gfx::Rectf{0.0f, 0.0f, 32.0f, 48.0f});
        rr.fillRect(gfx::Rectf{0.0f, 0.0f, 64.0f, 48.0f}, kGrön);
        rr.popClip();
        rr.popTarget();
        rr.drawImage(halv->image(), dH);
    };
    b = bild(f);
    const int hx = static_cast<int>(dH.x), hy = static_cast<int>(dH.y);
    check(ärFärg(b.at(hx + 8, hy + 4), kGrön) &&
              ärFärg(b.at(hx + 8, hy + 44), kGrön) &&
              ärFärg(b.at(hx + 56, hy + 24), kVit),
          "E4 ett klipp i målet skär MÅLETS vänstra halva, hela höjden",
          px(b.at(hx + 8, hy + 4)) + " / " + px(b.at(hx + 56, hy + 24)));

    // --- E5 NÄSTLADE MÅL: popTarget binder det YTTRE målet igen.
    auto yttre = mål(64, 48);
    auto inre  = mål(16, 16);
    const gfx::Rectf dN = hel(s.box, 260, 100, 64.0f, 48.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*yttre, gfx::Color{0, 0, 0, 0});
        rr.fillRect(gfx::Rectf{0.0f, 0.0f, 20.0f, 48.0f}, kRöd);
        rr.pushTarget(*inre, kBlå);   // rensas blå
        rr.popTarget();
        rr.drawImage(inre->image(), gfx::Rectf{40.0f, 16.0f, 16.0f, 16.0f});
        rr.popTarget();
        rr.drawImage(yttre->image(), dN);
    };
    b = bild(f);
    const int nx = static_cast<int>(dN.x), ny = static_cast<int>(dN.y);
    check(ärFärg(b.at(nx + 10, ny + 24), kRöd) &&
              ärFärg(b.at(nx + 48, ny + 24), kBlå) &&
              ärFärg(b.at(nx + 30, ny + 5), kVit),
          "E5 nästlat: det inre målet ritat i det yttre, inte i fönstret",
          px(b.at(nx + 48, ny + 24)) + " / " + px(b.at(nx + 30, ny + 5)));

    // --- E6 SIG SJÄLV: målets bild ritas inte i målet (loggas).
    auto själv = mål(32, 32);
    const gfx::Rectf dS = hel(s.box, 340, 100, 32.0f, 32.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*själv, gfx::Color{0, 0, 0, 0});
        rr.drawImage(själv->image(), gfx::Rectf{0.0f, 0.0f, 32.0f, 32.0f});
        rr.popTarget();
        rr.drawImage(själv->image(), dS);
    };
    b = bild(f);
    check(ärFärg(b.at(static_cast<int>(dS.x) + 16,
                      static_cast<int>(dS.y) + 16),
                 kVit),
          "E6 målets bild ritas inte i målet självt — målet förblir tomt",
          px(b.at(static_cast<int>(dS.x) + 16, static_cast<int>(dS.y) + 16)));

    // --- E8 RENSFÄRGEN lagras PREMULTIPLICERAD: ett halvgenomskinligt grått
    //     mål över vitt är 128 · 0,5 + 255 · 0,5 ≈ 191. Lagrad rak hade
    //     av-premultipliceringen gjort det vitt (255).
    auto grått = mål(16, 16);
    const gfx::Rectf dG = hel(s.box, 400, 100, 16.0f, 16.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*grått, gfx::Color{128, 128, 128, 128});
        rr.popTarget();
        rr.drawImage(grått->image(), dG);
    };
    b = bild(f);
    const std::uint8_t* g = b.at(static_cast<int>(dG.x) + 8,
                                 static_cast<int>(dG.y) + 8);
    check(ärFärg(g, gfx::Color{191, 191, 191, 255}, 3),
          "E8 rensfärgen lagras premultiplicerad (grått 50 % över vitt)",
          px(g));

    // --- E9 SCENEN I ETT MÅL: Image- och Line-programmen räknar px → NDC
    //     mot MÅLETS storlek. UI-primitiverna märker inget fel där (D3D11:s
    //     viewport och b0 behöver bara vara ense), men en mesh vars program
    //     tar fönstrets storlek, eller ett mål utan sin viewport, hamnar
    //     skalad och förskjuten. En blå platta (16..48, 12..36) och en röd
    //     linje på rad 42 i målets egna pixlar.
    auto scenMål = mål(64, 48);
    const std::uint8_t blåPx[4] = {0, 0, 255, 255};
    auto blåBild = r.createImage(1, 1, gfx::ImageFormat::Rgba8, blåPx, 0);
    const Platta       pS   = platta(r, -16.0, -12.0, 16.0, 12.0);
    const gfx::MapView vyRt = pxVy(gfx::Rectf{0.0f, 0.0f, 64.0f, 48.0f});
    const auto         lin  = ladda(r, linje({{-30.0, 18.0}, {30.0, 18.0}},
                                             4.0f));
    const gfx::Rectf dE9 = hel(s.box, 20, 120, 64.0f, 48.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        rr.pushTarget(*scenMål, gfx::Color{0, 0, 0, 0});
        ritaPlatta(rr, pS, vyRt, *blåBild);
        gfx::MeshDraw d;
        d.program    = gfx::MeshProgram::Line;
        d.vertices   = gfx::slice(*lin[0].vb);
        d.indices    = gfx::slice(*lin[0].ib);
        d.indexCount = lin[0].count;
        gfx::viewProjection(vyRt, lin[0].ox, lin[0].oy)
            .toFloat(d.uniforms.viewProj);
        rr.drawMesh(d);
        rr.popTarget();
        rr.drawImage(scenMål->image(), dE9);
    };
    b = bild(f);
    const int ex = static_cast<int>(dE9.x), ey = static_cast<int>(dE9.y);
    check(ärFärg(b.at(ex + 32, ey + 24), kBlå) &&
              ärFärg(b.at(ex + 18, ey + 14), kBlå) &&
              ärFärg(b.at(ex + 8, ey + 24), kVit) &&
              ärFärg(b.at(ex + 32, ey + 6), kVit),
          "E9 Image-programmet I ett mål: plattan där målets pixlar säger",
          px(b.at(ex + 32, ey + 24)) + " / " + px(b.at(ex + 8, ey + 24)));
    check(ärFärg(b.at(ex + 32, ey + 42), kRöd) &&
              ärFärg(b.at(ex + 6, ey + 42), kRöd) &&
              ärFärg(b.at(ex + 32, ey + 46), kVit),
          "E9 Line-programmet I ett mål: linjen på målets rad 42",
          px(b.at(ex + 32, ey + 42)) + " / " + px(b.at(ex + 32, ey + 46)));
}

// ----------------------------------------------------------------- E7
//
// glaze::RenderTexture: målningen körs LAT i nästa frame som ritar den, en
// gång per version, igen efter repaint — och under budgeten vräks och målas
// den om som en bild laddas upp igen. Genom fönstrets riktiga väg
// (`c.images`) och en egen cache för budgeten.
void fallE7()
{
    BildFönster w;
    w.setOffscreen(true);
    w.show();
    w.window().renderFrame();

    int              målningar = 0;
    glaze::RenderTexture rt(32, 32);
    rt.setFilter(gfx::ImageFilter::Nearest);
    rt.paint([&målningar](const glaze::Ctx& c) {
        ++målningar;
        glaze::Draw::rect(c, c.bounds(), 0.0f, kGrön);
    });
    gfx::Rectf ruta{};
    w.yta.rita = [&](const glaze::Ctx& c) {
        ruta = hel(c.box, 20, 20, 32.0f, 32.0f);
        if (gfx::Image* i = c.images->get(rt, c))
            c.r->drawImage(*i, ruta);
    };
    auto frame = [&w](Bild& b) {
        w.yta.invalidate();
        w.window().renderFrame();
        w.window().captureFrame(b.px, b.w, b.h);
    };
    Bild b;
    frame(b);
    const int rx = static_cast<int>(ruta.x) + 16;
    const int ry = static_cast<int>(ruta.y) + 16;
    check(målningar == 1 && ärFärg(b.at(rx, ry), kGrön),
          "E7 RenderTexture: målad i första framen som ritar den",
          std::to_string(målningar) + " målningar, " + px(b.at(rx, ry)));
    frame(b);
    check(målningar == 1 && ärFärg(b.at(rx, ry), kGrön),
          "E7 RenderTexture: nästa frame ritar texturen utan att måla om",
          std::to_string(målningar));
    rt.repaint();
    frame(b);
    check(målningar == 2, "E7 RenderTexture: repaint målar om i nästa frame",
          std::to_string(målningar));

    // En textur som ritar SIG SJÄLV i sin målning: rekursionen bryts.
    int                  självmålningar = 0;
    glaze::RenderTexture själv(16, 16);
    själv.paint([&](const glaze::Ctx& c) {
        ++självmålningar;
        check(c.images->get(själv, c) == nullptr,
              "E7 RenderTexture: sig själv i sin egen målning ger ingen bild");
    });
    w.yta.rita = [&](const glaze::Ctx& c) { c.images->get(själv, c); };
    frame(b);
    check(självmålningar == 1,
          "E7 RenderTexture: självritningen målas EN gång, ingen rekursion",
          std::to_string(självmålningar));

    // BUDGETEN: två texturer på 4 KB, budget för en. Den som inte ritades i
    // förra framen vräks och MÅLAS OM när den ritas igen.
    glaze::ImageCache    lokal(w.window().renderer());
    int                  ma = 0, mb = 0;
    glaze::RenderTexture ta(32, 32), tb(32, 32);
    ta.paint([&ma](const glaze::Ctx&) { ++ma; });
    tb.paint([&mb](const glaze::Ctx&) { ++mb; });
    const std::size_t en = 32 * 32 * 4;
    int               steg = 0;
    w.yta.rita = [&](const glaze::Ctx& c) {
        lokal.trim(en, glaze::app().canvasUploadBudget());   // den här cachens framstart
        if (steg == 0 || steg == 2)
            lokal.get(ta, c);
        if (steg == 1 || steg == 2)
            lokal.get(tb, c);
    };
    for (steg = 0; steg < 3; ++steg)
        frame(b);
    check(ma == 2 && mb == 1,
          "E7 budgeten: texturen som legat oritad vräks och målas om; den "
          "som ritades i förra framen står kvar",
          "A målad " + std::to_string(ma) + " gånger, B " +
              std::to_string(mb));
    w.yta.rita = nullptr;
}

// ------------------------------------------------------------------ MM
void fallMm(Fönster& f)
{
    Scen& s = f.scen;
    nollställ(s);
    s.breddIMm = true;
    s.chunkar  = ladda(f.window().renderer(),
                       linje({{-5000.0, 0.0}, {5000.0, 0.0}}, 2.0f));   // 2 mm
    const std::string id = f.display().id;
    check(!id.empty(),
          "MM: fönstrets skärm har ett id — kalibreringens nyckel",
          "källa " + std::to_string(static_cast<int>(f.display().source)) +
              ", " + f.display().reason);
    if (id.empty())
        return;
    glaze::app().setDisplayCalibration(id, 4.0f);
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    check(f.display().source == gfx::DisplaySource::Calibrated &&
              f.display().pxPerMm == 4.0f && s.pxPerMm == 4.0f,
          "MM: kalibreringen når Ctx::pxPerMm", tal(s.pxPerMm));
    const Yta y   = ytan(s.box);
    const double sum = kolumn(b, static_cast<int>(y.cx), y.y0, y.y1);
    check(std::abs(sum - 8.0) < 0.25, "MM: 2 mm är 8 px vid 4 px/mm",
          tal(sum));
    glaze::app().setDisplayCalibration(id, 0.0f);   // tillbaka till mätningen
}

// ============================================================ DEL C
//
// YTORNA OCH GRADIENTERNA — Fill-programmet med ytor ur
// `buildPolygon`, i samma retained-yta som del A. Röd yta på vitt ger
// täckningen ur grönkanalen som där. Referens: kantremsan är EN fysisk px utåt
// med täckning från 1 till 0, så en yta W px bred täcker W + 1 px summerat
// längs en rad (½ px på var sida — högst 0,5 px uppblåsning), vid varje
// zoom; gradientens färger kommer ur stoppen, inte ur programmet.

// En ytchunk på GPU:n.
struct GpuYta {
    double                          ox = 0.0, oy = 0.0;
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
};

std::vector<GpuYta> laddaYta(gfx::Renderer&                     r,
                             const std::vector<gfx::FillChunk>& ch)
{
    std::vector<GpuYta> ut;
    for (const gfx::FillChunk& c : ch) {
        GpuYta g;
        g.ox    = c.originX;
        g.oy    = c.originY;
        g.vb    = r.createBuffer(gfx::BufferKind::Vertex,
                                 c.vertices.size() * sizeof(gfx::FillVertex),
                                 c.vertices.data());
        g.ib    = r.createBuffer(gfx::BufferKind::Index,
                                 c.indices.size() * sizeof(std::uint32_t),
                                 c.indices.data());
        g.count = static_cast<std::uint32_t>(c.indices.size());
        ut.push_back(std::move(g));
    }
    return ut;
}

std::vector<GpuYta> yta(gfx::Renderer& r,
                        std::vector<std::vector<gfx::Point2d>> ringar,
                        gfx::Color färg = kRöd)
{
    gfx::PolygonStyle st;
    st.color = färg;
    std::vector<gfx::FillChunk> ch;
    gfx::buildPolygon(ringar, st, ch);
    return laddaYta(r, ch);
}

// En gradient i VÄRLDEN; varje chunk får den relativt sitt eget origo.
struct Gradient {
    gfx::PaintKind kind = gfx::PaintKind::Solid;
    gfx::Point2d   a{}, b{};      // linjär: från a till b; radiell: mitt a
    double         r0 = 0.0, r1 = 0.0;
};

void ritaYta(gfx::Renderer& r, const std::vector<GpuYta>& ytor,
             const gfx::MapView& vy, const Gradient& g = {},
             const gfx::Image* ramp = nullptr)
{
    for (const GpuYta& y : ytor) {
        gfx::MeshDraw d;
        d.program    = gfx::MeshProgram::Fill;
        d.vertices   = gfx::slice(*y.vb);
        d.indices    = gfx::slice(*y.ib);
        d.indexCount = y.count;
        d.image      = ramp;
        gfx::viewProjection(vy, y.ox, y.oy).toFloat(d.uniforms.viewProj);
        if (g.kind == gfx::PaintKind::Linear)
            d.uniforms.paint = gfx::linearPaint(g.a, g.b, y.ox, y.oy);
        else if (g.kind == gfx::PaintKind::Radial)
            d.uniforms.paint = gfx::radialPaint(g.a, g.r0, g.r1, y.ox, y.oy);
        r.drawMesh(d);
    }
}

// Täckningen summerad längs en rad respektive en kolumn, över hela ytan.
double radSumma(const Bild& b, int y, const Yta& u)
{
    return rad(b, y, u.x0 - 2, u.x1 + 2);
}

// ------------------------------------------------------------------ C1
void fallC1(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};

    // 160 × 160 med ett hål 60 × 60 i mitten (världen = px från ytans mitt,
    // y nedåt).
    const auto  ytor = yta(r, {{{-80, -80}, {80, -80}, {80, 80}, {-80, 80}},
                                {{-30, -30}, {30, -30}, {30, 30}, {-30, 30}}});
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaYta(rr, ytor, vy); };
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const Yta u  = ytan(s.box);
    const int cx = static_cast<int>(u.cx), cy = static_cast<int>(u.cy);
    check(b.täck(cx, cy) < 0.02 && b.täck(cx + 25, cy - 25) < 0.02,
          "C1 HÅLET visar bakgrunden (mitten och nära hålets hörn)",
          tal(b.täck(cx, cy)) + " / " + tal(b.täck(cx + 25, cy - 25)));
    check(b.täck(cx + 55, cy) > 0.98 && b.täck(cx, cy - 55) > 0.98 &&
              b.täck(cx - 70, cy + 70) > 0.98,
          "C1 ringen runt hålet är täckt",
          tal(b.täck(cx + 55, cy)) + " / " + tal(b.täck(cx - 70, cy + 70)));
    check(b.täck(cx + 95, cy) < 0.02,
          "C1 utanför ytan är bakgrunden", tal(b.täck(cx + 95, cy)));
}

// ------------------------------------------------------------------ C2
void fallC2(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u = ytan(s.box);

    // KANTREMSAN vid zoom ×1 och ×16: kanterna på bråkdelar av en px, och
    // ytan W px bred ska täcka W + 1 px längs en rad.
    struct Prov {
        double zoom, halv;   // halva bredden i världen
    };
    for (const Prov p : {Prov{1.0, 50.25}, Prov{16.0, 6.03}}) {
        const auto ytor = yta(r, {{{-p.halv, -4.0 * p.halv / p.zoom},
                                   {p.halv, -4.0 * p.halv / p.zoom},
                                   {p.halv, 4.0 * p.halv / p.zoom},
                                   {-p.halv, 4.0 * p.halv / p.zoom}}});
        gfx::MapView vy = pxVy(s.box);
        vy.pxPerUnit    = p.zoom;
        s.egenRitning = [&](gfx::Renderer& rr) { ritaYta(rr, ytor, vy); };
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const double W   = 2.0 * p.halv * p.zoom;
        const double sum = radSumma(b, static_cast<int>(u.cy), u);
        check(std::abs(sum - (W + 1.0)) < 0.1,
              "C2 zoom ×" + tal(p.zoom) + ": kantremsan är 1 px — en yta " +
                  tal(W) + " px bred täcker W + 1",
              tal(sum) + " mot " + tal(W + 1.0));
        // Bandet: inne i ytan (pixelcentrum ½ px innanför kanten) helt
        // täckt, och mer än 1 px utanför kanten ingenting — oavsett var
        // kanten hamnar mellan två pixelcentrum.
        const double cxPx = s.box.x + s.box.w / 2.0;
        const double vä = cxPx - p.halv * p.zoom, hö = cxPx + p.halv * p.zoom;
        int fel = 0;
        for (int x = u.x0 - 2; x < u.x1 + 2; ++x) {
            const double c = x + 0.5;
            const double a = b.täck(x, static_cast<int>(u.cy));
            if ((c > vä + 0.5 && c < hö - 0.5 && a < 0.97) ||
                ((c > hö + 1.0 || c < vä - 1.0) && a > 0.03))
                ++fel;
        }
        check(fel == 0,
              "C2 zoom ×" + tal(p.zoom) + ": inne helt täckt, och inget "
                                         "mer än 1 px utanför kanten",
              std::to_string(fel) + " px fel");
    }

    // UNDER TILT 45°: en yta i världen, kolumnen genom mitten. Kanterna där
    // `project` säger, och täckningen = avståndet + 1 px (½ per kant) — den
    // projicerade remsan är fortfarande en px.
    const auto ytor = yta(r, {{{-60, -50}, {60, -50}, {60, 50}, {-60, 50}}});
    gfx::MapView vy = pxVy(s.box);
    vy.yUp          = true;
    vy.pitchDeg     = 45.0;
    s.egenRitning = [&](gfx::Renderer& rr) { ritaYta(rr, ytor, vy); };
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    double fx = 0, yNära = 0, yFjärran = 0;
    const bool ok = gfx::project(vy, 0.0, -50.0, fx, yNära) &&
                    gfx::project(vy, 0.0, 50.0, fx, yFjärran);
    const double sum = kolumn(b, static_cast<int>(fx), u.y0, u.y1);
    check(ok && std::abs(sum - (yNära - yFjärran + 1.0)) < 0.15,
          "C2 pitch 45°: kantremsan är 1 px också under tilt",
          tal(sum) + " mot " + tal(yNära - yFjärran + 1.0));
}

// ------------------------------------------------------------------ C3
void fallC3(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u  = ytan(s.box);
    const int cx = static_cast<int>(u.cx), cy = static_cast<int>(u.cy);
    const gfx::Color kBlå{0, 0, 255, 255};

    // Rampen ur stoppen röd → blå. Ytan är VIT (vertexfärgen multipliceras)
    // och sträcker sig BORTOM gradientens ändar: pad ger ändfärgerna där.
    const gfx::GradientStop stopp[] = {{0.0f, kRöd}, {1.0f, kBlå}};
    gfx::GradientRamp       pixl{};
    gfx::buildGradientRamp(stopp, pixl);
    auto ramp = r.createImage(gfx::kGradientRampSize, 1, gfx::ImageFormat::Rgba8,
                              pixl.data(), 0);
    check(ramp->valid(), "C3 rampen laddades upp (256 × 1 RGBA8)");
    const auto ytor =
        yta(r, {{{-100, -40}, {100, -40}, {100, 40}, {-100, 40}}}, kVit);

    Gradient g;
    g.kind = gfx::PaintKind::Linear;
    g.a    = {-50.0, 0.0};
    g.b    = {50.0, 0.0};
    gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) {
        ritaYta(rr, ytor, vy, g, ramp.get());
    };
    Bild b = bild(f);
    if (g_ritfel)
        return;
    const gfx::Color lila{128, 0, 128, 255};
    check(ärFärg(b.at(cx - 80, cy), kRöd, 2) && ärFärg(b.at(cx - 51, cy), kRöd, 4),
          "C3 linjär: före startpunkten är det startfärgen (pad)",
          px(b.at(cx - 80, cy)));
    check(ärFärg(b.at(cx + 80, cy), kBlå, 2) && ärFärg(b.at(cx + 51, cy), kBlå, 4),
          "C3 linjär: efter slutpunkten är det slutfärgen (pad)",
          px(b.at(cx + 80, cy)));
    check(ärFärg(b.at(cx, cy), lila, 4),
          "C3 linjär: mitt emellan är det mitt emellan", px(b.at(cx, cy)));

    // Radiell: mitten röd, bortom r1 blå.
    g.kind = gfx::PaintKind::Radial;
    g.a    = {0.0, 0.0};
    g.r0   = 0.0;
    g.r1   = 30.0;
    b      = bild(f);
    check(ärFärg(b.at(cx, cy), kRöd, 6) && ärFärg(b.at(cx + 70, cy), kBlå, 2) &&
              ärFärg(b.at(cx, cy + 35), kBlå, 2),
          "C3 radiell: mitten startfärgen, bortom r1 slutfärgen",
          px(b.at(cx, cy)) + " / " + px(b.at(cx + 70, cy)));

    // UNDER TILT OCH BÄRING ligger gradienten fast i världen: världens
    // x = −80 är rött och x = +80 blått där `project` lägger dem.
    g.kind          = gfx::PaintKind::Linear;
    g.a             = {-50.0, 0.0};
    g.b             = {50.0, 0.0};
    vy.yUp          = true;
    vy.pitchDeg     = 40.0;
    vy.bearingDeg   = 25.0;
    b               = bild(f);
    double xr = 0, yr = 0, xb = 0, yb = 0;
    const bool ok = gfx::project(vy, -80.0, 0.0, xr, yr) &&
                    gfx::project(vy, 80.0, 0.0, xb, yb);
    check(ok && ärFärg(b.at(static_cast<int>(xr), static_cast<int>(yr)), kRöd, 3) &&
              ärFärg(b.at(static_cast<int>(xb), static_cast<int>(yb)), kBlå, 3),
          "C3 tilt 40° och bäring 25°: gradienten ligger fast i världen",
          px(b.at(static_cast<int>(xr), static_cast<int>(yr))) + " / " +
              px(b.at(static_cast<int>(xb), static_cast<int>(yb))));

    // SAMMA BLOCK I LINE-PROGRAMMET: en VIT kontur 8 px bred med samma
    // gradient — röd före starten, blå efter slutet — och anropets
    // opacitet (0,5 över vitt ger en halvt så mättad färg).
    const std::vector<GpuChunk> kontur =
        ladda(r, linje({{-100.0, 0.0}, {100.0, 0.0}}, 8.0f, kVit));
    const gfx::MapView rak = pxVy(s.box);
    float opacitet = 1.0f;
    s.egenRitning = [&](gfx::Renderer& rr) {
        for (const GpuChunk& c : kontur) {
            gfx::MeshDraw d;
            d.program    = gfx::MeshProgram::Line;
            d.vertices   = gfx::slice(*c.vb);
            d.indices    = gfx::slice(*c.ib);
            d.indexCount = c.count;
            d.image      = ramp.get();
            gfx::viewProjection(rak, c.ox, c.oy).toFloat(d.uniforms.viewProj);
            d.uniforms.paint =
                gfx::linearPaint({-50.0, 0.0}, {50.0, 0.0}, c.ox, c.oy);
            d.uniforms.opacity = opacitet;
            rr.drawMesh(d);
        }
    };
    b = bild(f);
    check(ärFärg(b.at(cx - 80, cy), kRöd, 3) && ärFärg(b.at(cx + 80, cy), kBlå, 3) &&
              ärFärg(b.at(cx, cy), lila, 5),
          "C3 Line-programmet: konturen får gradienten (start, slut, mitt)",
          px(b.at(cx - 80, cy)) + " / " + px(b.at(cx, cy)) + " / " +
              px(b.at(cx + 80, cy)));
    opacitet = 0.5f;
    b        = bild(f);
    const gfx::Color halvRöd{255, 128, 128, 255};
    check(ärFärg(b.at(cx - 80, cy), halvRöd, 3),
          "C3 Line-programmet: anropets opacitet 0,5 halverar täckningen",
          px(b.at(cx - 80, cy)));

    // RAMPEN SAMPLAS LINJÄRT — i båda programmen, också när bildens eget
    // filter är Nearest (samplern är batch-tillstånd och texturernas egna
    // parametrar är Nearest, så ett program som inte binder sin linjära
    // sampler hade fått hårda steg). En ramp med en SKARP gräns mellan
    // texel 127 och 128, utdragen över 2550 px: varje texel ~10 px. Linjärt
    // ger det en tioPX-övergång rött → blått kring x = 0, Nearest ett steg.
    const gfx::GradientStop skarp[] = {{0.0f, kRöd}, {0.5f, kRöd},
                                       {0.5f, kBlå}, {1.0f, kBlå}};
    gfx::GradientRamp skarpPx{};
    gfx::buildGradientRamp(skarp, skarpPx);
    auto skarpRamp = r.createImage(gfx::kGradientRampSize, 1,
                                   gfx::ImageFormat::Rgba8, skarpPx.data(), 0);
    skarpRamp->setFilter(gfx::ImageFilter::Nearest);
    const auto lilaPx = [&](const Bild& bb, int y) {
        int n = 0;
        for (int x = cx - 30; x < cx + 30; ++x) {
            const std::uint8_t* p = bb.at(x, y);
            if (p[0] > 40 && p[2] > 40 && p[1] < 40)   // lila — inte vitt
                ++n;
        }
        return n;
    };
    Gradient lång;
    lång.kind = gfx::PaintKind::Linear;
    lång.a    = {-1275.0, 0.0};
    lång.b    = {1275.0, 0.0};
    vy        = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) {
        ritaYta(rr, ytor, vy, lång, skarpRamp.get());
    };
    b = bild(f);
    const int fillLila = lilaPx(b, cy);
    s.egenRitning = [&](gfx::Renderer& rr) {
        for (const GpuChunk& c : kontur) {
            gfx::MeshDraw d;
            d.program    = gfx::MeshProgram::Line;
            d.vertices   = gfx::slice(*c.vb);
            d.indices    = gfx::slice(*c.ib);
            d.indexCount = c.count;
            d.image      = skarpRamp.get();
            gfx::viewProjection(rak, c.ox, c.oy).toFloat(d.uniforms.viewProj);
            d.uniforms.paint = gfx::linearPaint({-1275.0, 0.0}, {1275.0, 0.0},
                                                c.ox, c.oy);
            rr.drawMesh(d);
        }
    };
    b = bild(f);
    const int lineLila = lilaPx(b, cy);
    // Båda kanalerna över 40 under ~69 % av de 10 px: 6–7 px lila
    // linjärt, 0 med Nearest.
    check(fillLila >= 4 && lineLila >= 4,
          "C3 rampen samplas LINJÄRT i Fill och Line (också med bildens "
          "filter Nearest): ~10 px övergång vid en skarp texelgräns",
          std::to_string(fillLila) + " / " + std::to_string(lineLila) +
              " px lila");

    // En gradient UTAN ramp ritas inte (meshDrawOk loggar en gång) — en RÖD
    // yta, så att en ritning som ändå skett syns mot det vita.
    const auto rödYta =
        yta(r, {{{-100, -40}, {100, -40}, {100, 40}, {-100, 40}}}, kRöd);
    s.egenRitning = [&](gfx::Renderer& rr) {
        Gradient utan;
        utan.kind = gfx::PaintKind::Linear;
        utan.a    = {-50.0, 0.0};
        utan.b    = {50.0, 0.0};
        ritaYta(rr, rödYta, vy, utan, nullptr);
    };
    vy = pxVy(s.box);
    b  = bild(f);
    check(b.täck(cx, cy) < 0.02,
          "C3 en gradient utan ramp avvisas — inget ritas",
          tal(b.täck(cx, cy)));
}

// ------------------------------------------------------------------ C4
void fallC4(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};

    // DEN RUNDADE MASKEN i Fill-programmet: en yta som täcker hela klippet;
    // kontroll med radie 0 först (A5:s mönster).
    const float kw = 240.0f, kh = 160.0f;
    s.egetKlipp = true;
    s.klipp     = gfx::Rectf{s.box.w / 2.0f - kw / 2.0f,
                             s.box.h / 2.0f - kh / 2.0f, kw, kh};
    const auto ytor = yta(r, {{{-200, -150}, {200, -150}, {200, 150},
                               {-200, 150}}});
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaYta(rr, ytor, vy); };
    const int hx = static_cast<int>(std::ceil(s.box.x + s.klipp.x)) + 2;
    const int hy = static_cast<int>(std::ceil(s.box.y + s.klipp.y)) + 2;
    const int mx = static_cast<int>(s.box.x + s.box.w / 2.0f);
    s.klippRadie = 0.0f;
    Bild b       = bild(f);
    if (g_ritfel)
        return;
    check(b.täck(hx, hy) > 0.9 && b.täck(mx, hy) > 0.9,
          "C4 kontroll (radie 0): hörnet och kantens mitt är täckta",
          tal(b.täck(hx, hy)) + " / " + tal(b.täck(mx, hy)));
    s.klippRadie = 40.0f;
    b            = bild(f);
    check(b.täck(hx, hy) < 0.1 && b.täck(mx, hy) > 0.9,
          "C4 RUNDAD MASK: Fill-programmet maskar hörnet, kantens mitt står "
          "kvar",
          tal(b.täck(hx, hy)) + " / " + tal(b.täck(mx, hy)));
    s.egetKlipp  = false;
    s.klippRadie = 0.0f;
}

// ============================================================ DEL D
//
// TEXTEN OCH MARKÖRERNA — Glyph-programmet med quads ur
// `buildText`/`buildMarkers` och SDF-atlasen ur den RIKTIGA backendens font.
// Svart text på vitt: bläcket är där täckningen ur grönkanalen är hög.
// Referensen är geometrisk (lådans mått före och efter en kameraändring, en
// form med känd area) och relativ (fet mot vanlig) — aldrig programmets
// egen formel.

const gfx::Color kSvart{0, 0, 0, 255};

struct GpuGlyfer {
    double                          ox = 0.0, oy = 0.0;
    int                             page = -1;
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
};

std::vector<GpuGlyfer> laddaGlyfer(gfx::Renderer&                      r,
                                   const std::vector<gfx::GlyphChunk>& ch)
{
    std::vector<GpuGlyfer> ut;
    for (const gfx::GlyphChunk& c : ch) {
        GpuGlyfer g;
        g.ox    = c.originX;
        g.oy    = c.originY;
        g.page  = c.page;
        g.vb    = r.createBuffer(gfx::BufferKind::Vertex,
                                 c.vertices.size() * sizeof(gfx::GlyphVertex),
                                 c.vertices.data());
        g.ib    = r.createBuffer(gfx::BufferKind::Index,
                                 c.indices.size() * sizeof(std::uint32_t),
                                 c.indices.data());
        g.count = static_cast<std::uint32_t>(c.indices.size());
        ut.push_back(std::move(g));
    }
    return ut;
}

void ritaGlyfer(gfx::Renderer& r, const std::vector<GpuGlyfer>& gl,
                const gfx::MapView& vy, const gfx::SdfAtlas* atlas,
                float pxPerEnhet = 1.0f, float golvPx = 0.0f, float takPx = 0.0f)
{
    for (const GpuGlyfer& g : gl) {
        gfx::MeshDraw d;
        d.program    = gfx::MeshProgram::Glyph;
        d.vertices   = gfx::slice(*g.vb);
        d.indices    = gfx::slice(*g.ib);
        d.indexCount = g.count;
        d.image      = atlas && g.page >= 0
                           ? atlas->page(static_cast<std::size_t>(g.page))
                           : nullptr;
        gfx::viewProjection(vy, g.ox, g.oy).toFloat(d.uniforms.viewProj);
        d.uniforms.pxPerWidthUnit = pxPerEnhet;
        d.uniforms.glyphMinPx     = golvPx;   // (D10)
        d.uniforms.glyphMaxPx     = takPx;
        r.drawMesh(d);
    }
}

// Bläckets låda: pixlarna med täckning över tröskeln i ytan.
struct Låda {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    int w() const { return x1 - x0 + 1; }
    int h() const { return y1 - y0 + 1; }
    bool tom() const { return x1 < x0; }
    double cx() const { return (x0 + x1 + 1) / 2.0; }
    double cy() const { return (y0 + y1 + 1) / 2.0; }
};

Låda bläck(const Bild& b, const Yta& u, double tröskel = 0.5)
{
    Låda l;
    for (int y = u.y0; y < u.y1; ++y)
        for (int x = u.x0; x < u.x1; ++x)
            if (b.täck(x, y) > tröskel) {
                l.x0 = std::min(l.x0, x);
                l.y0 = std::min(l.y0, y);
                l.x1 = std::max(l.x1, x);
                l.y1 = std::max(l.y1, y);
            }
    return l;
}

double bläckSumma(const Bild& b, const Yta& u)
{
    double s = 0.0;
    for (int y = u.y0; y < u.y1; ++y)
        for (int x = u.x0; x < u.x1; ++x)
            s += b.täck(x, y);
    return s;
}

std::string låda(const Låda& l)
{
    return std::to_string(l.w()) + " × " + std::to_string(l.h()) + " vid (" +
           std::to_string(l.x0) + ", " + std::to_string(l.y0) + ")";
}

std::vector<GpuGlyfer> text(gfx::Renderer& r, gfx::SdfAtlas& atlas,
                            const char* utf8, const gfx::TextLayoutStyle& st,
                            gfx::Point2d ankare = {0.0, 0.0})
{
    std::vector<gfx::GlyphChunk> ch;
    gfx::buildText(atlas, utf8, ankare, st, ch);
    return laddaGlyfer(r, ch);
}

// ------------------------------------------------------------------ D1
void fallD1(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u = ytan(s.box);

    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
    check(atlas.valid(), "D1 atlasen laddade backendens UI-font");
    if (!atlas.valid())
        return;
    gfx::TextLayoutStyle st;
    st.size  = 28.0f;
    st.color = kSvart;
    const char* ord = "HHHHHH";
    const auto  gl  = text(r, atlas, ord, st);
    gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, gl, vy, &atlas); };

    // Lådans bredd = Font::measure med den riktiga fonten i samma storlek.
    std::vector<gfx::GlyphChunk> tom;
    const gfx::TextBox tb = gfx::buildText(atlas, ord, {0, 0}, st, tom);
    const auto ref = r.loadFont(gfx::FontFamily::UI, st.size,
                                gfx::FontStyle::Regular);
    check(std::abs((tb.x1 - tb.x0) * st.size - ref->measure(ord).w) <
              0.01f * ref->measure(ord).w,
          "D1 lådans bredd = Font::measure i samma storlek (riktig font)",
          tal((tb.x1 - tb.x0) * st.size) + " mot " +
              tal(ref->measure(ord).w));

    Bild b = bild(f);
    const Låda l0 = bläck(b, u);
    check(!l0.tom() && l0.w() > 3 * l0.h(),
          "D1 texten ritas, liggande (bredare än hög)", låda(l0));

    // UPPRÄTT UNDER BÄRING: Viewport-vinkeln 0 är vågrät på skärmen oavsett
    // kartans vridning, och ankaret ligger kvar.
    vy.bearingDeg = 30.0;
    b             = bild(f);
    const Låda l1 = bläck(b, u);
    check(std::abs(l1.w() - l0.w()) <= 1 && std::abs(l1.h() - l0.h()) <= 1 &&
              std::abs(l1.cx() - l0.cx()) <= 1.0 &&
              std::abs(l1.cy() - l0.cy()) <= 1.0,
          "D1 bäring 30°: texten står kvar upprätt och vågrät (samma låda)",
          låda(l0) + " / " + låda(l1));

    // UPPRÄTT UNDER TILT: billboarden har samma storlek och vinkel.
    vy.bearingDeg = 0.0;
    vy.pitchDeg   = 45.0;
    b             = bild(f);
    const Låda l2 = bläck(b, u);
    check(std::abs(l2.w() - l0.w()) <= 1 && std::abs(l2.h() - l0.h()) <= 1,
          "D1 pitch 45°: texten står upprätt med samma mått", låda(l2));
}

// ------------------------------------------------------------------ D2
void fallD2(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u = ytan(s.box);
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);

    // MAP-ROTATION: vinkeln 0 följer kartans öster — vid bäring 90° står
    // raden lodrätt.
    gfx::TextLayoutStyle st;
    st.size     = 28.0f;
    st.color    = kSvart;
    st.rotation = gfx::TextRotation::Map;
    const auto gl   = text(r, atlas, "HHHHHH", st);
    gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, gl, vy, &atlas); };
    Bild b        = bild(f);
    const Låda l0 = bläck(b, u);
    vy.bearingDeg = 90.0;
    b             = bild(f);
    const Låda l1 = bläck(b, u);
    check(!l0.tom() && std::abs(l1.w() - l0.h()) <= 2 &&
              std::abs(l1.h() - l0.w()) <= 2,
          "D2 Map-rotation följer bäringen: vid 90° står raden lodrätt",
          låda(l0) + " / " + låda(l1));

    // Och vid 30°: lådan är den vridna radens (W cos + H sin, W sin + H cos).
    vy.bearingDeg = 30.0;
    b             = bild(f);
    const Låda   l2 = bläck(b, u);
    const double c = std::cos(30.0 * M_PI / 180.0), sn = std::sin(30.0 * M_PI / 180.0);
    const double fw = l0.w() * c + l0.h() * sn, fh = l0.w() * sn + l0.h() * c;
    check(std::abs(l2.w() - fw) <= 3.0 && std::abs(l2.h() - fh) <= 3.0,
          "D2 Map-rotation vid 30°: lådan är den vridna radens",
          låda(l2) + " mot " + tal(fw) + " × " + tal(fh));
}

// ------------------------------------------------------------------ D3
void fallD3(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u = ytan(s.box);
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);

    // GLORIAN: svart text med röd gloria 3 px. Röda px finns bara med den,
    // och den äter inte fyllningen (lika många svarta px som utan).
    gfx::TextLayoutStyle st;
    st.size  = 32.0f;
    st.color = kSvart;
    const auto utan = text(r, atlas, "IHIH", st);
    st.halo      = kRöd;
    st.haloWidth = 3.0f;
    const auto med  = text(r, atlas, "IHIH", st);
    const gfx::MapView vy = pxVy(s.box);
    const auto räkna = [&](const Bild& b, int& röda, int& svarta) {
        röda = svarta = 0;
        for (int y = u.y0; y < u.y1; ++y)
            for (int x = u.x0; x < u.x1; ++x) {
                const std::uint8_t* p = b.at(x, y);
                if (p[0] > 200 && p[1] < 60 && p[2] < 60)
                    ++röda;
                if (p[0] < 50 && p[1] < 50 && p[2] < 50)
                    ++svarta;
            }
    };
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, utan, vy, &atlas); };
    int r0 = 0, k0 = 0;
    räkna(bild(f), r0, k0);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, med, vy, &atlas); };
    int r1 = 0, k1 = 0;
    räkna(bild(f), r1, k1);
    check(r0 == 0 && r1 > 200,
          "D3 glorian: röda px bara med glorian",
          std::to_string(r0) + " / " + std::to_string(r1));
    check(k0 > 100 && std::abs(k1 - k0) <= k0 / 10,
          "D3 glorian ligger UNDER fyllningen — lika många svarta px",
          std::to_string(k0) + " / " + std::to_string(k1));
}

// ------------------------------------------------------------------ D4
void fallD4(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u = ytan(s.box);
    const std::string id = f.display().id;
    check(!id.empty(), "D4 skärmen har ett id — kalibreringens nyckel");
    if (id.empty())
        return;
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);

    // STORLEKEN I MM: 6 mm vid 4 px/mm (injicerad kalibrering, som MM) ritar
    // som 24 px — enheten är `pxPerWidthUnit` = Ctx::pxPerMm.
    gfx::TextLayoutStyle st;
    st.size  = 24.0f;
    st.color = kSvart;
    const auto px = text(r, atlas, "HHHH", st);
    st.size       = 6.0f;
    const auto mm = text(r, atlas, "HHHH", st);
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, px, vy, &atlas); };
    const Låda lp = bläck(bild(f), u);
    glaze::app().setDisplayCalibration(id, 4.0f);
    s.egenRitning = [&](gfx::Renderer& rr) {
        ritaGlyfer(rr, mm, vy, &atlas, s.pxPerMm);
    };
    bild(f);   // Ctx::pxPerMm når widgeten
    const Låda lm = bläck(bild(f), u);
    check(s.pxPerMm == 4.0f && !lp.tom() && std::abs(lm.w() - lp.w()) <= 1 &&
              std::abs(lm.h() - lp.h()) <= 1,
          "D4 6 mm vid 4 px/mm är 24 px — samma låda som 24 px",
          låda(lp) + " / " + låda(lm));
    glaze::app().setDisplayCalibration(id, 0.0f);
}

// ------------------------------------------------------------------ D5
void fallD5(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u = ytan(s.box);

    // FET ≠ VANLIG: den riktiga feta fonten (Segoe UI Bold, DejaVu Sans
    // Bold …) ger mer bläck; och den syntetiska feta (flaggan på den vanliga
    // fontens quadar) likaså — tröskelförskjutningen i programmet.
    // EN atlas per familj bär alla stilarna; stilen är
    // etikettens (`TextLayoutStyle::fontStyle`).
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
    check(atlas.style(gfx::FontStyle::Regular) == gfx::FontStyle::Regular &&
              atlas.style(gfx::FontStyle::Bold) == gfx::FontStyle::Bold,
          "D5 plattformens UI-familj har en riktig fet fil — style(Bold) säger "
          "Bold",
          std::to_string(static_cast<int>(atlas.style(gfx::FontStyle::Bold))));
    gfx::TextLayoutStyle st;
    st.size  = 28.0f;
    st.color = kSvart;
    const char* ord = "Hamburgefonstiv";
    const auto glV  = text(r, atlas, ord, st);
    st.fontStyle    = gfx::FontStyle::Bold;
    const auto glF  = text(r, atlas, ord, st);
    st.fontStyle    = gfx::FontStyle::Regular;
    std::vector<gfx::GlyphChunk> syntCh;
    gfx::buildText(atlas, ord, {0, 0}, st, syntCh);
    for (gfx::GlyphChunk& c : syntCh)
        for (gfx::GlyphVertex& v : c.vertices)
            v.flags |= gfx::glyphflag::kSyntheticBold;
    const auto glS = laddaGlyfer(r, syntCh);
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, glV, vy, &atlas); };
    const double bv = bläckSumma(bild(f), u);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, glF, vy, &atlas); };
    const double bf = bläckSumma(bild(f), u);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, glS, vy, &atlas); };
    const double bs = bläckSumma(bild(f), u);
    check(bv > 50.0 && bf > 1.15 * bv,
          "D5 den riktiga feta fonten ger mer bläck än den vanliga",
          tal(bv) + " / " + tal(bf));
    check(bs > 1.08 * bv,
          "D5 syntetisk fet (tröskeln förskjuten) ger mer bläck",
          tal(bv) + " / " + tal(bs));
}

// ------------------------------------------------------------------ D6
void fallD6(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u  = ytan(s.box);
    const int cx = static_cast<int>(u.cx), cy = static_cast<int>(u.cy);
    const gfx::MapView vy = pxVy(s.box);
    const gfx::Color   kBlå{0, 0, 255, 255};

    // MARKÖRERNA: cirkeln 20 px täcker π·10², kvadraten 400 — skärmkonstant.
    const auto markör = [&](gfx::MarkerShape form, gfx::Color kontur,
                            float konturBredd) {
        gfx::MarkerLayoutStyle m;
        m.shape        = form;
        m.size         = 20.0f;
        m.fill         = kRöd;
        m.outline      = kontur;
        m.outlineWidth = konturBredd;
        std::vector<gfx::GlyphChunk> ch;
        const gfx::Point2d p[] = {{0.0, 0.0}};
        gfx::buildMarkers(p, m, ch);
        return laddaGlyfer(r, ch);
    };
    auto gl = markör(gfx::MarkerShape::Circle, gfx::Color{0, 0, 0, 0}, 0.0f);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, gl, vy, nullptr); };
    const double cirkel = bläckSumma(bild(f), u);
    check(std::abs(cirkel - M_PI * 100.0) < 0.04 * M_PI * 100.0,
          "D6 cirkelmarkören 20 px täcker π · 10²", tal(cirkel));
    gl = markör(gfx::MarkerShape::Square, gfx::Color{0, 0, 0, 0}, 0.0f);
    const double kvadrat = bläckSumma(bild(f), u);
    check(std::abs(kvadrat - 400.0) < 0.04 * 400.0,
          "D6 kvadratmarkören 20 px täcker 400", tal(kvadrat));

    // KONTUREN ligger utanför fyllningen: blå i ringen, röd i mitten.
    gl = markör(gfx::MarkerShape::Circle, kBlå, 4.0f);
    Bild b = bild(f);
    check(ärFärg(b.at(cx, cy), kRöd, 3) && ärFärg(b.at(cx + 12, cy), kBlå, 3),
          "D6 konturen: röd fyllning, blå ring utanför den",
          px(b.at(cx, cy)) + " / " + px(b.at(cx + 12, cy)));

    // PLATTAN bakom texten.
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
    gfx::TextLayoutStyle st;
    st.size       = 24.0f;
    st.color      = kSvart;
    st.background = gfx::Color{255, 255, 0, 255};
    st.padding    = 8.0f;
    std::vector<gfx::GlyphChunk> ch;
    const gfx::TextBox tb = gfx::buildText(atlas, "II", {0, 0}, st, ch);
    const auto plat = laddaGlyfer(r, ch);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, plat, vy, &atlas); };
    b = bild(f);
    const int hörnX = cx + static_cast<int>(tb.x0 * st.size) + 3;
    const int hörnY = cy + static_cast<int>(tb.y0 * st.size) + 3;
    check(ärFärg(b.at(hörnX, hörnY), gfx::Color{255, 255, 0, 255}, 3) &&
              ärFärg(b.at(cx + static_cast<int>(tb.x1 * st.size) + 4, cy), kVit, 3),
          "D6 plattan: gul innanför sin kant, vitt utanför",
          px(b.at(hörnX, hörnY)));

    // DEN RUNDADE MASKEN i Glyph-programmet: en kvadratmarkör 400 px som
    // täcker hela klippet; kontroll med radie 0 först (A5:s mönster).
    gfx::MarkerLayoutStyle stor;
    stor.shape = gfx::MarkerShape::Square;
    stor.size  = 400.0f;
    stor.fill  = kRöd;
    std::vector<gfx::GlyphChunk> sc;
    const gfx::Point2d mitt[] = {{0.0, 0.0}};
    gfx::buildMarkers(mitt, stor, sc);
    const auto kv = laddaGlyfer(r, sc);
    s.egetKlipp   = true;
    const float kw = 240.0f, kh = 160.0f;
    s.klipp = gfx::Rectf{s.box.w / 2.0f - kw / 2.0f, s.box.h / 2.0f - kh / 2.0f,
                         kw, kh};
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, kv, vy, nullptr); };
    const int hx = static_cast<int>(std::ceil(s.box.x + s.klipp.x)) + 2;
    const int hy = static_cast<int>(std::ceil(s.box.y + s.klipp.y)) + 2;
    s.klippRadie = 0.0f;
    b            = bild(f);
    const bool kontroll = b.täck(hx, hy) > 0.9;
    s.klippRadie = 40.0f;
    b            = bild(f);
    check(kontroll && b.täck(hx, hy) < 0.1 && b.täck(cx, hy) > 0.9,
          "D6 RUNDAD MASK: Glyph-programmet maskar hörnet (radie 0 täcker det)",
          tal(b.täck(hx, hy)) + " / " + tal(b.täck(cx, hy)));
    s.egetKlipp  = false;
    s.klippRadie = 0.0f;
}

// ------------------------------------------------------------------ D7
void fallD7(Fönster& f)
{
    Scen&           s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta u  = ytan(s.box);
    const int cx = static_cast<int>(u.cx), cy = static_cast<int>(u.cy);

    // ATLASEN SAMPLAS LINJÄRT — också när sidans eget filter är Nearest. En
    // egen "atlas" på 2 × 1 texlar (fältvärdena 0 och 255) under en quad
    // 120 px bred, UV från texelmitt till texelmitt; storleken 1 px gör
    // täckningen till 0,5 · värdet + 0,25. Linjärt: en jämn ramp över hela
    // quaden (många grånivåer), Nearest: två platåer.
    const std::uint8_t fält[2] = {0, 255};
    auto sida = r.createImage(2, 1, gfx::ImageFormat::R8, fält, 0);
    sida->setFilter(gfx::ImageFilter::Nearest);
    gfx::GlyphVertex v[4];
    const float ox[4] = {-60.0f, 60.0f, -60.0f, 60.0f};
    const float oy[4] = {-10.0f, -10.0f, 10.0f, 10.0f};
    for (int k = 0; k < 4; ++k) {
        v[k].ox    = ox[k];
        v[k].oy    = oy[k];
        v[k].u     = (k & 1) ? 0.75f : 0.25f;
        v[k].v     = 0.5f;
        v[k].color = kSvart;
        v[k].size  = 1.0f;
    }
    const std::uint32_t idx[6] = {0, 1, 2, 2, 1, 3};
    const auto vb = r.createBuffer(gfx::BufferKind::Vertex, sizeof v, v);
    const auto ib = r.createBuffer(gfx::BufferKind::Index, sizeof idx, idx);
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) {
        gfx::MeshDraw d;
        d.program    = gfx::MeshProgram::Glyph;
        d.vertices   = gfx::slice(*vb);
        d.indices    = gfx::slice(*ib);
        d.indexCount = 6;
        d.image      = sida.get();
        gfx::viewProjection(vy, 0.0, 0.0).toFloat(d.uniforms.viewProj);
        rr.drawMesh(d);
    };
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    bool nivå[256] = {};
    int  nivåer    = 0;
    for (int x = cx - 55; x < cx + 55; ++x) {
        const std::uint8_t g = b.at(x, cy)[1];
        if (!nivå[g]) {
            nivå[g] = true;
            ++nivåer;
        }
    }
    check(nivåer >= 30,
          "D7 Glyph-programmet samplar atlasen LINJÄRT (också med sidans filter "
          "Nearest): en jämn ramp, inte två platåer",
          std::to_string(nivåer) + " grånivåer");
}

// ============================================================ DEL F
//
// RITYTAN — en RIKTIG `glaze::Canvas` i ett dolt
// fönster, ritad av fönstret som vilken widget som helst. Allt går genom
// widgetens egen väg (lagren, rutnätet, målarna, tonen); referensen är färger
// ur formernas egna och stilens ton, och kamerans `project` för var saker
// hamnar.

class RitFönster : public glaze::AppWindow {
public:
    RitFönster()
    {
        setTitle("scene_pixel_gate F");
        setClientSize(480, 360);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        yta.setBackground(kVit);
        yta.setBorder(false);
        root().adopt(yta);
    }
    glaze::Canvas yta;
};

// Ett nytt fönster per fall — ritytan har inget "nollställ".
struct Rit {
    RitFönster f;
    gfx::Rectf box{};
    float      skala = 1.0f;

    bool öppna()
    {
        f.setOffscreen(true);
        f.show();
        if (!f.isOpen())
            return false;
        f.window().renderFrame();
        box   = f.yta.resolve(f.yta.behaviorCtx());
        skala = f.window().uiScale();
        return true;
    }
    Bild bild()
    {
        Bild b;
        f.yta.invalidate();
        f.window().renderFrame();
        if (!f.window().captureFrame(b.px, b.w, b.h)) {
            std::printf("  FEL  kunde inte läsa ritytans frame\n");
            g_ritfel = true;
            ++g_fel;
        }
        return b;
    }
    // Ytans inre med marginalen i LOGISKA px: ritytans rundade hörn är
    // fysiska, dubbelt så stora på Retina, och 2 px lämnade där en
    // hörnpixel kvar i ytan — G1/G2 mätte den som bläck.
    Yta inre() const
    {
        return ytan(box, static_cast<int>(std::ceil(2.0f * skala)));
    }
    // Lokala logiska px → bildens pixel.
    int x(double lx) const { return static_cast<int>(box.x + lx * skala); }
    int y(double ly) const { return static_cast<int>(box.y + ly * skala); }
};

// ------------------------------------------------------------------ F1
void fallF1()
{
    Rit r;
    if (!r.öppna())
        return;
    r.f.yta.addShape(glaze::Shape::rect({20, 20, 200, 150}, kRöd));
    Bild b = r.bild();
    check(ärFärg(b.at(r.x(100), r.y(80)), kRöd),
          "F1 lagrets yta ritas (röd på vit)", px(b.at(r.x(100), r.y(80))));
    // INAKTIVERAD: innehållet tonas med 1 − iconFade mot bakgrunden.
    r.f.yta.disable();
    b = r.bild();
    const double a = 1.0 - r.f.style().iconFade;
    const gfx::Color väntad{255, static_cast<std::uint8_t>(std::lround(255.0 * (1.0 - a))),
                            static_cast<std::uint8_t>(std::lround(255.0 * (1.0 - a))),
                            255};
    check(ärFärg(b.at(r.x(100), r.y(80)), väntad, 3),
          "F1 inaktiverad: ytan tonas med 1 − iconFade (rött över vitt)",
          px(b.at(r.x(100), r.y(80))) + " mot " + px(&väntad.r));
}

// ------------------------------------------------------------------ F2
//
// Rutnätet var 20:e världsenhet, kameran i (0, 0) med zoom 1: linjer i
// ytans mitt och var 20:e logiska px, inget mitt emellan. Hårlinjen kan
// delas mellan två pixelkolumner, så MÖRKRET summeras över fem.
double mörker(const Bild& b, int x0, int x1, int y)
{
    double s = 0.0;
    for (int x = x0; x < x1; ++x)
        s += 255.0 - b.at(x, y)[1];
    return s;
}

void fallF2()
{
    Rit r;
    if (!r.öppna())
        return;
    r.f.yta.setCenter(0.0, 0.0);
    r.f.yta.setGridSize(20.0);
    Bild b = r.bild();
    double mx = 0.0, my = 0.0;
    gfx::MapView vy;
    vy.box       = r.box;
    vy.pxPerUnit = r.skala;
    gfx::project(vy, 0.0, 0.0, mx, my);
    const int rad  = static_cast<int>(my + 10.0 * r.skala);   // mitt i en ruta
    const int cx   = static_cast<int>(mx);
    const int steg = static_cast<int>(std::lround(20.0 * r.skala));
    const double påLinje = mörker(b, cx - 2, cx + 3, rad);
    const double mellan  = mörker(b, cx + steg / 2 - 2, cx + steg / 2 + 3, rad);
    const double nästa   = mörker(b, cx + steg - 2, cx + steg + 3, rad);
    check(påLinje > 20.0 && nästa > 20.0 && mellan < 1.0,
          "F2 rutnätet: linjer i världens 0 och 20, ingenting mitt emellan",
          tal(påLinje) + " / " + tal(mellan) + " / " + tal(nästa));
    // GLESARE vid utzoomning: 20 världsenheter blir 4 logiska px, under
    // stilens minsta avstånd (canvasGridMin) — då ritas var 40:e, inte var
    // 20:e.
    r.f.yta.setZoom(0.2);
    b = r.bild();
    const int s20 = static_cast<int>(std::lround(20.0 * 0.2 * r.skala));
    const int s40 = static_cast<int>(std::lround(40.0 * 0.2 * r.skala));
    const double v20 = mörker(b, cx + s20 - 1, cx + s20 + 2, rad);
    const double v40 = mörker(b, cx + s40 - 1, cx + s40 + 2, rad);
    check(v40 > 20.0 && v20 < 1.0,
          "F2 utzoomat glesas rutnätet ut: linje vid 40, ingen vid 20",
          tal(v20) + " / " + tal(v40));
}

// ------------------------------------------------------------------ F3
void fallF3()
{
    Rit r;
    if (!r.öppna())
        return;
    const gfx::Color kBlå{0, 0, 255, 255};
    const gfx::Color kGrönt{0, 200, 0, 255};
    r.f.yta.addShape(glaze::Shape::rect({40, 40, 160, 120}, kBlå));
    r.f.yta.onPaint = [](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.fillRect({80, 60, 40, 30}, kRöd);
    };
    int  hud      = 0;
    bool hudSkärm = false;
    r.f.yta.onPaintOverlay = [&](glaze::Widget*, glaze::PaintArgs& e) {
        ++hud;
        hudSkärm = e.painter.inScreenSpace();
        e.painter.fillRect({0, 0, 10, 10}, kGrönt);
    };
    Bild b = r.bild();
    check(ärFärg(b.at(r.x(100), r.y(75)), kRöd) &&
              ärFärg(b.at(r.x(60), r.y(50)), kBlå),
          "F3 onPaint ritar OVANPÅ lagren",
          px(b.at(r.x(100), r.y(75))) + " / " + px(b.at(r.x(60), r.y(50))));
    // HUD:en ritas i OVERLAY-LAGRET — vid presenten, efter
    // compositen. `captureFrame` läser retained-ytan FÖRE compositen, så
    // HUD:ens pixlar syns inte här (canvas_gate mäter lagrets box och
    // klipp); det som mäts är att den ritas vid presenten och INTE i
    // innehållet.
    check(hud >= 1 && hudSkärm,
          "F3 onPaintOverlay ritas vid presenten, i ritytans lokala px",
          std::to_string(hud));
    check(ärFärg(b.at(r.x(5), r.y(5)), kVit),
          "F3 onPaintOverlay ritas inte i ritytans innehåll (overlay-lagret)",
          px(b.at(r.x(5), r.y(5))));
    r.f.yta.setCenter(5000.0, 5000.0);
    b = r.bild();
    check(ärFärg(b.at(r.x(100), r.y(75)), kVit),
          "F3 onPaint följer kameran — världen har flyttat",
          px(b.at(r.x(100), r.y(75))));
}

// ------------------------------------------------------------------ F4
void fallF4()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setYUp(true);
    y.setCenter(674000.0, 6580000.0);
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
    r.bild();
    gfx::Renderer& rr = r.f.window().renderer();
    rr.resetStats();
    // Scenen har EN kolliderande etikett, och en kameraändring får ladda upp
    // dess synlighetstexel — den lämnar ytan längs vägen och döljs då
    //. Allt annat är geometri, och den ligger kvar.
    std::uint64_t störst = 0, före = 0;
    for (int i = 0; i < 10; ++i) {
        y.setCenter(674000.0 + 7.0 * i, 6580000.0 - 3.0 * i);
        y.setZoom(1.0 + 0.3 * i);
        y.setBearing(static_cast<float>(9 * i));
        y.setPitch(static_cast<float>(5 * i));
        r.bild();
        störst = std::max(störst, rr.stats().bytesUploaded - före);
        före   = rr.stats().bytesUploaded;
    }
    const gfx::RenderStats s = rr.stats();
    check(störst <= 1 && s.buffersCreated == 0 && s.drawCalls > 0,
          "F4 tio kameraändringar: ingen geometri laddas upp och ingen buffert "
          "skapas — lager, text och rutnät ligger kvar på GPU:n (högst "
          "etikettens synlighetstexel per ändring)",
          "störst " + std::to_string(störst) + " byte per ändring, " +
              std::to_string(s.bytesUploaded) + " totalt, " +
              std::to_string(s.buffersCreated) + " buffertar, " +
              std::to_string(s.drawCalls) + " anrop");
}

// ------------------------------------------------------------------ F5
//
// TEXTEN I MM genom ritytan: 6 mm vid en injicerad kalibrering på 4 px/mm
// har samma bläck som 24 fysiska px (= 24 / skalan logiska px).
void fallF5()
{
    Rit r;
    if (!r.öppna())
        return;
    const std::string id = r.f.display().id;
    check(!id.empty(), "F5 skärmen har ett id — kalibreringens nyckel");
    if (id.empty())
        return;
    const Yta u = r.inre();
    glaze::Canvas& y = r.f.yta;
    y.addShape(glaze::Shape::text({200, 150}, "HHHH",
                                  glaze::TextStyle{.size = 24.0f / r.skala,
                                                   .color = kSvart}));
    const Låda lp = bläck(r.bild(), u);
    glaze::app().setDisplayCalibration(id, 4.0f);
    y.setShape(0, glaze::Shape::text({200, 150}, "HHHH",
                                     glaze::TextStyle{.size = glaze::mm(6.0f),
                                                      .color = kSvart}));
    r.bild();
    const Låda lm = bläck(r.bild(), u);
    check(!lp.tom() && std::abs(lm.w() - lp.w()) <= 1 &&
              std::abs(lm.h() - lp.h()) <= 1,
          "F5 ritytans text i mm: 6 mm vid 4 px/mm = 24 px",
          låda(lp) + " / " + låda(lm));
    glaze::app().setDisplayCalibration(id, 0.0f);
}

// ------------------------------------------------------------------ F6
//
// GLIDNINGEN (`easeTo`) stegar av sig själv. Ramarna här ritas med
// `renderFrame` UTAN en invalidate utifrån, så på en partiell väg ritas
// ritytan bara om den själv bad om det i förra ritningen.
void fallF6()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCenter(0.0, 0.0);
    y.setZoom(1.0);
    r.bild();
    const glaze::Camera mål{{300.0, -120.0}, 4.0, 90.0f, 0.0f};
    y.easeTo(mål, 800);
    check(y.isEasing() && y.camera().center.x == 0.0 && y.camera().zoom == 1.0,
          "F6 easeTo flyttar inte kameran förrän ritytan ritas");
    r.f.window().renderFrame();   // första steget — ritytan bad själv
    const double x1 = y.camera().center.x;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    r.f.window().renderFrame();
    const double x2 = y.camera().center.x;
    check(y.isEasing() && x2 > x1 && x2 < mål.center.x,
          "F6 glidningen stegar av sig själv: nästa frame tar nästa steg utan "
          "en invalidate utifrån",
          std::to_string(x1) + " → " + std::to_string(x2));
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    r.f.window().renderFrame();
    const glaze::Camera k = y.camera();
    check(!y.isEasing() && k.center.x == mål.center.x &&
              k.center.y == mål.center.y && k.zoom == mål.zoom &&
              k.bearing == mål.bearing,
          "F6 framme: kameran är EXAKT målet och glidningen slut",
          std::to_string(k.center.x) + ", " + std::to_string(k.center.y) +
              " ×" + std::to_string(k.zoom) + " " + std::to_string(k.bearing) +
              "°");
    y.easeTo(glaze::Camera{{0.0, 0.0}, 1.0, 0.0f, 0.0f}, 800);
    y.setZoom(2.0);
    r.f.window().renderFrame();
    check(!y.isEasing() && y.camera().zoom == 2.0 &&
              y.camera().center.x == mål.center.x,
          "F6 en sättare mitt i glidningen avbryter den — sättarens värde "
          "gäller, resten står kvar");
}

// ============================================================ DEL G
//
// ETIKETTKOLLISIONEN — en riktig `glaze::Canvas` (del
// F:s fönster) med etiketter som KROCKAR. Röd text på vitt sänker grön- och
// blåkanalen, blå text röd- och grönkanalen — så varje etiketts BLÄCK läses
// för sig ur den kanal bara den andra färgen lämnar orörd: röd = Σ (255 − B),
// blå = Σ (255 − R). Referensen är vilken etikett som SYNS, aldrig placeringens
// räkning.

const gfx::Color kBlåText{0, 0, 255, 255};

double rödtBläck(const Bild& b, const Yta& u)
{
    double s = 0.0;
    for (int y = u.y0; y < u.y1; ++y)
        for (int x = u.x0; x < u.x1; ++x)
            s += (255.0 - b.at(x, y)[2]) / 255.0;
    return s;
}

double blåttBläck(const Bild& b, const Yta& u)
{
    double s = 0.0;
    for (int y = u.y0; y < u.y1; ++y)
        for (int x = u.x0; x < u.x1; ++x)
            s += (255.0 - b.at(x, y)[0]) / 255.0;
    return s;
}

glaze::Shape etikett(double x, double y, gfx::Color färg, int prioritet = 0,
                     bool kolliderar = true)
{
    return glaze::Shape::text({x, y}, "HHHHHH",
                              glaze::TextStyle{.size     = 22.0f,
                                               .color    = färg,
                                               .collide  = kolliderar,
                                               .priority = prioritet});
}

// ------------------------------------------------------------------ G1
// Två etiketter vars lådor krockar: BARA EN ritas, och prioriteten avgör.
void fallG1()
{
    Rit r;
    if (!r.öppna())
        return;
    const Yta      u = r.inre();
    glaze::Canvas& y = r.f.yta;
    y.addShape(etikett(200, 150, kRöd, 0));
    y.addShape(etikett(215, 158, kBlåText, 1));
    Bild         b   = r.bild();
    const double rö1 = rödtBläck(b, u), bl1 = blåttBläck(b, u);
    check(rö1 < 1.0 && bl1 > 40.0,
          "G1 två krockande etiketter: bara den med högst prioritet ritas",
          tal(rö1) + " / " + tal(bl1));
    // PRIORITETEN AVGÖR: sänk den blå under den röda.
    y.setShape(1, etikett(215, 158, kBlåText, -1));
    b                = r.bild();
    const double rö2 = rödtBläck(b, u), bl2 = blåttBläck(b, u);
    check(rö2 > 40.0 && bl2 < 1.0,
          "G1 prioriteten avgör — med den blå lägre syns den röda",
          tal(rö2) + " / " + tal(bl2));
    // LIKA PRIORITET: den som kom först behåller platsen.
    y.setShape(1, etikett(215, 158, kBlåText, 0));
    b = r.bild();
    check(rödtBläck(b, u) > 40.0 && blåttBläck(b, u) < 1.0,
          "G1 lika prioritet: den första etiketten vinner");
    // KOLLISIONEN AV i lagret, och på en etikett: båda syns.
    y.setLayerCollision(0, false);
    b = r.bild();
    check(rödtBläck(b, u) > 40.0 && blåttBläck(b, u) > 40.0,
          "G1 setLayerCollision(false): båda etiketterna syns");
    y.setLayerCollision(0, true);
    y.setShape(1, etikett(215, 158, kBlåText, 0, false));
    b = r.bild();
    check(rödtBläck(b, u) > 40.0 && blåttBläck(b, u) > 40.0,
          "G1 collide = false på stilen: etiketten syns och tar ingen plats");
    // ETT ÖVRE LAGER vinner vid lika prioritet (det ritas överst).
    y.removeShape(1);
    const int övre = y.addLayer("över");
    y.addShape(övre, etikett(215, 158, kBlåText, 0));
    b = r.bild();
    check(rödtBläck(b, u) < 1.0 && blåttBläck(b, u) > 40.0,
          "G1 lika prioritet i två lager: det övre lagrets etikett syns");
}

// ------------------------------------------------------------------ G2
// VRIDNINGEN ÄNDRAR LÅDORNA: två Viewport-etiketter sida vid sida krockar
// (texten är bredare än avståndet); vrids kartan 90° hamnar ankarna över
// varandra — längre isär än texten är hög — och båda syns. Med toningen
// på glider den dolda in i stället för att tändas.
void fallG2()
{
    Rit r;
    if (!r.öppna())
        return;
    const Yta      u = r.inre();
    glaze::Canvas& y = r.f.yta;
    y.setCenter(200.0, 150.0);
    y.addShape(etikett(170, 150, kRöd));
    y.addShape(etikett(230, 150, kBlåText));
    Bild b = r.bild();
    check(rödtBläck(b, u) > 40.0 && blåttBläck(b, u) < 1.0,
          "G2 sida vid sida krockar de — den första syns",
          tal(rödtBläck(b, u)) + " / " + tal(blåttBläck(b, u)));
    y.setBearing(90.0f);
    b                = r.bild();
    const double hel = blåttBläck(b, u);
    check(rödtBläck(b, u) > 40.0 && hel > 40.0,
          "G2 bäring 90°: ankarna över varandra — båda syns",
          tal(rödtBläck(b, u)) + " / " + tal(hel));

    // TONINGEN: tillbaka till 0° (den blå döljs direkt utan toning), toningen
    // på, och 90° igen — nu glider den blå in över flera frames och stannar.
    y.setBearing(0.0f);
    r.bild();
    y.setLabelFade(true);
    y.setBearing(90.0f);
    r.bild();
    check(y.isAnimating(), "G2 toningen: en etikett som visas tonar in");
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    const double halv = blåttBläck(r.bild(), u);
    check(halv > 0.1 * hel && halv < 0.9 * hel,
          "G2 …halvvägs är den delvis synlig",
          tal(halv) + " av " + tal(hel));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const double sist = blåttBläck(r.bild(), u);
    check(!y.isAnimating() && std::abs(sist - hel) < 0.02 * hel,
          "G2 …och toningen stannar framme (helt synlig, ingen animering)",
          tal(sist) + " av " + tal(hel));
}

// ------------------------------------------------------------------ G3
// G7:s DELNING: en kameraändring laddar upp HÖGST en byte per etikett — och
// ingen geometri, ingen buffert. 48 etiketter i ett rutnät som krockar vid
// zoom 1 och glesnar ut när kameran zoomar in.
void fallG3()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& y = r.f.yta;
    y.setCenter(0.0, 0.0);
    constexpr int kKol = 8, kRad = 6, kN = kKol * kRad;
    for (int j = 0; j < kRad; ++j)
        for (int i = 0; i < kKol; ++i)
            y.addShape(glaze::Shape::text(
                {(i - kKol / 2) * 40.0, (j - kRad / 2) * 30.0}, "HHHH",
                glaze::TextStyle{.size = 18.0f, .color = kSvart}));
    r.bild();
    gfx::Renderer& rr = r.f.window().renderer();
    const double   zoomar[] = {1.5, 2.5, 1.0, 3.0, 1.2, 0.8, 2.0};
    std::uint64_t  störst = 0, summa = 0;
    rr.resetStats();
    for (double z : zoomar) {
        const std::uint64_t före = rr.stats().bytesUploaded;
        y.setZoom(z);
        r.bild();
        const std::uint64_t delta = rr.stats().bytesUploaded - före;
        störst = std::max(störst, delta);
        summa += delta;
    }
    check(störst <= static_cast<std::uint64_t>(kN) && summa > 0 &&
              rr.stats().buffersCreated == 0,
          "G3 (G7) kameraändringar: högst en byte per etikett — synligheten, "
          "aldrig geometri — och ingen ny buffert",
          "störst " + std::to_string(störst) + " byte (" +
              std::to_string(kN) + " etiketter), totalt " +
              std::to_string(summa) + ", " +
              std::to_string(rr.stats().buffersCreated) + " buffertar");
}

} // namespace

// ============================================================ DEL H
//
// KONTUREN KOMPLETT (Line-programmet): skarvarna, ändarna och
// strecken. Referensen är formernas GEOMETRI — hörnets kvadrat, skivan, fasens
// linje, mönstrets intervall räknade från linjens början och projicerade med
// kamerans `project` — aldrig programmets formel. En pixels täckning i ett
// streck är dess överlapp med strecket (en lådas kantutjämning).

// Världen → bildens pixel (vyn är den senast ritade, `vy.box` satt).
struct Px {
    double x = 0.0, y = 0.0;
};
Px skärm(const Scen& s, double wx, double wy)
{
    Px p;
    gfx::project(s.vy, wx, wy, p.x, p.y);
    return p;
}
double täckVid(const Bild& b, const Px& p)
{
    return b.täck(static_cast<int>(std::floor(p.x)),
                  static_cast<int>(std::floor(p.y)));
}

// Överlappet mellan pixeln [p, p + 1) och intervallen (i samma axel).
double överlapp(double p, const std::vector<std::pair<double, double>>& iv)
{
    double s = 0.0;
    for (const auto& [a, b] : iv) {
        const double lo = std::max(a, p), hi = std::min(b, p + 1.0);
        if (hi > lo)
            s += hi - lo;
    }
    return s;
}

// Ett streckmönster (på, av) i skärmens px längs en vågrät linje xs → xe,
// förskjutet `förskj` px: strecken k = …, −1, 0, 1, … i [xs, xe].
std::vector<std::pair<double, double>> streckIntervall(double xs, double xe,
                                                       double på, double av,
                                                       double förskj)
{
    std::vector<std::pair<double, double>> iv;
    const double period = på + av;
    for (double k = std::floor(-(på + period) / period) - 1;
         xs + k * period - förskj <= xe; k += 1.0) {
        const double a = std::max(xs, xs + k * period - förskj);
        const double b = std::min(xe, xs + k * period - förskj + på);
        if (b > a)
            iv.emplace_back(a, b);
    }
    return iv;
}

// Raden av pixlar [x0, x1) på y jämförd med väntad täckning: antalet pixlar
// som avviker mer än `tol`, och den största avvikelsen.
struct Avvik {
    int    antal = 0;
    double störst = 0.0;
    int    var    = 0;
};
Avvik jämförRad(const Bild& b, int y, int x0, int x1,
                const std::vector<std::pair<double, double>>& iv, double tol)
{
    Avvik a;
    for (int x = x0; x < x1; ++x) {
        const double fel = std::abs(b.täck(x, y) - överlapp(x, iv));
        if (fel > tol)
            ++a.antal;
        if (fel > a.störst) {
            a.störst = fel;
            a.var    = x;
        }
    }
    return a;
}
std::string avvik(const Avvik& a)
{
    return std::to_string(a.antal) + " pixlar, störst " + tal(a.störst) +
           " vid x = " + std::to_string(a.var);
}

gfx::PolylineStyle konturStil(float bredd, gfx::LineJoin join,
                              gfx::LineCap cap = gfx::LineCap::Butt)
{
    gfx::PolylineStyle st;
    st.width = bredd;
    st.color = kRöd;
    st.join  = join;
    st.cap   = cap;
    return st;
}

// ------------------------------------------------------------------ H1
// En rät vinkel österut och därefter norrut (y uppåt), 40 px bred: hörnets
// YTTRE kvadrant är sydöst om knäet, och ingen av segmenten når dit (de
// slutar rakt i knäet). Spetsen fyller kvadraten [0, 20] × [−20, 0], den
// runda skivan radien 20, fasen halvplanet x − y ≤ 20.
void fallH1(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    struct Fall {
        gfx::LineJoin join;
        const char*   namn;
        bool          a, b, c;   // täcks (16, −16), (12, −12), (6, −6)?
    };
    const Fall fall[] = {{gfx::LineJoin::Miter, "spetsig", true, true, true},
                         {gfx::LineJoin::Round, "rund", false, true, true},
                         {gfx::LineJoin::Bevel, "avfasad", false, false, true}};
    for (const Fall& k : fall) {
        nollställ(s);
        s.chunkar = ladda(r, linje({{-150.0, 0.0}, {0.0, 0.0}, {0.0, 150.0}},
                                   konturStil(40.0f, k.join)));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const double ta = täckVid(b, skärm(s, 16, -16));
        const double tb = täckVid(b, skärm(s, 12, -12));
        const double tc = täckVid(b, skärm(s, 6, -6));
        const auto   som = [](double t, bool full) {
            return full ? t > 0.95 : t < 0.05;
        };
        check(som(ta, k.a) && som(tb, k.b) && som(tc, k.c),
              std::string("H1 skarven ") + k.namn +
                  ": hörnets punkter (16,−16) (12,−12) (6,−6) täcks " +
                  (k.a ? "ja " : "nej ") + (k.b ? "ja " : "nej ") +
                  (k.c ? "ja" : "nej"),
              tal(ta) + " " + tal(tb) + " " + tal(tc));
    }
}

// ------------------------------------------------------------------ H2
// SPETSGRÄNSEN: knäet har vinkeln φ mellan segmenten, spetsen är 1/sin(φ/2)
// × bredden lång längs bisektrisen m — 30°: 3,86, 20°: 5,76. Punkten
// knäet + 60 · m ligger inne i spetsen (4,6 resp. 9,7 px från dess kanter)
// och utanför båda segmenten.
void fallH2(Fönster& f)
{
    Scen&          s  = f.scen;
    gfx::Renderer& r  = f.window().renderer();
    constexpr double kPi = 3.14159265358979323846;
    struct Fall {
        double      vinkel;   // φ, grader
        float       gräns;
        bool        spets;    // syns spetsen?
        const char* namn;
    };
    const Fall fall[] = {{30.0, 4.0f, true, "30° med gränsen 4 är spetsig"},
                         {30.0, 3.0f, false, "30° med gränsen 3 är avfasad"},
                         {20.0, 4.0f, false, "20° med förvalet 4 är avfasad"},
                         {20.0, 10.0f, true, "20° med gränsen 10 är spetsig"}};
    for (const Fall& k : fall) {
        nollställ(s);
        s.spetsgräns     = k.gräns;
        const double ut  = (180.0 - k.vinkel) * kPi / 180.0;   // utriktningen
        const double dbx = std::cos(ut), dby = std::sin(ut);
        s.chunkar = ladda(r, linje({{-150.0, 0.0}, {0.0, 0.0},
                                    {150.0 * dbx, 150.0 * dby}},
                                   konturStil(40.0f, gfx::LineJoin::Miter)));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        // m = (in − ut) / |in − ut| — bort från vinkelns insida.
        const double mx = 1.0 - dbx, my = -dby, ml = std::hypot(mx, my);
        const double t60 = täckVid(b, skärm(s, 60.0 * mx / ml, 60.0 * my / ml));
        const double t2  = täckVid(b, skärm(s, 2.0 * mx / ml, 2.0 * my / ml));
        check((k.spets ? t60 > 0.95 : t60 < 0.05) && t2 > 0.95,
              std::string("H2 ") + k.namn +
                  " (60 px ut längs bisektrisen; 2 px ut alltid täckt)",
              tal(t60) + " / " + tal(t2));
    }
}

// ------------------------------------------------------------------ H3
// FASEN ÄR PLATT: i den räta vinkeln (40 px) går fasens linje x − y = 20 —
// på skärmen (y nedåt) en diagonal där X + Y är konstant. Pixlarna längs
// den har ALLA samma tecknade avstånd till linjen och därmed samma
// täckning; 2,8 px utanför ingen, 2,8 px innanför full. Den spetsiga skarven
// täcker samma pixlar helt.
void fallH3(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    for (gfx::LineJoin join : {gfx::LineJoin::Bevel, gfx::LineJoin::Miter}) {
        nollställ(s);
        s.chunkar = ladda(r, linje({{-150.0, 0.0}, {0.0, 0.0}, {0.0, 150.0}},
                                   konturStil(40.0f, join)));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        // Linjen X + Y = c (pixelcentrum X + ½, Y + ½): välj pixeln nära
        // fasens mitt (10, −10) vars centrum ligger närmast linjen, så att
        // täckningen är delvis (|avstånd| ≤ 0,36 px).
        const Px     a  = skärm(s, 20, 0), z = skärm(s, 0, -20);
        const double c  = a.x + a.y;   // X + Y på linjen (båda ändarna)
        const Px     mp = skärm(s, 10, -10);
        int          X  = static_cast<int>(std::floor(mp.x));
        const int    Y  = static_cast<int>(std::floor(mp.y));
        int          bästX = X;
        for (int dx = -1; dx <= 1; ++dx)
            if (std::abs(X + dx + Y + 1 - c) < std::abs(bästX + Y + 1 - c))
                bästX = X + dx;
        X = bästX;
        double lo = 2.0, hi = -1.0;
        for (int k = -5; k <= 5; ++k) {
            const double t = b.täck(X + k, Y - k);
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
        double ute = 0.0, inne = 2.0;
        for (int k = -5; k <= 5; ++k) {
            ute  = std::max(ute, b.täck(X + k + 2, Y - k + 2));
            inne = std::min(inne, b.täck(X + k - 2, Y - k - 2));
        }
        if (join == gfx::LineJoin::Bevel) {
            check(hi - lo < 0.06 && lo > 0.1 && hi < 0.9,
                  "H3 fasen: lika DELVIS täckning längs fasens linje (platt)",
                  tal(lo) + " – " + tal(hi) + " (linjen " + tal(c) + ", " +
                      tal(z.x + z.y) + ")");
            check(ute < 0.05 && inne > 0.95,
                  "H3 fasen: 2,8 px utanför tom, 2,8 px innanför full",
                  tal(ute) + " / " + tal(inne));
        } else {
            check(lo > 0.95 && ute > 0.95,
                  "H3 den spetsiga täcker fasens linje och bortom den",
                  tal(lo) + " / " + tal(ute));
        }
    }
}

// ------------------------------------------------------------------ H4
// ÄNDARNA på en 200 px lång, 20 px bred vågrät linje: längs mittlinjen
// täcker den rakt avslutade 200 px och den fyrkantiga 220 (halva bredden i
// varje ände); på en rad 8 px från mittlinjen den fyrkantiga 220 och den
// runda 200 + 2 · √(10² − dy²) (skivans korda).
void fallH4(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    for (gfx::LineCap cap :
         {gfx::LineCap::Butt, gfx::LineCap::Square, gfx::LineCap::Round}) {
        nollställ(s);
        s.chunkar = ladda(r, linje({{-100.0, 0.3}, {100.0, 0.3}},
                                   konturStil(20.0f, gfx::LineJoin::Round, cap)));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const Px     mitt = skärm(s, 0.0, 0.3);
        const int    yc   = static_cast<int>(std::floor(mitt.y));
        const int    y8   = yc - 8;
        const int    x0   = static_cast<int>(mitt.x) - 130;
        const int    x1   = static_cast<int>(mitt.x) + 130;
        const double sc   = rad(b, yc, x0, x1);
        const double s8   = rad(b, y8, x0, x1);
        const double dy   = std::abs(y8 + 0.5 - mitt.y);
        const double korda = 200.0 + 2.0 * std::sqrt(std::max(0.0, 100.0 - dy * dy));
        if (cap == gfx::LineCap::Butt)
            check(std::abs(sc - 200.0) < 0.6,
                  "H4 rak ände: 200 px längs mittlinjen", tal(sc));
        else if (cap == gfx::LineCap::Square)
            check(std::abs(sc - 220.0) < 0.6 && std::abs(s8 - 220.0) < 0.6,
                  "H4 fyrkantig ände: 220 px, också 8 px från mittlinjen",
                  tal(sc) + " / " + tal(s8));
        else
            check(std::abs(sc - 220.0) < 1.0 && std::abs(s8 - korda) < 1.0,
                  "H4 rund ände: 220 px längs mittlinjen, skivans korda 8 px "
                  "därifrån",
                  tal(sc) + " / " + tal(s8) + " mot " + tal(korda));
    }
}

// ------------------------------------------------------------------ H5
// STRECKEN {10, 5} px längs en vågrät linje från x = −150 till 150 (6 px
// bred): varje pixels täckning på mittlinjens rad är dess överlapp med
// mönstrets streck räknade från linjens början — vid zoom ×1 och ×4 (där
// linjens början ligger utanför bilden; mönstret står still i skärmen), med
// förskjutningen 5 px, och i världsenheter vid zoom ×2 (20/10 px).
void fallH5(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    struct Fall {
        double      zoom, på, av, förskj;
        bool        värld;
        const char* namn;
    };
    const Fall fall[] = {{1.0, 10, 5, 0, false, "{10, 5} px vid zoom ×1"},
                         {4.0, 10, 5, 0, false, "{10, 5} px vid zoom ×4"},
                         {1.0, 10, 5, 5, false, "förskjutet 5 px"},
                         {2.0, 10, 5, 0, true, "{10, 5} världsenheter vid ×2"}};
    for (const Fall& k : fall) {
        nollställ(s);
        s.vy.pxPerUnit = k.zoom;
        const float mönster[] = {static_cast<float>(k.på),
                                 static_cast<float>(k.av)};
        gfx::makeDash(mönster, static_cast<float>(k.förskj), s.streck);
        // Mönsterenheter per världsenhet: px mäts på skärmen (fysiska px per
        // världsenhet), världsenheter är världens.
        s.streck.perWorldUnit = k.värld ? 1.0f : static_cast<float>(k.zoom);
        s.chunkar = ladda(r, linje({{-150.0, 0.3}, {150.0, 0.3}}, 6.0f));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const Px     a = skärm(s, -150.0, 0.3), e = skärm(s, 150.0, 0.3);
        const double sk = k.värld ? k.zoom : 1.0;   // mönstrets px per enhet
        const auto   iv = streckIntervall(a.x, e.x, k.på * sk, k.av * sk,
                                          k.förskj * sk);
        const Yta    y  = ytan(s.box);
        const int    rd = static_cast<int>(std::floor(a.y));
        const Avvik  av = jämförRad(b, rd, std::max(y.x0, static_cast<int>(a.x) - 3),
                                    std::min(y.x1, static_cast<int>(e.x) + 3),
                                    iv, 0.2);
        check(av.antal == 0,
              "H5 " + std::string(k.namn) +
                  ": varje pixel = överlappet med mönstrets streck",
              avvik(av));
    }
}

// ------------------------------------------------------------------ H6
// STRECKENS ÄNDFORM: {0, 18} med runda ändar och bredden 8 är prickar — en
// skiva med radien 4 var 18:e px (arean π · 4²) — och med fyrkantiga ändar
// kvadrater 8 × 8. Mäts på prickarna i linjens mitt.
void fallH6(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    constexpr double kPi = 3.14159265358979323846;
    for (gfx::LineCap cap : {gfx::LineCap::Round, gfx::LineCap::Square}) {
        nollställ(s);
        const float mönster[] = {0.0f, 18.0f};
        gfx::makeDash(mönster, 0.0f, s.streck);
        s.streck.perWorldUnit = 1.0f;
        s.chunkar = ladda(r, linje({{-150.0, 0.3}, {150.0, 0.3}},
                                   konturStil(8.0f, gfx::LineJoin::Round, cap)));
        const Bild b = bild(f);
        if (g_ritfel)
            return;
        const Px     a     = skärm(s, -150.0, 0.3);
        const double väntad = cap == gfx::LineCap::Round ? kPi * 16.0 : 64.0;
        double       lo = 1e9, hi = -1.0;
        for (int p = 4; p <= 12; ++p) {
            const int cx = static_cast<int>(std::floor(a.x + 18.0 * p));
            const int cy = static_cast<int>(std::floor(a.y));
            double    sum = 0.0;
            for (int yy = cy - 8; yy <= cy + 8; ++yy)
                for (int xx = cx - 8; xx <= cx + 8; ++xx)
                    sum += b.täck(xx, yy);
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
        }
        check(lo > väntad * 0.92 && hi < väntad * 1.08,
              cap == gfx::LineCap::Round
                  ? "H6 runda streckändar: prickar med arean π · 4²"
                  : "H6 fyrkantiga streckändar: kvadrater 8 × 8",
              tal(lo) + " – " + tal(hi) + " mot " + tal(väntad));
    }
}

// ------------------------------------------------------------------ H8
// STRECKEN UNDER TILT 45°: en linje rakt in i bilden (norrut) med {10, 5}
// i kamerans skala i mitten. Mönstret ligger FAST LÄNGS LINJEN i
// världsenheter — strecket k är världens [−150 + 15k, −150 + 15k + 10] — så
// på skärmen hamnar varje streck där kamerans `project` lägger dess ändar:
// längre nära, kortare långt bort (förkortningen med djupet,
// en skriven begränsning). Mäts där perioden på skärmen är minst 6 px.
void fallH8(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    s.vy.pitchDeg = 45.0;
    const float mönster[] = {10.0f, 5.0f};
    gfx::makeDash(mönster, 0.0f, s.streck);
    s.streck.perWorldUnit = 1.0f;   // kamerans skala i mitten är 1 px/enhet
    s.chunkar = ladda(r, linje({{0.3, -150.0}, {0.3, 3000.0}}, 4.0f));
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    std::vector<std::pair<double, double>> iv;
    for (int k = 0; -150.0 + 15.0 * k < 3000.0; ++k) {
        const Px lo = skärm(s, 0.3, -150.0 + 15.0 * k);
        const Px hi = skärm(s, 0.3, -150.0 + 15.0 * k + 10.0);
        iv.emplace_back(std::min(lo.y, hi.y), std::max(lo.y, hi.y));
    }
    const Yta y  = ytan(s.box);
    const int kx = static_cast<int>(std::floor(skärm(s, 0.3, 0.0).x));
    int       mätta = 0, fel = 0;
    double    störst = 0.0;
    for (int yy = y.y0; yy < y.y1; ++yy) {
        // Världens y på raden — och skärmens period där.
        double wx = 0.0, wy = 0.0;
        if (!gfx::unproject(s.vy, kx + 0.5, yy + 0.5, wx, wy) || wy < -150.0)
            continue;
        const double per = std::abs(skärm(s, 0.3, wy + 7.5).y -
                                    skärm(s, 0.3, wy - 7.5).y);
        if (per < 6.0)
            continue;
        ++mätta;
        const double d = std::abs(b.täck(kx, yy) - överlapp(yy, iv));
        störst = std::max(störst, d);
        fel += d > 0.25 ? 1 : 0;
    }
    check(mätta > 60 && fel == 0,
          "H8 under tilt: varje streck där kameran projicerar det (längre nära, "
          "kortare bort)",
          std::to_string(mätta) + " rader, " + std::to_string(fel) +
              " avviker, störst " + tal(störst));
}

// ------------------------------------------------------------------ H7
// STRECKEN I MM genom ritytan (`Stroke::dash` + `dashUnit`): {2, 1} mm vid
// en injicerad kalibrering på 4 px/mm är 8/4 fysiska px — och samma efter
// en zoomändring (mönstret mäts på skärmen).
void fallH7()
{
    Rit r;
    if (!r.öppna())
        return;
    const std::string id = r.f.display().id;
    check(!id.empty(), "H7 skärmen har ett id — kalibreringens nyckel");
    if (id.empty())
        return;
    glaze::app().setDisplayCalibration(id, 4.0f);
    glaze::Canvas& c = r.f.yta;
    c.addShape(glaze::Shape::polyline(
        {{20, 100}, {440, 100}},
        glaze::Stroke{.paint    = kRöd,
                      .width    = glaze::px(6.0f / r.skala),
                      .dash     = {2.0f, 1.0f},
                      .dashUnit = glaze::Unit::Mm}));
    for (double zoom : {0.0, 2.0}) {
        if (zoom > 0.0)
            c.setZoom(zoom);
        const Bild b = r.bild();
        if (g_ritfel)
            break;
        const auto la = c.toLocal({20, 100});
        const auto le = c.toLocal({440, 100});
        if (!la || !le) {
            check(false, "H7 linjens ändar projiceras");
            break;
        }
        const double xs = r.box.x + la->x * r.skala;
        const double xe = r.box.x + le->x * r.skala;
        const int    rd = static_cast<int>(std::floor(r.box.y + la->y * r.skala));
        const Yta    u  = r.inre();
        const Avvik  av = jämförRad(b, rd, std::max(u.x0, static_cast<int>(xs) - 3),
                                    std::min(u.x1, static_cast<int>(xe) + 3),
                                    streckIntervall(xs, xe, 8.0, 4.0, 0.0), 0.2);
        check(av.antal == 0,
              std::string("H7 {2, 1} mm = 8/4 px vid 4 px/mm") +
                  (zoom > 0.0 ? ", också vid zoom ×2" : ""),
              avvik(av));
    }
    glaze::app().setDisplayCalibration(id, 0.0f);
}

// ------------------------------------------------------------------ H9
// BILDMARKÖREN (MarkerStyle::img) i Glyph-programmet: en 4 × 4-bild, röd
// överst och blå nederst, som markör 20 px med ankaret Bottom i ytans mitt —
// röd 15 px ovanför punkten, blå 5 px ovanför, vitt under punkten och utanför
// rutan. Under bäring 30° och tilt 45° står den UPPRÄTT med samma mått. En
// 8 × 4-bild (vänster röd, höger blå) med ankaret Center blir 20 × 10 px. Ett
// render-mål (GL:s spegling, premultiplicerat innehåll) med halvgenomskinligt
// rött överst och blått nederst står rättvänt, och dess röda är RAKT
// (R 255 över vitt — premultiplicerat hade gett 191).
void fallH9(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const gfx::Color  kBlå{0, 0, 255, 255};

    // 4 × 4: rad 0–1 röd, rad 2–3 blå. 8 × 4: kolumn 0–3 röd, 4–7 blå.
    std::vector<std::uint8_t> px44(4 * 4 * 4), px84(8 * 4 * 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const gfx::Color c = y < 2 ? kRöd : kBlå;
            std::memcpy(&px44[(y * 4 + x) * 4], &c, 4);
        }
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) {
            const gfx::Color c = x < 4 ? kRöd : kBlå;
            std::memcpy(&px84[(y * 8 + x) * 4], &c, 4);
        }
    auto bild44 = r.createImage(4, 4, gfx::ImageFormat::Rgba8, px44.data(), 0);
    auto bild84 = r.createImage(8, 4, gfx::ImageFormat::Rgba8, px84.data(), 0);

    const auto markör = [&](gfx::TextAnchor ankare) {
        gfx::MarkerLayoutStyle m;
        m.size   = 20.0f;
        m.anchor = ankare;
        m.image  = true;
        std::vector<gfx::GlyphChunk> ch;
        const gfx::Point2d p[] = {{0.0, 0.0}};
        gfx::buildMarkers(p, m, ch);
        return laddaGlyfer(r, ch);
    };
    // Markörerna med bilden bunden — efter `före` (ett mål att måla först).
    const auto ritaMarkör = [](gfx::Renderer& rr, const std::vector<GpuGlyfer>& gl,
                               const gfx::Image* img, const gfx::MapView& vy) {
        for (const GpuGlyfer& g : gl) {
            gfx::MeshDraw d;
            d.program    = gfx::MeshProgram::Glyph;
            d.vertices   = gfx::slice(*g.vb);
            d.indices    = gfx::slice(*g.ib);
            d.indexCount = g.count;
            d.image      = img;
            gfx::viewProjection(vy, g.ox, g.oy).toFloat(d.uniforms.viewProj);
            d.uniforms.pxPerWidthUnit = 1.0f;
            rr.drawMesh(d);
        }
    };
    const auto rita = [&](const std::vector<GpuGlyfer>& gl, const gfx::Image* img,
                          const gfx::MapView& vy) {
        s.egenRitning = [&gl, img, vy, &ritaMarkör](gfx::Renderer& rr) {
            ritaMarkör(rr, gl, img, vy);
        };
        return bild(f);
    };

    // Upprätt i en vy utan kamera, och under bäring + tilt (y uppåt).
    const auto nål = markör(gfx::TextAnchor::Bottom);
    for (int tilt = 0; tilt < 2; ++tilt) {
        gfx::MapView vy = pxVy(s.box);
        if (tilt) {
            vy.yUp        = true;
            vy.bearingDeg = 30.0;
            vy.pitchDeg   = 45.0;
        }
        const Bild b = rita(nål, bild44.get(), vy);
        double px0 = 0.0, py0 = 0.0;
        gfx::project(vy, 0.0, 0.0, px0, py0);
        const int x = static_cast<int>(std::floor(px0));
        const int y = static_cast<int>(std::floor(py0));
        check(ärFärg(b.at(x, y - 15), kRöd, 3) && ärFärg(b.at(x, y - 5), kBlå, 3) &&
                  ärFärg(b.at(x, y + 3), kVit, 3) &&
                  ärFärg(b.at(x, y - 24), kVit, 3) &&
                  ärFärg(b.at(x - 13, y - 10), kVit, 3) &&
                  ärFärg(b.at(x + 13, y - 10), kVit, 3),
              std::string("H9 bildmarkören 20 px, ankaret Bottom: röd över blå, "
                          "PÅ punkten") +
                  (tilt ? " — upprätt under bäring 30° och tilt 45°" : ""),
              px(b.at(x, y - 15)) + " " + px(b.at(x, y - 5)) + " " +
                  px(b.at(x, y + 3)) + " " + px(b.at(x - 13, y - 10)));
    }

    // PROPORTIONERNA: 8 × 4 i rutan 20 px ⇒ 20 × 10, kring mitten.
    {
        const gfx::MapView vy     = pxVy(s.box);
        const auto         mitten = markör(gfx::TextAnchor::Center);
        const Bild b = rita(mitten, bild84.get(), vy);
        const Yta  u = ytan(s.box);
        const int  x = static_cast<int>(u.cx), y = static_cast<int>(u.cy);
        check(ärFärg(b.at(x - 6, y), kRöd, 3) && ärFärg(b.at(x + 6, y), kBlå, 3) &&
                  ärFärg(b.at(x, y - 8), kVit, 3) && ärFärg(b.at(x, y + 8), kVit, 3) &&
                  ärFärg(b.at(x - 13, y), kVit, 3) && ärFärg(b.at(x + 13, y), kVit, 3),
              "H9 en 8 × 4-bild blir 20 × 10 px (bildens proportioner)",
              px(b.at(x - 6, y)) + " " + px(b.at(x + 6, y)) + " " +
                  px(b.at(x, y - 8)));
    }

    // ETT RENDER-MÅL: speglat på GL, premultiplicerat överallt — målat i
    // framen, före markören.
    {
        auto               mål = r.createRenderTarget(4, 4);
        const gfx::MapView vy  = pxVy(s.box);
        s.egenRitning = [&](gfx::Renderer& rr) {
            rr.pushTarget(*mål, gfx::Color{0, 0, 0, 0});
            rr.fillRect(gfx::Rectf{0, 0, 4, 2}, gfx::Color{255, 0, 0, 128});
            rr.fillRect(gfx::Rectf{0, 2, 4, 2}, kBlå);
            rr.popTarget();
            ritaMarkör(rr, nål, &mål->image(), vy);
        };
        const Bild b = bild(f);
        const Yta  u = ytan(s.box);
        const int  x = static_cast<int>(u.cx), y = static_cast<int>(u.cy);
        const std::uint8_t* över = b.at(x, y - 15);
        check(över[0] > 245 && över[1] > 110 && över[1] < 145 &&
                  ärFärg(b.at(x, y - 5), kBlå, 3),
              "H9 ett render-mål som markör: rättvänt, och det halvgenomskinliga "
              "röda RAKT (R 255, G ≈ 127 över vitt)",
              px(över) + " / " + px(b.at(x, y - 5)));
    }
}

// ------------------------------------------------------------------ H10
// BILDMARKÖREN GENOM RITYTAN: en `Bitmap` (röd över blå) som markör 5 mm vid
// en injicerad kalibrering på 4 px/mm är 20 fysiska px, med spetsen PÅ punkten
// (ankaret Bottom) — lagrets körning binder bilden. En markör med en OGILTIG
// bild ritar ingenting (ingen cirkel i dess ställe).
void fallH10()
{
    Rit r;
    if (!r.öppna())
        return;
    const std::string id = r.f.display().id;
    check(!id.empty(), "H10 skärmen har ett id — kalibreringens nyckel");
    if (id.empty())
        return;
    glaze::app().setDisplayCalibration(id, 4.0f);
    const gfx::Color kBlå{0, 0, 255, 255};
    std::vector<std::uint8_t> rgba(4 * 4 * 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const gfx::Color c = y < 2 ? kRöd : kBlå;
            std::memcpy(&rgba[(y * 4 + x) * 4], &c, 4);
        }
    glaze::Bitmap nål(4, 4, rgba.data(), 0);
    glaze::Canvas& c = r.f.yta;
    c.addShape(glaze::Shape::marker(
        {120, 120}, glaze::MarkerStyle{.size   = glaze::mm(5.0f),
                                       .anchor = glaze::TextAnchor::Bottom,
                                       .img    = nål}));
    c.addShape(glaze::Shape::marker(
        {300, 120}, glaze::MarkerStyle{.size = glaze::mm(5.0f),
                                       .img  = glaze::AssetHandle{}}));
    r.bild();   // bildcachen laddar upp bilden i den första ritningen
    const Bild b = r.bild();
    if (!g_ritfel) {
        const int x = r.x(120), y = static_cast<int>(r.box.y + 120.0 * r.skala);
        check(ärFärg(b.at(x, y - 15), kRöd, 3) && ärFärg(b.at(x, y - 5), kBlå, 3) &&
                  ärFärg(b.at(x, y + 3), kVit, 3) && ärFärg(b.at(x, y - 24), kVit, 3),
              "H10 en Bitmap som markör 5 mm vid 4 px/mm: 20 px, spetsen på "
              "punkten",
              px(b.at(x, y - 15)) + " " + px(b.at(x, y - 5)) + " " +
                  px(b.at(x, y - 24)));
        const int xo = r.x(300), yo = r.y(120);
        bool      tom = true;
        for (int dy = -12; dy <= 12; ++dy)
            for (int dx = -12; dx <= 12; ++dx)
                tom = tom && ärFärg(b.at(xo + dx, yo + dy), kVit, 3);
        check(tom, "H10 en markör med en ogiltig bild ritar ingenting");
    }
    glaze::app().setDisplayCalibration(id, 0.0f);
}

// ===================================================== BILD PÅ FYRA HÖRN, TEXTDELAR, GLYFRESERV
//
// BILDEN PÅ FYRA FRIA HÖRN (B8–B11, genom en riktig glaze::Canvas), TEXTDELARNA
// (D8–D9, Glyph-programmet med quads ur `buildText`) och GLYFRESERVEN (R, den
// riktiga backendens UI-font). Referensen är grindens EGEN homografi (DLT) och
// kamerans `toLocal`, fonternas egna `measure`, och färgerna.

// En HOMOGRAFI mellan fyra punktpar — DLT med åtta ekvationer (h33 = 1),
// löst med Gauss–Jordan. `till` avbildar en punkt.
struct Homografi {
    double g[9] = {};
    bool   ok   = false;

    void till(double x, double y, double& ux, double& uy) const
    {
        const double w = g[6] * x + g[7] * y + g[8];
        ux             = (g[0] * x + g[1] * y + g[2]) / w;
        uy             = (g[3] * x + g[4] * y + g[5]) / w;
    }
};

Homografi homografi(const double (&från)[4][2], const double (&mot)[4][2])
{
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = från[i][0], y = från[i][1];
        const double u = mot[i][0], v = mot[i][1];
        double*      r0 = a[2 * i];
        double*      r1 = a[2 * i + 1];
        r0[0] = x;
        r0[1] = y;
        r0[2] = 1.0;
        r0[6] = -u * x;
        r0[7] = -u * y;
        r0[8] = u;
        r1[3] = x;
        r1[4] = y;
        r1[5] = 1.0;
        r1[6] = -v * x;
        r1[7] = -v * y;
        r1[8] = v;
    }
    for (int c = 0; c < 8; ++c) {
        int p = c;
        for (int rr = c + 1; rr < 8; ++rr)
            if (std::abs(a[rr][c]) > std::abs(a[p][c]))
                p = rr;
        if (std::abs(a[p][c]) < 1e-14)
            return Homografi{};
        if (p != c)
            for (int k = 0; k < 9; ++k)
                std::swap(a[p][k], a[c][k]);
        for (int rr = 0; rr < 8; ++rr) {
            if (rr == c)
                continue;
            const double fk = a[rr][c] / a[c][c];
            for (int k = c; k < 9; ++k)
                a[rr][k] -= fk * a[c][k];
        }
    }
    Homografi h;
    for (int i = 0; i < 8; ++i)
        h.g[i] = a[i][8] / a[i][i];
    h.g[8] = 1.0;
    h.ok   = true;
    return h;
}

// Bildens hörn i ringordning: (0, 0), (1, 0), (1, 1), (0, 1).
const double kBildHörn[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

// EN BILD MED STRECK (B8–B10): vit 64 × 64 med ett lodrätt och ett vågrätt
// svart streck på 2 texlar mitt i (kolumn och rad 31–32: u = v = 0,5),
// Nearest — strecken är skarpa var de än hamnar.
glaze::Bitmap streckBild()
{
    constexpr int             N = 64;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(N) * N * 4, 255);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            if (x == 31 || x == 32 || y == 31 || y == 32) {
                std::uint8_t* p = &px[(static_cast<std::size_t>(y) * N + x) * 4];
                p[0] = p[1] = p[2] = 0;
            }
    glaze::Bitmap b(N, N, px.data(), 0);
    b.setFilter(gfx::ImageFilter::Nearest);
    return b;
}

// Mörkrets (255 − G) tyngdpunkt längs en rad [x0, x1) eller en kolumn
// [y0, y1), i pixelcentrumkoordinater — −1 utan mörker.
double mörkRad(const Bild& b, int y, int x0, int x1)
{
    double s = 0.0, m = 0.0;
    for (int x = x0; x < x1; ++x) {
        const double w = 255.0 - b.at(x, y)[1];
        s += w;
        m += w * (x + 0.5);
    }
    return s > 255.0 ? m / s : -1.0;
}
double mörkKolumn(const Bild& b, int x, int y0, int y1)
{
    double s = 0.0, m = 0.0;
    for (int y = y0; y < y1; ++y) {
        const double w = 255.0 - b.at(x, y)[1];
        s += w;
        m += w * (y + 0.5);
    }
    return s > 255.0 ? m / s : -1.0;
}

// Strecket genom skärmpunkten (sx, sy) med skärmriktningen (dx, dy), mätt
// TVÄRS: längs pixelraden för ett brant streck, längs kolumnen för ett
// flackt, med referensen flyttad till radens (kolumnens) mitt längs strecket.
// Svaret är mörkrets avvikelse från strecket i px (1e9 utan mörker).
double streckAvvik(const Bild& b, double sx, double sy, double dx, double dy)
{
    if (std::abs(dx) <= std::abs(dy)) {
        const int    y  = static_cast<int>(std::floor(sy));
        const double fx = sx + (y + 0.5 - sy) * dx / dy;
        const int    x  = static_cast<int>(std::floor(fx));
        const double c  = mörkRad(b, y, x - 25, x + 26);
        return c < 0.0 ? 1e9 : c - fx;
    }
    const int    x  = static_cast<int>(std::floor(sx));
    const double fy = sy + (x + 0.5 - sx) * dy / dx;
    const int    y  = static_cast<int>(std::floor(fy));
    const double c  = mörkKolumn(b, x, y - 25, y + 26);
    return c < 0.0 ? 1e9 : c - fy;
}

// B8/B9 — STRECKEN på fyrhörningen där grindens homografi lägger dem: nio
// punkter längs u = 0,5 (bort från v = 0,5) och nio längs v = 0,5 (bort
// från u = 0,5), projicerade med ritytans `toLocal`, mätta tvärs strecket.
// Svaret är den största avvikelsen i px.
double streckPåFyrhörning(Rit& r, const double (&ring)[4][2])
{
    glaze::Canvas&  c = r.f.yta;
    const Homografi h = homografi(kBildHörn, ring);   // bilden → världen
    const auto skärm = [&](double u, double v, double& sx, double& sy) {
        double wx = 0.0, wy = 0.0;
        h.till(u, v, wx, wy);
        const std::optional<glaze::Point> l = c.toLocal(glaze::Point{wx, wy});
        if (!l)
            return false;
        sx = r.box.x + l->x * r.skala;
        sy = r.box.y + l->y * r.skala;
        return true;
    };
    const Bild b = r.bild();
    if (g_ritfel || !h.ok)
        return 1e9;
    const double led[] = {0.08, 0.16, 0.24, 0.32, 0.40, 0.62, 0.72, 0.82, 0.92};
    double       värst = 0.0;
    for (int lodrätt = 0; lodrätt < 2; ++lodrätt)
        for (double t : led) {
            const double u = lodrätt ? 0.5 : t, v = lodrätt ? t : 0.5;
            const double du = lodrätt ? 0.0 : 0.01, dv = lodrätt ? 0.01 : 0.0;
            double sx = 0, sy = 0, ax = 0, ay = 0, bx = 0, by = 0;
            if (!skärm(u, v, sx, sy) || !skärm(u - du, v - dv, ax, ay) ||
                !skärm(u + du, v + dv, bx, by))
                return 1e9;
            värst = std::max(värst, std::abs(streckAvvik(b, sx, sy, bx - ax, by - ay)));
        }
    return värst;
}

// ------------------------------------------------------------------ B8
// En trapets rakt uppifrån: strecken ligger där homografin säger, ±1 px. En
// AFFIN uv per triangel hade lagt det lodräta strecket upp till ~45 px fel
// vid diagonalen och det vågräta ~40 px fel.
void fallB8()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& c = r.f.yta;
    c.setCenter(0.0, 0.0);
    const double ring[4][2] = {{-150, -100}, {150, -100}, {60, 100}, {-60, 100}};
    c.addShape(glaze::Shape::image({ring[0][0], ring[0][1]}, {ring[1][0], ring[1][1]},
                                   {ring[2][0], ring[2][1]}, {ring[3][0], ring[3][1]},
                                   streckBild()));
    r.bild();   // bildcachen laddar upp bilden i den första ritningen
    const double värst = streckPåFyrhörning(r, ring);
    check(värst <= 1.0,
          "B8 en bild på en trapets rakt uppifrån: strecken u = v = 0,5 där "
          "grindens homografi lägger dem, ±1 px på nio punkter var",
          tal(värst) + " px");
}

// ------------------------------------------------------------------ B9
// Samma under bäring 30° och tilt 45° (en allmän konvex fyrhörning):
// perspektivet och homografin tillsammans, ±1,5 px.
void fallB9()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& c = r.f.yta;
    c.setCenter(0.0, 0.0);
    c.setBearing(30.0f);
    c.setPitch(45.0f);
    const double ring[4][2] = {{-140, -90}, {130, -110}, {70, 95}, {-90, 80}};
    c.addShape(glaze::Shape::image({ring[0][0], ring[0][1]}, {ring[1][0], ring[1][1]},
                                   {ring[2][0], ring[2][1]}, {ring[3][0], ring[3][1]},
                                   streckBild()));
    r.bild();
    const double värst = streckPåFyrhörning(r, ring);
    check(värst <= 1.5,
          "B9 under bäring 30° och tilt 45°: strecken där homografin och "
          "kameran lägger dem, ±1,5 px",
          tal(värst) + " px");
}

// ------------------------------------------------------------------ B10
// `image(Rect)` och `image` med rektangelns fyra hörn ger IDENTISKA pixlar —
// en rektangel får vikterna 1 och samma hörn i samma ordning.
void fallB10()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas&      c    = r.f.yta;
    const glaze::Bitmap bild = streckBild();
    c.setCenter(0.0, 0.0);
    c.setPitch(30.0f);   // under tilt — vikterna 1 ska rita exakt som rektangeln
    c.addShape(glaze::Shape::image(glaze::Rect{-120, -80, 240, 160}, bild));
    r.bild();
    const Bild a = r.bild();
    c.setShape(0, glaze::Shape::image({-120, -80}, {120, -80}, {120, 80},
                                      {-120, 80}, bild));
    r.bild();
    const Bild q = r.bild();
    if (g_ritfel)
        return;
    int skilda = 0;
    for (std::size_t i = 0; i < a.px.size() && i < q.px.size(); ++i)
        skilda += a.px[i] != q.px[i] ? 1 : 0;
    check(a.px.size() == q.px.size() && skilda == 0,
          "B10 image(Rect) och image med rektangelns fyra hörn: identiska "
          "pixlar (under tilt 30°)",
          std::to_string(skilda) + " byte skiljer");
}

// ------------------------------------------------------------------ B11
// RÄTTVÄND på fyrhörningen: övre vänstra fjärdedelen röd, resten blå — i en
// RenderTexture (premultiplicerad, och på GL speglad: speglingen ska ske
// FÖRE multiplikationen med q) och i en Bitmap. Nära hörnet ÖV är det rött,
// nära NH blått.
void fallB11()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas&   c = r.f.yta;
    const gfx::Color kBlå{0, 0, 255, 255};
    c.setCenter(0.0, 0.0);
    glaze::RenderTexture rt(32, 32);
    rt.setFilter(gfx::ImageFilter::Nearest);
    rt.paint([kBlå](const glaze::Ctx& k) {
        const gfx::Rectf b = k.bounds();
        glaze::Draw::rect(k, b, 0.0f, kBlå);
        glaze::Draw::rect(k, gfx::Rectf{b.x, b.y, b.w / 2.0f, b.h / 2.0f}, 0.0f,
                          kRöd);
    });
    std::vector<std::uint8_t> rgba(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const gfx::Color f = x < 16 && y < 16 ? kRöd : kBlå;
            std::memcpy(&rgba[(static_cast<std::size_t>(y) * 32 + x) * 4], &f, 4);
        }
    glaze::Bitmap bmp(32, 32, rgba.data(), 0);
    bmp.setFilter(gfx::ImageFilter::Nearest);
    const double vä[4][2] = {{-200, -100}, {-40, -80}, {-60, 90}, {-180, 100}};
    const double hö[4][2] = {{40, -100}, {200, -80}, {180, 100}, {60, 90}};
    c.addShape(glaze::Shape::image({vä[0][0], vä[0][1]}, {vä[1][0], vä[1][1]},
                                   {vä[2][0], vä[2][1]}, {vä[3][0], vä[3][1]}, rt));
    c.addShape(glaze::Shape::image({hö[0][0], hö[0][1]}, {hö[1][0], hö[1][1]},
                                   {hö[2][0], hö[2][1]}, {hö[3][0], hö[3][1]}, bmp));
    r.bild();
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const auto pixel = [&](const double (&ring)[4][2], double u, double v) {
        double wx = 0, wy = 0;
        homografi(kBildHörn, ring).till(u, v, wx, wy);
        const std::optional<glaze::Point> l = c.toLocal(glaze::Point{wx, wy});
        return l ? b.at(static_cast<int>(r.box.x + l->x * r.skala),
                        static_cast<int>(r.box.y + l->y * r.skala))
                 : b.at(-1, -1);
    };
    // Ett RUTNÄT 4 × 4 av (u, v) i cellernas mitt (aldrig på gränsen 0,5),
    // var och en mot grindens egen homografi: rött bara där u < 0,5 OCH
    // v < 0,5. Två hörn räcker inte — en spegling EFTER
    // multiplikationen med q, `(1 − v·q)/q`, hamnar fortfarande i rätt halva
    // vid ÖV och NH; i rutnätet byter den färg på flera punkter.
    const auto rättvänd = [&](const double (&ring)[4][2], std::string& fel) {
        bool ok = true;
        for (int iv = 0; iv < 4; ++iv)
            for (int iu = 0; iu < 4; ++iu) {
                const double u = 0.125 + 0.25 * iu, v = 0.125 + 0.25 * iv;
                const gfx::Color väntad = u < 0.5 && v < 0.5 ? kRöd : kBlå;
                const std::uint8_t* p = pixel(ring, u, v);
                if (!ärFärg(p, väntad, 3)) {
                    ok = false;
                    fel += "(" + std::to_string(iu) + "," + std::to_string(iv) +
                           ") " + px(p) + " ";
                }
            }
        return ok;
    };
    std::string rtFel, bmpFel;
    const bool  rtOk  = rättvänd(vä, rtFel);
    const bool  bmpOk = rättvänd(hö, bmpFel);
    check(rtOk,
          "B11 en RenderTexture på fyrhörningen: rättvänd i alla 16 punkter "
          "(rött bara i övre vänstra fjärdedelen)",
          rtFel);
    check(bmpOk,
          "B11 en Bitmap på fyrhörningen: rättvänd i alla 16 punkter", bmpFel);
}

// ------------------------------------------------------------------ B13
//
// RITYTANS BILDLAGER BYGGT OM: 40 bilder om 8 × 8 i egna färger
// som `Shape::image`, därefter `setShapes` med 40 NYA. Framen efter bryter inget
// pass — de nya chunkarna och bilderna har aldrig ritats (Metal: direkt) —
// skapar två buffertar per chunk (en per bild: kontraktet, och D3D11:s antal)
// och ritar varje bild i sin nya färg.
void fallB13()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& c = r.f.yta;
    c.setCenter(0.0, 0.0);
    constexpr int kKolumner = 8, kAntal = 40;
    const auto    ruta = [](int i) {
        return glaze::Rect{-160.0 + 40.0 * (i % kKolumner),
                           -100.0 + 40.0 * (i / kKolumner), 30.0, 30.0};
    };
    // Varje bild en egen färg; den nya omgången i omvänd ordning och en annan
    // blå, så ingen bild får sin gamla färg.
    const auto färg = [](int i, bool ny) {
        const int k = ny ? kAntal - 1 - i : i;
        return gfx::Color{static_cast<std::uint8_t>(20 + 5 * k),
                          static_cast<std::uint8_t>(230 - 5 * k),
                          static_cast<std::uint8_t>(ny ? 40 : 200), 255};
    };
    const auto lager = [&](bool ny) {
        std::vector<glaze::Shape> former;
        for (int i = 0; i < kAntal; ++i) {
            glaze::Bitmap bmp(8, 8);
            bmp.fill(färg(i, ny));
            former.push_back(glaze::Shape::image(ruta(i), bmp));
        }
        return former;
    };
    c.setShapes(lager(false));
    r.bild();   // den första omgången ritad
    if (g_ritfel)
        return;
    gfx::Renderer& rr = r.f.window().renderer();
    c.setShapes(lager(true));
    rr.resetStats();
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const gfx::RenderStats st = rr.stats();
    check(st.uploadBreaks == 0,
          "B13 ett ombyggt bildlager (40 nya bilder): framen efter bryter inget "
          "pass",
          std::to_string(st.uploadBreaks) + " brott");
    check(st.buffersCreated == 2 * kAntal,
          "B13 två buffertar per chunk — en chunk per bild (kontraktet)",
          std::to_string(st.buffersCreated) + " buffertar");
    int         fel = 0;
    std::string första;
    for (int i = 0; i < kAntal; ++i) {
        const glaze::Rect                 q = ruta(i);
        const std::optional<glaze::Point> l =
            c.toLocal(glaze::Point{q.x + q.w / 2.0, q.y + q.h / 2.0});
        const std::uint8_t* p = l ? b.at(r.x(l->x), r.y(l->y)) : b.at(-1, -1);
        if (!ärFärg(p, färg(i, true), 3)) {
            if (!fel)
                första = "bild " + std::to_string(i) + " = " + px(p);
            ++fel;
        }
    }
    check(fel == 0, "B13 varje bild i sin nya färg",
          std::to_string(fel) + " fel" + (fel ? ", första " + första : ""));
}

// ------------------------------------------------------------------ B15
//
// FÄRGJUSTERINGEN GENOM RITYTAN (shape.h `ImageStyle`): två bilder ur
// SAMMA Bitmap i ett lager, den vänstra med mättnaden 0 och den högra orörd,
// ritas var för sig i sina färger — samma bild med olika stil får inte dela
// rit-anrop (körningens nyckel); `setShape` med en ny stil (kontrasten 0) ger
// den nya färgen i nästa ritning UTAN att bilden laddas upp igen (framens
// uppladdade byte under en bilds 64 × 64 × 4 — plattornas vertex och index
// är ~100 byte per chunk); och målarens `drawImage` med en stil ritar som
// lagret.
void fallB15()
{
    Rit r;
    if (!r.öppna())
        return;
    glaze::Canvas& c = r.f.yta;
    c.setCenter(0.0, 0.0);
    constexpr int kSida = 64;
    glaze::Bitmap bmp(kSida, kSida);
    bmp.fill(kTegel);
    const glaze::Rect vä{-120, -40, 80, 80}, hö{40, -40, 80, 80};
    c.setShapes({glaze::Shape::image(vä, bmp, {.saturation = 0}),
                 glaze::Shape::image(hö, bmp)});
    Bild b = r.bild();
    if (g_ritfel)
        return;
    const auto mitt = [&](const glaze::Rect& q) {
        const std::optional<glaze::Point> l =
            c.toLocal(glaze::Point{q.x + q.w / 2.0, q.y + q.h / 2.0});
        return l ? b.at(r.x(l->x), r.y(l->y)) : b.at(-1, -1);
    };
    const gfx::Color grå = justerad(kTegel, 0, 1, 1);
    check(ärFärg(mitt(vä), grå) && ärFärg(mitt(hö), kTegel),
          "B15 samma Bitmap i två stilar i ett lager: den grå och den orörda "
          "ritas var för sig",
          px(mitt(vä)) + " / " + px(mitt(hö)));

    gfx::Renderer& rr = r.f.window().renderer();
    c.setShape(1, glaze::Shape::image(hö, bmp, {.contrast = 0}));
    rr.resetStats();
    b = r.bild();
    if (g_ritfel)
        return;
    const gfx::RenderStats st      = rr.stats();
    const gfx::Color       mittgrå = justerad(kTegel, 1, 0, 1);
    check(ärFärg(mitt(hö), mittgrå) && ärFärg(mitt(vä), grå) &&
              st.bytesUploaded < static_cast<std::uint64_t>(kSida) * kSida * 4,
          "B15 en ny stil med setShape: den nya färgen i nästa ritning, och "
          "bilden laddas inte upp igen",
          px(mitt(hö)) + ", " + std::to_string(st.bytesUploaded) + " byte");

    c.setShapes({});
    c.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.drawImage(vä, bmp, {.brightness = 0.5f});
    };
    b = r.bild();
    if (g_ritfel)
        return;
    const gfx::Color halv = justerad(kTegel, 1, 1, 0.5);
    check(ärFärg(mitt(vä), halv), "B15 målarens drawImage med en ImageStyle",
          px(mitt(vä)) + " mot " + px(&halv.r));
    c.onPaint = nullptr;
}

// ------------------------------------------------------------------ D8/D9
//
// TEXTDELARNA: "HH" vanlig svart + "HH" fet röd i EN etikett, ankaret Left i
// ytans mitt. Gränsen mellan delarna ligger den vanliga fontens `measure`
// till höger om ankaret.

bool ärRöd(const std::uint8_t* p) { return p[0] > 150 && p[1] < 100 && p[2] < 100; }
bool ärSvart(const std::uint8_t* p) { return p[0] < 80 && p[1] < 80 && p[2] < 80; }

void fallD8D9(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta         u = ytan(s.box);

    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
    gfx::TextLayoutStyle st;
    st.size   = 40.0f;
    st.anchor = gfx::TextAnchor::Left;
    const gfx::StyledText delar[] = {{"HH", kSvart, gfx::FontStyle::Regular},
                                     {"HH", kRöd, gfx::FontStyle::Bold}};
    std::vector<gfx::GlyphChunk> ch;
    const gfx::TextBox tb = gfx::buildText(atlas, delar, {0.0, 0.0}, st, ch);
    const auto vanlig = r.loadFont(gfx::FontFamily::UI, st.size, gfx::FontStyle::Regular);
    const auto fet    = r.loadFont(gfx::FontFamily::UI, st.size, gfx::FontStyle::Bold);
    const double wV = vanlig->measure("HH").w, wF = fet->measure("HH").w;
    const double bredd = (tb.x1 - tb.x0) * st.size;
    check(ch.size() == 1,
          "D8 en vanlig och en fet del är EN chunk — ett rit-anrop",
          std::to_string(ch.size()) + " chunkar");
    check(std::abs(bredd - (wV + wF)) <= 0.01 * (wV + wF),
          "D8 lådans bredd = summan av de två fonternas measure (±1 %)",
          tal(bredd) + " mot " + tal(wV + wF));

    const auto         gl = laddaGlyfer(r, ch);
    const gfx::MapView vy = pxVy(s.box);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, gl, vy, &atlas); };
    const Bild b = bild(f);
    if (g_ritfel)
        return;
    const double gräns = u.cx + tb.x0 * st.size + wV;
    int    röda = 0, svarta = 0, rödVänster = 0, svartHöger = 0;
    int    rx0 = 1 << 30, rx1 = -1, sx0 = 1 << 30, sx1 = -1;
    for (int y = u.y0; y < u.y1; ++y)
        for (int x = u.x0; x < u.x1; ++x) {
            const std::uint8_t* p = b.at(x, y);
            if (ärRöd(p)) {
                ++röda;
                rödVänster += x + 0.5 < gräns - 1.0 ? 1 : 0;
                rx0 = std::min(rx0, x);
                rx1 = std::max(rx1, x);
            } else if (ärSvart(p)) {
                ++svarta;
                svartHöger += x + 0.5 > gräns + 1.0 ? 1 : 0;
                sx0 = std::min(sx0, x);
                sx1 = std::max(sx1, x);
            }
        }
    check(röda > 50 && svarta > 50 && rödVänster == 0 && svartHöger == 0,
          "D8 rött bläck bara till höger om delgränsen, svart bara till vänster "
          "— färgen är delens, per vertex",
          std::to_string(röda) + " röda (" + std::to_string(rödVänster) +
              " till vänster), " + std::to_string(svarta) + " svarta (" +
              std::to_string(svartHöger) + " till höger)");
    check(rx1 - rx0 > sx1 - sx0,
          "D8 den feta delen är bredare än den vanliga",
          std::to_string(rx1 - rx0) + " mot " + std::to_string(sx1 - sx0));

    // D9 — en STOR gloria (gul, 8 px): den feta delens gloria får aldrig
    // ritas över den vanliga delens fyllning — den vanliga sista glyfens
    // högra stam (bandet strax vänster om gränsen) är lika svart som utan
    // gloria.
    const auto svartIBand = [&](const Bild& bb) {
        int n = 0;
        for (int y = u.y0; y < u.y1; ++y)
            for (int x = static_cast<int>(gräns - 0.3 * st.size);
                 x < static_cast<int>(gräns); ++x)
                n += ärSvart(bb.at(x, y)) ? 1 : 0;
        return n;
    };
    const int utan = svartIBand(b);
    st.halo      = gfx::Color{255, 220, 0, 255};
    st.haloWidth = 8.0f;
    std::vector<gfx::GlyphChunk> chG;
    gfx::buildText(atlas, delar, {0.0, 0.0}, st, chG);
    const auto glG = laddaGlyfer(r, chG);
    s.egenRitning = [&](gfx::Renderer& rr) { ritaGlyfer(rr, glG, vy, &atlas); };
    const int med = svartIBand(bild(f));
    check(utan > 20 && std::abs(med - utan) <= utan / 10,
          "D9 en stor gloria: den vanliga delens högra stam vid gränsen är "
          "fyllnadsfärg, inte den feta delens gloria",
          std::to_string(utan) + " svarta utan gloria, " + std::to_string(med) +
              " med");
}

// ------------------------------------------------------------- D10 och D12
//
// GOLV OCH TAK I GLYPH-PROGRAMMET och TAKET I TEXTEN.
// D10: "H" i storleken 10 med golvet 20 px och taket 40 px vid tätheten 1, 3
// och 6 (zoomen i en världsenhet) har SAMMA bläckhöjd (±1 px) som "H" utan
// gränser vid em 20, 30 och 40 px — referensen är den oklampade ritningen, inte
// klampningens formel. D12: maxLines 2 på ett stycke om fem rader lämnar
// inget bläck under två radhöjder från blockets överkant.
void fallD10D12(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta         u = ytan(s.box);

    gfx::SdfAtlas        atlas(r, gfx::FontFamily::UI);
    gfx::TextLayoutStyle st;
    st.size               = 10.0f;
    st.color              = kSvart;
    const auto         gl = text(r, atlas, "H", st);
    const gfx::MapView vy = pxVy(s.box);
    float              täthet = 1.0f, golv = 0.0f, tak = 0.0f;
    s.egenRitning = [&](gfx::Renderer& rr) {
        ritaGlyfer(rr, gl, vy, &atlas, täthet, golv, tak);
    };
    const auto höjd = [&](float t, float g, float k) {
        täthet       = t;
        golv         = g;
        tak          = k;
        const Låda l = bläck(bild(f), u);
        return l.tom() ? 0 : l.h();
    };
    const int klampad[3] = {höjd(1.0f, 20.0f, 40.0f), höjd(3.0f, 20.0f, 40.0f),
                            höjd(6.0f, 20.0f, 40.0f)};
    const int fri[3]     = {höjd(2.0f, 0.0f, 0.0f), höjd(3.0f, 0.0f, 0.0f),
                            höjd(4.0f, 0.0f, 0.0f)};
    bool ok = fri[0] > 5;
    for (int i = 0; i < 3; ++i)
        ok = ok && std::abs(klampad[i] - fri[i]) <= 1;
    check(ok,
          "D10 golvet 20 px och taket 40 px vid zoom 1, 3 och 6 = em 20, 30 och "
          "40 px (bläckhöjden ±1 px)",
          std::to_string(klampad[0]) + "/" + std::to_string(klampad[1]) + "/" +
              std::to_string(klampad[2]) + " mot " + std::to_string(fri[0]) + "/" +
              std::to_string(fri[1]) + "/" + std::to_string(fri[2]));

    // D12 — fem rader, maxLines 2, ankaret uppe till vänster i ytans mitt.
    gfx::TextLayoutStyle kort;
    kort.size     = 20.0f;
    kort.color    = kSvart;
    kort.anchor   = gfx::TextAnchor::TopLeft;
    kort.align    = gfx::TextAlign::Left;
    kort.maxLines = 2;
    const auto gl2 = text(r, atlas, "HHH\nHHH\nHHH\nHHH\nHHH", kort);
    s.egenRitning  = [&](gfx::Renderer& rr) { ritaGlyfer(rr, gl2, vy, &atlas); };
    const auto   ref = r.loadFont(gfx::FontFamily::UI, kort.size, gfx::FontStyle::Regular);
    const double rad = ref ? ref->height() : 0.0;
    const Låda   l   = bläck(bild(f), u);
    check(rad > 0.0 && !l.tom() && l.y1 > u.cy + rad &&
              l.y1 <= u.cy + 2.0 * rad + 1.0,
          "D12 maxLines 2: bläck på andra raden men inget under två radhöjder",
          låda(l) + ", raden " + tal(rad));
}

// ------------------------------------------------------------------ D11
//
// SYNLIGHETEN EFTER ZOOM genom en riktig rityta: två etiketter
// med intervallet [2, ∞) — en som kolliderar och en utan kollision — saknar
// bläck vid zoom 1 och har bläck vid zoom 3, båda.
void fallD11()
{
    Rit r;
    if (!r.öppna())
        return;
    r.f.yta.setCenter(0.0, 20.0);
    r.f.yta.setShapes(
        {glaze::Shape::text({0, 0}, "HHHH",
                            glaze::TextStyle{.size = 20.0f, .minZoom = 2.0}),
         glaze::Shape::text({0, 40}, "HHHH",
                            glaze::TextStyle{.size = 20.0f, .collide = false,
                                             .minZoom = 2.0})});
    const Yta u = r.inre();
    r.f.yta.setZoom(1.0);
    const bool ute = bläck(r.bild(), u).tom();
    r.f.yta.setZoom(3.0);
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    // Ankarna 60 logiska px ovanför och under mitten vid zoom 3.
    const Yta övre{u.x0, u.y0, u.x1, static_cast<int>(u.cy), u.cx, u.cy};
    const Yta nedre{u.x0, static_cast<int>(u.cy), u.x1, u.y1, u.cx, u.cy};
    const bool inne = !bläck(b, övre).tom() && !bläck(b, nedre).tom();
    check(ute && inne,
          "D11 utanför zoomintervallet inget bläck, innanför båda etiketterna — "
          "med och utan kollision",
          std::string(ute ? "" : "bläck vid zoom 1 ") + (inne ? "" : "saknas vid zoom 3"));
}

// ------------------------------------------------------------------ R
//
// GLYFRESERVEN genom den RIKTIGA backendens UI-font. Kodpunkterna är valda
// så att de SAKNAS i OS:ets förvalda UI-font: U+4E2D (中) i Segoe UI och SF,
// U+2212 (−) och U+2248 (≈) i Noto Sans — de två senare har Segoe UI och SF
// själva, och de prövas överallt.

std::string utf8Av(char32_t cp)
{
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

std::string kodpunkt(char32_t cp)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04X", static_cast<unsigned>(cp));
    return buf;
}

bool sammaRaster(const gfx::GlyphRaster& a, const gfx::GlyphRaster& b)
{
    return a.width == b.width && a.height == b.height &&
           a.advance == b.advance && a.coverage == b.coverage;
}

void fallR(Fönster& f)
{
    Scen&          s = f.scen;
    gfx::Renderer& r = f.window().renderer();
    nollställ(s);
    bild(f);
    if (g_ritfel)
        return;
    const EgenRitning vakt{s};
    const Yta         u    = ytan(s.box);
    const auto        font = r.loadFont(gfx::FontFamily::UI, 32.0f,
                                        gfx::FontStyle::Regular);
    check(font && font->valid(), "R UI-fonten laddades");
    if (!font || !font->valid())
        return;
    std::vector<char32_t> riktiga = {0x2212, 0x2248};
#if defined(_WIN32) || defined(__APPLE__)
    riktiga.push_back(0x4E2D);   // CJK saknas i Segoe UI och SF
#endif
    gfx::GlyphRaster ersättning;
    font->rasterGlyph(0xFFFD, ersättning);

    // R1 — en riktig glyf (egen eller reservens): hasGlyph, ett annat raster
    // än ersättningstecknet, och mätningen = rastrets steg.
    for (char32_t cp : riktiga) {
        gfx::GlyphRaster g;
        const bool  ok   = font->rasterGlyph(cp, g);
        const float mätt = font->measure(utf8Av(cp)).w;
        check(font->hasGlyph(cp) && ok && !sammaRaster(g, ersättning) &&
                  std::abs(mätt - g.advance) <= 0.01f,
              "R1 " + kodpunkt(cp) +
                  ": hasGlyph, ett annat raster än U+FFFD och measure = "
                  "rastrets steg",
              tal(mätt) + " mot " + tal(g.advance));
    }

    // R2 — drawText ritar glyfen och inte ersättningstecknet, och svarar med
    // mätningens bredd.
    const auto rita = [&](const std::string& text, float& bredd) {
        s.egenRitning = [&](gfx::Renderer& rr) {
            bredd = rr.drawText(*font, static_cast<float>(u.cx) - 20.0f,
                                static_cast<float>(u.cy) - 20.0f, text, kSvart);
        };
        return bild(f);
    };
    float      breddE = 0.0f;
    const Bild be     = rita(utf8Av(0xFFFD), breddE);
    for (char32_t cp : riktiga) {
        if (cp == 0x2212)
            continue;   // ≈ och 中 räcker — minus är ett streck i båda
        float       bredd = 0.0f;
        const Bild  bg    = rita(utf8Av(cp), bredd);
        int         skilda = 0;
        for (int y = u.y0; y < u.y1; ++y)
            for (int x = u.x0; x < u.x1; ++x)
                skilda += std::abs(bg.at(x, y)[1] - be.at(x, y)[1]) > 64 ? 1 : 0;
        const float mätt = font->measure(utf8Av(cp)).w;
        check(skilda > 20 && std::abs(bredd - mätt) <= 0.01f,
              "R2 drawText(" + kodpunkt(cp) +
                  ") ritar annat bläck än U+FFFD och svarar med measure",
              std::to_string(skilda) + " px skiljer, bredd " + tal(bredd) +
                  " mot " + tal(mätt));
    }

    // R3 — en OTILLDELAD kodpunkt (U+0378): ersättningstecknet — hasGlyph
    // false, samma raster och samma steg.
    gfx::GlyphRaster otilldelad;
    font->rasterGlyph(0x0378, otilldelad);
    check(!font->hasGlyph(0x0378) && sammaRaster(otilldelad, ersättning) &&
              std::abs(font->measure(utf8Av(0x0378)).w - ersättning.advance) <=
                  0.01f,
          "R3 U+0378 (otilldelad): hasGlyph false, ersättningens raster och "
          "steg");

    // R4 — ett STYRTECKEN söker ingen reserv och är aldrig en riktig glyf.
    check(!font->hasGlyph(U'\t') && !font->hasGlyph(0x85),
          "R4 tabben och U+0085: hasGlyph false");

    // R5 — SDF-atlasen går samma väg: glyfen har en sida, och dess steg × 2
    // (rasteriseringens supersampling) = atlasfontens measure.
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
    for (char32_t cp : riktiga) {
        const gfx::SdfAtlas::Glyph* g = atlas.glyph(cp);
        const float mätt = atlas.font() ? atlas.font()->measure(utf8Av(cp)).w : 0.0f;
        check(g && g->page >= 0 &&
                  std::abs(g->advance * gfx::kSdfSupersample - mätt) <= 0.01f,
              "R5 SDF-atlasen: " + kodpunkt(cp) +
                  " har ett fält och steget × 2 = atlasfontens measure",
              g ? tal(g->advance * gfx::kSdfSupersample) + " mot " + tal(mätt)
                : "ingen glyf");
    }
}

// ============================================================ DEL K
//
// MÅLARENS KLIPP genom en riktig rityta: `Painter::save(Rect)` i de
// lokala px och i världen. Referensen är rektanglarnas egen geometri — målarens
// `rotate` och ritytans `toLocal` — aldrig programmens formel: en punkt 3
// logiska px inne i klippet ritas, en 3 px ute inte (klippets kantutjämning
// är en fysisk px).

// Den lokala punkten i en vriden ram: mitten (mx, my) och målarens
// `rotate(g)` på q — (cos·x − sin·y, sin·x + cos·y).
glaze::Point iRam(double mx, double my, double g, glaze::Point q)
{
    const double r = g * 3.14159265358979323846 / 180.0;
    return {mx + std::cos(r) * q.x - std::sin(r) * q.y,
            my + std::sin(r) * q.x + std::cos(r) * q.y};
}

// Är pixeln under den lokala punkten färgen? `fick` får pixeln.
bool färgVid(const Rit& r, const Bild& b, glaze::Point l, gfx::Color c,
             std::string& fick)
{
    const std::uint8_t* p = b.at(r.x(l.x), r.y(l.y));
    fick += " " + px(p);
    return ärFärg(p, c, 3);
}

// K1 VARJE PROGRAM KLIPPS av ett axelparallellt klipp (x 100–260, y 60–160
// i de lokala px): ytan (Fill), en tjock linje (Line), en stor markör och en
// text (Glyph) och en bild (Image) — var och en i sin ritning, i sin färg
// innanför och vit utanför.
void fallK1()
{
    Rit r;
    if (!r.öppna())
        return;
    const gfx::Color          kBlå{0, 0, 255, 255};
    std::vector<std::uint8_t> rgba(4 * 4 * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        std::memcpy(&rgba[i], &kBlå, 4);
    const glaze::Bitmap blå(4, 4, rgba.data(), 0);
    int                 fall = 0;
    r.f.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        p.screenSpace();
        auto k = p.save(glaze::Rect{100, 60, 160, 100});
        switch (fall) {
        case 0: p.fillRect({0, 0, 480, 360}, kBlå); break;
        case 1:
            p.drawLine({0, 110}, {480, 110}, {.paint = kBlå, .width = 20.0f});
            break;
        case 2: p.drawMarker({260, 110}, {.size = 120.0f, .fill = kBlå}); break;
        case 3:
            p.drawText({180, 110}, "HHHHHHHHHHHH", {.size = 40.0f, .color = kBlå});
            break;
        default: p.drawImage({0, 0, 480, 360}, blå); break;
        }
    };
    using L = glaze::Point;
    // Ytan, linjen och bilden: tre punkter inne, fyra ute (linjen bara längs
    // sig själv).
    const char* namn[] = {"ytan (Fill)", "linjen (Line)", "", "", "bilden (Image)"};
    for (fall = 0; fall < 5; ++fall) {
        if (fall == 2 || fall == 3)
            continue;
        if (fall == 4)
            r.bild();   // bildcachen laddar upp bilden i den första ritningen
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        std::string fick;
        bool        ok = färgVid(r, b, L{180, 110}, kBlå, fick) &&
                  färgVid(r, b, L{103, 110}, kBlå, fick) &&
                  färgVid(r, b, L{257, 110}, kBlå, fick);
        fick += " |";
        ok = färgVid(r, b, L{97, 110}, kVit, fick) &&
             färgVid(r, b, L{263, 110}, kVit, fick) && ok;
        if (fall != 1)
            ok = färgVid(r, b, L{180, 57}, kVit, fick) &&
                 färgVid(r, b, L{180, 163}, kVit, fick) && ok;
        check(ok, std::string("K1 ") + namn[fall] +
                      " klipps: blå 3 px innanför kanterna, vit 3 px utanför",
              fick);
    }
    // Markören (en skiva med diametern 120 kring (260, 110)) skärs av
    // klippets högra kant och dess övre: inne blå, ute vit.
    fall = 2;
    {
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        std::string fick;
        bool        ok = färgVid(r, b, L{230, 110}, kBlå, fick) &&
                  färgVid(r, b, L{257, 110}, kBlå, fick) &&
                  färgVid(r, b, L{250, 63}, kBlå, fick);
        fick += " |";
        ok = färgVid(r, b, L{263, 110}, kVit, fick) &&
             färgVid(r, b, L{290, 110}, kVit, fick) &&
             färgVid(r, b, L{250, 57}, kVit, fick) && ok;
        check(ok, "K1 markören (Glyph) klipps av högerkanten och överkanten",
              fick);
    }
    // Texten (tolv H runt x = 180, bredare än klippet): bläck mellan
    // kanterna, inget bläck 3 px och mer utanför dem.
    fall = 3;
    {
        const Bild b = r.bild();
        if (g_ritfel)
            return;
        int inne = 0, ute = 0;
        for (int ly = 85; ly <= 135; ++ly)
            for (int lx = 10; lx <= 350; ++lx) {
                if (ärFärg(b.at(r.x(lx), r.y(ly)), kVit, 3))
                    continue;
                if (lx >= 103 && lx <= 257)
                    ++inne;
                else if (lx <= 97 || lx >= 263)
                    ++ute;
            }
        check(inne > 200 && ute == 0,
              "K1 texten (Glyph) klipps: bläck mellan kanterna, inget utanför",
              std::to_string(inne) + " inne, " + std::to_string(ute) + " ute");
    }
}

// K2 VRIDET 30° kring (180, 110): klippet är rektangeln 140 × 80 i den vridna
// ramen — blått 3 px innanför varje kant, vitt 3 px utanför och i hörnet av
// den omskrivna rutan (som en scissor hade släppt in).
void fallK2()
{
    Rit r;
    if (!r.öppna())
        return;
    const gfx::Color kBlå{0, 0, 255, 255};
    r.f.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        p.screenSpace();
        p.translate(180, 110);
        p.rotate(30);
        auto k = p.save(glaze::Rect{-70, -40, 140, 80});
        p.fillRect({-400, -400, 800, 800}, kBlå);
    };
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    std::string fick;
    bool        ok = true;
    for (const glaze::Point q : {glaze::Point{0, 0}, {67, 0}, {-67, 0}, {0, 37},
                                 {0, -37}, {60, 30}})
        ok = färgVid(r, b, iRam(180, 110, 30, q), kBlå, fick) && ok;
    fick += " |";
    for (const glaze::Point q : {glaze::Point{73, 0}, {-73, 0}, {0, 43}, {0, -43}})
        ok = färgVid(r, b, iRam(180, 110, 30, q), kVit, fick) && ok;
    // Den omskrivna rutans hörn: halva måtten 70·cos 30 + 40·sin 30 och
    // 70·sin 30 + 40·cos 30, 3 px in.
    const double hx = 70.0 * std::cos(3.14159265358979323846 / 6.0) + 40.0 * 0.5;
    const double hy = 70.0 * 0.5 + 40.0 * std::cos(3.14159265358979323846 / 6.0);
    ok = färgVid(r, b, glaze::Point{180 - hx + 3, 110 - hy + 3}, kVit, fick) &&
         färgVid(r, b, glaze::Point{180 + hx - 3, 110 + hy - 3}, kVit, fick) && ok;
    check(ok, "K2 ett vridet klipp är fyrhörningen — inte dess omskrivna ruta",
          fick);
}

// K3 NÄSTLAT OCH TILLBAKA: rektangeln x 100–260, y 60–160 och i den en 45°
// vriden kvadrat (sidan 100) kring (180, 110). Blått bara i snittet; i
// rektangeln utanför kvadraten och i kvadraten utanför rektangeln vitt — och
// efter båda lägena ritas en grön yta utanför dem utan klipp.
void fallK3()
{
    Rit r;
    if (!r.öppna())
        return;
    const gfx::Color kBlå{0, 0, 255, 255};
    r.f.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        glaze::Painter& p = e.painter;
        p.screenSpace();
        {
            auto a = p.save(glaze::Rect{100, 60, 160, 100});
            p.translate(180, 110);
            p.rotate(45);
            auto k = p.save(glaze::Rect{-50, -50, 100, 100});
            p.fillRect({-400, -400, 800, 800}, kBlå);
        }
        p.fillRect({10, 200, 60, 60}, kGrön);
    };
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    using L = glaze::Point;
    std::string fick;
    bool        ok = färgVid(r, b, L{180, 110}, kBlå, fick) &&
              färgVid(r, b, L{180, 65}, kBlå, fick) &&
              färgVid(r, b, L{115, 110}, kBlå, fick);
    fick += " |";
    // (105, 110) och (103, 63) i rektangeln men utanför kvadraten; (180, 55)
    // och (180, 165) i kvadraten men utanför rektangeln.
    ok = färgVid(r, b, L{105, 110}, kVit, fick) &&
         färgVid(r, b, L{103, 63}, kVit, fick) &&
         färgVid(r, b, L{180, 55}, kVit, fick) &&
         färgVid(r, b, L{180, 165}, kVit, fick) && ok;
    check(ok, "K3 nästlade klipp: bara snittet ritas", fick);
    std::string grön;
    check(färgVid(r, b, L{40, 230}, kGrön, grön),
          "K3 efter lägena ritas utan klipp (och utan scissor)", grön);
}

// K4 I VÄRLDEN under bäring 20°, tilt 45° och zoom 2: klippet är rektangelns
// bild där ritytans `toLocal` lägger hörnen — blått i mitten och 3 px
// innanför varje kants mitt, vitt 3 px utanför.
void fallK4()
{
    Rit r;
    if (!r.öppna())
        return;
    const gfx::Color kBlå{0, 0, 255, 255};
    glaze::Canvas&   y = r.f.yta;
    y.setCenter(500.0, 500.0);
    y.setZoom(2.0);
    y.setBearing(20.0f);
    y.setPitch(45.0f);
    y.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        auto k = e.painter.save(glaze::Rect{470, 485, 60, 30});
        e.painter.fillRect({300, 300, 400, 400}, kBlå);
    };
    const Bild b = r.bild();
    if (g_ritfel)
        return;
    const glaze::Point hörn[4] = {{470, 485}, {530, 485}, {530, 515}, {470, 515}};
    glaze::Point       l[4];
    for (int i = 0; i < 4; ++i) {
        const std::optional<glaze::Point> p = y.toLocal(hörn[i]);
        if (!p) {
            check(false, "K4 rektangelns hörn ligger framför kameran");
            return;
        }
        l[i] = *p;
    }
    const glaze::Point mitt{(l[0].x + l[1].x + l[2].x + l[3].x) / 4.0,
                            (l[0].y + l[1].y + l[2].y + l[3].y) / 4.0};
    std::string fick;
    bool        ok = färgVid(r, b, mitt, kBlå, fick);
    for (int i = 0; i < 4; ++i) {
        const glaze::Point& u = l[i];
        const glaze::Point& v = l[(i + 1) % 4];
        const glaze::Point  m{(u.x + v.x) / 2.0, (u.y + v.y) / 2.0};
        const double        len = std::hypot(v.x - u.x, v.y - u.y);
        glaze::Point        n{-(v.y - u.y) / len, (v.x - u.x) / len};
        if (n.x * (mitt.x - m.x) + n.y * (mitt.y - m.y) < 0.0)
            n = {-n.x, -n.y};   // inåt
        ok = färgVid(r, b, {m.x + 3.0 * n.x, m.y + 3.0 * n.y}, kBlå, fick) &&
             färgVid(r, b, {m.x - 3.0 * n.x, m.y - 3.0 * n.y}, kVit, fick) && ok;
    }
    check(ok, "K4 i världen under bäring och tilt: klippet är rektangelns bild "
              "på ytan",
          fick);
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== scene_pixel_gate — scenens pipeline i pixlar ===\n");

    Fönster f;
    f.setOffscreen(true);
    f.show();
    if (!f.isOpen()) {
        std::printf("  FEL  fönstret öppnades inte — ingen headless-yta?\n");
        std::printf("=== scene_pixel_gate: FALLERAR ===\n");
        return 1;   // ALDRIG skip — se filhuvudet
    }

    fallA1(f);
    if (!g_ritfel) fallA2(f);
    if (!g_ritfel) fallA3(f);
    if (!g_ritfel) fallA4(f);
    if (!g_ritfel) fallA5(f);
    if (!g_ritfel) fallA6(f);
    if (!g_ritfel) fallA7(f);
    if (!g_ritfel) fallA8(f);
    if (!g_ritfel) fallA9(f);
    if (!g_ritfel) fallA10(f);
    if (!g_ritfel) fallA11(f);
    if (!g_ritfel) fallMm(f);
    if (!g_ritfel) fallB1(f);
    if (!g_ritfel) fallB2(f);
    if (!g_ritfel) fallB3(f);
    if (!g_ritfel) fallB4(f);
    if (!g_ritfel) fallB5(f);
    if (!g_ritfel) fallB6(f);
    if (!g_ritfel) fallB7(f);
    if (!g_ritfel) fallB14(f);
    if (!g_ritfel) fallB12(f);
    if (!g_ritfel) fallE(f);
    if (!g_ritfel) fallE7();
    if (!g_ritfel) fallC1(f);
    if (!g_ritfel) fallC2(f);
    if (!g_ritfel) fallC3(f);
    if (!g_ritfel) fallC4(f);
    if (!g_ritfel) fallD1(f);
    if (!g_ritfel) fallD2(f);
    if (!g_ritfel) fallD3(f);
    if (!g_ritfel) fallD4(f);
    if (!g_ritfel) fallD5(f);
    if (!g_ritfel) fallD6(f);
    if (!g_ritfel) fallD7(f);
    if (!g_ritfel) fallF1();
    if (!g_ritfel) fallF2();
    if (!g_ritfel) fallF3();
    if (!g_ritfel) fallF4();
    if (!g_ritfel) fallF5();
    if (!g_ritfel) fallF6();
    if (!g_ritfel) fallG1();
    if (!g_ritfel) fallG2();
    if (!g_ritfel) fallG3();
    if (!g_ritfel) fallH1(f);
    if (!g_ritfel) fallH2(f);
    if (!g_ritfel) fallH3(f);
    if (!g_ritfel) fallH4(f);
    if (!g_ritfel) fallH5(f);
    if (!g_ritfel) fallH6(f);
    if (!g_ritfel) fallH8(f);
    if (!g_ritfel) fallH9(f);
    if (!g_ritfel) fallH7();
    if (!g_ritfel) fallH10();
    if (!g_ritfel) fallB8();
    if (!g_ritfel) fallB9();
    if (!g_ritfel) fallB10();
    if (!g_ritfel) fallB11();
    if (!g_ritfel) fallB13();
    if (!g_ritfel) fallB15();
    if (!g_ritfel) fallD8D9(f);
    if (!g_ritfel) fallD10D12(f);
    if (!g_ritfel) fallD11();
    if (!g_ritfel) fallR(f);
    if (!g_ritfel) fallK1();
    if (!g_ritfel) fallK2();
    if (!g_ritfel) fallK3();
    if (!g_ritfel) fallK4();
    f.scen.chunkar.clear();   // buffertarna före fönstret (livstidsregeln)

    std::printf(g_fel ? "=== scene_pixel_gate: %d FEL av %d ===\n"
                      : "=== scene_pixel_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
