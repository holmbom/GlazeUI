// Grafikkorten på macOS — listan (`gfx::gpuAdapters`),
// enheten en ny renderare tar (`preferredMetalDevice`: kortet, annars
// GPU-preferensen) och enheten i loggen. Kontraktet står i gfx/platform.h,
// id:ts form i ../gpu_util.h.
//
// LISTAN HAR INGA SIDOEFFEKTER: `MTLCreateSystemDefaultDevice()` anropas
// ALDRIG här — på en Mac med två GPU:er slår anropet om hela systemet till
// den kraftfulla. `isDefault` är därför Apples regel UTAN att fråga (den
// första som varken är isLowPower, isHeadless eller isRemovable, annars den
// första) — UPPSKATTAD, och fel bara på en Mac med automatisk grafikväxling
// avstängd. Glazes `Default` är oförändrat `MTLCreateSystemDefaultDevice()`.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../gpu_util.h"
#include "gfx/platform.h"
#include "metal_backend.h"

namespace gfx {

namespace {

struct MetalCard {
    GpuAdapter    info;
    id<MTLDevice> device;
};

// Tillverkaren ur namnets början — Metal har inga PCI-id.
std::string vendorOf(const std::string& namn)
{
    for (const char* v : {"Apple", "AMD", "Intel", "NVIDIA"})
        if (namn.rfind(v, 0) == 0)
            return v;
    return {};
}

// Korten i MTLCopyAllDevices ordning, med id:n gjorda unika (platsen vid
// dubbletter: `@<location>.<locationNumber>` på en Intel-Mac, annars `#n`).
std::vector<MetalCard> metalCards()
{
    std::vector<MetalCard>   ut;
    std::vector<std::string> plats;
    NSArray<id<MTLDevice>>*  alla = MTLCopyAllDevices();
    for (id<MTLDevice> d in alla) {
        MetalCard k;
        k.device              = d;
        GpuAdapter& g         = k.info;
        g.name                = d.name ? d.name.UTF8String : "";
        g.id                  = "metal:" + g.name;
        g.vendor              = vendorOf(g.name);
        g.kind                = d.isRemovable        ? GpuKind::External
                                : d.hasUnifiedMemory ? GpuKind::Integrated
                                                     : GpuKind::Discrete;
        g.memoryBytes         = d.recommendedMaxWorkingSetSize;
        g.selectable          = !d.isHeadless;
        // Platsen finns bara på en Intel-Mac: med en minsta macOS-version
        // på 27 (bara Apple silicon) är `location` utfasad, och platsen tom
        // ger `#n` vid en dubblett (metal_scene.mm:s villkor för Managed).
#if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && \
    __MAC_OS_X_VERSION_MIN_REQUIRED < 270000
        char buf[48];
        std::snprintf(buf, sizeof buf, "%lu.%lu",
                      static_cast<unsigned long>(d.location),
                      static_cast<unsigned long>(d.locationNumber));
        plats.emplace_back(buf);
#else
        plats.emplace_back();
#endif
        ut.push_back(k);
    }
    auto förval = std::find_if(ut.begin(), ut.end(), [](const MetalCard& k) {
        return !k.device.isLowPower && !k.device.isHeadless &&
               !k.device.isRemovable;
    });
    if (förval == ut.end())
        förval = ut.begin();
    if (förval != ut.end())
        förval->info.isDefault = true;

    std::vector<GpuAdapter> lista;
    for (const MetalCard& k : ut)
        lista.push_back(k.info);
    gpu::disambiguateIds(lista, plats);
    for (std::size_t i = 0; i < ut.size(); ++i)
        ut[i].info.id = lista[i].id;
    return ut;
}

} // namespace

std::vector<GpuAdapter> gpuAdapters()
{
    std::vector<GpuAdapter> ut;
    for (const MetalCard& k : metalCards())
        ut.push_back(k.info);
    return ut;
}

// macOS ärver ingenting av valet till en barnprocess — det görs i
// processen, aldrig i miljön.
std::vector<std::string> childEnvironmentRemovals()
{
    return {};
}

std::string metalAdapterId(id<MTLDevice> d)
{
    if (!d)
        return {};
    for (const MetalCard& k : metalCards())
        if (k.device.registryID == d.registryID)
            return k.info.id;
    return {};
}

// ------------------------------------------------------ GPU-preferensen
//
// Se gfx/platform.h. KORTET (`gpuAdapter()`) vinner när det
// finns och är valbart (inte isHeadless); annars loggas det en gång per id
// och preferensen gäller. Default är systemets förval
// (MTLCreateSystemDefaultDevice — på en Mac med två GPU:er den kraftfulla,
// och det slår om hela systemet till den). HighPerformance tar den första
// enheten som INTE är isLowPower, LowPower den första som ÄR det; en enhet
// utan skärm (isHeadless) presenterar inte och hoppas över. Apple silicon har
// en enda GPU — den är inte isLowPower, så LowPower hittar ingen och förvalet
// gäller, loggat.
id<MTLDevice> preferredMetalDevice()
{
    if (const std::string valt = gpuAdapter(); !valt.empty()) {
        for (const MetalCard& k : metalCards())
            if (k.info.id == valt && k.info.selectable)
                return k.device;
        static std::string loggat;
        if (loggat != valt) {
            loggat = valt;
            log("Metal: kortet %s finns inte eller kan inte väljas — "
                "GPU-preferensen %s gäller",
                valt.c_str(), gpu::preferenceName(gpuPreference()));
        }
    }
    const GpuPreference p = gpuPreference();
    if (p != GpuPreference::Default) {
        const bool wantLow = p == GpuPreference::LowPower;
        NSArray<id<MTLDevice>>* all = MTLCopyAllDevices();
        for (id<MTLDevice> d in all)
            if (d.isLowPower == wantLow && !d.isHeadless)
                return d;
        static bool logged = false;
        if (!logged) {
            logged = true;
            log("Metal: ingen GPU passar preferensen %s — systemets förval "
                "gäller",
                gpu::preferenceName(p));
        }
    }
    return MTLCreateSystemDefaultDevice();
}

// ENHETEN I LOGGEN — vid varje NY enhet (en annan än förra
// renderarens): vilken GPU som ritar, dess id och vad preferensen och kortet
// bad om. En popup på samma enhet loggar ingenting.
void logDevice(id<MTLDevice> d, const std::string& adapterId)
{
    static std::uint64_t senaste = 0;
    static bool          loggad  = false;
    const std::uint64_t  reg     = d.registryID;
    if (loggad && reg == senaste)
        return;
    loggad                 = true;
    senaste                = reg;
    const std::string valt = gpuAdapter();
    log("Metal: ritar med %s [%s] (preferensen %s, kortet %s)",
        d.name ? d.name.UTF8String : "?",
        adapterId.empty() ? "?" : adapterId.c_str(),
        gpu::preferenceName(gpuPreference()),
        valt.empty() ? "inget" : valt.c_str());
}

} // namespace gfx
