// Systemfältet på Linux — en KOMMENTERAD DUMMY.
//
// Systemfältet finns inte på Linux (bara macOS och Windows har det).
// Vägen dit: StatusNotifierItem över D-Bus mot
// `org.kde.StatusNotifierWatcher` (KDE; GNOME bara med AppIndicator-
// tillägget) med vår egen popup som meny (ingen dbusmenu). XEmbed
// (`_NET_SYSTEM_TRAY_S0`) är X11-only och utgående.
//
// Fasaden (gfx/tray.h) svarar därför ärligt att det inte finns något fält:
// `trayAvailable()` är false, `setTrayIcon` vägrar, `trayIconRect` vet
// ingenting och `removeTrayIcon`/`setTrayTimer` är no-op. Allt är TYST —
// `glaze::TrayIcon` loggar EN gång per process när appen ber om en ikon
// (`show()`), och en rad per anrop här hade bara upprepat den.
//
// DOCKAN finns inte heller i protokollen vi talar: begäran lagras i
// platform.cpp (som `setAppId`s id), och `applyShowInDock` loggar false en
// gång.
//
// Aktivitetsfältets motsvarighet, `WindowDesc::showInTaskbar = false`, är
// också en dummy och loggas i proxyns init (linux_platform.cpp) — X11:s
// `_NET_WM_STATE_SKIP_TASKBAR` är inte byggt, och Wayland saknar formen.

#include "../icon_util.h"   // applyShowInDock
#include "gfx/platform.h"
#include "gfx/tray.h"

namespace gfx {

bool trayAvailable()
{
    return false;
}

bool setTrayIcon(TrayId, const TrayIconDesc&)
{
    return false;
}

void removeTrayIcon(TrayId)
{
}

bool trayIconRect(TrayId, Rectf&, Rectf&)
{
    return false;
}

void setTrayTimer(TrayId, std::uint32_t)
{
}

// Loggen går bara vid false — det är BEGÄRAN som inte kan uppfyllas, och en
// rad varje gång någon bekräftar förvalet vore brus. En rad per process.
void applyShowInDock(bool show)
{
    static bool loggat = false;
    if (!show && !loggat) {
        loggat = true;
        log("setShowInDock(false): Linux har ingen docka i protokollen vi "
            "talar — begäran lagras men ändrar ingenting");
    }
}

} // namespace gfx
