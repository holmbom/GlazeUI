#pragma once
#include <cstdint>
#include <vector>

// RITYTANS GEOMETRI OCH ENHETER — de minsta
// typerna ritytans scen byggs av. Världens koordinater är DOUBLE hela vägen
// till chunkens origo: en SWEREF-koordinat (~6,6·10⁶ m) får inte avrundas
// till float förrän den blivit relativ en närliggande punkt.
//
//     glaze::Point p{674000.0, 6580000.0};            // en punkt i världen
//     glaze::Rect  r{674000.0, 6579950.0, 256, 256};  // x, y, bredd, höjd
//     glaze::Stroke s{.width = glaze::mm(0.7)};       // 0,7 mm på skärmen
//
// ENHETERNA:
//   Px     LOGISKA px — följer OS-skalan och användarskalan (`uiScale`), som
//          allt annat i GlazeUI. Ett naket tal är px.
//   Mm     MILLIMETER på skärmen, med linjal — ur skärmens EDID och appens
//          kalibrering (`AppWindow::display()`), OBEROENDE av OS- och
//          användarskalan.
//   World  VÄRLDSENHETER — måttet växer och krymper med zoomen (en linje som
//          är 3 m bred i terrängen). Linjer extruderas då i planet före
//          projektionen; text och markörer står upprätta i mittens skala.

namespace glaze {

// En punkt i världen (eller i lokala logiska px — ritytans rymd utan kamera).
struct Point {
    double x = 0.0;
    double y = 0.0;
};

// En rektangel i världen: hörnet (x, y) och utsträckningen (w, h) längs
// axlarna. Vilket hörn som är "överst" beror på kamerans y-riktning
// (`Canvas::setYUp`) — en bild ritas alltid rättvänd (se `Shape::image`).
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

// KLIPPETS RYMD (`Canvas::setClipSpace`, `setLayerClipSpace`):
//   World   världens XY, LODRÄTT genom scenen — ett PRISMA: i 2D en yta på
//           kartan som följer kameran, i 3D klipps terrängen, meshar och
//           stående text där området slutar, oavsett tilt och höjd (stående
//           text och markörer på sitt ankare: hela etiketten eller ingen).
//   Screen  ritytans LOKALA LOGISKA px, (0, 0) i övre vänstra hörnet och y
//           nedåt — en yta på skärmen som ingen kamera flyttar.
enum class ClipSpace : std::uint8_t { World, Screen };

// En POLYGON: ytterringen och dess HÅL. Samma geometri för en FORM i flera
// delar (`Shape::polygons(std::vector<Polygon>, …)`, i världen; delarna får
// inte överlappa) och för KLIPPET (i klippets rymd; flera ytor i en samling
// är deras UNION och får överlappa). Orienteringen spelar ingen roll och en
// upprepad slutpunkt tas bort. Ingen gräns för antalet hörn.
// En OGILTIG KLIPPYTA är en yta vars ytterring eller hål korsar sig själv, vars hål går
// utanför ytterringen eller korsar ett annat hål, vars ytterring har färre än
// tre olika punkter (eller alla på en linje), eller som har en punkt som inte
// är ändlig — vad som då händer väljer `InvalidClip`. Ett HÅL utan yta (färre
// än tre punkter, alla på en linje) bortses från.
struct Polygon {
    std::vector<Point>              outer;
    std::vector<std::vector<Point>> holes;
};

// EN OGILTIG KLIPPYTA (`Canvas::setInvalidClip`):
//   Show  förvalet — klippet den ingår i (ritytans, lagrets eller målarens
//         `save`) GÄLLER INTE: allt det hade klippt syns.
//   Hide  den ogiltiga ytan täcker ingenting: det som bara den hade släppt
//         igenom klipps bort, och utan någon giltig yta klipps allt bort.
// Båda loggar ytan (`gfx::log`).
enum class InvalidClip : std::uint8_t { Show, Hide };

enum class Unit : std::uint8_t { Px, Mm, World };

// Ett MÅTT med sin enhet. Ett naket tal är px (konverteringen är implicit
// med flit, så `.padding = 4` och `.width = 2` läser som man menar);
// millimeter och världsenheter skrivs ut: `glaze::mm(2.5)`, `glaze::world(3)`.
struct Length {
    float value = 0.0f;
    Unit  unit  = Unit::Px;

    constexpr Length() = default;
    constexpr Length(float v) : value(v) {}   // NOLINT — implicit = px
    constexpr Length(float v, Unit u) : value(v), unit(u) {}
};

constexpr Length px(float v) { return Length{v, Unit::Px}; }
constexpr Length mm(float v) { return Length{v, Unit::Mm}; }
constexpr Length world(float v) { return Length{v, Unit::World}; }

// 3D: världen är HÖGERHÄNT: x österut, y norrut, z UPP i
// kartvärlden (en app med Y upp väljer `UpAxis::Y` på kameran). Punkter i
// världen är double — precisionen är ramverkets (RTC) och appen anger alltid
// världskoordinater; `Float3` är lokala data relativt ett origo (meshar,
// punktmoln). Egna namn med suffixet 3: `{x, y}` initierar också en Point3,
// så en överlagring på Point hade gjort befintlig kod tvetydig.
struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// En axelparallell låda i världen.
struct Box3 {
    Point3 min{};
    Point3 max{};
};

// Ett plan genom `point` med normalen `normal` (behöver inte vara enhetlig).
struct Plane3 {
    Point3 point{};
    Vec3   normal{0.0, 0.0, 1.0};
};

// En stråle från `origin` längs `direction` (behöver inte vara enhetlig).
struct Ray3 {
    Point3 origin{};
    Vec3   direction{};
};

} // namespace glaze
