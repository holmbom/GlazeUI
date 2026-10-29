#pragma once

#include <cstdint>

#include "glaze/canvas/geometry.h"

// glaze::Camera — RITYTANS KAMERA. Panorering, zoom,
// vridning och tilt är BARA kameran: innehållet ligger kvar i GPU:n och ritas
// om med en ny matris, aldrig med ny geometri.
//
//     karta.setCamera({.center = {674100, 6580050}, .zoom = 2.5,
//                      .bearing = 15, .pitch = 30});
//
// MÅTTEN ÄR GLAZES: zoomen i LOGISKA px per världsenhet (fönstrets skala
// läggs på av ritytan), vinklarna i GRADER (hela API:t talar grader).
// Ritytan räknar om till gfx-fasadens `MapView`, som talar fysiska px.
//
// UTAN KAMERA är världen ritytans lokala logiska px — samma rymd som `Draw`:
// (0, 0) i övre vänstra hörnet, y nedåt, zoom 1 (`Canvas::hasCamera()`).

namespace glaze {

struct Camera {
    Point  center{};          // världspunkten mitt i ytan
    double zoom    = 1.0;     // logiska px per världsenhet (> 0)
    float  bearing = 0.0f;    // grader; innehållet vrids MOTURS (MapLibre)
    float  pitch   = 0.0f;    // grader; 0 = rakt uppifrån, > 0 = perspektiv
};

// glaze::Camera3D — RITYTANS FRIA KAMERA. Kartkameran ovan
// står kvar med sina gester; den här behövs för horisonten, förstapersonen
// och orbiten.
//
//     scen.setCamera(glaze::Camera3D{.target = {674100, 6580050, 12},
//                                    .distance = 80, .azimuth = 30,
//                                    .elevation = 25});
//     auto gå = glaze::Camera3D::lookFrom({674100, 6580000, 2}, 45, -5);
//
// ORBITPARAMETRISERAD — ett mål, ett avstånd och två vinklar — och EN form:
// förstapersonen går genom `lookFrom`, som lägger målet en enhet framför
// ögat. Ett ögonbaserat fält bredvid hade gett två sanningar. Vinklarna i
// GRADER, längderna i världsenheter. KONVENTIONEN (grindad i
// scene_math_gate):
//   azimuth    KOMPASSEN — riktningen kameran TITTAR: 0 = norr, 90 = öster
//              (medurs), som kartans bäring. Vid 0 står ögat SÖDER om målet.
//   elevation  hur högt över målets horisont ögat står — blicken lutar lika
//              mycket nedåt: 0 = vågrätt, 90 = rakt uppifrån, negativt =
//              underifrån. Upp-vektorn följer elevationens TANGENT, så 90°
//              och förbi är kontinuerligt (ingen gimbal lock): rakt uppifrån
//              ligger blickens kompassriktning uppåt i bilden, och orbitens
//              tumling (`Canvas::setOrbitTumble`) passerar polen.
//   roll       grader kring blicken; positivt vrider KAMERAN medurs och
//              bilden moturs, som bäringen.
//   fov        vertikalt synfält i perspektiv.
//   orthoHeight halva synliga höjden ortografiskt; 0 = MATCHAD mot
//              perspektivet vid målet (distance · tan(fov/2)), så bytet mellan
//              projektionerna inte ändrar skalan där.
//   nearPlane, farPlane  klipplanen längs blicken; 0 = ur scenens låda.
//   up         Z: x öster, y norr, z upp — kartans värld. Y: x öster, y upp
//              och −z norr — golvet är XZ, en modellerares värld. Högerhänt i
//              båda. Ritytans 2D-former (`{x, y}`) ligger i XY-planet med
//              båda, med Y upp alltså på högkant.
enum class Projection : std::uint8_t { Perspective, Orthographic };
enum class UpAxis : std::uint8_t { Z, Y };

struct Camera3D {
    Point3     target{};
    double     distance    = 10.0;
    float      azimuth     = 0.0f;
    float      elevation   = 30.0f;
    float      roll        = 0.0f;
    Projection projection  = Projection::Perspective;
    float      fov         = 45.0f;
    double     orthoHeight = 0.0;
    double     nearPlane   = 0.0;
    double     farPlane    = 0.0;
    UpAxis     up          = UpAxis::Z;

    // FÖRSTAPERSONEN: ögat i `eye`, blicken mot kompassriktningen `yaw` (0 =
    // norr, medurs — som azimuten) och `pitch` grader UPPÅT från horisonten
    // (negativt = nedåt; elevationen blir −pitch). Målet hamnar en enhet
    // framför ögat.
    static Camera3D lookFrom(Point3 eye, float yaw, float pitch,
                             float fov = 45.0f, UpAxis up = UpAxis::Z);

    // Ögat: målet minus blicken × avståndet, i double.
    Point3 eye() const;
};

} // namespace glaze
