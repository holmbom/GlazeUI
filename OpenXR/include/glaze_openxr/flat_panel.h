#pragma once

// PLATT PANEL — ett riktigt Glaze-widgetträd som visas på en yta i en
// 3D-scen (`Panel::flat` i `Interface3D`) i stället för i ett eget fönster.
//
// FlatPanel är ett AppWindow som aldrig visas (`setOffscreen`), utan ram.
// Appen bygger trädet som i vilket fönster som helst — `root().adopt(...)`,
// fokus, tema, händelser — och anropar `show()`. Pumpen ritar fönstret när
// något i det ändrats; `Interface3D` läser bilden (`refresh`) och lägger den
// på ytan, och skickar pekarens mus- och hjulhändelser till fönstret
// (`sendEvent`). VR-tangentbordet (`VrKeyboard`) skickar tangenterna samma väg.
//
// BEGRÄNSNINGAR: fönster som panelen skulle äga — popuper (Dropdown, menyer,
// verktygstips) och dialoger — öppnas inte (`acceptsOwnedWindow`), och
// fönstrets overlay följer inte med bilden.
//
// TRÅDARNA: allt i UI-tråden. Ett levande FlatPanel är ett eget fönster med
// egen grafikenhet; många paneler visar därför stillbilder (`snapshot`).

#include <glaze/glaze.h>

namespace glaze::openxr {

class FlatPanel : public glaze::AppWindow {
public:
    // Klientytan i logiska px.
    FlatPanel(int width, int height);
    ~FlatPanel() override;

    FlatPanel(const FlatPanel&)            = delete;
    FlatPanel& operator=(const FlatPanel&) = delete;

    // En syntetisk händelse till fönstrets egen ingång — samma väg som en från
    // OS:et (mus i fönstrets fysiska px, hjul, tangenter, text). Aldrig
    // inifrån en handler i panelen; posta då i stället.
    void sendEvent(const gfx::Event& event);

    // Läser en ny bild om fönstret ritats sedan förra anropet; true när
    // `image()` fått nya pixlar.
    bool refresh();
    // Den senast lästa bilden (fysiska px). Ett handtag som följer panelen:
    // det får nya pixlar vid nästa `refresh`.
    const glaze::Bitmap& image() const { return image_; }
    // En fristående KOPIA av den senast lästa bilden — en stillbild som inte
    // ändras när panelen ritas om. Ogiltig innan något lästs.
    glaze::Bitmap snapshot() const;

protected:
    // Panelen äger inga fönster (popuper, dialoger): de kan inte visas på ytan.
    bool acceptsOwnedWindow(const glaze::AppWindow& child) const override;

private:
    glaze::Bitmap image_;
};

} // namespace glaze::openxr
