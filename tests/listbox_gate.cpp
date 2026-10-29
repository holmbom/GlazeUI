// listbox_gate — LISTBOXENS grind.
//
// Kör:  ./build/tests/listbox_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Headless: inga fönster, ingen pump, ingen `gfx::init`. Det som mäts är
// widgetens TILLSTÅNDSKONTRAKT, och det är just det som är svårt att se
// med ögat:
//
//   * ANVÄNDARE vs PROGRAM. `selectFromUser`/`toggleCheckFromUser`
//     fyrar EXAKT ETT event; `setSelectedIndex`/`setChecked`/
//     `setProvisionalIndex` fyrar ALDRIG något. Att dubbelfyrning ska vara
//     omöjlig BY DESIGN är hela skälet dropdownens popup går genom den
//     programmatiska vägen — men designen är bara sann om koden är det.
//   * KANTFALLEN: tom lista, index utanför intervallet, `setItems` som
//     KRYMPER (checks-vektorn och `changedRow` måste normaliseras), och
//     tom lista med kryssen påslagna.
//
// Geometrin (`rowRect`/`checkRect`/`textX`) mäts INTE här: den kräver ett
// `Ctx` med font och renderer, alltså ett fönster. Den verifieras i stället
// RENDERAD via glaze-render på demons galleri — samma sätt som resten av
// ritningen.
//
// FLERVALET — headless genom användarvägen
// `selectFromUser(rad, mods, frånTangent)` och `keyDown`:
//   M1  enkelvalet är orört med flervalet AV (de befintliga fallen ovan, och
//       `setSelected` i enkelval = `setSelectedIndex`)
//   M2  klick väljer bara raden, Ctrl vänder, Shift spannet från ANKARET,
//       Ctrl+Shift LÄGGER TILL spannet
//   M3  tangenterna: Shift+Ned utökar, Ctrl+Ned flyttar UTAN att välja,
//       Ctrl+Space vänder, Ctrl+A väljer alla (och bara i flerval)
//   M4  EN `onChange` + EN `onCommit` per handling som ändrade valet, inget av
//       settrarna eller av en handling som inte ändrade något
//   M5  `setItems` behåller valet per index och klipper det som försvann
//   M6  `setMultiSelect(false)` behåller `selectedIndex()` (den aktuella om
//       den är vald) — och `selectedIndex()` i flerval
//   M7  SOLO VID SLÄPPET (med fönster, i geometriblocket): ett tryck på en
//       vald rad väljer den ensam först i `mouseUp`
// SABOTAGEN som ska fälla dem: `isSelected` utan flervalsvektorn (M2), Ctrl+A
// också i enkelval (M3 — `Prov` i enkelval svarar false), `assignSelection`
// utan ändringsvakten (M4: ett onChange för en oförändrad mängd), `setItems`
// som tömmer valet (M5), `mouseDown` utan solovalet (M7: valet krymper redan
// vid trycket).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;   // överhoppade block — SYNLIGA i sammanfattningen
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

// De skyddade vägarna anropbara — precis det en subklass får göra, och
// exakt så dropdownens popup-lista når dem.
struct Prov : glaze::ListBox {
    using glaze::ListBox::rowTop;
    using glaze::ListBox::keyDown;
    using glaze::ListBox::selectFromUser;
    using glaze::ListBox::setProvisionalIndex;
    using glaze::ListBox::toggleCheckFromUser;
    using glaze::ListBox::mouseDown;
    using glaze::ListBox::mouseUp;
    using glaze::ListBox::rowRect;
    using glaze::ListBox::rightDown;
};

std::string rader(const std::vector<int>& v)
{
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? "," : "") + std::to_string(v[i]);
    return s + "}";
}

