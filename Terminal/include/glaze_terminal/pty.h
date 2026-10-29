#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// PSEUDOTERMINALEN — ett program i OS:ets egen pty:
// ConPTY på Windows (`CreatePseudoConsole`, uppslaget med GetProcAddress så
// att Windows före 1809 ger ett läsbart fel i stället för en programfil som
// inte startar), `posix_openpt` på macOS och Linux. Det här är OS-lagret
// UTAN trådar: varje anrop BLOCKERAR, och `Session` (session.h) har en tråd
// per kanal ovanpå — ConPTY kräver det, och POSIX tjänar på samma form.
//
// TRÅDKONTRAKTET: `start`, `resize`, `terminate`, `closeConsole` och
// `wakeAll` anropas från EN ägande tråd (UI-tråden i widgeten); `read`,
// `write` och `wait` från var sin egen tråd. `closeConsole` får ALDRIG
// anropas på läsartråden — ConPTY:s stängning väntar (före Windows 11 24H2)
// tills utdatan tömts, och det är läsaren som tömmer.

namespace glaze::term {

// EN miljöändring: ett värde SÄTTER variabeln, `nullopt` TAR BORT den.
struct EnvChange {
    std::string                name;
    std::optional<std::string> value;
};

struct StartInfo {
    // Tomt = OS:ets skal: `%ComSpec%` på Windows, `$SHELL` (annars `/bin/sh`)
    // på POSIX — utan inloggningsflagga. Utan katalog söks programmet i PATH.
    std::string program;
    // EN kommandorad (command_line.h): ordagrant på Windows, delad med
    // CommandLineToArgvW:s regler på POSIX.
    std::string arguments;
    // Tomt = processens aktuella katalog. En relativ katalog görs absolut
    // mot den.
    std::string workingDirectory;
    // Ändringar i processens miljö, i ordning (den sista vinner). Förvalet
    // lägger till `TERM=xterm-256color` och `COLORTERM=truecolor` om appen
    // inte nämnt dem — också en borttagning är att nämna dem.
    std::vector<EnvChange> environment;
    int columns = 80;
    int rows    = 24;
};

class Pty {
public:
    Pty();
    ~Pty();   // terminate(true), closeConsole och handtagen stängda
    Pty(const Pty&)            = delete;
    Pty& operator=(const Pty&) = delete;

    // Starta programmet. false = det startade inte; `error` säger varför
    // (programmet hittades inte, ConPTY saknas, exec misslyckades …).
    bool start(const StartInfo& info, std::string& error);

    // LÄSARTRÅDEN: blockerar tills något finns. > 0 byte, 0 = slut (barnets
    // sida stängd, eller `wakeAll`), < 0 ett fel.
    std::ptrdiff_t read(char* buf, std::size_t n);
    // SKRIVTRÅDEN: blockerar tills allt skrivits. false = stängd.
    bool write(const char* data, std::size_t n);
    // VÄNTARTRÅDEN: blockerar tills processen avslutats. Slutkoden — POSIX
    // `WEXITSTATUS`, eller 128 + signalen för en dödad process (skalens
    // konvention) — eller −1 när den är okänd (`wakeAll` före slutet, eller
    // SIGCHLD satt till SIG_IGN av appen: då står skälet i `notice`).
    int wait(std::string& notice);

    void resize(int columns, int rows);
    // Döda HELA processkedjan. Windows: jobbobjektet (barnen också);
    // POSIX: processgruppen — SIGHUP, eller SIGKILL med `force` — också
    // efter att huvudprocessen skördats (ett bakgrundsjobb lever i gruppen).
    void terminate(bool force);
    // Lever något i processkedjan? Windows: jobbets aktiva processer; POSIX:
    // `kill(-grupp, 0)`. Rivningens livstecken mellan SIGHUP och SIGKILL.
    bool alive() const;
    // Windows: `ClosePseudoConsole` — EN gång, aldrig på läsartråden (se
    // filhuvudet). POSIX: ingenting (läsaren får EIO när barnets sida
    // stängts).
    void closeConsole();
    // Väck `read`, `write` och `wait` för gott (rivningens säkring).
    void wakeAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace glaze::term
