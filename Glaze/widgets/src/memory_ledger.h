#pragma once
// MINNETS LIGGARE — INTERN. Ett fönsters räkning av det ritytorna håller,
// per KONTO, och vräkningen över budgeten. Bildcachen äger liggaren
// (`ImageCache::ledger()`); bildcachen, ritytans resurser, föremålens
// residens och draperingarna är dess ÄGARE (`MemoryHolder`) och ger den sina
// poster.
//
// KONTONA: en rityta som ritar öppnar sitt konto (`beginAccount` — scenens
// id, scenen som livstecken och dess budget; `detail::MemoryAccount` i
// canvas/memory_account.h), och varje post som ANVÄNDS medan det är öppet
// (`use`) räknas på det — på den rityta som använde den senast. Det som
// används utanför ritytorna räknas på FÖNSTRET, konto 0, med appens förval.
// Ett dött kontos poster flyttas till fönstret.
//
// EN POST (`Charge`) är ett stycke minne som en ägare håller: en textur, en
// buffert, ett mål — eller en CPU-kopia. Posten bär sitt konto, sina byte och
// bilden den senast användes i. GPU-poster och CPU-poster räknas var för sig
// och jämförs TILLSAMMANS mot kontots budget.
//
// VRÄKNINGEN sker BARA i `trim()`, i bildens början — aldrig mitt i en bild,
// där en batchad ritning kan peka på resursen. För varje konto över sin
// budget vräks dess VRÄKBARA poster som legat oanvända längst; en post som
// användes när kontots rityta SENAST RITADE vräks aldrig — en bild där
// ritytan inte ritades om (skadan låg någon annanstans) rör alltså inte det
// den visar — och inte en post ägaren säger att den inte kan återskapa. Ordningen är bestämd: äldst först, vid
// lika ägarnas ordning (`add`), sedan ägarens slag och nyckel.
//
// CPU-ORIGINALEN (meshens lägen, molnets punkter, bildens pixlar) är inga
// poster: ritytan summerar dem själv och lämnar summan med `setAccountCpu`.
// De räknas mot budgeten och vräks aldrig.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gfx/image.h"
#include "gfx/scene.h"

namespace glaze::detail {

// RESURSERNAS BYTE som liggaren räknar dem — NOMINELLA tal ur måtten, inte
// drivrutinens allokering: en buffert är sin storlek, en bild sina pixlar i
// nivå 0 (en mip-kedja är GPU:ns tredjedel till ovanpå), och ett mål sin
// upplösta bild plus, med MSAA, den provtagna ytan och, med djup, fyra byte
// per prov. Ingenting (nullptr, ogiltigt) är 0.
inline std::size_t bufferBytes(const gfx::GpuBuffer* b)
{
    return b && b->valid() ? b->size() : 0;
}
inline std::size_t imageBytes(const gfx::Image* i)
{
    return i && i->valid() ? static_cast<std::size_t>(i->width()) *
                                 static_cast<std::size_t>(i->height()) *
                                 gfx::bytesPerPixel(i->format())
                           : 0;
}
inline std::size_t targetBytes(gfx::RenderTarget* t)
{
    if (!t || !t->image().valid())
        return 0;
    const std::size_t pixels  = static_cast<std::size_t>(t->image().width()) *
                                static_cast<std::size_t>(t->image().height());
    const std::size_t samples = static_cast<std::size_t>(t->samples());
    constexpr std::size_t kDepthBytes = 4;   // D32F
    std::size_t bytes = pixels * gfx::bytesPerPixel(t->image().format());
    if (samples > 1)
        bytes += bytes * samples;
    if (t->hasDepth())
        bytes += pixels * samples * kDepthBytes;
    return bytes;
}

enum class MemoryPlace : std::uint8_t { Gpu, Cpu };

// En posts räkning. Ägaren lagrar den i sin post och rör bara `place`;
// kontot, bytena och `lastUse` sätts av liggaren (`book`, `use`).
struct Charge {
    std::uint64_t account = 0;   // kontot som använde posten senast
    std::size_t   bytes   = 0;
    std::uint64_t lastUse = 0;   // liggarens bild (`frame()`) vid senaste användningen
    MemoryPlace   place   = MemoryPlace::Gpu;
};

// Nyckeln en ägare känner igen sin post på — ägarens SLAG och upp till tre
// tal (ett id, eller (id, w, h)). Slaget avgör också ordningen bland poster
// som användes i samma bild: lägst först.
struct ChargeKey {
    int           kind = 0;
    std::uint64_t id   = 0;
    int           a = 0, b = 0;
    bool operator<(const ChargeKey& o) const
    {
        return kind != o.kind ? kind < o.kind
               : id != o.id   ? id < o.id
               : a != o.a     ? a < o.a
                              : b < o.b;
    }
};

// En ägare av poster. `eachCharge` besöker varje post med dess nyckel och om
// den får vräkas; `evict` släpper posten med nyckeln och tar bort dess räkning
// (`MemoryLedger::drop`) — en nyckel utan post gör ingenting. Liggaren anropar
// `evict` bara mellan två bilder.
class MemoryHolder {
public:
    using Visit = std::function<void(Charge&, const ChargeKey&, bool evictable)>;
    virtual void eachCharge(const Visit& visit) = 0;
    virtual void evict(const ChargeKey& key)    = 0;

protected:
    ~MemoryHolder() = default;
};

// Ett kontos minne, i byte.
struct MemoryUse {
    std::size_t gpu = 0;
    std::size_t cpu = 0;
    std::size_t total() const { return gpu + cpu; }
};

class MemoryLedger {
public:
    // ÄGARNA, i den ordning de lades till — ordningen avgör vräkningen bland
    // poster som användes i samma bild. En ägare tar bort sig innan den dör.
    void add(MemoryHolder& holder);
    void remove(MemoryHolder& holder);

