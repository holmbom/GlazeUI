#include "interface3d_logic.h"

#include <algorithm>
#include <cmath>

namespace glaze::openxr::detail {

namespace {


int clampInt(int v, int lo, int hi)
{
    return std::max(lo, std::min(v, hi));
}

// En cell i rutnätet: första raden/kolumnen och hur många den spänner över.
struct Cell {
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
};

std::vector<Cell> assignCells(const Panel& panel)
{
    const Layout& layout = panel.layout;
    const std::size_t n  = panel.elements.size();
    std::vector<Cell> cells(n);
    switch (layout.kind) {
    case LayoutKind::Row: {
        int cursor = 0;
        for (std::size_t i = 0; i < n; ++i) {
            cells[i].column     = cursor;
            cells[i].columnSpan = std::max(1, panel.elements[i].columnSpan);
            cursor += cells[i].columnSpan;
        }
        break;
    }
    case LayoutKind::Column: {
        int cursor = 0;
        for (std::size_t i = 0; i < n; ++i) {
            cells[i].row     = cursor;
            cells[i].rowSpan = std::max(1, panel.elements[i].rowSpan);
            cursor += cells[i].rowSpan;
        }
        break;
    }
    case LayoutKind::Grid: {
        const int columns = std::max(1, layout.columns);
        // Utan angivna celler (alla på 0, 0) läggs elementen ut radvis.
        bool unplaced = n > 1;
        for (const Element& e : panel.elements)
            if (e.row != 0 || e.column != 0)
                unplaced = false;
        // Upptagna celler, rad för rad: ett element med span tar alla sina.
        std::vector<std::vector<bool>> taken;
        const auto isFree = [&](int r, int c, int rows, int cols) {
            for (int rr = r; rr < r + rows; ++rr)
                for (int cc = c; cc < c + cols; ++cc)
                    if (static_cast<std::size_t>(rr) < taken.size() &&
                        taken[static_cast<std::size_t>(rr)][static_cast<std::size_t>(cc)])
                        return false;
            return true;
        };
        int row = 0, column = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const Element& e = panel.elements[i];
            Cell& cell       = cells[i];
            cell.columnSpan  = std::min(std::max(1, e.columnSpan), columns);
            cell.rowSpan     = std::max(1, e.rowSpan);
            if (unplaced) {
                // Nästa lediga plats i läsordning där hela spannet ryms.
                for (;;) {
                    if (column + cell.columnSpan > columns) {
                        column = 0;
                        ++row;
                    } else if (isFree(row, column, cell.rowSpan, cell.columnSpan)) {
                        break;
                    } else {
                        ++column;
                    }
                }
                if (taken.size() < static_cast<std::size_t>(row + cell.rowSpan))
                    taken.resize(static_cast<std::size_t>(row + cell.rowSpan),
                                 std::vector<bool>(static_cast<std::size_t>(columns), false));
                for (int rr = row; rr < row + cell.rowSpan; ++rr)
                    for (int cc = column; cc < column + cell.columnSpan; ++cc)
                        taken[static_cast<std::size_t>(rr)][static_cast<std::size_t>(cc)] = true;
                cell.row    = row;
                cell.column = column;
                column += cell.columnSpan;
            } else {
                cell.column = clampInt(e.column, 0, columns - cell.columnSpan);
                cell.row    = std::max(0, e.row);
                if (layout.rows > 0)
                    cell.row = clampInt(cell.row, 0, std::max(0, layout.rows - cell.rowSpan));
            }
        }
        break;
    }
    case LayoutKind::Free:
        break;
    }
    return cells;
}

// Kolumnbredder eller radhöjder: de största i varje, sedan underskottet hos
// element med span fördelat jämnt över det de spänner över.
std::vector<double> trackSizes(const std::vector<double>& sizes,
                               const std::vector<int>& first,
                               const std::vector<int>& span, int count,
                               double spacing)
{
    std::vector<double> track(static_cast<std::size_t>(std::max(0, count)), 0.0);
    for (std::size_t i = 0; i < sizes.size(); ++i)
        if (span[i] == 1)
            track[static_cast<std::size_t>(first[i])] =
                std::max(track[static_cast<std::size_t>(first[i])], sizes[i]);
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        if (span[i] <= 1)
            continue;
        double have = spacing * (span[i] - 1);
        for (int k = 0; k < span[i]; ++k)
            have += track[static_cast<std::size_t>(first[i] + k)];
        if (sizes[i] > have) {
            const double add = (sizes[i] - have) / span[i];
            for (int k = 0; k < span[i]; ++k)
                track[static_cast<std::size_t>(first[i] + k)] += add;
        }
    }
    return track;
}

