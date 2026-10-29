#pragma once
#include <algorithm>
#include <optional>
#include <vector>

#include <gfx/geometry.h>
#include <gfx/platform.h>   // gfx::ScreenInfo

#include "glaze/app_window.h"   // StartPosition

// INTERN header — ingår inte i publika API:t.
//
// FÖNSTRETS PLATS — startplatsen och
// omräkningen mellan LOGISKA px och placeringsrymden, räknade i glaze ur
// `gfx::screens()`. EN implementation, ren logik utan ett enda gfx-anrop, så
// den grindas headless mot handräknade värden (tests/geometry_gate.cpp)
// — i stället för centrering och klämning i fyra backends.
//
// RYMDERNA:
//   * PLACERINGSRYMDEN är `WindowDesc::posX`/`setPosition`s — Win32 fysiska
//     px, AppKit punkter, X11 rotens px.
//   * LOGISKA px, det appen talar (`setLocation`, `geometry()`), är
//     placeringsrymden delad med PRIMÄRSKÄRMENS skala, LINJÄRT över alla
//     skärmar. Kontinuerligt över skärmgränser (Qt:s logiskt-per-skärm ger
//     hopp där), exakt när skalan är ett heltal och ±1 px annars.
//   * Fönstrets STORLEK i placeringsenheter är däremot dess logiska storlek
//     × MÅLSKÄRMENS skala — det är där det står.

