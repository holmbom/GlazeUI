#pragma once

// 3D-GRÄNSSNITT I SCENEN — paneler av knappar, vippor, reglage, vred,
// handtag och etiketter som står i världen, sitter på en hand eller följer
// huvudet, och som pekas på med fingret, kontrollens spets eller strålen.
//
// Gränssnittet ritar sig självt som vanliga former i ETT eget lager i
// ritytan (`Interface3D(canvas, layer)`): en panels bakplatta och varje
// elements kropp är lådor som flyttas genom att deras Placement byts, och
// texten och ikonen ligger på elementets framsida som en texturerad yta
// (`glaze::RenderTexture` + `MeshStyle::texture`) — med ritytans fönsters
// fonter och tema, ramverkets standardikoner (`glaze::StockIcon`) eller
// appens egna bilder. Allt i utseendet går att byta: stilen per gränssnitt,
// panel och element, och en egen ritfunktion per element som ersätter den
// inbyggda helt.
//
// Ingenting är låst till en form: layouten är fri, ett rutnät, en rad eller
// en kolumn; placeringen är världen, en hand (vänster, höger, den primära
// eller den andra), huvudet eller appens egen pose varje bildruta; och vilken
// hand som är primär väljs (`setPrimaryHand`, förval höger — en panel på
// `Attach::SecondaryHand` sitter då på vänster hand).
//
// TRÅDARNA: allt i UI-tråden. `update` anropas en gång per bildruta från
// sessionens `onFrame` (före ritningen), och `onElement` körs inifrån den.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <glaze_openxr/math.h>
#include <glaze_openxr/session.h>

