#pragma once
#include <cstdint>

// RAMVERKETS ÖVERSÄTTNINGSKONSTANTER — id 1–999.
//
// HANDSKRIVEN, med flit: raderna växer i MÄNNISKOTAKT (en ny komponent
// lägger till en handfull — filväljaren 28, färgväljaren 4, datumväljaren
// 20), och en handskriven header kan bära grupperingen och skälen. Appens
// 1000+ genereras i stället av
// `glazelanguage pack --header`, eftersom de är många och byts ofta.
//
// TEXTERNA bor i `Glaze/language/framework.langtxt` och packas därifrån
// till `Glaze/language/language.bin`, som `glazelanguage embed` bäddar in
// i `lang_builtin.cpp`. INBÄDDNINGEN ÄR ETT KRAV och inte en optimering:
// libglaze får aldrig sakna "Avbryt" för att en fil fattas — ett bibliotek
// utan sina egna knapptexter är trasigt, inte osatt.
//
// VARJE konstant här måste finnas i ALLA FYRA språken (sv, en, nb,
// fi). `lang_gate` del 2 mäter det med DEN HÄR HEADERN som orakel — inte
// bin-filen, som annars hade fått godkänna sig själv.
//
// Id:n är STABILA ETIKETTER: ett id återanvänds aldrig för en annan
// betydelse, och en borttagen sträng lämnar sitt nummer tomt.

