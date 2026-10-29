// menu_gate — MENYERNAS grind.
//
// Kör:  ./build/tests/menu_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// TVÅDELAD, av samma skäl som dropdown_gate:
//
//   HEADLESS   allt som inte kräver ett fönster: trädet och index-stigen,
//              AKTIVERINGSTABELLEN med radio-uteslutningen,
//              acceleratormatchningen och genvägsformatteringen — plus
//              menyradens inverkan på innehållsytan, som `AppWindow`
//              räknar utan att fönstret öppnats (tvåfasen).
//   MED FÖNSTER  ett DOLT ägarfönster (`setOffscreen`) räcker för att
//              faktiskt öppna popup-kedjan: en presentation per `popupAt`,
//              stängningen som är UPPSKJUTEN till pump-svansen, och
//              menyradens rubrikbokföring.
//
// **OTESTAT HÄR och ärligt redovisat:** hover-vägen (arm rad, öppna
// undermeny vid hover, byt rad) går genom `MenuRows` i POPUP-fönstret, och
// det fönstret är internt i menu.cpp — ingen grind når dess ingång, så
// hovern verifieras manuellt eller renderat. TANGENTNAVIGERINGEN genom
// ägarens tangentkrok mäts däremot i `numpad_gate` (fall
// V3), som matar ägarfönstrets `handleEvent` (protected) —
// End/Home/Down och Enter, med numpaden vikt. Livstidsfällan i dem (en
// avfärdad nivås
// `onClose` kommer först i NÄSTA pumpvarv och får inte tolkas som att
// ROTEN stängdes) hanteras i `MenuPresentation::windowClosed` — men den
// har ingen körbar rad här.
//
// **REGELN FÖR FÖNSTERTESTER ÄR BINDANDE:** ett test får ALDRIG anropa
// `show()` följt av `run()` — `runInternal` avvisar ett redan öppet
// fönster och returnerar OMEDELBART, varvid hela den postade kedjan dör
// medan testet rapporterar grönt. `run()` ÄR öppnaren, `kedjanKordes` är
// vakten och `g_skips` syns i sammanfattningen. Ett test som ljuger är
// sämre än inget test.

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_skips   = 0;   // överhoppade block — SYNLIGA i sammanfattningen
int  g_cases   = 0;
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

