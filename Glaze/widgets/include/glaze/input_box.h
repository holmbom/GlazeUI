#pragma once
#include <optional>
#include <string>

#include "glaze/app_window.h"
#include "glaze/language.h"

// INMATNINGSRUTAN (VB:s `InputBox`, `wxGetTextFromUser`). En modal dialog med
// en ledtext, ett
// textfält och OK/Avbryt — meddelanderutans syskon, och lika kort att skriva:
//
//     if (auto namn = glaze::inputBox(*this, "Lagrets namn:", "Byt namn",
//                                     lager.name()))
//         lager.setName(*namn);
//
// FRI FUNKTION som `messageBox` (ett utvärde, en handfull invärden) och inte
// en klass som `FileDialog`. SVARET ÄR `std::optional<std::string>`: texten
// vid OK — också en TOM text, som är ett svar — och `std::nullopt` vid
// Avbryt, Escape, krysset eller ägarens kaskadstängning. VB:s "tom sträng =
// avbrutet" hade inte gått att skilja från ett tömt fält.
//
// ETIKETTERNA ÄR `TrText` (husets etikettyp — `glaze::tr(id)` går rakt in),
// förvalet en `std::string`: det är ANVÄNDARENS text, `TextInput`s regel.
// Förvalet står MARKERAT med fokus i fältet, så att man skriver över det
// eller trycker Enter. Enter är OK och Escape Avbryt — fönstrets standard-
// och avbrytknapp, samma väg som i varje dialog.
//
// Måtten är meddelanderutans (`msg*` i Style): samma luft, och fältet är
// brytbredden brett (`msgTextMaxW`) — ledtexten bryts mot samma bredd.
//
// FLERA RADER: `InputBoxMode::MultiLine` byter fältet mot ett
// `TextArea` — Enter ger en NY RAD, och rutan stängs med OK-KNAPPEN (Escape är
// fortfarande Avbryt). Fältet är stilens `textAreaRows` rader högt, förvalet
// står markerat som i enradsläget, och svaret är textens LF-form (`TextArea`s
// kontrakt: CRLF och ensamt CR har blivit LF). Resten är oförändrat.
//
// DIALOGEN ÄR INGEN WIDGET (meddelanderutans prejudikat): den registreras
// inte, adopteras inte och finns aldrig i en `.glazeui`. Fönstret bor i
// `src/input_box.cpp` och den interna `src/input_box_window.h`, för grindens
// skull. INTE HÄR med flit: lösenordsläge, validering medan man skriver — en
// sådan dialog bygger appen av samma widgets.

namespace glaze {

// Fältets form: EN rad (`TextInput`, Enter är OK) eller flera (`TextArea`,
// Enter är en ny rad).
enum class InputBoxMode { SingleLine, MultiLine };

// Visar rutan MODALT över `owner` och svarar med texten vid OK.
std::optional<std::string> inputBox(AppWindow& owner, const TrText& prompt,
                                    const TrText&      caption = {},
                                    const std::string& initial = {},
                                    InputBoxMode       mode =
                                        InputBoxMode::SingleLine);

// ÄGARLÖS variant — för det som händer innan ett fönster finns
// (meddelanderutans regel: har appen ett fönster ska det skickas).
std::optional<std::string> inputBox(const TrText&      prompt,
                                    const TrText&      caption = {},
                                    const std::string& initial = {},
                                    InputBoxMode       mode =
                                        InputBoxMode::SingleLine);

} // namespace glaze