// VARIABEL RADHÖJD — den väg menyerna öppnade i basen.
// Utan de här fallen hade geometriomskrivningen haft NOLL regressionsskydd:
// de övriga fallen mäter tillstånd och event, aldrig pixlar.
// Var tredje rad är "tunn" som en separator.
struct Varierad : glaze::ListBox {
    using glaze::ListBox::firstVisibleRow;
    using glaze::ListBox::rowAtOffset;
    using glaze::ListBox::rowTop;
    using glaze::ListBox::totalRowsH;
    using glaze::ListBox::ensureVisible;

    static constexpr float kTunn = 7.0f;

    bool  uniformRows() const override { return false; }
    float rowH(const glaze::Ctx& c, int row) const override
    {
        return (row % 3 == 2) ? kTunn : c.style->listRowH;
    }
};

// Registret måste känna typen — annars faller GlazeBuilders palett och
// previewn tillbaka på en stand-in utan att bygget säger något.
void fallRegistret()
{
    auto w = glaze::makeWidget("listbox");
    check(w != nullptr, "makeWidget(\"listbox\") ger en widget");
    if (w)
        check(dynamic_cast<glaze::ListBox*>(w.get()) != nullptr,
              "registret ger en riktig glaze::ListBox");
}

void fallSetProp()
{
    glaze::ListBox l;
    check(l.setProp("items", "Alfa;Beta;Gamma"), "setProp: items känns igen");
    check(l.items().size() == 3, "setProp: items splittas på ';'",
          std::to_string(l.items().size()));
    check(l.setProp("selectedIndex", "2"), "setProp: selectedIndex känns igen");
    check(l.selectedIndex() == 2, "setProp: selectedIndex sätts",
          std::to_string(l.selectedIndex()));
    check(l.setProp("showChecks", "true"), "setProp: showChecks känns igen");
    check(l.showsChecks(), "setProp: showChecks sätts");
    check(!l.setProp("dettaFinnsInte", "1"),
          "setProp: okänd nyckel svarar false (opak rad)");
    // Basen (Panel/Widget) måste fortfarande nås — subklassen anropar den
    // SIST, annars tappas hela layout-vokabulären.
    check(l.setProp("border", "false"), "setProp: Panels egna nycklar når basen");
    check(l.setProp("visible", "false"), "setProp: Widgets nycklar når basen");
}

// TOM LISTA och INDEX UTANFÖR — de två fall som en widget med ett rått
// index alltid går sönder på.
void fallTomOchUtanfor()
{
    glaze::ListBox l;
    check(l.selectedText().empty(), "tom lista: selectedText är tom");
    check(l.selectedIndex() == -1, "tom lista: selectedIndex är -1",
          std::to_string(l.selectedIndex()));
    check(!l.isChecked(0), "tom lista: isChecked(0) är false");
    check(!l.isChecked(-1), "tom lista: isChecked(-1) är false");
    l.setShowChecks(true);
    check(l.showsChecks(), "tom lista + kryss påslaget är lagligt");

    l.setItems({"a", "b"});
    l.setSelectedIndex(9);
    check(l.selectedText().empty(), "index utanför: selectedText är tom",
          l.selectedText());
    check(l.selectedIndex() == 9,
          "index utanför LAGRAS ändå (visningen vaktar — Dropdown-beteendet)",
          std::to_string(l.selectedIndex()));
    l.setChecked(9, true);
    check(!l.isChecked(9), "setChecked utanför intervallet är no-op");
}

