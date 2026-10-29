// glaze_net — TCP. Logiken skrivs EN gång ovanpå OS-lagret (sys.h);
// kontraktet står i tcp.h.

#include "glaze_net/tcp.h"

#include <utility>

#include "glaze_net/dns.h"
#include "sys.h"

namespace glaze::net {
namespace {

// EN adress, med det tak som återstår. Lyckas den lämnas socketen i `ut`.
Result<void> anslutEn(std::intptr_t& ut, const Endpoint& till, const std::stop_token& stop,
                      sys::Deadline deadline)
{
    const std::string vad = "anslutning till " + till.toString();
    if (stop.stop_requested())
        return sys::avbrutet(vad);

    auto h = sys::open(till.address.isV6(), sys::Kind::Stream);
    if (!h)
        return sys::med(vad, h.failure());

    auto försök = sys::tryConnect(*h, till);
    if (!försök) {   // pågår — vänta på skrivbarhet, läs sedan utfallet
        auto väntan = sys::wait(*h, sys::Dir::Write, deadline, stop);
        if (väntan)
            försök = sys::connectResult(*h);
        else
            försök = std::move(väntan);
    }
    if (!*försök) {
        sys::close(*h);
        return sys::med(vad, försök->failure());
    }
    ut = *h;
    return {};
}

Error inteAnsluten(const char* vad)
{
    return Error{Code::InvalidArgument, std::string(vad) + ": socketen är inte ansluten"};
}

} // namespace

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : handle_(std::exchange(other.handle_, sys::kNoHandle))
{}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, sys::kNoHandle);
    }
    return *this;
}

void TcpSocket::close()
{
    if (handle_ != sys::kNoHandle) {
        sys::close(handle_);
        handle_ = sys::kNoHandle;
    }
}

Result<void> TcpSocket::connect(const Endpoint& to, std::stop_token stop,
                                std::chrono::milliseconds timeout)
{
    close();
    if (auto init = sys::startup(); !init)
        return init;
    return anslutEn(handle_, to, stop, sys::deadlineFrom(timeout));
}

Result<void> TcpSocket::connect(std::string_view host, std::uint16_t port,
                                std::stop_token stop, std::chrono::milliseconds timeout)
{
    close();
    if (auto init = sys::startup(); !init)
        return init;
    const sys::Deadline deadline = sys::deadlineFrom(timeout);

    // Uppslaget får det tak som återstår — HELA förloppet ryms i `timeout`.
    auto adresser = resolve(host, stop, sys::remaining(deadline));
    if (!adresser)
        return adresser.failure();

    // Adresserna i OS:ets ordning tills en svarar. Timeout och avbrott
    // avslutar direkt: det finns ingen tid kvar att pröva nästa med.
    Error sista{Code::HostNotFound, "inga adresser"};
    for (const Address& a : *adresser) {
        auto r = anslutEn(handle_, Endpoint{a, port}, stop, deadline);
        if (r)
            return {};
        sista = r.failure();
        if (r.cancelled() || r.code() == Code::Timeout)
            break;
    }
    return sista;
}

Result<void> TcpSocket::send(std::string_view data, std::stop_token stop,
                             std::chrono::milliseconds timeout)
{
    if (handle_ == sys::kNoHandle)
        return inteAnsluten("sändning");
    const sys::Deadline deadline = sys::deadlineFrom(timeout);
    std::size_t         skickat  = 0;
    while (skickat < data.size()) {
        if (stop.stop_requested())
            return sys::avbrutet("sändning");
        auto r = sys::trySend(handle_, data.data() + skickat, data.size() - skickat);
        if (!r) {
            if (auto v = sys::wait(handle_, sys::Dir::Write, deadline, stop); !v)
                return sys::med("sändning", v.failure());
            continue;
        }
        if (!*r)
            return sys::med("sändning", r->failure());
        skickat += **r;
    }
    return {};
}

