// glaze_net — OS-lagret på WINDOWS (Winsock 2). Kontraktet står i sys.h.
//
// VÄNTAN är `select` och inte `WSAPoll`: WSAPoll rapporterade länge inte en
// misslyckad icke-blockerande connect (curl slutade använda den av det
// skälet), medan `select` lägger det felet i except-mängden. Två sockets per
// väntan ryms med marginal i FD_SETSIZE (64).
//
// VÄCKAREN är en UDP-socket på 127.0.0.1 som stop-callbacken skickar en byte
// till — `select` tar bara sockets, så en Event eller en pipe går inte. Den
// skapas BARA när anropet faktiskt måste vänta och stoppet är möjligt, och
// den är PER väntan: en delad väckare per socket hade låtit en samtidig
// `send` stjäla det `receive` väcktes av.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mswsock.h>   // SIO_UDP_CONNRESET hos mingw-w64 (Windows SDK: mstcpip.h)
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstring>

#include "sys.h"
#include "win_text.h"

namespace glaze::net::sys {
namespace {

SOCKET s(Handle h) { return static_cast<SOCKET>(h); }

Code koda(int fel)
{
    switch (fel) {
    case WSAECONNREFUSED:
        return Code::Refused;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENETRESET:
        return Code::Reset;
    case WSAETIMEDOUT:
        return Code::Timeout;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
    case WSAENETDOWN:
    case WSAEHOSTDOWN:
    case WSATRY_AGAIN:   // namnservern svarar inte
        return Code::Unreachable;
    case WSAEADDRINUSE:
        return Code::AddressInUse;
    case WSAEADDRNOTAVAIL:
    case WSAEINVAL:
    case WSAEFAULT:
    case WSAEAFNOSUPPORT:
    case WSAEDESTADDRREQ:
    case WSAEMSGSIZE:
        return Code::InvalidArgument;
    case WSAEACCES:
        return Code::Permission;
    case WSAENOTCONN:
    case WSAESHUTDOWN:
        return Code::Closed;
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA:
        return Code::HostNotFound;
    default:
        return Code::Failed;
    }
}

Error fel(int kod)
{
    return Error{koda(kod), win::systemText(static_cast<DWORD>(kod)) + " (WSA " +
                                std::to_string(kod) + ")"};
}

int tillSockaddr(const Endpoint& ep, sockaddr_storage& ss)
{
    std::memset(&ss, 0, sizeof ss);
    if (ep.address.isV6()) {
        auto* a        = reinterpret_cast<sockaddr_in6*>(&ss);
        a->sin6_family = AF_INET6;
        a->sin6_port   = htons(ep.port);
        std::memcpy(&a->sin6_addr, ep.address.bytes().data(), 16);
        a->sin6_scope_id = ep.address.scopeId();
        return static_cast<int>(sizeof(sockaddr_in6));
    }
    auto* a       = reinterpret_cast<sockaddr_in*>(&ss);
    a->sin_family = AF_INET;
    a->sin_port   = htons(ep.port);
    std::memcpy(&a->sin_addr, ep.address.bytes().data(), 4);
    return static_cast<int>(sizeof(sockaddr_in));
}

Endpoint frånSockaddr(const sockaddr* sa)
{
    Endpoint ep;
    if (sa->sa_family == AF_INET6) {
        const auto*                  a = reinterpret_cast<const sockaddr_in6*>(sa);
        std::array<std::uint8_t, 16> b{};
        std::memcpy(b.data(), &a->sin6_addr, 16);
        ep.address = Address::v6(b, a->sin6_scope_id);
        ep.port    = ntohs(a->sin6_port);
    } else {
        const auto* a = reinterpret_cast<const sockaddr_in*>(sa);
        const auto* b = reinterpret_cast<const std::uint8_t*>(&a->sin_addr);
        ep.address    = Address::v4(b[0], b[1], b[2], b[3]);
        ep.port       = ntohs(a->sin_port);
    }
    return ep;
}

bool ickeBlockerande(SOCKET h)
{
    u_long på = 1;
    return ioctlsocket(h, FIONBIO, &på) == 0;
}

} // namespace

Error lastError() { return fel(WSAGetLastError()); }

Result<void> startup()
{
    static const Result<void> svar = []() -> Result<void> {
        WSADATA   data;
        const int r = WSAStartup(MAKEWORD(2, 2), &data);
        if (r != 0)
            return Error{Code::Failed, "Winsock kunde inte startas: " + fel(r).message};
        return {};
    }();
    return svar;
}

Result<Handle> open(bool v6, Kind kind)
{
    if (kind == Kind::Icmp)
        return Error{Code::Failed, "ICMP-sockets används inte på Windows (ping går via "
                                   "IcmpSendEcho)"};
    const int    familj = v6 ? AF_INET6 : AF_INET;
    const bool   ström  = kind == Kind::Stream;
    const SOCKET h = WSASocketW(familj, ström ? SOCK_STREAM : SOCK_DGRAM,
                                ström ? IPPROTO_TCP : IPPROTO_UDP, nullptr, 0,
                                WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (h == INVALID_SOCKET)
        return lastError();
    if (!ickeBlockerande(h)) {
        const Error e = lastError();
        closesocket(h);
        return e;
    }
    if (v6) {
        const DWORD ja = 1;
        setsockopt(h, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&ja),
                   sizeof ja);
    }
    if (!ström) {
        // En ICMP "port unreachable" som svar på ett tidigare datagram gör
        // annars NÄSTA recvfrom till WSAECONNRESET — en lyssnarloop hade
        // dött på att EN mottagare saknades.
        BOOL  nej = FALSE;
        DWORD n   = 0;
        WSAIoctl(h, SIO_UDP_CONNRESET, &nej, sizeof nej, nullptr, 0, &n, nullptr, nullptr);
    }
    return static_cast<Handle>(h);
}

void close(Handle h) { closesocket(s(h)); }

std::optional<Result<void>> tryConnect(Handle h, const Endpoint& to)
{
    sockaddr_storage ss;
    const int        n = tillSockaddr(to, ss);
    if (::connect(s(h), reinterpret_cast<const sockaddr*>(&ss), n) == 0)
        return Result<void>{};
    const int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS)
        return std::nullopt;
    return Result<void>{fel(e)};
}

