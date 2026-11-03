#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "glaze/print.h"
#include "glaze/strings.h"

#include "file_dialog_model.h"

// UTSKRIFTENS FILREGLER — INTERN och utan tillstånd: reglerna för vilken fil
// ett jobb skrivs till och när filvalet visas, som rena inline-funktioner så
// att tests/print_gate.cpp når dem utan skrivare. Flödet som använder dem bor
// i print.cpp.

namespace glaze::printout {

// FILEN som står kvar när användaren valt skrivare i utskriftsdialogen. En
// fysisk skrivare skriver till papper, så filen töms. En fil vald i vår
// dialog gällde förra skrivarvalet och frågas om på nytt; en fil som appen
// satt står kvar hos en skrivare som frågar efter fil.
inline std::string keptOutputFile(const std::string& file, bool fromDialog,
                                  bool promptsForFile)
{
    return promptsForFile && !fromDialog ? file : std::string();
}

// FILEN som gäller i ett `run`. Appens fil står kvar. En fil vald i vår dialog
// hör till en filskrivare och till ETT jobb: `spent` säger att ett jobb redan
// använt den.
inline std::string fileForRun(const std::string& file, bool fromDialog, bool spent,
                              bool promptsForFile)
{
    return !fromDialog || (promptsForFile && !spent) ? file : std::string();
}

// Visas vårt filval? Skrivaren frågar efter en fil, ingen är satt och appen har
// inte stängt av frågan.
inline bool needsFilePrompt(bool promptsForFile, const std::string& outputFile,
                            bool filePrompt)
{
    return promptsForFile && outputFile.empty() && filePrompt;
}

// Ett intervall som börjar efter dokumentets sista sida (antalet känt).
inline bool emptyRange(int pages, int fromPage)
{
    return pages > 0 && fromPage > pages;
}

// Ett namn utan tecknen ett filnamn inte får ha (Windows-mängden, den snävaste),
// utan blanktecken först och blanktecken och punkter sist.
inline std::string cleanFileName(const std::string& name)
{
    constexpr std::string_view förbjudna = "<>:\"/\\|?*";
    std::string                ut;
    for (const char ch : name)
        if (static_cast<unsigned char>(ch) >= 0x20 &&
            förbjudna.find(ch) == std::string_view::npos)
            ut += ch;
    const std::size_t först = ut.find_first_not_of(' ');
    if (först == std::string::npos)
        return std::string();
    const std::size_t sist = ut.find_last_not_of(" .");
    return sist == std::string::npos || sist < först ? std::string()
                                                     : ut.substr(först, sist - först + 1);
}

// FILVALETS FÖRSLAG: appens förslag, annars jobbets titel, annars ägarens, det
// första som är ett namn efter rensningen — och med skrivarens ändelse (".pdf")
// när namnet saknar en egen och ändelsen är känd.
inline std::string suggestedName(const std::string& suggested, const std::string& title,
                                 const std::string& ownerTitle, const std::string& ext)
{
    for (const std::string* kandidat : {&suggested, &title, &ownerTitle}) {
        const std::string namn = cleanFileName(*kandidat);
        if (!namn.empty())
            return filedlg::withExtension(namn, ext);
    }
    return std::string();
}

// Ändelsen som filtrets namn: ".pdf" ⇒ "PDF".
inline std::string extensionLabel(const std::string& ext)
{
    std::string ut = ext.empty() ? ext : ext.substr(1);
    for (char& ch : ut)
        if (ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');
    return ut;
}

// Jobbresultatet som utfall, och felbeskedet det ger (0 = inget).
inline PrintOutcome outcomeOf(gfx::PrintJobResult r)
{
    switch (r) {
    case gfx::PrintJobResult::Ok:         return PrintOutcome::Ok;
    case gfx::PrintJobResult::Cancelled:  return PrintOutcome::Cancelled;
    case gfx::PrintJobResult::FileFailed: return PrintOutcome::FileFailed;
    case gfx::PrintJobResult::Failed:     return PrintOutcome::Failed;
    }
    return PrintOutcome::Failed;
}

inline std::uint32_t errorTextId(gfx::PrintJobResult r)
{
    switch (r) {
    case gfx::PrintJobResult::FileFailed: return str::PrintErrorFile;
    case gfx::PrintJobResult::Failed:     return str::PrintErrorJob;
    case gfx::PrintJobResult::Ok:
    case gfx::PrintJobResult::Cancelled:  return 0;
    }
    return str::PrintErrorJob;
}

} // namespace glaze::printout
