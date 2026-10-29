// filepicker_gate — FILVÄLJARFÄLTETS grind.
//
// Kör:  ./build/tests/filepicker_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// TVÅDELAD:
//
//   HEADLESS      registret, KOMPOSITEN (två riktiga barn som inte går att
//                 lossa, `disable()` når båda), setProp-vokabulären (path,
//                 mode — ett okänt läge är en känd nyckel med fel värde),
//                 filtrens kollektionsvokabulär (`addFilter`/`setFilter`
//                 utanför intervallet/`setFilters`), att programmatisk
//                 `setPath` aldrig fyrar `onChange`, och knappens tooltip som
//                 följer läget (dialogens titel för läget).
//   MED FÖNSTER   ett DOLT ägarfönster genom den RIKTIGA ingången
//                 (`AppWindow::handleEvent`): MÅTTET (axelregeln — AUTO-bredden
//                 ur stilen, höjden ur fältet), PLACERINGEN mot ett oberoende
//                 förväntat värde (knappen i högerkanten så bred som sin egen mätning,
//                 fältet resten minus `filePickerGap`), TABBORDNINGEN (fältet
//                 och knappen är två stopp, kompositen inget), SKRIVNINGEN i
//                 fältet (ett `onChange` per tecken, sökvägen följer) — och
//                 DIALOGEN genom den riktiga `FileDialog::run`: ett klick på
//                 "…" öppnar den i sökvägens mapp, i fältets läge och med
//                 dess filter; OK ger en ny sökväg och EXAKT ett `onChange`
//                 och ett `onCommit` (ett val är en avslutad
//                 handling; fältets egen commit mäter commit_gate),
//                 Avbryt lämnar sökvägen orörd — men kryssrutan för DOLDA
//                 filer står kvar till nästa gång, också efter
//                 Avbryt — och ett inaktiverat fält öppnar ingenting.
//
// Dialogen besvaras INIFRÅN sin egen modala pump (ett postat steg): den
// hittas bland `glaze::app().windows()` som en `FileDialogWindow` (den
// interna headern), namnfältet och knapparna med `find<T>(namn)`, och OK/
// Avbryt genom knapparnas publika `performClick` — `onClick`
// och knappens `dialogResult`, det ett klick gör: Avbryt svarar
// med sitt resultat och har ingen `onClick`.
//
// SABOTAGEN den fäller (ett i taget):
//   * knappens klick som inte öppnar dialogen (D1)
//   * `onChange` som fyras också när dialogen avbryts (D2)
//   * dialogen som öppnar i hemmet i stället för i sökvägens mapp (D1)
//   * fältet som ritas ÖVER knappen — luften glömd i placeringen (F2)
//   * kryssrutan för dolda filer som fältet bara minns efter OK (D3)
//
// VAD DEN INTE MÄTER:
//   * dialogens eget innehåll och flöde — filedialog_gate.
//   * hur det SER UT (pixel_gate mäter att fältet och knappen ritar, och
//     galleriet är renderat och granskat i alla tre UI).
//   * dialogfönstret är RIKTIGT och syns ett ögonblick: grinden går genom
//     produktens `FileDialog::run`, som inte kan göras dold utifrån.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "file_dialog_model.h"    // UTF-8-sökvägarna (intern)
#include "file_dialog_window.h"   // dialogens fönster (internt)

namespace fs = std::filesystem;
namespace fd = glaze::filedlg;

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
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

std::string tr(std::uint32_t id)
{
    const glaze::TrText t = glaze::tr(id);
    return t.str();
}

// De två barnen i adopt-ordning: fältet och knappen.
glaze::TextInput* fältet(glaze::FilePicker& f)
{
    return f.children().size() > 0
               ? dynamic_cast<glaze::TextInput*>(f.children()[0])
               : nullptr;
}
glaze::Button* knappen(glaze::FilePicker& f)
{
    return f.children().size() > 1
               ? dynamic_cast<glaze::Button*>(f.children()[1])
               : nullptr;
}

// ================================================================ HEADLESS