namespace glaze::openxr {

class FlatPanel;   // glaze_openxr/flat_panel.h

// ---------------------------------------------------------------- PLACERING
enum class Attach : std::uint8_t {
    World,           // `Mount::pose` i världen
    LeftHand,        // vänster hand
    RightHand,       // höger hand
    PrimaryHand,     // den primära handen (`setPrimaryHand`)
    SecondaryHand,   // den andra handen — förval för verktygslådor
    Head,            // följer huvudet (`Mount::headLag` mjukar upp det)
    Custom           // appen sätter posen varje bildruta (`Interface3D::setPanelPose`)
};
enum class HandFrame : std::uint8_t { Grip, Aim, Palm };

struct Mount {
    Attach    attach    = Attach::World;
    HandFrame handFrame = HandFrame::Grip;   // vilken handpose en hand-panel följer
    Pose      pose;                          // World: i världen; annars förskjutningen i handens/huvudets ram (meter)
    // Spegla förskjutningen (x och vridningen kring z) för vänster hand, så
    // samma `pose` ger samma läge relativt tummen i båda händerna.
    bool      mirrorForLeft = true;
    double    headLag       = 0.0;           // Head: 0 = styvt, 0 … 1 = andel kvar per bildruta (mjuk följning)
    bool      facePointer   = false;         // World/Head: vrid panelen mot huvudet (bara kring z)
};

// NÄR EN PANEL SYNS — utöver `Panel::visible`. Med båda villkoren av syns
// den alltid. Med något på visas den när villkoren gäller (`requireBoth`:
// båda, annars något av dem) och tonar in och ut på `fadeSeconds`. En panel
// som tonat bort tar inga tryck.
struct Visibility {
    // Blicken: panelens mitt inom så många grader från huvudets blick
    // (0 = villkoret av).
    double gazeDegrees      = 0.0;
    // Den andra handen — den som INTE bär panelen; för en panel som inte
    // sitter på en hand den primära — inom så många meter från panelen
    // (0 = villkoret av).
    double otherHandMetres  = 0.0;
    bool   requireBoth      = false;
    double fadeSeconds      = 0.15;
    // Så länge en hand trycker, drar eller greppar något i panelen syns den.
};

// ------------------------------------------------------------------- UTSEENDE
// Alla mått i meter (i panelens ram, före `Anchor::scale`).
struct ElementStyle {
    double     width  = 0.04;
    double     height = 0.04;
    double     depth  = 0.012;     // kroppens tjocklek
    double     travel = 0.008;     // hur långt en knapp går in
    gfx::Color body{60, 66, 78};
    gfx::Color face{230, 233, 238};   // framsidans bakgrund (texten/ikonen ritas på den)
    gfx::Color text{20, 22, 26};
    gfx::Color hover{90, 150, 240};    // kanten/glöden när något närmar sig
    gfx::Color active{40, 120, 230};   // nedtryckt, påslagen vippa, reglagets fyllning
    gfx::Color disabled{120, 120, 120};
    double     textSize    = 0.012;   // teckenhöjd i meter
    double     iconSize    = 0.022;   // ikonens sida i meter
    int        texturePxPerMetre = 4000;   // framsidans upplösning
    bool       lit = true;
    bool operator==(const ElementStyle&) const = default;
};

struct PanelStyle {
    bool       backplate = true;
    gfx::Color plate{34, 37, 44};
    double     thickness = 0.006;
    double     padding   = 0.01;
    double     spacing   = 0.008;
    ElementStyle element;   // elementens förval i panelen
};

// ------------------------------------------------------------------- ELEMENT
enum class ElementKind : std::uint8_t {
    Button,   // trycks in, klick vid släpp
    Toggle,   // trycks in, växlar `toggled`
    Slider,   // värdet längs elementets bredd (dras med fingret eller strålen)
    Dial,     // värdet som vridning kring framsidans normal
    Handle,   // greppas och flyttar panelen (`Squeeze` eller nyp)
    Label     // bara text/ikon, ingen interaktion
};

struct Content {
    TrText                          text;    // tom = ingen text
    std::optional<glaze::StockIcon> icon;    // ramverkets ikon
    ImageSource                     image;   // appens bild (går före `icon`)
};

struct Element {
    std::string  id;                      // unikt i panelen
    ElementKind  kind = ElementKind::Button;
    Content      content;
    std::optional<ElementStyle> style;    // tomt = panelens
    bool   enabled = true;
    bool   toggled = false;               // Toggle
    double value   = 0.0;                 // Slider/Dial i [min, max]
    double min = 0.0, max = 1.0, step = 0.0;   // step 0 = steglöst
    // Var elementet står: i Free-layouten `pose` i panelens ram; i rutnät,
    // rad och kolumn cellen (span för större element) — `pose` läggs då ovanpå.
    int  row = 0, column = 0, rowSpan = 1, columnSpan = 1;
    Pose pose;
};

// ------------------------------------------------------------------- LAYOUT
enum class LayoutKind : std::uint8_t { Free, Grid, Row, Column };
enum class Align : std::uint8_t { Start, Center, End };
struct Layout {
    LayoutKind kind    = LayoutKind::Grid;
    int        columns = 5;     // Grid
    int        rows    = 0;     // Grid: 0 = så många som behövs
    Align      horizontal = Align::Center;
    Align      vertical   = Align::Center;
    bool       autoSize   = true;   // panelens storlek ur innehållet; annars `size`
    double     width = 0.0, height = 0.0;   // meter, när `autoSize` är false
};

// ------------------------------------------------------------ PLATTA PANELER
// En platt panel visar ett riktigt Glaze-widgetträd (`FlatPanel`) eller en
// stillbild på en yta i scenen — som en tavla med tjocklek och ram, och
// valfritt på en stolpe ned till marken (en skylt). Pekaren är musen:
// hover, tryck med avtryckaren eller fingrets tryckdjup, drag och rullning
// med spaken.
struct Stand {                              // stolpen från tavlans underkant rakt ned
    bool       on = false;
    double     radius = 0.03;               // meter
    gfx::Color color{90, 94, 100};
    // Var stolpen slutar: tomt = första scenytan rakt under tavlan (utan
    // gränssnittets lager), annars den här höjden (världsenheter).
    std::optional<double> groundZ;
};
struct FlatStyle {
    double     thickness = 0.03;            // tavlans tjocklek (meter); 0 = bara ytan
    gfx::Color body{60, 66, 78};            // kanter, ram och baksida
    double     border = 0.01;               // ram runt ytan (meter)
    Stand      stand;
};
struct FlatContent {
    // Ett levande widgetträd (lånat, lever längre än panelen) som pekaren
    // styr — ELLER `image`, en stillbild (t.ex. `FlatPanel::snapshot()`). Varje
    // levande FlatPanel är ett eget offscreen-fönster; många skyltar visar
    // därför stillbilder och appen kopplar ett levande träd till den skylt
    // användaren arbetar vid.
    FlatPanel*    live = nullptr;
    glaze::Bitmap image;
    FlatStyle     style;
};

struct Panel {
    std::string          id;
    Mount                mount;
    Layout               layout;
    PanelStyle           style;
    std::vector<Element> elements;
    bool                 visible     = true;
    Visibility           showWhen;              // blick och den andra handen; förval: alltid
    bool                 grabbable   = false;   // panelen kan greppas i plattan och flyttas
    // En platt panel: med den är `elements` tom och `layout.width/height`
    // ytans mått i meter (utan ram).
    std::optional<FlatContent> flat;
};

// ------------------------------------------------------------ PEKAREN
// EN pekare per hand, i ETT läge i taget, och ETT mål per bildruta — för
// gränssnittets element och för scenen. Appen läser `Interface3D::pointer`
// i stället för att räkna egna strålar.
//   Wand  NÄRLÄGET: en stav ut ur kontrollen längs siktet; spetsen trycker
//         knappar, och greppet tar vred, reglage, handtag och föremål vid
//         spetsen. För precision nära det man arbetar med.
//   Ray   FJÄRRLÄGET: strålen ur siktet till första träffen; avtryckaren
//         trycker, greppet tar det strålen pekar på.
//   Auto  staven när spetsen är nära något den kan påverka (element, en
//         grepbar platta, en yta i scenen inom `wandRange`), annars strålen.
//   None  ingen pekare.
// Med handspårning tar fingertoppen över närläget (`Pointers::fingertip`).
// En hand pekar aldrig på en panel den själv bär, och medan en panel den bär
// syns har handen ingen pekare alls.
enum class PointerMode : std::uint8_t { Auto, Wand, Ray, None };

struct Pointers {
    bool   fingertip     = true;
    double tipOffset     = 0.12;   // stavens längd: spetsen framför kontrollens sikte (meter)
    double wandRange     = 0.20;   // Auto: staven inom så många meter från något, annars strålen
    double rayLength     = 20.0;   // strålens längd (meter)
    bool   draw          = true;   // gränssnittet ritar staven och strålen i sitt lager
    double wandRadius    = 0.004;  // meter
    gfx::Color wandColor{200, 205, 215};
    gfx::Color wandTip{90, 150, 240};   // spetsen; lyser med `ElementStyle::hover` vid närhet
    double rayRadius     = 0.002;  // meter
    gfx::Color rayColor{150, 190, 255};
    double hoverDistance = 0.05;   // närhet till ett element räknas inom detta avstånd (meter)
    bool   haptics       = true;   // vibration vid tryck och släpp
};

// Pekarens mål den här bildrutan: närmast av gränssnittets element, en
// grepbar platta och scenen (gränssnittets eget lager räknas aldrig som scen).
enum class PointerTarget : std::uint8_t { None, Element, Plate, Flat, Scene };
struct PointerHit {
    PointerTarget                   kind = PointerTarget::None;
    std::string                     panel, element;   // Element/Plate
    std::optional<glaze::HitResult> scene;            // Scene: träffen (lager, form, punkt, normal)
    gfx::Pointf                     flatPx{};         // Flat: träffen i panelens fönster (fysiska px)
    Vec3   point;                                     // träffen i världen
    Vec3   normal{0.0, 0.0, 1.0};
    double distance = 0.0;                            // från pekarens början, världsenheter
};

struct PointerState {
    bool        valid  = false;                  // handen har en pose och läget är inte None
    PointerMode mode   = PointerMode::None;      // det UPPLÖSTA läget: Wand eller Ray (aldrig Auto)
    bool        finger = false;                  // närläget är handspårningens fingertopp
    Vec3        origin;                          // där pekaren börjar (siktet eller fingret)
    Vec3        direction{0.0, 1.0, 0.0};
    Vec3        tip;                             // stavens spets, eller strålens slut (träffen eller full längd)
    PointerHit  hit;
    bool        consumed = false;                // gränssnittet tar handens tryck den här bildrutan
    bool        pressing = false;                // handen håller ett tryck i gränssnittet (avtryckare, stav eller finger)
};

enum class ElementEventKind : std::uint8_t {
    HoverEnter, HoverLeave, Pressed, Released, Clicked, Toggled, ValueChanged, Grabbed, Dropped
};
struct ElementEvent {
    std::string      panel;
    std::string      element;
    ElementEventKind kind = ElementEventKind::Clicked;
    Hand             hand = Hand::Right;
    double           value = 0.0;   // ValueChanged: det nya värdet; Toggled: 1/0
};

// Ett elements tillstånd den här bildrutan — för egna ritfunktioner och
// egna effekter.
struct ElementState {
    double proximity  = 0.0;    // 0 … 1, hur nära en pekare är
    double pressDepth = 0.0;    // 0 … 1 av `travel`
    bool   hovered    = false;
    bool   pressed    = false;
    std::optional<Hand> by;     // handen som pekar/trycker
};

// En egen ritning av ett element: lägg former i `out` i elementets ram
// (`frame` är elementets pose i världen; `state` dess tillstånd). Ersätter
// den inbyggda ritningen för elementet helt.
struct ElementDrawArgs {
    const Panel&        panel;
    const Element&      element;
    const ElementStyle& style;
    const ElementState& state;
    Pose                frame;
    double              scale = 1.0;   // panelens skala i världen
};
using ElementDrawer = std::function<void(const ElementDrawArgs&, std::vector<glaze::Shape>& out)>;

class Interface3D final {
public:
    // Ritar i ritytans lager `layer` (som gränssnittet äger helt).
    Interface3D(glaze::Canvas& canvas, int layer);
    ~Interface3D();

