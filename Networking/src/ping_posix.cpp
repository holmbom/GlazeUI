// glaze_net — PING på LINUX och MACOS: en ICMP-DATAGRAMSOCKET
// (SOCK_DGRAM + IPPROTO_ICMP/ICMPV6) — aldrig en rå socket, som hade krävt
// root. Kontraktet står i ping.h. macOS-delen följer Apples SimplePing-exempel
// och Darwins headers.
//
// DE TVÅ KÄRNORNA BETER SIG OLIKA, och koden tål båda utan att fråga vilken
// den kör på:
//
//                       Linux (net/ipv4/ping.c)      macOS (SimplePing)
//   identifierare       kärnan skriver över den      vår egen
//   IPv4-kontrollsumma  kärnan räknar                VI måste räkna
//   IPv4-svaret         börjar på ICMP-huvudet       börjar på IP-HUVUDET
//   IPv6                kärnan räknar summan, inget IP-huvud — båda
//
// Därför: summan räknas alltid (Linux skriver över den), identifieraren
// matchas aldrig (Linux har bytt den), och ett IPv4-huvud i svaret känns
// igen på versionsnibbeln (4) och hoppas över. Svaret matchas på
// sekvensnumret PLUS en slumpad nyttolast — en annan process ping får
// aldrig räknas som vår.
//
// ICMP-FEL (destination unreachable) läses inte ut: Linux levererar dem
// bara via IP_RECVERR-kön, och ett fel som syns på den ena plattformen men
// inte den andra vore värre än ett enhetligt `Code::Timeout`.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <random>

#include "glaze_net/dns.h"
#include "glaze_net/ping.h"
#include "sys.h"

