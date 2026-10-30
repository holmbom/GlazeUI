// external_target_gate — EXTERNA RENDER-MÅL (`Renderer::importRenderTarget`)
// och de tre anropen som hör till dem: enheten (`nativeDevice`), GPU-markören
// (`insertCompletion`) och den externa bildtakten (`setExternalFramePacing`).
// Bara D3D11 stöder dem; på andra system är grinden ett SKIP.
//
// VÄGEN är scene3d_pixel_gates: ett DOLT fönster (`setOffscreen`), och
// bilderna är texturer som grinden själv skapar på FÖNSTRETS RENDERARES enhet
// (den som `nativeDevice` lånar ut) — så som en annan modul skulle göra. Målet
// ritas i ett `beginOffscreen`-block (rensning, en mesh med djup och en
// UI-rektangel efter den), och texturen läses tillbaka genom en egen
// staging-textur. Förväntat värde är grindens egna färger i texturens
// kanalordning — RGBA-texturen har rött i byte 0, BGRA-texturen blått.
//
// VAD GRINDEN PÅSTÅR:
//   N1  nativeDevice: api D3D11, en enhet, funktionsnivån är enhetens, och
//       kortets id är DXGI-adapterns LUID — samma som listans
//       `GpuAdapter::localAdapterId` för kortet renderaren ritar med.
//   I1  importen lyckas för RGBA8 och BGRA8, TYPELESS och UNORM, med 1×, 2×
//       och 4× (de som `supportedSamples` ger): målet är giltigt, har rätt
//       mått, provtagning och djup, och bilden är orörd efter importen
//       (inget rensas — en sentinel finns kvar).
//   D1  efter ritningen har bilden rätt färg PÅ RÄTT KANAL i alla fem fält:
//       bakgrund (rensningen), grön ensam, överlappet (den närmare gröna
//       vinner trots att den blå ritas sist — djupet fungerar), blå ensam och
//       UI-remsan som ritas efter meshen (UI:ts läge återställs, i ett mål
//       MED djup). Med MSAA stämmer det efter upplösningen — i texturens eget
//       format.
//   G1  `insertCompletion` ger en markör som blir Complete utan att blockera
//       (polling), efter att arbetet skickats.
//   S1  sharedAttachments: två bilder delar hjälpytor och ritar var och en
//       rätt (djupet nollställs per push; A:s bild överlever B:s ritning);
//       en delning mot ett mål med andra mått, annan provtagning, ett
//       vanligt mål eller ett mål importerat av en annan renderare (en
//       annan enhet; rätt bild på den egna enheten) ger ett ogiltigt mål med
//       text.
//   F1  FELEN ger ett ogiltigt mål med text och lämnar renderaren hel: en
//       textur från en ANNAN enhet, fel mått, fel formatfamilj (kanalbyte
//       RGBA/BGRA och ett flyttalsformat), en bild utan RENDER_TARGET, en bild
//       med flera prov, en provtagning som inte är 1/2/4/8, mipmappar, ingen
//       bild, fel API och mått under 1.
//   T1  `setExternalFramePacing`: på och av, med frames emellan — fönstret
//       ritar och fångar som förut, och UI-ritningen efter ett externt block
//       ger rätt pixlar (tillståndet läckte inte).
//   M1  GPU-tiden för ett offscreen-block: med `setGpuTiming(true)` kommer ett
//       svar med blockets löpnummer (hoppas över där backenden inte mäter).
//
// INTE MÄTT här: att en present faktiskt hoppas över med DO_NOT_WAIT (ett
// dolt fönster har ingen vsync att vänta på — T1 mäter att anropet inte
// ändrar något annat), förlorad enhet i `insertCompletion`, en riktig
// kompositors läsning av bilden (headset), och texturer som deklarerats som
// UNORM_SRGB i stället för TYPELESS (UNORM-vyn på dem prövas inte). INGEN
// SKIP_RETURN_CODE: utan headless-yta blir ctest RÖD (clip_gate-beslutet).
//
// SABOTAGEN — alla fäller grinden: upplösningen hårdkodad till BGRA (D1 för
// RGBA-målen med MSAA), sRGB-vy i stället för UNORM (D1 alla fält utom
// bakgrunden fel värden), importen som rensar bilden (I1 sentinel), djupet
// delat utan nollställning (S1), GetDevice-kontrollen borttagen (F1 annan
// enhet), enhetskontrollen av sharedAttachments borttagen (S1 hjälpmål från
// en annan enhet), formatfamiljen oprövad (F1 kanalbyte), `insertCompletion` som
// aldrig blir klar (G1), `nativeDevice` med fel LUID-packning (N1) och
// offscreen-blocket utan GPU-fråga (M1).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gfx/geom2d.h>
#include <gfx/mat4.h>
#include <glaze/glaze.h>

#ifdef _WIN32

#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <d3d11.h>
#    include <dxgi.h>
#    include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;
bool g_ritfel  = false;   // en frame gick inte att läsa — ALDRIG en skip

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

