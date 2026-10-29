// dock_gate — DOCKNINGENS grind: `DockHost` + `DockPanel` — modellen
// kant → remsa → cell → flik, handtagen, min/max, huvudytan och
// layoutsträngen v2 —, verktygsfältens BAND, KNAPPNÅLEN med overlayn,
// lämna-prövningen, snappen och glidningen, och de FLYTANDE panelerna.
//
// Kör:  ./build/tests/dock_gate [-v] [--png <katalog>]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//   --png  skriv per UI × läge en flikgrupp med en stapel och ett handtag, och
//          släppindikatorn mitt i ett drag, att ÖGNA (inte en del av grinden)
//
// ÅTTADELAD (BANDEN har både ett handräknat och ett fönsterblock):
//
//   LAYOUT       geometrin mot HANDRÄKNADE värden (riggen utan renderer,
//                med en falsk font — handtagens bredd är text): skärordningen,
//                två celler 1:1 och 2:1, ett minimum som tvingar en
//                omfördelning och ett maximum på remsans tjocklek (min vinner
//                en krock), flikraden bara vid minst två öppna flikar och en
//                TOM yta för den inaktiva fliken, en stängd panel skär inget,
//                en infälld panel blir ett handtag i en remsa YTTERST med
//                `dockHandleH` (och handtaget hittas med sin tooltip), skala 2
//                och en panel utan värd. De förväntade värdena är tal skrivna här; ur stilen
//                läses bara indata (skiktets tjocklek, flikradens och
//                handtagets höjd, luften, AUTO-måttet). Också TALEN:
//                vikter på 3e38 (lika
//                celler, ändliga ytor inom remsan) och 300 staplingar i rad
//                utan att en vikt underflödar till 0.
//   BANDEN       (handräknat ur stilens mått och den falska fontens 8 px
//                per byte) två fält i band 0 överst med luckan, ett band 1
//                innanför, ett STÅENDE band vid vänsterkanten — allt skuret
//                FÖRE panelremsan — och `t=` i `layout()`; ett dolt fält skär
//                inget och behåller sitt index; ett fält adopterat under
//                körning hamnar sist i sitt band, ett band utanför antalet
//                blir ett nytt innersta; `t=` tur och retur och en sträng som
//                ordnar om (ett fält som byter riktning byter bredd och
//                höjd); glesa bandnycklar packas — och ett UTELÄMNAT fält
//                med en gles nyckel byter inte plats vid återläsningen;
//                previewns ordning (fälten satta efter trädbygget, också ett
//                fält som adopterades UTAN kant) ger samma band som appens;
//                ett överfullt stående fält kortas vid bandets slut, och
//                bandet krymper fälten bakifrån ned till greppet,
//                luften och överflödsknappen — utan överflöde oförändrat.
//   STRÄNGEN     `layout()` ↔ `setLayout()` (v2): tur och retur, ATOMISK
//                läsning av varje regel i grammatiken (trasiga strängar
//                lämnar ALLT orört — också tal utanför float, NaN och
//                oändligt), ett okänt namn hoppas över och resten gäller (en
//                tom cell och en tom remsa tas bort), en panel som saknas
//                läggs innerst, en panel adopterad efter raden hamnar som en
//                adopterad före, `v1` avvisas, skrivaren utelämnar namnlösa
//                paneler och väljer den aktiva bland de kvarvarande, RÅA mått
//                som −1, NaN och ∞ skrivs som AUTO och ger samma yta inlästa,
//                och vikter med många decimaler går tur och retur BITVIS —
//                den första emissionen läses med grindens egen `from_chars`.
//                `t=` har sina egna trasiga strängar (fältantal, kant, heltal,
//                flagga, två fält på samma plats, ett namn två gånger).
//   MED FÖNSTER  ett DOLT fönster, händelserna genom fönstrets RIKTIGA ingång
//                (`AppWindow::handleEvent`, som i dnd_gate): en rubrik
//                dras till mitten av en panel (flik — indikatorns pixlar är
//                exakt panelens kommande yta, flikraden UTANFÖR den), ett klick
//                på en flik byter, en flik dras ut till en kant, en rubrik till
//                en panels nederkant (staplad cell) och till värdens kant
//                (ytterst), ett cellskikt dras och klampas mot minimum och
//                släppet ger BITVIS förhandsvisningens ytor, ett cellskikt
//                vars två golv inte ryms står still, ett remsskikt skriver
//                tjockleken till ALLA paneler i remsan, knappnålen gör ett
//                handtag, ett klick på handtaget öppnar OVERLAYN (fokus in i
//                panelen, inget onChange) och knappnålen i den fäster den
//                igen, värden är ETT tabbstopp och Enter fäller ut,
//                `show()` på en stängd flik gör den aktiv och flyttar ett
//                riktigt tangentfokus ur den flik som blev inaktiv (text når
//                inte det dolda fältet), `hide()` av appen och stängknappen
//                flyttar fokus till huvudytan, och utan något stopp att lämna
//                det till står fönstret utan fokus (också efter `setLayout`),
//                `focusOrder()` innehåller aldrig en inaktiv fliks fält,
//                fokusförlust mitt i ett drag släcker indikatorn utan
//                dockning, `onChange` alltid panelens före värdens — och
//                `hide()`/`show()`/`setCollapsed()` UTANFÖR ett event ger
//                exakt samma bild som en full omritning.
//                F16 ett tryck på den ANDRA fliken i en flikgrupp lägger
//                fokusringen (temats `accent`, pixeln i ringens mitt) på den
//                och inte kvar på listans första post, och Vänster/Höger
//                flyttar den.
//   OVERLAYN     (ett eget dolt fönster) en onålad panel vid start är ett
//                handtag; handtagsklicket gör den till värdens FRÄMRE BARN
//                vid kanten, och huvudytan är BITVIS oförändrad; overlayn
//                ritas ovanpå huvudytans mätare (pixlar, full och delvis
//                omritning) och träffas före huvudytans fält; Escape fäller
//                in den med fokus på värden; knappnålen fäster och lossar;
//                skiktens SNAPP — nära det förvalda måttet blir det exakt,
//                under golvet minus zonen visar förhandsvisningen panelerna
//                infällda och släppet fäller in dem med måttet kvar (remsa
//                och cell). Med RIKTIG tid (stegen väntar i pumpen):
//                lämna-prövningen fäller in först när pekaren, fokus och ett
//                tryck alla är ute — pekaren över en INAKTIVERAD etikett
//                räknas som inne — och glidningen ligger mellan start och
//                slut efter den första bilden och ber inte om fler när den
//                är framme (`damageStamp()` står still).
//                O9 en adopterad, utfälld onålad panel tränger undan
//                overlayn med fokus i sitt fält — fokus lämnar fältet och
//                text når det inte · O10 ett overlay-LAGER under overlayn
//                ritas bara utanför den · D5 värden DÖLJS — inga tick, och
//                visad igen fälls overlayn in · D6 glidningen ritad i två
//                PARTIELLA skadeområden per bild ger en slutbild exakt lika
//                en full omritning · D7 fönstret döljs — inga tick.
//   BANDEN MED   (ett eget dolt fönster med den riktiga fonten — referensen
//   FÖNSTER      är strukturen) flyttpekaren över greppet; greppdraget till
//                ett NYTT band (indikatorns pixel är fältets kommande yta),
//                omordning i ett band med mittpunktsregeln, till ett annat
//                band efter ett fält, till vänsterkanten där fältet STÅR, och
//                fokusförlust mitt i draget som inte flyttar något — med
//                `band()`/`dock` tillbakaskrivna och värdens `onChange` —
//                och ett band som DÖLJS med fokus i ett av dess fält
//                (`hide()` och `setLayout`): fokus lämnar det, text når det
//                inte, och fältets `onFocusLost` levereras.
//   FLYTANDE     (DOLDA fönster — `hide()` före `run()`, inte offscreen)
//                FL1 en panel flytande vid starten: efter ett pumpvarv i ett
//                eget fönster ÄGT av huvudfönstret, DOLT som det,
//                `f=` och ingen `p=`, huvudytan når högerkanten · FL2
//                `setFloating(false)`: dockad i `layout()` DIREKT, hem och
//                fönstret rivet i svansen · FL3 dubbelklick på rubriken:
//                flytande, onChange panel före värd · FL4 det flytande
//                fönstrets `close()` stänger PANELEN (fönstret lever, `f=…,1`),
//                en icke stängbar står kvar · FL6 (Windows) F5 POSTAD till det
//                flytande fönstret fyrar ÄGARENS menygenväg · LEVANDE DRAG:
//                LV1 fall A — rubriken
//                över tröskeln LOSSAR panelen direkt (flytande, värdens
//                capture, `f=`, huvudytan växer), fönstret öppnas där
//                bärandet står och följer pekaren (Windows ±1), släppt utan
//                platshållare flyter den — ETT onChange · LV4 fokusförlusten
//                mitt i draget avbryter: capturen och platshållaren borta,
//                panelen flytande, onChange POSTAT exakt en gång · LV2
//                platshållaren över Bevakning är slutgeometrin (accentramen
//                på referensytans kanter), släppet ger fliken · LV9 en FLIK
//                lossas med greppet i rubrikens höjd · LV12 ett fall A-drag
//                släppt FÖRE svansen, långt utanför huvudfönstret: fönstret
//                öppnas i svansen där bärandet stod (Windows ±1), inte inklämt
//                · LV3 (Windows, postat) fall B: det flytande fönstrets rubrik
//                dras, fönstret följer, släppt mitt i huvudytan flyter det kvar
//                (ingen reserv hem), släppt i högra kantzonen dockas det — det
//                andra trycket utanför dubbelklickets avstånd · LV11 (Windows)
//                fall B:s avbrott med dolda fönster: panelen håller trycket i
//                sitt fönster, OS:et tar capturen, och draget avslutas utan en
//                ritning — ETT onChange · LV5 (utan skärmpositioner) draget
//                visar platshållaren medan panelen står kvar · OV1/OV2 ägaren
//                visas och döljs — det flytande fönstret följer · LV10
//                (Windows) panelens fönster är inte NOACTIVATE, och — med
//                huvudfönstret GJORT till förgrunden direkt före — en lossning
//                stjäl den inte · FL5 en öppen popup i
//                panelen NEKAR flytten hem: den väntar med tillståndet redan
//                dockat, och efter popupens omritning görs den ·
//                QUIT (Windows) ett riktigt appavslut (`PostQuitMessage`) med
//                veto i huvudfönstret — panelen syns och flyter, det flytande
//                fönstret står kvar · FL9 huvudfönstrets `close()` stänger
//                det, `layout()` i dess onClose säger flytande och öppen, och
//                panelens läge är orört. TOM (Windows) en flytande panel som
//                minns vänsterkanten, DRAGEN (postat, fönstret följer) in i
//                högra kantzonen i en tom värd, dockas vid högerkanten. OFF
//                en offscreen-värd skapar
//                inget fönster. T tre
//                fönsternivåer: vetot i toppen stoppar hela
//                kaskaden utan att det flytande fönstret lägger ett eget eller
//                rör panelen; utan veto stängs alla tre med läget orört. R
//                rivningen: RA värden död först (panelen lossad
//                utan barnvandring, dess lånade barn orört och UTAN fönster —
//                en sättare på det går igenom), RB panelen och dess lånade
//                barn döda först (ingen dereferens), RC en panel PÅ VÄG HEM
//                som flik hos en levande panel dör mitt i ett remsskiktsdrag
//                — Ctrl+PageDown i den levande fliken och skiktsläppet direkt
//                efteråt läser cellen och remsan utan den, RE ett SYNLIGT
//                flytande fönster med fokus i panelens fält: panelen och
//                fältet dör, och fönstret ritas direkt efteråt och (Windows)
//                får mus och tangent i luckan före synken — en kontroll
//                postad FÖRE döden ser att de kom fram medan det levde, och
//                därefter rivs det — och RD en
//                ÄGD panel som förblir ägd ut och hem och förstörs en gång.
//                RV de generella krokarna med ett eget fönsterpar: egen genväg
//                först, reservvägen före Tab, ägarens visning och döljning.
//   FLYTFÄLT     (ett eget fönster som startar DOLT) det
//                FLYTANDE VERKTYGSFÄLTET: FT1 bandet saknar fältet direkt,
//                `ft=`, ett eget fönster med FAST storlek = rubriken + fältet
//                liggande · FT2 tillbaka sist i bandet · FT3 `ft=` med plats
//                (Windows ±1) och en `t=` som dockar, FT3b STÅENDE återdockning
//                med längden till höjden · FT4 trasiga `ft=` · FT5 dubbelklick
//                på greppet (flytande) och (Windows) på rubriken (dockat) ·
//                FT7 bredd och höjd byts · FT6 fönstrets stängning och
//                (Windows) rubrikens ✕ döljer fältet · FT9 fönstret följer
//                fältets mått: ett barn; en text och en bild medan ägaren och
//                fönstret FÖRBLIR dolda (passat med gränserna, utan visning);
//                ett stilbyte dolt (före visningen) och synligt (vid
//                ritningen) · FT10a (Windows, LV8, alla fönster dolda) fall B
//                för ett fält med NEKAT övertagande: rubriken dras, värden får
//                ingen capture och fältets fönster behåller trycket och OS:ets
//                capture, platshållaren är slutgeometrin för ett liggande fält
//                som dockas stående · FT11 en väntande hemflytt · FT10b
//                (Windows, LV8, huvudfönstret gjort till förgrunden) samma drag
//                med LYCKAT övertagande: värden och värdens fönster håller
//                trycket, fältets fönster inte längre · LV6
//                fall A: greppet lossar fältet direkt, fönstret vid pekaren
//                minus greppet, platshållaren i vänsterbandet och släppet där
//                · LV7 släppt utan platshållare flyter fältet, `ft=` med
//                plats, och (Windows, LV10) fönstret är NOACTIVATE och
//                TOOLWINDOW · FT8 ett flytande fält som dör med fokus i
//                sig, och (Windows) ✕, dubbelklick och tangent i luckan före
//                synken, levererade medan fönstret levde · FT12 värden rivs
//                först. Detaljerna står vid `fallFlytFalt`.
//
// SABOTAGEN den ska fälla (prövas ett i taget mot ett rent original):
//   * `showsChildren` ignoreras i `AppWindow::collectFocusable` (F1, F2: den
//     inaktiva flikens fält står i fokusordningen)
//   * cellernas vikter ignoreras i `computeLayout` (L3: 2:1 blir 1:1)
//   * min/max-klampningen tas bort (L4)
//   * handtagen skärs INNANFÖR panelremsorna (L7: vänsterpanelen når taket)
//   * den inaktiva fliken placeras i cellen (L5; F1 pixlar)
//   * `setLayout` tillämpar posterna före den sista är tolkad (S2)
//   * ett okänt namn avvisar hela strängen (S3)
//   * Fill- och sidozonerna byter plats i `dropAt` (F1: ingen flik; F4: ingen
//     stapel)
//   * indikatorn ritas ur en kantremsa och inte ur simuleringen (F1: pixeln i
//     den kommande flikraden ändras)
//   * `childVisibilityChanged` anropas inte från `setVisible` (F10)
//   * värdens `onChange` före panelens (F1, F3, F4, F5)
//   * skrivaren emitterar en tom cell/remsa för namnlösa paneler (S7)
//   * talen läses med `parseNumber` utan ändlighetskontroll (S2: 1e39)
//   (tal, skrivare och fokus:)
//   * `fördela` räknar i float igen (L10: NaN/∞ i ytorna)
//   * `dockOn` utan `normalizeWeights` före halveringen (L11: "c=0")
//   * skrivaren emitterar de råa måtten utan `panelSize` (S7: strängen går
//     inte att läsa) — eller `computeLayout` läser dem utan (S7: ∞ tar
//     hela höjden, ytorna skiljer)
//   * cellskiktets förhandsvisning tvingar en längd i stället för vikter
//     (F5b: förhandsvisningen visar vänster vid golvet)
//   * `childVisibilityChanged` utan `guardFocus` (F10: fokus och texten
//     stannar i det dolda sökfältet)
//   * `focusHostOrClear` utan `clearFocus` (F15: fokus kvar i den stängda
//     panelen)
//   (verktygsfältens band:)
//   * nya band läggs YTTERST i stället för innerst (B3: band 7 hamnar före
//     band 0; FB1: t2 över t1 och t3)
//   * banden skärs efter handtagen eller panelremsorna (B1: panelen når
//     taket)
//   * `t=` tappar ordningen — den onämnda/nämnda ordningen sorteras inte på
//     plats (B4: t1 före t2)
//   * ett dolt fält skär sitt band ändå (B2)
//   * `writeBackBars` byter inte bredd och höjd vid ett riktningsbyte (B4:
//     v1:s höjd 0 och bandet `toolBarH` tjockt)
//   * normaliseringen läser inte fältens `band()` efter adoptionen (B5, B6)
//   * mittpunktsregeln räknar fältets vänsterkant i stället för mitten (FB2:
//     släppt en fjärdedel in i t1 hamnar t3 efter)
//   (band, fokus och ordning:)
//   * fokusvakten ser bara paneler — eller synlighetskroken för ett fält
//     återvänder utan vakt (FB6: fokus och texten stannar i det dolda fältet)
//   * `setLayout` blandar strängens packade index med de onämnda fältens
//     glesa nycklar (B5: t3 går före t2)
//   * fältens rektanglar klampas inte vid bandets slut (B7)
//   * ett fält utan kant lämnar modellens ordning (B6: appen ger b, a)
//   (bandets överflöde:)
//   * bandet krymper inte fälten (B7: `extra` får längden 0; B8: t2 i sin
//     egen längd), krymper framifrån (B8: t1 krymper först) eller förbi den
//     kortaste längden (B8: t2 under greppet + luften + knappen)
//   * `minLength` struntar i `setOverflow(false)` (B8: bandet utan knapp)
//   (knappnålen och overlayn:)
//   * `frontChild` ignoreras i `widgetAtRec` (O3), i `drawTree` (O2b full)
//     eller i `drawTreePartial` (O2b delvis)
//   * overlayn skärs ur ytan i stället för att läggas ovanpå (O2: huvudytan)
//   * fokusvillkoret bort ur `hideCheck` (D1), tryckvillkoret bort (D4), och
//     pekarvillkoret via `hovered()` i stället för geometrin (D3)
//   * `stepAnims` invaliderar också när allt är framme (D6: stämpeln)
//   * snappen utan infällning (O7, O8: förhandsvisningen) eller utan
//     magneterna (O7, O8: exakt-kontrollerna)
//   * Escape-grenen bort ur `descendantKeyDown` (O4)
//   * handtagsklicket på en onålad panel fyrar `onChange` (F8, O2)
//   (overlayns fokus, lager och timer:)
//   * fokusvakten inte frågad efter en undanträngning (O9: texten når fältet)
//   * overlay-lagren under `frontChild` inte skymda (O10: ett klipp snittar)
//   * `shownChanged` stoppar inte timern (D5, D7: overlayn fälls in dold)
//   * glidningen stegas i `draw()` igen (D6: slutbilden skiljer)
//   (flytande paneler:)
//   * ingen ägarkontroll i det flytande fönstrets `onClose` (FL9, T2/T3 —
//     pumpen står kvar, ctest:s tidsgräns fäller den)
//   * synken utan väntande flytt — `draw`/`FloatBody::draw` postar inte
//     igen (FL5: panelen kommer aldrig hem)
//   * en offscreen-värd tillåts skapa fönster (OFF)
//   * reservvägen anropas inte (FL6, RV2/RV3)
//   * ägarens synlighet följs inte (OV1/OV2, RV4)
//   * `pruneModel` borttagen (RC: den döda panelen i `layout()` — eller en
//     krasch)
//   * destruktorn lossar med `detach` i stället för `severChild` (RB: en
//     död panel derefereras)
//   * en ägd panel förs över som lånad (RD: räknaren 0)
//   * hidden start skapar inga fönster (FL1)
//   (flytande paneler, rivning och avslut:)
//   * `~DockPanel` säger inget till värden — fönstret släpper panelen först
//     i synken (RE: ritningen eller händelserna derefererar den döda panelen
//     — en krasch)
//   * `forgetWidgets` släpper inte fokus (RE: caret- och tangentvägen läser
//     det döda fältet)
//   * `syncModel` borttagen i `descendantKeyDown` OCH dödsfallets
//     modellborttagning borttagen (RC: den döda fliken läses — en krasch;
//     var för sig täcker de varandra)
//   * `appWindow()`/`invalidate()` läser `owner_` utan livstecknet (RA: en
//     sättare på det överlevande barnet skriver genom en dinglande pekare)
//   * appavslutet köar varje fönster (QUIT: panelen stängs efter vetot)
//   * självträffsregeln gäller också en flytande panel (TOM: hem till
//     vänster)
//   (tangentbordsfokus:)
//   * `selectKbdItem` borttagen ur `mouseDown` (F16: ringen står kvar på
//     vänsters flik efter trycket på söks)
//   (levande drag:)
//   * ingen capture till värden i fall A — rörelserna går till den lossade
//     panelen (LV1: fönstret följer inte, och `hasPointerCapture` är falsk)
//   * `forgetPanel` nollar `carry_` (LV1 efter svansen: draget dött)
//   * Home-reserven kvar i draget (LV3: mittsläppet dockar)
//   * `onChange` vid lossningen i stället för vid släppet (LV1: två onChange)
//   * `checkCarry` bort ur ritningen OCH `DockHost::mouseCancel` tom (LV4:
//     platshållaren står kvar, inget onChange — var för sig täcker de
//     varandra i fall A)
//   * `DockHost::mouseCancel` kör handlarna direkt i stället för att posta
//     (LV4: onChange i samma ögonblick)
//   * `showInactive_` inte kopierad i `showOwned` (LV10, villkorat)
//   * `noActivate_` inte satt för fältfönstret (LV7/LV10: stilen)
//   * `carryPlacement` ignoreras — fönstret mitt över ägaren (LV1, LV6:
//     hörnet)
//   * greppet räknat utan k eller utan klampning, eller en flik med greppet
//     i flikens höjd (LV9: hörnet)
//   * platshållaren ur `drop_` i stället för ur draget (LV2:
//     ingen ram)
//   * `liveCarry` frågar fall B:s tryck i stället för värdens capture (LV1:
//     draget avslutas i första ritningen)
//   * självträffsregeln utan `!isFloating()` (TOM, LV3: kantzonen nekas
//     medan panelen väntar på sitt fönster — eller hem)
//   (levande drag, övertagande och landning:)
//   * fall B utan explicit capture på panelen (LV11: panelen håller inget
//     tryck), eller `DockPanel::mouseCancel` tom (LV11: inget onChange
//     — de dolda fönstren ritas aldrig)
//   * `rememberCarryPlace` lägger ingen landning, eller `carryPlacement`
//     frågar den inte (LV12: fönstret inklämt i arbetsytan)
//   * fältets övertagande aldrig försökt (FT10b: värden utan capture) eller
//     alltid antaget lyckat (FT10a: värden med capture, eller draget dör)
//   * `captureMouse` utan förgrundskontrollen (FT10a: värden tar trycket)
//   (flytande verktygsfält:)
//   * `normalize` tar inte bort ett flytande fält (FT1: `t=flyt` står kvar)
//   * `placeChild` ger ett flytande fält på väg ut huvudytan (FT1: ytan)
//   * `turnIfNeeded` borttagen (FT7: bredd och höjd; FT3b: höjden 160)
//   * `ft=` tolkas utan namnkontrollen mot `p=`/`t=` (FT4)
//   * `fitWanted` aldrig sann (FT9: stilbytet i det SYNLIGA fönstret passas
//     aldrig)
//   * `syncVisible` passar inte fönstret före visningen (FT9: stilbytet
//     medan det var dolt)
//   * `~ToolBar` säger inget till värden (FT8: en krasch)
//   * `moveBar` utan insättning, eller `applyTool` med `ownsBar` i stället
//     för `mine` (FT10, LV6: ingen platshållare / släppet dockar inte)
//   * `layoutIn` mäter ett flytande fält utan grepp (FT10: platshållaren är
//     en greppbredd kortare än slutgeometrin)
//   * `closeFloatingBar` döljer inte fältet (FT6)
//   (flytande verktygsfält, fokus och postning:)
//   * `ToolButton::setText`/`setImg` utan `relayoutSiblings`, kroken bort ur
//     `relayoutSiblings`, eller `ToolBar::childLayoutChanged` tom (FT9: det
//     dolda fönstret passas aldrig — tidsgränsen)
//   * `fitToolBar` sätter innehållsytan men inte gränserna (FT9: `låst`)
//   * rubrikens ✕ eller dubbelklick frågar inte `win_.bar()` (FT8: en
//     krasch, eller värdens onChange)
//   * `itemDestroyed` glömmer inte fokus (FT8, RE: tangenten i luckan når
//     det döda fältet — en krasch)
//   * pumpen kör de postade FÖRE OS:ets händelser (FT8, RE: kontrollen ser
//     ingen pekarposition och ingen hållen tangent)
//   (verktygsfältets kanter, del TE:)
//   * filtret borttaget i `toolDropAt` (TE1: vänsterkanten dockar; TE5)
//   * filtret också i band med en TILLÅTEN kant (TE2: omordningen nekas)
//   * kanterna filtrerade i `writeBackBars` (TE4: appens `dock` nekas)
//   * `setAllowedEdges` tar med `Fill` (TE6)
//   (PR och IF, den tomma rekten:)
//   * PR1–PR2 en inaktiv flik i en grupp till höger: värdens hörn är
//     enfärgat i full och partiell bild (partiell enligt profileraren, annars
//     en SKIP-rad). Sabotage: spärren bort i `drawTree` (PR1) resp.
//     `drawTreePartial` (PR2) — ▾, nålen och ✕ syns i hörnet.
//   * IF en värd på en flikkontrolls ovalda sida, en flytt som väntar på en
//     öppen popup: när popupen stängts görs flytten vid nästa bild.
//     IF med en full bild, IF2 med en bild där bara popupen invaliderats
//     (skadan klipps bort — ingen ritvandring alls).
//     Sabotage: `DockHost::frameStep` tillbaka in i `draw()` (IF, IF2:
//     panelen står kvar); `stepTree` bort och steget i ritvandringen där
//     skadan skär widgeten (IF2).
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * hur panelerna, flikarna och handtagen SER UT utöver "rätt yta ändras"
//     — utseendet ögnas med --png, och pixel_gate mäter att varje panel ritar
//     i varje palett. F16 mäter ringens FÄRGPUNKT i den UI som gäller, inte
//     dess form i Glaze och Minimalistic, och inte handtagsgrenen (ett
//     handtagsklick flyttar fokus in i panelen, så ringen syns inte där).
//   * backendernas egna mus-events — grinden matar ingången direkt.
//   * loggtexten (`gfx::log` har ingen fångst) — texten är
//     verifierad genom läsning av dock_string.cpp.
//   * att glidningen SER mjuk ut, och timerns takt — bara att prövningen
//     levereras inom två sekunder och att glidningen ligger mellan och slutar.
//   * rubrikmenyn som popup (en meny öppnas inte i grinden) och att
//     lämna-prövningen väntar medan en popup i overlayn är öppen.
//   * en `setLayout`/`dockPanel` som står FÖRE trädbygget i en handskriven
//     `.glazeui`: previewn bygger trädet före satserna och utför den ändå,
//     appen kör källordning och loggar panelerna som okända. Det är en skriven
//     begränsning (dock_host.h), inte något den här grinden kan fånga.
//   * FLYTANDE: verkliga drag mellan skärmar och blandad DPI, placeringen på
//     macOS, X11 och Wayland (TESTS.md), att OS:et respekterar
//     `setPosition` under en fönsterhanterare, rubrikmenyns Flytande/Docka
//     som popup (samma `userFloat` som dubbelklicket), och dubbelklicket i
//     det FLYTANDE fönstret. FL6, LV3, LV10, LV11, TOM, FT10a/b och ±1
//     px-jämförelserna körs bara med D3D11 (postade Win32-meddelanden).
//   * LEVANDE DRAG: riktig mus och verkliga OS-capture-förlopp — de postade
//     meddelandena når WndProc men inte `SetCapture`-mekanikens routning (vart
//     OS:et skickar rörelserna efter ett övertagande antar FT10b);
//     ÖVERTAGANDET bara där dess förutsättning går att ställa: FT10a när
//     grindens tråd inte äger förgrunden (alla fönster dolda), FT10b och LV10
//     när huvudfönstret går att göra till förgrunden — en session utan
//     förgrund hoppar över dem (77); avbrottskrokar i ANDRA fönster under
//     övertagandet som river värden, källan eller draget (
//     återvalideringen är läst, inte körd); en ritning MITT i
//     släppet (en flytt till en skärm med annan DPI); fall B:s avbrott
//     för ett FÄLTS rubrik (LV11 mäter panelens — samma `itemPressLost`);
//     landningens lyft (LV12 har dolda fönster, som inte lyfts); att fönstret
//     SER ut att följa mjukt; darr på X11; macOS och X11 överhuvudtaget utöver
//     det plattformsoberoende; Wayland bara som LV5; känslan (testas för hand).

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>   // gfx::ticksMs — väntan
#include <glaze/glaze.h>

#ifdef GLAZE_BACKEND_D3D11
// Samma vakter som backendens d3d11_backend.h (som i tray_gate):
// MinGW:s libstdc++ definierar redan NOMINMAX.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

int         g_fails   = 0;
int         g_cases   = 0;
int         g_skips   = 0;
bool        g_verbose = false;
std::string g_pngDir;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    std::fflush(stdout);
    ++g_fails;
}

bool nära(float x, float y)
{
    return x - y < 0.01f && y - x < 0.01f;
}

bool lika(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return nära(a.x, b.x) && nära(a.y, b.y) && nära(a.w, b.w) &&
           nära(a.h, b.h);
}

bool tom(const gfx::Rectf& r)
{
    return r.w <= 0.0f && r.h <= 0.0f;
}

// BITVIS lika — förhandsvisningen och släppet räknas ur samma vikter och ska
// ge exakt samma tal, inte bara nästan.
bool exakt(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool ändlig(const gfx::Rectf& r)
{
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) &&
           std::isfinite(r.h);
}

// En pixel (RGBA packad som `Vard::pixel` ger den) i temats färg `c` — inom
// två steg per kanal, för blandningens avrundning. Referensen är TEMAT, inte en
// pixel ur produktionen.
bool somFärg(std::uint32_t px, gfx::Color c)
{
    auto nära8 = [](unsigned a, unsigned b) {
        return (a > b ? a - b : b - a) <= 2u;
    };
    return nära8((px >> 24) & 0xFFu, c.r) && nära8((px >> 16) & 0xFFu, c.g) &&
           nära8((px >> 8) & 0xFFu, c.b);
}

std::string rekt(const gfx::Rectf& r)
{
    char b[96];
    std::snprintf(b, sizeof b, "{%.2f, %.2f, %.2f, %.2f}",
                  static_cast<double>(r.x), static_cast<double>(r.y),
                  static_cast<double>(r.w), static_cast<double>(r.h));
    return b;
}

// Ett tal ur en sparad sträng, läst med grindens EGEN läsning
// (`std::from_chars`) och inte med produktionens tolkare — NaN om stycket
// inte är ett helt tal.
float läsTal(const std::string& s)
{
    float      v = 0.0f;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size()
               ? v
               : std::numeric_limits<float>::quiet_NaN();
}

// Alla `c=`-vikter i en sparad sträng, i ordning.
std::vector<float> vikterI(const std::string& s)
{
    std::vector<float> ut;
    std::size_t        p = 0;
    while ((p = s.find(";c=", p)) != std::string::npos) {
        p += 3;
        const std::size_t slut = s.find(';', p);
        ut.push_back(läsTal(s.substr(
            p, slut == std::string::npos ? std::string::npos : slut - p)));
    }
    return ut;
}

// Panelens BREDD i en sparad sträng — sjätte fältet i `p=namn,…`.
float breddI(const std::string& s, const std::string& namn)
{
    const std::string nyckel = ";p=" + namn + ",";
    std::size_t       p      = s.find(nyckel);
    if (p == std::string::npos)
        return std::numeric_limits<float>::quiet_NaN();
    p += nyckel.size();
    for (int flagga = 0; flagga < 4 && p != std::string::npos; ++flagga) {
        p = s.find(',', p);
        if (p != std::string::npos)
            ++p;
    }
    if (p == std::string::npos)
        return std::numeric_limits<float>::quiet_NaN();
    return läsTal(s.substr(p, s.find(',', p) - p));
}

// ------------------------------------------------------ falsk font
// Fast teckensteg per BYTE — titlarna i layoutfallen är ASCII (tab_gate).
class StegFont final : public gfx::NullFont {
public:
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{8.0f * float(s.size()), 16.0f};
    }
    float height() const override { return 16.0f; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string&, std::size_t b) const override
    {
        return 8.0f * float(b);
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        return fitWidth(s, x + 4.0f);
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        const auto n = std::size_t(std::max(0.0f, maxW) / 8.0f);
        return std::min(n, s.size());
    }
};

StegFont g_font;

// ================================================================== LAYOUT

struct Yta final : glaze::Widget {
    Yta() { anchors = {true, true, true, true}; }
    void draw(const glaze::Ctx&) override {}
};

glaze::Ctx ctxFör(const glaze::Style& st, float scale, const gfx::Rectf& area)
{
    glaze::Ctx c{};
    c.font  = &g_font;
    c.scale = scale;
    c.ui    = glaze::UI::Slate;
    c.style = &st;
    c.theme = &glaze::Theme::slateDefault().light;
    c.area  = area;
    return c;
}

glaze::Ctx barnCtx(const glaze::Widget& förälder, const glaze::Ctx& c)
{
    glaze::Ctx cc = c;
    cc.area       = förälder.childArea(c);
    return cc;
}

// Två paneler till vänster och en överst, i barnordning, och ett innehåll.
struct Rigg {
    Yta              rot;
    glaze::DockHost  host;
    glaze::DockPanel a{"Alfa"}, b{"Beta"}, c{"Gamma"};
    glaze::Panel     mitt;

    Rigg()
    {
        host.anchors = {true, true, true, true};
        rot.adopt(host);
        a.name   = "a";
        a.width  = 150;
        b.name   = "b";
        b.width  = 150;
        c.name   = "c";
        c.dock   = glaze::Dock::Top;
        c.height = 100;
        host.adopt(a);
        host.adopt(b);
        host.adopt(c);
        host.adopt(mitt);
    }

    glaze::Ctx värd(const glaze::Ctx& rc) { return barnCtx(rot, rc); }
    gfx::Rectf rekt(const glaze::Widget& w, const glaze::Ctx& rc)
    {
        return w.resolve(barnCtx(host, värd(rc)));
    }
    // Delen under en LOGISK punkt i värden, avläst med dess tooltip: ett
    // handtag och en flik svarar med panelens titel, allt annat tomt.
    std::string tipsVid(const glaze::Ctx& rc, float lx, float ly)
    {
        const glaze::Ctx   hc = värd(rc);
        const gfx::Rectf   r  = host.resolve(hc);
        const glaze::TrText t =
            host.toolTipAt(hc, r.x + lx * rc.scale, r.y + ly * rc.scale);
        return t.str();
    }
};

