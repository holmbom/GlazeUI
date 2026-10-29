#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "glaze/assets.h"
#include "glaze/component.h"
#include "glaze/language.h"
#include "glaze/menu.h"
#include "glaze/widget.h"

// SYSTEMFÄLTET (WinForms `NotifyIcon`) — appens
// ikon vid klockan: Windows meddelandefält och macOS menyradens statusposter.
//
//     glaze::TrayIcon tray;                        // en medlem i fönstret
//     tray.setToolTip("Mätningen pågår");
//     tray.addItem({}, "Visa fönstret");
//     tray.setHandler("Visa fönstret", [this] { show(); });
//     tray.onClick = [this] { isHidden() ? show() : hide(); };
//     tray.show();
//
// Filen ligger i katalogROTEN av `Menu`s och `FileDialog`s skäl: ikonen är
// ingen WIDGET. En widget sitter i ett fönsters träd och ikonen i OS:ets fält,
// och ett `Widget`-arv hade gett trettio fält och krokar utan betydelse
// (dock, ankare, fokus, `draw`) och en `adopt` som kompilerar men inte gör
// något. Den är inte heller ett värdefält på `AppWindow` som `appIcon`:
// ikonen är APPENS och inte fönstrets, och då hade varje dialog fått en.
//
// I `.glazeui` SOM KOMPONENT: ikonen är en
// `glaze::Component` — en medlem i regionen som aldrig adopteras, med platta
// settrar (`setIcon`, `setSmallIcon`, `setTemplateImage`, `setToolTip`,
// `setInterval`), kollektionsvokabulären för bilderna, `{}`-roten för menyn
// och event-kopplingar med ikonen som sender. Previewn skapar den och utför
// allt som bara LAGRAR — men aldrig `show()`: ritbordet får aldrig lägga en
// ikon i OS:ets fält, och konstruktorn rör inte gfx (tray_icon.cpp, punkt 1).
//
// FÖRVAL DOLD: ingenting syns förrän `show()`, som skapar `glaze::App` lat
// precis som ett fönsters `show()`. Varje objekt är en ikon — flera går bra,
// normalt räcker en.
//
// BILDEN är `setIcon` med `setSmallIcon` vid sidan av, `AppIcon`s par. Ett
// ogiltigt handtag är FÖRVALET, GlazeUI-ikonen, och då vinner programfilens
// egen ikon där den finns (exe:ns ikonresurs nr 1, paketets
// `CFBundleIconFile`) — samma regel som fönstrets ikon. Fältet visar en
// liten bild (16–48 px på Windows, 18 pt på macOS), räknad ur ramarna.
// `setTemplateImage(true)` gör den till en MALLBILD på macOS, som menyraden
// färgar efter ljust och mörkt läge.
//
// ANIMATIONEN är bilder och ett intervall: `addFrame`/`setFrame`/`setFrames`
// (kollektionsvokabulären), `setInterval` och `startAnimation`/
// `stopAnimation`. Den GÅR bara medan ikonen syns, är startad och har minst
// två bilder — annars står timern still och appen sover (regel 1). En
// startad animation med en enda bild visar den bilden; en stoppad visar den
// stilla ikonen.
//
// TOOLTIPEN är OS:ets egen. `setToolTip` tar en `TrText`, så
// `glaze::tr(id)` följer ett språkbyte. Windows kapar den vid 127 tecken.
//
// HÄNDELSERNA följer regel 6, och SENDER ÄR IKONEN: `Handler<Args,
// TrayIcon>` tar båda formerna, och värdena läses ur sender
// (`sender->isVisible()`).
//   * `onClick` — vänsterklick, och på Windows Enter på en ikon som valts
//     med tangentbordet (Win+B). Ett dubbelklick ger `onClick` OCH sedan
//     `onDoubleClick`, som i WinForms och Qt.
//   * `onContextMenu` — högerklick, Ctrl+klick på macOS, Skift+F10 och
//     menytangenten på Windows. Den fyras FÖRE menyn öppnas, så handlern
//     kan bocka och tända poster; menyn öppnas efteråt om den har poster.
//   * `onHover` — `HoverArgs::entered` in/ut. Windows kan säga "ut" upp till
//     100 ms sent.
//   Medan en MODAL dialog är öppen fyras klicken INTE: dialogen lyfts i
//   stället, som vid ett klick på ett blockerat fönster. Hovern går igenom.
//
// MENYN ÄR VÅR, aldrig OS:ets: en `glaze::Menu` byggd med `AppIcon`s
// vokabulär, där den TOMMA stigen är roten — men utan genvägsargument, för
// inget fönster tar menyns acceleratorer. Den öppnas vid ikonen, kant i kant
// med aktivitetsfältet respektive under menyraden, i ett internt värdfönster
// som TAR FOKUS, så att ett klick utanför och Escape stänger den som vilken
// meny som helst. MENYPOSTERNAS sender är värdfönstrets rot, en intern widget —
// den som vill nå ikonen ur en posts handler fångar den i lambdan. På macOS
// öppnar ett vänsterklick ingen meny av sig självt; vill appen det är det en
// rad: `tray.onClick = [this] { tray.openMenu(); };`.
//
// PLATTFORMARNA: Windows och macOS. Linux har inget systemfält än —
// `isSupported()` svarar false och `show()` loggar en gång per process. En
// app som startar DOLD (`AppWindow::hide()` före `run()`) ska fråga först,
// annars blir den osynlig och går inte att nå.
//
// LIVSTIDEN: en medlem i huvudfönstret eller en lokal i `main`. Objektet är
// varken kopierbart eller flyttbart — menyn och registret pekar hit — och
// när det förstörs tas ikonen bort ur OS:ets fält och menyn stängs.

