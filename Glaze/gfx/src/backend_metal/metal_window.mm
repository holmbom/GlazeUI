// MetalWindow — gfx::Window ovanpå AppKit. NSWindow (NSPanel för
// noActivate/toolWindow) + GlazeView: rå NSView vars backing layer är en
// CAMetalLayer (MetalKit används INTE — MTKView äger sin egen draw-loop,
// oförenligt med hård regel 1). Event-översättningen bor i vyn och
// fönster-delegaten — spegeln av d3d11_window.cpp:s WndProc-tabell:
// oöversatt NSEvent = inget pushat = ingen omritning. Live-resize: macOS
// kör drag/resize i en modal runloop-fas där pumpen står still —
// windowDidResize ritar direkt (motsvarigheten till WM_SIZE-ritningen).

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../display_util.h"
#include "../icon_util.h"
#include "metal_backend.h"
#include "gfx/platform.h"

// Carbon/HIToolbox — BARA för tangentbordslayouten (TISCopyCurrent…,
// UCKeyTranslate, LMGetKbdType), som level-0-kontraktet i gfx/events.h
// kräver. Importen står HÄR och inte i metal_backend.h med
// flit: Carbon drar in gamla namn (`Point`, `Rect`, `Boolean`) i det
// globala namnrummet, och ju färre TU:er som ser dem desto bättre. Spricker
// bygget här är det namnkollisioner det handlar om.
#import <Carbon/Carbon.h>

namespace gfx {

// Logiska mått (multipliceras med uiScale vid användning) — samma värden
// som D3D11-backenden så chromet ser identiskt ut.
// Captionens höjd ägs av widget-trädet (captionH_ trycks ner av glaze);
// inga hårdkodade chrome-mått här.

// ---------------------------------------------------------------- push-hjälpare

namespace {

// ZOOMAT? AppKits `isZoomed` svarar YES för VARJE fönster utan
// `NSWindowStyleMaskResizable` — ramlöst (`Chrome::None`) som fast (min ==
// max) — och `zoom:` gör ingenting där. Utan
// vakten är ett sådant fönster "maximerat" på (0, 0) i `placement()`, och
// `onDidResize` ser aldrig återställningsflanken.
bool zoomed(NSWindow* w)
{
    return w && (w.styleMask & NSWindowStyleMaskResizable) && w.isZoomed;
}

// Alla push-hjälpare stämplar avsändande fönster i Event::windowId
// (ordagrann spegel av d3d11_window.cpp).

void pushSimple(WindowId wid, EventType t)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    pushEvent(e);
}

// `mods` = `translateMods(ev.modifierFlags)` — modifierarna NÄR eventet
// hände (Ctrl-/Shift-klick i en lista med flerval).
void pushMouse(WindowId wid, EventType t, float x, float y,
               MouseButton b, std::uint32_t mods)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    e.mouseX   = x;
    e.mouseY   = y;
    e.button   = b;
    e.mods     = mods;
    pushEvent(e);
}

// `precise`/`phase` — styrplattans tvåfingersdrag (kontraktet vid
// `kPrecisePxPerRow` i gfx/events.h); ett mushjul har `false`/None. `mods` =
// `translateMods(ev.modifierFlags)` som för musen — Cmd/Ctrl + styrplattans
// drag kretsar på ritytan.
void pushWheel(WindowId wid, float x, float y, float dx, float dy,
               bool precise, GesturePhase phase, std::uint32_t mods)
{
    Event e;
    e.type     = EventType::MouseWheel;
    e.windowId = wid;
    e.mouseX   = x;
    e.mouseY   = y;
    e.wheelX   = dx;
    e.wheelY   = dy;
    e.precise  = precise;
    e.phase    = phase;
    e.mods     = mods;
    pushEvent(e);
}

// Nypning och vridning — EN sekvens av två AppKit-källor. `mods`
// som hjulets.
void pushGesture(WindowId wid, float x, float y, GesturePhase phase,
                 float scale, float rotation, std::uint32_t mods)
{
    Event e;
    e.type            = EventType::Gesture;
    e.windowId        = wid;
    e.mouseX          = x;
    e.mouseY          = y;
    e.phase           = phase;
    e.gestureScale    = scale;
    e.gestureRotation = rotation;
    e.mods            = mods;
    pushEvent(e);
}

void pushText(WindowId wid, const char* utf8)
{
    Event e;
    e.type     = EventType::TextInput;
    e.windowId = wid;
    // text[32] är nollad — kopiera max 31 byte och trunkera i så fall på
    // en UTF-8-teckengräns (fortsättningsbyte har formen 10xxxxxx).
    std::size_t n = std::strlen(utf8);
    if (n >= sizeof(e.text)) {
        n = sizeof(e.text) - 1;
        while (n > 0 && (static_cast<unsigned char>(utf8[n]) & 0xC0) == 0x80)
            --n;
    }
    std::memcpy(e.text, utf8, n);
    pushEvent(e);
}

// Den OSKIFTADE symbolen för en keyCode, direkt ur den aktiva
// tangentbordslayouten. `charactersIgnoringModifiers`
// ignorerar allt UTOM Shift, så den duger inte när Shift hålls nere —
// och kontraktet vid `enum class Key` säger LEVEL-0-symbolen oavsett
// modifierare. `UCKeyTranslate` med modifierState 0 ger exakt den, utan
// att röra dead-key-state (vi skickar en egen `deadKeyState`).
unichar unshiftedChar(unsigned short keyCode)
{
    TISInputSourceRef src = TISCopyCurrentKeyboardLayoutInputSource();
    if (!src)
        return 0;
    CFDataRef data = static_cast<CFDataRef>(
        TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData));
    unichar out = 0;
    if (data) {
        const UCKeyboardLayout* layout =
            reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
        UInt32     dead = 0;
        UniChar    buf[4] = {};
        UniCharCount len = 0;
        if (UCKeyTranslate(layout, keyCode, kUCKeyActionDown,
                           /*modifierKeyState*/ 0, LMGetKbdType(),
                           kUCKeyTranslateNoDeadKeysBit, &dead,
                           sizeof buf / sizeof buf[0], &len, buf) == noErr &&
            len > 0)
            out = buf[0];
    }
    CFRelease(src);
    return out;
}

// Specialtangenter och NUMPADEN ur keyCode (layoutoberoende positioner —
// pilar m.fl. bär funktionstecken i `characters`, inte användbara här);
// symboler och bokstäver ur LAYOUTEN enligt level-0-kontraktet.
Key translateKey(NSEvent* ev)
{
    switch (ev.keyCode) {
    case 36:  return Key::Return;      // kVK_Return
    case 53:  return Key::Escape;      // kVK_Escape
    case 48:  return Key::Tab;         // kVK_Tab
    case 51:  return Key::Backspace;   // kVK_Delete (bakåt)
    case 49:  return Key::Space;       // kVK_Space
    case 117: return Key::Delete;      // kVK_ForwardDelete
    case 123: return Key::Left;
    case 124: return Key::Right;
    case 126: return Key::Up;
    case 125: return Key::Down;
    case 115: return Key::Home;
    case 119: return Key::End;
    // Navigationsklustrets resterande dedikerade tangenter.
    // `kVK_Help` (0x72) är INSERT-platsen: ett PC-tangentbord på
    // en Mac skickar den för Insert, och GLFW mappar den likadant.
    case 116: return Key::PageUp;      // kVK_PageUp
    case 121: return Key::PageDown;    // kVK_PageDown
    case 114: return Key::Insert;      // kVK_Help
    case 122: return Key::F1;
    case 120: return Key::F2;
    case 99:  return Key::F3;
    case 118: return Key::F4;
    case 96:  return Key::F5;
    case 97:  return Key::F6;
    case 98:  return Key::F7;
    case 100: return Key::F8;
    case 101: return Key::F9;
    case 109: return Key::F10;
    case 103: return Key::F11;
    case 111: return Key::F12;

    // NUMPADEN — ur KEYCODE och inte ur tecknet, för det är
    // enda sättet att skilja numpad-1 från siffer-radens 1: båda
    // producerar "1". Med NumLock av skickar macOS ändå numpadens
    // keyCodes, så posterna gäller alltid.
    //
    // Det är KONTRAKTETS macOS-GREN och inte en avvikelse:
    // `events.h` säger uttryckligen att
    // faktiskt-läge-regeln gäller Windows/Linux och att macOS alltid
    // levererar `KP_*`. Skälet är att macOS inte HAR något NumLock —
    // `NSEvent` bär ingen sådan flagga och OS:et gör aldrig om numpaden
    // till navigationstangenter, så det finns inget läge att följa.
    // Koden här ska alltså INTE "rättas" — och `KP_End`/`KP_Home` m.fl.
    // levereras därför ALDRIG från den här backenden.
    case 82:  return Key::KP_N0;   // kVK_ANSI_Keypad0
    case 83:  return Key::KP_N1;
    case 84:  return Key::KP_N2;
    case 85:  return Key::KP_N3;
    case 86:  return Key::KP_N4;
    case 87:  return Key::KP_N5;
    case 88:  return Key::KP_N6;
    case 89:  return Key::KP_N7;
    case 91:  return Key::KP_N8;
    case 92:  return Key::KP_N9;
    case 69:  return Key::KP_Add;
    case 78:  return Key::KP_Subtract;
    case 67:  return Key::KP_Multiply;
    case 75:  return Key::KP_Divide;
    case 65:  return Key::KP_Decimal;
    case 76:  return Key::Return;   // kVK_ANSI_KeypadEnter — fälls ihop
                                    // med Return (se gfx/events.h)
    default:  break;
    }

    // `charactersIgnoringModifiers` ignorerar allt UTOM Shift och
    // CapsLock, så den är level-0-symbolen så länge Shift inte hålls nere
    // — och när Shift hålls nere säger kontraktet att vi ska svara med
    // den OSKIFTADE symbolen ändå. Därför frågas layouten direkt
    // (`UCKeyTranslate` med noll modifierare) när Shift är nere, och
    // tecknet används rakt av annars.
    unichar c = 0;
    if (ev.modifierFlags & NSEventModifierFlagShift) {
        c = unshiftedChar(ev.keyCode);
    } else {
        NSString* chars = ev.charactersIgnoringModifiers;
        if (chars.length > 0)
            c = [chars characterAtIndex:0];
    }
    if (c == 0)
        return Key::Unknown;

    if (c >= 'a' && c <= 'z')
        return static_cast<Key>(static_cast<int>(Key::A) + (c - 'a'));
    if (c >= 'A' && c <= 'Z')
        return static_cast<Key>(static_cast<int>(Key::A) + (c - 'A'));
    if (c >= '0' && c <= '9')
        return static_cast<Key>(static_cast<int>(Key::N0) + (c - '0'));

    switch (c) {
    case '-':  return Key::Minus;
    case '+':  return Key::Plus;
    case '=':  return Key::Equals;
    case ',':  return Key::Comma;
    case '.':  return Key::Period;
    case '/':  return Key::Slash;
    case '\\': return Key::Backslash;
    case ';':  return Key::Semicolon;
    case '\'': return Key::Apostrophe;
    case '[':  return Key::LeftBracket;
    case ']':  return Key::RightBracket;
    case '`':  return Key::Grave;
    case '%':  return Key::Percent;
    case '*':  return Key::Asterisk;
    default:   return Key::Unknown;
    }
}

