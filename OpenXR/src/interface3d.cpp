#include <glaze_openxr/interface3d.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

#include "interface3d_logic.h"

// 3D-GRÄNSSNITTET — paneler av element i ritytans lager.
//
// RITNINGEN. Gränssnittet äger ETT lager. Varje bildruta som något ändrats i
// byggs formlistan om och byts med ETT `Canvas::setShapes`:
//   * bakplattan och varje elements kropp är lådor ur EN delad enhetsmesh,
//     flyttade med sin Placement (knappens intryckning är en annan Placement);
//   * framsidan är en tunn fyrhörning (en delad enhetsmesh med uv) med
//     `MeshStyle::texture` = en `glaze::RenderTexture` per element. Texturen
//     målas med `Draw` i fysiska px — framsidans färg, ikonen (standardikon
//     eller appens bild) och texten, med textSize/iconSize räknade i px ur
//     `texturePxPerMetre` — och målas om bara när innehållet, stilen,
//     `enabled` eller `toggled` ändras. Hover, nedtryckt, reglagets fyllning
//     och knopp och vredets markering är former och färger, aldrig en ny
//     målning.
//   * `RenderTexture::paint` körs lat i förberedelsen av nästa ritning som
//     binder texturen. Den ritningen är desamma för skrivbordet och för
//     ögonen (ritvägen delas), så texturen målas också när scenen bara ritas
//     i headsetet.
//
// FRAMSIDAN OCH RAMEN: se interface3d_logic.h (panelens framsida vetter mot −y).
//
// EN EGEN RITNING (`ElementDrawer`) lägger formerna i elementets ram (meter,
// origo i elementets mitt på plattans framkant, x höger, y in, z upp); här
// förs de över till världen med panelens pose och skala. Stöd: Mesh (inklusive
// låda, sfär, cylinder; meshens eget origo ska vara noll), Lines3, Area3,
// Text3, Markers3 och Points3. Andra slag lämnas ut.
//
// PEKARNA. En per hand och bildruta, i ETT läge (`PointerMode`, upplöst i
// `resolvePointer`): närläget är fingertoppen (handledens index 10) eller
// stavens spets, `tipOffset` framför aim-posen; fjärrläget är strålen ur
// aim-posen. Auto väljer närläget när spetsen är inom `wandRange` från ett
// element, en greppbar platta eller en yta i scenen. Spetsen trycker genom
// att gå in längs framsidans normal (Pressed vid 0,7 av `travel`, Released
// under 0,4, Clicked vid släpp över elementet); strålen trycker med
// avtryckaren. Squeeze eller nyp (≥ 0,8) greppar ett handtag eller en
// greppbar platta (panelen följer handen med `followGrab` och blir därefter
// en World-panel), vrider ett vred (handens rullning) eller drar ett reglage
// (pekarens läge längs spåret).
//
// PEKARNA RITAS i gränssnittets lager när `Pointers::draw`: staven som en
// tunn låda från aim-läget till spetsen och en liten sfär i spetsen, vars
// färg blandas mot elementets hover-färg efter närhet; strålen som en linje
// till träffen. Ingen stav ritas för en hand vars fingertopp används.
//
// SYNLIGHETEN (`Panel::showWhen`). Villkoren — panelens mitt inom
// `gazeDegrees` från blicken, den andra handens spets eller grepp inom
// `otherHandMetres` från mitten — styr ett mål (1 eller 0) som panelens
// opacitet följer på `fadeSeconds`. Opaciteten sätts på formernas
// `MeshStyle::opacity` (och färgernas alfa i egna ritningar); framsidornas
// texturer följer med. En panel som tonat under en tröskel tar inga tryck, och
// en panel som en hand trycker, drar eller greppar i förblir synlig.

namespace glaze::openxr {

using namespace detail;

namespace {

constexpr int    kIndexTipJoint     = 10;     // XR_HAND_JOINT_INDEX_TIP_EXT
constexpr double kPinchGrab         = 0.8;
constexpr double kHoverMargin       = 0.006;  // sidled marginal för spetsen (meter)
constexpr double kRayPressSeconds   = 0.06;   // strålens animerade intryckning
constexpr double kFaceLift          = 0.0003; // framsidan framför kroppens framkant
constexpr double kToggleHold        = 0.55;   // en påslagen vippa står så här långt inne
constexpr double kFadeInteract      = 0.3;    // opacitet under vilken en panel inte tar tryck
constexpr double kFadeSteps         = 64.0;   // opaciteten ritas i så här många steg
constexpr double kVisibleWiden      = 1.15;   // en synlig panel släcks först vid 15 % längre bort
constexpr double kWandTipFactor     = 1.8;    // spetsens radie i stavens radier
constexpr double kNearRelease       = 1.25;   // Auto: närläget släpps först vid 25 % längre bort

Hand otherHand(Hand h) { return h == Hand::Left ? Hand::Right : Hand::Left; }

bool interactive(ElementKind k)
{
    return k == ElementKind::Button || k == ElementKind::Toggle || k == ElementKind::Slider ||
           k == ElementKind::Dial;
}
bool hoverable(const Element& e)
{
    return e.enabled && e.kind != ElementKind::Label;
}

double sanitizedScale(double s)
{
    return std::isfinite(s) && s > 0.0 ? s : 1.0;
}

// ------------------------------------------------------------ DELADE MESHAR
const glaze::Mesh& unitBox()
{
    static const glaze::Mesh mesh =
        glaze::Shape::box(glaze::Box3{{-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5}}).mesh();
    return mesh;
}

// En fyrhörning i planet y = 0 av storleken 1 × 1 (x, z), framsidan mot −y,
// uv (0, 0) i övre vänstra hörnet.
const glaze::Mesh& unitQuad()
{
    static const glaze::Mesh mesh = [] {
        glaze::MeshDesc d;
        d.positions = {{-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, 0.5f}, {-0.5f, 0.0f, 0.5f}};
        d.normals   = std::vector<glaze::Float3>(4, glaze::Float3{0.0f, -1.0f, 0.0f});
        d.uvs       = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
        d.indices   = {0, 1, 2, 0, 2, 3};
        return glaze::Mesh(std::move(d));
    }();
    return mesh;
}

// ----------------------------------------------------------------- FRAMSIDAN
// Allt en målning av framsidan beror på. Två lika värden ger samma pixlar.
struct FaceSpec {
    std::string                     text;
    std::optional<glaze::StockIcon> icon;
    glaze::ImageSource              image;
    gfx::Color                      clear{};
    gfx::Color                      ink{};
    double                          textPx = 0.0;
    double                          iconPx = 0.0;
    double                          contentFraction = 1.0;   // reglage: innehållets del av bredden
    int                             width = 0, height = 0;   // texturens px
    bool                            disabled = false;
    bool                            handle   = false;        // utan innehåll: greppets prickar

