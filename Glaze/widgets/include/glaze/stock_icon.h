#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "glaze/canvas/image_source.h"

// STANDARDIKONERNA — ramverkets egna ikoner för det
// varje app behöver: spara, ångra, sök, zooma, spela … Appen lägger dem inte i
// sin assets.bin; de nås genom en uppräkning:
//
//     sparaBtn.setImg(glaze::stockIcon(glaze::StockIcon::Save));
//     zoomBtn.setImg(glaze::stockIcon(glaze::StockIcon::ZoomIn, 24));
//
// Svaret är ett `ImageSource`, så ikonen går överallt där en bild tas emot —
// knappar, menyposter, trädets och tabellernas rader, `Picture`, ritytan,
// fönstrets och systemfältets ikon.
//
// IKONERNA ÄR VEKTORER, inte bilder: varje ikon är en ritbeskrivning i
// src/stock_icon.cpp — linjer, bågar, rektanglar och cirklar i en ruta på
// 24 × 24 med EN streckbredd för alla — som RASTRERAS när den ritas, i
// fönstrets FYSISKA px. Därför följer den
//   * TÄTHETEN — 1×, 2× och fraktionerad skala ger alla en skarp bild,
//     rastrerad i exakt den storlek den ritas (aldrig en skalad bitmapp) och
//     fäst mot pixelrutnätet: strecket är ett helt antal px och det raka
//     står på hela pixlar, så bara det sneda och det runda kantutjämnas;
//   * TEMAT — ikonen färgas med temats textfärg (`Theme::Colors::text`) i det
//     läge fönstret ritar i, och färgvariantens accenter med temats
//     `stock*`-roller, så ljust och mörkt följer utan att appen gör något;
//   * STORLEKSKLASSEN — 16, 24, 32 och 48 logiska px (`sizeClass()`, som en
//     assets storleksklass: widgetarna ger ikonen den sidan).
// Stilen: en kontur med raka ändar och skarvar, ett streck på en hel fysisk
// px upp till 31 px och två från 32, och färgade ytor (stilen nedan). En
// INAKTIV kontroll tonar den som varje annan bild (`Draw::stateGlyph`:
// opacitet 1 − `Style::iconFade`), och `IconStyle::Auto` byter dessutom till
// Mono.
//
// Rastreringen cachas per (ikon, fysisk storlek, färg) i processen — UI-
// tråden, som all ritning. En OS-ikon (`AppWindow::appIcon`, `TrayIcon`) av en
// standardikon rastreras i appens aktuella textfärg när den sätts.
//
// APPENS EGNA IKONER ritas med SAMMA motor och SAMMA språk (`registerIcon`,
// `GLAZE_REGISTER_ICON` och `drawnIcon` längst ned) — lagren, färgerna,
// tonerna, gradienterna, kontrastkraven, delarna och pixelfästningen. Det är
// förstahandsvalet för en apps ikoner; en PNG i assets.bin är för det som
// inte går att rita.
//
// Regel 8: `glaze::stockIcon(glaze::StockIcon::Save)`,
// `glaze::stockIcon(glaze::StockIcon::Save, 24)`,
// `glaze::stockIcon(glaze::StockIcon::Save, 24, glaze::IconStyle::Color)` och
// `glaze::drawnIcon("Spara", 24, glaze::IconStyle::Auto)` är bildargument i
// `.glazeui` precis som `glaze::img(...)`, och previewn ritar dem (en appikon
// som previewn inte känner ritas som `StockIcon::Image` och loggas).

