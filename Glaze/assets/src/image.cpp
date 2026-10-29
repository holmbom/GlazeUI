// Bildverktyget — skalaren och valregeln. REN STD.
//
// SKALAREN ÄR SEPARERBAR: först längs x (källa w×h → mål W×h), sedan längs y
// (→ W×H). Varje axel får en lista bidrag per målpixel — (källindex, vikt) —
// räknad EN gång. Nedåt är vikterna den exakta överlappningen mellan
// målpixelns intervall och källpixelns (yt-medelvärde); uppåt är de två
// bilinjära grannarna.
//
// HELTAL HELA VÄGEN, med flit: bytena ska vara identiska på alla plattformar
// (de committade ikonfilerna), och float är det inte — clang på ARM64
// slår ihop multiplikation och addition till FMA där x86-64 inte gör det.
// Vikterna är 16.16-fixtal som summerar EXAKT till 65536 per målpixel
// (avrundningsresten läggs på det största bidraget), och överlappningarna är
// rationella tal i heltal: källpixel j täcker [j·W, (j+1)·W) och målpixel i
// täcker [i·w, (i+1)·w) i samma enhet.
//
// FÖRMULTIPLICERAD RYMD: färgkanalerna bärs som c·a och alfan som a·255 —
// samma skala, 0..65025 — så en genomskinlig pixels färg aldrig vägs in.
// Tillbaka till rak alfa delar vi med den vägda alfan.

#include "glaze_assets/image.h"

#include <algorithm>
#include <cstddef>

namespace glzassets {
namespace {

constexpr std::uint32_t kOne = 65536;   // vikten 1.0 i 16.16

struct Tap {
    int           src;
    std::uint32_t w;
};

// Bidragen för en axel: dst målpixlar ur src källpixlar. `first[i]` är
// målpixel i:s första bidrag i `taps`; `first[dst]` är slutet.
struct Axis {
    std::vector<Tap>         taps;
    std::vector<std::size_t> first;
};

Axis axisTaps(int src, int dst)
{
    Axis a;
    a.first.reserve(std::size_t(dst) + 1);
    for (int i = 0; i < dst; ++i) {
        a.first.push_back(a.taps.size());
        if (dst < src) {
            // Yt-medelvärde: överlappningen i enheten 1/(src·dst).
            const std::int64_t lo = std::int64_t(i) * src;
            const std::int64_t hi = lo + src;
            const int j0 = int(lo / dst);
            const int j1 = int((hi - 1) / dst);
            std::uint32_t sum = 0;
            std::size_t   big = a.taps.size();
            std::int64_t  bigOv = -1;
            for (int j = j0; j <= j1; ++j) {
                const std::int64_t s  = std::max(lo, std::int64_t(j) * dst);
                const std::int64_t e  = std::min(hi, std::int64_t(j + 1) * dst);
                const std::int64_t ov = e - s;
                if (ov <= 0)
                    continue;
                const auto w = std::uint32_t(ov * kOne / src);
                if (ov > bigOv) {
                    bigOv = ov;
                    big   = a.taps.size();
                }
                a.taps.push_back({j, w});
                sum += w;
            }
            a.taps[big].w += kOne - sum;   // resten på det största bidraget
        } else if (dst > src) {
            // Bilinjärt: pixelcentrum mot pixelcentrum,
            // x = ((2i+1)·src − dst) / (2·dst), kanten klampad.
            const std::int64_t num = std::int64_t(2 * i + 1) * src - dst;
            const std::int64_t den = std::int64_t(2) * dst;
            int           j0 = 0;
            std::uint32_t t  = 0;
            if (num > 0) {
                j0 = int(num / den);
                t  = std::uint32_t((num % den) * kOne / den);
            }
            const int j1 = std::min(j0 + 1, src - 1);
            if (j1 == j0 || t == 0) {
                a.taps.push_back({j0, kOne});
            } else {
                a.taps.push_back({j0, kOne - t});
                a.taps.push_back({j1, t});
            }
        } else {
            a.taps.push_back({i, kOne});
        }
    }
    a.first.push_back(a.taps.size());
    return a;
}

} // namespace

Rgba resizeRgba(const Rgba& src, int w, int h)
{
    Rgba out;
    if (src.empty() || w <= 0 || h <= 0 ||
        src.px.size() < std::size_t(src.width) * std::size_t(src.height) * 4)
        return out;
    if (w == src.width && h == src.height) {
        out = src;
        out.px.resize(std::size_t(w) * std::size_t(h) * 4);
        return out;
    }

    const Axis ax = axisTaps(src.width, w);
    const Axis ay = axisTaps(src.height, h);

    // Pass 1, längs x: källrad för källrad till W pixlar. Mellanvärdet
    // bär 24 bitar (summan >> 8) — 65025·65536 >> 8 ryms i uint32.
    const std::size_t sw = std::size_t(src.width);
    std::vector<std::uint32_t> mid(std::size_t(w) * std::size_t(src.height) * 4);
    for (int y = 0; y < src.height; ++y) {
        const std::uint8_t* row = src.px.data() + std::size_t(y) * sw * 4;
        std::uint32_t*      dst = mid.data() + std::size_t(y) * std::size_t(w) * 4;
        for (int x = 0; x < w; ++x) {
            std::uint64_t acc[4] = {0, 0, 0, 0};
            for (std::size_t k = ax.first[std::size_t(x)];
                 k < ax.first[std::size_t(x) + 1]; ++k) {
                const std::uint8_t* p = row + std::size_t(ax.taps[k].src) * 4;
                const std::uint64_t wt = ax.taps[k].w;
                const std::uint32_t a  = p[3];
                acc[0] += wt * (std::uint32_t(p[0]) * a);
                acc[1] += wt * (std::uint32_t(p[1]) * a);
                acc[2] += wt * (std::uint32_t(p[2]) * a);
                acc[3] += wt * (a * 255u);
            }
            for (int c = 0; c < 4; ++c)
                dst[x * 4 + c] = std::uint32_t((acc[c] + 128) >> 8);
        }
    }

    // Pass 2, längs y, och tillbaka till rak alfa. Summan bär skalan
    // 255·2^24 för alfan: a = round(acc / (255 << 24)); färgen är den
    // vägda färgen delad med den vägda alfan, c = round(255·acc_c / acc_a).
    out.width  = w;
    out.height = h;
    out.px.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    const std::uint64_t alphaDiv = std::uint64_t(255) << 24;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = out.px.data() + std::size_t(y) * std::size_t(w) * 4;
        for (int x = 0; x < w; ++x) {
            std::uint64_t acc[4] = {0, 0, 0, 0};
            for (std::size_t k = ay.first[std::size_t(y)];
                 k < ay.first[std::size_t(y) + 1]; ++k) {
                const std::uint32_t* p =
                    mid.data() +
                    (std::size_t(ay.taps[k].src) * std::size_t(w) + std::size_t(x)) * 4;
                const std::uint64_t wt = ay.taps[k].w;
                for (int c = 0; c < 4; ++c)
                    acc[c] += wt * p[c];
            }
            std::uint8_t* o = row + std::size_t(x) * 4;
            const std::uint64_t a = (acc[3] + alphaDiv / 2) / alphaDiv;
            o[3] = std::uint8_t(std::min<std::uint64_t>(a, 255));
            if (o[3] == 0 || acc[3] == 0)
                continue;   // helt genomskinlig: färgen är 0, som i källans tomrum
            for (int c = 0; c < 3; ++c) {
                const std::uint64_t v = (acc[c] * 255 + acc[3] / 2) / acc[3];
                o[c] = std::uint8_t(std::min<std::uint64_t>(v, 255));
            }
        }
    }
    return out;
}

