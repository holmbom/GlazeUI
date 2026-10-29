// glaze_threads — trådpaketets felkategori. Texterna är den GENERISKA
// beskrivningen av en kod; den specifika (vilket undantag, vilken värd)
// bärs av `Error::message`.

#include "glaze_threads/result.h"

namespace glaze::threads {
namespace {

class Kategori final : public std::error_category {
public:
    const char* name() const noexcept override { return "glaze::threads"; }

    std::string message(int kod) const override
    {
        switch (static_cast<Code>(kod)) {
        case Code::Ok:        return "inget fel";
        case Code::Cancelled: return "avbrutet";
        case Code::Exception: return "undantag i arbetet";
        }
        return "okänd glaze::threads-kod " + std::to_string(kod);
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

} // namespace glaze::threads
