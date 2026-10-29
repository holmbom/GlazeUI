// http_gate — SIDOPAKETET Networking: HTTP genom OS:ETS stack.
//
// Kör:  ./build/tests/http_gate
// Exit: 0 = grönt · 1 = ett fall föll · ctest-timeouten fångar ett häng
//
// Grinden startar SIN EGEN server: en liten HTTP/1.1-server byggd på
// paketets eget TCP-lager, på 127.0.0.1 OCH ::1 (samma port), och klienten
// går mot "http://localhost:<port>". Namnet och inte en IP-adress, för att
// macOS App Transport Security från macOS 14 stoppar http mot en IP-adress
// men släpper igenom ett okvalificerat namn. INGA externa värdar.
//
// VAD DEN MÄTER — klienten genom den stack plattformen har (WinHTTP här,
// libcurl på Linux, NSURLSession på macOS):
//   status, kropp och headers (namn utan skiftläge), binär kropp byte för
//   byte, 404 som SVAR och inte fel, omdirigering följd (relativ och
//   absolut, slutlig url), INTE följd när den stängts av, för många
//   omdirigeringar → Protocol, POST med kropp och Content-Type (och den
//   enhetliga default-typen när ingen anges), egen header, chunkad kropp,
//   HEAD, inga automatiska kakor (WinHTTP skickar annars tillbaka en Set-Cookie;
//   fallet fäller det), tak → Timeout, avbrott →
//   Cancelled, stängd port → Refused,
//   valideringen (url, metod, header-injektion, GET med kropp), och den
//   asynkrona formen via en Worker.
//
// VAD DEN INTE MÄTER: HTTPS (kräver en riktig server med ett riktigt
// certifikat — en rad per OS i doc/os/*/TESTS.md), proxy, avkomprimering
// (servern skickar aldrig gzip) och nedgraderingsspärren https → http (den
// kräver https). Avbrottets LATENS mäts med tak i sekunder, inte i ms —
// libcurl avbryter först vid nästa förloppsanrop (högst ~1 s).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glaze_net/net.h>

namespace {

using namespace std::chrono_literals;
namespace net = glaze::net;
namespace gt  = glaze::threads;

int g_fel = 0;

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

bool slutarMed(const std::string& t, const std::string& slut)
{
    return t.size() >= slut.size() && t.compare(t.size() - slut.size(), slut.size(), slut) == 0;
}

std::string binärKropp()
{
    std::string b(16 * 1024, '\0');
    for (std::size_t i = 0; i < b.size(); ++i)
        b[i] = static_cast<char>(i % 256);
    return b;
}

// ------------------------------------------------------------ servern

struct Inkommande {
    std::string                    metod;
    std::string                    sökväg;
    std::vector<net::Header>       headers;
    std::string                    kropp;

    std::string header(const std::string& namn) const
    {
        net::HttpResponse r;   // återanvänder paketets skiftlägesokänsliga uppslag
        r.headers = headers;
        return r.header(namn);
    }
};

class Server {
public:
    bool starta()
    {
        if (!v4_.listen({net::Address::loopbackV4(), 0}))
            return false;
        port_ = v4_.localEndpoint()->port;
        // ::1 på SAMMA port, så att "localhost" fungerar vilken familj OS:et
        // än väljer. Saknas IPv6 räcker IPv4 (namnet ger då bara 127.0.0.1).
        const bool v6 = v6_.listen({net::Address::loopbackV6(), port_}).ok();
        mottagare_.emplace_back([this](std::stop_token s) { taEmot(v4_, s); });
        if (v6)
            mottagare_.emplace_back([this](std::stop_token s) { taEmot(v6_, s); });
        return true;
    }

    ~Server()
    {
        for (auto& t : mottagare_)
            t.request_stop();
        mottagare_.clear();   // joinar
        std::lock_guard<std::mutex> lås(m_);
        for (auto& t : anslutningar_)
            t.request_stop();
        anslutningar_.clear();   // joinar
    }

    std::uint16_t port() const { return port_; }
    std::string   bas() const { return "http://localhost:" + std::to_string(port_); }

private:
    void taEmot(net::TcpListener& l, std::stop_token stopp)
    {
        for (;;) {
            auto s = l.accept(stopp);
            if (!s)
                return;   // avbrutet (servern rivs)
            std::lock_guard<std::mutex> lås(m_);
            anslutningar_.emplace_back(
                [this, sock = std::move(*s)](std::stop_token st) mutable {
                    hantera(std::move(sock), st);
                });
        }
    }

