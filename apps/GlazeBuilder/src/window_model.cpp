// Värde-hjälparna: rå C++-uttryckstext ↔ typade värden för de fält
// buildern själv redigerar (placering m.m.). Ren std — headless-testbar.

#include "window_model.h"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>   // std::size — arvskedjans vandringstak
#include <utility>

namespace bld {

namespace {

std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// FÖNSTERANROPEN VARS ARGUMENT ÄR EN MEDLEM —
// `setAcceptButton(okBtn);`: argumentet är NODENS NAMN, så ett namnbyte och en
// borttagning måste följa med (annars skriver buildern kod som inte
// kompilerar). `nullptr` och andra argument rörs inte.
constexpr const char* kMemberRefWindowCalls[] = {"setAcceptButton",
                                                 "setCancelButton"};

// Pekar satsen ut medlemmen `id` som argument till ett sådant anrop?
bool windowCallRefersTo(const Stmt& s, const std::string& id)
{
    if (s.kind != Stmt::Kind::WindowCall || trim(s.value) != id)
        return false;
    for (const char* m : kMemberRefWindowCalls)
        if (s.name == m)
            return true;
    return false;
}

} // namespace

// DELMÄNGDEN står som kontrakt i window_model.h — den här funktionen och
// `cppString` nedan är varandras EXAKTA inverser på den, och grinden
// (`roundtrip_gate`) mäter båda riktningarna byte för byte.
bool parseCppString(const std::string& v, std::string& out)
{
    const std::string t = trim(v);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"')
        return false;
    out.clear();
    for (size_t i = 1; i + 1 < t.size(); ++i) {
        if (t[i] == '\\') {
            // Escapen måste ha ett tecken INNANFÖR literalen efter sig.
            // `"abc\"` har det inte: slutcitatet är då escapat, alltså
            // ingen delimiter, och raden är ingen komplett literal.
            if (i + 2 >= t.size())
                return false;
            const char e = t[++i];
            switch (e) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            case 'r':  out += '\r'; break;
            default:
                // `\x41`, oktalt, `\u`, `\b`, `\f`, `\0`, `\å` … Avvisas
                // hellre än avkodas: emissionen kan inte skriva tillbaka
                // dem i sin ursprungliga form, och en tyst omskrivning av
                // användarens rad är exakt vad regel 4 förbjuder.
                return false;
            }
            continue;
        }
        // ETT OESCAPAT citattecken inuti betyder att det här INTE är en enda
        // strängliteral. Utan kontrollen räckte det att första och sista
        // tecknet var `"`, så C++:s sammanslagna form `"a" "b"` slank igenom
        // som ett element med innehållet `a" "b` — trots att kommentaren vid
        // `parseStringList` uttryckligen lovar att den avvisas. Avvisad
        // betyder OPAK rad, aldrig en tyst förvrängd sträng.
        if (t[i] == '"')
            return false;
        // RÅA KONTROLLBYTES avvisas — annars är round-trippen inte
        // byte-exakt: en rå TAB hade lästs in och sedan emitterats som
        // `\t`, alltså semantiskt samma men textuellt en annan rad. (En rå
        // LF kan för övrigt inte ens stå i en C++-literal.) UTF-8 ligger
        // på ≥ 0x80 och berörs aldrig.
        if (static_cast<unsigned char>(t[i]) < 0x20)
            return false;
        out += t[i];
    }
    return true;
}

// LISTLITERALEN — `{"a", "b"}` → tre steg: skalet, splitten, elementen.
// FÖRLUSTFRI med flit: den semikolonseparerade formen som
// `setProp("items", …)` tar kan inte representera ett item som INNEHÅLLER
// semikolon, ett tomt element eller en escape — och den befintliga
// splittern tappar dessutom ett avslutande tomt element. Den här vägen
// avkodar C++-literaler och klarar alla tre.
//
// STRIKT delmängd, som `parseImgCall`/`parseEnumTail`: varje element måste
// vara EN strängliteral. Sammanslagna literaler (`{"a" "b"}`, som C++
// konkatenerar) och uttryck (`{namn}`) avvisas — och avvisad betyder att
// raden förblir OPAK, aldrig att den degraderas till en skräplista.
// En avslutande komma (`{"a", }`) är laglig C++ och tillåts därför.
bool splitArgs(const std::string& s, std::vector<std::string>& out)
{
    out.clear();
    std::string       cur;
    std::string       stack;   // '{' och '(' i den ordning de öppnats
    bool              iStr = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (iStr && c == '\\' && i + 1 < s.size()) {
            cur += c;
            cur += s[++i];
            continue;
        }
        if (c == '"') {
            iStr = !iStr;
            cur += c;
            continue;
        }
        if (!iStr) {
            if (c == '{' || c == '(') {
                stack += c;
            } else if (c == '}' || c == ')') {
                const char vantad = (c == '}') ? '{' : '(';
                // STACKEN, inte två räknare: `({)}` har summan noll i båda
                // räknarna men är ändå fel ordning.
                if (stack.empty() || stack.back() != vantad)
                    return false;
                stack.pop_back();
            } else if (c == ',' && stack.empty()) {
                out.push_back(cur);
                cur.clear();
                continue;
            }
        }
        cur += c;
    }
    if (iStr || !stack.empty())
        return false;
    // Det sista elementet läggs till om det bär något ELLER om det finns
    // tidigare element (`f(a, )` har två argument, `f()` har noll). Regeln
    // är `delaKlammer`s, inte `delaArgument`s: `pg.setRows({ })` ska
    // fortsätta betyda en TOM lista och inte en oparsbar rad.
    if (!trim(cur).empty() || !out.empty())
        out.push_back(cur);
    return true;
}