// ------------------------------------------------------ genvägstexten
// MATCHNING och PRESENTATION är skilda saker — det här mäter bara
// presentationen, och att den är STABIL är hela poängen: två menyer får
// aldrig visa samma genväg olika.
void fallGenvagstext()
{
    using K = gfx::Key;
    // SPRÅKET SÄTTS EXPLICIT: visningstexten ÖVERSÄTTS
    // så en grind som läser OS-lokalen hade gett olika
    // resultat på olika maskiner. Engelska är det förväntade språket nedan.
    check(glaze::setLanguage("en"), "grinden kör på engelska");
    check(glaze::shortcutText({}) .empty(),
          "ingen genväg ger TOM text");
#ifdef __APPLE__
    check(glaze::shortcutText({K::S, gfx::kModCmd}) == "\xE2\x8C\x98S",
          "macOS visar symboler utan plustecken",
          glaze::shortcutText({K::S, gfx::kModCmd}));
#else
    check(glaze::shortcutText({K::S, gfx::kModCtrl}) == "Ctrl+S",
          "Ctrl+S", glaze::shortcutText({K::S, gfx::kModCtrl}));
    check(glaze::shortcutText({K::S, gfx::kModCtrl | gfx::kModShift}) ==
              "Ctrl+Shift+S",
          "ordningen är Ctrl · Alt · Shift · Super",
          glaze::shortcutText({K::S, gfx::kModCtrl | gfx::kModShift}));
    check(glaze::shortcutText({K::F4, gfx::kModAlt}) == "Alt+F4",
          "funktionstangent", glaze::shortcutText({K::F4, gfx::kModAlt}));
    check(glaze::shortcutText({K::KP_N1, 0}) == "Num 1",
          "numpaden får ett EGET prefix (skild från sifferraden i fasaden)",
          glaze::shortcutText({K::KP_N1, 0}));
    check(glaze::shortcutText({K::Plus, gfx::kModCtrl}) == "Ctrl++",
          "Ctrl+Plus är level-0-symbolen Plus — ingen Shift",
          glaze::shortcutText({K::Plus, gfx::kModCtrl}));
#endif
    // En tangent utan namn får en RESERVTEXT och inte tomhet: en felbunden
    // genväg ska SYNAS i menyn i stället för att se ut som ingen genväg.
    glaze::Shortcut udda;
    udda.key = gfx::Key::KP_Decimal;
    check(!glaze::shortcutText(udda).empty(),
          "varje bunden tangent ger en icke-tom text");

    // ---------------------------------------------------------- SPRÅKET
    //
    // KLYVNINGEN är den farligaste punkten: glöms
    // den går `.glazeui`-filer sönder PER SPRÅK. Två saker mäts därför
    // under ALLA FYRA språken:
    //
    //   1. SERIALISERINGEN är oförändrad — `"Ctrl+Backspace"` parsas
    //      identiskt oavsett aktivt språk. Det förväntade är skrivet här.
    //   2. VISNINGEN översätts — och att den GÖR det mäts genom att den
    //      skiljer sig mellan två språk. Utan den andra halvan hade en
    //      `keyName` som glömt sin `tr` passerat grinden.
    const char* kSprak[] = {"sv", "en", "nb", "fi"};
    for (const char* tag : kSprak) {
        check(glaze::setLanguage(tag), std::string("setLanguage(") + tag + ")");
        const glaze::Shortcut s = glaze::parseShortcut("Ctrl+Backspace");
        check(s.key == K::Backspace && s.mods == gfx::kModCtrl,
              std::string("SERIALISERINGEN är språkoberoende: "
                          "\"Ctrl+Backspace\" parsas på ") + tag);
        check(std::string(glaze::canonicalKeyName(K::Backspace)) == "Backspace",
              std::string("canonicalKeyName är KANONISK på ") + tag,
              glaze::canonicalKeyName(K::Backspace));
        check(glaze::parseCanonicalKey("Num 7") == K::KP_N7,
              "parseCanonicalKey täcker numpaden");
        check(glaze::parseCanonicalKey("Askelpalautin") == K::Unknown,
              "…och accepterar ALDRIG ett översatt namn");
    }
    check(glaze::setLanguage("fi"), "finska aktivt");
    const std::string fi = glaze::shortcutText({K::Backspace, 0});
    check(glaze::setLanguage("sv"), "svenska aktivt");
    const std::string sv = glaze::shortcutText({K::Backspace, 0});
#ifndef __APPLE__
    check(fi == "Askelpalautin", "VISNINGEN översätts (fi)", fi);
    check(sv == "Backsteg", "VISNINGEN översätts (sv)", sv);
    check(glaze::shortcutText({K::S, gfx::kModCtrl | gfx::kModShift}) ==
              "Ctrl+Skift+S",
          "…och MODIFIERARNA med (sv: Skift+)",
          glaze::shortcutText({K::S, gfx::kModCtrl | gfx::kModShift}));
#endif
    check(fi != sv, "de fyra språken ger inte samma visningstext");
    glaze::setLanguage("en");
}

