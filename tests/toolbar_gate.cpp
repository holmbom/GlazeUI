// toolbar_gate — VERKTYGSFÄLTETS grind.
//
// Kör:  ./build/tests/toolbar_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display, eller popupfönster kom aldrig upp)
//
// TVÅDELAD, menu_gates form: fall 0–7, 9, 10
// och 11 är HEADLESS — inga fönster öppnas, ingen pump, ingen `gfx::init` — och
// fall 8, 12 och 13 öppnar var sitt DOLT fönster (`setOffscreen`), för en meny
// öppnas bara i ett öppet fönster. (Banden i en dockningsyta mäts av dock_gate.) Det som mäts är det som är SVÅRT att se med ögat —
// kontrakten:
//
//   1. KLICKKONTRAKTET. En vanlig verktygsknapp fyrar bara `onClick`. En
//      växlingsbar slår om läget och fyrar `onChange` FÖRE `onClick`
//      (Checkboxens ordning — båda ser det nya läget). Programmatisk
//      `setChecked` fyrar ALDRIG något.
//   2. FOKUS. Verktygsknappen är inget tabbstopp — men ett FÄLT i remsan
//      är det, för remsans barn är riktiga widgets.
//   3. PLACERINGEN. Remsans luft (`toolBarPadX/Y`) och luckan MELLAN synliga
//      barn (`toolGap`) — förväntade värden räknade för hand, inklusive att ett DOLT
//      barn varken tar plats eller lucka och att en Spacer slutar exakt
//      vid innerkanten (luckorna är med i flexens fasta summa). Och att en
//      vanlig `Bar` (lucka 0) placerar OFÖRÄNDRAT. Och REN RAD (
//      `setPlain`): ett barn med satt höjd 20 står centrerat i barens 32, en
//      Spacer fyller baren — ritningen utan yta och hårlinje mäts INTE här
//      (pixel_gate), inte heller den stående varianten (samma `crossSpan`).
//      Sabotage: `crossSpan` utan `plain_`-grenen (barnet fyller 32).
//   4. HÄRLEDNINGEN i style.cpp: en ikonknapps naturliga höjd ÄR remsans
//      innerhöjd, i alla tre UI. Det står som en mening vid talen; här är
//      meningen grindad, så ett ändrat `toolBarH` utan att knappen följer
//      med fäller bygget.
//   5. IKONENS OPACITET (`Content::imgOpacity`): en inaktiv
//      ikonknapp ritar bilden med opacitet 1 − `iconFade` och INGENTING
//      över den, en aktiv med full opacitet — i alla tre UI, ikryssad som
//      oikryssad, och likadant för `Button`. Tonningen är ingen
//      slöja i ytans färg (den syntes som en ruta på en panel). Det här
//      mäter kontraktet widget → Draw → Renderer; att SHADERN gör något av
//      opaciteten mäter pixel_gate (verktygsknappen måste visa sitt
//      inaktiva läge där).
//   6. BILDER UR PIXLAR: en verktygsknapp och
//      en knapp med en `glaze::Bitmap` ritar EN bild med punkt 5:s
//      opacitetsregel och ingenting över den, i alla tre UI. En `Picture`
//      med en 64 × 64-bild mäter 64 / skala logiska px (64 FYSISKA på både
//      skala 1 och 2 — DPI-följden) utan att ladda upp något
//      (`createImage` räknas), och `setImg` med samma bild invaliderar bara
//      efter `setPixels` (versionen i bildvärdet).
//      Sabotagen (skrivna): `Draw::glyph`
//      som bara tar assets (inga bilder), `imageSize` som räknar Bitmapens
//      pixlar som logiska px (64 på skala 2), mätningen genom cachen
//      (`createImage` > 0) och bildvärdets versionssnapshot borttagen (ingen
//      invalidering efter `setPixels`).
//   7. RULLGARDINEN, HEADLESS: med poster växer
//      AUTO-bredden med EXAKT `toolArrowW` i alla tre UI (en satt bredd
//      vinner), och i en remsa flyttar grannen lika mycket. Menyns platta
//      form med `{}` som roten — alla fem add-satserna, en okänd och en
//      UNDERKÄND stig som aldrig landar i roten, `setHandler` vars sender är
//      knappen — och postens tre tillstånd som SAMMA tillstånd som
//      trädformen. `setProp("split")`.
//   8. RULLGARDINEN MED FÖNSTER (riktiga tryck genom `handleEvent`): den
//      första posten ritar om remsan (grannarna flyttar) och nästa bara
//      knappen; ett tryck öppnar menyn och ger inget `onClick`, ett tryck till stänger den
//      (toggle); `onDropDown` fyras FÖRE öppningen — handlern ser menyn
//      stängd, och posten den lägger till STÅR I POPUPENS ÖGONBLICKSBILD
//      (popupens rader läses ur fönstret, inte ur menyns data); den DELADE
//      knappens huvuddel klickar och växlar, pildelen öppnar utan klick och
//      utan växling; en växlingsbar odelad knapp med poster växlar inte; en
//      INAKTIVERAD knapp gör ingenting; `openMenu()` öppnar utan
//      `onDropDown`, och `disable()` stänger en öppen meny. En PROBE (en
//      vanlig `Menu::popupAt`) avgör först om popuper kan öppnas här — kan
//      de inte är det 77, inte ett fel.
//      Sabotagen:
//        · `clicked()` utan `menuPress_`-vakten ............ 8 (onClick-räknarna)
//        · `onDropDown` fyrad EFTER `openMenu()` ........... 8 (ögonblicksbilden,
//                                                              "handlern såg stängd")
//        · `contentSize` utan `toolArrowW` ................. 7 (bredderna)
//        · pildelens träff som hela knappen (`split_` ignorerad) 8 (huvuddelens klick)
//        · `enabledChanged` utan `closeMenu()` ............. 8 (disable stänger)
//        · `itemsAdded` utan `relayoutSiblings` ............ 8 (remsans stämpel)
//        · `onChainClosed` som släcker utan `!menu_.isOpen()` .. —  (fångas INTE:
//          kräver två kedjor i samma pumpvarv, se VAD DEN INTE MÄTER)
//      `onDropDown` saknad i `isKnownEvent` fälls av roundtrip_gate
//      (rullgardinsblocket), inte här.
//   9. STÅENDE FÄLT OCH EGEN LÄNGD (D15/D16, headless): den
//      stående flexen handräknad (tre knappar, en separator, luckor och
//      luften som byter axel), separatorn TVÄRS baren i båda riktningarna
//      (mätt och inspelad), verktygsfältets egna längd liggande och stående,
//      en satt bredd som vinner och ett fält med `Dock::None` och bara
//      vänsterankare som nu syns. Den liggande flexen mäts oförändrad av
//      fall 3.
//      Sabotagen:
//        · den stående grenen mäter `.w` i stället för `.h` .. 9 (knapparna)
//        · separatorn följer `vertical_` och inte baren ....... 9 (tunn längs,
//                                                               strecket)
//        · `ToolBar::contentSize` utan egen längd (bredd 0) ... 9 (79, 27)
//        · luften byter inte axel stående .................... 9 (innerytan)
//        · den liggande grenen ändras (luckan dubbelräknas) ... 3
//  10. DET FLYTANDE FÄLTET (headless): ett flytande
//      `Dock::Left`-fält LIGGER (`isVertical()` false), bredd och höjd byts
//      fram och tillbaka, en separator i det ritas lodrätt (tvärs den
//      liggande baren) och fältet ritar ingen hårlinje.
//      Sabotagen:
//        · `ToolBar::isVertical` utan `!floating_` .......... 10 (ligger inte,
//                                                              separatorn)
//        · `turnIfNeeded` borttagen ur `setFloating` ........ 10 (bredd/höjd)
//        · `drawsInnerEdge` svarar alltid true .............. 10 (hårlinjen)
//  11. ÖVERFLÖDET, HEADLESS: ett liggande fält i 120 med A, en
//      spacer, BB, en separator, CCC och DDDD handräknat — de första packas
//      från början (spacern växer inte), separatorn som slutar prefixet och
//      de som inte ryms får en tom rekt men behåller `visible`, och knappen
//      står 20 lång i fältets ände; ryms barnen är det Bars flex (knappen
//      utan yta, spacern tar det lediga); `setOverflow(false)` och
//      `setProp("overflow")`; knappen går inte att lossa; ett överskott på en
//      halv pixel flödar inte över men en hel gör det; det stående fältet; och
//      den egna längden utan knappen.
//      Sabotagen:
//        · `inFlow` utan knappen (Bars flex räknar den) .... 3, 9, 11 (luckan,
//                                                              längden)
//        · prefixet får sluta på en separator ............. 11 (separatorn syns)
//        · `std::floor(total - yta) <= 0` → `total <= yta` . 11 (halv pixel)
//        · knappen placerad efter prefixet, inte i änden .. 11 (96)
//        · `canReleaseChild` borttagen ..................... 11 (lossas)
//        · `setOverflow` lagrar men `overflowCut` läser inte  11 (rinner över)
//  12. ÖVERFLÖDET MED FÖNSTER (riktiga tryck): i ett 300 brett fönster ett
//      prefix av åtta textknappar synligt och resten — alltid de tio sista
//      sakerna — dolda, knappen i fältets ände och inget tabbstopp medan det
//      dolda sökfältet är ett; trycket fyrar den dolda rullgardinens
//      `onDropDown` EN gång och öppnar menyn; posterna: de dolda knapparna i
//      ordning, två separatorer i rad som EN (aldrig först eller sist), den
//      växlingsbara kryssbar, den inaktiverade släckt, ikonknappen med sitt
//      verktygstips och sin ikon, rullgardinen som undermeny MED den dynamiska
//      posten, den delade knappen som klick + skiljelinje + poster, ingen
//      post för etiketten, sökfältet med sitt verktygstips och dropdownen som
//      "Kontroll"; popupens rader är menyns; aktiveringen klickar knappen med
//      knappen som sender, växlar, gör ingenting för en släckt post och går
//      genom rullgardinens och den delade knappens egen meny med knappen som
//      sender. Kontrollens post ger fokus och fältet VISAR sökfältet före
//      knappen och ritas om (`childFocusChanged`), fokus utanför döljer det
//      igen, en dold dropdown som får fokus syns, och `setOverflow(false)`
//      tar bort knappen.
//      Sabotagen:
//        · `fillOverflowMenu` utan `onDropDown`-passet ..... 12 (nyDrop,
//                                                              "Senast")
//        · `buttonItem` med `textToken()` alltid ........... 12 (ikonknappen)
//        · separatorerna utan dubblettvakt ................. 12 (två i rad)
//        · `mirrorItems` med överflödsknappen som sender .... 12 (avs)
//        · `controlItem` utan `kanTaFokus` .................. 12 (etiketten)
//        · `overflowCut` utan `revealed` ................... 12 (sökfältet
//                                                              syns inte)
//        · `childFocusChanged` utan `invalidate()` ......... 12 (stämpeln)
//        · `AppWindow::postFocusEvents` utan kroken ........ 12 (stämpeln)
//  13. PRICKARNA (pixlar, eget dolt fönster): ett fält utan barn är
//      enfärgat i sin inre yta (varje pixel mot samma rads mittpixel, ±2),
//      i en full bild och efter en partiell omritning — överflödsknappens
//      ikon ligger i sin egen rekt och inte kring fältets hörn.
//      Sabotagen:
//        · spärren bort i `drawTree` ....................... 13 (full)
//        · spärren bort i `drawTreePartial` ................ 13 (partiell)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   - hur det SER UT — färgerna per UI och tillstånd verifieras renderat
//     (glaze-render) och av pixel_gate, inte här;
//   - hover/tryck-ritningen — den kräver musen;
//   - att `AppWindow` låter fokus stå kvar vid ett KLICK på verktygsknappen.
//     Det som mäts är förutsättningen (`focusable()` är false) och
//     Tab-listan; fall 8 trycker men frågar inte fokus — det provas för hand;
//   - RULLGARDINEN: var menyn hamnar på skärmen (under knappen — backendens
//     popupankare, doc/os/*/TESTS.md), hur pilen och skiljelinjen SER UT,
//     att en post AKTIVERAD med musen i popupen får knappen som sender
//     (popupens rader är interna i menu.cpp — headless-fallet mäter
//     `activatePath` med knappen som sender, inte popupens klick), och två
//     kedjor i SAMMA pumpvarv (ett tryck som stänger och ett som öppnar igen
//     innan svansen rivit den gamla) — `onChainClosed`s `isOpen`-vakt är
//     skriven ur koden i menu.cpp (`finish` tar bort presentationen ur listan
//     före kroken) och provas inte här.
//   - `App::dispatch`s popupstängning vid ett tryck (`closePopups`): fall 8
//     matar `handleEvent` direkt, så toggle-vägen som mäts är knappens egen
//     `closeMenu()`, inte App:s köade stängning.
//   - att grannarna i remsan RITAS OM med rätt pixlar när den första posten
//     gör knappen bredare — fall 7 mäter placeringen och fall 8 att remsan
//     invalideras (`damageStamp`), inte vad som hamnar på skärmen.
//   - ÖVERFLÖDET: hur knappen och menyn SER UT (ikonen, plattan, att pilen
//     saknas), var menyn hamnar, att en post AKTIVERAD med musen i popupen
//     går samma väg som `activatePath` (popupens rader är interna), banden i
//     en dockningsyta (dock_gate B7–B8), en delad knapp vars `onDropDown`
//     river fältet mitt i byggandet, och Tab genom flera dolda kontroller i
//     rad (fall 12 sätter fokus med `focus()` — Tab-listan är densamma).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

