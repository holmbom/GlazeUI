#pragma once
#include <cstdint>

#include "glaze/assets.h"
#include "glaze/canvas/bitmap.h"
#include "glaze/canvas/render_texture.h"

// VAR EN BILD KOMMER IFRÅN — ritytans OCH widgetarnas bildvärde: appens assets
// (`glaze::img`), pixlar (`glaze::Bitmap`) eller en målning
// (`glaze::RenderTexture`). Konstruktorerna är implicita, så
// `Shape::image(r, platta)`, `MarkerStyle::img`, `MeshStyle::texture` och
// varje widget som visar en bild — `Button::setImg`, `Picture::setImg`,
// `MenuItem::icon`, trädets och tabellens rader, dragbilden, programikonen
// och systemfältet — tar vilken som helst av dem. `glaze::img(...)` svarar
// fortfarande med en `AssetHandle`, som är asset-delen av värdet och
// konverteras utan att anropsstället märker något. Texturen bor per fönster i
// bildcachen (budget, LRU) — källan är ett handtag.
//
// VÄRDELIKHETEN (`==`) är det setterns vakt (`if (img_ == h) return;`)
// frågar: två OGILTIGA värden är alltid lika (`{}` och `AssetHandle{}`),
// annars krävs samma bild (`sameImage`) i SAMMA VERSION. Versionen är ett
// SNAPSHOT av en Bitmaps eller RenderTextures version när värdet skapas, så
// `setImg(sammaBitmap)` efter `setPixels` blir en riktig ändring som
// invaliderar — och `AppIcon::setIcon`/`TrayIcon::setIcon` med samma bild i
// en ny version lägger om OS-ikonen, som annars inte hade haft någon väg att
// få de nya pixlarna. RITNINGEN läser alltid de LEVANDE pixlarna, så
// snapshotet påverkar aldrig vad som ritas; `sameImage` förblir identiteten
// (ritytans körningsnycklar).
//
// TRÅDARNA: ett `ImageSource` hör till UI-tråden, som widgetarna —
// konstruktorn läser bildens version. En arbetartråd lämnar en `Bitmap`,
// aldrig ett `ImageSource`.
//
// En egen header: formerna (shape.h) och rymdens
// stilar (style3d.h — `MeshStyle::texture`) bär båda en bildkälla, och
// shape.h drar in style3d.h. `glaze/widget.h` drar in den åt widgetarna.
// Implementationen står kvar i src/canvas/shape.cpp.

namespace glaze {

enum class StockIcon : std::uint16_t;       // glaze/stock_icon.h
enum class StockIconStyle : std::uint8_t;   // glaze/stock_icon.h

class ImageSource {
public:
    // `Stock` är en STANDARDIKON (glaze/stock_icon.h): en vektor som
    // rastreras när den ritas, i fönstrets täthet och temats textfärg —
    // `stockIcon()` är enda vägen dit.
    enum class Kind : std::uint8_t { None, Asset, Bitmap, Texture, Stock };

    ImageSource() = default;
    ImageSource(AssetHandle h);       // NOLINT — implicita med flit
    ImageSource(Bitmap b);            // NOLINT
    ImageSource(RenderTexture t);     // NOLINT

    Kind                 kind() const { return kind_; }
    bool                 valid() const;
    explicit operator bool() const { return valid(); }   // `AssetHandle`s par
    const AssetHandle&   asset() const { return asset_; }
    const Bitmap&        bitmap() const { return bitmap_; }
    const RenderTexture& texture() const { return texture_; }
    StockIcon            stock() const { return stock_; }             // Kind::Stock
    StockIconStyle       stockStyle() const { return stockStyle_; }   // Kind::Stock

    // Assetens storleksklass i LOGISKA px (`assetSizeClass`), standard-
    // ikonens klass (`stockIcon`s `sizeClass`), och 0 för en Bitmap, en
    // RenderTexture och ingen bild — assets.h:s "0 = ospecificerad, widgeten
    // väljer fallback" (`Style::iconFallback`).
    float sizeClass() const;

    // Samma BILD (inte samma handtagsobjekt) — kopior av en Bitmap delar
    // pixlar och är samma bild. Versionen frågas INTE (se `==`).
    bool sameImage(const ImageSource& o) const;

    // VÄRDELIKHETEN — se filhuvudet.
    friend bool operator==(const ImageSource& a, const ImageSource& b);

private:
    ImageSource(StockIcon icon, int sizeClass, StockIconStyle style);   // stockIcon()
    friend ImageSource stockIcon(StockIcon icon, int sizeClass, StockIconStyle style);

    Kind          kind_ = Kind::None;
    AssetHandle   asset_{};
    Bitmap        bitmap_;
    RenderTexture texture_;
    std::uint64_t version_ = 0;   // Bitmap/RenderTexture: versionen vid skapandet
    StockIcon     stock_{};       // Stock: ikonen och dess storleksklass
    int           stockSize_ = 0;
    StockIconStyle stockStyle_{};
};

} // namespace glaze
