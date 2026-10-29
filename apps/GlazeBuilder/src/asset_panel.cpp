// Asset-läget — AssetDoc (fil ↔ minne) + AssetWindow (redigeringen).
// All filläsning/skrivning går via glaze_assets (samma kod som CLI:t —
// ingen andra sanning om formatet).

#include "asset_panel.h"

#include <filesystem>
#include <fstream>

#include <gfx/platform.h>

namespace fs = std::filesystem;

namespace bld {

bool AssetDoc::load(const std::string& startDir)
{
    loaded = false;
    lists.clear();
    entries.clear();
    path.clear();

    // Leta uppåt: <dir>/assets.bin, <dir>/../assets.bin, … (fyra nivåer).
    fs::path dir = startDir.empty() ? fs::current_path()
                                    : fs::path(startDir);
    for (int i = 0; i < 4; ++i) {
        const fs::path kandidat = dir / "assets.bin";
        std::error_code ec;
        if (fs::exists(kandidat, ec)) {
            path = kandidat.string();
            break;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir)
            break;
        dir = dir.parent_path();
    }
    if (path.empty()) {
        // Ingen fil är inget fel — nya assets sparas bredvid .h-filen.
        path = (fs::path(startDir) / "assets.bin").string();
        return false;
    }

    std::string err;
    glzassets::File f;
    if (!f.openFile(path, err)) {
        gfx::log("assets-läget: %s", err.c_str());
        return false;
    }
    // Kopiera in som packbara poster — bytes ägs av dokumentet.
    for (const glzassets::ListView& l : f.lists())
        lists.push_back({l.name, l.sizeClass});
    for (const glzassets::EntryView& e : f.entries()) {
        glzassets::PackEntry pe;
        pe.list     = f.lists()[e.listIndex].name;
        pe.key      = e.key;
        pe.origName = e.origName;
        pe.typeTag  = e.typeTag;
        pe.density  = e.density;
        pe.bytes.assign(f.data(e), f.data(e) + e.dataSize);
        entries.push_back(std::move(pe));
    }
    loaded = true;
    return true;
}

bool AssetDoc::save(std::string& err) const
{
    return glzassets::pack(lists, entries, path, err);   // atomär
}

std::vector<std::string> AssetDoc::allKeys() const
{
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const auto& e : entries)
        out.push_back(e.list + "/" + e.key);
    return out;
}

} // namespace bld

// ------------------------------------------------------------- fönstret

namespace {
constexpr float kRadH = 30.0f;   // radpoolens steg (logiska px)
}

AssetWindow::AssetWindow(bld::AssetDoc& doc, std::function<void()> onChanged)
    : glaze::AppWindow("Assets", 560, 520),
      doc_(doc),
      onChanged_(std::move(onChanged))
{
    rubrik_    = add<glaze::Label>("");
    rubrik_->x = 16;
    rubrik_->y = 12;

    rader_ = add<glaze::Panel>();
    rader_->x       = 16;
    rader_->y       = 40;
    rader_->width   = 528;
    rader_->height  = 280;
    rader_->anchors = {true, true, true, false};
    rader_->right   = 16;
    rader_->setBorder(true);
    rader_->setClip(true);
    rader_->showScrollbars(true);

    // Formuläret: ny lista + lägg till PNG (via sökväg) + export + spara.
    auto* l1 = add<glaze::Label>("Ny lista (namn · storleksklass):");
    l1->x = 16;  l1->y = 336;
    nyListaNamn_  = add<glaze::TextInput>();
    nyListaNamn_->x = 16;   nyListaNamn_->y = 358;  nyListaNamn_->width = 180;
    nyListaKlass_ = add<glaze::TextInput>("16");
    nyListaKlass_->x = 204; nyListaKlass_->y = 358; nyListaKlass_->width = 48;
    auto* nyBtn = add<glaze::Button>("Ny lista");
    nyBtn->x = 260;  nyBtn->y = 357;  nyBtn->width = 90;  nyBtn->height = 28;
    nyBtn->onClick = [this](glaze::Widget*, glaze::ClickArgs&) { addList(); };

    auto* l2 = add<glaze::Label>("Lägg till PNG (sökväg · nyckel · lista):");
    l2->x = 16;  l2->y = 394;
    pngSokvag_ = add<glaze::TextInput>();
    pngSokvag_->x = 16;   pngSokvag_->y = 416;  pngSokvag_->width = 236;
    pngNyckel_ = add<glaze::TextInput>();
    pngNyckel_->x = 260;  pngNyckel_->y = 416;  pngNyckel_->width = 90;
    pngLista_  = add<glaze::TextInput>();
    pngLista_->x = 358;   pngLista_->y = 416;   pngLista_->width = 90;
    auto* addBtn = add<glaze::Button>("Lägg till");
    addBtn->x = 456;  addBtn->y = 415;  addBtn->width = 88;  addBtn->height = 28;
    addBtn->onClick = [this](glaze::Widget*, glaze::ClickArgs&) { addPng(); };

    auto* expBtn = add<glaze::Button>("Exportera alla");
    expBtn->x = 16;  expBtn->y = 452;  expBtn->width = 130;  expBtn->height = 30;
    expBtn->onClick = [this](glaze::Widget*, glaze::ClickArgs&) {
        exportAll();
    };
    auto* sparaBtn = add<glaze::Button>("Spara");
    sparaBtn->x = 154;  sparaBtn->y = 452;  sparaBtn->width = 90;
    sparaBtn->height = 30;
    sparaBtn->onClick = [this](glaze::Widget*, glaze::ClickArgs&) {
        saveDoc();
    };

    status_    = add<glaze::Label>("");
    status_->x = 16;
    status_->anchors = {true, false, false, true};
    status_->bottom  = 8;
    status_->setRole(glaze::Label::Role::Dim);

    rebuild();
}

