// dialog_gate — STANDARD- OCH AVBRYTKNAPPEN:
// `AppWindow::setAcceptButton`/`setCancelButton`, `Button::setDialogResult`/
// `performClick`, `AppWindow::dialogResult()`, vetot som nollställer
// resultatet, standardknappens ritning, och Enter-konsumenterna som släpper
// tangenten (talfältet, datumväljaren, listorna och de fyra kontrollerna).
//
// Kör:  ./build/tests/dialog_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett fönsterblock kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt
//
// HEADLESS
//   D1  KNAPPEN: `setProp("dialogResult", "cancel")` ⇒ Cancel, ett okänt namn
//       ⇒ false · `performClick` på en inaktiverad knapp kör inte `onClick` ·
//       `performClick` utan fönster kör `onClick` och kraschar inte
//   D2  LISTORNA: listvyn och trädet tar Enter med en aktuell rad bara
//       när `onActivate` är satt
//
// MED FÖNSTER — DOLDA fönster (`setOffscreen`), ALLT genom fönstrets riktiga
// ingång (`handleEvent`), som en kedja av postade steg (dropdown_gate):
//   W1  Enter i ett fokuserat fält ⇒ ok klickas EN gång, och fältets
//       `onCommit` kommer FÖRE ok:s `onClick`; Shift+Enter likaså
//   W2  fokus på avbryt + Enter ⇒ avbryt klickas (knappens eget Enter), ok inte
//   W3  TextArea: Enter ⇒ ny rad och ingen ok · Ctrl+Enter ⇒ ingen ok och
//       texten orörd · Shift+Enter ⇒ ny rad
//   W4  Escape i ett fält ⇒ avbryt · en fokuserad widget som TAR Escape ⇒
//       ingen avbryt · Ctrl+Escape och Shift+Escape ⇒ ingen avbryt
//   W5  acceptknappen dold, inaktiverad, i en dold Panel eller i ett ANNAT
//       fönster ⇒ Enter gör ingenting
//   W6  en ÄGD acceptknapp som `releaseOwned` och dör ⇒ `acceptButton()` är
//       nullptr och Enter gör ingenting (och kraschar inte)
//   W7  `isDefaultButton`: fokus i ett fält ⇒ ok · fokus på avbryt ⇒ avbryt
//       och inte ok · utan acceptknapp ⇒ ingen · ok:s `damageStamp` stegas när
//       fokus går fält → avbryt (`focus()`, ingen full omritning) · avbryt
//       dold medan den har fokus ⇒ ok tillbaka, med stämpeln stegad
//   W8  `run(ägare)`: Enter ⇒ OK, Escape ⇒ Cancel · ok utan `dialogResult`
//       vars `onClick` inte stänger ⇒ dialogen står kvar · `onClose`-veto ⇒
//       `dialogResult()` None, och ett senare kryss ⇒ `run` svarar None
//   W9  meddelanderutan, alla sex uppsättningar: `acceptButton()` = första
//       knappen, knapparnas `dialogResult` = tabellens, `cancelButton()` =
//       avbryt-ekvivalenten eller nullptr (YesNo, AbortRetryIgnore), och
//       `performClick` på avbrytknappen (annars den första) ger svaret
//   W10 talfältet och datumväljaren: Enter committar värdet OCH klickar ok,
//       och ok:s handler ser det committade värdet
//   R   REGRESSIONERNA: de fyra kontrollerna —
//       kryssrutan, radioknappen, den stängda dropdownen och färgväljaren —
//       släpper Enter till ok och ändras inte (Space gör fortfarande sitt) ·
//       en förfader som tar Enter i `descendantKeyDown` vinner · Enter och
//       Escape bundna som acceleratorer vinner · Escape med en ÖPPEN meny,
//       dropdown, färgpopup och datumpopup stänger popupen och inte dialogen ·
//       en `onClick` som RIVER fönstret, en som FLYTTAR knappen till ett annat
//       fönster (ingen stängning, inte heller av det nya) och handlerordningen
//       (`dialogResult()` är None medan `onClick` körs)
//   T   TRÄDMUTATIONEN: acceptknappen lossad ur
//       sin panel och adopterad tillbaka — knappen med fokus i en ANNAN panel
//       byter roll och ritas om (`damageStamp`)
//   P   EN ÖPPEN POPUP UTAN FOKUS: färgväljaren,
//       datumväljaren (genom fältet, ombudet) och dropdownen öppnade med
//       `open()` medan kryssrutan har fokus — Escape stänger popupen och
//       Enter väljer i listan, och varken avbryt eller ok klickas
//   G   EGENSKAPSLISTANS VALLISTA: Escape i den
//       öppna listan och Enter respektive Escape i den STÄNGDA listan som står
//       kvar i cellen avslutar redigeringen (`activeRow() == -1`), och
//       dialogens knappar klickas inte
//   I1  INMATNINGSRUTAN: fönstret (`input_box_window.h`
//       i en subklass som når `handleEvent`) visar förvalet MARKERAT med fokus i
//       fältet, OK och Avbryt är fönstrets standard- och avbrytknapp, skriv +
//       Enter ⇒ OK med texten, Escape ⇒ inget svar; den fria funktionen
//       `glaze::inputBox` svarar med en TOM text vid OK (ett svar) och med
//       `std::nullopt` vid Avbryt. FLERA RADER
//       (`InputBoxMode::MultiLine`): fältet "field" är ett `TextArea` med fokus
//       och förvalet markerat, Enter ger en ny rad (fönstret förblir öppet) och
//       OK med knappen svarar "b\nc" — fönstret direkt, inte den fria funktionen
//   W11 PIXLAR (pixel_gate:s väg, `renderFrame`/`captureFrame`): två knappar
//       med samma text — medelfärgen i acceptknappens yta skiljer sig från den
//       andras i Glaze, Slate och Minimalistic, och efter `avbryt.focus()` har
//       rollerna bytts
//
// SABOTAGEN den ska fälla (ett i taget):
//   * steget borttaget ur `handleEvent` — W1, W4, W8, W10, R
//   * steget flyttat FÖRE den fokuserade widgeten — W2, W3, W4 (`egen`), R
//     (dropdownens/färgväljarens Escape)
//   * modifierarvillkoret struket — W3 (Ctrl+Enter), W4 (Ctrl/Shift+Escape)
//   * `usable` utan `shownInTree` — W5 (dold Panel); utan fönstervillkoret —
//     W5 (annat fönster); utan `isEnabled` — W5
//   * livstecknet oprövat i `acceptButton`/`liveAccept` — W6 (krasch eller fel
//     svar; säkrast under ASAN, som inte körs i vardagssviten)
//   * `setFocus` utan `defaultButtonChanged` — W7 (stämpeln); `isDefaultButton`
//     utan fokusregeln — W7, W11; `refreshDefaultButton` borttagen — W7 (dold)
//   * `defaultButtonMayChange` borttagen ur `detachTree` eller `adopt` — T
//   * `Button::clicked` utan `dialogResult` — W8, W9; vetot som inte nollställer
//     — W8; `clicked` utan vakten efter `onClick` — R (flyttad knapp: det nya
//     fönstret fick resultatet)
//   * talfältets/datumväljarens Enter konsumerad — W10
//   * listornas Enter-regel bortplockad — D2; de fyra kontrollernas — R
//   * meddelanderutans avbrytknapp på första knappen, eller glömd
//     `setCancelButton` — W9
//   * inmatningsrutan utan `setCancelButton` (I1: Escape svarar) eller utan
//     `selectAll` (I1: förvalet är inte markerat), och `inputBox` som svarar
//     `nullopt` för en tom text (I1); flerradsläget som skapar ett `TextInput`
//     ("field" är inte ett TextArea), eller där Enter är OK (I1: `öppen` falsk)
//   * `Button::draw` utan `shownAsDefault`, eller `isDefaultButton` alltid sann
//     — W11
//   * popupvägen borttagen ur steget (`openPopupKeyTarget`) — P1, P2, P3;
//     utan fokusombudet (`proxyTarget`) — P2
//   * egenskapslistans `ChoiceEditor` utbytt mot en vanlig `Dropdown`, eller
//     dess Enter-/Escape-gren borttagen — G1, G2, G3
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   - hur standardknappen SER UT (färgvärden, referensbilder) — W11 mäter bara
//     att den skiljer sig och att rollen följer fokus; kontrasten är
//     contrast_gates `onAcc`-rader
//   - backendernas tangentöversättning (eventen matas direkt), IME, macOS Cmd+.
//   - den nedhållna Enter-kaskaden till ägaren efter en stängd dialog
//   - GlazeBuilders duk och egna knappsubklasser i previewn
//   - en container som döljer innehåll UTAN `setVisible` (en infälld
//     dockningspanel) — standardutseendet ritas om först vid nästa omritning,
//     en skriven gräns
//   - meddelanderutans Escape genom ingången (rutan är intern och dess
//     `handleEvent` skyddad) — W9 mäter kopplingen, W4 vägen

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "input_box_window.h"    // intern — inmatningsrutans fönster (I1)
#include "message_box_table.h"   // intern (tests/CMakeLists.txt pekar hit)

