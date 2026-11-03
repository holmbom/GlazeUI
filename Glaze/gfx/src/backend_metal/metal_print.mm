// UTSKRIFTEN PÅ macOS — fasaden gfx/print.h ovanpå
// NSPrintPanel och NSPrintOperation.
//
// DIALOGEN är `NSPrintPanel runModalWithPrintInfo:` — APPMODAL och inte ett
// ark på ägaren: arket är asynkront, och fasaden svarar när dialogen
// stängts. Förvalen går in i en KOPIA av `sharedPrintInfo` (papperet i
// punkter, orienteringen, kopiorna och sidintervallet), och samma NSPrintInfo
// är `PrintSettings::native` efteråt, med skrivaren användaren valde.
// Marginalerna sätts till 0: sidan är HELA papperet, och bilden läggs på det
// utskrivbara området (`imageablePageBounds`).
//
// JOBBET samlar sidornas bilder (CGImage) och skickar dem i `finish` genom
// en NSPrintOperation med en vy som känner sina sidor (`knowsPageRange:`),
// utan dialog och med systemets förloppspanel. Sidintervallet har glaze
// redan gjort — bara de valda sidorna skickas — så jobbet skriver ut ALLA
// vyns sidor. Kopiorna står kvar i NSPrintInfo, och systemet skriver dem.
//
// TILL FIL: en satt `PrintSettings::outputFile` ger jobbets egen kopia av
// NSPrintInfo `jobDisposition = NSPrintSaveJob` och `NSPrintJobSavingURL` med
// filen, så att jobbet skrivs till filen och aldrig går till papper. Före
// start kontrolleras att filen (eller, när den inte finns, dess katalog) går
// att skriva, annars blir svaret `FileFailed`; efter `runOperation` kontrolleras
// att filen finns. Skrivaren frågar aldrig efter fil här (`promptsForFile` är
// false och `outputExtension` tom), och orsaken till ett misslyckat
// `runOperation` går inte att läsa, så det blir `Failed`.

#import <AppKit/AppKit.h>

#include <algorithm>

#include "gfx/platform.h"
#include "gfx/print.h"

namespace {

// Punkter per mm (1 pt = 1/72 tum).
constexpr CGFloat kPtPerMm = 72.0 / 25.4;

} // namespace

// VYN som ritar sidorna: en sida per bild, staplade nedifrån (vyn är inte
// vänd), varje sida pappersstor och bilden på det utskrivbara området.
@interface GlazePrintView : NSView
@property(nonatomic, strong) NSArray* pages;   // CGImageRef per sida (id)
@property(nonatomic) NSSize paper;             // pappersets punkter, orienterat
@property(nonatomic) NSRect printable;         // sidans utskrivbara del, y uppåt
@end

@implementation GlazePrintView

- (BOOL)knowsPageRange:(NSRangePointer)range
{
    range->location = 1;
    range->length   = self.pages.count;
    return YES;
}

- (NSRect)rectForPage:(NSInteger)page
{
    const CGFloat y =
        static_cast<CGFloat>(static_cast<NSInteger>(self.pages.count) - page) *
        self.paper.height;
    return NSMakeRect(0.0, y, self.paper.width, self.paper.height);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const NSInteger page = [NSPrintOperation currentOperation].currentPage;
    if (page < 1 || page > static_cast<NSInteger>(self.pages.count))
        return;
    CGContextRef cg   = [NSGraphicsContext currentContext].CGContext;
    const NSRect sida = [self rectForPage:page];
    const CGRect mål  = CGRectMake(sida.origin.x + self.printable.origin.x,
                                   sida.origin.y + self.printable.origin.y,
                                   self.printable.size.width,
                                   self.printable.size.height);
    CGContextSetInterpolationQuality(cg, kCGInterpolationHigh);
    CGContextDrawImage(
        cg, mål,
        (__bridge CGImageRef)self.pages[static_cast<NSUInteger>(page - 1)]);
}

@end

