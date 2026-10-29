#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gfx/color.h"
#include "gfx/geometry.h"
#include "gfx/image.h"
#include "gfx/scene.h"
#include "gfx/text.h"

namespace gfx {

// RENDERSTATISTIKEN — räknare som backendarna stegar
// DÄR arbetet görs: vid varje rit-anrop, programbyte och uppladdning. De
// nollställs ALDRIG av sig själva; den som mäter tar `resetStats()` före och
// läser `stats()` efter (grindarna, prestandautskriften).
//
// `bytesUploaded` räknar BARA scenens uppladdningsvägar — buffertarnas
// innehåll (createBuffer med data, updateBuffer, uploadTransient) och
// bildernas (createImage med pixlar, updateImage — och loadImage, som går
// genom createImage). UI-
// batcherns vertexström räknas INTE: den strömmas om varje frame per
// konstruktion och syns i `vertices`/`drawCalls`. Kamerans uniformer räknas
// inte heller — de är per rit-anrop och ska vara det. Det är det som gör
// "en kameraändring laddar inte upp något" till ett mätbart påstående.
struct RenderStats {
    std::uint32_t drawCalls       = 0;   // Draw/DrawIndexed, glDraw*, drawPrimitives
    std::uint32_t programSwitches = 0;   // ett ANNAT program togs i bruk mitt i en
                                         // frame (UI-pipelinen räknas som ett)
    std::uint32_t buffersCreated  = 0;   // lyckade createBuffer
    std::uint64_t bytesUploaded   = 0;   // byte genom scenens uppladdningsvägar
    std::uint64_t vertices        = 0;   // ritade vertex (index i indexerade anrop;
                                         // × instanserna i ett instansierat)
    // Ritade instanser i INSTANSIERADE rit-anrop (`MeshDraw::instanceCount`
    // > 0 i ett eget program). Ett vanligt rit-anrop
    // räknas inte — det är `drawCalls`.
    std::uint64_t instances       = 0;
    // Lyckade `createProgram` — giltiga program. Ett
    // ogiltigt program räknas inte, och inte heller backendens varianter
    // (D3D11:s input layouts, Metals PSO:er) — de är formen, inte programmet.
    std::uint32_t programsBuilt   = 0;
    // Uppladdningar som AVSLUTADE ett öppet render-pass (
    // Metal: en blit mitt i framen bryter render-encodern och startar om den
    // med Load). D3D11 och GL ordnar uppladdningen i kontexten och bryter
    // inget pass — alltid 0 där.
    std::uint32_t uploadBreaks    = 0;
    // PRESENTATIONENS VÄNTAN i ns: D3D11 `Present`, Metal
    // ring-semaforen och `nextDrawable`, GL `eglSwapBuffers` och Waylands
    // frame-callback-väntan (`beforeSwap`). Ett dolt fönster väntar inte på
    // vsync, så där är den nära 0.
    std::uint64_t presentNs       = 0;
    // KOMPILERINGENS tid i ns: D3D11 `D3DCompile` (en träff i processens
    // cache kostar ingenting), Metal `newLibraryWithSource` och
    // PSO-varianterna, GL programbyggena (kompileringen och länkningen).
    std::uint64_t compileNs       = 0;
    std::uint32_t targetsCreated  = 0;   // lyckade createRenderTarget
};

// GPU:NS TID FÖR EN FRAME— svaret i `Renderer::takeGpuTimes`.
struct GpuTime {
    std::uint64_t serial = 0;   // framens `Renderer::frameSerial`
    std::uint64_t ns     = 0;   // GPU:ns tid för framen
};

// RUNDAD KLIPPMASK— rect i fysiska px,
// radius > 0 (radie 0 vore en ren rektangel och tar aldrig den här vägen:
// den ligger redan exakt i scissorn). Masken utvärderas mot sin EGEN
// geometri och ALDRIG mot stackens snitt — snittet av två rundade
// rektanglar är inte en rundad rektangel.
struct RoundedClip {
    Rectf rect;
    float radius;
};

// Taket för samtidiga RUNDADE masker. Uniformarrayen
// loopas per FRAGMENT, så platserna är inte gratis; fyra räcker för
// panel-i-gruppram-i-flik-i-dockpanel. Rektangulära klipp (damage-recten,
// alla vanliga containrar) konsumerar ALDRIG en plats. En femte nivå
// degraderar till sin REKTANGEL — som redan är exakt i scissorn — plus en
// logg; aldrig läckage.
inline constexpr std::size_t kMaxRoundedClips = 4;

// Det render-oberoende rit-API:t — allt (widgets, appar, chrome-fasaden)
// ritar genom det här interfacet. Backends (backend_d3d11/backend_metal/
// backend_linux) implementerar det; ingen utanför backenden ser vad som
// ligger bakom.
//
// Ingen present() här — Window::renderFrame presenterar. SCENEN
// (createBuffer/drawMesh) står längst ned i den publika delen;
// djupet, MSAA och renderläget bor i render-målen och i `MeshDraw::state`,
// och de egna programmen i `createProgram`.
class Renderer {
public:
    virtual ~Renderer() = default;

