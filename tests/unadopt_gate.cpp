// unadopt_gate — BORTTAGNINGENS grind.
//
// Kör:  ./build/tests/unadopt_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Trädet har en motsats till `adopt` — `unadopt`, `releaseOwned` och
// `transferChild` — och den här grinden mäter att den river rätt: vad som
// släpps, vad som lever vidare och vad som aldrig får gå sönder.
//
// VAD GRINDEN MÄTER, och varför just det:
//
//   SYMMETRIN     ett LÅNAT barn släpps och går att adoptera OM (den vägen
//                 kräver att `parent_` faktiskt nollställdes —
//                 dubbeladoptionsvakten läser just det fältet), ett ÄGT
//                 barn kan lämnas ut som `unique_ptr` och lever vidare
//                 fristående.
//   ATOMICITETEN  varje NEKAT fall verifieras "trädet är EXAKT som det
//                 var". Det är hela poängen med att valideringen är fyra
//                 kontroller FÖRE mutationen: en halvt utförd borttagning
//                 är värre än ingen.
//   DEN FARLIGA FORMEN  `releaseOwned` på ett LÅNAT barn måste ge tom
//                 retur OCH ett orört träd. Tom retur betyder normalt
//                 misslyckande, så en tom retur som ändå muterat hade
//                 lurat anroparen att tro att trädet står kvar.
//   DENY-FALLEN   en öppen `Dropdown`-popup och en öppen MENY nekas, och
//                 nekandet gäller fortfarande direkt efter `close()` —
//                 stängningen är uppskjuten till pump-svansen. Skrivet
//                 beteende, inte en bugg, och därför grindat.
//   FLYTTEN       `transferChild` (fall 9): ÄGANDET FÖLJER MED
//                 — ett ägt barn förstörs av sin NYA förälder och inte av den
//                 gamla (en räknare i destruktorn), ett lånat förblir lånat
//                 (`releaseOwned` nekar det efteråt) — och varje nekad flytt
//                 (popup djupt i delträdet, målet är föräldern, barnet självt
//                 eller i dess delträd, inget direkt barn) lämnar BÅDA träden
//                 orörda. Fokus i det flyttade delträdet städas i det GAMLA
//                 fönstret, och stoppet finns i det nya. SABOTAGEN som ska
//                 fälla fallet: flytten adopterar LÅNAT i stället för att
//                 föra `unique_ptr` vidare (räknaren står på 0 när den nya
//                 föräldern rivs), valideringen borttagen (popupfallet och
//                 cykelfallet muterar), cykelkontrollen borttagen
//                 (cykelfallet). Fallet mäter INTE flytten mellan två ÖPPNA fönster
//                 med bildcache och rityta (dock_gate:s flytande panel gör
//                 den vägen).
//
// TVÅDELAD som dropdown_gate: headless för allt som inte kräver ett
// fönster, och ett DOLT fönster (`setOffscreen`, som i window_gate)
// för deny-fallen — de körs som en KEDJA AV POSTADE STEG, för `App::post`
// körs i pump-svansen och är därmed klockan som mäter uppskjutenheten.

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_skips   = 0;
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

// En naken widget: allt som mäts här är TRÄDET, inte ritningen.
struct Nod : glaze::Widget {
    explicit Nod(std::string n) { name = std::move(n); }
    void draw(const glaze::Ctx&) override {}
};

// Barnens namn i ordning — "trädet är oförändrat" mäts som en STRÄNG, så
// ett fel visar exakt vad som hände i stället för bara `false`.
std::string barnen(const glaze::Widget& w)
{
    std::string s;
    for (const glaze::Widget* ch : w.children())
        s += (s.empty() ? "" : ",") + ch->name;
    return s;
}

// ------------------------------------------------------- 1. SYMMETRIN

void fallLanat()
{
    std::printf("\n=== 1. LÅNAT barn: släpps, och kan adopteras OM ===\n");

    glaze::Panel rot;
    Nod          a("a"), b("b"), c("c");
    rot.adopt(a);
    rot.adopt(b);
    rot.adopt(c);
    check(barnen(rot) == "a,b,c", "utgångsläget", barnen(rot));

    check(rot.unadopt(b), "unadopt(lånat) svarar true");
    check(barnen(rot) == "a,c", "barnet är borta, ordningen bevarad",
          barnen(rot));
    check(b.parent() == nullptr, "det lossade barnet har ingen förälder");

    // ÅTER-ADOPTIONEN är det egentliga beviset: `adopt(Widget&)` vägrar om
    // `parent_` är satt, så den fungerar bara om `unadopt` faktiskt nollade
    // fältet i stället för att bara plocka bort pekaren ur listan.
    rot.adopt(b);
    check(barnen(rot) == "a,c,b",
          "åter-adoption fungerar (dubbeladoptionsvakten läser parent_)",
          barnen(rot));

    // Objektet lever — trädet ägde det aldrig.
    check(b.name == "b", "det lånade objektet lever vidare");
}

