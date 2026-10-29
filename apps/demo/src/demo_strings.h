#pragma once
#include <cstdint>

// GENERERAD av `glazelanguage pack --header` — committas (regel 7)
// och kompileras som vanlig källkod. Bygget genererar ingenting.
// Att skriva raderna för hand är lika lagligt; formen är kontraktet.

namespace demo::str {

// "GlazeUI — demo (åäö ÅÄÖ)"
inline constexpr std::uint32_t Titel = 1000;
// "Arkiv"
inline constexpr std::uint32_t MenuArkiv = 1001;
// "Nytt fönster"
inline constexpr std::uint32_t NyttFonster = 1002;
// "Alla widgets…"
inline constexpr std::uint32_t AllaWidgets = 1003;
// "Avsluta"
inline constexpr std::uint32_t MenuAvsluta = 1004;
// "Utseende"
inline constexpr std::uint32_t MenuUtseende = 1005;
// "Läge"
inline constexpr std::uint32_t Lage = 1006;
// "System"
inline constexpr std::uint32_t LageSystem = 1007;
// "Ljust"
inline constexpr std::uint32_t LageLjust = 1008;
// "Mörkt"
inline constexpr std::uint32_t LageMorkt = 1009;
// "Animera rutan"
inline constexpr std::uint32_t AnimeraRutan = 1010;
// "Hjälp"
inline constexpr std::uint32_t MenuHjalp = 1011;
// "Om GlazeUI…"
inline constexpr std::uint32_t MenuOm = 1012;
// "GlazeUI ritar varenda pixel själv."
inline constexpr std::uint32_t Rubrik = 1013;
// "Namnlist, knappar och text är samma kod på Windows, macOS och Linux."
inline constexpr std::uint32_t Intro1 = 1014;
// "Fönstret är GENERERAD C++ — byt utseende nedan och se att allt följer med."
inline constexpr std::uint32_t Intro2 = 1015;
// "Utseendet — tre oberoende axlar"
inline constexpr std::uint32_t UtsRubrik = 1016;
// "UI — ritningen"
inline constexpr std::uint32_t UiLbl = 1017;
// "Tema — färgerna"
inline constexpr std::uint32_t ThemeLbl = 1018;
// "Style — måtten"
inline constexpr std::uint32_t StyleLbl = 1019;
// "Skala — allt växer"
inline constexpr std::uint32_t SkalaLbl = 1020;
// "Byt en — de andra står kvar."
inline constexpr std::uint32_t UtsHint = 1021;
// "Prova interaktionen"
inline constexpr std::uint32_t ProvaRubrik = 1022;
// "Tryck här"
inline constexpr std::uint32_t TryckHar = 1023;
// "Tema: System"
inline constexpr std::uint32_t TemaSystem = 1024;
// "Tema: Ljust"
inline constexpr std::uint32_t TemaLjust = 1025;
// "Tema: Mörkt"
inline constexpr std::uint32_t TemaMorkt = 1026;
// "Animera rutan bakom panelerna"
inline constexpr std::uint32_t AnimeraBakom = 1027;
// "Inga klick ännu"
inline constexpr std::uint32_t IngaKlick = 1028;
// "Nivå 1-komponenterna"
inline constexpr std::uint32_t KompRubrik = 1029;
// "GlazeUI på GitHub"
inline constexpr std::uint32_t KompLank = 1030;
// "Alternativ A"
inline constexpr std::uint32_t AlternativA = 1031;
// "Alternativ B"
inline constexpr std::uint32_t AlternativB = 1032;
// "Obestämt förlopp"
inline constexpr std::uint32_t ObestamtForlopp = 1033;
// "Panel — barnen klipps mot kanten"
inline constexpr std::uint32_t PanelRubrik = 1034;
// "I panelen"
inline constexpr std::uint32_t IPanelen = 1035;
// "Knappen nedanför klipps med flit."
inline constexpr std::uint32_t KlippsMedFlit = 1036;
// "Klipps & scrollas"
inline constexpr std::uint32_t KlippsScrollas = 1037;
// "Stäng"
inline constexpr std::uint32_t Stang = 1040;
// "Språk — texten byts i drift"
inline constexpr std::uint32_t SprakLbl = 1041;
// "Rensa status"
inline constexpr std::uint32_t RensaStatus = 1042;
// "Kopiera namnet till status"
inline constexpr std::uint32_t KopieraNamnet = 1043;
// "GlazeUI — samma kod på Windows, macOS och Linux."
inline constexpr std::uint32_t OmText = 1044;
// "Reglage: "
inline constexpr std::uint32_t SliderPrefix = 1045;
// "Länken öppnas i webbläsaren…"
inline constexpr std::uint32_t LankenOppnas = 1046;
// "Valde: "
inline constexpr std::uint32_t ValdePrefix = 1047;
// "Antal klick: "
inline constexpr std::uint32_t AntalKlick = 1048;
// "Musen är över knappen…"
inline constexpr std::uint32_t MusenOver = 1049;
// "Hej "
inline constexpr std::uint32_t HejPrefix = 1050;
// "!"
inline constexpr std::uint32_t HejSuffix = 1051;
// "Andra fönstret"
inline constexpr std::uint32_t AndraTitel = 1100;
// "Ett andra fönster — samma app, samma pump."
inline constexpr std::uint32_t AndraInfo = 1101;
// "Stäng huvudfönstret så följer det här med."
inline constexpr std::uint32_t AndraTips = 1102;
// "Typ"
inline constexpr std::uint32_t TypLbl = 1103;
// "Knappar"
inline constexpr std::uint32_t KnappLbl = 1104;
// "Egen bild i stället för badgen"
inline constexpr std::uint32_t EgenBild = 1105;
// "Om…"
inline constexpr std::uint32_t OmKnapp = 1106;
// "Fritt fönster"
inline constexpr std::uint32_t FrittFonster = 1107;
// "Visa meddelanderutan"
inline constexpr std::uint32_t VisaMeddelande = 1108;
// "Om: OK"
inline constexpr std::uint32_t OmSvarOk = 1109;
// "Om: Avbryt"
inline constexpr std::uint32_t OmSvarAvbryt = 1110;
// "Om: stängd"
inline constexpr std::uint32_t OmSvarStangd = 1111;
// "Radbrytningen är rutans egen: den här texten är medvetet lång nog att brytas över flera rader, så att både höjden och knappraden räknas om."
inline constexpr std::uint32_t MeddText = 1112;
// "Meddelanderuta"
inline constexpr std::uint32_t MeddTitel = 1113;
// "Svar: "
inline constexpr std::uint32_t SvarPrefix = 1114;
// "Avbryt (abort)"
inline constexpr std::uint32_t SvarAbort = 1115;
// "None — KONTRAKTSBROTT"
inline constexpr std::uint32_t SvarKontraktsbrott = 1116;
// "Om demon"
inline constexpr std::uint32_t OmTitel = 1120;
// "GlazeUI-demo"
inline constexpr std::uint32_t OmRubrik = 1121;
// "Ett eget GUI-ramverk i C++20."
inline constexpr std::uint32_t OmBrodtext = 1122;
// "Tema:"
inline constexpr std::uint32_t TemaLbl = 1123;
// "Fri geometri — texten sticker ut"
inline constexpr std::uint32_t FriGeometri = 1130;
// "Alla widgets — HANDKODAT (normalt och inaktiverat)"
inline constexpr std::uint32_t GalleriTitel = 1200;
// "1 · Label, Button, Link, Separator"
inline constexpr std::uint32_t G1 = 1201;
// "Label — normal"
inline constexpr std::uint32_t LabelNormal = 1202;
// "Label — inaktiverad"
inline constexpr std::uint32_t LabelAv = 1203;
// "Button"
inline constexpr std::uint32_t ButtonNormal = 1204;
// "Button av"
inline constexpr std::uint32_t ButtonAv = 1205;
// "Link — normal"
inline constexpr std::uint32_t LinkNormal = 1206;
// "Link — inaktiverad"
inline constexpr std::uint32_t LinkAv = 1207;
// "Separator: normal överst, inaktiverad här"
inline constexpr std::uint32_t SepFot = 1208;
// "2 · Checkbox, Radio"
inline constexpr std::uint32_t G2 = 1209;
// "Checkbox — normal"
inline constexpr std::uint32_t ChkNormal = 1210;
// "Checkbox — inaktiverad"
inline constexpr std::uint32_t ChkAv = 1211;
// "Inaktiverad"
inline constexpr std::uint32_t AltInaktiverad = 1212;
// "3 · TextInput, Dropdown, ListBox"
inline constexpr std::uint32_t G3 = 1213;
// "TextInput — normal"
inline constexpr std::uint32_t TxtNormal = 1214;
// "TextInput — inaktiverad"
inline constexpr std::uint32_t TxtAv = 1215;
// "TextInput — skrivskyddad"
inline constexpr std::uint32_t TxtSkrivskyddad = 1216;
// "Dropdown — normal"
inline constexpr std::uint32_t DdNormal = 1217;
// "Andra"
inline constexpr std::uint32_t DdAndra = 1218;
// "Tredje"
inline constexpr std::uint32_t DdTredje = 1219;
// "Dropdown — inaktiverad"
inline constexpr std::uint32_t DdAv = 1220;
// "ListBox — normal"
inline constexpr std::uint32_t LstNormal = 1221;
// "Rad 2"
inline constexpr std::uint32_t Rad2 = 1222;
// "Rad 3"
inline constexpr std::uint32_t Rad3 = 1223;
// "Rad 4"
inline constexpr std::uint32_t Rad4 = 1224;
// "Med kryssrutor"
inline constexpr std::uint32_t MedKryssrutor = 1225;
// "ListBox — inaktiverad"
inline constexpr std::uint32_t LstAv = 1226;
// "4 · Slider, Progress"
inline constexpr std::uint32_t G4 = 1227;
// "Progress: bestämt, inaktiverat, obestämt"
inline constexpr std::uint32_t PrgFot = 1228;
// "5 · Picture, Scrollbar"
inline constexpr std::uint32_t G5 = 1229;
// "Picture: normal, av, ur pixlar"
inline constexpr std::uint32_t PicFot = 1230;
// "Scrollbar: normal och inaktiverad"
inline constexpr std::uint32_t SbFot = 1231;
// "6 · Panel — klipp och overlay-remsor"
inline constexpr std::uint32_t G6 = 1232;
// "Knapp "
inline constexpr std::uint32_t KnappPrefix = 1233;
// "Av "
inline constexpr std::uint32_t AvPrefix = 1234;
// "…och en etikett till"
inline constexpr std::uint32_t EnEtikettTill = 1235;
// "Vänster: scrollbar panel. Höger: inaktiverad."
inline constexpr std::uint32_t PanFot = 1236;
// "7 · PropertyGrid"
inline constexpr std::uint32_t G7 = 1237;
// "8 · Bar, Spacer, CaptionButton, TopBar"
inline constexpr std::uint32_t G8 = 1238;
// "Bar-knapp"
inline constexpr std::uint32_t BarKnapp = 1239;
// "Bar av"
inline constexpr std::uint32_t BarAv = 1240;
// "TopBar — inaktiverad"
inline constexpr std::uint32_t TopBarAv = 1241;
// "StatusBar — inaktiverad"
inline constexpr std::uint32_t StatusBarAv = 1242;
// "Det här fönstret är HANDKODAT — ingen .glazeui, ingen region."
inline constexpr std::uint32_t GalleriStatus = 1243;
// "Startfönstret visar designervägen"
inline constexpr std::uint32_t GalleriHint = 1244;
// "Fönster"
inline constexpr std::uint32_t MenuFonster = 1245;
// "Rutnät: ändrad"
inline constexpr std::uint32_t GridAndrad = 1246;
// "Rutnät: "
inline constexpr std::uint32_t GridPrefix = 1247;
// "9 · ToolBar, ToolButton"
inline constexpr std::uint32_t G9 = 1248;
// "Text"
inline constexpr std::uint32_t VerktygText = 1249;
// "Övre remsan aktiv, nedre inaktiverad"
inline constexpr std::uint32_t VerktygFot = 1250;
// "Verktyg: "
inline constexpr std::uint32_t VerktygPrefix = 1251;
// " (på)"
inline constexpr std::uint32_t VerktygPa = 1252;
// " (av)"
inline constexpr std::uint32_t VerktygAvslagen = 1253;
// "10 · GroupBox"
inline constexpr std::uint32_t G10 = 1254;
// "Utskrift"
inline constexpr std::uint32_t GbUtskrift = 1255;
// "Dubbelsidig"
inline constexpr std::uint32_t GbDubbelsidig = 1256;
// "Svartvit"
inline constexpr std::uint32_t GbSvartvit = 1257;
// "Inaktiv ram"
inline constexpr std::uint32_t GbAv = 1258;
// "Stående"
inline constexpr std::uint32_t GbStaende = 1259;
// "Liggande"
inline constexpr std::uint32_t GbLiggande = 1260;
// "Inga mått — ramen följer sina barn"
inline constexpr std::uint32_t GbFot = 1261;
// "11 · SpinBox"
inline constexpr std::uint32_t G11 = 1262;
// "Heltal"
inline constexpr std::uint32_t SpHeltal = 1263;
// "Två decimaler"
inline constexpr std::uint32_t SpDecimaler = 1264;
// "Inaktiverat"
inline constexpr std::uint32_t SpAv = 1265;
// "Decimaltecknet följer språket"
inline constexpr std::uint32_t SpFot = 1266;
// "Talfält: "
inline constexpr std::uint32_t SpPrefix = 1267;
// "12 · Dra och släpp"
inline constexpr std::uint32_t G12 = 1280;
// "Dra mig"
inline constexpr std::uint32_t DraMig = 1281;
// "Släpp här"
inline constexpr std::uint32_t SlappHar = 1282;
// "Släppt: "
inline constexpr std::uint32_t Slappt = 1283;
// "Inget släpp — draget avbröts eller avvisades"
inline constexpr std::uint32_t IngetSlapp = 1284;
// "Dra knappen till rutan — eller tryck Escape"
inline constexpr std::uint32_t DraFot = 1285;
// "13 · TreeView"
inline constexpr std::uint32_t G13Trad = 1340;
// "Dokument"
inline constexpr std::uint32_t TradDokument = 1341;
// "Bilder"
inline constexpr std::uint32_t TradBilder = 1342;
// "Musik"
inline constexpr std::uint32_t TradMusik = 1343;
// "Semester"
inline constexpr std::uint32_t TradSemester = 1344;
// "Dra noder: före, efter eller in i en mapp · Ctrl/Skift väljer flera · rutan kryssar"
inline constexpr std::uint32_t TradFot = 1345;
// "Trädet: "
inline constexpr std::uint32_t TradPrefix = 1346;
// " (utfälld)"
inline constexpr std::uint32_t TradUtfalld = 1347;
// " (ihopfälld)"
inline constexpr std::uint32_t TradIhopfalld = 1348;
// "16 · Tooltip"
inline constexpr std::uint32_t G16Tip = 1349;
// "Spara"
inline constexpr std::uint32_t TipSpara = 1350;
// "Spara filen — Ctrl+S"
inline constexpr std::uint32_t TipSparaTips = 1351;
// "Kräver en öppen fil"
inline constexpr std::uint32_t TipAvTips = 1352;
// "Inställningar"
inline constexpr std::uint32_t TipKuggTips = 1353;
// "Antal kopior"
inline constexpr std::uint32_t TipSpinTips = 1354;
// "Panelen förklarar det som saknar en egen"
inline constexpr std::uint32_t TipPanelTips = 1355;
// "Kopior"
inline constexpr std::uint32_t TipBarn = 1356;
// "Vila pekaren en halv sekund"
inline constexpr std::uint32_t TipFot = 1357;
// "Dra en nod för att flytta den — en fil kan inte läggas överst, och i Musik sorterar appen in den"
inline constexpr std::uint32_t TradTips = 1358;
// "Trädet — öppnade: "
inline constexpr std::uint32_t TradOppnad = 1359;
// "Grundläggande"
inline constexpr std::uint32_t FlikGrund = 1360;
// "Text och listor"
inline constexpr std::uint32_t FlikText = 1361;
// "Paneler och remsor"
inline constexpr std::uint32_t FlikYtor = 1362;
// "Tabeller och träd"
inline constexpr std::uint32_t FlikData = 1363;
// "Layout"
inline constexpr std::uint32_t FlikLayout = 1364;
// "Interaktion"
inline constexpr std::uint32_t FlikInteraktion = 1365;
// "20 · TabControl"
inline constexpr std::uint32_t G20Flik = 1366;
// "Ny flik"
inline constexpr std::uint32_t FlikNy = 1367;
// "Allmänt"
inline constexpr std::uint32_t FlikAllmant = 1368;
// "Avancerat"
inline constexpr std::uint32_t FlikAvancerat = 1369;
// "Ctrl+Tab byter flik — krysset stänger"
inline constexpr std::uint32_t FlikFot = 1370;
// "Flikar: "
inline constexpr std::uint32_t FlikPrefix = 1371;
// "stängde "
inline constexpr std::uint32_t FlikStangd = 1372;
// "Sidans innehåll"
inline constexpr std::uint32_t FlikSidaText = 1373;
// "Dokument"
inline constexpr std::uint32_t FlikDokument = 1374;
// "Väljare"
inline constexpr std::uint32_t FlikValjare = 1375;
// "Rityta"
inline constexpr std::uint32_t FlikRityta = 1376;
// "3D"
inline constexpr std::uint32_t Flik3D = 1377;
// "3D-verktyg"
inline constexpr std::uint32_t Flik3DVerktyg = 1378;
// "14 · ListView"
inline constexpr std::uint32_t G14Lista = 1380;
// "Namn"
inline constexpr std::uint32_t ListaNamn = 1381;
// "Storlek"
inline constexpr std::uint32_t ListaStorlek = 1382;
// "Ändrad"
inline constexpr std::uint32_t ListaAndrad = 1383;
// "Sortera i huvudet — Ctrl/Skift väljer flera"
inline constexpr std::uint32_t ListaFot = 1384;
// "Listvyn — valda: "
inline constexpr std::uint32_t ListaValda = 1385;
// "Öppnade: "
inline constexpr std::uint32_t ListaOppnad = 1386;
// "18 · DataGrid"
inline constexpr std::uint32_t G18Data = 1390;
// "Värd"
inline constexpr std::uint32_t DataVard = 1391;
// "Port"
inline constexpr std::uint32_t DataPort = 1392;
// "Protokoll"
inline constexpr std::uint32_t DataProtokoll = 1393;
// "F2, dubbelklick eller skriv — Enter sparar"
inline constexpr std::uint32_t DataFot = 1394;
// "Datagriden — "
inline constexpr std::uint32_t DataPrefix = 1395;
// "15 · Splitter"
inline constexpr std::uint32_t G15Delare = 1420;
// "Första (fast)"
inline constexpr std::uint32_t DelareForsta = 1421;
// "Andra"
inline constexpr std::uint32_t DelareAndra = 1422;
// "Dra skiktet — stående, liggande, av"
inline constexpr std::uint32_t DelareFot = 1423;
// "Delaren: "
inline constexpr std::uint32_t DelarePrefix = 1424;
// "17 · DockHost"
inline constexpr std::uint32_t G17Dock = 1430;
// "Verktyg"
inline constexpr std::uint32_t DockVerktyg = 1431;
// "Utdata"
inline constexpr std::uint32_t DockUtdata = 1432;
// "Visa alla"
inline constexpr std::uint32_t DockVisa = 1433;
// "Återställ"
inline constexpr std::uint32_t DockAterstall = 1434;
// "Flikar och staplar: demo --dock"
inline constexpr std::uint32_t DockFot = 1435;
// "Dockningen: "
inline constexpr std::uint32_t DockPrefix = 1436;
// "Sparad: "
inline constexpr std::uint32_t DockSparad = 1437;
// "21 · FilePicker"
inline constexpr std::uint32_t G21Fil = 1440;
// "Bilder"
inline constexpr std::uint32_t FilBilder = 1441;
// "Textfiler"
inline constexpr std::uint32_t FilText = 1442;
// "Skriv en sökväg eller öppna dialogen med …"
inline constexpr std::uint32_t FilFot = 1443;
// "Filväljarfältet: "
inline constexpr std::uint32_t FilPrefix = 1444;
// "22 · ColorPicker"
inline constexpr std::uint32_t G22Farg = 1450;
// "Textfärg"
inline constexpr std::uint32_t FargText = 1451;
// "Så här ser texten ut"
inline constexpr std::uint32_t FargProv = 1452;
// "Sträckt"
inline constexpr std::uint32_t FargBred = 1453;
// "Palett, reglage eller hex — färgen gäller direkt"
inline constexpr std::uint32_t FargFot = 1454;
// "Färgväljaren: "
inline constexpr std::uint32_t FargPrefix = 1455;
// "23 · DatePicker"
inline constexpr std::uint32_t G23Datum = 1460;
// "Datum"
inline constexpr std::uint32_t DatumDatum = 1461;
// "Leverans"
inline constexpr std::uint32_t DatumLeverans = 1462;
// "Leverans: från i dag och 30 dagar fram"
inline constexpr std::uint32_t DatumFot = 1463;
// "Skriv ÅÅÅÅ-MM-DD eller välj i kalendern"
inline constexpr std::uint32_t DatumFot2 = 1464;
// "Datumväljaren: "
inline constexpr std::uint32_t DatumPrefix = 1465;
// "24 · TextArea"
inline constexpr std::uint32_t G24Ant = 1470;
// "Anteckningar\nSkriv flera rader — Enter ger en ny rad, och en lång rad bryts mot fältets bredd.\nRulla med hjulet, också utan att klicka i fältet.\nPageUp och PageDown bläddrar en sida.\nTab ger en tabb; Ctrl+Tab lämnar fältet."
inline constexpr std::uint32_t AntText = 1471;
// "Utan brytning rullar fältet i sidled när en rad inte får plats\nKort rad\nTredje raden"
inline constexpr std::uint32_t AntBryt = 1472;
// "Skrivskyddad:\nmarkera och kopiera"
inline constexpr std::uint32_t AntSkydd = 1473;
// "Inaktiverad:\ntexten dimmas"
inline constexpr std::uint32_t AntAv = 1474;
// "Enter ger en ny rad · Tab flyttar fokus"
inline constexpr std::uint32_t AntFot = 1475;
// "Anteckningen, rader: "
inline constexpr std::uint32_t AntPrefix = 1476;
// "25 · Canvas — karta"
inline constexpr std::uint32_t G25Karta = 1480;
// "25 · Canvas — graf"
inline constexpr std::uint32_t G25Graf = 1481;
// "25 · Canvas — kalibrering"
inline constexpr std::uint32_t G25Kal = 1482;
// "Plattor"
inline constexpr std::uint32_t LagerPlattor = 1483;
// "Ytor"
inline constexpr std::uint32_t LagerYtor = 1484;
// "Vägar"
inline constexpr std::uint32_t LagerVagar = 1485;
// "Etiketter"
inline constexpr std::uint32_t LagerEtiketter = 1486;
// "Badplats"
inline constexpr std::uint32_t KartaBadplats = 1487;
// "Utsiktsplats"
inline constexpr std::uint32_t KartaUtsikt = 1488;
// "Hållplats"
inline constexpr std::uint32_t KartaHallplats = 1489;
// "Information"
inline constexpr std::uint32_t KartaInfo = 1490;
// "mm: uppmätt"
inline constexpr std::uint32_t MmUppmatt = 1491;
// "mm: antagen"
inline constexpr std::uint32_t MmAntagen = 1492;
// "mm: kalibrerad"
inline constexpr std::uint32_t MmKalibrerad = 1493;
// "Dra panorerar · hjulet zoomar · Ctrl+dra vrider och lutar · klicka på en form"
inline constexpr std::uint32_t KartaFot = 1494;
// "Y uppåt · Skift+dra zoomar till rutan · dubbelklick visar allt"
inline constexpr std::uint32_t GrafFot = 1495;
// "Till höger samma graf, inaktiverad — innehållet tonas"
inline constexpr std::uint32_t GrafAvFot = 1496;
// "Mät linjen med en linjal och dra eller stega tills den är 100 mm — en riktig app sparar talet och sätter det vid start."
inline constexpr std::uint32_t KalFot = 1497;
// "Skärm: "
inline constexpr std::uint32_t KalSkarm = 1498;
// "linjen: "
inline constexpr std::uint32_t KalLinjen = 1499;
// "Kartan: "
inline constexpr std::uint32_t KartaPrefix = 1500;
// "Kartan: ingen form under pekaren"
inline constexpr std::uint32_t KartaIngen = 1501;
// "Kalibreringen sparad för "
inline constexpr std::uint32_t KalSparad = 1502;
// "Kalibreringen återställd för "
inline constexpr std::uint32_t KalAterstalld = 1503;
// "Skärmen har inget id — den går inte att kalibrera"
inline constexpr std::uint32_t KalIngetId = 1504;
// "Vridsnäpp: "
inline constexpr std::uint32_t SnappRubrik = 1505;
// "av"
inline constexpr std::uint32_t SnappAv = 1506;
// "norr"
inline constexpr std::uint32_t SnappNorr = 1507;
// "26 · Kolumnrubriker"
inline constexpr std::uint32_t G26Rubriker = 1510;
// "Spann"
inline constexpr std::uint32_t RubSpann = 1511;
// "Längd\na\n(m)"
inline constexpr std::uint32_t RubLangd = 1512;
// "Nedhäng\nvertikal\nprojektion\nb\n(m)"
inline constexpr std::uint32_t RubNedhang = 1513;
// "Lastfall\nSS-EN 50341"
inline constexpr std::uint32_t RubLastfall = 1514;
// "Beskrivning av stolpen och dess placering"
inline constexpr std::uint32_t RubBeskrivning = 1515;
// "Antal"
inline constexpr std::uint32_t RubAntal = 1516;
// "Höjd över mark (m)"
inline constexpr std::uint32_t RubHojd = 1517;
// "Auto, nederkant / två rader, överkant"
inline constexpr std::uint32_t RubFot = 1518;
// "27 · Canvas — scen i 3D"
inline constexpr std::uint32_t G27Scen = 1520;
// "Rutnät"
inline constexpr std::uint32_t Lager3dRutnat = 1521;
// "Stolpar"
inline constexpr std::uint32_t Lager3dStolpar = 1522;
// "Linor"
inline constexpr std::uint32_t Lager3dLinor = 1523;
// "Byggnader"
inline constexpr std::uint32_t Lager3dByggnader = 1524;
// "Träd"
inline constexpr std::uint32_t Lager3dTrad = 1525;
// "Punktmoln"
inline constexpr std::uint32_t Lager3dPunkter = 1526;
// "Stolpe"
inline constexpr std::uint32_t Etikett3dStolpe = 1527;
// "Nätstation"
inline constexpr std::uint32_t Etikett3dStation = 1528;
// "Skog"
inline constexpr std::uint32_t Etikett3dSkog = 1529;
// "Laserskanning"
inline constexpr std::uint32_t Etikett3dSkanning = 1530;
// "Primitiver, rör, linjer i rymden, instansierade träd, ett punktmoln färgat efter höjd över mark, en himmel ur ett eget program och terräng med draperade vägar"
inline constexpr std::uint32_t Fot3d = 1531;
// "Terräng"
inline constexpr std::uint32_t Lager3dTerrang = 1532;
// "Vägar"
inline constexpr std::uint32_t Lager3dVagar = 1533;
// "Skogsväg"
inline constexpr std::uint32_t Etikett3dVag = 1534;
// "Grusväg"
inline constexpr std::uint32_t Etikett3dGrusvag = 1535;
// "Miniatyren kunde inte ritas"
inline constexpr std::uint32_t MiniatyrFel = 1536;
// "Miniatyren ritad: "
inline constexpr std::uint32_t MiniatyrKlar = 1537;
// "28 · Canvas — 3D-verktyg"
inline constexpr std::uint32_t G28Verktyg = 1550;
// "Modell"
inline constexpr std::uint32_t Lager3dModell = 1551;
// "Kanter"
inline constexpr std::uint32_t Lager3dKanter = 1552;
// "Dolda kanter"
inline constexpr std::uint32_t Lager3dDolda = 1553;
// "Navigeringskub"
inline constexpr std::uint32_t Lager3dKub = 1554;
// "Ovan"
inline constexpr std::uint32_t KubOvan = 1555;
// "Under"
inline constexpr std::uint32_t KubUnder = 1556;
// "Fram"
inline constexpr std::uint32_t KubFram = 1557;
// "Bak"
inline constexpr std::uint32_t KubBak = 1558;
// "Vänster"
inline constexpr std::uint32_t KubVanster = 1559;
// "Höger"
inline constexpr std::uint32_t KubHoger = 1560;
// "Kantläge"
inline constexpr std::uint32_t Kantlage = 1561;
// "Miniatyr"
inline constexpr std::uint32_t Miniatyr = 1562;
// "Rutnätet följer kameran · klicka på kuben för en standardvy · kantläget ritar de dolda kanterna ljust och streckat · Miniatyr ritar scenen till en bild"
inline constexpr std::uint32_t FotVerktyg = 1563;
// "Förstaperson"
inline constexpr std::uint32_t Forstaperson = 1564;
// "Klicka på punktmolnet: närmaste punkt inom 8 px och 10 m · Förstaperson: W/A/S/D går, Q/E vrider, dra tittar"
inline constexpr std::uint32_t Fot3dKlick = 1565;
// "Punktmolnet: punkt "
inline constexpr std::uint32_t PunktPrefix = 1566;
// ", höjd över mark "
inline constexpr std::uint32_t PunktHag = 1567;
// "Punktmolnet: ingen punkt inom 8 px och 10 m"
inline constexpr std::uint32_t PunktIngen = 1568;
// "Markering"
inline constexpr std::uint32_t Lager3dMarkering = 1569;
// "GlazeUI — systemfältet"
inline constexpr std::uint32_t TrayTitel = 1570;
// "Appen bor i systemfältet: ikonen vid klockan är appens, och fönstret går att dölja och visa igen."
inline constexpr std::uint32_t TrayRad1 = 1571;
// "Klick på ikonen visar eller döljer fönstret, dubbelklick visar det och högerklick öppnar menyn."
inline constexpr std::uint32_t TrayRad2 = 1572;
// "Systemfältet finns inte på den här plattformen — fönstret stannar framme."
inline constexpr std::uint32_t TrayInteStod = 1573;
// "Animera ikonen"
inline constexpr std::uint32_t AnimeraIkonen = 1574;
// "Stäng till systemfältet"
inline constexpr std::uint32_t StangTillFaltet = 1575;
// "Visa fönstret"
inline constexpr std::uint32_t VisaFonstret = 1576;
// "Dölj fönstret"
inline constexpr std::uint32_t DoljFonstret = 1577;
// "GlazeUI-demo — klicka för att visa eller dölja fönstret"
inline constexpr std::uint32_t TrayTips = 1578;
// "Ikonens händelser visas här"
inline constexpr std::uint32_t TrayStatus = 1579;
// "Klick på ikonen"
inline constexpr std::uint32_t IkonKlick = 1580;
// "Dubbelklick på ikonen"
inline constexpr std::uint32_t IkonDubbelklick = 1581;
// "Högerklick på ikonen — menyn öppnas"
inline constexpr std::uint32_t IkonMeny = 1582;
// "Pekaren står på ikonen"
inline constexpr std::uint32_t PekarenPa = 1583;
// "Pekaren lämnade ikonen"
inline constexpr std::uint32_t PekarenAv = 1584;
// "29 · Lösenord, ångra och radbrytning"
inline constexpr std::uint32_t G29Losen = 1590;
// "Visa lösenordet"
inline constexpr std::uint32_t VisaLosen = 1591;
// "Texten bryts mot rutans bredd — dra skiktet så bryts den om."
inline constexpr std::uint32_t LosenBrod = 1592;
// "Raden under följer med."
inline constexpr std::uint32_t LosenBrodFot = 1593;
// "Ctrl+Z ångrar och Ctrl+Y gör om i varje textfält"
inline constexpr std::uint32_t LosenFot = 1594;
// "Kryssad: "
inline constexpr std::uint32_t TradKryssad = 1600;
// "Avkryssad: "
inline constexpr std::uint32_t TradAvkryssad = 1601;
// "Valda noder: "
inline constexpr std::uint32_t TradValda = 1602;
// "GlazeUI — dockningen"
inline constexpr std::uint32_t DocknTitel = 1700;
// "Visa"
inline constexpr std::uint32_t DocknVisa = 1701;
// "Utforskare"
inline constexpr std::uint32_t DocknUtforskare = 1702;
// "Sök"
inline constexpr std::uint32_t DocknSok = 1703;
// "Egenskaper"
inline constexpr std::uint32_t DocknEgenskaper = 1704;
// "Utdata"
inline constexpr std::uint32_t DocknUtdata = 1705;
// "Layout"
inline constexpr std::uint32_t DocknLayout = 1706;
// "Spara layouten"
inline constexpr std::uint32_t DocknSpara = 1707;
// "Återställ den sparade"
inline constexpr std::uint32_t DocknAterstall = 1708;
// "Startlayouten"
inline constexpr std::uint32_t DocknStart = 1709;
// "Layout: "
inline constexpr std::uint32_t DocknLayoutPrefix = 1710;
// "Sparad: "
inline constexpr std::uint32_t DocknSparadPrefix = 1711;
// "Dokumentet är huvudytan — det som blir kvar när panelerna och verktygsfälten tagit sin plats. Dra en panels rubrik eller flik, eller ett verktygsfälts grepp — fönstret följer med. Släpp där platshållaren syns för att docka: mitt på en annan panel blir den en flik, vid dess kant en stapel, vid fönstrets kant en ny remsa. Släppt någon annanstans flyter den; dubbelklicka på rubriken för att docka tillbaka."
inline constexpr std::uint32_t DocknDokument = 1712;
// "Sök efter:"
inline constexpr std::uint32_t DocknSokEtikett = 1713;
// "Fäll in en panel med pilen i rubriken — den blir ett handtag på kanten, och ett klick fäller ut den igen. Visa-menyn tar fram en stängd panel."
inline constexpr std::uint32_t DocknUtdataText = 1714;
// "Utforskaren är minst 160 och högst 400 px bred."
inline constexpr std::uint32_t DocknMinMax = 1715;
// "Ingen layout sparad än"
inline constexpr std::uint32_t DocknIngenSparad = 1716;
// "Nytt verktygsfält"
inline constexpr std::uint32_t DocknNyttFalt = 1717;
// "Kör"
inline constexpr std::uint32_t DocknKor = 1718;
// "Stega"
inline constexpr std::uint32_t DocknStega = 1719;
// "Klipp ut"
inline constexpr std::uint32_t DocknKlipp = 1720;
// "Kopiera"
inline constexpr std::uint32_t DocknKopiera = 1721;
// "Klistra in"
inline constexpr std::uint32_t DocknKlistra = 1722;
// "Extra"
inline constexpr std::uint32_t DocknExtra = 1723;
// "Verktyg: "
inline constexpr std::uint32_t DocknVerktygPrefix = 1724;
// "Bevakning"
inline constexpr std::uint32_t DocknBevakning = 1725;
// "Bevakningen startar flytande, i ett eget fönster. Dra dess rubrik — fönstret följer med — och släpp där platshållaren syns för att docka den, eller dubbelklicka på rubriken (eller välj Docka i dess meny) så dockas den vid högerkanten."
inline constexpr std::uint32_t DocknBevakningText = 1726;
// "Standard"
inline constexpr std::uint32_t DocknStandard = 1727;
// "Redigera"
inline constexpr std::uint32_t DocknRedigera = 1728;
// "Felsök"
inline constexpr std::uint32_t DocknFelsok = 1729;
// "Verktyg"
inline constexpr std::uint32_t DocknVerktyg = 1730;
// "Ny"
inline constexpr std::uint32_t VerktygNy = 1740;
// "Ångra"
inline constexpr std::uint32_t VerktygAngra = 1741;
// "Dokument"
inline constexpr std::uint32_t NyDokument = 1742;
// "Mapp"
inline constexpr std::uint32_t NyMapp = 1743;
// "Skriv text"
inline constexpr std::uint32_t HistorikSkriv = 1744;
// "Flytta ruta"
inline constexpr std::uint32_t HistorikFlytta = 1745;
// "Radera rad"
inline constexpr std::uint32_t HistorikRadera = 1746;
// "Menyval: "
inline constexpr std::uint32_t MenyvalPrefix = 1747;
// "Ångrat: "
inline constexpr std::uint32_t AngratPrefix = 1748;
// "Ångra är delad: pilen visar historiken"
inline constexpr std::uint32_t RullgardinFot = 1749;
// "GlazeUI — startar"
inline constexpr std::uint32_t SplashTitel = 1750;
// "GlazeUI startar…"
inline constexpr std::uint32_t SplashRubrik = 1751;
// "Ett ramlöst fönster mitt på skärmen, utan knapp i aktivitetsfältet. En timer i .glazeui stänger det efter en och en halv sekund."
inline constexpr std::uint32_t SplashText = 1752;
// "GlazeUI — fönsterlabbet"
inline constexpr std::uint32_t LabTitel = 1760;
// "Geometri: "
inline constexpr std::uint32_t LabGeometri = 1761;
// "Normal"
inline constexpr std::uint32_t LabNormal = 1762;
// "Minimerat"
inline constexpr std::uint32_t LabMinimerat = 1763;
// "Maximerat"
inline constexpr std::uint32_t LabMaximerat = 1764;
// "ingen position"
inline constexpr std::uint32_t LabIngenPosition = 1765;
// "Maximera"
inline constexpr std::uint32_t LabMaximera = 1766;
// "Minimera"
inline constexpr std::uint32_t LabMinimera = 1767;
// "Spara geometrin"
inline constexpr std::uint32_t LabSpara = 1768;
// "Öppna nytt med den sparade"
inline constexpr std::uint32_t LabOppnaSparad = 1769;
// "Sparad: "
inline constexpr std::uint32_t LabSparad = 1770;
// "Ingen geometri sparad än"
inline constexpr std::uint32_t LabIngetSparat = 1771;
// "Pekarna — håll musen över rutorna"
inline constexpr std::uint32_t LabPekarna = 1772;
// "Hand"
inline constexpr std::uint32_t LabHand = 1773;
// "Stängd hand"
inline constexpr std::uint32_t LabStangdHand = 1774;
// "Dold"
inline constexpr std::uint32_t LabDold = 1775;
// "Vänta"
inline constexpr std::uint32_t LabVanta = 1776;
// "Väntepekare över hela fönstret"
inline constexpr std::uint32_t LabVantaHela = 1777;
// "Det nya fönstret är redan öppet — stäng det först"
inline constexpr std::uint32_t LabNyttOppet = 1778;
// "30 · Inmatning"
inline constexpr std::uint32_t G30Inmatning = 1800;
// "Ctrl+Enter skickar, Enter ger ny rad"
inline constexpr std::uint32_t InSkickaTips = 1801;
// "Skickat: "
inline constexpr std::uint32_t InSkickat = 1802;
// "Dra med höger knapp, dubbelklicka med mittenknappen, håll mellanslag — klicka i ytan först"
inline constexpr std::uint32_t InYtaTips = 1803;
// "Höger ned"
inline constexpr std::uint32_t InHogerNer = 1804;
// "Höger upp: "
inline constexpr std::uint32_t InHogerUpp = 1805;
// "Mitten dubbelklick"
inline constexpr std::uint32_t InMittenDubbel = 1806;
// "Höger dubbelklick"
inline constexpr std::uint32_t InHogerDubbel = 1807;
// "Trycket avbröts"
inline constexpr std::uint32_t InAvbrutet = 1808;
// "Mellanslag hålls"
inline constexpr std::uint32_t InMellanslagHalls = 1809;
// "Mellanslag släppt"
inline constexpr std::uint32_t InMellanslagSlappt = 1810;
// "Mellanslag släppt (avbrutet)"
inline constexpr std::uint32_t InMellanslagAvbrutet = 1811;
// "Flytta framåt"
inline constexpr std::uint32_t KartaLagerFram = 1820;
// "Flytta bakåt"
inline constexpr std::uint32_t KartaLagerBak = 1821;
// "Lager: "
inline constexpr std::uint32_t KartaLagerPrefix = 1822;
// "Lagerordning: "
inline constexpr std::uint32_t KartaLagerOrdning = 1823;
// "Lösenord (högst 16 tecken)"
inline constexpr std::uint32_t LosenPlats = 1830;
// "Skriv en anteckning …"
inline constexpr std::uint32_t AntPlats = 1831;
// "Flyttade noder: "
inline constexpr std::uint32_t TradFlyttade = 1840;
// "Sorterade in alfabetiskt: "
inline constexpr std::uint32_t TradSorterade = 1841;
// "Rutan framför namnet kryssar raden — valet står kvar"
inline constexpr std::uint32_t ListaTips = 1842;
// "31 · Kryssruta med tre lägen"
inline constexpr std::uint32_t G31Tre = 1865;
// "Tre lägen — klicka runt"
inline constexpr std::uint32_t TreKlick = 1866;
// "Alla lager"
inline constexpr std::uint32_t TreAlla = 1867;
// "Mellanläge, inaktiverad"
inline constexpr std::uint32_t TreAv = 1868;
// "Klick: av → på → mellanläge"
inline constexpr std::uint32_t TreFot = 1869;
// "32 · ListBox med flerval"
inline constexpr std::uint32_t G32Flerval = 1870;
// "Ctrl vänder · Skift ger spannet · Ctrl+A alla"
inline constexpr std::uint32_t FlervalFot = 1871;
// "Valda rader: "
inline constexpr std::uint32_t ValdaRader = 1872;
// "33 · DataGrid med radval"
inline constexpr std::uint32_t G33Radval = 1873;
// "Hela rader — Ctrl och Skift väljer flera"
inline constexpr std::uint32_t RadvalFot = 1874;
// "Klockslag"
inline constexpr std::uint32_t DatumKlockslag = 1875;
// "34 · Färgdialog och inmatning"
inline constexpr std::uint32_t G34Dialoger = 1876;
// "Färgdialog …"
inline constexpr std::uint32_t DialogFarg = 1877;
// "Byt namn …"
inline constexpr std::uint32_t DialogNamn = 1878;
// "Byt namn"
inline constexpr std::uint32_t DialogNamnRubrik = 1879;
// "Lagrets nya namn:"
inline constexpr std::uint32_t DialogPrompt = 1880;
// "Nytt namn: "
inline constexpr std::uint32_t DialogNytt = 1881;
// "Dialogen avbröts"
inline constexpr std::uint32_t DialogAvbruten = 1882;
// "Modala — Enter är OK och Escape Avbryt"
inline constexpr std::uint32_t DialogFot = 1883;
// "Färgdialogen: "
inline constexpr std::uint32_t FargdialogPrefix = 1884;
// "Ta bort"
inline constexpr std::uint32_t SnabbTaBort = 1900;
// "Snabbmeny för: "
inline constexpr std::uint32_t SnabbmenyFor = 1901;
// "36 · Etiketter och knappar"
inline constexpr std::uint32_t G36Etiketter = 1902;
// "En lång etikett som kortas med en ellips när den inte får plats i sin bredd"
inline constexpr std::uint32_t EtikettLang = 1903;
// "Röd"
inline constexpr std::uint32_t RadioRod = 1904;
// "Blå"
inline constexpr std::uint32_t RadioBla = 1905;
// "Välj en bild"
inline constexpr std::uint32_t FilDialogTitel = 1906;
// "35 · Storlek och tangenter"
inline constexpr std::uint32_t G35Storlek = 1920;
// "Ytan: "
inline constexpr std::uint32_t YtanPrefix = 1921;
// "Dra i delaren · högerklicka i ytan · skriv i fältet (å, AltGr+2, ´ och e)"
inline constexpr std::uint32_t G35Tips = 1922;
// "Markera här"
inline constexpr std::uint32_t MarkeraHar = 1923;
// "Tangent: "
inline constexpr std::uint32_t TangentPrefix = 1924;
// "text: "
inline constexpr std::uint32_t TextPrefix = 1925;
// "Innehåll: "
inline constexpr std::uint32_t LabInnehall = 1926;
// "Aktivt fönster: "
inline constexpr std::uint32_t LabAktivt = 1927;
// "inget"
inline constexpr std::uint32_t LabInget = 1928;
// "Följ innehållet"
inline constexpr std::uint32_t LabFolj = 1929;
// "Fler rader"
inline constexpr std::uint32_t LabFlerRader = 1930;
// "Kryssa i rutan: dialogen växer med raderna och krymper igen. Gör den bredare, så följer höjden texten."
inline constexpr std::uint32_t LabFoljText = 1931;
// "Mät"
inline constexpr std::uint32_t DocknMat = 1932;
// "Genvägen M kördes"
inline constexpr std::uint32_t DocknMatKord = 1933;
// "Dockas bara uppe eller nere"
inline constexpr std::uint32_t DocknKanterTips = 1934;
// "Grävtillstånd: ledningsarbete längs Kyrkvägen hela hösten. Vägen är avstängd nattetid mellan 22 och 06, och gående leds om via Strandstigen."
inline constexpr std::uint32_t KartaAnteckning = 1935;
// "Formulär…"
inline constexpr std::uint32_t LabFormular = 1950;
// "Formulär"
inline constexpr std::uint32_t FormTitel = 1951;
// "Kontakt"
inline constexpr std::uint32_t FormKontakt = 1952;
// "E-postadress"
inline constexpr std::uint32_t FormEpost = 1953;
// "Telefon"
inline constexpr std::uint32_t FormTelefon = 1954;
// "Adress"
inline constexpr std::uint32_t FormAdress = 1955;
// "Gatuadress"
inline constexpr std::uint32_t FormGata = 1956;
// "Postnummer och ort"
inline constexpr std::uint32_t FormPostort = 1957;
// "Kontakter"
inline constexpr std::uint32_t FormKontakter = 1958;
// "Visa kontakterna"
inline constexpr std::uint32_t FormVisa = 1959;
// "Lås storleken"
inline constexpr std::uint32_t FormLas = 1960;
// "37 · Markdown"
inline constexpr std::uint32_t G37Markdown = 1970;
// "Länk: "
inline constexpr std::uint32_t MdLank = 1971;
// "# Markdown-vyn\n\nEn **formaterad** text i en *rullande* yta — med `kod`, [en länk](https://github.com/holmbom/GlazeUI \"GlazeUI på GitHub\") och bilder: ![Info](ikon) ![Kugghjul](toolbar/gear)\n\n## Listor\n\n- en punkt med **fet** text\n- [byt till fliken Tabeller och träd](demo:data)\n  - en nästlad punkt\n\n1. ett\n2. två\n\n> Ett citat\nmed en lat rad.\n\n---\n\n## Kod\n\n```\nfor (int i = 0; i < 3; ++i)\n    rita(i);\n```\n\n| Vänster | Mitten | Höger |\n|:--|:-:|--:|\n| a | **b** | 12 |\n| c | `d` | 345 |\n\n[Till toppen](#markdown-vyn) · [Till koden](#kod)"
inline constexpr std::uint32_t MdExempel = 1972;
// "En **kort** vy med AUTO-höjd: den växer när fönstret görs smalare."
inline constexpr std::uint32_t MdKort = 1973;
// "Raden följer den korta vyns höjd."
inline constexpr std::uint32_t MdFot = 1974;
// "Terminal"
inline constexpr std::uint32_t DocknTerminal = 2000;
// "Programmet i terminalen avslutades med kod "
inline constexpr std::uint32_t TermSlut = 2001;
// "Terminalen startade inte: "
inline constexpr std::uint32_t TermFel = 2002;
// "Läge: automatiskt"
inline constexpr std::uint32_t InmLageAuto = 2010;
// "Läge: karta"
inline constexpr std::uint32_t InmLageKarta = 2011;
// "Läge: orbit"
inline constexpr std::uint32_t InmLageOrbit = 2012;
// "Läge: förstaperson"
inline constexpr std::uint32_t InmLageForsta = 2013;
// "Läge: mäta"
inline constexpr std::uint32_t InmLageMata = 2014;
// "Vänd hjulet"
inline constexpr std::uint32_t InmVandHjul = 2015;
// "Vänd styrplattan"
inline constexpr std::uint32_t InmVandPlatta = 2016;
// "Vänd zoomen"
inline constexpr std::uint32_t InmVandZoom = 2017;
// "Hjulet rullar åt andra hållet i den här listan"
inline constexpr std::uint32_t InmListaVand = 2018;

} // namespace demo::str