    Interface3D(const Interface3D&)            = delete;
    Interface3D& operator=(const Interface3D&) = delete;

    // Panelerna — kollektionsvokabulären: lägg till, ersätt helt, ta bort.
    void addPanel(Panel panel);
    void setPanel(const std::string& id, Panel panel);
    void removePanel(const std::string& id);
    const Panel* panel(const std::string& id) const;
    void setPanelVisible(const std::string& id, bool visible);
    void setPanelPose(const std::string& id, const Pose& pose);   // Custom, och World efter ett grepp
    std::optional<Pose> panelPose(const std::string& id) const;   // senaste i världen

    // Ett element — ersätts helt; bara det elementet ritas om.
    void setElement(const std::string& panel, Element element);
    const Element* element(const std::string& panel, const std::string& id) const;
    ElementState state(const std::string& panel, const std::string& id) const;

    void setPrimaryHand(Hand hand);       // förval Hand::Right
    Hand primaryHand() const;
    void setPointers(const Pointers& pointers);
    void        setPointerMode(Hand hand, PointerMode mode);   // förval Auto för båda
    PointerMode pointerMode(Hand hand) const;
    // Pekaren efter senaste `update` — läget, geometrin och målet. Appen
    // använder `hit` för sina egna föremål (grepp, markörer) och låter bli
    // när `consumed` är sant.
    const PointerState& pointer(Hand hand) const;
    void setDrawer(const std::string& panel, const std::string& element, ElementDrawer drawer);

    std::function<void(const ElementEvent&)> onElement;

    // En gång per bildruta från `onFrame`, FÖRE appens egen hantering av
    // knapparna: placerar panelerna, upplöser pekarna och deras mål, låter dem
    // verka, fyrar `onElement` och byter de former som ändrats. `session` ger
    // scenens träffar för staven och vibrationen, och en hand som gränssnittet
    // tar (`consumed`) tas från förflyttningens grepp (`Session::claimHand`);
    // null = inget av det. Utan
    // poser står World-panelerna kvar och hand-panelerna och pekarna döljs.
    void update(const FrameInfo& frame, Session* session);

    // `pointer(hand).consumed`.
    bool consumes(Hand hand) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace glaze::openxr
