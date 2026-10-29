// net_gate — SIDOPAKETET Networking: adresser, DNS, TCP, UDP och PING.
// HTTP har en egen grind (http_gate).
//
// Kör:  ./build/tests/net_gate
// Exit: 0 = grönt · 1 = ett fall föll · 77 = PING nekades av OS:et (bara
//       det fallet kunde inte köras — se nedan) · ctest-timeouten fångar häng
//
// INGA EXTERNA VÄRDAR: allt går över loopback (127.0.0.1 och ::1) och mot
// "localhost". Länkar `glaze_net` (+ `glaze_threads`) — aldrig `glaze`.
//
// VAD DEN MÄTER:
//   Adress   tolkning/utskrift mot HANDSKRIVNA förväntade värden (RFC 5952-fallen står
//            utskrivna, inte härledda ur implementationen) och avslagen.
//   Väntan   det frikopplade OS-anropet (detached.h, som DNS och Windows ping
//            går genom) mot en FALSK långsam funktion: taket och avbrottet
//            verkar direkt i stället för att vänta ut anropet.
//   DNS      numerisk adress utan OS-anrop, "localhost" → bara loopback,
//            tomt namn → InvalidArgument, stoppad token → Cancelled.
//   TCP      utbyte i båda riktningar, 8 MiB binärdata mot en server som
//            väntar innan den läser (delvisa sändningar och väntan på
//            skrivbarhet), halvstängning → Closed, tak → Timeout, avbrott →
//            Cancelled DIREKT, stängd port → Refused, upptagen port →
//            AddressInUse, namnvägen via "localhost".
//   UDP      datagram fram och åter med rätt avsändare, IPv6, tak, avbrott,
//            fel adressfamilj, och att ett ICMP "port unreachable" INTE
//            förgiftar nästa mottagning (Windows SIO_UDP_CONNRESET).
//   PING     127.0.0.1 och ::1 — eller, om OS:et nekar ICMP-socketen, att
//            felet är Permission med sysctl-namnet i texten (aldrig en tyst
//            timeout); då svarar grinden 77 i stället för 0.
//   Worker   de asynkrona formerna (resolve/ping) och ett TCP-utbyte som
//            arbete — Result<T> hela vägen, aldrig Result<Result<T>>.
//
// VAD DEN INTE MÄTER: riktiga nät (inga externa värdar, inga routrar),
// ICMP-fel från en router, DNS-svar från en riktig namnserver, och det
// frikopplade anropets VERKLIGA OS-uppslag under ett avbrott (det kan inte
// framkallas utan en långsam namnserver — mekanismen mäts med en falsk).
// DELVISA SÄNDNINGAR mäts bara där OS:et ger dem: Linux delar upp de 8 MiB
// (sabotaget "allt gick iväg" fäller grinden där), men Windows loopback tar
// dem i EN icke-blockerande send — där är vägen omätt, och ingen
// datamängd ändrade det. macOS-delarna av PING (IPv4-huvudet i svaret,
// vår egen kontrollsumma) körs aldrig här: på Linux börjar svaret på
// ICMP-typen och kärnan räknar summan.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <glaze_net/net.h>

#include "detached.h"   // INTERN (Networking/src) — msgbox_gate-prejudikatet

