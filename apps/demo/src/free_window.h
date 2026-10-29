#pragma once
#include <glaze/glaze.h>

#include "demo_text.h"

// Genomskinligt fönster — VISION-exemplet: en rund platta med text som
// sticker ut utanför formen. setTransparent(true) före show() är allt som
// krävs: bakgrunden blir per-pixel-genomskinlig, chromet tvingas av och
// (på Windows) släpps klick i genomskinliga pixlar igenom till fönstret
// under. Formen ritas i frame(); widgets fungerar som vanligt ovanpå.
class FreeWindow : public glaze::AppWindow {
public:
    FreeWindow();

private:
    void frame(const glaze::Ctx& c) override;
};
