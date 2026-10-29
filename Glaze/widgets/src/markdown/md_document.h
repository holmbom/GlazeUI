#pragma once
#include <cstdint>
#include <string>
#include <vector>

// MARKDOWN-DOKUMENTET — det `md::parse` ger och
// layouten (`md_layout.h`) läser. INTERNT och REN STD: inga glaze-typer, så
// parsern grindas headless (`markdown_gate` del P) som `date_model.h`.
//
// FORMEN ÄR PLATT DÄR DEN KAN: ett stycke är en lista SPANS med stilbitar och
// inte ett träd av betoningar — layouten ritar en rad i taget och behöver
// bara veta vilken font, vilken länk och vilken bild varje bit har. Blocken
// är däremot ett träd (citat i listor i citat), nästlat högst `kMaxNesting`
// djupt (md_parse.cpp).

namespace glaze::md {

// Stilbitarna på en span — fet, kursiv och kod. Kod ritas i Mono och tar
// ingen fetstil (en rubrik med kod har kod i rubrikens storlek).
enum : std::uint8_t { kBold = 1, kItalic = 2, kCode = 4 };

// En BIT TEXT med en stil, eller en bild, eller en hård radbrytning.
//   Text   `text` är texten; mjuka radbrytningar har redan blivit blanksteg
//   Image  `text` är alt-texten PLATT (betoningen i den syns inte), och
//          `image` pekar ut källan i `Document::images`
//   Break  en hård radbrytning (två blanksteg eller `\` sist på raden)
// `link` är länken spannet står i (`Document::links`), −1 utan länk. En
// autolänk inne i en länktext är den innersta och vinner.
struct Span {
    enum class Kind : std::uint8_t { Text, Image, Break };
    Kind         kind  = Kind::Text;
    std::string  text;
    std::uint8_t style = 0;
    int          link  = -1;
    int          image = -1;
};

// Målet som det står i dokumentet, med escapes och entiteter avkodade.
struct Link {
    std::string url;
    std::string title;
};

// Bildens källa som den står (`![alt](src "titel")`) — widgeten slår upp den.
struct Image {
    std::string src;
    std::string title;
};

// Tabellkolumnens justering. Ett EGET enum och inte `glaze::Align`: det hade
// dragit in draw.h i en header som ska vara ren std. En kolumn utan kolon i
// avgränsningsraden är Left.
enum class CellAlign : std::uint8_t { Left, Center, Right };

// ETT BLOCK. Fälten används per sort:
//   Paragraph  `spans`
//   Heading    `level` (1–6), `spans`, `anchor` (unik slug — `md::slug` och
//              `-1`, `-2` för dubbletter)
//   Code       `code` — raderna med `\n` emellan, tabbar expanderade till
//              stopp 4 (inga avslutande radbrytningar)
//   Quote      `children`
//   List       `ordered`, `tight`, `start` (den numrerade), `delim` (`.`/`)`
//              eller punktens tecken `-`/`*`/`+`), `children` = Item
//   Item       `children`
//   Rule       —
//   Table      `align` (en per kolumn) och `rows` — rad 0 är huvudet, varje
//              rad har exakt `align.size()` celler (en kort rad är utfylld,
//              en lång kapad)
struct Block {
    enum class Kind : std::uint8_t {
        Paragraph,
        Heading,
        Code,
        Quote,
        List,
        Item,
        Rule,
        Table
    };
    Kind                                        kind  = Kind::Paragraph;
    int                                         level = 0;
    std::vector<Span>                           spans;
    std::string                                 code;
    std::vector<Block>                          children;
    bool                                        ordered = false;
    bool                                        tight   = true;
    int                                         start   = 1;
    char                                        delim   = 0;
    std::string                                 anchor;
    std::vector<CellAlign>                      align;
    std::vector<std::vector<std::vector<Span>>> rows;
};

struct Document {
    std::vector<Block> blocks;
    std::vector<Link>  links;
    std::vector<Image> images;
};

} // namespace glaze::md
