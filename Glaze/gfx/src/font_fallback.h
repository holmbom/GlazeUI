#pragma once
// GLYFRESERVEN — EN regel för alla tre backendarnas `Font`.
// Backend-neutralt, internt och HEADER-ONLY: ingen CMake-rad, så
// fällan med de tre backendblocken i ../CMakeLists.txt uppstår inte.
//
// ORDNINGEN för en kodpunkt:
//   1. fontens EGEN glyf,
//   2. en RESERVFONT som har glyfen (OS:ets egen kedja: DirectWrite
//      `MapCharacters`, Core Text `CTFontCreateForString`, fontconfigs
//      `FcFontSort`) — inte för ett STYRTECKEN (C0, DEL, C1),
//   3. U+FFFD — fontens egen, sedan en reservs,
//   4. fontens .notdef (index 0).
// `real` är sant BARA för kodpunktens egen glyf i steg 1 och 2 — det
// `Font::hasGlyph` svarar. Ett styrtecken är ALDRIG `real`: det har ingen
// synlig glyf att lova (en font som mappar tabben ger oftast en tom glyf),
// och dess egen glyf ritas ändå när fonten har en.
//
// Backenden CACHAR svaret per kodpunkt i sin font (också ett negativt), och
// mätningen (`advanceOf`), UI-atlasen (`getGlyph`) och `rasterGlyph` går alla
// genom samma cache — därför kan de aldrig vara oense om vilken glyf en
// kodpunkt är.

#include <cstdint>

namespace gfx {

// En upplöst glyf: vilken font-face (0 = fontens egen, 1… reserverna i den
// ordning backenden lade till dem) och glyfens index i den.
struct GlyphRef {
    int           face  = 0;
    std::uint32_t index = 0;
    bool          real  = false;   // kodpunktens egen glyf — inte ersättningen
};

// C0 (U+0000–U+001F), DEL (U+007F) och C1 (U+0080–U+009F): styrtecken, som
// aldrig söker en reserv.
inline bool isControlCodepoint(char32_t cp)
{
    return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
}

// Regeln ovan. `own(cp)` svarar med fontens eget glyfindex (0 = saknas);
// `reserve(cp, ref)` letar en reservfont som har kodpunkten och fyller `ref`
// (face och index) — false när ingen har den.
template <class Own, class Reserve>
GlyphRef resolveGlyph(char32_t cp, Own own, Reserve reserve)
{
    constexpr char32_t kReplacement = 0xFFFD;
    const bool         control      = isControlCodepoint(cp);
    if (const std::uint32_t gi = own(cp); gi != 0)
        return GlyphRef{0, gi, !control};
    GlyphRef r;
    if (!control && reserve(cp, r)) {
        r.real = true;
        return r;
    }
    if (cp != kReplacement) {
        if (const std::uint32_t gi = own(kReplacement); gi != 0)
            return GlyphRef{0, gi, false};
        // Ett styrtecken söker ALDRIG en reserv — inte heller för
        // ersättningstecknet.
        if (!control && reserve(kReplacement, r)) {
            r.real = false;
            return r;
        }
    }
    return GlyphRef{0, 0, false};
}

} // namespace gfx