    // Klipp-stack (fysiska px): all ritning klipps mot stackens topp.
    // pushClip intersekterar med aktuellt klipp — nästlade klipp kan
    // aldrig växa; popClip återställer föregående. Balansera anropen
    // (obalans är ett programfel; popClip på tom stack loggas och
    // ignoreras). Tom stack = ingen klippning. Stack-logiken är delad
    // (renderer.cpp) så semantiken är identisk i alla backends; backends
    // implementerar bara applyClip nedan.
    void pushClip(const Rectf& r);

    // RUNDAT KLIPP: samma rektangulära
    // klipp som ovan PLUS en rundad mask över r. radius <= 0 är exakt
    // överlagringen ovan — inget mask-arbete, ingen maskplats.
    //
    // Nästlingsinvarianten, skriven: scissorn är snittet av ALLA
    // klipprektanglar (även de rundades), så "klippet kan aldrig växa"
    // gäller oförändrat. Inne/ute för varje mask är EXAKT; approximationen
    // finns bara i AA-bandet (≤ 1 px), där produkten av två partiella
    // täckningar inte är exakt geometrisk area — samma klass av
    // approximation som all annan SDF-AA i renderaren.
    void pushClip(const Rectf& r, float radius);

    void popClip();

    // DET KLIPP SOM GÄLLER NU— stackens topp, alltså
    // snittet av alla nivåers rektanglar i fysiska px, före backendens
    // avrundning utåt; false när stacken är tom (ingen klippning). De
    // rundade maskerna ingår inte: svaret är scissorn. En fråga och inget
    // tillstånd — den som ritar något dyrt kan gallra mot den, och
    // overlay-vandringens grind (`tests/middle_gate.cpp`) mäter med den att
    // en widgets overlay klipps mot förfädernas klipp.
    bool clipBounds(Rectf& out) const;

    // Rensar HELA det aktiva målet (klippet gäller inte) till `c`, lagrad
    // premultiplicerad — och ett mål med djup får djupet 1.
    virtual void clear(Color c) = 0;
    virtual void fillRect(const Rectf& r, Color c) = 0;
    virtual void strokeRect(const Rectf& r, Color c) = 0;
    virtual void line(float x1, float y1, float x2, float y2, Color c) = 0;

    // Rikare 2D-primitiv — grunden för widget-utseendet. Alla mått är
    // fysiska px (logiska mått × uiScale före anropet), radius/width/blur
    // likaså. radius klampas till halva kortsidan; radius 0 = skarpa hörn.
    // D3D11 ritar dem analytiskt (SDF i pixel-shadern — skarpt vid alla
    // DPI); backends utan SDF-stöd approximerar med tesselerad geometri.
    // Fler primitiv (t.ex. gradienter) läggs till när en widget behöver dem.
    virtual void fillRoundedRect(const Rectf& r, float radius, Color c) = 0;

    // Konturen ritas med bredden width INNANFÖR r (som strokeRect:s
    // 1-px-stavar). Default-bredden ger den klassiska tunna ramen.
    virtual void strokeRoundedRect(const Rectf& r, float radius, Color c,
                                   float width = 1.0f) = 0;

    // Mjuk skugga UTANFÖR r (offset förskjuter, blur = mjuk kant på båda
    // sidor om skuggranden). Rita skuggan först, sedan fyllningen ovanpå.
    // c bär opaciteten i sin alfa (skuggfärg med låg alfa = diskret skugga).
    virtual void dropShadow(const Rectf& r, float radius,
                            float offsetX, float offsetY, float blur,
                            Color c) = 0;

    // Mjuk skugga INNANFÖR r:s kant (klipps mot r). offset förskjuter
    // skuggkällan — (0, positiv) ger klassiskt "nedsänkt" utseende.
    virtual void innerShadow(const Rectf& r, float radius,
                             float offsetX, float offsetY, float blur,
                             Color c) = 0;