Result<void> connectResult(Handle h)
{
    int e   = 0;
    int len = sizeof e;
    if (getsockopt(s(h), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&e), &len) != 0)
        return lastError();
    if (e != 0)
        return fel(e);
    return {};
}

std::optional<Result<std::size_t>> trySend(Handle h, const char* data, std::size_t n)
{
    const int bit = static_cast<int>(std::min<std::size_t>(n, INT_MAX));
    const int r   = ::send(s(h), data, bit, 0);
    if (r != SOCKET_ERROR)
        return Result<std::size_t>{static_cast<std::size_t>(r)};
    const int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK)
        return std::nullopt;
    return Result<std::size_t>{fel(e)};
}

std::optional<Result<std::size_t>> tryRecv(Handle h, char* buf, std::size_t n)
{
    const int bit = static_cast<int>(std::min<std::size_t>(n, INT_MAX));
    const int r   = ::recv(s(h), buf, bit, 0);
    if (r != SOCKET_ERROR)
        return Result<std::size_t>{static_cast<std::size_t>(r)};
    const int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK)
        return std::nullopt;
    return Result<std::size_t>{fel(e)};
}

std::optional<Result<std::size_t>> trySendTo(Handle h, const Endpoint& to, const char* data,
                                             std::size_t n)
{
    sockaddr_storage ss;
    const int        len = tillSockaddr(to, ss);
    const int        bit = static_cast<int>(std::min<std::size_t>(n, INT_MAX));
    const int        r   = ::sendto(s(h), data, bit, 0, reinterpret_cast<const sockaddr*>(&ss),
                                    len);
    if (r != SOCKET_ERROR)
        return Result<std::size_t>{static_cast<std::size_t>(r)};
    const int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK)
        return std::nullopt;
    return Result<std::size_t>{fel(e)};
}