namespace glaze {

namespace detail {
// Det interna, i src/tray_icon.cpp: registrets id och generation,
// animationsmodellen (src/tray_model.h), hovern, ankaret och menyns
// värdfönster — allt som bara maskineriet läser.
struct TrayState;
} // namespace detail

// Args till `onContextMenu` — fyras FÖRE menyn öppnas. Tom i dag; ny data
// blir ett fält här och aldrig en parameter (regel 6).
struct TrayMenuArgs : EventArgs {};

class TrayIcon : public Component {
public:
    // Animationens intervall i millisekunder: förvalet, och golvet som
    // `setInterval` klampar till (en bild per 60-hertzram).
    static constexpr int kDefaultIntervalMs = 500;
    static constexpr int kMinIntervalMs     = 16;

    TrayIcon();
    ~TrayIcon() override;
    // ICKE KOPIERBAR OCH ICKE FLYTTBAR: menyn och registret pekar hit, och en
    // flytt hade lämnat dem dinglande (`Menu`s regel; basen säger detsamma).
    TrayIcon(const TrayIcon&)            = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&)                 = delete;
    TrayIcon& operator=(TrayIcon&&)      = delete;

    // Har plattformen ett systemfält (`gfx::trayAvailable()`)? Windows och
    // macOS ja, Linux nej. Går att fråga innan någon `App` finns.
    static bool isSupported();

    // OBJEKTETS NAMN är basens (`Component::name`) — för loggarna och för
    // verktygen. Ramverket läser det aldrig.

    // Previewns väg in (`Component::setProp`):
    // `interval` (heltal), `templateImage` (`true`/`false`) och `toolTip` som
    // strängliteral. Bilderna är handtag och går den typade vägen (verktygens
    // `applyComponentStmt`); ett översättnings-id i `toolTip` går genom
    // `setTextToken`. Ett ogiltigt värde loggas och ignoreras.
    bool setProp(const std::string& key, const std::string& value) override;
    bool setTextToken(const std::string& key, const TrText& value) override;

    // ---------------------------------------------------------- synligheten
    // FÖRVAL DOLD. `show()` lägger ikonen i OS:ets fält och skapar
    // `glaze::App` lat; `hide()` tar bort den, stänger menyn och stoppar
    // timern. `isVisible()` svarar med BEGÄRAN — också på Linux, där ingen
    // ikon syns (`isSupported()`).
    void show();
    void hide();
    bool isVisible() const { return visible_; }

    // ---------------------------------------------------------------- bilden
    // Ogiltig bild = FÖRVALET (GlazeUI-ikonen, och programfilens egen ikon
    // där den finns). Den LILLA bilden är en handritad 16 eller 32 vid sidan
    // av — valregeln tar den i de storlekar den räcker till. Ett
    // `ImageSource`: en asset eller en `glaze::Bitmap`,
    // vars pixlar räknas om på CPU:n till fältets ramar. Ändrade pixlar når
    // fältet med ett nytt `setIcon(bild)` — samma bild i en ny version är en
    // ändring. En `RenderTexture` kan inte bli en OS-ikon: fältet visar
    // förvalet (loggat en gång).
    void               setIcon(ImageSource img);
    const ImageSource& icon() const { return icon_; }
    void               setSmallIcon(ImageSource img);
    const ImageSource& smallIcon() const { return small_; }
    // macOS MALLBILD (menyraden färgar den); ingen verkan på Windows.
    void setTemplateImage(bool on);
    bool isTemplateImage() const { return template_; }

    // -------------------------------------------------------------- tooltipen
    // OS:ets egen. Den UPPLÖSTA texten och IDENTITETEN, `AppWindow::title()`
    // och `titleToken()`s par.
    void               setToolTip(TrText t);
    const std::string& toolTip() const { return toolTip_.str(); }
    const TrText&      toolTipToken() const { return toolTip_; }

    // ------------------------------------- ANIMATIONEN (kollektionsvokabulären)
    // `addX` · `setX(index, …)` · `setXs(lista)`, som varje lista i
    // ramverket. Index utanför ⇒ `gfx::log` + no-op, aldrig tyst tillväxt.
    // En ogiltig bild i listan visar förvalet i den rutan. Bilderna är
    // `ImageSource` — en ruta ur pixlar räknas om ur bildens LEVANDE pixlar
    // när den visas, och `setFrame(i, bild)` med en ny version lägger om den
    // direkt när rutan syns.
    void        addFrame(ImageSource img);
    void        setFrame(int index, ImageSource img);
    void        setFrames(std::vector<ImageSource> frames);
    int         frameCount() const { return static_cast<int>(frames_.size()); }
    // Bilden på index, eller en ogiltig bild (och en loggrad) utanför.
    ImageSource frame(int index) const;

