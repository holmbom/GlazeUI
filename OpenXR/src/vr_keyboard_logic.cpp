#include "vr_keyboard_logic.h"

#include <algorithm>
#include <cstring>

namespace glaze::openxr {

namespace {

constexpr double kKeyWidth = 0.04;    // en tangents bredd i layouterna (meter)
constexpr double kKeyGap   = 0.008;   // mellanrummet mellan tangenter (panelens `spacing`)

// Bygger en layout rad för rad: raden och kolumnen nästa tangent hamnar på.
class Builder {
public:
    explicit Builder(int columns) { layout_.columns = columns; }

    // En tangent som skriver `text` (med Shift `shifted`); `id` är unikt i layouten.
    void character(const char* id, gfx::Key key, const char* label, const char* text,
                   const char* shifted = "", int span = 1)
    {
        KeyboardKey k = base(id, label, span);
        k.key         = key;
        k.text        = text;
        k.shiftedText = shifted;
        add(std::move(k), span);
    }

    // En tangent som bara skickar tangenten (Backspace, Enter, piltangenter).
    void command(const char* id, gfx::Key key, const char* label, int span = 1)
    {
        KeyboardKey k = base(id, label, span);
        k.key         = key;
        add(std::move(k), span);
    }

    // Space skriver ett blanksteg.
    void space(int span) { character("space", gfx::Key::Space, "Space", " ", "", span); }

    // En vippa för Shift eller Ctrl.
    void modifier(const char* id, std::uint32_t mask, const char* label, int span = 1)
    {
        KeyboardKey k    = base(id, label, span);
        k.element.kind   = ElementKind::Toggle;
        k.toggleModifier = mask;
        add(std::move(k), span);
    }

    void endRow()
    {
        ++row_;
        column_ = 0;
    }

    KeyboardLayout take() { return std::move(layout_); }

private:
    static KeyboardKey base(const char* id, const char* label, int span)
    {
        KeyboardKey k;
        k.element.id           = id;
        k.element.kind         = ElementKind::Button;
        k.element.content.text = label;
        if (span > 1) {
            // Bredare tangenter fyller sina celler.
            ElementStyle s;
            s.width         = kKeyWidth * span + kKeyGap * (span - 1);
            k.element.style = s;
        }
        return k;
    }

    void add(KeyboardKey k, int span)
    {
        k.element.row        = row_;
        k.element.column     = column_;
        k.element.columnSpan = span;
        column_ += span;
        layout_.keys.push_back(std::move(k));
    }

