#pragma once
#include "glaze/widgets/bar.h"

// TopBar — den RIKTIGA namnlisten: en Bar som varje
// AppWindow med Chrome::Custom skapar och äger (första barnet i roten,
// placerad mot CHROME-ctx:t = hela klientytan medan innehållsbarnen
// behåller contentArea — captionen är non-client à la WinForms, så
// ankrade/dockade app-widgets påverkas inte). Plattforms-defaulterna och
// kopplingen till fönsterkontrollerna byggs av AppWindow vid konstruktion
// (OS-check — inte i flexmotorn):
//   Windows/Linux: [12][title][spacer][min][max][close] (glyf-knappar)
//   Mac:           [9][close][9][min][9][max][spacer]   (traffic lights)
// Fullt customizerbar via samma add()/hide() som alla barer; de namngivna
// pekarna nedan är null för barn som inte finns (Mac har ingen titel) —
// setTitle/controlBox-API:t använder dem null-säkert. Tom TopBar-yta är
// fönstrets DRAGYTA (hit-test-kartan byggs ur widget-trädet).
//
// MÖNSTER: inbyggd widget — ritningen är Bars (src/bar.cpp).

namespace glaze {

class AppWindow;
class CaptionButton;
class Label;

class TopBar : public Bar {
public:
    TopBar()
    {
        // Sträck-ankare överst i stället för Dock::Top: baren placeras av
        // AppWindow mot chrome-ctx:t, och som ODOCKAD carvar den inget ur
        // innehålls-syskonens area (klient-offseten ägs av backendens contentArea).
        anchors.right = true;   // left+top är default → sträck i toppen
    }

    // Namngivna default-barn (null när de inte byggts/tagits bort).
    // Ägda av trädet — pekarna är bara uppslag för fönster-API:t.
    //
    // LÄSACCESSORER och inte settrar: de här fyra är inte
    // TILLSTÅND utan pekare till BARN, så "sätt och invalidera" är fel
    // form för dem — den som byter ett barn ändrar trädet, och det gör
    // man via `add`/`adopt`. `AppWindow` är därför `friend`: den BYGGER
    // baren i `buildTopBar()` och är den enda som fyller i pekarna.
    Label*         titleLabel() const { return titleLabel_; }
    CaptionButton* minButton() const { return minButton_; }
    CaptionButton* maxButton() const { return maxButton_; }
    CaptionButton* closeButton() const { return closeButton_; }

    // STRUKTURELLA BARN. De fyra pekarna ovan är
    // RÅA och avrefereras av `AppWindow::setTitle`, `applyCaptionBoxes` och
    // maximerings-glyfen utan kontroll. `children()` är publikt, så vägen
    // in finns — vakten är att baren nekar att de tas bort. Definierad i
    // cpp:n: `Label`/`CaptionButton` är bara framåtdeklarerade här.
    bool canReleaseChild(const Widget& child) const override;

private:
    friend class AppWindow;   // buildTopBar() fyller i pekarna nedan
    Label*         titleLabel_  = nullptr;
    CaptionButton* minButton_   = nullptr;
    CaptionButton* maxButton_   = nullptr;
    CaptionButton* closeButton_ = nullptr;

protected:
    // Namnlisten ligger MOT fönstrets övre hörn, så den måste rundas med
    // exakt fönstrets radie — annars sticker en fyrkantig remsa ut ur ett
    // rundat fönster. Talet hämtas ur `AppWindow::cornerRadius()` (EN
    // sanning, delad med bakgrundsritningen) och är 0 för ett fyrkantigt
    // eller maximerat fönster.
    float topRadius(const Ctx& c) const override;

    // Under den STORA programikonen ritas ingen hårlinje:
    // ikonen täcker namnlist + menyrad, och linjen hade gått tvärs genom
    // bilden. Bara fönstrets EGEN namnlist — en nästlad TopBar har ingen
    // ikon bredvid sig.
    EdgeInsets innerEdgeInsets(const Ctx& c) const override;
};

} // namespace glaze