// Grindens färger: alla kanaler olika, så ett kanalbyte syns på varje fält.
const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kBakgrund{200, 90, 40, 255};
const gfx::Color kGrön{30, 180, 60, 255};
const gfx::Color kBlå{40, 70, 220, 255};
const gfx::Color kGul{230, 210, 20, 255};
const gfx::Color kCyan{90, 200, 200, 255};

constexpr int kStorlek = 64;   // måltexturernas sida

// ----------------------------------------------------------------- fönstret

class Yta : public glaze::Widget {
public:
    gfx::Color färg = kVit;
    void draw(const glaze::Ctx& c) override
    {
        c.r->fillRect(c.box, kVit);
        c.r->fillRect(gfx::Rectf{c.box.x + 10, c.box.y + 10, 40, 30}, färg);
    }
};

class Fönster : public glaze::AppWindow {
public:
    Fönster()
    {
        setTitle("external_target_gate");
        setClientSize(320, 240);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        root().adopt(yta);
    }
    Yta yta;
};

struct Bild {
    int                       w = 0, h = 0;
    std::vector<std::uint8_t> px;
    const std::uint8_t*       at(int x, int y) const
    {
        static const std::uint8_t ingen[4] = {1, 2, 3, 4};
        if (x < 0 || y < 0 || x >= w || y >= h ||
            px.size() < static_cast<std::size_t>(w) * h * 4)
            return ingen;
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

// En frame i fönstret; fönstrets bild ut.
Bild frame(Fönster& f)
{
    f.yta.invalidate();
    f.window().renderFrame();
    Bild b;
    if (!f.window().captureFrame(b.px, b.w, b.h)) {
        std::printf("  FEL  kunde inte rendera/läsa en frame — ingen "
                    "headless-yta?\n");
        g_ritfel = true;
        ++g_fel;
    }
    return b;
}

// ------------------------------------------------------------- texturerna

// En textur på enheten. `sentinel` (fyra byte) fyller den vid skapandet.
ComPtr<ID3D11Texture2D> skapaTextur(ID3D11Device* dev, int w, int h,
                                    DXGI_FORMAT fmt, UINT bind,
                                    const std::uint8_t* sentinel = nullptr,
                                    UINT samples = 1, UINT mips = 1)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width            = static_cast<UINT>(w);
    td.Height           = static_cast<UINT>(h);
    td.MipLevels        = mips;
    td.ArraySize        = 1;
    td.Format           = fmt;
    td.SampleDesc.Count = samples;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = bind;
    std::vector<std::uint8_t> data;
    D3D11_SUBRESOURCE_DATA    sd{};
    if (sentinel) {
        data.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = sentinel[i % 4];
        sd.pSysMem     = data.data();
        sd.SysMemPitch = static_cast<UINT>(w * 4);
    }
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(dev->CreateTexture2D(&td, sentinel ? &sd : nullptr, &tex)))
        return nullptr;
    return tex;
}

// Texturens byte i dess egen kanalordning, via en staging-textur.
bool läsTextur(ID3D11Device* dev, ID3D11Texture2D* tex, Bild& ut)
{
    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    td.Usage          = D3D11_USAGE_STAGING;
    td.BindFlags      = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags      = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &staging)))
        return false;
    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    ctx->CopyResource(staging.Get(), tex);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)))
        return false;
    ut.w = static_cast<int>(td.Width);
    ut.h = static_cast<int>(td.Height);
    ut.px.resize(static_cast<std::size_t>(ut.w) * ut.h * 4);
    const auto* rader = static_cast<const std::uint8_t*>(m.pData);
    for (int y = 0; y < ut.h; ++y)
        std::memcpy(&ut.px[static_cast<std::size_t>(y) * ut.w * 4],
                    rader + static_cast<std::size_t>(y) * m.RowPitch,
                    static_cast<std::size_t>(ut.w) * 4);
    ctx->Unmap(staging.Get(), 0);
    return true;
}

// Färgens byte i texturens kanalordning (opak — ingen premultiplikation att
// räkna med).
void bytes(gfx::TargetColorFormat fmt, gfx::Color c, std::uint8_t (&ut)[4])
{
    const bool bgra = fmt == gfx::TargetColorFormat::Bgra8Unorm;
    ut[0] = bgra ? c.b : c.r;
    ut[1] = c.g;
    ut[2] = bgra ? c.r : c.b;
    ut[3] = 255;
}

bool ärBytes(const std::uint8_t* p, const std::uint8_t (&v)[4], int tol = 2)
{
    for (int i = 0; i < 4; ++i)
        if (std::abs(static_cast<int>(p[i]) - v[i]) > tol)
            return false;
    return true;
}

std::string px(const std::uint8_t* p)
{
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
           std::to_string(p[2]) + "," + std::to_string(p[3]);
}

// ----------------------------------------------------------- scenanropen
//
// Världen = målets px (rad 0 överst), z = djup i [0, 1] (w = 1) — scene3d_
// pixel_gates form.

