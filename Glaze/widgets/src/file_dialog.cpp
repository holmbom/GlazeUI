// Filväljardialogen — se glaze/file_dialog.h för API:t och besluten, och
// file_dialog_window.h för varför fönstret står i en intern header.
//
// Dialogen är ett VANLIGT modalt AppWindow byggt av vanliga widgets, precis
// som meddelanderutan: adressfältet och namnfältet är `TextInput`, platserna
// en `ListBox`, filerna en `ListView` (sorteringen, kolumnhuvudet, flervalet
// och aktiveringen är dess), delaren en `Splitter` och filtypslistan en
// `Dropdown`, och kryssrutan för dolda filer en `Checkbox`. Det som är NYTT
// här är LAYOUTEN, FLÖDET (navigering, OK-logiken per läge,
// överskrivningsfrågan) och fem små ättlingar till befintliga widgets. Vad
// dialogen VET bor i den rena std-modellen (file_dialog_model.h) och grindas
// headless.
//
// FYRDELAT som allt annat: måtten ur `Style` (`fileDlg*`, `fileCol*`,
// `iconStroke`), färgerna ur temat genom widgetarna och ikonerna ur
// `Draw::icon` — inga tal och inga färger i den här filen.

#include "file_dialog_window.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <gfx/events.h>

#include "glaze/draw.h"
#include "glaze/message_box.h"
#include "glaze/strings.h"
#include "glaze/widgets/button.h"
#include "glaze/widgets/checkbox.h"
#include "glaze/widgets/dropdown.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/list_view.h"
#include "glaze/widgets/listbox.h"
#include "glaze/widgets/splitter.h"
#include "glaze/widgets/textinput.h"

namespace glaze {

namespace fs = std::filesystem;
using filedlg::Entry;
using filedlg::ListError;

// ========================================================== de fem ättlingarna
//
// Alla fem lyder regel 3 fullt ut trots att de är interna: mått ur Style,
// färger ur temat, ritning genom Draw.

// FILLISTAN — en ListView som RITAR sina ikoner: mappen och bladet i radens
// textfärg (`Draw::icon`), så ikonen byter färg med en vald rad precis som
// texten. Tabellens ikonkrokar (`iconSide`/`drawIcon`) är hela tillägget.
class FileList final : public ListView {
public:
    // Parallell med raderna (appens index) — sätts i samma svep som dem.
    void setFolders(std::vector<bool> f)
    {
        folders_ = std::move(f);
        invalidate();
    }

protected:
    // En uttryckligt satt `setIconSize` gäller också här:
    // ritningen är fortfarande fillistans egen.
    float iconSide(const Ctx& c, int row) const override
    {
        return row >= 0 && std::size_t(row) < folders_.size()
                   ? (iconSize() > 0.0f ? iconSize() : c.style->iconFallback)
                   : 0.0f;
    }
    void drawIcon(const Ctx& c, const gfx::Rectf& box, int row,
                  gfx::Color fg) const override
    {
        Draw::icon(c, box, folders_[std::size_t(row)] ? Icon::Folder : Icon::File,
                   Pen{fg, c.style->iconStroke});
    }

private:
    std::vector<bool> folders_;
};

// PLATSLISTAN — en ListBox vars rad är en ikon och ett namn. Radens INNEHÅLL
// är den virtuella `drawRow` (menyradernas väg); markeringen, scrollen och
// radloopen är listans. Namnet kortas med ellips, för rutan kan dras smal.
class PlacesList final : public ListBox {
public:
    void setIcons(std::vector<Icon> i)
    {
        icons_ = std::move(i);
        invalidate();
    }

protected:
    // INGET HÖGERTRYCKSVAL: ett val i platslistan
    // NAVIGERAR, och ett högerklick på en plats ska inte göra det.
    bool rightDown(const Ctx&, float, float) override { return false; }