// setItems som KRYMPER: kryssen klipps, changedRow nollställs, och ett
// index som hamnat utanför rör inte lagringen.
void fallSetItemsKrymper()
{
    Prov l;
    l.setItems({"a", "b", "c", "d"});
    l.setChecked(3, true);
    l.setChecked(1, true);
    check(l.isChecked(3) && l.isChecked(1), "checks sätts");

    l.toggleCheckFromUser(2);
    check(l.changedRow() == 2, "changedRow sätts av en KRYSShändelse",
          std::to_string(l.changedRow()));

    l.setItems({"a", "b"});
    check(l.items().size() == 2, "setItems krymper listan",
          std::to_string(l.items().size()));
    check(l.isChecked(1), "checks som ryms behålls");
    check(!l.isChecked(3), "checks utanför den nya listan är borta");
    check(l.changedRow() == -1, "setItems nollställer changedRow",
          std::to_string(l.changedRow()));

    // Växer den igen ska de nya raderna vara OKRYSSADE — inte bära ett
    // gammalt värde ur en vektor som aldrig krympte.
    l.setItems({"a", "b", "c", "d"});
    check(!l.isChecked(3), "nya rader är okryssade efter att listan växt");
}

// KONTRAKTET som gör dubbelfyrning omöjlig: en användarväg fyrar exakt en
// gång, varje programmatisk väg noll.
void fallEventKontraktet()
{
    int  byten = 0, kryss = 0;
    Prov l;
    l.setItems({"a", "b", "c"});
    l.onChange = [&byten] { ++byten; };
    l.onCheck  = [&kryss] { ++kryss; };

    l.setSelectedIndex(1);
    check(byten == 0, "setSelectedIndex fyrar INTE onChange",
          std::to_string(byten));
    l.setProvisionalIndex(2);
    check(byten == 0 && l.selectedIndex() == 2,
          "setProvisionalIndex ändrar men fyrar INTE (popup-vägen)",
          std::to_string(byten) + "/" + std::to_string(l.selectedIndex()));
    l.setChecked(0, true);
    check(kryss == 0, "setChecked fyrar INTE onCheck", std::to_string(kryss));
    check(l.changedRow() == -1, "programmatisk setChecked sätter INTE changedRow",
          std::to_string(l.changedRow()));

    l.selectFromUser(0);
    check(byten == 1 && l.selectedIndex() == 0,
          "selectFromUser: EXAKT ETT onChange",
          std::to_string(byten) + "/" + std::to_string(l.selectedIndex()));
    l.selectFromUser(0);
    check(byten == 1, "selectFromUser på SAMMA rad fyrar inte igen",
          std::to_string(byten));
    l.selectFromUser(99);
    check(byten == 1 && l.selectedIndex() == 0,
          "selectFromUser utanför intervallet gör ingenting",
          std::to_string(byten));

    l.toggleCheckFromUser(1);
    check(kryss == 1 && l.isChecked(1) && l.changedRow() == 1,
          "toggleCheckFromUser: EXAKT ETT onCheck + changedRow",
          std::to_string(kryss) + "/" + std::to_string(l.changedRow()));
    check(l.selectedIndex() == 0 && byten == 1,
          "ett kryss FLYTTAR INTE valet och fyrar inget onChange",
          std::to_string(l.selectedIndex()) + "/" + std::to_string(byten));
    l.toggleCheckFromUser(1);
    check(kryss == 2 && !l.isChecked(1), "toggleCheckFromUser togglar tillbaka");

    // KRYSS -> RENT VAL: `changedRow` måste SLÄPPA sitt värde. Utan den här
    // sekvensen läste nästa `onChange` en rad som kryssades för
    // länge sedan — kontraktet stod i listbox.h men gällde inte i koden.
    l.toggleCheckFromUser(2);
    check(l.changedRow() == 2, "changedRow bärs av krysshändelsen",
          std::to_string(l.changedRow()));
    l.selectFromUser(1);
    check(l.changedRow() == -1,
          "en REN valhändelse NOLLSTÄLLER changedRow (kontraktet i listbox.h)",
          std::to_string(l.changedRow()));
    const int fore = kryss;
    l.toggleCheckFromUser(9);
    check(kryss == fore, "toggleCheckFromUser utanför intervallet är no-op",
          std::to_string(kryss) + " mot " + std::to_string(fore));
}

