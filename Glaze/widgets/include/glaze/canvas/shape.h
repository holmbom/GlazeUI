#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gfx/color.h>
#include <gfx/geom2d.h>   // gfx::GradientStop
#include <gfx/geom3d.h>   // gfx::kCylinderSides, gfx::kTubeSides
#include <gfx/scene.h>    // gfx::PaintKind
#include <gfx/sdf.h>      // TextAnchor, TextRotation, MarkerShape
#include <gfx/text.h>     // FontFamily, fontsize

#include "glaze/canvas/geometry.h"
#include "glaze/canvas/image_source.h"   // ImageSource (+ Bitmap, RenderTexture)
#include "glaze/canvas/mesh.h"           // Mesh, PointCloud
#include "glaze/canvas/path.h"
#include "glaze/canvas/style3d.h"        // MeshStyle, Placement, Instance, PointStyle
#include "glaze/draw.h"       // glaze::Align — radernas justering
#include "glaze/language.h"   // TrText

// RITYTANS FORMER — EN innehållsmodell. Formlistan i
// `.glazeui` och lagrens data är samma sak: `Canvas::addShape(lager, form)`
// följer kollektionsvokabulären (hård regel 8), och `Painter::draw(form)`
// ritar samma form omedelbart i `onPaint`.
//
//     karta.addShape(vagar, glaze::Shape::polyline(
//         {{674000, 6580000}, {674150, 6580060}},
//         {.paint = gfx::Color{210, 70, 40}, .width = glaze::mm(0.8)}));
//     karta.addShape(glaze::Shape::text({674150, 6580060}, "Storgatan",
//         {.size = glaze::mm(2.2), .halo = gfx::Color{255, 255, 255}}));
//
// En form är ett VÄRDE: den kopieras in i lagret, och lagret bygger sin
// geometri ur kopian. Att ändra en form i ett lager är `setShape(i, ny)`.
//
// FÄRGERNA är appens data och följer INTE temat — en standardfärg är svart,
// som en penna (QPainter, HTML canvas). Ytan bakom är ritytans fält-yta, och
// den följer temat.

namespace glaze {

// ------------------------------------------------------------------ FÄRGEN

// Ett stopp i en gradient: läget 0–1 och färgen (rak alfa). Samma typ som
// gfx-fasadens rampbyggare tar.
using GradientStop = gfx::GradientStop;

// EN FÄRG ELLER EN GRADIENT (räknad i GPU:n). En gradient ligger FAST I
// VÄRLDEN — den följer zoom, vridning och tilt — och spridningen är PAD:
// bortom ändarna gäller ändfärgerna.
//
//     glaze::Paint röd = gfx::Color{200, 0, 0};                 // implicit
//     auto lin = glaze::Paint::linear({0, 0}, {100, 0},
//                                     {{0, gfx::Color{255, 0, 0}},
//                                      {1, gfx::Color{0, 0, 255}}});
//
// Standardfärgen är opak svart. Varje gradientform ritas med ett eget
// rit-anrop (gradienten är anropets uniform), medan enfärgade former delar
// anrop — tusen enfärgade vägar är ETT anrop.
class Paint {
public:
    Paint() = default;
    Paint(gfx::Color c) : color_(c) {}   // NOLINT — implicit med flit

    static Paint linear(Point from, Point to, std::vector<GradientStop> stops);
    static Paint radial(Point center, double r0, double r1,
                        std::vector<GradientStop> stops);

    gfx::PaintKind kind() const { return kind_; }
    bool           isGradient() const { return kind_ != gfx::PaintKind::Solid; }
    gfx::Color     color() const { return color_; }   // Solid
    Point          from() const { return a_; }        // Linear: start, Radial: mitt
    Point          to() const { return b_; }          // Linear: slut
    double         r0() const { return r0_; }         // Radial
    double         r1() const { return r1_; }
    const std::vector<GradientStop>& stops() const { return stops_; }

    // Ritar färgen något alls? Enfärgad med alfa 0 är "ingen färg"; en
    // gradient räknas som synlig om något stopp har alfa.
    bool visible() const;