double totalOf(const std::vector<double>& track, double spacing)
{
    double sum = 0.0;
    for (double t : track)
        sum += t;
    if (!track.empty())
        sum += spacing * static_cast<double>(track.size() - 1);
    return sum;
}

// Var i ett utrymme [start, start + room] ett block av storleken `size` hamnar.
double alignedStart(Align align, double start, double room, double size)
{
    switch (align) {
    case Align::Start:  return start;
    case Align::Center: return start + (room - size) / 2.0;
    case Align::End:    return start + room - size;
    }
    return start;
}

} // namespace

// ----------------------------------------------------------------- VEKTORER
gfx::Color mixColor(gfx::Color a, gfx::Color b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const auto mix = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (y - x) * t));
    };
    return gfx::Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

// ------------------------------------------------------------------- LAYOUT
const ElementStyle& effectiveStyle(const Panel& panel, const Element& element)
{
    return element.style ? *element.style : panel.style.element;
}

PanelLayout layoutPanel(const Panel& panel)
{
    PanelLayout out;
    const Layout& layout = panel.layout;
    const double  pad    = std::max(0.0, panel.style.padding);
    const double  gap    = std::max(0.0, panel.style.spacing);
    const std::size_t n  = panel.elements.size();
    out.places.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const ElementStyle& s = effectiveStyle(panel, panel.elements[i]);
        out.places[i].width   = std::max(0.0, s.width);
        out.places[i].height  = std::max(0.0, s.height);
    }

    if (layout.kind == LayoutKind::Free) {
        if (layout.autoSize) {
            if (n == 0) {
                out.width = out.height = 2.0 * pad;
            } else {
                double lo_u = 0, hi_u = 0, lo_v = 0, hi_v = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const Pose& p = panel.elements[i].pose;
                    const double u0 = p.position.x - out.places[i].width / 2.0;
                    const double u1 = p.position.x + out.places[i].width / 2.0;
                    const double v0 = p.position.z - out.places[i].height / 2.0;
                    const double v1 = p.position.z + out.places[i].height / 2.0;
                    if (i == 0) {
                        lo_u = u0; hi_u = u1; lo_v = v0; hi_v = v1;
                    } else {
                        lo_u = std::min(lo_u, u0); hi_u = std::max(hi_u, u1);
                        lo_v = std::min(lo_v, v0); hi_v = std::max(hi_v, v1);
                    }
                }
                out.width  = hi_u - lo_u + 2.0 * pad;
                out.height = hi_v - lo_v + 2.0 * pad;
                out.plateU = (lo_u + hi_u) / 2.0;
                out.plateV = (lo_v + hi_v) / 2.0;
            }
        } else {
            out.width  = std::max(0.0, layout.width);
            out.height = std::max(0.0, layout.height);
        }
        return out;   // `u`, `v` är 0: elementets `pose` placerar det
    }

    const std::vector<Cell> cells = assignCells(panel);
    int columnCount = 0, rowCount = 0;
    std::vector<int> firstColumn(n), spanColumn(n), firstRow(n), spanRow(n);
    std::vector<double> widths(n), heights(n);
    for (std::size_t i = 0; i < n; ++i) {
        firstColumn[i] = cells[i].column;
        spanColumn[i]  = cells[i].columnSpan;
        firstRow[i]    = cells[i].row;
        spanRow[i]     = cells[i].rowSpan;
        widths[i]      = out.places[i].width;
        heights[i]     = out.places[i].height;
        columnCount    = std::max(columnCount, cells[i].column + cells[i].columnSpan);
        rowCount       = std::max(rowCount, cells[i].row + cells[i].rowSpan);
    }
    if (layout.kind == LayoutKind::Grid && layout.rows > 0)
        rowCount = std::max(rowCount, layout.rows);
    const std::vector<double> columnW = trackSizes(widths, firstColumn, spanColumn, columnCount, gap);
    const std::vector<double> rowH    = trackSizes(heights, firstRow, spanRow, rowCount, gap);
    const double contentW = totalOf(columnW, gap);
    const double contentH = totalOf(rowH, gap);

    if (layout.autoSize) {
        out.width  = contentW + 2.0 * pad;
        out.height = contentH + 2.0 * pad;
    } else {
        out.width  = std::max(0.0, layout.width);
        out.height = std::max(0.0, layout.height);
    }

    // Innehållsblockets övre vänstra hörn i det inre utrymmet.
    const double innerW = out.width - 2.0 * pad;
    const double innerH = out.height - 2.0 * pad;
    const double blockLeft = alignedStart(layout.horizontal, -out.width / 2.0 + pad, innerW, contentW);
    // Uppåt är v: blockets överkant = inre överkant, justerad nedåt.
    const double blockTop = out.height / 2.0 - pad
                            - alignedStart(layout.vertical, 0.0, innerH, contentH);

    const auto trackStart = [gap](const std::vector<double>& track, int index) {
        double start = 0.0;
        for (int k = 0; k < index; ++k)
            start += track[static_cast<std::size_t>(k)] + gap;
        return start;
    };
    const auto trackExtent = [gap](const std::vector<double>& track, int index, int span) {
        double extent = gap * (span - 1);
        for (int k = 0; k < span; ++k)
            extent += track[static_cast<std::size_t>(index + k)];
        return extent;
    };
    for (std::size_t i = 0; i < n; ++i) {
        const Cell& c      = cells[i];
        const double cellL = blockLeft + trackStart(columnW, c.column);
        const double cellW = trackExtent(columnW, c.column, c.columnSpan);
        const double cellT = blockTop - trackStart(rowH, c.row);
        const double cellH = trackExtent(rowH, c.row, c.rowSpan);
        ElementPlace& p    = out.places[i];
        p.u = alignedStart(layout.horizontal, cellL, cellW, p.width) + p.width / 2.0;
        // Lodrätt: Start = överkant.
        p.v = cellT - (alignedStart(layout.vertical, 0.0, cellH, p.height) + p.height / 2.0);
    }
    return out;
}

