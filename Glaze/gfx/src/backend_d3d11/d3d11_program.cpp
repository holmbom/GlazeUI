// Egna program i D3D11-backenden (gfx/program.h): appens
// HLSL kompilerad i körtid med FÖRSPELET före och definitionerna genom
// D3DCompile, och `MeshProgram::Custom` — indexerat eller inte, instansierat
// eller inte, med strömmarnas form per rit-anrop.
//
//   * vs_4_0/ps_4_0 — feature level 10, som resten av backenden.
//   * Platserna är semantiken ATTR<N>. Input layouten byggs per program och
//     `layout.key()` och cachas i programmet — också en som INTE gick att
//     bygga, så att det loggas en gång och inte varje frame. Att layouten
//     passar programmet har `customDrawOk` redan prövat (reflektionen nedan);
//     CreateInputLayout är det sista skyddet, inte regeln.
//   * Instansströmmen ligger i slot 1 (D3D11_INPUT_PER_INSTANCE_DATA) och
//     binds MED OFFSET — StartInstanceLocation är alltid 0.
//     SV_InstanceID räknar därför från delens början.
//   * GlazeFrame i b3 (renderarens buffert), GlazeUser i b4 (programmets,
//     DYNAMIC) och texturerna på t0–t3/s0–s3 — i BÅDA stegen. b0–b2 är UI:ts
//     och scenens och rörs inte.
//   * REFLEKTIONEN (D3DReflect) ger vertexstegets platser och slag
//     (`ShaderProgram::inputs`), och prövar att källans GlazeUser ligger i b4
//     och är exakt `ProgramDesc::userBytes` stort och att texturerna och
//     samplerna ligger under `ProgramDesc::textures` — annars ett läsbart fel
//     när programmet skapas, i stället för nollor ur en obunden resurs.

#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// FÖRSPELET (gfx/program.h) — GlazeFrame som `glaze` i b3 och glaze_toClip,
// och därefter färgjusteringen (`kHlslAdjustSrc` nedan) och de
// skärmvända hörnens djup (`kHlslStandSrc`).
// Blocket är en struct i cbuffern så att medlemmarna heter `glaze.…` som i
// GLSL och MSL; packningen är densamma (en struct börjar på en hel
// 16-byte-rad, matriserna är column_major som scenens). Efter förspelet
// står `#line 1 "<namn>"` (createProgram): appens källa börjar på rad 1 i
// kompilatorns felrader, under programmets namn — utan namnet gör D3DCompile
// källan till en sökväg under arbetskatalogen.
const char kHlslPrelude[] = R"hlsl(
struct GlazeFrameData
{
    float4x4 viewProj;
    float4x4 invViewProj;
    float4   eye;
    float4   forward;
    float4   right;
    float4   up;
    float4   target;
    float4   scale;
    float4   pixelSize;
};
cbuffer GlazeFrame : register(b3)
{
    GlazeFrameData glaze;
};
// Kamerans pixelklipp (x/w och y/w i målets px, z/w i [0, 1]) → D3D:s
// klipprymd. Linjär — w går orört vidare till rasteriseringen.
float4 glaze_toClip(float4 c)
{
    return float4(c.x * 2.0 * glaze.target.z - c.w,
                  c.w - c.y * 2.0 * glaze.target.w, c.z, c.w);
}
)hlsl";

} // namespace

// FÄRGJUSTERINGEN (gfx/program.h) — förspelets andra del och
// Image-programmets (d3d11_image.cpp). Ljusheten är c·k och kontrasten och
// mättnaden c·k + p·(1 − k), så (1, 1, 1) är EXAKT identiteten också när
// kompilatorn gör en mad av stegen.
const char kHlslAdjustSrc[] = R"hlsl(
// Färgjusteringen av en RAK färg 0–1: a = (mättnad, kontrast, ljushet) —
// ljusheten multiplicerar, kontrasten sträcker kring mittgrått 0,5 och
// mättnaden blandar mot luminansen (Rec. 709), klampad efter varje steg.
float3 glaze_colorAdjust(float3 c, float3 a)
{
    c = saturate(c * a.z);
    c = saturate(c * a.y + 0.5 * (1.0 - a.y));
    const float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    return saturate(c * a.x + lum * (1.0 - a.x));
}
)hlsl";

