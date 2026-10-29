#pragma once
#include <glaze/glaze.h>

#include <memory>
#include <string>

// StandInWidget — verktygens ställföreträdare för en OKÄND widget-typ:
// en klass REGISTRET inte känner (en egen widget i ett verktygsbygge som inte
// länkar den — den buntade glaze-render i VS Code). Ritas som en ruta i rätt
// storlek med klassnamnet centrerat; placeringen (bas-fälten
// x/y/right/bottom/width/height/anchors/dock) är redigerbar, allt annat
// bevaras orört som opaka rader.
//
// MÖNSTER: EGEN widget (fallback-vägen, hård regel 3) — ärver Widget och
// överrider draw() själv, ritar via c.r med AKTIVA temats färger
// (c.theme->colors) och text via c.font (regel 2). Bor i GlazeBuilder-
// appen, INTE i libglaze, och registreras aldrig i widget-registret.
class StandInWidget : public glaze::Widget {
public:
    explicit StandInWidget(std::string typeName)
        : typeName_(std::move(typeName))
    {
    }

    const std::string& typeName() const { return typeName_; }

protected:
    gfx::Sizef contentSize(const glaze::Ctx& c) const override;
    void       draw(const glaze::Ctx& c) override;

private:
    std::string typeName_;
};

namespace bld {

// EN SKAPELSEVÄG för en modellnod: REGISTRET först
// — `glaze::makeWidgetOfClass` med nodens C++-typ, alltså ramverkets typer OCH
// appens egna (`GLAZE_REGISTER_WIDGET`) i ett verktygsbygge som länkar dem —
// och en `StandInWidget` bara när ingen registrerad klass bär namnet. Paletten
// (palette.h) är metadata för verktygslådan och frågas inte här. `standIn`
// (valfri) svarar vilket det blev. Delad av
// glaze-renders och pixelgrindens trädbygge (window_tree), builderns
// `rebuildPreview` och preview_gate.
std::unique_ptr<glaze::Widget> makeNodeWidget(const std::string& cppType,
                                              bool* standIn = nullptr);

} // namespace bld
