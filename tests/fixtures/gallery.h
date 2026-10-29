#pragma once
#include <glaze/glaze.h>

// Widget-galleriet — "allt måste synas i demot": alla tjugo widgets, var
// och en i minst TVÅ tillstånd (normalt och inaktiverat), så ett byte av
// UI/Theme/Style i startfönstrets väljare går att BEDÖMA på en skärm.
// Hover, tryck och öppen lista provas med musen — de iscensätts inte här.
//
// Tre kolumner med grupp-paneler; fönstrets EGEN namnlist är TopBar-
// widgeten i normalt läge (AppWindow skapar den vid Chrome::Custom) och
// dess knappar är CaptionButton i normalt läge — de inaktiverade
// motsvarigheterna står i grupp 9.
//
// FJÄRDE KOLUMNEN (x = 960): komponentlistans widgets från
// verktygsfältet och framåt. Fixturen är
// roundtrip_gates BREDASTE referens och pixel_gates
// korpus — en komponent som saknas HÄR mäts av ingen av dem. Den nya
// kolumnens inaktiverade noder sätts med METODANROPSSATSEN `disable();`
// så previewn och
// pixel_gate ser dem inaktiverade på riktigt.
//
// FEMTE KOLUMNEN (x = 1276): drag-and-drop. Fixturen är testdata
// och inte en skärm, så den får vara bredare än demons galleri — och en
// egen kolumn per spår håller samtidiga spår isär.
//
// SJÄTTE KOLUMNEN (x = 1592): trädet, normalt och
// inaktiverat — `setNodes` med den rekursiva literalen (en nod med bild,
// en utfälld gren), `setSelectedPath` och basens drag-egenskaper på det
// aktiva, vars två filer bär nodens FEMTE fält `false` (ett löv
// för släppet, massformen av `setAllowDropInto`).
// Det aktiva har FLERVAL och KRYSS (`setMultiSelect`/`setShowChecks`, ett
// kryss, en textfärg på en omarkerad nod och två valda) och det inaktiva
// ett SJU-fältsaggregat (kryssad och färgad) med kryssen synliga — så
// pixel_gate ritar rutorna och färgen i varje palett. Listvyerna och
// datagridden i åttonde kolumnen bär RADFÄRGEN (`setRowColor` och radens
// tredje fält). Under det TOOLTIPEN: basens setter-fält `setToolTip` på en
// knapp, en inaktiverad knapp, en verktygsknapp och på gruppen själv.
// `grpDra` bär basens två setter-fält (`setAllowDrag`/`setAllowDrop`)
// och tre av drag-eventen, en av dem i den KORTA formen.
//
// SJUNDE KOLUMNEN (x = 1908): delaren och dockningen.
// Delaren i tre former — stående med första rutan fast, liggande med andra
// rutan fast och minimum, och inaktiverad — med `Panel`-rutor som ramar in
// sig (en ruta utan ram hade inte ritat något eget mot gruppens yta, och
// pixel_gate mäter varje nivå för sig). Under den DOCKNINGEN: en värd med
// en vänster- och en bottenpanel och ett dokument i mitten, vars ordning
// en `setLayout` EFTER trädbygget vänder (botten ytterst — previewn utför
// raden), och en värd med en hopfälld toppanel och en högerpanel, båda
// inaktiverade. Ingen hopfälld panel vid en STÅENDE kant: den ritar ingen
// titel (ingen roterad text), och pixel_gate kräver att texten syns.
//
// ÅTTONDE KOLUMNEN (x = 2224): listvyn och datagriden.
// Den aktiva listvyn byggs STYCKE FÖR STYCKE (`addColumn`/`addRow` med och
// utan bild, `setSortKey`, `setCell`, `setSelected`) och den inaktiverade
// med MASSFORMEN (`setColumns`/`setRows` — aggregatliteralerna), så både
// grammatiken och previewn bär varje form. `onActivate` står i den KORTA
// formen. Datagriden likadant, med kolumntypen (`setColumnEditor`/
// `setColumnChoices`) FÖRE kolumnerna på den aktiva — typen hör till
// INDEXET, så satsordningen ska inte avgöra — och en aktuell cell i båda.
// Dess `onChange` bär `CellChangeArgs` (rad och kolumn).
// Båda listvyerna har KRYSSRUTOR (`setShowChecks` och ett
// `setChecked`), så pixel_gate ritar rutan och bocken aktiv och inaktiv i
// varje palett.
//
// NIONDE KOLUMNEN (x = 2540): flikarna. Flikkontrollens sidor är
// RIKTIGA BARN (`flikN.adopt(flikA);`) och fliktiteln är sidans `text`; den
// aktiva har `setSelectedIndex(1)` FÖRE trädbygget — valet ska gälla när den
// andra sidan kommer — och `setClosable(true)`, och `onTabClose` står i den
// KORTA formen. Den inaktiverade har två sidor utan innehåll. Fixturen förblir
// PLATT i övrigt: en dold sida syns inte, så pixel_gate mäter bara den valda
// sidans innehåll — varje komponent ska därför stå SYNLIG någon annanstans.
//
// TIONDE KOLUMNEN (x = 2856): filväljarfältet i sina tre lägen och
// inaktiverat. Läget och sökvägen är SETTER-fält (`setMode`/`setPath`) och
// filtren LISTAN i kollektionsvokabulärens tre former — `addFilter` med en
// literal och med `glaze::tr(43)` (ramverkets "Alla filer"), massformen
// `setFilters` med aggregatliteralen och `setFilter(index, …)` efter den.
// `onChange` står i båda formerna.
//
// ELFTE KOLUMNEN (x = 3172): färgväljaren. Färgen är ett SETTER-fält
// i två former — hextexten (`setColor("#3366FF")`, designerns) och
// `gfx::Color`-literalen (kodens) — på en AUTO-storad, en sträckt och en
// inaktiverad. Popupen syns inte här: pixel_gate mäter den stängda
// kontrollen, popupen grindas i colorpicker_gate.
//
// TOLFTE KOLUMNEN (x = 3488): datumväljaren. Datumet och gränserna
// är Call-satser i den PLATTA formen (tre heltal); den mittersta sätter
// gränserna FÖRE ett datum utanför dem, så previewn visar det KLAMPADE
// (satsordningen ska inte avgöra), och den sista är inaktiverad. `onChange`
// står i den KORTA formen.
//
// TRETTONDE KOLUMNEN (x = 3804): det flerradiga fältet. Texten är
// ett SETTER-fält vars literal bär RADBRYTNINGAR (`\n`) — och citattecken —
// som ska round-trippa byte-exakt; det första fältet har fler rader än det
// rymmer (den lodräta remsan), det andra är utan brytning med en rad bredare
// än fältet (den vågräta), därefter ett skrivskyddat och ett inaktiverat sida vid
// sida. `onChange` står i den fulla formen.
//
// KOLUMNHUVUDET PÅ FLERA RADER (grupp 26, i nionde kolumnen under flikarna,
// x = 2540, y = 324): en listvy med AUTO-höjd (`setHeaderLines(0)`
// ger fem rader ur den längsta rubriken), BOTTENJUSTERADE rubriker
// (`setHeaderVAlign`) och kolumnaggregatets FJÄRDE fält —
// rubrikerna centrerade, talen högerjusterade —
// så pilen står i det höga huvudet; och en inaktiverad datagrid med två
// rader, överkant och en rubrik som bryts automatiskt och kortas med "…".
// Rubrikerna bär `\n` i literalen, som ska round-trippa byte-exakt.
//
// RITYTAN (grupp 25, i tolfte kolumnen under datumväljaren):
// två ytor med RUTNÄT och FORMER I TVÅ LAGER (`addLayer` +
// `addShape` med och utan lagrets index — en yta med kontur, en streckad
// polylinje med spetsiga skarvar, en cirkel och en markör; en triangel och en
// linje med runda ändar). Den första är VRIDEN OCH TILTAD (`setCamera`
// designerad), den andra inaktiverad (innehållet tonas). Formlistan utförs av
// previewns ritytegren, så pixel_gate ser verkligt innehåll och
// inte bara ytan.
//
// RITYTAN I RYMDEN (grupp 27, i tionde kolumnen under filväljarfältet):
// Två ytor i 3D-MÅLET. Den första har en `Camera3D`, en
// LÅDA, en CYLINDER (stolpen), en LINJE I RYMDEN (Line3), en ETIKETT vid
// stolpens topp (Text3), en rad PUNKTER och en DEL-VY nere till höger — en
// ortografisk kub i ett eget lager (`setLayerViews(2, {1})`) som följer
// huvudvyns rotation. Den andra är inaktiverad (tonen läggs vid kompositen)
// och bär resten av fabrikerna: en yta i rymden, en sfär, ett rör, instanser
// av en cylinder och en mesh ur `Mesh::triangles`, under ett eget ljus. Allt
// UTFÖRS av previewns ritytegren, så pixel_gate mäter 3D-målets innehåll i
// varje palett och inte bara ytan.
//
// MARKDOWN-VYN (grupp 28, i elfte kolumnen under färgväljaren):
// en RULLANDE vy med ram och satt höjd vars dokument bär varje
// konstruktion i delmängden — tre rubriknivåer, betoning, kod, länkar,
// listor, citat, linje, kodblock, tabell, en asset-bild (`![](toolbar/info)`)
// och en som saknas — en INAKTIVERAD, och en AUTO-HÖG sträckt vy med en
// etikett som är syskonankrad under den (`anchorTop`), så etiketten följer
// vyns höjd. Dokumenten är EN literal med `\n` och `\"`, som ska round-trippa
// byte-exakt.
//
// STANDARDKNAPPEN: `setAcceptButton(btnN);` bland
// fönsteranropen — grupp 1:s aktiva knapp ritas i standardutseendet
// (accentytan), så pixel_gate mäter generiskt att den ritar, att den skiljer
// sig mellan aktiverad och inaktiverad (en inaktiverad knapp är aldrig
// standard) och att texten syns, i varje UI × tema × läge × stil.
//
// Tre-fils-modellen: den här filen har EN GlazeBuilder-region (medlemmar),
// .glazeui bär layouten och .cpp bara handler-kropparna. Tillstånd som
// inte går att uttrycka som en FÄLTSÄTTNING (disable(), showScrollbars(),
// setIndeterminate(), setRows()) sätts i onLoad — startlogikens hem.
class WidgetGallery : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    WidgetGallery();

