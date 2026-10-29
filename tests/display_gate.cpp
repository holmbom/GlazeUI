// display_gate — SKÄRMENS FYSISKA UPPLÖSNING:
// `gfx::Window::displayMetrics()` i sex implementationer, det backend-neutrala
// i `Glaze/gfx/src/display_util.h`, kalibreringen och `AppWindow::display()`.
//
// Kör:  ./build/tests/display_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar det som SKIPPED, aldrig grönt
//
// HEADLESS — det backend-neutrala mot HANDSKRIVNA förväntade värden, aldrig mot sig
// självt (grinden räknar inga diagonaler; värdena är valda tal med kända svar):
//   E  EDID-PARSERN — timing-deskriptorns millimeter, VAKTEN mot en monitor-
//      deskriptor i första blocket (läst utan vakten hade den gett millimeter ur
//      bokstäver), basfältens hela centimeter, ett block utan EDID-huvud, och
//      id:t tillverkare-produkt-serie
//   D  DIAGONALFORMELN — två 3-4-5-trianglar med exakta svar — och SPANNET
//      50–800 dpi, strax innanför och strax utanför båda gränserna
//   A  APPLES 72-DPI-UPPSKATTNING detekteras; en riktig storlek gör det inte
//   R  RESERVEN 96/25,4 FYSISKA px per mm (inte × OS-
//      skalan): R1 `assumed`, R2 `measure` utan användbara mått, R3
//      användarskalan ändrar ingenting
//   K  KALIBRERINGEN — `setDisplayCalibration`/`effectiveDisplay`: ger
//      Calibrated, rör inte en annan skärm, tas bort med 0, och ett tomt id
//      eller ett värde utanför spannet avvisas
// MED FÖNSTER — ett DOLT fönster genom den riktiga backenden:
//   F1 mätningen finns efter show: tätheten i spannet, och ett id — eller en
//      orsak när den är antagen — och det är den `Ctx::pxPerMm` bär
//   F2 svaret är det INRE fönstrets och inte ett standardvärde: på Linux är
//      proxyn den sjätte implementationen, och ett eget svar efter init hade
//      sett ut som en giltig antagen mätning (jämförs med ett fönster som
//      aldrig visats)
//   F3 användarskalan ändrar INTE millimetern (zoom är inte täthet)
//   F4 kalibreringen genom `App::setDisplayCalibration`: fönstret blir
//      Calibrated, `Ctx::pxPerMm` följer, och borttagen går den tillbaka
//   F5 RESERVEN I BACKENDEN: står fönstret på en ANTAGEN
//      skärm är backendens svar exakt `kAssumedPxPerMm` vid användarskalan 1
//      och 1,5, oavsett `uiScale()`. På en mätt skärm finns ingen reserv att
//      pröva — raden skrivs ut som överhoppad men räknas INTE som skip (77
//      betyder "ingen display" här, och referensmaskinen har en mätt skärm)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * att en millimeter MÄTER en millimeter — det är ett linjalprov
//     (doc/os/*/TESTS.md), och grinden kan inte se panelen
//   * att WM_MOVE/WM_DISPLAYCHANGE, RRScreenChangeNotify, wl_output.done och
//     windowDidChangeScreen faktiskt kommer vid ett skärmbyte — det kräver
//     två skärmar (TESTS-raderna)
//   * loggraden EN gång per skärm-id
//   * macOS — Apple-detekteringens rena del mäts här (A)
//   * en multiplicering med skalan vid ett ANROPSSTÄLLE i en backend på en
//     MÄTT skärm — F5 fäller den bara på en antagen skärm (med OS-skala ≠ 1,
//     eller med användarskalan när backenden mäter om vid den), vilket
//     varken referensmaskinen eller WSLg har; reservens formel kan inte
//     längre ta emot en skala (parametern finns inte)
//
// SABOTAGEN den fäller (prövade i en privat kopia): proxyns
// standardvärde efter init (F2, Linux) · deskriptorvakten bort (E) · reserven
// × användarskalan (R) · Apple-detekteringen bort (A). Därtill, som SKA fälla
// grinden: `assumed` med
// `kAssumedPxPerMm × gfx::userScale()` (R3) och med en annan konstant (R1).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <gfx/platform.h>

