// glaze_net — HTTP(S) på WINDOWS genom WinHTTP. Kontraktet står i http.h;
// valideringen och likriktningen i http.cpp.
//
// ASYNKRONT LÄGE, och det är inte ett val: avbrytning kräver att
// förfrågans handtag stängs från en annan tråd, och Microsofts
// dokumentation förbjuder det för en SYNKRON förfrågan ("An application
// should never call WinHttpCloseHandle on a synchronous request. This can
// create a race condition."). I asynkront läge är stängningen det
// dokumenterade sättet att avbryta.
//
// FORMEN: den anropande pooltråden DRIVER tillståndsmaskinen — skicka,
// vänta, ta emot, vänta, läs, vänta … — och callbacken gör bara två saker:
// noterar vilken komplettering som kom och väcker väntan. Ingen logik i
// callbacken, alltså ingen rekursion (WinHttpReadData kan komplettera
// synkront, inifrån sig själv) och inga lås som hålls över ett API-anrop.
//
// LIVSTIDEN: tillståndet ägs delat (shared_ptr). Kontextvärdet är en
// heap-kopia av pekaren som WinHTTP äger tills HANDLE_CLOSING — den SISTA
// callbacken för handtaget — och som raderas där. Förfrågan väntar alltid in
// HANDLE_CLOSING innan den returnerar, så läsbufferten och kroppen (som
// WinHTTP skriver i respektive läser ur asynkront) lever tills WinHTTP är
// klar med dem, även vid avbrott.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <climits>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "http_impl.h"
#include "sys.h"
#include "win_text.h"

namespace glaze::net::detail {
namespace {

enum class Händelse { Ingen, Skickad, Huvuden, Läst, Fel };

struct Tillstånd {
    std::mutex                  m;
    std::condition_variable_any cv;
    Händelse                    händelse    = Händelse::Ingen;
    DWORD                       lästa       = 0;
    DWORD                       fel         = 0;
    DWORD                       certFlaggor = 0;
    bool                        stängd      = false;
};

void CALLBACK callback(HINTERNET, DWORD_PTR kontext, DWORD status, LPVOID info, DWORD längd)
{
    auto* hållare = reinterpret_cast<std::shared_ptr<Tillstånd>*>(kontext);
    if (!hållare)
        return;
    const std::shared_ptr<Tillstånd> t = *hållare;   // lever genom hela anropet
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
        delete hållare;   // den sista callbacken för handtaget
    std::lock_guard<std::mutex> lås(t->m);
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        t->händelse = Händelse::Skickad;
        break;
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        t->händelse = Händelse::Huvuden;
        break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        t->lästa    = längd;
        t->händelse = Händelse::Läst;
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        t->fel      = static_cast<const WINHTTP_ASYNC_RESULT*>(info)->dwError;
        t->händelse = Händelse::Fel;
        break;
    case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
        if (info)
            t->certFlaggor = *static_cast<const DWORD*>(info);
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        t->stängd = true;
        break;
    default:
        return;
    }
    t->cv.notify_all();
}

// Den delade sessionen — EN per process, så att WinHTTP kan återanvända
// anslutningar (keep-alive) mellan förfrågningar. Systemets proxy- och
// WPAD-inställningar följer med via AUTOMATIC_PROXY (Windows 8.1+); på en
// äldre Windows faller den tillbaka på den statiska proxyn.
struct Session {
    HINTERNET h   = nullptr;
    DWORD     fel = 0;
};

const Session& session()
{
    static const Session s = [] {
        Session r;
        r.h = WinHttpOpen(L"glaze-net/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (!r.h)
            r.h = WinHttpOpen(L"glaze-net/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                              WINHTTP_FLAG_ASYNC);
        if (!r.h)
            r.fel = GetLastError();
        return r;
    }();
    return s;
}

std::string certText(DWORD f)
{
    std::string t;
    auto lägg = [&](DWORD bit, const char* text) {
        if (f & bit)
            t += t.empty() ? text : std::string(", ") + text;
    };
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA, "okänd utfärdare");
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID, "namnet stämmer inte");
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID, "utanför giltighetstiden");
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED, "återkallat");
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED, "återkallelsekontrollen misslyckades");
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT, "ogiltigt certifikat");
    lägg(WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR, "TLS-kanalen");
    return t;
}