    KeyboardLayout layout_;
    int            row_    = 0;
    int            column_ = 0;
};

struct Letter {
    const char* id;
    gfx::Key    key;
    const char* lower;
    const char* upper;
};

// Bokstäverna a–z i de tre raderna, gemensamma för layouterna.
constexpr Letter kTopLetters[] = {
    {"q", gfx::Key::Q, "q", "Q"}, {"w", gfx::Key::W, "w", "W"}, {"e", gfx::Key::E, "e", "E"},
    {"r", gfx::Key::R, "r", "R"}, {"t", gfx::Key::T, "t", "T"}, {"y", gfx::Key::Y, "y", "Y"},
    {"u", gfx::Key::U, "u", "U"}, {"i", gfx::Key::I, "i", "I"}, {"o", gfx::Key::O, "o", "O"},
    {"p", gfx::Key::P, "p", "P"},
};
constexpr Letter kHomeLetters[] = {
    {"a", gfx::Key::A, "a", "A"}, {"s", gfx::Key::S, "s", "S"}, {"d", gfx::Key::D, "d", "D"},
    {"f", gfx::Key::F, "f", "F"}, {"g", gfx::Key::G, "g", "G"}, {"h", gfx::Key::H, "h", "H"},
    {"j", gfx::Key::J, "j", "J"}, {"k", gfx::Key::K, "k", "K"}, {"l", gfx::Key::L, "l", "L"},
};
constexpr Letter kBottomLetters[] = {
    {"z", gfx::Key::Z, "z", "Z"}, {"x", gfx::Key::X, "x", "X"}, {"c", gfx::Key::C, "c", "C"},
    {"v", gfx::Key::V, "v", "V"}, {"b", gfx::Key::B, "b", "B"}, {"n", gfx::Key::N, "n", "N"},
    {"m", gfx::Key::M, "m", "M"},
};

void letter(Builder& b, const Letter& l)
{
    b.character(l.id, l.key, l.lower, l.lower, l.upper);
}

template <std::size_t N>
void letterRow(Builder& b, const Letter (&letters)[N])
{
    for (const Letter& l : letters)
        letter(b, l);
}

// Siffrorna 1 … 0 (med sina Shift-tecken) och Backspace.
void numberRow(Builder& b, const char* const (&shifted)[10])
{
    static const struct {
        const char* id;
        gfx::Key    key;
        const char* text;
    } digits[10] = {
        {"n1", gfx::Key::N1, "1"}, {"n2", gfx::Key::N2, "2"}, {"n3", gfx::Key::N3, "3"},
        {"n4", gfx::Key::N4, "4"}, {"n5", gfx::Key::N5, "5"}, {"n6", gfx::Key::N6, "6"},
        {"n7", gfx::Key::N7, "7"}, {"n8", gfx::Key::N8, "8"}, {"n9", gfx::Key::N9, "9"},
        {"n0", gfx::Key::N0, "0"},
    };
    for (int i = 0; i < 10; ++i)
        b.character(digits[i].id, digits[i].key, digits[i].text, digits[i].text, shifted[i]);
    b.command("backspace", gfx::Key::Backspace, "Bksp", 2);
    b.endRow();
}

// Sista raden: Ctrl, Tab, Space, vänster, höger och Enter (tolv kolumner).
void bottomRow(Builder& b)
{
    b.modifier("ctrl", gfx::kModCtrl, "Ctrl", 2);
    b.command("tab", gfx::Key::Tab, "Tab");
    b.space(5);
    b.command("left", gfx::Key::Left, "<");
    b.command("right", gfx::Key::Right, ">");
    b.command("enter", gfx::Key::Return, "Enter", 2);
    b.endRow();
}

KeyboardLayout swedishLayout()
{
    static const char* const shiftedDigits[10] = {"!", "\"", "#", "\xC2\xA4", "%", "&", "/", "(", ")", "="};

    Builder b(12);
    numberRow(b, shiftedDigits);
    letterRow(b, kTopLetters);
    letter(b, {"aring", gfx::Key::Unknown, "\xC3\xA5", "\xC3\x85"});
    b.character("apostrophe", gfx::Key::Apostrophe, "'", "'", "*");
    b.endRow();
    letterRow(b, kHomeLetters);
    letter(b, {"odiaeresis", gfx::Key::Unknown, "\xC3\xB6", "\xC3\x96"});
    letter(b, {"adiaeresis", gfx::Key::Unknown, "\xC3\xA4", "\xC3\x84"});
    b.character("plus", gfx::Key::Plus, "+", "+", "?");
    b.endRow();
    b.modifier("shift", gfx::kModShift, "Shift", 2);
    letterRow(b, kBottomLetters);
    b.character("comma", gfx::Key::Comma, ",", ",", ";");
    b.character("period", gfx::Key::Period, ".", ".", ":");
    b.character("minus", gfx::Key::Minus, "-", "-", "_");
    b.endRow();
    bottomRow(b);
    return b.take();
}

KeyboardLayout englishLayout()
{
    static const char* const shiftedDigits[10] = {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"};

    Builder b(12);
    numberRow(b, shiftedDigits);
    letterRow(b, kTopLetters);
    b.character("minus", gfx::Key::Minus, "-", "-", "_");
    b.character("equals", gfx::Key::Equals, "=", "=", "+");
    b.endRow();
    letterRow(b, kHomeLetters);
    b.character("semicolon", gfx::Key::Semicolon, ";", ";", ":");
    b.character("apostrophe", gfx::Key::Apostrophe, "'", "'", "\"");
    b.character("slash", gfx::Key::Slash, "/", "/", "?");
    b.endRow();
    b.modifier("shift", gfx::kModShift, "Shift", 2);
    letterRow(b, kBottomLetters);
    b.character("comma", gfx::Key::Comma, ",", ",", "<");
    b.character("period", gfx::Key::Period, ".", ".", ">");
    b.endRow();
    bottomRow(b);
    return b.take();
}

} // namespace

KeyboardLayout keyboardLayout(KeyboardPreset preset)
{
    return preset == KeyboardPreset::English ? englishLayout() : swedishLayout();
}

namespace detail {

std::vector<std::string> splitUtf8(const std::string& text, std::size_t maxBytes)
{
    std::vector<std::string> out;
    if (maxBytes == 0)
        return out;
    const auto isContinuation = [](char c) { return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u; };
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = std::min(text.size(), pos + maxBytes);
        if (end < text.size()) {
            // Backa till ett tecken börjar; finns ingen sådan gräns (trasig
            // text) klipps det vid taket.
            std::size_t cut = end;
            while (cut > pos && isContinuation(text[cut]))
                --cut;
            if (cut > pos)
                end = cut;
        }
        out.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

const std::string& typedText(const KeyboardKey& key, std::uint32_t mods)
{
    static const std::string none;
    if ((mods & gfx::kModCtrl) != 0)
        return none;
    if ((mods & gfx::kModShift) != 0 && !key.shiftedText.empty())
        return key.shiftedText;
    return key.text;
}

std::vector<gfx::Event> keyEvents(const KeyboardKey& key, std::uint32_t mods)
{
    std::vector<gfx::Event> out;
    if (key.toggleModifier != 0)
        return out;
    const std::vector<std::string> pieces = splitUtf8(typedText(key, mods));
    if (key.key == gfx::Key::Unknown && pieces.empty())
        return out;

    const auto setText = [](gfx::Event& e, const std::string& s) {
        std::memcpy(e.text, s.data(), s.size());
        e.text[s.size()] = '\0';
    };

    gfx::Event down;
    down.type = gfx::EventType::KeyDown;
    down.key  = key.key;
    down.mods = mods;
    if (!pieces.empty())
        setText(down, pieces.front());
    out.push_back(down);

    for (const std::string& piece : pieces) {
        gfx::Event text;
        text.type = gfx::EventType::TextInput;
        setText(text, piece);
        out.push_back(text);
    }

    gfx::Event up;
    up.type = gfx::EventType::KeyUp;
    up.key  = key.key;
    up.mods = mods;
    out.push_back(up);
    return out;
}

} // namespace detail
} // namespace glaze::openxr