    // Ritar UTF-8-text med övre vänstra hörnet i (x, y).
    // Returnerar textens bredd i pixlar.
    virtual float drawText(Font& f, float x, float y,
                           const std::string& utf8, Color c) = 0;

    // Bild-fabriker — returnerar alltid ett objekt (valid() avgör), som
    // font-fabrikerna. Filvarianten laddar en PNG-fil; minnesvarianten tar
    // en inbäddad resurs (rå fildata). Båda går genom `decodeImage` och dess
    // kontrakt: BARA PNG på alla plattformar (gfx/image.h). Bilden hör till
    // renderern — släpp den före fönstret (fontens livstidsregel).
    virtual std::unique_ptr<Image> loadImage(const std::string& path) = 0;
    virtual std::unique_ptr<Image> loadImage(const void* data,
                                             std::size_t size) = 0;

    // Ritar bilden till dst (fysiska px), hela eller en käll-rect (src i
    // bildens pixlar — sprites/atlas). Alfa blandas som allt annat.
    //
    // `opacity` (0–1, klampas) MULTIPLICERAR bildens egen alfa; 1 ritar den
    // som den är. Den finns för en INAKTIV ikon: tonad mot
    // VILKEN yta som helst. En slöja i ytans färg över bilden stämmer bara mot
    // en enfärgad yta i just den färgen — på en panel eller en gruppram syns
    // den som en ruta. EN virtuell, och
    // opaciteten är OBLIGATORISK i den (applyClip-prejudikatet: en ny
    // förmåga i fasaden ska ge kompileringsfel i varje implementation som
    // inte bär den, aldrig ett tyst löfte). Helbildsformen är en
    // icke-virtuell bekvämlighet ovanpå den.
    //
    // Bildens egenskaper följer med: FILTRET väljer
    // samplingen per ritning, en R8-bild ritas som gråskala, ett render-måls
    // premultiplicerade innehåll av-premultipliceras och en `flippedV`-bild
    // speglas — så ett mål ritas rättvänt och utan mörk frans.
    void drawImage(Image& img, const Rectf& dst, float opacity = 1.0f);
    virtual void drawImage(Image& img, const Rectf& src, const Rectf& dst,
                           float opacity) = 0;

    // Font-fabriker — returnerar alltid ett objekt (valid() avgör om
    // laddningen lyckades). Sökvägsvarianten laddar en godtycklig .ttf
    // (stilen är filens egen); familjevarianten löser en FontFamily (UI/Mono)
    // i STILEN `style` via plattformens kandidatlista i font_catalog.cpp —
    // appar hårdkodar aldrig sökvägar. Saknar familjen stilen laddas den
    // närmaste; `Font::style()` säger vilken. Stilen är
    // obligatorisk med flit: varje anropsställe säger vilken det menar.
    virtual std::unique_ptr<Font> loadFont(const std::string& path,
                                           float sizePt) = 0;
    virtual std::unique_ptr<Font> loadFont(FontFamily family, float sizePt,
                                           FontStyle style) = 0;

    // Bekvämlighet: UI-familjen i vanlig stil — fonten AppWindow ger alla
    // widgets.
    std::unique_ptr<Font> loadDefaultFont(float sizePt)
    {
        return loadFont(FontFamily::UI, sizePt, FontStyle::Regular);
    }

    // ---- SCENEN— buffertar som lever mellan frames och
    // program bredvid UI-pipelinen. Innehållet laddas upp EN gång; en
    // kameraändring är bara en ny matris i `MeshDraw` (gfx/camera.h).
    // Typerna och vertexformaten står i gfx/scene.h.

    // En buffert på GPU:n, fylld med `data` (oinitierad med nullptr).
    // Returnerar ALLTID ett objekt — `valid()` avgör, som för fonter och
    // bilder — och bufferten hör till renderern: släpp den före fönstret.
    virtual std::unique_ptr<GpuBuffer> createBuffer(BufferKind kind,
                                                    std::size_t bytes,
                                                    const void* data) = 0;

    // Skriver om [offset, offset + bytes) i en buffert ur `createBuffer`.
    // Det som redan ritats i framen behåller det gamla innehållet. offset
    // och bytes ska vara hela fyrbytes (en LineVertex är 40, ett index 4);
    // annars, eller utanför bufferten, false och ingenting skrivs.
    virtual bool updateBuffer(GpuBuffer& b, std::size_t offset,
                              const void* data, std::size_t bytes) = 0;

