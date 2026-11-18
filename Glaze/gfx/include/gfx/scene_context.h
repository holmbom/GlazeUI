#pragma once
#include <memory>

#include "gfx/image.h"

// SCEN-KONTEXTEN — en fristående renderare på SAMMA grafikkort som ett
// fönsters, gjord för att ritas med i en ANNAN TRÅD, och bilderna den lämnar
// till fönstret.
//
// Ett fönsters `Renderer` hör till tråden som pumpar fönstret. Det som ska
// ritas utan att den tråden väntar — ritytans scen — ritas därför med en egen
// renderare (`SceneContext::renderer()`) i en egen tråd, i en DELAD BILD
// (`SharedTarget`) som fönstrets renderare sedan ritar med `drawImage`.
//
//   D3D11  en andra enhet på fönsterenhetens adapter; bilden är en textur
//          delad med NT-handtag och keyed mutex
//   Metal  en egen kommandokö på fönstrets enhet; bilden är samma textur
//   GL     en kontext i fönsterkontextens delningsgrupp; bilden är en textur
//          i gruppen, lämnad över med en synkpunkt
//
// TRÅDARNA. Kontexten och dess delade bilder skapas och rivs av
// SCENTRÅDEN (den som ritar med kontextens renderare); `createSceneContext`
// och `createSharedTarget` läser bara det hos fönstrets renderare som inte
// ändras efter dess init. Allt som skapats ur `renderer()` hör till
// scentråden. Fönstrets tråd rör bara `SharedTarget::acquire`/`retire` och
// bilden `acquire` gav.
//
// LIVSTIDEN. Bilderna rivs före kontexten, kontexten före fönstrets
// renderare. Fönstrets tråd lämnar tillbaka (`retire`) varje bild den håller
// innan scentråden river den.

namespace gfx {

class Renderer;

// EN DELAD BILD: scen-sidan ritar i den, fönster-sidan visar den. Innehållet
// är PREMULTIPLICERAT, som varje render-måls. Bilden är alltid ett prov per
// pixel och utan djup — ett mål med MSAA eller djup ritas på scen-sidan och
// läggs in här med `drawImage`.
//
// EN ÄGARE I TAGET, i den här ordningen:
//
//   scen:    beginWrite → rita i target() (inom beginOffscreen/endOffscreen)
//            → publish
//   fönster: acquire → drawImage (över valfritt antal bilder) → retire
//
// En publicerad bild som fönstret aldrig tog kan skrivas om: `beginWrite` tar
// tillbaka den. En bild fönstret håller kan inte skrivas förrän den lämnats
// tillbaka — anroparen håller reda på vilka bilder som är lediga, och den som
// växlar mellan tre behöver aldrig vänta.
class SharedTarget {
public:
    virtual ~SharedTarget() = default;

    virtual int width() const  = 0;
    virtual int height() const = 0;

    // SCEN-SIDAN ─────────────────────────────────────────────────────────

    // Tar bilden för skrivning. Väntar högst en kort, fast tid på att
    // fönster-sidans sista läsning ska vara klar på GPU:n; false (+ logg) när
    // bilden hålls av fönstret eller väntan tog slut — då ritas ingenting.
    virtual bool beginWrite() = 0;

    // Målet att rita i mellan `beginWrite` och `publish` — bundet med
    // scen-renderarens `pushTarget`/`popTarget` i ett offscreen-block.
    virtual RenderTarget& target() = 0;

    // Lämnar bilden: anropas efter `endOffscreen`. GPU:ns arbete med bilden
    // VÄNTAS IN HÄR, på scentråden, så fönster-sidan aldrig väntar på det.
    virtual void publish() = 0;

    // FÖNSTER-SIDAN ───────────────────────────────────────────────────────

    // Tar den publicerade bilden för visning. VÄNTAR ALDRIG: nullptr när
    // bilden inte är publicerad (eller redan hålls). Bilden ritas med
    // fönstrets renderare och gäller tills `retire`.
    virtual Image* acquire() = 0;

    // Lämnar tillbaka en bild `acquire` gav — fönstret ritar den inte mer.
    // Utan en hållen bild görs ingenting.
    virtual void retire() = 0;
};

// Kontexten: en hel `Renderer` (samma fabriker, scenväg och render-mål som
// fönstrets) plus fabriken för de delade bilderna.
class SceneContext {
public:
    virtual ~SceneContext() = default;

    // Först respektive sist på scentråden, kring allt arbete med
    // `renderer()` (GL: gör kontexten aktuell på tråden och släpper den).
    // false (+ logg) när kontexten inte går att ta i bruk.
    virtual bool attachThread() = 0;
    virtual void detachThread() = 0;

    // Scen-sidans renderare. Den ritar BARA i offscreen-block
    // (`beginOffscreen`/`endOffscreen`) — den har inget fönster och ingen
    // frame.
    virtual Renderer& renderer() = 0;

    // En delad bild w × h fysiska px; nullptr (+ logg) när den inte går att
    // skapa.
    virtual std::unique_ptr<SharedTarget> createSharedTarget(int w, int h) = 0;
};

} // namespace gfx
