#pragma once

// glaze_net — PARAPLYET. Allt paketet har:
//
//   Code, Result<T>, kForever      felen och taket          glaze_net/error.h
//   Address, Endpoint              adresserna               glaze_net/address.h
//   resolve                        DNS                      glaze_net/dns.h
//   TcpSocket, TcpListener         TCP                      glaze_net/tcp.h
//   UdpSocket, Datagram            UDP                      glaze_net/udp.h
//   ping, PingReply                ICMP-eko                 glaze_net/ping.h
//   request, get, post             HTTP(S) via OS:ets stack glaze_net/http.h
//   …(Worker&, …, klar)            de asynkrona formerna    glaze_net/async.h
//
// Paketet beror på `glaze_threads` (Result, Worker) men ALDRIG på `glaze`.
// Svar till UI-tråden går genom trådpaketets UI-brygga, inte genom något här.

#include "glaze_net/address.h"
#include "glaze_net/async.h"
#include "glaze_net/dns.h"
#include "glaze_net/error.h"
#include "glaze_net/http.h"
#include "glaze_net/ping.h"
#include "glaze_net/tcp.h"
#include "glaze_net/udp.h"