Error fel(DWORD kod, DWORD certFlaggor = 0)
{
    Code c = Code::Failed;
    switch (kod) {
    case ERROR_WINHTTP_TIMEOUT:
        c = Code::Timeout;
        break;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        c = Code::HostNotFound;
        break;
    case ERROR_WINHTTP_CANNOT_CONNECT:
        c = Code::Refused;
        break;
    case ERROR_WINHTTP_CONNECTION_ERROR:
        c = Code::Reset;
        break;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        c = Code::Tls;
        break;
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
        c = Code::InvalidArgument;
        break;
    case ERROR_WINHTTP_REDIRECT_FAILED:
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        c = Code::Protocol;
        break;
    default:
        break;
    }
    std::string text = win::systemText(kod, GetModuleHandleW(L"winhttp.dll"));
    if (c == Code::Tls && certFlaggor)
        text += " — " + certText(certFlaggor);
    return Error{c, text + " (WinHTTP " + std::to_string(kod) + ")"};
}

// "HTTP/1.1 200 OK\r\nNamn: värde\r\n…\r\n\r\n" — statusraden hoppas över.
std::vector<Header> tolkaHeaders(const std::string& rå)
{
    std::vector<Header> ut;
    std::size_t         i      = rå.find("\r\n");
    while (i != std::string::npos) {
        i += 2;
        const std::size_t slut = rå.find("\r\n", i);
        const std::string rad  = rå.substr(i, slut == std::string::npos ? std::string::npos : slut - i);
        i = slut;
        const std::size_t kolon = rad.find(':');
        if (rad.empty() || kolon == std::string::npos)
            continue;
        std::size_t v = kolon + 1;
        while (v < rad.size() && (rad[v] == ' ' || rad[v] == '\t'))
            ++v;
        std::size_t e = rad.size();
        while (e > v && (rad[e - 1] == ' ' || rad[e - 1] == '\t'))
            --e;
        ut.push_back({rad.substr(0, kolon), rad.substr(v, e - v)});
    }
    return ut;
}

// En förfrågan i flykten. Destruktorn stänger handtagen i rätt ordning och
// väntar in HANDLE_CLOSING — hur funktionen än lämnas.
class Flykt {
public:
    explicit Flykt(std::shared_ptr<Tillstånd> t) : t_(std::move(t)) {}
    ~Flykt()
    {
        if (förfrågan_) {
            WinHttpCloseHandle(förfrågan_);
            if (callbackSatt_) {
                std::unique_lock<std::mutex> lås(t_->m);
                t_->cv.wait(lås, [&] { return t_->stängd; });
            }
        }
        if (anslutning_)
            WinHttpCloseHandle(anslutning_);
    }
    Flykt(const Flykt&)            = delete;
    Flykt& operator=(const Flykt&) = delete;

    HINTERNET anslutning_   = nullptr;
    HINTERNET förfrågan_    = nullptr;
    bool      callbackSatt_ = false;

private:
    std::shared_ptr<Tillstånd> t_;
};

enum class Väntan { Klar, Stopp, Tak };

// Väntar på NÄSTA komplettering (eller stopp/tak). Händelsen nollställs
// av anroparen INNAN API-anropet som ska ge den — callbacken kan komma
// synkront, inifrån anropet.
Väntan vänta(Tillstånd& t, const std::stop_token& stop, sys::Deadline deadline)
{
    std::unique_lock<std::mutex> lås(t.m);
    const auto klar = [&] { return t.händelse != Händelse::Ingen; };
    const bool fick = deadline == sys::Deadline::max() ? t.cv.wait(lås, stop, klar)
                                                       : t.cv.wait_until(lås, stop, deadline, klar);
    if (fick)
        return Väntan::Klar;
    return stop.stop_requested() ? Väntan::Stopp : Väntan::Tak;
}

