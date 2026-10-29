// glaze_net — nätverkspaketets felkategori. Texterna är den GENERISKA
// beskrivningen av en kod; det specifika (vilken värd, vilket systemfel)
// bärs av `Error::message`.

#include <string>

#include "glaze_net/error.h"

namespace glaze::net {
namespace {

class Kategori final : public std::error_category {
public:
    const char* name() const noexcept override { return "glaze::net"; }

    std::string message(int kod) const override
    {
        switch (static_cast<Code>(kod)) {
        case Code::Ok:              return "inget fel";
        case Code::Timeout:         return "tidsgränsen passerades";
        case Code::HostNotFound:    return "värden finns inte";
        case Code::Refused:         return "anslutningen nekades";
        case Code::Reset:           return "anslutningen bröts";
        case Code::Closed:          return "motparten stängde";
        case Code::Unreachable:     return "nätet eller värden kan inte nås";
        case Code::AddressInUse:    return "adressen är upptagen";
        case Code::InvalidArgument: return "ogiltigt argument";
        case Code::Tls:             return "fel i den krypterade förbindelsen";
        case Code::Protocol:        return "motparten svarade fel";
        case Code::Permission:      return "operativsystemet nekade";
        case Code::Failed:          return "nätverksfel";
        }
        return "okänd glaze::net-kod " + std::to_string(kod);
    }
};

} // namespace

const std::error_category& category()
{
    static const Kategori kategori;
    return kategori;
}

std::error_code make_error_code(Code c)
{
    return {static_cast<int>(c), category()};
}

} // namespace glaze::net
