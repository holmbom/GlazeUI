// numpad_gate — NUMPADENS NAVIGATIONSLÄGE och navigationsklustret.
//
// Kör:  ./build/tests/numpad_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde
//       inte köras (ingen display, ingen xkb-data) — det som KUNDE mätas är
//       mätt, och skippet SYNS i ctest
//
// Numpadens navigationsläge har EGNA poster (`KP_End` …) i stället
// för att smälta ihop med de dedikerade tangenterna, `PageUp`/`PageDown`/`Insert`
// finns som egna poster, och `AppWindow` VIKER numpaden till
// tvillingen innan en widget får tangenten. Det är tre lager, och grinden
// mäter dem var för sig:
//
//   HEADLESS      VIKNINGEN (`gfx::navigationKey`) mot en HANDSKRIVEN
//                 tabell över HELA enumet · NAMNEN: varje post har ett
//                 eget kanoniskt namn som går tur och retur, de nya mot
//                 handskrivna förväntade värden, och de ÖVERSATTA namnen i alla fyra språken mot
//                 handskrivna förväntade värden — inte mot `tr()`, som annars hade fått
//                 godkänna sig själv · på LINUX översättningen
//                 (`gfx::translateXkbKey`) mot en RIKTIG keymap ur systemets
//                 xkeyboard-config, med NumLock låst respektive olåst.
//   MED FÖNSTER   ett DOLT fönster. På WINDOWS översättningen genom
//                 fönstrets RIKTIGA WndProc: `SendMessageW(WM_KEYDOWN)` med
//                 och utan extended-flaggan, avläst i `event()`-kroken — så
//                 att det är lParam-vägen ända fram (pushKey → translateKey)
//                 som mäts, inte en tabell. Därefter VIKNINGEN genom fönstrets
//                 riktiga ingång (`AppWindow::handleEvent`, protected): en
//                 fokuserad ListBox bläddrar med numpaden, appens
//                 `event()`-krok ser den OVIKTA posten, acceleratorerna
//                 skiljer `End` från `KP_End`, och en öppen meny navigerar
//                 med numpaden (ägarfönstrets tangentkrok).
//
// SABOTAGEN den fäller (ett i taget):
//   * vikningen borttagen ur `handleEvent` för den fokuserade widgeten
//     (fall V1) respektive för menyns tangentkrok (fall V3)
//   * acceleratorerna matade med den VIKTA posten (fall V2)
//   * en post fel eller saknad i `navigationKey` (fall H1)
//   * extended-flaggan ignorerad i `translateKey`, eller lParam tappad i
//     `pushKey` (fall W1, Windows)
//   * numpadens navigationsroller tillbaka i level-0-tabellen så att
//     NumLock av ger `End` (fall X1, Linux)
//   * `xkbTypesText` alltid true (fall X2: Ctrl+1), eller Level3/Mod5
//     räknad som Alt (fall X2: AltGr+2)
//   * ett nytt kanoniskt namn som saknas eller krockar (fall H2)
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   * att ett FYSISKT tangentbord sätter extended-flaggan på Windows — det
//     är OS:ets dokumenterade beteende, och meddelandena här är syntetiska.
//     Rad i doc/os/windows/TESTS.md.
//   * att Wayland-/X11-drivrutinerna håller xkb-staten i takt med NumLock.
//     Den vägen behövs av `KP_N1` och matas av
//     kompositören/X-servern, som en grind inte kan styra.
//   * macOS: Metal-översättningen tar riktiga `NSEvent` och kan inte
//     syntetiseras här. Rad i doc/os/macos/TESTS.md.
//   * PageUp/PageDown som BETEENDE — ingen widget bläddrar sida för sida än.

#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glaze/glaze.h>

#ifdef GLAZE_BACKEND_D3D11
// Samma vakter som backendens d3d11_backend.h (pump_gate-prejudikatet):
// MinGW:s libstdc++ definierar redan NOMINMAX.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef GLAZE_BACKEND_LINUX
// Backendens INTERNA översättning — tests/CMakeLists.txt pekar hit, samma
// förhållande som msgbox_gate har till message_box_table.h. xkb_keys.h
// drar in xkbcommon.h, och därmed modifierarnamnen.
#include "xkb_keys.h"
#endif

