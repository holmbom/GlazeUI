// gpu_gate — GRAFIKKORTEN, det backend-neutrala:
// id:ts form, dubbletterna, tillverkarnamnen och LINUX-VALET — sysfs-läsningen
// mot ett FALSKT träd och miljöregeln mot en falsk `getenv` — i
// `Glaze/gfx/src/gpu_util.h`. Headless på alla tre OS: logiken kompileras i
// varje backend just för att den här grinden ska nå den.
//
// Kör:  ./build/tests/gpu_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// FÖRVÄNTAT VÄRDE ÄR HANDSKRIVET — id-strängarna, namnen och variablerna står som
// literaler här, aldrig räknade med produktionens funktioner:
//   G1 `pciAdapterId` — tillverkare, enhet, subsystem och revision i gemen hex
//   G2 dubbletterna: två lika id får `@01:00.0`/`@02:00.0`, ett unikt rörs
//      inte, och utan adress blir det `#0`/`#1`
//   G3 tillverkarnamnen (två AMD-id, ett okänt ger tomt)
//   G4 sysfs mot ett falskt träd i en temporär katalog: card0 (i915,
//      boot_vga), card1 (nvidia), anslutningen card1-HDMI-A-1 och simpledrm
//      utan renderD (båda ignoreras), NVIDIA:s `information` och en pci.ids
//      (på Windows utan `information` — katalognamnet har kolon — så där
//      mäts reserven pci.ids i stället)
//   G5 miljöregeln: a) Intel + NVIDIA med HighPerformance ger exakt
//      __NV_PRIME_RENDER_OFFLOAD=1 · b) Intel + amdgpu ger DRI_PRIME med `_`
//      · c) preferensens mål är förvalskortet: ingenting · c2) ett
//      UTTRYCKLIGT förvalskort sätter ändå DRI_PRIME · d) en satt
//      DRI_PRIME vinner · e) ett okänt id: preferensen, och orsaken säger det
//      · f) förvalet NVIDIA, LowPower: ingenting, men Intel är valbart
//      · g) två NVIDIA utöver Intel: ingenting · h) Default utan id:
//      ingenting · i) ett id som finns men inte är valbart behandlas som
//      saknat · j) en TOM variabel är osatt · f2) förvalet NVIDIA: ett UTTRYCKLIGT Mesa-kort
//      sätter ändå DRI_PRIME (boot_vga är bara uppskattat)
//   G6 TILLÄMPNINGEN (`applyPrimeEnvironment`) mot en falsk miljö: en tom
//      variabel skrivs över, en icke-tom aldrig, en vägrad setenv räknas inte
//
// VAD DEN INTE MÄTER: att Mesa och libglvnd faktiskt läser variablerna, att
// den riktiga sysfs-läsningen ser ut som det falska trädet (symlänkarna — här
// är uevent-filen vägen), EGL-frågan (`eglAdapterId`), Windows D3DKMT och
// proben, och Metal — de står i scene3d_pixel_gate K2–K4 (riktig backend)
// och i doc/os/linux/TESTS.md och doc/os/macos/TESTS.md.
//
// SABOTAGEN den ska fälla: DRI_PRIME för ett NVIDIA-mål
// (G5a) · användarens variabel ignorerad (G5d) · anslutningskatalogen
// räknad som kort (G4) · DRI_PRIME med `:` kvar (G5b) · id:t med namnet i
// stället för subsystemet (G1) · valbarheten efter preferensen (G5i) ·
// förvalets drivrutin som stoppar ett uttryckligt val (G5f/G5f2) · en tom
// variabel som inte skrivs över (G6a).
//
// Att `linux_gpu.cpp` verkligen anropar `applyPrimeEnvironment` med
// `setenv(…, 1)` mäts inte här (Linux-only, TESTS-raden).

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "gpu_util.h"   // Glaze/gfx/src — det backend-neutrala

