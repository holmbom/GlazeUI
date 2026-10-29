#include "widget_gallery.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// APPENS EGNA FÄRGER — DATA och inte tema:
// statusfärgerna på en nod och en rad, och de två färgerna i bilderna ur
// pixlar. Valda för att läsas mot både ljusa och mörka teman; på en VALD
// rad vinner markeringens textfärg, och en inaktiverad widget dimmar allt.
constexpr gfx::Color kAndrad{214, 118, 44, 255};   // en ändrad fil
constexpr gfx::Color kFel{204, 68, 68, 255};       // ett fel
constexpr gfx::Color kKlar{64, 150, 84, 255};      // klart
constexpr gfx::Color kBildA{44, 112, 196, 255};    // bildernas två färger
constexpr gfx::Color kBildB{246, 186, 58, 255};

// En BILD UR PIXLAR: `n` × `n` pixlar som appen räknar fram —
// fyra ringar i två färger ut till en genomskinlig rand, så skalningen syns.
// Det en app annars hade fått ur en server, en kamera eller en heatmap.
glaze::Bitmap ringar(int n, gfx::Color a, gfx::Color b)
{
    std::vector<std::uint8_t> px(std::size_t(n) * std::size_t(n) * 4);
    const float mitt  = float(n - 1) / 2.0f;
    const float bredd = float(n) / 8.0f;   // fyra ringar per radie
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float      r = std::hypot(float(x) - mitt, float(y) - mitt);
            const gfx::Color f = int(r / bredd) % 2 ? b : a;
            std::uint8_t*    p = &px[(std::size_t(y) * std::size_t(n) +
                                      std::size_t(x)) * 4];
            p[0] = f.r;
            p[1] = f.g;
            p[2] = f.b;
            p[3] = r <= float(n) / 2.0f ? f.a : 0;   // hörnen genomskinliga
        }
    }
    return glaze::Bitmap(n, n, px.data());
}

// FÄRGEN SOM `#RRGGBBAA` — färgdialogens svar i statusraden. Dialogen är
// ingen widget och har ingen `colorText()` (färgväljarfältets textform), så
// appen skriver sin egen.
std::string hexText(gfx::Color c)
{
    char s[10];
    std::snprintf(s, sizeof s, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    return s;
}

} // namespace

// ---------------------------------------------------------------- hjälpare

glaze::Panel* WidgetGallery::grupp(glaze::Widget* sida, std::uint32_t rubrik,
                                   const std::string& namn,
                                   float x, float y, float w, float h)
{
    auto* p   = sida->add<glaze::Panel>();
    p->x      = x;
    p->y      = y;
    p->width  = w;
    p->height = h;
    p->name   = namn;
    p->setBorder(true);

    auto* r = p->add<glaze::Label>(glaze::tr(rubrik));
    r->x    = 8;
    r->y    = 6;
    r->name = namn + "Rub";

    // Separatorn ANKRAS under rubriken i stället för att få ett spikat
    // y-tal: rubrikens höjd kommer ur Style, och Slates radhöjd är inte
    // Minimalistics. Ett tal hade varit rätt i exakt en stil — vilket är
    // hela skälet att syskonankaret finns.
    auto* s      = p->add<glaze::Separator>();
    s->x         = 8;
    s->anchorTop = {r->name, 6};
    s->anchors   = {true, true, true, false};
    s->right     = 8;
    s->name      = namn + "Sep";
    return p;
}

// En FLIK: sidan är ett ÄGT barn till galleriets flikkontroll och fliktiteln
// sidans `text` — ett id, så rubriken följer språket. Remsorna visas: blir
// sidan högre än ytan (ett litet fönster, en tät stil) rullar den, i stället
// för att innehållet klipps bort.
glaze::TabPage* WidgetGallery::sida(std::uint32_t titel)
{
    auto* s = flikar_->add<glaze::TabPage>(glaze::tr(titel));
    s->showScrollbars(true);
    return s;
}

glaze::TabPage* WidgetGallery::nySida(glaze::TabControl* tc,
                                      const std::string& titel)
{
    // `addPage` — sidornas `addX` i kollektionsvokabulären,
    // WinForms `TabPages.Add`: en ÄGD sida sist, och referensen tillbaka så
    // att koden kan fylla den.
    glaze::TabPage& s = tc->addPage(titel);
    auto*           l = s.add<glaze::Label>(glaze::tr(demo::str::FlikSidaText));
    l->x              = 8;
    l->y              = 8;
    return &s;
}

glaze::Label* WidgetGallery::fot(glaze::Panel* g, std::uint32_t text,
                                 const std::string& over, float gap)
{
    auto* l      = g->add<glaze::Label>(glaze::tr(text));
    l->x         = 8;
    l->anchorTop = {over, gap};
    l->setRole(glaze::Label::Role::Dim);
    l->name      = over + "Fot";
    return l;
}

// ------------------------------------------------------------------ bygget

