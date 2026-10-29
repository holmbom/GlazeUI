// Grafikkorten på Windows — listan (`gfx::gpuAdapters`),
// valet en ny renderare gör (`chosenAdapter`: kortet, annars GPU-preferensen)
// och adaptrarna i loggen. Kontraktet står i gfx/platform.h, id:ts form och
// tillverkarnamnen i ../gpu_util.h.
//
// SLAGET (integrerat/diskret/externt) ur kärnans adaptertyp: D3DKMT, som
// MinGW saknar headern till (`d3dkmthk.h` hör till WDK:n). Deklarationerna
// nedan är våra, med talen ur Microsoft Learn och Wines `d3dkmthk.h`;
// importstubbarna finns i libgdi32.a, som common_gfx redan länkar. Utan
// hybridflaggor avgör en PROB: en enhet utan kontext på adaptern och dess
// `UnifiedMemoryArchitecture` — bara i listan, aldrig när ett fönster öppnas.

#include <dxgi1_6.h>

#include <cstdio>

#include "../gpu_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"

// ------------------------------------------------------------------ D3DKMT
//
// WDK:ns typer i den form gdi32 exporterar. Returtypen är NTSTATUS (LONG), och
// ≥ 0 betyder lyckat. KMTQUERYADAPTERINFOTYPE är en enum i int-storlek:
// ADAPTERADDRESS = 6 och ADAPTERTYPE = 15 — 14 är ett HÅL i enumen
// (kontrollerat mot Wines header), så talet räknas inte fram ur grannarna.
extern "C" {
typedef UINT D3DKMT_HANDLE;
struct D3DKMT_OPENADAPTERFROMLUID {
    LUID          AdapterLuid;
    D3DKMT_HANDLE hAdapter;
};
struct D3DKMT_QUERYADAPTERINFO {
    D3DKMT_HANDLE hAdapter;
    UINT          Type;
    void*         pPrivateDriverData;
    UINT          PrivateDriverDataSize;
};
struct D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
};
struct D3DKMT_ADAPTERADDRESS {
    UINT BusNumber;
    UINT DeviceNumber;
    UINT FunctionNumber;
};
LONG APIENTRY D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID* p);
LONG APIENTRY D3DKMTQueryAdapterInfo(const D3DKMT_QUERYADAPTERINFO* p);
LONG APIENTRY D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER* p);
}

namespace gfx {

namespace {

constexpr UINT kKmtAdapterAddress = 6;
constexpr UINT kKmtAdapterType    = 15;

// D3DKMT_ADAPTERTYPE:s bitar (en union av bitfält och `UINT Value`).
constexpr UINT kTypeSoftwareDevice   = 1u << 2;
constexpr UINT kTypeHybridDiscrete   = 1u << 4;
constexpr UINT kTypeHybridIntegrated = 1u << 5;
constexpr UINT kTypeDetachable       = 1u << 10;

// En öppnad kärnadapter för LUID:en — stängd i destruktorn. `query` svarar
// false när öppningen eller frågan vägrades (en äldre Windows, en adapter
// utan kärndrivrutin).
class KmtAdapter {
public:
    explicit KmtAdapter(const LUID& luid)
    {
        D3DKMT_OPENADAPTERFROMLUID o{};
        o.AdapterLuid = luid;
        if (D3DKMTOpenAdapterFromLuid(&o) >= 0) {
            handle_ = o.hAdapter;
            open_   = true;
        }
    }
    ~KmtAdapter()
    {
        if (open_) {
            D3DKMT_CLOSEADAPTER c{handle_};
            D3DKMTCloseAdapter(&c);
        }
    }
    KmtAdapter(const KmtAdapter&)            = delete;
    KmtAdapter& operator=(const KmtAdapter&) = delete;