void fallAgt()
{
    std::printf("\n=== 2. ÄGT barn: unadopt FÖRSTÖR, releaseOwned LÄMNAR UT "
                "===\n");

    // (a) `releaseOwned` ger ägandet tillbaka — flik-fallet.
    {
        glaze::Panel rot;
        auto*        ägd = rot.add<Nod>("ägd");
        rot.add<Nod>("kvar");
        check(barnen(rot) == "ägd,kvar", "utgångsläget", barnen(rot));

        std::unique_ptr<glaze::Widget> ut = rot.releaseOwned(*ägd);
        check(ut != nullptr, "releaseOwned(ägt) lämnar ut en unique_ptr");
        check(ut.get() == ägd, "…och det är rätt objekt");
        check(barnen(rot) == "kvar", "barnet är borta ur trädet", barnen(rot));
        check(ut && ut->parent() == nullptr && ut->children().empty(),
              "det utlämnade trädet är FRISTÅENDE");

        // Ett fristående träd ska vara no-op-säkert: `invalidate()` utan
        // ägarfönster får inte krascha, och det går att adoptera in igen.
        ut->invalidate();
        rot.adopt(std::move(ut));
        check(barnen(rot) == "kvar,ägd", "det utlämnade trädet kan adopteras in",
              barnen(rot));
    }

    // (b) `unadopt` på ett ÄGT barn är symmetrin till `adopt(unique_ptr)`:
    // objektet FÖRSTÖRS. Mäts med en destruktorräknare, för det går inte
    // att läsa av på pekaren efteråt.
    {
        struct Räknad : glaze::Widget {
            int* n;
            explicit Räknad(int* r) : n(r) {}
            ~Räknad() override { ++*n; }
            void draw(const glaze::Ctx&) override {}
        };

        int          döda = 0;
        glaze::Panel rot;
        auto*        ägd = rot.add<Räknad>(&döda);
        check(rot.unadopt(*ägd), "unadopt(ägt) svarar true");
        check(döda == 1, "unadopt(ägt) FÖRSTÖR barnet (symmetrin till adopt)",
              std::to_string(döda));
        check(rot.children().empty(), "…och listan är tom");
    }
}

// -------------------------------------------- 3. DEN FARLIGA FORMEN

void fallReleaseOwnedPaLanat()
{
    std::printf("\n=== 3. releaseOwned på ett LÅNAT barn: tom retur OCH "
                "orört träd ===\n");

    glaze::Panel rot;
    Nod          a("a"), b("b");
    rot.adopt(a);
    rot.adopt(b);

    std::unique_ptr<glaze::Widget> ut = rot.releaseOwned(b);

    // Tom retur betyder MISSLYCKANDE — och då måste trädet vara orört, för
    // annars tror anroparen att `b` sitter kvar när den inte gör det.
    check(ut == nullptr, "releaseOwned(lånat) svarar med tom unique_ptr");
    check(barnen(rot) == "a,b", "barnordningen är INTAKT", barnen(rot));
    check(b.parent() == &rot, "barnets parent() är INTAKT");
}

// ------------------------------------------ 4. VALIDERINGENS ÖVRIGA NEJ

void fallInteMittBarn()
{
    std::printf("\n=== 4. inte ett direkt barn ⇒ nekas ===\n");

    glaze::Panel rot, mellan;
    Nod          barnbarn("barnbarn");
    rot.adopt(mellan);
    mellan.adopt(barnbarn);

    check(!rot.unadopt(barnbarn), "unadopt av ett BARNBARN nekas");
    check(barnbarn.parent() == &mellan, "…och trädet är orört");

    Nod fri("fri");
    check(!rot.unadopt(fri), "unadopt av en widget som inte sitter i trädet");
    check(rot.releaseOwned(fri) == nullptr, "…och releaseOwned likaså");
}

