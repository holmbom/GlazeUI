// glaze_net — UDP. Logiken skrivs EN gång ovanpå OS-lagret (sys.h);
// kontraktet står i udp.h.

#include "glaze_net/udp.h"

#include <utility>

#include "sys.h"

namespace glaze::net {
namespace {

// Ett UDP-datagram får vara högst 65 507 byte (IPv4); bufferten rymmer det.
constexpr std::size_t kMaxDatagram = 65536;

} // namespace

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : handle_(std::exchange(other.handle_, sys::kNoHandle)), v6_(other.v6_)
{}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, sys::kNoHandle);
        v6_     = other.v6_;
    }
    return *this;
}

void UdpSocket::close()
{
    if (handle_ != sys::kNoHandle) {
        sys::close(handle_);
        handle_ = sys::kNoHandle;
    }
}

Result<void> UdpSocket::open(bool v6)
{
    if (handle_ != sys::kNoHandle) {
        if (v6 != v6_)
            return Error{Code::InvalidArgument,
                         std::string("socketen är ") + (v6_ ? "IPv6" : "IPv4") +
                             " men adressen " + (v6 ? "IPv6" : "IPv4")};
        return {};
    }
    if (auto init = sys::startup(); !init)
        return init;
    auto h = sys::open(v6, sys::Kind::Datagram);
    if (!h)
        return h.failure();
    handle_ = *h;
    v6_     = v6;
    return {};
}

Result<void> UdpSocket::bind(const Endpoint& local)
{
    close();
    const std::string vad = "bindning till " + local.toString();
    if (auto r = open(local.address.isV6()); !r)
        return sys::med(vad, r.failure());
    if (auto r = sys::bind(handle_, local, false); !r) {
        close();
        return sys::med(vad, r.failure());
    }
    return {};
}

Result<void> UdpSocket::sendTo(const Endpoint& to, std::string_view data, std::stop_token stop,
                               std::chrono::milliseconds timeout)
{
    const std::string vad = "sändning till " + to.toString();
    if (auto r = open(to.address.isV6()); !r)
        return sys::med(vad, r.failure());
    const sys::Deadline deadline = sys::deadlineFrom(timeout);
    for (;;) {
        if (stop.stop_requested())
            return sys::avbrutet(vad);
        auto r = sys::trySendTo(handle_, to, data.data(), data.size());
        if (!r) {
            if (auto v = sys::wait(handle_, sys::Dir::Write, deadline, stop); !v)
                return sys::med(vad, v.failure());
            continue;
        }
        if (!*r)
            return sys::med(vad, r->failure());
        if (**r != data.size())   // ett datagram går helt eller inte alls
            return Error{Code::Failed, vad + ": datagrammet skickades bara delvis"};
        return {};
    }
}

Result<Datagram> UdpSocket::receive(std::stop_token stop, std::chrono::milliseconds timeout)
{
    if (handle_ == sys::kNoHandle)
        return Error{Code::InvalidArgument,
                     "mottagning: socketen är inte öppen — bind() eller sendTo() först"};
    const sys::Deadline deadline = sys::deadlineFrom(timeout);
    Datagram            d;
    d.data.resize(kMaxDatagram);
    for (;;) {
        if (stop.stop_requested())
            return sys::avbrutet("mottagning");
        auto r = sys::tryRecvFrom(handle_, d.data.data(), d.data.size(), d.from);
        if (!r) {
            if (auto v = sys::wait(handle_, sys::Dir::Read, deadline, stop); !v)
                return sys::med("mottagning", v.failure());
            continue;
        }
        if (!*r)
            return sys::med("mottagning", r->failure());
        d.data.resize(**r);
        return d;
    }
}

Result<Endpoint> UdpSocket::localEndpoint() const
{
    if (handle_ == sys::kNoHandle)
        return Error{Code::InvalidArgument, "lokal adress: socketen är inte öppen"};
    return sys::localEndpoint(handle_);
}

} // namespace glaze::net