#include "image_cache.h"   // intern — opaciteten behöver en riktig bildcache

namespace {

int  g_fails   = 0;
int  g_skips   = 0;   // överhoppade block (fall 8) — SYNLIGA i sammanfattningen
int  g_cases   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

bool nära(float a, float b) { return std::fabs(a - b) < 0.01f; }

std::string rekt(const gfx::Rectf& r)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "{%g, %g, %g, %g}", r.x, r.y, r.w, r.h);
    return buf;
}

bool lika(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return nära(a.x, b.x) && nära(a.y, b.y) && nära(a.w, b.w) &&
           nära(a.h, b.h);
}

// Fast teckensteg per kodpunkt — som i measure_gate. Fonten talar
// FYSISKA px och skapas per skala, precis som AppWindow::reloadFont gör.
class FalskFont final : public gfx::NullFont {
public:
    FalskFont(float adv, float h) : adv_(adv), h_(h) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        std::size_t n = 0;
        for (char ch : s)
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
                ++n;
        return gfx::Sizef{adv_ * static_cast<float>(n), h_};
    }
    float height() const override { return h_; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string&, std::size_t bytes) const override
    {
        return adv_ * static_cast<float>(bytes);
    }
    std::size_t byteIndexAtX(const std::string& s, float) const override
    {
        return s.size();
    }
    std::size_t fitWidth(const std::string& s, float) const override
    {
        return s.size();
    }

private:
    float adv_;
    float h_;
};

// Mätaren — `contentSize` är protected; `Widget::measure` når en godtycklig
// widget (som i measure_gate).
struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

glaze::Ctx ctxFör(glaze::UI ui, gfx::Font& f)
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = 1.0f;
    c.ui          = ui;
    c.style       = &glaze::defaultStyle(ui);
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 400.0f, 300.0f};
    return c;
}

// ------------------------------------------------------------ 0. registret

void fallRegistret()
{
    auto bar = glaze::makeWidget("toolbar");
    auto btn = glaze::makeWidget("toolbutton");
    check(bar && dynamic_cast<glaze::ToolBar*>(bar.get()) != nullptr,
          "registret: \"toolbar\" ger en riktig glaze::ToolBar");
    check(btn && dynamic_cast<glaze::ToolButton*>(btn.get()) != nullptr,
          "registret: \"toolbutton\" ger en riktig glaze::ToolButton");
    check(bar && bar->dock == glaze::Dock::Top,
          "ToolBar dockar Top som default (WinForms ToolStrip.Dock)");
}

// ------------------------------------------------------ 1. klickkontraktet

// `clicked()` är protected — en subklass får anropa den, och testet
// slipper fejka AppWindows event-routing (som i dropdown_gate).
struct ProvKnapp : glaze::ToolButton {
    using glaze::ToolButton::clicked;
};

void fallKlick()
{
    // VANLIG knapp: bara onClick, läget orört.
    {
        ProvKnapp   b;
        std::string spår;
        b.onClick  = [&] { spår += "klick "; };
        b.onChange = [&] { spår += "ändring "; };
        b.clicked();
        check(spår == "klick ", "vanlig knapp: bara onClick", spår);
        check(!b.isChecked(), "vanlig knapp: läget orört");
    }

    // VÄXLINGSBAR: onChange FÖRE onClick, och båda ser det NYA läget.
    {
        ProvKnapp   b;
        std::string spår;
        b.setCheckable(true);
        b.onChange = [&](glaze::Widget* s, glaze::ChangeArgs&) {
            spår += static_cast<glaze::ToolButton*>(s)->isChecked()
                        ? "ändring(på) "
                        : "ändring(av) ";
        };
        b.onClick = [&](glaze::Widget* s, glaze::ClickArgs&) {
            spår += static_cast<glaze::ToolButton*>(s)->isChecked()
                        ? "klick(på) "
                        : "klick(av) ";
        };
        b.clicked();
        check(b.isChecked(), "växlingsbar: första klicket slår PÅ");
        check(spår == "ändring(på) klick(på) ",
              "växlingsbar: onChange före onClick, båda ser nya läget", spår);
        spår.clear();
        b.clicked();
        check(!b.isChecked(), "växlingsbar: andra klicket slår AV");
        check(spår == "ändring(av) klick(av) ",
              "växlingsbar: samma ordning åt andra hållet", spår);
    }

    // PROGRAMMATISKT: aldrig ett event — kontraktet för varje widget.
    {
        ProvKnapp b;
        int       n = 0;
        b.setCheckable(true);
        b.onChange = [&] { ++n; };
        b.onClick  = [&] { ++n; };
        b.setChecked(true);
        b.setChecked(false);
        b.setChecked(true);
        check(n == 0, "programmatisk setChecked fyrar ingenting",
              std::to_string(n));
        check(b.isChecked(), "programmatisk setChecked sätter läget");
        // Läget är ett EGET fält: att slå av växlingen rör det inte.
        b.setCheckable(false);
        check(b.isChecked(), "setCheckable(false) lämnar checked orört");
    }

    // setProp-vokabulären — formen previewvägen levererar.
    {
        glaze::ToolButton b;
        check(b.setProp("checkable", "true") && b.isCheckable(),
              "setProp: checkable");
        check(b.setProp("checked", "true") && b.isChecked(),
              "setProp: checked");
        check(b.setProp("imagePos", "below") &&
                  b.imagePos() == glaze::ImagePos::Below,
              "setProp: imagePos");
        check(!b.setProp("imagePos", "diagonal"),
              "setProp: okänd imagePos svarar false");
        check(!b.setProp("img", "utan-snedstreck"),
              "setProp: img utan lista/nyckel-form svarar false");
        check(b.setProp("visible", "false") && !b.isVisible(),
              "setProp: basens nycklar nås (basen anropas SIST)");
    }
}

// --------------------------------------------------------------- 2. fokus

struct Vard : glaze::AppWindow {
    Vard() : glaze::AppWindow("toolbar_gate", 400, 300) {}
    using glaze::AppWindow::add;
    using glaze::AppWindow::focusOrder;
};

void fallFokus()
{
    glaze::ToolButton b;
    check(!b.focusable(), "verktygsknappen är inte fokuserbar");

    Vard w;
    auto* före = w.add<glaze::Button>("före");
    auto* tb   = w.add<glaze::ToolBar>();
    tb->add<glaze::ToolButton>("Ny");
    auto* fält = tb->add<glaze::TextInput>();
    tb->add<glaze::ToolButton>("Spara");
    auto* efter = w.add<glaze::Button>("efter");

    const std::vector<glaze::Widget*> ordning = w.focusOrder();
    check(ordning.size() == 3 && ordning[0] == före && ordning[1] == fält &&
              ordning[2] == efter,
          "Tab-listan: knapparna i remsan är INGA stopp, fältet i remsan ÄR "
          "ett", std::to_string(ordning.size()) + " poster");
}

// ----------------------------------------------------------- 3. placering

void fallPlacering()
{
    const glaze::UI     ui = glaze::UI::Slate;
    const glaze::Style& st = glaze::defaultStyle(ui);
    FalskFont           f(7.0f, st.uiFontSize);
    const glaze::Ctx    c = ctxFör(ui, f);

    // Förväntade Slate-tal (30 / 4 / 3 / 2 / 6) — står de inte där är
    // det tabellen och inte placeringen som ändrats, och då ska den här
    // grinden få det sagt i stället för att räkna om sig själv.
    check(nära(st.toolBarH, 30.0f) && nära(st.toolBarPadX, 4.0f) &&
              nära(st.toolBarPadY, 3.0f) && nära(st.toolGap, 2.0f) &&
              nära(st.toolButtonPadX, 6.0f),
          "facit: Slates verktygsmått är de facit räknats för");

    glaze::ToolBar tb;
    auto* a   = tb.add<glaze::ToolButton>("A");      // 7 + 2·6 = 19
    auto* dold = tb.add<glaze::ToolButton>("B");
    dold->hide();
    auto* sep = tb.add<glaze::Separator>();          // 0 + 2·4 = 8
    sep->setVertical(true);
    auto* d   = tb.add<glaze::ToolButton>("D");      // 19
    auto* sp  = tb.addSpacer();                      // växer

    const gfx::Rectf r = tb.resolve(c);
    check(lika(r, {0.0f, 0.0f, 400.0f, 30.0f}),
          "remsan: Dock::Top, höjden är toolBarH", rekt(r));

    glaze::Ctx cc = c;
    cc.area       = static_cast<const glaze::Widget&>(tb).childArea(c);
    check(lika(cc.area, {4.0f, 3.0f, 392.0f, 24.0f}),
          "remsan: barnens yta är remsan minus luften", rekt(cc.area));

    check(lika(a->resolve(cc), {4.0f, 3.0f, 19.0f, 24.0f}),
          "första barnet: vid vänsterluften, remsans innerhöjd",
          rekt(a->resolve(cc)));
    check(lika(dold->resolve(cc), {23.0f, 3.0f, 0.0f, 24.0f}),
          "dolt barn: ingen bredd OCH ingen lucka", rekt(dold->resolve(cc)));
    check(lika(sep->resolve(cc), {25.0f, 3.0f, 8.0f, 24.0f}),
          "separatorn: EN lucka efter första barnet (det dolda räknas inte)",
          rekt(sep->resolve(cc)));
    check(lika(d->resolve(cc), {35.0f, 3.0f, 19.0f, 24.0f}),
          "tredje synliga: lucka + separatorns bredd + lucka",
          rekt(d->resolve(cc)));
    // 392 − (19 + 8 + 19 + 0) − 3 luckor à 2 = 340 ledigt.
    check(lika(sp->resolve(cc), {56.0f, 3.0f, 340.0f, 24.0f}),
          "spacern slutar EXAKT vid innerkanten (luckorna är fasta)",
          rekt(sp->resolve(cc)));

    // EN VANLIG BAR är oförändrad: lucka 0, ingen luft, barHeight.
    glaze::Bar bar;
    auto* ba  = bar.add<glaze::ToolButton>("A");
    auto* bs  = bar.add<glaze::Separator>();
    bs->setVertical(true);
    auto* bd  = bar.add<glaze::ToolButton>("D");
    auto* bsp = bar.addSpacer();
    bar.dock  = glaze::Dock::Top;
    glaze::Ctx bc = c;
    bc.area       = static_cast<const glaze::Widget&>(bar).childArea(c);
    check(lika(ba->resolve(bc), {0.0f, 0.0f, 19.0f, 32.0f}) &&
              lika(bs->resolve(bc), {19.0f, 0.0f, 8.0f, 32.0f}) &&
              lika(bd->resolve(bc), {27.0f, 0.0f, 19.0f, 32.0f}) &&
              lika(bsp->resolve(bc), {46.0f, 0.0f, 354.0f, 32.0f}),
          "Bar (lucka 0): placeringen är exakt den gamla, kant i kant",
          rekt(bsp->resolve(bc)));

    // REN RAD: barnet behåller sin MÄTTA höjd (en satt 20) centrerad
    // i barens 32, och en Spacer utan eget tvärmått fyller baren.
    // Den liggande flexen är bara oförändrad i x — mätt bredd, samma ordning.
    glaze::Bar rad;
    rad.dock = glaze::Dock::Top;
    rad.setPlain(true);
    auto* radKnapp = rad.add<glaze::Button>("OK");
    radKnapp->height = 20.0f;
    auto* radLucka   = rad.addSpacer();
    glaze::Ctx rc = c;
    rc.area       = static_cast<const glaze::Widget&>(rad).childArea(c);
    const gfx::Rectf k = radKnapp->resolve(rc);
    const gfx::Rectf s = radLucka->resolve(rc);
    check(rad.isPlain() && nära(k.x, rc.area.x) &&
              nära(k.y, rc.area.y + 6.0f * c.scale) &&
              nära(k.h, 20.0f * c.scale),
          "ren rad: barnet behåller sin satta höjd, centrerad i baren", rekt(k));
    check(nära(s.y, rc.area.y) && nära(s.h, rc.area.h) &&
              nära(s.x, k.x + k.w),
          "ren rad: en Spacer utan tvärmått fyller baren, efter barnet", rekt(s));
}

