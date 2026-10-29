#pragma once
#include <algorithm>

#include <gfx/events.h>
#include <gfx/geometry.h>

// SYSTEMFÄLTETS MODELL — INTERN header,
// header-only och REN LOGIK: animationens räkning, händelsernas routning och
// menyns ankare, utan en ikon i OS:ets fält och utan ett fönster.
// `date_model.h`s och `file_dialog_model.h`s prejudikat: det som går att
// räkna utan systemfältet grindas HEADLESS (tests/tray_gate.cpp når den här
// filen) mot handskrivna förväntade värden, och `TrayIcon` läser samma funktioner —
// det som körs och det som grindas kan inte glida isär.
//
// Det är testsömmen, och den är en MODELL och inte en testkrok i
// produktkoden: grinden lägger aldrig en ikon i utvecklarens systemfält, för
// Windows minns varje programfil som någon gång visat en.

namespace glaze::detail {

// ------------------------------------------------------------ ANIMATIONEN
//
// Bilderna växlar BARA medan ikonen syns, är startad och har minst två
// bilder — annars står timern still och appen sover (regel 1). Tiden
// kommer ur `gfx::setTrayTimer`; modellen räknar bara vilken bild som visas.
struct TrayAnimation {
    int  frameCount = 0;       // antalet bilder (`TrayIcon::frameCount`)
    bool started    = false;   // `startAnimation()` — BEGÄRAN
    bool shown      = false;   // ikonen står i OS:ets fält (synlig OCH stödd)
    int  index      = 0;       // bilden en startad animation visar

    // Ska tidskällan gå? Bara med något att växla mellan, och bara när det
    // syns.
    bool wantsTimer() const { return started && shown && frameCount >= 2; }

    // Bilden som visas: −1 = den STILLA ikonen (`setIcon`, annars förvalet).
    // En startad animation med minst EN bild visar `index` — med en enda bild
    // står den bilden still, utan timer.
    int shownFrame() const { return started && frameCount >= 1 ? index : -1; }

    // Ett tick ur tidskällan. Bara när timern ska gå — ett sent tick efter
    // att ikonen dolts eller animationen stoppats flyttar ingenting.
    void tick()
    {
        if (wantsTimer())
            index = (index + 1) % frameCount;
    }

    // Antalet bilder ändrades (kollektionsvokabulären): ett index utanför
    // börjar om på den första.
    void setFrameCount(int n)
    {
        frameCount = std::max(0, n);
        if (index >= frameCount)
            index = 0;
    }

    // `stopAnimation()` — tillbaka till den stilla ikonen, och nästa start
    // börjar på den första bilden.
    void stop()
    {
        started = false;
        index   = 0;
    }
};

// ------------------------------------------------------------- ROUTNINGEN
//
// Vad ett `EventType::Tray` ska leda till. Vid `fire` gör `TrayIcon` det
// händelsen själv säger (`Event::trayAction`: handlern, för Enter och Leave
// `onHover`, för Tick ett steg i animationen), öppnar sedan menyn om
// `openMenu` säger det och posterna finns (skalet prövar posterna EFTER
// handlern, som får bygga dem), och lyfter den modala dialogen vid
// `raiseModal`.
struct TrayRoute {
    bool fire       = false;   // händelsen går fram
    bool openMenu   = false;   // ContextMenu: menyn efter handlern
    bool raiseModal = false;   // input under en modal: lyft den i stället
};

// Så routas det:
//   * En DOLD ikon gör ingenting — ett event som låg kvar i kön när den
//     doldes inräknat — och glömmer att pekaren stod på den.
//   * Tick går fram, också medan en modal dialog är öppen.
//   * Enter och Leave går fram bara när de ÄNDRAR hovern — Enter om pekaren
//     inte redan stod på ikonen, Leave om den gjorde det — för backenden får
//     skicka dubbletter (gfx/events.h). Hovern spärras inte av en modal: den
//     släcker och tänder bara, och modalfiltret släpper igenom allt som inte
//     är input.
//   * Click, DoubleClick och ContextMenu är INPUT: medan en modal dialog är
//     öppen går de inte fram, och dialogen lyfts i stället — som ett klick på
//     ett blockerat fönster. Annars går de fram, och ContextMenu öppnar menyn.
inline TrayRoute routeTray(gfx::TrayAction a, bool visible, bool modalOpen,
                           bool& hovered)
{
    TrayRoute r;
    if (!visible) {
        hovered = false;
        return r;
    }
    switch (a) {
    case gfx::TrayAction::Tick:
        r.fire = true;
        break;
    case gfx::TrayAction::Enter:
    case gfx::TrayAction::Leave: {
        const bool in = a == gfx::TrayAction::Enter;
        r.fire        = hovered != in;
        hovered       = in;
        break;
    }
    case gfx::TrayAction::Click:
    case gfx::TrayAction::DoubleClick:
    case gfx::TrayAction::ContextMenu:
        if (modalOpen) {
            r.raiseModal = true;
            break;
        }
        r.fire     = true;
        r.openMenu = a == gfx::TrayAction::ContextMenu;
        break;
    }
    return r;
}

// -------------------------------------------------------- MENYNS ANKARE
//
// Ikonens rect INKLÄMD i arbetsytan, per axel: clamp(kant, work.min,
// work.max). En ikon i aktivitetsfältet eller menyraden ligger UTANFÖR
// arbetsytan, och klämd blir den en kant med höjden (eller bredden) 0 —
// popup-kontraktets "under ankaret, vänd uppåt när skärmen tar slut" lägger
// då menyn kant i kant med fältet: ovanför ett nedre aktivitetsfält, under
// ett övre och under menyraden, bredvid ett på sidan. Ikonens rect rakt av
// hade lagt menyn UNDER aktivitetsfältet, som ligger överst och täckt den.
// En ikon inne i arbetsytan står kvar oförändrad, och en TOM arbetsyta (OS:et
// kunde inte svara) lämnar ikonen orörd. Allt i placeringsrymden.
inline gfx::Rectf trayMenuAnchor(const gfx::Rectf& icon, const gfx::Rectf& work)
{
    if (work.w <= 0.0f || work.h <= 0.0f)
        return icon;
    const float x0 = std::clamp(icon.x, work.x, work.x + work.w);
    const float y0 = std::clamp(icon.y, work.y, work.y + work.h);
    const float x1 = std::clamp(icon.x + icon.w, work.x, work.x + work.w);
    const float y1 = std::clamp(icon.y + icon.h, work.y, work.y + work.h);
    return gfx::Rectf{x0, y0, x1 - x0, y1 - y0};
}

} // namespace glaze::detail
