#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <glaze/glaze.h>

#include "glaze_terminal/scheme.h"

// Terminal — EN INBYGGD TERMINAL (WinForms har ingen,
// VS Codes integrerade terminal och Windows Terminal är förebilderna). Den
// kör ett program — ett skal, `claude`, ett byggkommando — i OS:ets
// pseudoterminal och visar det med VÅR VT-emulering i Mono-fonten, temats
// färger, historik och markering. Sidopaketet `Terminal/` (glaze länkar det
// aldrig): appen länkar `glaze_terminal_ui` och inkluderar den här headern
// UTANFÖR GlazeBuilder-regionen, som `glaze_threads/threads.h`.
//
// MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) — inga tal och inga färger
// i terminal.cpp: måtten är befintliga Style-fält, färgerna schemats
// (scheme.h). RULLNINGEN är Panels (overlay-remsorna, hjulet); innehållet är
// DATA, inga barn.
//
// PROGRAMMET sätts före `start()` — som setter-fält och en nycklad
// miljötabell, så formen går i `.glazeui` (regel 8). `start()` hör hemma i
// `onLoad`, aldrig i konstruktorn. Tomt program = OS:ets skal.
//
// TANGENTBORDET: piltangenter, F-tangenter, Ctrl-kombinationer (Ctrl+C är
// avbrottet), Alt som Meta, Tab och Escape går till PROGRAMMET — före
// fönstrets acceleratorer, avbrytknapp och fokusvandring. Vägarna UT är
// Ctrl+Tab (flikkontrollen), musklick och appens egen `onKeyDown` med
// `handled` (fokusenheten frågas först). Kopiera/klistra in: Ctrl+Shift+C/V
// och Ctrl+Insert/Shift+Insert på Windows och Linux, Cmd+C/V på macOS.
// Tabellen står i Terminal/src/ui/keys.h.
//
// FÄRGERNA följer temat och läget DIREKT (också medan programmet kör), eller
// låses till ljust/mörkt (`setColorMode`) och ett annat schema
// (`setColorScheme`), med egna färger ovanpå (`setSchemeColor`).
//
// INGEN INBYGGD SNABBMENY: `onContextMenu` är appens, med `copy()`/`paste()`
// — paketet äger inga strängar.
//
// INTE I FÖRSTA VERSIONEN (skrivna gränser): omflödning vid
// storleksändring, IME-kandidatfönstrets plats, BEL (inget ljud, ingen
// blinkning), OSC 8-länkar, Sixel och bilder, DECKPAM för numpaden, den
// primära markeringen på Linux, och breddavvikelser mellan ConPTY:s egen
// tabell och vår.

namespace glaze {

enum class TerminalColorMode : std::uint8_t { Auto, Light, Dark };

class Terminal : public Panel {
public:
    Terminal();
    ~Terminal() override;   // kill() och trådarna joinade

    // ---- programmet — läses vid start()
    void               setProgram(std::string path);
    const std::string& program() const;
    void               setArguments(std::string commandLine);   // .NET:s ProcessStartInfo.Arguments
    const std::string& arguments() const;
    void               setWorkingDirectory(std::string dir);
    const std::string& workingDirectory() const;
    // En NYCKLAD tabell (MarkdownView `setImage`-prejudikatet: en adress och
    // inget index). `unset…` tar bort variabeln ur programmets miljö.
    void setEnvironmentVariable(const std::string& name, std::string value);
    void unsetEnvironmentVariable(const std::string& name);
    // Appens värde för variabeln — nullopt när appen inte satt den eller tog
    // bort den (processens eget värde syns inte här).
    std::optional<std::string> environmentVariable(const std::string& name) const;

    // false: `lastError()` säger varför (inget öppet fönster, ConPTY saknas,
    // programmet hittades inte …). En terminal som kör startar om: den gamla
    // processen dödas först.
    bool               start();
    const std::string& lastError() const;
    bool               isRunning() const;
    int                exitCode() const;   // −1 före slutet, eller okänd
    void               kill();
    void               write(std::string_view bytes);   // rått till programmets indata
    void               paste(std::string_view text);    // som en inklistring (bracketed paste)
    // UTDATA UTAN PROGRAM — bytes som om programmet skrivit dem: en app som
    // själv har VT-utdata (en loggström med färger från ett byggverktyg den
    // kör med Threads) visar den i terminalen utan en pty. Samma tolkning,
    // svar och invalidering som pty:ns utdata; svaren (DSR, DA) går till ett
    // program som kör, annars ingenstans.
    void               feedOutput(std::string_view vt);

