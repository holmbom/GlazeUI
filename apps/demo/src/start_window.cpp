#include "start_window.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace {

// MENYSTIGARNA som appkoden refererar till. De byggs av SAMMA id som
// `.glazeui` skriver, så de två filerna kan läsas mot varandra — och
// matchningen är ID mot ID, alltså språkoberoende. En stig med literaler
// hade slutat träffa i samma sekund användaren bytte språk.
glaze::MenuPath lagePath(std::uint32_t id)
{
    return glaze::MenuPath({glaze::tr(demo::str::MenuUtseende),
                            glaze::tr(demo::str::Lage), glaze::tr(id)});
}

glaze::MenuPath animeraPath()
{
    return glaze::MenuPath({glaze::tr(demo::str::MenuUtseende),
                            glaze::tr(demo::str::AnimeraRutan)});
}

} // namespace

// Startlogik bor i onLoad (livscykel-eventet) — konstruktorn är genererad
// plumbing (initializeComponent) och skrivs aldrig för hand.
void StartWindow::onLoad(glaze::Widget*, glaze::EventArgs&)
{
    setAnimating(animate.isChecked());
    // Synka skala-dropdownen mot aktuell användarskala (app-global som
    // temat; appen äger lagringen — demon lagrar inget mellan körningar).
    const float us = glaze::app().userScale();
    skalaDd.setSelectedIndex(us < 0.85f   ? 0
                             : us < 0.95f ? 1
                             : us < 1.05f ? 2
                             : us < 1.18f ? 3
                                          : 4);
    // De tre utseende-axlarna mot faktiskt state (app-globalt, som läget).
    uiDd.setSelectedIndex(ui() == glaze::UI::Slate          ? 1
                          : ui() == glaze::UI::Minimalistic ? 2
                                                            : 0);
    syncThemeAndStyleDropdowns();
    syncSprak();
    // Nivå 1-panelens startvärde — samma väg som allt annat: text ur data.
    skjutVarde.setText(trs(demo::str::SliderPrefix) +
                       std::to_string(static_cast<int>(skjut.value())));
    byggMenyer();
}

// ------------------------------------------------------------- SPRÅKET
//
// LISTORNA fylls i KOD och inte i `.glazeui`: `setItems` tar
// `std::vector<std::string>`, och `glaze::tr(id).str()` är ett
// KOMPILERINGSFEL på en temporär (den `&&`-raderade överlagringen fångar
// just den dinglande referensen). Det är också hela skälet till att
// listornas items är kvar strängar — appen fyller om dem vid språkbyte,
// och det är precis vad `syncSprak` gör.
//
// SPRÅKLISTAN byggs ur `glaze::languageAt()` och inte ur en hårdkodad
// lista, så den följer filen. Raderna är SJÄLVBENÄMNINGAR (Svenska ·
// English · Norsk bokmål · Suomi) och översätts ALDRIG: en språkväljare
// som visar "Finska" för den som inte läser svenska är oanvändbar.
void StartWindow::syncSprak()
{
    std::vector<std::string> sprak;
    int                      vald = 0;
    for (int i = 0; i < glaze::languageCount(); ++i) {
        const glaze::LanguageInfo li = glaze::languageAt(i);
        sprak.push_back(li.name);
        if (li.tag == glaze::language())
            vald = i;
    }
    sprakDd.setItems(std::move(sprak));
    // `setSelectedIndex` fyrar ALDRIG `onChange` (kontraktet i
    // dropdown.h), så startspråket ur OS-lokalen visas utan att trigga
    // ett byte — och samma rad kan användas EFTER ett byte utan att loopa.
    sprakDd.setSelectedIndex(vald);

    temaDd.setItems({trs(demo::str::LageSystem), trs(demo::str::LageLjust),
                     trs(demo::str::LageMorkt)});
    temaDd.setSelectedIndex(themeMode() == glaze::ThemeMode::System  ? 0
                            : themeMode() == glaze::ThemeMode::Light ? 1
                                                                     : 2);
}

