#pragma once

// UI + Style: RITNINGSVALET och MÅTTEN — vilken form en widget har och hur
// stor den är. TRE OBEROENDE AXLAR bär utseendet (+ ThemeMode som fjärde,
// läget), och gränsen mellan dem är hård och lätt att grinda:
//
//     Är det en RITNINGSGREN?  -> UI     (den här filen)  switch (c.ui)
//     Är det en FÄRG?          -> Theme  (glaze/theme.h)  c.theme->colors.x
//     Är det ett MÅTT/en FORM? -> Style  (den här filen)  c.style->x
//     Är det RITNING?          -> Draw   (glaze/draw.h)   Draw::rect(c, ...)
//
// Att välja UI sätter Theme och Style till den familjens defaults, men båda
// kan sedan bytas fritt utan att lämna familjen: en UI kan ha FLERA teman
// (Slate har tio) och FLERA storlekar, vilket ETT enum för ritning + mått +
// palett på en gång inte hade tillåtit.
//
//   UI      = VALET (enum, app-globalt, Ctx::ui) — widgeten switchar på den
//             och kan rita en helt annan FORM.
//   Style   = TABELLEN med måtten (Ctx::style) — alla mått i LOGISKA px.
//             INGA sifferkonstanter i widget-kod, inga i Draw: ett tal som
//             styr utseendet bor HÄR.
//
// Därför räcker inte tabellen ensam och switchen inte heller: en variant kan
// vara "samma form, andra mått" (bara en ny Style) ELLER "en annan form"
// (färre/andra Draw-anrop i widgetens case) — oftast både och.

namespace glaze {

// Inbyggda ritningar (FORMEN — vilken `draw()`-gren som körs).
//
//   Slate         BASEN och `default:`-grenen i varje widget. Alla komponenter
//                 ritas i fyra klasser — Yta / Platta / Fält /
//                 Spår — med en skriven fyra-tillstånds-tabell var.
//                 Teman: "Windows" + de nio hues.
//                 Styles: "Regular".
//   Glaze         husets ritning, "Soft Laminate": tonad kropp + två klippta
//                 ljusfält + bestämd kontur. Lamineringen KRÄVER en kontur —
//                 den drar ytan mot den ljusa änden, så kanten måste bära formen.
//                 Teman: "Lavender" (ETT). Styles: "Regular".
//   Minimalistic  "Outline": ytan är fönstrets, kanten ÄR formen. Fyllning
//                 betyder TILLSTÅND, spår är streck och inte rännor. Teman:
//                 de nio hues. Styles: "Regular".
//
// **VARJE UI ÄGER SINA EGNA teman och styles — namnrymden är per UI, inte
// global**. Namn får därför SAMMANFALLA och betyda olika
// saker: "Regular" finns tre gånger (höjd 27 i Slate, 27.3 i Glaze, 18 i
// Minimalistic) och "Neutral" två (Slate ritar fyllda ytor där Minimalistic
// ritar kanter, så samma mättnad ger ett annat intryck). De jämförs aldrig
// med varandra — ett namn är bara meningsfullt TILLSAMMANS med sitt UI, och
// därför går varje uppslag via UI:t.
//
// **ALLA TRE RITNINGARNA ÄR GENOMGÅNGNA.** Varje widget med
// en `draw()` har en `case UI::Glaze:` OCH en `case UI::Minimalistic:` i sin
// egen cpp, tillsammans med Slates `default:`, och Dropdown
// har dem i BÅDA sina draw-funktioner (fältet och popup-listans rader).
// `default:` är alltså bara Slates gren plus vaktposten för ett okänt värde.
//
// Ett par widgets ritas MEDVETET likadant i två eller tre familjer (Label,
// Separator, Picture i alla tre; Link och Panel i Glaze). De står ändå som
// egna `case`-rader med SKRIVET skäl — att text är text i alla formspråk är
// ett beslut, inte en lucka, och beslut ska stå i filen.
enum class UI { Glaze, Slate, Minimalistic };

// Alla mått i LOGISKA px (× Ctx::scale sker inne i Draw — aldrig i
// widget-kod). Nya fält läggs till i takt med att widgets konverteras;
// fältnamnen är <widget><Vad> när måttet bara rör en widget, annars
// generiskt.
struct Style {
    const char* name;     // storlekens namn INOM sitt UI ("Regular"). Bara
                          // meningsfullt tillsammans med UI:t — se noten vid
                          // enum UI ovan. Väljare (demons dropdown) läser
                          // det via styleAt().

    // ---- typografi
    float uiFontSize;     // UI-textens storlek. EGET fält och inte
                          // `gfx::fontsize::kUi` direkt: radhöjden ensam
                          // sätter GOLVET för varje kontrollhöjd, så en
                          // kompakt Style måste kunna sänka den. Fasadens
                          // konstant förblir enda SIFFERkällan — Glazes
                          // och Slates Regular sätter fältet till den, och
                          // bara Minimalistics avviker. Fonten laddas om
                          // när fältet ändras (AppWindow::reloadFont).

    float headingFontSize;// RUBRIKENS storlek (`Label::Role::Heading`).
                          // Att den bor HÄR och inte som ett
                          // tal på widgeten är hela poängen med
                          // rollvägen: appen säger VAD texten är, stilen
                          // äger HUR STOR, och stil-växlaren når därför
                          // typografin också. En multiplikator på
                          // widgeten (`fontScale = 1.6f`) hade hållit
                          // regel 3 formellt men släppt in ett VALT TAL i
                          // appkoden — och gjort mängden fontstorlekar
                          // ÖPPEN, alltså obegränsad fontcache och lat
                          // fontskapande mitt i ritvägen. Med roller är
                          // mängden SLUTEN: `Style` räknar upp den, och
                          // `AppWindow::reloadFont` förskapar dem alla.
                          // Behövs något mellan två roller är svaret en
                          // roll till, inte en lucka.

