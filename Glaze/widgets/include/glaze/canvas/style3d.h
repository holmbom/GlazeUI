#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <gfx/color.h>
#include <gfx/geom2d.h>  // gfx::GradientStop (= glaze::GradientStop, shape.h)
#include <gfx/scene.h>   // gfx::DepthTest, gfx::Blend, gfx::Cull
#include <gfx/sdf.h>     // gfx::TextAnchor, gfx::MarkerShape (shape.h:s alias)

#include "glaze/canvas/camera.h"         // Camera3D — del-vyns kamera
#include "glaze/canvas/geometry.h"       // Point, Point3, Vec3, Length
#include "glaze/canvas/image_source.h"   // MeshStyle::texture
#include "glaze/canvas/mesh.h"           // IndexRange

// RITYTANS 3D-STILAR — det som avgör HUR ett lager ritas i 3D-målet: var det
// ligger (`LayerPlacement`) och dess renderläge (`RenderState`), ljuset
// (`Lighting`), meshernas och punkternas stil (`MeshStyle`, `PointStyle`,
// `ColorRamp`), var en mesh eller en instans står (`Placement`, `Instance`),
// de EGNA PROGRAMMEN (`Program`, `PassStyle` och programmens fält sist i
// `MeshStyle` och `PointStyle`) och DEL-VYERNA (`View`), en egen kamera
// i en ruta på ritytan. Formerna i rymden står i shape.h (`line3`, `mesh`,
// `instances`, `screenPass` …), kameran i camera.h (`Camera3D`).
//
//     scen.setLayerPlacement(stolpar, glaze::LayerPlacement::World);
//     scen.setLayerRenderState(kanter,
//         glaze::RenderState{.depth = glaze::DepthTest::LessEqual,
//                            .depthWrite = false});
//     scen.setLighting(glaze::Lighting{.ambient = 0.45f, .exponent = 1.5f});
//     scen.addShape(stolpar, glaze::Shape::mesh(stolpe, {.at = fot, .yaw = 30},
//                                               {.color = gfx::Color{180, 180, 190}}));
//
// FÄLTORDNINGEN I VARJE STRUCT ÄR KONTRAKT för designerade initierare (regel
// 8 — `.glazeui` och previewn läser dem i deklarationsordning). Ett nytt fält
// läggs SIST — som de egna programmens fält i `MeshStyle` och `PointStyle`
// (`program`, `uniforms`, `textures`).

