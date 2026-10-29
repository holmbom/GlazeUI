// tooltip.cpp — TOOLTIPEN: visningen av
// `Widget::setToolTip`. API:t står i widget.h (texten, på basen) och
// app_window.h (tillståndet per fönster och `toolTipWidget()`); de interna
// klasserna i tooltip.h. drag_drop.cpp är förebilden: en fil som
// implementerar en bit av `AppWindow` och håller den isär från resten.
//
// TRE DELAR:
//
//   1. PLATTAN (`ToolTipBody`) — texten brutet mot `toolTipMaxW` på en
//      platta, ritad per UI. Den som MÄTER och den som RITAR delar EN
//      brytbredd, så fönstret aldrig blir för litet för sin egen text.
//   2. FÖNSTRET (`ToolTipWindow`) — Chrome::None + popup, placerat via
//      popup-kontraktet: ett ankare runt pekaren i ägarens klient-px, och
//      backenden lägger fönstret under det och vänder/klampar vid skärmkant
//      (Wayland: xdg_positioner — där finns ingen skärmrymd att räkna i).
//   3. TILLSTÅNDSMASKINEN i `AppWindow` — vila, väntar (timer), visas —
//      driven av `handleEvent` med en rad per händelsetyp.
//
// VARFÖR ETT POPUP-FÖNSTER och inte overlay-lagret: en tooltip ska kunna gå
// UTANFÖR fönstret (en knapp i nederkanten, ett smalt verktygsfönster), och
// det kan bara ett eget OS-fönster. Priset är popup-reglerna — framför allt
// att en ny popup stänger de gamla (`App::closePopupsNow`, xdg-shells
// ordningskrav), så en tooltip VISAS ALDRIG medan en annan popup (en öppen
// lista, en meny) lever: den hade stängt den.
//
// REGEL 1: fördröjningen är en TIMER (`Window::startTimer`) och aldrig
// `setAnimating`. Timern löper bara medan något väntar, och stoppas så fort
// den löst ut eller väntan avbryts, så appen sover.

#include "tooltip.h"

#include <cmath>

#include "glaze/app.h"
#include "glaze/draw.h"

namespace glaze {

// ================================================================ PLATTAN

gfx::Sizef ToolTipBody::measure(const Ctx& c, const std::string& text)
{
    const auto&      m = *c.style;
    const gfx::Sizef t = Draw::measureWrapped(c, text, m.toolTipMaxW);
    return gfx::Sizef{t.w + 2.0f * m.toolTipPadX, t.h + 2.0f * m.toolTipPadY};
}

gfx::Sizef ToolTipBody::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN, och i praktiken alltid AUTO: plattan fyller fönstret
        // (Dock::Fill), och fönstret får sin storlek ur `measure` — samma
        // uträkning som här.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        const gfx::Sizef m = measure(c, text());
        if (s.w <= 0.0f) s.w = m.w;
        if (s.h <= 0.0f) s.h = m.h;
        return s;
    }
    }
}

void ToolTipBody::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;

    // PLATTAN är dragbildens — samma sorts flytande etikett, samma recept
    // (drag_drop.cpp): en yta man LÄSER, och ingenting man trycker på.
    // Fyrkantig med flit: tooltipfönstret är OPAKT, och ett rundat hörn på
    // en opak yta hade visat fönsterbakgrunden i hörnen.
    switch (c.ui) {
    // GLAZE — fältets recept: `panel` med den BESTÄMDA konturen
    // `buttonBorder`. Ingen laminering — plattan är information, inte en
    // kropp.
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.panel, Pen{col.buttonBorder, m.borderWidth});
        break;

    // MINIMALISTIC — "Outline": konturen är formen, och ytan är fönstrets
    // egen bakgrund (`windowBg`). Plattan svävar över godtyckligt innehåll
    // och måste vara läsbar där; ingen ny ton, bara ingen genomlysning.
    case UI::Minimalistic:
        Draw::rect(c, b, 0.0f, col.windowBg,
                   Pen{col.buttonBorder, m.borderWidth});
        break;

    // SLATE — en YTA: `panel` och hårlinjen `border`, som en panel och en
    // lista i samma familj.
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel, Pen{col.border, m.borderWidth});
        break;
    }

    // Texten bryts mot SAMMA bredd som `measure` bröt mot — inte mot
    // fönstrets, som efter avrundning till hela pixlar kan vara en bråkdel
    // smalare och då hade brutit en rad till. Blocket står vänsterställt
    // och centreras i höjdled (Draw::textWrapped).
    Draw::textWrapped(c,
                      {b.x + m.toolTipPadX, b.y + m.toolTipPadY,
                       m.toolTipMaxW, b.h - 2.0f * m.toolTipPadY},
                      text(), col.text);
}

// =============================================================== FÖNSTRET

