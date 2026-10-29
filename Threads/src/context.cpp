// glaze_threads — kontexterna i kärnan: pooltråden själv och den
// registrerade defaulten. UI-bryggan bor i ui.cpp (eget target).

#include <atomic>

#include "glaze_threads/context.h"

namespace glaze::threads {
namespace {

// Fortsättningen körs där den anropas — i pooltråden. `runsInline()` säger
// det till `Worker`, som då anropar fortsättningen själv, utanför sina lås.
class InlineContext final : public Context {
public:
    void post(std::function<void()> fn) override { fn(); }
    bool runsInline() const override { return true; }
};

// KONSTANT-initierad (constexpr-konstruktor) — alltså satt till nullptr
// innan någon dynamisk initiering körs, och därmed säker mot UI-bryggans
// statiska registrering oavsett i vilken ordning TU:erna initieras.
std::atomic<Context*> g_default{nullptr};

} // namespace

Context& workerContext()
{
    static InlineContext kontext;
    return kontext;
}

Context& defaultContext()
{
    Context* c = g_default.load(std::memory_order_acquire);
    return c ? *c : workerContext();
}

void setDefaultContext(Context* context)
{
    g_default.store(context, std::memory_order_release);
}

} // namespace glaze::threads
