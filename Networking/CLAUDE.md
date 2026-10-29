# Networking — sidopaketet för nätverk (lib glaze_net)

**UTANFÖR ramverket, innanför v1.0-grinden.** `glaze` länkar aldrig paketet.
Det beror på `glaze_threads` (Result, Worker) men ALDRIG på `glaze` — svar
till UI-tråden går genom trådpaketets UI-brygga. Bygger fristående
(`cmake -S Networking -B b` drar in Threads själv). Rotens
`GLAZE_NETWORKING=OFF` stänger av paketet för en konsument som inte vill ha
libcurl på Linux. I DELAT läge (`GLAZE_SHARED`, TODO 73) är `glaze_net`
libglaze_net (DLL/.so/.dylib) i samma växel som Threads; CMakeLists
inkluderar `cmake/GlazeShared.cmake` FÖRE Threads, och en `glaze_threads` som
redan finns i det andra läget ger `FATAL_ERROR` — blandat godtas aldrig tyst.
Paketet har inget observerbart processtillstånd (WinHTTP-sessionen och
`curl_global_init` bor i cpp).

## Så använder en app det

```cmake
target_link_libraries(minapp PRIVATE glaze glaze_threads_ui glaze_net)
```

```cpp
#include <glaze_net/net.h>

glaze::threads::Worker nät;   // MEDLEM i fönstret

// Fråga–svar: den asynkrona formen tar Workern (hård regel 1 — aldrig på UI-tråden).
glaze::net::get(nät, url, [this](glaze::net::Result<glaze::net::HttpResponse>& r) {
    if (!r)                status.setText(r.error());         // nätfel
    else if (r->success()) visaKarta(r->body);                // 2xx
    else                   status.setText("HTTP " + std::to_string(r->status));
});

// Ett utbyte: vanlig rak kod i ett arbete — Result<T> plattas ut.
nät.run([](std::stop_token stop) -> glaze::net::Result<std::string> {
    glaze::net::TcpSocket s;
    if (auto r = s.connect("example.com", 7, stop); !r) return r.failure();
    if (auto r = s.send("hej\n", stop); !r)             return r.failure();
    return s.receive(stop);
}, [this](glaze::net::Result<std::string>& svar) { … });
```

## Filerna

- **`include/glaze_net/error.h`** — `Code` (Timeout, HostNotFound, Refused,
  Reset, Closed, Unreachable, AddressInUse, InvalidArgument, Tls, Protocol,
  Permission, Failed) med egen kategori, `Result<T>`/`Error` (= trådpaketets),
  `kForever`. Avbrott = `threads::Code::Cancelled`, inte en nätkod.
- **`include/glaze_net/address.h`** — `Address` (v4/v6, ren std) + `Endpoint`.
- **`include/glaze_net/dns.h`** · **`tcp.h`** (`TcpSocket`, `TcpListener`) ·
  **`udp.h`** (`UdpSocket`, `Datagram`) · **`ping.h`** · **`http.h`**
  (`HttpRequest`/`HttpResponse`, `request`/`get`/`post`, `percentEncode`) —
  det SYNKRONA lagret: blockerande, med tidsgräns och `std::stop_token`.
- **`include/glaze_net/async.h`** — `get`/`request`/`resolve`/`ping` med en
  `Worker` först: `worker.run` runt det synkrona anropet, inget mer.
- **`include/glaze_net/net.h`** — paraplyet.
- **`src/sys.h`** — INTERNT OS-lager ("försök, annars vänta"). Implementeras
  av **`src/sys_win.cpp`** (Winsock, `select` + UDP-väckare) och
  **`src/sys_posix.cpp`** (Linux + macOS, `poll` + pipe-väckare).
- **`src/tcp.cpp` · `udp.cpp` · `dns.cpp`** — logiken EN gång ovanpå sys.
- **`src/detached.h`** — oavbrytbart OS-anrop i en frikopplad tråd, väntat på
  med tak och stopp (DNS, Windows ping).
- **`src/ping_win.cpp`** (IcmpSendEcho/Icmp6SendEcho2) · **`src/ping_posix.cpp`**
  (ICMP-datagramsocket).
