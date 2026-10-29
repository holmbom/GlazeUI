// Filväljarens modell — se file_dialog_model.h för kontraktet. Ren std:
// ingenting här vet om fönster, fonter eller widgets, och ingenting kastar.

#include "file_dialog_model.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <numeric>

namespace glaze::filedlg {

namespace {

std::string trim(const std::string& s)
{
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return std::string();
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// UTF-8 → kodpunkter, ASCII-bokstäver vikta till gemener. Ett trasigt
// byte blir sig självt — ett namn på disken ska alltid gå att jämföra,
// också när det inte är giltig UTF-8.
std::vector<char32_t> codepointsFolded(const std::string& s)
{
    std::vector<char32_t> ut;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        int                 n = 0;   // fortsättningsbyten efter ledaren
        char32_t            cp = b;
        if (b >= 0xC0 && b <= 0xDF)      { n = 1; cp = b & 0x1Fu; }
        else if (b >= 0xE0 && b <= 0xEF) { n = 2; cp = b & 0x0Fu; }
        else if (b >= 0xF0 && b <= 0xF7) { n = 3; cp = b & 0x07u; }
        if (b >= 0x80 && n == 0) {   // fortsättningsbyte utan ledare
            ut.push_back(b);
            ++i;
            continue;
        }
        bool hel = i + static_cast<std::size_t>(n) < s.size();
        for (int k = 1; hel && k <= n; ++k) {
            const unsigned char f = static_cast<unsigned char>(s[i + k]);
            if ((f & 0xC0u) != 0x80u)
                hel = false;
            else
                cp = (cp << 6) | (f & 0x3Fu);
        }
        if (!hel) {
            ut.push_back(b);
            ++i;
            continue;
        }
        if (cp >= U'A' && cp <= U'Z')
            cp += U'a' - U'A';
        ut.push_back(cp);
        i += static_cast<std::size_t>(n) + 1;
    }
    return ut;
}

// Klassisk jokerteckenmatchning med `*` och `?` över kodpunkter —
// iterativ, med ett återhopp till den senaste `*`, så den är linjär i
// praktiken och kan aldrig spränga stacken på ett långt namn.
bool wildcard(const std::vector<char32_t>& s, const std::vector<char32_t>& p)
{
    std::size_t si = 0, pi = 0, stjärna = std::string::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && (p[pi] == U'?' || p[pi] == s[si])) {
            ++si;
            ++pi;
        } else if (pi < p.size() && p[pi] == U'*') {
            stjärna = pi++;
            mark    = si;
        } else if (stjärna != std::string::npos) {
            pi = stjärna + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == U'*')
        ++pi;
    return pi == p.size();
}

// Vikningen bakom namnordningen: ASCII-versaler och Latin-1-versalerna
// (U+00C0–U+00DE utom ×, i UTF-8 0xC3 0x80–0x9E) till gemener. Bytevis och
// längdbevarande, så den naturliga jämförelsen kan gå direkt på resultatet.
std::string foldForOrder(const std::string& s)
{
    std::string ut = s;
    for (std::size_t i = 0; i < ut.size(); ++i) {
        const unsigned char b = static_cast<unsigned char>(ut[i]);
        if (b >= 'A' && b <= 'Z') {
            ut[i] = static_cast<char>(b - 'A' + 'a');
        } else if (b == 0xC3 && i + 1 < ut.size()) {
            const unsigned char f = static_cast<unsigned char>(ut[i + 1]);
            if (f >= 0x80 && f <= 0x9E && f != 0x97)
                ut[i + 1] = static_cast<char>(f + 0x20);
            ++i;
        }
    }
    return ut;
}

bool isDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

// Naturlig jämförelse över två (vikta) strängar: en sifferföljd jämförs som
// ETT TAL (inledande nollor bort, sedan längd, sedan siffra för siffra),
// allt annat som osignerade byte.
int naturalCompare(const std::string& a, const std::string& b)
{
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            std::size_t ia = i, jb = j;
            while (ia < a.size() && a[ia] == '0')
                ++ia;
            while (jb < b.size() && b[jb] == '0')
                ++jb;
            std::size_t ea = ia, eb = jb;
            while (ea < a.size() && isDigit(a[ea]))
                ++ea;
            while (eb < b.size() && isDigit(b[eb]))
                ++eb;
            if (ea - ia != eb - jb)
                return ea - ia < eb - jb ? -1 : 1;
            for (std::size_t k = 0; k < ea - ia; ++k)
                if (a[ia + k] != b[jb + k])
                    return a[ia + k] < b[jb + k] ? -1 : 1;
            i = ea;
            j = eb;
            continue;
        }
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size())
        return 1;
    if (j < b.size())
        return -1;
    return 0;
}

} // namespace

