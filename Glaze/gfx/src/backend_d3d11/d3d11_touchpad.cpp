// Styrplattan i D3D11-backenden — se d3d11_touchpad.h.
//
// DIRECT MANIPULATION, Chromiums sekvens (content/browser/renderer_host/
// direct_manipulation_helper_win.cc och _event_handler_win.cc), med två
// avsteg: ingen RAILS_X/Y (en karta panoreras fritt) och pumpningen är en
// PRIVAT Win32-timer i stället för kompositorns frames — den går bara medan
// en interaktion pågår (och tills nollställningen syns), så appen sover
// mellan gesterna (hård regel 1).
//
// HÄNDELSEHANTERAREN är handrullad COM (IUnknown med referensräkning, ingen
// WRL): den implementerar viewportens och interaktionens gränssnitt och
// pekar tillbaka på sin ägare tills `detach` klipper tråden. Alla anrop
// kommer på UI-tråden — ur `Update` eller ur Direct Manipulations egna
// meddelanden, som vår pump levererar.
//
// DE RÅA KONTAKTERNA: `RegisterRawInputDevices` för precisionsplattan
// (Digitizer/Touch Pad) EN gång per process, utan målfönster — då går WM_INPUT
// till fönstret med tangentbordsfokus. Rapporterna tolkas med HidP_* och en
// cachad beskrivning per enhet; kontakterna går till ramsamlaren och
// spåraren i ../touchpad_rotation.h.

#include "d3d11_touchpad.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../touchpad_rotation.h"
#include "d3d11_backend.h"
#include "gfx/events.h"
#include "gfx/platform.h"

#include <directmanipulation.h>   // efter windows.h (d3d11_backend.h)
#include <hidsdi.h>               // NTSTATUS, hidusage.h och hidpi.h