void fallStrukturelltBarn()
{
    std::printf("\n=== 5. canReleaseChild — kompositens egen inredning ===\n");

    // `PropertyGrid` skapar `editor_`/`picker_` med add<> i ctorn och håller
    // RÅA pekare till dem. `children()` är publikt, så vägen in finns —
    // vakten är att griden nekar.
    glaze::PropertyGrid grid;
    check(!grid.children().empty(),
          "griden har interna barn att skydda (annars mäter fallet inget)",
          std::to_string(grid.children().size()));
    if (grid.children().empty())
        return;

    const std::string före = barnen(grid);
    glaze::Widget*    inre = grid.children()[0];
    check(!grid.unadopt(*inre), "grid.unadopt(editor) NEKAS");
    check(grid.releaseOwned(*inre) == nullptr, "grid.releaseOwned(editor) NEKAS");
    check(barnen(grid) == före, "…och grinden är orörd", barnen(grid));

    // Ett barn som INTE är inredning går att ta bort som vanligt — vakten
    // får inte låsa hela widgeten.
    Nod eget("eget");
    grid.adopt(eget);
    check(grid.unadopt(eget), "ett VANLIGT barn i griden går att ta bort");
}

// ------------------------------- 5b. DENY-REKURSIONEN, HELT UTAN OS
//
// Fall 8 nedan öppnar en RIKTIG popup och kan därför hoppas över på en
// maskin utan display — och då mäts deny-regeln inte alls, medan ctest
// ändå blev grön. Det här fallet stänger det hålet:
// `hasOpenPopup()` är en virtual, så en testwidget kan svara `true` utan
// att något fönster finns. Rekursionen, ordningen och "ingen mutation vid
// nej" grindas alltså ALLTID, på varje maskin.
struct FejkPopup : glaze::Panel {
    bool hasOpenPopup() const override { return true; }
};

void fallDenyUtanOS()
{
    std::printf("\n=== 5b. deny-rekursionen (OS-fritt) ===\n");

    Nod            rot("rot");
    glaze::Panel   mellan;
    FejkPopup      barare;
    Nod            syskon("syskon");

    rot.adopt(mellan);
    mellan.adopt(barare);   // DJUPT — rekursionen måste hitta den
    rot.adopt(syskon);

    const std::string före = barnen(rot);

    check(!rot.unadopt(mellan),
          "unadopt NEKAS när en popup-bärare sitter DJUPT i delträdet");
    check(rot.releaseOwned(mellan) == nullptr,
          "releaseOwned NEKAS av samma skäl");
    check(barnen(rot) == före, "…och trädet är ORÖRT efter nejet", barnen(rot));
    check(mellan.parent() == &rot, "…förälderkedjan intakt");

    // Ett syskon UTAN popup i sitt delträd går att ta bort som vanligt —
    // vakten får inte låsa hela trädet.
    check(rot.unadopt(syskon),
          "ett syskon utan popup i delträdet går att ta bort");

    // Och bäraren SJÄLV nekas när man tar den direkt (roten i checken).
    const std::string före2 = barnen(mellan);
    check(!mellan.unadopt(barare), "bäraren själv nekas också");
    check(barnen(mellan) == före2, "…orört", barnen(mellan));
}

// ----------------------------------------------- 6. FÖNSTRETS INBYGGDA

struct Vard : glaze::AppWindow {
    glaze::Dropdown dd;
    glaze::Panel    panel;

    Vard() : glaze::AppWindow("unadopt_gate", 400, 300, gfx::Chrome::Custom)
    {
        dd.x     = 10;
        dd.y     = 10;
        dd.width = 160;
        root().adopt(panel);
        panel.adopt(dd);   // DJUPT: deny-checken frågas rekursivt
    }
    using glaze::AppWindow::makeCtx;
    using glaze::AppWindow::root;
};