void pxMatris(float out[16], double ox, double oy, double djup)
{
    gfx::Mat4d m;   // kolumnordning: m[kolumn * 4 + rad]
    m.m[0]  = 1.0;
    m.m[5]  = 1.0;
    m.m[12] = ox;
    m.m[13] = oy;
    m.m[14] = djup;
    m.m[15] = 1.0;
    m.toFloat(out);
}

struct Form {
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
    double                          ox = 0.0, oy = 0.0;
};

Form fillRuta(gfx::Renderer& r, double x0, double y0, double x1, double y1,
              gfx::Color färg)
{
    const std::vector<std::vector<gfx::Point2d>> ringar = {
        {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
    gfx::PolygonStyle s;
    s.color = färg;
    std::vector<gfx::FillChunk> chunks;
    gfx::buildPolygon(ringar, s, chunks);
    const gfx::FillChunk& c = chunks.front();
    Form                  f;
    f.vb = r.createBuffer(gfx::BufferKind::Vertex,
                          c.vertices.size() * sizeof(c.vertices[0]),
                          c.vertices.data());
    f.ib = r.createBuffer(gfx::BufferKind::Index,
                          c.indices.size() * sizeof(std::uint32_t),
                          c.indices.data());
    f.count = static_cast<std::uint32_t>(c.indices.size());
    f.ox    = c.originX;
    f.oy    = c.originY;
    return f;
}

void rita(gfx::Renderer& r, const Form& f, double djup)
{
    gfx::MeshDraw d;
    d.program    = gfx::MeshProgram::Fill;
    d.vertices   = gfx::slice(*f.vb);
    d.indices    = gfx::slice(*f.ib);
    d.indexCount = f.count;
    pxMatris(d.uniforms.viewProj, f.ox, f.oy, djup);
    d.uniforms.pxPerWidthUnit = 1.0f;
    d.state.depth             = gfx::DepthTest::Less;
    d.state.depthWrite        = true;
    r.drawMesh(d);
}

// Meshens fält i en 64 × 64-bild: grön (4..36) på z 0,3 och sedan blå
// (20..52) på z 0,7 — i överlappet vinner den närmare gröna bara om djupet
// fungerar. `omvänd` byter rollerna (blå närmast).
struct Scen {
    Form grön, blå;
    Scen(gfx::Renderer& r)
        : grön(fillRuta(r, 4, 4, 36, 36, kGrön)),
          blå(fillRuta(r, 20, 20, 52, 52, kBlå))
    {
    }
    void rita(gfx::Renderer& r, bool omvänd) const
    {
        if (!omvänd) {
            ::rita(r, grön, 0.3);
            ::rita(r, blå, 0.7);
        } else {
            ::rita(r, blå, 0.3);
            ::rita(r, grön, 0.7);
        }
    }
};

// ---------------------------------------------------------------- fallen

struct Variant {
    const char*              namn;
    gfx::TargetColorFormat   färg;
    DXGI_FORMAT              format;
};

const Variant kVarianter[] = {
    {"RGBA8 TYPELESS", gfx::TargetColorFormat::Rgba8Unorm,
     DXGI_FORMAT_R8G8B8A8_TYPELESS},
    {"BGRA8 TYPELESS", gfx::TargetColorFormat::Bgra8Unorm,
     DXGI_FORMAT_B8G8R8A8_TYPELESS},
    {"RGBA8 UNORM", gfx::TargetColorFormat::Rgba8Unorm,
     DXGI_FORMAT_R8G8B8A8_UNORM},
    {"BGRA8 UNORM", gfx::TargetColorFormat::Bgra8Unorm,
     DXGI_FORMAT_B8G8R8A8_UNORM},
};

constexpr UINT kBindMål = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

ID3D11Device* enhetOf(gfx::Renderer& r)
{
    return static_cast<ID3D11Device*>(r.nativeDevice().device);
}

gfx::ExternalTargetDesc beskriv(ID3D11Texture2D* tex, const Variant& v,
                                int samples, bool djup)
{
    gfx::ExternalTargetDesc d;
    d.api            = gfx::NativeGraphicsApi::D3D11;
    d.image          = tex;
    d.width          = kStorlek;
    d.height         = kStorlek;
    d.color          = v.färg;
    d.target.samples = samples;
    d.target.depth   = djup;
    return d;
}

// Väntar in en GPU-markör utan att blockera: pollar till Complete (högst 5 s).
bool väntaKlar(gfx::GpuCompletion& m, std::string& fel)
{
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        switch (m.poll(fel)) {
        case gfx::GpuCompletionState::Complete: return true;
        case gfx::GpuCompletionState::Failed:   return false;
        case gfx::GpuCompletionState::Pending:  break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// ------------------------------------------------------------------- N1
void fallN1(Fönster& f)
{
    gfx::Renderer&          r  = f.window().renderer();
    const gfx::NativeDevice nd = r.nativeDevice();
    check(nd.api == gfx::NativeGraphicsApi::D3D11 && nd.device != nullptr,
          "N1 nativeDevice: D3D11 och en enhet");
    if (nd.api != gfx::NativeGraphicsApi::D3D11 || !nd.device)
        return;
    ID3D11Device* dev = enhetOf(r);
    check(nd.featureLevel == static_cast<std::uint32_t>(dev->GetFeatureLevel()),
          "N1 funktionsnivån är enhetens",
          std::to_string(nd.featureLevel));

    ComPtr<IDXGIDevice>  dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC    desc{};
    const bool ok = SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&dxgiDev))) &&
                    SUCCEEDED(dxgiDev->GetAdapter(&adapter)) &&
                    SUCCEEDED(adapter->GetDesc(&desc));
    check(ok, "N1 enhetens DXGI-adapter går att fråga");
    if (!ok)
        return;
    const std::uint64_t luid =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
             desc.AdapterLuid.HighPart))
         << 32) |
        desc.AdapterLuid.LowPart;
    check(nd.localAdapterId == luid,
          "N1 localAdapterId är adapterns LUID (HighPart << 32 | LowPart)",
          std::to_string(nd.localAdapterId) + " mot " + std::to_string(luid));

    // Listans post för kortet renderaren ritar med — uppslagen på LUID:en,
    // som är unik i körningen. Det stabila id:t är det inte alltid: samma
    // kort kan räknas upp två gånger med samma PCI-adress och olika LUID.
    const std::string id = r.adapterId();
    if (id.empty()) {
        std::printf("  SKIP N1 listans localAdapterId — renderaren har inget "
                    "adapterId\n");
        return;
    }
    std::optional<gfx::GpuAdapter> post;
    for (const gfx::GpuAdapter& g : gfx::gpuAdapters())
        if (g.localAdapterId && *g.localAdapterId == nd.localAdapterId)
            post = g;
    check(post.has_value(),
          "N1 listan har en post med nativeDevice().localAdapterId",
          std::to_string(nd.localAdapterId));
    check(post && post->id == id,
          "N1 postens id = renderarens adapterId",
          post ? post->id + " mot " + id : id);
}