namespace {

using K = gfx::Key;

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

std::string namn(K k)
{
    return glaze::canonicalKeyName(k);
}

std::string lista(const std::vector<K>& v)
{
    std::string s;
    for (K k : v)
        s += (s.empty() ? "" : " ") + namn(k);
    return "[" + s + "]";
}

// ================================================================ HEADLESS

// H1. VIKNINGEN — förväntat värde skrivet för hand, och HELA enumet prövas: en ny
// numpadpost som glöms i `navigationKey` viks inte, och en post som viks
// fel hamnar på fel tvilling.
void fallVikning()
{
    const std::pair<K, K> facit[] = {
        {K::KP_Home, K::Home},         {K::KP_End, K::End},
        {K::KP_Left, K::Left},         {K::KP_Right, K::Right},
        {K::KP_Up, K::Up},             {K::KP_Down, K::Down},
        {K::KP_PageUp, K::PageUp},     {K::KP_PageDown, K::PageDown},
        {K::KP_Insert, K::Insert},     {K::KP_Delete, K::Delete},
    };
    int vikta = 0;
    for (int i = 0; i <= int(gfx::kLastKey); ++i) {
        const K k      = K(i);
        K       väntat = k;
        for (const auto& [från, till] : facit)
            if (från == k)
                väntat = till;
        const K fick = gfx::navigationKey(k);
        if (fick != k)
            ++vikta;
        check(fick == väntat,
              "vikning: " + namn(k) + " → " + namn(väntat), namn(fick));
        check(gfx::navigationKey(fick) == fick,
              "vikning: " + namn(k) + " viks EN gång (tvillingen är fast)",
              namn(gfx::navigationKey(fick)));
    }
    check(vikta == 10, "vikning: exakt tio poster viks",
          std::to_string(vikta));
    // Mitttangenten saknar tvilling, och siffrorna är inga roller.
    check(gfx::navigationKey(K::KP_Begin) == K::KP_Begin &&
              gfx::navigationKey(K::KP_N1) == K::KP_N1,
          "vikning: KP_Begin och siffrorna kommer tillbaka orörda");
}

// H2. KANONISKA NAMN — det `.glazeui` bär. Varje post 1..kLastKey har ett
// EGET namn (inte reservtexten "?", inte ett annat namns dubblett) som går
// tur och retur, så `parseCanonicalKey` hittar även de nya posterna.
void fallKanoniskaNamn()
{
    std::set<std::string> sedda;
    for (int i = 1; i <= int(gfx::kLastKey); ++i) {
        const K           k = K(i);
        const std::string n = glaze::canonicalKeyName(k);
        check(n != "?", "kanoniskt: post " + std::to_string(i) +
                            " har ett namn (inte reservtexten)", n);
        check(sedda.insert(n).second,
              "kanoniskt: \"" + n + "\" är unikt", n);
        check(glaze::parseCanonicalKey(n) == k,
              "kanoniskt: \"" + n + "\" går tur och retur",
              namn(glaze::parseCanonicalKey(n)));
    }

    const std::pair<K, const char*> facit[] = {
        {K::PageUp, "PgUp"},          {K::PageDown, "PgDn"},
        {K::Insert, "Ins"},           {K::KP_Home, "Num Home"},
        {K::KP_End, "Num End"},       {K::KP_Left, "Num Left"},
        {K::KP_Right, "Num Right"},   {K::KP_Up, "Num Up"},
        {K::KP_Down, "Num Down"},     {K::KP_PageUp, "Num PgUp"},
        {K::KP_PageDown, "Num PgDn"}, {K::KP_Insert, "Num Ins"},
        {K::KP_Delete, "Num Del"},    {K::KP_Begin, "Num Begin"},
    };
    for (const auto& [k, väntat] : facit)
        check(std::string(glaze::canonicalKeyName(k)) == väntat,
              std::string("kanoniskt facit: ") + väntat, namn(k));

    // Hela genvägen, modifierare och allt, i båda riktningarna.
    const glaze::Shortcut s = glaze::parseShortcut("Ctrl+Num End");
    check(s.key == K::KP_End && s.mods == gfx::kModCtrl,
          "parseShortcut(\"Ctrl+Num End\") ⇒ Ctrl + KP_End", namn(s.key));
    check(glaze::parseShortcut("Num End").key != K::End,
          "\"Num End\" är INTE End — de är olika poster");
}

// H3. ÖVERSATTA NAMN — det menyn VISAR. Utan modifierare är visningen ren
// `keyName` på alla plattformar (Mac-grenen lägger bara till symboler före
// den), så det förväntade värdet gäller överallt. Det förväntade värdet är handskrivet ur
// Glaze/language/framework.langtxt, inte läst ur `tr()`.
void fallOversattaNamn()
{
    struct Rad {
        const char* sprak;
        const char* pgUp;
        const char* ins;
        const char* kpLeft;
        const char* kpDown;
        const char* kpDel;
        const char* kpBegin;
    };
    const Rad facit[] = {
        {"sv", "PgUp", "Ins", "Num Vänster", "Num Ner", "Num Del", "Num Mitt"},
        {"en", "PgUp", "Ins", "Num Left", "Num Down", "Num Del", "Num Begin"},
        {"nb", "PgUp", "Ins", "Num Venstre", "Num Ned", "Num Del", "Num Midt"},
        {"fi", "PgUp", "Ins", "Num Vasen", "Num Alas", "Num Del", "Num Keski"},
    };
    auto visat = [](K k) { return glaze::shortcutText({k, 0}); };
    for (const Rad& r : facit) {
        const std::string tag = r.sprak;
        check(glaze::setLanguage(tag), "språket " + tag + " finns");
        check(visat(K::PageUp) == r.pgUp, tag + ": PageUp", visat(K::PageUp));
        check(visat(K::PageDown) == "PgDn", tag + ": PageDown",
              visat(K::PageDown));
        check(visat(K::Insert) == r.ins, tag + ": Insert", visat(K::Insert));
        check(visat(K::KP_Left) == r.kpLeft,
              tag + ": KP_Left = prefixet + tvillingens ÖVERSATTA namn",
              visat(K::KP_Left));
        check(visat(K::KP_Down) == r.kpDown, tag + ": KP_Down",
              visat(K::KP_Down));
        check(visat(K::KP_Delete) == r.kpDel, tag + ": KP_Delete",
              visat(K::KP_Delete));
        check(visat(K::KP_Begin) == r.kpBegin,
              tag + ": KP_Begin = prefixet + mitttangentens eget namn",
              visat(K::KP_Begin));
    }
    glaze::setLanguage("sv");
}

#ifdef GLAZE_BACKEND_LINUX
// X1. XKB — den RIKTIGA översättningen mot en keymap kompilerad ur systemets
// xkeyboard-config (samma data X11-drivrutinen läser), nycklarna slagna upp
// på sina xkb-NAMN och inte på hårdkodade keycodes. NumLock är `Mod2` —
// den RIKTIGA modifierare som kompositören och X-servern skickar i
// locked-masken, alltså exakt vad drivrutinerna matar `xkb_state` med.
void fallXkb()
{
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const xkb_rule_names namnen{"evdev", "pc105", "us", "", ""};
    xkb_keymap* km =
        ctx ? xkb_keymap_new_from_names(ctx, &namnen,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS)
            : nullptr;
    xkb_state* st = km ? xkb_state_new(km) : nullptr;
    if (!st) {
        std::printf("  --   xkb-fallen hoppas över: ingen keymap kunde "
                    "kompileras (saknas xkeyboard-config?)\n");
        ++g_skips;
        if (km)
            xkb_keymap_unref(km);
        if (ctx)
            xkb_context_unref(ctx);
        return;
    }
    const xkb_mod_index_t num = xkb_keymap_mod_get_index(km, XKB_MOD_NAME_NUM);
    check(num != XKB_MOD_INVALID, "xkb: keymapen har NumLock (Mod2)");

    struct Rad {
        const char* tangent;   // xkb-namnet (evdev)
        K           numPå;
        K           numAv;
    };
    const Rad facit[] = {
        {"KP1", K::KP_N1, K::KP_End},       {"KP2", K::KP_N2, K::KP_Down},
        {"KP3", K::KP_N3, K::KP_PageDown},  {"KP4", K::KP_N4, K::KP_Left},
        {"KP5", K::KP_N5, K::KP_Begin},     {"KP6", K::KP_N6, K::KP_Right},
        {"KP7", K::KP_N7, K::KP_Home},      {"KP8", K::KP_N8, K::KP_Up},
        {"KP9", K::KP_N9, K::KP_PageUp},    {"KP0", K::KP_N0, K::KP_Insert},
        {"KPDL", K::KP_Decimal, K::KP_Delete},
        {"KPEN", K::Return, K::Return},     {"KPAD", K::KP_Add, K::KP_Add},
        {"KPSU", K::KP_Subtract, K::KP_Subtract},
        {"KPMU", K::KP_Multiply, K::KP_Multiply},
        {"KPDV", K::KP_Divide, K::KP_Divide},
        // De DEDIKERADE tangenterna — NumLock rör dem aldrig.
        {"END", K::End, K::End},            {"HOME", K::Home, K::Home},
        {"PGUP", K::PageUp, K::PageUp},     {"PGDN", K::PageDown, K::PageDown},
        {"INS", K::Insert, K::Insert},      {"DELE", K::Delete, K::Delete},
        {"UP", K::Up, K::Up},               {"DOWN", K::Down, K::Down},
        {"LEFT", K::Left, K::Left},         {"RGHT", K::Right, K::Right},
        {"AE01", K::N1, K::N1},
    };
    for (const bool numLock : {true, false}) {
        const xkb_mod_mask_t låst =
            numLock && num != XKB_MOD_INVALID ? (1u << num) : 0u;
        xkb_state_update_mask(st, 0, 0, låst, 0, 0, 0);
        for (const Rad& r : facit) {
            const xkb_keycode_t kod = xkb_keymap_key_by_name(km, r.tangent);
            const K väntat = numLock ? r.numPå : r.numAv;
            const K fick   = gfx::translateXkbKey(st, km, kod);
            check(kod != XKB_KEYCODE_INVALID && fick == väntat,
                  std::string("xkb: ") + r.tangent + " med NumLock " +
                      (numLock ? "PÅ" : "AV") + " ⇒ " + namn(väntat),
                  namn(fick));
        }
    }
    xkb_state_unref(st);
    xkb_keymap_unref(km);
    xkb_context_unref(ctx);
}

// X2. SKRIVER TRYCKET TEXT? (`gfx::xkbTypesText`)
// mot riktiga keymaps: us — a och Shift+a skriver, Ctrl+1, Alt+a och Super+a
// gör det inte; se — AltGr+2 (`@`, Level3 = Mod5) skriver. Modifierarna sätts
// som de RIKTIGA modifierarna (`Control`, `Mod1`, `Mod4`, `Mod5`) i den
// nedtryckta masken — det drivrutinerna matar staten med. Sabotagen: alltid
// true (Ctrl+1), Level3 räknad som Alt (AltGr+2).
void fallXkbText()
{
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        std::printf("  --   X2 hoppas över: ingen xkb-kontext\n");
        ++g_skips;
        return;
    }
    const auto prova = [&](const char* layout, const char* tangent,
                           const char* mod, bool väntat, const char* vad) {
        const xkb_rule_names namnen{"evdev", "pc105", layout, "", ""};
        xkb_keymap* km = xkb_keymap_new_from_names(ctx, &namnen,
                                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb_state* st = km ? xkb_state_new(km) : nullptr;
        if (!st) {
            std::printf("  --   X2 %s hoppas över: ingen keymap\n", vad);
            ++g_skips;
            if (km)
                xkb_keymap_unref(km);
            return;
        }
        xkb_mod_mask_t mask = 0;
        if (mod) {
            const xkb_mod_index_t i = xkb_keymap_mod_get_index(km, mod);
            if (i != XKB_MOD_INVALID)
                mask = 1u << i;
        }
        xkb_state_update_mask(st, mask, 0, 0, 0, 0, 0);
        const xkb_keycode_t kod = xkb_keymap_key_by_name(km, tangent);
        check(kod != XKB_KEYCODE_INVALID &&
                  gfx::xkbTypesText(st, km, kod) == väntat,
              std::string("X2 ") + vad);
        xkb_state_unref(st);
        xkb_keymap_unref(km);
    };
    prova("us", "AC01", nullptr, true, "us: a skriver");
    prova("us", "AC01", XKB_MOD_NAME_SHIFT, true, "us: Shift+a skriver");
    prova("us", "AE01", XKB_MOD_NAME_CTRL, false, "us: Ctrl+1 skriver inte");
    prova("us", "AC01", XKB_MOD_NAME_ALT, false, "us: Alt+a skriver inte");
    prova("us", "AC01", XKB_MOD_NAME_LOGO, false, "us: Super+a skriver inte");
    prova("se", "AE02", "Mod5", true, "se: AltGr+2 (@) skriver");
    xkb_context_unref(ctx);
}
#endif

// ============================================================= MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::ListBox lista;
    glaze::Button  knapp{"knapp"};
    glaze::Menu    meny;

