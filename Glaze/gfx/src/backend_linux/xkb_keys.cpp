// xkb → gfx::Key (se .h). Kontraktet står vid `enum class Key` i
// gfx/events.h — level-0-symbolen i den aktiva layouten, numpaden ur det
// faktiska läget.

#include "xkb_keys.h"

namespace gfx {
namespace {

// NUMPADEN, ur tangentbordets FAKTISKA läge — siffrorna och operatorerna
// OCH navigationsrollerna, som har egna poster
// (`KP_End` …) i stället för att smälta ihop med de dedikerade
// tangenterna. NumLock av ger alltså `KP_End`, på `KP_N1`; widgetarna ser
// skillnaden aldrig, för `AppWindow` viker tillbaka (gfx::navigationKey).
//
// `KP_Enter` saknas med FLIT — den fälls ihop med `Return` nedan.
// `KP_Prior`/`KP_Next` är samma keysyms som `KP_Page_Up`/`KP_Page_Down`.
Key keypadKey(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
        return static_cast<Key>(static_cast<int>(Key::KP_N0) +
                                (sym - XKB_KEY_KP_0));
    switch (sym) {
    case XKB_KEY_KP_Add:      return Key::KP_Add;
    case XKB_KEY_KP_Subtract: return Key::KP_Subtract;
    case XKB_KEY_KP_Multiply: return Key::KP_Multiply;
    case XKB_KEY_KP_Divide:   return Key::KP_Divide;
    case XKB_KEY_KP_Decimal:
    case XKB_KEY_KP_Separator: return Key::KP_Decimal;
    case XKB_KEY_KP_Home:      return Key::KP_Home;
    case XKB_KEY_KP_End:       return Key::KP_End;
    case XKB_KEY_KP_Left:      return Key::KP_Left;
    case XKB_KEY_KP_Right:     return Key::KP_Right;
    case XKB_KEY_KP_Up:        return Key::KP_Up;
    case XKB_KEY_KP_Down:      return Key::KP_Down;
    case XKB_KEY_KP_Page_Up:   return Key::KP_PageUp;
    case XKB_KEY_KP_Page_Down: return Key::KP_PageDown;
    case XKB_KEY_KP_Insert:    return Key::KP_Insert;
    case XKB_KEY_KP_Delete:    return Key::KP_Delete;
    case XKB_KEY_KP_Begin:     return Key::KP_Begin;   // numpad-5
    default:                   return Key::Unknown;
    }
}

// Level-0-symbolen → Key. Bokstäverna tar både gemena och versala
// keysyms: en layout kan ha versalen på level 0 (och CapsLock ändrar
// inget här eftersom vi läser level 0), och `Key::A` betyder tangenten,
// inte tecknet.
Key symbolKey(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z)
        return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_a));
    if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z)
        return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_A));
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9)
        return static_cast<Key>(static_cast<int>(Key::N0) + (sym - XKB_KEY_0));
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        return static_cast<Key>(static_cast<int>(Key::F1) + (sym - XKB_KEY_F1));

    switch (sym) {
    // KP_Enter fälls ihop med Return: ingen app vill skilja dem, och varje
    // aktiveringsväg (Button/Checkbox svarar på Enter) fortsätter gälla.
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:     return Key::Return;
    case XKB_KEY_Escape:       return Key::Escape;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return Key::Tab;
    case XKB_KEY_BackSpace:    return Key::Backspace;
    case XKB_KEY_space:        return Key::Space;
    // Numpadens navigationsroller hör INTE hit: de tas av
    // `keypadKey` ur det faktiska läget och har egna `KP_*`-poster.
    case XKB_KEY_Delete:       return Key::Delete;
    case XKB_KEY_Left:         return Key::Left;
    case XKB_KEY_Right:        return Key::Right;
    case XKB_KEY_Up:           return Key::Up;
    case XKB_KEY_Down:         return Key::Down;
    case XKB_KEY_Home:         return Key::Home;
    case XKB_KEY_End:          return Key::End;
    case XKB_KEY_Page_Up:      return Key::PageUp;
    case XKB_KEY_Page_Down:    return Key::PageDown;
    case XKB_KEY_Insert:       return Key::Insert;

    // SKILJETECKNEN. Bara level-0-formen listas — en skiftad
    // symbol når enligt kontraktet aldrig hit (den kommer som sin
    // oskiftade granne + `kModShift`). `plus`/`percent`/`asterisk` står
    // med ändå: på svensk layout ÄR `+` oskiftat, och på flera layouter
    // gäller detsamma för de andra.
    case XKB_KEY_minus:        return Key::Minus;
    case XKB_KEY_plus:         return Key::Plus;
    case XKB_KEY_equal:        return Key::Equals;
    case XKB_KEY_comma:        return Key::Comma;
    case XKB_KEY_period:       return Key::Period;
    case XKB_KEY_slash:        return Key::Slash;
    case XKB_KEY_backslash:    return Key::Backslash;
    case XKB_KEY_semicolon:    return Key::Semicolon;
    case XKB_KEY_apostrophe:   return Key::Apostrophe;
    case XKB_KEY_bracketleft:  return Key::LeftBracket;
    case XKB_KEY_bracketright: return Key::RightBracket;
    case XKB_KEY_grave:        return Key::Grave;
    case XKB_KEY_percent:      return Key::Percent;
    case XKB_KEY_asterisk:     return Key::Asterisk;

    // En numpad-keysym på level 0 når hit bara om det faktiska läget gav
    // något `keypadKey` inte känner — då är level 0-symbolens egen post
    // det ärligaste svaret.
    default:                   return keypadKey(sym);
    }
}

} // namespace