void nollställ(Tillstånd& t)
{
    std::lock_guard<std::mutex> lås(t.m);
    t.händelse = Händelse::Ingen;
}

// Utfallet av en väntan på `väntad`: tomt = som väntat, annars felet.
std::optional<Error> utfall(Tillstånd& t, Väntan v, Händelse väntad)
{
    if (v == Väntan::Stopp)
        return Error{threads::Code::Cancelled, "avbrutet"};
    if (v == Väntan::Tak)
        return Error{Code::Timeout, "inget svar inom tidsgränsen"};
    std::lock_guard<std::mutex> lås(t.m);
    if (t.händelse == Händelse::Fel) {
        if (t.fel == ERROR_WINHTTP_OPERATION_CANCELLED)
            return Error{threads::Code::Cancelled, "avbrutet"};
        return fel(t.fel, t.certFlaggor);
    }
    if (t.händelse != väntad)
        return Error{Code::Failed, "oväntad komplettering från WinHTTP"};
    return std::nullopt;
}

DWORD tidsgräns(sys::Deadline deadline)
{
    const auto kvar = sys::remaining(deadline);
    if (kvar == kForever)
        return 0;   // 0 = oändligt för WinHttpSetTimeouts
    return static_cast<DWORD>(std::clamp<long long>(kvar.count(), 1, INT_MAX));
}

} // namespace