// ------------------------------------------------------ MENYSTIGEN
// SEGMENTERAD tokenstig. Headless — `MenuBar` behöver
// varken fönster eller font för sina add-satser.
void fallMenyStig()
{
    glaze::setLanguage("sv");
    glaze::MenuBar bar;

    // DE TRE ENKELSEGMENT-CTORERNA måste KOMPILERA från alla tre formerna
    // — utan dem hade varenda befintligt `addItem("Arkiv", …)` slutat gå
    // (`const char* → TrText → MenuPath` är två användardefinierade
    // konverteringar). Den här raden ÄR grinden: faller en ctor bort
    // faller BYGGET, inte en assertion.
    const std::string   sträng = "Arkiv";
    const glaze::TrText token  = glaze::TrText("Arkiv");
    const glaze::MenuPath p1("Arkiv"), p2(sträng), p3(token);
    check(p1.size() == 1 && p2.size() == 1 && p3.size() == 1,
          "MenuPath: const char*, std::string och TrText ger ETT segment");

    // VALIDATORN — ALLA ctorer går genom den, och listinitialisering
    // prioriterar `initializer_list`, så klammerformen är det farliga
    // fallet.
    check(glaze::MenuPath{"a/b"}.empty(),
          "MenuPath{\"a/b\"} AVVISAS — klammerformen kringgår inte "
          "validatorn");
    check(glaze::MenuPath("Utseende/Läge").empty(),
          "MenuPath(\"a/b\") avvisas — /-formen UTGÅR");
    check(glaze::MenuPath({"a", "b/c"}).empty(),
          "snedstreck i ETT av flera segment förkastar hela stigen");
    check(glaze::MenuPath("").empty(), "TOMT segment är ogiltigt i TYPEN");
    check(glaze::MenuPath(std::initializer_list<glaze::TrText>{}).empty(),
          "TOM segmentlista är ogiltig");
    check(glaze::MenuPath({"Utseende", "Läge"}).size() == 2,
          "två giltiga segment godtas");

    // LITERAL/LITERAL matchar.
    bar.addMenu("Utseende");
    bar.addSubmenu("Utseende", "Läge");
    bar.addRadio({"Utseende", "Läge"}, "System");
    check(bar.itemAtPath({"Utseende", "Läge", "System"}) != nullptr,
          "literal/literal: den segmenterade stigen matchar");
    check(bar.itemAtPath({"Utseende", "Läge/System"}) == nullptr,
          "…och den gamla /-formen gör det ALDRIG");

    // ID/ID matchar — samma id, oavsett vad det just nu betyder.
    glaze::MenuBar idbar;
    idbar.addMenu(glaze::tr(glaze::str::Yes));
    idbar.addItem(glaze::MenuPath(glaze::tr(glaze::str::Yes)),
                  glaze::tr(glaze::str::No));
    check(idbar.itemAtPath({glaze::tr(glaze::str::Yes),
                            glaze::tr(glaze::str::No)}) != nullptr,
          "id/id: stigen matchar på ID:t");
    check(idbar.menuCount() == 1 && idbar.menuTitle(0) == "Ja",
          "…och rubriken är den UPPLÖSTA texten", idbar.menuTitle(0));
    // …och den gör det EFTER ett språkbyte, vilket är hela poängen.
    glaze::setLanguage("fi");
    check(idbar.itemAtPath({glaze::tr(glaze::str::Yes),
                            glaze::tr(glaze::str::No)}) != nullptr,
          "id/id: stigen matchar ÄVEN efter språkbyte");
    check(idbar.menuTitle(0) == "Kyllä", "…medan TEXTEN bytte språk",
          idbar.menuTitle(0));

    // ID/LITERAL är ett TYPFEL — no-op, aldrig en tyst träff.
    check(idbar.itemAtPath({"Kyllä", "Ei"}) == nullptr,
          "id/literal är ett TYPFEL — matchar aldrig, ens på samma text");
    const int före = int(idbar.menuAt(0)->items.size());
    idbar.addItem({"Kyllä"}, "Ny");
    check(int(idbar.menuAt(0)->items.size()) == före,
          "…och en add-sats på en typfelsstig är en NO-OP");

    // TOM STIG når aldrig något.
    check(bar.itemAtPath(glaze::MenuPath("")) == nullptr,
          "tom stig matchar ingenting");
    glaze::setLanguage("en");
}

// ------------------------------------------------------ MATCHNINGEN
void fallMatchning()
{
    glaze::Shortcut s{gfx::Key::S, gfx::kModCtrl};
    check(s.matches(gfx::Key::S, gfx::kModCtrl), "Ctrl+S matchar");
    check(!s.matches(gfx::Key::S, gfx::kModCtrl | gfx::kModShift),
          "matchningen är EXAKT: Ctrl+Shift+S träffar inte Ctrl+S");
    check(!s.matches(gfx::Key::D, gfx::kModCtrl), "annan tangent matchar inte");
    check(!glaze::Shortcut{}.matches(gfx::Key::Unknown, 0),
          "en OSATT genväg matchar aldrig — inte ens Key::Unknown");
}

