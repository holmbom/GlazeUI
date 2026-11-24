// glaze-icons — GLYFRECEPT → ARK: läser recept i `GLAZE_REGISTER_ICON`-form ur
// en eller flera filer (en .cpp eller en ren receptfil), registrerar dem i
// körtid och skriver ikonarket som PNG — utan att något kompileras om.
//
//     glaze-icons <recept ...> [-o ark.png] [--only A,B] [--with Save,Open]
//                 [--zoom N] [--big N] [--list]
//     glaze-icons --version
//
// Ett recept står som i appens kod, `GLAZE_REGISTER_ICON("Namn", "kontur",
// "färg")` eller `GLAZE_REGISTER_ICON_PART(...)`, med strängliteraler och
// filens strängmakron (`#define KUB "M 12 3 …"`) som argument.
//
// Arket är RAMVERKETS (`detail::renderIconSheet`, samma som
// `glaze::writeIconSheet`): varje ikon i 16, 24, 32 och 48 med namnet under,
// mono till vänster och färg till höger, ljust överst och mörkt under, ritat
// i ett dolt fönster i skärmens skala. Bilden är alltså den appen visar.
//
//   --only A,B     bara de recepten (förval: alla i filerna, i filernas ordning)
//   --with S,T     standardikoner bredvid, som jämförelse (namnen i StockIcon)
//   --zoom N       arket förstorat N gånger utan utjämning (1–8, förval 1)
//   --big N        en remsa under arket: varje ikon i färg i N px (32–256),
//                  ljust till vänster och mörkt till höger
//   --list         skriver receptens namn och slutar (inget ark)
//   --version      skriver "glaze-icons <GlazeUI-versionen>" och slutar
//
// Stdout vid OK: "<fil>: <b>x<h> px, <n> ikoner". Ramverkets logg — också
// felen i ett recept, med ikonens namn och ordet där felet sitter — går till
// stderr. Slutkoder: 0 = arket skrivet · 1 = fel i indata (fil, recept, namn)
// · 2 = felaktig användning eller rit-/skrivfel.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gfx/image.h>
#include <glaze/glaze.h>

#include "icon_sheet.h"   // detail::renderIconSheet — ramverkets ark

namespace {

struct Recipe {
    std::string name, shapes, colors;
    bool        part = false;
};

int usage()
{
    std::fprintf(stderr,
                 "användning: glaze-icons <recept ...> [-o ark.png] [--only A,B] "
                 "[--with Save,Open] [--zoom N] [--big N] [--list] | --version\n");
    return 2;
}

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> out;
    std::string              word;
    std::istringstream       in(text);
    while (std::getline(in, word, ','))
        if (!word.empty())
            out.push_back(word);
    return out;
}

// Källtexten utan kommentarer (`//` och `/* */`); strängliteralerna står kvar
// orörda, så ett `//` i ett recept inte räknas som en kommentar.
std::string withoutComments(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '"') {
            out += s[i++];
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size())
                    out += s[i++];
                out += s[i++];
            }
            if (i < s.size())
                out += s[i++];
        } else if (s.compare(i, 2, "//") == 0) {
            while (i < s.size() && s[i] != '\n')
                ++i;
        } else if (s.compare(i, 2, "/*") == 0) {
            const std::size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
            out += ' ';
        } else {
            out += s[i++];
        }
    }
    return out;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Filens strängmakron: `#define NAMN "text" …`, där texten är strängliteraler
// och tidigare strängmakron efter varandra.
using Defines = std::map<std::string, std::string>;

// Ett argument: en eller flera strängliteraler eller strängmakron efter
// varandra (som C++ fogar ihop). Svarar false om det inte står någon på platsen.
bool readLiteral(const std::string& s, std::size_t& i, std::string& out, const Defines& defines)
{
    bool any = false;
    for (;;) {
        while (i < s.size() && isSpace(s[i]))
            ++i;
        if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
            std::size_t end = i;
            while (end < s.size() && isNameChar(s[end]))
                ++end;
            const auto found = defines.find(s.substr(i, end - i));
            if (found == defines.end())
                return false;   // ett namn som inte är ett strängmakro
            out += found->second;
            i   = end;
            any = true;
            continue;
        }
        if (i >= s.size() || s[i] != '"')
            return any;
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                out += s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i];
            } else {
                out += s[i];
            }
            ++i;
        }
        if (i >= s.size())
            return false;   // strängen stängs inte
        ++i;
        any = true;
    }
}