// ------------------------------------------------------------- I1 + D1 + G1
void fallImportOchRitning(Fönster& f, const Variant& v, int samples)
{
    gfx::Renderer& r = f.window().renderer();
    ID3D11Device*  dev = enhetOf(r);
    const std::string namn = std::string(v.namn) + " " +
                             std::to_string(samples) + "×";
    const std::uint8_t sentinel[4] = {10, 20, 30, 40};
    auto tex = skapaTextur(dev, kStorlek, kStorlek, v.format, kBindMål, sentinel);
    check(tex != nullptr, "I1 " + namn + ": texturen skapas");
    if (!tex)
        return;

    std::string fel;
    auto        t = r.importRenderTarget(beskriv(tex.Get(), v, samples, true),
                                         nullptr, fel);
    check(t && t->image().valid(), "I1 " + namn + ": importen ger ett giltigt mål",
          fel);
    if (!t || !t->image().valid())
        return;
    check(t->samples() == samples && t->hasDepth() &&
              t->image().width() == kStorlek && t->image().height() == kStorlek,
          "I1 " + namn + ": mått, provtagning och djup",
          std::to_string(t->samples()) + " prov");

    // Ingen rensning vid import: sentinelen finns kvar.
    Bild före;
    check(läsTextur(dev, tex.Get(), före), "I1 " + namn + ": texturen läses");
    std::uint8_t sv[4] = {10, 20, 30, 40};
    check(ärBytes(före.at(0, 0), sv, 0) && ärBytes(före.at(32, 32), sv, 0) &&
              ärBytes(före.at(63, 63), sv, 0),
          "I1 " + namn + ": bilden är orörd efter importen",
          px(före.at(32, 32)));

    // Ritningen.
    const Scen scen(r);
    check(r.beginOffscreen(), "D1 " + namn + ": beginOffscreen");
    r.pushTarget(*t, kBakgrund);
    scen.rita(r, false);
    r.fillRect(gfx::Rectf{0, 56, 64, 8}, kGul);   // UI efter scenanropen
    r.popTarget();
    r.endOffscreen();

    // Markören: arbetet är skickat, och markören blir klar utan blockering.
    std::string mfel;
    auto        marker = r.insertCompletion(mfel);
    check(marker != nullptr, "G1 " + namn + ": insertCompletion ger en markör",
          mfel);
    if (marker) {
        std::string pfel;
        check(väntaKlar(*marker, pfel),
              "G1 " + namn + ": markören blir Complete", pfel);
    }

    Bild efter;
    check(läsTextur(dev, tex.Get(), efter), "D1 " + namn + ": texturen läses");
    struct Prov {
        int         x, y;
        gfx::Color  c;
        const char* vad;
    };
    const Prov prov[] = {
        {60, 8, kBakgrund, "bakgrunden (rensningen)"},
        {10, 10, kGrön, "grön ensam"},
        {28, 28, kGrön, "överlappet: den närmare gröna (djupet)"},
        {44, 44, kBlå, "blå ensam"},
        {32, 60, kGul, "UI-remsan efter meshen"},
    };
    for (const Prov& p : prov) {
        std::uint8_t v4[4];
        bytes(v.färg, p.c, v4);
        check(ärBytes(efter.at(p.x, p.y), v4), "D1 " + namn + ": " + p.vad,
              px(efter.at(p.x, p.y)) + " väntat " + px(v4));
    }
}

