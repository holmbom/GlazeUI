// statusbar_gate — STATUSRADENS FÄLT.
//
// Kör:  ./build/tests/statusbar_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterblocket kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt
//
// HEADLESS
//   B1  `addPanel` ×3 i ordning, `panelCount`, `panelText`, `panel(i)` med
//       bredden och fyllningen; utanför ⇒ tomt
//   B2  `setPanel` är TILLDELNING (en utelämnad bredd blir AUTO, fyllningen
//       av), `setPanelText` byter BARA texten, ett index utanför ⇒ no-op,
//       och `glaze::tr(id)` bärs som token
//   B3  `setPanels` PÅ PLATS mot ett adopterat barn: de befintliga fälten får
//       de nya värdena där de står, överskottet tas bort och det som saknas
//       läggs sist — appens barn står kvar mellan dem
//   B5  en `unadopt` av ett fält (barnet nås genom `children()`) lämnar inget
//       dinglande: antalet minskar och de andra fälten svarar
//   B6  fälten FÖLJER radens `enabled` (inredningens regel): en inaktiverad
//       rad släcker sina fält — också ett som läggs till efteråt med
//       `addPanel` eller `setPanels` — och tänder dem igen, medan appens eget
//       barn i raden inte rörs
// MED FÖNSTER (DOLT, `setOffscreen`)
//   B4  BREDDERNA i flexen: ett fast fält är sin bredd, ett AUTO-fält texten +
//       `statusPanelPadX` på var sida, och två fyllande delar det lediga
//       LIKA — summan är radens bredd
//   B7  ett FYLLANDE AUTO-fält har texten + luften som GOLV:
//       två fyllande fält med olika långa texter i en rad som
//       rymmer båda är var och ett minst sin text, det lediga delas lika
//       OVANPÅ textbredderna och summan är radens; och en ny text i ett
//       fyllande AUTO-fält lägger om grannarna (`relayoutSiblings`, räknad
//       genom förälderns `childLayoutChanged`)
//
// SABOTAGEN som ska fälla den: `Cell::flexGrow` som svarar 0 (B4: de
// fyllande blir 0 breda), `setPanels` som river alla och lägger sist (B3:
// ordningen mot det adopterade barnet), `setPanel` som behåller en utelämnad
// bredd (B2), `setPanelText` som också nollar bredden (B2), `Cell::contentSize`
// som ger ett fyllande AUTO-fält bredden 0 (B7: det långa fältet blir lika
// brett som det korta och kapas), `Cell::setText` som bara lägger om för ett
// icke-fyllande fält (B7: kroken räknar inget),
// `StatusBar::enabledChanged` borttagen (B6: fälten lyser i en inaktiverad
// rad), `addCell` utan släckningen (B6: det tillagda fältet lyser), och en
// krok som släcker ALLA barn (B6: appens etikett släcks).
//
// VAD DEN INTE MÄTER: hur fälten SER UT (skiljelinjen, ellipsen) — de ritas
// likadant i alla tre UI och ögnas i demons galleri; och `.glazeui`-vägen,
// som `preview_gate` (fallFas1B, fallCallsFas1B) och `roundtrip_gate` mäter.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/glaze.h>

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

void fallAddPanel()
{
    glaze::StatusBar sb;
    sb.addPanel("Klar", 0.0f, true);
    sb.addPanel("Rad 1", 110.0f);
    sb.addPanel();
    check(sb.panelCount() == 3, "B1: tre fält", std::to_string(sb.panelCount()));
    check(sb.panelText(0) == "Klar" && sb.panel(0).fill &&
              sb.panel(0).width == 0.0f,
          "B1: fält 0 — texten och fyllningen");
    check(sb.panelText(1) == "Rad 1" && sb.panel(1).width == 110.0f &&
              !sb.panel(1).fill,
          "B1: fält 1 — den fasta bredden");
    check(sb.panelText(2).empty() && sb.panel(2).width == 0.0f,
          "B1: fält 2 — tomt och AUTO");
    const glaze::StatusPanel ute = sb.panel(7);
    check(sb.panelText(3).empty() && sb.panelText(-1).empty() &&
              ute.text.str().empty() && ute.width == 0.0f && !ute.fill,
          "B1: utanför ⇒ tomt");
}