void fallLayout()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const float         t  = st.splitterThick;   // indata, inte svar
    const float         TH = st.tabHeaderH;
    const float         HH = st.dockHandleH;
    const float         PX = st.dockHandlePadX;
    const auto          c  = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});

    // L1 — SKÄRORDNINGEN är barnordningen: a ytterst till vänster, b
    // innanför, c överst i det som är kvar. Skiktet ligger mellan varje remsa
    // och resten, och huvudytan får resten.
    {
        Rigg r;
        check(lika(r.rekt(r.a, c), {0, 0, 150, 400}) &&
                  lika(r.rekt(r.b, c), {150 + t, 0, 150, 400}),
              "L1: a ytterst, b innanför — full höjd",
              rekt(r.rekt(r.a, c)) + " " + rekt(r.rekt(r.b, c)));
        check(lika(r.rekt(r.c, c), {300 + 2 * t, 0, 300 - 2 * t, 100}),
              "L1: c överst i resten", rekt(r.rekt(r.c, c)));
        check(lika(r.rekt(r.mitt, c),
                   {300 + 2 * t, 100 + t, 300 - 2 * t, 300 - t}),
              "L1: huvudytan — resten, innanför alla tre skikten",
              rekt(r.rekt(r.mitt, c)));
    }

    // L2 — TVÅ CELLER 1:1: b staplad under a (längs den stående remsan) —
    // målets vikt delas lika, och skiktet mellan cellerna tar sin tjocklek.
    {
        Rigg r;
        r.host.dockPanel(r.b, r.a, glaze::Dock::Bottom);
        const float halv = (400 - t) / 2;
        check(r.b.dock == glaze::Dock::Left,
              "L2: b fick remsans kant tillbakaskriven");
        check(lika(r.rekt(r.a, c), {0, 0, 150, halv}) &&
                  lika(r.rekt(r.b, c), {0, halv + t, 150, halv}),
              "L2: två celler 1:1 med ett skikt emellan",
              rekt(r.rekt(r.a, c)) + " " + rekt(r.rekt(r.b, c)));
        check(lika(r.rekt(r.c, c), {150 + t, 0, 450 - t, 100}),
              "L2: c tar bredden b lämnade", rekt(r.rekt(r.c, c)));
    }

    // L3 — 2:1 ur strängen: vikterna fördelar längden.
    {
        Rigg r;
        check(r.host.setLayout("v2;s=left;c=2;p=a,1,0,1,0,150,0;c=1;"
                               "p=b,1,0,1,0,150,0;s=top;c=1;p=c,1,0,1,0,0,100"),
              "L3: setLayout tar en giltig sträng");
        const float rum = 400 - t;
        check(lika(r.rekt(r.a, c), {0, 0, 150, rum * 2 / 3}) &&
                  lika(r.rekt(r.b, c), {0, rum * 2 / 3 + t, 150, rum / 3}),
              "L3: vikterna 2:1 — a två tredjedelar",
              rekt(r.rekt(r.a, c)) + " " + rekt(r.rekt(r.b, c)));
    }

    // L4 — MIN/MAX: ett minimum på a:s höjd tvingar en omfördelning (a låses,
    // b tar resten), ett maximum på a:s bredd klampar remsans tjocklek, och
    // ett minimum som krockar med maximumet VINNER.
    {
        Rigg r;
        r.host.dockPanel(r.b, r.a, glaze::Dock::Bottom);
        r.a.setMinimumSize(0, 300);
        check(lika(r.rekt(r.a, c), {0, 0, 150, 300}) &&
                  lika(r.rekt(r.b, c), {0, 300 + t, 150, 400 - t - 300}),
              "L4: a:s minimum 300 — a låses, b får resten",
              rekt(r.rekt(r.a, c)) + " " + rekt(r.rekt(r.b, c)));
        r.a.setMaximumSize(120, 0);
        check(nära(r.rekt(r.a, c).w, 120) && nära(r.rekt(r.b, c).w, 120),
              "L4: a:s maximum 120 klampar hela remsans tjocklek",
              rekt(r.rekt(r.a, c)));
        r.b.setMinimumSize(130, 0);
        check(nära(r.rekt(r.a, c).w, 130),
              "L4: b:s minimum 130 krockar med a:s maximum 120 — min vinner",
              rekt(r.rekt(r.a, c)));
        r.a.setMinimumSize(-1, 0);   // loggas och ändrar ingenting
        check(r.a.minimumSize().h == 300.0f,
              "L4: ett negativt mått ignoreras");
    }

    // L5 — FLIKRADEN bara vid minst två öppna flikar, och den INAKTIVA
    // fliken får en TOM yta (den döljs inte — `isVisible()` står kvar).
    {
        Rigg r;
        r.host.dockPanel(r.b, r.a, glaze::Dock::Fill);
        check(lika(r.rekt(r.b, c), {0, 0, 150, 400 - TH}),
              "L5: den aktiva fliken (b) — cellen minus flikraden nederst",
              rekt(r.rekt(r.b, c)));
        check(tom(r.rekt(r.a, c)) && r.a.isVisible() &&
                  !r.a.showsChildren() && r.b.showsChildren(),
              "L5: den inaktiva (a) har en tom yta men är fortfarande öppen",
              rekt(r.rekt(r.a, c)));
        check(r.tipsVid(c, 20, 400 - TH / 2) == "Alfa" &&
                  r.tipsVid(c, 130, 400 - TH / 2) == "Beta",
              "L5: flikarna står lika breda i raden (tooltip = titeln)");
        r.b.hide();
        check(lika(r.rekt(r.a, c), {0, 0, 150, 400}) && r.a.showsChildren(),
              "L5: b stängd — en öppen flik kvar, ingen flikrad",
              rekt(r.rekt(r.a, c)));
    }

    // L6 — STÄNGD: en stängd panel skär ingen remsa.
    {
        Rigg r;
        r.a.hide();
        check(lika(r.rekt(r.b, c), {0, 0, 150, 400}),
              "L6: a stängd — b ytterst", rekt(r.rekt(r.b, c)));
    }

    // L7 — INFÄLLD: ett HANDTAG i en remsa vid kanten, YTTERST — utanför
    // alla panelremsor, också de som skärs före i ordningen. Ett liggande
    // handtag är titeln med luft; ett stående en kvadrat.
    {
        Rigg r;
        r.c.setCollapsed(true);
        check(lika(r.rekt(r.a, c), {0, HH, 150, 400 - HH}) &&
                  lika(r.rekt(r.mitt, c), {300 + 2 * t, HH, 300 - 2 * t, 400 - HH}),
              "L7: c infälld — handtagsremsan överst, YTTERST (a börjar under)",
              rekt(r.rekt(r.a, c)) + " " + rekt(r.rekt(r.mitt, c)));
        const float bredd = 2 * PX + 8 * 5;   // "Gamma" i den falska fonten
        check(r.tipsVid(c, bredd - 1, HH / 2) == "Gamma" &&
                  r.tipsVid(c, bredd + st.dockHandleGap + 1, HH / 2).empty(),
              "L7: handtaget är titelns bredd plus luften");
        r.a.setCollapsed(true);
        check(lika(r.rekt(r.b, c), {HH, HH, 150, 400 - HH}),
              "L7: a infälld — ett stående handtag till vänster, under det "
              "liggande", rekt(r.rekt(r.b, c)));
        check(r.tipsVid(c, HH / 2, HH + HH / 2) == "Alfa" &&
                  r.tipsVid(c, HH / 2, HH + HH + st.dockHandleGap + 1).empty(),
              "L7: det stående handtaget är en kvadrat med titeln som tooltip");
    }

    // L8 — SKALA 2: logiskt samma, dubbla fysiska px.
    {
        Rigg       r;
        const auto c2 = ctxFör(st, 2.0f, gfx::Rectf{0, 0, 1200, 800});
        check(lika(r.rekt(r.a, c2), {0, 0, 300, 800}) &&
                  lika(r.rekt(r.b, c2), {(150 + t) * 2, 0, 300, 800}),
              "L8: skala 2 — 150 logiska px blir 300 fysiska",
              rekt(r.rekt(r.a, c2)) + " " + rekt(r.rekt(r.b, c2)));
    }

    // L9 — UTAN VÄRD är panelen en vanlig dockad remsa (Widget::resolve):
    // hopfälld vid Left är den rubrikens höjd bred, vid Top lika hög.
    {
        const float      H = st.dockHeaderH;
        Yta              rot;
        glaze::DockPanel p("Fri");
        rot.adopt(p);
        p.setCollapsed(true);
        check(nära(p.resolve(barnCtx(rot, c)).w, H) && p.focusable() &&
                  !p.showsChildren(),
              "L9: utan värd — hopfälld vänsterpanel är rubrikens höjd och "
              "ett tabbstopp", rekt(p.resolve(barnCtx(rot, c))));
        p.dock = glaze::Dock::Top;
        check(nära(p.resolve(barnCtx(rot, c)).h, H),
              "L9: utan värd — hopfälld topp-panel är rubrikens höjd",
              rekt(p.resolve(barnCtx(rot, c))));
    }

    // L10 — STORA VIKTER: 3e38 är ett
    // giltigt float och en giltig vikt, men två sådana summerade i float är
    // oändligt, och `rum · vikt` likaså. Lika vikter ger lika celler; 3e38
    // mot 1 ger a nästan allt — och båda ytorna är ändliga och ryms i remsan.
    {
        Rigg        r;
        const float rum = 400 - t;
        check(r.host.setLayout("v2;s=left;c=3e38;p=a,1,0,1,0,150,0;c=3e38;"
                               "p=b,1,0,1,0,150,0;s=top;c=1;p=c,1,0,1,0,0,100"),
              "L10: setLayout tar vikten 3e38");
        check(lika(r.rekt(r.a, c), {0, 0, 150, rum / 2}) &&
                  lika(r.rekt(r.b, c), {0, rum / 2 + t, 150, rum / 2}),
              "L10: två vikter 3e38 — två lika celler",
              rekt(r.rekt(r.a, c)) + " " + rekt(r.rekt(r.b, c)));
        Rigg r2;
        check(r2.host.setLayout("v2;s=left;c=3e38;p=a,1,0,1,0,150,0;c=1;"
                                "p=b,1,0,1,0,150,0;s=top;c=1;p=c,1,0,1,0,0,100"),
              "L10: setLayout tar 3e38 mot 1");
        const gfx::Rectf ra = r2.rekt(r2.a, c), rb = r2.rekt(r2.b, c);
        check(ändlig(ra) && ändlig(rb) && nära(ra.h, rum) && rb.h >= 0.0f &&
                  rb.y + rb.h <= 400.0f + 0.01f,
              "L10: 3e38 mot 1 — a tar remsan, båda ändliga och inom den",
              rekt(ra) + " " + rekt(rb));
    }

    // L11 — HALVERINGEN: en stapling halverar målets vikt, och 300
    // staplingar i rad (a och b byter plats) hade gett 2^-300 — en nolla i
    // float, som `layout()` skrev och `setLayout()` avvisade. Remsan
    // normaliseras före varje delning, så vikterna förblir 0.5 + 0.5.
    {
        Rigg r;
        for (int i = 0; i < 150; ++i) {
            r.host.dockPanel(r.b, r.a, glaze::Dock::Bottom);
            r.host.dockPanel(r.a, r.b, glaze::Dock::Bottom);
        }
        const std::string s = r.host.layout();
        check(s == "v2;s=left;c=0.5;p=b,1,0,1,0,150,0;c=0.5;p=a,1,0,1,0,150,0;"
                   "s=top;c=1;p=c,1,0,1,0,0,100",
              "L11: 300 staplingar — vikterna är fortfarande 0.5 och 0.5", s);
        Rigg r2;
        check(r2.host.setLayout(s), "L11: …och strängen går att läsa", s);
    }
}

// =================================================================== BANDEN
//
// VERKTYGSFÄLTEN: tre liggande fält — två i band 0 och
// ett i band 1 överst — ett stående vid vänsterkanten, en panel och ett
// innehåll. Räknat för hand ur stilens mått (indata) och den falska
// fontens 8 px per byte och 16 px höjd: en textknapp är texten + 2 ×
// `toolButtonPadX` bred och 16 + 2 × `toolButtonPadY` hög, och ett fält i en
// värd är greppet + 2 × `toolBarPadX` + barnen långt.

struct BandRigg {
    Yta               rot;
    glaze::DockHost   host;
    glaze::ToolBar    t1, t2, t3, v1;
    glaze::ToolButton k1{"AB"}, k2{"CDE"}, k3{"F"}, kv{"G"};
    glaze::DockPanel  p{"Panel"};
    glaze::Panel      mitt;
    glaze::ToolBar    extra, extra2;   // adopteras under körning (B3)

    // `fältEfter` = previewns ordning: trädet byggs FÖRST med förvalen, och
    // fälten (`setBand`, `dock`) sätts efteråt — appen sätter dem före.
    explicit BandRigg(bool fältEfter = false)
    {
        host.anchors = {true, true, true, true};
        rot.adopt(host);
        t1.name     = "t1";
        t2.name     = "t2";
        t3.name     = "t3";
        v1.name     = "v1";
        p.name      = "p";
        p.width     = 100;
        extra.name  = "extra";
        extra2.name = "extra2";
        auto fält = [this] {
            t3.setBand(1);
            v1.dock = glaze::Dock::Left;
        };
        if (!fältEfter)
            fält();
        host.adopt(t1);
        t1.adopt(k1);
        host.adopt(t2);
        t2.adopt(k2);
        host.adopt(t3);
        t3.adopt(k3);
        host.adopt(v1);
        v1.adopt(kv);
        host.adopt(p);
        host.adopt(mitt);
        if (fältEfter)
            fält();
    }

    gfx::Rectf rekt(const glaze::Widget& w, const glaze::Ctx& rc)
    {
        return w.resolve(barnCtx(host, barnCtx(rot, rc)));
    }
};

void fallBand()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const float         t  = st.splitterThick;   // indata, inte svar
    const float         TH = st.toolBarH;
    const float         G  = st.toolGap;
    const float         GW = st.toolGripW;
    const float         PX = st.toolBarPadX;
    const float         BX = st.toolButtonPadX;
    const float         BY = st.toolButtonPadY;
    const auto          c  = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});
    // Fältens längder (liggande ur knappens bredd, stående ur dess höjd).
    const float L1 = GW + 2 * PX + (8 * 2 + 2 * BX);    // "AB"
    const float L2 = GW + 2 * PX + (8 * 3 + 2 * BX);    // "CDE"
    const float L3 = GW + 2 * PX + (8 * 1 + 2 * BX);    // "F"
    const float LV = GW + 2 * PX + (16 + 2 * BY);       // "G", stående
    const float LT = GW + 2 * PX;                       // utan barn

    // B1 — BANDEN skärs YTTERST: Top band 0 (t1, t2 med luckan), Top band 1
    // (t3), därefter Left band 0 (v1 stående, toolBarH bred) — och först
    // därefter panelens remsa och huvudytan.
    {
        BandRigg r;
        check(lika(r.rekt(r.t1, c), {0, 0, L1, TH}) &&
                  lika(r.rekt(r.t2, c), {L1 + G, 0, L2, TH}),
              "B1: två fält i band 0 överst, packade med luckan",
              rekt(r.rekt(r.t1, c)) + " " + rekt(r.rekt(r.t2, c)));
        check(lika(r.rekt(r.t3, c), {0, TH, L3, TH}),
              "B1: band 1 innanför band 0", rekt(r.rekt(r.t3, c)));
        check(r.v1.isVertical() &&
                  lika(r.rekt(r.v1, c), {0, 2 * TH, TH, LV}),
              "B1: vänsterbandet efter de liggande — fältet STÅR",
              rekt(r.rekt(r.v1, c)));
        check(lika(r.rekt(r.p, c), {TH, 2 * TH, 100, 400 - 2 * TH}) &&
                  lika(r.rekt(r.mitt, c), {TH + 100 + t, 2 * TH,
                                           600 - TH - 100 - t, 400 - 2 * TH}),
              "B1: panelremsan och huvudytan innanför ALLA band",
              rekt(r.rekt(r.p, c)) + " " + rekt(r.rekt(r.mitt, c)));
        check(r.host.layout() == "v2;s=left;c=1;p=p,1,0,1,0,100,0;"
                                 "t=t1,top,0,0,0;t=t2,top,0,1,0;"
                                 "t=t3,top,1,0,0;t=v1,left,0,0,0",
              "B1: layout() bär fälten — kant, band, plats, dold",
              r.host.layout());
    }

    // B2 — ett DOLT fält skär inget: t2 dold lämnar t1 orört, och med båda
    // dolda försvinner band 0 och band 1 flyttar ut. Bandindexen står kvar.
    {
        BandRigg r;
        r.t2.hide();
        check(lika(r.rekt(r.t1, c), {0, 0, L1, TH}) && tom(r.rekt(r.t2, c)),
              "B2: t2 dold — ingen yta, t1 orört", rekt(r.rekt(r.t2, c)));
        r.t1.hide();
        check(lika(r.rekt(r.t3, c), {0, 0, L3, TH}) &&
                  lika(r.rekt(r.v1, c), {0, TH, TH, LV}),
              "B2: båda dolda — band 0 skär inget, band 1 och vänsterbandet "
              "flyttar ut",
              rekt(r.rekt(r.t3, c)) + " " + rekt(r.rekt(r.v1, c)));
        check(r.host.layout() == "v2;s=left;c=1;p=p,1,0,1,0,100,0;"
                                 "t=t1,top,0,0,1;t=t2,top,0,1,1;"
                                 "t=t3,top,1,0,0;t=v1,left,0,0,0",
              "B2: de dolda behåller band och plats i strängen",
              r.host.layout());
    }

    // B3 — ADOPT UNDER KÖRNING: sist i sitt band; ett band utanför antalet
    // blir ett nytt innersta.
    {
        BandRigg r;
        r.host.adopt(r.extra);
        check(lika(r.rekt(r.extra, c), {L1 + G + L2 + G, 0, LT, TH}),
              "B3: ett fält adopterat under körning hamnar SIST i band 0",
              rekt(r.rekt(r.extra, c)));
        r.extra2.setBand(7);
        r.host.adopt(r.extra2);
        check(lika(r.rekt(r.extra2, c), {0, 2 * TH, LT, TH}) &&
                  lika(r.rekt(r.v1, c), {0, 3 * TH, TH, LV}),
              "B3: band 7 av 2 — ett nytt innersta band, vänsterbandet "
              "flyttar in",
              rekt(r.rekt(r.extra2, c)) + " " + rekt(r.rekt(r.v1, c)));
        check(r.extra2.band() == 7,
              "B3: fältets `band()` står kvar — värden skriver bara tillbaka "
              "efter en användarändring");
    }

    // B4 — `t=` TUR OCH RETUR, och en sträng som ordnar om: t2 före t1, v1
    // till band 1 överst (LIGGANDE — dess satta bredd 34 blir höjden, så
    // tjockleken förblir tjocklek) och t3 till vänsterkanten (STÅENDE).
    {
        BandRigg          r;
        const std::string s = r.host.layout();
        BandRigg          r2;
        check(r2.host.setLayout(s) && r2.host.layout() == s,
              "B4: setLayout(layout()) ger samma sträng", r2.host.layout());
        BandRigg r3;
        r3.v1.width = 34;
        check(r3.host.setLayout("v2;t=t2,top,0,0,0;t=t1,top,0,1,0;"
                                "t=v1,top,1,0,0;t=t3,left,0,0,0"),
              "B4: en sträng som ordnar om läses");
        check(r3.v1.dock == glaze::Dock::Top && r3.v1.band() == 1 &&
                  r3.v1.height == 34.0f && r3.v1.width == 0.0f &&
                  r3.t3.dock == glaze::Dock::Left && r3.t3.band() == 0,
              "B4: kant och band tillbakaskrivna, och v1:s bredd blev höjd");
        const float LVh = GW + 2 * PX + (8 * 1 + 2 * BX);   // "G" liggande
        check(lika(r3.rekt(r3.t2, c), {0, 0, L2, TH}) &&
                  lika(r3.rekt(r3.t1, c), {L2 + G, 0, L1, TH}),
              "B4: t2 först i band 0, t1 efter",
              rekt(r3.rekt(r3.t2, c)) + " " + rekt(r3.rekt(r3.t1, c)));
        check(lika(r3.rekt(r3.v1, c), {0, TH, LVh, 34}) &&
                  lika(r3.rekt(r3.t3, c), {0, TH + 34, TH, LV}),
              "B4: v1 ligger i band 1 med tjockleken 34, t3 står vid "
              "vänsterkanten",
              rekt(r3.rekt(r3.v1, c)) + " " + rekt(r3.rekt(r3.t3, c)));
        check(r3.host.layout() == "v2;s=left;c=1;p=p,1,0,1,0,100,0;"
                                  "t=t2,top,0,0,0;t=t1,top,0,1,0;"
                                  "t=v1,top,1,0,0;t=t3,left,0,0,0",
              "B4: …och layout() säger den nya ordningen", r3.host.layout());
    }

    // B5 — GLESA BANDNYCKLAR packas: band 5 där bara band 0 finns blir band
    // 1 i geometrin och i strängen, och fältets eget tal står kvar.
    {
        BandRigg r;
        r.t3.setBand(5);
        check(lika(r.rekt(r.t3, c), {0, TH, L3, TH}) && r.t3.band() == 5 &&
                  r.host.layout().find(";t=t3,top,1,0,0") != std::string::npos,
              "B5: band 5 av 1 — index 1, och strängen skriver det packade",
              r.host.layout());
    }
    // …och ett UTELÄMNAT fält med en gles nyckel:
    // t2 är namnlöst i band 5 och t3 står i band 7. Strängen
    // skriver t1 i band 0 och t3 i band 2 och utelämnar t2 — och
    // återläsningen får inte låta t3 gå före t2 (den glesa nyckeln 5 mot
    // strängens packade 2).
    {
        BandRigg r;
        r.t2.name = "";
        r.t2.setBand(5);
        r.t3.setBand(7);
        const std::string s     = r.host.layout();
        const gfx::Rectf  före2 = r.rekt(r.t2, c);
        const gfx::Rectf  före3 = r.rekt(r.t3, c);
        check(s.find(";t=t3,top,2,0,0") != std::string::npos &&
                  s.find("t=t2") == std::string::npos &&
                  lika(före2, {0, TH, L2, TH}) && lika(före3, {0, 2 * TH, L3, TH}),
              "B5: t2 utelämnas, t3 skrivs i band 2 — t2 står i band 1", s);
        check(r.host.setLayout(s) && r.host.layout() == s &&
                  lika(r.rekt(r.t2, c), före2) && lika(r.rekt(r.t3, c), före3),
              "B5: setLayout(layout()) med ett utelämnat glest fält — banden "
              "byter inte plats",
              r.host.layout() + " " + rekt(r.rekt(r.t3, c)));
    }

    // B6 — PREVIEWNS ORDNING: fälten satta EFTER trädbygget ger samma band
    // och samma ytor som appens, som sätter dem före.
    {
        BandRigg app(false), preview(true);
        check(preview.host.layout() == app.host.layout() &&
                  lika(preview.rekt(preview.t3, c), app.rekt(app.t3, c)) &&
                  lika(preview.rekt(preview.v1, c), app.rekt(app.v1, c)),
              "B6: `setBand`/`dock` efter adopt — samma layout som före",
              preview.host.layout() + " mot " + app.host.layout());

        // …och ett fält som ADOPTERAS UTAN KANT:
        // `a.dock = None` före adopt i appen, `a.dock = Top` efter
        // — previewn har förvalet Top vid adoptionen och sätter båda raderna
        // efter trädbygget. Båda ska ge (a, b): adoptionsplatsen står kvar.
        struct Ordning {
            Yta             rot;
            glaze::DockHost host;
            glaze::ToolBar  a, b;
            explicit Ordning(bool fältEfter)
            {
                host.anchors = {true, true, true, true};
                rot.adopt(host);
                a.name = "a";
                b.name = "b";
                if (!fältEfter)
                    a.dock = glaze::Dock::None;
                host.adopt(a);
                host.adopt(b);
                if (fältEfter)
                    a.dock = glaze::Dock::None;
                a.dock = glaze::Dock::Top;   // raden EFTER trädbygget
            }
        };
        Ordning oa(false), op(true);
        check(oa.host.layout() == "v2;t=a,top,0,0,0;t=b,top,0,1,0" &&
                  op.host.layout() == oa.host.layout(),
              "B6: ett fält som fick sin kant efter adopt behåller sin plats — "
              "samma ordning i appen och previewn",
              oa.host.layout() + " mot " + op.host.layout());
    }

    // B7 — ETT ÖVERFULLT STÅENDE FÄLT KORTAS vid bandets slut:
    // v1 med den satta längden 500 i ett
    // vänsterband som slutar där bandet vid nederkanten börjar — och därefter
    // KRYMPER bandet fälten bakifrån: `extra` efter det (tomt, sin
    // egen längd LT, som är kortare än greppet + luften + överflödsknappen)
    // står kvar i sin längd vid bandets slut, och v1 tar resten.
    // Ytan är
    // också träffprovets.
    {
        BandRigg r;
        r.t3.dock    = glaze::Dock::Bottom;   // band 0 vid nederkanten
        r.v1.height  = 500;                   // stående: höjden är längden
        r.extra.dock = glaze::Dock::Left;
        r.host.adopt(r.extra);                // efter v1 i vänsterbandet
        const float längd = 400 - 2 * TH;     // Top-bandet och Bottom-bandet
        check(lika(r.rekt(r.t3, c), {0, 400 - TH, L3, TH}) &&
                  lika(r.rekt(r.v1, c), {0, TH, TH, längd - G - LT}),
              "B7: v1 kortas till det som blir över vid vänsterbandets slut — "
              "inte in i bandet vid nederkanten",
              rekt(r.rekt(r.v1, c)) + " " + rekt(r.rekt(r.t3, c)));
        check(lika(r.rekt(r.extra, c), {0, TH + längd - LT, TH, LT}),
              "B7: fältet efter det överfulla behåller sin längd vid bandets "
              "slut (bandet krymper bakifrån)",
              rekt(r.rekt(r.extra, c)));
    }

    // B8 — BANDET KRYMPER FÄLTEN BAKIFRÅN ned till greppet, luften
    // och överflödsknappen (`KORT`), så varje fält har sin knapp: i ett band
    // tio px för kort krymper t2 med tio; fyra px kortare än t1 + t2 i sin
    // kortaste längd krymper t2 till `KORT` och t1 med resten. Utan överflöde
    // (`setOverflow(false)`) oförändrat: t1 i sin längd och t2 kortat vid
    // bandets slut. Och knappen i ett fält i sin kortaste längd är hela dess
    // innerlängd, med knappen i fältet dold.
    {
        const float KORT = GW + 2 * PX + st.toolOverflowW;
        check(L2 - 10 > KORT && L1 - 4 >= KORT,
              "B8: facit — fälten är längre än sin kortaste längd");
        BandRigg         r;
        const glaze::Ctx c88 =
            ctxFör(st, 1.0f, gfx::Rectf{0, 0, L1 + G + L2 - 10, 400});
        check(lika(r.rekt(r.t1, c88), {0, 0, L1, TH}) &&
                  lika(r.rekt(r.t2, c88), {L1 + G, 0, L2 - 10, TH}),
              "B8: tio px för kort — t2 krymper med tio, t1 står kvar",
              rekt(r.rekt(r.t1, c88)) + " " + rekt(r.rekt(r.t2, c88)));
        const float      bandet = L1 + G + KORT - 4;
        const glaze::Ctx ck = ctxFör(st, 1.0f, gfx::Rectf{0, 0, bandet, 400});
        check(lika(r.rekt(r.t1, ck), {0, 0, L1 - 4, TH}) &&
                  lika(r.rekt(r.t2, ck), {L1 - 4 + G, 0, KORT, TH}),
              "B8: t2 i sin kortaste längd, t1 krymper med resten",
              rekt(r.rekt(r.t1, ck)) + " " + rekt(r.rekt(r.t2, ck)));
        // Knappen i t2: hela innerlängden, och k2 dold (en tom rekt).
        const glaze::Ctx t2c = barnCtx(r.t2, barnCtx(r.host, barnCtx(r.rot, ck)));
        glaze::Widget*   ob  = nullptr;
        for (glaze::Widget* b : r.t2.children())
            if (b != &r.k2)
                ob = b;
        const gfx::Rectf obr = ob ? ob->resolve(t2c) : gfx::Rectf{};
        const gfx::Rectf k2r = r.k2.resolve(t2c);
        check(ob && nära(obr.w, st.toolOverflowW) && nära(obr.h, TH - 2 * st.toolBarPadY) &&
                  k2r.w <= 0.0f,
              "B8: i sin kortaste längd visar t2 bara överflödsknappen",
              rekt(obr) + " " + rekt(k2r));
        r.t1.setOverflow(false);
        r.t2.setOverflow(false);
        check(lika(r.rekt(r.t1, ck), {0, 0, L1, TH}) &&
                  lika(r.rekt(r.t2, ck), {L1 + G, 0, bandet - L1 - G, TH}),
              "B8: setOverflow(false) — t1 i sin längd, t2 kortat vid bandets "
              "slut (det gamla)",
              rekt(r.rekt(r.t1, ck)) + " " + rekt(r.rekt(r.t2, ck)));
    }
}

// ================================================================= STRÄNGEN

void fallStrangen()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const float         t  = st.splitterThick;
    const auto          c  = ctxFör(st, 1.0f, gfx::Rectf{0, 0, 600, 400});

    // S1 — TUR OCH RETUR med alla sju fälten och ordningen.
    {
        Rigg r;
        r.b.setCollapsed(true);
        r.c.hide();
        const std::string s = r.host.layout();
        check(s == "v2;s=left;c=1;p=a,1,0,1,0,150,0;s=left;c=1;"
                   "p=b,1,1,1,0,150,0;s=top;c=1;p=c,0,0,1,1,0,100",
              "S1: layout() — skärordning, kant, aktiv, infälld, nålad, "
              "stängd, mått", s);
        Rigg r2;
        check(r2.host.setLayout(s) && r2.host.layout() == s &&
                  r2.b.isCollapsed() && !r2.c.isVisible(),
              "S1: setLayout(layout()) återskapar samma layout",
              r2.host.layout());
    }

    // S2 — ATOMISK: varje trasig sträng lämnar HELA layouten orörd. Den
    // första posten är giltig i alla — en läsning som tillämpar post för post
    // hade flyttat a till toppen innan felet upptäcktes. En sträng per regel
    // i grammatiken (dock_string.h).
    {
        const std::string g = "v2;s=top;c=1;p=a,1,0,1,0,1,1";   // giltig början
        const std::string trasiga[] = {
            "v1;a=top,1,1,0,0",                            // okänd version
            "s=top;c=1;p=a,1,0,1,0,1,1",                   // ingen version
            "",                                            // tom
            g + ";s=left",                                 // remsa utan cell
            g + ";s=left;c=1",                             // cell utan panel
            "v2;c=1;p=a,1,0,1,0,1,1",                      // cell utan remsa
            "v2;s=top;p=a,1,0,1,0,1,1",                    // panel utan cell
            g + ";x=1",                                    // okänd tagg
            g + ";s=left;c=1;p=b,1,0,1,0,1",               // sex fält
            g + ";s=left,5;c=1;p=b,1,0,1,0,1,1",           // remsan: två fält
            g + ";s=left;c=1,2;p=b,1,0,1,0,1,1",           // cellen: två fält
            g + ";s=left;c=1;p=b,2,0,1,0,1,1",             // flagga ej 0/1
            g + ";s=left;c=1;p=b,1,0,1,0,x,1",             // mått ej tal
            g + ";s=left;c=1;p=b,1,0,1,0,-5,1",            // negativt mått
            g + ";s=left;c=1;p=b,1,0,1,0,1,inf",           // oändligt mått
            g + ";s=left;c=0;p=b,1,0,1,0,1,1",             // vikt 0
            g + ";s=left;c=1e39;p=b,1,0,1,0,1,1",          // utanför float
            g + ";s=left;c=nan;p=b,1,0,1,0,1,1",           // NaN
            g + ";s=mitten;c=1;p=b,1,0,1,0,1,1",           // okänd kant
            g + ";s=fill;c=1;p=b,1,0,1,0,1,1",             // fill är ingen kant
            g + ";s=left;c=1;p=a,1,0,1,0,1,1",             // namn två gånger
            g + ";=x",                                     // post utan tagg
            g + ";",                                       // tom post
            "v2;s=top;c=1;p=a=b,1,0,1,0,1,1",              // ogiltigt namn
            // VERKTYGSFÄLTEN
            g + ";t=x,top,0,0",                            // fyra fält
            g + ";t=x,mitten,0,0,0",                       // okänd kant
            g + ";t=x,fill,0,0,0",                         // fill är ingen kant
            g + ";t=x,top,-1,0,0",                         // negativt band
            g + ";t=x,top,0,1.5,0",                        // platsen ej heltal
            g + ";t=x,top,99999999999,0,0",                // utanför int
            g + ";t=x,top,0,0,2",                          // flagga ej 0/1
            g + ";t=x,top,0,0,0;t=y,top,0,0,0",            // samma plats
            g + ";t=a,top,0,0,0",                          // namnet två gånger
        };
        for (const std::string& s : trasiga) {
            Rigg              r;
            const std::string före = r.host.layout();
            const bool        ok   = r.host.setLayout(s);
            check(!ok && r.host.layout() == före &&
                      r.a.dock == glaze::Dock::Left && r.a.width == 150.0f,
                  "S2: trasig sträng avvisas och lämnar allt orört: \"" + s +
                      "\"",
                  r.host.layout());
        }
    }

    // S3 — ett OKÄNT namn hoppas över post för post, och resten gäller; en
    // remsa och en cell som blir TOMMA tas bort, och de onämnda läggs innerst
    // i barnordning.
    {
        Rigg r;
        check(r.host.setLayout("v2;s=top;c=1;p=borta,1,0,1,0,0,0;s=right;c=1;"
                               "p=b,1,0,1,0,120,0") &&
                  r.b.dock == glaze::Dock::Right && r.b.width == 120.0f,
              "S3: okänd panel hoppas över — posterna efter gäller");
        check(r.host.layout() ==
                  "v2;s=right;c=1;p=b,1,0,1,0,120,0;s=left;c=1;"
                  "p=a,1,0,1,0,150,0;s=top;c=1;p=c,1,0,1,0,0,100",
              "S3: den tomma remsan borta, a och c innerst i barnordning",
              r.host.layout());
        Rigg r2;
        check(r2.host.setLayout("v2;s=left;c=1;p=borta,1,0,1,0,0,0;c=3;"
                                "p=a,1,0,1,0,150,0;p=b,0,0,1,0,150,0") &&
                  r2.host.layout() ==
                      "v2;s=left;c=3;p=a,1,0,1,0,150,0;p=b,0,0,1,0,150,0;"
                      "s=top;c=1;p=c,1,0,1,0,0,100",
              "S3: en cell som blir tom tas bort, grannen står kvar",
              r2.host.layout());
    }

    // S4 — en panel som SAKNAS i strängen läggs innerst vid sin kant.
    {
        Rigg r;
        check(r.host.setLayout("v2;s=right;c=1;p=b,1,0,1,0,150,0"),
              "S4: setLayout med en enda post");
        check(lika(r.rekt(r.b, c), {450, 0, 150, 400}) &&
                  lika(r.rekt(r.a, c), {0, 0, 150, 400}) &&
                  lika(r.rekt(r.c, c), {150 + t, 0, 300 - 2 * t, 100}),
              "S4: den nämnda ytterst, de onämnda innanför i barnordning",
              rekt(r.rekt(r.b, c)) + " " + rekt(r.rekt(r.a, c)) + " " +
                  rekt(r.rekt(r.c, c)));
    }

    // S5 — KÄLLORDNINGEN: en onämnd panel som adopteras EFTER raden hamnar
    // exakt där den hade hamnat om den fanns före.
    {
        struct Sen {
            Yta              rot;
            glaze::DockHost  host;
            glaze::DockPanel a{"A"}, b{"B"}, ny{"Ny"};
            Sen(bool nyEfter)
            {
                host.anchors = {true, true, true, true};
                rot.adopt(host);
                a.name    = "a";
                a.width   = 100;
                b.name    = "b";
                b.dock    = glaze::Dock::Top;
                b.height  = 50;
                ny.name   = "ny";
                ny.dock   = glaze::Dock::Bottom;
                ny.height = 80;
                host.adopt(a);
                host.adopt(b);
                if (!nyEfter)
                    host.adopt(ny);
                host.setLayout("v2;s=top;c=1;p=b,1,0,1,0,0,50");
                if (nyEfter)
                    host.adopt(ny);
            }
            gfx::Rectf rekt(const glaze::Widget& w, const glaze::Ctx& rc)
            {
                return w.resolve(barnCtx(host, barnCtx(rot, rc)));
            }
        };
        Sen före(false), efter(true);
        check(lika(efter.rekt(efter.ny, c), före.rekt(före.ny, c)) &&
                  lika(efter.rekt(efter.a, c), före.rekt(före.a, c)) &&
                  lika(före.rekt(före.ny, c), {100 + t, 320, 500 - t, 80}),
              "S5: en onämnd panel adopterad EFTER setLayout hamnar som en "
              "adopterad före (innanför den nämnda)",
              rekt(efter.rekt(efter.ny, c)) + " mot " +
                  rekt(före.rekt(före.ny, c)));
    }

    // S6 — v1 AVVISAS (brytande ändring före v1.00 — inga alias).
    {
        Rigg r;
        check(!r.host.setLayout("v1;a=left,150,0,0,0;b=left,150,0,0,0"),
              "S6: en v1-sträng avvisas");
    }

    // S7 — SKRIVAREN emitterar bara det parsern godtar: en grupp där INGEN
    // panel har ett sparbart namn ger varken cell eller remsa.
    {
        Rigg r;
        r.a.name = "";
        r.b.name = "x;y";
        const std::string s = r.host.layout();
        check(s == "v2;s=top;c=1;p=c,1,0,1,0,0,100",
              "S7: namnlösa paneler utelämnas — ingen tom cell, ingen tom remsa",
              s);
        Rigg r2;
        check(r2.host.setLayout(s), "S7: …och strängen går att läsa", s);

        // …och RÅA MÅTT som inte är tal > 0:
        // −1, NaN och ∞ skrivs som 0 = AUTO, strängen går att läsa, och
        // layouten ur den är densamma som ur fälten — EN regel vid
        // användning och sparning (∞ hade annars tagit hela höjden).
        Rigg r3;
        r3.a.width  = -1.0f;
        r3.b.width  = std::numeric_limits<float>::quiet_NaN();
        r3.c.height = std::numeric_limits<float>::infinity();
        const std::string s3 = r3.host.layout();
        check(s3 == "v2;s=left;c=1;p=a,1,0,1,0,0,0;s=left;c=1;"
                    "p=b,1,0,1,0,0,0;s=top;c=1;p=c,1,0,1,0,0,0",
              "S7: −1, NaN och ∞ skrivs som AUTO (0)", s3);
        Rigg r4;
        check(r4.host.setLayout(s3) &&
                  lika(r4.rekt(r4.a, c), r3.rekt(r3.a, c)) &&
                  lika(r4.rekt(r4.b, c), r3.rekt(r3.b, c)) &&
                  lika(r4.rekt(r4.c, c), r3.rekt(r3.c, c)),
              "S7: …strängen läses, och den ger samma yta som fälten gav",
              rekt(r3.rekt(r3.c, c)) + " mot " + rekt(r4.rekt(r4.c, c)));
    }

    // S8 — den AKTIVA fliken väljs bland de kvarvarande: b är aktiv men
    // namnlös, så a skrivs som aktiv (en aktiv som inte skrivs hade lämnat
    // cellen utan).
    {
        Rigg r;
        r.host.dockPanel(r.b, r.a, glaze::Dock::Fill);   // b aktiv
        r.b.name = "";
        check(r.host.layout() ==
                  "v2;s=left;c=1;p=a,1,0,1,0,150,0;s=top;c=1;p=c,1,0,1,0,0,100",
              "S8: den aktiva är namnlös — a skrivs som aktiv",
              r.host.layout());
        r.b.name = "b";
        r.b.hide();
        check(r.host.layout() ==
                  "v2;s=left;c=1;p=a,1,0,1,0,150,0;p=b,0,0,1,1,150,0;s=top;c=1;"
                  "p=c,1,0,1,0,0,100",
              "S8: den aktiva är stängd — den första öppna skrivs som aktiv",
              r.host.layout());
    }

    // S9 — TALEN går tur och retur EXAKT:
    // vikter och ett mått med många decimaler läses, skrivs i kortaste form,
    // och den FÖRSTA emissionen tolkas HÄR — med grindens egen läsning
    // (`std::from_chars`), inte produktionens tolkare — och jämförs BITVIS
    // med floatvärdena strängen började som. Därefter läses den in igen, skrivs
    // ut likadant, och de två riggarna ger exakt samma yta.
    {
        constexpr float v1 = 0.333333343f, v2 = 1.66666663f, b1 = 150.25f;
        Rigg r;
        check(r.host.setLayout("v2;s=left;c=0.333333343;p=a,1,0,1,0,150.25,0;"
                               "c=1.66666663;p=b,1,0,1,0,150,0"),
              "S9: vikter med nio decimaler läses");
        const std::string        s1 = r.host.layout();
        const std::vector<float> v  = vikterI(s1);
        check(v.size() == 3 && v[0] == v1 && v[1] == v2 && v[2] == 1.0f &&
                  breddI(s1, "a") == b1,
              "S9: den första emissionen bär EXAKT samma float — vikterna och "
              "a:s bredd",
              s1);
        Rigg r2;
        check(r2.host.setLayout(s1) && r2.host.layout() == s1 &&
                  exakt(r2.rekt(r2.a, c), r.rekt(r.a, c)) &&
                  exakt(r2.rekt(r2.b, c), r.rekt(r.b, c)),
              "S9: …inläst igen skrivs den likadant och ger exakt samma yta",
              s1 + " mot " + r2.host.layout());
    }
}