Rgba fitSquare(const Rgba& src, int side)
{
    if (src.empty() || side <= 0)
        return {};
    if (src.width == src.height)
        return resizeRgba(src, side, side);

    const int longest = std::max(src.width, src.height);
    const int nw = std::max(1, int((std::int64_t(src.width) * side + longest / 2) / longest));
    const int nh = std::max(1, int((std::int64_t(src.height) * side + longest / 2) / longest));
    const Rgba inner = resizeRgba(src, nw, nh);

    Rgba out;
    out.width  = side;
    out.height = side;
    out.px.assign(std::size_t(side) * std::size_t(side) * 4, 0);
    const int ox = (side - nw) / 2;
    const int oy = (side - nh) / 2;
    for (int y = 0; y < nh; ++y)
        std::copy_n(inner.px.data() + std::size_t(y) * std::size_t(nw) * 4,
                    std::size_t(nw) * 4,
                    out.px.data() +
                        (std::size_t(oy + y) * std::size_t(side) + std::size_t(ox)) * 4);
    return out;
}

int pickIconSource(const std::vector<int>& sides, int target)
{
    int best = -1;       // minsta sidan som räcker
    int largest = -1;    // största sidan över huvud taget
    for (std::size_t i = 0; i < sides.size(); ++i) {
        const int s = sides[i];
        if (s <= 0)
            continue;
        if (s >= target && (best < 0 || s < sides[std::size_t(best)]))
            best = int(i);
        if (largest < 0 || s > sides[std::size_t(largest)])
            largest = int(i);
    }
    return best >= 0 ? best : largest;
}

} // namespace glzassets