// ------------------------------------------------------------------- S1
void fallS1(Fönster& f)
{
    gfx::Renderer& r   = f.window().renderer();
    ID3D11Device*  dev = enhetOf(r);
    int            n   = r.supportedSamples(4);
    for (const Variant& v : kVarianter) {
        const std::string namn = std::string("S1 ") + v.namn + " " +
                                 std::to_string(n) + "×";
        auto texA = skapaTextur(dev, kStorlek, kStorlek, v.format, kBindMål);
        auto texB = skapaTextur(dev, kStorlek, kStorlek, v.format, kBindMål);
        check(texA && texB, namn + ": texturerna skapas");
        if (!texA || !texB)
            continue;
        std::string felA, felB;
        auto a = r.importRenderTarget(beskriv(texA.Get(), v, n, true), nullptr,
                                      felA);
        check(a && a->image().valid(), namn + ": A importeras", felA);
        if (!a || !a->image().valid())
            continue;
        auto b = r.importRenderTarget(beskriv(texB.Get(), v, n, true), a.get(),
                                      felB);
        check(b && b->image().valid(), namn + ": B delar A:s hjälpytor", felB);
        if (!b || !b->image().valid())
            continue;

        const Scen scen(r);
        check(r.beginOffscreen(), namn + ": beginOffscreen");
        r.pushTarget(*a, kBakgrund);
        scen.rita(r, false);
        r.popTarget();
        // B efter A, på samma delade ytor: blå är nu närmast, och bara om
        // djupet nollställdes vid push vinner den i överlappet.
        r.pushTarget(*b, kCyan);
        scen.rita(r, true);
        r.popTarget();
        r.endOffscreen();

        Bild ba, bb;
        check(läsTextur(dev, texA.Get(), ba) && läsTextur(dev, texB.Get(), bb),
              namn + ": texturerna läses");
        std::uint8_t vb[4], vg[4], vbak[4], vcy[4];
        bytes(v.färg, kBlå, vb);
        bytes(v.färg, kGrön, vg);
        bytes(v.färg, kBakgrund, vbak);
        bytes(v.färg, kCyan, vcy);
        check(ärBytes(ba.at(28, 28), vg) && ärBytes(ba.at(44, 44), vb) &&
                  ärBytes(ba.at(60, 8), vbak),
              namn + ": A:s bild är rätt efter B:s ritning (överlapp grönt)",
              px(ba.at(28, 28)) + " " + px(ba.at(44, 44)) + " " +
                  px(ba.at(60, 8)));
        check(ärBytes(bb.at(28, 28), vb) && ärBytes(bb.at(10, 10), vg) &&
                  ärBytes(bb.at(60, 8), vcy),
              namn + ": B:s bild är rätt (djupet nollställt: överlapp blått)",
              px(bb.at(28, 28)) + " " + px(bb.at(10, 10)) + " " +
                  px(bb.at(60, 8)));

        // Delningen mot ett mål som inte passar.
        if (&v == &kVarianter[0]) {
            auto smal = skapaTextur(dev, 32, 32, v.format, kBindMål);
            gfx::ExternalTargetDesc ds = beskriv(smal.Get(), v, n, true);
            ds.width = ds.height = 32;
            std::string fel;
            auto        c = r.importRenderTarget(ds, a.get(), fel);
            check(c && !c->image().valid() && !fel.empty(),
                  namn + ": delning mot andra mått ger ett ogiltigt mål",
                  fel);
            fel.clear();
            auto d1 = r.importRenderTarget(beskriv(texB.Get(), v, 1, true),
                                           a.get(), fel);
            if (n > 1)
                check(d1 && !d1->image().valid() && !fel.empty(),
                      namn + ": delning mot annan provtagning ger ett "
                             "ogiltigt mål",
                      fel);
            fel.clear();
            auto vanligt = r.createRenderTarget(
                kStorlek, kStorlek, gfx::TargetDesc{n, true, false});
            auto e = r.importRenderTarget(beskriv(texB.Get(), v, n, true),
                                          vanligt.get(), fel);
            check(e && !e->image().valid() && !fel.empty(),
                  namn + ": delning mot ett vanligt mål ger ett ogiltigt mål",
                  fel);
        }
    }
}

