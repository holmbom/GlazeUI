#include <glaze_openxr/flat_panel.h>

namespace glaze::openxr {

FlatPanel::FlatPanel(int width, int height)
    : AppWindow({}, width, height, gfx::Chrome::None)
{
    setOffscreen(true);
}

FlatPanel::~FlatPanel() = default;

void FlatPanel::sendEvent(const gfx::Event& event)
{
    handleEvent(event);
}

bool FlatPanel::refresh()
{
    return updateOffscreen(image_) == OffscreenUpdate::Updated;
}

glaze::Bitmap FlatPanel::snapshot() const
{
    if (!image_.valid() || image_.pixels() == nullptr)
        return {};
    return glaze::Bitmap(image_.width(), image_.height(), image_.pixels());
}

bool FlatPanel::acceptsOwnedWindow(const glaze::AppWindow&) const
{
    return false;
}

} // namespace glaze::openxr