std::uint32_t translateMods(NSEventModifierFlags m)
{
    // Command får en EGEN bit (kModCmd) — handlers kan skilja Cmd/Ctrl
    // plattformsoberoende.
    std::uint32_t mods = 0;
    if (m & NSEventModifierFlagControl) mods |= kModCtrl;
    if (m & NSEventModifierFlagShift)   mods |= kModShift;
    if (m & NSEventModifierFlagOption)  mods |= kModAlt;
    if (m & NSEventModifierFlagCommand) mods |= kModCmd;
    return mods;
}

// `text` är texten trycket skriver (KeyDown) —
// trunkerad på en UTF-8-teckengräns som i pushText.
void pushKey(WindowId wid, EventType t, NSEvent* ev, const char* text = nullptr)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    e.key      = translateKey(ev);
    e.mods     = translateMods(ev.modifierFlags);
    if (text) {
        std::size_t n = std::strlen(text);
        if (n >= sizeof(e.text)) {
            n = sizeof(e.text) - 1;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
                --n;
        }
        std::memcpy(e.text, text, n);
    }
    pushEvent(e);
}

// MODIFIERARTANGENTERNA. AppKit skickar ingen
// `keyDown:` för dem, bara `flagsChanged:` med det NYA läget — utan den här
// vägen gav en ensam Cmd inget event alls, och `AppWindow::modifierKeys()`
// såg den inte förrän nästa mushändelse. Tangenten blir ett KeyDown eller
// KeyUp med `Key::Unknown` (som en modifierare på Windows och Linux) och
// masken läget EFTER ändringen — kontraktet vid `kModCtrl` i gfx/events.h.
// Tryck eller släpp avgörs av tangentens egen flagga i det nya läget;
// vänster och höger delar flagga, så släppet av den ena medan den andra
// hålls blir ett KeyDown — masken stämmer ändå. Caps Lock och Fn bär ingen
// bit i fasadens mask och ger inget event.
void pushModifierKey(WindowId wid, NSEvent* ev)
{
    NSEventModifierFlags flagga = 0;
    switch (ev.keyCode) {
    case 59:   // kVK_Control
    case 62:   // kVK_RightControl
        flagga = NSEventModifierFlagControl;
        break;
    case 56:   // kVK_Shift
    case 60:   // kVK_RightShift
        flagga = NSEventModifierFlagShift;
        break;
    case 58:   // kVK_Option
    case 61:   // kVK_RightOption
        flagga = NSEventModifierFlagOption;
        break;
    case 55:   // kVK_Command
    case 54:   // kVK_RightCommand
        flagga = NSEventModifierFlagCommand;
        break;
    default:
        return;
    }
    Event e;
    e.type     = (ev.modifierFlags & flagga) ? EventType::KeyDown
                                             : EventType::KeyUp;
    e.windowId = wid;
    e.key      = Key::Unknown;
    e.mods     = translateMods(ev.modifierFlags);
    pushEvent(e);
}

// MUSPEKAREN — fasadens form → AppKits SYSTEMPEKARE. Alla
// sex är klassmetoder på NSCursor från macOS 10.6 (operationNotAllowed är
// den yngsta). `resizeLeftRightCursor`/`resizeUpDownCursor` är skiktens
// pilar; de nyare `columnResizeCursor`/`rowResizeCursor` (macOS 15) väljs
// inte, för de finns inte på de system vi annars kör på. Hårkorset och
// flytta: AppKit har ingen publik fyrpil, så `Move` är
// den stängda handen — Apples egen form för att dra innehåll.
//
// `ClosedHand` är samma systempekare, så Mac
// ser oförändrad ut för `Move`. `Hidden` är en GENOMSKINLIG `NSCursor` och
// inte `[NSCursor hide]`, vars räknare läcker om den inte paras. `Wait` är
// DEGRADERAD till pilen: macOS har ingen publik väntepekare (snurrbollen är
// systemets egen och visas av OS:et när appen inte svarar).

// Den genomskinliga pekaren — 16 × 16 nollställda pixlar, skapad en gång.
NSCursor* hiddenCursor()
{
    static NSCursor* osynlig = nil;
    if (!osynlig) {
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:16
                          pixelsHigh:16
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:16 * 4
                        bitsPerPixel:32];
        NSImage* bild = [[NSImage alloc] initWithSize:NSMakeSize(16, 16)];
        if (rep && rep.bitmapData) {
            std::memset(rep.bitmapData, 0, 16 * 16 * 4);
            [bild addRepresentation:rep];
        }
        osynlig = [[NSCursor alloc] initWithImage:bild
                                          hotSpot:NSMakePoint(0, 0)];
    }
    return osynlig;
}

NSCursor* nsCursorFor(PointerShape s)
{
    switch (s) {
    case PointerShape::Arrow:      return [NSCursor arrowCursor];
    case PointerShape::IBeam:      return [NSCursor IBeamCursor];
    case PointerShape::Hand:       return [NSCursor pointingHandCursor];
    case PointerShape::ResizeWE:   return [NSCursor resizeLeftRightCursor];
    case PointerShape::ResizeNS:   return [NSCursor resizeUpDownCursor];
    case PointerShape::NotAllowed: return [NSCursor operationNotAllowedCursor];
    case PointerShape::Crosshair:  return [NSCursor crosshairCursor];
    case PointerShape::Move:       return [NSCursor closedHandCursor];
    case PointerShape::ClosedHand: return [NSCursor closedHandCursor];
    case PointerShape::Hidden:     return hiddenCursor();
    case PointerShape::Wait:       return [NSCursor arrowCursor];   // degraderad
    }
    return [NSCursor arrowCursor];
}

} // namespace
} // namespace gfx

// ---------------------------------------------------------------- NSWindow-subklasser

// Borderless (Chrome::None) kan annars inte bli key — popups med TextInput
// kräver det. noActivate-fönster ska däremot ALDRIG ta fokus (WS_EX_
// NOACTIVATE-semantiken) — flaggan styr.
@interface GlazeNSWindow : NSWindow
@property(nonatomic) BOOL glazeCanBecomeKey;
@end

@implementation GlazeNSWindow
- (BOOL)canBecomeKeyWindow
{
    return self.glazeCanBecomeKey;
}
@end

// NSPanel-varianten: nonactivating/utility kräver NSPanel (orderFront utan
// att aktivera appen).
@interface GlazePanel : NSPanel
@property(nonatomic) BOOL glazeCanBecomeKey;
@end

@implementation GlazePanel
- (BOOL)canBecomeKeyWindow
{
    return self.glazeCanBecomeKey;
}
@end

// ---------------------------------------------------------------- GlazeView

// Vyn ÄR event-översättningen (mus/tangent/text) + Metal-ytan. isFlipped
// ger top-left-origo — hela backenden slipper Y-flippar i mus-/rit-
// koordinater. AppKit levererar mouseDragged till samma view även utanför
// fönstret under knapptryck = SetCapture-semantiken gratis.
@interface GlazeView : NSView <NSTextInputClient> {
  @public
    gfx::MetalWindow* owner_;   // rå C++-pekare — nollas i shutdown
    NSTrackingArea*   trackingArea_;
    // Står pekaren i vyn? (mouseEntered:/mouseExited:) — MetalWindow::
    // setCursor sätter pekaren direkt bara då, eller när en knapp hålls.
    BOOL              mouseInside_;
    // STYRPLATTAN. `scrollBegan_`: ett tvåfingersdrag har fått sitt
    // Began, så dess Ended/Cancelled hör till en sekvens (fingrar som vilar
    // och lyfts utan att rulla ger MayBegin + Cancelled, och det är ingen).
    // `magnifyActive_`/`rotateActive_`: vilka av AppKits två gestkällor som
    // pågår — sekvensen börjar när den första startar och slutar när den
    // sista slutat.
    BOOL              scrollBegan_;
    BOOL              magnifyActive_;
    BOOL              rotateActive_;
    // TANGENTENS TEXT: medan `keyDown:` kör
    // `interpretKeyEvents` samlar `insertText:` tecknen här i stället för
    // att köa dem, så KeyDown kan bära texten och köas FÖRE dess TextInput.
    BOOL                     capturing_;
    std::vector<std::string> captured_;
}
- (instancetype)initWithFrame:(NSRect)frame owner:(gfx::MetalWindow*)owner;
- (void)clearOwner;
@end

@implementation GlazeView

- (instancetype)initWithFrame:(NSRect)frame owner:(gfx::MetalWindow*)owner
{
    if ((self = [super initWithFrame:frame])) {
        owner_ = owner;
        self.wantsLayer = YES;   // triggar makeBackingLayer
    }
    return self;
}

- (void)clearOwner
{
    owner_ = nullptr;
}

// Layern ÄR en CAMetalLayer (ingen mellanliggande CALayer + sublager).
- (CALayer*)makeBackingLayer
{
    CAMetalLayer* layer = [CAMetalLayer layer];
    // GPU-preferensens enhet — renderaren tar lagrets.
    layer.device      = gfx::preferredMetalDevice();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;   // spegel av B8G8R8A8_UNORM
    layer.displaySyncEnabled = YES;   // vsync per präsenterad frame — som
                                      // Present(1, 0), INTE per sekund
    return layer;
}

- (BOOL)isFlipped
{
    return YES;   // top-left-origo — fasadens koordinatkonvention direkt
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (trackingArea_)
        [self removeTrackingArea:trackingArea_];
    // NSTrackingCursorUpdate: AppKit skickar `cursorUpdate:` när
    // pekaren går in i ytan och sätter annars tillbaka pilen — utan den
    // försvann den begärda formen vid varje inträde.
    //
    // ETT FÖNSTER SOM ALDRIG BLIR KEY:
    // menyer, dropdown-listor och datum- och färgpopuper är `noActivate`, så
    // `ActiveInKeyWindow` gav dem aldrig något `mouseMoved:` och raden under
    // pekaren markerades inte. De får `ActiveAlways` — och då INTE
    // `CursorUpdate`, som Apple inte stöder ihop med ActiveAlways
    // (NSTrackingArea.h). Popuperna visar pilen ändå: `setCursor` sätter
    // formen direkt medan pekaren står i vyn (`mouseInside_`). Andra fönster
    // behåller sina flaggor.
    const NSTrackingAreaOptions flaggor =
        (owner_ && owner_->noActivate())
            ? (NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
               NSTrackingActiveAlways | NSTrackingInVisibleRect)
            : (NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
               NSTrackingCursorUpdate | NSTrackingActiveInKeyWindow |
               NSTrackingInVisibleRect);
    trackingArea_ = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                 options:flaggor
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:trackingArea_];
}

