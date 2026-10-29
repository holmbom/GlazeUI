#pragma once
#include <chrono>
#include <stop_token>
#include <string_view>

#include "glaze_net/address.h"
#include "glaze_net/error.h"

// glaze_net — PING. En ICMP-eko, ett svar.
//
// PLATTFORMEN BESTÄMMER VÄGEN, och den kräver aldrig administratör:
//   Windows        IcmpSendEcho / Icmp6SendEcho2 (iphlpapi) — OS-tjänsten
//                  skickar åt oss, inga rättigheter behövs.
//   Linux, macOS   en ICMP-DATAGRAMSOCKET (SOCK_DGRAM + IPPROTO_ICMP), inte
//                  en rå socket. macOS tillåter den alltid. Linux tillåter
//                  den bara för grupper inom `net.ipv4.ping_group_range`
//                  (kärnans default "1 0" = ingen; systemd sätter
//                  "0 2147483647" på de flesta distributioner). Nekas den
//                  svarar anropet `Code::Permission` med sysctl-namnet i
//                  texten — ALDRIG en tyst timeout.
//
// Ett namn slås upp först; finns både IPv4 och IPv6 pingas IPv4-adressen
// (den nås oftare än en halvkonfigurerad IPv6-väg). Tiden är mätt av OSS,
// runt utbytet, med samma klocka på alla plattformar.

namespace glaze::net {

struct PingReply {
    Address                   address;   // den som svarade
    std::chrono::microseconds time{};    // tur och retur
};

Result<PingReply> ping(std::string_view host, std::stop_token stop,
                       std::chrono::milliseconds timeout = std::chrono::seconds{4});

} // namespace glaze::net
