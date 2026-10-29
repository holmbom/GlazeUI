// VT-tolken — Williams tillståndsmaskin, kontraktet i parser.h. Övergångarna
// står i samma ordning som i diagrammet på vt100.net: först "anywhere"
// (ESC, CAN, SUB), sedan per tillstånd.

#include "glaze_terminal/parser.h"

namespace glaze::term {
namespace {

constexpr char32_t kReplacement = 0xFFFD;

bool isC0(char32_t c) { return c < 0x20; }
bool inRange(char32_t c, char32_t a, char32_t b) { return c >= a && c <= b; }

void appendUtf8(std::string& s, char32_t cp)
{
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

void Parser::reset()
{
    state_   = State::Ground;
    utfNeed_ = 0;
    clear();
    osc_.clear();
}

// UTF-8 → kodpunkter. En följebyte som inte kommer, en överlång form, en
// surrogat och allt över U+10FFFF blir U+FFFD; den byte som bröt en följd
// tolkas sedan på nytt.
void Parser::feed(std::string_view bytes)
{
    for (const char ch : bytes) {
        const auto b = static_cast<std::uint8_t>(ch);
        if (utfNeed_ > 0) {
            if ((b & 0xC0) == 0x80) {
                utf_ = (utf_ << 6) | (b & 0x3Fu);
                if (--utfNeed_ == 0) {
                    const bool ok = utf_ >= utfMin_ && utf_ <= 0x10FFFF &&
                                    !(utf_ >= 0xD800 && utf_ <= 0xDFFF);
                    codepoint(ok ? utf_ : kReplacement);
                }
                continue;
            }
            utfNeed_ = 0;
            codepoint(kReplacement);
        }
        if (b < 0x80) {
            codepoint(b);
        } else if ((b & 0xE0) == 0xC0) {
            utf_ = b & 0x1Fu;  utfNeed_ = 1;  utfMin_ = 0x80;
        } else if ((b & 0xF0) == 0xE0) {
            utf_ = b & 0x0Fu;  utfNeed_ = 2;  utfMin_ = 0x800;
        } else if ((b & 0xF8) == 0xF0) {
            utf_ = b & 0x07u;  utfNeed_ = 3;  utfMin_ = 0x10000;
        } else {
            codepoint(kReplacement);   // en ensam följebyte eller F8–FF
        }
    }
}

void Parser::clear()
{
    params_      = CsiParams{};
    paramsFull_  = false;
    prefix_      = 0;
    interLen_    = 0;
}

void Parser::collect(char c)
{
    if (interLen_ < static_cast<int>(sizeof inter_))
        inter_[interLen_++] = c;
}

// Ett parametertecken: siffra, `;` (nytt fält) eller `:` (ny underparameter).
void Parser::param(char c)
{
    if (params_.count == 0) {
        params_.count    = 1;
        params_.value[0] = -1;
    }
    if (c == ';' || c == ':') {
        if (params_.count == CsiParams::kMax) {
            paramsFull_ = true;
            return;
        }
        params_.value[params_.count] = -1;
        params_.sub[params_.count]   = c == ':';
        ++params_.count;
        return;
    }
    if (paramsFull_)
        return;
    int& v = params_.value[params_.count - 1];
    v      = (v < 0 ? 0 : v) * 10 + (c - '0');
    if (v > 65535)
        v = 65535;
}

void Parser::endOsc(bool bel)
{
    sink_.oscDispatch(osc_, bel);
    osc_.clear();
}

void Parser::codepoint(char32_t cp)
{
    // ---- anywhere
    if (cp == 0x1B) {
        if (state_ == State::OscString)
            endOsc(false);   // ESC \ — ST; `\` blir en tom ESC-sekvens
        clear();
        state_ = State::Escape;
        return;
    }
    if (cp == 0x18 || cp == 0x1A) {   // CAN, SUB — avbryter vad som helst
        osc_.clear();
        sink_.execute(static_cast<std::uint8_t>(cp));
        state_ = State::Ground;
        return;
    }
    if (inRange(cp, 0x80, 0x9F))
        return;   // C1 i UTF-8-form — se parser.h

    const char c = cp < 0x80 ? static_cast<char>(cp) : 0;
    switch (state_) {
    case State::Ground:
        if (isC0(cp))
            sink_.execute(static_cast<std::uint8_t>(cp));
        else if (cp != 0x7F)
            sink_.print(cp);
        return;

    case State::Escape:
        if (isC0(cp)) {
            sink_.execute(static_cast<std::uint8_t>(cp));
        } else if (cp == 0x7F) {
        } else if (inRange(cp, 0x20, 0x2F)) {
            collect(c);
            state_ = State::EscapeIntermediate;
        } else if (cp == '[') {
            clear();
            state_ = State::CsiEntry;
        } else if (cp == ']') {
            osc_.clear();
            state_ = State::OscString;
        } else if (cp == 'P') {
            clear();
            state_ = State::DcsEntry;
        } else if (cp == 'X' || cp == '^' || cp == '_') {
            state_ = State::SosPmApcString;
        } else if (cp < 0x7F) {
            sink_.escDispatch(std::string_view(inter_, interLen_), c);
            state_ = State::Ground;
        } else {
            state_ = State::Ground;
        }
        return;

    case State::EscapeIntermediate:
        if (isC0(cp)) {
            sink_.execute(static_cast<std::uint8_t>(cp));
        } else if (inRange(cp, 0x20, 0x2F)) {
            collect(c);
        } else if (inRange(cp, 0x30, 0x7E)) {
            sink_.escDispatch(std::string_view(inter_, interLen_), c);
            state_ = State::Ground;
        } else if (cp != 0x7F) {
            state_ = State::Ground;
        }
        return;

    case State::CsiEntry:
    case State::CsiParam:
        if (isC0(cp)) {
            sink_.execute(static_cast<std::uint8_t>(cp));
        } else if (inRange(cp, '0', '9') || cp == ';' || cp == ':') {
            param(c);
            state_ = State::CsiParam;
        } else if (inRange(cp, 0x3C, 0x3F)) {
            // Det privata tecknet står bara FÖRST; senare är sekvensen trasig.
            if (state_ == State::CsiEntry) {
                prefix_ = c;
                state_  = State::CsiParam;
            } else {
                state_ = State::CsiIgnore;
            }
        } else if (inRange(cp, 0x20, 0x2F)) {
            collect(c);
            state_ = State::CsiIntermediate;
        } else if (inRange(cp, 0x40, 0x7E)) {
            sink_.csiDispatch(prefix_, params_, std::string_view(inter_, interLen_), c);
            state_ = State::Ground;
        } else if (cp != 0x7F) {
            state_ = State::CsiIgnore;
        }
        return;

    case State::CsiIntermediate:
        if (isC0(cp)) {
            sink_.execute(static_cast<std::uint8_t>(cp));
        } else if (inRange(cp, 0x20, 0x2F)) {
            collect(c);
        } else if (inRange(cp, 0x40, 0x7E)) {
            sink_.csiDispatch(prefix_, params_, std::string_view(inter_, interLen_), c);
            state_ = State::Ground;
        } else if (cp != 0x7F) {
            state_ = State::CsiIgnore;
        }
        return;

    case State::CsiIgnore:
        if (isC0(cp))
            sink_.execute(static_cast<std::uint8_t>(cp));
        else if (inRange(cp, 0x40, 0x7E))
            state_ = State::Ground;
        return;

    case State::OscString:
        if (cp == 0x07) {
            endOsc(true);
            state_ = State::Ground;
        } else if (!isC0(cp) && osc_.size() < kOscMax) {
            appendUtf8(osc_, cp);
        }
        return;

    // DCS: formen följs till slutet, innehållet sväljs (parser.h).
    case State::DcsEntry:
    case State::DcsParam:
        if (inRange(cp, 0x20, 0x2F))
            state_ = State::DcsIntermediate;
        else if (inRange(cp, '0', '9') || cp == ';' || cp == ':')
            state_ = State::DcsParam;
        else if (inRange(cp, 0x3C, 0x3F))
            state_ = state_ == State::DcsEntry ? State::DcsParam : State::DcsIgnore;
        else if (inRange(cp, 0x40, 0x7E))
            state_ = State::DcsPassthrough;
        return;

    case State::DcsIntermediate:
        if (inRange(cp, 0x30, 0x3F))
            state_ = State::DcsIgnore;
        else if (inRange(cp, 0x40, 0x7E))
            state_ = State::DcsPassthrough;
        return;

    case State::DcsPassthrough:
    case State::DcsIgnore:
    case State::SosPmApcString:
        return;   // till ST (ESC), CAN eller SUB — "anywhere" ovan
    }
}

} // namespace glaze::term