// ----------------------------------------------------------------- UTF-8

fs::path pathFromUtf8(const std::string& utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8FromPath(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---------------------------------------------------------------- FILTER

std::vector<std::string> splitPatterns(const std::string& patterns)
{
    std::vector<std::string> ut;
    std::size_t              start = 0;
    while (start <= patterns.size()) {
        const std::size_t slut = patterns.find(';', start);
        const std::string del  = trim(patterns.substr(
            start, slut == std::string::npos ? std::string::npos : slut - start));
        if (!del.empty())
            ut.push_back(del);
        if (slut == std::string::npos)
            break;
        start = slut + 1;
    }
    return ut;
}

bool globMatch(const std::string& name, const std::string& pattern)
{
    if (pattern == "*" || pattern == "*.*")
        return true;
    return wildcard(codepointsFolded(name), codepointsFolded(pattern));
}

bool matchesAny(const std::string& name,
                const std::vector<std::string>& patterns)
{
    if (patterns.empty())
        return true;
    for (const std::string& p : patterns)
        if (globMatch(name, p))
            return true;
    return false;
}

std::string defaultExtension(const std::vector<std::string>& patterns)
{
    if (patterns.empty())
        return std::string();
    const std::string& p = patterns.front();
    if (p.size() < 3 || p[0] != '*' || p[1] != '.')
        return std::string();
    if (p.find_first_of("*?", 1) != std::string::npos)
        return std::string();
    return p.substr(1);   // ".png"
}

std::string extensionOf(const std::string& name)
{
    const std::size_t punkt = name.find_last_of('.');
    if (punkt == std::string::npos || punkt == 0 || punkt + 1 >= name.size())
        return std::string();
    return name.substr(punkt + 1);
}

std::string withExtension(const std::string& name, const std::string& ext)
{
    if (ext.empty() || name.empty() || !extensionOf(name).empty())
        return name;
    if (name.back() == '.')
        return name + ext.substr(1);
    return name + ext;
}

// ------------------------------------------------------------ NAMNFÄLTET

std::vector<std::string> splitNames(const std::string& field)
{
    const std::string f = trim(field);
    if (f.empty())
        return {};
    if (f[0] != '"')
        return {f};
    std::vector<std::string> ut;
    std::size_t              i = 0;
    while (i < f.size()) {
        while (i < f.size() && (f[i] == ' ' || f[i] == '\t'))
            ++i;
        if (i >= f.size())
            break;
        if (f[i] != '"') {
            // Text UTANFÖR citat mellan citerade namn är ett eget namn —
            // `"a.txt" b.txt` ger två.
            const std::size_t slut = f.find_first_of(" \t\"", i);
            ut.push_back(f.substr(i, slut == std::string::npos ? std::string::npos
                                                               : slut - i));
            i = slut == std::string::npos ? f.size() : slut;
            continue;
        }
        const std::size_t slut = f.find('"', i + 1);
        const std::string namn = trim(f.substr(
            i + 1, slut == std::string::npos ? std::string::npos : slut - i - 1));
        if (!namn.empty())
            ut.push_back(namn);
        i = slut == std::string::npos ? f.size() : slut + 1;
    }
    return ut;
}

std::string joinNames(const std::vector<std::string>& names)
{
    if (names.size() == 1)
        return names.front();
    std::string ut;
    for (const std::string& n : names) {
        if (!ut.empty())
            ut += ' ';
        ut += '"' + n + '"';
    }
    return ut;
}

// ------------------------------------------------------------- ORDNINGEN

int compareNames(const std::string& a, const std::string& b)
{
    const int v = naturalCompare(foldForOrder(a), foldForOrder(b));
    if (v != 0)
        return v;
    return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
}

// ---------------------------------------------------------------- VISNINGEN

std::string sizeText(std::uintmax_t bytes, const std::string& decimalSep,
                     const std::string& byteUnit)
{
    if (bytes < 1000)
        return std::to_string(bytes) + " " + byteUnit;
    // SI-PREFIXEN är internationella; enheten efter dem är språkets.
    static const char* const kPrefix[] = {"k", "M", "G", "T", "P"};
    double v = static_cast<double>(bytes) / 1000.0;
    int    e = 0;
    while (v >= 1000.0 && e < 4) {
        v /= 1000.0;
        ++e;
    }
    // Det VISADE talet avgör enheten: ett värde som avrundas till 1000
    // (999,9 kB) skrivs i nästa enhet ("1,0 MB"), aldrig som "1000 kB".
    // Avrundningen sker i heltal, så den aldrig beror på en lokal.
    if (static_cast<long long>(v + 0.5) >= 1000 && e < 4) {
        v /= 1000.0;
        ++e;
    }
    const long long tiondelar = static_cast<long long>(v * 10.0 + 0.5);
    if (tiondelar < 100)   // under tio: en decimal
        return std::to_string(tiondelar / 10) + decimalSep +
               std::to_string(tiondelar % 10) + " " + kPrefix[e] + byteUnit;
    return std::to_string(static_cast<long long>(v + 0.5)) + " " + kPrefix[e] +
           byteUnit;
}

std::string dateText(std::int64_t unixSeconds)
{
    const std::time_t t  = static_cast<std::time_t>(unixSeconds);
    const std::tm*    tm = std::localtime(&t);   // UI-tråden, som allt här
    if (!tm)
        return std::string();
    char buf[32];
    if (std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm) == 0)
        return std::string();
    return buf;
}

// ---------------------------------------------------------- KATALOGLÄSNINGEN

ListError classify(const std::error_code& ec)
{
    if (!ec)
        return ListError::None;
    if (ec == std::errc::permission_denied ||
        ec == std::errc::operation_not_permitted)
        return ListError::NoAccess;
    if (ec == std::errc::no_such_file_or_directory ||
        ec == std::errc::not_a_directory)
        return ListError::Missing;
    return ListError::Unreadable;
}

Listing listFolder(const fs::path& folder, bool foldersOnly,
                   const std::vector<std::string>& patterns, HiddenFn hidden,
                   bool showHidden)
{
    Listing                 ut;
    std::error_code         ec;
    fs::directory_iterator  it(folder, ec);
    const fs::directory_iterator slut;
    if (ec) {
        ut.error = classify(ec);
        return ut;
    }
    while (it != slut) {
        const fs::directory_entry& e = *it;
        Entry                      rad;
        rad.name = utf8FromPath(e.path().filename());
        // DOLT är ett punktnamn eller det OS:et döljer — men ingenting när
        // dolda poster ska visas, och då frågas OS:et inte heller.
        const bool dold =
            !showHidden && !rad.name.empty() &&
            (rad.name[0] == '.' || (hidden && hidden(utf8FromPath(e.path()))));
        if (!rad.name.empty() && !dold) {
            std::error_code fel;
            rad.folder = e.is_directory(fel);   // följer symboliska länkar
            const bool med = rad.folder ? true
                             : !foldersOnly && matchesAny(rad.name, patterns);
            if (med) {
                if (!rad.folder) {
                    const std::uintmax_t n = e.file_size(fel);
                    if (!fel) {
                        rad.size    = n;
                        rad.hasSize = true;
                    }
                }
                const fs::file_time_type ft = e.last_write_time(fel);
                if (!fel) {
                    const auto sys = std::chrono::file_clock::to_sys(ft);
                    rad.modified = std::chrono::duration_cast<std::chrono::seconds>(
                                       sys.time_since_epoch())
                                       .count();
                    rad.hasTime = true;
                }
                ut.entries.push_back(std::move(rad));
            }
        }
        it.increment(ec);
        if (ec) {
            ut.error = ListError::Unreadable;   // det som hann läsas står kvar
            break;
        }
    }
    std::stable_sort(ut.entries.begin(), ut.entries.end(),
                     [](const Entry& a, const Entry& b) {
                         if (a.folder != b.folder)
                             return a.folder;
                         return compareNames(a.name, b.name) < 0;
                     });
    return ut;
}

std::vector<std::vector<double>> sortKeys(
    const std::vector<Entry>&       entries,
    const std::vector<std::string>& typeTexts)
{
    const std::size_t n = entries.size();
    std::vector<std::vector<double>> ut(n, std::vector<double>(4, 0.0));
    std::vector<std::size_t>         ordning(n);

    // Mappar först; sedan kolumnens eget värde (`före` < 0 / 0 / > 0); sist
    // namnet. Rangen i den ordningen ÄR nyckeln.
    auto ranga = [&](int kolumn, auto&& före) {
        std::iota(ordning.begin(), ordning.end(), std::size_t{0});
        std::stable_sort(ordning.begin(), ordning.end(),
                         [&](std::size_t a, std::size_t b) {
                             const Entry& x = entries[a];
                             const Entry& y = entries[b];
                             if (x.folder != y.folder)
                                 return x.folder;
                             const int v = före(a, b);
                             if (v != 0)
                                 return v < 0;
                             return compareNames(x.name, y.name) < 0;
                         });
        for (std::size_t plats = 0; plats < n; ++plats)
            ut[ordning[plats]][std::size_t(kolumn)] = static_cast<double>(plats);
    };
    auto jämför = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };

    ranga(ColName, [](std::size_t, std::size_t) { return 0; });
    ranga(ColSize, [&](std::size_t a, std::size_t b) {
        const Entry& x = entries[a];
        const Entry& y = entries[b];
        if (x.folder || x.hasSize != y.hasSize)
            return x.folder ? 0 : (x.hasSize ? 1 : -1);
        return jämför(x.size, y.size);
    });
    ranga(ColModified, [&](std::size_t a, std::size_t b) {
        const Entry& x = entries[a];
        const Entry& y = entries[b];
        if (x.hasTime != y.hasTime)
            return x.hasTime ? 1 : -1;
        return jämför(x.modified, y.modified);
    });
    ranga(ColType, [&](std::size_t a, std::size_t b) {
        const std::string& x = a < typeTexts.size() ? typeTexts[a] : std::string();
        const std::string& y = b < typeTexts.size() ? typeTexts[b] : std::string();
        return compareNames(x, y);
    });
    return ut;
}

