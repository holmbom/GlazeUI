#pragma once
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>

#include "glaze_net/address.h"
#include "glaze_net/error.h"

// glaze_net — UDP.
//
// Samma form som TCP: blockerande med tidsgräns och avbrytning, för en
// pooltråd. En lyssnare som ska leva länge får en EGEN tråd —
// `glaze::threads::Pool{1}` + en `Worker` på den — så att den inte lånar en
// av den delade poolens trådar för evigt. Socketen FLYTTAS in i arbetet (den
// rörs bara där), och felet går vidare i stället för att sväljas:
//
//     glaze::threads::Pool   lyssnarTråd{1};          // medlemmar, i DEN ordningen
//     glaze::threads::Worker lyssnare{lyssnarTråd};
//
//     glaze::net::UdpSocket s;
//     s.bind({glaze::net::Address::loopbackV4(), 5000});
//     lyssnare.run([s = std::move(s), this](std::stop_token stop) mutable
//                      -> glaze::net::Result<void> {
//         for (;;) {
//             auto d = s.receive(stop);
//             if (!d) return d.failure();   // ett avbrott levereras aldrig
//             lyssnare.post(stop, [this, text = d->data] { logg.setText(text); });
//         }
//     }, [this](glaze::net::Result<void>& r) { logg.setText(r.error()); });
//
// Ett datagram är helt eller inget; storleken är högst 64 KiB.

namespace glaze::net {

struct Datagram {
    Endpoint    from;
    std::string data;   // binärsäker
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Port 0 = valfri. Frivillig för en avsändare: första `sendTo` öppnar
    // socketen i mottagarens adressfamilj och OS:et binder den själv.
    Result<void> bind(const Endpoint& local);

    Result<void> sendTo(const Endpoint& to, std::string_view data, std::stop_token stop,
                        std::chrono::milliseconds timeout = std::chrono::seconds{10});

    // Kräver en öppen socket (`bind` eller en tidigare `sendTo`).
    Result<Datagram> receive(std::stop_token stop,
                             std::chrono::milliseconds timeout = kForever);

    void close();
    bool isOpen() const { return handle_ != -1; }

    Result<Endpoint> localEndpoint() const;

private:
    Result<void> open(bool v6);

    std::intptr_t handle_ = -1;
    bool          v6_     = false;
};

} // namespace glaze::net