private:
    glaze::StatusBar statusRad;
    glaze::Spacer statusLead;
    glaze::Label statusTxt;
    glaze::Spacer statusSp;
    glaze::Label statusHint;
    glaze::Spacer statusPad;
    glaze::Button stangBtn;
    glaze::Panel grpText;
    glaze::Label g1Rub;
    glaze::Separator g1Sep;
    glaze::Label lblN;
    glaze::Label lblD;
    glaze::Button btnN;
    glaze::Button btnD;
    glaze::Link lnkN;
    glaze::Separator g1SepV;
    glaze::Link lnkD;
    glaze::Separator g1SepD;
    glaze::Label g1Txt;
    glaze::Panel grpVal;
    glaze::Label g2Rub;
    glaze::Checkbox chkN;
    glaze::Checkbox chkD;
    glaze::Radio radA;
    glaze::Radio radB;
    glaze::Radio radD;
    glaze::Panel grpFalt;
    glaze::Label g3Rub;
    glaze::TextInput txtN;
    glaze::TextInput txtD;
    glaze::Dropdown ddN;
    glaze::Dropdown ddD;
    glaze::ListBox lstN;
    glaze::ListBox lstD;
    glaze::ListBox lstC;
    glaze::Label g3Txt;
    glaze::Panel grpReglage;
    glaze::Label g4Rub;
    glaze::Slider sldN;
    glaze::Slider sldD;
    glaze::Slider sldV;
    glaze::Progress prgN;
    glaze::Progress prgD;
    glaze::Progress prgI;
    glaze::Label g4Txt;
    glaze::Panel grpBild;
    glaze::Label g5Rub;
    glaze::Picture picN;
    glaze::Picture picD;
    glaze::Label g5Txt;
    glaze::Panel grpScroll;
    glaze::Label g6Rub;
    glaze::Scrollbar sbV1;
    glaze::Scrollbar sbV2;
    glaze::Scrollbar sbH1;
    glaze::Scrollbar sbH2;
    glaze::Label g6Txt;
    glaze::Panel grpPanel;
    glaze::Label g7Rub;
    glaze::Panel panScroll;
    glaze::Button psA;
    glaze::Button psB;
    glaze::Button psC;
    glaze::Button psD;
    glaze::Label psE;
    glaze::Panel panDis;
    glaze::Button pdA;
    glaze::Button pdB;
    glaze::Button pdC;
    glaze::Button pdD;
    glaze::Label g7Txt;
    glaze::Panel grpGrid;
    glaze::Label g8Rub;
    glaze::PropertyGrid grid1;
    glaze::PropertyGrid grid2;
    glaze::Label g8Txt;
    glaze::Panel grpBar;
    glaze::StatusBar statusD;
    glaze::Label sdTxt;
    glaze::Label g9Rub;
    glaze::Bar barN;
    glaze::Button barBtn1;
    glaze::Spacer barSp1;
    glaze::CaptionButton capMin;
    glaze::CaptionButton capMax;
    glaze::CaptionButton capClose;
    glaze::Bar barD;
    glaze::Button barBtn2;
    glaze::Spacer barSp2;
    glaze::CaptionButton capMinD;
    glaze::CaptionButton capMaxD;
    glaze::CaptionButton capCloseD;
    glaze::TopBar topBarD;
    glaze::Label tbTitle;
    glaze::Spacer tbSp;
    glaze::CaptionButton tbMin;
    glaze::CaptionButton tbClose;
    glaze::Label g9Txt;
    glaze::Panel grpVerktyg;
    glaze::Label g10Rub;
    glaze::ToolBar verktygN;
    glaze::ToolButton vkOk;
    glaze::ToolButton vkAvbryt;
    glaze::Separator vkSep1;
    glaze::ToolButton vkInfo;
    glaze::ToolButton vkKugg;
    glaze::Separator vkSep2;
    glaze::ToolButton vkText;
    glaze::ToolBar verktygD;
    glaze::ToolButton vkOkD;
    glaze::Separator vkSepD;
    glaze::ToolButton vkInfoD;
    glaze::ToolButton vkTextD;
    glaze::Label g10Txt;
    glaze::Panel grpGruppram;
    glaze::Label g11Rub;
    glaze::GroupBox gbN;
    glaze::Checkbox gbC1;
    glaze::Checkbox gbC2;
    glaze::GroupBox gbD;
    glaze::Radio gbR1;
    glaze::Radio gbR2;
    glaze::GroupBox gbTom;
    glaze::Label gbTomTxt;
    glaze::Label g11Txt;
    glaze::Panel grpTalfalt;
    glaze::Label g12Rub;
    glaze::SpinBox spN;
    glaze::SpinBox spD;
    glaze::SpinBox spAv;
    glaze::SpinBox spBred;
    glaze::Label g12Txt;
    glaze::Panel grpDra;
    glaze::Label draRub;
    glaze::Button draKalla;
    glaze::Panel draZon;
    glaze::Label draZonTxt;
    glaze::Panel draAv;
    glaze::Label draAvTxt;
    glaze::Label draTxt;
    glaze::Panel grpTrad;
    glaze::Label tradRub;
    glaze::TreeView tradN;
    glaze::TreeView tradD;
    glaze::Label tradTxt;
    glaze::Panel grpTip;
    glaze::Label tipRub;
    glaze::Button tipKnapp;
    glaze::Button tipAv;
    glaze::ToolButton tipVerktyg;
    glaze::Label tipTxt;
    glaze::Panel grpDela;
    glaze::Label delaRub;
    glaze::Splitter delaN;
    glaze::Panel delaNa;
    glaze::Label delaNaTxt;
    glaze::Panel delaNb;
    glaze::Label delaNbTxt;
    glaze::Splitter delaH;
    glaze::Panel delaHa;
    glaze::Panel delaHb;
    glaze::Splitter delaD;
    glaze::Panel delaDa;
    glaze::Panel delaDb;
    glaze::Label delaTxt;
    glaze::Panel grpLista;
    glaze::Label listaRub;
    glaze::ListView listaN;
    glaze::ListView listaD;
    glaze::Label listaTxt;
    glaze::Panel grpData;
    glaze::Label dataRub;
    glaze::DataGrid dataN;
    glaze::DataGrid dataD;
    glaze::Label dataTxt;
    glaze::Panel grpDock;
    glaze::Label dockRub;
    glaze::DockHost dockVard;
    glaze::DockPanel dockV;
    glaze::Label dockVTxt;
    glaze::DockPanel dockU;
    glaze::Label dockUTxt;
    glaze::Panel dockDok;
    glaze::Label dockDokTxt;
    glaze::DockHost dockVard2;
    glaze::DockPanel dockT;
    glaze::DockPanel dockR;
    glaze::Panel dockDok2;
    glaze::Label dockTxt;
    glaze::Panel grpFlik;
    glaze::Label flikRub;
    glaze::TabControl flikN;
    glaze::TabPage flikA;
    glaze::Label flikATxt;
    glaze::TabPage flikB;
    glaze::Button flikBKnapp;
    glaze::TabPage flikC;
    glaze::TabControl flikD;
    glaze::TabPage flikDa;
    glaze::TabPage flikDb;
    glaze::Label flikTxt;
    glaze::Panel grpFil;
    glaze::Label filRub;
    glaze::FilePicker filN;
    glaze::FilePicker filS;
    glaze::FilePicker filM;
    glaze::FilePicker filD;
    glaze::Label filTxt;
    glaze::Panel grpFarg;
    glaze::Label fargRub;
    glaze::ColorPicker fargN;
    glaze::ColorPicker fargB;
    glaze::ColorPicker fargD;
    glaze::Label fargTxt;
    glaze::Panel grpDatum;
    glaze::Label datumRub;
    glaze::DatePicker datumN;
    glaze::DatePicker datumG;
    glaze::DatePicker datumD;
    glaze::Label datumTxt;
    glaze::Panel grpAnt;
    glaze::Label antRub;
    glaze::TextArea antN;
    glaze::TextArea antB;
    glaze::TextArea antS;
    glaze::TextArea antD;
    glaze::Label antTxt;
    glaze::Panel grpRita;
    glaze::Label ritaRub;
    glaze::Canvas ritaN;
    glaze::Canvas ritaD;
    glaze::Label ritaTxt;
    glaze::Panel grpRubrik;
    glaze::Label rubrikRub;
    glaze::ListView rubrikN;
    glaze::DataGrid rubrikD;
    glaze::Label rubrikTxt;
    glaze::Panel grp3D;
    glaze::Label tredeRub;
    glaze::Canvas tredeN;
    glaze::Canvas tredeD;
    glaze::Label tredeTxt;
    glaze::Panel grpMd;
    glaze::Label mdRub;
    glaze::MarkdownView mdN;
    glaze::MarkdownView mdD;
    glaze::MarkdownView mdA;
    glaze::Label mdTxt;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);
    void stangClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void sldChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void gridChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void vkClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void spChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void draStart(glaze::Widget* sender, glaze::DragStartArgs& e);
    void zonIn();
    void zonSlapp(glaze::Widget* sender, glaze::DragArgs& e);
    void delaFlyttad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void listaAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void listaOppnad();
    void dataAndrad(glaze::Widget* sender, glaze::CellChangeArgs& e);
    void dockAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void flikBytt(glaze::Widget* sender, glaze::ChangeArgs& e);
    void flikStangs();
    void filAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void filSparad();
    void fargAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void datumAndrat();
    void antAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
};