Result<std::string> TcpSocket::receive(std::stop_token stop, std::chrono::milliseconds timeout,
                                       std::size_t max)
{
    if (handle_ == sys::kNoHandle)
        return inteAnsluten("mottagning");
    if (max == 0)
        return Error{Code::InvalidArgument, "mottagning: max måste vara minst 1"};
    const sys::Deadline deadline = sys::deadlineFrom(timeout);
    std::string         buf(max, '\0');
    for (;;) {
        if (stop.stop_requested())
            return sys::avbrutet("mottagning");
        auto r = sys::tryRecv(handle_, buf.data(), buf.size());
        if (!r) {
            if (auto v = sys::wait(handle_, sys::Dir::Read, deadline, stop); !v)
                return sys::med("mottagning", v.failure());
            continue;
        }
        if (!*r)
            return sys::med("mottagning", r->failure());
        if (**r == 0)
            return Error{Code::Closed, "mottagning: motparten stängde anslutningen"};
        buf.resize(**r);
        return buf;
    }
}

Result<void> TcpSocket::shutdownSend()
{
    if (handle_ == sys::kNoHandle)
        return inteAnsluten("halvstängning");
    if (auto r = sys::shutdownSend(handle_); !r)
        return sys::med("halvstängning", r.failure());
    return {};
}

Result<Endpoint> TcpSocket::localEndpoint() const
{
    if (handle_ == sys::kNoHandle)
        return inteAnsluten("lokal adress");
    return sys::localEndpoint(handle_);
}

Result<Endpoint> TcpSocket::remoteEndpoint() const
{
    if (handle_ == sys::kNoHandle)
        return inteAnsluten("motpartens adress");
    return sys::remoteEndpoint(handle_);
}

// ------------------------------------------------------------- lyssnaren

TcpListener::~TcpListener() { close(); }

TcpListener::TcpListener(TcpListener&& other) noexcept
    : handle_(std::exchange(other.handle_, sys::kNoHandle))
{}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept
{
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, sys::kNoHandle);
    }
    return *this;
}

void TcpListener::close()
{
    if (handle_ != sys::kNoHandle) {
        sys::close(handle_);
        handle_ = sys::kNoHandle;
    }
}

Result<void> TcpListener::listen(const Endpoint& local, int backlog)
{
    close();
    const std::string vad = "lyssning på " + local.toString();
    if (auto init = sys::startup(); !init)
        return sys::med(vad, init.failure());
    auto h = sys::open(local.address.isV6(), sys::Kind::Stream);
    if (!h)
        return sys::med(vad, h.failure());
    if (auto r = sys::bind(*h, local, true); !r) {
        sys::close(*h);
        return sys::med(vad, r.failure());
    }
    if (auto r = sys::listen(*h, backlog); !r) {
        sys::close(*h);
        return sys::med(vad, r.failure());
    }
    handle_ = *h;
    return {};
}

Result<TcpSocket> TcpListener::accept(std::stop_token stop, std::chrono::milliseconds timeout)
{
    if (handle_ == sys::kNoHandle)
        return Error{Code::InvalidArgument, "accept: lyssnaren är inte öppen"};
    const sys::Deadline deadline = sys::deadlineFrom(timeout);
    for (;;) {
        if (stop.stop_requested())
            return sys::avbrutet("accept");
        auto r = sys::tryAccept(handle_);
        if (!r) {
            if (auto v = sys::wait(handle_, sys::Dir::Read, deadline, stop); !v)
                return sys::med("accept", v.failure());
            continue;
        }
        if (!*r)
            return sys::med("accept", r->failure());
        TcpSocket s;
        s.handle_ = **r;
        return s;
    }
}

Result<Endpoint> TcpListener::localEndpoint() const
{
    if (handle_ == sys::kNoHandle)
        return Error{Code::InvalidArgument, "lokal adress: lyssnaren är inte öppen"};
    return sys::localEndpoint(handle_);
}

} // namespace glaze::net