    // Det appens `event()`-krok SÅG — och om den ska ÄTA tangenterna (under
    // översättningsfallen, så att inget annat i fönstret rör sig).
    std::vector<std::pair<gfx::EventType, K>> sett;
    bool                                     ät = false;

    int ändringar = 0;   // listans onChange
    int endKlick = 0, kpEndKlick = 0;
    int aKlick = 0, bKlick = 0, cKlick = 0;

    explicit Vard(const std::string& titel) : glaze::AppWindow(titel, 400, 300)
    {
        lista.x      = 10;
        lista.y      = 10;
        lista.width  = 160;
        lista.height = 120;
        lista.setItems({"a", "b", "c", "d"});
        lista.setSelectedIndex(0);
        lista.onChange = [this] { ++ändringar; };
        knapp.x = 200;
        knapp.y = 10;
        root().adopt(lista);
        root().adopt(knapp);

        // TVÅ genvägar som skiljer sig BARA i numpaden:
        // de är olika poster, och acceleratorn ser den oviktade.
        glaze::Menu& m = menuBar.addMenu("Test");
        {
            glaze::MenuItem& slut = m.addItem("Slut");
            slut.shortcut         = {K::End, 0};
            slut.onClick          = [this] { ++endKlick; };
        }
        {
            // Referensen ovan är död här: `addItem` kan reallokera.
            glaze::MenuItem& numSlut = m.addItem("NumSlut");
            numSlut.shortcut         = {K::KP_End, 0};
            numSlut.onClick          = [this] { ++kpEndKlick; };
        }
        menuBar.show();

        meny.addItem("A").onClick = [this] { ++aKlick; };
        meny.addItem("B").onClick = [this] { ++bKlick; };
        meny.addItem("C").onClick = [this] { ++cKlick; };
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::KeyDown ||
            e.type == gfx::EventType::KeyUp) {
            sett.emplace_back(e.type, e.key);
            return ät;
        }
        return false;
    }

    // En tangent genom den RIKTIGA ingången (`handleEvent`) — hela
    // routingen, med `event()`-kroken först.
    void tangent(K k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }

    std::vector<K> settaTangenter() const
    {
        std::vector<K> v;
        for (const auto& [typ, k] : sett)
            if (typ == gfx::EventType::KeyDown)
                v.push_back(k);
        return v;
    }
};