    static bool läs(net::TcpSocket& s, Inkommande& in, std::stop_token stopp)
    {
        std::string buf;
        std::size_t huvudSlut = std::string::npos;
        while ((huvudSlut = buf.find("\r\n\r\n")) == std::string::npos) {
            auto d = s.receive(stopp, 10s);
            if (!d)
                return false;
            buf += *d;
        }
        const std::size_t radSlut = buf.find("\r\n");
        const std::string första  = buf.substr(0, radSlut);
        const std::size_t m1      = första.find(' ');
        const std::size_t m2      = första.find(' ', m1 + 1);
        in.metod                  = första.substr(0, m1);
        in.sökväg                 = första.substr(m1 + 1, m2 - m1 - 1);
        std::size_t i             = radSlut + 2;
        while (i < huvudSlut) {
            const std::size_t slut  = buf.find("\r\n", i);
            const std::string rad   = buf.substr(i, slut - i);
            const std::size_t kolon = rad.find(':');
            if (kolon != std::string::npos) {
                std::size_t v = kolon + 1;
                while (v < rad.size() && rad[v] == ' ')
                    ++v;
                in.headers.push_back({rad.substr(0, kolon), rad.substr(v)});
            }
            i = slut + 2;
        }
        in.kropp                  = buf.substr(huvudSlut + 4);
        const std::string längd   = in.header("Content-Length");
        const std::size_t behövs  = längd.empty() ? 0 : std::stoul(längd);
        while (in.kropp.size() < behövs) {
            auto d = s.receive(stopp, 10s);
            if (!d)
                return false;
            in.kropp += *d;
        }
        return true;
    }

    static void svara(net::TcpSocket& s, const Inkommande& in, int status, const char* text,
                      const std::string& kropp, const std::string& extra,
                      std::stop_token stopp)
    {
        std::string ut = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\n" +
                         "Content-Length: " + std::to_string(kropp.size()) + "\r\n" +
                         "Connection: close\r\n" + extra + "\r\n";
        if (in.metod != "HEAD")
            ut += kropp;
        (void)s.send(ut, stopp);
    }

    void hantera(net::TcpSocket s, std::stop_token stopp)
    {
        Inkommande in;
        if (!läs(s, in, stopp))
            return;
        const std::string& p = in.sökväg;
        if (p == "/hej") {
            svara(s, in, 200, "OK", "hej världen",
                  "Content-Type: text/plain; charset=utf-8\r\nX-Glaze: ja\r\n", stopp);
        } else if (p == "/binar") {
            svara(s, in, 200, "OK", binärKropp(), "Content-Type: application/octet-stream\r\n",
                  stopp);
        } else if (p == "/omdirigera") {
            svara(s, in, 302, "Found", "", "Location: /hej\r\n", stopp);
        } else if (p == "/absolut") {
            svara(s, in, 302, "Found", "", "Location: " + bas() + "/hej\r\n", stopp);
        } else if (p.rfind("/kedja/", 0) == 0) {
            const int n = std::stoi(p.substr(7));
            if (n > 0)
                svara(s, in, 302, "Found", "",
                      "Location: /kedja/" + std::to_string(n - 1) + "\r\n", stopp);
            else
                svara(s, in, 200, "OK", "slut", "", stopp);
        } else if (p == "/eko") {
            svara(s, in, 200, "OK", in.kropp,
                  "X-Typ: " + in.header("Content-Type") + "\r\nX-Metod: " + in.metod + "\r\n",
                  stopp);
        } else if (p == "/eko-header") {
            svara(s, in, 200, "OK", in.header("X-Test"), "", stopp);
        } else if (p == "/kaka") {
            svara(s, in, 200, "OK", "satt", "Set-Cookie: glaze=1; Path=/\r\n", stopp);
        } else if (p == "/kaka-eko") {
            svara(s, in, 200, "OK", in.header("Cookie"), "", stopp);
        } else if (p == "/chunkad") {
            (void)s.send("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                         "Connection: close\r\n\r\n3\r\nabc\r\n3\r\ndef\r\n3\r\nghi\r\n0\r\n\r\n",
                         stopp);
        } else if (p == "/seg") {
            // Svarar först efter 3 s — men släpper direkt när servern rivs.
            std::mutex                   m;
            std::condition_variable_any  cv;
            std::unique_lock<std::mutex> lås(m);
            cv.wait_for(lås, stopp, 3s, [] { return false; });
            svara(s, in, 200, "OK", "seg", "", stopp);
        } else {
            svara(s, in, 404, "Not Found", "finns inte", "", stopp);
        }
        (void)s.shutdownSend();
    }