    // Kopierar `data` till renderarens TRANSIENTA ring och returnerar
    // delen — det omedelbara lägets väg (Painter), som bygger om sitt
    // innehåll varje frame. Anropas inom en frame; delen gäller till
    // framens slut. Tom del vid fel.
    virtual BufferSlice uploadTransient(BufferKind kind, const void* data,
                                        std::size_t bytes) = 0;

    // Ritar en mesh i det AKTIVA målet, under klippstacken (scissorn OCH
    // de rundade maskerna) och i målarordning med UI-primitiven: det som
// batchats före anropet ritas före. Programmen är `MeshProgram::Line` och
    // `MeshProgram::Image` (`ImageVertex` + `MeshDraw::image`, som
    // samplas med sitt eget filter); ett program som saknas loggas (en
    // gång) och ritas inte.
    //
    // RENDERLÄGET (`MeshDraw::state`) gäller BARA anropet:
    // nästa UI-primitiv ritas med UI:ts eget läge — inget djup, rak alfa,
    // ingen gallring — också i ett mål med djup (där D3D11:s förvalda
    // djupläge annars hade varit PÅ).
    //
    // `MeshProgram::Custom`ritar med ett EGET program
    // (`MeshDraw::shader`) — indexerat eller inte, instansierat eller inte —
    // under samma regler: klippstacken, målarordningen och renderläget.
    virtual void drawMesh(const MeshDraw& d) = 0;

    // EGNA PROGRAM (gfx/program.h): kompilerar källan för
    // DEN HÄR backenden (HLSL, GLSL eller MSL) med förspelet och
    // definitionerna. Returnerar ALLTID ett objekt — `valid()` avgör och
    // `error()` bär felet: en saknad källa ("programmet 'x' saknar GLSL-källa
    // (Linux)"), en ogiltig beskrivning eller kompilatorns text med radnummer
    // i appens egen källa. Ett ogiltigt program loggas EN gång, här, och ett
    // rit-anrop med det ritas inte. Programmet hör till renderern: släpp det
    // före fönstret. Får anropas utanför en frame.
    virtual std::unique_ptr<ShaderProgram> createProgram(
        const ProgramDesc& d) = 0;

    // ---- BILDER UR PIXLAR— plattor, rampar och
    // atlaser som appen eller scenen själv fyller. Formaten och filtret står
    // i gfx/image.h.

    // En bild på w × h px i formatet `f`, fylld ur `px` (rad 0 överst;
    // `stride` = byte mellan två rader, 0 = täta rader). `px` nullptr ger
    // en NOLLSTÄLLD bild (genomskinlig/svart). Returnerar ALLTID ett objekt
    // — `valid()` avgör — och bilden hör till renderern: släpp den före
    // fönstret. `stride` måste rymma en rad och vara en multipel av
    // pixelstorleken (annars ogiltig bild + logg). Mått över
    // `maxTextureSize()` ger en ogiltig bild (glaze delar en sådan Bitmap i
    // rutor, image_cache.h).
    //
    // `mipmaps`ger bilden en MIP-KEDJA, räknad på GPU:n ur nivå 0
    // (D3D11 `GenerateMips`, GL `glGenerateMipmap`, Metal-blittens
    // `generateMipmapsForTexture:`) — vid skapandet och efter varje
    // `updateImage`. Filtret gäller då också mellan nivåerna (gfx/image.h).
    // Kedjan är för det som ritas SKALAT (ritytans bilder); en atlas, en ramp
    // eller en datatextur ska inte ha någon.
    virtual std::unique_ptr<Image> createImage(int w, int h, ImageFormat f,
                                               const void* px,
                                               std::size_t stride,
                                               bool mipmaps) = 0;
    // Bekvämlighet: en bild utan mip-kedja.
    std::unique_ptr<Image> createImage(int w, int h, ImageFormat f,
                                       const void* px, std::size_t stride);

    // Skriver om rektangeln (x, y, w, h) i en bild ur `createImage` eller
    // `loadImage` med pixlar i BILDENS format (`stride` som ovan). Det som
    // redan ritats i framen behåller det gamla innehållet — samma kontrakt
    // som `updateBuffer`. Rektangeln utanför bilden, en tom rektangel, fel
    // stride eller ett render-måls bild (det ritas, det laddas inte upp)
    // ⇒ false och ingenting skrivs. En bild med mip-kedja får kedjan omräknad
    // efter skrivningen.
    virtual bool updateImage(Image& img, int x, int y, int w, int h,
                             const void* px, std::size_t stride) = 0;