    // Tiden per bild i millisekunder. Under `kMinIntervalMs` klampas den
    // (loggas). Ett byte medan animationen går gäller från nästa bild.
    void setInterval(int ms);
    int  interval() const { return intervalMs_; }

    // BEGÄRAN, inte vad som syns: en startad animation går först när ikonen
    // syns och har minst två bilder, och `isAnimating()` svarar true redan
    // innan. `stopAnimation()` visar den stilla ikonen igen, och nästa start
    // börjar på den första bilden.
    void startAnimation();
    void stopAnimation();
    bool isAnimating() const;

    // ------------------------------------------------------------ MENYN
    // `AppIcon`s platta form med den TOMMA stigen `{}` som roten, men utan
    // genvägsargument: inget fönster tar systemfältsmenyns acceleratorer.
    // Okänd stig loggas och ignoreras (regel 4-arvet).
    //     tray.addItem({}, "Visa fönstret");
    //     tray.addSubmenu({}, "Läge");
    //     tray.addRadio("Läge", "Tyst", true);
    void addItem(const MenuPath& path, TrText text);
    void addCheck(const MenuPath& path, TrText text, bool checked = false);
    void addRadio(const MenuPath& path, TrText text, bool checked = false);
    void addSeparator(const MenuPath& path);
    void addSubmenu(const MenuPath& path, TrText text);

    // Handlern på en stig — TEMPLATE av `AppIcon::setHandler`s skäl (den
    // parameterlösa formen adapteras i `Handler::operator=`). Postens sender
    // är menyns interna värd, inte ikonen (se ovan).
    template <class F>
    void setHandler(const MenuPath& path, F&& f)
    {
        if (MenuItem* it = itemAtPath(path))
            it->onClick = std::forward<F>(f);
        else
            logMissingPath("setHandler", path);
    }

    // MENYPOSTENS TILLSTÅND — MenuBars och AppIcons tre platta mutatorer
    // (en implementation i src/menu_path.h): postens
    // bild, `enabled` och kryss, SAMMA tillstånd som `itemAtPath(stig)->…`.
    // Okänd stig loggas och ignoreras; ett kryss på en radiopost avmarkerar
    // inte syskonen. En öppen meny visar sin snapshot tills den öppnas igen.
    void setItemIcon(const MenuPath& path, ImageSource icon);
    void setItemEnabled(const MenuPath& path, bool on);
    void setItemChecked(const MenuPath& path, bool on);

    // Posten på en stig, eller nullptr. Den tomma stigen är menyn och ingen
    // post, så den svarar nullptr.
    MenuItem* itemAtPath(const MenuPath& path);

    // TRÄD-API:t för handkod — SAMMA tillstånd som den platta formen.
    // Referensen är stabil (menyn är ett fält).
    Menu& menu() { return menu_; }

    // Öppna menyn VID IKONEN — samma väg som högerklicket tar, utan
    // `onContextMenu`. En meny utan poster, en dold ikon och en plattform
    // utan systemfält öppnar ingenting. `closeMenu()` stänger (uppskjutet,
    // som allt stängande).
    void openMenu();
    void closeMenu();
    bool isMenuOpen() const;

    // ---------------------------------------------------------- händelserna
    // SENDER ÄR IKONEN. Båda formerna:
    //     tray.onClick = [this](glaze::TrayIcon* s, glaze::ClickArgs&) { … };
    //     tray.onClick = [this] { … };
    Handler<ClickArgs, TrayIcon>    onClick;
    Handler<ClickArgs, TrayIcon>    onDoubleClick;
    Handler<TrayMenuArgs, TrayIcon> onContextMenu;   // menyn öppnas EFTER
    Handler<HoverArgs, TrayIcon>    onHover;

private:
    // Maskineriet (src/tray_icon.cpp) — registret, dispatchen, synkningen
    // mot backenden och värdfönstret — står i `TrayState`, den ENDA vännen.
    // De fria funktionerna i src/tray_registry.h (`trayDispatch`,
    // `trayLanguageChanged`, `trayIdOf`) går genom dess statiska medlemmar,
    // så den publika headern varken deklarerar dem eller behöver deras typer.
    friend struct detail::TrayState;

    static void logMissingPath(const char* fn, const MenuPath& path);

    // Det appen BEGÄRT — det getters ovan svarar med.
    ImageSource              icon_{};
    ImageSource              small_{};
    bool                     template_   = false;
    TrText                   toolTip_;
    std::vector<ImageSource> frames_;
    int                      intervalMs_ = kDefaultIntervalMs;
    bool                     visible_    = false;
    Menu                     menu_;

    // Det interna (se `detail::TrayState` ovan). Bakom en pekare så att
    // headern inte behöver de interna typerna; skapas i konstruktorn och
    // finns alltid.
    std::unique_ptr<detail::TrayState> state_;
};

} // namespace glaze
