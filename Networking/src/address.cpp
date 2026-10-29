// glaze_net — adresserna: text ↔ bytes. REN STD och samma på alla
// plattformar (ingen inet_pton) — reglerna står i address.h.

#include "glaze_net/address.h"

#include <cstdio>
#include <vector>

namespace glaze::net {
namespace {

bool ärSiffra(char c) { return c >= '0' && c <= '9'; }

int hexVärde(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Strikt fyrdelad decimal till fyra bytes. Inledande nollor vägras.
bool tolkaV4(std::string_view t, std::uint8_t* ut)
{
    int del = 0;
    std::size_t i = 0;
    while (true) {
        const std::size_t start = i;
        unsigned          värde = 0;
        while (i < t.size() && ärSiffra(t[i]) && i - start < 3)
            värde = värde * 10 + static_cast<unsigned>(t[i++] - '0');
        const std::size_t längd = i - start;
        if (längd == 0 || värde > 255 || (längd > 1 && t[start] == '0'))
            return false;
        ut[del++] = static_cast<std::uint8_t>(värde);
        if (del == 4)
            return i == t.size();
        if (i >= t.size() || t[i] != '.')
            return false;
        ++i;
    }
}

// En kolonseparerad följd av 16-bitarsgrupper, ev. med avslutande IPv4.
// Tom text ger noll grupper (sidan av ett "::").
bool tolkaGrupper(std::string_view t, std::vector<std::uint16_t>& grupper,
                  bool sistaFårVaraV4)
{
    if (t.empty())
        return true;
    std::size_t i = 0;
    while (true) {
        const std::size_t slut = t.find(':', i);
        const std::string_view g =
            t.substr(i, slut == std::string_view::npos ? std::string_view::npos : slut - i);
        if (slut == std::string_view::npos && sistaFårVaraV4 &&
            g.find('.') != std::string_view::npos) {
            std::uint8_t v4[4];
            if (!tolkaV4(g, v4))
                return false;
            grupper.push_back(static_cast<std::uint16_t>(v4[0] << 8 | v4[1]));
            grupper.push_back(static_cast<std::uint16_t>(v4[2] << 8 | v4[3]));
            return true;
        }
        if (g.empty() || g.size() > 4)
            return false;
        unsigned värde = 0;
        for (char c : g) {
            const int h = hexVärde(c);
            if (h < 0)
                return false;
            värde = värde * 16 + static_cast<unsigned>(h);
        }
        grupper.push_back(static_cast<std::uint16_t>(värde));
        if (slut == std::string_view::npos)
            return true;
        i = slut + 1;
    }
}

bool tolkaV6(std::string_view t, std::array<std::uint8_t, 16>& ut, std::uint32_t& zon)
{
    zon = 0;
    if (const std::size_t pct = t.find('%'); pct != std::string_view::npos) {
        const std::string_view z = t.substr(pct + 1);
        if (z.empty() || z.size() > 10)
            return false;
        std::uint64_t v = 0;
        for (char c : z) {
            if (!ärSiffra(c))
                return false;   // gränssnittsNAMN kräver ett OS-uppslag
            v = v * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (v > 0xFFFFFFFFull)
            return false;
        zon = static_cast<std::uint32_t>(v);
        t   = t.substr(0, pct);
    }

    std::vector<std::uint16_t> vänster, höger;
    const std::size_t dubbel = t.find("::");
    if (dubbel == std::string_view::npos) {
        if (!tolkaGrupper(t, vänster, true) || vänster.size() != 8)
            return false;
    } else {
        if (t.find("::", dubbel + 1) != std::string_view::npos)
            return false;   // högst ett "::"
        const std::string_view v = t.substr(0, dubbel);
        const std::string_view h = t.substr(dubbel + 2);
        if (!tolkaGrupper(v, vänster, false) || !tolkaGrupper(h, höger, true))
            return false;
        if (vänster.size() + höger.size() > 7)
            return false;
    }

    std::array<std::uint16_t, 8> g{};
    for (std::size_t i = 0; i < vänster.size(); ++i)
        g[i] = vänster[i];
    for (std::size_t i = 0; i < höger.size(); ++i)
        g[8 - höger.size() + i] = höger[i];
    for (std::size_t i = 0; i < 8; ++i) {
        ut[2 * i]     = static_cast<std::uint8_t>(g[i] >> 8);
        ut[2 * i + 1] = static_cast<std::uint8_t>(g[i] & 0xFF);
    }
    return true;
}

} // namespace

Address Address::v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
{
    Address r;
    r.bytes_[0] = a;
    r.bytes_[1] = b;
    r.bytes_[2] = c;
    r.bytes_[3] = d;
    return r;
}

Address Address::v6(const std::array<std::uint8_t, 16>& bytes, std::uint32_t scopeId)
{
    Address r;
    r.bytes_ = bytes;
    r.scope_ = scopeId;
    r.v6_    = true;
    return r;
}

Address Address::loopbackV4() { return v4(127, 0, 0, 1); }
Address Address::anyV4() { return {}; }

Address Address::loopbackV6()
{
    std::array<std::uint8_t, 16> b{};
    b[15] = 1;
    return v6(b);
}

Address Address::anyV6() { return v6({}); }

bool Address::isLoopback() const
{
    if (!v6_)
        return bytes_[0] == 127;
    bool nollor = true;
    for (int i = 0; i < 15; ++i)
        nollor = nollor && bytes_[i] == 0;
    if (nollor && bytes_[15] == 1)
        return true;   // ::1
    // ::ffff:127.x.x.x — IPv4-mappad loopback
    for (int i = 0; i < 10; ++i)
        if (bytes_[i] != 0)
            return false;
    return bytes_[10] == 0xFF && bytes_[11] == 0xFF && bytes_[12] == 127;
}

std::optional<Address> Address::parse(std::string_view t)
{
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
        t = t.substr(1, t.size() - 2);
    if (t.empty())
        return std::nullopt;
    if (t.find(':') == std::string_view::npos) {
        std::uint8_t b[4];
        if (!tolkaV4(t, b))
            return std::nullopt;
        return v4(b[0], b[1], b[2], b[3]);
    }
    std::array<std::uint8_t, 16> b{};
    std::uint32_t                zon = 0;
    if (!tolkaV6(t, b, zon))
        return std::nullopt;
    return v6(b, zon);
}

std::string Address::toString() const
{
    char buf[64];
    if (!v6_) {
        std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", unsigned{bytes_[0]},
                      unsigned{bytes_[1]}, unsigned{bytes_[2]}, unsigned{bytes_[3]});
        return buf;
    }

    // RFC 5952 §5: en IPv4-mappad adress skrivs med punktdecimal svans.
    bool mappad = bytes_[10] == 0xFF && bytes_[11] == 0xFF;
    for (int i = 0; mappad && i < 10; ++i)
        mappad = bytes_[i] == 0;
    std::string ut;
    if (mappad) {
        std::snprintf(buf, sizeof buf, "::ffff:%u.%u.%u.%u", unsigned{bytes_[12]},
                      unsigned{bytes_[13]}, unsigned{bytes_[14]}, unsigned{bytes_[15]});
        ut = buf;
    } else {
        std::uint16_t g[8];
        for (int i = 0; i < 8; ++i)
            g[i] = static_cast<std::uint16_t>(bytes_[2 * i] << 8 | bytes_[2 * i + 1]);
        // RFC 5952 §4.2: den LÄNGSTA följden av minst två nollgrupper blir
        // "::" — vid lika längd den FÖRSTA.
        int bästStart = -1, bästLängd = 0;
        for (int i = 0; i < 8;) {
            if (g[i] != 0) {
                ++i;
                continue;
            }
            int j = i;
            while (j < 8 && g[j] == 0)
                ++j;
            if (j - i > bästLängd && j - i >= 2) {
                bästStart = i;
                bästLängd = j - i;
            }
            i = j;
        }
        for (int i = 0; i < 8; ++i) {
            if (i == bästStart) {
                ut += "::";
                i += bästLängd - 1;
                continue;
            }
            if (!ut.empty() && ut.back() != ':')
                ut += ':';
            std::snprintf(buf, sizeof buf, "%x", unsigned{g[i]});
            ut += buf;
        }
    }
    if (scope_ != 0)
        ut += "%" + std::to_string(scope_);
    return ut;
}

std::string Endpoint::toString() const
{
    if (address.isV6())
        return "[" + address.toString() + "]:" + std::to_string(port);
    return address.toString() + ":" + std::to_string(port);
}

} // namespace glaze::net
