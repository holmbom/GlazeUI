#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "md_document.h"

// PARSERN — Markdown-text → `md::Document`. INTERN
// och REN STD (grindad headless i `markdown_gate` del P). Delmängden och dess
// skrivna gränser står i `glaze/widgets/markdown_view.h`; algoritmen är
// CommonMark 0.31.2:s "Appendix A: A parsing strategy" i två faser —
// blockstrukturen rad för rad med en stack av öppna behållare, sedan inline
// per stycke, rubrik och tabellcell med avgränsarstacken och
// `openers_bottom` (`process emphasis`), så att patologisk indata förblir
// linjär. Inga undantag kastas: allt som inte är en konstruktion är text.

namespace glaze::md {

// Hela dokumentet. CR LF och ensamt CR blir LF, NUL och ogiltig UTF-8 blir
// U+FFFD (en per byte), och referensdefinitionerna löses (den FÖRSTA vinner)
// innan någon länk tolkas.
Document parse(std::string_view text);

// RUBRIKANKARET (GitHubs regel) för en rubriks text: gemener (ASCII och
// Latin-1 U+00C0–U+00DE), blanksteg blir `-`, och ASCII-skiljetecken utom
// `-` och `_` försvinner liksom Latin-1:s symboler U+0080–U+00BF och × ÷.
// "Översikt & mer" → "översikt--mer". UTAN unikhetssuffix — `parse` lägger
// till `-1`, `-2` för dubbletter.
std::string slug(std::string_view text);

// SCHEMAT i början av en adress — `[A-Za-z][A-Za-z0-9+.-]*:` (RFC 3986) —
// som dess längd utan kolonet, eller 0 när adressen inte har något. Länkens
// förval (nedan) och bildernas uppslag läser samma regel: ett bildnamn MED
// ett schema (`https://…/x.png`) är en adress och slås aldrig upp som en
// asset — inga filer och inget nät, och ingen asset-miss att logga.
std::size_t schemeLength(std::string_view url);

// VAD ETT KLICK PÅ LÄNKEN GÖR när appen inte tog den (`LinkClickArgs::
// handled`): `#ankare` rullar till rubriken, schemat `http`, `https` eller
// `mailto` (skiftlägesokänsligt) öppnas i systemets hanterare, och allt
// annat — `file:`, `javascript:`, en relativ stig — gör ingenting. Ett
// dokument kan komma utifrån, och `ShellExecute("file:///…exe")` kör program.
enum class LinkAction : std::uint8_t { Anchor, Open, None };
LinkAction linkAction(std::string_view url);

} // namespace glaze::md