namespace {

using namespace std::chrono_literals;
namespace net = glaze::net;
namespace gt  = glaze::threads;

int  g_fel       = 0;
bool g_pingNekad = false;

void check(bool ok, const std::string& vad)
{
    std::printf("%-6s %s\n", ok ? "OK" : "FEL", vad.c_str());
    if (!ok)
        ++g_fel;
}

using Klocka = std::chrono::steady_clock;

double sek(Klocka::time_point t0)
{
    return std::chrono::duration<double>(Klocka::now() - t0).count();
}

// Begär stopp efter en stund, från en egen tråd.
struct Avbrytare {
    std::stop_source källa;
    std::jthread     tråd;
    explicit Avbrytare(std::chrono::milliseconds efter)
        : tråd([this, efter] {
              std::this_thread::sleep_for(efter);
              källa.request_stop();
          })
    {}
    std::stop_token token() const { return källa.get_token(); }
};

// --------------------------------------------------------------- adresser

void adresser()
{
    struct Fall {
        const char* in;
        const char* ut;
    };
    // Förväntade värden skrivna för hand ur RFC 5952 — aldrig genererat av koden.
    const Fall fall[] = {
        {"127.0.0.1", "127.0.0.1"},
        {"0.0.0.0", "0.0.0.0"},
        {"::1", "::1"},
        {"::", "::"},
        {"[::1]", "::1"},
        {"2001:0db8:0000:0000:0001:0000:0000:0001", "2001:db8::1:0:0:1"},   // lika långa ⇒ första
        {"2001:db8:0:0:0:0:2:1", "2001:db8::2:1"},
        {"2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1"},                   // EN nollgrupp komprimeras inte
        {"2001:DB8::ABCD", "2001:db8::abcd"},                               // gemener
        {"::ffff:192.0.2.1", "::ffff:192.0.2.1"},
        {"1::", "1::"},
        {"fe80::1%3", "fe80::1%3"},
    };
    for (const Fall& f : fall) {
        const auto a = net::Address::parse(f.in);
        check(a && a->toString() == f.ut,
              std::string("adress: \"") + f.in + "\" → \"" + f.ut + "\"" +
                  (a ? " (fick \"" + a->toString() + "\")" : " (tolkades inte)"));
    }
    const char* ogiltiga[] = {"",        "256.1.1.1", "1.2.3",         "01.2.3.4", "1.2.3.4.",
                              "1::2::3", "12345::",   "fe80::1%eth0", "::g",      "1:2:3:4:5:6:7:8:9",
                              "[::1"};
    for (const char* t : ogiltiga)
        check(!net::Address::parse(t), std::string("adress: \"") + t + "\" vägras");

    check(net::Endpoint{net::Address::loopbackV6(), 80}.toString() == "[::1]:80" &&
              net::Endpoint{net::Address::loopbackV4(), 8080}.toString() == "127.0.0.1:8080",
          "Endpoint::toString med hakparenteser för IPv6");
    check(net::Address::loopbackV4().isLoopback() && net::Address::loopbackV6().isLoopback() &&
              net::Address::parse("::ffff:127.0.0.9")->isLoopback() &&
              !net::Address::parse("10.0.0.1")->isLoopback(),
          "isLoopback: 127/8, ::1 och IPv4-mappad 127/8");
}

// ------------------------------------------- det frikopplade OS-anropet

void frikopplat()
{
    // Taket: anropet tar 2 s, taket är 150 ms.
    auto t0 = Klocka::now();
    auto r  = net::sys::runDetached<int>(
        [] {
            std::this_thread::sleep_for(2s);
            return net::Result<int>(1);
        },
        net::sys::deadlineFrom(150ms), {}, "falskt anrop");
    check(!r && r.code() == net::Code::Timeout && sek(t0) < 1.0,
          "frikopplat anrop: taket verkar direkt (" + r.error() + ")");

    // Avbrottet: stopp efter 100 ms, inget tak.
    Avbrytare avbryt(100ms);
    t0 = Klocka::now();
    r  = net::sys::runDetached<int>(
        [] {
            std::this_thread::sleep_for(2s);
            return net::Result<int>(1);
        },
        net::sys::Deadline::max(), avbryt.token(), "falskt anrop");
    check(r.cancelled() && sek(t0) < 1.0, "frikopplat anrop: avbrottet verkar direkt");

    // Svaret och felet kommer fram oförändrade.
    r = net::sys::runDetached<int>([] { return net::Result<int>(42); },
                                   net::sys::deadlineFrom(5s), {}, "falskt anrop");
    check(r && *r == 42, "frikopplat anrop: värdet når anroparen");
}

// --------------------------------------------------------------------- DNS

void dns()
{
    auto r = net::resolve("127.0.0.1", {});
    check(r && r->size() == 1 && (*r)[0] == net::Address::loopbackV4(),
          "resolve: numerisk adress svaras direkt");
    r = net::resolve("[::1]", {});
    check(r && r->size() == 1 && (*r)[0] == net::Address::loopbackV6(),
          "resolve: [::1] tolkas som adress");

    r                 = net::resolve("localhost", {});
    bool allaLoopback = r && !r->empty();
    for (std::size_t i = 0; r && i < r->size(); ++i)
        allaLoopback = allaLoopback && (*r)[i].isLoopback();
    check(allaLoopback, "resolve: \"localhost\" ger bara loopback-adresser" +
                            (r ? std::string() : " (" + r.error() + ")"));

    r = net::resolve("", {});
    check(!r && r.code() == net::Code::InvalidArgument, "resolve: tomt namn → InvalidArgument");

    std::stop_source stoppad;
    stoppad.request_stop();
    r = net::resolve("localhost", stoppad.get_token());
    check(r.cancelled(), "resolve: stoppad token → Cancelled");
}

// --------------------------------------------------------------------- TCP

void tcp()
{
    net::TcpListener lyssnare;
    auto             l = lyssnare.listen({net::Address::loopbackV4(), 0});
    check(l.ok(), "listen på 127.0.0.1:0" + (l ? std::string() : " (" + l.error() + ")"));
    if (!l)
        return;
    const auto port = lyssnare.localEndpoint()->port;
    check(port != 0, "valfri port tilldelades (" + std::to_string(port) + ")");

    // 8 MiB med alla bytevärden, och en server som VÄNTAR innan den läser —
    // sändbufferten fylls, så sändningen måste vänta på skrivbarhet och
    // delas upp. (256 KiB räckte inte: Windows loopback tog dem i EN
    // icke-blockerande send, och ett sabotage som låtsades att allt gick
    // iväg passerade grönt.)
    std::string stor(8 * 1024 * 1024, '\0');
    for (std::size_t i = 0; i < stor.size(); ++i)
        stor[i] = static_cast<char>(i * 31 % 256);

    std::string mottagetAvServern;
    bool        serverSågClosed = false;
    std::jthread server([&](std::stop_token stopp) {
        auto s = lyssnare.accept(stopp, 10s);
        if (!s)
            return;
        std::this_thread::sleep_for(300ms);
        while (mottagetAvServern.size() < 3 + stor.size()) {
            auto d = s->receive(stopp, 10s);
            if (!d)
                return;
            mottagetAvServern += *d;
        }
        (void)s->send("svar", stopp);
        (void)s->send(stor, stopp, 20s);
        auto slut       = s->receive(stopp, 10s);   // klienten halvstänger
        serverSågClosed = !slut && slut.code() == net::Code::Closed;
    });

    net::TcpSocket klient;
    auto           c = klient.connect({net::Address::loopbackV4(), port}, {});
    check(c.ok(), "connect till 127.0.0.1" + (c ? std::string() : " (" + c.error() + ")"));
    if (!c)
        return;
    check(klient.remoteEndpoint() && klient.remoteEndpoint()->port == port,
          "remoteEndpoint är lyssnarens port");
    check(klient.send("hej", {}).ok() && klient.send(stor, {}, 20s).ok(),
          "send: 3 byte + 8 MiB");

    std::string svar;
    while (svar.size() < 4 + stor.size()) {
        auto d = klient.receive({}, 10s);
        if (!d)
            break;
        svar += *d;
    }
    check(svar.size() == 4 + stor.size() && svar.compare(0, 4, "svar") == 0 &&
              svar.compare(4, std::string::npos, stor) == 0,
          "receive: svaret och 8 MiB binärdata kom fram byte för byte");
    check(klient.shutdownSend().ok(), "shutdownSend");
    server.join();
    check(mottagetAvServern.size() == 3 + stor.size() &&
              mottagetAvServern.compare(0, 3, "hej") == 0 &&
              mottagetAvServern.compare(3, std::string::npos, stor) == 0,
          "servern fick klientens bytes oförändrade");
    check(serverSågClosed, "halvstängning ⇒ motparten får Code::Closed (aldrig tom sträng)");

    // Ett andra utbyte: tak och avbrott på en ansluten socket som inte får
    // något.
    std::jthread tyst([&](std::stop_token stopp) {
        auto s = lyssnare.accept(stopp, 10s);
        if (s)
            (void)s->receive(stopp, 10s);   // håll anslutningen öppen, skicka inget
    });
    net::TcpSocket k2;
    if (k2.connect({net::Address::loopbackV4(), port}, {})) {
        auto t0 = Klocka::now();
        auto r  = k2.receive({}, 200ms);
        check(!r && r.code() == net::Code::Timeout && sek(t0) >= 0.15 && sek(t0) < 2.0,
              "receive med tak 200 ms → Timeout efter taket");
        Avbrytare avbryt(150ms);
        t0 = Klocka::now();
        r  = k2.receive(avbryt.token(), net::kForever);
        check(r.cancelled() && sek(t0) < 2.0,
              "receive utan tak avbryts DIREKT av stop_token (" + std::to_string(sek(t0)) +
                  " s)");
    } else {
        check(false, "andra anslutningen");
    }
    k2.close();
    tyst.request_stop();
    tyst.join();

    // Stängd port → Refused. En port som nyss var bunden och nu är stängd.
    std::uint16_t stängdPort = 0;
    {
        net::TcpListener tillfällig;
        if (tillfällig.listen({net::Address::loopbackV4(), 0}))
            stängdPort = tillfällig.localEndpoint()->port;
    }
    net::TcpSocket k3;
    auto           t0 = Klocka::now();
    auto           r3 = k3.connect({net::Address::loopbackV4(), stängdPort}, {}, 10s);
    check(!r3 && r3.code() == net::Code::Refused,
          "connect till stängd port → Refused (" + r3.error() + ", " + std::to_string(sek(t0)) +
              " s)");

    // Upptagen port → AddressInUse.
    net::TcpListener dubblett;
    auto             d = dubblett.listen({net::Address::loopbackV4(), port});
    check(!d && d.code() == net::Code::AddressInUse,
          "listen på upptagen port → AddressInUse" + (d ? std::string() : " (" + d.error() + ")"));

    std::stop_source stoppad;
    stoppad.request_stop();
    net::TcpSocket k4;
    check(k4.connect({net::Address::loopbackV4(), port}, stoppad.get_token()).cancelled(),
          "connect med stoppad token → Cancelled");
    net::TcpSocket oansluten;
    check(oansluten.send("x", {}).code() == net::Code::InvalidArgument &&
              oansluten.receive({}).code() == net::Code::InvalidArgument,
          "send/receive på oansluten socket → InvalidArgument");
}

// Namnvägen: "localhost" mot en lyssnare på BÅDA loopbackadresserna (namnet
// kan ge ::1 först, och det ska inte bero på vilken familj OS:et väljer).
void tcpNamn()
{
    net::TcpListener v4, v6;
    if (!v4.listen({net::Address::loopbackV4(), 0})) {
        check(false, "lyssnare för namnvägen");
        return;
    }
    const auto port   = v4.localEndpoint()->port;
    const bool harV6  = v6.listen({net::Address::loopbackV6(), port}).ok();
    std::jthread svar4([&](std::stop_token stopp) {
        if (auto s = v4.accept(stopp, 10s))
            (void)s->send("v4", stopp);
    });
    std::jthread svar6([&](std::stop_token stopp) {
        if (!harV6)
            return;
        if (auto s = v6.accept(stopp, 10s))
            (void)s->send("v6", stopp);
    });
    net::TcpSocket k;
    auto           c = k.connect("localhost", port, {});
    auto           d = c ? k.receive({}, 5s) : net::Result<std::string>(c.failure());
    check(c && d && (*d == "v4" || *d == "v6"),
          "connect(\"localhost\", port) → slog upp och anslöt (" + (d ? *d : d.error()) + ")");
    k.close();
    svar4.request_stop();
    svar6.request_stop();
}

// --------------------------------------------------------------------- UDP

void udp()
{
    net::UdpSocket a, b;
    check(a.bind({net::Address::loopbackV4(), 0}).ok() &&
              b.bind({net::Address::loopbackV4(), 0}).ok(),
          "två UDP-sockets binder 127.0.0.1:0");
    const auto ea = *a.localEndpoint();
    const auto eb = *b.localEndpoint();
    const std::string data("ping\0binärt", sizeof("ping\0binärt") - 1);
    check(a.sendTo(eb, data, {}).ok(), "sendTo");
    auto d = b.receive({}, 5s);
    check(d && d->data == data && d->from == ea,
          "receive: datagrammet (binärsäkert) med rätt avsändare");

    auto t0 = Klocka::now();
    auto r  = b.receive({}, 200ms);
    check(!r && r.code() == net::Code::Timeout && sek(t0) >= 0.15,
          "receive med tak → Timeout");
    Avbrytare avbryt(150ms);
    t0 = Klocka::now();
    r  = b.receive(avbryt.token(), net::kForever);
    check(r.cancelled() && sek(t0) < 2.0, "receive utan tak avbryts DIREKT");

    // Ett datagram till en port där ingen lyssnar ger ett ICMP "port
    // unreachable". Nästa mottagning ska ändå bara vänta ut sitt tak — på
    // Windows förgiftades den annars av WSAECONNRESET.
    std::uint16_t död = 0;
    {
        net::UdpSocket tillfällig;
        if (tillfällig.bind({net::Address::loopbackV4(), 0}))
            död = tillfällig.localEndpoint()->port;
    }
    (void)a.sendTo({net::Address::loopbackV4(), död}, "ingen där", {});
    std::this_thread::sleep_for(50ms);
    r = a.receive({}, 200ms);
    check(!r && r.code() == net::Code::Timeout,
          "ICMP port unreachable förgiftar inte nästa receive (" + r.error() + ")");

    check(a.sendTo({net::Address::loopbackV6(), 9}, "x", {}).code() ==
              net::Code::InvalidArgument,
          "IPv4-socket mot IPv6-adress → InvalidArgument");
    net::UdpSocket oöppnad;
    check(oöppnad.receive({}, 10ms).code() == net::Code::InvalidArgument,
          "receive på oöppnad socket → InvalidArgument");

    net::UdpSocket c6, d6;
    if (c6.bind({net::Address::loopbackV6(), 0}) && d6.bind({net::Address::loopbackV6(), 0})) {
        (void)c6.sendTo(*d6.localEndpoint(), "sex", {});
        auto m = d6.receive({}, 5s);
        check(m && m->data == "sex" && m->from.address == net::Address::loopbackV6(),
              "UDP över ::1");
    } else {
        std::printf("  --   ::1 saknas — IPv6-fallet för UDP hoppas över\n");
    }
}

// -------------------------------------------------------------------- PING

void ping()
{
    auto r = net::ping("127.0.0.1", {}, 2s);
    if (!r && r.code() == net::Code::Permission) {
        g_pingNekad = true;
        check(r.error().find("ping_group_range") != std::string::npos ||
                  r.error().find("nekas") != std::string::npos,
              "ping nekad av OS:et → Permission med förklaringen i texten");
        std::printf("  --   ping nekas här (%s) — svarsfallen kan inte köras\n",
                    r.error().c_str());
        return;
    }
    check(r && r->address == net::Address::loopbackV4() && r->time < 1s,
          "ping 127.0.0.1" + (r ? " (" + std::to_string(r->time.count()) + " µs)"
                                : " (" + r.error() + ")"));
    r = net::ping("::1", {}, 2s);
    check(r && r->address == net::Address::loopbackV6(),
          "ping ::1" + (r ? std::string() : " (" + r.error() + ")"));
    r = net::ping("localhost", {}, 2s);
    check(r && r->address.isLoopback(), "ping localhost (namnvägen)");

    std::stop_source stoppad;
    stoppad.request_stop();
    check(net::ping("127.0.0.1", stoppad.get_token()).cancelled(),
          "ping med stoppad token → Cancelled");
    check(net::ping("", {}).code() == net::Code::InvalidArgument, "ping utan värd → InvalidArgument");
}

// ------------------------------------------------------------------ Worker

void asynkront()
{
    gt::Pool   pool{2};
    gt::Worker w{pool, gt::workerContext()};

    std::atomic<bool> dnsOk{false};
    net::resolve(w, "localhost", [&](net::Result<std::vector<net::Address>>& r) {
        dnsOk = r && !r->empty();
    });
    std::atomic<int> pingKod{-1};
    net::ping(w, "127.0.0.1", [&](net::Result<net::PingReply>& r) {
        pingKod = r ? 0 : r.code().value();
    });

    // Ett TCP-utbyte som vanligt rakt arbete — Result<std::string> hela vägen.
    net::TcpListener l;
    (void)l.listen({net::Address::loopbackV4(), 0});
    const auto   port = l.localEndpoint() ? l.localEndpoint()->port : std::uint16_t{0};
    std::jthread server([&](std::stop_token stopp) {
        if (auto s = l.accept(stopp, 10s))
            (void)s->send("från servern", stopp);
    });
    std::string svar;
    w.run([port](std::stop_token stopp) -> net::Result<std::string> {
        net::TcpSocket s;
        if (auto c = s.connect({net::Address::loopbackV4(), port}, stopp); !c)
            return c.failure();
        return s.receive(stopp);
    },
          [&](net::Result<std::string>& r) { svar = r ? *r : r.error(); });
    w.wait();

    check(dnsOk, "resolve(worker, …): svaret når fortsättningen");
    check(pingKod == 0 || (g_pingNekad && pingKod == static_cast<int>(net::Code::Permission)),
          "ping(worker, …): svaret når fortsättningen");
    check(svar == "från servern", "TCP-utbyte som arbete: Result<std::string> plattas ut");

    // Avbrott mitt i en blockerande mottagning: arbetet återvänder direkt,
    // fortsättningen körs aldrig.
    net::TcpListener tyst;
    (void)tyst.listen({net::Address::loopbackV4(), 0});
    const auto   tystPort = tyst.localEndpoint() ? tyst.localEndpoint()->port : std::uint16_t{0};
    std::jthread tystServer([&](std::stop_token stopp) {
        if (auto s = tyst.accept(stopp, 10s))
            (void)s->receive(stopp, 10s);
    });
    std::atomic<bool> startad{false};
    std::atomic<bool> levererad{false};
    w.run([&startad, tystPort](std::stop_token stopp) -> net::Result<std::string> {
        net::TcpSocket s;
        if (auto c = s.connect({net::Address::loopbackV4(), tystPort}, stopp); !c)
            return c.failure();
        startad = true;
        return s.receive(stopp, net::kForever);
    },
          [&](net::Result<std::string>&) { levererad = true; });
    while (!startad)
        std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(50ms);
    const auto t0 = Klocka::now();
    w.cancel();
    w.wait();
    check(sek(t0) < 2.0 && !levererad,
          "Worker::cancel() under en blockerande receive: tillbaka direkt, ingen leverans");
    tystServer.request_stop();
}

} // namespace

int main()
{
    std::printf("=== net_gate — sidopaketet Networking (adresser, DNS, TCP, UDP, PING) ===\n");
    adresser();
    frikopplat();
    dns();
    tcp();
    tcpNamn();
    udp();
    ping();
    asynkront();
    std::printf("\nnet_gate: %s (%d fel)%s\n", g_fel ? "FÄLLD" : "GRÖN", g_fel,
                g_pingNekad ? " — PING NEKAD, svarsfallen ej körda" : "");
    if (g_fel)
        return 1;
    return g_pingNekad ? 77 : 0;
}