bool parseStringList(const std::string& v, std::vector<std::string>& out)
{
    const std::string t = trim(v);
    if (t.size() < 2 || t.front() != '{' || t.back() != '}')
        return false;
    const std::string inre = trim(t.substr(1, t.size() - 2));
    out.clear();
    if (inre.empty())
        return true;   // {} = tom lista, inte ett fel

    // Splitta på kommatecken UTANFÖR strängliteraler — ett item får
    // innehålla både komma och klammer.
    std::vector<std::string> delar;
    std::string              cur;
    bool                     iStr = false;
    for (size_t i = 0; i < inre.size(); ++i) {
        const char c = inre[i];
        if (iStr && c == '\\' && i + 1 < inre.size()) {
            cur += c;
            cur += inre[++i];
            continue;
        }
        if (c == '"')
            iStr = !iStr;
        if (c == ',' && !iStr) {
            delar.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (iStr)
        return false;   // oavslutad literal
    delar.push_back(cur);

    for (size_t i = 0; i < delar.size(); ++i) {
        const std::string d = trim(delar[i]);
        if (d.empty()) {
            // Bara som AVSLUTANDE komma — `{"a", , "b"}` är inte C++.
            if (i + 1 == delar.size() && i > 0)
                break;
            return false;
        }
        std::string item;
        if (!parseCppString(d, item))
            return false;
        out.push_back(std::move(item));
    }
    return true;
}

bool parseIndexPath(const std::string& v, std::vector<int>& out)
{
    const std::string t = trim(v);
    if (t.size() < 2 || t.front() != '{' || t.back() != '}')
        return false;
    const std::string inre = trim(t.substr(1, t.size() - 2));
    std::vector<int>  stig;
    std::size_t       start = 0;
    while (start < inre.size()) {
        const std::size_t komma = inre.find(',', start);
        const std::string led =
            trim(inre.substr(start, komma == std::string::npos
                                        ? std::string::npos
                                        : komma - start));
        // STRIKT: decimala siffror och inget annat — och högst nio, så att
        // talet alltid ryms i en `int` utan en overflowfråga.
        if (led.empty() || led.size() > 9)
            return false;
        for (char ch : led)
            if (ch < '0' || ch > '9')
                return false;
        stig.push_back(std::atoi(led.c_str()));
        if (komma == std::string::npos)
            break;
        start = komma + 1;
        // En AVSLUTANDE komma (`{0, 2, }`) är laglig C++ — men bara sist.
        if (trim(inre.substr(start)).empty())
            break;
    }
    out = std::move(stig);
    return true;
}

std::string indexPathText(const std::vector<int>& p)
{
    std::string s = "{";
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (i)
            s += ", ";
        s += std::to_string(p[i]);
    }
    return s + "}";
}

bool cppString(const std::string& raw, std::string& ut)
{
    std::string out = "\"";
    for (char c : raw) {
        switch (c) {
        case '"':  out += "\\\""; continue;
        case '\\': out += "\\\\"; continue;
        case '\n': out += "\\n";  continue;
        case '\t': out += "\\t";  continue;
        case '\r': out += "\\r";  continue;
        default: break;
        }
        // Utanför delmängden: `\b`, `\f`, `\0` och övriga kontrollbytes.
        // Att emittera dem RÅTT ger kod som inte kompilerar, och att koda
        // dem hade utökat delmängden i bara EN riktning — då är
        // round-trippen inte längre en inverspar. Felkanalen är
        // anroparens sak.
        if (static_cast<unsigned char>(c) < 0x20)
            return false;
        out += c;
    }
    out += '"';
    ut = std::move(out);
    return true;
}

bool parseNum(const std::string& v, float& out)
{
    const std::string t = trim(v);
    if (t.empty())
        return false;
    // `strtof` avkodar ÄVEN "nan" och "inf" — och de är INTE fristående
    // C++-literaler. En sådan rad hade passerat builderns grind
    // ("`.glazeui` får aldrig bära text som inte kompilerar"), gett previewn
    // ett NaN-mått som förgiftar all geometri den rör, och sedan vägrat
    // kompilera i den byggda appen. Kravet är därför att talet börjar som
    // ett C++-tal gör: siffra, punkt, eller tecken följt av en av dem.
    const char c0 = t[0];
    const std::size_t d = (c0 == '+' || c0 == '-') ? 1 : 0;
    if (d >= t.size())
        return false;
    const char c1 = t[d];
    if (!std::isdigit(static_cast<unsigned char>(c1)) && c1 != '.')
        return false;
    char* end = nullptr;
    out = std::strtof(t.c_str(), &end);
    // tillåt f-suffix (12.5f)
    if (end && (*end == 'f' || *end == 'F'))
        ++end;
    if (end != t.c_str() + t.size())
        return false;
    // Hexflyttal (`0x1p3`) klarar sig genom sifferkravet ovan; overflow
    // ("1e400") ger HUGE_VALF. Båda fångas av ändlighetskravet.
    return std::isfinite(out);
}

std::string numText(float v)
{
    // Kanonisk emission: heltal utan decimaler, annars kort decimalform.
    // Heltalsprovet bara inom `long long` (en minnesbudget i byte
    // är ett stort tal): utanför hade omvandlingen varit odefinierad.
    if (std::isfinite(v) && std::fabs(v) < 9.0e18f &&
        v == static_cast<long long>(v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        return buf;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

bool parseTwoInts(const std::string& v, int& a, int& b)
{
    // Strikt form: <heltal> "," <heltal> och INGET mer — ett släpande
    // tredje argument ska falla igenom till den opaka vägen i stället för
    // att tyst tolkas som två (regel 4: gissa aldrig).
    const char* p   = v.c_str();
    char*       end = nullptr;
    const long  x   = std::strtol(p, &end, 10);
    if (end == p)
        return false;
    while (*end == ' ' || *end == '\t')
        ++end;
    if (*end != ',')
        return false;
    const char* q = end + 1;
    const long  y = std::strtol(q, &end, 10);
    if (end == q)
        return false;
    while (*end == ' ' || *end == '\t')
        ++end;
    if (*end != '\0')
        return false;
    a = static_cast<int>(x);
    b = static_cast<int>(y);
    return true;
}

bool parseAnchorsText(const std::string& v, bool out[4])
{
    const std::string t = trim(v);
    if (t.size() < 2 || t.front() != '{' || t.back() != '}')
        return false;
    std::string inner = t.substr(1, t.size() - 2);
    int idx = 0;
    size_t start = 0;
    while (idx < 4) {
        const size_t comma = inner.find(',', start);
        const std::string part =
            trim(comma == std::string::npos ? inner.substr(start)
                                            : inner.substr(start, comma - start));
        if (part == "true")
            out[idx] = true;
        else if (part == "false")
            out[idx] = false;
        else
            return false;
        ++idx;
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return idx == 4;
}

std::string anchorsText(const bool a[4])
{
    std::string out = "{";
    for (int i = 0; i < 4; ++i) {
        if (i)
            out += ", ";
        out += a[i] ? "true" : "false";
    }
    out += "}";
    return out;
}

bool parseSiblingAnchorText(const std::string& v, std::string& to, float& gap)
{
    const std::string t = trim(v);
    if (t.size() < 2 || t.front() != '{' || t.back() != '}')
        return false;
    const std::string inre = trim(t.substr(1, t.size() - 2));

    // `{}` = INGET ankare. Formen är laglig C++ (aggregatet nollställs) och
    // betyder exakt det fältets tomma `to` betyder, så den avkodas i
    // stället för att bli opak — en rad som KOMPILERAR och som previewn
    // inte utför är ett WYSIWYG-brott (hård regel 8), inte en detalj.
    if (inre.empty()) {
        to.clear();
        gap = 0.0f;
        return true;
    }

    // Splitta på det komma som ligger UTANFÖR strängliteralen — ett id får
    // i princip innehålla komma (det är en sträng), och escapes hoppas
    // över så `\"` inte avslutar den.
    size_t komma = std::string::npos;
    bool   iStr  = false;
    for (size_t i = 0; i < inre.size(); ++i) {
        const char c = inre[i];
        if (iStr && c == '\\' && i + 1 < inre.size()) {
            ++i;
            continue;
        }
        if (c == '"')
            iStr = !iStr;
        else if (c == ',' && !iStr) {
            komma = i;
            break;
        }
    }
    // ETT element (`{"knapp7"}`) är också laglig C++ — `gap` tar sitt
    // default 0 — och avkodas därför, av samma skäl som `{}` ovan.
    // Emissionen skriver alltid tvåformen; den här grenen finns för
    // HANDSKRIVEN kod, som hård regel 8 gör likvärdig.
    if (komma == std::string::npos) {
        gap = 0.0f;
        return parseCppString(inre, to);
    }
    return parseCppString(trim(inre.substr(0, komma)), to) &&
           parseNum(trim(inre.substr(komma + 1)), gap);
}

// INVARIANT: `to` är ett NODNAMN — en C++-identifierare — och
// alltså alltid kodningsbart. Skulle den ändå inte vara det svarar
// funktionen med TOM sträng i stället för en halv literal; anroparen
// vägrar då operationen. Ingen tyst förvrängning, ingen okompilerbar rad.
std::string siblingAnchorText(const std::string& to, float gap)
{
    std::string lit;
    if (!cppString(to, lit))
        return {};
    return "{" + lit + ", " + numText(gap) + "}";
}

bool isSiblingAnchorField(const std::string& field)
{
    return field == "anchorLeft" || field == "anchorTop" ||
           field == "anchorRight" || field == "anchorBottom";
}

bool parseEnumTail(const std::string& v, std::string& outLower)
{
    const std::string t = trim(v);
    const size_t p = t.rfind("::");
    if (p == std::string::npos || p + 2 >= t.size())
        return false;
    outLower.clear();
    for (size_t i = p + 2; i < t.size(); ++i) {
        const char c = t[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
        outLower += static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return true;
}

bool parseImgCall(const std::string& v, std::string& list, std::string& key)
{
    // Strikt form: glaze::img( <strängliteral> , <strängliteral> ) —
    // exakt vad emissionen skriver. Allt annat = false (opak rad,
    // regel 4: gissa aldrig).
    std::string t = trim(v);
    const std::string prefix = "glaze::img(";
    if (t.rfind(prefix, 0) != 0 || t.back() != ')')
        return false;
    t = trim(t.substr(prefix.size(), t.size() - prefix.size() - 1));

    // Två citerade literaler separerade av ett komma UTANFÖR citaten.
    size_t komma = std::string::npos;
    bool   inStr = false;
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '"' && (i == 0 || t[i - 1] != '\\'))
            inStr = !inStr;
        else if (c == ',' && !inStr) {
            komma = i;
            break;
        }
    }
    if (komma == std::string::npos)
        return false;
    return parseCppString(trim(t.substr(0, komma)), list) &&
           parseCppString(trim(t.substr(komma + 1)), key);
}

// Samma invariant som `siblingAnchorText`: argumenten är en LISTA och en
// NYCKEL ur assets.bin, aldrig fri text. TOM sträng = kunde inte emitteras.
std::string imgCallText(const std::string& list, const std::string& key)
{
    std::string l, k;
    if (!cppString(list, l) || !cppString(key, k))
        return {};
    return "glaze::img(" + l + ", " + k + ")";
}

// ---------------------------------------------------- ÖVERSÄTTNINGEN

bool parseTrCall(const std::string& v, std::uint32_t& id)
{
    // Strikt form: glaze::tr( <decimalt heltal> ) — exakt vad emissionen
    // skriver. Allt annat = false (opak rad, regel 4: gissa aldrig).
    std::string             t      = trim(v);
    const std::string       prefix = "glaze::tr(";
    if (t.size() <= prefix.size() || t.rfind(prefix, 0) != 0 || t.back() != ')')
        return false;
    t = trim(t.substr(prefix.size(), t.size() - prefix.size() - 1));
    if (t.empty())
        return false;
    // BARA SIFFROR: inget tecken, inget suffix (`1042u`), ingen bas
    // (`0x412`), ingen konstant. Previewn får aldrig tolka en rad
    // annorlunda än C++ gör, och att GISSA vad `kNågot` betyder är just
    // det. Elva siffror räcker för att fånga overflow före `strtoul`.
    if (t.size() > 10)
        return false;
    for (char c : t)
        if (c < '0' || c > '9')
            return false;
    const unsigned long n = std::strtoul(t.c_str(), nullptr, 10);
    if (n == 0 || n > 0xFFFFFFFFul)
        return false;   // 0 betyder "ingen översättning"
    id = static_cast<std::uint32_t>(n);
    return true;
}

std::string trCallText(std::uint32_t id)
{
    if (id == 0)
        return {};
    return "glaze::tr(" + std::to_string(id) + ")";
}

TextValueKind classifyTextValue(const std::string& v, std::string& text,
                                std::uint32_t& id)
{
    if (parseTrCall(v, id))
        return TextValueKind::TrId;
    const std::string t = trim(v);
    if (parseCppString(t, text))
        return TextValueKind::Literal;
    // ETT ensamt tr-anrop med något annat än ett tal i: prefixet, en
    // avslutande parentes OCH ett inre som är balanserat för sig — annars hade
    // `glaze::tr(a) + glaze::tr(b)` (prefix och slut stämmer, men inre är
    // `a) + glaze::tr(b`) räknats som ett anrop. `splitArgs` är grammatikens
    // EN strukturella splittare (stacken över `{}`/`()`, strängliteraler
    // hoppade).
    const std::string        prefix = "glaze::tr(";
    std::vector<std::string> delar;
    if (t.size() > prefix.size() + 1 && t.rfind(prefix, 0) == 0 &&
        t.back() == ')' &&
        splitArgs(t.substr(prefix.size(), t.size() - prefix.size() - 1),
                  delar) &&
        !delar.empty())
        return TextValueKind::TrExpr;
    return TextValueKind::Other;
}

// ------------------------------------------------------ MENYSTIGEN

namespace {

// ETT segment: strängliteral ELLER strikt `glaze::tr(N)`. Inget annat.
bool parseSeg(const std::string& raw, PathSeg& ut)
{
    const std::string t = trim(raw);
    std::uint32_t     id = 0;
    if (parseTrCall(t, id)) {
        ut.isId = true;
        ut.id   = id;
        ut.text.clear();
        return true;
    }
    std::string lit;
    if (!parseCppString(t, lit))
        return false;
    // SNEDSTRECKREGELN — se kontraktet i headern. Utan den hade en
    // omigrerad `"Utseende/Läge"` tyst blivit ETT segment som aldrig
    // matchar någon meny.
    if (lit.find('/') != std::string::npos)
        return false;
    ut.isId = false;
    ut.id   = 0;
    ut.text = lit;
    return true;
}

} // namespace

bool parseMenuPath(const std::string& v, std::vector<PathSeg>& out)
{
    out.clear();
    const std::string t = trim(v);
    if (t.empty())
        return false;

    std::vector<std::string> delar;
    if (t.front() == '{') {
        if (t.back() != '}')
            return false;
        const std::string inre = t.substr(1, t.size() - 2);
        if (!splitArgs(inre, delar))
            return false;
    } else {
        // ETT segment utan klamrar — `addMenu("Arkiv")`, den form som
        // står i varje befintlig fil.
        delar.push_back(t);
    }

    std::vector<PathSeg> segs;
    for (const std::string& d : delar) {
        PathSeg s;
        if (!parseSeg(d, s))
            return false;
        segs.push_back(std::move(s));
    }
    if (segs.empty())
        return false;   // TOM STIG är ogiltig i TYPEN, inte bara i parsern
    out = std::move(segs);
    return true;
}

std::string menuPathText(const std::vector<PathSeg>& segs)
{
    if (segs.empty())
        return {};
    auto ettSeg = [](const PathSeg& s, std::string& ut) {
        if (s.isId) {
            ut = trCallText(s.id);
            return !ut.empty();
        }
        if (s.text.find('/') != std::string::npos)
            return false;
        return cppString(s.text, ut);
    };
    if (segs.size() == 1) {
        std::string ut;
        return ettSeg(segs[0], ut) ? ut : std::string();
    }
    std::string ut = "{";
    for (std::size_t i = 0; i < segs.size(); ++i) {
        std::string s;
        if (!ettSeg(segs[i], s))
            return {};
        ut += (i ? ", " : "") + s;
    }
    return ut + "}";
}

// ------------------------------------------------------------- satslistan
//
// Allt nedan läser eller muterar EN lista — modellen har ingen andra
// bokföring att hålla i takt .

const Stmt* WindowModel::lastWindowCall(const std::string& method) const
{
    const Stmt* hit = nullptr;
    for (const auto& s : stmts)
        if (s.kind == Stmt::Kind::WindowCall && s.name == method)
            hit = &s;   // sista förekomsten gäller
    return hit;
}

// Titeln som TEXT — bara när den är en STRÄNGLITERAL. En `glaze::tr(N)`-titel
// kan `builder_core` inte lösa upp (ren std, ingen språkbutik), och ett annat
// uttryck kan ingen lösa; båda svarar TOMT. Den RÅA uttryckstexten hade
// fått glaze-render att konstruera fönstret med uttrycket som titel.
// Den som HAR en språkbutik
// läser satsen själv (`lastWindowCall("setTitle")` — builderns rubrik) eller
// utför den på ett riktigt fönster (`bld::applyWindowCall`).
std::string WindowModel::title() const
{
    std::string t;
    const Stmt* s = lastWindowCall("setTitle");
    if (s && parseCppString(s->value, t))
        return t;
    return {};
}

int WindowModel::clientWidth() const
{
    int w = 0, h = 0;
    const Stmt* s = lastWindowCall("setClientSize");
    return (s && parseTwoInts(s->value, w, h)) ? w : 800;
}

int WindowModel::clientHeight() const
{
    int w = 0, h = 0;
    const Stmt* s = lastWindowCall("setClientSize");
    return (s && parseTwoInts(s->value, w, h)) ? h : 520;
}

const Stmt* WindowModel::adoptionOf(const std::string& id) const
{
    for (const auto& s : stmts)
        if (s.kind == Stmt::Kind::Adopt && s.target == id)
            return &s;
    return nullptr;
}

std::string WindowModel::parentOf(const std::string& id) const
{
    const Stmt* s = adoptionOf(id);
    return s ? s->name : std::string{};
}

const Stmt* WindowModel::findProp(const std::string& id,
                                  const std::string& field) const
{
    for (const auto& s : stmts)
        if (s.kind == Stmt::Kind::Prop && s.target == id && s.name == field)
            return &s;
    return nullptr;
}

Stmt* WindowModel::findProp(const std::string& id, const std::string& field)
{
    for (auto& s : stmts)
        if (s.kind == Stmt::Kind::Prop && s.target == id && s.name == field)
            return &s;
    return nullptr;
}

void WindowModel::setProp(const std::string& id, const std::string& field,
                          const std::string& value)
{
    if (Stmt* s = findProp(id, field)) {
        s->value = value;
        return;
    }
    Stmt s;
    s.kind   = Stmt::Kind::Prop;
    s.target = id;
    s.name   = field;
    s.value  = value;
    stmts.push_back(std::move(s));
}

void WindowModel::appendCall(const std::string& id, const std::string& method,
                             const std::string& args)
{
    Stmt s;
    s.kind   = Stmt::Kind::Call;
    s.target = id;
    s.name   = method;
    s.value  = args;
    stmts.push_back(std::move(s));
}

void WindowModel::appendEvent(const std::string& id, const EventBind& e)
{
    Stmt s;
    s.kind    = Stmt::Kind::Event;
    s.target  = id;
    s.name    = e.event;
    s.args    = e.args;
    s.handler = e.handler;
    // FORMEN måste med. Missas raden skriver writern
    // tillbaka en kort koppling som fullform, och hela round-trip-
    // garantin faller på ett fält som glömdes i en tilldelning.
    s.shortForm = e.shortForm;
    // SENDERTYPEN likaså: utan den skrivs
    // en timers koppling tillbaka med `glaze::Widget* s` — och kompilerar
    // inte längre mot `Handler<TimerArgs, Timer>`.
    s.sender = e.sender;
    stmts.push_back(std::move(s));
}

void WindowModel::appendWindowEvent(const EventBind& e)
{
    Stmt s;
    s.kind    = Stmt::Kind::WindowEvent;
    s.name    = e.event;
    s.args    = e.args;
    s.handler = e.handler;
    s.shortForm = e.shortForm;   // se appendEvent ovan
    s.sender    = e.sender;
    stmts.push_back(std::move(s));
}

void WindowModel::setAdoption(const std::string& id, const std::string& parent)
{
    for (auto& s : stmts)
        if (s.kind == Stmt::Kind::Adopt && s.target == id) {
            s.name = parent;
            return;
        }
    Stmt s;
    s.kind   = Stmt::Kind::Adopt;
    s.target = id;
    s.name   = parent;
    stmts.push_back(std::move(s));
}

void WindowModel::setWindowCall(const std::string& method,
                                const std::string& args)
{
    // Sista förekomsten uppdateras PÅ PLATS — se duplikatpolicyn i .h.
    for (auto it = stmts.rbegin(); it != stmts.rend(); ++it)
        if (it->kind == Stmt::Kind::WindowCall && it->name == method) {
            it->value = args;
            return;
        }
    Stmt s;
    s.kind  = Stmt::Kind::WindowCall;
    s.name  = method;
    s.value = args;
    stmts.push_back(std::move(s));
}

void WindowModel::appendWindowCall(const std::string& method,
                                   const std::string& args)
{
    Stmt s;
    s.kind  = Stmt::Kind::WindowCall;
    s.name  = method;
    s.value = args;
    stmts.push_back(std::move(s));
}

WidgetNode& WindowModel::appendNode(const std::string& type,
                                    const std::string& id)
{
    WidgetNode n;
    n.type = type;
    n.id   = id;
    nodes.push_back(std::move(n));
    return nodes.back();
}

void WindowModel::removeNode(const std::string& id)
{
    std::vector<Stmt> kept;
    kept.reserve(stmts.size());
    for (auto& s : stmts) {
        // Ett fönsteranrop med den borttagna noden som ARGUMENT
        // (`setAcceptButton(id);`) går med noden — kvar hade
        // det pekat på en medlem som inte finns, och filen inte kompilerat.
        if (windowCallRefersTo(s, id))
            continue;
        if (s.target != id || s.kind == Stmt::Kind::WindowCall ||
            s.kind == Stmt::Kind::WindowEvent)
            kept.push_back(std::move(s));
    }
    stmts = std::move(kept);

    std::vector<WidgetNode> keptNodes;
    keptNodes.reserve(nodes.size());
    for (auto& n : nodes)
        if (n.id != id)
            keptNodes.push_back(std::move(n));
    nodes = std::move(keptNodes);
}

// ------------------------------------------------- fält ↔ setter

namespace {

// ARVSKEDJAN — widget- och komponentheadernas egna basklasser, som en
// tabell över DIREKT förälder ("" = kedjans rot). `builder_core` är ren
// std och kan inte fråga C++ om typerna, så den här listan ÄR
// klassningen; `preview_gate` grindar att varje palettrad och varje
// registrerad glaze-komponent står med.
//
// TVÅ RÖTTER: `glaze::Widget` och
// `glaze::Component`, den icke-visuella medlemmens bas. En komponent ärver
// aldrig Widget, så `{"glaze::Widget", …, true}`-raderna nedan (drag, tooltip,
// pekaren) träffar den inte.
struct TypeParent {
    const char* cppType;
    const char* parent;   // "" = kedjans rot
};

constexpr TypeParent kTypeParents[] = {
    {"glaze::Widget",        ""},
    {"glaze::Panel",         "glaze::Widget"},
    {"glaze::Bar",           "glaze::Panel"},
    {"glaze::MenuBar",       "glaze::Bar"},
    {"glaze::TopBar",        "glaze::Bar"},
    {"glaze::StatusBar",     "glaze::Bar"},
    {"glaze::ToolBar",       "glaze::Bar"},
    {"glaze::PropertyGrid",  "glaze::Panel"},
    {"glaze::ListBox",       "glaze::Panel"},
    // KOLUMNHUVUDET: den DELADE basen ColumnPanel ärver Panel —
    // och därmed clip/border. Den registreras inte (skyddad konstruktor) men
    // står här så att kedjorna är hela.
    {"glaze::ColumnPanel",   "glaze::Panel"},
    // TRÄDET ärver Panel som listan, genom
    // ColumnPanel (kolumnerna).
    {"glaze::TreeView",      "glaze::ColumnPanel"},
    // TABELLEN: ListView och DataGrid ärver den DELADE basen
    // Table, som ärver ColumnPanel. Table registreras inte (skyddad
    // konstruktor) men står här så att kedjan är hel.
    {"glaze::Table",         "glaze::ColumnPanel"},
    {"glaze::ListView",      "glaze::Table"},
    {"glaze::DataGrid",      "glaze::Table"},
    {"glaze::Label",         "glaze::Widget"},
    {"glaze::Button",        "glaze::Widget"},
    {"glaze::Checkbox",      "glaze::Widget"},
    {"glaze::Radio",         "glaze::Widget"},
    {"glaze::TextInput",     "glaze::Widget"},
    {"glaze::Dropdown",      "glaze::Widget"},
    {"glaze::Scrollbar",     "glaze::Widget"},
    {"glaze::Separator",     "glaze::Widget"},
    {"glaze::Picture",       "glaze::Widget"},
    {"glaze::Link",          "glaze::Widget"},
    {"glaze::Slider",        "glaze::Widget"},
    {"glaze::Progress",      "glaze::Widget"},
    {"glaze::Spacer",        "glaze::Widget"},
    {"glaze::CaptionButton", "glaze::Widget"},
    {"glaze::ToolButton",    "glaze::Widget"},
    // INTE Panel — gruppramen ärver Widget med flit (group_box.h), och då
    // ärver den heller inte Panels clip/border-poster.
    {"glaze::GroupBox",      "glaze::Widget"},
    {"glaze::SpinBox",       "glaze::Widget"},
    // INTE Panel — delaren ärver Widget (splitter.h): den scrollar inte och
    // har varken clip- eller border-sättare.
    {"glaze::Splitter",      "glaze::Widget"},
    // DOCKNINGEN — båda ärver Widget: värden scrollar inte och
    // panelen är ingen Panel (inga clip/border-sättare).
    {"glaze::DockHost",      "glaze::Widget"},
    {"glaze::DockPanel",     "glaze::Widget"},
    // FLIKARNA — flikkontrollen ärver Widget (den scrollar inte och
    // har inga clip/border-sättare), sidan ÄR en Panel och ärver därmed
    // clip/border.
    {"glaze::TabControl",    "glaze::Widget"},
    {"glaze::TabPage",       "glaze::Panel"},
    // FILVÄLJARFÄLTET — en komposit som ärver Widget (talfältets
    // form): inga clip/border-sättare.
    {"glaze::FilePicker",    "glaze::Widget"},
    // FÄRGVÄLJAREN ärver Widget — popupen är ett eget fönster, inte
    // barn i trädet.
    {"glaze::ColorPicker",   "glaze::Widget"},
    // DATUMVÄLJAREN — talfältets komposit, ärver Widget.
    {"glaze::DatePicker",    "glaze::Widget"},
    // DET FLERRADIGA FÄLTET ÄR en Panel (ListBox-prejudikatet:
    // remsorna och hjulet) — och ärver därmed clip/border.
    {"glaze::TextArea",      "glaze::Panel"},
    // MARKDOWN-VYN ÄR en Panel (TextArea-
    // prejudikatet: remsorna och hjulet) — och ärver därmed clip/border.
    {"glaze::MarkdownView",  "glaze::Panel"},
    // DEN FLYTTBARA PANELEN ÄR en Panel — och ärver därmed
    // clip/border.
    {"glaze::FloatingPanel", "glaze::Panel"},
    // TERMINALEN ÄR en Panel (MarkdownView-prejudikatet:
    // remsorna och hjulet) — och ärver därmed clip/border. Typen bor i
    // sidopaketet Terminal; här är den bara ett namn (builder_core länkar
    // ingenting), och utan paketet blir den en stand-in.
    {"glaze::Terminal",      "glaze::Panel"},
    // RITYTAN ärver Widget — den scrollar inte och har
    // ingen Panel-yta; dess `setBorder` är dess egen.
    {"glaze::Canvas",        "glaze::Widget"},
    // PROGRAMIKONEN ärver Widget — ett chrome-barn som
    // TopBar men ingen Bar: den bär inga barn och har ingen Panel-yta.
    {"glaze::AppIcon",       "glaze::Widget"},
    // DE ICKE-VISUELLA MEDLEMMARNA — den andra roten
    // och dess två inbyggda typer. Medlemmar i regionen som aldrig adopteras.
    {"glaze::Component",     ""},
    {"glaze::Timer",         "glaze::Component"},
    {"glaze::TrayIcon",      "glaze::Component"},
};

// De fält som är PRIVATA med setter i C++ — alltså
// exakt de som INTE går att skriva som `<id>.<fält> = …;`. Listan är
// migreringslistan: en ny privatisering är en rad här.
//
// TYPMEDVETEN — se `SetterFieldSpec` i
// headern för skälet. Regeln för en ny rad: den typ
// vars `setProp()` FAKTISKT känner nyckeln. Regel 6 säger att samma
// KONCEPT bär samma namn där konceptet finns — inte att varje widget
// erbjuder konceptet, och `nullptr` är därför nästan alltid fel.
//
// LAYOUT-fälten står medvetet INTE med (`x`, `y`, `width`, `height`,
// `right`, `bottom`, `dock`, `anchors`, `tabIndex`, `name`): de är den
// GENERERADE kodens yta — fem rader per widget i varje `.glazeui` — och
// som settrar hade hela fältrads-grammatiken dött till ingen
// användarnytta.
//
// `color` står INTE med, av samma skäl: `Label::setProp("color")` är ett
// ALIAS för `role` och vill ha ett ROLLNAMN, medan `Label::setColor`
// tar en riktig `gfx::Color`. Namnet delades, värderepresentationen inte.
// Därför blir `label.setColor(gfx::Color{…});` en Call som `applyCall` utför.
constexpr SetterFieldSpec kSetterFields[] = {
    // text — ÅTTA EXAKTA poster här (och ToolBars vid bandet nedan,
    // DockPanels och TabPages vid sina block). `nullptr` vore fel: Panel,
    // Slider, PropertyGrid, ListBox och Spacer har ingen `setProp("text")`
    // alls.
    {"glaze::Button",        "text",          false},
    {"glaze::Checkbox",      "text",          false},
    {"glaze::Radio",         "text",          false},
    {"glaze::Label",         "text",          false},
    {"glaze::Link",          "text",          false},
    {"glaze::TextInput",     "text",          false},
    {"glaze::ToolButton",    "text",          false},
    {"glaze::GroupBox",      "text",          false},
    // img finns på ALLA TRE bildbärarna — binds den bara till Button
    // försvinner egenskapspanelens bildrad för de andra (panelen visar
    // bara Props).
    {"glaze::Button",        "img",           false},
    {"glaze::Picture",       "img",           false},
    {"glaze::ToolButton",    "img",           false},
    {"glaze::Button",        "imagePos",      false},
    {"glaze::ToolButton",    "imagePos",      false},
    // IKONENS SIDA — tabellernas namn, ett tal.
    // Knapparnas TEXTFÄRG står INTE här: `setColor(std::optional)` tar
    // `gfx::Color{…}` eller `std::nullopt` och är en anropssats (apply_props).
    {"glaze::Button",        "iconSize",      false},
    {"glaze::ToolButton",    "iconSize",      false},
    // DIALOGKNAPPEN — ett enum-värde, EXAKT Button:
    // `ok.setDialogResult(glaze::DialogResult::OK);`. Fönstrets
    // `setAcceptButton(ok);` är ett naket fönsteranrop och står inte här.
    {"glaze::Button",        "dialogResult",  false},
    // checked — KOLLISIONEN. `ListBox::setChecked(int,bool)` träffar
    // ingen av de tre och blir därmed en Call — likaså listvyns och trädets.
    {"glaze::Checkbox",      "checked",       false},
    // KRYSSRUTANS TRE LÄGEN — EXAKT Checkbox: en
    // bool (`setThreeState(true)`) och ett enum-värde
    // (`setCheckState(glaze::CheckState::Indeterminate)`).
    {"glaze::Checkbox",      "threeState",    false},
    {"glaze::Checkbox",      "checkState",    false},
    {"glaze::Radio",         "checked",       false},
    {"glaze::ToolButton",    "checked",       false},
    {"glaze::ToolButton",    "checkable",     false},
    // DEN DELADE KNAPPEN — ett bool-argument, setter-fält som
    // `checkable`. Rullgardinens poster (`addItem({}, …)` och postens tre
    // tillstånd) är Call-satser, som programikonens.
    {"glaze::ToolButton",    "split",         false},
    // VERKTYGSFÄLTETS BAND — ett heltal, setter-fält som
    // flikkontrollens `selectedIndex`. Kanten är det råa layoutfältet `dock`.
    // Titeln i det flytande fönstrets rubrik heter `text`
    // som på varje widget (tar `glaze::tr`), och det flytande läget och
    // flytbarheten är bools som panelens — previewn utför tillståndet (fältet
    // lämnar bandet) men öppnar aldrig ett fönster; platsen
    // `setFloatingPosition(x, y)` är en Call-sats.
    {"glaze::ToolBar",       "band",          false},
    {"glaze::ToolBar",       "text",          false},
    {"glaze::ToolBar",       "floating",      false},
    {"glaze::ToolBar",       "floatable",     false},
    // ÖVERFLÖDET — en bool, förval på: `verktyg.setOverflow(false);`.
    {"glaze::ToolBar",       "overflow",      false},
    {"glaze::Label",         "role",          false},
    {"glaze::Label",         "wrap",          false},
    {"glaze::Label",         "align",         false},
    // KORTNINGEN, HÖJDLEDET OCH GRANNEN — en bool,
    // ett enum (`glaze::VAlign::Top`) och ett syskons namn (`renameNode`
    // följer med när grannen döps om).
    {"glaze::Label",         "elide",         false},
    {"glaze::Label",         "vAlign",        false},
    {"glaze::Label",         "buddy",         false},
    // BREDDGRUPPEN — ett gruppnamn som en strängliteral,
    // `lblNamn.setWidthGroup("form");`. Inget nodnamn: `renameNode` rör den
    // inte (gruppen är ett namn för sig, som Radio-gruppen).
    {"glaze::Label",         "widthGroup",    false},
    // GLORIANS BREDD — ett tal i logiska px, 0 = stilens. Färgen,
    // `setHalo(std::optional<gfx::Color>)`, är en anropssats (apply_props).
    {"glaze::Label",         "haloWidth",     false},
    {"glaze::TextInput",     "align",         false},
    {"glaze::TextInput",     "readOnly",      false},
    // LÖSENORDSFÄLTET — EXAKT TextInput: det flerradiga fältet
    // har inget lösenordsläge. Utan raden hade `losen.setPassword(true);`
    // emitterats som fältraden `losen.password = true;`, som inte kompilerar.
    {"glaze::TextInput",     "password",      false},
    // PLATSHÅLLAREN OCH MAXLÄNGDEN — på BÅDA fälten
    // (TextArea nedan), samma namn för samma sak. Hinten tar en literal
    // eller `glaze::tr(N)`, gränsen ett heltal.
    {"glaze::TextInput",     "placeholder",   false},
    {"glaze::TextInput",     "maxLength",     false},
    {"glaze::Dropdown",      "items",         false},
    {"glaze::ListBox",       "items",         false},
    // Dropdownens `align` — samma namn och samma värdeform som
    // Labels och TextInputs: datagridens val-editor står i kolumnens
    // justering, och det är en publik egenskap på dropdownen.
    {"glaze::Dropdown",      "align",         false},
    {"glaze::Dropdown",      "selectedIndex", false},
    {"glaze::ListBox",       "selectedIndex", false},
    {"glaze::ListBox",       "showChecks",    false},
    // LISTBOXENS FLERVAL — listvyns och trädets
    // bool; `setSelected(rad, på)` tar två och är en Call.
    {"glaze::ListBox",       "multiSelect",   false},
    // TRÄDETS val — listans `selectedIndex` med en STIG där listan
    // har ett index. Värdet är en klammerliteral (`{0, 2}`) och avkodas
    // TYPAT i previewn, som `items`. Trädets kollektionsanrop (`addNode`/
    // `setNode`/`setNodes`/`setExpanded`/…) står medvetet INTE här: de tar
    // flera argument eller ett rekursivt aggregat och är Call-satser, som
    // PropertyGrids `setRows`/`addRow`. Flervalet och kryssens synlighet
    // är däremot ETT bool-argument och setter-fält;
    // `setChecked(stig, på)`/`setSelected`/`setNodeColor` tar två och är
    // Call-satser, som ListBoxens `setChecked`.
    {"glaze::TreeView",      "selectedPath",  false},
    {"glaze::TreeView",      "multiSelect",   false},
    {"glaze::TreeView",      "showChecks",    false},
    // TRÄDETS KOLUMNER — ColumnPanels sortering, huvud och
    // fyllkolumn, samma rader som listvyns; kolumnerna och cellerna
    // (`setCell(stig, kolumn, text)`) är Call-satser.
    {"glaze::TreeView",      "sortColumn",    false},
    {"glaze::TreeView",      "sortAscending", false},
    {"glaze::TreeView",      "sortable",      false},
    {"glaze::TreeView",      "headerLines",   false},
    {"glaze::TreeView",      "headerVAlign",  false},
    {"glaze::TreeView",      "fillColumn",    false},
    {"glaze::Slider",        "min",           false},
    {"glaze::Scrollbar",     "min",           false},
    {"glaze::Progress",      "min",           false},
    {"glaze::SpinBox",       "min",           false},
    {"glaze::Slider",        "max",           false},
    {"glaze::Scrollbar",     "max",           false},
    {"glaze::Progress",      "max",           false},
    {"glaze::SpinBox",       "max",           false},
    {"glaze::Slider",        "value",         false},
    {"glaze::Scrollbar",     "value",         false},
    {"glaze::Progress",      "value",         false},
    {"glaze::SpinBox",       "value",         false},
    {"glaze::Slider",        "step",          false},
    {"glaze::SpinBox",       "step",          false},
    {"glaze::SpinBox",       "decimals",      false},
    {"glaze::Slider",        "vertical",      false},
    {"glaze::Separator",     "vertical",      false},
    {"glaze::Scrollbar",     "orientation",   false},
    {"glaze::Scrollbar",     "page",          false},
    {"glaze::Radio",         "group",         false},
    {"glaze::Link",          "url",           false},
    {"glaze::Picture",       "fit",           false},
    // clip/border ÄRVS av Bar, PropertyGrid, ListBox, TreeView — och
    // transitivt av MenuBar/TopBar/StatusBar. (De andra med `subtypes` är drag-and-drop-
    // paret längst ner, vars bas är `glaze::Widget` själv.)
    {"glaze::Panel",         "clip",          true},
    {"glaze::Panel",         "border",        true},
    // REN RAD — en bool, EXAKT Bar (flexraden); chrome-ättlingarna
    // är inte gjorda för den.
    {"glaze::Bar",           "plain",         false},
    {"glaze::Spacer",        "grow",          false},
    {"glaze::CaptionButton", "kind",          false},
    {"glaze::CaptionButton", "shape",         false},
    {"glaze::CaptionButton", "restoreGlyph",  false},
    {"glaze::PropertyGrid",  "nameWidth",     false},
    // LISTVYN — sorteringen är Tables och valet listvyns. EXAKTA
    // typer och inte `subtypes` mot Table: basen registreras inte, och en
    // användartyp utanför arvstabellen ska behålla sin anropsform. Kryssens
    // synlighet är listvyns ETT bool-argument, som
    // ListBoxens och trädets; `setChecked(rad, på)` tar två och är en Call.
    {"glaze::ListView",      "selectedIndex", false},
    {"glaze::ListView",      "multiSelect",   false},
    {"glaze::ListView",      "showChecks",    false},
    {"glaze::ListView",      "sortColumn",    false},
    {"glaze::ListView",      "sortAscending", false},
    {"glaze::ListView",      "sortable",      false},
    // KOLUMNHUVUDET — Tables, alltså båda ättlingarna: radantalet
    // (0 = AUTO) och justeringen i höjdled. Rubrikens justering i sidled är
    // kolumnaggregatets fjärde fält och åker med kollektionens Call-satser.
    {"glaze::ListView",      "headerLines",   false},
    {"glaze::ListView",      "headerVAlign",  false},
    // FYLLKOLUMNEN OCH IKONSTORLEKEN — Tables,
    // ett heltal och ett tal; datagridens rader nedan.
    {"glaze::ListView",      "fillColumn",    false},
    {"glaze::ListView",      "iconSize",      false},
    // RADRUBRIKERNA — Tables ETT bool-argument, båda ättlingarna.
    {"glaze::ListView",      "rowHeaders",    false},
    // DATAGRIDDEN — samma sortering och samma huvud, och inget
    // eget setter-fält: kolumntypen och den aktuella cellen tar två
    // argument och är Call-satser (`setColumnEditor`/`setColumnChoices`/
    // `setCurrentCell`).
    {"glaze::DataGrid",      "sortColumn",    false},
    {"glaze::DataGrid",      "sortAscending", false},
    {"glaze::DataGrid",      "sortable",      false},
    {"glaze::DataGrid",      "headerLines",   false},
    {"glaze::DataGrid",      "headerVAlign",  false},
    {"glaze::DataGrid",      "fillColumn",    false},
    {"glaze::DataGrid",      "iconSize",      false},
    {"glaze::DataGrid",      "rowHeaders",    false},
    // DATAGRIDDENS RADVAL — ett enum-värde
    // (`setSelectionMode(glaze::DataGrid::SelectionMode::FullRow)`) och
    // listvyns bool; `setSelected(rad, på)` är en Call.
    {"glaze::DataGrid",      "selectionMode", false},
    {"glaze::DataGrid",      "multiSelect",   false},
    // DELAREN — `vertical` är SAMMA namn och samma betydelse som
    // Separatorns (linjen är stående), därav en exakt rad till och inte en
    // generisk post.
    {"glaze::Splitter",      "vertical",      false},
    {"glaze::Splitter",      "fixedPane",     false},
    {"glaze::Splitter",      "paneSize",      false},
    {"glaze::Splitter",      "firstMin",      false},
    {"glaze::Splitter",      "secondMin",     false},
    // DOCKNINGEN. Panelens rubrik heter `text` som på varje widget;
    // värdens `layout` är strängen ur `layout()` — en setter, så raden kan
    // stå i `.glazeui` och utföras av previewn (efter trädbygget).
    // Stängknappens synlighet heter `closable` som
    // flikkontrollens, och ikonen `img` som Buttons (`glaze::img(…)`). Min/max
    // (två tal), `activate()` och värdens `dockPanel(…)` (identifierare) är
    // Call-satser i `applyCall`. Knappnålen heter `pinned`
    // (`egenskaper.setPinned(false);` — handtaget ritas i previewn).
    // `floating` och `floatable` — previewn utför tillståndet
    // (panelen lämnar layouten) men öppnar aldrig ett fönster; platsen
    // `setFloatingBounds(x, y, b, h)` är en Call-sats.
    {"glaze::DockPanel",     "text",          false},
    {"glaze::DockPanel",     "collapsed",     false},
    {"glaze::DockPanel",     "pinned",        false},
    {"glaze::DockPanel",     "closable",      false},
    {"glaze::DockPanel",     "floating",      false},
    {"glaze::DockPanel",     "floatable",     false},
    {"glaze::DockPanel",     "img",           false},
    {"glaze::DockHost",      "layout",        false},
    // FLIKARNA. Sidans rubrik heter `text` som på varje widget —
    // det är FLIKENS titel; flikkontrollens val heter `selectedIndex` som
    // listornas, och stängknapparna är en bool. Sidorna själva är barn och
    // uttrycks med trädbyggets `adopt`, inte med en kollektion.
    {"glaze::TabPage",       "text",          false},
    {"glaze::TabControl",    "selectedIndex", false},
    {"glaze::TabControl",    "closable",      false},
    // FILVÄLJARFÄLTET. Sökvägen är VÄRDET (`path`, inte `text` —
    // talfältets `value`-prejudikat) och läget ett enum. Filtren är en
    // LISTA och därmed Call-satser (`addFilter`/`setFilter`/`setFilters`),
    // som tabellens kolumner.
    {"glaze::FilePicker",    "path",          false},
    {"glaze::FilePicker",    "mode",          false},
    // DIALOGENS TITEL — en literal eller
    // `glaze::tr(N)` (`ärTextfält` i apply_props tar tr-vägen).
    {"glaze::FilePicker",    "dialogTitle",   false},
    // FÄRGVÄLJAREN. Värdet är HEXTEXTEN (`farg.setColor("#3366FF");`)
    // — EXAKT typen och inte `nullptr`: `Label::setColor` tar en riktig
    // `gfx::Color` och dess `setProp("color")` vill ha ett ROLLNAMN, så
    // samma namn med en annan värdeform hos Label är precis den kollision
    // `color` står utanför tabellen för (se noten ovanför tabellen). Här är en sträng
    // värdet, och `setProp("color")` tar just den.
    {"glaze::ColorPicker",   "color",         false},
    // ALFAN — en bool; `setColor("#RRGGBBAA")` är
    // färgens setter-fält ovan.
    {"glaze::ColorPicker",   "allowAlpha",    false},
    // DATUMVÄLJARENS KLOCKSLAG — en bool; tiden
    // själv tar två heltal (`setTime(14, 30)`) och är en Call, som datumet.
    {"glaze::DatePicker",    "showTime",      false},
    // DET FLERRADIGA FÄLTET. Texten heter `text` och skyddet
    // `readOnly` som på det enradiga, brytningen `wrap` som på Label (regel
    // 6 — samma sak, samma namn). Texten FÅR bära radbrytningar: `\n` är en
    // vanlig escape i strängliteralen och round-trippar byte-exakt.
    {"glaze::TextArea",      "text",          false},
    {"glaze::TextArea",      "readOnly",      false},
    {"glaze::TextArea",      "wrap",          false},
    {"glaze::TextArea",      "placeholder",   false},
    {"glaze::TextArea",      "maxLength",     false},
    // TAB SKRIVER EN TABB — en bool.
    {"glaze::TextArea",      "acceptsTab",    false},
    // TEXTSTORLEKEN — ett MÅTT, `glaze::px(…)`/`glaze::mm(…)` eller
    // ett naket tal, på de fyra som har en (samma namn och samma form);
    // previewn sätter det TYPAT (`bld::applyProp`).
    {"glaze::TextInput",     "textSize",      false},
    {"glaze::TextArea",      "textSize",      false},
    {"glaze::ListBox",       "textSize",      false},
    {"glaze::Dropdown",      "textSize",      false},
    // MARKDOWN-VYN. Dokumentet heter `text` (regel
    // 6) och är EN literal med `\n` eller `glaze::tr(N)`; bilderna
    // (`setImage(namn, bild)`) och ankaret (`scrollToAnchor`) är Call-satser.
    {"glaze::MarkdownView",  "text",          false},
    // DEN FLYTTBARA PANELEN. Rubriken heter `text` (regel 6) och
    // stängknappen `closable` som dockningspanelens; platsen är layoutfälten.
    // Förankringen på en rityta är ritytans Call (`setChildAnchor`).
    {"glaze::FloatingPanel", "text",          false},
    {"glaze::FloatingPanel", "closable",      false},
    // TERMINALEN — programmet, utseendet och historiken,
    // ett värde var i `setProp`-form. Miljön (`setEnvironmentVariable`) och de
    // egna färgerna (`setSchemeColor`) har flera argument och är Call-satser;
    // `start()`/`kill()`/`write(…)` utförs aldrig av previewn.
    {"glaze::Terminal",      "program",          false},
    {"glaze::Terminal",      "arguments",        false},
    {"glaze::Terminal",      "workingDirectory", false},
    {"glaze::Terminal",      "colorMode",        false},
    {"glaze::Terminal",      "colorScheme",      false},
    {"glaze::Terminal",      "scrollback",       false},
    {"glaze::Terminal",      "altAsMeta",        false},
    {"glaze::Terminal",      "textSize",         false},
    // RITYTAN — de sättare som tar ETT värde i samma
    // form som `setProp`-texten: rutnätet, kanten (ritytans egen, den är
    // ingen Panel), y-riktningen, kamerans tal, gesterna och etiketternas
    // toning. Färglitteralen (`setBackground(gfx::Color{…})`), de med två
    // argument (`setCenter`, `setZoomRange`, `setMapScale`), kameran, lagren
    // och formlistan är Call-satser — `applyCall`s ritytegren.
    // `focusable` är ingen sättare: ritytan är alltid ett tabbstopp.
    {"glaze::Canvas",        "gridSize",      false},
    {"glaze::Canvas",        "border",        false},
    // PLATT YTA — en bool, kant i kant med grannarna.
    {"glaze::Canvas",        "flat",          false},
    {"glaze::Canvas",        "yUp",           false},
    {"glaze::Canvas",        "zoom",          false},
    {"glaze::Canvas",        "bearing",       false},
    {"glaze::Canvas",        "pitch",         false},
    {"glaze::Canvas",        "maxPitch",      false},
    {"glaze::Canvas",        "pannable",      false},
    {"glaze::Canvas",        "zoomable",      false},
    {"glaze::Canvas",        "rotatable",     false},
    {"glaze::Canvas",        "pitchable",     false},
    {"glaze::Canvas",        "inertia",       false},
    {"glaze::Canvas",        "labelFade",     false},
    // VRIDSNÄPPET: steget och toleransen i grader
    // — `karta.setRotationSnap(90);`, ett tal var.
    {"glaze::Canvas",        "rotationSnap",  false},
    {"glaze::Canvas",        "rotationSnapTolerance", false},
    // I RYMDEN: 3D-målets provtagning (ett heltal) och
    // djup och orbitens tumling (bools) — ett värde var, och `Canvas::setProp`
    // känner nycklarna. 3D-kameran, ljuset, lagrens renderläge, del-vyerna och
    // orbitens spann (två argument) är Call-satser i `applyCall`s ritytegren,
    // och `setTicking` likaså: tickningen har ingen `setProp`-nyckel.
    {"glaze::Canvas",        "multisample",   false},
    {"glaze::Canvas",        "depth",         false},
    {"glaze::Canvas",        "orbitTumble",   false},
    // BINDNINGARNAS LÄGE — en strängliteral; raderna själva
    // (`setBinding` …) är Call-satser i `applyCall`s ritytegren.
    {"glaze::Canvas",        "inputMode",     false},
    // MINNET: ritytans minnesbudget i byte — ett heltal.
    {"glaze::Canvas",        "memoryBudget",  false},
    // PROGRAMIKONEN. Bilderna är handtag — värdet är
    // `glaze::img("lista", "nyckel")` eller `{}` (= förvalet), som Buttons
    // `img` — och den stora formen och "ingen ikon alls" är bools. EXAKT
    // typen: `icon` och `large` är namn som andra typer kan vilja ha med en
    // annan värdeform (`color`-noten ovan). Programmenyn (`addItem({}, …)`
    // med den TOMMA stigen som roten) är Call-satser, som menyradens.
    {"glaze::AppIcon",       "icon",          false},
    {"glaze::AppIcon",       "smallIcon",     false},
    {"glaze::AppIcon",       "large",         false},
    {"glaze::AppIcon",       "noIcon",        false},
    // DRAG-AND-DROP — basens egenskaper, alltså på VARJE
    // glaze-typ: `Widget::setProp` känner nycklarna. `subtypes` mot
    // `glaze::Widget` och INTE `nullptr`: en okänd användartyp ska behålla
    // sin anropsform (`isOrDerivesFrom` svarar nej för den), precis som för
    // clip/border.
    {"glaze::Widget",        "allowDrag",     true},
    {"glaze::Widget",        "allowDrop",     true},
    // TOOLTIPEN — basens textfält, samma form som drag-paret:
    // `Widget::setProp` känner nyckeln och `Widget::setTextToken` tar ett
    // översättnings-id, på varje glaze-typ.
    {"glaze::Widget",        "toolTip",       true},
    // DE ICKE-VISUELLA MEDLEMMARNA. Timerns två värden
    // och systemfältsikonens fem — bilderna är handtag som AppIcons
    // (`ikon.setIcon(glaze::img("lista", "nyckel"));`), resten ett värde var.
    // EXAKT typen: `interval`, `icon` och `toolTip` finns på andra typer med
    // en annan värdeform eller ett annat arv. Starten, menyn och bilderna i
    // animationen är Call-satser (`applyComponentStmt`).
    {"glaze::Timer",         "interval",      false},
    {"glaze::Timer",         "singleShot",    false},
    {"glaze::TrayIcon",      "icon",          false},
    {"glaze::TrayIcon",      "smallIcon",     false},
    {"glaze::TrayIcon",      "templateImage", false},
    {"glaze::TrayIcon",      "toolTip",       false},
    {"glaze::TrayIcon",      "interval",      false},
    // PEKAREN — basens egenskap som drag-paret och
    // tooltipen: `w.setCursor(gfx::PointerShape::Hand);` på varje glaze-typ.
    // `resetCursor()` är en Call.
    {"glaze::Widget",        "cursor",        true},
};

// Fältnamnet finns NÅGONSTANS i tabellen — previewns generösa väg.
bool anyTypeHasField(const std::string& field)
{
    for (const SetterFieldSpec& s : kSetterFields)
        if (field == s.field)
            return true;
    return false;
}

} // namespace

std::span<const SetterFieldSpec> setterFieldSpecs()
{
    return std::span<const SetterFieldSpec>(kSetterFields);
}

bool isKnownType(const std::string& cppType)
{
    for (const TypeParent& t : kTypeParents)
        if (cppType == t.cppType)
            return true;
    return false;
}

bool isOrDerivesFrom(const std::string& cppType, const std::string& base)
{
    std::string cur = cppType;
    // Kedjan är ändlig och tabellen är handskriven — vandra med ett tak så
    // en felskriven rad blir en utebliven träff och aldrig ett häng.
    for (std::size_t steg = 0; steg <= std::size(kTypeParents); ++steg) {
        if (cur == base)
            return true;
        const TypeParent* hit = nullptr;
        for (const TypeParent& t : kTypeParents)
            if (cur == t.cppType) {
                hit = &t;
                break;
            }
        if (!hit || *hit->parent == '\0')
            return false;
        cur = hit->parent;
    }
    return false;
}

bool isSetterField(const std::string& cppType, const std::string& field)
{
    for (const SetterFieldSpec& s : kSetterFields) {
        if (field != s.field)
            continue;
        if (!s.cppType)
            return true;                       // alla glaze-typer
        if (s.subtypes ? isOrDerivesFrom(cppType, s.cppType)
                       : cppType == s.cppType)
            return true;
    }
    return false;
}

std::string setterName(const std::string& field)
{
    if (field.empty())
        return {};
    std::string s = "set" + field;
    s[3] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[3])));
    return s;
}

namespace {

// "setText" → "text". false = inte ens formen `set` + versal.
bool methodToField(const std::string& method, std::string& out)
{
    if (method.rfind("set", 0) != 0 || method.size() < 4)
        return false;
    std::string f = method.substr(3);
    f[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(f[0])));
    out = std::move(f);
    return true;
}

} // namespace

bool setterField(const std::string& cppType, const std::string& method,
                 std::string& field)
{
    std::string f;
    if (!methodToField(method, f) || !isSetterField(cppType, f))
        return false;
    field = std::move(f);
    return true;
}

bool setterField(const std::string& method, std::string& field)
{
    std::string f;
    if (!methodToField(method, f) || !anyTypeHasField(f))
        return false;
    field = std::move(f);
    return true;
}

bool isGlazeType(const std::string& cppType)
{
    return cppType.rfind("glaze::", 0) == 0;
}

// ------------------------------------------------------------ designtaggen

bool parseDesignLine(const std::string& text, std::vector<DesignPair>& out)
{
    out.clear();
    size_t i = 0;
    while (i < text.size()) {
        // Hoppa blanksteg mellan fälten.
        if (text[i] == ' ' || text[i] == '\t') {
            ++i;
            continue;
        }
        const size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t')
            ++i;
        const std::string fält = text.substr(start, i - start);

        const size_t eq = fält.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == fält.size())
            return false;   // saknat '=', tom nyckel eller tomt värde
        const std::string key   = fält.substr(0, eq);
        const std::string value = fält.substr(eq + 1);

        for (char c : key)
            if (c < 'a' || c > 'z')
                return false;   // nyckel = [a-z]+
        for (const auto& p : out)
            if (p.key == key)
                return false;   // dubblerad nyckel — tvetydigt, avkoda inget

        out.push_back(DesignPair{key, value});
    }
    return true;
}

