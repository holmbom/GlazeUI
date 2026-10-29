// TreeView — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna — inga tal och
// inga färger i den här filen. Basen Panel ger klipp, hjulscroll, overlay-
// remsorna och innehållsytan; contentExtent överrids så de SYNLIGA raderna
// (data, inte barn) styr scrollen. ListBox är förebilden rad för rad — samma
// tre recept för markeringen, samma remsa-först, samma hjul i rader, samma
// kryssruta — och listvyn för flervalet.
//
// DET SOM ÄR TRÄDETS EGET:
//   * RADERNA ÄR DE SYNLIGA NODERNA, utplattade i djupet-först-ordning genom
//     de utfällda noderna (`rows()`). Listan byggs om LATT efter en mutation
//     — en hopfälld gren kostar ingenting att rita, och radloopen är
//     O(synliga) som ListBoxens.
//   * IDENTITETEN ÄR ETT ID per nod, inte en position: den aktuella noden,
//     ankaret och dragets källor håller fast vid noden när andra noder läggs
//     till, tas bort eller flyttas. VALET OCH KRYSSET ÄR FLAGGOR i noden
//     — de följer noden utan bokföring. Stigarna
//     räknas fram när någon frågar.
//   * VÄNSTER/HÖGER fäller ihop/ut — och vandrar till förälder respektive
//     första barn när det inte finns något att fälla (Windows trädvy).
//   * AKTIVERINGEN (dubbelklick, Enter) är listvyns `onActivate` på den
//     AKTUELLA noden; ett dubbelklick på en nod med barn fäller den också,
//     före aktiveringen.
//
// SKRIVNA BESLUT (de långa motiveringarna står i tree_view.h):
//   ingen autostorlek · ingen horisontell scroll · flervalet är listvyns ·
//   kryssen är oberoende (ingen kaskad) · Ctrl+A väljer de synliga · ingen
//   gummibandsmarkering · inga linjer · ingen typeahead · ingen
//   auto-rullning under ett drag · dragbilden visar inte antalet · släppet
//   frågar appen FÖRST, och det inbyggda flyttar till luckan appen såg
//   (hållen vid id:n, aldrig omräknad ur pekaren).

#include "glaze/widgets/tree_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <numeric>
#include <utility>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("treeview", TreeView)

namespace {

// Barnlistan under en stig — `{}` är rotlistan. Mall över const/icke-const
// så de två vägarna är EN kod (en const_cast hade gått runt kontraktet).
template <class Vec>
Vec* kidsIn(Vec& roots, const TreePath& parent)
{
    Vec* v = &roots;
    for (int i : parent) {
        if (i < 0 || std::size_t(i) >= v->size())
            return nullptr;
        v = &(*v)[std::size_t(i)].kids;
    }
    return v;
}

// Varje nod i ett delträd, djupet först i TRÄDORDNING — mall av samma skäl.
template <class Vec, class Fn>
void forEachNode(Vec& v, Fn&& fn)
{
    for (auto& n : v) {
        fn(n);
        forEachNode(n.kids, fn);
    }
}

// Är `a` en förfader till `b` ELLER samma nod? Ett mål som uppfyller det
// relativt en dragen nod ligger INUTI den.
bool ärFörfaderEllerSamma(const TreePath& a, const TreePath& b)
{
    return a.size() <= b.size() && std::equal(a.begin(), a.end(), b.begin());
}

TreePath föräldernTill(const TreePath& p)
{
    return p.empty() ? TreePath{} : TreePath(p.begin(), p.end() - 1);
}

// Stigens TEXTform "0,2" — draglasten och `setProp`. Klamrar och blanksteg
// tolereras, så C++-literalen `{0, 2}` tas också emot när den når hit via
// setProp-vägen. Varje led är ett ICKE-negativt decimalt heltal; tom text
// (eller `{}`) är roten. Allt annat ⇒ false, och ingenting gissas.
bool läsStig(const std::string& text, TreePath& ut)
{
    std::string t;
    for (char ch : text)
        if (ch != ' ' && ch != '\t')
            t += ch;
    if (!t.empty() && t.front() == '{') {
        if (t.size() < 2 || t.back() != '}')
            return false;
        t = t.substr(1, t.size() - 2);
    }
    TreePath stig;
    std::size_t start = 0;
    while (start < t.size()) {
        const std::size_t komma = t.find(',', start);
        const std::string led   = t.substr(start, komma == std::string::npos
                                                      ? std::string::npos
                                                      : komma - start);
        if (led.empty() || led.size() > 9)
            return false;
        for (char ch : led)
            if (ch < '0' || ch > '9')
                return false;
        stig.push_back(std::atoi(led.c_str()));
        if (komma == std::string::npos)
            break;
        start = komma + 1;
        if (start >= t.size())
            return false;   // avslutande komma — ingen form vi skriver
    }
    ut = std::move(stig);
    return true;
}

std::string stigText(const TreePath& p)
{
    std::string s;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (i)
            s += ',';
        s += std::to_string(p[i]);
    }
    return s;
}

} // namespace

TreeView::TreeView()
{
    setClip(true);          // raderna klipps mot innehållsytan
    setBorder(true);        // trädet ÄR en inramad yta, som listan
    showScrollbars(true);   // överflöd ska SYNAS (ListBox-prejudikatet)
}

// ---------------------------------------------------------------- noderna

std::vector<TreeView::Node>* TreeView::kids(const TreePath& parent)
{
    return kidsIn(roots_, parent);
}

const std::vector<TreeView::Node>* TreeView::kids(const TreePath& parent) const
{
    return kidsIn(roots_, parent);
}

TreeView::Node* TreeView::node(const TreePath& p)
{
    if (p.empty())
        return nullptr;
    std::vector<Node>* v = kids(föräldernTill(p));
    if (!v || p.back() < 0 || std::size_t(p.back()) >= v->size())
        return nullptr;
    return &(*v)[std::size_t(p.back())];
}

const TreeView::Node* TreeView::node(const TreePath& p) const
{
    if (p.empty())
        return nullptr;
    const std::vector<Node>* v = kids(föräldernTill(p));
    if (!v || p.back() < 0 || std::size_t(p.back()) >= v->size())
        return nullptr;
    return &(*v)[std::size_t(p.back())];
}

// Stigen till noden med id:t — djupet först genom HELA trädet, också de
// hopfällda grenarna (en vald nod kan ligga dold).
bool TreeView::pathOf(std::uint64_t id, TreePath& ut) const
{
    if (id == 0)
        return false;
    TreePath p;
    auto sök = [&](auto& själv, const std::vector<Node>& v) -> bool {
        for (std::size_t i = 0; i < v.size(); ++i) {
            p.push_back(int(i));
            if (v[i].id == id || själv(själv, v[i].kids))
                return true;
            p.pop_back();
        }
        return false;
    };
    if (!sök(sök, roots_))
        return false;
    ut = std::move(p);
    return true;
}

// Massformens fält 1–8 till den interna noden — VARJE fält, för en tappad
// flagga här ger ett annat tillstånd än de platta satserna (tree_gate 27).
TreeView::Node TreeView::fromPublic(TreeNode&& t)
{
    Node n;
    n.text          = std::move(t.text);
    n.img           = std::move(t.img);
    n.expanded      = t.expanded;
    n.allowDropInto = t.allowDropInto;
    n.checked       = t.checked;
    n.color         = t.color;
    n.cells         = std::move(t.cells);
    n.id            = nextId_++;
    n.kids.reserve(t.children.size());
    for (TreeNode& barn : t.children)
        n.kids.push_back(fromPublic(std::move(barn)));
    return n;
}

// SORTERINGEN INOM SYSKON görs HÄR, när raderna byggs: varje
// barnlista besöks i kolumnens ordning, men stigen är nodens plats i DATA —
// indexen förblir appens, som tabellens. Kolumn 0 jämförs på nodens text,
// resten på cellerna, naturligt och STABILT (lika syskon behåller appens
// ordning i båda riktningarna, `Table::ensureOrder`). Ingen användarkod körs.
const std::vector<TreeView::Row>& TreeView::rows() const
{
    if (!rowsDirty_)
        return rows_;
    rows_.clear();
    const int  kol      = sortColumn();
    const bool sorterar = kol >= 0 && kol < columnCount();
    const bool upp      = isSortAscending();
    const auto värde    = [kol](const Node& n) -> const std::string& {
        static const std::string tom;
        if (kol == 0)
            return n.text.str();
        return std::size_t(kol - 1) < n.cells.size()
                   ? n.cells[std::size_t(kol - 1)]
                   : tom;
    };
    TreePath p;
    auto gå = [&](auto& själv, const std::vector<Node>& v, int djup) -> void {
        std::vector<int> ordning(v.size());
        std::iota(ordning.begin(), ordning.end(), 0);
        if (sorterar)
            std::stable_sort(ordning.begin(), ordning.end(),
                             [&](int a, int b) {
                                 const int r = compareNatural(
                                     värde(v[std::size_t(a)]),
                                     värde(v[std::size_t(b)]));
                                 return upp ? r < 0 : r > 0;
                             });
        for (int i : ordning) {
            const Node& n = v[std::size_t(i)];
            p.push_back(i);
            rows_.push_back(Row{&n, djup, p});
            if (n.expanded && !n.kids.empty())
                själv(själv, n.kids, djup + 1);
            p.pop_back();
        }
    };
    gå(gå, roots_, 0);
    rowsDirty_ = false;
    return rows_;
}

// EN väg för varje strukturändring: radcachen är inaktuell, hover-raden
// pekar kanske på en annan nod nu, och ytan ska ritas om.
void TreeView::rowsChanged()
{
    rowsDirty_ = true;
    hover_     = -1;
    contentChanged();   // de synliga raderna är det `cellContentW` mäter
    invalidate();
}

