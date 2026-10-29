// MarkdownView — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna. Tolkningen
// (`../markdown/md_parse.h`) och layouten (`../markdown/md_layout.h`) är
// interna och grindas headless; här bor cacharna, ritningen per familj och
// beteendet — länkarna, pekaren, tooltipen och ankarna. Rullningen är Panels.
//
// MÄTNINGEN OCH RITNINGEN LÄSER SAMMA TVÅ FUNKTIONER: `contentWidth` (den
// bredd dokumentet bryts i, ur widgetens bredd) och `layoutAt` (layouten i
// den bredden). `heightForWidth`/`contentSize` får sin bredd GIVEN och läser
// aldrig `resolve`, `behaviorCtx`, `scrollRange` eller `c.box` (en
// fälla) — bara ritningen, musen och rullningens mått gör det.

#include "glaze/widgets/markdown_view.h"

#include <algorithm>
#include <cstddef>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/assets.h"
#include "glaze/draw.h"

#include "../image_cache.h"   // detail::assetGeneration
#include "../markdown/md_layout.h"
#include "../markdown/md_parse.h"
#include "../prop_parse.h"

namespace glaze {

// EN LAYOUTPLATS — nyckeln är ALLT layouten beror på utöver dokumentet, som
// VÄRDEN: `md::Metrics` (en stil byts utan att `c.style` byter pekare),
// bildtabellens generation, asset-butikens generation och om fönstret har en
// bildcache (utan den mäter en asset 0 — mätt före fönstret blir den saknad,
// och nästa mätning i fönstret lägger om). Bilderna slås upp EN gång per
// plats och ritas därifrån, så mätningen och ritningen ser samma källor.
struct MarkdownView::LayoutSlot {
    struct Key {
        std::uint64_t docGen = 0;
        float         width  = 0.0f;
        md::Metrics   metrics;
        std::uint64_t imagesGen = 0;
        std::uint64_t assetGen  = 0;
        bool          cache     = false;
        bool          operator==(const Key&) const = default;
    };
    bool                     used = false;
    Key                      key;
    md::Layout               layout;
    std::vector<ImageSource> images;   // källan för `Document::images[i]`
};

// TVÅ PLATSER: mätordningen frågar AUTO-bredden OCH den givna bredden i
// varje `resolve`, så en plats hade lagt om dokumentet två gånger per
// resolve. Den senast använda skrivs aldrig över.
struct MarkdownView::Layouts {
    LayoutSlot slots[2];
    int        last = 0;
};

namespace {

// Ytorna och strecken bakom texten, per familj. De fyra tillstånden
// (princip 7) står vid `draw` — ytorna har inget eget läge.
void drawDeco(const Ctx& c, md::Deco::Kind kind, const gfx::Rectf& r,
              gfx::Color fg)
{
    const auto& m    = *c.style;
    const auto& col  = c.theme->colors;
    const Pen   kant{col.border, m.borderWidth};
    using K = md::Deco::Kind;
    switch (kind) {
    case K::QuoteBar:
        Draw::rect(c, r, 0.0f, col.border);
        return;
    case K::Rule:
        Draw::line(c, r.x, r.y, r.x + r.w, r.y, kant);
        return;
    case K::BulletDisc:
        Draw::circle(c, r, fg);
        return;
    case K::BulletRing:
        Draw::frame(c, r, r.w / 2.0f, Pen{fg, m.borderWidth});
        return;
    case K::BulletSquare:
        Draw::rect(c, r, 0.0f, fg);
        return;
    case K::CodeBlock:
    case K::CodeSpan:
    case K::Cell:
    case K::HeaderCell:
        break;
    }

    switch (c.ui) {
    // MINIMALISTIC — "kanten är formen": kodblocket är bara hårlinjen,
    // inline-koden har ingen yta (monofonten bär skillnaden), och tabellen
    // har bara vågräta linjer — under varje rad och över huvudet.
    case UI::Minimalistic:
        if (kind == K::CodeBlock)
            Draw::frame(c, r, m.cornerRadius, kant);
        else if (kind == K::Cell || kind == K::HeaderCell)
            Draw::line(c, r.x, r.y + r.h, r.x + r.w, r.y + r.h, kant);
        if (kind == K::HeaderCell)
            Draw::line(c, r.x, r.y, r.x + r.w, r.y, kant);
        break;

    // GLAZE — faller igenom till Slate, och det är ett BESLUT: ett dokument
    // är TEXT på en yta, och Glazes formspråk (laminering, skugga) hör till
    // det man trycker på. Det som gör vyn till Glazes är paletten.
    case UI::Glaze:

    // SLATE — kodytorna i den skrivskyddade fältytan `fieldReadOnly` ("läs
    // gärna"), kodblocket med en hårlinje i `border` så ytan syns också där
    // `fieldReadOnly` ligger nära `panel`; inline-koden utan kant; fullt
    // tabellnät.
    case UI::Slate:
    default:
        if (kind == K::CodeBlock)
            Draw::rect(c, r, m.cornerRadius, col.fieldReadOnly, kant);
        else if (kind == K::CodeSpan)
            Draw::rect(c, r, m.cornerRadius, col.fieldReadOnly);
        else
            Draw::frame(c, r, 0.0f, kant);
        break;
    }
}

} // namespace

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("markdownview", MarkdownView)

MarkdownView::MarkdownView(TrText t) : text_(std::move(t))
{
    setClip(true);
    showScrollbars(true);   // överflöd ska SYNAS (TextArea/ListBox-prejudikatet)
}

// Här och inte i headern: dokumentet och layouterna är interna typer.
MarkdownView::~MarkdownView() = default;

// ------------------------------------------------------------ tillstånd

void MarkdownView::setText(TrText t)
{
    if (text_ == t)
        return;
    text_ = std::move(t);
    setScroll(0.0f, 0.0f);   // ett nytt dokument börjar överst
    contentChanged();
}

void MarkdownView::setImage(const std::string& name, ImageSource img)
{
    const auto it = std::find_if(images_.begin(), images_.end(),
                                 [&](const auto& p) { return p.first == name; });
    if (!img.valid()) {
        if (it == images_.end())
            return;
        images_.erase(it);
    } else if (it != images_.end()) {
        if (it->second == img)
            return;
        it->second = std::move(img);
    } else {
        images_.emplace_back(name, std::move(img));
    }
    ++imagesGen_;
    contentChanged();
}

ImageSource MarkdownView::image(const std::string& name) const
{
    for (const auto& [namn, bild] : images_)
        if (namn == name)
            return bild;
    return {};
}

void MarkdownView::scrollToAnchor(std::string anchor)
{
    pendingAnchor_ = std::move(anchor);
    invalidate();
}

// AUTO-HÖJD: höjden följer dokumentet, och ett syskon som är ankrat under vyn
// flyttar utan att vara skadat — hela fönstret ritas om (samma lucka som
// Label har, skriven som kantfall; ett dokumentbyte är sällsynt).
void MarkdownView::contentChanged()
{
    if (height <= 0.0f)
        if (AppWindow* f = appWindow()) {
            f->invalidate();
            return;
        }
    invalidate();
}

bool MarkdownView::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    return Panel::setProp(key, value);
}