namespace glaze::net {
namespace {

constexpr std::uint8_t kEkoV4     = 8;     // ICMP echo request
constexpr std::uint8_t kSvarV4    = 0;     // ICMP echo reply
constexpr std::uint8_t kEkoV6     = 128;   // ICMPv6 echo request
constexpr std::uint8_t kSvarV6    = 129;   // ICMPv6 echo reply
constexpr std::size_t  kHuvud     = 8;     // typ, kod, summa, id, sekvens
constexpr std::size_t  kNyttolast = 16;

// RFC 1071 — internetkontrollsumman över hela ICMP-meddelandet.
std::uint16_t kontrollsumma(const std::uint8_t* d, std::size_t n)
{
    std::uint32_t summa = 0;
    for (std::size_t i = 0; i + 1 < n; i += 2)
        summa += static_cast<std::uint32_t>(d[i] << 8 | d[i + 1]);
    if (n & 1)
        summa += static_cast<std::uint32_t>(d[n - 1] << 8);
    while (summa >> 16)
        summa = (summa & 0xFFFF) + (summa >> 16);
    return static_cast<std::uint16_t>(~summa & 0xFFFF);
}

Error nekad(const std::string& vad, const Error& e)
{
    if (e.code != Code::Permission)
        return sys::med(vad, e);
#if defined(__linux__)
    return Error{Code::Permission,
                 vad + ": ICMP-datagramsockets nekas — Linux tillåter dem bara för "
                       "grupper inom sysctl net.ipv4.ping_group_range (" +
                     e.message + ")"};
#else
    return sys::med(vad, e);
#endif
}

} // namespace

Result<PingReply> ping(std::string_view host, std::stop_token stop,
                       std::chrono::milliseconds timeout)
{
    const std::string   vad      = "ping av \"" + std::string(host) + "\"";
    const sys::Deadline deadline = sys::deadlineFrom(timeout);
    auto adresser = resolve(host, stop, sys::remaining(deadline));
    if (!adresser)
        return sys::med(vad, adresser.failure());

    // IPv4 före IPv6 när båda finns (ping.h).
    const auto v4  = std::find_if(adresser->begin(), adresser->end(),
                                  [](const Address& a) { return a.isV4(); });
    const Address mål = v4 != adresser->end() ? *v4 : adresser->front();
    const bool    v6  = mål.isV6();
    const std::string vadMål = vad + " (" + mål.toString() + ")";

    auto h = sys::open(v6, sys::Kind::Icmp);
    if (!h)
        return nekad(vadMål, h.failure());
    struct Stäng {
        sys::Handle h;
        ~Stäng() { sys::close(h); }
    } stäng{*h};

    // Paketet: huvud + slumpad nyttolast. Sekvensnumret och nyttolasten är
    // det svaret matchas på.
    std::random_device                             slump;
    std::array<std::uint8_t, kHuvud + kNyttolast> paket{};
    paket[0]                  = v6 ? kEkoV6 : kEkoV4;
    const std::uint16_t id    = static_cast<std::uint16_t>(slump());
    const std::uint16_t sekv  = static_cast<std::uint16_t>(slump());
    paket[4]                  = static_cast<std::uint8_t>(id >> 8);
    paket[5]                  = static_cast<std::uint8_t>(id & 0xFF);
    paket[6]                  = static_cast<std::uint8_t>(sekv >> 8);
    paket[7]                  = static_cast<std::uint8_t>(sekv & 0xFF);
    for (std::size_t i = 0; i < kNyttolast; ++i)
        paket[kHuvud + i] = static_cast<std::uint8_t>(slump());
    if (!v6) {
        const std::uint16_t summa = kontrollsumma(paket.data(), paket.size());
        paket[2]                  = static_cast<std::uint8_t>(summa >> 8);
        paket[3]                  = static_cast<std::uint8_t>(summa & 0xFF);
    }

    const Endpoint till{mål, 0};
    const auto     t0 = sys::Clock::now();
    for (;;) {
        if (stop.stop_requested())
            return sys::avbrutet(vadMål);
        auto s = sys::trySendTo(*h, till, reinterpret_cast<const char*>(paket.data()),
                                paket.size());
        if (!s) {
            if (auto v = sys::wait(*h, sys::Dir::Write, deadline, stop); !v)
                return sys::med(vadMål, v.failure());
            continue;
        }
        if (!*s)
            return nekad(vadMål, s->failure());
        break;
    }

    std::array<char, 2048> buf{};
    for (;;) {
        if (stop.stop_requested())
            return sys::avbrutet(vadMål);
        Endpoint från;
        auto     r = sys::tryRecvFrom(*h, buf.data(), buf.size(), från);
        if (!r) {
            if (auto v = sys::wait(*h, sys::Dir::Read, deadline, stop); !v) {
                if (v.code() == Code::Timeout)
                    return Error{Code::Timeout, vadMål + ": inget ekosvar inom tidsgränsen"};
                return sys::med(vadMål, v.failure());
            }
            continue;
        }
        if (!*r)
            return sys::med(vadMål, r->failure());

        const auto*       d = reinterpret_cast<const std::uint8_t*>(buf.data());
        const std::size_t n = **r;
        std::size_t       start = 0;
        // macOS lämnar IPv4-huvudet kvar; Linux börjar på ICMP-typen (0 för
        // ett svar), så versionsnibbeln skiljer dem åt utan att fråga OS:et.
        if (!v6 && n >= 20 && (d[0] >> 4) == 4)
            start = static_cast<std::size_t>(d[0] & 0x0F) * 4;
        if (n < start + kHuvud + kNyttolast)
            continue;
        const std::uint8_t* svar = d + start;
        if (svar[0] != (v6 ? kSvarV6 : kSvarV4) || svar[1] != 0)
            continue;
        if (svar[6] != paket[6] || svar[7] != paket[7])
            continue;
        if (std::memcmp(svar + kHuvud, paket.data() + kHuvud, kNyttolast) != 0)
            continue;

        PingReply p;
        p.address = från.address;
        p.time    = std::chrono::duration_cast<std::chrono::microseconds>(sys::Clock::now() - t0);
        return p;
    }
}

} // namespace glaze::net
