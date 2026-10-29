#pragma once
// INTERN — FÖREMÅLEN I RITYTAN: meshar, ytor i rymden, instanser och punkter
// ritade genom de
// inbyggda Mesh- och Points-programmen (programs3d.h). Lagret håller formerna
// i sina Mesh- och Points-körningar (scene.h `RunData::objects`), fönstret
// deras data på GPU:n (residency.h) — här är förberedelsen och ritningen.
//
// FÖRBERED (före ritytans 3D-mål — allt som laddar upp sker där):
// varje föremål i ett synligt lager görs residentt inom budgeten (också
// utanför bild och utanför lagrets zoomintervall, så en kameraändring aldrig
// laddar upp), och för ett ritat lager hämtas texturerna och ramperna. Första
// gången ett fönster förbereder föremål kompileras de vanligaste
// programvarianterna (residency.h `precompile`). SYNLIGT FÖRST:
// föremålen som syns i vyn — gallrade med lådan formen har nu — laddas
// upp närmast först, och de andra med det som blir kvar av framens budget
// (scene.h `prepareUploads`); de blir residenta över de följande framesen. I
// 2D-vägen laddar ritningen upp de synliga och `prepareUploads` resten.
//
// RITA, per föremål på dess plats (det platsen VISAR — dubbelbuffringen):
//   PRECISIONEN (RTC)  anropets origo i double: meshens origo +
//            placeringens `at`, instansblockets origo, punktchunkens origo —
//            GlazeFrame:s matris tas med det, och bara det lilla går till float
//   GALLRINGEN  lådans åtta hörn — meshens låda genom placeringen,
//            instanscellens, punktchunkens — prövas HOMOGENT mot synpyramidens
//            sex plan: lådan gallras när alla hörn ligger utanför SAMMA plan
//            (bakom kameran är bakom närplanet). Konservativ. En
//            skärmkonstant mesh prövas med sitt origo och sin storlek i px.
//   DETALJNIVÅN  per mesh och per instanscell: avståndet från ögat
//            till lådans närmaste punkt (axelparallella lådan kring den
//            placerade lådan) och `Lod::maxDistance` — samma regel som
//            plockningen (mesh_data.h `lodLevel`); stilens `range` snittas
//            med nivån
//   INSTANSERNA  angränsande celler i samma block och på samma nivå ritas i
//            ETT anrop per segment — anropen ≤ de synliga cellerna
//   PROGRAMMET  varianten ur kopians strömmar och stilen (normaler, färger,
//            uv med en textur, skalärer med en ramp, instanser, skärmstorlek)
//   LJUSET OCH STILEN  `Lighting` och `MeshStyle` till GlazeUser
//            (`meshUniforms`), lagrets opacitet och tonen i GlazeFrame
//
// I 2D-VÄGEN (ett Ground-lager med föremål utan 3D-mål) ritas föremålen
// genom kartkamerans 3D-vy (`gfx::toView3D` över körningens höjder) och
// laddas upp i ritningen, som chunkarna där.
//
// EGNA PROGRAM: en stil med `program` ritar meshen
// eller punkterna med ritytans program med det namnet (resources.h, ett per
// version och fönster) — samma gallring, detaljnivå, intervall och anrop per
// segment, men med appens GlazeUser (stilens `uniforms`, float4-packat och
// utfyllt till programmets storlek) och texturer (`MeshStyle::textures`).
// En mesh får GlazeFrame med placeringens VRIDNING och SKALA invikta —
// matriserna tar meshens lokala lägen, och ögat och strålbasen står i samma
// ram (strålbasen oskalad: strålen genom en pixel är fortfarande ögat +
// t · (forward + x · right + y · up)). RESERVEN: saknas programmet, är det
// ogiltigt, läser det en plats som formens ström saknar, vill det ha fler
// texturer än stilen ger — eller är formen INSTANSER, vars prototyp bär sin
// placering i det inbyggda programmets block — ritas formen med det
// inbyggda programmet, loggat EN gång per rityta och namn
// (`SceneData::fallbackLogged`). En textur som väntar på bildcachens budget
// ritar ingenting den framen (som det inbyggda programmets).
//
// HELSKÄRMSPASSEN (`Shape::screenPass`): tre vertex utan ström
// (helskärmstriangeln), GlazeFrame med origot i ÖGAT och skalan (pxPerMm,
// uiScale, lagrets opacitet), GlazeUser och texturerna ur `PassStyle`. Ett
// pass som inte kan ritas fyller ritytans ruta med `PassStyle::fallback`
// (alfa 0 ritar ingenting), loggat en gång per rityta och namn.

