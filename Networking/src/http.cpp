// glaze_net — HTTP-lagrets GEMENSAMMA del: validering, url-tolkning och
// likriktningen mellan de tre OS-stackarna. Kontraktet står i http.h; OS:ets
// del bor i http_winhttp.cpp / http_apple.mm / http_curl.cpp.

#include "glaze_net/http.h"

#include "http_impl.h"
#include "sys.h"

namespace glaze::net {
namespace {

char gemen(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool likaUtanSkiftläge(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (gemen(a[i]) != gemen(b[i]))
            return false;
    return true;
}

bool börjarMed(std::string_view t, std::string_view början)
{
    return t.size() >= början.size() && likaUtanSkiftläge(t.substr(0, början.size()), början);
}

// RFC 9110 `tchar` — tecknen ett metodnamn och ett headernamn får bestå av.
bool ärTecken(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    return std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

bool ärToken(std::string_view t)
{
    if (t.empty())
        return false;
    for (char c : t)
        if (!ärTecken(c))
            return false;
    return true;
}

Error ogiltig(const std::string& varför) { return Error{Code::InvalidArgument, varför}; }

} // namespace

std::string HttpResponse::header(std::string_view name) const
{
    for (const Header& h : headers)
        if (likaUtanSkiftläge(h.name, name))
            return h.value;
    return {};
}

std::string percentEncode(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string           ut;
    ut.reserve(text.size());
    for (char c : text) {
        const auto b = static_cast<unsigned char>(c);
        if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') ||
            b == '-' || b == '.' || b == '_' || b == '~') {
            ut += c;
        } else {
            ut += '%';
            ut += kHex[b >> 4];
            ut += kHex[b & 0x0F];
        }
    }
    return ut;
}

namespace detail {

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

Result<Url> parseUrl(std::string_view text)
{
    // ASCII utan mellanslag eller styrtecken. En stack som procentkodar åt
    // en och en som vägrar hade gjort samma url till två olika förfrågningar.
    for (char c : text) {
        const auto b = static_cast<unsigned char>(c);
        if (b <= 0x20 || b >= 0x7F)
            return ogiltig("url:en ska vara ASCII utan mellanslag — procentkoda värden med "
                           "glaze::net::percentEncode");
    }
    Url u;
    std::size_t i = 0;
    if (börjarMed(text, "https://")) {
        u.https = true;
        i       = 8;
    } else if (börjarMed(text, "http://")) {
        i = 7;
    } else {
        return ogiltig("bara http:// och https:// stöds");
    }

    const std::size_t auktSlut = text.find_first_of("/?#", i);
    const std::string_view auktoritet =
        text.substr(i, auktSlut == std::string_view::npos ? std::string_view::npos : auktSlut - i);
    if (auktoritet.find('@') != std::string_view::npos)
        return ogiltig("användarnamn i url:en stöds inte — skicka en Authorization-header");

    std::string_view portText;
    if (!auktoritet.empty() && auktoritet.front() == '[') {
        const std::size_t slut = auktoritet.find(']');
        if (slut == std::string_view::npos)
            return ogiltig("IPv6-adressen i url:en saknar ]");
        u.host = std::string(auktoritet.substr(1, slut - 1));
        const auto adress = Address::parse(u.host);
        if (!adress || !adress->isV6())
            return ogiltig("ogiltig IPv6-adress i url:en");
        const std::string_view rest = auktoritet.substr(slut + 1);
        if (!rest.empty()) {
            if (rest.front() != ':')
                return ogiltig("tecken efter ] i url:en");
            portText = rest.substr(1);
            if (portText.empty())
                return ogiltig("tom port i url:en");
        }
    } else {
        const std::size_t kolon = auktoritet.find(':');
        u.host                  = std::string(auktoritet.substr(0, kolon));
        if (kolon != std::string_view::npos) {
            portText = auktoritet.substr(kolon + 1);
            if (portText.empty())
                return ogiltig("tom port i url:en");
        }
    }
    if (u.host.empty())
        return ogiltig("url:en saknar värd");

    u.port = u.https ? 443 : 80;
    if (!portText.empty()) {
        unsigned long p = 0;
        for (char c : portText) {
            if (c < '0' || c > '9' || p > 65535)
                return ogiltig("ogiltig port i url:en");
            p = p * 10 + static_cast<unsigned long>(c - '0');
        }
        if (p == 0 || p > 65535)
            return ogiltig("porten i url:en ska vara 1–65535");
        u.port = static_cast<std::uint16_t>(p);
    }

    // Sökväg + fråga; fragmentet skickas aldrig till servern.
    std::string_view resten =
        auktSlut == std::string_view::npos ? std::string_view{} : text.substr(auktSlut);
    if (const std::size_t fragment = resten.find('#'); fragment != std::string_view::npos)
        resten = resten.substr(0, fragment);
    u.target = std::string(resten);
    if (u.target.empty() || u.target.front() == '?')
        u.target = "/" + u.target;
    u.full = (u.https ? "https://" : "http://") + std::string(auktoritet) + u.target;
    return u;
}

} // namespace detail

Result<HttpResponse> request(const HttpRequest& req, std::stop_token stop)
{
    const std::string vad = (req.method.empty() ? std::string("?") : req.method) + " " + req.url;
    if (!ärToken(req.method))
        return sys::med(vad, ogiltig("ogiltig metod"));
    for (const Header& h : req.headers) {
        if (!ärToken(h.name))
            return sys::med(vad, ogiltig("ogiltigt headernamn \"" + h.name + "\""));
        // CR/LF i ett värde hade varit en egen, injicerad header.
        if (h.value.find_first_of("\r\n") != std::string::npos)
            return sys::med(vad, ogiltig("radbrytning i headern \"" + h.name + "\""));
        // Bara synlig ASCII + tab: WinHTTP skriver om "ä" till Latin-1 på
        // tråden, libcurl skickar UTF-8-bytesen — samma värde hade blivit två.
        for (char c : h.value) {
            const auto b = static_cast<unsigned char>(c);
            if ((b < 0x20 && b != '\t') || b >= 0x7F)
                return sys::med(vad, ogiltig("headern \"" + h.name +
                                             "\" ska vara ASCII (koda värdet, t.ex. RFC 8187)"));
        }
    }
    // GET och HEAD bär ingen kropp: NSURLSession vägrar den, libcurl och
    // WinHTTP skickar den — samma förfrågan hade betytt tre olika saker.
    if (!req.body.empty() && (req.method == "GET" || req.method == "HEAD"))
        return sys::med(vad, ogiltig("GET och HEAD kan inte ha en kropp"));
    auto url = detail::parseUrl(req.url);
    if (!url)
        return sys::med(vad, url.failure());
    if (req.timeout.count() <= 0)
        return Error{Code::Timeout, vad + ": tidsgränsen är noll"};
    if (stop.stop_requested())
        return sys::avbrutet(vad);

    // En kropp utan Content-Type får en uttrycklig — annars hade libcurl
    // satt application/x-www-form-urlencoded, NSURLSession kanske samma och
    // WinHTTP ingenting alls.
    HttpRequest skickas = req;
    if (!skickas.body.empty()) {
        bool harTyp = false;
        for (const Header& h : skickas.headers)
            harTyp = harTyp || likaUtanSkiftläge(h.name, "Content-Type");
        if (!harTyp)
            skickas.headers.push_back({"Content-Type", "application/octet-stream"});
    }

    auto svar = detail::perform(skickas, *url, stop);
    if (!svar)
        return sys::med(vad, svar.failure());

    // LIKRIKTNINGEN: en vägrad nedgradering https → http. WinHTTP (policyn
    // DISALLOW_HTTPS_TO_HTTP) och NSURLSession (vår delegat) svarar med själva
    // 3xx-svaret; här blir det samma fel som libcurl ger (http_curl.cpp).
    if (req.followRedirects && detail::isRedirect(svar->status) && börjarMed(svar->url, "https:") &&
        börjarMed(svar->header("Location"), "http:"))
        return Error{Code::Protocol, vad + ": " + detail::kNedgradering + " (" +
                                         svar->header("Location") + ")"};
    return svar;
}

Result<HttpResponse> get(std::string_view url, std::stop_token stop,
                         std::chrono::milliseconds timeout)
{
    HttpRequest req;
    req.url     = std::string(url);
    req.timeout = timeout;
    return request(req, std::move(stop));
}

Result<HttpResponse> post(std::string_view url, std::string body, std::string_view contentType,
                          std::stop_token stop, std::chrono::milliseconds timeout)
{
    HttpRequest req;
    req.method  = "POST";
    req.url     = std::string(url);
    req.body    = std::move(body);
    req.timeout = timeout;
    if (!contentType.empty())
        req.headers.push_back({"Content-Type", std::string(contentType)});
    return request(req, std::move(stop));
}

} // namespace glaze::net
