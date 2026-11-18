// DET RUMSLIGA TRÄDET (spatial_index.h).

#include "spatial_index.h"

#include <algorithm>
#include <cmath>

namespace glaze::detail {

namespace {

// BLADENS CELLSIDA i världsenheter. En post mindre än den ligger på nivå 0;
// en större på den nivå där den ryms. Sidan är ett mått på hur fint trädet
// delar det minsta innehållet, inte en gräns: i en värld vars föremål är
// mycket mindre än sidan hamnar fler av dem i samma blad, och frågan prövar
// då dem en och en.
constexpr double kLeafCell = 16.0;

// Den högsta nivån: cellsidan 16 · 2^40 världsenheter — längre än något
// koordinatsystem ritytan möter. En större post ligger där.
constexpr int kMaxLevel = 40;

// Rötterna som mest: fyra räcker för innehåll kring ett cellhörn (två celler
// per axel), och några till ger utrymme innan nästa lyft.
constexpr std::size_t kMaxRoots = 8;

// Cellindexens spann — heltal som ryms exakt i en double.
constexpr double kMaxIndex = 4503599627370496.0;   // 2^52

long long cellOf(double v, double cell)
{
    const double t = std::floor(v / cell);
    if (!std::isfinite(t))
        return 0;
    return static_cast<long long>(std::clamp(t, -kMaxIndex, kMaxIndex));
}

void grow(SpatialIndex::Box& a, bool& has, const SpatialIndex::Box& b)
{
    if (!has) {
        a   = b;
        has = true;
        return;
    }
    a.minX = std::min(a.minX, b.minX);
    a.minY = std::min(a.minY, b.minY);
    a.minZ = std::min(a.minZ, b.minZ);
    a.maxX = std::max(a.maxX, b.maxX);
    a.maxY = std::max(a.maxY, b.maxY);
    a.maxZ = std::max(a.maxZ, b.maxZ);
}

} // namespace

std::size_t SpatialIndex::KeyHash::operator()(const Key& k) const noexcept
{
    // FNV-1a över nyckelns tre tal.
    std::uint64_t       h = 14695981039346656037ull;
    const std::uint64_t v[3] = {static_cast<std::uint64_t>(k.level),
                                static_cast<std::uint64_t>(k.ix),
                                static_cast<std::uint64_t>(k.iy)};
    for (std::uint64_t x : v)
        for (int i = 0; i < 8; ++i) {
            h ^= (x >> (8 * i)) & 0xffu;
            h *= 1099511628211ull;
        }
    return static_cast<std::size_t>(h);
}

void SpatialIndex::clear()
{
    nodes_.clear();
    byKey_.clear();
    item_.clear();
    next_.clear();
    roots_.clear();
    top_   = 0;
    items_ = 0;
}

std::int32_t SpatialIndex::nodeAt(int level, long long ix, long long iy, bool& fresh)
{
    const auto [it, inserted] =
        byKey_.try_emplace(Key{level, ix, iy}, static_cast<std::int32_t>(nodes_.size()));
    fresh = inserted;
    if (inserted) {
        Node n;
        n.level = level;
        n.ix    = ix;
        n.iy    = iy;
        nodes_.push_back(n);
    }
    return it->second;
}

void SpatialIndex::raise(int level)
{
    while (top_ < level) {
        std::vector<std::int32_t> upp;
        for (std::int32_t r : roots_) {
            const Node  barn  = nodes_[static_cast<std::size_t>(r)];
            bool        fresh = false;
            const std::int32_t p = nodeAt(top_ + 1, barn.ix >> 1, barn.iy >> 1, fresh);
            Node&       förälder = nodes_[static_cast<std::size_t>(p)];
            förälder.child[(barn.ix & 1) | ((barn.iy & 1) << 1)] = r;
            if (barn.hasBox)
                grow(förälder.box, förälder.hasBox, barn.box);
            nodes_[static_cast<std::size_t>(r)].parent = p;
            if (fresh)
                upp.push_back(p);
        }
        roots_ = std::move(upp);
        ++top_;
    }
}

void SpatialIndex::link(std::int32_t n)
{
    std::int32_t cur = n;
    while (nodes_[static_cast<std::size_t>(cur)].level < top_) {
        const Node  barn  = nodes_[static_cast<std::size_t>(cur)];
        bool        fresh = false;
        const std::int32_t p = nodeAt(barn.level + 1, barn.ix >> 1, barn.iy >> 1, fresh);
        nodes_[static_cast<std::size_t>(p)].child[(barn.ix & 1) | ((barn.iy & 1) << 1)] = cur;
        nodes_[static_cast<std::size_t>(cur)].parent = p;
        if (!fresh)
            return;   // resten av kedjan finns redan
        cur = p;
    }
    roots_.push_back(cur);
}

void SpatialIndex::insert(std::uint32_t item, const Box& box)
{
    // NIVÅN: den lägsta där utsträckningen i planet ryms i en cell.
    const double ext = std::max(box.maxX - box.minX, box.maxY - box.minY);
    int          level = 0;
    if (ext > kLeafCell && std::isfinite(ext))
        level = std::min(kMaxLevel, static_cast<int>(std::ceil(std::log2(ext / kLeafCell))));
    else if (!std::isfinite(ext))
        level = kMaxLevel;
    const double cell = std::ldexp(kLeafCell, level);
    const long long ix = cellOf((box.minX + box.maxX) * 0.5, cell);
    const long long iy = cellOf((box.minY + box.maxY) * 0.5, cell);

    if (nodes_.empty())
        top_ = level;
    else if (level > top_)
        raise(level);
    bool               fresh = false;
    const std::int32_t n     = nodeAt(level, ix, iy, fresh);
    if (fresh) {
        link(n);
        // FÅ RÖTTER: frågan börjar i dem, så de lyfts tills de är få — annars
        // hade ett träd av små poster haft en rot per post.
        while (roots_.size() > kMaxRoots && top_ < kMaxLevel)
            raise(top_ + 1);
    }

    // Posten först i nodens kedja, och lådan upp genom förfäderna.
    const auto plats = static_cast<std::uint32_t>(item_.size());
    item_.push_back(item);
    next_.push_back(nodes_[static_cast<std::size_t>(n)].first);
    nodes_[static_cast<std::size_t>(n)].first = plats;
    ++items_;
    for (std::int32_t cur = n; cur >= 0; cur = nodes_[static_cast<std::size_t>(cur)].parent) {
        Node& nod = nodes_[static_cast<std::size_t>(cur)];
        grow(nod.box, nod.hasBox, box);
    }
}

} // namespace glaze::detail
