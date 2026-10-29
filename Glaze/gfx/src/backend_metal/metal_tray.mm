// Systemfältet på macOS — menyradens statusposter.
//
// EN `NSStatusItem` PER ID, kvadratisk (`NSSquareStatusItemLength`), med en
// bild i 18 × 18 punkter. Fasaden (gfx/tray.h) lägger ikonen i fältet,
// levererar händelserna, svarar var den står och tickar animationen — menyn
// är glaze:s egen popup och tooltipen knappens egen (`button.toolTip`).
//
// HÄNDELSERNA köas som `EventType::Tray` med windowId 0 (gfx/events.h):
//   * KLICKEN är knappens action med `sendActionOn:` vänster- OCH högersläpp.
//     Högerklick eller Ctrl+klick = ContextMenu; ett jämnt `clickCount` =
//     DoubleClick, ett udda = Click (Windows mönster: klick, dubbelklick,
//     klick …), så ett dubbelklick ger Click och därefter DoubleClick som
//     kontraktet säger. En action UTAN mushändelse (VoiceOver,
//     tangentbordet) är ett Click. Ankaret är pekaren i placeringsrymden.
//   * Vid Click och DoubleClick AKTIVERAS appen FÖRE eventet köas: en app
//     med Accessory-policy (ingen dockikon) aktiveras inte av ett klick i
//     menyraden, och ett fönster som handlern visar hade annars hamnat bakom
//     den app användaren stod i. ContextMenu aktiverar inte — menyns
//     värdfönster gör det själv (`raise()`).
//   * HOVERN är en spåryta på knappen: `EnteredAndExited | ActiveAlways |
//     InVisibleRect`, utan `CursorUpdate` (stöds inte ihop med ActiveAlways,
//     NSTrackingArea.h). Dubbletter sållas av glaze (gfx/events.h).
//   * TICKEN är en NSTimer i CommonModes — den tickar också medan en meny
//     eller ett fönsterdrag kör sin egen runloop-fas. `pushEvent` postar
//     väcknings-eventet, så `waitEvent` vaknar.
//
// LIVSTIDEN följer plattformen: `trayInit()` (ur `gfx::init`) öppnar fasaden
// och `trayShutdown()` (ur `gfx::shutdown`) tar bort varje statuspost och
// stänger den. Före och efter svarar `setTrayIcon` false utan en loggrad och
// resten gör ingenting — en `glaze::TrayIcon` som lever längre än appen kan
// inte lägga en ikon i fältet efter processens slut (som D3D11:s
// `platformInitialized`). `trayAvailable()` svarar ändå: menyraden finns på
// varje Mac.
//
// DOCKAN bor också här: `applyShowInDock` är aktiveringspolicyn. Före init
// finns bara begäran (platform.cpp), och `gfx::init` läser den
// (metal_platform.mm), så en app som bor i menyraden aldrig blinkar till i
// dockan; efter init byts policyn direkt.

#include <map>
#include <string>

#include "../icon_util.h"
#include "metal_backend.h"
#include "gfx/platform.h"
#include "gfx/tray.h"

namespace gfx {
namespace {

// Ett systemfältsevent till kön. `x`/`y` är ankaret i PLACERINGSRYMDEN
// (punkter, origo uppe till vänster på huvudskärmen — som `setPosition`), 0
// för Enter, Leave och Tick.
void pushTray(TrayId trayId, TrayAction a, float x, float y)
{
    Event e;
    e.type       = EventType::Tray;
    e.windowId   = 0;   // appens, inte ett fönsters
    e.trayId     = trayId;
    e.trayAction = a;
    e.mouseX     = x;
    e.mouseY     = y;
    pushEvent(e);
}

// Pekaren i placeringsrymden: AppKit räknar från HUVUDskärmens nederkant,
// fasaden från dess överkant (samma vändning som `MetalWindow::setPosition`).
Pointf pointerInPlacementSpace()
{
    const NSPoint p = [NSEvent mouseLocation];
    const Rectf   r = toPlacement(NSMakeRect(p.x, p.y, 0.0, 0.0));
    return Pointf{r.x, r.y};
}

} // namespace
} // namespace gfx

