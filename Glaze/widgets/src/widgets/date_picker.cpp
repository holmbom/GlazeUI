// DatePicker — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3): fältet är ett
// riktigt barn som ritar sig själv, kompositen ritar KNAPPEN — talfältets
// platta per familj med kalenderikonen i. Popupen och dess KALENDERVY (dagar,
// månader, år) bor här också, som `Dropdown::ListWindow`. Måtten
// kommer ur stilen (`date*`/`cal*`,
// ikonernas `iconFallback`/`iconStroke`, pilarnas `chevron*`), färgerna ur
// temat och kalenderräkningen ur den rena std-modellen (`../date_model.h`) —
// inga tal och inga färger i den här filen. Beslutens skäl står i
// date_picker.h.

#include "glaze/widgets/date_picker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/textinput.h"

#include "../date_model.h"
#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("datepicker", DatePicker)

namespace {

using datemodel::Datum;

bool före(const Datum& a, const Datum& b)
{
    return std::chrono::sys_days(a) < std::chrono::sys_days(b);
}

// ============================================================== KALENDERVYN
//
// TRE LÄGEN i samma ruta (Windows kalendervy):
//   DAGARNA    titeln "oktober 2026", veckodagarna i SPRÅKETS ordning och sex
//              rader dagar — alltid sex, så vyn har samma storlek varje månad
//   MÅNADERNA  titeln "2026" och årets tolv månader, ett kvartal per rad
//   ÅREN       titeln "2020–2029" och decenniet, med ett år före och ett
//              efter (grannmånadernas dagar i dagsläget)
// Ett tryck på TITELN går ett läge UPP, ett VAL i en översikt ett läge NED —
// året till månaderna, månaden till dagarna — och först en DAG väljer
// datumet. Popupen kan inte byta storlek när den väl visas (Wayland), så
// översikternas tolv rutor delar dagrutnätets yta: veckodagsraden och de sex
// dagraderna.
//
// Allt är DATA i en widget (ETT fokus, ListBoxens skäl). Vyns tillstånd är
// LÄGET och MARKÖREN — det provisoriska, ett datum i varje läge: dagen,
// månaden eller året det står i — och den VISADE månaden i dagläget (följer
// markören). Det VALDA, dagens datum och gränserna sätts av datumväljaren när
// popupen öppnas, och varje öppning börjar bland dagarna.
//
// TANGENTERNA per läge — Escape och Tab är popupens och stänger i VARJE läge:
//                 dagarna              månaderna           åren
//   pilarna       en dag / en vecka    en månad / ett      ett år / tre år
//                                      kvartal
//   PageUp/Down   en månad             ett år              ett decennium
//     med Ctrl    ett år               ett decennium       ett sekel
//   Home/End      månadens första/     januari/december    decenniets första/
//                 sista dag                                sista år
//   Enter/Space   VÄLJER dagen         ned till dagarna    ned till månaderna
//   Ctrl+Upp      upp till månaderna   upp till åren       (det översta)
// Ctrl är Cmd på Mac, som årssteget. Skälen till Ctrl+Upp står i headern.
class CalendarView final : public Widget {
public:
    // Valt — datumet; popupen stänger.
    std::function<void(Datum)> onPick;

    void setup(Datum vald, Datum lo, Datum hi, Datum idag)
    {
        vald_   = vald;
        lo_     = lo;
        hi_     = före(hi, lo) ? lo : hi;
        idag_   = idag;
        markör_ = datemodel::klampa(vald, lo_, hi_);
        visad_  = std::chrono::year_month{markör_.year(), markör_.month()};
        läge_   = Läge::Dagar;
        hover_  = -1;
        pil_    = 0;
        titel_  = false;
        rest_   = 0.0f;
        // VECKANS FÖRSTA DAG ur språket — ett ogiltigt värde ger ISO-veckans
        // måndag och en loggrad (en gång per öppning, aldrig i ritningen).
        const TrText förstaText = tr(str::FirstWeekday);
        förstaVd_ = datemodel::läsFörstaVeckodag(förstaText.str());
        if (förstaVd_ == 0) {
            gfx::log("DatePicker: veckans första dag '%s' är inget ISO-nummer "
                     "1–7 — måndag gäller", förstaText.str().c_str());
            förstaVd_ = 1;
        }
        invalidate();
    }

    Datum markör() const { return markör_; }

    bool focusable() const override { return true; }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            // AXELREGELN: satt axel vinner, AUTO är rutnätet ur stilen —
            // titelraden, veckodagsraden och sex rader dagar. Översikterna
            // delar ytan (se ovan), så dagarna bestämmer storleken.
            const auto& m = *c.style;
            gfx::Sizef  s{width, height};
            if (s.w > 0.0f && s.h > 0.0f)
                return s;
            if (s.w <= 0.0f)
                s.w = 2.0f * m.calPad +
                      static_cast<float>(datemodel::kVeckodagar) * m.calCellW;
            if (s.h <= 0.0f)
                s.h = 2.0f * m.calPad + m.calHeaderH +
                      static_cast<float>(datemodel::kRader + 1) * m.calCellH;
            return s;
        }
        }
    }

    void draw(const Ctx& c) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;

