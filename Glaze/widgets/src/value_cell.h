#pragma once
#include <limits>
#include <optional>
#include <string>

#include <gfx/color.h>

#include "glaze/draw.h"
#include "glaze/widget.h"
#include "glaze/widgets/checkbox.h"       // CheckState
#include "glaze/widgets/color_picker.h"   // ColorDisplay

// DE TYPADE CELLERNA — intern, delad av PropertyGrid och DataGrid (och
// listvyns och kolumnhuvudets kryssruta): textformerna, tolkningen, måtten och
// ritningen av en kryss-, färg-, tal-, knapp- och kategoricell. EN kod för
// alla, så en talcell i datagriden och en talrad i egenskapslistan aldrig kan
// tolka, klampa eller se olika ut. Inget statiskt tillstånd.
//
// TEXTFORMERNA är lagringen: Check "true"/"false" (läses också "yes"/"1"),
// Color `#RRGGBB` (`#RRGGBBAA` med alfa), Number den kanoniska formen med
// punkt. Visningen av ett tal använder språkets decimaltecken (`sep`), och
// användaren får skriva både `,` och `.`.
//
// GEOMETRIN är widget-relativa LOGISKA px: `content` är cellen minus cellens
// luft (och, i en tabell, indraget, zonen, ikonen och cellens bild). Inga tal
// och inga färger här: måtten är `c.style->*` och färgerna `c.theme->colors.*`.