WidgetGallery::WidgetGallery()
{
    setTitle(glaze::tr(demo::str::GalleriTitel));
    // FLIKAR: grupperna ligger i en flikkontroll, en flik per tema — en enda
    // rad kolumner hade blivit bredare än en skärm. En sida rymmer
    // fyra kolumner (x = 12 / 328 / 644 / 960, alla 304 breda) och den
    // högsta gruppen (g3, 484).
    //
    // GRUPPERNAS HÖJDER ÄR MÄTTA mot innehållet i alla tre UI (g3 484,
    // g6/g7 212, g8 208): en lägre grupp klipper sitt innehåll — den
    // inaktiverade listan i g3 syns inte alls och fötterna i g6/g7 klipps.
    //
    // MÅTTEN ÄR RÄKNADE, inte provade. De två
    // sättarna talar OLIKA baser och får aldrig blandas ihop:
    //
    //   setContentSize  INNEHÅLLSYTAN  = exakt den yta ett icke-dockat
    //                                    barn får (menyrad/statusrad är
    //                                    redan avdragna)
    //   setMinimumSize  logiska KLIENTMÅTT = innehållsytan + chromet
    //                                    (barHeight 32 + menuRowH 24 +
    //                                    statusBarH 24 = 80 i Slate)
    //
    // Bredden: sidan behöver 12 + 4×304 + 3×12 + 12 = 1276, flikkontrollens
    // ram tar `panelInset` (1) på var sida och fönstret 12 runt om:
    // 1276 + 2 + 24 = **1302**. Höjden: 12 + flikraden (`tabHeaderH`, högst
    // 30 i Glaze) + 2 + sidan (12 + 484 + 12 = 508) + 12 + stängknappen (högst
    // 27.3, Glaze) + 12 = 603.3 → **610**, och minimimåttet 610 + 80 = 690.
    //
    // FLIKRADEN OCH KNAPPEN är stilens mått, och flikkontrollen är därför
    // SPÄND mellan fönstrets överkant och stängknappens överkant
    // (`anchorBottom` mot knappen) i stället för att få en spikad höjd: en
    // tätare stil ger sidan mer luft, en grövre mindre — och blir fönstret
    // ändå för lågt RULLAR sidan (`sida()` slår på remsorna) i stället för
    // att klippa bort innehåll.
    setContentSize(1302, 610);
    setMinimumSize(1302, 690);

    // STÄNGKNAPPEN FÖRST: flikkontrollens underkant ankras mot den, och ett
    // syskonankare kan bara peka på ett TIDIGARE syskon.
    auto* stang    = add<glaze::Button>(glaze::tr(demo::str::Stang));
    stang->anchors = {false, false, true, true};
    stang->right   = 12;
    stang->bottom  = 12;
    stang->name    = "stang";
    stang->onClick = [this] { close(); };

    flikar_               = add<glaze::TabControl>();
    flikar_->x            = 12;
    flikar_->y            = 12;
    flikar_->anchors      = {true, true, true, false};
    flikar_->right        = 12;
    flikar_->anchorBottom = {"stang", 12};
    flikar_->name         = "flikar";
    glaze::TabPage* sGrund  = sida(demo::str::FlikGrund);
    glaze::TabPage* sText   = sida(demo::str::FlikText);
    glaze::TabPage* sYtor   = sida(demo::str::FlikYtor);
    glaze::TabPage* sData   = sida(demo::str::FlikData);
    glaze::TabPage* sLayout = sida(demo::str::FlikLayout);
    glaze::TabPage* sInter  = sida(demo::str::FlikInteraktion);
    // VÄLJARNA — komponenter som väljer ett VÄRDE i en popup.
    glaze::TabPage* sVal    = sida(demo::str::FlikValjare);
    // RITYTAN — byggs i gallery_canvas.cpp.
    glaze::TabPage* sRita   = sida(demo::str::FlikRityta);
    // 3D — byggs i gallery_3d.cpp, och
    // 3D-verktygen i gallery_3d_tools.cpp.
    glaze::TabPage* s3D     = sida(demo::str::Flik3D);
    glaze::TabPage* s3DVerk = sida(demo::str::Flik3DVerktyg);
    flikar_->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
        flikBytt(s, e);
    };

    // === 1 · Label, Button, Link, Separator ==============================
    {
        auto* g = grupp(sGrund, demo::str::G1, "g1", 12, 12, 304, 214);

        auto* lblN      = g->add<glaze::Label>(glaze::tr(demo::str::LabelNormal));
        lblN->x         = 8;
        lblN->anchorTop = {"g1Sep", 10};
        lblN->name      = "lblN";

        auto* lblD      = g->add<glaze::Label>(glaze::tr(demo::str::LabelAv));
        lblD->x         = 8;
        lblD->anchorTop = {"lblN", 6};
        lblD->name      = "lblD";
        lblD->disable();   // <- vid widgeten, inte i onLoad

        auto* btnN      = g->add<glaze::Button>(glaze::tr(demo::str::ButtonNormal));
        btnN->x         = 8;
        btnN->anchorTop = {"lblD", 10};
        btnN->name      = "btnN";
        btnN->setImg(glaze::img("toolbar", "ok"));

        // SYSKONANKARE I SIDLED: knappen bredvid grannen, inte på ett
        // spikat x. Byter man UI krymper "Button" och den här följer med.
        auto* btnD       = g->add<glaze::Button>(glaze::tr(demo::str::ButtonAv));
        btnD->anchorLeft = {"btnN", 10};
        btnD->anchorTop  = {"lblD", 10};
        btnD->name       = "btnD";
        btnD->setImg(glaze::img("toolbar", "cancel"));
        btnD->disable();

        auto* lnkN      = g->add<glaze::Link>(glaze::tr(demo::str::LinkNormal));
        lnkN->x         = 8;
        lnkN->anchorTop = {"btnN", 12};
        lnkN->name      = "lnkN";
        lnkN->setUrl("https://github.com/holmbom/GlazeUI");

        auto* lnkD       = g->add<glaze::Link>(glaze::tr(demo::str::LinkAv));
        lnkD->anchorLeft = {"lnkN", 16};
        lnkD->anchorTop  = {"btnN", 12};
        lnkD->name       = "lnkD";
        lnkD->disable();

        auto* sepD      = g->add<glaze::Separator>();
        sepD->x         = 8;
        sepD->anchorTop = {"lnkN", 12};
        sepD->anchors   = {true, true, true, false};
        sepD->right     = 8;
        sepD->name      = "sepD";
        sepD->disable();

        fot(g, demo::str::SepFot, "sepD", 8);
    }

    // === 2 · Checkbox, Radio ============================================
    {
        auto* g = grupp(sGrund, demo::str::G2, "g2", 328, 12, 304, 186);

        auto* chkN      = g->add<glaze::Checkbox>(glaze::tr(demo::str::ChkNormal));
        chkN->x         = 8;
        chkN->anchorTop = {"g2Sep", 10};
        chkN->name      = "chkN";
        chkN->setChecked(true);

        auto* chkD      = g->add<glaze::Checkbox>(glaze::tr(demo::str::ChkAv));
        chkD->x         = 8;
        chkD->anchorTop = {"chkN", 6};
        chkD->name      = "chkD";
        chkD->setChecked(true);
        chkD->disable();

        // RADIOGRUPPEN loopas. I `.glazeui` står varje rad utskriven —
        // en loop går inte att emittera, och det är inte en brist i
        // grammatiken utan i vad en WYSIWYG-yta kan visa.
        const std::uint32_t alt[] = {demo::str::AlternativA,
                                     demo::str::AlternativB,
                                     demo::str::AltInaktiverad};
        std::string over   = "chkD";
        for (int i = 0; i < 3; ++i) {
            auto* r      = g->add<glaze::Radio>(glaze::tr(alt[i]));
            r->x         = 8;
            r->name      = std::string("rad") + char('A' + i);
            r->anchorTop = {over, i == 0 ? 10.0f : 4.0f};
            r->setGroup("galleri");
            if (i == 0)
                r->setChecked(true);
            if (i == 2)
                r->disable();
            over = r->name;
        }
    }

    // === 31 · Checkbox med tre lägen ==========
    {
        // Under g2 i samma kolumn. 194 HÖG: fem rutor och foten, RÄKNAT i
        // Slates radhöjd (rutan 18, texten högst 17,3) — inte renderat, se
        // doc/os/windows/TESTS.md.
        auto* g = grupp(sGrund, demo::str::G31Tre, "g31", 328, 210, 304, 194);

        // TRE LÄGEN för ANVÄNDAREN: klicket går av → på → mellanläge → av
        // (WinForms `ThreeState`). Utan flaggan går ett klick aldrig TILL
        // mellanläget — det sätter appen.
        auto* tre      = g->add<glaze::Checkbox>(glaze::tr(demo::str::TreKlick));
        tre->x         = 8;
        tre->anchorTop = {"g31Sep", 10};
        tre->name      = "treKlick";
        tre->setThreeState(true);
        tre->setCheckState(glaze::CheckState::Indeterminate);

        // "ALLA LAGER" — förälderns läge är APPENS: mellanläget när bara
        // några av lagren är ikryssade. Ett klick i den kryssar alla eller
        // inga; ur mellanläget går klicket till av, för `isChecked()` är sann
        // där (WinForms regel). Lagren är kartans (1483–1486) och indragna
        // under föräldern. Settrarna fyrar inget, så de två handlarna
        // väcker aldrig varandra.
        auto* alla      = g->add<glaze::Checkbox>(glaze::tr(demo::str::TreAlla));
        alla->x         = 8;
        alla->anchorTop = {"treKlick", 6};
        alla->name      = "treAlla";
        std::vector<glaze::Checkbox*> lager;
        std::string                   över = alla->name;
        for (std::uint32_t id : {demo::str::LagerVagar, demo::str::LagerYtor}) {
            auto* l      = g->add<glaze::Checkbox>(glaze::tr(id));
            l->x         = 28;
            l->anchorTop = {över, 4};
            l->name      = "treLager" + std::to_string(lager.size());
            över         = l->name;
            lager.push_back(l);
        }
        lager.front()->setChecked(true);
        alla->setCheckState(glaze::CheckState::Indeterminate);
        alla->onChange = [alla, lager] {
            for (glaze::Checkbox* l : lager)
                l->setChecked(alla->isChecked());
        };
        for (glaze::Checkbox* l : lager)
            l->onChange = [alla, lager] {
                const auto på = std::count_if(
                    lager.begin(), lager.end(),
                    [](const glaze::Checkbox* x) { return x->isChecked(); });
                alla->setCheckState(
                    på == 0 ? glaze::CheckState::Unchecked
                    : på == static_cast<std::ptrdiff_t>(lager.size())
                        ? glaze::CheckState::Checked
                        : glaze::CheckState::Indeterminate);
            };

        // INAKTIVERAD i mellanläget — strecket tonas som bocken gör.
        auto* av      = g->add<glaze::Checkbox>(glaze::tr(demo::str::TreAv));
        av->x         = 8;
        av->anchorTop = {över, 6};
        av->name      = "treAv";
        av->setCheckState(glaze::CheckState::Indeterminate);
        av->disable();

        fot(g, demo::str::TreFot, "treAv", 8);
    }

    // === 3 · TextInput, Dropdown, ListBox ===============================
    {
        auto* g = grupp(sText, demo::str::G3, "g3", 12, 12, 304, 484);

        auto* txtN      = g->add<glaze::TextInput>();
        txtN->x         = 8;
        txtN->anchorTop = {"g3Sep", 10};
        txtN->anchors   = {true, true, true, false};
        txtN->right     = 8;
        txtN->name      = "txtN";
        txtN->setText(trs(demo::str::TxtNormal));

        auto* txtD      = g->add<glaze::TextInput>();
        txtD->x         = 8;
        txtD->anchorTop = {"txtN", 6};
        txtD->anchors   = {true, true, true, false};
        txtD->right     = 8;
        txtD->name      = "txtD";
        txtD->setText(trs(demo::str::TxtAv));
        txtD->disable();

        auto* txtR      = g->add<glaze::TextInput>();
        txtR->x         = 8;
        txtR->anchorTop = {"txtD", 6};
        txtR->anchors   = {true, true, true, false};
        txtR->right     = 8;
        txtR->name      = "txtR";
        txtR->setText(trs(demo::str::TxtSkrivskyddad));
        txtR->setReadOnly(true);

        auto* ddN      = g->add<glaze::Dropdown>();
        ddN->x         = 8;
        ddN->anchorTop = {"txtR", 10};
        ddN->anchors   = {true, true, true, false};
        ddN->right     = 8;
        ddN->name      = "ddN";
        ddN->setItems({trs(demo::str::DdNormal), trs(demo::str::DdAndra),
                       trs(demo::str::DdTredje)});
        ddN->setSelectedIndex(0);

        auto* ddD      = g->add<glaze::Dropdown>();
        ddD->x         = 8;
        ddD->anchorTop = {"ddN", 6};
        ddD->anchors   = {true, true, true, false};
        ddD->right     = 8;
        ddD->name      = "ddD";
        ddD->setItems({trs(demo::str::DdAv)});
        ddD->setSelectedIndex(0);
        ddD->disable();

        auto* lstN      = g->add<glaze::ListBox>();
        lstN->x         = 8;
        lstN->anchorTop = {"ddD", 10};
        lstN->height    = 82;
        lstN->anchors   = {true, true, true, false};
        lstN->right     = 8;
        lstN->name      = "lstN";
        lstN->setItems({trs(demo::str::LstNormal), trs(demo::str::Rad2),
                        trs(demo::str::Rad3), trs(demo::str::Rad4)});
        lstN->setSelectedIndex(0);

        // `setChecked(rad, på)` tar TVÅ argument och kan inte emitteras
        // som en setter-rad (writern skriver `set<Fält>(<ett värde>)`) —
        // i den genererade vägen bor de här två raderna i `onLoad`.
        auto* lstC      = g->add<glaze::ListBox>();
        lstC->x         = 8;
        lstC->anchorTop = {"lstN", 8};
        lstC->height    = 82;
        lstC->anchors   = {true, true, true, false};
        lstC->right     = 8;
        lstC->name      = "lstC";
        lstC->setItems({trs(demo::str::MedKryssrutor), trs(demo::str::Rad2),
                        trs(demo::str::Rad3), trs(demo::str::Rad4)});
        lstC->setShowChecks(true);
        lstC->setChecked(0, true);
        lstC->setChecked(3, true);
        // RULLNINGENS RIKTNING PER WIDGET: mushjulet rullar
        // den här listan åt andra hållet — resten av fönstret ärver appens.
        lstC->setWheelInvert(glaze::InputDevice::Mouse, true, false);
        lstC->setToolTip(glaze::tr(demo::str::InmListaVand));

        auto* lstD      = g->add<glaze::ListBox>();
        lstD->x         = 8;
        lstD->anchorTop = {"lstC", 8};
        lstD->height    = 60;
        lstD->anchors   = {true, true, true, false};
        lstD->right     = 8;
        lstD->name      = "lstD";
        lstD->setItems({trs(demo::str::LstAv), trs(demo::str::Rad2)});
        lstD->disable();
    }

    // === 4 · Slider, Progress ===========================================
    {
        auto* g = grupp(sGrund, demo::str::G4, "g4", 644, 12, 304, 218);

        auto* sldN      = g->add<glaze::Slider>();
        sldN->x         = 8;
        sldN->anchorTop = {"g4Sep", 12};
        sldN->anchors   = {true, true, true, false};
        sldN->right     = 8;
        sldN->name      = "sldN";
        sldN->setMin(0);
        sldN->setMax(100);
        sldN->setStep(5);
        sldN->setValue(40);
        sldN->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            sldChanged(s, e);
        };

        auto* sldD      = g->add<glaze::Slider>();
        sldD->x         = 8;
        sldD->anchorTop = {"sldN", 10};
        sldD->anchors   = {true, true, true, false};
        sldD->right     = 8;
        sldD->name      = "sldD";
        sldD->setValue(70);
        sldD->disable();

        prgN_            = g->add<glaze::Progress>();
        prgN_->x         = 8;
        prgN_->anchorTop = {"sldD", 14};
        prgN_->anchors   = {true, true, true, false};
        prgN_->right     = 8;
        prgN_->name      = "prgN";
        prgN_->setValue(40);

        auto* prgD      = g->add<glaze::Progress>();
        prgD->x         = 8;
        prgD->anchorTop = {"prgN", 8};
        prgD->anchors   = {true, true, true, false};
        prgD->right     = 8;
        prgD->name      = "prgD";
        prgD->setValue(70);
        prgD->disable();

        // Obestämt läge animerar — widgeten driver sin egen animation och
        // stänger av den själv (regel 1: aldrig en render-loop).
        auto* prgI      = g->add<glaze::Progress>();
        prgI->x         = 8;
        prgI->anchorTop = {"prgD", 8};
        prgI->anchors   = {true, true, true, false};
        prgI->right     = 8;
        prgI->name      = "prgI";
        prgI->setIndeterminate(true);

        fot(g, demo::str::PrgFot, "prgI", 8);
    }

    // === 5 · Picture, Scrollbar =========================================
    {
        auto* g = grupp(sGrund, demo::str::G5, "g5", 960, 12, 304, 182);

        auto* picN      = g->add<glaze::Picture>();
        picN->x         = 8;
        picN->anchorTop = {"g5Sep", 10};
        picN->width     = 32;
        picN->height    = 32;
        picN->name      = "picN";
        picN->setImg(glaze::img("toolbar", "ok"));

        auto* picD       = g->add<glaze::Picture>();
        picD->anchorLeft = {"picN", 12};
        picD->anchorTop  = {"g5Sep", 10};
        picD->width      = 32;
        picD->height     = 32;
        picD->name       = "picD";
        picD->setImg(glaze::img("toolbar", "cancel"));
        picD->disable();

        // UR PIXLAR: samma widget och samma `setImg` som
        // assetbilderna, men bilden är en `glaze::Bitmap` som appen räknar
        // fram här — 64 × 64 pixlar i en ruta på 32 × 32, så den är skarp
        // på en skärm med skala 2. Pixlarnas LOGISKA storlek är pixlarna
        // delat med skalan: utan satt storlek hade rutan blivit 64 px bred på
        // skala 1 och 32 på skala 2.
        auto* picPix       = g->add<glaze::Picture>();
        picPix->anchorLeft = {"picD", 12};
        picPix->anchorTop  = {"g5Sep", 10};
        picPix->width      = 32;
        picPix->height     = 32;
        picPix->name       = "picPix";
        picPix->setImg(ringar(64, kBildA, kBildB));

        // Sträckt och radbruten: texten ryms inte på en rad bredvid tre
        // bilder i alla fyra språken, och höjden följer bredden.
        auto* picTxt       = g->add<glaze::Label>(glaze::tr(demo::str::PicFot));
        picTxt->anchorLeft = {"picPix", 12};
        picTxt->anchorTop  = {"g5Sep", 10};
        picTxt->anchors    = {true, true, true, false};
        picTxt->right      = 8;
        picTxt->name       = "picTxt";
        picTxt->setWrap(true);
        picTxt->setRole(glaze::Label::Role::Dim);

        // VÅGRÄTA — och det måste SÄGAS: `Scrollbar` står lodrätt som
        // default. Utan raden blir de två en 200 px hög lodrät list
        // utsträckt över gruppens hela bredd.
        auto* sbH      = g->add<glaze::Scrollbar>();
        sbH->setOrientation(glaze::Scrollbar::Orientation::Horizontal);
        sbH->x         = 8;
        sbH->anchorTop = {"picN", 14};
        sbH->anchors   = {true, true, true, false};
        sbH->right     = 8;
        sbH->name      = "sbH";

        auto* sbHD      = g->add<glaze::Scrollbar>();
        sbHD->setOrientation(glaze::Scrollbar::Orientation::Horizontal);
        sbHD->x         = 8;
        sbHD->anchorTop = {"sbH", 10};
        sbHD->anchors   = {true, true, true, false};
        sbHD->right     = 8;
        sbHD->name      = "sbHD";
        sbHD->disable();

        fot(g, demo::str::SbFot, "sbHD", 8);
    }

    // === 6 · Panel med overlay-remsor ===================================
    {
        auto* g = grupp(sYtor, demo::str::G6, "g6", 12, 12, 304, 212);

        // Remsorna är opt-in OCH kräver innehåll som svämmar över, annars
        // ritas de aldrig. Fem barn i en liten ruta ger båda riktningarna.
        auto* ps      = g->add<glaze::Panel>();
        ps->x         = 8;
        ps->anchorTop = {"g6Sep", 10};
        ps->width     = 140;
        ps->height    = 130;
        ps->name      = "panScroll";
        ps->setBorder(true);
        ps->showScrollbars(true);

        std::string over;
        for (int i = 0; i < 4; ++i) {
            auto* b   = ps->add<glaze::Button>(
                trs(demo::str::KnappPrefix) + char('A' + i));
            b->x      = 8;
            b->name   = std::string("ps") + char('A' + i);
            if (i == 0)
                b->y = 8;
            else
                b->anchorTop = {over, 6};
            over = b->name;
        }
        auto* psE      = ps->add<glaze::Label>(glaze::tr(demo::str::EnEtikettTill));
        psE->x         = 8;
        psE->anchorTop = {over, 6};
        psE->name      = "psE";

        // `enabled` kaskadar INTE ner i trädet — en inaktiverad container
        // måste släcka sina barn själv. Här syns det som en loop.
        auto* pd       = g->add<glaze::Panel>();
        pd->anchorLeft = {"panScroll", 12};
        pd->anchorTop  = {"g6Sep", 10};
        pd->width      = 130;
        pd->height     = 130;
        pd->name       = "panDis";
        pd->setBorder(true);
        pd->showScrollbars(true);
        pd->disable();

        over.clear();
        for (int i = 0; i < 4; ++i) {
            auto* b = pd->add<glaze::Button>(
                trs(demo::str::AvPrefix) + char('A' + i));
            b->x    = 8;
            b->name = std::string("pd") + char('A' + i);
            if (i == 0)
                b->y = 8;
            else
                b->anchorTop = {over, 6};
            b->disable();
            over = b->name;
        }

        fot(g, demo::str::PanFot,
            "panScroll", 8);
    }

    // === 7 · PropertyGrid ===============================================
    {
        auto* g = grupp(sText, demo::str::G7, "g7", 644, 12, 304, 212);

        // Raderna är DATA, inte widgets — och de kan inte uttryckas som en
        // fältrad, så i den genererade vägen bor det här i `onLoad`.
        std::vector<glaze::PropertyGrid::Row> rader;
        rader.push_back({"name", "okBtn", {}, true, ""});
        rader.push_back({"text", "Tryck här", {}, false, ""});
        rader.push_back({"dock", "None",
                         {"None", "Left", "Top", "Right", "Bottom", "Fill"},
                         false, ""});
        rader.push_back({"width", "120", {}, false, ""});

        auto* grid1      = g->add<glaze::PropertyGrid>();
        grid1->x         = 8;
        grid1->anchorTop = {"g7Sep", 10};
        grid1->height    = 72;
        grid1->anchors   = {true, true, true, false};
        grid1->right     = 8;
        grid1->name      = "grid1";
        grid1->setNameWidth(56);
        grid1->setRows(rader);
        grid1->onChange  = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            gridChanged(s, e);
        };

        auto* grid2      = g->add<glaze::PropertyGrid>();
        grid2->x         = 8;
        grid2->anchorTop = {"grid1", 10};
        grid2->height    = 72;
        grid2->anchors   = {true, true, true, false};
        grid2->right     = 8;
        grid2->name      = "grid2";
        grid2->setNameWidth(56);
        grid2->setRows(rader);
        grid2->disable();
    }

    // === 8 · Bar, Spacer, CaptionButton, TopBar, StatusBar ==============
    {
        auto* g = grupp(sYtor, demo::str::G8, "g8", 328, 12, 304, 208);

        // Baren PLACERAR sina barn själv (flexen) — dockade/ankrade barn
        // ignoreras där, och Spacer sväljer det lediga utrymmet.
        auto* barN      = g->add<glaze::Bar>();
        barN->x         = 8;
        barN->anchorTop = {"g8Sep", 10};
        barN->anchors   = {true, true, true, false};
        barN->right     = 8;
        barN->name      = "barN";
        barN->add<glaze::Button>(glaze::tr(demo::str::BarKnapp));
        barN->addSpacer();
        barN->add<glaze::CaptionButton>()->setKind(
            glaze::CaptionButton::Kind::Minimize);
        barN->add<glaze::CaptionButton>()->setKind(
            glaze::CaptionButton::Kind::Maximize);
        barN->add<glaze::CaptionButton>()->setKind(
            glaze::CaptionButton::Kind::Close);

        auto* barD      = g->add<glaze::Bar>();
        barD->x         = 8;
        barD->anchorTop = {"barN", 10};
        barD->anchors   = {true, true, true, false};
        barD->right     = 8;
        barD->name      = "barD";
        barD->disable();
        // Barnen släcks var för sig (enabled kaskadar inte).
        auto* bd1 = barD->add<glaze::Button>(glaze::tr(demo::str::BarAv));
        bd1->disable();
        barD->addSpacer()->disable();
        for (glaze::CaptionButton::Kind k :
             {glaze::CaptionButton::Kind::Minimize,
              glaze::CaptionButton::Kind::Maximize,
              glaze::CaptionButton::Kind::Close}) {
            auto* cb = barD->add<glaze::CaptionButton>();
            cb->setKind(k);
            cb->disable();
        }

        auto* tb      = g->add<glaze::TopBar>();
        tb->x         = 8;
        tb->anchorTop = {"barD", 10};
        tb->anchors   = {true, true, true, false};
        tb->right     = 8;
        tb->name      = "topBarD";
        tb->disable();
        auto* tbT = tb->add<glaze::Label>(glaze::tr(demo::str::TopBarAv));
        tbT->setRole(glaze::Label::Role::Titlebar);
        tbT->disable();
        tb->addSpacer()->disable();

        auto* sd      = g->add<glaze::StatusBar>();
        // `StatusBar`-ctorn sätter `dock = Dock::Bottom`, och här är den
        // fel: gruppen VISAR widgeten i en ankarkedja, den är inte
        // gruppens statusrad. Dockad vinner gren 1 över ankarraderna nedan och
        // skär dessutom 24 px ur alla syskonexempel i g8. Nollas därför
        // uttryckligen.
        sd->dock      = glaze::Dock::None;
        sd->x         = 8;
        sd->anchorTop = {"topBarD", 10};
        sd->anchors   = {true, true, true, false};
        sd->right     = 8;
        sd->name      = "statusD";
        sd->disable();
        // FÄLTEN: texten FYLLER, procenten är AUTO
        // (texten och luften) och kodningen FAST — hårlinjen skiljer dem. Ett
        // fält är radens INREDNING, så en inaktiverad rad dämpar sina fält
        // själv, också de som läggs till efter `disable()` (som här). Talet och
        // kodningens namn är data och översätts inte.
        sd->addPanel(glaze::tr(demo::str::StatusBarAv), 0, true);
        sd->addPanel("100 %");
        sd->addPanel("UTF-8", 48);
    }

    // === 9 · ToolBar, ToolButton ==============================
    {
        // Höjden: de två första remsorna med sin fot (154) plus rullgardinens
        // remsa, dess lucka, en fotrad och luften till den (30 + 10 + 18 + 8).
        auto* g = grupp(sYtor, demo::str::G9, "g9", 644, 12, 304, 220);

        // Två remsor med SAMMA innehåll — aktiv och inaktiverad. Remsans
        // barn är RIKTIGA widgets (verktygsknappar och en vanlig Separator
        // på högkant), byggda i en lambda: en loop över en remsa går inte
        // att emittera i `.glazeui`, och det är handkodsvägens styrka att
        // den kan.
        //
        // Ikonen ur pixlar räknas fram EN gång: 32 × 32 pixlar, skarp i
        // stilens ikonruta på skala 2. Den är en MEDLEM — en `Bitmap` är ett
        // handtag, så båda remsornas knappar delar den (en textur per
        // fönster), och en app som skriver om pixlarna når båda.
        pixelIkon_ = ringar(32, kBildA, kBildB);
        auto remsa = [&](const std::string& namn, const std::string& över,
                         bool av) {
            auto* tb = g->add<glaze::ToolBar>();
            // ToolBar dockar Top som DEFAULT (WinForms ToolStrip). Här
            // VISAS den i en ankarkedja och är inte gruppens remsa — samma
            // nollställning som statusraden i g8.
            tb->dock      = glaze::Dock::None;
            tb->x         = 8;
            tb->anchorTop = {över, 10};
            tb->anchors   = {true, true, true, false};
            tb->right     = 8;
            tb->name      = namn;

            // `enabled` kaskadar INTE — varje barn släcks för sig, som i g8.
            auto släck = [av](glaze::Widget* w) {
                if (av)
                    w->disable();
                return w;
            };
            // Bilden är en asset ELLER pixlar — samma `setImg` tar båda.
            auto verktyg = [&](glaze::ImageSource bild, const char* suffix) {
                auto* b = tb->add<glaze::ToolButton>();
                b->setImg(std::move(bild));
                b->name    = namn + suffix;
                b->onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) {
                    verktygKlickat(s, e);
                };
                släck(b);
                return b;
            };
            auto skiljelinje = [&] {
                // I en bar står separatorn TVÄRS baren av sig själv.
                släck(tb->add<glaze::Separator>());
            };

            verktyg(glaze::img("toolbar", "ok"), "Ok");
            verktyg(glaze::img("toolbar", "cancel"), "Avbryt");
            skiljelinje();
            // Två LÄGEN: ett ikryssat och ett av. Klicket slår om dem och
            // fyrar onChange före onClick — statusraden visar läget.
            auto* info = verktyg(glaze::img("toolbar", "info"), "Info");
            info->setCheckable(true);
            info->setChecked(true);
            verktyg(glaze::img("toolbar", "gear"), "Kugg")->setCheckable(true);
            // UR PIXLAR: appens egen bild, och i den inaktiverade
            // remsan samma tonning som assetbilderna.
            verktyg(pixelIkon_, "Pixlar");
            skiljelinje();
            // Ren TEXT fungerar lika bra som en ikon.
            auto* text = tb->add<glaze::ToolButton>(
                glaze::tr(demo::str::VerktygText));
            text->name    = namn + "Text";
            text->onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) {
                verktygKlickat(s, e);
            };
            släck(text);
            släck(tb);
        };
        remsa("tbN", "g9Sep", false);
        remsa("tbD", "tbN", true);

        fot(g, demo::str::VerktygFot, "tbD", 8);

        // RULLGARDINERNA — en tredje remsa. "Ny ▾"
        // har menyn bakom HELA knappen; "Ångra ▾" är DELAD: knappen ångrar
        // det senaste, och pilen visar historiken, som byggs om ur
        // `angraHistorik_` i `onDropDown` — FÖRE menyn öppnas, den dynamiska
        // menyns form (senaste filer). Bredvid dem samma två inaktiverade.
        // Menyn har systemfältsikonens vokabulär: `{}` är roten, och
        // handlern sätts per stig i kod.
        angraHistorik_ = {demo::str::HistorikSkriv, demo::str::HistorikFlytta,
                          demo::str::HistorikRadera};
        auto* tbM      = g->add<glaze::ToolBar>();
        tbM->dock      = glaze::Dock::None;   // visad i ankarkedjan, som ovan
        tbM->x         = 8;
        tbM->anchorTop = {"tbDFot", 10};
        tbM->anchors   = {true, true, true, false};
        tbM->right     = 8;
        tbM->name      = "tbM";
        auto knappar = [&](bool av) {
            const std::string suffix = av ? "Av" : "";
            auto* ny = tbM->add<glaze::ToolButton>(
                glaze::tr(demo::str::VerktygNy));
            ny->name = "tbMNy" + suffix;
            for (const std::uint32_t id :
                 {demo::str::NyDokument, demo::str::NyMapp}) {
                ny->addItem({}, glaze::tr(id));
                ny->setHandler(glaze::tr(id), [this, id] {
                    statusTxt_->setText(trs(demo::str::MenyvalPrefix) +
                                        trs(id));
                });
            }
            auto* angra = tbM->add<glaze::ToolButton>(
                glaze::tr(demo::str::VerktygAngra));
            angra->name = "tbMAngra" + suffix;
            angra->setSplit(true);
            byggAngraMeny(*angra);
            angra->onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) {
                angraKlickat(s, e);
            };
            angra->onDropDown = [this](glaze::Widget* s, glaze::EventArgs& e) {
                angraMeny(s, e);
            };
            if (av) {   // `enabled` kaskadar inte — varje knapp för sig
                ny->disable();
                angra->disable();
            }
        };
        knappar(false);
        tbM->add<glaze::Separator>();   // tvärs baren av sig själv
        knappar(true);

        fot(g, demo::str::RullgardinFot, "tbM", 8);
    }

    // === 10 · GroupBox ========================================
    {
        auto* g = grupp(sYtor, demo::str::G10, "g10", 960, 12, 304, 164);

        // INGA MÅTT på ramarna: en gruppram AUTOSIZAR ur sina barn
        // (kompositstödets opt-in) — bredden blir barnens eller rubrikens,
        // höjden barnens plus luften. Byter man UI krymper allt med.
        auto* gbN      = g->add<glaze::GroupBox>(glaze::tr(demo::str::GbUtskrift));
        gbN->x         = 8;
        gbN->anchorTop = {"g10Sep", 10};
        gbN->name      = "gbN";
        auto* c1       = gbN->add<glaze::Checkbox>(
            glaze::tr(demo::str::GbDubbelsidig));
        c1->name = "gbC1";
        c1->setChecked(true);
        auto* c2      = gbN->add<glaze::Checkbox>(
            glaze::tr(demo::str::GbSvartvit));
        c2->anchorTop = {"gbC1", 6};
        c2->name      = "gbC2";

        // INAKTIVERAD ram: rubriken dimmas, och barnen släcks av appen —
        // `enabled` kaskadar inte, samma regel som för en Panel.
        auto* gbD       = g->add<glaze::GroupBox>(glaze::tr(demo::str::GbAv));
        gbD->anchorLeft = {"gbN", 12};
        gbD->anchorTop  = {"g10Sep", 10};
        gbD->name       = "gbD";
        gbD->disable();
        auto* r1 = gbD->add<glaze::Radio>(glaze::tr(demo::str::GbStaende));
        r1->name = "gbR1";
        r1->setChecked(true);
        r1->disable();
        auto* r2      = gbD->add<glaze::Radio>(glaze::tr(demo::str::GbLiggande));
        r2->anchorTop = {"gbR1", 6};
        r2->name      = "gbR2";
        r2->disable();

        fot(g, demo::str::GbFot, "gbN", 8);
    }

    // === 11 · SpinBox =========================================
    {
        // 184 HÖG och tätare rader (6 mellan talfälten, som raderna i g10)
        // — måtten är från tiden då gruppen delade kolumn med g10 och g12,
        // och de håller fortfarande.
        auto* g = grupp(sText, demo::str::G11, "g11", 328, 12, 304, 184);

        // Etikett + talfält per rad, talfältet ankrat till höger om en
        // gemensam kolumn — och UTAN mått: bredden ur stilen, höjden ur
        // fältet, så raden står i linje med ett vanligt textfält i varje UI.
        // Etiketten står 4 px lägre än talfältet så texten hamnar mitt för
        // fältets.
        auto rad = [&](std::uint32_t text, const std::string& namn,
                       const std::string& över, float gap) {
            auto* l      = g->add<glaze::Label>(glaze::tr(text));
            l->x         = 8;
            l->anchorTop = {över, gap + 4};
            l->name      = namn + "Lbl";
            auto* s      = g->add<glaze::SpinBox>();
            s->x         = 120;
            s->anchorTop = {över, gap};
            s->name      = namn;
            return s;
        };

        auto* spN = rad(demo::str::SpHeltal, "spN", "g11Sep", 10);
        spN->setValue(42);
        spN->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            talfaltAndrat(s, e);
        };

        // Två decimaler och kvartssteg — decimaltecknet följer SPRÅKET
        // ("3,25" på svenska, "3.25" på engelska), byt i startfönstret.
        auto* spD = rad(demo::str::SpDecimaler, "spD", "spN", 6);
        spD->setDecimals(2);
        spD->setStep(0.25);
        spD->setMin(-10);
        spD->setValue(3.25);
        spD->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            talfaltAndrat(s, e);
        };

        // `disable()` når det inre fältet också — talfältet är EN kontroll.
        auto* spAv = rad(demo::str::SpAv, "spAv", "spD", 6);
        spAv->setValue(7);
        spAv->disable();

        fot(g, demo::str::SpFot, "spAv", 8);
    }

    // === 21 · FilePicker =======================================
    {
        // Fjärde kolumnen på "Text och listor" — ett filväljarfält är ett
        // textfält med en knapp. 262 HÖG: tre etiketter och fyra fält plus
        // foten, mätt i alla tre UI.
        auto* g = grupp(sText, demo::str::G21Fil, "g21", 960, 12, 304, 262);

        // Etiketten ÖVER fältet och fältet sträckt mellan gruppens kanter —
        // UTAN höjd (fältets egen) och utan bredd (dubbelankaret ger den).
        // Etiketterna är ramverkets egna dialogtitlar: samma ord som
        // dialogen själv visar, och redan översatta.
        auto rad = [&](std::uint32_t text, const std::string& namn,
                       const std::string& över) {
            auto* l      = g->add<glaze::Label>(glaze::tr(text));
            l->x         = 8;
            l->anchorTop = {över, 8};
            l->name      = namn + "Lbl";
            auto* f      = g->add<glaze::FilePicker>();
            f->x         = 8;
            f->right     = 8;
            f->anchors   = {true, true, true, false};
            f->anchorTop = {l->name, 2};
            f->name      = namn;
            f->onChange  = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
                filvalAndrat(s, e);
            };
            return f;
        };

        // ÖPPNA med två filter — appens "Bilder" och ramverkets "Alla filer".
        auto* fOpp = rad(glaze::str::FileOpen, "filOpp", "g21Sep");
        // DIALOGENS EGEN TITEL — "Välj en bild" i
        // stället för lägets "Öppna"; knappens tooltip följer med.
        fOpp->setDialogTitle(glaze::tr(demo::str::FilDialogTitel));
        fOpp->addFilter(glaze::tr(demo::str::FilBilder), "*.png;*.jpg");
        fOpp->addFilter(glaze::tr(glaze::str::FileAllFiles), "*.*");

        // SPARA — ett namn utan ändelse får filtrets ".txt".
        auto* fSpa = rad(glaze::str::FileSaveAs, "filSpa", "filOpp");
        fSpa->setMode(glaze::FileDialogMode::Save);
        fSpa->setFilters({{glaze::tr(demo::str::FilText), "*.txt"}});

        // MAPP — listan visar bara mappar och svaret är en mapp.
        auto* fMap = rad(glaze::str::FileSelectFolder, "filMap", "filSpa");
        fMap->setMode(glaze::FileDialogMode::Folder);

        // INAKTIVERAT — fältet OCH knappen släcks (kompositen för det vidare).
        auto* fAv      = g->add<glaze::FilePicker>();
        fAv->x         = 8;
        fAv->right     = 8;
        fAv->anchors   = {true, true, true, false};
        fAv->anchorTop = {"filMap", 8};
        fAv->name      = "filAv";
        fAv->setPath("rapport.pdf");
        fAv->disable();

        fot(g, demo::str::FilFot, "filAv", 8);
    }

    // === 29 · Lösenord, ångra och radbrytning ==============
    {
        // Under filväljarfältet i fjärde kolumnen (g21 slutar på 274) och
        // ned till g3:s underkant, 496 — så sidan inte börjar rulla.
        // 210 HÖG, RÄKNAT i Slates och Glazes radhöjd (14.72 — den högsta;
        // Minimalistics 12.16 ger 20 px luft över): rubrik och skiljelinje
        // 34.72, fältraden 10 + 26.72, delaren 8 + 84 och foten 8 + två
        // rader. Talen är stilens; måttet är räknat och inte provat renderat
        // i alla tre UI och fyra språk (skrivet så, det är provningens rad).
        auto* g = grupp(sText, demo::str::G29Losen, "g29", 960, 286, 304, 210);

        // LÖSENORDET — kryssrutan FÖRST, i högerkanten: fältet sträcks fram
        // till den med ett syskonankare, och ett ankare pekar bara bakåt.
        // Rutan står 4 px under fältets överkant, så den hamnar mitt på
        // fältraden (fältet är 26.72 högt i Slate, rutan 18).
        auto* visa      = g->add<glaze::Checkbox>(glaze::tr(demo::str::VisaLosen));
        visa->anchors   = {false, true, true, false};
        visa->right     = 8;
        visa->anchorTop = {"g29Sep", 14};
        visa->name      = "visaLosen";

        // Masken är STILENS ("•", `Style::passwordMask`) — en per kodpunkt,
        // medan `text()` och careten talar den riktiga texten. Kopiera och
        // klipp ut är avstängda, och fältet har ingen ångra-historik. Texten
        // är exempeldata, inte UI-text.
        auto* losen        = g->add<glaze::TextInput>("p4ssw0rd");
        losen->x           = 8;
        losen->anchors     = {true, true, false, false};
        losen->anchorRight = {"visaLosen", 8};
        losen->anchorTop   = {"g29Sep", 10};
        losen->name        = "losen";
        losen->setPassword(true);
        // PLATSHÅLLAREN OCH MAXLÄNGDEN: töm fältet så syns
        // hinten — i klartext också i lösenordsläge, och med fokus tills man
        // skriver — och gränsen är 16 KODPUNKTER, också för en inklistring
        // (den kapas). Gränsen gäller det man SKRIVER, aldrig appens
        // `setText`.
        losen->setPlaceholder(glaze::tr(demo::str::LosenPlats));
        losen->setMaxLength(16);
        // Värdet läses ur SENDER (regel 6): kryss = visa texten.
        visa->onChange = [losen](glaze::Widget* s, glaze::ChangeArgs&) {
            losen->setPassword(!static_cast<glaze::Checkbox*>(s)->isChecked());
        };

        // RADBRYTNINGEN SOM FÖLJER LAYOUTENS BREDD — provytan
        // är en bredd som ÄNDRAS: ett stående skikt, där den första rutan
        // bär en radbruten och STRÄCKT etikett (bredden ges av rutan, ingen
        // `width`) och en dämpad rad ankrad under den. Dra skiktet: texten
        // bryts om, etiketten blir högre eller lägre, och raden under flyttar
        // med. Första rutan står fast på 210 (etiketten 196 bred), och
        // texterna är skrivna för tre rader där i alla fyra språken; rutan
        // KLIPPER, så en text som inte ryms när den dras smal tar slut vid
        // rutans kant i stället för att rinna ut.
        auto* del      = g->add<glaze::Splitter>();
        del->x         = 8;
        del->right     = 8;
        del->anchors   = {true, true, true, false};
        del->anchorTop = {"losen", 8};
        del->height    = 84;
        del->name      = "delText";
        del->setPaneSize(210);

        auto* ruta = del->add<glaze::Panel>();
        ruta->setBorder(true);
        ruta->setClip(true);
        auto* brod    = ruta->add<glaze::Label>(glaze::tr(demo::str::LosenBrod));
        brod->x       = 6;
        brod->right   = 6;
        brod->y       = 6;
        brod->anchors = {true, true, true, false};   // sträckt: höjden följer
        brod->name    = "brod";
        brod->setWrap(true);
        auto* under      = ruta->add<glaze::Label>(glaze::tr(demo::str::LosenBrodFot));
        under->x         = 6;
        under->right     = 6;
        under->anchors   = {true, true, true, false};
        under->anchorTop = {"brod", 4};
        under->name      = "brodFot";
        under->setWrap(true);
        under->setRole(glaze::Label::Role::Dim);
        del->add<glaze::Panel>()->setBorder(true);   // andra rutan — tom

        // FOTRADEN — också den sträckt och radbruten: den är längre än en rad
        // i alla fyra språken, och höjden följer gruppens bredd.
        auto* fotrad      = g->add<glaze::Label>(glaze::tr(demo::str::LosenFot));
        fotrad->x         = 8;
        fotrad->right     = 8;
        fotrad->anchors   = {true, true, true, false};
        fotrad->anchorTop = {"delText", 8};
        fotrad->name      = "delTextFot";
        fotrad->setWrap(true);
        fotrad->setRole(glaze::Label::Role::Dim);
    }

    // === 24 · TextArea ========================================
    {
        // "Text och listor", under talfältet OCH egenskapslistan — gruppen
        // spänner kolumn två och tre (328 + 304 + 12 + 304 = 948) från under
        // den högre av dem (g7: 12 + 212 + 12 = 236) till g3:s underkant
        // (236 + 260 = 496). Två kolumner fält i stället för en hög stapel:
        // så får VARJE fält sin AUTO-höjd (`textAreaRows` rader ur stilen)
        // och inget spikat mått — en stapel av fyra hade inte rymts i en
        // kolumn i Glaze och Slate. Fälten står under varandra med
        // syskonankare, så Minimalistics lägre rader inte gapar.
        auto* g = grupp(sText, demo::str::G24Ant, "g24", 328, 236, 620, 260);

        auto fält = [&](const std::string& namn, float x,
                        const std::string& över, float gap) {
            auto* t      = g->add<glaze::TextArea>();
            t->x         = x;
            t->width     = 296;
            t->anchorTop = {över, gap};
            t->name      = namn;
            return t;
        };

        // BRYTNING (standard) — en lång rad bryts mot bredden, och texten
        // har fler rader än fältet rymmer, så den lodräta remsan syns och
        // hjulet rullar (också utan fokus). Statusraden räknar raderna.
        auto* ant = fält("ant", 8, "g24Sep", 10);
        ant->setText(trs(demo::str::AntText));
        // TAB GER EN TABB — i jämna tabbstopp; Shift+Tab
        // och Ctrl+Tab lämnar fältet.
        ant->setAcceptsTab(true);
        // Töm anteckningen så syns platshållaren, radbruten mot fältets
        // bredd (samma namn som på det enradiga fältet).
        ant->setPlaceholder(glaze::tr(demo::str::AntPlats));
        ant->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            anteckningAndrad(s, e);
        };

        // UTAN BRYTNING — en rad längre än fältet rullar i sidled (den
        // vågräta remsan).
        auto* bryt = fält("antBryt", 316, "g24Sep", 10);
        bryt->setWrap(false);
        bryt->setText(trs(demo::str::AntBryt));

        // SKRIVSKYDDAD — går att markera och kopiera ur (ytan
        // `fieldReadOnly`), men inte att skriva i.
        auto* skydd = fält("antSkydd", 8, "ant", 8);
        skydd->setReadOnly(true);
        skydd->setText(trs(demo::str::AntSkydd));

        // INAKTIVERAD — texten och kanten dimmas, och hjulet når inte fältet.
        auto* av = fält("antAv", 316, "antBryt", 8);
        av->setText(trs(demo::str::AntAv));
        av->disable();

        fot(g, demo::str::AntFot, "antSkydd", 8);
    }

    // === 36 · Etiketter och knappar =============
    {
        // UNDER fliken Texts tre kolumner (sidan rullar): etikettens granne
        // och kortning, knapparnas textfärg och ikonstorlek, och en listvy
        // med stora miniatyrer och en fyllkolumn.
        auto* g = grupp(sText, demo::str::G36Etiketter, "g36", 12, 508, 936,
                        200);

        // GRANNEN — "Namn" är lika hög som fältet bredvid, mätt ur det, så
        // texten står mitt för fältets i varje UI och stil.
        auto* namn      = g->add<glaze::Label>(glaze::tr(demo::str::ListaNamn));
        namn->x         = 8;
        namn->anchorTop = {"g36Sep", 10};
        namn->name      = "g36NamnLbl";
        namn->setBuddy("g36Namn");
        auto* falt       = g->add<glaze::TextInput>();
        falt->anchorLeft = {"g36NamnLbl", 8};
        falt->anchorTop  = {"g36Sep", 10};
        falt->width      = 220;
        falt->name       = "g36Namn";

        // KORTNINGEN — en rad med "…" vid etikettens bredd, och hela texten
        // som tooltip när den faktiskt kortats.
        auto* kort      = g->add<glaze::Label>(glaze::tr(demo::str::EtikettLang));
        kort->x         = 8;
        kort->anchorTop = {"g36Namn", 10};
        kort->width     = 300;
        kort->name      = "g36Kort";
        kort->setElide(true);

        // KNAPPARNAS TEXTFÄRG — appens, på texten; plattan följer temat.
        auto* taBort      = g->add<glaze::Button>(glaze::tr(demo::str::SnabbTaBort));
        taBort->x         = 8;
        taBort->anchorTop = {"g36Kort", 10};
        taBort->name      = "g36TaBort";
        taBort->setColor(kFel);

        // IKONSTORLEKEN — en 64 px-bild ritad i en 32-ruta.
        auto* bildKnapp       = g->add<glaze::Button>();
        bildKnapp->anchorLeft = {"g36TaBort", 8};
        bildKnapp->anchorTop  = {"g36Kort", 10};
        bildKnapp->name       = "g36Bild";
        bildKnapp->setImg(ringar(64, kBildA, kBildB));
        bildKnapp->setIconSize(32);

        // RADIOKNAPPAR i sina egna färger — ringen och pricken följer temat.
        auto* rod      = g->add<glaze::Radio>(glaze::tr(demo::str::RadioRod));
        rod->x         = 8;
        rod->anchorTop = {"g36Bild", 10};
        rod->name      = "g36Rod";
        rod->setChecked(true);
        rod->setColor(kFel);
        auto* bla       = g->add<glaze::Radio>(glaze::tr(demo::str::RadioBla));
        bla->anchorLeft = {"g36Rod", 16};
        bla->anchorTop  = {"g36Bild", 10};
        bla->name       = "g36Bla";
        bla->setColor(kBildA);

        // LISTVYN TILL HÖGER — 48 px-miniatyrer (raden växer med dem) och
        // namnkolumnen som fyller bredden.
        auto* lista      = g->add<glaze::ListView>();
        lista->x         = 336;
        lista->anchorTop = {"g36Sep", 10};
        lista->anchors   = {true, true, true, false};
        lista->right     = 8;
        lista->height    = 150;
        lista->name      = "g36Lista";
        lista->addColumn(glaze::tr(demo::str::ListaNamn), 120);
        lista->addColumn(glaze::tr(demo::str::ListaStorlek), 70,
                         glaze::Align::Right);
        lista->setIconSize(48);
        lista->addRow({"Ringar.png", "9 kB"}, ringar(48, kBildA, kBildB));
        lista->addRow({"Ringar-omvänd.png", "9 kB"}, ringar(48, kBildB, kBildA));
        lista->setFillColumn(0);
    }

    // === 37 · Markdown ========================
    {
        // UNDER grupp 36 och STRÄCKT med fönstret: den korta vyn till höger
        // har AUTO-höjd, så den växer när fönstret görs smalare — och raden
        // under följer den (syskonankaret).
        auto* g    = grupp(sText, demo::str::G37Markdown, "g37", 12, 720, 936, 340);
        g->anchors = {true, true, true, false};
        g->right   = 12;

        // DOKUMENTET är ETT översättnings-id: rubrikerna, listorna, koden,
        // tabellen och länkarna följer språket, ankarlänkarna med sina
        // rubriker. "ikon" slås upp i vyns bildtabell, "toolbar/gear" som
        // asset.
        auto* md      = g->add<glaze::MarkdownView>(glaze::tr(demo::str::MdExempel));
        md->x         = 8;
        md->anchorTop = {"g37Sep", 10};
        md->width     = 560;
        md->height    = 280;
        md->name      = "g37Md";
        md->setBorder(true);
        md->setImage("ikon", glaze::img("toolbar", "info"));

        // LÄNKARNA: `demo:data` är APPENS — den byter flik och tar länken
        // (`handled`), så förvalet inte körs; webbadressen går till
        // webbläsaren och `#kod` rullar (förvalet). Statusraden visar målet.
        md->onLinkClick = [this, sData](glaze::Widget*, glaze::LinkClickArgs& e) {
            statusTxt_->setText(trs(demo::str::MdLank) + e.url);
            if (e.url == "demo:data") {
                flikar_->setSelectedIndex(flikar_->pageIndex(*sData));
                e.handled = true;
            }
        };

        // DEN KORTA VYN — sträckt mellan dokumentet och gruppens högerkant,
        // med AUTO-höjd (`heightForWidth`).
        auto* kort       = g->add<glaze::MarkdownView>(glaze::tr(demo::str::MdKort));
        kort->anchorLeft = {"g37Md", 12};
        kort->anchorTop  = {"g37Sep", 10};
        kort->anchors    = {true, true, true, false};
        kort->right      = 8;
        kort->name       = "g37Kort";
        kort->setBorder(true);

        auto* under       = g->add<glaze::Label>(glaze::tr(demo::str::MdFot));
        under->anchorLeft = {"g37Md", 12};
        under->anchorTop  = {"g37Kort", 6};
        under->name       = "g37Fot";
        under->setRole(glaze::Label::Role::Dim);
    }

    // === 22 · ColorPicker =====================================
    {
        // Första kolumnen på "Väljare". Etikett + färgväljare per rad,
        // väljaren i en gemensam kolumn och UTAN mått — bredden ur stilen,
        // höjden FÄLTETS, så den står i linje med ett textfält i varje UI.
        // 204 HÖG för tre rader, provtexten och foten, mätt i alla tre UI —
        // och 238 med ALFARADEN: en fältrad och dess lucka
        // till, RÄKNAD (fältet ≈ 27, luckan 6).
        auto* g = grupp(sVal, demo::str::G22Farg, "g22", 12, 12, 304, 238);

        auto rad = [&](std::uint32_t text, const std::string& namn,
                       const std::string& över, float gap) {
            auto* l      = g->add<glaze::Label>(glaze::tr(text));
            l->x         = 8;
            l->anchorTop = {över, gap + 6};
            l->name      = namn + "Lbl";
            auto* f      = g->add<glaze::ColorPicker>();
            f->x         = 120;
            f->anchorTop = {över, gap};
            f->name      = namn;
            return f;
        };

        // TEXTFÄRGEN — provtexten under följer valet LIVE: varje reglageläge
        // och varje tolkbar hextext är redan färgen (färgväljarens modell).
        auto* text = rad(demo::str::FargText, "fargText", "g22Sep", 10);
        text->setColor("#3366FF");
        auto* prov      = g->add<glaze::Label>(glaze::tr(demo::str::FargProv));
        prov->x         = 120;
        prov->anchorTop = {"fargText", 6};
        prov->name      = "fargProv";
        prov->setColor(text->color());
        text->onChange = [this, prov](glaze::Widget* s, glaze::ChangeArgs& e) {
            prov->setColor(static_cast<glaze::ColorPicker*>(s)->color());
            fargAndrad(s, e);
        };

        // STRÄCKT — dubbelankaret ger bredden, färgrutan fyller den. Färgen
        // i KODENS form; `setColor("#4CAF50")` hade gett exakt samma tillstånd.
        auto* bred    = rad(demo::str::FargBred, "fargBred", "fargProv", 8);
        bred->anchors = {true, true, true, false};
        bred->right   = 8;
        bred->setColor(gfx::Color{76, 175, 80});
        bred->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            fargAndrad(s, e);
        };

        // MED ALFA: popupen får ett A-reglage och
        // hexfältet åtta siffror, och rutan står över ett rutmönster som syns
        // genom färgen. Etiketten är RAMVERKETS (reglagets "Alfa").
        auto* alfa = rad(glaze::str::ColorAlpha, "fargAlfa", "fargBred", 6);
        alfa->setAllowAlpha(true);
        alfa->setColor("#3366FF80");
        alfa->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            fargAndrad(s, e);
        };

        // INAKTIVERAD — rutan tonas, och popupen öppnas inte.
        auto* av = rad(demo::str::AltInaktiverad, "fargAv", "fargAlfa", 6);
        av->setColor("#FF9800");
        av->disable();

        fot(g, demo::str::FargFot, "fargAv", 8);
    }

    // === 23 · DatePicker ======================================
    {
        // Andra kolumnen på "Väljare", 22:ans form: etikett + datumväljare
        // per rad, väljaren i en gemensam kolumn och UTAN mått — bredden ur
        // stilen, höjden FÄLTETS. 206 HÖG för tre rader och två fötter, mätt
        // i alla tre UI — och 240 med KLOCKSLAGET: en
        // fältrad och dess lucka till, RÄKNAD.
        auto* g = grupp(sVal, demo::str::G23Datum, "g23", 328, 12, 304, 240);

        auto rad = [&](std::uint32_t text, const std::string& namn,
                       const std::string& över, float gap) {
            auto* l      = g->add<glaze::Label>(glaze::tr(text));
            l->x         = 8;
            l->anchorTop = {över, gap + 6};
            l->name      = namn + "Lbl";
            auto* d      = g->add<glaze::DatePicker>();
            d->x         = 120;
            d->anchorTop = {över, gap};
            d->name      = namn;
            d->onChange  = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
                datumAndrat(s, e);
            };
            return d;
        };

        // DAGENS DATUM är standardvärdet — ingen sättning behövs.
        rad(demo::str::DatumDatum, "datum", "g23Sep", 10);

        // LEVERANS — gränserna är dagens datum och 30 dagar fram: dagarna
        // utanför släcks i kalendern, och en skriven text utanför KLAMPAS.
        // Datumet är `std::chrono::year_month_day`, så räkningen är
        // standardens; `.glazeui`-formen är tre heltal per sättare.
        auto* lev        = rad(demo::str::DatumLeverans, "datumLev", "datum", 8);
        const auto idag  = lev->date();
        lev->setMinDate(idag);
        lev->setMaxDate(std::chrono::year_month_day{
            std::chrono::sys_days{idag} + std::chrono::days{30}});

        // MED KLOCKSLAG: fältet visar
        // `ÅÅÅÅ-MM-DD HH:MM` och är bredare (stilens `dateTimeDefaultW`).
        // Tiden är NU som datumet är i dag; värdet är paret `date()` +
        // `time()`, så appen väljer själv klocka för det.
        auto* tid = rad(demo::str::DatumKlockslag, "datumTid", "datumLev", 6);
        tid->setShowTime(true);

        // INAKTIVERAD — fältet OCH knappen släcks, och kalendern öppnas inte.
        auto* av = rad(demo::str::AltInaktiverad, "datumAv", "datumTid", 6);
        av->setDate(2026, 1, 15);
        av->disable();

        auto* fot1 = fot(g, demo::str::DatumFot, "datumAv", 8);
        fot(g, demo::str::DatumFot2, fot1->name, 2);
    }

    // === 34 · Färgdialog och inmatning ========
    {
        // Tredje kolumnen på "Väljare": de MODALA dialogerna, båda VÅRA och
        // aldrig OS:ets. 144 HÖG: två knappar under varandra (finskans texter
        // ryms inte bredvid varandra) och foten, RÄKNAT i Glazes knapphöjd —
        // den högsta av de tre.
        auto* g = grupp(sVal, demo::str::G34Dialoger, "g34", 644, 12, 304, 144);

        auto* farg      = g->add<glaze::Button>(glaze::tr(demo::str::DialogFarg));
        farg->x         = 8;
        farg->anchorTop = {"g34Sep", 10};
        farg->name      = "dialogFarg";
        farg->onClick   = [this](glaze::Widget* s, glaze::ClickArgs& e) {
            fargDialogKlickad(s, e);
        };

        auto* namn      = g->add<glaze::Button>(glaze::tr(demo::str::DialogNamn));
        namn->x         = 8;
        namn->anchorTop = {"dialogFarg", 6};
        namn->name      = "dialogNamn";
        namn->onClick   = [this](glaze::Widget* s, glaze::ClickArgs& e) {
            namnDialogKlickad(s, e);
        };

        fot(g, demo::str::DialogFot, "dialogNamn", 8);

        // Färgdialogen är en MEDLEM: ett behållet objekt öppnar med förra
        // valet. Lagrets namn är exemplets data — förvalet i inmatningen.
        fargDialog_.setAllowAlpha(true);
        fargDialog_.setColor(gfx::Color{0x33, 0x66, 0xFF, 0x80});
        lagerNamn_ = trs(demo::str::LagerVagar);
    }

    // === 12 · Dra och släpp ===================================
    {
        // 90 HÖG och ingen fot — fotens text visas i stället i statusraden
        // när draget börjar.
        auto* g = grupp(sInter, demo::str::G12, "g12", 12, 12, 304, 90);

        // KÄLLAN är en vanlig knapp med `allowDrag`: trycket är ett KLICK
        // tills pekaren rört sig mer än stilens tröskel. Lasten är ett
        // filnamn — ett egennamn, alltså oöversatt — och dragbilden får
        // knappens ikon.
        auto* kalla      = g->add<glaze::Button>(glaze::tr(demo::str::DraMig));
        kalla->x         = 8;
        kalla->anchorTop = {"g12Sep", 10};
        kalla->name      = "draKalla";
        kalla->setImg(glaze::img("toolbar", "info"));
        kalla->setAllowDrag(true);
        kalla->onDragStart = [this](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("Rapport.pdf");
            e.img = glaze::img("toolbar", "info");
            statusTxt_->setText(glaze::tr(demo::str::DraFot));
        };
        // Utfallet läses ur args — källan VET inte annars om släppet gick
        // fram (WinForms returvärde ur `DoDragDrop`).
        kalla->onDragEnd = [this](glaze::Widget*, glaze::DragEndArgs& e) {
            if (e.effect == glaze::DragEffect::None)
                statusTxt_->setText(glaze::tr(demo::str::IngetSlapp));
        };

        // MÅLET tar emot TEXT som en kopia. Ramen runt det och "+"-märket
        // vid pekaren ritar ramverket — appen säger bara ja.
        auto* zon       = g->add<glaze::Panel>();
        zon->anchorLeft = {"draKalla", 10};
        zon->anchorTop  = {"g12Sep", 8};
        zon->width      = 88;
        zon->height     = 36;
        zon->name       = "draZon";
        zon->setBorder(true);
        zon->setAllowDrop(true);
        auto* zonTxt    = zon->add<glaze::Label>(glaze::tr(demo::str::SlappHar));
        zonTxt->x       = 8;
        zonTxt->anchors = {true, false, false, false};   // v-centrerad
        zonTxt->name    = "draZonTxt";
        zon->onDragEnter = [](glaze::Widget*, glaze::DragArgs& e) {
            if (e.data.has(glaze::DragData::kText))
                e.effect = glaze::DragEffect::Copy;
        };
        zon->onDrop = [this, zonTxt](glaze::Widget*, glaze::DragArgs& e) {
            zonTxt->setText(e.data.text());
            statusTxt_->setText(trs(demo::str::Slappt) + e.data.text());
        };

        // INAKTIVERAT MÅL — `allowDrop`, men av: draget routas förbi det
        // till gruppen, som inte tar emot, så pekaren visar förbudsmärket.
        auto* av       = g->add<glaze::Panel>();
        av->anchorLeft = {"draZon", 8};
        av->anchorTop  = {"g12Sep", 8};
        av->anchors    = {true, true, true, false};
        av->right      = 8;
        av->height     = 36;
        av->name       = "draAv";
        av->setBorder(true);
        av->setAllowDrop(true);
        av->disable();
        auto* avTxt    = av->add<glaze::Label>(
            glaze::tr(demo::str::AltInaktiverad));
        avTxt->x       = 8;
        avTxt->anchors = {true, false, false, false};
        avTxt->name    = "draAvTxt";
        avTxt->disable();
    }

    // === 13 · TreeView =======================================
    {
        // 268 HÖG: rubrik, träden (150) och foten, med foten räknad i
        // Slates radhöjd — den högsta av de tre — och på TRE rader: den är
        // sträckt och radbruten, och finskan behöver tre rader i gruppens
        // bredd. Under gruppen står g32 (listboxens flerval).
        auto* g = grupp(sData, demo::str::G13Trad, "g13", 12, 12, 304, 268);

        // HANDKODEN bygger med `addNode`, som svarar med den nya nodens
        // STIG — kedjan nedan är handkodsvägens bekvämlighet. `.glazeui`
        // skriver samma träd med stiglitteraler (`addNode({0}, …)`) eller
        // som ett enda `setNodes`-aggregat, som det inaktiverade trädet
        // bredvid visar.
        auto* tN      = g->add<glaze::TreeView>();
        tN->x         = 8;
        tN->anchorTop = {"g13Sep", 10};
        tN->width     = 140;
        tN->height    = 150;
        tN->name      = "tradN";
        const glaze::TreePath dok =
            tN->addNode({}, glaze::tr(demo::str::TradDokument));
        // Filnamnen är DATA och översätts inte — bara mapparna är UI-text.
        // En FIL är ett löv för släppet: en nod kan dras före eller
        // efter den, men aldrig in i den.
        tN->setAllowDropInto(
            tN->addNode(dok, "Rapport.pdf", glaze::img("toolbar", "info")),
            false);
        const glaze::TreePath brev = tN->addNode(dok, "Brev.txt");
        tN->setAllowDropInto(brev, false);
        const glaze::TreePath bild =
            tN->addNode({}, glaze::tr(demo::str::TradBilder));
        tN->addNode(bild, glaze::tr(demo::str::TradSemester));
        const glaze::TreePath musik =
            tN->addNode({}, glaze::tr(demo::str::TradMusik));
        // TOLV SPÅR — data och inte UI-text, och löv
        // för släppet. Trädet blir högre än sin yta, så det rullar, och ett
        // drag nära kanten AUTOSKROLLAR (`Widget::dragScroll`).
        for (int i = 1; i <= 12; ++i)
            tN->setAllowDropInto(
                tN->addNode(musik, (i < 10 ? "0" : "") + std::to_string(i) +
                                       ".flac"),
                false);
        tN->setExpanded(musik, true);
        tN->setExpanded(dok, true);
        tN->setSelectedPath({0, 1});
        // OMORDNING är opt-in med basens två drag-egenskaper: noderna blir
        // källor OCH trädet ett mål, och en nod kan dras före, efter eller
        // in i en mapp (filerna ovan tar inte emot).
        tN->setAllowDrag(true);
        tN->setAllowDrop(true);
        // FLERVAL och KRYSS — två OBEROENDE egenskaper: valet är
        // markeringen (Ctrl/Skift-klick, Skift+pilar, Ctrl+A; Cmd på macOS),
        // krysset rutan framför raden (ett klick i rutan eller Space). Ett
        // drag av en vald nod drar HELA valet. Brev.txt bär en STATUSFÄRG
        // — appens, inte temats; på en vald rad vinner markeringen.
        tN->setMultiSelect(true);
        tN->setShowChecks(true);
        tN->setNodeColor(brev, kAndrad);
        tN->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            tradAndrat(s, e);
        };
        tN->onCheck = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            tradKryssad(s, e);
        };
        tN->onExpand = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            tradFalld(s, e);
        };
        tN->onCollapse = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            tradFalld(s, e);
        };
        tN->onActivate = [this](glaze::Widget* s, glaze::ActivateArgs& e) {
            tradAktiverad(s, e);
        };
        // APPENS GODKÄNNANDE AV DRAGET: trädet föreslår en
        // flytt på varje plats där det går, och appen avgör. Regeln per plats
        // står i `onDragOver` — den fyras också vid inträdet, så ett släpp
        // utan en rörelse till håller den — och `onDrop` frågas FÖRE trädets
        // flytt: i Musik sorterar appen in noderna själv.
        tN->onDragOver = [this](glaze::Widget* s, glaze::DragArgs& e) {
            tradOver(s, e);
        };
        tN->onDrop = [this](glaze::Widget* s, glaze::DragArgs& e) {
            tradSlapp(s, e);
        };
        // SNABBMENYN: högertrycket väljer noden som
        // Utforskaren — en nod i valet lämnar valet — och menyn tar bort det
        // valda.
        tradN_ = tN;
        snabbmeny_.addItem(glaze::tr(demo::str::SnabbTaBort)).onClick =
            [this] { tradTaBort(); };
        tN->onContextMenu = [this](glaze::Widget* s, glaze::ContextMenuArgs& e) {
            tradMeny(s, e);
        };
        tN->setToolTip(glaze::tr(demo::str::TradTips));

        // INAKTIVERAT, byggt med MASSFORMEN — aggregatet `glaze::TreeNode`
        // (text, barn, bild, utfälld, släpp in, kryss, färg) i en enda sats:
        // POSITIONELLT för mapparna och DESIGNERAT för filerna, och
        // `.glazeui` tar båda formerna.
        auto* tD       = g->add<glaze::TreeView>();
        tD->anchorLeft = {"tradN", 8};
        tD->anchorTop  = {"g13Sep", 10};
        tD->width      = 140;
        tD->height     = 150;
        tD->name       = "tradD";
        tD->setNodes({{glaze::tr(demo::str::TradDokument),
                       {{.text    = "Rapport.pdf",
                         .img     = glaze::img("toolbar", "info"),
                         .checked = true},
                        {.text = "Brev.txt", .color = kAndrad}},
                       {},
                       true},
                      {glaze::tr(demo::str::TradBilder)},
                      {glaze::tr(demo::str::TradMusik)}});
        tD->setShowChecks(true);
        tD->setSelectedPath({0, 1});
        tD->disable();

        // Sträckt och radbruten — längre än en rad i alla fyra språken.
        auto* tFot    = fot(g, demo::str::TradFot, "tradN", 8);
        tFot->anchors = {true, true, true, false};
        tFot->right   = 8;
        tFot->setWrap(true);
    }

    // === 32 · ListBox med flerval =============
    {
        // Under g13 i första kolumnen, bredvid trädets och listvyns flerval.
        // 186 HÖG: rubrik, listan (fyra rader à `listRowH` 26 och ramen) och
        // foten, RÄKNAT i Slates radhöjd — gruppen slutar vid g3:s
        // underkant, så fönstrets mått gäller oförändrade.
        auto* g = grupp(sData, demo::str::G32Flerval, "g32", 12, 292, 304, 186);

        // LISTVYNS ORD: klick väljer bara raden, Ctrl/Cmd vänder, Skift ger
        // spannet från ankaret, Ctrl+A väljer alla — och ett tryck på en vald
        // rad väljer den ensam först vid släppet. Lagren är kartans.
        auto* lst      = g->add<glaze::ListBox>();
        lst->x         = 8;
        lst->anchorTop = {"g32Sep", 10};
        lst->height    = 106;
        lst->anchors   = {true, true, true, false};
        lst->right     = 8;
        lst->name      = "flerval";
        lst->setItems({trs(demo::str::LagerPlattor), trs(demo::str::LagerYtor),
                       trs(demo::str::LagerVagar),
                       trs(demo::str::LagerEtiketter)});
        lst->setMultiSelect(true);
        lst->setSelected(1, true);
        lst->setSelected(2, true);
        lst->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            valdaRaderAndrade(s, e);
        };

        fot(g, demo::str::FlervalFot, "flerval", 8);
    }

    // === 16 · Tooltip =========================================
    {
        // 160 HÖG: en knapprad, rutan (40) och foten, räknat i Slates
        // radhöjd — den högsta av de tre.
        auto* g = grupp(sInter, demo::str::G16Tip, "g16", 328, 12, 304, 160);

        // Tooltipen är en egenskap på BASEN — varje widget har den, och
        // texten är ett token som allt annat: byt språk i startfönstret och
        // nästa tooltip talar det nya. Vila pekaren; ett klick eller en
        // tangent tystar den tills pekaren lämnat widgeten.
        auto* spara      = g->add<glaze::Button>(glaze::tr(demo::str::TipSpara));
        spara->x         = 8;
        spara->anchorTop = {"g16Sep", 10};
        spara->name      = "tipSpara";
        spara->setImg(glaze::img("toolbar", "ok"));
        spara->setToolTip(glaze::tr(demo::str::TipSparaTips));

        // INAKTIVERAD — och tooltipen visas ändå: det är just där den
        // behövs, för att säga VARFÖR knappen inte går att trycka.
        auto* av       = g->add<glaze::Button>(glaze::tr(demo::str::ButtonAv));
        av->anchorLeft = {"tipSpara", 8};
        av->anchorTop  = {"g16Sep", 10};
        av->name       = "tipAv";
        av->setToolTip(glaze::tr(demo::str::TipAvTips));
        av->disable();

        // En knapp med BARA en ikon — tooltipens främsta konsument.
        auto* kugg       = g->add<glaze::ToolButton>();
        kugg->anchorLeft = {"tipAv", 8};
        kugg->anchorTop  = {"g16Sep", 10};
        kugg->name       = "tipKugg";
        kugg->setImg(glaze::img("toolbar", "gear"));
        kugg->setToolTip(glaze::tr(demo::str::TipKuggTips));

        // UPPSLAGET går uppåt till NÄRMASTE förfader med en tooltip.
        // Etiketten saknar en egen, så rutans visas över den; talfältet är
        // en KOMPOSIT, och över dess inre fält och pilar vinner talfältets
        // egen — den sitter närmare än rutans. 40 HÖG rymmer talfältet i
        // alla tre familjerna; barnen står v-centrerade (inget ankare i
        // höjdled).
        auto* ruta      = g->add<glaze::Panel>();
        ruta->x         = 8;
        ruta->anchorTop = {"tipSpara", 8};
        ruta->anchors   = {true, true, true, false};
        ruta->right     = 8;
        ruta->height    = 40;
        ruta->name      = "tipRuta";
        ruta->setBorder(true);
        ruta->setToolTip(glaze::tr(demo::str::TipPanelTips));
        auto* barn    = ruta->add<glaze::Label>(glaze::tr(demo::str::TipBarn));
        barn->x       = 8;
        barn->anchors = {true, false, false, false};
        barn->name    = "tipBarn";
        auto* spin       = ruta->add<glaze::SpinBox>();
        spin->anchorLeft = {"tipBarn", 8};
        spin->anchors    = {false, false, false, false};
        spin->name       = "tipSpin";
        spin->setMin(1);
        spin->setValue(1);
        spin->setToolTip(glaze::tr(demo::str::TipSpinTips));

        fot(g, demo::str::TipFot, "tipRuta", 8);
    }

    // === 30 · Inmatning ================================
    {
        // 360 HÖG: rubriken, två radbrutna tips (tre rader i finskan), fältet
        // (70) och ytan, som är SPÄND mellan sitt tips och gruppens underkant
        // och tar det som blir över — så ett längre tips aldrig klipper den.
        auto* g = grupp(sInter, demo::str::G30Inmatning, "g30", 644, 12, 304,
                        360);

        // TANGENTERNA PÅ VARJE WIDGET: fältets `onKeyDown` får tangenten FÖRE
        // fältet. Ctrl+Enter skickar och blir ingen radbrytning; vanlig Enter
        // når fältet och ger en ny rad.
        auto* tips      = g->add<glaze::Label>(glaze::tr(demo::str::InSkickaTips));
        tips->x         = 8;
        tips->anchorTop = {"g30Sep", 10};
        tips->anchors   = {true, true, true, false};
        tips->right     = 8;
        tips->name      = "inTips";
        tips->setWrap(true);

        inMeddelande_            = g->add<glaze::TextArea>();
        inMeddelande_->x         = 8;
        inMeddelande_->anchorTop = {"inTips", 6};
        inMeddelande_->anchors   = {true, true, true, false};
        inMeddelande_->right     = 8;
        inMeddelande_->height    = 70;
        inMeddelande_->name      = "inMeddelande";
        inMeddelande_->onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& e) {
            inTangent(s, e);
        };

        // HÖGERKNAPPEN, DUBBELKLICKEN, AVBROTTET OCH TANGENTSLÄPPET på en
        // rityta UTAN gester: allt är appens. Ett högerdrag som handlern tar
        // drar en linjal (och ger ingen snabbmeny), ett mitten- eller
        // högerdubbelklick skrivs i statusraden, Alt+Tab mitt i draget är
        // "Trycket avbröts", och mellanslaget visar trycket och släppet — också
        // släppet som aldrig kom (fokusförlusten).
        auto* ytTips      = g->add<glaze::Label>(glaze::tr(demo::str::InYtaTips));
        ytTips->x         = 8;
        ytTips->anchorTop = {"inMeddelande", 10};
        ytTips->anchors   = {true, true, true, false};
        ytTips->right     = 8;
        ytTips->name      = "inYtaTips";
        ytTips->setWrap(true);

        inYta_            = g->add<glaze::Canvas>();
        inYta_->x         = 8;
        inYta_->anchorTop = {"inYtaTips", 6};
        inYta_->anchors   = {true, true, true, true};
        inYta_->right     = 8;
        inYta_->bottom    = 8;
        inYta_->name      = "inYta";
        inYta_->onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            inYtaNed(s, e);
        };
        inYta_->onMouseMove = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            inYtaRor(s, e);
        };
        inYta_->onMouseUp = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            inYtaSlapp(s, e);
        };
        inYta_->onDoubleClick = [this](glaze::Widget* s,
                                       glaze::CanvasMouseArgs& e) {
            inYtaDubbel(s, e);
        };
        inYta_->onMouseCancel = [this](glaze::Widget* s,
                                       glaze::CanvasMouseArgs& e) {
            inYtaAvbruten(s, e);
        };
        inYta_->onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& e) {
            inYtaTangent(s, e);
        };
        inYta_->onKeyUp = [this](glaze::Widget* s, glaze::KeyArgs& e) {
            inYtaTangentUpp(s, e);
        };
        inYta_->onPaintOverlay = [this](glaze::Widget* s, glaze::PaintArgs& e) {
            inYtaOverlay(s, e);
        };
    }

    // === 35 · Storlek och tangenter ===============
    {
        // 360 HÖG, under grupp 12: rubriken, ett radbrutet tips, fältet och
        // delaren, som är SPÄND mellan fältet och gruppens underkant.
        auto* g = grupp(sInter, demo::str::G35Storlek, "g35", 12, 114, 304,
                        360);

        auto* tips      = g->add<glaze::Label>(glaze::tr(demo::str::G35Tips));
        tips->x         = 8;
        tips->anchorTop = {"g35Sep", 10};
        tips->anchors   = {true, true, true, false};
        tips->right     = 8;
        tips->name      = "g35Tips";
        tips->setWrap(true);

        // TANGENTENS TEXT: fältets `onKeyDown` skriver tangentens namn och
        // texten trycket skriver — "å", "@" för AltGr+2, ´ sedan e ger
        // "é" på e, och Ctrl+S ingen text.
        auto* falt      = g->add<glaze::TextInput>();
        falt->x         = 8;
        falt->anchorTop = {"g35Tips", 8};
        falt->anchors   = {true, true, true, false};
        falt->right     = 8;
        falt->name      = "stFalt";
        falt->onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& e) {
            stTangent(s, e);
        };

        // STORLEKEN: ritytan i delarens vänstra ruta — dra i delaren, och
        // `onResize` skriver ytan i statusraden per rörelse (delaren drar i
        // vår pump). Högertrycket TAS i `onMouseDown`, och släppet öppnar
        // snabbmenyn exakt vid pekaren genom `toWindow`.
        auto* del      = g->add<glaze::Splitter>();
        del->x         = 8;
        del->right     = 8;
        del->bottom    = 8;
        del->anchors   = {true, true, true, true};
        del->anchorTop = {"stFalt", 8};
        del->name      = "stDelare";
        del->setPaneSize(170);

        stYta_       = del->add<glaze::Canvas>();
        stYta_->name = "stYta";
        stYta_->onResize = [this](glaze::Widget* s, glaze::ResizeArgs& e) {
            stYtaStorlek(s, e);
        };
        stYta_->onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            stYtaNed(s, e);
        };
        stYta_->onMouseUp = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            stYtaSlapp(s, e);
        };
        auto* ruta = del->add<glaze::Panel>();
        ruta->name = "stRuta";
        ruta->setBorder(true);

        stMeny_.addItem(glaze::tr(demo::str::MarkeraHar)).onClick = [this] {
            statusTxt_->setText(trs(demo::str::MarkeraHar) + " · " +
                                std::to_string(std::lround(stMarkerat_.x)) +
                                ", " +
                                std::to_string(std::lround(stMarkerat_.y)));
        };
    }

    // === 14 · ListView ========================================
    {
        // 318 HÖG: rubrik, listan (150), den inaktiverade (80) och foten,
        // med foten räknad i Slates radhöjd — den högsta av de tre.
        auto* g = grupp(sData, demo::str::G14Lista, "g14", 328, 12, 304, 318);

        // FLERVAL, sorterad på namn, med ikoner — filväljarens lista i
        // miniatyr. Storleken VISAS som text men SORTERAS på bytes:
        // sorteringsnyckeln (`setSortKey`) är vägen för allt som visas
        // annorlunda än det ska ordnas. Klicka i huvudet för att sortera om
        // — valet följer raderna, inte platserna.
        auto* lv      = g->add<glaze::ListView>();
        lv->x         = 8;
        lv->anchorTop = {"g14Sep", 10};
        lv->anchors   = {true, true, true, false};
        lv->right     = 8;
        lv->height    = 150;
        lv->name      = "listaN";
        lv->addColumn(glaze::tr(demo::str::ListaNamn), 128);
        lv->addColumn(glaze::tr(demo::str::ListaStorlek), 60,
                      glaze::Align::Right);
        lv->addColumn(glaze::tr(demo::str::ListaAndrad), 88);
        // Filnamnen, storlekarna och datumen är DATA och översätts inte —
        // bara rubrikerna är UI-text (samma gräns som trädets filer).
        struct Fil {
            const char* namn;
            const char* storlek;
            double      bytes;
            const char* datum;
            const char* ikon;   // nullptr = ingen
        };
        const Fil filer[] = {
            {"Rapport.pdf", "12 kB", 12288, "2026-10-07", "info"},
            {"Semester.png", "3 MB", 3145728, "2026-09-30", "ok"},
            {"Budget.xlsx", "48 kB", 49152, "2026-08-21", "gear"},
            {"Anteckningar.txt", "900 B", 900, "2026-08-19", nullptr},
            {"fil10.txt", "1 kB", 1024, "2026-01-02", nullptr},
            {"fil2.txt", "2 kB", 2048, "2026-01-01", nullptr},
        };
        for (const Fil& f : filer) {
            lv->addRow({f.namn, f.storlek, f.datum},
                       f.ikon ? glaze::img("toolbar", f.ikon)
                              : glaze::ImageSource{});
            lv->setSortKey(lv->rowCount() - 1, 1, f.bytes);
        }
        // En STATUSFÄRG på en rad — Budget.xlsx har ett fel. Indexet
        // är appens, så färgen följer raden genom sorteringen; på en vald rad
        // vinner markeringens textfärg.
        lv->setRowColor(2, kFel);
        lv->setMultiSelect(true);
        lv->setSortColumn(0);   // naturligt: fil2 före fil10
        lv->setSelectedIndex(0);
        // KRYSS — rutan först i första kolumnen, OBEROENDE
        // av valet: ett klick i rutan eller Space vänder krysset utan att
        // flytta markeringen, och krysset följer RADEN (appens index) genom
        // sorteringen. Semester.png är rad 1.
        lv->setShowChecks(true);
        lv->setChecked(1, true);
        lv->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            listaAndrad(s, e);
        };
        lv->onActivate = [this](glaze::Widget* s, glaze::ActivateArgs& e) {
            listaAktiverad(s, e);
        };
        lv->onCheck = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            listaKryssad(s, e);
        };
        // FYLLKOLUMNEN: namnet tar resten av bredden,
        // och dess satta bredd är minimum. Högertrycket väljer raden och
        // skriver den i statusraden.
        lv->setFillColumn(0);
        lv->onContextMenu = [this](glaze::Widget* s, glaze::ContextMenuArgs& e) {
            listaMeny(s, e);
        };
        lv->setToolTip(glaze::tr(demo::str::ListaTips));

        // INAKTIVERAD, byggd med MASSFORMEN — `setColumns`/`setRows` med
        // aggregaten `glaze::Table::Column` och `glaze::Table::Row` (celler,
        // bild, färg — den andra raden bär en statusfärg, som inaktiverad
        // dimmas till temats), och sorterad FALLANDE på storleken (texten,
        // naturligt: utan nycklar).
        auto* lD      = g->add<glaze::ListView>();
        lD->x         = 8;
        lD->anchorTop = {"listaN", 8};
        lD->anchors   = {true, true, true, false};
        lD->right     = 8;
        lD->height    = 80;
        lD->name      = "listaD";
        lD->setColumns({{glaze::tr(demo::str::ListaNamn), 128},
                        {glaze::tr(demo::str::ListaStorlek), 60,
                         glaze::Align::Right},
                        {glaze::tr(demo::str::ListaAndrad), 88}});
        lD->setRows({{{"Rapport.pdf", "12 kB", "2026-10-07"},
                      glaze::img("toolbar", "info")},
                     {{"Anteckningar.txt", "900 B", "2026-08-19"}, {}, kKlar}});
        lD->setSortColumn(1);
        lD->setSortAscending(false);
        lD->setSelectedIndex(0);
        lD->setShowChecks(true);
        lD->setChecked(0, true);   // inaktiv: rutan och bocken tonas
        lD->disable();

        fot(g, demo::str::ListaFot, "listaD", 8);
    }

    // === 18 · DataGrid ========================================
    {
        // 298 HÖG: g14:s räkning med en kortare tabell — rubrik, datagriden
        // (130), den inaktiverade (80) och foten.
        auto* g = grupp(sData, demo::str::G18Data, "g18", 644, 12, 304, 298);

        // REDIGERBAR: värd och port i fritext, protokollet ur en VALLISTA och
        // id:t skrivskyddat. Kolumntypen hör till INDEXET, så den kan sättas
        // före eller efter kolumnerna. Värdarna och protokollen är DATA och
        // översätts inte — bara rubrikerna är UI-text ("Id" heter samma sak
        // på alla fyra språken). Fem rader i en yta för fyra: den rullar.
        auto* dg      = g->add<glaze::DataGrid>();
        dg->x         = 8;
        dg->anchorTop = {"g18Sep", 10};
        dg->anchors   = {true, true, true, false};
        dg->right     = 8;
        dg->height    = 130;
        dg->name      = "dataN";
        dg->addColumn(glaze::tr(demo::str::DataVard), 96);
        dg->addColumn(glaze::tr(demo::str::DataPort), 52, glaze::Align::Right);
        dg->addColumn(glaze::tr(demo::str::DataProtokoll), 84);
        dg->addColumn("Id", 48, glaze::Align::Right);
        dg->setColumnEditor(2, glaze::DataGrid::Editor::Choice);
        dg->setColumnChoices(2, {"HTTP", "HTTPS", "FTP", "SSH"});
        dg->setColumnEditor(3, glaze::DataGrid::Editor::None);
        dg->addRow({"alpha", "8080", "HTTP", "1"});
        dg->addRow({"beta", "443", "HTTPS", "2"});
        dg->addRow({"gamma", "21", "FTP", "3"});
        dg->addRow({"delta", "22", "SSH", "4"});
        dg->addRow({"epsilon", "8443", "HTTPS", "5"});
        // En STATUSFÄRG på en rad: gamma talar okrypterad FTP. Den
        // AKTUELLA cellen behåller markeringens färg, resten av raden bär
        // appens.
        dg->setRowColor(2, kFel);
        dg->setCurrentCell(0, 0);
        dg->onChange = [this](glaze::Widget* s, glaze::CellChangeArgs& e) {
            dataAndrad(s, e);
        };

        // INAKTIVERAD, byggd med MASSFORMEN — samma aggregat som listvyns.
        auto* dD      = g->add<glaze::DataGrid>();
        dD->x         = 8;
        dD->anchorTop = {"dataN", 8};
        dD->anchors   = {true, true, true, false};
        dD->right     = 8;
        dD->height    = 80;
        dD->name      = "dataD";
        dD->setColumns({{glaze::tr(demo::str::DataVard), 96},
                        {glaze::tr(demo::str::DataPort), 52,
                         glaze::Align::Right},
                        {glaze::tr(demo::str::DataProtokoll), 84},
                        {"Id", 48, glaze::Align::Right}});
        dD->setRows({{{"alpha", "8080", "HTTP", "1"}},
                     {{"beta", "443", "HTTPS", "2"}}});
        dD->setColumnEditor(3, glaze::DataGrid::Editor::None);
        dD->setCurrentCell(1, 2);
        dD->disable();

        fot(g, demo::str::DataFot, "dataD", 8);
    }

    // === 33 · DataGrid med radval =============
    {
        // Under g18 i tredje kolumnen. 172 HÖG: rubrik, datagriden (92 —
        // huvudet och drygt två rader, så de fyra rullar) och foten, RÄKNAT
        // i Slates radhöjd — gruppen slutar vid g3:s underkant.
        auto* g = grupp(sData, demo::str::G33Radval, "g33", 644, 322, 304, 172);

        // HELA RADER väljs (WinForms `FullRowSelect`): klicket gör cellen
        // aktuell OCH väljer raden, Ctrl vänder, Skift ger spannet och Ctrl+A
        // väljer alla — och cellen redigeras som vanligt. Valet har sitt EGET
        // event, `onSelectionChange`: datagridens `onChange` är cellernas.
        auto* dg      = g->add<glaze::DataGrid>();
        dg->x         = 8;
        dg->anchorTop = {"g33Sep", 10};
        dg->anchors   = {true, true, true, false};
        dg->right     = 8;
        dg->height    = 92;
        dg->name      = "radval";
        dg->setColumns({{glaze::tr(demo::str::DataVard), 96},
                        {glaze::tr(demo::str::DataPort), 52,
                         glaze::Align::Right},
                        {glaze::tr(demo::str::DataProtokoll), 84},
                        {"Id", 48, glaze::Align::Right}});
        dg->setRows({{{"alpha", "8080", "HTTP", "1"}},
                     {{"beta", "443", "HTTPS", "2"}},
                     {{"gamma", "21", "FTP", "3"}},
                     {{"delta", "22", "SSH", "4"}}});
        dg->setSelectionMode(glaze::DataGrid::SelectionMode::FullRow);
        dg->setMultiSelect(true);
        dg->setSelected(0, true);
        dg->setSelected(2, true);
        dg->setCurrentCell(2, 0);
        dg->onSelectionChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            valdaRaderAndrade(s, e);
        };

        fot(g, demo::str::RadvalFot, "radval", 8);
    }

    // === 26 · Kolumnrubriker ==================================
    {
        // 418 HÖG: rubrik, linberäkningen (204 — fem rubrikrader och fyra
        // rader i Slate, den högsta familjen: Segoe UI:s radhöjd är 17,3),
        // stolparna (126) och foten. Kolumnerna fyller tabellens inre
        // bredd, 284 (gruppens ram och tabellens tar en px var på varje
        // sida), och är MÄTTA: det längsta ordet i varje rubrik, på alla fyra
        // språken, ryms i Slate ("Lengde" 42 · "pystysuora" 62 ·
        // "Kuormitus-" 64 logiska px, plus cellens luft 2 × 8).
        auto* g = grupp(sData, demo::str::G26Rubriker, "g26", 960, 12, 304, 418);

        // LINBERÄKNINGEN: rubrikerna på flera
        // rader — `\n` i texten — CENTRERADE och BOTTENJUSTERADE, talen
        // högerjusterade och lastfallet centrerat. AUTO-höjden
        // (`setHeaderLines(0)`) ger huvudet lika många rader som den längsta
        // rubriken; klicka i huvudet för att sortera, så står pilen vid
        // rubrikernas sista rad. Spannen, längderna och nedhängen är DATA.
        auto* lv      = g->add<glaze::ListView>();
        lv->x         = 8;
        lv->anchorTop = {"g26Sep", 10};
        lv->anchors   = {true, true, true, false};
        lv->right     = 8;
        lv->height    = 204;
        lv->name      = "rubrikN";
        lv->setHeaderLines(0);
        lv->setHeaderVAlign(glaze::VAlign::Bottom);
        lv->setColumns(
            {{glaze::tr(demo::str::RubSpann), 54, glaze::Align::Left,
              glaze::Align::Center},
             {glaze::tr(demo::str::RubLangd), 60, glaze::Align::Right,
              glaze::Align::Center},
             {glaze::tr(demo::str::RubNedhang), 80, glaze::Align::Right,
              glaze::Align::Center},
             {glaze::tr(demo::str::RubLastfall), 90, glaze::Align::Center,
              glaze::Align::Center}});
        lv->setRows({{{"12–13", "82,4", "1,92", "1a"}},
                     {{"13–14", "64,0", "1,18", "2b"}},
                     {{"14–15", "105,8", "3,07", "1a"}},
                     {{"15–16", "71,2", "1,41", "3"}}});
        lv->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            listaAndrad(s, e);
        };

        // STOLPARNA — en redigerbar datagrid med TVÅ rubrikrader i
        // överkant. Rubrikerna saknar egen justering och följer sina celler:
        // vänster, centrerat och höger. Den långa rubriken bryts mot
        // kolumnen och kortas med "…" på den andra raden. Antalet är en
        // VALLISTA: redigera en cell, så står editorn i kolumnens justering.
        auto* dg      = g->add<glaze::DataGrid>();
        dg->x         = 8;
        dg->anchorTop = {"rubrikN", 8};
        dg->anchors   = {true, true, true, false};
        dg->right     = 8;
        dg->height    = 126;
        dg->name      = "rubrikD";
        dg->setHeaderLines(2);
        dg->setHeaderVAlign(glaze::VAlign::Top);
        dg->addColumn(glaze::tr(demo::str::RubBeskrivning), 128);
        dg->addColumn(glaze::tr(demo::str::RubAntal), 66, glaze::Align::Center);
        dg->addColumn(glaze::tr(demo::str::RubHojd), 90, glaze::Align::Right);
        dg->setColumnEditor(1, glaze::DataGrid::Editor::Choice);
        dg->setColumnChoices(1, {"1", "2", "3", "4"});
        dg->addRow({"Hörnstolpe", "2", "12,0"});
        dg->addRow({"Raklinjestolpe", "4", "10,5"});
        dg->addRow({"Ändstolpe", "1", "11,2"});
        dg->onChange = [this](glaze::Widget* s, glaze::CellChangeArgs& e) {
            dataAndrad(s, e);
        };

        fot(g, demo::str::RubFot, "rubrikD", 8);
    }

    // === 15 · Splitter ========================================
    {
        // 268 HÖG: rubrik, två rader delare (90 + 90) och foten, med foten
        // räknad i Slates radhöjd — den högsta av de tre.
        auto* g = grupp(sLayout, demo::str::G15Delare, "g15", 12, 12, 304, 268);

        // En RUTA är en Panel med ram — delaren har ingen egen yta, rutorna
        // ÄR ytan, och den placerar dem själv (deras x/y/dock ignoreras).
        // Etiketten i rutan står i rutans hörn; talen är en offset inom
        // rutan och beror inte på stilen.
        auto ruta = [](glaze::Splitter* s, std::uint32_t text) {
            auto* p = s->add<glaze::Panel>();
            p->setBorder(true);
            if (text) {
                auto* l = p->add<glaze::Label>(glaze::tr(text));
                l->x    = 6;
                l->y    = 6;
            }
        };

        // STÅENDE skikt, första rutan FAST på 110 px: dra skiktet, och
        // släpp — läget skrivs först då (`onChange` en gång per drag).
        // Gör fönstret bredare: den fasta rutan står still.
        auto* sN      = g->add<glaze::Splitter>();
        sN->x         = 8;
        sN->anchorTop = {"g15Sep", 10};
        sN->anchors   = {true, true, true, false};
        sN->right     = 8;
        sN->height    = 90;
        sN->name      = "delN";
        sN->setPaneSize(110);
        sN->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            delareFlyttad(s, e);
        };
        ruta(sN, demo::str::DelareForsta);
        ruta(sN, demo::str::DelareAndra);

        // LIGGANDE skikt, ANDRA rutan fast och aldrig under 20 px.
        auto* sH      = g->add<glaze::Splitter>();
        sH->x         = 8;
        sH->anchorTop = {"delN", 8};
        sH->width     = 140;
        sH->height    = 90;
        sH->name      = "delH";
        sH->setVertical(false);
        sH->setFixedPane(glaze::Splitter::Pane::Second);
        sH->setPaneSize(30);
        sH->setSecondMin(20);
        sH->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            delareFlyttad(s, e);
        };
        ruta(sH, 0);
        ruta(sH, 0);

        // INAKTIVERAD: skiktet tänds inte och går inte att dra, pekaren
        // förblir en pil — men delaren ser likadan ut (en yta har inget
        // läge). Rutorna släcks av appen om den vill; `enabled` kaskadar
        // inte.
        auto* sD       = g->add<glaze::Splitter>();
        sD->anchorLeft = {"delH", 8};
        sD->anchorTop  = {"delN", 8};
        sD->anchors    = {true, true, true, false};
        sD->right      = 8;
        sD->height     = 90;
        sD->name       = "delD";
        sD->disable();
        ruta(sD, 0);
        ruta(sD, 0);

        fot(g, demo::str::DelareFot, "delH", 8);
    }

    // === 17 · DockHost ========================================
    {
        // 350 HÖG: rubrik, värden (230), knappraden och foten, räknat i
        // Slates radhöjd — den högsta av de tre.
        auto* g = grupp(sLayout, demo::str::G17Dock, "g17", 328, 12, 304, 350);

        // VÄRDEN: en verktygspanel, en utdatapanel och ett dokument i
        // mitten. Dra en panels rubrik mot en kant eller en annan panel —
        // indikatorn visar exakt var den hamnar (mitt på en panel blir den en
        // flik, vid dess kant en stapel, vid värdens kant ytterst). Skikten
        // dras som delarens. Värdens `onChange` fyras efter varje ändring
        // användaren gör — platsen att spara layouten. Flikar, staplar och
        // handtag i en större värd: `demo --dock` (foten pekar dit).
        auto* h      = g->add<glaze::DockHost>();
        h->x         = 8;
        h->anchorTop = {"g17Sep", 10};
        h->anchors   = {true, true, true, false};
        h->right     = 8;
        h->height    = 230;
        h->name      = "dockVard";
        h->onChange  = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            dockAndrad(s, e);
        };

        // Panelernas `name` är nyckeln i layoutsträngen. Verktygspanelen
        // bär ett textfält: utfälld är FÄLTET ett tabbstopp. Knappnålen i
        // rubriken gör panelen till ett HANDTAG på kanten, och då är VÄRDEN
        // tabbstoppet — Enter eller ett klick fäller ut den som en OVERLAY
        // ovanpå dokumentet, och knappnålen i overlayn fäster den igen.
        auto* v  = h->add<glaze::DockPanel>(glaze::tr(demo::str::DockVerktyg));
        v->name  = "dockV";
        v->width = 110;
        auto* f  = v->add<glaze::TextInput>();
        f->dock  = glaze::Dock::Top;

        auto* u   = h->add<glaze::DockPanel>(glaze::tr(demo::str::DockUtdata));
        u->name   = "dockU";
        u->dock   = glaze::Dock::Bottom;
        u->height = 70;

        // Allt som inte är en dockad panel får mitten.
        auto* dok = h->add<glaze::Panel>();
        dok->setBorder(true);
        auto* dl = dok->add<glaze::Label>(glaze::tr(demo::str::TradDokument));
        dl->x    = 6;
        dl->y    = 6;

        // KNAPPRADEN: en STÄNGD panel tas fram med `show()`, och layouten
        // sparas som sträng och läses tillbaka (`layout()`/`setLayout()`,
        // wxAUI:s Save/LoadPerspective). En riktig app lagrar strängen i
        // sina inställningar; demon håller den i en medlem.
        auto* visa      = g->add<glaze::Button>(glaze::tr(demo::str::DockVisa));
        visa->x         = 8;
        visa->anchorTop = {"dockVard", 8};
        visa->name      = "dockVisa";
        visa->onClick   = [v, u] {
            v->show();
            u->show();
        };
        auto* spara =
            g->add<glaze::Button>(glaze::tr(demo::str::TipSpara));
        spara->anchorLeft = {"dockVisa", 8};
        spara->anchorTop  = {"dockVard", 8};
        spara->name       = "dockSpara";
        spara->onClick    = [this, h] {
            dockLayout_ = h->layout();
            statusTxt_->setText(trs(demo::str::DockSparad) + dockLayout_);
        };
        auto* ater =
            g->add<glaze::Button>(glaze::tr(demo::str::DockAterstall));
        ater->anchorLeft = {"dockSpara", 8};
        ater->anchorTop  = {"dockVard", 8};
        ater->name       = "dockAter";
        ater->onClick    = [this, h] {
            // Den programmatiska vägen fyrar aldrig onChange — statusraden
            // skrivs här i stället.
            if (dockLayout_.empty() || !h->setLayout(dockLayout_))
                return;
            statusTxt_->setText(trs(demo::str::DockPrefix) + h->layout());
        };

        fot(g, demo::str::DockFot, "dockVisa", 8);
    }

    // === 20 · TabControl ======================================
    {
        // 342 HÖG: rubrik, flikkontrollen (130), knappen, den inaktiverade
        // (80) och foten, räknat i Glazes knapphöjd — den högsta av de tre.
        auto* g = grupp(sLayout, demo::str::G20Flik, "g20", 644, 12, 304, 342);

        // STÄNGBARA flikar — sidorna är ÄGDA barn, och krysset fyrar
        // `onTabClose`: flikkontrollen tar aldrig bort en sida själv, så
        // handlern gör det med trädets eget `unadopt` (ägd ⇒ förstörd).
        // Filnamnen är data och översätts inte. Ctrl+Tab byter flik också
        // när fokus står INNE i en sida — och den här flikkontrollen ligger i
        // galleriets egen, så den INRE tar tangenten först.
        auto* tc      = g->add<glaze::TabControl>();
        tc->x         = 8;
        tc->anchorTop = {"g20Sep", 10};
        tc->anchors   = {true, true, true, false};
        tc->right     = 8;
        tc->height    = 130;
        tc->name      = "flikN";
        tc->setClosable(true);
        for (const char* fil : {"Rapport.pdf", "Brev.txt", "Budget.xlsx"})
            nySida(tc, fil);
        tc->setSelectedIndex(1);
        tc->onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
            flikBytt(s, e);
        };
        tc->onTabClose = [this](glaze::Widget* s, glaze::TabCloseArgs& e) {
            flikStangs(s, e);
        };

        // En sida läggs till i DRIFT med `addPage` (i `nySida`) — den nya
        // väljs med den programmatiska sättaren, som inte fyrar `onChange`.
        auto* ny      = g->add<glaze::Button>(glaze::tr(demo::str::FlikNy));
        ny->x         = 8;
        ny->anchorTop = {"flikN", 8};
        ny->name      = "flikNy";
        ny->onClick   = [this, tc] {
            nySida(tc, trs(demo::str::FlikDokument) + " " +
                           std::to_string(++nyaFlikar_));
            tc->setSelectedIndex(tc->pageCount() - 1);
        };

        // INAKTIVERAD, med översatta rubriker: varken tryck eller tangenter
        // byter flik.
        auto* td      = g->add<glaze::TabControl>();
        td->x         = 8;
        td->anchorTop = {"flikNy", 10};
        td->anchors   = {true, true, true, false};
        td->right     = 8;
        td->height    = 80;
        td->name      = "flikD";
        td->add<glaze::TabPage>(glaze::tr(demo::str::FlikAllmant));
        td->add<glaze::TabPage>(glaze::tr(demo::str::FlikAvancerat));
        td->disable();

        fot(g, demo::str::FlikFot, "flikD", 8);
    }

    // === 25 · Canvas ===============================
    // Kartan, grafen med en inaktiverad kopia och linjalprovet — en hel
    // flik, i en egen fil (gallery_canvas.cpp).
    ritytaFlik(sRita);
    tredeFlik(s3D);
    tredeVerktygFlik(s3DVerk);

    // === fönstrets egen statusrad =======================================
    statusBar.show();
    {
        auto* lead = statusBar.add<glaze::Spacer>();
        lead->setGrow(0);
        lead->width = 12;

        statusTxt_ = statusBar.add<glaze::Label>(
            glaze::tr(demo::str::GalleriStatus));
        statusTxt_->setRole(glaze::Label::Role::Dim);

        statusBar.add<glaze::Spacer>();   // grow 1 — trycker resten höger

        auto* hint = statusBar.add<glaze::Label>(
            glaze::tr(demo::str::GalleriHint));
        hint->setRole(glaze::Label::Role::Dim);

        // ETT FÄLT med den AKTIVA FLIKEN: fälten
        // står i barnordning bland appens egna barn, och hårlinjen skiljer
        // fältet från ledtexten före det. `flikBytt` skriver om det.
        statusBar.addPanel(glaze::tr(demo::str::FlikGrund));

        auto* pad = statusBar.add<glaze::Spacer>();
        pad->setGrow(0);
        pad->width = 12;
    }

    // === menyraden ======================================================
    menuBar.show();
    menuBar.addMenu(glaze::tr(demo::str::MenuFonster));
    menuBar.addItem(glaze::tr(demo::str::MenuFonster), glaze::tr(demo::str::Stang),
                    "Ctrl+W");
    menuBar.setHandler({glaze::tr(demo::str::MenuFonster), glaze::tr(demo::str::Stang)},
                       [this] { close(); });

    // === programikonen ==================================================
    // KODVÄGEN (regel 8): galleriet visar den LILLA ikonen med en egen bild
    // (startfönstret visar förvalet stort, ur `.glazeui`) och bygger
    // programmenyn med träd-API:t — `menu()` är SAMMA tillstånd som
    // `appIcon.addItem({}, …)`.
    appIcon.setIcon(glaze::img("toolbar", "gear"));
    appIcon.menu().addItem(glaze::tr(demo::str::Stang)).onClick = [this] {
        close();
    };

    // Stängknappen skapades FÖRST (se ovan) — flikkontrollen ankras mot den.
}