// ----------------------------------------------------- 4. härledningen

void fallHarledning()
{
    // En ikonknapp behöver en GILTIG bild för att mätas som ikon —
    // `Draw::measure` frågar handtaget, inte texturen, så ingen renderer
    // behövs. Demons assets.bin bär listan "toolbar" (16 px).
    const glaze::AssetHandle ok = glaze::img("toolbar", "ok");
    check(ok.valid(), "facit: demons assets.bin bär toolbar/ok");
    if (!ok.valid())
        return;

    for (glaze::UI ui :
         {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic}) {
        const glaze::Style& st = glaze::defaultStyle(ui);
        FalskFont           f(7.0f, st.uiFontSize);
        const glaze::Ctx    c = ctxFör(ui, f);
        const char* namn = ui == glaze::UI::Glaze   ? "Glaze"
                           : ui == glaze::UI::Slate ? "Slate"
                                                    : "Minimalistic";

        glaze::ToolButton ikon;
        ikon.setImg(ok);
        const gfx::Sizef m     = Meter::measure(ikon, c);
        const float      inner = st.toolBarH - 2.0f * st.toolBarPadY;
        check(nära(m.h, inner),
              std::string(namn) + ": ikonknappens naturliga höjd == remsans "
              "innerhöjd (style.cpp:s härledning)",
              std::to_string(m.h) + " mot " + std::to_string(inner));

        glaze::ToolBar tb;
        check(nära(Meter::measure(tb, c).h, st.toolBarH),
              std::string(namn) + ": remsans auto-höjd är toolBarH");
    }
}

// ------------------------------------- IKONSTORLEKEN
//
// Q1–Q2 i alla tre UI: `ToolButton::setIconSize(32)` ger den naturliga höjden
// 32 + 2 × `toolButtonPadY` (ikonen ensam — inget textblock), och fältets
// AUTO-tjocklek den + 2 × `toolBarPadY`; vanliga knappar ger EXAKT
// `toolBarH`, och ett högt barn som inte är en verktygsknapp ändrar inget.
// SABOTAGEN: tjockleken ur alla barn (Q2 — panelen gör fältet tjockt), utan
// max mot `toolBarH` (Q2 — en liten knapp gör fältet tunnare). MÄTER INTE
// banden i en dockningsyta (de mäter med `sizeFor`, samma väg).
void fallIkonstorlek()
{
    const glaze::AssetHandle ok = glaze::img("toolbar", "ok");
    check(ok.valid(), "Q: demons assets.bin bär toolbar/ok");
    if (!ok.valid())
        return;
    for (glaze::UI ui :
         {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic}) {
        const glaze::Style& st = glaze::defaultStyle(ui);
        FalskFont           f(7.0f, st.uiFontSize);
        const glaze::Ctx    c = ctxFör(ui, f);
        const std::string   namn = ui == glaze::UI::Glaze   ? "Glaze"
                                   : ui == glaze::UI::Slate ? "Slate"
                                                            : "Minimalistic";

        glaze::ToolBar    tb;
        glaze::ToolButton vanlig, stor;
        vanlig.setImg(ok);
        stor.setImg(ok);
        tb.adopt(vanlig);
        check(nära(Meter::measure(tb, c).h, st.toolBarH),
              namn + " Q2: vanliga knappar — exakt toolBarH");
        glaze::Panel hög;
        hög.width  = 20;
        hög.height = 200;
        tb.adopt(hög);
        check(nära(Meter::measure(tb, c).h, st.toolBarH),
              namn + " Q2: ett högt barn som inte är en verktygsknapp ändrar inget");

        stor.setIconSize(32.0f);
        const float knapp = 32.0f + 2.0f * st.toolButtonPadY;
        check(nära(Meter::measure(stor, c).h, knapp),
              namn + " Q1: setIconSize(32) ger höjden 32 + 2 × toolButtonPadY",
              std::to_string(Meter::measure(stor, c).h));
        tb.adopt(stor);
        check(nära(Meter::measure(tb, c).h,
                   std::max(st.toolBarH, knapp + 2.0f * st.toolBarPadY)),
              namn + " Q1: fältets AUTO-tjocklek följer den högsta knappen",
              std::to_string(Meter::measure(tb, c).h));
    }
}

// ----------------------------------------------------- 5. ikonens opacitet

// Bild som bara är ett mått — ImageCache får en sådan av renderern.
struct FalskBild final : gfx::NullImage {
    int  width() const override { return 16; }
    int  height() const override { return 16; }
    bool valid() const override { return true; }
};

// Renderer som SPELAR IN fyllda rektar och bilder — med bildens OPACITET.
// `pushClip`/`popClip` är icke-virtuella och klarar sig själva; allt annat
// är NullRendererns no-op.
struct Inspelning : gfx::NullRenderer {
    struct Anrop {
        std::string vad;   // "fyll" | "bild"
        gfx::Rectf  r;
        gfx::Color  färg;
        float       opacitet = 1.0f;   // bara "bild"
    };
    std::vector<Anrop> logg;
    // `createImage`-anropen — en Bitmap laddas upp genom den, och MÄTNINGEN
    // ska aldrig göra det (fall 6). Svaret är en falsk bild med mått, så
    // fönstrets cache tar emot den.
    int skapade = 0;

    void fillRect(const gfx::Rectf& r, gfx::Color c) override
    {
        logg.push_back({"fyll", r, c});
    }
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int, int, gfx::ImageFormat,
                                            const void*, std::size_t,
                                            bool) override
    {
        ++skapade;
        return std::make_unique<FalskBild>();
    }
    void fillRoundedRect(const gfx::Rectf& r, float, gfx::Color c) override
    {
        logg.push_back({"fyll", r, c});
    }
    std::unique_ptr<gfx::Image> loadImage(const std::string&) override
    {
        return std::make_unique<FalskBild>();
    }
    std::unique_ptr<gfx::Image> loadImage(const void*, std::size_t) override
    {
        return std::make_unique<FalskBild>();
    }
    void drawImage(gfx::Image&, const gfx::Rectf&, const gfx::Rectf& dst,
                   float opacity) override
    {
        logg.push_back({"bild", dst, {}, opacity});
    }
};

// Ritar widgeten i en 28 × 24-ruta och returnerar inspelningen.
std::vector<Inspelning::Anrop> rita(glaze::Widget& w, glaze::UI ui)
{
    Inspelning         rec;
    glaze::ImageCache  bilder(rec);
    const glaze::Style& st = glaze::defaultStyle(ui);
    FalskFont          f(7.0f, st.uiFontSize);
    glaze::Ctx         c = ctxFör(ui, f);
    c.r      = &rec;
    c.images = &bilder;
    c.box    = gfx::Rectf{0.0f, 0.0f, 28.0f, 24.0f};
    w.render(c);
    return rec.logg;
}

// Hur många bilder inspelningen har, och vad som ritades EFTER den första.
struct Bildfakta {
    int   bilder = 0;
    float opacitet = -1.0f;   // första bildens
    bool  fyllEfter = false;  // en fyllning efter bilden (en slöja)
};
Bildfakta bildfakta(const std::vector<Inspelning::Anrop>& logg)
{
    Bildfakta f;
    for (const auto& a : logg) {
        if (a.vad == "bild") {
            if (f.bilder++ == 0)
                f.opacitet = a.opacitet;
        } else if (f.bilder > 0) {
            f.fyllEfter = true;
        }
    }
    return f;
}

void fallSlojan()
{
    const glaze::AssetHandle ok = glaze::img("toolbar", "ok");
    if (!ok.valid())
        return;   // raden ovan har redan fällt grinden

    // En INAKTIV ikon ritas med OPACITETEN 1 − iconFade (`Content::
    // imgOpacity` → `Renderer::drawImage`), och INGENTING läggs över den:
    // tonningen är ingen slöja i remsans färg, som skulle synas som
    // en ruta på varje annan yta (en panel, en gruppram). I alla tre UI.
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string namn  = ui == glaze::UI::Slate   ? "Slate"
                                  : ui == glaze::UI::Glaze ? "Glaze"
                                                           : "Minimalistic";
        const float       tonad = 1.0f - glaze::defaultStyle(ui).iconFade;

        glaze::ToolButton a;
        a.setImg(ok);
        const Bildfakta fa = bildfakta(rita(a, ui));
        check(fa.bilder == 1 && fa.opacitet == 1.0f && !fa.fyllEfter,
              namn + ": aktiv ikonknapp — EN bild, full opacitet",
              std::to_string(fa.bilder) + " bilder, opacitet " +
                  std::to_string(fa.opacitet));

        glaze::ToolButton d;
        d.setImg(ok);
        d.disable();
        const Bildfakta fd = bildfakta(rita(d, ui));
        check(fd.bilder == 1 && std::fabs(fd.opacitet - tonad) < 1e-6f,
              namn + ": inaktiv ikonknapp — bilden med opacitet 1 − iconFade",
              std::to_string(fd.opacitet));
        check(!fd.fyllEfter,
              namn + ": inaktiv ikonknapp — INGENTING ritas över ikonen");

        // IKRYSSAD och inaktiv: plattan ligger UNDER ikonen (Slate/Glaze),
        // aldrig över den, och ikonen tonas lika.
        glaze::ToolButton k;
        k.setImg(ok);
        k.setCheckable(true);
        k.setChecked(true);
        k.disable();
        const Bildfakta fk = bildfakta(rita(k, ui));
        check(fk.bilder == 1 && std::fabs(fk.opacitet - tonad) < 1e-6f &&
                  !fk.fyllEfter,
              namn + ": ikryssad inaktiv — plattan under, ikonen tonad",
              std::to_string(fk.opacitet));

        // BUTTON: samma tonning över den inaktiva plattan.
        glaze::Button bd;
        bd.setImg(ok);
        bd.disable();
        const Bildfakta fbd = bildfakta(rita(bd, ui));
        check(fbd.bilder == 1 && std::fabs(fbd.opacitet - tonad) < 1e-6f &&
                  !fbd.fyllEfter,
              namn + ": inaktiv Button — ikonen tonad, ingen slöja",
              std::to_string(fbd.opacitet));
        glaze::Button ba;
        ba.setImg(ok);
        const Bildfakta fba = bildfakta(rita(ba, ui));
        check(fba.bilder == 1 && fba.opacitet == 1.0f,
              namn + ": aktiv Button — full opacitet",
              std::to_string(fba.opacitet));
    }
}

