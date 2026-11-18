// RITYTANS KONTO i fönstrets liggare (memory_account.h).

#include "memory_account.h"

#include "../image_cache.h"
#include "glaze/widget.h"
#include "scene.h"

namespace glaze::detail {

MemoryAccount::MemoryAccount(const Ctx& c, const std::shared_ptr<SceneData>& scene)
    : ledger_(scene && c.images ? &c.images->ledger() : nullptr)
{
    if (!ledger_)
        return;
    previous_ = ledger_->beginAccount(scene->id, std::weak_ptr<const void>(scene->life),
                                      scene->memoryBudget);
    // CPU-originalen räknas på kontot bredvid GPU-posterna.
    ledger_->setAccountCpu(scene->id, sceneCpuBytes(*scene));
}

MemoryAccount::~MemoryAccount()
{
    if (ledger_)
        ledger_->endAccount(previous_);
}

} // namespace glaze::detail
