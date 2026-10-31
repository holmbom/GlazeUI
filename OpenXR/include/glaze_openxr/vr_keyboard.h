#pragma once

// VR-TANGENTBORDET — en panel i `Interface3D` med tangenter som skriver i en
// platt panel (`FlatPanel`): ett klick på en tangent ger panelens fokuserade
// widget `KeyDown`, texten (`TextInput`) och `KeyUp`, precis som ett fysiskt
// tangentbord. Layouten är data: färdiga förval eller appens egna tangenter,
// placerade som panelens element (rutnät, rader eller fritt).
//
// Shift och Ctrl är vippor som gäller nästa tangent (Shift släpps efter den,
// Ctrl också). Ingen IME och ingen upprepning när en tangent hålls.
//
// Appen äger kopplingen till händelserna: `Interface3D::onElement` är appens,
// och appen skickar varje händelse vidare till `handleElement`, som svarar
// true när den hörde till tangentbordet.

#include <cstdint>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <glaze_openxr/interface3d.h>

namespace glaze::openxr {

class FlatPanel;

struct KeyboardKey {
    Element       element;                 // id, cell, stil och etikett (Button eller Toggle)
    gfx::Key      key = gfx::Key::Unknown;   // tangenten i KeyDown/KeyUp (Unknown = bara text)
    std::string   text;                    // texten tangenten skriver (UTF-8, tom = ingen)
    std::string   shiftedText;             // texten med Shift (tom = `text`)
    std::uint32_t toggleModifier = 0;      // kModShift eller kModCtrl: en vippa i stället för en tangent
};

struct KeyboardLayout {
    std::vector<KeyboardKey> keys;
    int columns = 11;                      // rutnätets kolumner (panelens `Layout::columns`)
};

enum class KeyboardPreset : std::uint8_t { Swedish, English };
KeyboardLayout keyboardLayout(KeyboardPreset preset);

class VrKeyboard final {
public:
    // Lägger panelen `panel` i `ui` med layoutens tangenter som element
    // (panelens egna `elements` ersätts; placering, stil och synlighet är
    // panelens). Tas bort ur `ui` när tangentbordet förstörs.
    VrKeyboard(Interface3D& ui, Panel panel, KeyboardLayout layout);
    ~VrKeyboard();

    VrKeyboard(const VrKeyboard&)            = delete;
    VrKeyboard& operator=(const VrKeyboard&) = delete;

    void setLayout(KeyboardLayout layout);
    // Panelen som tangenterna skriver i; nullptr kopplar bort.
    void setTarget(FlatPanel* target);
    FlatPanel* target() const { return target_; }
    const std::string& panelId() const { return panelId_; }

    // En händelse ur `Interface3D::onElement`; true när den hörde till
    // tangentbordet (och då är hanterad).
    bool handleElement(const ElementEvent& event);

private:
    void rebuild();

    Interface3D&   ui_;
    Panel          panel_;
    std::string    panelId_;
    KeyboardLayout layout_;
    FlatPanel*     target_ = nullptr;
    std::uint32_t  mods_   = 0;   // påslagna vippor (kModShift, kModCtrl)
};

} // namespace glaze::openxr