// TANGENTBORDET: valet flyttas (klampat), Space togglar bara när kryssen
// visas, och modifierade tangenter lämnas åt framtida flerval.
void fallTangenter()
{
    int  byten = 0, kryss = 0;
    Prov l;
    l.setItems({"a", "b", "c"});
    l.onChange = [&byten] { ++byten; };
    l.onCheck  = [&kryss] { ++kryss; };

    check(l.keyDown(gfx::Key::Down, 0), "Down konsumeras");
    check(l.selectedIndex() == 0 && byten == 1, "Down från -1 väljer första",
          std::to_string(l.selectedIndex()) + "/" + std::to_string(byten));
    l.keyDown(gfx::Key::End, 0);
    check(l.selectedIndex() == 2 && byten == 2, "End går till sista raden",
          std::to_string(l.selectedIndex()));
    l.keyDown(gfx::Key::Down, 0);
    check(l.selectedIndex() == 2 && byten == 2,
          "Down vid kanten ändrar inget och fyrar inget",
          std::to_string(byten));
    l.keyDown(gfx::Key::Home, 0);
    check(l.selectedIndex() == 0 && byten == 3, "Home går till första raden",
          std::to_string(l.selectedIndex()));

    check(!l.keyDown(gfx::Key::Space, 0),
          "Space utan kryss bubblar vidare (false)");
    check(kryss == 0, "Space utan kryss togglar ingenting");
    l.setShowChecks(true);
    check(l.keyDown(gfx::Key::Space, 0), "Space med kryss konsumeras");
    check(kryss == 1 && l.isChecked(0), "Space togglar VALDA radens kryss",
          std::to_string(kryss));

    check(!l.keyDown(gfx::Key::Down, gfx::kModCtrl),
          "Ctrl+Down konsumeras INTE (reserverad för flerval)");
    check(l.selectedIndex() == 0, "Ctrl+Down flyttar inte valet",
          std::to_string(l.selectedIndex()));

    Prov tom;
    check(!tom.keyDown(gfx::Key::Down, 0), "tom lista: Down bubblar vidare");
}

// AKTIVERINGEN — A1–A3, listvyns form. Sabotage: Return utan
// `!onActivate`-vakten (A1 fäller), aktivering utan vald rad (A2 fäller).
// Dubbelklicket (mouseDoubleClick) kräver ett Ctx och mäts inte här.
void fallAktivering()
{
    int  akt = 0;
    Prov l;
    l.setItems({"a", "b", "c"});
    l.setSelectedIndex(1);
    check(!l.keyDown(gfx::Key::Return, 0),
          "A1: Enter UTAN onActivate bubblar (standardknappen nås)");
    l.onActivate = [&akt] { ++akt; };
    check(l.keyDown(gfx::Key::Return, 0) && akt == 1,
          "A3: Enter MED onActivate och vald rad aktiverar EN gång",
          std::to_string(akt));
    l.setSelectedIndex(-1);
    check(!l.keyDown(gfx::Key::Return, 0) && akt == 1,
          "A2: Enter utan vald rad bubblar och aktiverar inte",
          std::to_string(akt));
}