namespace glaze {

// En post per ikon. Namnen säger vad ikonen BETYDER (Save, ZoomIn); bilden
// är den vedertagna (disketten, förstoringsglaset med plus). Grupperna står
// i uppräkningen nedan. FILFORMATEN
// (`File` + ändelsen — `FileDwg`, `FileShp`, `FilePng`) är sidan med en liten
// symbol och ändelsen UTSTANSAD ur en etikett i kategorins färg; den läses från
// 24 px, i 16 px bär etikettens färg typen.
enum class StockIcon : std::uint16_t {
    // Navigering
    Back, Forward, Up, Down, Home, First, Previous, Next, Last, Refresh, Stop,
    GoTo,
    // Pilar — som pil, som vinkel och som dubbelvinkel, och rörelserna
    ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
    ChevronLeft, ChevronRight, ChevronUp, ChevronDown,
    ChevronsLeft, ChevronsRight, ChevronsUp, ChevronsDown,
    Flip, RotateClockwise, RotateCounterClockwise, Swap,
    // Fil och dokument
    New, Open, Save, SaveAs, SaveAll, Close, Print, PrintPreview, Import,
    Export, Properties, File, Document, Folder, FolderOpen, NewFolder, Recent,
    // Lagring och plats
    Drive, Network, Cloud, HomeFolder, Desktop, Trash, Download, Upload, Sync,
    // Redigering
    Undo, Redo, Cut, Copy, Paste, Delete, SelectAll, Search, Replace, Clear,
    Edit, Rename, Duplicate,
    // Listor och objekt
    Add, Remove, MoveUp, MoveDown, MoveFirst, MoveLast, SortAscending,
    SortDescending, Filter, Group, Expand, Collapse, ExpandAll, CollapseAll,
    // Vyer
    ListView, DetailsView, IconView, Grid, SplitView, Sidebar, FullScreen,
    ExitFullScreen,
    // Zoom och visning
    ZoomIn, ZoomOut, ZoomToFit, ActualSize, Show, Hide, Lock, Unlock, Pin,
    Unpin,
    // Text och formatering
    Bold, Italic, Underline, AlignLeft, AlignCenter, AlignRight, AlignJustify,
    BulletList, NumberedList, Font, TextColor, FillColor,
    // Status och dialog
    Information, Warning, Error, Question, Done, Cancel, Help, Tip, Loading,
    // Fönster och meny
    Menu, MoreHorizontal, MoreVertical, Settings, CloseWindow, Minimize,
    Maximize, Restore, NewWindow, ExternalLink,
    // Verktyg och ritning
    Pointer, Pan, Measure, Pen, Eraser, Eyedropper, Shape, Line, Rectangle,
    Ellipse, Polygon, Text, Layers, Anchor, Magnet,
    // Media
    Play, Pause, MediaStop, Record, PreviousTrack, NextTrack, Repeat, Shuffle,
    SoundOn, SoundOff,
    // Kommunikation och person
    User, Users, Mail, Message, Notification, Share, Link, Attach,
    // Tid och data
    Calendar, Clock, History, Chart, Table, Database, Funnel, Tag, Bookmark,
    Star, Flag,
    // Säkerhet
    SecurityLock, Key, Shield, LogIn, LogOut,
    // CAD — rita (greppen är CAD-programmens fyllda rutor)
    Point, Polyline, Arc, Circle, Spline, Hatch, Dimension, AngleDimension,
    // CAD — ändra (spegla är `Flip`, vrid åt ett håll `RotateClockwise`)
    Move, Rotate, Scale, Offset, Trim, Extend, Fillet, Chamfer, Array,
    // CAD — fästpunkter och hjälp
    SnapEndpoint, SnapMidpoint, SnapCenter, SnapIntersection,
    SnapPerpendicular, SnapGrid, Ortho, Crosshair,
    // 3D — vyer och kamera
    ViewTop, ViewFront, ViewRight, ViewIsometric, Perspective, Orthographic,
    Orbit, Camera, Light, Axes, ZoomWindow,
    // 3D — former
    Box, Sphere, Cylinder, Cone, Torus, Plane, Mesh,
    // 3D — visning och modellering
    Wireframe, Solid, Rendered, SelectVertex, SelectEdge, SelectFace, Extrude,
    Union, Subtract, Intersect,
    // Bild
    Image, Crop, Resize, Brightness, Contrast,
    // Dockning och fönster
    PanelLeft, PanelRight, PanelBottom, Float, Tabs, SplitHorizontal,
    // Kod och system
    Terminal, Code, Bug, Package,
    // Karta och plats
    MapPin, MyLocation, Compass, Globe, Route, Terrain,
    // Ordna objekt — det fyllda är det markerade
    BringToFront, SendToBack, BringForward, SendBackward, Ungroup,
    AlignObjectsLeft, AlignObjectsCenter, AlignObjectsRight, AlignObjectsTop,
    AlignObjectsMiddle, AlignObjectsBottom, DistributeHorizontal,
    DistributeVertical,
    // 3D — fler vyer och verktyg (de dolda sidornas kanter streckade)
    ViewBottom, ViewLeft, ViewBack, Walk, Section,
    // CAD — fler
    Explode, Block, Note, RevisionCloud, Area,
    // Text — fler
    Strikethrough, Indent, Outdent, Heading, Quote, CodeBlock,
    // Diagram
    LineChart, PieChart, Sum,
    // Status — i ring
    AddCircle, RemoveCircle, CheckCircle, Blocked, Online, Offline,
    // Hårdvara
    Laptop, Phone, Keyboard, Mouse, Cpu, Battery, Power, Wifi, Usb,
    // Tid
    Timer, Stopwatch,
    // Diagonala pilar
    ArrowUpLeft, ArrowUpRight, ArrowDownLeft, ArrowDownRight,
    // Filtyper — kategorier (sidan med symbolen stor)
    FileImage, FileVideo, FileAudio, FileArchive, FileCode, FileSpreadsheet,
    FilePresentation, FileGeo, FileCad, FileModel, FilePointCloud,
    FileDatabase,
    // Filtyper — geodata (grön etikett)
    FileShp, FileGeoJson, FileKml, FileKmz, FileGpx, FileGpkg, FileGml,
    FileGdb, FileOsm, FileGeoTiff, FileMbTiles, FileTab, FileQgz,
    // Filtyper — CAD och BIM (röd etikett)
    FileDwg, FileDxf, FileDgn, FileIfc, FileStep, FileIges, FileRvt,
    // Filtyper — 3D-modeller (bärnsten)
    FileObj, FileFbx, FileGltf, FileGlb, FileStl, File3mf, FileDae, FileSkp,
    FileUsd,
    // Filtyper — punktmoln (bärnsten)
    FileLas, FileLaz, FileE57, FilePly, FileXyz,
    // Filtyper — bilder (gul)
    FilePng, FileJpg, FileGif, FileBmp, FileTiff, FileWebp, FileSvg, FileHeic,
    FileIco, FilePsd, FileRaw,
    // Filtyper — dokument (text blå, kalkyl grön, presentation bärnsten, PDF röd)
    FilePdf, FileDoc, FileDocx, FileOdt, FileRtf, FileTxt, FileMd, FileXls,
    FileXlsx, FileOds, FileCsv, FilePpt, FilePptx, FileOdp,
    // Filtyper — data och webb (neutral)
    FileJson, FileYaml, FileXml, FileHtml, FileSql, FileSqlite,
    // Filtyper — arkiv (neutral)
    FileZip, File7z, FileRar, FileTar, FileGz,
    // Filtyper — ljud och video (neutral)
    FileMp3, FileWav, FileMp4, FileMov,
    // Utseendets läge — System (skärmen, halvt ifylld), ljust (solen), mörkt
    // (månen). Glyferna för en lägesväljare, till exempel vid fönsterknapparna.
    ThemeSystem, ThemeLight, ThemeDark,
};

// STILEN — för standardikonerna och appens ikoner. `Color` är konturen i
// temats textfärg med YTOR och LINJER i en semantisk färgroll — mappen gul,
// spara och information blå, fel och ta bort röda, varningen bärnsten, klar
// och lägg till gröna, filens sida i papperets ton (`Theme::Colors::stockBlue`
// … `stockYellow` och `stockPaper`, härledda ur paletten för ljust och mörkt
// läge). `Mono` är SAMMA bild NEDTONAD: ytorna blekta mot papperet, linjerna
// och konturen i textfärgen. En ikon utan färg ritas likadant i båda.
// `Auto` är Color när kontrollen är aktiv och Mono när den är inaktiv
// (`ImageSource::forState`, som `Draw::stateGlyph` frågar); där inget
// tillstånd finns ritas den som Color.
enum class IconStyle : std::uint8_t { Mono, Color, Auto };

// Ikonen i storleksklassen `sizeClass` (logiska px — 16, 24, 32 eller 48; en
// annan positiv sida går också) och stilen `style`. `sizeClass` ≤ 0 loggas
// och blir 16.
ImageSource stockIcon(StockIcon icon, int sizeClass = 16,
                      IconStyle style = IconStyle::Mono);

// Antalet ikoner — uppräkningens värden är 0 … stockIconCount() − 1 i
// deklarationsordning (en palett, ett galleri).
int stockIconCount();

// Postens NAMN, som det står i uppräkningen ("Save", "ZoomIn") — det
// `.glazeui` skriver efter `glaze::StockIcon::`. Tom vy för ett värde utanför
// uppräkningen.
std::string_view stockIconName(StockIcon icon);

// Namnet tillbaka till posten; inget svar för ett okänt namn.
std::optional<StockIcon> stockIconByName(std::string_view name);

// ----------------------------------------------------------- appens ikoner
//
// En app RITAR sina ikoner med standardikonernas motor: ett namn, KONTUREN
// och FÄRGEN i samma språk och samma form som ramverkets egna — tabellen i
// src/stock_icon.cpp är 395 poster av just det slaget. Rutan är 24 × 24 med
// rad 0 överst:
//
//     GLAZE_REGISTER_ICON("Spara",
//         "@disk | M 9 16 L 15 10",          // konturen, ett lager per `|`
//         "#disk | all deep red");           // färgen, samma lager
//
//     sparaBtn.setImg(glaze::drawnIcon("Spara", 24, glaze::IconStyle::Auto));
//
//   former    M L H V Q Z (linjer och kurvor), F (nästa linje fylls), C A E U
//             (cirkel, båge, ellips, ellipsbåge), R B (rektangel, fylld), D
//             (skiva), K (utstansad text), @namn (en DEL)
//   färger    blue green red amber yellow paper — ett ord börjar ett lager i
//             den färgen; tonerna light dark deep ink, `strong`, `gradient`,
//             `over`/`under`, `all [ton] <färg>` för konturen, `contrast N`
//             (lagrets krav mot det under, förval 3:1; en linje 4,5:1) och
//             #namn (en FÄRGDEL)
//   lager     upp till FYRA, åtskilda av `|` i båda strängarna. Varje lager
//             läggs på det under med sitt kontrastkrav: ytterpixlarna flyttas
//             i ljushet, och räcker det inte växer en ring runt lagret.
//
// Delarna är ramverkets (`@fil`, `@mapp`, `@disk`, `#fil`, `#rund` …) och
// appens egna (`registerIconPart`). Ett fel i en beskrivning loggas EN gång
// per ikon, med ikonens namn och ordet där felet sitter, och resten av det
// lagret ritas inte. Ritningen cachas som standardikonernas, per (ikon,
// fysisk storlek, stil, färger).
//
// Registreringen är processens — innan första ritningen, gärna med makrot
// på namespace-nivå i appens cpp, eller i `main`. Ett namn som registreras
// igen ERSÄTTER det förra (och tömmer cachen för det). Tomt namn eller tom
// kontur loggas och registreras inte (svaret false).
bool registerIcon(std::string_view name, std::string_view shapes,
                  std::string_view colors = {});

// En DEL som appens ikoner (och bara de) når med `@namn` i konturen och
// `#namn` i färgen. Ramverkets delar går före en app-del med samma namn.
bool registerIconPart(std::string_view name, std::string_view shapes,
                      std::string_view colors = {});

// Appens ikon `name` som bild, i storleksklassen och stilen. Namnet slås upp
// när ikonen ritas: ett namn som inte är registrerat då loggas en gång och
// ritas som `StockIcon::Image`.
ImageSource drawnIcon(std::string_view name, int sizeClass = 16,
                      IconStyle style = IconStyle::Mono);

// De registrerade namnen, i registreringsordning (arket, en palett).
std::vector<std::string> drawnIconNames();

// ARKET: ikonerna `names` (appens namn, eller standardikonernas — "Save") i
// 16, 24, 32 och 48 med namnet under, i fyra rutor: mono till vänster och
// färg till höger, ljust överst och mörkt under — ritade i ett dolt fönster
// i skärmens skala, utan förstoring, och skrivna som PNG. Tom lista = alla
// appens ikoner. Svarar false (och loggar) om ett namn är okänt eller om
// fönstret eller filen inte gick att skapa. UI-tråden — appen anropar den
// till exempel bakom en egen flagga i `main`.
bool writeIconSheet(const std::string& pngPath,
                    const std::vector<std::string>& names = {});

} // namespace glaze

// Läggs på namespace-nivå i appens cpp:
//     GLAZE_REGISTER_ICON("Spara", "@disk", "#disk")
//     GLAZE_REGISTER_ICON_PART("ram", "R 3 3 18 18 2", "all deep blue")
#define GLAZE_ICON_CAT2(a, b) a##b
#define GLAZE_ICON_CAT(a, b)  GLAZE_ICON_CAT2(a, b)
#define GLAZE_REGISTER_ICON(name, shapes, colors)                              \
    namespace {                                                                \
    const bool GLAZE_ICON_CAT(glazeIconReg_, __LINE__) =                       \
        ::glaze::registerIcon(name, shapes, colors);                           \
    }
#define GLAZE_REGISTER_ICON_PART(name, shapes, colors)                         \
    namespace {                                                                \
    const bool GLAZE_ICON_CAT(glazeIconPartReg_, __LINE__) =                   \
        ::glaze::registerIconPart(name, shapes, colors);                       \
    }