// MUSPEKAREN: den form fönstret begärt — AppWindow sätter
// den vid varje rörelse, och det här är vägen tillbaka när AppKit själv har
// rört pekaren (inträde i ytan, fönstret blev key).
- (void)cursorUpdate:(NSEvent*)ev
{
    (void)ev;
    if (!owner_) return;
    [gfx::nsCursorFor(owner_->cursor()) set];
}

- (void)mouseEntered:(NSEvent*)ev
{
    (void)ev;
    mouseInside_ = YES;
}

// Muspos i FYSISKA px (fasadens kontrakt): view-koordinater (flippade =
// top-left) × backingScaleFactor.
- (NSPoint)physicalPoint:(NSEvent*)ev
{
    const NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    const CGFloat s = self.window ? self.window.backingScaleFactor : 1.0;
    return NSMakePoint(p.x * s, p.y * s);
}

// ---- mus ----

- (void)mouseDown:(NSEvent*)ev
{
    if (!owner_) return;
    const NSPoint p = [self physicalPoint:ev];
    // Custom-chromets caption (titelraden minus knapparna): native drag
    // via performWindowDragWithEvent — flytt inkl. skärmkant-beteenden
    // (HTCAPTION-motsvarigheten); dubbelklick = zoom (OS-konventionen).
    // Inget MouseButtonDown pushas då — draget äger gesten.
    if (owner_->captionHitTest((float)p.x, (float)p.y)) {
        if (ev.clickCount == 2) {
            [self.window zoom:nil];
            return;
        }
        [self.window performWindowDragWithEvent:ev];
        return;
    }
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseButtonDown,
                   (float)p.x, (float)p.y, gfx::MouseButton::Left,
                   gfx::translateMods(ev.modifierFlags));
}

- (void)rightMouseDown:(NSEvent*)ev
{
    if (!owner_) return;
    const NSPoint p = [self physicalPoint:ev];
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseButtonDown,
                   (float)p.x, (float)p.y, gfx::MouseButton::Right,
                   gfx::translateMods(ev.modifierFlags));
}

// "Other" är VARJE knapp utom vänster och höger — också sidoknapparna
// (bakåt/framåt, buttonNumber 3 och 4). Bara 2 är mittenknappen, och den betyder
// något (ritytan panoreras), så en
// sidoknapp får inte låtsas vara den.
- (void)otherMouseDown:(NSEvent*)ev
{
    if (!owner_ || ev.buttonNumber != 2) return;
    const NSPoint p = [self physicalPoint:ev];
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseButtonDown,
                   (float)p.x, (float)p.y, gfx::MouseButton::Middle,
                   gfx::translateMods(ev.modifierFlags));
}

- (void)mouseUp:(NSEvent*)ev
{
    if (!owner_) return;
    const NSPoint p = [self physicalPoint:ev];
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseButtonUp,
                   (float)p.x, (float)p.y, gfx::MouseButton::Left,
                   gfx::translateMods(ev.modifierFlags));
}

- (void)rightMouseUp:(NSEvent*)ev
{
    if (!owner_) return;
    const NSPoint p = [self physicalPoint:ev];
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseButtonUp,
                   (float)p.x, (float)p.y, gfx::MouseButton::Right,
                   gfx::translateMods(ev.modifierFlags));
}

- (void)otherMouseUp:(NSEvent*)ev
{
    if (!owner_ || ev.buttonNumber != 2) return;   // se otherMouseDown
    const NSPoint p = [self physicalPoint:ev];
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseButtonUp,
                   (float)p.x, (float)p.y, gfx::MouseButton::Middle,
                   gfx::translateMods(ev.modifierFlags));
}

// mouseDragged är OBLIGATORISK — under knapptryck skickas ALDRIG mouseMoved.
- (void)mouseMoved:(NSEvent*)ev
{
    if (!owner_) return;
    const NSPoint p = [self physicalPoint:ev];
    // Transparent: genomskinlig pixel under musen ⇒ släpp musen igenom
    // (ignoresMouseEvents) — den globala monitorn slår tillbaka
    owner_->updateClickThrough((float)p.x, (float)p.y);
    gfx::pushMouse(owner_->id(), gfx::EventType::MouseMotion,
                   (float)p.x, (float)p.y, gfx::MouseButton::None,
                   gfx::translateMods(ev.modifierFlags));
}

- (void)mouseDragged:(NSEvent*)ev       { [self mouseMoved:ev]; }
- (void)rightMouseDragged:(NSEvent*)ev  { [self mouseMoved:ev]; }
- (void)otherMouseDragged:(NSEvent*)ev  { [self mouseMoved:ev]; }

- (void)mouseExited:(NSEvent*)ev
{
    mouseInside_ = NO;
    if (!owner_) return;
    gfx::pushSimple(owner_->id(), gfx::EventType::MouseLeave);
}

// HJULET. Naturlig rullning är redan tillämpad av AppKit. TECKNEN:
// lodrätt positivt = uppåt som D3D11, men vågrätt är AppKits positiva
// `scrollingDeltaX` åt VÄNSTER (Chromium vänder Win32:s WM_MOUSEHWHEEL för
// att matcha just det) och kontraktets positiva `wheelX` åt höger — därför
// minus.
//
// STYRPLATTAN (kontraktet vid `kPrecisePxPerRow` i gfx/events.h):
// `hasPreciseScrollingDeltas` är tvåfingersdraget (och Magic Mouse) i
// PUNKTER — fönstrets logiska px är punkter × backing ÷ fulla skalan (som
// också bär användarskalan), och raderna de ÷ kPrecisePxPerRow. Fasen ur
// `phase`, och `momentumPhase` är OS:ets egen tröghet efter att fingrarna
// lyfts: Momentum. Ett klassiskt hjul levererar redan rader och har ingen
// fas.
- (void)scrollWheel:(NSEvent*)ev
{
    if (!owner_) return;
    const NSPoint       p    = [self physicalPoint:ev];
    const std::uint32_t mods = gfx::translateMods(ev.modifierFlags);
    if (!ev.hasPreciseScrollingDeltas) {
        // ENHETENS AXEL: AppKit byter själv ett
        // mushjul med Shift till sidled (deltaY 0, deltaX med hjulets tecken).
        // Fasaden ska bära hjulets egen axel på alla tre — glaze byter för det
        // som rullar, och ritytans bindningar ser `WheelY` med Shift — så
        // bytet görs om här. Gränsen: ett äkta sidohjul som rullas med Shift
        // nedtryckt blir lodrätt.
        if ((mods & gfx::kModShift) && ev.scrollingDeltaY == 0.0 &&
            ev.scrollingDeltaX != 0.0) {
            gfx::pushWheel(owner_->id(), (float)p.x, (float)p.y, 0.0f,
                           (float)ev.scrollingDeltaX, false,
                           gfx::GesturePhase::None, mods);
            return;
        }
        gfx::pushWheel(owner_->id(), (float)p.x, (float)p.y,
                       -(float)ev.scrollingDeltaX, (float)ev.scrollingDeltaY,
                       false, gfx::GesturePhase::None, mods);
        return;
    }
    gfx::GesturePhase phase = gfx::GesturePhase::None;
    if (ev.momentumPhase != NSEventPhaseNone) {
        phase = gfx::GesturePhase::Momentum;
    } else if (ev.phase & NSEventPhaseBegan) {
        phase        = gfx::GesturePhase::Begin;
        scrollBegan_ = YES;
    } else if (ev.phase & (NSEventPhaseChanged | NSEventPhaseStationary)) {
        phase = gfx::GesturePhase::Update;
    } else if (ev.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
        if (!scrollBegan_)
            return;   // MayBegin + Cancelled: fingrarna vilade, ingen rullning
        scrollBegan_ = NO;
        phase = (ev.phase & NSEventPhaseCancelled) ? gfx::GesturePhase::Cancel
                                                   : gfx::GesturePhase::End;
    } else if (ev.phase & NSEventPhaseMayBegin) {
        return;       // fingrarna ligger på plattan — inget har rullat än
    }
    const CGFloat backing = self.window ? self.window.backingScaleFactor : 1.0;
    const float   perRow  = owner_->uiScale() * gfx::kPrecisePxPerRow;
    gfx::pushWheel(owner_->id(), (float)p.x, (float)p.y,
                   -(float)(ev.scrollingDeltaX * backing) / perRow,
                   (float)(ev.scrollingDeltaY * backing) / perRow, true, phase,
                   mods);
}

// NYPNING OCH VRIDNING — två AppKit-källor, EN sekvens i
// kontraktet: Begin när den första startar, End när den sista slutat (Cancel
// om den sista avbröts). `magnification` är additiv (faktor 1 +
// magnification); `rotation` är grader MOTURS och kontraktet vill medurs —
// minus. Ankaret är pekaren, som står still under gesten. En gest utan fas
// (en äldre enhet) blir en hel sekvens i ett event.
- (void)magnifyWithEvent:(NSEvent*)ev
{
    if (!owner_) return;
    [self gestureEvent:ev
             magnify:YES
               scale:(float)(1.0 + ev.magnification)
            rotation:0.0f];
}

- (void)rotateWithEvent:(NSEvent*)ev
{
    if (!owner_) return;
    [self gestureEvent:ev magnify:NO scale:1.0f rotation:-(float)ev.rotation];
}

- (void)gestureEvent:(NSEvent*)ev
             magnify:(BOOL)magnify
               scale:(float)scale
            rotation:(float)rotation
{
    const NSPoint       p   = [self physicalPoint:ev];
    const gfx::WindowId wid = owner_->id();
    const float         x = (float)p.x, y = (float)p.y;
    // Modifierarna NÄR gesten hände, som hjulets.
    const std::uint32_t mods = gfx::translateMods(ev.modifierFlags);
    BOOL* const         own = magnify ? &magnifyActive_ : &rotateActive_;
    const bool          change = scale != 1.0f || rotation != 0.0f;

    if (ev.phase & NSEventPhaseBegan) {
        if (!magnifyActive_ && !rotateActive_)
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::Begin, 1.0f, 0.0f,
                             mods);
        *own = YES;
        if (change)
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::Update, scale,
                             rotation, mods);
    } else if (ev.phase & (NSEventPhaseChanged | NSEventPhaseStationary)) {
        if (!*own) {   // ett missat Began: sekvensen börjar här
            if (!magnifyActive_ && !rotateActive_)
                gfx::pushGesture(wid, x, y, gfx::GesturePhase::Begin, 1.0f,
                                 0.0f, mods);
            *own = YES;
        }
        gfx::pushGesture(wid, x, y, gfx::GesturePhase::Update, scale,
                         rotation, mods);
    } else if (ev.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
        if (!*own)
            return;
        if (change)
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::Update, scale,
                             rotation, mods);
        *own = NO;
        if (!magnifyActive_ && !rotateActive_)
            gfx::pushGesture(wid, x, y,
                             (ev.phase & NSEventPhaseCancelled)
                                 ? gfx::GesturePhase::Cancel
                                 : gfx::GesturePhase::End,
                             1.0f, 0.0f, mods);
    } else if (ev.phase == NSEventPhaseNone && change) {
        if (magnifyActive_ || rotateActive_) {
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::Update, scale,
                             rotation, mods);
        } else {
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::Begin, 1.0f, 0.0f,
                             mods);
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::Update, scale,
                             rotation, mods);
            gfx::pushGesture(wid, x, y, gfx::GesturePhase::End, 1.0f, 0.0f,
                             mods);
        }
    }
}