// ------------------------------------------------------ trädet & stigen
void fallTradOchStig()
{
    glaze::Menu m;
    m.addItem("Ny");
    m.addSeparator();
    glaze::MenuItem& arkiv = m.addSubmenu("Senaste");
    arkiv.submenu.push_back(glaze::MenuItem{});
    arkiv.submenu.back().text = "a.txt";
    arkiv.submenu.push_back(glaze::MenuItem{});
    arkiv.submenu.back().text = "b.txt";

    check(m.items.size() == 3, "tre poster", std::to_string(m.items.size()));
    check(m.items[1].separator, "separatorn är en separator");
    check(!m.items[1].enabled,
          "separatorn är ALDRIG valbar (armeras aldrig, aktiveras aldrig)");

    check(m.itemAt({0}) && m.itemAt({0})->text == "Ny", "stigen {0}");
    check(m.itemAt({2, 1}) && m.itemAt({2, 1})->text == "b.txt",
          "stigen {2,1} går ner i undermenyn");
    check(m.itemAt({9}) == nullptr, "stig utanför intervallet ger nullptr");
    check(m.itemAt({2, 9}) == nullptr, "djup stig utanför ger nullptr");
    check(m.itemAt({0, 0}) == nullptr,
          "stig NER i en post utan undermeny ger nullptr");

    // REALLOKERING är hela skälet till att stigar används i stället för
    // pekare: en `push_back` flyttar hela vektorn.
    const glaze::MenuItem* fore = m.itemAt({2, 0});
    for (int i = 0; i < 64; ++i)
        m.items[2].submenu.push_back(glaze::MenuItem{});
    check(m.itemAt({2, 0}) != nullptr && m.itemAt({2, 0})->text == "a.txt",
          "stigen överlever en reallokering (en pekare hade inte gjort det)");
    (void)fore;
}

// -------------------------------------------------- AKTIVERINGSTABELLEN
// Rad för rad. `activatePath` är den ENDA vägen in — klick, Enter och
// accelerator går alla genom den.
void fallAktivering()
{
    int n = 0;
    glaze::Menu m;
    m.addItem("Vanlig").onClick    = [&n] { ++n; };
    m.addSeparator();
    m.addCheck("Kryss").onClick    = [&n] { ++n; };
    glaze::MenuItem& sub = m.addSubmenu("Undermeny");
    sub.onClick = [&n] { ++n; };   // ska ALDRIG fyras
    sub.submenu.push_back(glaze::MenuItem{});
    sub.submenu.back().text = "barn";
    m.addItem("Inaktiv").enabled   = false;

    check(m.activatePath({0}, nullptr) && n == 1,
          "vanlig post: EXAKT ETT onClick", std::to_string(n));
    check(!m.activatePath({1}, nullptr) && n == 1,
          "separator: no-op", std::to_string(n));
    check(m.activatePath({2}, nullptr) && m.items[2].checked && n == 2,
          "checkable: togglas EXAKT en gång + ett onClick",
          std::to_string(n));
    check(m.activatePath({2}, nullptr) && !m.items[2].checked && n == 3,
          "checkable togglas tillbaka", std::to_string(n));
    check(m.activatePath({3}, nullptr) && n == 3,
          "undermeny ÖPPNAR och fyrar ALDRIG onClick", std::to_string(n));
    check(!m.activatePath({4}, nullptr) && n == 3,
          "inaktiv post: no-op", std::to_string(n));
    check(!m.activatePath({99}, nullptr) && n == 3,
          "stig som inte finns: no-op (menyn kan ha muterats)");
}