// ============================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    std::vector<std::string> logg;
    glaze::DockHost          host;
    glaze::DockPanel         vänster{"Vänster"}, sök{"Sök"}, botten{"Botten"};
    glaze::Panel             mitt;
    glaze::TextInput         fält;       // i vänster
    glaze::TextInput         sökFält;    // i sök
    glaze::TextInput         mittFält;   // i huvudytan

    Vard() : glaze::AppWindow("dock_gate", 640, 420, gfx::Chrome::None)
    {
        host.name      = "host";
        host.dock      = glaze::Dock::Fill;
        vänster.name   = "vanster";
        vänster.width  = 150;
        sök.name       = "sok";
        sök.width      = 150;
        botten.name    = "botten";
        botten.dock    = glaze::Dock::Bottom;
        botten.width   = 150;
        botten.height  = 100;
        mitt.setBorder(true);
        fält.dock      = glaze::Dock::Top;
        sökFält.dock   = glaze::Dock::Top;
        mittFält.x     = 10;
        mittFält.y     = 10;

        root().adopt(host);
        host.adopt(vänster);
        host.adopt(sök);
        host.adopt(botten);
        host.adopt(mitt);
        vänster.adopt(fält);
        sök.adopt(sökFält);
        mitt.adopt(mittFält);

        vänster.onChange = [this] { logg.push_back("P:vanster"); };
        sök.onChange     = [this] { logg.push_back("P:sok"); };
        botten.onChange  = [this] { logg.push_back("P:botten"); };
        host.onChange    = [this] { logg.push_back("H"); };
    }

    gfx::Rectf rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    float      s() { return window().uiScale(); }
    // En LOGISK punkt i värden (som täcker hela klientytan) i fysiska px.
    gfx::Pointf P(float lx, float ly) { return {lx * s(), ly * s()}; }
    // En punkt på rubrikens TITEL (inte på knapparna).
    gfx::Pointf rubrik(glaze::DockPanel& p)
    {
        const gfx::Rectf r = rekt(p);
        return {r.x + style().dockHeaderPadX * s() + 4.0f * s(),
                r.y + style().dockHeaderH * s() / 2.0f};
    }
    // Rubrikknapparna: kvadrater längst till höger (stäng ytterst).
    gfx::Pointf stängKnapp(glaze::DockPanel& p)
    {
        const gfx::Rectf r = rekt(p);
        const float      h = style().dockHeaderH * s();
        return {r.x + r.w - h / 2.0f, r.y + h / 2.0f};
    }
    // Knappnålen: kvadraten innanför stängknappen.
    gfx::Pointf nålKnapp(glaze::DockPanel& p)
    {
        const gfx::Rectf r = rekt(p);
        const float      h = style().dockHeaderH * s();
        return {r.x + r.w - 1.5f * h, r.y + h / 2.0f};
    }
    bool iOrdningen(const glaze::Widget& w)
    {
        const std::vector<glaze::Widget*> ord = focusOrder();
        return std::find(ord.begin(), ord.end(), &w) != ord.end();
    }

    void mus(gfx::EventType t, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
    void klicka(gfx::Pointf p)
    {
        ner(p);
        upp(p);
    }
    // Ett drag: tryck, en rörelse förbi tröskeln och släpp vid målet.
    void dra(gfx::Pointf från, gfx::Pointf till)
    {
        ner(från);
        flytta(till);
        upp(till);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    // TEXT genom fönstrets riktiga ingång — den går till den fokuserade
    // widgeten, och får aldrig nå ett fält ingen ser.
    void text(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, s, sizeof e.text - 1);
        handleEvent(e);
    }
    // Står fokus NÅGONSTANS i fönstret? (Alla widgets i riggen.)
    bool någotFokus() const
    {
        const glaze::Widget* alla[] = {&host,  &vänster, &sök,     &botten,
                                       &mitt,  &fält,    &sökFält, &mittFält};
        for (const glaze::Widget* x : alla)
            if (x->focused())
                return true;
        return false;
    }

    // Hela bilden ur damage-vägen (`full` = efter en FULL omritning, som
    // pixel_gate gör). Tom vektor när ytan inte kan läsas.
    std::vector<std::uint8_t> bild(bool full)
    {
        if (full)
            invalidate();
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, Hh = 0;
        if (!window().captureFrame(rgba, W, Hh))
            rgba.clear();
        return rgba;
    }

    // En pixel ur en FÄRSK bild — renderFrame + captureFrame (pixel_gates
    // väg). false när ytan inte kan läsas.
    bool pixel(gfx::Pointf p, std::uint32_t& ut)
    {
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, Hh = 0;
        if (!window().captureFrame(rgba, W, Hh))
            return false;
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        if (x < 0 || y < 0 || x >= W || y >= Hh)
            return false;
        const std::size_t i =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
             static_cast<std::size_t>(x)) * 4u;
        ut = static_cast<std::uint32_t>(rgba[i]) << 24 |
             static_cast<std::uint32_t>(rgba[i + 1]) << 16 |
             static_cast<std::uint32_t>(rgba[i + 2]) << 8 | rgba[i + 3];
        return true;
    }
};

std::string fog(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : " ") + x;
    return "[" + s + "]";
}

using Logg = std::vector<std::string>;

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);
    // YTORNA MÄTS UTAN GLIDNING: fönstret är öppet och ritat, så en
    // infällning hade annars glidit i 160 ms och ytan mätts mitt i den.
    // Glidningen och lämna-prövningen har egna fall (fallOverlay).
    const glaze::Style förra = w.style();
    glaze::Style       st    = förra;
    st.dockAnimMs            = 0;
    w.setStyle(st);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const float       s  = w.s();
        const float       t  = w.style().splitterThick;
        const float       TH = w.style().tabHeaderH;
        const float       G  = w.style().tabGap;
        const float       HH = w.style().dockHandleH;
        const std::string start = w.host.layout();

        // ---- F0. PEKAREN över ett stående remsskikt.
        w.flytta(w.P(150 + t / 2, 200));
        check(w.window().cursor() == gfx::PointerShape::ResizeWE,
              "F0: ↔ över vänsterremsans skikt");

        // ---- F1. RUBRIKEN TILL MITTEN AV EN PANEL → FLIK. Indikatorn är
        // exakt den yta sök FÅR: vänsters cell minus den kommande flikraden.
        // Pixeln i panelytan ändras, pixeln där flikraden kommer att stå och
        // pixeln i sök själv gör det inte.
        w.logg.clear();
        const gfx::Pointf inne    = w.P(75, 100);
        const gfx::Pointf flikrad = w.P(75, 420 - TH / 2);
        const gfx::Pointf egen    = w.P(150 + t + 75, 200);
        std::uint32_t     iF = 0, fF = 0, eF = 0, iU = 0, fU = 0, eU = 0;
        const bool kanLäsa = w.pixel(inne, iF) && w.pixel(flikrad, fF) &&
                             w.pixel(egen, eF);
        w.ner(w.rubrik(w.sök));
        w.flytta(w.P(75, 200));
        if (kanLäsa) {
            w.pixel(inne, iU);
            w.pixel(flikrad, fU);
            w.pixel(egen, eU);
            check(iU != iF, "F1: indikatorn täcker vänsters panelyta");
            check(fU == fF,
                  "F1: …men inte raden där flikarna ska stå — indikatorn är "
                  "SIMULERAD, inte cellens yta");
            check(eU == eF, "F1: …och inte sök själv");
        } else {
            std::printf("  --   F1: pixlarna kunde inte läsas (captureFrame) "
                        "— indikatorns pixelkontroll hoppas över\n");
            ++g_skips;
        }
        check(w.logg.empty(), "F1: ingenting dockar före släppet", fog(w.logg));
        w.upp(w.P(75, 200));
        check(w.host.layout() ==
                  "v2;s=left;c=1;p=vanster,0,0,1,0,150,0;p=sok,1,0,1,0,150,0;"
                  "s=bottom;c=1;p=botten,1,0,1,0,150,100",
              "F1: sök är en flik i vänsters cell, och aktiv", w.host.layout());
        check(lika(w.rekt(w.sök), {0, 0, 150 * s, (420 - TH) * s}) &&
                  tom(w.rekt(w.vänster)),
              "F1: sök har cellen minus flikraden, vänster en tom yta",
              rekt(w.rekt(w.sök)) + " " + rekt(w.rekt(w.vänster)));
        check(w.logg == Logg{"P:sok", "H"},
              "F1: onChange — panelen, sedan värden", fog(w.logg));
        check(w.iOrdningen(w.sökFält) && !w.iOrdningen(w.fält) &&
                  w.iOrdningen(w.host),
              "F1: fokusordningen — den visade flikens fält och värden, aldrig "
              "den inaktivas");

        // ---- F2. ETT KLICK PÅ EN FLIK byter.
        w.logg.clear();
        const float flikB = (150 - G) / 2;
        w.klicka(w.P(flikB / 2, 420 - TH / 2));
        check(w.vänster.showsChildren() && !w.sök.showsChildren() &&
                  w.logg == Logg{"P:vanster", "H"},
              "F2: klick på vänsters flik — den visas, onChange", fog(w.logg));
        check(w.iOrdningen(w.fält) && !w.iOrdningen(w.sökFält),
              "F2: fokusordningen följer fliken");

        // ---- F3. EN FLIK DRAS UT till värdens högra kant (över huvudytan):
        // trycket väljer den, släppet lägger den ytterst där.
        w.logg.clear();
        w.dra(w.P(flikB + G + flikB / 2, 420 - TH / 2), w.P(640 - 10, 150));
        check(w.sök.dock == glaze::Dock::Right &&
                  lika(w.rekt(w.sök), {490 * s, 0, 150 * s, 420 * s}),
              "F3: sök ytterst till höger, full höjd", rekt(w.rekt(w.sök)));
        check(lika(w.rekt(w.vänster), {0, 0, 150 * s, 420 * s}),
              "F3: vänster ensam i sin cell — ingen flikrad",
              rekt(w.rekt(w.vänster)));
        check(w.logg == Logg{"P:sok", "H", "P:sok", "H"},
              "F3: onChange vid trycket (fliken valdes) och vid släppet",
              fog(w.logg));

        // ---- F4. RUBRIKEN TILL EN PANELS NEDERKANT → STAPLAD CELL under den
        // (längs den stående remsan), och målets vikt delas lika.
        w.logg.clear();
        w.dra(w.rubrik(w.botten), w.P(75, 420 - 10));
        const float halv = (420 - t) / 2;
        check(w.botten.dock == glaze::Dock::Left &&
                  lika(w.rekt(w.vänster), {0, 0, 150 * s, halv * s}) &&
                  lika(w.rekt(w.botten), {0, (halv + t) * s, 150 * s, halv * s}),
              "F4: botten staplad under vänster, 1:1",
              rekt(w.rekt(w.vänster)) + " " + rekt(w.rekt(w.botten)));
        check(w.logg == Logg{"P:botten", "H"},
              "F4: onChange — panelen, sedan värden", fog(w.logg));

        // ---- F5. CELLSKIKTET dras uppåt och KLAMPAS mot vänsters minimum
        // (satt via API); förhandsvisning till släppet, vikterna skrivs då.
        // Draget slutar INNANFÖR snappens zon under golvet (200 − Z/2):
        // snappen fäller ett drag under golvet minus `dockSnapZone`
        // in cellen i stället för att klampa den (O8 mäter det), så ett
        // mål y = 40 mätte infällningen och inte klampningen.
        w.vänster.setMinimumSize(0, 200);
        w.logg.clear();
        const gfx::Pointf C  = w.P(75, halv + t / 2);
        const float       Z5 = w.style().dockSnapZone;
        const gfx::Pointf C5 = w.P(75, 200 - Z5 / 2 + t / 2);
        w.ner(C);
        w.flytta(C5);
        check(nära(w.rekt(w.vänster).h, 200 * s) && w.logg.empty(),
              "F5: förhandsvisningen klampas till minimum 200",
              std::to_string(w.rekt(w.vänster).h));
        const gfx::Rectf pv = w.rekt(w.vänster), pb = w.rekt(w.botten);
        w.upp(C5);
        check(nära(w.rekt(w.vänster).h, 200 * s) &&
                  nära(w.rekt(w.botten).h, (420 - t - 200) * s),
              "F5: släppet skriver vikterna — vänster 200, botten resten",
              rekt(w.rekt(w.vänster)) + " " + rekt(w.rekt(w.botten)));
        check(exakt(w.rekt(w.vänster), pv) && exakt(w.rekt(w.botten), pb),
              "F5: släppet ger EXAKT förhandsvisningens ytor — samma vikter "
              "(fynd 5)",
              rekt(pv) + " mot " + rekt(w.rekt(w.vänster)));
        check(w.logg == Logg{"P:vanster", "P:botten", "H"},
              "F5: onChange — de två panelerna, sedan värden", fog(w.logg));

        // ---- F5b. GOLV SOM INTE RYMS:
        // botten kräver 400 av remsans 420 − skiktet, så vänster får det som
        // blir kvar — under sitt dragolv (`dockHeaderH`). Inget läge för
        // skiktet ger båda sina golv, och då STÅR det STILL: förhandsvisningen
        // visar ingen längd som positiva vikter inte återskapar (en
        // klampningen visade vänster vid golvet, och släppet gav tillbaka den
        // ursprungliga delningen), och släppet fyrar inget onChange.
        w.vänster.setMinimumSize(0, 0);
        w.botten.setMinimumSize(0, 400);
        const gfx::Rectf v0 = w.rekt(w.vänster), b0 = w.rekt(w.botten);
        check(nära(v0.h, (420 - t - 400) * s) && nära(b0.h, 400 * s),
              "F5b: botten låst vid sitt minimum, vänster får resten",
              rekt(v0) + " " + rekt(b0));
        // Draget stannar innanför BÅDA cellernas snappzon: ett drag till
        // y = 100 tog botten 71 px under sitt golv minus zonen, och då fäller
        // snappen in botten — det mätte infällningen, inte det stillastående
        // skiktet. Målet är det största som håller vänster över dragolvet
        // minus zonen och botten över sitt minimum minus zonen.
        const float v5 =
            std::min(w.style().dockHeaderH, 420 - t - 400 + Z5) - 2.0f;
        w.logg.clear();
        w.ner(w.P(75, v0.h / s + t / 2));
        w.flytta(w.P(75, v5 + t / 2));
        check(exakt(w.rekt(w.vänster), v0) && exakt(w.rekt(w.botten), b0),
              "F5b: golven ryms inte — förhandsvisningen står still",
              rekt(w.rekt(w.vänster)) + " " + rekt(w.rekt(w.botten)));
        w.upp(w.P(75, v5 + t / 2));
        check(exakt(w.rekt(w.vänster), v0) && exakt(w.rekt(w.botten), b0) &&
                  w.logg.empty(),
              "F5b: …och släppet ändrar ingenting, inget onChange",
              fog(w.logg));
        w.botten.setMinimumSize(0, 0);
        w.vänster.setMinimumSize(0, 200);   // F5:s läge igen

        // ---- F6. REMSSKIKTET skriver tjockleken till ALLA paneler i remsan.
        // +20 och inte +40: 190 ligger inom snappens zon från det förvalda
        // måttet (`dockDefaultSize` 200) och drogs till 200 — O7 mäter
        // magneten, här mäts bara att tjockleken når båda panelerna.
        w.logg.clear();
        const gfx::Pointf R6 = w.P(150 + t / 2, 100);
        w.dra(R6, {R6.x + 20 * s, R6.y});
        check(w.vänster.width == 170.0f && w.botten.width == 170.0f,
              "F6: båda panelerna i remsan fick bredden 170",
              std::to_string(w.vänster.width) + " " +
                  std::to_string(w.botten.width));
        check(w.logg == Logg{"P:vanster", "P:botten", "H"},
              "F6: onChange för båda, sedan värden", fog(w.logg));
        w.vänster.width = 150;   // tillbaka (råa layoutfält)
        w.botten.width  = 150;
        w.invalidate();

        // ---- F7. RUBRIKEN TILL VÄRDENS KANT (över huvudytan) → ytterst där,
        // över hela kantens längd; AUTO-höjden är stilens.
        w.logg.clear();
        w.dra(w.rubrik(w.vänster), w.P(300, 10));
        check(w.vänster.dock == glaze::Dock::Top &&
                  lika(w.rekt(w.vänster),
                       {0, 0, 640 * s, w.style().dockDefaultSize * s}),
              "F7: vänster ytterst överst, full bredd", rekt(w.rekt(w.vänster)));
        check(w.logg == Logg{"P:vanster", "H"}, "F7: onChange", fog(w.logg));

        // ---- F8. KNAPPNÅLEN på en dockad panel: onålad och infälld —
        // ett HANDTAG — och onChange. Ett klick på handtaget fäller ut den som
        // OVERLAY (värdens främre barn) med fokus på panelens första
        // tabbstopp och UTAN onChange (ett drifttillstånd), och knappnålen i
        // overlayn fäster den igen — dockad överst, där den stod.
        w.logg.clear();
        w.klicka(w.nålKnapp(w.vänster));
        check(w.vänster.isCollapsed() && !w.vänster.isPinned() &&
                  tom(w.rekt(w.vänster)) && w.logg == Logg{"P:vanster", "H"},
              "F8: knappnålen — onålad och infälld (ingen yta), onChange",
              fog(w.logg));
        w.logg.clear();
        w.klicka(w.P(4, HH / 2));   // handtaget börjar i remsans början
        check(!w.vänster.isCollapsed() && w.host.frontChild() == &w.vänster &&
                  w.fält.focused() && w.logg.empty(),
              "F8: klick på handtaget — overlayn, fokus i panelen, inget "
              "onChange",
              fog(w.logg));
        w.logg.clear();
        w.klicka(w.nålKnapp(w.vänster));
        check(w.vänster.isPinned() && !w.vänster.isCollapsed() &&
                  w.host.frontChild() == nullptr &&
                  w.vänster.dock == glaze::Dock::Top &&
                  w.logg == Logg{"P:vanster", "H"},
              "F8: knappnålen i overlayn — fäst och dockad överst, onChange",
              fog(w.logg));

        // ---- F9. VÄRDEN ÄR ETT TABBSTOPP när den har ett handtag, och Enter
        // fäller ut det valda.
        w.vänster.setCollapsed(true);
        check(w.iOrdningen(w.host) && !w.iOrdningen(w.fält),
              "F9: infälld — värden är tabbstoppet, fältet inte");
        w.host.focus();
        w.logg.clear();
        w.tangent(gfx::Key::Return);
        check(!w.vänster.isCollapsed() && w.fält.focused() &&
                  w.logg == Logg{"P:vanster", "H"},
              "F9: Enter fäller ut och lämnar fokus i panelen", fog(w.logg));

        // ---- F10. `show()` på en STÄNGD flik gör den aktiv (API:t
        // `dockPanel` och `activate` sätter upp gruppen).
        w.host.dockPanel(w.sök, w.botten, glaze::Dock::Fill);
        w.botten.activate();
        check(w.botten.showsChildren() && !w.sök.showsChildren(),
              "F10: activate() väljer botten");
        w.sök.hide();
        w.sök.show();
        check(w.sök.showsChildren() && !w.botten.showsChildren(),
              "F10: show() på den stängda — sök blir den aktiva fliken");

        // …och FOKUS FÖLJER: ett riktigt
        // tangentfokus i sökfältet, och text genom fönstrets ingång. När
        // botten blir den aktiva fliken genom `show()` blir sök inaktiv —
        // fokus lämnar fältet (botten saknar stopp, så värden tar det), och
        // texten därefter når inte det dolda fältet. När sök döljs av APPEN
        // går fokus till huvudytans första stopp.
        w.sökFält.focus();
        w.text("x");
        check(w.sökFält.focused() && w.sökFält.text() == "x",
              "F10: fokus i sökfältet, och texten når det", w.sökFält.text());
        w.botten.hide();   // den inaktiva fliken stängs — fokus får stå
        check(w.sökFält.focused(), "F10: en inaktiv flik stängd — fokus står");
        w.botten.show();   // …och öppnas igen: den blir den AKTIVA
        check(w.botten.showsChildren() && !w.sökFält.focused() &&
                  w.host.focused(),
              "F10: show() gjorde botten aktiv — fokus till värden, inte kvar "
              "i den inaktiva flikens fält");
        w.text("y");
        check(w.sökFält.text() == "x",
              "F10: …och texten når inte det dolda fältet", w.sökFält.text());
        w.sök.activate();
        w.sökFält.focus();
        w.sök.hide();      // APPEN stänger panelen med fokus i
        check(!w.sökFält.focused() && w.mittFält.focused(),
              "F10: hide() av appen — fokus till huvudytans första stopp");
        w.text("z");
        check(w.sökFält.text() == "x" && w.mittFält.text() == "z",
              "F10: …och texten går dit, inte till det stängda fältet",
              w.sökFält.text() + " / " + w.mittFält.text());
        w.sök.show();      // F11 stänger den igen med musen

        // ---- F11. STÄNGKNAPPEN döljer panelen, och fokus i den går till
        // huvudytans första tabbstopp.
        w.sökFält.focus();
        w.logg.clear();
        w.klicka(w.stängKnapp(w.sök));
        check(!w.sök.isVisible() && w.mittFält.focused() &&
                  w.logg == Logg{"P:sok", "H"},
              "F11: stäng — dold, fokus till huvudytan, onChange", fog(w.logg));
        w.sök.show();

        // ---- F12. FOKUSFÖRLUST mitt i ett rubrikdrag: indikatorn syns under
        // draget och är borta så fort trycket avbryts, fast inget släpp har
        // skett — och ingenting dockar.
        w.logg.clear();
        const gfx::Pointf mål = w.P(10, 300);   // botten-cellens vänstra zon
        const gfx::Pointf p12 = w.P(75, 300);
        std::uint32_t     pFöre = 0, pUnder = 0, pEfter = 0;
        const bool        kanLäsa12 = w.pixel(p12, pFöre);
        w.ner(w.rubrik(w.vänster));
        w.flytta(mål);
        if (kanLäsa12)
            w.pixel(p12, pUnder);
        w.enkel(gfx::EventType::WindowFocusLost);
        if (kanLäsa12) {
            w.pixel(p12, pEfter);
            check(pUnder != pFöre && pEfter == pFöre,
                  "F12: indikatorn syns under draget och är borta när "
                  "fokusförlusten avbryter det");
        }
        w.upp(mål);
        check(w.vänster.dock == glaze::Dock::Top && w.logg.empty(),
              "F12: fokusförlust mitt i draget — ingen dockning, inget "
              "onChange", fog(w.logg));
        w.enkel(gfx::EventType::WindowFocusGained);

        // ---- F13. SPARBART: startlayoutens sträng återställer allt, och
        // tillbaka igen.
        const std::string nu = w.host.layout();
        check(w.host.setLayout(start) && w.vänster.dock == glaze::Dock::Left &&
                  w.host.layout() == start,
              "F13: setLayout(start) återställer startlayouten",
              w.host.layout());
        check(w.host.setLayout(nu) && w.host.layout() == nu,
              "F13: och tillbaka igen", w.host.layout());

        // ---- F14. PROGRAMMATISKT hide()/show()/setCollapsed() UTANFÖR ett
        // event (här: i en postad funktion, som en timer eller en annan tråd
        // via `app().post`). Förväntat: samma bild som en FULL omritning.
        {
            const std::vector<std::uint8_t> före = w.bild(true);
            if (före.empty()) {
                std::printf("  --   F14: bilden kunde inte läsas "
                            "(captureFrame) — hoppas över\n");
                ++g_skips;
            } else {
                w.botten.hide();
                const std::vector<std::uint8_t> delvis = w.bild(false);
                check(delvis == w.bild(true),
                      "F14: hide() utanför ett event — delvis omritning "
                      "ger samma bild som en full");
                w.botten.show();
                const std::vector<std::uint8_t> delvis2 = w.bild(false);
                check(delvis2 == w.bild(true),
                      "F14: show() utanför ett event — samma bild som en full");
                w.vänster.setCollapsed(true);
                const std::vector<std::uint8_t> delvis3 = w.bild(false);
                check(delvis3 == w.bild(true),
                      "F14: setCollapsed() utanför ett event — samma bild "
                      "som en full");
                w.vänster.setCollapsed(false);
                const std::vector<std::uint8_t> delvis4 = w.bild(false);
                check(delvis4 == w.bild(true),
                      "F14: och utfälld igen — samma bild som en full");
            }
        }

        // ---- F15. INGET STOPP ATT LÄMNA FOKUS TILL:
        // startlayouten har varken flikgrupper eller handtag, så
        // värden är inget tabbstopp, och huvudytans fält är dolt. En panel
        // med fokus som stängs — av appen, med stängknappen eller genom
        // `setLayout` — lämnar då fönstret UTAN fokus, och text når inget
        // dolt fält.
        {
            check(w.host.setLayout(start) && !w.host.focusable(),
                  "F15: startlayouten — värden är inget tabbstopp");
            w.mittFält.hide();
            w.fält.focus();
            const std::string före = w.fält.text();
            w.vänster.hide();
            check(!w.någotFokus(),
                  "F15: hide() av appen — fönstret står utan fokus");
            w.text("q");
            check(w.fält.text() == före,
                  "F15: …och texten når inte det stängda fältet",
                  w.fält.text());
            w.vänster.show();
            w.fält.focus();
            w.logg.clear();
            w.klicka(w.stängKnapp(w.vänster));
            check(!w.vänster.isVisible() && !w.någotFokus() &&
                      w.logg == Logg{"P:vanster", "H"},
                  "F15: stängknappen — dold, inget fokus, onChange",
                  fog(w.logg));
            w.vänster.show();
            w.sökFält.focus();
            check(w.host.setLayout(
                      "v2;s=left;c=1;p=vanster,1,0,1,0,150,0;s=left;c=1;"
                      "p=sok,1,0,1,1,150,0;s=bottom;c=1;p=botten,1,0,1,0,150,"
                      "100") &&
                      !w.sök.isVisible() && !w.någotFokus(),
                  "F15: setLayout stänger panelen med fokus i — inget fokus");
            w.mittFält.show();
            w.host.setLayout(start);
        }

        // ---- F16. FLIKRINGEN FÖLJER TRYCKET: en
        // flikgrupp med vänster FÖRST och sök efter, och tangentbordets val på
        // vänster (Home med fokus på värden). Ett tryck på SÖKS flik ger
        // värden fokus och gör sök aktiv — och ringen i `accent` står vid
        // söks vänsterkant på mitthöjd, inte kvar på listans första post.
        // Därefter flyttar Vänster/Höger ringen.
        //
        // DET FÖRVÄNTADE ÄR EN JÄMFÖRELSE, inte en färg (vid 125 %): ringen
        // är `focusWidth` (1 logisk px) bred, och vid en bråkdelsskala (1,25
        // fysiska px) täcker den ingen fysisk pixel helt, så ingen pixel bär
        // accenten exakt. Och i Glaze-UI:t är den visade flikens ram
        // `buttonBorder` (#9b88ee i Lavender mörk) — 17 steg från accenten,
        // närmare än kantutjämningens fel. En läsning av EN pixel inom
        // ±2 av accenten går därför bara vid heltalsskala. Ringens
        // fysiska spann läses (`läsRing` — pixlarna och den som ligger närmast
        // accenten), och "ringen står på A, inte på B" betyder att A:s spann
        // ligger NÄRMARE accenten än i ett läge med SAMMA aktiva flik där
        // ringen står på B — då är ringen det enda som skiljer bilderna.
        {
            w.host.dockPanel(w.sök, w.vänster, glaze::Dock::Fill);
            w.vänster.activate();
            const float       fw    = w.style().focusWidth;
            const float       flik  = (150 - G) / 2;
            const float       mittY = 420 - TH / 2;
            const gfx::Pointf ringA = w.P(fw / 2, mittY);
            const gfx::Pointf ringB = w.P(flik + G + fw / 2, mittY);
            const gfx::Color  acc = w.host.behaviorCtx().theme->colors.accent;
            // Största kanalavvikelsen från accenten.
            auto avvikelse = [&](std::uint32_t px) {
                auto d = [](unsigned a, unsigned b) {
                    return a > b ? a - b : b - a;
                };
                return std::max({d((px >> 24) & 0xFFu, acc.r),
                                 d((px >> 16) & 0xFFu, acc.g),
                                 d((px >> 8) & 0xFFu, acc.b)});
            };
            struct Spann {
                std::vector<std::uint32_t> px;
                unsigned                   bäst = ~0u;   // närmast accenten
                bool operator==(const Spann& o) const { return px == o.px; }
            };
            auto läsRing = [&](gfx::Pointf p, Spann& ut) {
                ut = Spann{};
                const float bredd = fw * s / 2.0f + 1.0f;
                for (float x = std::floor(p.x - bredd); x <= p.x + bredd;
                     x += 1.0f) {
                    std::uint32_t q = 0;
                    if (!w.pixel({x, p.y}, q))
                        continue;
                    ut.px.push_back(q);
                    ut.bäst = std::min(ut.bäst, avvikelse(q));
                }
                return !ut.px.empty();
            };
            struct Läge {
                Spann a, b;
            };
            auto läs = [&](Läge& l) {
                return läsRing(ringA, l.a) && läsRing(ringB, l.b);
            };
            // Ringen står på A i `med` och på B i `utan` (samma aktiva flik).
            auto påA = [](const Läge& med, const Läge& utan) {
                return med.a.bäst < utan.a.bäst && med.b.bäst > utan.b.bäst;
            };
            Läge hemma, slut, tryck, vänsterL, högerL;
            w.host.focus();
            w.tangent(gfx::Key::End);   // ringen på sök, vänster aktiv
            const bool kanLäsa = läs(slut);
            w.tangent(gfx::Key::Home);
            if (!kanLäsa || !läs(hemma)) {
                std::printf("  --   F16: pixlarna kunde inte läsas "
                            "(captureFrame) — ringens kontroll hoppas över\n");
                ++g_skips;
            } else {
                check(w.host.focused() && påA(hemma, slut),
                      "F16: (förutsättning) Home på värden — ringen på "
                      "vänsters flik");
                w.klicka(w.P(flik + G + flik / 2, mittY));
                check(w.host.focused() && w.sök.showsChildren() &&
                          !w.vänster.showsChildren(),
                      "F16: trycket på söks flik — värden har fokus och sök "
                      "är aktiv");
                läs(tryck);
                w.tangent(gfx::Key::Left);
                läs(vänsterL);
                w.tangent(gfx::Key::Right);
                läs(högerL);
                check(påA(vänsterL, tryck),
                      "F16: ringen står på SÖKS flik — den som trycktes, inte "
                      "listans första");
                check(påA(vänsterL, högerL),
                      "F16: Vänster — ringen flyttar till vänsters flik");
                check(högerL.a == tryck.a && högerL.b == tryck.b,
                      "F16: Höger — och tillbaka till söks (samma bild som "
                      "efter trycket)");
            }
            w.host.setLayout(start);
        }

        // ---- PNG:erna att ögna, per UI × läge: flikgruppen med stapeln, ett
        // handtag överst och indikatorn mitt i ett drag.
        if (!g_pngDir.empty()) {
            const glaze::UI uier[] = {glaze::UI::Slate, glaze::UI::Glaze,
                                      glaze::UI::Minimalistic};
            const char*     namn[] = {"Slate", "Glaze", "Minimalistic"};
            auto spara = [&](const std::string& fil) {
                w.window().renderFrame();
                std::vector<std::uint8_t> rgba;
                int                       W = 0, Hh = 0;
                if (w.window().captureFrame(rgba, W, Hh))
                    gfx::encodePng(g_pngDir + "/" + fil, rgba, W, Hh);
            };
            w.host.setLayout(nu);
            for (int u = 0; u < 3; ++u)
                for (int läge = 0; läge < 2; ++läge) {
                    w.setUI(uier[u]);
                    w.setThemeMode(läge ? glaze::ThemeMode::Dark
                                        : glaze::ThemeMode::Light);
                    const std::string bas = std::string("dock-") + namn[u] +
                                            (läge ? "-mörkt" : "-ljust");
                    w.vänster.setCollapsed(true);
                    spara(bas + "-grupp.png");
                    w.vänster.setCollapsed(false);
                    w.ner(w.rubrik(w.vänster));
                    w.flytta(w.P(75, 300));
                    spara(bas + "-drag.png");
                    w.enkel(gfx::EventType::WindowFocusLost);   // avbryt
                    w.enkel(gfx::EventType::WindowFocusGained);
                }
        }

        w.close();
    });
    w.run();
    w.setStyle(förra);

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ============================================================ OVERLAYN
//
// KNAPPNÅLEN, OVERLAYN, LÄMNA-PRÖVNINGEN, SNAPPEN OCH GLIDNINGEN genom
// fönstrets riktiga ingång, i ett eget dolt fönster. Verktyg är ONÅLAD vid
// vänsterkanten (ett handtag) och bär ett fält, ett reglage och en
// INAKTIVERAD etikett; Höger och Under är en stapel vid högerkanten; och
// huvudytan är en panel med ett fält och en full mätare som overlayn täcker.
//
// LÄMNA-PRÖVNINGEN (D5) och GLIDNINGEN (D6) mäts med RIKTIG tid: stegen
// väntar i pumpen (`vänta`), så att timern levereras och bilderna ritas
// emellan, och glidningen mäts med en verklig sömn mellan två bilder.