// ------------------------------------------------------------- SÖKVÄGARNA

namespace {

// UNC-ROTEN. En Windows-sökväg som börjar med två avskiljare och ett namn
// ("\\server\resurs\…") har servern och resursen som sin ROT — men libstdc++
// ger den inget rotnamn: `root_name()` är tomt, `is_absolute()` falskt,
// `current / "\\server\resurs"` blir "C:\\server\resurs" och
// `lexically_normal()` slår ihop de två inledande avskiljarna till en
// (med libstdc++ på UCRT64). Därför läses roten ur TEXTEN, och
// funktionerna nedan som behöver den frågar här i stället för std. Bara
// Windows-grammatiken har formen — på Linux och macOS är "//x" samma sak som
// "/x" — så svaret är alltid 0 där.
bool avskiljare(fs::path::value_type c)
{
    return c == '/' || c == fs::path::preferred_separator;
}

// Längden på UNC-roten ("\\server\resurs", eller bara "\\server") i början
// av `s`, eller 0 när `s` inte börjar med en.
std::size_t uncRootLength(const fs::path::string_type& s)
{
    if constexpr (fs::path::preferred_separator == '\\') {
        if (s.size() < 3 || !avskiljare(s[0]) || !avskiljare(s[1]) ||
            avskiljare(s[2]))
            return 0;
        std::size_t i = 2;
        while (i < s.size() && !avskiljare(s[i]))   // servern
            ++i;
        std::size_t j = i + 1;
        if (j >= s.size())
            return i;                               // "\\server", "\\server\"
        while (j < s.size() && !avskiljare(s[j]))   // resursen
            ++j;
        return j;
    } else {
        return 0;
    }
}

} // namespace