// ------------------------------------------------- 6. bilder ur pixlar
//
// Samma `setImg` tar en `glaze::Bitmap`. En
// verktygsknapp och en knapp med en bild ur pixlar ritar EN bild med samma
// opacitetsregel som assetbilden, och ingenting över den. En `Picture`s
// naturliga mått är bildens pixlar räknade som FYSISKA px — en 64 × 64-bild
// är 64 logiska px på skala 1 och 32 på skala 2, 64 fysiska på båda — och
// mätningen laddar aldrig upp något. `setImg` med SAMMA bild efter
// `setPixels` är en ändring (versionen), men utan `setPixels` är den det
// inte.

struct ProvBild : glaze::Picture {
    using glaze::Widget::damageStamp;
};

void fallPixlar()
{
    glaze::Bitmap bild(16, 16);
    bild.fill(gfx::Color{40, 120, 200, 255});
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string namn  = ui == glaze::UI::Slate   ? "Slate"
                                  : ui == glaze::UI::Glaze ? "Glaze"
                                                           : "Minimalistic";
        const float       tonad = 1.0f - glaze::defaultStyle(ui).iconFade;

        glaze::ToolButton a;
        a.setImg(bild);
        const Bildfakta fa = bildfakta(rita(a, ui));
        check(fa.bilder == 1 && fa.opacitet == 1.0f && !fa.fyllEfter,
              namn + ": verktygsknapp med Bitmap — EN bild, full opacitet",
              std::to_string(fa.bilder) + " bilder");
        glaze::ToolButton d;
        d.setImg(bild);
        d.disable();
        const Bildfakta fd = bildfakta(rita(d, ui));
        check(fd.bilder == 1 && std::fabs(fd.opacitet - tonad) < 1e-6f &&
                  !fd.fyllEfter,
              namn + ": inaktiv verktygsknapp med Bitmap — tonad, ingen slöja",
              std::to_string(fd.opacitet));
        glaze::Button ba;
        ba.setImg(bild);
        const Bildfakta fba = bildfakta(rita(ba, ui));
        check(fba.bilder == 1 && fba.opacitet == 1.0f && !fba.fyllEfter,
              namn + ": Button med Bitmap — EN bild, full opacitet",
              std::to_string(fba.bilder) + " bilder");
        glaze::Button bd;
        bd.setImg(bild);
        bd.disable();
        const Bildfakta fbd = bildfakta(rita(bd, ui));
        check(fbd.bilder == 1 && std::fabs(fbd.opacitet - tonad) < 1e-6f &&
                  !fbd.fyllEfter,
              namn + ": inaktiv Button med Bitmap — tonad, ingen slöja",
              std::to_string(fbd.opacitet));
    }

    // PICTURE: det naturliga måttet, utan uppladdning.
    glaze::Bitmap stor(64, 64);
    ProvBild      p;
    p.setImg(stor);
    for (const float skala : {1.0f, 2.0f}) {
        const std::string  tag = " (skala " + std::to_string(int(skala)) + ")";
        Inspelning         rec;
        glaze::ImageCache  bilder(rec);
        const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
        FalskFont          f(7.0f * skala, st.uiFontSize * skala);
        glaze::Ctx         c = ctxFör(glaze::UI::Slate, f);
        c.r      = &rec;
        c.images = &bilder;
        c.scale  = skala;
        const gfx::Sizef m = Meter::measure(p, c);
        check(nära(m.w, 64.0f / skala) && nära(m.h, 64.0f / skala),
              "Picture med 64 × 64-Bitmap — naturligt mått 64 / skala" + tag,
              std::to_string(m.w) + " × " + std::to_string(m.h));
        const gfx::Rectf r = p.resolve(c);
        check(nära(r.w, 64.0f) && nära(r.h, 64.0f),
              "…och 64 FYSISKA px på skärmen — pixlarna 1:1" + tag,
              rekt(r));
        check(rec.skapade == 0,
              "mätningen laddar inte upp bilden (ingen createImage)" + tag,
              std::to_string(rec.skapade));
    }

    // SAMMA bild igen är ingen ändring — förrän pixlarna ändrats.
    const std::uint64_t s0 = p.damageStamp();
    p.setImg(stor);
    check(p.damageStamp() == s0,
          "setImg med samma Bitmap och samma pixlar — ingen invalidering");
    const std::uint8_t röd[4] = {255, 0, 0, 255};
    stor.setPixels(0, 0, 1, 1, röd);
    p.setImg(stor);
    check(p.damageStamp() > s0,
          "setImg med samma Bitmap EFTER setPixels — en ändring som "
          "invaliderar");
}

// --------------------------------------------- 7. rullgardinen, headless
//
// Bredden och den platta formen — inget fönster
// behövs: mätningen frågar bara stilen och fonten, och menyn är data.

void fallRullgardin()
{
    // BREDDEN: med poster växer AUTO-bredden med EXAKT `toolArrowW`, i alla
    // tre UI — och en satt bredd vinner.
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string   namn = ui == glaze::UI::Slate   ? "Slate"
                                   : ui == glaze::UI::Glaze ? "Glaze"
                                                            : "Minimalistic";
        const glaze::Style& st = glaze::defaultStyle(ui);
        FalskFont           f(7.0f, st.uiFontSize);
        const glaze::Ctx    c = ctxFör(ui, f);

        glaze::ToolButton b("Ny");
        const float       utan = Meter::measure(b, c).w;
        b.addItem({}, "Dokument");
        const float med = Meter::measure(b, c).w;
        check(st.toolArrowW > 0.0f && nära(med - utan, st.toolArrowW),
              namn + ": med poster växer bredden med EXAKT toolArrowW",
              std::to_string(utan) + " → " + std::to_string(med));

        glaze::ToolButton satt("Ny");
        satt.width = 60;
        satt.addItem({}, "Dokument");
        check(nära(Meter::measure(satt, c).w, 60.0f),
              namn + ": en SATT bredd vinner — pilen tar sin ruta ur den");
    }

    // I REMSAN: knappen med poster tar pilens ruta, och grannen flyttar lika
    // mycket. Slates tal och fall 3:s förväntade värden ("A" = 7 + 2·6 = 19, luften 4/3,
    // innerhöjden 24, luckan 2) — står de inte där har fall 3 redan sagt det.
    {
        const glaze::UI     ui = glaze::UI::Slate;
        const glaze::Style& st = glaze::defaultStyle(ui);
        FalskFont           f(7.0f, st.uiFontSize);
        const glaze::Ctx    c = ctxFör(ui, f);

        glaze::ToolBar tb;
        auto* a = tb.add<glaze::ToolButton>("A");
        auto* d = tb.add<glaze::ToolButton>("D");
        a->addItem({}, "Dokument");
        glaze::Ctx cc = c;
        cc.area       = static_cast<const glaze::Widget&>(tb).childArea(c);
        const float aw = 19.0f + st.toolArrowW;
        check(lika(a->resolve(cc), {4.0f, 3.0f, aw, 24.0f}),
              "remsan: knappen med poster är 19 + toolArrowW bred",
              rekt(a->resolve(cc)));
        check(lika(d->resolve(cc), {4.0f + aw + 2.0f, 3.0f, 19.0f, 24.0f}),
              "remsan: grannen flyttar lika mycket", rekt(d->resolve(cc)));
    }

    // DEN PLATTA FORMEN — systemfältsikonens, med `{}` som roten.
    glaze::setLanguage("sv");
    glaze::ToolButton b("Ny");
    check(b.menu().items.empty() && !b.isMenuOpen(),
          "rullgardin: utan poster ingen meny");
    b.addItem({}, "Dokument");
    b.addSubmenu({}, "Mall");
    b.addItem("Mall", glaze::tr(glaze::str::Yes));
    b.addCheck({}, "Visa", true);
    b.addRadio({}, "Liten");
    b.addSeparator({});
    const glaze::Menu& m = b.menu();
    check(m.items.size() == 5 && m.items[1].submenu.size() == 1 &&
              m.items[2].checkable && m.items[2].checked && m.items[3].radio &&
              m.items[4].separator,
          "rullgardin: de fem add-satserna med {}-roten, i källordning",
          std::to_string(m.items.size()));
    check(b.itemAtPath({"Mall", glaze::tr(glaze::str::Yes)}) != nullptr &&
              b.itemAtPath({}) == nullptr,
          "rullgardin: undermenyns post nås på stigen; den tomma stigen är "
          "menyn och ingen post");
    b.addItem("Finns inte", "X");
    b.addItem("a/b", "Y");   // UNDERKÄND stig — landar aldrig i roten
    check(m.items.size() == 5 && m.items[1].submenu.size() == 1,
          "rullgardin: en okänd och en underkänd stig lägger inget någonstans",
          std::to_string(m.items.size()));

    // setHandler i BÅDA formerna. Aktiveringen går genom `activatePath`
    // med knappen som värd — den väg popupens rader tar.
    int            kört = 0;
    glaze::Widget* avs  = nullptr;
    b.setHandler("Dokument", [&](glaze::Widget* s, glaze::ClickArgs&) {
        ++kört;
        avs = s;
    });
    b.setHandler({"Mall", glaze::tr(glaze::str::Yes)}, [&] { ++kört; });
    b.menu().activatePath({0}, &b);
    b.menu().activatePath({1, 0}, &b);
    check(kört == 2 && avs == &b,
          "rullgardin: setHandler kopplar postens handler, full och "
          "parameterlös form", std::to_string(kört));

    // POSTENS TILLSTÅND — den platta formen ÄR trädformens tillstånd.
    glaze::Bitmap ikon(4, 4);
    b.setItemIcon("Dokument", ikon);
    b.setItemEnabled({"Mall", glaze::tr(glaze::str::Yes)}, false);
    b.setItemChecked("Visa", false);
    check(m.items[0].icon == glaze::ImageSource(ikon) &&
              !m.items[1].submenu[0].enabled && !m.items[2].checked,
          "rullgardin: setItemIcon/setItemEnabled/setItemChecked ändrar "
          "menyns poster");
    b.setItemEnabled({}, false);
    b.setItemChecked("Finns inte", true);
    check(m.items[0].enabled && m.items[1].enabled && !m.items[2].checked,
          "rullgardin: den tomma och en okänd stig ändrar ingenting");

    // setProp — den delade knappen.
    check(b.setProp("split", "true") && b.isSplit(), "setProp: split");
    check(b.setProp("split", "false") && !b.isSplit(), "setProp: split av");
}

// ---------------------------------------- 8. rullgardinen med fönster
//
// Ett DOLT fönster (`setOffscreen`, menu_gates rigg) och RIKTIGA tryck genom
// `handleEvent`: trycket går genom AppWindow till knappens `mouseDown`, och
// släppet till `clicked()` — musens väg. Stängningen är UPPSKJUTEN till
// pump-svansen, så fallen är en KEDJA AV POSTADE STEG; `run()` ÄR öppnaren
// (aldrig `show()` först — se menu_gate).

// Remsan med sin damage-stämpel nåbar (protected — `ProvBild`s mönster).
struct ProvRemsa : glaze::ToolBar {
    using glaze::Widget::damageStamp;
};