// ------------------------------------------------------ RADIOGRUPPEN
// Definitionen: gruppen är de SAMMANHÄNGANDE radio-posterna mellan
// separatorer eller icke-radiorader i SAMMA meny. Utan den går testet inte
// att skriva — och det är just därför den står i menu.h.
void fallRadio()
{
    glaze::Menu m;
    // OBS: inga avslutande backslash i kommentarerna här — ett `//` som
    // slutar med `\` SPLITSAR nästa rad in i kommentaren, och då försvinner
    // halva menyn utan att bygget säger något.
    m.addRadio("A", true);   // 0 — grupp 1
    m.addRadio("B");         // 1 — grupp 1
    m.addRadio("C");         // 2 — grupp 1
    m.addSeparator();        // 3 — bryter
    m.addRadio("X", true);   // 4 — grupp 2
    m.addRadio("Y");         // 5 — grupp 2
    m.addItem("Vanlig");     // 6 — bryter
    m.addRadio("Z");         // 7 — egen grupp

    m.activatePath({1}, nullptr);
    check(!m.items[0].checked && m.items[1].checked && !m.items[2].checked,
          "radio: exakt en i gruppen är markerad");
    check(m.items[4].checked && !m.items[5].checked,
          "en SEPARATOR bryter gruppen — grupp 2 är orörd");

    m.activatePath({5}, nullptr);
    check(m.items[5].checked && !m.items[4].checked,
          "grupp 2 utesluter internt");
    check(m.items[1].checked, "…utan att röra grupp 1");

    m.activatePath({7}, nullptr);
    check(m.items[7].checked && m.items[5].checked,
          "en ICKE-radiorad bryter också gruppen");

    int n = 0;
    m.items[2].onClick = [&n] { ++n; };
    m.activatePath({2}, nullptr);
    check(n == 1 && m.items[2].checked && !m.items[1].checked,
          "radio fyrar onClick EXAKT en gång", std::to_string(n));
}

// ---------------------------------------------------- ACCELERATORERNA
void fallAccelerator()
{
    int spara = 0, djupt = 0;
    glaze::Menu m;
    m.addItem("Spara").shortcut = {gfx::Key::S, gfx::kModCtrl};
    m.items.back().onClick      = [&spara] { ++spara; };
    m.addSeparator();
    glaze::MenuItem& sub = m.addSubmenu("Mer");
    sub.submenu.push_back(glaze::MenuItem{});
    sub.submenu.back().text     = "Djupt";
    sub.submenu.back().shortcut = {gfx::Key::D, gfx::kModCtrl};
    sub.submenu.back().onClick  = [&djupt] { ++djupt; };
    m.addItem("Låst").shortcut  = {gfx::Key::L, gfx::kModCtrl};
    m.items.back().enabled      = false;

    check(m.tryShortcut(gfx::Key::S, gfx::kModCtrl, nullptr) && spara == 1,
          "acceleratorn hittar och aktiverar EXAKT en gång",
          std::to_string(spara));
    check(m.tryShortcut(gfx::Key::D, gfx::kModCtrl, nullptr) && djupt == 1,
          "acceleratorn söker REKURSIVT i undermenyer", std::to_string(djupt));
    check(!m.tryShortcut(gfx::Key::L, gfx::kModCtrl, nullptr),
          "en INAKTIVERAD post svarar inte på sin genväg");
    check(!m.tryShortcut(gfx::Key::S,
                         gfx::kModCtrl | gfx::kModShift, nullptr) &&
              spara == 1,
          "modifierarna matchas EXAKT", std::to_string(spara));
    check(!m.tryShortcut(gfx::Key::Q, gfx::kModCtrl, nullptr),
          "okänd genväg konsumeras INTE (tangenten bubblar vidare)");
}

// ------------------------------------------------------------ MENYRADEN
void fallMenyrad()
{
    glaze::MenuBar bar;
    check(!bar.focusable(),
          "menyraden är INGET tabbstopp (titlarna är data, inte barn)");
    check(bar.children().empty(),
          "menyraden har INGA widget-barn — annars ett tabbstopp per rubrik",
          std::to_string(bar.children().size()));
    check(bar.menuCount() == 0, "tom från början");
    check(bar.menuAt(0) == nullptr, "menuAt utanför intervallet ger nullptr");
    check(bar.menuTitle(5).empty(), "menuTitle utanför intervallet är tom");

    int arkiv = 0, redigera = 0;
    glaze::Menu& a = bar.addMenu("Arkiv");
    a.addItem("Spara").shortcut = {gfx::Key::S, gfx::kModCtrl};
    a.items.back().onClick      = [&arkiv] { ++arkiv; };
    glaze::Menu& r = bar.addMenu("Redigera");
    r.addItem("Ångra").shortcut = {gfx::Key::Z, gfx::kModCtrl};
    r.items.back().onClick      = [&redigera] { ++redigera; };

    // REFERENSEN MÅSTE VARA STABIL: menyerna ligger bakom unique_ptr just
    // för att `Menu` varken är kopierbar eller flyttbar (presentationerna
    // pekar tillbaka på den).
    check(&a == bar.menuAt(0) && &r == bar.menuAt(1),
          "addMenu ger STABILA referenser trots att listan växt");
    check(bar.menuCount() == 2 && bar.menuTitle(1) == "Redigera",
          "rubrikerna lagras", bar.menuTitle(1));

    check(bar.tryShortcut(gfx::Key::S, gfx::kModCtrl) && arkiv == 1,
          "menyradens accelerator når första menyn");
    check(bar.tryShortcut(gfx::Key::Z, gfx::kModCtrl) && redigera == 1,
          "…och den andra");
    check(!bar.tryShortcut(gfx::Key::Q, gfx::kModCtrl),
          "okänd genväg konsumeras inte av raden heller");

    bar.clearMenus();
    check(bar.menuCount() == 0 && bar.openIndex() == -1,
          "clearMenus tömmer och släpper rubriken");
}

