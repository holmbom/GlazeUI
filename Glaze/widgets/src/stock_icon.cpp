// STANDARDIKONERNA — ritbeskrivningarna och
// rastreringen bakom `glaze::stockIcon`. Kontraktet utåt står i
// glaze/stock_icon.h; ritningen går genom `detail::stockBitmap`
// (image_cache.h), som `Draw::glyph`, fönstrets bildcache och OS-ikonen frågar.
//
// RITBESKRIVNINGEN är en sträng per ikon i en ruta på 24 × 24 (rad 0 överst,
// y nedåt), skriven för att vara lätt att ändra — talen är ikonernas form,
// precis som `Draw::icon`s enhetskvadrat:
//
//   M x y            ny linje från (x, y)        L x y    linje till
//   H x · V y        vågrätt / lodrätt till      Q kx ky x y   kurva till (x, y)
//   Z                sluter linjen               F        nästa M-linje FYLLS
//   C cx cy r        cirkel                      A cx cy r a0 a1   båge
//   E cx cy rx ry    ellips                      U cx cy rx ry a0 a1   ellipsbåge
//   R x y b h r      rektangel med hörnradie     B x y b h r   fylld rektangel
//   D cx cy r        fylld skiva                 @namn    en gemensam DEL (kDelar)
//   K x y b h TEXT   TEXTEN UTSTANSAD ur hela ikonen, centrerad i rutan
//                    (filtypernas etikett: A–Z och 0–9 i `kGlyfer`)
//
// Vinklarna är grader medurs från höger (0 höger, 90 nedåt) och en båge går
// medurs från a0 till a1 (a1 > a0, gärna över 360). Allt utom F/B/D är
// STRECK med EN bredd för alla ikoner (`kStreck`) och runda ändar och skarvar
// — samma kontur som ramverkets egna ikoner, en logisk px i 16.
//
// RASTRERINGEN provar varje ritad form i `kProv` × `kProv` punkter per
// fysisk px och tar ned bilden med `glzassets::resizeRgba` (ytans medelvärde i
// förmultiplicerad rymd) — kantutjämningen är programikonens skalare, inte en
// egen. I mono är färgen EN: ikonen är en mask i temats textfärg, med rak
// alfa som `glaze::Bitmap` vill ha. FÄRGVARIANTEN (`StockIconStyle::Color`) lägger
// accentdelar i temats `stock*`-roller bakom eller ovanpå konturen
// (`kFärger`); varje prov får färgen från det översta lager det ligger i, och
// samma nedskalning blandar dem. En UTSTANSAD text (`K`) är genomskinlig i
// alla lager, så bakgrunden syns genom den — filtypernas etikett läses i
// varje tema och läge utan en egen textfärg.

#include "glaze/stock_icon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gfx/geometry.h>
#include <gfx/platform.h>

#include "glaze/color.h"          // mix — färgvariantens starka toner
#include "glaze_assets/image.h"   // resizeRgba — kantutjämningen
#include "image_cache.h"          // detail::stockBitmap
#include "prop_parse.h"           // parseDouble — talen, oberoende av lokalen

