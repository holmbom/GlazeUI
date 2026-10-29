#pragma once
#include <cstdint>
#include <string>

#include "gfx/geometry.h"
#include "gfx/window.h"   // WindowIcon — ikonens ramar

namespace gfx {

// Systemfältet — appens ikoner vid klockan:
// Windows meddelandefält (`Shell_NotifyIcon`) och macOS menyradens
// statusposter (`NSStatusItem`). Konsumeras av `glaze::TrayIcon`; appar ser
// aldrig den här headern.
//
// APP-GLOBALT, och därför fria funktioner med ett id i stället för rena
// virtuella på `Window`: ikonen hör till APPEN och inte till ett fönster. Det
// ger TRE implementationer (en per backend) i stället för sex, och ingen
// proxyfälla.
//
// ID:T VÄLJS AV ANROPAREN (timer-id-mönstret), 1–`kTrayIdMax`: Windows v4
// bär ikonens id i 16 bitar (HIWORD i callbacken), och samma id mellan två
// körningar låter Windows 11 minnas var användaren placerade ikonen — glaze
// tar därför det LÄGSTA lediga.
//
// HÄNDELSERNA kommer som `EventType::Tray` med windowId 0: `Event::trayId`,
// `Event::trayAction` och ankaret i mouseX/mouseY (tabellen och rymden står
// i gfx/events.h). Ett event kan ligga kvar i kön efter `removeTrayIcon` —
// mottagaren slår upp id:t och släpper det som inte finns.
//
// DET SOM RITAS är inte fasadens: menyn är glaze:s egen popup och aldrig
// OS:ets, och tooltipen är OS:ets egen. Fasaden lägger
// ikonen i fältet, levererar händelserna, svarar var ikonen står och tickar
// animationen.
//
// UI-TRÅDENS, som resten av fasaden. Varje funktion är säker före `init()`
// och efter `shutdown()`: `trayAvailable()` svarar ändå — det är ett faktum
// om plattformen, och appen frågar innan något fönster finns — och resten
// svarar false och gör ingenting, så en `glaze::TrayIcon` som lever längre
// än appen inte kan krascha den.
//
//   Windows  `Shell_NotifyIcon` v4 med ett dolt TOPPNIVÅfönster som mottagare
//            (inte message-only: ett sådant får inga broadcasts, och utan
//            `TaskbarCreated` kommer ikonen inte tillbaka efter en
//            Explorer-omstart); bilden i aktivitetsfältets DPI
//   macOS    en `NSStatusItem` per id; bilden 18 pt
//   Linux    INTE BYGGT — `trayAvailable()` är false och resten vägrar tyst
//            (backend_linux/linux_tray.cpp bär vägen dit)

using TrayId = std::uint32_t;

// Det största id:t — Windows v4:s 16 bitar. 0 är aldrig ett id.
inline constexpr TrayId kTrayIdMax = 0xFFFF;

// IKONENS INNEHÅLL — allt `setTrayIcon` ersätter på en gång.
struct TrayIconDesc {
    // RAMARNA med rak alfa (`WindowIcon`, gfx/window.h): backenden väljer den
    // storlek fältet visar och skalar aldrig själv. `programFirst` =
    // programfilens egen ikon vinner där den finns (exe:ns ikonresurs nr 1,
    // paketets `CFBundleIconFile`), och ramarna gäller där den saknas.
    WindowIcon  image;
    // UTF-8; tom = ingen tooltip. Windows kapar vid 127 UTF-16-enheter, på
    // en teckengräns.
    std::string toolTip;
    // macOS MALLBILD — menyraden färgar den efter ljust och mörkt läge. Ingen
    // verkan på Windows och Linux.
    bool        templateImage = false;
};

// Har plattformen ett systemfält? Windows och macOS true, Linux false —
// samma svar före `init()` som efter (ett Explorer som inte startat än gör
// inte Windows till en plattform utan fält; se `setTrayIcon`).
bool trayAvailable();

// LÄGG TILL eller UPPDATERA ikonen med id:t — samma anrop för båda, och det
// ersätter hela innehållet. true = id:t är registrerat: ikonen visas, eller
// visas så snart skalet kan ta emot den (Windows: ett Explorer som inte
// startat än vid en autostart lägger till den vid `TaskbarCreated`, liksom
// efter en Explorer-omstart och ett byte av primärskärmens DPI). false =
// VÄGRAT och inte registrerat: id:t är 0 eller större än `kTrayIdMax`, det
// finns ingen bild (inga ramar och ingen ikon i programfilen), eller
// plattformen saknar systemfält. Vägran loggas, utom på Linux där den är tyst.
bool setTrayIcon(TrayId id, const TrayIconDesc& d);

// Tar bort ikonen och stoppar dess timers. Idempotent: ett okänt id är en
// no-op.
void removeTrayIcon(TrayId id);

// VAR IKONEN STÅR — `icon` är ikonens rect och `work` ARBETSYTAN (skärmen
// minus aktivitetsfält och menyrad) på den skärm ikonen står på, båda i
// PLACERINGSRYMDEN (`WindowDesc::posX`s). glaze lägger menyn mot dem: ikonen
// klämd i arbetsytan, så menyn hamnar kant i kant med fältet. false = okänt —
// id:t är inte registrerat, OS:et kan inte svara, eller Linux; utvärdena är
// då orörda och glaze faller tillbaka på eventets ankare.
//
//   Windows  Shell_NotifyIconGetRect + skärmens rcWork
//   macOS    knappens fönsterram + skärmens visibleFrame, vända mot
//            huvudskärmens höjd (som `Window::setPosition`)
bool trayIconRect(TrayId id, Rectf& icon, Rectf& work);

// ANIMATIONENS TIDSKÄLLA — `EventType::Tray` med `TrayAction::Tick` var
// `ms`:e millisekund, tills 0 stänger av. Ett nytt anrop startar om
// perioden. id:t måste vara registrerat (`setTrayIcon`), annars en no-op, och
// `removeTrayIcon` stoppar timern. Den väcker `waitEvent` som en fönstertimer
// gör, och glaze startar den bara medan ikonen syns, är startad och har
// minst två bilder — annars sover appen (regel 1).
void setTrayTimer(TrayId id, std::uint32_t ms);

} // namespace gfx
