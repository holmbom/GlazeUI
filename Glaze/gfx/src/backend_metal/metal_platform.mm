// AppKit-implementationen av plattformsfasaden (gfx/platform.h): global init
// (NSApplication + aktiveringspolicy + minimal menyrad), den händelsestyrda
// event-pumpen, loggning, exe-katalog, klippbord, systemtema och
// fönsterfabriken. Spegel av d3d11_platform.cpp.
//
// VI äger runloopen — [NSApp run] körs ALDRIG. waitEvent blockerar i
// nextEventMatchingMask:untilDate:distantFuture (~0 % CPU i vila — hård
// regel 1) och skickar varje NSEvent genom [NSApp sendEvent:] — utom
// Cmd-släppet, som går direkt till fönstret (`dispatchNSEvent`); GlazeView/
// delegaterna översätter och köar gfx::Event via pushEvent, som ALLTID
// postar ett väcknings-event så pumpen vaknar även när källan inte var ett
// NSEvent (NSTimer/KVO). Etablerat mönster — GLFW gör exakt så.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>

#include <dlfcn.h>          // dladdr (isInProgramFile)
#include <mach-o/dyld.h>    // _dyld_get_image_header (isInProgramFile)
#include <os/log.h>         // os_log (log)
#include <sys/stat.h>       // lstat + UF_HIDDEN (isHiddenFile)

#include "../icon_util.h"
#include "metal_backend.h"
#include "gfx/platform.h"

// ---------------------------------------------------------------- ObjC-sidan

// App-delegaten: Cmd+Q/OS-avslut går genom applicationShouldTerminate —
// vi svarar ALDRIG "ja" direkt utan köar Quit (windowId 0) och låter appen
// (glaze::App) bestämma, precis som WM_CLOSE-vägen returnerar 0 i D3D11.
@interface GlazeAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation GlazeAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender
{
    gfx::Event e;
    e.type = gfx::EventType::Quit;
    gfx::pushEvent(e);
    return NSTerminateCancel;
}
@end

// KVO-observatör på NSApp.effectiveAppearance: OS:et bytte ljus/mörkt →
// SystemThemeChanged till varje öppet fönster (spegeln av
// WM_SETTINGCHANGE-per-fönster — repainten resolvar temat på nytt).
@interface GlazeThemeObserver : NSObject
@end

namespace gfx {
namespace {
void broadcastThemeChanged();
} // namespace
} // namespace gfx

@implementation GlazeThemeObserver
- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context
{
    gfx::broadcastThemeChanged();
}
@end

