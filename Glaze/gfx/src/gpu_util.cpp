// Grafikkorten — det backend-neutrala (se gpu_util.h): id:ts form,
// tillverkarnamnen och Linux-valets sysfs-läsning och miljöregel. Delas av
// alla tre backends och mäts headless av tests/gpu_gate.cpp.

#include "gpu_util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>

namespace gfx::gpu {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kGiB = std::uint64_t{1024} * 1024 * 1024;

// Drivrutinerna Mesa driver — bara de kan pekas ut med DRI_PRIME. Ett kort
// med en drivrutin utanför listan är valbart bara som förvalskortet: utan
// Mesa-drivrutin faller DRI_PRIME till llvmpipe (REQ:ens fälla).
bool isMesaDriver(const std::string& d)
{
    static const char* const kMesa[] = {
        "i915",   "xe",    "amdgpu",   "radeon", "nouveau", "virtio_gpu",
        "vmwgfx", "msm",   "panfrost", "panthor", "lima",   "v3d",
        "vc4",    "etnaviv", "asahi"};
    return std::any_of(std::begin(kMesa), std::end(kMesa),
                       [&](const char* m) { return d == m; });
}

// SoC:ernas drivrutiner — en GPU i samma kisel som processorn.
bool isSocDriver(const std::string& d)
{
    static const char* const kSoc[] = {"msm",  "panfrost", "panthor", "lima",
                                       "v3d",  "vc4",      "etnaviv", "asahi"};
    return std::any_of(std::begin(kSoc), std::end(kSoc),
                       [&](const char* m) { return d == m; });
}

std::string trim(std::string s)
{
    const auto ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while (!s.empty() && ws(s.back()))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && ws(s[i]))
        ++i;
    return s.substr(i);
}

// Första raden i en sysfs-fil, trimmad — tom när filen saknas.
std::string readLine(const fs::path& p)
{
    std::ifstream in(p);
    std::string   rad;
    if (!in || !std::getline(in, rad))
        return {};
    return trim(rad);
}

// Ett tal i sysfs ("0x10de", "8589934592"); 0 när filen saknas.
std::uint64_t readNumber(const fs::path& p, int base)
{
    const std::string s = readLine(p);
    return s.empty() ? 0 : std::strtoull(s.c_str(), nullptr, base);
}

// `device/uevent`: NYCKEL=värde per rad.
std::map<std::string, std::string> readUevent(const fs::path& p)
{
    std::map<std::string, std::string> ut;
    std::ifstream                      in(p);
    std::string                        rad;
    while (in && std::getline(in, rad)) {
        const std::size_t eq = rad.find('=');
        if (eq != std::string::npos)
            ut[rad.substr(0, eq)] = trim(rad.substr(eq + 1));
    }
    return ut;
}

// Basnamnet av en symlänks mål — sysfs reserv när uevent saknar fältet.
std::string linkBase(const fs::path& p)
{
    std::error_code ec;
    const fs::path  mål = fs::canonical(p, ec);
    return ec ? std::string() : mål.filename().string();
}

bool hasRenderNode(const fs::path& dev)
{
    std::error_code ec;
    for (fs::directory_iterator it(dev / "drm", ec), slut; !ec && it != slut;
         it.increment(ec))
        if (it->path().filename().string().rfind("renderD", 0) == 0)
            return true;
    return false;
}

