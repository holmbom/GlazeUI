#pragma once
#include <glaze/glaze.h>

#include "window_model.h"

// apply_props — modellens fältrader och metodanrop applicerade på en
// RIKTIG glaze::Widget. EN sanning, delad av GlazeBuilders preview och
// glaze-render.
//
// Det finns EN implementation, och `preview_gate` mäter den.
//
// Libbet **builder_preview** (apps/GlazeBuilder/CMakeLists.txt) är den
// tredje nivån mellan `builder_core` (ren std — vet inget om glaze) och
// apparna: här bor det som beror på BÅDA. `builder_core` förblir ren std.

namespace bld {

// En fältsättningsrad → widget. Basfälten sätts direkt ur den råa
// C++-uttryckstexten, `img` via parseImgCall → handtags-formen, övrigt
// via setProp (sträng avciterad, enum → sista segmentet gement).
// Okänd nyckel = OPAK: loggas + ritas inte (regel 4:s arvtagare) — raden
// round-trippas ordagrant vid spara.
//
// `origin` är bara loggprefixet ("preview" respektive "glaze-render") —
// det är diagnostik, ingen semantik.
void applyProp(glaze::Widget& w, const Prop& p, const char* origin);

// En METODANROPSSATS → widget. Samma kontrakt som
// applyProp: KÄNT metodnamn utförs, OKÄNT loggas, bevaras opakt och
// ignoreras vid ritning — ALDRIG fail-safe. En okänd metod är samma sak
// som ett okänt fält (regel 4).
//
// Kända anrop i dag: `show()`/`hide()`/`enable()`/`disable()`/
// `resetCursor()` (alla widgets), `setIndeterminate(bool)` (Progress),
// `showScrollbars(bool)`
// (Panel), menyradens platta form (`addMenu`/`addItem`/`addCheck`/
// `addRadio`/`addSeparator`/`addSubmenu`) och KOLLEKTIONSVOKABULÄREN:
// `setRows`/`addRow`/`setRow`
// (PropertyGrid), `addItem`/`setItem` (ListBox · Dropdown),
// `setChecked(int,bool)` (ListBox) och `setColor(gfx::Color{…})` (Label).
//
// De två literalparsrarna (`setRows`-raderna och `gfx::Color`) är
// STRIKTA delmängder i samma familj som `parseStringList`/`parseImgCall`,
// och avkodningen är ATOMISK: allt valideras till temporärer innan
// widgeten muteras, annars logg + oförändrat tillstånd. Menygrenens lena
// "tom sträng och ändå framgång" är INGET prejudikat — den skapar ett
// PÅHITTAT tillstånd vid parsfel och är bevarad bara för att den redan
// mäts.
//
// Returnerar true om anropet kändes igen och utfördes.
bool applyCall(glaze::Widget& w, const Call& call, const char* origin);

// ETT MÅTT ur en rad — `glaze::px(…)`, `glaze::mm(…)`, `glaze::world(…)`
// eller ett naket tal (px): previewns EN tolkning, den ritytans former och
// textstorleken går genom. Egenskapspanelen frågar den innan en
// `textSize`-rad skrivs, så `.glazeui` aldrig bär ett mått som inte
// kompilerar.
bool parseLength(const std::string& v, glaze::Length& ut);

// Ett NAKET FÖNSTERANROP → fönstret: `setContentSize`,
// `setMinimumSize`/`setMaximumSize`, `setControlBox`/`setMinimizeBox`/
// `setMaximizeBox`, `setModal`/`setPopup`/`setTransparent`/`setTopmost`/
// `setShowInTaskbar` — plus `setTitle`/`setClientSize`, ramen (`setChrome`),
// startplatsen (`setStartPosition`, `setLocation`), startläget
// (`setWindowState`) och väntepekaren (`setUseWaitCursor`); enum-argumenten
// i en STRIKT tabell, kvalificerade eller bara. Dessutom gränserna och
// den låsta storleken i innehållsmått (`setMinimumContentSize`/
// `setMaximumContentSize`/`setFixedContentSize`, två heltal) och
// `setPopupTakesKeys` (en bool).
//
// Samma kontrakt som `applyCall`: KÄNT namn utförs, OKÄNT loggas och
// bevaras opakt — ALDRIG fail-safe. Anropen körs i KÄLLORDNING, för
// ordningen bär semantik ("den som sätts sist gäller").
//
// Returnerar true om anropet kändes igen och utfördes.
bool applyWindowCall(glaze::AppWindow& w, const Call& call,
                     const char* origin);

// EN SATS ur satslistan → widget. Fält och anrop
// dispatchas till de två ovan; allt annat (adopt, events, fönsteranrop)
// ignoreras här — de hör till trädbygget respektive fönstret.
//
// Formen finns för att BÅDA trädbyggarna (GlazeBuilders preview och
// glaze-render) ska köra satserna i KÄLLORDNING utan att var för sig
// veta vilka satstyper som är widget-riktade.
void applyStmt(glaze::Widget& w, const Stmt& s, const char* origin);

// EN SATS → en ICKE-VISUELL MEDLEM: `glaze::Timer`,
// `glaze::TrayIcon` eller en egen `glaze::Component`. Samma kontrakt som
// widgetvägen — känt utförs, okänt loggas och bevaras opakt, ALDRIG
// fail-safe — med EN skillnad: det som hade gjort något UTANFÖR ritbordet
// är KÄNT och utförs INTE. Timerns `start()`/`stop()` (ingen timer tickar i
// en preview) och ikonens `show()`/`hide()`/`openMenu()`/`closeMenu()`
// (ingen ikon i OS:ets fält). Allt som bara LAGRAR utförs: settrarna,
// animationens bilder (`addFrame`/`setFrame`/`setFrames`, `startAnimation`
// som begäran) och menyn med `{}`-roten.
//
// Returnerar true för en KÄND sats — utförd, eller medvetet inte utförd
// (också en event-koppling, som aldrig görs i previewn). false = opak
// (loggad) eller en satstyp som inte hör till en medlem.
bool applyComponentStmt(glaze::Component& c, const Stmt& s,
                        const char* origin);

} // namespace bld