int lineOf(const std::string& s, std::size_t at)
{
    return 1 + static_cast<int>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
}

// Strängmakrona i texten, i filens ordning. En `#define` som inte bara består
// av strängliteraler och kända strängmakron hoppas över.
Defines readDefines(const std::string& s)
{
    Defines defines;
    for (std::size_t at = s.find("#define"); at != std::string::npos; at = s.find("#define", at + 1)) {
        std::size_t i = at + 7;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        std::size_t end = i;
        while (end < s.size() && isNameChar(s[end]))
            ++end;
        if (end == i || (end < s.size() && s[end] == '('))
            continue;   // inget namn, eller ett makro med parametrar
        const std::string name = s.substr(i, end - i);
        // Makrots text: raden ut, med fortsättningsrader (\ sist på raden).
        std::string body;
        for (i = end; i < s.size() && s[i] != '\n'; ++i) {
            if (s[i] == '\\' && (s.compare(i + 1, 1, "\n") == 0 || s.compare(i + 1, 2, "\r\n") == 0)) {
                i += s[i + 1] == '\r' ? 2 : 1;
                body += ' ';
            } else {
                body += s[i];
            }
        }
        std::size_t j = 0;
        std::string value;
        if (!readLiteral(body, j, value, defines))
            continue;
        while (j < body.size() && isSpace(body[j]))
            ++j;
        if (j == body.size())
            defines[name] = std::move(value);
    }
    return defines;
}

// Recepten i filen: varje `GLAZE_REGISTER_ICON(namn, kontur[, färg])` och
// `GLAZE_REGISTER_ICON_PART(...)`. Argumenten är strängliteraler och filens
// strängmakron. Makrots egen definition (`#define …`) och allt i kommentarer
// hoppas över. Svarar false med felet på stderr.
bool readRecipes(const std::string& path, std::vector<Recipe>& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "glaze-icons: kan inte läsa %s\n", path.c_str());
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string s       = withoutComments(buffer.str());
    const Defines     defines = readDefines(s);

    static constexpr std::string_view kMacro = "GLAZE_REGISTER_ICON";
    bool                              ok     = true;
    for (std::size_t at = s.find(kMacro); at != std::string::npos; at = s.find(kMacro, at + 1)) {
        // Ett längre namn före (t.ex. MY_GLAZE_REGISTER_ICON) är inte makrot.
        if (at > 0 && isNameChar(s[at - 1]))
            continue;
        const std::size_t lineStart = s.rfind('\n', at) == std::string::npos ? 0 : s.rfind('\n', at) + 1;
        if (s.find("#define", lineStart) < at)
            continue;
        std::size_t i = at + kMacro.size();
        Recipe      r;
        if (s.compare(i, 5, "_PART") == 0) {
            r.part = true;
            i += 5;
        }
        while (i < s.size() && isSpace(s[i]))
            ++i;
        if (i >= s.size() || s[i] != '(')
            continue;
        ++i;
        const int line = lineOf(s, at);
        if (!readLiteral(s, i, r.name, defines)) {
            std::fprintf(stderr, "%s:%d: namnet ska vara en strängliteral\n", path.c_str(), line);
            ok = false;
            continue;
        }
        while (i < s.size() && isSpace(s[i]))
            ++i;
        if (i >= s.size() || s[i] != ',') {
            std::fprintf(stderr, "%s:%d: %s saknar konturen\n", path.c_str(), line, r.name.c_str());
            ok = false;
            continue;
        }
        ++i;
        if (!readLiteral(s, i, r.shapes, defines)) {
            std::fprintf(stderr, "%s:%d: %s: konturen ska vara en strängliteral\n", path.c_str(), line, r.name.c_str());
            ok = false;
            continue;
        }
        while (i < s.size() && isSpace(s[i]))
            ++i;
        if (i < s.size() && s[i] == ',') {
            ++i;
            if (!readLiteral(s, i, r.colors, defines)) {
                std::fprintf(stderr, "%s:%d: %s: färgen ska vara en strängliteral\n", path.c_str(), line, r.name.c_str());
                ok = false;
                continue;
            }
            while (i < s.size() && isSpace(s[i]))
                ++i;
        }
        if (i >= s.size() || s[i] != ')') {
            std::fprintf(stderr, "%s:%d: %s: ) saknas efter receptet\n", path.c_str(), line, r.name.c_str());
            ok = false;
            continue;
        }
        out.push_back(std::move(r));
    }
    return ok;
}