private:
    enum class Läge { Dagar, Månader, År };

    // VAD ett tryck träffar — vilken ruta står i `träff`s utargument.
    enum class Träff { Ingen, Bakåt, Framåt, Titel, Ruta };
    static int riktning(Träff t)
    {
        return t == Träff::Bakåt ? -1 : t == Träff::Framåt ? +1 : 0;
    }

    // GEOMETRIN — EN sanning i widget-lokala LOGISKA px, som tar ytan `b`
    // (gruppramens form): ritningen och träffen läser samma.
    gfx::Rectf titel(const Ctx& c, const gfx::Rectf& b) const
    {
        const auto& m = *c.style;
        return gfx::Rectf{b.x + m.calPad, b.y + m.calPad,
                          static_cast<float>(datemodel::kVeckodagar) * m.calCellW,
                          m.calHeaderH};
    }
    gfx::Rectf pilRect(const Ctx& c, const gfx::Rectf& b, int riktning) const
    {
        const gfx::Rectf t = titel(c, b);
        const float      w = c.style->calCellW;
        return riktning < 0 ? gfx::Rectf{t.x, t.y, w, t.h}
                            : gfx::Rectf{t.x + t.w - w, t.y, w, t.h};
    }
    // TITELKNAPPEN — titelraden mellan pilarna.
    gfx::Rectf titelKnapp(const Ctx& c, const gfx::Rectf& b) const
    {
        const gfx::Rectf t = titel(c, b);
        const float      w = c.style->calCellW;
        return gfx::Rectf{t.x + w, t.y, std::max(0.0f, t.w - 2.0f * w), t.h};
    }
    // Ruta `i` — rad 0 är VECKODAGARNA, dagarna börjar på rad 1.
    gfx::Rectf ruta(const Ctx& c, const gfx::Rectf& b, int i) const
    {
        const auto&      m = *c.style;
        const gfx::Rectf t = titel(c, b);
        return gfx::Rectf{t.x + static_cast<float>(i % datemodel::kVeckodagar) *
                                    m.calCellW,
                          t.y + t.h + static_cast<float>(i / datemodel::kVeckodagar) *
                                          m.calCellH,
                          m.calCellW, m.calCellH};
    }
    gfx::Rectf dagRuta(const Ctx& c, const gfx::Rectf& b, int i) const
    {
        return ruta(c, b, i + datemodel::kVeckodagar);
    }
    // ÖVERSIKTENS ruta `i` (0–11) — tolv rutor över dagrutnätets hela yta
    // (veckodagsraden och de sex dagraderna).
    gfx::Rectf översiktRuta(const Ctx& c, const gfx::Rectf& b, int i) const
    {
        const auto&      m = *c.style;
        const gfx::Rectf t = titel(c, b);
        const float      w = t.w / static_cast<float>(datemodel::kOversiktKolumner);
        const float      h = static_cast<float>(datemodel::kRader + 1) * m.calCellH /
                        static_cast<float>(datemodel::kOversiktRader);
        return gfx::Rectf{t.x + static_cast<float>(i % datemodel::kOversiktKolumner) * w,
                          t.y + t.h + static_cast<float>(i / datemodel::kOversiktKolumner) * h,
                          w, h};
    }
    // Översiktsrutans PLATTA — en dagrads höjd, mitt i rutan: en ruta som är
    // nästan dubbelt så hög som en dag hade blivit ett block.
    gfx::Rectf platta(const Ctx& c, const gfx::Rectf& r) const
    {
        const float h = std::min(r.h, c.style->calCellH);
        return gfx::Rectf{r.x, r.y + (r.h - h) / 2.0f, r.w, h};
    }

    Datum dagen(int i) const
    {
        return datemodel::plusDagar(datemodel::förstaRutan(visad_, förstaVd_),
                                    i);
    }
    bool inom(const Datum& d) const { return !före(d, lo_) && !före(hi_, d); }
    int  markörÅr() const { return static_cast<int>(markör_.year()); }
    int  årIRuta(int i) const
    {
        return datemodel::åretIRuta(datemodel::decennium(markörÅr()), i);
    }
    // Går översiktsruta `i` att välja i läget? (dagarna har `inom`.)
    bool rutanInom(int i) const
    {
        if (läge_ == Läge::Månader)
            return datemodel::månadenInom(
                std::chrono::year_month{markör_.year(),
                                        std::chrono::month{unsigned(i + 1)}},
                lo_, hi_);
        return datemodel::åretInom(årIRuta(i), lo_, hi_);
    }

    // En SIDA i månader — det titelpilarna, PageUp/PageDown och hjulet stegar:
    // en månad, ett år, ett decennium. `uppåt` läger högre ger Ctrl-steget.
    int sida(int uppåt = 0) const
    {
        const int nivå = static_cast<int>(läge_) + uppåt;
        int       steg = nivå > 0 ? datemodel::kManaderPerAr : 1;
        for (int k = 1; k < nivå; ++k)
            steg *= datemodel::kArPerSida;
        return steg;
    }

    // Träffen: en pil, titelknappen eller en ruta — rutans index via
    // `rutIndex` (dagen 0…41 eller översiktsrutan 0…11), annars −1.
    Träff träff(const Ctx& c, float mx, float my, int& rutIndex) const
    {
        const gfx::Rectf r  = resolve(c);
        const gfx::Rectf b  = Draw::bounds(c, r);
        const float      lx = Draw::localX(c, r, mx);
        const float      ly = Draw::localY(c, r, my);
        rutIndex = -1;
        if (pilRect(c, b, -1).contains(lx, ly))
            return Träff::Bakåt;
        if (pilRect(c, b, +1).contains(lx, ly))
            return Träff::Framåt;
        if (titelKnapp(c, b).contains(lx, ly))
            return Träff::Titel;
        const int n = läge_ == Läge::Dagar
                          ? datemodel::kRader * datemodel::kVeckodagar
                          : datemodel::kOversiktRutor;
        for (int i = 0; i < n; ++i) {
            const gfx::Rectf q =
                läge_ == Läge::Dagar ? dagRuta(c, b, i) : översiktRuta(c, b, i);
            if (q.contains(lx, ly)) {
                rutIndex = i;
                return Träff::Ruta;
            }
        }
        return Träff::Ingen;
    }

    // Markören till ett mål, KLAMPAD till gränserna; den visade månaden följer.
    void flytta(const Datum& mål)
    {
        const Datum ny = datemodel::klampa(mål, lo_, hi_);
        if (ny == markör_)
            return;
        markör_ = ny;
        visad_  = std::chrono::year_month{ny.year(), ny.month()};
        invalidate();
    }

    // LÄGESBYTET — pekarens rutindex gällde det gamla rutnätet.
    void byt(Läge l)
    {
        if (l == läge_)
            return;
        läge_  = l;
        hover_ = -1;
        titel_ = false;
        invalidate();
    }
    void upp()
    {
        if (läge_ == Läge::Dagar)
            byt(Läge::Månader);
        else if (läge_ == Läge::Månader)
            byt(Läge::År);
    }

    // VALET I EN ÖVERSIKT — markören till rutan (dagen står kvar, klampad
    // till månadens längd och till gränserna) och därefter ENTERS väg, ett läge
    // ned: musen och tangentbordet delar övergången. Månaden/året måste gå
    // att välja.
    void väljRuta(int i)
    {
        if (i < 0 || i >= datemodel::kOversiktRutor || !rutanInom(i))
            return;
        if (läge_ == Läge::Månader)
            flytta(datemodel::plusMånader(
                markör_, i + 1 - static_cast<int>(unsigned(markör_.month()))));
        else
            flytta(datemodel::plusMånader(
                markör_, (årIRuta(i) - markörÅr()) * datemodel::kManaderPerAr));
        väljMarkören();
    }
    // Enter och Space — dagen väljs, en översikt går ned vid markören.
    void väljMarkören()
    {
        if (läge_ == Läge::Dagar)
            välj(markör_);
        else if (läge_ == Läge::Månader)
            byt(Läge::Dagar);
        else
            byt(Läge::Månader);
    }

    void välj(const Datum& d)
    {
        if (inom(d) && onPick)
            onPick(d);
    }

    void ritaDagar(const Ctx& c, const gfx::Rectf& b) const;
    void ritaÖversikt(const Ctx& c, const gfx::Rectf& b) const;
    // EN ruta i dagarnas fyra kanaler (se `ritaDagar`): `yta` fylls och
    // ringas, `text` står mitt i `r`.
    void ritaRuta(const Ctx& c, const gfx::Rectf& r, const gfx::Rectf& yta,
                  const std::string& text, bool vald, bool idag, bool markör,
                  bool tänd, bool dämpad) const;

    Datum                   vald_{}, lo_{}, hi_{}, idag_{}, markör_{};
    std::chrono::year_month visad_{};
    Läge                    läge_     = Läge::Dagar;
    int                     förstaVd_ = 1;       // ISO 1 = måndag … 7 = söndag
    int                     hover_    = -1;      // rutan under pekaren
    int                     pil_      = 0;       // pilen under pekaren (−1/+1)
    bool                    titel_    = false;   // titelknappen under pekaren
    float                   rest_     = 0.0f;    // hjulets bråkdel (pekplattor)
};