namespace {

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

void hoppa(const std::string& varför)
{
    std::printf("  --   %s\n", varför.c_str());
    ++g_skips;
}

using glaze::DialogResult;
using K = gfx::Key;

constexpr std::uint32_t kCtrl  = gfx::kModCtrl;
constexpr std::uint32_t kShift = gfx::kModShift;
// Fältens redigeringsgenvägar (Ctrl+A) är plattformens — Cmd på macOS, Ctrl
// annars (`textfield::editShortcut`).
#ifdef __APPLE__
constexpr std::uint32_t kRedigera = gfx::kModCmd;
#else
constexpr std::uint32_t kRedigera = kCtrl;
#endif

const char* namn(DialogResult r)
{
    switch (r) {
    case DialogResult::None:   return "None";
    case DialogResult::OK:     return "OK";
    case DialogResult::Cancel: return "Cancel";
    case DialogResult::Yes:    return "Yes";
    case DialogResult::No:     return "No";
    case DialogResult::Abort:  return "Abort";
    case DialogResult::Retry:  return "Retry";
    case DialogResult::Ignore: return "Ignore";
    }
    return "?";
}

// En TANGENT och en TEXT genom fönstrets riktiga ingång — en mall, för
// varje provfönster nedan når `handleEvent` på samma sätt.
template <class F>
void tangent(F& f, K k, std::uint32_t mods = 0)
{
    gfx::Event e;
    e.type     = gfx::EventType::KeyDown;
    e.windowId = f.window().id();
    e.key      = k;
    e.mods     = mods;
    f.handleEvent(e);
}

template <class F>
void skriv(F& f, const char* utf8)
{
    gfx::Event e;
    e.type     = gfx::EventType::TextInput;
    e.windowId = f.window().id();
    std::strncpy(e.text, utf8, sizeof(e.text) - 1);
    f.handleEvent(e);
}

// Ett KLICK — tryck och släpp med vänsterknappen — i fysiska fönster-px.
template <class F>
void klick(F& f, gfx::Pointf p)
{
    gfx::Event e;
    e.windowId = f.window().id();
    e.mouseX   = p.x;
    e.mouseY   = p.y;
    e.button   = gfx::MouseButton::Left;
    e.type     = gfx::EventType::MouseButtonDown;
    f.handleEvent(e);
    e.type = gfx::EventType::MouseButtonUp;
    f.handleEvent(e);
}

// ============================================================ D1 KNAPPEN

void fallKnappen()
{
    std::printf("\n=== D1. knappen ===\n");

    glaze::Button b("Knapp");
    check(b.dialogResult() == DialogResult::None,
          "D1: förvalet är None (en vanlig knapp)");
    check(b.setProp("dialogResult", "cancel") &&
              b.dialogResult() == DialogResult::Cancel,
          "D1: setProp(\"dialogResult\", \"cancel\") ⇒ Cancel",
          namn(b.dialogResult()));
    check(b.setProp("dialogresult", "ignore") &&
              b.dialogResult() == DialogResult::Ignore,
          "D1: den gemena nyckeln och sista värdet (ignore)",
          namn(b.dialogResult()));
    check(!b.setProp("dialogResult", "kanske") &&
              b.dialogResult() == DialogResult::Ignore,
          "D1: ett okänt namn ⇒ false (en opak rad) och värdet orört",
          namn(b.dialogResult()));

    int klick = 0;
    b.onClick = [&klick] { ++klick; };
    b.disable();
    b.performClick();
    check(klick == 0, "D1: performClick på en INAKTIVERAD knapp kör inte onClick",
          std::to_string(klick));
    b.enable();
    b.setDialogResult(DialogResult::OK);
    b.performClick();   // inget fönster: onClick, och ingen stängning att göra
    check(klick == 1,
          "D1: performClick UTAN fönster kör onClick (och kraschar inte)",
          std::to_string(klick));
}

// ============================================================ D2 LISTORNA

struct ProvLista : glaze::ListView {
    using glaze::ListView::keyDown;
};
struct ProvTrad : glaze::TreeView {
    using glaze::TreeView::keyDown;
};

void fallListorna()
{
    std::printf("\n=== D2. listorna (B9) ===\n");

    int       akt = 0;
    ProvLista l;
    l.addColumn("Namn");
    l.addRow({"a"});
    l.addRow({"b"});
    l.setSelectedIndex(0);
    check(l.currentRow() == 0, "D2: listvyn har en aktuell rad (förutsättning)");
    check(!l.keyDown(K::Return, 0),
          "D2: listvyn UTAN onActivate tar inte Enter (den bubblar)");
    l.onActivate = [&akt] { ++akt; };
    check(l.keyDown(K::Return, 0) && akt == 1,
          "D2: listvyn MED onActivate tar Enter och aktiverar",
          std::to_string(akt));

    ProvTrad t;
    t.addNode({}, "A");
    t.addNode({}, "B");
    t.setSelectedPath({0});
    check(!t.keyDown(K::Return, 0),
          "D2: trädet UTAN onActivate tar inte Enter (den bubblar)");
    t.onActivate = [&akt] { ++akt; };
    check(t.keyDown(K::Return, 0) && akt == 2,
          "D2: trädet MED onActivate tar Enter och aktiverar",
          std::to_string(akt));
}

// ============================================================ FÖNSTREN

// En widget som TAR Escape själv (W4) — som en editor som avbryter sin egen
// redigering.
struct TarEscape : glaze::Widget {
    int  escape = 0;
    bool focusable() const override { return true; }

protected:
    void draw(const glaze::Ctx&) override {}   // grindens widget ritar inget
    bool keyDown(gfx::Key k, std::uint32_t) override
    {
        if (k != K::Escape)
            return false;
        ++escape;
        return true;
    }
};

// En container som tar Enter genom `descendantKeyDown` (R) — en förfader
// närmare fokus än standardknappen.
struct TarEnterUppifran : glaze::Panel {
    int enter = 0;

protected:
    bool descendantKeyDown(gfx::Key k, std::uint32_t) override
    {
        if (k != K::Return)
            return false;
        ++enter;
        return true;
    }
};

// Knappen med sin skyddade damage-stämpel läsbar (W7, T).
struct ProvKnapp : glaze::Button {
    using glaze::Button::Button;
    using glaze::Widget::damageStamp;
};

// HUVUDFÖNSTRET — knapparna RÄKNAR sina klick och bär inget resultat, så att
// fönstret står kvar genom hela kedjan; resultatet mäts i egna dialoger (W8).
struct Fonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::TextInput   falt;
    glaze::TextArea    yta;
    TarEscape          egen;
    glaze::Checkbox    kryss{"Kryss"};
    glaze::Radio       radio{"Radio"};
    glaze::Dropdown    dd;
    glaze::ColorPicker farg;
    glaze::DatePicker  datum;
    glaze::SpinBox     tal;
    TarEnterUppifran   ram;
    glaze::TextInput   inre;
    glaze::Panel       gomma;
    glaze::Button      gomdKnapp{"Gömd"};
    glaze::PropertyGrid pg;
    ProvKnapp          ok{"OK"};
    ProvKnapp          avbryt{"Avbryt"};