    // ---- form, delat av kontrollerna
    float cornerRadius;   // hörnradie på knapp/fält/ruta
    float borderWidth;    // ramtjocklek; 0 = HÅRLINJE (1 fysisk px oavsett
                          // skala — fasadens default). Sätt 1.0 i en stil
                          // som vill att ramen växer med skalan.
    float focusOffset;    // fokusringens avstånd utanför kanten
    float focusWidth;     // fokusringens tjocklek

    // ---- lamineringen (DELAD — inte Buttons)
    // Andelarna (0–1) av en ytas höjd som de två ljusfälten täcker. Måtten
    // delas av knappen, kryssrutan, radion, sliderns och scrollistans
    // tumme och spårens fyllda delar, så de heter inte `button…` — och
    // `Draw::laminate` tar dem som PARAMETRAR just för att en generell
    // primitiv aldrig ska känna till en widgets fält. Värdena är 0.62 / 0.34
    // i Glaze och 0 i de andra.
    float sheenLower;     // det NEDRE, större fältet, andel av höjden
    float sheenUpper;     // det ÖVRE, mindre — läggs ovanpå

    // ---- upphöjning (skuggan som lyfter en kontroll ur ytan)
    float raiseDy;        // skuggans nedåtförskjutning
    float raiseBlur;      // mjukhet i vila
    float raiseBlurHover; // mjukhet vid hover (knappen "lyfter" mer)

    // ---- Button
    float buttonRadius;   // knappens hörnradie; **negativt = PILLER** (radien
                          // är halva kortsidan, som Sliders tumme). EGEN
                          // storhet skild från cornerRadius: en stil får
                          // vilja piller på handlingsknappar och måttlig
                          // radie på fält — TVÅ radier, inte en.
    float buttonPadX;     // autostorlekens padding i sidled
    float buttonPadY;     //   (höjden = radhöjd + 2 × den här)

    // Knappens DJUP. Skuggans mått är samma i båda lägena — det är
    // skuggFÄRGENS alfa (Theme) som skiljer dem åt, för form hör till
    // stilen och läge till paletten.
    float buttonShadowDy;    // skuggans nedåtförskjutning; 0 = ingen skugga
    float buttonShadowBlur;  // dess mjukhet
    float buttonPressShift;  // hur långt innehållet sjunker vid tryck
    float iconGap;        // avstånd ikon <-> text
    float iconFallback;   // ikonsida när assetens storleksklass är ospec

    // ---- Checkbox
    float checkSize;      // rutans sida
    float checkRadius;    // rutans hörnradie
    float checkGap;       // avstånd ruta <-> text
    float checkStroke;    // bockens tjocklek

    // ---- fälten (TextInput/Dropdown — samma recept, egna tal)
    float fieldPadX;      // textens indrag i sidled
    float fieldPadY;      // autohöjdens padding över/under textraden
    float fieldDefaultW;  // bredd när layouten inte satt någon
    float caretWidth;     // textcaretens bredd
    float caretMargin;    // luft mellan caret och fältkant vid scroll

    // ---- LISTRADERNA (ListBox OCH Dropdownens popup-lista)
    // Två widgets, ListBox och dropdownens popup-lista, ritar SAMMA
    // radgeometri ur samma två tal — och ett mått som bärs av mer än en
    // widget får inte heta efter en av dem (samma resonemang som
    // `sheenLower`). Det ändrar inte den generella satsen i den här filen:
    // delad visuell PRINCIP betyder inte automatiskt delat TAL. Här delas
    // det faktiska talet, för raderna ÄR samma rader.
    float listRowH;       // radhöjd i en lista
    float listPadX;       // radernas indrag i sidled

    // ---- Dropdown (det som bara är dropdownens)
    float chevronW;       // pilens HALVA bredd
    float chevronH;       // pilens halva höjd
    int   dropMaxRows;    // synliga rader innan popup-listan kapas + scrollar

    // ---- rullningslisterna (Scrollbar OCH Panels overlay-remsor)
    float scrollThick;    // remsans tjocklek
    float scrollPad;      // tummens indrag i remsan
    float scrollThumbMin; // minsta tumlängd
    float scrollDefaultLen; // AUTO-längd när layouten inte satt någon
    float wheelStep;      // hjulsteg per rad (Panel-scrollen)

    // ---- Panel / PropertyGrid
    float panelRadius;    // CONTAINERNS hörnradie. 0 = fyrkantigt, och det är
                          // värdet i alla tre tabellerna i dag. Barnen klipps
                          // till den rundade formen (`Panel::clipRadius`), så
                          // varje värde ger rena hörn.
    float panelInset;     // innehållsytans indrag när panelen har ram
    float gridRowH;       // griddens radhöjd
    float gridNameW;      // namnkolumnens bredd när nameWidth är 0
    float gridPadX;       // textens indrag i en cell
    float rowMarkerW;     // en RADS vänstermarkör — PropertyGrids
                          // aktiva rad och Dropdown-listans valda rad, som är
                          // samma sak sedd två gånger. Minimalistic får inte
                          // markera en rad med en fyllning (fyllning betyder
                          // tillstånd på en KONTROLL, och en rad är ingen
                          // kontroll), så markeringen blev ett accent-streck i
                          // vänsterkanten. En hårlinje hade varit för svag för
                          // en markering som ska läsas på avstånd. Slate och
                          // Glaze ritar ingen markör — de fyller cellen/raden
                          // — men måttet står i alla tre tabellerna, som varje
                          // annat fält.

