#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gfx/events.h"
#include "gfx/window.h"

namespace gfx {

// Plattforms-/backend-funktionerna — fria funktioner som den inkompilerade
// backenden (backend_d3d11/backend_metal/backend_linux) implementerar. Bara
// en backend byggs in åt gången; valet sker i byggtid, ingen runtime-dispatch.

// Global init/städning av backenden. init() loggar själv vid fel.
bool init();
void shutdown();

// Händelsestyrd event-pump: waitEvent blockerar tills något händer
// (~0 % CPU i vila — hård regel 1), pollEvent returnerar false när kön
// är tom. Backend-events utan motsvarighet blir EventType::None.
bool waitEvent(Event& e);
bool pollEvent(Event& e);

// TRÅDSÄKER VÄCKNING av pumpen — den ENDA gfx-funktion
// som får kallas från en annan tråd än UI-tråden. Den gör TVÅ saker, och
// båda krävs: väcker OS-pumpen ur sin blockering OCH köar ett
// `EventType::Wakeup` som `waitEvent` kan returnera. Bara det första hade
// dispatchats och skickat loopen tillbaka i blockering, så
// `glaze::App`-pumpen aldrig nått sin svans.
//
// Den invaliderar INGENTING och ritar INGENTING (hård regel 1) — den är
// transportlagret under `glaze::app().post(fn)`, som är det appar
// använder. Säker att kalla före `init()` och efter `shutdown()`: då är
// den en no-op.
void postWakeup();

// Loggning, printf-stil. OBS: WIN32-subsystem — syns inte i konsolen;
// verifiera beteende via röktest-exitkoder, inte loggutskrift.
void log(const char* fmt, ...);

// Exe-katalogen (med avslutande separator), "" om okänd.
std::string basePath();

// LIGGER ADRESSEN I PROGRAMFILEN? — true när
// `address` hör till processens programfil (exe:n) och inte till en
// DLL/.so/.dylib. Frågan ställs av programfilens ankare i delat läge
// (glaze/src/app_anchor.cpp), som kompileras in i både programmet och en
// plugin: bara i programmet får det riva App. Tillståndslös och säker före
// `init()` och efter `shutdown()` — och under laddarlåset.
//
//   Windows  GetModuleHandleExW(FROM_ADDRESS) == GetModuleHandleW(nullptr)
//   macOS    dladdr: modulens Mach-O-huvud == dyld:s bild 0 (programfilen)
//   Linux    dl_iterate_phdr: adressen i ett av huvudprogrammets laddade
//            segment (det första objektet som besöks är huvudprogrammet)
bool isInProgramFile(const void* address);

// Monotona millisekunder (backend-neutral, std::chrono).
std::uint64_t ticksMs();

// DUBBELKLICKETS TIDSFÖNSTER i millisekunder — OS:ets ANVÄNDARINSTÄLLNING.
// Två tryck med vänsterknappen inom den tiden, på
// samma widget och nästan samma punkt, är ETT dubbelklick
// (`glaze::Widget::mouseDoubleClick`). Det är en fråga till OS:et av samma
// skäl som `systemTheme`: användaren har ställt in den (motorik,
// tillgänglighet), och en egen konstant hade gjort en Glaze-app långsammare
// eller snabbare än resten av skrivbordet.
//
//   Windows  GetDoubleClickTime()
//   macOS    NSEvent.doubleClickInterval
//   Linux    portalens org.gnome.desktop.peripherals.mouse/double-click
//            (GNOME, hålls levande via SettingChanged), annars 400 — GTK:s
//            och KDE:s förval
std::uint32_t doubleClickMs();

// Klippbordet, alltid UTF-8. clipboardText() ger "" när klippbordet är
// tomt eller inte bär text.
//
// RADSLUTEN är LF i fasaden, åt båda hållen:
// backenden översätter till OS:ets form och tillbaka. Windows
// `CF_UNICODETEXT` har CR-LF som radslut — ett klassiskt flerradigt
// EDIT-fält visar en ensam LF som ingenting — så `setClipboardText` skriver
// CRLF och `clipboardText` gör CRLF till LF. macOS och Linux talar LF och
// översätter ingenting. Ett ensamt CR (klassisk Mac) lämnas åt mottagaren.
std::string clipboardText();
void setClipboardText(const std::string& utf8);

// Öppnar en URL (UTF-8) i systemets standardhanterare — en OS-HANDLING,
// inte ett gränssnitt, därför fasadens jobb (glaze::Link använder den).
// true = OS:et tog emot begäran; false = tom sträng eller att OS:et
// vägrade (loggas). Vad som händer sedan (vilken webbläsare, om den ens
// startar) är utanför vår kontroll — svaret är "skickad", inte "visad".
bool openUrl(const std::string& url);

// Öppnar en FIL (UTF-8-sökväg, absolut eller relativ mot arbetskatalogen) i
// systemets standardprogram för dess typ — `openUrl`s syskon för en lokal
// sökväg. Appen slipper koda sökvägen till en URL själv
// (mellanslag, tecken utanför ASCII); OS:et får sökvägen som den är. true =
// OS:et tog emot begäran; false = tom sträng, en fil som inte finns eller att
// OS:et vägrade (ingen standardhanterare — loggas). Som `openUrl` betyder
// true "skickad", inte "visad". OS:ets standardåtgärd för typen gäller: en
// körbar fil körs, så skicka inte in en sökväg appen inte litar på.
bool openFile(const std::string& path);

// Användarskalan — appens egen multiplikator OVANPÅ OS-skalan:
// uiScale() = osScale × userScale, default 1.0. App-global (som temat),
// backend-neutral: värdet lagras här, varje backends uiScale() läser in
// det EXAKT en gång. setUserScale klampas till 0.5–2.0 (loggas);
// NaN/oändligt/≤ 0 ignoreras helt. Sättaren LAGRAR bara — omräkningen av
// öppna fönster görs av anroparen via Window::userScaleChanged()
// (glaze::App::setUserScale äger den rundan). Ramverket exponerar API:t,
// appen äger lagringen (ingen inställningsfil här).
float userScale();
void  setUserScale(float s);

// GPU-PREFERENSEN — vilken GPU en dator
// med två ska rita med. `Default` är OS:ets och drivrutinens val:
// på en Optimus-bärbar tar D3D11 då DXGI:s första adapter, den integrerade
// (Intel UHD), och en 3D-scen på miljoner trianglar hamnar där av misstag.
// `HighPerformance` ber om den diskreta, `LowPower` om den snålaste.
//
// Anropas FÖRST I `main`, före det första fönstret — och före appens egna
// trådar och andra EGL-användare (trådförkravet): på Linux
// verkställs valet som MILJÖVARIABLER i `gfx::init`, och `setenv` är inte
// trådsäkert mot en `getenv` i en annan tråd (libcurl:s proxy i en `Worker`).
// Vilken GPU det blev läses i `Renderer::deviceName`/`adapterId`. App-global
// och backend-neutralt lagrad, som användarskalan; UI-trådens, säker före
// `init()`. Ett KORT (`setGpuAdapter` nedan) vinner över preferensen.
//
//   Windows  IDXGIFactory6::EnumAdapterByGpuPreference, vid varje NY
//            renderare (ett byte mitt i appen ger fönster på olika GPU:er);
//            utan fabriken (före Windows 10 1803) eller om enheten inte går
//            att skapa på adaptern: DXGI:s första adapter, och en logg
//   macOS    en enhet ur MTLCopyAllDevices som inte är (resp. är) isLowPower,
//            vid varje ny renderare; Apple silicon har en enda GPU
//   Linux    BARA i `gfx::init`, före första EGL-anropet: NVIDIA:s
//            `__NV_PRIME_RENDER_OFFLOAD=1` eller Mesas `DRI_PRIME=pci-…`
//            sätts i processens miljö. En variabel ANVÄNDAREN redan satt
//            (DRI_PRIME, __NV_PRIME_RENDER_OFFLOAD, __GLX_VENDOR_LIBRARY_NAME,
//            __EGL_VENDOR_LIBRARY_*, LIBGL_ALWAYS_SOFTWARE) vinner helt, och
//            ingenting sätts. Ett anrop efter init lagras och loggas — det
//            gäller nästa start. Barnprocesser ärver inte valet
//            (`childEnvironmentRemovals`).
enum class GpuPreference : std::uint8_t { Default, HighPerformance, LowPower };
void          setGpuPreference(GpuPreference p);
GpuPreference gpuPreference();

// GRAFIKKORTEN — listan och ett uttryckligt val.
//
// `id` HÅLLER ÖVER OMSTART och är det appen sparar i sina inställningar:
// PCI-identiteten `pci:vvvv:dddd:ssssssss:rr` (tillverkare, enhet, subsystem
// och revision i hex — samma på Windows och Linux), med `@bb:dd.f` (PCI-
// bussen) bara när två kort i listan annars fått samma id, och `#n` när
// adressen saknas. macOS har inga PCI-id i Metal: `metal:<namn>`, vid
// dubbletter `@<location>.<locationNumber>`. Linux icke-PCI-kort (en SoC):
// `drm:<drivrutin>@<enhet>`. Namnet ingår inte, för en drivrutinsuppdatering
// kan byta det.
//
// `kind` är en HEURISTIK utanför Windows hybridflaggor: Linux läser
// drivrutinen (amdgpu med minst 1 GiB eget minne är Discrete — en APU med en
// stor UMA-buffert också; Intel Arc listas som Integrated), macOS
// `isRemovable`/`hasUnifiedMemory`. `memoryBytes` är det EGNA minnet; på macOS
// `recommendedMaxWorkingSetSize`, som med delat minne är en andel av RAM.
//
// `selectable` = `setGpuAdapter` kan välja kortet HÄR. Windows: alltid.
// macOS: inte en `isHeadless`. Linux: kortet går att nå med miljövariablerna
// ovan (inte ett NVIDIA-kort bland flera, inte ett kort vars drivrutin Mesa
// inte känner om det inte är förvalet, och inget alls när användarens
// variabel gäller). Ett uttryckligt Mesa-kort är valbart också när
// förvalskortet drivs av NVIDIA:s drivrutin — det förvalet är uppskattat;
// bara preferensen undviker det.
//
// `isDefault` = det `Default` ritar med. Windows: DXGI:s första adapter.
// UPPSKATTAT på macOS (Apples regel — den första som varken är isLowPower,
// isHeadless eller isRemovable — utan att fråga systemet, för den frågan slår
// om hela systemet till den kraftfulla GPU:n) och på Linux (kortet med
// `boot_vga`; Mesas verkliga förval är kompositorns kort).
enum class GpuKind : std::uint8_t { Unknown, Integrated, Discrete, External, Software };

struct GpuAdapter {
    std::string   id;
    std::string   name;            // "NVIDIA GeForce RTX 4070 Laptop GPU"
    std::string   vendor;          // "NVIDIA", "AMD", "Intel" … tom = okänd
    std::uint32_t vendorId    = 0; // PCI; 0 = okänt (Metal, icke-PCI)
    std::uint32_t deviceId    = 0;
    GpuKind       kind        = GpuKind::Unknown;
    std::uint64_t memoryBytes = 0; // 0 = okänt
    bool          selectable  = false;
    bool          isDefault   = false;
    // Kortets id för den HÄR körningen, som ett annat grafik-API anger det
    // (D3D11: LUID:ens två halvor, HighPart i de övre 32 bitarna — samma form
    // som `NativeDevice::localAdapterId`). Tomt där det saknas. Byter värde
    // mellan körningar; det stabila id:t är `id`.
    std::optional<std::uint64_t> localAdapterId;
};

// Listan i systemets ordning, utan sidoeffekter: Windows skapar en enhet utan
// kontext per kort som saknar hybridflaggor för att läsa slaget (tiotals ms),
// Linux läser sysfs och rör aldrig EGL, macOS anropar aldrig
// `MTLCreateSystemDefaultDevice`. Säker före `init()`. UI-trådens.
std::vector<GpuAdapter> gpuAdapters();

// Det UTTRYCKLIGA valet — `id` ur listan, "" = inget (preferensen gäller).
// Samma tidpunkt och samma trådförkrav som `setGpuPreference`: först i
// `main`. Ett id som finns och är `selectable` vinner över preferensen; ett
// som saknas eller inte går att välja loggas EN gång, och då väljer
// preferensen bland de valbara korten. Verkar vid varje ny renderare på
// Windows och macOS, och bara i `gfx::init` på Linux. `gpuAdapter()` svarar
// med BEGÄRAN (som `appId()`), inte med kortet som ritar — det är
// `Renderer::adapterId`.
void        setGpuAdapter(const std::string& id);
std::string gpuAdapter();

// MILJÖVARIABLERNA GLAZEUI SJÄLV SATT i processen — namnen en barnprocess
// INTE ska ärva (en webbläsare eller ett skal ska inte
// hamna på den diskreta GPU:n för att appen gjorde det). Tom på Windows och
// macOS, och på Linux före `init()` eller när inget sattes. Ramverkets egna
// `openUrl`/`openFile` startar redan utan dem; en app eller ett sidopaket som
// startar processer själv (terminalen) tar bort dem ur barnets miljö. En
// variabel användaren satt står aldrig här.
std::vector<std::string> childEnvironmentRemovals();

// SKÄRMKALIBRERINGEN — appens egen mätning av en skärm,
// i FYSISKA px per mm, per skärm-id (`DisplayMetrics::id`). App-global som
// användarskalan, och appen äger lagringen. 0 tar bort kalibreringen; ett
// tomt id eller ett värde utanför 50–800 dpi loggas och ignoreras.
// `effectiveDisplay` lägger den på en mätning: finns en kalibrering för
// mätningens id blir svaret Calibrated med appens tal, annars mätningen som
// den är. I en app går vägen genom `glaze::App::setDisplayCalibration`, som
// också mäter om varje öppet fönster. UI-trådens, som resten av fasaden.
void           setDisplayCalibration(const std::string& id, float pxPerMm);
DisplayMetrics effectiveDisplay(const DisplayMetrics& m);

// OS:ets ljus/mörkt-läge just nu. Läses färskt vid varje anrop (ingen
// cache, ingen polling — anropas när något ska resolvas, typiskt per
// repaint). Kan OS:et inte svara → Light (Windows-defaulten). Byten
// annonseras som EventType::SystemThemeChanged.
enum class SystemTheme { Light, Dark };
SystemTheme systemTheme();

// OS:ets SPRÅKVAL som en BCP-47-tagg ("sv-SE", "nb-NO", "fi"), eller TOM
// STRÄNG när OS:et inte har någon preferens att ge. Den
// är fasadens jobb av samma skäl som `systemTheme` — det är en fråga till
// OS:et, och glaze-lagret får aldrig se en plattformsdetalj.
//
// Normaliseringen är fasadens ansvar och inte anroparens: POSIX-formen
// `sv_SE.UTF-8` och `nb_NO@variant` blir `sv-SE` respektive `nb-NO`, och
// `C`/`POSIX` blir TOM STRÄNG — det är en frånvaro av val, inte ett
// språk. Anroparen får alltså alltid antingen en tagg eller ingenting.
//
// KEDJAN ovanpå den (`glaze::setLanguage` → OS-lokalen → engelska) bor i
// widget-lagret; fasaden svarar bara på frågan.
//
//   Linux    LC_ALL → LC_MESSAGES → LANG (POSIX-prioriteten)
//   Windows  GetUserDefaultLocaleName
//   macOS    NSLocale.preferredLanguages.firstObject
std::string systemLocale();

// APPMENYNS AVSLUTA-POST. macOS är den enda plattformen
// med en meny som OS:et ritar och vi ändå fyller: appmenyn i skärmens
// menyrad, där "Avsluta <app>" måste finnas för att Cmd+Q ska fungera. Den
// visar text, och texten är ÖVERSATT — så den kommer UPPIFRÅN: glaze-lagret
// trycker in prefixet ur språkbutiken (med sitt avslutande blanksteg,
// "Avsluta "/"Quit "/"Lopeta ") efter `init()` och vid varje språkbyte, och
// backenden lägger processnamnet efter. Fasaden bär aldrig en egen text.
//
//   macOS    sätter postens titel (prefix + processnamn)
//   Windows  no-op — ingen appmeny (fönstrets systemmeny är OS:ets egen)
//   Linux    no-op — ingen global appmeny i protokollen vi talar
void setQuitMenuPrefix(const std::string& prefix);

// KÄNDA PLATSER — filväljarens vänsterkolumn: hemmet,
// skrivbordet, dokumenten, de hämtade filerna, bilderna och de monterade
// volymerna. Det är det ENDA OS-specifika i filväljaren — katalogläsningen
// är ren `std::filesystem` i widget-lagret — för VAR användarens mappar bor
// är en fråga till OS:et: de kan vara omdirigerade (OneDrive), översatta
// (`~/Skrivbord`) eller flyttade, och vilka volymer som finns vet bara
// OS:et. Fasaden svarar med en LISTA MED SÖKVÄGAR och aldrig med en dialog
// — dialogerna är våra.
//
// KONTRAKTET:
//   * ORDNINGEN är fast: Home, Desktop, Documents, Downloads, Pictures (de
//     som finns), sedan volymerna i OS:ets ordning.
//   * Varje ANVÄNDARMAPP finns och är en katalog när listan byggs. En som
//     saknas hoppas över, och en som är hemkatalogen själv likaså (XDG:s
//     sätt att stänga av en mapp är att peka den på `$HOME`).
//   * VOLYMERNA är de OS:et rapporterar som monterade och redo. En
//     NÄTVERKSENHET (Windows) listas UTAN att kontaktas — en frånkopplad
//     hade annars frusit dialogen i SMB:s tidsgräns — och kan därför visa
//     sig sakna innehåll när man öppnar den.
//   * Sökvägarna är UTF-8 och absoluta. `label` är volymens NAMN
//     ("Windows (C:)", "USB", "/"); användarmapparna har inget, för de
//     namnges av anroparen — "Dokument" följer appens språk, inte OS:ets.
//
//   Windows  SHGetKnownFolderPath (FOLDERID_Profile/Desktop/Documents/
//            Downloads/Pictures) + GetLogicalDriveStringsW/GetDriveTypeW/
//            GetVolumeInformationW
//   Linux    $HOME + XDG user dirs ($XDG_CONFIG_HOME/user-dirs.dirs) +
//            "/" och monteringarna under /media och /run/media
//            (/proc/self/mounts)
//   macOS    NSFileManager URLsForDirectory:inDomains: + /Volumes
enum class PlaceKind { Home, Desktop, Documents, Downloads, Pictures, Volume };

struct Place {
    PlaceKind   kind = PlaceKind::Home;
    std::string path;    // UTF-8, absolut
    std::string label;   // volymens namn — tom för användarmapparna
};

std::vector<Place> knownPlaces();

// DÖLJER OS:ET POSTEN? — det OS:ets EGEN filhanterare
// inte visar, utöver punktnamnen. Filväljaren läser mappen med ren
// `std::filesystem`, men Windows döljer med ett ATTRIBUT och macOS med en
// FLAGGA, och inget av dem syns i namnet eller går att läsa med std: utan
// frågan visar Hem på Windows profilens `NTUSER.DAT*` och ett tiotal
// gamla kompatibilitetskorsningar ("Mina dokument", "Mallar" …) som inte
// ens går att öppna. Punktnamnen döljer filväljaren själv på alla
// plattformar; den här svarar bara på det OS:et vet utöver dem.
//
//   Windows  GetFileAttributesW → FILE_ATTRIBUTE_HIDDEN
//   macOS    lstat → st_flags & UF_HIDDEN (`~/Library`)
//   Linux    alltid false — där ÄR punkten hela konventionen
//
// Ett anrop som misslyckas svarar false: en post som inte går att fråga om
// visas hellre än göms.
bool isHiddenFile(const std::string& utf8Path);

// APPENS ID — namnet skrivbordet känner igen appen
// på och hittar dess `.desktop`-fil (och därmed ikonen) genom. Det är bara
// Linux som frågar: Wayland `xdg_toplevel_set_app_id` och X11 `WM_CLASS`
// (res_name och res_class). Windows och macOS identifierar appen genom
// programfilen och läser aldrig värdet.
//
// Anropas FÖRE det första fönstret — ett fönster läser id:t när det skapas.
// TOM sträng = förvalet, PROGRAMFILENS NAMN utan katalog och filändelse
// (`demo`), och det är det `appId()` svarar med då. Tillåtna tecken är
// `[A-Za-z0-9._-]` (en `.desktop`-fils namn utan `.desktop`), högst 255; ett
// ogiltigt id loggas och ignoreras, så det föregående gäller. App-globalt och
// backend-neutralt lagrat, som användarskalan; UI-trådens.
void        setAppId(const std::string& id);
std::string appId();

// DOCKAN — om APPEN har en ikon i macOS-dockan.
// App-global och skild från fönstrens knappar i aktivitetsfältet
// (`WindowDesc::showInTaskbar`) med flit: det är två begrepp, och en funktion
// som gjorde olika saker per OS hade dolt det. Anropas FÖRE det första
// fönstret, som `setAppId` — då läser `init()` värdet och dockikonen blinkar
// aldrig till.
//
//   macOS    NSApplicationActivationPolicyRegular (true) eller Accessory
//            (false — en app som bor i menyradens systemfält). Efter init
//            byts policyn direkt, och bytet kan lägga fönstren bakåt
//   Windows  ingen docka — begäran lagras och false loggas en gång; fönstrets
//            knapp styrs per fönster (`WindowDesc::showInTaskbar`)
//   Linux    ingen docka i protokollen vi talar — lagras, false loggas en gång
//
// `showInDock()` svarar med BEGÄRAN (som `appId()`), förval true.
// UI-trådens, som resten av fasaden.
void setShowInDock(bool show);
bool showInDock();

// SKÄRMARNA — varje ansluten skärm i
// PLACERINGSRYMDEN (`WindowDesc::posX`/`setPosition`s rymd): Win32 fysiska
// px, AppKit punkter med origo uppe till vänster på huvudskärmen, X11 rotens
// px. glaze räknar startplatsen ur listan (centrering, klämning in i
// arbetsytan) — en implementation i glaze, grindad headless, i stället för
// fyra blinda i backendarna. Ingen app räknar i rymden själv.
//
// `scale` är PLACERINGSENHETER PER LOGISK PX för ett fönster på skärmen:
// Win32 skärmens DPI/96 × användarskalan, macOS användarskalan (punkterna är
// redan logiska), X11 Xft.dpi/96 × användarskalan. Ett fönster med den
// logiska bredden B är alltså B × `scale` placeringsenheter brett där.
// `primary` är skärmen origo hör till (Win32 MONITORINFOF_PRIMARY, macOS
// `screens[0]`, X11 RandR:s primära utgång eller skärmen vid origo).
//
//   Windows  EnumDisplayMonitors + GetMonitorInfoW + GetDpiForMonitor
//   macOS    [NSScreen screens] — frame och visibleFrame, vända mot
//            huvudskärmens höjd
//   X11      XRandR:s aktiva crtc:er, arbetsytan ur `_NET_WORKAREA` (EN
//            rektangel för hela skrivbordet — med paneler på bara en av
//            flera skärmar blir arbetsytan per skärm ungefärlig)
//   Wayland  TOM — det finns ingen skärmrymd; kompositören placerar
//   headless TOM
//
// Tom också före init. UI-trådens, som resten av fasaden.
struct ScreenInfo {
    Rectf bounds;          // hela skärmen, placeringsrymden
    Rectf work;            // arbetsytan (utan aktivitetsfält/menyrad)
    float scale   = 1.0f;  // placeringsenheter per LOGISK px för ett fönster där
    bool  primary = false;
};
std::vector<ScreenInfo> screens();

// Backend-fabriken: skapar ett fönsterobjekt UTAN OS-fönster (tvåfas —
// anropa init(desc) när fönstret ska öppnas, se gfx::Window).
std::unique_ptr<Window> createWindow();

} // namespace gfx