// ---------------------------------------------------------------- handlers

void WidgetGallery::sldChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Värdet läses ur sender (regel 6) — args bär ingen nyttolast.
    auto* s = static_cast<glaze::Slider*>(sender);
    statusTxt_->setText(trs(demo::str::SliderPrefix) +
                        std::to_string(static_cast<int>(s->value())));
    prgN_->setValue(s->value());   // det bestämda förloppet följer reglaget
}

void WidgetGallery::verktygKlickat(glaze::Widget* sender, glaze::ClickArgs&)
{
    // Vilket verktyg läses ur sender (regel 6): NAMNET är identiteten, och
    // ett växlingsbart verktyg har redan slagit om när klicket når hit —
    // `onChange` fyras före `onClick`, så läget här är det nya.
    auto*       b = static_cast<glaze::ToolButton*>(sender);
    std::string t = trs(demo::str::VerktygPrefix) + b->name;
    if (b->isCheckable())
        t += trs(b->isChecked() ? demo::str::VerktygPa
                                : demo::str::VerktygAvslagen);
    statusTxt_->setText(t);
}

// ÅNGRA ▾ — knappen ångrar den SENASTE handlingen, och en
// post i pilens meny allt fram till och med den. Demons historik tar aldrig
// slut: en tom lista fylls på igen, så knappen behåller sin pil — utan poster
// vore den en vanlig knapp.
void WidgetGallery::angraKlickat(glaze::Widget*, glaze::ClickArgs&)
{
    angraSteg(1);
}