    std::vector<std::string> logg;
    int                      okKlick     = 0;
    int                      avbrytKlick = 0;
    int                      gomdKlick   = 0;
    double                   talVidKlick = -1.0;
    std::string              datumVidKlick;

    Fonster() : glaze::AppWindow("dialog_gate", 640, 400)
    {
        const auto ställ = [](glaze::Widget& w, const char* n, float x,
                              float y) {
            w.name = n;
            w.x    = x;
            w.y    = y;
        };
        ställ(falt, "falt", 10, 10);
        falt.width = 200;
        ställ(yta, "yta", 10, 50);
        yta.width  = 200;
        yta.height = 80;
        ställ(egen, "egen", 10, 140);
        egen.width  = 40;
        egen.height = 20;
        ställ(kryss, "kryss", 10, 170);
        ställ(radio, "radio", 10, 200);
        radio.setGroup("ensam");
        ställ(dd, "dd", 230, 10);
        dd.width = 160;
        dd.setItems({"Ett", "Två"});
        ställ(farg, "farg", 230, 50);
        ställ(datum, "datum", 230, 90);
        datum.setDate(2026, 10, 8);
        ställ(tal, "tal", 230, 130);
        tal.setMax(1000);
        ställ(ram, "ram", 230, 170);
        ram.width  = 220;
        ram.height = 40;
        ställ(inre, "inre", 4, 4);
        inre.width = 200;
        ställ(gomma, "gomma", 460, 10);
        gomma.width  = 160;
        gomma.height = 60;
        ställ(gomdKnapp, "gomdKnapp", 4, 4);
        ställ(pg, "pg", 460, 80);
        pg.width  = 170;
        pg.height = 80;
        pg.setNameWidth(80);
        pg.setRows({{"Typ", "A", {"A", "B", "C"}}});
        ställ(ok, "ok", 300, 340);
        ställ(avbryt, "avbryt", 420, 340);

        for (glaze::Widget* w : std::initializer_list<glaze::Widget*>{
                 &falt, &yta, &egen, &kryss, &radio, &dd, &farg, &datum,
                 &tal, &ram, &gomma, &pg, &ok, &avbryt})
            root().adopt(*w);
        ram.adopt(inre);
        gomma.adopt(gomdKnapp);

        falt.onCommit  = [this] { logg.push_back("falt:commit"); };
        ok.onClick     = [this] {
            ++okKlick;
            logg.push_back("ok:klick");
            talVidKlick   = tal.value();
            datumVidKlick = datum.dateText();
        };
        avbryt.onClick = [this] { ++avbrytKlick; };
        gomdKnapp.onClick = [this] { ++gomdKlick; };

        setAcceptButton(ok);
        setCancelButton(avbryt);
    }
};

// Ett ANNAT fönster med en knapp (W5) — visas aldrig: knappen sitter i det
// ändå (rotens ägare sätts när fönstret konstrueras).
struct Annat : glaze::AppWindow {
    glaze::Button knapp{"Annan"};
    int           klick = 0;
    Annat() : glaze::AppWindow("dialog_gate: annat", 200, 100)
    {
        knapp.name = "knapp";
        root().adopt(knapp);
        knapp.onClick = [this] { ++klick; };
    }
};

// Ett litet fönster med ett fält och en OK som BÄR sitt resultat — rivningens,
// flyttens och handlerordningens prov (R).
struct Litet : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    glaze::TextInput falt;
    glaze::Button    ok{"OK"};
    explicit Litet(const char* titel) : glaze::AppWindow(titel, 260, 120)
    {
        falt.name  = "falt";
        falt.x     = 10;
        falt.y     = 10;
        falt.width = 200;
        ok.name    = "ok";
        ok.x       = 10;
        ok.y       = 60;
        root().adopt(falt);
        root().adopt(ok);
        ok.setDialogResult(DialogResult::OK);
        setAcceptButton(ok);
    }
};

// Acceleratorerna (R) — Enter och Escape bundna i menyraden, som frågas FÖRE
// steget: kollisionsregeln säger att närmare fokus vinner.
struct Accel : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    glaze::TextInput falt;
    glaze::Button    ok{"OK"};
    glaze::Button    avbryt{"Avbryt"};
    int              kord = 0, stoppad = 0, okKlick = 0, avbrytKlick = 0;
    Accel() : glaze::AppWindow("dialog_gate: accel", 320, 160)
    {
        falt.name  = "falt";
        falt.x     = 10;
        falt.y     = 10;
        falt.width = 200;
        ok.name    = "ok";
        ok.x       = 10;
        ok.y       = 60;
        avbryt.name = "avbryt";
        avbryt.x    = 120;
        avbryt.y    = 60;
        root().adopt(falt);
        root().adopt(ok);
        root().adopt(avbryt);
        ok.onClick     = [this] { ++okKlick; };
        avbryt.onClick = [this] { ++avbrytKlick; };
        setAcceptButton(ok);
        setCancelButton(avbryt);
        menuBar.addMenu("Arkiv");
        menuBar.addItem("Arkiv", "K\xC3\xB6r", "Enter");
        menuBar.addItem("Arkiv", "Stoppa", "Esc");
        menuBar.setHandler({"Arkiv", "K\xC3\xB6r"}, [this] { ++kord; });
        menuBar.setHandler({"Arkiv", "Stoppa"}, [this] { ++stoppad; });
        menuBar.show();
    }
};

// Trädmutationen (T): acceptknappen i en panel, en annan knapp i en ANNAN.
struct Trad : glaze::AppWindow {
    glaze::Panel p1;
    glaze::Panel p2;
    ProvKnapp    ok{"OK"};
    ProvKnapp    knapp{"Knapp"};
    Trad() : glaze::AppWindow("dialog_gate: träd", 360, 160)
    {
        p1.name   = "p1";
        p1.x      = 10;
        p1.y      = 10;
        p1.width  = 160;
        p1.height = 80;
        p2.name   = "p2";
        p2.x      = 190;
        p2.y      = 10;
        p2.width  = 160;
        p2.height = 80;
        ok.name    = "ok";
        ok.x       = 4;
        ok.y       = 4;
        knapp.name = "knapp";
        knapp.x    = 4;
        knapp.y    = 4;
        root().adopt(p1);
        root().adopt(p2);
        p1.adopt(ok);
        p2.adopt(knapp);
        setAcceptButton(ok);
    }
};

// En MODAL dialog med standard- och avbrytknapp (W8). `medResultat` falskt ⇒
// OK bär inget resultat (en OK som validerar och stänger själv — här gör den
// det inte).
struct Dialog : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    glaze::TextInput falt;
    glaze::Button    ok{"OK"};
    glaze::Button    avbryt{"Avbryt"};
    int              okKlick = 0;
    explicit Dialog(bool medResultat)
        : glaze::AppWindow("dialog_gate: dialog", 320, 160)
    {
        setModal(true);
        falt.name   = "falt";
        falt.x      = 10;
        falt.y      = 10;
        falt.width  = 200;
        ok.name     = "ok";
        ok.x        = 10;
        ok.y        = 60;
        avbryt.name = "avbryt";
        avbryt.x    = 120;
        avbryt.y    = 60;
        root().adopt(falt);
        root().adopt(ok);
        root().adopt(avbryt);
        ok.onClick = [this] { ++okKlick; };
        if (medResultat)
            ok.setDialogResult(DialogResult::OK);
        avbryt.setDialogResult(DialogResult::Cancel);
        setAcceptButton(ok);
        setCancelButton(avbryt);
    }
};

// KÖR EN DIALOG (filedialog_gate:s `kör`): den visas DOLD och pumpar modalt
// medan `steg` körs inne i dess pump. Steget får stänga den; gör det inte det
// stängs den efteråt, utan resultat, så en fallerande kontroll aldrig hänger
// grinden.
DialogResult kor(glaze::AppWindow& agare, Dialog& d,
                 const std::function<void()>& steg)
{
    d.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        if (!d.isOpen()) {
            hoppa("dialogen kom aldrig upp");
            return;
        }
        steg();
        d.close();   // idempotent — en köad stängning med resultat rörs inte
    });
    const DialogResult r = d.run(agare);
    if (!kordes)
        ++g_skips;
    return r;
}