// DE SKÄRMVÄNDA HÖRNENS DJUP (gfx/program.h; markdjupet på ritytan)
// — förspelets tredje del och Line-, Glyph- och Points-programmens. Matrisen
// är ett argument: scenens program har `gViewProj` (b2), förspelets `glaze.
// viewProj` (b3).
const char kHlslStandSrc[] = R"hlsl(
// ETT SKÄRMVÄNT HÖRNS DJUP: hörnet `off` px från ankarets pixelklipp `c`
// (målets px, y nedåt; `m` matrisen som gav det) får djupet av PLANET GENOM
// ANKARET PARALLELLT MED XY-PLANET där det ligger närmare än ankaret — så
// skär ett plan som primitiven står på (marken z = 0, ritytans markdjup) den
// aldrig. Över quaden interpolerat ligger den då hela framför planet (kordan
// under en konkav funktion), och ovanför ankaret har den ankarets djup.
// Punkten på planet: a·(Mx.xy − P·Mx.w) + b·(My.xy − P·My.w) = off·c.w, med
// Mx och My matrisens x- och y-kolumner och P hörnets px. Planet sett från
// kanten, bakom kameran eller ett ankare bortom djupintervallet: ankarets
// djup. DRAGET framåt är en ANDEL av ankarets djup (w är djupet längs
// blicken): helt upp till 1/4, inget från 1/2 och linjärt däremellan — ett
// plan som nästan går genom ögat drar aldrig hörnet mot närplanet, där det
// hade vunnit djuptestet mot allt. Ortografiskt är w 1 och draget
// alltid helt.
float glaze_standingZ(float4x4 m, float4 c, float2 off)
{
    if (c.w <= 0.0 || c.z > c.w)
        return c.z;
    const float4 mx  = mul(m, float4(1.0, 0.0, 0.0, 0.0));
    const float4 my  = mul(m, float4(0.0, 1.0, 0.0, 0.0));
    const float2 p   = c.xy / c.w + off;
    const float2 u   = mx.xy - p * mx.w;
    const float2 v   = my.xy - p * my.w;
    const float  det = u.x * v.y - u.y * v.x;
    if (abs(det) <= 1e-6 * length(u) * length(v))
        return c.z;
    const float2 r  = off * c.w;
    const float  a  = (r.x * v.y - r.y * v.x) / det;
    const float  b  = (u.x * r.y - u.y * r.x) / det;
    const float  wq = c.w + a * mx.w + b * my.w;
    if (wq <= 1e-6 * c.w)
        return c.z;
    const float zq   = (c.z + a * mx.z + b * my.z) / wq;
    const float z0   = c.z / c.w;
    const float drag = 1.0 - wq / c.w;
    const float k    = saturate((0.5 - drag) / 0.25);
    return lerp(z0, min(z0, max(zq, 0.0)), k) * c.w;
}
)hlsl";

namespace {

DXGI_FORMAT dxgiFormatOf(AttribFormat f)
{
    switch (f) {
    case AttribFormat::Float:    return DXGI_FORMAT_R32_FLOAT;
    case AttribFormat::Float2:   return DXGI_FORMAT_R32G32_FLOAT;
    case AttribFormat::Float3:   return DXGI_FORMAT_R32G32B32_FLOAT;
    case AttribFormat::Float4:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case AttribFormat::UNorm8x4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case AttribFormat::SNorm8x4: return DXGI_FORMAT_R8G8B8A8_SNORM;
    case AttribFormat::UInt8x4:  return DXGI_FORMAT_R8G8B8A8_UINT;
    case AttribFormat::UInt:     return DXGI_FORMAT_R32_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

// Programmets löpnummer — aldrig återanvänt, till skillnad från en adress.
std::uint64_t nextProgramSerial()
{
    static std::uint64_t n = 0;
    return ++n;
}

ComPtr<ID3D11ShaderReflection> reflect(ID3DBlob* blob)
{
    ComPtr<ID3D11ShaderReflection> refl;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_ID3D11ShaderReflection,
                          reinterpret_cast<void**>(refl.GetAddressOf()))))
        refl.Reset();
    return refl;
}

