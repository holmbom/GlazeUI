// FilePicker — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3), och här är
// "själv" ingenting: fältet och knappen är riktiga barn som ritar sig med sin
// familjs form. Det kompositen äger är PLACERINGEN (knappen i högerkanten,
// fältet resten), måttet (axelregeln) och FLÖDET — knappen öppnar
// filväljardialogen, och dess svar blir sökvägen. Måtten kommer ur stilen
// (`filePicker*`), inga tal och inga färger i den här filen.

#include "glaze/widgets/file_picker.h"

#include <algorithm>
#include <filesystem>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/strings.h"
#include "glaze/widgets/button.h"
#include "glaze/widgets/textinput.h"

#include "../file_dialog_model.h"   // sökvägarna i UTF-8 (pathFromUtf8 …)

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("filepicker", FilePicker)

FilePicker::FilePicker()
{
    field_  = add<TextInput>();
    button_ = add<Button>(tr(str::FilePickerBrowse));
    // LIVE, som fältets egen: varje ANVÄNDARändring av texten är en ändrad
    // sökväg. Programmatisk text (`setPath`) fyrar inte fältets onChange, så
    // ingen eko-loop.
    field_->onChange = [this] { fireEvent(onChange); };   // läses: path()
    // KLAR — fältets egen commit: Enter direkt, fokusförlusten
    // postad av fältet. Baslinjen är fältets; `setPath` flyttar den.
    field_->onCommit = [this] { fireEvent(onCommit); };
    button_->onClick = [this] { browse(); };
    syncToolTip();
}

FilePicker::~FilePicker() = default;

// Fältets egen commit — dess `onCommit` är kopplad vidare till vår.
void FilePicker::commitEdit()
{
    field_->commitEdit();
}

// ---------------------------------------------------------------- värdet

void FilePicker::setPath(std::string utf8)
{
    field_->setText(std::move(utf8));   // invaliderar själv vid ändring
}

const std::string& FilePicker::path() const
{
    return field_->text();
}

void FilePicker::selectAll()
{
    field_->selectAll();
}

void FilePicker::setSelection(int begin, int end)
{
    field_->setSelection(begin, end);
}

void FilePicker::setMode(FileDialogMode m)
{
    if (mode_ == m)
        return;
    mode_ = m;
    syncToolTip();
    invalidate();
}

void FilePicker::setDialogTitle(TrText t)
{
    if (dialogTitle_ == t)
        return;
    dialogTitle_ = std::move(t);
    syncToolTip();
}

void FilePicker::syncToolTip()
{
    // En SATT titel är dialogens, och knappen säger vad den öppnar.
    if (!dialogTitle_.empty()) {
        button_->setToolTip(dialogTitle_);
        return;
    }
    switch (mode_) {
    case FileDialogMode::Save:   button_->setToolTip(tr(str::FileSaveAs)); break;
    case FileDialogMode::Folder: button_->setToolTip(tr(str::FileSelectFolder)); break;
    case FileDialogMode::Open:
    default:                     button_->setToolTip(tr(str::FileOpen)); break;
    }
}

void FilePicker::addFilter(TrText rubrik, std::string patterns)
{
    filters_.push_back(FileFilter{std::move(rubrik), std::move(patterns)});
}

void FilePicker::setFilter(int index, TrText rubrik, std::string patterns)
{
    if (index < 0 || std::size_t(index) >= filters_.size()) {
        gfx::log("FilePicker(%s): setFilter(%d) utanför intervallet (%zu "
                 "filter) — ignoreras", name.c_str(), index, filters_.size());
        return;
    }
    filters_[std::size_t(index)] = FileFilter{std::move(rubrik), std::move(patterns)};
}

void FilePicker::setFilters(std::vector<FileFilter> filters)
{
    filters_     = std::move(filters);
    filterIndex_ = 0;   // det minda valet hörde till de gamla filtren
}

// ---------------------------------------------------------------- dialogen

