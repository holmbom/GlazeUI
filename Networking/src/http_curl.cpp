// glaze_net — HTTP(S) på LINUX genom libcurl, systemets eget (pkg-config;
// saknas utvecklingspaketet stoppar CMake med paketnamnen). Kontraktet står i
// http.h; valideringen och likriktningen i http.cpp. Linux har ingen
// systemgemensam HTTP-stack, och libcurl är det närmaste: distributionens
// paket med distributionens certifikatlager och TLS-bibliotek.
//
// AVBRYTNINGEN går genom förloppsanropet (CURLOPT_XFERINFOFUNCTION) — det
// enda sättet att avbryta en `curl_easy_perform`. libcurl anropar det ofta
// under överföring men bara ungefär en gång per sekund när inget händer,
// så ett avbrott verkar inom en sekund (http.h säger det).
//
// OMDIRIGERINGAR följs av libcurl själv, högst 10. Nedgraderingen
// https → http stoppas av protokollistan för omdirigeringar och blir samma
// `Code::Protocol` som de två andra stackarna ger.

#include <curl/curl.h>

#include <algorithm>
#include <climits>

#include "http_impl.h"
#include "sys.h"

namespace glaze::net::detail {
namespace {

// En gång per process, vid FÖRSTA förfrågan (alltså i en pooltråd) och inte
// i statisk initiering — curl_global_init initierar TLS-biblioteket, och
// det ska inte ske innan main fått starta. Trådsäker från 7.84 (bitten
// CURL_VERSION_THREADSAFE); den statiska lokalen gör anropet till exakt ett
// oavsett version. curl_global_cleanup anropas aldrig: libcurl lever
// processen ut, precis som Winsock.
CURLcode globalInit()
{
    static const CURLcode r = curl_global_init(CURL_GLOBAL_DEFAULT);
    return r;
}

std::size_t skriv(char* data, std::size_t storlek, std::size_t antal, void* ud)
{
    static_cast<std::string*>(ud)->append(data, storlek * antal);
    return storlek * antal;
}

// En headerrad i taget, statusraden och den tomma slutraden inräknade.
// libcurl skickar raderna för VARJE svar (omdirigeringar, 100 Continue) —
// en statusrad börjar alltså om samlingen, så att det SLUTLIGA svarets
// headers är det som blir kvar.
std::size_t rubrik(char* data, std::size_t storlek, std::size_t antal, void* ud)
{
    auto&            headers = *static_cast<std::vector<Header>*>(ud);
    const std::size_t n      = storlek * antal;
    std::string_view rad(data, n);
    while (!rad.empty() && (rad.back() == '\n' || rad.back() == '\r'))
        rad.remove_suffix(1);
    if (rad.substr(0, 5) == "HTTP/") {
        headers.clear();
        return n;
    }
    const std::size_t kolon = rad.find(':');
    if (rad.empty() || kolon == std::string_view::npos)
        return n;
    std::size_t v = kolon + 1;
    while (v < rad.size() && (rad[v] == ' ' || rad[v] == '\t'))
        ++v;
    std::size_t e = rad.size();
    while (e > v && (rad[e - 1] == ' ' || rad[e - 1] == '\t'))
        --e;
    headers.push_back({std::string(rad.substr(0, kolon)), std::string(rad.substr(v, e - v))});
    return n;
}

int förlopp(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    return static_cast<const std::stop_token*>(ud)->stop_requested() ? 1 : 0;
}

Error fel(CURLcode kod, const char* felbuffert)
{
    Code c = Code::Failed;
    switch (kod) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
        c = Code::HostNotFound;
        break;
    case CURLE_COULDNT_CONNECT:
        c = Code::Refused;
        break;
    case CURLE_OPERATION_TIMEDOUT:
        c = Code::Timeout;
        break;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    case CURLE_SSL_INVALIDCERTSTATUS:
    case CURLE_USE_SSL_FAILED:
        c = Code::Tls;
        break;
    case CURLE_URL_MALFORMAT:
        c = Code::InvalidArgument;
        break;
    case CURLE_TOO_MANY_REDIRECTS:
    case CURLE_GOT_NOTHING:
    case CURLE_WEIRD_SERVER_REPLY:
    case CURLE_PARTIAL_FILE:
    case CURLE_BAD_CONTENT_ENCODING:
    case CURLE_HTTP2:
    case CURLE_HTTP2_STREAM:
        c = Code::Protocol;
        break;
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
        c = Code::Reset;
        break;
    default:
        break;
    }
    const std::string text = (felbuffert && felbuffert[0]) ? felbuffert : curl_easy_strerror(kod);
    return Error{c, text + " (curl " + std::to_string(static_cast<int>(kod)) + ")"};
}

// Städningen — handtaget och headerlistan, hur funktionen än lämnas.
struct Städ {
    CURL*              h       = nullptr;
    struct curl_slist* headers = nullptr;
    ~Städ()
    {
        if (headers)
            curl_slist_free_all(headers);
        if (h)
            curl_easy_cleanup(h);
    }
};

} // namespace

