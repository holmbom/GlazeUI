#pragma once
#include "glaze/draw.h"
#include "glaze/widget.h"

// INTERN header (prejudikatet är `status_badge.h` och `prop_parse.h` i samma
// mapp) — SKIKTET: delarens mekanik, delad av `Splitter` och
// `DockHost`.
//
// Två widgets drar i skikt. Delaren har ETT, mellan sina två rutor;
// dockningsytan har ett per dockad panel, mellan panelen och resten. Det är
// samma sak sedd två gånger — samma ritning, samma pekare, samma räkning
// från pekare till rutstorlek — och den skrivs därför EN gång, här, i
// stället för en gång i varje widget (där de två hade kunnat glida isär i
// just de detaljer man ser: tjockleken, greppet, pekaren).
//
// Vad som INTE bor här, med skäl: VILKA skikt som finns och VAR de ligger.
// Det är respektive widgets layout (`Splitter::layout`,
// `DockHost::strips`), och den är olika i grunden — två rutor med en fast
// mot N remsor i en ordning. Skiktet får sin rekt därifrån och vet inget om
// rutorna runt det.
//
// Den är INTE publik: en app har inget ärende hit, och en EGEN widget som
// vill ha ett skikt komponerar en `Splitter`.

namespace glaze::sash {

// SKIKTETS RITNING — med `switch (c.ui)` inuti, så den per-UI-form som
// annars hade stått i två widgets draw() står på ett ställe. `r` är
// skiktets yta i widget-LOKALA LOGISKA px (Draw-rymden), `vertical` att
// skiktet är STÅENDE (rutorna ligger bredvid varandra) och `hot` att
// pekaren står över det eller att ett drag pågår — anroparen avgör det,
// och ett inaktiverat skikt är aldrig hett.
//
// INGET INAKTIVT UTSEENDE, och det är ett beslut: skiktet är en del av en
// YTA, och en yta har inget läge (bar.cpp, group_box.cpp). Att det inte går
// att dra syns i att det inte tänds och att pekaren förblir en pil.
void draw(const Ctx& c, const gfx::Rectf& r, bool vertical, bool hot);

// PEKAREN över ett skikt: ↔ för ett stående (det dras i sidled), ↕ för ett
// liggande.
gfx::PointerShape cursor(bool vertical);

// RUTANS NYA STORLEK när skiktet dras — i LOGISKA px, OKLAMPAD.
//
//   p      pekaren längs axeln (fysiska px, mus-krokarnas rymd)
//   edge   rutans YTTRE kant längs axeln (fysisk) — den som står still
//   before rutan ligger FÖRE skiktet (vänster om / ovanför det)
//   grip   var i skiktet trycket tog, fysiska px från skiktets början — så
//          skiktet följer handen och inte hoppar när draget börjar
//   thick  skiktets tjocklek i FYSISKA px
//
// Klampningen är anroparens (`clampPane`), för minimimåtten är dess.
float paneFromPointer(float p, float edge, bool before, float grip,
                      float thick, float scale);

// KLAMPNINGEN, en sanning för layouten och draget: rutan får aldrig ta så
// mycket att den ANDRA rutan hamnar under sitt minimum, och aldrig bli
// mindre än sitt EGET — går inte båda att uppfylla vinner det egna (den
// rutan är den man drar i, eller den som är fast). Allt klampas till
// [0, avail]. Logiska px.
float clampPane(float want, float ownMin, float otherMin, float avail);

} // namespace glaze::sash
