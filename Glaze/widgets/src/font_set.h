#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/renderer.h>
#include <gfx/text.h>

#include "glaze/canvas/geometry.h"
#include "glaze/style.h"
#include "glaze/widget.h"

// TEXTFONTERNA — fönstrets fonter för FORMATERAD
// text: tre STORLEKAR (brödtexten, underrubriken och rubriken — `Style` räknar
// upp dem) × två familjer (UI, Mono) × fyra stilar. INTERN, och nådd genom
// `Ctx::textFonts` (widget.h); Markdown-vyns layout (`markdown/md_layout.cpp`)
// är konsumenten.
//
// SLUTEN MEN LAT: mängden är räknad på förhand — 24 platser, inga
// fria storlekar — men en plats laddas först när den FRÅGAS, också mitt i en
// bildruta. Eager i `reloadFont` hade gjort att varje popup, tooltip och meny
// betalat för fonter den aldrig ritar. Prejudikatet är ritytans
// `gfx::SdfAtlas::font` (sdf.cpp).
//
// VARJE PLATS MINNS SIN STORLEK: en ny px (stilbyte, skalbyte, ny skärm)
// nollställer den, och nästa fråga laddar om. En font som INTE går att ladda
// ger nullptr och prövas inte igen förrän storleken ändras — så en saknad stil
// inte blir en laddning per bildruta. Anroparen faller då tillbaka på
// fönstrets font (`md::fontFor`).
//
// LIVSTIDEN är fontens och bildcachens: fonterna hör till fönstrets renderer
// (en font-atlas är en TEXTUR), så `AppWindow` skapar mängden i `showOwned`
// och `App::executeClose` river den i SAMMA block som `font_` — före
// `window().shutdown()`. Inget statiskt tillstånd här (`dll_gate`).
//
// FRIA STORLEKAR bredvid den slutna mängden: ett fälts eller en
// listas textstorlek i px eller mm (`TextInput::setTextSize` m.fl.) är DATA,
// som ritytans `TextStyle::size` — en storlek som inte står i stilen. De
// laddas lika lat, en per (FYSISK px-storlek, familj, stil), och delas av alla
// widgets i fönstret med samma nyckel. Mängden har ett TAK: fler än
// `kFriaMax` storlekar släpper de som frågats längst sedan, och det sker BARA
// i `trim()` vid framens början (bildcachens regel) — aldrig mitt i en
// bildruta, där en batch kan peka på den gamla atlasen.

namespace glaze {

// Storleken en text ritas i — `uiFontSize`, `subheadingFontSize` respektive
// `headingFontSize` i fönstrets stil.
enum class TextSize : std::uint8_t { Body, Subheading, Heading };

class FontSet {
public:
    // Fonten för (storlek, familj, stil) i fönstrets renderer `r`, med
    // stilens storlek × skalan — eller nullptr när den inte går att ladda.
    gfx::Font* get(gfx::Renderer& r, const Style& s, float scale,
                   TextSize size, gfx::FontFamily family,
                   gfx::FontStyle style);

    // En FRI storlek: fonten i `px` FYSISKA px i familjen och
    // stilen — eller nullptr när den inte går att ladda (prövas inte igen
    // förrän den släppts). Familjen och stilen finns för `Draw::withFont`:
    // terminalen ritar i Mono, fet och kursiv i en fri
    // storlek. Fältens och listornas `sizedText` frågar UI/Regular.
    gfx::Font* sized(gfx::Renderer& r, float px,
                     gfx::FontFamily family = gfx::FontFamily::UI,
                     gfx::FontStyle  style  = gfx::FontStyle::Regular);

    // Framens början (`AppWindow::renderContent`): de fria storlekarna över
    // taket släpps, de som frågats längst sedan först.
    void trim();

private:
    struct Slot {
        std::unique_ptr<gfx::Font> font;
        float                      px    = 0.0f;   // storleken den laddades i
        bool                       tried = false;  // laddad eller prövad
    };
    Slot slots_[3][2][4];   // [storlek][familj][stil]

    // De fria platsernas NYCKEL är (px, familj, stil).
    struct Fri {
        std::unique_ptr<gfx::Font> font;   // nullptr = gick inte att ladda
        float                      px     = 0.0f;
        gfx::FontFamily            family = gfx::FontFamily::UI;
        gfx::FontStyle             style  = gfx::FontStyle::Regular;
        std::uint64_t              frågad = 0;   // `ram_` vid senaste frågan
    };
    std::vector<Fri> fria_;
    std::uint64_t    ram_ = 0;   // stegas av `trim()` — en per bildruta
};

// TEXTSTORLEKENS REGEL — EN för fälten och listorna som tar en
// (`TextInput`, `TextArea`, `ListBox`, `Dropdown`). `textSizeValue` är
// sättarens: ett ändligt mått ≥ 0 i px eller mm, 0 = stilens (normaliserat
// till `Length{}`); världsenheter har ingen mening utanför ritytans kamera.
// Svaret är det normaliserade måttet, eller nullopt — loggat med typen och
// namnet — när det avvisas. `sizedText` är ritningens och mätningens: Ctx:t
// med storlekens font isatt (`Label::roleCtx`s form — Draw läser `c.font`,
// så ett storleksbyte är ett fältbyte), px logiska och mm skärmens
// (`Ctx::pxPerMm`). Utan storlek, med stilens egen storlek, utan textfonterna
// (ett Ctx utanför ett fönster) eller när fonten inte går att ladda är svaret
// `c` självt. Bara för TEXTEN: placeringen (`resolve`) får aldrig det
// ändrade Ctx:t, för den mäter också syskonen.
std::optional<Length> textSizeValue(const Length& size, const char* typ,
                                    const std::string& namn);
Ctx sizedText(const Ctx& c, const Length& size);

} // namespace glaze