// ------------------------------------------------ inbyggda fönstermedlemmar

namespace {
// Publika värde-fält på `glaze::AppWindow` som adopteras i fönstrets
// konstruktor — de deklareras alltså aldrig i GlazeBuilder-regionen.
// `statusBar` och `menuBar` är DOLDA tills något gör
// `<id>.show();`; `appIcon` är den första som är
// SYNLIG som förval — `appIcon.hide();` döljer den i fönstret.
// Att det är en TABELL och inte ett specialfall är hela poängen: nästa
// inbyggda fönsterwidget är en RAD, inte en gren.
constexpr BuiltinMember kBuiltins[] = {
    {"statusBar", "glaze::StatusBar"},
    {"menuBar",   "glaze::MenuBar"},
    {"appIcon",   "glaze::AppIcon"},
};
} // namespace

const BuiltinMember* builtinMember(const std::string& id)
{
    for (const auto& b : kBuiltins)
        if (id == b.id)
            return &b;
    return nullptr;
}

// ------------------------------------------------------- NAMN-INVARIANTEN
//
// Kontraktet står i .h. Allt här är REN STD och grindas headless av
// `roundtrip_gate` — precis som resten av modellen.

namespace {

// C++:s nyckelord (C++20) + de fyra kontextuella som ändå är för
// förvirrande att tillåta som medlemsnamn. Listan är statisk med flit:
// ett namn som inte kompilerar får aldrig komma in i modellen, för
// `.glazeui` ÄR C++ som byggs.
constexpr const char* kNyckelord[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
    "bitor", "bool", "break", "case", "catch", "char", "char8_t",
    "char16_t", "char32_t", "class", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "continue",
    "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline",
    "int", "long", "mutable", "namespace", "new", "noexcept", "not",
    "not_eq", "nullptr", "operator", "or", "or_eq", "private",
    "protected", "public", "register", "reinterpret_cast", "requires",
    "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "thread_local",
    "throw", "true", "try", "typedef", "typeid", "typename", "union",
    "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
    "while", "xor", "xor_eq",
};

// Alla `name`-rader för en nod, i satsordning. Readern pushar `Prop`
// DIREKT (till skillnad från `setProp`, som skriver om på plats), så en
// handredigerad fil KAN bära två.
std::vector<const Stmt*> nameRader(const WindowModel& m,
                                   const std::string& id)
{
    std::vector<const Stmt*> ut;
    for (const Stmt& s : m.stmts)
        if (s.kind == Stmt::Kind::Prop && s.target == id && s.name == "name")
            ut.push_back(&s);
    return ut;
}

} // namespace

