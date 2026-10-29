#pragma once
#include <chrono>
#include <stop_token>
#include <string_view>
#include <vector>

#include "glaze_net/address.h"
#include "glaze_net/error.h"

// glaze_net — NAMNUPPSLAGNINGEN.
//
// BLOCKERANDE, som varje anrop i paketets synkrona lager: för pooltrådar och
// kommandoradsverktyg. I en GUI-app körs den i en `Worker` —
// `glaze::net::resolve(worker, värd, klar)` i glaze_net/async.h. Aldrig på
// UI-tråden (hård regel 1).
//
// Svaret är adresserna i OS:ets ordning (det sorterar efter RFC 6724), utan
// dubbletter. En numerisk adress ("10.0.0.1", "::1", "[::1]") svaras direkt
// utan OS-anrop. IDN-namn (åäö) ska skickas som punycode — hur OS:et tolkar
// UTF-8 i ett värdnamn skiljer sig mellan plattformarna.
//
// OS:ets uppslag (getaddrinfo/GetAddrInfoW) GÅR INTE att avbryta. Det körs
// därför i en egen, frikopplad tråd, och anropet här väntar på det med tak
// och stopp — avbrytning och tidsgräns verkar DIREKT, medan själva
// OS-uppslaget får löpa klart i bakgrunden och dess svar kastas.

namespace glaze::net {

Result<std::vector<Address>> resolve(std::string_view host, std::stop_token stop,
                                     std::chrono::milliseconds timeout =
                                         std::chrono::seconds{10});

} // namespace glaze::net
