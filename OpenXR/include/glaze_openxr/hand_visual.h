#pragma once

// HÄNDERNA RITADE — en valfri byggsten som visar de spårade händerna
// (`FrameInfo::hands`) som fingrar och handflata: en linje per fingerled med
// ledernas radier som tjocklek, i ett eget lager i ritytan som HandVisual
// äger helt. Linjer träffas aldrig av strålarna (`castRay`), så händerna blir
// aldrig ett mål för handens egen pekare.
//
// TRÅDARNA: UI-tråden; `update` en gång per bildruta från sessionens `onFrame`.

#include <glaze/glaze.h>
#include <glaze_openxr/session.h>

namespace glaze::openxr {

struct HandVisualStyle {
    gfx::Color color{190, 200, 215};
    double     radiusScale = 1.0;   // linjernas tjocklek i ledernas radier
};

class HandVisual final {
public:
    HandVisual(glaze::Canvas& canvas, int layer);
    ~HandVisual();

    HandVisual(const HandVisual&)            = delete;
    HandVisual& operator=(const HandVisual&) = delete;

    void setVisible(bool visible);
    bool visible() const { return visible_; }
    void setStyle(const HandVisualStyle& style);

    // Händerna ur bildrutan; en hand som inte spåras ritas inte.
    void update(const FrameInfo& frame);

private:
    glaze::Canvas&  canvas_;
    int             layer_;
    bool            visible_ = true;
    bool            drawn_   = false;   // lagret har former
    HandVisualStyle style_;
};

} // namespace glaze::openxr