    bool operator==(const FaceSpec&) const = default;
};

// Målar framsidan i ett fönsters Ctx. Ritrymden är logiska px (fysiska /
// skalan), så alla mått räknas om med `inv` och texten blir lika stor i px
// vilket fönster och vilken skala som än målar.
void paintFace(const glaze::Ctx& c, const FaceSpec& f)
{
    if (!c.theme)
        return;
    // Standardikonen färgas med temats textfärg: ge den framsidans bläck.
    glaze::Theme::Variant variant = *c.theme;
    variant.colors.text           = f.ink;
    glaze::Ctx base               = c;
    base.theme                    = &variant;

    const float inv = c.scale > 0.0f ? 1.0f / c.scale : 1.0f;
    const gfx::Rectf all = base.bounds();
    const gfx::Rectf area{all.x, all.y, all.w * static_cast<float>(f.contentFraction), all.h};
    const float textSize = static_cast<float>(f.textPx) * inv;
    const float opacity  = f.disabled ? 0.55f : 1.0f;

    glaze::ImageSource img = f.image;
    if (!img.valid() && f.icon)
        img = glaze::stockIcon(*f.icon, 48);
    const bool hasIcon = img.valid();

    glaze::Ctx tc = glaze::Draw::withFont(base, gfx::FontFamily::UI, gfx::FontStyle::Regular,
                                          glaze::px(textSize));
    std::string shown = f.text;
    if (!shown.empty()) {
        const float avail = area.w * 0.92f;
        const float width = glaze::Draw::measure(tc, shown).w;
        if (width > avail && width > 0.0f)
            tc = glaze::Draw::withFont(base, gfx::FontFamily::UI, gfx::FontStyle::Regular,
                                       glaze::px(textSize * std::max(0.55f, avail / width)));
        shown = glaze::Draw::elide(tc, shown, avail);
    }
    const bool hasText = !shown.empty();

    if (!hasIcon && !hasText) {
        if (f.handle) {
            const float side = std::min(area.w, area.h) * 0.5f;
            const float w    = area.w > area.h ? side * 1.6f : side;
            const float h    = area.w > area.h ? side : side * 1.6f;
            glaze::Draw::grip(base,
                              gfx::Rectf{area.x + (area.w - w) / 2.0f, area.y + (area.h - h) / 2.0f, w, h},
                              area.h > area.w, f.ink);
        }
        return;
    }

    // Ikonens mått: sidan ur iconSize, proportionerna ur bilden.
    float iconW = 0.0f, iconH = 0.0f;
    if (hasIcon) {
        float side = static_cast<float>(f.iconPx) * inv;
        side       = std::min(side, std::min(area.w, area.h) * 0.9f);
        float aspect = 1.0f;
        if (img.kind() != glaze::ImageSource::Kind::Stock) {
            const gfx::Sizef natural = glaze::Draw::imageSize(base, img);
            if (natural.w > 0.0f && natural.h > 0.0f)
                aspect = natural.w / natural.h;
        }
        iconW = aspect >= 1.0f ? side : side * aspect;
        iconH = aspect >= 1.0f ? side / aspect : side;
    }
    const gfx::Sizef textBox = hasText ? glaze::Draw::measure(tc, shown) : gfx::Sizef{};
    const float      gap     = textSize * 0.25f;
    const bool       beside  = area.w > area.h * 2.2f;

    if (hasIcon && hasText) {
        if (beside) {
            const float block = iconW + gap + textBox.w;
            const float x0    = area.x + (area.w - block) / 2.0f;
            glaze::Draw::glyph(base, gfx::Rectf{x0, area.y + (area.h - iconH) / 2.0f, iconW, iconH},
                               img, opacity);
            glaze::Draw::text(tc, gfx::Rectf{x0 + iconW + gap, area.y, textBox.w, area.h}, shown,
                              f.ink, glaze::Align::Left);
        } else {
            // Stapeln måste rymmas: krymp ikonen om texten tar plats.
            const float room = area.h - gap - textBox.h;
            if (iconH > room && room > 0.0f) {
                const float k = room / iconH;
                iconW *= k;
                iconH *= k;
            }
            const float block = iconH + gap + textBox.h;
            const float y0    = area.y + (area.h - block) / 2.0f;
            glaze::Draw::glyph(base, gfx::Rectf{area.x + (area.w - iconW) / 2.0f, y0, iconW, iconH},
                               img, opacity);
            glaze::Draw::text(tc, gfx::Rectf{area.x, y0 + iconH + gap, area.w, textBox.h}, shown,
                              f.ink, glaze::Align::Center);
        }
    } else if (hasIcon) {
        glaze::Draw::glyph(base,
                           gfx::Rectf{area.x + (area.w - iconW) / 2.0f,
                                      area.y + (area.h - iconH) / 2.0f, iconW, iconH},
                           img, opacity);
    } else {
        glaze::Draw::text(tc, area, shown, f.ink, glaze::Align::Center);
    }
}

// ------------------------------------------------------------ EGEN RITNING
glaze::Length scaledLength(glaze::Length l, double scale)
{
    if (l.unit == glaze::Unit::World)
        l.value = static_cast<float>(l.value * scale);
    return l;
}

// Färgens alfa gånger opaciteten.
gfx::Color fadedColor(gfx::Color c, double opacity)
{
    c.a = static_cast<std::uint8_t>(std::lround(c.a * std::clamp(opacity, 0.0, 1.0)));
    return c;
}

// Som `fadedColor`; en gradient lämnas som den är.
glaze::Paint fadedPaint(const glaze::Paint& paint, double opacity)
{
    return paint.isGradient() ? paint : glaze::Paint(fadedColor(paint.color(), opacity));
}

// Lägger `s` (given i elementets ram, meter) i världen genom `frame` (elementets
// pose i världen) och `scale`, med formens opacitet gånger `opacity`.
void appendInWorld(const glaze::Shape& s, const Pose& frame, double scale, double opacity,
                   std::vector<glaze::Shape>& out)
{
    const auto point = [&](const glaze::Point3& p) {
        const Vec3 w = transformPoint(frame, vmul(Vec3{p.x, p.y, p.z}, scale));
        return glaze::Point3{w.x, w.y, w.z};
    };
    const auto points = [&] {
        std::vector<glaze::Point3> pts;
        pts.reserve(s.points3().size());
        for (const glaze::Point3& p : s.points3())
            pts.push_back(point(p));
        return pts;
    };
    switch (s.kind()) {
    case glaze::Shape::Kind::Mesh: {
        const glaze::Placement& pl = s.placement();
        const Pose local           = fromPlacement(pl);
        const Pose full = composeScaled(frame, local, scale);
        glaze::Placement np = toPlacement(full, 1.0);
        const auto k        = static_cast<float>(scale);
        np.scale            = glaze::Float3{pl.scale.x * k, pl.scale.y * k, pl.scale.z * k};
        glaze::MeshStyle style = s.meshStyle();
        style.opacity *= static_cast<float>(opacity);
        out.push_back(glaze::Shape::mesh(s.mesh(), np, std::move(style)));
        break;
    }
    case glaze::Shape::Kind::Lines3: {
        glaze::Stroke stroke = s.stroke();
        stroke.width         = scaledLength(stroke.width, scale);
        stroke.paint         = fadedPaint(stroke.paint, opacity);
        out.push_back(glaze::Shape::polyline3(points(), std::move(stroke)));
        break;
    }
    case glaze::Shape::Kind::Area3: {
        std::optional<glaze::Stroke> stroke;
        if (s.hasStroke()) {
            stroke        = s.stroke();
            stroke->width = scaledLength(stroke->width, scale);
            stroke->paint = fadedPaint(stroke->paint, opacity);
        }
        out.push_back(glaze::Shape::polygon3(points(), fadedPaint(s.fill(), opacity), std::move(stroke)));
        break;
    }
    case glaze::Shape::Kind::Text3: {
        if (s.points3().empty())
            break;
        glaze::TextStyle style = s.textStyle();
        style.size             = scaledLength(style.size, scale);
        style.color            = fadedColor(style.color, opacity);
        const glaze::Point3 at = point(s.points3().front());
        if (s.spans().empty())
            out.push_back(glaze::Shape::text3(at, s.text(), std::move(style)));
        else
            out.push_back(glaze::Shape::text3(at, s.spans(), std::move(style)));
        break;
    }
    case glaze::Shape::Kind::Markers3: {
        if (s.points3().empty())
            break;
        glaze::MarkerStyle style = s.markerStyle();
        style.size               = scaledLength(style.size, scale);
        style.fill               = fadedColor(style.fill, opacity);
        style.outline            = fadedColor(style.outline, opacity);
        out.push_back(glaze::Shape::marker3(point(s.points3().front()), std::move(style)));
        break;
    }
    case glaze::Shape::Kind::Points3: {
        glaze::PointStyle style = s.pointStyle();
        style.size              = scaledLength(style.size, scale);
        style.color             = fadedColor(style.color, opacity);
        out.push_back(glaze::Shape::points3(points(), std::move(style)));
        break;
    }
    default:
        break;
    }
}

// ------------------------------------------------------------------ TILLSTÅND
// Det som senast ritades för ett element — jämförs mot nuet för att veta om
// panelens former måste byggas om.
struct Visual {
    int    prox    = 0;
    int    depth   = 0;
    bool   hovered = false, pressed = false, toggled = false, enabled = true;
    double value   = 0.0;
    bool operator==(const Visual&) const = default;
};

struct ElemRt {
    ElementState state;             // publicerat (state())
    Pose         frame;             // elementets pose i panelens ram (meter)
    Pose         frameInv;
    // Framsidan: målas om bara när något den visar ändrats (`texDirty`), och
    // appens egen ritning (`setDrawer`) slås upp samtidigt.
    glaze::RenderTexture tex;
    FaceSpec             spec;
    bool                 hasTex   = false;
    bool                 texDirty = true;
    const ElementDrawer* drawer   = nullptr;
    Visual               drawn;
    // Bildrutans summering ur händerna
    double               accProx = 0.0, accDepth = 0.0;
    bool                 accHover = false, accPressed = false;
    std::optional<Hand>  accBy;
    // Strålens animerade intryckning
    double               rayDepth = 0.0;
    bool                 rayHeld  = false;
};

struct PanelRt {
    Panel                panel;
    std::vector<ElemRt>  elems;
    PanelLayout          layout;
    bool                 layoutDirty = true;
    // Placeringen
    bool                 hasPose = false;
    Pose                 world, worldInv;
    double               scale      = 1.0;
    double               worldScale = 1.0;   // World-panelens skala (en släppt panel behåller sin)
    std::optional<Pose>  custom;
    Pose                 headSmooth;
    bool                 headInit = false;
    std::optional<Hand>  grabbedBy;
    // Synligheten (`Panel::showWhen`)
    double               fade       = 1.0;     // opaciteten 0 … 1
    bool                 fadeInit   = false;   // `fade` har fått sitt första värde
    bool                 condShown  = false;   // villkoren gäller (med hysteres)
    // Ritningen
    bool                 drawnShown = false;
    Pose                 drawnWorld;
    double               drawnScale = 1.0;
    int                  drawnFade  = 0;
    bool                 dirty      = true;
    std::vector<glaze::Shape> shapes;

    bool shown() const { return panel.visible && hasPose; }
    // Opaciteten i steg: det som ritas och jämförs.
    int    fadeSteps() const { return static_cast<int>(std::lround(std::clamp(fade, 0.0, 1.0) * kFadeSteps)); }
    double opacity() const { return fadeSteps() / kFadeSteps; }
    // Ritas överhuvudtaget: placerad, på och inte helt bortonad.
    bool   drawable() const { return shown() && fadeSteps() > 0; }
    // Tar tryck: synlig nog.
    bool   active() const { return shown() && fade >= kFadeInteract; }
};

// Handens indata den här bildrutan. Pekaren (spetsen eller strålen) står i
// handens `PointerState`.
struct HandIn {
    std::optional<Vec3> finger;              // pekfingrets topp, när handen spåras
    std::optional<WorldPose> aim;
    bool                trigger  = false;
    bool                grabLike = false;
    bool                focused  = false;   // headsetet har fokus (inga tryck utan)
    std::optional<Pose> grabPose;
    std::optional<Vec3> rollRight;
};

enum class Via : std::uint8_t { Poke, Trigger, Squeeze };

struct Engaged {
    std::string panel, element;
    Via         via        = Via::Poke;
    bool        angleValid = false;
    double      angleLast  = 0.0;   // vredet: rullningen förra bildrutan
    double      turned     = 0.0;   // vredet: vridningen sedan greppet
    double      value0     = 0.0;
};

struct Grab {
    std::string panel, element;
    Pose        handAtGrab, panelAtGrab;
    double      scale = 1.0;
};

struct HandRt {
    bool                  prevTrigger  = false;
    bool                  prevGrabLike = false;
    bool                  pokePressed  = false;
    bool                  pokeArmed    = true;
    std::optional<Engaged> engaged;
    std::optional<Grab>    grab;
    // Stavens spets den här bildrutan: närhet till ett mål och målets hover-färg.
    double                 wandProx  = 0.0;
    gfx::Color             wandHover{90, 150, 240};
};

// Det som behövs för att rita en hands stav; jämförs mot förra bildrutan.
struct WandDraw {
    bool       on     = false;
    bool       ray    = false;   // strålen; annars staven
    Pose       aim;
    Vec3       end;              // spetsen eller strålens slut
    double     length = 0.0;     // världsenheter
    double     scale  = 1.0;
    double     prox   = 0.0;
    gfx::Color hover{};
    bool operator==(const WandDraw&) const = default;
};

// Ett träffat mål för en hand.
struct Target {
    int     panel   = -1;
    int     element = -1;      // −1 = plattan
    bool    ray     = false;
    int     cls     = 0;       // 0 element, 1 platta (elementet vinner)
    double  distance = 0.0;
    double  lateral  = 0.0;
    TipEval tip;
    Vec3    local;             // spetsen eller strålträffen i elementets ram
};

bool better(const Target& a, const Target& b)
{
    if (a.cls != b.cls)
        return a.cls < b.cls;
    if (a.distance != b.distance)
        return a.distance < b.distance;
    return a.lateral < b.lateral;
}

void keepBetter(std::optional<Target>& best, const Target& t)
{
    if (!best || better(t, *best))
        best = t;
}

// Pekarens spets i närläget; tomt i fjärrläget och utan pekare.
std::optional<Vec3> nearTip(const PointerState& st)
{
    if (st.valid && st.mode == PointerMode::Wand)
        return st.tip;
    return std::nullopt;
}

// Pekaren är strålen ur `origin` längs `direction`.
bool isRay(const PointerState& st)
{
    return st.valid && st.mode == PointerMode::Ray;
}

} // namespace

// ================================================================== IMPL
struct Interface3D::Impl {
    glaze::Canvas&                              canvas;
    int                                         layer;
    Hand                                        primary = Hand::Right;
    Pointers                                    pointers;
    std::vector<PanelRt>                        panels;
    std::map<std::pair<std::string, std::string>, ElementDrawer> drawers;
    std::array<HandRt, 2>                       hands;
    std::vector<ElementEvent>                   events;
    Session*                                    session = nullptr;
    bool                                        layerDirty = true;   // hela lagret byggs om (först alltid)
    double                                      dt = 0.0;
    std::array<WandDraw, 2>                     wands, drawnWands;
    std::size_t                                 pointerBase = 0;   // pekarnas första form i lagret
    // Pekarna: läget handen begär, det upplösta tillståndet, Auto-lägets
    // hysteres och gränssnittets mål ur `processHand`.
    std::array<PointerMode, 2>                  modes{PointerMode::Auto, PointerMode::Auto};
    std::array<PointerState, 2>                 states{};
    std::array<bool, 2>                         nearLatch{};
    std::array<std::optional<Target>, 2>        uiTargets{};