namespace glaze {
namespace {

constexpr float kRuta   = 24.0f;   // ritrutans sida
constexpr float kStreck = 1.5f;    // strecket i rutans enheter: 1 px i 16
constexpr int   kProv   = 4;       // prov per fysisk px och axel
constexpr int   kMaxPx  = 512;     // största rastrering (sida, fysiska px)

// Den utstansade TEXTEN (`K`): glyferna står i en ruta på 3 × 5 med ett
// tunnare streck än ikonerna och 1.5 mellan glyfernas rutor; texten skalas
// ned i sin helhet (streck och mellanrum med) när den inte ryms.
constexpr float kGlyfB      = 3.0f;
constexpr float kGlyfH      = 5.0f;
constexpr float kGlyfSteg   = 1.5f;
constexpr float kTextStreck = 0.9f;

// Färgvariantens STARKA ton (`strong`): rollen blandad så här långt mot
// textfärgen — mörkare i ljust läge och ljusare i mörkt, så en utstansad
// etikett på grönt eller bärnsten håller kontrasten mot bakgrunden.
constexpr float kStark = 0.35f;

struct Def {
    StockIcon   icon;
    const char* name;
    const char* path;
};

// lang_gate:av ritbeskrivningar och ikonernas API-namn — ingen användartext
// DELAR som flera ikoner ritar — `@namn` i en beskrivning.
struct Del {
    const char* name;
    const char* path;
};

constexpr Del kDelar[] = {
    {"fil", "M 14 2 H 6 Q 4 2 4 4 V 20 Q 4 22 6 22 H 18 Q 20 22 20 20 V 8 Z "
            "M 14 2 V 8 H 20"},
    {"mapp", "M 4 20 H 20 Q 22 20 22 18 V 8 Q 22 6 20 6 H 12 L 10 3 H 4 "
             "Q 2 3 2 5 V 18 Q 2 20 4 20 Z"},
    {"oppen", "M 6 14 L 7.5 11.1 Q 8 10 9.2 10 H 20 Q 22.4 10 21.9 12.5 "
              "L 20.4 18.5 Q 20 20 18.5 20 H 4 Q 2 20 2 18 V 5 Q 2 3 4 3 H 8 "
              "L 10 6 H 18 Q 20 6 20 8 V 10"},
    {"disk", "M 5 3 H 16 L 21 8 V 19 Q 21 21 19 21 H 5 Q 3 21 3 19 V 5 "
             "Q 3 3 5 3 Z M 17 21 V 13 H 7 V 21 M 7 3 V 8 H 15"},
    {"kopia", "R 8 8 14 14 2 M 4 16 Q 2 16 2 14 V 4 Q 2 2 4 2 H 14 Q 16 2 16 4"},
    {"lupp", "C 11 11 8 M 21 21 L 16.7 16.7"},
    {"oga", "A 12 15.6 10.6 200 340 A 12 8.4 10.6 20 160"},
    {"nal", "M 12 17 V 22 M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 "
            "M 8 2 H 16 Q 18 2 18 4 Q 18 6 16 6 H 8 Q 6 6 6 4 Q 6 2 8 2"},
    {"las", "R 3 11 18 11 2 M 7 11 V 7 A 12 7 5 180 360 M 17 7 V 11"},
    {"hogtalare", "M 11 4.7 L 6.4 8 H 3 Q 2 8 2 9 V 15 Q 2 16 3 16 H 6.4 "
                  "L 11 19.3 Z"},
    {"kryss", "M 18 6 L 6 18 M 6 6 L 18 18"},
    {"rader", "M 3 6 H 13 M 3 12 H 13 M 3 18 H 13"},
    {"veck", "M 12 22 V 16 M 12 8 V 2 M 2 12 H 4 M 8 12 H 10 M 14 12 H 16 "
             "M 20 12 H 22"},
    // Den isometriska kuben: sexhörningen och de tre synliga kanterna inåt.
    {"kub", "M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z "
            "M 4.2 7.5 L 12 12 L 19.8 7.5 M 12 12 V 21"},
    // En yta i perspektiv — markeringslägenas punkt, kant och yta.
    {"yta", "M 3 18 L 7 6 H 21 L 17 18 Z"},
    // Kuben sedd UNDIFRÅN: botten och de två främre sidorna synliga.
    {"kubunder", "M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z "
                 "M 4.2 16.5 L 12 12 L 19.8 16.5 M 12 12 V 3"},
    // FILTYPERNAS små symboler i sidans övre vänstra hörn (ovanför etiketten,
    // till vänster om vecket).
    {"fpin", "C 9.15 6 2 M 7.5 7.2 L 9.15 10.2 L 10.8 7.2"},
    {"fglob", "C 9.15 7.15 2.9 E 9.15 7.15 1.2 2.9 M 6.25 7.15 H 12.05"},
    {"fdb", "E 9.15 5 2.6 1 M 6.55 5 V 9.3 M 11.75 5 V 9.3 U 9.15 9.3 2.6 1 0 180"},
    {"fvinkel", "M 6.5 10.3 V 4.2 L 11.8 10.3 Z"},
    {"fkub", "M 9.15 4.1 L 11.83 5.65 V 8.75 L 9.15 10.3 L 6.47 8.75 V 5.65 Z "
             "M 6.47 5.65 L 9.15 7.2 L 11.83 5.65 M 9.15 7.2 V 10.3"},
    {"fmoln", "D 7 5 0.7 D 9.6 4.3 0.7 D 11.4 5.8 0.7 D 8.2 7.3 0.7 D 10.6 7.9 0.7 "
              "D 6.9 9.6 0.7 D 9.3 10 0.7 D 11.5 9.8 0.7"},
    {"fbild", "M 6.5 10.3 L 8.6 7.2 L 10 9 L 10.9 8 L 11.9 10.3 Z D 10.6 4.9 1"},
    {"ftext", "M 6.5 4.6 H 11.8 M 6.5 7.3 H 11.8 M 6.5 10 H 10"},
    {"ftabell", "R 6.5 4.2 5.3 6.1 0.6 M 6.5 7.25 H 11.8 M 9.15 4.2 V 10.3"},
    {"fslide", "R 6.5 4.2 5.3 4.2 0.5 M 9.15 8.4 V 10.4"},
    {"fklammer", "M 7.9 4.2 Q 7.1 4.2 7.1 5 V 6.4 Q 7.1 7.2 6.4 7.2 Q 7.1 7.2 7.1 8 "
                 "V 9.5 Q 7.1 10.3 7.9 10.3 M 10.4 4.2 Q 11.2 4.2 11.2 5 V 6.4 "
                 "Q 11.2 7.2 11.9 7.2 Q 11.2 7.2 11.2 8 V 9.5 Q 11.2 10.3 10.4 10.3"},
    {"fkod", "M 8 4.6 L 6.5 7.25 L 8 9.9 M 10.3 4.6 L 11.8 7.25 L 10.3 9.9"},
    {"fdragkedja", "M 8.5 3.8 V 4.3 M 9.8 5.3 V 5.8 M 8.5 6.8 V 7.3 B 7.9 8.4 2.5 2 0.5"},
    {"fnot", "M 8.2 9.6 V 5 L 11.6 4.2 V 8.8 D 7.3 9.6 1.1 D 10.7 8.8 1.1"},
    {"fspela", "F M 7.3 4.5 L 11.3 7.25 L 7.3 10 Z"},
};

// En post per ikon, i UPPRÄKNINGENS ordning (static_assert nedan).
constexpr Def kDefs[] = {
    // ----------------------------------------------------------- Navigering
    {StockIcon::Back, "Back", "C 12 12 10 M 16 12 H 8 M 12 8 L 8 12 L 12 16"},
    {StockIcon::Forward, "Forward", "C 12 12 10 M 8 12 H 16 M 12 8 L 16 12 L 12 16"},
    {StockIcon::Up, "Up", "C 12 12 10 M 12 16 V 8 M 8 12 L 12 8 L 16 12"},
    {StockIcon::Down, "Down", "C 12 12 10 M 12 8 V 16 M 8 12 L 12 16 L 16 12"},
    {StockIcon::Home, "Home", "M 3 11 L 12 3 L 21 11 M 5 9.5 V 21 H 19 V 9.5 "
                              "M 10 21 V 15 H 14 V 21"},
    {StockIcon::First, "First", "M 17 18 L 11 12 L 17 6 M 7 6 V 18"},
    {StockIcon::Previous, "Previous", "C 12 12 10 M 14 16 L 10 12 L 14 8"},
    {StockIcon::Next, "Next", "C 12 12 10 M 10 8 L 14 12 L 10 16"},
    {StockIcon::Last, "Last", "M 7 18 L 13 12 L 7 6 M 17 6 V 18"},
    {StockIcon::Refresh, "Refresh", "A 12 12 9 200 333 M 20 3 V 8 H 15 "
                                    "A 12 12 9 20 153 M 4 21 V 16 H 9"},
    {StockIcon::Stop, "Stop", "M 8.3 3 H 15.7 L 21 8.3 V 15.7 L 15.7 21 H 8.3 "
                              "L 3 15.7 V 8.3 Z M 9 9 L 15 15 M 15 9 L 9 15"},
    {StockIcon::GoTo, "GoTo", "M 3 11 L 21 3 L 13 21 L 11 13 Z"},
    // ---------------------------------------------------------------- Pilar
    {StockIcon::ArrowLeft, "ArrowLeft", "M 19 12 H 5 M 12 5 L 5 12 L 12 19"},
    {StockIcon::ArrowRight, "ArrowRight", "M 5 12 H 19 M 12 5 L 19 12 L 12 19"},
    {StockIcon::ArrowUp, "ArrowUp", "M 12 19 V 5 M 5 12 L 12 5 L 19 12"},
    {StockIcon::ArrowDown, "ArrowDown", "M 12 5 V 19 M 5 12 L 12 19 L 19 12"},
    {StockIcon::ChevronLeft, "ChevronLeft", "M 15 6 L 9 12 L 15 18"},
    {StockIcon::ChevronRight, "ChevronRight", "M 9 6 L 15 12 L 9 18"},
    {StockIcon::ChevronUp, "ChevronUp", "M 6 15 L 12 9 L 18 15"},
    {StockIcon::ChevronDown, "ChevronDown", "M 6 9 L 12 15 L 18 9"},
    {StockIcon::ChevronsLeft, "ChevronsLeft",
     "M 11 17 L 6 12 L 11 7 M 18 17 L 13 12 L 18 7"},
    {StockIcon::ChevronsRight, "ChevronsRight",
     "M 6 17 L 11 12 L 6 7 M 13 17 L 18 12 L 13 7"},
    {StockIcon::ChevronsUp, "ChevronsUp",
     "M 17 11 L 12 6 L 7 11 M 17 18 L 12 13 L 7 18"},
    {StockIcon::ChevronsDown, "ChevronsDown",
     "M 7 6 L 12 11 L 17 6 M 7 13 L 12 18 L 17 13"},
    {StockIcon::Flip, "Flip", "M 3 7 L 8 12 L 3 17 Z M 21 7 L 16 12 L 21 17 Z "
                              "M 12 2 V 4 M 12 8 V 10 M 12 14 V 16 M 12 20 V 22"},
    {StockIcon::RotateClockwise, "RotateClockwise",
     "A 12 12 9 0 315 M 18.4 5.6 L 21 8 M 21 3 V 8 H 16"},
    {StockIcon::RotateCounterClockwise, "RotateCounterClockwise",
     "A 12 12 9 225 540 M 5.6 5.6 L 3 8 M 3 3 V 8 H 8"},
    {StockIcon::Swap, "Swap", "M 8 3 L 4 7 L 8 11 M 4 7 H 20 "
                              "M 16 13 L 20 17 L 16 21 M 20 17 H 4"},
    // ----------------------------------------------------- Fil och dokument
    {StockIcon::New, "New", "@fil M 9 15 H 15 M 12 12 V 18"},
    {StockIcon::Open, "Open", "@oppen"},
    {StockIcon::Save, "Save", "@disk"},
    {StockIcon::SaveAs, "SaveAs",
     "M 5 3 H 16 L 21 8 V 10 M 10 21 H 5 Q 3 21 3 19 V 5 Q 3 3 5 3 "
     "M 7 3 V 8 H 15 M 7 21 V 13 H 12 "
     "M 13 21 L 13.5 18.5 L 19.5 12.5 L 21.5 14.5 L 15.5 20.5 Z"},
    {StockIcon::SaveAll, "SaveAll",
     "M 8 2 H 17 L 22 7 V 16 Q 22 18 20 18 H 8 Q 6 18 6 16 V 4 Q 6 2 8 2 Z "
     "M 10 2 V 6 H 16 M 10 18 V 12 H 18 V 18 M 2 6 V 20 Q 2 22 4 22 H 18"},
    {StockIcon::Close, "Close", "@fil M 9.5 12.5 L 14.5 17.5 M 14.5 12.5 L 9.5 17.5"},
    {StockIcon::Print, "Print",
     "M 6 18 H 4 Q 2 18 2 16 V 11 Q 2 9 4 9 H 20 Q 22 9 22 11 V 16 "
     "Q 22 18 20 18 H 18 M 6 9 V 2 H 18 V 9 R 6 14 12 8 1"},
    {StockIcon::PrintPreview, "PrintPreview",
     "M 14 2 H 6 Q 4 2 4 4 V 20 Q 4 22 6 22 H 10 M 14 2 L 20 8 V 11 "
     "M 14 2 V 8 H 20 C 16 16 3 M 18.2 18.2 L 21.5 21.5"},
    {StockIcon::Import, "Import",
     "M 12 3 V 15 M 8 11 L 12 15 L 16 11 M 8 5 H 4 Q 2 5 2 7 V 17 "
     "Q 2 19 4 19 H 20 Q 22 19 22 17 V 7 Q 22 5 20 5 H 16"},
    {StockIcon::Export, "Export",
     "M 12 15 V 3 M 8 7 L 12 3 L 16 7 M 8 9 H 6 Q 4 9 4 11 V 20 "
     "Q 4 22 6 22 H 18 Q 20 22 20 20 V 11 Q 20 9 18 9 H 16"},
    {StockIcon::Properties, "Properties",
     "M 21 4 H 14 M 10 4 H 3 M 21 12 H 12 M 8 12 H 3 M 21 20 H 16 M 12 20 H 3 "
     "M 14 2 V 6 M 8 10 V 14 M 16 18 V 22"},
    {StockIcon::File, "File", "@fil"},
    {StockIcon::Document, "Document", "@fil M 8 13 H 16 M 8 17 H 16 M 8 9 H 10"},
    {StockIcon::Folder, "Folder", "@mapp"},
    {StockIcon::FolderOpen, "FolderOpen", "@oppen"},
    {StockIcon::NewFolder, "NewFolder", "@mapp M 12 10 V 16 M 9 13 H 15"},
    {StockIcon::Recent, "Recent",
     "M 14 2 H 6 Q 4 2 4 4 V 20 Q 4 22 6 22 H 9 M 14 2 L 20 8 V 10 "
     "M 14 2 V 8 H 20 C 15.5 16.5 5 M 15.5 14 V 16.5 L 17 18"},
    // ---------------------------------------------------- Lagring och plats
    {StockIcon::Drive, "Drive",
     "M 22 12 H 2 M 5.5 5 L 2 12 V 18 Q 2 20 4 20 H 20 Q 22 20 22 18 V 12 "
     "L 18.5 5 Q 18 4 17 4 H 7 Q 6 4 5.5 5 D 6 16 1 D 10 16 1"},
    {StockIcon::Network, "Network",
     "R 9 2 6 6 1 R 2 16 6 6 1 R 16 16 6 6 1 M 5 16 V 12 H 19 V 16 M 12 12 V 8"},
    {StockIcon::Cloud, "Cloud",
     "A 9 12 7 90 343.4 M 15.7 10 H 17.5 A 17.5 14.5 4.5 270 450 M 17.5 19 H 9"},
    {StockIcon::HomeFolder, "HomeFolder",
     "@mapp M 8.5 14.5 L 12 11.5 L 15.5 14.5 M 10 13.5 V 17 H 14 V 13.5"},
    {StockIcon::Desktop, "Desktop", "R 2 3 20 14 2 M 8 21 H 16 M 12 17 V 21"},
    {StockIcon::Trash, "Trash",
     "M 3 6 H 21 M 19 6 V 20 Q 19 22 17 22 H 7 Q 5 22 5 20 V 6 "
     "M 8 6 V 4 Q 8 2 10 2 H 14 Q 16 2 16 4 V 6 M 10 11 V 17 M 14 11 V 17"},
    {StockIcon::Download, "Download",
     "M 21 15 V 19 Q 21 21 19 21 H 5 Q 3 21 3 19 V 15 M 7 10 L 12 15 L 17 10 "
     "M 12 15 V 3"},
    {StockIcon::Upload, "Upload",
     "M 21 15 V 19 Q 21 21 19 21 H 5 Q 3 21 3 19 V 15 M 17 8 L 12 3 L 7 8 "
     "M 12 3 V 15"},
    {StockIcon::Sync, "Sync",
     "A 12 12 7 180 360 M 16.5 9.5 L 19 12 L 21.5 9.5 "
     "A 12 12 7 0 180 M 2.5 14.5 L 5 12 L 7.5 14.5"},
    // ------------------------------------------------------------ Redigering
    {StockIcon::Undo, "Undo", "M 9 14 L 4 9 L 9 4 M 4 9 H 14.5 "
                              "A 14.5 14.5 5.5 270 450 M 14.5 20 H 11"},
    {StockIcon::Redo, "Redo", "M 15 14 L 20 9 L 15 4 M 20 9 H 9.5 "
                              "A 9.5 14.5 5.5 90 270 M 9.5 20 H 13"},
    {StockIcon::Cut, "Cut", "C 6 6 3 C 6 18 3 M 20 4 L 8.1 15.9 M 14.5 14.5 L 20 20 "
                            "M 8.1 8.1 L 12 12"},
    {StockIcon::Copy, "Copy", "@kopia"},
    {StockIcon::Paste, "Paste",
     "R 8 2 8 4 1 M 16 4 H 18 Q 20 4 20 6 V 20 Q 20 22 18 22 H 6 Q 4 22 4 20 "
     "V 6 Q 4 4 6 4 H 8"},
    {StockIcon::Delete, "Delete", "M 10 5 L 2.5 12 L 10 19 H 20 Q 22 19 22 17 "
                                  "V 7 Q 22 5 20 5 Z M 12 9 L 18 15 M 18 9 L 12 15"},
    {StockIcon::SelectAll, "SelectAll",
     "M 5 3 Q 3 3 3 5 M 19 3 Q 21 3 21 5 M 21 19 Q 21 21 19 21 M 5 21 Q 3 21 3 19 "
     "M 9 3 H 10 M 14 3 H 15 M 9 21 H 10 M 14 21 H 15 M 3 9 V 10 M 3 14 V 15 "
     "M 21 9 V 10 M 21 14 V 15 R 8 8 8 8 1"},
    {StockIcon::Search, "Search", "@lupp"},
    {StockIcon::Replace, "Replace",
     "R 2 2 8 8 1 R 14 14 8 8 1 M 14 4 H 18 Q 20 4 20 6 V 10 "
     "M 17.5 7.5 L 20 10 L 22.5 7.5 M 10 20 H 6 Q 4 20 4 18 V 14 "
     "M 1.5 16.5 L 4 14 L 6.5 16.5"},
    {StockIcon::Clear, "Clear",
     "M 12 2 Q 10 2 10 4 V 9 Q 10 10 9 10 H 6 Q 4 10 4 12 V 14 H 20 V 12 "
     "Q 20 10 18 10 H 15 Q 14 10 14 9 V 4 Q 14 2 12 2 Z M 5 14 L 3 22 H 21 "
     "L 19 14 M 9 22 L 10 18 M 15 22 L 14 18"},
    {StockIcon::Edit, "Edit", "M 3 21 L 4 16.5 L 16.5 4 Q 18 2.5 19.5 4 L 20 4.5 "
                              "Q 21.5 6 20 7.5 L 7.5 20 Z M 14.5 6 L 18 9.5"},
    {StockIcon::Rename, "Rename",
     "M 5 8 H 4 Q 2 8 2 10 V 14 Q 2 16 4 16 H 5 M 13 8 H 20 Q 22 8 22 10 V 14 "
     "Q 22 16 20 16 H 13 M 9 5 V 19 M 7 4 Q 9 4 9 6 Q 9 4 11 4 "
     "M 7 20 Q 9 20 9 18 Q 9 20 11 20"},
    {StockIcon::Duplicate, "Duplicate", "@kopia M 15 12 V 18 M 12 15 H 18"},
    // ------------------------------------------------------ Listor och objekt
    {StockIcon::Add, "Add", "M 5 12 H 19 M 12 5 V 19"},
    {StockIcon::Remove, "Remove", "M 5 12 H 19"},
    {StockIcon::MoveUp, "MoveUp", "@rader M 18 20 V 4 M 15 7 L 18 4 L 21 7"},
    {StockIcon::MoveDown, "MoveDown", "@rader M 18 4 V 20 M 15 17 L 18 20 L 21 17"},
    {StockIcon::MoveFirst, "MoveFirst", "M 12 21 V 8 M 7 13 L 12 8 L 17 13 M 5 3 H 19"},
    {StockIcon::MoveLast, "MoveLast", "M 12 3 V 16 M 7 11 L 12 16 L 17 11 M 5 21 H 19"},
    {StockIcon::SortAscending, "SortAscending",
     "M 3 8 L 7 4 L 11 8 M 7 4 V 20 M 11 12 H 15 M 11 16 H 18 M 11 20 H 21"},
    {StockIcon::SortDescending, "SortDescending",
     "M 3 16 L 7 20 L 11 16 M 7 20 V 4 M 11 4 H 21 M 11 8 H 18 M 11 12 H 15"},
    {StockIcon::Filter, "Filter", "M 3 6 H 21 M 7 12 H 17 M 10 18 H 14"},
    {StockIcon::Group, "Group",
     "M 3 7 V 5 Q 3 3 5 3 H 7 M 17 3 H 19 Q 21 3 21 5 V 7 M 21 17 V 19 "
     "Q 21 21 19 21 H 17 M 7 21 H 5 Q 3 21 3 19 V 17 R 7 7 7 5 1 R 10 12 7 5 1"},
    {StockIcon::Expand, "Expand", "R 3 3 18 18 2 M 8 12 H 16 M 12 8 V 16"},
    {StockIcon::Collapse, "Collapse", "R 3 3 18 18 2 M 8 12 H 16"},
    {StockIcon::ExpandAll, "ExpandAll",
     "@veck M 15 19 L 12 22 L 9 19 M 15 5 L 12 2 L 9 5"},
    {StockIcon::CollapseAll, "CollapseAll",
     "@veck M 15 19 L 12 16 L 9 19 M 15 5 L 12 8 L 9 5"},
    // ----------------------------------------------------------------- Vyer
    {StockIcon::ListView, "ListView",
     "R 3 3 7 7 1 R 3 14 7 7 1 M 14 4 H 21 M 14 9 H 21 M 14 15 H 21 M 14 20 H 21"},
    {StockIcon::DetailsView, "DetailsView",
     "B 3 4 3 3 0.5 M 9 5.5 H 14 M 17 5.5 H 21 B 3 10.5 3 3 0.5 M 9 12 H 14 "
     "M 17 12 H 21 B 3 17 3 3 0.5 M 9 18.5 H 14 M 17 18.5 H 21"},
    {StockIcon::IconView, "IconView",
     "R 3 3 7 7 1 R 14 3 7 7 1 R 3 14 7 7 1 R 14 14 7 7 1"},
    {StockIcon::Grid, "Grid", "R 3 3 18 18 2 M 3 9 H 21 M 3 15 H 21 M 9 3 V 21 "
                              "M 15 3 V 21"},
    {StockIcon::SplitView, "SplitView", "R 3 3 18 18 2 M 12 3 V 21"},
    {StockIcon::Sidebar, "Sidebar", "R 3 3 18 18 2 M 9 3 V 21"},
    {StockIcon::FullScreen, "FullScreen",
     "M 8 3 H 5 Q 3 3 3 5 V 8 M 21 8 V 5 Q 21 3 19 3 H 16 M 3 16 V 19 "
     "Q 3 21 5 21 H 8 M 16 21 H 19 Q 21 21 21 19 V 16"},
    {StockIcon::ExitFullScreen, "ExitFullScreen",
     "M 8 3 V 6 Q 8 8 6 8 H 3 M 21 8 H 18 Q 16 8 16 6 V 3 M 3 16 H 6 "
     "Q 8 16 8 18 V 21 M 16 21 V 18 Q 16 16 18 16 H 21"},
    // ------------------------------------------------------ Zoom och visning
    {StockIcon::ZoomIn, "ZoomIn", "@lupp M 11 8 V 14 M 8 11 H 14"},
    {StockIcon::ZoomOut, "ZoomOut", "@lupp M 8 11 H 14"},
    {StockIcon::ZoomToFit, "ZoomToFit",
     "M 21 21 L 15 15 M 21 16.2 V 21 H 16.2 M 3 16.2 V 21 H 7.8 M 3 21 L 9 15 "
     "M 21 7.8 V 3 H 16.2 M 21 3 L 15 9 M 3 7.8 V 3 H 7.8 M 3 3 L 9 9"},
    {StockIcon::ActualSize, "ActualSize",
     "R 2 3 20 18 2 M 6 9 L 8 7.5 V 16.5 M 16 9 L 18 7.5 V 16.5 "
     "D 12 10 0.9 D 12 14 0.9"},
    {StockIcon::Show, "Show", "@oga C 12 12 3"},
    {StockIcon::Hide, "Hide", "@oga C 12 12 3 M 3 3 L 21 21"},
    {StockIcon::Lock, "Lock", "@las"},
    {StockIcon::Unlock, "Unlock", "R 3 11 18 11 2 M 7 11 V 7 A 12 7 5 180 348"},
    {StockIcon::Pin, "Pin", "@nal"},
    {StockIcon::Unpin, "Unpin", "@nal M 2 2 L 22 22"},
    // ------------------------------------------------- Text och formatering
    {StockIcon::Bold, "Bold", "M 6 4 V 20 H 15 M 6 12 H 15 M 6 4 H 14 "
                              "A 15 16 4 270 450 A 14 8 4 270 450"},
    {StockIcon::Italic, "Italic", "M 19 4 H 10 M 14 20 H 5 M 15 4 L 9 20"},
    {StockIcon::Underline, "Underline",
     "M 6 4 V 10 M 18 4 V 10 A 12 10 6 0 180 M 4 20 H 20"},
    {StockIcon::AlignLeft, "AlignLeft", "M 3 5 H 21 M 3 10 H 15 M 3 15 H 21 M 3 20 H 13"},
    {StockIcon::AlignCenter, "AlignCenter",
     "M 3 5 H 21 M 7 10 H 17 M 3 15 H 21 M 8 20 H 16"},
    {StockIcon::AlignRight, "AlignRight",
     "M 3 5 H 21 M 9 10 H 21 M 3 15 H 21 M 11 20 H 21"},
    {StockIcon::AlignJustify, "AlignJustify",
     "M 3 5 H 21 M 3 10 H 21 M 3 15 H 21 M 3 20 H 21"},
    {StockIcon::BulletList, "BulletList",
     "D 4 6 1 D 4 12 1 D 4 18 1 M 8 6 H 21 M 8 12 H 21 M 8 18 H 21"},
    {StockIcon::NumberedList, "NumberedList",
     "M 10 6 H 21 M 10 12 H 21 M 10 18 H 21 M 4 6 H 5 V 10 M 4 10 H 6 "
     "M 4 14 Q 6 13.5 6 15 Q 6 16 4 18 H 6"},
    {StockIcon::Font, "Font", "M 3 16 L 7.5 7 L 12 16 M 4.5 13 H 10.5 M 21 14 H 16 "
                              "M 16 16 V 12.5 A 18.5 12.5 2.5 180 360 M 21 12.5 V 16"},
    {StockIcon::TextColor, "TextColor", "M 4 20 H 20 M 6 16 L 12 4 L 18 16 M 8 12 H 16"},
    {StockIcon::FillColor, "FillColor",
     "M 19 11 L 11 3 L 2.4 11.6 Q 1.6 12.4 2.4 13.2 L 7.6 18.4 Q 8.4 19.2 9.2 18.4 "
     "Z M 5 2 L 10 7 M 2 13 H 17 M 20 16 Q 18 18.5 18 20 M 22 20 Q 22 18.5 20 16 "
     "A 20 20 2 0 180"},
    // ---------------------------------------------------- Status och dialog
    {StockIcon::Information, "Information", "C 12 12 10 M 12 16 V 11 D 12 7.8 1"},
    {StockIcon::Warning, "Warning",
     "M 10.3 4 Q 12 1.2 13.7 4 L 21.7 18 Q 23.2 21 20 21 H 4 Q 0.8 21 2.3 18 Z "
     "M 12 9 V 13 D 12 17 1"},
    {StockIcon::Error, "Error", "C 12 12 10 M 15 9 L 9 15 M 9 9 L 15 15"},
    {StockIcon::Question, "Question",
     "C 12 12 10 A 12 9.5 3 190 370 M 15 10 Q 15 12 12 13 D 12 17 1"},
    {StockIcon::Done, "Done", "M 20 6 L 9 17 L 4 12"},
    {StockIcon::Cancel, "Cancel", "@kryss"},
    {StockIcon::Help, "Help",
     "C 12 12 10 C 12 12 4 M 4.9 4.9 L 9.2 9.2 M 14.8 14.8 L 19.1 19.1 "
     "M 14.8 9.2 L 19.1 4.9 M 9.2 14.8 L 4.9 19.1"},
    {StockIcon::Tip, "Tip",
     "M 15 14 Q 15.3 12.8 16.5 11.5 Q 18 10 18 8 A 12 8 6 180 360 "
     "M 6 8 Q 6 10 7.5 11.5 Q 8.7 12.8 9 14 M 9 14 V 18 M 15 14 V 18 "
     "M 9 18 H 15 M 10 22 H 14"},
    {StockIcon::Loading, "Loading", "A 12 12 9 0 288"},
    // ------------------------------------------------------ Fönster och meny
    {StockIcon::Menu, "Menu", "M 4 6 H 20 M 4 12 H 20 M 4 18 H 20"},
    {StockIcon::MoreHorizontal, "MoreHorizontal", "D 5 12 1.5 D 12 12 1.5 D 19 12 1.5"},
    {StockIcon::MoreVertical, "MoreVertical", "D 12 5 1.5 D 12 12 1.5 D 12 19 1.5"},
    {StockIcon::Settings, "Settings",
     "C 12 12 6 C 12 12 2.5 B 10.3 2.5 3.4 4 0.5 B 10.3 17.5 3.4 4 0.5 "
     "B 2.5 10.3 4 3.4 0.5 B 17.5 10.3 4 3.4 0.5 "
     "F M 15 17.4 L 17.4 15 L 19.9 17.5 L 17.5 19.9 Z "
     "F M 9 17.4 L 6.6 15 L 4.1 17.5 L 6.5 19.9 Z "
     "F M 15 6.6 L 17.4 9 L 19.9 6.5 L 17.5 4.1 Z "
     "F M 9 6.6 L 6.6 9 L 4.1 6.5 L 6.5 4.1 Z"},
    {StockIcon::CloseWindow, "CloseWindow", "@kryss"},
    {StockIcon::Minimize, "Minimize", "M 5 17 H 19"},
    {StockIcon::Maximize, "Maximize", "R 4 4 16 16 1"},
    {StockIcon::Restore, "Restore", "R 4 8 12 12 1 M 8 4 H 18 Q 20 4 20 6 V 16"},
    {StockIcon::NewWindow, "NewWindow", "R 2 4 20 16 2 M 2 8 H 22 M 12 11 V 17 "
                                        "M 9 14 H 15"},
    {StockIcon::ExternalLink, "ExternalLink",
     "M 15 3 H 21 V 9 M 10 14 L 21 3 M 18 13 V 19 Q 18 21 16 21 H 5 Q 3 21 3 19 "
     "V 8 Q 3 6 5 6 H 11"},
    // --------------------------------------------------- Verktyg och ritning
    {StockIcon::Pointer, "Pointer", "M 4 4 L 20.5 10.7 L 13.5 13.5 L 10.7 20.5 Z"},
    {StockIcon::Pan, "Pan",
     "M 18 11 V 6 A 16 6 2 180 360 M 14 10 V 4 A 12 4 2 180 360 M 10 4 V 10.5 "
     "A 8 6 2 180 360 M 6 6 V 14 A 20 8 2 180 360 M 22 8 V 14 A 14 14 8 0 90 "
     "M 14 22 H 12 Q 9 22 6 19.7 L 2.4 16.1 Q 1 14.5 2.4 13.2 Q 3.8 12 5.2 13.2 "
     "L 7 15"},
    {StockIcon::Measure, "Measure",
     "M 21.3 15.3 Q 23 17 21.3 18.7 L 18.7 21.3 Q 17 23 15.3 21.3 L 2.7 8.7 "
     "Q 1 7 2.7 5.3 L 5.3 2.7 Q 7 1 8.7 2.7 Z M 14.5 12.5 L 16.5 10.5 "
     "M 11.5 9.5 L 13.5 7.5 M 8.5 6.5 L 10.5 4.5 M 17.5 15.5 L 19.5 13.5"},
    {StockIcon::Pen, "Pen",
     "M 15 21.3 L 12.7 19 L 18.7 13 L 21 15.3 Z M 18 13 L 16.6 6.1 L 3.2 2 "
     "L 2 3.2 L 6.1 16.6 L 13 18 M 2.3 2.3 L 9.6 9.6 C 11 11 2"},
    {StockIcon::Eraser, "Eraser",
     "M 21 21 H 8 L 3 16 Q 1.6 14.6 3 13.2 L 13 3.2 Q 14.4 1.8 15.8 3.2 L 21 8.4 "
     "Q 22.4 9.8 21 11.2 L 11.2 21 M 5.1 11.1 L 13.9 19.9"},
    {StockIcon::Eyedropper, "Eyedropper",
     "M 2 22 L 3 21 H 6 L 15 12 M 3 21 V 18 L 12 9 M 15 6 L 18.4 2.6 "
     "Q 19.9 1.1 21.4 2.6 Q 22.9 4.1 21.4 5.6 L 18 9 L 18.4 9.4 "
     "Q 19.9 10.9 18.4 12.4 Q 16.9 13.9 15.4 12.4 L 11.6 8.6 Q 10.1 7.1 11.6 5.6 "
     "Q 13.1 4.1 14.6 5.6 Z"},
    {StockIcon::Shape, "Shape", "M 8 10 L 12 3 L 16 10 Z R 3 14 7 7 1 C 17.5 17.5 3.5"},
    {StockIcon::Line, "Line", "M 4 20 L 20 4"},
    {StockIcon::Rectangle, "Rectangle", "R 3 5 18 14 2"},
    {StockIcon::Ellipse, "Ellipse", "E 12 12 10 7"},
    {StockIcon::Polygon, "Polygon", "M 12 2.5 L 21.5 9.4 L 17.9 20.5 H 6.1 L 2.5 9.4 Z"},
    {StockIcon::Text, "Text", "M 12 4 V 20 M 4 7 V 4 H 20 V 7 M 9 20 H 15"},
    {StockIcon::Layers, "Layers",
     "M 12 2 L 2 7 L 12 12 L 22 7 Z M 2 12 L 12 17 L 22 12 M 2 17 L 12 22 L 22 17"},
    {StockIcon::Anchor, "Anchor",
     "M 12 22 V 8 C 12 5 3 M 5 12 H 2 A 12 12 10 0 180 M 22 12 H 19"},
    {StockIcon::Magnet, "Magnet",
     "M 4 3 V 12 M 20 12 V 3 A 12 12 8 0 180 M 9 3 V 12 M 15 12 V 3 "
     "A 12 12 3 0 180 M 4 3 H 9 M 15 3 H 20 M 4 7 H 9 M 15 7 H 20"},
    // ----------------------------------------------------------------- Media
    {StockIcon::Play, "Play", "M 6 3 L 20 12 L 6 21 Z"},
    {StockIcon::Pause, "Pause", "R 6 4 4 16 1 R 14 4 4 16 1"},
    {StockIcon::MediaStop, "MediaStop", "R 6 6 12 12 2"},
    {StockIcon::Record, "Record", "C 12 12 8 D 12 12 4"},
    {StockIcon::PreviousTrack, "PreviousTrack", "M 19 20 L 9 12 L 19 4 Z M 5 19 V 5"},
    {StockIcon::NextTrack, "NextTrack", "M 5 4 L 15 12 L 5 20 Z M 19 5 V 19"},
    {StockIcon::Repeat, "Repeat",
     "M 17 2 L 21 6 L 17 10 M 3 11 V 10 Q 3 6 7 6 H 21 M 7 22 L 3 18 L 7 14 "
     "M 21 13 V 14 Q 21 18 17 18 H 3"},
    {StockIcon::Shuffle, "Shuffle",
     "M 18 14 L 22 18 L 18 22 M 18 2 L 22 6 L 18 10 M 2 18 H 4 Q 6 18 7.3 16.3 "
     "L 12.7 8.7 Q 14 6 16 6 H 22 M 2 6 H 4 Q 6.4 6 7.6 8.2 M 22 18 H 16 "
     "Q 14 18 12.7 16.2 L 12.3 15.7"},
    {StockIcon::SoundOn, "SoundOn", "@hogtalare A 12 12 5 323 397 A 12 12 9 315 405"},
    {StockIcon::SoundOff, "SoundOff", "@hogtalare M 16 9 L 22 15 M 22 9 L 16 15"},
    // -------------------------------------------- Kommunikation och person
    {StockIcon::User, "User", "C 12 7 4 M 19 21 V 19 Q 19 15 15 15 H 9 Q 5 15 5 19 V 21"},
    {StockIcon::Users, "Users",
     "C 9 7 4 M 16 21 V 19 Q 16 15 12 15 H 6 Q 2 15 2 19 V 21 "
     "M 22 21 V 19 Q 22 16 19 15.1 A 15 7 4 284.5 435.5"},
    {StockIcon::Mail, "Mail", "R 2 4 20 16 2 M 22 7 L 13 12.7 Q 12 13.3 11 12.7 L 2 7"},
    {StockIcon::Message, "Message",
     "M 21 15 Q 21 17 19 17 H 7 L 3 21 V 5 Q 3 3 5 3 H 19 Q 21 3 21 5 Z"},
    {StockIcon::Notification, "Notification",
     "M 4 17 H 20 M 20 17 Q 18 14 18 8 A 12 8 6 180 360 M 4 17 Q 6 14 6 8 "
     "M 10.3 21 Q 12 23 13.7 21"},
    {StockIcon::Share, "Share",
     "C 18 5 3 C 6 12 3 C 18 19 3 M 8.6 13.5 L 15.4 17.5 M 15.4 6.5 L 8.6 10.5"},
    {StockIcon::Link, "Link",
     "M 9 17 H 7 A 7 12 5 90 270 M 7 7 H 9 M 15 7 H 17 A 17 12 5 270 450 "
     "M 17 17 H 15 M 8 12 H 16"},
    {StockIcon::Attach, "Attach",
     "M 16 6 V 17 A 12 17 4 0 180 M 8 17 V 5 A 10.5 5 2.5 180 360 M 13 5 V 16"},
    // --------------------------------------------------------- Tid och data
    {StockIcon::Calendar, "Calendar", "R 3 4 18 18 2 M 8 2 V 6 M 16 2 V 6 M 3 10 H 21"},
    {StockIcon::Clock, "Clock", "C 12 12 10 M 12 6 V 12 L 16 14"},
    {StockIcon::History, "History",
     "A 12 12 9 270 540 M 12 3 Q 8 3 5.3 5.7 L 3 8 M 3 3 V 8 H 8 "
     "M 12 7 V 12 L 16 14"},
    {StockIcon::Chart, "Chart",
     "M 3 3 V 19 Q 3 21 5 21 H 21 M 18 17 V 9 M 13 17 V 5 M 8 17 V 14"},
    {StockIcon::Table, "Table", "R 3 3 18 18 2 M 3 9 H 21 M 3 15 H 21 M 12 3 V 21"},
    {StockIcon::Database, "Database",
     "E 12 5 9 3 M 3 5 V 19 M 21 5 V 19 U 12 12 9 3 0 180 U 12 19 9 3 0 180"},
    {StockIcon::Funnel, "Funnel", "M 3 3 H 21 L 14 11.5 V 21 L 10 19 V 11.5 Z"},
    {StockIcon::Tag, "Tag",
     "M 12.6 2.6 Q 12 2 11.2 2 H 4 Q 2 2 2 4 V 11.2 Q 2 12 2.6 12.6 L 11.3 21.3 "
     "Q 13 23 14.7 21.3 L 21.3 14.7 Q 23 13 21.3 11.3 Z D 7.5 7.5 1.2"},
    {StockIcon::Bookmark, "Bookmark",
     "M 19 21 L 12 17 L 5 21 V 5 Q 5 3 7 3 H 17 Q 19 3 19 5 Z"},
    {StockIcon::Star, "Star",
     "M 12 2 L 15.1 8.3 L 22 9.3 L 17 14.1 L 18.2 21 L 12 17.8 L 5.8 21 L 7 14.1 "
     "L 2 9.3 L 8.9 8.3 Z"},
    {StockIcon::Flag, "Flag",
     "M 4 22 V 3 M 4 3 Q 8 1 12 3 Q 16 5 20 3 V 15 Q 16 17 12 15 Q 8 13 4 15"},
    // -------------------------------------------------------------- Säkerhet
    {StockIcon::SecurityLock, "SecurityLock", "@las D 12 15.5 1.5 M 12 16 V 18.5"},
    {StockIcon::Key, "Key",
     "C 7.5 15.5 5.5 M 21 2 L 11.4 11.6 M 15.5 7.5 L 18.5 10.5 L 22 7 L 19 4"},
    {StockIcon::Shield, "Shield",
     "M 20 13 Q 20 19 12 22 Q 4 19 4 13 V 5.5 Q 8 5 12 2 Q 16 5 20 5.5 Z"},
    {StockIcon::LogIn, "LogIn",
     "M 10 17 L 15 12 L 10 7 M 15 12 H 3 M 15 3 H 19 Q 21 3 21 5 V 19 "
     "Q 21 21 19 21 H 15"},
    {StockIcon::LogOut, "LogOut",
     "M 16 17 L 21 12 L 16 7 M 21 12 H 9 M 9 21 H 5 Q 3 21 3 19 V 5 Q 3 3 5 3 "
     "H 9"},
    // ------------------------------------------------------------ CAD — rita
    // Greppen (de små fyllda rutorna) är CAD-programmens: där en form går att
    // ta i. Samma ruta, 3.2 i sida, på varje ritad form som har dem.
    {StockIcon::Point, "Point",
     "D 12 12 2.2 M 5 5 L 8.5 8.5 M 19 5 L 15.5 8.5 M 5 19 L 8.5 15.5 "
     "M 19 19 L 15.5 15.5"},
    {StockIcon::Polyline, "Polyline",
     "M 3 19 L 9 7 L 15 15 L 21 5 B 1.4 17.4 3.2 3.2 0.4 B 7.4 5.4 3.2 3.2 0.4 "
     "B 13.4 13.4 3.2 3.2 0.4 B 19.4 3.4 3.2 3.2 0.4"},
    {StockIcon::Arc, "Arc",
     "A 12 17 9 180 360 B 1.4 15.4 3.2 3.2 0.4 B 19.4 15.4 3.2 3.2 0.4 "
     "B 10.4 6.4 3.2 3.2 0.4"},
    {StockIcon::Circle, "Circle", "C 12 12 9 M 10 12 H 14 M 12 10 V 14"},
    {StockIcon::Spline, "Spline",
     "M 3 18 Q 7 4 12 12 Q 17 20 21 6 B 1.4 16.4 3.2 3.2 0.4 "
     "B 19.4 4.4 3.2 3.2 0.4"},
    {StockIcon::Hatch, "Hatch",
     "R 3 3 18 18 2 M 3 11 L 11 3 M 3 19 L 19 3 M 9 21 L 21 9 M 17 21 L 21 17"},
    {StockIcon::Dimension, "Dimension",
     "M 4 5 V 19 M 20 5 V 19 M 6 12 H 18 F M 4.5 12 L 8.5 9.5 V 14.5 Z "
     "F M 19.5 12 L 15.5 9.5 V 14.5 Z"},
    {StockIcon::AngleDimension, "AngleDimension",
     "M 4 20 H 21 M 4 20 L 14 6.3 A 4 20 11 306 360"},
    // ----------------------------------------------------------- CAD — ändra
    {StockIcon::Move, "Move",
     "M 12 2 V 22 M 2 12 H 22 M 9 5 L 12 2 L 15 5 M 9 19 L 12 22 L 15 19 "
     "M 5 9 L 2 12 L 5 15 M 19 9 L 22 12 L 19 15"},
    {StockIcon::Rotate, "Rotate",
     "R 8.5 8.5 7 7 1 A 12 12 9 0 315 M 18.4 5.6 L 21 8 M 21 3 V 8 H 16"},
    {StockIcon::Scale, "Scale",
     "R 3 13 8 8 1 M 3 9 V 5 Q 3 3 5 3 H 9 M 15 3 H 19 Q 21 3 21 5 V 9 "
     "M 21 15 V 19 Q 21 21 19 21 H 15 M 13 11 L 17 7 M 13 7 H 17 V 11"},
    {StockIcon::Offset, "Offset",
     "M 3 21 V 13 Q 3 5 11 5 H 21 M 9 21 V 13 Q 9 11 11 11 H 21"},
    {StockIcon::Trim, "Trim",
     "M 14 3 V 21 M 3 12 H 14 M 16.5 12 H 17.5 M 20 12 H 21"},
    {StockIcon::Extend, "Extend",
     "M 20 3 V 21 M 3 12 H 17 M 14 9 L 17 12 L 14 15"},
    {StockIcon::Fillet, "Fillet", "M 4 21 V 15 M 14 5 H 21 A 14 15 10 180 270"},
    {StockIcon::Chamfer, "Chamfer", "M 4 21 V 11 L 10 5 H 21"},
    {StockIcon::Array, "Array",
     "R 3 3 4 4 0.8 R 10 3 4 4 0.8 R 17 3 4 4 0.8 R 3 10 4 4 0.8 "
     "R 10 10 4 4 0.8 R 17 10 4 4 0.8 R 3 17 4 4 0.8 R 10 17 4 4 0.8 "
     "R 17 17 4 4 0.8"},
    // ----------------------------------------- CAD — fästpunkter och hjälp
    // Fästpunkternas märken är CAD-programmens: ruta vid änden, triangel mitt
    // på, ring i centrum, kryss i skärningen, vinkel vid normalen.
    {StockIcon::SnapEndpoint, "SnapEndpoint", "M 4 20 L 17 7 R 14 4 6 6 0"},
    {StockIcon::SnapMidpoint, "SnapMidpoint",
     "M 3 16 H 21 M 12 9.5 L 17.5 19 H 6.5 Z"},
    {StockIcon::SnapCenter, "SnapCenter", "A 12 12 9 160 380 C 12 12 3"},
    {StockIcon::SnapIntersection, "SnapIntersection",
     "M 3 15 H 21 M 9 3 V 21 M 5.5 11.5 L 12.5 18.5 M 12.5 11.5 L 5.5 18.5"},
    {StockIcon::SnapPerpendicular, "SnapPerpendicular",
     "M 3 19 H 21 M 10 19 V 4 M 10 14 H 15 V 19"},
    {StockIcon::SnapGrid, "SnapGrid",
     "D 4 4 1.1 D 9.3 4 1.1 D 14.7 4 1.1 D 20 4 1.1 D 4 9.3 1.1 D 9.3 9.3 1.1 "
     "D 14.7 9.3 1.1 D 20 9.3 1.1 D 4 14.7 1.1 D 9.3 14.7 1.1 D 14.7 14.7 1.1 "
     "D 20 14.7 1.1 D 4 20 1.1 D 9.3 20 1.1 D 14.7 20 1.1 D 20 20 1.1 "
     "C 14.7 9.3 2.8"},
    {StockIcon::Ortho, "Ortho",
     "M 5 4 V 19 H 20 M 2 7 L 5 4 L 8 7 M 17 16 L 20 19 L 17 22"},
    {StockIcon::Crosshair, "Crosshair",
     "M 12 2 V 9 M 12 15 V 22 M 2 12 H 9 M 15 12 H 22 R 9 9 6 6 0"},
    // --------------------------------------------------- 3D — vyer och kamera
    // Vyerna är kuben med den yta man ser rakt på FYLLD; den isometriska har
    // hörnet mot betraktaren markerat.
    {StockIcon::ViewTop, "ViewTop", "@kub F M 12 3 L 19.8 7.5 L 12 12 L 4.2 7.5 Z"},
    {StockIcon::ViewFront, "ViewFront", "@kub F M 4.2 7.5 L 12 12 V 21 L 4.2 16.5 Z"},
    {StockIcon::ViewRight, "ViewRight", "@kub F M 12 12 L 19.8 7.5 V 16.5 L 12 21 Z"},
    {StockIcon::ViewIsometric, "ViewIsometric", "@kub D 12 12 2.2"},
    {StockIcon::Perspective, "Perspective",
     "M 3 20 L 8 5 H 16 L 21 20 Z M 12 5 V 20 M 5.5 12.5 H 18.5"},
    {StockIcon::Orthographic, "Orthographic",
     "M 3 19 L 8 5 H 21 L 16 19 Z M 14.5 5 L 9.5 19 M 5.5 12 H 18.5"},
    {StockIcon::Orbit, "Orbit",
     "D 12 12 2.5 U 12 12 10 5 30 330 M 19.8 6.1 L 20.7 9.5 L 17.2 9.1"},
    {StockIcon::Camera, "Camera",
     "R 2 6 20 15 2 C 12 13.5 4 M 8 6 L 9.5 3 H 14.5 L 16 6"},
    {StockIcon::Light, "Light",
     "C 12 12 4 M 12 2 V 4 M 12 20 V 22 M 2 12 H 4 M 20 12 H 22 "
     "M 4.9 4.9 L 6.3 6.3 M 17.7 17.7 L 19.1 19.1 M 4.9 19.1 L 6.3 17.7 "
     "M 17.7 6.3 L 19.1 4.9"},
    {StockIcon::Axes, "Axes",
     "M 12 14 V 4 M 12 14 L 4 18.5 M 12 14 L 20 18.5 D 12 4 2 D 4 18.5 2 "
     "D 20 18.5 2"},
    {StockIcon::ZoomWindow, "ZoomWindow",
     "M 2 6 V 3 Q 2 2 3 2 H 6 M 10 2 H 13 Q 14 2 14 3 V 6 M 2 10 V 13 "
     "Q 2 14 3 14 H 6 C 15.5 15.5 4.5 M 22 22 L 18.7 18.7"},
    // ---------------------------------------------------------- 3D — former
    {StockIcon::Box, "Box", "@kub"},
    {StockIcon::Sphere, "Sphere",
     "C 12 12 9 U 12 12 9 3.5 0 180 U 12 12 3.5 9 270 450"},
    {StockIcon::Cylinder, "Cylinder",
     "E 12 5 7 2.5 M 5 5 V 19 M 19 5 V 19 U 12 19 7 2.5 0 180"},
    {StockIcon::Cone, "Cone", "M 12 3 L 4 18 M 12 3 L 20 18 E 12 18 8 3"},
    {StockIcon::Torus, "Torus",
     "E 12 12 10 6 M 6 10.5 Q 12 15.5 18 10.5 M 8.2 12 Q 12 9 15.8 12"},
    {StockIcon::Plane, "Plane",
     "M 2 13 L 12 7 L 22 13 L 12 19 Z M 7 10 L 17 16 M 17 10 L 7 16"},
    {StockIcon::Mesh, "Mesh",
     "R 3 5 18 14 0 M 9 5 V 19 M 15 5 V 19 M 3 12 H 21 M 3 5 L 15 19 "
     "M 9 5 L 21 19 M 15 5 L 21 12 M 3 12 L 9 19"},
    // ------------------------------------------ 3D — visning och modellering
    // Visningslägena är klotet: tråd, fyllt och renderat med en glans. De
    // booleska är två cirklar; det som tas bort står streckat.
    {StockIcon::Wireframe, "Wireframe", "C 12 12 9 E 12 12 9 3.5 E 12 12 3.5 9"},
    {StockIcon::Solid, "Solid", "D 12 12 9"},
    {StockIcon::Rendered, "Rendered", "C 12 12 9 A 12 12 5.5 195 255"},
    {StockIcon::SelectVertex, "SelectVertex",
     "@yta D 3 18 2 D 7 6 2 D 21 6 2 D 17 18 2"},
    {StockIcon::SelectEdge, "SelectEdge", "@yta D 7 6 2 D 21 6 2"},
    {StockIcon::SelectFace, "SelectFace", "@yta F @yta"},
    {StockIcon::Extrude, "Extrude",
     "M 2 17 L 12 12 L 22 17 L 12 22 Z M 12 17 V 3 M 8.5 6.5 L 12 3 L 15.5 6.5"},
    {StockIcon::Union, "Union", "A 8.5 12 6.5 57.4 302.6 A 15.5 12 6.5 237.4 482.6"},
    {StockIcon::Subtract, "Subtract",
     "A 8.5 12 6.5 57.4 302.6 A 15.5 12 6.5 122.6 237.4 "
     "A 15.5 12 6.5 266.4 291.4 A 15.5 12 6.5 320.5 345.5 "
     "A 15.5 12 6.5 374.5 399.5 A 15.5 12 6.5 428.6 453.6"},
    {StockIcon::Intersect, "Intersect",
     "A 8.5 12 6.5 302.6 417.4 A 15.5 12 6.5 122.6 237.4 "
     "A 8.5 12 6.5 86.4 111.4 A 8.5 12 6.5 140.5 165.5 "
     "A 8.5 12 6.5 194.5 219.5 A 8.5 12 6.5 248.6 273.6 "
     "A 15.5 12 6.5 266.4 291.4 A 15.5 12 6.5 320.5 345.5 "
     "A 15.5 12 6.5 374.5 399.5 A 15.5 12 6.5 428.6 453.6"},
    // -------------------------------------------------------------- Bild
    {StockIcon::Image, "Image",
     "R 3 4 18 16 2 M 3 17 L 9 11 L 14 16 M 12.5 14.5 L 15 12 L 21 18 C 15.5 "
     "8.5 1.8"},
    {StockIcon::Crop, "Crop",
     "M 6 2 V 16 Q 6 18 8 18 H 22 M 2 6 H 16 Q 18 6 18 8 V 22"},
    {StockIcon::Resize, "Resize",
     "R 2 2 20 20 2 M 7 17 L 17 7 M 12 7 H 17 V 12 M 7 12 V 17 H 12"},
    {StockIcon::Brightness, "Brightness",
     "C 12 12 4.5 F M 12 7.5 Q 16.5 7.5 16.5 12 Q 16.5 16.5 12 16.5 Z M 12 2 "
     "V 4.5 M 12 19.5 V 22 M 2 12 H 4.5 M 19.5 12 H 22 M 4.9 4.9 L 6.7 6.7 M "
     "17.3 17.3 L 19.1 19.1 M 4.9 19.1 L 6.7 17.3 M 17.3 6.7 L 19.1 4.9"},
    {StockIcon::Contrast, "Contrast",
     "C 12 12 9 F M 12 3 Q 3 3 3 12 Q 3 21 12 21 Z"},
    // ---------------------------------------------- Dockning och fönster
    {StockIcon::PanelLeft, "PanelLeft",
     "R 3 3 18 18 2 F M 5 3 H 9 V 21 H 5 Q 3 21 3 19 V 5 Q 3 3 5 3 Z"},
    {StockIcon::PanelRight, "PanelRight",
     "R 3 3 18 18 2 F M 19 3 H 15 V 21 H 19 Q 21 21 21 19 V 5 Q 21 3 19 3 Z"},
    {StockIcon::PanelBottom, "PanelBottom",
     "R 3 3 18 18 2 F M 3 15 H 21 V 19 Q 21 21 19 21 H 5 Q 3 21 3 19 Z"},
    {StockIcon::Float, "Float",
     "R 9 3 12 10 1.5 M 9 6.5 H 21 M 6 9 H 5 Q 3 9 3 11 V 19 Q 3 21 5 21 H "
     "13 Q 15 21 15 19 V 16"},
    {StockIcon::Tabs, "Tabs",
     "M 3 19 V 5 Q 3 3 5 3 H 10 Q 12 3 12 5 V 8 H 19 Q 21 8 21 10 V 19 Q 21 "
     "21 19 21 H 5 Q 3 21 3 19 M 14 8 V 5 Q 14 3 16 3 H 19 Q 21 3 21 5 V 8"},
    {StockIcon::SplitHorizontal, "SplitHorizontal",
     "R 3 3 18 18 2 M 3 12 H 21"},
    // ---------------------------------------------------- Kod och system
    {StockIcon::Terminal, "Terminal",
     "R 2 4 20 16 2 M 6 9 L 9 12 L 6 15 M 11 15 H 17"},
    {StockIcon::Code, "Code",
     "M 8 6 L 2 12 L 8 18 M 16 6 L 22 12 L 16 18 M 14 4 L 10 20"},
    {StockIcon::Bug, "Bug",
     "E 12 15 5 5.5 A 12 9.5 3 180 360 M 12 11 V 20 M 10 6.8 L 8 4 M 14 6.8 "
     "L 16 4 M 7.4 12 L 4 10 M 16.6 12 L 20 10 M 7 15.5 H 3.5 M 17 15.5 H "
     "20.5 M 7.6 18.6 L 4.5 21 M 16.4 18.6 L 19.5 21"},
    {StockIcon::Package, "Package",
     "@kub M 8.1 5.25 L 15.9 9.75 V 18.75"},
    // --------------------------------------------------- Karta och plats
    {StockIcon::MapPin, "MapPin",
     "M 12 22 Q 5 14.5 5 9.5 A 12 9.5 7 180 360 M 19 9.5 Q 19 14.5 12 22 C "
     "12 9.5 2.5"},
    {StockIcon::MyLocation, "MyLocation",
     "C 12 12 7 D 12 12 3 M 12 2 V 5 M 12 19 V 22 M 2 12 H 5 M 19 12 H 22"},
    {StockIcon::Compass, "Compass",
     "C 12 12 10 F M 12 4.5 L 15 12 H 9 Z M 9 12 L 12 19.5 L 15 12"},
    {StockIcon::Globe, "Globe",
     "C 12 12 10 E 12 12 4.5 10 M 2 12 H 22 M 3.3 7 H 20.7 M 3.3 17 H 20.7"},
    {StockIcon::Route, "Route",
     "C 5.5 18.5 2.5 C 18.5 5.5 2.5 M 5.5 16 V 14 Q 5.5 12 7.5 12 H 16.5 Q "
     "18.5 12 18.5 10 V 8"},
    {StockIcon::Terrain, "Terrain",
     "M 2 20 L 8.5 8 L 13 15.5 L 16 11.5 L 22 20 Z M 6.6 11.5 L 8.5 13 L "
     "10.6 11.5"},
    // ------------------------ Ordna objekt — det fyllda är det markerade
    {StockIcon::BringToFront, "BringToFront",
     "M 5.5 12 H 3.5 Q 2 12 2 10.5 V 3.5 Q 2 2 3.5 2 H 10.5 Q 12 2 12 3.5 V "
     "5.5 M 12 18.5 V 20.5 Q 12 22 13.5 22 H 20.5 Q 22 22 22 20.5 V 13.5 Q "
     "22 12 20.5 12 H 18.5 B 7 7 10 10 1.5"},
    {StockIcon::SendToBack, "SendToBack",
     "F M 3.5 2 H 10.5 Q 12 2 12 3.5 V 5.5 H 5.5 V 12 H 3.5 Q 2 12 2 10.5 V "
     "3.5 Q 2 2 3.5 2 Z M 10.5 17 H 8.5 Q 7 17 7 15.5 V 8.5 Q 7 7 8.5 7 H "
     "15.5 Q 17 7 17 8.5 V 10.5 R 12 12 10 10 1.5"},
    {StockIcon::BringForward, "BringForward",
     "M 7.5 15 H 4.5 Q 3 15 3 13.5 V 4.5 Q 3 3 4.5 3 H 13.5 Q 15 3 15 4.5 V "
     "7.5 B 9 9 12 12 1.5"},
    {StockIcon::SendBackward, "SendBackward",
     "F M 4.5 3 H 13.5 Q 15 3 15 4.5 V 7.5 H 7.5 V 15 H 4.5 Q 3 15 3 13.5 V "
     "4.5 Q 3 3 4.5 3 Z R 9 9 12 12 1.5"},
    {StockIcon::Ungroup, "Ungroup",
     "R 3 3 9 7 1 R 12 14 9 7 1 M 17 3 H 19 Q 21 3 21 5 V 7 M 3 17 V 19 Q 3 "
     "21 5 21 H 7"},
    {StockIcon::AlignObjectsLeft, "AlignObjectsLeft",
     "M 3 2 V 22 R 6 5 12 5 1 R 6 14 7 5 1"},
    {StockIcon::AlignObjectsCenter, "AlignObjectsCenter",
     "M 12 2 V 22 R 5 5 14 5 1 R 8 14 8 5 1"},
    {StockIcon::AlignObjectsRight, "AlignObjectsRight",
     "M 21 2 V 22 R 6 5 12 5 1 R 11 14 7 5 1"},
    {StockIcon::AlignObjectsTop, "AlignObjectsTop",
     "M 2 3 H 22 R 5 6 5 12 1 R 14 6 5 7 1"},
    {StockIcon::AlignObjectsMiddle, "AlignObjectsMiddle",
     "M 2 12 H 22 R 5 5 5 14 1 R 14 8 5 8 1"},
    {StockIcon::AlignObjectsBottom, "AlignObjectsBottom",
     "M 2 21 H 22 R 5 6 5 12 1 R 14 11 5 7 1"},
    {StockIcon::DistributeHorizontal, "DistributeHorizontal",
     "M 3 2 V 22 M 21 2 V 22 R 9 6 6 12 1"},
    {StockIcon::DistributeVertical, "DistributeVertical",
     "M 2 3 H 22 M 2 21 H 22 R 6 9 12 6 1"},
    // --- 3D — fler vyer och verktyg (de dolda sidornas kanter streckade)
    {StockIcon::ViewBottom, "ViewBottom",
     "@kub M 10.44 12.9 L 9.4 13.5 M 8.19 14.2 L 7.15 14.8 M 5.93 15.5 L "
     "4.89 16.1 M 13.56 12.9 L 14.6 13.5 M 15.81 14.2 L 16.85 14.8 M 18.07 "
     "15.5 L 19.11 16.1"},
    {StockIcon::ViewLeft, "ViewLeft",
     "@kub M 10.44 12.9 L 9.4 13.5 M 8.19 14.2 L 7.15 14.8 M 5.93 15.5 L "
     "4.89 16.1 M 12 10.2 V 9 M 12 7.6 V 6.4 M 12 5 V 3.8"},
    {StockIcon::ViewBack, "ViewBack",
     "@kub M 13.56 12.9 L 14.6 13.5 M 15.81 14.2 L 16.85 14.8 M 18.07 15.5 L "
     "19.11 16.1 M 12 10.2 V 9 M 12 7.6 V 6.4 M 12 5 V 3.8"},
    {StockIcon::Walk, "Walk",
     "F M 8.5 3.8 Q 11 3.8 11 7 Q 11 10.2 8.5 10.2 Q 6 10.2 6 7 Q 6 3.8 8.5 "
     "3.8 Z D 8.5 13 1.7 F M 15.5 9.8 Q 18 9.8 18 13 Q 18 16.2 15.5 16.2 Q "
     "13 16.2 13 13 Q 13 9.8 15.5 9.8 Z D 15.5 19 1.7"},
    {StockIcon::Section, "Section",
     "M 12 7.5 L 19.8 12 L 12 16.5 L 4.2 12 Z M 4.2 12 V 16.5 L 12 21 L 19.8 "
     "16.5 V 12 M 12 16.5 V 21 M 9.4 9 L 17.2 13.5 M 6.8 10.5 L 14.6 15"},
    // -------------------------------------------------------- CAD — fler
    {StockIcon::Explode, "Explode",
     "M 3 8 V 3 H 8 M 16 3 H 21 V 8 M 21 16 V 21 H 16 M 8 21 H 3 V 16 M 10 "
     "10 L 8 8 M 14 10 L 16 8 M 10 14 L 8 16 M 14 14 L 16 16 D 12 12 1.3"},
    {StockIcon::Block, "Block",
     "R 9 3 12 12 2 C 15 9 2.5 M 3 21 L 10.5 13.5 M 5.5 13.5 H 10.5 V 18.5"},
    {StockIcon::Note, "Note",
     "M 3 5 Q 3 3 5 3 H 19 Q 21 3 21 5 V 15 L 15 21 H 5 Q 3 21 3 19 Z M 21 "
     "15 H 17 Q 15 15 15 17 V 21 M 7 8 H 17 M 7 12 H 13"},
    {StockIcon::RevisionCloud, "RevisionCloud",
     "A 7 6 2.5 180 360 A 12 6 2.5 180 360 A 17 6 2.5 180 360 A 19.5 9 3 270 "
     "450 A 19.5 15 3 270 450 A 17 18 2.5 0 180 A 12 18 2.5 0 180 A 7 18 2.5 "
     "0 180 A 4.5 15 3 90 270 A 4.5 9 3 90 270"},
    {StockIcon::Area, "Area",
     "M 3 19 L 5 6 L 14 3 L 21 10 L 17 21 Z M 4.45 9.55 L 9.5 4.5 M 3.2 17.8 "
     "L 16 5 M 8.25 19.75 L 19.5 8.5 M 14.4 20.6 L 18.7 16.3"},
    // ------------------------------------------------------- Text — fler
    {StockIcon::Strikethrough, "Strikethrough",
     "M 17 6.5 Q 16 4 12 4 Q 7 4 7 7.5 Q 7 10 12 11 M 12 13 Q 17 14 17 16.5 "
     "Q 17 20 12 20 Q 8 20 7 17.5 M 4 12 H 20"},
    {StockIcon::Indent, "Indent",
     "M 3 4 H 21 M 11 9.5 H 21 M 11 14.5 H 21 M 3 20 H 21 M 3 9 L 6.5 12 L 3 "
     "15"},
    {StockIcon::Outdent, "Outdent",
     "M 3 4 H 21 M 11 9.5 H 21 M 11 14.5 H 21 M 3 20 H 21 M 6.5 9 L 3 12 L "
     "6.5 15"},
    {StockIcon::Heading, "Heading",
     "M 4 4 V 20 M 14 4 V 20 M 4 12 H 14 M 17.5 11 L 20 9 V 20"},
    {StockIcon::Quote, "Quote",
     "B 3.5 10 6.5 7 1 M 4.25 11 Q 4.25 6 9 5 B 13.5 10 6.5 7 1 M 14.25 11 Q "
     "14.25 6 19 5"},
    {StockIcon::CodeBlock, "CodeBlock",
     "R 2 4 20 16 2 M 9 9 L 6 12 L 9 15 M 15 9 L 18 12 L 15 15"},
    // ----------------------------------------------------------- Diagram
    {StockIcon::LineChart, "LineChart",
     "M 3 3 V 19 Q 3 21 5 21 H 21 M 7 16 L 11 11 L 14 14 L 20 6"},
    {StockIcon::PieChart, "PieChart",
     "C 12 12 9 M 12 3 V 12 L 19.8 16.5"},
    {StockIcon::Sum, "Sum",
     "M 18 4 H 6 L 12.5 12 L 6 20 H 18"},
    // --------------------------------------------------- Status — i ring
    {StockIcon::AddCircle, "AddCircle",
     "C 12 12 10 M 12 7 V 17 M 7 12 H 17"},
    {StockIcon::RemoveCircle, "RemoveCircle",
     "C 12 12 10 M 7 12 H 17"},
    {StockIcon::CheckCircle, "CheckCircle",
     "C 12 12 10 M 16.5 8.5 L 10.5 15 L 7.5 12"},
    {StockIcon::Blocked, "Blocked",
     "C 12 12 10 M 4.9 4.9 L 19.1 19.1"},
    {StockIcon::Online, "Online",
     "D 12 12 6"},
    {StockIcon::Offline, "Offline",
     "C 12 12 6"},
    // ---------------------------------------------------------- Hårdvara
    {StockIcon::Laptop, "Laptop",
     "R 4 4 16 11 1.5 M 2 19 H 22"},
    {StockIcon::Phone, "Phone",
     "R 6 2 12 20 2.5 M 10.5 18 H 13.5"},
    {StockIcon::Keyboard, "Keyboard",
     "R 2 6 20 12 2 D 6 10 0.9 D 10 10 0.9 D 14 10 0.9 D 18 10 0.9 M 7 14.5 "
     "H 17"},
    {StockIcon::Mouse, "Mouse",
     "R 6 2 12 20 6 M 12 6 V 10"},
    {StockIcon::Cpu, "Cpu",
     "R 6 6 12 12 1.5 R 9.5 9.5 5 5 0.5 M 9 2 V 6 M 15 2 V 6 M 9 18 V 22 M "
     "15 18 V 22 M 2 9 H 6 M 2 15 H 6 M 18 9 H 22 M 18 15 H 22"},
    {StockIcon::Battery, "Battery",
     "R 2 7 17 10 2 M 22 10.5 V 13.5 B 5 10 7 4 0.5"},
    {StockIcon::Power, "Power",
     "A 12 13 8 300 600 M 12 3 V 12"},
    {StockIcon::Wifi, "Wifi",
     "D 12 19 1.3 A 12 19 4.5 225 315 A 12 19 9 225 315 A 12 19 13.5 225 315"},
    {StockIcon::Usb, "Usb",
     "M 12 3 V 18.5 M 9.5 5.5 L 12 3 L 14.5 5.5 M 12 15 L 7 12 V 9.6 M 12 "
     "12.5 L 17 9.5 V 7.6 C 7 8.3 1.3 R 15.7 5 2.6 2.6 0.3 D 12 19.5 2"},
    // --------------------------------------------------------------- Tid
    {StockIcon::Timer, "Timer",
     "M 6 3 H 18 M 6 21 H 18 M 7 3 V 6 Q 7 9 12 12 Q 17 15 17 18 V 21 M 17 3 "
     "V 6 Q 17 9 12 12 Q 7 15 7 18 V 21"},
    {StockIcon::Stopwatch, "Stopwatch",
     "C 12 13.5 8 M 10 2 H 14 M 12 2 V 5.5 M 12 13.5 V 9 M 18.5 6 L 20 4.5"},
    // --------------------------------------------------- Diagonala pilar
    {StockIcon::ArrowUpLeft, "ArrowUpLeft",
     "M 17 17 L 7 7 M 16 7 H 7 V 16"},
    {StockIcon::ArrowUpRight, "ArrowUpRight",
     "M 7 17 L 17 7 M 8 7 H 17 V 16"},
    {StockIcon::ArrowDownLeft, "ArrowDownLeft",
     "M 17 7 L 7 17 M 7 8 V 17 H 16"},
    {StockIcon::ArrowDownRight, "ArrowDownRight",
     "M 7 7 L 17 17 M 17 8 V 17 H 8"},
    // ------------------- Filtyper — kategorier (sidan med symbolen stor)
    {StockIcon::FileImage, "FileImage",
     "@fil M 7 19 L 10.5 14 L 13 17 L 14.5 15.5 L 17 19 Z C 9.5 11 1.4"},
    {StockIcon::FileVideo, "FileVideo",
     "@fil F M 10 12 L 15.5 15.5 L 10 19 Z"},
    {StockIcon::FileAudio, "FileAudio",
     "@fil M 10.5 18.5 V 12 L 15.5 11 V 17.5 D 9.2 18.5 1.6 D 14.2 17.5 1.6"},
    {StockIcon::FileArchive, "FileArchive",
     "@fil M 11 5 V 6 M 12.8 7.5 V 8.5 M 11 10 V 11 M 12.8 12.5 V 13.5 R 10 "
     "15.5 4 3.5 0.8"},
    {StockIcon::FileCode, "FileCode",
     "@fil M 10 12 L 7.5 15.5 L 10 19 M 14 12 L 16.5 15.5 L 14 19"},
    {StockIcon::FileSpreadsheet, "FileSpreadsheet",
     "@fil R 7.5 11 9 8 0.8 M 7.5 15 H 16.5 M 12 11 V 19"},
    {StockIcon::FilePresentation, "FilePresentation",
     "@fil R 7.5 11 9 6 0.8 M 12 17 V 19.5"},
    {StockIcon::FileGeo, "FileGeo",
     "@fil C 12 13.5 2.8 M 9.8 15.2 L 12 19 L 14.2 15.2"},
    {StockIcon::FileCad, "FileCad",
     "@fil M 8 19 V 11 L 16 19 Z M 8 13.5 H 9.5 M 8 16 H 9.5"},
    {StockIcon::FileModel, "FileModel",
     "@fil M 12 11 L 15.5 13 V 17 L 12 19 L 8.5 17 V 13 Z M 8.5 13 L 12 15 L "
     "15.5 13 M 12 15 V 19"},
    {StockIcon::FilePointCloud, "FilePointCloud",
     "@fil D 8.5 12 0.9 D 12 11 0.9 D 15.5 12.5 0.9 D 10 15 0.9 D 13.8 15.5 "
     "0.9 D 8 18 0.9 D 11.5 19 0.9 D 15.5 18.5 0.9"},
    {StockIcon::FileDatabase, "FileDatabase",
     "@fil E 12 11.8 4 1.5 M 8 11.8 V 17.5 M 16 11.8 V 17.5 U 12 17.5 4 1.5 "
     "0 180"},
    // --------------------------------- Filtyper — geodata (grön etikett)
    {StockIcon::FileShp, "FileShp",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 SHP"},
    {StockIcon::FileGeoJson, "FileGeoJson",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 GEOJSON"},
    {StockIcon::FileKml, "FileKml",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 KML"},
    {StockIcon::FileKmz, "FileKmz",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 KMZ"},
    {StockIcon::FileGpx, "FileGpx",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 GPX"},
    {StockIcon::FileGpkg, "FileGpkg",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 GPKG"},
    {StockIcon::FileGml, "FileGml",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 GML"},
    {StockIcon::FileGdb, "FileGdb",
     "@fil @fdb B 1 12 19 8 1.5 K 2 13.5 17 5 GDB"},
    {StockIcon::FileOsm, "FileOsm",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 OSM"},
    {StockIcon::FileGeoTiff, "FileGeoTiff",
     "@fil @fglob B 1 12 19 8 1.5 K 2 13.5 17 5 GEOTIFF"},
    {StockIcon::FileMbTiles, "FileMbTiles",
     "@fil @fglob B 1 12 19 8 1.5 K 2 13.5 17 5 MBTILES"},
    {StockIcon::FileTab, "FileTab",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 TAB"},
    {StockIcon::FileQgz, "FileQgz",
     "@fil @fpin B 1 12 19 8 1.5 K 2 13.5 17 5 QGZ"},
    // ------------------------------ Filtyper — CAD och BIM (röd etikett)
    {StockIcon::FileDwg, "FileDwg",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 DWG"},
    {StockIcon::FileDxf, "FileDxf",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 DXF"},
    {StockIcon::FileDgn, "FileDgn",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 DGN"},
    {StockIcon::FileIfc, "FileIfc",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 IFC"},
    {StockIcon::FileStep, "FileStep",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 STEP"},
    {StockIcon::FileIges, "FileIges",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 IGES"},
    {StockIcon::FileRvt, "FileRvt",
     "@fil @fvinkel B 1 12 19 8 1.5 K 2 13.5 17 5 RVT"},
    // --------------------------------- Filtyper — 3D-modeller (bärnsten)
    {StockIcon::FileObj, "FileObj",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 OBJ"},
    {StockIcon::FileFbx, "FileFbx",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 FBX"},
    {StockIcon::FileGltf, "FileGltf",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 GLTF"},
    {StockIcon::FileGlb, "FileGlb",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 GLB"},
    {StockIcon::FileStl, "FileStl",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 STL"},
    {StockIcon::File3mf, "File3mf",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 3MF"},
    {StockIcon::FileDae, "FileDae",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 DAE"},
    {StockIcon::FileSkp, "FileSkp",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 SKP"},
    {StockIcon::FileUsd, "FileUsd",
     "@fil @fkub B 1 12 19 8 1.5 K 2 13.5 17 5 USD"},
    // ----------------------------------- Filtyper — punktmoln (bärnsten)
    {StockIcon::FileLas, "FileLas",
     "@fil @fmoln B 1 12 19 8 1.5 K 2 13.5 17 5 LAS"},
    {StockIcon::FileLaz, "FileLaz",
     "@fil @fmoln B 1 12 19 8 1.5 K 2 13.5 17 5 LAZ"},
    {StockIcon::FileE57, "FileE57",
     "@fil @fmoln B 1 12 19 8 1.5 K 2 13.5 17 5 E57"},
    {StockIcon::FilePly, "FilePly",
     "@fil @fmoln B 1 12 19 8 1.5 K 2 13.5 17 5 PLY"},
    {StockIcon::FileXyz, "FileXyz",
     "@fil @fmoln B 1 12 19 8 1.5 K 2 13.5 17 5 XYZ"},
    // ------------------------------------------- Filtyper — bilder (gul)
    {StockIcon::FilePng, "FilePng",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 PNG"},
    {StockIcon::FileJpg, "FileJpg",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 JPG"},
    {StockIcon::FileGif, "FileGif",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 GIF"},
    {StockIcon::FileBmp, "FileBmp",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 BMP"},
    {StockIcon::FileTiff, "FileTiff",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 TIFF"},
    {StockIcon::FileWebp, "FileWebp",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 WEBP"},
    {StockIcon::FileSvg, "FileSvg",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 SVG"},
    {StockIcon::FileHeic, "FileHeic",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 HEIC"},
    {StockIcon::FileIco, "FileIco",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 ICO"},
    {StockIcon::FilePsd, "FilePsd",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 PSD"},
    {StockIcon::FileRaw, "FileRaw",
     "@fil @fbild B 1 12 19 8 1.5 K 2 13.5 17 5 RAW"},
    // --- Filtyper — dokument (text blå, kalkyl grön, presentation bärnsten, PDF röd)
    {StockIcon::FilePdf, "FilePdf",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 PDF"},
    {StockIcon::FileDoc, "FileDoc",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 DOC"},
    {StockIcon::FileDocx, "FileDocx",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 DOCX"},
    {StockIcon::FileOdt, "FileOdt",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 ODT"},
    {StockIcon::FileRtf, "FileRtf",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 RTF"},
    {StockIcon::FileTxt, "FileTxt",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 TXT"},
    {StockIcon::FileMd, "FileMd",
     "@fil @ftext B 1 12 19 8 1.5 K 2 13.5 17 5 MD"},
    {StockIcon::FileXls, "FileXls",
     "@fil @ftabell B 1 12 19 8 1.5 K 2 13.5 17 5 XLS"},
    {StockIcon::FileXlsx, "FileXlsx",
     "@fil @ftabell B 1 12 19 8 1.5 K 2 13.5 17 5 XLSX"},
    {StockIcon::FileOds, "FileOds",
     "@fil @ftabell B 1 12 19 8 1.5 K 2 13.5 17 5 ODS"},
    {StockIcon::FileCsv, "FileCsv",
     "@fil @ftabell B 1 12 19 8 1.5 K 2 13.5 17 5 CSV"},
    {StockIcon::FilePpt, "FilePpt",
     "@fil @fslide B 1 12 19 8 1.5 K 2 13.5 17 5 PPT"},
    {StockIcon::FilePptx, "FilePptx",
     "@fil @fslide B 1 12 19 8 1.5 K 2 13.5 17 5 PPTX"},
    {StockIcon::FileOdp, "FileOdp",
     "@fil @fslide B 1 12 19 8 1.5 K 2 13.5 17 5 ODP"},
    // -------------------------------- Filtyper — data och webb (neutral)
    {StockIcon::FileJson, "FileJson",
     "@fil @fklammer B 1 12 19 8 1.5 K 2 13.5 17 5 JSON"},
    {StockIcon::FileYaml, "FileYaml",
     "@fil @fklammer B 1 12 19 8 1.5 K 2 13.5 17 5 YAML"},
    {StockIcon::FileXml, "FileXml",
     "@fil @fkod B 1 12 19 8 1.5 K 2 13.5 17 5 XML"},
    {StockIcon::FileHtml, "FileHtml",
     "@fil @fkod B 1 12 19 8 1.5 K 2 13.5 17 5 HTML"},
    {StockIcon::FileSql, "FileSql",
     "@fil @fdb B 1 12 19 8 1.5 K 2 13.5 17 5 SQL"},
    {StockIcon::FileSqlite, "FileSqlite",
     "@fil @fdb B 1 12 19 8 1.5 K 2 13.5 17 5 SQLITE"},
    // ---------------------------------------- Filtyper — arkiv (neutral)
    {StockIcon::FileZip, "FileZip",
     "@fil @fdragkedja B 1 12 19 8 1.5 K 2 13.5 17 5 ZIP"},
    {StockIcon::File7z, "File7z",
     "@fil @fdragkedja B 1 12 19 8 1.5 K 2 13.5 17 5 7Z"},
    {StockIcon::FileRar, "FileRar",
     "@fil @fdragkedja B 1 12 19 8 1.5 K 2 13.5 17 5 RAR"},
    {StockIcon::FileTar, "FileTar",
     "@fil @fdragkedja B 1 12 19 8 1.5 K 2 13.5 17 5 TAR"},
    {StockIcon::FileGz, "FileGz",
     "@fil @fdragkedja B 1 12 19 8 1.5 K 2 13.5 17 5 GZ"},
    // ------------------------------- Filtyper — ljud och video (neutral)
    {StockIcon::FileMp3, "FileMp3",
     "@fil @fnot B 1 12 19 8 1.5 K 2 13.5 17 5 MP3"},
    {StockIcon::FileWav, "FileWav",
     "@fil @fnot B 1 12 19 8 1.5 K 2 13.5 17 5 WAV"},
    {StockIcon::FileMp4, "FileMp4",
     "@fil @fspela B 1 12 19 8 1.5 K 2 13.5 17 5 MP4"},
    {StockIcon::FileMov, "FileMov",
     "@fil @fspela B 1 12 19 8 1.5 K 2 13.5 17 5 MOV"},
};

constexpr int kAntal = static_cast<int>(sizeof(kDefs) / sizeof(kDefs[0]));

// Tabellen i uppräkningens ordning och komplett — ett tillägg i enum:en utan
// en rad här, eller en rad på fel plats, kompilerar inte.
constexpr bool iOrdning()
{
    for (int i = 0; i < kAntal; ++i)
        if (static_cast<int>(kDefs[i].icon) != i)
            return false;
    return kAntal == static_cast<int>(StockIcon::FileMov) + 1;
}
static_assert(iOrdning(), "kDefs ska ha en rad per StockIcon, i ordning");

// FÄRGVARIANTENS ACCENTER (`StockIconStyle::Color`) — konturen ovan ritas som
// i mono, och det här läggs till: samma språk, plus ett FÄRGORD (`blue`,
// `green`, `red`, `amber`, `yellow` — `Theme::Colors::stock*`) som börjar ett
// nytt lager i den färgen, och `under` (förvalet: bakom konturen, för ytor)
// eller `over` (ovanpå, för en linje som byter färg). `all <färg>` först
// färgar hela konturen. En ikon som saknas här ritas likadant i båda stilarna.
struct Färg {
    StockIcon   icon;
    const char* path;
};

constexpr Färg kFärger[] = {
    // Navigering
    {StockIcon::Back, "blue D 12 12 10"},
    {StockIcon::Forward, "blue D 12 12 10"},
    {StockIcon::Up, "blue D 12 12 10"},
    {StockIcon::Down, "blue D 12 12 10"},
    {StockIcon::Home, "blue F M 5 9.5 L 12 3.6 L 19 9.5 V 21 H 5 Z"},
    {StockIcon::Previous, "blue D 12 12 10"},
    {StockIcon::Next, "blue D 12 12 10"},
    {StockIcon::Refresh, "all green"},
    {StockIcon::Stop, "red F M 8.3 3 H 15.7 L 21 8.3 V 15.7 L 15.7 21 H 8.3 "
                      "L 3 15.7 V 8.3 Z"},
    {StockIcon::GoTo, "blue F M 3 11 L 21 3 L 13 21 L 11 13 Z"},
    // Pilar
    {StockIcon::ArrowLeft, "all blue"},
    {StockIcon::ArrowRight, "all blue"},
    {StockIcon::ArrowUp, "all blue"},
    {StockIcon::ArrowDown, "all blue"},
    {StockIcon::Flip, "blue F M 3 7 L 8 12 L 3 17 Z F M 21 7 L 16 12 L 21 17 Z"},
    {StockIcon::RotateClockwise, "all blue"},
    {StockIcon::RotateCounterClockwise, "all blue"},
    {StockIcon::Swap, "all blue"},
    // Fil och dokument
    {StockIcon::New, "over green M 9 15 H 15 M 12 12 V 18"},
    {StockIcon::Open, "yellow F @oppen"},
    {StockIcon::Save, "blue F @disk"},
    {StockIcon::SaveAs, "blue F M 5 3 H 16 L 21 8 V 10 L 13.5 17.5 L 13 21 H 5 "
                        "Q 3 21 3 19 V 5 Q 3 3 5 3 Z amber "
                        "F M 13 21 L 13.5 18.5 L 19.5 12.5 L 21.5 14.5 L 15.5 20.5 Z"},
    {StockIcon::SaveAll, "blue F M 8 2 H 17 L 22 7 V 16 Q 22 18 20 18 H 8 "
                         "Q 6 18 6 16 V 4 Q 6 2 8 2 Z"},
    {StockIcon::Close, "over red M 9.5 12.5 L 14.5 17.5 M 14.5 12.5 L 9.5 17.5"},
    {StockIcon::Print, "blue F M 2 11 Q 2 9 4 9 H 20 Q 22 9 22 11 V 16 "
                       "Q 22 18 20 18 H 18 V 14 H 6 V 18 H 4 Q 2 18 2 16 Z"},
    {StockIcon::PrintPreview, "blue D 16 16 3"},
    {StockIcon::Import, "over green M 12 3 V 15 M 8 11 L 12 15 L 16 11"},
    {StockIcon::Export, "over blue M 12 15 V 3 M 8 7 L 12 3 L 16 7"},
    {StockIcon::Properties, "over blue M 14 2 V 6 M 8 10 V 14 M 16 18 V 22"},
    {StockIcon::Folder, "yellow F @mapp"},
    {StockIcon::FolderOpen, "yellow F @oppen"},
    {StockIcon::NewFolder, "yellow F @mapp over green M 12 10 V 16 M 9 13 H 15"},
    {StockIcon::Recent, "blue D 15.5 16.5 5"},
    // Lagring och plats
    {StockIcon::Drive, "over green D 6 16 1 D 10 16 1"},
    {StockIcon::Network, "blue B 9 2 6 6 1 B 2 16 6 6 1 B 16 16 6 6 1"},
    {StockIcon::Cloud, "blue D 9 12 7 D 17.5 14.5 4.5 B 9 10 8.5 9 0"},
    {StockIcon::HomeFolder, "yellow F @mapp"},
    {StockIcon::Desktop, "blue B 2 3 20 14 2"},
    {StockIcon::Trash, "red F M 5 6 H 19 V 20 Q 19 22 17 22 H 7 Q 5 22 5 20 Z"},
    {StockIcon::Download, "over green M 7 10 L 12 15 L 17 10 M 12 15 V 3"},
    {StockIcon::Upload, "over blue M 17 8 L 12 3 L 7 8 M 12 3 V 15"},
    {StockIcon::Sync, "all blue"},
    // Redigering
    {StockIcon::Undo, "all blue"},
    {StockIcon::Redo, "all blue"},
    {StockIcon::Cut, "red D 6 6 3 D 6 18 3"},
    {StockIcon::Copy, "blue B 8 8 14 14 2"},
    {StockIcon::Paste, "amber F M 6 4 H 18 Q 20 4 20 6 V 20 Q 20 22 18 22 H 6 "
                       "Q 4 22 4 20 V 6 Q 4 4 6 4 Z"},
    {StockIcon::Delete, "over red M 12 9 L 18 15 M 18 9 L 12 15"},
    {StockIcon::SelectAll, "blue B 8 8 8 8 1"},
    {StockIcon::Search, "blue D 11 11 8"},
    {StockIcon::Replace, "blue B 2 2 8 8 1 B 14 14 8 8 1"},
    {StockIcon::Clear, "amber F M 5 14 L 3 22 H 21 L 19 14 Z"},
    {StockIcon::Edit, "amber F M 3 21 L 4 16.5 L 16.5 4 Q 18 2.5 19.5 4 L 20 4.5 "
                      "Q 21.5 6 20 7.5 L 7.5 20 Z"},
    {StockIcon::Rename, "over blue M 9 5 V 19 M 7 4 Q 9 4 9 6 Q 9 4 11 4 "
                        "M 7 20 Q 9 20 9 18 Q 9 20 11 20"},
    {StockIcon::Duplicate, "blue B 8 8 14 14 2 over green M 15 12 V 18 M 12 15 H 18"},
    // Listor och objekt
    {StockIcon::Add, "all green"},
    {StockIcon::Remove, "all red"},
    {StockIcon::MoveUp, "over blue M 18 20 V 4 M 15 7 L 18 4 L 21 7"},
    {StockIcon::MoveDown, "over blue M 18 4 V 20 M 15 17 L 18 20 L 21 17"},
    {StockIcon::MoveFirst, "over blue M 12 21 V 8 M 7 13 L 12 8 L 17 13"},
    {StockIcon::MoveLast, "over blue M 12 3 V 16 M 7 11 L 12 16 L 17 11"},
    {StockIcon::SortAscending, "over blue M 3 8 L 7 4 L 11 8 M 7 4 V 20"},
    {StockIcon::SortDescending, "over blue M 3 16 L 7 20 L 11 16 M 7 20 V 4"},
    {StockIcon::Group, "blue B 7 7 7 5 1 B 10 12 7 5 1"},
    {StockIcon::Expand, "over blue M 8 12 H 16 M 12 8 V 16"},
    {StockIcon::Collapse, "over blue M 8 12 H 16"},
    {StockIcon::ExpandAll, "over blue M 15 19 L 12 22 L 9 19 M 15 5 L 12 2 L 9 5"},
    {StockIcon::CollapseAll, "over blue M 15 19 L 12 16 L 9 19 M 15 5 L 12 8 L 9 5"},
    // Vyer
    {StockIcon::ListView, "blue B 3 3 7 7 1 B 3 14 7 7 1"},
    {StockIcon::DetailsView, "over blue B 3 4 3 3 0.5 B 3 10.5 3 3 0.5 B 3 17 3 3 0.5"},
    {StockIcon::IconView, "blue B 3 3 7 7 1 B 14 3 7 7 1 B 3 14 7 7 1 B 14 14 7 7 1"},
    {StockIcon::SplitView, "blue B 12 3 9 18 2"},
    {StockIcon::Sidebar, "blue B 3 3 6 18 2"},
    {StockIcon::FullScreen, "all blue"},
    {StockIcon::ExitFullScreen, "all blue"},
    // Zoom och visning
    {StockIcon::ZoomIn, "blue D 11 11 8"},
    {StockIcon::ZoomOut, "blue D 11 11 8"},
    {StockIcon::ZoomToFit, "all blue"},
    {StockIcon::ActualSize, "blue B 2 3 20 18 2"},
    {StockIcon::Show, "blue D 12 12 3"},
    {StockIcon::Hide, "blue D 12 12 3 over red M 3 3 L 21 21"},
    {StockIcon::Lock, "amber B 3 11 18 11 2"},
    {StockIcon::Unlock, "amber B 3 11 18 11 2"},
    {StockIcon::Pin, "red F M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 Z "
                     "B 6 2 12 4 2"},
    {StockIcon::Unpin, "red F M 9 6 V 11 L 5 14.5 V 17 H 19 V 14.5 L 15 11 V 6 Z "
                       "B 6 2 12 4 2"},
    // Text och formatering
    {StockIcon::TextColor, "over red M 4 20 H 20"},
    {StockIcon::FillColor, "blue D 20 20 2 F M 20 16 L 18.3 19.5 H 21.7 Z"},
    // Status och dialog
    {StockIcon::Information, "blue D 12 12 10"},
    {StockIcon::Warning, "amber F M 10.3 4 Q 12 1.2 13.7 4 L 21.7 18 Q 23.2 21 20 21 "
                         "H 4 Q 0.8 21 2.3 18 Z"},
    {StockIcon::Error, "red D 12 12 10"},
    {StockIcon::Question, "blue D 12 12 10"},
    {StockIcon::Done, "all green"},
    {StockIcon::Cancel, "all red"},
    {StockIcon::Help, "all blue"},
    {StockIcon::Tip, "yellow F M 15 14 Q 15.3 12.8 16.5 11.5 Q 18 10 18 8 Q 18 2 12 2 "
                     "Q 6 2 6 8 Q 6 10 7.5 11.5 Q 8.7 12.8 9 14 V 18 H 15 Z"},
    {StockIcon::Loading, "all blue"},
    // Fönster och meny
    {StockIcon::CloseWindow, "all red"},
    {StockIcon::NewWindow, "blue F M 2 8 V 6 Q 2 4 4 4 H 20 Q 22 4 22 6 V 8 Z "
                           "over green M 12 11 V 17 M 9 14 H 15"},
    {StockIcon::ExternalLink, "over blue M 15 3 H 21 V 9 M 10 14 L 21 3"},
    // Verktyg och ritning
    {StockIcon::Measure, "amber F M 21.3 15.3 Q 23 17 21.3 18.7 L 18.7 21.3 "
                         "Q 17 23 15.3 21.3 L 2.7 8.7 Q 1 7 2.7 5.3 L 5.3 2.7 "
                         "Q 7 1 8.7 2.7 Z"},
    {StockIcon::Pen, "blue F M 18 13 L 16.6 6.1 L 3.2 2 L 2 3.2 L 6.1 16.6 L 13 18 Z"},
    {StockIcon::Eraser, "red F M 13 3.2 Q 14.4 1.8 15.8 3.2 L 21 8.4 Q 22.4 9.8 21 11.2 "
                        "L 13.9 19.9 L 5.1 11.1 Z"},
    {StockIcon::Eyedropper, "blue F M 15 6 L 18.4 2.6 Q 19.9 1.1 21.4 2.6 "
                            "Q 22.9 4.1 21.4 5.6 L 18 9 Z"},
    {StockIcon::Shape, "red F M 8 10 L 12 3 L 16 10 Z blue B 3 14 7 7 1 "
                       "green D 17.5 17.5 3.5"},
    {StockIcon::Line, "all blue"},
    {StockIcon::Rectangle, "blue B 3 5 18 14 2"},
    {StockIcon::Ellipse, "all blue"},
    {StockIcon::Polygon, "blue F M 12 2.5 L 21.5 9.4 L 17.9 20.5 H 6.1 L 2.5 9.4 Z"},
    {StockIcon::Layers, "blue F M 12 2 L 2 7 L 12 12 L 22 7 Z"},
    {StockIcon::Anchor, "all blue"},
    {StockIcon::Magnet, "red B 4 3 5 4 0 B 15 3 5 4 0"},
    // Media
    {StockIcon::Play, "green F M 6 3 L 20 12 L 6 21 Z"},
    {StockIcon::Pause, "blue B 6 4 4 16 1 B 14 4 4 16 1"},
    {StockIcon::MediaStop, "red B 6 6 12 12 2"},
    {StockIcon::Record, "over red D 12 12 4"},
    {StockIcon::PreviousTrack, "blue F M 19 20 L 9 12 L 19 4 Z"},
    {StockIcon::NextTrack, "blue F M 5 4 L 15 12 L 5 20 Z"},
    {StockIcon::Repeat, "all blue"},
    {StockIcon::Shuffle, "all blue"},
    {StockIcon::SoundOn, "over blue A 12 12 5 323 397 A 12 12 9 315 405"},
    {StockIcon::SoundOff, "over red M 16 9 L 22 15 M 22 9 L 16 15"},
    // Kommunikation och person
    {StockIcon::User, "blue D 12 7 4 F M 19 21 V 19 Q 19 15 15 15 H 9 Q 5 15 5 19 "
                      "V 21 Z"},
    {StockIcon::Users, "blue D 9 7 4 F M 16 21 V 19 Q 16 15 12 15 H 6 Q 2 15 2 19 "
                       "V 21 Z"},
    {StockIcon::Mail, "blue B 2 4 20 16 2"},
    {StockIcon::Message, "blue F M 21 15 Q 21 17 19 17 H 7 L 3 21 V 5 Q 3 3 5 3 H 19 "
                         "Q 21 3 21 5 Z"},
    {StockIcon::Notification, "amber F M 4 17 Q 6 14 6 8 Q 6 2 12 2 Q 18 2 18 8 "
                              "Q 18 14 20 17 Z"},
    {StockIcon::Share, "blue D 18 5 3 D 6 12 3 D 18 19 3"},
    {StockIcon::Link, "all blue"},
    // Tid och data
    {StockIcon::Calendar, "red F M 3 10 V 6 Q 3 4 5 4 H 19 Q 21 4 21 6 V 10 Z"},
    {StockIcon::Clock, "over blue M 12 6 V 12 L 16 14"},
    {StockIcon::History, "over blue M 12 7 V 12 L 16 14"},
    {StockIcon::Chart, "over blue M 18 17 V 9 green M 13 17 V 5 red M 8 17 V 14"},
    {StockIcon::Table, "blue F M 3 9 V 5 Q 3 3 5 3 H 19 Q 21 3 21 5 V 9 Z"},
    {StockIcon::Database, "all blue"},
    {StockIcon::Funnel, "blue F M 3 3 H 21 L 14 11.5 V 21 L 10 19 V 11.5 Z"},
    {StockIcon::Tag, "amber F M 12.6 2.6 Q 12 2 11.2 2 H 4 Q 2 2 2 4 V 11.2 "
                     "Q 2 12 2.6 12.6 L 11.3 21.3 Q 13 23 14.7 21.3 L 21.3 14.7 "
                     "Q 23 13 21.3 11.3 Z"},
    {StockIcon::Bookmark, "red F M 19 21 L 12 17 L 5 21 V 5 Q 5 3 7 3 H 17 "
                          "Q 19 3 19 5 Z"},
    {StockIcon::Star, "amber F M 12 2 L 15.1 8.3 L 22 9.3 L 17 14.1 L 18.2 21 "
                      "L 12 17.8 L 5.8 21 L 7 14.1 L 2 9.3 L 8.9 8.3 Z"},
    {StockIcon::Flag, "red F M 4 3 Q 8 1 12 3 Q 16 5 20 3 V 15 Q 16 17 12 15 "
                      "Q 8 13 4 15 Z"},
    // Säkerhet
    {StockIcon::SecurityLock, "amber B 3 11 18 11 2"},
    {StockIcon::Key, "amber D 7.5 15.5 5.5"},
    {StockIcon::Shield, "blue F M 20 13 Q 20 19 12 22 Q 4 19 4 13 V 5.5 Q 8 5 12 2 "
                        "Q 16 5 20 5.5 Z"},
    {StockIcon::LogIn, "over green M 10 17 L 15 12 L 10 7 M 15 12 H 3"},
    {StockIcon::LogOut, "over red M 16 17 L 21 12 L 16 7 M 21 12 H 9"},
    // CAD — rita: formen blå som de andra ritverktygen, det som MÄTS blått
    // ovanpå en svart hjälplinje.
    {StockIcon::Point, "over blue D 12 12 2.2"},
    {StockIcon::Polyline, "all blue"},
    {StockIcon::Arc, "all blue"},
    {StockIcon::Circle, "all blue"},
    {StockIcon::Spline, "all blue"},
    {StockIcon::Hatch, "over blue M 3 11 L 11 3 M 3 19 L 19 3 M 9 21 L 21 9 "
                       "M 17 21 L 21 17"},
    {StockIcon::Dimension, "over blue M 6 12 H 18 F M 4.5 12 L 8.5 9.5 V 14.5 Z "
                           "F M 19.5 12 L 15.5 9.5 V 14.5 Z"},
    {StockIcon::AngleDimension, "over blue A 4 20 11 306 360"},
    // CAD — ändra: RESULTATET blått, det som tas bort rött.
    {StockIcon::Move, "all blue"},
    {StockIcon::Rotate, "blue B 8.5 8.5 7 7 1"},
    {StockIcon::Scale, "blue B 3 13 8 8 1"},
    {StockIcon::Offset, "over blue M 9 21 V 13 Q 9 11 11 11 H 21"},
    {StockIcon::Trim, "over red M 16.5 12 H 17.5 M 20 12 H 21"},
    {StockIcon::Extend, "over blue M 10 12 H 17 M 14 9 L 17 12 L 14 15"},
    {StockIcon::Fillet, "over blue A 14 15 10 180 270"},
    {StockIcon::Chamfer, "over blue M 4 11 L 10 5"},
    {StockIcon::Array, "blue B 3 3 4 4 0.8"},
    // Fästpunkterna: märket grönt, som markören i ett CAD-program.
    {StockIcon::SnapEndpoint, "over green R 14 4 6 6 0"},
    {StockIcon::SnapMidpoint, "over green M 12 9.5 L 17.5 19 H 6.5 Z"},
    {StockIcon::SnapCenter, "over green C 12 12 3"},
    {StockIcon::SnapIntersection, "over green M 5.5 11.5 L 12.5 18.5 "
                                  "M 12.5 11.5 L 5.5 18.5"},
    {StockIcon::SnapPerpendicular, "over green M 10 14 H 15 V 19"},
    {StockIcon::SnapGrid, "over green C 14.7 9.3 2.8"},
    {StockIcon::Ortho, "all blue"},
    {StockIcon::Crosshair, "over blue R 9 9 6 6 0"},
    // 3D — vyer och kamera: den sedda ytan blå; axlarna X röd, Y grön, Z blå.
    {StockIcon::ViewTop, "over blue F M 12 3 L 19.8 7.5 L 12 12 L 4.2 7.5 Z"},
    {StockIcon::ViewFront, "over blue F M 4.2 7.5 L 12 12 V 21 L 4.2 16.5 Z"},
    {StockIcon::ViewRight, "over blue F M 12 12 L 19.8 7.5 V 16.5 L 12 21 Z"},
    {StockIcon::ViewIsometric, "over blue D 12 12 2.2"},
    {StockIcon::Perspective, "blue F M 3 20 L 8 5 H 16 L 21 20 Z"},
    {StockIcon::Orthographic, "blue F M 3 19 L 8 5 H 21 L 16 19 Z"},
    {StockIcon::Orbit, "over blue U 12 12 10 5 30 330 "
                       "M 19.8 6.1 L 20.7 9.5 L 17.2 9.1"},
    {StockIcon::Camera, "blue D 12 13.5 4"},
    {StockIcon::Light, "yellow D 12 12 4"},
    {StockIcon::Axes, "over red M 12 14 L 4 18.5 D 4 18.5 2 "
                      "green M 12 14 L 20 18.5 D 20 18.5 2 "
                      "blue M 12 14 V 4 D 12 4 2"},
    {StockIcon::ZoomWindow, "blue D 15.5 15.5 4.5"},
    // 3D — former: kroppen blå bakom konturen.
    {StockIcon::Box, "blue F M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z"},
    {StockIcon::Sphere, "blue D 12 12 9"},
    {StockIcon::Cylinder, "blue F M 5 5 V 19 Q 12 24 19 19 V 5 Q 12 0 5 5 Z"},
    {StockIcon::Cone, "blue F M 12 3 L 4 18 Q 12 24 20 18 Z"},
    {StockIcon::Torus, "all blue"},
    {StockIcon::Plane, "blue F M 2 13 L 12 7 L 22 13 L 12 19 Z"},
    {StockIcon::Mesh, "all blue"},
    // 3D — visning och modellering: det markerade och resultatet blått, det
    // som tas bort rött.
    {StockIcon::Wireframe, "all blue"},
    {StockIcon::Solid, "all blue"},
    {StockIcon::Rendered, "blue D 12 12 9"},
    {StockIcon::SelectVertex, "over blue D 3 18 2 D 7 6 2 D 21 6 2 D 17 18 2"},
    {StockIcon::SelectEdge, "over blue M 7 6 H 21 D 7 6 2 D 21 6 2"},
    {StockIcon::SelectFace, "over blue F @yta"},
    {StockIcon::Extrude, "blue F M 2 17 L 12 12 L 22 17 L 12 22 Z "
                         "over green M 12 17 V 3 M 8.5 6.5 L 12 3 L 15.5 6.5"},
    {StockIcon::Union, "blue D 8.5 12 6.5 D 15.5 12 6.5"},
    {StockIcon::Subtract, "over blue A 8.5 12 6.5 57.4 302.6 "
                          "A 15.5 12 6.5 122.6 237.4 "
                          "red A 15.5 12 6.5 266.4 291.4 A 15.5 12 6.5 320.5 345.5 "
                          "A 15.5 12 6.5 374.5 399.5 A 15.5 12 6.5 428.6 453.6"},
    {StockIcon::Intersect, "over blue A 8.5 12 6.5 302.6 417.4 "
                           "A 15.5 12 6.5 122.6 237.4"},
    // Bild
    {StockIcon::Image,
     "over green M 3 17 L 9 11 L 14 16 M 12.5 14.5 L 15 12 L 21 18 yellow D "
     "15.5 8.5 1.8"},
    {StockIcon::Crop,
     "all blue"},
    {StockIcon::Resize,
     "over blue M 7 17 L 17 7 M 12 7 H 17 V 12 M 7 12 V 17 H 12"},
    {StockIcon::Brightness,
     "yellow D 12 12 4.5"},
    // Dockning och fönster
    {StockIcon::PanelLeft,
     "over blue F M 5 3 H 9 V 21 H 5 Q 3 21 3 19 V 5 Q 3 3 5 3 Z"},
    {StockIcon::PanelRight,
     "over blue F M 19 3 H 15 V 21 H 19 Q 21 21 21 19 V 5 Q 21 3 19 3 Z"},
    {StockIcon::PanelBottom,
     "over blue F M 3 15 H 21 V 19 Q 21 21 19 21 H 5 Q 3 21 3 19 Z"},
    {StockIcon::Float,
     "blue F M 9 6.5 V 4.5 Q 9 3 10.5 3 H 19.5 Q 21 3 21 4.5 V 6.5 Z"},
    {StockIcon::Tabs,
     "blue F M 3 19 V 5 Q 3 3 5 3 H 10 Q 12 3 12 5 V 8 H 19 Q 21 8 21 10 V "
     "19 Q 21 21 19 21 H 5 Q 3 21 3 19 Z"},
    {StockIcon::SplitHorizontal,
     "blue B 3 12 18 9 2"},
    // Kod och system
    {StockIcon::Terminal,
     "over green M 6 9 L 9 12 L 6 15 M 11 15 H 17"},
    {StockIcon::Code,
     "all blue"},
    {StockIcon::Bug,
     "red F M 12 9.5 Q 17 9.5 17 15 Q 17 20.5 12 20.5 Q 7 20.5 7 15 Q 7 9.5 "
     "12 9.5 Z"},
    {StockIcon::Package,
     "amber F M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z"},
    // Karta och plats
    {StockIcon::MapPin,
     "red F M 12 22 Q 5 14.5 5 9.5 Q 5 2.5 12 2.5 Q 19 2.5 19 9.5 Q 19 14.5 "
     "12 22 Z"},
    {StockIcon::MyLocation,
     "over blue D 12 12 3"},
    {StockIcon::Compass,
     "over red F M 12 4.5 L 15 12 H 9 Z"},
    {StockIcon::Globe,
     "blue D 12 12 10"},
    {StockIcon::Route,
     "over blue M 5.5 16 V 14 Q 5.5 12 7.5 12 H 16.5 Q 18.5 12 18.5 10 V 8 "
     "green C 5.5 18.5 2.5 red C 18.5 5.5 2.5"},
    {StockIcon::Terrain,
     "green F M 2 20 L 8.5 8 L 13 15.5 L 16 11.5 L 22 20 Z"},
    // Ordna objekt — det fyllda är det markerade
    {StockIcon::BringToFront,
     "over blue B 7 7 10 10 1.5"},
    {StockIcon::SendToBack,
     "over blue F M 3.5 2 H 10.5 Q 12 2 12 3.5 V 5.5 H 5.5 V 12 H 3.5 Q 2 12 "
     "2 10.5 V 3.5 Q 2 2 3.5 2 Z"},
    {StockIcon::BringForward,
     "over blue B 9 9 12 12 1.5"},
    {StockIcon::SendBackward,
     "over blue F M 4.5 3 H 13.5 Q 15 3 15 4.5 V 7.5 H 7.5 V 15 H 4.5 Q 3 15 "
     "3 13.5 V 4.5 Q 3 3 4.5 3 Z"},
    {StockIcon::Ungroup,
     "blue B 3 3 9 7 1 B 12 14 9 7 1"},
    {StockIcon::AlignObjectsLeft,
     "blue B 6 5 12 5 1 B 6 14 7 5 1"},
    {StockIcon::AlignObjectsCenter,
     "blue B 5 5 14 5 1 B 8 14 8 5 1"},
    {StockIcon::AlignObjectsRight,
     "blue B 6 5 12 5 1 B 11 14 7 5 1"},
    {StockIcon::AlignObjectsTop,
     "blue B 5 6 5 12 1 B 14 6 5 7 1"},
    {StockIcon::AlignObjectsMiddle,
     "blue B 5 5 5 14 1 B 14 8 5 8 1"},
    {StockIcon::AlignObjectsBottom,
     "blue B 5 6 5 12 1 B 14 11 5 7 1"},
    {StockIcon::DistributeHorizontal,
     "blue B 9 6 6 12 1"},
    {StockIcon::DistributeVertical,
     "blue B 6 9 12 6 1"},
    // 3D — fler vyer och verktyg (de dolda sidornas kanter streckade)
    {StockIcon::ViewBottom,
     "blue F M 12 12 L 19.8 16.5 L 12 21 L 4.2 16.5 Z"},
    {StockIcon::ViewLeft,
     "blue F M 12 3 L 4.2 7.5 V 16.5 L 12 12 Z"},
    {StockIcon::ViewBack,
     "blue F M 12 3 L 19.8 7.5 V 16.5 L 12 12 Z"},
    {StockIcon::Walk,
     "all blue"},
    {StockIcon::Section,
     "blue F M 12 7.5 L 19.8 12 L 12 16.5 L 4.2 12 Z"},
    // CAD — fler
    {StockIcon::Explode,
     "over amber M 10 10 L 8 8 M 14 10 L 16 8 M 10 14 L 8 16 M 14 14 L 16 16 "
     "D 12 12 1.3"},
    {StockIcon::Block,
     "blue B 9 3 12 12 2 over green M 3 21 L 10.5 13.5 M 5.5 13.5 H 10.5 V "
     "18.5"},
    {StockIcon::Note,
     "yellow F M 3 5 Q 3 3 5 3 H 19 Q 21 3 21 5 V 15 L 15 21 H 5 Q 3 21 3 19 "
     "Z"},
    {StockIcon::RevisionCloud,
     "all red"},
    {StockIcon::Area,
     "blue F M 3 19 L 5 6 L 14 3 L 21 10 L 17 21 Z"},
    // Text — fler
    {StockIcon::Strikethrough,
     "over red M 4 12 H 20"},
    {StockIcon::Indent,
     "over blue M 3 9 L 6.5 12 L 3 15"},
    {StockIcon::Outdent,
     "over blue M 6.5 9 L 3 12 L 6.5 15"},
    {StockIcon::Quote,
     "all blue"},
    {StockIcon::CodeBlock,
     "over blue M 9 9 L 6 12 L 9 15 M 15 9 L 18 12 L 15 15"},
    // Diagram
    {StockIcon::LineChart,
     "over blue M 7 16 L 11 11 L 14 14 L 20 6"},
    {StockIcon::PieChart,
     "blue F M 12 12 V 3 Q 17.2 3 19.8 7.5 Q 22.4 12 19.8 16.5 Z"},
    // Status — i ring
    {StockIcon::AddCircle,
     "green D 12 12 10"},
    {StockIcon::RemoveCircle,
     "red D 12 12 10"},
    {StockIcon::CheckCircle,
     "green D 12 12 10"},
    {StockIcon::Blocked,
     "all red"},
    {StockIcon::Online,
     "all green"},
    // Hårdvara
    {StockIcon::Laptop,
     "blue B 4 4 16 11 1.5"},
    {StockIcon::Phone,
     "blue B 6 2 12 20 2.5"},
    {StockIcon::Keyboard,
     "blue B 2 6 20 12 2"},
    {StockIcon::Mouse,
     "blue B 6 2 12 20 6"},
    {StockIcon::Cpu,
     "blue B 9.5 9.5 5 5 0.5"},
    {StockIcon::Battery,
     "over green B 5 10 7 4 0.5"},
    {StockIcon::Wifi,
     "all blue"},
    // Tid
    {StockIcon::Timer,
     "amber F M 9 19 Q 12 16 15 19 V 21 H 9 Z"},
    {StockIcon::Stopwatch,
     "over red M 12 13.5 V 9"},
    // Diagonala pilar
    {StockIcon::ArrowUpLeft,
     "all blue"},
    {StockIcon::ArrowUpRight,
     "all blue"},
    {StockIcon::ArrowDownLeft,
     "all blue"},
    {StockIcon::ArrowDownRight,
     "all blue"},
    // Filtyper — kategorier (sidan med symbolen stor)
    {StockIcon::FileImage,
     "over green M 7 19 L 10.5 14 L 13 17 L 14.5 15.5 L 17 19 Z yellow D 9.5 "
     "11 1.4"},
    {StockIcon::FileVideo,
     "over red F M 10 12 L 15.5 15.5 L 10 19 Z"},
    {StockIcon::FileAudio,
     "over blue M 10.5 18.5 V 12 L 15.5 11 V 17.5 D 9.2 18.5 1.6 D 14.2 17.5 "
     "1.6"},
    {StockIcon::FileArchive,
     "amber B 10 15.5 4 3.5 0.8"},
    {StockIcon::FileCode,
     "over blue M 10 12 L 7.5 15.5 L 10 19 M 14 12 L 16.5 15.5 L 14 19"},
    {StockIcon::FileSpreadsheet,
     "green B 7.5 11 9 8 0.8"},
    {StockIcon::FilePresentation,
     "amber B 7.5 11 9 6 0.8"},
    {StockIcon::FileGeo,
     "over green C 12 13.5 2.8 M 9.8 15.2 L 12 19 L 14.2 15.2"},
    {StockIcon::FileCad,
     "red F M 8 19 V 11 L 16 19 Z"},
    {StockIcon::FileModel,
     "amber F M 12 11 L 15.5 13 V 17 L 12 19 L 8.5 17 V 13 Z"},
    {StockIcon::FilePointCloud,
     "over amber D 8.5 12 0.9 D 12 11 0.9 D 15.5 12.5 0.9 D 10 15 0.9 D 13.8 "
     "15.5 0.9 D 8 18 0.9 D 11.5 19 0.9 D 15.5 18.5 0.9"},
    {StockIcon::FileDatabase,
     "blue F M 8 11.8 V 17.5 Q 12 20.5 16 17.5 V 11.8 Q 12 8.8 8 11.8 Z"},
    // Filtyper — geodata (grön etikett)
    {StockIcon::FileShp,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileGeoJson,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileKml,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileKmz,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileGpx,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileGpkg,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileGml,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileGdb,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileOsm,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileGeoTiff,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileMbTiles,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileTab,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileQgz,
     "over strong green B 1 12 19 8 1.5"},
    // Filtyper — CAD och BIM (röd etikett)
    {StockIcon::FileDwg,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileDxf,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileDgn,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileIfc,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileStep,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileIges,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileRvt,
     "over strong red B 1 12 19 8 1.5"},
    // Filtyper — 3D-modeller (bärnsten)
    {StockIcon::FileObj,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileFbx,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileGltf,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileGlb,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileStl,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::File3mf,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileDae,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileSkp,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileUsd,
     "over strong amber B 1 12 19 8 1.5"},
    // Filtyper — punktmoln (bärnsten)
    {StockIcon::FileLas,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileLaz,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileE57,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FilePly,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileXyz,
     "over strong amber B 1 12 19 8 1.5"},
    // Filtyper — bilder (gul)
    {StockIcon::FilePng,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileJpg,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileGif,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileBmp,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileTiff,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileWebp,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileSvg,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileHeic,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileIco,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FilePsd,
     "over strong yellow B 1 12 19 8 1.5"},
    {StockIcon::FileRaw,
     "over strong yellow B 1 12 19 8 1.5"},
    // Filtyper — dokument (text blå, kalkyl grön, presentation bärnsten, PDF röd)
    {StockIcon::FilePdf,
     "over strong red B 1 12 19 8 1.5"},
    {StockIcon::FileDoc,
     "over strong blue B 1 12 19 8 1.5"},
    {StockIcon::FileDocx,
     "over strong blue B 1 12 19 8 1.5"},
    {StockIcon::FileOdt,
     "over strong blue B 1 12 19 8 1.5"},
    {StockIcon::FileRtf,
     "over strong blue B 1 12 19 8 1.5"},
    {StockIcon::FileXls,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileXlsx,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileOds,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FileCsv,
     "over strong green B 1 12 19 8 1.5"},
    {StockIcon::FilePpt,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FilePptx,
     "over strong amber B 1 12 19 8 1.5"},
    {StockIcon::FileOdp,
     "over strong amber B 1 12 19 8 1.5"},
    // Filtyper — data och webb (neutral)
    // Filtyper — arkiv (neutral)
    // Filtyper — ljud och video (neutral)
};
// GLYFERNA för den utstansade texten — streck i en ruta på 3 × 5 (rad 0
// överst), samma språk som ikonerna men bara M L H V Q Z. Siffran 0 är O.
struct Glyf {
    char        tecken;
    const char* path;
};

constexpr Glyf kGlyfer[] = {
    {'A', "M 0 5 L 1.5 0 L 3 5 M 0.55 3.4 H 2.45"},
    {'B', "M 0 2.5 H 1.8 Q 2.7 2.5 2.7 1.25 Q 2.7 0 1.8 0 H 0 V 5 H 1.9 "
          "Q 3 5 3 3.75 Q 3 2.5 1.9 2.5"},
    {'C', "M 3 0.8 Q 2.6 0 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 2.6 5 3 4.2"},
    {'D', "M 0 0 V 5 H 1.2 Q 3 5 3 2.5 Q 3 0 1.2 0 Z"},
    {'E', "M 3 0 H 0 V 5 H 3 M 0 2.5 H 2.2"},
    {'F', "M 3 0 H 0 V 5 M 0 2.5 H 2.2"},
    {'G', "M 3 0.8 Q 2.6 0 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 3.2 V 2.7 H 1.7"},
    {'H', "M 0 0 V 5 M 3 0 V 5 M 0 2.5 H 3"},
    {'I', "M 1.5 0 V 5 M 0.5 0 H 2.5 M 0.5 5 H 2.5"},
    {'J', "M 3 0 V 3.5 Q 3 5 1.5 5 Q 0 5 0 3.6"},
    {'K', "M 0 0 V 5 M 3 0 L 0 3.2 M 1.1 2 L 3 5"},
    {'L', "M 0 0 V 5 H 3"},
    {'M', "M 0 5 V 0 L 1.5 2.8 L 3 0 V 5"},
    {'N', "M 0 5 V 0 L 3 5 V 0"},
    {'O', "M 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 2.5 Q 3 0 1.5 0 Z"},
    {'P', "M 0 5 V 0 H 1.8 Q 3 0 3 1.4 Q 3 2.8 1.8 2.8 H 0"},
    {'Q', "M 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 2.5 Q 3 0 1.5 0 Z "
          "M 1.9 3.7 L 3 5"},
    {'R', "M 0 5 V 0 H 1.8 Q 3 0 3 1.4 Q 3 2.8 1.8 2.8 H 0 M 1.6 2.8 L 3 5"},
    {'S', "M 3 0.7 Q 2.5 0 1.5 0 Q 0 0 0 1.3 Q 0 2.4 1.5 2.5 Q 3 2.6 3 3.7 "
          "Q 3 5 1.5 5 Q 0.5 5 0 4.3"},
    {'T', "M 0 0 H 3 M 1.5 0 V 5"},
    {'U', "M 0 0 V 3.5 Q 0 5 1.5 5 Q 3 5 3 3.5 V 0"},
    {'V', "M 0 0 L 1.5 5 L 3 0"},
    {'W', "M 0 0 L 0.7 5 L 1.5 1.8 L 2.3 5 L 3 0"},
    {'X', "M 0 0 L 3 5 M 3 0 L 0 5"},
    {'Y', "M 0 0 L 1.5 2.5 L 3 0 M 1.5 2.5 V 5"},
    {'Z', "M 0 0 H 3 L 0 5 H 3"},
    {'0', "M 1.5 0 Q 0 0 0 2.5 Q 0 5 1.5 5 Q 3 5 3 2.5 Q 3 0 1.5 0 Z"},
    {'1', "M 0.6 1.1 L 1.9 0 V 5"},
    {'2', "M 0 1 Q 0.4 0 1.5 0 Q 3 0 3 1.5 Q 3 2.5 0 5 H 3"},
    {'3', "M 0 0 H 3 L 1.4 2 Q 3 2 3 3.5 Q 3 5 1.5 5 Q 0.5 5 0 4.3"},
    {'4', "M 2.2 5 V 0 L 0 3.5 H 3"},
    {'5', "M 3 0 H 0.3 L 0 2.3 Q 0.6 2 1.5 2 Q 3 2 3 3.5 Q 3 5 1.5 5 Q 0.5 5 0 4.3"},
    {'6', "M 2.7 0.3 Q 2.2 0 1.5 0 Q 0 0 0 2.8 V 3.4 Q 0 5 1.5 5 Q 3 5 3 3.4 "
          "Q 3 2 1.5 2 Q 0.3 2 0 3"},
    {'7', "M 0 0 H 3 L 1 5"},
    {'8', "M 1.5 2.4 Q 0.2 2.4 0.2 1.2 Q 0.2 0 1.5 0 Q 2.8 0 2.8 1.2 "
          "Q 2.8 2.4 1.5 2.4 Q 0 2.4 0 3.7 Q 0 5 1.5 5 Q 3 5 3 3.7 Q 3 2.4 1.5 2.4"},
    {'9', "M 0.3 4.7 Q 0.8 5 1.5 5 Q 3 5 3 2.2 V 1.6 Q 3 0 1.5 0 Q 0 0 0 1.6 "
          "Q 0 3 1.5 3 Q 2.7 3 3 2"},
};
// lang_gate:på

// ------------------------------------------------------------- ritningen

// En tolkad beskrivning i rutans enheter.
struct Ritning {
    struct Båge {   // ellipsens kontur; en cirkel har rx == ry
        float cx, cy, rx, ry, a0, a1;
        bool  hel;
    };
    struct Ruta {
        float x, y, w, h, r;
        bool  fylld;
    };
    struct Skiva {
        float cx, cy, r;
    };
    std::vector<gfx::Pointf>              streck;   // par: segmentens ändar
    std::vector<Båge>                     bågar;
    std::vector<Ruta>                     rutor;
    std::vector<Skiva>                    skivor;
    std::vector<std::vector<gfx::Pointf>> ytor;     // fyllda polygoner
    std::vector<gfx::Pointf>              hål;      // par: utstansad text
    std::vector<float>                    hålH;     // halva strecket per par
};

// En beskrivning ritad förskjuten och skalad — glyferna i en text.
struct Avbildning {
    float s = 1.0f, dx = 0.0f, dy = 0.0f;
    float x(float v) const { return dx + v * s; }
    float y(float v) const { return dy + v * s; }
};

void tolka(const char* text, Ritning& ut, int djup, bool fyllAlla,
           Avbildning a = {});

// `K`: texten, glyf för glyf, som utstansade streck i rutan (x, y, b, h) —
// centrerad, och nedskalad i sin helhet när den inte ryms. Ett tecken utan
// glyf lämnar en lucka.
void stansa(const std::string& text, float x, float y, float b, float h,
            Ritning& ut)
{
    if (text.empty())
        return;
    const float n     = static_cast<float>(text.size());
    const float bredd = n * kGlyfB + (n - 1.0f) * kGlyfSteg;
    const float s     = std::min({1.0f, h / kGlyfH, b / bredd});
    const float x0    = x + (b - bredd * s) / 2.0f;
    const float y0    = y + (h - kGlyfH * s) / 2.0f;
    for (std::size_t k = 0; k < text.size(); ++k) {
        char c = text[k];
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        for (const Glyf& g : kGlyfer)
            if (g.tecken == c) {
                Ritning glyf;
                tolka(g.path, glyf, 1, false,
                      {s, x0 + static_cast<float>(k) * (kGlyfB + kGlyfSteg) * s, y0});
                ut.hål.insert(ut.hål.end(), glyf.streck.begin(), glyf.streck.end());
                ut.hålH.resize(ut.hål.size() / 2, kTextStreck * s / 2.0f);
                break;
            }
    }
}

// Beskrivningens ord, åtskilda av mellanslag.
std::vector<std::string> ord(const char* text)
{
    std::vector<std::string> ut;
    std::string              o;
    for (const char* p = text; ; ++p) {
        if (*p == ' ' || *p == '\0') {
            if (!o.empty())
                ut.push_back(std::move(o));
            o.clear();
            if (*p == '\0')
                break;
        } else {
            o += *p;
        }
    }
    return ut;
}

// `fyllAlla` fyller varje linje — en DEL efter `F` (`F @mapp`).
void tolka(const char* text, Ritning& ut, int djup, bool fyllAlla, Avbildning a)
{
    const std::vector<std::string> o = ord(text);
    std::size_t                    i = 0;
    bool                           fel = false;
    const auto tal = [&]() {
        double v = 0.0;
        if (i >= o.size() || !propparse::parseDouble(o[i], v)) {
            fel = true;
            return 0.0f;
        }
        ++i;
        return static_cast<float>(v);
    };

    std::vector<gfx::Pointf> väg;   // linjen som byggs
    bool fylls = false, nästaFylls = false;
    const auto avsluta = [&] {
        if (väg.size() >= 2) {
            if (fylls) {
                ut.ytor.push_back(väg);
            } else {
                for (std::size_t k = 1; k < väg.size(); ++k) {
                    ut.streck.push_back(väg[k - 1]);
                    ut.streck.push_back(väg[k]);
                }
            }
        }
        väg.clear();
        fylls = false;
    };
    const auto till = [&](float x, float y) {
        if (väg.empty())
            fel = true;   // L/H/V/Q utan ett M före
        else
            väg.push_back({x, y});
    };

    while (i < o.size() && !fel) {
        const std::string& k = o[i++];
        if (k[0] == '@') {
            avsluta();
            const Del* del = nullptr;
            for (const Del& d : kDelar)
                if (k.compare(1, std::string::npos, d.name) == 0)
                    del = &d;
            if (!del || djup > 0) {   // okänd del, eller en del i en del
                fel = true;
                break;
            }
            tolka(del->path, ut, djup + 1, fyllAlla || nästaFylls, a);
            nästaFylls = false;
            continue;
        }
        if (k.size() != 1) {
            fel = true;
            break;
        }
        switch (k[0]) {
        case 'F':
            avsluta();
            nästaFylls = true;
            break;
        case 'M': {
            avsluta();
            const float x = a.x(tal()), y = a.y(tal());
            väg   = {{x, y}};
            fylls      = fyllAlla || nästaFylls;
            nästaFylls = false;
            break;
        }
        case 'L': {
            const float x = a.x(tal()), y = a.y(tal());
            till(x, y);
            break;
        }
        case 'H':
            if (!väg.empty()) {
                const float x = a.x(tal());
                till(x, väg.back().y);
            } else {
                fel = true;
            }
            break;
        case 'V':
            if (!väg.empty()) {
                const float y = a.y(tal());
                till(väg.back().x, y);
            } else {
                fel = true;
            }
            break;
        case 'Q': {
            const float kx = a.x(tal()), ky = a.y(tal());
            const float x = a.x(tal()), y = a.y(tal());
            if (väg.empty()) {
                fel = true;
                break;
            }
            // Kurvan som korda: åtta bitar räcker i en ruta på 24.
            const gfx::Pointf a = väg.back();
            for (int s = 1; s <= 8; ++s) {
                const float t = static_cast<float>(s) / 8.0f, u = 1.0f - t;
                till(u * u * a.x + 2.0f * u * t * kx + t * t * x,
                     u * u * a.y + 2.0f * u * t * ky + t * t * y);
            }
            break;
        }
        case 'Z':
            if (!väg.empty())
                väg.push_back(väg.front());
            avsluta();
            break;
        case 'C': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal()), r = tal() * a.s;
            ut.bågar.push_back({cx, cy, r, r, 0.0f, 360.0f, true});
            break;
        }
        case 'A': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal()), r = tal() * a.s;
            const float a0 = tal(), a1 = tal();
            ut.bågar.push_back({cx, cy, r, r, a0, a1, false});
            break;
        }
        case 'E': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal());
            const float rx = tal() * a.s, ry = tal() * a.s;
            ut.bågar.push_back({cx, cy, rx, ry, 0.0f, 360.0f, true});
            break;
        }
        case 'U': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal());
            const float rx = tal() * a.s, ry = tal() * a.s;
            const float a0 = tal(), a1 = tal();
            ut.bågar.push_back({cx, cy, rx, ry, a0, a1, false});
            break;
        }
        case 'R':
        case 'B': {
            avsluta();
            const float x = a.x(tal()), y = a.y(tal());
            const float w = tal() * a.s, h = tal() * a.s, r = tal() * a.s;
            ut.rutor.push_back({x, y, w, h, r, k[0] == 'B'});
            break;
        }
        case 'D': {
            avsluta();
            const float cx = a.x(tal()), cy = a.y(tal()), r = tal() * a.s;
            ut.skivor.push_back({cx, cy, r});
            break;
        }
        case 'K': {
            avsluta();
            const float x = a.x(tal()), y = a.y(tal());
            const float w = tal() * a.s, h = tal() * a.s;
            if (fel || i >= o.size()) {
                fel = true;
                break;
            }
            stansa(o[i++], x, y, w, h, ut);
            break;
        }
        default:
            fel = true;
            break;
        }
    }
    avsluta();
    if (fel)
        gfx::log("stockIcon: ritbeskrivningen \"%s\" går inte att läsa vid ord %d "
                 "— resten ritas inte",
                 text, static_cast<int>(i));
}

// Avståndet från (px, py) till segmentet a–b, i kvadrat.
float avstånd2(float px, float py, gfx::Pointf a, gfx::Pointf b)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float l2 = dx * dx + dy * dy;
    float       t  = l2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0.0f;
    t              = std::clamp(t, 0.0f, 1.0f);
    const float ex = a.x + t * dx - px, ey = a.y + t * dy - py;
    return ex * ex + ey * ey;
}

// Ligger punkten (x, y) — rutans enheter — i någon av ritningens former?
// `h` är halva strecket.
bool inne(const Ritning& r, float x, float y, float h)
{
    const float h2 = h * h;
    for (std::size_t k = 0; k + 1 < r.streck.size(); k += 2)
        if (avstånd2(x, y, r.streck[k], r.streck[k + 1]) <= h2)
            return true;
    for (const Ritning::Skiva& s : r.skivor)
        if ((x - s.cx) * (x - s.cx) + (y - s.cy) * (y - s.cy) <= s.r * s.r)
            return true;
    for (const Ritning::Ruta& q : r.rutor) {
        // Den rundade rektangelns avståndsfält (negativt inne i den).
        const float rad = std::min({q.r, q.w / 2.0f, q.h / 2.0f});
        const float qx  = std::abs(x - (q.x + q.w / 2.0f)) - q.w / 2.0f + rad;
        const float qy  = std::abs(y - (q.y + q.h / 2.0f)) - q.h / 2.0f + rad;
        const float d   = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) +
                        std::min(std::max(qx, qy), 0.0f) - rad;
        if (q.fylld ? d <= 0.0f : std::abs(d) <= h)
            return true;
    }
    constexpr float kGrad = 3.14159265358979f / 180.0f;
    for (const Ritning::Båge& b : r.bågar) {
        const float nx = (x - b.cx) / b.rx, ny = (y - b.cy) / b.ry;
        const float q  = std::hypot(nx, ny);
        if (q <= 0.0f)
            continue;
        // Avståndet till ellipsen i första ordningen: (|n| − 1) / |∇|n||.
        const float grad = std::hypot(nx / b.rx, ny / b.ry) / q;
        const float d    = std::abs(q - 1.0f) / grad;
        if (b.hel) {
            if (d <= h)
                return true;
            continue;
        }
        float vinkel = std::atan2(ny, nx) / kGrad;   // −180 … 180
        if (vinkel < 0.0f)
            vinkel += 360.0f;
        const float start = std::fmod(b.a0, 360.0f) < 0.0f
                                ? std::fmod(b.a0, 360.0f) + 360.0f
                                : std::fmod(b.a0, 360.0f);
        const float rel = std::fmod(vinkel - start + 720.0f, 360.0f);
        if (rel <= b.a1 - b.a0 && d <= h)
            return true;
        // De runda ändarna.
        for (const float a : {b.a0, b.a1}) {
            const gfx::Pointf p{b.cx + b.rx * std::cos(a * kGrad),
                                b.cy + b.ry * std::sin(a * kGrad)};
            if ((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) <= h2)
                return true;
        }
    }
    for (const std::vector<gfx::Pointf>& yta : r.ytor) {
        bool udda = false;   // jämn-udda-regeln
        for (std::size_t a = 0, b = yta.size() - 1; a < yta.size(); b = a++) {
            const gfx::Pointf p = yta[a], q = yta[b];
            if ((p.y > y) != (q.y > y) &&
                x < (q.x - p.x) * (y - p.y) / (q.y - p.y) + p.x)
                udda = !udda;
        }
        if (udda)
            return true;
    }
    return false;
}

// Ligger punkten i en UTSTANSAD text? Varje par har sitt eget streck.
bool iHål(const Ritning& r, float x, float y)
{
    for (std::size_t k = 0; k + 1 < r.hål.size(); k += 2) {
        const float h = r.hålH[k / 2];
        if (avstånd2(x, y, r.hål[k], r.hål[k + 1]) <= h * h)
            return true;
    }
    return false;
}

// FÄRGROLLERNA — konturen och färgvariantens fem (`kFärger`).
enum class Roll : std::uint8_t { Text, Blue, Green, Red, Amber, Yellow };

bool rollAv(const std::string& ord, Roll& ut)
{
    static constexpr std::pair<const char*, Roll> kRoller[] = {
        {"blue", Roll::Blue},   {"green", Roll::Green},   {"red", Roll::Red},
        {"amber", Roll::Amber}, {"yellow", Roll::Yellow},
    };
    for (const auto& [namn, roll] : kRoller)
        if (ord == namn) {
            ut = roll;
            return true;
        }
    return false;
}

gfx::Color färgAv(Roll roll, const Theme::Colors& p)
{
    switch (roll) {
    case Roll::Blue:   return p.stockBlue;
    case Roll::Green:  return p.stockGreen;
    case Roll::Red:    return p.stockRed;
    case Roll::Amber:  return p.stockAmber;
    case Roll::Yellow: return p.stockYellow;
    case Roll::Text:   break;
    }
    return p.text;
}

// Ett LAGER: formerna i en färgroll, bakom eller ovanpå konturen.
struct Lager {
    Roll    roll  = Roll::Text;
    bool    över  = false;
    bool    stark = false;   // `strong`: tonen blandad mot texten
    Ritning former;
};

// Färgbeskrivningen i lager (`kFärger`s språk): ett färgord eller
// `over`/`under` börjar ett nytt lager, och `all <färg>` färgar konturen.
// `strong` före ett färgord gör det lagrets ton STARK (`kStark`).
void tolkaFärg(const char* text, std::vector<Lager>& ut, Roll& kontur)
{
    const std::vector<std::string> o = ord(text);
    Roll        roll = Roll::Text;
    bool        över = false, stark = false, nästaStark = false;
    std::string del;
    const auto  spara = [&] {
        if (del.empty())
            return;
        Lager l;
        l.roll  = roll;
        l.över  = över;
        l.stark = stark;
        tolka(del.c_str(), l.former, 0, false);
        ut.push_back(std::move(l));
        del.clear();
    };
    for (std::size_t i = 0; i < o.size(); ++i) {
        Roll r = Roll::Text;
        if (o[i] == "all" && i + 1 < o.size() && rollAv(o[i + 1], kontur)) {
            ++i;
        } else if (rollAv(o[i], r)) {
            spara();
            roll       = r;
            stark      = nästaStark;
            nästaStark = false;
        } else if (o[i] == "strong") {
            spara();
            nästaStark = true;
        } else if (o[i] == "over" || o[i] == "under") {
            spara();
            över = o[i] == "over";
        } else {
            del += o[i];
            del += ' ';
        }
    }
    spara();
}

const char* färgbeskrivning(StockIcon icon)
{
    for (const Färg& f : kFärger)
        if (f.icon == icon)
            return f.path;
    return nullptr;
}

Bitmap rastrera(StockIcon icon, int px, StockIconStyle style,
                const Theme::Colors& p)
{
    Lager kontur;
    tolka(kDefs[static_cast<int>(icon)].path, kontur.former, 0, false);
    std::vector<Lager> färg;
    if (style == StockIconStyle::Color)
        if (const char* f = färgbeskrivning(icon))
            tolkaFärg(f, färg, kontur.roll);

    // Ordningen ett prov frågar lagren i, det ÖVERSTA först: färgens `over`
    // (det senast skrivna överst), konturen, färgens `under`. Det första
    // lager provet ligger i ger dess färg — i mono bara konturen.
    std::vector<const Lager*> ordning;
    for (auto it = färg.rbegin(); it != färg.rend(); ++it)
        if (it->över)
            ordning.push_back(&*it);
    ordning.push_back(&kontur);
    for (auto it = färg.rbegin(); it != färg.rend(); ++it)
        if (!it->över)
            ordning.push_back(&*it);
    std::vector<gfx::Color> färger;
    for (const Lager* l : ordning)
        färger.push_back(l->stark ? color::mix(färgAv(l->roll, p), p.text, kStark)
                                  : färgAv(l->roll, p));

    // Proven i `kProv` gånger målets upplösning, ned till målet med
    // programikonens skalare (ytans medelvärde i förmultiplicerad rymd, så
    // två färger möts utan en mörk söm).
    const int       n = px * kProv;
    glzassets::Rgba stor;
    stor.width  = n;
    stor.height = n;
    stor.px.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) * 4);
    const float   enhet = kRuta / static_cast<float>(n);
    const float   h     = kStreck / 2.0f;
    std::uint8_t* ut    = stor.px.data();
    for (int y = 0; y < n; ++y) {
        const float uy = (static_cast<float>(y) + 0.5f) * enhet;
        for (int x = 0; x < n; ++x, ut += 4) {
            const float ux = (static_cast<float>(x) + 0.5f) * enhet;
            if (iHål(kontur.former, ux, uy))
                continue;   // utstansat: genomskinligt i alla lager
            for (std::size_t k = 0; k < ordning.size(); ++k)
                if (inne(ordning[k]->former, ux, uy, h)) {
                    ut[0] = färger[k].r;
                    ut[1] = färger[k].g;
                    ut[2] = färger[k].b;
                    ut[3] = färger[k].a;
                    break;
                }
        }
    }
    const glzassets::Rgba liten = glzassets::resizeRgba(stor, px, px);
    return Bitmap(px, px, liten.px.data());
}

std::uint32_t packa(gfx::Color c)
{
    return (std::uint32_t{c.r} << 24) | (std::uint32_t{c.g} << 16) |
           (std::uint32_t{c.b} << 8) | std::uint32_t{c.a};
}

} // namespace

ImageSource stockIcon(StockIcon icon, int sizeClass, StockIconStyle style)
{
    if (sizeClass <= 0) {
        gfx::log("stockIcon: storleksklassen %d är inte positiv — 16 används",
                 sizeClass);
        sizeClass = 16;
    }
    return ImageSource(icon, sizeClass, style);
}

int stockIconCount()
{
    return kAntal;
}

std::string_view stockIconName(StockIcon icon)
{
    const int i = static_cast<int>(icon);
    return i >= 0 && i < kAntal ? std::string_view(kDefs[i].name)
                                : std::string_view();
}

std::optional<StockIcon> stockIconByName(std::string_view name)
{
    for (const Def& d : kDefs)
        if (name == d.name)
            return d.icon;
    return std::nullopt;
}

namespace detail {

Bitmap stockBitmap(StockIcon icon, int px, StockIconStyle style,
                   const Theme::Colors& colors)
{
    const int i = static_cast<int>(icon);
    if (i < 0 || i >= kAntal || px <= 0)
        return Bitmap{};
    px = std::min(px, kMaxPx);
    // EN bild per (ikon, px, stil, färgerna): fönstrens texturer delar den,
    // och en ny färg (ett temabyte) är en ny bild. Mono läser bara texten.
    const bool färg = style == StockIconStyle::Color;
    using Nyckel    = std::tuple<int, int, bool, std::array<std::uint32_t, 6>>;
    static std::map<Nyckel, Bitmap> cache;
    const Nyckel nyckel{
        i, px, färg,
        {packa(colors.text), färg ? packa(colors.stockBlue) : 0u,
         färg ? packa(colors.stockGreen) : 0u, färg ? packa(colors.stockRed) : 0u,
         färg ? packa(colors.stockAmber) : 0u, färg ? packa(colors.stockYellow) : 0u}};
    auto it = cache.find(nyckel);
    if (it == cache.end())
        it = cache.emplace(nyckel, rastrera(icon, px, style, colors)).first;
    return it->second;
}

} // namespace detail
} // namespace glaze