void StartWindow::sprakDdChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    const int i = static_cast<glaze::Dropdown*>(sender)->selectedIndex();
    if (i < 0 || i >= glaze::languageCount())
        return;
    const std::string tagg = glaze::languageAt(i).tag;
    // ⚠ POSTAS, aldrig anropas direkt — se noten i `start_window.h` och
    // kontraktet i `glaze/language.h`. `Dropdown::commit` fyrar det här
    // eventet MITT I popup-fönstrets klick-dispatch, och `setLanguage`
    // river popups synkront.
    glaze::app().post([tagg] { glaze::setLanguage(tagg); });
}

// Ramverket har redan tryckt om titeln och ritat om fönstret; det som
// återstår är det som INTE är `TrText` — listornas items och de texter
// appen själv satt ihop av flera delar.
void StartWindow::onLanguageChanged()
{
    syncSprak();
    syncThemeAndStyleDropdowns();
    skjutVarde.setText(trs(demo::str::SliderPrefix) +
                       std::to_string(static_cast<int>(skjut.value())));
    status.setText(namn.text().empty()
                       ? trs(demo::str::IngaKlick)
                       : trs(demo::str::HejPrefix) + namn.text() +
                             trs(demo::str::HejSuffix));
    temaBtn.setText(glaze::tr(themeMode() == glaze::ThemeMode::System
                                  ? demo::str::TemaSystem
                              : themeMode() == glaze::ThemeMode::Light
                                  ? demo::str::TemaLjust
                                  : demo::str::TemaMorkt));
}

// ------------------------------------------------------------- MENYERNA
//
// Innehållet byggs i KOD och i `onLoad` — aldrig i konstruktorn (den är
// genererad plumbing) och aldrig i `.glazeui` (ett träd av structar med
// handlers har ingen textform i grammatiken, så `setMenus(...)` hade blivit
// en OPAK rad). Det som står i den
// genererade filen är därför bara `menuBar.show();` och kopplingen av
// snabbmenyn.
// MENYERNAS STRUKTUR står i `.glazeui` (hård regel 8 — den går att bygga
// i BÅDA vägarna, och demon visar designer-vägen). Kvar här är bara det
// som ÄR kod: handlerna och snabbmenyn.
void StartWindow::byggMenyer()
{
    menuBar.setHandler({glaze::tr(demo::str::MenuArkiv), glaze::tr(demo::str::NyttFonster)},
                       [this] { second_.show(*this); });
    menuBar.setHandler({glaze::tr(demo::str::MenuArkiv), glaze::tr(demo::str::AllaWidgets)},
                       [this] { galleri_.show(*this); });
    menuBar.setHandler({glaze::tr(demo::str::MenuArkiv), glaze::tr(demo::str::MenuAvsluta)},
                       [this] { close(); });

    menuBar.setHandler(lagePath(demo::str::LageSystem),
                       [this] { setThemeMode(glaze::ThemeMode::System);
                                syncMenyTillstand(); });
    menuBar.setHandler(lagePath(demo::str::LageLjust),
                       [this] { setThemeMode(glaze::ThemeMode::Light);
                                syncMenyTillstand(); });
    menuBar.setHandler(lagePath(demo::str::LageMorkt),
                       [this] { setThemeMode(glaze::ThemeMode::Dark);
                                syncMenyTillstand(); });
    menuBar.setHandler(animeraPath(), [this] {
        // Menyn har REDAN togglat sin egen bock (aktiveringstabellen
        // ändrar tillståndet FÖRE handlern) — här speglas den till
        // kryssrutan, som är appens sanning.
        animate.setChecked(!animate.isChecked());
        setAnimating(animate.isChecked());
    });
    menuBar.setHandler({glaze::tr(demo::str::MenuHjalp), glaze::tr(demo::str::MenuOm)},
                       [this] {
        status.setText(glaze::tr(demo::str::OmText));
    });

    // PROGRAMMENYN — den stora ikonen i hörnet. Strukturen
    // står i `.glazeui` (`appIcon.addItem({}, …)`, roten är den tomma
    // stigen), och en rotpost adresseras med sin EGEN text som enda
    // segment. Samma handlingar som Arkiv- och Hjälp-menyn: programmenyn
    // ersätter inget i menyraden, och genvägarna bor kvar där.
    appIcon.setHandler(glaze::tr(demo::str::NyttFonster),
                       [this] { second_.show(*this); });
    appIcon.setHandler(glaze::tr(demo::str::AllaWidgets),
                       [this] { galleri_.show(*this); });
    appIcon.setHandler(glaze::tr(demo::str::MenuOm), [this] {
        status.setText(glaze::tr(demo::str::OmText));
    });
    appIcon.setHandler(glaze::tr(demo::str::MenuAvsluta), [this] { close(); });

    // SNABBMENYN byggs i KOD, för den hänger inte på menyraden och har
    // därför ingen `.glazeui`-bärare. Den är en MEDLEM och inte en lokal
    // variabel: `popupAt` är icke-blockerande.
    snabbmeny_.addItem(glaze::tr(demo::str::RensaStatus)).onClick = [this] {
        status.setText(glaze::tr(demo::str::IngaKlick));
    };
    snabbmeny_.addItem(glaze::tr(demo::str::KopieraNamnet)).onClick = [this] {
        status.setText(namn.text());
    };
    snabbmeny_.addSeparator();
    snabbmeny_.addCheck(glaze::tr(demo::str::AnimeraRutan)).onClick = [this] {
        animate.setChecked(!animate.isChecked());
        setAnimating(animate.isChecked());
    };

    syncMenyTillstand();
}