    bool operator==(const Paint& o) const;

private:
    gfx::PaintKind            kind_  = gfx::PaintKind::Solid;
    gfx::Color                color_ = gfx::Color{0, 0, 0, 255};
    Point                     a_{}, b_{};
    double                    r0_ = 0.0, r1_ = 0.0;
    std::vector<GradientStop> stops_;
};

// ----------------------------------------------------------------- KONTUREN

// Ändarna: raka (vid punkten), runda (en halvskiva) eller fyrkantiga
// (förlängda halva bredden). Skarvarna: runda, spetsiga eller avfasade.
enum class LineCap : std::uint8_t { Butt, Round, Square };
enum class LineJoin : std::uint8_t { Round, Miter, Bevel };

// HUR EN LINJE RITAS. Bredden är ett mått med enhet: px och mm är konstanta
// på SKÄRMEN vid varje zoom, vridning och tilt (extruderade efter
// projektionen); world följer zoomen. Skarvarna och ändarna räknas i
// samma rymd som bredden — en spetsig skarv är spetsig på skärmen också
// under tilt.
//
//     glaze::Stroke{.paint = gfx::Color{200, 60, 40}, .width = glaze::mm(0.7),
//                   .join = glaze::LineJoin::Miter, .dash = {4, 2},
//                   .dashUnit = glaze::Unit::Mm}
//
// SPETSGRÄNSEN (`miterLimit`, SVG:s `stroke-miterlimit`): en spetsig skarv
// vars spets blir längre än gränsen × bredden blir avfasad. Förval 4, som
// SVG; ett värde under 1 (eller inte ändligt) ritas med förvalet.
//
// STRECKEN (`dash`): längderna på, av, på, av … i `dashUnit` — px och mm
// mäts på SKÄRMEN (mönstret står still i px när kartan zoomas), world
// följer zoomen. `dashOffset` flyttar mönstrets början, i samma enhet. Tomt
// = heldragen; ett udda antal upprepas (SVG), och ett negativt värde eller
// summan 0 ritas heldraget. Högst 8 längder efter upprepningen (loggas
// annars, och linjen blir heldragen). Strecken har linjens ändform — runda
// ändar och `{0, 6}` ger prickar. SKRIVEN BEGRÄNSNING: under tilt förkortas
// strecken med djupet (mönstret mäts i kamerans skala i mitten).
//
// INSIDAN TRÄFFAS (`hitInside`): konturens insida räknas som YTA i
// `Canvas::hitTest` — också utan fyllning, med en genomskinlig och med en
// osynlig kontur (alfa 0 eller bredden 0), så en ram kan väljas inifrån och
// en helt osynlig yta vara en träffzon (SVG:s `pointer-events: fill`).
// Insidan är den en fyllning hade fått: ringarna jämn–udda, och en ÖPPEN
// kontur sluten mellan sina ändar. Gäller ytorna och 2D-linjerna; ritningen
// rör den inte.
struct Stroke {
    Paint              paint{gfx::Color{0, 0, 0, 255}};
    Length             width = px(1.0f);
    LineCap            cap   = LineCap::Butt;
    LineJoin           join  = LineJoin::Round;
    float              miterLimit = 4.0f;
    std::vector<float> dash;
    Unit               dashUnit   = Unit::Px;
    float              dashOffset = 0.0f;
    bool               hitInside  = false;
};

// -------------------------------------------------------------------- TEXTEN

using TextAnchor = gfx::TextAnchor;
// Vad etikettens vinkel mäts mot: SKÄRMEN (Viewport — står still när
// kartan vrids) eller KARTAN (Map — följer vägen när kartan vrids); i båda
// står etiketten UPPRÄTT. GROUND LÄGGER den i markplanet
// vid ankaret: vinkeln är kartans, texten förkortas under tilt som allt annat
// på marken, och storleken i px eller mm gäller vid ankaret (vinkelrätt mot
// blicken). En liggande etikett kolliderar med sin projicerade ruta; på
// terräng DRAPERAS den (style3d.h `LayerPlacement::Terrain`) och kolliderar
// inte — den är en del av marken, som en vägmarkering.
using Alignment = gfx::TextRotation;

// EN DEL AV EN ETIKETT — en etikett med flera stilar:
// delarna läggs ut efter varandra på samma rader, i samma storlek och med
// samma gloria, platta och ankare (etikettens `TextStyle`), men var och en
// med sin egen text, färg och fet/kursiv. Ett OSATT fält ärver etikettens.
// Varje del bär sin egen `TrText`, så en översatt del följer språket.
//
//     karta.addShape(glaze::Shape::text({674060, 6580030},
//         {glaze::TextSpan{.text = "Storgatan "},
//          {.text = "12", .color = gfx::Color{200, 0, 0}, .bold = true}},
//         {.size = glaze::mm(2.5)}));
//
// Fältordningen är kontrakt (designerade initierare). FALLGROP: en lista med
// EN bar del `{{"abc"}}` (och `{}`) är tvetydig mot strängformen och
// kompilerar inte — skriv `glaze::TextSpan{"abc"}` uttryckligen.
//
// BAKGRUNDEN OCH TABBSTOPPET har inget att ärva — osatt är ingen.
// En bakgrund är en ruta bakom delen på varje rad den står på, radhöjden hög
// (följande delar i samma färg blir EN ruta). Tabbstoppet är x där delen
// BÖRJAR, mätt från RADENS början (med `Align::Left` textens vänsterkant;
// Center och Right justerar raden med luckan) och räknat om till storlekens
// enhet som stilens mått. Står det som kommer före redan längre fram börjar
// delen direkt efter det, och en del efter en radbrytning börjar vid stoppet
// — ett indrag. Kolumner med färgade fält:
//
//     {glaze::TextSpan{.text = "Stolpe"},
//      {.text = "12,4 m", .background = gfx::Color{255, 230, 150},
//       .tabStop = glaze::mm(20)}}
//
// SKRIVEN GRÄNS: två bakgrunder kant i kant (två färger, eller två rader)
// kantutjämnas var för sig, så fogen mellan dem blir en aning genomskinlig.
struct TextSpan {
    TrText                    text;
    std::optional<gfx::Color> color;        // osatt = TextStyle::color
    std::optional<bool>       bold;         // osatt = TextStyle::bold
    std::optional<bool>       italic;       // osatt = TextStyle::italic
    std::optional<gfx::Color> background;   // osatt = ingen
    std::optional<Length>     tabStop;      // osatt = direkt efter föregående
};

// ETIKETTENS UTSEENDE. Måtten är `Length` och får blandas — en text i mm med
// en platta vars luft är i px räknas om till storlekens enhet när etiketten
// byggs (px och mm exakt ur skärmens täthet; världsenheter mot zoomen när
// den byggdes). Sist står TAKET (radantal och höjd, med "…"), GOLVET OCH
// TAKET för storleken på skärmen och SYNLIGHETEN EFTER ZOOM, och
// efter dem plattans KONTUR och RADAVSTÅNDET.
struct TextStyle {
    Length          size   = px(gfx::fontsize::kUi);
    gfx::FontFamily family = gfx::FontFamily::UI;
    bool            bold   = false;   // fet — riktig fil, annars syntetisk
    bool            italic = false;   // kursiv — riktig fil, annars skjuven
    gfx::Color      color{0, 0, 0, 255};
    gfx::Color      halo{0, 0, 0, 0};          // glorians färg; alfa 0 = ingen
    Length          haloWidth = px(1.5f);      // glorians bredd när den finns
    gfx::Color      background{0, 0, 0, 0};    // plattan bakom; alfa 0 = ingen
    Length          padding;                   // plattans luft runt texten
    TextAnchor      anchor   = TextAnchor::Center;
    float           angle    = 0.0f;           // grader, moturs
    Alignment       rotation = Alignment::Viewport;
    Length          offsetX, offsetY;          // i textens egen ram, y nedåt
    Length          wrapWidth;                 // radbrytning; 0 = bara '\n'
    Align           align = Align::Center;     // radernas justering i blocket
    // ETIKETTKOLLISIONEN: en etikett vars låda krockar med en
    // viktigare döljs. `priority` — högre vinner (vid lika vinner lagret som
    // ritas överst, sedan den som kom först); `collisionPadding` — luft runt
    // lådan; `collide = false` — alltid synlig, och tar ingen plats.
    bool            collide  = true;
    int             priority = 0;
    Length          collisionPadding;
    // TAKET: högst så många rader och högst så hög (textblocket, utan
    // plattans luft); 0 = inget tak, minst en rad. Det som inte ryms kortas med
    // "…" efter Draw::elideLines regel — i den sista synliga delens färg och stil.
    int             maxLines = 0;
    Length          maxHeight;
    // GOLVET OCH TAKET: en em på SKÄRMEN hålls i [minSize, maxSize]
    // (0 = ingen gräns; taket vinner). Hela etiketten följer, vid varje
    // kameraändring utan ombyggnad; kollisionen och träffprovet räknar med den.
    // FALLGROP: radantalet avgörs när etiketten byggs, så en världstext vid
    // golvet blir större än sin ruta i världen.
    Length          minSize;
    Length          maxSize;
    // SYNLIGHETEN EFTER ZOOM: etiketten syns bara när ritytans zoom
    // ligger i [minZoom, maxZoom] (setLayerZoomRange:s regel); dold tar den
    // ingen plats och träffas inte, med setLabelFade tonar den.
    double          minZoom = 0.0;
    double          maxZoom = 0.0;
    // KONTUREN RUNT PLATTAN: ett band `outlineWidth` brett UTANFÖR
    // plattan (`background` + `padding`), som MarkerStyle:s kontur — också
    // utan bakgrund, då är den en ram. Lådan växer med den, så kollisionen och
    // träffprovet räknar med den. Alfa 0 = ingen.
    gfx::Color      outline{0, 0, 0, 0};
    Length          outlineWidth = px(1.0f);
    // RADAVSTÅNDET: en baslinje till nästa är radhöjden × faktorn,
    // och textblocket är en radhöjd plus ett radavstånd per rad efter den
    // första — radbrytningen, taket (`maxHeight`) och mätningen räknar med
    // det. 1 är fontens eget; ett värde ≤ 0 (eller som inte är ändligt) är 1.
    float           lineSpacing = 1.0f;
};

// TEXTENS MÅTT (`Canvas::measureText`): textblocket som en etikett
// lägger ut det — samma rader ('\n' och `wrapWidth`), radhöjd och delar —
// utan platta, luft, ankare, förskjutning och vinkel, i den enhet man bad
// om. En tom text är EN rad; en storlek ≤ 0 mäter noll med `lines` 0. TAKET
// räknas med (`lines` är de synliga raderna), och golvet och taket för
// storleken vid kamerans mitt; zoomintervallet inte — måttet säger hur
// texten ser ut, inte om den syns.
struct TextMetrics {
    double width      = 0.0;   // den bredaste raden, tabbstoppen inräknade
    double height     = 0.0;   // en radhöjd + radavståndet per rad efter den
    double lineHeight = 0.0;   // en rad — den största bland delarnas stilar
    double baseline   = 0.0;   // första radens baslinje under blockets överkant
    int    lines      = 0;
    bool   truncated  = false; // kortades av maxLines/maxHeight
};

// ------------------------------------------------------------------- BILDEN
//
// `ImageSource` — var en bild kommer ifrån (assets, pixlar, en målning) — står
// i glaze/canvas/image_source.h, eftersom också rymdens
// stilar bär den (`MeshStyle::texture`).

// HUR EN BILD RITAS (`Shape::image`, `Painter::drawImage`).
//
//     karta.addShape(foto, glaze::Shape::image(ruta, glaze::img("orto", "1"),
//         {.opacity = 0.8f, .saturation = 0, .contrast = 1.3f}));
//
// `opacity` (0–1) är bildens egen och ritas GÅNGER lagrets
// (`Canvas::setLayerOpacity`) — en höjdmodell på 0,6 i ett lager på 0,5 syns
// med 0,3, också draperad på terrängen.
//
// FÄRGJUSTERINGEN verkar på bildens RAKA färg och ändras när som
// helst utan att pixlarna räknas om — en ny form med `setShape` bygger om fyra
// hörn, och texturen står kvar på GPU:n:
//   saturation  mättnaden mot luminansen (Rec. 709): 0 GRÅSKALA, 1 orört,
//               över 1 mättare
//   contrast    kontrasten kring mittgrått: 0 grått, 1 orört, över 1 skarpare
//   brightness  ljusheten — färgen gånger talet: 0 svart, 1 orört, över 1
//               ljusare
// räknade i ordningen ljushet, kontrast, mättnad och klampade till 0–1 efter
// varje steg. Ett tal under 0 räknas som 0 och ett som inte är ändligt som 1;
// förvalen ritar bilden exakt som den är. Samma namn och samma betydelse som
// `MeshStyle`s (style3d.h) — och samma inbördes ordning, så de designerade
// initierarna skrivs likadant i båda. Fältordningen är kontrakt.
struct ImageStyle {
    float opacity    = 1.0f;
    float saturation = 1.0f;
    float contrast   = 1.0f;
    float brightness = 1.0f;
};

// ------------------------------------------------------------------ MARKÖREN

using MarkerShape = gfx::MarkerShape;

// En MARKÖR — en skärmkonstant cirkel eller kvadrat (till exempel 6 px)
// eller en BILD (en kartnål). Tusentals går: varje markör är fyra vertex i
// samma anrop. Markören står upprätt i skärmen (billboard) i sin storlek —
// px och mm konstanta vid varje zoom, vridning och tilt.
//
//     karta.addShape(glaze::Shape::marker({674100, 6580050},
//         {.size = glaze::mm(8), .anchor = glaze::TextAnchor::Bottom,
//          .img = glaze::img("nålar", "röd")}));
//
// ANKARET säger var punkten sitter på markören (textens nio lägen): Center
// mitt i, Bottom mitt på underkanten — nålens spets på punkten.
//
// BILDMARKÖREN (`img` satt): bilden i en ruta `size` × `size` med BILDENS
// proportioner (den längre sidan är `size`); form, fyllning och kontur gäller
// inte. Bilden samplas linjärt, som texten, och har ingen egen kantutjämning
// utöver sin alfa. Markörer med olika bilder ritas i var sitt anrop —
// samma bild (ett handtag, eller kopior av samma Bitmap) delar.
struct MarkerStyle {
    MarkerShape shape = MarkerShape::Circle;
    Length      size  = px(6.0f);                // fyllningens diameter/sida
    gfx::Color  fill{0, 0, 0, 255};
    gfx::Color  outline{0, 0, 0, 0};             // konturens färg; alfa 0 = ingen
    Length      outlineWidth = px(1.0f);         // utanför fyllningen
    float       angle    = 0.0f;                 // vridningen, grader
    Alignment   rotation = Alignment::Viewport;
    TextAnchor  anchor   = TextAnchor::Center;
    ImageSource img;                             // en bild i stället för formen
    // Kollisionen (E9) som TextStyle:s — men AV från start: en markör är
    // en punkt i data och inte en etikett, och tusentals punkter ska inte
    // gallra varandra om appen inte ber om det.
    bool        collide  = false;
    int         priority = 0;
    Length      collisionPadding;
    // GOLVET OCH TAKET och SYNLIGHETEN EFTER ZOOM som TextStyle:s.
    Length      minSize;
    Length      maxSize;
    double      minZoom = 0.0;
    double      maxZoom = 0.0;
};

// -------------------------------------------------------------------- FORMEN

namespace detail {
struct InstanceSet;   // src/canvas/instances.h — instansernas celler
}

// 3D-DELEN av en form i rymden. Bara 3D-formerna bär
// den, delad och oföränderlig bakom en `shared_ptr`, så att 2D-formerna inte
// växer. Vilka fält som gäller avgör formens slag (`Shape::Kind`).
struct Shape3DData {
    // Linjens och ytans punkter, textens och markörens ankare som `[0]`.
    std::vector<Point3> points;
    // MESHEN: meshen, var den står och hur den ritas — också för
    // primitiverna och för polygon3:s yta; för instanserna är det PROTOTYPEN.
    Mesh      mesh;
    Placement placement;
    MeshStyle meshStyle;
    // INSTANSERNA och deras celler — byggda en gång i fabriken.
    std::vector<Instance>                      instances;
    std::shared_ptr<const detail::InstanceSet> instanceSet;
    // PUNKTERNA.
    PointCloud cloud;
    PointStyle pointStyle;
    // HELSKÄRMSPASSET: programmets namn och stilen.
    std::string passProgram;
    PassStyle   passStyle;
};

class Shape {
public:
    // Vad formen ritar — och därmed vilka program den går till.
    enum class Kind : std::uint8_t {
        None,      // tom form, ritar ingenting
        Lines,     // en eller flera konturer (Line)
        Area,      // ringar med fyllning och valfri kontur (Fill + Line)
        Text,      // en etikett (Glyph)
        Markers,   // en eller flera markörer (Glyph)
        Image,     // en bild på en världsrektangel (Image)
        // I RYMDEN — punkterna i `points3()`.
        Lines3,    // en polylinje (Line3)
        Text3,     // en etikett vid ett ankare med höjd (Glyph3)
        Markers3,  // en markör vid ett ankare med höjd (Glyph3)
        // FÖREMÅLEN I RYMDEN — Mesh- och
        // Points-programmen (src/canvas/programs3d.h).
        Mesh,      // en mesh med placering — också box/sphere/cylinder/tube
        Area3,     // en plan yta i rymden med valfri kontur (Mesh + Line3)
        Instances, // en prototyp på många platser (Mesh, instansierad)
        Points3,   // punkter i rymden (Points)
        // HELSKÄRMSPASSET — ett eget program över hela
        // ritytan, först i bildrutan.
        Pass,
    };

