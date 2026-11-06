#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <gfx/scene.h>

// INTERN — RENDER-MÅL ÅTERANVÄNDA ÖVER FRAMER, per fönster: klippmaskerna
// (clip.h `ClipMasks`) och målen för en rityta på en annan yta
// (resources.h `pageTarget`). Ett mål bär det som ritats för en NYCKEL och
// används igen för samma nyckel; ett mål av samma mått som inte använts i den
// här framen tas av en ny nyckel; ett som inte använts på två framer släpps.
// Målet skapas av anroparen — maskerna och sidorna vill ha olika slag.

namespace glaze::detail {

// Hur många bilder ett mål (och klippmaskernas minne, clip.h) sparas utan att
// användas innan det släpps.
inline constexpr std::uint64_t kKeptFrames = 2;

class TargetPool {
public:
    struct Slot {
        std::unique_ptr<gfx::RenderTarget> target;   // tomt tills anroparen skapat det
        int                                w = 0, h = 0;
        std::uint64_t                      key   = 0;
        bool                               built = false;   // bär det som ritades för `key`
        std::uint64_t                      used  = 0;       // framens löpnummer
    };

    // Det byggda målet för nyckeln, märkt använt i framen `serial` — eller
    // nullptr.
    Slot* find(std::uint64_t key, std::uint64_t serial)
    {
        for (const std::unique_ptr<Slot>& s : slots_)
            if (s->built && s->key == key) {
                s->used = serial;
                return s.get();
            }
        return nullptr;
    }

    // Ett mål w × h för nyckeln, märkt använt i framen: det byggda för den,
    // annars ett av samma mått som inte använts i framen (`built` false), annars
    // ett nytt (`built` false, `target` tomt). Platserna flyttar aldrig, så en
    // referens håller medan andra tas.
    Slot& take(std::uint64_t key, int w, int h, std::uint64_t serial)
    {
        Slot* ledig = nullptr;
        for (const std::unique_ptr<Slot>& s : slots_) {
            if (s->w != w || s->h != h)
                continue;
            if (s->built && s->key == key) {
                s->used = serial;
                return *s;
            }
            if (!ledig && s->used != serial)
                ledig = s.get();
        }
        if (!ledig) {
            slots_.push_back(std::make_unique<Slot>());
            ledig    = slots_.back().get();
            ledig->w = w;
            ledig->h = h;
        }
        ledig->key   = key;
        ledig->built = false;
        ledig->used  = serial;
        return *ledig;
    }

    // Släpper det som inte använts på två framer.
    void trim(std::uint64_t serial)
    {
        std::erase_if(slots_, [serial](const std::unique_ptr<Slot>& s) {
            return s->used + kKeptFrames < serial;
        });
    }

    std::size_t size() const { return slots_.size(); }

    // Summan w × h över målen som finns skapade.
    std::uint64_t pixels() const
    {
        std::uint64_t n = 0;
        for (const std::unique_ptr<Slot>& s : slots_)
            if (s->target)
                n += static_cast<std::uint64_t>(s->w) * static_cast<std::uint64_t>(s->h);
        return n;
    }

private:
    std::vector<std::unique_ptr<Slot>> slots_;
};

} // namespace glaze::detail