void CalendarView::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const gfx::Rectf t   = titel(c, b);

    // YTAN — dropdownlistans och färgväljarens popup: en yta man läser i,
    // fyrkantig (popupen är opak).
    switch (c.ui) {
    case UI::Glaze:
    case UI::Minimalistic:
        Draw::rect(c, b, 0.0f, col.panel, Pen{col.buttonBorder, m.borderWidth});
        break;
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel, Pen{col.border, m.borderWidth});
        break;
    }

    // TITELRADEN — läget i ord: månaden på språket och året, året, eller
    // decenniet. Titeln är en KNAPP upp till nästa läge (inte i det översta),
    // och den tända får verktygsknappens platta, som pilarna i var sin ände
    // (dropdownens vinkelhake vriden).
    std::string rubrik;
    if (läge_ == Läge::Dagar) {
        const TrText månad =
            tr(str::MonthJanuary + static_cast<unsigned>(visad_.month()) - 1u);
        rubrik = månad.str() + " " +
                 std::to_string(static_cast<int>(visad_.year()));
    } else if (läge_ == Läge::Månader) {
        rubrik = std::to_string(markörÅr());
    } else {
        const int d = datemodel::decennium(markörÅr());
        rubrik = std::to_string(d) + "–" +
                 std::to_string(d + datemodel::kArPerSida - 1);
    }
    if (hovered() && titel_ && läge_ != Läge::År)
        Draw::rect(c, titelKnapp(c, b), m.calCellRadius, col.buttonHover);
    Draw::text(c, t, rubrik, col.text, Align::Center);
    for (int riktning : {-1, +1}) {
        const gfx::Rectf r = pilRect(c, b, riktning);
        if (hovered() && pil_ == riktning)
            Draw::rect(c, r, m.calCellRadius, col.buttonHover);
        const float w = 2.0f * m.chevronH;   // vriden: ‹ › är ∧ ∨ på sidan
        const float h = 2.0f * m.chevronW;
        Draw::chevron(c,
                      {r.x + (r.w - w) / 2.0f, r.y + (r.h - h) / 2.0f, w, h},
                      riktning < 0 ? Dir::Left : Dir::Right,
                      Pen{col.text, 0.0f});
    }

    if (läge_ == Läge::Dagar)
        ritaDagar(c, b);
    else
        ritaÖversikt(c, b);
}

