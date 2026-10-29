// drag_drop.cpp — DRAG-AND-DROP: lasten (`DragData`),
// sessionen i `AppWindow` och återkopplingen i overlay-lagret.
//
// API:t står i glaze/widget.h (typerna, egenskaperna, eventen och krokarna)
// och sessionens deklarationer i glaze/app_window.h. Den här filen är
// MEKANIKEN, och den har tre delar som hålls isär:
//
//   1. LASTEN — en ordnad mängd format → bytes. Ren data, inget fönster.
//   2. SESSIONEN — tillståndsmaskinen Idle → Pending → Active → Finishing,
//      driven av `AppWindow::handleEvent` med en rad per händelsetyp. Den
//      äger tröskeln, var ett drag får börja (`canDragFrom`), målvalet,
//      ordningen mellan krokar och handlers — kroken FÖRESLÅR först i varje
//      steg, utom i SLÄPPET, där appen frågas först och det inbyggda körs
//      efter — och att en handler som river trädet
//      mitt i ett drag inte river oss. Dessutom AUTOSKROLLEN:
//      kedjan under pekaren frågas med
//      `Widget::dragScroll`, och fönstrets timer går bara medan något kan
//      rulla.
//   3. ÅTERKOPPLINGEN — ritad i OVERLAY-lagret efter appens egen overlay
//      och före damage-debuggen: målets ram, effektmärket och dragbilden.
//      Overlayn komponeras om vid varje present och är självstädande, så
//      ett drag kostar EN present per musrörelse och ingen damage alls —
//      hård regel 1 utan undantag.
//
// RÄCKVIDDEN är fönstret. Den dag ett drag ska kunna gå mellan våra fönster
// flyttar del 2 och 3 till `App`; del 1 och hela widget-API:t står kvar.

#include "glaze/glaze.h"

#include <algorithm>

#include "drag_scroll.h"   // autoskrollens timer-id