    Shape() = default;

    // ---- LINJERNA (Stroke) — konturer i världen.
    static Shape line(Point a, Point b, Stroke stroke = {});
    static Shape polyline(std::vector<Point> points, Stroke stroke = {});
    // En CIRKELBÅGE: mitten, radien (världsenheter), startvinkeln och
    // svepet i grader (moturs med y uppåt, som matematiken).
    static Shape arc(Point center, double r, double startDeg, double sweepDeg,
                     Stroke stroke = {});

    // ---- YTORNA (Paint + valfri Stroke) — trianguleras EN gång i världen,
    // kantutjämnade med en remsa på en px (T3).
    static Shape polygon(std::vector<Point> outer, Paint fill,
                         std::optional<Stroke> stroke = std::nullopt);
    // Ringar: den första är ytterringen, resten HÅL. Orienteringen är fri.
    static Shape polygon(std::vector<std::vector<Point>> rings, Paint fill,
                         std::optional<Stroke> stroke = std::nullopt);
    static Shape rect(Rect r, Paint fill,
                      std::optional<Stroke> stroke = std::nullopt);
    static Shape ellipse(Point center, double rx, double ry, Paint fill,
                         std::optional<Stroke> stroke = std::nullopt);
    static Shape circle(Point center, double r, Paint fill,
                        std::optional<Stroke> stroke = std::nullopt);
    // En väg som yta (första delvägen ytterring, resten hål) eller som
    // KONTUR (bara Stroke — varje delväg för sig).
    static Shape path(const Path& p, Paint fill,
                      std::optional<Stroke> stroke = std::nullopt);
    static Shape path(const Path& p, Stroke stroke);