// DAGARNA. Fyra tillstånd i fyra kanaler, så de kan stå på samma ruta:
//   vald      fyllning (receptet per familj i `ritaRuta`)
//   dagens    talet i `accent` och en hårlinjering `accent`
//   markören  fokusringen INNANFÖR rutan (fokusringens tjocklek), bara när
//             vyn har fokus — innanför, så den syns innanför dagens
//   pekaren   `buttonHover`-platta, bara på en ruta som går att välja
// En dag utanför den visade månaden eller utanför gränserna har talet i
// `textDim`; utanför gränserna går den varken att peka på eller välja.
// Översikternas rutor följer EXAKT samma kanaler.
void CalendarView::ritaDagar(const Ctx& c, const gfx::Rectf& b) const
{
    const auto& col = c.theme->colors;
    // VECKODAGARNA i språkets ordning — veckans första dag först.
    for (int k = 0; k < datemodel::kVeckodagar; ++k) {
        const int    iso  = (förstaVd_ - 1 + k) % datemodel::kVeckodagar;
        const TrText namn = tr(str::DayMonday + static_cast<unsigned>(iso));
        Draw::text(c, ruta(c, b, k), namn.str(), col.textDim, Align::Center);
    }
    for (int i = 0; i < datemodel::kRader * datemodel::kVeckodagar; ++i) {
        const Datum      d      = dagen(i);
        const gfx::Rectf r      = dagRuta(c, b, i);
        const bool       giltig = inom(d);
        ritaRuta(c, r, r, std::to_string(static_cast<unsigned>(d.day())),
                 d == vald_, d == idag_, focused() && d == markör_,
                 hovered() && hover_ == i && giltig,
                 !giltig || d.month() != visad_.month());
    }
}

// ÖVERSIKTERNA — månaderna med språkets namn, åren som tal. En månad eller
// ett år UTANFÖR gränserna är släckt och går inte att välja; årslägets
// yttersta rutor (året före och efter decenniet) står dämpade som
// grannmånadernas dagar, och ett år formatet inte kan skriva ritas inte.
void CalendarView::ritaÖversikt(const Ctx& c, const gfx::Rectf& b) const
{
    const bool månader = läge_ == Läge::Månader;
    const int  år      = markörÅr();
    for (int i = 0; i < datemodel::kOversiktRutor; ++i) {
        const gfx::Rectf r      = översiktRuta(c, b, i);
        const bool       giltig = rutanInom(i);
        if (månader) {
            const unsigned mån = unsigned(i + 1);
            const auto     är  = [&](const Datum& d) {
                return static_cast<int>(d.year()) == år &&
                       unsigned(d.month()) == mån;
            };
            const TrText namn = tr(str::MonthJanuary + mån - 1u);
            ritaRuta(c, r, platta(c, r), namn.str(), är(vald_), är(idag_),
                     focused() && unsigned(markör_.month()) == mån,
                     hovered() && hover_ == i && giltig, !giltig);
        } else {
            const int y = årIRuta(i);
            if (!datemodel::åretFinns(y))
                continue;
            const auto är = [y](const Datum& d) {
                return static_cast<int>(d.year()) == y;
            };
            const bool utanför =
                i == 0 || i == datemodel::kOversiktRutor - 1;
            ritaRuta(c, r, platta(c, r), std::to_string(y), är(vald_),
                     är(idag_), focused() && y == år,
                     hovered() && hover_ == i && giltig, !giltig || utanför);
        }
    }
}

void CalendarView::ritaRuta(const Ctx& c, const gfx::Rectf& r,
                            const gfx::Rectf& yta, const std::string& text,
                            bool vald, bool idag, bool markör, bool tänd,
                            bool dämpad) const
{
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;
    gfx::Color  fyll = col.panel;
    gfx::Color  tal  = dämpad ? col.textDim : idag ? col.accent : col.text;
    bool        fylld = false;
    if (vald) {
        switch (c.ui) {
        // GLAZE och MINIMALISTIC: accentYTAN med `onAccent` — det grindade
        // paret (ListBoxens Glaze-rad, kryssrutans Minimalistic-fyllning:
        // "fyllning betyder tillstånd"). Ingen laminering — en ruta har
        // ingen kontur att bära den.
        case UI::Glaze:
        case UI::Minimalistic:
            fyll = col.accentSurface;
            tal  = col.onAccent;
            break;
        // SLATE: `accent` och `windowBg`, ListBoxens valda rad.
        case UI::Slate:
        default:
            fyll = col.accent;
            tal  = col.windowBg;
            break;
        }
        fylld = true;
    } else if (tänd) {
        fyll  = col.buttonHover;
        fylld = true;
    }
    if (fylld)
        Draw::rect(c, yta, m.calCellRadius, fyll);
    if (idag)
        Draw::frame(c, yta, m.calCellRadius,
                    Pen{vald ? tal : col.accent, m.borderWidth});
    if (markör)
        Draw::frame(c, Draw::inset(yta, m.focusWidth),
                    std::max(0.0f, m.calCellRadius - m.focusWidth),
                    Pen{vald ? tal : col.accent, m.focusWidth});
    Draw::textElided(c, r, text, tal, Align::Center);
}