Result<HttpResponse> perform(const HttpRequest& req, const Url& url, const std::stop_token& stop)
{
    if (const CURLcode r = globalInit(); r != CURLE_OK)
        return Error{Code::Failed, std::string("libcurl kunde inte initieras: ") +
                                       curl_easy_strerror(r)};
    Städ städ;
    städ.h = curl_easy_init();
    if (!städ.h)
        return Error{Code::Failed, "libcurl kunde inte skapa ett handtag"};
    CURL* h = städ.h;

    char         felbuffert[CURL_ERROR_SIZE] = {};
    HttpResponse svar;
    curl_easy_setopt(h, CURLOPT_URL, url.full.c_str());
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);   // trådar: inga signaler för DNS-tak
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, felbuffert);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, skriv);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &svar.body);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, rubrik);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &svar.headers);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, förlopp);
    // En egen KOPIA av token (delar stopptillståndet) — curl vill ha en
    // skrivbar pekare, och anroparens token är const.
    std::stop_token stopp = stop;
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &stopp);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");   // allt libcurl kan avkoda
    curl_easy_setopt(h, CURLOPT_USERAGENT, "glaze-net/1");
    if (req.timeout != kForever)
        curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,
                         static_cast<long>(std::min<long long>(req.timeout.count(), LONG_MAX)));

    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, req.followRedirects ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500   // 7.85: protokollistorna som text
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, url.https ? "https" : "http,https");
#else
    curl_easy_setopt(h, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS,
                     static_cast<long>(url.https ? CURLPROTO_HTTPS
                                                 : (CURLPROTO_HTTP | CURLPROTO_HTTPS)));
#endif

    // Metoden. GET är libcurls default; en kropp skickas för allt utom HEAD.
    if (req.method == "HEAD") {
        curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    } else if (req.method != "GET" || !req.body.empty()) {
        if (req.method != "POST")
            curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(req.body.size()));
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, req.body.data());
    }

    for (const Header& rad : req.headers)
        städ.headers = curl_slist_append(städ.headers, (rad.name + ": " + rad.value).c_str());
    // libcurl lägger till "Expect: 100-continue" för stora kroppar och väntar
    // då upp till en sekund på servern — de andra stackarna gör det inte.
    städ.headers = curl_slist_append(städ.headers, "Expect:");
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, städ.headers);

    const CURLcode r = curl_easy_perform(h);
    if (r != CURLE_OK) {
        if (r == CURLE_ABORTED_BY_CALLBACK && stop.stop_requested())
            return Error{threads::Code::Cancelled, "avbrutet"};
        // Schemat är redan validerat (http/https) — ett ostött protokoll kan
        // bara komma från en omdirigering utanför REDIR-listan.
        if (r == CURLE_UNSUPPORTED_PROTOCOL)
            return Error{Code::Protocol, std::string(kNedgradering) + " (" +
                                             (felbuffert[0] ? felbuffert : "libcurl") + ")"};
        return fel(r, felbuffert);
    }
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    svar.status = static_cast<int>(status);
    char* slutlig = nullptr;
    curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &slutlig);
    svar.url = slutlig ? slutlig : url.full;
    return svar;
}

} // namespace glaze::net::detail