#include <glaze/glaze.h>

#include "display_util.h"   // Glaze/gfx/src — det backend-neutrala

namespace {

namespace dsp = gfx::display;

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

bool nära(double a, double b, double tol = 1e-4) { return std::abs(a - b) < tol; }

std::string tal(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", v);
    return b;
}

// ------------------------------------------------------------- EDID-blocken
// Grindens EGEN byggare — förväntade värden får aldrig vara produktionens.

std::vector<std::uint8_t> edidBlock(const char* mfg, std::uint16_t produkt,
                                    std::uint32_t serie)
{
    std::vector<std::uint8_t> e(128, 0);
    const std::uint8_t huvud[8] = {0x00, 0xFF, 0xFF, 0xFF,
                                   0xFF, 0xFF, 0xFF, 0x00};
    std::copy(huvud, huvud + 8, e.begin());
    const unsigned kod = (unsigned(mfg[0] - 'A' + 1) << 10) |
                         (unsigned(mfg[1] - 'A' + 1) << 5) |
                         unsigned(mfg[2] - 'A' + 1);
    e[8]  = std::uint8_t(kod >> 8);
    e[9]  = std::uint8_t(kod & 0xFF);
    e[10] = std::uint8_t(produkt & 0xFF);
    e[11] = std::uint8_t(produkt >> 8);
    for (int i = 0; i < 4; ++i)
        e[12 + i] = std::uint8_t((serie >> (8 * i)) & 0xFF);
    return e;
}

// En DETALJERAD TIMING-deskriptor (pixelklocka ≠ 0) med bildstorleken i mm.
void timing(std::vector<std::uint8_t>& e, int block, int mmW, int mmH)
{
    std::uint8_t* d = &e[54 + 18 * block];
    d[0]  = 0x01;   // pixelklockan, låg och hög byte — inte noll
    d[1]  = 0x1D;
    d[12] = std::uint8_t(mmW & 0xFF);
    d[13] = std::uint8_t(mmH & 0xFF);
    d[14] = std::uint8_t(((mmW >> 8) << 4) | (mmH >> 8));
}

// En MONITOR-deskriptor (pixelklocka 0, tagg 0xFC = skärmens namn). Namnets
// bokstäver ligger där en timing-deskriptor har sina millimeter.
void monitorNamn(std::vector<std::uint8_t>& e, int block, const char* namn)
{
    std::uint8_t* d = &e[54 + 18 * block];
    d[0] = d[1] = d[2] = 0;
    d[3]              = 0xFC;
    d[4]              = 0;
    for (int i = 0; i < 13; ++i)
        d[5 + i] = namn[i] ? std::uint8_t(namn[i]) : std::uint8_t(' ');
}

// ================================================================= HEADLESS

void fallEdid()
{
    // ---- E1. Timing-deskriptorn i första blocket: millimetrarna och id:t.
    {
        auto e = edidBlock("BOE", 0x0B8B, 0x12345678);
        timing(e, 0, 344, 215);
        dsp::Edid ut;
        check(dsp::parseEdid(e.data(), e.size(), ut) && ut.widthMm == 344 &&
                  ut.heightMm == 215 && ut.id == "BOE-0B8B-12345678",
              "E1: timing-deskriptorn ger 344 × 215 mm och id BOE-0B8B-12345678",
              std::to_string(ut.widthMm) + "×" + std::to_string(ut.heightMm) +
                  " " + ut.id);
    }
    // ---- E2. VAKTEN: skärmens NAMN ligger först — dess bokstäver får aldrig
    //          bli millimeter; måtten står i block 1.
    {
        auto e = edidBlock("GLZ", 0x2701, 7);
        monitorNamn(e, 0, "GLAZE 27 IPS!");
        timing(e, 1, 597, 336);
        dsp::Edid ut;
        check(dsp::parseEdid(e.data(), e.size(), ut) && ut.widthMm == 597 &&
                  ut.heightMm == 336,
              "E2: en monitor-deskriptor först hoppas över — måtten ur nästa "
              "block (597 × 336)",
              std::to_string(ut.widthMm) + "×" + std::to_string(ut.heightMm));
    }
    // ---- E3. Ingen storlek i deskriptorerna — basfälten i hela cm.
    {
        auto e = edidBlock("DEL", 0x40F4, 0);
        timing(e, 0, 0, 0);
        e[21] = 34;
        e[22] = 19;
        dsp::Edid ut;
        check(dsp::parseEdid(e.data(), e.size(), ut) && ut.widthMm == 340 &&
                  ut.heightMm == 190 && ut.id == "DEL-40F4-00000000",
              "E3: basfälten 34 × 19 cm blir 340 × 190 mm",
              std::to_string(ut.widthMm) + "×" + std::to_string(ut.heightMm));
    }
    // ---- E4. Inget EDID-huvud, eller för kort — false och `ut` orörd.
    {
        auto e = edidBlock("BOE", 1, 1);
        timing(e, 0, 300, 200);
        e[0] = 0x01;
        dsp::Edid ut;
        ut.widthMm = 11;
        check(!dsp::parseEdid(e.data(), e.size(), ut) && ut.widthMm == 11,
              "E4: utan EDID-huvudet — false, ut orörd");
        check(!dsp::parseEdid(e.data(), 64, ut) && ut.widthMm == 11,
              "E4: ett block kortare än 128 byte — false");
    }
    // ---- E5. Id:t ur talen direkt (macOS-vägen): Apples kod 0x0610.
    check(dsp::edidId(0x0610, 0xA050, 0) == "APP-A050-00000000",
          "E5: edidId(0x0610, …) är APP — samma form som EDID-vägen",
          dsp::edidId(0x0610, 0xA050, 0));
}

void fallDiagonal()
{
    // ---- D1. 3-4-5-trianglar: diagonalen 5000 px över 500 mm är 10 px/mm,
    //          2500 px över 500 mm är 5.
    check(nära(dsp::pxPerMmFromDiagonal(3000, 4000, 300, 400), 10.0),
          "D1: 3000×4000 px på 300×400 mm är 10 px/mm",
          tal(dsp::pxPerMmFromDiagonal(3000, 4000, 300, 400)));
    check(nära(dsp::pxPerMmFromDiagonal(1500, 2000, 300, 400), 5.0),
          "D1: 1500×2000 px på 300×400 mm är 5 px/mm",
          tal(dsp::pxPerMmFromDiagonal(1500, 2000, 300, 400)));
    // ---- D2. SPANNET 50–800 dpi. 300×400 px (diagonal 500) över 3k × 4k mm
    //          har 2540/k dpi: k 50,7 → 50,1 · k 50,9 → 49,9 · k 3,18 → 798,7 ·
    //          k 3,17 → 801,3.
    check(dsp::pxPerMmFromDiagonal(300, 400, 152.1, 202.8) > 0.0f,
          "D2: 50,1 dpi ligger INNANFÖR spannet");
    check(dsp::pxPerMmFromDiagonal(300, 400, 152.7, 203.6) == 0.0f,
          "D2: 49,9 dpi ligger utanför — 0");
    check(dsp::pxPerMmFromDiagonal(300, 400, 9.54, 12.72) > 0.0f,
          "D2: 798,7 dpi ligger innanför");
    check(dsp::pxPerMmFromDiagonal(300, 400, 9.51, 12.68) == 0.0f,
          "D2: 801,3 dpi ligger utanför — 0");
    // ---- D3. Ett bildförhållande i mm-fälten (projektorer skriver 16 × 9)
    //          och saknade mått.
    check(dsp::pxPerMmFromDiagonal(1920, 1080, 16, 9) == 0.0f &&
              dsp::pxPerMmFromDiagonal(1920, 1080, 0, 0) == 0.0f &&
              dsp::pxPerMmFromDiagonal(0, 0, 300, 200) == 0.0f,
          "D3: 16 × 9 \"mm\", 0 mm och 0 px ger alla 0");
    // ---- D4. Mätningen som helhet: Measured med det mätta, Assumed med en
    //          orsak och OS-måtten kvar.
    const gfx::DisplayMetrics m = dsp::measure(3000, 4000, 300, 400, "X");
    check(m.source == gfx::DisplaySource::Measured && nära(m.pxPerMm, 10.0) &&
              m.widthMm == 300 && m.heightMm == 400 && m.id == "X" &&
              m.reason.empty(),
          "D4: measure — Measured, 10 px/mm, måtten och id:t, ingen orsak");
    const gfx::DisplayMetrics u = dsp::measure(1920, 1080, 16, 9, "P");
    check(u.source == gfx::DisplaySource::Assumed && !u.reason.empty() &&
              u.widthMm == 16 && u.heightMm == 9,
          "D4: measure utanför spannet — Assumed, med orsaken utskriven",
          u.reason);
}

void fallApple()
{
    // ---- A1. CGDisplayScreenSize utan EDID: punkterna / 2,835 px/mm —
    //          1440 × 900 punkter blir 508,0 × 317,5 mm.
    check(dsp::isApple72Estimate(1440, 900, 508.0, 317.5),
          "A1: 1440×900 punkter som 508,0 × 317,5 mm är Apples 72-dpi-"
          "uppskattning");
    check(dsp::isApple72Estimate(2560, 1440, 903.1, 508.0),
          "A1: 2560×1440 punkter som 903,1 × 508,0 mm likaså");
    // ---- A2. En riktig panel (MacBook Air 13": 286 × 179 mm) är det inte.
    check(!dsp::isApple72Estimate(1440, 900, 286.0, 179.0),
          "A2: 1440×900 punkter på 286 × 179 mm är en riktig mätning");
    check(!dsp::isApple72Estimate(1440, 900, 0.0, 0.0),
          "A2: inga mått är ingen uppskattning");
}

void fallReserven()
{
    // ---- R1. 96 dpi i FYSISKA px: 96/25,4 = 3,779528 px/mm, med källan och
    //          orsaken kvar.
    const gfx::DisplayMetrics a = dsp::assumed("Z", "grindens orsak");
    check(nära(a.pxPerMm, 3.779528) && a.source == gfx::DisplaySource::Assumed &&
              a.id == "Z" && a.reason == "grindens orsak",
          "R1: assumed — 3,7795 px/mm, Assumed, id:t och orsaken",
          tal(a.pxPerMm));
    // ---- R2. measure utan användbara mått — ett bildförhållande i mm-fälten
    //          och inga mm alls — ger samma reserv.
    const gfx::DisplayMetrics p = dsp::measure(1920, 1080, 16, 9, "P");
    const gfx::DisplayMetrics q = dsp::measure(1920, 1080, 0, 0, "Q");
    check(nära(p.pxPerMm, 3.779528) && nära(q.pxPerMm, 3.779528) &&
              p.source == gfx::DisplaySource::Assumed &&
              q.source == gfx::DisplaySource::Assumed,
          "R2: measure med 16 × 9 \"mm\" och med 0 mm — reserven 3,7795",
          tal(p.pxPerMm) + " / " + tal(q.pxPerMm));
    // ---- R3. ALDRIG skalan — appens zoom är ingen täthet, och OS-skalan
    //          inte heller.
    gfx::setUserScale(1.75f);
    const gfx::DisplayMetrics med = dsp::assumed("Z", "grindens orsak");
    gfx::setUserScale(1.0f);
    check(nära(med.pxPerMm, 3.779528),
          "R3: med användarskalan 1,75 är reserven fortfarande 3,7795",
          tal(med.pxPerMm));
}

void fallKalibreringen()
{
    gfx::DisplayMetrics m;
    m.pxPerMm = 4.0f;
    m.source  = gfx::DisplaySource::Measured;
    m.id      = "TST-0001-00000001";
    gfx::DisplayMetrics annan = m;
    annan.id                  = "TST-0002-00000002";

    // ---- K1. En kalibrering för skärmens id: Calibrated med appens tal.
    gfx::setDisplayCalibration(m.id, 5.0f);
    const gfx::DisplayMetrics k = gfx::effectiveDisplay(m);
    check(k.source == gfx::DisplaySource::Calibrated && nära(k.pxPerMm, 5.0) &&
              k.id == m.id && k.reason.empty(),
          "K1: kalibrerad skärm — Calibrated, 5 px/mm");
    check(gfx::effectiveDisplay(annan).source ==
                  gfx::DisplaySource::Measured &&
              nära(gfx::effectiveDisplay(annan).pxPerMm, 4.0),
          "K1: en annan skärm rörs inte");
    // ---- K2. 0 tar bort den.
    gfx::setDisplayCalibration(m.id, 0.0f);
    check(gfx::effectiveDisplay(m).source == gfx::DisplaySource::Measured &&
              nära(gfx::effectiveDisplay(m).pxPerMm, 4.0),
          "K2: setDisplayCalibration(id, 0) tar bort kalibreringen");
    // ---- K3. Avvisningar: utanför spannet (0,5 px/mm = 12,7 dpi) och ett
    //          tomt id ändrar ingenting.
    gfx::setDisplayCalibration(m.id, 0.5f);
    check(gfx::effectiveDisplay(m).source == gfx::DisplaySource::Measured,
          "K3: 12,7 dpi avvisas — skärmen står okalibrerad");
    gfx::DisplayMetrics utanId = m;
    utanId.id.clear();
    gfx::setDisplayCalibration("", 5.0f);
    check(gfx::effectiveDisplay(utanId).source ==
              gfx::DisplaySource::Measured,
          "K3: ett tomt id kalibreras aldrig");
}

// ============================================================ MED FÖNSTER

struct Prov : glaze::AppWindow {
    using glaze::AppWindow::makeCtx;
    Prov() : glaze::AppWindow("display_gate", 240, 140, gfx::Chrome::None) {}
};

std::string beskriv(const gfx::DisplayMetrics& m)
{
    const char* k = m.source == gfx::DisplaySource::Measured   ? "Measured"
                    : m.source == gfx::DisplaySource::Calibrated ? "Calibrated"
                                                                 : "Assumed";
    return std::string(k) + " " + tal(m.pxPerMm) + " px/mm (" +
           tal(m.pxPerMm * 25.4) + " dpi) id='" + m.id + "' orsak='" +
           m.reason + "'";
}

void fallMedFönster()
{
    // Ett fönster som ALDRIG visas — backendens svar före init (på Linux
    // proxyns), förväntat värde för F2.
    Prov aldrig;
    const gfx::DisplayMetrics före = aldrig.window().displayMetrics();

    Prov w;
    w.setOffscreen(true);
    bool                  kördes = false;
    int                   steg   = 0;
    float                 p0     = 0.0f;
    float                 s0     = 0.0f;
    gfx::DisplayMetrics   b0;   // backendens svar vid användarskalan 1 (F5)
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kördes = true;
            if (!w.isOpen()) {
                std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                            "aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }
            const gfx::DisplayMetrics& m = w.display();
            if (g_verbose)
                std::printf("       skärmen: %s\n", beskriv(m).c_str());
            const double dpi = double(m.pxPerMm) * 25.4;
            // ---- F1. Mätningen finns, i spannet, med id eller orsak.
            check(dpi >= 50.0 && dpi <= 800.0,
                  "F1: tätheten efter show ligger i 50–800 dpi", beskriv(m));
            check(m.source == gfx::DisplaySource::Assumed ? !m.reason.empty()
                                                          : !m.id.empty(),
                  "F1: en mätt skärm har ett id, en antagen en orsak",
                  beskriv(m));
            check(nära(w.makeCtx().pxPerMm, m.pxPerMm) &&
                      nära(m.pxPerMm,
                           gfx::effectiveDisplay(w.window().displayMetrics())
                               .pxPerMm),
                  "F1: Ctx::pxPerMm är fönstrets mätning (med kalibreringen)");
            // ---- F2. Det INRE fönstrets svar — aldrig standardvärdet före
            //          init (proxyn på Linux).
            const gfx::DisplayMetrics efter = w.window().displayMetrics();
            check(!(efter.id == före.id && efter.reason == före.reason),
                  "F2: efter init svarar backenden själv — inte med "
                  "fönstrets värde före init",
                  "före: " + beskriv(före) + " · efter: " + beskriv(efter));
            // ---- F3, första halvan: användarskalan byts i drift.
            p0 = m.pxPerMm;
            s0 = w.window().uiScale();
            b0 = efter;
            glaze::app().setUserScale(1.5f);
            break;
        }
        case 1: {
            check(!nära(w.window().uiScale(), s0, 1e-3),
                  "F3: användarskalan slog igenom (uiScale bytte)",
                  tal(w.window().uiScale()));
            check(nära(w.display().pxPerMm, p0),
                  "F3: användarskalan 1,5 ändrar INTE millimetern",
                  tal(p0) + " → " + tal(w.display().pxPerMm));
            // ---- F5. Reserven i backenden: på en ANTAGEN skärm exakt 96 dpi
            //          i fysiska px, vid användarskalan 1 och 1,5 — vilken
            //          OS-skala skärmen än har.
            const gfx::DisplayMetrics b1 = w.window().displayMetrics();
            if (b0.source == gfx::DisplaySource::Assumed ||
                b1.source == gfx::DisplaySource::Assumed)
                check(b0.pxPerMm == gfx::kAssumedPxPerMm &&
                          b1.pxPerMm == gfx::kAssumedPxPerMm,
                      "F5: den antagna skärmen är 3,7795 px/mm oavsett "
                      "uiScale()",
                      tal(b0.pxPerMm) + " vid uiScale " + tal(s0) + " · " +
                          tal(b1.pxPerMm) + " vid " + tal(w.window().uiScale()));
            else
                std::printf("  --   F5 hoppas över: skärmen är mätt — ingen "
                            "reserv att pröva (räknas inte som skip)\n");
            glaze::app().setUserScale(1.0f);
            break;
        }
        case 2: {
            // ---- F4. Kalibreringen genom App — fönstret följer direkt.
            const std::string id = w.display().id;
            if (id.empty()) {
                std::printf("  --   F4 hoppas över: skärmen har inget id att "
                            "kalibrera\n");
                ++g_skips;
                w.close();
                return;
            }
            glaze::app().setDisplayCalibration(id, 4.0f);
            check(w.display().source == gfx::DisplaySource::Calibrated &&
                      nära(w.display().pxPerMm, 4.0) &&
                      nära(w.makeCtx().pxPerMm, 4.0),
                  "F4: App::setDisplayCalibration — fönstret är Calibrated "
                  "och Ctx bär 4 px/mm",
                  beskriv(w.display()));
            glaze::app().setDisplayCalibration(id, 0.0f);
            check(w.display().source != gfx::DisplaySource::Calibrated &&
                      nära(w.display().pxPerMm, p0),
                  "F4: borttagen — tillbaka till mätningen",
                  beskriv(w.display()));
            w.close();
            return;
        }
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();

    if (!kördes) {
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

    std::printf("display_gate\n");
    fallEdid();
    fallDiagonal();
    fallApple();
    fallReserven();
    fallKalibreringen();
    fallMedFönster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
