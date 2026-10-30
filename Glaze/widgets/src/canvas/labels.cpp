// Etikettkollisionen inkopplad — vad som samlas, hur
// lådorna räknas och när något laddas upp står i labels.h; algoritmen i
// placement.h.

#include "labels.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <gfx/platform.h>

#include "build.h"
#include "placement.h"

namespace glaze::detail {

namespace {

// Synligheten i bilden: 0 dold, 255 synlig.
constexpr std::uint8_t kHidden  = 0;
constexpr std::uint8_t kVisible = 255;

// En längd i kvadrat under det här är noll (en riktning som inte syns).
constexpr double kTiny = 1e-20;

bool sameView(const gfx::MapView& a, const gfx::MapView& b)
{
    return a.cx == b.cx && a.cy == b.cy && a.pxPerUnit == b.pxPerUnit &&
           a.bearingDeg == b.bearingDeg && a.pitchDeg == b.pitchDeg &&
           a.yUp == b.yUp && a.box.x == b.box.x && a.box.y == b.box.y &&
           a.box.w == b.box.w && a.box.h == b.box.h;
}

bool sameVec(const gfx::Vec3d& a, const gfx::Vec3d& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Ett frustum — båda tomma (synfältet i `fovY`), eller samma tangenter.
bool sameFrustum(const std::optional<gfx::FrustumTangents>& a,
                 const std::optional<gfx::FrustumTangents>& b)
{
    if (!a || !b)
        return !a && !b;
    return a->left == b->left && a->right == b->right && a->down == b->down &&
           a->up == b->up;
}

// 3D-målets vy — båda tomma, eller samma vy.
bool sameView3D(const std::optional<gfx::View3D>& a,
                const std::optional<gfx::View3D>& b)
{
    if (!a || !b)
        return !a && !b;
    return sameVec(a->eye, b->eye) && sameVec(a->forward, b->forward) &&
           sameVec(a->up, b->up) && a->projection == b->projection &&
           a->fovY == b->fovY && sameFrustum(a->frustum, b->frustum) &&
           a->orthoHalfHeight == b->orthoHalfHeight &&
           a->nearZ == b->nearZ && a->farZ == b->farZ && a->box.x == b->box.x &&
           a->box.y == b->box.y && a->box.w == b->box.w && a->box.h == b->box.h;
}

// Har lådan en YTA på ritytan? Placeringens överlapp (placement.h): kanter
// som bara möts räknas inte, och en låda som inte är ändlig når ingenting.
bool påYtan(const gfx::Rectf& b, const gfx::Rectf& yta)
{
    return std::max(b.x, yta.x) < std::min(b.x + b.w, yta.x + yta.w) &&
           std::max(b.y, yta.y) < std::min(b.y + b.h, yta.y + yta.h);
}

// De KOLLIDERANDE etiketternas texlar i placeringens ordning — prioritet,
// lager, index, med placement.h:s egen jämförelse (en definition av
// ordningen).
void ordna(CanvasResources::LabelState& st, std::span<const LabelSource> sources)
{
    std::vector<LabelBox>      nycklar;
    std::vector<std::uint32_t> texlar;
    std::uint32_t              t = 0;
    for (const LabelSource& s : sources)
        for (const CanvasResources::LabelInfo& li : s.build->labels) {
            if (li.collide && s.collide) {
                LabelBox b;
                b.priority = li.priority;
                b.layer    = s.layer;
                nycklar.push_back(b);
                texlar.push_back(t);
            }
            ++t;
        }
    const std::vector<std::uint32_t> ordning = placementOrder(nycklar);
    st.order.resize(ordning.size());
    for (std::size_t k = 0; k < ordning.size(); ++k)
        st.order[k] = texlar[ordning[k]];
}

// Ändrade texlar i [lo, hi) — tomt när lo ≥ hi.
struct Span {
    std::size_t lo = std::numeric_limits<std::size_t>::max();
    std::size_t hi = 0;
    void        add(std::size_t i)
    {
        lo = std::min(lo, i);
        hi = std::max(hi, i + 1);
    }
    bool empty() const { return lo >= hi; }
};

// LIGGANDE TEXT (`Alignment::Ground`): lådan på skärmen
// är den PROJICERADE QUADENS skärmlåda, axelparallell — Glyph-programmets
// `glyphGround` (gfx/scene.h `kGroundAligned`) räknat i double: baslinjen
// världsriktningen (cos, sin, 0), "nedåt" dess vinkelräta i planet vänd nedåt
// på skärmen, och em-storleken `sizePx` vid ankaret vinkelrätt mot blicken.
// Derivatorna tas ur projektionens klipprymd (matrisen är linjär i den).
// false när ett hörn inte har någon skärmpunkt.
bool groundGeometry(const CanvasResources::LabelInfo& li, double sizePx,
                    const Projection& p, double sx, double sy, LabelGeom& out)
{
    const double ax = li.anchor.x, ay = li.anchor.y, az = li.z;
    double       c[4];
    p.clip(ax, ay, az, c);
    // Världsriktningen v genom matrisen (M · (v, 0)) och som skärmens
    // riktning vid ankaret (perspektivdivisionens derivata, y nedåt).
    const auto genom = [&](double vx, double vy, double vz, double d[4]) {
        double q[4];
        p.clip(ax + vx, ay + vy, az + vz, q);
        for (int i = 0; i < 4; ++i)
            d[i] = q[i] - c[i];
    };
    const auto skärm = [&](double vx, double vy, double vz, double& ex,
                           double& ey) {
        double d[4];
        genom(vx, vy, vz, d);
        ex = (d[0] * c[3] - c[0] * d[3]) / (c[3] * c[3]);
        ey = (d[1] * c[3] - c[1] * d[3]) / (c[3] * c[3]);
    };
    const double ew[3] = {li.cosA, li.sinA, 0.0};
    double       dw[3] = {-li.sinA, li.cosA, 0.0};
    double       esx = 0.0, esy = 0.0, dsx = 0.0, dsy = 0.0;
    skärm(ew[0], ew[1], ew[2], esx, esy);
    skärm(dw[0], dw[1], dw[2], dsx, dsy);
    if (esx * dsy - esy * dsx < 0.0)
        for (double& v : dw)
            v = -v;
    // Blicken: w-raden, ortografiskt djupraden (programmets ordning).
    double dx[4], dy[4], dz[4];
    genom(1.0, 0.0, 0.0, dx);
    genom(0.0, 1.0, 0.0, dy);
    genom(0.0, 0.0, 1.0, dz);
    double fw[3] = {dx[3], dy[3], dz[3]};
    if (fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2] < kTiny) {
        fw[0] = dx[2];
        fw[1] = dy[2];
        fw[2] = dz[2];
    }
    if (fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2] < kTiny) {
        fw[0] = 0.0;
        fw[1] = 0.0;
        fw[2] = 1.0;
    }
    const auto kryss = [](const double a[3], const double b[3], double ut[3]) {
        ut[0] = a[1] * b[2] - a[2] * b[1];
        ut[1] = a[2] * b[0] - a[0] * b[2];
        ut[2] = a[0] * b[1] - a[1] * b[0];
    };
    double tv[3];
    kryss(fw, ew, tv);
    if (tv[0] * tv[0] + tv[1] * tv[1] + tv[2] * tv[2] < kTiny)
        kryss(fw, dw, tv);
    const double tl = std::sqrt(tv[0] * tv[0] + tv[1] * tv[1] + tv[2] * tv[2]);
    double       tx = 0.0, ty = 0.0;
    if (tl > 0.0)
        skärm(tv[0] / tl, tv[1] / tl, tv[2] / tl, tx, ty);
    const double k = std::hypot(tx, ty);
    const double s = sizePx / std::max(k, kTiny);   // världsenheter per em
    // Quadens fyra hörn (lådan i em) på skärmen.
    const gfx::TextBox& b  = li.box;
    const double        hx[2] = {b.x0, b.x1}, hy[2] = {b.y0, b.y1};
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    bool   först = true;
    for (const double ex : hx)
        for (const double ey : hy) {
            double px = 0.0, py = 0.0;
            if (!p.toScreen(ax + (ex * ew[0] + ey * dw[0]) * s,
                            ay + (ex * ew[1] + ey * dw[1]) * s,
                            az + (ex * ew[2] + ey * dw[2]) * s, px, py))
                return false;
            x0    = först ? px : std::min(x0, px);
            y0    = först ? py : std::min(y0, py);
            x1    = först ? px : std::max(x1, px);
            y1    = först ? py : std::max(y1, py);
            först = false;
        }
    out.ax    = sx;
    out.ay    = sy;
    out.cx    = (x0 + x1) / 2.0;
    out.cy    = (y0 + y1) / 2.0;
    out.halfW = (x1 - x0) / 2.0;
    out.halfH = (y1 - y0) / 2.0;
    out.ex    = 1.0;
    out.ey    = 0.0;
    return true;
}

} // namespace

