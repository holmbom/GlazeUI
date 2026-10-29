#pragma once
#include <chrono>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "glaze_net/error.h"

// glaze_net — HTTP(S). "Appen ber om en resurs och
// får ett svar" — anslutning, TLS, omdirigeringar, chunkad överföring och
// avkomprimering är paketets.
//
// OS:ETS HTTP-STACK, ALDRIG EN EGEN — OS-native, uppfinn inget: WinHTTP på
// Windows, NSURLSession på macOS, libcurl på Linux. OS:et
// sköter certifikaten, proxyn och TLS-versionerna; paketet har ingen egen
// certifikathantering och kan därför inte ha fel i en.
//
// Det paketet LIKRIKTAR, så att samma anrop betyder samma sak överallt:
//   - `timeout` är taket för HELA förfrågan (anslutning + sändning + svar).
//   - Omdirigeringar följs, högst 10. En omdirigering från https till http
//     följs ALDRIG — den svarar `Code::Protocol` på alla tre.
//   - Kroppen avkomprimeras (gzip/deflate och det OS:et mer kan). En
//     `Content-Length`-header gäller då den KOMPRIMERADE längden.
//   - En 4xx/5xx-status är ett SVAR, inte ett fel: `r.ok()` och
//     `r->status == 404`. `success()` säger 2xx.
//   - url:en ska vara ASCII (procentkodad); `percentEncode` bygger
//     frågesträngar. Användarnamn i url:en vägras — skicka en
//     `Authorization`-header.
//   - Avbrytning via stop_token. Den verkar direkt på Windows och macOS och
//     inom en sekund på Linux (libcurls förloppsanrop är det enda sättet att
//     avbryta en `curl_easy_perform`).
//
// KÄND SKILLNAD som inte går att likrikta: macOS App Transport Security
// stoppar http:// (okrypterat) i en app utan undantag i sin Info.plist — se
// Networking/CLAUDE.md.
//
// BLOCKERANDE som resten av det synkrona lagret; i en GUI-app körs den i en
// `Worker`: `glaze::net::get(worker, url, klar)` (glaze_net/async.h).

namespace glaze::net {

struct Header {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string               method = "GET";
    std::string               url;
    std::vector<Header>       headers;
    std::string               body;      // binärsäker
    std::chrono::milliseconds timeout{std::chrono::seconds{30}};
    bool                      followRedirects = true;
};

struct HttpResponse {
    int                 status = 0;
    std::vector<Header> headers;   // det SLUTLIGA svarets, i ordning
    std::string         body;      // binärsäker, avkomprimerad
    std::string         url;       // slutliga adressen efter omdirigeringar

    // Första headern med namnet (skiftlägesokänsligt); "" om den saknas.
    std::string header(std::string_view name) const;
    bool        success() const { return status >= 200 && status <= 299; }
};

Result<HttpResponse> request(const HttpRequest& req, std::stop_token stop);

Result<HttpResponse> get(std::string_view url, std::stop_token stop,
                         std::chrono::milliseconds timeout = std::chrono::seconds{30});

Result<HttpResponse> post(std::string_view url, std::string body,
                          std::string_view contentType, std::stop_token stop,
                          std::chrono::milliseconds timeout = std::chrono::seconds{30});

// Procentkodar allt utom de oreserverade tecknen (A–Z a–z 0–9 - . _ ~) —
// för ett VÄRDE i en frågesträng: "?LAYERS=" + percentEncode("vägar å").
std::string percentEncode(std::string_view text);

} // namespace glaze::net