// VERTEXSTEGETS INDATA ur signaturen: ATTR<N> är platsen N, och
// komponenttypen säger heltal (UINT32/SINT32) eller flyttal. Systemvärdena
// (SV_VertexID, SV_InstanceID) läser ingen ström. En annan semantik än ATTR
// eller en plats utanför 0–15 är ett fel — strömmarna adresseras bara med
// ATTR<N>, och layouten kan aldrig ge något annat. Svarar med felet utan
// programmets namn, tomt när signaturen duger.
std::string inputSignature(ID3DBlob* vs, std::uint32_t& inputs,
                           std::uint32_t& integer)
{
    inputs = integer = 0;
    ComPtr<ID3D11ShaderReflection> refl = reflect(vs);
    D3D11_SHADER_DESC              sd{};
    if (!refl || FAILED(refl->GetDesc(&sd)))
        return "kunde inte reflekteras (D3DReflect, vertexsteget)";
    for (UINT i = 0; i < sd.InputParameters; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC p{};
        if (FAILED(refl->GetInputParameterDesc(i, &p)) ||
            p.SystemValueType != D3D_NAME_UNDEFINED)
            continue;
        const std::string sem = p.SemanticName ? p.SemanticName : "";
        if (_stricmp(sem.c_str(), "ATTR") != 0)
            return "läser semantiken " + sem + std::to_string(p.SemanticIndex) +
                   " — ett eget program adresserar strömmarna med ATTR<N>, "
                   "där N är platsen";
        if (p.SemanticIndex >= attrib::kCount)
            return "läser ATTR" + std::to_string(p.SemanticIndex) +
                   " — platserna är 0–" + std::to_string(attrib::kCount - 1);
        inputs |= 1u << p.SemanticIndex;
        if (p.ComponentType == D3D_REGISTER_COMPONENT_UINT32 ||
            p.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
            integer |= 1u << p.SemanticIndex;
    }
    return {};
}

// RESURSERNA i ETT stegs källa: texturerna och samplerna på platser under
// `textures` (en plats utanför hade samplat en obunden vy — nollor, tyst),
// inga andra cbuffrar än GlazeFrame och GlazeUser (en lös global hamnar i
// `$Globals` på b0, där UI:ts viewport-buffert sitter) och GlazeUser i b4 och
// exakt `userBytes` stort. Svarar med felet utan programmets namn
// (anroparen lägger till det), tomt när resurserna stämmer — ett oanvänt
// block eller en oanvänd textur tar kompilatorn bort.
std::string resourceError(ID3DBlob* blob, const char* steg,
                          std::uint32_t userBytes, int textures)
{
    ComPtr<ID3D11ShaderReflection> refl = reflect(blob);
    D3D11_SHADER_DESC              sd{};
    if (!refl || FAILED(refl->GetDesc(&sd)))
        return std::string("kunde inte reflekteras (D3DReflect, ") + steg + ")";
    for (UINT i = 0; i < sd.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC r{};
        if (FAILED(refl->GetResourceBindingDesc(i, &r)))
            continue;
        const std::string namn = r.Name ? r.Name : "?";
        if (r.Type == D3D_SIT_CBUFFER && namn != "GlazeFrame" &&
            namn != "GlazeUser") {
            // $Globals bär de lösa globalerna — felet nämner den första.
            std::string                  vad = "cbuffern " + namn;
            ID3D11ShaderReflectionVariable* v =
                refl->GetConstantBufferByName(namn.c_str())
                    ->GetVariableByIndex(0);
            D3D11_SHADER_VARIABLE_DESC vd{};
            if (namn == "$Globals" && v && SUCCEEDED(v->GetDesc(&vd)) &&
                vd.Name)
                vad = std::string("den lösa globalen ") + vd.Name +
                      " (i $Globals, register b0)";
            return "har " + vad + " i " + steg +
                   " — ett eget programs data går i GlazeUser (b4)";
        }
        if ((r.Type == D3D_SIT_TEXTURE || r.Type == D3D_SIT_SAMPLER) &&
            r.BindPoint + r.BindCount > static_cast<UINT>(textures))
            return "samplar " + namn + " (" +
                   (r.Type == D3D_SIT_TEXTURE ? "t" : "s") +
                   std::to_string(r.BindPoint) + ") i " + steg +
                   ", men ProgramDesc::textures är " + std::to_string(textures);
    }
    D3D11_SHADER_INPUT_BIND_DESC bind{};
    if (FAILED(refl->GetResourceBindingDescByName("GlazeUser", &bind)))
        return {};
    if (bind.Type != D3D_SIT_CBUFFER || bind.BindPoint != 4)
        return std::string("har GlazeUser i register(b") +
               std::to_string(bind.BindPoint) + ") i " + steg +
               " — blocket ska ligga i register(b4)";
    D3D11_SHADER_BUFFER_DESC cb{};
    ID3D11ShaderReflectionConstantBuffer* c =
        refl->GetConstantBufferByName("GlazeUser");
    if (!c || FAILED(c->GetDesc(&cb)))
        return {};
    if (cb.Size != userBytes)
        return "har GlazeUser på " + std::to_string(cb.Size) + " byte i " +
               steg + ", men ProgramDesc::userBytes är " +
               std::to_string(userBytes);
    return {};
}

// Programmet: shadrarna, vertexshaderns bytekod (input layouterna byggs mot
// dess signatur), GlazeUser-bufferten och CACHEN av input layouter per
// layoutnyckel. Cachen är `mutable`: att bygga en input layout ändrar inte
// programmet, och `MeshDraw::shader` är const.
class D3dShaderProgram final : public ShaderProgram {
public:
    explicit D3dShaderProgram(const ProgramDesc& d)
        : ShaderProgram(d), serial(nextProgramSerial())
    {
    }

    bool               valid() const override { return vs && ps; }
    const std::string& error() const override { return err; }

    // Vertexstegets indata (`ShaderProgram::inputs`) — ur signaturen.
    void reflected(std::uint32_t inputs, std::uint32_t integer)
    {
        setInputs(inputs, integer);
    }

    // Input layouten för `l` — cachad efter nyckeln, också när den inte gick
    // att bygga (nullptr; `failedNow` är sant just den gången, så att
    // anroparen loggar en gång). En TOM layout har ingen: D3D11 vägrar en
    // input layout utan element (E_INVALIDARG), och helskärmspasset
    // ritas med ingen alls bunden — `meshDrawOk` har redan krävt att
    // programmet då inte läser någon plats.
    ID3D11InputLayout* inputLayout(ID3D11Device* dev, const VertexLayout& l,
                                   bool& failedNow, HRESULT& hr) const
    {
        failedNow = false;
        if (l.attribs.empty())
            return nullptr;
        const std::uint64_t key = l.key();
        if (auto it = layouts.find(key); it != layouts.end())
            return it->second.Get();
        std::vector<D3D11_INPUT_ELEMENT_DESC> el;
        el.reserve(l.attribs.size());
        for (const VertexAttrib& a : l.attribs)
            el.push_back(D3D11_INPUT_ELEMENT_DESC{
                "ATTR", a.location, dxgiFormatOf(a.format), a.stream, a.offset,
                a.stream ? D3D11_INPUT_PER_INSTANCE_DATA
                         : D3D11_INPUT_PER_VERTEX_DATA,
                a.stream ? 1u : 0u});
        ComPtr<ID3D11InputLayout> il;
        hr = dev->CreateInputLayout(el.data(), static_cast<UINT>(el.size()),
                                    vsBlob->GetBufferPointer(),
                                    vsBlob->GetBufferSize(), &il);
        if (FAILED(hr)) {
            il.Reset();
            failedNow = true;
        }
        ID3D11InputLayout* raw = il.Get();
        layouts.emplace(key, std::move(il));
        return raw;
    }

    const std::uint64_t        serial;
    std::string                err;
    ComPtr<ID3DBlob>           vsBlob;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader>  ps;
    ComPtr<ID3D11Buffer>       userCb;   // b4, userBytes stor — nullptr utan block

private:
    mutable std::unordered_map<std::uint64_t, ComPtr<ID3D11InputLayout>>
        layouts;
};

bool layoutReads(const VertexLayout& l, unsigned stream)
{
    for (const VertexAttrib& a : l.attribs)
        if (a.stream == stream)
            return true;
    return false;
}

} // namespace