// VÄNTA I PUMPEN — inte i en sömn: `klart` frågas en gång per pumpvarv
// (timrarna levereras och bilderna ritas emellan, `App::pumpUntilClosed`)
// tills den svarar true eller `ms` har gått; sedan körs `sedan(lyckades)`.
// Varje varv postar nästa, så pumpen sover inte under väntan — den snurrar,
// vilket en grind får göra.
struct Väntan {
    std::function<bool()>     klart;
    std::function<void(bool)> sedan;
    std::uint64_t             slut = 0;
};

void väntaVarv(const std::shared_ptr<Väntan>& v)
{
    if (v->klart()) {
        v->sedan(true);
        return;
    }
    if (gfx::ticksMs() >= v->slut) {
        v->sedan(false);
        return;
    }
    glaze::app().post([v] { väntaVarv(v); });
}

void vänta(std::function<bool()> klart, std::uint64_t ms,
           std::function<void(bool)> sedan)
{
    auto v   = std::make_shared<Väntan>();
    v->klart = std::move(klart);
    v->sedan = std::move(sedan);
    v->slut  = gfx::ticksMs() + ms;
    glaze::app().post([v] { väntaVarv(v); });
}

// Värden med sin skadestämpel synlig (skyddad i widget.h — commit_gates
// mönster), så att D6 kan se att den står still.
struct StämpelVärd : glaze::DockHost {
    using glaze::Widget::damageStamp;
};

// En widget med ett OVERLAY-LAGER — den läser klippet
// renderern har INNE I `drawOverlay` (middle_gates mönster), ett per anrop.
// Ritar ingenting själv.
struct LagerYta final : glaze::Widget {
    std::vector<gfx::Rectf> klipp;

    void draw(const glaze::Ctx&) override {}

protected:
    bool hasOverlay() const override { return true; }
    void drawOverlay(const glaze::Ctx& c) override
    {
        gfx::Rectf r{};
        if (c.r->clipBounds(r))
            klipp.push_back(r);
    }
};

struct OverlayVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    std::vector<std::string> logg;
    StämpelVärd              host;
    glaze::DockPanel         verktyg{"Verktyg"}, höger{"Höger"}, under{"Under"};
    glaze::TextInput         fält;       // i verktyg
    glaze::Slider            reglage;    // i verktyg
    glaze::Label             etikett;    // i verktyg — INAKTIVERAD (D3)
    glaze::Panel             mitt;
    glaze::TextInput         mittFält;    // i huvudytan, under overlayn
    glaze::Progress          mittMätare;  // i huvudytan, under overlayn —
                                          // FULL: accentens fyllning
    LagerYta                 lagret;      // i huvudytan, delvis under overlayn
    glaze::Label             märkA, märkB;   // två skadeområden isär (D6)
    glaze::DockPanel         extra{"Extra"};   // adopteras i O9

    OverlayVard()
        : glaze::AppWindow("dock_gate overlay", 640, 420, gfx::Chrome::None)
    {
        host.name     = "host";
        host.dock     = glaze::Dock::Fill;
        verktyg.name  = "verktyg";
        verktyg.width = 160;
        verktyg.setPinned(false);   // före adopt — fälten är sanningen
        höger.name    = "hoger";
        höger.dock    = glaze::Dock::Right;
        höger.width   = 150;
        under.name    = "under";
        under.dock    = glaze::Dock::Right;
        under.width   = 150;
        fält.dock     = glaze::Dock::Top;
        reglage.dock  = glaze::Dock::Top;
        etikett.setText("Etikett");
        etikett.dock = glaze::Dock::Fill;
        etikett.disable();
        mittFält.x = 10;
        mittFält.y = 10;
        mittMätare.setMin(0.0f);
        mittMätare.setMax(1.0f);
        mittMätare.setValue(1.0f);
        mittMätare.x     = 10;
        mittMätare.y     = 200;
        mittMätare.width = 60;
        lagret.x      = 110;   // x HH+110…HH+260 — overlayn slutar vid HH+160
        lagret.y      = 100;
        lagret.width  = 150;
        lagret.height = 40;
        märkA.setText("A");
        märkA.x = 10;          // under overlayn
        märkA.y = 300;
        märkB.setText("B");
        märkB.x = 300;         // utanför den
        märkB.y = 300;
        extra.name = "extra";

        root().adopt(host);
        host.adopt(verktyg);
        host.adopt(höger);
        host.adopt(under);
        host.adopt(mitt);
        verktyg.adopt(fält);
        verktyg.adopt(reglage);
        verktyg.adopt(etikett);
        mitt.adopt(mittFält);
        mitt.adopt(mittMätare);
        mitt.adopt(lagret);
        mitt.adopt(märkA);
        mitt.adopt(märkB);
        host.dockPanel(under, höger, glaze::Dock::Bottom);   // en stapel

        verktyg.onChange = [this] { logg.push_back("P:verktyg"); };
        höger.onChange   = [this] { logg.push_back("P:hoger"); };
        under.onChange   = [this] { logg.push_back("P:under"); };
        host.onChange    = [this] { logg.push_back("H"); };
    }

    gfx::Rectf  rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    float       s() { return window().uiScale(); }
    gfx::Pointf P(float lx, float ly) { return {lx * s(), ly * s()}; }
    // Verktygs handtag — det första vid vänsterkanten, en kvadrat överst.
    gfx::Pointf handtag()
    {
        const float HH = style().dockHandleH;
        return P(HH / 2.0f, HH / 2.0f);
    }
    // En punkt i huvudytan, utanför overlayn och handtagen.
    gfx::Pointf ute() { return P(400, 300); }
    // Knappnålen: kvadraten innanför stängknappen längst till höger.
    gfx::Pointf nålKnapp(glaze::DockPanel& p)
    {
        const gfx::Rectf r = rekt(p);
        const float      h = style().dockHeaderH * s();
        return {r.x + r.w - 1.5f * h, r.y + h / 2.0f};
    }
    gfx::Pointf mittpunkt(glaze::Widget& w)
    {
        const gfx::Rectf r = rekt(w);
        return {r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }

    void mus(gfx::EventType t, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
    void klicka(gfx::Pointf p)
    {
        ner(p);
        upp(p);
    }
    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    // TEXT genom fönstrets riktiga ingång — den når bara den fokuserade.
    void text(const char* t)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, t, sizeof e.text - 1);
        handleEvent(e);
    }
    // Hela bilden ur damage-vägen (`full` = efter en FULL omritning). Tom
    // när ytan inte kan läsas.
    std::vector<std::uint8_t> bild(bool full)
    {
        if (full)
            invalidate();
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, Hh = 0;
        if (!window().captureFrame(rgba, W, Hh))
            rgba.clear();
        return rgba;
    }

    // TVÅ PIXLAR ur en FÄRSK bild. `full` = efter en FULL omritning
    // (`AppWindow::drawTree`), annars bara det `skada` invaliderade
    // (`drawTreePartial`). false när ytan inte kan läsas.
    bool pixlar(bool full, glaze::Widget* skada, gfx::Pointf a, gfx::Pointf b,
                std::uint32_t& pa, std::uint32_t& pb)
    {
        window().renderFrame();   // allt annat ritat först
        if (full)
            invalidate();
        else if (skada)
            skada->invalidate();
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, Hh = 0;
        if (!window().captureFrame(rgba, W, Hh))
            return false;
        auto läs = [&](gfx::Pointf p, std::uint32_t& ut) {
            const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
            if (x < 0 || y < 0 || x >= W || y >= Hh)
                return false;
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
                 static_cast<std::size_t>(x)) * 4u;
            ut = static_cast<std::uint32_t>(rgba[i]) << 24 |
                 static_cast<std::uint32_t>(rgba[i + 1]) << 16 |
                 static_cast<std::uint32_t>(rgba[i + 2]) << 8 | rgba[i + 3];
            return true;
        };
        return läs(a, pa) && läs(b, pb);
    }
};

void fallOverlay()
{
    OverlayVard w;
    w.setOffscreen(true);
    // Ytorna mäts utan glidning, och lämna-prövningen går på timerns minsta
    // intervall (0). Glidningen får sin längd i sitt eget steg (D6).
    const glaze::Style förra = w.style();
    glaze::Style       st    = förra;
    st.dockAnimMs            = 0;
    st.dockHideDelayMs       = 0;
    w.setStyle(st);

    bool                               kordes = false;
    std::vector<std::function<void()>> steg;
    std::size_t                        nr = 0;
    auto nästa = [&] {
        if (nr < steg.size())
            glaze::app().post(steg[nr++]);
    };

    // ---- O. KNAPPNÅLEN, OVERLAYN, ESCAPE OCH SNAPPEN — utan tid.
    steg.push_back([&] {
        kordes           = true;
        const float s    = w.s();
        const float HH   = w.style().dockHandleH;
        const float t    = w.style().splitterThick;
        const float Z    = w.style().dockSnapZone;
        const float D    = w.style().dockDefaultSize;
        const float golv = w.style().dockHeaderH;   // inget minsta mått satt

        // ---- O1. STARTLÄGET: Verktyg är onålad — ett handtag, ingen
        // overlay, och layouten skriver den infälld. Mätarens färg läses
        // här, med ingenting ovanpå (O2b:s kontroll).
        const gfx::Rectf  mätare = w.rekt(w.mittMätare);
        const gfx::Pointf påM    = {mätare.x + mätare.w / 2.0f,
                                    mätare.y + mätare.h / 2.0f};
        const gfx::Pointf ref    = {(HH + 150) * s, påM.y};
        std::uint32_t     synlig = 0, oanvänd = 0;
        const bool kanLäsa = w.pixlar(true, nullptr, påM, ref, synlig, oanvänd);
        check(w.verktyg.isCollapsed() && !w.verktyg.isPinned() &&
                  tom(w.rekt(w.verktyg)) && w.host.frontChild() == nullptr,
              "O1: onålad vid start — ett handtag, ingen overlay");
        check(w.host.layout().find(";p=verktyg,1,1,0,0,160,0") !=
                  std::string::npos,
              "O1: layout() skriver den onålad och infälld", w.host.layout());

        // ---- O2. HANDTAGSKLICKET öppnar OVERLAYN: värdens främre barn, vid
        // vänsterkanten innanför handtaget, i sin egen bredd och full höjd —
        // och HUVUDYTAN ÄR OFÖRÄNDRAD. Fokus i panelen och inget onChange:
        // overlayn är ett drifttillstånd, och layouten säger fortfarande
        // infälld.
        const gfx::Rectf mittFöre = w.rekt(w.mitt);
        w.logg.clear();
        w.klicka(w.handtag());
        check(w.host.frontChild() == &w.verktyg && !w.verktyg.isCollapsed() &&
                  !w.verktyg.isPinned(),
              "O2: handtagsklicket — overlayn är värdens främre barn");
        check(lika(w.rekt(w.verktyg), {HH * s, 0, 160 * s, 420 * s}),
              "O2: overlayn vid vänsterkanten, innanför handtaget",
              rekt(w.rekt(w.verktyg)));
        check(exakt(w.rekt(w.mitt), mittFöre),
              "O2: huvudytan är OFÖRÄNDRAD — overlayn trycker inte undan den",
              rekt(w.rekt(w.mitt)));
        check(w.fält.focused() && w.logg.empty(),
              "O2: fokus i panelen, inget onChange", fog(w.logg));
        check(w.host.layout().find(";p=verktyg,1,1,0,0,160,0") !=
                  std::string::npos,
              "O2: layout() säger fortfarande infälld", w.host.layout());

        // ---- O2b. RITORDNINGEN: overlayn ritas OVANPÅ huvudytan. Den fulla
        // mätaren i huvudytan ligger under overlayns yta — pixeln där är
        // densamma som en bit overlay utan något under (panelens yta), i en
        // FULL omritning och i en DELVIS (bara mätaren invaliderad).
        // KONTROLLEN är att mätarens egen färg (O1, utan något ovanpå)
        // skiljer sig från overlayns: annars mäter fallet ingenting, och det
        // hoppas över i stället för att bli grönt.
        {
            std::uint32_t a = 0, b = 0, a2 = 0, b2 = 0;
            if (kanLäsa && w.pixlar(true, nullptr, påM, ref, a, b)) {
                if (synlig == b) {
                    std::printf("  --   O2b: mätaren och overlayn har samma "
                                "färg i den här paletten — ritordningen "
                                "hoppas över\n");
                    ++g_skips;
                } else {
                    check(a == b,
                          "O2b: full omritning — overlayn täcker huvudytans "
                          "mätare");
                    w.pixlar(false, &w.mittMätare, påM, ref, a2, b2);
                    check(a2 == b2,
                          "O2b: delvis omritning (mätaren invaliderad) — "
                          "overlayn ritas ändå sist");
                }
            } else {
                std::printf("  --   O2b: pixlarna kunde inte läsas — "
                            "ritordningen hoppas över\n");
                ++g_skips;
            }
        }

        // ---- O3. TRÄFFEN: en punkt i overlayns fält som också ligger över
        // huvudytans fält träffar OVERLAYNS — det främre barnet träffas
        // först, fast huvudytan står senare i barnordningen.
        w.mittFält.focus();
        const gfx::Rectf a = w.rekt(w.fält), b = w.rekt(w.mittFält);
        const float      x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
        const float      x1 = std::min(a.x + a.w, b.x + b.w);
        const float      y1 = std::min(a.y + a.h, b.y + b.h);
        check(x1 > x0 && y1 > y0,
              "O3: förutsättningen — overlayns fält ligger över huvudytans",
              rekt(a) + " / " + rekt(b));
        w.klicka({(x0 + x1) / 2.0f, (y0 + y1) / 2.0f});
        check(w.fält.focused() && !w.mittFält.focused(),
              "O3: punkten träffar overlayns fält, inte huvudytans");

        // ---- O4. ESCAPE inifrån overlayn fäller in den direkt, och fokus
        // går till värden (handtaget valt) — inget onChange.
        w.logg.clear();
        w.tangent(gfx::Key::Escape);
        check(w.verktyg.isCollapsed() && w.host.frontChild() == nullptr &&
                  w.host.focused() && !w.fält.focused() && w.logg.empty(),
              "O4: Escape — infälld, fokus på värden, inget onChange",
              fog(w.logg));

        // ---- O5. KNAPPNÅLEN I OVERLAYN fäster den: dockad vid vänsterkanten
        // i sin bredd (handtagsremsan är borta) och onChange.
        w.klicka(w.handtag());
        w.logg.clear();
        w.klicka(w.nålKnapp(w.verktyg));
        check(w.verktyg.isPinned() && !w.verktyg.isCollapsed() &&
                  w.host.frontChild() == nullptr &&
                  lika(w.rekt(w.verktyg), {0, 0, 160 * s, 420 * s}) &&
                  w.logg == Logg{"P:verktyg", "H"},
              "O5: knappnålen i overlayn — fäst och dockad, onChange",
              rekt(w.rekt(w.verktyg)) + " " + fog(w.logg));

        // ---- O6. KNAPPNÅLEN PÅ EN DOCKAD panel: onålad och infälld igen.
        w.logg.clear();
        w.klicka(w.nålKnapp(w.verktyg));
        check(!w.verktyg.isPinned() && w.verktyg.isCollapsed() &&
                  tom(w.rekt(w.verktyg)) && w.logg == Logg{"P:verktyg", "H"},
              "O6: knappnålen på en dockad panel — infälld, onChange",
              fog(w.logg));

        // ---- O7. SNAPPEN I REMSSKIKTET. Högerremsan är 150 bred, och
        // skiktet står innanför den. Draget till två px innanför zonen från
        // det förvalda måttet ger EXAKT det förvalda — till båda panelerna i
        // remsan. Under golvet minus zonen visar förhandsvisningen remsans
        // paneler INFÄLLDA, och släppet fäller in dem med måttet kvar.
        auto skiktX = [&](float bredd) { return 640.0f - bredd - t / 2.0f; };
        w.logg.clear();
        w.ner(w.P(skiktX(150), 300));
        w.flytta(w.P(skiktX(D - (Z - 2.0f)), 300));
        w.upp(w.P(skiktX(D - (Z - 2.0f)), 300));
        check(w.höger.width == D && w.under.width == D &&
                  w.logg == Logg{"P:hoger", "P:under", "H"},
              "O7: nära det förvalda måttet — exakt det förvalda, i båda",
              std::to_string(w.höger.width) + " " + fog(w.logg));
        w.logg.clear();
        w.ner(w.P(skiktX(D), 300));
        w.flytta(w.P(skiktX(golv - Z - 5.0f), 300));
        check(tom(w.rekt(w.höger)) && tom(w.rekt(w.under)) &&
                  !w.höger.isCollapsed() && w.logg.empty(),
              "O7: under golvet minus zonen — förhandsvisningen visar remsan "
              "infälld, och ingenting är ändrat än",
              rekt(w.rekt(w.höger)));
        w.upp(w.P(skiktX(golv - Z - 5.0f), 300));
        check(w.höger.isCollapsed() && w.under.isCollapsed() &&
                  w.höger.width == D && w.under.width == D &&
                  w.logg == Logg{"P:hoger", "P:under", "H"},
              "O7: släppet fäller in båda, och måttet står kvar",
              fog(w.logg));
        w.höger.setCollapsed(false);
        w.under.setCollapsed(false);

        // ---- O8. SNAPPEN I CELLSKIKTET: först en ojämn delning (100 över),
        // därefter två px innanför zonen från den JÄMNA delningen — exakt jämnt
        // — och sist Höger under sitt golv minus zonen: förhandsvisningen
        // visar den infälld och Under i hela remsan, och släppet fäller in
        // den. Vikterna står kvar, så den fälls ut i sin gamla längd.
        const float par = 420.0f - t;   // remsans två celler
        auto skiktY = [&](float övre) { return övre + t / 2.0f; };
        w.logg.clear();
        w.ner(w.P(640.0f - D / 2.0f, skiktY(par / 2.0f)));
        w.flytta(w.P(640.0f - D / 2.0f, skiktY(100)));
        w.upp(w.P(640.0f - D / 2.0f, skiktY(100)));
        check(lika(w.rekt(w.höger), {(640 - D) * s, 0, D * s, 100 * s}) &&
                  w.logg == Logg{"P:hoger", "P:under", "H"},
              "O8: cellskiktet till 100 — en ojämn delning",
              rekt(w.rekt(w.höger)));
        w.ner(w.P(640.0f - D / 2.0f, skiktY(100)));
        w.flytta(w.P(640.0f - D / 2.0f, skiktY(par / 2.0f + Z - 2.0f)));
        w.upp(w.P(640.0f - D / 2.0f, skiktY(par / 2.0f + Z - 2.0f)));
        check(nära(w.rekt(w.höger).h, w.rekt(w.under).h) &&
                  nära(w.rekt(w.höger).h, par / 2.0f * s),
              "O8: nära den jämna delningen — exakt jämnt",
              rekt(w.rekt(w.höger)) + " / " + rekt(w.rekt(w.under)));
        w.logg.clear();
        w.ner(w.P(640.0f - D / 2.0f, skiktY(par / 2.0f)));
        w.flytta(w.P(640.0f - D / 2.0f, skiktY(golv - Z - 5.0f)));
        check(tom(w.rekt(w.höger)) && nära(w.rekt(w.under).h, 420 * s) &&
                  w.logg.empty(),
              "O8: under golvet minus zonen — Höger visas infälld och Under "
              "fyller remsan",
              rekt(w.rekt(w.under)));
        w.upp(w.P(640.0f - D / 2.0f, skiktY(golv - Z - 5.0f)));
        check(w.höger.isCollapsed() && !w.under.isCollapsed() &&
                  w.logg == Logg{"P:hoger", "H"},
              "O8: släppet fäller in Höger — bara den", fog(w.logg));
        w.höger.setCollapsed(false);
        check(nära(w.rekt(w.höger).h, w.rekt(w.under).h),
              "O8: utfälld igen i sin gamla längd — vikterna stod kvar",
              rekt(w.rekt(w.höger)));

        // ---- O9. EN UNDANTRÄNGD OVERLAY BEHÅLLER INTE FOKUS.
        // Verktygs overlay har fokus i sitt fält när appen adopterar en
        // synlig, onålad, UTFÄLLD panel: den nya är den senast öppnade och
        // vinner, Verktyg fälls in, och fokus lämnar dess fält — text når
        // det inte.
        w.klicka(w.handtag());
        w.text("a");
        check(w.fält.focused() && w.fält.text() == "a",
              "O9: overlayns fält har fokus och tar emot text", w.fält.text());
        w.extra.setPinned(false);
        w.extra.setCollapsed(false);
        w.host.adopt(w.extra);
        check(w.host.frontChild() == &w.extra && w.verktyg.isCollapsed() &&
                  !w.fält.focused(),
              "O9: den adopterade panelen trängde undan overlayn — och fokus "
              "lämnade dess fält");
        w.text("b");
        check(w.fält.text() == "a",
              "O9: …och texten når inte den undanträngda overlayns fält",
              w.fält.text());
        w.host.unadopt(w.extra);
        check(w.host.frontChild() == nullptr && w.extra.parent() == nullptr,
              "O9: (städat) den extra panelen är ute");

        // ---- O10. OVERLAYNS TÄCKNING GÄLLER LAGREN. En
        // widget i huvudytan med ett overlay-lager ligger delvis under den
        // öppna overlayn: lagret ritas bara UTANFÖR overlayns yta — inget
        // klipp lagret ritas i snittar overlayn, och den synliga delen ritas.
        // Kontrollen: infälld ritas lagret över hela sin yta.
        {
            const gfx::Rectf  ly    = w.rekt(w.lagret);
            const gfx::Pointf under = {(HH + 130) * s, ly.y + ly.h / 2.0f};
            const gfx::Pointf synlig = {(HH + 200) * s, ly.y + ly.h / 2.0f};
            auto innehåller = [](const gfx::Rectf& r, gfx::Pointf p) {
                return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y &&
                       p.y < r.y + r.h;
            };
            w.lagret.klipp.clear();
            w.window().renderFrame();
            bool täcktDär = false;
            for (const gfx::Rectf& k : w.lagret.klipp)
                täcktDär = täcktDär || innehåller(k, under);
            check(!w.lagret.klipp.empty() && täcktDär,
                  "O10: (kontrollen) infälld ritas lagret också där overlayn "
                  "kommer att ligga");
            w.klicka(w.handtag());
            const gfx::Rectf o = w.rekt(w.verktyg);
            w.lagret.klipp.clear();
            w.window().renderFrame();
            bool snitt = false, syns = false;
            for (const gfx::Rectf& k : w.lagret.klipp) {
                const float x0 = std::max(k.x, o.x);
                const float y0 = std::max(k.y, o.y);
                const float x1 = std::min(k.x + k.w, o.x + o.w);
                const float y1 = std::min(k.y + k.h, o.y + o.h);
                snitt = snitt || (x1 - x0 > 0.5f && y1 - y0 > 0.5f);
                syns  = syns || innehåller(k, synlig);
            }
            check(w.host.frontChild() == &w.verktyg && !snitt,
                  "O10: inget klipp lagret ritas i snittar den öppna "
                  "overlayn",
                  std::to_string(w.lagret.klipp.size()) + " klipp");
            check(syns, "O10: …och lagrets synliga del ritas");
            w.tangent(gfx::Key::Escape);   // infälld igen inför D
        }
        nästa();
    });

    // ---- D. LÄMNA-PRÖVNINGEN (D5) med RIKTIG tid. Overlayn fälls in först
    // när pekaren, fokus, ett tryck och popuperna alla är utanför panelen.
    // "Står kvar" mäts över 120 ms (tolv intervall); "fälls in" väntar högst
    // två sekunder. D2 visar att timern faktiskt går i den här miljön, så D1:s
    // "står kvar" är inte tom.
    steg.push_back([&] {
        // ---- D1. FOKUS I PANELEN: pekaren ut, men fältet har fokus — öppen.
        w.klicka(w.handtag());
        w.flytta(w.ute());
        vänta([] { return false; }, 120, [&](bool) {
            check(w.host.frontChild() == &w.verktyg && w.fält.focused(),
                  "D1: pekaren ute men fokus i panelens fält — overlayn står "
                  "kvar");
            nästa();
        });
    });
    steg.push_back([&] {
        // ---- D2. …och FOKUS UT: pekaren ute, fokus ute — infälld, utan
        // onChange (den fälldes in av sig själv).
        w.logg.clear();
        w.mittFält.focus();
        vänta([&] { return w.verktyg.isCollapsed(); }, 2000, [&](bool) {
            check(w.verktyg.isCollapsed() && w.host.frontChild() == nullptr &&
                      w.logg.empty(),
                  "D2: pekaren ute, fokus ute — overlayn fälls in, inget "
                  "onChange",
                  fog(w.logg));
            nästa();
        });
    });
    steg.push_back([&] {
        // ---- D3. PEKAREN ÖVER OVERLAYN — över den INAKTIVERADE etiketten,
        // där ingen widget är hovrad — med fokus ute: öppen. Pekaren ut:
        // infälld.
        w.klicka(w.handtag());
        w.mittFält.focus();
        w.flytta(w.mittpunkt(w.etikett));
        vänta([] { return false; }, 120, [&](bool) {
            check(w.host.frontChild() == &w.verktyg && !w.etikett.hovered(),
                  "D3: pekaren över en inaktiverad etikett i overlayn, fokus "
                  "ute — overlayn står kvar");
            w.flytta(w.ute());
            vänta([&] { return w.verktyg.isCollapsed(); }, 2000, [&](bool) {
                check(w.verktyg.isCollapsed(),
                      "D3: …och pekaren ut — overlayn fälls in");
                nästa();
            });
        });
    });
    steg.push_back([&] {
        // ---- D4. ETT TRYCK PÅ REGLAGET i panelen, fokus flyttat ut och
        // pekaren ute: öppen så länge trycket pågår — släppt: infälld.
        w.klicka(w.handtag());
        w.ner(w.mittpunkt(w.reglage));
        w.mittFält.focus();
        w.flytta(w.ute());
        vänta([] { return false; }, 120, [&](bool) {
            check(w.host.frontChild() == &w.verktyg && w.reglage.pressed() &&
                      !w.fält.focused() && !w.reglage.focused(),
                  "D4: ett tryck på reglaget pågår, pekaren och fokus ute — "
                  "overlayn står kvar");
            w.upp(w.ute());
            vänta([&] { return w.verktyg.isCollapsed(); }, 2000, [&](bool) {
                check(w.verktyg.isCollapsed(),
                      "D4: …och trycket släppt — overlayn fälls in");
                nästa();
            });
        });
    });

    // ---- D5. VÄRDEN DÖLJS: lämna-prövningen stannar —
    // tickleveranserna upphör — och startar igen när värden visas. Mätt med
    // en overlay vars villkor ALLA är uppfyllda för att fällas in: med
    // pekaren och fokus ute hade en levererad tick fällt in den, så en
    // overlay som står kvar medan värden är dold betyder att inga tick kom.
    // Visad igen fälls den in inom två sekunder.
    steg.push_back([&] {
        w.klicka(w.handtag());
        w.mittFält.focus();
        w.host.hide();
        w.flytta(w.ute());
        vänta([] { return false; }, 120, [&](bool) {
            check(w.verktyg.isCollapsed() == false &&
                      w.host.frontChild() == &w.verktyg,
                  "D5: värden dold — inga tick, overlayn står kvar fast "
                  "pekaren och fokus är ute");
            w.host.show();
            vänta([&] { return w.verktyg.isCollapsed(); }, 2000, [&](bool) {
                check(w.verktyg.isCollapsed(),
                      "D5: värden visad igen — prövningen startade om och "
                      "overlayn fälls in");
                nästa();
            });
        });
    });

    // ---- D6. GLIDNINGEN (`dockAnimMs = 160`) med pumpen igång: overlayns
    // bredd ses STRIKT mellan start och slut under förloppet, och när den
    // når full bredd ber värden inte om fler bilder — `damageStamp()` står
    // still över tre bilder. Medan den glider invalideras två små widgets
    // långt isär i varje pumpvarv, så bilderna ritas i FLERA PARTIELLA
    // skadeområden: slutbilden ur skadevägen är då exakt
    // densamma som en full omritning.
    bool  glidMellan = false;   // D6: en bredd strikt mellan start och slut
    float glidFull   = 0.0f;    // D6: overlayns fulla bredd, fysiska px
    steg.push_back([&] {
        glaze::Style glid = w.style();
        glid.dockAnimMs   = 160;
        w.setStyle(glid);
        w.window().renderFrame();   // värden har ritats — D6:s villkor
        glidFull = 160.0f * w.s();
        w.klicka(w.handtag());
        w.flytta(w.ute());          // fokus är kvar i fältet — overlayn står
        vänta(
            [&] {
                w.märkA.invalidate();
                w.märkB.invalidate();
                const float b = w.rekt(w.verktyg).w;
                if (b > 0.0f && b < glidFull)
                    glidMellan = true;
                return nära(b, glidFull);
            },
            2000, [&](bool) { nästa(); });
    });
    // Ett varv till i pumpen: ett sista steg som väntar i svansen hinner
    // fram och ritar slutläget.
    steg.push_back([&] { vänta([] { return false; }, 100, [&](bool) {
        nästa(); }); });
    steg.push_back([&] {
        check(glidMellan,
              "D6: under glidningen ligger bredden strikt mellan start och "
              "slut");
        w.window().renderFrame();
        const std::uint64_t stämpel = w.host.damageStamp();
        w.window().renderFrame();
        w.window().renderFrame();
        w.window().renderFrame();
        check(nära(w.rekt(w.verktyg).w, glidFull) &&
                  w.host.damageStamp() == stämpel,
              "D6: framme — full bredd, och stämpeln står still över tre "
              "bilder",
              std::to_string(w.rekt(w.verktyg).w));
        const std::vector<std::uint8_t> delvis = w.bild(false);
        if (delvis.empty()) {
            std::printf("  --   D6: bilden kunde inte läsas — slutbildens "
                        "jämförelse hoppas över\n");
            ++g_skips;
        } else {
            check(delvis == w.bild(true),
                  "D6: slutbilden ur de partiella skadeområdena är exakt en "
                  "full omritning");
        }
        nästa();
    });

    // ---- D7. FÖNSTRET DÖLJS: lämna-prövningen stannar med det —
    // samma mätning som D5. Sist, för det dolda fönstret visas inte igen
    // (ett offscreen-fönster hade blivit synligt på skärmen).
    steg.push_back([&] {
        w.mittFält.focus();
        // Inne i den öppna overlayn (vid kanten, innanför handtaget).
        w.flytta(w.P(w.style().dockHandleH + 80.0f, 200));
        w.hide();
        w.flytta(w.ute());
        vänta([] { return false; }, 120, [&](bool) {
            check(!w.verktyg.isCollapsed() &&
                      w.host.frontChild() == &w.verktyg,
                  "D7: fönstret dolt — inga tick, overlayn står kvar fast "
                  "pekaren och fokus är ute");
            nästa();
        });
    });
    steg.push_back([&] { w.close(); });

    nästa();
    w.run();
    w.setStyle(förra);

    if (!kordes) {
        std::printf("  --   overlayfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// ====================================================== BANDEN MED FÖNSTER
//
// GREPPDRAGET genom fönstrets riktiga ingång: tre fält i band
// 0 överst. Fönstret har den RIKTIGA fonten, så måtten läses ur fälten själva
// — referensen är strukturen (`layout()`, `band()`, `dock`) och var ytorna hamnar
// i förhållande till varandra.

struct BandVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    std::vector<std::string> logg;
    glaze::DockHost          host;
    glaze::ToolBar           t1, t2, t3;
    glaze::ToolButton        k1{"AB"}, k2{"CDE"}, k3{"F"};
    glaze::TextInput         fältT;   // i t2 — fokus i ett band (FB6)
    glaze::Panel             mitt;

    BandVard() : glaze::AppWindow("dock_gate band", 640, 420, gfx::Chrome::None)
    {
        host.name = "host";
        host.dock = glaze::Dock::Fill;
        t1.name   = "t1";
        t2.name   = "t2";
        t3.name   = "t3";
        root().adopt(host);
        host.adopt(t1);
        t1.adopt(k1);
        host.adopt(t2);
        t2.adopt(k2);
        t2.adopt(fältT);
        host.adopt(t3);
        t3.adopt(k3);
        host.adopt(mitt);
        host.onChange = [this] { logg.push_back("H"); };
    }

    gfx::Rectf  rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    float       s() { return window().uiScale(); }
    gfx::Pointf P(float lx, float ly) { return {lx * s(), ly * s()}; }
    // Mitten av fältets GREPP — i början, `toolGripW` långt.
    gfx::Pointf grepp(glaze::ToolBar& t)
    {
        const gfx::Rectf r = rekt(t);
        const float      g = style().toolGripW * s();
        return t.isVertical() ? gfx::Pointf{r.x + r.w / 2.0f, r.y + g / 2.0f}
                              : gfx::Pointf{r.x + g / 2.0f, r.y + r.h / 2.0f};
    }
    void mus(gfx::EventType typ, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = typ;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
    void dra(gfx::Pointf från, gfx::Pointf till)
    {
        ner(från);
        flytta(till);
        upp(till);
    }
    void enkel(gfx::EventType typ)
    {
        gfx::Event e;
        e.type     = typ;
        e.windowId = window().id();
        handleEvent(e);
    }
    void text(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, s, sizeof e.text - 1);
        handleEvent(e);
    }
    bool pixel(gfx::Pointf p, std::uint32_t& ut)
    {
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, Hh = 0;
        if (!window().captureFrame(rgba, W, Hh))
            return false;
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        if (x < 0 || y < 0 || x >= W || y >= Hh)
            return false;
        const std::size_t i =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
             static_cast<std::size_t>(x)) * 4u;
        ut = static_cast<std::uint32_t>(rgba[i]) << 24 |
             static_cast<std::uint32_t>(rgba[i + 1]) << 16 |
             static_cast<std::uint32_t>(rgba[i + 2]) << 8 | rgba[i + 3];
        return true;
    }
};

void fallBandFonster()
{
    BandVard w;
    w.setOffscreen(true);

    bool kordes  = false;
    bool tappade = false;   // fältets onFocusLost (FB6) — levereras i svansen
    glaze::app().post([&] {
        kordes = true;
        const float s  = w.s();
        const float TH = w.style().toolBarH;
        const float Z  = w.style().dockDropZone;

        // ---- FB0. PEKAREN över greppet, och startläget.
        w.flytta(w.grepp(w.t1));
        check(w.window().cursor() == gfx::PointerShape::Move,
              "FB0: flyttpekaren över greppet");
        check(w.host.layout() ==
                  "v2;t=t1,top,0,0,0;t=t2,top,0,1,0;t=t3,top,0,2,0",
              "FB0: tre fält i band 0", w.host.layout());

        // ---- FB1. t2 till ett NYTT innersta band — släppt i zonen under
        // band 0. Indikatorn är t2:s kommande yta: pixeln mitt i den ändras
        // under draget.
        w.logg.clear();
        const gfx::Rectf  r2   = w.rekt(w.t2);
        const gfx::Pointf mål1 = w.P(300, TH + Z / 2);
        const gfx::Pointf ny   = {r2.w / 2.0f, (TH + TH / 2) * s};
        std::uint32_t     före = 0, under = 0;
        const bool        kanLäsa = w.pixel(ny, före);
        w.ner(w.grepp(w.t2));
        w.flytta(mål1);
        if (kanLäsa) {
            w.pixel(ny, under);
            check(under != före,
                  "FB1: indikatorn står där t2 hamnar — i det nya bandet");
        } else {
            std::printf("  --   FB1: pixlarna kunde inte läsas — "
                        "indikatorns pixelkontroll hoppas över\n");
            ++g_skips;
        }
        check(w.logg.empty(), "FB1: ingenting flyttar före släppet");
        w.upp(mål1);
        check(w.host.layout() ==
                      "v2;t=t1,top,0,0,0;t=t3,top,0,1,0;t=t2,top,1,0,0" &&
                  w.t2.band() == 1 && w.logg == Logg{"H"},
              "FB1: t2 i band 1, `band()` tillbakaskrivet, värdens onChange",
              w.host.layout() + " " + fog(w.logg));
        check(nära(w.rekt(w.t2).y, TH * s) && nära(w.rekt(w.t2).x, 0.0f),
              "FB1: t2 står först i band 1, under band 0",
              rekt(w.rekt(w.t2)));

        // ---- FB2. MITTPUNKTSREGELN: t3 släppt en fjärdedel in i t1 —
        // innanför t1:s vänsterkant men före dess mitt — hamnar FÖRE t1.
        w.logg.clear();
        const gfx::Rectf r1 = w.rekt(w.t1);
        w.dra(w.grepp(w.t3), {r1.x + r1.w / 4.0f, r1.y + r1.h / 2.0f});
        check(w.host.layout() ==
                      "v2;t=t3,top,0,0,0;t=t1,top,0,1,0;t=t2,top,1,0,0" &&
                  w.logg == Logg{"H"},
              "FB2: t3 före t1 i band 0", w.host.layout());

        // ---- FB3. t1 till band 1, bortom t2:s mitt — efter t2.
        w.logg.clear();
        const gfx::Rectf b2 = w.rekt(w.t2);
        w.dra(w.grepp(w.t1), {b2.x + b2.w + 4.0f * s, b2.y + b2.h / 2.0f});
        check(w.host.layout() ==
                  "v2;t=t3,top,0,0,0;t=t2,top,1,0,0;t=t1,top,1,1,0",
              "FB3: t1 efter t2 i band 1", w.host.layout());

        // ---- FB4. t3 till VÄNSTERKANTEN (zonen innanför bandområdet där):
        // ett nytt band, och fältet STÅR. Band 0 överst blir tomt och tas
        // bort, så t2 och t1 flyttar ut.
        w.logg.clear();
        w.dra(w.grepp(w.t3), w.P(Z / 2, 300));
        const gfx::Rectf r3 = w.rekt(w.t3);
        check(w.t3.dock == glaze::Dock::Left && w.t3.isVertical() &&
                  w.t3.band() == 0 && r3.h > r3.w && nära(r3.x, 0.0f),
              "FB4: t3 står vid vänsterkanten", rekt(r3));
        check(w.host.layout() ==
                      "v2;t=t2,top,0,0,0;t=t1,top,0,1,0;t=t3,left,0,0,0" &&
                  w.logg == Logg{"H"},
              "FB4: …och strängen säger det", w.host.layout());

        // ---- FB5. FOKUSFÖRLUST mitt i ett greppdrag: indikatorn är borta
        // så fort trycket avbryts, och ingenting flyttar.
        w.logg.clear();
        const std::string före5 = w.host.layout();
        w.ner(w.grepp(w.t1));
        w.flytta(w.P(300, TH + Z / 2));
        w.enkel(gfx::EventType::WindowFocusLost);
        w.upp(w.P(300, TH + Z / 2));
        check(w.host.layout() == före5 && w.logg.empty(),
              "FB5: fokusförlust mitt i draget — ingen flytt, inget onChange",
              w.host.layout());
        w.enkel(gfx::EventType::WindowFocusGained);

        // ---- FB6. ETT DOLT BAND MED FOKUS:
        // fokus i ett fält i t2, och t2 döljs — med `hide()` och med
        // `setLayout(…t=t2,…,1…)`. Fokus lämnar fältet (huvudytan har inget
        // stopp och värden är inget, så fönstret står utan fokus), text når
        // inte det dolda fältet, och fältets `onFocusLost` levereras.
        w.fältT.onFocusLost = [&tappade] { tappade = true; };
        w.fältT.focus();
        w.text("x");
        check(w.fältT.focused() && w.fältT.text() == "x",
              "FB6: fokus i bandets fält, och texten når det", w.fältT.text());
        w.t2.hide();
        check(!w.fältT.focused() && !w.host.focused(),
              "FB6: t2 dold — fokus lämnar fältet, fönstret står utan fokus");
        w.text("y");
        check(w.fältT.text() == "x",
              "FB6: …och texten når inte det dolda fältet", w.fältT.text());
        w.t2.show();
        w.fältT.focus();
        check(w.host.setLayout("v2;t=t2,top,0,0,1;t=t1,top,0,1,0;"
                               "t=t3,left,0,0,0") &&
                  !w.t2.isVisible() && !w.fältT.focused(),
              "FB6: setLayout döljer bandet — fokus lämnar fältet");
        w.t2.show();

        // Fokushändelserna levereras i pump-svansen — prövas i ett eget
        // steg, köat EFTER dem.
        glaze::app().post([&] {
            check(tappade, "FB6: fältets onFocusLost levererades");
            w.close();
        });
    });
    w.run();

    if (!kordes) {
        std::printf("  --   bandfallen med fönster hoppades över: inget "
                    "fönster kunde skapas (ingen display)\n");
        ++g_skips;
    }
}

// ============================================================ FLYTANDE
//
// DE FLYTANDE PANELERNA genom fönstrens riktiga ingångar, i DOLDA fönster
// (`hide()` före `run()`, inte offscreen — ett offscreen-fönster får inga
// flytande fönster, OFF). Riggen `FlytVard` startar med Bevakning FLYTANDE
// (satt före adoptionen, som demons `.glazeui`) och huvudfönstret DOLT, så
// det flytande fönstret skapas dolt; bara FL5 och OV behöver
// synliga fönster — en ritning är den väntande flyttens klocka. Det LEVANDE
// DRAGET (LV) går i de dolda fönstren: syntetiska händelser
// genom huvudfönstrets ingång (fall A), postade Win32-meddelanden till det
// flytande fönstret (fall B), och platshållarens pixlar ur `renderFrame`.
// Dragriggarna (F, FB) står i OFFSCREEN-fönster, där draget aldrig
// bärs (`canCarry`) — de mäter alltså vägen utan bärande. Tre egna
// riggar till: offscreen-värden (OFF), tre fönsternivåer med veto (T)
// och rivningen (R, med den returnerande panelen som
// dör under väntan). `fallReservvagen` mäter de två generella krokarna
// (`acceleratorFallback`, `ownerVisibilityChanged`) med ett eget fönsterpar.
//
// På WINDOWS (D3D11) matas det flytande fönstret med POSTADE Win32-meddelanden
// till dess HWND (som i tray_gate — `FindWindowW` med en titel unik per
// process): F5 i det (FL6) och ett rubrikdrag som fönstret följer (LV3, TOM,
// FT10 — `postatDrag`). Ett dolt fönster får inga riktiga mushändelser, så
// pekaren på skärmen stör inte.

// Står fönstret kvar i appens register? Pekaren JÄMFÖRS bara — ett rivet
// fönster derefereras aldrig.
bool registrerat(const glaze::AppWindow* f)
{
    const std::vector<glaze::AppWindow*>& v = glaze::app().windows();
    return std::find(v.begin(), v.end(), f) != v.end();
}

#ifdef GLAZE_BACKEND_D3D11
HWND hwndFör(const std::string& titel)
{
    const std::wstring w(titel.begin(), titel.end());   // titlarna är ASCII
    return FindWindowW(nullptr, w.c_str());
}

// Ett musmeddelande i fönstrets KLIENTRYMD, som ett från OS:et. Backenden tar
// capture vid trycket, så punkter UTANFÖR fönstret (negativa, stora) går fram;
// de packas som `short` (GET_X_LPARAM).
void postaMus(HWND h, UINT msg, int x, int y, WPARAM knappar)
{
    PostMessageW(h, msg, knappar,
                 MAKELPARAM(static_cast<WORD>(static_cast<short>(x)),
                            static_cast<WORD>(static_cast<short>(y))));
}

void postaTangent(HWND h, UINT vk)
{
    PostMessageW(h, WM_KEYDOWN, vk, 1);
    PostMessageW(h, WM_KEYUP, vk, static_cast<LPARAM>(0xC0000001u));
}

// LIVSTIDSLUCKANS HÄNDELSER — ett klick per
// punkt (två på samma punkt i följd är ett dubbelklick) och sist tangenten
// 'A' UTAN släpp, så att `isKeyHeld` visar att den kom fram. Punkterna är
// heltal (klientrymden, fysiska px). Inget WM_MOUSEMOVE med flit: rörelsen ber
// om WM_MOUSELEAVE (TrackMouseEvent), som med den verkliga pekaren någon
// annanstans kommer direkt och släcker positionen kontrollen läser.
void postaLuckhändelser(HWND h, const std::vector<gfx::Pointf>& klick)
{
    for (const gfx::Pointf& p : klick) {
        const int x = static_cast<int>(p.x);
        const int y = static_cast<int>(p.y);
        postaMus(h, WM_LBUTTONDOWN, x, y, MK_LBUTTON);
        postaMus(h, WM_LBUTTONUP, x, y, 0);
    }
    PostMessageW(h, WM_KEYDOWN, 'A', 1);
}

// En punkt i SKÄRMEN (placeringsrymden — Win32 fysiska px) som ett
// musmeddelande till `f`, med klientkoordinater ur fönstrets NUVARANDE hörn.
void postaVid(HWND h, glaze::AppWindow& f, UINT msg, gfx::Pointf skärm,
              WPARAM knappar)
{
    const gfx::Pointf o = f.window().clientToScreen(0.0f, 0.0f);
    postaMus(h, msg, static_cast<int>(std::lround(skärm.x - o.x)),
             static_cast<int>(std::lround(skärm.y - o.y)), knappar);
}

// ETT POSTAT DRAG mot ett fönster som FÖLJER PEKAREN. Win32-
// meddelanden bär KLIENTkoordinater, och det levande dragets fönster flyttar
// sig mellan två meddelanden: varje punkt hålls i skärmen och görs om mot
// fönstrets nuvarande hörn precis innan den postas (`postaVid`), och nästa
// postas först när den förra levererats — ett pumpvarv (`vänta` med ett sant
// villkor körs i nästa svans, efter nästa varvs händelser).
//
// MOTTAGAREN EFTER TRÖSKELN är fallets ANTAGANDE, gjort i förväg ur en
// förutsättning, och aldrig avläst ur utfallet:
// `övertag` — värden tar ett verktygsfälts tryck vid lossningen
// (`takePointerCapture`, som kräver förgrunden) — och då går resten till
// värdens fönster, som OS:et gör med en flyttad capture; annars till källan.
// `efterTröskel` mäter antagandet när tröskelrörelsen levererats och får veta
// om källans fönster höll OS:ets capture efter trycket; `under` körs medan
// pekaren står över målet, före släppet; `klar` sist. Ett fönster som inte
// längre står i registret får inga fler meddelanden och derefereras inte
// `klar` körs ändå, och fallet ser vad som hände.
struct PostDrag {
    HWND              källaH  = nullptr;
    glaze::AppWindow* källa   = nullptr;
    HWND              värdH   = nullptr;
    glaze::AppWindow* värd    = nullptr;
    gfx::Pointf       ner{};              // trycket, skärmen
    gfx::Pointf       mål{};              // släppet, skärmen
    float             tröskel = 0.0f;     // rörelsen över tröskeln, x
    bool              övertag = false;    // värden tar trycket vid tröskeln
};

// Ett meddelande till ett fönster som fortfarande är registrerat.
bool postaOmKvar(HWND h, glaze::AppWindow* f, UINT msg, gfx::Pointf skärm,
                 WPARAM knappar)
{
    if (!registrerat(f))
        return false;
    postaVid(h, *f, msg, skärm, knappar);
    return true;
}

void postatDrag(PostDrag d, std::function<void(bool)> efterTröskel,
                std::function<void()> under, std::function<void()> klar)
{
    if (!postaOmKvar(d.källaH, d.källa, WM_LBUTTONDOWN, d.ner, MK_LBUTTON)) {
        klar();
        return;
    }
    vänta([] { return true; }, 1000, [d, efterTröskel, under, klar](bool) {
        const bool fångat =
            registrerat(d.källa) && d.källa->window().hasMouseCapture();
        if (!postaOmKvar(d.källaH, d.källa, WM_MOUSEMOVE,
                         gfx::Pointf{d.ner.x + d.tröskel, d.ner.y},
                         MK_LBUTTON)) {
            klar();
            return;
        }
        vänta([] { return true; }, 1000,
              [d, efterTröskel, under, klar, fångat](bool) {
            if (efterTröskel)
                efterTröskel(fångat);
            HWND const              h = d.övertag ? d.värdH : d.källaH;
            glaze::AppWindow* const f = d.övertag ? d.värd : d.källa;
            if (!postaOmKvar(h, f, WM_MOUSEMOVE, d.mål, MK_LBUTTON)) {
                klar();
                return;
            }
            vänta([] { return true; }, 1000, [d, under, klar, h, f](bool) {
                if (under)
                    under();
                postaOmKvar(h, f, WM_LBUTTONUP, d.mål, 0);
                vänta([] { return true; }, 1000, [klar](bool) { klar(); });
            });
        });
    });
}

// Äger grindens tråd förgrunden? Bara då tar värden över ett verktygsfälts
// tryck (`captureMouse` — capture_gate D6 och D8).
bool förgrundÄrVår()
{
    const HWND f = GetForegroundWindow();
    return f && GetWindowThreadProcessId(f, nullptr) == GetCurrentThreadId();
}

// FÖRGRUNDEN TILL `h` och prövad — förberedelsen för ett fall som kräver den.
// Svarar om den gick igenom; en
// session utan förgrund (ctest utan skrivbord) nekar den.
bool görTillFörgrund(HWND h)
{
    if (!h)
        return false;
    SetForegroundWindow(h);
    return GetForegroundWindow() == h;
}
#endif

// Ett unikt fönsternamn per process: på Windows är titeln FindWindowW-nyckeln.
std::string unikTitel(const char* del)
{
#ifdef GLAZE_BACKEND_D3D11
    return std::string("dock_gate-") + del + "-" +
           std::to_string(GetCurrentProcessId());
#else
    return std::string("dock_gate-") + del;
#endif
}

// En widget vars popup grinden styr —
// `hasOpenPopup()` är en virtual, så en öppen lista spelas utan ett fönster.
struct PopupYta final : glaze::Widget {
    bool öppen = false;
    void draw(const glaze::Ctx&) override {}
    bool hasOpenPopup() const override { return öppen; }
};

// Posterna i en layoutsträng, och fält `i` i den post som börjar med `tagg`
// (tom när posten saknas).
std::vector<std::string> poster(const std::string& s)
{
    std::vector<std::string> ut;
    std::size_t              start = 0;
    while (true) {
        const std::size_t p = s.find(';', start);
        ut.push_back(s.substr(start, p - start));
        if (p == std::string::npos)
            return ut;
        start = p + 1;
    }
}

std::string postFält(const std::string& layout, const std::string& tagg,
                     std::size_t i)
{
    for (const std::string& post : poster(layout)) {
        if (post.compare(0, tagg.size(), tagg) != 0)
            continue;
        std::vector<std::string> fält;
        std::size_t              start = tagg.size();
        while (true) {
            const std::size_t p = post.find(',', start);
            fält.push_back(post.substr(start, p - start));
            if (p == std::string::npos)
                break;
            start = p + 1;
        }
        return i < fält.size() ? fält[i] : std::string();
    }
    return std::string();
}

// Står två p=-poster direkt efter varandra (samma cell)?
bool sammaCell(const std::string& layout, const std::string& a,
               const std::string& b)
{
    const std::vector<std::string> v = poster(layout);
    for (std::size_t i = 0; i + 1 < v.size(); ++i)
        if (v[i].rfind("p=" + a + ",", 0) == 0 &&
            v[i + 1].rfind("p=" + b + ",", 0) == 0)
            return true;
    return false;
}

std::size_t antalFönster()
{
    return glaze::app().windowCount();
}

// KONTROLLEN I LIVSTIDSLUCKAN. Ett flytande
// objekt ska just dö, och dess tomma fönster lever tills värdens synk river
// det i pumpens svans. Kontrollen POSTAS FÖRE döden och står därför före
// synken som döden postar; luckans händelser (`postaLuckhändelser`, D3D11)
// postas EFTER döden men levereras i nästa varvs händelsefas, alltså före båda
// (`App::pumpUntilClosed`: OS:ets händelser först, de postade i svansen). När
// kontrollen körs ska fönstret därför fortfarande vara REGISTRERAT — synken
// har inte kommit — och, när händelser postades, pekarens senaste position
// vara det sista klickets (`sista`) och tangenten stå som hållen: händelserna
// nådde det tomma fönstret medan det levde. `orört` är fallets egen fråga om
// att de inte rörde något mer. Ett fönster som redan rivits derefereras inte:
// då faller kontrollen med det beskedet.
void postaLuckKontroll(const std::string& fall, glaze::AppWindow* f,
                       bool händelser, gfx::Pointf sista,
                       std::function<bool()> orört)
{
    glaze::app().post([fall, f, händelser, sista, orört = std::move(orört)] {
        const bool lever = registrerat(f);
        check(lever, fall + ": kontrollen i luckan kom före synken — det "
                            "tomma fönstret lever");
        if (!lever || !händelser)
            return;
        const std::optional<gfx::Pointf> p = f->mousePosition();
        check(p && std::abs(p->x - sista.x) <= 0.5f &&
                  std::abs(p->y - sista.y) <= 0.5f &&
                  f->isKeyHeld(gfx::Key::A),
              fall + ": musen och tangenten nådde det tomma fönstret medan "
                     "det levde — före synken",
              p ? std::to_string(p->x) + ", " + std::to_string(p->y)
                : std::string("ingen pekarposition"));
        check(!orört || orört(),
              fall + ": …och händelserna rörde ingenting annat");
    });
}

// Hela bilden av ett fönster (RGBA), tom när ytan inte kan läsas —
// platshållarens pixlar (LV2, LV4, FT10).
std::vector<std::uint8_t> bildAv(glaze::AppWindow& f, int& W, int& H)
{
    f.window().renderFrame();
    std::vector<std::uint8_t> rgba;
    if (!f.window().captureFrame(rgba, W, H))
        rgba.clear();
    return rgba;
}

// En pixel ur en bild, packad som `Vard::pixel` gör det; false utanför.
bool pixelI(const std::vector<std::uint8_t>& rgba, int W, int H, float px,
            float py, std::uint32_t& ut)
{
    const int x = static_cast<int>(px), y = static_cast<int>(py);
    if (rgba.empty() || x < 0 || y < 0 || x >= W || y >= H)
        return false;
    const std::size_t i =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) +
         static_cast<std::size_t>(x)) * 4u;
    ut = static_cast<std::uint32_t>(rgba[i]) << 24 |
         static_cast<std::uint32_t>(rgba[i + 1]) << 16 |
         static_cast<std::uint32_t>(rgba[i + 2]) << 8 | rgba[i + 3];
    return true;
}

