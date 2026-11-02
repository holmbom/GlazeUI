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
// De namngivna pekarna nedan är null för barn som inte finns — setTitle/
// controlBox-API:t använder dem null-säkert. Tom TopBar-yta är fönstrets
// DRAGYTA (hit-test-kartan byggs ur widget-trädet).
//
// FÖNSTRETS EGEN NAMNLIST KAN BÄRA MENYER OCH GLYFKNAPPAR:
//     [ikon] [menyer] [start …]   ……  Titel  ……   [… slut] [– □ ✕]
// (macOS speglar: fönsterknapparna, slutgruppen, titeln, startgruppen, menyerna
// och ikonen). Menyerna kommer med `menuBar.setInTitleBar(true)`, och ett barn
// hamnar i slutgruppen med `dock = Dock::Right`, annars i startgruppen:
// `addStart<T>()`/`addEnd<T>()` är kortformerna, och i `.glazeui` är det
// `topBar.adopt(knapp);` och `knapp.dock = glaze::Dock::Right;`. Menar man enkla
// glyfer är barnet en `ToolButton` med `setGlyphOnly(true)`.
//
// TITELN centreras i området som blir kvar, med stilens `titleMinGap` på båda
// sidor. Blir det trångt kortas den till `Titel…`, sedan samlas menyrubriker
// under ⋯, och till sist döljs titeln. Allt fönsterknappar och glyfer behöver har
// företräde; de rinner över hellre än att krympa. `setShowTitle(false)` tar bort
// titeln ur namnlisten (OS:et behåller fönstrets titel), och ytan förblir dragyta.
// Bär namnlisten inget utöver standardbarnen står titeln mitt i hela fönstret.
//
// I Slate tonas menyerna och glyferna (`Bar::mutedChildTheme`), medan titeln och
// fönsterknapparna behåller sina färger.
//
// MÖNSTER: inbyggd widget — ritningen är Bars (src/bar.cpp).

namespace glaze {

class AppWindow;
class CaptionButton;
class Label;
class Spacer;

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

    // Kortformerna för ett barn i start- eller slutgruppen (headern).
    template <class T, class... A>
    T* addStart(A&&... a)
    {
        return add<T>(std::forward<A>(a)...);
    }
    template <class T, class... A>
    T* addEnd(A&&... a)
    {
        T* w  = add<T>(std::forward<A>(a)...);
        w->dock = Dock::Right;
        return w;
    }

    // TITELN i namnlisten — på som förval. Av tar bort den och dess marginaler.
    void setShowTitle(bool on);
    bool showTitle() const { return showTitle_; }

    // setProp-egenskaper: showTitle (true/false); resten tar Bar.
    bool setProp(const std::string& key, const std::string& value) override;

    // Fönstrets egen namnlist placerar sina barn när den bär något utöver
    // standardbarnen (headern); annars, och i en nästlad TopBar, är det Bars flex.
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    // Menyerna och glyferna tonas i Slate (`Bar::mutedChildTheme`).
    const Theme::Variant* childTheme(const Widget& child,
                                     const Ctx& c) const override;

private:
    friend class AppWindow;   // buildTopBar() fyller i pekarna nedan
    Label*         titleLabel_  = nullptr;
    CaptionButton* minButton_   = nullptr;
    CaptionButton* maxButton_   = nullptr;
    CaptionButton* closeButton_ = nullptr;
    Spacer*        balance_     = nullptr;   // motvikten mot fönsterknapparna
    Spacer*        titleBefore_ = nullptr;   // de två kring titeln
    Spacer*        titleAfter_  = nullptr;
    Spacer*        edgeGap_     = nullptr;   // kantluften vid fönsterknapparna
    bool           showTitle_   = true;

    // Standardbarnen — de som placeras av flexen när inget annat finns.
    bool isStandard(const Widget& child) const;
    bool isWindowBar() const;   // fönstrets EGEN namnlist
    bool bearsExtras() const;   // ett synligt barn utöver standardbarnen

    // Var barnet hamnar när namnlisten bär något (fysiska px). Tillståndslös,
    // räknad per fråga som flexen.
    gfx::Rectf extrasRect(const Widget& child, const Ctx& c) const;

protected:
    // En kontroll behåller sin egen höjd; fönsterknapparna och menyerna fyller
    // remsan.
    std::pair<float, float> crossSpan(const Widget& child, const Ctx& c,
                                      bool row) const override;


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
