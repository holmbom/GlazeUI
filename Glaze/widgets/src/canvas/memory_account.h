#pragma once
// RITYTANS KONTO — INTERN. Medan objektet lever är ritytans konto ÖPPET i
// fönstrets liggare (memory_ledger.h): allt som används genom fönstrets
// resurser — bilder, render-texturer, chunkarnas buffertar, föremålens
// kopior, mål och draperingar — räknas på scenen (id:t, scenen som livstecken
// och dess budget), och scenens CPU-original läggs på kontot. Ritytans
// ritvägar öppnar det; utan bildcache gör objektet ingenting.

#include <cstdint>
#include <memory>

namespace glaze {

struct Ctx;

namespace detail {

struct SceneData;
class MemoryLedger;

class MemoryAccount {
public:
    MemoryAccount(const Ctx& c, const std::shared_ptr<SceneData>& scene);
    ~MemoryAccount();
    MemoryAccount(const MemoryAccount&)            = delete;
    MemoryAccount& operator=(const MemoryAccount&) = delete;

private:
    MemoryLedger* ledger_   = nullptr;
    std::uint64_t previous_ = 0;
};

} // namespace detail
} // namespace glaze
