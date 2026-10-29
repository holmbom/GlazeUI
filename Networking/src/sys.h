#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "glaze_net/address.h"
#include "glaze_net/error.h"

// INTERN — OS-LAGRET under glaze_net. Samma roll som en backend under
// `gfx::`: ingen OS-typ syns här, och TCP/UDP/DNS/PING-logiken ovanpå
// (tcp.cpp, udp.cpp, dns.cpp, ping_posix.cpp) skrivs EN gång. Det som skiljer
// per OS bor i sys_win.cpp (Winsock) respektive sys_posix.cpp (BSD-sockets —
// Linux och macOS delar den, med `#ifdef` där de skiljer sig).
//
// Formen är "FÖRSÖK, annars VÄNTA": varje socket är icke-blockerande, ett
// `try*`-anrop svarar `nullopt` när det hade blockerat, och anroparen väntar
// då med `wait` — på socketen OCH på en väckare som stop-callbacken skriver
// till. Ingen tråd vaknar någonsin för att titta efter ett stopp.
//
// FELTEXTERNA härifrån bär bara systemets egen beskrivning och felnumret;
// SAMMANHANGET ("anslutning till 10.0.0.1:80") sätts av anroparen med `med`.

namespace glaze::net::sys {

using Handle               = std::intptr_t;
constexpr Handle kNoHandle = -1;
using Clock                = std::chrono::steady_clock;
using Deadline             = Clock::time_point;   // max() = inget tak

inline Deadline deadlineFrom(std::chrono::milliseconds timeout)
{
    if (timeout == kForever)
        return Deadline::max();
    const auto nu = Clock::now();
    if (timeout.count() <= 0)
        return nu;
    if (timeout > std::chrono::duration_cast<std::chrono::milliseconds>(Deadline::max() - nu))
        return Deadline::max();
    return nu + timeout;
}

// Det som återstår av taket (för ett anrop som själv tar en tidsgräns).
inline std::chrono::milliseconds remaining(Deadline deadline)
{
    if (deadline == Deadline::max())
        return kForever;
    const auto kvar =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return kvar.count() > 0 ? kvar : std::chrono::milliseconds{0};
}

// Sammanhanget först i feltexten: "anslutning till 10.0.0.1:80: <orsak>".
inline Error med(const std::string& vad, Error e)
{
    e.message = vad + ": " + e.message;
    return e;
}

inline Error avbrutet(const std::string& vad)
{
    return Error{threads::Code::Cancelled, vad + ": avbrutet"};
}

// Winsock-init EN gång per process (WSAStartup, aldrig WSACleanup — Winsock
// lever processen ut); på POSIX en no-op. Varje publik ingång anropar den.
Result<void> startup();

enum class Kind { Stream, Datagram, Icmp };   // Icmp: bara POSIX (ping_posix)

// Ny socket — ALLTID icke-blockerande, ärvs inte av barnprocesser, en IPv6-
// socket är BARA IPv6 (IPV6_V6ONLY, så att Linux och Windows beter sig
// lika), och en skrivning mot en stängd motpart ger ett fel i stället för
// SIGPIPE.
Result<Handle> open(bool v6, Kind kind);
void           close(Handle h);

// nullopt = pågår/hade blockerat — vänta (Write för connect) och fråga igen.
std::optional<Result<void>> tryConnect(Handle h, const Endpoint& to);
Result<void>                connectResult(Handle h);   // efter väntan: SO_ERROR

std::optional<Result<std::size_t>> trySend(Handle h, const char* data, std::size_t n);
// 0 byte = motparten har stängt.
std::optional<Result<std::size_t>> tryRecv(Handle h, char* buf, std::size_t n);
std::optional<Result<std::size_t>> trySendTo(Handle h, const Endpoint& to,
                                             const char* data, std::size_t n);
std::optional<Result<std::size_t>> tryRecvFrom(Handle h, char* buf, std::size_t n,
                                               Endpoint& from);
std::optional<Result<Handle>>      tryAccept(Handle h);

Result<void>     bind(Handle h, const Endpoint& local, bool listener);
Result<void>     listen(Handle h, int backlog);
Result<void>     shutdownSend(Handle h);
Result<Endpoint> localEndpoint(Handle h);
Result<Endpoint> remoteEndpoint(Handle h);

enum class Dir { Read, Write };

// Väntar tills `h` är redo åt hållet, `deadline` passerat eller `stop`
// begärts. Lyckat = redo (ett eventuellt socketfel läses av nästa try* eller
// `connectResult`). Misslyckat = `Code::Timeout`, `threads::Code::Cancelled`
// eller ett systemfel.
Result<void> wait(Handle h, Dir dir, Deadline deadline, const std::stop_token& stop);

// OS:ets namnuppslag — BLOCKERANDE och oavbrytbart; dns.cpp kör det i en
// frikopplad tråd. Utan dubbletter, i OS:ets ordning.
Result<std::vector<Address>> resolveBlocking(const std::string& host);

// Det senaste socketfelet (errno/WSAGetLastError) som ett `Error` — anropas
// DIREKT efter det misslyckade anropet.
Error lastError();

} // namespace glaze::net::sys
