#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Bild-fasaden: en Image skapas av sin Renderer (loadImage — PNG, se
// `decodeImage` — eller createImage ur pixlar) och ritas
// med Renderer::drawImage eller scenens Image-program. Samma livstidsregler
// som Font: texturen hör till renderern — släpp bilden INNAN fönstret/
// renderern rivs (fönster som stängs städar sin renderer; en Image som
// överlever den får inte röras mer).
//
// Mått i FYSISKA px (bildens pixlar är fysiska — DPI-skala appliceras av
// anroparen på dst-recten vid ritning).

namespace gfx {

// PIXELFORMATET. Rgba8 är fyra byte per pixel i ordningen
// R, G, B, A med RAK alfa — samma form som loadImage, decodeImage och
// encodePng talar. R8 är EN kanal per pixel: data för scenens program
// (SDF-atlasen, etikettsynligheten); ritad som bild är den GRÅSKALA,
// helt täckande.
enum class ImageFormat : std::uint8_t { Rgba8, R8 };

// Byte per pixel i formatet.
constexpr std::size_t bytesPerPixel(ImageFormat f)
{
    return f == ImageFormat::R8 ? 1u : 4u;
}

// Samplingen när en bild ritas SKALAD: LINJÄR som
// förval — kartplattor och foton ska inte pixla sig — och Nearest per bild
// för det som ska förbli kantigt (pixelkonst, en förstorad heatmap).
// Filtret är BATCH-TILLSTÅND hos backenden: det läses vid varje ritning, så
// `setFilter` mellan två ritningar gäller den andra. Text och fönstrets
// komposit samplar alltid Nearest — de ritas 1:1 och binder det själva.
//
// MED EN MIP-KEDJA (`createImage(…, mipmaps)`, `TargetDesc::
// mipmaps`) gäller filtret också MELLAN nivåerna: Linear är MJUKT
// (trilinjärt — linjärt i nivån och mellan de två närmaste nivåerna), Nearest
// är SKARPT (närmaste pixel i närmaste nivå), så pixlarna syns inzoomat men
// en utzoomad bild ger ingen moiré. En bild utan kedja har bara nivå 0.
enum class ImageFilter : std::uint8_t { Linear, Nearest };

class Image {
public:
    virtual ~Image() = default;

    virtual int  width() const  = 0;
    virtual int  height() const = 0;

    // Laddades bilden? Fabrikerna returnerar ALLTID ett objekt (som
    // loadFont) — ogiltig bild har mått 0 och drawImage ritar inget.
    virtual bool valid() const = 0;

    virtual ImageFormat format() const = 0;

    // Bär texturen PREMULTIPLICERAD färg? Sant för ett render-måls bild:
    // varje mål bär premultiplicerat innehåll, för
    // rak-alfa-blandning mot en genomskinlig yta ger exakt det. Ritvägarna
    // (drawImage, Image-programmet) av-premultiplicerar då, annars får
    // kanterna en mörk frans över ljus bakgrund. Bilder ur pixlar är raka.
    virtual bool premultiplied() const = 0;

    // Ligger raderna NERIFRÅN och upp i texturen? Sant för GL:s render-mål:
    // en FBO ritas med y uppåt, så bildens rad 0 hamnar överst i texturen
    // (v = 1). Ritvägarna speglar v när den är sann. D3D11 och Metal har
    // rad 0 överst i alla texturer och svarar alltid falskt.
    virtual bool flippedV() const = 0;