#ifdef GLAZE_BACKEND_D3D11
// W1. WIN32 — riktiga WM_KEYDOWN genom fönstrets WndProc. lParam bär
// repetitionsräknaren 1 och, för de dedikerade tangenterna, EXTENDED-flaggan
// (bit 24) — det ENDA som skiljer End från numpad-End när NumLock är av.
struct Win32Rad {
    WPARAM vk;
    bool   extended;
    K      väntat;
};
const Win32Rad kWin32[] = {
    {VK_END, true, K::End},          {VK_END, false, K::KP_End},
    {VK_HOME, true, K::Home},        {VK_HOME, false, K::KP_Home},
    {VK_LEFT, true, K::Left},        {VK_LEFT, false, K::KP_Left},
    {VK_RIGHT, true, K::Right},      {VK_RIGHT, false, K::KP_Right},
    {VK_UP, true, K::Up},            {VK_UP, false, K::KP_Up},
    {VK_DOWN, true, K::Down},        {VK_DOWN, false, K::KP_Down},
    {VK_PRIOR, true, K::PageUp},     {VK_PRIOR, false, K::KP_PageUp},
    {VK_NEXT, true, K::PageDown},    {VK_NEXT, false, K::KP_PageDown},
    {VK_INSERT, true, K::Insert},    {VK_INSERT, false, K::KP_Insert},
    {VK_DELETE, true, K::Delete},    {VK_DELETE, false, K::KP_Delete},
    {VK_CLEAR, false, K::KP_Begin},  {VK_NUMPAD1, false, K::KP_N1},
    {VK_NUMPAD0, false, K::KP_N0},   {VK_DECIMAL, false, K::KP_Decimal},
    {VK_RETURN, true, K::Return},    {VK_RETURN, false, K::Return},
    {'1', false, K::N1},
};

