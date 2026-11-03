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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <gfx/platform.h>
#include <gfx/renderer.h>

#include "canvas/camera_math.h"
#include "canvas/resources.h"
#include "canvas/scene.h"
#include "dialog_layout.h"
#include "glaze/app.h"
#include "glaze/file_dialog.h"
#include "glaze/strings.h"
#include "glaze/widgets/button.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/progress.h"
#include "image_cache.h"
#include "print_output.h"

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

// SIDANS RASTER — målets storlek och täthet för ett område i sidans mm. Den
// längsta sidan hålls inom `maxPx`, och `dpi` är den täthet som blev.
struct PageRaster {
    int    w = 0;
    int    h = 0;
    double dpi = 0.0;
    double pxPerMm = 0.0;
    double cx = 0.0;   // områdets mitt i sidans mm
    double cy = 0.0;
};

PageRaster pageRaster(const Rect& area, double dpi, int maxPx)
{
    const double längst = std::max(area.w, area.h) / kMmPerInch * dpi;
    if (längst > maxPx)
        dpi *= maxPx / längst;
    PageRaster r;
    r.dpi     = dpi;
    r.pxPerMm = dpi / kMmPerInch;
    r.w       = std::max(1, static_cast<int>(std::lround(area.w * r.pxPerMm)));
    r.h       = std::max(1, static_cast<int>(std::lround(area.h * r.pxPerMm)));
    r.cx      = area.x + area.w * 0.5;
    r.cy      = area.y + area.h * 0.5;
    return r;
}

struct SlutPåSida {
    CanvasResources& res;
    ~SlutPåSida() { res.setPageRender(false); }
};

// `running_` är sann så länge `run` pågår, hur det än lämnas.
struct RunningScope {
    bool& flag;
    explicit RunningScope(bool& f) : flag(f) { flag = true; }
    ~RunningScope() { flag = false; }
    RunningScope(const RunningScope&)            = delete;
    RunningScope& operator=(const RunningScope&) = delete;
};

// EN SIDA I ETT MÅL: vitt, med en målare i sidans mm, och om tills ingen bild
// väntar på uppladdningen. `read` = läs tillbaka bilden. false = ett fel (loggat);
// `complete` false = taket för varven nåddes och bilder saknas.
bool renderRaster(const Ctx& c, const PageRaster& raster,
                  const std::function<void(Painter&)>& draw, bool read,
                  std::vector<std::uint8_t>& rgba, bool& complete)
{
    gfx::Renderer& r = *c.r;
    // Sidans Ctx: fönstrets resurser, tema och fonter, med målets täthet.
    Ctx sc     = c;
    sc.box     = gfx::Rectf{0.0f, 0.0f, static_cast<float>(raster.w),
                            static_cast<float>(raster.h)};
    sc.area    = sc.box;
    sc.scale   = static_cast<float>(raster.pxPerMm * kMmPerInch / kLogicalPerInch);
    sc.pxPerMm = static_cast<float>(raster.pxPerMm);

    complete = true;
    for (int varv = 0;; ++varv) {
        if (!r.beginOffscreen())
            return false;   // inuti en frame — renderaren loggar orsaken
        // Varje varv börjar som en frame: släpp det döda, öppna budgeten. Sidor i
        // följd utan en fönsterframe emellan samlar då inga mål.
        if (c.images)
            c.images->trim(app().canvasMemoryBudget(), app().canvasUploadBudget());
        c.canvas->trim();
        c.canvas->setPageRender(true);
        const SlutPåSida slut{*c.canvas};
        const std::unique_ptr<gfx::RenderTarget> mål =
            r.createRenderTarget(raster.w, raster.h);
        if (!mål->image().valid()) {
            r.endOffscreen();
            gfx::log("Sidans mål (%d × %d) gick inte att skapa", raster.w, raster.h);
            return false;
        }
        r.pushTarget(*mål, gfx::Color{255, 255, 255, 255});
        detail::PaintContext f;
        f.c      = &sc;
        f.res    = c.canvas;
        f.world  = gfx::MapView{raster.cx, raster.cy, raster.pxPerMm, 0.0,
                                0.0,       false,     sc.box};
        f.screen = detail::localView(sc.box, sc.scale);
        f.zoom   = raster.pxPerMm / sc.scale;
        {
            // Målaren tömmer sin sista sats när den går ur räckvidd — innan
            // målet poppas.
            Painter p(f, false);
            draw(p);
        }
        r.popTarget();
        r.endOffscreen();
        if (c.images && c.images->uploadsDeferred()) {
            if (varv + 1 < kMaxUploadRounds)
                continue;   // en bild väntade — sidan ritas om
            complete = false;
        }
        return !read || r.readTarget(*mål, rgba);
    }
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
    // Har jobbet nått ett slut? false efter `run` = fönstret öppnades aldrig.
    bool ended() const { return done_; }

protected:
    // Språkbytet: titeln och knappen är `TrText`, statusraden byggs om.
    void onLanguageChanged() override
    {
        updateStatus();
        layoutAndSize();
    }

private:
    // Målets storlek och täthet, räknade en gång.
    PageRaster raster() const;

    void layoutAndSize();
    void updateStatus();
    void postStep();
    void step();
    void abortJob();
    void failJob();
    // Ritar sidan; `read` = läs tillbaka bilden. false = ett fel (loggat).
    bool renderPage(int page, bool read, std::vector<std::uint8_t>& rgba,
                    bool& more);

    PrintJob&                           job_;
    std::unique_ptr<gfx::PrintDocument> doc_;
    PrintJob::PageFn                    draw_;
    PageRaster                          raster_;
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

PageRaster PrintProgressWindow::raster() const
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
    const PageRaster r = pageRaster(pa, dpi, PrintJob::kMaxPagePx);
    if (r.dpi < dpi) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            gfx::log("PrintJob: sidan (%.0f × %.0f mm) rastreras i %.0f dpi i "
                     "stället för %.0f — bilden får vara högst %d punkter; "
                     "loggas en gång",
                     pa.w, pa.h, r.dpi, dpi, PrintJob::kMaxPagePx);
        }
    }
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
    done_ = true;
    job_.setResult(gfx::PrintJobResult::Cancelled);
    doc_->abort();
}

