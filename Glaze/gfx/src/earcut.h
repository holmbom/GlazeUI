#pragma once
// Intern header (src/, INTE include/) — ytornas triangulering.
//
// EGEN implementation av öronklippningen ("earcut"-metoden: hålen bryggas in
// i ytterringen, öronen klipps ur en dubbellänkad ring, z-ordning gallrar
// testet i stora ringar, och två reservsteg tar det som fastnar). Ingen
// tredjepartskod i trädet. Ren std, ingen backend — körs
// EN gång när en yta läggs till (`buildPolygon`, geom2d.cpp).

#include <cstdint>
#include <span>
#include <vector>

#include "gfx/geom2d.h"

namespace gfx {

// Triangulerar ringarna i `points`: ring k är punkterna
// [ringStarts[k], ringStarts[k + 1]) (den sista till listans slut), ring 0 är
// YTTERRINGEN och resten HÅL. Orienteringen spelar ingen roll — den vänds
// internt. Punkterna förväntas rensade (ändliga, ingen punkt lika med sin
// föregångare, ingen upprepad slutpunkt); en ring med färre än tre punkter
// hoppas över.
//
// Trianglarna LÄGGS TILL i `out` som index-tripplar i den platta listan
// (orienteringen är inte lovad — ingenting i scenen gallrar baksidor). Svaret
// är false när ringarna inte gick att triangulera som en enkel yta med hål:
// en självkorsning eller ett hål utanför ytterringen. Trianglarna täcker då
// det användaren ritade så gott det går — det som fastnade i klippningen
// stängs med en solfjäder, så ingenting av ytan uteblir — men formen är en
// approximation.
bool triangulate(std::span<const Point2d>       points,
                 std::span<const std::uint32_t> ringStarts,
                 std::vector<std::uint32_t>&    out);

} // namespace gfx
