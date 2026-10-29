#pragma once
#include <cstdint>
#include <vector>

#include "glaze/message_box.h"
#include "glaze/strings.h"

// INTERN header (som `prop_parse.h`) — meddelanderutans TABELL: vilka
// knappar en uppsättning har, vilket `DialogResult` var och en ger, och
// vad rutan svarar när den stängdes UTAN att en knapp trycktes.
//
// Den ligger för sig själv av ett enda skäl: det här är den enda delen av
// `message_box.cpp` som är REN std och därför går att grinda headless
// (`tests/msgbox_gate.cpp`). Resten kräver ett fönster, en font och en
// pump. Att lägga tabellen i tre skilda switchar inne i cpp:n hade
// dessutom varit precis så en uppsättning glider isär från sitt eget
// returvärde.
//
// Den är INTE publik: `glaze/message_box.h` är API:t, och en app har inget
// ärende till mappningen — den ska bara kunna lita på att `None` aldrig
// kommer tillbaka.

namespace glaze {
namespace msgbox {

// TEXTEN ÄR ETT ID och ingen svensk
// literal — knapparna är ramverkets strängar, och `glaze/strings.h` bär
// dem med `lang_gate` del 2 som garant för att alla fyra språken har dem.
// Tabellen förblir REN STD (bara ett tal) och grindas därför headless.
struct ButtonSpec {
    std::uint32_t textId;
    DialogResult  result;
};

// Knapparna i VISUELL ordning (vänster → höger).
inline std::vector<ButtonSpec> buttonSpecs(MessageBoxButtons b)
{
    switch (b) {
    case MessageBoxButtons::OKCancel:
        return {{str::Ok, DialogResult::OK},
                {str::Cancel, DialogResult::Cancel}};
    case MessageBoxButtons::YesNo:
        return {{str::Yes, DialogResult::Yes}, {str::No, DialogResult::No}};
    case MessageBoxButtons::YesNoCancel:
        return {{str::Yes, DialogResult::Yes},
                {str::No, DialogResult::No},
                {str::Cancel, DialogResult::Cancel}};
    case MessageBoxButtons::RetryCancel:
        return {{str::Retry, DialogResult::Retry},
                {str::Cancel, DialogResult::Cancel}};
    case MessageBoxButtons::AbortRetryIgnore:
        // Abort har ett EGET id: svenskan och norskan säger
        // "Avbryt" om både Abort och Cancel, men engelskan säger "Abort"
        // och finskan "Keskeytä" — som Windows egen ruta.
        return {{str::Abort, DialogResult::Abort},
                {str::Retry, DialogResult::Retry},
                {str::Ignore, DialogResult::Ignore}};
    case MessageBoxButtons::OK:
    default:
        return {{str::Ok, DialogResult::OK}};
    }
}

// AVBRYT-EKVIVALENTEN — svaret när rutan stängdes utan knapptryck
// (Escape, krysset, ägaren som kaskadstänger). Det är den här funktionen
// som gör att `DialogResult::None` aldrig läcker ut ur `messageBox()`.
//
// De två uppsättningar som saknar ett uttryckligt avbryt får det svar som
// är MINST ingripande: `YesNo` → No, `AbortRetryIgnore` → Abort.
// Deras kryss är släckt och Escape
// ignoreras, men ägaren kan fortfarande kaskadstänga rutan — och då måste
// det finnas ett svar.
inline DialogResult cancelEquivalent(MessageBoxButtons b)
{
    switch (b) {
    case MessageBoxButtons::OKCancel:
    case MessageBoxButtons::YesNoCancel:
    case MessageBoxButtons::RetryCancel:
        return DialogResult::Cancel;
    case MessageBoxButtons::YesNo:
        return DialogResult::No;
    case MessageBoxButtons::AbortRetryIgnore:
        return DialogResult::Abort;
    case MessageBoxButtons::OK:
    default:
        return DialogResult::OK;
    }
}

// Har uppsättningen ett avbryt att NÅ med Escape och fönsterkrysset?
// `YesNo` och `AbortRetryIgnore` har det inte — där är ett val
// obligatoriskt, och då släcks krysset (`setControlBox(false)`) i stället
// för att tyst svara åt användaren.
inline bool hasCancelPath(MessageBoxButtons b)
{
    return b == MessageBoxButtons::OK ||   // krysset = OK, det enda svaret
           b == MessageBoxButtons::OKCancel ||
           b == MessageBoxButtons::YesNoCancel ||
           b == MessageBoxButtons::RetryCancel;
}

} // namespace msgbox
} // namespace glaze