// ------------------------------------------------------------------ livet

bool D3dRenderer::initProgramSupport()
{
    // GlazeFrame (b3): en DYNAMIC buffert för alla egna program, skriven per
    // rit-anrop — 240 byte, redan en multipel av 16.
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth      = sizeof(FrameUniforms);
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &frameCbuf_))) {
        log("D3D11: GlazeFrame-bufferten (b3) kunde inte skapas — drawMesh "
            "ritar inga egna program");
        frameCbuf_.Reset();
        return false;
    }
    return true;
}

void D3dRenderer::shutdownProgramSupport()
{
    frameCbuf_.Reset();
    curProgram_ = 0;
    customSrvs_ = 0;
}

// ---------------------------------------------------------------- program

std::unique_ptr<ShaderProgram> D3dRenderer::createProgram(const ProgramDesc& d)
{
    auto        p   = std::make_unique<D3dShaderProgram>(d);
    std::string err = programDescError(d);
    if (err.empty() && d.hlsl.empty())
        err = programError(d, "saknar HLSL-källa (Windows)");
    if (err.empty() && !device_)
        err = programError(d, "kunde inte skapas — renderaren har ingen enhet");

    ComPtr<ID3DBlob> vsb, psb;
    if (err.empty()) {
        // Definitionerna som D3D_SHADER_MACRO, nullterminerade; förspelet
        // FÖRE appens källa, och källans namn i kompilatorns felrader.
        std::vector<D3D_SHADER_MACRO> macros;
        macros.reserve(d.defines.size() + 1);
        for (const auto& [namn, värde] : d.defines)
            macros.push_back(D3D_SHADER_MACRO{namn.c_str(), värde.c_str()});
        macros.push_back(D3D_SHADER_MACRO{nullptr, nullptr});
        const std::string src = std::string(kHlslPrelude) + kHlslAdjustSrc +
                                kHlslStandSrc + "#line 1 \"" +
                                programSourceName(d) + "\"\n" +
                                d.hlsl;
        std::string text;
        vsb = compileHlsl(src, d.vertexEntry.c_str(), "vs_4_0", macros.data(),
                          &text);
        if (!vsb) {
            err = programError(d, "kunde inte kompileras (HLSL, " +
                                      d.vertexEntry + ", vs_4_0):\n" + text);
        } else {
            psb = compileHlsl(src, d.fragmentEntry.c_str(), "ps_4_0",
                              macros.data(), &text);
            if (!psb)
                err = programError(d, "kunde inte kompileras (HLSL, " +
                                          d.fragmentEntry + ", ps_4_0):\n" +
                                          text);
        }
    }
    std::uint32_t inputs = 0, integer = 0;
    if (err.empty()) {
        std::string vad = inputSignature(vsb.Get(), inputs, integer);
        if (vad.empty())
            vad = resourceError(vsb.Get(), "vertexsteget", d.userBytes,
                                d.textures);
        if (vad.empty())
            vad = resourceError(psb.Get(), "fragmentsteget", d.userBytes,
                                d.textures);
        if (!vad.empty())
            err = programError(d, vad);
    }

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader>  ps;
    ComPtr<ID3D11Buffer>       userCb;
    if (err.empty() &&
        (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(),
                                            vsb->GetBufferSize(), nullptr,
                                            &vs)) ||
         FAILED(device_->CreatePixelShader(psb->GetBufferPointer(),
                                           psb->GetBufferSize(), nullptr,
                                           &ps))))
        err = programError(d, "kunde inte skapas (CreateVertexShader/"
                              "CreatePixelShader)");
    if (err.empty() && d.userBytes > 0) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = d.userBytes;   // en multipel av 16 (programDescError)
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &userCb)))
            err = programError(d, "kunde inte skapas (GlazeUser-bufferten, b4)");
    }

    if (!err.empty()) {
        p->err = err;
        log("%s", err.c_str());   // EN gång — här, aldrig per rit-anrop
        return p;
    }
    p->vsBlob = std::move(vsb);
    p->vs     = std::move(vs);
    p->ps     = std::move(ps);
    p->userCb = std::move(userCb);
    p->reflected(inputs, integer);
    ++stats_.programsBuilt;
    return p;
}

