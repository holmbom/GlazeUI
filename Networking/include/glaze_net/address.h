#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// glaze_net — ADRESSERNA.
//
// `Address` är en IPv4- eller IPv6-adress utan port, `Endpoint` en adress
// plus port. Båda är rena värden — inga OS-typer i headern (fasadregeln från
// `gfx::`); omvandlingen till `sockaddr` sker bakom fasaden.
//
// Text ↔ adress är REN STD och identisk på alla plattformar — ingen
// `inet_pton`, så att samma sträng tolkas likadant överallt:
//   IPv4  strikt fyrdelad decimal: "192.168.1.10". Inledande nollor vägras
//         ("010" hade varit oktalt för en del tolkare och decimalt för andra).
//   IPv6  RFC 4291-formerna, med "::" och inbäddad IPv4 ("::ffff:1.2.3.4");
//         utskrift enligt RFC 5952 (gemener, längsta nollföljden blir "::").
//         Zon-id stöds bara NUMERISKT ("fe80::1%3"); ett gränssnittsnamn
//         ("%eth0") kräver ett OS-uppslag och vägras.
//   Hakparenteser ("[::1]") accepteras vid tolkning, som i en url.

namespace glaze::net {

class Address {
public:
    Address() = default;   // 0.0.0.0 — IPv4 "alla gränssnitt"

    static Address v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);
    static Address v6(const std::array<std::uint8_t, 16>& bytes, std::uint32_t scopeId = 0);
    static Address loopbackV4();   // 127.0.0.1
    static Address loopbackV6();   // ::1
    static Address anyV4();        // 0.0.0.0
    static Address anyV6();        // ::

    // nullopt = texten är ingen adress (alltså ett namn, eller trasig).
    static std::optional<Address> parse(std::string_view text);

    bool isV4() const { return !v6_; }
    bool isV6() const { return v6_; }
    bool isLoopback() const;

    std::string toString() const;

    // Nätverksordning. IPv4 använder de fyra första.
    const std::array<std::uint8_t, 16>& bytes() const { return bytes_; }
    std::uint32_t                       scopeId() const { return scope_; }

    friend bool operator==(const Address&, const Address&) = default;

private:
    std::array<std::uint8_t, 16> bytes_{};
    std::uint32_t                scope_ = 0;
    bool                         v6_    = false;
};

struct Endpoint {
    Address       address;
    std::uint16_t port = 0;

    std::string toString() const;   // "1.2.3.4:80", "[::1]:80"

    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

} // namespace glaze::net
