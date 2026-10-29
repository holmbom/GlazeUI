// commit_gate — COMMIT-HÄNDELSEN, FOKUSHÄNDELSERNA, MARKERINGEN och
// PropertyGrids redigering.
//
// Kör:  ./build/tests/commit_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt, och
//       skippet SYNS i ctest
//
// BAKGRUNDEN: en PropertyGrid som committar LIVE — varje tangent skriver radens
// värde och fyrar onChange — stänger editorn efter första tecknet när
// handlern räknar om och bygger om raderna. Då går det inte att skriva ett värde.
// Lösningen är ett event för "användaren är
// KLAR" på varje inmatande widget (`onCommit`), fokus INOM (`onFocus`/
// `onFocusLost` på `Widget`) och en programmatisk markering.
//
// FÖRVÄNTAT VÄRDE ÄR GRINDENS EGET: en EVENTLOGG som grindens egna handlers skriver
// ("namn:change", "namn:commit", "namn:focus" …), och förväntningarna är
// skrivna för hand ur designens tabell — vilken handling som ska ge vilka
// events, i vilken ordning. Fokusordningens förväntade värde räknas ur trädets
// förälderkedjor i grinden, aldrig ur produktionens kod.
//
// TVÅDELAD:
//
//   HEADLESS      MARKERINGEN på TextInput och TextArea (byte-index, klampat,
//                 nedsnappat till teckengräns, bakåt, inga events men
//                 invaliderat) och VIDAREBEFORDRAN i talfältet,
//                 datumväljaren och filväljarfältet · att PROGRAMMATISKA
//                 settrar aldrig fyrar onChange eller onCommit, på varje
//                 widget i tabellen.
//   MED FÖNSTER   ett DOLT fönster (`setOffscreen`), syntetiska händelser
//                 genom fönstrets RIKTIGA ingång (`AppWindow::handleEvent`)
//                 och stegen som en KEDJA AV POSTADE STEG — fokushändelserna
//                 och fokusförlustens commits verkställs i pump-svansen, så
//                 de räknas i steget EFTER handlingen. Tabellen widget för
//                 widget:
//                   TextInput   onChange per tangent, onCommit vid Enter
//                               (som INTE konsumeras — en förfader ser den)
//                               och vid fokus bort (postat); oförändrat ⇒
//                               ingen commit; setText flyttar baslinjen men
//                               ett eko av samma text gör det inte; fönstrets
//                               fokusförlust committar inte
//                   TextArea    Enter är en ny rad, commit bara vid fokus bort
//                   SpinBox     att skriva ändrar inget; Enter, fokus bort,
//                               pilen, hjulet och stegaren ger onChange OCH
//                               onCommit tillsammans; otolkbar text skrivs
//                               tillbaka utan event
//                   DatePicker  samma för fältet, och kalenderns val
//                   ColorPicker paletten och reglagets steg tillsammans, och
//                               en skriven hexfärg som committas av Escape
//                   FilePicker  fältets commit vid Enter och när fokus går
//                               till "…"-knappen — utan att kompositen
//                               tappar fokus INOM (dialogens val mäter
//                               filepicker_gate)
//                   Slider      live under draget, EN commit vid släppet,
//                               ingen för ett tryck som inte ändrade något;
//                               steg och hjul tillsammans
//                   Scrollbar   rännklicket committar vid släppet, hjulet
//                               tillsammans
//                   Checkbox, Radio, ToolButton, Dropdown, ListBox (val OCH
//                               kryss), TreeView, ListView — tillsammans,
//                               onChange (onCheck) först
//                   PropertyGrid  "12"+Enter, "12"+Escape,
//                               "12"+klick på en annan rad, en onCommit som
//                               anropar setRows river inte nästa redigering,
//                               fokus bort (postat), oförändrat, markerat allt
//                               när editorn öppnas, och vallistan
//                   DataGrid    onChange OCH onCommit bär samma rad/kolumn
//                               (båda mätta), också postat
//                 och FOKUS INOM: lost innerst först och focus ytterst först
//                 vid klick och Tab, kompositerna (talfältet, filväljaren),
//                 roten fyrar aldrig, fönstrets fokusförlust fyrar inte,
//                 POSTAT och inte synkront, en widget som rivs innan svansen
//                 får inget — och `selectAll` ur `onFocus` vid klick och Tab.
//                 EN HANDLER SOM RÖR WIDGETEN ELLER FÖNSTRET:
//                   R1–R5   en handler som LOSSAR (eller river, R3) widgeten
//                           mellan paret: inget onCommit/onClick efteråt, och
//                           ingen fortsättning — egenskapslistans klick
//                           öppnar ingen editor (R1), datagridens Enter
//                           flyttar inte cellen (R5)
//                   W1, X2  commitFromUser rör inte fönstret efter handlern —
//                           avgörbart när fältet FLYTTAS (W1, fönstrets
//                           damage-stämpel), kraschfritt när fönstret RIVS (X2)
//                   X1      en handler i fokuskön river fönstret — resten
//                           levereras inte till en lånad widget som lever
//                   N1, G10, X3  köade events slopas när widgeten lossas och
//                           sätts in igen (N1), lossas (G10) eller när dess
//                           fönster dör (X3)
//                   P8      changedRow() i onCommit efter en onChange som
//                           byggde om raderna
//                   P9, P10 setRow slopar en köad sparning av SAMMA rad, inte
//                           av en annan
//                   O1      fokusordningen genom en nästlad pump (en modal
//                           dialog ur onFocusLost som byter fokus)
//                   E1–E8   commitEdits() ur en verktygsknapp (talfältet),
//                           och ur koden genom varje övrig override:
//                           egenskapslistan, textfältet, filväljarfältet,
//                           datumväljaren, det flerradiga fältet, datagriden
//                           och färgväljarens hexfält i en öppen popup
//                   Z1–Z3   dragtröskeln efter ett fokusklick: darr rör inte
//                           selectAll (TextInput, TextArea), ett drag ankras
//                           vid trycket
//                 ÄGDA WIDGETAR, KÖADE SPARNINGAR OCH EDITORER:
//                   R6, R7  en ÄGD widget river sig själv i sin handler
//                           (onChange, och onFocusLost i fokusleveransen) och
//                           läser SEDAN sin capture — det anropsbara lever
//                           genom anropet (Handler håller en egen referens)
//                   P11,P12 en köad sparning och färre rader (setRows);
//                           addRow + setRow på en tom grid. Att `rows()` är
//                           en läsväg fäller en static_assert överst i filen
//                   E9, E10 en editors EGEN onCommit flyttar fokus mitt i
//                           commitEdits() — kompositens (gridens, datagridens)
//                           commit är ändå levererad när den återvänder
//                   E11     commitEdits() utan något att committa ritar inte
//                           om fönstret; med en commit gör den det
//                   E12     FilePicker::commitEdit() anropad direkt
//                   Z4, Z5  omankringen ensam (careten står still) RITAS OM —
//                           mätt med damage-stämpeln, efter att hovern lagt
//                           sig (pekarens första rörelse ritar om av sig själv)
//                 FÖNSTER SOM RIVS, REKURSION OCH UNDANTAG:
//                   RV1–RV3 en handler river sitt FÖNSTER mitt i dess dispatch
//                           — musvägen (ett talfälts steg), tangentvägen
//                           konsumerad (talfältets Upp — Enter bubblar)
//                           och inte konsumerad (ett
//                           textfälts onCommit). Dispatchen slutar utan
//                           att läsa det döda fönstret; avgörande under ASan
//                   E13     en rekursiv commitEdits() ("Spara" i editorns
//                           onCommit) levererar det den yttre samlat
//                   E14     en modal dialog i en insamlad handler levererar
//                           resten av kön i sin pump, i ordning
//                   E15     ett undantag ur en handler i kedjan når anroparen,
//                           och det som hann samlas in levereras i svansen
//                   E16,E17 en fokuserad OFÖRÄNDRAD SpinBox/DatePicker ritas
//                           inte om av commitEdits()
//                 ÅNGRA-HISTORIK:
//                   U1      egenskapslistans editor börjar varje rad med TOM
//                           ångra-historik, också när nästa rad har samma
//                           värde som det förra fick (sabotage: `börja` utan
//                           `resetHistory` i property_grid.cpp)
//
// SABOTAGEN den fäller (ett i taget, fallen inom parentes):
//   * PropertyGrid med LIVE-commit tillbaka, editorns onChange skriver
//     värdet (P1, P2, P3, P4 — "editorn står kvar efter första tecknet" —
//     och P5)
//   * PropertyGrid::mouseDown vars commit POSTAS i stället för att fyras
//     direkt — handlerns setRows stänger då den nyss öppnade editorn i
//     svansen (P3, P4 och "står kvar också efter pump-svansen")
//   * TextInput::commitFromUser utan ändringskollen (T2, T4)
//   * TextInput::focusLost utan ändringskollen (T5)
//   * TextInput::focusLost som fyrar onCommit DIREKT, inte postat (T3)
//   * Enter KONSUMERAD av TextInput (T1 — förfadern ser den inte)
//   * fokushändelserna fyrade SYNKRONT i AppWindow (G0, G1, G6 — den rivna
//     widgetens handler körde — G8, G9, T3)
//   * onFocus INNERST först (G1) · onFocusLost YTTERST först (G2, G3)
//   * talfältet LIVE igen, fältets onChange → textFromUser (S1, S3)
//   * färgväljarens stängning som inte committar hexfältet (C3)
//   * datagriden utan onCommit (DG1, DG2)
//   * TextArea som committar vid Enter (A1 — två commits)
// …och för handlerfallen:
//   * `firePair` utan vakten emellan (R2; sedan KRASCH i R3 — onCommit lästes
//     ur den rivna kryssrutan, alltså use-after-free)
//   * PropertyGrid::fireChange utan vakten (R1 — onCommit fyrades) ·
//     PropertyGrid::mouseDown som fortsätter efter commiten (R1 — editor 1)
//   * commitFromUser med fönsterpekaren tagen FÖRE handlern (W1)
//   * HandlerGuard utan fönstrets liv (X1, X3) · utan anknytningen (R1, R2,
//     R4, R5, N1, G10)
//   * changedRow sätts inte igen före onCommit (P8)
//   * setRow utan radgenerationen (P9)
//   * en egen kö per fokusbyte (O1)
//   * commitEdits bara på den fokuserade widgeten (E1, E2, E5, E7) · varje
//     override av commitEdit() tom, en i taget: talfältet (E1),
//     egenskapslistan (E2), textfältet (E3, E4, W1, X2), datumväljaren (E5),
//     det flerradiga fältet (E6), datagriden (E7), färgväljaren (E8)
//   * TextInput utan det hållna trycket (Z1, Z2) · utan omankringen (Z2) ·
//     TextArea utan det hållna trycket (Z3)
//   * datagridens onChange utan cellen i args (DG1, DG2)
// …och för ägda widgetar och editorer:
//   * den muterbara `rows()` tillbaka (BYGGET faller — static_assert)
//   * addRow utan radgenerationen (P12 — libstdc++:s indexkontroll avbryter)
//   * postEvent som inte samlas under commitEdits (E9, E10)
//   * commitEdits som alltid ritar om (E11) · tom FilePicker::commitEdit (E12)
//   * omankringen utan omritning, TextInput (Z4) och TextArea (Z5)
//   * Handler som anropar utan egen referens: GRÖN utan sanerare (det
//     frigjorda minnet stod kvar) — RÖD under AddressSanitizer (nedan)
//
// UNDER ADDRESSSANITIZER (WSL, egen byggkatalog med -fsanitize=address,
// `ASAN_OPTIONS=detect_leaks=0`): osaboterat GRÖNT på Wayland och X11, och
// de fyra use-after-free-sabotagen fälls med en ASan-rapport — Handler utan
// egen referens (R6), firePair utan vakt (R3), commitFromUser med
// fönsterpekaren före handlern (X2) och vakten utan fönstrets liv (X2).
// X2 är alltså avgörande först under saneraren; W1 är dess avgörbara
// motsvarighet utan.
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * FilePickers DIALOGVAL — filepicker_gate driver dialogen och räknar
//     onChange och onCommit där.
//   * reglagens DRAG i färgväljarens popup — popupens mus är protected och
//     plattformskod; reglagets egen drag-commit mäts på ett fristående
//     Slider (L1–L2), och popupens reglage är samma klass.
//   * Wayland-vägen där popupens tangenter går direkt till popupen — samma
//     routing, mätt i colorpicker_gate/datepicker_gate.
//   * hur markeringen RITAS — bara att den sätts (TextInputs selBegin/
//     selEnd); ritningen är textinput:s och pixel_gate:s.
//   * varje dispatchväg där en handler kan riva fönstret: RV1–RV3 prövar
//     musens tryck och tangenten (konsumerad och inte); släppet, draget,
//     hjulet, gesten, drag-and-drop, högerklicket och hovern bär samma
//     kontroller men har inget eget fall. Ritningens krokar (`frame`/
//     `overlay`) får inte riva fönster alls — kontraktet vid `close()`.
//   * att fönstrets kontroll efter `mouseDown` (`pressedWidget_ != over`)
//     behövs — utan den läses `over` efter en handler som lossat den, men
//     det syns bara som en tyst läsning av en levande, lossad widget.
//   * use-after-free utan sanerare: i det vanliga bygget syns ett sådant fel
//     bara när det kraschar — fallen mäter därför det AVGÖRBARA (lossad
//     widget, flyttat fält) och kör de rivande vägarna kraschfritt, och
//     ASan-körningen ovan är den som avgör dem.
//   * fokuskön som deque — en prestandafråga utan
//     beteende att mäta; ordningen mäts av O1 och G-fallen.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

// `PropertyGrid::rows()` är en LÄSVÄG. En muterbar
// referens lät rader läggas till, bytas och raderas förbi radgenerationerna
// som slopar en köad sparning — grinden KOMPILERAR INTE om den kommer
// tillbaka (sabotaget är att lägga tillbaka den).
static_assert(std::is_const_v<std::remove_reference_t<
                  decltype(std::declval<glaze::PropertyGrid&>().rows())>>,
              "PropertyGrid::rows() ska vara en läsväg — raderna ändras bara "
              "via setRows/addRow/setRow");

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

