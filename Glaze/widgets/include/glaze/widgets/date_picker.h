#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "glaze/widget.h"

// DatePicker — DATUMVÄLJAREN (wxDatePickerCtrl, WinForms
// DateTimePicker). Ett textfält
// för ett DATUM och en kalenderknapp bredvid som öppnar en POPUP med ett
// MÅNADSRUTNÄT. MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/date_picker.cpp med switch (c.ui) (hård regel 3); popupen och
// rutnätet bor HELT i cpp:n, som `Dropdown::ListWindow`.
//
// TALFÄLTETS KOMPOSIT, inte filväljarfältets:
//   * FÄLTET är en riktig `TextInput` (privat nästlad `DatePicker::Field`),
//     ägd och dockad Fill i kompositens vänstra del — caret, markering och
//     klippbord är fältets.
//   * KNAPPEN ÄR DATA och inte ett `Button`-barn (talfältets stegare): popupen
//     är INTE modal, tangenterna ska stanna i fältet medan den är öppen, och
//     en knapp som tog fokus vid klicket hade flyttat dem. Filväljarfältets
//     knapp är ett eget stopp just för att den öppnar en MODAL dialog.
//   * ETT TABBSTOPP (`focusesChildren`), fokus går vidare till fältet
//     (`focusProxy`), `disable()` når fältet och inredningen går inte att
//     lossa.
//
// VÄRDET är `std::chrono::year_month_day`, och `date()` svarar med det
// EFFEKTIVA: klampat till [minDate, maxDate] VID LÄSNING, så settrarnas
// ordning aldrig spelar roll (talfältets regel — `.glazeui` kör satserna i
// källordning). Det finns ALLTID ett datum: utan `setDate` är det DAGENS, som
// wx och WinForms. Åren är 1–9999, ISO-formens fyra siffror.
//
// TEXTFORMEN I FÄLTET är ISO 8601, `ÅÅÅÅ-MM-DD` — den filväljaren redan visar,
// och den enda som läses likadant på varje språk. ANVÄNDAREN ÄNDRAR DATUMET på
// två sätt, och båda fyrar `onChange` OCH `onCommit` (i den ordningen) när det
// effektiva datumet ändras — aldrig annars och aldrig vid programmatisk
// sättning. Som talfältet ändras datumet bara när användaren är KLAR:
//   * I FÄLTET, vid ENTER eller när FOKUS LÄMNAR datumväljaren (inte live):
//     texten tolkas, KLAMPAS och skrivs om till det som
//     gäller; det som inte tolkas (halvskrivet, `ÅÅÅÅ-02-30`) lämnar datumet
//     orört och texten skrivs tillbaka. Att ÖPPNA kalendern ur fältet (Ned,
//     F4, knappen) committar också — kalendern ska visa det man skrev. Enter
//     KONSUMERAS INTE (`TextInput`s kontrakt): i en dialog
//     trycker den standardknappen med datumet redan committat.
//     Fokusförlustens events POSTAS till pump-svansen (`focusLost` får aldrig
//     köra användarkod). Bara siffror och `-` tas emot. Ett klick som INTE
//     tar fokus — en `ToolButton`, ett menyval, en accelerator — committar
//     inte av sig självt: `date()` svarar med det förra datumet medan fältet
//     visar det skrivna, tills handlern anropar `AppWindow::commitEdits()`
//     (eller `commitEdit()` här).
//   * I POPUPEN: pilarna flyttar en MARKÖR dag för dag (upp/ner en vecka),
//     PageUp/PageDown en månad (Ctrl: ett år), Home/End till månadens första/
//     sista dag — och Enter eller ett tryck på en dag VÄLJER och stänger.
//     Escape och Tab stänger UTAN ändring. Markören är det PROVISORISKA,
//     dropdownlistans modell, och den går aldrig utanför [minDate, maxDate].
//     Dagens datum är markerat; dagar utanför gränserna ritas släckta och går
//     inte att välja.
//
// SNABBLÄDDRINGEN (Windows kalendervy): ett tryck på TITELN går ett
// läge UPP — från dagarna till årets tolv MÅNADER, därifrån till ÅREN (ett
// decennium per sida, med ett år före och ett efter) — och ett val i en
// översikt går ett läge NED, året till månaderna och månaden till dagarna.
// Först en DAG väljer datumet. I översikterna stegar pilarna en månad/ett år
// (upp/ner en rad), PageUp/PageDown och titelns pilar en sida (ett år, ett
// decennium; med Ctrl nästa lägets sida), Home/End till sidans första/sista
// och Enter går ned ett läge. Månader och år som inte rör [minDate, maxDate]
// är släckta, och markören stannar i dem som går att välja. Escape och Tab
// stänger i VARJE läge, och varje öppning börjar bland dagarna. Månaderna står
// med HELA namnen (ramverkets id 61–72, inga nya strängar): tre kolumner — ett
// kvartal per rad — ger dem plats i alla tre stilarna, och ett namn som ändå
// inte ryms kortas med ellips.
//
// UPP ETT LÄGE MED TANGENTBORDET är Ctrl+Upp (Cmd+Upp på Mac), och det är ett
// val med tre skäl: Upp flyttar redan markören INOM läget, och Ctrl är den här
// popupens "större steg" (Ctrl+PageUp är året) — samma mask, samma
// konvention; Cmd+Upp är macOS Finders "upp en nivå"; och Alt är upptaget —
// Alt+Ned öppnar kalendern, och Alt+Upp stänger en lista på Windows. Escape
// stänger i stället för att backa ett läge: översikterna är vyer i samma
// popup och inga nästlade popuper, ett Escape avbryter alltid, och vägen
// tillbaka ned utan ändring är Enter på den orörda markören.
//
// ÖPPNA med ett tryck på knappen eller med Ned, Alt+Ned eller F4 i fältet. Hur
// tangenterna därefter når popupen är ett PLATTFORMSDELTA som bor i fönstret
// (`AppWindow::setPopupTakesKeys`) — ägarfönstret skickar dem
// vidare så länge popupen är öppen, på Wayland får popupen dem själv.
//
// HJULET: det stängda fältet ändrar aldrig datumet med hjulet
// (dropdownens regel — hjulet bubblar), och i popupen BLÄDDRAR det en sida
// (månad, år, decennium): det är vyn som rullar, inte värdet som ändras.
//
// VECKAN börjar på den dag SPRÅKET säger (`glaze::str::FirstWeekday`, ett
// språkfaktum som decimaltecknet): måndag på svenska, norska och finska — och
// på engelska, ett skrivet val (ISO 8601, samma regel som fältets form;
// språket bär ingen region). Månads- och veckodagsnamnen är ramverkssträngar
// i alla fyra språken.
//
// HÅRD REGEL 8 — den PLATTA formen i `.glazeui` är tre heltal:
//     datum.setDate(2026, 10, 8);
//     datum.setMinDate(2026, 1, 1);
//     datum.setMaxDate(2026, 12, 31);
// `setDate(std::chrono::year_month_day)` är kodens form bredvid och ger exakt
// samma tillstånd. Ett datum som inte finns (`setDate(2026, 2, 30)`) LOGGAS
// och ändrar ingenting.
//
// KLOCKSLAGET (WinForms
// `DateTimePicker` med ett eget format, wx:s wxTimePickerCtrl i samma fält).
// `setShowTime(true)` ger fältet ISO 8601 med tiden, `ÅÅÅÅ-MM-DD HH:MM`
// (blanksteg, RFC 3339:s läsbara form — `T` tas också vid tolkningen), och
// tiden skrivs i fältet; kalendern väljer datumet och BEHÅLLER tiden. En text
// med bara datumet behåller också tiden, och ett klockslag som inte finns
// (`25:00`, `12:60`) lämnar värdet orört, som ett ogiltigt datum. VÄRDET är
// paret `date()` + `time()` — tiden som `std::chrono::minutes` sedan
// midnatt — och båda är klocklösa, som `year_month_day`: appen lägger ihop
// dem med sin klocka (`std::chrono::local_days{d.date()} + d.time()`). Ingen
// `local_time` i API:t: `std::chrono::local_t` saknas i äldre libc++. Utan
// `setTime` är tiden NU (lokal, hela minuter), som datumet är i dag —
// WinForms `Value = DateTime.Now`. Gränserna `minDate`/`maxDate` är DATUM;
// tiden är fri. `onChange` + `onCommit` fyras när datumet ELLER tiden
// ändrades. I `.glazeui`:
//     datum.setShowTime(true);   // setter-fält
//     datum.setTime(14, 30);     // två heltal, `setDate`s platta form
//
// INTE HÄR med flit: sekunder, ett klockslag i popupen (tiden skrivs i
// fältet), "inget datum" (wxDP_ALLOWNONE), veckonummer, flera månader i
// popupen och ett lokalt datumformat i fältet.

