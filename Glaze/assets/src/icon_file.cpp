// Programfilens ikonfiler — .ico och .icns. REN STD.
//
// .ico (little-endian): ICONDIR (6 byte) + en ICONDIRENTRY per ram (16 byte)
// + ramarnas data i samma ordning. Bredd/höjd 0 i posten betyder 256.
// Ramar under 256 px är en BMP UTAN filhuvud: BITMAPINFOHEADER med dubblad
// höjd (XOR-bilden + AND-masken), 32 bitar BGRA nerifrån och upp, och masken
// som 1 bit per pixel, nerifrån och upp, rader utfyllda till 32 bitar. 256 px
// är en hel PNG — det är vad Windows själv skriver för den storleken.
//
// .icns (big-endian): 'icns' + total längd, sedan element 'typ' + längd
// (inklusive de 8 byten) + data. Elementet 'TOC ' först listar de övriga
// (typ + längd). is32/il32 är RGB med Apples PackBits-liknande RLE per kanal
// (R-planet, G-planet, B-planet var för sig), s8mk/l8mk är alfan okomprimerad;
// de större typerna är hela PNG:er.

#include "glaze_assets/icon_file.h"

#include <algorithm>
#include <cstddef>

#include "glaze_assets/png.h"

namespace glzassets {
namespace {

void wrLE16(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(std::uint8_t(x & 0xFF));
    v.push_back(std::uint8_t((x >> 8) & 0xFF));
}

void wrLE32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    wrLE16(v, x & 0xFFFF);
    wrLE16(v, x >> 16);
}

void wrBE32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(std::uint8_t(x >> 24));
    v.push_back(std::uint8_t((x >> 16) & 0xFF));
    v.push_back(std::uint8_t((x >> 8) & 0xFF));
    v.push_back(std::uint8_t(x & 0xFF));
}

void putBE32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x)
{
    v[at]     = std::uint8_t(x >> 24);
    v[at + 1] = std::uint8_t((x >> 16) & 0xFF);
    v[at + 2] = std::uint8_t((x >> 8) & 0xFF);
    v[at + 3] = std::uint8_t(x & 0xFF);
}

bool validFrame(const Rgba& f)
{
    return !f.empty() && f.width == f.height &&
           f.px.size() >= std::size_t(f.width) * std::size_t(f.height) * 4;
}

// Ramarna i stigande sidordning — samma indata i annan ordning ger samma fil.
std::vector<const Rgba*> sortedFrames(const std::vector<Rgba>& frames)
{
    std::vector<const Rgba*> v;
    for (const Rgba& f : frames)
        v.push_back(&f);
    std::stable_sort(v.begin(), v.end(), [](const Rgba* a, const Rgba* b) {
        return a->width < b->width;
    });
    return v;
}

bool checkFrames(const std::vector<const Rgba*>& v, int maxSide,
                 std::string& err)
{
    if (v.empty()) {
        err = "inga ramar att skriva";
        return false;
    }
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!validFrame(*v[i])) {
            err = "en ram är tom eller inte kvadratisk";
            return false;
        }
        if (v[i]->width > maxSide) {
            err = "ramen " + std::to_string(v[i]->width) + " px är större än " +
                  std::to_string(maxSide) + " px";
            return false;
        }
        if (i > 0 && v[i]->width == v[i - 1]->width) {
            err = "två ramar med sidan " + std::to_string(v[i]->width) + " px";
            return false;
        }
    }
    return true;
}

// En ram under 256 px som BMP utan filhuvud (se filhuvudet).
std::vector<std::uint8_t> icoBmp(const Rgba& f)
{
    const auto w = std::uint32_t(f.width);
    const auto h = std::uint32_t(f.height);
    const std::uint32_t maskStride = ((w + 31) / 32) * 4;
    std::vector<std::uint8_t> b;
    b.reserve(40 + w * h * 4 + maskStride * h);
    wrLE32(b, 40);          // biSize
    wrLE32(b, w);           // biWidth
    wrLE32(b, h * 2);       // biHeight — XOR + AND
    wrLE16(b, 1);           // biPlanes
    wrLE16(b, 32);          // biBitCount
    wrLE32(b, 0);           // biCompression = BI_RGB
    wrLE32(b, w * h * 4 + maskStride * h);   // biSizeImage
    wrLE32(b, 0);           // biXPelsPerMeter
    wrLE32(b, 0);           // biYPelsPerMeter
    wrLE32(b, 0);           // biClrUsed
    wrLE32(b, 0);           // biClrImportant
    for (std::uint32_t y = h; y-- > 0;) {
        const std::uint8_t* row = f.px.data() + std::size_t(y) * w * 4;
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t* p = row + std::size_t(x) * 4;
            b.push_back(p[2]);
            b.push_back(p[1]);
            b.push_back(p[0]);
            b.push_back(p[3]);
        }
    }
    // AND-masken: satt bit = genomskinlig. Den läses bara där alfan inte
    // gör det (äldre ritvägar); en helt genomskinlig pixel är den enda som
    // säkert ska vara hål.
    for (std::uint32_t y = h; y-- > 0;) {
        const std::uint8_t* row = f.px.data() + std::size_t(y) * w * 4;
        std::vector<std::uint8_t> m(maskStride, 0);
        for (std::uint32_t x = 0; x < w; ++x)
            if (row[std::size_t(x) * 4 + 3] == 0)
                m[x / 8] |= std::uint8_t(0x80u >> (x % 8));
        b.insert(b.end(), m.begin(), m.end());
    }
    return b;
}

