#pragma once
#include <cassert>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

// glaze_threads — RESULTATET av ett arbete.
//
// Ett `Result<T>` bär ANTINGEN ett värde ELLER ett läsbart fel — aldrig
// båda, aldrig inget. Det är SAMMA typ i båda sidopaketen: `glaze::net`
// returnerar ett `Result<T>` från varje blockerande anrop, och `Worker`
// levererar ett `Result<T>` till fortsättningen. Felet är en
// `std::error_code` (VAD som gick fel — jämförbart) plus en text (VARFÖR —
// för loggen):
//
//     if (r)                                          visa(*r);
//     else if (r.code() == glaze::net::Code::Timeout) försökIgen();
//     else                                            status.setText(r.error());
//
// `std::error_code` är std:s egen mekanism för fel som korsar bibliotek:
// varje paket äger sin KATEGORI (`glaze::threads::Code`, `glaze::net::Code`),
// och en jämförelse mot fel kategori är falsk i stället för en tyst
// sifferkollision. Texten är TEKNISK (svenska plus systemets egen
// felbeskrivning) — UI-texten väljer appen själv ur koden, på sitt språk.
//
// Husets form är annars `bool f(…, std::string& err)`; den räcker inte här,
// för ett fel som ska fram till en fortsättning i en ANNAN tråd måste vara
// ett värde som går att flytta dit.

namespace glaze::threads {

// Trådpaketets egna felkoder. 0 är "inget fel" (std::error_code-regeln).
enum class Code {
    Ok        = 0,
    Cancelled = 1,   // stopp begärdes (std::stop_token) — se Worker om leverans
    Exception = 2,   // arbetet kastade; texten är undantagets what()
};

const std::error_category& category();
std::error_code make_error_code(Code c);   // hittas via ADL av std::error_code

} // namespace glaze::threads

// Måste stå FÖRE Result: `code() == Code::Cancelled` nedan är inte beroende
// av mallparametern och slås därför upp redan vid definitionen.
template <>
struct std::is_error_code_enum<glaze::threads::Code> : std::true_type {};

namespace glaze::threads {

// Felet — koden (vad) och texten (varför). Ett aggregat, så
// `Error{glaze::net::Code::Timeout, "…"}` fungerar direkt: enumet
// konverteras till `std::error_code` av std:s egen konstruktor.
struct Error {
    std::error_code code;
    std::string     message;
};

template <class T>
class Result {
public:
    // LYCKAT — värdet. Konverterande, så `return "text";` fungerar för ett
    // `Result<std::string>` (två användardefinierade konverteringar i rad
    // hade annars krävt `std::string("text")` på varje sådan rad).
    template <class U = T>
        requires(std::is_constructible_v<T, U &&> &&
                 !std::is_same_v<std::remove_cvref_t<U>, Result> &&
                 !std::is_same_v<std::remove_cvref_t<U>, Error>)
    Result(U&& value) : value_(std::in_place, std::forward<U>(value))
    {}

    // MISSLYCKAT — felet. Koden får inte vara 0: då vore det inget fel, och
    // `ok()` (som läser VÄRDET) och `code()` hade sagt emot varandra.
    Result(Error e) : error_(std::move(e)) { assert(error_.code); }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }
    bool cancelled() const { return error_.code == Code::Cancelled; }

    // Felet. `{}` respektive tom text när resultatet är lyckat.
    const std::error_code& code() const { return error_.code; }
    const std::string&     error() const { return error_.message; }

    // Hela felet — för att skicka det VIDARE in i ett annat `Result`:
    //     if (!r) return r.failure();
    const Error& failure() const { return error_; }

    // Värdet. FÖRUTSÄTTER `ok()` — ett misslyckat resultat har inget.
    T& value() &
    {
        assert(ok());
        return *value_;
    }
    const T& value() const&
    {
        assert(ok());
        return *value_;
    }
    T&& value() &&
    {
        assert(ok());
        return std::move(*value_);
    }
    T*       operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T&       operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::optional<T> value_;
    Error            error_;
};

// `void`-arbete har inget värde — bara lyckat eller ett fel. Ett
// default-konstruerat `Result<void>` är LYCKAT, så `return {};` räcker.
template <>
class Result<void> {
public:
    Result() = default;
    Result(Error e) : error_(std::move(e)), ok_(false) { assert(error_.code); }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    bool cancelled() const { return error_.code == Code::Cancelled; }

    const std::error_code& code() const { return error_.code; }
    const std::string&     error() const { return error_.message; }
    const Error&           failure() const { return error_; }

private:
    Error error_;
    bool  ok_ = true;
};

} // namespace glaze::threads