// Tangenterna per läge — tabellen står vid klassen. Markören stannar inom
// gränserna i varje läge (`flytta` klampar).
bool CalendarView::keyDown(gfx::Key k, std::uint32_t mods)
{
    const bool ctrl = (mods & (gfx::kModCtrl | gfx::kModCmd)) != 0;
    const int  år   = markörÅr();
    const int  mån  = static_cast<int>(unsigned(markör_.month()));
    // Ett steg i rutnätet: en dag eller en månad eller ett år — och raden.
    const int  enhet = läge_ == Läge::År ? datemodel::kManaderPerAr : 1;
    auto stega = [&](int rutor) {
        if (läge_ == Läge::Dagar)
            flytta(datemodel::plusDagar(markör_, rutor));
        else
            flytta(datemodel::plusMånader(markör_, rutor * enhet));
    };
    const int rad = läge_ == Läge::Dagar ? datemodel::kVeckodagar
                                         : datemodel::kOversiktKolumner;
    switch (k) {
    case gfx::Key::Left:
        stega(-1);
        return true;
    case gfx::Key::Right:
        stega(+1);
        return true;
    case gfx::Key::Up:
        if (ctrl)
            upp();
        else
            stega(-rad);
        return true;
    case gfx::Key::Down:
        stega(+rad);
        return true;
    case gfx::Key::PageUp:
        flytta(datemodel::plusMånader(markör_, -sida(ctrl ? 1 : 0)));
        return true;
    case gfx::Key::PageDown:
        flytta(datemodel::plusMånader(markör_, +sida(ctrl ? 1 : 0)));
        return true;
    case gfx::Key::Home:
        if (läge_ == Läge::Dagar)
            flytta(Datum{markör_.year(), markör_.month(), std::chrono::day{1}});
        else if (läge_ == Läge::Månader)
            flytta(datemodel::plusMånader(markör_, 1 - mån));
        else
            flytta(datemodel::plusMånader(
                markör_, (datemodel::decennium(år) - år) * datemodel::kManaderPerAr));
        return true;
    case gfx::Key::End:
        if (läge_ == Läge::Dagar)
            flytta(datemodel::sistaIMånaden(markör_));
        else if (läge_ == Läge::Månader)
            flytta(datemodel::plusMånader(markör_, datemodel::kManaderPerAr - mån));
        else
            flytta(datemodel::plusMånader(
                markör_, (datemodel::decennium(år) + datemodel::kArPerSida - 1 - år) *
                             datemodel::kManaderPerAr));
        return true;
    case gfx::Key::Return:
    case gfx::Key::Space:
        väljMarkören();
        return true;
    default:
        return false;
    }
}

// Ett tryck på en pil stegar en sida (som PageUp/PageDown), på titeln går det
// ett läge upp, och på en ruta som går att välja VÄLJER det: en dag — också i
// grannmånaden — väljer datumet, en månad eller ett år går ett läge ned.
void CalendarView::mouseDown(const Ctx& c, float mx, float my)
{
    int         i   = -1;
    const Träff vad = träff(c, mx, my, i);
    if (riktning(vad) != 0) {
        flytta(datemodel::plusMånader(markör_, riktning(vad) * sida()));
        return;
    }
    if (vad == Träff::Titel) {
        upp();
        return;
    }
    if (vad != Träff::Ruta)
        return;
    if (läge_ != Läge::Dagar) {
        väljRuta(i);
        return;
    }
    const Datum d = dagen(i);
    if (!inom(d))
        return;
    markör_ = d;
    invalidate();
    välj(d);
}

void CalendarView::mouseMove(const Ctx& c, float mx, float my)
{
    int         i     = -1;
    const Träff vad   = träff(c, mx, my, i);
    const int   pil   = riktning(vad);
    const bool  titel = vad == Träff::Titel && läge_ != Läge::År;
    if (pil != pil_ || i != hover_ || titel != titel_) {   // hård regel 1
        pil_   = pil;
        hover_ = i;
        titel_ = titel;
        invalidate();
    }
}

// HJULET BLÄDDRAR EN SIDA — det är VYN som rullar, inte värdet som ändras
// (det som rullar tar hjulet): en månad, ett år eller ett decennium.
// Hjul upp = bakåt i tiden, som en lista som rullas uppåt. Hela steg:
// bråkdelen från ett pekplattehjul sparas tills den blivit ett (talfältets
// recept).
bool CalendarView::mouseWheel(const Ctx& c, float, float, float wheelX,
                              float wheelY)
{
    const float w = wheelY != 0.0f ? wheelY : wheelX;
    if (w == 0.0f)
        return false;
    rest_ += wheelNotches(c, w);
    const int n = static_cast<int>(std::trunc(rest_));
    if (n != 0) {
        rest_ -= static_cast<float>(n);
        flytta(datemodel::plusMånader(markör_, -n * sida()));
    }
    return true;
}

} // namespace

// ================================================================ POPUPEN
//
// Färgväljarens popupform — `Chrome::None` + popup-attributet som TAR
// TANGENTBORDET (`setPopupTakesKeys`) — med kalendervyn som enda
// innehåll. Escape stänger (attributets), och TAB också — vyn är popupens
// enda stopp, så en Tab i popupen har ingenstans att gå, och samma sak på
// varje plattform (på Wayland kan den inte heller nå ägaren). Det gäller i
// VARJE läge: översikterna är vyer i samma popup, inte popuper i popupen.
class DatePicker::Popup final : public AppWindow {
public:
    explicit Popup(DatePicker& p)
        : AppWindow("", 100, 40, gfx::Chrome::None), picker_(p)
    {
        setPopup(true);
        setPopupTakesKeys(true);
        view_       = add<CalendarView>();
        view_->dock = Dock::Fill;
        view_->name = "calendar";
        // VALET STÄNGER, och stängningen köas FÖRE händelsen (menyns
        // aktiveringsordning — färgväljarens palett gör likadant).
        view_->onPick = [this](Datum d) {
            close();
            picker_.dateFromUser(d);
        };
        onClose = [this] { picker_.popupClosed(); };
    }

    CalendarView& view() { return *view_; }

protected:
    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::KeyDown && e.key == gfx::Key::Tab) {
            close();
            return true;
        }
        return AppWindow::event(e);
    }

private:
    DatePicker&   picker_;
    CalendarView* view_ = nullptr;
};

