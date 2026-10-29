// Etikettplaceringen — regeln och ordningen står i
// placement.h. Här: den omslutande lådan, rutnätet och den giriga loopen.

#include "placement.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace glaze::detail {
namespace {

// Rutnätets cell i fysiska px. En etikett täcker i regel
// en till tre celler i bredd, så en fråga rör få celler och varje cell få
// lådor.
constexpr float kCellPx = 64.0f;

// Taket per axel: en ritytas box är högst några tusen px, men en felaktig
// skärm ska inte kunna begära ett rutnät på gigabyte. Över taket delar de
// yttersta cellerna på resten — långsammare, aldrig fel (se `cellAv`).
constexpr int kMaxCells = 512;

// Den omslutande lådan i skärmens axlar.
struct Aabb {
    float x0, y0, x1, y1;
};

// EXAKT överlapp: två lådor krockar när SNITTET har en yta. Lådor som bara
// möts i en kant krockar alltså inte, och en låda utan yta (luften har
// krympt den till en punkt eller ett streck) krockar med ingenting — inte
// heller när den ligger inne i en annan.
bool överlappar(const Aabb& a, const Aabb& b)
{
    return std::max(a.x0, b.x0) < std::min(a.x1, b.x1) &&
           std::max(a.y0, b.y0) < std::min(a.y1, b.y1);
}

bool ändlig(const LabelBox& l)
{
    return std::isfinite(l.cx) && std::isfinite(l.cy) &&
           std::isfinite(l.halfW) && std::isfinite(l.halfH) &&
           std::isfinite(l.dirX) && std::isfinite(l.dirY) &&
           std::isfinite(l.padding);
}

// Den vridna lådans omslutande, med luften utanpå. |x| och |y| av baslinjens
// riktning räcker: den omslutande är densamma åt båda vridhållen, och en
// lodrät riktning (0, ±1) ger en lodrät låda utan en tusendel px av bredden
// kvar.
Aabb omslutande(const LabelBox& l)
{
    const float  c  = std::fabs(l.dirX);
    const float  s  = std::fabs(l.dirY);
    const float  hw = std::max(0.0f, l.halfW);
    const float  hh = std::max(0.0f, l.halfH);
    const float  ex = std::max(0.0f, c * hw + s * hh + l.padding);
    const float  ey = std::max(0.0f, s * hw + c * hh + l.padding);
    return Aabb{l.cx - ex, l.cy - ey, l.cx + ex, l.cy + ey};
}

// Cellen en koordinat hamnar i, KLAMPAD till rutnätet. Klampningen sker i
// flyttal FÖRE heltalet — en låda 1e30 px bort får inte spräcka en int — och
// är MONOTON: överlappar två lådor på en axel finns en punkt i båda, och
// dess cell ligger i båda lådornas intervall. Två lådor som krockar delar
// alltså alltid minst en cell, var de än ligger; det är det som gör
// rutnätet till ren grovsållning, utan att `screen` kan ändra svaret.
int cellAv(float v, float origo, int n)
{
    const float c = std::floor((v - origo) / kCellPx);
    return static_cast<int>(std::clamp(c, 0.0f, static_cast<float>(n - 1)));
}

int cellerPå(float längd)
{
    if (!(längd > 0.0f) || !std::isfinite(längd))
        return 1;
    return static_cast<int>(std::clamp(std::ceil(längd / kCellPx), 1.0f,
                                       static_cast<float>(kMaxCells)));
}

// DEN GIRIGA PLACERINGEN, etiketterna i `ordning` (index i `labels`).
std::vector<std::uint8_t> placera(std::span<const LabelBox>      labels,
                                  std::span<const std::uint32_t> ordning,
                                  const gfx::Rectf&              screen)
{
    const std::size_t         n = labels.size();
    std::vector<std::uint8_t> synlig(n, 0);
    if (n == 0)
        return synlig;

    // RUTNÄTET: varje cell är en enkellänkad lista (`huvud` → `noder`) över
    // de insatta lådor som täcker den. En skärm utan användbara mått blir
    // EN cell — rätt, bara långsamt.
    const bool  skärmOk = std::isfinite(screen.x) && std::isfinite(screen.y);
    const float ox      = skärmOk ? screen.x : 0.0f;
    const float oy      = skärmOk ? screen.y : 0.0f;
    const int   kol     = skärmOk ? cellerPå(screen.w) : 1;
    const int   rad     = skärmOk ? cellerPå(screen.h) : 1;

    struct Nod {
        int låda;
        int nästa;
    };
    std::vector<int>           huvud(static_cast<std::size_t>(kol) *
                                         static_cast<std::size_t>(rad),
                                     -1);
    std::vector<Nod>           noder;
    std::vector<Aabb>          lådor;     // de insatta, i insättningsordning
    std::vector<std::uint32_t> stämpel;   // senaste fråga som prövade lådan
    noder.reserve(n * 2);
    lådor.reserve(n);
    stämpel.reserve(n);
    std::uint32_t fråga = 0;

    for (const std::uint32_t i : ordning) {
        const LabelBox& l = labels[i];
        if (!l.collide) {
            synlig[i] = 1;   // döljs aldrig, täcker ingen
            continue;
        }
        if (!ändlig(l))
            continue;        // kan inte placeras — döljs

        const Aabb b  = omslutande(l);
        const int  x0 = cellAv(b.x0, ox, kol);
        const int  x1 = cellAv(b.x1, ox, kol);
        const int  y0 = cellAv(b.y0, oy, rad);
        const int  y1 = cellAv(b.y1, oy, rad);

        // En låda som täcker flera celler ligger i alla — stämpeln gör att
        // den prövas EN gång per fråga ändå.
        ++fråga;
        bool krock = false;
        for (int cy = y0; cy <= y1 && !krock; ++cy)
            for (int cx = x0; cx <= x1 && !krock; ++cx)
                for (int k = huvud[static_cast<std::size_t>(cy) * kol + cx];
                     k >= 0; k = noder[static_cast<std::size_t>(k)].nästa) {
                    const auto id =
                        static_cast<std::size_t>(noder[static_cast<std::size_t>(k)].låda);
                    if (stämpel[id] == fråga)
                        continue;
                    stämpel[id] = fråga;
                    if (överlappar(b, lådor[id])) {
                        krock = true;
                        break;
                    }
                }
        if (krock)
            continue;

        synlig[i]    = 1;
        const int id = static_cast<int>(lådor.size());
        lådor.push_back(b);
        stämpel.push_back(0);
        for (int cy = y0; cy <= y1; ++cy)
            for (int cx = x0; cx <= x1; ++cx) {
                int& h = huvud[static_cast<std::size_t>(cy) * kol + cx];
                noder.push_back(Nod{id, h});
                h = static_cast<int>(noder.size()) - 1;
            }
    }
    return synlig;
}

} // namespace

