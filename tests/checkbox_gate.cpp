// checkbox_gate — KRYSSRUTANS TRE LÄGEN.
//
// Kör:  ./build/tests/checkbox_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Headless: inga fönster, ingen pump. Det som mäts är TILLSTÅNDSKONTRAKTET
// mot HANDSKRIVNA förväntade värden (WinForms `ThreeState`/`CheckState`):
//
//   C1  med `threeState` går ANVÄNDARENS klick Unchecked → Checked →
//       Indeterminate → Unchecked — WinForms ordning, inte Qt:s
//   C2  utan `threeState` växlar klicket mellan tom och ikryssad, och ett
//       mellanläge appen satt blir TOMT (WinForms `Checked = !Checked`)
//   C3  `isChecked()` är sann för Checked OCH Indeterminate, falsk för
//       Unchecked
//   C4  `setChecked(true)` ur mellanläget ger Checked, `setChecked(false)`
//       Unchecked; `setThreeState` ändrar inte läget
//   C5  ANVÄNDARE mot PROGRAM: klicket fyrar `onChange` och `onCommit` en gång
//       var, settrarna aldrig; Space är klicket, Enter inte
//   C6  `setProp`: `threeState`, `checkState` med enumets gemena namn, ett
//       okänt namn ⇒ false och orört
//
// SABOTAGEN som ska fälla den: cykeln i Qt:s ordning (C1), `isChecked` som
// bara svarar för Checked (C3), `clicked` utan grenen för två lägen (C2:
// mellanläget går till Checked), `setProp` utan `checkState` (C6).
//
// VAD DEN INTE MÄTER: hur mellanläget SER UT (strecket och Minimalistics
// fyllning) — det ritas i alla tre UI och ögnas i demons galleri, och en
// pixelgrind för en glyf vore en referens skriven ur produktionen.

#include <cstdio>
#include <cstring>
#include <string>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
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

using glaze::CheckState;

const char* namn(CheckState s)
{
    switch (s) {
    case CheckState::Unchecked:     return "Unchecked";
    case CheckState::Checked:       return "Checked";
    case CheckState::Indeterminate: return "Indeterminate";
    }
    return "?";
}

// Användarens väg — `clicked()` (Space är samma väg, `keyDown`).
struct Prov : glaze::Checkbox {
    using glaze::Checkbox::clicked;
    using glaze::Checkbox::keyDown;
};

void fallTreLagesCykeln()
{
    Prov k;
    k.setThreeState(true);
    check(k.checkState() == CheckState::Unchecked, "C1: tom från början");
    k.clicked();
    check(k.checkState() == CheckState::Checked, "C1: klick 1 ⇒ Checked",
          namn(k.checkState()));
    k.clicked();
    check(k.checkState() == CheckState::Indeterminate,
          "C1: klick 2 ⇒ Indeterminate (WinForms ordning)",
          namn(k.checkState()));
    k.clicked();
    check(k.checkState() == CheckState::Unchecked, "C1: klick 3 ⇒ Unchecked",
          namn(k.checkState()));
}

void fallTvaLagen()
{
    Prov k;
    k.clicked();
    check(k.checkState() == CheckState::Checked, "C2: två lägen: tom ⇒ Checked");
    k.clicked();
    check(k.checkState() == CheckState::Unchecked,
          "C2: två lägen: Checked ⇒ Unchecked");
    k.setCheckState(CheckState::Indeterminate);
    check(k.checkState() == CheckState::Indeterminate,
          "C2: appen får sätta mellanläget utan threeState");
    k.clicked();
    check(k.checkState() == CheckState::Unchecked,
          "C2: ett klick i mellanläget utan threeState ⇒ Unchecked",
          namn(k.checkState()));
}

void fallIsChecked()
{
    glaze::Checkbox k;
    k.setCheckState(CheckState::Unchecked);
    check(!k.isChecked(), "C3: Unchecked ⇒ isChecked() falsk");
    k.setCheckState(CheckState::Checked);
    check(k.isChecked(), "C3: Checked ⇒ isChecked() sann");
    k.setCheckState(CheckState::Indeterminate);
    check(k.isChecked(), "C3: Indeterminate ⇒ isChecked() SANN (WinForms, Qt)");
}

void fallSetChecked()
{
    glaze::Checkbox k;
    k.setCheckState(CheckState::Indeterminate);
    k.setChecked(true);
    check(k.checkState() == CheckState::Checked,
          "C4: setChecked(true) ur mellanläget ⇒ Checked", namn(k.checkState()));
    k.setCheckState(CheckState::Indeterminate);
    k.setChecked(false);
    check(k.checkState() == CheckState::Unchecked,
          "C4: setChecked(false) ur mellanläget ⇒ Unchecked");
    k.setCheckState(CheckState::Indeterminate);
    k.setThreeState(true);
    k.setThreeState(false);
    check(k.checkState() == CheckState::Indeterminate && !k.isThreeState(),
          "C4: setThreeState ändrar inte läget");
}

void fallEvents()
{
    int  byten = 0, klara = 0;
    Prov k;
    k.setThreeState(true);
    k.onChange = [&byten] { ++byten; };
    k.onCommit = [&klara] { ++klara; };
    k.setCheckState(CheckState::Indeterminate);
    k.setChecked(true);
    k.setThreeState(false);
    check(byten == 0 && klara == 0, "C5: settrarna fyrar ALDRIG",
          std::to_string(byten));
    k.setThreeState(true);
    k.clicked();   // Checked ⇒ Indeterminate
    check(byten == 1 && klara == 1 &&
              k.checkState() == CheckState::Indeterminate,
          "C5: klicket fyrar onChange och onCommit en gång var",
          std::to_string(byten) + "/" + std::to_string(klara));
    check(k.keyDown(gfx::Key::Space, 0) &&
              k.checkState() == CheckState::Unchecked && byten == 2,
          "C5: Space är klicket (Indeterminate ⇒ Unchecked)");
    check(!k.keyDown(gfx::Key::Return, 0) && byten == 2,
          "C5: Enter växlar inte (standardknappens)");
}

void fallSetProp()
{
    glaze::Checkbox k;
    check(k.setProp("threeState", "true") && k.isThreeState(),
          "C6: setProp threeState");
    check(k.setProp("checkState", "indeterminate") &&
              k.checkState() == CheckState::Indeterminate,
          "C6: setProp checkState = indeterminate");
    check(k.setProp("checkState", "checked") &&
              k.checkState() == CheckState::Checked,
          "C6: setProp checkState = checked");
    check(!k.setProp("checkState", "halv") &&
              k.checkState() == CheckState::Checked,
          "C6: ett okänt namn ⇒ false, läget orört");
    check(k.setProp("checked", "false") &&
              k.checkState() == CheckState::Unchecked,
          "C6: den gamla nyckeln checked står kvar");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("checkbox_gate\n");
    fallTreLagesCykeln();
    fallTvaLagen();
    fallIsChecked();
    fallSetChecked();
    fallEvents();
    fallSetProp();

    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