#include <gfx/scene.h>

#include "camera_math.h"
#include "glaze/canvas/style3d.h"
#include "programs3d.h"
#include "scene.h"
#include "transform3d.h"

namespace glaze::detail {

// UPPLADDNINGENS KÖ (scene.h `prepareUploads`): ett föremål
// eller en bildkörning, avståndet från ögat och om lagret ritas — det som
// syns laddas upp först, närmast först, och resten med det som blir kvar.
struct UploadItem {
    double                             distance = 0.0;
    const std::shared_ptr<ShapeEntry>* object   = nullptr;   // ett föremål …
    const RunData*                     imageRun = nullptr;   // … eller en bildkörning
    bool                               draw     = false;     // lagret ritas
};
// Körningens föremål i kön: de som syns i vyn (`f.view3D`, i 2D-vägen
// kartkamerans 3D-vy över körningens höjder) med avståndet i `visible`,
// resten i `rest`. En skärmkonstant mesh räknas som synlig — dess låda är
// origot, och ritningen gallrar den med sin storlek.
void queueObjects(const PaintContext& f, const RunData& run, bool draw,
                  std::vector<UploadItem>& visible, std::vector<UploadItem>& rest);
// Förberedelsen av ett föremål (se filhuvudet): resident inom budgeten och,
// med `draw` (lagret ritas), det ritningen binder — texturen, rampen och ett
// eget program med dess texturer. `scene` bär ritytans egna program.
void prepareObject(const PaintContext& f, const SceneData& scene,
                   const std::shared_ptr<ShapeEntry>& e, bool draw);

// Körningens föremål ritade med renderläget `state` och lagrets opacitet
// `op` (tonen inräknad); utan `draw` hålls de bara residenta. `lf` är
// förberedelsens svar (labels.h — draperingarna).
void drawObjects(const PaintContext& f, const SceneData& scene,
                 const LayerFrame& lf, const RunData& run, float op, bool draw,
                 const gfx::RenderState& state);

// Helskärmspasset `s`: förberedelsen — programmet kompilerat och
// texturerna hämtade, före målet — och ritningen med lagrets opacitet `op`
// och renderläget `state` (djupet av). Bara i 3D-målet.
void prepareScreenPass(const PaintContext& f, const SceneData& scene,
                       const Shape& s);
void drawScreenPass(const PaintContext& f, const SceneData& scene,
                    const Shape& s, float op, const gfx::RenderState& state);

// GALLRINGEN (filhuvudet): syns någon del av lådans — eller de åtta
// hörnens — avtryck med `margin` fysiska px? Homogent mot synpyramidens sex
// plan: gallrad när alla hörn ligger utanför SAMMA plan (bakom kameran är
// bakom närplanet). Draperingen (drape.h) frågar samma för sina plattor.
bool cornersVisible(const Projection& proj, const Point3 (&c)[8], double margin);
bool boxVisible3(const Projection& proj, const Box3& b, double margin);

// STILEN OCH LJUSET i Mesh-programmets block: modellen (en affin 3 × 4 utan
// förflyttning för en mesh — anropets origo bär den —, instansens ram med
// prototypens förskjutning för instanser), färgen, ljuset (eller obelyst),
// rimljuset, dimman (eller ingen), opaciteten (klampad till 0–1; ett tal som
// inte är ändligt ger förvalet), färgjusteringen (mättnaden,
// kontrasten och ljusheten genom `gfx::adjustFactor`, som en bilds) och
// skärmstorleken.
// Texturens och rampens flaggor sätts av den som binder bilderna.
void meshUniforms(const MeshStyle& st, const Lighting& light,
                  const Affine3& model, MeshUniforms& u);

} // namespace glaze::detail