const char* kRutansRubrik = "dialog_gate: meddelande";

// Meddelanderutan som just öppnats ovanpå `agare`, eller nullptr.
glaze::AppWindow* rutan(glaze::AppWindow& agare)
{
    for (glaze::AppWindow* x : glaze::app().windows())
        if (x != &agare && x->isOpen() && x->isModal() &&
            x->title() == kRutansRubrik)
            return x;
    return nullptr;
}

std::vector<glaze::Button*> knapparI(glaze::AppWindow& f)
{
    std::vector<glaze::Button*> v;
    for (glaze::Widget* w : f.root().children())
        if (auto* b = dynamic_cast<glaze::Button*>(w))
            v.push_back(b);
    return v;
}

// Egenskapslistans VÄRDECELL på rad `i`, i fysiska px — ur kontraktet, som
// commit_gate: ingen ram, raden `gridRowH` hög, värdekolumnen från
// `nameWidth` (80, satt i fönstret).
gfx::Pointf värdecell(Fonster& w, int i)
{
    const gfx::Rectf r = w.pg.resolve(w.pg.behaviorCtx());
    const float      s = w.window().uiScale();
    return gfx::Pointf{r.x + (80.0f + 40.0f) * s,
                       r.y + (float(i) + 0.5f) * w.style().gridRowH * s};
}

// Egenskapslistans vallista — ett ägt barn, en `Dropdown` — eller nullptr.
glaze::Dropdown* gridensVallista(Fonster& w)
{
    for (glaze::Widget* b : w.pg.children())
        if (auto* d = dynamic_cast<glaze::Dropdown*>(b))
            return d;
    return nullptr;
}

// ---------------------------------------------------------------- W1–W7, W10

void fallTangenter(Fonster& w)
{
    std::printf("\n=== W1–W7, W10. tangenterna genom fönstret ===\n");

    // W1. Enter i ett fält: commit FÖRST, sedan ok — en gång.
    w.falt.focus();
    skriv(w, "a");
    w.logg.clear();
    tangent(w, K::Return);
    check(w.okKlick == 1 &&
              w.logg == std::vector<std::string>{"falt:commit", "ok:klick"},
          "W1: Enter i fältet — onCommit FÖRST, sedan ok:s onClick, EN gång",
          std::to_string(w.okKlick));
    tangent(w, K::Return, kShift);
    check(w.okKlick == 2, "W1: Shift+Enter trycker också standardknappen",
          std::to_string(w.okKlick));

    // W2. En fokuserad knapp tar sitt EGET Enter.
    w.avbryt.focus();
    tangent(w, K::Return);
    check(w.avbrytKlick == 1 && w.okKlick == 2,
          "W2: fokus på avbryt + Enter ⇒ avbryt klickas, ok inte",
          std::to_string(w.avbrytKlick) + "/" + std::to_string(w.okKlick));

    // W3. Det flerradiga fältet tar sin radbrytning; Ctrl+Enter är appens.
    w.yta.setText("");
    w.yta.focus();
    tangent(w, K::Return);
    check(w.yta.text() == "\n" && w.okKlick == 2,
          "W3: TextArea — Enter ger en ny rad, ok klickas inte", w.yta.text());
    tangent(w, K::Return, kCtrl);
    check(w.yta.text() == "\n" && w.okKlick == 2,
          "W3: TextArea — Ctrl+Enter: ingen ok och texten orörd",
          std::to_string(w.okKlick));
    tangent(w, K::Return, kShift);
    check(w.yta.text() == "\n\n" && w.okKlick == 2,
          "W3: TextArea — Shift+Enter ger en ny rad", w.yta.text());

    // W4. Escape.
    w.falt.focus();
    tangent(w, K::Escape);
    check(w.avbrytKlick == 2, "W4: Escape i ett fält ⇒ avbryt",
          std::to_string(w.avbrytKlick));
    w.egen.focus();
    tangent(w, K::Escape);
    check(w.egen.escape == 1 && w.avbrytKlick == 2,
          "W4: en fokuserad widget som TAR Escape vinner — ingen avbryt",
          std::to_string(w.avbrytKlick));
    w.falt.focus();
    tangent(w, K::Escape, kCtrl);
    tangent(w, K::Escape, kShift);
    check(w.avbrytKlick == 2,
          "W4: Ctrl+Escape och Shift+Escape ⇒ ingen avbryt (omodifierad "
          "Escape)",
          std::to_string(w.avbrytKlick));

    // W10. Talfältet och datumväljaren committar OCH bubblar.
    w.tal.setValue(5);
    w.tal.focus();
    tangent(w, K::A, kRedigera);
    skriv(w, "42");
    w.talVidKlick = -1.0;
    tangent(w, K::Return);
    check(w.tal.value() == 42.0 && w.okKlick == 3 && w.talVidKlick == 42.0,
          "W10: talfältets Enter committar 42 OCH klickar ok — ok:s handler "
          "ser det committade värdet",
          std::to_string(w.tal.value()) + " / " + std::to_string(w.okKlick) +
              " / " + std::to_string(w.talVidKlick));
    w.datum.focus();
    tangent(w, K::A, kRedigera);
    skriv(w, "2026-03-05");
    w.datumVidKlick.clear();
    tangent(w, K::Return);
    check(w.datum.dateText() == "2026-03-05" && w.okKlick == 4 &&
              w.datumVidKlick == "2026-03-05",
          "W10: datumväljarens Enter committar datumet OCH klickar ok",
          w.datum.dateText() + " / " + std::to_string(w.okKlick) + " / " +
              w.datumVidKlick);

    // W5. En oanvändbar acceptknapp tar ingen tangent.
    w.falt.focus();
    w.ok.hide();
    tangent(w, K::Return);
    check(w.okKlick == 4, "W5: acceptknappen DOLD ⇒ Enter gör ingenting",
          std::to_string(w.okKlick));
    w.ok.show();
    w.ok.disable();
    tangent(w, K::Return);
    check(w.okKlick == 4, "W5: acceptknappen INAKTIVERAD ⇒ Enter gör ingenting",
          std::to_string(w.okKlick));
    w.ok.enable();
    w.gomma.hide();
    w.setAcceptButton(w.gomdKnapp);
    tangent(w, K::Return);
    check(w.gomdKlick == 0 && w.okKlick == 4,
          "W5: acceptknappen i en DOLD Panel ⇒ Enter gör ingenting",
          std::to_string(w.gomdKlick));
    w.gomma.show();
    tangent(w, K::Return);
    check(w.gomdKlick == 1,
          "W5: (panelen synlig igen ⇒ knappen tar Enter — kontrollen ovan "
          "mätte synligheten)",
          std::to_string(w.gomdKlick));
    {
        Annat annat;
        w.setAcceptButton(annat.knapp);   // loggar: ett annat fönster
        tangent(w, K::Return);
        check(annat.klick == 0 && w.okKlick == 4,
              "W5: acceptknappen i ett ANNAT fönster ⇒ Enter gör ingenting",
              std::to_string(annat.klick));
        w.setAcceptButton(nullptr);
    }
    check(w.acceptButton() == nullptr,
          "W5: setAcceptButton(nullptr) nollställer");
    w.setAcceptButton(w.ok);

    // W6. En ägd acceptknapp som släpps och dör.
    {
        int   klick = 0;
        auto* agd   = w.root().add<glaze::Button>("Ägd");
        agd->onClick = [&klick] { ++klick; };
        w.setAcceptButton(*agd);
        check(w.acceptButton() == agd, "W6: den ägda knappen är acceptknappen");
        {
            std::unique_ptr<glaze::Widget> borta = w.root().releaseOwned(*agd);
            check(borta != nullptr, "W6: releaseOwned gav knappen");
        }   // knappen dör här
        check(w.acceptButton() == nullptr,
              "W6: den döda knappen — acceptButton() är nullptr");
        tangent(w, K::Return);
        check(klick == 0 && w.okKlick == 4,
              "W6: Enter efter knappens död gör ingenting (och kraschar inte)");
        w.setAcceptButton(w.ok);
    }

    // W7. Standardknappen — Win32-modellen — och invalideringen.
    w.falt.focus();
    check(w.isDefaultButton(w.ok) && !w.isDefaultButton(w.avbryt),
          "W7: fokus i ett fält ⇒ acceptknappen är standard");
    std::uint64_t s0 = w.ok.damageStamp();
    w.avbryt.focus();
    check(w.isDefaultButton(w.avbryt) && !w.isDefaultButton(w.ok),
          "W7: fokus på avbryt ⇒ avbryt är standard, ok inte");
    check(w.ok.damageStamp() > s0,
          "W7: ok ritas om när rollen går till avbryt (setFocus, ingen full "
          "omritning)",
          std::to_string(w.ok.damageStamp()) + " mot " + std::to_string(s0));
    s0 = w.ok.damageStamp();
    w.avbryt.hide();   // fokus står kvar på den dolda knappen
    check(w.isDefaultButton(w.ok),
          "W7: avbryt dold medan den har fokus ⇒ ok är standard igen");
    check(w.ok.damageStamp() > s0,
          "W7: …och ok ritas om (refreshDefaultButton)",
          std::to_string(w.ok.damageStamp()) + " mot " + std::to_string(s0));
    w.avbryt.show();
    w.setAcceptButton(nullptr);
    w.avbryt.focus();
    check(!w.isDefaultButton(w.avbryt) && !w.isDefaultButton(w.ok),
          "W7: ett fönster UTAN acceptknapp har ingen standardknapp — inte "
          "ens den fokuserade");
    w.setAcceptButton(w.ok);
    check(w.isDefaultButton(w.avbryt),
          "W7: (acceptknappen tillbaka ⇒ den fokuserade knappen är standard)");
}