// Textfältens redigeringsgenvägar (Ctrl+A, Ctrl+Z …) är plattformens — Cmd på
// macOS, Ctrl annars (`textfield::editShortcut`).
#ifdef __APPLE__
constexpr std::uint32_t kRedigera = gfx::kModCmd;
#else
constexpr std::uint32_t kRedigera = gfx::kModCtrl;
#endif

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
    std::fflush(stdout);   // ett sabotage kan krascha längre fram — raden ska synas
    ++g_fails;
}

// ------------------------------------------------------------ EVENTLOGGEN
// Grindens egna handlers skriver hit — förväntat värde är loggens innehåll mot handens
// förväntan, aldrig mot produktionens räkning.
std::vector<std::string> g_logg;

bool slutarPå(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// `namn`s VÄRDE-events sedan `från` (change, commit, check) — fokus utelämnat.
std::vector<std::string> värden(const std::string& namn, std::size_t från)
{
    std::vector<std::string> ut;
    for (std::size_t i = från; i < g_logg.size(); ++i) {
        const std::string& s = g_logg[i];
        if (s.rfind(namn + ":", 0) == 0 && !slutarPå(s, ":focus") &&
            !slutarPå(s, ":focusLost"))
            ut.push_back(s.substr(namn.size() + 1));
    }
    return ut;
}

// ALLA fokus-events sedan `från`, i ordning ("namn:focus"/"namn:focusLost").
std::vector<std::string> fokus(std::size_t från)
{
    std::vector<std::string> ut;
    for (std::size_t i = från; i < g_logg.size(); ++i)
        if (slutarPå(g_logg[i], ":focus") || slutarPå(g_logg[i], ":focusLost"))
            ut.push_back(g_logg[i]);
    return ut;
}

int antal(const std::string& händelse, std::size_t från = 0)
{
    int n = 0;
    for (std::size_t i = från; i < g_logg.size(); ++i)
        if (g_logg[i] == händelse)
            ++n;
    return n;
}

std::string text(const std::vector<std::string>& v)
{
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? ", " : "") + v[i];
    return s + "]";
}

using Lista = std::vector<std::string>;

// Handlers i den KORTA formen — `Handler<Args>` tar den för varje args-typ,
// också datagridens `CellChangeArgs`.
template <class W>
void kopplaVärde(W& w, const std::string& namn)
{
    w.onChange = [namn] { g_logg.push_back(namn + ":change"); };
    w.onCommit = [namn] { g_logg.push_back(namn + ":commit"); };
}

void kopplaFokus(glaze::Widget& w, const std::string& namn)
{
    w.onFocus     = [namn] { g_logg.push_back(namn + ":focus"); };
    w.onFocusLost = [namn] { g_logg.push_back(namn + ":focusLost"); };
}

// Det inre fältet i en komposit — ett riktigt barn, `children()` är publikt.
glaze::TextInput* fältI(glaze::Widget& w)
{
    for (glaze::Widget* b : w.children())
        if (auto* f = dynamic_cast<glaze::TextInput*>(b))
            return f;
    return nullptr;
}

// Damage-stämpeln är protected — en subklass når den (plumbing_gate). Och
// skrivkroken, så ett fält i ett hjälpfönster kan få användarens text utan
// en fönsterväg på stacken.
struct Fält : glaze::TextInput {
    using glaze::TextInput::textInput;
    using glaze::Widget::damageStamp;
};

// Ett tomt fönster att riva medan dess (lånade) widgets lever.
struct Hjälp : glaze::AppWindow {
    Hjälp() : glaze::AppWindow("hjälp", 300, 120, gfx::Chrome::None) {}
};

// Ett fönster som en handler RIVER mitt i sin egen dispatch
// (grinden anropar dess `handleEvent` direkt), kompositer vars
// damage-stämpel grinden läser, och en widget som postar åt grinden.
struct Rivbar : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    Rivbar() : glaze::AppWindow("rivbar", 320, 140, gfx::Chrome::None) {}
};
struct Tal : glaze::SpinBox {
    using glaze::Widget::damageStamp;
};
struct Datum : glaze::DatePicker {
    using glaze::Widget::damageStamp;
};
struct Postare : glaze::Panel {
    using glaze::Widget::postEvent;
};

gfx::Event händelse(glaze::AppWindow& w, gfx::EventType t, float x = 0.0f,
                    float y = 0.0f)
{
    gfx::Event e;
    e.type     = t;
    e.windowId = w.window().id();
    e.mouseX   = x;
    e.mouseY   = y;
    e.button   = gfx::MouseButton::Left;
    return e;
}
struct Ruta : glaze::TextArea {
    using glaze::Widget::damageStamp;
};

std::string sel(const glaze::TextInput& f)
{
    return std::to_string(f.selBegin()) + ".." + std::to_string(f.selEnd()) +
           " caret " + std::to_string(f.caret());
}
std::string sel(const glaze::TextArea& f)
{
    return std::to_string(f.selBegin()) + ".." + std::to_string(f.selEnd()) +
           " caret " + std::to_string(f.caret());
}

// ================================================================ HEADLESS

// MARKERINGEN — byte i UTF-8, klampat, nedsnappat, bakåt, inga events.
void fallMarkering()
{
    Fält f;
    f.setText("aåb");   // a = byte 0, å = byte 1–2, b = byte 3; längd 4
    int n = 0;
    f.onChange = [&n] { ++n; };
    f.onCommit = [&n] { ++n; };
    const auto stämpel = f.damageStamp();
    f.selectAll();
    check(f.selBegin() == 0 && f.selEnd() == 4 && f.caret() == 4,
          "M1: selectAll — hela texten, careten sist", sel(f));
    check(f.damageStamp() > stämpel, "M1: selectAll invaliderar");
    f.setSelection(1, 3);
    check(f.selBegin() == 1 && f.selEnd() == 3 && f.caret() == 3,
          "M2: setSelection(1, 3) — ankaret på 1, careten på 3 (å är byte 1–2)",
          sel(f));
    f.setSelection(2, 4);
    check(f.selBegin() == 1 && f.selEnd() == 4,
          "M3: ett index MITT I ett tecken snappas nedåt (2 → 1, början av å)",
          sel(f));
    f.setSelection(-5, 99);
    check(f.selBegin() == 0 && f.selEnd() == 4,
          "M4: klampat till [0, längden]", sel(f));
    f.setSelection(3, 1);
    check(f.selBegin() == 1 && f.selEnd() == 3 && f.caret() == 1,
          "M5: begin > end är en markering BAKÅT — careten på 1", sel(f));
    f.setSelection(2, 2);
    check(!f.hasSelection() && f.caret() == 1,
          "M6: begin == end är en ren caret (nedsnappad)", sel(f));
    check(n == 0, "M7: markeringen fyrar varken onChange eller onCommit",
          std::to_string(n));

    Ruta a;
    a.setText("rad1\nrådd");   // å = byte 6–7
    int m = 0;
    a.onChange = [&m] { ++m; };
    a.onCommit = [&m] { ++m; };
    const auto st2 = a.damageStamp();
    a.selectAll();
    check(a.selBegin() == 0 && a.selEnd() == 10 && a.damageStamp() > st2,
          "M8: TextArea::selectAll — hela texten (över raden), invaliderad",
          sel(a));
    a.setSelection(7, 2);
    check(a.selBegin() == 2 && a.selEnd() == 6 && a.caret() == 2,
          "M9: TextArea — 7 (mitt i å) snappas till 6, bakåt", sel(a));
    check(m == 0, "M9: inga events från TextAreas markering");
}

// VIDAREBEFORDRAN — kompositernas markering är fältets.
void fallVidarebefordran()
{
    glaze::SpinBox s;
    s.setDecimals(2);
    s.setValue(12.5);   // "12,50" på svenska, "12.50" på engelska — 5 byte
    glaze::TextInput* sf = fältI(s);
    s.setSelection(1, 3);
    check(sf && sf->selBegin() == 1 && sf->selEnd() == 3,
          "V1: SpinBox::setSelection når fältet", sf ? sel(*sf) : "inget fält");
    s.selectAll();
    check(sf && sf->selBegin() == 0 && sf->selEnd() == sf->text().size(),
          "V1: SpinBox::selectAll når fältet", sf ? sel(*sf) : "inget fält");

    glaze::DatePicker d;
    d.setDate(2026, 10, 11);   // "2026-10-11"
    glaze::TextInput* df = fältI(d);
    d.setSelection(5, 7);
    check(df && df->selBegin() == 5 && df->selEnd() == 7,
          "V2: DatePicker::setSelection når fältet (månaden)",
          df ? sel(*df) : "inget fält");
    d.selectAll();
    check(df && df->selBegin() == 0 && df->selEnd() == 10,
          "V2: DatePicker::selectAll når fältet", df ? sel(*df) : "inget fält");

    glaze::FilePicker p;
    p.setPath("C:/mapp/fil.txt");
    glaze::TextInput* pf = fältI(p);
    p.setSelection(8, 11);
    check(pf && pf->selBegin() == 8 && pf->selEnd() == 11,
          "V3: FilePicker::setSelection når fältet ('fil')",
          pf ? sel(*pf) : "inget fält");
    p.selectAll();
    check(pf && pf->selBegin() == 0 && pf->selEnd() == p.path().size(),
          "V3: FilePicker::selectAll når fältet", pf ? sel(*pf) : "inget fält");
}

// PROGRAMMATISKT — ingen setter fyrar onChange eller onCommit, på någon
// widget i tabellen.
void fallProgrammatiskt()
{
    g_logg.clear();
    glaze::TextInput   ti;  kopplaVärde(ti, "ti");
    glaze::TextArea    ta;  kopplaVärde(ta, "ta");
    glaze::SpinBox     sb;  kopplaVärde(sb, "sb");
    glaze::DatePicker  dp;  kopplaVärde(dp, "dp");
    glaze::ColorPicker cp;  kopplaVärde(cp, "cp");
    glaze::FilePicker  fp;  kopplaVärde(fp, "fp");
    glaze::Slider      sl;  kopplaVärde(sl, "sl");
    glaze::Scrollbar   sc;  kopplaVärde(sc, "sc");
    glaze::Checkbox    cb;  kopplaVärde(cb, "cb");
    glaze::Radio       ra;  kopplaVärde(ra, "ra");
    glaze::ToolButton  tb;  kopplaVärde(tb, "tb");
    glaze::Dropdown    dd;  kopplaVärde(dd, "dd");
    glaze::ListBox     lb;  kopplaVärde(lb, "lb");
    glaze::TreeView    tv;  kopplaVärde(tv, "tv");
    glaze::ListView    lv;  kopplaVärde(lv, "lv");
    glaze::PropertyGrid pg; kopplaVärde(pg, "pg");
    glaze::DataGrid    dg;  kopplaVärde(dg, "dg");

    ti.setText("x");
    ta.setText("y");
    sb.setValue(5);
    dp.setDate(2026, 1, 2);
    cp.setColor("#102030");
    fp.setPath("a.txt");
    sl.setValue(40);
    sc.setValue(30);
    cb.setChecked(true);
    ra.setChecked(true);
    tb.setCheckable(true);
    tb.setChecked(true);
    dd.setItems({"a", "b"});
    dd.setSelectedIndex(1);
    lb.setItems({"a", "b"});
    lb.setShowChecks(true);
    lb.setSelectedIndex(1);
    lb.setChecked(0, true);
    tv.setNodes({{"rot"}, {"två"}});
    tv.setSelectedPath({1});
    lv.addColumn("A", 50);
    lv.addRow({"r"});
    lv.setSelectedIndex(0);
    pg.setRows({{"Bredd", "1"}});
    pg.setRow(0, "Bredd", "2");
    dg.addColumn("A", 50);
    dg.addRow({"c"});
    dg.setCell(0, 0, "d");
    check(g_logg.empty(),
          "H1: programmatiska settrar fyrar ALDRIG onChange eller onCommit — "
          "på någon av de sjutton", text(g_logg));
}

// ============================================================ MED FÖNSTER

