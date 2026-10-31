// Prov av sidopaketet OpenXR:s VR-tangentbord utan headset, runtime, fönster
// eller grafikenhet. De rena delarna (vr_keyboard_logic.h) provas direkt.
//
// Prövar:
//   L  layouterna: alla id unika, inga överlappande celler, ingen tangent
//      utanför kolumnerna, å ä ö i den svenska, vipporna Shift och Ctrl,
//      Backspace, Enter, Tab, Space och piltangenterna i båda
//   E  tangent → händelseföljd: KeyDown med texten, TextInput, KeyUp; en
//      kommandotangent utan text; Space; en tangent utan Key (å)
//   S  Shift väljer `shiftedText` (och faller tillbaka på `text`)
//   C  Ctrl ger ingen text och ingen TextInput, mods på tangenthändelserna
//   U  UTF-8 delad på teckengränser: aldrig mitt i ett tecken, i ordning,
//      varje bit ryms i `gfx::Event::text`, KeyDown bär första biten
//   M  en vippa ger inga händelser
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "vr_keyboard_logic.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace glaze::openxr;
using namespace glaze::openxr::detail;

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what, int line = 0)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FALL (rad " << line << "): " << what << "\n";
    }
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

const KeyboardKey* findKey(const KeyboardLayout& layout, const std::string& id)
{
    for (const KeyboardKey& k : layout.keys) {
        if (k.element.id == id)
            return &k;
    }
    return nullptr;
}

bool hasText(const KeyboardLayout& layout, const std::string& text)
{
    for (const KeyboardKey& k : layout.keys) {
        if (k.text == text)
            return true;
    }
    return false;
}

std::string textOf(const gfx::Event& e)
{
    return std::string(e.text);
}

KeyboardKey letterKey(gfx::Key key, const std::string& lower, const std::string& upper)
{
    KeyboardKey k;
    k.element.id  = lower;
    k.key         = key;
    k.text        = lower;
    k.shiftedText = upper;
    return k;
}

void layoutCases()
{
    for (KeyboardPreset preset : {KeyboardPreset::Swedish, KeyboardPreset::English}) {
        const std::string name = preset == KeyboardPreset::Swedish ? "svenska" : "engelska";
        const KeyboardLayout layout = keyboardLayout(preset);
        CHECK(!layout.keys.empty(), "L1 " + name + ": layouten har tangenter");
        CHECK(layout.columns > 0, "L1 " + name + ": kolumner");

        std::set<std::string> ids;
        std::set<std::pair<int, int>> cells;
        bool uniqueIds = true, noOverlap = true, inside = true, spanOk = true;
        for (const KeyboardKey& k : layout.keys) {
            if (!ids.insert(k.element.id).second)
                uniqueIds = false;
            if (k.element.id.empty())
                uniqueIds = false;
            const int span = k.element.columnSpan;
            if (span < 1)
                spanOk = false;
            if (k.element.column < 0 || k.element.column + span > layout.columns)
                inside = false;
            for (int c = 0; c < span; ++c) {
                if (!cells.insert({k.element.row, k.element.column + c}).second)
                    noOverlap = false;
            }
        }
        CHECK(uniqueIds, "L2 " + name + ": alla id unika och icke-tomma");
        CHECK(noOverlap, "L3 " + name + ": inga överlappande celler");
        CHECK(inside, "L4 " + name + ": ingen tangent utanför kolumnerna");
        CHECK(spanOk, "L5 " + name + ": columnSpan minst 1");

        // Bokstäverna a–z, siffrorna, skiljetecken och kommandotangenterna.
        for (char c = 'a'; c <= 'z'; ++c)
            CHECK(hasText(layout, std::string(1, c)), std::string("L6 ") + name + ": bokstaven " + c);
        for (char c = '0'; c <= '9'; ++c)
            CHECK(hasText(layout, std::string(1, c)), std::string("L7 ") + name + ": siffran " + c);
        CHECK(hasText(layout, ","), "L8 " + name + ": komma");
        CHECK(hasText(layout, "."), "L8 " + name + ": punkt");
        CHECK(hasText(layout, " "), "L8 " + name + ": blanksteg");

        const KeyboardKey* back = findKey(layout, "backspace");
        const KeyboardKey* enter = findKey(layout, "enter");
        const KeyboardKey* tab   = findKey(layout, "tab");
        const KeyboardKey* left  = findKey(layout, "left");
        const KeyboardKey* right = findKey(layout, "right");
        const KeyboardKey* space = findKey(layout, "space");
        CHECK(back && back->key == gfx::Key::Backspace && back->text.empty(), "L9 " + name + ": Backspace");
        CHECK(enter && enter->key == gfx::Key::Return && enter->text.empty(), "L9 " + name + ": Enter");
        CHECK(tab && tab->key == gfx::Key::Tab && tab->text.empty(), "L9 " + name + ": Tab");
        CHECK(left && left->key == gfx::Key::Left, "L9 " + name + ": vänster");
        CHECK(right && right->key == gfx::Key::Right, "L9 " + name + ": höger");
        CHECK(space && space->key == gfx::Key::Space && space->text == " ", "L9 " + name + ": Space");

        const KeyboardKey* shift = findKey(layout, "shift");
        const KeyboardKey* ctrl  = findKey(layout, "ctrl");
        CHECK(shift && shift->toggleModifier == gfx::kModShift && shift->element.kind == ElementKind::Toggle,
              "L10 " + name + ": Shift är en vippa");
        CHECK(ctrl && ctrl->toggleModifier == gfx::kModCtrl && ctrl->element.kind == ElementKind::Toggle,
              "L10 " + name + ": Ctrl är en vippa");
    }

    const KeyboardLayout sv = keyboardLayout(KeyboardPreset::Swedish);
    CHECK(hasText(sv, "\xC3\xA5"), "L11 svenska: å finns");
    CHECK(hasText(sv, "\xC3\xA4"), "L11 svenska: ä finns");
    CHECK(hasText(sv, "\xC3\xB6"), "L11 svenska: ö finns");
    const KeyboardKey* aring = findKey(sv, "aring");
    CHECK(aring && aring->shiftedText == "\xC3\x85", "L12 svenska: Å med Shift");
    const KeyboardKey* odia = findKey(sv, "odiaeresis");
    CHECK(odia && odia->shiftedText == "\xC3\x96", "L12 svenska: Ö med Shift");
    const KeyboardKey* two = findKey(sv, "n2");
    CHECK(two && two->shiftedText == "\"", "L13 svenska: Shift+2 är citattecken");

    const KeyboardLayout en = keyboardLayout(KeyboardPreset::English);
    CHECK(!hasText(en, "\xC3\xA5") && !hasText(en, "\xC3\xA4") && !hasText(en, "\xC3\xB6"),
          "L14 engelska: inga å ä ö");
    const KeyboardKey* enTwo = findKey(en, "n2");
    CHECK(enTwo && enTwo->shiftedText == "@", "L15 engelska: Shift+2 är snabel-a");
}

