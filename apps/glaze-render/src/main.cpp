// glaze-render — renderar en genererad fönster-triplett offscreen till PNG.
// Fundamentet för VS Code-previewn och
// regressionsbilder.
//
//   glaze-render <fil.h|.glazeui|.cpp> [-o ut.png] [--scale <faktor>]
//
// --scale tar ABSOLUT skala: --scale 1.0 ger scale 1 på
// alla maskiner (namnkonventionen <fönster>_<skala×100>.png i doc/os/).
// Probe-and-recreate, INTE skalbyte i drift — CLI:t har ingen pump, så
// eventvägen (userScaleChanged → WindowDisplayScaleChanged) fungerar inte
// här: (1) skapa fönstret dolt och läs OS-skalan (userScale är 1.0), (2)
// riv det och sätt userScale = mål/os, (3) skapa PÅ NYTT — init räknar
// logisk × os × user från början och showOwned laddar fonten i fulla
// skalan. Deterministiskt på alla backends. OBS: userScale klampas till
// 0.5–2.0, så ett mål utanför os×[0.5, 2.0] klampas —
// stdout-raden visar den faktiska skalan.
//
// Läser tripletten via builderns cpp_reader (parsar ALDRIG användarens
// .cpp/handlers — regel 4:s fail-safe ärvs rakt av), bygger widget-trädet
// ur registret exakt enligt GlazeBuilders rebuildPreview-mönster (okänd
// typ → StandInWidget), renderar EN frame i ett DOLT fönster (regel 1 —
// ingen pump, ingen loop) och skriver retained-RT:n 1:1 som PNG.
// PNG:en ritas av exakt samma inkompilerade backend som appen kör med på
// maskinen ⇒ pixelidentisk per konstruktion (native scale, chrome med).
//
// Exit-koder: 0 = OK · 1 = reader-fail-safe (diagnos på stderr) ·
// 2 = render-/IO-fel (eller felaktig användning).

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <glaze/glaze.h>

#include "component_tree.h"
#include "cpp_reader.h"
#include "window_model.h"
#include "design_theme.h"
#include "window_tree.h"

namespace fs = std::filesystem;

