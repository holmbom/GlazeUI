// Draw — ritandet. Inga konstanter, inga mått, inga färger: allt sådant
// kommer in som argument (Style respektive Theme äger dem). Det enda den
// här filen vet på egen hand är hur logiska px blir fysiska och var
// widgeten sitter — se glaze/draw.h.

#include "glaze/draw.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <gfx/text.h>      // gfx::wrapText — radbrytningens råvara

#include "font_set.h"     // FontSet — Draw::withFont
#include "glaze_assets/image.h"   // pickIconSource — Draw::pickIcon
#include "image_cache.h"   // ImageSource -> textur (c.images)

namespace glaze {
namespace {

// Widget-relativa LOGISKA px -> absoluta FYSISKA px. Enda stället
// omräkningen sker.
gfx::Rectf phys(const Ctx& c, const gfx::Rectf& r)
{
    return gfx::Rectf{c.box.x + r.x * c.scale, c.box.y + r.y * c.scale,
                      r.w * c.scale, r.h * c.scale};
}

// Bilden i `Content`, med dess OPACITET (inaktivt läge — se
// `Content::imgOpacity`). Ingenting ritas ovanpå: tonningen är bildens
// egen alfa, så den stämmer mot vilken yta som helst.
void contentGlyph(const Ctx& c, const gfx::Rectf& r, const Content& ct)
{
    Draw::glyph(c, r, ct.img, ct.imgOpacity);
}

// ELLIPSEN — U+2026. Typografi och inget mått, av samma skäl som bockens
// proportioner bor i `check`. Delad av kortningen och textblocket.
const std::string& ellips()
{
    static const std::string e = "\xE2\x80\xA6";
    return e;
}

// Ett STYCKES rader (inget `\n` i `p`) som byte-intervall i stycket —
// brutna där `wrapLines` bryter, med EN skillnad: en rad som `wrapText`
// HÅRDBRÖT mitt i ett ord förlängs till ordets slut, så att ordet står helt
// på sin rad (blocket kortar det). En hårdbrytning känns igen på
// att raden slutar varken sist eller vid ett blanksteg — en brytning på
// ordgräns slutar ALLTID vid blanksteget den åt. Resten bryts om från
// ordets slut, så raderna efter det långa ordet packas som vanligt.
std::vector<gfx::WrappedLine> ordrader(const Ctx& c, const std::string& p,
                                       float maxW)
{
    std::vector<gfx::WrappedLine> ut;
    std::size_t                   pos = 0;
    bool                          klar = false;
    while (!klar) {
        const std::string rest  = p.substr(pos);
        const auto        rader = Draw::wrapLines(c, rest, maxW);
        klar = true;
        for (std::size_t i = 0; i < rader.size(); ++i) {
            const gfx::WrappedLine& l = rader[i];
            if (i + 1 == rader.size() || rest[l.end] == ' ') {
                ut.push_back(gfx::WrappedLine{pos + l.begin, pos + l.end});
                continue;
            }
            std::size_t slut = rest.find(' ', l.end);
            if (slut == std::string::npos)
                slut = rest.size();
            ut.push_back(gfx::WrappedLine{pos + l.begin, pos + slut});
            pos += slut;
            while (pos < p.size() && p[pos] == ' ')
                ++pos;
            klar = pos >= p.size();
            break;
        }
    }
    return ut;
}

} // namespace

// ------------------------------------------------------------------ ytor

void Draw::rect(const Ctx& c, const gfx::Rectf& r, float radius,
                gfx::Color fill)
{
    c.r->fillRoundedRect(phys(c, r), radius * c.scale, fill);
}

void Draw::rect(const Ctx& c, const gfx::Rectf& r, float radius,
                gfx::Color fill, const Pen& pen)
{
    Draw::rect(c, r, radius, fill);
    Draw::frame(c, r, radius, pen);
}

// RUTMÖNSTRET — klippet med radien gör hörnen exakt `rect`s, och
// rutorna längs högerkant och nederkant kapas av samma klipp, så måttet är
// alltid `r` och aldrig ett helt antal rutor.
void Draw::checker(const Ctx& c, const gfx::Rectf& r, float radius,
                   float cell, gfx::Color a, gfx::Color b)
{
    if (r.w <= 0.0f || r.h <= 0.0f)
        return;
    Draw::rect(c, r, radius, a);
    if (cell <= 0.0f)
        return;
    Draw::clip(c, r, radius);
    int rad = 0;
    for (float y = r.y; y < r.y + r.h; y += cell, ++rad)
        for (float x = r.x + ((rad % 2) ? 0.0f : cell); x < r.x + r.w;
             x += 2.0f * cell)
            Draw::rect(c, {x, y, cell, cell}, 0.0f, b);
    Draw::endClip(c);
}

void Draw::frame(const Ctx& c, const gfx::Rectf& r, float radius,
                 const Pen& pen)
{
    const gfx::Rectf pr  = phys(c, r);
    const float      rad = radius * c.scale;
    // width 0 = hårlinje: fasadens default är 1 FYSISK px och ska INTE
    // skalas (se Pen i draw.h).
    if (pen.width <= 0.0f)
        c.r->strokeRoundedRect(pr, rad, pen.color);
    else
        c.r->strokeRoundedRect(pr, rad, pen.color, pen.width * c.scale);
}

// Samma regel som `frame` ovan: hårlinjen är fasadens 1 FYSISKA px.
float Draw::penWidth(const Ctx& c, const Pen& pen)
{
    return pen.width <= 0.0f ? 1.0f / c.scale : pen.width;
}

// Konturen med en lucka i överkanten. Draw har bara REKTANGULÄRA klipp —
// aldrig ett hål — så hela konturen ritas tre gånger, varje gång klippt
// till ett område som INTE innehåller luckan: till vänster om den, till
// höger om den, och rakt under den från rektens mitt och nedåt (där finns
// bara nederkanten). Områdena sträcker sig en rekt-storlek utanför `r` åt
// alla håll, så kantutjämningen aldrig klipps — måttet är rektens eget och
// inte ett tal.
//
// LUCKANS KANTER SNÄPPS TILL HELA FYSISKA PIXLAR. Backendens scissor rundar
// UTÅT (floor/ceil), så två klipp som möts mitt i en pixel tar BÅDA med den
// kolumnen — och nederkantens hårlinje ritades då två gånger där, en ljusare
// prick under luckans kant. På en hel pixel är
// floor och ceil samma tal, och klippen delar kanten exakt.
//
// LUCKAN KAPAS MOT ÖVERKANTENS RAKA DEL. En lucka som
// nådde ut i ett hörn — flikkontrollens valda flik längst till höger — tog
// sidlinjen med sig: klippet "höger om" började då utanför linjen, och
// "under luckan" ritar bara från mitten och nedåt, så sidlinjens övre halva
// saknades. Kanterna hålls därför innanför sidlinjen och förbi hörnbågen
// (radien kapad som backenden kapar den), och kapningen avrundar INÅT —
// också en kantutjämnad sidlinje på en bråkdelsposition står då hel.
void Draw::frameGap(const Ctx& c, const gfx::Rectf& r, float radius,
                    const Pen& pen, float gapX, float gapW)
{
    const float vänster = c.box.x + r.x * c.scale;
    const float höger   = c.box.x + (r.x + r.w) * c.scale;
    const float rak =
        std::max(std::min(radius, std::min(r.w, r.h) / 2.0f) * c.scale,
                 Draw::penWidth(c, pen) * c.scale);
    const float g0 = std::max(std::round(c.box.x + gapX * c.scale),
                              std::ceil(vänster + rak));
    const float g1 = std::min(std::round(c.box.x + (gapX + gapW) * c.scale),
                              std::floor(höger - rak));
    if (gapW <= 0.0f || g1 <= g0) {
        Draw::frame(c, r, radius, pen);
        return;
    }
    gapX = (g0 - c.box.x) / c.scale;
    gapW = (g1 - g0) / c.scale;

    const float      l    = r.x - r.w;
    const float      t    = r.y - r.h;
    const float      rgt  = r.x + 2.0f * r.w;
    const float      bot  = r.y + 2.0f * r.h;
    const float      mitt = r.y + r.h / 2.0f;
    const gfx::Rectf delar[] = {
        {l, t, gapX - l, bot - t},                        // vänster om
        {gapX + gapW, t, rgt - (gapX + gapW), bot - t},   // höger om
        {gapX, mitt, gapW, bot - mitt},                   // under luckan
    };
    for (const gfx::Rectf& d : delar) {
        if (d.w <= 0.0f || d.h <= 0.0f)
            continue;
        Draw::clip(c, d);
        Draw::frame(c, r, radius, pen);
        Draw::endClip(c);
    }
}

void Draw::shadow(const Ctx& c, const gfx::Rectf& r, float radius, float dx,
                  float dy, float blur, gfx::Color col)
{
    c.r->dropShadow(phys(c, r), radius * c.scale, dx * c.scale, dy * c.scale,
                    blur * c.scale, col);
}

// Lamineringen: fyll HELA formen två gånger, men klippt till övre andelen
// var gång. Att fylla hela formen (och inte bara det klippta bandet) är det
// som gör att hörnbågarna följer med — ett band ritat som egen rekt hade
// fått raka hörn uppe och en synlig söm nere.
//
// Klippen nästlas och stängs i omvänd ordning (klippstacken kan aldrig växa,
// så det ÖVRE fältet måste ligga innanför det nedre — det gör det: upper
// < lower i varje stil som lamineras).
void Draw::laminate(const Ctx& c, const gfx::Rectf& r, float radius,
                    gfx::Color lower, float lowerFrac, gfx::Color upper,
                    float upperFrac)
{
    Draw::clip(c, gfx::Rectf{r.x, r.y, r.w, r.h * lowerFrac});
    Draw::rect(c, r, radius, lower);
    Draw::clip(c, gfx::Rectf{r.x, r.y, r.w, r.h * upperFrac});
    Draw::rect(c, r, radius, upper);
    Draw::endClip(c);
    Draw::endClip(c);
}

void Draw::circle(const Ctx& c, const gfx::Rectf& box, gfx::Color fill)
{
    Draw::rect(c, box, std::min(box.w, box.h) / 2.0f, fill);
}

void Draw::circle(const Ctx& c, const gfx::Rectf& box, gfx::Color fill,
                  const Pen& pen)
{
    Draw::rect(c, box, std::min(box.w, box.h) / 2.0f, fill, pen);
}

// ---------------------------------------------------------------- streck

void Draw::line(const Ctx& c, float x1, float y1, float x2, float y2,
                const Pen& pen)
{
    const float ox = c.box.x, oy = c.box.y, s = c.scale;
    const float px1 = ox + x1 * s, py1 = oy + y1 * s;
    const float px2 = ox + x2 * s, py2 = oy + y2 * s;

    const float w = pen.width * s;
    if (w <= 1.0f) {   // hårlinje — fasadens 1 fysiska px
        c.r->line(px1, py1, px2, py2, pen.color);
        return;
    }

    // Axelparallellt: en FYLLD stav ger exakt bredd utan sömmar. Det är
    // vanligaste fallet (caption-glyfer, gridlinjer, understrykningar).
    if (px1 == px2) {
        c.r->fillRect(gfx::Rectf{px1 - w / 2.0f, std::min(py1, py2), w,
                                 std::abs(py2 - py1)}, pen.color);
        return;
    }
    if (py1 == py2) {
        c.r->fillRect(gfx::Rectf{std::min(px1, px2), py1 - w / 2.0f,
                                 std::abs(px2 - px1), w}, pen.color);
        return;
    }

    // Diagonal: fasaden har ingen bredd på line, så staven byggs av
    // hårlinjer förskjutna längs NORMALEN (inte längs en axel — det gav
    // fel tjocklek och hål i diagonaler) med en fysisk px mellan varje.
    const float dx  = px2 - px1, dy = py2 - py1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f)
        return;
    const float nx = -dy / len, ny = dx / len;
    const int   n  = static_cast<int>(std::lround(w));
    for (int i = 0; i < n; ++i) {
        const float off = static_cast<float>(i) - (n - 1) / 2.0f;
        c.r->line(px1 + nx * off, py1 + ny * off,
                  px2 + nx * off, py2 + ny * off, pen.color);
    }
}