    const std::string& title() const;   // OSC 0/2
    int                columns() const;
    int                rows() const;

    // ---- historik och markering
    void        setScrollback(int lines);   // < 0 ⇒ logg + no-op; förval 10 000
    int         scrollback() const;
    void        clear();                    // töm historiken (skärmen står kvar)
    void        scrollToBottom();
    bool        hasSelection() const;
    std::string selectedText() const;
    void        selectAll();
    void        clearSelection();
    void        copy();

    // ---- utseendet
    void              setColorMode(TerminalColorMode m);
    TerminalColorMode colorMode() const;
    void              setColorScheme(TerminalScheme s);
    TerminalScheme    colorScheme() const;
    // En egen färg för en roll i en variant — OVANPÅ schemat. `ThemeMode::
    // System` är ingen variant (loggas, ingenting händer).
    void setSchemeColor(ThemeMode variant, TerminalColor role, gfx::Color c);
    void clearSchemeColors();
    // Textstorleken i px eller mm; 0 = stilens. Världsenheter,
    // negativa och icke-ändliga mått loggas och ignoreras.
    void   setTextSize(Length size);
    Length textSize() const;
    // Alt som Meta (ESC + tecknet): förval PÅ på Windows och Linux, AV på
    // macOS, där Option skriver tecken (@ och € på ett svenskt tangentbord).
    void setAltAsMeta(bool on);
    bool altAsMeta() const;

    // Paletten nästa ritning använder — schemat, läget och de egna färgerna.
    // Utan öppet fönster: schemat i ramverkets förvalda tema.
    TerminalPalette palette() const;

    // ---- händelserna (regel 6: värdena läses ur sendern)
    Handler<EventArgs> onExit;          // programmet har avslutats — `exitCode()`
    Handler<EventArgs> onTitleChange;   // `title()`

    // setProp: program, arguments, workingDirectory, colorMode, colorScheme,
    // scrollback, altAsMeta, textSize (ett tal = px) — därefter Panels.
    bool setProp(const std::string& key, const std::string& value) override;
    bool wantsTextInput() const override { return true; }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    gfx::Sizef contentExtent(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    bool       focusable() const override { return true; }
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;
    bool       textInput(const char* utf8) override;
    void       caretBlink(const Ctx& c, bool on) override;
    void       focusGained() override;
    void       focusLost() override;
    void       mouseDown(const Ctx& c, float x, float y) override;
    void       mouseDrag(const Ctx& c, float x, float y) override;
    void       mouseMove(const Ctx& c, float x, float y) override;
    bool       mouseUp(const Ctx& c, float x, float y) override;
    bool       mouseCancel(gfx::MouseButton b) override;
    void       mouseDoubleClick(const Ctx& c, float x, float y) override;
    bool       middleDown(const Ctx& c, float x, float y) override;
    void       middleDrag(const Ctx& c, float x, float y) override;
    bool       middleUp(const Ctx& c, float x, float y) override;
    bool       mouseWheel(const Ctx& c, float x, float y, float wheelX,
                          float wheelY) override;
    gfx::PointerShape cursorAt(const Ctx& c, float x, float y) const override;

private:
    struct State;   // skärmen, sessionen, markeringen (terminal.cpp)
    struct Grid;    // cellens mått och rutnätets plats i en ritning

    Grid            grid(const Ctx& c) const;
    gfx::Rectf      rowRect(const Grid& g, int index, float sy) const;
    float           scrolledY(const Ctx& c) const;
    TerminalPalette paletteWith(const Theme::Variant& resolved) const;
    bool            consume();
    void            repost(std::uint64_t generation, void (Terminal::*step)(std::uint64_t));
    void            flushTitle();
    void            drain(std::uint64_t generation);
    void            sessionEnded(std::uint64_t generation);
    void            afterOutput(bool screenSwitched);
    void            syncFocusReport();
    void            followIfNeeded(const Ctx& c);
    void            noteUserScroll(const Ctx& c);
    bool            reportMouse(const Ctx& c, float x, float y, int button,
                                bool press, bool motion);
    void            invalidateCursor();

    std::unique_ptr<State> s_;
};

} // namespace glaze

namespace glaze::term::detail {
// ANKARETS REFERENS i DELAT läge (src/ui/terminal_anchor.cpp) — en no-op i
// libglaze_terminal_ui. Att den ANROPAS är hela poängen: referensen håller
// DLL:en kvar, och registreringen av "terminal" körs när den laddas.
void uiAnchor();
} // namespace glaze::term::detail