    // ---- TEXTEN och MARKÖRERNA — upprätta i skärmen (billboards, T4).
    static Shape text(Point at, TrText text, TextStyle style = {});
    // En etikett i DELAR med var sin färg och fet/kursiv (`TextSpan`) — i EN
    // atlas och ett rit-anrop; radbrytningen går över delarna, och radhöjden
    // är den största bland stilarna som används.
    static Shape text(Point at, std::vector<TextSpan> spans, TextStyle style = {});
    static Shape marker(Point at, MarkerStyle style = {});
    // ETT PUNKTMOLN är EN form (T6): en markör per punkt, ett anrop.
    static Shape points(std::vector<Point> points, MarkerStyle style = {});

    // ---- BILDEN — en världsrektangel, perspektivkorrekt under tilt. Bilden
    // står RÄTTVÄND: med y uppåt (en karta) är rektangelns översta kant den
    // med störst y, utan är den det minsta (widgetens lokala px). Opaciteten
    // och färgjusteringen i `style` (`ImageStyle` ovan).
    static Shape image(Rect r, ImageSource source, ImageStyle style = {});
    // BILDEN PÅ FYRA FRIA HÖRN — en georefererad
    // plåt, ett snett flygfoto: bildens övre vänstra, övre högra, nedre högra
    // och nedre vänstra hörn i världen, i RINGORDNING (spegelvänt går också).
    // Bilden avbildas PROJEKTIVT på fyrhörningen — raka linjer i bilden förblir
    // raka, utan knäck längs diagonalen — och perspektivkorrekt under tilt.
    // Fyrhörningen måste vara STRIKT KONVEX: tre hörn i linje, en fluga, en
    // inbuktning eller ett hörn som inte är ändligt ritar ingenting, träffas
    // inte och loggas en gång per process (som en ogiltig bild). Fyra `Point`
    // och inte en `std::array`: `{x, y, b, h}` initierar också en array med
    // fyra punkter, och rektangelns rader hade blivit tvetydiga.
    static Shape image(Point topLeft, Point topRight, Point bottomRight,
                       Point bottomLeft, ImageSource source,
                       ImageStyle style = {});

