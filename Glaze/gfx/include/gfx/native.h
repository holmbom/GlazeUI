#pragma once
#include <cstdint>
#include <string>

#include "gfx/image.h"

// INTEGRATION MED ETT ANNAT GRAFIK-API — för den som ska dela renderarens
// enhet med ett bibliotek utanför ramverket (ett VR-API som tar emot
// bildrutor på den enhet appen ritar med). Allt här är OPAKT: pekarna är
// backendens egna objekt bakom `void*`, och bara den som vet vilket API
// `NativeGraphicsApi` säger får tolka dem. Inga backendtyper i fasaden.

namespace gfx {

enum class NativeGraphicsApi : std::uint8_t { None, D3D11 };

// Renderarens inbyggda enhet. `device` är LÅNAD — ingen referens tas och
// ingen ges bort — och lever lika länge som renderern. D3D11: en
// `ID3D11Device*`. Renderarens kontext lämnas aldrig ut: allt grafikarbete
// går genom renderern, i UI-tråden. `localAdapterId` är kortets id för den
// här körningen (D3D11: LUID:ens två halvor, HighPart i de övre 32 bitarna)
// och `featureLevel` API:ets nivå (D3D11: `D3D_FEATURE_LEVEL`). En renderare
// utan stöd svarar `api == None`.
struct NativeDevice {
    NativeGraphicsApi api            = NativeGraphicsApi::None;
    void*             device         = nullptr;
    std::uint64_t     localAdapterId = 0;
    std::uint32_t     featureLevel   = 0;
};

// Färgformatet i en EXTERN bild, så som renderern ska skriva den: de vanliga
// 8-bitarskanalerna utan sRGB-kodning. En bild som deklarerats som sRGB för
// en kompositor skrivs med motsvarande UNORM-vy — renderarens färger är
// redan sRGB-kodade och ska inte kodas en gång till.
enum class TargetColorFormat : std::uint8_t { Bgra8Unorm, Rgba8Unorm };

// En extern färgbild att rita i (`Renderer::importRenderTarget`). `image` ägs
// av anroparen och måste leva medan målet används. D3D11: en
// `ID3D11Texture2D*` på RENDERARENS enhet, ett prov per pixel, en nivå, med
// en TYPELESS- eller UNORM-familj som matchar `color` och bindbar både som
// render target och som shaderresurs (`image()` är en vanlig bild som går att
// rita). `target` säger vad målet
// ska ha utöver bilden: MSAA (ritas i ett eget provtaget mål och löses upp
// till bilden när målet lämnas) och djup (D32F). Mipmappar stöds inte.
struct ExternalTargetDesc {
    NativeGraphicsApi api    = NativeGraphicsApi::None;
    void*             image  = nullptr;
    int               width  = 0;
    int               height = 0;
    TargetColorFormat color  = TargetColorFormat::Bgra8Unorm;
    TargetDesc        target{};
};

enum class GpuCompletionState : std::uint8_t { Pending, Complete, Failed };

// En markör i renderarens kommandoström (`Renderer::insertCompletion`):
// `poll` svarar om GPU:n kört allt som skickades före markören — utan att
// vänta. `Failed` (med `error`) när enheten förlorats eller frågan inte gick
// att ställa. Släpps före renderern, i UI-tråden.
class GpuCompletion {
public:
    virtual ~GpuCompletion() = default;
    virtual GpuCompletionState poll(std::string& error) = 0;
};

} // namespace gfx