void fallSetPanel()
{
    glaze::StatusBar sb;
    sb.addPanel("A", 80.0f, true);
    sb.setPanel(0, "B");
    check(sb.panelText(0) == "B" && sb.panel(0).width == 0.0f &&
              !sb.panel(0).fill,
          "B2: setPanel är tilldelning — utelämnad bredd AUTO, fyllningen av");
    sb.setPanel(0, "C", 70.0f, true);
    sb.setPanelText(0, glaze::tr(3));
    check(sb.panel(0).text == glaze::tr(3) && sb.panel(0).width == 70.0f &&
              sb.panel(0).fill,
          "B2: setPanelText byter bara texten (som token)");
    sb.setPanel(5, "X");
    sb.setPanelText(-1, "Y");
    check(sb.panelCount() == 1 && sb.panel(0).text == glaze::tr(3),
          "B2: index utanför ⇒ no-op");
}

void fallSetPanels()
{
    glaze::StatusBar sb;
    glaze::Label     egen("egen");
    sb.addPanel("A");
    sb.adopt(egen);
    sb.addPanel("B");
    sb.addPanel("C");
    sb.setPanels({{"1"}, {"2", 40.0f}});
    check(sb.panelCount() == 2 && sb.panelText(0) == "1" &&
              sb.panelText(1) == "2" && sb.panel(1).width == 40.0f,
          "B3: setPanels — två fält kvar med de nya värdena",
          std::to_string(sb.panelCount()));
    check(sb.children().size() == 3 && sb.children()[1] == &egen,
          "B3: appens barn står kvar MELLAN fälten (på plats)",
          std::to_string(sb.children().size()));
    sb.setPanels({{"x"}, {"y"}, {"z", 0.0f, true}});
    check(sb.panelCount() == 3 && sb.children().size() == 4 &&
              sb.children()[1] == &egen && sb.panelText(2) == "z" &&
              sb.panel(2).fill,
          "B3: det som saknas läggs SIST");
    sb.setPanels({});
    check(sb.panelCount() == 0 && sb.children().size() == 1 &&
              sb.children()[0] == &egen,
          "B3: setPanels({}) tar bort alla fält och bara dem");
    sb.unadopt(egen);
}

void fallUnadopt()
{
    glaze::StatusBar sb;
    sb.addPanel("A");
    sb.addPanel("B");
    sb.addPanel("C");
    // Fältet nås som en vanlig widget — typen är intern, men barnet är ett
    // barn, och borttagningen är trädets.
    glaze::Widget* mitten = sb.children()[1];
    check(sb.unadopt(*mitten), "B5: ett fält går att ta bort som ett barn");
    check(sb.panelCount() == 2 && sb.panelText(0) == "A" &&
              sb.panelText(1) == "C",
          "B5: de andra fälten svarar efteråt");
    sb.setPanelText(1, "D");
    check(sb.panelText(1) == "D", "B5: och går att skriva i");
}

void fallEnabled()
{
    glaze::StatusBar sb;
    glaze::Label     egen("egen");
    sb.addPanel("A");
    sb.adopt(egen);
    sb.disable();
    sb.addPanel("B");
    sb.setPanels({{"x"}, {"y"}, {"z"}});   // A och B på plats, z sist
    const std::vector<glaze::Widget*> b = sb.children();   // x, egen, y, z
    bool släckta = b.size() == 4;
    for (std::size_t i = 0; släckta && i < b.size(); ++i)
        if (b[i] != &egen)
            släckta = !b[i]->isEnabled();
    check(släckta,
          "B6: en inaktiverad rad släcker sina fält — också de som läggs till "
          "efteråt", std::to_string(b.size()));
    check(egen.isEnabled(),
          "B6: appens eget barn i raden rörs inte (enabled kaskadar inte)");
    sb.enable();
    bool tända = b.size() == 4;
    for (std::size_t i = 0; tända && i < b.size(); ++i)
        tända = b[i]->isEnabled();
    check(tända, "B6: en aktiverad rad tänder dem igen");
    sb.unadopt(egen);
}

// ------------------------------------------------------------------ B4

// B7: en fristående statusrad som RÄKNAR sina barns `relayoutSiblings`
// (förälderns krok `childLayoutChanged`, widget.h) — kroken körs bara i ett
// öppet fönster, därför står raden här och inte headless.
struct Raknare : glaze::StatusBar {
    int omlagda = 0;

protected:
    void childLayoutChanged(glaze::Widget&) override { ++omlagda; }
};

const char* const kLang = "En längre statustext";

struct Vard : glaze::AppWindow {
    Raknare fyll;   // B7 — en remsa överst

