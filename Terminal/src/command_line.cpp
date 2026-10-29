// Kommandoraden — reglerna står i command_line.h.

#include "glaze_terminal/command_line.h"

#include <cstddef>

namespace glaze::term {

std::vector<std::string> splitCommandLine(std::string_view s)
{
    std::vector<std::string> ut;
    std::string              arg;
    bool                     iArg   = false;   // ett argument har börjat (också "")
    bool                     citat  = false;
    std::size_t              i      = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (!citat && (c == ' ' || c == '\t')) {
            if (iArg) {
                ut.push_back(std::move(arg));
                arg.clear();
                iArg = false;
            }
            ++i;
            continue;
        }
        iArg = true;
        if (c == '\\') {
            std::size_t n = 0;
            while (i < s.size() && s[i] == '\\') {
                ++n;
                ++i;
            }
            if (i < s.size() && s[i] == '"') {
                arg.append(n / 2, '\\');
                if (n % 2 == 1) {
                    arg += '"';   // udda: ett bokstavligt citattecken
                    ++i;
                }
                // jämnt: citattecknet hanteras av nästa varv
            } else {
                arg.append(n, '\\');
            }
            continue;
        }
        if (c == '"') {
            if (citat && i + 1 < s.size() && s[i + 1] == '"') {
                arg += '"';   // "" inne i citat — ett bokstavligt, läget står kvar
                i += 2;
                continue;
            }
            citat = !citat;
            ++i;
            continue;
        }
        arg += c;
        ++i;
    }
    if (iArg)
        ut.push_back(std::move(arg));
    return ut;
}

std::string quoteArgument(std::string_view a)
{
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string_view::npos)
        return std::string(a);
    std::string ut = "\"";
    std::size_t n  = 0;   // bakstreck i rad, ännu inte skrivna
    for (const char c : a) {
        if (c == '\\') {
            ++n;
            continue;
        }
        if (c == '"') {
            ut.append(2 * n + 1, '\\');   // dubblade + ett som skyddar citatet
            ut += '"';
        } else {
            ut.append(n, '\\');
            ut += c;
        }
        n = 0;
    }
    ut.append(2 * n, '\\');   // före slutcitatet dubblas de
    ut += '"';
    return ut;
}

} // namespace glaze::term
