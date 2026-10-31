#include "comfort_pass.h"

#include <utility>

#include <gfx/native.h>
#include <gfx/platform.h>
#include <gfx/scene.h>

#include "vector_ops.h"

namespace glaze::openxr::detail {

namespace {

// Fullskärmstriangeln gör sina hörn ur SV_VertexID. Per pixel räknas vinkeln
// från blickens axel ur ögats tangenter (tangenterna interpoleras linjärt över
// bilden, som projektionen gör), och vinjetten är en mjuk kant från helt klart
// vid `inner` till helt mörkt vid `inner + feather`. Svärtan läggs ovanpå som
// en jämn alfa. Färgen är alltid svart.
//   uTan   = (vänster, höger, ned, upp) som tangenter
//   uShape = (inner, feather, svärta, 0), vinklar i radianer
constexpr const char* kHlsl = R"hlsl(
cbuffer GlazeUser : register(b4) { float4 uTan; float4 uShape; };
struct VsOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VsOut vsMain(uint id : SV_VertexID)
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float4 fsMain(VsOut i) : SV_Target
{
    float tx    = lerp(uTan.x, uTan.y, i.ndc.x * 0.5 + 0.5);
    float ty    = lerp(uTan.z, uTan.w, i.ndc.y * 0.5 + 0.5);
    float angle = atan(sqrt(tx * tx + ty * ty));
    float vig   = smoothstep(uShape.x, uShape.x + uShape.y, angle);
    float a     = 1.0 - (1.0 - vig) * (1.0 - saturate(uShape.z));
    return float4(0.0, 0.0, 0.0, a);
}
)hlsl";

constexpr std::uint32_t kUserBytes = 32;   // två float4

gfx::ProgramDesc programDesc() {
    gfx::ProgramDesc desc;
    desc.name = "komfort";
    desc.hlsl = kHlsl;
    desc.userBytes = kUserBytes;
    return desc;
}

// Ett offscreen-block som alltid slutar.
class Block {
public:
    explicit Block(gfx::Renderer& r) : r_(r), open_(r.beginOffscreen()) {}
    ~Block() {
        if (open_) r_.endOffscreen();
    }
    Block(const Block&)            = delete;
    Block& operator=(const Block&) = delete;
    bool open() const { return open_; }

private:
    gfx::Renderer& r_;
    bool           open_;
};

} // namespace

ComfortPass::ComfortPass(gfx::Renderer& renderer) : renderer_(&renderer) {
    program_ = renderer.createProgram(programDesc());
    if (!program_ || !program_->valid()) {
        gfx::log("komforten: programmet för vinjetten och svarttoningen gick inte att bygga: %s",
                 program_ ? program_->error().c_str() : "inget program");
    }
}

ComfortPass::~ComfortPass() { release(); }

bool ComfortPass::usable() const { return renderer_ && program_ && program_->valid(); }

bool ComfortPass::importImages(std::size_t eye, const EyeImageSet& images, std::string& error) {
    if (eye >= targets_.size() || !renderer_) {
        error = "Komfortpasset har inget öga med det numret.";
        return false;
    }
    for (void* image : images.images) {
        gfx::ExternalTargetDesc desc;
        desc.api = gfx::NativeGraphicsApi::D3D11;
        desc.image = image;
        desc.width = images.width;
        desc.height = images.height;
        desc.color = images.bgra ? gfx::TargetColorFormat::Bgra8Unorm : gfx::TargetColorFormat::Rgba8Unorm;
        desc.target.samples = 1;
        desc.target.depth = false;

        std::string importError;
        std::unique_ptr<gfx::RenderTarget> target = renderer_->importRenderTarget(desc, nullptr, importError);
        if (!target || !target->image().valid()) {
            error = "En swapchain-bild gick inte att importera för komforten" +
                    (importError.empty() ? std::string(".") : ": " + importError);
            return false;
        }
        targets_[eye].push_back(std::move(target));
    }
    return true;
}

bool ComfortPass::draw(const std::array<Eye, 2>& eyes, double blackAlpha, std::string& error) {
    if (!usable()) {
        error = "Komfortpasset saknar ett giltigt program.";
        return false;
    }
    for (std::size_t eye = 0; eye < 2; ++eye) {
        if (eyes[eye].imageIndex >= targets_[eye].size()) {
            error = "Runtimen gav en bild som inte importerats för komforten.";
            return false;
        }
    }

    const Block block(*renderer_);
    if (!block.open()) {
        error = "Komforten kunde inte öppna ett offscreen-block.";
        return false;
    }

    const float black = static_cast<float>(unitValue(blackAlpha));
    bool ok = true;
    for (std::size_t eye = 0; eye < 2; ++eye) {
        gfx::RenderTarget& target = *targets_[eye][eyes[eye].imageIndex];
        if (!renderer_->pushTargetPreserved(target)) {
            error = "Komforten kunde inte binda en swapchain-bild utan att rensa den.";
            ok = false;
            continue;
        }
        const Eye& e = eyes[eye];
        const float user[8] = {static_cast<float>(e.tangents.left),
                               static_cast<float>(e.tangents.right),
                               static_cast<float>(e.tangents.down),
                               static_cast<float>(e.tangents.up),
                               static_cast<float>(e.shape.innerRadians),
                               static_cast<float>(e.shape.featherRadians),
                               black,
                               0.0f};
        gfx::MeshDraw draw;
        draw.program = gfx::MeshProgram::Custom;
        draw.shader = program_.get();
        draw.layout = &layout_;
        draw.vertexCount = 3;
        draw.user = user;
        draw.userBytes = kUserBytes;
        draw.state.blend = gfx::Blend::Alpha;
        renderer_->drawMesh(draw);
        renderer_->popTarget();
    }
    return ok;
}

void ComfortPass::release() {
    for (auto& eye : targets_) {
        while (!eye.empty()) eye.pop_back();
    }
    program_.reset();
}

} // namespace glaze::openxr::detail