    // ---- I RYMDEN — punkter med höjd. Suffixet 3: en
    // klammerlista `{x, y}` initierar också en `Point3`, så en överlagring
    // hade gjort befintlig kod tvetydig.
    //
    // LINJEN i rymden (Line3) har samma `Stroke` som 2D-linjen: px och mm är
    // konstanta på skärmen, en bredd i VÄRLDSENHETER är bredden vid varje
    // punkt (avsmalnande med avståndet) — alltid ett band MOT KAMERAN, för en
    // linje i rymden ligger inte i något plan. Streck i världsenheter mäts
    // längs 3D-längden. TEXTEN och MARKÖREN står upprätta i skärmen vid
    // ankarets projektion, på ankarets djup. En form i rymden tar lagret till
    // World (`LayerPlacement::Auto`, style3d.h) och ritytan till 3D-målet.
    static Shape line3(Point3 a, Point3 b, Stroke stroke = {});
    static Shape polyline3(std::vector<Point3> points, Stroke stroke = {});
    static Shape text3(Point3 at, TrText text, TextStyle style = {});
    static Shape text3(Point3 at, std::vector<TextSpan> spans,
                       TextStyle style = {});
    static Shape marker3(Point3 at, MarkerStyle style = {});

    // ---- FÖREMÅLEN I RYMDEN — meshar, instanser och
    // punkter, djuptestade i ett World-lager. Precisionen är ramverkets:
    // varje form ritas relativt sitt eget origo i double (RTC), residensen på
    // GPU:n, uppladdningen inom budgeten, gallringen och detaljnivån sköter
    // ritytan (canvas.h).
    //
    // YTAN I RYMDEN: ytterringen i sitt eget plan (Newells normal —
    // `gfx::triangulatePlanar`), OBELYST i fyllningens färg som 2D-ytan, och
    // konturen som en sluten linje i rymden (Line3). En linjär gradient ligger
    // fast i världens xy som i 2D; en RADIELL ritas med sitt första stopp
    // (skriven begränsning — loggas en gång).
    static Shape polygon3(std::vector<Point3> outer, Paint fill,
                          std::optional<Stroke> stroke = std::nullopt);
    // EN MESH (mesh.h) där `placement` ställer den (style3d.h) och ritad med
    // `style`. Samma mesh i flera former är EN mesh på GPU:n.
    static Shape mesh(Mesh m, Placement placement = {}, MeshStyle style = {});
    // PRIMITIVERNA — belysta, med normalerna utåt (gfx/geom3d.h). Lådan,
    // sfären och cylindern är en DELAD ENHETSMESH som placeringen skalar och
    // flyttar (`mesh()`/`placement()` visar det), så tusen lådor är EN mesh
    // på GPU:n och en form byggd om varje bildruta laddar aldrig upp något.
    // SFÄRENS radie i världsenheter är världens; i px eller mm är den
    // KONSTANT PÅ SKÄRMEN (`MeshStyle::size` = diametern). CYLINDERN står
    // lodrätt från `base` (`sides` klampas till 3–256). RÖRET följer vägen
    // med geringar i knäna och lock i ändarna (`sides` 3–256) — dess egen
    // mesh, så en ändrad DIAMETER (data per form, som istjockleken per
    // ledning) byggs om för just den formen med `setShape`.
    static Shape box(Box3 box, MeshStyle style = {});
    static Shape sphere(Point3 center, Length radius, MeshStyle style = {});
    static Shape cylinder(Point3 base, double radius, double height,
                          MeshStyle style = {}, int sides = gfx::kCylinderSides);
    static Shape tube(std::vector<Point3> path, double diameter,
                      MeshStyle style = {}, int sides = gfx::kTubeSides);
    // INSTANSERNA: `prototype` (en mesh, låda, sfär eller cylinder
    // — annars blir formen tom och det loggas) på varje instans plats i
    // instansens färg, ritad med `style`. Prototypens koordinater är
    // instansens lokala ram och dess placering en förskjutning i den; en
    // sfär med radien i px eller mm behåller sin skärmstorlek när `style`
    // inte sätter en egen. Instanserna sorteras i rumsliga celler EN gång,
    // här: en kameraändring laddar inte upp något, cellerna utanför bild
    // ritas inte, och detaljnivån (prototypens `Lod`) väljs per cell.
    static Shape instances(Shape prototype, std::vector<Instance> instances,
                           MeshStyle style = {});
    // PUNKTERNA: världspunkter i stilens färg, och ett punktmoln
    // (mesh.h) med sina färger och skalärer — rampen läser skalären.
    static Shape points3(std::vector<Point3> points, PointStyle style = {});
    static Shape pointCloud(PointCloud cloud, PointStyle style = {});