    bool query(UINT type, void* data, UINT size) const
    {
        if (!open_)
            return false;
        D3DKMT_QUERYADAPTERINFO q{handle_, type, data, size};
        return D3DKMTQueryAdapterInfo(&q) >= 0;
    }

private:
    D3DKMT_HANDLE handle_ = 0;
    bool          open_   = false;
};

// PROBEN: en enhet utan kontext på adaptern, och om minnet är delat med
// processorn. Unknown när enheten inte går att skapa.
GpuKind probeAdapterKind(IDXGIAdapter1* a)
{
    ComPtr<ID3D11Device> dev;
    if (FAILED(D3D11CreateDevice(a, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                 nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr,
                                 nullptr)))
        return GpuKind::Unknown;
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 o{};
    if (FAILED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o,
                                        sizeof o)))
        return GpuKind::Unknown;
    return o.UnifiedMemoryArchitecture ? GpuKind::Integrated
                                       : GpuKind::Discrete;
}

bool sameLuid(const LUID& a, const LUID& b)
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

} // namespace

std::vector<DxgiAdapter> enumerateAdapters(IDXGIFactory1& f, bool probeKind)
{
    std::vector<DxgiAdapter> ut;
    std::vector<std::string> adress;
    ComPtr<IDXGIAdapter1>    a;
    for (UINT i = 0; SUCCEEDED(f.EnumAdapters1(i, &a)); ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d)))
            continue;
        DxgiAdapter k;
        k.adapter = a;
        k.luid    = d.AdapterLuid;
        GpuAdapter& g = k.info;
        g.id          = gpu::pciAdapterId(d.VendorId, d.DeviceId, d.SubSysId,
                                          d.Revision);
        g.name        = toUtf8(d.Description);
        g.vendor      = gpu::vendorName(d.VendorId);
        g.vendorId    = d.VendorId;
        g.deviceId    = d.DeviceId;
        g.memoryBytes = d.DedicatedVideoMemory;
        g.selectable  = true;
        g.isDefault   = i == 0;   // den D3D11CreateDevice(nullptr, …) tar

        const KmtAdapter kmt(d.AdapterLuid);
        UINT             typ    = 0;
        const bool       harTyp = kmt.query(kKmtAdapterType, &typ, sizeof typ);
        if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
            (harTyp && (typ & kTypeSoftwareDevice)))
            g.kind = GpuKind::Software;
        else if (harTyp && (typ & kTypeDetachable))
            g.kind = GpuKind::External;
        else if (harTyp && (typ & kTypeHybridIntegrated))
            g.kind = GpuKind::Integrated;
        else if (harTyp && (typ & kTypeHybridDiscrete))
            g.kind = GpuKind::Discrete;
        else if (probeKind)
            g.kind = probeAdapterKind(a.Get());

        D3DKMT_ADAPTERADDRESS plats{};
        char                  buf[16] = {};
        if (kmt.query(kKmtAdapterAddress, &plats, sizeof plats))
            std::snprintf(buf, sizeof buf, "%02x:%02x.%x", plats.BusNumber,
                          plats.DeviceNumber, plats.FunctionNumber);
        adress.emplace_back(buf);
        ut.push_back(std::move(k));
    }
    std::vector<GpuAdapter> lista;
    for (const DxgiAdapter& k : ut)
        lista.push_back(k.info);
    gpu::disambiguateIds(lista, adress);
    for (std::size_t i = 0; i < ut.size(); ++i)
        ut[i].info.id = lista[i].id;
    return ut;
}

std::vector<GpuAdapter> gpuAdapters()
{
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) {
        log("gpuAdapters: DXGI-fabriken gick inte att skapa");
        return {};
    }
    std::vector<GpuAdapter> ut;
    for (DxgiAdapter& k : enumerateAdapters(*f.Get(), true))
        ut.push_back(std::move(k.info));
    return ut;
}

// Windows ärver ingenting av valet till en barnprocess — det görs i
// processen, aldrig i miljön.
std::vector<std::string> childEnvironmentRemovals()
{
    return {};
}

