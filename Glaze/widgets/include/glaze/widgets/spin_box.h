#pragma once
#include "glaze/widget.h"

// SpinBox — TALFÄLTET (wxSpinCtrl/wxSpinCtrlDouble,
// WinForms NumericUpDown). Ett textfält för ett TAL och en stegare med två
// halvor (upp/ner) bredvid. MÖNSTER: widgeten ritar SIG SJÄLV —
// draw()/contentSize() bor i src/widgets/spin_box.cpp med switch (c.ui)
// (hård regel 3).
//
// DEN FÖRSTA RIKTIGA KOMPOSITEN — och den prövade kompositstödet:
//
//   * FÄLTET ÄR EN RIKTIG `TextInput` (en privat, nästlad subklass, som
//     `Dropdown::ListWindow`), ägd och dockad Fill i kompositens vänstra del.
//     Caret, markering, klippbord, UTF-8 och scroll är fältets — ingenting
//     skrivs om. Klick i fältet ger caret direkt (klickfokus ändras inte).
//   * STEGAREN ÄR DATA och inte två `Button`-barn — prejudikatet från
//     ListBox/MenuBar: en `Button` är ett tabbstopp och tar fokus vid klick,
//     så ett klick på "upp" hade flyttat caret ut ur fältet. Halvorna ritas
//     och träffas av kompositen själv.
//   * ETT TABBSTOPP: `focusesChildren()` gör kompositen till EN station, och
//     FOKUSOMBUDET (`focusProxy()` → fältet) gör att fokus som landar på
//     kompositen — Tab, klick på stegaren, `focus()` — hamnar i fältet. De
//     två hålen talfältet visade i kompositstödet (fokus stannade i
//     kompositen, och Tab inifrån fältet hoppade till fönstrets första stopp)
//     är stängda i `Widget`/`AppWindow`, inte runt omkring här.
//   * `enabled` FÖLJER MED in: `disable()` på talfältet släcker fältet också
//     (`enabledChanged()`), annars gick ett inaktivt talfält att skriva i.
//   * Inredningen kan inte lossas utifrån (`canReleaseChild`).
//
// VÄRDET: `double`, som wxSpinCtrlDouble — `float` räcker inte för ett tal
// man SKRIVER (sju siffror). `value()` svarar alltid med det EFFEKTIVA
// värdet: klampat till [min, max] och avrundat till `decimals` — beräknat
// vid LÄSNING, så settrarnas ordning aldrig spelar roll (`setValue(150);
// setMax(200);` ger 150 i vilken ordning raderna än står, vilket `.glazeui`
// kräver: satserna körs i källordning).
//
// ANVÄNDAREN ÄNDRAR VÄRDET på två sätt, och båda fyrar `onChange` OCH
// `onCommit` (i den ordningen) när det EFFEKTIVA värdet ändras — aldrig
// annars, och aldrig vid programmatisk sättning (kontraktet för varje
// widget). Talfältet är NumberBox/NumericUpDown-modellen: värdet ändras
// bara när användaren är KLAR, så de två eventen kommer alltid tillsammans.
//   * STEGAREN (klick), PILTANGENTERNA i fältet och HJULET — ett `step` per
//     steg, klampat. En halvskriven text som går att tolka blir först
//     värdet steget utgår från (NumericUpDown gör likadant); en som inte går
//     att tolka ("-") ignoreras. HJULET KRÄVER FOKUS: det stegar bara när
//     fältet har fokus. Utan fokus svarar
//     `mouseWheel` false och hjulet BUBBLAR till närmaste rullande
//     förälder, så en sida som rullas förbi talfältet aldrig ändrar värdet
//     av misstag. Samma regel som `Slider`, den andra värdewidgeten.
//   * TEXTEN — vid ENTER eller när FOKUS LÄMNAR talfältet (inte live). Texten tolkas, KLAMPAS
//     ("5" + Tab med min 10 ⇒ 10) och skrivs om till värdet som gäller; det
//     som inte går att tolka (tomt, "-") lämnar värdet orört och texten
//     skrivs tillbaka. Medan man skriver är texten användarens och `value()`
//     det förra värdet — inga mellanvärden (1, 12, 120) och ingen klampning
//     mitt i ett tal. Fokusförlustens events POSTAS till pump-svansen
//     (`Widget::postEvent`), för `focusLost` får aldrig köra användarkod;
//     värdet och texten skrivs ändå direkt. Enter KONSUMERAS INTE —
//     `TextInput`s kontrakt: tangenten bubblar vidare
//     med talet redan committat, så Enter i en dialog trycker standardknappen
//     (`AppWindow::setAcceptButton`) med det committade värdet.
//     Ett klick som INTE tar fokus — en `ToolButton`, ett menyval, en
//     accelerator — committar alltså inte av sig självt: fokus står kvar i
//     fältet, och `value()` svarar med det förra värdet medan `text()` visar
//     det skrivna. Handlern som behöver värdet anropar
//     `AppWindow::commitEdits()` (eller `commitEdit()` här) först.
//
// FOKUS OCH MARKERING: talfältet får `onFocus`/`onFocusLost` när fokus går in
// i respektive lämnar det (widget.h — fokus INOM), och `selectAll`/
// `setSelection` når fältet: `hojd.onFocus = [this] { hojd.selectAll(); };`.
//
// DECIMALTECKNET följer SPRÅKET (`glaze::str::DecimalSeparator`: "," på
// svenska, norska och finska, "." på engelska) — och ett språkbyte skriver
// om ett fält som inte redigeras, genom kroken `languageChanged()` och inte
// i `draw()` (ritningen ändrar inget). TOLKNINGEN är generösare: både "," och
// "." godtas, men bara ETT av dem (tusentalsgrupper tolkas inte).
//
// INTE HÄR med flit: upprepning medan man HÅLLER stegaren (widgets har ingen
// egen timer — det är en golvfråga), hexadecimalt läge och tusentalsgrupper.