    // ---- HELSKÄRMSPASSET: det EGNA programmet med namnet
    // `program` (`Canvas::addProgram`) ritar över hela ritytan FÖRST i
    // bildrutan — före marken och allt annat, utan djup, i lagrens ordning och
    // med lagrets blandning och opacitet. Programmet gör sina vertex själv:
    // tre ur SV_VertexID/gl_VertexID/[[vertex_id]] (helskärmstriangeln), ingen
    // ström. GlazeFrame bär kamerans matriser och STRÅLBASEN — strålen genom
    // NDC-punkten (x, y) är `forward + x · right + y · up`, med x och y i
    // −1…1 över målet och y uppåt — och origot är ÖGAT (chunk-lokalt är
    // världen minus ögat). En himmel, en bakgrundstoning, en vinjett: APPENS,
    // byggda med verktyget — ramverket har ingen färdig skydome. Ett okänt
    // eller trasigt program ritar
    // `PassStyle::fallback` i stället (loggat en gång per rityta och namn).
    // Ett pass tar ritytan till 3D-målet men ändrar inte lagrets placering.
    // Passet är LAGRENS: `Painter::draw` ritar det inte, och det träffas inte.
    static Shape screenPass(std::string program, PassStyle style = {});

    // ---- läsvägen (lagret bygger geometrin ur den). En kontur är vägens:
    // punkterna och om den är sluten.
    using Contour = Path::Contour;