    // ---- fönstret självt
    // Hörnradien på HELA fönstret. 0 = fyrkantigt.
    // Ett rundat fönster kräver en GENOMSKINLIG yta — hörnen är inte
    // målade, de är bortklippta — så måttet är samtidigt en begäran om
    // per-pixel-alfa. Maximerat fönster ritas alltid fyrkantigt: ett
    // rundat hörn mot skärmkanten är ett hål, inte en form.
    float windowRadius;

    // ---- caption-barerna (Bar/CaptionButton)
    float barHeight;      // barens höjd när ingen satts
    float statusBarH;     // STATUSRADENS höjd när ingen satts. EGET fält
                          // och inte `barHeight`: en statusrad är lägre än
                          // en namnlist av designskäl, och ett tal i
                          // `status_bar.h` vore ett mått i en widget-fil,
                          // mot regel 3:s anda. Det spelar roll i praktiken
                          // också: `setContentSize` räknar baklänges genom
                          // chromet, och ett tal som INTE följer stilen hade
                          // gjort just den uträkningen oärlig.
    float captionBtnW;    // glyf-fönsterknappens bredd
    float captionGlyph;   // glyfens HALVA sida
    float captionStroke;  // glyfens tjocklek; 0 = hårlinje (Windows-formen)
    float restoreOffset;  // förskjutning mellan återställ-glyfens två rutor
    float trafficSize;    // traffic light-cirkelns diameter
    float trafficGlyph;   // hover-glyfens HALVA sida i cirkeln
    float trafficStroke;  // hover-glyfens tjocklek

    // ---- kapselklustret (CaptionButton::Shape::Capsule)
    // Klustret är EN form som tre fack delar; måtten är fackets, inte hela
    // kapselns — bredden på hela kapseln faller ut ur hur många fack som är
    // synliga (controlBox/min/max-box döljer fack, och då krymper den).
    float capsuleH;       // kapselns höjd; v-centrerad i barens fulla höjd
    float capsuleSeg;     // ETT facks bredd (= dess klickyta i sidled)
    float capsuleInset;   // det TÄNDA fackets luft in från kapselns kontur —
                          // facket får aldrig ta i ytterkanten
    float capsuleStroke;  // glyfens tjocklek i kapseln. EGET fält och inte
                          // `captionStroke`: Windows-formens glyfer är
                          // hårlinjer mot en stor platta, kapselns sitter i
                          // ett 26 px fack och försvinner då

    // ---- Separator
    float separatorThickness; // strecket; 0 = HÅRLINJE (1 fysisk px)
    float separatorInset;     // luft på VARDERA sidan om strecket (tunna
                              // axeln blir tjocklek + 2 × inset)
    float separatorDefaultLen; // AUTO-längd när layouten inte satt någon

    // ---- Link
    float linkUnderline;      // understrykningens tjocklek; 0 = hårlinje
    float linkGap;            // luft mellan textens underkant och strecket

    // ---- Radio
    float radioSize;          // ringens ytterdiameter
    float radioDot;           // den ifyllda prickens diameter
    float radioGap;           // avstånd ring <-> text
    float radioStroke;        // ringens tjocklek; 0 = hårlinje

    // ---- Slider
    float sliderTrack;        // rännans tjocklek
    float sliderThumb;        // tummens diameter
    float sliderPad;          // rännans indrag i ändarna (= halva tummen,
                              // så tummen aldrig sticker utanför ytan)
    float sliderDefaultLen;   // AUTO-längd när layouten inte satt någon
    // Tummens DJUP — EGNA fält, inte `buttonShadow*` under ett annat
    // widgetnamn. Regeln "skugga bara på fristående plattor"
    // gäller knappen OCH sliderns tumme, men det betyder inte att de måste
    // dela tal: en 16 px tumme och en 27 px knapp tål olika mycket, och ett
    // delat fält hade gjort den ena omöjlig att justera utan att röra den
    // andra. 0 = ingen skugga (Slate och Minimalistic).
    float sliderShadowDy;
    float sliderShadowBlur;

    // ---- Progress
    float progressHeight;     // rännans höjd när layouten inte satt någon
    float progressDefaultLen; // AUTO-längd när layouten inte satt någon
    float progressRadius;     // rännans/fyllningens hörnradie
    float progressChunk;      // obestämda bitens andel av rännan (0–1)
    int   progressPeriodMs;   // en tur-och-retur i obestämt läge (ms)

    // ---- damage
    float overdraw;       // största marginal ritningen sticker UTANFÖR
                          // widgetens rect (skugga + fokusring + SDF-kant).
                          // Widget::paintBounds blåser upp damage med den —
                          // för snålt värde ger spökpixlar vid partiell
                          // omritning.

    // ---- MeddelanderutaN (glaze::messageBox)
    // Rutan är ett FÖNSTER och inte en widget, men dess mått hör hit av
    // exakt samma skäl som allt annat: en tätare stil ska kunna krympa
    // dialogen utan att en enda rad kod rörs, och ett tal i
    // message_box.cpp hade tyst kopplat bort Style-axeln för den.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring —
    // ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    //
    // Alla tre familjerna har SAMMA tal, och det är
    // skrivet med flit: `kGlazeRegular`/`kMinimalisticRegular` är DELTAN av
    // Slate och ärver därför värdena. Avvikande mått per familj läggs till
    // när formen kräver det — precis som Minimalistics övriga mått.
    float msgIconSize;    // badgens diameter (kvadratisk ruta)
    float msgPad;         // marginalen runt hela innehållet
    float msgGap;         // luft ikon <-> text och text <-> knapprad
    float msgTextMaxW;    // textens BRYTBREDD — dialogens bredd faller ut
    float msgBtnGap;      // luft mellan knapparna
    float msgBtnMinW;     // knappens minsta bredd (så "OK" inte blir en prick)