Key translateXkbKey(xkb_state* state, xkb_keymap* keymap,
                    xkb_keycode_t keycode)
{
    if (!state)
        return Key::Unknown;

    // NUMPADEN FÖRST, ur det faktiska läget: level 0 på en numpad-tangent
    // är navigationssymbolen, så en ren level-0-läsning hade gjort
    // siffrorna omöjliga att ta emot oavsett NumLock.
    const Key kp = keypadKey(xkb_state_key_get_one_sym(state, keycode));
    if (kp != Key::Unknown)
        return kp;

    // LEVEL 0 — kontraktet: symbolen UTAN modifierare, i den layout som
    // gäller för just den här tangenten (`key_get_layout` respekterar
    // per-tangent-grupper). Utan keymap finns ingen level-0-fråga att
    // ställa; då är state-symbolen det bästa svar vi har.
    if (!keymap)
        return symbolKey(xkb_state_key_get_one_sym(state, keycode));

    const xkb_layout_index_t layout =
        xkb_state_key_get_layout(state, keycode);
    const xkb_keysym_t* syms = nullptr;
    const int n = xkb_keymap_key_get_syms_by_level(keymap, keycode, layout,
                                                   0, &syms);
    if (n > 0 && syms)
        return symbolKey(syms[0]);

    return symbolKey(xkb_state_key_get_one_sym(state, keycode));
}

std::uint32_t xkbModsAfterKey(std::uint32_t before, xkb_keysym_t sym,
                              bool pressed)
{
    // Symbolen och inte modifierarkartan: samma standardmappning som
    // drivrutinernas masköversättning antar (Mod1 = Alt, Mod4 = Super).
    // Meta ligger på Alt-tangenten i flera layouter (Shift+Alt).
    std::uint32_t bit = 0;
    switch (sym) {
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R: bit = kModCtrl;  break;
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:   bit = kModShift; break;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:    bit = kModAlt;   break;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:   bit = kModCmd;   break;
    default:                return before;
    }
    return pressed ? (before | bit) : (before & ~bit);
}

bool xkbTypesText(xkb_state* state, xkb_keymap* keymap,
                  xkb_keycode_t keycode)
{
    if (!state || !keymap)
        return false;
    // Modifierarna ur KEYMAPENS namn och inte ur bitar: samma index som
    // staten räknar med. En modifierare som tangenten själv konsumerar (en
    // layout där Alt väljer en nivå) gör den inte till ett kommando.
    const char* const kommandon[] = {XKB_MOD_NAME_CTRL, XKB_MOD_NAME_ALT,
                                     XKB_MOD_NAME_LOGO};
    for (const char* namn : kommandon) {
        const xkb_mod_index_t idx = xkb_keymap_mod_get_index(keymap, namn);
        if (idx == XKB_MOD_INVALID)
            continue;
        if (xkb_state_mod_index_is_active(state, idx,
                                          XKB_STATE_MODS_EFFECTIVE) > 0 &&
            xkb_state_mod_index_is_consumed(state, keycode, idx) <= 0)
            return false;
    }
    return true;
}

} // namespace gfx