namespace glaze {

using DepthTest = gfx::DepthTest;
using Blend     = gfx::Blend;
using Cull      = gfx::Cull;

// ETT LAGERS RENDERLÄGE i 3D-målet. APPENS FÖRVAL ÄR 3D: djuptest Less med
// skrivning, så `{.depthWrite = false}` stänger av skrivningen utan att stänga
// av testet. (Fasadens `gfx::RenderState` har förvalet Off (2D) — så
// att inget 2D-anrop ändras; det här är lagrets.) Fälten och deras betydelse
// är fasadens (gfx/scene.h): djupbias negativt = närmare, gallringens
// framsida moturs sett från kameran. Ordningen är kontrakt för designerade
// initierare.
//
// ETIKETTER (text och markörer) i ett World-lager djuptestas med lagrets läge
// men SKRIVER ALDRIG DJUP: de står upprätta i skärmen, och en etiketts gloria
// och fyllning ligger på samma djup — med skrivning hade gloria skymt
// fyllningen. De skyms alltså av 3D, men skymmer inget själva.
struct RenderState {
    DepthTest depth      = DepthTest::Less;
    bool      depthWrite = true;
    float     depthBias  = 0.0f;
    float     slopeBias  = 0.0f;
    Blend     blend      = Blend::Alpha;
    Cull      cull       = Cull::None;
};

// VAR ETT LAGER LIGGER i 3D-målet:
//   Auto     förvalet: Ground om lagret bara har 2D-former, annars World —
//            avgjort ur formerna, deterministiskt.
//   Ground   målas på MARKEN, utan djup och i målarordning — 2D-kartans hela
//            beteende. Etiketterna ritas ÖVERST, efter World-lagren, och
//            kollisionen gäller alla etiketter. Meshar, instanser och punkter
//            i ett Ground-lager ritas också utan djup, i lagrets ordning.
//   World    djuptestas med lagrets `RenderState`; 2D-former i lagret ligger
//            på z = 0.
//   Terrain  lagrets meshar ÄR marken: de ritas med
//            lagrets renderläge och bär DRAPERINGEN — Ground-lagrens bilder,
//            ytor, linjer och liggande text (`Alignment::Ground`) ritade
//            uppifrån i ett mål per mesh och lagda över dess yta. Med terräng
//            ritas Ground-lagrens geometri BARA genom draperingen (ett hål i
//            terrängen visar det som ligger bakom, inte marken), och deras
//            stående etiketter och markörer står vid markhöjd (terrängens
//            höjd under ankaret — `Canvas::heightAt`). Instanser i ett
//            Terrain-lager ritas utan drapering.
// Ett World- eller Terrain-lager tar ritytan till 3D-målet (canvas.h).
enum class LayerPlacement : std::uint8_t { Auto, Ground, World, Terrain };

// EN FÄRGRAMP — en skalär per läge (meshens `scalars`,
// punktmolnets) blir en färg: t = (skalär − min) / (max − min), klampad till
// 0–1, genom stoppen (2D-gradientens stopp och dess bild om 256 texlar,
// LINJÄRT mellan dem). Utan stopp ingen ramp; max ≤ min ger första stoppet.
// Rampen MULTIPLICERAR stilens färg och lägets — en vit stil visar rampen som
// den är.
struct ColorRamp {
    std::vector<gfx::GradientStop> stops;
    double                         min = 0.0;
    double                         max = 1.0;
};

// LJUSET för ritytans belysta meshar (`Canvas::setLighting`) — ETT
// riktat ljus fast i världen, och dimman. Modellen per fragment:
//
//     färg × (ambient + diffuse · max(N·L, 0)^exponent + rim · max(N·R, 0))
//
// klampad till 0–1, där L är riktningen MOT ljuset (`direction` är ljusets
// FÄRDRIKTNING — nedåt för en sol — och vänds) och R `rimDirection` normerad
// (en yta vars normal pekar dit får rimljuset, till exempel norr). TVÅSIDIGT
// (`twoSided`) belyses en triangels baksida som framsidan, annars bara med
// ambient. DIMMAN blandar färgen mot `fog` med andelen
// ((d − fogStart) / (fogEnd − fogStart))^fogPower × `fog`s alfa, klampad, där
// d är avståndet till ögat (ortografiskt djupet längs blicken); fogEnd ≤
// fogStart eller alfa 0 är ingen dimma (förvalet). Avstånden i
// världsenheter. Förvalen ger ett ljus snett ovanifrån. Ljuset gäller meshar
// med `MeshStyle::lit`; dimman meshar med `MeshStyle::fog` och punkter med
// `PointStyle::fog`.
struct Lighting {
    Vec3       direction{-0.3, -0.4, -0.866};
    float      ambient  = 0.4f;
    float      diffuse  = 0.7f;
    float      exponent = 1.0f;
    Vec3       rimDirection{0.0, 1.0, 0.0};
    float      rim = 0.0f;
    gfx::Color fog{0, 0, 0, 0};
    double     fogStart = 0.0;
    double     fogEnd   = 0.0;
    float      fogPower = 1.0f;
    bool       twoSided = true;
};

// VAR EN MESH STÅR (`Shape::mesh`): meshen VRIDS och SKALAS kring
// sitt eget origo (`Mesh::origin`) och FLYTTAS med `at` — världen = origo +
// at + R · (skala · läget), där läget är relativt origot. Vinklarna i grader
// i den högerhänta kartvärlden med z upp, i den här ordningen på meshen:
//   yaw    kring z, MEDURS sett ovanifrån som kompassen (`Camera3D::azimuth`):
//          meshens +y (norr) pekar mot bäringen `yaw`
//   pitch  kring meshens x, positivt lyfter +y (nosen upp)
//   roll   kring meshens y, positivt sänker +x (höger sida ned)
// alltså R = Rz(−yaw) · Rx(pitch) · Ry(roll), och skalan före rotationen. En
// speglande skala (ett negativt tal) vänder vindningen: programmet räknar med
// det för framsidan och normalen, men `Cull::Back` gallrar då utsidan.
// Precisionen är ramverkets: origo + at summeras i double (RTC).
struct Placement {
    Point3 at{};
    float  yaw   = 0.0f;
    float  pitch = 0.0f;
    float  roll  = 0.0f;
    Float3 scale{1.0f, 1.0f, 1.0f};
};

// EN INSTANS (`Shape::instances`): prototypen står där en Placement
// hade ställt den — `at`, vinklarna och skalan som ovan, med prototypens
// koordinater som instansens lokala ram — och färgen MULTIPLICERAR stilens;
// alfa 0 är ingen färg (förvalet), så en vit prototyp får instansens färg och
// en färgad tonas.
struct Instance {
    Point3     at{};
    float      yaw   = 0.0f;
    float      pitch = 0.0f;
    float      roll  = 0.0f;
    Float3     scale{1.0f, 1.0f, 1.0f};
    gfx::Color color{0, 0, 0, 0};
};

// HUR EN MESH RITAS (`Shape::mesh`, `box`, `sphere`, `cylinder`, `tube`,
// `instances`). Färgen kedjas: stilens `color` × meshens färg per
// läge × instansens × `texture` (meshens uv) × rampen (meshens skalär). Sedan
// FÄRGJUSTERINGEN — `brightness` (färgen gånger talet: 0 svart),
// `contrast` (kring mittgrått: 0 grått) och `saturation` (mot luminansen: 0
// GRÅSKALA), i den ordningen och klampade till 0–1 efter varje steg; 1 är
// orört och över 1 förstärker, precis som en bilds (shape.h `ImageStyle`) —,
// ljuset (`lit`, se `Lighting`), dimman (`fog`) och sist alfan × `opacity` ×
// lagrets opacitet. Justeringen är uniformer: att ändra den med `setShape`
// laddar varken upp meshen eller texturen igen.
//   range   elementen som ritas (markering, en del av meshen), snittade med
//           detaljnivån — förvalet är hela meshen (mesh.h `IndexRange`)
//   size    > 0: KONSTANT SKÄRMSTORLEK — meshens LOKALA ENHET blir `size`
//           på skärmen (px eller mm; i världsenheter en skala), räknad per
//           bildruta vid meshens origo: en sfär med diametern 1 och storleken
//           mm(4) är 4 mm nära och fjärran. `Shape::sphere` med radien i px
//           eller mm sätter den själv.
// Texturen samplas med bildens eget filter. Utan uv i meshen gäller
// texturen inte, och utan skalärer gäller rampen inte.
//
// ETT EGET PROGRAM (`Program` nedan): `program` är programmets
// NAMN (`Canvas::addProgram`), och då ritar det meshen i stället för det
// inbyggda Mesh-programmet — med meshens strömmar enligt platskonventionen
// (gfx/program.h: läge, normal, färg, uv, skalär och user0–3), GlazeFrame,
// GlazeUser ur `uniforms` (float4-packat, utfyllt med nollor till programmets
// `uniformFloats`) och texturerna ur `textures` på platserna 0, 1 …, i
// lagrets renderläge. Färgen, ljuset, dimman, färgjusteringen, `texture`,
// `ramp` och `size` är det INBYGGDA programmets och gäller inte ett eget
// (förspelets `glaze_colorAdjust` gör samma justering, gfx/program.h) — men
// detaljnivån, `range`, gallringen och lagrets opacitet (GlazeFrame:s
// `scale.z`) gör det. Placeringens vridning och skala ligger i GlazeFrame:s
// matriser, så programmet läser meshens LOKALA lägen rakt av. Saknas
// programmet, kompilerar det inte, saknar det källa för backenden eller läser
// det en ström meshen inte har, ritas meshen med det inbyggda programmet
// (loggat en gång per rityta och namn; felet i `Canvas::programError`). Egna
// program ritar inte INSTANSER (det inbyggda tar dem, loggat): prototypens
// placering bor i det inbyggda programmets block.
struct MeshStyle {
    gfx::Color               color{255, 255, 255, 255};
    float                    opacity    = 1.0f;
    bool                     lit        = true;
    bool                     fog        = true;
    float                    saturation = 1.0f;
    ImageSource              texture;
    ColorRamp                ramp;
    IndexRange               range;
    Length                   size;
    std::string              program;
    std::vector<float>       uniforms;
    std::vector<ImageSource> textures;
    // FÄRGJUSTERINGENS två andra tal — med `saturation` ovan; SIST,
    // för fältordningen är kontrakt.
    float                    contrast   = 1.0f;
    float                    brightness = 1.0f;
};

// HUR PUNKTER RITAS (`Shape::points3`, `pointCloud`): en kvadrat
// eller en cirkel per punkt, upprätt i skärmen på punktens djup (djuptestad
// mot meshar), med DIAMETERN `size` — px och mm konstanta på skärmen,
// världsenheter genom djupet. Färgen: `color` × punktens färg × rampen
// (punktens skalär). Inget ljus; dimman med `fog`.
//
// ETT EGET PROGRAM: `program` och `uniforms` som i `MeshStyle`. Det
// ritas som det inbyggda — sex vertex per punkt (två trianglar ur
// SV_VertexID/gl_VertexID/[[vertex_id]]) med punkterna i INSTANSSTRÖMMEN
// (läget, färgen och skalären på kPosition/kColor/kScalar) — och samplar inga
// texturer. Reserven som för meshen.
struct PointStyle {
    Length             size  = px(3.0f);
    gfx::MarkerShape   shape = gfx::MarkerShape::Square;
    gfx::Color         color{0, 0, 0, 255};
    ColorRamp          ramp;
    bool               fog = true;
    std::string        program;
    std::vector<float> uniforms;
};

// ETT HELSKÄRMSPASS (`Shape::screenPass`): programmets GlazeUser
// (`uniforms`, float4-packat) och texturer (`textures`, platserna 0, 1 …) —
// och `fallback`, färgen som fyller ritytan när programmet saknas, inte
// kompilerar eller saknar källa för backenden (alfa 0, förvalet, ritar
// ingenting). Previewn kör aldrig ett program och ritar alltid `fallback`.
struct PassStyle {
    std::vector<float>       uniforms;
    std::vector<ImageSource> textures;
    gfx::Color               fallback{0, 0, 0, 0};
};

// ETT EGET PROGRAM — appens shader i ritytan,
// registrerat med `Canvas::addProgram` och nämnt vid NAMN av `MeshStyle`,
// `PointStyle` och `Shape::screenPass`. Källkod per backend: `hlsl`
// (D3D11), `glslVertex` + `glslFragment` (GL 3.3) och `msl` (Metal) — bara
// den backend som kör behöver sin. Kontrakten — förspelet med GlazeFrame som
// `glaze` och `glaze_toClip`, platserna, GlazeUser och texturerna — står i
// gfx/program.h. `uniformFloats` är GlazeUser-blockets flyttal (avrundat
// uppåt till hela float4, högst 1024 — Metals tak) och `textures` antalet
// texturer (högst 4). Kompileras per fönster första gången det används; ett
// fel loggas en gång och står i `Canvas::programError(namn)` med raden i
// appens EGEN källa. Programmet är KODDATA (Bitmaps prejudikat) och har
// ingen `.glazeui`-form — previewn kör aldrig användarkod.
struct Program {
    std::string name, hlsl, glslVertex, glslFragment, msl;
    std::string vertexEntry   = "vsMain";
    std::string fragmentEntry = "fsMain";
    int         textures      = 0;
    int         uniformFloats = 0;
};

// EN DEL-VY — en egen kamera i en
// rektangel på ritytan, ovanpå huvudvyn: navigeringskuben, axeltriaden,
// förhandsvisningens andra och tredje vy. Vyn ritas i ett EGET mål med eget
// djup och egen MSAA, så huvudscenen skymmer aldrig det vyn ritar (en vy i
// huvudmålet med viewport och scissor förkastades — D3D11 rensar inte en
// del av djupet).
//
//     int kub = scen.addView(glaze::View{
//         .anchor = glaze::TextAnchor::TopRight, .offset = {-8, 8},
//         .width = 96, .height = 96,
//         .camera = glaze::Camera3D{.distance = 6,
//                                   .projection = glaze::Projection::Orthographic},
//         .followRotation = true});
//     scen.setLayerViews(kubLager, {kub});   // lagret ritas bara i vyn
//
// RUTAN är i ritytans LOKALA LOGISKA px (Draw:s rymd, y nedåt). `anchor` säger
// vilken punkt på ritytan och samma punkt på vyn som ligger på varandra —
// TopRight lägger vyns övre högra hörn i ritytans, Center mitt i mitt — och
// `offset` flyttar vyn därifrån (x åt höger, y nedåt). `width`/`height` 0 är
// ritytans hela bredd respektive höjd. Det som hamnar utanför ritytan syns
// inte.
//
// KAMERAN är vyns egen. `followRotation` vrider den som HUVUDVYN — en
// `Camera3D`s azimut, elevation, roll och upp-axel, kartkamerans bäring och
// tilt — medan målet, avståndet, projektionen och synfältet är vyns: en
// navigeringskub följer huvudvyns rotation men inte dess förflyttning. (En
// karta med y NEDÅT — `setYUp(false)`, widgetens lokala rymd — ses som
// underifrån, och vyn följer det.)
//
// LAGREN: en vy ritar BARA de lager `Canvas::setLayerViews` lagt i den
// (huvudvyn är vy 0 och har alla lager från start), med huvudvyns regler —
// Ground-lagren på marken, World-lagren med djup, Ground-etiketterna överst —
// och etiketterna kolliderar inom vyn. Rutnätet och `onPaint` hör till
// huvudvyn.
//
// BAKGRUNDEN fyller vyns ruta innan scenen ritas; alfa 0 (förvalet) låter
// huvudvyn synas där vyn inte ritar något.
//
// Fältordningen är kontrakt för designerade initierare.
struct View {
    gfx::TextAnchor anchor = gfx::TextAnchor::TopLeft;
    Point           offset{};
    double          width  = 0.0;
    double          height = 0.0;
    Camera3D        camera;
    bool            followRotation = false;
    gfx::Color      background{0, 0, 0, 0};
};

} // namespace glaze