void StartWindow::syncMenyTillstand()
{
    // Menyns tillstånd är DATA appen äger — ramverket synkar det aldrig
    // åt en. Stigarna är samma som i `.glazeui`, så de två filerna kan
    // läsas mot varandra.
    const glaze::ThemeMode m = themeMode();
    if (auto* it = menuBar.itemAtPath(lagePath(demo::str::LageSystem)))
        it->checked = (m == glaze::ThemeMode::System);
    if (auto* it = menuBar.itemAtPath(lagePath(demo::str::LageLjust)))
        it->checked = (m == glaze::ThemeMode::Light);
    if (auto* it = menuBar.itemAtPath(lagePath(demo::str::LageMorkt)))
        it->checked = (m == glaze::ThemeMode::Dark);
    if (auto* it = menuBar.itemAtPath(animeraPath()))
        it->checked = animate.isChecked();
    if (glaze::MenuItem* anim = snabbmeny_.itemAt({3}))
        anim->checked = animate.isChecked();
}

void StartWindow::snabbmenyBegard(glaze::Widget*, glaze::ContextMenuArgs& e)
{
    // MUTERA FÖRE popupAt: presentationen tar sin SNAPSHOT när menyn
    // öppnas, och en ändring efteråt slår inte igenom i den öppna menyn.
    snabbmeny_.items[1].enabled = !namn.text().empty();
    syncMenyTillstand();
    snabbmeny_.popupAt(prova, e.x, e.y);
}

// Nivå 1-komponenterna — en rad var, så alla sex går att prova.
void StartWindow::kompLankClicked(glaze::Widget*, glaze::ClickArgs&)
{
    // Handlern körs FÖRE gfx::openUrl — url:en är valfri, klicket är alltid
    // appens först.
    status.setText(glaze::tr(demo::str::LankenOppnas));
}

void StartWindow::valChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Gruppen (valA/valB, group "demo") sköter sig själv — bara den som
    // BLEV vald fyrar, och värdet läses ur sender (regel 6).
    status.setText(trs(demo::str::ValdePrefix) +
                   static_cast<glaze::Radio*>(sender)->text());
}

void StartWindow::skjutChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    auto* s = static_cast<glaze::Slider*>(sender);
    skjutVarde.setText(trs(demo::str::SliderPrefix) +
                       std::to_string(static_cast<int>(s->value())));
    forlopp.setValue(s->value());   // bestämt förlopp följer reglaget
}

void StartWindow::obestamdChanged(glaze::Widget*, glaze::ChangeArgs&)
{
    // Obestämt läge ANIMERAR (setAnimating i widgeten) — kryssa ur och
    // appen slutar rita om igen.
    forlopp.setIndeterminate(obestamd.isChecked());
}

void StartWindow::okBtnClicked(glaze::Widget*, glaze::ClickArgs&)
{
    ++clicks_;
    status.setText(trs(demo::str::AntalKlick) + std::to_string(clicks_));
}

void StartWindow::okBtnHovered(glaze::Widget*, glaze::HoverArgs& e)
{
    if (e.entered && clicks_ == 0)
        status.setText(glaze::tr(demo::str::MusenOver));
}