    Impl(glaze::Canvas& c, int l) : canvas(c), layer(l) {}

    PanelRt* findPanel(const std::string& id)
    {
        for (PanelRt& p : panels)
            if (p.panel.id == id)
                return &p;
        return nullptr;
    }
    const PanelRt* findPanel(const std::string& id) const
    {
        for (const PanelRt& p : panels)
            if (p.panel.id == id)
                return &p;
        return nullptr;
    }
    static int findElement(const PanelRt& p, const std::string& id)
    {
        for (std::size_t i = 0; i < p.panel.elements.size(); ++i)
            if (p.panel.elements[i].id == id)
                return static_cast<int>(i);
        return -1;
    }

    // Grepp och tryck som pågick på panelen `id` är över (panelen byts eller tas bort).
    void dropHands(const std::string& id)
    {
        for (HandRt& hs : hands) {
            if (hs.grab && hs.grab->panel == id)
                hs.grab.reset();
            if (hs.engaged && hs.engaged->panel == id)
                hs.engaged.reset();
        }
    }

    void push(const std::string& panel, const std::string& element, ElementEventKind kind,
              Hand hand, double value = 0.0)
    {
        events.push_back(ElementEvent{panel, element, kind, hand, value});
    }

    void haptic(Hand hand, double amplitude, double seconds)
    {
        if (session && pointers.haptics)
            session->vibrate(hand, amplitude, seconds);
    }

    // --------------------------------------------------------------- LAYOUT
    void refreshLayouts()
    {
        for (PanelRt& p : panels) {
            if (!p.layoutDirty && p.elems.size() == p.panel.elements.size())
                continue;
            p.elems.resize(p.panel.elements.size());
            p.layout = layoutPanel(p.panel);
            for (std::size_t i = 0; i < p.elems.size(); ++i) {
                p.elems[i].frame    = elementFrame(p.layout.places[i], p.panel.elements[i]);
                p.elems[i].frameInv = inverse(p.elems[i].frame);
            }
            p.layoutDirty = false;
            p.dirty       = true;
        }
    }

    // ------------------------------------------------------------ PLACERING
    static std::optional<Pose> handFramePose(const FrameInfo& f, Hand hand, HandFrame frame)
    {
        const std::size_t i = handIndex(hand);
        const Controller& c = f.controllers[i];
        const TrackedPose* tp = nullptr;
        switch (frame) {
        case HandFrame::Grip: tp = &c.grip; break;
        case HandFrame::Aim:  tp = &c.aim; break;
        case HandFrame::Palm: tp = &c.palm; break;
        }
        if (tp && tp->pose)
            return toPose(*tp->pose);
        if (frame == HandFrame::Palm && c.grip.pose)
            return toPose(*c.grip.pose);
        const TrackedHand& th = f.hands[i];
        if (th.active && th.joints[0].valid)
            return toPose(th.joints[0].pose);
        return std::nullopt;
    }

    void placePanel(PanelRt& p, const FrameInfo& f)
    {
        if (p.grabbedBy) {   // greppad: `world` sätts av greppet
            p.hasPose = true;
            return;
        }
        const Mount& m = p.panel.mount;
        const double anchorScale = sanitizedScale(f.anchor.scale);
        std::optional<Vec3> headPos;
        if (f.head.pose)
            headPos = f.head.pose->position;
        p.hasPose = false;
        switch (m.attach) {
        case Attach::World: {
            Pose w = m.pose;
            if (m.facePointer && headPos)
                w = faceHead(w, *headPos);
            p.world = w;
            p.scale = p.worldScale;
            p.hasPose = true;
            break;
        }
        case Attach::Custom:
            if (p.custom) {
                p.world   = *p.custom;
                p.scale   = 1.0;
                p.hasPose = true;
            }
            break;
        case Attach::LeftHand:
        case Attach::RightHand:
        case Attach::PrimaryHand:
        case Attach::SecondaryHand: {
            Hand hand = Hand::Left;
            if (m.attach == Attach::RightHand)
                hand = Hand::Right;
            else if (m.attach == Attach::PrimaryHand)
                hand = primary;
            else if (m.attach == Attach::SecondaryHand)
                hand = otherHand(primary);
            const auto base = handFramePose(f, hand, m.handFrame);
            if (!base)
                break;
            p.world   = attachedPose(*base, m, hand == Hand::Left, anchorScale);
            p.scale   = anchorScale;
            p.hasPose = true;
            break;
        }
        case Attach::Head: {
            if (!f.head.pose)
                break;
            const Pose now = toPose(*f.head.pose);
            const double lag = std::clamp(m.headLag, 0.0, 0.999);
            if (lag > 0.0 && p.headInit)
                p.headSmooth = interpolate(p.headSmooth, now, 1.0 - lag);
            else
                p.headSmooth = now;
            p.headInit = true;
            Pose w = attachedPose(p.headSmooth, m, false, anchorScale);
            if (m.facePointer && headPos)
                w = faceHead(w, *headPos);
            p.world   = w;
            p.scale   = anchorScale;
            p.hasPose = true;
            break;
        }
        }
        if (p.hasPose)
            p.worldInv = inverse(p.world);
    }

    // ---------------------------------------------------------------- GEOMETRI
    static Vec3 panelLocal(const PanelRt& p, Vec3 world)
    {
        return vmul(transformPoint(p.worldInv, world), 1.0 / p.scale);
    }
    // En punkt i världen i elementets ram.
    static Vec3 inElement(const PanelRt& p, std::size_t ei, Vec3 world)
    {
        return transformPoint(p.elems[ei].frameInv, panelLocal(p, world));
    }
    // Strålen mot elementets framsida, i elementets ram (`t` i panelens enheter).
    static RayHit rayAtElement(const PanelRt& p, std::size_t ei, const ElementGeom& g, Vec3 origin,
                               Vec3 dir)
    {
        const Pose& inv = p.elems[ei].frameInv;
        return rayVsFace(transformPoint(inv, panelLocal(p, origin)),
                         transformDirection(inv, transformDirection(p.worldInv, dir)), -g.depth, g.width,
                         g.height);
    }
    static ElementGeom geomOf(const PanelRt& p, const Element& e)
    {
        const ElementStyle& s = effectiveStyle(p.panel, e);
        ElementGeom g;
        g.width          = s.width;
        g.height         = s.height;
        g.depth          = s.depth;
        g.travel         = s.travel;
        g.plateThickness = p.panel.style.thickness;
        return g;
    }
    static bool overPlate(const PanelRt& p, Vec3 local, double margin)
    {
        return std::abs(local.x - p.layout.plateU) <= p.layout.width / 2.0 + margin &&
               std::abs(local.z - p.layout.plateV) <= p.layout.height / 2.0 + margin;
    }

    // Handens poser och knappar ur bildrutan. Pekaren (spetsen eller strålen)
    // sätts sedan av `resolvePointer`.
    HandIn gather(Hand hand, const FrameInfo& f) const
    {
        HandIn in;
        const std::size_t i = handIndex(hand);
        const Controller& c = f.controllers[i];
        const TrackedHand& th = f.hands[i];
        if (pointers.fingertip && th.active && th.joints[kIndexTipJoint].valid)
            in.finger = th.joints[kIndexTipJoint].pose.position;
        in.aim = c.aim.pose;
        const auto button = [&](Button b) -> const ButtonState& {
            return c.buttons[static_cast<std::size_t>(b)];
        };
        const bool focused = f.focused;
        in.focused         = focused;
        in.trigger = focused && button(Button::Trigger).available && button(Button::Trigger).pressed;
        const bool squeeze = focused && button(Button::Squeeze).available && button(Button::Squeeze).pressed;
        const bool pinch   = focused && th.active && th.pinch >= kPinchGrab;
        in.grabLike = squeeze || pinch;
        in.grabPose = handFramePose(f, hand, HandFrame::Grip);
        if (c.aim.pose)
            in.rollRight = c.aim.pose->right;
        else if (th.active && th.joints[0].valid)
            in.rollRight = th.joints[0].pose.right;
        return in;
    }