void fallKompositen()
{
    auto w = glaze::makeWidget("filepicker");
    auto* f = dynamic_cast<glaze::FilePicker*>(w.get());
    check(f != nullptr, "R1: registret ger en riktig glaze::FilePicker");
    if (!f)
        return;
    glaze::TextInput* fält  = fältet(*f);
    glaze::Button*    knapp = knappen(*f);
    check(f->children().size() == 2 && fält && knapp,
          "R1: två riktiga barn — ett textfält och en knapp");
    check(knapp && knapp->text() == tr(glaze::str::FilePickerBrowse),
          "R1: knappens text är ramverkets \"…\"",
          knapp ? knapp->text() : std::string("?"));
    check(fält && knapp && !f->canReleaseChild(*fält) &&
              !f->canReleaseChild(*knapp) && !f->unadopt(*fält) &&
              !f->releaseOwned(*knapp) && f->children().size() == 2,
          "R2: inredningen går inte att lossa");
    check(!f->focusable() && !f->focusesChildren(),
          "R2: kompositen är INGET tabbstopp och ersätter inte sina barn");
    f->disable();
    check(fält && knapp && !fält->isEnabled() && !knapp->isEnabled(),
          "R3: disable() når fältet OCH knappen");
    f->enable();
    check(fält && knapp && fält->isEnabled() && knapp->isEnabled(),
          "R3: enable() tänder båda igen");
}

void fallVardet()
{
    glaze::FilePicker f;
    int               ändringar = 0;
    f.onChange = [&] { ++ändringar; };
    f.setPath("C:/Användare/översikt.pdf");
    check(f.path() == "C:/Användare/översikt.pdf" && fältet(f)->text() == f.path(),
          "V1: setPath — sökvägen är det fältet visar (åäö orörda)");
    check(ändringar == 0, "V1: programmatisk setPath fyrar ALDRIG onChange");
    check(f.setProp("path", "/hem/a.txt") && f.path() == "/hem/a.txt",
          "V2: setProp(\"path\")");
    check(f.setProp("mode", "save") && f.mode() == glaze::FileDialogMode::Save &&
              f.setProp("mode", "folder") &&
              f.mode() == glaze::FileDialogMode::Folder &&
              f.setProp("mode", "open") && f.mode() == glaze::FileDialogMode::Open,
          "V2: setProp(\"mode\") — open/save/folder");
    check(f.setProp("mode", "kanske") && f.mode() == glaze::FileDialogMode::Open,
          "V2: okänt läge — känd nyckel, värdet ignoreras");
    check(!f.setProp("finnsinte", "1"), "V2: okänd nyckel ⇒ false (opak)");

    // Knappens tooltip följer läget — dialogens titel för läget.
    glaze::setLanguage("sv");
    glaze::Button* k = knappen(f);
    f.setMode(glaze::FileDialogMode::Open);
    const bool öppna = k->toolTip() == tr(glaze::str::FileOpen);
    f.setMode(glaze::FileDialogMode::Save);
    const bool spara = k->toolTip() == tr(glaze::str::FileSaveAs);
    f.setMode(glaze::FileDialogMode::Folder);
    const bool mapp = k->toolTip() == tr(glaze::str::FileSelectFolder);
    check(öppna && spara && mapp, "V3: knappens tooltip följer läget");

    // ---- Q1. En SATT dialogtitel är knappens
    //      tooltip, ett tr-token följer språket, och tom ger lägets titel.
    //      Själva dialogens titel mäts i fönsterdelen (D6). SABOTAGE: en
    //      `syncToolTip` som bara läser läget (Q1 — tooltipen står kvar).
    f.setDialogTitle("Välj CSV-fil");
    check(k->toolTip() == "Välj CSV-fil" && f.dialogTitle() == "Välj CSV-fil",
          "Q1: dialogTitle blir knappens tooltip", k->toolTip());
    f.setDialogTitle(glaze::tr(glaze::str::FileOpen));
    glaze::setLanguage("en");
    const bool följer = k->toolTip() == tr(glaze::str::FileOpen) &&
                        f.dialogTitleToken().id() == glaze::str::FileOpen;
    glaze::setLanguage("sv");
    check(följer, "Q1: ett tr-token följer språket");
    f.setDialogTitle({});
    check(k->toolTip() == tr(glaze::str::FileSelectFolder),
          "Q1: tom titel — lägets titel igen", k->toolTip());
    check(f.setProp("dialogTitle", "Bilder") && f.dialogTitle() == "Bilder",
          "Q1: setProp(\"dialogTitle\")");
    f.setDialogTitle({});
}

