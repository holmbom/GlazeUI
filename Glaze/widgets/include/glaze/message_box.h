#pragma once
#include <string>

#include "glaze/app_window.h"
#include "glaze/canvas/image_source.h"

// MESSAGEBOX — meddelanderutan. Den mest använda dialogen som finns, och
// därför den
// som mest av allt ska gå att skriva på EN rad:
//
//     glaze::messageBox(*this, "Filen kunde inte sparas.", "Fel",
//                       glaze::MessageBoxButtons::OK,
//                       glaze::MessageBoxType::Error);
//
// FRI FUNKTION och inte en klass med en statisk medlem — husets form
// (`glaze::loadAssets`/`glaze::img`/`glaze::makeWidget` är alla fria
// funktioner). Rutan ÄR ett vanligt modalt `AppWindow` inuti, byggt av
// vanliga widgets; den bor i cpp-filen eftersom ingen behöver ärva den.
// Prejudikatet är `Dropdown::ListWindow` i dropdown.cpp.
//
// Den ligger i katalogROTEN och inte i `widgets/`: den mappen är "en fil
// per widget, ingen infrastruktur", och det här är ingen widget — den
// registreras inte, kan inte adopteras och finns aldrig i en `.glazeui`.

namespace glaze {

// Rutans BETYDELSE — den styr default-ikonen och ingenting annat.
// Namnen följer husets vokabulär och inte .NET:s: `Confirmation` där
// `MessageBoxIcon` säger `Question`.
//
// `Custom` betyder "ingen default-ikon ritas" och är platsen för en egen
// bild (`customIcon` — ett `ImageSource`, alltså en asset eller pixlar) —
// fältet betyder alltså något även när man inte skickar med någon: en ruta
// helt utan ikon.
enum class MessageBoxType { Custom, Information, Confirmation, Warning, Error };

// Knappuppsättningen — ett ENUM och inte en fri stränglista. De 184
// anropsställena är wx-idiom (`wxYES_NO | wxCANCEL`) som mappar 1:1, och en
// fri lista kan inte mappas på `DialogResult` utan ett andra returkontrakt
// (ett index), vilket hade dragit isär API:t. En överlagring med egen
// knapplista kan läggas till senare — brytande ändringar är gratis före
// v1.00.
enum class MessageBoxButtons {
    OK,
    OKCancel,
    YesNo,
    YesNoCancel,
    RetryCancel,
    AbortRetryIgnore,
};

// Visar rutan MODALT och returnerar knappen användaren valde.
//
// `DialogResult::None` LÄCKER ALDRIG UT: en stängning som inte gick via en
// knapp (Escape, fönsterkrysset, ägaren som kaskadstänger) mappas till
// uppsättningens avbryt-ekvivalent. Skälet är anropsställena — de ska kunna
// skriva `if (r == DialogResult::Yes)` utan att hantera ett fjärde tyst
// utfall. Mappningen:
//
//   OK                              -> OK
//   OKCancel · YesNoCancel ·
//   RetryCancel                     -> Cancel
//   YesNo                           -> No
//   AbortRetryIgnore                -> Abort
//
// De två sista uppsättningarna har inget avbryt, så deras fönsterkryss är
// SLÄCKT (`setControlBox(false)`) och Escape ignoreras — men ägaren kan
// fortfarande kaskadstänga rutan, och då gäller raderna ovan.
//
// Knapptexterna är RAMVERKETS strängar (`glaze/strings.h`) och följer det
// aktiva språket — `Abort` och `Cancel` har var sitt id, för de heter olika
// på engelska och finska fast svenskan säger "Avbryt" om båda.
//
// Standardknappen är FÖRSTA knappen (den har fokus när rutan öppnas) och
// avbrytknappen avbryt-ekvivalenten — fönstrets `setAcceptButton`/
// `setCancelButton`, så Enter och Escape går samma väg som
// i varje dialog. Ingen `defaultButton`-parameter: en sådan kan läggas till
// utan brott senare.
DialogResult messageBox(AppWindow& owner, const std::string& text,
                        const std::string& caption = "",
                        MessageBoxButtons  buttons = MessageBoxButtons::OK,
                        MessageBoxType     type = MessageBoxType::Information,
                        const ImageSource& customIcon = {});

// ÄGARLÖS variant — för det som händer innan ett fönster finns (ett fel
// under uppstart). Rutan blir ett eget toppfönster med en egen nästlad
// pump. Har appen ett fönster ska man skicka det: en ägd ruta hålls över
// sin ägare i Z-ordning och kaskadstängs med den.
DialogResult messageBox(const std::string& text,
                        const std::string& caption = "",
                        MessageBoxButtons  buttons = MessageBoxButtons::OK,
                        MessageBoxType     type = MessageBoxType::Information,
                        const ImageSource& customIcon = {});

} // namespace glaze