    Vard() : glaze::AppWindow("statusbar_gate", 500, 200)
    {
        statusBar.show();
        statusBar.addPanel("Fast", 100.0f);
        statusBar.addPanel("Abc");
        statusBar.addPanel("", 0.0f, true);
        statusBar.addPanel("", 0.0f, true);

        fyll.name = "fyll";
        fyll.dock = glaze::Dock::Top;
        fyll.addPanel(kLang, 0.0f, true);
        fyll.addPanel("Ab", 0.0f, true);
        root().adopt(fyll);
    }
};

bool nära(float a, float b)
{
    return std::fabs(a - b) < 1.01f;   // en fysisk pixels avrundning
}

void fallBredder()
{
    Vard w;
    w.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        std::vector<gfx::Rectf> r;
        float                   skala = 1.0f, luft = 0.0f, abc = 0.0f;
        for (glaze::Widget* b : w.statusBar.children()) {
            const glaze::Ctx c = b->behaviorCtx();
            skala              = c.scale;
            luft               = c.style->statusPanelPadX;
            abc                = glaze::Draw::measure(c, "Abc").w;
            r.push_back(b->resolve(c));
        }
        check(r.size() == 4, "B4: fyra fält i raden",
              std::to_string(r.size()));
        if (r.size() == 4) {
            // Radens INNERYTA — den yta flexen fördelar (barens `childArea`,
            // publik på `Widget`: overriden får vara protected).
            const glaze::Ctx     sc  = w.statusBar.behaviorCtx();
            const glaze::Widget& bas = w.statusBar;
            const gfx::Rectf     rad = bas.childArea(sc);
            check(nära(r[0].w / skala, 100.0f), "B4: det fasta fältet är 100",
                  std::to_string(r[0].w / skala));
            check(nära(r[1].w / skala, abc + 2.0f * luft),
                  "B4: AUTO-fältet är texten + luften på var sida",
                  std::to_string(r[1].w / skala));
            check(nära(r[2].w, r[3].w) && r[2].w > 0.0f,
                  "B4: de två fyllande delar lika",
                  std::to_string(r[2].w) + " mot " + std::to_string(r[3].w));
            const float summa = r[0].w + r[1].w + r[2].w + r[3].w;
            check(nära(summa / skala, rad.w / skala),
                  "B4: summan är radens bredd",
                  std::to_string(summa) + " mot " + std::to_string(rad.w));
        }

        // B7 — de fyllande fältens golv och omläggningen.
        std::vector<gfx::Rectf> f;
        for (glaze::Widget* b : w.fyll.children())
            f.push_back(b->resolve(b->behaviorCtx()));
        check(f.size() == 2, "B7: två fyllande fält i raden",
              std::to_string(f.size()));
        if (f.size() == 2) {
            const glaze::Ctx     fc  = w.fyll.behaviorCtx();
            const glaze::Widget& fb  = w.fyll;
            const gfx::Rectf     rad = fb.childArea(fc);
            const float t0 = glaze::Draw::measure(fc, kLang).w + 2.0f * luft;
            const float t1 = glaze::Draw::measure(fc, "Ab").w + 2.0f * luft;
            check(t0 > t1 && (f[0].w + f[1].w) / skala > t0 + t1,
                  "B7: förutsättningen — olika långa texter, raden rymmer båda",
                  std::to_string(t0) + " + " + std::to_string(t1));
            check(f[0].w / skala >= t0 - 1.01f && f[1].w / skala >= t1 - 1.01f,
                  "B7: varje fyllande AUTO-fält är minst sin text + luft",
                  std::to_string(f[0].w / skala) + " mot " + std::to_string(t0));
            // Två avrundade rektar: skillnaden får vara av med en fysisk px
            // per sida.
            check(std::fabs((f[0].w - f[1].w) / skala - (t0 - t1)) < 2.02f,
                  "B7: det lediga delas LIKA ovanpå textbredderna",
                  std::to_string((f[0].w - f[1].w) / skala) + " mot " +
                      std::to_string(t0 - t1));
            check(nära((f[0].w + f[1].w) / skala, rad.w / skala),
                  "B7: summan är radens bredd",
                  std::to_string(f[0].w + f[1].w) + " mot " +
                      std::to_string(rad.w));
        }
        const int före = w.fyll.omlagda;
        w.fyll.setPanelText(1, "Abcdefgh");
        check(w.fyll.omlagda > före,
              "B7: en ny text i ett fyllande AUTO-fält lägger om grannarna",
              std::to_string(w.fyll.omlagda - före));
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   B4 hoppades över: inget fönster kunde skapas "
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

    std::printf("statusbar_gate\n");
    fallAddPanel();
    fallSetPanel();
    fallSetPanels();
    fallUnadopt();
    fallEnabled();
    fallBredder();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
