#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

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
//     rastrerad i exakt den storlek den ritas (aldrig en skalad bitmapp);
//   * TEMAT — ikonen färgas med temats textfärg (`Theme::Colors::text`) i det
//     läge fönstret ritar i, och färgvariantens accenter med temats
//     `stock*`-roller, så ljust och mörkt följer utan att appen gör något;
//   * STORLEKSKLASSEN — 16, 24, 32 och 48 logiska px (`sizeClass()`, som en
//     assets storleksklass: widgetarna ger ikonen den sidan).
// Stilen är ramverkets egna ikoners (`Draw::icon`): en enfärgad kontur, ett
// streck på en logisk px i 16. En INAKTIV kontroll tonar den som varje annan
// bild (`Content::imgOpacity` = 1 − `Style::iconFade`).
//
// Rastreringen cachas per (ikon, fysisk storlek, färg) i processen — UI-
// tråden, som all ritning. En OS-ikon (`AppWindow::appIcon`, `TrayIcon`) av en
// standardikon rastreras i appens aktuella textfärg när den sätts.
//
// Regel 8: `glaze::stockIcon(glaze::StockIcon::Save)`,
// `glaze::stockIcon(glaze::StockIcon::Save, 24)` och
// `glaze::stockIcon(glaze::StockIcon::Save, 24, glaze::StockIconStyle::Color)`
// är bildargument i `.glazeui` precis som `glaze::img(...)`, och previewn
// ritar dem.

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
};

// STILEN. `Mono` är den enfärgade konturen i temats textfärg. `Color` är
// SAMMA kontur med ACCENTDELAR i en semantisk färgroll — mappen gul, spara
// och information blå, fel och ta bort röda, varningen bärnsten, klar och
// lägg till gröna (`Theme::Colors::stockBlue` … `stockYellow`, härledda ur
// paletten för ljust och mörkt läge). En ikon utan accent ritas likadant i
// båda.
enum class StockIconStyle : std::uint8_t { Mono, Color };

// Ikonen i storleksklassen `sizeClass` (logiska px — 16, 24, 32 eller 48; en
// annan positiv sida går också) och stilen `style`. `sizeClass` ≤ 0 loggas
// och blir 16.
ImageSource stockIcon(StockIcon icon, int sizeClass = 16,
                      StockIconStyle style = StockIconStyle::Mono);

// Antalet ikoner — uppräkningens värden är 0 … stockIconCount() − 1 i
// deklarationsordning (en palett, ett galleri).
int stockIconCount();

// Postens NAMN, som det står i uppräkningen ("Save", "ZoomIn") — det
// `.glazeui` skriver efter `glaze::StockIcon::`. Tom vy för ett värde utanför
// uppräkningen.
std::string_view stockIconName(StockIcon icon);

// Namnet tillbaka till posten; inget svar för ett okänt namn.
std::optional<StockIcon> stockIconByName(std::string_view name);

} // namespace glaze
