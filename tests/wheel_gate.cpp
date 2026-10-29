// wheel_gate — RULLNINGENS RIKTNING: appens,
// fönstrets och widgetens vändning av hjulet, Shift+mushjul i sidled, och att
// ritytan får det RÅA hjulet — genom FÖNSTRETS ingång (`handleEvent`) i ett
// DOLT fönster med riktiga widgets.
//
// Kör:  ./build/tests/wheel_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönstret kunde inte
//       skapas (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// VAD GRINDEN PÅSTÅR (det som mäts är riktningen widgeten rör sig — scrollen,
// värdet, zoomen — aldrig dispatchens räkning):
//   G   `App::setWheelInvert(Mouse, true, false)` vänder Panel, ListBox,
//       TextArea, Scrollbar, Slider (med fokus) och SpinBox (med fokus); i
//       sidled vänds inte (reglaget får ett sidohjul).
//   W   per widget: en lista med egen inställning i en panel med motsatt, ett
//       barn utan inställning ärver närmaste förfaders, `clearWheelInvert`
//       ärver igen, och fönstrets `setWheelInvert` gäller allt utan egen.
//   S   Shift+mushjul rullar i SIDLED åt samma håll som Windows (hjulet ned =
//       höger); ett event som redan bär sidled (ett sidohjul, macOS-formen)
//       byts inte tillbaka; Shift + styrplattan byts aldrig.
//   C   ritytan får det RÅA hjulet med appens vändning satt (zoomar in), och
//       en rityta som inte tar hjulet låter panelen rulla med PANELENS
//       vändning (räknad per widget, inte en gång för kedjan).
//   K   den fångade widgeten (explicit capture) får sin vändning.
//   P   arvet STANNAR vid fönstrets rot: en lista i ett annat fönster (en
//       popup) ärver appens, inte det första fönstrets.
//
// VAD DEN INTE MÄTER: riktiga hjul och styrplattor och OS:ens naturliga
// rullning (fasaden matas syntetiskt — doc/os/*/TESTS.md); macOS-backendens
// om-byte av Shift+mushjul; Table/ListView, TreeView,
// menyernas och rullgardinens listor (samma dispatch, samma `wheelScrollPx`
// som ListBox — inte var för sig); en riktig rullgardins popup (P mäter
// regeln med ett eget fönster).
//
// SABOTAGEN som ska fälla den: vändningen räknad en gång
// för kedjan (C, W), `followsWheelDirection` saknas på ritytan (C), Shift-
// bytet utan `wheelX == 0`-villkoret (S), bytet också för styrplattan (S),
// arvet som fortsätter förbi fönstrets rot till ägaren (P), den fångade
// widgeten utan `wheelFor` (K).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/glaze.h>

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string tal(double v)
{
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

using glaze::InputDevice;

// En lista som kan ta explicit capture (K) — kroken är skyddad.
struct FångadLista : glaze::ListBox {
    using glaze::ListBox::releasePointerCapture;
    using glaze::ListBox::setPointerCapture;
};

std::vector<std::string> rader()
{
    std::vector<std::string> v;
    for (int i = 0; i < 30; ++i)
        v.push_back("Rad " + std::to_string(i));
    return v;
}

struct Värd : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Panel     panel, inre, bred;
    glaze::Label     fyll, bredFyll;
    glaze::ListBox   lista, lista2;
    glaze::TextArea  text;
    glaze::Scrollbar rull;
    glaze::Slider    reglage;
    glaze::SpinBox   talfält;
    glaze::Canvas    yta;
    FångadLista      fångad;

    Värd() : glaze::AppWindow("wheel_gate", 700, 520, gfx::Chrome::None)
    {
        const auto ställ = [](glaze::Widget& w, const char* n, float x, float y,
                              float bw, float h) {
            w.name   = n;
            w.x      = x;
            w.y      = y;
            w.width  = bw;
            w.height = h;
        };
        ställ(panel, "panel", 10, 10, 300, 480);
        root().adopt(panel);
        ställ(lista, "lista", 10, 110, 200, 100);   // synlig med panelen rullad 100
        lista.setItems(rader());
        panel.adopt(lista);
        ställ(inre, "inre", 10, 230, 200, 120);
        panel.adopt(inre);
        ställ(lista2, "lista2", 10, 10, 180, 100);
        lista2.setItems(rader());
        inre.adopt(lista2);
        ställ(yta, "yta", 10, 360, 200, 100);
        yta.setBorder(false);
        panel.adopt(yta);
        fyll.name = "fyll";
        fyll.x    = 10;
        fyll.y    = 1200;
        fyll.setText("fyll");
        panel.adopt(fyll);
        ställ(bred, "bred", 330, 10, 200, 100);
        root().adopt(bred);
        bredFyll.name = "bredFyll";
        bredFyll.x    = 900;
        bredFyll.y    = 10;
        bredFyll.setText("bred");
        bred.adopt(bredFyll);
        ställ(text, "text", 330, 120, 200, 100);
        std::string t;
        for (int i = 0; i < 40; ++i)
            t += "rad " + std::to_string(i) + "\n";
        text.setText(t);
        root().adopt(text);
        ställ(rull, "rull", 550, 10, 16, 200);
        rull.setOrientation(glaze::Scrollbar::Orientation::Vertical);
        rull.setMin(0);
        rull.setMax(100);
        rull.setPage(10);
        root().adopt(rull);
        ställ(reglage, "reglage", 330, 240, 200, 24);
        reglage.setMin(0);
        reglage.setMax(100);
        reglage.setStep(1);
        root().adopt(reglage);
        ställ(talfält, "talfält", 330, 280, 120, 26);
        talfält.setMin(0);
        talfält.setMax(100);
        talfält.setStep(1);
        root().adopt(talfält);
        ställ(fångad, "fångad", 330, 330, 200, 80);
        fångad.setItems(rader());
        root().adopt(fångad);
    }

    gfx::Pointf fys(glaze::Widget& w, double lx, double ly)
    {
        const gfx::Rectf b = w.resolve(w.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{static_cast<float>(b.x + lx * s),
                           static_cast<float>(b.y + ly * s)};
    }
    void hjul(gfx::Pointf p, float wx, float wy, bool precise = false,
              std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelX   = wx;
        e.wheelY   = wy;
        e.precise  = precise;
        e.phase    = precise ? gfx::GesturePhase::Update : gfx::GesturePhase::None;
        e.mods     = mods;
        handleEvent(e);
    }
    void rita() { window().renderFrame(); }

    // Varje fall börjar från samma läge: inga vändningar någonstans, allt
    // rullat till mitten eller starten.
    void nollställ()
    {
        for (const InputDevice dv : {InputDevice::Mouse, InputDevice::Touchpad}) {
            glaze::app().setWheelInvert(dv, false, false);
            clearWheelInvert(dv);
            for (glaze::Widget* w : std::initializer_list<glaze::Widget*>{
                     &panel, &inre, &bred, &lista, &lista2, &text, &rull, &reglage,
                     &talfält, &yta, &fångad})
                w->clearWheelInvert(dv);
        }
        panel.setScroll(0.0f, 100.0f);
        bred.setScroll(100.0f, 0.0f);
        for (glaze::Panel* p : std::initializer_list<glaze::Panel*>{&lista, &lista2, &text,
                                                                     &fångad})
            p->setScroll(0.0f, 60.0f);
        rull.setValue(50);
        reglage.setValue(50);
        talfält.setValue(50);
        yta.setZoomable(false);
        yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f});
        invalidate();
        rita();
    }

    void kör();
};

