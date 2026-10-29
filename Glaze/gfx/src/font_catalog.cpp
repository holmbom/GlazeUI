// Systemfont-katalogen — backend-neutral. Familjer i fallande prioritet per
// FontFamily och plattform, med filen för varje stil; alla med full
// åäö-täckning. Backends provar listan i ordning tills en fil laddas
// (saknad fil är inget fel).

#include "font_catalog.h"

namespace gfx {

namespace {

#if defined(_WIN32)

// Windows fyra-filsfamiljer (r/b/i/z) — Tahoma och Lucida Console saknar
// kursiv, och Lucida Console fet: där syntetiserar ritytan.
constexpr FontFiles kUi[] = {
    {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf",
     "C:/Windows/Fonts/segoeuii.ttf", "C:/Windows/Fonts/segoeuiz.ttf"},
    {"C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/arialbd.ttf",
     "C:/Windows/Fonts/ariali.ttf", "C:/Windows/Fonts/arialbi.ttf"},
    {"C:/Windows/Fonts/tahoma.ttf", "C:/Windows/Fonts/tahomabd.ttf", nullptr,
     nullptr},
};
constexpr FontFiles kMono[] = {
    {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/consolab.ttf",
     "C:/Windows/Fonts/consolai.ttf", "C:/Windows/Fonts/consolaz.ttf"},
    {"C:/Windows/Fonts/cour.ttf", "C:/Windows/Fonts/courbd.ttf",
     "C:/Windows/Fonts/couri.ttf", "C:/Windows/Fonts/courbi.ttf"},
    {"C:/Windows/Fonts/lucon.ttf", nullptr, nullptr, nullptr},
};

#elif defined(__APPLE__)

// Samlingsfilerna (.ttc) bär stilarna själva — Core Text-backenden väljer
// stilen ur samlingen (CTFontCreateCopyWithSymbolicTraits), så filen är
// densamma för alla fyra. SFNS är en variabel font: samma sak.
constexpr FontFiles kUi[] = {
    {"/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Helvetica.ttc",
     "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Helvetica.ttc"},
    {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/SFNS.ttf",
     "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/SFNS.ttf"},
    {"/Library/Fonts/Arial Unicode.ttf", nullptr, nullptr, nullptr},
};
constexpr FontFiles kMono[] = {
    {"/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Menlo.ttc",
     "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Menlo.ttc"},
    {"/System/Library/Fonts/Monaco.ttf", nullptr, nullptr, nullptr},
    {"/System/Library/Fonts/Supplemental/Courier New.ttf",
     "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
     "/System/Library/Fonts/Supplemental/Courier New Italic.ttf",
     "/System/Library/Fonts/Supplemental/Courier New Bold Italic.ttf"},
};

#else

// Linux: fontconfig först (backenden frågar "sans-serif:bold" osv.) — det
// här är reserven när fontconfig inte svarar. DejaVus kursiv heter Oblique,
// och Noto Sans Mono saknar kursiv.
constexpr FontFiles kUi[] = {
    {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"},
    {"/usr/share/fonts/TTF/DejaVuSans.ttf",
     "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
     "/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf",
     "/usr/share/fonts/TTF/DejaVuSans-BoldOblique.ttf"},
    {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
     "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
     "/usr/share/fonts/truetype/noto/NotoSans-Italic.ttf",
     "/usr/share/fonts/truetype/noto/NotoSans-BoldItalic.ttf"},
    {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
     "/usr/share/fonts/noto/NotoSans-Bold.ttf",
     "/usr/share/fonts/noto/NotoSans-Italic.ttf",
     "/usr/share/fonts/noto/NotoSans-BoldItalic.ttf"},
};
constexpr FontFiles kMono[] = {
    {"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf",
     "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-BoldOblique.ttf"},
    {"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
     "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
     "/usr/share/fonts/TTF/DejaVuSansMono-Oblique.ttf",
     "/usr/share/fonts/TTF/DejaVuSansMono-BoldOblique.ttf"},
    {"/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
     "/usr/share/fonts/truetype/noto/NotoSansMono-Bold.ttf", nullptr,
     nullptr},
    {"/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
     "/usr/share/fonts/noto/NotoSansMono-Bold.ttf", nullptr, nullptr},
    {"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
     "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
     "/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf",
     "/usr/share/fonts/truetype/liberation/LiberationMono-BoldItalic.ttf"},
};

#endif

constexpr FontStyle kNearestBoldItalic[] = {
    FontStyle::BoldItalic, FontStyle::Bold, FontStyle::Italic,
    FontStyle::Regular};
constexpr FontStyle kNearestBold[]    = {FontStyle::Bold, FontStyle::Regular};
constexpr FontStyle kNearestItalic[]  = {FontStyle::Italic, FontStyle::Regular};
constexpr FontStyle kNearestRegular[] = {FontStyle::Regular};

} // namespace

std::span<const FontFiles> systemFontCandidates(FontFamily family)
{
    switch (family) {
    case FontFamily::Mono: return kMono;
    case FontFamily::UI:   break;
    }
    return kUi;
}

const char* fontFile(const FontFiles& f, FontStyle s)
{
    switch (s) {
    case FontStyle::Bold:       return f.bold;
    case FontStyle::Italic:     return f.italic;
    case FontStyle::BoldItalic: return f.boldItalic;
    case FontStyle::Regular:    break;
    }
    return f.regular;
}

std::span<const FontStyle> nearestStyles(FontStyle wanted)
{
    switch (wanted) {
    case FontStyle::BoldItalic: return kNearestBoldItalic;
    case FontStyle::Bold:       return kNearestBold;
    case FontStyle::Italic:     return kNearestItalic;
    case FontStyle::Regular:    break;
    }
    return kNearestRegular;
}

} // namespace gfx