// `onDropDown` fyras FÖRE menyn öppnas, så menyn som visas är alltid
// historiken som den är NU — också efter det knappen redan ångrat.
void WidgetGallery::angraMeny(glaze::Widget* sender, glaze::EventArgs&)
{
    byggAngraMeny(*static_cast<glaze::ToolButton*>(sender));
}

// Trädformen töms och den platta formen fyller igen — de två är samma
// tillstånd. Senaste överst, och varje post ångrar sig själv och allt efter.
void WidgetGallery::byggAngraMeny(glaze::ToolButton& b)
{
    b.menu().items.clear();
    for (std::size_t i = angraHistorik_.size(); i-- > 0;) {
        const std::uint32_t id    = angraHistorik_[i];
        const int           antal = static_cast<int>(angraHistorik_.size() - i);
        b.addItem({}, glaze::tr(id));
        b.setHandler(glaze::tr(id), [this, antal] { angraSteg(antal); });
    }
}

void WidgetGallery::angraSteg(int antal)
{
    std::uint32_t sista = 0;
    for (int i = 0; i < antal && !angraHistorik_.empty(); ++i) {
        sista = angraHistorik_.back();
        angraHistorik_.pop_back();
    }
    if (angraHistorik_.empty())
        angraHistorik_ = {demo::str::HistorikSkriv, demo::str::HistorikFlytta,
                          demo::str::HistorikRadera};
    if (sista != 0)
        statusTxt_->setText(trs(demo::str::AngratPrefix) + trs(sista));
}