// ---------------------------------------------------------------- R (del 1)

void fallRegressionerna(Fonster& w)
{
    std::printf("\n=== R. fyra kontroller och förfadern ===\n");

    const int ok0 = w.okKlick;
    // VAL 1 (a): kryssrutan och radioknappen växlar bara på Space.
    w.kryss.focus();
    tangent(w, K::Return);
    check(w.okKlick == ok0 + 1 && !w.kryss.isChecked(),
          "R: kryssrutan släpper Enter till ok och växlar inte",
          std::to_string(w.okKlick - ok0));
    tangent(w, K::Space);
    check(w.kryss.isChecked() && w.okKlick == ok0 + 1,
          "R: (Space växlar kryssrutan)");
    w.radio.focus();
    tangent(w, K::Return);
    check(w.okKlick == ok0 + 2 && !w.radio.isChecked(),
          "R: radioknappen släpper Enter till ok och väljs inte",
          std::to_string(w.okKlick - ok0));
    tangent(w, K::Space);
    check(w.radio.isChecked() && w.okKlick == ok0 + 2,
          "R: (Space väljer radioknappen)");
    // VAL 1 (b): den stängda dropdownen och färgväljaren öppnas inte av Enter.
    w.dd.focus();
    tangent(w, K::Return);
    check(w.okKlick == ok0 + 3 && !w.dd.isOpen(),
          "R: den STÄNGDA dropdownen släpper Enter till ok och öppnas inte",
          std::to_string(w.okKlick - ok0));
    w.farg.focus();
    tangent(w, K::Return);
    check(w.okKlick == ok0 + 4 && !w.farg.isOpen(),
          "R: den stängda färgväljaren släpper Enter till ok och öppnas inte",
          std::to_string(w.okKlick - ok0));

    // En FÖRFADER som tar Enter (`descendantKeyDown`) står närmare fokus.
    w.inre.focus();
    tangent(w, K::Return);
    check(w.ram.enter == 1 && w.okKlick == ok0 + 4,
          "R: en förfader som tar Enter i descendantKeyDown vinner över ok",
          std::to_string(w.ram.enter));
}

// ---------------------------------------------------------------- W8

void fallDialogerna(glaze::AppWindow& agare)
{
    std::printf("\n=== W8. run(ägare) ===\n");

    {
        Dialog d(true);
        const DialogResult r = kor(agare, d, [&] {
            d.falt.focus();
            tangent(d, K::Return);
        });
        check(r == DialogResult::OK && d.okKlick == 1,
              "W8: Enter ⇒ run svarar OK (knappens dialogResult)", namn(r));
    }
    {
        Dialog d(true);
        const DialogResult r = kor(agare, d, [&] {
            d.falt.focus();
            tangent(d, K::Escape);
        });
        check(r == DialogResult::Cancel && d.okKlick == 0,
              "W8: Escape ⇒ run svarar Cancel", namn(r));
    }
    {
        Dialog d(false);
        bool   stodKvar = false;
        const DialogResult r = kor(agare, d, [&] {
            d.falt.focus();
            tangent(d, K::Return);
            stodKvar = d.isOpen() && d.dialogResult() == DialogResult::None;
        });
        check(d.okKlick == 1 && stodKvar,
              "W8: ok UTAN dialogResult vars onClick inte stänger ⇒ dialogen "
              "står kvar");
        check(r == DialogResult::None,
              "W8: …och stängd utan resultat svarar run None", namn(r));
    }
    {
        // VETOT: closeWithResult(OK), ett veto, och sedan krysset.
        Dialog d(true);
        d.setOffscreen(true);
        int  vetoKvar = 1;
        bool steg2    = false;
        d.onClose = [&vetoKvar](glaze::Widget*, glaze::CloseArgs& e) {
            if (vetoKvar > 0) {
                --vetoKvar;
                e.cancel = true;
            }
        };
        glaze::app().post([&] {
            if (!d.isOpen()) {
                hoppa("vetots dialog kom aldrig upp");
                return;
            }
            d.falt.focus();
            tangent(d, K::Return);   // closeWithResult(OK) köas
            check(d.dialogResult() == DialogResult::OK,
                  "W8: Enter satte OK (före vetorundan i pump-svansen)",
                  namn(d.dialogResult()));
            glaze::app().post([&] {
                steg2 = true;
                check(d.isOpen() && d.dialogResult() == DialogResult::None,
                      "W8: vetot stoppade stängningen OCH nollställde "
                      "resultatet",
                      namn(d.dialogResult()));
                d.close();   // krysset — utan resultat
            });
        });
        const DialogResult r = d.run(agare);
        check(steg2 && r == DialogResult::None,
              "W8: ett kryss efter vetot ⇒ run svarar None, inte det vetade OK",
              namn(r));
    }
}

// ---------------------------------------------------------------- W9

void fallMeddelanderutan(glaze::AppWindow& agare)
{
    std::printf("\n=== W9. meddelanderutan ===\n");

    using glaze::MessageBoxButtons;
    const MessageBoxButtons uppsättningar[] = {
        MessageBoxButtons::OK,          MessageBoxButtons::OKCancel,
        MessageBoxButtons::YesNo,       MessageBoxButtons::YesNoCancel,
        MessageBoxButtons::RetryCancel, MessageBoxButtons::AbortRetryIgnore};
    int i = 0;
    for (const MessageBoxButtons u : uppsättningar) {
        const std::string vad = "W9[" + std::to_string(i++) + "]";
        const auto specar = glaze::msgbox::buttonSpecs(u);
        const bool avbrytVag = glaze::msgbox::hasCancelPath(u);
        bool       sedd      = false;
        glaze::app().post([&] {
            glaze::AppWindow* m = rutan(agare);
            if (!m) {
                check(false, vad + ": rutan hittades bland fönstren");
                return;
            }
            sedd = true;
            const std::vector<glaze::Button*> k = knapparI(*m);
            bool resultaten = k.size() == specar.size();
            for (std::size_t j = 0; resultaten && j < k.size(); ++j)
                resultaten = k[j]->dialogResult() == specar[j].result;
            check(resultaten,
                  vad + ": knapparna bär tabellens dialogResult i ordning",
                  std::to_string(k.size()));
            check(!k.empty() && m->acceptButton() == k.front(),
                  vad + ": standardknappen är den FÖRSTA knappen");
            glaze::Button* väntad = nullptr;
            if (avbrytVag)
                for (glaze::Button* b : k)
                    if (b->dialogResult() == glaze::msgbox::cancelEquivalent(u))
                        väntad = b;
            check(m->cancelButton() == väntad,
                  vad + (avbrytVag ? ": avbrytknappen är avbryt-ekvivalenten"
                                   : ": INGEN avbrytknapp (Escape gör "
                                     "ingenting)"));
            if (glaze::Button* c = m->cancelButton())
                c->performClick();
            else if (!k.empty())
                k.front()->performClick();
            m->close();   // fallnät — en köad stängning med resultat rörs inte
        });
        const DialogResult r = glaze::messageBox(
            agare, "dialog_gate", kRutansRubrik, u,
            glaze::MessageBoxType::Information);
        const DialogResult väntat = avbrytVag
                                        ? glaze::msgbox::cancelEquivalent(u)
                                        : specar.front().result;
        check(sedd && r == väntat,
              vad + ": messageBox svarar " + namn(väntat), namn(r));
    }
}

