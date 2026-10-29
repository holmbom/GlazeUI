// Utskriften — se glaze/print.h för API:t och besluten.
//
// Jobbet går i ett MODALT FÖRLOPPSFÖNSTER, meddelanderutans syskon (mitt över
// ägaren, måtten `msg*`, mätningen i `onLoad`, Avbryt som fönstrets
// avbrytknapp). Sidorna ritas EN I TAGET i pump-svansen (`App::post`): varje
// steg ritar en sida, skickar den och postar nästa, så förloppet ritas och
// Avbryt tas emot mellan sidorna — ingen egen pump, ingen tråd.
//
// SIDAN RITAS MED RITYTANS MOTOR i förloppsfönstrets renderare, utanför en
// frame (`beginOffscreen`, renderToBitmaps väg): ett render-mål över det
// utskrivbara området, rensat till vitt, och en `Painter` vars VÄRLD är
// sidans mm — kartkameran ortografiskt med (0, 0) i pappersets hörn och y
// nedåt. Fönstrets Ctx bär sidans täthet: `pxPerMm` är målets punkter per mm
// (så `glaze::mm` är millimeter på papperet) och `scale` målets punkter per
// logisk px (1/96 tum). Bilderna laddas upp i fönstrets bildcache, och en
// sida som fick en bild att vänta på uppladdningens budget ritas om — varje
// varv laddar upp minst en (ImageCache, framens första uppladdning går alltid
// igenom) och det som ritades i förra varvet vräks aldrig, så sidan blir
// komplett.

#include "glaze/print.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <gfx/platform.h>
#include <gfx/renderer.h>

#include "canvas/camera_math.h"
#include "canvas/scene.h"
#include "dialog_layout.h"
#include "glaze/app.h"
#include "glaze/strings.h"
#include "glaze/widgets/button.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/progress.h"
#include "image_cache.h"

namespace glaze {

namespace {

// Millimeter per tum, och de logiska px som en tum har (96 dpi).
constexpr double kMmPerInch     = 25.4;
constexpr double kLogicalPerInch = 96.0;
// Upplösningens gränser (punkter per tum).
constexpr int kMinDpi = 72;
constexpr int kMaxDpi = 1200;
// Ett standardformat matchar när båda måtten stämmer så här nära (mm).
constexpr double kPaperToleranceMm = 1.0;
// Varv en sida får ritas om för bilder som väntade på uppladdningen — varje
// varv laddar upp minst en, så taket nås bara av en sida med fler bilder.
constexpr int kMaxUploadRounds = 256;

struct PaperDims {
    PaperSize kind;
    double    w;
    double    h;
};
constexpr PaperDims kPapers[] = {
    {PaperSize::A3, 297.0, 420.0},     {PaperSize::A4, 210.0, 297.0},
    {PaperSize::A5, 148.0, 210.0},     {PaperSize::A6, 105.0, 148.0},
    {PaperSize::Letter, 215.9, 279.4}, {PaperSize::Legal, 215.9, 355.6},
    {PaperSize::Tabloid, 279.4, 431.8},
};

// En rad per process när plattformen saknar utskrift (systemfältets mönster).
void logUnsupported(const char* vem)
{
    static bool loggat = false;
    if (loggat)
        return;
    loggat = true;
    gfx::log("PrintJob::%s: utskriften är inte byggd på den här plattformen "
             "(Linux) — inget skrivs ut",
             vem);
}

} // namespace

// ---------------------------------------------------------------- förloppet

// FÖRLOPPSFÖNSTRET — jobbets steg (TU-lokalt som meddelanderutans fönster;
// vän till `PrintJob` för dess räknare).
class PrintProgressWindow : public AppWindow {
public:
    PrintProgressWindow(PrintJob& job, std::unique_ptr<gfx::PrintDocument> doc,
                        int pages, PrintJob::PageFn draw);
    ~PrintProgressWindow() override;

    bool succeeded() const { return succeeded_; }

protected:
    // Språkbytet: titeln och knappen är `TrText`, statusraden byggs om.
    void onLanguageChanged() override
    {
        updateStatus();
        layoutAndSize();
    }

private:
    // SIDANS RASTER — målets storlek och täthet, räknade en gång.
    struct Raster {
        int    w = 0;
        int    h = 0;
        double pxPerMm = 0.0;
        double cx = 0.0;   // det utskrivbara områdets mitt i sidans mm
        double cy = 0.0;
    };
    Raster raster() const;