Pose composeScaled(const Pose& parent, const Pose& child, double scale)
{
    return compose(parent, Pose{vmul(child.position, scale), child.orientation});
}

Pose elementFrame(const ElementPlace& place, const Element& element)
{
    Pose cell;
    cell.position = Vec3{place.u, 0.0, place.v};
    return compose(cell, element.pose);
}

// ----------------------------------------------------------- PLACERING
Pose mirrorForLeftHand(const Pose& offset)
{
    Pose out = offset;
    out.position.x    = -offset.position.x;
    out.orientation.y = -offset.orientation.y;
    out.orientation.z = -offset.orientation.z;
    return out;
}

Pose attachedPose(const Pose& base, const Mount& mount, bool left, double scale)
{
    Pose offset = mount.pose;
    if (left && mount.mirrorForLeft)
        offset = mirrorForLeftHand(offset);
    return composeScaled(base, offset, scale);
}

double yawFacing(Vec3 panelPosition, Vec3 head)
{
    const double dx = panelPosition.x - head.x;
    const double dy = panelPosition.y - head.y;
    return std::atan2(dx, dy) * 180.0 / kPi;
}

Pose faceHead(const Pose& pose, Vec3 head)
{
    const double dx = pose.position.x - head.x;
    const double dy = pose.position.y - head.y;
    if (std::hypot(dx, dy) < 1e-9)
        return pose;
    YawPitchRoll ypr = toYawPitchRoll(pose.orientation);
    ypr.yawDegrees   = yawFacing(pose.position, head);
    Pose out         = pose;
    out.orientation  = fromYawPitchRoll(ypr);
    return out;
}

// ---------------------------------------------------------------- PEKARE
Vec3 controllerTip(const WorldPose& aim, double tipOffset, double scale)
{
    return vadd(aim.position, vmul(vnormalized(aim.forward), tipOffset * scale));
}

double gazeAngleDegrees(Vec3 head, Vec3 headForward, Vec3 target)
{
    const Vec3 forward = vnormalized(headForward);
    const Vec3 toward  = vnormalized(vsub(target, head));
    if (vlength(forward) < 0.5 || vlength(toward) < 0.5)
        return 180.0;
    return std::acos(std::clamp(vdot(forward, toward), -1.0, 1.0)) * 180.0 / kPi;
}

bool visibleByConditions(const Visibility& visibility, bool gazeOk, bool handOk)
{
    const bool gazeOn = visibility.gazeDegrees > 0.0;
    const bool handOn = visibility.otherHandMetres > 0.0;
    if (!gazeOn && !handOn)
        return true;
    if (gazeOn && handOn)
        return visibility.requireBoth ? (gazeOk && handOk) : (gazeOk || handOk);
    return gazeOn ? gazeOk : handOk;
}

double stepFade(double fade, bool visible, double dt, double fadeSeconds)
{
    if (!(fadeSeconds > 0.0))
        return visible ? 1.0 : 0.0;
    const double step = std::max(0.0, dt) / fadeSeconds;
    return std::clamp(visible ? fade + step : fade - step, 0.0, 1.0);
}

TipEval evalTip(Vec3 local, const ElementGeom& g, double hoverDistance, double margin)
{
    TipEval e;
    e.penetration = local.y + g.depth;
    e.over = std::abs(local.x) <= g.width / 2.0 + margin &&
             std::abs(local.z) <= g.height / 2.0 + margin;
    const double maxPenetration = g.depth + g.plateThickness + kBehindPlate;
    e.inRange = e.over && e.penetration >= -hoverDistance && e.penetration <= maxPenetration;
    e.distance = std::max(0.0, -e.penetration);
    if (e.penetration >= 0.0)
        e.proximity = 1.0;
    else if (hoverDistance > 0.0)
        e.proximity = std::clamp(1.0 - (-e.penetration) / hoverDistance, 0.0, 1.0);
    return e;
}