void StartWindow::animateChanged(glaze::Widget*, glaze::ChangeArgs&)
{
    // Nya värdet läses ur widgeten (C#-mönstret: args bär ingen nyttolast)
    setAnimating(animate.isChecked());   // av = appen sover, ~0 % CPU
    syncMenyTillstand();                 // menyns kryss speglar rutan
}

void StartWindow::temaBtnClicked(glaze::Widget*, glaze::ClickArgs&)
{
    // Cykla System -> Light -> Dark. System följer OS:ets ljus/mörkt vid
    // varje repaint; Light/Dark låser valet. Bytet är bara en invalidate.
    using glaze::ThemeMode;
    switch (themeMode()) {
    case ThemeMode::System:
        setThemeMode(ThemeMode::Light);
        temaBtn.setText(glaze::tr(demo::str::TemaLjust));
        break;
    case ThemeMode::Light:
        setThemeMode(ThemeMode::Dark);
        temaBtn.setText(glaze::tr(demo::str::TemaMorkt));
        break;
    default:
        setThemeMode(ThemeMode::System);
        temaBtn.setText(glaze::tr(demo::str::TemaSystem));
        break;
    }
    syncMenyTillstand();   // menyns radiogrupp är DATA appen äger
}

void StartWindow::namnChanged(glaze::Widget*, glaze::ChangeArgs&)
{
    // Nya texten läses ur widgeten (C#-mönstret — args bär ingen nyttolast)
    status.setText(namn.text().empty() ? trs(demo::str::IngaKlick)
                                       : trs(demo::str::HejPrefix) +
                                             namn.text() +
                                             trs(demo::str::HejSuffix));
}

void StartWindow::nyttFonsterClicked(glaze::Widget*, glaze::ClickArgs&)
{
    // ÄGT fönster: stängs huvudfönstret kaskad-stängs det här också.
    // Redan öppet → show() lyfter det bara.
    second_.show(*this);
}

// Widget-galleriet: alla nitton widgets i normalt OCH inaktiverat läge, så
// ett byte i väljarna nedanför går att bedöma på en skärm. ÄGT fönster —
// kaskad-stängs med startfönstret; redan öppet → show() lyfter det bara.
void StartWindow::galleriClicked(glaze::Widget*, glaze::ClickArgs&)
{
    galleri_.show(*this);
}

// Samma tema-väljare som Om-dialogens dropdown, fast på den ICKE-modala
// första sidan — isolerar om popup-listans mus-problem på Windows hör
// ihop med att ägarfönstret är modalt. Värdet läses ur sender (regel 6).
void StartWindow::temaDdChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    auto* d = static_cast<glaze::Dropdown*>(sender);
    setThemeMode(d->selectedIndex() == 0   ? glaze::ThemeMode::System
                 : d->selectedIndex() == 1 ? glaze::ThemeMode::Light
                                           : glaze::ThemeMode::Dark);
    syncMenyTillstand();
}

// ------------------------------------------------- de tre utseende-axlarna
//
// UI (ritning) / Theme (färg) / Style (mått) är TRE OBEROENDE axlar, och
// demons uppgift är att visa just det: varje axel har en egen väljare, och
// läget (System/Ljust/Mörkt) är en fjärde som inte blandas in.
//
// Att välja UI återställer de två andra till familjens defaults —
// därför fyller kaskaden om dem här.

namespace {

glaze::UI uiByIndex(int i)
{
    return i == 1 ? glaze::UI::Slate
           : i == 2 ? glaze::UI::Minimalistic
                    : glaze::UI::Glaze;
}

} // namespace