bool isCardName(const std::string& n)
{
    if (n.size() <= 4 || n.compare(0, 4, "card") != 0)
        return false;
    return std::all_of(n.begin() + 4, n.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

// NVIDIA:s egen modellsträng — `Model:` i drivrutinens `information`.
std::string nvidiaModel(const DrmPaths& paths, const std::string& slot)
{
    std::ifstream in(fs::path(paths.nvidiaProc) / slot / "information");
    std::string   rad;
    while (in && std::getline(in, rad))
        if (rad.compare(0, 6, "Model:") == 0)
            return trim(rad.substr(6));
    return {};
}

// Enhetens namn ur pci.ids: tillverkarens block ("10de  NVIDIA …") och i det
// enhetsraden ("\t2560  GA106M …"). Den första filen som finns gäller.
std::string pciIdsName(const DrmPaths& paths, std::uint32_t vendor,
                       std::uint32_t device)
{
    char tv[8], te[8];
    std::snprintf(tv, sizeof tv, "%04x", unsigned(vendor & 0xFFFFu));
    std::snprintf(te, sizeof te, "%04x", unsigned(device & 0xFFFFu));
    for (const std::string& fil : paths.pciIds) {
        std::ifstream in(fil);
        if (!in)
            continue;
        std::string rad;
        bool        iBlock = false;
        while (std::getline(in, rad)) {
            if (rad.empty() || rad[0] == '#')
                continue;
            if (rad[0] != '\t') {
                if (iBlock)
                    break;   // nästa tillverkare — enheten finns inte
                iBlock = rad.size() > 4 && rad.compare(0, 4, tv) == 0 &&
                         (rad[4] == ' ' || rad[4] == '\t');
            } else if (iBlock && rad.size() > 6 && rad[1] != '\t' &&
                       rad.compare(1, 4, te) == 0) {
                return trim(rad.substr(5));
            }
        }
        return {};
    }
    return {};
}

GpuKind kindOf(const std::string& driver, const std::string& removable,
               std::uint64_t vram)
{
    if (removable == "removable")
        return GpuKind::External;
    if (driver == "nvidia" || driver == "nouveau")
        return GpuKind::Discrete;
    if (driver == "amdgpu")
        return vram >= kGiB ? GpuKind::Discrete : GpuKind::Integrated;
    if (driver == "i915" || driver == "xe" || isSocDriver(driver))
        return GpuKind::Integrated;
    return GpuKind::Unknown;
}

// "0000:01:00.0" → "01:00.0" — samma form som Windows D3DKMT-adress.
std::string busAddress(const std::string& slot)
{
    return std::count(slot.begin(), slot.end(), ':') == 2 ? slot.substr(5)
                                                         : slot;
}

int defaultIndex(const std::vector<DrmCard>& cards)
{
    for (std::size_t i = 0; i < cards.size(); ++i)
        if (cards[i].a.isDefault)
            return static_cast<int>(i);
    return -1;
}

using EnvSet = std::vector<std::pair<std::string, std::string>>;

// Det som måste sättas för att rita på kort `i` (regel 3–6 i gpu_util.h);
// nullopt = det går inte, och `why` säger varför.
std::optional<EnvSet> envFor(const std::vector<DrmCard>& cards, int i,
                             bool uttryckligt, std::string& why)
{
    const DrmCard& c   = cards[static_cast<std::size_t>(i)];
    const int      def = defaultIndex(cards);
    if (!uttryckligt && i == def)
        return EnvSet{};
    if (c.pciSlot.empty()) {
        if (i == def)
            return EnvSet{};
        why = "inte ett PCI-kort och inte förvalskortet";
        return std::nullopt;
    }
    if (c.driver == "nvidia") {
        const auto nv = std::count_if(cards.begin(), cards.end(),
                                      [](const DrmCard& k) {
                                          return k.driver == "nvidia";
                                      });
        if (nv > 1) {
            why = "flera NVIDIA-kort — valet mellan dem kräver X:s "
                  "providernamn";
            return std::nullopt;
        }
        if (cards.size() == 1)
            return EnvSet{};
        return EnvSet{{"__NV_PRIME_RENDER_OFFLOAD", "1"}};
    }
    if (isMesaDriver(c.driver)) {
        // Förvalskortet är UPPSKATTAT (boot_vga), och kompositorn kan köra
        // på ett Mesa-kort fast boot_vga-kortet drivs av NVIDIA. Ett
        // uttryckligt val stoppas därför aldrig av det; bara
        // preferensen undviker Mesa-kort bakom ett NVIDIA-förval.
        if (!uttryckligt && def >= 0 &&
            cards[static_cast<std::size_t>(def)].driver == "nvidia") {
            why = "förvalskortet drivs av NVIDIA:s drivrutin — preferensen "
                  "väljer inte ett Mesa-kort förbi den";
            return std::nullopt;
        }
        std::string v = "pci-" + c.pciSlot;
        std::replace(v.begin(), v.end(), ':', '_');
        std::replace(v.begin(), v.end(), '.', '_');
        return EnvSet{{"DRI_PRIME", v}};
    }
    if (i == def)
        return EnvSet{};
    why = "drivrutinen " + (c.driver.empty() ? std::string("?") : c.driver) +
          " är okänd för Mesa";
    return std::nullopt;
}

// Regel 1: användarens variabel, som "NAMN=värde" — tom när ingen är satt.
std::string userVariable(const GetEnv& getenv)
{
    for (const char* namn : primeVariables())
        if (const char* v = getenv ? getenv(namn) : nullptr; v && *v)
            return std::string(namn) + "=" + v;
    return {};
}

const GpuKind kHögOrdning[] = {GpuKind::External, GpuKind::Discrete,
                               GpuKind::Unknown, GpuKind::Integrated};
const GpuKind kLågOrdning[] = {GpuKind::Integrated, GpuKind::Discrete,
                               GpuKind::External};

} // namespace

std::string vendorName(std::uint32_t vendorId)
{
    switch (vendorId) {
    case 0x10de: return "NVIDIA";
    case 0x1002:
    case 0x1022: return "AMD";
    case 0x8086: return "Intel";
    case 0x1414: return "Microsoft";
    case 0x106b: return "Apple";
    case 0x5143: return "Qualcomm";
    case 0x13b5: return "ARM";
    case 0x15ad: return "VMware";
    case 0x1af4: return "Red Hat";
    case 0x80ee: return "Oracle";
    default:     return {};
    }
}

const char* preferenceName(GpuPreference p)
{
    return p == GpuPreference::HighPerformance ? "HighPerformance"
           : p == GpuPreference::LowPower      ? "LowPower"
                                               : "Default";
}

const char* kindName(GpuKind k)
{
    switch (k) {
    case GpuKind::Integrated: return "integrerat";
    case GpuKind::Discrete:   return "diskret";
    case GpuKind::External:   return "externt";
    case GpuKind::Software:   return "programvara";
    case GpuKind::Unknown:    break;
    }
    return "okänt slag";
}

std::string pciAdapterId(std::uint32_t vendor, std::uint32_t device,
                         std::uint32_t subsys, std::uint32_t revision)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "pci:%04x:%04x:%08lx:%02x",
                  unsigned(vendor & 0xFFFFu), unsigned(device & 0xFFFFu),
                  static_cast<unsigned long>(subsys),
                  unsigned(revision & 0xFFu));
    return buf;
}