    // ---- MENYERNA — meny, snabbmeny, undermeny, menyrad
    // Menyn är en DATASTRUKTUR som visas i ett popup-FÖNSTER, men måtten
    // hör hit av samma skäl som meddelanderutans: ett tal i `menu.cpp`
    // hade tyst kopplat bort Style-axeln för menyerna.
    //
    // MEDVETET EGNA TAL och inte lån av `listRowH`/`listPadX`: en menyrad
    // och en listrad ser lika ut i dag, men de är olika saker (en menyrad
    // bär glyfkolumn, genvägstext och undermenypil), och den här filen
    // säger själv att delad visuell PRINCIP inte betyder delat TAL.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float menuRowH;         // en vanlig menyrads höjd
    float menuPadX;         // radens indrag i sidled (båda kanterna)
    float menuSepH;         // SEPARATORRADENS höjd — tunn, och det är hela
                            // skälet att ListBox fick variabel radhöjd
    float menuShortcutGap;  // minsta luft mellan etikett och genvägstext
    float menuMinW;         // menyns minsta bredd
    float menuBarPadX;      // menyradsrubrikens indrag i sidled
    float menuArrowW;       // undermenypilens HALVA bredd (som chevronW)
    int   menuMaxRows;      // synliga rader innan menyn kapas + scrollar.
                            // Kapningen sker FÖRE `show()` — Wayland-
                            // positioneraren har varken RESIZE_Y eller
                            // SLIDE_Y, så en för hög popup blir ALDRIG en
                            // mindre viewport och innehållet hade blivit
                            // oåtkomligt. Samma mönster som `dropMaxRows`,
                            // eget tal (en meny tål fler rader än en
                            // vallista).

    // ---- VERKTYGSFÄLTET (ToolBar/ToolButton)
    // Remsan är samma sorts sak som menyraden och statusraden och får sin
    // HÖJD ur stilen precis som de (`barHeight`/`statusBarH`/`menuRowH`).
    // Knappens EGNA mått gäller när den står fritt; i remsan får den
    // remsans innerhöjd — och tabellerna väljer talen så att de två är
    // SAMMA (toolBarH = knappens naturliga höjd + 2 × toolBarPadY), se
    // noten vid talen i style.cpp.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float toolBarH;         // verktygsfältets höjd när ingen satts
    float toolBarPadX;      // barnens indrag från remsans ändar
    float toolBarPadY;      // barnens indrag över/under (= knappens luft
                            // mot remsans kant)
    float toolGap;          // luft MELLAN remsans barn (flexens lucka —
                            // `Bar::childGap`)
    float toolButtonPadX;   // verktygsknappens autostorlek: padding i sidled
    float toolButtonPadY;   //   och över/under innehållet
    float iconFade;         // INAKTIV BILD: hur mycket (0–1) ikonen TONAS
                            // BORT — den ritas med opacitet 1 − iconFade
                            // (`Content::imgOpacity` i draw.h), mot vilken
                            // yta som helst

    // ---- GRUPPRAMEN (GroupBox)
    // EGEN radie och inte `panelRadius`: panelens radie är 0 i alla tre
    // tabellerna — att ge den ett värde hade rundat varje ramad Panel i varje
    // app. Gruppramen har därför sin egen radie.
    float groupRadius;      // ramens hörnradie
    float groupPad;         // barnens indrag från ramen (sidorna, botten,
                            // och under rubrikbandet)
    float groupTitleIndent; // rubrikluckans början, från ramens vänsterkant
                            // (≥ radien, annars äter bågen rubriken)
    float groupTitleGap;    // luft mellan linjen och rubriktexten i luckan

    // ---- TALFÄLTET (SpinBox)
    // Fältet är en riktig `TextInput` med fältets egna mått; det här är
    // STEGARENS mått och talfältets auto-bredd. Pilarna är dropdownens
    // (`chevronW`/`chevronH`) — samma tecken, samma storlek.
    float spinStepW;        // stegarens bredd (de två halvorna delar höjden)
    float spinGap;          // luft mellan fältet och stegaren
    float spinDefaultW;     // talfältets AUTO-bredd, stegaren inräknad — ett
                            // tal är smalare än en textrad (`fieldDefaultW`)

    // ---- DRAG-AND-DROP
    // Tröskeln är BETEENDE och inte utseende — men den är ett MÅTT i logiska
    // px, och ett tal i drag_drop.cpp hade kopplat bort Style-axeln för den
    // precis som ett tal i en widget-fil gör. Resten är återkopplingen, som
    // ramverket ritar i overlay-lagret: ett EFFEKTMÄRKE vid pekaren (OS:ens
    // "+" och "⃠" — vi äger pekaren lika lite som vi äger en systemdialog,
    // så märket är vårt) och en DRAGBILD med lastens text intill.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float dragThreshold;    // hur långt ett tryck får röra sig och ändå vara
                            // ett TRYCK; MER än så startar draget
    float dragOffset;       // återkopplingens förskjutning från pekarens
                            // spets, båda axlarna (nedanför och till höger,
                            // där OS-pekarens eget märke brukar sitta)
    float dragBadgeSize;    // effektmärkets diameter
    float dragGhostPadX;    // dragbildens luft i sidled
    float dragGhostPadY;    //   och över/under textraden
    float dragGhostMaxW;    // dragbildens största bredd — längre text klipps
    float dropLineW;        // släppmarkeringens tjocklek: ramen runt ett
                            // accepterande mål och en lista/ett träds
                            // insättningslinje