    // KONTONA. `beginAccount` öppnar kontot `id` (≠ 0) med livstecknet och
    // budgeten (0 = förvalet) och svarar med det som var öppet; `endAccount`
    // öppnar det igen. `open()` är det öppna kontot.
    std::uint64_t beginAccount(std::uint64_t id, std::weak_ptr<const void> life,
                               std::size_t budget);
    void          endAccount(std::uint64_t previous) { account_ = previous; }
    std::uint64_t open() const { return account_; }

    // POSTERNA. `book` sätter postens konto och byte — en ny post, en ny
    // storlek, ett annat konto; samma konto och storlek ändrar ingenting.
    // `use` antecknar att posten används i den här bilden och räknar den på
    // det öppna kontot med storleken `bytes`. `drop` tar bort räkningen.
    void book(Charge& c, std::uint64_t account, std::size_t bytes);
    void use(Charge& c, std::size_t bytes)
    {
        const bool first = c.lastUse != frame_ || c.account != account_;
        c.lastUse        = frame_;
        book(c, account_, bytes);
        if (first)
            noteUsed(bytes);
    }
    void drop(Charge& c) { book(c, 0, 0); }

    // CPU-originalen på ett öppnat konto (filhuvudet).
    void setAccountCpu(std::uint64_t id, std::size_t bytes);

    // Kontots minne: posterna och CPU-originalen. Ett okänt konto är tomt.
    MemoryUse usage(std::uint64_t id) const;
    // Kontona som finns, i id-ordning (fönstret, 0, först när det finns) —
    // för mätningar och grindar.
    std::vector<std::uint64_t> accounts() const;

    // Har det öppna kontot plats för mer som inte syns? Sant under
    // fyllnadsandelen av dess budget (med förvalet från senaste `trim`).
    bool underBudget() const;

    // Bildens början (filhuvudet): döda kontons poster till fönstret,
    // vräkningen per konto över budgeten (kontots, eller `defaultBudget` när
    // den är 0 — och alltid för fönstret), en loggrad per konto och episod
    // när budgeten inte går att hålla, och en ny bild.
    void trim(std::size_t defaultBudget);

    // Budgeten för ett konto med den egna budgeten `own` (0 = förvalet från
    // senaste `trim`; före den första ingen gräns) — det ritytans motor
    // jämför sina byte med, så den aldrig frågar appen.
    std::size_t budgetFor(std::size_t own) const;

    // Bilden posterna stämplas med, och antalet vräkningar hittills.
    std::uint64_t frame() const { return frame_; }
    std::uint64_t evictions() const { return evictions_; }
    // Vräkningarna och deras byte i senaste `trim`.
    std::uint64_t lastEvictions() const { return lastEvictions_; }
    std::size_t   lastEvictedBytes() const { return lastEvictedBytes_; }

private:
    struct Account {
        std::weak_ptr<const void> life;
        std::size_t               budget = 0;   // 0 = förvalet
        MemoryUse                 charged;      // posternas byte
        std::size_t               fixed = 0;    // CPU-originalen (`setAccountCpu`)
        // Bilden kontot senast ÖPPNADES i (ritytan ritade; fönstret varje
        // bild), och posternas byte som använts sedan dess — är det allt
        // kontot bär finns ingenting att vräka, och `trim` letar inte.
        std::uint64_t             drawn = 0;
        std::size_t               used  = 0;
        bool                      overLogged = false;
    };
    void        noteUsed(std::size_t bytes);
    std::size_t budgetOf(std::uint64_t id, const Account& a) const;
    bool        over(std::uint64_t id) const;

    std::vector<MemoryHolder*>                 holders_;
    std::unordered_map<std::uint64_t, Account> accounts_;
    std::uint64_t                              account_ = 0;   // det öppna
    std::uint64_t                              frame_   = 0;   // stegas av trim()
    std::size_t   defaultBudget_    = 0;   // senaste `trim`; 0 = ingen ännu
    std::uint64_t evictions_        = 0;
    std::uint64_t lastEvictions_    = 0;
    std::size_t   lastEvictedBytes_ = 0;
};

} // namespace glaze::detail