// FLERVALET — M1–M6, se filhuvudet.
void fallFlerval()
{
    using V = std::vector<int>;
    int  byten = 0, klara = 0;
    Prov l;
    l.setItems({"a", "b", "c", "d", "e"});
    l.onChange = [&byten] { ++byten; };
    l.onCommit = [&klara] { ++klara; };

    // M1: enkelval — setSelected är setSelectedIndex, inget fyras.
    l.setSelected(2, true);
    check(l.selectedIndex() == 2 && l.selectedRows() == V{2} && byten == 0,
          "M1: enkelval: setSelected(2, true) = setSelectedIndex(2), inget "
          "event", rader(l.selectedRows()));
    l.setSelected(2, false);
    check(l.selectedIndex() == -1, "M1: enkelval: setSelected(2, false) väljer "
                                   "bort den valda");
    check(!l.keyDown(gfx::Key::A, gfx::kModCtrl),
          "M3: Ctrl+A konsumeras INTE i enkelval");

    l.setMultiSelect(true);
    check(l.isMultiSelect() && l.selectedRows().empty(),
          "M2: flervalet på, inget valt");
    // M2: klick, Ctrl, Shift, Ctrl+Shift.
    l.selectFromUser(1, 0, false);
    check(l.selectedRows() == V{1} && byten == 1 && klara == 1,
          "M2/M4: klick väljer bara raden — ETT onChange och ETT onCommit",
          rader(l.selectedRows()));
    l.selectFromUser(3, gfx::kModCtrl, false);
    check(l.selectedRows() == V{1, 3} && byten == 2,
          "M2: Ctrl+klick vänder raden in", rader(l.selectedRows()));
    l.selectFromUser(3, gfx::kModCmd, false);
    check(l.selectedRows() == V{1} && byten == 3,
          "M2: Cmd+klick vänder raden ut (båda räknas)", rader(l.selectedRows()));
    l.selectFromUser(4, gfx::kModShift, false);
    check(l.selectedRows() == V{3, 4} && byten == 4,
          "M2: Shift+klick väljer spannet från ANKARET (rad 3, där Cmd "
          "vände)", rader(l.selectedRows()));
    l.selectFromUser(0, gfx::kModCtrl, false);
    l.selectFromUser(1, gfx::kModCtrl | gfx::kModShift, false);
    check(l.selectedRows() == V{0, 1, 3, 4},
          "M2: Ctrl+Shift+klick LÄGGER TILL spannet", rader(l.selectedRows()));
    const int före = byten;
    l.selectFromUser(4, gfx::kModShift | gfx::kModCtrl, false);
    // ankaret är 0 ⇒ spannet 0–4 läggs till ⇒ alla
    check(l.selectedRows() == V{0, 1, 2, 3, 4} && byten == före + 1,
          "M2: Ctrl+Shift från ankaret 0 till 4 ger alla",
          rader(l.selectedRows()));
    l.selectFromUser(4, gfx::kModShift | gfx::kModCtrl, false);
    check(byten == före + 1 && klara == byten,
          "M4: en handling som inte ändrar valet fyrar INGET",
          std::to_string(byten));

    // M3: tangenterna.
    l.selectFromUser(1, 0, false);
    l.keyDown(gfx::Key::Down, gfx::kModShift);
    l.keyDown(gfx::Key::Down, gfx::kModShift);
    check(l.selectedRows() == V{1, 2, 3} && l.currentRow() == 3,
          "M3: Shift+Ned utökar från ankaret", rader(l.selectedRows()));
    const int f2 = byten;
    l.keyDown(gfx::Key::Down, gfx::kModCtrl);
    check(l.selectedRows() == V{1, 2, 3} && l.currentRow() == 4 && byten == f2,
          "M3: Ctrl+Ned flyttar den aktuella UTAN att välja (och fyrar inget)",
          rader(l.selectedRows()));
    check(l.keyDown(gfx::Key::Space, gfx::kModCtrl) &&
              l.selectedRows() == V{1, 2, 3, 4} && byten == f2 + 1,
          "M3: Ctrl+Space vänder den aktuella in", rader(l.selectedRows()));
    l.selectFromUser(0, 0, false);
    check(l.keyDown(gfx::Key::A, gfx::kModCtrl) &&
              l.selectedRows() == V{0, 1, 2, 3, 4},
          "M3: Ctrl+A väljer alla", rader(l.selectedRows()));
    const int f3 = byten;
    l.keyDown(gfx::Key::A, gfx::kModCtrl);
    check(byten == f3, "M4: Ctrl+A när alla redan är valda fyrar inget");

    // M4: settrarna fyrar aldrig.
    const int f4 = byten;
    l.clearSelection();
    l.setSelected(2, true);
    l.selectAll();
    l.setSelectedIndex(3);
    check(byten == f4 && l.selectedRows() == V{3} && l.currentRow() == 3,
          "M4: clearSelection/setSelected/selectAll/setSelectedIndex fyrar "
          "inget", rader(l.selectedRows()));
    l.setSelected(9, true);
    check(l.selectedRows() == V{3}, "M4: setSelected utanför är no-op");

    // M5: setItems behåller valet per index.
    l.setSelected(4, true);
    l.setItems({"a", "b", "c", "d"});
    check(l.selectedRows() == V{3}, "M5: setItems klipper rad 4 och behåller 3",
          rader(l.selectedRows()));
    l.addItem("e");
    check(!l.isSelected(4), "M5: en ny rad är aldrig vald");

    // M6: selectedIndex i flerval och setMultiSelect(false).
    l.setSelected(1, true);
    check(l.selectedIndex() == 3, "M6: selectedIndex = den aktuella när den är "
                                  "vald", std::to_string(l.selectedIndex()));
    l.setSelected(3, false);
    check(l.selectedIndex() == 1, "M6: annars den första valda",
          std::to_string(l.selectedIndex()));
    check(l.selectedText() == "b", "M6: selectedText följer selectedIndex",
          l.selectedText());
    l.setMultiSelect(false);
    check(!l.isMultiSelect() && l.selectedIndex() == 1 &&
              l.selectedRows() == V{1},
          "M6: setMultiSelect(false) behåller bara selectedIndex()",
          rader(l.selectedRows()));
    check(byten == f4, "M6: bytet av läge fyrar inget");
}

