#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "gfx/platform.h"

// GRAFIKKORTEN — det backend-neutrala. INTERN header i
// display_util.h:s form: de tre backendarna läser HÄR, så id:ts form,
// tillverkarnamnen och Linux-valets regel är EN sanning — och
// tests/gpu_gate.cpp når filen och mäter den headless mot ett HANDSKRIVET
// svar. Ren std, inga OS-typer.
//
// LINUX-LOGIKEN kompileras också i Windows- och Mac-bygget (ett par kB), så
// att grinden mäter sysfs-läsningen mot ett falskt träd och miljöregeln mot
// en falsk `getenv` på varje plattform — display_util:s modell med
// EDID-parsern. Den rör aldrig processens miljö; det gör linux_gpu.cpp.

namespace gfx::gpu {

// Tillverkaren ur PCI-id:t ("NVIDIA", "AMD", "Intel" …), tom när den är okänd.
std::string vendorName(std::uint32_t vendorId);

// Preferensens och slagets namn i loggen.
const char* preferenceName(GpuPreference p);
const char* kindName(GpuKind k);

// ID:T: `pci:vvvv:dddd:ssssssss:rr` i gemen hex —
// subsystemet i DXGI:s `SubSysId`-form (subsystem-enhet << 16 |
// subsystem-tillverkare), som är samma tal som Linux `subsystem_device` och
// `subsystem_vendor` ihop.
std::string pciAdapterId(std::uint32_t vendor, std::uint32_t device,
                         std::uint32_t subsys, std::uint32_t revision);

// DUBBLETTERNA: varje id som förekommer mer än en gång i `list` får `@where[i]`
// (PCI-adressen "bb:dd.f", eller Metals plats) — eller `#n`, löpnumret bland
// dubbletterna, när `where[i]` är tom. Ett unikt id rörs inte, så id:t byts
// bara på en maskin med två lika kort. `where` är parallell med `list`.
void disambiguateIds(std::vector<GpuAdapter>& list,
                     const std::vector<std::string>& where);

// ------------------------------------------------------------------ Linux

// Sökvägarna sysfs-läsningen går mot — standardvägarna, eller grindens
// falska träd.
struct DrmPaths {
    std::string              sysClassDrm = "/sys/class/drm";
    std::string              nvidiaProc  = "/proc/driver/nvidia/gpus";
    std::vector<std::string> pciIds      = {"/usr/share/hwdata/pci.ids",
                                            "/usr/share/misc/pci.ids",
                                            "/usr/share/pci.ids"};
};

// Ett DRM-kort: listans post, och det miljöregeln behöver.
struct DrmCard {
    GpuAdapter  a;
    std::string card;      // "card1" — EGL:s `/dev/dri/card1` slås upp mot den
    std::string pciSlot;   // "0000:01:00.0"; tom för ett icke-PCI-kort
    std::string driver;    // "i915", "nvidia", "amdgpu" …
    bool        bootVga = false;
};

// KORTEN I SYSFS, i PCI-ordning (icke-PCI sist). Räknas: katalogerna
// `card[0-9]+` (inte anslutningarna `card1-HDMI-A-1`) med en
// `device/drm/renderD*` (inte simpledrm och vgem). Ur `device/` läses
// `vendor`, `device`, `subsystem_vendor`, `subsystem_device`, `revision`,
// `boot_vga`, `removable` och `mem_info_vram_total`, och ur `device/uevent`
// drivrutinen (`DRIVER=`) och sloten (`PCI_SLOT_NAME=`) — symlänkarnas
// basnamn är reserven. Namnet ur NVIDIA:s `information` (`Model:`), annars
// ur pci.ids, annars "PCI vvvv:dddd". `isDefault` är kortet med `boot_vga`,
// annars det första. `selectable` sätts INTE här (`markSelectable`).
std::vector<DrmCard> scanDrmCards(const DrmPaths& paths = {});

// Processens miljö, som `std::getenv` — grinden ger en falsk. nullptr eller
// "" = osatt.
using GetEnv = std::function<const char*(const char*)>;

// Det `gfx::init` ska sätta, och varför. `chosen` är målets index i korten
// (−1 = inget mål: Default, eller användarens miljö gäller).
struct PrimeEnv {
    std::vector<std::pair<std::string, std::string>> set;
    int                                              chosen = -1;
    std::string                                      reason;
};

// MILJÖREGELN:
//  1. Är någon av variablerna som väljer kortet satt och icke-tom (DRI_PRIME,
//     __NV_PRIME_RENDER_OFFLOAD, __NV_PRIME_RENDER_OFFLOAD_PROVIDER,
//     __GLX_VENDOR_LIBRARY_NAME, __EGL_VENDOR_LIBRARY_FILENAMES,
//     __EGL_VENDOR_LIBRARY_DIRS, LIBGL_ALWAYS_SOFTWARE) sätts INGENTING —
//     användaren vinner helt.
//  2. VALBARHETEN FÖRST: `wantedId` är målet bara om kortet finns OCH är
//     valbart; annars väljer preferensen bland de VALBARA (HighPerformance:
//     External, Discrete, Unknown, Integrated; LowPower: Integrated,
//     Discrete, External; PCI-ordning inom slaget). Default väljer inget.
//  3. Ett mål ur PREFERENSEN som är förvalskortet (`boot_vga`): ingenting.
//     Ett UTTRYCKLIGT mål stoppas aldrig av `boot_vga`.
//  4. NVIDIA:s drivrutin: `__NV_PRIME_RENDER_OFFLOAD=1` — ALDRIG `DRI_PRIME`
//     (Mesa hade fallit till llvmpipe) — och ingenting när det är det enda
//     kortet. Bland flera NVIDIA-kort går det inte (providernamnet är X:s).
//  5. En Mesa-drivrutin: `DRI_PRIME=pci-0000_01_00_0`. PREFERENSEN väljer
//     inte ett Mesa-kort när förvalskortet drivs av NVIDIA:s drivrutin; ett
//     UTTRYCKLIGT val gör det ändå (förvalet är bara uppskattat, och
//     kompositorn kan köra på Mesa).
//  6. Ett icke-PCI-kort, eller en drivrutin Mesa inte känner: bara som
//     förvalskortet, och då sätts ingenting.
PrimeEnv primeEnvironment(const std::vector<DrmCard>& cards,
                          const std::string& wantedId, GpuPreference pref,
                          const GetEnv& getenv);

// TILLÄMPNINGEN: `setenv(namn, värde)` för varje par i `env.set` vars
// variabel är osatt ELLER TOM (en tom räknas som osatt, som i regel 1, och
// skrivs över); en icke-tom är användarens och rörs aldrig. Svaret är paren
// som faktiskt sattes — `setenv` svarar false när det vägrades.
using SetEnv = std::function<bool(const char*, const char*)>;
std::vector<std::pair<std::string, std::string>>
applyPrimeEnvironment(const PrimeEnv& env, const GetEnv& getenv,
                      const SetEnv& setenv);

// `selectable` på varje kort med regeln ovan: kortet går att nå som ett
// uttryckligt mål — falskt för alla när användarens variabel gäller.
void markSelectable(std::vector<DrmCard>& cards, const GetEnv& getenv);

// Variablerna regel 1 läser — de enda GlazeUI någonsin sätter är DRI_PRIME
// och __NV_PRIME_RENDER_OFFLOAD.
const std::vector<const char*>& primeVariables();

} // namespace gfx::gpu