void fallFiltren()
{
    glaze::FilePicker f;
    f.addFilter("Bilder", "*.png;*.jpg");
    f.addFilter(glaze::tr(glaze::str::FileAllFiles), "*.*");
    check(f.filters().size() == 2 && f.filters()[1].name.id() ==
                                         glaze::str::FileAllFiles,
          "F1: addFilter — literal och översättnings-id");
    f.setFilter(0, "PNG", "*.png");
    check(f.filters()[0].name.str() == "PNG" && f.filters()[0].patterns == "*.png",
          "F1: setFilter ersätter posten HELT");
    f.setFilter(5, "X", "*.x");
    f.setFilter(-1, "X", "*.x");
    check(f.filters().size() == 2, "F1: setFilter utanför ⇒ logg + no-op");
    f.setFilters({{"Text", "*.txt"}});
    check(f.filters().size() == 1 && f.filters()[0].patterns == "*.txt",
          "F1: setFilters byter allt");
}

// ============================================================= MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::FilePicker val;
    glaze::Button     efter{"efter"};
    int               ändringar = 0;
    int               commits   = 0;   // onCommit (0.1.2)

    Vard() : glaze::AppWindow("filepicker_gate", 420, 160)
    {
        val.name = "val";
        val.x    = 10;
        val.y    = 10;
        efter.x  = 10;
        efter.y  = 60;
        val.onChange = [this] { ++ändringar; };
        val.onCommit = [this] { ++commits; };
        root().adopt(val);
        root().adopt(efter);
    }

    gfx::Rectf yta(glaze::Widget& w) { return w.resolve(w.behaviorCtx()); }
    void       klick(glaze::Widget& w)
    {
        const gfx::Rectf r = yta(w);
        gfx::Event       e;
        e.windowId = window().id();
        e.mouseX   = r.x + r.w / 2.0f;
        e.mouseY   = r.y + r.h / 2.0f;
        e.button   = gfx::MouseButton::Left;
        e.type     = gfx::EventType::MouseButtonDown;
        handleEvent(e);
        e.type = gfx::EventType::MouseButtonUp;
        handleEvent(e);
    }
    void skriv(const char* utf8)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, utf8, sizeof e.text - 1);
        handleEvent(e);
    }
};

// Dialogfönstret som just öppnats, eller nullptr.
glaze::FileDialogWindow* dialogen()
{
    for (glaze::AppWindow* w : glaze::app().windows())
        if (auto* d = dynamic_cast<glaze::FileDialogWindow*>(w); d && d->isOpen())
            return d;
    return nullptr;
}

void tryck(glaze::Button* b)
{
    if (b)
        b->performClick();
}