struct Image {
    std::vector<std::uint8_t> px;   // RGBA8, rak alfa, täta rader
    int                       w = 0, h = 0;
};

// Bilden förstorad `n` gånger, varje pixel som en ruta på n × n.
Image zoomed(const Image& in, int n)
{
    if (n <= 1)
        return in;
    Image out;
    out.w = in.w * n;
    out.h = in.h * n;
    out.px.resize(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h) * 4);
    for (int y = 0; y < out.h; ++y) {
        const std::uint8_t* from = &in.px[static_cast<std::size_t>(y / n) * static_cast<std::size_t>(in.w) * 4];
        std::uint8_t*       to   = &out.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.w) * 4];
        for (int x = 0; x < out.w; ++x)
            std::copy_n(from + static_cast<std::size_t>(x / n) * 4, 4, to + static_cast<std::size_t>(x) * 4);
    }
    return out;
}

// `b` under `a`, vänsterställda; ytan utanför den smalare är genomskinlig.
Image stacked(const Image& a, const Image& b)
{
    Image out;
    out.w = std::max(a.w, b.w);
    out.h = a.h + b.h;
    out.px.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h) * 4, 0);
    const auto put = [&](const Image& src, int y0) {
        for (int y = 0; y < src.h; ++y)
            std::copy_n(&src.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(src.w) * 4],
                        static_cast<std::size_t>(src.w) * 4,
                        &out.px[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(out.w) * 4]);
    };
    put(a, 0);
    put(b, a.h);
    return out;
}

// ------------------------------------------------------------ stora remsan

constexpr float kBigGap   = 16.0f;
constexpr float kBigLabel = 20.0f;

// Ikonerna i färg i en rad, var och en `side` px, med namnet under.
struct BigRow : glaze::Widget {
    std::vector<std::string> names;
    std::vector<bool>        app;      // appens recept, annars standardikon
    float                    side = 96.0f;

protected:
    void draw(const glaze::Ctx& c) override
    {
        float x = kBigGap;
        for (std::size_t i = 0; i < names.size(); ++i) {
            // Den största storleksklassen: motorn ritar vektorn i rutans
            // fysiska storlek, så bilden är skarp också i `side` px.
            const glaze::ImageSource image =
                app[i] ? glaze::drawnIcon(names[i], 48, glaze::IconStyle::Color)
                       : glaze::stockIcon(*glaze::stockIconByName(names[i]), 48, glaze::IconStyle::Color);
            glaze::Draw::glyph(c, {x, kBigGap, side, side}, image);
            glaze::Draw::text(c, {x, kBigGap + side + 4.0f, side + kBigGap, kBigLabel}, names[i], c.theme->colors.text);
            x += side + kBigGap;
        }
    }
};

class BigWindow : public glaze::AppWindow {
public:
    BigWindow(int w, int h) : glaze::AppWindow("glaze-icons", w, h, gfx::Chrome::None)
    {
        row.name = "row";
        row.dock = glaze::Dock::Fill;
        root().adopt(row);
        setOffscreen(true);
    }

    BigRow row;
};

// Remsan: ljust till vänster och mörkt till höger. Svarar false vid ritfel.
bool renderBig(const std::vector<std::string>& names, const std::vector<bool>& app, int side, Image& out)
{
    const float       s = static_cast<float>(side);
    const int         w = static_cast<int>(kBigGap + static_cast<float>(names.size()) * (s + kBigGap));
    const int         h = static_cast<int>(kBigGap + s + 4.0f + kBigLabel + kBigGap);
    const std::size_t previous = glaze::app().canvasUploadBudget();
    glaze::app().setCanvasUploadBudget(std::size_t{1} << 30);
    BigWindow win(w, h);
    win.row.names = names;
    win.row.app   = app;
    win.row.side  = s;
    win.show();
    bool  ok = win.isOpen();
    Image halves[2];
    for (int i = 0; ok && i < 2; ++i) {
        win.setThemeMode(i == 0 ? glaze::ThemeMode::Light : glaze::ThemeMode::Dark);
        win.row.invalidate();
        win.window().renderFrame();
        ok = win.window().captureFrame(halves[i].px, halves[i].w, halves[i].h) && halves[i].w == halves[0].w &&
             halves[i].h == halves[0].h;
    }
    glaze::app().setCanvasUploadBudget(previous);
    if (!ok)
        return false;
    out.w = halves[0].w * 2;
    out.h = halves[0].h;
    out.px.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h) * 4, 0);
    for (int i = 0; i < 2; ++i)
        for (int y = 0; y < out.h; ++y)
            std::copy_n(&halves[i].px[static_cast<std::size_t>(y) * static_cast<std::size_t>(halves[i].w) * 4],
                        static_cast<std::size_t>(halves[i].w) * 4,
                        &out.px[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.w) +
                                 static_cast<std::size_t>(i) * static_cast<std::size_t>(halves[i].w)) * 4]);
    return true;
}