// ================================================================== FÄLTET
//
// En riktig `TextInput` med tre tillägg och inget annat — talfältets
// `SpinBox::Field`-form:
//   * MED ÖPPEN POPUP når tangenterna aldrig hit: popupen tar tangentbordet
//     (`setPopupTakesKeys`), och ägarfönstret skickar dem dit först.
//   * STÄNGD öppnar Ned (också med Alt) och F4 kalendern — efter att texten
//     committats, så kalendern visar det man skrev — och Enter committar.
//   * Bara SIFFROR och `-` tas emot; fokusförlust committar texten
//     och stänger en öppen popup. `focusLost` rör bara datumet, fältets egen
//     text och popupens liv — eventen POSTAS, aldrig en handler direkt.
class DatePicker::Field final : public TextInput {
public:
    explicit Field(DatePicker& owner) : owner_(owner) {}

protected:
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        switch (k) {
        case gfx::Key::Down:
        case gfx::Key::F4:
            if (owner_.textFromUser())   // en handler får riva datumväljaren
                owner_.open();
            return true;
        case gfx::Key::Return:
            // COMMITTAR OCH BUBBLAR — `TextInput`s
            // kontrakt, "Enter konsumeras inte": i en dialog committar Enter
            // datumet OCH trycker standardknappen, med det committade värdet.
            // Svaret (livet) behövs inte — inget här rör `this` efteråt.
            owner_.textFromUser();
            return false;
        default:
            return TextInput::keyDown(k, mods);
        }
    }

    bool textInput(const char* utf8) override
    {
        // Med KLOCKSLAGET också tidens avskiljare och den som
        // skiljer datumet från tiden (blanksteg eller ISO:s `T`).
        const bool  tid = owner_.showTime_;
        std::string ok;
        for (const char* p = utf8; *p; ++p)
            if ((*p >= '0' && *p <= '9') || *p == '-' ||
                (tid && (*p == ':' || *p == ' ' || *p == 'T' || *p == 't')))
                ok.push_back(*p);
        if (ok.empty())
            return true;   // sväljs — inget annat hör hemma i ett ISO-datum
        return TextInput::textInput(ok.c_str());
    }

    void focusLost() override
    {
        TextInput::focusLost();
        owner_.commitText(true);
        owner_.close();
    }

private:
    DatePicker& owner_;
};

// ============================================================ DATUMVÄLJAREN

DatePicker::DatePicker()
    : value_(datemodel::idag()),
      time_(datemodel::nuMinuter()),
      min_(std::chrono::year{datemodel::kForstaAr} / std::chrono::January /
           std::chrono::day{1}),
      max_(std::chrono::year{datemodel::kSistaAr} / std::chrono::December /
           std::chrono::last)
{
    field_       = add<Field>(*this);
    field_->dock = Dock::Fill;   // fyller kompositens vänstra del (childArea)
    // Fältets text prövas INTE medan man skriver — den committas vid
    // Enter, fokusförlust och öppning (`Field`), så fältets `onChange` lämnas
    // osatt.
    syncText();
}

DatePicker::~DatePicker() = default;   // Popup komplett här — se headern

// ------------------------------------------------------------- värdet

Datum DatePicker::date() const
{
    return datemodel::klampa(value_, min_, max_);
}

void DatePicker::setDate(int year, int month, int day)
{
    Datum d{};
    if (!datemodel::datum(year, month, day, d)) {
        gfx::log("DatePicker(%s): setDate(%d, %d, %d) är inget datum — "
                 "ignoreras", name.c_str(), year, month, day);
        return;
    }
    setDate(d);
}

void DatePicker::setDate(std::chrono::year_month_day d)
{
    if (!datemodel::giltigt(d)) {
        gfx::log("DatePicker(%s): setDate med ett ogiltigt datum — ignoreras",
                 name.c_str());
        return;
    }
    if (d == value_)
        return;
    value_ = d;
    syncText();
    invalidate();
    close();   // en öppen kalender visar det gamla — stängd är ärligare
}

void DatePicker::setMinDate(int year, int month, int day)
{
    Datum d{};
    if (!datemodel::datum(year, month, day, d)) {
        gfx::log("DatePicker(%s): setMinDate(%d, %d, %d) är inget datum — "
                 "ignoreras", name.c_str(), year, month, day);
        return;
    }
    setMinDate(d);
}

void DatePicker::setMinDate(std::chrono::year_month_day d)
{
    if (!datemodel::giltigt(d)) {
        gfx::log("DatePicker(%s): setMinDate med ett ogiltigt datum — "
                 "ignoreras", name.c_str());
        return;
    }
    if (d == min_)
        return;
    min_ = d;
    syncText();
    invalidate();
    close();
}

void DatePicker::setMaxDate(int year, int month, int day)
{
    Datum d{};
    if (!datemodel::datum(year, month, day, d)) {
        gfx::log("DatePicker(%s): setMaxDate(%d, %d, %d) är inget datum — "
                 "ignoreras", name.c_str(), year, month, day);
        return;
    }
    setMaxDate(d);
}

void DatePicker::setMaxDate(std::chrono::year_month_day d)
{
    if (!datemodel::giltigt(d)) {
        gfx::log("DatePicker(%s): setMaxDate med ett ogiltigt datum — "
                 "ignoreras", name.c_str());
        return;
    }
    if (d == max_)
        return;
    max_ = d;
    syncText();
    invalidate();
    close();
}

const std::string& DatePicker::text() const
{
    return field_->text();
}

void DatePicker::selectAll()
{
    field_->selectAll();
}

void DatePicker::setSelection(int begin, int end)
{
    field_->setSelection(begin, end);
}

std::chrono::year_month_day DatePicker::provisionalDate() const
{
    return open_ && popup_ ? popup_->view().markör() : Datum{};
}

std::string DatePicker::dateText() const
{
    return datemodel::iso(date());
}

std::string DatePicker::timeText() const
{
    return datemodel::hhmm(time_);
}

// ------------------------------------------------- klockslaget

void DatePicker::setShowTime(bool on)
{
    if (showTime_ == on)
        return;
    showTime_ = on;
    syncText();   // fältets form byts (AUTO-bredden också — `contentSize`)
    invalidate();
}

