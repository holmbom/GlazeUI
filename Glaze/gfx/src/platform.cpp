// Backend-neutrala delar av plattformsfasaden — delas av alla backends
// (backend_d3d11/backend_metal/backend_linux).

#include <algorithm>
#include <chrono>
#include <cmath>

#include "gfx/platform.h"
#include "icon_util.h"

namespace gfx {

namespace {
// Användarskalan — app-global som temat. Enkeltrådad som
// resten av fasaden (pump + settrar bor i huvudtråden).
float g_userScale = 1.0f;

// GPU-preferensen — läses av backenden när ett fönster
// skapar sin renderare. UI-trådens, som användarskalan.
GpuPreference g_gpuPreference = GpuPreference::Default;

// Det UTTRYCKLIGA KORTET — `GpuAdapter::id`, tomt = inget.
// Läses där preferensen läses. UI-trådens, som preferensen.
std::string g_gpuAdapter;

// APPENS ID — tom = förvalet, programfilens namn. Läses av
// Linux-backenden när ett fönster skapas. UI-trådens, som användarskalan.
std::string g_appId;

// DOCKANS BEGÄRAN — `showInDock()` svarar med den, och
// macOS-backenden läser den i init. UI-trådens, som användarskalan.
bool g_showInDock = true;

// Tecknen i en `.desktop`-fils namn utan `.desktop` — mängden fasaden lovar.
constexpr std::size_t kAppIdMax = 255;

bool appIdChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}
} // namespace

void setAppId(const std::string& id)
{
    if (id.size() > kAppIdMax ||
        !std::all_of(id.begin(), id.end(), appIdChar)) {
        log("setAppId: ogiltigt id ignoreras (\"%s\") — tillåtet är "
            "[A-Za-z0-9._-], högst 255 tecken",
            id.c_str());
        return;
    }
    g_appId = id;
}

// Förvalet är programfilens namn, och det NORMALISERAS till samma mängd som
// sättaren kräver: varje byte utanför `[A-Za-z0-9._-]` blir '_' (ett UTF-8-
// tecken alltså ett '_' per byte) och namnet kapas vid 255. Ett namn som inte
// går att läsa ger "glazeui" — ett tomt app_id är ingen identitet alls.
std::string appId()
{
    if (!g_appId.empty())
        return g_appId;
    std::string namn = programName();
    for (char& c : namn)
        if (!appIdChar(c))
            c = '_';
    if (namn.size() > kAppIdMax)
        namn.resize(kAppIdMax);
    return namn.empty() ? std::string("glazeui") : namn;
}

// Begäran lagras här, och backenden gör sin del i `applyShowInDock`
// (icon_util.h): macOS byter aktiveringspolicy, Windows och Linux loggar.
void setShowInDock(bool show)
{
    g_showInDock = show;
    applyShowInDock(show);
}

bool showInDock()
{
    return g_showInDock;
}

void setGpuPreference(GpuPreference p)
{
    g_gpuPreference = p;
}

GpuPreference gpuPreference()
{
    return g_gpuPreference;
}

void setGpuAdapter(const std::string& id)
{
    g_gpuAdapter = id;
}

std::string gpuAdapter()
{
    return g_gpuAdapter;
}

float userScale()
{
    return g_userScale;
}

void setUserScale(float s)
{
    if (!std::isfinite(s) || s <= 0.0f) {
        log("setUserScale: ogiltigt värde ignoreras (%f)",
            static_cast<double>(s));
        return;
    }
    // Intervall 0.5–2.0, klampas + loggas.
    float klampad = s;
    if (klampad < 0.5f) klampad = 0.5f;
    if (klampad > 2.0f) klampad = 2.0f;
    if (klampad != s)
        log("setUserScale: %g klampas till %g (intervall 0.5–2.0)",
            static_cast<double>(s), static_cast<double>(klampad));
    g_userScale = klampad;
}

std::uint64_t ticksMs()
{
    using namespace std::chrono;
    // Monoton klocka med process-lokal nollpunkt — samma semantik som
    // "ms sedan start" (används för animationer, inte väggtid).
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}

} // namespace gfx