namespace gfx {

namespace {

// `PrintSettings::native` — skrivarens NSPrintInfo.
struct MacPrintInfo {
    NSPrintInfo* info;
};

// Inställningarnas papper, orientering, kopior och sidintervall i `info`.
void applySettings(NSPrintInfo* info, const PrintSettings& s)
{
    info.orientation = NSPaperOrientationPortrait;
    info.paperSize   = NSMakeSize(s.paperMm.w * kPtPerMm, s.paperMm.h * kPtPerMm);
    // Liggande byter papperets bredd och höjd (NSPrintInfo.h).
    if (s.orientation == PageOrientation::Landscape)
        info.orientation = NSPaperOrientationLandscape;
    info.leftMargin           = 0.0;
    info.rightMargin          = 0.0;
    info.topMargin            = 0.0;
    info.bottomMargin         = 0.0;
    info.horizontallyCentered = NO;
    info.verticallyCentered   = NO;
    NSMutableDictionary* d = info.dictionary;
    d[NSPrintCopies]       = @(std::max(1, s.copies));
    if (s.fromPage > 0) {
        d[NSPrintAllPages]  = @NO;
        d[NSPrintFirstPage] = @(s.fromPage);
        d[NSPrintLastPage]  = @(s.toPage > 0 ? s.toPage : s.fromPage);
    } else {
        d[NSPrintAllPages] = @YES;
    }
}

// UT-fälten ur `info`.
void readSettings(NSPrintInfo* info, PrintSettings& s)
{
    const NSSize p    = info.paperSize;   // orienterat
    const bool   land = info.orientation == NSPaperOrientationLandscape;
    s.orientation     = land ? PageOrientation::Landscape : PageOrientation::Portrait;
    const float b     = static_cast<float>(p.width / kPtPerMm);
    const float h     = static_cast<float>(p.height / kPtPerMm);
    s.paperMm         = land ? Sizef{h, b} : Sizef{b, h};
    const NSRect ib   = info.imageablePageBounds;   // y uppåt
    s.printableMm     = Rectf{
        static_cast<float>(ib.origin.x / kPtPerMm),
        static_cast<float>((p.height - ib.origin.y - ib.size.height) / kPtPerMm),
        static_cast<float>(ib.size.width / kPtPerMm),
        static_cast<float>(ib.size.height / kPtPerMm)};
    NSDictionary* d = info.dictionary;
    s.copies        = std::max(1, [d[NSPrintCopies] intValue]);
    if ([d[NSPrintAllPages] boolValue]) {
        s.fromPage = 0;
        s.toPage   = 0;
    } else {
        s.fromPage = [d[NSPrintFirstPage] intValue];
        s.toPage   = [d[NSPrintLastPage] intValue];
    }
    s.promptsForFile = false;   // NSPrintOperation frågar aldrig efter fil här
    s.outputExtension.clear();
    NSPrinter* skrivare = info.printer;
    s.printer = skrivare.name ? std::string(skrivare.name.UTF8String) : std::string();
    NSValue* upplösning = skrivare.deviceDescription[NSDeviceResolution];
    if (upplösning) {
        const NSSize dpi = upplösning.sizeValue;
        s.dpiX           = static_cast<float>(dpi.width);
        s.dpiY           = static_cast<float>(dpi.height);
    }
}

class MetalPrintDocument final : public PrintDocument {
public:
    MetalPrintDocument(NSPrintInfo* info, const PrintSettings& s,
                       const std::string& title)
        : info_([info copy]), pages_([NSMutableArray array]),
          title_([NSString stringWithUTF8String:title.c_str()]),
          file_(s.outputFile.empty()
                    ? nil
                    : [NSURL fileURLWithPath:[NSString stringWithUTF8String:
                                                           s.outputFile.c_str()]])
    {
        const CGFloat pb = s.paperMm.w * kPtPerMm;
        const CGFloat ph = s.paperMm.h * kPtPerMm;
        paper_ = s.orientation == PageOrientation::Landscape
                     ? NSMakeSize(ph, pb)
                     : NSMakeSize(pb, ph);
        // Det utskrivbara området med y uppåt inom sidan.
        printable_ = NSMakeRect(
            s.printableMm.x * kPtPerMm,
            paper_.height - (s.printableMm.y + s.printableMm.h) * kPtPerMm,
            s.printableMm.w * kPtPerMm, s.printableMm.h * kPtPerMm);
    }

    bool addPage(const std::uint8_t* rgba, int w, int h) override
    {
        if (!open_ || !rgba || w <= 0 || h <= 0)
            return false;
        @autoreleasepool {
            const std::size_t rad = static_cast<std::size_t>(w) * 4;
            CFDataRef data = CFDataCreate(
                nullptr, rgba, static_cast<CFIndex>(rad * static_cast<std::size_t>(h)));
            CGDataProviderRef källa = CGDataProviderCreateWithCFData(data);
            CGColorSpaceRef   rymd  = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            CGImageRef        bild  = CGImageCreate(
                static_cast<std::size_t>(w), static_cast<std::size_t>(h), 8, 32,
                rad, rymd,
                static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipLast) |
                    kCGBitmapByteOrderDefault,
                källa, nullptr, false, kCGRenderingIntentDefault);
            CGColorSpaceRelease(rymd);
            CGDataProviderRelease(källa);
            CFRelease(data);
            if (!bild) {
                log("Utskrift: sidans bild (%d × %d) gick inte att skapa", w, h);
                return false;
            }
            [pages_ addObject:CFBridgingRelease(bild)];
        }
        return true;
    }