Result<HttpResponse> perform(const HttpRequest& req, const Url& url, const std::stop_token& stop)
{
    const Session& s = session();
    if (!s.h)
        return fel(s.fel);
    const sys::Deadline deadline = sys::deadlineFrom(req.timeout);

    // Allt WinHTTP läser eller skriver ASYNKRONT deklareras FÖRE `flykt`,
    // så att det överlever dess dtor — som väntar in HANDLE_CLOSING. Vid ett
    // avbrott mitt i en läsning skriver WinHTTP annars i en buffert som redan
    // är borta. Kroppen är en egen kopia: API:t tar `LPVOID`, och att kasta
    // bort const från anroparens sträng vore att lita på att den bara läses.
    std::string       kropp = req.body;
    std::wstring      headers;
    std::vector<char> buf(64 * 1024);
    for (const Header& h : req.headers)
        headers += win::wide(h.name + ": " + h.value) + L"\r\n";

    auto  t = std::make_shared<Tillstånd>();
    Flykt flykt(t);

    flykt.anslutning_ = WinHttpConnect(s.h, win::wide(url.host).c_str(),
                                       static_cast<INTERNET_PORT>(url.port), 0);
    if (!flykt.anslutning_)
        return fel(GetLastError());
    flykt.förfrågan_ = WinHttpOpenRequest(flykt.anslutning_, win::wide(req.method).c_str(),
                                          win::wide(url.target).c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          url.https ? WINHTTP_FLAG_SECURE : 0);
    if (!flykt.förfrågan_)
        return fel(GetLastError());

    // Kontexten FÖRE callbacken — en callback utan kontext ignoreras.
    auto*     hållare = new std::shared_ptr<Tillstånd>(t);
    DWORD_PTR kontext = reinterpret_cast<DWORD_PTR>(hållare);
    if (!WinHttpSetOption(flykt.förfrågan_, WINHTTP_OPTION_CONTEXT_VALUE, &kontext,
                          sizeof kontext) ||
        WinHttpSetStatusCallback(flykt.förfrågan_, callback,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                     WINHTTP_CALLBACK_FLAG_HANDLES |
                                     WINHTTP_CALLBACK_FLAG_SECURE_FAILURE,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        const DWORD e = GetLastError();
        delete hållare;   // ingen callback ⇒ ingen HANDLE_CLOSING som tar den
        return fel(e);
    }
    flykt.callbackSatt_ = true;

    const DWORD ms = tidsgräns(deadline);
    WinHttpSetTimeouts(flykt.förfrågan_, static_cast<int>(ms), static_cast<int>(ms),
                       static_cast<int>(ms), static_cast<int>(ms));
    DWORD policy = req.followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP
                                       : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(flykt.förfrågan_, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
    DWORD maxOmdir = 10;
    WinHttpSetOption(flykt.förfrågan_, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &maxOmdir,
                     sizeof maxOmdir);
    // Avkomprimering (Windows 8.1+). Saknas den på en äldre Windows skickas
    // ingen Accept-Encoding, och servern svarar okomprimerat.
    DWORD komp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(flykt.förfrågan_, WINHTTP_OPTION_DECOMPRESSION, &komp, sizeof komp);
    // Inga automatiska kakor (likriktningen): WinHTTP sparar annars kakor i
    // den DELADE sessionen och skickar dem i senare förfrågningar — libcurl
    // gör det aldrig, och NSURLSession är konfigurerad utan kaklagring.
    DWORD utanKakor = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(flykt.förfrågan_, WINHTTP_OPTION_DISABLE_FEATURE, &utanKakor,
                     sizeof utanKakor);

    // ---- Skicka.
    nollställ(*t);
    if (!WinHttpSendRequest(flykt.förfrågan_,
                            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1L),
                            kropp.empty() ? WINHTTP_NO_REQUEST_DATA : kropp.data(),
                            static_cast<DWORD>(kropp.size()), static_cast<DWORD>(kropp.size()),
                            kontext))
        return fel(GetLastError());
    if (auto e = utfall(*t, vänta(*t, stop, deadline), Händelse::Skickad))
        return *e;

    // ---- Ta emot huvudet.
    nollställ(*t);
    if (!WinHttpReceiveResponse(flykt.förfrågan_, nullptr))
        return fel(GetLastError());
    if (auto e = utfall(*t, vänta(*t, stop, deadline), Händelse::Huvuden))
        return *e;

    HttpResponse svar;
    DWORD        status = 0;
    DWORD        len    = sizeof status;
    if (!WinHttpQueryHeaders(flykt.förfrågan_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                             WINHTTP_NO_HEADER_INDEX))
        return fel(GetLastError());
    svar.status = static_cast<int>(status);

    len = 0;
    WinHttpQueryHeaders(flykt.förfrågan_, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len,
                        WINHTTP_NO_HEADER_INDEX);
    if (len > 0) {
        std::wstring rå(len / sizeof(wchar_t) + 1, L'\0');
        if (WinHttpQueryHeaders(flykt.förfrågan_, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, rå.data(), &len,
                                WINHTTP_NO_HEADER_INDEX)) {
            rå.resize(len / sizeof(wchar_t));
            svar.headers = tolkaHeaders(win::utf8(rå));
        }
    }

    len = 0;
    WinHttpQueryOption(flykt.förfrågan_, WINHTTP_OPTION_URL, nullptr, &len);
    if (len > 0) {
        std::wstring adress(len / sizeof(wchar_t) + 1, L'\0');
        if (WinHttpQueryOption(flykt.förfrågan_, WINHTTP_OPTION_URL, adress.data(), &len)) {
            adress.resize(len / sizeof(wchar_t));
            while (!adress.empty() && adress.back() == L'\0')
                adress.pop_back();
            svar.url = win::utf8(adress);
        }
    }
    if (svar.url.empty())
        svar.url = url.full;

    // ---- Kroppen.
    for (;;) {
        nollställ(*t);
        if (!WinHttpReadData(flykt.förfrågan_, buf.data(), static_cast<DWORD>(buf.size()),
                             nullptr))
            return fel(GetLastError());
        if (auto e = utfall(*t, vänta(*t, stop, deadline), Händelse::Läst))
            return *e;
        DWORD lästa = 0;
        {
            std::lock_guard<std::mutex> lås(t->m);
            lästa = t->lästa;
        }
        if (lästa == 0)
            break;
        svar.body.append(buf.data(), lästa);
    }
    return svar;
}

} // namespace glaze::net::detail