// En container som SER en Enter som den fokuserade widgeten tackade nej till
// (`descendantKeyDown`) — beviset att TextInput inte konsumerar den.
struct Lyssnare : glaze::Panel {
    int returer = 0;

protected:
    bool descendantKeyDown(gfx::Key k, std::uint32_t) override
    {
        if (k == gfx::Key::Return)
            ++returer;
        return false;
    }
};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Button       knapp{"knapp"};   // ett fokus "någon annanstans"
    glaze::TextInput    falt2;            // selectAll ur onFocus (G7)
    Lyssnare            fältPanel;
    glaze::TextInput    falt;
    glaze::SpinBox      tal;
    glaze::DatePicker   datum;
    glaze::TextArea     anteckning;
    glaze::FilePicker   sökväg;
    glaze::ColorPicker  farg;
    glaze::Slider       reglage;
    glaze::Scrollbar    list;
    glaze::Checkbox     kryss{"kryss"};
    glaze::Radio        r1{"r1"};
    glaze::Radio        r2{"r2"};
    glaze::ToolButton   verktyg{"V"};
    glaze::Dropdown     val;
    glaze::ListBox      lista;
    glaze::TreeView     träd;
    glaze::ListView     listvy;
    glaze::PropertyGrid grid;
    glaze::DataGrid     tabell;
    glaze::Panel        grupp;
    glaze::Panel        inre;
    glaze::TextInput    g1;
    glaze::TextInput    g2;
    // Handlerfallen — sist i adopt-ordningen, så
    // tabbordningen ovan är orörd.
    glaze::ToolButton   spara{"Spara"};   // tar aldrig fokus
    glaze::TextInput    o1, o2, o3;       // fokusordningen
    glaze::TextInput    b3;               // lånad, lossas med köat
    glaze::Checkbox     rk{"rk"};         // lossas mellan paret
    Fält                m1;               // fönsterpekaren
    Fält                probe;            // fönstrets damage-stämpel
    // Färska kompositer (grid/tabell lossas av R1/R5) och
    // fält vars damage-stämpel grinden läser (omankringen).
    glaze::PropertyGrid grid2;
    glaze::DataGrid     tabell2;
    Fält                z1;
    Ruta                z2;
    // Oförändrade kompositer (E16/E17) och en postare (E14).
    Tal                 tal3;
    Datum               datum3;
    Postare             kö;
    // Den "nästlade pumpen" i o1:s onFocusLost är beväpnad bara en gång.
    bool                pumpaNästlat = false;

    // Datagridens par med ARGS — raden och kolumnen, i BÅDA eventen.
    int dgRad = -1, dgKol = -1;
    int dgChRad = -1, dgChKol = -1;
    // Vad g1 såg i sin onFocus-handler — fokus som det ÄR.
    bool g1SågFokus = false;
    int  rotHändelser = 0;

    Vard() : glaze::AppWindow("commit_gate", 1000, 640, gfx::Chrome::None)
    {
        const auto ställ = [](glaze::Widget& w, const char* namn, float x,
                              float y, float bredd = 0, float höjd = 0) {
            w.name   = namn;
            w.x      = x;
            w.y      = y;
            w.width  = bredd;
            w.height = höjd;
        };
        // ADOPT-ORDNINGEN ÄR TABBORDNINGEN: knapp → falt2 står först, så en
        // Tab från knappen landar i falt2 (G7).
        ställ(knapp, "knapp", 10, 10);
        ställ(falt2, "falt2", 120, 10, 160);
        ställ(fältPanel, "fältPanel", 300, 6, 190, 40);
        ställ(falt, "falt", 5, 4, 170);
        ställ(tal, "tal", 510, 10, 120);
        ställ(datum, "datum", 650, 10, 160);
        ställ(anteckning, "anteckning", 10, 60, 200, 80);
        ställ(sökväg, "sökväg", 230, 60, 240);
        ställ(farg, "farg", 490, 60, 100);
        ställ(reglage, "reglage", 610, 60, 200);
        ställ(list, "list", 10, 160, 16, 120);
        ställ(kryss, "kryss", 40, 160);
        ställ(r1, "r1", 40, 190);
        ställ(r2, "r2", 40, 220);
        ställ(verktyg, "verktyg", 160, 160);
        ställ(val, "val", 220, 160, 120);
        ställ(lista, "lista", 360, 160, 120, 100);
        ställ(träd, "träd", 500, 160, 140, 100);
        ställ(listvy, "listvy", 660, 160, 200, 100);
        ställ(grid, "grid", 10, 300, 300, 150);
        ställ(tabell, "tabell", 330, 300, 300, 150);
        ställ(grupp, "grupp", 650, 300, 300, 150);
        ställ(inre, "inre", 5, 5, 220, 44);
        ställ(g1, "g1", 5, 5, 150);
        ställ(g2, "g2", 5, 70, 150);
        ställ(o1, "o1", 10, 462, 90);
        ställ(o2, "o2", 110, 462, 90);
        ställ(o3, "o3", 210, 462, 90);
        ställ(b3, "b3", 310, 462, 90);
        ställ(m1, "m1", 410, 462, 90);
        ställ(probe, "probe", 510, 462, 90);
        ställ(rk, "rk", 620, 464);
        ställ(spara, "spara", 700, 462);
        ställ(grid2, "grid2", 10, 500, 300, 120);
        ställ(tabell2, "tabell2", 330, 500, 300, 120);
        ställ(z1, "z1", 830, 10, 160);
        ställ(z2, "z2", 870, 160, 120, 100);
        ställ(tal3, "tal3", 660, 560, 120);
        ställ(datum3, "datum3", 800, 560, 160);
        ställ(kö, "kö", 960, 600, 20, 20);
        tal3.setMax(100);
        tal3.setValue(5);
        datum3.setDate(2026, 5, 6);
        grid2.setNameWidth(80);
        grid2.setRows({{"A", "1"}, {"B", "2"}, {"C", "3"}});
        tabell2.addColumn("A", 120);
        tabell2.addRow({"x0"});
        tabell2.addRow({"x1"});
        kopplaVärde(grid2, "grid2");
        tabell2.onChange = [] { g_logg.push_back("tabell2:change"); };
        tabell2.onCommit = [] { g_logg.push_back("tabell2:commit"); };

        tal.setMin(0);
        tal.setMax(100);
        list.setOrientation(glaze::Scrollbar::Orientation::Vertical);
        list.setMax(100);
        list.setPage(10);
        r1.setGroup("g");
        r2.setGroup("g");
        verktyg.setCheckable(true);
        val.setItems({"Ett", "Två", "Tre"});
        val.setSelectedIndex(0);
        lista.setItems({"a", "b", "c"});
        lista.setShowChecks(true);
        träd.setNodes({{"rot"}, {"två"}});
        listvy.addColumn("A", 120);
        listvy.addRow({"x"});
        listvy.addRow({"y"});
        grid.setNameWidth(80);
        grid.setRows({{"Bredd", "10"},
                      {"Höjd", "20"},
                      {"Djup", "30"},
                      {"Typ", "A", {"A", "B", "C"}}});
        tabell.addColumn("A", 120);
        tabell.addRow({"a0"});
        tabell.addRow({"a1"});

        kopplaVärde(falt, "falt");
        kopplaVärde(tal, "tal");
        kopplaVärde(datum, "datum");
        kopplaVärde(anteckning, "anteckning");
        kopplaVärde(sökväg, "sökväg");
        kopplaVärde(farg, "farg");
        kopplaVärde(reglage, "reglage");
        kopplaVärde(list, "list");
        kopplaVärde(kryss, "kryss");
        kopplaVärde(r1, "r1");
        kopplaVärde(r2, "r2");
        kopplaVärde(verktyg, "verktyg");
        kopplaVärde(val, "val");
        kopplaVärde(lista, "lista");
        lista.onCheck = [] { g_logg.push_back("lista:check"); };
        kopplaVärde(träd, "träd");
        kopplaVärde(listvy, "listvy");
        kopplaVärde(grid, "grid");
        tabell.onChange = [this](glaze::Widget*, glaze::CellChangeArgs& e) {
            g_logg.push_back("tabell:change");
            dgChRad = e.row;
            dgChKol = e.column;
        };
        tabell.onCommit = [this](glaze::Widget*, glaze::CellChangeArgs& e) {
            g_logg.push_back("tabell:commit");
            dgRad = e.row;
            dgKol = e.column;
        };
        kopplaVärde(b3, "b3");
        kopplaFokus(o1, "o1");
        kopplaFokus(o2, "o2");
        kopplaFokus(o3, "o3");
        // o1:s onFocusLost gör ett NYTT byte (o2 → o3) och pumpar
        // sedan nästlat, som en modal dialog — en postad stängning släpper den.
        o1.onFocusLost = [this] {
            g_logg.push_back("o1:focusLost");
            if (!pumpaNästlat)
                return;
            pumpaNästlat = false;
            o3.focus();
            Hjälp d;
            d.setOffscreen(true);
            glaze::app().post([&d] { d.close(); });
            d.run(*this);
        };

        kopplaFokus(knapp, "knapp");
        kopplaFokus(falt, "falt");
        kopplaFokus(tal, "tal");
        kopplaFokus(sökväg, "sökväg");
        kopplaFokus(grupp, "grupp");
        kopplaFokus(inre, "inre");
        kopplaFokus(g1, "g1");
        kopplaFokus(g2, "g2");
        g1.onFocus = [this] {
            g_logg.push_back("g1:focus");
            g1SågFokus = g1.focused();
        };
        root().onFocus     = [this] { ++rotHändelser; };
        root().onFocusLost = [this] { ++rotHändelser; };

        root().adopt(knapp);
        root().adopt(falt2);
        root().adopt(fältPanel);
        fältPanel.adopt(falt);
        root().adopt(tal);
        root().adopt(datum);
        root().adopt(anteckning);
        root().adopt(sökväg);
        root().adopt(farg);
        root().adopt(reglage);
        root().adopt(list);
        root().adopt(kryss);
        root().adopt(r1);
        root().adopt(r2);
        root().adopt(verktyg);
        root().adopt(val);
        root().adopt(lista);
        root().adopt(träd);
        root().adopt(listvy);
        root().adopt(grid);
        root().adopt(tabell);
        root().adopt(grupp);
        grupp.adopt(inre);
        inre.adopt(g1);
        grupp.adopt(g2);
        root().adopt(spara);
        root().adopt(o1);
        root().adopt(o2);
        root().adopt(o3);
        root().adopt(b3);
        root().adopt(m1);
        root().adopt(probe);
        root().adopt(rk);
        root().adopt(grid2);
        root().adopt(tabell2);
        root().adopt(z1);
        root().adopt(z2);
        root().adopt(tal3);
        root().adopt(datum3);
        root().adopt(kö);
    }

    // ---- INGÅNGEN — syntetiska händelser genom `handleEvent`.
    void mus(gfx::EventType t, float x, float y)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    gfx::Pointf punkt(glaze::Widget& w, float fx = 0.5f, float fy = 0.5f)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        return gfx::Pointf{r.x + r.w * fx, r.y + r.h * fy};
    }
    void klick(gfx::Pointf p)
    {
        mus(gfx::EventType::MouseButtonDown, p.x, p.y);
        mus(gfx::EventType::MouseButtonUp, p.x, p.y);
    }
    void klick(glaze::Widget& w, float fx = 0.5f, float fy = 0.5f)
    {
        klick(punkt(w, fx, fy));
    }
    void tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void skriv(const char* utf8)
    {
        // Ett tecken per event, som backenden levererar dem.
        for (const char* p = utf8; *p;) {
            const unsigned char c   = static_cast<unsigned char>(*p);
            const int           len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            gfx::Event e;
            e.type     = gfx::EventType::TextInput;
            e.windowId = window().id();
            std::snprintf(e.text, sizeof e.text, "%.*s", len, p);
            handleEvent(e);
            p += len;
        }
    }
    void hjul(glaze::Widget& w, float rader)
    {
        const gfx::Pointf p = punkt(w);
        gfx::Event        e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelY   = rader;
        handleEvent(e);
    }
    void fönsterFokus(bool fått)
    {
        gfx::Event e;
        e.type     = fått ? gfx::EventType::WindowFocusGained
                          : gfx::EventType::WindowFocusLost;
        e.windowId = window().id();
        handleEvent(e);
    }

    // PropertyGridens VÄRDECELL på rad `i` — ur KONTRAKTET: ingen ram, raden
    // `gridRowH` hög, värdekolumnen från `nameWidth` (80) till remsan.
    gfx::Pointf värdecell(int i) { return värdecell(grid, i); }
    gfx::Pointf värdecell(glaze::PropertyGrid& g, int i)
    {
        const gfx::Rectf r = g.resolve(g.behaviorCtx());
        const float      s = window().uiScale();
        const float      h = style().gridRowH;
        return gfx::Pointf{r.x + (80.0f + 40.0f) * s,
                           r.y + (float(i) + 0.5f) * h * s};
    }
    // Den SYNLIGA editorn i gridden, och dess fält om det är fritexten.
    glaze::Widget* gridEditor()
    {
        for (glaze::Widget* b : grid.children())
            if (b->isVisible())
                return b;
        return nullptr;
    }
    glaze::TextInput* gridFält()
    {
        return dynamic_cast<glaze::TextInput*>(gridEditor());
    }
    // Talfältets FÄLT (vänstra delen) och STEGARENS övre halva.
    gfx::Pointf talFält() { return punkt(tal, 0.25f); }
    gfx::Pointf talUpp()
    {
        const gfx::Rectf r = tal.resolve(tal.behaviorCtx());
        const float      w = style().spinStepW * window().uiScale();
        return gfx::Pointf{r.x + r.w - w / 2.0f, r.y + r.h / 4.0f};
    }
};

glaze::AppWindow* popupen(const glaze::AppWindow& ägare)
{
    for (glaze::AppWindow* w : glaze::app().windows())
        if (w != &ägare)
            return w;
    return nullptr;
}

// Förälderkedjan upp till (men inte med) fönstrets rot — GRINDENS förväntade värde för
// fokus inom, räknat ur trädet och inte ur produktionens kod.
Lista kedjan(glaze::Widget* w, const glaze::Widget* rot)
{
    Lista k;
    for (glaze::Widget* p = w; p && p != rot; p = p->parent())
        k.push_back(p->name);
    return k;
}
// Väntade fokus-events för bytet a → b bland de loggade namnen.
Lista väntatFokus(glaze::Widget* a, glaze::Widget* b, const glaze::Widget* rot,
                  const Lista& loggade)
{
    const Lista ka = kedjan(a, rot), kb = kedjan(b, rot);
    const auto  har = [](const Lista& l, const std::string& s) {
        return std::find(l.begin(), l.end(), s) != l.end();
    };
    Lista ut;
    for (const std::string& n : ka)
        if (!har(kb, n) && har(loggade, n))
            ut.push_back(n + ":focusLost");
    for (auto it = kb.rbegin(); it != kb.rend(); ++it)
        if (!har(ka, *it) && har(loggade, *it))
            ut.push_back(*it + ":focus");
    return ut;
}