void WidgetGallery::talfaltAndrat(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Värdet läses ur sender (regel 6) — och TEXTEN också: den är redan
    // formaterad med språkets decimaltecken, så demon slipper formatera
    // talet en gång till.
    auto* s = static_cast<glaze::SpinBox*>(sender);
    statusTxt_->setText(trs(demo::str::SpPrefix) + s->text());
}

void WidgetGallery::delareFlyttad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Läget läses ur sender (regel 6) — det FASTA rutans mått, i logiska
    // px, efter släppet. "px" är en enhet och inte UI-text.
    auto* s = static_cast<glaze::Splitter*>(sender);
    statusTxt_->setText(trs(demo::str::DelarePrefix) +
                        std::to_string(static_cast<int>(s->paneSize())) +
                        " px");
}

void WidgetGallery::dockAndrad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Layouten läses ur sender (regel 6) — strängen ÄR det en app sparar,
    // så demon visar den rakt av. Den är data och inte UI-text.
    auto* h = static_cast<glaze::DockHost*>(sender);
    statusTxt_->setText(trs(demo::str::DockPrefix) + h->layout());
}

void WidgetGallery::tradAndrat(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Valet läses ur sender (regel 6) — args bär ingen nyttolast. Med
    // FLERVAL är valet en lista: flera valda visas som ett antal, en ensam
    // med sin text.
    auto*             t = static_cast<glaze::TreeView*>(sender);
    const std::size_t n = t->selectedPaths().size();
    statusTxt_->setText(n > 1 ? trs(demo::str::TradValda) + std::to_string(n)
                              : trs(demo::str::TradPrefix) + t->selectedText());
}