    void drawRow(const Ctx& c, int row, gfx::Color fg, gfx::Color) const override
    {
        const gfx::Rectf r    = rowRect(c, row);
        const float      sida = c.style->iconFallback;
        const float      x    = textX(c, row);
        if (std::size_t(row) < icons_.size())
            Draw::icon(c, {x, r.y + (r.h - sida) / 2.0f, sida, sida},
                       icons_[std::size_t(row)], Pen{fg, c.style->iconStroke});
        const float tx = x + sida + c.style->iconGap;
        Draw::textElided(c, {tx, r.y, r.x + r.w - tx - c.style->listPadX, r.h},
                         items()[std::size_t(row)], fg);
    }

private:
    std::vector<Icon> icons_;
};

// EN KNAPP MED EN RITAD IKON i stället för text (uppåtknappen) — plattan,
// tillstånden, fokusringen och Space/Enter är knappens egna. Rutan är
// KVADRATISK och lika hög som fältet bredvid (`buddy`), mätt ur det och inte
// räknad på nytt: en sanning om höjden.
class IconButton final : public Button {
public:
    IconButton(Icon i, const Widget& buddy) : icon_(i), buddy_(buddy) {}

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            gfx::Sizef s{width, height};
            if (s.h <= 0.0f) s.h = measure(buddy_, c).h;
            if (s.w <= 0.0f) s.w = s.h;
            return s;
        }
        }
    }

    void draw(const Ctx& c) override
    {
        Button::draw(c);   // plattan, kanten och fokusringen — knappens egna
        const float      sida = c.style->iconFallback;
        const gfx::Rectf b    = c.bounds();
        const gfx::Rectf ruta{b.x + (b.w - sida) / 2.0f,
                              b.y + (b.h - sida) / 2.0f, sida, sida};
        switch (c.ui) {
        // Ikonen ritas LIKADANT i alla tre familjerna, och det är ett
        // beslut: den är en GLYF, som dropdownens pil och kryssets bock, i
        // knappens textfärg — det som skiljer familjerna är plattan runt
        // den, och den ritar `Button::draw` ovan. Färgen är knappens EGEN
        // textfärg (`labelColor`), så pilen syns också när knappen har fokus
        // och därmed ritas som standard på accentytan.
        case UI::Glaze:
        case UI::Minimalistic:
        case UI::Slate:
        default:
            Draw::icon(c, ruta, icon_, Pen{labelColor(c), c.style->iconStroke});
            break;
        }
    }

private:
    Icon          icon_;
    const Widget& buddy_;
};

// EN KONTROLL SOM STÅR MITT FÖR SIN GRANNE: lika hög som `buddy` (namnfältet,
// OK-knappen, adressfältet), så att innehållet — som Label och Checkbox
// alltid v-centrerar — hamnar i höjd med grannens. Höjden MÄTS ur grannen,
// aldrig en kopia av dess formel. En MALL över basen, för regeln är densamma
// för etiketterna och kryssrutan för dolda filer; bara basen skiljer, och
// båda tar sin text som ett `TrText`.
template <class Base>
class Buddy final : public Base {
public:
    Buddy(TrText t, const Widget& buddy) : Base(std::move(t)), buddy_(buddy) {}

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            gfx::Sizef s = Base::contentSize(c);
            if (this->height <= 0.0f)
                s.h = Widget::measure(buddy_, c).h;
            return s;
        }
        }
    }

private:
    const Widget& buddy_;
};

// OK OCH AVBRYT — en Button med dialogens minsta bredd (`fileDlgBtnMinW`),
// så "OK" aldrig blir en prick och de två knapparna inte hoppar i bredd
// mellan språken. Mäts vid varje fråga, så ett språkbyte breddar knappen
// utan att dialogen behöver göra om sin layout (meddelanderutans omväg).
class DialogButton final : public Button {
public:
    explicit DialogButton(TrText t) : Button(std::move(t)) {}

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            gfx::Sizef s = Button::contentSize(c);
            if (width <= 0.0f)
                s.w = std::max(s.w, c.style->fileDlgBtnMinW);
            return s;
        }
        }
    }
};