// ---------------------------------------------------------------- ObjC-sidan

// Mottagaren för EN statuspost: knappens action (klicken) och spårytans in
// och ut (hovern). Id:t är allt den bär — glaze routar eventet på det.
// Ägs av postens Entry: knappens `target` och spårytans `owner` håller den
// inte vid liv.
@interface GlazeTrayTarget : NSObject {
  @public
    gfx::TrayId trayId_;
}
- (void)clicked:(id)sender;
@end

@implementation GlazeTrayTarget

- (void)clicked:(id)sender
{
    (void)sender;
    NSEvent* const ev = NSApp.currentEvent;
    const bool     vänster = ev && ev.type == NSEventTypeLeftMouseUp;
    const bool     höger   = ev && ev.type == NSEventTypeRightMouseUp;

    gfx::TrayAction a = gfx::TrayAction::Click;
    if (höger || (vänster && (ev.modifierFlags & NSEventModifierFlagControl)))
        a = gfx::TrayAction::ContextMenu;
    else if (vänster && ev.clickCount % 2 == 0)
        a = gfx::TrayAction::DoubleClick;

    if (a != gfx::TrayAction::ContextMenu)
        [NSApp activate];   // FÖRE eventet — se filhuvudet
    const gfx::Pointf p = gfx::pointerInPlacementSpace();
    gfx::pushTray(trayId_, a, p.x, p.y);
}

- (void)mouseEntered:(NSEvent*)ev
{
    (void)ev;
    gfx::pushTray(trayId_, gfx::TrayAction::Enter, 0.0f, 0.0f);
}

- (void)mouseExited:(NSEvent*)ev
{
    (void)ev;
    gfx::pushTray(trayId_, gfx::TrayAction::Leave, 0.0f, 0.0f);
}

@end