namespace glaze::detail {

inline gfx::Pointf toPlacement(gfx::Pointf logical, float primaryScale)
{
    return gfx::Pointf{logical.x * primaryScale, logical.y * primaryScale};
}

inline gfx::Pointf toLogical(gfx::Pointf placement, float primaryScale)
{
    if (!(primaryScale > 0.0f))
        return placement;
    return gfx::Pointf{placement.x / primaryScale, placement.y / primaryScale};
}

// Primärskärmen — den markerade, annars den första. nullptr för en tom lista.
inline const gfx::ScreenInfo* primaryOf(const std::vector<gfx::ScreenInfo>& s)
{
    for (const gfx::ScreenInfo& sc : s)
        if (sc.primary)
            return &sc;
    return s.empty() ? nullptr : &s.front();
}

// Skärmen som INNEHÅLLER punkten (hela skärmen, inte arbetsytan), annars den
// NÄRMASTE — avståndet från punkten till skärmens rektangel. Så hamnar en
// sparad position från en skärm som kopplats ur på den skärm som ligger
// närmast där den stod. nullptr för en tom lista.
inline const gfx::ScreenInfo* screenFor(const std::vector<gfx::ScreenInfo>& s,
                                        gfx::Pointf p)
{
    const gfx::ScreenInfo* bäst  = nullptr;
    float                  minst = 0.0f;
    for (const gfx::ScreenInfo& sc : s) {
        if (sc.bounds.contains(p.x, p.y))
            return &sc;
        const gfx::Rectf& b  = sc.bounds;
        const float       dx = std::max({b.x - p.x, 0.0f, p.x - (b.x + b.w)});
        const float       dy = std::max({b.y - p.y, 0.0f, p.y - (b.y + b.h)});
        const float       d  = dx * dx + dy * dy;
        if (!bäst || d < minst) {
            bäst  = &sc;
            minst = d;
        }
    }
    return bäst;
}

// KLÄMNINGEN: övre vänstra hörnet så att fönstret står HELT inne i
// skärmens ARBETSYTA (inte under aktivitetsfältet eller menyraden). Per axel:
// är fönstret större än arbetsytan läggs det mot arbetsytans början — titeln
// och knapparna sitter där.
inline gfx::Pointf clampToWork(gfx::Pointf topLeft, gfx::Sizef physSize,
                               const gfx::ScreenInfo& sc)
{
    const gfx::Rectf& w   = sc.work;
    auto              axel = [](float v, float start, float längd, float mått) {
        if (mått >= längd)
            return start;
        return std::clamp(v, start, start + längd - mått);
    };
    return gfx::Pointf{axel(topLeft.x, w.x, w.w, physSize.w),
                       axel(topLeft.y, w.y, w.h, physSize.h)};
}

// En startbegäran — `AppWindow::showOwned` fyller den.
struct StartRequest {
    StartPosition pos = StartPosition::Default;
    gfx::Pointf   logical{};       // Manual: `setLocation`s position
    gfx::Sizef    logicalSize{};   // fönstrets klientyta, logiska px
    // Ägarens AKTUELLA klientyta i placeringsrymden — bara när ägaren
    // är öppen, synlig och inte minimerad. Inte `placement()`: det är
    // NORMALrutan, och för en maximerad ägare hade dialogen hamnat där ägaren
    // inte står.
    std::optional<gfx::Rectf> ownerRect;
};

// STARTPLATSEN i placeringsrymden — fönstrets övre vänstra hörn — eller
// nullopt, och då väljer OS:et: `Default`, och en tom skärmlista (Wayland,
// före init). Reglerna:
//   * Manual: positionen omräknad med primärskärmens skala, på skärmen som
//     innehåller den (annars den närmaste) och klämd in i dess arbetsyta.
//   * CenterOwner: mitt över ägarens klientyta, på skärmen som innehåller
//     ägarens mitt; utan ägaryta som CenterScreen.
//   * CenterScreen: mitt i arbetsytan på skärmen under ägarens mitt, utan
//     ägaryta primärskärmens.
// Storleken är logisk × målskärmens skala, och resultatet klämt in i
// målskärmens arbetsyta.
inline std::optional<gfx::Pointf>
startPlacement(const StartRequest& r, const std::vector<gfx::ScreenInfo>& screens)
{
    if (screens.empty() || r.pos == StartPosition::Default)
        return std::nullopt;
    const gfx::ScreenInfo* primär = primaryOf(screens);

    std::optional<gfx::Pointf> ägarMitt;
    if (r.ownerRect)
        ägarMitt = gfx::Pointf{r.ownerRect->x + r.ownerRect->w * 0.5f,
                               r.ownerRect->y + r.ownerRect->h * 0.5f};
    auto storlek = [&r](const gfx::ScreenInfo& sc) {
        return gfx::Sizef{r.logicalSize.w * sc.scale,
                          r.logicalSize.h * sc.scale};
    };

    const gfx::ScreenInfo* mål = primär;
    gfx::Pointf            hörn{};
    if (r.pos == StartPosition::Manual) {
        hörn = toPlacement(r.logical, primär->scale);
        mål  = screenFor(screens, hörn);
    } else if (r.pos == StartPosition::CenterOwner && ägarMitt) {
        mål                = screenFor(screens, *ägarMitt);
        const gfx::Sizef s = storlek(*mål);
        hörn = gfx::Pointf{ägarMitt->x - s.w * 0.5f, ägarMitt->y - s.h * 0.5f};
    } else {
        // CenterScreen — och CenterOwner utan en synlig ägare.
        if (ägarMitt)
            mål = screenFor(screens, *ägarMitt);
        const gfx::Sizef  s = storlek(*mål);
        const gfx::Rectf& w = mål->work;
        hörn = gfx::Pointf{w.x + (w.w - s.w) * 0.5f, w.y + (w.h - s.h) * 0.5f};
    }
    return clampToWork(hörn, storlek(*mål), *mål);
}

// Primärskärmens skala — `toLogical`/`toPlacement`s faktor. 1 utan skärmar
// (Wayland: positionen finns inte ändå).
inline float primaryScale(const std::vector<gfx::ScreenInfo>& screens)
{
    const gfx::ScreenInfo* p = primaryOf(screens);
    return p && p->scale > 0.0f ? p->scale : 1.0f;
}

} // namespace glaze::detail