    // GPU:ns STÖRSTA TEXTUR: den längsta sida en bild eller ett
    // render-mål får ha, i px — D3D11 ur funktionsnivån (16384 från 11_0),
    // GL `GL_MAX_TEXTURE_SIZE`, Metal 16384 (varje Mac-GPU i Metals
    // funktionstabell). Får frågas utanför en frame.
    virtual int maxTextureSize() const = 0;

    // ---- RENDER-MÅLEN— att rita i en bild och sedan
    // rita bilden: en dyr sammansättning EN gång, en symbol, en legend.

    // Ett mål på w × h FYSISKA px med formen `d` — MSAA och djup, se
    // `TargetDesc` (gfx/image.h) — och bilden nollställd (genomskinlig).
    // Returnerar ALLTID ett objekt — `image().valid()` avgör — och målet hör
    // till renderern: släpp det före fönstret. Bilden är premultiplicerad
    // (`Image::premultiplied`) och på GL speglad (`Image::flippedV`) —
    // ritvägarna sköter båda. Mått ≤ 0 eller en provtagning som inte är
    // 1/2/4/8 avvisas med en logg (`targetDescOk`).
    virtual std::unique_ptr<RenderTarget> createRenderTarget(
        int w, int h, const TargetDesc& d) = 0;
    // Bekvämlighet: 2D-målet — ett prov, inget djup (`TargetDesc{}`).
    std::unique_ptr<RenderTarget> createRenderTarget(int w, int h);

    // MSAA-provtagningen backenden klarar: den närmaste STÖDDA ≤ `requested`,
    // minst 1. Både färgformatet och djupets prövas, så svaret gäller ett mål
    // med och utan djup. D3D11 `CheckMultisampleQualityLevels`, GL
    // `GL_MAX_SAMPLES` (llvmpipe: 4), Metal `supportsTextureSampleCount:`.
    virtual int supportedSamples(int requested) const = 0;

    // LÄSER MÅLETS BILD till CPU:n: rå RGBA8, rad 0 överst, täta rader och RAK
    // alfa — målets premultiplicerade innehåll delas tillbaka — alltså
    // `encodePng`:s och `Window::captureFrame`:s form. Ett MSAA-mål läses
    // upplöst. Anropas UTANFÖR en frame (efter renderFrame eller endOffscreen)
    // och med målet obundet: Metal läser först när GPU:n kört klart, och mitt
    // i en frame är det arbetet inte ens inskickat. false (+ logg) för ett
    // ogiltigt mål, ett bundet mål och — på Metal — mitt i en frame.
    virtual bool readTarget(RenderTarget& t,
                            std::vector<std::uint8_t>& rgba) = 0;

    // RITNING UTANFÖR EN FRAME (3D till bild). Mellan
    // `beginOffscreen` och `endOffscreen` ritar man i RENDER-MÅL precis som i
    // en frame — pushTarget, rita, popTarget, balanserat — men framens eget
    // mål finns inte, så ingenting ritas utanför ett mål. `endOffscreen`
    // skickar arbetet till GPU:n (Metal: commit + vänta), så `readTarget`
    // efteråt ser det. Fönstret behöver aldrig ha ritat en frame (ett dolt
    // fönster duger). false (+ logg) inuti en frame eller ett redan öppnat
    // block; då ska `endOffscreen` inte heller anropas.
    //   D3D11  binder UI-pipelinens tillstånd (beginFrame gör det annars —
    //          före första framen är ingenting bundet)
    //   GL     gör kontexten aktuell
    //   Metal  öppnar en egen command buffer
    virtual bool beginOffscreen() = 0;
    virtual void endOffscreen()   = 0;

    // GPU:N renderaren ritar med — DXGI-adapterns beskrivning (D3D11),
    // `GL_RENDERER` (Linux) eller `MTLDevice.name` (Metal); tom i en renderare
    // utan GPU. Hur den valdes står vid `gfx::setGpuPreference`
    // (gfx/platform.h). Icke-virtuell: backenden fyller den när enheten
    // skapats.
    const std::string& deviceName() const { return deviceName_; }

    // KORTETS ID— `GpuAdapter::id` (gfx/platform.h) för
    // det kort som FAKTISKT ritar, alltså det `setGpuAdapter` jämförs mot.
    // Tomt när det inte går att avgöra: NullRenderer, mjukvarurendering utan
    // ett kort (llvmpipe), EGL utan EGL_EXT_device_query. Icke-virtuell:
    // backenden fyller det när enheten skapats.
    const std::string& adapterId() const { return adapterId_; }