namespace {

namespace fs  = std::filesystem;
namespace gpu = gfx::gpu;

int  g_fails   = 0;
int  g_cases   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

using Env = std::vector<std::pair<std::string, std::string>>;

std::string text(const Env& e)
{
    std::string ut;
    for (const auto& [n, v] : e)
        ut += (ut.empty() ? "" : " ") + n + "=" + v;
    return "{" + ut + "}";
}

// ---------------------------------------------------------------- G1–G3

void fallId()
{
    check(gpu::pciAdapterId(0x10de, 0x2560, 0x0a8c1028, 0xa1) ==
              "pci:10de:2560:0a8c1028:a1",
          "G1 pciAdapterId", gpu::pciAdapterId(0x10de, 0x2560, 0x0a8c1028, 0xa1));

    std::vector<gfx::GpuAdapter> l(3);
    l[0].id = "pci:a";
    l[1].id = "pci:b";
    l[2].id = "pci:a";
    gpu::disambiguateIds(l, {"01:00.0", "03:00.0", "02:00.0"});
    check(l[0].id == "pci:a@01:00.0" && l[2].id == "pci:a@02:00.0",
          "G2 dubbletterna får sin adress", l[0].id + " " + l[2].id);
    check(l[1].id == "pci:b", "G2 ett unikt id rörs inte", l[1].id);
    std::vector<gfx::GpuAdapter> u(2);
    u[0].id = u[1].id = "metal:X";
    gpu::disambiguateIds(u, {"", ""});
    check(u[0].id == "metal:X#0" && u[1].id == "metal:X#1",
          "G2 utan adress: #0 och #1", u[0].id + " " + u[1].id);

    check(gpu::vendorName(0x10de) == "NVIDIA" && gpu::vendorName(0x1002) == "AMD" &&
              gpu::vendorName(0x1022) == "AMD" && gpu::vendorName(0x8086) == "Intel" &&
              gpu::vendorName(0x1414) == "Microsoft" && gpu::vendorName(0x1234).empty(),
          "G3 tillverkarnamnen");
}

// ------------------------------------------------------------------- G4

void skriv(const fs::path& p, const std::string& innehåll)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << innehåll;
}

// Ett PCI-kort i det falska trädet: `device/` med filerna och `device/drm/`
// med noderna.
void pciKort(const fs::path& drm, const std::string& namn,
             const std::map<std::string, std::string>& filer,
             const std::vector<std::string>& noder)
{
    for (const auto& [f, v] : filer)
        skriv(drm / namn / "device" / f, v);
    for (const std::string& n : noder)
        fs::create_directories(drm / namn / "device" / "drm" / n);
}