// ------------------------------------------------------ GPU-preferensen
//
// Se gfx/platform.h. `Default` lämnar adaptern åt
// D3D11CreateDevice — DXGI:s första. De andra två
// frågar IDXGIFactory6 (Windows 10 1803+) efter adaptern i preferensens
// ordning. nullptr när den inte går att välja; då loggas det EN gång och
// förvalet gäller.
ComPtr<IDXGIAdapter1> preferredAdapter(IDXGIFactory1& f, GpuPreference p)
{
    if (p == GpuPreference::Default)
        return nullptr;
    static bool           logged = false;
    ComPtr<IDXGIFactory6> f6;
    ComPtr<IDXGIAdapter1> adapter;
    const DXGI_GPU_PREFERENCE pref = p == GpuPreference::HighPerformance
                                         ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                         : DXGI_GPU_PREFERENCE_MINIMUM_POWER;
    if (FAILED(f.QueryInterface(IID_PPV_ARGS(&f6))) ||
        FAILED(f6->EnumAdapterByGpuPreference(0, pref,
                                              IID_PPV_ARGS(&adapter)))) {
        if (!logged) {
            logged = true;
            log("D3D11: GPU-preferensen %s går inte att välja (IDXGIFactory6 "
                "saknas — Windows före 10 1803) — DXGI:s första adapter "
                "gäller",
                gpu::preferenceName(p));
        }
        return nullptr;
    }
    return adapter;
}

ComPtr<IDXGIAdapter1> chosenAdapter(IDXGIFactory1& f,
                                    const std::vector<DxgiAdapter>& kort)
{
    const std::string id = gpuAdapter();
    if (!id.empty()) {
        for (const DxgiAdapter& k : kort)
            if (k.info.id == id)
                return k.adapter;   // varje adapter är valbar på Windows
        static std::string loggat;
        if (loggat != id) {
            loggat = id;
            log("D3D11: kortet %s finns inte — GPU-preferensen %s gäller",
                id.c_str(), gpu::preferenceName(gpuPreference()));
        }
    }
    return preferredAdapter(f, gpuPreference());
}

std::string adapterIdOf(const std::vector<DxgiAdapter>& kort, const LUID& luid)
{
    for (const DxgiAdapter& k : kort)
        if (sameLuid(k.luid, luid))
            return k.info.id;
    return {};
}

// ADAPTRARNA I LOGGEN. En gång per process varje adapter DXGI
// räknar upp — id:t, namnet, slaget när kärnans flaggor känner det (proben
// körs inte här), tillverkaren, det egna minnet — och sedan, vid varje NY
// adapter (en annan än förra enhetens), vilken enheten fick och vad
// preferensen och kortet bad om. En popup på samma adapter loggar ingenting.
void logAdapter(const std::vector<DxgiAdapter>& kort,
                const DXGI_ADAPTER_DESC& vald, const std::string& id)
{
    constexpr UINT64 kMiB    = UINT64{1024} * 1024;
    static bool      listade = false;
    if (!listade) {
        listade = true;
        for (std::size_t i = 0; i < kort.size(); ++i) {
            const GpuAdapter& g = kort[i].info;
            log("D3D11: adapter %u: %s — %s (%s, tillverkare 0x%04x, %llu MB "
                "eget minne)",
                static_cast<unsigned>(i), g.id.c_str(), g.name.c_str(),
                gpu::kindName(g.kind), g.vendorId,
                static_cast<unsigned long long>(g.memoryBytes / kMiB));
        }
    }
    static LUID senaste{};
    static bool loggad = false;
    if (loggad && sameLuid(senaste, vald.AdapterLuid))
        return;
    loggad  = true;
    senaste = vald.AdapterLuid;
    const std::string begärt = gpuAdapter();
    log("D3D11: ritar med %s [%s] (preferensen %s, kortet %s)",
        toUtf8(vald.Description).c_str(), id.empty() ? "?" : id.c_str(),
        gpu::preferenceName(gpuPreference()),
        begärt.empty() ? "inget" : begärt.c_str());
}

} // namespace gfx
