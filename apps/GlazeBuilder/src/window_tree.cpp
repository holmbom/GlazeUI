// window_tree — se .h. Delas av glaze-render och pixelgrinden, så att
// grinden bygger EXAKT det träd verktyget bygger.

#include "window_tree.h"

#include <memory>

#include "apply_props.h"
#include "standin_widget.h"

namespace bld {

namespace {

// Modellens inbyggda medlemsnamn → fönstrets EGEN widget. Tabellen i
// `bld::builtinMember` säger vilka namn som finns; den här funktionen
// säger var de bor i ett riktigt fönster.
glaze::Widget* builtinWidget(glaze::AppWindow& win, const std::string& id)
{
    if (id == "statusBar")
        return &win.statusBar;
    if (id == "menuBar")
        return &win.menuBar;
    if (id == "appIcon")
        return &win.appIcon;
    return nullptr;
}

} // namespace

std::vector<glaze::Widget*> buildWindowTree(glaze::AppWindow&  win,
                                            const WindowModel& m,
                                            const char*        origin)
{
    std::vector<glaze::Widget*> byNode(m.nodes.size(), nullptr);
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        const WidgetNode& n = m.nodes[i];

        // INBYGGD FÖNSTERMEDLEM: `statusBar` skapas
        // ALDRIG ur registret — fönstret HAR redan widgeten och har
        // adopterat den i sin konstruktor. Att göra en ny hade gett en
        // andra statusrad utan koppling till fönstret, och `.show()` hade
        // då synts i previewn men inte i appen.
        if (n.builtin) {
            byNode[i] = builtinWidget(win, n.id);
            if (!byNode[i])
                gfx::log("%s: okänd inbyggd medlem '%s'", origin,
                         n.id.c_str());
            continue;
        }

        if (!m.isAdopted(n.id))
            continue;

        // REGISTRET avgör: klassen ur medlemsraden, ramverkets eller
        // appens egen; okänd klass = stand-in (regel 4) — ruta i rätt storlek
        // med klassnamnet, exakt builderns väg (`makeNodeWidget`).
        std::unique_ptr<glaze::Widget> w = makeNodeWidget(n.type);

        // NAMNET = MEDLEMSNAMNET. Den emitterade `name`-raden
        // sätter samma sträng en gång till i pass 2 — och det är hela
        // poängen: raden är kanonisk, så previewn och den byggda appen kan
        // inte säga olika saker. Builtins får sina namn av
        // `AppWindow`-konstruktorn i stället.
        w->name = n.id;

        glaze::Widget*    parent = &win.root();
        const std::string pid    = m.parentOf(n.id);
        if (!pid.empty()) {
            bool hittad = false;
            for (size_t k = 0; k < m.nodes.size(); ++k)
                if (m.nodes[k].id == pid && byNode[k]) {
                    parent = byNode[k];
                    hittad = true;
                    break;
                }
            // TYSTNAD vore fel: utan raden nedan hamnar
            // barnet i roten utan ett ord, och previewn ljuger med grönt
            // bygge.
            if (!hittad)
                gfx::log("%s: '%s' skulle adopteras i '%s' som inte är "
                         "byggd — hamnar i roten",
                         origin, n.id.c_str(), pid.c_str());
        }
        byNode[i] = parent->adopt(std::move(w));
    }

    // Pass 2: fält, widget-anrop OCH fönsteranrop i KÄLLORDNING. Ordningen
    // bär semantik — `setClientSize` och `setContentSize` utesluter
    // varandra och "den som sätts sist gäller", så ett omkastat par hade
    // renderat fel storlek med grönt bygge.
    for (const Stmt& s : m.stmts) {
        if (s.kind == Stmt::Kind::WindowCall) {
            applyWindowCall(win, Call{s.name, s.value}, origin);
            continue;
        }
        if (s.kind != Stmt::Kind::Prop && s.kind != Stmt::Kind::Call)
            continue;
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (m.nodes[i].id == s.target && byNode[i]) {
                applyStmt(*byNode[i], s, origin);
                break;
            }
    }
    return byNode;
}

} // namespace bld