ToolTipWindow::ToolTipWindow(AppWindow& ägare)
    : AppWindow("", 100, 24, gfx::Chrome::None)
{
    setPopup(true);   // noActivate + toolWindow + App stänger vid klick utanför
    body_ = add<ToolTipBody>();
    // Stängd av NÅGON väg — ett tryck någon annanstans (App), en annan popup
    // som öppnas (`closePopupsNow`), ägarens kaskad eller vår egen
    // `toolTipCancel` — så får ägaren veta det. Det är ägarens tillstånd
    // som säger att ingen tooltip visas.
    onClose = [&ägare](Widget*, CloseArgs&) { ägare.toolTipClosed(); };
}

bool ToolTipWindow::event(const gfx::Event& e)
{
    if (e.type == gfx::EventType::MouseButtonDown) {
        close();   // uppskjuten — onClose ovan säger det till ägaren
        return true;
    }
    return false;
}

// ================================================ TILLSTÅNDSMASKINEN

// ROUTED LOOKUP — `onContextMenu`s form: widgeten under pekaren eller dess
// närmaste förfader vars tooltip VID PEKAREN är icke-tom (`toolTipAt`, en
// text per del — basen svarar med widgetens egen). Tom
// fönsteryta börjar i roten, så `root().setToolTip(…)` är en tooltip för
// hela fönstret.
//
// Frågan ställs vid VARJE musrörelse, så kedjans Ctx räknas UPPIFRÅN i ett
// pass — `ctxFor`s egen regel (chrome-roten mot hela klientytan, sedan varje
// förälders `childArea`) — i stället för ett `ctxFor` per förfader, som hade
// vandrat kedjan en gång per nivå.
AppWindow::ToolTipSource AppWindow::toolTipSourceAt(Widget* over, float x,
                                                    float y)
{
    std::vector<Widget*> kedja;   // pekaren först, roten (eller toppen) sist
    for (Widget* w = over ? over : &root_; w; w = w->parent())
        kedja.push_back(w);
    std::vector<Ctx> ctx(kedja.size());
    for (std::size_t i = kedja.size(); i-- > 0;) {
        Widget* const p = kedja[i]->parent();
        if (!p || p == &root_) {
            ctx[i] = ctxFor(*kedja[i]);   // toppen: inga föräldrar att vandra
        } else {
            ctx[i]      = ctx[i + 1];   // förälderns Ctx — och dess yta
            ctx[i].area = p->childArea(ctx[i + 1]);
        }
    }
    for (std::size_t i = 0; i < kedja.size(); ++i) {
        TrText t = kedja[i]->toolTipAt(ctx[i], x, y);
        if (!t.empty())
            return ToolTipSource{kedja[i], std::move(t)};
    }
    return ToolTipSource{};
}

// PEKAREN RÖRDE SIG. Under ett tryck, en capture eller ett drag finns ingen
// tooltip alls — pekaren är upptagen.
//
// EN KÄLLA ÄR WIDGET OCH TEXT: två delar i samma widget (dockningsytans
// handtag) är två källor, så pekaren som går från den ena till den andra
// gömmer den visade och väntar den korta tiden — som mellan två knappar.
void AppWindow::toolTipMotion(Widget* over, float x, float y)
{
    if (pressedWidget_ || capturedWidget_ || drag_.phase != DragPhase::Idle) {
        toolTipCancel(false);
        return;
    }
    ToolTipSource src = toolTipSourceAt(over, x, y);
    tip_.x = x;
    tip_.y = y;
    // TYSTNADEN efter ett klick/en tangent gäller tills pekaren lämnar
    // källan — en annan källa (eller ingen) släpper den.
    const bool tystad = src.widget && src.widget == tip_.blocked &&
                        src.text == tip_.blockedText;
    if (!tystad)
        tip_.blocked = nullptr;
    else
        src = ToolTipSource{};

    if (tip_.shown) {
        if (src.widget == tip_.shown && src.text == tip_.text)
            return;   // rörelse INOM källan — tooltipen står kvar där den står
        // DIREKT till en annan källa: den gamla göms, och nästa är "varm".
        const bool varm = src.widget != nullptr;
        toolTipCancel(false);
        tip_.warm = varm;
        if (!src.widget)
            return;
    } else if (!src.widget) {
        toolTipCancel(false);
        return;
    }

    // VÄNTA PÅ VILA: varje rörelse startar om perioden, så tooltipen kommer
    // först när pekaren stått still. En varm väntan är kort — och värmen
    // står kvar så länge pekaren går från källa till källa; den släpps
    // först på en yta UTAN tooltip (eller av ett klick, en tangent …).
    tip_.source  = src.widget;
    tip_.text    = std::move(src.text);
    tip_.pending = true;
    win_->startTimer(kToolTipTimerId,
                     static_cast<std::uint32_t>(tip_.warm ? style().toolTipReshowMs
                                                          : style().toolTipDelayMs));
}

void AppWindow::toolTipCancel(bool blockera)
{
    if (blockera) {
        Widget* tyst = tip_.shown ? tip_.shown : tip_.source;
        if (tyst) {
            tip_.blocked     = tyst;
            tip_.blockedText = tip_.text;
        }
    }
    if (tip_.pending)
        win_->stopTimer(kToolTipTimerId);
    tip_.pending = false;
    tip_.source  = nullptr;
    tip_.warm    = false;
    if (tip_.shown) {
        tip_.shown = nullptr;
        if (tipWin_ && tipWin_->isOpen())
            tipWin_->close();   // uppskjuten — verkställs i pump-svansen
    }
}

