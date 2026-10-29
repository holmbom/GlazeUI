// range_gate — REGLAGETS och RULLNINGSLISTENS värde:
// `setValue` KLAMPAS VID LÄSNING till [min, max(min, max)].
//
// Kör:  ./build/tests/range_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Headless: inga fönster, ingen pump. De förväntade värdena är HANDSKRIVNA:
//
//   R1  Slider: `setValue` över och under intervallet läses som gränsen, och
//       ett värde inom läses som det är
//   R2  SATSORDNINGEN: `setValue(150)` FÖRE `setMax(200)` ger 150 — läsningens
//       regel (talfältets, datumväljarens) och inte sättningens; och en
//       senare `setMax(50)` ger 50 utan att det lagrade talet glöms
//   R3  `min > max`: den övre räknas som den undre (`max(min, max)`)
//   R4  Scrollbar: R1–R3 likadant
//   R5  INGEN FALSK COMMIT: en `setMax` som flyttar det effektiva värdet
//       flyttar baslinjen, så ett tangentsteg tillbaka till det värdet
//       committar inte; ett steg som ändrar committar en gång
//
// SABOTAGEN som ska fälla den: `value()` som svarar med det råa talet (R1,
// R4), klampning vid SÄTTNING i `setValue` (R2: 150 blir 100), `max_` utan
// `max(min, max)` i läsningen (R3), `setMax` utan baslinjeflytten (R5: ett
// onCommit för ett värde användaren aldrig ändrade).
//
// VAD DEN INTE MÄTER: musens väg (draget och rännklicket kräver ett Ctx med
// font — spinbox_gate och commit_gate har reglaget i ett fönster) och
// ritningen.

#include <cstdio>
#include <cstring>
#include <string>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
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

std::string tal(float v)
{
    return std::to_string(v);
}

struct ProvSlider : glaze::Slider {
    using glaze::Slider::keyDown;
};

// R1–R3 på båda typerna — de delar regeln och läsvägen.
template <class T>
void fallKlampning(const char* typ)
{
    const std::string t = typ;
    T s;   // 0 … 100 som förval
    s.setValue(150.0f);
    check(s.value() == 100.0f, "R1: " + t + " setValue(150) läses 100",
          tal(s.value()));
    s.setValue(-5.0f);
    check(s.value() == 0.0f, "R1: " + t + " setValue(-5) läses 0",
          tal(s.value()));
    s.setValue(42.0f);
    check(s.value() == 42.0f, "R1: " + t + " ett värde inom läses som det är",
          tal(s.value()));

    T o;
    o.setValue(150.0f);
    o.setMax(200.0f);
    check(o.value() == 150.0f,
          "R2: " + t + " setValue(150) FÖRE setMax(200) ger 150", tal(o.value()));
    o.setMax(50.0f);
    check(o.value() == 50.0f, "R2: " + t + " en lägre max klampar läsningen",
          tal(o.value()));
    o.setMax(300.0f);
    check(o.value() == 150.0f,
          "R2: " + t + " det lagrade talet står kvar när max växer igen",
          tal(o.value()));

    T b;
    b.setMin(60.0f);
    b.setMax(40.0f);
    b.setValue(10.0f);
    check(b.value() == 60.0f, "R3: " + t + " min > max: läses som min",
          tal(b.value()));
    b.setValue(90.0f);
    check(b.value() == 60.0f,
          "R3: " + t + " min > max: den övre gränsen är den undre",
          tal(b.value()));
}

void fallIngenFalskCommit()
{
    int        byten = 0, klara = 0;
    ProvSlider s;   // 0 … 100, steg 1
    s.onChange = [&byten] { ++byten; };
    s.onCommit = [&klara] { ++klara; };
    s.setValue(80.0f);
    s.setMax(50.0f);   // det effektiva värdet flyttar 80 ⇒ 50 — appens ändring
    s.keyDown(gfx::Key::Right, 0);   // 50 ⇒ 50 (klampat): ingen ändring
    check(byten == 0 && klara == 0 && s.value() == 50.0f,
          "R5: ett steg mot taket efter setMax ändrar inget och committar "
          "inget", std::to_string(byten) + "/" + std::to_string(klara));
    s.keyDown(gfx::Key::Left, 0);    // 50 ⇒ 49
    check(byten == 1 && klara == 1 && s.value() == 49.0f,
          "R5: ett steg som ändrar fyrar onChange och onCommit en gång",
          std::to_string(byten) + "/" + std::to_string(klara));
    s.keyDown(gfx::Key::Right, 0);   // 49 ⇒ 50
    check(byten == 2 && klara == 2 && s.value() == 50.0f,
          "R5: och tillbaka igen är också en ändring", tal(s.value()));
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("range_gate\n");
    fallKlampning<glaze::Slider>("Slider");
    fallKlampning<glaze::Scrollbar>("Scrollbar");   // R4
    fallIngenFalskCommit();

    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