namespace glaze {

// ================================================================= LASTEN

void DragData::set(const std::string& format, std::string value)
{
    // PÅ PLATS: ordningen är källans preferens, och en ersättning får inte
    // flytta formatet sist.
    for (auto& e : entries_)
        if (e.first == format) {
            e.second = std::move(value);
            return;
        }
    entries_.emplace_back(format, std::move(value));
}

bool DragData::has(const std::string& format) const
{
    for (const auto& e : entries_)
        if (e.first == format)
            return true;
    return false;
}

const std::string& DragData::get(const std::string& format) const
{
    static const std::string tom;
    for (const auto& e : entries_)
        if (e.first == format)
            return e.second;
    return tom;
}

std::vector<std::string> DragData::formats() const
{
    std::vector<std::string> ut;
    ut.reserve(entries_.size());
    for (const auto& e : entries_)
        ut.push_back(e.first);
    return ut;
}

// ============================================================== SESSIONEN

namespace {

// Målets val mot källans mask. En effekt UTANFÖR masken är `None` — WinForms
// regel, och skälet att ingen handler behöver jämföra själv. Sätter målet
// FLERA bitar väljs den första av Copy · Move · Link som källan tillåter:
// ett släpp gör EN sak, och Copy står först för att den aldrig förstör
// något hos källan.
DragEffect validerad(DragEffect e, DragEffect allowed)
{
    const DragEffect ok = e & allowed;
    for (DragEffect kandidat :
         {DragEffect::Copy, DragEffect::Move, DragEffect::Link})
        if (allows(ok, kandidat))
            return kandidat;
    return DragEffect::None;
}

gfx::Rectf snitt(const gfx::Rectf& a, const gfx::Rectf& b)
{
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    return gfx::Rectf{x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
}

} // namespace

// VÄNSTERTRYCK. Bara en `allowDrag`-widget blir en kandidat — alla andra
// tryck är vanliga tryck, tröskeln frågas inte ens.
void AppWindow::dragArm(Widget* w, float x, float y)
{
    if (drag_.phase != DragPhase::Idle || !w || !w->allowDrag())
        return;
    drag_.phase  = DragPhase::Pending;
    drag_.source = w;
    drag_.startX = drag_.x = x;
    drag_.startY = drag_.y = y;
}

// MER än tröskeln, inte lika med: `dragThreshold` är hur långt ett tryck
// får darra och ändå vara ett tryck. Avståndet är euklidiskt — en tröskel i
// en RUTA (Windows SM_CXDRAG/CYDRAG) hade startat diagonala drag tidigare
// än raka, och det finns inget skäl att ärva den skevheten.
bool AppWindow::dragThresholdPassed(float x, float y) const
{
    const float t  = style().dragThreshold * win_->uiScale();
    const float dx = x - drag_.startX;
    const float dy = y - drag_.startY;
    return dx * dx + dy * dy > t * t;
}

// TRÖSKELN ÄR PASSERAD — trycket avgörs nu en gång för alla: antingen blir
// det ett drag, eller så förblir det ett tryck och tröskeln frågas aldrig
// igen (sessionen går till Idle). Först frågas källan om draget får börja
// där trycket skedde (`canDragFrom`), sedan krokarna, appens handler sist,
// och efter VARJE anrop ut prövas sessionens egna fält: en handler som
// unadopterar källan nollar `drag_.source` via `detachTree`, och den lokala
// kopian får då aldrig röras igen.
bool AppWindow::dragBegin(float x, float y)
{
    Widget* src = drag_.source;
    if (!src || !src->isEnabled() || !src->allowDrag()) {
        drag_ = DragSession{};
        return false;
    }
    // FÖNSTRETS LIV: en handler får riva fönstret — då läses inte
    // `drag_` igen (kontraktet vid `AppWindow::close()`).
    const std::weak_ptr<char> liv = lifeToken();

    DragStartArgs e;
    e.x = drag_.startX;
    e.y = drag_.startY;
    {
        const Ctx        c = ctxFor(*src);
        const gfx::Rectf r = src->resolve(c);
        e.localX = Draw::localX(c, r, drag_.startX);
        e.localY = Draw::localY(c, r, drag_.startY);
        // FÅR DRAGET BÖRJA HÄR? — frågan gäller
        // TRYCKET, och den ställs före allt som fyller lasten: en avdelare
        // eller en kryssruta förblir ett tryck också när appens `onDragStart`
        // hade fyllt den. Kroken kör ingen användarkod enligt kontraktet, men
        // en override i en egen widget är kod som kan lossa källan eller
        // stänga fönstret — sessionen prövas därför som efter en handler.
        const bool får = src->canDragFrom(c, drag_.startX, drag_.startY);
        if (liv.expired())
            return false;
        if (drag_.source != src || drag_.phase != DragPhase::Pending)
            return false;
        if (!får) {
            drag_ = DragSession{};   // ett vanligt tryck — klicket kommer
            return false;
        }
        // STARTEN PÅGÅR: från kroken till och med appens `onDragStart` är
        // källan dragets källa (`Widget::isDragSource`), så en inbyggd last
        // går att läsa redan i start-handlern. En avböjd start återställer
        // sessionen och släcker flaggan med den.
        drag_.starting = true;
        src->dragStart(c, e);   // inbyggd last (TreeView) — FÖRE appen
    }
    if (liv.expired())
        return false;
    if (drag_.source != src || drag_.phase != DragPhase::Pending)
        return false;
    if (src->onDragStart) {
        src->onDragStart(src, e);
        if (liv.expired())
            return false;
        invalidate();   // handlern kan ha ändrat vad som helst
        if (drag_.source != src || drag_.phase != DragPhase::Pending)
            return false;
    }

    // TOM LAST = INGET DRAG. Det är så en källa avstår (en TreeView som
    // trycktes nedanför sista raden, en handler som inte vill), och då är
    // trycket fortfarande ett tryck: mouseDrag fortsätter, klicket kommer.
    // Återställningen släcker också `starting` — den avböjda starten bär
    // ingen last.
    if (e.data.empty() || e.allowed == DragEffect::None) {
        drag_ = DragSession{};
        return false;
    }

    drag_.phase    = DragPhase::Active;
    drag_.starting = false;   // Active bär källan från och med nu
    drag_.data     = std::move(e.data);
    drag_.allowed  = e.allowed;
    drag_.text     = e.text.empty() ? drag_.data.text() : std::move(e.text);
    drag_.img      = e.img;

    // TRYCKET ÄR FÖRBRUKAT: inget `clicked()` vid släppet (ett drag som
    // slutar över källan är ingen knapptryckning) och ingen `mouseDrag` till
    // källan medan draget pågår — en panels tumgrepp ska inte rulla vidare
    // under lasten.
    if (pressedWidget_) {
        pressedWidget_->pressed_ = false;
        pressedWidget_->invalidate();
        pressedWidget_ = nullptr;
    }
    dragMotion(x, y);
    return true;
}

// MÅLET UNDER EN PUNKT — `onContextMenu`s routing: första förfader (eller
// widgeten själv) med `allowDrop` som är aktiverad. Tom fönsteryta börjar
// i roten, så `root().setAllowDrop(true)` gör hela fönstret till ett mål.
// UTANFÖR klientytan finns inget mål alls — annars hade ett släpp utanför
// fönstret landat i roten.
Widget* AppWindow::dropTargetAt(float x, float y)
{
    const Ctx        c = makeCtx();
    const gfx::Rectf klient{0.0f, 0.0f, c.area.x + c.area.w,
                            c.area.y + c.area.h};
    if (!klient.contains(x, y))
        return nullptr;
    Widget* over = widgetAt(x, y, c);
    for (Widget* w = over ? over : &root_; w; w = w->parent())
        if (w->allowDrop() && w->isEnabled())
            return w;
    return nullptr;
}

DragArgs AppWindow::makeDragArgs(Widget& w, const Ctx& c) const
{
    DragArgs e(drag_.data);
    e.allowed = drag_.allowed;
    e.effect  = drag_.effect;   // BÄRS — se DragArgs i widget.h
    e.x       = drag_.x;
    e.y       = drag_.y;
    const gfx::Rectf r = w.resolve(c);
    e.localX = Draw::localX(c, r, drag_.x);
    e.localY = Draw::localY(c, r, drag_.y);
    e.source = drag_.source;
    return e;
}

// Den validerade effekten till sessionen OCH widgeten. Widgeten invalideras
// bara vid ÄNDRING (regel 1) — dess `draw()` får läsa `dropEffect()` rakt
// av för sin egen markering (TreeViews insättningslinje).
void AppWindow::setDropEffect(Widget* t, DragEffect e)
{
    drag_.effect = e;
    if (t->dropEffect_ != e) {
        t->dropEffect_ = e;
        t->invalidate();
    }
}

// INTRÄDET — enter och DIREKT därefter over (HTML5:s
// ordning): varje plats möter `onDragOver`, också den första, så ett mål
// skriver sin regel per plats på ETT ställe och den håller för ett släpp
// utan en enda rörelse efter inträdet. Enters val BÄRS in i over (sessionens
// `effect`, som `makeDragArgs` läser), och det är overs val som gäller.
void AppWindow::dragEnterTarget(Widget* t)
{
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    drag_.target = t;
    drag_.effect = DragEffect::None;   // ett nytt mål börjar AVVISANDE
    const Ctx c = ctxFor(*t);
    DragArgs  e = makeDragArgs(*t, c);
    t->dragEnter(c, e);
    if (liv.expired())
        return;
    if (drag_.target != t || drag_.phase != DragPhase::Active)
        return;
    if (t->onDragEnter) {
        t->onDragEnter(t, e);
        if (liv.expired())
            return;
        invalidate();   // lågfrekvent — den vanliga fulla omritningen
        if (drag_.target != t || drag_.phase != DragPhase::Active)
            return;
    }
    drag_.effect = validerad(e.effect, drag_.allowed);
    dragOverTarget();   // sätter den validerade effekten på målet
}

void AppWindow::dragOverTarget()
{
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    Widget*                   t   = drag_.target;
    const Ctx                 c   = ctxFor(*t);
    DragArgs                  e   = makeDragArgs(*t, c);
    t->dragOver(c, e);
    if (liv.expired())
        return;
    if (drag_.target != t || drag_.phase != DragPhase::Active)
        return;
    if (t->onDragOver) {
        // INGEN full omritning efter den här handlern: den körs per
        // musrörelse, och dess jobb är att sätta `e.effect`. Kontraktet
        // står vid `Widget::onDragOver`.
        t->onDragOver(t, e);
        if (liv.expired())
            return;
        if (drag_.target != t || drag_.phase != DragPhase::Active)
            return;
    }
    setDropEffect(t, validerad(e.effect, drag_.allowed));
}

// Lämna det aktuella målet: kroken släcker widgetens egen markering, appens
// handler får veta. Sessionens pekare nollas FÖRE anropen — river handlern
// målet finns det ingenting kvar här att städa, och `t` rörs inte efter
// handlern. Inte heller fönstret, om handlern rev det.
void AppWindow::dragLeaveTarget()
{
    Widget* t = drag_.target;
    if (!t)
        return;
    const std::weak_ptr<char> liv = lifeToken();
    const Ctx                 c   = ctxFor(*t);
    DragArgs                  e   = makeDragArgs(*t, c);   // senaste valet
    drag_.target = nullptr;
    drag_.effect = DragEffect::None;
    if (t->dropEffect_ != DragEffect::None) {
        t->dropEffect_ = DragEffect::None;
        t->invalidate();
    }
    t->dragLeave();
    if (liv.expired())
        return;
    if (t->onDragLeave) {
        t->onDragLeave(t, e);
        if (!liv.expired())
            invalidate();
    }
}

void AppWindow::dragMotion(float x, float y)
{
    if (drag_.phase != DragPhase::Active)
        return;
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    drag_.x = x;
    drag_.y = y;
    // DRAGBILDEN FÖLJER PEKAREN — en present, ingen damage (regel 1).
    invalidateOverlay();
    // AUTOSKROLLEN: kan något under pekaren rulla
    // startar fönstrets timer — den går bara medan något faktiskt kan rulla
    // (regel 1, tooltipens form), och första ticket utan rullning stoppar
    // den. Frågan kör ingen användarkod.
    if (!drag_.scrolling && dragScrollChain(x, y, false)) {
        win_->startTimer(kDragScrollTimerId,
                         static_cast<std::uint32_t>(std::max(style().dragScrollMs, 1)));
        drag_.scrolling = true;
    }

    Widget* t = dropTargetAt(x, y);
    if (drag_.target && t != drag_.target) {
        dragLeaveTarget();
        if (liv.expired() || drag_.phase != DragPhase::Active)
            return;
        // Handlern kan ha ändrat trädet — fråga igen i stället för att lita
        // på ett svar som räknades före den.
        t = dropTargetAt(x, y);
    }
    if (!t)
        return;
    if (t != drag_.target)
        dragEnterTarget(t);
    else
        dragOverTarget();
}

// KEDJAN under pekaren, uppåt som hjulet: widgeten under (x, y) och dess
// förfäder till men INTE med roten — inaktiverade och dolda hoppas över, och
// första som svarar true vinner, så en inre lista vid sitt slut lämnar till
// den yttre panelen. Utanför klientytan rullar ingenting (pekaren står på
// −1e9 efter `dragOutside`). Ingen användarkod körs.
bool AppWindow::dragScrollChain(float x, float y, bool apply)
{
    const Ctx        c = makeCtx();
    const gfx::Rectf klient{0.0f, 0.0f, c.area.x + c.area.w,
                            c.area.y + c.area.h};
    if (!klient.contains(x, y))
        return false;
    for (Widget* w = widgetAt(x, y, c); w && w != &root_; w = w->parent()) {
        if (!w->isEnabled() || !w->isVisible())
            continue;
        if (w->dragScroll(ctxFor(*w), x, y, apply))
            return true;
    }
    return false;
}

// TIMERNS STEG: rulla ett steg och gör sedan målvalet om på SAMMA punkt —
// hjulets regel (hjulsteget under ett drag), så insättningslinjen och appens
// `onDragOver` följer med utan att pekaren rört sig. Ett tick utan något att
// rulla, eller ett sent tick efter sessionens slut, stoppar timern.
void AppWindow::dragScrollTick()
{
    if (drag_.phase != DragPhase::Active ||
        !dragScrollChain(drag_.x, drag_.y, true)) {
        win_->stopTimer(kDragScrollTimerId);
        drag_.scrolling = false;
        return;
    }
    dragMotion(drag_.x, drag_.y);
}

// KÄLLANS SLUT — kroken (en TreeView tar bort en nod som FLYTTADES till ett
// annat mål) och sedan appens handler. `target` är giltig under anropet.
void AppWindow::dragEnd(DragEffect effect, Widget* target)
{
    Widget* s = drag_.source;
    if (!s)
        return;   // källan lämnade trädet under draget — se detachTree
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    DragEndArgs e;
    e.effect = effect;
    e.target = target;
    s->dragEnd(e);
    if (liv.expired())
        return;
    if (drag_.source == s && s->onDragEnd) {
        s->onDragEnd(s, e);
        if (!liv.expired())
            invalidate();
    }
}

// KNAPPEN SLÄPPT. Ett Pending-tryck var aldrig ett drag och lämnas till
// den vanliga klickvägen. Ett aktivt drag avgörs där knappen släpptes:
// accepterat ⇒ släppet på målet, avvisat ⇒ `leave` (OLE-ordningen: ett mål
// som aldrig sa ja får DragLeave i stället för Drop), och källan får alltid
// `onDragEnd` SIST, med utfallet.
//
// SLÄPPET FRÅGAR APPEN FÖRST — det enda steget som
// ÄNDRAR något, så frågan kommer före handlingen (`onClose`-mönstret, och
// ritytans "appens handler först, sedan gesten"): `onDrop` får neka
// (`effect = None`) eller utföra släppet själv (`handled`), och först
// därefter — med effekten validerad mot källans mask — kör widgetens
// `drop`-krok det inbyggda, om målet sitter kvar och appen gjorde varken
// eller. Kroken får ett FÄRSKT Ctx: handlern kan ha ändrat layouten, och
// `e.source` läst om ur sessionen (nullptr om handlern rev källan). Tog
// handlern målet ur trädet utan `handled` utförde ingen släppet, och
// utfallet blir None.
//
// FINISHING-fasen täcker handlers: en modal dialog i `onDrop` pumpar
// nästlat, och ett WindowFocusLost därifrån hade annars avbrutit ett
// släpp som redan pågår.
void AppWindow::dragRelease(float x, float y)
{
    if (drag_.phase == DragPhase::Pending) {
        drag_ = DragSession{};   // ett vanligt tryck — klicket kommer
        return;
    }
    if (drag_.phase != DragPhase::Active)
        return;
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    if (x != drag_.x || y != drag_.y) {
        dragMotion(x, y);   // släppet avgörs där knappen faktiskt släpptes
        if (liv.expired() || drag_.phase != DragPhase::Active)
            return;
    }

    drag_.phase       = DragPhase::Finishing;
    Widget*    t      = drag_.target;
    DragEffect utfall = DragEffect::None;
    if (t && drag_.effect != DragEffect::None) {
        DragArgs e = makeDragArgs(*t, ctxFor(*t));
        if (t->onDrop) {
            t->onDrop(t, e);   // appen FÖRST — den kan neka eller ta över
            if (liv.expired())
                return;
            // Handlern kan ha rivit KÄLLAN: `detachTree` har då nollat
            // sessionens pekare, och kroken får inte se den gamla.
            e.source = drag_.source;
        }
        e.effect = validerad(e.effect, drag_.allowed);
        if (drag_.target != t) {
            // Målet lämnade trädet i handlern, så ingen krok utför släppet.
            // Utan `handled` gjorde ingen det: källan får None och aldrig
            // det föreslagna Move — en TreeView-källa hade annars tagit bort
            // sina noder i `dragEnd` utan att de hamnat någonstans.
            if (!e.handled)
                e.effect = DragEffect::None;
        } else if (!e.handled && e.effect != DragEffect::None) {
            t->drop(ctxFor(*t), e);   // det inbyggda, på platsen appen såg
            if (liv.expired())
                return;
        }
        invalidate();   // ett släpp ändrar nästan alltid något synligt
        utfall = validerad(e.effect, drag_.allowed);
        // STÄDNINGEN efter besöket — `dragLeave` är krokens städning också
        // efter ett lyckat släpp (appens `onDragLeave` fyras däremot inte:
        // Drop ERSÄTTER Leave). Målet kan ha lämnat trädet i sin handler,
        // och då har `detachTree` redan städat.
        if (drag_.target == t) {
            if (t->dropEffect_ != DragEffect::None) {
                t->dropEffect_ = DragEffect::None;
                t->invalidate();
            }
            t->dragLeave();
            if (liv.expired())
                return;
        }
    } else if (t) {
        dragLeaveTarget();
        if (liv.expired())
            return;
    }

    Widget* mål = utfall != DragEffect::None ? drag_.target : nullptr;
    dragEnd(utfall, mål);
    if (liv.expired())
        return;
    if (drag_.scrolling)
        win_->stopTimer(kDragScrollTimerId);   // autoskrollen slutar med draget
    drag_ = DragSession{};
    invalidateOverlay();   // dragbilden ska bort
}

// AVBROTT — Escape eller fokusförlust. Samma slut som ett avvisat släpp:
// målet lämnas, källan får `None`. Pending släpps tyst (inget drag hade
// börjat), och Finishing är en no-op (se dragRelease).
void AppWindow::dragCancel()
{
    if (drag_.phase == DragPhase::Pending) {
        drag_ = DragSession{};
        return;
    }
    if (drag_.phase != DragPhase::Active)
        return;
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    drag_.phase = DragPhase::Finishing;
    dragLeaveTarget();
    if (liv.expired())
        return;
    dragEnd(DragEffect::None, nullptr);
    if (liv.expired())
        return;
    if (drag_.scrolling)
        win_->stopTimer(kDragScrollTimerId);   // autoskrollen slutar med draget
    drag_ = DragSession{};
    invalidateOverlay();
}

// ÄR JAG KÄLLAN? — kontraktet står vid
// deklarationen i widget.h. Svaret läses ur SESSIONEN och aldrig ur widgeten:
// varje väg som avslutar eller avböjer ett drag återställer sessionen (eller
// nollar källan, `detachTree`), så en inbyggd last kan inte överleva sitt drag
// bara för att ingen krok frågades. Widget är AppWindows vän och läser fältet
// rakt av.
bool Widget::isDragSource() const
{
    const AppWindow* w = appWindow();
    if (!w || w->drag_.source != this)
        return false;
    const AppWindow::DragPhase p = w->drag_.phase;
    return p == AppWindow::DragPhase::Active ||
           p == AppWindow::DragPhase::Finishing ||
           (p == AppWindow::DragPhase::Pending && w->drag_.starting);
}

// PEKAREN LÄMNADE FÖNSTRET. Draget lever vidare (backenden håller
// mus-capture, så släppet kommer), men utanför finns inget mål och ingen
// dragbild att rita.
void AppWindow::dragOutside()
{
    if (drag_.phase != DragPhase::Active)
        return;
    const std::weak_ptr<char> liv = lifeToken();   // se dragBegin
    dragLeaveTarget();
    if (liv.expired())
        return;
    drag_.x = drag_.y = -1.0e9f;   // utanför varje klientyta — se dropTargetAt
    invalidateOverlay();
}

// ========================================================== ÅTERKOPPLINGEN

// Ritas i OVERLAY-lagret: efter compositen och chromet, varje present, och
// självstädande. Boxen sätts till hela klientytan, så Draw-koordinaterna
// här är FÖNSTRETS logiska px.
//
// TRE saker, alla ur Style och Theme:
//   1. MÅLETS RAM — ett accepterande mål ramas in i `accent` med
//      `dropLineW`, innanför sin egen kant och klippt mot förfädernas klipp
//      (ett mål som rullat halvt ur sin panel ramas där det SYNS). En widget
//      som visar sin egen markering (`ownsDropIndicator`) slipper ramen.
//      SAMMA i alla tre familjerna, och det är ett beslut: ramen är
//      fokusringens släkting — en MARKERING och inte en yta — och den har
//      redan samma roll och form i alla tre.
//   2. EFFEKTMÄRKET vid pekaren: förbudsmärket (`errorSurface`/`onError`)
//      när inget mål tar emot, `+` för Copy och `›` för Link
//      (`accentSurface`/`onAccent`), inget för Move — OS:ens egen
//      konvention, där en flytt är det tysta normalfallet. Också samma i
//      alla tre: ett märke BETYDER något, precis som trafikljusen.
//   3. DRAGBILDEN — lastens text (och källans ikon) på en liten platta,
//      och det är HÄR familjerna skiljer sig (switchen nedan).
void AppWindow::drawDragFeedback(Ctx c)
{
    if (drag_.phase != DragPhase::Active)
        return;
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;
    const float s   = c.scale;
    c.box = gfx::Rectf{0.0f, 0.0f, c.area.x + c.area.w, c.area.y + c.area.h};

    // ---- 1. målets ram
    Widget* t = drag_.target;
    if (t && drag_.effect != DragEffect::None && !t->ownsDropIndicator()) {
        gfx::Rectf r = t->resolve(ctxFor(*t));   // fysiska px
        for (const Widget* p = t->parent(); p && p != &root_; p = p->parent())
            if (p->clipsChildren())
                r = snitt(r, p->clipRect(ctxFor(*p)));
        if (r.w > 0.0f && r.h > 0.0f)
            Draw::frame(c,
                        Draw::inset({r.x / s, r.y / s, r.w / s, r.h / s},
                                    m.dropLineW / 2.0f),
                        0.0f, Pen{col.accent, m.dropLineW});
    }

    // ---- 2. märket — pekaren är utanför klientytan efter MouseLeave
    const gfx::Rectf klient = Draw::bounds(c, c.box);
    const float      px     = drag_.x / s;
    const float      py     = drag_.y / s;
    if (!klient.contains(px, py))
        return;
    float            x = px + m.dragOffset;
    const float      y = py + m.dragOffset;
    const gfx::Rectf märke{x, y, m.dragBadgeSize, m.dragBadgeSize};
    switch (drag_.effect) {
    case DragEffect::None:
        Draw::badge(c, märke, Sign::Minus, col.errorSurface, col.onError);
        x += m.dragBadgeSize + m.iconGap;
        break;
    case DragEffect::Copy:
        Draw::badge(c, märke, Sign::Plus, col.accentSurface, col.onAccent);
        x += m.dragBadgeSize + m.iconGap;
        break;
    case DragEffect::Link:
        Draw::badge(c, märke, Sign::Arrow, col.accentSurface, col.onAccent);
        x += m.dragBadgeSize + m.iconGap;
        break;
    default:
        break;   // Move — det tysta normalfallet
    }

    // ---- 3. dragbilden
    if (drag_.text.empty() && !drag_.img.valid())
        return;
    // Sidan är assetens storleksklass, och för en bild ur pixlar (klass 0)
    // stilens reservsida — ikonplatsernas regel (Button, raderna).
    const float sida   = drag_.img.valid()
                             ? (drag_.img.sizeClass() > 0.0f
                                    ? drag_.img.sizeClass()
                                    : m.iconFallback)
                             : 0.0f;
    const float textW  = drag_.text.empty() ? 0.0f
                                            : Draw::measure(c, drag_.text).w;
    const float mellan = sida > 0.0f && textW > 0.0f ? m.iconGap : 0.0f;
    const float innerH = std::max(Draw::lineHeight(c), sida);
    const float w = std::min(m.dragGhostMaxW,
                             2.0f * m.dragGhostPadX + sida + mellan + textW);
    const float h = innerH + 2.0f * m.dragGhostPadY;
    // Plattan v-centreras mot märket, så de två läser som EN följeslagare.
    const gfx::Rectf platta{x, y + (m.dragBadgeSize - h) / 2.0f, w, h};

    switch (c.ui) {
    // GLAZE — fältets recept: `panel` med den BESTÄMDA konturen
    // `buttonBorder`. Ingen laminering: plattan är en etikett man BÄR, inte
    // en yta man trycker på (samma skäl som listans rad inte lamineras).
    case UI::Glaze:
        Draw::rect(c, platta, m.cornerRadius, col.panel,
                   Pen{col.buttonBorder, m.borderWidth});
        break;

    // MINIMALISTIC — "Outline": konturen är formen. Ytan fylls ändå med
    // fönstrets EGEN bakgrund — plattan svävar över godtyckligt innehåll
    // och måste vara läsbar där, och `windowBg` är ytan familjen redan
    // ritar allt mot. Ingen ny ton, bara ingen genomlysning.
    case UI::Minimalistic:
        Draw::rect(c, platta, m.cornerRadius, col.windowBg,
                   Pen{col.buttonBorder, m.borderWidth});
        break;

    // SLATE — en YTA: `panel` och hårlinjen `border`, som en panel och en
    // lista i samma familj.
    case UI::Slate:
    default:
        Draw::rect(c, platta, m.cornerRadius, col.panel,
                   Pen{col.border, m.borderWidth});
        break;
    }

    // Innehållet är detsamma i alla tre: ikonen till vänster, texten efter
    // och KLIPPT mot plattan — en lång text kortas av i högerkanten, aldrig
    // i början (därför inte `Draw::content`, som centrerar blocket).
    float cx = platta.x + m.dragGhostPadX;
    if (sida > 0.0f) {
        Draw::glyph(c, {cx, platta.y + (h - sida) / 2.0f, sida, sida},
                    drag_.img);
        cx += sida + mellan;
    }
    if (textW > 0.0f) {
        const gfx::Rectf textRuta{cx, platta.y,
                                  platta.x + w - m.dragGhostPadX - cx, h};
        Draw::clip(c, textRuta);
        Draw::text(c, textRuta, drag_.text, col.text);
        Draw::endClip(c);
    }
}

} // namespace glaze