    Kind                       kind() const { return kind_; }
    const std::vector<Contour>& contours() const { return contours_; }
    bool                        hasFill() const { return hasFill_; }
    const Paint&                fill() const { return fill_; }
    bool                        hasStroke() const { return hasStroke_; }
    const Stroke&               stroke() const { return stroke_; }
    // Text: ankaret [0]; markörerna; en bild på fyra hörn: hörnen i
    // RINGORDNING (övre vänstra, övre högra, nedre högra, nedre vänstra).
    const std::vector<Point>&   points() const { return points_; }
    // Etikettens text — TOM för en etikett i delar (läs `spans()`).
    const TrText&               text() const { return text_; }
    // Etikettens DELAR — tom för en vanlig text.
    const std::vector<TextSpan>& spans() const { return spans_; }
    const TextStyle&            textStyle() const { return textStyle_; }
    const MarkerStyle&          markerStyle() const { return marker_; }
    // Bildens rektangel — för en bild på fyra hörn LÅDAN kring hörnen.
    const Rect&                 rect() const { return rect_; }
    const ImageSource&          image() const { return image_; }
    // Bildens opacitet och färgjustering — normerade som fabriken lagrade dem
    // (opaciteten 0–1, justeringen ≥ 0, ett tal som inte är ändligt förvalet).
    const ImageStyle&           imageStyle() const { return imageStyle_; }
    // En bild på FYRA FRIA HÖRN (`image(öv, öh, nh, nv, …)`) — hörnen står i
    // `points()`; en bild på en rektangel har inga.
    bool isImageQuad() const { return kind_ == Kind::Image && points_.size() == 4; }
    // En form i rymden? (`Lines3`, `Text3`, `Markers3` och föremålen —
    // helskärmspasset ligger inte i rymden, och det gör inte sitt lager till
    // World)
    bool is3D() const { return d3_ != nullptr && kind_ != Kind::Pass; }
    // Formens punkter i rymden (linjen, ytan i rymden; textens och markörens
    // ankare som `[0]`) — tom för en 2D-form.
    const std::vector<Point3>&  points3() const;
    // FÖREMÅLEN — tomma för de andra slagen. `mesh()` är meshen,
    // primitivens enhetsmesh, ytans triangulering eller instansernas
    // prototyp; `placement()` och `meshStyle()` hör till den.
    const Mesh&                  mesh() const;
    const Placement&             placement() const;
    const MeshStyle&             meshStyle() const;
    const std::vector<Instance>& instances() const;
    const PointCloud&            pointCloud() const;
    const PointStyle&            pointStyle() const;
    // HELSKÄRMSPASSET — tomma för de andra slagen.
    const std::string&           passProgram() const;
    const PassStyle&             passStyle() const;
    // Instansernas celler (intern, src/canvas/instances.h) — tom för de
    // andra slagen.
    const std::shared_ptr<const detail::InstanceSet>& instanceSet() const;

private:
    static Shape area(std::vector<Contour> rings, Paint fill,
                      std::optional<Stroke> stroke);

    Kind                 kind_ = Kind::None;
    std::vector<Contour> contours_;
    Paint                fill_;
    bool                 hasFill_ = false;
    Stroke               stroke_;
    bool                 hasStroke_ = false;
    std::vector<Point>   points_;
    TrText               text_;
    TextStyle            textStyle_;
    MarkerStyle          marker_;
    Rect                 rect_{};
    ImageSource          image_;
    ImageStyle           imageStyle_;
    std::shared_ptr<const Shape3DData> d3_;   // bara formerna i rymden
    std::vector<TextSpan> spans_;             // en etikett i delar
};

} // namespace glaze