// ---------------------------------------------------------------- ritning

void D3dRenderer::drawCustomMesh(const MeshDraw& d)
{
    if (!context_ || !meshDrawOk(d, frameCbuf_ != nullptr))
        return;
    // Programmet kommer alltid ur den här backenden (createProgram) — casten
    // är säker per konstruktion, som drawText:s Font.
    const auto&         p = static_cast<const D3dShaderProgram&>(*d.shader);
    const VertexLayout& l = *d.layout;

    bool    failedNow = false;
    HRESULT hr        = S_OK;
    ID3D11InputLayout* il =
        p.inputLayout(device_.Get(), l, failedNow, hr);
    if (!il && !l.attribs.empty()) {
        if (failedNow)
            log("drawMesh: programmet '%s' läser en plats som layouten saknar "
                "eller har av fel slag (CreateInputLayout 0x%08lx) — anropet "
                "ritas inte",
                p.name().c_str(), hr);
        return;
    }

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    if (pipeline_ != Pipeline::Custom || curProgram_ != p.serial) {
        context_->VSSetShader(p.vs.Get(), nullptr, 0);
        context_->PSSetShader(p.ps.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(3, 1, frameCbuf_.GetAddressOf());
        context_->PSSetConstantBuffers(3, 1, frameCbuf_.GetAddressOf());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_   = Pipeline::Custom;
        curProgram_ = p.serial;
        ++stats_.programSwitches;
    }
    context_->IASetInputLayout(il);

    // GlazeFrame: anroparens block med det BUNDNA målets storlek i `target`
    // — vyn vet bara sin ruta, backenden målets mått.
    FrameUniforms f = d.frame;
    f.target[0]     = static_cast<float>(targetW_);
    f.target[1]     = static_cast<float>(targetH_);
    f.target[2]     = targetW_ > 0 ? 1.0f / static_cast<float>(targetW_) : 0.0f;
    f.target[3]     = targetH_ > 0 ? 1.0f / static_cast<float>(targetH_) : 0.0f;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(frameCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &m)))
        return;
    std::memcpy(m.pData, &f, sizeof f);
    context_->Unmap(frameCbuf_.Get(), 0);

    // GlazeUser (b4) — programmets egen buffert, exakt userBytes stor
    // (meshDrawOk har prövat anropets storlek mot den).
    if (p.userCb) {
        if (FAILED(context_->Map(p.userCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                 &m)))
            return;
        std::memcpy(m.pData, d.user, d.userBytes);
        context_->Unmap(p.userCb.Get(), 0);
        context_->VSSetConstantBuffers(4, 1, p.userCb.GetAddressOf());
        context_->PSSetConstantBuffers(4, 1, p.userCb.GetAddressOf());
    }

    // Texturerna på t0… med var sin bilds filter, i båda stegen. De sitter
    // kvar efter anropet; bindTarget nollar dem innan ett mål binds.
    const int n = p.textures();
    if (n > 0) {
        ID3D11ShaderResourceView* srv[kMaxProgramTextures] = {};
        ID3D11SamplerState*       smp[kMaxProgramTextures] = {};
        for (int i = 0; i < n; ++i) {
            const Image* img = d.textures[static_cast<std::size_t>(i)];
            srv[i] = static_cast<const D3dImage*>(img)->srv();
            smp[i] = samplerFor(img->filter());
        }
        const UINT un = static_cast<UINT>(n);
        context_->VSSetShaderResources(0, un, srv);
        context_->PSSetShaderResources(0, un, srv);
        context_->VSSetSamplers(0, un, smp);
        context_->PSSetSamplers(0, un, smp);
        customSrvs_ = std::max(customSrvs_, un);
    }
    applyMeshState(d.state);   // renderläget

    // Strömmarna: slot 0 vertex, slot 1 instans — instansdelen MED OFFSET.
    // En ström layouten inte läser binds tom.
    ID3D11Buffer* bufs[2]    = {};
    UINT          strides[2] = {};
    UINT          offsets[2] = {};
    if (layoutReads(l, 0)) {
        bufs[0]    = static_cast<const D3dGpuBuffer*>(d.vertices.buffer)->buffer();
        strides[0] = l.stride[0];
        offsets[0] = static_cast<UINT>(d.vertices.offset);
    }
    if (layoutReads(l, 1)) {
        bufs[1]    = static_cast<const D3dGpuBuffer*>(d.instances.buffer)->buffer();
        strides[1] = l.stride[1];
        offsets[1] = static_cast<UINT>(d.instances.offset);
    }
    context_->IASetVertexBuffers(0, 2, bufs, strides, offsets);

    const UINT inst = d.instanceCount;
    UINT       count = 0;
    if (d.indexCount > 0) {
        count = d.indexCount;
        context_->IASetIndexBuffer(
            static_cast<const D3dGpuBuffer*>(d.indices.buffer)->buffer(),
            DXGI_FORMAT_R32_UINT, static_cast<UINT>(d.indices.offset));
        if (inst > 0)
            context_->DrawIndexedInstanced(count, inst, 0, 0, 0);
        else
            context_->DrawIndexed(count, 0, 0);
    } else {
        count = d.vertexCount;
        if (inst > 0)
            context_->DrawInstanced(count, inst, 0, 0);
        else
            context_->Draw(count, 0);
    }
    ++stats_.drawCalls;
    stats_.vertices += static_cast<std::uint64_t>(count) * std::max(inst, 1u);
    stats_.instances += inst;
}

} // namespace gfx