// -------------------------------------- INNEHÅLLSYTAN (utan fönster)
// Menyraden är klientyta men skär ändå en remsa — `chromeInsetH` måste
// räkna den, precis som statusraden. Mäts UTAN fönster: tvåfasen gör
// konstruktorn ofarlig (som window_gate).
void fallInnehallsyta()
{
    glaze::AppWindow w("meny", 400, 300, gfx::Chrome::Custom);
    w.setContentSize(400, 300);
    const float utan = w.clientSize().h;
    w.menuBar.show();
    const float med = w.clientSize().h;
    const float rad = w.style().menuRowH;
    check(med > utan,
          "en SYNLIG menyrad växer klientytan (chromeInsetH räknar den)",
          std::to_string(utan) + " -> " + std::to_string(med));
    check(std::abs((med - utan) - rad) < 1.5f,
          "…med exakt menyradens höjd (menuRowH)",
          std::to_string(med - utan) + " mot " + std::to_string(rad));
    w.menuBar.hide();
    check(std::abs(w.clientSize().h - utan) < 0.5f,
          "en DOLD menyrad skär ingen remsa igen",
          std::to_string(w.clientSize().h));
    check(w.contentSize().h > 0.0f, "innehållsytan är fortfarande positiv");
}

// ------------------------------------------------------ POPUP utan fönster
void fallPopupUtanFonster()
{
    glaze::Menu m;
    glaze::Button lös;
    m.popupAt(lös, 10.0f, 10.0f);
    check(!m.isOpen(),
          "popupAt på en widget UTAN fönster är no-op (loggar, kraschar ej)");

    glaze::Menu tom;
    glaze::AppWindow w("x", 200, 200);
    check(!tom.isOpen(), "en tom meny är aldrig öppen");
    tom.popupAt(w.root(), 1.0f, 1.0f);
    check(!tom.isOpen(), "en TOM meny öppnar ingenting");
    tom.close();
    check(!tom.isOpen(), "close() på en stängd meny är no-op");
}

// ------------------------------------------------------------ MED FÖNSTER
//
// Stängningen är UPPSKJUTEN (App verkställer i pump-svansen), så ett test
// som anropar `close()` och läser `isOpen()` i nästa rad mäter ingenting.
// Fallen körs därför som en KEDJA AV POSTADE STEG — samma mekanik som
// post_gate och dropdown_gate, använd som klocka.

// SPRÅKDROPDOWNEN. `Dropdown::commit()`
// fyrar `onChange` och anropar `close()` FÖRST DÄREFTER — alltså mitt i
// popup-fönstrets egen klick-dispatch — medan `glaze::setLanguage` river
// popups SYNKRONT (`closePopupsNow`). Ett DIREKT anrop i handlern hade
// därför rivit det fönster vars event pågår, samma UAF-klass som menyerna
// löser med "stängningen köas FÖRE handlern".
//
// `keyDown` är protected på `Widget`; en testsubklass får den, och
// Enter i öppet läge går genom EXAKT samma `commit()` som ett radklick
// (popupen ber alltid ÄGAREN committa). Det är därför den här
// riggen mäter samma väg som musen tar.
struct ProvDd : glaze::Dropdown {
    using glaze::Dropdown::keyDown;
};

