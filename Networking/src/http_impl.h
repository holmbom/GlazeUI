#pragma once
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>

#include "glaze_net/http.h"

// INTERN — gränsen mellan HTTP-lagrets gemensamma del (http.cpp:
// validering, url-tolkning, likriktningen) och OS:ets stack (en fil per
// plattform: http_winhttp.cpp, http_apple.mm, http_curl.cpp).

namespace glaze::net::detail {

// En VALIDERAD url. `full` är det som skickas till en stack som vill ha hela
// strängen (libcurl, NSURL); `host`/`port`/`target` till en som vill ha
// delarna (WinHTTP).
struct Url {
    bool          https = false;
    std::string   host;      // utan hakparenteser
    std::uint16_t port = 0;  // uttryckligen angiven, annars 80/443
    std::string   target;    // sökväg + fråga, minst "/"; fragmentet bortskuret
    std::string   full;      // schema://auktoritet + target
};

Result<Url> parseUrl(std::string_view url);

// OS:ets stack — EN förfrågan, utan likriktningen. `req` och `url` är redan
// validerade; felen returneras UTAN sammanhang (http.cpp sätter det).
Result<HttpResponse> perform(const HttpRequest& req, const Url& url, const std::stop_token& stop);

bool isRedirect(int status);

// Feltexten när en omdirigering https → http vägras — samma ord på alla tre.
inline constexpr const char* kNedgradering =
    "omdirigering från https till http följs inte";

} // namespace glaze::net::detail