bool validNodeName(const std::string& s)
{
    if (s.empty())
        return false;
    const unsigned char c0 = static_cast<unsigned char>(s[0]);
    if (!std::isalpha(c0) && s[0] != '_')
        return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    for (const char* k : kNyckelord)
        if (s == k)
            return false;
    return true;
}

NameState nameState(const WindowModel& m, const WidgetNode& n)
{
    const std::vector<const Stmt*> rader = nameRader(m, n.id);
    if (rader.empty())
        return NameState::Saknas;
    if (rader.size() > 1)
        return NameState::Avviker;
    std::string v;
    if (!parseCppString(rader[0]->value, v) || v != n.id)
        return NameState::Avviker;
    return NameState::Kanoniskt;
}

bool migrateNames(WindowModel& m, std::string& fel)
{
    // FÖRST hela valideringen, SEDAN muteringen — annars kan en fil bli
    // halvmigrerad innan en avvikande nod hittas.
    for (const WidgetNode& n : m.nodes) {
        // BUILTINS kräver att raden SAKNAS — inte att den är kanonisk.
        // `menuBar`/`statusBar` får sina runtime-namn av `AppWindow`s
        // konstruktor, och en `name`-rad i filen APPLICERAS av previewn och
        // glaze-render (apply_props) OVANPÅ det. En rad som råkar vara
        // kanonisk är alltså bara ofarlig av en slump, medan
        // `menuBar.name = "annat";` tyst dödar varje ankare mot "menuBar"
        // och ändå öppnade filen redigerbart. Att hoppa över builtins helt
        // vore ett hål.
        if (n.builtin) {
            if (nameState(m, n) != NameState::Saknas) {
                fel = "'" + n.id + "' är en INBYGGD fönstermedlem och får "
                      "aldrig bära en name-rad: namnet sätts av AppWindow, "
                      "och en rad i filen skriver över det. Ta bort raden.";
                return false;
            }
            continue;
        }
        if (nameState(m, n) == NameState::Avviker) {
            fel = "'" + n.id + "' bär en name-rad som inte är nodens namn "
                  "(eller flera name-rader) — namnet ÄR medlemsnamnet och "
                  "kan inte vara något annat. Rätta raden för hand eller "
                  "döp om medlemmen.";
            return false;
        }
    }
    for (const WidgetNode& n : m.nodes) {
        if (n.builtin || nameState(m, n) != NameState::Saknas)
            continue;
        std::string lit;
        if (!cppString(n.id, lit))
            continue;   // INVARIANT: ett nodnamn är en C++-identifierare

        Stmt s;
        s.kind   = Stmt::Kind::Prop;
        s.target = n.id;
        s.name   = "name";
        s.value  = lit;

        // PLACERINGEN, inte bara närvaron: `setProp` appendar SIST i hela
        // satslistan, alltså efter trädbygget — raden hade kompilerat men
        // hamnat i ett eget block under `root().adopt(...)`. Den ska in
        // FÖRST i nodens EGET block, precis där `placeAt` skriver den för
        // en ny nod, så en migrerad fil ser ut som en nyskapad.
        std::size_t pos     = m.stmts.size();
        bool        hittad  = false;
        for (std::size_t i = 0; i < m.stmts.size(); ++i) {
            const Stmt& e = m.stmts[i];
            if ((e.kind == Stmt::Kind::Prop || e.kind == Stmt::Kind::Call) &&
                e.target == n.id) {
                pos    = i;
                hittad = true;
                break;
            }
        }
        if (!hittad) {
            // Noden har inga egna rader alls (bara en adopt-sats) — lägg
            // den nya raden FÖRE event- och trädbygg-blocken, så den
            // hamnar bland widgetarnas rader och inte efter adopt.
            for (std::size_t i = 0; i < m.stmts.size(); ++i) {
                const Stmt::Kind k = m.stmts[i].kind;
                if (k == Stmt::Kind::Event || k == Stmt::Kind::WindowEvent ||
                    k == Stmt::Kind::Adopt) {
                    pos = i;
                    break;
                }
            }
        }
        m.stmts.insert(m.stmts.begin() +
                           static_cast<std::ptrdiff_t>(pos),
                       std::move(s));
    }
    return true;
}