void disambiguateIds(std::vector<GpuAdapter>& list,
                     const std::vector<std::string>& where)
{
    std::vector<std::string> original;
    original.reserve(list.size());
    for (const GpuAdapter& a : list)
        original.push_back(a.id);
    std::map<std::string, int> löpnummer;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (std::count(original.begin(), original.end(), original[i]) < 2)
            continue;
        const std::string& w = i < where.size() ? where[i] : std::string();
        const int          n = löpnummer[original[i]]++;
        list[i].id += w.empty() ? "#" + std::to_string(n) : "@" + w;
    }
}

std::vector<DrmCard> scanDrmCards(const DrmPaths& paths)
{
    std::vector<DrmCard> ut;
    std::error_code      ec;
    for (fs::directory_iterator it(paths.sysClassDrm, ec), slut;
         !ec && it != slut; it.increment(ec)) {
        const std::string namn = it->path().filename().string();
        const fs::path    dev  = it->path() / "device";
        if (!isCardName(namn) || !hasRenderNode(dev))
            continue;
        DrmCard c;
        c.card                = namn;
        const auto    ue      = readUevent(dev / "uevent");
        const auto    drv     = ue.find("DRIVER");
        c.driver              = drv != ue.end() ? drv->second
                                                : linkBase(dev / "driver");
        const auto    vendor  = static_cast<std::uint32_t>(
            readNumber(dev / "vendor", 16));
        if (vendor == 0) {   // ett icke-PCI-kort (en SoC)
            const std::string bas = linkBase(dev);
            c.a.id   = "drm:" + c.driver + "@" + (bas.empty() ? namn : bas);
            c.a.name = c.driver;
            c.a.kind = GpuKind::Integrated;
            ut.push_back(std::move(c));
            continue;
        }
        const auto slot = ue.find("PCI_SLOT_NAME");
        c.pciSlot = slot != ue.end() ? slot->second : linkBase(dev);
        const auto device =
            static_cast<std::uint32_t>(readNumber(dev / "device", 16));
        const auto subsys = static_cast<std::uint32_t>(
            (readNumber(dev / "subsystem_device", 16) << 16) |
            readNumber(dev / "subsystem_vendor", 16));
        const auto rev =
            static_cast<std::uint32_t>(readNumber(dev / "revision", 16));
        const std::uint64_t vram = readNumber(dev / "mem_info_vram_total", 10);
        c.bootVga       = readLine(dev / "boot_vga") == "1";
        c.a.id          = pciAdapterId(vendor, device, subsys, rev);
        c.a.vendorId    = vendor;
        c.a.deviceId    = device;
        c.a.vendor      = vendorName(vendor);
        c.a.memoryBytes = vram;
        c.a.kind = kindOf(c.driver, readLine(dev / "removable"), vram);
        c.a.name = nvidiaModel(paths, c.pciSlot);
        if (c.a.name.empty())
            c.a.name = pciIdsName(paths, vendor, device);
        if (c.a.name.empty()) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "PCI %04x:%04x", unsigned(vendor),
                          unsigned(device));
            c.a.name = buf;
        }
        ut.push_back(std::move(c));
    }
    // PCI-ordning, icke-PCI sist (och där kortnumret).
    std::sort(ut.begin(), ut.end(), [](const DrmCard& x, const DrmCard& y) {
        if (x.pciSlot.empty() != y.pciSlot.empty())
            return y.pciSlot.empty();
        if (x.pciSlot != y.pciSlot)
            return x.pciSlot < y.pciSlot;
        return std::stoi(x.card.substr(4)) < std::stoi(y.card.substr(4));
    });
    const auto boot = std::find_if(ut.begin(), ut.end(),
                                   [](const DrmCard& c) { return c.bootVga; });
    if (boot != ut.end())
        boot->a.isDefault = true;
    else if (!ut.empty())
        ut.front().a.isDefault = true;

    std::vector<GpuAdapter>  lista;
    std::vector<std::string> var;
    for (const DrmCard& c : ut) {
        lista.push_back(c.a);
        var.push_back(busAddress(c.pciSlot));
    }
    disambiguateIds(lista, var);
    for (std::size_t i = 0; i < ut.size(); ++i)
        ut[i].a.id = lista[i].id;
    return ut;
}