namespace glaze {
namespace str {

// --------------------------------------------- meddelanderutans knappar
// `Abort` (id 81, längre ned — ett id flyttas aldrig) är INTE `Cancel`:
// svenskan och norskan säger "Avbryt" om båda, men engelskan "Abort" mot
// "Cancel" och finskan "Keskeytä" mot "Peruuta".
inline constexpr std::uint32_t Ok     = 1;   // "OK"
inline constexpr std::uint32_t Cancel = 2;   // "Avbryt"
inline constexpr std::uint32_t Yes    = 3;   // "Ja"
inline constexpr std::uint32_t No     = 4;   // "Nej"
inline constexpr std::uint32_t Retry  = 5;   // "Försök igen"
inline constexpr std::uint32_t Ignore = 6;   // "Ignorera"

// ------------------------------------------------- genvägarnas VISNING
// SERIALISERINGEN är en annan sak: `.glazeui` bär alltid de KANONISKA
// engelska namnen ("Ctrl+Backspace"), medan de här bara syns i menyn.
// Utan den klyvningen hade en fil sparad på finska inte parsats på
// svenska.
inline constexpr std::uint32_t KeyEnter     = 7;    // "Retur"
inline constexpr std::uint32_t KeyEscape    = 8;    // "Esc"
inline constexpr std::uint32_t KeyTab       = 9;    // "Tabb"
inline constexpr std::uint32_t KeyBackspace = 10;   // "Backsteg"
inline constexpr std::uint32_t KeySpace     = 11;   // "Blanksteg"
inline constexpr std::uint32_t KeyDelete    = 12;   // "Del"
inline constexpr std::uint32_t KeyLeft      = 13;   // "Vänster"
inline constexpr std::uint32_t KeyRight     = 14;   // "Höger"
inline constexpr std::uint32_t KeyUp        = 15;   // "Upp"
inline constexpr std::uint32_t KeyDown      = 16;   // "Ner"
inline constexpr std::uint32_t KeyHome      = 17;   // "Home"
inline constexpr std::uint32_t KeyEnd       = 18;   // "End"

// Prefixen. `KeyNumPrefix` bär sitt avslutande blanksteg och modifierarna
// sitt plustecken — de KONKATENERAS med tangentnamnet, och separatorn är
// därför en del av översättningen (ett språk kan vilja ha en annan).
inline constexpr std::uint32_t KeyNumPrefix = 19;   // "Num "
inline constexpr std::uint32_t ModCtrl      = 20;   // "Ctrl+"
inline constexpr std::uint32_t ModAlt       = 21;   // "Alt+"
inline constexpr std::uint32_t ModShift     = 22;   // "Skift+"
inline constexpr std::uint32_t ModSuper     = 23;   // "Super+"

// DECIMALTECKNET (SpinBox). Ett tal som VISAS är text
// på användarens språk: svenska, norska och finska skriver "3,5", engelska
// "3.5". Tecknet bor här och inte i en lokaltabell i widgeten, för språket
// är redan dimensionen och fallbacken redan en GARANTI — och ett språkbyte
// tar då talfälten med sig utan att en enda widget behöver veta om det.
// (Det som TOLKAS är generösare: talfältet godtar både "," och ".".)
inline constexpr std::uint32_t DecimalSeparator = 24;   // ","

// NAVIGATIONSKLUSTRETS RESTERANDE NAMN — samma
// grupp som tangentnamnen ovan, men id:na tar vid där numreringen stod:
// ett id flyttas aldrig. Tangentbordsetiketterna (PgUp/PgDn/Ins) är
// desamma på alla fyra språken, precis som Home/End/Del. `KeyBegin` är
// numpadens mitttangent i navigationsläge och har ingen etikett, så den
// heter det den ÄR ("mitten") — engelskan behåller xkb:s namn.
inline constexpr std::uint32_t KeyPageUp   = 25;   // "PgUp"
inline constexpr std::uint32_t KeyPageDown = 26;   // "PgDn"
inline constexpr std::uint32_t KeyInsert   = 27;   // "Ins"
inline constexpr std::uint32_t KeyBegin    = 28;   // "Mitt"

// ------------------------------------------- FILVÄLJAREN
// Dialogen är VÅR och aldrig OS:ets, så varje sträng i den är ramverkets.
// Titlarna och OK-knappen per läge — "Öppna" är både titel och knapp,
// sparandet har två ("Spara som" i namnlisten, "Spara" på knappen), och
// mappläget "Välj mapp" för båda.
inline constexpr std::uint32_t FileOpen         = 29;   // "Öppna"
inline constexpr std::uint32_t FileSave         = 30;   // "Spara"
inline constexpr std::uint32_t FileSaveAs       = 31;   // "Spara som"
inline constexpr std::uint32_t FileSelectFolder = 32;   // "Välj mapp"
// Fillistans kolumner.
inline constexpr std::uint32_t FileColName      = 33;   // "Namn"
inline constexpr std::uint32_t FileColSize      = 34;   // "Storlek"
inline constexpr std::uint32_t FileColModified  = 35;   // "Ändrad"
inline constexpr std::uint32_t FileColType      = 36;   // "Typ"
// Etiketterna framför namnfältet (fil- respektive mappläget) och
// filtypslistans tooltip.
inline constexpr std::uint32_t FileNameLabel    = 37;   // "Filnamn:"
inline constexpr std::uint32_t FileFolderLabel  = 38;   // "Mapp:"
inline constexpr std::uint32_t FileTypeLabel    = 39;   // "Filtyp"
// TYP-kolumnen. En fil med ändelse visas som ändelsen i versaler PLUS
// `FileTypeSuffix` ("PNG" + "-fil") — suffixet bär sin skarv (bindestreck
// eller mellanslag), precis som `ModCtrl` bär sitt plustecken. En fil utan
// ändelse är bara `FileTypeGeneric`.
inline constexpr std::uint32_t FileFolderType   = 40;   // "Filmapp"
inline constexpr std::uint32_t FileTypeSuffix   = 41;   // "-fil"
inline constexpr std::uint32_t FileTypeGeneric  = 42;   // "Fil"
// Filtret som gäller när appen inte angett något.
inline constexpr std::uint32_t FileAllFiles     = 43;   // "Alla filer"
// Överskrivningsfrågan (meddelanderutan, Ja/Nej) — filnamnet står på egen
// rad OVANFÖR meningen, så ingen översättning behöver en platshållare.
inline constexpr std::uint32_t FileReplaceQuestion = 44;
// Felmeningarna i dialogens meddelanderad — HELA meningar, och sökvägen
// står redan i adressfältet.
inline constexpr std::uint32_t FileErrNoAccess      = 45;
inline constexpr std::uint32_t FileErrFolderMissing = 46;   // "Mappen finns inte."
inline constexpr std::uint32_t FileErrUnreadable    = 47;
inline constexpr std::uint32_t FileErrFileMissing   = 48;   // "Filen finns inte."
// Uppåtknappens tooltip (knappen är en ikon utan text).
inline constexpr std::uint32_t FileUpOneLevel   = 49;   // "Upp en nivå"
// De KÄNDA PLATSERNA (`gfx::knownPlaces`) — namnen följer APPENS språk och
// inte mappens namn på disken. Volymerna bär sitt eget namn ur OS:et.
inline constexpr std::uint32_t PlaceHome        = 50;   // "Hem"
inline constexpr std::uint32_t PlaceDesktop     = 51;   // "Skrivbord"
inline constexpr std::uint32_t PlaceDocuments   = 52;   // "Dokument"
inline constexpr std::uint32_t PlaceDownloads   = 53;   // "Hämtade filer"
inline constexpr std::uint32_t PlacePictures    = 54;   // "Bilder"
// Filväljarfältets knapp. Samma tecken på alla fyra språken i
// dag, men en etikett — ett språk får skriva "Bläddra…" utan att en
// widget-fil rörs.
inline constexpr std::uint32_t FilePickerBrowse = 55;   // "…"
// Dialogens kryssruta för DOLDA filer — punktnamnen och det OS:et
// döljer. Id:t står efter fältets, för ett id flyttas aldrig.
inline constexpr std::uint32_t FileShowHidden   = 56;   // "Visa dolda filer"

// ------------------------------------------- FÄRGVÄLJAREN
// Popupens etiketter: de tre reglagen och hexfältet. "Hex" är samma ord på
// alla fyra språken i dag — men en etikett, som filväljarfältets "…".
inline constexpr std::uint32_t ColorRed   = 57;   // "Röd"
inline constexpr std::uint32_t ColorGreen = 58;   // "Grön"
inline constexpr std::uint32_t ColorBlue  = 59;   // "Blå"
inline constexpr std::uint32_t ColorHex   = 60;   // "Hex"

// ------------------------------------------- DATUMVÄLJAREN
// Månadsrutnätets titel ("oktober 2026") och veckodagarnas korta namn, i
// ISO-ordning måndag–söndag — rutnätet ordnar om dem efter veckans första
// dag. Månadsnamnen skrivs som språket skriver dem (gemener på svenska).
inline constexpr std::uint32_t MonthJanuary   = 61;   // "januari"
inline constexpr std::uint32_t MonthFebruary  = 62;
inline constexpr std::uint32_t MonthMarch     = 63;
inline constexpr std::uint32_t MonthApril     = 64;
inline constexpr std::uint32_t MonthMay       = 65;
inline constexpr std::uint32_t MonthJune      = 66;
inline constexpr std::uint32_t MonthJuly      = 67;
inline constexpr std::uint32_t MonthAugust    = 68;
inline constexpr std::uint32_t MonthSeptember = 69;
inline constexpr std::uint32_t MonthOctober   = 70;
inline constexpr std::uint32_t MonthNovember  = 71;
inline constexpr std::uint32_t MonthDecember  = 72;   // "december"
inline constexpr std::uint32_t DayMonday      = 73;   // "må"
inline constexpr std::uint32_t DayTuesday     = 74;
inline constexpr std::uint32_t DayWednesday   = 75;
inline constexpr std::uint32_t DayThursday    = 76;
inline constexpr std::uint32_t DayFriday      = 77;
inline constexpr std::uint32_t DaySaturday    = 78;
inline constexpr std::uint32_t DaySunday      = 79;   // "sö"
// VECKANS FÖRSTA DAG — ett SPRÅKFAKTUM och ingen etikett, decimaltecknets
// (id 24) form: ISO-numret "1" (måndag) till "7" (söndag). "1" på alla fyra
// språken i dag; för engelskan är det ett skrivet val (ISO 8601, samma regel
// som datumväljarens fält — språket bär ingen region, och USA:s söndag är en
// regionsfråga). Ett ogiltigt värde ger måndag.
inline constexpr std::uint32_t FirstWeekday   = 80;   // "1"

// ------------------------------------- ÖVRIGA STRÄNGAR
// Tre strängar som inte hör till någon grupp ovan.
//
// MEDDELANDERUTANS "Abort" (AbortRetryIgnore), skild från `Cancel`: lika på
// svenska och norska ("Avbryt"), men engelskan säger "Abort" (inte "Cancel") och
// finskan "Keskeytä" (inte "Peruuta"), som Windows egen ruta.
inline constexpr std::uint32_t Abort         = 81;   // "Avbryt"
// FILSTORLEKENS ENHET för byte — läggs efter talet ("512 B") och efter
// SI-prefixet ("12 kB", "3,4 MB"). Prefixen k/M/G/T/P är internationella,
// men byte är det inte: finskan skriver tavu ("512 t", "12 kt", "3,4 Mt"),
// som Windows, GNOME och macOS gör på finska.
inline constexpr std::uint32_t FileSizeUnit  = 82;   // "B"
// macOS APPMENY: "Avsluta <app>" — prefixet med sitt avslutande
// blanksteg, och backenden lägger processnamnet efter
// (`gfx::setQuitMenuPrefix`).
inline constexpr std::uint32_t QuitAppPrefix = 83;   // "Avsluta "

// ----------------------------------- DOCKNINGEN
// Blocket 160–199 är dockningens.
//
// RUBRIKMENYNS FÖNSTERLÄGE: panelen i ett eget fönster, och tillbaka.
inline constexpr std::uint32_t DockFloat     = 160;  // "Flytande"
inline constexpr std::uint32_t DockDock      = 161;  // "Docka"
//
// PANELRUBRIKENS KNAPPNÅL — knappens tooltip och rubrikmenyns rad säger vad
// ett tryck GÖR: "Dölj automatiskt" på en fäst panel, "Fäst" på en som döljs
// automatiskt (Visual Studios Auto Hide/Pin).
inline constexpr std::uint32_t DockAutoHide  = 162;  // "Dölj automatiskt"
inline constexpr std::uint32_t DockPin       = 163;  // "Fäst"
// Stängknappens tooltip och rubrikmenyns sista rad.
inline constexpr std::uint32_t DockClose     = 164;  // "Stäng"
// Menyknappens tooltip — VS:s "Window Position".
inline constexpr std::uint32_t DockPosition  = 165;  // "Fönsterläge"
//
// DET FLYTANDE VERKTYGSFÄLTETS RUBRIK när fältet inte har
// någon egen `text`.
inline constexpr std::uint32_t DockToolBar   = 166;  // "Verktygsfält"

// ---------------------------------- FÄRGVÄLJAREN OCH FÄRGDIALOGEN
// Blocket 255–269 är färgväljarens och färgdialogens.
//
// FÄRGVÄLJARENS FJÄRDE REGLAGE — raden syns när alfan tillåts.
inline constexpr std::uint32_t ColorAlpha       = 255;  // "Alfa"
// FÄRGDIALOGENS TITEL när appen inte satt någon.
inline constexpr std::uint32_t ColorDialogTitle = 256;  // "Färg"

// ------------------------------------------- UTSKRIFTEN
// Blocket 400–409 är utskriftens.
//
// FÖRLOPPSFÖNSTRETS TITEL och ordet i sidraden ("Sida 3 / 10" — talen sätts
// efter ordet, så ordföljden är lika i alla fyra språken).
inline constexpr std::uint32_t PrintTitle = 400;  // "Skriver ut"
inline constexpr std::uint32_t PrintPage  = 401;  // "Sida"

// ------------------------- VERKTYGSFÄLTETS ÖVERFLÖDE
// Blocket 420–429 är överflödets.
//
// ÖVERFLÖDSKNAPPENS TOOLTIP (knappen är en ikon utan text), och posten för en
// inbäddad kontroll som inte har ett eget verktygstips.
inline constexpr std::uint32_t ToolBarMore    = 420;  // "Visa mer"
inline constexpr std::uint32_t ToolBarControl = 421;  // "Kontroll"

// NÄSTA LEDIGA ID ÄR 84 (blocket 160–199 är dockningens; 167 nästa där —
// 255–269 färgväljarens och färgdialogens, 257 nästa där — 400–409 utskriftens, 402 nästa
// där — och 420–429 verktygsfältets överflöde, 422 nästa där).
// Talet står som en KOMMENTAR och inte som en konstant: `lang_gate` del 2
// läser den här filen som TEXT och kräver att varje
// `inline constexpr std::uint32_t <Namn> = <tal>;` finns i alla fyra
// språken — en hjälpkonstant hade blivit ett id utan översättning.

} // namespace str
} // namespace glaze