// Ett heltal i [lo, hi]; hela argumentet ska vara talet.
bool readInt(const char* text, int lo, int hi, int& out)
{
    char*      end   = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < lo || value > hi)
        return false;
    out = static_cast<int>(value);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> files, only, with;
    std::string              outPath = "glyfark.png";
    int                      zoom = 1, big = 0;
    bool                     list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto        value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "-o") {
            const char* v = value();
            if (!v)
                return usage();
            outPath = v;
        } else if (arg == "--only" || arg == "--with") {
            const char* v = value();
            if (!v)
                return usage();
            (arg == "--only" ? only : with) = split(v);
        } else if (arg == "--zoom") {
            const char* v = value();
            if (!v || !readInt(v, 1, 8, zoom))
                return usage();
        } else if (arg == "--big") {
            const char* v = value();
            if (!v || !readInt(v, 32, 256, big))
                return usage();
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--version") {
            std::printf("glaze-icons %s\n", GLAZE_VERSION_STRING);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            return usage();
        } else {
            files.push_back(arg);
        }
    }
    if (files.empty())
        return usage();

    std::vector<Recipe> recipes;
    bool                readOk = true;
    for (const std::string& f : files)
        readOk = readRecipes(f, recipes) && readOk;
    if (!readOk)
        return 1;
    if (recipes.empty()) {
        std::fprintf(stderr, "glaze-icons: inga GLAZE_REGISTER_ICON i filerna\n");
        return 1;
    }

    // Delarna först: en ikon får använda en del som står längre ned i filen.
    std::vector<std::string> icons;
    for (const Recipe& r : recipes)
        if (r.part && !glaze::registerIconPart(r.name, r.shapes, r.colors))
            readOk = false;
    for (const Recipe& r : recipes) {
        if (r.part)
            continue;
        if (!glaze::registerIcon(r.name, r.shapes, r.colors))
            readOk = false;
        else if (std::find(icons.begin(), icons.end(), r.name) == icons.end())
            icons.push_back(r.name);
    }
    if (!readOk)
        return 1;

    if (list) {
        for (const Recipe& r : recipes)
            std::printf("%s%s\n", r.part ? "@" : "", r.name.c_str());
        return 0;
    }

    std::vector<std::string> names;
    std::vector<bool>        app;
    for (const std::string& n : only.empty() ? icons : only) {
        if (std::find(icons.begin(), icons.end(), n) == icons.end()) {
            std::fprintf(stderr, "glaze-icons: --only %s finns inte i recepten\n", n.c_str());
            return 1;
        }
        names.push_back(n);
        app.push_back(true);
    }
    for (const std::string& n : with) {
        if (!glaze::stockIconByName(n)) {
            std::fprintf(stderr, "glaze-icons: --with %s är ingen standardikon\n", n.c_str());
            return 1;
        }
        if (std::find(names.begin(), names.end(), n) != names.end())
            continue;   // ett recept med standardikonens namn går före
        names.push_back(n);
        app.push_back(false);
    }
    if (names.empty()) {
        std::fprintf(stderr, "glaze-icons: filerna har bara delar, inga ikoner\n");
        return 1;
    }

    glaze::detail::IconSheetImage sheet;
    if (!glaze::detail::renderIconSheet(names, sheet))
        return 2;
    Image image = zoomed({std::move(sheet.px), sheet.w, sheet.h}, zoom);
    if (big > 0) {
        Image strip;
        if (!renderBig(names, app, big, strip)) {
            std::fprintf(stderr, "glaze-icons: den stora remsan kunde inte ritas\n");
            return 2;
        }
        image = stacked(image, strip);
    }
    if (!gfx::encodePng(outPath, image.px, image.w, image.h)) {
        std::fprintf(stderr, "glaze-icons: kunde inte skriva %s\n", outPath.c_str());
        return 2;
    }
    std::printf("%s: %dx%d px, %d ikoner\n", outPath.c_str(), image.w, image.h, static_cast<int>(names.size()));
    return 0;
}