    // ---- TRÄDET (TreeView)
    // Raderna ÄR listrader — höjden och sidoindraget är `listRowH`/`listPadX`
    // av samma skäl som dropdownens popup delar dem med ListBox. Det som är
    // trädets eget är NIVÅERNA: hur långt ett barn dras in och hur bred
    // expanderns kolumn är. Pilen i den är dropdownens (`chevronW`/
    // `chevronH`) — samma tecken, samma storlek, som talfältets stegare.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float treeIndent;       // indraget PER NIVÅ (ett barn står så här mycket
                            // längre in än sin förälder)
    float treeExpanderW;    // expanderns kolumn — pilens ruta OCH dess
                            // klickyta, före ikonen och texten
    float treeDropEdge;     // ett släpp över en rad: den ANDEL (0–1) av
                            // radhöjden i radens över- respektive underkant
                            // som betyder FÖRE/EFTER noden — mitten betyder
                            // IN I den. Ett BETEENDEMÅTT som `dragThreshold`,
                            // och av samma skäl här: ett tal i widget-filen
                            // hade kopplat bort Style-axeln för det

    // ---- TABELLEN (ListView och DataGrid)
    // Raderna är LÅNADE och inte tabellens: en listvys rad ÄR en listrad
    // (`listRowH`/`listPadX`, samma skäl som trädet ovan), en datagrids rad
    // ÄR en gridrad (`gridRowH`/`gridPadX` — PropertyGrid är dess
    // prejudikat). Det som är tabellens EGET är KOLUMNHUVUDET och
    // kolumnernas BREDD. Sorteringspilen är dropdownens (`chevronW`/
    // `chevronH`) — samma tecken, samma storlek.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float tableHeaderH;     // kolumnhuvudets höjd
    float columnDefaultW;   // en kolumns bredd när ingen satts (width <= 0)
    float columnMinW;       // det smalaste en kolumn kan DRAS till i huvudet
    float columnGrip;       // avdelarens GREPPZON i huvudet — HALVA bredden,
                            // på var sida om linjen. Ett BETEENDEMÅTT
                            // (`dragThreshold`-prejudikatet)

    // ---- SKIKTET (Splitter och DockHost)
    // Skiktet är EN sak sedd två gånger — delarens skikt mellan två rutor
    // och dockningsytans skikt mellan en panel och resten — och ritas av
    // samma interna ritning (`src/sash.cpp`). Därför ett namn utan widget:
    // samma regel som `listRowH`.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float splitterThick;    // skiktets tjocklek = dess GREPPYTA (det är
                            // mellanrummet mellan rutorna, inte en linje)
    float splitterGripLen;  // greppets längd mitt på skiktet; 0 = inget
                            // grepp, bara linjen (Slate, Minimalistic)

    // ---- DEN DOCKNINGSBARA PANELEN (DockPanel/DockHost)
    float dockHeaderH;      // rubrikradens höjd — och den hopfällda
                            // panelens hela tjocklek, åt vilket håll den
                            // än är dockad
    float dockHeaderPadX;   // rubrikens indrag från vänsterkanten
    float dockGlyph;        // rubrikknapparnas tecken (fäll ‹ och stäng ✕):
                            // rutans sida, centrerad i en kvadratisk knapp
                            // lika hög som raden
    float dockDefaultSize;  // AUTO-tjockleken (width/height <= 0) — en
                            // PLATSHÅLLARE som remsornas `*DefaultLen`
    float dockDropZone;     // hur nära en kant ett draget huvud ska vara
                            // för att panelen ska dockas där. Ett
                            // BETEENDEMÅTT, som `dragThreshold` — handen är
                            // densamma i varje stil

    // ---- TOOLTIPEN (Widget::setToolTip)
    // Tooltipen är ett POPUP-FÖNSTER med en rad text (eller flera — den
    // bryts mot `toolTipMaxW`), och måtten hör hit av meddelanderutans skäl:
    // ett tal i tooltip.cpp hade tyst kopplat bort Style-axeln för den.
    // TIDERNA står här också, som `progressPeriodMs`: hur länge pekaren ska
    // VILA är en egenskap hos stilen (en tät, snabb stil kan vilja kortare),
    // och ett tal i AppWindow hade inte gått att byta.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float toolTipPadX;      // textens luft mot plattans sidor
    float toolTipPadY;      //   och över/under textblocket
    float toolTipMaxW;      // BRYTBREDDEN — längre text bryts på ordgräns
    float toolTipOffsetY;   // hur långt under pekarens spets plattan börjar
                            // (under pilpekarens kropp, inte på den)
    int   toolTipDelayMs;   // hur länge pekaren ska VILA innan tooltipen visas
    int   toolTipReshowMs;  // fördröjningen när en tooltip REDAN syntes och
                            // pekaren gick direkt till nästa (verktygsfältet:
                            // man läser sig längs raden utan att vänta om)

    // ---- DEN DOCKNINGSBARA PANELEN, forts. — står efter
    // tooltipens fält: positionen i en positionell literal är SIST, inte
    // bredvid släktingarna.
    float dockButtonInset;  // rubrikknappens TÄNDA platta: luften mellan
                            // plattan och den kvadratiska knappens kant, så
                            // plattan aldrig tar i rubrikradens kanter