const std::vector<const char*>& primeVariables()
{
    static const std::vector<const char*> kNamn = {
        "DRI_PRIME",
        "__NV_PRIME_RENDER_OFFLOAD",
        "__NV_PRIME_RENDER_OFFLOAD_PROVIDER",
        "__GLX_VENDOR_LIBRARY_NAME",
        "__EGL_VENDOR_LIBRARY_FILENAMES",
        "__EGL_VENDOR_LIBRARY_DIRS",
        "LIBGL_ALWAYS_SOFTWARE"};
    return kNamn;
}

std::vector<std::pair<std::string, std::string>>
applyPrimeEnvironment(const PrimeEnv& env, const GetEnv& getenv,
                      const SetEnv& setenv)
{
    std::vector<std::pair<std::string, std::string>> satta;
    for (const auto& [namn, värde] : env.set) {
        const char* nu = getenv ? getenv(namn.c_str()) : nullptr;
        if (nu && *nu)
            continue;   // användarens — skrivs aldrig över
        if (setenv && setenv(namn.c_str(), värde.c_str()))
            satta.emplace_back(namn, värde);
    }
    return satta;
}

void markSelectable(std::vector<DrmCard>& cards, const GetEnv& getenv)
{
    const bool låst = !userVariable(getenv).empty();
    for (std::size_t i = 0; i < cards.size(); ++i) {
        std::string why;
        cards[i].a.selectable =
            !låst && envFor(cards, static_cast<int>(i), true, why).has_value();
    }
}