struct MenyVard : glaze::AppWindow {
    ProvRemsa         remsa;
    glaze::ToolButton ny{"Ny"};         // odelad, med poster
    glaze::ToolButton angra{"Ångra"};   // DELAD
    glaze::ToolButton vaxla{"Läge"};    // växlingsbar, odelad, med poster
    glaze::ToolButton av{"Av"};         // inaktiverad, med poster
    glaze::Menu       probe;            // kan popuper öppnas här alls?

    MenyVard() : glaze::AppWindow("toolbar_gate", 480, 200)
    {
        ny.addItem({}, "Dokument");
        angra.addItem({}, "Skriv");
        angra.setSplit(true);
        vaxla.addItem({}, "Ett");
        vaxla.setCheckable(true);
        av.addItem({}, "Ett");
        av.disable();
        probe.addItem("Probe");
        root().adopt(remsa);   // Dock::Top är förvalet
        remsa.adopt(ny);
        remsa.adopt(angra);
        remsa.adopt(vaxla);
        remsa.adopt(av);
    }

    // Knappens rect i fönstrets FYSISKA klientpx — det `handleEvent` läser.
    gfx::Rectf rektAv(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    // Pilens ruta i fysiska px (stilens logiska bredd × skalan).
    float pilBredd()
    {
        const glaze::Ctx c = ny.behaviorCtx();
        return c.style->toolArrowW * c.scale;
    }
    void mus(gfx::EventType t, float x, float y)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void ner(float x, float y) { mus(gfx::EventType::MouseButtonDown, x, y); }
    void upp(float x, float y) { mus(gfx::EventType::MouseButtonUp, x, y); }
    void klick(float x, float y)
    {
        ner(x, y);
        upp(x, y);
    }
};

// POPUPENS ÖGONBLICKSBILD — raderna i det öppna menyfönstret, lästa ur
// FÖNSTRET (dess rader är en ListBox, menu.cpp) och inte ur menyns data. Det
// är skillnaden mellan "posten finns i menyn" och "posten syns i menyn som
// öppnades".
std::vector<std::string> ögonblicksbild(glaze::AppWindow& ägare)
{
    for (glaze::AppWindow* p : glaze::app().windows()) {
        if (p == &ägare)
            continue;
        for (glaze::Widget* barn : p->root().children())
            if (auto* lb = dynamic_cast<glaze::ListBox*>(barn))
                return lb->items();
    }
    return {};
}

std::string rader(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& r : v)
        s += (s.empty() ? "" : ", ") + r;
    return "[" + s + "]";
}

// ------------------------------------------ 9. stående fält och egen längd
//
// D15: en `Bar` med `dock` Left/Right STÅR — flexen packar
// uppifrån, varje barn får barens fulla bredd och sin mätta höjd, och
// luften byter axel (`toolBarPadX` längs fältet, `toolBarPadY` tvärs). En
// `Separator` i en bar står TVÄRS baren vilket `vertical` den än har: tunn
// längs baren (mätt) och ritad som ett streck tvärs (inspelad). D16: ett
// verktygsfält utan satt längd har en EGEN längd — luften, barnen och
// luckorna — och syns därför med `Dock::None` och bara vänsterankare.
// De förväntade värdena är tal skrivna här (Slates 30 / 4 / 3 / 2 / 6 och separatorns 8 —
// fallPlacering prövar att de står i stilen).

// Renderer som spelar in STRECKEN (hårlinjerna) — separatorns riktning.
struct Streck : gfx::NullRenderer {
    std::vector<gfx::Rectf> linjer;   // {x1, y1, x2, y2}
    void line(float x1, float y1, float x2, float y2, gfx::Color) override
    {
        linjer.push_back({x1, y1, x2, y2});
    }
};

// Separatorns streck i rutan `box` — lodrätt (x1 == x2) eller vågrätt.
std::vector<gfx::Rectf> streck(glaze::Separator& s, const glaze::Ctx& c,
                               const gfx::Rectf& box)
{
    Streck     rec;
    glaze::Ctx rc = c;
    rc.r          = &rec;
    rc.box        = box;
    s.render(rc);
    return rec.linjer;
}

void fallStaende()
{
    const glaze::UI     ui = glaze::UI::Slate;
    const glaze::Style& st = glaze::defaultStyle(ui);
    FalskFont           f(7.0f, st.uiFontSize);
    const glaze::Ctx    c = ctxFör(ui, f);   // ytan 400 × 300
    check(nära(st.separatorThickness + 2.0f * st.separatorInset, 8.0f) &&
              st.separatorThickness * c.scale <= 1.0f,
          "facit: separatorn är 8 tunn och en hårlinje i Slate");

    // D15 — STÅENDE FLEX: tre knappar med satt höjd 20 och en separator
    // vars `vertical` är false (förvalet).
    glaze::ToolBar tb;
    tb.dock  = glaze::Dock::Left;
    auto* a  = tb.add<glaze::ToolButton>("A");
    a->height = 20;
    auto* b  = tb.add<glaze::ToolButton>("B");
    b->height = 20;
    auto* sep = tb.add<glaze::Separator>();
    auto* d  = tb.add<glaze::ToolButton>("D");
    d->height = 20;
    check(tb.isVertical() && lika(tb.resolve(c), {0.0f, 0.0f, 30.0f, 300.0f}),
          "stående: Dock::Left — bredden är toolBarH, höjden hela ytan",
          rekt(tb.resolve(c)));
    glaze::Ctx cc = c;
    cc.area       = static_cast<const glaze::Widget&>(tb).childArea(c);
    check(lika(cc.area, {3.0f, 4.0f, 24.0f, 292.0f}),
          "stående: luften byter axel — padX längs (y), padY tvärs (x)",
          rekt(cc.area));
    check(lika(a->resolve(cc), {3.0f, 4.0f, 24.0f, 20.0f}) &&
              lika(b->resolve(cc), {3.0f, 26.0f, 24.0f, 20.0f}),
          "stående: knapparna uppifrån med luckan 2, barens innerbredd",
          rekt(a->resolve(cc)) + " " + rekt(b->resolve(cc)));
    check(lika(sep->resolve(cc), {3.0f, 48.0f, 24.0f, 8.0f}) &&
              lika(d->resolve(cc), {3.0f, 58.0f, 24.0f, 20.0f}),
          "stående: separatorn är TUNN längs baren (8), fast `vertical` är "
          "false",
          rekt(sep->resolve(cc)) + " " + rekt(d->resolve(cc)));
    const std::vector<gfx::Rectf> vågrätt =
        streck(*sep, c, {0.0f, 0.0f, 24.0f, 8.0f});
    check(vågrätt.size() == 1 && vågrätt[0].y == vågrätt[0].h &&
              vågrätt[0].x != vågrätt[0].w,
          "stående: separatorn ritas VÅGRÄT — tvärs baren");

    // D16 — EGEN LÄNGD, stående: 2·4 + 20 + 20 + 8 + 20 + 3 luckor à 2 = 82.
    const gfx::Sizef ms = Meter::measure(tb, c);
    check(nära(ms.w, 30.0f) && nära(ms.h, 82.0f),
          "egen längd: stående — luften, barnen och luckorna (30 × 82)",
          std::to_string(ms.w) + " × " + std::to_string(ms.h));

    // …och LIGGANDE med samma barn: knapparna är 7 + 2·6 = 19 breda, och
    // separatorn — fortfarande `vertical` false — står nu LODRÄT och är 8
    // bred: 2·4 + 19 + 19 + 8 + 19 + 3·2 = 79.
    glaze::ToolBar tl;
    tl.add<glaze::ToolButton>("A");
    tl.add<glaze::ToolButton>("B");
    auto* sepL = tl.add<glaze::Separator>();
    tl.add<glaze::ToolButton>("D");
    const gfx::Sizef ml = Meter::measure(tl, c);
    check(!tl.isVertical() && nära(ml.w, 79.0f) && nära(ml.h, 30.0f),
          "egen längd: liggande — 79 × 30",
          std::to_string(ml.w) + " × " + std::to_string(ml.h));
    const std::vector<gfx::Rectf> lodrätt =
        streck(*sepL, c, {0.0f, 0.0f, 8.0f, 24.0f});
    check(lodrätt.size() == 1 && lodrätt[0].x == lodrätt[0].w &&
              lodrätt[0].y != lodrätt[0].h,
          "liggande: separatorn ritas LODRÄT — tvärs baren");
    tl.width = 200.0f;
    check(nära(Meter::measure(tl, c).w, 200.0f),
          "egen längd: en satt bredd vinner");

    // D16 — SYNS med Dock::None och bara vänster- och toppankare.
    glaze::ToolBar fri;
    fri.dock    = glaze::Dock::None;
    fri.anchors = {true, true, false, false};
    fri.x       = 10.0f;
    fri.y       = 10.0f;
    fri.add<glaze::ToolButton>("A");
    check(lika(fri.resolve(c), {10.0f, 10.0f, 27.0f, 30.0f}),
          "egen längd: Dock::None med vänsterankare syns (2·4 + 19 = 27)",
          rekt(fri.resolve(c)));

    // UTANFÖR en bar gäller `vertical` oförändrat.
    glaze::Separator ensam;
    check(nära(Meter::measure(ensam, c).w, st.separatorDefaultLen) &&
              nära(Meter::measure(ensam, c).h, 8.0f),
          "utanför en bar: en vågrät separator är lång och tunn som förut");
}

// ------------------------------------ 10. det flytande fältet
//
// T5: ett FLYTANDE fält LIGGER alltid — `dock` är bara minnet för var det
// dockas tillbaka — och en separator i det står TVÄRS den liggande baren
// (`Separator::drawnVertical` frågar den virtuella `isVertical`). Byts den
// effektiva riktningen byts bredd och höjd, så den satta längden förblir
// längd (`turnIfNeeded`), och tillbaka igen. Ett flytande fält ritar ingen
// hårlinje (`drawsInnerEdge`), ett stående gör det. Headless: utan värd är
// `setFloating` bara tillståndet.
void fallFlytande()
{
    const glaze::UI     ui = glaze::UI::Slate;
    const glaze::Style& st = glaze::defaultStyle(ui);
    FalskFont           f(7.0f, st.uiFontSize);
    const glaze::Ctx    c = ctxFör(ui, f);
    auto linjerI = [&c](glaze::Widget& w, const gfx::Rectf& box) {
        Streck     rec;
        glaze::Ctx rc = c;
        rc.r          = &rec;
        rc.box        = box;
        w.render(rc);
        return rec.linjer;
    };

    glaze::ToolBar tb;
    tb.dock   = glaze::Dock::Left;
    tb.height = 300.0f;   // stående: längden
    tb.add<glaze::ToolButton>("A");
    auto* sep = tb.add<glaze::Separator>();
    tb.add<glaze::ToolButton>("B");
    check(tb.isVertical() && linjerI(tb, {0.0f, 0.0f, 30.0f, 300.0f}).size() == 1,
          "flytande: (förutsättning) Dock::Left står och ritar sin hårlinje");

    tb.setFloating(true);
    check(tb.isFloating() && !tb.isVertical() && tb.dock == glaze::Dock::Left,
          "flytande: ett flytande Dock::Left-fält LIGGER — `dock` är minnet");
    check(nära(tb.width, 300.0f) && nära(tb.height, 0.0f),
          "flytande: bredd och höjd byttes — längden 300 är nu bredden",
          std::to_string(tb.width) + " × " + std::to_string(tb.height));
    const gfx::Sizef m = Meter::measure(tb, c);
    check(nära(m.w, 300.0f) && nära(m.h, st.toolBarH),
          "flytande: liggande mått — längden 300, tjockleken toolBarH",
          std::to_string(m.w) + " × " + std::to_string(m.h));
    const std::vector<gfx::Rectf> lodrätt =
        streck(*sep, c, {0.0f, 0.0f, 8.0f, 24.0f});
    check(lodrätt.size() == 1 && lodrätt[0].x == lodrätt[0].w &&
              lodrätt[0].y != lodrätt[0].h,
          "flytande: separatorn ritas LODRÄT — tvärs den liggande baren");
    check(linjerI(tb, {0.0f, 0.0f, 300.0f, 30.0f}).empty(),
          "flytande: ingen hårlinje — fältet vetter inte mot något innehåll");

    tb.setFloating(false);
    check(tb.isVertical() && nära(tb.height, 300.0f) && nära(tb.width, 0.0f),
          "flytande: dockat igen — det står, och höjden är längden igen",
          std::to_string(tb.width) + " × " + std::to_string(tb.height));
}