void WidgetGallery::tradKryssad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // VILKEN nod som kryssades står i `changedPath()` och ÅT VILKET HÅLL i
    // nodens tillstånd — samma mönster som utfällningen nedan. Krysset är
    // inte valet: det ändrar inget av det `tradAndrat` skriver.
    auto*                 t = static_cast<glaze::TreeView*>(sender);
    const glaze::TreePath p = t->changedPath();
    statusTxt_->setText(trs(t->isChecked(p) ? demo::str::TradKryssad
                                            : demo::str::TradAvkryssad) +
                        t->nodeText(p));
}

void WidgetGallery::tradFalld(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // VILKEN nod som fälldes står i `changedPath()` och ÅT VILKET HÅLL i
    // nodens tillstånd — samma handler kan betjäna båda eventen.
    auto*                 t = static_cast<glaze::TreeView*>(sender);
    const glaze::TreePath p = t->changedPath();
    statusTxt_->setText(trs(demo::str::TradPrefix) + t->nodeText(p) +
                        trs(t->isExpanded(p) ? demo::str::TradUtfalld
                                             : demo::str::TradIhopfalld));
}

void WidgetGallery::tradAktiverad(glaze::Widget* sender, glaze::ActivateArgs&)
{
    // AKTIVERINGEN (dubbelklick eller Enter) gäller den AKTUELLA noden — med
    // flerval är den inte alltid den enda valda. Samma event som listvyns,
    // läst ur sender på samma sätt.
    auto* t = static_cast<glaze::TreeView*>(sender);
    statusTxt_->setText(trs(demo::str::TradOppnad) +
                        t->nodeText(t->currentPath()));
}