// Platshållarramens MITTLINJE, räknad från platshållarens kant (LV2, LV4,
// FT10, LV6). `Draw::frame` stryker konturen INNANFÖR sin rekt
// (`Renderer::strokeRoundedRect`), och värden ritar ramen i
// `Draw::inset(r, dropLineW / 2)` — drag-och-släppets målram, samma recept —
// så linjen ligger [dl/2, 3dl/2] in från kanten och dess mitt `dl` in. En
// läsning `dl / 2` in träffar linjens YTTRE kant: vid heltalsskala en
// hel pixel ändå, vid 125 % en pixel som linjen bara täcker till 75 %. Vid
// mitten är pixeln hel vid varje skala ≥ 1 (linjen är ≥ 2 fysiska px).
float ramMitt(float dl)
{
    return dl;
}

// BÄRANDETS HÖRN: det burna fönstret står där pekaren står
// minus greppet — `pekare` och `grepp` i källfönstrets fysiska px. Exakt
// bara där placeringsrymden är skärmens px och ingen fönsterhanterare flyttar
// fönster (D3D11, ±1 px); på macOS och X11 är det en rad i TESTS.md.
void kollaHörn(const std::string& fall, [[maybe_unused]] glaze::AppWindow& källa,
               [[maybe_unused]] glaze::AppWindow& f,
               [[maybe_unused]] gfx::Pointf pekare,
               [[maybe_unused]] gfx::Pointf grepp)
{
#ifdef GLAZE_BACKEND_D3D11
    const gfx::Pointf fp  = f.window().clientToScreen(0.0f, 0.0f);
    const gfx::Pointf exp = källa.window().clientToScreen(pekare.x - grepp.x,
                                                          pekare.y - grepp.y);
    check(std::abs(fp.x - exp.x) <= 1.0f && std::abs(fp.y - exp.y) <= 1.0f,
          fall + ": fönstret står där bärandet står — pekaren minus greppet "
                 "(±1 px)",
          std::to_string(fp.x) + "," + std::to_string(fp.y) + " mot " +
              std::to_string(exp.x) + "," + std::to_string(exp.y));
#else
    std::printf("  --   %s: hörnets ±1 px-kontroll gäller D3D11\n",
                fall.c_str());
#endif
}

