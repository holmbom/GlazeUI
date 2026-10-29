#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>

#include "glaze_net/address.h"
#include "glaze_net/error.h"

// glaze_net — TCP.
//
// BLOCKERANDE med tidsgräns och avbrytning: varje anrop tar en
// `std::stop_token` och väntar högst `timeout`. Under ytan är socketen
// ALLTID icke-blockerande och väntan är `poll`/`select` på socketen PLUS en
// väckare som stop-callbacken skriver till — ett avbrott verkar alltså
// direkt, utan att någon tråd vaknar med jämna mellanrum för att titta
// (hård regel 1:s anda även utanför UI-tråden).
//
// I en GUI-app körs ett utbyte som vanlig, rak kod i en `Worker`:
//
//     bakgrund.run([](std::stop_token stop) -> glaze::net::Result<std::string> {
//         glaze::net::TcpSocket s;
//         if (auto r = s.connect("example.com", 7, stop); !r) return r.failure();
//         if (auto r = s.send("hej\n", stop); !r)             return r.failure();
//         return s.receive(stop);
//     }, [this](glaze::net::Result<std::string>& svar) { … });
//
// TRÅDAR: `send` och `receive` får köras samtidigt från två trådar (TCP är
// full duplex); allt annat — en tråd i taget. `close()` medan en annan tråd
// väntar i ett anrop är FÖRBJUDET: avbryt med stop_token i stället.

namespace glaze::net {

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    TcpSocket(const TcpSocket&)            = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Namn ELLER adress. Ett namn slås upp och adresserna prövas i OS:ets
    // ordning tills en svarar — HELA förloppet ryms inom `timeout`.
    Result<void> connect(std::string_view host, std::uint16_t port, std::stop_token stop,
                         std::chrono::milliseconds timeout = std::chrono::seconds{10});
    Result<void> connect(const Endpoint& to, std::stop_token stop,
                         std::chrono::milliseconds timeout = std::chrono::seconds{10});

    // Skickar ALLT, eller svarar med ett fel. Binärsäker.
    Result<void> send(std::string_view data, std::stop_token stop,
                      std::chrono::milliseconds timeout = std::chrono::seconds{10});

    // Tar emot det som finns: minst en byte, högst `max`. Har motparten
    // stängt svarar den `Code::Closed` — aldrig en tom sträng.
    Result<std::string> receive(std::stop_token stop,
                                std::chrono::milliseconds timeout = std::chrono::seconds{10},
                                std::size_t max = 65536);

    // Halvstängning: "inget mer från mig" (FIN). Mottagningen fortsätter.
    Result<void> shutdownSend();

    void close();
    bool isOpen() const { return handle_ != -1; }

    Result<Endpoint> localEndpoint() const;
    Result<Endpoint> remoteEndpoint() const;

private:
    friend class TcpListener;
    std::intptr_t handle_ = -1;   // OS-socketen; -1 = ingen (både fd och SOCKET)
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;
    TcpListener(const TcpListener&)            = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // Port 0 = valfri ledig port (läs den ur `localEndpoint()`). Lyssna på
    // `Address::loopbackV4()` när bara den egna datorn ska nå porten —
    // ett lyssnande på alla gränssnitt får Windows brandvägg att fråga.
    Result<void> listen(const Endpoint& local, int backlog = 16);

    Result<TcpSocket> accept(std::stop_token stop,
                             std::chrono::milliseconds timeout = kForever);

    void close();
    bool isOpen() const { return handle_ != -1; }

    Result<Endpoint> localEndpoint() const;

private:
    std::intptr_t handle_ = -1;
};

} // namespace glaze::net