// ================================================================= hjälpare
namespace {

// Platsens ikon — volymen är en låda, resten säger sig själva.
Icon iconFor(gfx::PlaceKind k)
{
    switch (k) {
    case gfx::PlaceKind::Home:      return Icon::House;
    case gfx::PlaceKind::Desktop:   return Icon::Monitor;
    case gfx::PlaceKind::Documents: return Icon::Document;
    case gfx::PlaceKind::Downloads: return Icon::Download;
    case gfx::PlaceKind::Pictures:  return Icon::Picture;
    case gfx::PlaceKind::Volume:
    default:                        return Icon::Drive;
    }
}

// Platsens NAMN — användarmapparna i APPENS språk, volymerna med OS:ets
// etikett.
std::string nameFor(const gfx::Place& p)
{
    std::uint32_t id = 0;
    switch (p.kind) {
    case gfx::PlaceKind::Home:      id = str::PlaceHome;      break;
    case gfx::PlaceKind::Desktop:   id = str::PlaceDesktop;   break;
    case gfx::PlaceKind::Documents: id = str::PlaceDocuments; break;
    case gfx::PlaceKind::Downloads: id = str::PlaceDownloads; break;
    case gfx::PlaceKind::Pictures:  id = str::PlacePictures;  break;
    case gfx::PlaceKind::Volume:
    default:                        return p.label;
    }
    const TrText t = tr(id);
    return t.str();
}

// Lägets egen titel när appen inte satt någon.
TrText titleFor(const FileDialog& cfg)
{
    if (!cfg.title().empty())
        return cfg.titleToken();
    switch (cfg.mode()) {
    case FileDialogMode::Save:   return tr(str::FileSaveAs);
    case FileDialogMode::Folder: return tr(str::FileSelectFolder);
    case FileDialogMode::Open:
    default:                     return tr(str::FileOpen);
    }
}

TrText okTextFor(FileDialogMode m)
{
    switch (m) {
    case FileDialogMode::Save:   return tr(str::FileSave);
    case FileDialogMode::Folder: return tr(str::FileSelectFolder);
    case FileDialogMode::Open:
    default:                     return tr(str::FileOpen);
    }
}

std::string upperAscii(std::string s)
{
    for (char& ch : s)
        if (ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');
    return s;
}

bool isDirectory(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

bool pathExists(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p, ec);
}

} // namespace

// ================================================================== fönstret

FileDialogWindow::FileDialogWindow(FileDialog& cfg)
    : AppWindow(titleFor(cfg)), cfg_(cfg), mode_(cfg.mode())
{
    setModal(true);
    // Mitt över ägaren, som meddelanderutan — utan en
    // synlig ägare mitt på skärmen.
    setStartPosition(StartPosition::CenterOwner);
    setMinimizeBox(false);
    setMaximizeBox(false);
    // Ingen programikon i dialogens namnlist — som
    // meddelanderutan; OS-ikonen står kvar.
    appIcon.hide();

    const Style& m        = style();
    const bool   mappläge = mode_ == FileDialogMode::Folder;

    // STORLEKEN i INNEHÅLLSYTAN, och minimimåttet räknat genom chromet:
    // `setMinimumSize` talar KLIENTSTORLEK (meddelanderutans fälla), och
    // skillnaden mellan de två läsarna är exakt namnlistens höjd.
    setContentSize(static_cast<int>(std::ceil(m.fileDlgW)),
                   static_cast<int>(std::ceil(m.fileDlgH)));
    const float chrome = clientSize().h - contentSize().h;
    setMinimumSize(static_cast<int>(std::ceil(m.fileDlgMinW)),
                   static_cast<int>(std::ceil(m.fileDlgMinH + chrome)));

    // ---- DELARNA, i en ADOPTIONSORDNING som låter varje syskonankare peka
    // BAKÅT (kontraktet i widget.h). Tabbordningen är en annan och sätts
    // därför som VÄRDEN (`tabIndex`) i den ordning ögat läser.
    auto path  = std::make_unique<TextInput>();
    auto up    = std::make_unique<IconButton>(Icon::ArrowUp, *path);
    auto dolda = std::make_unique<Buddy<Checkbox>>(tr(str::FileShowHidden), *path);
    up_        = static_cast<Button*>(root().adopt(std::move(up)));
    hidden_    = static_cast<Checkbox*>(root().adopt(std::move(dolda)));
    path_      = static_cast<TextInput*>(root().adopt(std::move(path)));
    cancel_    = add<DialogButton>(tr(str::Cancel));
    ok_        = add<DialogButton>(okTextFor(mode_));
    message_   = add<Buddy<Label>>(TrText{}, *ok_);
    filter_    = add<Dropdown>();
    auto namn  = std::make_unique<TextInput>(cfg.fileName());
    nameLabel_ = static_cast<Label*>(root().adopt(std::make_unique<Buddy<Label>>(
        mappläge ? tr(str::FileFolderLabel) : tr(str::FileNameLabel), *namn)));
    name_      = static_cast<TextInput*>(root().adopt(std::move(namn)));
    split_     = add<Splitter>();
    placeList_ = split_->add<PlacesList>();
    files_     = split_->add<FileList>();

    up_->name        = "up";
    path_->name      = "path";
    hidden_->name    = "hidden";
    cancel_->name    = "cancel";
    ok_->name        = "ok";
    message_->name   = "message";
    filter_->name    = "filter";
    nameLabel_->name = "nameLabel";
    name_->name      = "name";
    split_->name     = "split";
    placeList_->name = "places";
    files_->name     = "files";

    // Överst: uppåtknappen, adressfältet och kryssrutan för dolda filer i
    // högerkanten. Rutan är en VYINSTÄLLNING och står därför vid det den
    // styr — mappen och listan under — och inte i svarsraden nederst, där
    // meddelanderaden behöver bredden åt sina hela meningar. Adressfältet
    // sträcks mellan knappen och rutan.
    up_->x             = m.fileDlgPad;
    up_->y             = m.fileDlgPad;
    up_->setToolTip(tr(str::FileUpOneLevel));
    hidden_->anchors   = {false, true, true, false};
    hidden_->right     = m.fileDlgPad;
    hidden_->y         = m.fileDlgPad;
    path_->y           = m.fileDlgPad;
    path_->anchorLeft  = {"up", m.fileDlgGap};
    path_->anchorRight = {"hidden", m.fileDlgGap};

    // Nederst: Avbryt i hörnet, OK till vänster om den, meddelanderaden
    // sträckt från vänsterkanten fram till OK.
    cancel_->anchors = {false, false, true, true};
    cancel_->right   = m.fileDlgPad;
    cancel_->bottom  = m.fileDlgPad;
    ok_->anchors     = {false, false, false, true};
    ok_->anchorRight = {"cancel", m.fileDlgGap};
    ok_->bottom      = m.fileDlgPad;
    message_->anchors     = {true, false, false, true};
    message_->x           = m.fileDlgPad;
    message_->bottom      = m.fileDlgPad;
    message_->anchorRight = {"ok", m.fileDlgGap};

    // Raden ovanför: etiketten, namnfältet och filtypslistan. Överkanten
    // släpps på alla tre (förvalet binder den), annars hade den bundna
    // UNDERkanten sträckt dem upp till fönstrets topp.
    filter_->anchors      = {false, false, true, false};
    filter_->right        = m.fileDlgPad;
    filter_->width        = m.fileDlgFilterW;
    filter_->anchorBottom = {"ok", m.fileDlgGap};
    filter_->setToolTip(tr(str::FileTypeLabel));
    nameLabel_->anchors      = {true, false, false, false};
    nameLabel_->x            = m.fileDlgPad;
    nameLabel_->anchorBottom = {"ok", m.fileDlgGap};
    name_->anchors      = {false, false, false, false};
    name_->anchorLeft   = {"nameLabel", m.fileDlgGap};
    name_->anchorBottom = {"ok", m.fileDlgGap};
    if (mappläge) {
        // Mappläget har inga filter: namnfältet når högerkanten (ett dolt
        // ankarmål hade lämnat ett hål — syskonankarets kontrakt).
        filter_->hide();
        name_->anchors.right = true;
        name_->right         = m.fileDlgPad;
    } else {
        name_->anchorRight = {"filter", m.fileDlgGap};
    }

    // Mitten: delaren mellan adressraden och namnraden, platserna fasta.
    split_->x            = m.fileDlgPad;
    split_->right        = m.fileDlgPad;
    split_->anchors      = {true, false, true, false};
    split_->anchorTop    = {"path", m.fileDlgGap};
    split_->anchorBottom = {"name", m.fileDlgGap};
    split_->setPaneSize(m.fileDlgPlacesW);
    split_->setFirstMin(m.fileDlgPlacesMin);
    split_->setSecondMin(m.fileDlgListMin);

    files_->addColumn(tr(str::FileColName), m.fileColNameW);
    files_->addColumn(tr(str::FileColSize), m.fileColSizeW, Align::Right);
    files_->addColumn(tr(str::FileColModified), m.fileColDateW);
    files_->addColumn(tr(str::FileColType), m.fileColTypeW);
    files_->setSortColumn(filedlg::ColName);
    files_->setMultiSelect(cfg.isMultiSelect() &&
                           mode_ == FileDialogMode::Open);

    // TABBORDNINGEN som ögat läser: överst, mitten, nederst.
    up_->tabIndex        = 0;
    path_->tabIndex      = 1;
    hidden_->tabIndex    = 2;
    placeList_->tabIndex = 3;
    files_->tabIndex     = 4;
    name_->tabIndex      = 5;
    filter_->tabIndex    = 6;
    ok_->tabIndex        = 7;
    cancel_->tabIndex    = 8;

    up_->onClick        = [this] { goUp(); };
    hidden_->onChange   = [this] { hiddenToggled(); };
    ok_->onClick        = [this] { accept(); };
    // STANDARD- OCH AVBRYTKNAPPEN — Enter och Escape går
    // fönstrets väg, som i varje dialog: Enter i namnfältet committar och
    // bubblar till OK, och en öppen filtypslista tar sitt eget Escape före
    // steget. OK VALIDERAR och bär därför inget resultat (`accept` stänger
    // själv); Avbryt svarar med sitt.
    cancel_->setDialogResult(DialogResult::Cancel);
    setAcceptButton(*ok_);
    setCancelButton(*cancel_);
    placeList_->onChange = [this] { placeChosen(); };
    files_->onChange    = [this] { selectionChanged(); };
    files_->onActivate  = [this] { activated(); };
    filter_->onChange   = [this] { filterChanged(); };

    // ---- INNEHÅLLET. Filtren (utan filter gäller "Alla filer") och det
    // valda, platserna, och startmappen.
    if (!mappläge) {
        filters_ = cfg.filters();
        if (filters_.empty())
            filters_.push_back(FileFilter{tr(str::FileAllFiles), "*.*"});
        const int valt = std::clamp(cfg.filterIndex(), 0,
                                    static_cast<int>(filters_.size()) - 1);
        fillFilters();
        filter_->setSelectedIndex(valt);
        patterns_ = filedlg::splitPatterns(filters_[std::size_t(valt)].patterns);
    }
    places_ = gfx::knownPlaces();
    fillPlaces();
    // DOLDA FILER som appen bad om. Krysset ÄR vyns tillstånd — listan läser
    // det — så det sätts före den första läsningen nedan.
    hidden_->setChecked(cfg.showHidden());

    // STARTMAPPEN: appens — en relativ läst mot arbetsmappen, med samma
    // tolkning som adressfältet (en UNC-sökväg är absolut) — annars
    // dokumenten, annars hemmet. Finns den inte längre, dess närmaste
    // förälder som gör det.
    std::error_code ec;
    fs::path        start;
    if (!cfg.folder().empty())
        start = filedlg::resolvePath(fs::current_path(ec),
                                     filedlg::pathFromUtf8(cfg.folder()));
    if (start.empty()) {
        for (gfx::PlaceKind k : {gfx::PlaceKind::Documents, gfx::PlaceKind::Home})
            for (const gfx::Place& p : places_)
                if (start.empty() && p.kind == k)
                    start = filedlg::pathFromUtf8(p.path);
    }
    fs::path finns = filedlg::nearestExisting(start);
    if (finns.empty())
        finns = fs::current_path(ec);
    navigate(finns);

    // Fokus när fönstret öppnats (`focus()` kräver ett öppet fönster):
    // namnfältet i fillägena — där skriver man — och listan i mappläget.
    onLoad = [this] {
        if (mode_ == FileDialogMode::Folder)
            files_->focus();
        else
            name_->focus();
    };
}

FileDialogWindow::~FileDialogWindow() = default;

// ---------------------------------------------------------------- innehållet

void FileDialogWindow::fillPlaces()
{
    std::vector<std::string> namn;
    std::vector<Icon>        ikoner;
    for (const gfx::Place& p : places_) {
        namn.push_back(nameFor(p));
        ikoner.push_back(iconFor(p.kind));
    }
    placeList_->setItems(std::move(namn));
    placeList_->setIcons(std::move(ikoner));
}

void FileDialogWindow::fillFilters()
{
    std::vector<std::string> rader;
    for (const FileFilter& f : filters_)
        rader.push_back(f.name.str() + " (" + f.patterns + ")");
    filter_->setItems(std::move(rader));
}

std::string FileDialogWindow::typeText(const Entry& e) const
{
    if (e.folder) {
        const TrText t = tr(str::FileFolderType);
        return t.str();
    }
    const std::string ext = filedlg::extensionOf(e.name);
    if (ext.empty()) {
        const TrText t = tr(str::FileTypeGeneric);
        return t.str();
    }
    const TrText suffix = tr(str::FileTypeSuffix);
    return upperAscii(ext) + suffix.str();
}

void FileDialogWindow::navigate(const fs::path& p, const std::string& selectName)
{
    cur_ = filedlg::withoutTrailingSeparator(p);
    path_->setText(filedlg::utf8FromPath(cur_));
    files_->setScroll(0.0f, 0.0f);   // en ny mapp börjar överst
    fillList(selectName);
    syncPlaces();
    if (filedlg::isRootFolder(cur_))
        up_->disable();
    else
        up_->enable();
}

// LÄS MAPPEN och fyll listan: raderna, ikonerna och de fyra
// sorteringsnycklarna. `selectName` väljer en rad efteråt (mappen man kom
// ifrån efter ett steg uppåt). Ett fel står i meddelanderaden.
void FileDialogWindow::fillList(const std::string& selectName)
{
    // Det OS:et döljer (Windows attribut, macOS flagga) döljer vi också —
    // `gfx::isHiddenFile`, ovanpå modellens egen punktregel — utom när
    // kryssrutan för dolda filer säger att allt ska synas.
    listing_ = filedlg::listFolder(cur_, mode_ == FileDialogMode::Folder,
                                   patterns_, &gfx::isHiddenFile,
                                   hidden_->isChecked());
    const TrText sep  = tr(str::DecimalSeparator);
    const TrText byte = tr(str::FileSizeUnit);

    std::vector<Table::Row>  rader;
    std::vector<bool>        mappar;
    std::vector<std::string> typer;
    rader.reserve(listing_.entries.size());
    for (const Entry& e : listing_.entries) {
        typer.push_back(typeText(e));
        rader.push_back(Table::Row{
            {e.name,
             e.hasSize ? filedlg::sizeText(e.size, sep.str(), byte.str()) : "",
             e.hasTime ? filedlg::dateText(e.modified) : "", typer.back()},
            {}});
        mappar.push_back(e.folder);
    }
    files_->setRows(std::move(rader));
    files_->setFolders(std::move(mappar));
    const auto nycklar = filedlg::sortKeys(listing_.entries, typer);
    for (std::size_t r = 0; r < nycklar.size(); ++r)
        for (std::size_t k = 0; k < nycklar[r].size(); ++k)
            files_->setSortKey(static_cast<int>(r), static_cast<int>(k),
                               nycklar[r][k]);

    switch (listing_.error) {
    case ListError::NoAccess:   showError(str::FileErrNoAccess); break;
    case ListError::Missing:    showError(str::FileErrFolderMissing); break;
    case ListError::Unreadable: showError(str::FileErrUnreadable); break;
    case ListError::None:
    default:                    showError(0); break;
    }

    if (!selectName.empty()) {
        for (std::size_t r = 0; r < listing_.entries.size(); ++r) {
            if (listing_.entries[r].name == selectName) {
                files_->setSelectedIndex(static_cast<int>(r));
                files_->ensureVisible(static_cast<int>(r));
                break;
            }
        }
    }
}

// LÄS OM mappen med den aktuella raden kvar vald — F5, språkbytet och
// kryssrutan för dolda filer. En rad som inte finns kvar (borttagen, eller
// dold igen) lämnar listan utan val.
void FileDialogWindow::refresh()
{
    const int r = files_->currentRow();
    fillList(r >= 0 ? listing_.entries[std::size_t(r)].name : std::string());
}

// Den plats som ÄR mappen som visas markeras; ingen annars.
void FileDialogWindow::syncPlaces()
{
    int träff = -1;
    for (std::size_t i = 0; i < places_.size() && träff < 0; ++i)
        if (filedlg::withoutTrailingSeparator(
                filedlg::pathFromUtf8(places_[i].path)) == cur_)
            träff = static_cast<int>(i);
    placeList_->setSelectedIndex(träff);
}

// 0 = släck raden.
void FileDialogWindow::showError(std::uint32_t textId)
{
    message_->setText(textId != 0 ? tr(textId) : TrText{});
}

// ------------------------------------------------------------------- flödet

void FileDialogWindow::goUp()
{
    if (filedlg::isRootFolder(cur_))
        return;
    // Mappen man kom ifrån blir vald — Utforskarens beteende, och det som
    // gör att en andra Backspace + Enter tar en tillbaka.
    const std::string från = filedlg::utf8FromPath(cur_.filename());
    navigate(filedlg::parentFolder(cur_), från);
}

void FileDialogWindow::placeChosen()
{
    const int i = placeList_->selectedIndex();
    if (i >= 0 && std::size_t(i) < places_.size())
        navigate(filedlg::pathFromUtf8(places_[std::size_t(i)].path));
}

void FileDialogWindow::filterChanged()
{
    const int i = filter_->selectedIndex();
    if (i < 0 || std::size_t(i) >= filters_.size())
        return;
    patterns_ = filedlg::splitPatterns(filters_[std::size_t(i)].patterns);
    fillList(std::string());
}

// Kryssrutan för DOLDA FILER: valet gäller DIREKT — mappen läses om — och
// skrivs tillbaka till inställningarna med en gång och inte först vid OK,
// för det är en vyinställning och inte en del av svaret (file_dialog.h).
void FileDialogWindow::hiddenToggled()
{
    cfg_.setShowHidden(hidden_->isChecked());
    refresh();
}

// Användaren valde i listan: de valda FILERNAS namn står i namnfältet
// (citerade vid flera — Utforskarens form), i mappläget den valda mappens.
// Ett val av bara mappar lämnar ett inskrivet filnamn orört.
void FileDialogWindow::selectionChanged()
{
    std::vector<std::string> filer, mappar;
    for (int r : files_->selectedRows()) {
        const Entry& e = listing_.entries[std::size_t(r)];
        (e.folder ? mappar : filer).push_back(e.name);
    }
    if (mode_ == FileDialogMode::Folder) {
        if (mappar.size() == 1)
            name_->setText(mappar.front());
    } else if (!filer.empty()) {
        name_->setText(filedlg::joinNames(filer));
    }
}

// Dubbelklick eller Enter i listan: en mapp ÖPPNAS, en fil är ett svar.
void FileDialogWindow::activated()
{
    const int r = files_->currentRow();
    if (r < 0 || std::size_t(r) >= listing_.entries.size())
        return;
    const Entry& e = listing_.entries[std::size_t(r)];
    if (e.folder) {
        navigate(cur_ / filedlg::pathFromUtf8(e.name));
        return;
    }
    // Trycket som startade aktiveringen har redan valt raden, och valet har
    // skrivit namnfältet (`selectionChanged`) — OK-vägen tar det därifrån.
    accept();
}

// Enter i ADRESSFÄLTET: en mapp öppnas, allt annat är ett fel och texten
// står kvar så att den kan rättas.
void FileDialogWindow::pathEntered()
{
    const fs::path p = filedlg::resolveTyped(cur_, path_->text());
    if (isDirectory(p)) {
        navigate(p);
        return;
    }
    showError(str::FileErrFolderMissing);
}

// OK — per läge. Ett namn som är en MAPP öppnar mappen i stället för att
// svara (alla tre lägena, som Utforskarens dialoger); allt annat prövas mot
// filsystemet INNAN dialogen stänger, och ett fel står i meddelanderaden.
void FileDialogWindow::accept()
{
    std::vector<std::string> namn = filedlg::splitNames(name_->text());

    if (mode_ == FileDialogMode::Folder) {
        if (namn.size() == 1) {
            const fs::path p = filedlg::resolveTyped(cur_, namn.front());
            if (isDirectory(p))
                finish({p});
            else
                showError(str::FileErrFolderMissing);
            return;
        }
        // Inget namn: den VALDA mappen, annars mappen som visas.
        const int r = files_->selectedIndex();
        if (r >= 0 && listing_.entries[std::size_t(r)].folder)
            finish({cur_ / filedlg::pathFromUtf8(
                               listing_.entries[std::size_t(r)].name)});
        else
            finish({cur_});
        return;
    }

    if (namn.empty()) {
        // Tomt fält men en vald MAPP: öppna den (OK på en mapp går in i den).
        const int r = files_->selectedIndex();
        if (r >= 0 && listing_.entries[std::size_t(r)].folder)
            navigate(cur_ / filedlg::pathFromUtf8(
                                listing_.entries[std::size_t(r)].name));
        return;
    }
    if (namn.size() == 1) {
        const fs::path p = filedlg::resolveTyped(cur_, namn.front());
        if (isDirectory(p)) {
            navigate(p);
            name_->setText(std::string());
            return;
        }
    }

    if (mode_ == FileDialogMode::Open) {
        if (namn.size() > 1 && !files_->isMultiSelect()) {
            showError(str::FileErrFileMissing);
            return;
        }
        std::vector<fs::path> svar;
        for (const std::string& n : namn) {
            const fs::path p = filedlg::resolveTyped(cur_, n);
            std::error_code ec;
            if (!fs::is_regular_file(p, ec)) {
                showError(str::FileErrFileMissing);
                return;
            }
            svar.push_back(p);
        }
        finish(svar);
        return;
    }

    // SPARA: ett namn, med filtrets ändelse om det saknar en egen, i en mapp
    // som finns — och en fil som redan finns ger frågan.
    if (namn.size() != 1) {
        showError(str::FileErrFileMissing);
        return;
    }
    // Ändelsen prövas på det SISTA namnet och inte på hela texten — en
    // inskriven "v1.2/rapport" har ingen ändelse, fast texten har en punkt.
    const fs::path    inskriven = filedlg::resolveTyped(cur_, namn.front());
    const std::string filnamn   = filedlg::withExtension(
        filedlg::utf8FromPath(inskriven.filename()),
        filedlg::defaultExtension(patterns_));
    const fs::path p = inskriven.parent_path() / filedlg::pathFromUtf8(filnamn);
    if (isDirectory(p)) {
        // Namnet MED ändelsen är en mapp ("arkiv" + ".pdf") — den öppnas, som
        // varje namn som är en mapp, i stället för att bli ett sparnamn.
        navigate(p);
        name_->setText(std::string());
        return;
    }
    if (!isDirectory(p.parent_path())) {
        showError(str::FileErrFolderMissing);
        return;
    }
    if (pathExists(p) && cfg_.overwritePrompt()) {
        const TrText fråga = tr(str::FileReplaceQuestion);
        const DialogResult svar =
            messageBox(*this, filedlg::utf8FromPath(p.filename()) + "\n" + fråga.str(),
                       title(), MessageBoxButtons::YesNo,
                       MessageBoxType::Warning);
        if (svar != DialogResult::Yes) {
            name_->focus();
            return;
        }
    }
    finish({p});
}

// SVARET: inställningsobjektet får resultatet och mappen användaren slutade
// i, sedan stängs dialogen med OK — `FileDialog::run` läser det ur
// `dialogResult()` (`accepted()`).
void FileDialogWindow::finish(const std::vector<fs::path>& paths)
{
    cfg_.paths_.clear();
    for (const fs::path& p : paths)
        cfg_.paths_.push_back(filedlg::utf8FromPath(p));
    cfg_.folder_ = filedlg::utf8FromPath(cur_);
    if (!paths.empty())
        cfg_.fileName_ = filedlg::utf8FromPath(paths.front().filename());
    if (mode_ != FileDialogMode::Folder)
        cfg_.filterIndex_ = filter_->selectedIndex();
    closeWithResult(DialogResult::OK);
}

// ----------------------------------------------------------------- tangenterna

bool FileDialogWindow::event(const gfx::Event& e)
{
    if (e.type != gfx::EventType::KeyDown)
        return false;
    // Den VIKTA tangenten — numpadens navigationsläge ska gå uppåt som
    // piltangenten (event-kroken ser den oviktade, se app_window.h).
    const gfx::Key k        = gfx::navigationKey(e.key);
    const bool     textfält = name_->focused() || path_->focused();

    // ENTER I ADRESSFÄLTET är fältets egen handling — "gå dit" — och inte OK,
    // så den tas här, före fönstrets standardknapp. Escape och namnfältets
    // Enter är fönstrets avbryt- och standardknapp
    // (konstruktorn).
    if (k == gfx::Key::Return && e.mods == 0 && path_->focused()) {
        pathEntered();
        return true;
    }
    if ((k == gfx::Key::Backspace && e.mods == 0 && !textfält) ||
        (k == gfx::Key::Up && e.mods == gfx::kModAlt)) {
        goUp();
        return true;
    }
    if (k == gfx::Key::F5 && e.mods == 0) {
        refresh();
        return true;
    }
    return false;
}

void FileDialogWindow::onLanguageChanged()
{
    fillPlaces();
    syncPlaces();
    if (mode_ != FileDialogMode::Folder) {
        const int valt = filter_->selectedIndex();
        fillFilters();
        filter_->setSelectedIndex(valt);
    }
    refresh();
}

// ===================================================================== API:t

FileDialog::FileDialog(FileDialogMode mode) : mode_(mode) {}

void FileDialog::addFilter(TrText name, std::string patterns)
{
    filters_.push_back(FileFilter{std::move(name), std::move(patterns)});
}

void FileDialog::setFilter(int index, TrText name, std::string patterns)
{
    if (index < 0 || std::size_t(index) >= filters_.size()) {
        gfx::log("FileDialog: setFilter(%d) utanför intervallet (%zu filter) "
                 "— ignoreras", index, filters_.size());
        return;
    }
    filters_[std::size_t(index)] = FileFilter{std::move(name), std::move(patterns)};
}

void FileDialog::setFilters(std::vector<FileFilter> filters)
{
    filters_ = std::move(filters);
}

const std::string& FileDialog::path() const
{
    static const std::string tom;
    return paths_.empty() ? tom : paths_.front();
}

// `None` LÄCKER ALDRIG UT: allt som inte är ett OK — Avbryt, Escape,
// krysset, ägarens kaskadstängning — är Cancel.
DialogResult FileDialog::run(AppWindow& owner)
{
    paths_.clear();
    FileDialogWindow w(*this);
    w.run(owner);
    return w.accepted() ? DialogResult::OK : DialogResult::Cancel;
}

DialogResult FileDialog::run()
{
    paths_.clear();
    FileDialogWindow w(*this);
    w.run();
    return w.accepted() ? DialogResult::OK : DialogResult::Cancel;
}

} // namespace glaze