// ---- tangentbord ----

- (void)keyDown:(NSEvent*)ev
{
    if (!owner_) return;
    // TextInput via interpretKeyEvents → insertText: (döda tangenter
    // ¨+a → ä komponeras av AppKit), som ropas SYNKRONT: tecknen fångas
    // under anropet, KeyDown köas med den sammanslagna texten och därefter ett
    // TextInput per tecken — de andra backendarnas ordning.
    // Cmd- och Ctrl-kombinationer är kommandon, inte text —
    // samma anda som WM_CHAR-filtrets kontrolltecken; Ctrl-bindningarna
    // (moveToBeginningOfLine: …) hade ändå gått till vår tomma
    // doCommandBySelector:. Option är en skrivtangent och skriver.
    captured_.clear();
    if (!(ev.modifierFlags &
          (NSEventModifierFlagCommand | NSEventModifierFlagControl))) {
        capturing_ = YES;
        [self interpretKeyEvents:@[ ev ]];
        capturing_ = NO;
    }
    if (!owner_) {
        captured_.clear();
        return;
    }
    std::string text;
    for (const std::string& del : captured_)
        text += del;
    gfx::pushKey(owner_->id(), gfx::EventType::KeyDown, ev, text.c_str());
    for (const std::string& del : captured_)
        gfx::pushText(owner_->id(), del.c_str());
    captured_.clear();
}

- (void)keyUp:(NSEvent*)ev
{
    if (!owner_) return;
    gfx::pushKey(owner_->id(), gfx::EventType::KeyUp, ev);
}

// Modifierartangenterna — de ger aldrig
// keyDown:/keyUp:, bara den här (se `pushModifierKey`).
- (void)flagsChanged:(NSEvent*)ev
{
    if (!owner_) return;
    gfx::pushModifierKey(owner_->id(), ev);
}

// ---- NSTextInputClient (minimal — markerad text no-op:as) ----

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    if (!owner_) return;
    NSString* s = [string isKindOfClass:[NSAttributedString class]]
                      ? [string string]
                      : string;
    const char* utf8 = s.UTF8String;
    if (!utf8 || !utf8[0])
        return;
    // Kontrolltecken är kommandon (KeyDown-vägen), inte text — spegel av
    // WM_CHAR-filtret.
    const unsigned char first = (unsigned char)utf8[0];
    if (first < 0x20 || first == 0x7F)
        return;
    // Under keyDown: följer tecknet sin KeyDown (se där); annars (teckenpaletten
    // m.m.) köas det direkt.
    if (capturing_) {
        captured_.emplace_back(utf8);
        return;
    }
    gfx::pushText(owner_->id(), utf8);
}