// --------------------------------------------------------------- cacharna

// Dokumentet tolkas om när den UPPLÖSTA texten skiljer sig från den det
// tolkades ur — ett språkbyte slår igenom utan krokar, också i en vy utan
// fönster. Länkindexen pekar in i det gamla dokumentet och nollas.
const md::Document& MarkdownView::document() const
{
    const std::string& t = text();
    if (!doc_ || t != parsedFrom_) {
        doc_        = std::make_unique<md::Document>(md::parse(t));
        parsedFrom_ = t;
        ++docGen_;
        hoverLink_ = -1;
        pressLink_ = -1;
    }
    return *doc_;
}

// BILDEN för `![alt](src)`: vyns tabell först, sedan `lista/nyckel` som
// asset — men aldrig ett namn med ett schema, för det är en adress.
ImageSource MarkdownView::imageNamed(const std::string& src) const
{
    for (const auto& [namn, bild] : images_)
        if (namn == src)
            return bild;
    AssetHandle h;
    if (md::schemeLength(src) == 0 && propparse::parseImg(src, h))
        return h;
    return {};
}

// Bredden dokumentet bryts i: widgetens minus ramen och luften. HÖGERLUFTEN är
// ALLTID minst remsans tjocklek (`TextArea::padRight`), så brytbredden aldrig
// beror på om remsan syns.
float MarkdownView::contentWidth(const Ctx& c, float widgetW) const
{
    const auto& m = *c.style;
    return std::max(0.0f, widgetW - (isBorder() ? 2.0f * m.panelInset : 0.0f) -
                              m.markdownPad -
                              std::max(m.markdownPad, m.scrollThick));
}