// Ett FEL mitt i jobbet: inget avbrott, så `isCancelled()` står kvar.
void PrintProgressWindow::failJob()
{
    done_ = true;
    job_.setResult(gfx::PrintJobResult::Failed);
    doc_->abort();
    close();
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
        failJob();
        return;
    }
    if (skickas) {
        if (!doc_->addPage(px.data(), raster_.w, raster_.h)) {
            failJob();
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
            const gfx::PrintJobResult r = doc_->finish();
            succeeded_                  = r == gfx::PrintJobResult::Ok;
            job_.setResult(r);
        } else {
            gfx::log("PrintJob::run: ingen sida i intervallet %d–%d — inget "
                     "skrivs ut",
                     från, till);
            doc_->abort();
            job_.outcome_ = PrintOutcome::NothingToPrint;
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
    bool komplett = true;
    const bool ok = renderRaster(
        c, raster_, [&](Painter& p) { more = draw_(page, p); }, read, rgba, komplett);
    if (!komplett)
        gfx::log("PrintJob::run: sidan %d har bilder som inte hann laddas "
                 "upp på %d varv — de saknas",
                 page, kMaxUploadRounds);
    return ok;
}

// --------------------------------------------------------------- sidan som bild

std::optional<PageImage> renderPageImage(AppWindow& window, double widthMm,
                                         double heightMm, double dpi,
                                         const std::function<void(Painter&)>& draw)
{
    if (!(std::isfinite(widthMm) && std::isfinite(heightMm) && std::isfinite(dpi) &&
          widthMm > 0.0 && heightMm > 0.0 && dpi > 0.0)) {
        gfx::log("renderPageImage(%g, %g, %g): måtten och tätheten måste vara "
                 "ändliga och större än 0 — ingen bild",
                 widthMm, heightMm, dpi);
        return std::nullopt;
    }
    if (!draw) {
        gfx::log("renderPageImage: ingen ritfunktion — ingen bild");
        return std::nullopt;
    }
    if (!window.isOpen()) {
        gfx::log("renderPageImage: fönstret är inte öppet (renderaren är "
                 "fönstrets) — ingen bild");
        return std::nullopt;
    }
    const Ctx c = window.root().behaviorCtx();
    if (!c.r || !c.canvas) {
        gfx::log("renderPageImage: fönstret har ingen renderare — ingen bild");
        return std::nullopt;
    }
    const PageRaster raster =
        pageRaster(Rect{0.0, 0.0, widthMm, heightMm}, dpi,
                   std::min(PrintJob::kMaxPagePx, c.r->maxTextureSize()));
    std::vector<std::uint8_t> px;
    bool                      komplett = true;
    if (!renderRaster(c, raster, draw, true, px, komplett))
        return std::nullopt;
    Bitmap bild(raster.w, raster.h, px.data(), 0);
    if (!bild.valid())
        return std::nullopt;   // Bitmap loggar orsaken
    return PageImage{std::move(bild), raster.dpi, komplett};
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

void PrintJob::setOutputFile(std::string utf8)
{
    fileFromDialog_ = false;
    fileSpent_      = false;
    if (utf8.empty()) {
        settings_.outputFile.clear();
        return;
    }
    std::error_code ec;
    const filedlg::fs::path abs = filedlg::fs::absolute(filedlg::pathFromUtf8(utf8), ec);
    if (ec) {
        gfx::log("PrintJob::setOutputFile: '%s' gick inte att göra absolut (%s) — "
                 "sökvägen står som den är",
                 utf8.c_str(), ec.message().c_str());
        settings_.outputFile = std::move(utf8);
        return;
    }
    settings_.outputFile = filedlg::utf8FromPath(abs);
}

void PrintJob::setPrinter(std::string name)
{
    settings_.printer = std::move(name);
    reopenPrinter();
    settings_.promptsForFile = false;
    settings_.outputExtension.clear();
}

bool PrintJob::askForFile(AppWindow& owner, const gfx::PrintSettings& s,
                          std::string& chosen)
{
    FileDialog dlg(FileDialogMode::Save);
    dlg.setFileName(printout::suggestedName(suggestedName_, title_.str(), owner.title(),
                                            s.outputExtension));
    if (!s.outputExtension.empty())
        dlg.addFilter(printout::extensionLabel(s.outputExtension),
                      "*" + s.outputExtension);
    if (dlg.run(owner) != DialogResult::OK)
        return false;
    chosen = dlg.path();
    return !chosen.empty();
}

void PrintJob::setResult(gfx::PrintJobResult r)
{
    outcome_ = printout::outcomeOf(r);
    if (const std::uint32_t id = printout::errorTextId(r); id != 0)
        error_ = tr(id);
    if (r == gfx::PrintJobResult::Cancelled)
        cancelled_ = true;
}

bool PrintJob::showDialog(AppWindow& owner)
{
    // Ett nästlat anrop nekas utan att röra det pågående jobbets utfall och besked.
    if (running_) {
        gfx::log("PrintJob::showDialog: jobbet skriver ut — ingen dialog");
        return false;
    }
    error_ = TrText{};
    // Utfallet och avbrottet följer varandra ur varje utgång härifrån.
    const auto slut = [this](PrintOutcome o) {
        outcome_   = o;
        cancelled_ = o == PrintOutcome::Cancelled;
        return o == PrintOutcome::Ok;
    };
    if (!isSupported()) {
        logUnsupported("showDialog");
        error_ = tr(str::PrintErrorUnsupported);
        return slut(PrintOutcome::Failed);
    }
    if (!owner.isOpen()) {
        gfx::log("PrintJob::showDialog: ägaren '%s' är inte öppen",
                 owner.title().c_str());
        error_ = tr(str::PrintErrorDialog);
        return slut(PrintOutcome::Failed);
    }
    // En KOPIA: en avbruten dialog, i någon av de två, lämnar egenskaperna orörda.
    gfx::PrintSettings s = settings_;
    switch (gfx::showPrintDialog(&owner.window(), s)) {
    case gfx::PrintDialogResult::Print:
        break;
    case gfx::PrintDialogResult::Cancelled:
        return slut(PrintOutcome::Cancelled);
    case gfx::PrintDialogResult::DialogFailed:
        error_ = tr(str::PrintErrorDialog);
        return slut(PrintOutcome::Failed);
    case gfx::PrintDialogResult::PrinterFailed:
        error_ = tr(str::PrintErrorPrinter);
        return slut(PrintOutcome::Failed);
    }
    s.outputFile = printout::keptOutputFile(s.outputFile, fileFromDialog_, s.promptsForFile);
    bool fromDialog = false;
    if (printout::needsFilePrompt(s.promptsForFile, s.outputFile, filePrompt_)) {
        // Filvalet pumpar: ett nästlat anrop nekas, som under ett jobb.
        const RunningScope running(running_);
        std::string        vald;
        if (!askForFile(owner, s, vald))
            return slut(PrintOutcome::Cancelled);
        s.outputFile = std::move(vald);
        fromDialog   = true;
    }
    settings_       = std::move(s);
    fileFromDialog_ = fromDialog;
    fileSpent_      = false;
    return slut(PrintOutcome::Ok);
}

bool PrintJob::run(AppWindow& owner, int pages, PageFn draw)
{
    // Ett nästlat anrop nekas utan att röra det pågående jobbets utfall och besked.
    if (running_) {
        gfx::log("PrintJob::run: jobbet skriver redan ut");
        return false;
    }
    error_     = TrText{};
    printed_   = 0;
    cancelled_ = false;
    if (!isSupported()) {
        logUnsupported("run");
        error_   = tr(str::PrintErrorUnsupported);
        outcome_ = PrintOutcome::Failed;
        return false;
    }
    if (!draw || pages < 0) {
        gfx::log("PrintJob::run: en ritfunktion och ett sidantal ≥ 0 krävs");
        error_   = tr(str::PrintErrorJob);
        outcome_ = PrintOutcome::Failed;
        return false;
    }
    if (!owner.isOpen()) {
        gfx::log("PrintJob::run: ägaren '%s' är inte öppen (renderaren är "
                 "fönstrets)",
                 owner.title().c_str());
        error_   = tr(str::PrintErrorJob);
        outcome_ = PrintOutcome::Failed;
        return false;
    }
    if (printout::emptyRange(pages, settings_.fromPage)) {
        gfx::log("PrintJob::run: intervallet börjar på sidan %d men dokumentet har %d "
                 "— inget skrivs ut",
                 settings_.fromPage, pages);
        outcome_ = PrintOutcome::NothingToPrint;
        return false;
    }
    // Jobbet går från här, också under vårt filval: ett nästlat anrop nekas och
    // `cancel()` gäller.
    const RunningScope running(running_);
    // Utan en öppen skrivare: den senast valda (eller standardskrivaren) med
    // egenskaperna — backenden loggar. FÖRE filvalet, för frågan om fil kommer
    // ur skrivaren.
    if (!settings_.native && !gfx::openPrinter(settings_)) {
        error_   = tr(str::PrintErrorPrinter);
        outcome_ = PrintOutcome::Failed;
        return false;
    }
    const std::string kept = printout::fileForRun(settings_.outputFile, fileFromDialog_,
                                                  fileSpent_, settings_.promptsForFile);
    if (kept != settings_.outputFile) {
        settings_.outputFile = kept;
        fileFromDialog_      = false;
    }
    if (printout::needsFilePrompt(settings_.promptsForFile, settings_.outputFile,
                                  filePrompt_)) {
        std::string vald;
        if (askForFile(owner, settings_, vald)) {
            settings_.outputFile = std::move(vald);
            fileFromDialog_      = true;
            fileSpent_           = false;
        } else {
            cancelled_ = true;
        }
    }
    if (cancelled_ || !owner.isOpen()) {   // Avbryt, `cancel()` eller en stängd ägare
        outcome_   = PrintOutcome::Cancelled;
        cancelled_ = true;
        return false;
    }
    const std::string namn  = title_.str().empty() ? owner.title() : title_.str();
    gfx::PrintStart   start = gfx::beginPrintDocument(settings_, namn);
    fileSpent_ = fileFromDialog_;   // en fil vald i vår dialog gäller det här jobbet
    if (start.result != gfx::PrintJobResult::Ok || !start.doc) {
        setResult(start.result == gfx::PrintJobResult::Ok ? gfx::PrintJobResult::Failed
                                                          : start.result);
        return false;
    }
    outcome_ = PrintOutcome::Failed;   // tills fönstret avslutat jobbet
    bool ok  = false;
    {
        PrintProgressWindow w(*this, std::move(start.doc), pages, std::move(draw));
        w.run(owner);
        ok = w.succeeded();
        if (!w.ended()) {
            error_   = tr(str::PrintErrorJob);
            outcome_ = PrintOutcome::Failed;
        }
    }
    return ok;
}

void PrintJob::cancel()
{
    if (running_)
        cancelled_ = true;
}

} // namespace glaze
