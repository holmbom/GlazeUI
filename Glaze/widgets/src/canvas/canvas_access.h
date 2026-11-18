#pragma once
// INTERN — ritytans scen för ramverket och grindarna (mesh_data.h
// `MeshAccess`s form): scenen är ritytans privata data, och den som mäter
// ögonblicksbilderna (scene.h `publishScene`) når den här.

#include <memory>

#include "glaze/widgets/canvas.h"
#include "scene.h"

namespace glaze::detail {

struct CanvasAccess {
    static const std::shared_ptr<SceneData>& scene(Canvas& c) { return c.scene_; }
};

} // namespace glaze::detail
