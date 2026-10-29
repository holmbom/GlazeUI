// preview_tree — se .h. Det finns EN vandring, och den är headless-grindbar.

#include "preview_tree.h"

namespace bld {

void drawPreviewTree(glaze::Widget& w, const glaze::Ctx& c)
{
    // SYNLIGHETSVAKTEN — speglar `AppWindow::drawTree`, som returnerar
    // tidigt för osynliga widgets. Utan den ritas barnen till en DOLD Panel
    // i canvasen men inte i appen, och remsraden nedan ritar overlay-lister
    // på en dold panel: `Panel::drawAfterChildren` kollar inte `visible_` —
    // det är `render()` som skyddar `draw()`, och kroken går utanför den.
    if (!w.isVisible())
        return;

    // WIDGETENS EGEN RECT MÅSTE BÄRAS I CTX:T UNDER draw() — `Draw::phys`
    // projicerar varje ritanrop mot `c.box`, så en widget som ritas med
    // osatt box lägger allt i en NOLLSTOR rect och blir helt osynlig.
    glaze::Ctx wc = c;
    wc.box        = w.resolve(c);
    // BILDENS STEG och DEN TOMMA REKTEN — som i `drawTree`: steget
    // också utan yta, ritningen (egen och efter barnen) bara med.
    const bool yta = wc.box.w > 0.0f && wc.box.h > 0.0f;
    w.frameStep();
    if (yta)
        w.render(wc);

    if (!w.children().empty()) {
        glaze::Ctx cc = c;
        cc.area       = w.childArea(c);
        // RADIEN FÖLJER MED: samma anrop som
        // `AppWindow::drawTree`, med samma `Widget`-virtuella par
        // (`clipRect`/`clipRadius`) — pariteten är strukturell, och den
        // grindas av preview_gate:s ordagranna sekvens.
        const bool doClip = w.clipsChildren();
        if (doClip)
            c.r->pushClip(w.clipRect(c), w.clipRadius(c));
        // DET FRÄMRE BARNET
        // ritas SIST, precis som i `AppWindow::drawTree` — en öppen
        // overlay hade annars hamnat under huvudytan i previewn.
        glaze::Widget* fram = w.frontChild();
        if (fram && fram->parent() != &w)
            fram = nullptr;
        for (glaze::Widget* ch : w.children())
            if (ch != fram)
                drawPreviewTree(*ch, cc);
        if (fram)
            drawPreviewTree(*fram, cc);
        if (doClip)
            c.r->popClip();
    }

    // OVERLAY-REMSORNA, sist och UTANFÖR barnklippet — exakt som
    // `drawTree` gör. Ctx:t är widgetens eget (`wc`, med satt box), och
    // ordningen följer av placeringen: efter barnloopen, efter `popClip`.
    if (yta)
        w.drawAfterChildren(wc);
}

} // namespace bld