namespace glaze {

class DatePicker : public Widget {
public:
    // BÅDA out-of-line (date_picker.cpp): popupen är ofullständig här.
    DatePicker();
    ~DatePicker() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern är
    // den som INVALIDERAR. Programmatisk sättning fyrar ALDRIG onChange. Ett
    // datum som inte finns ⇒ `gfx::log` + orört.
    void setDate(int year, int month, int day);
    void setDate(std::chrono::year_month_day d);
    void setMinDate(int year, int month, int day);
    void setMinDate(std::chrono::year_month_day d);
    void setMaxDate(int year, int month, int day);
    void setMaxDate(std::chrono::year_month_day d);

    std::chrono::year_month_day date() const;   // det EFFEKTIVA (klampat)
    std::chrono::year_month_day minDate() const { return min_; }
    std::chrono::year_month_day maxDate() const { return max_; }

    // KLOCKSLAGET (se ovan). `setTime` utanför dygnet (timmen 0–23,
    // minuten 0–59) ⇒ `gfx::log` + orört. Programmatiskt: inget event.
    void setShowTime(bool on);
    bool showsTime() const { return showTime_; }
    void setTime(int hour, int minute);
    void setTime(std::chrono::minutes timeOfDay);
    std::chrono::minutes time() const { return time_; }
    // Tiden i textformen `HH:MM` (`dateText()`s syskon).
    std::string timeText() const;
    // Datumet i TEXTFORMEN — det effektiva, i ISO-form (`ÅÅÅÅ-MM-DD`): det
    // `setProp("date", …)` tar och fältet visar när texten skrivits om
    // (färgväljarens `colorText()`). Under redigering kan `text()` vara en
    // annan — användarens `2026-3-5`, eller ett datum utanför gränserna.
    std::string dateText() const;