void fallInbyggda()
{
    std::printf("\n=== 6. fönstrets INBYGGDA kan inte tas bort ===\n");

    Vard w;
    const std::string före = barnen(w.root());

    check(!w.root().unadopt(w.statusBar), "statusBar nekas");
    check(!w.root().unadopt(w.menuBar), "menuBar nekas");
    // PROGRAMIKONEN — den tredje inbyggda och ett
    // CHROME-barn som TopBar: `isBuiltinMember` måste känna den, annars
    // lämnar en borttagning namnlistens motvikt och hit-kartan hängande.
    check(!w.root().unadopt(w.appIcon), "appIcon nekas");
    if (glaze::TopBar* tb = w.topBar())
        check(!w.root().unadopt(*tb), "topBar nekas");
    else
        check(false, "fönstret saknar TopBar (Chrome::Custom förväntades)");
    check(barnen(w.root()) == före, "…och rot-trädet är orört",
          barnen(w.root()));

    // Ett VANLIGT barn i samma rot går att ta bort — vakten träffar bara
    // de inbyggda.
    check(w.root().unadopt(w.panel), "ett vanligt barn i roten går att ta bort");
    w.root().adopt(w.panel);   // återställ inför nästa fall
}

// ------------------------------------------------- 7. FOKUSSTÄDNINGEN

void fallFokus()
{
    std::printf("\n=== 7. fokus/press/capture städas när trädet lämnar ===\n");

    struct FokusVard : glaze::AppWindow {
        glaze::Panel  panel;
        glaze::Button knapp{"tryck"};
        FokusVard() : glaze::AppWindow("unadopt_focus", 400, 300)
        {
            root().adopt(panel);
            panel.adopt(knapp);
        }
        using glaze::AppWindow::focusOrder;
        using glaze::AppWindow::root;
    };

    FokusVard w;
    check(w.focusOrder().size() == 1, "knappen är fönstrets enda tabbstopp",
          std::to_string(w.focusOrder().size()));

    // Fokusera knappen och lossa hela PANELEN — fokuset sitter alltså på ett
    // BARNBARN till det som tas bort.
    w.knapp.focus();
    check(w.knapp.focused(), "knappen har fokus före borttagningen");

    check(w.root().unadopt(w.panel), "panelen (med fokuserat barnbarn) lossas");
    check(!w.knapp.focused(), "fokusflaggan är nollställd på barnbarnet");
    check(w.focusOrder().empty(), "fönstrets tab-lista är tom efteråt",
          std::to_string(w.focusOrder().size()));
}

// ----------------------------------------- 8. DENY: ÖPPEN POPUP / MENY
//
// KEDJA AV POSTADE STEG (dropdown_gate-mönstret): `App::post` körs i
// pump-SVANSEN, alltså efter att föregående varvs `flushPendingCloses()`
// faktiskt rivit popupen. Det är den enda klockan som kan mäta att
// nekandet gäller ÄVEN direkt efter `close()`.