// ----------------------------------------------------------- glyf-former
// Proportionerna är TECKNENS, inte stilens: rutan som skickas in är hela
// måttet (se draw.h). Därför står det tal här och bara här.

void Draw::check(const Ctx& c, const gfx::Rectf& b, const Pen& pen)
{
    // Bocken: kort nedslag följt av långt uppslag, inskriven i rutan.
    const float x1 = b.x + 0.22f * b.w, y1 = b.y + 0.52f * b.h;
    const float x2 = b.x + 0.42f * b.w, y2 = b.y + 0.72f * b.h;
    const float x3 = b.x + 0.78f * b.w, y3 = b.y + 0.28f * b.h;
    Draw::line(c, x1, y1, x2, y2, pen);
    Draw::line(c, x2, y2, x3, y3, pen);
}

void Draw::chevron(const Ctx& c, const gfx::Rectf& b, Dir dir, const Pen& pen)
{
    // ∧ ∨ ‹ › — SAMMA tecken vridet: spetsen i den kant `dir` pekar mot,
    // skänklarna i den motsatta. Två linjer i alla fyra fallen.
    const float cx = b.x + b.w / 2.0f;
    const float cy = b.y + b.h / 2.0f;
    switch (dir) {
    case Dir::Up:
        Draw::line(c, b.x, b.y + b.h, cx, b.y, pen);
        Draw::line(c, cx, b.y, b.x + b.w, b.y + b.h, pen);
        break;
    case Dir::Left:
        Draw::line(c, b.x + b.w, b.y, b.x, cy, pen);
        Draw::line(c, b.x, cy, b.x + b.w, b.y + b.h, pen);
        break;
    case Dir::Right:
        Draw::line(c, b.x, b.y, b.x + b.w, cy, pen);
        Draw::line(c, b.x + b.w, cy, b.x, b.y + b.h, pen);
        break;
    case Dir::Down:
    default:
        Draw::line(c, b.x, b.y, cx, b.y + b.h, pen);
        Draw::line(c, cx, b.y + b.h, b.x + b.w, b.y, pen);
        break;
    }
}