std::optional<Result<std::size_t>> tryRecvFrom(Handle h, char* buf, std::size_t n,
                                               Endpoint& from)
{
    sockaddr_storage ss{};
    int              len = sizeof ss;
    const int        bit = static_cast<int>(std::min<std::size_t>(n, INT_MAX));
    const int r = ::recvfrom(s(h), buf, bit, 0, reinterpret_cast<sockaddr*>(&ss), &len);
    if (r != SOCKET_ERROR) {
        from = frånSockaddr(reinterpret_cast<const sockaddr*>(&ss));
        return Result<std::size_t>{static_cast<std::size_t>(r)};
    }
    const int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK)
        return std::nullopt;
    return Result<std::size_t>{fel(e)};
}

std::optional<Result<Handle>> tryAccept(Handle h)
{
    const SOCKET ny = ::accept(s(h), nullptr, nullptr);
    if (ny == INVALID_SOCKET) {
        const int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET)   // motparten gav upp i kön
            return std::nullopt;
        return Result<Handle>{fel(e)};
    }
    // Den nya socketen ska ha samma egenskaper som `open` ger: icke-
    // blockerande och inte ärvbar. Läget ärvs från lyssnaren, men det sägs
    // uttryckligen här i stället för att lita på det.
    SetHandleInformation(reinterpret_cast<HANDLE>(ny), HANDLE_FLAG_INHERIT, 0);
    if (!ickeBlockerande(ny)) {
        const Error e = lastError();
        closesocket(ny);
        return Result<Handle>{e};
    }
    return Result<Handle>{static_cast<Handle>(ny)};
}

Result<void> bind(Handle h, const Endpoint& local, bool listener)
{
    if (listener) {
        // Windows SO_REUSEADDR låter en ANNAN process kapa en aktiv port;
        // EXCLUSIVEADDRUSE är motsatsen och det säkra valet för en lyssnare.
        const BOOL ja = TRUE;
        setsockopt(s(h), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&ja),
                   sizeof ja);
    }
    sockaddr_storage ss;
    const int        n = tillSockaddr(local, ss);
    if (::bind(s(h), reinterpret_cast<const sockaddr*>(&ss), n) != 0)
        return lastError();
    return {};
}

Result<void> listen(Handle h, int backlog)
{
    if (::listen(s(h), backlog) != 0)
        return lastError();
    return {};
}

Result<void> shutdownSend(Handle h)
{
    if (::shutdown(s(h), SD_SEND) != 0)
        return lastError();
    return {};
}