void fallMedFonster()
{
    glaze::setLanguage("sv");
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path rot = fs::temp_directory_path() /
                         fd::pathFromUtf8("filepicker_gate_" + std::to_string(t));
    fs::create_directories(rot / fd::pathFromUtf8("Mapp ä"));
    { std::ofstream(rot / fd::pathFromUtf8("befintlig.txt")) << "x"; }
    const std::string r8 = fd::utf8FromPath(rot);

    Vard w;
    w.setOffscreen(true);
    bool kedjan = false;
    // Det de POSTADE stegen skriver bor HÄR och inte i kedjans lambda: ett
    // steg som inte körs inne i en dialogs pump (D5 — ingen dialog öppnas,
    // eller ett sabotage som hindrar en att öppnas) körs först efter att
    // kedjans lambda returnerat, och dess lokala variabler är då borta.
    bool öppnad = false, rättMapp = false, rättTitel = false, öppnadIgen = false,
         doldaKvar = false;
    glaze::app().post([&] {
        kedjan = true;
        if (!w.isOpen()) {
            std::printf("  --   fönsterdelen hoppas över: fönstret kom aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }
        glaze::TextInput* fält  = fältet(w.val);
        glaze::Button*    knapp = knappen(w.val);
        const float       s     = w.window().uiScale();
        const glaze::Style& m   = w.style();

        // ---- F1. MÅTTET: AUTO-bredden ur stilen, höjden ur fältet.
        const gfx::Rectf r  = w.yta(w.val);
        const gfx::Rectf rf = w.yta(*fält);
        const gfx::Rectf rk = w.yta(*knapp);
        check(std::abs(r.w - m.filePickerDefaultW * s) < 0.5f,
              "F1: AUTO-bredden är stilens filePickerDefaultW",
              std::to_string(r.w / s));
        check(std::abs(r.h - (fält->resolve(fält->behaviorCtx()).h)) < 0.5f &&
                  r.h > 0.0f,
              "F1: höjden är fältets egen");
        // ---- F2. PLACERINGEN mot ett oberoende förväntat värde: knappen i högerkanten,
        //          hela höjden; fältet från vänsterkanten fram till luften.
        check(std::abs(rk.x + rk.w - (r.x + r.w)) < 0.5f && rk.h == r.h &&
                  rk.w > 0.0f,
              "F2: knappen står i högerkanten, hela höjden");
        check(std::abs(rf.x - r.x) < 0.5f &&
                  std::abs(rf.x + rf.w + m.filePickerGap * s - rk.x) < 0.5f,
              "F2: fältet tar resten minus filePickerGap",
              std::to_string((rk.x - (rf.x + rf.w)) / s));

        // ---- T1. TABBORDNINGEN: fältet och knappen, sedan "efter".
        const auto ord = w.focusOrder();
        check(ord.size() == 3 && ord[0] == fält && ord[1] == knapp &&
                  ord[2] == &w.efter,
              "T1: två stopp — fältet och knappen — kompositen inget",
              std::to_string(ord.size()));

        // ---- S1. SKRIVNINGEN: ett onChange per tecken, sökvägen följer.
        fält->focus();
        const int n0 = w.ändringar;
        w.skriv("a");
        w.skriv("ö");
        check(w.val.path() == "aö" && w.ändringar == n0 + 2,
              "S1: skrivet i fältet ⇒ sökvägen och ett onChange per tecken",
              w.val.path());

        // ---- D1. DIALOGEN — öppningsläget, OK på en befintlig fil.
        w.val.setPath(r8 + "/inget-här.txt");
        w.val.setFilters({{"Text", "*.txt"}});
        const int n1 = w.ändringar;
        // `setPath` flyttade fältets commit-baslinje, så när fokus går till
        // "…" committas inte det som skrevs i S1 — bara dialogens val räknas.
        const int c1 = w.commits;
        glaze::app().post([&] {
            glaze::FileDialogWindow* d = dialogen();
            öppnad = d != nullptr;
            if (!d)
                return;
            rättMapp  = d->currentFolder() == rot;
            rättTitel = d->title() == tr(glaze::str::FileOpen);
            d->find<glaze::TextInput>("name")->setText("befintlig.txt");
            tryck(d->find<glaze::Button>("ok"));
            d->close();   // fallnät — OK har redan stängt
        });
        w.klick(*knapp);
        check(öppnad, "D1: ett klick på \"…\" öppnar filväljardialogen");
        if (!öppnad) {
            // Utan dialog har resten av D-stegen inget att svara på: ett
            // senare `browse()` hade öppnat en dialog åt ett steg som redan
            // körts, och den sista hade stått öppen för evigt. Grinden
            // avslutar hellre med sitt fel än hänger.
            std::error_code ec;
            fs::remove_all(rot, ec);
            w.close();
            return;
        }
        check(rättMapp && rättTitel,
              "D1: dialogen öppnar i sökvägens mapp, i fältets läge");
        check(fd::pathFromUtf8(w.val.path()) ==
                      rot / fd::pathFromUtf8("befintlig.txt") &&
                  w.ändringar == n1 + 1 && w.commits == c1 + 1,
              "D1: OK ger den valda sökvägen och EXAKT ett onChange och ett "
              "onCommit", w.val.path() + " · " + std::to_string(w.commits - c1));

        // ---- D2. Avbryt lämnar allt orört — UTOM kryssrutan för dolda filer,
        //          som är en vyinställning: den kryssas här (rutans egen
        //          `onChange`, samma handler som ett klick) och ska stå
        //          kryssad nästa gång (D3).
        const std::string före = w.val.path();
        const int         n2   = w.ändringar;
        const int         c2   = w.commits;
        glaze::app().post([&] {
            if (glaze::FileDialogWindow* d = dialogen()) {
                if (auto* k = d->find<glaze::Checkbox>("hidden"); k && k->onChange) {
                    k->setChecked(true);
                    glaze::ChangeArgs e;
                    k->onChange(k, e);
                }
                tryck(d->find<glaze::Button>("cancel"));
                d->close();
            }
        });
        w.klick(*knapp);
        check(w.val.path() == före && w.ändringar == n2 && w.commits == c2,
              "D2: Avbryt ⇒ sökvägen orörd, inget onChange och inget onCommit");

        // ---- D3. SPARLÄGET: ett nytt namn får filtrets ändelse — och
        //          kryssrutan från D2 står kvar.
        w.val.setMode(glaze::FileDialogMode::Save);
        const int n3 = w.ändringar;
        glaze::app().post([&] {
            if (glaze::FileDialogWindow* d = dialogen()) {
                auto* k   = d->find<glaze::Checkbox>("hidden");
                doldaKvar = k && k->isChecked();
                d->find<glaze::TextInput>("name")->setText("ny");
                tryck(d->find<glaze::Button>("ok"));
                d->close();
            }
        });
        check(w.val.browse() &&
                  fd::pathFromUtf8(w.val.path()) == rot / fd::pathFromUtf8("ny.txt") &&
                  w.ändringar == n3 + 1,
              "D3: browse() i sparläget — \"ny\" + filtrets \".txt\"",
              w.val.path());
        check(doldaKvar,
              "D3: kryssrutan för dolda filer står kvar från förra gången — "
              "fast den dialogen avbröts (D2)");

        // ---- D4. MAPPLÄGET: OK utan val ger mappen sökvägen pekar på.
        w.val.setMode(glaze::FileDialogMode::Folder);
        w.val.setPath(fd::utf8FromPath(rot / fd::pathFromUtf8("Mapp ä")));
        glaze::app().post([&] {
            if (glaze::FileDialogWindow* d = dialogen()) {
                tryck(d->find<glaze::Button>("ok"));
                d->close();
            }
        });
        const int n4 = w.ändringar;
        check(w.val.browse() &&
                  fd::pathFromUtf8(w.val.path()) == rot / fd::pathFromUtf8("Mapp ä") &&
                  w.ändringar == n4,
              "D4: mappläget öppnar I mappen; samma svar ⇒ inget onChange",
              w.val.path());

        // ---- D6. DIALOGENS TITEL är fältets
        //      `dialogTitle` — och tom igen lägets.
        w.val.setDialogTitle("Välj CSV-fil");
        std::string titel6;
        glaze::app().post([&] {
            if (glaze::FileDialogWindow* d = dialogen()) {
                titel6 = d->title();
                tryck(d->find<glaze::Button>("cancel"));
                d->close();
            }
        });
        w.val.browse();
        check(titel6 == "Välj CSV-fil",
              "D6: dialogTitle blir dialogens titel", titel6);
        w.val.setDialogTitle({});

        // ---- D5. INAKTIVERAT: knappen öppnar ingenting.
        w.val.disable();
        glaze::app().post([&] {
            if (glaze::FileDialogWindow* d = dialogen()) {
                öppnadIgen = true;
                d->close();
            }
        });
        w.klick(*knapp);
        // Steget ovan körs i NÄSTA pumpvarv — dialogen hade då redan stått
        // öppen om klicket nått knappen. Ett andra steg avgör.
        glaze::app().post([&] {
            check(!öppnadIgen, "D5: ett inaktiverat fält öppnar ingen dialog");
            std::error_code ec;
            fs::remove_all(rot, ec);
            w.close();
        });
    });
    w.run();
    if (!kedjan) {
        std::printf("  --   fönsterdelen kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("filepicker_gate — filväljarfältet\n");
    fallKompositen();
    fallVardet();
    fallFiltren();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