namespace glaze {

class SpinBox : public Widget {
public:
    SpinBox();

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar ALDRIG onChange.
    void setValue(double v);
    void setMin(double v);
    void setMax(double v);
    void setStep(double v);       // <= 0 ⇒ minsta visade steget (10^−decimals)
    void setDecimals(int d);      // klampas till 0–9

    double value() const;         // det EFFEKTIVA värdet (klampat, avrundat)
    double min() const { return min_; }
    double max() const { return max_; }
    double step() const { return step_; }
    int    decimals() const { return decimals_; }

    // Det fältet VISAR just nu — under redigering användarens text, annars
    // värdet formaterat med språkets decimaltecken. Läsväg, ingen sättare:
    // talfältets tillstånd är `value`.
    const std::string& text() const;

    // MARKERINGEN i fältet — vidare till det inre fältet, med
    // `TextInput`s kontrakt (byte i `text()`, klampat och snappat).
    void selectAll();
    void setSelection(int begin, int end);

    // Avfyras när ANVÄNDAREN ändrat det effektiva värdet — läs det ur sender
    // (`value()`), regel 6. `onCommit` fyras direkt efter, för samma handling
    // (se ovan): ett talfält ändrar sitt värde bara när användaren är klar.
    Handler<ChangeArgs> onChange;
    Handler<ChangeArgs> onCommit;

    // Enter utan tangenten (widget.h): den skrivna texten tolkas, klampas
    // och blir värdet NU — `AppWindow::commitEdits()` når den.
    void commitEdit() override;

    // ETT tabbstopp, och fokus går vidare till fältet.
    bool    focusable() const override { return true; }
    bool    focusesChildren() const override { return true; }
    Widget* focusProxy() const override;

    // Fältet är INREDNING — kompositen håller en rå pekare till det.
    bool canReleaseChild(const Widget& child) const override;

    // setProp-egenskaper: value, min, max, step, decimals.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    gfx::Rectf childArea(const Ctx& c) const override;   // fältets yta

    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;
    void enabledChanged() override;
    void languageChanged() override;   // decimaltecknet i ett vilande fält

    // DE TVÅ ANVÄNDARVÄGARNA — de enda som fyrar `onChange`/`onCommit`
    // (ListBoxens `selectFromUser`-prejudikat; protected så en subklass, och
    // en grind, når dem). `stepFromUser(n)` tar n steg (negativt = nedåt);
    // `textFromUser()` committar fältets text som den står — Enters väg, med
    // eventen direkt (fokusförlusten går samma väg men postar dem). Båda
    // svarar som `Widget::firePair`: false = en handler rev eller flyttade
    // talfältet, och det får inte röras igen.
    bool stepFromUser(int n);
    bool textFromUser();

private:
    class Field;                  // fältet — definierat i spin_box.cpp
    enum class Part { None, Up, Down };

    // GEOMETRIN — EN sanning i widget-lokala LOGISKA px, som tar ytan `b`:
    // ritningen skickar `c.bounds()`, beteendet `Draw::bounds(c, resolve(c))`.
    gfx::Rectf stepperRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf partRect(const Ctx& c, const gfx::Rectf& b, Part p) const;
    Part       partAt(const Ctx& c, float mx, float my) const;

    double effective(double v) const;          // klampa + avrunda
    double effectiveStep() const;
    std::string format(double v) const;        // med språkets decimaltecken
    void        syncText();                    // fältet := format(value())
    // COMMIT av fältets text — `postad` = fokus lämnade, eventen väntar på
    // pump-svansen. Svarar som `stepFromUser`.
    bool        commitText(bool postad);
    bool        fireChange();                  // onChange, därefter onCommit

    Field*      field_ = nullptr;   // ägt barn (add<> i ctorn)
    double      value_ = 0.0;
    double      min_   = 0.0;
    double      max_   = 100.0;
    double      step_  = 1.0;
    int         decimals_ = 0;
    Part        hover_ = Part::None;
    Part        press_ = Part::None;
    float       wheelRest_ = 0.0f;   // hjulets bråkdel tills ett helt steg
    TrText      sep_;                // decimaltecknet — löser upp sig själv
};

} // namespace glaze