std::int64_t LabelFrame::baseOf(std::uint64_t runId) const
{
    if (!state)
        return -1;
    for (const CanvasResources::LabelRun& r : state->runs)
        if (r.run == runId)
            return r.base;
    return -1;
}

bool labelGeometry(const CanvasResources::LabelInfo& li, double sizePx,
                   const Projection& p, LabelGeom& out)
{
    // Ankaret med sitt z — 0 för en plan etikett, så den räknas som i 2D.
    double sx = 0.0, sy = 0.0;
    if (!p.toScreen(li.anchor.x, li.anchor.y, li.z, sx, sy))
        return false;
    if (li.rotation == Alignment::Ground)
        return groundGeometry(li, sizePx, p, sx, sy, out);
    // Baslinjens riktning: skärmens vinkel (moturs, y nedåt), eller kartans
    // riktning så som kameran projicerar den vid ankaret — Glyph-programmets
    // två grenar, med samma reserv (vågrätt) när riktningen inte syns.
    double ex = li.cosA, ey = -li.sinA;
    if (li.rotation == Alignment::Map &&
        !p.direction(li.anchor.x, li.anchor.y, li.z, li.cosA, li.sinA, 0.0, ex,
                     ey)) {
        ex = 1.0;
        ey = 0.0;
    }
    // Lådans mitt i em (x höger, y NED före vridningen) läggs längs
    // baslinjen och "nedåt" (−e.y, e.x), som programmet lägger hörnen.
    const gfx::TextBox& b  = li.box;
    const double        mx = (b.x0 + b.x1) / 2.0, my = (b.y0 + b.y1) / 2.0;
    out.ax    = sx;
    out.ay    = sy;
    out.cx    = sx + sizePx * (mx * ex - my * ey);
    out.cy    = sy + sizePx * (mx * ey + my * ex);
    out.halfW = (b.x1 - b.x0) / 2.0 * sizePx;
    out.halfH = (b.y1 - b.y0) / 2.0 * sizePx;
    out.ex    = ex;
    out.ey    = ey;
    return true;
}

