#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

// Talet som text — det SpinBox och PropertyGrids talrader delar: vilka tecken
// som ingår i ett skrivet tal, användarens text → tal, klampning och
// avrundning, och talet → text. Internt i libglaze (en header, inga
// statiska tillstånd); allt sker i "C"-lokalen, så en app som anropat
// `setlocale` kan inte ändra vad ett tal betyder.

namespace glaze::numbertext {

// Utöver siffrorna i den fasta formen: tecknet, punkten och avslutningen.
inline constexpr int kFixedSlack = 3;

// Tecken som kan ingå i ett skrivet tal. Allt annat sväljs redan vid
// tangenttrycket — en bokstav i ett talfält är aldrig vad användaren menar.
inline bool isNumberChar(char ch)
{
    return (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == ',' ||
           ch == '.';
}

inline std::string trim(const std::string& s)
{
    const std::size_t a = s.find_first_not_of(" \t");
    const std::size_t b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// ANVÄNDARENS TEXT → tal. Generös där det är ofarligt och strikt där det
// inte är det: valfritt tecken, siffror, och HÖGST ETT decimaltecken — ","
// ELLER "." oavsett språk, för numerisk tangentbords decimaltangent ger det
// layouten säger och användaren ska inte behöva tänka på det. Två
// avskiljare ("1,000.5") är TVETYDIGT — tusentalsgrupp eller fel? — och
// tolkas inte alls; inte heller exponenter, som ingen skriver i ett talfält.
inline bool parseUser(const std::string& in, double& out)
{
    std::string s = trim(in);
    if (s.empty())
        return false;
    std::size_t i = (s[0] == '+' || s[0] == '-') ? 1u : 0u;
    int siffror = 0, avskiljare = 0;
    for (std::size_t k = i; k < s.size(); ++k) {
        if (s[k] >= '0' && s[k] <= '9') {
            ++siffror;
        } else if (s[k] == ',' || s[k] == '.') {
            ++avskiljare;
            s[k] = '.';
        } else {
            return false;
        }
    }
    if (siffror == 0 || avskiljare > 1)
        return false;
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0.0;
    is >> v;
    if (is.fail() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

// KLAMPA och AVRUNDA. Avrundningen sker efter klampningen och klampas igen:
// en gräns som inte är en multipel av 10^−decimals (min 0.05 med en decimal)
// får då aldrig ett värde utanför intervallet, även om visningen rundar den.
// `decimals` < 0 avrundar inte. En övre gräns under den nedre är den nedre.
// Aldrig −0: "−0,0" är ingen siffra någon vill se.
inline double effective(double v, double lo, double hi, int decimals)
{
    hi = std::max(lo, hi);
    if (!std::isfinite(v))
        v = lo;
    v = std::clamp(v, lo, hi);
    if (decimals >= 0) {
        const double skal = std::pow(10.0, decimals);
        const double rund = std::round(v * skal) / skal;
        if (std::isfinite(rund))
            v = std::clamp(rund, lo, hi);
    }
    return v == 0.0 ? 0.0 : v;
}

// TALET → text med decimaltecknet `sep`. `decimals` ≥ 0 är fasta decimaler,
// < 0 den kortaste form som läses tillbaka exakt (utan exponent — texten ska
// kunna tolkas av `parseUser`).
inline std::string format(double v, int decimals, const std::string& sep)
{
    std::string s;
    if (decimals >= 0) {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << std::fixed << std::setprecision(decimals) << v;
        s = os.str();
    } else {
        // Den längsta fasta formen av en double: talets heltalsdel upp till
        // `max_exponent10` siffror, punkten och de decimaler kortaste formen
        // behöver — med marginal för tecknet.
        char       buf[std::numeric_limits<double>::max_exponent10 +
                       std::numeric_limits<double>::max_digits10 + kFixedSlack];
        const auto r =
            std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
        s.assign(buf, r.ptr);
    }
    const std::size_t punkt = s.find('.');
    if (punkt != std::string::npos)
        s.replace(punkt, 1, sep);
    return s;
}

} // namespace glaze::numbertext