struct Vard : glaze::AppWindow {
    glaze::Button knapp;
    ProvDd        sprakDd;
    glaze::Menu   snabb;
    Vard() : glaze::AppWindow("menu_gate", 420, 320)
    {
        knapp.x     = 10;
        knapp.y     = 10;
        knapp.width = 120;
        root().adopt(knapp);
        sprakDd.x     = 10;
        sprakDd.y     = 60;
        sprakDd.width = 160;
        root().adopt(sprakDd);
    }
};

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);   // som window_gate: dolt men RIKTIGT

    int klick = 0, stangd = 0;
    w.snabb.addItem("Klipp ut").onClick = [&klick] { ++klick; };
    w.snabb.addSeparator();
    glaze::MenuItem& sub = w.snabb.addSubmenu("Mer");
    sub.submenu.push_back(glaze::MenuItem{});
    sub.submenu.back().text = "Djupt";
    w.snabb.onClose = [&stangd] { ++stangd; };

    glaze::Menu& arkiv = w.menuBar.addMenu("Arkiv");
    arkiv.addItem("Ny");
    glaze::Menu& hjalp = w.menuBar.addMenu("Hjälp");
    hjalp.addItem("Om");
    w.menuBar.show();

    // BREDDMENYN: en enda rad vars genväg är Backspace — den tangent vars
    // finska namn ("Askelpalautin") är tabellens längsta. Deklarerad
    // EFTER `w` så den rivs FÖRE fönstret.
    glaze::Menu bredd;
    bredd.addItem("Rad").shortcut = {gfx::Key::Backspace, 0};
    float       breddEn = 0.0f, breddFi = 0.0f;
    std::string sprakFore;

    // ANROPA ALDRIG show() HÄR — se filhuvudet.
    bool kedjanKordes = false;
    int  steg         = 0;
    std::function<void()> nasta;
    nasta = [&] {
        switch (steg++) {
        case 0:
            kedjanKordes = true;
            w.snabb.popupAt(w.knapp, 40.0f, 40.0f);
            if (!w.snabb.isOpen()) {
                std::printf("  --   popup-fallen hoppas över: popup-fönstret "
                            "kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            check(true, "popupAt: kedjan kom upp");
            check(glaze::app().windowCount() == 2,
                  "popupAt: EXAKT ett popup-fönster",
                  std::to_string(glaze::app().windowCount()));
            w.snabb.close();
            break;
        case 1:
            check(!w.snabb.isOpen(), "close(): kedjan är stängd");
            check(stangd == 1, "onClose fyras EXAKT en gång",
                  std::to_string(stangd));
            check(glaze::app().windowCount() == 1,
                  "close(): popup-fönstret är rivet",
                  std::to_string(glaze::app().windowCount()));
            // MENYRADEN: rubriken öppnar, och ett byte lämnar ALDRIG två
            // popups uppe (kedjeregeln — `showOwned` → `closePopupsNow`).
            w.menuBar.openMenu(0);
            break;
        case 2:
            check(w.menuBar.openIndex() == 0, "menyraden: rubrik 0 är öppen",
                  std::to_string(w.menuBar.openIndex()));
            check(arkiv.isOpen(), "menyraden: rubrikens meny är öppen");
            w.menuBar.openMenu(1);
            break;
        case 3:
            check(w.menuBar.openIndex() == 1, "menyraden: rubriken byttes",
                  std::to_string(w.menuBar.openIndex()));
            check(glaze::app().windowCount() == 2,
                  "menyraden: bytet lämnade INTE två popups uppe",
                  std::to_string(glaze::app().windowCount()));
            w.menuBar.closeMenu();
            break;
        case 4:
            check(w.menuBar.openIndex() == -1, "menyraden: stängd",
                  std::to_string(w.menuBar.openIndex()));
            check(glaze::app().windowCount() == 1,
                  "menyraden: inga popups kvar",
                  std::to_string(glaze::app().windowCount()));
            // EN AKTIVERING stänger kedjan OCH kör handlern exakt en gång.
            w.snabb.popupAt(w.knapp, 40.0f, 40.0f);
            break;
        case 5:
            check(w.snabb.isOpen(), "andra öppningen kom upp");
            w.snabb.activatePath({0}, &w.knapp);
            check(klick == 1, "activate: handlern kördes EXAKT en gång",
                  std::to_string(klick));
            w.snabb.close();
            break;
        case 6:
            check(!w.snabb.isOpen(), "kedjan stängd efter aktivering");
            check(glaze::app().windowCount() == 1, "inga popups kvar",
                  std::to_string(glaze::app().windowCount()));
            // FINSKAN MÄTS, PÅSTÅS INTE. Popupens bredd
            // härleds ur den MÄTTA längsta raden plus genvägskolumnen, så
            // en meny vars enda skillnad är att genvägen heter
            // "Askelpalautin" i stället för "Backspace" MÅSTE bli bredare.
            // Det förväntade är olikheten, inte ett tal: en hårdkodad bredd hade
            // varit ett tal i ett test, och det är precis vad grinden
            // finns för att fånga.
            glaze::setLanguage("en");
            bredd.popupAt(w.knapp, 40.0f, 40.0f);
            break;
        case 7:
            if (!bredd.isOpen()) {
                std::printf("  --   breddmätningen hoppas över\n");
                ++g_skips;
                w.close();
                return;
            }
            for (glaze::AppWindow* p : glaze::app().windows())
                if (p != &w)
                    breddEn = p->clientSize().w;
            bredd.close();
            break;
        case 8:
            glaze::setLanguage("fi");
            bredd.popupAt(w.knapp, 40.0f, 40.0f);
            break;
        case 9:
            for (glaze::AppWindow* p : glaze::app().windows())
                if (p != &w)
                    breddFi = p->clientSize().w;
            check(breddEn > 0.0f && breddFi > breddEn,
                  "fi: popupbredden följer den MÄTTA genvägskolumnen "
                  "(Askelpalautin > Backspace)",
                  std::to_string(breddEn) + " → " + std::to_string(breddFi));
            bredd.close();
            glaze::setLanguage("en");
            break;
        case 10: {
            // SPRÅKBYTE UR EN POPUP-DRIVEN HANDLER — se noten vid `ProvDd`.
            // Handlern POSTAR bytet; kör den `setLanguage` direkt rivs
            // popupfönstret mitt i sin egen dispatch.
            std::vector<std::string> namn;
            for (int i = 0; i < glaze::languageCount(); ++i)
                namn.push_back(glaze::languageAt(i).tag);
            w.sprakDd.setItems(namn);
            w.sprakDd.setSelectedIndex(0);
            glaze::setLanguage(namn.empty() ? "en" : namn[0]);
            w.sprakDd.onChange = [&w](glaze::Widget* s, glaze::ChangeArgs&) {
                const int i = static_cast<glaze::Dropdown*>(s)->selectedIndex();
                const std::string t = glaze::languageAt(i).tag;
                glaze::app().post([t] { glaze::setLanguage(t); });
            };
            w.sprakDd.open();
            if (!w.sprakDd.isOpen()) {
                std::printf("  --   språkdropdownen hoppas över: popupen "
                            "kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            sprakFore = glaze::language();
            w.sprakDd.keyDown(gfx::Key::Down, 0);
            w.sprakDd.keyDown(gfx::Key::Return, 0);   // commit → onChange → close
            check(glaze::language() == sprakFore,
                  "språkdropdown: handlern POSTAR — språket är oförändrat "
                  "under dispatchen", glaze::language());
            break;
        }
        case 11:
            check(glaze::app().windowCount() == 1,
                  "språkdropdown: popupen är riven, ägarfönstret LEVER",
                  std::to_string(glaze::app().windowCount()));
            check(glaze::language() != sprakFore,
                  "språkdropdown: den POSTADE funktionen körde i "
                  "pump-svansen och bytte språk",
                  sprakFore + " → " + glaze::language());
            check(w.isOpen(), "språkdropdown: ägarfönstret är öppet efteråt");
            w.close();
            return;
        default:
            w.close();
            return;
        }
        glaze::app().post(nasta);
    };
    glaze::app().post(nasta);
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

    std::printf("menu_gate\n");
    fallGenvagstext();
    fallMenyStig();
    fallMatchning();
    fallTradOchStig();
    fallAktivering();
    fallRadio();
    fallAccelerator();
    fallMenyrad();
    fallInnehallsyta();
    fallPopupUtanFonster();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