void Draw::grip(const Ctx& c, const gfx::Rectf& b, bool vertical,
                gfx::Color col)
{
    // Prickens sida d = en fjärdedel av den korta sidan: d/2 luft, en prick,
    // d luft, en prick, d/2 luft. Längs den långa sidan står prickarna med
    // steget 2d, och raden centreras.
    const float kort = vertical ? b.w : b.h;
    const float lång = vertical ? b.h : b.w;
    const float d    = kort / 4.0f;
    if (d <= 0.0f || lång < d)
        return;
    const int   n     = static_cast<int>((lång + d) / (2.0f * d));
    const float rad   = static_cast<float>(n) * 2.0f * d - d;
    const float start = (lång - rad) / 2.0f;
    for (int i = 0; i < n; ++i) {
        const float längs = start + static_cast<float>(i) * 2.0f * d;
        for (const float tvärs : {d / 2.0f, d / 2.0f + 2.0f * d}) {
            const gfx::Rectf p =
                vertical ? gfx::Rectf{b.x + tvärs, b.y + längs, d, d}
                         : gfx::Rectf{b.x + längs, b.y + tvärs, d, d};
            Draw::rect(c, p, 0.0f, col);
        }
    }
}

void Draw::cross(const Ctx& c, const gfx::Rectf& b, const Pen& pen)
{
    Draw::line(c, b.x, b.y, b.x + b.w, b.y + b.h, pen);
    Draw::line(c, b.x, b.y + b.h, b.x + b.w, b.y, pen);
}

void Draw::minus(const Ctx& c, const gfx::Rectf& b, const Pen& pen)
{
    const float cy = b.y + b.h / 2.0f;
    // Hårlinje = ett streck; med bredd blir det en STAV (skarpare än
    // dubblerade linjer i de små caption-glyferna).
    if (pen.width <= 0.0f)
        Draw::line(c, b.x, cy, b.x + b.w, cy, pen);
    else
        Draw::rect(c, {b.x, cy - pen.width / 2.0f, b.w, pen.width}, 0.0f,
                   pen.color);
}

