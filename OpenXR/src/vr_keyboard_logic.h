#pragma once

// VR-TANGENTBORDETS RENA DELAR — layouterna (`keyboardLayout`) och vad en
// tangent skickar: tangent + påslagna vippor → den exakta följden av
// `gfx::Event` som ett fysiskt tangentbord ger (KeyDown med texten,
// TextInput, KeyUp). Ingen panel, ingen ritning och inget fönster, så delarna
// provas utan grafik (tests/vr_keyboard_gate).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glaze_openxr/vr_keyboard.h>

namespace glaze::openxr::detail {

// Mest så många byte (utan NUL) ryms i `gfx::Event::text`.
inline constexpr std::size_t kEventTextBytes = sizeof(gfx::Event::text) - 1;

// Delar UTF-8-texten i bitar om högst `maxBytes` byte, aldrig mitt i ett
// tecken. En tom text ger inga bitar.
std::vector<std::string> splitUtf8(const std::string& text, std::size_t maxBytes = kEventTextBytes);

// Texten tangenten skriver med vipporna `mods`: tom med Ctrl, `shiftedText`
// med Shift (om den finns), annars `text`.
const std::string& typedText(const KeyboardKey& key, std::uint32_t mods);

// Händelserna ett klick på tangenten ger, i ordning: KeyDown (bär texten, en
// lång text delas och KeyDown bär första biten), en TextInput per bit, KeyUp.
// `mods` ligger på tangenthändelserna. En vippa (`toggleModifier`) och en
// tangent utan både `Key` och text ger inga händelser.
std::vector<gfx::Event> keyEvents(const KeyboardKey& key, std::uint32_t mods);

} // namespace glaze::openxr::detail
