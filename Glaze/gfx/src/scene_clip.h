#pragma once
// Intern header (src/, INTE include/) — MÅLARENS KLIPP i backendernas
// klippblock (gfx::SceneClip, gfx/scene.h): packningen och bindningens två
// regler, EN gång för D3D11, Metal och Linux. Ren std, inget tillstånd.

#include <cstring>

#include "gfx/image.h"
#include "gfx/scene.h"

namespace gfx {

// BLOCKET programmen läser — HLSL `cbuffer GlazeClip` (b5), MSL
// `GlazeClipBlock` (fragmentbuffert 4) och GLSL `uniform GlazeClip`
// (bindningspunkt 5): tre rader om fyra, 48 byte.
//   rect   skärmmaskens ruta i målets px: x, y, w, h (en negativ storlek
//          speglar masken längs axeln)
//   world  världsmaskens uv-affin: skala xy, förskjutning xy
//   meta   .x 1 = skärmmask, .y 1 = världsmask, .z/.w 1 = masken är
//          y-speglad (`Image::flippedV` — GL:s render-mål; D3D11 och Metal
//          läser dem inte och svarar 0)
struct SceneClipBlock {
    float rect[4];
    float world[4];
    float meta[4];
};
static_assert(sizeof(SceneClipBlock) == 48,
              "SceneClipBlock är tre float4 — samma layout som programmens block");

inline SceneClipBlock packSceneClip(const SceneClip& k)
{
    SceneClipBlock b{};
    b.rect[0] = k.screenRect.x;
    b.rect[1] = k.screenRect.y;
    b.rect[2] = k.screenRect.w;
    b.rect[3] = k.screenRect.h;
    std::memcpy(b.world, k.worldUv, sizeof b.world);
    b.meta[0] = k.screen ? 1.0f : 0.0f;
    b.meta[1] = k.world ? 1.0f : 0.0f;
    b.meta[2] = k.screen && k.screen->flippedV() ? 1.0f : 0.0f;
    b.meta[3] = k.world && k.world->flippedV() ? 1.0f : 0.0f;
    return b;
}

// OMBINDNINGEN: maskernas platser (skärm, värld) binds om vid ett anrop som
// har en mask, och vid ett utan när det som står bundet bär en — en bild kan
// ha bytt textur sedan sist, och ett mål som binds kan nolla platserna. Annars
// står platserna kvar. Blocket skrivs bara när anropets klipp skiljer från det
// bundna (`operator==`).
//
// INGEN MASK: platsen får backendens VITA 1 × 1-textur — aldrig en obunden
// plats (Metal och GL validerar en obunden textur som programmet deklarerar).
// Programmen läser den aldrig: `meta` säger att masken saknas.
inline bool rebindClipMasks(const SceneClip& anrop, const SceneClip& bundet)
{
    return anrop.screen || anrop.world || bundet.screen || bundet.world;
}

} // namespace gfx
