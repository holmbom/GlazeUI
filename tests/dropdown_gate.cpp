// dropdown_gate — DROPDOWNENS regressionsgrind.
//
// Kör:  ./build/tests/dropdown_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// VARFÖR den finns:
// dropdownens popup-lista är en SUBKLASS av `glaze::ListBox` med en
// tillståndsmodell där Dropdown äger det COMMITTADE valet och popup-listan
// det PROVISORISKA. Det är dropdownens största regressionsrisk, och en
// manuell kontroll efteråt är för svag — listan här ÄR bevarandelistan,
// gjord körbar.
//
// Grinden är TVÅDELAD:
//
//   HEADLESS   allt som inte kräver ett fönster: setProp-vokabulären,
//              kontraktet programmatisk-vs-användare (regel 6), hela
//              det STÄNGDA lägets tangentbord och JUSTERINGEN —
//              fältets text Left/Center/Right i alla tre familjerna, mätt
//              som textens x-läge i en inspelande renderer mot förväntade värden ur
//              stilens mått (sabotaget "align_ ignoreras" fäller den).
//   MED FÖNSTER  ett DOLT ägarfönster (`setOffscreen(true)` —
//              som i window_gate) räcker för att faktiskt ÖPPNA
//              popupen: öppna/stäng, `dropMaxRows`-kapningen, det ÖPPNA
//              lägets tangentbord, Escape utan ändring, commit exakt en
//              gång och `setItems` under öppen popup. Saknas display
//              hoppas de fallen över med en SKRIVEN rad i stället för att
//              fälla grinden.
//
// De TVÅ tekniska knepen, båda utan en enda ändring i produktionskoden:
//   * `keyDown`/`mouseDown` är protected på `Widget`. En testsubklass med
//     `using` gör dem anropbara — det är precis vad en subklass får.
//   * det PROVISORISKA valet i öppen popup läses via EN lokal hjälpare
//     (`prov`), så ombyggnaden bara rör den raden och aldrig ett enda
//     testfall.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int g_skips  = 0;   // överhoppade block — SYNLIGA i sammanfattningen
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

// Protected-krokarna anropbara — en subklass får det, och testet slipper
// därför fejka event-routingen i AppWindow.
struct Prov : glaze::Dropdown {
    using glaze::Dropdown::keyDown;
    using glaze::Dropdown::mouseDown;
};

// DET PROVISORISKA VALET i en öppen popup — den ENDA rad en ombyggnad av
// popupen rör. Modellen är att Dropdown äger det COMMITTADE valet
// och popup-listan det PROVISORISKA.
int prov(const glaze::Dropdown& d) { return d.provisionalRow(); }

// ------------------------------------------------------------- HEADLESS

// setProp-vokabulären. `selectedIndex` är den ENDA formen.
// Fältet heter `selectedIndex` i `kSetterFields`, så `setProp` måste
// känna igen det namnet — annars blir
// `typDd.setSelectedIndex(1);` en opak rad i previewn.
void fallSetProp()
{
    glaze::Dropdown d;
    check(d.setProp("items", "Alfa;Beta;Gamma"), "setProp: items känns igen");
    check(d.items().size() == 3 && d.items()[2] == "Gamma",
          "setProp: items splittas på ';'",
          std::to_string(d.items().size()));
    check(d.setProp("selectedIndex", "1"), "setProp: selectedIndex känns igen");
    check(d.selectedIndex() == 1, "setProp: selectedIndex sätts",
          std::to_string(d.selectedIndex()));
    check(!d.setProp("selected", "2"),
          "setProp: 'selected' är BORTTAGET (arv, ej alias)");
    check(d.selectedIndex() == 1, "setProp: den borttagna nyckeln rör inget",
          std::to_string(d.selectedIndex()));
}

// Regel 6: PROGRAMMATISK sättning avfyrar ALDRIG onChange.
void fallProgrammatisk()
{
    int      n = 0;
    glaze::Dropdown d;
    d.onChange = [&n] { ++n; };
    d.setItems({"a", "b", "c"});
    d.setSelectedIndex(2);
    check(n == 0, "programmatisk setSelectedIndex fyrar INTE onChange",
          std::to_string(n));
    check(d.selectedText() == "c", "selectedText: valt värde", d.selectedText());
}

// selectedText vaktar intervallet i BÅDA ändar — listan kan krympa under
// fötterna på ett satt index.
void fallSelectedText()
{
    glaze::Dropdown d;
    check(d.selectedText().empty(), "selectedText: tom lista ⇒ tom sträng");
    d.setItems({"a", "b"});
    d.setSelectedIndex(7);
    check(d.selectedText().empty(), "selectedText: index utanför ⇒ tom sträng",
          d.selectedText());
    d.setSelectedIndex(-1);
    check(d.selectedText().empty(), "selectedText: -1 ⇒ tom sträng");
}