bool FilePicker::browse()
{
    AppWindow* fönster = appWindow();
    if (!fönster || !fönster->isOpen()) {
        gfx::log("FilePicker(%s): browse() utan ett öppet fönster — ignoreras",
                 name.c_str());
        return false;
    }
    FileDialog dlg(mode_);
    if (!dialogTitle_.empty())
        dlg.setTitle(dialogTitle_);   // annars lägets titel
    dlg.setFilters(filters_);
    dlg.setFilterIndex(filterIndex_);
    dlg.setShowHidden(showHidden_);
    // Dialogen öppnar där sökvägen pekar: i mappläget mappen själv, annars
    // dess mapp med namnet förvalt. Finns den inte längre tar dialogen
    // närmaste förälder som gör det.
    const std::string nu = field_->text();
    if (!nu.empty()) {
        const std::filesystem::path p = filedlg::pathFromUtf8(nu);
        if (mode_ == FileDialogMode::Folder) {
            dlg.setFolder(nu);
        } else {
            dlg.setFolder(filedlg::utf8FromPath(p.parent_path()));
            dlg.setFileName(filedlg::utf8FromPath(p.filename()));
        }
    }

    // MODAL: pumpen går nästlat medan dialogen står öppen, och fältet kan
    // ha rivits, lossats eller förlorat sitt fönster när den återvänder (en
    // postad funktion stängde fönstret). Vakten (`Widget::HandlerGuard`)
    // avgör om `this` får röras igen.
    const HandlerGuard vakt(*this);
    const DialogResult svar = dlg.run(*fönster);
    if (!vakt.ok())
        return false;
    // Kryssrutan för dolda filer minns fältet OCKSÅ efter Avbryt — den är en
    // vyinställning, och dialogen skriver den när den kryssas.
    showHidden_ = dlg.showHidden();
    if (svar != DialogResult::OK)
        return false;

    // Ett VAL är en avslutad handling: `onChange` och `onCommit` tillsammans,
    // i den ordningen — och bara när sökvägen blev en annan.
    // `setText` flyttar fältets baslinje, så valet committas inte igen när
    // fokus därefter lämnar fältet.
    filterIndex_ = dlg.filterIndex();
    if (dlg.path() != field_->text()) {
        field_->setText(dlg.path());
        firePair(onChange, onCommit);   // vakten emellan
    }
    return true;
}

// ----------------------------------------------------------- kompositkrokarna

bool FilePicker::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(field_) &&
           &child != static_cast<const Widget*>(button_);
}

// `enabled` kaskadar inte i ramverket — men fältet och knappen är INREDNING,
// och ett inaktivt filväljarfält vars knapp öppnade dialogen hade ljugit.
void FilePicker::enabledChanged()
{
    if (isEnabled()) {
        field_->enable();
        button_->enable();
    } else {
        field_->disable();
        button_->disable();
    }
}

bool FilePicker::setProp(const std::string& key, const std::string& value)
{
    if (key == "path") {
        setPath(value);
        return true;
    }
    if (key == "mode") {
        if (value == "open")        setMode(FileDialogMode::Open);
        else if (value == "save")   setMode(FileDialogMode::Save);
        else if (value == "folder") setMode(FileDialogMode::Folder);
        else
            gfx::log("FilePicker(%s): okänt läge '%s' (ignoreras)",
                     name.c_str(), value.c_str());
        return true;   // nyckeln är känd — värdet kan vara fel
    }
    if (key == "dialogTitle") {
        setDialogTitle(value);   // en literal; tr-vägen är setTextToken
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

bool FilePicker::setTextToken(const std::string& key, const TrText& value)
{
    if (key == "dialogTitle") {
        setDialogTitle(value);
        return true;
    }
    return Widget::setTextToken(key, value);   // basens toolTip
}

// ------------------------------------------------------------ placeringen

// KNAPPEN i högerkanten, hela höjden och så bred som sin text (knappens egen
// autostorlek), FÄLTET resten minus luften. `c.area` är kompositens yta
// (barnens Ctx — se `Widget::placeChild`), i fysiska px.
gfx::Rectf FilePicker::placeChild(const Widget& child, const Ctx& c) const
{
    const gfx::Rectf a     = c.area;
    const float      knapp = std::min(measure(*button_, c).w * c.scale, a.w);
    const float      luft  = c.style->filePickerGap * c.scale;
    if (&child == button_)
        return gfx::Rectf{a.x + a.w - knapp, a.y, knapp, a.h};
    if (&child == field_)
        return gfx::Rectf{a.x, a.y, std::max(0.0f, a.w - knapp - luft), a.h};
    return gfx::Rectf{};   // ett tredje barn placeras inte
}

gfx::Sizef FilePicker::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner. AUTO-bredden är stilens
        // `filePickerDefaultW` (en sökväg är längre än ett ord), AUTO-höjden
        // är FÄLTETS — fältet ska stå i rad med ett vanligt textfält i varje
        // stil, och fältet vet sin höjd (talfältets recept).
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f) s.w = c.style->filePickerDefaultW;
        if (s.h <= 0.0f) s.h = measure(*field_, c).h;
        return s;
    }
    }
}

void FilePicker::draw(const Ctx& c)
{
    switch (c.ui) {
    // Kompositen har INGEN egen yta i någon familj, och det är ett beslut:
    // fältet och knappen är riktiga barn som ritar sig med sin familjs form
    // — Glazes laminerade knapp, Slates platta, Minimalistics kant — och en
    // yta bakom dem hade bara syntes i luften mellan dem. (pixel_gate har den
    // därför bland sina rena layoutwidgetar, med skälet utskrivet.)
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default:
        break;
    }
}

} // namespace glaze