// --------------------------------------------- 11. överflödet, headless
//
// De förväntade värdena är tal skrivna här (Slates 30 / 4 / 3 / 2 / 6, separatorns
// 8 och överflödsknappens 20 — fallPlacering prövar de fem första) och den
// falska fontens 7 px per kodpunkt: "A" = 19, "BB" = 26, "CCC" = 33,
// "DDDD" = 40.

// Överflödsknappen — fältets barn som inte är något av `egna`.
glaze::Widget* knappen(glaze::ToolBar& tb,
                       std::initializer_list<const glaze::Widget*> egna)
{
    for (glaze::Widget* b : tb.children()) {
        bool egen = false;
        for (const glaze::Widget* e : egna)
            egen = egen || e == b;
        if (!egen)
            return b;
    }
    return nullptr;
}

void fallOverflode()
{
    const glaze::UI     ui = glaze::UI::Slate;
    const glaze::Style& st = glaze::defaultStyle(ui);
    FalskFont           f(7.0f, st.uiFontSize);
    glaze::Ctx          c = ctxFör(ui, f);
    check(nära(st.toolOverflowW, 20.0f),
          "facit: Slates överflödsknapp är 20 lång");

    // ÖVERFLÖDE LIGGANDE i 120: barnens yta {4, 3, 112, 24}, platsen före
    // knappen 112 − 20 − 2 = 90. A (19) + luckan + spacern (0, växer inte) +
    // luckan + BB (26) = 49; separatorn (8) ryms (59) men SLUTAR prefixet och
    // följer med till överflödet; CCC (33) ryms inte (94 > 90).
    glaze::ToolBar tb;
    auto*          a   = tb.add<glaze::ToolButton>("A");
    auto*          sp  = tb.addSpacer();
    auto*          b   = tb.add<glaze::ToolButton>("BB");
    auto*          sep = tb.add<glaze::Separator>();
    auto*          cc  = tb.add<glaze::ToolButton>("CCC");
    auto*          d   = tb.add<glaze::ToolButton>("DDDD");
    glaze::Widget* ob  = knappen(tb, {a, sp, b, sep, cc, d});
    check(ob && dynamic_cast<glaze::ToolButton*>(ob) && !ob->focusable(),
          "överflöde: fältet har en egen verktygsknapp som inte är ett "
          "tabbstopp");
    if (!ob)
        return;
    check(tb.overflow(), "överflöde: på som förval");
    c.area.w = 120.0f;
    glaze::Ctx cc120 = c;
    cc120.area       = static_cast<const glaze::Widget&>(tb).childArea(c);
    check(lika(cc120.area, {4.0f, 3.0f, 112.0f, 24.0f}),
          "överflöde: barnens yta i 120", rekt(cc120.area));
    check(lika(a->resolve(cc120), {4.0f, 3.0f, 19.0f, 24.0f}) &&
              lika(sp->resolve(cc120), {25.0f, 3.0f, 0.0f, 24.0f}) &&
              lika(b->resolve(cc120), {27.0f, 3.0f, 26.0f, 24.0f}),
          "överflöde: de första packas från början, spacern växer inte",
          rekt(a->resolve(cc120)) + " " + rekt(sp->resolve(cc120)) + " " +
              rekt(b->resolve(cc120)));
    check(sep->resolve(cc120).w <= 0.0f && cc->resolve(cc120).w <= 0.0f &&
              d->resolve(cc120).w <= 0.0f,
          "överflöde: separatorn som slutade prefixet och de som inte ryms "
          "är dolda i layouten",
          rekt(sep->resolve(cc120)) + " " + rekt(cc->resolve(cc120)));
    check(sep->isVisible() && cc->isVisible() && d->isVisible(),
          "överflöde: de dolda behåller sitt visible");
    check(lika(ob->resolve(cc120), {96.0f, 3.0f, 20.0f, 24.0f}),
          "överflöde: knappen i fältets ände, 20 lång, i innerhöjden",
          rekt(ob->resolve(cc120)));

    // RYMS BARNEN är det Bars flex rakt av — knappen har ingen yta, och
    // spacern tar det lediga (392 − 136 = 256), så DDDD slutar vid
    // innerkanten 396.
    glaze::Ctx c400  = ctxFör(ui, f);
    glaze::Ctx cc400 = c400;
    cc400.area       = static_cast<const glaze::Widget&>(tb).childArea(c400);
    check(ob->resolve(cc400).w <= 0.0f &&
              lika(sp->resolve(cc400), {25.0f, 3.0f, 256.0f, 24.0f}) &&
              lika(d->resolve(cc400), {356.0f, 3.0f, 40.0f, 24.0f}),
          "överflöde: ryms barnen syns ingen knapp och flexen är Bars",
          rekt(ob->resolve(cc400)) + " " + rekt(d->resolve(cc400)));

    // AV: barnen rinner över och klipps (som i en vanlig Bar), ingen knapp.
    tb.setOverflow(false);
    check(!tb.overflow() && ob->resolve(cc120).w <= 0.0f &&
              lika(sep->resolve(cc120), {55.0f, 3.0f, 8.0f, 24.0f}) &&
              lika(d->resolve(cc120), {100.0f, 3.0f, 40.0f, 24.0f}),
          "setOverflow(false): barnen rinner över, ingen knapp",
          rekt(d->resolve(cc120)));
    check(tb.setProp("overflow", "true") && tb.overflow(),
          "setProp: overflow");

    // KNAPPEN ÄR INREDNING — den går inte att lossa.
    check(!tb.unadopt(*ob) && ob->parent() == &tb,
          "överflöde: knappen går inte att lossa (canReleaseChild)");

    // EN HEL FYSISK PIXEL: ett överskott under den är avrundning — barnens
    // yta 46.5 för A + BB (47) flödar inte över, 46 gör det.
    glaze::ToolBar t2;
    auto*          a2  = t2.add<glaze::ToolButton>("A");
    auto*          b2  = t2.add<glaze::ToolButton>("BB");
    glaze::Widget* ob2 = knappen(t2, {a2, b2});
    glaze::Ctx     cx  = ctxFör(ui, f);
    cx.area.w          = 4.0f + 4.0f + 46.5f;
    glaze::Ctx cxx     = cx;
    cxx.area           = static_cast<const glaze::Widget&>(t2).childArea(cx);
    const bool halv    = ob2 && ob2->resolve(cxx).w <= 0.0f;
    cx.area.w          = 4.0f + 4.0f + 46.0f;
    cxx.area           = static_cast<const glaze::Widget&>(t2).childArea(cx);
    const bool hel     = ob2 && ob2->resolve(cxx).w > 0.0f;
    check(halv && hel,
          "överflöde: en halv pixel för kort flödar inte över, en hel gör det");

    // STÅENDE i 80: barnens yta {3, 4, 24, 72}, platsen 72 − 20 − 2 = 50 —
    // två knappar om 20 och luckan (42), den tredje ryms inte.
    glaze::ToolBar tv;
    tv.dock = glaze::Dock::Left;
    glaze::ToolButton* v[4];
    for (int i = 0; i < 4; ++i) {
        v[i]         = tv.add<glaze::ToolButton>("V");
        v[i]->height = 20.0f;
    }
    glaze::Widget* obv = knappen(tv, {v[0], v[1], v[2], v[3]});
    glaze::Ctx     c80 = ctxFör(ui, f);
    c80.area.h         = 80.0f;
    glaze::Ctx cv      = c80;
    cv.area            = static_cast<const glaze::Widget&>(tv).childArea(c80);
    check(obv && lika(v[0]->resolve(cv), {3.0f, 4.0f, 24.0f, 20.0f}) &&
              lika(v[1]->resolve(cv), {3.0f, 26.0f, 24.0f, 20.0f}) &&
              v[2]->resolve(cv).h <= 0.0f && v[3]->resolve(cv).h <= 0.0f &&
              lika(obv->resolve(cv), {3.0f, 56.0f, 24.0f, 20.0f}),
          "överflöde stående: uppifrån, knappen nederst och 20 hög",
          obv ? rekt(obv->resolve(cv)) : std::string("ingen knapp"));

    // DEN EGNA LÄNGDEN räknar inte knappen (fall 9:s 79 och 82 står kvar).
    check(nära(Meter::measure(t2, c400).w, 4.0f + 19.0f + 2.0f + 26.0f + 4.0f),
          "överflöde: den egna längden räknar inte knappen",
          std::to_string(Meter::measure(t2, c400).w));
}