void WidgetGallery::tradOver(glaze::Widget* sender, glaze::DragArgs& e)
{
    // APPENS REGEL PER PLATS: en FIL får inte ligga överst. Platsen och de
    // dragna noderna läses ur sender (regel 6) som stigar, och en fil känns
    // igen på trädets egen regel — den tar inte emot något
    // (`allowDropInto`, satt när trädet byggdes). Ett nej här släcker
    // insättningslinjen och ger förbudsmärket; släppet blir inget släpp.
    auto* t = static_cast<glaze::TreeView*>(sender);
    if (!t->dropParent().empty() || t->dropIndex() < 0)
        return;   // inte överst — eller ingen plats alls
    for (const glaze::TreePath& p : t->draggedPaths())
        if (!t->allowDropInto(p)) {
            e.effect = glaze::DragEffect::None;
            return;
        }
}

void WidgetGallery::tradSlapp(glaze::Widget* sender, glaze::DragArgs& e)
{
    // SLÄPPET FRÅGAR APPEN FÖRST — handlern ser det OFLYTTADE trädet. I
    // mappen Musik sorterar appen in noderna SJÄLV, alfabetiskt efter den
    // första dragnas text, och säger det med `handled`, så trädet inte
    // flyttar en gång till. Annars utför trädet samma plats efter handlern.
    // Mappen känns igen på sitt översättnings-id och inte på texten, som
    // byter språk.
    auto*                 t   = static_cast<glaze::TreeView*>(sender);
    const glaze::TreePath mål = t->dropParent();
    const std::vector<glaze::TreePath> noder = t->draggedPaths();
    if (!noder.empty() && mål.size() == 1 &&
        t->nodeTextToken(mål) == glaze::tr(demo::str::TradMusik)) {
        // Indexet räknas i FÖRE-flytten-koordinater (`moveNodes`), så en nod
        // som redan ligger i Musik får stå med i jämförelsen.
        const std::string först = t->nodeText(noder.front());
        glaze::TreePath   barn  = mål;
        barn.push_back(0);
        int index = 0;
        while (index < t->nodeCount(mål)) {
            barn.back() = index;
            if (t->nodeText(barn) >= först)
                break;
            ++index;
        }
        // Utfällningen FÖRST: den följer noden, men stigen `mål` kan peka på
        // en annan nod efter flytten (en dragen toppnod före Musik).
        t->setExpanded(mål, true);
        if (!t->moveNodes(noder, mål, index)) {
            e.effect = glaze::DragEffect::None;   // gick inte — källan får None
            return;
        }
        e.handled = true;
        statusTxt_->setText(trs(demo::str::TradSorterade) +
                            std::to_string(noder.size()));
        return;
    }
    statusTxt_->setText(trs(demo::str::TradFlyttade) +
                        std::to_string(noder.size()));
}