// ---------------------------------------------------------------- I1

struct InmatningProv : glaze::InputBoxWindow {
    using glaze::InputBoxWindow::InputBoxWindow;
    using glaze::AppWindow::handleEvent;

    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    void skriv(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, s, sizeof e.text - 1);
        handleEvent(e);
    }
};

const char* kFragansRubrik = "dialog_gate: fråga";

glaze::AppWindow* fragan(glaze::AppWindow& agare)
{
    for (glaze::AppWindow* x : glaze::app().windows())
        if (x != &agare && x->isOpen() && x->isModal() &&
            x->title() == kFragansRubrik)
            return x;
    return nullptr;
}

void fallInmatningsrutan(glaze::AppWindow& agare)
{
    std::printf("\n=== I1. inmatningsrutan ===\n");

    // Fönstret direkt: förvalet markerat, skriv över + Enter ⇒ texten.
    {
        InmatningProv w("Namn:", "Byt namn", "Lager 1");
        w.setOffscreen(true);
        bool sedd = false;
        glaze::app().post([&] {
            sedd       = true;
            auto* falt = w.find<glaze::TextInput>("field");
            check(falt && falt->focused() && falt->text() == "Lager 1" &&
                      falt->selBegin() == 0 && falt->selEnd() == 7,
                  "I1: förvalet står MARKERAT med fokus i fältet",
                  falt ? falt->text() : "");
            check(w.acceptButton() && w.acceptButton()->name == "ok" &&
                      w.cancelButton() && w.cancelButton()->name == "cancel",
                  "I1: OK är standardknappen och Avbryt avbrytknappen");
            w.skriv("Vägar");
            w.tangent(gfx::Key::Return);
        });
        w.run(agare);
        check(sedd && w.accepted() && w.text() == "Vägar",
              "I1: skriv över + Enter ⇒ OK med texten", w.text());
    }

    // FLERA RADER: fältet är ett TextArea, Enter är en ny rad (rutan
    // stängs inte) och OK trycks med knappen — svaret bär raderna.
    {
        InmatningProv w("Kommentar:", "Redigera", "a",
                        glaze::InputBoxMode::MultiLine);
        w.setOffscreen(true);
        bool sedd = false;
        bool öppen = false;
        glaze::app().post([&] {
            sedd       = true;
            auto* area = w.find<glaze::TextArea>("field");
            check(area && !w.find<glaze::TextInput>("field") && area->focused() &&
                      area->text() == "a" && area->selBegin() == 0 &&
                      area->selEnd() == 1,
                  "I1: flerradsläget har ett TextArea med fokus och förvalet "
                  "markerat",
                  area ? area->text() : "");
            w.skriv("b");                  // skriver över det markerade
            w.tangent(gfx::Key::Return);   // en ny rad, inte OK
            w.skriv("c");
            öppen = w.isOpen();
            if (glaze::Button* ok = w.acceptButton())
                ok->performClick();
        });
        w.run(agare);
        check(sedd && öppen && w.accepted() && w.text() == "b\nc",
              "I1: Enter ger en ny rad och OK med knappen svarar med raderna",
              w.text());
    }

    // Escape ⇒ inget svar.
    {
        InmatningProv w("Namn:", "Byt namn", "Lager 1");
        w.setOffscreen(true);
        glaze::app().post([&] { w.tangent(gfx::Key::Escape); });
        w.run(agare);
        check(!w.accepted() && w.dialogResult() == DialogResult::Cancel,
              "I1: Escape ⇒ Avbryt", namn(w.dialogResult()));
    }

    // Den fria funktionen: OK med en TOM text är ett svar, Avbryt inget.
    {
        bool sedd = false;
        glaze::app().post([&] {
            glaze::AppWindow* x = fragan(agare);
            sedd                = x != nullptr;
            if (!x)
                return;
            if (auto* f = x->find<glaze::TextInput>("field"))
                f->setText("");
            if (glaze::Button* ok = x->acceptButton())
                ok->performClick();
            x->close();   // fallnät — en köad stängning med resultat rörs inte
        });
        const std::optional<std::string> svar =
            glaze::inputBox(agare, "Fråga:", kFragansRubrik, "förval");
        check(sedd && svar.has_value() && svar->empty(),
              "I1: inputBox svarar med en TOM text vid OK — ett svar");
    }
    {
        bool sedd = false;
        glaze::app().post([&] {
            glaze::AppWindow* x = fragan(agare);
            sedd                = x != nullptr;
            if (!x)
                return;
            if (glaze::Button* c = x->cancelButton())
                c->performClick();
            x->close();
        });
        const std::optional<std::string> inget =
            glaze::inputBox(agare, "Fråga:", kFragansRubrik, "förval");
        check(sedd && !inget.has_value(), "I1: Avbryt ⇒ std::nullopt");
    }
}

// ---------------------------------------------------------------- PIXLAR

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
};

struct Medel {
    double r = 0.0, g = 0.0, b = 0.0;
};

// Medelfärgen i rektens INRE — fyra px in, så kanten, skuggan och en
// fokusring utanför aldrig räknas.
Medel medel(const Bild& bild, const gfx::Rectf& r)
{
    const int x0 = std::max(0, static_cast<int>(std::ceil(r.x)) + 4);
    const int y0 = std::max(0, static_cast<int>(std::ceil(r.y)) + 4);
    const int x1 = std::min(bild.w, static_cast<int>(r.x + r.w) - 4);
    const int y1 = std::min(bild.h, static_cast<int>(r.y + r.h) - 4);
    Medel  m;
    double n = 0.0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(bild.w) +
                 static_cast<std::size_t>(x)) * 4;
            m.r += bild.px[i];
            m.g += bild.px[i + 1];
            m.b += bild.px[i + 2];
            n += 1.0;
        }
    if (n > 0.0) {
        m.r /= n;
        m.g /= n;
        m.b /= n;
    }
    return m;
}

double avstand(const Medel& a, const Medel& b)
{
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g),
                     std::abs(a.b - b.b)});
}

std::string text(const Medel& m)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "(%.0f, %.0f, %.0f)", m.r, m.g, m.b);
    return buf;
}

// Två knappar med SAMMA text och storlek — bara rollen skiljer dem. Lägena
// är multiplar av 20 logiska px, hela fysiska px i skala 1, 1,25, 1,5 och
// 1,75, så de två ytorna rastreras lika.
struct Pixelfonster : glaze::AppWindow {
    glaze::Button ok{"Knapp"};
    glaze::Button avbryt{"Knapp"};
    Pixelfonster() : glaze::AppWindow("dialog_gate: pixlar", 340, 120)
    {
        ok.name     = "ok";
        ok.x        = 20;
        ok.y        = 40;
        ok.width    = 120;
        ok.height   = 40;
        avbryt.name   = "avbryt";
        avbryt.x      = 180;
        avbryt.y      = 40;
        avbryt.width  = 120;
        avbryt.height = 40;
        root().adopt(ok);
        root().adopt(avbryt);
        setAcceptButton(ok);
    }
};