void Draw::plus(const Ctx& c, const gfx::Rectf& b, const Pen& pen)
{
    const float cx = b.x + b.w / 2.0f, cy = b.y + b.h / 2.0f;
    Draw::minus(c, b, pen);
    if (pen.width <= 0.0f)
        Draw::line(c, cx, b.y, cx, b.y + b.h, pen);
    else
        Draw::rect(c, {cx - pen.width / 2.0f, b.y, pen.width, b.h}, 0.0f,
                   pen.color);
}

void Draw::badge(const Ctx& c, const gfx::Rectf& box, Sign sign,
                 gfx::Color fill, gfx::Color fg)
{
    Draw::circle(c, box, fill);
    // Tecknet fyller skivans mittersta HALVA och strecket är en ÅTTONDEL av
    // diametern — märkets definition. Vid 12–14 px blir strecket 1.5–1.75 px,
    // tjockt nog att läsas mot en mättad skiva.
    const float      d   = std::min(box.w, box.h);
    const gfx::Rectf g   = Draw::inset(box, d / 4.0f);
    const Pen        pen{fg, d / 8.0f};
    switch (sign) {
    case Sign::Plus:
        Draw::plus(c, g, pen);
        break;
    case Sign::Minus:
        Draw::minus(c, g, pen);
        break;
    case Sign::Arrow:
    default:
        // › är SMALARE än + och −: en vinkelhake över hela rutan blir en
        // trubbig kil. Halva bredden, centrerad.
        Draw::chevron(c, {g.x + g.w / 4.0f, g.y, g.w / 2.0f, g.h}, Dir::Right,
                      pen);
        break;
    }
}

void Draw::icon(const Ctx& c, const gfx::Rectf& box, Icon icon, const Pen& pen)
{
    // Ikonerna är ritade i en ENHETSKVADRAT (0–1 åt båda hållen) som
    // centreras i rutan. Talen nedan är ikonernas form — luften till rutans
    // kant ingår, så att en mapp och en fil står lika stora bredvid
    // varandra. Raka konturer är `frame` (SDF-hörn utan skarvar), sneda och
    // öppna streck `line`.
    const float s  = std::min(box.w, box.h);
    const float ox = box.x + (box.w - s) / 2.0f;
    const float oy = box.y + (box.h - s) / 2.0f;
    auto L = [&](float x1, float y1, float x2, float y2) {
        Draw::line(c, ox + x1 * s, oy + y1 * s, ox + x2 * s, oy + y2 * s, pen);
    };
    auto R = [&](float x1, float y1, float x2, float y2, float radie) {
        Draw::frame(c, {ox + x1 * s, oy + y1 * s, (x2 - x1) * s, (y2 - y1) * s},
                    radie * s, pen);
    };
    // Bladet med det vikta hörnet — `File` och `Document` delar det.
    auto blad = [&] {
        L(0.20f, 0.06f, 0.62f, 0.06f);
        L(0.62f, 0.06f, 0.82f, 0.26f);
        L(0.82f, 0.26f, 0.82f, 0.94f);
        L(0.82f, 0.94f, 0.20f, 0.94f);
        L(0.20f, 0.94f, 0.20f, 0.06f);
        L(0.62f, 0.06f, 0.62f, 0.26f);
        L(0.62f, 0.26f, 0.82f, 0.26f);
    };

    switch (icon) {
    case Icon::Folder:
        R(0.06f, 0.30f, 0.94f, 0.86f, 0.06f);   // kroppen
        L(0.06f, 0.30f, 0.06f, 0.16f);          // fliken uppe till vänster
        L(0.06f, 0.16f, 0.40f, 0.16f);
        L(0.40f, 0.16f, 0.50f, 0.30f);
        break;
    case Icon::File:
        blad();
        break;
    case Icon::Document:
        blad();
        L(0.32f, 0.46f, 0.70f, 0.46f);
        L(0.32f, 0.60f, 0.70f, 0.60f);
        L(0.32f, 0.74f, 0.58f, 0.74f);
        break;
    case Icon::Drive:
        R(0.06f, 0.30f, 0.94f, 0.74f, 0.08f);
        L(0.18f, 0.52f, 0.50f, 0.52f);          // springan
        R(0.70f, 0.47f, 0.80f, 0.57f, 0.05f);   // lampan (radien = halva
        break;                                  // sidan ⇒ en ring)
    case Icon::House:
        L(0.06f, 0.50f, 0.50f, 0.10f);          // taket
        L(0.50f, 0.10f, 0.94f, 0.50f);
        L(0.18f, 0.40f, 0.18f, 0.92f);          // väggarna
        L(0.18f, 0.92f, 0.82f, 0.92f);
        L(0.82f, 0.92f, 0.82f, 0.40f);
        R(0.42f, 0.62f, 0.58f, 0.92f, 0.0f);    // dörren
        break;
    case Icon::Monitor:
        R(0.06f, 0.12f, 0.94f, 0.70f, 0.04f);   // skärmen
        L(0.50f, 0.70f, 0.50f, 0.84f);          // foten
        L(0.28f, 0.88f, 0.72f, 0.88f);
        break;
    case Icon::Download:
        L(0.50f, 0.08f, 0.50f, 0.62f);          // pilen
        L(0.28f, 0.40f, 0.50f, 0.62f);
        L(0.50f, 0.62f, 0.72f, 0.40f);
        L(0.12f, 0.66f, 0.12f, 0.90f);          // tråget
        L(0.12f, 0.90f, 0.88f, 0.90f);
        L(0.88f, 0.90f, 0.88f, 0.66f);
        break;
    case Icon::Picture:
        R(0.06f, 0.14f, 0.94f, 0.86f, 0.04f);   // ramen
        L(0.14f, 0.78f, 0.38f, 0.48f);          // bergen
        L(0.38f, 0.48f, 0.58f, 0.70f);
        L(0.58f, 0.70f, 0.70f, 0.58f);
        L(0.70f, 0.58f, 0.86f, 0.78f);
        R(0.62f, 0.24f, 0.78f, 0.40f, 0.08f);   // solen
        break;
    case Icon::Calendar:
        R(0.10f, 0.18f, 0.90f, 0.90f, 0.06f);   // bladet
        L(0.10f, 0.38f, 0.90f, 0.38f);          // huvudet
        L(0.32f, 0.08f, 0.32f, 0.26f);          // ringarna
        L(0.68f, 0.08f, 0.68f, 0.26f);
        R(0.56f, 0.56f, 0.76f, 0.76f, 0.02f);   // den markerade dagen
        break;
    case Icon::Pinned:
        R(0.34f, 0.08f, 0.66f, 0.46f, 0.04f);   // huvudet
        L(0.20f, 0.46f, 0.80f, 0.46f);          // kragen
        L(0.50f, 0.46f, 0.50f, 0.92f);          // nålen
        break;
    case Icon::Unpinned:   // samma nål vriden ett kvarts varv
        R(0.54f, 0.34f, 0.92f, 0.66f, 0.04f);
        L(0.54f, 0.20f, 0.54f, 0.80f);
        L(0.08f, 0.50f, 0.54f, 0.50f);
        break;
    case Icon::ArrowUp:
    default:
        L(0.50f, 0.90f, 0.50f, 0.12f);
        L(0.22f, 0.40f, 0.50f, 0.12f);
        L(0.50f, 0.12f, 0.78f, 0.40f);
        break;
    }
}