struct FlytVard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Logg             logg;
    glaze::DockHost  host;
    glaze::DockPanel vänster, bevak;
    glaze::TextInput fält;    // i bevak
    PopupYta         popup;   // i bevak — hindret för FL5
    glaze::Panel     mitten;
    int              genvägar = 0;

    FlytVard(const std::string& titelV, const std::string& titelB)
        : glaze::AppWindow("dock_gate flytande", 640, 420)
    {
        host.name     = "host";
        host.dock     = glaze::Dock::Fill;
        vänster.name  = "vanster";
        vänster.setText(titelV);   // det flytande fönstrets titel (FindWindowW)
        vänster.width = 160;
        bevak.name    = "bevak";
        bevak.setText(titelB);
        bevak.dock    = glaze::Dock::Right;
        bevak.width   = 150;
        bevak.setFloating(true);   // FÖRE adopt — som demons `.glazeui`
        fält.dock  = glaze::Dock::Top;
        popup.dock = glaze::Dock::Fill;
        root().adopt(host);
        host.adopt(vänster);
        host.adopt(bevak);
        host.adopt(mitten);
        bevak.adopt(fält);
        bevak.adopt(popup);
        menuBar.show();
        menuBar.addMenu("Arkiv");
        menuBar.addItem("Arkiv", "Uppdatera", "F5");
        menuBar.setHandler({"Arkiv", "Uppdatera"}, [this] { ++genvägar; });
        vänster.onChange = [this] { logg.push_back("P:vanster"); };
        bevak.onChange   = [this] { logg.push_back("P:bevak"); };
        host.onChange    = [this] { logg.push_back("H"); };
    }

    gfx::Rectf rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    float      s() { return window().uiScale(); }
    // Rubrikens vänstra del — titeln, långt från knapparna.
    gfx::Pointf rubrik(glaze::DockPanel& p)
    {
        const gfx::Rectf r = rekt(p);
        return {r.x + 20.0f * s(), r.y + style().dockHeaderH * s() / 2.0f};
    }
    gfx::Pointf mittpunkt(glaze::Widget& w)
    {
        const gfx::Rectf r = rekt(w);
        return {r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    void mus(gfx::EventType t, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
};

void fallFlytande()
{
    std::printf("\n=== FLYTANDE (E3) ===\n");
    const std::string titelV = unikTitel("vanster");
    const std::string titelB = unikTitel("bevak");
    const std::string titelW = unikTitel("flytande");
    FlytVard          w(titelV, titelB);
    w.setTitle(titelW);       // huvudfönstrets FindWindowW-nyckel (LV3)
    w.setLocation(60, 60);    // fönstret och de flytande i arbetsytan
    w.hide();                 // DOLD START
    const bool drag = glaze::DockHost::isFloatDragSupported();

    bool                               kordes = false;
    std::vector<std::function<void()>> steg;
    std::size_t                        nr = 0;
    auto nästa = [&] {
        if (nr < steg.size())
            glaze::app().post(steg[nr++]);
    };
    auto hemma = [&w](glaze::DockPanel& p) { return p.parent() == &w.host; };
    glaze::AppWindow* flyt  = nullptr;   // bevaks fönster
    std::size_t       antal = 0;
    std::string       iOnClose;
    bool              veto  = false;     // QUIT: huvudfönstret säger nej
    w.onClose = [&](glaze::Widget*, glaze::CloseArgs& e) {
        iOnClose = w.host.layout();
        e.cancel = veto;
    };

    // ---- FL1. DOLD START: Bevakning flyter redan vid starten — efter ett
    // pumpvarv i ett eget fönster, ÄGT av huvudfönstret och DOLT som det
    // (fönstret skapas dolt). `layout()` har f= och ingen p=, och huvudytan
    // når högerkanten.
    steg.push_back([&] {
        kordes = true;
        check(w.isOpen() && w.isHidden(),
              "FL1: (riggen) huvudfönstret är öppet och dolt");
        vänta([&] { return !hemma(w.bevak); }, 2000, [&](bool) {
            flyt  = w.bevak.appWindow();
            antal = antalFönster();
            check(!hemma(w.bevak) && flyt && flyt != &w && flyt->owner() == &w,
                  "FL1: Bevakning flyter — i ett eget fönster ägt av "
                  "huvudfönstret");
            check(flyt && flyt->isOpen() && flyt->isHidden(),
                  "FL1: DOLD START — det flytande fönstret skapades dolt");
            const std::string l = w.host.layout();
            check(l.find(";f=bevak,") != std::string::npos &&
                      l.find("p=bevak") == std::string::npos,
                  "FL1: layout() har f=bevak och ingen p=bevak", l);
            const gfx::Rectf m = w.rekt(w.mitten);
            const gfx::Rectf h = w.rekt(w.host);
            check(nära(m.x + m.w, h.x + h.w),
                  "FL1: huvudytan når högerkanten — ingen remsa för Bevakning",
                  rekt(m));
            nästa();
        });
    });

    // ---- FL2. TILLSTÅNDET FÖRST: `setFloating(false)` gör panelen dockad i
    // `layout()` direkt, medan trädet fortfarande står i det flytande
    // fönstret; i svansen flyttar den hem och fönstret rivs.
    steg.push_back([&] {
        w.bevak.setFloating(false);
        const std::string l = w.host.layout();
        check(!w.bevak.isFloating() && !hemma(w.bevak) &&
                  l.find("p=bevak") != std::string::npos &&
                  l.find("f=bevak") == std::string::npos,
              "FL2: TILLSTÅNDET FÖRST — dockad i layout() direkt, flytten i "
              "svansen",
              l);
        vänta([&] { return hemma(w.bevak); }, 2000, [&](bool) {
            check(hemma(w.bevak) && antalFönster() == antal - 1 &&
                      w.rekt(w.bevak).w > 0.0f,
                  "FL2: hemma — panelen står i högerremsan och fönstret är "
                  "rivet",
                  std::to_string(antalFönster()));
            nästa();
        });
    });

    // ---- FL3. DUBBELKLICK PÅ RUBRIKEN gör en dockad panel flytande —
    // en användarändring: panelens onChange före värdens.
    steg.push_back([&] {
        w.logg.clear();
        const gfx::Pointf p = w.rubrik(w.bevak);
        w.ner(p);
        w.upp(p);
        w.ner(p);
        w.upp(p);
        check(w.bevak.isFloating() && fog(w.logg) == "[P:bevak H]",
              "FL3: dubbelklick på rubriken — flytande, panelens onChange "
              "före värdens",
              fog(w.logg));
        vänta([&] { return !hemma(w.bevak); }, 2000, [&](bool) {
            flyt = w.bevak.appWindow();
            check(!hemma(w.bevak) && flyt && flyt->owner() == &w,
                  "FL3: …och i svansen i ett eget fönster");
            nästa();
        });
    });

    // ---- FL4. EN STÄNGNINGSBEGÄRAN till det flytande fönstret (Alt+F4)
    // stänger PANELEN, som dess stängknapp: dold, onChange, och fönstret
    // LEVER (vetot). En panel som inte får stängas står kvar.
    steg.push_back([&] {
        w.logg.clear();
        flyt->close();
        vänta([&] { return !w.bevak.isVisible(); }, 2000, [&](bool) {
            check(!w.bevak.isVisible() && w.bevak.isFloating() &&
                      flyt->isOpen() && fog(w.logg) == "[P:bevak H]",
                  "FL4: fönstrets stängning stänger PANELEN — fönstret lever, "
                  "onChange som stängknappen",
                  fog(w.logg));
            check(postFält(w.host.layout(), "f=bevak,", 4) == "1",
                  "FL4: …och f= skriver den stängd", w.host.layout());
            w.bevak.show();
            w.bevak.setClosable(false);
            flyt->close();
            vänta([] { return false; }, 150, [&](bool) {
                check(w.bevak.isVisible() && flyt->isOpen(),
                      "FL4: en panel som inte får stängas står kvar — och "
                      "fönstret med den");
                w.bevak.setClosable(true);
                nästa();
            });
        });
    });

#ifdef GLAZE_BACKEND_D3D11
    // ---- FL6. RESERVVÄGEN: F5 POSTAD till det flytande fönstret —
    // ingen widget och ingen genväg där tar den — fyrar ÄGARENS menygenväg.
    steg.push_back([&] {
        const HWND h = hwndFör(titelB);
        check(h != nullptr,
              "FL6: (fixturen) det flytande fönstret hittas med sin titel");
        if (!h) {
            nästa();
            return;
        }
        postaTangent(h, VK_F5);
        vänta([&] { return w.genvägar == 1; }, 2000, [&](bool) {
            check(w.genvägar == 1,
                  "FL6: F5 i det flytande fönstret fyrar ägarens menygenväg "
                  "(acceleratorFallback)",
                  std::to_string(w.genvägar));
            nästa();
        });
    });
#endif

    // Bevakning hem — den är LV-fallens mål.
    steg.push_back([&] {
        w.bevak.setFloating(false);
        vänta([&] { return hemma(w.bevak); }, 2000, [&](bool) { nästa(); });
    });

    // ---- LV1. LEVANDE DRAG, FALL A:
    // Vänsters rubrik dras över tröskeln — panelen LOSSNAR direkt (flytande,
    // värden håller capture, f= och ingen p=, huvudytan växer); efter svansen
    // står den i ett eget fönster vars hörn är pekaren minus greppet (D3D11
    // ±1), en rörelse till flyttar fönstret exakt lika långt, och släppt mitt
    // i huvudytan (ingen platshållare) flyter den kvar: floatingBounds() är
    // fönstrets plats, ETT onChange, capturen släppt. Utan skärmpositioner
    // (LV5) står panelen kvar.
    steg.push_back([&] {
        if (!drag) {
            nästa();   // LV5 mäter vägen utan bärande
            return;
        }
        w.logg.clear();
        const gfx::Pointf p     = w.rubrik(w.vänster);
        const gfx::Rectf  pr    = w.rekt(w.vänster);
        const float       mitt0 = w.rekt(w.mitten).w;
        const gfx::Pointf grepp = {p.x - pr.x, p.y - pr.y};
        const gfx::Pointf p1    = {p.x + 30.0f * w.s(), p.y};
        w.ner(p);
        w.flytta(p1);
        const std::string l = w.host.layout();
        check(w.vänster.isFloating() && w.host.hasPointerCapture() &&
                  l.find("f=vanster,") != std::string::npos &&
                  l.find("p=vanster") == std::string::npos &&
                  w.rekt(w.mitten).w > mitt0 + 1.0f && w.logg.empty(),
              "LV1: över tröskeln LOSSNAR panelen direkt — flytande, värden "
              "håller trycket, f= och ingen p=, huvudytan växer, inget "
              "onChange än",
              l + " " + fog(w.logg));
        vänta([&] { return !hemma(w.vänster); }, 2000,
              [&, p1, grepp](bool) {
            glaze::AppWindow* fv = w.vänster.appWindow();
            check(!hemma(w.vänster) && fv && fv != &w && fv->owner() == &w &&
                      w.host.hasPointerCapture(),
                  "LV1: efter svansen i ett eget fönster ägt av huvudfönstret — "
                  "och draget lever");
            if (!fv || fv == &w) {
                w.upp(p1);
                nästa();
                return;
            }
            // Öppnat där bärandet står, och en rörelse till flyttar fönstret
            // exakt lika långt.
            kollaHörn("LV1 (öppnat)", w, *fv, p1, grepp);
            const gfx::Pointf p2 = w.mittpunkt(w.mitten);
            w.flytta(p2);
            kollaHörn("LV1 (flyttat)", w, *fv, p2, grepp);
            w.upp(p2);
            // FÖNSTRETS PLATS är OS-fönstrets (`placement()`), i logiska px
            // med primärskärmens skala — inte `geometry()`: riggen startar
            // DOLD, så det flytande fönstret har aldrig visats, och
            // före den första visningen svarar `geometry()` med BEGÄRAN
            // — utan position, fast bärandet ställt OS-fönstret.
            const gfx::WindowPlacement g  = fv->window().placement();
            const gfx::Rectf           fb = w.vänster.floatingBounds();
            float                      ps = 1.0f;
            for (const gfx::ScreenInfo& sk : gfx::screens())
                if (sk.primary)
                    ps = sk.scale;
            check(w.vänster.isFloating() && !w.host.hasPointerCapture() &&
                      fog(w.logg) == "[P:vanster H]",
                  "LV1: släppt utan platshållare — flytande där den står, "
                  "capturen släppt, ETT onChange (panelens, sedan värdens)",
                  fog(w.logg));
            check(g.hasPosition &&
                      std::abs(fb.x - static_cast<float>(g.x) / ps) <= 1.0f &&
                      std::abs(fb.y - static_cast<float>(g.y) / ps) <= 1.0f,
                  "LV1: floatingBounds() är fönstrets plats (±1 logisk px)",
                  rekt(fb) + " mot " + std::to_string(g.x) + "," +
                      std::to_string(g.y) + " / " + std::to_string(ps));
            nästa();
        });
    });

    // ---- LV4. ETT AVBRUTET DRAG: Vänster dockas, dras
    // över Bevakningens mitt (platshållaren syns), och fönstret tappar fokus
    // — capturen tas, platshållaren är borta i nästa bild, panelen flyter där
    // den står, och onChange POSTAS: inget i samma ögonblick, exakt ett efter
    // ett pumpvarv.
    steg.push_back([&] {
        if (!drag) {
            nästa();
            return;
        }
        w.vänster.setFloating(false);
        vänta([&] { return hemma(w.vänster); }, 2000, [&](bool) {
            w.logg.clear();
            const gfx::Pointf p    = w.rubrik(w.vänster);
            const gfx::Pointf över = w.mittpunkt(w.bevak);
            w.ner(p);
            w.flytta({p.x + 30.0f * w.s(), p.y});
            w.flytta(över);
            const gfx::Color acc = w.host.behaviorCtx().theme->colors.accent;
            const gfx::Rectf br  = w.rekt(w.bevak);
            const float      dl  = w.style().dropLineW * w.s();
            // Platshållarens övre kant står i Bevakningens överkant.
            auto ram = [&w, acc, br, dl](bool& läst) {
                int        W = 0, H = 0;
                const auto b = bildAv(w, W, H);
                läst         = !b.empty();
                std::uint32_t px = 0;
                return pixelI(b, W, H, br.x + br.w / 2.0f, br.y + ramMitt(dl),
                              px) &&
                       somFärg(px, acc);
            };
            bool       läst1 = false, läst2 = false;
            const bool syns  = ram(läst1);
            gfx::Event e;
            e.type     = gfx::EventType::WindowFocusLost;
            e.windowId = w.window().id();
            w.handleEvent(e);
            check(!w.host.hasPointerCapture() && w.logg.empty() &&
                      w.vänster.isFloating(),
                  "LV4: fokusförlusten avbryter — capturen borta, panelen "
                  "flytande, inget onChange i samma ögonblick",
                  fog(w.logg));
            const bool kvar = ram(läst2);
            if (läst1 && läst2) {
                check(syns && !kvar,
                      "LV4: platshållaren syntes under draget och är borta i "
                      "nästa bild");
            } else {
                std::printf("  --   LV4: pixlarna kunde inte läsas "
                            "(captureFrame) — platshållarens kontroll hoppas "
                            "över\n");
                ++g_skips;
            }
            vänta([] { return false; }, 100, [&](bool) {
                check(fog(w.logg) == "[P:vanster H]",
                      "LV4: …onChange POSTADES — exakt ett", fog(w.logg));
                vänta([&] { return !hemma(w.vänster); }, 2000,
                      [&](bool) { nästa(); });
            });
        });
    });

    // ---- LV2. PLATSHÅLLAREN ÄR SLUTGEOMETRIN: referensen först —
    // Vänster som flik hos Bevakning ger ytan R. Därefter dockas Vänster tillbaka
    // och dras (fall A) in över Bevakningens mitt: platshållarens accentram
    // ligger på R:s kanter och inte utanför dem, och släppet ger fliken i
    // Bevakningens cell — fönstret rivs i svansen, ETT onChange.
    steg.push_back([&] {
        if (!drag) {
            nästa();
            return;
        }
        w.vänster.setFloating(false);
        vänta([&] { return hemma(w.vänster); }, 2000, [&](bool) {
            const std::string före = w.host.layout();
            w.host.dockPanel(w.vänster, w.bevak, glaze::Dock::Fill);
            const gfx::Rectf R = w.rekt(w.vänster);
            w.host.setLayout(före);
            w.logg.clear();
            const gfx::Pointf p    = w.rubrik(w.vänster);
            const gfx::Pointf över = w.mittpunkt(w.bevak);
            w.ner(p);
            w.flytta({p.x + 30.0f * w.s(), p.y});
            w.flytta(över);
            const gfx::Color acc = w.host.behaviorCtx().theme->colors.accent;
            const float      dl  = w.style().dropLineW * w.s();
            int              W = 0, H = 0;
            const auto       b   = bildAv(w, W, H);
            auto acc2 = [&b, W, H, acc](float x, float y) {
                std::uint32_t px = 0;
                return pixelI(b, W, H, x, y, px) && somFärg(px, acc);
            };
            const float mx = R.x + R.w / 2.0f;
            const float my = R.y + R.h / 2.0f;
            if (!b.empty()) {
                check(acc2(R.x + ramMitt(dl), my) &&
                          acc2(R.x + R.w - ramMitt(dl), my) &&
                          acc2(mx, R.y + ramMitt(dl)) &&
                          acc2(mx, R.y + R.h - ramMitt(dl)),
                      "LV2: platshållarens ram ligger på slutgeometrins fyra "
                      "kanter",
                      rekt(R));
                check(!acc2(mx, R.y + R.h + 2.0f * dl),
                      "LV2: …och inte i flikraden under den — platshållaren "
                      "är simulerad, inte cellens yta",
                      rekt(R));
            } else {
                std::printf("  --   LV2: pixlarna kunde inte läsas "
                            "(captureFrame) — platshållarens kontroll hoppas "
                            "över\n");
                ++g_skips;
            }
            w.upp(över);
            check(!w.vänster.isFloating() &&
                      sammaCell(w.host.layout(), "bevak", "vanster") &&
                      fog(w.logg) == "[P:vanster H]",
                  "LV2: släppt på platshållaren — en flik i Bevakningens cell, "
                  "ETT onChange",
                  w.host.layout() + " " + fog(w.logg));
            vänta([&] { return hemma(w.vänster); }, 2000, [&, R](bool) {
                check(hemma(w.vänster) && lika(w.rekt(w.vänster), R),
                      "LV2: …hemma i svansen, exakt där platshållaren stod",
                      rekt(w.rekt(w.vänster)));
                nästa();
            });
        });
    });

    // ---- LV9. FLIKDRAGET LOSSAR FLIKEN: Vänster är en flik hos
    // Bevakning (LV2) — dess FLIK dras över tröskeln, och greppet står i
    // rubrikens höjd: fönstrets hörn är pekaren minus (fliken mot panelens
    // vänsterkant, halva rubrikhöjden) (D3D11 ±1). Släppt mitt i huvudytan
    // flyter den.
    steg.push_back([&] {
        if (!drag) {
            nästa();
            return;
        }
        w.logg.clear();
        const float      s     = w.s();
        const gfx::Rectf pr    = w.rekt(w.vänster);   // den visade fliken
        const float      TH    = w.style().tabHeaderH;
        const float      G     = w.style().tabGap;
        const float      flikB = (pr.w / s - G) / 2.0f;
        // Bevakning först, Vänster sist — dess flik är den högra.
        const gfx::Pointf flik = {pr.x + (flikB + G + flikB / 2.0f) * s,
                                  pr.y + pr.h + TH * s / 2.0f};
        const gfx::Pointf p1   = {flik.x, flik.y - 30.0f * s};
        w.ner(flik);
        w.flytta(p1);
        check(w.vänster.isFloating() && w.host.hasPointerCapture(),
              "LV9: fliken dras över tröskeln — panelen lossnar, värden "
              "håller trycket");
        const gfx::Pointf grepp = {flik.x - pr.x,
                                   w.style().dockHeaderH * s / 2.0f};
        vänta([&] { return !hemma(w.vänster); }, 2000,
              [&, p1, grepp](bool) {
            glaze::AppWindow* fv = w.vänster.appWindow();
            check(fv && fv != &w, "LV9: (förutsättning) i ett eget fönster");
            // Greppet i RUBRIKENS höjd: (fliken längs, halva rubriken).
            if (fv && fv != &w)
                kollaHörn("LV9", w, *fv, p1, grepp);
            const gfx::Pointf p2 = w.mittpunkt(w.mitten);
            w.flytta(p2);
            w.upp(p2);
            check(w.vänster.isFloating() && fog(w.logg) == "[P:vanster H]",
                  "LV9: släppt mitt i huvudytan — flytande, ETT onChange",
                  fog(w.logg));
            nästa();
        });
    });

    // ---- LV12. SLÄPPT FÖRE SVANSEN:
    // Vänster dockas, och ett fall A-drag släpps i SAMMA varv som det lossade
    // — fönstret finns ännu inte. Släppet står långt till vänster om
    // huvudfönstret, så att fönstret hamnar delvis utanför skärmen: det öppnas
    // i svansen där bärandet stod (pekaren minus greppet, D3D11 ±1) och inte
    // där den sparade logiska platsen hade klämts in (`setLocation`). Panelen
    // flyter därefter kvar — LV3 drar dess fönster.
    steg.push_back([&] {
        if (!drag) {
            nästa();
            return;
        }
        w.vänster.setFloating(false);
        vänta([&] { return hemma(w.vänster); }, 2000, [&](bool) {
            w.logg.clear();
            const gfx::Pointf p     = w.rubrik(w.vänster);
            const gfx::Rectf  pr    = w.rekt(w.vänster);
            const gfx::Pointf grepp = {p.x - pr.x, p.y - pr.y};
            const gfx::Pointf ute   = {-300.0f * w.s(), p.y};
            w.ner(p);
            w.flytta({p.x + 30.0f * w.s(), p.y});
            w.flytta(ute);
            w.upp(ute);
            check(w.vänster.isFloating() && hemma(w.vänster) &&
                      !w.host.hasPointerCapture() &&
                      fog(w.logg) == "[P:vanster H]",
                  "LV12: släppt före svansen — flytande, fönstret ännu inte "
                  "öppnat, capturen släppt, ETT onChange",
                  fog(w.logg));
            vänta([&] { return !hemma(w.vänster); }, 2000,
                  [&, ute, grepp](bool) {
                glaze::AppWindow* fv = w.vänster.appWindow();
                check(fv && fv != &w, "LV12: …fönstret öppnat i svansen");
                if (fv && fv != &w)
                    kollaHörn("LV12", w, *fv, ute, grepp);
                nästa();
            });
        });
    });

#ifdef GLAZE_BACKEND_D3D11
    // ---- LV3. FALL B: Vänster flyter (LV12), och dess
    // fönsters RUBRIK dras med postade Win32-meddelanden. Först släppt mitt i
    // huvudytan, där ingen platshållare står: fönstret har följt pekaren
    // (hörnet = släppet minus greppet, ±1) och panelen FLYTER kvar — ingen
    // reserv hem. Därefter släppt i värdens högra kantzon:
    // dockad vid högerkanten, och fönstret rivs. ETT onChange per drag. Det
    // andra trycket tar 40 logiska px längre in i rubriken:
    // samma punkt inom `doubleClickMs()` är ett DUBBELKLICK,
    // som dockar panelen och river fönstret under fortsättningarna — som
    // därför också frågar registret innan de rör det.
    steg.push_back([&] {
        glaze::AppWindow* fv = w.vänster.appWindow();
        const HWND        hv = hwndFör(titelV);
        const HWND        hw = hwndFör(titelW);
        check(hv != nullptr && hw != nullptr && fv && fv != &w,
              "LV3: (fixturen) huvudfönstret och Vänsters fönster hittas");
        if (!hv || !hw || !fv || fv == &w) {
            nästa();
            return;
        }
        w.logg.clear();
        const float       fs    = fv->window().uiScale();
        const gfx::Pointf grepp = {20.0f * fs, fv->style().dockHeaderH * fs / 2.0f};
        const gfx::Pointf grepp2 = {grepp.x + 40.0f * fs, grepp.y};
        check(40.0f > fv->style().dragThreshold,
              "LV3: (fixturen) det andra trycket ligger utanför "
              "dubbelklickets avstånd (stilens dragThreshold)");
        PostDrag d;
        d.källaH  = hv;
        d.källa   = fv;
        d.värdH   = hw;
        d.värd    = &w;
        d.ner     = fv->window().clientToScreen(grepp.x, grepp.y);
        d.tröskel = 30.0f * fs;
        const gfx::Pointf m = w.mittpunkt(w.mitten);
        d.mål               = w.window().clientToScreen(m.x, m.y);
        postatDrag(d, nullptr, nullptr, [&, fv, grepp, grepp2, d] {
            const bool kvar = registrerat(fv) && w.vänster.appWindow() == fv;
            check(kvar, "LV3: (förutsättning) Vänsters fönster står kvar "
                        "efter det första draget",
                  fog(w.logg));
            if (!kvar) {
                nästa();
                return;
            }
            const gfx::Pointf fp = fv->window().clientToScreen(0.0f, 0.0f);
            check(w.vänster.isFloating() && !hemma(w.vänster) &&
                      std::abs(fp.x - (d.mål.x - grepp.x)) <= 1.0f &&
                      std::abs(fp.y - (d.mål.y - grepp.y)) <= 1.0f &&
                      fog(w.logg) == "[P:vanster H]",
                  "LV3: rubriken dragen och släppt mitt i huvudytan — fönstret "
                  "följde pekaren, panelen FLYTER kvar (ingen reserv hem), "
                  "ETT onChange",
                  std::to_string(fp.x) + "," + std::to_string(fp.y) + " " +
                      fog(w.logg));
            w.logg.clear();
            const gfx::Rectf hr = w.rekt(w.host);
            const gfx::Pointf kant{
                hr.x + hr.w - w.style().dockDropZone * w.s() / 2.0f,
                hr.y + hr.h / 2.0f};
            PostDrag d2 = d;
            d2.ner      = fv->window().clientToScreen(grepp2.x, grepp2.y);
            d2.mål      = w.window().clientToScreen(kant.x, kant.y);
            postatDrag(d2, nullptr, nullptr, [&] {
                check(!w.vänster.isFloating() &&
                          w.vänster.dock == glaze::Dock::Right &&
                          fog(w.logg) == "[P:vanster H]",
                      "LV3: släppt i värdens högra kantzon — dockad vid "
                      "högerkanten, ETT onChange",
                      w.host.layout() + " " + fog(w.logg));
                vänta([&] { return hemma(w.vänster); }, 2000, [&](bool) {
                    check(hemma(w.vänster),
                          "LV3: …hemma i svansen, fönstret rivet");
                    nästa();
                });
            });
        });
    });

    // ---- LV11. FALL B:S AVBROTT MED DOLDA FÖNSTER:
    // Vänster flyter, och dess fönsters rubrik trycks och dras över
    // tröskeln med postade meddelanden — panelen håller trycket i sitt eget
    // fönster (explicit capture), värden inte. Därefter tar OS:et capturen
    // (WM_CAPTURECHANGED med lParam NULL — capture_gate D1). Ingenting ritas
    // (fönstren är dolda), så bara avbrottets krok kan avsluta draget: ETT
    // onChange postat, panelen flytande kvar och capturen släppt.
    steg.push_back([&] {
        w.vänster.setFloating(true);
        vänta([&] { return !hemma(w.vänster); }, 2000, [&](bool) {
            glaze::AppWindow* fv = w.vänster.appWindow();
            const HWND        hv = hwndFör(titelV);
            check(fv && fv != &w && hv != nullptr && w.isHidden(),
                  "LV11: (fixturen) Vänster flyter i ett dolt fönster som "
                  "hittas");
            if (!fv || fv == &w || !hv) {
                nästa();
                return;
            }
            const float       fs = fv->window().uiScale();
            const gfx::Pointf g  = fv->window().clientToScreen(
                20.0f * fs, fv->style().dockHeaderH * fs / 2.0f);
            postaVid(hv, *fv, WM_LBUTTONDOWN, g, MK_LBUTTON);
            vänta([] { return true; }, 1000, [&, fv, hv, g, fs](bool) {
                if (!registrerat(fv)) {
                    check(false, "LV11: Vänsters fönster står kvar");
                    nästa();
                    return;
                }
                postaVid(hv, *fv, WM_MOUSEMOVE,
                         gfx::Pointf{g.x + 30.0f * fs, g.y}, MK_LBUTTON);
                vänta([] { return true; }, 1000, [&, hv](bool) {
                    w.logg.clear();
                    check(w.vänster.hasPointerCapture() &&
                              !w.host.hasPointerCapture() &&
                              w.vänster.isFloating(),
                          "LV11: fall B — panelen håller trycket i sitt eget "
                          "fönster, värden inte");
                    SendMessageW(hv, WM_CAPTURECHANGED, 0, 0);
                    vänta([&] { return !w.logg.empty(); }, 500,
                          [&, hv](bool) {
                        check(fog(w.logg) == "[P:vanster H]" &&
                                  w.vänster.isFloating() &&
                                  !w.vänster.hasPointerCapture() &&
                                  w.isHidden(),
                              "LV11: avbrottet i panelens fönster når värden "
                              "utan en ritning — ETT onChange, flytande kvar, "
                              "capturen släppt",
                              fog(w.logg));
                        // Städningen: knappen upp, OS:ets capture släpps.
                        SendMessageW(hv, WM_LBUTTONUP, 0, 0);
                        w.vänster.setFloating(false);
                        vänta([&] { return hemma(w.vänster); }, 2000,
                              [&](bool) { nästa(); });
                    });
                });
            });
        });
    });
#endif

    // ---- LV5. UTAN SKÄRMPOSITIONER (Wayland, headless): ett drag
    // ut ur fönstret gör ingenting, och ett drag in över Bevakning visar
    // platshållaren medan panelen STÅR KVAR — den flyter inte, och släppet
    // där dockar den. Det andra draget tar 40 logiska px längre in i rubriken,
    // av LV3:s skäl: samma punkt inom `doubleClickMs()` är ett DUBBELKLICK
    // (`AppWindow::countClick` — ett drag emellan nollställer inte följden,
    // och utan skärmrymd mäts avståndet i klientens), och dubbelklicket på
    // rubriken gör panelen flytande. Det mätte dubbelklicket, inte
    // draget.
    steg.push_back([&] {
        if (drag) {
            std::printf("  --   LV5: fönster har skärmposition här — den gamla "
                        "vägen mäts där de saknas (Wayland, headless)\n");
            nästa();
            return;
        }
        w.logg.clear();
        const gfx::Pointf p      = w.rubrik(w.vänster);
        const gfx::Rectf  klient = w.window().contentArea();
        w.ner(p);
        w.flytta({p.x + 30.0f * w.s(), p.y});
        w.flytta({klient.x + klient.w + 40.0f, p.y});
        check(!w.vänster.isFloating() && hemma(w.vänster),
              "LV5: utan skärmpositioner lossnar panelen inte");
        w.upp({klient.x + klient.w + 40.0f, p.y});
        check(!w.vänster.isFloating() && w.logg.empty(),
              "LV5: …och ett släpp utanför fönstret gör ingenting",
              fog(w.logg));
        check(40.0f > w.style().dragThreshold,
              "LV5: (fixturen) det andra trycket ligger utanför "
              "dubbelklickets avstånd (stilens dragThreshold)");
        const gfx::Pointf p2 = {p.x + 40.0f * w.s(), p.y};
        w.ner(p2);
        w.flytta({p2.x + 30.0f * w.s(), p2.y});
        w.flytta(w.mittpunkt(w.bevak));
        check(!w.vänster.isFloating() && hemma(w.vänster),
              "LV5: draget in över Bevakning — panelen står kvar");
        w.upp(w.mittpunkt(w.bevak));
        check(sammaCell(w.host.layout(), "bevak", "vanster") &&
                  fog(w.logg) == "[P:vanster H]",
              "LV5: …och släppet på platshållaren dockar den som flik",
              w.host.layout() + " " + fog(w.logg));
        nästa();
    });

    // ---- OV1. ÄGARENS SYNLIGHET: Bevakning flyter igen, och när
    // huvudfönstret visas följer det flytande fönstret med.
    steg.push_back([&] {
        w.bevak.setFloating(true);
        vänta([&] { return !hemma(w.bevak); }, 2000, [&](bool) {
            flyt = w.bevak.appWindow();
            check(flyt && flyt->isHidden(),
                  "OV1: (kontroll) med ett dolt huvudfönster är det dolt");
            w.show();
            check(!w.isHidden() && flyt && !flyt->isHidden(),
                  "OV1: huvudfönstret visas — det flytande fönstret följer");
            nästa();
        });
    });

#ifdef GLAZE_BACKEND_D3D11
    // ---- LV10. AKTIVERINGEN: Bevakningens fönster är ingen NOACTIVATE —
    // det går att aktivera, bara dess FÖRSTA visning stjäl inte fokus — och
    // en panel som lossnar lämnar förgrunden där den var. FÖRBEREDELSEN står
    // här, direkt före visningen som prövas:
    // huvudfönstret görs till förgrunden och det kontrolleras — en
    // föregående visning som stal den (OV1 med en saknad `showInactive`) hade
    // annars fått fallet att hoppa över sig självt. Bara en NEKAD förberedelse
    // (ingen förgrund i sessionen) hoppar över. (Fältets del — ett fönster som
    // aldrig aktiveras — står i LV7.)
    steg.push_back([&] {
        const HWND     hb = hwndFör(titelB);
        const HWND     hw = hwndFör(titelW);
        const LONG_PTR ex = hb ? GetWindowLongPtrW(hb, GWL_EXSTYLE) : 0;
        check(hb != nullptr && (ex & WS_EX_NOACTIVATE) == 0,
              "LV10: panelens fönster är inte NOACTIVATE");
        if (w.isHidden() || !görTillFörgrund(hw)) {
            std::printf("  --   LV10: huvudfönstret kunde inte göras till "
                        "förgrunden (ingen förgrund i den här sessionen) — "
                        "aktiveringens kontroll hoppas över\n");
            ++g_skips;
            nästa();
            return;
        }
        w.vänster.setFloating(true);
        vänta([&] { return !hemma(w.vänster); }, 2000, [&, hw](bool) {
            const HWND hv = hwndFör(titelV);
            check(GetForegroundWindow() == hw && hv != nullptr &&
                      IsWindowVisible(hv),
                  "LV10: en panel som lossnar stjäl inte förgrunden — "
                  "huvudfönstret är kvar, det flytande syns");
            w.vänster.setFloating(false);
            vänta([&] { return hemma(w.vänster); }, 2000,
                  [&](bool) { nästa(); });
        });
    });
#endif

    // ---- FL5. EN NEKAD FLYTT VÄNTAR: med en öppen popup i panelen
    // dockas den — tillståndet säger dockad direkt och FÖRBLIR sant, men
    // flytten görs inte; när popupen stängts och panelen ritats om görs den.
    steg.push_back([&] {
        w.popup.öppen = true;
        w.bevak.setFloating(false);
        vänta([] { return false; }, 150, [&](bool) {
            const std::string l = w.host.layout();
            check(!hemma(w.bevak) && !w.bevak.isFloating() &&
                      l.find("p=bevak") != std::string::npos &&
                      l.find("f=bevak") == std::string::npos,
                  "FL5: en öppen popup i panelen NEKAR flytten — den väntar, "
                  "och layout() säger redan dockad",
                  l);
            w.popup.öppen = false;
            w.popup.invalidate();   // popupens ägare ritas om när den stängs
            vänta([&] { return hemma(w.bevak); }, 2000, [&](bool) {
                check(hemma(w.bevak),
                      "FL5: hindret borta — nästa ritning gjorde flytten hem");
                nästa();
            });
        });
    });

    // ---- OV2. …och när huvudfönstret döljs döljs det flytande också.
    steg.push_back([&] {
        w.bevak.setFloating(true);
        vänta([&] { return !hemma(w.bevak); }, 2000, [&](bool) {
            flyt = w.bevak.appWindow();
            check(flyt && !flyt->isHidden(),
                  "OV2: (kontroll) med ett synligt huvudfönster syns det");
            w.hide();
            check(flyt && flyt->isHidden(),
                  "OV2: huvudfönstret döljs — det flytande fönstret följer");
            nästa();
        });
    });

#ifdef GLAZE_BACKEND_D3D11
    // ---- QUIT. ETT RIKTIGT APPAVSLUT med veto i huvudfönstret:
    // `PostQuitMessage` ger `EventType::Quit`,
    // App köar bara ägarkedjornas rötter, huvudfönstret vetar — och det
    // flytande fönstret får ingen egen stängningsrunda efteråt, så panelen
    // syns och flyter fortfarande.
    steg.push_back([&] {
        veto = true;
        PostQuitMessage(0);
        vänta([] { return false; }, 200, [&](bool) {
            check(w.isOpen() && flyt && flyt->isOpen() &&
                      w.bevak.isFloating() && w.bevak.isVisible(),
                  "QUIT: appavslutet med veto i huvudfönstret — panelen syns "
                  "och flyter, det flytande fönstret står kvar");
            veto = false;
            nästa();
        });
    });
#endif

    // ---- FL9. HUVUDFÖNSTRETS close() STÄNGER det: det flytande
    // fönstrets onClose lägger inget veto i en kaskad, och `layout()` i
    // huvudfönstrets onClose säger fortfarande "flytande, öppen". Inget postas
    // efter stängningen — en kvarliggande post hade kört i nästa riggs pump
    // med den här riggen riven. (Ett veto i det flytande fönstret hade hållit
    // pumpen kvar; ctest:s tidsgräns fäller det.)
    steg.push_back([&] { w.close(); });

    nästa();
    w.run();

    if (!kordes) {
        std::printf("  --   de flytande fallen hoppades över: inget fönster "
                    "kunde skapas (ingen display)\n");
        ++g_skips;
        return;
    }
    check(!w.isOpen() && postFält(iOnClose, "f=bevak,", 4) == "0",
          "FL9: huvudfönstret STÄNGS — layout() i dess onClose säger "
          "flytande, öppen",
          iOnClose);
    check(w.bevak.isFloating() && w.bevak.isVisible(),
          "FL9: kaskaden rörde inte panelens tillstånd");
    check(flyt && !flyt->isOpen(),
          "FL9: det flytande fönstret stängdes med ägaren");
}

#ifdef GLAZE_BACKEND_D3D11
// ---- TOM. EN FLYTANDE PANEL I EN TOM VÄRD:
// panelen minns vänsterkanten, och dess fönsters rubrik DRAS (postat, fall B
// — fönstret följer pekaren, `postatDrag`) in i högra kantzonen — huvudytan
// täcker hela den tomma värden, men självträffsregeln gäller bara en panel
// som står i huvudytan, så kantzonen vinner och panelen dockas vid
// HÖGERKANTEN.
void fallFlytTomVard()
{
    const std::string titel  = unikTitel("tom");
    const std::string titelW = unikTitel("tomvard");
    struct Rigg : glaze::AppWindow {
        glaze::DockHost  host;
        glaze::DockPanel panel;
        explicit Rigg(const std::string& t)
            : glaze::AppWindow("dock_gate tom", 480, 320)
        {
            host.name = "host";
            host.dock = glaze::Dock::Fill;
            panel.name = "p";
            panel.setText(t);
            panel.dock = glaze::Dock::Left;   // minnet: vänster
            panel.setFloating(true);
            root().adopt(host);
            host.adopt(panel);
        }
    };
    Rigg w(titel);
    w.setTitle(titelW);
    w.setLocation(60, 60);
    w.hide();
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        vänta([&] { return w.panel.parent() != &w.host; }, 2000, [&](bool) {
            glaze::AppWindow* fp = w.panel.appWindow();
            const HWND        h  = hwndFör(titel);
            const HWND        hw = hwndFör(titelW);
            check(fp && fp != &w && h != nullptr && hw != nullptr,
                  "TOM: (fixturen) panelen flyter och fönstren hittas");
            if (!fp || fp == &w || !h || !hw) {
                w.close();
                return;
            }
            const float       s  = w.window().uiScale();
            const gfx::Rectf  hr = w.host.resolve(w.host.behaviorCtx());
            const gfx::Pointf mål{hr.x + hr.w - w.style().dockDropZone * s / 2.0f,
                                  hr.y + hr.h / 2.0f};
            const float       fs = fp->window().uiScale();
            PostDrag d;
            d.källaH  = h;
            d.källa   = fp;
            d.värdH   = hw;
            d.värd    = &w;
            d.ner     = fp->window().clientToScreen(
                20.0f * fs, fp->style().dockHeaderH * fs / 2.0f);
            d.mål     = w.window().clientToScreen(mål.x, mål.y);
            d.tröskel = 30.0f * fs;
            postatDrag(d, nullptr, nullptr, [&] {
                vänta([&] { return w.panel.parent() == &w.host; }, 2000,
                      [&](bool) {
                    check(w.panel.parent() == &w.host &&
                              !w.panel.isFloating() &&
                              w.panel.dock == glaze::Dock::Right &&
                              w.host.layout().find("s=right") !=
                                  std::string::npos,
                          "TOM: släppt i högra kantzonen i en tom värd — "
                          "dockad vid HÖGERKANTEN, inte hem till vänster",
                          w.host.layout());
                    w.close();
                });
            });
        });
    });
    w.run();
    if (!kordes) {
        std::printf("  --   TOM hoppades över (ingen display)\n");
        ++g_skips;
    }
}
#endif

// ---- OFF. EN OFFSCREEN-VÄRD (glaze-render) skapar inget fönster: panelen
// lämnar bara layouten.
void fallFlytOffscreen()
{
    struct Rigg : glaze::AppWindow {
        glaze::DockHost  host;
        glaze::DockPanel panel{"P"};
        Rigg() : glaze::AppWindow("dock_gate offscreen", 400, 300)
        {
            host.name  = "host";
            host.dock  = glaze::Dock::Fill;
            panel.name = "p";
            panel.setFloating(true);
            root().adopt(host);
            host.adopt(panel);
        }
    };
    Rigg w;
    w.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes             = true;
        const std::size_t n = antalFönster();
        vänta([] { return false; }, 150, [&, n](bool) {
            check(w.panel.parent() == &w.host && antalFönster() == n &&
                      w.panel.isFloating() &&
                      w.host.layout().find(";f=p,") != std::string::npos,
                  "OFF: offscreen-värd — inget fönster skapas, panelen lämnar "
                  "bara layouten",
                  w.host.layout());
            w.close();
        });
    });
    w.run();
    if (!kordes) {
        std::printf("  --   OFF hoppades över (ingen display)\n");
        ++g_skips;
    }
}

// ---- T. TRE FÖNSTERNIVÅER: toppen äger mellanfönstret, som har
// värden — och det flytande fönstret ägs av mellanfönstret. Ett veto i
// TOPPEN stoppar hela kaskaden, och det flytande fönstrets onClose varken
// lägger ett eget veto eller rör panelen (`confirmClose` sätter inte
// flaggan på mellanägaren — hela ägarkedjan frågas). Utan veto stängs alla
// tre, och panelens tillstånd är orört.
void fallFlytTreNivaer()
{
    struct Mellan : glaze::AppWindow {
        glaze::DockHost  host;
        glaze::DockPanel panel{"Mellanpanel"};
        Mellan() : glaze::AppWindow("dock_gate mellan", 400, 300)
        {
            host.name  = "host";
            host.dock  = glaze::Dock::Fill;
            panel.name = "panel";
            panel.setFloating(true);
            root().adopt(host);
            host.adopt(panel);
        }
    };
    glaze::AppWindow  topp("dock_gate topp", 300, 200);
    Mellan            mellan;
    bool              veto   = true;
    int               frågad = 0;
    bool              kordes = false;
    std::string       iOnClose;
    glaze::AppWindow* flyt = nullptr;
    topp.onClose = [&](glaze::Widget*, glaze::CloseArgs& e) {
        ++frågad;
        e.cancel = veto;
    };
    mellan.onClose = [&] { iOnClose = mellan.host.layout(); };
    topp.hide();
    glaze::app().post([&] {
        kordes = true;
        mellan.hide();
        mellan.show(topp);
        vänta([&] { return mellan.panel.parent() != &mellan.host; }, 2000,
              [&](bool) {
            flyt = mellan.panel.appWindow();
            check(flyt && flyt->owner() == &mellan && mellan.owner() == &topp,
                  "T1: tre nivåer — toppen äger mellanfönstret, det äger det "
                  "flytande");
            topp.close();
            vänta([] { return false; }, 150, [&](bool) {
                check(frågad == 1 && topp.isOpen() && mellan.isOpen() && flyt &&
                          flyt->isOpen() && mellan.panel.isVisible() &&
                          mellan.panel.isFloating(),
                      "T2: vetot i toppen stoppar HELA kaskaden — det flytande "
                      "fönstret lade inget eget veto och rörde inte panelen",
                      std::to_string(frågad));
                veto = false;
                topp.close();   // inget postas efter — se FL9
            });
        });
    });
    topp.run();
    if (!kordes) {
        std::printf("  --   T hoppades över (ingen display)\n");
        ++g_skips;
        return;
    }
    check(!topp.isOpen() && !mellan.isOpen() && flyt && !flyt->isOpen(),
          "T3: utan veto stängs alla tre");
    check(mellan.panel.isFloating() && mellan.panel.isVisible() &&
              postFält(iOnClose, "f=panel,", 4) == "0",
          "T3: …och panelens tillstånd är orört — layout() i mellanfönstrets "
          "onClose säger flytande, öppen",
          iOnClose);
}

// ---- R. RIVNINGEN och den RETURNERANDE PANELEN som dör.
//   RA  värden rivs FÖRST: den levande panelen lossas utan att dess delträd
//       vandras — förälder och fönster nollade, dess LÅNADE barn orört — och
//       det flytande fönstret rivs
//   RB  panelen och dess lånade barn dör FÖRST, medan panelen flyter: värden
//       derefererar dem aldrig (`layout()` utan dem), och synken river
//       fönstret
//   RC  en panel PÅ VÄG HEM (en öppen popup håller flytten) dör under
//       väntan: modellen tappar den utan att röra den (`pruneModel`), och
//       synken river fönstret
//   RD  en ÄGD panel: förblir ägd genom flytten ut och hem, och förstörs EN
//       gång — med värden, medan den flyter
// Värden lossas ur riggen före rivningen (ett lånat barn som dör adopterat
// hade lämnat roten med en dinglande pekare).
struct RaknarPanel : glaze::DockPanel {
    int* rivna;
    explicit RaknarPanel(int* r) : glaze::DockPanel("Ägd"), rivna(r) {}
    ~RaknarPanel() override { ++*rivna; }
};

void fallFlytRivning()
{
    struct Rigg : glaze::AppWindow {
        Rigg() : glaze::AppWindow("dock_gate rivning", 400, 300) {}
        using glaze::AppWindow::handleEvent;
        using glaze::AppWindow::root;
        void mus(gfx::EventType t, gfx::Pointf p)
        {
            gfx::Event e;
            e.type     = t;
            e.windowId = window().id();
            e.mouseX   = p.x;
            e.mouseY   = p.y;
            e.button   = gfx::MouseButton::Left;
            handleEvent(e);
        }
        void tangent(gfx::Key k, std::uint32_t mods)
        {
            gfx::Event e;
            e.type     = gfx::EventType::KeyDown;
            e.windowId = window().id();
            e.key      = k;
            e.mods     = mods;
            handleEvent(e);
        }
    };
    Rigg w;
    w.hide();
    bool              kordes = false;
    const std::string titelE = unikTitel("rivE");

    auto värdA = std::make_unique<glaze::DockHost>();
    glaze::DockPanel panelA("A");
    glaze::Label     barnA;
    glaze::DockHost  värdB;
    auto             panelB = std::make_unique<glaze::DockPanel>("B");
    auto             barnB  = std::make_unique<glaze::Label>();
    auto             panelC = std::make_unique<glaze::DockPanel>("C");
    glaze::DockPanel lev("Lev");      // RC: en levande flik i C:s cell
    glaze::TextInput levFält;
    auto             panelE = std::make_unique<glaze::DockPanel>(titelE);
    auto             fältE  = std::make_unique<glaze::TextInput>();
    int              rivna  = 0;

    std::vector<std::function<void()>> steg;
    std::size_t                        nr = 0;
    auto nästa = [&] {
        if (nr < steg.size())
            glaze::app().post(steg[nr++]);
    };

    steg.push_back([&] {
        kordes      = true;
        värdA->name = "vardA";
        värdA->dock = glaze::Dock::Fill;
        panelA.name = "a";
        barnA.name  = "barnA";
        w.root().adopt(*värdA);
        panelA.adopt(barnA);
        värdA->adopt(panelA);
        panelA.setFloating(true);
        vänta([&] { return panelA.parent() != värdA.get(); }, 2000, [&](bool) {
            const std::size_t n = antalFönster();
            check(panelA.parent() != värdA.get() && panelA.appWindow() != &w,
                  "RA: (förutsättning) panelen flyter");
            w.root().unadopt(*värdA);
            värdA.reset();
            check(panelA.parent() == nullptr && panelA.appWindow() == nullptr &&
                      barnA.parent() == &panelA && antalFönster() == n - 1,
                  "RA: värden rivs först — panelen lossad utan barnvandring, "
                  "dess lånade barn orört, fönstret rivet",
                  std::to_string(antalFönster()));
            // BARNETS FÖNSTERPEKARE: det
            // rivna fönstrets livstecken har gått ut, så barnet har inget
            // fönster — och en sättare som invaliderar skriver inte genom
            // en dinglande pekare.
            check(barnA.appWindow() == nullptr,
                  "RA: det lånade barnets fönster är borta (livstecknet), "
                  "inte en dinglande pekare");
            barnA.setText("efter");
            check(barnA.text() == "efter",
                  "RA: …och en sättare på barnet går igenom");
            nästa();
        });
    });

    steg.push_back([&] {
        värdB.name = "vardB";
        värdB.dock = glaze::Dock::Fill;
        w.root().adopt(värdB);
        panelB->name = "b";
        panelB->adopt(*barnB);
        värdB.adopt(*panelB);
        panelB->setFloating(true);
        vänta([&] { return panelB->parent() != &värdB; }, 2000, [&](bool) {
            const std::size_t n = antalFönster();
            barnB.reset();    // det LÅNADE barnet dör först …
            panelB.reset();   // … därefter panelen, medan den flyter
            check(värdB.layout().find("=b,") == std::string::npos,
                  "RB: den döda panelen syns inte i layout() — ingen "
                  "dereferens",
                  värdB.layout());
            vänta([&, n] { return antalFönster() == n - 1; }, 2000,
                  [&, n](bool) {
                check(antalFönster() == n - 1,
                      "RB: synken river den döda panelens fönster",
                      std::to_string(antalFönster()));
                nästa();
            });
        });
    });

    // RC: C går hem som FLIK hos Lev (vänsterkanten) men hålls kvar av en
    // öppen popup; ett remsskiktsdrag pågår och fokus står i Levs fält när C
    // dör. Ctrl+PageDown och skiktsläppet direkt efter dödsfallet läser
    // cellen och remsan — utan C.
    steg.push_back([&] {
        lev.name     = "lev";
        lev.width    = 120;
        levFält.dock = glaze::Dock::Top;
        lev.adopt(levFält);
        värdB.adopt(lev);
        panelC->name  = "c";
        PopupYta* hin = panelC->add<PopupYta>();   // ÄGD — dör med panelen
        värdB.adopt(*panelC);
        panelC->setFloating(true);
        vänta([&] { return panelC->parent() != &värdB; }, 2000, [&, hin](bool) {
            const std::size_t n = antalFönster();
            hin->öppen = true;
            värdB.dockPanel(*panelC, lev, glaze::Dock::Fill);   // hem som flik
            // …och LEVS flik visas: `dockPanel` gör den dockade panelen till
            // cellens AKTIVA flik (F10), så utan det här stod Lev dold — ingen
            // yta, skiktet mättes vid x = 0 och fältet var inget fokusmål. Fallet
            // gäller fokus i Levs fält och remsans skikt när C dör.
            lev.activate();
            vänta([] { return false; }, 150, [&, n](bool) {
                check(sammaCell(värdB.layout(), "lev", "c") &&
                          panelC->parent() != &värdB,
                      "RC: på väg hem som flik hos Lev — modellen har panelen, "
                      "flytten väntar",
                      värdB.layout());
                const float      s  = w.window().uiScale();
                const gfx::Rectf lr = lev.resolve(lev.behaviorCtx());
                const float      t  = w.style().splitterThick * s;
                const gfx::Pointf skikt{lr.x + lr.w + t / 2.0f,
                                        lr.y + lr.h / 2.0f};
                const gfx::Pointf drag{skikt.x + 20.0f * s, skikt.y};
                const float       förr = lev.width;
                w.mus(gfx::EventType::MouseButtonDown, skikt);
                w.mus(gfx::EventType::MouseMotion, drag);
                // Fokus till Levs fält EFTER trycket: värden är ett tabbstopp
                // (flikgruppen Lev + C) och trycket på skiktet gav den fokus —
                // klick flyttar fokus till en fokuserbar widget (WinForms-
                // mönstret, `AppWindow::handleEvent`). Fokuset före trycket
                // stod alltså inte kvar till Ctrl+PageDown.
                levFält.focus();
                panelC.reset();   // dör under väntan, mitt i skiktdraget
                check(värdB.layout().find("=c,") == std::string::npos,
                      "RC: den döda panelen är borta ur layout() — modellen "
                      "rörde den inte",
                      värdB.layout());
                w.tangent(gfx::Key::PageDown, gfx::kModCtrl);
                check(levFält.focused(),
                      "RC: Ctrl+PageDown i Levs flik direkt efter dödsfallet — "
                      "cellen har en flik kvar, fokus står kvar");
                w.mus(gfx::EventType::MouseButtonUp, drag);
                check(lev.width > förr,
                      "RC: skiktsläppet direkt efter dödsfallet skriver "
                      "remsans mått — bara till de levande",
                      std::to_string(lev.width));
                check(värdB.setLayout(värdB.layout()),
                      "RC: …och en operation på modellen går igenom");
                vänta([&, n] { return antalFönster() == n - 1; }, 2000,
                      [&, n](bool) {
                    check(antalFönster() == n - 1,
                          "RC: synken river den döda panelens fönster",
                          std::to_string(antalFönster()));
                    nästa();
                });
            });
        });
    });

    // RE: ett SYNLIGT flytande fönster med fokus i panelens fält — panelen
    // och fältet dör, och fönstret ritas DIREKT efteråt:
    // det har släppt panelen och glömt sina widgetpekare i samma
    // ögonblick, och döljs. På Windows får det tomma fönstret dessutom mus och
    // tangent i LIVSTIDSLUCKAN — efter döden, före synken som river det — och
    // kontrollen ser att de kom fram medan det levde (`postaLuckKontroll`).
    steg.push_back([&] {
        w.show();
        panelE->name = "e";
        fältE->dock  = glaze::Dock::Top;
        panelE->adopt(*fältE);
        värdB.adopt(*panelE);
        panelE->setFloating(true);
        // LUGNET FÖRE DÖDEN (som FT8): synkar som flytten och fönstrets
        // öppning postade ska ha gått — en väntande synk hade tagit dödens
        // plats i kön och rivit fönstret före kontrollen.
        vänta([&] { return panelE->parent() != &värdB; }, 2000, [&](bool) {
            vänta([] { return false; }, 100, [&](bool) {
                glaze::AppWindow* fe = panelE->appWindow();
                check(fe && fe != &w && !fe->isHidden(),
                      "RE: (förutsättning) ett synligt flytande fönster");
                if (!fe || fe == &w) {
                    w.hide();
                    nästa();
                    return;
                }
                // Ett klick mitt i den tomma kroppen (D3D11).
                const std::vector<gfx::Pointf> klick{{20.0f, 20.0f}};
                bool                           posta = false;
#ifdef GLAZE_BACKEND_D3D11
                const HWND h = hwndFör(titelE);
                check(h != nullptr,
                      "RE: (fixturen) fönstret hittas med sin titel");
                posta = h != nullptr;
#endif
                const std::size_t n     = antalFönster();
                const auto        efter = std::make_shared<std::string>();
                postaLuckKontroll("RE", fe, posta, klick.back(),
                                  [&värdB, efter] {
                                      return värdB.layout() == *efter;
                                  });
                fältE->focus();
                fältE.reset();    // det lånade barnet dör först …
                panelE.reset();   // … därefter panelen
                fe->window().renderFrame();   // en ritning DIREKT efteråt
                *efter = värdB.layout();
                check(fe->isHidden(),
                      "RE: fönstret släppte den döda panelen och döljs i samma "
                      "ögonblick — ritningen direkt efteråt rör den inte");
#ifdef GLAZE_BACKEND_D3D11
                if (h)
                    postaLuckhändelser(h, klick);
#endif
                vänta([&, n] { return antalFönster() == n - 1; }, 2000,
                      [&, n, fe](bool) {
                    check(antalFönster() == n - 1 && !registrerat(fe),
                          "RE: …och synken river det tomma fönstret efteråt",
                          std::to_string(antalFönster()));
                    w.hide();
                    nästa();
                });
            });
        });
    });

    // Värden bor bakom en delad pekare: fortsättningarna fångar den, och den
    // rivs mitt i en av dem.
    auto värdD = std::make_shared<std::unique_ptr<glaze::DockHost>>(
        std::make_unique<glaze::DockHost>());
    RaknarPanel* ägd = nullptr;
    steg.push_back([&] {
        glaze::DockHost& vd = **värdD;
        vd.name             = "vardD";
        w.root().adopt(vd);
        ägd       = vd.add<RaknarPanel>(&rivna);
        ägd->name = "agd";
        ägd->setFloating(true);
        vänta([&] { return ägd->parent() != värdD->get(); }, 2000, [&](bool) {
            check(ägd->parent() != värdD->get() && rivna == 0,
                  "RD: den ägda panelen flyter — och lever");
            ägd->setFloating(false);
            vänta([&] { return ägd->parent() == värdD->get(); }, 2000,
                  [&](bool) {
                std::unique_ptr<glaze::Widget> u = (*värdD)->releaseOwned(*ägd);
                check(u.get() == ägd,
                      "RD: hemma igen är den ÄGD av värden — releaseOwned "
                      "lämnar ut den");
                (*värdD)->adopt(std::move(u));
                ägd->setFloating(true);
                vänta([&] { return ägd->parent() != värdD->get(); }, 2000,
                      [&](bool) {
                    w.root().unadopt(**värdD);
                    värdD->reset();   // värden rivs med panelen flytande
                    check(rivna == 1,
                          "RD: värden rivs — den ägda panelen förstörs med den, "
                          "en gång",
                          std::to_string(rivna));
                    w.close();
                });
            });
        });
    });

    nästa();
    w.run();
    w.root().unadopt(värdB);
    if (!kordes) {
        std::printf("  --   R hoppades över (ingen display)\n");
        ++g_skips;
    }
}

