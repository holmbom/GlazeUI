#pragma once
// DET RUMSLIGA TRÄDET — INTERN, ren std. Ett fyrträd i planet med nodernas
// lådor i xyz: en fråga besöker bara de noder vars låda den släpper in, så
// kostnaden följer det som syns och inte det som finns.
//
// NODERNA har en STABIL nyckel (nivå, ix, iy): nivå ℓ har cellsidan
// `kLeafCell · 2^ℓ`, och en post hamnar på den lägsta nivå där dess utsträckning
// i planet ryms i en cell, i cellen där dess mitt ligger. Samma poster ger
// samma noder oavsett i vilken ordning de lades in — trädet är deterministiskt,
// och en nod går att känna igen mellan två byggen. Världen är obegränsad:
// noderna ligger glest i en tabell, och rötterna (noderna på den översta
// nivån — fler än en när innehållet ligger på båda sidor om en axel) lyfts en
// nivå i taget när en post kräver det.
//
// LÅDAN i en nod är unionen av dess posters och barns lådor (i double). En
// post får alltså sticka ut ur sin cell; frågan prövar nodens LÅDA, aldrig
// cellen, så svaret är konservativt: varje post vars låda frågan hade släppt
// in ligger i en nod som frågan besöker.
//
// TRÄDET BYGGS OM, det redigeras inte: `clear` och `insert` — den som äger det
// vet när innehållet ändrats (canvas/run_index.h).

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace glaze::detail {

class SpatialIndex {
public:
    struct Box {
        double minX = 0.0, minY = 0.0, minZ = 0.0;
        double maxX = 0.0, maxY = 0.0, maxZ = 0.0;
    };

    void clear();
    // Posten `item` (ägarens tal) med lådan `box`. Lådan ska vara ändlig.
    void insert(std::uint32_t item, const Box& box);

    std::size_t size() const { return items_; }
    std::size_t nodeCount() const { return nodes_.size(); }

    // FRÅGAN: `enter(låda)` avgör om en nod besöks — den ska svara sant för
    // varje låda som innehåller något den vill ha — och `visit(post)` får
    // varje post i de besökta noderna, i en ordning som inte är bestämd.
    template <class Enter, class Visit>
    void query(Enter&& enter, Visit&& visit) const
    {
        std::vector<std::int32_t>& stack = stack_;
        stack.assign(roots_.begin(), roots_.end());
        while (!stack.empty()) {
            const Node& n = nodes_[static_cast<std::size_t>(stack.back())];
            stack.pop_back();
            if (!enter(n.box))
                continue;
            for (std::uint32_t i = n.first; i != kNone; i = next_[i])
                visit(item_[i]);
            for (std::int32_t c : n.child)
                if (c >= 0)
                    stack.push_back(c);
        }
    }

private:
    static constexpr std::uint32_t kNone = 0xffffffffu;

    struct Node {
        Box          box;
        int          level = 0;
        long long    ix = 0, iy = 0;
        std::int32_t parent   = -1;
        std::int32_t child[4] = {-1, -1, -1, -1};
        std::uint32_t first = kNone;   // nodens poster, en kedja genom `next_`
        bool          hasBox = false;
    };
    struct Key {
        int       level;
        long long ix, iy;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };

    // Noden med nyckeln, eller en ny; `fresh` säger vilket.
    std::int32_t nodeAt(int level, long long ix, long long iy, bool& fresh);
    // Noden hängs på sin förälder, och föräldrarna skapas upp till rötternas
    // nivå — en ny nod på den nivån blir en rot.
    void link(std::int32_t n);
    // Rötterna lyfts till nivån `level`.
    void raise(int level);

    std::vector<Node>                              nodes_;
    std::unordered_map<Key, std::int32_t, KeyHash> byKey_;
    std::vector<std::uint32_t>                     item_, next_;   // posternas kedjor
    std::vector<std::int32_t>                      roots_;
    int                                            top_   = 0;   // rötternas nivå
    std::size_t                                    items_ = 0;
    mutable std::vector<std::int32_t>              stack_;   // frågans arbetsminne
};

} // namespace glaze::detail