    // FRAMENS LÖPNUMMER— stegat när en frame BÖRJAR och när
    // ett block utanför en frame öppnas (`beginOffscreen`), så 0 betyder
    // aldrig och varje frame har sitt eget. Under framen är det framens.
    // Icke-virtuellt: backenden stegar det.
    std::uint64_t frameSerial() const { return frameSerial_; }

    // GPU-TIDEN PER FRAME— av som förval. Påslagen mäts varje
    // frame som BÖRJAR medan den är på: D3D11 med tidsstämpelfrågor, Metal ur
    // commandbufferns GPUStartTime/GPUEndTime och GL med `GL_TIMESTAMP`.
    // Svaret kommer i efterhand, 2–3 frames senare: `takeGpuTimes` LÄGGER
    // TILL det som kommit sedan förra hämtningen i `out` och tömmer
    // brevlådan, som håller de `kMaxGpuTimes` senaste. En frame som GPU:n
    // inte kunde mäta (D3D11:s disjoint) får inget svar, och ett block
    // utanför en frame mäts aldrig. `gpuTimingSupported` säger om backenden
    // kan mäta alls — falskt i en renderare utan GPU (NullRenderer). Ingen av
    // dem är virtuell: backenden läser flaggan och lämnar svaren.
    static constexpr std::size_t kMaxGpuTimes = 64;
    void setGpuTiming(bool on) { gpuTiming_ = on; }
    bool gpuTiming() const { return gpuTiming_; }
    bool gpuTimingSupported() const { return gpuTimingSupported_; }
    void takeGpuTimes(std::vector<GpuTime>& out);

    // Ritningen går till `t` till matchande `popTarget`, inom en frame (eller
    // ett `beginOffscreen`-block) och balanserat (som klippen). KLIPPSTACKEN
    // HÖR TILL MÅLET: fönstrets rektanglar betyder ingenting i målets pixlar,
    // så `pushTarget` SPARAR och TÖMMER den och `popTarget` lägger tillbaka
    // den — klippet efter `popTarget` är exakt det som gällde före
    // `pushTarget`. Målet rensas till `c` (lagrad PREMULTIPLICERAD, som allt
    // i ett mål) och dess djup till 1. Mål kan nästlas; `popTarget` binder
    // det yttre målet igen, eller framens eget — och ett MSAA-mål löses upp
    // varje gång det lämnas (gfx/image.h). Målets bild får inte ritas medan
    // målet är bundet (loggas, ritas inte).
    void pushTarget(RenderTarget& t, Color c);
    void popTarget();

    // Renderstatistiken (se `RenderStats` ovan). Icke-virtuell: räknarna
    // bor här och stegas av backendarna, så en mätning läser samma fält
    // oavsett vilken backend som ligger bakom.
    const RenderStats& stats() const { return stats_; }
    void               resetStats() { stats_ = RenderStats{}; }

protected:
    // Backendarna stegar räknarna där arbetet faktiskt görs — i flush, i
    // rit-anropen och i uppladdningarna. Ingen annan skriver hit.
    RenderStats stats_{};

    // Enhetens namn och kortets id (`deviceName`, `adapterId`) — backenden
    // skriver dem när enheten finns.
    std::string deviceName_;
    std::string adapterId_;

    // Framens löpnummer och GPU-tidens flaggor (`frameSerial`,
    // `setGpuTiming`) — backenden stegar och läser dem.
    std::uint64_t frameSerial_        = 0;
    bool          gpuTiming_          = false;
    bool          gpuTimingSupported_ = false;

    // GPU-TIDERNAS BREVLÅDA. Delad (`shared_ptr`), så att ett svar som kommer
    // i en annan tråd — Metals completion handler — kan lämnas också efter att
    // renderaren rivits. `addGpuTime` lägger till under brevlådans lås och
    // släpper de äldsta över `kMaxGpuTimes`.
    struct GpuTimeBox {
        std::mutex           m;
        std::vector<GpuTime> times;
    };
    std::shared_ptr<GpuTimeBox> gpuTimes_ = std::make_shared<GpuTimeBox>();
    static void addGpuTime(GpuTimeBox& box, std::uint64_t serial,
                           std::uint64_t ns);
    void        addGpuTime(std::uint64_t serial, std::uint64_t ns)
    {
        addGpuTime(*gpuTimes_, serial, ns);
    }