void AssetWindow::rebuild()
{
    rubrik_->setText(doc_.path + (doc_.loaded ? "" : " (ny fil)"));

    // Poolen: växer vid behov, krymper aldrig — överskott göms (trädet
    // saknar barn-borttagning med flit; se headern).
    while (pool_.size() < doc_.entries.size()) {
        Rad r;
        const float y = 8.0f + float(pool_.size()) * kRadH;
        r.text = rader_->add<glaze::Label>("");
        r.text->x = 12;
        r.text->y = y + 5.0f;
        r.exp = rader_->add<glaze::Button>("→ png");
        r.exp->x = 380;  r.exp->y = y;  r.exp->width = 60;
        r.exp->height = 24;
        r.bort = rader_->add<glaze::Button>("Ta bort");
        r.bort->x = 448;  r.bort->y = y;  r.bort->width = 64;
        r.bort->height = 24;
        pool_.push_back(r);
    }
    for (std::size_t i = 0; i < pool_.size(); ++i) {
        Rad& r = pool_[i];
        if (i >= doc_.entries.size()) {
            r.text->hide();
            r.exp->hide();
            r.bort->hide();
            continue;
        }
        const glzassets::PackEntry& e = doc_.entries[i];
        r.text->setText(
            e.list + "/" + e.key + " — " +
            std::to_string(e.bytes.size()) + " B" +
            (e.density == glzassets::kDensity2x ? " @2x" : ""));
        r.text->show();
        r.exp->show();
        r.bort->show();
        // Bind om per rebuild — indexet fångas per värde och följer
        // dokumentets aktuella ordning.
        r.exp->onClick = [this, i](glaze::Widget*, glaze::ClickArgs&) {
            exportOne(i);
        };
        r.bort->onClick = [this, i](glaze::Widget*, glaze::ClickArgs&) {
            removeOne(i);
        };
    }
    invalidate();
}

std::string AssetWindow::exportDir() const
{
    return (fs::path(doc_.path).parent_path() / "assets_export").string();
}

void AssetWindow::exportAll()
{
    std::error_code ec;
    fs::create_directories(exportDir(), ec);
    int n = 0;
    for (std::size_t i = 0; i < doc_.entries.size(); ++i, ++n)
        exportOne(i);
    status_->setText("Exporterade " + std::to_string(n) + " till " + exportDir());
    invalidate();
}

void AssetWindow::exportOne(std::size_t i)
{
    const glzassets::PackEntry& e = doc_.entries[i];
    std::error_code ec;
    fs::create_directories(exportDir(), ec);
    // Byte-identisk export per konstruktion: bytes ÄR källfilen.
    const std::string ut = exportDir() + "/" + e.origName;
    std::ofstream o(ut, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(e.bytes.data()),
            std::streamsize(e.bytes.size()));
    status_->setText(o.good() ? "Exporterade " + ut : "kan inte skriva " + ut);
    invalidate();
}

void AssetWindow::removeOne(std::size_t i)
{
    status_->setText("Tog bort " + doc_.entries[i].list + "/" +
                     doc_.entries[i].key + " (Spara för att verkställa)");
    doc_.entries.erase(doc_.entries.begin() + std::ptrdiff_t(i));
    rebuild();
    if (onChanged_)
        onChanged_();
}

void AssetWindow::addList()
{
    if (nyListaNamn_->text().empty()) {
        status_->setText("listan behöver ett namn");
        invalidate();
        return;
    }
    for (const auto& l : doc_.lists)
        if (l.name == nyListaNamn_->text()) {
            status_->setText("listan finns redan");
            invalidate();
            return;
        }
    doc_.lists.push_back(
        {nyListaNamn_->text(),
         std::uint16_t(std::atoi(nyListaKlass_->text().c_str()))});
    status_->setText("Ny lista '" + nyListaNamn_->text() +
                     "' (Spara för att verkställa)");
    rebuild();
}

void AssetWindow::addPng()
{
    const std::string& p = pngSokvag_->text();
    if (p.empty() || pngNyckel_->text().empty() || pngLista_->text().empty()) {
        status_->setText("fyll i sökväg, nyckel och lista");
        invalidate();
        return;
    }
    bool listFinns = false;
    for (const auto& l : doc_.lists)
        listFinns |= l.name == pngLista_->text();
    if (!listFinns) {
        status_->setText("okänd lista '" + pngLista_->text() + "'");
        invalidate();
        return;
    }
    for (const auto& e : doc_.entries)
        if (e.list == pngLista_->text() && e.key == pngNyckel_->text()) {
            status_->setText("nyckeln finns redan i listan");
            invalidate();
            return;
        }
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        status_->setText("kan inte läsa " + p);
        invalidate();
        return;
    }
    glzassets::PackEntry e;
    e.bytes.assign(std::istreambuf_iterator<char>(in), {});
    e.list     = pngLista_->text();
    e.key      = pngNyckel_->text();
    e.origName = fs::path(p).filename().string();
    e.typeTag  = glzassets::kTypeImage;
    e.density  = glzassets::kDensity2x;   // @2x-källor är husets standard
                                          // (skarpt på Retina)
    doc_.entries.push_back(std::move(e));
    status_->setText("La till " + pngLista_->text() + "/" +
                     pngNyckel_->text() + " (Spara för att verkställa)");
    rebuild();
    if (onChanged_)
        onChanged_();
}

void AssetWindow::saveDoc()
{
    std::string err;
    if (doc_.save(err)) {
        doc_.loaded   = true;
        status_->setText("Sparade " + doc_.path);
    } else {
        status_->setText("SPARFEL: " + err);
    }
    invalidate();
}