    void layoutAndSize();
    void updateStatus();
    void postStep();
    void step();
    void abortJob();
    // Ritar sidan; `read` = läs tillbaka bilden. false = ett fel (loggat).
    bool renderPage(int page, bool read, std::vector<std::uint8_t>& rgba,
                    bool& more);

    PrintJob&                           job_;
    std::unique_ptr<gfx::PrintDocument> doc_;
    PrintJob::PageFn                    draw_;
    Raster                              raster_;
    int                                 pages_;   // 0 = sida för sida
    int                                 page_      = 1;
    int                                 total_     = 0;   // sidor att skicka, 0 = okänt
    bool                                done_      = false;
    bool                                succeeded_ = false;
    // Livstecknet för de postade stegen — fönstret kan stängas med ett steg
    // kvar i kön.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    Label*    status_ = nullptr;
    Progress* bar_    = nullptr;
    Button*   cancel_ = nullptr;
};

PrintProgressWindow::PrintProgressWindow(PrintJob&                           job,
                                         std::unique_ptr<gfx::PrintDocument> doc,
                                         int pages, PrintJob::PageFn draw)
    : AppWindow(tr(str::PrintTitle)), job_(job), doc_(std::move(doc)),
      draw_(std::move(draw)), pages_(pages)
{
    setModal(true);
    setStartPosition(StartPosition::CenterOwner);
    setMinimizeBox(false);
    setMaximizeBox(false);
    appIcon.hide();   // ingen programikon i en dialogs namnlist

    // Intervallet: i ett känt antal börjar jobbet på första sidan i det;
    // sida för sida måste det rita sig fram dit.
    const int från = job_.fromPage();
    const int till = job_.toPage();
    if (pages_ > 0) {
        page_  = std::max(1, från);
        total_ = std::max(0, std::min(till > 0 ? till : pages_, pages_) - page_ + 1);
    } else if (från > 0 && till > 0) {
        total_ = till - från + 1;
    }
    raster_ = raster();

    status_       = add<Label>();
    status_->name = "status";
    bar_          = add<Progress>();
    bar_->name    = "progress";
    if (total_ > 0) {
        bar_->setMax(static_cast<float>(total_));
    } else {
        bar_->setIndeterminate(true);
    }
    // AVBRYT ÄR AVBRYTKNAPPEN: klicket, Escape och krysset stänger fönstret,
    // och stängningen avbryter jobbet (`onClose`).
    cancel_       = add<Button>(tr(str::Cancel));
    cancel_->name = "cancel";
    cancel_->setDialogResult(DialogResult::Cancel);
    cancel_->anchors = {false, false, true, true};
    setCancelButton(*cancel_);

    onLoad = [this] {
        updateStatus();
        layoutAndSize();
    };
    // Första steget när fönstret syns — förloppet ritas före första sidan.
    onShow  = [this] { postStep(); };
    onClose = [this] { abortJob(); };
}

PrintProgressWindow::~PrintProgressWindow()
{
    alive_.reset();   // ett steg kvar i kön gör ingenting
    if (doc_ && !done_)
        doc_->abort();
}

PrintProgressWindow::Raster PrintProgressWindow::raster() const
{
    const Rect pa = job_.printableArea();
    // Tätheten: jobbets upplösning, aldrig över skrivarens egen.
    double dpi = job_.resolution();
    const gfx::PrintSettings& s = job_.settings_;
    if (s.dpiX > 0.0f)
        dpi = std::min(dpi, static_cast<double>(s.dpiX));
    if (s.dpiY > 0.0f)
        dpi = std::min(dpi, static_cast<double>(s.dpiY));
    // Den längsta sidan inom målets tak.
    const double längst = std::max(pa.w, pa.h) / kMmPerInch * dpi;
    if (längst > PrintJob::kMaxPagePx) {
        static bool loggat = false;
        const double ny    = dpi * PrintJob::kMaxPagePx / längst;
        if (!loggat) {
            loggat = true;
            gfx::log("PrintJob: sidan (%.0f × %.0f mm) rastreras i %.0f dpi i "
                     "stället för %.0f — bilden får vara högst %d punkter; "
                     "loggas en gång",
                     pa.w, pa.h, ny, dpi, PrintJob::kMaxPagePx);
        }
        dpi = ny;
    }
    Raster r;
    r.pxPerMm = dpi / kMmPerInch;
    r.w       = std::max(1, static_cast<int>(std::lround(pa.w * r.pxPerMm)));
    r.h       = std::max(1, static_cast<int>(std::lround(pa.h * r.pxPerMm)));
    r.cx      = pa.x + pa.w * 0.5;
    r.cy      = pa.y + pa.h * 0.5;
    return r;
}

// LAYOUTEN — statusraden, stapeln under den och Avbryt högerställd i
// dialogernas knapprad (dialog_layout.h). Allt i LOGISKA px ur stilen.
void PrintProgressWindow::layoutAndSize()
{
    const Ctx    c     = makeCtx();
    const Style& m     = *c.style;
    const float  bredd = m.msgTextMaxW;
    const float  rad   = Draw::lineHeight(c);

    status_->x      = m.msgPad;
    status_->y      = m.msgPad;
    status_->width  = bredd;
    status_->height = rad;
    bar_->x         = m.msgPad;
    bar_->y         = m.msgPad + rad + m.msgGap;
    bar_->width     = bredd;
    bar_->height    = m.progressHeight;

    const dialoglayout::ButtonRow knappar =
        dialoglayout::placeButtonRow(c, {cancel_});
    const float contentW =
        std::max(2.0f * m.msgPad + bredd, 2.0f * m.msgPad + knappar.w);
    const float contentH =
        bar_->y + m.progressHeight + m.msgGap + knappar.h + m.msgPad;
    dialoglayout::lockSize(*this, contentW, contentH);
}

// "Sida 3 / 10" — eller bara "Sida 3" när antalet är okänt.
void PrintProgressWindow::updateStatus()
{
    const TrText ord = tr(str::PrintPage);
    std::string  t   = ord.str() + " " + std::to_string(page_);
    if (pages_ > 0)
        t += " / " + std::to_string(job_.toPage() > 0
                                        ? std::min(job_.toPage(), pages_)
                                        : pages_);
    status_->setText(t);
    if (total_ > 0)
        bar_->setValue(static_cast<float>(job_.pagesPrinted()));
}

void PrintProgressWindow::postStep()
{
    const std::weak_ptr<int> liv = alive_;
    app().post([this, liv] {
        if (!liv.expired())
            step();
    });
}

void PrintProgressWindow::abortJob()
{
    if (done_)
        return;
    done_            = true;
    job_.cancelled_  = true;
    doc_->abort();
}

// ETT STEG — sidan `page_`: ritas, skickas om den ligger i intervallet, och
// nästa steg postas eller jobbet avslutas.
void PrintProgressWindow::step()
{
    if (done_)
        return;
    if (job_.cancelled_) {   // `cancel()` ur ritfunktionen
        abortJob();
        close();
        return;
    }
    const int  sida = page_;
    const int  från = job_.fromPage();
    const int  till = job_.toPage();
    const bool skickas = sida >= std::max(1, från) && (till <= 0 || sida <= till);

    std::vector<std::uint8_t> px;
    bool                      mer = true;
    if (!renderPage(sida, skickas, px, mer)) {
        abortJob();
        close();
        return;
    }
    if (skickas) {
        if (!doc_->addPage(px.data(), raster_.w, raster_.h)) {
            abortJob();
            close();
            return;
        }
        ++job_.printed_;
    }
    ++page_;
    if (job_.cancelled_) {
        abortJob();
        close();
        return;
    }
    const bool sista = !mer || (pages_ > 0 && sida >= pages_) ||
                       (till > 0 && sida >= till);
    if (sista) {
        done_ = true;
        if (job_.printed_ > 0) {
            succeeded_ = doc_->finish();
        } else {
            gfx::log("PrintJob::run: ingen sida i intervallet %d–%d — inget "
                     "skrivs ut",
                     från, till);
            doc_->abort();
        }
        close();
        return;
    }
    updateStatus();
    postStep();
}

bool PrintProgressWindow::renderPage(int page, bool read,
                                     std::vector<std::uint8_t>& rgba,
                                     bool& more)
{
    const Ctx c = makeCtx();
    if (!c.r || !c.canvas) {
        gfx::log("PrintJob::run: förloppsfönstret har ingen renderare — "
                 "sidan ritas inte");
        return false;
    }
    gfx::Renderer& r = *c.r;
    // Sidans Ctx: fönstrets resurser, tema och fonter, med målets täthet.
    Ctx sc     = c;
    sc.box     = gfx::Rectf{0.0f, 0.0f, static_cast<float>(raster_.w),
                            static_cast<float>(raster_.h)};
    sc.area    = sc.box;
    sc.scale   = static_cast<float>(raster_.pxPerMm * kMmPerInch / kLogicalPerInch);
    sc.pxPerMm = static_cast<float>(raster_.pxPerMm);

    for (int varv = 0;; ++varv) {
        // Varje varv börjar som en frame: släpp det döda, öppna budgeten.
        if (c.images)
            c.images->trim(app().canvasMemoryBudget(), app().canvasUploadBudget());
        if (!r.beginOffscreen())
            return false;   // inuti en frame — renderaren loggar orsaken
        const std::unique_ptr<gfx::RenderTarget> mål =
            r.createRenderTarget(raster_.w, raster_.h);
        if (!mål->image().valid()) {
            r.endOffscreen();
            gfx::log("PrintJob::run: sidans mål (%d × %d) gick inte att skapa",
                     raster_.w, raster_.h);
            return false;
        }
        r.pushTarget(*mål, gfx::Color{255, 255, 255, 255});
        detail::PaintContext f;
        f.c      = &sc;
        f.res    = c.canvas;
        f.world  = gfx::MapView{raster_.cx, raster_.cy, raster_.pxPerMm, 0.0,
                                0.0,        false,      sc.box};
        f.screen = detail::localView(sc.box, sc.scale);
        f.zoom   = raster_.pxPerMm / sc.scale;
        {
            // Målaren tömmer sin sista sats när den går ur räckvidd — innan
            // målet poppas.
            Painter p(f, false);
            more = draw_(page, p);
        }
        r.popTarget();
        r.endOffscreen();
        if (c.images && c.images->uploadsDeferred()) {
            if (varv + 1 < kMaxUploadRounds)
                continue;   // en bild väntade — sidan ritas om
            gfx::log("PrintJob::run: sidan %d har bilder som inte hann laddas "
                     "upp på %d varv — de saknas",
                     page, kMaxUploadRounds);
        }
        return !read || r.readTarget(*mål, rgba);
    }
}

// ---------------------------------------------------------------- jobbet

PrintJob::PrintJob()  = default;
PrintJob::~PrintJob() = default;

bool PrintJob::isSupported()
{
    return gfx::printAvailable();
}

void PrintJob::setPaper(PaperSize p)
{
    const auto träff = std::find_if(std::begin(kPapers), std::end(kPapers),
                                    [p](const PaperDims& d) { return d.kind == p; });
    if (träff == std::end(kPapers)) {
        gfx::log("PrintJob::setPaper: Custom är egna mått — setPaperSize(b, h)");
        return;
    }
    settings_.paperMm = gfx::Sizef{static_cast<float>(träff->w),
                                   static_cast<float>(träff->h)};
    reopenPrinter();
}

void PrintJob::setPaperSize(double widthMm, double heightMm)
{
    if (!(std::isfinite(widthMm) && std::isfinite(heightMm) && widthMm > 0.0 &&
          heightMm > 0.0)) {
        gfx::log("PrintJob::setPaperSize(%g, %g): måtten måste vara ändliga "
                 "och större än 0 — det gamla gäller",
                 widthMm, heightMm);
        return;
    }
    settings_.paperMm = gfx::Sizef{static_cast<float>(widthMm),
                                   static_cast<float>(heightMm)};
    reopenPrinter();
}

void PrintJob::setOrientation(PageOrientation o)
{
    settings_.orientation = o;
    reopenPrinter();
}

void PrintJob::reopenPrinter()
{
    settings_.native.reset();
    settings_.printableMm = gfx::Rectf{};
    settings_.dpiX        = 0.0f;
    settings_.dpiY        = 0.0f;
}

PaperSize PrintJob::paper() const
{
    for (const PaperDims& d : kPapers)
        if (std::fabs(d.w - settings_.paperMm.w) <= kPaperToleranceMm &&
            std::fabs(d.h - settings_.paperMm.h) <= kPaperToleranceMm)
            return d.kind;
    return PaperSize::Custom;
}

double PrintJob::paperWidth() const
{
    return settings_.orientation == PageOrientation::Landscape
               ? settings_.paperMm.h
               : settings_.paperMm.w;
}

double PrintJob::paperHeight() const
{
    return settings_.orientation == PageOrientation::Landscape
               ? settings_.paperMm.w
               : settings_.paperMm.h;
}

void PrintJob::setCopies(int n)
{
    if (n < 1) {
        gfx::log("PrintJob::setCopies(%d): minst en kopia — det gamla gäller", n);
        return;
    }
    settings_.copies = n;
    reopenPrinter();
}

void PrintJob::setPageRange(int from, int to)
{
    if (from < 0 || to < 0 || (to > 0 && from > to) || (from == 0) != (to == 0)) {
        gfx::log("PrintJob::setPageRange(%d, %d): ett intervall från 1, eller "
                 "0, 0 för alla — det gamla gäller",
                 from, to);
        return;
    }
    settings_.fromPage = from;
    settings_.toPage   = to;
}

void PrintJob::setResolution(int dpi)
{
    dpi_ = std::clamp(dpi, kMinDpi, kMaxDpi);
}

Rect PrintJob::printableArea() const
{
    const gfx::Rectf& p = settings_.printableMm;
    if (p.w > 0.0f && p.h > 0.0f)
        return Rect{p.x, p.y, p.w, p.h};
    return Rect{0.0, 0.0, paperWidth(), paperHeight()};   // ingen skrivare än
}

PageMargins PrintJob::margins() const
{
    const Rect pa = printableArea();
    return PageMargins{pa.x, pa.y, paperWidth() - (pa.x + pa.w),
                       paperHeight() - (pa.y + pa.h)};
}

bool PrintJob::showDialog(AppWindow& owner)
{
    if (!isSupported()) {
        logUnsupported("showDialog");
        return false;
    }
    if (running_) {
        gfx::log("PrintJob::showDialog: jobbet skriver ut — ingen dialog");
        return false;
    }
    if (!owner.isOpen()) {
        gfx::log("PrintJob::showDialog: ägaren '%s' är inte öppen",
                 owner.title().c_str());
        return false;
    }
    // En KOPIA: en avbruten dialog lämnar egenskaperna orörda.
    gfx::PrintSettings s = settings_;
    if (!gfx::showPrintDialog(&owner.window(), s))
        return false;
    settings_ = std::move(s);
    return true;
}

bool PrintJob::run(AppWindow& owner, int pages, PageFn draw)
{
    if (!isSupported()) {
        logUnsupported("run");
        return false;
    }
    if (running_) {
        gfx::log("PrintJob::run: jobbet skriver redan ut");
        return false;
    }
    if (!draw || pages < 0) {
        gfx::log("PrintJob::run: en ritfunktion och ett sidantal ≥ 0 krävs");
        return false;
    }
    if (!owner.isOpen()) {
        gfx::log("PrintJob::run: ägaren '%s' är inte öppen (renderaren är "
                 "fönstrets)",
                 owner.title().c_str());
        return false;
    }
    printed_   = 0;
    cancelled_ = false;
    // Utan en öppen skrivare: den senast valda (eller standardskrivaren) med
    // egenskaperna — backenden loggar.
    if (!settings_.native && !gfx::openPrinter(settings_))
        return false;
    const std::string& namn = title_.str().empty() ? owner.title() : title_.str();
    std::unique_ptr<gfx::PrintDocument> doc = gfx::beginPrintDocument(settings_, namn);
    if (!doc)
        return false;   // backenden loggar orsaken
    running_ = true;
    bool ok  = false;
    {
        PrintProgressWindow w(*this, std::move(doc), pages, std::move(draw));
        w.run(owner);
        ok = w.succeeded();
    }
    running_ = false;
    return ok;
}

void PrintJob::cancel()
{
    if (running_)
        cancelled_ = true;
}

} // namespace glaze
