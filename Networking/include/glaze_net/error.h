#pragma once
#include <chrono>
#include <system_error>

#include <glaze_threads/result.h>

// glaze_net — FELEN och TIDSGRÄNSERNA.
//
// Varje nätverksanrop svarar med ett `Result<T>` — samma typ som
// `glaze::threads` levererar till en fortsättning, så att ett anrop som
// körs i en `Worker` blir `Result<T>` hela vägen fram och aldrig
// `Result<Result<T>>`. Ett fel är ALDRIG tyst: det är en kod ur `Code`
// (vad — jämförbar) plus en text (var och varför, med systemets egen
// felbeskrivning och felnummer — för loggen):
//
//     auto r = glaze::net::get(url, stop);
//     if (!r && r.code() == glaze::net::Code::Timeout) …
//
// AVBRYTNING är inte en nätverkskod: ett anrop vars `std::stop_token`
// begärs svarar `glaze::threads::Code::Cancelled` (`r.cancelled()`), samma
// kod som resten av trådpaketet. Avbrutet är avbrutet, oavsett vem som bad.

namespace glaze::net {

enum class Code {
    Ok = 0,
    Timeout,           // tidsgränsen passerades
    HostNotFound,      // namnet finns inte (DNS)
    Refused,           // motparten nekade anslutningen
    Reset,             // anslutningen bröts av motparten
    Closed,            // motparten stängde ordnat (`receive` vid slutet)
    Unreachable,       // nätet eller värden kan inte nås, namnservern svarar inte
    AddressInUse,      // `bind`/`listen`: adressen är upptagen
    InvalidArgument,   // ogiltig adress, url, port, metod …
    Tls,               // certifikatet eller den krypterade förbindelsen
    Protocol,          // motparten svarade fel, eller för många omdirigeringar
    Permission,        // OS:et nekade (t.ex. en ICMP-socket utan rättighet)
    Failed,            // övrigt systemfel — texten säger vad
};

const std::error_category& category();
std::error_code make_error_code(Code c);

} // namespace glaze::net

template <>
struct std::is_error_code_enum<glaze::net::Code> : std::true_type {};

namespace glaze::net {

template <class T>
using Result = threads::Result<T>;
using Error  = threads::Error;

// "Inget tak" — vänta tills något händer eller stoppet begärs.
constexpr std::chrono::milliseconds kForever = std::chrono::milliseconds::max();

} // namespace glaze::net