    // Det fältet VISAR — under redigering användarens text, annars datumet i
    // ISO-form. Läsväg, ingen sättare: datumväljarens tillstånd är `date`.
    const std::string& text() const;

    // MARKERINGEN i fältet — vidare till det inre fältet, med
    // `TextInput`s kontrakt. `datum.onFocus = [this] { datum.selectAll(); };`
    void selectAll();
    void setSelection(int begin, int end);

    // Det PROVISORISKA datumet i en ÖPPEN popup — markören. Ett ogiltigt
    // datum (`!ok()`) när popupen är stängd: då finns ingen markör (dropdownens
    // `provisionalRow()`).
    std::chrono::year_month_day provisionalDate() const;

    // ANVÄNDAREN ändrade datumet — läs det ur sender (`date()`), regel 6.
    // `onCommit` fyras direkt efter för samma handling (se ovan).
    Handler<ChangeArgs> onChange;
    Handler<ChangeArgs> onCommit;

    // Enter utan tangenten (widget.h): den skrivna texten tolkas, klampas
    // och blir datumet NU — `AppWindow::commitEdits()` når den.
    void commitEdit() override;

    bool isOpen() const { return open_; }
    void open();    // no-op utan ett öppet fönster, och när den är inaktiv
    void close();   // uppskjuten, som varje popups

    // ETT tabbstopp, och fokus går vidare till fältet.
    bool    focusable() const override { return true; }
    bool    focusesChildren() const override { return true; }
    Widget* focusProxy() const override;

    // Fältet är INREDNING — kompositen håller en rå pekare till det.
    bool canReleaseChild(const Widget& child) const override;

    // setProp-egenskaper: date, minDate, maxDate — i ISO-form (`ÅÅÅÅ-MM-DD`)
    // — time (`HH:MM`) och showTime. Ett ogiltigt värde loggas och nyckeln
    // räknas ändå som känd.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    gfx::Rectf childArea(const Ctx& c) const override;   // fältets yta

    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    void enabledChanged() override;

    // BORTTAGNINGENS DENY-CHECK — dropdownens.
    bool hasOpenPopup() const override { return isOpen(); }

    // DE TVÅ ANVÄNDARVÄGARNA — de enda som fyrar `onChange`/`onCommit`
    // (talfältets `stepFromUser`/`textFromUser`): `dateFromUser` är popupens
    // val, `textFromUser` committar fältets text som den står (Enters väg;
    // fokusförlusten går samma väg men postar eventen). `textFromUser` svarar
    // om datumväljaren går att röra efteråt — öppningen efter commiten
    // frågar det (`Widget::firePair`).
    void dateFromUser(std::chrono::year_month_day d);
    bool textFromUser();

private:
    class Field;   // fältet — date_picker.cpp
    class Popup;   // popupfönstret och månadsvyn — date_picker.cpp

    // GEOMETRIN — EN sanning i widget-lokala LOGISKA px, som tar ytan `b`:
    // knappen längst till höger, hela höjden.
    gfx::Rectf buttonRect(const Ctx& c, const gfx::Rectf& b) const;
    bool       overButton(const Ctx& c, float mx, float my) const;

    void openAt(const Ctx& c);
    void popupClosed();
    void syncText();   // fältet := ISO(date())
    // COMMIT av fältets text — `postad` = fokus lämnade, eventen väntar på
    // pump-svansen. Båda svarar som `Widget::firePair`: false = en handler
    // rev eller flyttade datumväljaren.
    bool commitText(bool postad);
    bool fireChange();   // onChange, därefter onCommit

    Field*                       field_ = nullptr;   // ägt barn (add<> i ctorn)
    std::unique_ptr<Popup>       popup_;             // lat — vid första open
    std::chrono::year_month_day  value_;             // RÅTT — klampas vid läsning
    std::chrono::year_month_day  min_;
    std::chrono::year_month_day  max_;
    std::chrono::minutes         time_{};            // efter midnatt
    bool                         open_     = false;
    bool                         hover_    = false;  // pekaren över knappen
    bool                         showTime_ = false;
};

} // namespace glaze