// STÄNGT LÄGE: pilarna byter val DIREKT (WinForms) och fyrar onChange en
// gång per faktisk ändring — vid kanten händer ingenting alls.
void fallStangdaTangenter()
{
    int  n = 0;
    Prov d;
    d.onChange = [&n] { ++n; };
    d.setItems({"a", "b", "c"});

    check(d.keyDown(gfx::Key::Down, 0), "stängd: Down konsumeras");
    check(d.selectedIndex() == 0 && n == 1,
          "stängd: Down från -1 väljer första + EXAKT ETT onChange",
          std::to_string(d.selectedIndex()) + "/" + std::to_string(n));
    d.keyDown(gfx::Key::Down, 0);
    check(d.selectedIndex() == 1 && n == 2, "stängd: Down stegar framåt",
          std::to_string(d.selectedIndex()) + "/" + std::to_string(n));
    d.keyDown(gfx::Key::Up, 0);
    check(d.selectedIndex() == 0 && n == 3, "stängd: Up stegar bakåt",
          std::to_string(d.selectedIndex()) + "/" + std::to_string(n));
    d.keyDown(gfx::Key::Up, 0);
    check(d.selectedIndex() == 0 && n == 3,
          "stängd: Up vid kanten ändrar inget och fyrar inget",
          std::to_string(d.selectedIndex()) + "/" + std::to_string(n));
    d.keyDown(gfx::Key::Down, 0);
    d.keyDown(gfx::Key::Down, 0);
    d.keyDown(gfx::Key::Down, 0);
    check(d.selectedIndex() == 2 && n == 5,
          "stängd: Down vid sista kanten fyrar inte igen",
          std::to_string(d.selectedIndex()) + "/" + std::to_string(n));

    check(!d.keyDown(gfx::Key::A, 0), "stängd: okänd tangent bubblar (false)");
    check(!d.keyDown(gfx::Key::Home, 0),
          "stängd: Home hör till ÖPPET läge och bubblar");
}

// JUSTERINGEN (datagridens val-editor står i kolumnens
// justering). Mätt som TEXTENS PLATS mot en inspelande renderer, i alla tre
// familjerna, mot ett förväntat värde ur kontraktet: textytan börjar `fieldPadX` från
// vänsterkanten och slutar `fieldPadX` före pilens ruta (`fieldPadX` från
// högerkanten, 2 × `chevronW` bred). Fonten har ett fast teckensteg.
class StegFont final : public gfx::NullFont {
public:
    gfx::Sizef  measure(const std::string& s) const override
    {
        return gfx::Sizef{8.0f * float(s.size()), 16.0f};
    }
    float       height() const override { return 16.0f; }
    bool        valid() const override { return true; }
    float       prefixWidth(const std::string&, std::size_t b) const override
    {
        return 8.0f * float(b);
    }
    std::size_t byteIndexAtX(const std::string&, float) const override
    {
        return 0;
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        return std::min(std::size_t(std::max(0.0f, maxW) / 8.0f), s.size());
    }
};

struct TextInspelning : gfx::NullRenderer {
    std::string s;
    float       x = -1.0f;
    float drawText(gfx::Font&, float px, float, const std::string& t,
                   gfx::Color) override
    {
        s = t;
        x = px;
        return 0.0f;
    }
};

struct Ritbar : glaze::Dropdown {
    using glaze::Dropdown::draw;
};

void fallJusteringen()
{
    StegFont       f;
    TextInspelning rec;
    Ritbar         d;
    d.width  = 160;
    d.height = 26;
    d.setItems({"abc"});
    d.setSelectedIndex(0);
    check(d.align() == glaze::Align::Left, "justering: Left är standard");
    check(d.setProp("align", "right") && d.align() == glaze::Align::Right,
          "justering: setProp(\"align\") känns igen");
    check(d.setProp("align", "kanske") && d.align() == glaze::Align::Right,
          "justering: ett okänt värde loggas och lämnar den orörd");
    const std::pair<glaze::UI, const char*> familjer[] = {
        {glaze::UI::Slate, "Slate"},
        {glaze::UI::Glaze, "Glaze"},
        {glaze::UI::Minimalistic, "Minimalistic"}};
    for (const auto& [ui, namn] : familjer) {
        const glaze::Style& st = glaze::defaultStyle(ui);
        glaze::Ctx          c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = ui;
        c.style = &st;
        c.theme = &glaze::defaultTheme(ui).light;
        c.area  = {0, 0, 400, 100};
        c.box   = d.resolve(c);
        const float x0  = st.fieldPadX;
        const float x1  = 160.0f - st.fieldPadX - 2.0f * st.chevronW -
                          st.fieldPadX;
        const float tw  = 24.0f;   // "abc"
        const struct {
            glaze::Align a;
            float        facit;
            const char*  vad;
        } fall[] = {{glaze::Align::Left, x0, "Left"},
                    {glaze::Align::Center, x0 + (x1 - x0 - tw) / 2.0f,
                     "Center"},
                    {glaze::Align::Right, x1 - tw, "Right"}};
        for (const auto& fl : fall) {
            d.setAlign(fl.a);
            rec.x = -1.0f;
            d.draw(c);
            check(rec.s == "abc" && std::abs(rec.x - fl.facit) < 0.01f,
                  std::string("justering: fältets text ") + fl.vad + " (" +
                      namn + ")",
                  std::to_string(rec.x) + " mot " + std::to_string(fl.facit));
        }
    }
}