void fallPixlar()
{
    std::printf("\n=== W11. pixlar ===\n");

    constexpr double kStor  = 12.0;   // rollerna SKILJER sig
    constexpr double kLiten = 4.0;    // samma roll ⇒ samma yta
    for (const glaze::UI ui :
         {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic}) {
        const std::string vad = ui == glaze::UI::Glaze   ? "Glaze"
                                : ui == glaze::UI::Slate ? "Slate"
                                                         : "Minimalistic";
        Pixelfonster p;
        p.setUI(ui);
        p.setOffscreen(true);
        p.show();
        if (!p.isOpen()) {
            hoppa("pixelfönstret kunde inte skapas (ingen display)");
            return;
        }
        const auto rendera = [&p](Bild& b) {
            p.invalidate();
            p.window().renderFrame();
            return p.window().captureFrame(b.px, b.w, b.h);
        };
        Bild fore;
        if (!rendera(fore)) {
            check(false, "W11 " + vad + ": captureFrame lyckades");
            continue;
        }
        const gfx::Rectf rOk = p.ok.resolve(p.ok.behaviorCtx());
        const gfx::Rectf rAv = p.avbryt.resolve(p.avbryt.behaviorCtx());
        const Medel okF = medel(fore, rOk);
        const Medel avF = medel(fore, rAv);
        check(avstand(okF, avF) > kStor,
              "W11 " + vad + ": acceptknappen ritas annorlunda än den andra",
              text(okF) + " mot " + text(avF));

        p.avbryt.focus();
        Bild efter;
        if (!rendera(efter)) {
            check(false, "W11 " + vad + ": captureFrame lyckades (efter)");
            continue;
        }
        const Medel okE = medel(efter, rOk);
        const Medel avE = medel(efter, rAv);
        check(avstand(okE, avE) > kStor && avstand(avE, okF) <= kLiten &&
                  avstand(okE, avF) <= kLiten,
              "W11 " + vad + ": fokus på den andra knappen — rollerna har "
                             "BYTTS (Win32-modellen)",
              "ok " + text(okE) + ", avbryt " + text(avE));
    }
}

// ============================================================ KEDJAN