// ---- RV. DE GENERELLA KROKARNA med ett eget fönsterpar: ett ägt
// fönsters EGEN genväg svarar först och reservvägen frågas inte; en tangent
// ingen tog går till `acceleratorFallback`, FÖRE Tab-vandringen; och
// `ownerVisibilityChanged` får både visningen och döljningen av ägaren.
void fallReservvagen()
{
    struct Ägd : glaze::AppWindow {
        using glaze::AppWindow::handleEvent;
        std::vector<gfx::Key> frågor;
        std::vector<bool>     synlighet;
        int                   egen = 0;
        Ägd() : glaze::AppWindow("dock_gate ägd", 300, 200)
        {
            menuBar.show();
            menuBar.addMenu("M");
            menuBar.addItem("M", "Egen", "F7");
            menuBar.setHandler({"M", "Egen"}, [this] { ++egen; });
        }
        void tangent(gfx::Key k)
        {
            gfx::Event e;
            e.type     = gfx::EventType::KeyDown;
            e.windowId = window().id();
            e.key      = k;
            handleEvent(e);
        }

    protected:
        bool acceleratorFallback(gfx::Key k, std::uint32_t) override
        {
            frågor.push_back(k);
            return k == gfx::Key::F6;
        }
        void ownerVisibilityChanged(bool shown) override
        {
            synlighet.push_back(shown);
        }
    };
    glaze::AppWindow ägare("dock_gate ägare", 300, 200);
    Ägd              ägd;
    bool             kordes = false;
    ägare.hide();
    glaze::app().post([&] {
        kordes = true;
        ägd.hide();
        ägd.show(ägare);
        ägd.tangent(gfx::Key::F7);
        check(ägd.egen == 1 && ägd.frågor.empty(),
              "RV1: fönstrets EGEN genväg svarar först — reservvägen frågas "
              "inte");
        ägd.tangent(gfx::Key::F6);
        check(ägd.frågor.size() == 1 && ägd.frågor[0] == gfx::Key::F6,
              "RV2: en tangent ingen tog går till reservvägen");
        ägd.tangent(gfx::Key::Tab);
        check(ägd.frågor.size() == 2 && ägd.frågor[1] == gfx::Key::Tab,
              "RV3: reservvägen frågas FÖRE Tab-vandringen");
        ägare.show();
        ägare.hide();
        check(ägd.synlighet == std::vector<bool>{true, false},
              "RV4: ägaren visas och döljs — det ägda fönstret får veta båda "
              "(ownerVisibilityChanged)");
        ägare.close();
    });
    ägare.run();
    if (!kordes) {
        std::printf("  --   RV hoppades över (ingen display)\n");
        ++g_skips;
    }
}

// ================================= FLYTANDE VERKTYGSFÄLT
//
// DET FLYTANDE VERKTYGSFÄLTET genom fönstrens riktiga ingångar, i ett eget
// fönster som startar DOLT (`hide()` före `run()` — ett offscreen-fönster
// får inga flytande fönster) och visas när ett fall kräver en ritning eller
// ett synligt flytande fönster. Riggen: `std1` och `flyt` i band 0 överst,
// `vbar` STÅENDE i band 0 vid vänsterkanten. Fältens titlar är unika per
// process (FindWindowW-nyckeln för de POSTADE Win32-meddelandena).
//
//   FT1  `setFloating(true)`: bandet saknar fältet DIREKT och `layout()` har
//        `ft=` och ingen `t=`; efter ett pumpvarv står fältet i ett fönster
//        ägt av värdens, DOLT som det, med FAST storlek (`isFixedSize()`), och
//        innehållsytan är rubriken plus fältets liggande mått
//   FT2  `setFloating(false)`: sist i band 0 överst DIREKT, hemma i svansen
//   FT3  `setLayout` med `ft=` och en plats — flytande, och (D3D11) fönstret
//        där ±1 och `layout()` skriver platsen; en `t=` för det flytande
//        fältet dockar det. STÅENDE ÅTERDOCKNING: med minnet Left
//        ligger fältet när det flyter, och en `t=…,left,…` dockar det
//        stående med den satta längden flyttad till höjden
//   FT4  trasiga `ft=` (fältantal, platspar, flagga, kant, band, ett namn som
//        också står i `p=` och i `t=`) lämnar allt orört
//   FT5  dubbelklick på greppet: flytande och värdens onChange; (D3D11)
//        dubbelklick på rubriken i det flytande fönstret dockar det
//   FT7  `dock = Left` och `height = 300` flyter: bredd och höjd byts
//        (liggande längd 300, och fönstret 300 brett), och tillbaka
//   FT6  det flytande fönstrets `close()` (Alt+F4) — och (D3D11) rubrikens ✕
//        — DÖLJER fältet: fönstret döljs och lever, `ft=…,1`, onChange
//   FT9  en knapp adopteras i det flytande fältet — fönstret blir bredare
//        efter synken; en text och sedan en bild som växer medan ägaren och
//        fönstret FÖRBLIR DOLDA passas, med gränserna (min == max ==
//        klientytan), utan att något visas eller ritas
//        (synken postas av måttändringen); ett stilbyte, som inte
//        har någon händelse, passas FÖRE visningen när fönstret är dolt
//        och vid nästa ritning när det syns
//   FT11 VÄNTANDE HEMFLYTT: en öppen popup i fältet håller det i
//        fönstret med tillståndet redan dockat, och efter popupens omritning
//        kommer det hem
//   FT10 PLATSHÅLLAREN ÄR SLUTGEOMETRIN (D3D11, LV8):
//        det flytande, liggande fältets rubrik DRAS (postat, fall B —
//        fönstret följer pekaren) in över vänsterbandet — platshållarens
//        accentram ligger exakt där fältet hamnar (stående, med greppet), och
//        släppet dockar det där. Två gånger, med övertagandet som ett
//        antagande ur förutsättningen:
//        FT10a med alla fönster dolda (förgrunden är inte grindens) — NEKAT:
//        ingen capture hos värden, trycket och OS:ets capture kvar i fältets
//        fönster; FT10b med huvudfönstret gjort till förgrunden — LYCKAT:
//        värden och värdens fönster håller trycket, fältets fönster inte
//   LV6  fall A: std1:s grepp över tröskeln lossar fältet DIREKT (flytande,
//        , värdens capture), fönstret öppnas vid pekaren minus greppet
//        (D3D11 ±1), platshållaren står på referensytan i vänsterbandet, och
//        släppet dockar det där — , ETT onChange
//   LV7  flyts grepp släppt mitt i huvudytan: flytande,  med plats
//        (D3D11: fönstrets ±1), och fönstret är NOACTIVATE och TOOLWINDOW
//        (LV10:s fältdel)
//   FT8  ett flytande fält som värdemedlem DÖR med fokus i ett fält i det:
//        fönstret släpper det och döljs i samma ögonblick (som RE); på Windows
//        får det tomma fönstret ett klick på rubrikens ✕, ett dubbelklick på
//        rubriken och en tangent i LUCKAN mellan döden och synken, och en
//        kontroll postad före döden ser att de kom fram medan fönstret levde
//        och inte rörde värden; därefter rivs
//        fönstret
//   FT12 värden rivs FÖRST medan ett fält flyter: fältet lossas utan
//        barnvandring och dess lånade barn står orört, utan fönster
//
// VAD DET INTE MÄTER: hur rubriken SER UT (bara att ✕ träffas och att
// fönstret har rätt storlek), riktig mus och verkliga OS-förlopp (D3D11:s
// postade meddelanden når WndProc), macOS och Linux utöver det
// plattformsoberoende (TESTS.md), och ett drag i rubriken som släpps utanför
// ett mål (det gör ingenting — det levande draget mäts i LV).

// Mätaren — `contentSize` är protected (som i measure_gate).
struct Mätare : glaze::Widget {
    using glaze::Widget::measure;
};

struct FältRigg : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Logg              logg;
    glaze::DockHost   host;
    glaze::ToolBar    std1, flyt, vbar;
    glaze::ToolButton k1{"AB"}, k2{"CDE"}, k3{"G"}, kv{"V"};
    PopupYta          popup;   // i flyt — FT11:s hinder
    glaze::Panel      mitt;

    FältRigg(const std::string& tS, const std::string& tF,
             const std::string& tV)
        : glaze::AppWindow("dock_gate flytfält", 640, 420)
    {
        host.name = "host";
        host.dock = glaze::Dock::Fill;
        std1.name = "std1";
        flyt.name = "flyt";
        vbar.name = "vbar";
        std1.setText(tS);   // fönstrens titlar (FindWindowW)
        flyt.setText(tF);
        vbar.setText(tV);
        vbar.dock    = glaze::Dock::Left;
        popup.width  = 8;   // en yta, så att dess omritning når fönstret
        popup.height = 8;
        root().adopt(host);
        host.adopt(std1);
        std1.adopt(k1);
        host.adopt(flyt);
        flyt.adopt(k2);
        flyt.adopt(popup);
        host.adopt(vbar);
        vbar.adopt(kv);
        host.adopt(mitt);
        host.onChange = [this] { logg.push_back("H"); };
    }

    gfx::Rectf rekt(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    float      s() { return window().uiScale(); }
    gfx::Pointf grepp(glaze::ToolBar& t)
    {
        const gfx::Rectf r = rekt(t);
        const float      g = style().toolGripW * s();
        return t.isVertical() ? gfx::Pointf{r.x + r.w / 2.0f, r.y + g / 2.0f}
                              : gfx::Pointf{r.x + g / 2.0f, r.y + r.h / 2.0f};
    }
    void mus(gfx::EventType t, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(gfx::Pointf p) { mus(gfx::EventType::MouseButtonDown, p); }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void upp(gfx::Pointf p) { mus(gfx::EventType::MouseButtonUp, p); }
};

void fallFlytFalt()
{
    std::printf("\n=== FLYTANDE VERKTYGSFÄLT ===\n");
    const std::string tS = unikTitel("std1");
    const std::string tF = unikTitel("flyt");
    const std::string tV = unikTitel("vbar");
    const std::string tW = unikTitel("flytfalt");
    FältRigg          w(tS, tF, tV);
    w.setTitle(tW);   // huvudfönstrets FindWindowW-nyckel (FT10)
    w.hide();         // DOLD START — de flytande fönstren skapas dolda
    const bool drag = glaze::DockHost::isFloatDragSupported();
    const std::string start =
        "v2;t=std1,top,0,0,0;t=flyt,top,0,1,0;t=vbar,left,0,0,0";

    bool                               kordes = false;
    std::vector<std::function<void()>> steg;
    std::size_t                        nr = 0;
    auto nästa = [&] {
        if (nr < steg.size())
            glaze::app().post(steg[nr++]);
    };
    auto hemma = [&w](glaze::Widget& t) { return t.parent() == &w.host; };
    glaze::AppWindow* fw    = nullptr;   // flyts fönster
    std::size_t       antal = 0;

#ifdef GLAZE_BACKEND_D3D11
    // FT10 — PLATSHÅLLAREN ÄR SLUTGEOMETRIN (LV8 — fall B för ett
    // verktygsfält), körd två gånger med ÖVERTAGANDET som
    // ett antagande ur en förutsättning och aldrig avläst ur utfallet:
    // NEKAT (FT10a, ingen förgrund på grindens
    // tråd) — värden har ingen capture, och trycket och OS:ets capture står
    // kvar i fältets fönster — och LYCKAT (FT10b, huvudfönstret är förgrunden)
    // — värden håller den explicita capturen och värdens fönster OS:ets,
    // fältets fönster inte längre. Referensen först: flyt dockad efter vbar i
    // vänsterbandet (stående, med greppet) ger ytan R. Därefter flyter det
    // LIGGANDE, och dess rubrik DRAS med postade meddelanden in mitt i R
    // (`postatDrag` — resten till värdens fönster när övertagandet antas) —
    // platshållarens accentram ligger på R:s kanter och inte utanför dem — och
    // släppet dockar fältet exakt där.
    auto ft10 = [&](const std::string fall, bool övertag) {
        w.host.setLayout("v2;t=std1,top,0,0,0;t=vbar,left,0,0,0;"
                         "t=flyt,left,0,1,0");
        const gfx::Rectf R = w.rekt(w.flyt);
        w.host.setLayout("v2;t=std1,top,0,0,0;t=vbar,left,0,0,0;"
                         "ft=flyt,-,-,0,top,0");
        vänta([&] { return !hemma(w.flyt); }, 2000,
              [&, R, fall, övertag](bool) {
            fw            = w.flyt.appWindow();
            const HWND h  = hwndFör(tF);
            const HWND hw = hwndFör(tW);
            check(h != nullptr && hw != nullptr && fw && fw != &w &&
                      !w.flyt.isVertical(),
                  fall + ": (förutsättning) flyt flyter liggande i sitt "
                         "fönster");
            if (!h || !hw || !fw || fw == &w) {
                w.host.setLayout(start);
                nästa();
                return;
            }
            w.logg.clear();
            const float sk = fw->window().uiScale();
            PostDrag    d;
            d.källaH  = h;
            d.källa   = fw;
            d.värdH   = hw;
            d.värd    = &w;
            d.ner     = fw->window().clientToScreen(
                10.0f * sk, fw->style().dockHeaderH * sk / 2.0f);
            d.mål     = w.window().clientToScreen(R.x + R.w / 2.0f,
                                                  R.y + R.h / 2.0f);
            d.tröskel = 30.0f * sk;
            d.övertag = övertag;
            // Fortsättningarna körs i senare pumpvarv — allt de läser ur det
            // här steget fångas som VÄRDEN.
            const gfx::Color acc = w.host.behaviorCtx().theme->colors.accent;
            const float      dl  = w.style().dropLineW * w.s();
            glaze::AppWindow* const källa = fw;
            postatDrag(
                d,
                [&w, källa, fall, övertag](bool fångat) {
                    if (!fångat || !registrerat(källa)) {
                        std::printf("  --   %s: trycket tog ingen capture i "
                                    "fältets fönster — övertagandets kontroll "
                                    "hoppas över\n",
                                    fall.c_str());
                        ++g_skips;
                        return;
                    }
                    const bool vE = w.host.hasPointerCapture();
                    const bool vO = w.window().hasMouseCapture();
                    const bool kO = källa->window().hasMouseCapture();
                    const std::string läge =
                        std::string("värden ") + (vE ? "capture" : "ingen") +
                        ", värdens fönster " + (vO ? "OS-capture" : "ingen") +
                        ", fältets fönster " + (kO ? "OS-capture" : "ingen");
                    if (övertag)
                        check(vE && vO && !kO,
                              fall + ": LYCKAT övertagande vid lossningen — "
                                     "värden håller den explicita capturen och "
                                     "värdens fönster OS:ets, fältets fönster "
                                     "inte längre",
                              läge);
                    else
                        check(!vE && !vO && kO,
                              fall + ": NEKAT övertagande — ingenting ändrat: "
                                     "värden utan capture, trycket och OS:ets "
                                     "capture kvar i fältets fönster",
                              läge);
                },
                [&w, acc, R, dl, fall] {
                    int        W = 0, H = 0;
                    const auto b = bildAv(w, W, H);
                    auto acc2 = [&b, W, H, acc](float x, float y) {
                        std::uint32_t px = 0;
                        return pixelI(b, W, H, x, y, px) && somFärg(px, acc);
                    };
                    const float mx = R.x + R.w / 2.0f;
                    const float my = R.y + R.h / 2.0f;
                    if (b.empty()) {
                        std::printf("  --   %s: pixlarna kunde inte läsas — "
                                    "platshållarens kontroll hoppas över\n",
                                    fall.c_str());
                        ++g_skips;
                        return;
                    }
                    check(acc2(R.x + ramMitt(dl), my) &&
                              acc2(R.x + R.w - ramMitt(dl), my) &&
                              acc2(mx, R.y + ramMitt(dl)) &&
                              acc2(mx, R.y + R.h - ramMitt(dl)),
                          fall + ": platshållarens ram ligger på "
                                 "slutgeometrins fyra kanter",
                          rekt(R));
                    check(!acc2(mx, R.y - 2.0f) &&
                              !acc2(mx, R.y + R.h + 2.0f) &&
                              !acc2(R.x + R.w + 2.0f, my),
                          fall + ": …och inte utanför dem — platshållaren är "
                                 "exakt slutgeometrin (med greppet, stående)",
                          rekt(R));
                },
                [&, R, fall] {
                    vänta([&] { return hemma(w.flyt); }, 2000,
                          [&, R, fall](bool) {
                        check(hemma(w.flyt) && w.flyt.isVertical() &&
                                  lika(w.rekt(w.flyt), R) &&
                                  w.host.layout().find("t=flyt,left,0,1,0") !=
                                      std::string::npos &&
                                  fog(w.logg) == "[H]",
                              fall + ": släppet dockade fältet exakt där "
                                     "platshållaren stod — ETT onChange",
                              rekt(w.rekt(w.flyt)) + " " + fog(w.logg));
                        w.host.setLayout(start);
                        nästa();
                    });
                });
        });
    };
#endif

    // ---- FT1. FLYTANDE: tillståndet direkt, fönstret i svansen.
    steg.push_back([&] {
        kordes = true;
        check(w.host.layout() == start, "FT1: (riggen) utgångsläget",
              w.host.layout());
        w.flyt.setFloating(true);
        check(w.host.layout() ==
                  "v2;t=std1,top,0,0,0;t=vbar,left,0,0,0;ft=flyt,-,-,0,top,0",
              "FT1: bandet saknar fältet DIREKT — ft= och ingen t=",
              w.host.layout());
        check(tom(w.rekt(w.flyt)),
              "FT1: …och fältet har ingen yta i värden medan det väntar på "
              "sitt fönster");
        vänta([&] { return !hemma(w.flyt); }, 2000, [&](bool) {
            fw    = w.flyt.appWindow();
            antal = antalFönster();
            check(!hemma(w.flyt) && fw && fw != &w && fw->owner() == &w,
                  "FT1: fältet står i ett eget fönster ägt av värdens");
            if (!fw || fw == &w) {
                w.close();
                return;
            }
            check(fw->isHidden() && fw->isFixedSize(),
                  "FT1: dolt som ägaren, och FAST storlek (min == max)");
            const glaze::Ctx  fc = w.flyt.behaviorCtx();
            const gfx::Sizef  m  = Mätare::measure(w.flyt, fc);
            const auto&       st = *fc.style;
            const gfx::Sizef  cs = fw->contentSize();
            const float bredd = std::max(m.w, st.dockHeaderPadX + st.dockHeaderH);
            check(std::abs(cs.w - bredd) <= 0.51f &&
                      std::abs(cs.h - (st.dockHeaderH + m.h)) <= 0.51f,
                  "FT1: innehållsytan är rubriken plus fältet liggande i sin "
                  "egen längd",
                  std::to_string(cs.w) + " × " + std::to_string(cs.h));
            check(postFält(w.host.layout(), "ft=flyt,", 2) == "0",
                  "FT1: ft= skriver fältet öppet", w.host.layout());
            check((postFält(w.host.layout(), "ft=flyt,", 0) == "-") == !drag,
                  "FT1: platsen ur det levande fönstret — `-` bara där fönster "
                  "saknar skärmposition (D12)",
                  w.host.layout());
            nästa();
        });
    });

    // ---- FT2. TILLBAKA: sist i band 0 överst DIREKT, hemma i svansen.
    steg.push_back([&] {
        w.flyt.setFloating(false);
        check(w.host.layout() == start,
              "FT2: setFloating(false) — sist i band 0 överst, t= och ingen "
              "ft= DIREKT",
              w.host.layout());
        vänta([&] { return hemma(w.flyt); }, 2000, [&](bool) {
            check(hemma(w.flyt) && antalFönster() == antal - 1 &&
                      !tom(w.rekt(w.flyt)),
                  "FT2: hemma i bandet, och fönstret är rivet",
                  std::to_string(antalFönster()));
            nästa();
        });
    });

    // ---- FT3. ft= MED PLATS, och en t= som dockar.
    steg.push_back([&] {
        check(w.host.setLayout("v2;t=std1,top,0,0,0;t=vbar,left,0,0,0;"
                               "ft=flyt,300,200,0,top,0") &&
                  w.flyt.isFloating() && w.flyt.floatingPosition().x == 300.0f &&
                  w.flyt.floatingPosition().y == 200.0f,
              "FT3: setLayout med ft= — flytande, platsen lagrad");
        vänta([&] { return !hemma(w.flyt); }, 2000, [&](bool) {
            fw = w.flyt.appWindow();
#ifdef GLAZE_BACKEND_D3D11
            if (fw && drag) {
                const glaze::WindowGeometry g = fw->geometry();
                check(std::abs(g.x - 300) <= 1 && std::abs(g.y - 200) <= 1,
                      "FT3: fönstret står där ft= sa (±1 logisk px)",
                      std::to_string(g.x) + "," + std::to_string(g.y));
                check(std::abs(läsTal(postFält(w.host.layout(), "ft=flyt,", 0)) -
                               300.0f) <= 1.0f,
                      "FT3: layout() läser platsen ur det levande fönstret",
                      w.host.layout());
            }
#endif
            check(w.host.setLayout(start) && !w.flyt.isFloating(),
                  "FT3: en t= för det flytande fältet dockar det DIREKT");
            vänta([&] { return hemma(w.flyt); }, 2000, [&](bool) {
                check(hemma(w.flyt) && w.host.layout() == start,
                      "FT3: …och hemma i svansen", w.host.layout());
                nästa();
            });
        });
    });

    // ---- FT3b. STÅENDE ÅTERDOCKNING: längden 160 liggande, ft=
    // med minnet Left (det LIGGER när det flyter), och en t= vid vänsterkanten
    // dockar det STÅENDE med längden som höjd.
    steg.push_back([&] {
        w.flyt.width = 160;
        check(w.host.setLayout("v2;t=std1,top,0,0,0;t=vbar,left,0,0,0;"
                               "ft=flyt,-,-,0,left,0") &&
                  w.flyt.isFloating() && !w.flyt.isVertical() &&
                  w.flyt.dock == glaze::Dock::Left && w.flyt.width == 160.0f,
              "FT3b: ft= med minnet Left — fältet flyter och LIGGER, längden "
              "är bredden");
        vänta([&] { return !hemma(w.flyt); }, 2000, [&](bool) {
            check(w.host.setLayout("v2;t=std1,top,0,0,0;t=vbar,left,0,0,0;"
                                   "t=flyt,left,0,1,0") &&
                      !w.flyt.isFloating() && w.flyt.isVertical() &&
                      w.flyt.height == 160.0f && w.flyt.width == 0.0f,
                  "FT3b: t= vid vänsterkanten — dockat STÅENDE, längden 160 "
                  "är nu höjden");
            vänta([&] { return hemma(w.flyt); }, 2000, [&](bool) {
                check(nära(w.rekt(w.flyt).h, 160.0f * w.s()),
                      "FT3b: hemma i vänsterbandet, 160 högt",
                      rekt(w.rekt(w.flyt)));
                w.host.setLayout(start);
                w.flyt.width = 0;   // AUTO igen (efter riktningsbytet)
                w.host.invalidate();
                nästa();
            });
        });
    });

    // ---- FT4. TRASIGA ft= lämnar allt orört.
    steg.push_back([&] {
        const std::string före = w.host.layout();
        const char* trasiga[] = {
            "v2;ft=flyt,1,2,0,top",                                // fältantal
            "v2;ft=flyt,1,-,0,top,0",                              // platspar
            "v2;ft=flyt,1,2,2,top,0",                              // flagga
            "v2;ft=flyt,1,2,0,mitten,0",                           // kant
            "v2;ft=flyt,1,2,0,top,-1",                             // band
            "v2;ft=flyt,1,2,0,top,x",                              // band
            "v2;s=left;c=1;p=flyt,1,0,1,0,0,0;ft=flyt,-,-,0,top,0",   // p=
            "v2;t=flyt,top,0,0,0;ft=flyt,-,-,0,top,0",                // t=
        };
        bool allaAvvisade = true;
        for (const char* t : trasiga)
            allaAvvisade = !w.host.setLayout(t) && allaAvvisade;
        check(allaAvvisade && w.host.layout() == före && !w.flyt.isFloating(),
              "FT4: åtta trasiga ft= avvisas — layouten orörd",
              w.host.layout());
        nästa();
    });

    // ---- FT5. DUBBELKLICK PÅ GREPPET gör ett dockat fält flytande; på
    // rubriken (D3D11, postat) dockar det.
    steg.push_back([&] {
        w.logg.clear();
        const gfx::Pointf g = w.grepp(w.std1);
        w.ner(g);
        w.upp(g);
        w.ner(g);
        w.upp(g);
        check(w.std1.isFloating() && fog(w.logg) == "[H]",
              "FT5: dubbelklick på greppet — flytande, värdens onChange",
              fog(w.logg));
        vänta([&] { return !hemma(w.std1); }, 2000, [&](bool) {
            glaze::AppWindow* fs = w.std1.appWindow();
#ifdef GLAZE_BACKEND_D3D11
            const HWND h = hwndFör(tS);
            check(h != nullptr && fs && fs != &w,
                  "FT5: (fixturen) std1:s fönster hittas med sin titel");
            if (h && fs && fs != &w) {
                w.logg.clear();
                const float sk = fs->window().uiScale();
                const int   rx = static_cast<int>(10.0f * sk);
                const int   ry =
                    static_cast<int>(fs->style().dockHeaderH * sk / 2.0f);
                postaMus(h, WM_LBUTTONDOWN, rx, ry, MK_LBUTTON);
                postaMus(h, WM_LBUTTONUP, rx, ry, 0);
                postaMus(h, WM_LBUTTONDOWN, rx, ry, MK_LBUTTON);
                postaMus(h, WM_LBUTTONUP, rx, ry, 0);
                vänta([&] { return !w.std1.isFloating(); }, 2000, [&](bool) {
                    check(!w.std1.isFloating() && fog(w.logg) == "[H]",
                          "FT5: dubbelklick på rubriken i det flytande "
                          "fönstret — dockat, onChange",
                          fog(w.logg));
                    vänta([&] { return hemma(w.std1); }, 2000, [&](bool) {
                        w.host.setLayout(start);   // std1 först igen
                        nästa();
                    });
                });
                return;
            }
#endif
            (void)fs;
            w.std1.setFloating(false);
            vänta([&] { return hemma(w.std1); }, 2000, [&](bool) {
                w.host.setLayout(start);
                nästa();
            });
        });
    });

    // ---- FT7. ETT STÅENDE FÄLT FLYTER: bredd och höjd byts, och tillbaka.
    steg.push_back([&] {
        w.vbar.height = 300;   // stående: längden
        w.vbar.setFloating(true);
        check(!w.vbar.isVertical() && w.vbar.width == 300.0f &&
                  w.vbar.height == 0.0f,
              "FT7: Dock::Left flyter — det ligger, och längden 300 är nu "
              "bredden",
              std::to_string(w.vbar.width) + " × " +
                  std::to_string(w.vbar.height));
        vänta([&] { return !hemma(w.vbar); }, 2000, [&](bool) {
            glaze::AppWindow* fv = w.vbar.appWindow();
            check(fv && fv != &w && std::abs(fv->contentSize().w - 300.0f) <= 0.51f,
                  "FT7: fönstret är 300 brett — den satta längden",
                  fv ? std::to_string(fv->contentSize().w) : std::string("-"));
            w.vbar.setFloating(false);
            check(w.vbar.isVertical() && w.vbar.height == 300.0f &&
                      w.vbar.width == 0.0f,
                  "FT7: dockat igen — det står, och höjden är längden igen");
            vänta([&] { return hemma(w.vbar); }, 2000, [&](bool) {
                w.vbar.height = 0;
                w.host.setLayout(start);
                nästa();
            });
        });
    });

#ifdef GLAZE_BACKEND_D3D11
    // ---- FT10a. NEKAT ÖVERTAGANDE (LV8) —
    // medan ALLA grindens fönster är dolda kan förgrunden inte vara grindens
    // tråd, och värden får inte ta trycket. (Står den ändå på grindens tråd
    // hoppas fallet över.)
    steg.push_back([&] {
        if (w.isHidden() && !förgrundÄrVår()) {
            ft10("FT10a", false);
            return;
        }
        std::printf("  --   FT10a: grindens tråd äger förgrunden — det nekade "
                    "övertagandet hoppas över\n");
        ++g_skips;
        nästa();
    });
#endif

    // Fönstret VISAS — fallen härifrån kräver ett synligt flytande fönster
    // eller en ritning.
    steg.push_back([&] {
        w.flyt.setFloating(true);
        vänta([&] { return !hemma(w.flyt); }, 2000, [&](bool) {
            fw = w.flyt.appWindow();
            w.show();
            check(fw && fw != &w && !fw->isHidden(),
                  "FT6: (kontroll) huvudfönstret visas — det flytande följer");
            nästa();
        });
    });

    // ---- FT6. STÄNGNINGEN döljer FÄLTET: fönstrets egen (Alt+F4) och, på
    // Windows, rubrikens ✕.
    steg.push_back([&] {
        w.logg.clear();
        fw->close();
        vänta([&] { return !w.flyt.isVisible() && fw->isHidden(); }, 2000,
              [&](bool) {
            check(!w.flyt.isVisible() && fw->isOpen() && fw->isHidden() &&
                      fog(w.logg) == "[H]",
                  "FT6: fönstrets stängning DÖLJER fältet — fönstret döljs och "
                  "lever, onChange",
                  fog(w.logg));
            check(postFält(w.host.layout(), "ft=flyt,", 2) == "1",
                  "FT6: …och ft= skriver det dolt", w.host.layout());
            w.flyt.show();
            vänta([&] { return !fw->isHidden(); }, 2000, [&](bool) {
                check(!fw->isHidden(), "FT6: show() visar fältet igen");
#ifdef GLAZE_BACKEND_D3D11
                const HWND h = hwndFör(tF);
                if (h) {
                    w.logg.clear();
                    const float sk = fw->window().uiScale();
                    const float hh = fw->style().dockHeaderH * sk;
                    const int   cx = static_cast<int>(
                        fw->window().contentArea().w - hh / 2.0f);
                    const int cy = static_cast<int>(hh / 2.0f);
                    postaMus(h, WM_LBUTTONDOWN, cx, cy, MK_LBUTTON);
                    postaMus(h, WM_LBUTTONUP, cx, cy, 0);
                    vänta([&] { return !w.flyt.isVisible(); }, 2000,
                          [&](bool) {
                        check(!w.flyt.isVisible() && fog(w.logg) == "[H]",
                              "FT6: rubrikens ✕ döljer fältet, onChange",
                              fog(w.logg));
                        w.flyt.show();
                        vänta([&] { return !fw->isHidden(); }, 2000,
                              [&](bool) { nästa(); });
                    });
                    return;
                }
                check(false, "FT6: (fixturen) flyts fönster hittas med sin "
                             "titel");
#endif
                nästa();
            });
        });
    });

    // ---- FT9. FÖNSTRET FÖLJER FÄLTETS MÅTT.
    // `låst`: fast storlek, och gränserna är den klientyta
    // fönstret faktiskt har — en passning som glömde gränserna hade lämnat
    // dem på det ursprungliga måttet.
    auto låst = [](glaze::AppWindow& f) {
        const gfx::Sizef mn = f.minimumSize();
        const gfx::Sizef cs = f.clientSize();
        return f.isFixedSize() && std::abs(mn.w - cs.w) <= 0.51f &&
               std::abs(mn.h - cs.h) <= 0.51f;
    };
    steg.push_back([&, låst] {
        const float b0 = fw->contentSize().w;
        w.flyt.adopt(w.k3);
        vänta([&, b0] { return fw->contentSize().w > b0 + 1.0f; }, 2000,
              [&, b0, låst](bool) {
            check(fw->contentSize().w > b0 + 1.0f && låst(*fw),
                  "FT9: en knapp adopteras i det flytande fältet — fönstret "
                  "blir bredare efter synken",
                  std::to_string(b0) + " → " +
                      std::to_string(fw->contentSize().w));
            // DOLT, OCH DOLT FÖRBLIR DET: ägaren och fönstret visas inte förrän
            // texten och bilden är kontrollerade, så ingen ritning och ingen
            // visning kan ha passat fönstret — synken postas av måttändringen
            // själv (`ToolBar::childLayoutChanged`).
            w.hide();
            const float b1 = fw->contentSize().w;
            w.k3.setText("GGGGGGGGGGGGGGGG");
            vänta([&, b1] { return fw->contentSize().w > b1 + 1.0f; }, 2000,
                  [&, b1, låst](bool) {
                check(w.isHidden() && fw->isHidden() &&
                          fw->contentSize().w > b1 + 1.0f && låst(*fw),
                      "FT9: en text som växer medan ägaren och fönstret är "
                      "DOLDA — passat, med gränserna, utan att något visats",
                      std::to_string(b1) + " → " +
                          std::to_string(fw->contentSize().w));
                const float   b2 = fw->contentSize().w;
                glaze::Bitmap bild(16, 16);
                bild.fill(gfx::Color{40, 120, 200, 255});
                w.k3.setImg(bild);   // till vänster om texten: bredare
                vänta([&, b2] { return fw->contentSize().w > b2 + 1.0f; },
                      2000, [&, b2, låst](bool) {
                    check(w.isHidden() && fw->isHidden() &&
                              fw->contentSize().w > b2 + 1.0f && låst(*fw),
                          "FT9: …och en bild likaså",
                          std::to_string(b2) + " → " +
                              std::to_string(fw->contentSize().w));
                    // Ett STILBYTE har ingen händelse som når fältet: medan
                    // fönstret är dolt passas det FÖRE visningen …
                    const glaze::Style förra = w.style();
                    glaze::Style       st    = förra;
                    st.toolBarH += 10.0f;
                    const float h0 = fw->contentSize().h;
                    w.setStyle(st);
                    w.show();
                    check(!fw->isHidden() &&
                              fw->contentSize().h >= h0 + 9.5f && låst(*fw),
                          "FT9: ett stilbyte medan fönstret var dolt — passat "
                          "FÖRE det visas",
                          std::to_string(h0) + " → " +
                              std::to_string(fw->contentSize().h));
                    // … och i ett SYNLIGT fönster vid nästa ritning.
                    const float h1 = fw->contentSize().h;
                    w.setStyle(förra);
                    vänta([&, h1] { return fw->contentSize().h <= h1 - 9.5f; },
                          2000, [&, h1, låst](bool) {
                        check(fw->contentSize().h <= h1 - 9.5f && låst(*fw),
                              "FT9: stilen tillbaka i det synliga fönstret — "
                              "passat vid nästa ritning",
                              std::to_string(h1) + " → " +
                                  std::to_string(fw->contentSize().h));
                        nästa();
                    });
                });
            });
        });
    });

    // ---- FT11. EN VÄNTANDE HEMFLYTT: en öppen popup i fältet
    // håller det kvar — tillståndet säger dockat direkt och FÖRBLIR sant —
    // och när popupen stängts och ritats om kommer det hem.
    steg.push_back([&] {
        w.popup.öppen = true;
        w.flyt.setFloating(false);
        vänta([] { return false; }, 150, [&](bool) {
            const std::string l = w.host.layout();
            check(!hemma(w.flyt) && !w.flyt.isFloating() &&
                      l.find("t=flyt,") != std::string::npos &&
                      l.find("ft=flyt") == std::string::npos,
                  "FT11: en öppen popup i fältet NEKAR flytten — den väntar, "
                  "och layout() säger redan dockat",
                  l);
            w.popup.öppen = false;
            w.popup.invalidate();   // popupens ägare ritas om när den stängs
            vänta([&] { return hemma(w.flyt); }, 2000, [&](bool) {
                check(hemma(w.flyt),
                      "FT11: hindret borta — nästa ritning gjorde flytten hem");
                nästa();
            });
        });
    });

#ifdef GLAZE_BACKEND_D3D11
    // ---- FT10b. LYCKAT ÖVERTAGANDE (LV8) — villkorat på förgrunden som
    // capture_gate D6: huvudfönstret,
    // synligt sedan FT9, görs till förgrunden och det prövas; bara en nekad
    // förberedelse hoppar över.
    steg.push_back([&] {
        if (!w.isHidden() && görTillFörgrund(hwndFör(tW))) {
            ft10("FT10b", true);
            return;
        }
        std::printf("  --   FT10b: huvudfönstret kunde inte göras till "
                    "förgrunden (ingen förgrund i den här sessionen) — det "
                    "lyckade övertagandet hoppas över\n");
        ++g_skips;
        nästa();
    });
#endif

    // ---- LV6. VERKTYGSFÄLT, FALL A: std1:s grepp över tröskeln
    // — fältet lossnar DIREKT (flytande, bandet utan det, ft= och ingen t=,
    // värden håller trycket); efter svansen står fönstret där pekaren minus
    // greppet (längs fältet, halva rubrikhöjden) är (D3D11 ±1). Referensen först:
    // std1 sist i vänsterbandet ger ytan R; draget in över vbars mitt visar
    // platshållaren på R, och släppet dockar fältet exakt där — t=, ETT
    // onChange.
    steg.push_back([&] {
        if (!drag) {
            nästa();
            return;
        }
        w.host.setLayout("v2;t=flyt,top,0,0,0;t=vbar,left,0,0,0;"
                         "t=std1,left,0,1,0");
        const gfx::Rectf R = w.rekt(w.std1);
        w.host.setLayout(start);
        w.logg.clear();
        const gfx::Pointf g     = w.grepp(w.std1);
        const gfx::Rectf  r     = w.rekt(w.std1);
        const gfx::Pointf p1    = {g.x + 30.0f * w.s(), g.y};
        const gfx::Pointf grepp = {g.x - r.x,
                                   w.style().dockHeaderH * w.s() / 2.0f};
        w.ner(g);
        w.flytta(p1);
        const std::string l = w.host.layout();
        check(w.std1.isFloating() && w.host.hasPointerCapture() &&
                  l.find("ft=std1,") != std::string::npos &&
                  // `;t=` — "t=std1," står annars i "ft=std1," självt
                  l.find(";t=std1,") == std::string::npos && w.logg.empty(),
              "LV6: greppet över tröskeln — fältet lossnar direkt: flytande, "
              "bandet utan det, ft=, värden håller trycket",
              l);
        vänta([&] { return !hemma(w.std1); }, 2000,
              [&, R, p1, grepp](bool) {
            glaze::AppWindow* fs = w.std1.appWindow();
            check(fs && fs != &w && w.host.hasPointerCapture(),
                  "LV6: efter svansen i ett eget fönster — och draget lever");
            if (fs && fs != &w)
                kollaHörn("LV6", w, *fs, p1, grepp);
            const gfx::Rectf  vb   = w.rekt(w.vbar);
            const gfx::Pointf över = {vb.x + vb.w / 2.0f, vb.y + vb.h / 2.0f};
            w.flytta(över);
            const gfx::Color acc = w.host.behaviorCtx().theme->colors.accent;
            const float      dl  = w.style().dropLineW * w.s();
            int              W = 0, H = 0;
            const auto       b   = bildAv(w, W, H);
            auto acc2 = [&b, W, H, acc](float x, float y) {
                std::uint32_t px = 0;
                return pixelI(b, W, H, x, y, px) && somFärg(px, acc);
            };
            if (!b.empty()) {
                check(acc2(R.x + R.w / 2.0f, R.y + ramMitt(dl)) &&
                          acc2(R.x + R.w / 2.0f, R.y + R.h - ramMitt(dl)),
                      "LV6: platshållaren står på fältets kommande plats i "
                      "vänsterbandet",
                      rekt(R));
            } else {
                std::printf("  --   LV6: pixlarna kunde inte läsas — "
                            "platshållarens kontroll hoppas över\n");
                ++g_skips;
            }
            w.upp(över);
            check(!w.std1.isFloating() && !w.host.hasPointerCapture() &&
                      w.host.layout().find("t=std1,left,0,1,0") !=
                          std::string::npos &&
                      fog(w.logg) == "[H]",
                  "LV6: släppt på platshållaren — dockat där, t=, ETT onChange",
                  w.host.layout() + " " + fog(w.logg));
            vänta([&] { return hemma(w.std1); }, 2000, [&, R](bool) {
                check(hemma(w.std1) && lika(w.rekt(w.std1), R),
                      "LV6: …hemma i svansen, exakt där platshållaren stod",
                      rekt(w.rekt(w.std1)));
                w.host.setLayout(start);
                nästa();
            });
        });
    });

    // ---- LV7. VERKTYGSFÄLT SLÄPPT UTAN PLATSHÅLLARE: flyts grepp dras
    // mitt i huvudytan och släpps — fältet FLYTER där, och ft= bär platsen
    // (D3D11: fönstrets, ±1 logisk px). ETT onChange.
    steg.push_back([&] {
        if (!drag) {
            nästa();
            return;
        }
        w.logg.clear();
        const gfx::Pointf g = w.grepp(w.flyt);
        w.ner(g);
        w.flytta({g.x + 30.0f * w.s(), g.y});
        const gfx::Rectf  m    = w.rekt(w.mitt);
        const gfx::Pointf ute  = {m.x + m.w / 2.0f, m.y + m.h / 2.0f};
        w.flytta(ute);
        w.upp(ute);
        check(w.flyt.isFloating() && !w.host.hasPointerCapture() &&
                  fog(w.logg) == "[H]",
              "LV7: släppt mitt i huvudytan — fältet flyter, ETT onChange",
              fog(w.logg));
        vänta([&] { return !hemma(w.flyt); }, 2000, [&](bool) {
            glaze::AppWindow* ff = w.flyt.appWindow();
            const std::string l  = w.host.layout();
            check(ff && ff != &w && postFält(l, "ft=flyt,", 0) != "-",
                  "LV7: …i sitt fönster, och ft= bär platsen", l);
#ifdef GLAZE_BACKEND_D3D11
            if (ff && ff != &w) {
                const glaze::WindowGeometry geo = ff->geometry();
                const gfx::Pointf           fp  = w.flyt.floatingPosition();
                check(geo.hasPosition &&
                          std::abs(fp.x - static_cast<float>(geo.x)) <= 1.0f &&
                          std::abs(fp.y - static_cast<float>(geo.y)) <= 1.0f,
                      "LV7: floatingPosition() är fönstrets plats (±1 logisk "
                      "px)",
                      std::to_string(fp.x) + "," + std::to_string(fp.y));
                // LV10 (fältets del): fönstret aktiveras aldrig.
                const HWND h = hwndFör(tF);
                const LONG_PTR ex =
                    h ? GetWindowLongPtrW(h, GWL_EXSTYLE) : 0;
                check(h != nullptr && (ex & WS_EX_NOACTIVATE) != 0 &&
                          (ex & WS_EX_TOOLWINDOW) != 0,
                      "LV10: fältets fönster är NOACTIVATE och TOOLWINDOW");
            }
#endif
            w.flyt.setFloating(false);
            vänta([&] { return hemma(w.flyt); }, 2000, [&](bool) {
                w.host.setLayout(start);
                nästa();
            });
        });
    });

    // ---- FT8. ETT FLYTANDE FÄLT DÖR med fokus i ett fält i det (som RE):
    // fönstret släpper det och döljs i samma ögonblick, och ritningen direkt
    // efteråt rör ingen död widget. På Windows får det tomma fönstret mus och
    // tangent i LIVSTIDSLUCKAN — efter döden, före synken som river det — på
    // rubrikens ✕ och ett dubbelklick på rubriken, fältets två vägar till
    // värden, och kontrollen ser att de kom fram medan fönstret levde och att
    // de varken rörde värdens onChange eller layouten (`postaLuckKontroll`).
    // Sist synken som river fönstret.
    auto dödFält = std::make_unique<glaze::ToolBar>();
    auto dödBarn = std::make_unique<glaze::TextInput>();
    const std::string tD = unikTitel("dod");
    steg.push_back([&] {
        dödFält->name = "dod";
        dödFält->setText(tD);
        dödFält->adopt(*dödBarn);
        w.host.adopt(*dödFält);
        dödFält->setFloating(true);
        // LUGNET FÖRE DÖDEN: synkar som flytten och fönstrets öppning postade
        // (fältets språkkrok vid visningen postar en) ska ha gått — en väntande
        // synk hade tagit dödens plats i kön och rivit fönstret före
        // kontrollen. Den inre `vänta`s första runda postas efter dem.
        vänta([&] { return dödFält->parent() != &w.host; }, 2000, [&](bool) {
            vänta([] { return false; }, 100, [&](bool) {
                glaze::AppWindow* fd = dödFält->appWindow();
                check(fd && fd != &w && !fd->isHidden(),
                      "FT8: (förutsättning) ett synligt flytande fönster");
                if (!fd || fd == &w) {
                    nästa();
                    return;
                }
                // Punkterna räknas medan fältet lever — rubriken är kvar i det
                // tomma fönstret, i samma storlek (D3D11).
                std::vector<gfx::Pointf> klick;
                bool                     posta = false;
#ifdef GLAZE_BACKEND_D3D11
                const HWND h = hwndFör(tD);
                check(h != nullptr,
                      "FT8: (fixturen) fönstret hittas med sin titel");
                posta          = h != nullptr;
                const float sk = fd->window().uiScale();
                const float hh = fd->style().dockHeaderH * sk;
                const float kx =
                    std::floor(fd->window().contentArea().w - hh / 2.0f);
                const float rx = std::floor(10.0f * sk);
                const float ry = std::floor(hh / 2.0f);
                klick = {{kx, ry}, {rx, ry}, {rx, ry}};   // ✕, dubbelklick
#endif
                const std::size_t n     = antalFönster();
                const auto        efter = std::make_shared<std::string>();
                w.logg.clear();
                postaLuckKontroll("FT8", fd, posta,
                                  posta ? klick.back() : gfx::Pointf{},
                                  [&w, efter] {
                                      return w.logg.empty() &&
                                             w.host.layout() == *efter;
                                  });
                dödBarn->focus();
                dödBarn.reset();   // det lånade barnet dör först …
                dödFält.reset();   // … därefter fältet
                fd->window().renderFrame();
                *efter = w.host.layout();
                check(fd->isHidden() &&
                          efter->find("dod") == std::string::npos,
                      "FT8: fönstret släppte det döda fältet och döljs i samma "
                      "ögonblick — layout() nämner det inte",
                      *efter);
#ifdef GLAZE_BACKEND_D3D11
                if (h)
                    postaLuckhändelser(h, klick);
#endif
                vänta([&, n] { return antalFönster() == n - 1; }, 2000,
                      [&, n, fd](bool) {
                    check(antalFönster() == n - 1 && !registrerat(fd),
                          "FT8: …och synken river det tomma fönstret efteråt",
                          std::to_string(antalFönster()));
                    nästa();
                });
            });
        });
    });

    // ---- FT12. VÄRDEN RIVS FÖRST medan ett fält flyter: fältet lossas utan
    // barnvandring, dess lånade barn står orört och utan fönster, och
    // fönstret rivs.
    auto              värdX = std::make_unique<glaze::DockHost>();
    glaze::ToolBar    fältX;
    glaze::ToolButton barnX{"X"};
    steg.push_back([&] {
        värdX->name = "vardX";
        fältX.name  = "faltX";
        w.root().adopt(*värdX);
        fältX.adopt(barnX);
        värdX->adopt(fältX);
        fältX.setFloating(true);
        vänta([&] { return fältX.parent() != värdX.get(); }, 2000, [&](bool) {
            const std::size_t n = antalFönster();
            check(fältX.parent() != värdX.get() && fältX.appWindow() != &w,
                  "FT12: (förutsättning) fältet flyter");
            w.root().unadopt(*värdX);
            värdX.reset();
            check(fältX.parent() == nullptr && fältX.appWindow() == nullptr &&
                      barnX.parent() == &fältX && barnX.appWindow() == nullptr &&
                      antalFönster() == n - 1,
                  "FT12: värden rivs först — fältet lossat utan barnvandring, "
                  "dess lånade barn orört och utan fönster, fönstret rivet",
                  std::to_string(antalFönster()));
            barnX.setText("efter");
            check(barnX.text() == "efter",
                  "FT12: …och en sättare på barnet går igenom");
            w.close();
        });
    });

    nästa();
    w.run();
    if (!kordes) {
        std::printf("  --   de flytande verktygsfälten hoppades över: inget "
                    "fönster kunde skapas (ingen display)\n");
        ++g_skips;
    }
}

// ============================================ TE — VERKTYGSFÄLTETS KANTER
//
// `ToolBar::setAllowedEdges` genom greppdraget i
// BandVard (vägen utan bärande: `setFloatable(false)`, så draget visar en
// platshållare i stället för att lossa fältet):
//   TE1 {Top, Bottom}: släppt i vänsterkantens zon ⇒ ingenting ändras;
//       släppt i nederkantens ⇒ Bottom
//   TE2 omordning i Top-bandet (en tillåten kant) fungerar
//   TE4 appen kan alltid: `dock = Left` ⇒ vid vänsterkanten
//   TE5 {} ⇒ inget mål någonstans
//   TE6 `Fill`/`None` i listan ignoreras; läsaren i ordningen Left, Top,
//       Right, Bottom
void fallKanter()
{
    BandVard w;
    w.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes        = true;
        const float Z = w.style().dockDropZone;
        const float H = w.window().contentArea().h / w.s();
        w.t3.setFloatable(false);
        w.t3.setAllowedEdges({glaze::Dock::Bottom, glaze::Dock::Top});
        check(w.t3.allowedEdges() ==
                      std::vector<glaze::Dock>{glaze::Dock::Top,
                                               glaze::Dock::Bottom} &&
                  !w.t3.allowsEdge(glaze::Dock::Left),
              "TE (fixturen) {Top, Bottom} i läsarens ordning");

        // TE2 — omordning i Top-bandet: t3 före t1
        const gfx::Rectf r1 = w.rekt(w.t1);
        w.dra(w.grepp(w.t3), {r1.x + r1.w / 4.0f, r1.y + r1.h / 2.0f});
        check(w.host.layout().rfind("v2;t=t3,top,0,0,0", 0) == 0,
              "TE2 omordningen i ett band vid en tillåten kant", w.host.layout());

        // TE1 — vänsterkanten är inget mål, nederkanten är det
        const std::string före = w.host.layout();
        w.dra(w.grepp(w.t3), w.P(Z / 2, 300));
        check(w.t3.dock == glaze::Dock::Top && w.host.layout() == före,
              "TE1 en otillåten kant är inget mål", w.host.layout());
        w.dra(w.grepp(w.t3), w.P(300, H - Z / 2));
        check(w.t3.dock == glaze::Dock::Bottom,
              "TE1 nederkanten är tillåten", w.host.layout());

        // TE4 — appen kan alltid
        w.t3.dock = glaze::Dock::Left;
        w.t3.invalidate();
        w.host.invalidate();
        check(w.host.layout().find("t=t3,left") != std::string::npos,
              "TE4 appens dock gäller vid en otillåten kant", w.host.layout());

        // TE5 — {} ⇒ inget mål
        w.t3.setAllowedEdges({});
        const std::string före5 = w.host.layout();
        w.dra(w.grepp(w.t3), w.P(300, H - Z / 2));
        w.dra(w.grepp(w.t3), w.P(300, Z / 2));
        check(w.host.layout() == före5, "TE5 {} ⇒ inget mål någonstans",
              w.host.layout());

        // TE6 — Fill och None ignoreras
        w.t3.setAllowedEdges(
            {glaze::Dock::Fill, glaze::Dock::Right, glaze::Dock::None});
        check(w.t3.allowedEdges() == std::vector<glaze::Dock>{glaze::Dock::Right},
              "TE6 Fill och None ignoreras");
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   TE: inget fönster kunde skapas (ingen display)\n");
        ++g_skips;
    }
}