void fallRullgardinFonster()
{
    MenyVard w;
    w.setOffscreen(true);   // som window_gate: dolt men RIKTIGT

    int            nyDrop = 0, nyKlick = 0;
    int            angraDrop = 0, angraKlick = 0, angraByte = 0;
    int            vaxlaByte = 0, vaxlaKlick = 0;
    int            avDrop = 0, avKlick = 0;
    bool           sågÖppen = true;   // vad `onDropDown` såg — ska vara STÄNGD
    glaze::Widget* dropAvs  = nullptr;
    w.ny.onDropDown = [&](glaze::Widget* s, glaze::EventArgs&) {
        ++nyDrop;
        sågÖppen = w.ny.isMenuOpen() || w.ny.menu().isOpen();
        dropAvs  = s;
        if (!w.ny.itemAtPath("Senast"))
            w.ny.addItem({}, "Senast");   // en DYNAMISK post — syns den?
    };
    w.ny.onClick       = [&] { ++nyKlick; };
    w.angra.onDropDown = [&] { ++angraDrop; };
    w.angra.onClick    = [&] { ++angraKlick; };
    w.angra.onChange   = [&] { ++angraByte; };
    w.vaxla.onChange   = [&] { ++vaxlaByte; };
    w.vaxla.onClick    = [&] { ++vaxlaKlick; };
    w.av.onDropDown    = [&] { ++avDrop; };
    w.av.onClick       = [&] { ++avKlick; };

    auto fönster = [] {
        return std::to_string(glaze::app().windowCount()) + " fönster";
    };

    // ANROPA ALDRIG show() HÄR — se filhuvudet i menu_gate.
    bool                  kedjanKordes = false;
    int                   steg         = 0;
    std::function<void()> nasta;
    nasta = [&] {
        switch (steg++) {
        case 0:
            kedjanKordes = true;
            // PROBEN: en vanlig meny. Kommer den inte upp kan ingen popup
            // öppnas här, och då säger knappens fall ingenting.
            w.probe.popupAt(w.remsa, 10.0f, 10.0f);
            if (!w.probe.isOpen()) {
                std::printf("  --   rullgardinsfallen hoppas över: "
                            "popupfönstret kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            w.probe.close();
            break;

        case 1: {
            check(!w.probe.isOpen() && glaze::app().windowCount() == 1,
                  "proben är stängd — ett fönster kvar", fönster());
            // DEN FÖRSTA POSTEN gör knappen bredare och lägger om grannarna
            // (`relayoutSiblings` ritar om remsan); nästa post ändrar inte
            // bredden och rör bara knappen. Knappen läggs SIST, så de andras
            // platser står kvar.
            auto* extra = w.remsa.add<glaze::ToolButton>("Extra");
            const std::uint64_t s0 = w.remsa.damageStamp();
            extra->addItem({}, "Ett");
            const std::uint64_t s1 = w.remsa.damageStamp();
            extra->addItem({}, "Två");
            const std::uint64_t s2 = w.remsa.damageStamp();
            check(s1 > s0 && s2 == s1,
                  "första posten ritar om remsan (grannarna flyttar), nästa "
                  "bara knappen", std::to_string(s0) + " → " +
                                      std::to_string(s1) + " → " +
                                      std::to_string(s2));
            // ODELAD MED POSTER: trycket öppnar menyn, och det blir inget
            // klick av släppet.
            const gfx::Rectf r = w.rektAv(w.ny);
            const float      x = r.x + r.w / 2.0f;
            const float      y = r.y + r.h / 2.0f;
            w.ner(x, y);
            check(w.ny.isMenuOpen() && w.ny.menu().isOpen(),
                  "tryck på en knapp med poster öppnar menyn");
            check(nyDrop == 1 && !sågÖppen && dropAvs == &w.ny,
                  "onDropDown fyras EN gång, FÖRE öppningen, med knappen som "
                  "sender", std::to_string(nyDrop));
            const std::vector<std::string> bild = ögonblicksbild(w);
            check(bild.size() == 2 && bild[0] == "Dokument" &&
                      bild[1] == "Senast",
                  "posten onDropDown lade till STÅR i popupens ögonblicksbild",
                  rader(bild));
            w.upp(x, y);
            check(nyKlick == 0, "släppet efter ett menytryck ger inget onClick",
                  std::to_string(nyKlick));
            // TOGGLE: ett tryck till stänger — utan onDropDown och utan klick.
            w.klick(x, y);
            check(!w.ny.isMenuOpen() && nyDrop == 1 && nyKlick == 0,
                  "ett tryck till stänger menyn (toggle) — utan onDropDown och "
                  "utan klick");
            break;
        }

        case 2: {
            check(!w.ny.menu().isOpen() && glaze::app().windowCount() == 1,
                  "toggle: popupen rivs i pump-svansen", fönster());
            // DELAD: huvuddelen klickar, pildelen öppnar menyn.
            const gfx::Rectf r     = w.rektAv(w.angra);
            const float      pil   = w.pilBredd();
            const float      yc    = r.y + r.h / 2.0f;
            const float      huvud = r.x + (r.w - pil) / 2.0f;
            const float      pilX  = r.x + r.w - pil / 2.0f;
            w.klick(huvud, yc);
            check(angraKlick == 1 && angraDrop == 0 && !w.angra.isMenuOpen(),
                  "delad: huvuddelen ger onClick och ingen meny",
                  std::to_string(angraKlick));
            w.ner(pilX, yc);
            check(w.angra.isMenuOpen() && angraDrop == 1,
                  "delad: pildelen öppnar menyn och fyrar onDropDown");
            w.upp(pilX, yc);
            check(angraKlick == 1, "delad: pildelen ger inget onClick",
                  std::to_string(angraKlick));
            w.angra.closeMenu();
            break;
        }

        case 3: {
            check(!w.angra.menu().isOpen() && glaze::app().windowCount() == 1,
                  "delad: closeMenu() stänger i pump-svansen", fönster());
            // VÄXLINGEN: en delad växlingsbar knapp växlar av huvuddelen och
            // aldrig av pilen.
            w.angra.setCheckable(true);
            const gfx::Rectf r     = w.rektAv(w.angra);
            const float      pil   = w.pilBredd();
            const float      yc    = r.y + r.h / 2.0f;
            const float      huvud = r.x + (r.w - pil) / 2.0f;
            const float      pilX  = r.x + r.w - pil / 2.0f;
            w.klick(huvud, yc);
            check(w.angra.isChecked() && angraByte == 1 && angraKlick == 2,
                  "delad växlingsbar: huvuddelen växlar och klickar");
            w.klick(pilX, yc);
            check(w.angra.isChecked() && angraByte == 1 && angraKlick == 2 &&
                      w.angra.isMenuOpen(),
                  "delad växlingsbar: pildelen öppnar menyn och växlar INTE");
            w.angra.closeMenu();
            // ODELAD växlingsbar med poster: trycket är menyns.
            const gfx::Rectf v = w.rektAv(w.vaxla);
            w.klick(v.x + v.w / 2.0f, v.y + v.h / 2.0f);
            check(w.vaxla.isMenuOpen() && !w.vaxla.isChecked() &&
                      vaxlaByte == 0 && vaxlaKlick == 0,
                  "odelad växlingsbar med poster: trycket öppnar menyn och "
                  "växlar inte");
            w.vaxla.closeMenu();
            break;
        }

        case 4: {
            check(glaze::app().windowCount() == 1,
                  "växlingsfallen: inga popuper kvar", fönster());
            // INAKTIVERAD: ingenting händer.
            const gfx::Rectf r = w.rektAv(w.av);
            w.klick(r.x + r.w / 2.0f, r.y + r.h / 2.0f);
            check(!w.av.isMenuOpen() && avDrop == 0 && avKlick == 0 &&
                      glaze::app().windowCount() == 1,
                  "inaktiverad: trycket öppnar ingen meny och fyrar ingenting",
                  fönster());
            // PROGRAMMATISKT: `openMenu()` öppnar utan onDropDown — och en
            // knapp som INAKTIVERAS har ingen öppen meny (`enabledChanged`).
            w.ny.openMenu();
            check(w.ny.isMenuOpen() && nyDrop == 1,
                  "openMenu() öppnar menyn utan onDropDown",
                  std::to_string(nyDrop));
            w.ny.disable();
            check(!w.ny.isMenuOpen(), "disable() stänger en öppen meny");
            break;
        }

        case 5:
            check(!w.ny.menu().isOpen() && glaze::app().windowCount() == 1,
                  "inaktiveringen: popupen rivs i pump-svansen", fönster());
            w.ny.enable();
            w.close();
            return;

        default:
            w.close();
            return;
        }
        glaze::app().post(nasta);
    };
    glaze::app().post(nasta);
    w.run();

    // VAKTEN: utan den kan hela den asynkrona halvan tystna och grinden
    // ändå rapportera grönt.
    if (!kedjanKordes) {
        std::printf("  --   rullgardinsfallen hoppades över: inget fönster "
                    "kunde skapas (ingen display)\n");
        ++g_skips;
    }
}

// ------------------------------------------ 12. överflödet med fönster
//
// Genom fönstrets RIKTIGA ingång (fall 8:s rigg): åtta textknappar
// "Knapp N" (minst 40 logiska px var i varje font) i ett 300 brett fönster, och
// SIST — alltid dolda — två separatorer i rad, en växlingsbar knapp, en
// inaktiverad, en ren ikonknapp med verktygstips, en rullgardinsknapp med
// `onDropDown`, en delad knapp, en etikett, ett sökfält med verktygstips och
// en dropdown utan. Var snittet hamnar beror på fonten, så fallet mäter
// FORMEN: ett prefix av synliga och resten dolda, knappen i fältets ände,
// och en post per dold sak i ordning.
struct OverflodsVard : glaze::AppWindow {
    ProvRemsa         remsa;
    glaze::ToolButton k[8];
    glaze::Separator  s1, s2;
    glaze::ToolButton vaxla{"Växla"};
    glaze::ToolButton av{"Av"};
    glaze::ToolButton ikon;
    glaze::ToolButton ny{"Ny"};
    glaze::ToolButton angra{"Ångra"};
    glaze::Label      etikett{"Zoom:"};
    glaze::TextInput  sok;
    glaze::Dropdown   zoom;
    glaze::Button     utanfor{"Utanför"};
    glaze::Menu       probe;

    OverflodsVard() : glaze::AppWindow("toolbar_gate överflöde", 300, 160)
    {
        for (int i = 0; i < 8; ++i)
            k[i].setText("Knapp " + std::to_string(i + 1));
        vaxla.setCheckable(true);
        av.disable();
        ikon.setImg(glaze::img("toolbar", "ok"));
        ikon.setToolTip("Ikon");
        ny.addItem({}, "Dokument");
        angra.addItem({}, "Allt");
        angra.setSplit(true);
        sok.setToolTip("Sök");
        sok.width = 80;
        zoom.setItems({"50 %", "100 %"});
        zoom.width = 80;
        probe.addItem("Probe");
        root().adopt(remsa);   // Dock::Top
        for (glaze::ToolButton& b : k)
            remsa.adopt(b);
        for (glaze::Widget* w : std::initializer_list<glaze::Widget*>{
                 &s1, &s2, &vaxla, &av, &ikon, &ny, &angra, &etikett, &sok,
                 &zoom})
            remsa.adopt(*w);
        root().adopt(utanfor);
    }

    // Fältets egna barn i ordning (allt utom överflödsknappen).
    std::vector<glaze::Widget*> egna()
    {
        std::vector<glaze::Widget*> v;
        for (glaze::ToolButton& b : k)
            v.push_back(&b);
        for (glaze::Widget* w : std::initializer_list<glaze::Widget*>{
                 &s1, &s2, &vaxla, &av, &ikon, &ny, &angra, &etikett, &sok,
                 &zoom})
            v.push_back(w);
        return v;
    }
    glaze::ToolButton* knapp()
    {
        const std::vector<glaze::Widget*> e = egna();
        for (glaze::Widget* b : remsa.children())
            if (std::find(e.begin(), e.end(), b) == e.end())
                return dynamic_cast<glaze::ToolButton*>(b);
        return nullptr;
    }
    gfx::Rectf rektAv(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    gfx::Rectf inre()
    {
        return static_cast<const glaze::Widget&>(remsa)
            .childArea(remsa.behaviorCtx());
    }
    void mus(gfx::EventType t, float x, float y)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void klick(float x, float y)
    {
        mus(gfx::EventType::MouseButtonDown, x, y);
        mus(gfx::EventType::MouseButtonUp, x, y);
    }
};

void fallOverflodeFonster()
{
    OverflodsVard w;
    w.setOffscreen(true);

    int            klick5 = 0, vaxlaByte = 0, nyDrop = 0, nyDok = 0;
    int            angraKlick = 0, angraAllt = 0;
    glaze::Widget* avs = nullptr;
    for (glaze::ToolButton& b : w.k)
        b.onClick = [&klick5, &avs](glaze::Widget* s, glaze::ClickArgs&) {
            ++klick5;
            avs = s;
        };
    w.vaxla.onChange = [&] { ++vaxlaByte; };
    w.ny.onDropDown  = [&] {
        ++nyDrop;
        if (!w.ny.itemAtPath("Senast"))
            w.ny.addItem({}, "Senast");   // DYNAMISK — står den i spegeln?
    };
    w.ny.setHandler("Dokument", [&](glaze::Widget* s, glaze::ClickArgs&) {
        ++nyDok;
        avs = s;
    });
    w.angra.onClick = [&] { ++angraKlick; };
    w.angra.setHandler("Allt", [&](glaze::Widget* s, glaze::ClickArgs&) {
        ++angraAllt;
        avs = s;
    });

    // Postens index i överflödsmenyn för en text (−1 = ingen).
    const auto post = [&w](const std::string& text) {
        glaze::ToolButton* ob = w.knapp();
        if (!ob)
            return -1;
        const auto& m = ob->menu().items;
        for (std::size_t i = 0; i < m.size(); ++i)
            if (!m[i].separator && m[i].text.str() == text)
                return static_cast<int>(i);
        return -1;
    };

    bool                  kedjanKordes = false;
    int                   steg         = 0;
    std::uint64_t         stämpel      = 0;
    std::function<void()> nasta;
    nasta = [&] {
        switch (steg++) {
        case 0:
            kedjanKordes = true;
            w.probe.popupAt(w.remsa, 10.0f, 10.0f);
            if (!w.probe.isOpen()) {
                std::printf("  --   överflödesfallen hoppas över: "
                            "popupfönstret kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            w.probe.close();
            break;

        case 1: {
            glaze::ToolButton* ob = w.knapp();
            check(ob != nullptr, "överflöde F: fältet har sin knapp");
            if (!ob) {
                w.close();
                return;
            }
            // FORMEN: ett prefix av synliga, resten dolda — knappen i änden.
            const std::vector<glaze::Widget*> e = w.egna();
            std::size_t synliga = 0;
            bool        prefix  = true, någonDold = false;
            for (glaze::Widget* b : e) {
                const bool syns = w.rektAv(*b).w > 0.0f;
                if (syns && någonDold)
                    prefix = false;
                if (!syns)
                    någonDold = true;
                else
                    ++synliga;
            }
            const gfx::Rectf obr = w.rektAv(*ob);
            const gfx::Rectf in  = w.inre();
            check(prefix && någonDold && synliga >= 1 && synliga < 8,
                  "överflöde F: ett prefix av textknapparna syns, resten är "
                  "dolda", std::to_string(synliga) + " synliga");
            check(obr.w > 0.0f && std::fabs(obr.x + obr.w - (in.x + in.w)) < 0.5f,
                  "överflöde F: knappen står i fältets ände", rekt(obr));
            const std::vector<glaze::Widget*> ordning = w.focusOrder();
            check(std::find(ordning.begin(), ordning.end(), ob) == ordning.end() &&
                      std::find(ordning.begin(), ordning.end(), &w.sok) !=
                          ordning.end(),
                  "överflöde F: knappen är inget tabbstopp, det dolda sökfältet "
                  "är ett");

            // TRYCKET: menyn byggs, `onDropDown` fyras först, och den öppnas.
            w.klick(obr.x + obr.w / 2.0f, obr.y + obr.h / 2.0f);
            check(ob->isMenuOpen() && nyDrop == 1,
                  "överflöde F: trycket öppnar menyn och fyrar den dolda "
                  "rullgardinens onDropDown en gång",
                  std::to_string(nyDrop));
            const auto& m = ob->menu().items;
            // En post per dold textknapp, i ordning.
            bool knapparna = true;
            int  först     = -1;
            for (std::size_t i = synliga; i < 8; ++i) {
                const int p = post("Knapp " + std::to_string(i + 1));
                if (först < 0)
                    först = p;
                knapparna = knapparna && p == först + int(i - synliga);
            }
            check(först == 0 && knapparna,
                  "överflöde F: de dolda textknapparna först i menyn, i ordning");
            // Separatorerna: EN, och aldrig först eller sist.
            int separatorer = 0;
            for (std::size_t i = 0; i < m.size(); ++i)
                if (m[i].separator)
                    ++separatorer;
            check(separatorer == 1 && !m.empty() && !m.front().separator &&
                      !m.back().separator,
                  "överflöde F: två separatorer i rad blir en, och ingen står "
                  "först eller sist",
                  std::to_string(separatorer));
            const glaze::TrText kontroll = glaze::tr(glaze::str::ToolBarControl);
            const int pv = post("Växla"), pa = post("Av"), pi = post("Ikon"),
                      pn = post("Ny"), pg = post("Ångra"), ps = post("Sök"),
                      pz = post(kontroll.str());
            check(pv >= 0 && m[std::size_t(pv)].checkable &&
                      !m[std::size_t(pv)].checked,
                  "överflöde F: den växlingsbara knappen är en kryssbar post");
            check(pa >= 0 && !m[std::size_t(pa)].enabled,
                  "överflöde F: den inaktiverade knappen är en släckt post");
            check(pi >= 0 && m[std::size_t(pi)].icon.valid(),
                  "överflöde F: en ren ikonknapp har sitt verktygstips och sin "
                  "ikon");
            check(pn >= 0 && m[std::size_t(pn)].submenu.size() == 2 &&
                      m[std::size_t(pn)].submenu[1].text.str() == "Senast",
                  "överflöde F: rullgardinen är en undermeny — med posten "
                  "onDropDown lade till");
            check(pg >= 0 && m[std::size_t(pg)].submenu.size() == 3 &&
                      m[std::size_t(pg)].submenu[0].text.str() == "Ångra" &&
                      m[std::size_t(pg)].submenu[1].separator,
                  "överflöde F: den delade knappen — klicket, en skiljelinje "
                  "och posterna");
            check(post("Zoom:") < 0,
                  "överflöde F: etiketten (kan inte ta fokus) har ingen post");
            check(ps >= 0 && pz >= 0 && m[std::size_t(ps)].enabled,
                  "överflöde F: sökfältet med sitt verktygstips, dropdownen "
                  "utan som \"Kontroll\"");
            check(ögonblicksbild(w).size() == m.size(),
                  "överflöde F: popupens rader är menyns poster",
                  rader(ögonblicksbild(w)));

            // AKTIVERINGEN — knappens egen väg, knappen som sender.
            if (först >= 0) {
                avs = nullptr;
                ob->menu().activatePath({först}, ob);
                check(klick5 == 1 &&
                          avs == &w.k[synliga],
                      "överflöde F: posten klickar den dolda knappen, som är "
                      "sender");
            }
            if (pv >= 0) {
                ob->menu().activatePath({pv}, ob);
                check(w.vaxla.isChecked() && vaxlaByte == 1,
                      "överflöde F: den växlingsbara knappen växlar genom "
                      "performClick");
            }
            if (pa >= 0)
                check(!ob->menu().activatePath({pa}, ob),
                      "överflöde F: den släckta posten aktiveras inte");
            if (pn >= 0) {
                avs = nullptr;
                ob->menu().activatePath({pn, 0}, ob);
                check(nyDok == 1 && avs == &w.ny,
                      "överflöde F: rullgardinens post aktiveras genom knappens "
                      "meny med knappen som sender");
            }
            if (pg >= 0) {
                avs = nullptr;
                ob->menu().activatePath({pg, 0}, ob);
                ob->menu().activatePath({pg, 2}, ob);
                check(angraKlick == 1 && angraAllt == 1 && avs == &w.angra,
                      "överflöde F: den delade knappens klick och post");
            }
            ob->closeMenu();
            break;
        }

        case 2: {
            glaze::ToolButton* ob = w.knapp();
            check(glaze::app().windowCount() == 1,
                  "överflöde F: menyn stängd i pump-svansen");
            // KONTROLLENS POST GER FOKUS — och ett fokuserat barn i
            // överflödet visas i fältet, före knappen, tills fokus går.
            const int ps = post("Sök");
            stämpel      = w.remsa.damageStamp();
            if (ob && ps >= 0)
                ob->menu().activatePath({ps}, ob);
            const gfx::Rectf sr  = w.rektAv(w.sok);
            const gfx::Rectf obr = ob ? w.rektAv(*ob) : gfx::Rectf{};
            check(w.sok.focused() && sr.w > 0.0f && sr.x + sr.w <= obr.x + 0.5f,
                  "överflöde F: posten ger sökfältet fokus, och det visas före "
                  "knappen", rekt(sr) + " " + rekt(obr));
            check(w.remsa.damageStamp() > stämpel,
                  "överflöde F: fokusbytet ritar om fältet (childFocusChanged)");
            stämpel = w.remsa.damageStamp();
            w.utanfor.focus();
            check(!w.sok.focused() && w.rektAv(w.sok).w <= 0.0f &&
                      w.remsa.damageStamp() > stämpel,
                  "överflöde F: fokus utanför — sökfältet döljs igen och fältet "
                  "ritas om", rekt(w.rektAv(w.sok)));
            // Samma regel för Tab-vägen: dropdownen med fokus syns.
            w.zoom.focus();
            check(w.zoom.focused() && w.rektAv(w.zoom).w > 0.0f,
                  "överflöde F: en dold kontroll som får fokus (Tab) visas",
                  rekt(w.rektAv(w.zoom)));
            w.utanfor.focus();
            // AV: ingen knapp, barnen rinner över.
            w.remsa.setOverflow(false);
            check(ob && w.rektAv(*ob).w <= 0.0f &&
                      w.rektAv(w.k[7]).w > 0.0f,
                  "överflöde F: setOverflow(false) — ingen knapp, barnen "
                  "rinner över");
            w.close();
            return;
        }

        default:
            w.close();
            return;
        }
        glaze::app().post(nasta);
    };
    glaze::app().post(nasta);
    w.run();

    if (!kedjanKordes) {
        std::printf("  --   överflödesfallen hoppades över: inget fönster "
                    "kunde skapas (ingen display)\n");
        ++g_skips;
    }
}

// ---- 13. PRICKARNA: ett fält UTAN barn — överflödsknappen finns
// ändå, med en tom rekt i hörnet.
struct PrickVard : glaze::AppWindow {
    glaze::ToolBar remsa;   // Dock::Top
    PrickVard()
        : glaze::AppWindow("toolbar_gate prickar", 300, 120, gfx::Chrome::None)
    {
        root().adopt(remsa);
    }

    // Pixlar i fältets inre yta som avviker från SAMMA RADS mittpixel med mer
    // än 2 per kanal — kanterna (2 px, där hårlinjen står) räknas inte. −1
    // när ytan inte kan läsas.
    int avvikande()
    {
        window().renderFrame();
        std::vector<std::uint8_t> rgba;
        int                       W = 0, H = 0;
        if (!window().captureFrame(rgba, W, H))
            return -1;
        const gfx::Rectf r = remsa.resolve(remsa.behaviorCtx());
        const int        m = static_cast<int>(std::ceil(2.0f * window().uiScale()));
        const int x0 = std::max(0, int(r.x) + m), x1 = std::min(W, int(r.x + r.w) - m);
        const int y0 = std::max(0, int(r.y) + m), y1 = std::min(H, int(r.y + r.h) - m);
        auto at = [&](int x, int y) {
            return &rgba[(std::size_t(y) * std::size_t(W) + std::size_t(x)) * 4u];
        };
        int n = 0;
        for (int y = y0; y < y1; ++y) {
            const std::uint8_t* mitt = at((x0 + x1) / 2, y);
            for (int x = x0; x < x1; ++x)
                for (int k = 0; k < 3; ++k)
                    if (std::abs(int(at(x, y)[k]) - int(mitt[k])) > 2) {
                        ++n;
                        break;
                    }
        }
        return n;
    }
};

void fallPrickar()
{
    PrickVard w;
    w.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        w.invalidate();
        const int full = w.avvikande();
        w.remsa.invalidate();   // en partiell omritning av fältet
        const int partiell = w.avvikande();
        if (full < 0 || partiell < 0) {
            std::printf("  --   13: pixlarna kunde inte läsas (captureFrame) — "
                        "hoppas över\n");
            ++g_skips;
        } else {
            check(full == 0,
                  "13: ett fält utan barn är enfärgat — överflödsknappen med "
                  "tom rekt ritar inga prickar i hörnet (full bild)",
                  std::to_string(full) + " px");
            check(partiell == 0, "13: …och inte efter en partiell omritning",
                  std::to_string(partiell) + " px");
        }
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   fall 13 hoppades över: inget fönster kunde skapas "
                    "(ingen display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("toolbar_gate — verktygsfältet (headless + ett dolt fönster)\n");

    // FÖRE allt annat: bildhandtagen slås upp ur den laddade filen.
    glaze::loadAssets(std::string(GLAZE_TEST_ROOT) + "/apps/demo/assets.bin");

    fallRegistret();
    fallKlick();
    fallFokus();
    fallPlacering();
    fallHarledning();
    fallIkonstorlek();
    fallSlojan();
    fallPixlar();
    fallRullgardin();
    fallStaende();
    fallFlytande();
    fallOverflode();
    fallRullgardinFonster();   // SIST — de två öppnar ett fönster och pumpar
    fallOverflodeFonster();
    fallPrickar();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "TOOLBAR_GATE GRÖN" : "TOOLBAR_GATE FÄLLD",
                g_cases, g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