double pressDepthOf(double penetration, double travel)
{
    if (!(travel > 0.0))
        return penetration > 0.0 ? 1.0 : 0.0;
    return std::clamp(penetration / travel, 0.0, 1.0);
}

PressEdge stepPress(bool& pressed, double depth, double on, double off)
{
    if (!pressed) {
        if (depth >= on) {
            pressed = true;
            return PressEdge::Pressed;
        }
        return PressEdge::None;
    }
    if (depth < off) {
        pressed = false;
        return PressEdge::Released;
    }
    return PressEdge::None;
}

RayHit rayVsFace(Vec3 origin, Vec3 direction, double planeY, double width, double height)
{
    RayHit hit;
    if (!(direction.y > 1e-9))
        return hit;
    const double t = (planeY - origin.y) / direction.y;
    if (!(t >= 0.0))
        return hit;
    hit.hit   = true;
    hit.t     = t;
    hit.local = vadd(origin, vmul(direction, t));
    hit.inside = std::abs(hit.local.x) <= width / 2.0 && std::abs(hit.local.z) <= height / 2.0;
    return hit;
}

// -------------------------------------------------------------- VÄRDEN
double quantizeValue(double value, double min, double max, double step)
{
    if (max < min)
        std::swap(min, max);
    double v = std::clamp(value, min, max);
    if (step > 0.0) {
        const double n = std::round((v - min) / step);
        v              = min + n * step;
        if (v > max)
            v = min + std::floor((max - min) / step + 1e-9) * step;
        v = std::clamp(v, min, max);
    }
    return v;
}

SliderTrack sliderTrack(double width, double height, bool hasText, bool hasIcon)
{
    SliderTrack t;
    const double pad = height * 0.2;
    t.labelWidth = hasText ? width * 0.35 : (hasIcon ? height : 0.0);
    t.left       = -width / 2.0 + t.labelWidth + pad;
    t.right      = width / 2.0 - pad;
    const double minSpan = width * 0.2;
    if (t.right - t.left < minSpan)
        t.left = t.right - minSpan;
    t.barHeight  = height * 0.18;
    t.knobWidth  = height * 0.22;
    t.knobHeight = height * 0.6;
    return t;
}

double sliderValueAt(double x, const SliderTrack& track, double min, double max, double step)
{
    const double span = track.right - track.left;
    const double t    = span > 0.0 ? std::clamp((x - track.left) / span, 0.0, 1.0) : 0.0;
    return quantizeValue(min + t * (max - min), min, max, step);
}

// Värdets andel av [min, max], klampad till 0 … 1 (0 när intervallet är tomt).
static double valueFraction(double value, double min, double max)
{
    const double range = max - min;
    return range != 0.0 ? std::clamp((value - min) / range, 0.0, 1.0) : 0.0;
}

double sliderKnobX(double value, const SliderTrack& track, double min, double max)
{
    return track.left + valueFraction(value, min, max) * (track.right - track.left);
}

double rollAngle(double right_x, double right_z)
{
    return std::atan2(-right_z, right_x);
}

double unwrapAngle(double delta)
{
    delta = std::fmod(delta, 2.0 * kPi);
    if (delta > kPi)
        delta -= 2.0 * kPi;
    else if (delta <= -kPi)
        delta += 2.0 * kPi;
    return delta;
}

double dialValueFrom(double value0, double turned, double min, double max, double step)
{
    const double sweep = kDialSweepDegrees * kPi / 180.0;
    return quantizeValue(value0 + turned / sweep * (max - min), min, max, step);
}

double dialMarkerAngle(double value, double min, double max)
{
    const double t = valueFraction(value, min, max);
    return (-kDialSweepDegrees / 2.0 + t * kDialSweepDegrees) * kPi / 180.0;
}

// ------------------------------------------------------------- FRAMSIDAN
double textureScale(double faceWidth, double faceHeight, int pxPerMetre)
{
    constexpr double kMaxSide = 4096.0;
    double k = std::max(50, pxPerMetre);
    const double longest = std::max(faceWidth, faceHeight);
    if (longest * k > kMaxSide && longest > 0.0)
        k = kMaxSide / longest;
    return k;
}

double faceRim(const ElementStyle& style)
{
    return std::max(0.0, std::min(0.0025, 0.1 * std::min(style.width, style.height)));
}

} // namespace glaze::openxr::detail