// Ett överhoppat block, sagt med en rad (filens form).
void hoppa(const char* vad)
{
    std::printf("  --   %s\n", vad);
    ++g_skips;
}

#ifdef GLAZE_BACKEND_D3D11
// ========================= FÄLTET ÖVER PANELEN
//
//   FT13 ett flytande verktygsfält och en flytande panel som ÖVERLAPPAR, båda
//        i egna SYNLIGA fönster: panelens fönster AKTIVERAS (`raise()`) och
//        står då över fältets — förutsättningen, för ett klick i en panel gör
//        just det. Därefter BÄRS fältet (fall B, ett postat rubrikdrag som
//        fönstret följer): efter tröskeln, mitt i draget och efter släppet
//        står fältets fönster ÖVER panelens (Z-ordningen, `GW_HWNDPREV`-kedjan
//        uppåt), och förgrunden är hela tiden PANELENS fönster — fältets lyft
//        aktiverar ingenting (`raise()` på ett noActivate-fönster).
//
// Villkorat på att huvudfönstret och därefter panelens fönster kunde göras till
// förgrunden (en session utan skrivbord nekar den) — annars hoppas fallet över
// med en rad. SABOTAGEN den ska fälla: lyftet bort ur `DockHost::carryMove`
// (fältet står kvar under panelen efter tröskeln) och D3D11:s `raise()` med
// `SetForegroundWindow` också för ett noActivate-fönster (förgrunden byter
// till fältets fönster). Mäter INTE: X11-fönsterhanterarens omstapling, Metal,
// Waylands degradering, att fältet SYNS överst med en riktig mus, eller en
// panel som dras (den lyfts med flit inte under draget).

// Står `övre` över `undre` i Z-ordningen? Kedjan UPPÅT från `undre`
// (`GW_HWNDPREV` är fönstret närmast ovanför).
bool överI(HWND övre, HWND undre)
{
    for (HWND h = GetWindow(undre, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV))
        if (h == övre)
            return true;
    return false;
}

struct ÖverRigg : glaze::AppWindow {
    glaze::DockHost   host;
    glaze::ToolBar    fält;
    glaze::ToolButton knapp{"AB"};
    glaze::DockPanel  panel;
    glaze::Panel      innehåll;

    ÖverRigg(const std::string& tF, const std::string& tP)
        : glaze::AppWindow("dock_gate över", 480, 320)
    {
        host.name  = "host";
        host.dock  = glaze::Dock::Fill;
        fält.name  = "falt";
        panel.name = "panel";
        fält.setText(tF);   // de flytande fönstrens titlar (FindWindowW)
        panel.setText(tP);
        root().adopt(host);
        host.adopt(fält);
        fält.adopt(knapp);
        host.adopt(panel);
        panel.adopt(innehåll);
    }
};

void fallFältÖverPanel()
{
    std::printf("\n=== FÄLTET ÖVER PANELEN ===\n");
    const std::string tF = unikTitel("overfalt");
    const std::string tP = unikTitel("overpanel");
    const std::string tW = unikTitel("over");
    ÖverRigg          w(tF, tP);
    w.setTitle(tW);
    w.setLocation(60, 60);
    bool kordes = false;

    glaze::app().post([&] {
        kordes = true;
        if (!glaze::DockHost::isFloatDragSupported()) {
            hoppa("FT13: det levande draget finns inte här");
            w.close();
            return;
        }
        w.fält.setFloating(true);
        w.panel.setFloating(true);
        auto synliga = [&w] {
            glaze::AppWindow* ff = w.fält.appWindow();
            glaze::AppWindow* pf = w.panel.appWindow();
            return ff && ff != &w && pf && pf != &w && !ff->isHidden() &&
                   !pf->isHidden();
        };
        vänta(synliga, 2000, [&, synliga](bool) {
            glaze::AppWindow* const ff = w.fält.appWindow();
            glaze::AppWindow* const pf = w.panel.appWindow();
            const HWND              hf = hwndFör(tF);
            const HWND              hp = hwndFör(tP);
            const HWND              hw = hwndFör(tW);
            check(synliga() && hf && hp && hw,
                  "FT13: (förutsättning) fältet och panelen flyter i egna "
                  "synliga fönster");
            if (!synliga() || !hf || !hp || !hw) {
                w.close();
                return;
            }
            // ÖVERLAPPET, till höger om huvudfönstret (inga dockningszoner där):
            // panelens fönster, och fältets över dess övre vänstra del.
            const gfx::Pointf o = w.window().clientToScreen(
                w.window().contentArea().w + 40.0f, 0.0f);
            pf->window().setPosition(static_cast<int>(std::lround(o.x)),
                                     static_cast<int>(std::lround(o.y)));
            ff->window().setPosition(static_cast<int>(std::lround(o.x + 20.0f)),
                                     static_cast<int>(std::lround(o.y + 20.0f)));
            if (!görTillFörgrund(hw)) {
                hoppa("FT13: huvudfönstret kunde inte göras till förgrunden "
                      "(ingen förgrund i den här sessionen) — fallet hoppas över");
                w.close();
                return;
            }
            pf->window().raise();   // ett klick i panelen gör just det
            vänta([] { return true; }, 1000, [&, ff, hf, hp, hw](bool) {
                check(GetForegroundWindow() == hp && överI(hp, hf),
                      "FT13: (förutsättning) panelens fönster är aktiverat och "
                      "står över fältets");
                if (GetForegroundWindow() != hp) {
                    w.close();
                    return;
                }
                const float sk = ff->window().uiScale();
                PostDrag    d;
                d.källaH  = hf;
                d.källa   = ff;
                d.värdH   = hw;
                d.värd    = &w;
                // TRYCKET I RUBRIKENS DRAGYTA, inte på ✕: fältet har EN knapp,
                // så fönstret är bara `dockHeaderPadX + dockHeaderH` brett
                // (`beginCarry`s mått) och stängknappen — en kvadrat längst
                // till höger — börjar vid `dockHeaderPadX`. Ett tryck 10
                // logiska px in tog ✕ och blev aldrig ett drag; mitt i
                // luften till vänster om titeln är rubriken vid varje bredd.
                d.ner = ff->window().clientToScreen(
                    ff->style().dockHeaderPadX * sk / 2.0f,
                    ff->style().dockHeaderH * sk / 2.0f);
                d.mål     = {d.ner.x + 60.0f * sk, d.ner.y + 40.0f * sk};
                d.tröskel = 30.0f * sk;
                // Värden tar fältets tryck vid lossningen när appen äger
                // förgrunden — och panelens fönster är appens.
                d.övertag = förgrundÄrVår();
                postatDrag(
                    d,
                    [hf, hp](bool) {
                        check(överI(hf, hp),
                              "FT13: efter tröskeln står fältets fönster ÖVER "
                              "panelens — lyftet vid lossningen");
                        check(GetForegroundWindow() == hp,
                              "FT13: …och förgrunden är fortfarande panelens "
                              "fönster — lyftet aktiverade inte fältets");
                    },
                    [hf, hp] {
                        check(överI(hf, hp) && GetForegroundWindow() == hp,
                              "FT13: mitt i draget — fältet överst, förgrunden "
                              "orörd");
                    },
                    [&, hf, hp] {
                        check(w.fält.isFloating() && överI(hf, hp) &&
                                  GetForegroundWindow() == hp,
                              "FT13: efter släppet flyter fältet ÖVERST, och "
                              "förgrunden är orörd");
                        w.close();
                    });
            });
        });
    });
    w.run();
    if (!kordes)
        hoppa("FT13: inget fönster kunde skapas (ingen display)");
}
#endif

// ---- PR1–PR2. PRICKARNA: en panel dockad till höger och en INAKTIV
// flik i dess grupp, som får en tom rekt i värdens hörn. Hörnet är huvudytans
// tomma bakgrund och ska vara enfärgat — annars står rubrikens ▾, nål och ✕
// där, alla tre i samma kvadrat (`slotRect` klampar till rektens vänsterkant).
void fallPrickar()
{
    struct Rigg : glaze::AppWindow {
        glaze::DockHost  host;
        glaze::DockPanel a{"A"}, b{"B"};
        Rigg() : glaze::AppWindow("dock_gate prickar", 480, 320, gfx::Chrome::None)
        {
            host.dock = glaze::Dock::Fill;
            a.name    = "a";
            a.dock    = glaze::Dock::Right;
            a.width   = 150;
            b.name    = "b";
            b.dock    = glaze::Dock::Right;
            b.width   = 150;
            root().adopt(host);
            host.adopt(a);
            host.adopt(b);
        }
        // Pixlar i hörnets kvadrat (rubrikens höjd) som avviker med mer än 2
        // per kanal från huvudytans pixel tre kvadrater in. −1 utan bild.
        int avvikande()
        {
            window().renderFrame();
            std::vector<std::uint8_t> rgba;
            int                       W = 0, H = 0;
            if (!window().captureFrame(rgba, W, H))
                return -1;
            const gfx::Rectf r = host.resolve(host.behaviorCtx());
            const int h = static_cast<int>(style().dockHeaderH * window().uiScale());
            auto at = [&](int x, int y) {
                return &rgba[(std::size_t(y) * std::size_t(W) + std::size_t(x)) * 4u];
            };
            const std::uint8_t* bas = at(int(r.x) + 3 * h, int(r.y) + 3 * h);
            int n = 0;
            for (int y = int(r.y); y < std::min(H, int(r.y) + h); ++y)
                for (int x = int(r.x); x < std::min(W, int(r.x) + h); ++x)
                    for (int k = 0; k < 3; ++k)
                        if (std::abs(int(at(x, y)[k]) - int(bas[k])) > 2) {
                            ++n;
                            break;
                        }
            return n;
        }
    };
    Rigg w;
    w.setOffscreen(true);
    glaze::Style st = w.style();
    st.dockAnimMs   = 0;
    w.setStyle(st);
    glaze::profiler().setEnabled(true);   // PR2: var bilden partiell?
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        w.host.dockPanel(w.b, w.a, glaze::Dock::Fill);   // b aktiv, a inaktiv
        const gfx::Rectf ra = w.a.resolve(w.a.behaviorCtx());
        check(ra.w <= 0.0f && ra.h <= 0.0f && w.a.isVisible(),
              "PR: (fixturen) den inaktiva fliken är synlig med tom rekt",
              rekt(ra));
        w.invalidate();
        const int full = w.avvikande();
        w.a.invalidate();   // skadan kring hörnet — en partiell bild
        const int partiell = w.avvikande();
        const std::vector<glaze::FrameProfile> f = glaze::profiler().frames(w, 1);
        const bool var = !f.empty() &&
            f.back().count[std::size_t(glaze::ProfileCount::FullDamage)] == 0;
        if (full < 0 || partiell < 0) {
            hoppa("PR1–PR2: pixlarna kunde inte läsas (captureFrame)");
        } else {
            check(full == 0,
                  "PR1: värdens hörn är enfärgat — den inaktiva flikens "
                  "rubrikknappar ritas inte (full bild)",
                  std::to_string(full) + " px");
            if (var)
                check(partiell == 0, "PR2: …och inte i en partiell bild",
                      std::to_string(partiell) + " px");
            else
                hoppa("PR2: bilden blev inte partiell");
        }
        w.close();
    });
    w.run();
    glaze::profiler().setEnabled(false);
    if (!kordes)
        hoppa("PR1–PR2: inget fönster kunde skapas (ingen display)");
}

// ---- IF. EN VÄRD I EN INAKTIV FLIK MED EN VÄNTANDE FLYTT:
// den inre värden står i en yttre värds
// INAKTIVA flik (synlig, med tom rekt — en flikkontroll hade dolt sidan) och
// en panel i den som ska flyta väntar på en öppen popup. När popupen stängts
// och fönstret ritas görs flytten — bildens steg (`frameStep`) körs också när
// värden själv inte ritas.
void fallInaktivFlik()
{
    struct Rigg : glaze::AppWindow {
        glaze::DockHost  yttre, host;
        glaze::DockPanel x{"X"}, y{"Y"}, p{"Lös"};
        PopupYta         popup;
        glaze::Panel     mitt;
        Rigg() : glaze::AppWindow("dock_gate inaktiv flik", 480, 320)
        {
            yttre.dock = glaze::Dock::Fill;
            for (glaze::DockPanel* q : {&x, &y}) {
                q->dock  = glaze::Dock::Right;
                q->width = 200;
            }
            x.name     = "x";
            y.name     = "y";
            host.dock  = glaze::Dock::Fill;
            p.name     = "los";
            p.dock     = glaze::Dock::Right;
            p.width    = 80;
            popup.dock = glaze::Dock::Fill;
            root().adopt(yttre);
            yttre.adopt(x);
            yttre.adopt(y);
            x.adopt(host);
            host.adopt(p);
            host.adopt(mitt);
            p.adopt(popup);
        }
    };
    Rigg w;
    w.setLocation(60, 60);
    w.hide();   // öppet och dolt — en offscreen-värd flyttar inget
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        w.yttre.dockPanel(w.y, w.x, glaze::Dock::Fill);   // y aktiv, x inaktiv
        const gfx::Rectf rh = w.host.resolve(w.host.behaviorCtx());
        check((rh.w <= 0.0f || rh.h <= 0.0f) && w.host.isVisible(),
              "IF: (fixturen) värden i den inaktiva fliken är synlig med tom "
              "rekt",
              rekt(rh));
        w.popup.öppen = true;
        w.p.setFloating(true);
        w.invalidate();
        w.window().renderFrame();   // steget postar synken — som nekas
        vänta([] { return false; }, 150, [&](bool) {
            check(w.p.parent() == &w.host,
                  "IF: (kontroll) en öppen popup i panelen NEKAR flytten");
            w.popup.öppen = false;
            // En full bild — värden själv ritas ändå inte. Direkt: pumpen
            // ritar inte ett dolt fönster (`wantsFrame`).
            w.invalidate();
            w.window().renderFrame();
            vänta([&] { return w.p.parent() != &w.host; }, 2000, [&](bool) {
                check(w.p.parent() != &w.host && w.p.appWindow() != &w,
                      "IF: hindret borta — nästa bild gjorde flytten fast "
                      "värden står i en inaktiv flik");
                // IF2. Samma sak med en PARTIELL bild: bara popupen
                // invalideras (dess skada klipps bort av den tomma fliken),
                // ingen full bild begärs — steget körs ändå (`stepTree`).
                w.p.setFloating(false);
                vänta([&] { return w.p.parent() == &w.host; }, 2000, [&](bool) {
                    w.popup.öppen = true;
                    w.p.setFloating(true);
                    w.window().renderFrame();
                    vänta([] { return false; }, 150, [&](bool) {
                        check(w.p.parent() == &w.host,
                              "IF2: (kontroll) hemma igen, och popupen NEKAR "
                              "flytten");
                        w.popup.öppen = false;
                        w.popup.invalidate();
                        w.window().renderFrame();
                        vänta([&] { return w.p.parent() != &w.host; }, 2000,
                              [&](bool) {
                            check(w.p.parent() != &w.host,
                                  "IF2: en bild där bara popupen invaliderats "
                                  "gör flytten — steget före skadan");
                            w.close();
                        });
                    });
                });
            });
        });
    });
    w.run();
    if (!kordes)
        hoppa("IF: inget fönster kunde skapas (ingen display)");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
            g_pngDir = argv[++i];
    }

    std::printf("dock_gate\n");
    fallLayout();
    fallBand();
    fallStrangen();
    fallMedFonster();
    fallOverlay();
    fallBandFonster();
    fallFlytande();
#ifdef GLAZE_BACKEND_D3D11
    fallFlytTomVard();
#endif
    fallFlytOffscreen();
    fallFlytTreNivaer();
    fallFlytRivning();
    fallReservvagen();
    fallFlytFalt();
    fallKanter();   // TE
#ifdef GLAZE_BACKEND_D3D11
    fallFältÖverPanel();   // FT13
#endif
    fallPrickar();       // PR1–PR2
    fallInaktivFlik();   // IF

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