namespace gfx {
namespace {

// DEN FALSKA VIEWPORTEN — Chromiums 1000 × 1000. Måttet spelar ingen roll
// för förflyttningen (den är fönstrets fysiska px), bara för nollställningen.
constexpr int kViewport = 1000;

// PUMPTIMERNS id: alla bitar satta. På en 64-bitarsbyggnad (UCRT64) ligger
// det utanför fasadens uint32-id:n (`Window::startTimer`), så det kan aldrig
// krocka med appens eller ramverkets timers och blir aldrig ett
// `EventType::Timer` (WndProc:en frågar `onTimer` först). På en 32-bitars
// byggnad vore det 0xFFFFFFFF — en id som en app i så fall inte får använda.
constexpr UINT_PTR kPumpTimer = ~UINT_PTR{0};
// Intervallet: Win32:s minsta (USER_TIMER_MINIMUM, 10 ms) — i praktiken
// systemklockans upplösning, en gång per bildruta eller tätare.
constexpr UINT kPumpMs = USER_TIMER_MINIMUM;
// Hur många pumpslag nollställningen får ta innan vi slutar vänta på att
// den syns i transformen — ett skydd, så timern aldrig går för evigt.
constexpr int kResetTicks = 30;
// SKALA ≈ 1 med RELATIV marginal (Chromiums FloatEquals): ett drag ger
// skalan 1 med avrundningsbrus, en nypning skiljer sig långt mer.
constexpr float kScaleEps = 1e-5f;
// Nollställningen syns när förflyttningen är under en halv px.
constexpr float kResetPx = 0.5f;
// KONTAKTERNA ÄR FÄRSKA när en ram kommit så nyss — en precisionsplatta
// rapporterar med 100 Hz eller tätare under en beröring. Äldre än så (fönstret
// tappade tangentbordsfokus, WM_INPUT går till ett annat) räknas de inte.
constexpr ULONGLONG kRamFarskMs = 150;

// HID-användningarna (HID Usage Tables: Generic Desktop och Digitizer).
constexpr USAGE kPageGeneric   = 0x01;
constexpr USAGE kUsageX        = 0x30;
constexpr USAGE kUsageY        = 0x31;
constexpr USAGE kPageDigitizer = 0x0D;
constexpr USAGE kUsageTouchPad = 0x05;
constexpr USAGE kUsageTip      = 0x42;
constexpr USAGE kUsageConf     = 0x47;
constexpr USAGE kUsageContact  = 0x51;
constexpr USAGE kUsageCount    = 0x54;

bool floatEquals(float a, float b)
{
    return std::fabs(a - b) <
           kScaleEps * std::fmax(std::fmax(std::fabs(a), std::fabs(b)), kScaleEps);
}

// Pekaren i fönstrets klientyta (fysiska px) — gestens ankare.
Pointf pekare(HWND h)
{
    POINT p{0, 0};
    if (GetCursorPos(&p))
        ScreenToClient(h, &p);
    return Pointf{static_cast<float>(p.x), static_cast<float>(p.y)};
}

// Står pekaren över DET HÄR fönstrets klientyta — och inte över ett annat
// fönster ovanpå (en egen popup, ett annat program)?
bool pekarenHar(HWND h)
{
    POINT s{0, 0};
    if (!GetCursorPos(&s) || WindowFromPoint(s) != h)
        return false;
    POINT k = s;
    ScreenToClient(h, &k);
    RECT r{};
    GetClientRect(h, &r);
    return PtInRect(&r, k) != FALSE;
}

// ------------------------------------------------- HID-beskrivningen

// En axel ur HIDP_VALUE_CAPS: det logiska spannet rapporten talar och det
// fysiska det motsvarar. Exponenten är HID:s fyrbitarskod (8–15 = −8…−1).
struct Axel {
    LONG   logMin = 0, logMax = 0;
    LONG   fysMin = 0, fysMax = 0;
    USHORT bitar  = 0;
    double tiopot = 1.0;
    bool   harFysisk() const { return fysMax > fysMin && logMax > logMin; }
};

Axel axel(const HIDP_VALUE_CAPS& v)
{
    Axel a;
    a.logMin = v.LogicalMin;
    a.logMax = v.LogicalMax;
    a.fysMin = v.PhysicalMin;
    a.fysMax = v.PhysicalMax;
    a.bitar  = v.BitSize;
    int exp  = static_cast<int>(v.UnitsExp & 0xF);
    if (exp >= 8)
        exp -= 16;
    a.tiopot = std::pow(10.0, exp);
    return a;
}

// En KONTAKTPLATS: en länksamling med både X och Y (fingrets samling i
// precisionsplattans beskrivning). Platserna står i länksamlingens ordning,
// som är rapportens.
struct Plats {
    USHORT link = 0;
    Axel   x, y;
    bool   harX = false, harY = false;
};

struct Enhet {
    std::vector<BYTE>  preparsed;
    ULONG              rapportLangd = 0;
    std::vector<Plats> platser;
    bool               harRaknare  = false;
    USHORT             raknarLink  = 0;
    bool               fysisk      = false;   // alla axlar har fysiska gränser
    ULONG              maxUsages   = 0;
    PHIDP_PREPARSED_DATA pp()
    {
        return reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());
    }
};

std::shared_ptr<Enhet> lasEnhet(HANDLE dev)
{
    UINT n = 0;
    if (GetRawInputDeviceInfoW(dev, RIDI_PREPARSEDDATA, nullptr, &n) != 0 || n == 0)
        return nullptr;
    auto e = std::make_shared<Enhet>();
    e->preparsed.resize(n);
    if (GetRawInputDeviceInfoW(dev, RIDI_PREPARSEDDATA, e->preparsed.data(), &n) ==
        static_cast<UINT>(-1))
        return nullptr;
    HIDP_CAPS caps{};
    if (HidP_GetCaps(e->pp(), &caps) != HIDP_STATUS_SUCCESS ||
        caps.UsagePage != kPageDigitizer || caps.Usage != kUsageTouchPad ||
        caps.NumberInputValueCaps == 0)
        return nullptr;
    e->rapportLangd = caps.InputReportByteLength;
    std::vector<HIDP_VALUE_CAPS> vc(caps.NumberInputValueCaps);
    USHORT nv = caps.NumberInputValueCaps;
    if (HidP_GetValueCaps(HidP_Input, vc.data(), &nv, e->pp()) != HIDP_STATUS_SUCCESS)
        return nullptr;
    vc.resize(nv);

    std::vector<Plats> perLink;
    const auto plats = [&perLink](USHORT link) -> Plats& {
        for (Plats& p : perLink)
            if (p.link == link)
                return p;
        perLink.push_back(Plats{});
        perLink.back().link = link;
        return perLink.back();
    };
    for (const HIDP_VALUE_CAPS& v : vc) {
        const auto omfattar = [&v](USAGE u) {
            return v.IsRange ? (u >= v.Range.UsageMin && u <= v.Range.UsageMax)
                             : v.NotRange.Usage == u;
        };
        if (v.UsagePage == kPageGeneric && omfattar(kUsageX)) {
            Plats& p = plats(v.LinkCollection);
            p.x      = axel(v);
            p.harX   = true;
        } else if (v.UsagePage == kPageGeneric && omfattar(kUsageY)) {
            Plats& p = plats(v.LinkCollection);
            p.y      = axel(v);
            p.harY   = true;
        } else if (v.UsagePage == kPageDigitizer && omfattar(kUsageCount)) {
            e->harRaknare = true;
            e->raknarLink = v.LinkCollection;
        }
    }
    for (const Plats& p : perLink)
        if (p.harX && p.harY)
            e->platser.push_back(p);
    if (e->platser.empty())
        return nullptr;
    std::sort(e->platser.begin(), e->platser.end(),
              [](const Plats& a, const Plats& b) { return a.link < b.link; });
    e->fysisk = std::all_of(e->platser.begin(), e->platser.end(),
                            [](const Plats& p) {
                                return p.x.harFysisk() && p.y.harFysisk();
                            });
    e->maxUsages = HidP_MaxUsageListLength(HidP_Input, kPageDigitizer, e->pp());
    return e;
}

// Ett logiskt värde ur rapporten i plattans FYSISKA enheter — utan fysiska
// gränser de logiska, för alla axlar lika, så att förhållandet mellan dem
// aldrig blandar två sorter.
double fysiskt(ULONG v, const Axel& a, bool fysisk)
{
    LONG s = static_cast<LONG>(v);
    if (a.logMin < 0 && a.bitar > 0 && a.bitar < 32 &&
        (v & (1ul << (a.bitar - 1))) != 0)
        s = static_cast<LONG>(v | ~((1ul << a.bitar) - 1));   // teckenförläng
    if (!fysisk)
        return static_cast<double>(s);
    return (a.fysMin + static_cast<double>(s - a.logMin) * (a.fysMax - a.fysMin) /
                           (a.logMax - a.logMin)) *
           a.tiopot;
}

// Enheternas beskrivningar, per process: WM_INPUT kan komma till vilket av
// appens fönster som helst.
std::unordered_map<HANDLE, std::shared_ptr<Enhet>>& enheter()
{
    static std::unordered_map<HANDLE, std::shared_ptr<Enhet>> m;
    return m;
}

bool g_rawForsokt = false;   // RegisterRawInputDevices — EN gång per process
bool g_dmLoggad   = false;

} // namespace