    net::TcpListener         v4_, v6_;
    std::uint16_t            port_ = 0;
    std::mutex               m_;
    std::list<std::jthread>  anslutningar_;
    std::vector<std::jthread> mottagare_;
};

// ------------------------------------------------------------- fallen

void grund(const Server& srv)
{
    const std::string bas = srv.bas();
    auto              r   = net::get(bas + "/hej", {});
    check(r && r->status == 200 && r->body == "hej världen",
          "GET /hej → 200 och kroppen" + (r ? std::string() : " (" + r.error() + ")"));
    if (r) {
        check(r->header("x-glaze") == "ja" && r->header("X-GLAZE") == "ja",
              "header(): namnet slås upp utan skiftläge");
        check(r->success() && slutarMed(r->url, "/hej"), "success() och slutlig url");
    }

    r = net::get(bas + "/binar", {});
    check(r && r->body == binärKropp(), "binär kropp (16 KiB, alla bytevärden) byte för byte");

    r = net::get(bas + "/saknas", {});
    check(r && r->status == 404 && !r->success() && r->body == "finns inte",
          "404 är ett SVAR (r.ok()), inte ett fel");

    r = net::get(bas + "/omdirigera", {});
    check(r && r->status == 200 && r->body == "hej världen" && slutarMed(r->url, "/hej"),
          "relativ omdirigering följs; url är den slutliga");
    r = net::get(bas + "/absolut", {});
    check(r && r->status == 200 && slutarMed(r->url, "/hej"), "absolut omdirigering följs");

    net::HttpRequest utan;
    utan.url             = bas + "/omdirigera";
    utan.followRedirects = false;
    r                    = net::request(utan, {});
    check(r && r->status == 302 && r->header("Location") == "/hej",
          "followRedirects = false ⇒ 3xx-svaret självt");

    r = net::get(bas + "/kedja/5", {});
    check(r && r->status == 200 && r->body == "slut", "fem omdirigeringar i rad följs");
    r = net::get(bas + "/kedja/12", {});
    check(!r && r.code() == net::Code::Protocol,
          "fler än 10 omdirigeringar → Protocol" + (r ? std::string(" (fick ett svar)")
                                                       : " (" + r.error() + ")"));
}

void kroppar(const Server& srv)
{
    const std::string bas = srv.bas();
    auto r = net::post(bas + "/eko", std::string("a\0b", 3), "text/plain", {});
    check(r && r->body == std::string("a\0b", 3) && r->header("X-Typ") == "text/plain" &&
              r->header("X-Metod") == "POST",
          "POST: kroppen (binärsäker) och Content-Type fram och åter");
    r = net::post(bas + "/eko", "utan typ", "", {});
    check(r && r->header("X-Typ") == "application/octet-stream",
          "POST utan Content-Type får den enhetliga application/octet-stream");

    net::HttpRequest put;
    put.method = "PUT";
    put.url    = bas + "/eko";
    put.body   = "lagrat";
    r          = net::request(put, {});
    check(r && r->body == "lagrat" && r->header("X-Metod") == "PUT", "PUT med kropp");

    net::HttpRequest egen;
    egen.url = bas + "/eko-header";
    egen.headers.push_back({"X-Test", "varde 1"});
    r = net::request(egen, {});
    check(r && r->body == "varde 1", "egen header når servern");
    egen.headers.back().value = "värde 1";
    check(net::request(egen, {}).code() == net::Code::InvalidArgument,
          "icke-ASCII i ett headervärde → InvalidArgument (stackarna kodar det olika)");

    r = net::get(bas + "/chunkad", {});
    check(r && r->body == "abcdefghi", "chunkad kropp sätts ihop");

    // Inga automatiska kakor (likriktningen): en Set-Cookie i ett svar får
    // aldrig komma tillbaka i nästa förfrågan.
    auto kaka = net::get(bas + "/kaka", {});
    r         = net::get(bas + "/kaka-eko", {});
    check(kaka && kaka->header("Set-Cookie").rfind("glaze=1", 0) == 0 && r && r->body.empty(),
          "inga automatiska kakor: Set-Cookie skickas inte tillbaka" +
              (r ? (r->body.empty() ? std::string() : " (fick \"" + r->body + "\")") : ""));

    net::HttpRequest huvud;
    huvud.method = "HEAD";
    huvud.url    = bas + "/hej";
    r            = net::request(huvud, {});
    check(r && r->status == 200 && r->body.empty(), "HEAD: status utan kropp");
}

void fel(const Server& srv)
{
    const std::string bas = srv.bas();

    auto t0 = Klocka::now();
    auto r  = net::get(bas + "/seg", {}, 500ms);
    check(!r && r.code() == net::Code::Timeout && sek(t0) >= 0.4 && sek(t0) < 2.5,
          "tak 500 ms mot en server som svarar efter 3 s → Timeout (" +
              std::to_string(sek(t0)) + " s)");

    std::stop_source avbryt;
    std::jthread     avbrytare([&] {
        std::this_thread::sleep_for(200ms);
        avbryt.request_stop();
    });
    t0 = Klocka::now();
    r  = net::get(bas + "/seg", avbryt.get_token());
    check(r.cancelled() && sek(t0) < 2.5,
          "avbrott under väntan → Cancelled (" + std::to_string(sek(t0)) + " s)");
    avbrytare.join();

    std::uint16_t stängd = 0;
    {
        net::TcpListener tillfällig;
        if (tillfällig.listen({net::Address::loopbackV4(), 0}))
            stängd = tillfällig.localEndpoint()->port;
    }
    r = net::get("http://localhost:" + std::to_string(stängd) + "/", {}, 15s);
    check(!r && r.code() == net::Code::Refused,
          "stängd port → Refused" + (r ? std::string(" (fick ett svar)") : " (" + r.error() + ")"));

    const char* ogiltiga[] = {"ftp://localhost/", "http://", "http://a b/", "http://user@localhost/",
                              "http://localhost:0/", "http://localhost:99999/", "http://[::1/",
                              "localhost/hej", "http://åäö.se/"};
    for (const char* u : ogiltiga) {
        auto o = net::get(u, {});
        check(!o && o.code() == net::Code::InvalidArgument,
              std::string("ogiltig url vägras: \"") + u + "\"");
    }

    net::HttpRequest getKropp;
    getKropp.url  = bas + "/hej";
    getKropp.body = "x";
    check(net::request(getKropp, {}).code() == net::Code::InvalidArgument,
          "GET med kropp → InvalidArgument (samma på alla tre stackar)");
    net::HttpRequest injektion;
    injektion.url = bas + "/hej";
    injektion.headers.push_back({"X-Test", "a\r\nX-Smugglad: ja"});
    check(net::request(injektion, {}).code() == net::Code::InvalidArgument,
          "CR/LF i ett headervärde → InvalidArgument (ingen header-injektion)");
    net::HttpRequest metod;
    metod.method = "GE T";
    metod.url    = bas + "/hej";
    check(net::request(metod, {}).code() == net::Code::InvalidArgument, "ogiltig metod vägras");

    std::stop_source stoppad;
    stoppad.request_stop();
    check(net::get(bas + "/hej", stoppad.get_token()).cancelled(),
          "stoppad token före start → Cancelled");

    check(net::percentEncode("vägar å/&=x") == "v%C3%A4gar%20%C3%A5%2F%26%3Dx" &&
              net::percentEncode("A-z_0.9~") == "A-z_0.9~",
          "percentEncode: UTF-8 per byte, de oreserverade orörda");
}

void asynkront(const Server& srv)
{
    gt::Pool         pool{2};
    gt::Worker       w{pool, gt::workerContext()};
    std::atomic<int> status{0};
    std::string      kropp;
    net::get(w, srv.bas() + "/hej", [&](net::Result<net::HttpResponse>& r) {
        if (r) {
            kropp  = r->body;
            status = r->status;
        }
    });
    w.wait();
    check(status == 200 && kropp == "hej världen",
          "get(worker, url, klar): svaret når fortsättningen som Result<HttpResponse>");
}

} // namespace

int main()
{
    std::printf("=== http_gate — sidopaketet Networking, HTTP genom OS:ets stack ===\n");
    Server srv;
    if (!srv.starta()) {
        std::printf("FEL    servern kunde inte starta på loopback\n");
        return 1;
    }
    std::printf("  server: %s (127.0.0.1 och ::1)\n", srv.bas().c_str());
    grund(srv);
    kroppar(srv);
    fel(srv);
    asynkront(srv);
    std::printf("\nhttp_gate: %s (%d fel)\n", g_fel ? "FÄLLD" : "GRÖN", g_fel);
    return g_fel ? 1 : 0;
}
