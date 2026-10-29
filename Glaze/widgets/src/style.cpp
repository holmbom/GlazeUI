// Mått-tabellerna. HÄR bor varje tal som styr utseendet — widget-kod och
// Draw är fria från sifferkonstanter (se glaze/style.h).

#include "glaze/style.h"

#include <gfx/text.h>

namespace glaze {
namespace {

// Slate::Regular — BASEN. Mjukt rundade kontroller, tunn ram, diskret
// upphöjning. Widgetarnas `default:`-gren ritar Slate, så allt som inte
// uttryckligen avviker ser ut så här.
//
// VARJE UI ÄGER SINA EGNA styles (se noten i style.h): den här heter
// "Regular" och så gör de två andra också. Att de tre Regular är olika
// stora är i sin ordning — de tillhör olika ritningar och jämförs aldrig.
// Att de två andra tabellerna DELTAR från den här är en implementationsdetalj
// som håller dem synkade vid nya fält, inte ett släktskap i modellen.
constexpr Style kSlateRegular{
    /* name           */ "Regular",   // lang_gate:rad stilens API-namn (styleAt)
    /* uiFontSize     */ gfx::fontsize::kUi,   // fasadens konstant = basen
    /* headingFontSize*/ gfx::fontsize::kUiHeading,
    /* cornerRadius   */ 4.0f,
    /* borderWidth    */ 0.0f,   // hårlinje — se fältets doc i style.h
    /* focusOffset    */ 3.0f,
    /* focusWidth     */ 2.0f,

    /* sheenLower     */ 0.0f,   // Slate lamineras inte — se style.h
    /* sheenUpper     */ 0.0f,

    /* raiseDy        */ 1.0f,
    /* raiseBlur      */ 2.0f,
    /* raiseBlurHover */ 3.0f,

    /* buttonRadius   */ 4.0f,   // = cornerRadius; Slate skiljer dem inte
    /* buttonPadX     */ 12.0f,
    /* buttonPadY     */ 6.0f,
    /* buttonShadowDy   */ 0.0f,   // Slate lyfter med raise* i stället
    /* buttonShadowBlur */ 0.0f,
    /* buttonPressShift */ 0.0f,   // Slate sjunker inte vid tryck
    /* iconGap        */ 6.0f,
    /* iconFallback   */ 16.0f,

    /* checkSize      */ 18.0f,
    /* checkRadius    */ 3.0f,
    /* checkGap       */ 8.0f,
    /* checkStroke    */ 1.0f,

    /* fieldPadX      */ 8.0f,
    /* fieldPadY      */ 6.0f,
    /* fieldDefaultW  */ 120.0f,
    /* caretWidth     */ 1.0f,
    /* caretMargin    */ 2.0f,

    /* listRowH       */ 26.0f,
    /* listPadX       */ 8.0f,
    /* chevronW       */ 4.5f,
    /* chevronH       */ 2.5f,
    /* dropMaxRows    */ 10,

    /* scrollThick    */ 10.0f,
    /* scrollPad      */ 2.0f,
    /* scrollThumbMin */ 24.0f,
    /* scrollDefaultLen */ 200.0f,   // se *DefaultLen-noten nedanför tabellen
    /* wheelStep      */ 40.0f,

    // ⚠ POSITIONELL LITERAL — kSlateRegular är den ENDA i filen (Glaze och
    // Minimalistic constexpr-kopierar och muterar PER NAMN och är därför
    // säkra). En rad på fel plats KOMPILERAR och förskjuter allt efter sig
    // tyst. `panelRadius` står här därför att den står FÖRST i sin grupp i
    // style.h — ordningen måste vara densamma i båda filerna.
    /* panelRadius    */ 0.0f,   // fyrkantigt; utseendepasset äger värdet
    /* panelInset     */ 1.0f,
    /* gridRowH       */ 26.0f,
    /* gridNameW      */ 110.0f,
    /* gridPadX       */ 8.0f,
    /* rowMarkerW     */ 2.0f,   // ritas bara av Minimalistic — se style.h

    // HÄRLEDD, inte vald: kapselns ände är en halvcirkel med radien
    // capsuleH/2 = 13, och den ska ha SAMMA luft mot fönsterhörnet som mot
    // barens över- och underkant, dvs. (barHeight − capsuleH)/2 = 3. Då är
    // fönsterhörnets radie 13 + 3 = 16 — och eftersom 13 + 3 också är
    // barHeight/2 hamnar de två bågarna KONCENTRISKT: exakt 3 px luft hela
    // vägen runt kapselns ände. Ändras barHeight eller capsuleH måste den
    // här raden räknas om, annars glider bågarna isär.
    /* windowRadius   */ 16.0f,

    /* barHeight      */ 32.0f,
    /* statusBarH     */ 24.0f,
    /* captionBtnW    */ 46.0f,
    /* captionGlyph   */ 5.0f,
    /* captionStroke  */ 0.0f,   // hårlinje — backend-chromets utseende
    /* restoreOffset  */ 2.0f,
    /* trafficSize    */ 14.0f,
    /* trafficGlyph   */ 4.2f,
    /* trafficStroke  */ 1.5f,
    /* capsuleH       */ 26.0f,   // efter GNOME:s kapsel
    /* capsuleSeg     */ 34.0f,
    /* capsuleInset   */ 2.0f,
    /* capsuleStroke  */ 1.2f,

    /* separatorThickness */ 0.0f,   // hårlinje — den klassiska skiljelinjen
    /* separatorInset     */ 4.0f,
    /* separatorDefaultLen */ 200.0f,

    /* linkUnderline      */ 0.0f,   // hårlinje — som webbens standardlänk
    /* linkGap            */ 1.0f,

    /* radioSize          */ 18.0f,   // samma ruta som checkboxen
    /* radioDot           */ 8.0f,
    /* radioGap           */ 8.0f,
    /* radioStroke        */ 1.0f,

    /* sliderTrack        */ 4.0f,
    /* sliderThumb        */ 16.0f,
    /* sliderPad          */ 8.0f,
    /* sliderDefaultLen   */ 200.0f,
    /* sliderShadowDy     */ 0.0f,   // Slates tumme är platt (skrivet
    /* sliderShadowBlur   */ 0.0f,   // undantag från princip 5: bara Button
                                     // lyfter i Slate)

    /* progressHeight     */ 8.0f,
    /* progressDefaultLen */ 200.0f,
    /* progressRadius     */ 4.0f,
    /* progressChunk      */ 0.3f,
    /* progressPeriodMs   */ 1600,

    /* overdraw       */ 6.0f,

    // ---- Meddelanderutan. SIST i tabellen, precis som
    // fälten är sist i structen.
    /* msgIconSize    */ 32.0f,
    /* msgPad         */ 16.0f,
    /* msgGap         */ 12.0f,
    /* msgTextMaxW    */ 360.0f,   // konservativ med flit: Wayland har
                                   // ingen skärmrymd att klampa mot
    /* msgBtnGap      */ 8.0f,
    /* msgBtnMinW     */ 88.0f,

    // ---- Menyerna. SIST i tabellen, precis som fälten är
    // sist i structen. Talen är menyns egna — se noten i style.h om varför
    // de inte lånar `listRowH`/`listPadX`.
    /* menuRowH        */ 24.0f,
    /* menuPadX        */ 10.0f,
    /* menuSepH        */ 7.0f,    // 1 px streck + luft över och under
    /* menuShortcutGap */ 24.0f,   // genvägen ska LÄSAS som en egen kolumn
    /* menuMinW        */ 140.0f,
    /* menuBarPadX     */ 10.0f,
    /* menuArrowW      */ 3.5f,
    /* menuMaxRows     */ 20,

    // ---- Verktygsfältet. SIST i tabellen, precis som fälten är
    // sist i structen.
    //
    // HÄRLEDDA i par, inte valda var för sig: knappens naturliga höjd är
    // max(ikon 16, radhöjd 14.72) + 2 × toolButtonPadY = 16 + 8 = 24, och
    // remsan är den plus luften över och under: 24 + 2 × 3 = 30. Remsan
    // ger sina barn sin INNERhöjd, så en knapp i remsan och en knapp som
    // står fritt blir lika höga. Ändras det ena talet måste det andra
    // räknas om — annars blir knappen i remsan en annan än den fria.
    /* toolBarH        */ 30.0f,
    /* toolBarPadX     */ 4.0f,
    /* toolBarPadY     */ 3.0f,
    /* toolGap         */ 2.0f,
    /* toolButtonPadX  */ 6.0f,    // ikon-knappen blir 28 × 24 — nästan
    /* toolButtonPadY  */ 4.0f,    // kvadratisk, som verktygsfältens brukar
    /* iconFade        */ 0.55f,   // hälften och lite till: ikonen ska
                                   // fortfarande gå att KÄNNA IGEN

    // ---- Gruppramen. SIST i tabellen, precis som fälten.
    /* groupRadius      */ 4.0f,   // = cornerRadius: ramen är en kontroll-
                                   // kant, inte ett fönsterhörn
    /* groupPad         */ 8.0f,
    /* groupTitleIndent */ 8.0f,   // ≥ groupRadius — bågen ska vara slut
                                   // innan luckan börjar
    /* groupTitleGap    */ 4.0f,

    // ---- Talfältet. SIST i tabellen, precis som fälten.
    /* spinStepW        */ 18.0f,  // två 13 px höga halvor i ett 26.7 px
                                   // fält — som en NSStepper, inte smalare
    /* spinGap          */ 4.0f,
    /* spinDefaultW     */ 96.0f,  // fält 74 + 4 + 18: ett femsiffrigt tal
                                   // med decimaler ryms utan scroll

    // ---- Drag-and-drop. SIST i tabellen, precis som fälten.
    /* dragThreshold    */ 4.0f,   // Windows SM_CXDRAG — ett darrigt klick
                                   // ska förbli ett klick
    /* dragOffset       */ 16.0f,  // under pilpekarens kropp, inte på den
    /* dragBadgeSize    */ 14.0f,
    /* dragGhostPadX    */ 8.0f,
    /* dragGhostPadY    */ 4.0f,   // 14.72 + 2 × 4 ≈ 22.7: en knapp minus
                                   // luften — bilden är en ETIKETT, ingen knapp
    /* dragGhostMaxW    */ 240.0f,
    /* dropLineW        */ 2.0f,   // = fokusringen: en markering som ska
                                   // synas mot vilken rad som helst

    // ---- Trädet. SIST i tabellen, precis som fälten.
    /* treeIndent       */ 16.0f,  // = expanderns kolumn: ett barns pil står
                                   // rakt under förälderns text, så nivåerna
                                   // läser som en trappa och inte som en glipa
    /* treeExpanderW    */ 16.0f,  // = `iconFallback`: pilen och en ikon tar
                                   // lika stor plats i raden
    /* treeDropEdge     */ 0.25f,  // en kvart i var kant, halva raden IN I —
                                   // Utforskarens fördelning: det vanligaste
                                   // släppet (in i en mapp) får störst yta

    // ---- Tabellen. SIST i tabellen, precis som fälten.
    /* tableHeaderH     */ 26.0f,  // = listRowH: huvudet är en rad bland
                                   // raderna, bara av ett annat slag
    /* columnDefaultW   */ 100.0f, // ett ord eller ett tal ryms; längre
                                   // text kortas med ellips (Draw::elide)
    /* columnMinW       */ 24.0f,  // pilen och en bokstav — en smalare
                                   // kolumn går inte att greppa igen
    /* columnGrip       */ 4.0f,   // = dragThreshold: samma hand, samma
                                   // darrning

    // ---- Skiktet. SIST i tabellen, precis som fälten.
    /* splitterThick    */ 6.0f,   // greppytan — smalare än så träffas inte
                                   // utan att man siktar
    /* splitterGripLen  */ 0.0f,   // Slate ritar bara linjen

    // ---- Den dockningsbara panelen. SIST i tabellen.
    /* dockHeaderH      */ 24.0f,  // radhöjden 14.72 + ~4.6 luft per sida —
                                   // lägre än en knapp (26.7): raden är en
                                   // RUBRIK, inte en kontroll
    /* dockHeaderPadX   */ 8.0f,   // = fieldPadX: texten står där en rads
                                   // text står
    /* dockGlyph        */ 8.0f,   // ett snäpp under dropdownpilens ruta
                                   // (2 × chevronW = 9): två tecken i rad
                                   // bredvid en rubrik ska inte ta över den
    /* dockDefaultSize  */ 200.0f, // = remsornas `*DefaultLen` (se noten)
    /* dockDropZone     */ 48.0f,  // två knapphöjder: lätt att träffa, men
                                   // mitten av ytan förblir "ingenstans"

    // ---- Tooltipen. SIST i tabellen, precis som fälten.
    /* toolTipPadX      */ 8.0f,   // = dragGhostPadX: samma sorts flytande
    /* toolTipPadY      */ 4.0f,   //   etikett som dragbilden, samma luft
    /* toolTipMaxW      */ 320.0f, // ~45 tecken: en förklaring, inte en text
    /* toolTipOffsetY   */ 20.0f,  // pilpekarens höjd — Windows lägger
                                   // tooltipen just under den
    /* toolTipDelayMs   */ 500,    // Windows TTDT_INITIAL (= dubbelklicks-
                                   // tiden): kortare och den dyker upp när
                                   // man bara passerar, längre och man har
                                   // redan gett upp
    /* toolTipReshowMs  */ 100,    // Windows TTDT_RESHOW

    // ---- Den dockningsbara panelen, forts.. SIST i tabellen.
    /* dockButtonInset  */ 3.0f,   // plattan 18 × 18 i en 24-rad: samma luft
                                   // som en verktygsknapp har i sin remsa

    // ---- Tooltipen, forts.. SIST i tabellen.
    /* toolTipOffsetUp  */ 4.0f,   // under: pilens hela kropp (20) ska gå
                                   // fri; över: bara spetsen, så en liten
                                   // luft räcker (Qt lämnar ~8 fysiska)

    // ---- Flikarna. SIST i tabellen, precis som fälten.
    /* tabHeaderH       */ 28.0f,  // radhöjden 14.72 + ~6.6 luft per sida —
                                   // ett snäpp över knappen (26.7): fliken
                                   // är navigering, inte en kontroll bland
                                   // andra
    /* tabPadX          */ 12.0f,  // = buttonPadX: rubriken står där en
                                   // knapps text står
    /* tabMinW          */ 48.0f,  // två tecken och ellipsen — smalare går
                                   // inte att läsa eller träffa
    /* tabGap           */ 2.0f,   // flikarna är skilda plattor, men nära
    /* tabRadius        */ 4.0f,   // = cornerRadius: samma hörn som fältet
    /* tabCloseInset    */ 5.0f,   // knappen 18 × 18 i en 28-rad — lika
                                   // stor som dockningsrubrikens platta
    /* tabCloseGlyph    */ 8.0f,   // = dockGlyph: samma ✕ som en panels

    // ---- Filväljaren. SIST i tabellen, precis som fälten.
    /* iconStroke       */ 1.0f,   // en logisk px: hårlinjen vid skala 1,
                                   // två fysiska vid 2 — en ikon på 16 px
                                   // ska inte tunnas ut när skärmen blir
                                   // skarpare (som `checkStroke`)
    /* fileDlgW         */ 760.0f, // fyra kolumner (namn 220 · storlek 80 ·
    /* fileDlgH         */ 460.0f, // ändrad 128 · typ 120) bredvid
                                   // platslistan, och ett tiotal rader
    /* fileDlgMinW      */ 560.0f, // platslistan på sitt minimum, namnet och
    /* fileDlgMinH      */ 340.0f, // storleken hela, knappraden utan krock
    /* fileDlgPad       */ 12.0f,
    /* fileDlgGap       */ 8.0f,   // = checkGap/radioGap: luften mellan en
                                   // etikett och dess kontroll
    /* fileDlgPlacesW   */ 170.0f, // "Hämtade filer" och en volymetikett
                                   // ("Windows (C:)") ryms med ikonen
    /* fileDlgPlacesMin */ 110.0f,
    /* fileDlgListMin   */ 220.0f, // namnkolumnen ska alltid synas
    /* fileDlgFilterW   */ 210.0f, // "Bilder (*.png;*.jpg)" ryms
    /* fileDlgBtnMinW   */ 88.0f,  // = msgBtnMinW: de två dialogernas
                                   // knappar ser likadana ut i dag
    /* fileColNameW     */ 220.0f,
    /* fileColSizeW     */ 80.0f,  // "999 MB" och lite luft
    /* fileColDateW     */ 128.0f, // "2026-10-08 14:32" i sin helhet
    /* fileColTypeW     */ 120.0f, // "Tiedostokansio" (finska) är längst

    // ---- Filväljarfältet. SIST i tabellen.
    /* filePickerGap      */ 4.0f,   // = spinGap: en knapp tätt intill sitt
                                     // fält, som talfältets stegare
    /* filePickerDefaultW */ 240.0f, // två fältbredder (120): en sökväg är
                                     // längre än ett ord

    // ---- Färgväljaren. SIST i tabellen, precis som fälten.
    /* colorPickerDefaultW */ 72.0f, // färgrutan ~46 bred bredvid pilens zon
                                     // (fieldPadX + 2 × chevronW = 17)
    /* colorSwatchInset    */ 4.0f,  // rutan står innanför fältets kant, inte
                                     // på den — en ruta i kanten läser som
                                     // fältets fyllning
    /* colorSwatchRadius   */ 2.0f,  // < cornerRadius: rutan står INNANFÖR
                                     // fältet, och en lika rund ruta i ett
                                     // rundat fält ser ut att flyta
    /* colorPopupPad       */ 8.0f,  // = fileDlgGap: popupen är en liten yta
    /* colorPopupGap       */ 8.0f,
    /* colorCellSize       */ 18.0f, // = checkSize: en ruta man klickar på
                                     // ska vara lika stor som en kryssruta
    /* colorCellGap        */ 4.0f,  // rutornas ringar (focusWidth 2) ryms i
                                     // luften utan att röra grannen
    /* colorSliderMinW     */ 120.0f,// 256 lägen på 120 px: varannan pixel
                                     // ett värde — pilarna tar resten

    // ---- Datumväljaren. SIST i tabellen.
    /* dateDefaultW   */ 124.0f, // "2026-10-08" (≈ 68) + fältets luft (16) +
                                 // dateGap + dateButtonW, och lite marginal
    /* dateButtonW    */ 26.0f,  // = knapphöjden: kalenderknappen är en
                                 // KVADRAT bredvid fältet
    /* dateGap        */ 4.0f,   // = spinGap/filePickerGap
    /* calPad         */ 8.0f,   // = colorPopupPad: samma sorts popup
    /* calHeaderH     */ 28.0f,  // = tabHeaderH: titelraden är navigering
    /* calCellW       */ 32.0f,  // "28" (≈ 15) med luft — sju kolumner ger
    /* calCellH       */ 26.0f,  // ett 240 px brett rutnät; höjden = listRowH
    /* calCellRadius  */ 4.0f,   // = cornerRadius

    // ---- Det flerradiga fältet. SIST i tabellen.
    /* textAreaDefaultW */ 240.0f, // = filePickerDefaultW: två fältbredder
    /* textAreaRows     */ 4,      // ett anteckningsfält visar ett stycke,
                                   // inte en rad — fyra rader är WinForms
                                   // och wx typiska flerradiga ruta

    // ---- Ritytan. SIST i tabellen.
    /* canvasDefaultW  */ 300.0f, // = GlazeBuilders palettmått (300 × 200):
    /* canvasDefaultH  */ 200.0f, // en yta man kan rita en skiss i
    /* canvasGridMin   */ 8.0f,   // tätare än 8 px blir rutnätet en grå yta
                                  // i stället för linjer
    /* canvasGridWidth */ 0.0f,   // hårlinjen: rutnätet är en hjälp, inte
                                  // innehåll, och ska vara lika tunt i varje
                                  // skala

    // ---- Ritytans gester. SIST i tabellen.
    /* canvasHitTolerance */ 4.0f,   // = dragThreshold: samma hand — en
                                     // hårlinje går att träffa utan att
                                     // pricka pixeln
    /* canvasPanStep      */ 100.0f, // MapLibres panStep: en tangent flyttar
                                     // kartan en bit, inte en pixel

    // ---- Programikonen. SIST i tabellen.
    /* appIconInset      */ 8.0f,  // 32 − 2 × 8 = 16: OS:ets lilla ikon i
                                   // namnlisten, 56 − 16 = 40 den stora
    /* appIconPlateInset */ 3.0f,  // = kapselns luft mot barens kant,
                                   // (barHeight − capsuleH) / 2
    /* appIconRadius     */ 6.0f,  // Utseendepasset
                                   // justerar värdet

    // ---- Lösenordsfältet. SIST i tabellen.
    /* passwordMask */ "\xE2\x80\xA2",   // "•" U+2022 — skälet står i style.h;
                                         // Glaze och Minimalistic ärver det

    // ---- Dockningen, forts.. SIST i tabellen, precis som fälten.
    /* dockHandleH     */ 24.0f,  // = dockHeaderH: ett infällt handtag är
                                  // lika högt som rubriken det ersätter
    /* dockHandlePadX  */ 8.0f,   // = dockHeaderPadX: texten står där
                                  // rubrikens text står
    /* dockHandleGap   */ 2.0f,   // = tabGap: handtagen är flikar på kanten
    /* dockSnapZone    */ 12.0f,  // tre dragtrösklar: magnetiskt, men ett
                                  // mått strax bredvid går att välja
    /* dockAnimMs      */ 160,    // en glidning man ser men inte väntar på
    /* dockHideDelayMs */ 400,    // längre än en pekare som snuddar kanten,
                                  // kortare än att man undrar
    /* toolGripW       */ 8.0f,   // två prickrader och luft — går att greppa
    /* toolArrowW      */ 14.0f,  // pilens tecken (7) med luft på var sida
    /* toolArrowGlyph  */ 7.0f,   // ett snäpp under dockGlyph (8): pilen
                                  // står bredvid en ikon och ska inte ta över
    /* dockDropShare   */ 0.33f,  // en tredjedel: en liten cell har
                                  // alltid en mitt att släppa en flik i

    // ---- Färgrutan, datumet och statuspanelen. SIST i tabellen, precis som fälten.
    /* colorCheckerCell */ 5.0f,   // färgrutan (inset 4 i ett ~27 px fält)
                                   // rymmer fyra rader: mönstret LÄSER som
                                   // ett mönster och inte som brus
    /* dateTimeDefaultW */ 168.0f, // "2026-10-08 14:32" (≈ 106) + fältets
                                   // luft (16) + dateGap + dateButtonW, och
                                   // lite marginal — dateDefaultW + en tid
    /* statusPanelPadX  */ 8.0f,   // = dockHeaderPadX: en text i en remsa
                                   // står lika långt från sin kant

    // ---- Autoskrollen och tabbstoppet. SIST i tabellen, precis som fälten.
    /* dragScrollZone    */ 16.0f, // Qt:s `autoScrollMargin`: en rad nära
                                   // kanten, inte halva listan
    /* dragScrollStep    */ 8.0f,  // 8 px per 30 ms ≈ tio rader i sekunden:
    /* dragScrollMs      */ 30,    // jämnt, och går att hinna släppa
    /* textAreaTabSpaces */ 4,     // en editors förval — Glaze och
                                   // Minimalistic ärver det

    // ---- Markdown-vyn. SIST i tabellen, precis som fälten.
    /* subheadingFontSize   */ gfx::fontsize::kUiSubheading, // H2: mellan
                                   // rubriken (20) och brödtexten (13)
    /* markdownDefaultW     */ 300.0f, // = canvasDefaultW: en yta man läser
                                       // ett stycke i, inte en rad
    /* markdownPad          */ 8.0f,   // = fieldPadX: texten står lika långt
                                       // in som i ett fält
    /* markdownBlockGap     */ 10.0f,  // ett halvt blankt radsteg: styckena
                                       // skiljs utan att glesna
    /* markdownHeadingGap   */ 6.0f,   // rubriken hör till texten UNDER sig
    /* markdownListIndent   */ 22.0f,  // punkten och luften — en bred
                                       // siffra ("10.") får växa kolumnen
    /* markdownMarkerGap    */ 6.0f,   // = iconGap: markören är en ikon
    /* markdownBulletSize   */ 5.0f,   // en punkt, inte en knapp
    /* markdownQuoteIndent  */ 14.0f,  // strecket och dess luft
    /* markdownQuoteBarW    */ 3.0f,   // syns som en linje, inte en ram
    /* markdownCodePad      */ 8.0f,   // = fieldPadX: kodytan är ett fält
    /* markdownCodeSpanPadX */ 3.0f,   // ytan syns runt ordet utan att
                                       // raden glesnar
    /* markdownCellPadX     */ 8.0f,   // = listPadX: en cell står in som en
                                       // listrad
    /* markdownCellPadY     */ 4.0f,   // radhöjden 14.7 + 2 × 4 ≈ 23, strax
                                       // under listRowH — ett nät, inte en lista

    // ---- Den fria panelen (FloatingPanel). SIST i tabellen, precis som fälten.
    /* floatingPanelGrip */ 12.0f,  // en fingertopp i hörnet, under raden
    /* floatingPanelMinW */ 120.0f, // rubriken och stängknappen ryms
    /* floatingPanelMinH */ 60.0f,  // rubriken, en rad innehåll och greppet

    // ---- Glorian (Label::setHalo). SIST i tabellen, precis som fältet.
    /* labelHaloWidth */ 1.5f,      // = ritytans `TextStyle::haloWidth`: syns
                                    // på en bild utan att äta glyferna

    // ---- Verktygsfältets överflöde. SIST i tabellen, som fältet.
    /* toolOverflowW */ 20.0f,      // ikonen 16 och 2 på var sida: smalare än
                                    // en ikonknapp (28), så den läser som
                                    // fältets ände och inte som ett verktyg
};

// De fyra `*DefaultLen` är AVSIKTLIGT samma tal (200 logiska px). En remsa
// — separator, slider, scrollbar, progress — har ingen längd som är mer
// "rätt" än de andras; talet är en PLATSHÅLLARE tills den som placerar
// widgeten säger något annat med dock/anchors eller ett satt mått. Fyra
// olika tal hade antytt en precision som inte finns. De hör hemma i
// tabellen (och inte som ett delat fält) just för att en tätare stil ska
// kunna korta dem var för sig — Minimalistic är den självklara platsen.

// Glaze::Regular — GLAZE-ritningens storlek: tydlig men
// modern, generös luft.
//
// Tabellen börjar som Slates och de rader en färdig widget äger skrivs över
// nedan. Resten är Slates mått — för det är Slate de widgetarna RITAR
// (`default:`-grenen). Delta-formen är med flit: en full kopia hade tyst
// glidit isär vid nästa nya fält.
constexpr Style kGlazeRegular = [] {
    Style m = kSlateRegular;

    // ---- Button
    m.buttonRadius   = 6.0f;    // mjuk rektangel, INTE piller: pillret läser
                                // som konsumentapp, 6 som verktyg
    m.buttonPadX     = 12.0f;   // Desktop-mått: tätt nog att det
    m.buttonPadY     = 6.3f;    // ryms knappar. Talet är HÄRLETT ur fonten
                                // och inte gissat: radhöjden vid font 13 är
                                // 14.72 logiska px, så höjden
                                // faller ut som 14.72 + 2 × 6.3 = 27.32.
    m.iconGap        = 8.0f;
    m.buttonShadowDy   = 1.0f;   // kort och låg — knappen vilar PÅ ytan,
    m.buttonShadowBlur = 3.0f;   // den svävar inte över den
    m.buttonPressShift = 1.0f;

    // ---- lamineringen (DELAD)
    m.sheenLower = 0.62f;   // två breda fält, inte en rand: en rand läser
    m.sheenUpper = 0.34f;   // som en söm på 28 px höjd. Samma andelar bär
                            // kryssrutan, radion, tummarna och
                            // spårens fyllda delar — en yta som lamineras
                            // ska se ut som SAMMA material var den än sitter.

    // ---- Slider
    // Tummen är en fristående platta och kastar därför skugga, precis som
    // knappen. EGNA tal och inte buttonShadow*: 16 px tumme mot 27 px knapp.
    // Samma dy/blur råkar passa båda — men de kan justeras var för sig,
    // och det är hela poängen med två fält.
    m.sliderShadowDy   = 1.0f;
    m.sliderShadowBlur = 3.0f;

    // Fokusringen halveras (2 → 1). Ringen är den enda markeringen som
    // ligger UTANFÖR kontrollen och konkurrerar därför med allt runt
    // omkring; i Glaze räcker en hårfin ring eftersom accenten är stark.
    // Alternativet "50 % genomskinlig" valdes BORT — se kommentaren under.
    m.focusWidth     = 1.0f;
    m.raiseDy        = 0.0f;    // raise* är SLATES lyft och används inte
    m.raiseBlur      = 0.0f;    // av Glaze — skuggan ovan ersätter den
    m.raiseBlurHover = 0.0f;

    // ---- Gruppramen — knappens radie och Glazes generösare luft.
    // Rubriken flyttar in i takt med radien, så bågen aldrig når luckan.
    m.groupRadius      = 6.0f;
    m.groupPad         = 10.0f;
    m.groupTitleIndent = 10.0f;

    // ---- Tabellen — Slates tal, och det är ett BESLUT:
    // Glaze ärver `listRowH`/`gridRowH`, och kolumnhuvudet är en rad bland
    // raderna. Greppzonen och minsta bredden är händernas mått, inte
    // familjens.

    // ---- Skiktet — Glaze ritar ett GREPP mitt på linjen: en
    // kort pille i kontrollkanten, "bestämd kontur" på det enda stället som
    // går att ta i. Tjockleken är Slates — greppytan är handens mått.
    m.splitterGripLen = 24.0f;

    // ---- Den dockningsbara panelen — Glazes generösare luft:
    // raden följer knappens 27.3 nedåt med samma marginal som Slates.
    m.dockHeaderH    = 26.0f;
    m.dockHeaderPadX = 10.0f;

    // ---- Flikarna — knappens radie och Glazes generösare luft:
    // raden följer knappens 27.3 uppåt med samma marginal som Slates.
    m.tabHeaderH = 30.0f;
    m.tabPadX    = 14.0f;
    m.tabRadius  = 6.0f;
    m.tabCloseInset = 6.0f;   // knappen 18 × 18 som i Slate — raden är högre

    // ---- Dockningens handtag — rubrikens mått i familjen: ett
    // infällt handtag ersätter rubriken och är lika högt som den.
    m.dockHandleH    = 26.0f;   // = dockHeaderH i familjen
    m.dockHandlePadX = 10.0f;   // = dockHeaderPadX i familjen

    // ---- Filväljaren — Slates tal, och det är ett BESLUT:
    // dialogen är byggd av listvyn, delaren och fälten, som alla redan bär
    // Glazes former och mått; dialogens egna tal är en layout ovanpå dem
    // (meddelanderutans `msg*` gjorde samma val).

    // ---- Färgväljaren — Slates tal utom rutans hörn: Glazes
    // former är rundare (knappen 6 mot 4), och rutan följer med ett steg.
    m.colorSwatchRadius = 3.0f;

    // ---- Datumväljaren — Slates rutnät, knappens radie på
    // dagrutan och flikradens höjd på titelraden (30 i Glaze): båda är
    // navigering ovanpå samma ytor.
    m.calHeaderH    = 30.0f;
    m.calCellRadius = 6.0f;

    return m;
}();

// Minimalistic::Regular — "Outline"-ritningens storlek.
// Desktop-täthet på riktigt: en
// verktygsrad ska rymma tre gånger så mycket. Att den är hälften så hög som
// Slates Regular är inget att jämföra — de tillhör olika ritningar.
//
// Höjden är HÄRLEDD, inte vald: UI-fontens radhöjd är 12.16
// logiska px vid font 10, och 12.16 + 2 × 2.92 = 18.0. Att
// fontstorleken måste med i tabellen är hela skälet till att `uiFontSize`
// finns: med font 13 (radhöjd 14.72) hade höjd 18 gett 1.6 px luft TOTALT
// och knappen läst som en etikett med bakgrund.
constexpr Style kMinimalisticRegular = [] {
    Style m = kSlateRegular;

    m.uiFontSize     = gfx::fontsize::kUiCompact;   // radhöjd 12.16 — mätt
    // Rubriken följer med ner: 20 px mot en 10 px brödtext hade varit
    // dubbla storleken och spräckt just den täthet familjen finns för.
    m.headingFontSize = gfx::fontsize::kUiHeadingCompact;

    // ---- Button
    m.buttonPadX     = 10.0f;
    m.buttonPadY     = 2.92f;   // HÄRLEDD ur radhöjden: ger exakt höjd 18
    m.buttonRadius   = 3.0f;    // stramare än Slates 4 och Glazes 6 — formen
    m.cornerRadius   = 3.0f;    // ÄR en kant, och en kant tål mindre rundning

    // Fokusringen halveras (2 → 1), samma beslut som Glaze tog och av samma
    // skäl: ringen ligger UTANFÖR kontrollen och konkurrerar med allt runt
    // omkring. Vid 18 px höjd väger den dessutom tyngre relativt kroppen.
    m.focusWidth     = 1.0f;
    m.focusOffset    = 2.0f;

    // PLATT hela vägen: Outline har varken lyft, skugga eller laminering,
    // och trycket syns i FYLLNADEN i stället för i en förskjutning — vid 18
    // px är 1 px en artondel av knappen och läser som ett hopp.
    m.raiseDy        = 0.0f;
    m.raiseBlur      = 0.0f;
    m.raiseBlurHover = 0.0f;
    m.buttonShadowDy   = 0.0f;
    m.buttonShadowBlur = 0.0f;
    m.sheenLower       = 0.0f;
    m.sheenUpper       = 0.0f;
    m.buttonPressShift = 0.0f;
    m.sliderShadowDy   = 0.0f;   // "noll djup rakt igenom" gäller ALLA
    m.sliderShadowBlur = 0.0f;   // widgets, inte bara knappen

    // ---- Verktygsfältet — desktop-tätheten igen. Samma
    // HÄRLEDNING som Slates par: ikonen (16) är högre än radhöjden vid
    // font 10 (12.16), så knappen blir 16 + 2 × 2 = 20 och remsan
    // 20 + 2 × 2 = 24 — en remsa i Minimalistic ska rymma tre gånger så
    // mycket, inte vara en Slate-remsa med mindre text.
    m.toolBarH       = 24.0f;
    m.toolBarPadX    = 2.0f;
    m.toolBarPadY    = 2.0f;
    m.toolButtonPadX = 4.0f;
    m.toolButtonPadY = 2.0f;

    // ---- Gruppramen — familjens stramare radie och tätare luft.
    m.groupRadius      = 3.0f;
    m.groupPad         = 6.0f;
    m.groupTitleIndent = 6.0f;
    m.groupTitleGap    = 3.0f;

    // ---- Talfältet — familjens täthet: fonten är 10 och ett tal
    // tar mindre plats, så både stegaren och auto-bredden krymper. (Fältets
    // HÖJD följer `fieldPadY`, som familjen ärver av Slate — den är
    // utseendepassets fråga och rörs inte här.)
    m.spinStepW    = 14.0f;
    m.spinGap      = 2.0f;
    m.spinDefaultW = 72.0f;

    // ---- Drag-and-drop — dragbilden får familjens knapphöjd ur
    // samma härledning som knappen: radhöjden 12.16 + 2 × 2.92 = 18.
    // Tröskeln och linjen är OFÖRÄNDRADE: en hand darrar lika mycket i en
    // tät stil, och en tunnare markering än 2 px försvinner mot en rad.
    m.dragGhostPadX = 6.0f;
    m.dragGhostPadY = 2.92f;
    m.dragBadgeSize = 12.0f;

    // ---- Trädet — familjens täthet, samma härledning som Slates
    // par: barnets pil står under förälderns text. Raderna själva ärver
    // `listRowH` (Minimalistics måttabell är utseendepassets).
    m.treeIndent    = 12.0f;
    m.treeExpanderW = 12.0f;

    // ---- Tabellen — huvudet följer RADERNA, och de ärver
    // Slates `listRowH`/`gridRowH` (Minimalistics måttabell är
    // utseendepassets). Ett tätare huvud över luftiga rader hade sett ut
    // som en annan widget. Standardbredden krymper med fonten (10 mot 13):
    // samma ord tar mindre plats.
    m.columnDefaultW = 80.0f;

    // ---- Skiktet — familjens täthet, ett snäpp smalare.
    // Släppzonen ärver Slates: handen är densamma i en tät stil
    // (`dragThreshold`-resonemanget).
    m.splitterThick = 5.0f;

    // ---- Den dockningsbara panelen — samma HÄRLEDNING som
    // knappen: radhöjden 12.16 + 2 × 2.92 = 18, alltså familjens
    // knapphöjd. Tecknen krymper med raden.
    m.dockHeaderH    = 18.0f;
    m.dockHeaderPadX = 6.0f;
    m.dockGlyph      = 7.0f;
    m.dockButtonInset = 2.0f;   // = toolBarPadY: plattan 14 × 14 i en 18-rad

    // ---- Tooltipen — dragbildens HÄRLEDNING: radhöjden 12.16 +
    // 2 × 2.92 = 18 för en rad, familjens knapphöjd. Tiderna ärver Slates —
    // en hand vilar lika länge i en tät stil.
    m.toolTipPadX = 6.0f;
    m.toolTipPadY = 2.92f;

    // ---- Flikarna — familjens täthet: radhöjden 12.16 + ~4.9
    // luft per sida, och fonten 10 gör en flik smalare. Ingen radie: en
    // Minimalistic-flik är ingen platta utan en rubrik med ett streck under
    // (tab_control.cpp), och flikarna står kant i kant som menyradens.
    m.tabHeaderH = 22.0f;
    m.tabPadX    = 8.0f;
    m.tabMinW    = 36.0f;
    m.tabGap     = 0.0f;
    m.tabRadius  = 0.0f;
    m.tabCloseInset = 4.0f;   // knappen 14 × 14 = dockningsrubrikens platta
    m.tabCloseGlyph = 7.0f;   // = dockGlyph i familjen

    // ---- Dockningens handtag och verktygsfältens tillägg —
    // familjens täthet: handtaget följer rubriken (18), luften rubrikens
    // luft, och greppet och pilen krymper med verktygsknapparnas luft.
    // Snappen, tiderna och handtagens lucka ärver Slates — handen och ögat
    // är desamma i en tät stil.
    m.dockHandleH    = 18.0f;   // = dockHeaderH i familjen
    m.dockHandlePadX = 6.0f;    // = dockHeaderPadX i familjen
    m.toolGripW      = 6.0f;
    m.toolArrowW     = 12.0f;
    m.toolArrowGlyph = 6.0f;    // ett snäpp under dockGlyph (7) i familjen
    m.toolOverflowW  = 18.0f;   // ikonen 16 och 1 på var sida — familjens
                                // ikonknapp är 24 (toolButtonPadX 4)

    // ---- Filväljaren — familjens täthet: fonten är 10 och samma
    // ord tar mindre plats, så kolumnerna, platslistan och hela dialogen
    // krymper i samma proportion som `columnDefaultW` (100 → 80). Raderna
    // ärver Slates `listRowH`, och knapparna följer familjens
    // knappar.
    m.fileDlgW         = 640.0f;
    m.fileDlgH         = 400.0f;
    m.fileDlgMinW      = 470.0f;
    m.fileDlgMinH      = 300.0f;
    m.fileDlgPad       = 8.0f;
    m.fileDlgGap       = 6.0f;
    m.fileDlgPlacesW   = 140.0f;
    m.fileDlgPlacesMin = 90.0f;
    m.fileDlgListMin   = 180.0f;
    m.fileDlgFilterW   = 170.0f;
    m.fileDlgBtnMinW   = 64.0f;
    m.fileColNameW     = 180.0f;
    m.fileColSizeW     = 64.0f;
    m.fileColDateW     = 104.0f;
    m.fileColTypeW     = 96.0f;

    // ---- Filväljarfältet — talfältets luft och familjens bredd.
    m.filePickerGap      = 2.0f;   // = spinGap i familjen
    m.filePickerDefaultW = 200.0f;

    // ---- Färgväljaren — familjens täthet: fältet är fältets (utseendepasset
    // äger dess höjd), men rutan, popupens luft och palettrutorna krymper
    // med fonten (13 → 10), och rutornas sida följer kryssrutans tänkta
    // familjemått snarare än det ärvda (checkSize 18 är utseendepassets fråga).
    m.colorPickerDefaultW = 56.0f;
    m.colorSwatchInset    = 3.0f;
    m.colorSwatchRadius   = 1.0f;
    m.colorPopupPad       = 6.0f;
    m.colorPopupGap       = 6.0f;
    m.colorCellSize       = 14.0f;
    m.colorCellGap        = 3.0f;
    m.colorSliderMinW     = 96.0f;

    // ---- Datumväljaren — familjens täthet: datumet tar mindre
    // plats i font 10, knappen är familjens knapphöjd (18 — kvadraten följer
    // fältets höjd, bredden är den här), och rutnätet krymper med fonten.
    m.dateDefaultW  = 100.0f;
    m.dateButtonW   = 20.0f;
    m.dateGap       = 2.0f;    // = spinGap i familjen
    m.calPad        = 6.0f;    // = colorPopupPad i familjen
    m.calHeaderH    = 22.0f;   // = tabHeaderH i familjen
    m.calCellW      = 26.0f;
    m.calCellH      = 20.0f;
    m.calCellRadius = 3.0f;    // = buttonRadius i familjen

    // ---- Det flerradiga fältet — filväljarfältets bredd i
    // familjen (fonten är 10 och samma text tar mindre plats); raderna är
    // lika många, för radhöjden krymper redan med fonten.
    m.textAreaDefaultW = 200.0f;

    // ---- Färgrutan, datumet och statuspanelen — familjens täthet: rutmönstret i den
    // mindre färgrutan (inset 3), datumet MED tiden i font 10 (dateDefaultW +
    // tidens ~32) och statusfältets luft som dockningsrubrikens.
    m.colorCheckerCell = 4.0f;
    m.dateTimeDefaultW = 136.0f;
    m.statusPanelPadX  = 6.0f;   // = dockHeaderPadX i familjen

    // ---- Ritytan — familjens täthet i AUTO-måttet
    // (samma 4:3-förhållande som textfältets 240 → 200). Rutnätet ärver
    // Slates: hårlinjen och det tätaste avståndet är ögats, inte familjens.
    m.canvasDefaultW = 240.0f;
    m.canvasDefaultH = 160.0f;

    // ---- Markdown-vyn — familjens täthet: underrubriken
    // följer rubriken ner (12 mellan 15 och 10), AUTO-bredden krymper som
    // ritytans (300 → 240), och luften och indragen krymper med fonten
    // (13 → 10) — samma ord tar mindre plats.
    m.subheadingFontSize   = gfx::fontsize::kUiSubheadingCompact;
    m.markdownDefaultW     = 240.0f;
    m.markdownPad          = 6.0f;
    m.markdownBlockGap     = 7.0f;
    m.markdownHeadingGap   = 4.0f;
    m.markdownListIndent   = 16.0f;
    m.markdownMarkerGap    = 4.0f;
    m.markdownBulletSize   = 4.0f;
    m.markdownQuoteIndent  = 10.0f;
    m.markdownQuoteBarW    = 2.0f;
    m.markdownCodePad      = 6.0f;
    m.markdownCodeSpanPadX = 2.0f;
    m.markdownCellPadX     = 6.0f;
    m.markdownCellPadY     = 3.0f;

    return m;
}();

} // namespace

// ------------------------------------------------------- UI -> dess styles
//
// Uppslaget går ALLTID via UI:t — det finns ingen platt global lista där
// "Regular" kan betyda vad som helst. Varje ritning har exakt en
// storlek; listan är skriven så att en andra bara är en rad till.

int styleCount(UI)
{
    return 1;   // en storlek per UI så länge; alla heter "Regular"
}

const Style& styleAt(UI u, int index)
{
    if (index < 0 || index >= styleCount(u))
        index = 0;
    switch (u) {
    case UI::Glaze:         return kGlazeRegular;
    case UI::Minimalistic:  return kMinimalisticRegular;
    case UI::Slate:
    default:                return kSlateRegular;
    }
}

const Style& defaultStyle(UI u)
{
    return styleAt(u, 0);
}

} // namespace glaze