PrimeEnv primeEnvironment(const std::vector<DrmCard>& cards,
                          const std::string& wantedId, GpuPreference pref,
                          const GetEnv& getenv)
{
    PrimeEnv ut;
    if (const std::string user = userVariable(getenv); !user.empty()) {
        ut.reason = "användarens miljö gäller (" + user + ")";
        return ut;
    }
    // Valbarheten FÖRST: ett kort som inte går att nå är som ett
    // som saknas, och preferensen väljer bara bland de valbara.
    // `valbar` är det uttryckliga valets regel, `kandidat` preferensens.
    std::vector<std::string> varför(cards.size());
    std::vector<bool>        valbar(cards.size()), kandidat(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        std::string ingen;
        valbar[i] =
            envFor(cards, static_cast<int>(i), true, varför[i]).has_value();
        kandidat[i] =
            envFor(cards, static_cast<int>(i), false, ingen).has_value();
    }

    int  mål         = -1;
    bool uttryckligt = false;
    if (!wantedId.empty()) {
        const auto it = std::find_if(cards.begin(), cards.end(),
                                     [&](const DrmCard& c) {
                                         return c.a.id == wantedId;
                                     });
        const auto i = static_cast<std::size_t>(it - cards.begin());
        if (it == cards.end())
            ut.reason = "kortet " + wantedId + " finns inte";
        else if (!valbar[i])
            ut.reason = "kortet " + wantedId + " kan inte väljas här (" +
                        varför[i] + ")";
        else {
            mål         = static_cast<int>(i);
            uttryckligt = true;
        }
    }
    if (mål < 0 && pref != GpuPreference::Default) {
        const bool hög = pref == GpuPreference::HighPerformance;
        const GpuKind* b = hög ? std::begin(kHögOrdning) : std::begin(kLågOrdning);
        const GpuKind* e = hög ? std::end(kHögOrdning) : std::end(kLågOrdning);
        for (const GpuKind* k = b; k != e && mål < 0; ++k)
            for (std::size_t i = 0; i < cards.size() && mål < 0; ++i)
                if (valbar[i] && kandidat[i] && cards[i].a.kind == *k)
                    mål = static_cast<int>(i);
        if (mål < 0)
            ut.reason += std::string(ut.reason.empty() ? "" : "; ") +
                         "inget valbart kort passar preferensen " +
                         preferenceName(pref);
    }
    if (mål < 0) {
        if (ut.reason.empty())
            ut.reason = "Default — Mesas och drivrutinens val";
        return ut;
    }
    std::string why;
    ut.chosen = mål;
    ut.set    = *envFor(cards, mål, uttryckligt, why);
    if (!uttryckligt && cards[static_cast<std::size_t>(mål)].a.isDefault)
        ut.reason += std::string(ut.reason.empty() ? "" : "; ") +
                     "preferensens kort är förvalskortet";
    return ut;
}

} // namespace gfx::gpu