// ETT tabbstopp — listan har inga widget-barn, så `collectFocusable`
// hittar bara den själv. Det var hela skälet raderna är DATA.
void fallFokus()
{
    glaze::ListBox l;
    l.setItems({"a", "b", "c"});
    check(l.focusable(), "listan är ett tabbstopp");
    check(l.children().empty(), "listan har INGA widget-barn (rader är data)",
          std::to_string(l.children().size()));
}

} // namespace

// GEOMETRIN med blandade radhöjder — KRÄVER fönster (Ctx med font och
// renderer), så den ligger sist och följer dropdown_gates mönster: `run()`
// öppnar SJÄLV (aldrig show() först — då avvisar runInternal och kedjan dör
// tyst), och `kordes` gör en överhoppning synlig i stället för grön.
struct GeoVard : glaze::AppWindow {
    Varierad lst;
    Prov     uni;
    GeoVard() : glaze::AppWindow("listbox_gate", 400, 300)
    {
        lst.x = 10; lst.y = 10; lst.width = 200; lst.height = 200;
        uni.x = 220; uni.y = 10; uni.width = 150; uni.height = 200;
        root().adopt(lst);
        root().adopt(uni);
    }
    using glaze::AppWindow::makeCtx;
};

bool nara(float a, float b) { return std::fabs(a - b) < 0.51f; }

