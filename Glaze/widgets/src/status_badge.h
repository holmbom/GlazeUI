#pragma once
#include "glaze/draw.h"
#include "glaze/message_box.h"
#include "glaze/widget.h"

// INTERN header (prejudikatet är `message_box_table.h` i samma mapp) —
// meddelanderutans IKON-BADGE.
//
// Den ligger för sig själv av ett enda skäl, och det är samma skäl som
// tabellens: den ska gå att GRINDA. `StatusBadge::contentSize()` är LEVANDE
// mätkod — `Widget::resolve` anropar `contentSize(c)` ovillkorligt, före all
// dock- och ankarlogik, och den här implementationen läser inte `width`/
// `height` alls utan svarar alltid med stilens `msgIconSize`. Att layouten
// sedan sätter badgens mått explicit gör den alltså INTE onåbar; värdena
// råkar bara sammanfalla. Ett fel i enheten här hade slagit igenom i
// badgens `resolve()` vid varje skala ≠ 1.
//
// Den är INTE publik: `glaze/message_box.h` är API:t, och en app har inget
// ärende till badgen — den ritas av rutan och av ingen annan.
//
// Klassen är `final`, så en grind kan inte ärva den. Den mäts i stället med
// den statiska, skyddade `Widget::measure(w, c)` via en `Meter`-subklass av
// BASEN — samma form som `measure_gate` använder.

namespace glaze {

// OREGISTRERAD widget — prejudikatet är `DropdownRows` i dropdown.cpp.
// Hård regel 3 gäller ändå FULLT ut: explicit `switch (c.ui)` med alla tre
// familjerna, alla mått ur `c.style` och alla färger ur `c.theme->colors`.
// Inga lokala ikonproportioner.
//
// FORMEN är samma för alla typer: en rund badge med en GLYF
// ritad som TEXT via fonten. Ingen varningstriangel och inget `Draw::cross`
// — ett konsekvent formspråk, och en glyf som är text håller hård regel 2
// (åäö och allt annat Unicode fonten täcker kommer gratis).
class StatusBadge final : public Widget {
public:
    explicit StatusBadge(MessageBoxType t) : type_(t) {}

protected:
    // LOGISKA px (mätkontraktet) — `Draw`-vägen och mätvägen talar samma
    // enhet, så stilmåttet går rakt igenom utan skalfaktor.
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        // Ingen familj har någon egen mening om en statusbadge — men att
        // grenarna STÅR här är poängen (regel 3): identisk ritning i tre
        // familjer ska vara skrivet, inte underförstått.
        case UI::Glaze:
        case UI::Minimalistic:
        case UI::Slate:
        default:
            return gfx::Sizef{c.style->msgIconSize, c.style->msgIconSize};
        }
    }

    void draw(const Ctx& c) override
    {
        switch (c.ui) {
        case UI::Glaze:
        case UI::Minimalistic:
        case UI::Slate:
        default:
            Draw::circle(c, c.bounds(), surface(c));
            // GLYFEN som TEXT, i RUBRIKENS font. `Label::roleCtx` är
            // privat, så mönstret kopieras — metoden anropas inte.
            // Fallbacken är brödtextens font: `headingFont` FÅR vara null.
            {
                Ctx rc = c;
                rc.font = c.headingFont ? c.headingFont : c.font;
                Draw::text(rc, c.bounds(), glyph(), onSurface(c),
                           Align::Center);
            }
            break;
        }
    }

private:
    // Information och Confirmation säger "här är något", vilket accenten
    // redan betyder — de återanvänder därför `accentSurface`/`onAccent`.
    // Bara varning och fel har en betydelse som färgen själv bär.
    gfx::Color surface(const Ctx& c) const
    {
        const auto& col = c.theme->colors;
        switch (type_) {
        case MessageBoxType::Warning: return col.warningSurface;
        case MessageBoxType::Error:   return col.errorSurface;
        default:                      return col.accentSurface;
        }
    }
    gfx::Color onSurface(const Ctx& c) const
    {
        const auto& col = c.theme->colors;
        switch (type_) {
        case MessageBoxType::Warning: return col.onWarning;
        case MessageBoxType::Error:   return col.onError;
        default:                      return col.onAccent;
        }
    }
    const char* glyph() const
    {
        switch (type_) {
        case MessageBoxType::Confirmation: return "?";
        case MessageBoxType::Warning:      return "!";
        case MessageBoxType::Error:        return "\xc3\x97";   // × U+00D7
        default:                           return "i";
        }
    }

    MessageBoxType type_;
};

} // namespace glaze