void eventCases()
{
    const KeyboardLayout sv = keyboardLayout(KeyboardPreset::Swedish);

    // E1 en bokstav: KeyDown (bär texten), TextInput, KeyUp.
    {
        const KeyboardKey* a = findKey(sv, "a");
        const auto ev = keyEvents(*a, 0);
        CHECK(ev.size() == 3, "E1 en bokstav ger tre händelser");
        if (ev.size() == 3) {
            CHECK(ev[0].type == gfx::EventType::KeyDown && ev[0].key == gfx::Key::A && ev[0].mods == 0 &&
                      textOf(ev[0]) == "a",
                  "E1 KeyDown A bär texten a");
            CHECK(ev[1].type == gfx::EventType::TextInput && textOf(ev[1]) == "a", "E1 TextInput a");
            CHECK(ev[2].type == gfx::EventType::KeyUp && ev[2].key == gfx::Key::A && textOf(ev[2]).empty(),
                  "E1 KeyUp A utan text");
        }
    }

    // E2 Backspace: bara KeyDown och KeyUp, ingen text.
    {
        const KeyboardKey* b = findKey(sv, "backspace");
        const auto ev = keyEvents(*b, 0);
        CHECK(ev.size() == 2, "E2 Backspace ger två händelser");
        if (ev.size() == 2) {
            CHECK(ev[0].type == gfx::EventType::KeyDown && ev[0].key == gfx::Key::Backspace &&
                      textOf(ev[0]).empty(),
                  "E2 KeyDown Backspace utan text");
            CHECK(ev[1].type == gfx::EventType::KeyUp && ev[1].key == gfx::Key::Backspace, "E2 KeyUp Backspace");
        }
    }

    // E3 Space skriver ett blanksteg.
    {
        const KeyboardKey* s = findKey(sv, "space");
        const auto ev = keyEvents(*s, 0);
        CHECK(ev.size() == 3 && ev[1].type == gfx::EventType::TextInput && textOf(ev[1]) == " ",
              "E3 Space skriver ett blanksteg");
    }

    // E4 å har ingen Key men skriver: KeyDown med Unknown och texten, TextInput, KeyUp.
    {
        const KeyboardKey* a = findKey(sv, "aring");
        const auto ev = keyEvents(*a, 0);
        CHECK(ev.size() == 3, "E4 å ger tre händelser");
        if (ev.size() == 3) {
            CHECK(ev[0].type == gfx::EventType::KeyDown && ev[0].key == gfx::Key::Unknown &&
                      textOf(ev[0]) == "\xC3\xA5",
                  "E4 KeyDown bär å");
            CHECK(ev[1].type == gfx::EventType::TextInput && textOf(ev[1]) == "\xC3\xA5", "E4 TextInput å");
            CHECK(ev[2].type == gfx::EventType::KeyUp, "E4 KeyUp");
        }
    }

    // S Shift väljer shiftedText; utan shiftedText gäller text.
    {
        const KeyboardKey* a = findKey(sv, "a");
        const auto ev = keyEvents(*a, gfx::kModShift);
        CHECK(ev.size() == 3, "S1 Shift+a ger tre händelser");
        if (ev.size() == 3) {
            CHECK(ev[0].key == gfx::Key::A && ev[0].mods == gfx::kModShift && textOf(ev[0]) == "A",
                  "S1 KeyDown A med Shift bär A");
            CHECK(textOf(ev[1]) == "A", "S1 TextInput A");
            CHECK(ev[2].type == gfx::EventType::KeyUp && ev[2].mods == gfx::kModShift, "S1 KeyUp bär Shift");
        }
        const KeyboardKey* odia = findKey(sv, "odiaeresis");
        const auto o = keyEvents(*odia, gfx::kModShift);
        CHECK(o.size() == 3 && textOf(o[1]) == "\xC3\x96", "S2 Shift+ö ger Ö");
        const KeyboardKey* two = findKey(sv, "n2");
        const auto t = keyEvents(*two, gfx::kModShift);
        CHECK(t.size() == 3 && t[0].key == gfx::Key::N2 && textOf(t[1]) == "\"",
              "S3 Shift+2 är N2 med citattecken");
        const KeyboardKey* b = findKey(sv, "backspace");
        const auto bs = keyEvents(*b, gfx::kModShift);
        CHECK(bs.size() == 2 && bs[0].mods == gfx::kModShift && textOf(bs[0]).empty(),
              "S4 Shift+Backspace utan text");
        const KeyboardKey* sp = findKey(sv, "space");
        const auto s = keyEvents(*sp, gfx::kModShift);
        CHECK(s.size() == 3 && textOf(s[1]) == " ", "S5 Space utan shiftedText faller på text");
    }

    // C Ctrl: ingen text, ingen TextInput, masken på tangenthändelserna.
    {
        const KeyboardKey* a = findKey(sv, "a");
        const auto ev = keyEvents(*a, gfx::kModCtrl);
        CHECK(ev.size() == 2, "C1 Ctrl+a ger två händelser");
        if (ev.size() == 2) {
            CHECK(ev[0].type == gfx::EventType::KeyDown && ev[0].key == gfx::Key::A &&
                      ev[0].mods == gfx::kModCtrl && textOf(ev[0]).empty(),
                  "C1 KeyDown A med Ctrl utan text");
            CHECK(ev[1].type == gfx::EventType::KeyUp && ev[1].mods == gfx::kModCtrl, "C1 KeyUp bär Ctrl");
        }
        const auto both = keyEvents(*a, gfx::kModCtrl | gfx::kModShift);
        CHECK(both.size() == 2 && both[0].mods == (gfx::kModCtrl | gfx::kModShift) &&
                  textOf(both[0]).empty(),
              "C2 Ctrl+Shift+a utan text, båda masker");
        // En tangent som bara är text (Key::Unknown) ger inget med Ctrl.
        const KeyboardKey* aring = findKey(sv, "aring");
        CHECK(keyEvents(*aring, gfx::kModCtrl).empty(), "C3 Ctrl+å utan Key ger inga händelser");
        // Piltangenten bär Ctrl.
        const KeyboardKey* left = findKey(sv, "left");
        const auto l = keyEvents(*left, gfx::kModCtrl);
        CHECK(l.size() == 2 && l[0].key == gfx::Key::Left && l[0].mods == gfx::kModCtrl, "C4 Ctrl+vänster");
    }

    // M en vippa ger inga händelser.
    {
        CHECK(keyEvents(*findKey(sv, "shift"), 0).empty(), "M1 Shift ger inga händelser");
        CHECK(keyEvents(*findKey(sv, "ctrl"), gfx::kModShift).empty(), "M1 Ctrl ger inga händelser");
    }
}

