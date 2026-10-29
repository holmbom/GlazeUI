#pragma once
#include <memory>
#include <optional>
#include <vector>

#include <gfx/geometry.h>

#include "glaze/canvas/geometry.h"
#include "glaze/canvas/shape.h"
#include "glaze/canvas/style3d.h"   // RenderState
#include "glaze/language.h"
#include "glaze/widget.h"   // EventArgs

// glaze::Painter — RITYTANS OMEDELBARA LÄGE. Det som
// ändras varje ritning — en HUD, ett gummiband, en markering under pekaren —
// ritas i ritytans `onPaint`/`onPaintOverlay` med samma former som lagren:
//
//     karta.onPaint = [this](glaze::Widget*, glaze::PaintArgs& e) {
//         e.painter.drawPolyline(spår, {.paint = gfx::Color{0, 120, 255},
//                                       .width = glaze::mm(0.5)});
//     };
//
// SAMMA BYGGARE SOM LAGREN, in i renderarens TRANSIENTA RING: varje ritning
// byggs om och laddas upp på nytt (det är därför lagren finns — det som
// ligger still ska ligga i lagren, där en kameraändring inte laddar upp
// något). På varandra följande former med samma program delar rit-anrop;
// ett programbyte tömmer satsen.
//
// RYMDEN: `onPaint` börjar i VÄRLDEN (kameran gäller), `onPaintOverlay` i
// ritytans LOKALA LOGISKA px (HUD:en — (0, 0) i övre vänstra hörnet).
// `onPaintLayer` börjar i världen som `onPaint`, men ritas i ett
// lagers ordning (`Canvas::setLayerPainter`) och inte ovanpå lagren.
// `screenSpace()` byter till den lokala rymden inom ett `save()`; `translate`/
// `rotate`/`scale` lägger en transform på punkterna (inte på måtten — en
// linje i mm är lika bred efter `scale(2, 2)`), och `save(rect)` KLIPPER det
// som ritas till en rektangel i den rymden.
//
// KONTRAKTET (som `AppWindow::frame`): ritningen MÅSTE vara DETERMINISTISK.
// Vid partiell omritning körs handlern en gång per skadad yta, och den får
// varken ändra trädet eller ritytans former — bara rita.
//
// Det som ritas här syns ALDRIG i GlazeBuilders preview: handlern är
// användarkod, och previewn kör aldrig användarkod. Det som ska synas i
// designern hör hemma i formlistan.

namespace glaze {

namespace detail {
struct PaintContext;   // src/canvas/scene.h — ritningens fönsterresurser
struct PainterState;   // src/canvas/painter.cpp
} // namespace detail

class Painter;

// Args till `onPaint`/`onPaintLayer`/`onPaintOverlay` — målaren är giltig
// under anropet.
struct PaintArgs : EventArgs {
    explicit PaintArgs(Painter& p) : painter(p) {}
    Painter& painter;
    // LAGRET målaren ritar i (`onPaintLayer` — dess plats, efter
    // en `moveLayer` den nya); −1 i `onPaint` och `onPaintOverlay`.
    int layer = -1;
};

class Painter {
public:
    // Skapas av ritytan för EN ritning — appen får den i `PaintArgs`.
    Painter(detail::PaintContext& ctx, bool screen);
    ~Painter();   // tömmer den sista satsen
    Painter(const Painter&)            = delete;
    Painter& operator=(const Painter&) = delete;

    // ---- FORMERNA — exakt lagrens (`Shape`), och bekvämligheterna ovanpå.
    void draw(const Shape& s);
    void drawLine(Point a, Point b, const Stroke& stroke = {});
    void drawPolyline(const std::vector<Point>& points,
                      const Stroke& stroke = {});
    void fillPolygon(const std::vector<Point>& outer, const Paint& fill,
                     std::optional<Stroke> stroke = std::nullopt);
    void fillRect(const Rect& r, const Paint& fill);
    void drawRect(const Rect& r, const Stroke& stroke = {});
    void fillCircle(Point center, double r, const Paint& fill);
    void drawText(Point at, const TrText& text, const TextStyle& style = {});
    // En etikett i DELAR (`Shape::text(p, {TextSpan…})`) — samma vertex som i
    // ett lager.
    void drawText(Point at, const std::vector<TextSpan>& spans,
                  const TextStyle& style = {});
    void drawMarker(Point at, const MarkerStyle& style = {});
    // Bilden med sin opacitet och FÄRGJUSTERING (shape.h `ImageStyle`) —
    // gånger målarens ton, som i ett lager.
    void drawImage(const Rect& r, const ImageSource& source,
                   const ImageStyle& style = {});
    // Bilden på FYRA FRIA HÖRN (`Shape::image(öv, öh, nh, nv, …)`) — hörnen
    // genom målarens transform; en fyrhörning som inte är strikt konvex ritar
    // ingenting.
    void drawImage(Point topLeft, Point topRight, Point bottomRight,
                   Point bottomLeft, const ImageSource& source,
                   const ImageStyle& style = {});

    // ---- I RYMDEN — `Shape::line3`/`polyline3`/
    // `text3`/`marker3`, ritade omedelbart. Transformen verkar i punktens
    // xy-plan och lämnar z orört. Djuptestet är målarens renderläge (nedan).
    void drawLine3(Point3 a, Point3 b, const Stroke& stroke = {});
    void drawPolyline3(const std::vector<Point3>& points,
                       const Stroke& stroke = {});
    void drawText3(Point3 at, const TrText& text, const TextStyle& style = {});
    void drawText3(Point3 at, const std::vector<TextSpan>& spans,
                   const TextStyle& style = {});
    void drawMarker3(Point3 at, const MarkerStyle& style = {});