namespace gfx {

// ---------------------------------------------------------------- state

namespace {

GlazeAppDelegate*   appDelegate   = nil;
GlazeThemeObserver* themeObserver = nil;
// Appmenyns "Avsluta <app>" — titeln sätts av `setQuitMenuPrefix`, som
// glaze-lagret anropar med det ÖVERSATTA prefixet efter `init()` och vid
// varje språkbyte. Fasaden bär ingen egen text.
NSMenuItem*         quitItem      = nil;

// Event-kön — enkeltrådad (AppKit levererar allt på main-tråden; NSTimer
// och KVO likaså). Samma std::deque som d3d11_platform.cpp.
std::deque<Event>& queue()
{
    static std::deque<Event> q;
    return q;
}

bool popQueued(Event& e)
{
    if (queue().empty())
        return false;
    e = queue().front();
    queue().pop_front();
    return true;
}

// Fönsterregistret för tema-utskicket (id-lista räcker — eventen routas
// av glaze::App via Window::id()).
std::vector<WindowId>& windowIds()
{
    static std::vector<WindowId> ids;
    return ids;
}

void broadcastThemeChanged()
{
    for (WindowId id : windowIds()) {
        Event e;
        e.type     = EventType::SystemThemeChanged;
        e.windowId = id;
        pushEvent(e);
    }
}

// CMD-SLÄPPET: `[NSApp sendEvent:]` levererar
// inget `keyUp:` för en tangent som släpps medan Cmd hålls — AppKit tar
// Cmd-tangenterna som menygenvägar och sväljer släppet (cocoa-dev 2003,
// Ghostty, OpenJDK AWT). Ett KeyUp med Command går därför direkt till
// fönstret (`ev.window`, annars nyckelfönstret), vars vy köar det som vilket
// släpp som helst; allt annat går den vanliga vägen genom NSApp. Utan det
// stod tangenten kvar i `isKeyHeld` till nästa fokusförlust, och `onKeyUp`
// kom aldrig. EN väg för båda pumparna.
void dispatchNSEvent(NSEvent* ev)
{
    if (ev.type == NSEventTypeKeyUp &&
        (ev.modifierFlags & NSEventModifierFlagCommand) != 0) {
        NSWindow* const w = ev.window ? ev.window : NSApp.keyWindow;
        if (w) {
            [w sendEvent:ev];
            return;
        }
    }
    [NSApp sendEvent:ev];
}

} // namespace

void registerWindowId(WindowId id)
{
    windowIds().push_back(id);
}

void unregisterWindowId(WindowId id)
{
    auto& ids = windowIds();
    std::erase(ids, id);
}

// ---------------------------------------------------------------- events

void pushEvent(const Event& e)
{
    queue().push_back(e);
    // Väcknings-eventet är BÄRANDE: köas något utanför sendEvent:-flödet
    // (NSTimer, KVO, delegate-callbacks) sover nextEventMatchingMask annars
    // vidare — symtomet är "timern tickar först när jag rör musen"
    // (D3D11-pump-buggens macOS-tvilling). Postas alltid; ett överflödigt
    // väcknings-event dräneras tyst av pumpen.
    if (NSApp) {
        @autoreleasepool {
            NSEvent* wake =
                [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                   location:NSMakePoint(0, 0)
                              modifierFlags:0
                                  timestamp:0
                               windowNumber:0
                                    context:nil
                                    subtype:0
                                      data1:0
                                      data2:0];
            [NSApp postEvent:wake atStart:NO];
        }
    }
}

// TRÅDSÄKER VÄCKNING.
//
// `pushEvent` ovan gör redan BÅDA delarna — köar gfx-eventet OCH postar
// ett `NSEventTypeApplicationDefined` som väcker `nextEventMatchingMask`.
// Mönstret finns alltså; det som INTE går är att kalla det härifrån:
// **kön är uttryckligen ENKELTRÅDAD** (se `queue()` — AppKit levererar
// allt på main-tråden), och en workertråd som skriver i `std::deque`
// medan pumpen läser den är ett datakapplöpning.
//
// Lösningen: workern gör INGET annat än
// att hoppa till main-tråden. `dispatch_async` på main-kön är trådsäker
// och väcker run-loopen — och `nextEventMatchingMask:...inMode:
// NSDefaultRunLoopMode` KÖR run-loopen, så blocket exekveras även när
// pumpen står och blockerar. Väl på main-tråden är `pushEvent` det
// vanliga, redan bevisade anropet.
void postWakeup()
{
    if (!NSApp)
        return;   // före init / efter shutdown — no-op, som fasaden lovar
    dispatch_async(dispatch_get_main_queue(), ^{
        Event e;
        e.type     = EventType::Wakeup;
        e.windowId = 0;   // appens event, inte fönstrets
        pushEvent(e);     // MAIN-tråden: köar OCH postar AppKit-eventet
    });
}

bool waitEvent(Event& e)
{
    for (;;) {
        if (popQueued(e))
            return true;
        @autoreleasepool {
            // Blockerar tills nästa NSEvent (inkl. vårt väcknings-event) —
            // ~0 % CPU. NSTimer på runloopen tickar medan vi står här.
            NSEvent* ev =
                [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate distantFuture]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
            if (ev)
                dispatchNSEvent(ev);   // view/delegate köar ev. gfx::Event
        }
    }
}

bool pollEvent(Event& e)
{
    for (;;) {
        if (popQueued(e))
            return true;
        @autoreleasepool {
            NSEvent* ev =
                [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate distantPast]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
            if (!ev)
                return false;
            dispatchNSEvent(ev);
        }
    }
}

// ---------------------------------------------------------------- init/log

bool init()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        // AKTIVERINGSPOLICYN ur DOCKANS BEGÄRAN:
        // `glaze::setShowInDock` lagrar den före det första fönstret och den
        // läses HÄR, så en app som bor i menyraden aldrig blinkar till i
        // dockan. Regular (förvalet) = fönster + Dock-ikon trots att binären
        // körs rå ur build-trädet (ingen .app-bundle behövs under
        // utveckling); Accessory = fönster och statusposter, men ingen
        // dockikon och ingen menyrad. Ett paket som startas genom Finder kan
        // ändå visa ikonen kort innan raden körs — `LSUIElement` i
        // Info.plist är vägen dit och ingår inte.
        [NSApp setActivationPolicy:showInDock()
                                       ? NSApplicationActivationPolicyRegular
                                       : NSApplicationActivationPolicyAccessory];