// Kaskaden: fyll om tema- och mått-väljarna för AKTUELL UI och markera
// det som faktiskt gäller. Körs både vid UI-byte och i onLoad, så start och
// drift inte kan glida isär.
//
// Glaze har med flit BARA ett tema. Dropdownen döljs INTE då — att en axel
// finns men bara har ett val är information, inte brus.
void StartWindow::syncThemeAndStyleDropdowns()
{
    const glaze::UI u = ui();

    // Teman och styles hör till SITT UI — listorna hämtas därför ur det
    // valda UI:t, aldrig ur en global lista. Namn får sammanfalla mellan
    // UI:n ("Regular" finns tre gånger, "Neutral" två) och betyder olika
    // saker; det är namnet PLUS ritningen som identifierar en palett.
    std::vector<std::string> teman;
    int                      themeSel = 0;
    for (int i = 0; i < glaze::themeCount(u); ++i) {
        const glaze::Theme& t = glaze::themeAt(u, i);
        teman.push_back(t.name);
        if (t.name == theme().name)
            themeSel = i;
    }
    themeDd.setItems(std::move(teman));
    themeDd.setSelectedIndex(themeSel);

    std::vector<std::string> matt;
    int                      styleSel = 0;
    for (int i = 0; i < glaze::styleCount(u); ++i) {
        const glaze::Style& s = glaze::styleAt(u, i);
        matt.push_back(s.name);
        // NAMNET jämförs, aldrig adressen och aldrig pekaren. `style()`
        // svarar med App::ThemeState::style — ett VÄRDE, alltså en kopia —
        // så `&s == &style()` kunde aldrig träffa. Och `Style::name` är
        // `const char*` (`Theme::name` är std::string), så ett naket
        // `s.name == style().name` hade jämfört PEKARE: samma bugg i ny
        // kostym, grön så länge kopian bär samma literal. string_view är
        // det som faktiskt jämför tecknen. Unikheten inom ett UI är grindad
        // i tests/contrast_gate.cpp.
        if (std::string_view(s.name) == std::string_view(style().name))
            styleSel = i;
    }
    styleDd.setItems(std::move(matt));
    styleDd.setSelectedIndex(styleSel);
}

// UI-bytet: setUI sätter ritningen OCH återställer tema + mått till
// familjens defaults. Bara BUTTON har än så länge en egen `case
// UI::Minimalistic:`/`case UI::Glaze:` — övriga widgets faller på
// `default:` och ritar Slates FORM i den valda familjens färger och mått.
// Det är väntat, inte en bugg.
void StartWindow::uiDdChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    setUI(uiByIndex(static_cast<glaze::Dropdown*>(sender)->selectedIndex()));
    syncThemeAndStyleDropdowns();
}

// Färgaxeln: en UI kan ha FLERA teman (Slate har tio, Minimalistic nio).
// Listan kommer ur themeAt() — EN sanning, samma som defaultTheme läser.
void StartWindow::themeDdChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    setTheme(glaze::themeAt(
        ui(), static_cast<glaze::Dropdown*>(sender)->selectedIndex()));
}

// Måttaxeln: byter storlek UTAN att lämna familjen. Style äger UI-fontens
// storlek, så ramverket laddar om fonten i alla öppna fönster — texten byter
// storlek och autostorlekarna räknas om.
void StartWindow::styleDdChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    setStyle(glaze::styleAt(
        ui(), static_cast<glaze::Dropdown*>(sender)->selectedIndex()));
}

// Användarskalan: multiplikator ovanpå OS-skalan — ALLT växer,
// den logiska storleken är invarianten. App-global som temat; värdet
// läses ur sender (regel 6).
void StartWindow::skalaDdChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    static constexpr float kSkalor[] = {0.8f, 0.9f, 1.0f, 1.1f, 1.25f};
    auto* d = static_cast<glaze::Dropdown*>(sender);
    if (d->selectedIndex() >= 0 && d->selectedIndex() < 5)
        glaze::app().setUserScale(kSkalor[d->selectedIndex()]);
}

void StartWindow::stangClicked(glaze::Widget*, glaze::ClickArgs&)
{
    close();
}

bool StartWindow::event(const gfx::Event& e)
{
    // F12 togglar damage-debug-overlayen (ramverket stjäl inga tangenter
    // självt — appen väljer sin bindning).
    if (e.type == gfx::EventType::KeyDown && e.key == gfx::Key::F12) {
        setDebugDamage(!debugDamage());
        return true;
    }
    return false;
}

void StartWindow::frame(const glaze::Ctx& c)
{
    // Pulserande ruta — visar att egen ritning samsas med widgetarna
    static float t = 0.0f;
    if (animate.isChecked())
        t = static_cast<float>(gfx::ticksMs()) / 1000.0f;

    const float size = (80.0f + 20.0f * std::sin(t * 2.0f)) * c.scale;
    const gfx::Rectf box{c.area.x + c.area.w / 2.0f - size / 2.0f,
                         c.area.y + c.area.h / 2.0f - size / 2.0f,
                         size, size};
    c.r->fillRoundedRect(box, 8.0f * c.scale, c.theme->colors.accent);
}