LabelFrame updateLabels(const PaintContext& f,
                        const std::shared_ptr<SceneData>& scene,
                        std::span<const LabelSource> sources, bool fade)
{
    // Ritningens plats: vyns egen placering eller bildens (scene.h
    // `PaintContext::labelSlot`).
    CanvasResources::LabelState& st = f.res->labelState(scene, f.labelSlot);
    LabelFrame                   out;
    out.state = &st;

    // KÖRNINGARNA i ritordning, med var deras etiketter börjar.
    std::vector<CanvasResources::LabelRun> runs;
    runs.reserve(sources.size());
    std::uint32_t n = 0;
    for (const LabelSource& s : sources) {
        const auto count = static_cast<std::uint32_t>(s.build->labels.size());
        runs.push_back(CanvasResources::LabelRun{s.run->id, s.build->serial, n,
                                                 count, s.layer, s.collide});
        n += count;
    }

    const bool sameRuns = st.placed && runs == st.runs;
    const bool sameAll  = sameRuns && sameView(st.view, f.world) &&
                          sameView3D(st.view3D, f.view3D) &&
                          st.pxPerPx == f.c->scale && st.pxPerMm == f.c->pxPerMm;
    Span changed;
    // Kan någon etikett döljas — av kollisionen eller av sitt zoomintervall?
    // Annars behövs ingen bild.
    bool anyHideable = false;

    if (!sameAll) {
        // LÅDORNA på skärmen och PLACERINGEN — bara för etiketterna som NÅR
        // YTAN. En låda helt utanför syns inte: den döljs och tränger inte
        // undan någon (MapLibre placerar inte heller utanför vyn). Det är
        // också det som håller placeringen billig i en inzoomad karta, där
        // nästan alla lådor ligger utanför — de delade annars rutnätets
        // kantceller, och varje fråga där prövades mot tusentals.
        //
        // ORDNINGEN beror inte på kameran: den räknas om bara när etiketterna
        // ändrats (en ny körning, ett nytt bygge, lagrets kollision) — med
        // 10 000 etiketter på skärmen var sorteringen hälften av placeringen.
        if (!sameRuns)
            ordna(st, sources);
        // Ritningens projektion: 3D-målets vy, annars kartkamerans.
        const Projection          proj =
            f.view3D ? Projection(*f.view3D) : Projection(f.world);
        const gfx::Rectf&         yta = f.world.box;
        std::vector<LabelBox>     låda(n);    // per texel …
        std::vector<std::uint8_t> når(n, 0);  // … om den når ytan
        st.target.assign(n, kHidden);
        std::uint32_t texel = 0;
        for (const LabelSource& s : sources) {
            const double unitPx = pxPerUnit(f, f.world, s.run->key.unit);
            // GOLVET OCH TAKET: lådan har den storlek Glyph-programmet
            // ritar — samma klampning med samma tätheter som anropet.
            const GlyphLimits lim =
                glyphLimits(s.run->key, f.c->scale, f.c->pxPerMm,
                            static_cast<float>(f.world.pxPerUnit));
            for (const CanvasResources::LabelInfo& li : s.build->labels) {
                const std::uint32_t i = texel++;
                // ZOOMINTERVALLET först: utanför är etiketten dold och
                // tar ingen plats. Med ett intervall är den döljbar också
                // innanför — bilden står kvar över tröskeln.
                if (li.minZoom > 0.0 || li.maxZoom > 0.0)
                    anyHideable = true;
                if (!zoomInRange(li.minZoom, li.maxZoom, f.zoom))
                    continue;   // målet är redan kHidden
                if (!(li.collide && s.collide)) {
                    st.target[i] = kVisible;   // alltid synlig, tar ingen plats
                    continue;
                }
                anyHideable = true;
                const double raw = li.size * unitPx;
                const double px  = clampGlyphPx(raw, lim);
                const double k   = raw > 0.0 ? px / raw : 1.0;
                LabelGeom g;
                if (!labelGeometry(li, px, proj, g))
                    continue;   // bakom närplanet: programmet ritar den inte
                LabelBox& b = låda[i];
                b.cx        = static_cast<float>(g.cx);
                b.cy        = static_cast<float>(g.cy);
                b.halfW     = static_cast<float>(g.halfW);
                b.halfH     = static_cast<float>(g.halfH);
                b.dirX      = static_cast<float>(g.ex);
                b.dirY      = static_cast<float>(g.ey);
                b.padding   = static_cast<float>(li.padding * unitPx * k);
                b.priority  = li.priority;
                b.layer     = s.layer;
                // Helt utanför — också en låda som inte är ändlig.
                når[i] = påYtan(labelBounds(b), yta) ? 1 : 0;
            }
        }
        // De som når ytan, i placeringens ordning.
        std::vector<LabelBox>      boxes;
        std::vector<std::uint32_t> plats;   // deras texlar
        for (const std::uint32_t t : st.order)
            if (når[t]) {
                boxes.push_back(låda[t]);
                plats.push_back(t);
            }
        const std::vector<std::uint8_t> vis = placeLabelsInOrder(boxes, yta);
        f.res->notePlacement();
        for (std::size_t k = 0; k < plats.size(); ++k)
            st.target[plats[k]] = vis[k] ? kVisible : kHidden;
        if (!sameRuns) {
            // Nya etiketter: indexen betyder något annat nu, så ingenting
            // tonas över — bilden får målet, hela.
            st.shown = st.target;
            if (n > 0)
                changed = Span{0, n};
        } else if (!fade) {
            for (std::uint32_t i = 0; i < n; ++i)
                if (st.shown[i] != st.target[i]) {
                    st.shown[i] = st.target[i];
                    changed.add(i);
                }
        }
        st.placed  = true;
        st.view    = f.world;
        st.view3D  = f.view3D;
        st.pxPerPx = f.c->scale;
        st.pxPerMm = f.c->pxPerMm;
        st.runs    = std::move(runs);
        if (!anyHideable) {
            // Ingen etikett kan döljas: ingen bild, alla synliga.
            f.res->dropLabels(st);
            st.fadeMs = 0;
            return out;
        }
    } else {
        anyHideable = static_cast<bool>(st.tex);
        if (!anyHideable)
            return out;
    }

    // TONINGEN: det bilden bär går mot målet med lika steg per ms — och
    // stannar när det är framme (ingen mer självinvalidering, regel 1).
    // Utan toning är bilden redan målet.
    const std::uint64_t now = gfx::ticksMs();
    if (!fade) {
        for (std::size_t i = 0; i < st.shown.size(); ++i)
            if (st.shown[i] != st.target[i]) {   // toningen slogs av mitt i
                st.shown[i] = st.target[i];
                changed.add(i);
            }
        st.fadeMs = 0;
    } else {
        const double ms   = st.fadeMs ? static_cast<double>(now - st.fadeMs) : 0.0;
        const int    step = std::max(
            1, static_cast<int>(std::lround(kVisible * ms / labelFadeMs())));
        bool moving = false;
        for (std::size_t i = 0; i < st.shown.size(); ++i) {
            const int cur = st.shown[i], to = st.target[i];
            if (cur == to)
                continue;
            const int next = cur < to ? std::min(to, cur + step)
                                      : std::max(to, cur - step);
            st.shown[i] = static_cast<std::uint8_t>(next);
            changed.add(i);
            moving = moving || next != to;
        }
        st.fadeMs     = moving ? now : 0;
        out.animating = moving;
    }

    if (!changed.empty() || !st.tex)
        f.res->uploadLabels(st, changed.empty() ? 0 : changed.lo,
                            changed.empty() ? st.shown.size() : changed.hi);
    out.tex = st.tex.get();
    return out;
}

} // namespace glaze::detail