void fallDeny()
{
    std::printf("\n=== 8. DENY: öppen popup och öppen meny ===\n");

    Vard w;
    w.setOffscreen(true);
    w.dd.setItems({"a", "b", "c"});

    glaze::Menu meny;
    meny.addItem("Ett");
    meny.addItem("Två");

    bool kedjanKordes = false;
    int  steg         = 0;

    std::function<void()> nasta;
    nasta = [&] {
        switch (steg++) {
        case 0: {
            kedjanKordes = true;
            w.dd.open();
            if (!w.dd.isOpen()) {
                std::printf("  --   deny-fallen hoppas över: popup-fönstret "
                            "kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            // Dropdownen sitter i `panel`, alltså ETT steg ner — checken
            // måste vara REKURSIV för att träffa.
            const std::string före = barnen(w.root());
            check(!w.root().unadopt(w.panel),
                  "öppen popup DJUPT i delträdet ⇒ unadopt NEKAS");
            check(w.root().releaseOwned(w.panel) == nullptr,
                  "…och releaseOwned likaså");
            check(barnen(w.root()) == före, "…och trädet är orört",
                  barnen(w.root()));

            // STÄNGNINGEN ÄR UPPSKJUTEN: direkt efter `close()` är popupen
            // fortfarande uppe, så nekandet ska stå kvar.
            w.dd.close();
            check(!w.root().unadopt(w.panel),
                  "DIREKT efter close() nekas den fortfarande (uppskjuten "
                  "stängning — skrivet beteende)");
            break;
        }
        case 1:
            // Ett pumpvarv senare är popupen riven och operationen går.
            check(!w.dd.isOpen(), "popupen är stängd efter ett pumpvarv");
            check(w.root().unadopt(w.panel),
                  "efter pumpvarvet SLÄPPS delträdet");
            w.root().adopt(w.panel);

            // MENYFALLET: värden är dropdownen, alltså återigen djupt nere.
            meny.popupAt(w.dd, 0.0f, 0.0f);
            if (!meny.isOpen()) {
                std::printf("  --   menyfallet hoppas över: menyn kom aldrig "
                            "upp\n");
                ++g_skips;
                w.close();
                return;
            }
            check(!w.root().unadopt(w.panel),
                  "VÄRD för en öppen meny ⇒ unadopt NEKAS");
            meny.close();
            break;
        case 2:
            // MENYN BEHÖVER TVÅ PUMPVARV, dropdownen ETT — och skillnaden
            // är inte slump utan TVÅ uppskjutningar staplade på varandra:
            // (1) `closeChain()` köar popup-fönstrets stängning, som App
            // verkställer i svansen och som då kör `finish()`, och (2)
            // `finish()` POSTAR sin egen självdestruktion (presentationen
            // får aldrig raderas inifrån sin egen `onClose`), så OBJEKTET
            // ligger kvar i modullistan ett varv till. Men den uppskjutna
            // destruktionen är LIVSTIDSHANTERING, inte "menyn är öppen":
            // kedjan är stängd, kroken släppt och callbacks körda.
            // `menuPresentationHostedIn` hoppar därför över AVSLUTADE
            // presentationer, så deny-fönstret är ETT varv — samma som
            // dropdownens. Annars krävdes TVÅ varv, och `Menu::isOpen()` sade
            // stängd medan
            // borttagningen nekades.
            check(!meny.isOpen(), "menyn är stängd efter ett pumpvarv");
            check(w.root().unadopt(w.panel),
                  "…och delträdet SLÄPPS i samma varv — inte ett senare");
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

    if (!kedjanKordes) {
        std::printf("  --   deny-fallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// --------------------------------------------- 9. FLYTTEN (transferChild)

// En widget som räknar sina destruktioner — ÄGANDET mäts genom VEM som river
// den, inte genom en flagga.
int g_rivna = 0;
struct Raknare : glaze::Widget {
    explicit Raknare(std::string n) { name = std::move(n); }
    ~Raknare() override { ++g_rivna; }
    void draw(const glaze::Ctx&) override {}
};

void fallFlytten()
{
    std::printf("\n=== 9. FLYTTEN: transferChild ===\n");

    // (a) LÅNAT förblir lånat.
    {
        glaze::Panel rot1, rot2;
        Nod          a("a"), b("b");
        rot1.adopt(a);
        rot1.adopt(b);
        check(rot1.transferChild(a, rot2), "transferChild(lånat) svarar true");
        check(barnen(rot1) == "b" && barnen(rot2) == "a" &&
                  a.parent() == &rot2,
              "barnet bor i det nya trädet och inte i det gamla",
              barnen(rot1) + " | " + barnen(rot2));
        check(rot2.releaseOwned(a) == nullptr && barnen(rot2) == "a",
              "det LÅNADE barnet förblir lånat — releaseOwned nekar, trädet "
              "orört");
    }

    // (b) ÄGT förblir ägt: den nya föräldern river det, den gamla inte.
    {
        g_rivna = 0;
        glaze::Panel rot2;
        Raknare*     ägd = nullptr;
        {
            glaze::Panel rot1;
            ägd = rot1.add<Raknare>("ägd");
            check(rot1.transferChild(*ägd, rot2),
                  "transferChild(ägt) svarar true");
        }   // rot1 rivs — det flyttade barnet får inte följa med
        check(g_rivna == 0 && ägd->parent() == &rot2,
              "den GAMLA förälderns rivning rör inte det flyttade ägda barnet",
              std::to_string(g_rivna));
        std::unique_ptr<glaze::Widget> tillbaka = rot2.releaseOwned(*ägd);
        check(tillbaka.get() == ägd,
              "…och det är ÄGT av den nya — releaseOwned lämnar ut det");
        rot2.adopt(std::move(tillbaka));
    }
    check(g_rivna == 1, "den NYA förälderns rivning river det ägda barnet",
          std::to_string(g_rivna));

    // (c) NEKAT: en popup djupt i delträdet — båda träden orörda.
    {
        glaze::Panel rot1, rot2;
        glaze::Panel mellan;
        FejkPopup    bärare;
        Nod          kvar("kvar");
        mellan.name = "mellan";
        rot1.adopt(mellan);
        mellan.adopt(bärare);
        rot2.adopt(kvar);
        check(!rot1.transferChild(mellan, rot2) &&
                  barnen(rot1) == "mellan" && barnen(rot2) == "kvar" &&
                  mellan.parent() == &rot1,
              "öppen popup DJUPT i delträdet ⇒ flytten NEKAS, båda träden "
              "orörda",
              barnen(rot1) + " | " + barnen(rot2));
    }

    // (d) NEKAT: målet är föräldern, barnet självt eller i dess delträd (en
    // cykel) — och ett barn som inte är mitt direkta.
    {
        glaze::Panel rot, annan;
        glaze::Panel mellan;
        Nod          barnbarn("barnbarn");
        mellan.name = "mellan";
        rot.adopt(mellan);
        mellan.adopt(barnbarn);
        check(!rot.transferChild(mellan, rot) && barnen(rot) == "mellan",
              "målet är redan föräldern ⇒ nekas, orört", barnen(rot));
        check(!rot.transferChild(mellan, mellan) && barnen(rot) == "mellan" &&
                  mellan.parent() == &rot,
              "målet är barnet självt ⇒ nekas (en cykel), orört");
        check(!rot.transferChild(mellan, barnbarn) && barnen(rot) == "mellan" &&
                  barnen(mellan) == "barnbarn" && barnbarn.parent() == &mellan,
              "målet ligger i barnets delträd ⇒ nekas (en cykel), orört",
              barnen(rot) + " | " + barnen(mellan));
        check(!rot.transferChild(barnbarn, annan) &&
                  barnen(mellan) == "barnbarn" && barnen(annan).empty(),
              "ett barnbarn är inget direkt barn ⇒ nekas, orört");
    }

    // (e) FÖNSTREN: fokus i det flyttade delträdet städas i det gamla
    // fönstret, och tabbstoppet finns i det nya.
    {
        struct Ett : glaze::AppWindow {
            glaze::Panel  panel;
            glaze::Button knapp{"tryck"};
            Ett() : glaze::AppWindow("transfer_ett", 400, 300)
            {
                root().adopt(panel);
                panel.adopt(knapp);
            }
            using glaze::AppWindow::focusOrder;
            using glaze::AppWindow::root;
        };
        struct Tva : glaze::AppWindow {
            Tva() : glaze::AppWindow("transfer_tva", 400, 300) {}
            using glaze::AppWindow::focusOrder;
            using glaze::AppWindow::root;
        };
        Ett ett;
        Tva tva;
        ett.knapp.focus();
        check(ett.knapp.focused(), "knappen har fokus i det första fönstret");
        check(ett.root().transferChild(ett.panel, tva.root()),
              "panelen flyttas till det andra fönstret");
        check(!ett.knapp.focused() && ett.focusOrder().empty(),
              "fokus och tabbstoppet är borta ur det GAMLA fönstret",
              std::to_string(ett.focusOrder().size()));
        check(ett.knapp.appWindow() == &tva && tva.focusOrder().size() == 1,
              "knappen hör till det NYA fönstret och är dess tabbstopp",
              std::to_string(tva.focusOrder().size()));
        tva.root().transferChild(ett.panel, ett.root());   // hem före rivningen
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("unadopt_gate — trädets borttagning\n");

    fallLanat();
    fallAgt();
    fallReleaseOwnedPaLanat();
    fallInteMittBarn();
    fallStrukturelltBarn();
    fallDenyUtanOS();
    fallInbyggda();
    fallFokus();
    fallFlytten();
    fallDeny();

    // ÖVERHOPPNING FÅR INTE SE UT SOM GRÖNT: en maskin utan display ger
    // kod 77, som `tests/CMakeLists.txt` gör till ctest SKIPPED — annars
    // hade deny-fall som aldrig kördes rapporterats GRÖNA, exakt
    // den falska tryggheten `layout_gate` fall 16 står som avskräckande
    // exempel för.
    // Kärnlogiken är ändå alltid mätt — fall 5b kör OS-fritt.
    if (g_skips) {
        std::printf("\n     %d block överhoppade (se raderna ovan)\n", g_skips);
        std::printf("\n%s — %d fall, %d fel, %d ÖVERHOPPADE\n",
                    g_fails == 0 ? "UNADOPT_GATE OFULLSTÄNDIG"
                                 : "UNADOPT_GATE FÄLLD",
                    g_cases, g_fails, g_skips);
        return g_fails == 0 ? 77 : 1;
    }
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "UNADOPT_GATE GRÖN" : "UNADOPT_GATE FÄLLD",
                g_cases, g_fails);
    return g_fails == 0 ? 0 : 1;
}