std::string hex(WPARAM vk)
{
    char b[8];
    std::snprintf(b, sizeof b, "0x%02X", unsigned(vk));
    return b;
}

LPARAM keyLParam(bool extended, bool up)
{
    LPARAM lp = 1;   // repetitionsräknaren
    if (extended)
        lp |= LPARAM(1) << 24;
    if (up)
        lp |= (LPARAM(1) << 30) | (LPARAM(1) << 31);   // föregående + upp
    return lp;
}
#endif

void fallMedFonster()
{
    // Titeln är FindWindowW-nyckeln på Windows — unik per process, så två
    // samtidiga körningar inte hittar varandras fönster (pump_gate).
#ifdef GLAZE_BACKEND_D3D11
    const std::string titel =
        "numpad_gate-" + std::to_string(GetCurrentProcessId());
#else
    const std::string titel = "numpad_gate";
#endif
    Vard w(titel);
    w.setOffscreen(true);

    // ANROPA ALDRIG show() HÄR — `run()` ÄR öppnaren.
    bool                  kedjanKordes = false;
    int                   steg         = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kedjanKordes = true;
            if (!w.isOpen()) {
                std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                            "aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
#ifdef GLAZE_BACKEND_D3D11
            const std::wstring wtitel(titel.begin(), titel.end());
            const HWND hwnd = FindWindowW(nullptr, wtitel.c_str());
            check(hwnd != nullptr, "W1: fönstret hittas via sin titel");
            w.sett.clear();
            w.ät = true;   // översättningen mäts — inget annat ska röra sig
            if (hwnd) {
                // Samma tråd ⇒ WndProc körs SYNKRONT, och eventet hamnar i
                // backendens kö. Det levereras i nästa pumpvarv.
                for (const Win32Rad& r : kWin32)
                    SendMessageW(hwnd, WM_KEYDOWN, r.vk,
                                 keyLParam(r.extended, false));
                SendMessageW(hwnd, WM_KEYUP, VK_END, keyLParam(false, true));
                SendMessageW(hwnd, WM_KEYUP, VK_END, keyLParam(true, true));
            }
#endif
            break;
        }
        case 1: {
#ifdef GLAZE_BACKEND_D3D11
            const std::size_t n = sizeof kWin32 / sizeof kWin32[0];
            check(w.sett.size() == n + 2,
                  "W1: varje WM_KEYDOWN/WM_KEYUP blev ETT event",
                  std::to_string(w.sett.size()));
            for (std::size_t i = 0; i < n && i < w.sett.size(); ++i) {
                const Win32Rad& r = kWin32[i];
                check(w.sett[i].first == gfx::EventType::KeyDown &&
                          w.sett[i].second == r.väntat,
                      "W1: VK " + hex(r.vk) +
                          (r.extended ? " extended" : " utan flagga") +
                          " ⇒ " + namn(r.väntat),
                      namn(w.sett[i].second));
            }
            if (w.sett.size() == n + 2) {
                check(w.sett[n].first == gfx::EventType::KeyUp &&
                          w.sett[n].second == K::KP_End,
                      "W1: WM_KEYUP går samma väg (utan flagga ⇒ KP_End)",
                      namn(w.sett[n].second));
                check(w.sett[n + 1].first == gfx::EventType::KeyUp &&
                          w.sett[n + 1].second == K::End,
                      "W1: WM_KEYUP extended ⇒ End",
                      namn(w.sett[n + 1].second));
            }
#endif
            w.ät = false;
            w.sett.clear();

            // ---- V1. VIKNINGEN för den FOKUSERADE widgeten: listan
            //          bläddrar med numpaden, och kroken ser oviktat.
            w.lista.focus();
            check(w.lista.focused(), "V1: listan har fokus");
            w.tangent(K::KP_Down);
            check(w.lista.selectedIndex() == 1,
                  "V1: KP_Down flyttar valet som Down",
                  std::to_string(w.lista.selectedIndex()));
            w.tangent(K::KP_End);
            check(w.lista.selectedIndex() == 3,
                  "V1: KP_End går till sista raden som End",
                  std::to_string(w.lista.selectedIndex()));
            w.tangent(K::KP_Up);
            check(w.lista.selectedIndex() == 2, "V1: KP_Up som Up",
                  std::to_string(w.lista.selectedIndex()));
            w.tangent(K::KP_Home);
            check(w.lista.selectedIndex() == 0,
                  "V1: KP_Home går till första raden som Home",
                  std::to_string(w.lista.selectedIndex()));
            check(w.ändringar == 4,
                  "V1: fyra användarval — onChange en gång per tangent",
                  std::to_string(w.ändringar));
            check(w.settaTangenter() ==
                      std::vector<K>{K::KP_Down, K::KP_End, K::KP_Up,
                                     K::KP_Home},
                  "V1: appens event()-krok ser den OVIKTA posten",
                  lista(w.settaTangenter()));

            // ---- V2. ACCELERATORERNA får den OVIKTA posten — där ÄR
            //          skillnaden poängen. Fokus på en knapp, som inte
            //          konsumerar End.
            w.knapp.focus();
            w.tangent(K::KP_End);
            check(w.kpEndKlick == 1 && w.endKlick == 0,
                  "V2: KP_End träffar genvägen \"Num End\" och INTE \"End\"",
                  std::to_string(w.kpEndKlick) + "/" +
                      std::to_string(w.endKlick));
            w.tangent(K::End);
            check(w.endKlick == 1 && w.kpEndKlick == 1,
                  "V2: End träffar \"End\" och INTE \"Num End\"",
                  std::to_string(w.endKlick) + "/" +
                      std::to_string(w.kpEndKlick));

            // ---- V3. EN ÖPPEN MENY navigerar med numpaden (ägarens
            //          tangentkrok får den VIKTA posten).
            w.meny.popupAt(w.knapp, 10.0f, 10.0f);
            if (!w.meny.isOpen()) {
                std::printf("  --   menyfallen hoppas över: popup-fönstret "
                            "kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            w.tangent(K::KP_End);      // sista raden
            w.tangent(K::Return);
            check(w.cKlick == 1 && w.aKlick == 0 && w.bKlick == 0,
                  "V3: KP_End + Enter i en öppen meny aktiverar SISTA "
                  "posten", std::to_string(w.cKlick));
            break;
        }
        case 2:
            // Stängningen efter aktiveringen är UPPSKJUTEN — öppna igen
            // först när kedjan är riven.
            w.meny.popupAt(w.knapp, 10.0f, 10.0f);
            if (!w.meny.isOpen()) {
                std::printf("  --   andra menyöppningen kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            w.tangent(K::KP_Home);     // första raden
            w.tangent(K::KP_Down);     // andra
            w.tangent(K::Return);
            check(w.bKlick == 1 && w.aKlick == 0 && w.cKlick == 1,
                  "V3: KP_Home, KP_Down + Enter aktiverar ANDRA posten",
                  std::to_string(w.bKlick));
            break;
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();

    // VAKTEN: utan den kan hela den asynkrona halvan tystna och grinden
    // ändå rapportera grönt.
    if (!kedjanKordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("numpad_gate — numpadens navigationsläge\n");
    fallVikning();
    fallKanoniskaNamn();
    fallOversattaNamn();
#ifdef GLAZE_BACKEND_LINUX
    fallXkb();
    fallXkbText();
#endif
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