- **`src/http.cpp`** — validering, url-tolkning, LIKRIKTNINGEN.
  **`src/http_impl.h`** — gränsen mot stackarna: **`src/http_winhttp.cpp`**
  (Windows) · **`src/http_curl.cpp`** (Linux) · **`src/http_apple.mm`** (macOS,
  ObjC++/ARC, BLIND). **`src/win_text.h`** — UTF-8/UTF-16 + FormatMessage.
- **`src/address.cpp`** · **`src/error.cpp`** — ren std.

## Kontraktet

- **Varje anrop har tak och avbrytning, och ingen tråd pollar.** Socketen är
  alltid icke-blockerande; väntan är `select`/`poll` på socketen PLUS en
  väckare som stop-callbacken skriver till. Väckaren är PER väntan (en delad
  hade låtit en samtidig `send` stjäla det `receive` väcktes av).
- **Fel är aldrig tysta**: kod + text med sammanhang ("anslutning till
  10.0.0.1:80: …") och systemets egen text och felnummer.
- **HTTP(S) via OS:ets stack, aldrig en egen** (Erik 2026-10-07 — "OS-native,
  uppfinn inget"). OS:et sköter certifikat, proxy och TLS-versioner. Det
  paketet LIKRIKTAR: `timeout` = hela förfrågan · högst 10 omdirigeringar ·
  https → http följs aldrig (`Code::Protocol` på alla tre) · avkomprimering ·
  4xx/5xx är svar, inte fel · ingen cache och inga automatiska kakor ·
  url och headervärden ASCII (stackarna kodar annat olika) · GET/HEAD utan
  kropp · en kropp utan Content-Type får `application/octet-stream`.
- **PING kräver aldrig root**: Windows IcmpSendEcho; Linux/macOS en
  SOCK_DGRAM-ICMP-socket. Linux nekar den utanför
  `net.ipv4.ping_group_range` ⇒ `Code::Permission` med sysctl-namnet i texten.
- **Adresstext är ren std**, samma på alla OS (ingen inet_pton): strikt IPv4,
  RFC 5952-utskrift, bara numeriska zon-id.

## Fallgropar

- **WinHTTP MÅSTE köras asynkront** för att kunna avbrytas: att stänga
  handtaget på en SYNKRON förfrågan är förbjudet enligt dokumentationen.
  Allt WinHTTP läser/skriver asynkront (kropp, läsbuffert, headers) deklareras
  FÖRE `Flykt` och överlever dess väntan på HANDLE_CLOSING — vid ett avbrott
  mitt i en läsning skriver WinHTTP annars i frigjort minne.
- **WinHTTP hanterar kakor AUTOMATISKT** i den delade sessionen tills
  `WINHTTP_DISABLE_COOKIES` sätts (per förfrågan) — utan den skickades en
  Set-Cookie tillbaka i nästa förfrågan och likriktningen ljög. http_gate har
  kakfallet.
- **mingw-w64:s `IPV6_ADDRESS_EX` saknar SDK:ns 1-byte-packning** — ping_win
  läser IPv6-svaret genom en egen packad kopia (static_assert på layouten).
- **`SIO_UDP_CONNRESET` står i mswsock.h hos mingw-w64** (SDK: mstcpip.h) —
  utan den gör ett ICMP "port unreachable" NÄSTA recvfrom till WSAECONNRESET.
- **Linux vs macOS i ping_posix**: Linux skriver över identifieraren och
  räknar IPv4-summan och levererar svaret utan IP-huvud; macOS gör inget av
  det. Koden räknar summan alltid, matchar aldrig på id och känner igen ett
  IPv4-huvud på versionsnibbeln.
- **macOS App Transport Security** stoppar http:// i en app utan undantag i
  Info.plist (och IP-adresser sedan macOS 14) — `Code::Permission`.
  `tests/http_gate` går därför mot "localhost", inte 127.0.0.1.
- **libcurl avbryts via förloppsanropet** — högst ~1 s latens.
  `curl_global_init` körs vid FÖRSTA förfrågan (i en pooltråd), aldrig i
  statisk initiering.
- **Delvisa sändningar på Windows loopback framkallas inte** — en
  icke-blockerande send tog 8 MiB i ett svep; vägen grindas på Linux.
- **Ingen `const_cast` mot C-API:er som tar `void*` men bara läser**
  (IcmpSendEcho, WinHttpSendRequest, curl): en egen skrivbar kopia i stället.