    // ---- TOOLTIPEN, forts. — samma skäl att stå SIST.
    float toolTipOffsetUp;  // luften mellan plattans underkant och pekarens
                            // spets när plattan inte får plats under och
                            // VÄNDS UPPÅT. Aldrig 0: en platta som täcker
                            // pekarens egen pixel tar musen från ägaren,
                            // som då gömmer den — och visar den igen efter
                            // fördröjningen, i en evig blinkning

    // ---- FLIKARNA (TabControl/TabPage)
    // Flikraden ovanför sidan. En flik är så bred som sin rubrik plus
    // luften (WinForms `SizeMode.Normal`) och KRYMPER mot `tabMinW` när
    // raden inte rymmer alla — rubriken kortas då med ellips. Den valda
    // flikens markör är radens (`rowMarkerW`) och sidans yta panelens
    // (`panel`/`border`), så här står bara det som är flikens EGET.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float tabHeaderH;       // flikradens höjd
    float tabPadX;          // rubrikens luft mot flikens sidor
    float tabMinW;          // det smalaste en flik krymper till — rubrikens
                            // flik; en stängbar är stängknappen bredare
    float tabGap;           // luften mellan två flikar — och före den första
    float tabRadius;        // flikens ÖVRE hörn; den nedre kanten är sidans
    float tabCloseInset;    // stängknappen (`setClosable`) är en KVADRAT
                            // längst till höger i fliken: radens höjd minus
                            // det här på varje sida — och samma luft mot
                            // flikens högerkant
    float tabCloseGlyph;    // stängknappens ✕ — rutans sida, centrerad

    // ---- FILVÄLJAREN — en DIALOG och inte en widget,
    // och måtten hör hit av meddelanderutans skäl: ett tal i file_dialog.cpp
    // hade tyst kopplat bort Style-axeln för den. Raderna och kolumnhuvudet
    // är listvyns (`listRowH`, `tableHeaderH`), ikonernas sida är
    // `iconFallback` och luften till texten `iconGap` — här står bara det som
    // är DIALOGENS eget.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float iconStroke;       // `Draw::icon`s linje — mappen, filen, platserna
                            // och uppåtpilen är RITADE glyfer och inte bilder
    float fileDlgW;         // innehållsytan när dialogen öppnas …
    float fileDlgH;
    float fileDlgMinW;      // … och den minsta den kan dras till
    float fileDlgMinH;
    float fileDlgPad;       // marginalen runt allt
    float fileDlgGap;       // luften mellan raderna och mellan kontrollerna
    float fileDlgPlacesW;   // platslistans bredd när dialogen öppnas (delarens
                            // FASTA ruta — fillistan tar resten)
    float fileDlgPlacesMin; // det smalaste platslistan kan dras till
    float fileDlgListMin;   //   och fillistan
    float fileDlgFilterW;   // filtypslistans bredd
    float fileDlgBtnMinW;   // OK/Avbryts minsta bredd (`msgBtnMinW`s roll i
                            // DEN HÄR dialogen — ett eget fält, så de två kan
                            // skilja sig)
    float fileColNameW;     // fillistans kolumner: namn …
    float fileColSizeW;     //   storlek
    float fileColDateW;     //   ändrad
    float fileColTypeW;     //   typ

    // ---- FILVÄLJARFÄLTET (FilePicker)
    float filePickerGap;      // luften mellan fältet och "…"-knappen
    float filePickerDefaultW; // AUTO-bredden, knappen inräknad — en sökväg är
                              // längre än ett ord, så fältets `fieldDefaultW`
                              // räcker inte

    // ---- FÄRGVÄLJAREN (ColorPicker)
    // Den stängda kontrollen är ett FÄLT med en färgruta där fältet har sin
    // text och dropdownens pil till höger (`chevron*`); popupen bär paletten,
    // tre reglage och hexfältet, och dess storlek räknas ur måtten nedan
    // FÖRE visningen (en xdg-popup kan inte växa efteråt). Reglagens och
    // fältets egna mått är deras (`slider*`, `field*`).
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float colorPickerDefaultW; // AUTO-bredden: färgrutan och pilen
    float colorSwatchInset;    // färgrutans luft mot fältets kant — och mot
                               // pilen
    float colorSwatchRadius;   // färgrutans hörn (i fältet och i paletten)
    float colorPopupPad;       // popupens marginal runt allt
    float colorPopupGap;       // luften mellan paletten och raderna, mellan
                               // raderna och mellan delarna i en rad
    float colorCellSize;       // en palettrutas sida
    float colorCellGap;        // luften mellan två palettrutor
    float colorSliderMinW;     // reglagets minsta längd — popupen blir hellre
                               // bredare än paletten än att reglaget krymper

    // ---- DATUMVÄLJAREN (DatePicker)
    // Den stängda kontrollen är talfältets form: ett riktigt textfält och en
    // KNAPP som data till höger (kalenderikonen, `Draw::icon` i
    // `iconFallback`/`iconStroke`). Popupen är ett MÅNADSRUTNÄT: en titelrad
    // med föregående/nästa månad, veckodagarna och sex rader dagar — ALLTID
    // sex, så popupen har samma storlek varje månad (den kan inte byta
    // storlek efter visningen).
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float dateDefaultW;   // AUTO-bredden, knappen inräknad — ett ISO-datum
                          // är kortare än en textrad
    float dateButtonW;    // kalenderknappens bredd (höjden är fältets)
    float dateGap;        // luften mellan fältet och knappen
    float calPad;         // månadsrutnätets marginal
    float calHeaderH;     // titelraden ("oktober 2026" och pilarna)
    float calCellW;       // en dagruta — och veckodagsrubrikens kolumn
    float calCellH;       //   och dess höjd (veckodagsraden är lika hög)
    float calCellRadius;  // dagrutans hörn (vald, dagens, under pekaren)