gfx::Rectf labelBounds(const LabelBox& l)
{
    const Aabb b = omslutande(l);
    return gfx::Rectf{b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0};
}

std::vector<std::uint32_t> placementOrder(std::span<const LabelBox> labels)
{
    // Prioritet, lager, index (placement.h). Index sist gör ordningen total,
    // så `std::sort` ger samma ordning varje gång.
    std::vector<std::uint32_t> ordning(labels.size());
    std::iota(ordning.begin(), ordning.end(), 0u);
    std::sort(ordning.begin(), ordning.end(),
              [&labels](std::uint32_t a, std::uint32_t b) {
                  const LabelBox& la = labels[a];
                  const LabelBox& lb = labels[b];
                  if (la.priority != lb.priority)
                      return la.priority > lb.priority;
                  if (la.layer != lb.layer)
                      return la.layer > lb.layer;
                  return a < b;
              });
    return ordning;
}

std::vector<std::uint8_t> placeLabels(std::span<const LabelBox> labels,
                                      const gfx::Rectf&         screen)
{
    return placera(labels, placementOrder(labels), screen);
}

std::vector<std::uint8_t> placeLabelsInOrder(std::span<const LabelBox> ordered,
                                             const gfx::Rectf&         screen)
{
    std::vector<std::uint32_t> rakt(ordered.size());
    std::iota(rakt.begin(), rakt.end(), 0u);
    return placera(ordered, rakt, screen);
}

} // namespace glaze::detail