// KOLUMNENS INNEHÅLL — bredden de SYNLIGA raderna behöver, utan
// cellens luft (basen lägger på den). Kolumn 0 är nodens block: indraget
// (en nivå per djup), pilens ruta, kryssrutans zon när kryssen syns, ikonen
// med sitt mellanrum och texten — samma ordning och samma mått som
// `contentX` och `draw` placerar dem i, räknade från cellens luft. Kolumn 1…n
// är nodens celler. En hopfälld grens noder syns inte och räknas inte: bredden
// följer det man ser, och `rowsChanged` räknar om den när något fälls ut.
float TreeView::cellContentW(const Ctx& c, int column) const
{
    const auto& m = *c.style;
    float       w = 0.0f;
    for (const Row& r : rows()) {
        float rad = 0.0f;
        if (column == 0) {
            rad = float(r.depth) * m.treeIndent + m.treeExpanderW;
            if (showChecks_)
                rad += m.checkSize + m.checkGap;
            if (r.n->img.valid()) {
                const float k = r.n->img.sizeClass();
                rad += (k > 0.0f ? k : m.iconFallback) + m.iconGap;
            }
            rad += Draw::measure(c, r.n->text.str()).w;
        } else if (std::size_t(column) <= r.n->cells.size()) {
            rad = Draw::measure(c, r.n->cells[std::size_t(column - 1)]).w;
        }
        w = std::max(w, rad);
    }
    return w;
}

// KOLUMNHUVUDETS KROKAR. En ny sorteringskolumn eller riktning
// ändrar vilka noder raderna visar var — samma väg som en strukturändring.
void TreeView::orderChanged()
{
    rowsChanged();
}

// Ett klick i huvudet: den aktuella noden står kvar som NOD — rulla den i
// synfältet på dess nya plats (tabellens `ensureVisible(currentRow())`). En
// dold aktuell nod fälls inte fram av ett klick i huvudet.
void TreeView::headerSorted()
{
    TreePath p;
    if (rowOf(currentId_) >= 0 && pathOf(currentId_, p))
        ensureVisible(p);
}

// Nodtexterna är `TrText` — ett språkbyte kan ändra syskonens ordning när
// trädet sorteras på kolumn 0.
void TreeView::languageChanged()
{
    ColumnPanel::languageChanged();
    if (sortColumn() == 0 && columnCount() > 0)
        rowsChanged();
}

int TreeView::rowOf(std::uint64_t id) const
{
    if (id == 0)
        return -1;
    const auto& r = rows();
    for (std::size_t i = 0; i < r.size(); ++i)
        if (r[i].n->id == id)
            return int(i);
    return -1;
}

// ------------------------------------------------------------------ valet

bool TreeView::assignSelection(std::vector<std::uint64_t> ids)
{
    std::sort(ids.begin(), ids.end());
    bool ändrat = false;
    forEachNode(roots_, [&](Node& n) {
        const bool på = std::binary_search(ids.begin(), ids.end(), n.id);
        if (n.selected != på) {
            n.selected = på;
            ändrat     = true;
        }
    });
    if (ändrat)
        invalidate();
    return ändrat;
}

std::vector<std::uint64_t> TreeView::selectedIds() const
{
    std::vector<std::uint64_t> ut;
    forEachNode(roots_, [&](const Node& n) {
        if (n.selected)
            ut.push_back(n.id);
    });
    return ut;
}

std::vector<std::uint64_t> TreeView::topLevelSelected() const
{
    // En vald nods delträd hoppas över helt — dess valda ättlingar följer
    // med den och ska inte flyttas en gång till.
    std::vector<std::uint64_t> ut;
    auto gå = [&](auto& själv, const std::vector<Node>& v) -> void {
        for (const Node& n : v) {
            if (n.selected)
                ut.push_back(n.id);
            else
                själv(själv, n.kids);
        }
    };
    gå(gå, roots_);
    return ut;
}

std::vector<std::uint64_t> TreeView::visibleIds() const
{
    std::vector<std::uint64_t> ut;
    for (const Row& r : rows())
        ut.push_back(r.n->id);
    return ut;
}

void TreeView::forgetIn(const Node& rot)
{
    auto i = [&rot](std::uint64_t id) {
        if (id == 0)
            return false;
        bool träff = rot.id == id;
        forEachNode(rot.kids, [&](const Node& n) {
            if (n.id == id)
                träff = true;
        });
        return träff;
    };
    if (i(currentId_))
        currentId_ = 0;
    if (i(anchorId_))
        anchorId_ = 0;
    if (i(pendingSoloId_))
        pendingSoloId_ = 0;
}

void TreeView::setMultiSelect(bool on)
{
    if (multiSelect_ == on)
        return;
    multiSelect_   = on;
    pendingSoloId_ = 0;
    // Av med flera valda: bara EN får stå kvar — `selectedPath()`s nod (den
    // aktuella om den är vald, annars den första valda) — och den blir den
    // aktuella, för i enkelval är de samma. Programmatiskt, alltså inget
    // event.
    if (!on) {
        const TreePath kvar = selectedPath();
        const Node*    n    = node(kvar);
        const std::uint64_t id = n ? n->id : 0;
        assignSelection(id ? std::vector<std::uint64_t>{id}
                           : std::vector<std::uint64_t>{});
        currentId_ = id;
        anchorId_  = id;
    }
    invalidate();
}

void TreeView::setSelectedPath(const TreePath& path)
{
    std::uint64_t id = 0;
    if (!path.empty()) {
        const Node* n = node(path);
        if (!n) {
            gfx::log("TreeView(%s): setSelectedPath — noden {%s} finns inte "
                     "(ignoreras)", name.c_str(), stigText(path).c_str());
            return;
        }
        id = n->id;
    }
    // `{}` släpper ÄVEN den aktuella noden — trädets kontrakt (headern).
    const bool ändrat = assignSelection(id ? std::vector<std::uint64_t>{id}
                                           : std::vector<std::uint64_t>{});
    if (currentId_ != id || anchorId_ != id) {
        currentId_ = id;
        anchorId_  = id;
        if (!ändrat)
            invalidate();   // fokusramen följer den aktuella noden
    }
}

TreePath TreeView::selectedPath() const
{
    TreePath p;
    const Node* akt = pathOf(currentId_, p) ? node(p) : nullptr;
    if (akt && akt->selected)
        return p;
    const std::vector<std::uint64_t> valda = selectedIds();   // trädordning
    p.clear();
    if (!valda.empty())
        pathOf(valda.front(), p);
    return p;
}

std::string TreeView::selectedText() const
{
    const Node* n = node(selectedPath());
    return n ? n->text.str() : std::string();
}