// ----------------------------------------------------------- text & bild

void Draw::text(const Ctx& c, const gfx::Rectf& r, const std::string& s,
                gfx::Color col, Align align)
{
    const gfx::Rectf pr = phys(c, r);
    const gfx::Sizef m  = c.font->measure(s);
    float            x  = pr.x;
    if (align == Align::Center)     x = pr.x + (pr.w - m.w) / 2.0f;
    else if (align == Align::Right) x = pr.x + (pr.w - m.w);
    c.r->drawText(*c.font, x, pr.y + (pr.h - m.h) / 2.0f, s, col);
}

// RADBRUTEN TEXT — enheterna och den vertikala placeringen står skrivna i
// draw.h och gäller BÅDA funktionerna här: bryt mot `maxW` LOGISKA px,
// mät fysiskt, dividera tillbaka, och lägg radblocket centrerat med en
// `lineHeight` per rad. Skulle de två sluta vara överens är resultatet en
// text som mäts till en storlek och ritas i en annan — därför delar de
// `wrapLines` nedan i stället för att räkna var för sig. Den är
// publik: det flerradiga fältet äger sina rader själv (caret, markering,
// scroll) och bryter genom SAMMA funktion som en radbruten etikett.

// Raderna i LOGISKA px. Tom text och avslutande '\n' ger en tom rad ur
// `wrapText`, alltså minst en rad — höjden kan aldrig bli 0.
std::vector<gfx::WrappedLine> Draw::wrapLines(const Ctx& c,
                                              const std::string& s, float maxW)
{
    std::vector<gfx::WrappedLine> lines = gfx::wrapText(*c.font, s,
                                                        maxW * c.scale);
    if (lines.empty())
        lines.push_back(gfx::WrappedLine{0, 0});
    return lines;
}

gfx::Sizef Draw::measureWrapped(const Ctx& c, const std::string& s, float maxW)
{
    const auto lines = wrapLines(c, s, maxW);
    float      w     = 0.0f;
    for (const gfx::WrappedLine& l : lines) {
        // Fysisk mätning (fonten talar fysiskt) — divisionen sker sist, på
        // maxvärdet, exakt som `Draw::measure` gör.
        const float lw =
            c.font->measure(s.substr(l.begin, l.end - l.begin)).w;
        w = std::max(w, lw);
    }
    return gfx::Sizef{w / c.scale,
                      static_cast<float>(lines.size()) * Draw::lineHeight(c)};
}

void Draw::textWrapped(const Ctx& c, const gfx::Rectf& r, const std::string& s,
                       gfx::Color col, Align align)
{
    const auto  lines  = wrapLines(c, s, r.w);
    const float lh     = Draw::lineHeight(c);
    const float blockH = static_cast<float>(lines.size()) * lh;
    float       y      = r.y + (r.h - blockH) / 2.0f;
    for (const gfx::WrappedLine& l : lines) {
        // Varje rad ritas i sin EGEN `lineHeight`-höga rect — `Draw::text`
        // v-centrerar i den, så radavståndet är exakt radhöjden.
        Draw::text(c, gfx::Rectf{r.x, y, r.w, lh},
                   s.substr(l.begin, l.end - l.begin), col, align);
        y += lh;
    }
}

// KORTNINGEN. Tecknet är U+2026 — typografi och inget mått, av
// samma skäl som bockens proportioner bor i `check`. Prefixet som ryms
// framför ellipsen är fontens `fitWidth` — samma råvara som radbrytningen,
// och den svarar alltid på en TECKENGRÄNS. Fonten talar fysiskt, så
// bredden skalas dit precis som i `wrapLines`.
std::string Draw::elide(const Ctx& c, const std::string& s, float maxW)
{
    if (Draw::measure(c, s).w <= maxW)
        return s;
    const float rum = maxW - Draw::measure(c, ellips()).w;
    if (rum < 0.0f)
        return std::string();   // inte ens ellipsen ryms
    std::size_t n = std::min(c.font->fitWidth(s, rum * c.scale), s.size());
    // Ett blanksteg närmast ellipsen ser ut som ett fel ("Rapport …").
    while (n > 0 && s[n - 1] == ' ')
        --n;
    return s.substr(0, n) + ellips();
}