void Värd::kör()
{
    // ---- G. APPENS VÄNDNING över varje rullande widget
    // `upp` är hjulet upp (+1): utan vändning rullar det mot början och
    // reglagen uppåt i värde; med vändning åt andra hållet.
    const auto rörelse = [&](bool vänd) {
        nollställ();
        glaze::app().setWheelInvert(InputDevice::Mouse, vänd, false);
        hjul(fys(panel, 250, 50), 0.0f, 1.0f);
        hjul(fys(lista, 50, 50), 0.0f, 1.0f);
        hjul(fys(text, 50, 50), 0.0f, 1.0f);
        hjul(fys(rull, 8, 100), 0.0f, 1.0f);
        reglage.focus();
        hjul(fys(reglage, 100, 12), 0.0f, 1.0f);
        talfält.focus();
        hjul(fys(talfält, 30, 13), 0.0f, 1.0f);
        return std::vector<double>{panel.scrollY() - 100.0, lista.scrollY() - 60.0,
                                   text.scrollY() - 60.0,   rull.value() - 50.0,
                                   reglage.value() - 50.0,  talfält.value() - 50.0};
    };
    const std::vector<double> rak  = rörelse(false);
    const std::vector<double> vänd = rörelse(true);
    static const char* const kNamn[] = {"Panel", "ListBox", "TextArea",
                                        "Scrollbar", "Slider", "SpinBox"};
    for (std::size_t i = 0; i < rak.size(); ++i)
        check(rak[i] != 0.0 && vänd[i] != 0.0 && (rak[i] > 0.0) != (vänd[i] > 0.0),
              std::string("G ") + kNamn[i] +
                  ": appens vändning av musens hjul rullar åt andra hållet",
              tal(rak[i]) + " mot " + tal(vänd[i]));
    nollställ();
    glaze::app().setWheelInvert(InputDevice::Mouse, true, false);
    reglage.focus();
    hjul(fys(reglage, 100, 12), 1.0f, 0.0f);
    check(reglage.value() > 50.0f,
          "G i sidled vänds inte: reglagets sidohjul går uppåt", tal(reglage.value()));
    nollställ();
    glaze::app().setWheelInvert(InputDevice::Touchpad, true, false);
    hjul(fys(panel, 250, 50), 0.0f, 1.0f);
    check(panel.scrollY() < 100.0f,
          "G styrplattans vändning rör inte mushjulet", tal(panel.scrollY()));

    // ---- W. PER WIDGET, MED ARV
    nollställ();
    glaze::app().setWheelInvert(InputDevice::Mouse, true, false);
    panel.setWheelInvert(InputDevice::Mouse, false, false);
    lista.setWheelInvert(InputDevice::Mouse, true, false);
    inre.setWheelInvert(InputDevice::Mouse, true, false);
    hjul(fys(panel, 250, 50), 0.0f, 1.0f);
    hjul(fys(lista, 50, 50), 0.0f, 1.0f);
    hjul(fys(lista2, 50, 50), 0.0f, 1.0f);
    const bool panelRak = panel.scrollY() < 100.0f, listaVänd = lista.scrollY() > 60.0f,
               ärvd = lista2.scrollY() > 60.0f;
    inre.clearWheelInvert(InputDevice::Mouse);
    lista2.setScroll(0.0f, 60.0f);
    hjul(fys(lista2, 50, 50), 0.0f, 1.0f);
    check(panelRak && listaVänd && ärvd && lista2.scrollY() < 60.0f,
          "W listan med egen vändning i en panel med motsatt, ett barn ärver "
          "närmaste förfaders, och clear ärver igen (panelens)",
          tal(panel.scrollY()) + " / " + tal(lista.scrollY()) + " / " +
              tal(lista2.scrollY()));
    nollställ();
    setWheelInvert(InputDevice::Mouse, true, false);
    hjul(fys(panel, 250, 50), 0.0f, 1.0f);
    check(panel.scrollY() > 100.0f && lista.isWheelInverted(InputDevice::Mouse, false) &&
              !glaze::app().wheelInverted(InputDevice::Mouse, false),
          "W fönstrets setWheelInvert gäller allt utan egen", tal(panel.scrollY()));

    // ---- S. SHIFT + MUSHJUL I SIDLED
    nollställ();
    hjul(fys(bred, 100, 50), 0.0f, -1.0f, false, gfx::kModShift);
    const bool höger = bred.scrollX() > 100.0f && bred.scrollY() == 0.0f;
    bred.setScroll(100.0f, 0.0f);
    hjul(fys(bred, 100, 50), -1.0f, 0.0f, false, gfx::kModShift);
    check(höger && bred.scrollX() > 100.0f,
          "S Shift+hjul NED rullar åt HÖGER (Windows riktning), och ett event som "
          "redan bär sidled byts inte tillbaka",
          tal(bred.scrollX()));
    hjul(fys(panel, 250, 50), 0.0f, -1.0f, true, gfx::kModShift);
    check(panel.scrollY() > 100.0f && panel.scrollX() == 0.0f,
          "S Shift + styrplattan byts aldrig", tal(panel.scrollY()));

    // ---- C. RITYTAN
    nollställ();
    glaze::app().setWheelInvert(InputDevice::Mouse, true, false);
    yta.setZoomable(true);
    const double z0 = yta.zoom();
    hjul(fys(yta, 100, 50), 0.0f, 1.0f);
    const bool rå = std::abs(yta.zoom() - z0 * std::sqrt(2.0)) < 1e-9;
    yta.setZoomable(false);
    hjul(fys(yta, 100, 50), 0.0f, 1.0f);
    check(rå && panel.scrollY() > 100.0f,
          "C ritytan får det RÅA hjulet (zoomar in), och utan zoom rullar panelen "
          "med SIN ärvda vändning",
          tal(yta.zoom() / z0) + " / " + tal(panel.scrollY()));

    // ---- K. DEN FÅNGADE
    nollställ();
    glaze::app().setWheelInvert(InputDevice::Mouse, true, false);
    fångad.setPointerCapture();
    hjul(fys(panel, 250, 50), 0.0f, 1.0f);
    fångad.releasePointerCapture();
    check(fångad.scrollY() > 60.0f && panel.scrollY() == 100.0f,
          "K den fångade widgeten får hjulet med sin vändning",
          tal(fångad.scrollY()));

    // ---- P. ARVET STANNAR VID FÖNSTRETS ROT
    nollställ();
    {
        glaze::AppWindow annat("popup", 200, 120, gfx::Chrome::None);
        annat.setPopup(true);
        glaze::ListBox popupLista;
        popupLista.name = "popupLista";
        annat.root().adopt(popupLista);
        setWheelInvert(InputDevice::Mouse, true, false);
        check(!popupLista.isWheelInverted(InputDevice::Mouse, false) &&
                  lista.isWheelInverted(InputDevice::Mouse, false),
              "P en lista i ett annat fönster ärver appens, inte det första "
              "fönstrets");
        annat.setWheelInvert(InputDevice::Mouse, true, false);
        check(popupLista.isWheelInverted(InputDevice::Mouse, false),
              "P … och sitt eget fönsters");
    }
    nollställ();
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== wheel_gate — rullningens riktning ===\n");
    bool kördes = false;
    {
        Värd w;
        w.setOffscreen(true);
        glaze::app().post([&] {
            kördes = true;
            w.kör();
            w.close();
        });
        w.run();
    }
    if (!kördes) {
        std::printf("  --   inget fönster kunde skapas (ingen display)\n");
        return 77;
    }
    std::printf(g_fel ? "=== wheel_gate: %d FEL av %d ===\n"
                      : "=== wheel_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