        appDelegate = [GlazeAppDelegate new];
        [NSApp setDelegate:appDelegate];

        // Minimal menyrad: appmeny med Quit — utan den fungerar inte Cmd+Q.
        // TITELN är bara processnamnet tills glaze-lagret trycker in det
        // översatta prefixet (`setQuitMenuPrefix`, direkt efter init) — en
        // text i fasaden hade varit ett språk som inte följer appens.
        NSMenu*     menubar = [NSMenu new];
        NSMenuItem* appItem = [NSMenuItem new];
        [menubar addItem:appItem];
        NSMenu* appMenu = [NSMenu new];
        quitItem = [[NSMenuItem alloc]
            initWithTitle:[[NSProcessInfo processInfo] processName]
                   action:@selector(terminate:)
            keyEquivalent:@"q"];
        [appMenu addItem:quitItem];
        [appItem setSubmenu:appMenu];
        [NSApp setMainMenu:menubar];

        // Tema-KVO:n — NSApplication är KVO-kompatibel för
        // effectiveAppearance från 10.14.
        themeObserver = [GlazeThemeObserver new];
        [NSApp addObserver:themeObserver
                forKeyPath:@"effectiveAppearance"
                   options:0
                   context:nullptr];

        [NSApp finishLaunching];
        [NSApp activate];
    }
    trayInit();       // systemfältet öppnas när NSApp är klar (metal_tray.mm)
    appTimerInit();   // appens timer likaså (metal_timer.mm)
    return true;
}

void shutdown()
{
    // Statusposterna FÖRST — de hör till appen, och ingen ikon får stå kvar
    // i menyraden efter plattformen (metal_tray.mm).
    trayShutdown();
    // Appens timrar därefter — ingen tick efter plattformen (metal_timer.mm).
    appTimerShutdown();
    @autoreleasepool {
        if (themeObserver) {
            [NSApp removeObserver:themeObserver
                       forKeyPath:@"effectiveAppearance"];
            themeObserver = nil;
        }
        if (appDelegate) {
            [NSApp setDelegate:nil];
            appDelegate = nil;
        }
    }
}

void log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // Unified log (`log stream`-synligt) + stderr — terminal-körningar ser
    // den direkt (bättre än Windows-fallet, där bara debug-utgången finns).
    // os_log och inte NSLog: NSLog skriver SJÄLV till stderr också, så varje
    // rad kom två gånger (lang_gate räknar dem), och den läste %s som
    // MacRoman — åäö blev mojibake.
    os_log(OS_LOG_DEFAULT, "%{public}s", buf);
    std::fprintf(stderr, "%s\n", buf);
}

std::string basePath()
{
    @autoreleasepool {
        NSURL* exe = NSBundle.mainBundle.executableURL;
        if (!exe)
            return {};
        NSURL* dir = exe.URLByDeletingLastPathComponent;
        const char* p = dir.fileSystemRepresentation;
        if (!p)
            return {};
        std::string out(p);
        if (out.empty() || out.back() != '/')
            out += '/';   // avslutande separator — fasadens kontrakt
        return out;
    }
}

// dladdr svarar med Mach-O-huvudet för bilden som äger adressen, och dyld:s
// bild 0 är programfilen.
bool isInProgramFile(const void* address)
{
    Dl_info info{};
    if (!dladdr(address, &info))
        return false;
    return info.dli_fbase == static_cast<const void*>(_dyld_get_image_header(0));
}

// PROGRAMFILENS NAMN (icon_util.h) — förvalet för `appId()`,
// som macOS själv aldrig läser (appen identifieras genom paketet); svaret
// finns för att fasadens fråga ska svara likadant överallt. Sista
// komponenten i den körbara filens sökväg (`demo.app/Contents/MacOS/demo`).
std::string programName()
{
    @autoreleasepool {
        NSURL* exe = NSBundle.mainBundle.executableURL;
        if (!exe)
            return {};
        const char* p = exe.lastPathComponent.UTF8String;
        return p ? std::string(p) : std::string();
    }
}

