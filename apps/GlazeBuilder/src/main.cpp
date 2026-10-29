// Vanlig main även i WIN32-subsystem (MinGW:s runtime slussar WinMain→main).
#include "builder_window.h"

int main(int argc, char** argv)
{
    // GlazeBuilder [fönster.h] — ingen fil ger ett nytt tomt fönster
    BuilderWindow win(argc > 1 ? argv[1] : "");
    return win.run();   // visar + pumpar (App skapas lat)
}