// Tom — annars NSBeep:ar NSResponder på pilar/Return/Escape (de är redan
// levererade som KeyDown).
- (void)doCommandBySelector:(SEL)selector
{
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange
{
}

- (void)unmarkText
{
}

- (NSRange)selectedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)markedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
    return NO;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:
                                                   (NSRangePointer)actualRange
{
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange
{
    // Caret-trakten — IME-paletten hamnar vid fönstret (förfinas vid behov)
    return self.window ? self.window.frame : NSZeroRect;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    return 0;
}

@end

// ---------------------------------------------------------------- delegaten

// Fönster-delegaten → de nio invalidate-typerna (tappas en är symtomet
// "UI:t uppdateras inte förrän jag rör musen" — komplett som WndProc:en).
@interface GlazeWindowDelegate : NSObject <NSWindowDelegate> {
  @public
    gfx::MetalWindow* owner_;
}
- (instancetype)initWithOwner:(gfx::MetalWindow*)owner;
@end

@implementation GlazeWindowDelegate

- (instancetype)initWithOwner:(gfx::MetalWindow*)owner
{
    if ((self = [super init]))
        owner_ = owner;
    return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    // Per-fönster-stängning (kryss/Cmd+W) — appen bestämmer, ingen close
    // här (spegel av WM_CLOSE → return 0). Quit är app-nivå (id 0).
    if (owner_)
        gfx::pushSimple(owner_->id(), gfx::EventType::WindowCloseRequested);
    return NO;
}

- (void)windowDidResize:(NSNotification*)n
{
    if (owner_)
        owner_->onDidResize();
}

// NORMALRUTAN: en flytt i normalläge flyttar
// rutan `restore()` ger tillbaka. `trackNormalFrame` hoppar själv över zoom
// och miniatyr.
- (void)windowDidMove:(NSNotification*)n
{
    if (owner_)
        owner_->trackNormalFrame();
}

// …och precis FÖRE zoomen, medan ramen ännu är normalrutan — svaret är
// alltid ja, kroken finns bara för spårningens skull.
- (BOOL)windowShouldZoom:(NSWindow*)window toFrame:(NSRect)newFrame
{
    if (owner_)
        owner_->trackNormalFrame();
    return YES;
}

// De vanliga ägda fönstren döljs FÖRE animationen och tas fram när ägaren
// är tillbaka — barnfönstrets beteende, utan dess Space-lås.
// Normalrutan spåras här av samma skäl som vid zoomen.
- (void)windowWillMiniaturize:(NSNotification*)n
{
    if (owner_) {
        owner_->trackNormalFrame();
        owner_->hideOwnedForMinimize();
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)n
{
    if (owner_) {
        owner_->restoreOwnedAfterMinimize();
        gfx::pushSimple(owner_->id(), gfx::EventType::WindowRestored);
    }
}

- (void)windowDidBecomeKey:(NSNotification*)n
{
    if (owner_) {
        owner_->liftOwned();   // ägda över ägaren
        gfx::pushSimple(owner_->id(), gfx::EventType::WindowFocusGained);
    }
}

- (void)windowDidResignKey:(NSNotification*)n
{
    if (owner_)
        gfx::pushSimple(owner_->id(), gfx::EventType::WindowFocusLost);
}

- (void)windowDidChangeBackingProperties:(NSNotification*)n
{
    if (owner_)
        owner_->onBackingChanged();
}

// SKÄRMENS mm: fönstret står nu på en annan skärm.
- (void)windowDidChangeScreen:(NSNotification*)n
{
    if (owner_)
        owner_->onScreenChanged();
}

- (void)windowDidChangeOcclusionState:(NSNotification*)n
{
    NSWindow* w = n.object;
    if (owner_ && (w.occlusionState & NSWindowOcclusionStateVisible))
        owner_->onOcclusionVisible();
}

@end

// ---------------------------------------------------------------- MetalWindow

namespace gfx {

bool MetalWindow::init(const WindowDesc& desc)
{
    chrome_      = desc.chrome;
    title_       = desc.title;
    transparent_ = desc.transparent;
    noActivate_  = desc.noActivate;
    // FÖRSTA VISNINGEN AKTIVERAR INTE — läst av den
    // första `setVisible(true)`.
    showInactive_ = desc.showInactive;
    // STARTLÄGET — tillämpas vid FÖRSTA synligheten
    // i `setVisible(true)`.
    startState_  = desc.startState;
    firstShown_  = false;

    // Transparent ⇒ chrome av (fri geometri och titelrad är oförenliga).
    // Flaggan lagras här och styr composite-vägen.
    if (transparent_ && chrome_ != Chrome::None) {
        log("transparent fönster: chrome tvingas av (None)");
        chrome_ = Chrome::None;
    }

    // AKTIVITETSFÄLTET: macOS har ingen knapp per
    // fönster, dockans ikon är APPENS (`gfx::setShowInDock`). Attributet går
    // inte att uppfylla här och sägs därför en gång per process
    // (degraderingstabellen vid `WindowDesc::showInTaskbar`).
    if (!desc.showInTaskbar) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            log("WindowDesc::showInTaskbar = false: macOS har ingen knapp per "
                "fönster — dockan är appens (gfx::setShowInDock)");
        }
    }

    // Fönster-id för Event::windowId — löpnummer (0 = ogiltigt), samma
    // semantik som D3D11. Enkeltrådad (som event-kön).
    static WindowId nextWindowId = 0;
    windowId_ = ++nextWindowId;

    @autoreleasepool {
        // styleMask: Native = OS:ets trafikljus; None = borderless.
        // Custom = native-masken + FullSizeContentView: innehållet täcker
        // hela ytan men OS:et behåller kant-resize, zoom-animationer och
        // Spaces — exakt samma filosofi som WM_NCCALCSIZE-receptet (ta bort
        // visuellt chrome, behåll OS-beteendet).
        NSWindowStyleMask style;
        if (chrome_ == Chrome::None) {
            style = NSWindowStyleMaskBorderless;
        } else {
            style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                    NSWindowStyleMaskMiniaturizable |
                    NSWindowStyleMaskResizable;
            if (chrome_ == Chrome::Custom)
                style |= NSWindowStyleMaskFullSizeContentView;
        }

        // noActivate/toolWindow kräver NSPanel (nonactivating/utility är
        // panel-masker — orderFront utan att aktivera appen).
        const bool panel = desc.noActivate || desc.toolWindow;
        if (desc.noActivate)
            style |= NSWindowStyleMaskNonactivatingPanel;
        if (desc.toolWindow)
            style |= NSWindowStyleMaskUtilityWindow;

        // desc-måtten är klientyta i LOGISKA px. Metal-specialen:
        // drawable-px = punkter × backingScaleFactor, så PUNKTstorleken
        // bär användarskalan — punkter = logiska px × userScale (fysiska
        // px = logiska × backing × userScale = logiska × uiScale()).
        logicalW_ = desc.width > 0 ? desc.width : 1;
        logicalH_ = desc.height > 0 ? desc.height : 1;
        const NSRect content =
            NSMakeRect(0, 0, logicalW_ * userScale(),
                       logicalH_ * userScale());

        if (panel) {
            GlazePanel* p =
                [[GlazePanel alloc] initWithContentRect:content
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
            p.glazeCanBecomeKey = desc.noActivate ? NO : YES;
            // Utility-paneler ska leva tills VI stänger dem
            p.hidesOnDeactivate = NO;
            nsWindow_ = p;
        } else {
            GlazeNSWindow* w =
                [[GlazeNSWindow alloc] initWithContentRect:content
                                                 styleMask:style
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
            w.glazeCanBecomeKey = YES;   // även borderless (popup-TextInput)
            nsWindow_ = w;
        }
        // ARC äger fönstret — AppKits självfrisläpp vid close stängs av
        nsWindow_.releasedWhenClosed = NO;
        nsWindow_.title = [NSString stringWithUTF8String:title_.c_str()]
                              ?: @"";

        // STARTGRÄNSERNA: desc-fälten är bara startvärdet för
        // `setSizeLimits` — samma lagring, samma väg.
        setSizeLimits(desc.minWidth, desc.minHeight, desc.maxWidth,
                      desc.maxHeight);

        // ALLTID ÖVERST: desc-fältet är startvärdet för
        // `setTopmost` — samma lagring, samma väg. Sätts FÖRE fönstret
        // visas så det aldrig blinkar förbi i fel z-ordning.
        setTopmost(desc.topmost);

        // Custom: göm OS:ets titelrad/trafikljus — vårt chrome ritas av
        // drawChrome() genom egna renderern (D3D11-måtten exakt).
        if (chrome_ == Chrome::Custom) {
            nsWindow_.titlebarAppearsTransparent = YES;
            nsWindow_.titleVisibility = NSWindowTitleHidden;
            [[nsWindow_ standardWindowButton:NSWindowCloseButton]
                setHidden:YES];
            [[nsWindow_ standardWindowButton:NSWindowMiniaturizeButton]
                setHidden:YES];
            [[nsWindow_ standardWindowButton:NSWindowZoomButton]
                setHidden:YES];
        }

        // Transparent: icke-opak yta hela vägen (fönster + Metal-layer);
        // ingen OS-skugga (spök-skugga runt fri geometri). Compositen
        // skriver premultiplicerat utan blandning — se metal_renderer.mm.
        if (transparent_) {
            nsWindow_.opaque          = NO;
            nsWindow_.backgroundColor = NSColor.clearColor;
            nsWindow_.hasShadow       = NO;
        }

        // Position: skärm-px med top-left-konvention → AppKit-punkter med
        // origo nere till vänster (flipp mot HUVUDskärmens höjd — screens[0]
        // bär det globala origot). Sentinel = centrera.
        if (desc.posX != kWindowPosDefault &&
            desc.posY != kWindowPosDefault) {
            const CGFloat screenH =
                NSScreen.screens.firstObject.frame.size.height;
            [nsWindow_ setFrameTopLeftPoint:NSMakePoint(desc.posX,
                                                        screenH - desc.posY)];
        } else {
            [nsWindow_ center];
        }

        // Ägare: Z-ordning/minimering följer ägaren. Ägaren är alltid ett
        // MetalWindow — samma per-konstruktion-säkra cast som D3D11.
        //
        // BARA ANKRADE fönster blir AppKit-BARNFÖNSTER:
        // popup-kontraktets fönster (listor, menyer,
        // tooltips, färg- och datumpopuper — alla med ankare) ska följa sin
        // ägare, även till dess Space. `noActivate` ensamt gör det inte
        // längre: det flytande verktygsfältet är noActivate men har inget
        // ankare, och som barnfönster hade det flyttat med huvudfönstret och
        // låsts till dess Space. Ett VANLIGT ägt fönster (dialog,
        // andrafönster, verktygsfältet) registreras i stället hos ägaren
        // längre ner — se `ownerWin_` i metal_backend.h.
        if (desc.owner && desc.popupAnchorWindow) {
            auto* ownerWin = static_cast<MetalWindow*>(desc.owner);
            if (ownerWin->nsWindow_)
                [ownerWin->nsWindow_ addChildWindow:nsWindow_
                                            ordered:NSWindowAbove];
        }

        // Popup-kontraktet: placera
        // fönstret under ankarrectet (ägarens klient-FYSISKA px) med
        // flip-uppåt/klamp-i-sidled.
        // Skärmrymden här är PUNKTER med top-left-
        // konvention (clientToScreen/screenArea nedan); fönstermåtten är
        // redan punkter (= logiska px), så ingen storleksomvandling
        // behövs. Överstyr posX/center-blocket ovan.
        if (desc.popupAnchorWindow) {
            auto* ow = static_cast<MetalWindow*>(desc.popupAnchorWindow);
            // Fönstermåtten i skärmrymden är PUNKTER = logiska px ×
            // userScale (samma som content-rectet ovan).
            const float  sw = static_cast<float>(desc.width) * userScale();
            const float  sh = static_cast<float>(desc.height) * userScale();
            const Rectf& a  = desc.popupAnchorRect;
            Pointf       pos = ow->clientToScreen(a.x, a.y + a.h);
            const Rectf  scr = ow->screenArea();
            if (scr.w > 0.0f) {
                if (pos.y + sh > scr.y + scr.h)
                    pos.y = ow->clientToScreen(a.x, a.y).y - sh;
                // OBS: inte std::clamp — popup bredare än arbetsytan ger
                // max < min = UB där; max/min-formen låter vänsterkanten
                // vinna i stället.
                pos.x = std::max(scr.x,
                                 std::min(pos.x, scr.x + scr.w - sw));
            }
            const CGFloat screenH =
                NSScreen.screens.firstObject.frame.size.height;
            [nsWindow_ setFrameTopLeftPoint:NSMakePoint(pos.x,
                                                        screenH - pos.y)];
        }

        // NORMALRUTANS STARTVÄRDE: ramen
        // fönstret har nu, placerat men inte visat — så en zoomad start bär
        // den BEGÄRDA rutan fast fönstret aldrig visats normalt.
        trackNormalFrame();

        view_ = [[GlazeView alloc] initWithFrame:content owner:this];
        nsWindow_.contentView = view_;
        [nsWindow_ makeFirstResponder:view_];

        delegate_ = [[GlazeWindowDelegate alloc] initWithOwner:this];
        nsWindow_.delegate = delegate_;

        // Layer-geometrin (contentsScale/drawableSize) före renderer-init
        syncLayerGeometry();
        auto* layer = (CAMetalLayer*)view_.layer;
        if (transparent_)
            layer.opaque = NO;
        const Sizef cs = clientSize();
        if (!gfxRenderer_.init(layer, (int)cs.w, (int)cs.h, transparent_)) {
            nsWindow_.delegate = nil;
            [view_ clearOwner];
            nsWindow_ = nil;
            view_     = nil;
            delegate_ = nil;
            return false;
        }

        // Transparent: global mouseMoved-monitor — när fönstret ignorerar
        // mus (klick-genomsläpp) får det inga lokala events; monitorn är
        // enda vägen tillbaka till ignoresMouseEvents=NO. Kräver inga
        // behörigheter för mouseMoved.
        if (transparent_) {
            MetalWindow* self_ = this;
            globalMouseMonitor_ = [NSEvent
                addGlobalMonitorForEventsMatchingMask:NSEventMaskMouseMoved
                                              handler:^(NSEvent* ev) {
                    // Globala events saknar fönster — locationInWindow är
                    // skärmkoordinater; översätt till vyn (flippad) × skala
                    if (!self_->nsWindow_ || !self_->view_)
                        return;
                    const NSPoint sp = ev.locationInWindow;
                    const NSPoint wp =
                        [self_->nsWindow_ convertPointFromScreen:sp];
                    const NSPoint vp =
                        [self_->view_ convertPoint:wp fromView:nil];
                    const CGFloat s = self_->nsWindow_.backingScaleFactor;
                    self_->updateClickThrough((float)(vp.x * s),
                                              (float)(vp.y * s));
                }];
        }

        registerWindowId(windowId_);

        // VANLIGT ägt fönster: registreras hos ägaren FÖRST
        // NÄR init lyckats — ett misslyckat init lämnar då ingen pekare
        // efter sig. Barnfönster-grenen ovan gäller bara ankrade fönster —
        // ett noActivate-fönster utan ankare hör hit.
        if (desc.owner && !desc.popupAnchorWindow) {
            auto* ownerWin = static_cast<MetalWindow*>(desc.owner);
            if (ownerWin->nsWindow_) {
                ownerWin_ = ownerWin;
                ownerWin->owned_.push_back(this);
            }
        }

        // Dolt fönster (glaze-render): hoppa orderFront helt — ytan och
        // retained-RT:n finns ändå, renderFrame/captureFrame fungerar, men
        // ingen drawable hämtas (`setPresenting`).
        gfxRenderer_.setPresenting(!desc.hidden);
        if (!desc.hidden)
            setVisible(true);   // orderFront + WindowShown/WindowExposed
    }
    measureDisplay(false);   // skärmens mm
    applyIcon();             // programikonen — ramar satta före init
    return true;
}

// ---- PROGRAMIKONEN ---------------------------
//
// Dockans ikon är APPENS och
// inte fönstrets — macOS har ingen ikon per fönster — så tre regler:
//   * PAKETETS IKON VINNER ALLTID. Har bundlen `CFBundleIconFile` (det
//     `glaze_app_icon` lägger in) rör ingen setIcon dockan: den ikonen är
//     appens identitet i dockan OCH i Finder. `iconApplied()` är då sant
//     bara för `programFirst` — förvalet, som just är programfilens ikon.
//   * Utan paketikon ÄGS dockan av det FÖRSTA levande fönster som satt en
//     ikon (`g_iconOwner`), alltså normalt huvudfönstret: ett senare fönsters
//     egen ikon (galleriets kugghjul) gäller bara dess namnlist, som widgeten
//     ritar, och dess `iconApplied()` är false. Ägaren kan byta ikon.
//   * En TOM lista återställer dockan (`setApplicationIconImage:nil`) bara
//     från ägaren — en dialog med `noIcon` tar inte appens ikon.
// Ramarna blir representationer i EN NSImage med samma punktstorlek, så
// AppKit väljer den ram vars pixlar passar dockans storlek.

namespace {
// Fönstret som äger dockans ikon (nullptr = inget levande). Rå pekare,
// nollad i shutdown — UI-trådens, som resten av backenden.
MetalWindow* g_iconOwner = nullptr;
} // namespace

NSBitmapImageRep* iconRep(const IconImage& im, NSSize points)
{
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr
                      pixelsWide:im.width
                      pixelsHigh:im.height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                    bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                     bytesPerRow:im.width * 4
                    bitsPerPixel:32];
    if (!rep || !rep.bitmapData)
        return nil;
    std::memcpy(rep.bitmapData, im.rgba.data(), im.rgba.size());
    rep.size = points;
    return rep;
}

Rectf toPlacement(NSRect r)
{
    NSScreen* const huvud = NSScreen.screens.firstObject;
    const CGFloat   höjd  = huvud ? huvud.frame.size.height : 0.0;
    return Rectf{static_cast<float>(r.origin.x),
                 static_cast<float>(höjd - r.origin.y - r.size.height),
                 static_cast<float>(r.size.width),
                 static_cast<float>(r.size.height)};
}

bool bundleHasIcon()
{
    return [NSBundle.mainBundle
               objectForInfoDictionaryKey:@"CFBundleIconFile"] != nil;
}

void MetalWindow::setIcon(const WindowIcon& icon)
{
    icon_ = icon;
    applyIcon();   // före init: lagrat, init tillämpar
}

void MetalWindow::applyIcon()
{
    if (!nsWindow_)
        return;
    @autoreleasepool {
        if (bundleHasIcon()) {
            iconApplied_ = icon_.programFirst && !icon_.images.empty();
            return;   // paketets ikon står i dockan — rörs aldrig
        }
        if (icon_.images.empty()) {
            if (g_iconOwner == this) {
                [NSApp setApplicationIconImage:nil];
                g_iconOwner = nullptr;
            }
            iconApplied_ = false;
            return;
        }
        if (g_iconOwner && g_iconOwner != this) {
            iconApplied_ = false;   // ett annat fönster äger dockan
            return;
        }
        int sida = 0;
        for (const IconImage& im : icon_.images)
            if (iconImageValid(im))
                sida = std::max(sida, iconImageSide(im));
        if (sida == 0) {
            log("setIcon: ingen av %u ramar är giltig",
                static_cast<unsigned>(icon_.images.size()));
            iconApplied_ = false;
            return;
        }
        const NSSize punkter = NSMakeSize(sida, sida);
        NSImage*     bild    = [[NSImage alloc] initWithSize:punkter];
        for (const IconImage& im : icon_.images) {
            if (!iconImageValid(im))
                continue;
            if (NSBitmapImageRep* rep = iconRep(im, punkter))   // flera täthet
                [bild addRepresentation:rep];
        }
        if (bild.representations.count == 0) {
            iconApplied_ = false;
            return;
        }
        [NSApp setApplicationIconImage:bild];
        g_iconOwner  = this;
        iconApplied_ = true;
    }
}

// ---- ÄGARSKAPET för vanliga ägda fönster ----------------
//
// Ett barnfönster ger Z-ordning och minimering gratis men också två saker
// fasaden INTE lovar: det flyttar med föräldern och är låst till dess
// Space, så ett ägt fönster försvinner vid skärm- och Space-byte. Ägarskap
// bär aldrig koordinater (gfx/window.h), så de två utlovade sakerna görs
// här i stället — exakt det Win32 ger ett hWndParent-ägt fönster.

// Z-ORDNINGEN: AppKit lägger det klickade fönstret överst, så ägaren hamnar
// över sina ägda. De lyfts tillbaka UTAN att bli key (`orderWindow:
// relativeTo:` aktiverar inte), så tangentbordet stannar i ägaren som
// användaren just valde. Bara fönster som syns på den AKTIVA Spacen: ett
// ägt fönster som flyttats till en annan Space ska ligga kvar där, inte
// dras hit. Rekursivt, så en dialogs egen dialog hamnar över den.
void MetalWindow::liftOwned()
{
    if (!nsWindow_)
        return;
    for (MetalWindow* o : owned_) {
        NSWindow* w = o->nsWindow_;
        if (!w || !w.isVisible || w.isMiniaturized || !w.isOnActiveSpace)
            continue;
        [w orderWindow:NSWindowAbove relativeTo:nsWindow_.windowNumber];
        o->liftOwned();
    }
}

// MINIMERINGEN: ägarens minimering döljer de ägda och återställningen tar
// fram dem igen — bara de som VI dolde, så att ett fönster appen själv
// gömt under tiden förblir gömt (`setVisible(false)` stryker det ur
// listan). Rekursivt av samma skäl som lyftet.
void MetalWindow::hideOwnedForMinimize()
{
    for (MetalWindow* o : owned_) {
        NSWindow* w = o->nsWindow_;
        if (!w || !w.isVisible)
            continue;
        o->hideOwnedForMinimize();
        [w orderOut:nil];
        hiddenByMinimize_.push_back(o);
    }
}

void MetalWindow::restoreOwnedAfterMinimize()
{
    if (!nsWindow_)
        return;
    const std::vector<MetalWindow*> dolda = std::move(hiddenByMinimize_);
    hiddenByMinimize_.clear();
    for (MetalWindow* o : dolda) {
        if (!o->nsWindow_)
            continue;
        [o->nsWindow_ orderWindow:NSWindowAbove
                       relativeTo:nsWindow_.windowNumber];
        o->restoreOwnedAfterMinimize();
    }
}

void MetalWindow::shutdown()
{
    // ÄGARSKAPET avvecklas FÖRST och i BÅDA riktningar: en
    // rå pekare åt något håll får aldrig överleva fönstret. Före vakten
    // nedan, så även ett fönster vars init misslyckades städas.
    if (ownerWin_) {
        std::erase(ownerWin_->owned_, this);
        std::erase(ownerWin_->hiddenByMinimize_, this);
        ownerWin_ = nullptr;
    }
    for (MetalWindow* o : owned_)
        o->ownerWin_ = nullptr;
    owned_.clear();
    hiddenByMinimize_.clear();

    // Städordningen ärvs från D3D11: timers → font (atlas-texturen hör
    // till renderern) → renderern → NSWindow-rivningen.
    for (auto& [id, timer] : timers_)
        [timer invalidate];   // inga ticks efter fönstrets död
    timers_.clear();
    gfxRenderer_.shutdownRenderer();
    if (globalMouseMonitor_) {
        [NSEvent removeMonitor:globalMouseMonitor_];
        globalMouseMonitor_ = nil;
    }
    // PROGRAMIKONEN: ägarskapet över dockan släpps (bilden står kvar tills
    // nästa fönster sätter en ikon); ramarna står kvar för ett nytt init.
    if (g_iconOwner == this)
        g_iconOwner = nullptr;
    iconApplied_ = false;
    if (!nsWindow_)
        return;
    unregisterWindowId(windowId_);
    @autoreleasepool {
        nsWindow_.delegate = nil;
        [view_ clearOwner];   // vyn kan överleva i autoreleasepooler
        [nsWindow_ orderOut:nil];
        [nsWindow_ close];   // releasedWhenClosed=NO — ARC städar objektet
        nsWindow_ = nil;
        view_     = nil;
        delegate_ = nil;
    }
}

// Lyft fönstret överst och ge det fokus (modal-klick-på-ägare m.m.).
// Ett noActivate-fönster (en NSPanel som aldrig blir key) lyfts med
// `orderFront:` och utan att appen aktiveras.
void MetalWindow::raise()
{
    // Dolt förblir dolt (fasaden): makeKeyAndOrderFront hade VISAT fönstret.
    if (!nsWindow_ || !nsWindow_.visible)
        return;
    if (noActivate_) {
        [nsWindow_ orderFront:nil];
        return;
    }
    [nsWindow_ makeKeyAndOrderFront:nil];
    [NSApp activate];
}

void MetalWindow::renderFrame()
{
    if (!nsWindow_)
        return;
    // EN POOL PER FRAME: commandbuffern, encoderna och drawablen är
    // autoreleasade, och commandbuffern håller varje resurs den rört vid liv
    // — staging-buffertarna och de ersatta chunkarnas buffertar. Pumpen är
    // vår egen (ingen NSApp-loop tömmer något), så utan poolen levde de i
    // trådens yttersta pool till processens slut: 8 GB GPU-minne per varv i
    // app_perf --varv, osynligt för `leaks` (nåbart via poolen).
    @autoreleasepool {
        gfxRenderer_.beginFrame();      // binder retained-RT:n
        if (renderCb_)
            renderCb_(*this);           // frame-innehållet
        gfxRenderer_.composite();       // RT → drawable
        drawChrome();                   // no-op vid Native
        if (overlayCb_)
            overlayCb_(*this);
        gfxRenderer_.present();
    }
}

// Fasadens readback (glaze-render): retained-RT:n 1:1 i fysiska px —
// pixelidentisk med det fönstret visar/skulle visa (WYSIWYG-kontraktet).
bool MetalWindow::captureFrame(std::vector<std::uint8_t>& rgba,
                               int& w, int& h)
{
    if (!nsWindow_)
        return false;
    return gfxRenderer_.captureRetained(rgba, w, h);
}

// ---------------------------------------------------------------- egenskaper

// No-op-säkra före init (tvåfas-mönstret: createWindow() i konstruktorn,
// init() i show()).

void MetalWindow::setSize(int w, int h)
{
    // LOGISKA px → punkter (× userScale); lagras som
    // invarianten.
    logicalW_ = w;
    logicalH_ = h;
    if (nsWindow_) {
        // Egen resize: onDidResize (som setContentSize kör synkront) får
        // INTE räkna tillbaka logisk storlek ur punktstorleken — den är
        // avrundad, och driften ackumuleras för varje omgång. Logiska
        // storleken ovan är sanningen.
        pendingScaleResize_ = true;
        [nsWindow_ setContentSize:NSMakeSize(w * userScale(),
                                             h * userScale())];
        pendingScaleResize_ = false;
    }
}

// STORLEKSGRÄNSERNA.
//
// `contentMinSize`/`contentMaxSize` talar PUNKTER, och punkterna bär
// användarskalan (Metal-specialen: drawable-px = punkter × backingScale,
// så punkt = logisk px × userScale). Därför måste de ÅTERAPPLICERAS vid
// userScale-byte — `userScaleChanged` gör det.
//
// `min == max` tar dessutom bort `NSWindowStyleMaskResizable`: utan det
// hade AppKit fortsatt visa resize-markören i kanten och zoom-knappen
// hade fortsatt fungera, alltså ett fönster som LOVAR något det inte kan.
void MetalWindow::applySizeLimits()
{
    if (!nsWindow_)
        return;
    const float us = userScale();
    // 0 = obegränsat. AppKit stavar "ingen gräns" som 0 för min och
    // CGFLOAT_MAX för max — inte 0, som hade betytt "max noll punkter".
    nsWindow_.contentMinSize =
        NSMakeSize(limMinW_ > 0 ? limMinW_ * us : 0.0,
                   limMinH_ > 0 ? limMinH_ * us : 0.0);
    nsWindow_.contentMaxSize =
        NSMakeSize(limMaxW_ > 0 ? limMaxW_ * us : CGFLOAT_MAX,
                   limMaxH_ > 0 ? limMaxH_ * us : CGFLOAT_MAX);

    // Chrome::None är borderless och har varken kant-resize eller
    // zoom-knapp — masken finns inte där att rensa.
    if (chrome_ == Chrome::None)
        return;
    const bool fixed = limMinW_ > 0 && limMinW_ == limMaxW_ &&
                       limMinH_ > 0 && limMinH_ == limMaxH_;
    NSWindowStyleMask m = nsWindow_.styleMask;
    if (fixed)
        m &= ~NSWindowStyleMaskResizable;
    else
        m |= NSWindowStyleMaskResizable;
    if (m != nsWindow_.styleMask)
        nsWindow_.styleMask = m;
}

void MetalWindow::setSizeLimits(int minW, int minH, int maxW, int maxH)
{
    limMinW_ = minW;
    limMinH_ = minH;
    limMaxW_ = maxW;
    limMaxH_ = maxH;
    applySizeLimits();
}

// ALLTID ÖVERST.
// AppKit-vägen är fönstrets LEVEL: `NSFloatingWindowLevel` lägger fönstret
// över vanliga fönster (även andra appars), `NSNormalWindowLevel` tar
// tillbaka det. Ingen `orderFront` här — nivån gäller z-ordningen, inte
// synligheten, och en av-göm-effekt vore en bieffekt appen inte bett om.
void MetalWindow::setTopmost(bool on)
{
    topmost_ = on;
    if (!nsWindow_)
        return;   // tvåfas: slår igenom i init via desc.topmost
    nsWindow_.level = on ? NSFloatingWindowLevel : NSNormalWindowLevel;
}

bool MetalWindow::topmost() const
{
    // Vad som FAKTISKT gäller — nivån läses ur fönstret, inte ur begäran.
    if (!nsWindow_)
        return topmost_;
    return nsWindow_.level >= NSFloatingWindowLevel;
}

// MUSPEKAREN. Formen lagras
// alltid; den SÄTTS direkt bara när pekaren står i vyn eller en knapp hålls
// (ett skikt som dras behåller ↔ utanför vyn). Annars hade ett fönster som
// fick sin form ändrad av en timer bytt pekaren över ett annat programs
// fönster. Vyns `cursorUpdate:` sätter den igen vid inträde.
void MetalWindow::setCursor(PointerShape shape)
{
    cursor_ = shape;
    if (view_ && (view_->mouseInside_ || [NSEvent pressedMouseButtons] != 0))
        [nsCursorFor(shape) set];
}

void MetalWindow::setTitle(const std::string& t)
{
    title_ = t;
    if (nsWindow_)
        nsWindow_.title = [NSString stringWithUTF8String:title_.c_str()]
                              ?: @"";
}

// Skärmrymden är AppKit-PUNKTER med top-left-konvention (flipp mot
// huvudskärmens höjd — screens[0] bär det globala origot), samma
// omvandling som init(desc.posX/posY). Fasadkontraktet: appkod räknar
// aldrig i rymden själv utan går via clientToScreen.

void MetalWindow::setPosition(int x, int y)
{
    if (!nsWindow_)
        return;
    const CGFloat screenH = NSScreen.screens.firstObject.frame.size.height;
    [nsWindow_ setFrameTopLeftPoint:NSMakePoint(x, screenH - y)];
}

Pointf MetalWindow::clientToScreen(float px, float py) const
{
    if (!nsWindow_ || !view_)
        return Pointf{0.0f, 0.0f};
    // Klient-FYSISKA px → vy-punkter (GlazeView är flippad = top-left) →
    // fönsterrymd → skärm (AppKit, origo nere-vänster) → top-left-flip.
    const CGFloat s     = nsWindow_.backingScaleFactor;
    const NSPoint pView = NSMakePoint(px / s, py / s);
    const NSPoint pWin  = [view_ convertPoint:pView toView:nil];
    const NSPoint pScr  = [nsWindow_ convertPointToScreen:pWin];
    const CGFloat screenH = NSScreen.screens.firstObject.frame.size.height;
    return Pointf{(float)pScr.x, (float)(screenH - pScr.y)};
}

Rectf MetalWindow::screenArea() const
{
    if (!nsWindow_)
        return Rectf{0.0f, 0.0f, 0.0f, 0.0f};
    // visibleFrame = arbetsytan (utan menyrad/Dock) på fönstrets skärm.
    NSScreen* scr = nsWindow_.screen ?: NSScreen.screens.firstObject;
    return toPlacement(scr.visibleFrame);
}

void MetalWindow::setVisible(bool on)
{
    if (!nsWindow_)
        return;
    gfxRenderer_.setPresenting(on);
    if (on) {
        // noActivate: visa utan att stjäla fokus (popups/menyer) — och
        // `showInactive_` den FÖRSTA gången: ett
        // flytande dockningsfönster som lossnar mitt i ett drag tar inte key
        // från fönstret som håller trycket; en senare visning gör det.
        if (noActivate_ || (showInactive_ && !firstShown_))
            [nsWindow_ orderFront:nil];
        else
            [nsWindow_ makeKeyAndOrderFront:nil];
        // STARTLÄGET vid FÖRSTA synligheten:
        // efter `orderFront`, för en zoom eller miniatyr på ett fönster som
        // aldrig visats animerar från ingenting. Normalrutan bar redan den
        // begärda ramen (init).
        if (!firstShown_) {
            firstShown_ = true;
            if (startState_ == WindowState::Maximized && !zoomed(nsWindow_))
                [nsWindow_ zoom:nil];
            else if (startState_ == WindowState::Minimized)
                [nsWindow_ miniaturize:nil];
        }
        pushSimple(windowId_, EventType::WindowShown);
        pushSimple(windowId_, EventType::WindowExposed);
    } else {
        [nsWindow_ orderOut:nil];
        // Appens EGET döljande vinner över ägarens minimering: dolt nu ska
        // förbli dolt när ägaren återställs.
        if (ownerWin_)
            std::erase(ownerWin_->hiddenByMinimize_, this);
    }
}

// Upprepande NSTimer på main-runloopen. Callbacket pushar Timer-eventet;
// pushEvent postar väcknings-eventet så nextEventMatchingMask vaknar —
// utan det sover pumpen genom ticken (Nyckelbeslut 3). CommonModes:
// tickar även under menyer/drag. No-op-säkra före init.

void MetalWindow::startTimer(std::uint32_t id, std::uint32_t ms)
{
    if (!nsWindow_ || id == 0)
        return;
    stopTimer(id);   // samma id = omstartad period (SetTimer-semantiken)
    const WindowId wid = windowId_;
    NSTimer* t = [NSTimer
        timerWithTimeInterval:ms / 1000.0
                      repeats:YES
                        block:^(NSTimer*) {
                            Event e;
                            e.type     = EventType::Timer;
                            e.windowId = wid;
                            e.timerId  = id;
                            pushEvent(e);
                        }];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    timers_[id] = t;
}

void MetalWindow::stopTimer(std::uint32_t id)
{
    const auto it = timers_.find(id);
    if (it == timers_.end())
        return;
    [it->second invalidate];
    timers_.erase(it);
}

// ---------------------------------------------------------------- geometri

// Fysiska px överallt (fasadens kontrakt): AppKit-punkter × skala.

Sizef MetalWindow::clientSize() const
{
    // OBS: backingScaleFactor, INTE fulla uiScale() — vy-punkterna bär
    // redan userScale, buffertens pixelmått är punkter ×
    // backing. Fulla skalan här hade dubbelapplicerat faktorn.
    if (!view_)
        return Sizef{0.0f, 0.0f};
    const NSSize  b = view_.bounds.size;
    const CGFloat s = nsWindow_ ? nsWindow_.backingScaleFactor : 1.0;
    return Sizef{(float)(b.width * s), (float)(b.height * s)};
}

float MetalWindow::uiScale() const
{
    // Fulla skalan: backingScaleFactor × användarskalan —
    // fysiska px = logiska px × uiScale() (punkterna bär userScale,
    // bufferten backing-faktorn; produkten är faktorn EXAKT en gång).
    const float os = nsWindow_ ? (float)nsWindow_.backingScaleFactor
                               : 1.0f;
    return os * userScale();
}

float MetalWindow::titlebarHeight() const
{
    return chrome_ == Chrome::Custom ? captionH_ * uiScale() : 0.0f;
}

bool MetalWindow::maximized() const
{
    return zoomed(nsWindow_);
}

// FÖNSTRETS GEOMETRI. I normalläge är den LEVANDE ramen svaret: AppKit
// förankrar en ram i sitt NEDRE vänstra hörn, så en `setContentSize` flyttar
// det övre utan någon flyttnotis, och en spårning hade då släpat efter.
// Zoomat eller i miniatyr är det den spårade rutan (`trackNormalFrame`).
// Storleken är `logicalW_`/`logicalH_`, som redan står still under zoomen
// och på återställningsflanken (onDidResize).
void MetalWindow::trackNormalFrame()
{
    if (!nsWindow_ || zoomed(nsWindow_) || nsWindow_.isMiniaturized)
        return;
    const Rectf r  = toPlacement(nsWindow_.frame);
    normalTopLeft_ = Pointf{r.x, r.y};
}

WindowPlacement MetalWindow::placement() const
{
    WindowPlacement p;
    if (!nsWindow_)
        return p;   // före init finns ingen geometri att spåra
    const bool zoomad   = zoomed(nsWindow_);
    const bool miniatyr = nsWindow_.isMiniaturized;
    Pointf     hörn     = normalTopLeft_;
    if (!zoomad && !miniatyr) {
        const Rectf r = toPlacement(nsWindow_.frame);
        hörn          = Pointf{r.x, r.y};
    }
    p.x           = static_cast<int>(std::lround(hörn.x));
    p.y           = static_cast<int>(std::lround(hörn.y));
    p.width       = logicalW_;
    p.height      = logicalH_;
    p.state       = miniatyr ? WindowState::Minimized
                  : zoomad   ? WindowState::Maximized
                             : WindowState::Normal;
    p.hasPosition = true;
    return p;
}

// Fönster-kontrollerna (fasaden): zoom är macOS "maximera" och togglar av
// sig själv — maximize() hålls REN genom isZoomed-vakten; restore() tar
// fönstret ur både miniatyr- och zoom-läget. No-op-säkra före init. FÖRE DEN
// FÖRSTA VISNINGEN byter de STARTLÄGET:
// `setVisible(true)` tillämpar det efter `orderFront`, och en zoom eller
// miniatyr på ett fönster som aldrig visats är odefinierad.
void MetalWindow::minimize()
{
    if (nsWindow_ && !firstShown_) {
        startState_ = WindowState::Minimized;
        return;
    }
    if (nsWindow_)
        [nsWindow_ miniaturize:nil];
}

void MetalWindow::maximize()
{
    if (nsWindow_ && !firstShown_) {
        startState_ = WindowState::Maximized;
        return;
    }
    if (nsWindow_ && !zoomed(nsWindow_))
        [nsWindow_ zoom:nil];
}

void MetalWindow::restore()
{
    if (!nsWindow_)
        return;
    if (!firstShown_) {
        startState_ = WindowState::Normal;
        return;
    }
    if (nsWindow_.isMiniaturized)
        [nsWindow_ deminiaturize:nil];
    else if (zoomed(nsWindow_))
        [nsWindow_ zoom:nil];
}

Rectf MetalWindow::contentArea() const
{
    const Sizef cs = clientSize();
    const float th = titlebarHeight();
    return Rectf{0.0f, th, cs.w, cs.h - th};
}

// Layer-geometrin följer vyn: contentsScale + drawableSize i fysiska px.
void MetalWindow::syncLayerGeometry()
{
    if (!view_ || !nsWindow_)
        return;
    auto* layer = (CAMetalLayer*)view_.layer;
    if (!layer)
        return;
    const CGFloat s = nsWindow_.backingScaleFactor;
    layer.contentsScale = s;
    const NSSize b = view_.bounds.size;
    layer.drawableSize = CGSizeMake(b.width * s, b.height * s);
}

// ---------------------------------------------------------------- delegat-krokar

void MetalWindow::onDidResize()
{
    // macOS har ingen egen zoom-delegate — flankdetektera isZoomed (samma
    // mönster som D3D11:s wasMaxOrMin_).
    const bool zoomed = maximized();
    if (zoomed && !wasZoomed_)
        pushSimple(windowId_, EventType::WindowMaximized);
    else if (!zoomed && wasZoomed_)
        pushSimple(windowId_, EventType::WindowRestored);
    // ÅTERSTÄLLNINGS-FLANKEN. `!zoomed`-vakten nedan är
    // falsk exakt PÅ den här flanken — och en tillbakaräkning ger då fel:
    // 801 logiska × 0.5 = 401 punkter, 401 ÷ 1.25 = 321.
    // macOS återställer den ihågkomna rect:en i den skala som gällde vid
    // zoomningen och vet ingenting om vår.
    const bool restoreFlank = (!zoomed && wasZoomed_);
    wasZoomed_ = zoomed;

    // REENTRANSEN: `setContentSize` kör windowDidResize → onDidResize
    // SYNKRONT. ÄGARSKAPET är skrivet: den INRE körningen har den
    // slutgiltiga storleken och äger både renderer-resizen, `renderFrame`
    // och event-emissionen — den YTTRE returnerar direkt efter
    // korrigerings-anropet utan att pusha om. Utan det får en enda
    // storleksändring dubbla WindowResized OCH två renderFrame, och
    // `glaze::App` ritar om per event (regel 1). Korrigeringen ligger
    // därför FÖRE emissionen.
    if (restoreFlank && !pendingScaleResize_ && view_ && nsWindow_) {
        const float  us     = userScale();
        const NSSize cur    = view_.bounds.size;
        const NSSize target = NSMakeSize(logicalW_ * us, logicalH_ * us);
        if (us > 0.0f &&
            (std::lround(cur.width) != std::lround(target.width) ||
             std::lround(cur.height) != std::lround(target.height))) {
            pendingScaleResize_ = true;
            [nsWindow_ setContentSize:target];
            pendingScaleResize_ = false;
            return;   // den INRE körningen ägde emissionen
        }
    }

    pushSimple(windowId_, EventType::WindowResized);
    pushSimple(windowId_, EventType::WindowPixelSizeChanged);

    // ANVÄNDAR-resize: räkna tillbaka den lagrade LOGISKA storleken
    // (punkter ÷ userScale). ALDRIG ur vår egen skalresize
    // (vakten — avrundningen driftar), aldrig zoomad och ALDRIG PÅ
    // RESTORE-FLANKEN (se ovan).
    if (!pendingScaleResize_ && !zoomed && !restoreFlank && view_) {
        const NSSize b  = view_.bounds.size;
        const float  us = userScale();
        if (b.width > 0 && b.height > 0 && us > 0.0f) {
            logicalW_ = std::max(
                1, (int)std::lround((double)b.width / us));
            logicalH_ = std::max(
                1, (int)std::lround((double)b.height / us));
        }
    }

    syncLayerGeometry();
    // NORMALRUTAN: en storleksändring från en kant
    // flyttar hörnet — bara i normalläge, det avgör `trackNormalFrame`.
    trackNormalFrame();
    const Sizef cs = clientSize();
    gfxRenderer_.resize((int)cs.w, (int)cs.h);
    // Rita direkt — under live-resize står pumpens nextEvent-loop still
    renderFrame();
}

// Användarskalan bytte (anropas av glaze::App per öppet fönster):
// ny PUNKTstorlek ur den lagrade logiska (punkter = logiska × userScale),
// event EFTER renderer-resizen — setContentSize kör windowDidResize →
// onDidResize SYNKRONT (layer + renderer får nya storleken), så eventet
// sist är rätt.
void MetalWindow::userScaleChanged()
{
    if (!nsWindow_)
        return;
    // Punkterna bär userScale, så gränserna betyder något annat efter ett
    // skalbyte — räkna om dem FÖRE resizen, annars klampar AppKit den nya
    // storleken mot de gamla talen.
    applySizeLimits();
    if (maximized()) {
        // OS:et äger zoomad geometri — hoppa resizen men pusha eventet
        // (fonten måste ändå om); lagrad logisk röres inte. Det räcker
        // INTE av sig självt: `onDidResize`:s restore-flank måste sätta om
        // punktstorleken ur den lagrade logiska i AKTUELL skala —
        // annars kommer rect:en tillbaka i den gamla.
        pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
        return;
    }
    const float  us     = userScale();
    const NSSize target = NSMakeSize(logicalW_ * us, logicalH_ * us);
    const NSSize cur    = view_ ? view_.bounds.size : NSMakeSize(0, 0);
    if (std::lround(cur.width) == std::lround(target.width) &&
        std::lround(cur.height) == std::lround(target.height)) {
        // Samma punktstorlek ⇒ ingen windowDidResize kommer — eventet
        // direkt, annars laddas fonten aldrig om (X11-mönstret).
        pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
        return;
    }
    pendingScaleResize_ = true;   // onDidResize ska inte räkna tillbaka
    [nsWindow_ setContentSize:target];
    pendingScaleResize_ = false;
    pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
}

void MetalWindow::onBackingChanged()
{
    syncLayerGeometry();
    const Sizef cs = clientSize();
    gfxRenderer_.resize((int)cs.w, (int)cs.h);
    measureDisplay(true);   // bufferten är tätare eller glesare nu
    pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
    pushSimple(windowId_, EventType::WindowPixelSizeChanged);
}

void MetalWindow::onScreenChanged()
{
    measureDisplay(true);
}

// SKÄRMENS mm. Bufferten Metal ritar i är
// punkter × backingScaleFactor (i "Mer utrymme" STÖRRE än panelen, och
// WindowServer skalar ner — formeln mäter bufferten mot millimetrarna och är
// rätt i båda fallen), och CGDisplayScreenSize läser EDID. `NSDeviceResolution`
// duger inte: den svarar 72 dpi och beskriver koordinatrymden, inte panelen.
// Saknar skärmen EDID räknar CGDisplayScreenSize fram en storlek ur 72 dpi
// mot CGDisplayBounds — den ser mätt ut men är det inte, och den
// detekteras och blir Assumed: 96 dpi i BUFFERTENS px (bufferten antas lika
// tät som panelen; någon omräkning buffert/panel i
// "Mer utrymme" görs inte).
void MetalWindow::measureDisplay(bool announce)
{
    if (!nsWindow_)
        return;
    DisplayMetrics m;
    @autoreleasepool {
        NSScreen* screen = nsWindow_.screen ?: NSScreen.mainScreen;
        NSNumber* num =
            screen ? [screen.deviceDescription objectForKey:@"NSScreenNumber"]
                   : nil;
        if (!num) {
            m = display::assumed("", "fönstret står inte på någon skärm");
        } else {
            const CGDirectDisplayID did = (CGDirectDisplayID)num.unsignedIntValue;
            const NSRect  frame   = screen.frame;               // punkter
            const CGFloat backing = screen.backingScaleFactor;
            const int pxW = (int)std::lround(frame.size.width * backing);
            const int pxH = (int)std::lround(frame.size.height * backing);
            const CGSize mm     = CGDisplayScreenSize(did);
            const CGRect bounds = CGDisplayBounds(did);         // punkter
            const std::string id = display::edidId(
                (std::uint16_t)CGDisplayVendorNumber(did),
                (std::uint16_t)CGDisplayModelNumber(did),
                (std::uint32_t)CGDisplaySerialNumber(did));
            if (display::isApple72Estimate((int)std::lround(bounds.size.width),
                                           (int)std::lround(bounds.size.height),
                                           mm.width, mm.height))
                m = display::assumed(id,
                                     "CGDisplayScreenSize är Apples "
                                     "72-dpi-uppskattning (ingen EDID)");
            else
                m = display::measure(pxW, pxH, (int)std::lround(mm.width),
                                     (int)std::lround(mm.height), id);
        }
    }
    const bool ändrad = m.pxPerMm != metrics_.pxPerMm ||
                        m.source != metrics_.source || m.id != metrics_.id;
    metrics_ = std::move(m);
    if (announce && ändrad)
        pushSimple(windowId_, EventType::WindowDisplayChanged);
}

void MetalWindow::onOcclusionVisible()
{
    pushSimple(windowId_, EventType::WindowExposed);
}

// ---------------------------------------------------------------- events

// Ordagrann port av D3dWindow::processEvent — chrome-knapp-grenarna gäller
// bara Custom, men switchen är hel.
bool MetalWindow::processEvent(const Event& e)
{
    switch (e.type) {
    // false = "det här fönstret begär stängning" — både per-fönster-
    // begäran (krysset) och app-avslut (Quit når varje fönster).
    case EventType::Quit:
    case EventType::WindowCloseRequested:
        return false;

    // Chrome-knapparna är CaptionButton-widgets, så
    // musen går orörd till widget-lagret.
    default:
        break;
    }
    return true;
}

// ---------------------------------------------------------------- hit-test

// Custom-chromets dragyta: titelraden minus knapparna. Resize-kanterna
// äger OS:et redan (Resizable-masken) — till skillnad från D3D11:s
// WM_NCHITTEST behövs inga egna kantzoner.
bool MetalWindow::captionHitTest(float x, float y) const
{
    if (chrome_ != Chrome::Custom)
        return false;
    // Kartan ur widget-trädet: första träffen
    // vinner. Resize-rollerna är GENOMSKINLIGA här — kanterna äger OS:et
    // på Mac (Resizable-masken), och remsans översta pixlar ska kännas
    // som caption precis som i native-titelrader.
    for (const auto& r : hitRegions_) {
        if (!r.rect.contains(x, y))
            continue;
        switch (r.role) {
        case HitRole::Caption:
            return true;
        case HitRole::Client:
        case HitRole::MinButton:
        case HitRole::MaxButton:
        case HitRole::CloseButton:
            return false;   // widget-yta — klicket går till trädet
        default:
            continue;       // resize-roller: se ovan
        }
    }
    return false;
}

// Transparent-vägens klick-genomsläpp: macOS saknar per-pixel-hit-test-API
// (WM_NCHITTEST-motsvarighet) — toggla ignoresMouseEvents dynamiskt efter
// alfa-spegeln. Etablerat mönster (Electron m.fl.).
void MetalWindow::updateClickThrough(float x, float y)
{
    if (!transparent_ || !nsWindow_)
        return;
    const bool hit = gfxRenderer_.alphaHit(static_cast<int>(x),
                                           static_cast<int>(y));
    nsWindow_.ignoresMouseEvents = hit ? NO : YES;
}

// ---------------------------------------------------------------- ritning

void MetalWindow::drawChrome()
{
    if (chrome_ != Chrome::Custom)
        return;

    // Captionen (titelrad + knappar) ritas av
    // TopBar-widgeten i innehållspasset. Här finns bara
    // den tunna ramen runt omaximerat fönster.
    const Sizef cs = clientSize();
    if (!maximized())
        gfxRenderer_.strokeRect(Rectf{0.0f, 0.0f, cs.w, cs.h},
                                chromeColors_.frame);
}

} // namespace gfx