// ---------------------------------------------------------------- klippbord

std::string clipboardText()
{
    @autoreleasepool {
        NSString* s = [NSPasteboard.generalPasteboard
            stringForType:NSPasteboardTypeString];
        if (!s)
            return {};
        const char* utf8 = s.UTF8String;
        return utf8 ? std::string(utf8) : std::string{};
    }
}

void setClipboardText(const std::string& utf8)
{
    @autoreleasepool {
        NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
        if (!s)
            return;   // ogiltig UTF-8 — rör inte klippbordet
        NSPasteboard* pb = NSPasteboard.generalPasteboard;
        [pb clearContents];
        [pb setString:s forType:NSPasteboardTypeString];
    }
}

// ------------------------------------------------------- openUrl / openFile

namespace {

// Öppningen openUrl och openFile delar: texten blir en NSURL — en adress, eller
// med `asPath` en filsökväg (`fileURLWithPath:` kodar mellanslag och tecken
// utanför ASCII åt oss) — och OS:ets standardhanterare får den. `vad` är
// anroparens namn i loggen.
bool workspaceOpen(const std::string& utf8, bool asPath, const char* vad)
{
    @autoreleasepool {
        NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
        if (!s)
            return false;   // ogiltig UTF-8
        NSURL* u = asPath ? [NSURL fileURLWithPath:s]
                          : [NSURL URLWithString:s];
        if (!u) {
            log("%s: ogiltig adress", vad);
            return false;
        }
        if (![NSWorkspace.sharedWorkspace openURL:u]) {
            log("%s: NSWorkspace openURL nekade", vad);
            return false;
        }
        return true;
    }
}

} // namespace

bool openUrl(const std::string& url)
{
    if (url.empty())
        return false;
    return workspaceOpen(url, false, "openUrl");
}

bool openFile(const std::string& path)
{
    if (path.empty())
        return false;
    return workspaceOpen(path, true, "openFile");
}

SystemTheme systemTheme()
{
    // Läses färskt per anrop (som registry-läsningen i D3D11) — värdet
    // resolvas när något ska ritas, aldrig cachat/pollat.
    @autoreleasepool {
        NSAppearanceName name = [NSApp.effectiveAppearance
            bestMatchFromAppearancesWithNames:@[
                NSAppearanceNameAqua, NSAppearanceNameDarkAqua
            ]];
        return [name isEqualToString:NSAppearanceNameDarkAqua]
                   ? SystemTheme::Dark
                   : SystemTheme::Light;
    }
}

std::uint32_t doubleClickMs()
{
    // Användarens inställning (Systeminställningar → Hjälpmedel → Pekare),
    // i SEKUNDER — läst färskt per anrop, som systemTheme.
    const double s = [NSEvent doubleClickInterval];
    return s > 0.0 ? static_cast<std::uint32_t>(s * 1000.0 + 0.5) : 500u;
}

std::string systemLocale()
{
    // `preferredLanguages` är ANVÄNDARENS språkordning (och alltså rätt
    // fråga för översättningar), medan `NSLocale.currentLocale` bär
    // REGIONEN — den svarar ofta "en_SE" för en svensk som kör systemet
    // på engelska. Formen är redan BCP-47 ("nb-NO", "sv"), så ingen
    // normalisering behövs.
    @autoreleasepool {
        NSArray<NSString*>* langs = [NSLocale preferredLanguages];
        if (langs.count == 0)
            return std::string();
        NSString* first = langs.firstObject;
        if (first.length == 0)
            return std::string();
        std::string ut(first.UTF8String ? first.UTF8String : "");
        if (ut == "C" || ut == "POSIX")
            return std::string();
        return ut;
    }
}

// Prefixet bär sitt
// avslutande blanksteg ("Avsluta "), processnamnet läggs efter. No-op före `init()`.
void setQuitMenuPrefix(const std::string& prefix)
{
    if (!quitItem)
        return;
    @autoreleasepool {
        NSString* p = [NSString stringWithUTF8String:prefix.c_str()];
        if (!p)
            p = @"";
        [quitItem setTitle:[p stringByAppendingString:
                                  [[NSProcessInfo processInfo] processName]]];
    }
}