    // Bär handen panelen? En hand pekar aldrig på en panel den själv bär.
    bool carries(const PanelRt& p, Hand hand) const
    {
        switch (p.panel.mount.attach) {
        case Attach::LeftHand:      return hand == Hand::Left;
        case Attach::RightHand:     return hand == Hand::Right;
        case Attach::PrimaryHand:   return hand == primary;
        case Attach::SecondaryHand: return hand != primary;
        case Attach::World:
        case Attach::Head:
        case Attach::Custom:        break;
        }
        return false;
    }

    // Bär handen en panel som syns? Då har handen ingen pekare.
    bool carriesShown(Hand hand) const
    {
        for (const PanelRt& p : panels)
            if (carries(p, hand) && p.shown() && p.fade > 0.0)
                return true;
        return false;
    }

    // Gränssnittets närmaste mål för en spets: ett element som spetsen är
    // inom `reach` framför (meter i panelens ram), eller en greppbar platta
    // med `plateMargin` i sidled. Panelerna handen bär räknas inte.
    std::optional<Target> nearestTip(Hand hand, Vec3 tip, double reach, double plateMargin) const
    {
        std::optional<Target> best;
        for (std::size_t pi = 0; pi < panels.size(); ++pi) {
            const PanelRt& p = panels[pi];
            if (!p.active() || carries(p, hand))
                continue;
            const Vec3 pl = panelLocal(p, tip);
            for (std::size_t ei = 0; ei < p.panel.elements.size(); ++ei) {
                const Element& e = p.panel.elements[ei];
                if (!hoverable(e))
                    continue;
                const Vec3 el    = transformPoint(p.elems[ei].frameInv, pl);
                const TipEval ev = evalTip(el, geomOf(p, e), reach, kHoverMargin);
                if (!ev.inRange)
                    continue;
                Target t;
                t.panel    = static_cast<int>(pi);
                t.element  = static_cast<int>(ei);
                t.distance = ev.distance;
                t.lateral  = std::hypot(el.x, el.z);
                t.tip      = ev;
                t.local    = el;
                keepBetter(best, t);
            }
            const double pen = pl.y;   // plattans framkant i y = 0
            if (p.panel.grabbable && overPlate(p, pl, plateMargin) && pen >= -reach &&
                pen <= p.panel.style.thickness + kBehindPlate) {
                Target t;
                t.panel    = static_cast<int>(pi);
                t.cls      = 1;
                t.distance = std::max(0.0, -pen);
                t.local    = pl;
                keepBetter(best, t);
            }
        }
        return best;
    }

    // Scenens första träff längs `ray` inom `maxDistance`, utan gränssnittets
    // eget lager. Utan ocklusion, så att gränssnittets former (pekaren själv,
    // en panel) aldrig skymmer scenen bakom sig; träffarna kommer närmast
    // först. Kräver en session (headsetets vy).
    std::optional<glaze::HitResult> sceneHit(Vec3 origin, Vec3 dir, double maxDistance) const
    {
        if (!session || !(maxDistance > 0.0))
            return std::nullopt;
        glaze::HitOptions options = defaultHoverOptions();
        options.occlusion         = false;
        options.maxDistance       = maxDistance;
        const glaze::Ray3 ray{{origin.x, origin.y, origin.z}, {dir.x, dir.y, dir.z}};
        for (const glaze::HitResult& h : session->castRay(ray, options))
            if (h.layer != layer)
                return h;
        return std::nullopt;
    }

    // PEKAREN: upplöser handens läge (Auto → Wand/Ray), sätter spetsen eller
    // strålen i `st` och scenens träff i `scene`. Under ett pågående tryck,
    // drag eller grepp behålls det upplösta läget, så ett tryck aldrig byter
    // pekare mitt i.
    void resolvePointer(Hand hand, const FrameInfo& f, const HandIn& in, PointerState& st,
                        std::optional<glaze::HitResult>& scene)
    {
        const std::size_t h     = handIndex(hand);
        const PointerMode prev  = st.mode;
        const bool        busy  = hands[h].engaged || hands[h].grab;
        st                      = PointerState{};
        scene.reset();
        if (modes[h] == PointerMode::None || carriesShown(hand)) {
            nearLatch[h] = false;
            return;
        }
        const double scale = sanitizedScale(f.anchor.scale);

        if (in.finger) {   // handspårningen: fingertoppen är närläget
            st.valid     = true;
            st.mode      = PointerMode::Wand;
            st.finger    = true;
            st.origin    = *in.finger;
            st.tip       = *in.finger;
            if (in.aim)
                st.direction = vnormalized(in.aim->forward);
            return;
        }
        if (!in.aim)
            return;

        const Vec3 origin = in.aim->position;
        const Vec3 dir    = vnormalized(in.aim->forward);
        const Vec3 tip    = controllerTip(*in.aim, pointers.tipOffset, scale);

        // Auto väljer läge när handen inte trycker eller greppar; annars står
        // det upplösta läget kvar.
        PointerMode mode = modes[h];
        const bool  pick = mode == PointerMode::Auto &&
                          !(busy && (prev == PointerMode::Wand || prev == PointerMode::Ray));
        if (mode == PointerMode::Auto && !pick)
            mode = prev;
        const double range = pointers.wandRange * (nearLatch[h] ? kNearRelease : 1.0);

        // Scenen framför spetsen: ETT kast, så långt som valet och staven
        // behöver; den första träffen avgör båda.
        std::optional<glaze::HitResult> ahead;
        if (pick || mode == PointerMode::Wand) {
            const double reach = std::max(pick ? range : 0.0, pointers.hoverDistance);
            ahead = sceneHit(origin, dir, (pointers.tipOffset + reach) * scale);
        }
        const auto aheadWithin = [&](double reach) {
            return ahead && ahead->distance <= (pointers.tipOffset + reach) * scale;
        };
        if (pick) {
            const bool near = nearestTip(hand, tip, range, range).has_value() || aheadWithin(range);
            mode            = near ? PointerMode::Wand : PointerMode::Ray;
        }
        if (modes[h] == PointerMode::Auto)
            nearLatch[h] = mode == PointerMode::Wand;

        st.valid     = true;
        st.mode      = mode;
        st.origin    = origin;
        st.direction = dir;
        if (mode == PointerMode::Wand) {
            st.tip = tip;
            if (aheadWithin(pointers.hoverDistance))
                scene = ahead;
        } else {
            const double length = pointers.rayLength * scale;
            st.tip = vadd(origin, vmul(dir, length));
            // Bildrutans hover gäller när dess närmaste träff är scenens: då
            // har inget i gränssnittets lager skymt den. Annars kastas
            // strålen förbi gränssnittet.
            const std::vector<glaze::HitResult>& hover = f.controllers[h].hover;
            if (!hover.empty() && hover.front().layer != layer) {
                if (hover.front().distance <= length)
                    scene = hover.front();
            } else {
                scene = sceneHit(origin, dir, length);
            }
        }
    }

    // MÅLET: närmast av gränssnittets mål (efter `processHand`) och scenens
    // träff. I närläget går gränssnittet alltid före scenen.
    void composeHit(Hand hand, PointerState& st, const std::optional<glaze::HitResult>& scene)
    {
        const std::size_t h = handIndex(hand);
        const HandRt& hs    = hands[h];
        if (!st.valid)
            return;

        std::optional<PointerHit> ui;
        const auto uiHit = [&](const PanelRt& p, const std::string& element, double distance) {
            PointerHit ph;
            ph.kind     = element.empty() ? PointerTarget::Plate : PointerTarget::Element;
            ph.panel    = p.panel.id;
            ph.element  = element;
            ph.distance = distance;
            ph.point    = st.mode == PointerMode::Ray ? vadd(st.origin, vmul(st.direction, distance)) : st.tip;
            ph.normal   = transformDirection(p.world, Vec3{0.0, -1.0, 0.0});
            return ph;
        };
        const double tipDistance = vlength(vsub(st.tip, st.origin));
        if (const std::optional<Target>& t = uiTargets[h]) {
            const PanelRt& p = panels[static_cast<std::size_t>(t->panel)];
            const std::string element =
                t->element >= 0 ? p.panel.elements[static_cast<std::size_t>(t->element)].id : std::string();
            ui = uiHit(p, element, t->ray ? t->distance : tipDistance);
        } else if (hs.engaged) {
            if (const PanelRt* p = findPanel(hs.engaged->panel))
                ui = uiHit(*p, hs.engaged->element, tipDistance);
        } else if (hs.grab) {
            if (const PanelRt* p = findPanel(hs.grab->panel))
                ui = uiHit(*p, hs.grab->element, tipDistance);
        }

        if (ui && (!scene || st.mode == PointerMode::Wand || ui->distance <= scene->distance)) {
            st.hit = *ui;
        } else if (scene) {
            st.hit.kind     = PointerTarget::Scene;
            st.hit.scene    = scene;
            st.hit.distance = scene->distance;
            st.hit.point    = scene->point ? Vec3{scene->point->x, scene->point->y, scene->point->z}
                                           : vadd(st.origin, vmul(st.direction, scene->distance));
            st.hit.normal   = Vec3{scene->normal.x, scene->normal.y, scene->normal.z};
        }
        if (st.mode == PointerMode::Ray && st.hit.kind != PointerTarget::None)
            st.tip = st.hit.point;
        st.consumed = hs.engaged.has_value() || hs.grab.has_value() ||
                      st.hit.kind == PointerTarget::Element || st.hit.kind == PointerTarget::Plate;
    }