void DatePicker::setTime(int hour, int minute)
{
    std::chrono::minutes t{0};
    if (!datemodel::tid(hour, minute, t)) {
        gfx::log("DatePicker(%s): setTime(%d, %d) är inget klockslag — "
                 "ignoreras", name.c_str(), hour, minute);
        return;
    }
    setTime(t);
}

void DatePicker::setTime(std::chrono::minutes timeOfDay)
{
    if (!datemodel::giltigTid(timeOfDay)) {
        gfx::log("DatePicker(%s): setTime med ett klockslag utanför dygnet — "
                 "ignoreras", name.c_str());
        return;
    }
    if (timeOfDay == time_)
        return;
    time_ = timeOfDay;
    syncText();
    invalidate();
}

void DatePicker::syncText()
{
    field_->setText(showTime_ ? dateText() + " " + timeText() : dateText());
}

// ------------------------------------------------------- användarvägarna

// POPUPENS VAL — texten skrivs ALLTID om (en halvskriven text i fältet ska
// inte stå kvar efter ett val), händelserna bara när datumet ändrades.
void DatePicker::dateFromUser(std::chrono::year_month_day d)
{
    if (!datemodel::giltigt(d))
        return;
    const Datum förut = date();
    value_            = datemodel::klampa(d, min_, max_);
    syncText();
    invalidate();
    if (date() != förut)
        fireChange();   // datumet läses ur sender (date())
}

bool DatePicker::textFromUser()
{
    return commitText(false);
}

// `commitEdit` (widget.h) är Enter utan tangenten.
void DatePicker::commitEdit()
{
    commitText(false);
}

// FÄLTETS TEXT — committad vid Enter, öppning (`postad` falsk) och
// fokusförlust (sann): en text som tolkas blir datumet (klampat), det som
// inte tolkas lämnar det orört — och texten skrivs ALLTID om till det som
// gäller. Datumet och texten är skrivna innan eventen fyras eller postas.
bool DatePicker::commitText(bool postad)
{
    const Datum                förut   = date();
    const std::chrono::minutes förTid  = time_;
    Datum                      d{};
    // Med klockslaget tolkas datum + tid; en text med bara datumet
    // behåller tiden. Utan klockslag är formen datumet ensamt.
    std::optional<std::chrono::minutes> t;
    if (showTime_ ? datemodel::läsIsoTid(field_->text(), d, t)
                  : datemodel::läsIso(field_->text(), d)) {
        value_ = datemodel::klampa(d, min_, max_);
        if (t)
            time_ = *t;
    }
    syncText();   // fältet invaliderar sig självt om texten skrivs om
    if (date() == förut && time_ == förTid)
        return true;   // inget committades — inget att rita om
    invalidate();
    if (!postad)
        return fireChange();
    postEvent([this] { fireChange(); });
    return true;
}

// `onChange` först och `onCommit` sedan — samma handling (headern) — med
// vakten emellan (`Widget::firePair`).
bool DatePicker::fireChange()
{
    return firePair(onChange, onCommit);
}

bool DatePicker::setProp(const std::string& key, const std::string& value)
{
    if (key == "date" || key == "minDate" || key == "maxDate") {
        Datum d{};
        if (!datemodel::läsIso(value, d)) {
            gfx::log("DatePicker(%s): '%s' är inget datum ÅÅÅÅ-MM-DD för %s "
                     "(ignoreras)", name.c_str(), value.c_str(), key.c_str());
            return true;   // nyckeln är känd — värdet är fel
        }
        if (key == "date")         setDate(d);
        else if (key == "minDate") setMinDate(d);
        else                       setMaxDate(d);
        return true;
    }
    if (key == "time") {
        std::chrono::minutes t{0};
        if (!datemodel::läsTid(value, t)) {
            gfx::log("DatePicker(%s): '%s' är inget klockslag HH:MM "
                     "(ignoreras)", name.c_str(), value.c_str());
            return true;   // nyckeln är känd — värdet är fel
        }
        setTime(t);
        return true;
    }
    if (key == "showTime") {
        setShowTime(propparse::parseBool(value));
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// ------------------------------------------------------------- popupen

void DatePicker::open()
{
    if (open_ || !isEnabled() || !appWindow() || !appWindow()->isOpen())
        return;
    openAt(behaviorCtx());
}

void DatePicker::openAt(const Ctx& c)
{
    if (!popup_)
        popup_ = std::make_unique<Popup>(*this);
    CalendarView& vy = popup_->view();
    vy.setup(date(), min_, max_, datemodel::idag());
    // STORLEKEN FÖRE VISNINGEN (en popup kan inte växa efteråt) — rutnätet
    // har samma storlek varje månad, så den räcker för hela besöket.
    const gfx::Sizef s = measure(vy, c);
    popup_->setPopupAnchor(resolve(c));
    popup_->setClientSize(static_cast<int>(std::ceil(s.w)),
                          static_cast<int>(std::ceil(s.h)));
    open_ = true;
    popup_->show(*appWindow());
    if (!popup_->isOpen()) {
        gfx::log("DatePicker(%s): popup-fönstret kom aldrig upp — stänger av",
                 name.c_str());
        popupClosed();
        return;
    }
    vy.focus();   // tangenterna arbetar i rutnätet direkt
    // Dropdownens tryck-och-dra-städning: capture och tryckt läge släpps.
    appWindow()->window().releaseMouseCapture();
    pressed_ = false;
    invalidate();
}

void DatePicker::close()
{
    if (open_ && popup_)
        popup_->close();   // uppskjuten → onClose → popupClosed
}

void DatePicker::popupClosed()
{
    open_ = false;
    invalidate();
}

// ----------------------------------------------------- kompositkrokarna

Widget* DatePicker::focusProxy() const
{
    return field_;
}

bool DatePicker::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(field_);
}

// `enabled` kaskadar inte i ramverket — fältet är INREDNING, och en inaktiv
// datumväljare har ingen öppen kalender.
void DatePicker::enabledChanged()
{
    if (isEnabled()) {
        field_->enable();
    } else {
        field_->disable();
        close();
    }
}

// ------------------------------------------------------------ geometrin

// Knappen längst till höger, hela höjden; fältet får resten minus luften.
gfx::Rectf DatePicker::buttonRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float w = std::min(c.style->dateButtonW, b.w);
    return gfx::Rectf{b.x + b.w - w, b.y, w, b.h};
}