void utf8Cases()
{
    // U1 delningen: aldrig mitt i ett tecken, bitarna i ordning och hela.
    {
        std::string text;
        for (int i = 0; i < 20; ++i)
            text += "\xC3\xA5";   // 40 byte, 20 tecken
        const auto pieces = splitUtf8(text);
        std::string joined;
        bool fits = true, whole = true;
        for (const std::string& p : pieces) {
            joined += p;
            if (p.size() > kEventTextBytes)
                fits = false;
            if (!p.empty() && (static_cast<unsigned char>(p[0]) & 0xC0u) == 0x80u)
                whole = false;
            if (p.size() % 2 != 0)
                whole = false;   // varje å är två byte
        }
        CHECK(joined == text, "U1 bitarna ger tillbaka texten");
        CHECK(fits, "U1 varje bit ryms i Event::text");
        CHECK(whole, "U1 ingen bit börjar mitt i ett tecken");
        CHECK(pieces.size() == 2, "U1 40 byte ger två bitar (30 + 10)");
        if (pieces.size() == 2)
            CHECK(pieces[0].size() == 30, "U1 första biten backar till teckengränsen (30 byte)");
    }

    // U2 en kort text delas inte, en tom ger inga bitar.
    {
        CHECK(splitUtf8("abc").size() == 1, "U2 kort text är en bit");
        CHECK(splitUtf8("").empty(), "U2 tom text ger inga bitar");
        const std::string exact(kEventTextBytes, 'x');
        CHECK(splitUtf8(exact).size() == 1, "U2 exakt taket är en bit");
        const std::string over(kEventTextBytes + 1, 'x');
        const auto two = splitUtf8(over);
        CHECK(two.size() == 2 && two[0].size() == kEventTextBytes && two[1].size() == 1,
              "U2 ett byte över taket ger två bitar");
    }

    // U3 fyra byte långa tecken (emoji) hålls ihop.
    {
        std::string text;
        for (int i = 0; i < 9; ++i)
            text += "\xF0\x9F\x98\x80";   // 36 byte
        const auto pieces = splitUtf8(text);
        bool whole = true;
        std::string joined;
        for (const std::string& p : pieces) {
            joined += p;
            if (p.size() % 4 != 0 || p.size() > kEventTextBytes)
                whole = false;
        }
        CHECK(whole && joined == text, "U3 4-bytestecken delas aldrig");
    }

    // U4 trasig text utan teckengräns klipps vid taket i stället för att fastna.
    {
        const std::string broken(100, static_cast<char>(0x80));
        const auto pieces = splitUtf8(broken);
        std::string joined;
        for (const std::string& p : pieces)
            joined += p;
        CHECK(joined == broken && !pieces.empty(), "U4 trasig text tappas inte");
    }

    // U5 en tangent med lång text: KeyDown bär första biten, en TextInput per bit, KeyUp sist.
    {
        KeyboardKey k = letterKey(gfx::Key::Unknown, "", "");
        std::string text;
        for (int i = 0; i < 20; ++i)
            text += "\xC3\xA5";
        k.text = text;
        const auto ev = keyEvents(k, 0);
        CHECK(ev.size() == 4, "U5 KeyDown, två TextInput och KeyUp");
        if (ev.size() == 4) {
            CHECK(ev[0].type == gfx::EventType::KeyDown && textOf(ev[0]) == textOf(ev[1]),
                  "U5 KeyDown bär första biten");
            CHECK(ev[1].type == gfx::EventType::TextInput && ev[2].type == gfx::EventType::TextInput,
                  "U5 TextInput per bit");
            CHECK(textOf(ev[1]) + textOf(ev[2]) == text, "U5 bitarna sätts ihop till texten");
            CHECK(ev[3].type == gfx::EventType::KeyUp, "U5 KeyUp sist");
        }
        // Texten är NUL-terminerad också när biten fyller bufferten.
        const std::string full(kEventTextBytes, 'x');
        KeyboardKey f = letterKey(gfx::Key::X, full, "");
        const auto fe = keyEvents(f, 0);
        CHECK(fe.size() == 3 && fe[1].text[kEventTextBytes] == '\0' && textOf(fe[1]) == full,
              "U5 en full bit är NUL-terminerad");
    }

    // U6 typedText följer vipporna.
    {
        KeyboardKey k = letterKey(gfx::Key::A, "a", "A");
        CHECK(typedText(k, 0) == "a", "U6 ingen vippa: text");
        CHECK(typedText(k, gfx::kModShift) == "A", "U6 Shift: shiftedText");
        CHECK(typedText(k, gfx::kModCtrl).empty(), "U6 Ctrl: tom");
        KeyboardKey plain = letterKey(gfx::Key::A, "a", "");
        CHECK(typedText(plain, gfx::kModShift) == "a", "U6 Shift utan shiftedText: text");
    }
}

} // namespace

int main()
{
    layoutCases();
    eventCases();
    utf8Cases();
    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll\n";
        return 1;
    }
    std::printf("vr_keyboard_gate: alla %d kontroller gröna\n", g_checks);
    return 0;
}