    // ---- DET FLERRADIGA FÄLTET (TextArea)
    // Ytan, kanten, luften och caretens mått är FÄLTETS (`field*`, `caret*`,
    // `cornerRadius`) och remsorna Panels (`scroll*`, `wheelStep`) — ett
    // anteckningsfält är samma sorts fält som det enradiga och rullar som en
    // lista. Bara AUTO-måtten är egna: det är bredare än ett ord och flera
    // rader högt.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float textAreaDefaultW; // AUTO-bredden — en anteckning är längre än ett
                            // ord, så fältets `fieldDefaultW` räcker inte
    int   textAreaRows;     // AUTO-höjden i RADER: så många textrader ryms
                            // utan att fältet rullar (luften och ramen
                            // läggs till — talet är rader, inte px)

    // ---- RITYTAN (Canvas)
    // Ytan är ett FÄLT (`cornerRadius`, `borderWidth`, fokusringens mått);
    // det egna är AUTO-måttet — en rityta har inget innehåll att mäta — och
    // rutnätet.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float canvasDefaultW;   // AUTO-bredden …
    float canvasDefaultH;   //   och höjden
    float canvasGridMin;    // rutnätets tätaste linjeavstånd i logiska px —
                            // tätare glesas det ut (× 2, × 5, × 10 …)
    float canvasGridWidth;  // rutnätets linjebredd i logiska px; 0 = HÅRLINJE
                            // (en fysisk px i varje skala)

    // ---- RITYTANS GESTER OCH TRÄFFPROV. Två
    // BETEENDEMÅTT (`dragThreshold`-prejudikatet); vinklarna, zoomstegen
    // och trögheten är kamerans räkning och bor i src/canvas/camera_math.cpp
    // — de är inga mått i logiska px.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring.
    float canvasHitTolerance;  // hur långt (logiska px) från en form ett
                               // klick får landa och ändå träffa den
    float canvasPanStep;       // en piltangents panorering i logiska px

    // ---- PROGRAMIKONEN (AppIcon)
    // PLATSEN är inget eget mått: den är en KVADRAT med namnlistens höjd —
    // och menyradens till, när den stora ikonen begärts och menyraden syns
    // — så den följer barerna i varje stil. Bilden och plattan är luft INOM
    // platsen.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float appIconInset;       // bildens luft mot platsens kant — namnlistens
                              // 32 ger en bild på 16, den stora platsen 56
                              // en på 40
    float appIconPlateInset;  // plattans luft mot platsens kant (plattan syns
                              // under pekaren och medan programmenyn är öppen)
    float appIconRadius;      // plattans hörn

    // ---- LÖSENORDSFÄLTET (TextInput::setPassword)
    // Masken är fältets FORM och inte appens val (Windows ritar ●, macOS •),
    // alltså ett mått/en form i Style och ingen `setPasswordChar` på widgeten
    // — och inte en konstant i fältets kod som `Draw::elide`s ellips, som är
    // ortografi. Den ritas en gång per KODPUNKT i texten och ska vara ETT
    // glyfsteg; den får inte vara tom (en tom eller saknad mask ger ett fält
    // utan synlig text med careten på origo — ogiltigt, men ingen krasch).
    //
    // "•" (U+2022) i alla tre Regular: tecknet ligger i Windows-1252 (0x95)
    // och därmed i varje västerländsk UI-font, också Noto Sans, som saknar
    // reservkedja på Linux. ● (U+25CF) ligger i Geometric Shapes,
    // som inte är garanterad där, och hade riskerat att ritas som "�".
    //
    // ADDERAD SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    const char* passwordMask;   // UTF-8, ett glyfsteg per kodpunkt

    // ---- DOCKNINGEN, forts. — handtagen för infällda
    // paneler, snappen, animationen och verktygsfältens band och rullgardin.
    // ETT block för hela dockningen, och det står SIST av positionsskälet:
    // varje block i den positionella
    // literalen är en chans till förskjutning. Flikraden i en flikgrupp är
    // flikkontrollens (`tabHeaderH`/`tabPadX`/`tabMinW`/`tabGap`) och
    // skiktet mellan två celler delarens (`splitterThick`) — flikar är
    // flikar, skikt är skikt. Handtagets ikon har listornas sida
    // (`iconFallback`) och luft (`iconGap`). Talen tillhör utseendepasset.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float dockHandleH;      // handtagsremsans tjocklek — ett handtag vid
                            // Left/Right är en kvadrat med den sidan
    float dockHandlePadX;   // handtagets textluft (Top/Bottom)
    float dockHandleGap;    // luften mellan två handtag på samma kant
    float dockSnapZone;     // snappens räckvidd under ett skiktdrag —
                            // ett BETEENDEMÅTT, som `dockDropZone`
    int   dockAnimMs;       // in- och utglidningens längd, 0 = ingen
    int   dockHideDelayMs;  // overlayns lämna-prövning, 0 = direkt
    float toolGripW;        // verktygsfältets grepp i en dockningsyta
    float toolArrowW;       // rullgardinspilens yta på en verktygsknapp
    float toolArrowGlyph;   // pilens tecken — rutans sida, centrerad
    float dockDropShare;    // ett rubrikdrag över en panel: den ANDEL (0–1)
                            // av cellens sida som HÖGST är en sidozon
                            // (`dockDropZone` är räckvidden) — mitten är en
                            // flik. Ett BETEENDEMÅTT, `treeDropEdge`s syskon

