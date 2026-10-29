#pragma once
#include <functional>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <glaze_assets/assets_file.h>

// Asset-läget: den redigerade appens assets.bin i
// minnet (AssetDoc) + fönstret som redigerar den (AssetWindow). Filen ÄR
// källan — export ut är därför en förstklassig operation, och
// sparningen är ATOMÄR (glzassets::pack skriver temp + rename).
//
// Fönstret komponerar BARA ramverkswidgets (alla widgets bor
// i glaze) — radlistan är en återanvänd widget-POOL i en scrollpanel
// (trädet saknar barn-borttagning med flit; poolen växer men krymper
// aldrig, rader utöver innehållet göms).

namespace bld {

// Den inlästa filen som packbara poster — bytes kopieras in i minnet så
// dokumentet kan muteras fritt och packas om (samma kod som CLI:t).
struct AssetDoc {
    std::string path;      // hittad/tänkt assets.bin ("" = ingen hittad)
    bool        loaded = false;
    std::vector<glzassets::PackList>  lists;
    std::vector<glzassets::PackEntry> entries;

    // Leta uppåt från startkatalogen (kataloger, max fyra nivåer) efter
    // assets.bin och läs in den. Ingen fil är INTE ett fel — bild-
    // väljaren blir bara tom (dokumenterat beteende).
    bool load(const std::string& startDir);

    bool save(std::string& err) const;   // pack → path (atomär)

    // Alla nycklar som "lista/nyckel" — PropertyGridens choices för
    // img-fältet.
    std::vector<std::string> allKeys() const;
};

} // namespace bld

// Asset-fönstret: listar poster (lista/nyckel, storlek, density) med
// export/ta bort per rad; ny lista + lägg-till-PNG via textfält (ramverkets
// filväljardialog, `glaze::FileDialog`, används inte här); exportera alla;
// spara.
class AssetWindow : public glaze::AppWindow {
public:
    AssetWindow(bld::AssetDoc& doc, std::function<void()> onChanged);

private:
    void rebuild();                       // radpoolen + rubriken
    void exportAll();
    void exportOne(std::size_t i);
    void removeOne(std::size_t i);
    void addList();
    void addPng();
    void saveDoc();
    std::string exportDir() const;

    bld::AssetDoc&        doc_;
    std::function<void()> onChanged_;     // buildern uppdaterar choices

    glaze::Label* rubrik_ = nullptr;
    glaze::Label* status_ = nullptr;
    glaze::Panel* rader_  = nullptr;      // scrollpanelen med radpoolen

    struct Rad {                          // poolens beståndsdelar
        glaze::Label*  text = nullptr;
        glaze::Button* exp  = nullptr;
        glaze::Button* bort = nullptr;
    };
    std::vector<Rad> pool_;

    glaze::TextInput* nyListaNamn_  = nullptr;
    glaze::TextInput* nyListaKlass_ = nullptr;
    glaze::TextInput* pngSokvag_    = nullptr;
    glaze::TextInput* pngNyckel_    = nullptr;
    glaze::TextInput* pngLista_     = nullptr;
};