void fallMedFonster()
{
    glaze::setLanguage("sv");
    g_logg.clear();
    Vard w;
    w.setOffscreen(true);

    const Lista loggadeFokus = {"knapp", "falt",  "tal", "sökväg",
                                "grupp", "inre",  "g1",  "g2"};
    bool                  kordes = false;
    int                   steg   = 0;
    std::size_t           märke  = 0;
    double                talFörr = 0.0;
    glaze::TextInput*     ägd    = nullptr;
    int                   ägdLost = 0;
    // Handlerfallen.
    double                sparaVärde = -1.0;
    int                   p8Rad      = -2;
    std::uint64_t         stämpelEfter = 0;
    std::size_t           tryckIndex = 0;
    gfx::Pointf           tryck{};
    // Hjälpfönstrens LÅNADE widgets lever här, utanför fönstren — de ska
    // överleva att fönstret rivs (deklarerade före fönstren: de dör efter).
    glaze::TextInput      hb1, hb2, hd2;
    Fält                  hc, hd;
    std::unique_ptr<Hjälp> hw, hw2, hw3;
    // En capture som INTE ryms i std::function:s lilla buffert
    // (den ligger då på heapen och dör med handlern om inget håller
    // den), och vad handlerna läste ur den.
    const std::string     lång = std::string(200, 'x') + "slut";
    std::string           läst6, läst7;
    glaze::TextInput*     f7 = nullptr;
    std::uint64_t         stämpelFöre = 0;
    // Fönstret som rivs i sin egen dispatch och dess ÄGDA
    // widgets (de dör med det), modellen en Spara-handler läser, och om
    // undantaget nådde anroparen.
    std::unique_ptr<Rivbar> rv;
    glaze::SpinBox*         rvTal  = nullptr;
    glaze::TextInput*       rvFält = nullptr;
    std::string             modell, sparat;
    bool                    fångat = false;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            if (!w.isOpen()) {
                std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                            "aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            // ---- T1. KLICK in i fältet: fokushändelsen är POSTAD.
            märke = g_logg.size();
            w.klick(w.falt);
            check(w.falt.focused() && antal("falt:focus", märke) == 0,
                  "G0: klicket flyttar fokus DIREKT, men onFocus är POSTAT — "
                  "inte i fokusbytet", text(fokus(märke)));
            w.skriv("abc");
            w.tangent(gfx::Key::Return);
            check(värden("falt", märke) ==
                      Lista{"change", "change", "change", "commit"},
                  "T1: tre tangenter = tre onChange, Enter = ett onCommit "
                  "(efter dem)", text(värden("falt", märke)));
            check(w.fältPanel.returer == 1,
                  "T1: Enter KONSUMERAS INTE — förfadern ser den "
                  "(descendantKeyDown)", std::to_string(w.fältPanel.returer));
            // ---- T2. Enter igen på en ORÖRD text: ingen commit.
            w.tangent(gfx::Key::Return);
            check(antal("falt:commit", märke) == 1 && w.fältPanel.returer == 2,
                  "T2: Enter utan ändring sedan commiten — inget onCommit",
                  text(värden("falt", märke)));
            break;
        }
        case 1: {
            check(antal("falt:focus") == 1 && w.falt.focused(),
                  "T1: …onFocus kom i pump-svansen", text(fokus(0)));
            // ---- T3. FOKUS BORT med en ändrad text: commit POSTAD.
            märke = g_logg.size();
            w.skriv("d");
            w.klick(w.knapp);
            check(värden("falt", märke) == Lista{"change"},
                  "T3: fokus bort — onCommit INTE i fokusbytet",
                  text(värden("falt", märke)));
            break;
        }
        case 2: {
            check(värden("falt", märke) == Lista{"change", "commit"},
                  "T3: …fokusförlustens onCommit kom i pump-svansen",
                  text(värden("falt", märke)));
            // Ordningen i svansen: commiten före fokushändelserna för SAMMA
            // byte (fältets krok postade först).
            const auto it  = std::find(g_logg.begin() + std::ptrdiff_t(märke), g_logg.end(),
                                       std::string("falt:commit"));
            const auto lö  = std::find(g_logg.begin() + std::ptrdiff_t(märke), g_logg.end(),
                                       std::string("falt:focusLost"));
            const auto fö  = std::find(g_logg.begin() + std::ptrdiff_t(märke), g_logg.end(),
                                       std::string("knapp:focus"));
            check(it < lö && lö < fö && fö != g_logg.end(),
                  "T3: ordningen — falt:commit, falt:focusLost, knapp:focus",
                  text(Lista(g_logg.begin() + std::ptrdiff_t(märke), g_logg.end())));
            // ---- T4. setText FLYTTAR baslinjen; ett EKO gör det inte.
            w.klick(w.falt);
            märke = g_logg.size();
            w.falt.setText("ny");
            w.tangent(gfx::Key::Return);
            check(värden("falt", märke).empty(),
                  "T4: programmatisk setText + Enter — ingen commit (baslinjen "
                  "flyttad)", text(värden("falt", märke)));
            w.tangent(gfx::Key::End);
            w.skriv("x");
            w.falt.setText("nyx");   // ekot — samma text
            w.tangent(gfx::Key::Return);
            check(värden("falt", märke) == Lista{"change", "commit"} &&
                      w.falt.text() == "nyx",
                  "T4: ett EKO av användarens text (samma text) flyttar inte "
                  "baslinjen — Enter committar", text(värden("falt", märke)));
            // ---- T6. FÖNSTRET tappar fokus: varken commit eller focusLost.
            w.skriv("y");
            märke = g_logg.size();
            w.fönsterFokus(false);
            w.fönsterFokus(true);
            break;
        }
        case 3: {
            // Det enda som får ha kommit är T4:s klick (knapp → falt), postat
            // i förra steget — fönstrets fokusförlust får inte ha lagt till
            // något.
            check(värden("falt", märke).empty() &&
                      fokus(märke) == Lista{"knapp:focusLost", "falt:focus"},
                  "T6/G5: fönstrets fokusförlust — inget onCommit, inget "
                  "onFocusLost (bara T4:s klick i svansen)",
                  text(Lista(g_logg.begin() + std::ptrdiff_t(märke),
                             g_logg.end())));
            märke = g_logg.size();
            w.klick(w.knapp);
            break;
        }
        case 4: {
            check(värden("falt", märke) == Lista{"commit"},
                  "T6: …och baslinjen stod kvar — fokus bort committar \"nyxy\"",
                  text(värden("falt", märke)));
            // ---- T5. In och ut UTAN ändring: ingen commit.
            w.klick(w.falt);
            märke = g_logg.size();
            w.klick(w.knapp);
            break;
        }
        case 5: {
            check(värden("falt", märke).empty(),
                  "T5: fokus in och ut utan ändring — inget onCommit",
                  text(värden("falt", märke)));
            // ---- A1. TEXTAREA: Enter är en ny rad, commit vid fokus bort.
            märke = g_logg.size();
            w.klick(w.anteckning);
            w.skriv("ab");
            w.tangent(gfx::Key::Return);
            w.skriv("c");
            check(värden("anteckning", märke) ==
                      Lista{"change", "change", "change", "change"} &&
                      w.anteckning.text() == "ab\nc",
                  "A1: TextArea — Enter är en ny rad (onChange), inget "
                  "onCommit", text(värden("anteckning", märke)));
            w.klick(w.knapp);
            break;
        }
        case 6: {
            check(antal("anteckning:commit", märke) == 1,
                  "A1: …fokus bort committar, en gång (postat)",
                  text(värden("anteckning", märke)));
            // ---- S1. TALFÄLTET: att skriva ändrar inget, Enter committar.
            w.tal.setValue(10);
            märke = g_logg.size();
            w.klick(w.talFält());
            w.tangent(gfx::Key::A, kRedigera);
            w.skriv("12");
            check(w.tal.value() == 10.0 && värden("tal", märke).empty(),
                  "S1: att skriva \"12\" ändrar inte värdet (inte LIVE)",
                  std::to_string(w.tal.value()));
            w.tangent(gfx::Key::Return);
            check(w.tal.value() == 12.0 &&
                      värden("tal", märke) == Lista{"change", "commit"},
                  "S1: Enter — värdet 12, onChange och onCommit TILLSAMMANS",
                  text(värden("tal", märke)));
            // ---- S2. Pilen, hjulet och stegaren — tillsammans.
            märke = g_logg.size();
            w.tangent(gfx::Key::Up);
            w.hjul(w.tal, 1.0f);
            w.klick(w.talUpp());
            check(w.tal.value() == 15.0 &&
                      värden("tal", märke) ==
                          Lista{"change", "commit", "change", "commit",
                                "change", "commit"},
                  "S2: pilen, hjulet och stegaren — ett par per steg",
                  text(värden("tal", märke)));
            // ---- S5. Enter utan ändring: inget.
            märke = g_logg.size();
            w.tangent(gfx::Key::Return);
            check(värden("tal", märke).empty(),
                  "S5: Enter på en orörd text — inga events");
            // ---- S3. Fokus bort committar — värdet direkt, eventen postade.
            w.tangent(gfx::Key::A, kRedigera);
            w.skriv("5");
            w.klick(w.knapp);
            check(w.tal.value() == 5.0 && w.tal.text() == "5" &&
                      värden("tal", märke).empty(),
                  "S3: fokus bort — värdet 5 och texten DIREKT, eventen inte i "
                  "fokusbytet", text(värden("tal", märke)));
            break;
        }
        case 7: {
            check(värden("tal", märke) == Lista{"change", "commit"},
                  "S3: …onChange och onCommit kom i pump-svansen",
                  text(värden("tal", märke)));
            // ---- S4. Otolkbar text: värdet står, texten skrivs tillbaka.
            w.klick(w.talFält());
            w.tangent(gfx::Key::A, kRedigera);
            w.skriv("-");
            märke = g_logg.size();
            w.klick(w.knapp);
            check(w.tal.value() == 5.0 && w.tal.text() == "5",
                  "S4: \"-\" + fokus bort — värdet står, texten skrivs tillbaka",
                  w.tal.text());
            break;
        }
        case 8: {
            check(värden("tal", märke).empty(),
                  "S4: …och inga events", text(värden("tal", märke)));
            // ---- D1. DATUMFÄLTET: Enter committar.
            w.datum.setDate(2026, 10, 8);
            märke = g_logg.size();
            w.klick(w.datum, 0.25f);
            w.tangent(gfx::Key::A, kRedigera);
            w.skriv("2026-03-05");
            check(w.datum.dateText() == "2026-10-08" &&
                      värden("datum", märke).empty(),
                  "D1: att skriva ett datum ändrar det inte (inte LIVE)",
                  w.datum.dateText());
            w.tangent(gfx::Key::Return);
            check(w.datum.dateText() == "2026-03-05" &&
                      värden("datum", märke) == Lista{"change", "commit"},
                  "D1: Enter — datumet, onChange och onCommit tillsammans",
                  text(värden("datum", märke)));
            // ---- D2. Fokus bort committar, postat.
            märke = g_logg.size();
            w.tangent(gfx::Key::A, kRedigera);
            w.skriv("2026-04-01");
            w.klick(w.knapp);
            check(w.datum.dateText() == "2026-04-01" &&
                      värden("datum", märke).empty(),
                  "D2: fokus bort — datumet direkt, eventen postade",
                  w.datum.dateText());
            break;
        }
        case 9: {
            check(värden("datum", märke) == Lista{"change", "commit"},
                  "D2: …kom i pump-svansen", text(värden("datum", märke)));
            // ---- D3. KALENDERNS VAL — tillsammans.
            w.datum.focus();
            märke = g_logg.size();
            w.tangent(gfx::Key::F4);
            if (!w.datum.isOpen()) {
                std::printf("  --   kalenderns fall hoppas över: popupen kom "
                            "aldrig upp\n");
                ++g_skips;
                break;
            }
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Return);
            check(w.datum.dateText() == "2026-04-02" &&
                      värden("datum", märke) == Lista{"change", "commit"},
                  "D3: kalenderns val — onChange och onCommit tillsammans",
                  text(värden("datum", märke)));
            break;
        }
        case 10: {
            // ---- C1. FÄRGVÄLJARENS PALETT — tillsammans.
            w.farg.setColor("#3366FF");
            w.farg.focus();
            märke = g_logg.size();
            w.tangent(gfx::Key::Space);
            if (!w.farg.isOpen()) {
                std::printf("  --   färgväljarens fall hoppas över: popupen "
                            "kom aldrig upp\n");
                ++g_skips;
                steg = 15;   // vidare till filväljarfältet
                break;
            }
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Return);
            check(värden("farg", märke) == Lista{"change", "commit"},
                  "C1: palettens val — onChange och onCommit tillsammans",
                  text(värden("farg", märke)));
            break;
        }
        case 11: {
            // ---- C2. REGLAGETS STEG i popupen — tillsammans.
            märke = g_logg.size();
            w.tangent(gfx::Key::Space);
            w.tangent(gfx::Key::Tab);   // paletten → rött
            w.tangent(gfx::Key::Right);
            check(värden("farg", märke) == Lista{"change", "commit"},
                  "C2: ett tangentsteg i ett reglage — tillsammans",
                  text(värden("farg", märke)));
            w.tangent(gfx::Key::Escape);
            break;
        }
        case 12: {
            // ---- C3. HEXFÄLTET: skrivet, och Escape stänger — commit.
            w.tangent(gfx::Key::Space);
            for (int i = 0; i < 4; ++i)
                w.tangent(gfx::Key::Tab);   // rött, grönt, blått, hex
            w.tangent(gfx::Key::A, kRedigera);
            märke = g_logg.size();
            w.skriv("#102030");
            check(värden("farg", märke).empty(),
                  "C3: en skriven hexfärg ändrar ingenting (inte LIVE)",
                  text(värden("farg", märke)));
            w.tangent(gfx::Key::Escape);   // stängningen verkställs i svansen
            break;
        }
        case 13: {
            const gfx::Color c = w.farg.color();
            check(!w.farg.isOpen() && c.r == 0x10 && c.g == 0x20 && c.b == 0x30,
                  "C3: Escape stängde — och stängningen committade färgen");
            break;
        }
        case 14: {
            check(värden("farg", märke) == Lista{"change", "commit"},
                  "C3: …stängningens onChange och onCommit kom postade",
                  text(värden("farg", märke)));
            break;
        }
        case 15: {
            // ---- F1. FILVÄLJARFÄLTET: onChange per tangent, Enter committar.
            glaze::TextInput* fält = fältI(w.sökväg);
            märke = g_logg.size();
            w.klick(w.sökväg, 0.2f);
            w.skriv("a");
            check(värden("sökväg", märke) == Lista{"change"},
                  "F1: en tangent i fältet — onChange, inget onCommit",
                  text(värden("sökväg", märke)));
            w.tangent(gfx::Key::Return);
            check(värden("sökväg", märke) == Lista{"change", "commit"},
                  "F1: Enter committar (fältets eget onCommit, vidare)",
                  text(värden("sökväg", märke)));
            // ---- F2. Fokus från fältet till "…": fältets commit, men
            //          kompositen har fortfarande fokus INOM sig.
            w.skriv("b");
            märke = g_logg.size();
            glaze::Widget* knappen = nullptr;
            for (glaze::Widget* b : w.sökväg.children())
                if (b != fält)
                    knappen = b;
            if (knappen)
                knappen->focus();
            break;
        }
        case 16: {
            check(värden("sökväg", märke) == Lista{"commit"} &&
                      antal("sökväg:focusLost", märke) == 0,
                  "F2: fältet → \"…\": fältets onCommit (postat), men INGET "
                  "onFocusLost på filväljarfältet — fokus står kvar inom det",
                  text(Lista(g_logg.begin() + std::ptrdiff_t(märke), g_logg.end())));
            märke = g_logg.size();
            w.knapp.focus();
            break;
        }
        case 17: {
            check(antal("sökväg:focusLost", märke) == 1 &&
                      värden("sökväg", märke).empty(),
                  "F2: …och när fokus lämnar \"…\" lämnar det kompositen",
                  text(fokus(märke)));
            // ---- L1. REGLAGET: live under draget, EN commit vid släppet.
            w.reglage.setValue(50);
            märke              = g_logg.size();
            const gfx::Pointf a = w.punkt(w.reglage, 0.25f);
            const gfx::Pointf b = w.punkt(w.reglage, 0.75f);
            w.mus(gfx::EventType::MouseButtonDown, a.x, a.y);
            w.mus(gfx::EventType::MouseMotion, b.x, b.y);
            check(värden("reglage", märke) == Lista{"change", "change"},
                  "L1: trycket och draget — onChange LIVE, inget onCommit än",
                  text(värden("reglage", märke)));
            w.mus(gfx::EventType::MouseButtonUp, b.x, b.y);
            check(värden("reglage", märke) ==
                      Lista{"change", "change", "commit"},
                  "L1: släppet — EXAKT ett onCommit, sist",
                  text(värden("reglage", märke)));
            // ---- L2. Ett tryck som inte ändrar värdet committar inte.
            märke = g_logg.size();
            w.klick(b);
            check(värden("reglage", märke).empty(),
                  "L2: tryck och släpp på samma värde — inget event",
                  text(värden("reglage", märke)));
            // ---- L3/L4. Tangent- och hjulsteg — tillsammans.
            w.tangent(gfx::Key::Right);
            w.hjul(w.reglage, 1.0f);
            check(värden("reglage", märke) ==
                      Lista{"change", "commit", "change", "commit"},
                  "L3/L4: tangent- och hjulsteg — ett par per steg",
                  text(värden("reglage", märke)));
            // ---- B1. RULLNINGSLISTEN: rännklicket committar vid släppet.
            märke = g_logg.size();
            const gfx::Pointf ränna = w.punkt(w.list, 0.5f, 0.9f);
            w.mus(gfx::EventType::MouseButtonDown, ränna.x, ränna.y);
            check(värden("list", märke) == Lista{"change"} &&
                      w.list.value() == 10.0f,
                  "B1: rännklicket — en sida, onChange (inget onCommit än)",
                  text(värden("list", märke)));
            w.mus(gfx::EventType::MouseButtonUp, ränna.x, ränna.y);
            w.hjul(w.list, -1.0f);
            check(värden("list", märke) ==
                      Lista{"change", "commit", "change", "commit"},
                  "B1/B3: släppet committar; hjulsteget är ett par",
                  text(värden("list", märke)));
            // ---- TILLSAMMANS-widgetarna: onChange (onCheck) först.
            märke = g_logg.size();
            w.klick(w.kryss);
            w.tangent(gfx::Key::Space);   // kryssrutan har fokus efter klicket
            check(värden("kryss", märke) ==
                      Lista{"change", "commit", "change", "commit"},
                  "K1: kryssrutan — klick och Space, ett par var",
                  text(värden("kryss", märke)));
            w.klick(w.r2);
            w.klick(w.r2);
            check(värden("r2", märke) == Lista{"change", "commit"} &&
                      värden("r1", märke).empty(),
                  "K2: radion — vald ger ett par, vald igen inget, syskonet "
                  "inget", text(värden("r2", märke)));
            w.klick(w.verktyg);
            w.verktyg.setCheckable(false);
            w.klick(w.verktyg);
            check(värden("verktyg", märke) == Lista{"change", "commit"},
                  "K3: verktygsknappen — växlingen ett par, en vanlig knapp "
                  "inget", text(värden("verktyg", märke)));
            w.val.focus();
            w.tangent(gfx::Key::Down);
            check(värden("val", märke) == Lista{"change", "commit"},
                  "K4: dropdownen — Ned på en stängd byter val, ett par",
                  text(värden("val", märke)));
            w.lista.focus();
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Space);
            check(värden("lista", märke) ==
                      Lista{"change", "commit", "check", "commit"},
                  "K5: listboxen — valet ett par, krysset (onCheck) ett par",
                  text(värden("lista", märke)));
            w.träd.focus();
            w.tangent(gfx::Key::Down);
            check(värden("träd", märke) == Lista{"change", "commit"},
                  "K6: trädet — valet ett par", text(värden("träd", märke)));
            w.listvy.focus();
            w.tangent(gfx::Key::Down);
            check(värden("listvy", märke) == Lista{"change", "commit"},
                  "K7: listvyn — valet ett par", text(värden("listvy", märke)));
            break;
        }
        case 18: {
            // ---- P1. "12" + Enter.
            märke = g_logg.size();
            w.klick(w.värdecell(0));
            glaze::TextInput* f = w.gridFält();
            check(f && w.grid.activeRow() == 0 && f->selBegin() == 0 &&
                      f->selEnd() == f->text().size() && f->text() == "10",
                  "P1: editorn öppnas med HELA värdet markerat",
                  f ? sel(*f) : "ingen editor");
            w.skriv("12");
            check(f && f->text() == "12" && w.grid.rows()[0].value == "10" &&
                      värden("grid", märke).empty(),
                  "P1: \"12\" ersätter markeringen — värdet orört, inga events "
                  "(inte LIVE)", f ? f->text() : "");
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[0].value == "12" && w.grid.changedRow() == 0 &&
                      värden("grid", märke) == Lista{"change", "commit"} &&
                      w.grid.activeRow() == -1,
                  "P1: Enter — värdet \"12\", changedRow 0, EXAKT ett onChange "
                  "och ett onCommit, editorn stängd",
                  text(värden("grid", märke)));
            // ---- P6. Enter utan ändring: inga events.
            märke = g_logg.size();
            w.klick(w.värdecell(0));
            w.tangent(gfx::Key::Return);
            check(värden("grid", märke).empty(),
                  "P6: Enter på ett orört värde — inga events");
            // ---- P2. "34" + Escape: inga events, värdet orört.
            w.klick(w.värdecell(0));
            w.skriv("34");
            w.tangent(gfx::Key::Escape);
            check(w.grid.rows()[0].value == "12" && värden("grid", märke).empty() &&
                      w.grid.activeRow() == -1,
                  "P2: \"34\" + Escape — editorn stängd, värdet orört, inga "
                  "events", w.grid.rows()[0].value);
            // ---- P3. "56" + klick på en ANNAN rad: sparas först.
            w.klick(w.värdecell(0));
            w.skriv("56");
            w.klick(w.värdecell(1));
            check(w.grid.rows()[0].value == "56" &&
                      värden("grid", märke) == Lista{"change", "commit"} &&
                      w.grid.activeRow() == 1 && w.gridFält() &&
                      w.gridFält()->text() == "20",
                  "P3: klick på rad 1 — rad 0 sparad (ett par), editorn öppen "
                  "på rad 1", text(värden("grid", märke)));
            w.tangent(gfx::Key::Escape);
            // ---- P4. OMRÄKNINGEN: en onCommit som anropar setRows river
            //          inte nästa redigering.
            w.grid.onCommit = [&w] {
                g_logg.push_back("grid:commit");
                auto rader = w.grid.rows();
                rader[2].value = "omräknad";
                w.grid.setRows(rader);
            };
            märke = g_logg.size();
            w.klick(w.värdecell(1));
            w.skriv("7");
            check(w.grid.activeRow() == 1 && w.gridFält() &&
                      w.gridFält()->text() == "7",
                  "P4: editorn står kvar efter FÖRSTA tecknet");
            w.skriv("8");
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[1].value == "78" &&
                      w.grid.rows()[2].value == "omräknad" &&
                      värden("grid", märke) == Lista{"change", "commit"},
                  "P4: Enter — värdet \"78\", omräkningen gjord, ett par",
                  text(värden("grid", märke)));
            märke = g_logg.size();
            w.klick(w.värdecell(0));
            w.skriv("9");
            w.klick(w.värdecell(1));
            check(w.grid.rows()[0].value == "9" &&
                      värden("grid", märke) == Lista{"change", "commit"} &&
                      w.grid.activeRow() == 1 && w.gridFält() != nullptr,
                  "P4: \"9\" + klick på rad 1 med omräkningen — rad 0 sparad "
                  "och NÄSTA editor öppen (handlern körde före öppningen)",
                  text(värden("grid", märke)));
            break;   // nästa editor prövas EFTER pump-svansen
        }
        case 19: {
            // En commit som POSTATS i stället för att fyras direkt hade kört
            // handlerns setRows nu — och stängt den nyss öppnade editorn.
            check(w.grid.activeRow() == 1 && w.gridFält() != nullptr,
                  "P4: …editorn på rad 1 står kvar också efter pump-svansen",
                  std::to_string(w.grid.activeRow()));
            w.skriv("1");
            check(w.grid.activeRow() == 1 && w.gridFält() &&
                      w.gridFält()->text() == "1",
                  "P4: …och den tar emot tecken");
            w.tangent(gfx::Key::Escape);
            // ---- P5. Fokus bort: värdet direkt, eventen postade.
            märke = g_logg.size();
            w.klick(w.värdecell(0));
            w.skriv("x");
            w.klick(w.knapp);
            check(w.grid.rows()[0].value == "x" && värden("grid", märke).empty(),
                  "P5: fokus bort — värdet skrivet DIREKT, eventen inte i "
                  "fokusbytet", text(värden("grid", märke)));
            break;
        }
        case 20: {
            check(värden("grid", märke) == Lista{"change", "commit"},
                  "P5: …onChange och onCommit kom i pump-svansen",
                  text(värden("grid", märke)));
            // ---- P7. VALLISTAN: ett val — tillsammans.
            märke = g_logg.size();
            w.klick(w.värdecell(3));
            auto* dd = dynamic_cast<glaze::Dropdown*>(w.gridEditor());
            if (!dd || !dd->isOpen()) {
                std::printf("  --   vallistans fall hoppas över: listan kom "
                            "aldrig upp\n");
                ++g_skips;
            } else {
                w.tangent(gfx::Key::Down);
                w.tangent(gfx::Key::Return);
                check(w.grid.rows()[3].value == "B" &&
                          värden("grid", märke) == Lista{"change", "commit"},
                      "P7: vallistans val — onChange och onCommit tillsammans",
                      text(värden("grid", märke)));
            }
            // ---- DG. DATAGRIDDEN: onCommit med onChanges rad och kolumn.
            märke = g_logg.size();
            w.tabell.setCurrentCell(1, 0);
            w.tabell.focus();
            w.tangent(gfx::Key::F2);
            w.skriv("x");
            w.tangent(gfx::Key::Return);
            check(värden("tabell", märke) == Lista{"change", "commit"} &&
                      w.dgChRad == 1 && w.dgChKol == 0 && w.dgRad == 1 &&
                      w.dgKol == 0,
                  "DG1: datagridens sparning — onChange OCH onCommit bär båda "
                  "(rad 1, kolumn 0)",
                  text(värden("tabell", märke)) + " change(" +
                      std::to_string(w.dgChRad) + "," +
                      std::to_string(w.dgChKol) + ") commit(" +
                      std::to_string(w.dgRad) + "," + std::to_string(w.dgKol) +
                      ")");
            märke = g_logg.size();
            w.tabell.setCurrentCell(0, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("y");
            w.klick(w.knapp);
            check(värden("tabell", märke).empty(),
                  "DG2: fokus bort — eventen inte i fokusbytet");
            break;
        }
        case 21: {
            check(värden("tabell", märke) == Lista{"change", "commit"} &&
                      w.dgChRad == 0 && w.dgChKol == 0 && w.dgRad == 0 &&
                      w.dgKol == 0,
                  "DG2: …de kom postade, tillsammans och med samma (rad 0, "
                  "kolumn 0)", text(värden("tabell", märke)));
            // ---- G1. FOKUS INOM vid KLICK: knapp → g1.
            märke                = g_logg.size();
            glaze::Widget* förut = &w.knapp;
            w.klick(w.g1);
            check(fokus(märke).empty(), "G1: inga fokushändelser i bytet");
            märke = g_logg.size();
            // Förväntat värde räknas här, medan trädet är som det var vid bytet.
            const Lista väntat = väntatFokus(förut, &w.g1, &w.root(),
                                             loggadeFokus);
            glaze::app().post([väntat, &märke] {
                check(fokus(märke) == väntat &&
                          väntat == Lista{"knapp:focusLost", "grupp:focus",
                                          "inre:focus", "g1:focus"},
                      "G1: klick knapp → g1 — lost innerst först, focus "
                      "YTTERST först", text(fokus(märke)) + " mot " +
                                           text(väntat));
            });
            break;
        }
        case 22: {
            check(w.g1SågFokus,
                  "G1: g1:s handler såg fokus som det ÄR (focused() sant)");
            // ---- G2. g1 → g2 med TAB: den gemensamma förfadern tiger.
            märke = g_logg.size();
            w.tangent(gfx::Key::Tab);
            check(w.g2.focused(), "G2: Tab från g1 landar i g2");
            break;
        }
        case 23: {
            check(fokus(märke) == Lista{"g1:focusLost", "inre:focusLost",
                                        "g2:focus"},
                  "G2: Tab g1 → g2 — g1 och inre tappar (innerst först), g2 "
                  "får; grupp (gemensam) TIGER", text(fokus(märke)));
            märke = g_logg.size();
            w.klick(w.knapp);
            break;
        }
        case 24: {
            check(fokus(märke) == Lista{"g2:focusLost", "grupp:focusLost",
                                        "knapp:focus"},
                  "G3: klick g2 → knapp — g2 och grupp tappar, knapp får",
                  text(fokus(märke)));
            // ---- G4. KOMPOSITEN: talfältet får sina egna events.
            märke = g_logg.size();
            w.tal.focus();   // fokusombudet: fältet
            break;
        }
        case 25: {
            check(fokus(märke) == Lista{"knapp:focusLost", "tal:focus"},
                  "G4: knapp → talfältet — kompositen får onFocus (fokus inom "
                  "dess fält)", text(fokus(märke)));
            märke = g_logg.size();
            talFörr = w.tal.value();
            w.klick(w.talUpp());   // fältet har redan fokus
            break;
        }
        case 26: {
            check(fokus(märke).empty() && w.tal.value() == talFörr + 1.0,
                  "G4: ett klick på stegaren med fokus i fältet — inga "
                  "fokushändelser", text(fokus(märke)));
            // ---- G6. EN WIDGET SOM RIVS innan svansen får ingenting.
            ägd = w.grupp.add<glaze::TextInput>();
            ägd->x           = 5;
            ägd->y           = 110;
            ägd->width       = 100;
            ägd->onFocusLost = [&ägdLost] { ++ägdLost; };
            ägd->focus();
            break;
        }
        case 27: {
            märke = g_logg.size();
            w.grupp.unadopt(*ägd);   // ÄGD — förstörs här
            ägd = nullptr;
            break;
        }
        case 28: {
            check(ägdLost == 0 && fokus(märke) == Lista{"grupp:focusLost"},
                  "G6: unadopt av den fokuserade — den rivna widgeten får "
                  "INGET (livstecknet), förfadern får onFocusLost",
                  std::to_string(ägdLost) + " " + text(fokus(märke)));
            check(w.rotHändelser == 0,
                  "G7: fönstrets rot fyrar aldrig", std::to_string(w.rotHändelser));
            // ---- G8. selectAll ur onFocus vid KLICK.
            w.falt2.setText("hej du");
            w.falt2.onFocus = [&w] { w.falt2.selectAll(); };
            w.klick(w.falt2, 0.1f);   // trycket lägger careten nära början
            break;
        }
        case 29: {
            check(w.falt2.selBegin() == 0 && w.falt2.selEnd() == 6,
                  "G8: onFocus = selectAll vid KLICK — hela texten (det postade "
                  "eventet körde efter klickets caret)", sel(w.falt2));
            w.falt2.setSelection(2, 2);
            w.knapp.focus();
            break;
        }
        case 30: {
            w.tangent(gfx::Key::Tab);   // knapp → falt2 (adopt-ordningen)
            check(w.falt2.focused() && !w.falt2.hasSelection(),
                  "G8: Tab till falt2 — markeringen kommer först med svansen");
            break;
        }
        case 31: {
            check(w.falt2.selBegin() == 0 && w.falt2.selEnd() == 6,
                  "G8: onFocus = selectAll vid TAB — hela texten", sel(w.falt2));
            // ---- G9. Samma i en komposit: talfältets onFocus når fältet.
            w.tal.onFocus = [&w] { w.tal.selectAll(); };
            w.tal.setValue(42);   // två siffror — max är 100
            w.knapp.focus();
            break;
        }
        case 32: {
            w.klick(w.talFält());
            break;
        }
        case 33: {
            glaze::TextInput* f = fältI(w.tal);
            check(f && f->focused() && f->selBegin() == 0 &&
                      f->selEnd() == 2 && f->text() == "42",
                  "G9: tal.onFocus = selectAll — klick i talfältet markerar "
                  "hela fältet", f ? sel(*f) : "inget fält");

            // ---- E1. commitEdits ur en VERKTYGSKNAPP: den tar
            // aldrig fokus, så talfältets skrivna tal committas bara om
            // handlern ber om det.
            märke = g_logg.size();
            w.tal.selectAll();
            w.skriv("77");
            w.spara.onClick = [&w, &sparaVärde] {
                w.commitEdits();
                sparaVärde = w.tal.value();
                g_logg.push_back("spara:efter");
            };
            check(w.tal.value() == 42.0 && värden("tal", märke).empty(),
                  "E1: skrivet men inte committat — value() är det förra",
                  std::to_string(w.tal.value()));
            w.klick(w.spara);
            check(f && f->focused(),
                  "E1: verktygsknappen tog inte fokus — fältet har det kvar");
            check(värden("tal", märke) == Lista{"change", "commit"} &&
                      sparaVärde == 77.0 && antal("spara:efter", märke) == 1,
                  "E1: commitEdits() i onClick — talfältets onChange och "
                  "onCommit har fyrats och value() är 77 när handlern läser det",
                  text(värden("tal", märke)) + " värde " +
                      std::to_string(sparaVärde));
            // E2: egenskapslistans öppna editor, från koden.
            märke = g_logg.size();
            w.klick(w.värdecell(0));
            w.skriv("13");
            w.commitEdits();
            check(w.grid.rows()[0].value == "13" && w.grid.activeRow() < 0 &&
                      värden("grid", märke) == Lista{"change", "commit"},
                  "E2: commitEdits() sparar och stänger egenskapslistans "
                  "editor, som Enter", text(värden("grid", märke)));
            // E3–E7: varje övrig override, genom samma kedja.
            w.falt.focus();
            w.skriv("w");
            märke = g_logg.size();
            w.commitEdits();
            check(värden("falt", märke) == Lista{"commit"},
                  "E3: commitEdits() — textfältets onCommit",
                  text(värden("falt", märke)));
            if (glaze::TextInput* sf = fältI(w.sökväg)) {
                sf->focus();
                w.skriv("x");
                märke = g_logg.size();
                w.commitEdits();
                check(värden("sökväg", märke) == Lista{"commit"},
                      "E4: commitEdits() — filväljarfältets onCommit (fältets "
                      "egen commit, vidare)", text(värden("sökväg", märke)));
            }
            w.datum.focus();
            w.datum.selectAll();
            w.skriv("2026-03-04");
            märke = g_logg.size();
            w.commitEdits();
            check(värden("datum", märke) == Lista{"change", "commit"} &&
                      w.datum.dateText() == "2026-03-04",
                  "E5: commitEdits() — datumväljarens skrivna datum",
                  text(värden("datum", märke)) + " " + w.datum.dateText());
            w.anteckning.focus();
            w.skriv("q");
            märke = g_logg.size();
            w.commitEdits();
            check(värden("anteckning", märke) == Lista{"commit"},
                  "E6: commitEdits() — det flerradiga fältets onCommit",
                  text(värden("anteckning", märke)));
            w.tabell.setCurrentCell(1, 0);
            w.tabell.focus();
            w.tangent(gfx::Key::F2);
            w.skriv("k");
            märke = g_logg.size();
            w.commitEdits();
            check(värden("tabell", märke) == Lista{"change", "commit"} &&
                      !w.tabell.isEditing(),
                  "E7: commitEdits() — datagridens öppna cell sparas",
                  text(värden("tabell", märke)));
            break;
        }
        case 34: {
            // ---- P8. En onChange som BYGGER OM raderna — onCommit
            // läser ändå den sparade radens index.
            märke = g_logg.size();
            w.grid.onChange = [&w] {
                g_logg.push_back("grid:change");
                std::vector<glaze::PropertyGrid::Row> nya = w.grid.rows();
                w.grid.setRows(std::move(nya));   // samma ordning, nya rader
            };
            w.grid.onCommit = [&w, &p8Rad] {
                g_logg.push_back("grid:commit");
                p8Rad = w.grid.changedRow();
            };
            w.klick(w.värdecell(1));
            w.skriv("21");
            w.tangent(gfx::Key::Return);
            check(värden("grid", märke) == Lista{"change", "commit"} &&
                      p8Rad == 1 && w.grid.rows()[1].value == "21",
                  "P8: onChange byggde om raderna (setRows) — changedRow() i "
                  "onCommit är ändå den sparade raden",
                  text(värden("grid", märke)) + " changedRow " +
                      std::to_string(p8Rad));
            // ---- P9. En KÖAD sparning av rad 2, och raden
            // ersätts innan svansen.
            kopplaVärde(w.grid, "grid");
            märke = g_logg.size();
            w.klick(w.värdecell(2));
            w.skriv("55");
            w.klick(w.knapp);   // fokus bort: värdet skrivet, eventen köade
            w.grid.setRow(2, "Djup", "99");
            break;
        }
        case 35: {
            check(värden("grid", märke).empty() &&
                      w.grid.rows()[2].value == "99",
                  "P9: setRow på raden med en köad sparning — eventen slopas "
                  "(de beskrev den ersatta raden)", text(värden("grid", märke)));
            // P10: en köad sparning av rad 1 och setRow på EN ANNAN rad.
            märke = g_logg.size();
            w.klick(w.värdecell(1));
            w.skriv("66");
            w.klick(w.knapp);
            w.grid.setRow(0, "Bredd", "11");
            break;
        }
        case 36: {
            check(värden("grid", märke) == Lista{"change", "commit"} &&
                      w.grid.changedRow() == 1 &&
                      w.grid.rows()[1].value == "66",
                  "P10: setRow på en ANNAN rad slopar inte den köade sparningen",
                  text(värden("grid", märke)));
            // ---- R1. EGENSKAPSLISTAN: klicket på en annan rad
            // sparar, och sparningens onChange LOSSAR gridden — då fyras inte
            // onCommit, och klicket öppnar ingen ny editor.
            w.klick(w.värdecell(0));
            w.skriv("77");
            märke = g_logg.size();
            w.grid.onChange = [&w] {
                g_logg.push_back("grid:change");
                w.root().unadopt(w.grid);   // LÅNAD — lever, men lossad
            };
            w.klick(w.värdecell(1));
            check(värden("grid", märke) == Lista{"change"} &&
                      w.grid.parent() == nullptr && w.grid.activeRow() < 0,
                  "R1: egenskapslistans onChange lossade gridden — inget "
                  "onCommit, och klicket öppnade ingen editor efteråt",
                  text(värden("grid", märke)) + " activeRow " +
                      std::to_string(w.grid.activeRow()));

            // R2: KRYSSRUTAN — onChange lossar den: varken onCommit eller
            // onClick.
            märke = g_logg.size();
            w.rk.onChange = [&w] {
                g_logg.push_back("rk:change");
                w.root().unadopt(w.rk);
            };
            w.rk.onCommit = [] { g_logg.push_back("rk:commit"); };
            w.rk.onClick  = [] { g_logg.push_back("rk:click"); };
            w.klick(w.rk);
            check(värden("rk", märke) == Lista{"change"} &&
                      antal("rk:click", märke) == 0 && w.rk.parent() == nullptr,
                  "R2: kryssrutans onChange lossade den — varken onCommit "
                  "eller onClick efteråt", text(värden("rk", märke)));

            // R3: en ÄGD kryssruta som RIVS i sin onChange — ingen krasch,
            // inget onCommit.
            märke   = g_logg.size();
            auto* k = w.root().add<glaze::Checkbox>("ägd");
            k->name = "ägdK";
            k->x    = 760;
            k->y    = 464;
            k->onChange = [&w, k] {
                g_logg.push_back("ägdK:change");
                w.root().unadopt(*k);   // ÄGD — förstörs här
            };
            k->onCommit = [] { g_logg.push_back("ägdK:commit"); };
            w.klick(*k);
            check(antal("ägdK:change", märke) == 1 &&
                      antal("ägdK:commit", märke) == 0,
                  "R3: en ägd kryssruta rivs i sin onChange — inget onCommit "
                  "på den döda", text(värden("ägdK", märke)));

            // R4: TALFÄLTET — stegarens onChange lossar det.
            märke = g_logg.size();
            w.tal.onChange = [&w] {
                g_logg.push_back("tal:change");
                w.root().unadopt(w.tal);
            };
            w.klick(w.talUpp());
            check(värden("tal", märke) == Lista{"change"} &&
                      w.tal.parent() == nullptr,
                  "R4: talfältets onChange lossade det — inget onCommit",
                  text(värden("tal", märke)));

            // R5: DATAGRIDDEN — Enter sparar, onChange lossar den: inget
            // onCommit, och editorn flyttar inte den aktuella cellen efteråt.
            märke = g_logg.size();
            w.tabell.setCurrentCell(0, 0);
            w.tabell.focus();
            w.tangent(gfx::Key::F2);
            w.skriv("z");
            w.tabell.onChange = [&w] {
                g_logg.push_back("tabell:change");
                w.root().unadopt(w.tabell);
            };
            w.tangent(gfx::Key::Return);
            check(värden("tabell", märke) == Lista{"change"} &&
                      w.tabell.parent() == nullptr &&
                      w.tabell.currentRow() == 0,
                  "R5: datagridens onChange lossade den — inget onCommit, och "
                  "Enter flyttade inte den aktuella cellen efteråt",
                  text(värden("tabell", märke)) + " rad " +
                      std::to_string(w.tabell.currentRow()));

            // ---- N1. En KÖAD commit och en widget som lossas och
            // sätts in IGEN i samma fönster innan svansen.
            märke = g_logg.size();
            w.b3.focus();
            w.skriv("q");
            w.klick(w.knapp);   // fokus bort: commit köad
            w.root().unadopt(w.b3);
            w.root().adopt(w.b3);
            break;
        }
        case 37: {
            check(antal("b3:commit", märke) == 0 && w.b3.text() == "q",
                  "N1: lossad och insatt igen med en köad commit — eventet "
                  "slopas (det gällde den gamla kontexten)",
                  text(värden("b3", märke)));
            // ---- G10. LÅNAD widget som lossas med köade FOKUShändelser.
            kopplaFokus(w.b3, "b3");
            w.b3.focus();
            märke = g_logg.size();
            w.knapp.focus();          // b3 → knapp, i samma varv
            w.root().unadopt(w.b3);   // innan svansen
            break;
        }
        case 38: {
            check(antal("b3:focus", märke) == 0 &&
                      antal("b3:focusLost", märke) == 0 &&
                      antal("knapp:focus", märke) == 1,
                  "G10: den lossade får inga köade fokushändelser — knappen, "
                  "som sitter kvar, får sin", text(fokus(märke)));
            // ---- O1. ORDNINGEN genom en nästlad pump.
            w.o1.focus();
            break;
        }
        case 39: {
            märke          = g_logg.size();
            w.pumpaNästlat = true;
            w.o2.focus();   // o1 → o2; o1:s onFocusLost byter o2 → o3 och pumpar
            break;
        }
        case 40: {
            check(fokus(märke) == Lista{"o1:focusLost", "o2:focus",
                                        "o2:focusLost", "o3:focus"} &&
                      w.o3.focused(),
                  "O1: ett byte UNDER en nästlad pump — händelserna kommer i "
                  "bytenas ordning (o2 får onFocus före sin onFocusLost)",
                  text(fokus(märke)));
            // ---- Z1. DARR efter fokusklicket. En text som fyller
            // fältet, så trycket i mitten landar mitt i texten (index > 0).
            w.falt2.setText("abcdefghijklmnopqrstuvwxyz");
            w.knapp.focus();
            tryck = w.punkt(w.falt2, 0.5f);
            w.mus(gfx::EventType::MouseButtonDown, tryck.x, tryck.y);
            tryckIndex = w.falt2.caret();   // före svansen — trycket
            break;
        }
        case 41: {
            const std::size_t n = w.falt2.text().size();
            check(w.falt2.selBegin() == 0 && w.falt2.selEnd() == n,
                  "Z1: onFocus = selectAll kom efter trycket", sel(w.falt2));
            w.mus(gfx::EventType::MouseMotion, tryck.x + 1.0f, tryck.y);
            w.mus(gfx::EventType::MouseButtonUp, tryck.x + 1.0f, tryck.y);
            check(w.falt2.selBegin() == 0 && w.falt2.selEnd() == n,
                  "Z1: en rörelse INOM dragtröskeln efter fokusklicket — "
                  "markeringen står kvar", sel(w.falt2));
            // Z2: ett riktigt drag ankras vid trycket.
            w.knapp.focus();
            w.mus(gfx::EventType::MouseButtonDown, tryck.x, tryck.y);
            tryckIndex = w.falt2.caret();
            break;
        }
        case 42: {
            const float bort = (w.style().dragThreshold + 40.0f) *
                               w.window().uiScale();
            w.mus(gfx::EventType::MouseMotion, tryck.x + bort, tryck.y);
            check(tryckIndex > 0 && w.falt2.selBegin() == tryckIndex &&
                      w.falt2.selEnd() > tryckIndex,
                  "Z2: ett drag BORTOM tröskeln markerar från trycket (inte "
                  "från selectAll:s början)",
                  sel(w.falt2) + " tryck " + std::to_string(tryckIndex));
            w.mus(gfx::EventType::MouseButtonUp, tryck.x + bort, tryck.y);
            // Z3: samma i det flerradiga fältet.
            w.anteckning.setText("rad ett\nrad två");
            w.anteckning.onFocus = [&w] { w.anteckning.selectAll(); };
            w.knapp.focus();
            tryck = w.punkt(w.anteckning, 0.3f, 0.2f);
            w.mus(gfx::EventType::MouseButtonDown, tryck.x, tryck.y);
            break;
        }
        case 43: {
            w.mus(gfx::EventType::MouseMotion, tryck.x + 1.0f, tryck.y + 1.0f);
            w.mus(gfx::EventType::MouseButtonUp, tryck.x + 1.0f, tryck.y + 1.0f);
            check(w.anteckning.selBegin() == 0 &&
                      w.anteckning.selEnd() == w.anteckning.text().size(),
                  "Z3: TextArea — en rörelse inom tröskeln efter fokusklicket "
                  "rör inte selectAll", sel(w.anteckning));

            // ---- X1. Ett fönster som RIVS av en handler i
            // fokuskön — resten av kön levereras inte till dess lånade.
            hw = std::make_unique<Hjälp>();
            hb1.name = "hb1";
            hb2.name = "hb2";
            hw->root().adopt(hb1);
            hw->root().adopt(hb2);
            hw->setOffscreen(true);
            hw->show(w);
            hb1.focus();
            break;
        }
        case 44: {
            märke           = g_logg.size();
            hb1.onFocusLost = [&hw] {
                g_logg.push_back("hb1:focusLost");
                hw.reset();   // fönstret dör mitt i leveransen
            };
            hb2.onFocus = [] { g_logg.push_back("hb2:focus"); };
            hb2.focus();   // [hb1:focusLost, hb2:focus] i kön
            break;
        }
        case 45: {
            check(!hw && antal("hb1:focusLost", märke) == 1 &&
                      antal("hb2:focus", märke) == 0,
                  "X1: en handler i fokuskön rev fönstret — nästa händelse "
                  "(till en lånad widget som lever) levereras inte",
                  text(fokus(märke)));
            // X2: commitFromUser och ett fönster som rivs i onCommit.
            hw2 = std::make_unique<Hjälp>();
            hw2->root().adopt(hc);
            hw2->setOffscreen(true);
            hw2->show(w);
            hc.focus();
            hc.textInput("x");
            märke       = g_logg.size();
            hc.onCommit = [&hw2] {
                g_logg.push_back("hc:commit");
                hw2.reset();
            };
            hc.commitEdit();   // utan fönstervägen på stacken
            check(!hw2 && antal("hc:commit", märke) == 1,
                  "X2: onCommit rev fönstret — commitFromUser rör det inte "
                  "efteråt (ingen krasch)");
            // X3: en KÖAD commit och ett fönster som rivs.
            hw3 = std::make_unique<Hjälp>();
            hw3->root().adopt(hd);
            hw3->root().adopt(hd2);
            hw3->setOffscreen(true);
            hw3->show(w);
            hd.onCommit = [] { g_logg.push_back("hd:commit"); };
            hd.focus();
            hd.textInput("y");
            hd2.focus();   // fokusförlust: commit KÖAD
            hw3.reset();
            märke = g_logg.size();
            break;
        }
        case 46: {
            check(antal("hd:commit", märke) == 0,
                  "X3: fönstret dog med en köad commit — den levereras inte "
                  "till den lånade widgeten");
            // ---- W1. commitFromUser och ett fält som
            // FLYTTAS ur fönstret i onCommit: fönstret som fältet satt i rörs
            // inte efter handlern.
            w.m1.focus();
            w.m1.textInput("x");
            w.m1.onCommit = [&w, &stämpelEfter] {
                w.root().unadopt(w.m1);
                stämpelEfter = w.probe.damageStamp();
            };
            w.m1.commitEdit();
            check(w.m1.parent() == nullptr &&
                      w.probe.damageStamp() == stämpelEfter,
                  "W1: onCommit flyttade fältet — commitFromUser ritar inte om "
                  "fönstret det satt i efter handlern",
                  std::to_string(w.probe.damageStamp()) + " mot " +
                      std::to_string(stämpelEfter));
            // ---- E8. commitEdits() och färgväljarens hexfält i en ÖPPEN
            // popup: det skrivna committas, popupen står kvar.
            w.farg.setColor("#3366FF");
            w.farg.focus();
            w.tangent(gfx::Key::Space);
            if (!w.farg.isOpen()) {
                std::printf("  --   E8 hoppas över: popupen kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            for (int i = 0; i < 4; ++i)
                w.tangent(gfx::Key::Tab);   // rött, grönt, blått, hex
            w.tangent(gfx::Key::A, kRedigera);
            w.skriv("#405060");
            märke = g_logg.size();
            w.commitEdits();
            {
                const gfx::Color c = w.farg.color();
                check(värden("farg", märke) == Lista{"change", "commit"} &&
                          c.r == 0x40 && c.g == 0x50 && c.b == 0x60 &&
                          w.farg.isOpen(),
                      "E8: commitEdits() — färgväljarens skrivna hexfärg "
                      "committas synkront, popupen står kvar",
                      text(värden("farg", märke)) + " " + w.farg.colorText());
            }
            w.tangent(gfx::Key::Escape);
            break;
        }
        case 47: {
            check(!w.farg.isOpen() && antal("farg:commit", märke) == 1,
                  "E8: …Escape stängde utan en andra commit (inget kvar att "
                  "committa)", text(värden("farg", märke)));

            // ==== ÄGDA WIDGETAR OCH EDITORER ====
            // ---- E9. Editorns EGEN onCommit flyttar fokus mitt i
            // commitEdits(): gridden committar då i sin focusLost (postat) —
            // och ändå ska dess events vara levererade när commitEdits()
            // återvänder.
            w.klick(w.värdecell(w.grid2, 0));
            w.skriv("31");
            glaze::TextInput* ed = nullptr;
            for (glaze::Widget* b : w.grid2.children())
                if (b->isVisible())
                    ed = dynamic_cast<glaze::TextInput*>(b);
            if (ed) {
                ed->onCommit = [&w] { w.knapp.focus(); };
                märke = g_logg.size();
                w.commitEdits();
                check(värden("grid2", märke) == Lista{"change", "commit"} &&
                          w.grid2.rows()[0].value == "31",
                      "E9: editorns onCommit flyttade fokus — gridens onChange "
                      "och onCommit är ÄNDÅ levererade när commitEdits() "
                      "återvänder", text(värden("grid2", märke)));
                ed->onCommit = nullptr;
            } else {
                check(false, "E9: gridens editor öppnades inte");
            }
            // E10: samma väg i datagridden.
            w.tabell2.setCurrentCell(0, 0);
            w.tabell2.focus();
            w.tangent(gfx::Key::F2);
            w.skriv("q");
            glaze::TextInput* ted = nullptr;
            for (glaze::Widget* b : w.tabell2.children())
                if (b->isVisible())
                    ted = dynamic_cast<glaze::TextInput*>(b);
            if (ted) {
                ted->onCommit = [&w] { w.knapp.focus(); };
                märke = g_logg.size();
                w.commitEdits();
                check(värden("tabell2", märke) == Lista{"change", "commit"},
                      "E10: datagridens editor flyttade fokus i sin onCommit — "
                      "cellen är ändå sparad och levererad när commitEdits() "
                      "återvänder", text(värden("tabell2", märke)));
                ted->onCommit = nullptr;
            } else {
                check(false, "E10: datagridens editor öppnades inte");
            }
            // ---- E11 (grindluckan). Ingenting att committa — inget fönster
            // att rita om; något att committa — omritat.
            w.knapp.focus();
            stämpelFöre = w.probe.damageStamp();
            w.commitEdits();
            check(w.probe.damageStamp() == stämpelFöre,
                  "E11: commitEdits() utan något att committa ritar inte om "
                  "fönstret", std::to_string(w.probe.damageStamp()) + " mot " +
                                  std::to_string(stämpelFöre));
            w.falt.focus();
            w.skriv("e");
            stämpelFöre = w.probe.damageStamp();
            w.commitEdits();
            check(w.probe.damageStamp() > stämpelFöre,
                  "E11: …och med en commit ritar den om det");
            // ---- E12 (grindluckan). FilePicker::commitEdit DIREKT.
            if (glaze::TextInput* sf = fältI(w.sökväg)) {
                sf->focus();
                w.skriv("z");
                märke = g_logg.size();
                w.sökväg.commitEdit();
                check(värden("sökväg", märke) == Lista{"commit"},
                      "E12: FilePicker::commitEdit() direkt — fältets skrivna "
                      "sökväg committas", text(värden("sökväg", märke)));
            }
            // ---- P11. En köad sparning och FÄRRE rader innan
            // svansen: raden finns inte — inget event, ingen krasch.
            w.klick(w.värdecell(w.grid2, 2));
            w.skriv("44");
            w.klick(w.knapp);   // fokus bort: sparningen köad
            märke = g_logg.size();
            w.grid2.setRows({{"A", "1"}, {"B", "2"}});
            break;
        }
        case 48: {
            check(värden("grid2", märke).empty() && w.grid2.rows().size() == 2,
                  "P11: köad sparning av rad 2 och setRows med två rader — "
                  "inget event", text(värden("grid2", märke)));
            {
                // P12: en tom grid, addRow och setRow på den nya raden — med
                // en köad sparning emellan finns generationen redan.
                glaze::PropertyGrid tom;
                tom.addRow("Ny", "1");
                tom.setRow(0, "Ny", "2");
                check(tom.rows().size() == 1 && tom.rows()[0].value == "2",
                      "P12: addRow på en tom grid och setRow på raden — "
                      "radlistan och generationerna följs åt");
            }
            // ---- R6. En ÄGD kryssruta vars onChange river den och
            // SEDAN läser sin capture — det anropsbara ska leva genom
            // anropet (under en sanerare: inget use-after-free).
            auto* k2 = w.root().add<glaze::Checkbox>("k2");
            k2->name = "k2";
            k2->x    = 790;
            k2->y    = 464;
            k2->onChange = [&w, k2, lång, &läst6] {
                w.root().unadopt(*k2);   // ÄGD — förstörs, och handlern med
                läst6 = lång;            // capturen läses EFTER rivningen
            };
            w.klick(*k2);
            check(läst6 == lång,
                  "R6: en handler river sin widget och läser sedan sin "
                  "capture — den lever genom anropet");
            // R7: samma i fokusleveransen.
            f7        = w.root().add<glaze::TextInput>();
            f7->name  = "f7";
            f7->x     = 650;
            f7->y     = 500;
            f7->width = 120;
            f7->focus();
            break;
        }
        case 49: {
            f7->onFocusLost = [&w, &f7, lång, &läst7] {
                w.root().unadopt(*f7);   // ÄGD — förstörs i leveransen
                f7    = nullptr;
                läst7 = lång;
            };
            w.knapp.focus();   // f7:s onFocusLost köas
            break;
        }
        case 50: {
            check(!f7 && läst7 == lång,
                  "R7: en onFocusLost river sin widget och läser sedan sin "
                  "capture — den lever genom fokusleveransen");
            // ---- Z4. Omankringen ENSAM ska rita om: careten står
            // still (pekaren bortom textens slut), bara ankaret flyttar.
            w.z1.setText("abcdefghij");
            w.z1.onFocus = [&w] { w.z1.selectAll(); };
            w.knapp.focus();
            tryck = w.punkt(w.z1, 0.2f);
            w.mus(gfx::EventType::MouseButtonDown, tryck.x, tryck.y);
            tryckIndex = w.z1.caret();
            break;
        }
        case 51: {
            // Hovern först (en rörelse INOM tröskeln): pekarens första
            // rörelse över fältet ritar om det av sig själv, och stämpeln ska
            // mäta omankringen och inget annat.
            w.mus(gfx::EventType::MouseMotion, tryck.x + 1.0f, tryck.y);
            stämpelFöre       = w.z1.damageStamp();
            const gfx::Pointf bortom = w.punkt(w.z1, 0.95f);
            w.mus(gfx::EventType::MouseMotion, bortom.x, bortom.y);
            check(tryckIndex > 0 && w.z1.selBegin() == tryckIndex &&
                      w.z1.selEnd() == 10 &&
                      w.z1.damageStamp() > stämpelFöre,
                  "Z4: draget ankrar om vid trycket medan careten står still — "
                  "fältet RITAS OM (damage-stämpeln stiger)",
                  sel(w.z1) + " stämpel " + std::to_string(w.z1.damageStamp()) +
                      " mot " + std::to_string(stämpelFöre));
            w.mus(gfx::EventType::MouseButtonUp, bortom.x, bortom.y);
            // Z5: samma i det flerradiga fältet.
            w.z2.setText("rad ett\nrad två");
            w.z2.onFocus = [&w] { w.z2.selectAll(); };
            w.knapp.focus();
            tryck = w.punkt(w.z2, 0.15f, 0.12f);
            w.mus(gfx::EventType::MouseButtonDown, tryck.x, tryck.y);
            tryckIndex = w.z2.caret();
            break;
        }
        case 52: {
            w.mus(gfx::EventType::MouseMotion, tryck.x + 1.0f, tryck.y);
            stämpelFöre       = w.z2.damageStamp();
            const gfx::Pointf bortom = w.punkt(w.z2, 0.95f, 0.9f);
            w.mus(gfx::EventType::MouseMotion, bortom.x, bortom.y);
            check(tryckIndex > 0 && w.z2.selBegin() == tryckIndex &&
                      w.z2.selEnd() == w.z2.text().size() &&
                      w.z2.damageStamp() > stämpelFöre,
                  "Z5: TextArea — omankringen ensam ritar om",
                  sel(w.z2) + " stämpel " + std::to_string(w.z2.damageStamp()) +
                      " mot " + std::to_string(stämpelFöre));
            w.mus(gfx::EventType::MouseButtonUp, bortom.x, bortom.y);

            // ==== FÖNSTER SOM RIVS, REKURSION OCH UNDANTAG ====
            // ---- E16/E17. En fokuserad OFÖRÄNDRAD komposit: ett
            // commitEdits() ritar om varken den eller fönstret.
            w.tal3.focus();
            stämpelFöre = w.tal3.damageStamp();
            w.commitEdits();
            check(w.tal3.damageStamp() == stämpelFöre,
                  "E16: commitEdits() på ett oförändrat talfält ritar inte om "
                  "det", std::to_string(w.tal3.damageStamp()) + " mot " +
                             std::to_string(stämpelFöre));
            w.datum3.focus();
            stämpelFöre = w.datum3.damageStamp();
            w.commitEdits();
            check(w.datum3.damageStamp() == stämpelFöre,
                  "E17: commitEdits() på en oförändrad datumväljare ritar inte "
                  "om den", std::to_string(w.datum3.damageStamp()) + " mot " +
                                std::to_string(stämpelFöre));

            // ---- E13 (punkt 3). Editorns onCommit flyttar fokus (gridens
            // commit samlas in) och anropar sedan "Spara", som anropar
            // commitEdits() REKURSIVT — och ska se modellen som gridens
            // handler uppdaterar.
            w.grid2.onCommit = [&w, &modell] {
                g_logg.push_back("grid2:commit");
                modell = w.grid2.rows()[0].value;
            };
            w.klick(w.värdecell(w.grid2, 0));
            w.skriv("77");
            glaze::TextInput* ed13 = nullptr;
            for (glaze::Widget* b : w.grid2.children())
                if (b->isVisible())
                    ed13 = dynamic_cast<glaze::TextInput*>(b);
            if (ed13) {
                ed13->onCommit = [&w, &modell, &sparat] {
                    w.knapp.focus();   // gridens commit samlas in
                    w.commitEdits();   // "Spara" — rekursivt
                    sparat = modell;
                };
                w.commitEdits();
                check(sparat == "77",
                      "E13: ett rekursivt commitEdits() i en insamlande kedja "
                      "levererar det den yttre samlat — Spara ser det nya "
                      "värdet", "'" + sparat + "'");
                ed13->onCommit = nullptr;
            } else {
                check(false, "E13: gridens editor öppnades inte");
            }

            // ---- E14 (punkt 3). En MODAL dialog i en insamlad handler:
            // resten av kön levereras i dess pump, i ordning.
            w.falt.onCommit = [&w] {
                w.kö.postEvent([&w] {
                    g_logg.push_back("kö:A:före");
                    Hjälp d;
                    d.setOffscreen(true);
                    glaze::app().post([&d] { d.close(); });
                    d.run(w);
                    g_logg.push_back("kö:A:efter");
                });
                w.kö.postEvent([] { g_logg.push_back("kö:B"); });
                w.kö.postEvent([] { g_logg.push_back("kö:C"); });
            };
            w.falt.focus();
            w.skriv("m");
            märke = g_logg.size();
            w.commitEdits();
            {
                Lista kö;
                for (std::size_t i = märke; i < g_logg.size(); ++i)
                    if (g_logg[i].rfind("kö:", 0) == 0)
                        kö.push_back(g_logg[i]);
                check(kö == Lista{"kö:A:före", "kö:B", "kö:C", "kö:A:efter"},
                      "E14: en modal dialog i en insamlad handler levererar "
                      "resten av kön i sin pump, i ordning", text(kö));
            }

            // ---- E15 (punkt 2). Ett UNDANTAG ur en handler i kedjan: det når
            // anroparen, och det som hann samlas in levereras ändå (i pumpens
            // svans) — ingen pekare in i en död stackram finns kvar.
            w.falt.onCommit = [&w] {
                w.kö.postEvent([] { g_logg.push_back("kö:D"); });
                throw std::runtime_error("handlern kastar");
            };
            w.skriv("n");
            märke  = g_logg.size();
            fångat = false;
            try {
                w.commitEdits();
            } catch (const std::runtime_error&) {
                fångat = true;
            }
            w.falt.onCommit = [] { g_logg.push_back("falt:commit"); };
            check(fångat, "E15: undantaget ur handlern nådde anroparen");
            break;
        }
        case 53: {
            check(antal("kö:D", märke) == 1,
                  "E15: …och händelsen som samlades in före undantaget "
                  "levererades i pumpens svans");
            // ---- RV1 (punkt 1). Ett talfälts STEG vars onChange river det
            // ägande FÖNSTRET mitt i dess dispatch (musvägen).
            rv    = std::make_unique<Rivbar>();
            rvTal = rv->root().add<glaze::SpinBox>();
            rvTal->x     = 10;
            rvTal->y     = 10;
            rvTal->width = 120;
            rvTal->setMax(100);
            rvTal->onChange = [&rv] {
                g_logg.push_back("rv1:change");
                rv.reset();   // fönstret dör — och talfältet med det
            };
            rv->setOffscreen(true);
            rv->show(w);
            break;
        }
        case 54: {
            if (!rv || !rv->isOpen()) {
                std::printf("  --   RV-fallen hoppas över: fönstret kom aldrig "
                            "upp\n");
                ++g_skips;
                w.close();
                return;
            }
            const gfx::Rectf r = rvTal->resolve(rvTal->behaviorCtx());
            const float      s = rv->style().spinStepW * rv->window().uiScale();
            const gfx::Event e =
                händelse(*rv, gfx::EventType::MouseButtonDown,
                         r.x + r.w - s / 2.0f, r.y + r.h / 4.0f);
            märke = g_logg.size();
            rv->handleEvent(e);   // fönstret rivs INNE i anropet
            check(!rv && antal("rv1:change", märke) == 1,
                  "RV1: ett steg rev fönstret i sin onChange — dispatchen "
                  "slutade utan att läsa det döda fönstret");
            // RV2 (tangentvägen, KONSUMERAD): talfältets Upp stegar från den
            // skrivna texten och onChange river fönstret. (Enter committar och
            // BUBBLAR som textfältets,
            // alltså RV3:s väg.)
            rv     = std::make_unique<Rivbar>();
            rvTal  = rv->root().add<glaze::SpinBox>();
            rvTal->x     = 10;
            rvTal->y     = 10;
            rvTal->width = 120;
            rvTal->setMax(100);
            rvTal->onChange = [&rv] {
                g_logg.push_back("rv2:change");
                rv.reset();
            };
            rv->setOffscreen(true);
            rv->show(w);
            break;
        }
        case 55: {
            if (!rv) {
                check(false, "RV2: fönstret fanns inte");
                w.close();
                return;
            }
            rvTal->focus();
            gfx::Event t = händelse(*rv, gfx::EventType::TextInput);
            std::snprintf(t.text, sizeof t.text, "%s", "7");
            rv->handleEvent(t);
            gfx::Event k = händelse(*rv, gfx::EventType::KeyDown);
            k.key = gfx::Key::Up;
            märke = g_logg.size();
            rv->handleEvent(k);   // Upp konsumeras, och fönstret rivs
            check(!rv && antal("rv2:change", märke) == 1,
                  "RV2: talfältets steg rev fönstret — den konsumerade "
                  "tangenten ritade inte om ett dött fönster");
            // RV3 (tangentvägen, INTE konsumerad): ett textfälts onCommit.
            rv     = std::make_unique<Rivbar>();
            rvFält = rv->root().add<glaze::TextInput>();
            rvFält->x     = 10;
            rvFält->y     = 10;
            rvFält->width = 120;
            rvFält->onCommit = [&rv] {
                g_logg.push_back("rv3:commit");
                rv.reset();
            };
            rv->setOffscreen(true);
            rv->show(w);
            break;
        }
        case 56: {
            if (!rv) {
                check(false, "RV3: fönstret fanns inte");
                w.close();
                return;
            }
            rvFält->focus();
            gfx::Event t = händelse(*rv, gfx::EventType::TextInput);
            std::snprintf(t.text, sizeof t.text, "%s", "a");
            rv->handleEvent(t);
            gfx::Event k = händelse(*rv, gfx::EventType::KeyDown);
            k.key = gfx::Key::Return;
            märke = g_logg.size();
            rv->handleEvent(k);   // Enter bubblar vidare — fönstret är dött
            check(!rv && antal("rv3:commit", märke) == 1,
                  "RV3: textfältets onCommit rev fönstret — dispatchen frågade "
                  "inte förfäderna och acceleratorerna i ett dött fönster");
            break;
        }
        case 57: {
            // ==== ÅNGRA-HISTORIK ====
            // ---- U1. Editorns ÅNGRA-HISTORIK följer inte med till nästa rad.
            // Gridden ÅTERANVÄNDER sin editor, och `setText`s likhetsvakt
            // bevarar historiken när nästa rad har samma värde: rad A "a" →
            // "b", rad B öppnas med "b", och Ctrl+Z hade gjort B till "a".
            w.grid2.onCommit = [] { g_logg.push_back("grid2:commit"); };
            w.grid2.setRows({{"A", "a"}, {"B", "b"}});
            w.klick(w.värdecell(w.grid2, 0));   // A — värdet markerat
            w.skriv("b");                        // ersätter "a"
            w.klick(w.värdecell(w.grid2, 1));   // sparar A, öppnar B med "b"
            glaze::TextInput* edU = nullptr;
            for (glaze::Widget* b : w.grid2.children())
                if (b->isVisible())
                    edU = dynamic_cast<glaze::TextInput*>(b);
            if (edU) {
                const bool fanns = edU->canUndo();
                w.tangent(gfx::Key::Z, kRedigera);
                check(w.grid2.rows()[0].value == "b" && !fanns &&
                          edU->text() == "b",
                      "U1: en ny rad i egenskapslistan börjar med TOM historik — "
                      "Ctrl+Z spelar inte upp förra radens redigering",
                      "rad A '" + w.grid2.rows()[0].value + "', editorn '" +
                          edU->text() + "'");
                w.tangent(gfx::Key::Escape);
            } else {
                check(false, "U1: gridens editor öppnades inte på rad B");
            }
            w.close();
            return;
        }
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();

    if (!kordes) {
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

    std::printf("commit_gate — commit, fokus inom och markeringen\n");
    glaze::setLanguage("sv");
    fallMarkering();
    fallVidarebefordran();
    fallProgrammatiskt();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
