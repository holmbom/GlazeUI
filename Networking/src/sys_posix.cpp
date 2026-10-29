// glaze_net — OS-lagret på LINUX och MACOS (BSD-sockets). Kontraktet står
// i sys.h. macOS-grenarna (`#ifdef`-blocken nedan) följer Darwins headers
// och dokumentation.
//
// Skillnaderna som spelar roll:
//   - Linux tar SOCK_NONBLOCK|SOCK_CLOEXEC direkt i `socket`/`accept4`;
//     macOS har varken flaggorna eller `accept4` ⇒ `fcntl` efteråt.
//   - SIGPIPE: Linux har MSG_NOSIGNAL per anrop, macOS socketflaggan
//     SO_NOSIGPIPE. Båda sätts där de finns — en SIGPIPE hade dödat appen.
//
// VÄCKAREN är en pipe (per väntan, av samma skäl som på Windows): stop-
// callbacken skriver en byte och `poll` vaknar på läsänden.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>

#include "sys.h"

namespace glaze::net::sys {
namespace {

int fd(Handle h) { return static_cast<int>(h); }

#ifdef MSG_NOSIGNAL
constexpr int kSändFlaggor = MSG_NOSIGNAL;
#else
constexpr int kSändFlaggor = 0;
#endif

Code koda(int e)
{
    switch (e) {
    case ECONNREFUSED:
        return Code::Refused;
    case ECONNRESET:
    case ECONNABORTED:
    case EPIPE:
    case ENETRESET:
        return Code::Reset;
    case ETIMEDOUT:
        return Code::Timeout;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
#ifdef EHOSTDOWN
    case EHOSTDOWN:
#endif
        return Code::Unreachable;
    case EADDRINUSE:
        return Code::AddressInUse;
    case EADDRNOTAVAIL:
    case EINVAL:
    case EFAULT:
    case EAFNOSUPPORT:
    case EDESTADDRREQ:
    case EMSGSIZE:
        return Code::InvalidArgument;
    case EACCES:
    case EPERM:
        return Code::Permission;
    case ENOTCONN:
#ifdef ESHUTDOWN
    case ESHUTDOWN:
#endif
        return Code::Closed;
    default:
        return Code::Failed;
    }
}

// `std::generic_category().message` är trådsäker (strerror är det inte).
Error fel(int e)
{
    return Error{koda(e), std::generic_category().message(e) + " (errno " +
                              std::to_string(e) + ")"};
}

bool sättFlaggor(int f)
{
    const int fl = fcntl(f, F_GETFL, 0);
    if (fl < 0 || fcntl(f, F_SETFL, fl | O_NONBLOCK) < 0)
        return false;
    return fcntl(f, F_SETFD, FD_CLOEXEC) == 0;
}

socklen_t tillSockaddr(const Endpoint& ep, sockaddr_storage& ss)
{
    std::memset(&ss, 0, sizeof ss);
    if (ep.address.isV6()) {
        auto* a        = reinterpret_cast<sockaddr_in6*>(&ss);
        a->sin6_family = AF_INET6;
        a->sin6_port   = htons(ep.port);
        std::memcpy(&a->sin6_addr, ep.address.bytes().data(), 16);
        a->sin6_scope_id = ep.address.scopeId();
        return sizeof(sockaddr_in6);
    }
    auto* a       = reinterpret_cast<sockaddr_in*>(&ss);
    a->sin_family = AF_INET;
    a->sin_port   = htons(ep.port);
    std::memcpy(&a->sin_addr, ep.address.bytes().data(), 4);
    return sizeof(sockaddr_in);
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

bool hadeBlockerat(int e) { return e == EAGAIN || e == EWOULDBLOCK; }

} // namespace

Error lastError() { return fel(errno); }

Result<void> startup() { return {}; }

Result<Handle> open(bool v6, Kind kind)
{
    const int familj = v6 ? AF_INET6 : AF_INET;
    const int typ    = kind == Kind::Stream ? SOCK_STREAM : SOCK_DGRAM;
    int       proto  = 0;
    // Som int var för sig: glibc lägger IPPROTO_ICMP och IPPROTO_ICMPV6 i
    // två olika anonyma enum, och ?: mellan dem är en -Wenum-compare.
    if (kind == Kind::Icmp)
        proto = v6 ? static_cast<int>(IPPROTO_ICMPV6) : static_cast<int>(IPPROTO_ICMP);
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    const int f = ::socket(familj, typ | SOCK_NONBLOCK | SOCK_CLOEXEC, proto);
    if (f < 0)
        return lastError();
#else
    const int f = ::socket(familj, typ, proto);
    if (f < 0)
        return lastError();
    if (!sättFlaggor(f)) {
        const Error e = lastError();
        ::close(f);
        return e;
    }
#endif
#ifdef SO_NOSIGPIPE
    const int ja = 1;
    setsockopt(f, SOL_SOCKET, SO_NOSIGPIPE, &ja, sizeof ja);
#endif
    if (v6 && kind != Kind::Icmp) {
        const int bara6 = 1;
        setsockopt(f, IPPROTO_IPV6, IPV6_V6ONLY, &bara6, sizeof bara6);
    }
    return static_cast<Handle>(f);
}

void close(Handle h) { ::close(fd(h)); }

std::optional<Result<void>> tryConnect(Handle h, const Endpoint& to)
{
    sockaddr_storage ss;
    const socklen_t  n = tillSockaddr(to, ss);
    if (::connect(fd(h), reinterpret_cast<const sockaddr*>(&ss), n) == 0)
        return Result<void>{};
    // EINTR på en icke-blockerande connect betyder att den fortsätter
    // asynkront — samma sak som EINPROGRESS.
    if (errno == EINPROGRESS || errno == EINTR)
        return std::nullopt;
    return Result<void>{lastError()};
}

Result<void> connectResult(Handle h)
{
    int       e   = 0;
    socklen_t len = sizeof e;
    if (getsockopt(fd(h), SOL_SOCKET, SO_ERROR, &e, &len) != 0)
        return lastError();
    if (e != 0)
        return fel(e);
    return {};
}

std::optional<Result<std::size_t>> trySend(Handle h, const char* data, std::size_t n)
{
    for (;;) {
        const ssize_t r = ::send(fd(h), data, n, kSändFlaggor);
        if (r >= 0)
            return Result<std::size_t>{static_cast<std::size_t>(r)};
        if (errno == EINTR)
            continue;
        if (hadeBlockerat(errno))
            return std::nullopt;
        return Result<std::size_t>{lastError()};
    }
}

std::optional<Result<std::size_t>> tryRecv(Handle h, char* buf, std::size_t n)
{
    for (;;) {
        const ssize_t r = ::recv(fd(h), buf, n, 0);
        if (r >= 0)
            return Result<std::size_t>{static_cast<std::size_t>(r)};
        if (errno == EINTR)
            continue;
        if (hadeBlockerat(errno))
            return std::nullopt;
        return Result<std::size_t>{lastError()};
    }
}

std::optional<Result<std::size_t>> trySendTo(Handle h, const Endpoint& to, const char* data,
                                             std::size_t n)
{
    sockaddr_storage ss;
    const socklen_t  len = tillSockaddr(to, ss);
    for (;;) {
        const ssize_t r = ::sendto(fd(h), data, n, kSändFlaggor,
                                   reinterpret_cast<const sockaddr*>(&ss), len);
        if (r >= 0)
            return Result<std::size_t>{static_cast<std::size_t>(r)};
        if (errno == EINTR)
            continue;
        if (hadeBlockerat(errno))
            return std::nullopt;
        return Result<std::size_t>{lastError()};
    }
}

std::optional<Result<std::size_t>> tryRecvFrom(Handle h, char* buf, std::size_t n,
                                               Endpoint& from)
{
    for (;;) {
        sockaddr_storage ss{};
        socklen_t        len = sizeof ss;
        const ssize_t    r   = ::recvfrom(fd(h), buf, n, 0, reinterpret_cast<sockaddr*>(&ss),
                                          &len);
        if (r >= 0) {
            from = frånSockaddr(reinterpret_cast<const sockaddr*>(&ss));
            return Result<std::size_t>{static_cast<std::size_t>(r)};
        }
        if (errno == EINTR)
            continue;
        if (hadeBlockerat(errno))
            return std::nullopt;
        return Result<std::size_t>{lastError()};
    }
}

std::optional<Result<Handle>> tryAccept(Handle h)
{
    for (;;) {
#if defined(__linux__)
        const int ny = ::accept4(fd(h), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        const int ny = ::accept(fd(h), nullptr, nullptr);
#endif
        if (ny < 0) {
            if (errno == EINTR)
                continue;
            // ECONNABORTED: motparten gav upp medan anslutningen låg i kön.
            if (hadeBlockerat(errno) || errno == ECONNABORTED)
                return std::nullopt;
            return Result<Handle>{lastError()};
        }
#if !defined(__linux__)
        if (!sättFlaggor(ny)) {
            const Error e = lastError();
            ::close(ny);
            return Result<Handle>{e};
        }
#endif
#ifdef SO_NOSIGPIPE
        const int ja = 1;
        setsockopt(ny, SOL_SOCKET, SO_NOSIGPIPE, &ja, sizeof ja);
#endif
        return Result<Handle>{static_cast<Handle>(ny)};
    }
}

Result<void> bind(Handle h, const Endpoint& local, bool listener)
{
    if (listener) {
        // POSIX SO_REUSEADDR = återanvänd en port i TIME_WAIT efter en
        // omstart. (Den kapar inte en AKTIV lyssnare som Windows variant.)
        const int ja = 1;
        setsockopt(fd(h), SOL_SOCKET, SO_REUSEADDR, &ja, sizeof ja);
    }
    sockaddr_storage ss;
    const socklen_t  n = tillSockaddr(local, ss);
    if (::bind(fd(h), reinterpret_cast<const sockaddr*>(&ss), n) != 0)
        return lastError();
    return {};
}

Result<void> listen(Handle h, int backlog)
{
    if (::listen(fd(h), backlog) != 0)
        return lastError();
    return {};
}

Result<void> shutdownSend(Handle h)
{
    if (::shutdown(fd(h), SHUT_WR) != 0)
        return lastError();
    return {};
}

Result<Endpoint> localEndpoint(Handle h)
{
    sockaddr_storage ss{};
    socklen_t        len = sizeof ss;
    if (getsockname(fd(h), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return lastError();
    return frånSockaddr(reinterpret_cast<const sockaddr*>(&ss));
}

Result<Endpoint> remoteEndpoint(Handle h)
{
    sockaddr_storage ss{};
    socklen_t        len = sizeof ss;
    if (getpeername(fd(h), reinterpret_cast<sockaddr*>(&ss), &len) != 0)
        return lastError();
    return frånSockaddr(reinterpret_cast<const sockaddr*>(&ss));
}

Result<void> wait(Handle h, Dir dir, Deadline deadline, const std::stop_token& stop)
{
    // Väckaren — bara om stoppet alls är möjligt.
    int rör[2] = {-1, -1};
    if (stop.stop_possible()) {
        if (::pipe(rör) != 0)
            return lastError();
        if (!sättFlaggor(rör[0]) || !sättFlaggor(rör[1])) {
            const Error e = lastError();
            ::close(rör[0]);
            ::close(rör[1]);
            return e;
        }
    }
    // Ordningen är bärande: callbacken (som skriver i röret) avregistreras
    // innan röret stängs — `stäng` deklareras därför först och dör sist.
    struct Stäng {
        int* r;
        ~Stäng()
        {
            if (r[0] >= 0)
                ::close(r[0]);
            if (r[1] >= 0)
                ::close(r[1]);
        }
    } stäng{rör};
    auto väck = [skriv = rör[1]] {
        const char x = 1;
        [[maybe_unused]] const ssize_t n = ::write(skriv, &x, 1);   // fullt rör = redan väckt
    };
    std::optional<std::stop_callback<decltype(väck)>> callback;
    if (rör[1] >= 0)
        callback.emplace(stop, väck);

    for (;;) {
        if (stop.stop_requested())
            return Error{threads::Code::Cancelled, "avbrutet"};
        pollfd p[2]{};
        p[0].fd     = fd(h);
        p[0].events = dir == Dir::Read ? POLLIN : POLLOUT;
        nfds_t antal = 1;
        if (rör[0] >= 0) {
            p[1].fd     = rör[0];
            p[1].events = POLLIN;
            antal       = 2;
        }
        int tak = -1;
        if (deadline != Deadline::max()) {
            const auto kvar = deadline - Clock::now();
            if (kvar <= Clock::duration::zero())
                return Error{Code::Timeout, "inget svar inom tidsgränsen"};
            tak = static_cast<int>(std::min<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(kvar).count() + 1,
                INT_MAX));
        }
        const int n = ::poll(p, antal, tak);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return lastError();
        }
        if (n == 0)
            continue;   // taket — loopens topp svarar Timeout
        // POLLERR/POLLHUP räknas som "redo": nästa try* eller connectResult
        // läser ut det faktiska felet.
        if (p[0].revents & (p[0].events | POLLERR | POLLHUP | POLLNVAL))
            return {};
        if (antal == 2 && (p[1].revents & POLLIN)) {
            char skräp[64];
            [[maybe_unused]] const ssize_t m = ::read(rör[0], skräp, sizeof skräp);
        }
    }
}

Result<std::vector<Address>> resolveBlocking(const std::string& host)
{
    addrinfo tips{};
    tips.ai_family   = AF_UNSPEC;
    tips.ai_socktype = SOCK_STREAM;   // en post per adress, inte en per sockettyp
    addrinfo* svar   = nullptr;
    const int r      = getaddrinfo(host.c_str(), nullptr, &tips, &svar);
    if (r != 0) {
        Code kod = Code::Failed;
        switch (r) {
        case EAI_NONAME:
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
        case EAI_NODATA:
#endif
            kod = Code::HostNotFound;
            break;
        case EAI_AGAIN:   // namnservern svarar inte
            kod = Code::Unreachable;
            break;
        case EAI_FAMILY:
        case EAI_SOCKTYPE:
        case EAI_BADFLAGS:
            kod = Code::InvalidArgument;
            break;
        case EAI_SYSTEM:
            return fel(errno);
        default:
            break;
        }
        return Error{kod, std::string(gai_strerror(r)) + " (EAI " + std::to_string(r) + ")"};
    }
    std::vector<Address> ut;
    for (const addrinfo* p = svar; p; p = p->ai_next) {
        if (!p->ai_addr || (p->ai_family != AF_INET && p->ai_family != AF_INET6))
            continue;
        const Address a = frånSockaddr(p->ai_addr).address;
        if (std::find(ut.begin(), ut.end(), a) == ut.end())
            ut.push_back(a);
    }
    freeaddrinfo(svar);
    if (ut.empty())
        return Error{Code::HostNotFound, "namnet gav inga IPv4- eller IPv6-adresser"};
    return ut;
}

} // namespace glaze::net::sys