// Hjälpytor från en ANNAN enhet: ett andra fönster har en egen renderare och
// därmed en egen enhet. Dess importerade mål delar mått, provtagning, djup och
// färgformat med det nya målet — bara enheten skiljer — och bilden är rätt,
// på den egna enheten. Delningen ska ändå ge ett ogiltigt mål med text.
void fallS1AnnanEnhet(Fönster& f)
{
    Fönster g;
    g.setOffscreen(true);
    g.show();
    if (!g.isOpen()) {
        std::printf("  FEL  S1 annan enhet: det andra fönstret öppnades inte\n");
        ++g_fel;
        ++g_fall;
        return;
    }
    gfx::Renderer& r    = f.window().renderer();
    gfx::Renderer& rAnn = g.window().renderer();
    ID3D11Device*  dev  = enhetOf(r);
    ID3D11Device*  ann  = enhetOf(rAnn);
    check(dev && ann && dev != ann, "S1 annan enhet: de två fönstren har varsin enhet");
    if (!dev || !ann || dev == ann)
        return;

    const Variant& v = kVarianter[0];
    const int      n = r.supportedSamples(4) == rAnn.supportedSamples(4)
                           ? r.supportedSamples(4)
                           : 1;
    auto egen  = skapaTextur(dev, kStorlek, kStorlek, v.format, kBindMål);
    auto andra = skapaTextur(ann, kStorlek, kStorlek, v.format, kBindMål);
    check(egen && andra, "S1 annan enhet: texturerna skapas");
    if (!egen || !andra)
        return;
    std::string fel;
    auto        peer = rAnn.importRenderTarget(beskriv(andra.Get(), v, n, true),
                                               nullptr, fel);
    check(peer && peer->image().valid(),
          "S1 annan enhet: det andra målet importeras", fel);
    if (!peer || !peer->image().valid())
        return;
    fel.clear();
    auto delat = r.importRenderTarget(beskriv(egen.Get(), v, n, true),
                                      peer.get(), fel);
    check(delat && !delat->image().valid() && !fel.empty(),
          "S1 annan enhet: delning mot ett mål från en annan enhet ger ett "
          "ogiltigt mål",
          fel);
    // Renderaren är hel: samma bild importeras utan delning.
    fel.clear();
    auto egenMål = r.importRenderTarget(beskriv(egen.Get(), v, n, true),
                                        nullptr, fel);
    check(egenMål && egenMål->image().valid(),
          "S1 annan enhet: renderaren är hel efter det nekade målet", fel);
}

// ------------------------------------------------------------------- F1
void ogiltigt(gfx::Renderer& r, const gfx::ExternalTargetDesc& d,
              gfx::RenderTarget* delad, const std::string& vad)
{
    std::string fel;
    auto        t = r.importRenderTarget(d, delad, fel);
    check(t && !t->image().valid() && t->samples() == 1 && !fel.empty(),
          "F1 " + vad + ": ogiltigt mål med text", fel);
}