    // Gränssnittets mål för handen: spetsen i närläget, strålen i fjärrläget —
    // bara inom `rayReach` (strålens längd, eller närmare när scenen står
    // framför).
    std::optional<Target> findTarget(Hand hand, double rayReach) const
    {
        const PointerState& ptr = states[handIndex(hand)];
        if (const std::optional<Vec3> tip = nearTip(ptr))
            return nearestTip(hand, *tip, pointers.hoverDistance, kHoverMargin);
        std::optional<Target> best;
        if (isRay(ptr)) {
            for (std::size_t pi = 0; pi < panels.size(); ++pi) {
                const PanelRt& p = panels[pi];
                if (!p.active() || carries(p, hand))
                    continue;
                for (std::size_t ei = 0; ei < p.panel.elements.size(); ++ei) {
                    const Element& e = p.panel.elements[ei];
                    if (!hoverable(e))
                        continue;
                    const RayHit r = rayAtElement(p, ei, geomOf(p, e), ptr.origin, ptr.direction);
                    if (!r.hit || !r.inside || r.t * p.scale > rayReach)
                        continue;
                    Target t;
                    t.panel    = static_cast<int>(pi);
                    t.element  = static_cast<int>(ei);
                    t.ray      = true;
                    t.distance = r.t * p.scale;
                    t.local    = r.local;
                    keepBetter(best, t);
                }
                if (p.panel.grabbable) {
                    const Vec3 o = panelLocal(p, ptr.origin);
                    const Vec3 d = transformDirection(p.worldInv, ptr.direction);
                    const Vec3 shifted{o.x - p.layout.plateU, o.y, o.z - p.layout.plateV};
                    const RayHit r = rayVsFace(shifted, d, 0.0, p.layout.width, p.layout.height);
                    if (r.hit && r.inside && r.t * p.scale <= rayReach) {
                        Target t;
                        t.panel    = static_cast<int>(pi);
                        t.ray      = true;
                        t.cls      = 1;
                        t.distance = r.t * p.scale;
                        t.local    = r.local;
                        keepBetter(best, t);
                    }
                }
            }
        }
        return best;
    }

    // ------------------------------------------------------------ VÄRDEN
    void setValue(PanelRt& p, int ei, double v, Hand hand)
    {
        Element& e = p.panel.elements[static_cast<std::size_t>(ei)];
        v = quantizeValue(v, e.min, e.max, e.step);
        if (std::abs(v - e.value) < 1e-12)
            return;
        e.value = v;
        push(p.panel.id, e.id, ElementEventKind::ValueChanged, hand, v);
    }

    std::optional<double> rollOf(const PanelRt& p, const ElemRt& rt, const HandIn& in) const
    {
        if (!in.rollRight)
            return std::nullopt;
        const Vec3 d = transformDirection(rt.frameInv, transformDirection(p.worldInv, *in.rollRight));
        if (std::hypot(d.x, d.z) < 0.25)
            return std::nullopt;
        return rollAngle(d.x, d.z);
    }

    SliderTrack trackOf(const PanelRt& p, const Element& e) const
    {
        const ElementStyle& s = effectiveStyle(p.panel, e);
        return sliderTrack(s.width, s.height, !e.content.text.empty(),
                           e.content.image.valid() || e.content.icon.has_value());
    }

    // Pekarens läge längs reglaget och vridningens uppdatering.
    void drive(PanelRt& p, int ei, Hand hand, const HandIn& in, std::optional<Vec3> local,
               Engaged& eng)
    {
        const Element& e = p.panel.elements[static_cast<std::size_t>(ei)];
        ElemRt& rt       = p.elems[static_cast<std::size_t>(ei)];
        if (e.kind == ElementKind::Slider && local) {
            setValue(p, ei, sliderValueAt(local->x, trackOf(p, e), e.min, e.max, e.step), hand);
        } else if (e.kind == ElementKind::Dial) {
            if (const auto angle = rollOf(p, rt, in)) {
                if (!eng.angleValid) {
                    eng.angleValid = true;
                    eng.angleLast  = *angle;
                    eng.turned     = 0.0;
                    eng.value0     = e.value;
                } else {
                    eng.turned += unwrapAngle(*angle - eng.angleLast);
                    eng.angleLast = *angle;
                    setValue(p, ei, dialValueFrom(eng.value0, eng.turned, e.min, e.max, e.step), hand);
                }
            }
        }
    }

    // -------------------------------------------------------------- TRYCK
    void beginPress(Hand hand, HandRt& hs, PanelRt& p, int ei, Via via, const HandIn& in,
                    std::optional<Vec3> local)
    {
        const Element& e = p.panel.elements[static_cast<std::size_t>(ei)];
        Engaged eng;
        eng.panel   = p.panel.id;
        eng.element = e.id;
        eng.via     = via;
        eng.value0  = e.value;
        push(p.panel.id, e.id, ElementEventKind::Pressed, hand);
        haptic(hand, 0.35, 0.03);
        hs.pokePressed = via == Via::Poke;
        hs.pokeArmed   = false;
        ElemRt& rt  = p.elems[static_cast<std::size_t>(ei)];
        rt.accHover = rt.accPressed = true;
        rt.accProx  = 1.0;
        rt.accBy    = hand;
        drive(p, ei, hand, in, local, eng);
        hs.engaged = std::move(eng);
    }

    void endPress(Hand hand, HandRt& hs, bool clicked)
    {
        if (!hs.engaged)
            return;
        const Engaged eng = *hs.engaged;
        hs.engaged.reset();
        hs.pokePressed = false;
        hs.pokeArmed   = false;
        PanelRt* p = findPanel(eng.panel);
        if (!p)
            return;
        const int ei = findElement(*p, eng.element);
        if (ei < 0)
            return;
        Element& e = p->panel.elements[static_cast<std::size_t>(ei)];
        push(p->panel.id, e.id, ElementEventKind::Released, hand);
        haptic(hand, 0.15, 0.02);
        if (clicked && (e.kind == ElementKind::Button || e.kind == ElementKind::Toggle)) {
            push(p->panel.id, e.id, ElementEventKind::Clicked, hand);
            if (e.kind == ElementKind::Toggle) {
                e.toggled = !e.toggled;
                p->elems[static_cast<std::size_t>(ei)].texDirty = true;
                push(p->panel.id, e.id, ElementEventKind::Toggled, hand, e.toggled ? 1.0 : 0.0);
            }
        }
    }