fs::path withoutTrailingSeparator(const fs::path& p)
{
    fs::path q = p;
    while (!q.empty() && !q.has_filename() && q.has_relative_path())
        q = q.parent_path();
    return q;
}

fs::path resolveTyped(const fs::path& current, const std::string& typedUtf8)
{
    return resolvePath(current, pathFromUtf8(trim(typedUtf8)));
}

fs::path resolvePath(const fs::path& current, const fs::path& t)
{
    const fs::path p =
        t.is_absolute() || uncRootLength(t.native()) > 0 ? t : current / t;
    // En UNC-sökväg normaliseras bara EFTER roten (std hade slagit ihop
    // rotens avskiljare), och ".." stannar vid resursen — ovanför den finns
    // ingen mapp att gå till.
    const std::size_t rot = uncRootLength(p.native());
    if (rot > 0) {
        fs::path       ut(p.native().substr(0, rot));
        const fs::path rest =
            fs::path(p.native().substr(rot)).relative_path().lexically_normal();
        for (const fs::path& led : rest)
            if (!led.empty() && led != "." && led != "..")
                ut /= led;
        return withoutTrailingSeparator(ut);
    }
    return withoutTrailingSeparator(p.lexically_normal());
}

fs::path nearestExisting(const fs::path& p)
{
    fs::path q = withoutTrailingSeparator(p);
    while (!q.empty()) {
        std::error_code ec;
        if (fs::is_directory(q, ec))
            return q;
        if (isRootFolder(q))
            break;
        q = q.parent_path();
    }
    return fs::path();
}

bool isRootFolder(const fs::path& p)
{
    const fs::path q = withoutTrailingSeparator(p);
    // En UNC-RESURS är en rot: servern ovanför den går inte att lista som en
    // mapp. Det som står efter roten får bara vara avskiljare.
    const std::size_t rot = uncRootLength(q.native());
    if (rot > 0)
        return std::all_of(q.native().begin() + static_cast<std::ptrdiff_t>(rot),
                           q.native().end(), avskiljare);
    return !q.has_relative_path();
}

fs::path parentFolder(const fs::path& p)
{
    const fs::path q = withoutTrailingSeparator(p);
    return isRootFolder(q) ? q : q.parent_path();
}

} // namespace glaze::filedlg