void fallF1(Fönster& f)
{
    gfx::Renderer& r   = f.window().renderer();
    ID3D11Device*  dev = enhetOf(r);
    const Variant& bgra = kVarianter[1];
    const Variant& rgba = kVarianter[0];

    auto bra = skapaTextur(dev, kStorlek, kStorlek, bgra.format, kBindMål);
    check(bra != nullptr, "F1 grundtexturen skapas");
    if (!bra)
        return;

    // En ANNAN enhet: egen enhet, samma mått och format.
    {
        ComPtr<ID3D11Device> annan;
        HRESULT              hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &annan, nullptr, nullptr);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &annan,
                                   nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            auto tex = skapaTextur(annan.Get(), kStorlek, kStorlek,
                                   bgra.format, kBindMål);
            check(tex != nullptr, "F1 texturen på den andra enheten skapas");
            if (tex)
                ogiltigt(r, beskriv(tex.Get(), bgra, 1, false), nullptr,
                         "en textur från en annan enhet");
        } else {
            std::printf("  SKIP F1 annan enhet — ingen andra D3D11-enhet "
                        "gick att skapa\n");
        }
    }

    // Mått: texturen är 64, begäran 32 (och tvärtom).
    {
        gfx::ExternalTargetDesc d = beskriv(bra.Get(), bgra, 1, false);
        d.width = d.height = 32;
        ogiltigt(r, d, nullptr, "begärda mått som inte är bildens");
        auto liten = skapaTextur(dev, 32, 32, bgra.format, kBindMål);
        if (liten)
            ogiltigt(r, beskriv(liten.Get(), bgra, 1, false), nullptr,
                     "en bild som är mindre än de begärda måtten");
        gfx::ExternalTargetDesc n = beskriv(bra.Get(), bgra, 1, false);
        n.width = 0;
        ogiltigt(r, n, nullptr, "bredd 0");
        n.width  = kStorlek;
        n.height = -4;
        ogiltigt(r, n, nullptr, "negativ höjd");
    }

    // Formatfamiljen: RGBA-bild begärd som BGRA (kanalbyte) och tvärtom, och
    // ett flyttalsformat.
    {
        auto rgbaTex = skapaTextur(dev, kStorlek, kStorlek, rgba.format,
                                   kBindMål);
        if (rgbaTex) {
            ogiltigt(r, beskriv(rgbaTex.Get(), bgra, 1, false), nullptr,
                     "en RGBA-bild begärd som BGRA");
            ogiltigt(r, beskriv(bra.Get(), rgba, 1, false), nullptr,
                     "en BGRA-bild begärd som RGBA");
        }
        auto flyt = skapaTextur(dev, kStorlek, kStorlek,
                                DXGI_FORMAT_R16G16B16A16_FLOAT, kBindMål);
        if (flyt)
            ogiltigt(r, beskriv(flyt.Get(), bgra, 1, false), nullptr,
                     "ett flyttalsformat");
    }

    // Bindningar och prov i bilden.
    {
        auto utanRtv = skapaTextur(dev, kStorlek, kStorlek, bgra.format,
                                   D3D11_BIND_SHADER_RESOURCE);
        if (utanRtv)
            ogiltigt(r, beskriv(utanRtv.Get(), bgra, 1, false), nullptr,
                     "en bild utan RENDER_TARGET");
        auto utanSrv = skapaTextur(dev, kStorlek, kStorlek, bgra.format,
                                   D3D11_BIND_RENDER_TARGET);
        if (utanSrv)
            ogiltigt(r, beskriv(utanSrv.Get(), bgra, 1, false), nullptr,
                     "en bild utan SHADER_RESOURCE (målets image() hade inte "
                     "kunnat ritas)");
        if (r.supportedSamples(4) == 4) {
            auto flera = skapaTextur(dev, kStorlek, kStorlek, bgra.format,
                                     kBindMål, nullptr, 4);
            if (flera)
                ogiltigt(r, beskriv(flera.Get(), bgra, 1, false), nullptr,
                         "en bild med flera prov per pixel");
        }
        auto kedja = skapaTextur(dev, kStorlek, kStorlek, bgra.format, kBindMål,
                                 nullptr, 1, 4);
        if (kedja)
            ogiltigt(r, beskriv(kedja.Get(), bgra, 1, false), nullptr,
                     "en bild med mip-kedja");
    }

    // Begäran.
    {
        ogiltigt(r, beskriv(bra.Get(), bgra, 3, false), nullptr,
                 "provtagning 3");
        ogiltigt(r, beskriv(bra.Get(), bgra, 0, false), nullptr,
                 "provtagning 0");
        gfx::ExternalTargetDesc m = beskriv(bra.Get(), bgra, 1, false);
        m.target.mipmaps          = true;
        ogiltigt(r, m, nullptr, "mipmappar i målet");
        gfx::ExternalTargetDesc ingen = beskriv(nullptr, bgra, 1, false);
        ogiltigt(r, ingen, nullptr, "ingen bild");
        gfx::ExternalTargetDesc api = beskriv(bra.Get(), bgra, 1, false);
        api.api                     = gfx::NativeGraphicsApi::None;
        ogiltigt(r, api, nullptr, "fel grafik-API");
    }

    // Renderaren är hel efter felen: en giltig import och ritning går.
    {
        std::string fel;
        auto        t = r.importRenderTarget(beskriv(bra.Get(), bgra, 1, true),
                                             nullptr, fel);
        check(t && t->image().valid(),
              "F1 en giltig import efter felen lyckas", fel);
        if (t && t->image().valid()) {
            const Scen scen(r);
            check(r.beginOffscreen(), "F1 beginOffscreen efter felen");
            r.pushTarget(*t, kBakgrund);
            scen.rita(r, false);
            r.popTarget();
            r.endOffscreen();
            Bild         b;
            std::uint8_t vg[4];
            bytes(bgra.färg, kGrön, vg);
            check(läsTextur(dev, bra.Get(), b) && ärBytes(b.at(10, 10), vg),
                  "F1 ritningen efter felen ger rätt pixlar", px(b.at(10, 10)));
        }
    }
}

// ------------------------------------------------------------------- T1
// Ett externt block med MSAA och djup (scenanrop som lämnar djupläget på) —
// och sedan en vanlig frame som ska ge samma pixlar som före blocket.
void externtBlock(gfx::Renderer& r)
{
    ID3D11Device*  dev = enhetOf(r);
    const Variant& v   = kVarianter[1];
    auto           tex = skapaTextur(dev, kStorlek, kStorlek, v.format, kBindMål);
    if (!tex)
        return;
    const int   n = r.supportedSamples(4);
    std::string fel;
    auto        t = r.importRenderTarget(beskriv(tex.Get(), v, n, true), nullptr,
                                         fel);
    if (!t || !t->image().valid())
        return;
    const Scen scen(r);
    if (!r.beginOffscreen())
        return;
    r.pushTarget(*t, kBakgrund);
    scen.rita(r, false);
    r.popTarget();
    r.endOffscreen();
}

int antalFärg(const Bild& b, gfx::Color c)
{
    int n = 0;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            const std::uint8_t* p = b.at(x, y);
            if (std::abs(p[0] - c.r) <= 2 && std::abs(p[1] - c.g) <= 2 &&
                std::abs(p[2] - c.b) <= 2)
                ++n;
        }
    return n;
}

int störstaSkillnad(const Bild& a, const Bild& b)
{
    if (a.w != b.w || a.h != b.h || a.px.size() != b.px.size())
        return 255;
    int m = 0;
    for (std::size_t i = 0; i < a.px.size(); ++i)
        m = std::max(m, std::abs(static_cast<int>(a.px[i]) - b.px[i]));
    return m;
}

