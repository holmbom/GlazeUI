// Vanlig main även i WIN32-subsystem (MinGW:s runtime slussar WinMain→main).
#include <cstring>

#include "dock_window.h"
#include "splash_window.h"
#include "start_window.h"
#include "tray_window.h"
#include "widget_gallery.h"
#include "window_lab.h"

// DEMON ÄR EN RIGG och inte ett exempel — därför ett argument som
// öppnar galleriet direkt. Det är den enda vägen att komma åt det
// HANDKODADE fönstret utan att klicka sig dit, och det gör det testbart:
// `timeout 3 ./demo --gallery` röktestar handkodsvägen för sig.
//
//   demo                  startfönstret (.glazeui-vägen: dock, ankare, syskon)
//   demo --gallery        galleriet (handkodat: samma ramverk, ingen .glazeui)
//   demo --tray           systemfältsappen (ikonen vid klockan, stäng till fältet)
//   demo --tray --hidden  …startad DOLD — bara ikonen syns, som vid en autostart
//   demo --window-lab     fönsterlabbet (geometrin, läget, spara/återställ, pekarna)
//   demo --dock           dockningen (flikar, staplar, handtag, layoutsträngen)
//   demo --splash …       splashen FÖRST (ramlös, mitt på skärmen, en Timer i
//                         .glazeui stänger den) — sedan som utan flaggan
int main(int argc, char** argv)
{
    glaze::loadAssets();   // assets.bin → råbytes, FÖRE första fönstret
                           // (saknad fil = tyst no-op + en loggrad)

    // …och language.bin — appens id 1000+. Samma
    // ordning och samma kandidatvägar som assets. STARTSPRÅKET väljs
    // sedan av kedjan i `glaze::tr`: explicit `setLanguage` (demon gör
    // inget) → OS-lokalen → engelska.
    glaze::loadLanguage();

    bool galleri = false, systemfalt = false, dold = false;
    bool splash = false, labb = false, dockning = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gallery") == 0)
            galleri = true;
        else if (std::strcmp(argv[i], "--tray") == 0)
            systemfalt = true;
        else if (std::strcmp(argv[i], "--hidden") == 0)
            dold = true;
        else if (std::strcmp(argv[i], "--splash") == 0)
            splash = true;
        else if (std::strcmp(argv[i], "--window-lab") == 0)
            labb = true;
        else if (std::strcmp(argv[i], "--dock") == 0)
            dockning = true;
    }

    // SPLASHEN går före allt annat och stänger sig själv:
    // `run()` återvänder när dess timer stängt den, och appen fortsätter
    // med fönstret flaggorna valt.
    if (splash) {
        SplashWindow s;
        s.run();
    }

    if (labb) {
        WindowLab l;
        return l.run();
    }

    if (dockning) {
        DockWindow d;
        return d.run();
    }

    if (galleri) {
        WidgetGallery g;
        return g.run();
    }

    if (systemfalt) {
        // Bara med ett systemfält: utan det hade appen varken haft en ikon
        // att visa sig igen med eller (på macOS) en dockikon — osynlig och
        // omöjlig att nå.
        const bool stod = glaze::TrayIcon::isSupported();
        if (stod)
            glaze::setShowInDock(false);   // app-globalt, FÖRE första fönstret
        TrayWindow t;
        if (stod && dold)
            t.hide();   // före run(): fönstret öppnas DOLT, ikonen syns
        return t.run();
    }

    StartWindow win;
    return win.run();   // visar + pumpar tills win stängts (App skapas lat)
}