Result<Endpoint> localEndpoint(Handle h)
{
    sockaddr_storage ss{};
    int              len = sizeof ss;
    if (getsockname(s(h), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return lastError();
    return frånSockaddr(reinterpret_cast<const sockaddr*>(&ss));
}

Result<Endpoint> remoteEndpoint(Handle h)
{
    sockaddr_storage ss{};
    int              len = sizeof ss;
    if (getpeername(s(h), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return lastError();
    return frånSockaddr(reinterpret_cast<const sockaddr*>(&ss));
}

Result<void> wait(Handle h, Dir dir, Deadline deadline, const std::stop_token& stop)
{
    // Väckaren — bara om stoppet alls är möjligt.
    SOCKET      väckare = INVALID_SOCKET;
    sockaddr_in egen{};
    if (stop.stop_possible()) {
        väckare = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (väckare == INVALID_SOCKET)
            return lastError();
        egen.sin_family      = AF_INET;
        egen.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int len              = sizeof egen;
        if (::bind(väckare, reinterpret_cast<const sockaddr*>(&egen), sizeof egen) != 0 ||
            getsockname(väckare, reinterpret_cast<sockaddr*>(&egen), &len) != 0 ||
            !ickeBlockerande(väckare)) {
            const Error e = lastError();
            closesocket(väckare);
            return e;
        }
    }
    // Ordningen är bärande: callbacken (som skriver till väckaren) måste vara
    // avregistrerad INNAN väckaren stängs — `stäng` deklareras därför först
    // och förstörs sist.
    struct Stäng {
        SOCKET s;
        ~Stäng()
        {
            if (s != INVALID_SOCKET)
                closesocket(s);
        }
    } stäng{väckare};
    auto väck = [väckare, &egen] {
        const char x = 1;
        ::sendto(väckare, &x, 1, 0, reinterpret_cast<const sockaddr*>(&egen), sizeof egen);
    };
    std::optional<std::stop_callback<decltype(väck)>> callback;
    if (väckare != INVALID_SOCKET)
        callback.emplace(stop, väck);

    for (;;) {
        if (stop.stop_requested())
            return Error{threads::Code::Cancelled, "avbrutet"};
        fd_set läs, skriv, undantag;
        FD_ZERO(&läs);
        FD_ZERO(&skriv);
        FD_ZERO(&undantag);
        if (dir == Dir::Read) {
            FD_SET(s(h), &läs);
        } else {
            FD_SET(s(h), &skriv);
            FD_SET(s(h), &undantag);   // en misslyckad connect hamnar HÄR
        }
        if (väckare != INVALID_SOCKET)
            FD_SET(väckare, &läs);

        timeval  tv{};
        timeval* tak = nullptr;
        if (deadline != Deadline::max()) {
            const auto kvar = deadline - Clock::now();
            if (kvar <= Clock::duration::zero())
                return Error{Code::Timeout, "inget svar inom tidsgränsen"};
            // Högst ett dygn per varv — timeval:s sekunder är 32 bitar här.
            const long long us = std::min<long long>(
                std::chrono::duration_cast<std::chrono::microseconds>(kvar).count() + 1,
                86400LL * 1000000LL);
            tv.tv_sec  = static_cast<long>(us / 1000000);
            tv.tv_usec = static_cast<long>(us % 1000000);
            tak        = &tv;
        }
        const int n = ::select(0, &läs, &skriv, &undantag, tak);
        if (n == SOCKET_ERROR)
            return lastError();
        if (n == 0)
            continue;   // taket — loopens topp svarar Timeout
        if (FD_ISSET(s(h), &läs) || FD_ISSET(s(h), &skriv) || FD_ISSET(s(h), &undantag))
            return {};
        if (väckare != INVALID_SOCKET && FD_ISSET(väckare, &läs)) {
            // Stoppet svaras i loopens topp. Ett datagram som INTE kom från
            // stoppet (någon annan på samma loopback-port) dräneras, annars
            // hade select svarat direkt varje varv.
            char skräp[64];
            ::recv(väckare, skräp, sizeof skräp, 0);
        }
    }
}

Result<std::vector<Address>> resolveBlocking(const std::string& host)
{
    ADDRINFOW tips{};
    tips.ai_family   = AF_UNSPEC;
    tips.ai_socktype = SOCK_STREAM;   // en post per adress, inte en per sockettyp
    ADDRINFOW*         svar = nullptr;
    const std::wstring namn = win::wide(host);
    const int          r    = GetAddrInfoW(namn.c_str(), nullptr, &tips, &svar);
    if (r != 0)
        return fel(r);
    std::vector<Address> ut;
    for (const ADDRINFOW* p = svar; p; p = p->ai_next) {
        if (!p->ai_addr || (p->ai_family != AF_INET && p->ai_family != AF_INET6))
            continue;
        const Address a = frånSockaddr(p->ai_addr).address;
        if (std::find(ut.begin(), ut.end(), a) == ut.end())
            ut.push_back(a);
    }
    FreeAddrInfoW(svar);
    if (ut.empty())
        return Error{Code::HostNotFound, "namnet gav inga IPv4- eller IPv6-adresser"};
    return ut;
}

} // namespace glaze::net::sys