    ImageFilter filter() const { return filter_; }
    void        setFilter(ImageFilter f) { filter_ = f; }

private:
    ImageFilter filter_ = ImageFilter::Linear;
};

// MÅLETS FORM — det `Renderer::createRenderTarget` bygger.
// Förvalet är 2D-målet: ett prov per pixel och inget djup.
//   samples  MSAA-provtagningen: 1, 2, 4 eller 8. Ett annat tal avvisas (logg,
//            ogiltigt mål). Backenden tar den närmaste STÖDDA ≤ det begärda
//            (`Renderer::supportedSamples` — llvmpipe har 4 som tak) och loggar
//            en gång när den går lägre; `RenderTarget::samples` säger vad det
//            blev.
//   depth    ett DJUP, D32F i alla tre backends (Metal på Apple silicon saknar
//            Depth24Stencil8, och biasens enhet blir densamma överallt).
//            Standard-Z: rensat till 1 (det bortre planet) och närmare är
//            mindre. Det rensas med färgen, i `pushTarget` och `clear`.
//   mipmaps  en MIP-KEDJA på bilden: nivåerna räknas om ur nivå 0
//            varje gång målet LÄMNAS — samma tillfälle som MSAA-upplösningen
//            — så en bild som ritas mindre än sin storlek inte ger moiré.
struct TargetDesc {
    int  samples = 1;
    bool depth   = false;
    bool mipmaps = false;
};

// RENDER-MÅLET — en yta att RITA i, skapad av sin
// Renderer (`createRenderTarget`) och bunden med `pushTarget`/`popTarget`.
// Det som ritats läses som en vanlig bild: `image()` går till drawImage och
// till scenens Image-program. Innehållet är PREMULTIPLICERAT (se
// `Image::premultiplied`). Samma livstidsregel som bilder: släpp målet före
// fönstret.
//
// MED MSAA ritas i en provtagen yta, och `image()` är den
// UPPLÖSTA bilden: backenden löser upp (resolve) varje gång målet LÄMNAS —
// i `popTarget` och när ett annat mål binds ovanpå det — så bilden stämmer i
// samma frame som målet poppas och kan ritas i det nästlade målet.
class RenderTarget {
public:
    virtual ~RenderTarget() = default;

    // Målets bild — fysiska px, giltig när målet gick att skapa. Alltid ett
    // prov per pixel, också för ett MSAA-mål (se ovan).
    virtual Image& image() = 0;

    // Provtagningen målet FAKTISKT fick: 1 = ingen MSAA. Kan vara lägre än
    // `TargetDesc::samples` (se där). Ett ogiltigt mål svarar 1.
    virtual int samples() const = 0;

    // Har målet ett djup (D32F)? Ett ogiltigt mål svarar false.
    virtual bool hasDepth() const = 0;
};

// PNG-encode: skriv rå RGBA8 (rad 0 överst, täta rader =
// w × 4 byte, rak alfa) till en PNG-fil. Encode-spegeln av loadImage —
// implementeras per backend med samma OS-komponenter/libbar som
// avkodningen (WIC / Image I/O / libpng), ingen ny dependency.
// false + logg vid fel (ogiltiga mått, oskrivbar sökväg, I/O).
bool encodePng(const std::string& path, const std::vector<std::uint8_t>& rgba,
               int w, int h);

// AVKODNING UTAN RENDERER: PNG-data → rå RGBA8, rad 0
// överst, täta rader, RAK alfa — samma form som encodePng tar.
// TRÅDSÄKER: rör ingen renderer och inget fönster, så en arbetartråd kan
// avkoda plattor och lämna pixlarna till UI-tråden (`glaze::app().post`).
//
// BARA PNG, PÅ ALLA PLATTFORMAR. De åtta
// första byten prövas mot PNG-signaturen INNAN plattformens avkodare får
// datat, så JPEG, BMP och GIF svarar false + logg också där WIC och Image I/O
// hade klarat dem. Skälet är ett kontrakt lika överallt: annars avkodar
// Windows och macOS JPEG medan Linux (libpng) bara tar PNG, och en app kunde
// råka luta sig mot JPEG på Windows och få fel på Linux. Annan bildavkodning
// är appens — pixlarna kommer in via `createImage` (glaze::Bitmap).
// `Renderer::loadImage` går genom samma prov. false + logg vid fel; utdata
// orörda då.
bool decodeImage(const void* data, std::size_t size,
                 std::vector<std::uint8_t>& rgba, int& w, int& h);

} // namespace gfx