bool renameNode(WindowModel& m, const std::string& från,
                const std::string& till, std::string& fel)
{
    WidgetNode* n = m.find(från);
    if (!n) {
        fel = "'" + från + "' finns inte";
        return false;
    }
    if (från == till)
        return true;
    if (!validNodeName(till)) {
        fel = "'" + till + "' är inte ett giltigt C++-medlemsnamn "
              "(bokstav/_ först, sedan bokstäver/siffror/_, och inte ett "
              "nyckelord)";
        return false;
    }
    if (m.find(till)) {
        fel = "'" + till + "' är redan taget";
        return false;
    }
    if (builtinMember(till) || n->builtin) {
        fel = "inbyggda fönstermedlemmar (statusBar/menuBar) kan inte "
              "döpas om";
        return false;
    }

    std::string lit;
    if (!cppString(till, lit)) {
        fel = "'" + till + "' kan inte skrivas som C++-literal";
        return false;
    }

    // OPAKA ANKARRADER GÖR NAMNBYTET OSÄKERT — vägra i stället för att
    // lyckas till hälften. En ankarrad som inte går att
    // parsa bevaras opakt av regel 4, men den kan mycket väl peka på
    // `från`: skriver vi om noden och lämnar raden står den kvar och pekar
    // på ett namn som inte längre finns, och kanten faller TYST tillbaka
    // på x/y. Att rapportera framgång för en halv omdöpning är värre än
    // att säga nej — användaren kan rätta raden och försöka igen.
    // Kontrollen ligger FÖRE all mutation, så operationen är atomisk.
    for (const Stmt& s : m.stmts) {
        if (s.kind != Stmt::Kind::Prop || !isSiblingAnchorField(s.name))
            continue;
        std::string ankTill;
        float       gap = 0.0f;
        if (!parseSiblingAnchorText(s.value, ankTill, gap)) {
            fel = "'" + s.target + "." + s.name + "' är en OPAK ankarrad "
                  "som inte går att läsa, så den kan peka på '" + från +
                  "' utan att vi kan se det. Rätta raden för hand först — "
                  "ett halvt namnbyte hade lämnat ankaret pekande på ett "
                  "namn som inte längre finns.";
            return false;
        }
    }
    // GRANNEN är syskonankarets `to` i en egen
    // rad: ett namn som en STRÄNGLITERAL. En `buddy`-rad som inte är en
    // literal (en variabel, ett uttryck) kan peka på `från` utan att vi ser
    // det — samma vägran, och likaså FÖRE all mutation.
    for (const Stmt& s : m.stmts) {
        if (s.kind != Stmt::Kind::Prop || s.name != "buddy")
            continue;
        std::string granne;
        if (!parseCppString(s.value, granne)) {
            fel = "'" + s.target + ".buddy' är ingen strängliteral, så den "
                  "kan peka på '" + från + "' utan att vi kan se det. Rätta "
                  "raden för hand först.";
            return false;
        }
    }

    n->id = till;
    for (Stmt& s : m.stmts) {
        if (s.target == från)
            s.target = till;
        // ADOPTIONSFÖRÄLDERN bor i `name` på en Adopt-sats — inte i
        // `target`. Missas den hamnar barnen i roten vid nästa spara.
        if (s.kind == Stmt::Kind::Adopt && s.name == från)
            s.name = till;
        // SYSKONANKARNAS `to` är en STRÄNGLITERAL inuti en structliteral,
        // och den pekar på RUNTIME-namnet — som alltid ÄR
        // nodnamnet. Uppdateras den inte pekar ankaret på ett namn som
        // inte längre finns, och kanten faller TYST tillbaka på x/y.
        if (s.kind == Stmt::Kind::Prop && isSiblingAnchorField(s.name)) {
            std::string ankTill;
            float       gap = 0.0f;
            if (parseSiblingAnchorText(s.value, ankTill, gap) &&
                ankTill == från)
                s.value = siblingAnchorText(till, gap);
        }
        // …och GRANNENS namn — samma skäl: etikettens höjd hade tyst
        // fallit tillbaka på sin egen.
        if (s.kind == Stmt::Kind::Prop && s.name == "buddy") {
            std::string granne;
            if (parseCppString(s.value, granne) && granne == från)
                s.value = lit;
        }
        // …OCH DEN KANONISKA name-radens LITERALVÄRDE. Utan den här raden
        // producerar operationen `BtnStatus.name = "Button1";` — samma
        // tvetydighet som `nameState` fail-safe:ar på, införd av oss.
        if (s.kind == Stmt::Kind::Prop && s.target == till &&
            s.name == "name")
            s.value = lit;
        // …och ett FÖNSTERANROP med noden som argument (`setAcceptButton(
        // Button1);`) — medlemsnamnet står där som en
        // identifierare, och kvar hade raden inte kompilerat.
        if (windowCallRefersTo(s, från))
            s.value = till;
    }
    return true;
}

} // namespace bld