void Draw::textElided(const Ctx& c, const gfx::Rectf& r, const std::string& s,
                      gfx::Color col, Align align)
{
    Draw::text(c, r, Draw::elide(c, s, r.w), col, align);
}

// GLORIAN. Ett Ctx vars `box` är förskjuten ritar allt det ritar
// förskjutet — `phys` lägger på `c.box` — så en kopia per punkt på ringen är
// hela mekanismen. Punkterna ligger en fysisk px isär längs omkretsen (2πr),
// minst åtta (en smal gloria ska ändå bli sluten) och högst trettiotvå (en
// bred kostar inte fler kopior än konturen behöver).
void Draw::halo(const Ctx& c, float width,
                const std::function<void(const Ctx&)>& paint)
{
    if (width <= 0.0f)
        return;
    constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
    const float     r    = width * c.scale;   // fysiska px
    const int       n    = std::clamp(static_cast<int>(std::ceil(kTau * r)), 8, 32);
    for (int i = 0; i < n; ++i) {
        const float a  = kTau * static_cast<float>(i) / static_cast<float>(n);
        Ctx         hc = c;
        hc.box.x += r * std::cos(a);
        hc.box.y += r * std::sin(a);
        paint(hc);
    }
}

// TEXTBLOCKET — styckena först (`\n` bryter alltid), sedan varje
// stycke på ordgräns (`ordrader` ovan). Raderna hålls som (stycke,
// intervall), för den SISTA synliga raden bär inte sin egen text utan
// RESTEN av sitt stycke — så att en enradsrubrik blir exakt
// `elide(texten)`, också när brytningen hade ätit ett avslutande blanksteg.
std::vector<std::string> Draw::elideLines(const Ctx& c, const std::string& s,
                                          float maxW, int maxLines)
{
    std::vector<std::string> stycken;
    for (std::size_t a = 0;;) {
        const std::size_t b = s.find('\n', a);
        stycken.push_back(s.substr(a, b == std::string::npos ? b : b - a));
        if (b == std::string::npos)
            break;
        a = b + 1;
    }
    struct Rad {
        std::size_t      stycke;
        gfx::WrappedLine l;
    };
    std::vector<Rad> rader;
    for (std::size_t i = 0; i < stycken.size(); ++i)
        for (const gfx::WrappedLine& l : ordrader(c, stycken[i], maxW))
            rader.push_back(Rad{i, l});

    const bool kapad =
        maxLines > 0 && rader.size() > static_cast<std::size_t>(maxLines);
    const std::size_t n =
        kapad ? static_cast<std::size_t>(maxLines) : rader.size();
    std::vector<std::string> ut;
    ut.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Rad&         r = rader[i];
        const std::string& p = stycken[r.stycke];
        if (i + 1 < n) {
            ut.push_back(elide(c, p.substr(r.l.begin, r.l.end - r.l.begin),
                               maxW));
            continue;
        }
        // DEN SISTA SYNLIGA: resten av stycket. Är blocket kapat och fler
        // stycken står under den läggs ellipsen till på förhand — resten av
        // just det här stycket kan rymmas, men texten tar inte slut här (och
        // ett blanksteg närmast ellipsen ser ut som ett fel, som i `elide`).
        std::string rest = p.substr(r.l.begin);
        if (kapad && r.stycke + 1 < stycken.size()) {
            while (!rest.empty() && rest.back() == ' ')
                rest.pop_back();
            rest += ellips();
        }
        ut.push_back(elide(c, rest, maxW));
    }
    return ut;
}

void Draw::glyph(const Ctx& c, const gfx::Rectf& r, const ImageSource& img,
                 float opacity)
{
    if (!img.valid() || !c.images)
        return;
    if (img.kind() == ImageSource::Kind::Stock) {
        // STANDARDIKONEN rastreras i rectets EXAKTA storlek i
        // fysiska px — kvadraten centrerad och fäst på hela pixlar som i
        // `glyphResampled` — så ingen sampling sker vid någon skala.
        const gfx::Rectf p    = phys(c, r);
        const float      sida = std::round(std::min(p.w, p.h));
        if (sida <= 0.0f)
            return;
        const Bitmap bild = detail::stockBitmap(
            img.stock(), static_cast<int>(sida), img.stockStyle(),
            c.theme->colors);
        if (gfx::Image* im = c.images->get(bild))
            c.r->drawImage(*im,
                           gfx::Rectf{std::round(p.x + (p.w - sida) / 2.0f),
                                      std::round(p.y + (p.h - sida) / 2.0f),
                                      sida, sida},
                           opacity);
        return;
    }
    if (gfx::Image* im = c.images->get(img, c))
        c.r->drawImage(*im, phys(c, r), opacity);
}