    // TIDEN FÖR ETT BLOCK i en av räknarna ovan: blockets ns
    // läggs till `ns` när tidtagaren går ur scope — `presentNs` runt
    // presentationens väntan, `compileNs` runt en kompilering.
    class StatTimer {
    public:
        explicit StatTimer(std::uint64_t& ns)
            : ns_(ns), t0_(std::chrono::steady_clock::now())
        {
        }
        ~StatTimer()
        {
            ns_ += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0_)
                    .count());
        }
        StatTimer(const StatTimer&)            = delete;
        StatTimer& operator=(const StatTimer&) = delete;

    private:
        std::uint64_t&                        ns_;
        std::chrono::steady_clock::time_point t0_;
    };

    // Scenens DELADE regler (renderer.cpp) — samma i alla backends, så ett
    // anrop som avvisas i en avvisas i alla. Loggar EN gång per orsak och
    // renderer (ett felaktigt anrop görs typiskt varje frame) och svarar
    // false när anropet inte ska utföras. `programSupported` är backendens
    // svar på om den har `d.program`. Renderlägetprövas
    // också: uppräkningarna inom sina värden och ändliga biaser — och ett
    // djuptest i ett mål utan djup loggas en gång men RITAS (som Off).
    // `MeshProgram::Custom`har egna regler: ett giltigt program
    // och en giltig layout, strömmarna som layouten läser (hela vertex och
    // hela instanser), GlazeUser i programmets storlek och dess texturer.
    bool meshDrawOk(const MeshDraw& d, bool programSupported);
    bool updateOk(const GpuBuffer& b, std::size_t offset, const void* data,
                  std::size_t bytes);

    // EGNA PROGRAM, samma regler i alla backends.
    // `programDescError` prövar det i en ProgramDesc som inte beror på
    // backenden — GlazeUser-blockets storlek (en multipel av 16, högst
    // `kMaxProgramUserBytes`), texturantalet och definitionernas form — och
    // svarar med felet, tomt när beskrivningen duger. `programError` formar
    // varje fel som "programmet '<namn>' <vad>", så felen läses likadant i
    // alla tre. Backenden prövar sin egen källa, kompilerar, loggar ett
    // ogiltigt program EN gång och stegar `programsBuilt` för ett giltigt.
    static std::string programDescError(const ProgramDesc& d);
    static std::string programError(const ProgramDesc& d,
                                    const std::string& vad);
    // Källans namn i kompilatorns felrader (`#line 1 "<namn>"` i HLSL och
    // MSL): programmets namn med allt utom ASCII-bokstäver, siffror, _, - och
    // . ersatt med _, och "program" för ett namnlöst.
    static std::string programSourceName(const ProgramDesc& d);

    // Bildernas delade regler, samma form som ovan.
    // `imageDescOk` prövar en ny bilds mått, format och stride och svarar
    // med den EFFEKTIVA stride:n (0 ⇒ täta rader) i `rowBytes`;
    // `imageUpdateOk` prövar rektangeln, stride:n och att bilden inte är
    // ett render-mål.
    bool imageDescOk(int w, int h, ImageFormat f, std::size_t stride,
                     std::size_t& rowBytes);
    bool imageUpdateOk(const Image& img, int x, int y, int w, int h,
                       const void* px, std::size_t stride,
                       std::size_t& rowBytes);
    // En bilds egenskaper som SCENPROGRAMMEN läser dem — samma tal i HLSL,
    // GLSL och MSL: 1 premultiplicerad (ett render-mål), 2 speglad v (GL:s
    // mål), 4 R8 (gråskala). Image-programmet och Glyph-programmets
    // bildmarkörer.
    static float sceneImageFlags(const Image& img);
    // Image-programmets FÄRGJUSTERING som programmen läser den —
    // samma vektor i HLSL, GLSL och MSL: (mättnad, kontrast, ljushet, 0) ur
    // `SceneUniforms`, varje tal genom `adjustFactor` (gfx/scene.h).
    static void sceneImageAdjust(const SceneUniforms& u, float (&out)[4]);

    // RENDER-MÅLEN. `bindTarget` byter det AKTIVA målet: flushar
    // det som batchats (det hör till målet det ritades för), binder `t`
    // med sin viewport och storlek — nullptr = FRAMENS EGET mål (retained-
    // ytan, eller backbuffern efter compositen) — och lämnar klippet åt
    // `pushTarget`/`popTarget`, som tillämpar sitt efteråt. Backenden kallar
    // `beginFrameTargets()` i sin beginFrame: en obalanserad push från förra
    // framen får aldrig peka in i den här (loggas, stacken töms).
    // `imageDrawOk` är ritvägarnas vakt: false (en logg) för det BUNDNA
    // målets egen bild, som inte kan samplas medan den ritas i.
    virtual void bindTarget(RenderTarget* t) = 0;
    void         beginFrameTargets();
    bool         imageDrawOk(const Image& img);
    // Klippstackens topp EN GÅNG TILL genom `applyClip` — för en backend som
    // flyttat det bundna målets koordinater under en öppen stack (Linux:
    // compositen förskjuter backbufferns viewport med storleksmarginalen)
    // och ritat något eget utan klipp däremellan.
    void         reapplyClip();

    // MÅLENS FORM OCH LÄSNING, samma regler i alla
    // backends. `targetDescOk` avvisar mått ≤ 0 och en provtagning som inte är
    // 1/2/4/8; `readTargetOk` ett ogiltigt mål och ett som ligger i målstacken
    // (bundet, eller ett yttre mål under ett nästlat). Loggar en gång per orsak.
    bool targetDescOk(int w, int h, const TargetDesc& d);
    bool readTargetOk(RenderTarget& t);
    // Målet som är BUNDET nu — toppen av målstacken, nullptr = framens eget.
    // pushTarget lägger på målet och popTarget tar bort det FÖRE `bindTarget`,
    // så under bindTarget är det redan det nya målet; det utgående får
    // backenden hålla reda på själv.
    RenderTarget* boundTarget() const;
    // Ligger `t` kvar i målstacken? Ett mål som lämnas för ett NÄSTLAT mål
    // gör det (man kommer tillbaka), ett som poppas inte — Metal behåller
    // provtagningen bara i det första fallet.
    bool targetInStack(const RenderTarget& t) const;

    // Backendens klipptillämpning. `scissor` är redan intersekterat
    // (stackens topp) eller nullptr = ingen klippning. Rektangeln rundas
    // UTÅT till hela pixlar (floor/ceil) i backenden så delvis täckta
    // pixlar ingår — samma expansion i alla backends. D3D11 måste flusha
    // batchen före bytet (scissor är context-state).
    //
    // `masks`/`count` är ALLA aktiva RUNDADE masker i stacken, YTTERSTA
    // först, högst `kMaxRoundedClips` stycken. count == 0 är det vanliga
    // fallet och måste vara gratis: shadern tar en uniform gren,
    // vertexformatet är oförändrat och uniformen sätts i den flush
    // `applyClip` ändå gör.
    //
    // SIGNATUREN ÄR BRYTANDE MED FLIT (setSizeLimits-prejudikatet): alla
    // implementationer tvingas till kompileringsfel i stället för tysta
    // löften om en mask de inte ritar.
    virtual void applyClip(const Rectf* scissor,
                           const RoundedClip* masks, std::size_t count) = 0;