// `open()` är NO-OP utan ägarfönster och utan items — och `close()` på en
// stängd dropdown är no-op. Fail-safen som håller `open_` sann utan popup
// får aldrig triggas här.
void fallOppnaUtanFonster()
{
    Prov d;
    d.open();
    check(!d.isOpen(), "open(): utan items OCH utan fönster = no-op");
    d.setItems({"a"});
    d.open();
    check(!d.isOpen(), "open(): utan ägarfönster = no-op");
    d.close();
    check(!d.isOpen(), "close(): på stängd dropdown = no-op");
    check(d.keyDown(gfx::Key::Space, 0),
          "stängd: Space konsumeras (öppningsförsöket)");
    check(!d.isOpen(), "stängd: Space utan fönster öppnar ingenting");
    // ENTER ÄR INTE DEN STÄNGDA LISTANS: den bubblar
    // till fönstrets standardknapp. dialog_gate mäter vägen genom fönstret.
    check(!d.keyDown(gfx::Key::Return, 0) && !d.isOpen(),
          "stängd: Return tas inte (bubblar till standardknappen)");
}

// --------------------------------------------------------- MED FÖNSTER
//
// STÄNGNINGEN ÄR UPPSKJUTEN (App verkställer i pump-svansen), så ett test
// som bara anropar `close()` och läser `isOpen()` mäter ingenting. Därför
// körs fönsterfallen som en KEDJA AV POSTADE STEG: varje steg gör sitt och
// postar nästa, och `App::post` körs i pump-svansen — alltså efter att
// föregående varvs `flushPendingCloses()` faktiskt rivit popupen. Det är
// samma mekanik `post_gate` vilar på, använd som en klocka.

struct Vard : glaze::AppWindow {
    Prov dd;
    Vard() : glaze::AppWindow("dropdown_gate", 400, 300)
    {
        dd.x     = 10;
        dd.y     = 10;
        dd.width = 160;
        root().adopt(dd);
    }
    // Dropdown::mouseDown läser aldrig sitt Ctx — men signaturen kräver
    // ett, och `makeCtx` är protected på fönstret.
    using glaze::AppWindow::makeCtx;
};