void fallT1(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    f.yta.färg       = kGrön;
    const Bild ref   = frame(f);
    check(antalFärg(ref, kGrön) >= 100,
          "T1 referensbilden har det gröna fältet",
          std::to_string(antalFärg(ref, kGrön)) + " px");

    // UI-ritningen efter ett externt block.
    externtBlock(r);
    const Bild efterBlock = frame(f);
    check(störstaSkillnad(ref, efterBlock) <= 1,
          "T1 en vanlig frame efter ett externt block ger samma pixlar (±1)",
          "största skillnad " + std::to_string(störstaSkillnad(ref, efterBlock)));

    // Den externa takten på: frames som förut.
    r.setExternalFramePacing(true);
    Bild på;
    for (int i = 0; i < 6; ++i) {
        if (i == 3)
            externtBlock(r);
        på = frame(f);
    }
    check(störstaSkillnad(ref, på) <= 1,
          "T1 med extern bildtakt ritar och fångar fönstret som förut (±1)",
          "största skillnad " + std::to_string(störstaSkillnad(ref, på)));

    // Av igen: inget kvar.
    r.setExternalFramePacing(false);
    Bild av;
    for (int i = 0; i < 3; ++i)
        av = frame(f);
    check(störstaSkillnad(ref, av) <= 1,
          "T1 takten av igen: samma pixlar (±1)",
          "största skillnad " + std::to_string(störstaSkillnad(ref, av)));
}

// ------------------------------------------------------------------- M1
void fallM1(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    if (!r.gpuTimingSupported()) {
        std::printf("  SKIP M1 GPU-tiden — backenden mäter inte\n");
        return;
    }
    ID3D11Device*  dev = enhetOf(r);
    const Variant& v   = kVarianter[1];
    auto           tex = skapaTextur(dev, kStorlek, kStorlek, v.format, kBindMål);
    if (!tex) {
        check(false, "M1 texturen skapas");
        return;
    }
    const int   n = r.supportedSamples(4);
    std::string fel;
    auto        t = r.importRenderTarget(beskriv(tex.Get(), v, n, true), nullptr,
                                         fel);
    check(t && t->image().valid(), "M1 importen", fel);
    if (!t || !t->image().valid())
        return;
    const Scen scen(r);

    r.setGpuTiming(true);
    std::vector<std::uint64_t> löpnummer;
    for (int i = 0; i < 24; ++i) {
        if (!r.beginOffscreen()) {
            check(false, "M1 beginOffscreen");
            break;
        }
        löpnummer.push_back(r.frameSerial());
        r.pushTarget(*t, kBakgrund);
        scen.rita(r, false);
        r.popTarget();
        r.endOffscreen();
        Bild sync;
        läsTextur(dev, tex.Get(), sync);   // väntar in blocket
    }
    // Svaren kommer 2–3 frames senare; frames hämtar dem.
    std::vector<gfx::GpuTime> tider;
    bool                      funnen = false;
    std::uint64_t             ns     = 0;
    for (int i = 0; i < 40 && !funnen; ++i) {
        frame(f);
        r.takeGpuTimes(tider);
        for (const gfx::GpuTime& g : tider)
            if (g.ns > 0 && std::find(löpnummer.begin(), löpnummer.end(),
                                      g.serial) != löpnummer.end()) {
                funnen = true;
                ns     = g.ns;
            }
    }
    r.setGpuTiming(false);
    check(funnen, "M1 ett offscreen-block får en GPU-tid under sitt löpnummer",
          funnen ? std::to_string(ns) + " ns"
                 : std::to_string(tider.size()) + " svar, inget för blocken");
}

}   // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== external_target_gate — externa render-mål, GPU-markör och "
                "bildtakt ===\n");

    Fönster f;
    f.setOffscreen(true);
    f.show();
    if (!f.isOpen()) {
        std::printf("  FEL  fönstret öppnades inte — ingen headless-yta?\n");
        std::printf("=== external_target_gate: FALLERAR ===\n");
        return 1;   // ALDRIG skip — se filhuvudet
    }
    frame(f);   // första framen — fönstret ritat en gång

    if (!g_ritfel) fallN1(f);
    if (!g_ritfel) {
        gfx::Renderer& r = f.window().renderer();
        for (const Variant& v : kVarianter)
            for (const int n : {1, 2, 4}) {
                if (n > 1 && r.supportedSamples(n) != n) {
                    std::printf("  SKIP %s %d× — provtagningen stöds inte\n",
                                v.namn, n);
                    continue;
                }
                fallImportOchRitning(f, v, n);
            }
    }
    if (!g_ritfel) fallS1(f);
    if (!g_ritfel) fallS1AnnanEnhet(f);
    if (!g_ritfel) fallF1(f);
    if (!g_ritfel) fallT1(f);
    if (!g_ritfel) fallM1(f);

    std::printf(g_fel ? "=== external_target_gate: %d FEL av %d ===\n"
                      : "=== external_target_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}

#else   // !_WIN32

int main()
{
    std::printf("=== external_target_gate: SKIP — externa mål finns bara på "
                "D3D11 ===\n");
    return 77;
}

#endif