// AUTO-höjden för en widget som är `widgetW` bred: dokumentet, luften och
// ramen — minst en rad, så en tom vy aldrig mäter 0.
float MarkdownView::autoHeight(const Ctx& c, float widgetW) const
{
    const auto&       m = *c.style;
    const md::Layout& L = layoutAt(c, contentWidth(c, widgetW)).layout;
    return std::max(L.height, Draw::lineHeight(c)) +
           2.0f * (m.markdownPad + (isBorder() ? m.panelInset : 0.0f));
}

const MarkdownView::LayoutSlot& MarkdownView::layoutAt(const Ctx& c,
                                                       float bredd) const
{
    const md::Document& d = document();
    if (!layouts_)
        layouts_ = std::make_unique<Layouts>();
    Layouts&              L = *layouts_;
    const LayoutSlot::Key k{docGen_,
                            bredd,
                            md::metricsFor(c),
                            imagesGen_,
                            detail::assetGeneration(),
                            c.images != nullptr};
    for (int i = 0; i < 2; ++i)
        if (L.slots[i].used && L.slots[i].key == k) {
            L.last = i;
            return L.slots[i];
        }
    const int   i = 1 - L.last;
    LayoutSlot& s = L.slots[i];
    s.used        = true;
    s.key         = k;
    s.images.clear();
    for (const md::Image& b : d.images)
        s.images.push_back(imageNamed(b.src));
    s.layout = md::build(c, d, s.images, bredd);
    L.last   = i;
    return s;
}

// ------------------------------------------------------------- mätningen

gfx::Sizef MarkdownView::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN — per axel: AUTO-bredden är stilens, AUTO-höjden
        // dokumentets i den bredden (samma väg som `heightForWidth`).
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f)
            s.w = c.style->markdownDefaultW;
        if (s.h <= 0.0f)
            s.h = autoHeight(c, s.w);
        return s;
    }
    }
}

// HÖJD FÖR BREDD (widget.h) — en sträckt eller Top/Bottom-dockad vy växer med
// sitt dokument. En satt höjd vinner (axelregeln), och då rullar vyn.
std::optional<float> MarkdownView::heightForWidth(const Ctx& c, float w) const
{
    if (height > 0.0f || w <= 0.0f)
        return std::nullopt;
    return autoHeight(c, w);
}

// FYSISKA px — Panels kontrakt. Bredden är ytans: dokumentet bryts mot den
// och rullar aldrig i sidled (en tabell som inte ryms klipps).
gfx::Sizef MarkdownView::contentExtent(const Ctx& c) const
{
    const float       w = Draw::bounds(c, resolve(c)).w;
    const md::Layout& L = layoutAt(c, contentWidth(c, w)).layout;
    return gfx::Sizef{innerBounds(c).w * c.scale,
                      (L.height + 2.0f * c.style->markdownPad) * c.scale};
}

// Dokumentets (0, 0) i widgetens lokala LOGISKA px: innehållsytans hörn plus
// luften, minus den KLAMPADE rullningen.
gfx::Pointf MarkdownView::origin(const Ctx& c) const
{
    const gfx::Rectf in    = innerBounds(c);
    const gfx::Sizef range = scrollRange(c);
    const float      pad   = c.style->markdownPad;
    return gfx::Pointf{in.x + pad - std::clamp(scrollX(), 0.0f, range.w),
                       in.y + pad - std::clamp(scrollY(), 0.0f, range.h)};
}

// ------------------------------------------------------------- ritningen