// ================================================================== Impl

struct D3dTouchpad::Impl {
    // ------------------------------------------------ händelsehanteraren
    class Hanterare final : public IDirectManipulationViewportEventHandler,
                            public IDirectManipulationInteractionEventHandler {
    public:
        explicit Hanterare(Impl* agare) : agare_(agare) {}
        void klipp() { agare_ = nullptr; }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ut) override
        {
            if (!ut)
                return E_POINTER;
            if (riid == __uuidof(IUnknown) ||
                riid == __uuidof(IDirectManipulationViewportEventHandler)) {
                *ut = static_cast<IDirectManipulationViewportEventHandler*>(this);
            } else if (riid == __uuidof(IDirectManipulationInteractionEventHandler)) {
                *ut = static_cast<IDirectManipulationInteractionEventHandler*>(this);
            } else {
                *ut = nullptr;
                return E_NOINTERFACE;
            }
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&ref_));
        }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const LONG n = InterlockedDecrement(&ref_);
            if (n == 0)
                delete this;
            return static_cast<ULONG>(n);
        }

        HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(
            IDirectManipulationViewport*, DIRECTMANIPULATION_STATUS current,
            DIRECTMANIPULATION_STATUS previous) override
        {
            if (agare_)
                agare_->statusChanged(current, previous);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnViewportUpdated(
            IDirectManipulationViewport*) override
        {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnContentUpdated(
            IDirectManipulationViewport*,
            IDirectManipulationContent* content) override
        {
            if (agare_ && content)
                agare_->contentUpdated(*content);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnInteraction(
            IDirectManipulationViewport2*,
            DIRECTMANIPULATION_INTERACTION_TYPE typ) override
        {
            if (agare_)
                agare_->interaction(typ);
            return S_OK;
        }

    private:
        ~Hanterare() = default;   // bara Release river
        LONG  ref_ = 1;
        Impl* agare_;
    };

    HWND          hwnd = nullptr;
    const Window* win  = nullptr;
    bool          com  = false;   // vår CoInitializeEx — balanseras i stoppet

    ComPtr<IDirectManipulationManager>       manager;
    ComPtr<IDirectManipulationUpdateManager> update;
    ComPtr<IDirectManipulationViewport>      viewport;
    Hanterare*                               hanterare = nullptr;
    DWORD                                    cookie    = 0;
    bool                                     aktiv     = false;   // Activate(hwnd)
    bool                                     dm        = false;   // allt startat

    // PUMPNINGEN: timern går medan en interaktion pågår, viewporten rör sig
    // eller nollställningen väntar på att synas.
    bool                      timer       = false;
    bool                      interagerar = false;
    DIRECTMANIPULATION_STATUS status      = DIRECTMANIPULATION_ENABLED;
    int                       nollstall   = 0;   // > 0: väntar på identiteten

    // Transformen Direct Manipulation senast gav (fysiska px) och läget.
    float lastScale = 1.0f, lastX = 0.0f, lastY = 0.0f;
    enum class Lage { Inget, Drag, Troghet, Nyp } lage = Lage::Inget;

    // GESTSEKVENSEN — nypningen och vridningen, EN sekvens.
    bool nyper = false, vrider = false, oppen = false;

    // DE RÅA KONTAKTERNA.
    HANDLE                         enhet = nullptr;
    std::vector<BYTE>              rapportBuf;
    std::vector<touchpad::Contact> platsBuf;
    std::vector<USAGE>             usageBuf;
    touchpad::FrameAssembler       ramar;
    touchpad::RotationTracker      vridning;
    bool                           vridningVar = false;   // greppet är vårt

    // VAD FINGRARNA GÖR (touchpad_rotation.h): panorering stänger zoom och
    // vridning för hela beröringen, zoom stänger vridningen och vridning
    // zoomen. Före beslutet går panoreringen direkt, medan zoomen och
    // vridningen HÅLLS och den valda släpps ikapp när beslutet faller. `last` är det senaste beslutet och gäller tills Direct
    // Manipulation-sekvensen tagit slut (sena steg efter lyftet). Utan färska
    // kontakter (fönstret saknar tangentbordsfokus) gäller hjulvägen.
    touchpad::TwoFingerClassifier klass;
    touchpad::TwoFingerMode       last         = touchpad::TwoFingerMode::None;
    ULONGLONG                     senasteRam   = 0;
    float                         hallenSkala  = 1.0f;
    double                        hallnaGrader = 0.0;
    // Det hållna efter beslutet: en SKULD som betalas av i takt med fingrarna
    // (`touchpad::catchUp`) — aldrig ett hopp. Zoomen i ln, vridningen i grader.
    double skuldLn     = 0.0;
    double skuldGrader = 0.0;

    touchpad::TwoFingerMode fingerLage() const
    {
        if (klass.mode() != touchpad::TwoFingerMode::None &&
            GetTickCount64() - senasteRam < kRamFarskMs)
            return klass.mode();
        return last;
    }

    // ---------------------------------------------------------- start/stopp

    void starta()
    {
        // STA på UI-tråden: Direct Manipulation kräver det. Anropet räknas
        // (S_OK eller S_FALSE) och balanseras i `stoppa`; är tråden redan
        // MTA (en app som valt det själv) avstår vi och hjulvägen gäller.
        const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        com = SUCCEEDED(hrCom);
        if (!com) {
            misslyckat("UI-trådens COM är inte STA", hrCom);
            return;
        }
        HRESULT hr = CoCreateInstance(__uuidof(DirectManipulationManager), nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
        if (FAILED(hr))
            return misslyckat("CoCreateInstance", hr);
        // Den falska viewporten behöver uppdateringshanteraren — det är den
        // som "ritar en frame" när vi pumpar.
        if (FAILED(hr = manager->GetUpdateManager(IID_PPV_ARGS(&update))))
            return misslyckat("GetUpdateManager", hr);
        if (FAILED(hr = manager->CreateViewport(nullptr, hwnd, IID_PPV_ARGS(&viewport))))
            return misslyckat("CreateViewport", hr);
        const DIRECTMANIPULATION_CONFIGURATION konf =
            DIRECTMANIPULATION_CONFIGURATION_INTERACTION |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA |
            DIRECTMANIPULATION_CONFIGURATION_SCALING;
        if (FAILED(hr = viewport->ActivateConfiguration(konf)))
            return misslyckat("ActivateConfiguration", hr);
        // MANUALUPDATE: viewporten är falsk och bara till för styrplattan, så
        // vi säger själva när en frame ska räknas (pumptimern).
        if (FAILED(hr = viewport->SetViewportOptions(
                       DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE)))
            return misslyckat("SetViewportOptions", hr);
        hanterare = new Hanterare(this);
        if (FAILED(hr = viewport->AddEventHandler(hwnd, hanterare, &cookie)))
            return misslyckat("AddEventHandler", hr);
        // Rektangeln FÖRE aktiveringen (Chromiums ordning).
        const RECT r{0, 0, kViewport, kViewport};
        if (FAILED(hr = viewport->SetViewportRect(&r)))
            return misslyckat("SetViewportRect", hr);
        if (FAILED(hr = manager->Activate(hwnd)))
            return misslyckat("Activate", hr);
        aktiv = true;
        if (FAILED(hr = viewport->Enable()))
            return misslyckat("Enable", hr);
        dm = true;
    }

    // Logga EN gång per process och riv det som hann skapas — hjulvägen
    // gäller då för fönstret.
    void misslyckat(const char* steg, HRESULT hr)
    {
        if (!g_dmLoggad) {
            g_dmLoggad = true;
            log("Direct Manipulation kunde inte startas (%s, 0x%08lx) — "
                "styrplattan går som mushjul", steg,
                static_cast<unsigned long>(hr));
        }
        stoppa();
    }

    void stoppa()
    {
        if (timer && hwnd)
            KillTimer(hwnd, kPumpTimer);
        timer = false;
        // Klipp tråden FÖRST: Stop och Abandon kan ropa på hanteraren, och
        // inget event får köas för ett fönster som rivs.
        if (hanterare)
            hanterare->klipp();
        if (viewport) {
            viewport->Stop();
            if (cookie)
                viewport->RemoveEventHandler(cookie);
            viewport->Abandon();
        }
        if (manager && aktiv)
            manager->Deactivate(hwnd);
        aktiv  = false;
        cookie = 0;
        viewport.Reset();
        update.Reset();
        manager.Reset();
        if (hanterare) {
            hanterare->Release();
            hanterare = nullptr;
        }
        dm = false;
        if (com) {
            CoUninitialize();
            com = false;
        }
        lage        = Lage::Inget;
        nyper       = vrider = oppen = false;
        interagerar = false;
        nollstall   = 0;
        ramar.reset();
        vridning.reset();
        vridningVar = false;
        klass.reset();
        last         = touchpad::TwoFingerMode::None;
        hallenSkala  = 1.0f;
        hallnaGrader = 0.0;
        skuldLn      = 0.0;
        skuldGrader  = 0.0;
    }

    // ------------------------------------------------------------ pumpen

    void synkaTimer()
    {
        const bool vill = dm && (interagerar ||
                                 status == DIRECTMANIPULATION_RUNNING ||
                                 status == DIRECTMANIPULATION_INERTIA ||
                                 nollstall > 0);
        if (vill && !timer)
            timer = SetTimer(hwnd, kPumpTimer, kPumpMs, nullptr) != 0;
        else if (!vill && timer) {
            KillTimer(hwnd, kPumpTimer);
            timer = false;
        }
    }

    void pumpa()
    {
        if (nollstall > 0)
            --nollstall;   // slutar vänta efter `kResetTicks` slag
        if (update)
            update->Update(nullptr);
        synkaTimer();
    }

    // ------------------------------------------------------------ eventen

    // Båda går WndProc:ens väg (`pushWheel`/`pushGesture` i
    // d3d11_window.cpp), som stämplar modifierarna — Ctrl/Cmd + draget
    // kretsar på ritytan.
    void hjul(float dx, float dy, GesturePhase fas)
    {
        // Innehållet flyttade (dx, dy) fysiska px; fasaden talar rader à
        // `kPrecisePxPerRow` LOGISKA px, och innehållet flyttar sig
        // (−wheelX, +wheelY) i dem — samma riktning som DM:s innehåll.
        const float s = win->uiScale() > 0.0f ? win->uiScale() : 1.0f;
        const Pointf p = pekare(hwnd);
        pushWheel(win->id(), p.x, p.y, -dx / s / kPrecisePxPerRow,
                  dy / s / kPrecisePxPerRow, true, fas);
    }

    void gest(GesturePhase fas, float skala, float grader)
    {
        const Pointf p = pekare(hwnd);
        pushGesture(win->id(), p.x, p.y, fas, skala, grader);
    }

    // Begin när sekvensen öppnas (första källan), annars ett Update — utan
    // ett tomt steg när det inte finns något att säga.
    void oppna(float skala, float grader)
    {
        if (!oppen) {
            oppen = true;
            gest(GesturePhase::Begin, skala, grader);
        } else if (skala != 1.0f || grader != 0.0f) {
            gest(GesturePhase::Update, skala, grader);
        }
    }

    // End när den SISTA källan slutat.
    void stangOmKlar()
    {
        if (oppen && !nyper && !vrider) {
            oppen = false;
            gest(GesturePhase::End, 1.0f, 0.0f);
        }
    }

    // Draget eller nypningen tog slut (READY, eller ett nytt tryck mitt i
    // trögheten).
    void avslutaSekvens()
    {
        switch (lage) {
        case Lage::Drag:
        case Lage::Troghet:
            hjul(0.0f, 0.0f, GesturePhase::End);
            break;
        case Lage::Nyp:
            nyper = false;
            stangOmKlar();
            break;
        case Lage::Inget:
            break;
        }
        lage        = Lage::Inget;
        last        = touchpad::TwoFingerMode::None;   // beslutet gällde sekvensen
        hallenSkala = 1.0f;   // en nypning som aldrig blev beslutad släpps
        skuldLn     = 0.0;    // och det som inte hann ikapp stryks — inget hopp
    }

    // Ett nypsteg ur Direct Manipulation (kvoten mot förra) — genom
    // fingrarnas beslut: hålls före det, släpps aldrig under en panorering.
    void skalSteg(float kvot)
    {
        switch (fingerLage()) {
        case touchpad::TwoFingerMode::Undecided:
            hallenSkala *= kvot;
            break;
        case touchpad::TwoFingerMode::Pan:
        case touchpad::TwoFingerMode::Rotate:
            break;   // panorering eller vridning: ingen zoom
        default:
            // Zoom — eller inga kontakter (hjulvägen, och då ingen skuld).
            oppna(static_cast<float>(
                      std::exp(touchpad::catchUp(std::log(kvot), skuldLn))),
                  0.0f);
            break;
        }
    }

    // ------------------------------------------------- Direct Manipulation

    void interaction(DIRECTMANIPULATION_INTERACTION_TYPE typ)
    {
        if (typ == DIRECTMANIPULATION_INTERACTION_BEGIN)
            interagerar = true;
        else if (typ == DIRECTMANIPULATION_INTERACTION_END)
            interagerar = false;
        synkaTimer();
    }

    void statusChanged(DIRECTMANIPULATION_STATUS nu, DIRECTMANIPULATION_STATUS fore)
    {
        status = nu;
        if (nu != fore) {
            // Fingrarna lyfte under ett drag: OS:ets tröghet tar vid. (Efter
            // en nypning fortsätter sekvensen — trögheten är förflyttning,
            // och den ignoreras under en nypning.)
            if (nu == DIRECTMANIPULATION_INERTIA && fore == DIRECTMANIPULATION_RUNNING &&
                lage == Lage::Drag)
                lage = Lage::Troghet;
            // Ett nytt tryck mitt i trögheten är en ny sekvens.
            if (nu == DIRECTMANIPULATION_RUNNING && fore == DIRECTMANIPULATION_INERTIA)
                avslutaSekvens();
            if (nu == DIRECTMANIPULATION_READY || nu == DIRECTMANIPULATION_ENABLED) {
                avslutaSekvens();
                // NOLLSTÄLL den falska viewporten, så nästa gest börjar i
                // identiteten. READY kommer en gång till när nollställningen
                // gjorts; den har inget att nollställa.
                const bool flyttad = !floatEquals(lastScale, 1.0f) ||
                                     lastX != 0.0f || lastY != 0.0f;
                lastScale = 1.0f;
                lastX = lastY = 0.0f;
                nollstall = 0;   // det andra READY:t — nollställningen är gjord
                if (flyttad && viewport &&
                    SUCCEEDED(viewport->ZoomToRect(0.0f, 0.0f, kViewport, kViewport,
                                                   FALSE)))
                    nollstall = kResetTicks;
            }
        }
        synkaTimer();
    }

    void contentUpdated(IDirectManipulationContent& innehall)
    {
        float m[6] = {};
        if (FAILED(innehall.GetContentTransform(m, 6)))
            return;
        const float skala = m[0], x = m[4], y = m[5];
        if (!(skala > 0.0f) || !std::isfinite(skala) || !std::isfinite(x) ||
            !std::isfinite(y))
            return;   // Windows har gett skalan 0 (Chromium loggar det)
        if (nollstall > 0) {
            // NOLLSTÄLLNINGEN PÅGÅR: transformen är en ny baslinje och ingen
            // rörelse. Den är klar när identiteten syns.
            lastScale = skala;
            lastX     = x;
            lastY     = y;
            if (floatEquals(skala, 1.0f) && std::fabs(x) < kResetPx &&
                std::fabs(y) < kResetPx)
                nollstall = 0;
            synkaTimer();
            return;
        }
        const bool  sammaSkala = floatEquals(skala, lastScale);
        const float dx = x - lastX, dy = y - lastY;
        if (sammaSkala && dx == 0.0f && dy == 0.0f)
            return;

        GesturePhase fas = GesturePhase::Update;
        // En beröring som fingrarna klassat som PANORERING eller VRIDNING
        // förblir ett drag också när Direct Manipulation ser avståndet ändras
        // — skalan räknas inte, och förflyttningen går som drag (vridning +
        // panorering är tillåtet, zoom är det inte).
        const touchpad::TwoFingerMode fl = fingerLage();
        const bool barePan = fl == touchpad::TwoFingerMode::Pan ||
                             fl == touchpad::TwoFingerMode::Rotate;
        if (floatEquals(skala, 1.0f) || barePan) {
            if (lage == Lage::Inget) {
                lage = Lage::Drag;
                fas  = GesturePhase::Begin;
            } else if (lage == Lage::Nyp && barePan) {
                // Nypningen DM började var en panorering — den släpps (den
                // hölls, så inget av den har nått ritytan).
                nyper = false;
                stangOmKlar();
                lage = Lage::Drag;
                fas  = GesturePhase::Begin;
            }
        } else if (lage == Lage::Inget || lage == Lage::Drag) {
            // En långsam nypning börjar som ett drag — draget avslutas, och
            // nypningen tar över sekvensen.
            if (lage == Lage::Drag)
                hjul(0.0f, 0.0f, GesturePhase::End);
            lage  = Lage::Nyp;
            nyper = true;
        }

        switch (lage) {
        case Lage::Drag:
            hjul(dx, dy, fas);
            break;
        case Lage::Troghet:
            hjul(dx, dy, GesturePhase::Momentum);
            break;
        case Lage::Nyp:
            // Förflyttningen under en nypning är brus; bara kvoten räknas.
            if (!sammaSkala)
                skalSteg(skala / lastScale);
            break;
        case Lage::Inget:
            break;
        }
        lastScale = skala;
        lastX     = x;
        lastY     = y;
    }

    // ------------------------------------------------------ råa kontakter

    void rawInput(LPARAM lp)
    {
        UINT storlek = 0;
        const auto h = reinterpret_cast<HRAWINPUT>(lp);
        if (GetRawInputData(h, RID_INPUT, nullptr, &storlek,
                            sizeof(RAWINPUTHEADER)) != 0 ||
            storlek < sizeof(RAWINPUTHEADER))
            return;
        rapportBuf.resize(storlek);
        if (GetRawInputData(h, RID_INPUT, rapportBuf.data(), &storlek,
                            sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
            return;
        auto* ri = reinterpret_cast<RAWINPUT*>(rapportBuf.data());
        if (ri->header.dwType != RIM_TYPEHID)
            return;
        const HANDLE dev = ri->header.hDevice;
        if (dev != enhet) {
            // En annan platta tog över: det som pågick med den förra är slut.
            enhet = dev;
            ramar.reset();
            klass.reset();
            slappVridning();
        }
        auto& tabell = enheter();
        auto  it     = tabell.find(dev);
        if (it == tabell.end()) {
            std::shared_ptr<Enhet> e = lasEnhet(dev);
            if (!e)
                return;
            it = tabell.emplace(dev, std::move(e)).first;
        }
        Enhet&       e   = *it->second;
        RAWHID&      hid = ri->data.hid;
        if (hid.dwSizeHid != e.rapportLangd) {
            // Beskrivningen stämmer inte med rapporten (en ny enhet på ett
            // återanvänt handtag) — läs om den till nästa rapport.
            tabell.erase(it);
            return;
        }
        for (DWORD i = 0; i < hid.dwCount; ++i)
            rapport(e, hid.bRawData + static_cast<std::size_t>(i) * hid.dwSizeHid,
                    hid.dwSizeHid);
    }

    void rapport(Enhet& e, BYTE* data, ULONG langd)
    {
        const auto r = reinterpret_cast<PCHAR>(data);
        int antal = static_cast<int>(e.platser.size());
        if (e.harRaknare) {
            ULONG n = 0;
            if (HidP_GetUsageValue(HidP_Input, kPageDigitizer, e.raknarLink,
                                   kUsageCount, &n, e.pp(), r, langd) !=
                HIDP_STATUS_SUCCESS)
                return;   // rapporten bär inga kontakter
            antal = static_cast<int>(n);
        }
        platsBuf.clear();
        usageBuf.resize(std::max<ULONG>(e.maxUsages, 1));
        for (std::size_t k = 0; k < e.platser.size(); ++k) {
            const Plats& p = e.platser[k];
            ULONG x = 0, y = 0, id = static_cast<ULONG>(k);
            if (HidP_GetUsageValue(HidP_Input, kPageGeneric, p.link, kUsageX, &x,
                                   e.pp(), r, langd) != HIDP_STATUS_SUCCESS ||
                HidP_GetUsageValue(HidP_Input, kPageGeneric, p.link, kUsageY, &y,
                                   e.pp(), r, langd) != HIDP_STATUS_SUCCESS)
                return;
            HidP_GetUsageValue(HidP_Input, kPageDigitizer, p.link, kUsageContact,
                               &id, e.pp(), r, langd);
            ULONG n = static_cast<ULONG>(usageBuf.size());
            bool  tip = false, conf = false;
            if (HidP_GetUsages(HidP_Input, kPageDigitizer, p.link, usageBuf.data(),
                               &n, e.pp(), r, langd) == HIDP_STATUS_SUCCESS)
                for (ULONG u = 0; u < n; ++u) {
                    tip  = tip || usageBuf[u] == kUsageTip;
                    conf = conf || usageBuf[u] == kUsageConf;
                }
            touchpad::Contact c;
            c.id         = static_cast<std::uint32_t>(id);
            c.x          = fysiskt(x, p.x, e.fysisk);
            c.y          = fysiskt(y, p.y, e.fysisk);
            c.tip        = tip;
            c.confidence = conf;
            platsBuf.push_back(c);
        }
        if (ramar.add(antal, platsBuf))
            ram(ramar.frame());
    }

    void ram(std::span<const touchpad::Contact> kontakter)
    {
        // FÖRST beslutet — det avgör vad ramens vridning och den hållna
        // zoomen får göra.
        const touchpad::ClassifyStep k = klass.update(kontakter);
        senasteRam                     = GetTickCount64();
        if (k.decided) {
            last = k.mode;
            // Det som hölls medan fingrarna inte bestämt sig blir en SKULD
            // för det valda (betalas av med fingrarna — inget hopp); det
            // andra kastas.
            if (k.mode == touchpad::TwoFingerMode::Zoom && nyper)
                skuldLn = std::log(static_cast<double>(hallenSkala));
            if (k.mode == touchpad::TwoFingerMode::Rotate && vridningVar)
                skuldGrader = hallnaGrader;
            hallenSkala  = 1.0f;
            hallnaGrader = 0.0;
        }

        const touchpad::RotationStep steg = vridning.update(kontakter);
        if (steg.began) {
            // Greppet är vårt bara med pekaren över klientytan — WM_INPUT
            // går till fönstret med TANGENTBORDSfokus, och den som vrider
            // menar det som står under pekaren.
            vridningVar = pekarenHar(hwnd);
        }
        if (vridningVar && steg.degrees != 0.0) {
            switch (fingerLage()) {
            case touchpad::TwoFingerMode::Undecided:
                hallnaGrader += steg.degrees;
                break;
            case touchpad::TwoFingerMode::Pan:
            case touchpad::TwoFingerMode::Zoom:
                break;   // panorering eller zoom: ingen vridning
            default:
                vrider = true;
                oppna(1.0f, static_cast<float>(
                                touchpad::catchUp(steg.degrees, skuldGrader)));
                break;
            }
        }
        if (steg.ended)
            slappVridning();
        if (k.ended) {
            hallnaGrader = 0.0;   // en beröring som aldrig bestämde sig
            skuldGrader  = 0.0;   // det som inte hann ikapp stryks
        }
    }

    void slappVridning()
    {
        vridning.reset();
        skuldGrader = 0.0;
        if (!vridningVar)
            return;
        vridningVar = false;
        vrider      = false;
        stangOmKlar();
    }
};

// ================================================================ ytan

D3dTouchpad::D3dTouchpad() = default;

D3dTouchpad::~D3dTouchpad()
{
    detach();
}

void D3dTouchpad::attach(HWND hwnd, const Window& win)
{
    detach();
    impl_       = std::make_unique<Impl>();
    impl_->hwnd = hwnd;
    impl_->win  = &win;
    impl_->starta();
    // De råa kontakterna — EN registrering per process, utan målfönster:
    // WM_INPUT följer tangentbordsfokus. Misslyckas den finns ingen vridning,
    // och det loggas en gång.
    if (!g_rawForsokt) {
        g_rawForsokt = true;
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = kPageDigitizer;
        rid.usUsage     = kUsageTouchPad;
        rid.dwFlags     = 0;
        rid.hwndTarget  = nullptr;
        if (!RegisterRawInputDevices(&rid, 1, sizeof rid))
            log("RegisterRawInputDevices (styrplattan) misslyckades (fel %lu) — "
                "ingen vridning med styrplattan",
                static_cast<unsigned long>(GetLastError()));
    }
}

void D3dTouchpad::detach()
{
    if (!impl_)
        return;
    impl_->stoppa();
    impl_.reset();
}

void D3dTouchpad::onPointerHitTest(WPARAM wp)
{
    // DM_POINTERHITTEST är det FÖRSTA meddelandet i en styrplattesekvens:
    // är kontakten styrplattans tar Direct Manipulation den — och då kommer
    // inget WM_MOUSEWHEEL för den.
    if (!impl_ || !impl_->dm)
        return;
    const UINT32       id  = GET_POINTERID_WPARAM(wp);
    POINTER_INPUT_TYPE typ = PT_POINTER;
    if (GetPointerType(id, &typ) && typ == PT_TOUCHPAD)
        impl_->viewport->SetContact(id);
}

bool D3dTouchpad::onTimer(WPARAM id)
{
    if (id != kPumpTimer)
        return false;
    if (impl_)
        impl_->pumpa();
    return true;
}

void D3dTouchpad::onRawInput(LPARAM lp)
{
    if (impl_)
        impl_->rawInput(lp);
}

void D3dTouchpad::onFocusLost()
{
    // WM_INPUT går till fönstret med fokus — efter förlusten kommer inga
    // fler kontakter hit, och en vridning som pågick tar slut nu.
    if (!impl_)
        return;
    impl_->ramar.reset();
    impl_->slappVridning();
}

} // namespace gfx