    // Håller ett tryck vid liv. Falskt när det tog slut.
    bool updateEngaged(Hand hand, HandRt& hs, const HandIn& in)
    {
        Engaged& eng = *hs.engaged;
        PanelRt* p   = findPanel(eng.panel);
        const int ei = p ? findElement(*p, eng.element) : -1;
        if (!p || ei < 0 || !p->shown() || !p->panel.elements[static_cast<std::size_t>(ei)].enabled) {
            endPress(hand, hs, false);
            return false;
        }
        const Element& e = p->panel.elements[static_cast<std::size_t>(ei)];
        ElemRt& rt       = p->elems[static_cast<std::size_t>(ei)];
        const ElementGeom g = geomOf(*p, e);
        const PointerState& ptr      = states[handIndex(hand)];
        const std::optional<Vec3> tip = nearTip(ptr);
        // Elementet är nedtryckt av den här handen så länge trycket håller i sig.
        const auto hold = [&] {
            rt.accHover = rt.accPressed = true;
            rt.accProx  = 1.0;
            rt.accBy    = hand;
            hs.wandProx  = 1.0;
            hs.wandHover = effectiveStyle(p->panel, e).hover;
        };

        switch (eng.via) {
        case Via::Poke: {
            if (!tip) {
                endPress(hand, hs, false);
                return false;
            }
            const Vec3 el = inElement(*p, static_cast<std::size_t>(ei), *tip);
            const TipEval ev = evalTip(el, g, pointers.hoverDistance, kHoverMargin);
            const double depth = pressDepthOf(ev.penetration, g.travel);
            rt.accDepth = std::max(rt.accDepth, depth);
            const bool through = ev.penetration > g.depth + g.plateThickness + kBehindPlate;
            if (through) {
                endPress(hand, hs, false);
                return false;
            }
            if (stepPress(hs.pokePressed, depth) == PressEdge::Released) {
                const bool inside = std::abs(el.x) <= g.width / 2.0 && std::abs(el.z) <= g.height / 2.0;
                endPress(hand, hs, inside);
                return false;
            }
            hold();
            drive(*p, ei, hand, in, el, eng);
            return true;
        }
        case Via::Trigger: {
            std::optional<Vec3> hitLocal;
            bool inside = false;
            if (isRay(ptr)) {
                const RayHit r = rayAtElement(*p, static_cast<std::size_t>(ei), g, ptr.origin, ptr.direction);
                if (r.hit) {
                    hitLocal = r.local;
                    inside   = r.inside;
                }
            }
            if (!in.trigger) {
                endPress(hand, hs, inside);
                return false;
            }
            hold();
            rt.rayHeld = true;
            drive(*p, ei, hand, in, hitLocal, eng);
            return true;
        }
        case Via::Squeeze:
            if (!in.grabLike) {
                endPress(hand, hs, false);
                return false;
            }
            hold();
            // Ett reglage dras med spetsens läge längs spåret.
            std::optional<Vec3> tipLocal;
            if (e.kind == ElementKind::Slider && tip)
                tipLocal = inElement(*p, static_cast<std::size_t>(ei), *tip);
            drive(*p, ei, hand, in, tipLocal, eng);
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------- GREPP
    void beginGrab(Hand hand, HandRt& hs, PanelRt& p, const std::string& element, const HandIn& in)
    {
        if (!in.grabPose || p.grabbedBy)
            return;
        Grab g;
        g.panel       = p.panel.id;
        g.element     = element;
        g.handAtGrab  = *in.grabPose;
        g.panelAtGrab = p.world;
        g.scale       = p.scale;
        hs.grab       = std::move(g);
        p.grabbedBy   = hand;
        push(p.panel.id, element, ElementEventKind::Grabbed, hand);
    }

    void dropGrab(Hand hand, HandRt& hs)
    {
        if (!hs.grab)
            return;
        const Grab g = *hs.grab;
        hs.grab.reset();
        PanelRt* p = findPanel(g.panel);
        if (!p || p->grabbedBy != hand)
            return;
        p->grabbedBy.reset();
        Mount& m       = p->panel.mount;
        m.attach       = Attach::World;
        m.pose         = p->world;
        m.facePointer  = false;
        p->worldScale  = g.scale;
        p->custom.reset();
        p->headInit = false;
        push(p->panel.id, g.element, ElementEventKind::Dropped, hand);
    }

    bool updateGrab(Hand hand, HandRt& hs, const HandIn& in)
    {
        PanelRt* p = findPanel(hs.grab->panel);
        if (!p || p->grabbedBy != hand) {
            hs.grab.reset();
            return false;
        }
        if (!in.grabLike || !in.grabPose) {
            dropGrab(hand, hs);
            return false;
        }
        hs.wandProx  = 1.0;
        hs.wandHover = p->panel.style.element.hover;
        p->world    = followGrab(hs.grab->handAtGrab, hs.grab->panelAtGrab, *in.grabPose);
        p->worldInv = inverse(p->world);
        p->scale    = hs.grab->scale;
        p->hasPose  = true;
        return true;
    }

    // ------------------------------------------------------------- HANDEN
    void processHand(Hand hand, const HandIn& in, double rayReach)
    {
        HandRt& hs = hands[handIndex(hand)];
        const bool grabEdge    = in.grabLike && !hs.prevGrabLike;
        const bool triggerEdge = in.trigger && !hs.prevTrigger;
        hs.prevGrabLike = in.grabLike;
        hs.prevTrigger  = in.trigger;
        hs.wandProx     = 0.0;
        std::optional<Target>& stored = uiTargets[handIndex(hand)];
        stored.reset();

        // Utan fokus (en meny i headsetet) avbryts ett tryck utan klick, ett
        // grepp släpps (greppet läses som släppt) och inget nytt börjar.
        if (!in.focused) {
            if (hs.engaged)
                endPress(hand, hs, false);
            if (hs.grab)
                updateGrab(hand, hs, in);
            hs.pokeArmed = false;
            return;
        }
        if (hs.grab && updateGrab(hand, hs, in))
            return;
        if (hs.engaged && updateEngaged(hand, hs, in))
            return;

        const std::optional<Target> target = findTarget(hand, rayReach);
        stored = target;
        if (!target) {
            hs.pokeArmed = true;
            return;
        }
        PanelRt& p = panels[static_cast<std::size_t>(target->panel)];

        if (target->element < 0) {   // plattan
            if (!target->ray && pointers.hoverDistance > 0.0) {
                hs.wandProx  = std::clamp(1.0 - target->distance / pointers.hoverDistance, 0.0, 1.0);
                hs.wandHover = p.panel.style.element.hover;
            }
            if (grabEdge && p.panel.grabbable)
                beginGrab(hand, hs, p, std::string(), in);
            return;
        }

        const int ei     = target->element;
        const Element& e = p.panel.elements[static_cast<std::size_t>(ei)];
        ElemRt& rt       = p.elems[static_cast<std::size_t>(ei)];
        const double prox = target->ray ? 1.0 : target->tip.proximity;
        if (!target->ray) {
            hs.wandProx  = prox;
            hs.wandHover = effectiveStyle(p.panel, e).hover;
        }
        if (prox >= rt.accProx) {
            rt.accProx = prox;
            rt.accBy   = hand;
        }
        rt.accHover = true;

        if (!target->ray) {
            const ElementGeom g = geomOf(p, e);
            const double depth = pressDepthOf(target->tip.penetration, g.travel);
            rt.accDepth = std::max(rt.accDepth, depth);
            if (depth < kPressOff)
                hs.pokeArmed = true;
            if (interactive(e.kind) && hs.pokeArmed && depth >= kPressOn) {
                beginPress(hand, hs, p, ei, Via::Poke, in, target->local);
                return;
            }
        } else if (triggerEdge && interactive(e.kind)) {
            beginPress(hand, hs, p, ei, Via::Trigger, in, target->local);
            return;
        }

        if (grabEdge) {
            if (e.kind == ElementKind::Dial)
                beginPress(hand, hs, p, ei, Via::Squeeze, in, std::nullopt);
            else if (e.kind == ElementKind::Slider && !target->ray)
                beginPress(hand, hs, p, ei, Via::Squeeze, in, target->local);
            else if (e.kind == ElementKind::Handle || p.panel.grabbable)
                beginGrab(hand, hs, p, e.kind == ElementKind::Handle ? e.id : std::string(), in);
        }
    }

    // ----------------------------------------------------- BILDRUTANS SLUT
    void finishStates()
    {
        for (PanelRt& p : panels) {
            for (std::size_t i = 0; i < p.elems.size() && i < p.panel.elements.size(); ++i) {
                ElemRt& rt = p.elems[i];
                const Element& e = p.panel.elements[i];
                // Strålens intryckning går mot 1 medan avtryckaren håller, annars mot 0.
                const double step = dt > 0.0 ? dt / kRayPressSeconds : 1.0;
                rt.rayDepth = rt.rayHeld ? std::min(1.0, rt.rayDepth + step)
                                         : std::max(0.0, rt.rayDepth - step);
                ElementState next;
                next.hovered   = rt.accHover && e.enabled;
                next.pressed   = rt.accPressed;
                next.proximity = next.hovered ? rt.accProx : 0.0;
                next.pressDepth = std::max(rt.accDepth, rt.rayDepth);
                if (next.hovered || next.pressed)
                    next.by = rt.accBy;
                if (next.hovered != rt.state.hovered) {
                    const Hand h = next.by ? *next.by : (rt.state.by ? *rt.state.by : Hand::Right);
                    push(p.panel.id, e.id,
                         next.hovered ? ElementEventKind::HoverEnter : ElementEventKind::HoverLeave, h);
                }
                rt.state = next;
            }
        }
    }

    void resetAccumulators()
    {
        for (PanelRt& p : panels)
            for (ElemRt& rt : p.elems) {
                rt.accProx = rt.accDepth = 0.0;
                rt.accHover = rt.accPressed = false;
                rt.accBy.reset();
                rt.rayHeld = false;
            }
    }

    // ------------------------------------------------------------- FRAMSIDAN
    // Texturens mått och innehåll för ett element ur dess stil.
    FaceSpec specFor(const PanelRt& p, const Element& e) const
    {
        const ElementStyle& st = effectiveStyle(p.panel, e);
        const bool label = e.kind == ElementKind::Label;
        const double rim = label ? 0.0 : faceRim(st);
        const double fw  = std::max(1e-4, st.width - 2.0 * rim);
        const double fh  = std::max(1e-4, st.height - 2.0 * rim);
        const double k   = textureScale(fw, fh, st.texturePxPerMetre);
        FaceSpec s;
        s.text   = e.content.text.str();
        s.icon   = e.content.icon;
        s.image  = e.content.image;
        s.width  = std::max(4, static_cast<int>(std::lround(fw * k)));
        s.height = std::max(4, static_cast<int>(std::lround(fh * k)));
        s.textPx = st.textSize * k;
        s.iconPx = st.iconSize * k;
        s.disabled = !e.enabled;
        s.ink      = s.disabled ? st.disabled : st.text;
        gfx::Color clear = st.face;
        if (e.kind == ElementKind::Toggle && e.toggled)
            clear = mixColor(clear, st.active, 0.28);
        if (s.disabled)
            clear = mixColor(clear, st.disabled, 0.25);
        s.clear = clear;
        if (e.kind == ElementKind::Slider) {
            const SliderTrack t = trackOf(p, e);
            const bool has = !s.text.empty() || s.image.valid() || s.icon.has_value();
            s.contentFraction = has ? std::clamp(t.labelWidth / fw, 0.05, 1.0) : 0.05;
        }
        s.handle = e.kind == ElementKind::Handle;
        return s;
    }

    void syncTexture(PanelRt& p, std::size_t i)
    {
        ElemRt& rt       = p.elems[i];
        const Element& e = p.panel.elements[i];
        // Texten följer också språket (`TrText`), som kan bytas utan att
        // elementet ändras.
        if (!rt.texDirty && (rt.drawer || rt.spec.text == e.content.text.str()))
            return;
        rt.texDirty = false;
        const auto it    = drawers.find({p.panel.id, e.id});
        rt.drawer        = it != drawers.end() && it->second ? &it->second : nullptr;
        if (rt.drawer) {
            rt.hasTex = false;
            return;
        }
        FaceSpec spec = specFor(p, e);
        if (rt.hasTex && rt.spec == spec)
            return;
        if (!rt.hasTex || rt.spec.width != spec.width || rt.spec.height != spec.height)
            rt.tex = glaze::RenderTexture(spec.width, spec.height);
        rt.hasTex = rt.tex.valid();
        rt.spec   = spec;
        if (rt.hasTex)
            rt.tex.paint([spec](const glaze::Ctx& c) { paintFace(c, spec); }, spec.clear);
        p.dirty = true;
    }

    // ---------------------------------------------------------------- FORMER
    Visual visualOf(const PanelRt& p, std::size_t i) const
    {
        const Element& e = p.panel.elements[i];
        const ElemRt& rt = p.elems[i];
        Visual v;
        v.prox    = static_cast<int>(std::lround(rt.state.proximity * 32.0));
        v.depth   = static_cast<int>(std::lround(rt.state.pressDepth * 64.0));
        v.hovered = rt.state.hovered;
        v.pressed = rt.state.pressed;
        v.toggled = e.toggled;
        v.enabled = e.enabled;
        v.value   = e.value;
        return v;
    }

    // En låda eller fyrhörning i en panelram (meter) → form i världen.
    static glaze::Shape placed(const PanelRt& p, const glaze::Mesh& mesh, const Pose& panelFrame,
                               Vec3 dims, glaze::MeshStyle style)
    {
        const Pose full = composeScaled(p.world, panelFrame, p.scale);
        glaze::Placement pl = toPlacement(full, 1.0);
        const auto k = static_cast<float>(p.scale);
        pl.scale = glaze::Float3{static_cast<float>(dims.x) * k, static_cast<float>(dims.y) * k,
                                 static_cast<float>(dims.z) * k};
        style.opacity *= static_cast<float>(p.opacity());
        return glaze::Shape::mesh(mesh, pl, std::move(style));
    }

    void buildElement(const PanelRt& p, std::size_t i, std::vector<glaze::Shape>& out) const
    {
        const Element& e = p.panel.elements[i];
        const ElemRt& rt = p.elems[i];
        const ElementStyle& st = effectiveStyle(p.panel, e);
        const ElementState& s  = rt.state;

        if (rt.drawer) {
            const Pose frameWorld = composeScaled(p.world, rt.frame, p.scale);
            std::vector<glaze::Shape> custom;
            const ElementDrawArgs args{p.panel, e, st, s, frameWorld, p.scale};
            (*rt.drawer)(args, custom);
            for (const glaze::Shape& shape : custom)
                appendInWorld(shape, frameWorld, p.scale, p.opacity(), out);
            return;
        }

        const auto at = [&](Vec3 centre) { return compose(rt.frame, Pose{centre, Quaternion{}}); };
        const double w = st.width, h = st.height, depth = st.depth;
        const gfx::Color white{255, 255, 255, 255};

        // Framsidans yta och texturen.
        const auto faceShape = [&](double y, double width, double height) {
            glaze::MeshStyle ms;
            ms.color   = white;
            ms.lit     = false;
            ms.fog     = false;
            ms.texture = glaze::ImageSource(rt.tex);
            out.push_back(placed(p, unitQuad(), at(Vec3{0.0, y, 0.0}), Vec3{width, 1.0, height}, ms));
        };

        if (e.kind == ElementKind::Label) {
            if (rt.hasTex)
                faceShape(-kFaceLift, w, h);
            return;
        }

        const bool pressable = e.kind == ElementKind::Button || e.kind == ElementKind::Toggle;
        double pushIn = 0.0;
        if (pressable)
            pushIn = std::max(s.pressDepth, e.kind == ElementKind::Toggle && e.toggled ? kToggleHold : 0.0) *
                   st.travel;

        gfx::Color body = st.body;
        if (!e.enabled) {
            body = mixColor(body, st.disabled, 0.6);
        } else {
            body = mixColor(body, st.hover, s.proximity * 0.65);
            double active = s.pressed ? std::max(0.6, s.pressDepth) : 0.0;
            if (e.kind == ElementKind::Toggle && e.toggled)
                active = std::max(active, 0.85);
            if (pressable)
                body = mixColor(body, st.active, active);
        }
        glaze::MeshStyle bodyStyle;
        bodyStyle.color = body;
        bodyStyle.lit   = st.lit;
        out.push_back(placed(p, unitBox(), at(Vec3{0.0, -depth / 2.0 + pushIn, 0.0}), Vec3{w, depth, h},
                             bodyStyle));

        const double rim = faceRim(st);
        const double faceY = -depth + pushIn - kFaceLift;
        if (rt.hasTex)
            faceShape(faceY, w - 2.0 * rim, h - 2.0 * rim);

        const auto accent = [&](Vec3 centre, Vec3 dims, gfx::Color color) {
            glaze::MeshStyle ms;
            ms.color = color;
            ms.lit   = st.lit;
            out.push_back(placed(p, unitBox(), at(centre), dims, ms));
        };

        if (e.kind == ElementKind::Slider) {
            const SliderTrack t = trackOf(p, e);
            const double knobX  = sliderKnobX(e.value, t, e.min, e.max);
            const double barL   = t.left - t.knobWidth / 2.0;
            const double barR   = t.right + t.knobWidth / 2.0;
            const gfx::Color barColor = e.enabled ? st.body : mixColor(st.body, st.disabled, 0.6);
            accent(Vec3{(barL + barR) / 2.0, faceY - 0.0006, 0.0},
                   Vec3{barR - barL, 0.0012, t.barHeight}, barColor);
            accent(Vec3{(barL + knobX) / 2.0, faceY - 0.0010, 0.0},
                   Vec3{std::max(1e-4, knobX - barL), 0.0016, t.barHeight * 0.999},
                   e.enabled ? st.active : st.disabled);
            gfx::Color knob = mixColor(st.active, white, s.pressed ? 0.15 : 0.55);
            knob = mixColor(knob, st.hover, s.proximity * 0.5);
            if (!e.enabled)
                knob = st.disabled;
            accent(Vec3{knobX, faceY - 0.0020, 0.0}, Vec3{t.knobWidth, 0.004, t.knobHeight}, knob);
        } else if (e.kind == ElementKind::Dial) {
            const double side   = std::min(w, h);
            const double angle  = dialMarkerAngle(e.value, e.min, e.max);
            const double radius = 0.36 * side;
            const double dot    = 0.14 * side;
            accent(Vec3{radius * std::sin(angle), faceY - 0.0015, radius * std::cos(angle)},
                   Vec3{dot, 0.003, dot}, e.enabled ? st.active : st.disabled);
        }
    }

    void rebuildShapes(PanelRt& p)
    {
        p.shapes.clear();
        if (!p.drawable())
            return;
        const PanelStyle& ps = p.panel.style;
        if (ps.backplate) {
            glaze::MeshStyle ms;
            ms.color = ps.plate;
            ms.lit   = ps.element.lit;
            const Pose plate{Vec3{p.layout.plateU, ps.thickness / 2.0, p.layout.plateV}, Quaternion{}};
            p.shapes.push_back(placed(p, unitBox(), plate,
                                      Vec3{p.layout.width, ps.thickness, p.layout.height}, ms));
        }
        for (std::size_t i = 0; i < p.panel.elements.size() && i < p.elems.size(); ++i) {
            buildElement(p, i, p.shapes);
            p.elems[i].drawn = visualOf(p, i);
        }
    }

    // ------------------------------------------------------------- SYNLIGHET
    // Handen som INTE bär panelen — för en panel utanför händerna den primära.
    Hand otherHandOf(const Panel& panel) const
    {
        switch (panel.mount.attach) {
        case Attach::LeftHand:      return Hand::Right;
        case Attach::RightHand:     return Hand::Left;
        case Attach::PrimaryHand:   return otherHand(primary);
        case Attach::SecondaryHand: return primary;
        case Attach::World:
        case Attach::Head:
        case Attach::Custom:        break;
        }
        return primary;
    }

    // Trycker, drar eller greppar någon hand något i panelen?
    bool inUse(const PanelRt& p) const
    {
        if (p.grabbedBy)
            return true;
        for (const HandRt& hs : hands)
            if ((hs.engaged && hs.engaged->panel == p.panel.id) ||
                (hs.grab && hs.grab->panel == p.panel.id))
                return true;
        return false;
    }

    // Målet för varje panels opacitet ur `showWhen`, och ett steg mot det.
    void updateVisibility(const FrameInfo& f, const std::array<HandIn, 2>& ins)
    {
        const double scale = sanitizedScale(f.anchor.scale);
        for (PanelRt& p : panels) {
            const Visibility& v = p.panel.showWhen;
            if (!(v.gazeDegrees > 0.0 || v.otherHandMetres > 0.0)) {
                p.fade     = 1.0;
                p.fadeInit = false;
                continue;
            }
            if (!p.hasPose) {
                p.fade     = 0.0;
                p.fadeInit = false;
                p.condShown = false;
                continue;
            }
            const Vec3 centre = transformPoint(
                p.world, vmul(Vec3{p.layout.plateU, p.panel.style.thickness / 2.0, p.layout.plateV}, p.scale));
            const double widen = p.condShown ? kVisibleWiden : 1.0;

            bool gazeOk = false;
            if (v.gazeDegrees > 0.0 && f.head.pose)
                gazeOk = gazeAngleDegrees(f.head.pose->position, f.head.pose->forward, centre) <=
                         v.gazeDegrees * widen;

            bool handOk = false;
            if (v.otherHandMetres > 0.0) {
                const HandIn& in   = ins[handIndex(otherHandOf(p.panel))];
                const double reach = v.otherHandMetres * widen * scale;
                if (in.finger && vlength(vsub(*in.finger, centre)) <= reach)
                    handOk = true;
                if (in.aim && vlength(vsub(in.aim->position, centre)) <= reach)
                    handOk = true;
                if (in.grabPose && vlength(vsub(in.grabPose->position, centre)) <= reach)
                    handOk = true;
            }

            p.condShown       = visibleByConditions(v, gazeOk, handOk);
            const bool target = p.condShown || inUse(p);
            if (!p.fadeInit) {
                p.fade     = target ? 1.0 : 0.0;
                p.fadeInit = true;
            } else {
                p.fade = stepFade(p.fade, target, dt, v.fadeSeconds);
            }
        }
    }

    // ------------------------------------------------------------ PEKARNA
    // Det som ska ritas per hand ur det upplösta tillståndet: staven i
    // närläget (inte för fingret — det är handen själv), strålen i fjärrläget.
    void updatePointerDraw(const FrameInfo& f, const std::array<HandIn, 2>& ins)
    {
        const double scale = sanitizedScale(f.anchor.scale);
        for (std::size_t h = 0; h < 2; ++h) {
            WandDraw w;
            const PointerState& st = states[h];
            if (pointers.draw && st.valid && !st.finger && ins[h].aim) {
                w.on     = true;
                w.ray    = st.mode == PointerMode::Ray;
                w.aim    = toPose(*ins[h].aim);
                w.end    = st.tip;
                w.length = vlength(vsub(st.tip, st.origin));
                w.scale  = scale;
                w.prox   = w.ray ? (st.hit.kind == PointerTarget::None ? 0.0 : 1.0) : hands[h].wandProx;
                w.hover  = hands[h].wandHover;
            }
            wands[h] = w;
        }
    }

    // En hands pekare som två former, alltid två (en tom form där inget
    // ritas), så att de har fasta platser i lagret. Staven: en tunn låda från
    // siktet till spetsen och en sfär i spetsen. Strålen: en tunn linje från
    // siktet till målet. Spetsens och strålens färg blandas mot målets
    // hover-färg.
    std::array<glaze::Shape, 2> pointerShapes(const WandDraw& w) const
    {
        std::array<glaze::Shape, 2> out{};
        if (!w.on || !(w.length > 0.0))
            return out;
        if (w.ray) {
            glaze::Stroke stroke;
            stroke.paint = mixColor(pointers.rayColor, w.hover, w.prox);
            stroke.width = glaze::world(static_cast<float>(2.0 * std::max(1e-4, pointers.rayRadius) * w.scale));
            out[0] = glaze::Shape::line3(glaze::Point3{w.aim.position.x, w.aim.position.y, w.aim.position.z},
                                         glaze::Point3{w.end.x, w.end.y, w.end.z}, stroke);
            return out;
        }
        const double radius = std::max(1e-4, pointers.wandRadius) * w.scale;
        const Vec3 forward  = transformDirection(Pose{Vec3{}, w.aim.orientation}, Vec3{0.0, 1.0, 0.0});

        glaze::MeshStyle shaft;
        shaft.color = pointers.wandColor;
        shaft.lit   = false;
        shaft.fog   = false;
        glaze::Placement pl = toPlacement(
            Pose{vadd(w.aim.position, vmul(forward, w.length / 2.0)), w.aim.orientation}, 1.0);
        pl.scale = glaze::Float3{static_cast<float>(2.0 * radius), static_cast<float>(w.length),
                                 static_cast<float>(2.0 * radius)};
        out[0] = glaze::Shape::mesh(unitBox(), pl, shaft);

        glaze::MeshStyle head;
        head.color = mixColor(pointers.wandTip, w.hover, w.prox);
        head.lit   = false;
        head.fog   = false;
        out[1] = glaze::Shape::sphere(glaze::Point3{w.end.x, w.end.y, w.end.z},
                                      glaze::world(static_cast<float>(radius * kWandTipFactor)), head);
        return out;
    }

    // Bygger om det som ändrats. Har en panel ändrats byts hela lagret med
    // ETT anrop; annars byts bara de pekare som ändrats, på sina platser.
    void redraw()
    {
        bool any = layerDirty;
        for (PanelRt& p : panels) {
            const bool shown = p.drawable();
            if (shown) {
                for (std::size_t i = 0; i < p.elems.size() && i < p.panel.elements.size(); ++i) {
                    syncTexture(p, i);
                    if (!(p.elems[i].drawn == visualOf(p, i)))
                        p.dirty = true;
                }
                if (!p.drawnShown || p.drawnWorld != p.world || p.drawnScale != p.scale ||
                    p.drawnFade != p.fadeSteps())
                    p.dirty = true;
            } else if (p.drawnShown) {
                p.dirty = true;
            }
            if (!p.dirty)
                continue;
            rebuildShapes(p);
            p.drawnShown = shown;
            p.drawnWorld = p.world;
            p.drawnScale = p.scale;
            p.drawnFade  = p.fadeSteps();
            p.dirty      = false;
            any          = true;
        }
        if (any) {
            std::vector<glaze::Shape> all;
            for (const PanelRt& p : panels)
                all.insert(all.end(), p.shapes.begin(), p.shapes.end());
            pointerBase = all.size();
            for (const WandDraw& w : wands)
                for (glaze::Shape& shape : pointerShapes(w))
                    all.push_back(std::move(shape));
            canvas.setShapes(layer, std::move(all));
            drawnWands = wands;
            layerDirty = false;
            return;
        }
        for (std::size_t h = 0; h < wands.size(); ++h) {
            if (wands[h] == drawnWands[h])
                continue;
            std::array<glaze::Shape, 2> shapes = pointerShapes(wands[h]);
            for (std::size_t k = 0; k < shapes.size(); ++k)
                canvas.setShape(layer, static_cast<int>(pointerBase + 2 * h + k), std::move(shapes[k]));
            drawnWands[h] = wands[h];
        }
    }

    // ------------------------------------------------------------ BILDRUTAN
    void run(const FrameInfo& f, Session* s)
    {
        session = s;
        dt      = std::clamp(f.deltaSeconds, 0.0, 0.1);
        events.clear();
        refreshLayouts();
        for (PanelRt& p : panels)
            placePanel(p, f);
        resetAccumulators();
        std::array<HandIn, 2> ins{gather(Hand::Left, f), gather(Hand::Right, f)};
        // Synligheten först, på händernas egna lägen: den avgör vilka händer
        // som bär en synlig panel och därför inte har någon pekare.
        updateVisibility(f, ins);
        std::array<std::optional<glaze::HitResult>, 2> scene;
        for (std::size_t h = 0; h < 2; ++h)
            resolvePointer(h == 0 ? Hand::Left : Hand::Right, f, ins[h], states[h], scene[h]);
        // Panelerna kan ha flyttats av en tidigare hands grepp i samma bildruta;
        // händerna tas därför i ordning, vänster först.
        const double rayLength = pointers.rayLength * sanitizedScale(f.anchor.scale);
        for (Hand hand : {Hand::Left, Hand::Right}) {
            const std::size_t h = handIndex(hand);
            processHand(hand, ins[h], scene[h] ? std::min(scene[h]->distance, rayLength) : rayLength);
        }
        for (std::size_t h = 0; h < 2; ++h)
            composeHit(h == 0 ? Hand::Left : Hand::Right, states[h], scene[h]);
        finishStates();
        // En hand som gränssnittet tar greppar inte världen i nästa bildruta.
        if (session) {
            for (Hand hand : {Hand::Left, Hand::Right})
                if (states[handIndex(hand)].consumed)
                    session->claimHand(hand);
        }
        updatePointerDraw(f, ins);
        redraw();
    }
};

// ====================================================== OFFENTLIGA GRÄNSSNITTET
Interface3D::Interface3D(glaze::Canvas& canvas, int layer) : impl_(std::make_unique<Impl>(canvas, layer)) {}

Interface3D::~Interface3D() = default;

void Interface3D::addPanel(Panel panel)
{
    setPanel(panel.id, panel);
}

void Interface3D::setPanel(const std::string& id, Panel panel)
{
    Impl& m  = *impl_;
    panel.id = id;
    PanelRt* p = m.findPanel(id);
    if (!p) {
        m.panels.emplace_back();
        p = &m.panels.back();
    } else {
        m.dropHands(id);
    }
    const std::optional<Pose> custom = p->custom;
    *p         = PanelRt{};
    p->panel   = std::move(panel);
    p->custom  = custom;
    p->elems.resize(p->panel.elements.size());
    m.layerDirty = true;
}

void Interface3D::removePanel(const std::string& id)
{
    Impl& m = *impl_;
    m.dropHands(id);
    const auto it = std::remove_if(m.panels.begin(), m.panels.end(),
                                   [&](const PanelRt& p) { return p.panel.id == id; });
    if (it == m.panels.end())
        return;
    m.panels.erase(it, m.panels.end());
    for (auto d = m.drawers.begin(); d != m.drawers.end();)
        d = d->first.first == id ? m.drawers.erase(d) : std::next(d);
    m.layerDirty = true;
}

const Panel* Interface3D::panel(const std::string& id) const
{
    const PanelRt* p = impl_->findPanel(id);
    return p ? &p->panel : nullptr;
}

void Interface3D::setPanelVisible(const std::string& id, bool visible)
{
    if (PanelRt* p = impl_->findPanel(id)) {
        p->panel.visible = visible;
        p->dirty         = true;
    }
}

void Interface3D::setPanelPose(const std::string& id, const Pose& pose)
{
    PanelRt* p = impl_->findPanel(id);
    if (!p)
        return;
    if (p->panel.mount.attach == Attach::World)
        p->panel.mount.pose = pose;
    else if (p->panel.mount.attach == Attach::Custom)
        p->custom = pose;
}

std::optional<Pose> Interface3D::panelPose(const std::string& id) const
{
    const PanelRt* p = impl_->findPanel(id);
    if (!p)
        return std::nullopt;
    if (p->hasPose)
        return p->world;
    if (p->panel.mount.attach == Attach::World)
        return p->panel.mount.pose;
    return std::nullopt;
}

void Interface3D::setElement(const std::string& panel, Element element)
{
    PanelRt* p = impl_->findPanel(panel);
    if (!p)
        return;
    const int i = Impl::findElement(*p, element.id);
    if (i < 0) {
        p->panel.elements.push_back(std::move(element));
        p->elems.emplace_back();
        p->layoutDirty = true;
    } else {
        // Värdet, texten eller vippan ritar bara om; slag, stil, cell och pose
        // lägger också ut panelen på nytt.
        Element& old = p->panel.elements[static_cast<std::size_t>(i)];
        if (old.kind != element.kind || old.style != element.style || old.row != element.row ||
            old.column != element.column || old.rowSpan != element.rowSpan ||
            old.columnSpan != element.columnSpan || old.pose != element.pose)
            p->layoutDirty = true;
        old = std::move(element);
        p->elems[static_cast<std::size_t>(i)].texDirty = true;
    }
    p->dirty = true;
}

const Element* Interface3D::element(const std::string& panel, const std::string& id) const
{
    const PanelRt* p = impl_->findPanel(panel);
    if (!p)
        return nullptr;
    const int i = Impl::findElement(*p, id);
    return i < 0 ? nullptr : &p->panel.elements[static_cast<std::size_t>(i)];
}

ElementState Interface3D::state(const std::string& panel, const std::string& id) const
{
    const PanelRt* p = impl_->findPanel(panel);
    if (!p)
        return {};
    const int i = Impl::findElement(*p, id);
    if (i < 0 || static_cast<std::size_t>(i) >= p->elems.size())
        return {};
    return p->elems[static_cast<std::size_t>(i)].state;
}

void Interface3D::setPrimaryHand(Hand hand)
{
    impl_->primary = hand;
}

Hand Interface3D::primaryHand() const
{
    return impl_->primary;
}

void Interface3D::setPointerMode(Hand hand, PointerMode mode)
{
    impl_->modes[handIndex(hand)] = mode;
    impl_->layerDirty = true;
}

PointerMode Interface3D::pointerMode(Hand hand) const
{
    return impl_->modes[handIndex(hand)];
}

const PointerState& Interface3D::pointer(Hand hand) const
{
    return impl_->states[handIndex(hand)];
}

void Interface3D::setPointers(const Pointers& pointers)
{
    impl_->pointers   = pointers;
    impl_->layerDirty = true;
}

void Interface3D::setDrawer(const std::string& panel, const std::string& element, ElementDrawer drawer)
{
    Impl& m = *impl_;
    const auto key = std::make_pair(panel, element);
    if (drawer)
        m.drawers[key] = std::move(drawer);
    else
        m.drawers.erase(key);
    if (PanelRt* p = m.findPanel(panel)) {
        p->dirty = true;
        const int i = Impl::findElement(*p, element);
        if (i >= 0 && static_cast<std::size_t>(i) < p->elems.size()) {
            p->elems[static_cast<std::size_t>(i)].texDirty = true;
        }
    }
}

void Interface3D::update(const FrameInfo& frame, Session* session)
{
    impl_->run(frame, session);
    if (impl_->events.empty())
        return;
    // Händelserna fyras sist: en callback får ändra paneler och element.
    std::vector<ElementEvent> events = std::move(impl_->events);
    impl_->events.clear();
    if (!onElement)
        return;
    for (const ElementEvent& e : events)
        onElement(e);
}

bool Interface3D::consumes(Hand hand) const
{
    return pointer(hand).consumed;
}

} // namespace glaze::openxr