namespace {

// Fönstret som renderas: titel/storlek ur modellen (ctor-argumenten —
// motsvarar genererade kodens setTitle/setClientSize), trädet byggs i
// konstruktorn ur modellen av `bld::buildWindowTree` (builder_preview):
// ÄGDA widgets adopteras i nodordning (adopt-ordningen ÄR z-ordningen),
// nästling via parent-id, satserna i källordning. Events ignoreras helt —
// de ligger som text i modellen, inget kompileras. Bygget delas med
// tests/pixel_gate.cpp, som behöver EXAKT samma träd; egenskaperna bor
// i `bld::applyProp`.
//
// De ICKE-VISUELLA medlemmarna — en `Timer`, en
// `TrayIcon` — hålls BREDVID trädet, skapade och utförda av
// `bld::buildComponents`: de syns inte i bilden, men raderna utförs som i
// appen (utom det som hade gjort något utanför ritbordet), och en okänd
// komponent loggas i stället för att tigas ihjäl.
class RenderWindow : public glaze::AppWindow {
public:
    explicit RenderWindow(const bld::WindowModel& m)
        : glaze::AppWindow(m.title(), m.clientWidth(), m.clientHeight())
    {
        bld::buildWindowTree(*this, m, "glaze-render");
        components_ = bld::buildComponents(m, "glaze-render");
    }

private:
    std::vector<bld::PreviewComponent> components_;
};

// assets.bin letas uppåt från .h-katalogen (samma vandring som builderns
// AssetDoc — fyra nivåer) och laddas FÖRE fönstret så img-fälten ritar
// riktiga ikoner. Ingen fil är inget fel — knappen ritas utan bild.
void loadAssetsNear(const std::string& hPath)
{
    fs::path dir = fs::path(hPath).parent_path();
    if (dir.empty())
        dir = fs::current_path();
    for (int i = 0; i < 4; ++i) {
        const fs::path kandidat = dir / "assets.bin";
        std::error_code ec;
        if (fs::exists(kandidat, ec)) {
            glaze::loadAssets(kandidat.string());
            return;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir)
            break;
        dir = dir.parent_path();
    }
}

// language.bin letas uppåt på EXAKT samma sätt och
// laddas FÖRE fönstret, så `setText(glaze::tr(1042))` ritar riktig text i
// stället för platshållaren ⟦1042⟧. Ingen fil är inget fel: ramverkets
// egna 1–999 är inbäddade i libglaze och fungerar ändå.
void loadLanguageNear(const std::string& hPath)
{
    fs::path dir = fs::path(hPath).parent_path();
    if (dir.empty())
        dir = fs::current_path();
    for (int i = 0; i < 4; ++i) {
        const fs::path kandidat = dir / "language.bin";
        std::error_code ec;
        if (fs::exists(kandidat, ec)) {
            glaze::loadLanguage(kandidat.string());
            return;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir)
            break;
        dir = dir.parent_path();
    }
}

int usage()
{
    std::fprintf(stderr,
                 "användning: glaze-render <fil.h|.glazeui|.cpp> "
                 "[-o ut.png] [--scale <faktor>]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::string inPath, outPath;
    float       targetScale = 0.0f;   // 0 = ingen flagga: maskinens nativa
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc)
                return usage();
            outPath = argv[++i];
        } else if (arg == "--scale") {
            if (i + 1 >= argc)
                return usage();
            // HELA argumentet måste vara ett ändligt positivt tal:
            // std::stof stannar vid första ogiltiga tecknet, så "1.25x"
            // hade annars tyst accepterats som 1.25 — och "inf" hade
            // passerat > 0 men sedan ignorerats av setUserScale, dvs.
            // renderat i nativ skala fast användaren bad om något annat.
            const std::string sv = argv[++i];
            std::size_t       used = 0;
            try {
                targetScale = std::stof(sv, &used);
            } catch (...) {
                return usage();
            }
            if (used != sv.size() || !std::isfinite(targetScale) ||
                !(targetScale > 0.0f))
                return usage();
        } else if (inPath.empty()) {
            inPath = arg;
        } else {
            return usage();
        }
    }
    if (inPath.empty())
        return usage();

    // Valfri triplett-fil → .h (association per konvention, som buildern).
    const std::string hPath = bld::headerPathFor(inPath);
    if (outPath.empty())
        outPath = hPath.substr(0, hPath.size() - 2) + ".png";   // <bas>.png

    // Läsvägen är builderns — fail-safe i stället för gissning: skadat
    // stängsel/saknad fil/oidentifierbar sats ⇒ diagnos + exit 1, inga
    // filer röres (och inget fönster skapas).
    bld::ReadResult r = bld::readWindow(hPath);
    if (!r.ok) {
        std::fprintf(stderr, "glaze-render: %s\n",
                     r.error.empty() ? (hPath + ": kunde inte läsas").c_str()
                                     : r.error.c_str());
        return 1;
    }

    loadAssetsNear(hPath);   // FÖRE fönstret (rå-bytes; texturer per fönster)
    loadLanguageNear(hPath); // FÖRE fönstret — texten mäts vid första ritningen

    // Designtaggen avkodas EN gång —
    // odekodbara värden loggas här, på stderr, medan stdout förblir ren.
    const bld::DesignTag design = bld::decodeDesignLine(r.model.designLine);

    // --scale: probe-and-recreate (se filhuvudet). Proben rivs av sin
    // dtor (executeClose — teardown utan pump) innan riktiga fönstret
    // skapas; gfx::setUserScale direkt eftersom inga fönster är öppna
    // (App::setUserScale-rundan är för skalbyte i DRIFT).
    if (targetScale > 0.0f) {
        float osScale = 1.0f;
        {
            RenderWindow probe(r.model);
            probe.setOffscreen(true);
            probe.show();
            if (!probe.isOpen()) {
                std::fprintf(stderr,
                             "glaze-render: fönstret kunde inte skapas\n");
                return 2;
            }
            osScale = probe.window().uiScale();   // userScale är 1.0 här
        }
        if (osScale > 0.0f)
            gfx::setUserScale(targetScale / osScale);
    }

    // EN engångs-frame i ett dolt fönster (regel 1 — ingen pump):
    // showOwned laddar font + kopplar render-callbacket + sätter
    // fullDamage_, renderFrame ritar trädet till retained-RT:n.
    RenderWindow win(r.model);
    win.setOffscreen(true);
    // Utseendet sätts EN gång och FÖRE show(): utseende-API:t skriver
    // App::themeState (app-globalt, statiskt) och show() laddar fonten ur
    // aktuell Style — inget explicit reloadFont() behövs. Probe-fönstret
    // ovan läser bara uiScale, som varken UI, Theme eller Style påverkar,
    // så det räcker att sätta det här.
    bld::applyDesignTag(win, design);
    win.show();
    if (!win.isOpen()) {
        std::fprintf(stderr, "glaze-render: fönstret kunde inte skapas\n");
        return 2;
    }
    win.window().renderFrame();

    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!win.window().captureFrame(rgba, w, h)) {
        std::fprintf(stderr, "glaze-render: captureFrame misslyckades\n");
        return 2;
    }
    if (!gfx::encodePng(outPath, rgba, w, h)) {
        std::fprintf(stderr, "glaze-render: kunde inte skriva %s\n",
                     outPath.c_str());
        return 2;
    }

    // Stdout-raden är ett KONTRAKT (VS Code-extensionen parsar bredd/höjd/
    // scale ur den) — %g ger full precision (1.25 får inte bli "1.2").
    std::printf("%s: %dx%d px (scale %g)\n", outPath.c_str(), w, h,
                win.window().uiScale());
    return 0;
    // Teardown utan pump: win:s dtor kör executeClose (images → font →
    // shutdown — orubblig ordning), App-singletonens dtor gfx::shutdown.
}