// 1:1 i fysiska px: rectet fästs på hela pixlar (avrundat hörn och storlek)
// och bilden räknas om till exakt den storleken, så ingen sampling sker vid
// ritningen. En RenderTexture har inga pixlar på CPU:n att räkna ur — den
// ritas med GPU:ns filter som i `glyph`, och likaså en Bitmap som släppt
// sina när fönstret inte redan har den omräknad.
void Draw::glyphResampled(const Ctx& c, const gfx::Rectf& r,
                          const ImageSource& img, float opacity)
{
    if (!img.valid() || !c.images)
        return;
    // En standardikon rastreras redan 1:1 i `glyph`.
    if (img.kind() == ImageSource::Kind::Texture ||
        img.kind() == ImageSource::Kind::Stock) {
        glyph(c, r, img, opacity);
        return;
    }
    const gfx::Rectf p = phys(c, r);
    const int        w = static_cast<int>(std::lround(p.w));
    const int        h = static_cast<int>(std::lround(p.h));
    if (w <= 0 || h <= 0)
        return;
    if (gfx::Image* im = c.images->getResampled(img, w, h))
        c.r->drawImage(*im,
                       gfx::Rectf{std::round(p.x), std::round(p.y),
                                  static_cast<float>(w), static_cast<float>(h)},
                       opacity);
    else if (img.kind() == ImageSource::Kind::Bitmap && !img.bitmap().pixels())
        glyph(c, r, img, opacity);   // utan pixlar: GPU:ns filter
}

ImageSource Draw::pickIcon(const Ctx& c, const gfx::Rectf& r,
                           std::initializer_list<ImageSource> sources)
{
    if (!c.images)
        return ImageSource{};
    const gfx::Rectf p = phys(c, r);
    const int target =
        static_cast<int>(std::lround(std::max(p.w, p.h)));
    std::vector<const ImageSource*> källor;
    std::vector<int>                sidor;
    for (const ImageSource& s : sources) {
        int w = 0, ht = 0;
        if (!s.valid() || !c.images->sourceSize(s, w, ht))
            continue;
        källor.push_back(&s);
        sidor.push_back(std::max(w, ht));   // den längsta sidan passas in
    }
    const int i = glzassets::pickIconSource(sidor, target);
    return i >= 0 ? *källor[static_cast<std::size_t>(i)] : ImageSource{};
}

gfx::Sizef Draw::imageSize(const Ctx& c, const ImageSource& img)
{
    // Bildens mått är FYSISKA px (gfx/image.h) — ritrymden talar logiska,
    // så divisionen sker här och bara här. En Bitmap eller RenderTexture
    // bär sina mått i handtaget: ingen uppladdning, ingen cache (mätningen
    // körs utanför en frame).
    switch (img.kind()) {
    case ImageSource::Kind::Bitmap:
    case ImageSource::Kind::Texture: {
        if (!img.valid())
            return gfx::Sizef{0.0f, 0.0f};
        const bool bild = img.kind() == ImageSource::Kind::Bitmap;
        const int  w    = bild ? img.bitmap().width() : img.texture().width();
        const int  h    = bild ? img.bitmap().height() : img.texture().height();
        return gfx::Sizef{static_cast<float>(w) / c.scale,
                          static_cast<float>(h) / c.scale};
    }
    case ImageSource::Kind::Asset: {
        if (!img.valid() || !c.images)
            return gfx::Sizef{0.0f, 0.0f};
        gfx::Image* im = c.images->get(img.asset());
        if (!im || !im->valid())
            return gfx::Sizef{0.0f, 0.0f};
        return gfx::Sizef{static_cast<float>(im->width()) / c.scale,
                          static_cast<float>(im->height()) / c.scale};
    }
    // En vektor: klassens sida, oberoende av skalan.
    case ImageSource::Kind::Stock:
        return gfx::Sizef{img.sizeClass(), img.sizeClass()};
    case ImageSource::Kind::None:
        break;
    }
    return gfx::Sizef{0.0f, 0.0f};
}

gfx::Sizef Draw::measure(const Ctx& c, const std::string& s)
{
    const gfx::Sizef m = c.font->measure(s);
    return gfx::Sizef{m.w / c.scale, m.h / c.scale};
}

float Draw::lineHeight(const Ctx& c)
{
    return c.font->height() / c.scale;
}

float Draw::ascent(const Ctx& c)
{
    return c.font->ascent() / c.scale;
}

// FONTEN ISATT — kontraktet står i draw.h. En storlek lika med stilens är
// ingen fri plats: brödtextens plats (eller fönstrets egen font) delas då.
Ctx Draw::withFont(const Ctx& c, gfx::FontFamily family, gfx::FontStyle style,
                   Length size)
{
    if (!c.textFonts || !c.r || !c.style)
        return c;
    float px = 0.0f;
    if (size.value > 0.0f && std::isfinite(size.value) &&
        size.unit != Unit::World) {
        px = size.unit == Unit::Mm ? size.value * c.pxPerMm
                                   : size.value * c.scale;
        if (px == c.style->uiFontSize * c.scale)
            px = 0.0f;
    }
    if (px <= 0.0f && family == gfx::FontFamily::UI &&
        style == gfx::FontStyle::Regular)
        return c;   // fönstrets font
    gfx::Font* f = px > 0.0f
                       ? c.textFonts->sized(*c.r, px, family, style)
                       : c.textFonts->get(*c.r, *c.style, c.scale,
                                          TextSize::Body, family, style);
    if (!f)
        return c;
    Ctx t  = c;
    t.font = f;
    return t;
}

float Draw::prefixWidth(const Ctx& c, const std::string& s, std::size_t bytes)
{
    return c.font->prefixWidth(s, bytes) / c.scale;
}

std::size_t Draw::byteIndexAtX(const Ctx& c, const std::string& s, float x)
{
    return c.font->byteIndexAtX(s, x * c.scale);
}