void MarkdownView::draw(const Ctx& c)
{
    Panel::draw(c);   // vyns yta är Panels i alla tre — ett dokument är en yta

    const auto&       m   = *c.style;
    const auto&       col = c.theme->colors;
    const LayoutSlot& s   = layoutAt(c, contentWidth(c, c.bounds().w));
    const md::Layout& L   = s.layout;

    // DET VÄNTANDE ANKARET löses här, mot det dokument som nu gäller — så
    // ordningen mellan `setText` och `scrollToAnchor` aldrig avgör.
    if (!pendingAnchor_.empty()) {
        const auto a = std::find_if(
            L.anchors.begin(), L.anchors.end(),
            [&](const auto& p) { return p.first == pendingAnchor_; });
        if (a != L.anchors.end())
            setScroll(0.0f, a->second);
        else
            gfx::log("MarkdownView(%s): ankaret '%s' finns inte — ingen "
                     "rullning",
                     name.c_str(), pendingAnchor_.c_str());
        pendingAnchor_.clear();
    }

    // De fyra tillstånden (princip 7) — dokumentet har två av dem:
    //   vila      text `text`, alt-texten `textDim`, länkar `link`, kodytorna
    //             `fieldReadOnly`, streck och nät `border`
    //   hover     länken under pekaren — ALLA dess delar — i `linkHover`
    //             (Minimalistic stryker under den först nu)
    //   tryck     — (klicket följer länken; inget mellanläge att visa)
    //   fokus     — (vyn är inte fokuserbar)
    //   inaktiv   all text `textDim`, bilderna tonade till 1 − iconFade
    const bool        på = isEnabled();
    const gfx::Color  fg = på ? col.text : col.textDim;
    const gfx::Rectf  in = innerBounds(c);
    const gfx::Pointf o  = origin(c);
    Draw::clip(c, in, innerRadius(c));

    for (const md::Deco& d : L.decos) {
        const gfx::Rectf r{o.x + d.r.x, o.y + d.r.y, d.r.w, d.r.h};
        if (r.y <= in.y + in.h && r.y + r.h >= in.y)
            drawDeco(c, d.kind, r, fg);
    }

    // RADERNA I VYN — den första med binärsökning (raderna står sorterade på
    // överkanten, och ingen är högre än `maxLineH`), därefter tills nederkanten:
    // O(synliga rader), aldrig O(dokumentet).
    const float topp   = in.y - o.y;
    const float botten = in.y + in.h - o.y;
    auto        rad    = std::lower_bound(
        L.lines.begin(), L.lines.end(), topp - L.maxLineH,
        [](const md::Line& l, float y) { return l.y < y; });
    for (; rad != L.lines.end() && rad->y < botten; ++rad) {
        if (rad->y + rad->h <= topp)
            continue;
        for (std::uint32_t i = rad->first; i < rad->first + rad->count; ++i) {
            const md::Item&  d = L.items[i];
            const gfx::Rectf r{o.x + d.x, o.y + d.top, d.w, d.h};
            switch (d.kind) {
            case md::Item::Kind::Image:
                Draw::glyph(c, r, s.images[static_cast<std::size_t>(d.image)],
                            på ? 1.0f : 1.0f - m.iconFade);
                break;
            case md::Item::Kind::Missing:
                Draw::icon(c, r, Icon::Picture, Pen{col.textDim, m.iconStroke});
                break;
            case md::Item::Kind::Text:
            case md::Item::Kind::Marker: {
                const bool hot = hovered() && d.link >= 0 && d.link == hoverLink_;
                const gfx::Color tc = !på            ? col.textDim
                                      : d.link >= 0  ? (hot ? col.linkHover
                                                            : col.link)
                                      : d.role == md::Role::Dim ? col.textDim
                                                                : col.text;
                Ctx fc  = c;
                fc.font = md::fontFor(c, d.font);
                Draw::text(fc, r, d.text, tc);
                // Understrykningen står `linkGap` under BASLINJEN — en rad i
                // ett stycke har ingen luft under sig att lägga den i, som
                // `Link` har. Slate och Glaze alltid, Minimalistic vid hover
                // (`Link`s regel).
                const bool streck = c.ui == UI::Minimalistic ? hot && på : true;
                if (d.link >= 0 && streck)
                    Draw::line(c, r.x, o.y + rad->baseline + m.linkGap,
                               r.x + r.w, o.y + rad->baseline + m.linkGap,
                               Pen{tc, m.linkUnderline});
                break;
            }
            }
        }
    }
    Draw::endClip(c);
}

// ------------------------------------------------------------------ musen

// Delen under punkten (fysiska px, `ctxFor`s Ctx) — remsan och ramen är
// ingen del. Träffen räknas på RADENS höjd, så hela radbandet över ett ord
// är ordet.
std::optional<MarkdownView::Hit> MarkdownView::hitAt(const Ctx& c, float mx,
                                                     float my) const
{
    if (inScrollStrip(c, mx, my))
        return std::nullopt;
    const gfx::Rectf r  = resolve(c);
    const float      lx = Draw::localX(c, r, mx);
    const float      ly = Draw::localY(c, r, my);
    if (!innerBounds(c).contains(lx, ly))
        return std::nullopt;
    const md::Layout& L = layoutAt(c, contentWidth(c, Draw::bounds(c, r).w)).layout;
    const gfx::Pointf o = origin(c);
    const float       x = lx - o.x;
    const float       y = ly - o.y;
    auto rad = std::lower_bound(L.lines.begin(), L.lines.end(), y - L.maxLineH,
                                [](const md::Line& l, float v) { return l.y < v; });
    for (; rad != L.lines.end() && rad->y <= y; ++rad) {
        if (y >= rad->y + rad->h)
            continue;
        for (std::uint32_t i = rad->first; i < rad->first + rad->count; ++i) {
            const md::Item& d = L.items[i];
            if (x >= d.x && x < d.x + d.w)
                return Hit{d.link, d.image};
        }
    }
    return std::nullopt;
}

