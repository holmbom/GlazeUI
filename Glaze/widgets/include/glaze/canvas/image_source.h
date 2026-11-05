#pragma once
#include <cstdint>
#include <string>
#include <string_view>

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

enum class StockIcon : std::uint16_t;   // glaze/stock_icon.h
enum class IconStyle : std::uint8_t;    // glaze/stock_icon.h

class ImageSource {
public:
    // `Icon` är en RITAD IKON (glaze/stock_icon.h) — en standardikon
    // (`stockIcon()`) eller appens egen (`drawnIcon()`, `drawnName()` inte
    // tom): en vektor som rastreras när den ritas, i fönstrets täthet och
    // temats färger.
    enum class Kind : std::uint8_t { None, Asset, Bitmap, Texture, Icon };

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
    StockIcon            stock() const { return stock_; }           // Kind::Icon
    const std::string&   drawnName() const { return drawn_; }       // Kind::Icon
    IconStyle            iconStyle() const { return iconStyle_; }   // Kind::Icon

    // Bilden för en kontroll i ett TILLSTÅND: en ikon i `IconStyle::Auto`
    // blir Color när `enabled` och Mono annars; allt annat svarar med sig
    // självt. `Draw::stateGlyph` frågar den.
    ImageSource forState(bool enabled) const;

    // Assetens storleksklass i LOGISKA px (`assetSizeClass`), ikonens klass
    // (`stockIcon`s och `drawnIcon`s `sizeClass`), och 0 för en Bitmap, en
    // RenderTexture och ingen bild — assets.h:s "0 = ospecificerad, widgeten
    // väljer fallback" (`Style::iconFallback`).
    float sizeClass() const;

    // Samma BILD (inte samma handtagsobjekt) — kopior av en Bitmap delar
    // pixlar och är samma bild. Versionen frågas INTE (se `==`).
    bool sameImage(const ImageSource& o) const;

    // VÄRDELIKHETEN — se filhuvudet.
    friend bool operator==(const ImageSource& a, const ImageSource& b);

private:
    ImageSource(StockIcon icon, std::string drawn, int sizeClass,
                IconStyle style);   // stockIcon(), drawnIcon()
    friend ImageSource stockIcon(StockIcon icon, int sizeClass, IconStyle style);
    friend ImageSource drawnIcon(std::string_view name, int sizeClass, IconStyle style);

    Kind          kind_ = Kind::None;
    AssetHandle   asset_{};
    Bitmap        bitmap_;
    RenderTexture texture_;
    std::uint64_t version_ = 0;   // Bitmap/RenderTexture: versionen vid skapandet
    StockIcon     stock_{};       // Icon: standardikonen, eller appens namn,
    std::string   drawn_;         // och storleksklassen och stilen
    int           iconSize_ = 0;
    IconStyle     iconStyle_{};
};

} // namespace glaze