bool DatePicker::overButton(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r = resolve(c);
    return buttonRect(c, Draw::bounds(c, r))
        .contains(Draw::localX(c, r, mx), Draw::localY(c, r, my));
}

gfx::Rectf DatePicker::childArea(const Ctx& c) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const gfx::Rectf kn = buttonRect(c, b);
    return Draw::project(
        c, r, {b.x, b.y, std::max(0.0f, kn.x - c.style->dateGap - b.x), b.h});
}

gfx::Sizef DatePicker::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner. AUTO-bredden är stilens
        // `dateDefaultW` (med klockslaget `dateTimeDefaultW`),
        // AUTO-höjden FÄLTETS — datumväljaren står i rad med ett textfält och
        // ett talfält i varje stil (talfältets recept).
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f)
            s.w = showTime_ ? c.style->dateTimeDefaultW : c.style->dateDefaultW;
        if (s.h <= 0.0f) s.h = measure(*field_, c).h;
        return s;
    }
    }
}

// ------------------------------------------------------------ beteendet

// Ett tryck på KNAPPEN togglar kalendern — dropdownens ordning: ett tryck
// medan den är öppen har redan köat stängningen (App::closePopups), och
// vägen blir `close()` (idempotent). Fokus har redan gått till fältet
// (fokusombudet) innan kroken körs, och en skriven text committas innan
// kalendern öppnas, som vid Ned och F4.
void DatePicker::mouseDown(const Ctx& c, float mx, float my)
{
    if (!overButton(c, mx, my))
        return;
    if (open_) {
        close();
    } else {
        if (!textFromUser())
            return;   // commitens handler rev eller flyttade datumväljaren
        open();
    }
    invalidate();
}

void DatePicker::mouseMove(const Ctx& c, float mx, float my)
{
    const bool över = overButton(c, mx, my);
    if (över != hover_) {   // hård regel 1: bara vid ändring
        hover_ = över;
        invalidate();
    }
}

// ------------------------------------------------------------ ritningen

void DatePicker::draw(const Ctx& c)
{
    const gfx::Rectf kn  = buttonRect(c, c.bounds());
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const bool       på  = isEnabled();
    const bool       tänd   = på && hovered() && hover_;
    const bool       öppen  = på && isOpen();
    const float      rad    = m.cornerRadius;

    // Kalenderikonen — en RITAD ikon (`Draw::icon`, filväljarens), i
    // ikonernas mått och mitt i knappen.
    auto ikon = [&](gfx::Color färg) {
        const float s = std::min(m.iconFallback, std::min(kn.w, kn.h));
        Draw::icon(c,
                   {kn.x + (kn.w - s) / 2.0f, kn.y + (kn.h - s) / 2.0f, s, s},
                   Icon::Calendar, Pen{färg, m.iconStroke});
    };
    const gfx::Color kropp = !på      ? col.buttonDisabled
                             : öppen ? col.buttonPress
                             : tänd  ? col.buttonHover
                                     : col.button;

    switch (c.ui) {
    // GLAZE — knappen är en Glaze-PLATTA (talfältets stegare): tonad kropp,
    // laminering och bestämd kontur; en ÖPPEN kalender trycker ner den.
    //
    //   vila       `button` + laminering + kontur `buttonBorder`
    //   hover      `buttonHover`
    //   öppen      `buttonPress`, ingen laminering
    //   inaktiv    `buttonDisabled`, kontur `border`, ikonen `textDim`
    //   (fokus är FÄLTETS — dess ring, dess caret)
    case UI::Glaze:
        Draw::rect(c, kn, rad, kropp);
        if (på && !öppen)
            Draw::laminate(c, kn, rad, col.sheen, m.sheenLower, col.sheenHigh,
                           m.sheenUpper);
        Draw::frame(c, kn, rad,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        ikon(på ? col.text : col.textDim);
        break;

    // MINIMALISTIC — "Outline": kanten är formen, fyllningen tillståndet;
    // en öppen kalender bär kanten i `accent` (talfältets tryck).
    //
    //   vila       `button` + kant `buttonBorder`
    //   hover      `buttonHover`
    //   öppen      `buttonPress`, kanten `accent`
    //   inaktiv    `buttonDisabled`, kant `border`, ikonen `textDim`
    case UI::Minimalistic:
        Draw::rect(c, kn, rad, kropp);
        Draw::frame(c, kn, rad,
                    Pen{!på     ? col.border
                        : öppen ? col.accent
                                : col.buttonBorder,
                        m.borderWidth});
        ikon(på ? col.text : col.textDim);
        break;

    // SLATE — en PLATTA i knappens färger, utan lyft (den sitter tätt intill
    // fältet); kanten tänds i `accent` under pekaren och medan kalendern är
    // öppen.
    //
    //   vila       `button` + hårlinje `border`
    //   hover      `buttonHover`, kanten `accent`
    //   öppen      `buttonPress`, kanten `accent`
    //   inaktiv    `buttonDisabled`, ikonen `textDim`
    case UI::Slate:
    default:
        Draw::rect(c, kn, rad, kropp,
                   Pen{tänd || öppen ? col.accent : col.border, m.borderWidth});
        ikon(på ? col.text : col.textDim);
        break;
    }
}

} // namespace glaze
