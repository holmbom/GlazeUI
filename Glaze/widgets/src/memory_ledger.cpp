// MINNETS LIGGARE — kontona, räkningen och vräkningen (memory_ledger.h).

#include "memory_ledger.h"

#include "gfx/platform.h"   // gfx::log

#include <algorithm>
#include <limits>

namespace glaze::detail {

namespace {

// FYLLNADSANDELEN: det som inte syns hålls resident bara medan kontot ligger
// under den här andelen av sin budget. Avståndet upp till budgeten är
// utrymmet det synliga får växa i innan något vräks — utan det hade nästa
// `trim` vräkt det som just laddades upp, och bilden efter laddat upp det igen.
constexpr double kResidentFill = 0.9;

std::size_t& placeOf(MemoryUse& u, MemoryPlace p)
{
    return p == MemoryPlace::Gpu ? u.gpu : u.cpu;
}

} // namespace

void MemoryLedger::add(MemoryHolder& holder)
{
    if (std::find(holders_.begin(), holders_.end(), &holder) == holders_.end())
        holders_.push_back(&holder);
}

void MemoryLedger::remove(MemoryHolder& holder)
{
    holders_.erase(std::remove(holders_.begin(), holders_.end(), &holder), holders_.end());
}

std::uint64_t MemoryLedger::beginAccount(std::uint64_t id, std::weak_ptr<const void> life,
                                         std::size_t budget)
{
    const std::uint64_t previous = account_;
    if (id == 0)
        return previous;   // 0 är fönstret — inget att öppna
    Account& a = accounts_[id];
    a.life     = std::move(life);
    a.budget   = budget;   // gäller från den här ritningen
    if (a.drawn != frame_) {
        a.drawn = frame_;   // ritytan ritar i den här bilden
        a.used  = 0;
    }
    account_   = id;
    return previous;
}

void MemoryLedger::book(Charge& c, std::uint64_t account, std::size_t bytes)
{
    // Det vanliga fallet — samma rityta, samma storlek — ändrar ingenting:
    // inga uppslag i kontona per post och bild.
    if (c.account == account && c.bytes == bytes)
        return;
    placeOf(accounts_[c.account].charged, c.place) -= c.bytes;
    c.account = account;
    c.bytes   = bytes;
    placeOf(accounts_[account].charged, c.place) += bytes;
}

void MemoryLedger::noteUsed(std::size_t bytes)
{
    accounts_[account_].used += bytes;
}

void MemoryLedger::setAccountCpu(std::uint64_t id, std::size_t bytes)
{
    if (const auto it = accounts_.find(id); it != accounts_.end())
        it->second.fixed = bytes;
}

MemoryUse MemoryLedger::usage(std::uint64_t id) const
{
    const auto it = accounts_.find(id);
    if (it == accounts_.end())
        return {};
    MemoryUse u = it->second.charged;
    u.cpu += it->second.fixed;
    return u;
}

std::vector<std::uint64_t> MemoryLedger::accounts() const
{
    std::vector<std::uint64_t> ids;
    ids.reserve(accounts_.size());
    for (const auto& [id, a] : accounts_)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::size_t MemoryLedger::budgetOf(std::uint64_t id, const Account& a) const
{
    return budgetFor(id != 0 ? a.budget : 0);
}

std::size_t MemoryLedger::budgetFor(std::size_t own) const
{
    if (own > 0)
        return own;
    // Före den första `trim` finns inget förval: ingen gräns.
    return defaultBudget_ > 0 ? defaultBudget_ : std::numeric_limits<std::size_t>::max();
}

bool MemoryLedger::over(std::uint64_t id) const
{
    const auto it = accounts_.find(id);
    if (it == accounts_.end())
        return false;
    const Account& a = it->second;
    return a.charged.total() + a.fixed > budgetOf(id, a);
}

bool MemoryLedger::underBudget() const
{
    const auto it = accounts_.find(account_);
    if (it == accounts_.end())
        return true;
    const Account& a     = it->second;
    const double   limit = static_cast<double>(budgetOf(account_, a)) * kResidentFill;
    return static_cast<double>(a.charged.total() + a.fixed) < limit;
}

void MemoryLedger::trim(std::size_t defaultBudget)
{
    defaultBudget_    = defaultBudget;
    lastEvictions_    = 0;
    lastEvictedBytes_ = 0;

    // 1. DÖDA KONTON — en riven rityta lämnar sina poster åt fönstret.
    //    Fönstrets konto finns FÖRE vandringen, så `book` aldrig lägger till
    //    ett konto mitt i den.
    accounts_[0];
    bool anyDead = false;
    for (const auto& [id, a] : accounts_)
        anyDead = anyDead || (id != 0 && a.life.expired());
    if (anyDead) {
        for (MemoryHolder* h : holders_)
            h->eachCharge([this](Charge& c, const ChargeKey&, bool) {
                const auto it = accounts_.find(c.account);
                if (c.account != 0 && it != accounts_.end() && it->second.life.expired())
                    book(c, 0, c.bytes);
            });
        for (auto it = accounts_.begin(); it != accounts_.end();)
            it = it->first != 0 && it->second.life.expired() ? accounts_.erase(it) : std::next(it);
    }
    if (!accounts_.count(account_))
        account_ = 0;

    // 2. ÖVER BUDGETEN, PER KONTO: de vräkbara posterna som legat oanvända
    //    längst först, aldrig en som användes när kontots rityta senast ritade
    //    (lastUse ≥ kontots `drawn`). En post vräks bara medan DESS konto
    //    ligger över sin budget.
    //    Ett konto vars alla poster användes i bilden har ingenting att vräka
    //    — då letas ägarna inte igenom.
    bool anyOver = false;
    for (const auto& [id, a] : accounts_)
        anyOver = anyOver || (a.charged.total() + a.fixed > budgetOf(id, a) &&
                              a.used < a.charged.total());
    if (anyOver) {
        struct Candidate {
            std::uint64_t lastUse;
            std::size_t   holder;   // ägarens plats i `holders_`
            ChargeKey     key;
            std::uint64_t account;
            std::size_t   bytes;
            bool operator<(const Candidate& o) const
            {
                return lastUse != o.lastUse  ? lastUse < o.lastUse
                       : holder != o.holder  ? holder < o.holder
                                             : key < o.key;
            }
        };
        std::vector<Candidate> candidates;
        for (std::size_t h = 0; h < holders_.size(); ++h)
            holders_[h]->eachCharge(
                [&](Charge& c, const ChargeKey& key, bool evictable) {
                    if (!evictable || c.bytes == 0 || !over(c.account))
                        return;
                    const auto it = accounts_.find(c.account);
                    if (it != accounts_.end() && c.lastUse < it->second.drawn)
                        candidates.push_back(Candidate{c.lastUse, h, key, c.account, c.bytes});
                });
        std::sort(candidates.begin(), candidates.end());
        for (const Candidate& k : candidates) {
            // Kontot kan ha kommit under budgeten av en tidigare vräkning.
            if (!over(k.account))
                continue;
            holders_[k.holder]->evict(k.key);
            ++evictions_;
            ++lastEvictions_;
            lastEvictedBytes_ += k.bytes;
        }
    }

    // 3. EN loggrad per konto och episod — det som användes i förra bilden
    //    och det som inte får vräkas överskrider budgeten.
    for (auto& [id, a] : accounts_) {
        if (a.charged.total() + a.fixed <= budgetOf(id, a)) {
            a.overLogged = false;
            continue;
        }
        if (!a.overLogged)
            gfx::log("Canvas: %s håller %zu byte på GPU:n och %zu byte på CPU:n, och allt "
                     "användes i förra bilden eller går inte att vräka — budgeten %zu byte "
                     "hålls inte (Canvas::setMemoryBudget, App::setCanvasMemoryBudget)",
                     id == 0 ? "fönstret utanför ritytorna" : "en rityta", a.charged.gpu,
                     a.charged.cpu + a.fixed, budgetOf(id, a));
        a.overLogged = true;
    }

    // 4. En ny bild — fönstrets konto är öppet i varje.
    ++frame_;
    Account& window = accounts_[0];
    window.drawn    = frame_;
    window.used     = 0;
}

} // namespace glaze::detail
