#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "glaze_terminal/pty.h"

// INTERN — det OS-lagren delar (Networkings `sys.h`-form): Pty::Impl
// definieras per OS i pty_win.cpp respektive pty_posix.cpp, och MILJÖN räknas
// här, en gång. Ren std och utan tillstånd (dll_gate).

namespace glaze::term::detail {

using EnvList = std::vector<std::pair<std::string, std::string>>;

// Processens miljö med appens ändringar, och förvalen `TERM=xterm-256color`
// och `COLORTERM=truecolor` där appen inte nämnt dem (appen vinner
// alltid, också med en borttagning). `same` jämför två namn: skiftlägesokänsligt
// på Windows, exakt på POSIX.
template <class Same>
EnvList mergeEnvironment(EnvList base, const std::vector<EnvChange>& changes, Same same)
{
    auto find = [&](const std::string& namn) {
        return std::find_if(base.begin(), base.end(),
                            [&](const auto& p) { return same(p.first, namn); });
    };
    auto nämnd = [&](const char* namn) {
        return std::any_of(changes.begin(), changes.end(),
                           [&](const EnvChange& c) { return same(c.name, namn); });
    };
    for (const auto& [namn, värde] : {std::pair{"TERM", "xterm-256color"},
                                      std::pair{"COLORTERM", "truecolor"}})
        if (!nämnd(namn)) {
            const auto it = find(namn);
            if (it != base.end())
                it->second = värde;
            else
                base.emplace_back(namn, värde);
        }
    for (const EnvChange& c : changes) {
        const auto it = find(c.name);
        if (!c.value) {
            if (it != base.end())
                base.erase(it);
        } else if (it != base.end()) {
            it->second = *c.value;
        } else {
            base.emplace_back(c.name, *c.value);
        }
    }
    return base;
}

} // namespace glaze::term::detail