// FÖRDRÖJNINGEN LÖPTE UT. Allt prövas IGEN här, för tiden har gått: källan
// kan ha gömts eller tömts, ett tryck kan ha börjat, en lista kan ha öppnats.
void AppWindow::toolTipTimer()
{
    win_->stopTimer(kToolTipTimerId);   // upprepande timer — en gång räcker
    // Ett tick UTAN väntan (sent levererat efter att väntan avbröts, eller
    // en visad tooltip som redan står) gör ingenting — annars hade det
    // startat om en väntan för en tooltip som redan syns.
    if (!tip_.pending)
        return;
    Widget* src  = tip_.source;
    tip_.pending = false;
    if (!src || !open_ || hidden_)
        return;
    // ETT NYTT TRÄFFPROV OCH ETT NYTT KÄLLUPPSLAG vid pekaren:
    // widgeten eller delen under den kan ha
    // bytts, gömts eller tömts medan tiden gick — en flik som byttes, ett
    // handtag som fälldes ut. Bara SAMMA källa visas, med sin text NU; en
    // annan källa väntar på nästa rörelse. Och källan måste SYNAS
    // (`shownInTree` — fönstrets fråga, delad med standardknappen) —
    // träffprovet når inte in i en inaktiv flik, men prövningen står kvar som
    // en sanning oberoende av hur träffprovet klipper.
    ToolTipSource nu = toolTipSourceAt(widgetAt(tip_.x, tip_.y, makeCtx()),
                                       tip_.x, tip_.y);
    if (nu.widget != src || nu.text.empty() || !shownInTree(*src) ||
        pressedWidget_ || capturedWidget_ || drag_.phase != DragPhase::Idle) {
        tip_.source = nullptr;
        return;
    }
    tip_.text = std::move(nu.text);
    // EN ANNAN POPUP LEVER (en öppen lista, en meny) — tooltipen hade
    // stängt den (`showOwned` → `closePopupsNow`). Den väntar i stället.
    for (AppWindow* w : glaze::app().windows())
        if (w != tipWin_.get() && w->popup() && w->isOpen() && !w->pendingClose_) {
            tip_.source = nullptr;
            return;
        }
    toolTipShow(src);
}

void AppWindow::toolTipShow(Widget* src)
{
    if (!tipWin_)
        tipWin_ = std::make_unique<ToolTipWindow>(*this);
    // Den förra stängs fortfarande (uppskjuten stängning, samma varv) — ett
    // `show()` nu hade bara lyft det döende fönstret. Vänta ett kort varv.
    if (tipWin_->isOpen()) {
        tip_.source  = src;
        tip_.pending = true;
        win_->startTimer(kToolTipTimerId,
                         static_cast<std::uint32_t>(style().toolTipReshowMs));
        return;
    }

    // STORLEKEN FÖRE VISNINGEN: en xdg-popup får sin storlek vid
    // mappningen och kan inte växa efteråt. Mätt med DET HÄR fönstrets font
    // — tooltipen skapar sin egen i samma stil och skala.
    const Ctx        c = makeCtx();
    const gfx::Sizef m = ToolTipBody::measure(c, tip_.text.str());
    tipWin_->body().setText(tip_.text);
    tipWin_->setClientSize(static_cast<int>(std::ceil(m.w)),
                           static_cast<int>(std::ceil(m.h)));
    // ANKARET är PEKARENS ZON: från `toolTipOffsetUp` ovanför spetsen till
    // `toolTipOffsetY` under den. Backenden lägger fönstret UNDER zonen —
    // under pilpekarens kropp — och vänder det OVANFÖR zonen när skärmen
    // tar slut. Luften ovanför är inte kosmetisk: med zonens överkant PÅ
    // spetsen räcker en avrundning i backenden (topp och höjd avrundas var
    // för sig) för att plattans nedersta rad hamnar under pekaren, och då
    // blinkar tooltipen (Windows-mätt: 1 px överlapp vid skala 1,25).
    const float s = win_->uiScale();
    tipWin_->setPopupAnchor(
        {tip_.x, tip_.y - style().toolTipOffsetUp * s, 1.0f,
         (style().toolTipOffsetUp + style().toolTipOffsetY) * s});
    tip_.shown  = src;
    tip_.source = src;
    tip_.warm   = false;
    tipWin_->show(*this);
    if (!tipWin_->isOpen()) {
        gfx::log("AppWindow: tooltipens popup kom aldrig upp — hoppar över");
        tip_.shown = nullptr;
    }
}

// TEXTEN BYTTES. Visas den här widgetens tooltip just nu stängs den — nästa
// vila visar den nya. En väntande timer behöver ingenting: den läser texten
// när den löser ut.
void AppWindow::toolTipChanged(Widget& w)
{
    if (tip_.shown == &w)
        toolTipCancel(false);
}

// Popupen stängdes — av vilken väg som helst (se ToolTipWindow-ctorn).
void AppWindow::toolTipClosed()
{
    tip_.shown = nullptr;
}

} // namespace glaze
