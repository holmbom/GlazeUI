#pragma once
#include <stop_token>
#include <string>
#include <utility>

#include <glaze_threads/worker.h>

#include "glaze_net/dns.h"
#include "glaze_net/http.h"
#include "glaze_net/ping.h"

// glaze_net — DE ASYNKRONA FORMERNA.
//
// Hård regel 1 gäller nätverket också: ett anrop får ALDRIG blockera
// UI-pumpen. De fyra fråga-svar-anropen finns därför i en form som tar en
// `glaze::threads::Worker` — anropet körs i en pooltråd och svaret kommer
// till Workerns kontext, alltså UI-tråden i en GUI-app som länkar
// `glaze_threads_ui`:
//
//     class KartFönster : public glaze::AppWindow {
//         ...
//         glaze::threads::Worker nät;   // dör med fönstret ⇒ inga svar efteråt
//     };
//
//     glaze::net::get(nät, url, [this](glaze::net::Result<glaze::net::HttpResponse>& r) {
//         if (!r)                status.setText(r.error());   // nätfel
//         else if (r->success()) visaKarta(r->body);          // 2xx
//         else                   status.setText("HTTP " + std::to_string(r->status));
//     });
//
// Formerna är TUNNA: `worker.run` runt det synkrona anropet, ingenting mer.
// Allt Workern lovar gäller alltså — svaret körs aldrig efter att fönstret
// dött, och `nät.cancel()` avbryter pågående anrop (deras svar levereras
// aldrig). "Bara den senaste kartbilden": `nät.cancel(); glaze::net::get(…)`.
//
// TCP och UDP har ingen sådan form — ett utbyte är flera anrop i rad, och
// skrivs som vanlig rak kod i `worker.run` (se tcp.h).

namespace glaze::net {

template <class Done>
void request(threads::Worker& worker, HttpRequest req, Done done)
{
    worker.run([req = std::move(req)](std::stop_token stop) { return request(req, stop); },
               std::move(done));
}

template <class Done>
void get(threads::Worker& worker, std::string url, Done done)
{
    worker.run([url = std::move(url)](std::stop_token stop) { return get(url, stop); },
               std::move(done));
}

template <class Done>
void resolve(threads::Worker& worker, std::string host, Done done)
{
    worker.run([host = std::move(host)](std::stop_token stop) { return resolve(host, stop); },
               std::move(done));
}

template <class Done>
void ping(threads::Worker& worker, std::string host, Done done)
{
    worker.run([host = std::move(host)](std::stop_token stop) { return ping(host, stop); },
               std::move(done));
}

} // namespace glaze::net
