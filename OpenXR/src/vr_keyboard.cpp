#include <glaze_openxr/vr_keyboard.h>

#include <algorithm>
#include <utility>

#include <glaze_openxr/flat_panel.h>

#include "vr_keyboard_logic.h"

namespace glaze::openxr {

namespace {

// Panelen som lagts i gränssnittet: layoutens tangenter som element i ett
// rutnät med layoutens kolumner. Vipporna börjar i läget `mods`.
Panel buildPanel(const Panel& base, const KeyboardLayout& layout, std::uint32_t mods)
{
    Panel p = base;
    p.flat.reset();
    p.elements.clear();
    p.elements.reserve(layout.keys.size());
    for (const KeyboardKey& key : layout.keys) {
        Element e = key.element;
        if (key.toggleModifier != 0) {
            e.kind    = ElementKind::Toggle;
            e.toggled = (mods & key.toggleModifier) != 0;
        }
        p.elements.push_back(std::move(e));
    }
    p.layout.kind    = LayoutKind::Grid;
    p.layout.columns = layout.columns;
    return p;
}

} // namespace

VrKeyboard::VrKeyboard(Interface3D& ui, Panel panel, KeyboardLayout layout)
    : ui_(ui), panel_(std::move(panel)), panelId_(panel_.id), layout_(std::move(layout))
{
    ui_.addPanel(buildPanel(panel_, layout_, mods_));
}

VrKeyboard::~VrKeyboard()
{
    ui_.removePanel(panelId_);
}

void VrKeyboard::setLayout(KeyboardLayout layout)
{
    layout_ = std::move(layout);
    rebuild();
}

void VrKeyboard::setTarget(FlatPanel* target)
{
    target_ = target;
}

void VrKeyboard::rebuild()
{
    mods_ = 0;
    // Panelens läge (en greppad pose, synlighet) tas ur gränssnittet; bara
    // elementen och rutnätet byts.
    Panel p = buildPanel(panel_, layout_, mods_);
    if (const Panel* live = ui_.panel(panelId_)) {
        p.mount   = live->mount;
        p.visible = live->visible;
    }
    ui_.setPanel(panelId_, std::move(p));
}

bool VrKeyboard::handleElement(const ElementEvent& event)
{
    if (event.panel != panelId_)
        return false;
    const auto found = std::find_if(layout_.keys.begin(), layout_.keys.end(),
                                    [&](const KeyboardKey& k) { return k.element.id == event.element; });
    if (found == layout_.keys.end())
        return false;
    const KeyboardKey& key = *found;

    // En vippa gäller nästa tangent: gränssnittet har redan växlat elementet,
    // här följer bara masken med.
    if (key.toggleModifier != 0) {
        if (event.kind == ElementEventKind::Toggled) {
            if (event.value != 0.0)
                mods_ |= key.toggleModifier;
            else
                mods_ &= ~key.toggleModifier;
        }
        return true;
    }

    if (event.kind != ElementEventKind::Clicked)
        return true;

    if (target_) {
        for (const gfx::Event& e : detail::keyEvents(key, mods_))
            target_->sendEvent(e);
    }

    // Shift och Ctrl släpps efter tangenten.
    if (mods_ != 0) {
        for (const KeyboardKey& k : layout_.keys) {
            if (k.toggleModifier == 0 || (mods_ & k.toggleModifier) == 0)
                continue;
            if (const Element* current = ui_.element(panelId_, k.element.id)) {
                Element off = *current;
                off.toggled = false;
                ui_.setElement(panelId_, std::move(off));
            }
        }
        mods_ = 0;
    }
    return true;
}

} // namespace glaze::openxr