// ------------------------------------------------------------ knownPlaces
//
// Användarmapparna ur
// NSFileManager — de kan ligga någon annanstans än `~/Desktop` (iCloud
// Drive flyttar Skrivbord och Dokument) — och volymerna ur /Volumes, där
// startvolymen är en symbolisk länk till "/". Sökvägarna lämnas i
// `fileSystemRepresentation` (filsystemets egen UTF-8-form), etiketten i
// `UTF8String` (den visas och jämförs aldrig mot en fil).

namespace {

bool isDirectory(NSFileManager* fm, NSString* p)
{
    BOOL dir = NO;
    return p.length > 0 && [fm fileExistsAtPath:p isDirectory:&dir] && dir;
}

} // namespace

std::vector<Place> knownPlaces()
{
    std::vector<Place> ut;
    @autoreleasepool {
        NSFileManager* fm  = [NSFileManager defaultManager];
        NSString*      hem = NSHomeDirectory();
        if (isDirectory(fm, hem))
            ut.push_back(Place{PlaceKind::Home, hem.fileSystemRepresentation, {}});
        auto mapp = [&](PlaceKind k, NSSearchPathDirectory d) {
            NSArray<NSURL*>* urls = [fm URLsForDirectory:d
                                               inDomains:NSUserDomainMask];
            NSString* p = urls.count > 0 ? urls.firstObject.path : nil;
            // Hemmet SJÄLVT är ingen egen plats (fasadens regel).
            if (isDirectory(fm, p) && ![p isEqualToString:hem])
                ut.push_back(Place{k, p.fileSystemRepresentation, {}});
        };
        mapp(PlaceKind::Desktop, NSDesktopDirectory);
        mapp(PlaceKind::Documents, NSDocumentDirectory);
        mapp(PlaceKind::Downloads, NSDownloadsDirectory);
        mapp(PlaceKind::Pictures, NSPicturesDirectory);

        NSArray<NSString*>* volymer =
            [fm contentsOfDirectoryAtPath:@"/Volumes" error:nil];
        for (NSString* namn in volymer) {
            if ([namn hasPrefix:@"."])
                continue;
            NSString* p = [@"/Volumes" stringByAppendingPathComponent:namn];
            if (!isDirectory(fm, p))
                continue;
            ut.push_back(Place{PlaceKind::Volume, p.fileSystemRepresentation,
                               namn.UTF8String ? namn.UTF8String : ""});
        }
    }
    return ut;
}

// Finder döljer det som bär BSD-flaggan UF_HIDDEN
// (`chflags hidden` — `~/Library` är det vanliga fallet); flaggan läses på
// posten SJÄLV (lstat), inte på det en länk pekar på.
bool isHiddenFile(const std::string& utf8Path)
{
    struct stat st{};
    return ::lstat(utf8Path.c_str(), &st) == 0 && (st.st_flags & UF_HIDDEN) != 0;
}

// ---------------------------------------------------------------- skärmarna
//
// SKÄRMARNA. `frame` och `visibleFrame` (utan menyrad och
// docka) vända mot huvudskärmens höjd till placeringsrymden (`toPlacement`,
// samma vändning som `setPosition`). Placeringsrymden är PUNKTER, och
// punkterna bär användarskalan (punkter = logiska px × userScale — samma
// räkning som fönstrets innehållsrect i init), så `scale` är userScale på
// varje skärm: backing-faktorn hör till bufferten, inte till placeringen.
// Huvudskärmen är `screens[0]` — den bär det globala origot.
std::vector<ScreenInfo> screens()
{
    std::vector<ScreenInfo> ut;
    if (!NSApp)
        return ut;   // före init
    @autoreleasepool {
        NSArray<NSScreen*>* alla = NSScreen.screens;
        for (NSUInteger i = 0; i < alla.count; ++i) {
            NSScreen*  s = alla[i];
            ScreenInfo si;
            si.bounds  = toPlacement(s.frame);
            si.work    = toPlacement(s.visibleFrame);
            si.scale   = userScale();
            si.primary = i == 0;
            ut.push_back(si);
        }
    }
    return ut;
}

// ---------------------------------------------------------------- fabrik

std::unique_ptr<Window> createWindow()
{
    return std::make_unique<MetalWindow>();
}

} // namespace gfx