    PrintJobResult finish() override
    {
        if (!open_)
            return PrintJobResult::Failed;
        open_ = false;
        if (pages_.count == 0)
            return PrintJobResult::Failed;
        @autoreleasepool {
            GlazePrintView* vy = [[GlazePrintView alloc]
                initWithFrame:NSMakeRect(0.0, 0.0, paper_.width,
                                         paper_.height *
                                             static_cast<CGFloat>(pages_.count))];
            vy.pages     = pages_;
            vy.paper     = paper_;
            vy.printable = printable_;
            // Intervallet är redan gjort: bara de valda sidorna finns i vyn.
            info_.dictionary[NSPrintAllPages] = @YES;
            if (file_) {
                // Jobbet sparas i filen och går aldrig till papper.
                info_.jobDisposition                    = NSPrintSaveJob;
                info_.dictionary[NSPrintJobSavingURL] = file_;
            }
            NSPrintOperation* op =
                [NSPrintOperation printOperationWithView:vy printInfo:info_];
            op.showsPrintPanel    = NO;
            op.showsProgressPanel = YES;
            op.jobTitle           = title_;
            const BOOL ok = [op runOperation];
            pages_        = nil;
            if (!ok) {
                log("Utskrift: NSPrintOperation skrev inte ut jobbet");
                return PrintJobResult::Failed;
            }
            if (file_ && ![[NSFileManager defaultManager]
                              fileExistsAtPath:file_.path]) {
                log("Utskrift: filen '%s' skrevs inte", file_.path.UTF8String);
                return PrintJobResult::FileFailed;
            }
            return PrintJobResult::Ok;
        }
    }

    void abort() override
    {
        open_  = false;
        pages_ = nil;   // inget har skickats — sidorna släpps
    }

private:
    NSPrintInfo*    info_;
    NSMutableArray* pages_;
    NSString*       title_;
    NSURL*          file_;   // nil när jobbet inte går till en fil
    NSSize          paper_;
    NSRect          printable_;
    bool            open_ = true;
};

} // namespace

bool printAvailable()
{
    return true;
}

PrintDialogResult showPrintDialog(Window* owner, PrintSettings& s)
{
    (void)owner;   // appmodal — se filhuvudet
    @autoreleasepool {
        NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
        applySettings(info, s);
        NSPrintPanel* panel = [NSPrintPanel printPanel];
        panel.options = NSPrintPanelShowsCopies | NSPrintPanelShowsPageRange |
                        NSPrintPanelShowsPaperSize | NSPrintPanelShowsOrientation;
        if ([panel runModalWithPrintInfo:info] != NSModalResponseOK)
            return PrintDialogResult::Cancelled;
        readSettings(info, s);
        s.native = std::make_shared<MacPrintInfo>(MacPrintInfo{info});
        return PrintDialogResult::Print;
    }
}

bool openPrinter(PrintSettings& s)
{
    @autoreleasepool {
        NSPrinter* skrivare =
            s.printer.empty()
                ? [NSPrintInfo defaultPrinter]
                : [NSPrinter printerWithName:[NSString
                                                 stringWithUTF8String:s.printer.c_str()]];
        if (!skrivare) {
            log("Utskrift: skrivaren '%s' finns inte",
                s.printer.empty() ? "(standard)" : s.printer.c_str());
            return false;
        }
        NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
        info.printer      = skrivare;
        applySettings(info, s);
        readSettings(info, s);
        s.native = std::make_shared<MacPrintInfo>(MacPrintInfo{info});
        return true;
    }
}

PrintStart beginPrintDocument(const PrintSettings& s, const std::string& title)
{
    PrintStart start;   // Failed tills jobbet startat
    // `native` sätts bara här i filen, så typen är känd.
    const auto info = std::static_pointer_cast<MacPrintInfo>(s.native);
    if (!info || !info->info) {
        log("Utskrift: ingen skrivare vald");
        return start;
    }
    if (!s.outputFile.empty()) {
        @autoreleasepool {
            NSFileManager* fm = [NSFileManager defaultManager];
            NSString* path = [NSString stringWithUTF8String:s.outputFile.c_str()];
            BOOL      katalog = NO;
            const BOOL finns  = path && [fm fileExistsAtPath:path isDirectory:&katalog];
            // En befintlig fil ska gå att skriva, annars ska katalogen det.
            const BOOL går =
                path && !katalog &&
                (finns ? [fm isWritableFileAtPath:path]
                       : [fm isWritableFileAtPath:[path stringByDeletingLastPathComponent]]);
            if (!går) {
                log("Utskrift: filen '%s' går inte att skriva", s.outputFile.c_str());
                start.result = PrintJobResult::FileFailed;
                return start;
            }
        }
    }
    start.result = PrintJobResult::Ok;
    start.doc    = std::make_unique<MetalPrintDocument>(info->info, s, title);
    return start;
}

} // namespace gfx
