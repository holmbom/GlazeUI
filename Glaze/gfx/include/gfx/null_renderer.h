#pragma once
// NullRenderer / NullFont / NullImage — fasadens TOMMA implementationer
//
// Varje ren virtuell i `Renderer`, `Font`, `Image`, `GpuBuffer` och
// `ShaderProgram` har här en no-op som
// håller fasadens kontrakt: fabrikerna returnerar ALLTID ett objekt (ogiltigt
// — `valid()` är falskt), rit-anropen ritar ingenting och klippet tillämpas
// inte. Klassen finns för grindarnas test-renderare och testfonter: de ärver
// härifrån och överskuggar BARA det de spelar in. Utan den kostar varje ny
// ren virtuell en stubbe i varje testfil; med den kostar den de tre backendarna
// plus EN rad här.
//
// INGEN produktionskod ritar med den — en app får alltid sin backends
// renderer av fönstret (Linux-proxyn svarar med en NullRenderer bara när det
// inte finns något fönster att rita i: före init och efter shutdown).
// Header-only med flit: den är fri från backend-typer
// och ska kunna inkluderas av en grind som bara länkar fasaden.

#include <memory>
#include <string>

#include "gfx/renderer.h"

namespace gfx {

// En bild utan pixlar: mått 0 och ogiltig — exakt det en misslyckad
// avkodning lämnar efter sig, så `ImageCache` behandlar den som en negativ
// post.
class NullImage : public Image {
public:
    int  width() const override { return 0; }
    int  height() const override { return 0; }
    bool valid() const override { return false; }
    ImageFormat format() const override { return ImageFormat::Rgba8; }
    bool premultiplied() const override { return false; }
    bool flippedV() const override { return false; }
};

// En font vars alla glyfer har bredd och höjd noll. Svaren är SJÄLVKONSEKVENTA
// med det, inte bara nollor: när inget tecken tar plats ryms hela strängen i
// varje bredd, och en position bortom texten (som har bredd noll) ger dess
// slut — precis som kontraktet i text.h säger för en riktig font.
class NullFont : public Font {
public:
    Sizef measure(const std::string&) const override { return Sizef{}; }
    float height() const override { return 0.0f; }
    bool  valid() const override { return false; }
    float prefixWidth(const std::string&, std::size_t) const override
    {
        return 0.0f;
    }
    std::size_t byteIndexAtX(const std::string& utf8, float x) const override
    {
        return x > 0.0f ? utf8.size() : 0;
    }
    std::size_t fitWidth(const std::string& utf8, float maxW) const override
    {
        return maxW >= 0.0f ? utf8.size() : 0;
    }
    float     ascent() const override { return 0.0f; }
    FontStyle style() const override { return FontStyle::Regular; }
    bool      rasterGlyph(char32_t, GlyphRaster& out) const override
    {
        out = GlyphRaster{};
        return false;   // ogiltig font — som en riktig utan fil
    }
    bool hasGlyph(char32_t) const override { return false; }
};

// En buffert utan lagring: storlek 0 och ogiltig, så varje del av den är
// ogiltig och ett rit-anrop med den ritar ingenting.
class NullGpuBuffer : public GpuBuffer {
public:
    explicit NullGpuBuffer(BufferKind kind) : kind_(kind) {}
    BufferKind  kind() const override { return kind_; }
    std::size_t size() const override { return 0; }
    bool        valid() const override { return false; }

private:
    BufferKind kind_;
};

// Ett mål utan yta: dess bild är en NullImage — ett prov, inget djup, som
// ett ogiltigt mål ur en riktig backend.
class NullRenderTarget : public RenderTarget {
public:
    Image& image() override { return image_; }
    int    samples() const override { return 1; }
    bool   hasDepth() const override { return false; }

private:
    NullImage image_;
};

// Ett program som aldrig kompilerades: ogiltigt, med ett
// fel som säger varför — som ett program vars källa saknas i en riktig
// backend.
class NullShaderProgram : public ShaderProgram {
public:
    explicit NullShaderProgram(const ProgramDesc& d)
        : ShaderProgram(d),
          error_("programmet '" + d.name + "' kompileras inte av NullRenderer")
    {
    }
    bool               valid() const override { return false; }
    const std::string& error() const override { return error_; }

private:
    std::string error_;
};

// Renderaren som inte ritar något. Fabrikerna ger Null-objekten ovan — aldrig
// nullptr, fasadens kontrakt gäller även här.
class NullRenderer : public Renderer {
public:
    void clear(Color) override {}
    void fillRect(const Rectf&, Color) override {}
    void strokeRect(const Rectf&, Color) override {}
    void line(float, float, float, float, Color) override {}
    void fillRoundedRect(const Rectf&, float, Color) override {}
    void strokeRoundedRect(const Rectf&, float, Color, float) override {}
    void dropShadow(const Rectf&, float, float, float, float, Color) override
    {
    }
    void innerShadow(const Rectf&, float, float, float, float, Color) override
    {
    }
    float drawText(Font&, float, float, const std::string&, Color) override
    {
        return 0.0f;
    }
    std::unique_ptr<Image> loadImage(const std::string&) override
    {
        return std::make_unique<NullImage>();
    }
    std::unique_ptr<Image> loadImage(const void*, std::size_t) override
    {
        return std::make_unique<NullImage>();
    }
    using Renderer::drawImage;
    void drawImage(Image&, const Rectf&, const Rectf&, float) override {}
    std::unique_ptr<Font> loadFont(const std::string&, float) override
    {
        return std::make_unique<NullFont>();
    }
    std::unique_ptr<Font> loadFont(FontFamily, float, FontStyle) override
    {
        return std::make_unique<NullFont>();
    }
    std::unique_ptr<GpuBuffer> createBuffer(BufferKind kind, std::size_t,
                                            const void*) override
    {
        return std::make_unique<NullGpuBuffer>(kind);
    }
    bool updateBuffer(GpuBuffer&, std::size_t, const void*,
                      std::size_t) override
    {
        return false;
    }
    BufferSlice uploadTransient(BufferKind, const void*, std::size_t) override
    {
        return BufferSlice{};
    }
    void drawMesh(const MeshDraw&) override {}
    std::unique_ptr<ShaderProgram> createProgram(const ProgramDesc& d) override
    {
        return std::make_unique<NullShaderProgram>(d);
    }
    using Renderer::createImage;
    std::unique_ptr<Image> createImage(int, int, ImageFormat, const void*,
                                       std::size_t, bool) override
    {
        return std::make_unique<NullImage>();
    }
    bool updateImage(Image&, int, int, int, int, const void*,
                     std::size_t) override
    {
        return false;
    }
    // D3D11:s tak från funktionsnivå 11_0 — det vanligaste, så en grind mot
    // den här renderaren delar en bild i rutor där en riktig GPU gör det.
    int maxTextureSize() const override { return 16384; }
    using Renderer::createRenderTarget;
    std::unique_ptr<RenderTarget> createRenderTarget(int, int,
                                                     const TargetDesc&) override
    {
        return std::make_unique<NullRenderTarget>();
    }
    int  supportedSamples(int) const override { return 1; }
    bool readTarget(RenderTarget&, std::vector<std::uint8_t>&) override
    {
        return false;   // inget mål har några pixlar här
    }
    bool beginOffscreen() override { return false; }
    void endOffscreen() override {}
    NativeDevice nativeDevice() const override { return {}; }
    std::unique_ptr<RenderTarget> importRenderTarget(const ExternalTargetDesc&,
                                                     RenderTarget*,
                                                     std::string& error) override
    {
        error = "renderaren har ingen GPU";
        return std::make_unique<NullRenderTarget>();
    }
    void setExternalFramePacing(bool) override {}
    std::unique_ptr<GpuCompletion> insertCompletion(std::string& error) override
    {
        error = "renderaren har ingen GPU";
        return nullptr;
    }

protected:
    void applyClip(const Rectf*, const RoundedClip*, std::size_t) override {}
    void bindTarget(RenderTarget*) override {}
};

} // namespace gfx