gfx::Sizef Draw::measure(const Ctx& c, const Content& ct)
{
    const bool       hasImg  = ct.img.valid();
    const bool       hasText = !ct.text.empty();
    const gfx::Sizef t       = hasText ? Draw::measure(c, ct.text)
                                       : gfx::Sizef{0.0f, 0.0f};
    if (!hasImg)
        return t;
    if (!hasText)
        return gfx::Sizef{ct.iconSide, ct.iconSide};

    const float gap   = ct.gap;
    const bool  horis = ct.pos == ImagePos::Left || ct.pos == ImagePos::Right;
    return horis ? gfx::Sizef{ct.iconSide + gap + t.w,
                              std::max(ct.iconSide, t.h)}
                 : gfx::Sizef{std::max(ct.iconSide, t.w),
                              ct.iconSide + gap + t.h};
}

void Draw::content(const Ctx& c, const gfx::Rectf& r, const Content& ct)
{
    const bool hasImg  = ct.img.valid() && c.images && c.images->get(ct.img, c);
    const bool hasText = !ct.text.empty();

    if (!hasImg) {   // ren text (även när assetens handtag är trasigt)
        if (hasText)
            Draw::text(c, r, ct.text, ct.color, Align::Center);
        return;
    }
    const float ic = ct.iconSide;
    if (!hasText) {  // ren ikon, centrerad
        contentGlyph(c, {r.x + (r.w - ic) / 2.0f, r.y + (r.h - ic) / 2.0f,
                         ic, ic}, ct);
        return;
    }

    // Bild + text: blocket centreras i r, varje del centreras på tväraxeln.
    const gfx::Sizef t     = Draw::measure(c, ct.text);
    const bool       horis = ct.pos == ImagePos::Left ||
                             ct.pos == ImagePos::Right;
    const bool       first = ct.pos == ImagePos::Left ||
                             ct.pos == ImagePos::Above;
    if (horis) {
        const float bx  = r.x + (r.w - (ic + ct.gap + t.w)) / 2.0f;
        const float ix  = first ? bx : bx + t.w + ct.gap;
        const float tx  = first ? bx + ic + ct.gap : bx;
        contentGlyph(c, {ix, r.y + (r.h - ic) / 2.0f, ic, ic}, ct);
        Draw::text (c, {tx, r.y, t.w, r.h}, ct.text, ct.color);
    } else {
        const float by = r.y + (r.h - (ic + ct.gap + t.h)) / 2.0f;
        const float iy = first ? by : by + t.h + ct.gap;
        const float ty = first ? by + ic + ct.gap : by;
        contentGlyph(c, {r.x + (r.w - ic) / 2.0f, iy, ic, ic}, ct);
        Draw::text (c, {r.x, ty, r.w, t.h}, ct.text, ct.color, Align::Center);
    }
}

// ----------------------------------------------------------------- klipp

void Draw::clip(const Ctx& c, const gfx::Rectf& r, float radius)
{
    // Radien skalas som varje annat logiskt mått — Draw är den enda plats
    // i widget-lagret som fortfarande får multiplicera med c.scale.
    c.r->pushClip(phys(c, r), radius * c.scale);
}

void Draw::endClip(const Ctx& c)
{
    c.r->popClip();
}

// ------------------------------------------------------------------ rymd

gfx::Rectf Draw::inset(const gfx::Rectf& r, float d)
{
    return gfx::Rectf{r.x + d, r.y + d, r.w - 2.0f * d, r.h - 2.0f * d};
}

gfx::Rectf Draw::project(const Ctx& c, const gfx::Rectf& base,
                         const gfx::Rectf& local)
{
    return gfx::Rectf{base.x + local.x * c.scale, base.y + local.y * c.scale,
                      local.w * c.scale, local.h * c.scale};
}

gfx::Rectf Draw::bounds(const Ctx& c, const gfx::Rectf& base)
{
    return gfx::Rectf{0.0f, 0.0f, base.w / c.scale, base.h / c.scale};
}

float Draw::localX(const Ctx& c, const gfx::Rectf& base, float physX)
{
    return (physX - base.x) / c.scale;
}

float Draw::localY(const Ctx& c, const gfx::Rectf& base, float physY)
{
    return (physY - base.y) / c.scale;
}

// ------------------------------------------------------ rullningslisterna

gfx::Rectf Draw::scrollThumb(const ScrollMetrics& m, const gfx::Rectf& track,
                             float pad, float minLen)
{
    // Tumlängd ∝ page/(intervall+page), aldrig under minLen; position ∝
    // value i det fria utrymmet. Enhetsneutralt — bara förhållandena
    // används, så samma funktion tjänar både widget och overlay-remsa.
    const float span   = std::max(0.0f, m.max - m.min);
    const float len    = m.vertical ? track.h : track.w;
    const float frac   = span + m.page > 0.0f ? m.page / (span + m.page)
                                              : 1.0f;
    const float thumbL = std::clamp(std::max(minLen, len * frac), 0.0f, len);
    const float t      = span > 0.0f
                             ? std::clamp((m.value - m.min) / span, 0.0f, 1.0f)
                             : 0.0f;
    const float start  = (m.vertical ? track.y : track.x) + (len - thumbL) * t;
    const float thick  = (m.vertical ? track.w : track.h) - 2.0f * pad;
    return m.vertical ? gfx::Rectf{track.x + pad, start, thick, thumbL}
                      : gfx::Rectf{start, track.y + pad, thumbL, thick};
}

void Draw::scrollThumbDraw(const Ctx& c, const gfx::Rectf& track,
                           const ScrollMetrics& m, float pad, float minLen,
                           gfx::Color col)
{
    const gfx::Rectf t = scrollThumb(m, track, pad, minLen);
    Draw::rect(c, t, std::min(t.w, t.h) / 2.0f, col);   // pillerform
}

} // namespace glaze