private:
    // En nivå i stacken. `scissor` är snittet hela vägen upp (det backends
    // scissor-rect sätts till), medan `mask`/`radius` är nivåns EGEN
    // geometri — masken får aldrig härledas ur snittet.
    struct ClipLevel {
        Rectf scissor;
        Rectf mask;
        float radius;   // <= 0 = rent rektangulär nivå, ingen maskplats
    };

    // Samlar de yttersta aktiva maskerna och anropar applyClip.
    void applyTop();

    // `meshDrawOk` för `MeshProgram::Custom`, och
    // svansen de två delar: `why` nullptr godkänner anropet (och loggar EN
    // gång ett djuptest i ett mål utan djup), annars loggas orsaken en gång
    // per bit och anropet avvisas.
    bool customDrawOk(const MeshDraw& d, bool programSupported);
    bool meshVerdict(const MeshDraw& d, const char* why, std::uint32_t bit);

    std::vector<ClipLevel> clipStack_;
    bool                   maskOverflowLogged_ = false;
    std::uint32_t          sceneLogged_ = 0;   // scenens loggade orsaker (bitar)

    // Målstacken: varje nivå är ett bundet mål och klippstacken
    // som gällde UTANFÖR det — den `popTarget` lägger tillbaka.
    struct TargetLevel {
        RenderTarget*          target;
        std::vector<ClipLevel> outerClips;
    };
    std::vector<TargetLevel> targetStack_;
};

} // namespace gfx