    // ---- FÄRGVÄLJAREN, DATUMVÄLJAREN OCH STATUSRADEN — ETT block:
    // färgväljarens rutmönster bakom en genomskinlig färg, datumväljarens
    // AUTO-bredd med klockslaget och statusradens fältluft. Talen tillhör
    // utseendepasset.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float colorCheckerCell;  // en ruta i rutmönstret (`Draw::checker`)
    float dateTimeDefaultW;  // datumväljarens AUTO-bredd med `showTime`
    float statusPanelPadX;   // statusradsfältets textluft på var sida

    // ---- AUTOSKROLL OCH TABBSTOPP — ETT block:
    // autoskrollen under drag-and-drop (`Widget::dragScroll`) och
    // TextAreas tabbstopp. Talen tillhör utseendepasset.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float dragScrollZone;     // autoskrollens zon i rullningsytans kant —
                              // ett BETEENDEMÅTT, som `dockSnapZone`
    float dragScrollStep;     // logiska px autoskrollen flyttar per tick
    int   dragScrollMs;       // autoskrollens tickperiod
    int   textAreaTabSpaces;  // tabbstoppets avstånd i blanksteg — ett
                              // ANTAL, som `textAreaRows`

    // ---- MARKDOWN-VYN — ETT block:
    // Markdown-vyns underrubrik och dokumentets luft. Brödtexten är
    // `uiFontSize`, H1 `headingFontSize`; ytorna är Panels (`panel`, kanten
    // `panelInset`) och remsan `scrollThick`. Talen tillhör utseendepasset.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float subheadingFontSize;    // H2 — `fontsize::kUiSubheading`, mellan
                                 // rubriken och brödtexten
    float markdownDefaultW;      // AUTO-bredden
    float markdownPad;           // luften mot ytan (höger minst `scrollThick`)
    float markdownBlockGap;      // mellan två block
    float markdownHeadingGap;    // EXTRA ovanför en rubrik
    float markdownListIndent;    // markörkolumnens minsta bredd
    float markdownMarkerGap;     // markören → texten
    float markdownBulletSize;    // punktens sida
    float markdownQuoteIndent;   // citatets indrag, strecket inräknat
    float markdownQuoteBarW;     // citatstrecket
    float markdownCodePad;       // kodblockets luft
    float markdownCodeSpanPadX;  // inline-kodens luft i sidled
    float markdownCellPadX;      // tabellcellens luft i sidled
    float markdownCellPadY;      // tabellcellens luft i höjdled

    // ---- DEN FLYTTBARA PANELEN (FloatingPanel).
    // Rubriken är dockningspanelens (`dockHeaderH`, `dockHeaderPadX`,
    // `dockGlyph`, `dockButtonInset`); eget är greppet och det minsta måttet.
    // Talen tillhör utseendepasset.
    //
    // ADDERADE SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float floatingPanelGrip;     // greppremsans höjd = greppets sida
    float floatingPanelMinW;     // det minsta måttet greppet ger — och
    float floatingPanelMinH;     //   auto-axelns

    // ---- ETIKETTENS GLORIA (`Label::setHalo`).
    // Bredden när appen inte satt någon (`setHaloWidth`); begränsas av
    // `overdraw`, för glorian sticker utanför etikettens yta. Talet tillhör
    // utseendepasset.
    //
    // ADDERAD SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float labelHaloWidth;        // glorians bredd i logiska px

    // ---- VERKTYGSFÄLTETS ÖVERFLÖDSKNAPP. Knappens
    // längd LÄNGS fältet (liggande bredden, stående höjden); tvärs är den
    // remsans innermått, ikonen (`StockIcon::MoreHorizontal`) verktygsknappens
    // 16 och luften kring ikonen det som blir över. Också fältets kortaste
    // längd i ett band (greppet, luften och knappen). Talet tillhör
    // utseendepasset.
    //
    // ADDERAD SIST i structen OCH sist i `kSlateRegular` i samma ändring
    // — ett fält insatt mitt i den positionella literalen KOMPILERAR och
    // förskjuter varje efterföljande mått.
    float toolOverflowW;         // överflödsknappens längd längs fältet
};

// ------------------------------------------------------- UI -> dess styles
//
// Vilka MÅTT-tabeller en ritning har, och i vilken ordning en väljare ska
// visa dem. Index 0 är ALLTID familjens default (det setUI återställer
// till). Det finns AVSIKTLIGT ingen platt global lista: `regular()` utan UI
// hade kunnat betyda vad som helst.
//
// Varje UI har exakt EN storlek, och den heter "Regular" i alla tre:
//
//   Glaze::Regular         knapphöjd 27.3, radie 6, font 13
//   Slate::Regular         knapphöjd 26.7, radie 4, font 13   (BASEN)
//   Minimalistic::Regular  knapphöjd 18,   radie 3, font 10
//
// Att en axel finns men bara har ett val är information, inte brus — och
// när en familj får sin andra storlek är det bara en rad till i tabellen.
int          styleCount(UI u);
const Style& styleAt(UI u, int index);   // klampas till giltigt index
const Style& defaultStyle(UI u);         // = styleAt(u, 0)

// `App::ThemeState::style` är ett VÄRDE (`Style`, inte `const Style*`) och
// initieras till `defaultStyle(UI::Glaze)`. Det finns alltså ingen
// nullptr-gren att glömma — state:t är komplett per KONSTRUKTION i stället
// för per konvention. Följden är att den som byter `ui` för hand också
// måste sätta `style` (det gör `AppWindow::setUI`).

} // namespace glaze