void fallSysfs()
{
    const fs::path rot =
        fs::temp_directory_path() /
        ("gpu_gate_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path drm = rot / "class" / "drm";
    pciKort(drm, "card0",
            {{"vendor", "0x8086\n"}, {"device", "0x9a49\n"},
             {"subsystem_vendor", "0x1028\n"}, {"subsystem_device", "0x0a8c\n"},
             {"revision", "0x01\n"}, {"class", "0x030000\n"}, {"boot_vga", "1\n"},
             {"uevent", "DRIVER=i915\nPCI_CLASS=30000\nPCI_SLOT_NAME=0000:00:02.0\n"}},
            {"card0", "renderD128"});
    pciKort(drm, "card1",
            {{"vendor", "0x10de\n"}, {"device", "0x2560\n"},
             {"subsystem_vendor", "0x1028\n"}, {"subsystem_device", "0x0a8c\n"},
             {"revision", "0xa1\n"}, {"class", "0x030200\n"},
             {"uevent", "DRIVER=nvidia\nPCI_CLASS=30200\nPCI_SLOT_NAME=0000:01:00.0\n"}},
            {"card1", "renderD129"});
    // Anslutningen ser ut som ett kort i allt utom namnet — sabotaget som
    // räknar den får tre.
    pciKort(drm, "card1-HDMI-A-1",
            {{"vendor", "0x10de\n"}, {"device", "0x2560\n"},
             {"uevent", "DRIVER=nvidia\nPCI_SLOT_NAME=0000:01:00.0\n"}},
            {"renderD130"});
    pciKort(drm, "card2", {{"uevent", "DRIVER=simple-framebuffer\n"}}, {"card2"});
    // NVIDIA:s katalog heter som PCI-adressen, och kolon är inte tillåtet i ett
    // filnamn på Windows — där saknas filen, och namnet kommer ur pci.ids.
#ifndef _WIN32
    skriv(rot / "nvidia" / "0000:01:00.0" / "information",
          "Model: \t\t NVIDIA GeForce RTX 3060 Laptop GPU\nIRQ:   150\n");
#endif
    skriv(rot / "pci.ids", "# pci.ids — grindens\n"
                           "8086  Intel Corporation\n"
                           "\t9a40  Annan enhet\n"
                           "\t9a49  TigerLake-LP GT2 [Iris Xe Graphics]\n"
                           "\t\t1028 0a8c  Dells variant\n"
                           "10de  NVIDIA Corporation\n"
                           "\t2560  GA106M [GeForce RTX 3060 Mobile / Max-Q]\n");

    gpu::DrmPaths p;
    p.sysClassDrm = (rot / "class" / "drm").string();
    p.nvidiaProc  = (rot / "nvidia").string();
    p.pciIds      = {(rot / "saknas.ids").string(), (rot / "pci.ids").string()};
    std::vector<gpu::DrmCard> k = gpu::scanDrmCards(p);
    gpu::markSelectable(k, [](const char*) -> const char* { return nullptr; });

    check(k.size() == 2, "G4 två kort (anslutningen och simpledrm ignoreras)",
          std::to_string(k.size()));
    if (k.size() == 2) {
        check(k[0].card == "card0" && k[0].driver == "i915" &&
                  k[0].pciSlot == "0000:00:02.0",
              "G4 card0: i915 på 0000:00:02.0", k[0].card + " " + k[0].driver);
        check(k[0].a.id == "pci:8086:9a49:0a8c1028:01", "G4 card0:s id", k[0].a.id);
        check(k[0].a.name == "TigerLake-LP GT2 [Iris Xe Graphics]",
              "G4 card0:s namn ur pci.ids (andra filen, rätt enhet)", k[0].a.name);
        check(k[0].a.kind == gfx::GpuKind::Integrated && k[0].a.isDefault &&
                  k[0].a.vendor == "Intel",
              "G4 card0: integrerat, förval, Intel");
        check(k[1].a.id == "pci:10de:2560:0a8c1028:a1" && k[1].driver == "nvidia",
              "G4 card1:s id och drivrutin", k[1].a.id);
#ifndef _WIN32
        check(k[1].a.name == "NVIDIA GeForce RTX 3060 Laptop GPU",
              "G4 card1:s namn ur NVIDIA:s information före pci.ids", k[1].a.name);
#else
        check(k[1].a.name == "GA106M [GeForce RTX 3060 Mobile / Max-Q]",
              "G4 card1:s namn ur pci.ids utan NVIDIA:s information", k[1].a.name);
#endif
        check(k[1].a.kind == gfx::GpuKind::Discrete && !k[1].a.isDefault,
              "G4 card1: diskret, inte förval");
        check(k[0].a.selectable && k[1].a.selectable, "G4 båda valbara");
    }
    std::error_code ec;
    fs::remove_all(rot, ec);
}

// ------------------------------------------------------------------- G5

gpu::DrmCard kort(const std::string& id, const std::string& slot,
                  const std::string& driver, gfx::GpuKind kind, bool boot)
{
    gpu::DrmCard c;
    c.a.id        = id;
    c.a.kind      = kind;
    c.a.isDefault = boot;
    c.card        = "card" + id;
    c.pciSlot     = slot;
    c.driver      = driver;
    c.bootVga     = boot;
    return c;
}

const char* ingen(const char*) { return nullptr; }

void fallMiljö()
{
    using K            = gfx::GpuKind;
    using P            = gfx::GpuPreference;
    const auto intel   = kort("intel", "0000:00:02.0", "i915", K::Integrated, true);
    const auto nv      = kort("nv", "0000:01:00.0", "nvidia", K::Discrete, false);
    const auto amd     = kort("amd", "0000:03:00.0", "amdgpu", K::Discrete, false);
    const Env  offload = {{"__NV_PRIME_RENDER_OFFLOAD", "1"}};

    gpu::PrimeEnv e = gpu::primeEnvironment({intel, nv}, "", P::HighPerformance, ingen);
    check(e.set == offload && e.chosen == 1,
          "G5a Intel + NVIDIA, HighPerformance: bara __NV_PRIME_RENDER_OFFLOAD=1",
          text(e.set));

    e = gpu::primeEnvironment({intel, amd}, "", P::HighPerformance, ingen);
    check(e.set == Env{{"DRI_PRIME", "pci-0000_03_00_0"}},
          "G5b Intel + amdgpu: DRI_PRIME=pci-0000_03_00_0", text(e.set));

    e = gpu::primeEnvironment({intel, nv}, "", P::LowPower, ingen);
    check(e.set.empty() && e.chosen == 0,
          "G5c preferensens mål är förvalskortet: ingenting", text(e.set));

    e = gpu::primeEnvironment({intel, amd}, "intel", P::Default, ingen);
    check(e.set == Env{{"DRI_PRIME", "pci-0000_00_02_0"}} && e.chosen == 0,
          "G5c2 ett uttryckligt förvalskort sätter ändå DRI_PRIME", text(e.set));

    e = gpu::primeEnvironment({intel, nv}, "nv", P::HighPerformance,
                              [](const char* n) -> const char* {
                                  return std::strcmp(n, "DRI_PRIME") == 0 ? "1" : nullptr;
                              });
    check(e.set.empty() && e.chosen == -1 &&
              e.reason.find("användarens") != std::string::npos,
          "G5d en satt DRI_PRIME vinner helt", text(e.set) + " " + e.reason);

    e = gpu::primeEnvironment({intel, nv}, "pci:ffff:ffff:00000000:00",
                              P::HighPerformance, ingen);
    check(e.set == offload && e.reason.find("finns inte") != std::string::npos,
          "G5e ett okänt id: preferensen gäller, och orsaken säger det",
          text(e.set) + " " + e.reason);

    const auto nvBoot = kort("nv", "0000:01:00.0", "nvidia", K::Discrete, true);
    const auto intel2 = kort("intel", "0000:00:02.0", "i915", K::Integrated, false);
    e = gpu::primeEnvironment({intel2, nvBoot}, "", P::LowPower, ingen);
    std::vector<gpu::DrmCard> f = {intel2, nvBoot};
    gpu::markSelectable(f, ingen);
    check(e.set.empty() && f[0].a.selectable,
          "G5f förvalet NVIDIA, LowPower utan id: ingenting — men Intel är "
          "valbart",
          text(e.set));
    e = gpu::primeEnvironment({intel2, nvBoot}, "intel", P::Default, ingen);
    check(e.set == Env{{"DRI_PRIME", "pci-0000_00_02_0"}} && e.chosen == 0,
          "G5f2 förvalet NVIDIA: ett UTTRYCKLIGT Mesa-kort sätter ändå "
          "DRI_PRIME (förvalet är uppskattat)",
          text(e.set));

    const auto nv2 = kort("nv2", "0000:02:00.0", "nvidia", K::Discrete, false);
    e = gpu::primeEnvironment({intel, nv, nv2}, "nv2", P::HighPerformance, ingen);
    std::vector<gpu::DrmCard> g = {intel, nv, nv2};
    gpu::markSelectable(g, ingen);
    check(e.set.empty() && !g[1].a.selectable && !g[2].a.selectable,
          "G5g två NVIDIA utöver Intel: ingenting, och ingen av dem valbar",
          text(e.set));

    e = gpu::primeEnvironment({intel, nv}, "", P::Default, ingen);
    check(e.set.empty() && e.chosen == -1, "G5h Default utan id: ingenting",
          text(e.set));

    const auto vm = kort("vm", "0000:00:05.0", "hyperv_drm", K::Unknown, false);
    e = gpu::primeEnvironment({intel, amd, vm}, "vm", P::HighPerformance, ingen);
    check(e.set == Env{{"DRI_PRIME", "pci-0000_03_00_0"}} && e.chosen == 1 &&
              e.reason.find("kan inte väljas") != std::string::npos,
          "G5i ett id som inte är valbart (okänd drivrutin): preferensen "
          "bland de valbara",
          text(e.set) + " " + std::to_string(e.chosen) + " " + e.reason);

    e = gpu::primeEnvironment({intel, nv}, "", P::HighPerformance,
                              [](const char* n) -> const char* {
                                  return std::strcmp(n, "DRI_PRIME") == 0 ? "" : nullptr;
                              });
    check(e.set == offload, "G5j en TOM variabel är osatt", text(e.set));
}

// ------------------------------------------------------------------- G6

// TILLÄMPNINGEN mot en falsk miljö: en osatt och en TOM variabel sätts, en
// icke-tom är användarens och rörs inte, och en vägrad setenv räknas inte.
void fallTillämpning()
{
    std::map<std::string, std::string> miljö = {{"DRI_PRIME", ""}};
    const gpu::GetEnv läs = [&miljö](const char* n) -> const char* {
        const auto it = miljö.find(n);
        return it == miljö.end() ? nullptr : it->second.c_str();
    };
    const gpu::SetEnv skriv = [&miljö](const char* n, const char* v) {
        miljö[n] = v;
        return true;
    };
    gpu::PrimeEnv env;
    env.set = {{"DRI_PRIME", "pci-0000_03_00_0"}, {"__NV_PRIME_RENDER_OFFLOAD", "1"}};
    Env satta = gpu::applyPrimeEnvironment(env, läs, skriv);
    check(satta == env.set && miljö["DRI_PRIME"] == "pci-0000_03_00_0" &&
              miljö["__NV_PRIME_RENDER_OFFLOAD"] == "1",
          "G6a en TOM och en osatt variabel sätts", text(satta));

    miljö = {{"DRI_PRIME", "1"}};
    satta = gpu::applyPrimeEnvironment(env, läs, skriv);
    check(satta == Env{{"__NV_PRIME_RENDER_OFFLOAD", "1"}} && miljö["DRI_PRIME"] == "1",
          "G6b en icke-tom är användarens och rörs inte", text(satta));

    miljö.clear();
    satta = gpu::applyPrimeEnvironment(env, läs, [](const char*, const char*) {
        return false;
    });
    check(satta.empty(), "G6c en vägrad setenv räknas inte som satt", text(satta));
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("gpu_gate\n");
    fallId();
    fallSysfs();
    fallMiljö();
    fallTillämpning();

    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases, g_fails);
    return g_fails ? 1 : 0;
}