void WidgetGallery::listaAndrad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Valet läses ur sender (regel 6) — med flerval är det en LISTA, i
    // visningsordningen och som appens radindex.
    auto* l = static_cast<glaze::ListView*>(sender);
    statusTxt_->setText(trs(demo::str::ListaValda) +
                        std::to_string(l->selectedRows().size()));
}

void WidgetGallery::listaAktiverad(glaze::Widget* sender, glaze::ActivateArgs&)
{
    // AKTIVERINGEN (dubbelklick eller Enter) gäller den AKTUELLA raden —
    // appens index, oberoende av hur listan är sorterad.
    auto* l = static_cast<glaze::ListView*>(sender);
    statusTxt_->setText(trs(demo::str::ListaOppnad) +
                        l->cell(l->currentRow(), 0));
}

void WidgetGallery::listaKryssad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // VILKEN rad som kryssades står i `changedRow()` och ÅT VILKET HÅLL i
    // radens tillstånd — trädets mönster, och samma statustexter. Krysset är
    // inte valet: det ändrar inget av det `listaAndrad` skriver.
    auto*     l   = static_cast<glaze::ListView*>(sender);
    const int rad = l->changedRow();
    statusTxt_->setText(trs(l->isChecked(rad) ? demo::str::TradKryssad
                                              : demo::str::TradAvkryssad) +
                        l->cell(rad, 0));
}

// SNABBMENYN MOT NODEN: högertrycket har redan
// valt noden som Utforskaren gör, och noden läses ur SENDER — `contextPath()`
// är `{}` på tom yta, och då öppnas ingen meny.
void WidgetGallery::tradMeny(glaze::Widget* sender, glaze::ContextMenuArgs& e)
{
    auto*                 t = static_cast<glaze::TreeView*>(sender);
    const glaze::TreePath p = t->contextPath();
    if (p.empty())
        return;
    statusTxt_->setText(trs(demo::str::SnabbmenyFor) + t->nodeText(p));
    snabbmeny_.popupAt(*sender, e.x, e.y);
}

// "Ta bort" tar bort VALET — bakifrån i trädordning, så en borttagen nod
// aldrig flyttar stigen till en som ännu ska bort.
void WidgetGallery::tradTaBort()
{
    std::vector<glaze::TreePath> valda = tradN_->selectedPaths();
    for (auto it = valda.rbegin(); it != valda.rend(); ++it)
        tradN_->removeNode(*it);
}

// …och mot RADEN i listvyn — samma läsväg, `contextRow()` (−1 på tom yta och
// i huvudet).
void WidgetGallery::listaMeny(glaze::Widget* sender, glaze::ContextMenuArgs&)
{
    auto*     l   = static_cast<glaze::ListView*>(sender);
    const int rad = l->contextRow();
    if (rad >= 0)
        statusTxt_->setText(trs(demo::str::SnabbmenyFor) + l->cell(rad, 0));
}

void WidgetGallery::dataAndrad(glaze::Widget* sender, glaze::CellChangeArgs& e)
{
    // VILKEN cell som sparades bär args — raden och kolumnen, appens index
    // oberoende av sorteringen. VÄRDET läses ur sender (regel 6).
    auto* g = static_cast<glaze::DataGrid*>(sender);
    statusTxt_->setText(trs(demo::str::DataPrefix) + g->cell(e.row, 0) +
                        " · " + g->columns()[std::size_t(e.column)].text.str() +
                        " = " + g->cell(e.row, e.column));
}

void WidgetGallery::flikBytt(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Den nya fliken läses ur sender (regel 6) — samma handler betjänar
    // galleriets flikar och grupp 20:s.
    auto* tc = static_cast<glaze::TabControl*>(sender);
    if (glaze::TabPage* s = tc->selectedPage()) {
        statusTxt_->setText(trs(demo::str::FlikPrefix) + s->text());
        // Statusradens fält visar galleriets AKTIVA flik — som token, så det
        // följer ett språkbyte.
        if (tc == flikar_)
            statusBar.setPanelText(0, s->textToken());
    }
}

void WidgetGallery::flikStangs(glaze::Widget* sender, glaze::TabCloseArgs& e)
{
    // VILKEN flik bär args; flikkontrollen tar aldrig bort den själv, så det
    // gör appen — texten läses FÖRE, för en ägd sida förstörs av `unadopt`.
    auto* tc = static_cast<glaze::TabControl*>(sender);
    if (glaze::TabPage* s = tc->page(e.index)) {
        statusTxt_->setText(trs(demo::str::FlikPrefix) +
                            trs(demo::str::FlikStangd) + s->text());
        tc->unadopt(*s);
    }
}

void WidgetGallery::gridChanged(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Vilken rad som ändrades läses ur sender, inte ur args.
    auto*     g = static_cast<glaze::PropertyGrid*>(sender);
    const int r = g->changedRow();
    statusTxt_->setText((r >= 0 && r < static_cast<int>(g->rows().size()))
                            ? trs(demo::str::GridPrefix) + g->rows()[r].name + " = " +
                                  g->rows()[r].value
                            : trs(demo::str::GridAndrad));
}

void WidgetGallery::filvalAndrat(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Sökvägen läses ur sender (regel 6) — skriven i fältet eller vald i
    // dialogen, samma event.
    const auto* f = static_cast<glaze::FilePicker*>(sender);
    statusTxt_->setText(trs(demo::str::FilPrefix) + f->path());
}

void WidgetGallery::fargAndrad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Färgen läses ur sender (regel 6) — i textformen, samma `#RRGGBB` som
    // popupens hexfält visar. Ett reglage som dras fyrar per läge.
    const auto* f = static_cast<glaze::ColorPicker*>(sender);
    statusTxt_->setText(trs(demo::str::FargPrefix) + f->colorText());
}

void WidgetGallery::datumAndrat(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Datumet läses ur sender (regel 6) — i ISO-form ur `dateText()`, det
    // EFFEKTIVA: medan man skriver kan fältets text vara `2026-3-5`, eller
    // ett datum utanför gränserna som redan klampats.
    const auto* d = static_cast<glaze::DatePicker*>(sender);
    statusTxt_->setText(trs(demo::str::DatumPrefix) + d->dateText() +
                        (d->showsTime() ? " " + d->timeText() : ""));
}

// FLERVALET — listboxens `onChange` och datagridens
// `onSelectionChange` delar handlern: valet läses ur sender (regel 6), och
// båda svarar med samma ord, `selectedRows()`.
void WidgetGallery::valdaRaderAndrade(glaze::Widget* sender, glaze::ChangeArgs&)
{
    std::size_t n = 0;
    if (const auto* l = dynamic_cast<const glaze::ListBox*>(sender))
        n = l->selectedRows().size();
    else if (const auto* d = dynamic_cast<const glaze::DataGrid*>(sender))
        n = d->selectedRows().size();
    statusTxt_->setText(trs(demo::str::ValdaRader) + std::to_string(n));
}

// FÄRGDIALOGEN — modal över galleriet. Medlemmen bär förra valet, och
// färgen skrivs bara vid OK.
void WidgetGallery::fargDialogKlickad(glaze::Widget*, glaze::ClickArgs&)
{
    if (fargDialog_.run(*this) == glaze::DialogResult::OK)
        statusTxt_->setText(trs(demo::str::FargdialogPrefix) +
                            hexText(fargDialog_.color()));
    else
        statusTxt_->setText(trs(demo::str::DialogAvbruten));
}

// INMATNINGSRUTAN — svaret är `std::optional`: en TOM text är ett svar,
// `nullopt` är Avbryt.
void WidgetGallery::namnDialogKlickad(glaze::Widget*, glaze::ClickArgs&)
{
    if (const auto svar = glaze::inputBox(
            *this, glaze::tr(demo::str::DialogPrompt),
            glaze::tr(demo::str::DialogNamnRubrik), lagerNamn_)) {
        lagerNamn_ = *svar;
        statusTxt_->setText(trs(demo::str::DialogNytt) + *svar);
    } else {
        statusTxt_->setText(trs(demo::str::DialogAvbruten));
    }
}

void WidgetGallery::anteckningAndrad(glaze::Widget* sender, glaze::ChangeArgs&)
{
    // Texten läses ur sender (regel 6). Raderna är de LOGISKA — styckena
    // mellan radbrytningarna, det anteckningen består av — och inte de
    // visuella, som beror på fältets bredd.
    const std::string& t     = static_cast<glaze::TextArea*>(sender)->text();
    const long         rader = 1 + std::count(t.begin(), t.end(), '\n');
    statusTxt_->setText(trs(demo::str::AntPrefix) + std::to_string(rader));
}

// ------------------------------------------------- 30 · INMATNINGEN

// SKICKA MED Ctrl+Enter — Ctrl eller Cmd, men INTE Alt: AltGr är Ctrl+Alt på
// Windows, och ett tecken som skrivs med den ska inte skicka. Upprepningen
// (tangenten hålls) skickar inte en gång till, men sväljs ändå — annars hade
// den nått fältet. Vanlig Enter lämnas åt fältet, som ger en ny rad.
void WidgetGallery::inTangent(glaze::Widget*, glaze::KeyArgs& e)
{
    const bool kort = (e.mods & (gfx::kModCtrl | gfx::kModCmd)) != 0 &&
                      (e.mods & gfx::kModAlt) == 0;
    if (e.key != gfx::Key::Return || !kort)
        return;
    e.handled = true;   // ingen radbrytning, inget mer
    if (e.repeat)
        return;
    std::string t = inMeddelande_->text();
    std::replace(t.begin(), t.end(), '\n', ' ');   // statusraden är en rad
    statusTxt_->setText(trs(demo::str::InSkickat) + t);
    inMeddelande_->setText("");
}

// Ytan har INGA gester, så bara det handlern TAR tas: mitten- och
// högertrycket — de räknas då på ytan, och deras dubbelklick kommer hit — men
// inte vänster, som ger ytan fokus som vanligt (mellanslaget behöver det).
// Högertrycket börjar linjalen.
void WidgetGallery::inYtaNed(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button == gfx::MouseButton::Left)
        return;
    e.handled = true;
    if (e.button != gfx::MouseButton::Right)
        return;
    inLinjalFran_ = e.local;
    inLinjalTill_ = e.local;
    statusTxt_->setText(glaze::tr(demo::str::InHogerNer));
    inYta_->invalidateOverlay();
}

void WidgetGallery::inYtaRor(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button != gfx::MouseButton::Right || !inLinjalFran_)
        return;
    inLinjalTill_ = e.local;
    inYta_->invalidateOverlay();   // bara en ny present — ytan ritas inte om
}

// Släppet SPARAR: längden i ytans logiska px. Ett avbrott (nedan) gör det inte.
void WidgetGallery::inYtaSlapp(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button != gfx::MouseButton::Right || !inLinjalFran_)
        return;
    const double dx = e.local.x - inLinjalFran_->x;
    const double dy = e.local.y - inLinjalFran_->y;
    statusTxt_->setText(trs(demo::str::InHogerUpp) +
                        std::to_string(std::lround(std::hypot(dx, dy))) +
                        " px");
    inLinjalFran_.reset();
    inYta_->invalidateOverlay();
}

void WidgetGallery::inYtaDubbel(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button == gfx::MouseButton::Middle)
        statusTxt_->setText(glaze::tr(demo::str::InMittenDubbel));
    else if (e.button == gfx::MouseButton::Right)
        statusTxt_->setText(glaze::tr(demo::str::InHogerDubbel));
}

// AVBROTTET: släppet kommer aldrig (Alt+Tab, en systemdialog tog musen) —
// linjalen släpps utan att något sparas.
void WidgetGallery::inYtaAvbruten(glaze::Widget*, glaze::CanvasMouseArgs&)
{
    statusTxt_->setText(glaze::tr(demo::str::InAvbrutet));
    inLinjalFran_.reset();
    inYta_->invalidateOverlay();
}

// MELLANSLAGET — ett läge som slås på av trycket och av av släppet, hela
// vägen till det släpp som aldrig kom (fokusförlusten: `cancelled`).
void WidgetGallery::inYtaTangent(glaze::Widget*, glaze::KeyArgs& e)
{
    if (e.key != gfx::Key::Space)
        return;
    e.handled = true;
    if (!e.repeat)
        statusTxt_->setText(glaze::tr(demo::str::InMellanslagHalls));
}

void WidgetGallery::inYtaTangentUpp(glaze::Widget*, glaze::KeyArgs& e)
{
    if (e.key != gfx::Key::Space)
        return;
    statusTxt_->setText(glaze::tr(e.cancelled ? demo::str::InMellanslagAvbrutet
                                              : demo::str::InMellanslagSlappt));
}

// Linjalen i overlay-lagret, ytans lokala px — temats accent.
void WidgetGallery::inYtaOverlay(glaze::Widget*, glaze::PaintArgs& e)
{
    if (!inLinjalFran_)
        return;
    const auto& col = theme().resolve(themeMode()).colors;
    e.painter.drawLine(*inLinjalFran_, inLinjalTill_,
                       {.paint = col.accent, .width = glaze::px(2.0f)});
    e.painter.fillCircle(*inLinjalFran_, 3.0, col.accent);
}

// ---------------------------------------- 35 · STORLEK OCH TANGENTER

// YTAN i statusraden — storleken läses ur sendern (regel 6), i logiska px.
void WidgetGallery::stYtaStorlek(glaze::Widget* sender, glaze::ResizeArgs&)
{
    const gfx::Sizef y = sender->actualSize();
    statusTxt_->setText(trs(demo::str::YtanPrefix) +
                        std::to_string(std::lround(y.w)) + " × " +
                        std::to_string(std::lround(y.h)));
}

// Högertrycket TAS — ytan har inga gester, och ett taget tryck ger ingen
// snabbmeny av sig självt; släppet öppnar vår.
void WidgetGallery::stYtaNed(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button == gfx::MouseButton::Right)
        e.handled = true;
}

// Snabbmenyn vid SLÄPPET, exakt vid pekaren: `toWindow` räknar ytans logiska
// punkt till fönstrets fysiska px, `Menu::popupAt`s rymd.
void WidgetGallery::stYtaSlapp(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button != gfx::MouseButton::Right)
        return;
    stMarkerat_ = e.local;
    if (const std::optional<gfx::Pointf> p = stYta_->toWindow(e.local))
        stMeny_.popupAt(*stYta_, p->x, p->y);
}

// TANGENTENS TEXT — namnet är den kanoniska symbolen, texten det trycket
// skriver (tom för Ctrl-kombinationer, navigering och döda tangenter).
void WidgetGallery::stTangent(glaze::Widget*, glaze::KeyArgs& e)
{
    statusTxt_->setText(trs(demo::str::TangentPrefix) +
                        glaze::canonicalKeyName(e.key) + " · " +
                        trs(demo::str::TextPrefix) + "«" + e.text + "»");
}
