// Backend-neutral textlogik: radbrytning på ordgränser ovanpå
// Font::fitWidth. Delad av alla backends (samma brytpunkter per
// konstruktion) — samma delningsmönster som klipp-stacken i renderer.cpp.

#include <string>

#include "gfx/text.h"

namespace gfx {
namespace {

// Nästa UTF-8-teckengräns efter i (fortsättningsbytes är 10xxxxxx).
std::size_t nextBoundary(const std::string& s, std::size_t i)
{
    if (i >= s.size())
        return s.size();
    ++i;
    while (i < s.size() &&
           (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        ++i;
    return i;
}

} // namespace

std::vector<WrappedLine> wrapText(const Font& f, const std::string& utf8,
                                  float maxW)
{
    std::vector<WrappedLine> lines;

    std::size_t pos = 0;
    while (true) {
        // Segment = text fram till nästa '\n' (som alltid bryter).
        std::size_t segEnd = utf8.find('\n', pos);
        const bool  hard   = segEnd != std::string::npos;
        if (!hard)
            segEnd = utf8.size();

        if (pos == segEnd) {
            lines.push_back(WrappedLine{pos, pos});   // tom rad
        } else {
            std::size_t s = pos;
            while (s < segEnd) {
                const std::string seg = utf8.substr(s, segEnd - s);
                std::size_t fit = f.fitWidth(seg, maxW);
                if (fit >= seg.size()) {
                    lines.push_back(WrappedLine{s, segEnd});   // resten ryms
                    break;
                }
                if (fit == 0)
                    fit = nextBoundary(seg, 0);   // minst ett tecken per rad

                // Backa till senaste ordgränsen inom det som ryms; utan
                // mellanslag hårdbryts ordet vid fit.
                const std::size_t brk = seg.rfind(' ', fit);
                if (brk == std::string::npos || brk == 0) {
                    lines.push_back(WrappedLine{s, s + fit});
                    s += fit;
                    // hoppa över ev. mellanslag direkt efter hårdbrytet
                    while (s < segEnd && utf8[s] == ' ')
                        ++s;
                } else {
                    lines.push_back(WrappedLine{s, s + brk});
                    s += brk + 1;   // radbrytande mellanslag ingår inte
                }
            }
        }

        if (!hard)
            break;
        pos = segEnd + 1;
        if (pos > utf8.size())
            break;
        if (pos == utf8.size()) {
            lines.push_back(WrappedLine{pos, pos});   // '\n' sist → tom rad
            break;
        }
    }
    return lines;
}

} // namespace gfx