namespace glaze::valuecell {

// ---------------------------------------------------------------- KRYSSET
bool        parseCheck(const std::string& value);   // "true"/"yes"/"1"
std::string checkText(bool on);                     // "true"/"false"

// ---------------------------------------------------------------- FÄRGEN
// `nullopt` för en otolkbar text. Utan `allowAlpha` är färgen OPAK
// (färgväljarens läsregel). `colorText` är den kanoniska hextexten.
std::optional<gfx::Color> parseColor(const std::string& value, bool allowAlpha);
std::string               colorText(gfx::Color c, bool allowAlpha);

// ------------------------------------------------------------------ TALET
struct NumberSpec {
    double      min = -std::numeric_limits<double>::infinity();
    double      max = std::numeric_limits<double>::infinity();
    int         decimals = -1;   // −1 kortaste formen, 0–9 fasta
    std::string prefix;          // före talet
    std::string suffix;          // efter talet
    bool        allowEmpty = false;   // tom text = ett giltigt tomt värde
    std::string emptyText;            // det som visas när värdet är tomt
};

// Är appens text ett TOMT värde (`allowEmpty` och bara blanktecken)?
bool isEmpty(const std::string& value, const NumberSpec& spec);
// Det effektiva talet — klampat och avrundat — ur appens text; `nullopt` för
// en otolkbar eller tom text.
std::optional<double> numberValue(const std::string& value, const NumberSpec& spec);
// Felet: varken ett tal eller ett tillåtet tomt värde.
bool numberError(const std::string& value, const NumberSpec& spec);

// SPARNINGEN av användarens text: Ok med den kanoniska texten (klampad,
// avrundad, punkt), Empty med en tom text (bara med `allowEmpty`), annars
// Invalid — värdet står då kvar och anroparen sparar texten som väntande fel.
enum class Commit { Ok, Empty, Invalid };
struct NumberCommit {
    Commit      result = Commit::Invalid;
    std::string canonical;   // Ok: den nya texten; Empty: ""
};
NumberCommit commitNumber(const std::string& userText, const NumberSpec& spec);
// Är den nya kanoniska texten SAMMA värde som den gamla (efter normaliseringen
// — "12,50" mot "12.5" är samma)? Då skrivs och fyras ingenting.
bool sameNumber(const std::string& oldValue, const std::string& canonical,
                const NumberSpec& spec);

// VISNINGEN: användarens väntande otolkbara text (`pending`, får vara nullptr)
// vinner, sedan det effektiva talet med `sep`, sedan `emptyText` när värdet är
// tomt, sedan appens råtext. `editNumberText` är det fältet BÖRJAR med: samma,
// utom att ett tomt värde ger en tom text.
std::string displayNumber(const std::string& value, const std::string* pending,
                          const NumberSpec& spec, const std::string& sep);
std::string editNumberText(const std::string& value, const std::string* pending,
                           const NumberSpec& spec, const std::string& sep);

// ------------------------------------------------------------- GEOMETRIN
// Rutan (kryssets och färgens) i `content`: v-centrerad, vid kanten som
// `align` säger (Left vid `content.x`, Center i mitten, Right vid högerkanten).
gfx::Rectf boxRect(const Ctx& c, const gfx::Rectf& content, Align align);
// Talets FÄLT mellan prefixet och suffixet — ritningen och editorn ställer
// sig i samma yta.
gfx::Rectf numberFieldRect(const Ctx& c, const gfx::Rectf& content,
                           const NumberSpec& spec);

// ---------------------------------------------------------------- MÅTTEN
// Innehållets bredd i logiska px, UTAN cellens luft.
float checkW(const Ctx& c);
// Färgens, per `ColorDisplay`: Swatch rutan (`colorCellSwatchW`), Hex
// hextexten, Both rutan (`checkSize`), mellanrummet och hextexten; en
// otolkbar text är textens bredd.
float colorW(const Ctx& c, const std::string& value, bool allowAlpha,
             ColorDisplay display);
float numberW(const Ctx& c, const std::string& display, const NumberSpec& spec);
float buttonW(const Ctx& c, const std::string& text, const ImageSource& img);

// ------------------------------------------------------------- RITNINGEN
// RUTAN i tre lägen med givna färger (kant, bock) — listvyns, kolumnhuvudets
// och cellernas: kontur med `checkRadius`, bocken med `checkStroke`, det
// tredje läget som ett streck (`Draw::checkDash`).
void drawCheckBox(const Ctx& c, const gfx::Rectf& box, CheckState state,
                  gfx::Color kant, gfx::Color bock);
// KRYSSCELLEN: rutan i `content` vid `align`, i temats färger — kanten
// `buttonBorder` (inaktiv `border`), bocken `fg`.
void drawCheck(const Ctx& c, const gfx::Rectf& content, Align align, bool on,
               bool enabled, gfx::Color fg);
// FÄRGCELLEN per `ColorDisplay`, som ETT block vid `align`: Swatch rutan
// `colorCellSwatchW` bred och `checkSize` hög, Hex bara hextexten i `fg`,
// Both rutan i `checkSize` och hextexten efter den. Rutan ritas över ett
// rutmönster med alfa och tonas med `iconFade` när inaktiv. En otolkbar text
// ritas som text oavsett läget.
void drawColor(const Ctx& c, const gfx::Rectf& content, Align align,
               const std::string& value, bool allowAlpha, ColorDisplay display,
               bool enabled, gfx::Color fg);
// TALCELLEN: prefixet vänster, suffixet högerställt och visningstexten
// emellan (`numberFieldRect`), i `fg`. `align` gäller talet i fältet.
void drawNumber(const Ctx& c, const gfx::Rectf& content, Align align,
                const std::string& display, const NumberSpec& spec,
                gfx::Color fg);
// FELET: cellen fylls med `errorSurface`; texten ritas sedan i `errorText`.
void       drawError(const Ctx& c, const gfx::Rectf& cell);
gfx::Color errorText(const Ctx& c);
// KNAPPCELLEN: en platt knapp `cellButtonInset` in från cellens kanter, med
// bilden och texten centrerade — per UI med knappens färger och utan skugga,
// tryckt och hovrad som en knapp, dämpad när inaktiv.
void drawCellButton(const Ctx& c, const gfx::Rectf& cell, const std::string& text,
                    const ImageSource& img, bool enabled, bool pressed,
                    bool hovered);
// KATEGORIRADEN: radens yta i `panelHeader`, pilen (ner = utfälld) i
// expanderns kolumn `padX` in, bilden och texten efter den — i `text`
// (inaktiv `textDim`). PropertyGrid och Table delar den.
void drawCategoryRow(const Ctx& c, const gfx::Rectf& r, const std::string& text,
                     const ImageSource& img, bool expanded, bool enabled,
                     float padX);

// ------------------------------------------------------------- TOOLTIPEN
TrText invalidToolTip();   // "Ogiltigt värde"

} // namespace glaze::valuecell