void fallMedFonster()
{
    std::printf("\n=== med fönster ===\n");

    Fonster w;
    w.setOffscreen(true);
    Accel                  acc;
    Litet                  flytt("dialog_gate: flytt");
    Litet                  ordning("dialog_gate: ordning");
    glaze::AppWindow       mal("dialog_gate: mål", 200, 100);
    std::unique_ptr<Litet> riv;
    Trad                   trad;
    int                    rivKlick  = 0;
    bool                   ddOppnad  = false;
    bool                   fargOppen = false;
    bool                   datOppen  = false;
    bool                   menyOppen = false;
    bool                   flyttProvad = false;
    bool                   fargUtan  = false;   // P: popuperna utan fokus
    bool                   datUtan   = false;
    bool                   ddUtan    = false;
    bool                   gOppen    = false;   // G: egenskapslistans vallista
    glaze::Dropdown*       vallista  = nullptr;
    DialogResult           underKlick = DialogResult::Yes;   // vaktvärde
    bool                   kedjan     = false;
    int                    steg       = 0;

    std::function<void()> nasta;
    nasta = [&] {
        switch (steg++) {
        case 0:
            kedjan = true;
            if (!w.isOpen()) {
                hoppa("fönsterfallen hoppas över: fönstret kom aldrig upp");
                w.close();
                return;
            }
            fallTangenter(w);
            fallRegressionerna(w);
            // ESCAPE MED ÖPPEN DROPDOWN: listan stängs, dialogen inte.
            w.dd.focus();
            tangent(w, K::Space);
            ddOppnad = w.dd.isOpen();
            if (ddOppnad) {
                const int av0 = w.avbrytKlick;
                tangent(w, K::Escape);
                check(w.avbrytKlick == av0,
                      "R: Escape med ÖPPEN dropdown — avbryt klickas inte",
                      std::to_string(w.avbrytKlick - av0));
            } else {
                hoppa("dropdownens popup kom aldrig upp");
            }
            break;
        case 1:
            if (ddOppnad)
                check(!w.dd.isOpen() && w.isOpen(),
                      "R: …dropdownen är stängd och fönstret står kvar");
            // ESCAPE MED ÖPPEN FÄRGPOPUP.
            w.farg.focus();
            tangent(w, K::Space);
            fargOppen = w.farg.isOpen();
            if (fargOppen) {
                const int av0 = w.avbrytKlick;
                tangent(w, K::Escape);
                check(w.avbrytKlick == av0,
                      "R: Escape med ÖPPEN färgpopup — avbryt klickas inte");
            } else {
                hoppa("färgväljarens popup kom aldrig upp");
            }
            break;
        case 2:
            if (fargOppen)
                check(!w.farg.isOpen() && w.isOpen(),
                      "R: …färgpopupen är stängd och fönstret står kvar");
            // ESCAPE MED ÖPPEN DATUMPOPUP.
            w.datum.focus();
            tangent(w, K::F4);
            datOppen = w.datum.isOpen();
            if (datOppen) {
                const int av0 = w.avbrytKlick;
                tangent(w, K::Escape);
                check(w.avbrytKlick == av0,
                      "R: Escape med ÖPPEN datumpopup — avbryt klickas inte");
            } else {
                hoppa("datumväljarens popup kom aldrig upp");
            }
            break;
        case 3:
            if (datOppen)
                check(!w.datum.isOpen() && w.isOpen(),
                      "R: …datumpopupen är stängd och fönstret står kvar");
            // ACCELERATORERNA vinner, och en ÖPPEN MENY tar sitt Escape.
            acc.setOffscreen(true);
            acc.show();
            if (!acc.isOpen()) {
                hoppa("acceleratorfönstret kom aldrig upp");
                break;
            }
            acc.falt.focus();
            tangent(acc, K::Return);
            check(acc.kord == 1 && acc.okKlick == 0,
                  "R: en accelerator bunden till Enter vinner över ok",
                  std::to_string(acc.kord) + "/" + std::to_string(acc.okKlick));
            tangent(acc, K::Escape);
            check(acc.stoppad == 1 && acc.avbrytKlick == 0,
                  "R: en accelerator bunden till Escape vinner över avbryt",
                  std::to_string(acc.stoppad));
            acc.menuBar.openMenu(0);   // mäts i nästa steg (menu_gate:s takt)
            break;
        case 4:
            menyOppen = acc.isOpen() && acc.menuBar.openIndex() == 0;
            if (menyOppen) {
                tangent(acc, K::Escape);
                check(acc.stoppad == 1 && acc.avbrytKlick == 0,
                      "R: Escape med ÖPPEN meny — menyn tar den, varken "
                      "acceleratorn eller avbryt");
            } else {
                hoppa("menyn öppnades inte");
            }
            break;
        case 5:
            if (menyOppen)
                check(acc.menuBar.openIndex() < 0 && acc.isOpen(),
                      "R: …menyn är stängd och fönstret står kvar");
            if (acc.isOpen())
                acc.close();
            // EN onClick SOM RIVER FÖNSTRET — dispatchen läser inget dött.
            riv = std::make_unique<Litet>("dialog_gate: rivs");
            riv->ok.onClick = [&riv, &rivKlick] {
                ++rivKlick;
                riv.reset();
            };
            riv->setOffscreen(true);
            riv->show();
            if (riv->isOpen()) {
                riv->falt.focus();
                gfx::Event e;
                e.type     = gfx::EventType::KeyDown;
                e.windowId = riv->window().id();
                e.key      = K::Return;
                riv->handleEvent(e);   // ok:s onClick river fönstret
                check(!riv && rivKlick == 1,
                      "R: en onClick som RIVER fönstret — Enter-steget läste "
                      "inget dött efteråt");
            } else {
                hoppa("rivningsfönstret kom aldrig upp");
                riv.reset();
            }
            // EN onClick SOM FLYTTAR KNAPPEN till ett annat fönster: vakten
            // efter handlern ser det, och INGET fönster stängs med resultatet.
            flytt.ok.onClick = [&flytt, &mal] {
                flytt.root().transferChild(flytt.ok, mal.root());
            };
            flytt.setOffscreen(true);
            flytt.show();
            // HANDLERORDNINGEN: onClick körs FÖRE stängningen.
            ordning.ok.onClick = [&ordning, &underKlick] {
                underKlick = ordning.dialogResult();
            };
            ordning.setOffscreen(true);
            ordning.show();
            flyttProvad = flytt.isOpen() && ordning.isOpen();
            if (!flyttProvad) {
                hoppa("flytt- eller ordningsfönstret kom aldrig upp");
                break;
            }
            flytt.falt.focus();
            tangent(flytt, K::Return);
            check(flytt.ok.parent() == &mal.root() &&
                      flytt.dialogResult() == DialogResult::None &&
                      mal.dialogResult() == DialogResult::None,
                  "R: en onClick som FLYTTAR knappen — varken det gamla eller "
                  "det nya fönstret fick resultatet");
            ordning.falt.focus();
            tangent(ordning, K::Return);
            check(underKlick == DialogResult::None &&
                      ordning.dialogResult() == DialogResult::OK,
                  "R: onClick körs FÖRE stängningen (dialogResult() None i "
                  "handlern, OK efter)",
                  std::string(namn(underKlick)) + " → " +
                      namn(ordning.dialogResult()));
            break;
        case 6:
            if (flyttProvad) {
                check(flytt.isOpen(),
                      "R: …flyttens fönster står kvar (knappen stängde inte "
                      "fönstret den lämnat)");
                check(!ordning.isOpen(),
                      "R: …ordningens fönster stängdes med knappens OK");
            }
            if (flytt.isOpen())
                flytt.close();
            if (ordning.isOpen())
                ordning.close();
            fallDialogerna(w);
            fallMeddelanderutan(w);
            fallInmatningsrutan(w);
            break;
        case 7: {
            // TRÄDMUTATIONEN (T).
            std::printf("\n=== T. trädmutationen ===\n");
            trad.setOffscreen(true);
            trad.show();
            trad.knapp.focus();
            check(trad.isDefaultButton(trad.knapp),
                  "T: en fokuserad knapp i en ANNAN panel är standard");
            std::uint64_t s = trad.knapp.damageStamp();
            trad.p1.unadopt(trad.ok);
            check(trad.acceptButton() == &trad.ok &&
                      !trad.isDefaultButton(trad.knapp),
                  "T: acceptknappen lossad ur sitt träd — ingen standardknapp "
                  "(den sitter inte längre i fönstret)");
            check(trad.knapp.damageStamp() > s,
                  "T: …och knappen i den andra panelen ritas om",
                  std::to_string(trad.knapp.damageStamp()) + " mot " +
                      std::to_string(s));
            s = trad.knapp.damageStamp();
            trad.p1.adopt(trad.ok);
            check(trad.isDefaultButton(trad.knapp) &&
                      trad.knapp.damageStamp() > s,
                  "T: acceptknappen adopterad tillbaka — knappen är standard "
                  "igen och ritas om");
            trad.close();
            break;
        }
        case 8:
            // P. EN ÖPPEN POPUP UTAN FOKUS: en
            // handler öppnar väljaren och fokus står kvar någon annanstans.
            std::printf("\n=== P. en öppen popup utan fokus ===\n");
            w.kryss.focus();
            w.farg.open();
            fargUtan = w.farg.isOpen();
            if (fargUtan) {
                const int av0 = w.avbrytKlick;
                tangent(w, K::Escape);
                check(w.avbrytKlick == av0,
                      "P1: Escape med färgpopupen öppen och fokus på kryssrutan "
                      "— popupen får tangenten, avbryt klickas inte",
                      std::to_string(w.avbrytKlick - av0));
            } else {
                hoppa("färgväljarens popup kom aldrig upp (P1)");
            }
            break;
        case 9:
            if (fargUtan)
                check(!w.farg.isOpen() && w.isOpen(),
                      "P1: …färgpopupen stängdes och fönstret står kvar");
            w.kryss.focus();
            w.datum.open();
            datUtan = w.datum.isOpen();
            if (datUtan) {
                const int av0 = w.avbrytKlick;
                tangent(w, K::Escape);
                check(w.avbrytKlick == av0,
                      "P2: Escape med datumpopupen öppen utan fokus — tangenten "
                      "når popupen genom fältet (ombudet), avbryt klickas inte",
                      std::to_string(w.avbrytKlick - av0));
            } else {
                hoppa("datumväljarens popup kom aldrig upp (P2)");
            }
            break;
        case 10:
            if (datUtan)
                check(!w.datum.isOpen() && w.isOpen(),
                      "P2: …datumpopupen stängdes och fönstret står kvar");
            w.kryss.focus();
            w.dd.open();
            ddUtan = w.dd.isOpen();
            if (ddUtan) {
                const int ok0 = w.okKlick;
                tangent(w, K::Return);
                check(w.okKlick == ok0,
                      "P3: Enter med dropdownens lista öppen utan fokus — listan "
                      "tar den, ok klickas inte",
                      std::to_string(w.okKlick - ok0));
            } else {
                hoppa("dropdownens popup kom aldrig upp (P3)");
            }
            break;
        case 11:
            if (ddUtan)
                check(!w.dd.isOpen() && w.isOpen(),
                      "P3: …listan stängdes av sitt val och fönstret står kvar");
            // G. EGENSKAPSLISTANS VALLISTA: en
            // redigering — Enter och Escape avslutar den och når aldrig
            // dialogens knappar.
            std::printf("\n=== G. egenskapslistans vallista ===\n");
            vallista = gridensVallista(w);
            klick(w, värdecell(w, 0));
            gOppen = vallista && vallista->isVisible() && vallista->isOpen() &&
                     w.pg.activeRow() == 0;
            if (gOppen) {
                const int av0 = w.avbrytKlick;
                tangent(w, K::Escape);
                check(w.pg.activeRow() < 0 && !vallista->isVisible() &&
                          w.avbrytKlick == av0,
                      "G1: Escape i den ÖPPNA vallistan avslutar redigeringen i "
                      "ett steg — avbryt klickas inte",
                      "rad " + std::to_string(w.pg.activeRow()) + ", avbryt +" +
                          std::to_string(w.avbrytKlick - av0));
            } else {
                hoppa("egenskapslistans vallista öppnades inte (G1–G3)");
            }
            break;
        case 12:
            if (gOppen) {
                check(!vallista->isOpen() && w.isOpen(),
                      "G1: …listan är stängd och fönstret står kvar");
                // STÄNGD men kvar i cellen med fokus — läget efter ett Escape
                // som bara stänger listan.
                klick(w, värdecell(w, 0));
                vallista->close();
            }
            break;
        case 13:
            if (gOppen) {
                if (vallista->isVisible() && !vallista->isOpen() &&
                    vallista->focused()) {
                    const int ok0 = w.okKlick;
                    tangent(w, K::Return);
                    check(w.pg.activeRow() < 0 && w.okKlick == ok0,
                          "G2: Enter i den STÄNGDA vallistan avslutar "
                          "redigeringen — dialogens OK klickas inte",
                          "rad " + std::to_string(w.pg.activeRow()) + ", ok +" +
                              std::to_string(w.okKlick - ok0));
                } else {
                    hoppa("vallistan stod inte stängd i cellen (G2)");
                }
                klick(w, värdecell(w, 0));
                vallista->close();
            }
            break;
        case 14:
            if (gOppen) {
                if (vallista->isVisible() && !vallista->isOpen() &&
                    vallista->focused()) {
                    const int av0 = w.avbrytKlick;
                    tangent(w, K::Escape);
                    check(w.pg.activeRow() < 0 && w.avbrytKlick == av0,
                          "G3: Escape i den STÄNGDA vallistan avslutar "
                          "redigeringen — dialogens Avbryt klickas inte",
                          "rad " + std::to_string(w.pg.activeRow()) +
                              ", avbryt +" +
                              std::to_string(w.avbrytKlick - av0));
                } else {
                    hoppa("vallistan stod inte stängd i cellen (G3)");
                }
            }
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

    if (!kedjan)
        hoppa("fönsterkedjan kördes ALDRIG (pumpen startade inte)");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("dialog_gate — standard- och avbrytknappen\n");
    glaze::setLanguage("sv");

    fallKnappen();
    fallListorna();
    fallMedFonster();
    fallPixlar();

    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "DIALOG_GATE GRÖN" : "DIALOG_GATE FÄLLD",
                g_cases, g_fails, g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