    // ---- FÖREMÅLEN I RYMDEN — `draw` tar också
    // `Shape::mesh`/`box`/`sphere`/`cylinder`/`tube`/`polygon3`/`instances`/
    // `points3`/`pointCloud`, med samma program, stil och ljus som i ett
    // lager, och två bekvämligheter ovanpå: TRIANGLAR (var tredje hörn en
    // triangel i stilens färg — en markerad facett, ett överlägg) och
    // PUNKTER. De ritas ur sin CPU-kopia genom den transienta ringen, VARJE
    // ritning — det som ligger still hör hemma i ett lager, där en
    // kameraändring inte laddar upp något. Transformen verkar på lägena som
    // för alla målarens former. En mesh vars data är större än ett segment
    // (64 MiB) ritas inte här (loggas en gång).
    void drawTriangles3(const std::vector<Point3>& corners,
                        const MeshStyle& style = {});
    void drawPoints3(const std::vector<Point3>& points,
                     const PointStyle& style = {});

    // ---- RENDERLÄGET (style3d.h `RenderState`) — hur
    // det som ritas härnäst djuptestas, skriver djup, blandas och gallras,
    // till nästa `setRenderState` eller till `save()`-lägets slut (läget hör
    // till det sparade). MÅLAREN BÖRJAR UTAN DJUP — `{.depth =
    // DepthTest::Off, .depthWrite = false}`, ovanpå lagren — och
    // `setRenderState(glaze::RenderState{})` ger 3D:s förval, Less med
    // skrivning. `onPaintLayer` i ett World- eller Terrain-lager börjar med
    // LAGRETS renderläge, djuptestad som lagrets former. Läget
    // verkar i ritytans 3D-MÅL och i VÄRLDEN (`onPaint`); i
    // 2D-vägen, i `screenSpace()` och i `onPaintOverlay` finns inget djup att
    // testa mot, och där ritas allt utan djup. Text och markörer skriver
    // aldrig djup (som i ett World-lager). Ett ogiltigt läge (en uppräkning
    // utanför sina värden, en bias som inte är ändlig) ⇒ `gfx::log` och det
    // gamla gäller.
    void        setRenderState(const RenderState& state);
    RenderState renderState() const;

    // ---- RYMDEN
    // Ett SPARAT läge (transformen, rymden, renderläget och klippet) som läggs
    // tillbaka när objektet går ur räckvidd:
    //     { auto s = e.painter.save(); e.painter.screenSpace(); … }
    class Saved {
    public:
        explicit Saved(Painter* p) : p_(p) {}
        Saved(Saved&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
        Saved(const Saved&)            = delete;
        Saved& operator=(const Saved&) = delete;
        Saved& operator=(Saved&&)      = delete;
        ~Saved();

    private:
        Painter* p_;
    };
    [[nodiscard]] Saved save();
    // KLIPPET: samma sparade läge, och det som ritas till lägets
    // slut KLIPPS till rektangeln `clip` — i målarens transform och rymd, som
    // en form: vriden en fyrhörning, under tilt eller i en 3D-kamera dess bild
    // på skärmen (den del som ligger framför ögat). Ett nästlat klipp är
    // SNITTET med det som redan gäller, så det kan aldrig växa:
    //     { auto k = e.painter.save(glaze::Rect{0, 0, 200, 120}); … }
    // Klippet blir en yta på skärmen när det sätts — en senare `translate`,
    // `rotate`, `scale` eller `screenSpace` flyttar det inte. Linjerna,
    // ytorna, texten, markörerna och bilderna — också i rymden — klipps i
    // varje pixel och kantutjämnas som formerna; det som hamnar utanför ritas
    // inte alls, inte heller i djupet. FÖREMÅLEN I RYMDEN (meshar, instanser,
    // punkter) klipps till klippets OMSKRIVNA rektangel på skärmen: exakt för
    // ett klipp som är axelparallellt där. Ytan har högst 8 kanter — snittet
    // av två godtyckligt vridna rektanglar ryms alltid; fler ⇒ `gfx::log` en
    // gång och de minsta hörnen skärs av (klippet krymper, det växer aldrig).
    // En rektangel utan yta klipper bort allt; ett tal som inte är ändligt ⇒
    // `gfx::log` och samma sak.
    [[nodiscard]] Saved save(const Rect& clip);

    void translate(double dx, double dy);
    void rotate(double deg);   // moturs med y uppåt, som `Shape::arc`
    void scale(double sx, double sy);
    // Ritytans LOKALA LOGISKA px från och med nu (till `save()`-lägets slut):
    // (0, 0) i övre vänstra hörnet, y nedåt — HUD:ens rymd.
    void screenSpace();
    bool inScreenSpace() const;

    // Textblockets storlek i LOGISKA px (ovriden, utan platta) — för att
    // lägga ut en HUD. Mäter med samma atlas som ritar texten men UTAN att
    // ladda upp något; en etikett i delar med delarnas egna stilar (samma
    // låda som lagrets etikett). `Canvas::measureText` är samma mätning
    // utanför en ritning, i valfri enhet.
    gfx::Sizef measureText(const TrText& text, const TextStyle& style = {});
    gfx::Sizef measureText(const std::vector<TextSpan>& spans,
                           const TextStyle& style = {});

    // Ritytans storlek i logiska px.
    gfx::Sizef size() const;

private:
    void restore();   // Saved
    std::unique_ptr<detail::PainterState> s_;
};

} // namespace glaze