// Apples RLE för ett kanalplan: 0x00–0x7F = (n+1) bokstavliga byte följer,
// 0x80–0xFF = nästa byte upprepad (n−125) gånger (3–130). Girig: en körning
// på minst 3 blir en upprepning, allt annat bokstavligt.
void icnsRle(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out)
{
    std::size_t i = 0;
    const std::size_t n = in.size();
    while (i < n) {
        std::size_t run = 1;
        while (i + run < n && run < 130 && in[i + run] == in[i])
            ++run;
        if (run >= 3) {
            out.push_back(std::uint8_t(run + 125));
            out.push_back(in[i]);
            i += run;
            continue;
        }
        // Bokstavligt fram till nästa körning på minst 3 (eller 128 byte).
        std::size_t lit = 0;
        while (i + lit < n && lit < 128) {
            if (i + lit + 2 < n && in[i + lit] == in[i + lit + 1] &&
                in[i + lit] == in[i + lit + 2])
                break;
            ++lit;
        }
        out.push_back(std::uint8_t(lit - 1));
        out.insert(out.end(), in.begin() + std::ptrdiff_t(i),
                   in.begin() + std::ptrdiff_t(i + lit));
        i += lit;
    }
}

std::vector<std::uint8_t> icnsRgb(const Rgba& f)
{
    std::vector<std::uint8_t> out;
    const std::size_t n = std::size_t(f.width) * std::size_t(f.height);
    for (int c = 0; c < 3; ++c) {
        std::vector<std::uint8_t> plane(n);
        for (std::size_t i = 0; i < n; ++i)
            plane[i] = f.px[i * 4 + std::size_t(c)];
        icnsRle(plane, out);
    }
    return out;
}

std::vector<std::uint8_t> icnsMask(const Rgba& f)
{
    const std::size_t n = std::size_t(f.width) * std::size_t(f.height);
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = f.px[i * 4 + 3];
    return out;
}

struct Element {
    char                      type[4];
    std::vector<std::uint8_t> data;
};

void addElement(std::vector<Element>& els, const char* type,
                std::vector<std::uint8_t> data)
{
    Element e;
    std::copy_n(type, 4, e.type);
    e.data = std::move(data);
    els.push_back(std::move(e));
}

} // namespace

bool writeIco(const std::vector<Rgba>& frames, std::vector<std::uint8_t>& out,
              std::string& err)
{
    out.clear();
    const std::vector<const Rgba*> v = sortedFrames(frames);
    if (!checkFrames(v, 256, err))
        return false;

    std::vector<std::vector<std::uint8_t>> data;
    for (const Rgba* f : v)
        data.push_back(f->width == 256 ? encodePng(*f) : icoBmp(*f));

    wrLE16(out, 0);                          // reserverat
    wrLE16(out, 1);                          // typ 1 = ikon
    wrLE16(out, std::uint32_t(v.size()));    // antal
    std::uint32_t offset = 6 + 16 * std::uint32_t(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        const int s = v[i]->width;
        out.push_back(std::uint8_t(s >= 256 ? 0 : s));   // bredd (0 = 256)
        out.push_back(std::uint8_t(s >= 256 ? 0 : s));   // höjd
        out.push_back(0);                                // färger (ingen palett)
        out.push_back(0);                                // reserverat
        wrLE16(out, 1);                                  // plan
        wrLE16(out, 32);                                 // bitar per pixel
        wrLE32(out, std::uint32_t(data[i].size()));
        wrLE32(out, offset);
        offset += std::uint32_t(data[i].size());
    }
    for (const auto& d : data)
        out.insert(out.end(), d.begin(), d.end());
    return true;
}

bool writeIcns(const std::vector<Rgba>& frames, std::vector<std::uint8_t>& out,
               std::string& err)
{
    out.clear();
    const std::vector<const Rgba*> v = sortedFrames(frames);
    if (!checkFrames(v, 1024, err))
        return false;

    std::vector<Element> els;
    for (const Rgba* f : v) {
        switch (f->width) {
        case 16:
            addElement(els, "is32", icnsRgb(*f));
            addElement(els, "s8mk", icnsMask(*f));
            break;
        case 32:
            addElement(els, "il32", icnsRgb(*f));
            addElement(els, "l8mk", icnsMask(*f));
            addElement(els, "ic11", encodePng(*f));
            break;
        case 64:
            addElement(els, "ic12", encodePng(*f));
            break;
        case 128:
            addElement(els, "ic07", encodePng(*f));
            break;
        case 256: {
            std::vector<std::uint8_t> png = encodePng(*f);
            addElement(els, "ic13", png);
            addElement(els, "ic08", std::move(png));
            break;
        }
        case 512: {
            std::vector<std::uint8_t> png = encodePng(*f);
            addElement(els, "ic14", png);
            addElement(els, "ic09", std::move(png));
            break;
        }
        case 1024:
            addElement(els, "ic10", encodePng(*f));
            break;
        default:
            err = "icns har ingen typ för sidan " + std::to_string(f->width) +
                  " px (16, 32, 64, 128, 256, 512 eller 1024)";
            return false;
        }
    }

    out.insert(out.end(), {'i', 'c', 'n', 's'});
    wrBE32(out, 0);   // total längd, fylls i sist
    out.insert(out.end(), {'T', 'O', 'C', ' '});
    wrBE32(out, 8 + 8 * std::uint32_t(els.size()));
    for (const Element& e : els) {
        out.insert(out.end(), e.type, e.type + 4);
        wrBE32(out, 8 + std::uint32_t(e.data.size()));
    }
    for (const Element& e : els) {
        out.insert(out.end(), e.type, e.type + 4);
        wrBE32(out, 8 + std::uint32_t(e.data.size()));
        out.insert(out.end(), e.data.begin(), e.data.end());
    }
    putBE32(out, 4, std::uint32_t(out.size()));
    return true;
}

} // namespace glzassets