void fallVariabelGeometri()
{
    GeoVard w;
    w.setOffscreen(true);
    w.lst.setItems({"r0", "r1", "r2", "r3", "r4", "r5", "r6"});
    w.uni.setItems({"a", "b", "c", "d"});

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const glaze::Ctx c = w.makeCtx();
        const float rh = c.style->listRowH;
        const float tu = Varierad::kTunn;

        // PREFIXSUMMAN — inte n * listRowH
        check(nara(w.lst.rowTop(c, 0), 0.0f), "rowTop(0) = 0");
        check(nara(w.lst.rowTop(c, 2), 2.0f * rh),
              "rowTop summerar FÖREGÅENDE raders höjder",
              std::to_string(w.lst.rowTop(c, 2)));
        check(nara(w.lst.rowTop(c, 3), 2.0f * rh + tu),
              "rowTop efter en TUNN rad räknar rätt",
              std::to_string(w.lst.rowTop(c, 3)));

        const float vantad = 5.0f * rh + 2.0f * tu;   // 7 rader: 5 normala, 2 tunna
        check(nara(w.lst.totalRowsH(c), vantad),
              "totalRowsH summerar VARIABLA höjder",
              std::to_string(w.lst.totalRowsH(c)) + " mot " + std::to_string(vantad));

        // HIT-TEST över radgränser — ritning och träff måste vara EN sanning
        check(w.lst.rowAtOffset(c, rh - 0.5f) == 0,
              "hit-test precis FÖRE gränsen ger raden ovan",
              std::to_string(w.lst.rowAtOffset(c, rh - 0.5f)));
        check(w.lst.rowAtOffset(c, rh + 0.5f) == 1,
              "hit-test precis EFTER gränsen ger nästa rad",
              std::to_string(w.lst.rowAtOffset(c, rh + 0.5f)));
        check(w.lst.rowAtOffset(c, 2.0f * rh + tu * 0.5f) == 2,
              "hit-test mitt i en TUNN rad träffar DEN, inte grannen",
              std::to_string(w.lst.rowAtOffset(c, 2.0f * rh + tu * 0.5f)));
        check(w.lst.rowAtOffset(c, -1.0f) < 0, "hit-test ovanför listan ger -1");
        check(w.lst.rowAtOffset(c, vantad + 50.0f) < 0,
              "hit-test under sista raden ger -1");

        // FÖRSTA SYNLIGA RAD efter en tunn rad
        check(w.lst.firstVisibleRow(c, 0.0f) == 0, "firstVisibleRow utan scroll = 0");
        check(w.lst.firstVisibleRow(c, 2.0f * rh + tu) == 3,
              "firstVisibleRow räknar med den tunna raden",
              std::to_string(w.lst.firstVisibleRow(c, 2.0f * rh + tu)));

        // UNIFORM väg måste ge samma svar som en vanlig division
        check(nara(w.uni.rowTop(c, 3), 3.0f * rh),
              "uniformRows: prefixsumman är identisk med n * listRowH",
              std::to_string(w.uni.rowTop(c, 3)));

        // M7 — SOLO VID SLÄPPET: ett tryck på en VALD rad i
        // flerval väljer den ensam först i `mouseUp`.
        {
            int byten = 0;
            w.uni.setMultiSelect(true);
            w.uni.selectFromUser(0, 0, false);
            w.uni.selectFromUser(2, gfx::kModCtrl, false);
            w.uni.onChange = [&byten] { ++byten; };
            const gfx::Rectf r2 = glaze::Draw::project(
                c, w.uni.resolve(c), w.uni.rowRect(c, 2));
            const float mx = r2.x + r2.w / 2.0f, my = r2.y + r2.h / 2.0f;
            w.uni.mouseDown(c, mx, my);
            check(w.uni.selectedRows() == std::vector<int>{0, 2} && byten == 0,
                  "M7: trycket på en vald rad ändrar inte valet",
                  rader(w.uni.selectedRows()));
            check(w.uni.mouseUp(c, mx, my) &&
                      w.uni.selectedRows() == std::vector<int>{2} && byten == 1,
                  "M7: släppet väljer raden ensam — ett onChange",
                  rader(w.uni.selectedRows()));
            w.uni.onChange = nullptr;
        }

        // Q1–Q4 — SNABBMENYN. Träffvägen i LOGISKA px
        // från listans hörn ur samma `rowRect` som ritningen, och högertryckets
        // val genom kroken med fönstrets Ctx. SABOTAGE: raden räknad utan
        // scrollen (Q1 rullad).
        {
            w.uni.setMultiSelect(false);
            std::vector<std::string> tjugo;
            for (int i = 0; i < 20; ++i)
                tjugo.push_back("r" + std::to_string(i));
            w.uni.setItems(tjugo);
            w.uni.setScroll(0.0f, 0.0f);
            const auto mitt = [&](int rad) {
                const gfx::Rectf r = w.uni.rowRect(c, rad);
                return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
            };
            const auto fys = [&](gfx::Pointf p) {
                const gfx::Rectf b = w.uni.resolve(c);
                return gfx::Pointf{b.x + p.x * c.scale, b.y + p.y * c.scale};
            };
            check(w.uni.rowAt(mitt(0).x, mitt(0).y) == 0 &&
                      w.uni.rowAt(mitt(3).x, mitt(3).y) == 3,
                  "Q1: rowAt ger radens index");
            const float utanför = -0.5f;   // vänster om innehållsytan
            const float remsa   = w.uni.width - c.style->scrollThick / 2.0f;
            check(w.uni.rowAt(utanför, mitt(1).y) == -1 &&
                      w.uni.rowAt(remsa, mitt(1).y) == -1,
                  "Q1: utanför i x och i remsan — −1",
                  std::to_string(w.uni.rowAt(remsa, mitt(1).y)));
            const gfx::Pointf p0 = mitt(0);
            w.uni.setScroll(0.0f, c.style->listRowH);
            check(w.uni.rowAt(p0.x, p0.y) == 1,
                  "Q1: rullad en rad — samma punkt ger nästa rad",
                  std::to_string(w.uni.rowAt(p0.x, p0.y)));
            w.uni.setScroll(0.0f, 0.0f);

            int byten = 0;
            w.uni.onChange = [&byten] { ++byten; };
            // Q2 ENKELVAL: en ovald rad väljs, en vald rad lämnas.
            w.uni.setSelectedIndex(0);
            w.uni.rightDown(c, fys(mitt(2)).x, fys(mitt(2)).y);
            check(w.uni.selectedIndex() == 2 && byten == 1 &&
                      w.uni.contextRow() == 2,
                  "Q2: enkelval — en ovald rad väljs (ett onChange)",
                  std::to_string(w.uni.selectedIndex()));
            w.uni.rightDown(c, fys(mitt(2)).x, fys(mitt(2)).y);
            check(byten == 1 && w.uni.contextRow() == 2,
                  "Q2: enkelval — den valda raden ger inget onChange");
            // Q3 FLERVAL: utanför valet väljs bara raden, i valet står valet.
            w.uni.setMultiSelect(true);
            w.uni.selectFromUser(0, 0, false);
            w.uni.selectFromUser(2, gfx::kModCtrl, false);
            byten = 0;
            w.uni.rightDown(c, fys(mitt(2)).x, fys(mitt(2)).y);
            w.uni.rightDown(c, fys(mitt(0)).x, fys(mitt(0)).y);
            check(w.uni.selectedRows() == std::vector<int>{0, 2} && byten == 0 &&
                      w.uni.currentRow() == 0 && w.uni.contextRow() == 0,
                  "Q3: flerval — i valet står valet, raden blir aktuell",
                  rader(w.uni.selectedRows()));
            w.uni.rightDown(c, fys(mitt(1)).x, fys(mitt(1)).y);
            check(w.uni.selectedRows() == std::vector<int>{1} && byten == 1,
                  "Q3: flerval — utanför valet väljs bara raden",
                  rader(w.uni.selectedRows()));
            w.uni.onChange = nullptr;
            // Q4: `setItems` nollar högertryckets rad.
            w.uni.setItems({"x", "y"});
            check(w.uni.contextRow() == -1, "Q4: setItems ger contextRow() −1");
        }

        w.close();
    });
    w.run();

    if (!kordes) {
        std::printf("  --   geometrifallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("listbox_gate\n");
    fallRegistret();
    fallSetProp();
    fallTomOchUtanfor();
    fallSetItemsKrymper();
    fallEventKontraktet();
    fallTangenter();
    fallAktivering();
    fallFlerval();
    fallVariabelGeometri();
    fallFokus();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