namespace gfx {

namespace {

// Fältets bild i PUNKTER — menyradens statusposter ritar 18 pt.
constexpr CGFloat kTrayImagePt = 18.0;
// Ramarna valregeln väljer för 1x och 2x: 18 och 36 px (`pickIconImage` —
// minsta ram med sidan ≥ målet, annars den största).
constexpr int kTrayImagePx1x = 18;
constexpr int kTrayImagePx2x = 36;

// En statuspost. ObjC-pekarna är __strong (ARC i C++-medlemmar), så posten
// håller allt den skapat vid liv tills `dropEntry` släpper det.
struct Entry {
    NSStatusItem*    item     = nil;
    GlazeTrayTarget* target   = nil;
    NSTrackingArea*  tracking = nil;
    NSTimer*         timer    = nil;
    std::string      toolTip;   // den som står på knappen — byts bara vid ändring
};

// Posterna per id. UI-trådens, som event-kön (metal_platform.mm).
//
// I NAMNRYMDEN och inte som en lokal static med flit: den konstrueras före
// main och rivs därför EFTER glaze::App (som skapas lat, i main). En lokal
// static hade skapats vid första `setTrayIcon` — efter App — och rivits FÖRE
// den, så `trayShutdown` från ~App via gfx::shutdown hade rört en riven
// karta.
std::map<TrayId, Entry> g_entries;

// Öppen mellan `trayInit` och `trayShutdown` (gfx::init / gfx::shutdown).
bool g_live = false;

// BILDEN i fältet: `programFirst` + paketikon ⇒ appens ikon (paketets
// `CFBundleIconFile`, samma regel som `MetalWindow::applyIcon`), annars en
// representation för 1x och en för 2x ur ramarna — AppKit väljer den som
// passar skärmen. nil = ingen bild att visa. Mallbilden sätts med meddelande
// och inte med punktsyntax: `template` är ett reserverat ord i C++.
NSImage* trayImage(const TrayIconDesc& d)
{
    NSImage* bild = nil;
    if (d.image.programFirst && bundleHasIcon()) {
        // En KOPIA — `imageNamed:` delar sin instans med hela appen, och
        // storleken nedan hade annars krympt dockans och Finders ikon.
        bild      = [[NSImage imageNamed:NSImageNameApplicationIcon] copy];
        bild.size = NSMakeSize(kTrayImagePt, kTrayImagePt);
    } else {
        // Ramarna är kvadratiska (glaze passar in dem); en avlång ram
        // sträcks till kvadraten.
        const NSSize     pt  = NSMakeSize(kTrayImagePt, kTrayImagePt);
        const IconImage* en  = pickIconImage(d.image, kTrayImagePx1x);
        const IconImage* två = pickIconImage(d.image, kTrayImagePx2x);
        if (en) {
            bild = [[NSImage alloc] initWithSize:pt];
            if (NSBitmapImageRep* r = iconRep(*en, pt))
                [bild addRepresentation:r];
            if (två && två != en) {
                if (NSBitmapImageRep* r = iconRep(*två, pt))
                    [bild addRepresentation:r];
            }
            if (bild.representations.count == 0)
                bild = nil;
        }
    }
    [bild setTemplate:d.templateImage ? YES : NO];
    return bild;
}

// Knappens tooltip — tom text = ingen. Ogiltig UTF-8 blir ingen tooltip.
NSString* toolTipText(const std::string& s)
{
    return s.empty() ? nil : [NSString stringWithUTF8String:s.c_str()];
}

// Släpper postens allt: timern, spårytan, knappens koppling och statusposten.
void dropEntry(Entry& e)
{
    [e.timer invalidate];
    e.timer = nil;
    if (e.item) {
        NSStatusBarButton* b = e.item.button;
        if (e.tracking)
            [b removeTrackingArea:e.tracking];
        b.target = nil;
        b.action = nullptr;
        [[NSStatusBar systemStatusBar] removeStatusItem:e.item];
    }
    e.item     = nil;
    e.tracking = nil;
    e.target   = nil;
}

} // namespace

// ---------------------------------------------------------------- livstiden

void trayInit()
{
    g_live = true;
}

void trayShutdown()
{
    @autoreleasepool {
        for (auto& kv : g_entries)
            dropEntry(kv.second);
    }
    g_entries.clear();
    g_live = false;
}

// ---------------------------------------------------------------- fasaden

bool trayAvailable()
{
    return true;   // menyraden finns på varje Mac — före init lika väl som efter
}

bool setTrayIcon(TrayId trayId, const TrayIconDesc& d)
{
    if (!g_live)
        return false;   // före init eller efter shutdown: ingenting (gfx/tray.h)
    if (trayId == 0 || trayId > kTrayIdMax) {
        log("setTrayIcon(%u): id:t ligger utanför 1–%u — vägras",
            static_cast<unsigned>(trayId), static_cast<unsigned>(kTrayIdMax));
        return false;
    }
    @autoreleasepool {
        NSImage* bild = trayImage(d);
        if (!bild) {
            // Kontraktet: vägrat = INTE registrerat — en befintlig post med
            // samma id tas bort, annars hade den gamla bilden stått kvar
            // under ett id fasaden just sagt nej till.
            log("setTrayIcon(%u): ingen bild (ingen giltig ram och ingen "
                "paketikon) — vägras",
                static_cast<unsigned>(trayId));
            removeTrayIcon(trayId);
            return false;
        }

        Entry& e = g_entries[trayId];
        if (!e.item) {
            e.item = [[NSStatusBar systemStatusBar]
                statusItemWithLength:NSSquareStatusItemLength];
            NSStatusBarButton* b = e.item.button;
            e.target             = [GlazeTrayTarget new];
            e.target->trayId_    = trayId;
            b.target             = e.target;
            b.action             = @selector(clicked:);
            b.imagePosition      = NSImageOnly;
            [b sendActionOn:(NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp)];
            e.tracking = [[NSTrackingArea alloc]
                initWithRect:NSZeroRect
                     options:(NSTrackingMouseEnteredAndExited |
                              NSTrackingActiveAlways | NSTrackingInVisibleRect)
                       owner:e.target
                    userInfo:nil];
            [b addTrackingArea:e.tracking];
            b.toolTip = toolTipText(d.toolTip);
            e.toolTip = d.toolTip;
        } else if (e.toolTip != d.toolTip) {
            // Bara vid ändring — animationen sätter bilden flera gånger i
            // sekunden, och en ny tooltip-sträng startar om visningen.
            e.item.button.toolTip = toolTipText(d.toolTip);
            e.toolTip             = d.toolTip;
        }
        e.item.button.image = bild;
    }
    return true;
}

void removeTrayIcon(TrayId trayId)
{
    const auto it = g_entries.find(trayId);
    if (it == g_entries.end())
        return;   // idempotent
    @autoreleasepool {
        dropEntry(it->second);
    }
    g_entries.erase(it);
}

// Knappens fönster ÄR posten i menyraden; arbetsytan är skärmens
// `visibleFrame` (utan menyrad och docka). Båda vända mot huvudskärmens höjd
// till placeringsrymden, som `MetalWindow::setPosition`/`screenArea`. En post
// som inte har något fönster (ännu inte utlagd, eller undanträngd) är okänd.
bool trayIconRect(TrayId trayId, Rectf& icon, Rectf& work)
{
    const auto it = g_entries.find(trayId);
    if (it == g_entries.end() || !it->second.item)
        return false;
    @autoreleasepool {
        NSWindow* const w     = it->second.item.button.window;
        NSScreen* const huvud = NSScreen.screens.firstObject;
        if (!w || !huvud)
            return false;
        NSScreen* const skärm = w.screen ?: huvud;
        icon = toPlacement(w.frame);
        work = toPlacement(skärm.visibleFrame);
    }
    return true;
}

void setTrayTimer(TrayId trayId, std::uint32_t ms)
{
    const auto it = g_entries.find(trayId);
    if (it == g_entries.end())
        return;   // oregistrerat id — no-op (kontraktet)
    Entry& e = it->second;
    @autoreleasepool {
        [e.timer invalidate];   // ett nytt anrop startar om perioden
        e.timer = nil;
        if (ms == 0)
            return;
        const TrayId tid = trayId;
        e.timer = [NSTimer timerWithTimeInterval:ms / 1000.0
                                         repeats:YES
                                           block:^(NSTimer*) {
                                               pushTray(tid, TrayAction::Tick,
                                                        0.0f, 0.0f);
                                           }];
        [[NSRunLoop mainRunLoop] addTimer:e.timer
                                  forMode:NSRunLoopCommonModes];
    }
}

// ---------------------------------------------------------------- dockan

// Före init finns ingen `NSApp` — begäran lagras och `gfx::init` läser den.
// Efter init byts policyn direkt: Regular = dockikon och menyrad, Accessory =
// ingen av dem men fönster och statusposter. Ett byte i drift kan lägga
// fönstren bakåt (gfx/platform.h).
void applyShowInDock(bool show)
{
    if (!NSApp)
        return;
    @autoreleasepool {
        const NSApplicationActivationPolicy p =
            show ? NSApplicationActivationPolicyRegular
                 : NSApplicationActivationPolicyAccessory;
        if ([NSApp activationPolicy] != p && ![NSApp setActivationPolicy:p])
            log("setShowInDock(%s): AppKit vägrade byta aktiveringspolicy",
                show ? "true" : "false");
    }
}

} // namespace gfx
