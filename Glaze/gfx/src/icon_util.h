#pragma once
// Backend-neutralt och internt: det de sex
// `Window::setIcon`-implementationerna och `gfx::appId()` DELAR, så att
// reglerna är EN sanning — inte sex kopior som kan glida isär.
//   * Ramens giltighet och VALREGELN (samma regel som
//     `glzassets::pickIconSource` i tillgångskedjan).
//   * Programfilens namn, förvalet för `gfx::appId()` — DEKLARERAT här och
//     implementerat per backend bredvid `basePath()`.
// Header-only utom `programName`; bara fasadtyper.

#include <algorithm>
#include <cstddef>
#include <string>

#include "gfx/window.h"

namespace gfx {

// Är ramen användbar? Mått > 0 och exakt width × height × 4 byte. En ram som
// inte är det hoppas över av varje backend, som om den inte fanns.
inline bool iconImageValid(const IconImage& im)
{
    return im.width > 0 && im.height > 0 &&
           im.rgba.size() == static_cast<std::size_t>(im.width) *
                                 static_cast<std::size_t>(im.height) * 4u;
}

// Ramens SIDA i valregelns mening: den längre av bredd och höjd (ramarna är
// kvadratiska i praktiken; en avlång ram mäts på sin största utsträckning).
inline int iconImageSide(const IconImage& im)
{
    return std::max(im.width, im.height);
}

// VALREGELN: för målsidan `target` px — den MINSTA giltiga ramen vars sida är
// ≥ target, annars den STÖRSTA. Vid lika sida vinner den som står först.
// nullptr när ingen ram är giltig (eller listan är tom). Backenden SKALAR
// ALDRIG själv: den väljer en ram och lämnar storleksanpassningen åt OS:et.
inline const IconImage* pickIconImage(const WindowIcon& icon, int target)
{
    const IconImage* minstaRäckande = nullptr;   // minsta med sida ≥ target
    const IconImage* största        = nullptr;
    for (const IconImage& im : icon.images) {
        if (!iconImageValid(im))
            continue;
        const int sida = iconImageSide(im);
        if (!största || sida > iconImageSide(*största))
            största = &im;
        if (sida >= target &&
            (!minstaRäckande || sida < iconImageSide(*minstaRäckande)))
            minstaRäckande = &im;
    }
    return minstaRäckande ? minstaRäckande : största;
}

// PROGRAMFILENS NAMN utan katalog och filändelse ("demo" ur `demo.exe` eller
// `.../demo`) — förvalet för `gfx::appId()`. Implementeras av den
// inkompilerade backenden bredvid `basePath()`: Windows `GetModuleFileNameW`,
// macOS `NSBundle.mainBundle.executableURL`, Linux `/proc/self/exe`. "" när
// OS:et inte svarar.
std::string programName();

// DOCKIKONEN — backendens del av `gfx::setShowInDock`;
// begäran är redan lagrad (platform.cpp) när den anropas. macOS byter
// aktiveringspolicy när `NSApp` finns (före init läser `gfx::init` begäran);
// Windows och Linux har ingen docka och loggar false en gång per process.
void applyShowInDock(bool show);

} // namespace gfx