void TreeView::setSelected(const TreePath& path, bool on)
{
    Node* n = node(path);
    if (!n) {
        gfx::log("TreeView(%s): setSelected — noden {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(path).c_str());
        return;
    }
    if (!multiSelect_) {
        // ENKELVAL: den här och ingen annan — och den aktuella är den valda.
        if (on)
            setSelectedPath(path);
        else if (n->selected)
            setSelectedPath({});
        return;
    }
    if (n->selected == on)
        return;
    n->selected = on;
    invalidate();
}

bool TreeView::isSelected(const TreePath& path) const
{
    const Node* n = node(path);
    return n && n->selected;
}

std::vector<TreePath> TreeView::selectedPaths() const
{
    std::vector<TreePath> ut;
    TreePath p;
    auto gå = [&](auto& själv, const std::vector<Node>& v) -> void {
        for (std::size_t i = 0; i < v.size(); ++i) {
            p.push_back(int(i));
            if (v[i].selected)
                ut.push_back(p);
            själv(själv, v[i].kids);
            p.pop_back();
        }
    };
    gå(gå, roots_);
    return ut;
}

TreePath TreeView::currentPath() const
{
    TreePath p;
    pathOf(currentId_, p);
    return p;
}

void TreeView::selectAll()
{
    if (!multiSelect_)
        return;   // enkelval har ingen "alla"
    assignSelection(visibleIds());
}

void TreeView::clearSelection()
{
    setSelectedPath({});
}

// ---------------------------------------------- KOLLEKTIONSVOKABULÄREN

TreePath TreeView::addNode(const TreePath& parent, TrText text, ImageSource img,
                           std::optional<gfx::Color> color,
                           std::vector<std::string> cells)
{
    std::vector<Node>* v = kids(parent);
    if (!v) {
        gfx::log("TreeView(%s): addNode — föräldern {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(parent).c_str());
        return {};
    }
    Node n;
    n.text  = std::move(text);
    n.img   = std::move(img);
    n.color = color;
    n.cells = std::move(cells);
    n.id    = nextId_++;
    v->push_back(std::move(n));
    rowsChanged();
    TreePath ut = parent;
    ut.push_back(int(v->size()) - 1);
    return ut;
}

void TreeView::setNode(const TreePath& path, TrText text, ImageSource img,
                       std::optional<gfx::Color> color,
                       std::vector<std::string> cells)
{
    Node* n = node(path);
    if (!n) {
        gfx::log("TreeView(%s): setNode — noden {%s} finns inte (ignoreras)",
                 name.c_str(), stigText(path).c_str());
        return;
    }
    if (n->text == text && n->img == img && n->color == color &&
        n->cells == cells)
        return;   // hård regel 1: invalidera vid ÄNDRING
    n->text  = std::move(text);
    n->img   = std::move(img);
    n->color = color;
    n->cells = std::move(cells);
    contentChanged();   // text, ikon och celler är det `cellContentW` mäter
    // Strukturen är densamma — radcachens pekare gäller — men i ett sorterat
    // träd kan nodens plats bland syskonen ha ändrats.
    if (sortColumn() >= 0 && sortColumn() < columnCount())
        orderChanged();
    else
        invalidate();
}

void TreeView::setNodes(std::vector<TreeNode> nodes)
{
    std::vector<Node> nya;
    nya.reserve(nodes.size());
    for (TreeNode& t : nodes)
        nya.push_back(fromPublic(std::move(t)));
    roots_         = std::move(nya);
    currentId_     = 0;   // ingen nod finns kvar att följa
    anchorId_      = 0;
    pendingSoloId_ = 0;
    changedPath_.clear();
    rowsChanged();
    // Scrollen klampas OMEDELBART (som ListBoxens) — och samma tvådelade
    // vakt: utan ett VISAT fönster finns varken renderer eller font.
    if (appWindow() && appWindow()->isOpen()) {
        const Ctx c = behaviorCtx();
        setScroll(scrollX(), std::min(scrollY(), scrollRange(c).h));
    }
}

void TreeView::removeNode(const TreePath& path)
{
    std::vector<Node>* v = kids(föräldernTill(path));
    if (path.empty() || !v || path.back() < 0 ||
        std::size_t(path.back()) >= v->size()) {
        gfx::log("TreeView(%s): removeNode — noden {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(path).c_str());
        return;
    }
    // Den aktuella noden, ankaret och ett väntande soloval i delträdet
    // släpps — utan event, det var inget användarval. Valets flaggor
    // försvinner med noderna. Frågan ställs FÖRE borttagningen.
    forgetIn((*v)[std::size_t(path.back())]);
    v->erase(v->begin() + path.back());
    rowsChanged();
    if (appWindow() && appWindow()->isOpen()) {
        const Ctx c = behaviorCtx();
        setScroll(scrollX(), std::min(scrollY(), scrollRange(c).h));
    }
}

// FLYTTEN — trädets enda strukturmutation som tar noden MED SIG (id,
// utfällning, val, kryss och delträd följer). Indexet är räknat i
// FÖRE-flytten-koordinater (Qt:s `moveRows`-konvention): det är så ett
// släpp mäts, och då behöver anroparen aldrig räkna om själv.
bool TreeView::moveNode(const TreePath& from, const TreePath& toParent,
                        int index)
{
    const Node*              källa = node(from);
    const std::vector<Node>* mål   = kids(toParent);
    if (!källa || !mål) {
        gfx::log("TreeView(%s): moveNode {%s} → {%s} — källan eller målet "
                 "finns inte (ignoreras)", name.c_str(),
                 stigText(from).c_str(), stigText(toParent).c_str());
        return false;
    }
    if (ärFörfaderEllerSamma(from, toParent)) {
        gfx::log("TreeView(%s): moveNode {%s} → {%s} — målet ligger INUTI "
                 "noden som flyttas (ignoreras)", name.c_str(),
                 stigText(from).c_str(), stigText(toParent).c_str());
        return false;
    }
    if (index < 0 || std::size_t(index) > mål->size()) {
        gfx::log("TreeView(%s): moveNode — index %d utanför [0, %zu] "
                 "(ignoreras)", name.c_str(), index, mål->size());
        return false;
    }

    // Målet hålls fast vid sitt ID över uttaget: tas källan ut ur en
    // syskonlista som ligger FÖRE målets förfader flyttar målets stig, och
    // den gamla stigen hade pekat på fel nod.
    const std::uint64_t målId = toParent.empty() ? 0 : node(toParent)->id;
    const TreePath      från  = föräldernTill(from);
    int                 at    = index;
    if (från == toParent && from.back() < index)
        --at;   // uttaget flyttar efterföljande syskon ett steg

    std::vector<Node>* ut = kids(från);
    Node flyttad = std::move((*ut)[std::size_t(from.back())]);
    ut->erase(ut->begin() + from.back());

    std::vector<Node>* in = &roots_;
    if (målId != 0) {
        TreePath p;
        if (!pathOf(målId, p)) {
            // Kan inte hända (målet låg inte i källans delträd) — men en
            // nod som försvann får aldrig tappas tyst.
            ut->insert(ut->begin() + from.back(), std::move(flyttad));
            return false;
        }
        in = &node(p)->kids;
    }
    in->insert(in->begin() + at, std::move(flyttad));
    rowsChanged();
    return true;
}

// FLERA FRÅN KODEN: stigarna slås upp till id:n FÖRE allt annat — en
// ogiltig stig avbryter innan något ändrats — och sedan är det SAMMA flytt
// som släppet utför.
bool TreeView::moveNodes(const std::vector<TreePath>& from,
                         const TreePath& toParent, int index)
{
    if (from.empty()) {
        gfx::log("TreeView(%s): moveNodes — ingen nod att flytta (ignoreras)",
                 name.c_str());
        return false;
    }
    std::vector<std::uint64_t> ids;
    ids.reserve(from.size());
    for (const TreePath& p : from) {
        const Node* n = node(p);
        if (!n) {
            gfx::log("TreeView(%s): moveNodes — noden {%s} finns inte "
                     "(ignoreras)", name.c_str(), stigText(p).c_str());
            return false;
        }
        ids.push_back(n->id);
    }
    return moveIds(ids, toParent, index);
}

bool TreeView::moveIds(const std::vector<std::uint64_t>& ids,
                       const TreePath& toParent, int index)
{
    // 1. VALIDERA ALLT FÖRE NÅGON MUTATION — källorna i trädordning (en
    //    stig jämförs lexikografiskt, och det ÄR trädordningen).
    std::vector<std::pair<TreePath, std::uint64_t>> källor;
    for (const std::uint64_t id : ids) {
        TreePath p;
        if (!pathOf(id, p)) {
            gfx::log("TreeView(%s): flytten — en dragen nod finns inte längre "
                     "(ignoreras)", name.c_str());
            return false;
        }
        källor.emplace_back(std::move(p), id);
    }
    std::sort(källor.begin(), källor.end());
    // ÖVERLAPP: en källa som ligger INUTI en annan
    // följer med sin förfader och tas inte ut för sig — annars hade den
    // försvunnit ur trädet med förfaderns uttag och nästa uppslag misslyckats.
    // Trädordningen lägger förfadern före ättlingen; samma id två gånger
    // räknas likadant. Gäller också när appens `onDragStart` flyttat en dragen
    // nod in under en annan.
    {
        std::vector<std::pair<TreePath, std::uint64_t>> kvar;
        for (auto& k : källor)
            if (kvar.empty() || !ärFörfaderEllerSamma(kvar.back().first, k.first))
                kvar.push_back(std::move(k));
        källor = std::move(kvar);
    }
    if (källor.empty())
        return false;   // ingenting att flytta
    const std::vector<Node>* mål = kids(toParent);
    if (!mål) {
        gfx::log("TreeView(%s): flytten → {%s} — målet finns inte (ignoreras)",
                 name.c_str(), stigText(toParent).c_str());
        return false;
    }
    for (const auto& k : källor)
        if (ärFörfaderEllerSamma(k.first, toParent)) {
            gfx::log("TreeView(%s): flytten → {%s} — målet ligger INUTI en "
                     "dragen nod (ignoreras)", name.c_str(),
                     stigText(toParent).c_str());
            return false;
        }
    if (index < 0 || std::size_t(index) > mål->size()) {
        gfx::log("TreeView(%s): flytten — index %d utanför [0, %zu] "
                 "(ignoreras)", name.c_str(), index, mål->size());
        return false;
    }

    // 2. INDEXET i FÖRE-flytten-koordinater: varje källa med samma förälder
    //    och lägre index flyttar platsen ett steg när den tas ut.
    int at = index;
    for (const auto& k : källor)
        if (föräldernTill(k.first) == toParent && k.first.back() < index)
            --at;

    // 3. UTTAGET, via id — en källas uttag flyttar de andras stigar, så
    //    varje stig slås upp på nytt. Målet hålls fast vid sitt ID.
    const std::uint64_t målId = toParent.empty() ? 0 : node(toParent)->id;
    std::vector<Node>   flyttade;
    flyttade.reserve(källor.size());
    for (const auto& k : källor) {
        TreePath p;
        if (!pathOf(k.second, p) || p.empty())
            continue;   // kan inte hända efter överlappsbeskärningen ovan
        std::vector<Node>* v = kids(föräldernTill(p));
        flyttade.push_back(std::move((*v)[std::size_t(p.back())]));
        v->erase(v->begin() + p.back());
    }

    // 4. MÅLSTIGEN RÄKNAS OM efter uttaget, och noderna sätts in i
    //    trädordning.
    std::vector<Node>* in = &roots_;
    if (målId != 0) {
        TreePath p;
        if (!pathOf(målId, p)) {
            // Kan inte hända (målet låg inte inuti en dragen nod) —
            // men en nod får aldrig tappas tyst: sist bland toppnoderna.
            gfx::log("TreeView(%s): flytten — målet försvann under uttaget; "
                     "noderna läggs sist bland toppnoderna", name.c_str());
            roots_.insert(roots_.end(), std::make_move_iterator(flyttade.begin()),
                          std::make_move_iterator(flyttade.end()));
            rowsChanged();
            return false;
        }
        in = &node(p)->kids;
    }
    in->insert(in->begin() + at, std::make_move_iterator(flyttade.begin()),
               std::make_move_iterator(flyttade.end()));
    rowsChanged();
    return true;
}

// ---------------------------------------------------------------- läsvägen

int TreeView::nodeCount(const TreePath& parent) const
{
    const std::vector<Node>* v = kids(parent);
    return v ? int(v->size()) : 0;
}

bool TreeView::hasNode(const TreePath& path) const
{
    return node(path) != nullptr;
}

const std::string& TreeView::nodeText(const TreePath& path) const
{
    static const std::string tom;
    const Node* n = node(path);
    return n ? n->text.str() : tom;
}

const TrText& TreeView::nodeTextToken(const TreePath& path) const
{
    static const TrText tom;
    const Node* n = node(path);
    return n ? n->text : tom;
}

const ImageSource& TreeView::nodeImg(const TreePath& path) const
{
    static const ImageSource tom;
    const Node* n = node(path);
    return n ? n->img : tom;
}

void TreeView::setNodeColor(const TreePath& path, std::optional<gfx::Color> color)
{
    Node* n = node(path);
    if (!n) {
        gfx::log("TreeView(%s): setNodeColor — noden {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(path).c_str());
        return;
    }
    if (n->color == color)
        return;   // hård regel 1: invalidera vid ÄNDRING
    n->color = color;
    invalidate();
}

std::optional<gfx::Color> TreeView::nodeColor(const TreePath& path) const
{
    const Node* n = node(path);
    return n ? n->color : std::nullopt;
}

// ------------------------------------------------------ cellerna

void TreeView::setCell(const TreePath& path, int column, std::string text)
{
    Node* n = node(path);
    if (!n || column < 1) {
        gfx::log("TreeView(%s): setCell({%s}, %d) — %s (ignoreras)",
                 name.c_str(), stigText(path).c_str(), column,
                 !n            ? "noden finns inte"
                 : column == 0 ? "kolumn 0 är nodens text, sätt den med setNode"
                               : "en negativ kolumn");
        return;
    }
    // Kolumnen behöver INTE finnas än (tabellens `setCell`) — en nod med
    // färre celler fylls ut med tomma.
    auto& celler = n->cells;
    if (std::size_t(column) > celler.size())
        celler.resize(std::size_t(column));
    if (celler[std::size_t(column - 1)] == text)
        return;   // hård regel 1: invalidera vid ÄNDRING
    celler[std::size_t(column - 1)] = std::move(text);
    contentChanged();   // cellens text är det `cellContentW` mäter
    if (column == sortColumn())
        orderChanged();   // nodens plats bland syskonen kan ha ändrats
    else
        invalidate();
}

const std::string& TreeView::cell(const TreePath& path, int column) const
{
    static const std::string tom;
    const Node* n = node(path);
    if (!n || column < 0)
        return tom;
    if (column == 0)
        return n->text.str();
    return std::size_t(column) <= n->cells.size()
               ? n->cells[std::size_t(column - 1)]
               : tom;
}

void TreeView::setExpanded(const TreePath& path, bool on)
{
    Node* n = node(path);
    if (!n) {
        gfx::log("TreeView(%s): setExpanded — noden {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(path).c_str());
        return;
    }
    if (n->expanded == on)
        return;
    n->expanded = on;
    rowsChanged();
}

bool TreeView::isExpanded(const TreePath& path) const
{
    const Node* n = node(path);
    return n && n->expanded;
}

// SLÄPP IN I — en regel för draget, inte ett utseende: ingenting
// syns förrän nästa `dragOver` räknar platsen, så settern invaliderar inte.
void TreeView::setAllowDropInto(const TreePath& path, bool on)
{
    Node* n = node(path);
    if (!n) {
        gfx::log("TreeView(%s): setAllowDropInto — noden {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(path).c_str());
        return;
    }
    n->allowDropInto = on;
}

bool TreeView::allowDropInto(const TreePath& path) const
{
    const Node* n = node(path);
    return n && n->allowDropInto;
}

// ----------------------------------------------------------------- kryssen

void TreeView::setShowChecks(bool on)
{
    if (showChecks_ == on)
        return;
    showChecks_ = on;
    contentChanged();   // rutans zon är kolumn 0:s innehåll
    invalidate();   // rutan tar plats — ikonen och texten flyttar
}

void TreeView::setChecked(const TreePath& path, bool on)
{
    Node* n = node(path);
    if (!n) {
        gfx::log("TreeView(%s): setChecked — noden {%s} finns inte "
                 "(ignoreras)", name.c_str(), stigText(path).c_str());
        return;
    }
    if (n->checked == on)
        return;   // hård regel 1: invalidera vid ÄNDRING
    n->checked = on;   // INGEN kaskad — barnen rörs inte
    invalidate();
}

bool TreeView::isChecked(const TreePath& path) const
{
    const Node* n = node(path);
    return n && n->checked;
}

// `selectedPaths()`s parhäst — djupet först genom HELA trädet, så en kryssad
// nod i en hopfälld gren är med.
std::vector<TreePath> TreeView::checkedPaths() const
{
    std::vector<TreePath> ut;
    TreePath p;
    auto gå = [&](auto& själv, const std::vector<Node>& v) -> void {
        for (std::size_t i = 0; i < v.size(); ++i) {
            p.push_back(int(i));
            if (v[i].checked)
                ut.push_back(p);
            själv(själv, v[i].kids);
            p.pop_back();
        }
    };
    gå(gå, roots_);
    return ut;
}

bool TreeView::setProp(const std::string& key, const std::string& value)
{
    if (key == "selectedPath") {
        TreePath p;
        if (!läsStig(value, p)) {
            gfx::log("setProp(%s): ogiltig selectedPath (förväntar \"0,2\"): "
                     "%s (ignoreras)", name.c_str(), value.c_str());
            return true;   // nyckeln är vår — värdet var fel
        }
        setSelectedPath(p);
        return true;
    }
    if (key == "multiSelect") {
        setMultiSelect(propparse::parseBool(value));
        return true;
    }
    if (key == "showChecks") {
        setShowChecks(propparse::parseBool(value));
        return true;
    }
    return ColumnPanel::setProp(key, value);   // basen SIST — konventionen
}

// -------------------------------------------------------------- geometrin
// EN sanning i widget-lokala LOGISKA px, scroll-förskjuten (samma rymd som
// ritningen). Musvägen projicerar med Draw::project/localY — aldrig två
// uppsättningar mått. ListBoxens `rowRect`/`checkRect`/`checkZone` är
// prejudikatet.

// Radens band är det SYNLIGA bandet i rullningsytan (under huvudet när
// trädet har kolumner) — markeringen fyller det man ser, som tabellens.
gfx::Rectf TreeView::rowRect(const Ctx& c, int row) const
{
    const gfx::Rectf in = scrollViewport(c);
    const float      h  = c.style->listRowH;
    return gfx::Rectf{in.x, in.y - scrollY() + float(row) * h, in.w, h};
}

// KOLUMNENS CELL på raden — följer den horisontella scrollen,
// tabellens `cellRect`.
gfx::Rectf TreeView::cellRect(const Ctx& c, int row, int column) const
{
    const gfx::Rectf r = rowRect(c, row);
    return gfx::Rectf{r.x - scrollXClamped(c) + columnX(c, column), r.y,
                      columnW(c, column), r.h};
}

// Pilens ruta: indragen en nivå per djup. Löv har ingen pil, men rutan
// finns ändå — kolumnen håller kryssen, ikonerna och texterna i linje. Den
// står i kolumn 0 och följer den i sidled (utan kolumner rullar inget i
// sidled, och förskjutningen är 0).
gfx::Rectf TreeView::expanderRect(const Ctx& c, int row) const
{
    const gfx::Rectf r = rowRect(c, row);
    const float      d = float(rows()[std::size_t(row)].depth);
    return gfx::Rectf{r.x - scrollXClamped(c) + c.style->listPadX +
                          d * c.style->treeIndent,
                      r.y, c.style->treeExpanderW, r.h};
}

// KRYSSRUTAN: direkt efter pilens kolumn, före ikonen — Windows
// trädvys plats — i ListBoxens mått, V-CENTRERAD i raden.
gfx::Rectf TreeView::checkRect(const Ctx& c, int row) const
{
    const gfx::Rectf e = expanderRect(c, row);
    const float      s = c.style->checkSize;
    return gfx::Rectf{e.x + e.w, e.y + (e.h - s) / 2.0f, s, s};
}

// Klickzonen: rutan plus `checkGap`, i radens HELA höjd (ListBoxens).
gfx::Rectf TreeView::checkZone(const Ctx& c, int row) const
{
    const gfx::Rectf r  = rowRect(c, row);
    const gfx::Rectf cr = checkRect(c, row);
    return gfx::Rectf{cr.x, r.y, cr.w + c.style->checkGap, r.h};
}

float TreeView::contentX(const Ctx& c, int row) const
{
    if (showChecks_) {
        const gfx::Rectf cr = checkRect(c, row);
        return cr.x + cr.w + c.style->checkGap;
    }
    const gfx::Rectf e = expanderRect(c, row);
    return e.x + e.w;
}

int TreeView::rowAt(const Ctx& c, float my) const
{
    const gfx::Rectf base = resolve(c);
    const gfx::Rectf in   = scrollViewport(c);
    const float      y    = Draw::localY(c, base, my);
    const float      h    = c.style->listRowH;
    if (y < in.y || y > in.y + in.h || h <= 0.0f)
        return -1;   // ovanför/nedanför rullningsytan (ramen och huvudet
                     // räknas inte)
    const int row = int(std::floor((y - in.y + scrollY()) / h));
    return row >= 0 && std::size_t(row) < rows().size() ? row : -1;
}

// Raden under en punkt i HELA innehållsytan (fysiska px): −1 i remsan och
// utanför innehållsytan i x OCH y — `pathAt` och
// högertrycket frågar samma sak, så de är överens om "ingen".
int TreeView::hitRow(const Ctx& c, float mx, float my) const
{
    return inRowColumn(c, mx, my) ? rowAt(c, my) : -1;   // y: `rowAt`
}

// ------------------------------------------------------------ SNABBMENYN
// SNABBMENYNS träffväg talar LOGISKA px från trädets
// hörn, och noden under det senaste högertrycket hålls vid ID — en `onChange`
// som bygger om trädet gör svaret `{}` i stället för en annan nod.

TreePath TreeView::pathAt(float localX, float localY) const
{
    if (!appWindow() || !appWindow()->isOpen())
        return {};   // ingen layout att träffa — `behaviorCtx`s krav
    const Ctx        c  = behaviorCtx();
    const gfx::Rectf pt = Draw::project(c, resolve(c),
                                        gfx::Rectf{localX, localY, 0.0f, 0.0f});
    const int        row = hitRow(c, pt.x, pt.y);
    if (row < 0)
        return {};   // FÖRE indexeringen
    return rows()[std::size_t(row)].path;
}

TreePath TreeView::contextPath() const
{
    // Inaktiverat: routingen hoppar över trädet, och en förfaders handler
    // ska få "ingen" och inte ett gammalt värde.
    TreePath p;
    if (!isEnabled() || !pathOf(contextId_, p))
        return {};
    return p;
}

// HÖGERTRYCKET VÄLJER SOM UTFORSKAREN: en nod utanför valet väljs ensam
// genom användarvägen (`onChange` + `onCommit`), en nod i valet blir bara
// aktuell. Ingen fokusflytt, och svaret är ALLTID false så att
// snabbmenyn fyras — med `contextPath()` satt.
bool TreeView::rightDown(const Ctx& c, float mx, float my)
{
    const int row = hitRow(c, mx, my);
    if (row < 0) {
        contextId_ = 0;   // tom yta, remsan, ramen
        return false;
    }
    const Node* n = rows()[std::size_t(row)].n;
    contextId_    = n->id;
    if (!n->selected) {
        HandlerGuard vakt(*this);
        const bool   fyrat = selectFromUser(row, 0u, false);
        if (!vakt.ok())
            return false;   // en handler rev eller lossade trädet
        if (fyrat)
            appWindow()->invalidate();   // en handler har kört (widget.h)
    } else if (currentId_ != n->id) {
        currentId_ = n->id;
        invalidate();   // fokusramen följer den aktuella noden
    }
    return false;
}

bool TreeView::onExpander(const Ctx& c, int row, float mx, float my) const
{
    return !rows()[std::size_t(row)].n->kids.empty() &&
           Draw::project(c, resolve(c), expanderRect(c, row)).contains(mx, my);
}

bool TreeView::onCheckZone(const Ctx& c, int row, float mx, float my) const
{
    return showChecks_ &&
           Draw::project(c, resolve(c), checkZone(c, row)).contains(mx, my);
}

void TreeView::ensureVisible(const TreePath& path)
{
    if (!node(path))
        return;
    // FÖRFÄDERNA FÄLLS UT (WinForms `EnsureVisible`) — en dold nod kan inte
    // rullas fram. Programmatiskt: inga event.
    bool ändrat = false;
    for (std::size_t k = 1; k < path.size(); ++k) {
        Node* f = node(TreePath(path.begin(),
                                path.begin() + std::ptrdiff_t(k)));
        if (f && !f->expanded) {
            f->expanded = true;
            ändrat      = true;
        }
    }
    if (ändrat)
        rowsChanged();
    // Rullningen kräver ett VISAT fönster (renderer och font) — samma
    // tvådelade vakt som ListBox.
    if (!appWindow() || !appWindow()->isOpen())
        return;
    const int row = rowOf(node(path)->id);
    if (row < 0)
        return;
    const Ctx   c   = behaviorCtx();
    const float rh  = c.style->listRowH;
    const float top = float(row) * rh;
    const float h   = scrollViewport(c).h;   // under huvudet
    float       s   = scrollY();
    if (top < s)
        s = top;
    else if (top + rh > s + h)
        s = top + rh - h;
    setScroll(scrollX(), s);   // invaliderar bara vid faktisk ändring
}

// ------------------------------------------------------------- ritningen

gfx::Sizef TreeView::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // INGEN AUTOSTORLEK — ListBoxens beslut (axelregeln, del A): ett
        // träd är en placeringsyta APPEN dimensionerar, och stilen kan inte
        // gissa hur många rader som ska synas.
        return gfx::Sizef{width, height};
    }
}

gfx::Sizef TreeView::contentExtent(const Ctx& c) const
{
    // FYSISKA px (Panels kontrakt). Bredden är VIEWPORTENS: ingen
    // horisontell scroll, så Panels andra overlayaxel aldrig aktiveras —
    // utom med kolumner, där den är kolumnernas, som tabellens.
    const gfx::Rectf in    = innerRect(c);
    const float      rader = float(rows().size()) * c.style->listRowH * c.scale;
    const float      bredd = hasHeader() ? columnsW(c) * c.scale : in.w;
    return gfx::Sizef{bredd, std::max(Panel::contentExtent(c).h, rader)};
}

void TreeView::draw(const Ctx& c)
{
    const gfx::Rectf b    = c.bounds();
    const gfx::Rectf in   = scrollViewport(c);   // raderna, under huvudet
    const auto&      m    = *c.style;
    const auto&      col  = c.theme->colors;
    const bool       på   = isEnabled();
    const auto&      rad  = rows();
    const int        hov  = hovered() && på ? hover_ : -1;
    // Den AKTUELLA noden ramas in när den kan skilja sig från valet — alltså
    // bara med flerval, och bara med fokus (listvyns fokusram).
    const int        aktuell = focused() && på && multiSelect_
                                   ? rowOf(currentId_)
                                   : -1;
    // Radloopen startar vid FÖRSTA SYNLIGA raden och bryter vid nederkanten
    // — O(synliga), ListBoxens svar på tusentals rader.
    const int första =
        m.listRowH > 0.0f
            ? std::clamp(int(scrollY() / m.listRowH), 0, int(rad.size()))
            : 0;

    // RADENS INNEHÅLL — pilen (bara för en nod med barn), kryssrutan (när
    // kryssen syns), ikonen och texten, delat av alla tre ritningarna: det som
    // skiljer familjerna åt är MARKERINGEN runt raden, inte hur en rad är
    // uppbyggd (ListBoxens `drawRow`-beslut). Färgerna in är TEMATS: `fg`
    // radens textfärg, `pil` pilens och `kant` rutans kant — bocken är `fg`
    // (ListBoxens regel). Bara TEXTEN tar nodens färg, och bara på en
    // omarkerad rad i ett aktivt träd: markeringens färg vinner, och en
    // röd bock hade lästs som ett fel i kryssets eget tillstånd. Ikonen och
    // texten är ETT block (`Draw::content`) mätt till sin egen bredd, så det
    // står vänsterställt; en inaktiv ikon tonas med `iconFade` som på
    // knapparna. MED KOLUMNER kortas texten mot kolumn 0:s kant,
    // och cellerna står i sina kolumner i nodens textfärg — tabellens
    // `drawCell`: cellens luft, ellipsen och kolumnens justering.
    auto innehåll = [&](int i, bool vald, gfx::Color fg, gfx::Color pil,
                        gfx::Color kant) {
        const Row&       r  = rad[std::size_t(i)];
        const gfx::Rectf rr = rowRect(c, i);
        if (!r.n->kids.empty()) {
            const gfx::Rectf e     = expanderRect(c, i);
            const bool       ut    = r.n->expanded;
            const float      halvW = ut ? m.chevronW : m.chevronH;
            const float      halvH = ut ? m.chevronH : m.chevronW;
            Draw::chevron(c,
                          {e.x + e.w / 2.0f - halvW, e.y + e.h / 2.0f - halvH,
                           2.0f * halvW, 2.0f * halvH},
                          ut ? Dir::Down : Dir::Right, Pen{pil, 0.0f});
        }
        if (showChecks_) {
            const gfx::Rectf cr = checkRect(c, i);
            Draw::frame(c, cr, m.checkRadius, Pen{kant, m.borderWidth});
            if (r.n->checked)
                Draw::check(c, cr, Pen{fg, m.checkStroke});
        }
        const gfx::Color text = !på || vald ? fg : r.n->color.value_or(fg);
        const float      k    = r.n->img.sizeClass();
        Content          ct{r.n->img,   r.n->text.str(),
                            text,       ImagePos::Left,
                            m.iconGap,  k > 0.0f ? k : m.iconFallback};
        if (!på)
            ct.imgOpacity = 1.0f - m.iconFade;
        const float x = contentX(c, i);
        if (hasHeader()) {
            const float      pad = cellPadX(c);
            const gfx::Rectf k0  = cellRect(c, i, 0);
            const float      bild =
                r.n->img.valid() ? ct.iconSide + ct.gap : 0.0f;
            ct.text = Draw::elide(c, ct.text, k0.x + k0.w - pad - x - bild);
            for (int kol = 1; kol < columnCount(); ++kol) {
                const gfx::Rectf cr = cellRect(c, i, kol);
                if (cr.x > in.x + in.w)
                    break;
                const float w = cr.w - 2.0f * pad;
                if (cr.x + cr.w < in.x || w <= 0.0f ||
                    std::size_t(kol) > r.n->cells.size())
                    continue;
                Draw::textElided(c, {cr.x + pad, cr.y, w, cr.h},
                                 r.n->cells[std::size_t(kol - 1)], text,
                                 columns()[std::size_t(kol)].align);
            }
        }
        Draw::content(c, {x, rr.y, Draw::measure(c, ct).w, rr.h}, ct);
    };

    switch (c.ui) {
    // GLAZE — ListBoxens recept rakt av: FÄLTETS yta (`panel`, BESTÄMD kontur
    // `buttonBorder`), den VALDA raden i det grindade paret `accentSurface`/
    // `onAccent` och ingen laminering (en rad har ingen kontur — skälet står
    // i listbox.cpp, 2.91:1 mätt). Pilen är en lugn markör i `textDim`, och
    // på den valda raden följer den radens text. Kryssrutans kant är
    // `buttonBorder`, på den valda raden radens text (ListBoxens).
    //
    //   vila     ingen radfyllning, text `text` eller nodens färg, pil
    //            `textDim`
    //   hover    `buttonHover` (SVAG — pekaren är inte ett val)
    //   vald     `accentSurface`-rad, text och pil `onAccent`
    //   fokus    fristående ring utanför kanten; med flerval en hårlinje
    //            runt den AKTUELLA raden i radens textfärg
    //   inaktiv  markeringen faller till `buttonDisabled`, text `textDim`,
    //            pil `border`, ikonerna tonade
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, in);
        for (int i = första; i < int(rad.size()); ++i) {
            const gfx::Rectf r = rowRect(c, i);
            if (r.y > in.y + in.h)
                break;
            const bool ärVald = rad[std::size_t(i)].n->selected;
            if (ärVald)
                Draw::rect(c, r, 0.0f, på ? col.accentSurface
                                          : col.buttonDisabled);
            else if (i == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på      ? col.textDim
                                  : ärVald ? col.onAccent
                                           : col.text;
            innehåll(i, ärVald, fg, ärVald ? fg : på ? col.textDim : col.border,
                     ärVald ? fg : på ? col.buttonBorder : col.border);
        }
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{rad[std::size_t(aktuell)].n->selected ? col.onAccent
                                                                  : col.text,
                            m.borderWidth});
        if (dropEffect() != DragEffect::None)
            drawDropIndicator(c, col.accent);
        Draw::endClip(c);
        drawHeader(c);   // efter raderna (inget utan kolumner)
        Draw::frame(c, b, 0.0f,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        break;

    // MINIMALISTIC — "Outline" i trädform, ListBoxens recept: INGEN yta i
    // vila och ingen radfyllning för valet, som markeras med ett accent-
    // STRECK i vänsterkanten (`rowMarkerW`) och accentfärgad text. En rad
    // är ingen kontroll, så "fyllning betyder tillstånd" får inte lånas.
    // Kryssrutans kant är alltid `buttonBorder` (ListBoxens).
    //
    //   vila     ingen yta, ingen radfyllning, text `text` eller nodens
    //            färg, pil `textDim`
    //   hover    `buttonHover` innanför trädets kant
    //   vald     accent-markör i vänsterkanten, text och pil `accent`
    //   fokus    fristående ring (stilens halva tjocklek); med flerval en
    //            hårlinje runt den aktuella raden
    //   inaktiv  markör, text och pil i `textDim`/`border`, kanten `border`
    case UI::Minimalistic:
        Draw::clip(c, in);
        for (int i = första; i < int(rad.size()); ++i) {
            const gfx::Rectf r = rowRect(c, i);
            if (r.y > in.y + in.h)
                break;
            const bool ärVald = rad[std::size_t(i)].n->selected;
            if (i == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på      ? col.textDim
                                  : ärVald ? col.accent
                                           : col.text;
            if (ärVald)
                Draw::rect(c, {r.x, r.y, m.rowMarkerW, r.h}, 0.0f, fg);
            innehåll(i, ärVald, fg, ärVald ? fg : på ? col.textDim : col.border,
                     på ? col.buttonBorder : col.border);
        }
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{rad[std::size_t(aktuell)].n->selected ? col.accent
                                                                  : col.text,
                            m.borderWidth});
        if (dropEffect() != DragEffect::None)
            drawDropIndicator(c, col.accent);
        Draw::endClip(c);
        drawHeader(c);   // efter raderna (inget utan kolumner)
        Draw::frame(c, b, 0.0f,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        break;

    // SLATE — ett FÄLT, ListBoxens recept: `panel`-
    // yta, hårlinje `border`, och den VALDA raden fylls med `accent` medan
    // texten och pilen går till `windowBg`. Kryssrutans kant som Glazes.
    //
    //   vila     ingen radfyllning, text `text` eller nodens färg, pil
    //            `textDim`
    //   hover    `buttonHover`
    //   vald     `accent`-rad, text och pil `windowBg`
    //   fokus    fristående ring utanför kanten; med flerval en hårlinje
    //            runt den aktuella raden
    //   inaktiv  markeringen faller till `buttonDisabled`, text `textDim`,
    //            pil `border`, ikonerna tonade
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, in);
        for (int i = första; i < int(rad.size()); ++i) {
            const gfx::Rectf r = rowRect(c, i);
            if (r.y > in.y + in.h)
                break;
            const bool ärVald = rad[std::size_t(i)].n->selected;
            if (ärVald)
                Draw::rect(c, r, 0.0f, på ? col.accent : col.buttonDisabled);
            else if (i == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på      ? col.textDim
                                  : ärVald ? col.windowBg
                                           : col.text;
            innehåll(i, ärVald, fg, ärVald ? fg : på ? col.textDim : col.border,
                     ärVald ? fg : på ? col.buttonBorder : col.border);
        }
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{rad[std::size_t(aktuell)].n->selected ? col.windowBg
                                                                  : col.text,
                            m.borderWidth});
        if (dropEffect() != DragEffect::None)
            drawDropIndicator(c, col.accent);
        Draw::endClip(c);
        drawHeader(c);   // efter raderna (inget utan kolumner)
        Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
        break;
    }

    if (focused() && på)
        Draw::frame(c, Draw::inset(b, -m.focusOffset), m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

// INSÄTTNINGSMARKERINGEN — trädets egen (`ownsDropIndicator`), för den
// säger VAR lasten hamnar och inte bara ATT trädet tar emot: en RAM runt
// raden för "in i", annars en LINJE i radens över- eller underkant, indragen
// till den nivå lasten hamnar på. Färgen och tjockleken är ramverkets
// släppmarkering (`accent`, `dropLineW`) — lika i alla tre familjerna, som
// ramen drag-and-drop ritar runt andra mål.
void TreeView::drawDropIndicator(const Ctx& c, gfx::Color col) const
{
    const auto&      m  = *c.style;
    const gfx::Rectf in = scrollViewport(c);   // raderna, under huvudet
    const Pen        pen{col, m.dropLineW};
    if (drop_.index < 0)
        return;   // ingen plats — appen kan ändå ha tagit emot
    if (drop_.row < 0) {
        // Tomt träd: lasten blir första toppnoden — linjen i överkanten.
        Draw::line(c, in.x, in.y + m.dropLineW / 2.0f, in.x + in.w,
                   in.y + m.dropLineW / 2.0f, pen);
        return;
    }
    const gfx::Rectf r = rowRect(c, drop_.row);
    if (drop_.into) {
        Draw::frame(c, Draw::inset(r, m.dropLineW / 2.0f), 0.0f, pen);
        return;
    }
    // Indraget följer kolumn 0 i sidled, som pilen (`expanderRect`).
    const float x = r.x - scrollXClamped(c) + m.listPadX +
                    float(drop_.depth) * m.treeIndent;
    const float y = drop_.before ? r.y : r.y + r.h;
    Draw::line(c, x, y, r.x + r.w, y, pen);
}

// --------------------------------------------------------- användarvägarna
// De ENDA platser som fyrar event (`Checkbox::clicked`-prejudikatet).
// `ListView::selectFromUser` är förlagan för valet.

bool TreeView::selectFromUser(int row, std::uint32_t mods, bool fromKey)
{
    const auto& r = rows();
    if (row < 0 || std::size_t(row) >= r.size())
        return false;
    const std::uint64_t id = r[std::size_t(row)].n->id;
    // KOPIA av stigen: `ensureVisible` kan bygga om radcachen, och en
    // referens in i den hade dinglat mitt i anropet.
    const TreePath stig  = r[std::size_t(row)].path;
    const bool     shift = multiSelect_ && (mods & gfx::kModShift);
    // Ctrl ELLER Cmd: på macOS är Ctrl+klick högerklick, så Cmd är vändaren
    // där — och att båda räknas överallt kostar ingenting.
    const bool vänd = multiSelect_ && (mods & (gfx::kModCtrl | gfx::kModCmd));

    bool ändrat = false;
    if (shift) {
        // SPANNET i SYNLIG ordning, från ankaret (eller den aktuella noden,
        // eller raden själv — när de inte syns) till hit. Ctrl+Shift+klick
        // LÄGGER TILL spannet; annars ersätter det valet, också de valda i
        // en stängd gren (listvyns semantik).
        int a = rowOf(anchorId_);
        if (a < 0)
            a = rowOf(currentId_);
        if (a < 0)
            a = row;
        const std::uint64_t ankare = r[std::size_t(a)].n->id;
        std::vector<std::uint64_t> ids =
            vänd && !fromKey ? selectedIds() : std::vector<std::uint64_t>{};
        for (int i = std::min(a, row); i <= std::max(a, row); ++i)
            ids.push_back(r[std::size_t(i)].n->id);
        ändrat    = assignSelection(std::move(ids));
        anchorId_ = ankare;
    } else if (vänd) {
        // Ctrl+pil flyttar bara den aktuella noden — valet står kvar, och
        // Ctrl+Space vänder därefter noden. Ctrl+klick vänder den direkt.
        if (!fromKey) {
            Node* n     = node(stig);
            n->selected = !n->selected;
            ändrat      = true;
            invalidate();
        }
        anchorId_ = id;
    } else {
        ändrat    = assignSelection({id});
        anchorId_ = id;
    }
    if (currentId_ != id) {
        currentId_ = id;
        invalidate();   // fokusramen följer den aktuella noden
    }
    // En REN valhändelse är ingen utfällning: `changedPath` släpper sitt
    // gamla värde (ListBoxens `changedRow`-kontrakt).
    changedPath_.clear();
    ensureVisible(stig);
    if (!ändrat)
        return false;
    firePair(onChange, onCommit);   // samma val; läses ur sender
    return true;
}

bool TreeView::toggleFromUser(int row)
{
    const auto& r = rows();
    if (row < 0 || std::size_t(row) >= r.size() ||
        r[std::size_t(row)].n->kids.empty())
        return true;   // ett löv har inget att fälla
    const TreePath stig = r[std::size_t(row)].path;   // KOPIA — cachen byggs om
    Node*          n    = node(stig);
    const bool     ut   = !n->expanded;
    n->expanded         = ut;
    changedPath_        = stig;

    // En ihopfällning som GÖMDE valda noder (enkelvalets regel
    // generaliserad): ättlingarna lämnar valet, noden själv väljs om någon av
    // dem var vald, och den aktuella noden och ankaret flyttar upp till den
    // — Windows trädvy, och annars stod tangentbordet kvar på en rad som
    // inte syns. Det är ANVÄNDARENS handling, så `onChange` fyras också.
    bool valFlyttat = false;
    if (!ut) {
        bool någonVald = false;
        bool aktuellIn = false;
        bool ankareIn  = false;
        forEachNode(n->kids, [&](Node& k) {
            if (k.selected) {
                k.selected = false;
                någonVald  = true;
            }
            aktuellIn = aktuellIn || k.id == currentId_;
            ankareIn  = ankareIn || k.id == anchorId_;
        });
        if (någonVald) {
            n->selected = true;
            valFlyttat  = true;
        }
        if (aktuellIn)
            currentId_ = n->id;
        if (ankareIn)
            anchorId_ = n->id;
    }
    rowsChanged();

    // Handlers SIST, och ingen pekare in i trädet efter dem: ett LATT träd
    // fyller på barnen i `onExpand`, så noden kan ha flyttat i minnet. Och
    // vakten emellan: en handler får riva eller flytta trädet, och
    // då fyras inte det flyttade valets onChange/onCommit.
    const bool lever = ut ? fireEvent(onExpand) : fireEvent(onCollapse);
    if (lever && valFlyttat) {
        changedPath_.clear();   // valhändelsen bär ingen nod (headern)
        return firePair(onChange, onCommit);   // det flyttade valet — klart
    }
    return lever;
}

// KRYSSET (förlagan `ListBox::toggleCheckFromUser`): vänd, och `onCheck`
// följt av `onCommit` för samma kryss — stigen SÄTTS IGEN emellan, för en
// onCheck-handler som fyller om trädet har nollat den. Valet rörs inte.
bool TreeView::toggleCheckFromUser(int row)
{
    const auto& r = rows();
    if (row < 0 || std::size_t(row) >= r.size())
        return true;
    const TreePath stig = r[std::size_t(row)].path;   // KOPIA — handlern
    Node*          n    = node(stig);
    n->checked          = !n->checked;   // INGEN kaskad
    changedPath_        = stig;
    invalidate();
    if (!fireEvent(onCheck))   // noden läses ur sender: changedPath()
        return false;
    changedPath_ = stig;
    return fireEvent(onCommit);
}

void TreeView::activateFromUser()
{
    TreePath p;
    if (!onActivate || !pathOf(currentId_, p))
        return;
    ActivateArgs e;
    onActivate(this, e);   // noden läses ur sender: currentPath()
}

// ------------------------------------------------------------- beteendet

void TreeView::setHoverRow(int row)
{
    if (row == hover_)
        return;
    hover_ = row;
    invalidate();   // hård regel 1: bara vid ÄNDRING
}

// Trycket, i den här ordningen: ett gammalt väntande
// soloval släpps, remsan och kolumnhuvudet först (basens `headerMouseDown` —
// remsa-först, avdelaren, rubriken), PILEN fäller, RUTANS ZON kryssar (och
// flyttar inte valet), ett tryck på en REDAN VALD nod i ett flerval utan
// modifierare väntar på släppet — och allt annat VÄLJER. Träffarna går via
// SAMMA geometri som ritningen, projicerad till musens fysiska rymd.
void TreeView::mouseDown(const Ctx& c, float mx, float my)
{
    pendingSoloId_ = 0;
    if (headerMouseDown(c, mx, my))
        return;
    const int row = rowAt(c, my);
    if (row < 0)
        return;
    if (onExpander(c, row, mx, my)) {
        toggleFromUser(row);
        return;
    }
    if (onCheckZone(c, row, mx, my)) {
        toggleCheckFromUser(row);
        return;
    }
    const AppWindow*    w    = appWindow();
    const std::uint32_t mods = w ? w->modifierKeys() : 0u;
    const Node*         n    = rows()[std::size_t(row)].n;
    const std::uint32_t val  = gfx::kModCtrl | gfx::kModCmd | gfx::kModShift;
    if (multiSelect_ && n->selected && !(mods & val)) {
        // SOLO VID SLÄPPET (Utforskarens och Finders regel): trycket
        // flyttar bara den aktuella noden — annars hade det aldrig gått att
        // dra ett helt val, för trycket hade valt bort de andra först.
        pendingSoloId_ = n->id;
        if (currentId_ != n->id) {
            currentId_ = n->id;
            invalidate();
        }
        return;
    }
    selectFromUser(row, mods, false);
}

bool TreeView::mouseUp(const Ctx& c, float mx, float my)
{
    const bool          bas = ColumnPanel::mouseUp(c, mx, my);   // huvudet först
    const std::uint64_t id  = pendingSoloId_;
    pendingSoloId_          = 0;
    // En nod som tagits bort mellan tryck och släpp — eller fällts undan —
    // har ingen rad, och då händer ingenting.
    const int row = rowOf(id);
    if (row < 0)
        return bas;
    return selectFromUser(row, 0u, false) || bas;
}

// Det ANDRA trycket har redan gått genom `mouseDown` (widget.h): på raden
// har det gjort noden aktuell, på PILEN har det fällt en gång till och i
// RUTAN kryssat en gång till — två tryck, två ändringar, och ingenting kvar
// att göra här.
void TreeView::mouseDoubleClick(const Ctx& c, float mx, float my)
{
    if (headerDoubleClick(c, mx, my))
        return;   // remsan eller huvudet — en avdelare anpassar sin kolumn
    const int row = rowAt(c, my);
    if (row < 0 || onExpander(c, row, mx, my) || onCheckZone(c, row, mx, my) ||
        rows()[std::size_t(row)].n->id != currentId_)
        return;
    if (toggleFromUser(row))   // ett löv har inget att fälla — bara aktivering
        activateFromUser();
}

void TreeView::mouseMove(const Ctx& c, float mx, float my)
{
    ColumnPanel::mouseMove(c, mx, my);   // huvudets hover och remsornas tumme
    // På remsan är pekaren inte på en rad; i huvudet svarar `rowAt` −1.
    setHoverRow(inScrollStrip(c, mx, my) ? -1 : rowAt(c, my));
}

bool TreeView::mouseWheel(const Ctx& c, float, float, float wheelX,
                          float wheelY)
{
    // Hjulet stegar i RADER (ListBoxens beslut) och bubblar vidare när det
    // inte finns något att rulla eller kanten är nådd. I sidled — bara med
    // kolumner, utan dem är spelrummet 0 — i Panels `wheelStep`, tabellens.
    const gfx::Sizef spelrum = scrollRange(c);
    const float      nx      = std::clamp(
        scrollXClamped(c) - wheelScrollPx(c, wheelX, c.style->wheelStep), 0.0f,
        spelrum.w);
    const float ny = spelrum.h > 0.0f
                         ? std::clamp(scrollY() - wheelScrollPx(
                                                      c, wheelY,
                                                      c.style->listRowH),
                                      0.0f, spelrum.h)
                         : scrollY();
    if (nx == scrollXClamped(c) && ny == scrollY())
        return false;
    setScroll(nx, ny);
    return true;
}

bool TreeView::keyDown(gfx::Key k, std::uint32_t mods)
{
    if (mods & gfx::kModAlt)
        return false;   // Alt är menyernas och appens
    const bool styr = mods & (gfx::kModCtrl | gfx::kModCmd);
    // Utan flerval har Ctrl ingen mening här — tangenten lämnas åt
    // genvägarna (som ListBox och listvyn gör).
    if (styr && !multiSelect_)
        return false;
    const auto& r = rows();
    const int   n = int(r.size());
    if (n == 0)
        return false;
    const int akt = rowOf(currentId_);   // −1: ingen aktuell, eller dold

    switch (k) {
    case gfx::Key::Up:
        selectFromUser(akt < 0 ? 0 : std::max(akt - 1, 0), mods, true);
        return true;
    case gfx::Key::Down:
        selectFromUser(akt < 0 ? 0 : std::min(akt + 1, n - 1), mods, true);
        return true;
    case gfx::Key::Home:
        selectFromUser(0, mods, true);
        return true;
    case gfx::Key::End:
        selectFromUser(n - 1, mods, true);
        return true;
    case gfx::Key::Left: {
        // UTFÄLLD med barn ⇒ fäll ihop; annars upp till FÖRÄLDERN — som i
        // dag, och med Ctrl bubblar den. Navigeringen väljer noden ensam.
        if (styr)
            return false;
        if (akt < 0) {
            selectFromUser(0, 0u, true);
            return true;
        }
        const Row& rr = r[std::size_t(akt)];
        if (rr.n->expanded && !rr.n->kids.empty()) {
            toggleFromUser(akt);
        } else if (rr.path.size() > 1) {
            const Node* f = node(föräldernTill(rr.path));
            selectFromUser(rowOf(f->id), 0u, true);
        }
        return true;
    }
    case gfx::Key::Right: {
        // HOPFÄLLD med barn ⇒ fäll ut; utfälld ⇒ ner till FÖRSTA BARNET.
        if (styr)
            return false;
        if (akt < 0) {
            selectFromUser(0, 0u, true);
            return true;
        }
        const Row& rr = r[std::size_t(akt)];
        if (rr.n->kids.empty())
            return true;   // ett löv — inget att göra, men tangenten är vår
        if (!rr.n->expanded)
            toggleFromUser(akt);
        else
            selectFromUser(akt + 1, 0u, true);   // första barnet är nästa rad
        return true;
    }
    case gfx::Key::Space:
        // Med synliga kryss vänder Space KRYSSET —
        // också i enkelval, och före valet; Ctrl+Space vänder VALET (bara i
        // flerval — enkelvalet har bubblat ovan); utan kryss väljer Space den
        // aktuella noden i ett flerval och bubblar i ett enkelval.
        if (akt < 0)
            return false;
        if (showChecks_ && !styr) {
            toggleCheckFromUser(akt);
            return true;
        }
        if (styr) {
            selectFromUser(akt, gfx::kModCtrl, false);   // vänd noden
            return true;
        }
        if (!multiSelect_)
            return false;
        if (!r[std::size_t(akt)].n->selected)
            selectFromUser(akt, 0u, true);
        return true;
    case gfx::Key::A:
        if (!styr)
            return false;
        // Ctrl+A är en ANVÄNDARväg: `selectAll`s val (de SYNLIGA noderna),
        // men med event när valet faktiskt ändrades.
        if (assignSelection(visibleIds())) {
            changedPath_.clear();
            firePair(onChange, onCommit);
        }
        return true;
    case gfx::Key::Return:
        // AKTIVERINGEN, som listvyns: tangenten är vår när en SYNLIG nod är
        // aktuell OCH `onActivate` är satt — annars gör den ingenting här och
        // bubblar vidare, så en dialog utan aktivering når sin standardknapp
        // Enter fäller INTE; det gör Vänster/Höger.
        if (akt < 0 || !onActivate)
            return false;
        activateFromUser();
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------- DRAG-AND-DROP
//
// Kontraktet står i widget.h och i tree_view.h; här är det inbyggda. Varje
// krok utom `drop` körs FÖRE appens handler med SAMMA args, så det trädet
// skriver i `e.data`/`e.effect` är vad appen ser — och får ändra. SLÄPPET
// frågar appen först: `drop` körs efter `onDrop`, bara när appen varken nekade
// eller tog över, och utför exakt den plats appen såg.

// VAR FÅR ETT DRAG BÖRJA? Aldrig från remsan (den rullar), kolumnhuvudet,
// pilen (den fäller) eller rutans zon (den kryssar) — SAMMA frågor som
// `mouseDown` ställer, och
// före all last, så regeln håller också när appens `onDragStart` fyller den.
// Nedanför sista raden finns ingen nod men inget hinder heller: lasten är då
// appens (`dragStart` lämnar den tom).
bool TreeView::canDragFrom(const Ctx& c, float x, float y) const
{
    if (inScrollStrip(c, x, y) || inHeader(c, x, y))
        return false;   // huvudet sorterar och drar avdelare
    const int row = rowAt(c, y);
    return row < 0 || (!onExpander(c, row, x, y) && !onCheckZone(c, row, x, y));
}

// KÄLLAN: tröskeln är passerad (och `canDragFrom` har svarat ja). Lasten är
// HELA VALET (beskuret till sina översta noder) när den tryckta noden är
// vald, annars den tryckta noden. Ett drag får ingen `mouseUp`, så ett
// väntande soloval släpps här. Ingen nod under trycket ⇒ ingen inbyggd last.
void TreeView::dragStart(const Ctx& c, DragStartArgs& e)
{
    dragIds_.clear();
    pendingSoloId_ = 0;
    const int row = rowAt(c, e.y);
    if (row < 0)
        return;
    const Row&                 r   = rows()[std::size_t(row)];
    std::vector<std::uint64_t> ids = r.n->selected
                                         ? topLevelSelected()
                                         : std::vector<std::uint64_t>{r.n->id};
    std::string stigar, texter;
    for (const std::uint64_t id : ids) {
        TreePath p;
        pathOf(id, p);
        if (!stigar.empty()) {
            stigar += ';';
            texter += '\n';
        }
        stigar += stigText(p);
        texter += node(p)->text.str();
    }
    e.data.set(kNodeFormat, stigar);
    e.data.setText(texter);
    // KOPIERA eller FLYTTA: en omordning är en flytt, men ett mål som bara
    // läser text får gärna ta en kopia — då står noderna kvar.
    e.allowed = DragEffect::Copy | DragEffect::Move;
    // DRAGBILDEN är den TRYCKTA nodens text och bild (antalet visas inte —
    // skriven gräns i headern).
    e.text   = r.n->text.str();
    e.img    = r.n->img;
    dragIds_ = std::move(ids);
}

// Draget är slut. En FLYTT till ett ANNAT mål betyder att noderna LÄMNAR
// trädet (widget.h: `Move` = posten lämnar källan). En flytt inom trädet
// är redan utförd i `drop`, och målet är då vi själva. Noderna slås upp via
// id, för varje borttagning flyttar de andras stigar.
void TreeView::dragEnd(DragEndArgs& e)
{
    const std::vector<std::uint64_t> ids = std::move(dragIds_);
    dragIds_.clear();
    if (e.effect != DragEffect::Move || e.target == this)
        return;
    for (const std::uint64_t id : ids) {
        TreePath p;
        if (pathOf(id, p))
            removeNode(p);
    }
}

// Är lasten VÅRA EGNA noder? Källan är trädet, lasten bär nodformatet och
// trädets egen `dragStart` fyllde den (`dragIds_`). Bara då tar det inbyggda
// emot — ett släpp från en annan källa är appens att avgöra.
bool TreeView::ownPayload(const DragArgs& e) const
{
    return e.source == this && e.data.has(kNodeFormat) && !dragIds_.empty();
}

// Lever ALLA dragna noder? En handler som tog bort en av dem — eller fyllde
// om trädet med `setNodes` — har gjort flytten omöjlig.
bool TreeView::draggedAlive() const
{
    for (const std::uint64_t id : dragIds_) {
        TreePath p;
        if (!pathOf(id, p))
            return false;
    }
    return true;
}

bool TreeView::acceptsDropInto(const TreePath& parent) const
{
    const Node* n = node(parent);
    return parent.empty() || (n && n->allowDropInto);
}

bool TreeView::insideDragged(const TreePath& parent) const
{
    for (const std::uint64_t id : dragIds_) {
        TreePath från;
        if (pathOf(id, från) && ärFörfaderEllerSamma(från, parent))
            return true;
    }
    return false;
}

// DE DRAGNA NODERNA — giltiga så länge SESSIONEN säger att trädet är
// dragets källa (`isDragSource`: från tröskeln, alltså redan i `onDragStart`,
// till och med slutet). Ett drag som avböjdes eller avbröts utan att
// `dragEnd` frågades lämnar `dragIds_` kvar, och då är det sessionens svar
// som håller dem osynliga. Stigen jämförs
// lexikografiskt, och det ÄR trädordningen — samma regel som `moveIds`.
std::vector<TreePath> TreeView::draggedPaths() const
{
    std::vector<TreePath> ut;
    if (dragIds_.empty() || !isDragSource())
        return ut;
    for (const std::uint64_t id : dragIds_) {
        TreePath p;
        if (pathOf(id, p))
            ut.push_back(std::move(p));
    }
    std::sort(ut.begin(), ut.end());
    return ut;
}

TreePath TreeView::dropParent() const
{
    TreePath förälder;
    int      index = -1;
    return resolveDrop(förälder, index) ? förälder : TreePath{};
}

int TreeView::dropIndex() const
{
    TreePath förälder;
    int      index = -1;
    return resolveDrop(förälder, index) ? index : -1;
}

// VAR HAMNAR LASTEN? Raden under pekaren delas i tre: översta och understa
// `treeDropEdge` av höjden betyder FÖRE respektive EFTER noden, mitten IN
// I den. Under en UTFÄLLD nod med barn betyder "efter" i stället FÖRST
// BLAND BARNEN — linjen står där mellan noden och dess första barn, och
// det är där lasten syns hamna. Under sista raden: sist bland toppnoderna.
//
// REGELN PER NOD: en nod som säger nej till "in i" har ingen
// mittzon — raden delas på mitten mellan före och efter, och "efter" under
// den är alltid platsen EFTER noden, också när den är utfälld (först bland
// barnen vore in i den). Och en plats vars FÖRÄLDER säger nej finns inte:
// `index` −1, ingen markering (barnen till en sådan nod, lagda med kod).
//
// LUCKAN SPARAS SOM ID:N: föräldern räknas fram
// som stig här och nu, men det som sparas är dess id och grannarnas på var
// sida av luckan — `resolveDrop` hittar dem igen efter att `onDrop` ändrat
// trädet.
TreeView::DropSpot TreeView::dropSpotAt(const Ctx& c, float my) const
{
    DropSpot         s;
    TreePath         förälder;   // luckans förälder — `{}` är roten
    const auto&      r    = rows();
    const gfx::Rectf base = resolve(c);
    const gfx::Rectf in   = scrollViewport(c);   // under huvudet
    const float      h    = c.style->listRowH;
    const float      off  = Draw::localY(c, base, my) - in.y + scrollY();
    const int        n    = int(r.size());
    const int        row  = h > 0.0f ? int(std::floor(off / h)) : n;
    if (n == 0 || row >= n) {
        s.index  = int(roots_.size());
        s.row    = n - 1;   // −1 i ett tomt träd: linjen i överkanten
        s.depth  = 0;
        s.before = false;
    } else {
        const int   i    = std::max(row, 0);
        const Row&  rr   = r[std::size_t(i)];
        const float inom = off - float(i) * h;
        const bool  tar  = rr.n->allowDropInto;   // har raden en mittzon?
        const float kant = tar ? c.style->treeDropEdge * h : h / 2.0f;
        s.row = i;
        if (inom < kant) {
            förälder = föräldernTill(rr.path);
            s.index  = rr.path.back();
            s.depth  = rr.depth;
            s.before = true;
        } else if (inom > h - kant || !tar) {
            if (tar && rr.n->expanded && !rr.n->kids.empty()) {
                förälder = rr.path;
                s.index  = 0;
                s.depth  = rr.depth + 1;
            } else {
                förälder = föräldernTill(rr.path);
                s.index  = rr.path.back() + 1;
                s.depth  = rr.depth;
            }
            s.before = false;
        } else {
            förälder = rr.path;
            s.index  = int(rr.n->kids.size());
            s.depth  = rr.depth + 1;
            s.into   = true;
        }
    }
    // En förälder som tar emot FINNS (roten, eller en nod med `allowDropInto`
    // — `acceptsDropInto` slår upp den), så barnlistan nedan finns också.
    if (!acceptsDropInto(förälder))
        return DropSpot{};
    const Node*              f      = node(förälder);
    const std::vector<Node>& syskon = *kids(förälder);
    const std::size_t        plats  = std::size_t(s.index);
    s.parentId = f ? f->id : 0;
    s.prevId   = plats > 0 ? syskon[plats - 1].id : 0;
    s.nextId   = plats < syskon.size() ? syskon[plats].id : 0;
    return s;
}

// LUCKAN LÖST NU: föräldern ur sitt id och
// platsen ur grannarnas — inte det index som räknades när pekaren stod där.
// Har `onDrop` lagt in eller tagit bort syskon FÖRE luckan står den på ett
// annat index i dag, och det är det som svaras; har luckan själv ändrats
// (grannen före är borta, något står insatt emellan, föräldern är borta)
// finns den inte längre, och svaret är false.
bool TreeView::resolveDrop(TreePath& parent, int& index) const
{
    if (drop_.index < 0)
        return false;   // ingen plats här
    TreePath förälder;
    if (drop_.parentId != 0 && !pathOf(drop_.parentId, förälder))
        return false;
    // Barnlistan finns: roten, eller noden `pathOf` just hittade.
    const std::vector<Node>& syskon = *kids(förälder);
    std::size_t              i      = 0;
    if (drop_.prevId != 0) {
        const auto före =
            std::find_if(syskon.begin(), syskon.end(), [this](const Node& n) {
                return n.id == drop_.prevId;
            });
        if (före == syskon.end())
            return false;
        i = std::size_t(före - syskon.begin()) + 1;
    }
    const std::uint64_t efter = i < syskon.size() ? syskon[i].id : 0;
    if (efter != drop_.nextId)
        return false;
    parent = std::move(förälder);
    index  = int(i);
    return true;
}

// MÅLET: pekaren kom in respektive rör sig. Platsen räknas om och ritas om
// BARA när den ändrats (regel 1 — dragOver körs per musrörelse). Egna noder
// FÖRESLÅS som en FLYTT, utom när målet ligger INUTI någon av dem eller en av
// dem har försvunnit; appens `onDragOver` får därefter säga nej. En främmande
// last lämnas åt appen (effekten rörs inte).
void TreeView::dragEnter(const Ctx& c, DragArgs& e)
{
    dragOver(c, e);
}

void TreeView::dragOver(const Ctx& c, DragArgs& e)
{
    const DropSpot s = dropSpotAt(c, e.y);
    if (s != drop_) {
        drop_ = s;
        invalidate();
    }
    if (!ownPayload(e))
        return;
    e.effect = drop_.index >= 0 && draggedAlive() &&
                       !insideDragged(dropParent()) &&
                       allows(e.allowed, DragEffect::Move)
                   ? DragEffect::Move
                   : DragEffect::None;
}

// Lämnat, avvisat, avbrutet — eller städningen efter ett lyckat släpp.
// Släcker markeringen; `dragIds_` står kvar, källan behöver dem i `dragEnd`.
void TreeView::dragLeave()
{
    if (drop_.index < 0 && drop_.row < 0)
        return;
    drop_ = DropSpot{};
    invalidate();
}

// SLÄPPET — körs EFTER appens `onDrop`, och bara när
// appen varken nekade eller satte `handled`. Platsen räknas INTE om ur
// pekaren: det är luckan appen såg, löst ur sina id:n (`resolveDrop`), så ett
// träd som handlern ändrat flyttar aldrig tyst till en annan plats. De egna
// noderna FLYTTAS dit i trädordning (`moveIds`). Bara en FLYTT är trädets —
// en kopia (`Copy`) är appens att göra — och går flytten inte (en
// dragen nod borta, luckan borta, ett mål inuti, en förälder som säger nej)
// blir utfallet `None`, som källan får veta. En flytt IN I en hopfälld nod
// fäller ut den, så noderna syns där de hamnade. Valet och krysset är
// nodernas flaggor och följer med — utan event, det var ett drag och inget
// val.
void TreeView::drop(const Ctx&, DragArgs& e)
{
    if (!ownPayload(e) || e.effect != DragEffect::Move)
        return;
    TreePath förälder;
    int      index = -1;
    if (!draggedAlive() || !resolveDrop(förälder, index) ||
        insideDragged(förälder) || !acceptsDropInto(förälder) ||
        !moveIds(dragIds_, förälder, index)) {
        e.effect = DragEffect::None;   // gick inte — källan får veta det
        return;
    }
    if (drop_.into && drop_.parentId != 0) {
        TreePath p;
        if (pathOf(drop_.parentId, p) && !node(p)->expanded) {
            node(p)->expanded = true;
            rowsChanged();
        }
    }
    TreePath ny;
    if (pathOf(dragIds_.front(), ny))
        ensureVisible(ny);
}

} // namespace glaze