// Remsan först (Panels — den nollar också sitt eget drag per tryck), sedan
// länken under trycket.
void MarkdownView::mouseDown(const Ctx& c, float mx, float my)
{
    const bool remsa = inScrollStrip(c, mx, my);
    Panel::mouseDown(c, mx, my);
    const std::optional<Hit> h = remsa ? std::nullopt : hitAt(c, mx, my);
    pressLink_                 = h ? h->link : -1;
}

// Hovern tänder HELA länken; en rörelse som inte byter länk ritar ingenting
// (regel 1).
void MarkdownView::mouseMove(const Ctx& c, float mx, float my)
{
    Panel::mouseMove(c, mx, my);
    const std::optional<Hit> h = hitAt(c, mx, my);
    const int                l = h ? h->link : -1;
    if (l == hoverLink_)
        return;
    hoverLink_ = l;
    invalidate();
}

// KLICKET ÄR EN KNAPPS: trycket och släppet på SAMMA länk. Url:en KOPIERAS
// före handlern — den kan byta text, och då pekar indexet in i ett annat
// dokument — och handlern anropas DIREKT under en vakt, för `fireEvent`
// kopierar sina args och `handled` hade gått förlorad. Lossar handlern vyn
// körs inget förval.
bool MarkdownView::mouseUp(const Ctx& c, float mx, float my)
{
    document();   // en ny text nollar trycket — indexet hörde till det gamla
    const int tryckt = pressLink_;
    pressLink_       = -1;
    if (tryckt < 0 || !isEnabled())
        return false;
    const std::optional<Hit> h = hitAt(c, mx, my);
    if (!h || h->link != tryckt)
        return false;
    LinkClickArgs e;
    e.url                 = document().links[static_cast<std::size_t>(tryckt)].url;
    const std::string url = e.url;
    if (!onLinkClick) {
        followLink(url);
        return false;
    }
    const HandlerGuard vakt(*this);
    onLinkClick(this, e);
    if (vakt.ok() && !e.handled)
        followLink(url);
    return true;   // en handler körde — AppWindow ritar om allt
}

bool MarkdownView::mouseCancel(gfx::MouseButton)
{
    pressLink_ = -1;
    return false;
}

// FÖRVALET: ett ankare rullar, http/https/mailto går till systemet, allt
// annat gör ingenting (`md::linkAction`).
void MarkdownView::followLink(const std::string& url)
{
    switch (md::linkAction(url)) {
    case md::LinkAction::Anchor:
        scrollToAnchor(url.substr(1));
        break;
    case md::LinkAction::Open:
        gfx::openUrl(url);
        break;
    case md::LinkAction::None:
        break;
    }
}

gfx::PointerShape MarkdownView::cursorAt(const Ctx& c, float x, float y) const
{
    const std::optional<Hit> h = hitAt(c, x, y);
    return h && h->link >= 0 ? gfx::PointerShape::Hand
                             : Widget::cursorAt(c, x, y);
}

// TOOLTIPEN PER DEL: länkens titel, annars dess adress (så man ser vart den
// går), och en bilds titel — annars vyns egen.
TrText MarkdownView::toolTipAt(const Ctx& c, float x, float y) const
{
    if (const std::optional<Hit> h = hitAt(c, x, y)) {
        const md::Document& d = document();
        if (h->link >= 0) {
            const md::Link& l = d.links[static_cast<std::size_t>(h->link)];
            return l.title.empty() ? l.url : l.title;
        }
        if (h->image >= 0) {
            const md::Image& b = d.images[static_cast<std::size_t>(h->image)];
            if (!b.title.empty())
                return b.title;
        }
    }
    return Widget::toolTipAt(c, x, y);
}

// Länken vid en LOKAL LOGISK punkt — genom fönstrets rymd och samma träff som
// musen, så snabbmenyn och klicket aldrig kan vara oense.
std::string MarkdownView::linkAt(float localX, float localY) const
{
    const std::optional<gfx::Pointf> p = toWindow(Point{localX, localY});
    if (!p)
        return {};
    const std::optional<Hit> h = hitAt(behaviorCtx(), p->x, p->y);
    if (!h || h->link < 0)
        return {};
    return document().links[static_cast<std::size_t>(h->link)].url;
}

} // namespace glaze
