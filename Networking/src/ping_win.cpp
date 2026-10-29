// glaze_net — PING på WINDOWS: IcmpSendEcho (IPv4) och Icmp6SendEcho2
// (IPv6) ur iphlpapi. OS-tjänsten skickar ekot, så inga rättigheter krävs.
// Kontraktet står i ping.h.
//
// Anropen är SYNKRONA och oavbrytbara (den asynkrona formen skriver i
// svarsbufferten tills den är klar, och en avbruten väntan hade lämnat en
// buffert som systemet fortfarande skriver i). De körs därför i en
// frikopplad tråd som äger sin buffert (detached.h) — avbrytning och tak
// verkar direkt för anroparen.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <cstring>
#include <vector>

#include "detached.h"
#include "glaze_net/dns.h"
#include "glaze_net/ping.h"
#include "win_text.h"

namespace glaze::net {
namespace {

// IPv6-svarets layout ENLIGT SDK:N. Där är `IPV6_ADDRESS_EX` packad till
// 1 byte (26 byte) — mingw-w64:s ipexport.h saknar packningen och lägger
// adressen två byte fel. Svaret läses därför genom den här kopian, med
// layouten låst av static_assert. (Wine:s ipexport.h har samma packning.)
#pragma pack(push, 1)
struct Ipv6AdressEx {
    USHORT port;
    ULONG  flowinfo;
    USHORT adress[8];
    ULONG  scopeId;
};
#pragma pack(pop)
struct Icmp6Svar {
    Ipv6AdressEx adress;
    ULONG        status;
    unsigned int tid;
};
static_assert(sizeof(Ipv6AdressEx) == 26, "IPV6_ADDRESS_EX är packad i SDK:n");
static_assert(offsetof(Icmp6Svar, status) == 28, "status efter den packade adressen");

// Samma 32 byte som Windows egen `ping` skickar.
constexpr char kNyttolast[] = "abcdefghijklmnopqrstuvwabcdefghi";
constexpr WORD kLängd       = sizeof kNyttolast - 1;

Code kodaIcmp(DWORD status)
{
    switch (status) {
    case IP_REQ_TIMED_OUT:
        return Code::Timeout;
    case IP_DEST_NET_UNREACHABLE:
    case IP_DEST_HOST_UNREACHABLE:
    case IP_DEST_PROT_UNREACHABLE:
    case IP_DEST_PORT_UNREACHABLE:
    case IP_TTL_EXPIRED_TRANSIT:
    case IP_TTL_EXPIRED_REASSEM:
    case IP_BAD_ROUTE:
    case IP_DEST_UNREACHABLE:
    case IP_TIME_EXCEEDED:
        return Code::Unreachable;
    case IP_BAD_DESTINATION:
        return Code::InvalidArgument;
    default:
        return Code::Failed;
    }
}

// IP_STATUS-koderna (110xx) har egna texter via GetIpErrorString; annat
// (ERROR_*) är vanliga systemfel.
Error icmpFel(DWORD status)
{
    std::string text;
    if (status >= IP_STATUS_BASE && status <= IP_STATUS_BASE + 255) {
        wchar_t buf[256];
        DWORD   storlek = static_cast<DWORD>(std::size(buf));
        if (GetIpErrorString(status, buf, &storlek) == NO_ERROR)
            text = win::utf8(std::wstring(buf));
    }
    if (text.empty())
        text = win::systemText(status);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return Error{kodaIcmp(status), text + " (" + std::to_string(status) + ")"};
}

Result<PingReply> ekoV4(const Address& mål, DWORD takMs)
{
    const HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE)
        return icmpFel(GetLastError());
    std::vector<unsigned char> svar(sizeof(ICMP_ECHO_REPLY) + kLängd + 8 + 64);
    IPAddr                     till = 0;
    std::memcpy(&till, mål.bytes().data(), 4);

    // API:t tar `LPVOID` men läser bara — en egen, skrivbar kopia i stället
    // för att kasta bort const från konstanten.
    std::array<char, kLängd> nyttolast{};
    std::memcpy(nyttolast.data(), kNyttolast, kLängd);

    const auto  t0 = sys::Clock::now();
    const DWORD n  = IcmpSendEcho(h, till, nyttolast.data(), kLängd, nullptr, svar.data(),
                                  static_cast<DWORD>(svar.size()), takMs);
    const auto  t1 = sys::Clock::now();
    const DWORD e  = n == 0 ? GetLastError() : 0;
    IcmpCloseHandle(h);
    if (n == 0)
        return icmpFel(e);

    // Address och Status är de två första fälten i både ICMP_ECHO_REPLY och
    // 64-bitarsvarianten ICMP_ECHO_REPLY32 — läsningen är layoutsäker.
    ICMP_ECHO_REPLY r;
    std::memcpy(&r, svar.data(), sizeof r);
    if (r.Status != IP_SUCCESS)
        return icmpFel(r.Status);
    PingReply p;
    const auto* a = reinterpret_cast<const std::uint8_t*>(&r.Address);
    p.address     = Address::v4(a[0], a[1], a[2], a[3]);
    p.time        = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
    return p;
}

Result<PingReply> ekoV6(const Address& mål, DWORD takMs)
{
    const HANDLE h = Icmp6CreateFile();
    if (h == INVALID_HANDLE_VALUE)
        return icmpFel(GetLastError());
    sockaddr_in6 från{};
    från.sin6_family = AF_INET6;   // :: = OS:et väljer källadress
    sockaddr_in6 till{};
    till.sin6_family   = AF_INET6;
    till.sin6_scope_id = mål.scopeId();
    std::memcpy(&till.sin6_addr, mål.bytes().data(), 16);
    // Dokumentationens krav: svaret + nyttolasten + 8 byte ICMP-fel + en
    // IO_STATUS_BLOCK.
    std::vector<unsigned char> svar(sizeof(Icmp6Svar) + kLängd + 8 + 2 * sizeof(void*) + 64);

    std::array<char, kLängd> nyttolast{};
    std::memcpy(nyttolast.data(), kNyttolast, kLängd);

    const auto  t0 = sys::Clock::now();
    const DWORD n  = Icmp6SendEcho2(h, nullptr, nullptr, nullptr, &från, &till,
                                    nyttolast.data(), kLängd, nullptr, svar.data(),
                                    static_cast<DWORD>(svar.size()), takMs);
    const auto  t1 = sys::Clock::now();
    const DWORD e  = n == 0 ? GetLastError() : 0;
    IcmpCloseHandle(h);
    if (n == 0)
        return icmpFel(e);

    Icmp6Svar r;
    std::memcpy(&r, svar.data(), sizeof r);
    if (r.status != IP_SUCCESS)
        return icmpFel(r.status);
    std::array<std::uint8_t, 16> b{};
    std::memcpy(b.data(), r.adress.adress, 16);
    PingReply p;
    p.address = Address::v6(b, r.adress.scopeId);
    p.time    = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
    return p;
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

    // IcmpSendEcho tar ett DWORD i millisekunder; "inget tak" blir en minut,
    // för ett eko utan tak finns inte i API:t.
    const auto  kvar  = sys::remaining(deadline);
    const DWORD takMs = kvar == kForever
                            ? 60000u
                            : static_cast<DWORD>(std::clamp<long long>(kvar.count(), 1, 600000));
    const bool v6 = mål.isV6();
    return sys::runDetached<PingReply>(
        [mål, takMs, v6] { return v6 ? ekoV6(mål, takMs) : ekoV4(mål, takMs); }, deadline,
        stop, vad + " (" + mål.toString() + ")");
}

} // namespace glaze::net