// Popup-fönstret är REGISTRERAT i App:n som varje annat fönster — det är
// så testet når dess klientstorlek utan att `list_` behöver bli publik.
glaze::AppWindow* popupen(const glaze::AppWindow& agare)
{
    for (glaze::AppWindow* w : glaze::app().windows())
        if (w != &agare)
            return w;
    return nullptr;
}

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);   // som window_gate: dolt men RIKTIGT

    // Tolv rader mot stilens dropMaxRows (10) — kapningen ska synas i
    // popupens KLIENTHÖJD, och resten hjulscrollas.
    w.dd.setItems({"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
                   "r10", "r11"});
    w.dd.setSelectedIndex(3);

    int n = 0;
    w.dd.onChange = [&n] { ++n; };

    // ANROPA ALDRIG show() HÄR. `run()` öppnar fönstret SJÄLV (runInternal →
    // showOwned), och ett redan öppet fönster får run() att logga "fönstret är
    // redan öppet" och returnera OMEDELBART — då körs inte en enda postad
    // funktion, medan testet ändå rapporterar allt grönt. Det var precis vad
    // som hände: hela den asynkrona halvan av den här grinden mätte
    // INGENTING, och ett test som ljuger är sämre än inget test.
    // `kedjanKordes` nedan är vakten som gör att det aldrig kan hända tyst
    // igen.
    bool kedjanKordes = false;

    int steg = 0;
    std::function<void()> nasta;
    nasta = [&] {
        switch (steg++) {
        case 0:
            kedjanKordes = true;
            w.dd.open();
            if (!w.dd.isOpen()) {
                std::printf("  --   popup-fallen hoppas över: popup-fönstret "
                            "kom aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            check(true, "open(): popupen kom upp");
            check(prov(w.dd) == 3,
                  "open(): det PROVISORISKA valet startar på det committade",
                  std::to_string(prov(w.dd)));
            check(n == 0, "open(): öppnandet fyrar inget onChange",
                  std::to_string(n));

            if (glaze::AppWindow* p = popupen(w)) {
                const int rader  = w.style().dropMaxRows;
                const int vantad =
                    int(float(rader) * w.style().listRowH + 0.5f);
                check(int(p->clientSize().h) == vantad,
                      "open(): listan kapas vid dropMaxRows (" +
                          std::to_string(rader) + " rader)",
                      std::to_string(int(p->clientSize().h)) + " mot " +
                          std::to_string(vantad));
            } else {
                check(false, "open(): popup-fönstret finns i App-registret");
            }

            // ÖPPET LÄGE: pilarna flyttar MARKÖREN, inte valet — utan event.
            w.dd.keyDown(gfx::Key::Down, 0);
            check(prov(w.dd) == 4 && w.dd.selectedIndex() == 3 && n == 0,
                  "öppen: Down flyttar markören men INTE valet, utan event",
                  std::to_string(prov(w.dd)) + "/" +
                      std::to_string(w.dd.selectedIndex()) + "/" +
                      std::to_string(n));
            w.dd.keyDown(gfx::Key::Home, 0);
            check(prov(w.dd) == 0, "öppen: Home går till första raden",
                  std::to_string(prov(w.dd)));
            w.dd.keyDown(gfx::Key::End, 0);
            check(prov(w.dd) == 11, "öppen: End går till sista raden",
                  std::to_string(prov(w.dd)));
            w.dd.keyDown(gfx::Key::Up, 0);
            check(prov(w.dd) == 10 && w.dd.selectedIndex() == 3,
                  "öppen: Up flyttar markören", std::to_string(prov(w.dd)));

            // ESCAPE kastar det provisoriska värdet.
            w.dd.keyDown(gfx::Key::Escape, 0);
            break;
        case 1:
            check(!w.dd.isOpen(), "öppen: Escape stänger popupen");
            check(w.dd.selectedIndex() == 3 && n == 0,
                  "öppen: Escape lämnar valet ORÖRT och fyrar inget",
                  std::to_string(w.dd.selectedIndex()) + "/" +
                      std::to_string(n));
            w.dd.open();
            check(prov(w.dd) == 3,
                  "ny öppning: markören startar om på det committade valet",
                  std::to_string(prov(w.dd)));
            // COMMIT: Enter på markören sätter valet och fyrar EXAKT en gång.
            w.dd.keyDown(gfx::Key::Down, 0);
            w.dd.keyDown(gfx::Key::Down, 0);
            w.dd.keyDown(gfx::Key::Return, 0);
            check(w.dd.selectedIndex() == 5 && n == 1,
                  "öppen: Enter committar markören + EXAKT ETT onChange",
                  std::to_string(w.dd.selectedIndex()) + "/" +
                      std::to_string(n));
            break;
        case 2:
            check(!w.dd.isOpen(), "commit stänger popupen");
            // Commit till SAMMA värde fyrar inte (bara faktisk ändring).
            w.dd.open();
            w.dd.keyDown(gfx::Key::Return, 0);
            check(w.dd.selectedIndex() == 5 && n == 1,
                  "öppen: Enter på oförändrat val fyrar inget",
                  std::to_string(n));
            break;
        case 3:
            // TOGGLE: ett klick medan listan är öppen STÄNGER den.
            w.dd.open();
            check(w.dd.isOpen(), "toggle: listan är öppen före klicket");
            w.dd.mouseDown(w.makeCtx(), 0.0f, 0.0f);
            break;
        case 4:
            check(!w.dd.isOpen(),
                  "toggle: klick medan öppen stänger (ingen återöppning)");
            check(glaze::app().windowCount() == 1,
                  "toggle: ingen andra popup skapades",
                  std::to_string(glaze::app().windowCount()));
            // setItems UNDER öppen popup STÄNGER den.
            w.dd.open();
            check(w.dd.isOpen(), "setItems: listan är öppen före bytet");
            w.dd.setItems({"x", "y"});
            break;
        case 5:
            check(!w.dd.isOpen(), "setItems under öppen popup STÄNGER den");
            w.close();
            return;   // ingen ny post — run() returnerar
        default:
            w.close();
            return;
        }
        glaze::app().post(nasta);
    };
    glaze::app().post(nasta);
    w.run();

    // VAKTEN: utan den kan hela den asynkrona halvan tystna igen och grinden
    // ändå rapportera allt grönt.
    if (!kedjanKordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("dropdown_gate\n");
    fallSetProp();
    fallProgrammatisk();
    fallSelectedText();
    fallStangdaTangenter();
    fallJusteringen();
    fallOppnaUtanFonster();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
