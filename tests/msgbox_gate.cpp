// msgbox_gate — MEDDELANDERUTANS grind. Headless: ingen
// display, inget fönster, ingen pump.
//
// Tre saker mäts, och alla tre är sådana som annars bara syns när någon
// råkar klicka på rätt knapp i rätt tema:
//
//   1. RADBRYTNINGEN — ordgräns, explicit '\n', ett för långt
//      UTF-8-ord, tom text och avslutande newline. Mätt mot en FALSK font
//      med känt teckensteg, så brytpunkterna är räknade och inte
//      uppskattade. Och mätt i TVÅ SKALOR: `Draw::measureWrapped` tar
//      logiska px in och ska ge logiska px ut, alltså SAMMA svar oavsett
//      DPI. Enhetsblandning är den fällan primitiven var mest utsatt för.
//   2. LABEL::WRAP:s auto-höjd i `contentSize` — med en EXPLICIT
//      satt bredd bryts höjden ur den, och utan bredd är `contentSize` EN
//      rad: den frågas innan `resolve()` räknat fram en sträckt bredd. Den
//      sträckta och den dockade etiketten får sin höjd ur
//      `heightForWidth` med den bredd layouten ger — det mäter layout_gate
//      fall 20, inte den här grinden (tabellen står i label.h).
//   3. RESULTATMAPPNINGEN — alla SEX knappuppsättningar, deras
//      knappar i visuell ordning, och avbryt-ekvivalenten som gör att
//      `DialogResult::None` aldrig läcker ut ur `messageBox()`.
//
// Vad som INTE mäts här och varför: själva DIALOGEN kräver en font, ett
// OS-fönster och en nästlad pump, alltså en display. Layoutformeln är
// verifierad i den körda demon (`second_window`s "Meddelande…"). Enter och
// Escape är fönstrets standard- och avbrytknapp: att
// rutan kopplar dem rätt i alla sex uppsättningar — och att svaret kommer ur
// knappens `dialogResult` — mäter dialog_gate (W9), och vägen genom ingången
// dess W1–W4.

#include <cstdio>
#include <algorithm>
#include <memory>
#include <cstring>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/draw.h>
#include <glaze/style.h>
#include <glaze/theme.h>
#include <glaze/language.h>
#include <glaze/widgets/button.h>
#include <glaze/widgets/label.h>
#include <glaze/widgets/panel.h>

#include "message_box_table.h"
#include "status_badge.h"

namespace {

int fel = 0;

void ok(bool cond, const std::string& vad)
{
    std::printf("%-6s %s\n", cond ? "OK" : "FEL", vad.c_str());
    if (!cond)
        ++fel;
}

// ------------------------------------------------------------ falsk font
//
// Ett FAST teckensteg per KODPUNKT gör brytpunkterna räknebara: "abc def"
// vid fyra tecken bredd bryter efter "abc", och det är en siffra man kan
// skriva i testet i stället för en förhoppning. Fonten talar FYSISKA px
// precis som de riktiga gör — därför tar den `adv`/`h` som argument och
// skapas en gång per skala (adv = 10 × scale), exakt som `reloadFont()`
// skapar en ny atlas vid skalbyte.
class FakeFont final : public gfx::NullFont {
public:
    FakeFont(float adv, float h) : adv_(adv), h_(h) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv_ * static_cast<float>(codepoints(s, s.size())),
                          h_};
    }
    float height() const override { return h_; }
    bool  valid() const override { return true; }

    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return adv_ * static_cast<float>(codepoints(s, bytes));
    }

    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size() && w + adv_ / 2.0f < x) {
            i = next(s, i);
            w += adv_;
        }
        return i;
    }

    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size() && w + adv_ <= maxW) {
            i = next(s, i);
            w += adv_;
        }
        return i;
    }

private:
    // UTF-8-gränsen: fortsättningsbytes är 10xxxxxx.
    static std::size_t next(const std::string& s, std::size_t i)
    {
        if (i >= s.size())
            return s.size();
        ++i;
        while (i < s.size() &&
               (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            ++i;
        return i;
    }
    static std::size_t codepoints(const std::string& s, std::size_t bytes)
    {
        if (bytes > s.size())
            bytes = s.size();
        std::size_t n = 0, i = 0;
        while (i < bytes) {
            i = next(s, i);
            ++n;
        }
        return n;
    }

    float adv_;
    float h_;
};

// Ctx utan renderer: varken `measureWrapped` eller `Label::contentSize`
// rör `c.r` eller `c.box` (mätvägen körs INNE i resolve och får inte läsa
// box — det är kontraktet i widget.h).
glaze::Ctx ctxAt(float scale, gfx::Font& f)
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = scale;
    c.ui          = glaze::UI::Slate;
    c.style       = &glaze::defaultStyle(glaze::UI::Slate);
    c.theme       = &glaze::Theme::slateDefault().light;
    return c;
}

bool nära(float a, float b) { return (a > b ? a - b : b - a) < 0.01f; }

// Label vars mätning går att fråga direkt — `contentSize` är protected,
// vilket är rätt för API:t och opraktiskt för en grind.
//
// MÖNSTRET BÄR BARA FÖR ICKE-`final` KLASSER, och det är skälet att badgen
// nedan mäts på ett annat sätt: `StatusBadge` är `final`, så arv går inte.
struct MättLabel final : glaze::Label {
    using glaze::Label::Label;
    using glaze::Label::contentSize;
};

// MÄTAREN för `final`-klasser: `Widget::measure` är en STATISK skyddad
// basmedlem som tar en godtycklig `const Widget&` och anropar dess
// `contentSize`. En `using`-deklaration i en subklass av BASEN lyfter den
// till publik, och då spelar det ingen roll att målklassen är `final`.
// Samma FORM som `measure_gate` använder (skilda testprogram, egen TU var).
// Meter instansieras aldrig — `draw()` förblir ren virtuell.
struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

// ------------------------------------------------------- 1. radbrytningen

void wrapTester()
{
    std::printf("\n=== radbrytningen (Draw::measureWrapped) ===\n");

    // Två skalor, två fonter — teckensteget följer skalan precis som en
    // riktig atlas gör. Svaret ska vara IDENTISKT i logiska px.
    for (float scale : {1.0f, 2.0f}) {
        FakeFont   f(10.0f * scale, 20.0f * scale);
        glaze::Ctx c   = ctxAt(scale, f);
        const std::string s = "skala " + std::to_string(static_cast<int>(scale));

        // Radhöjden i logiska px är fontens höjd delad med skalan.
        ok(nära(glaze::Draw::lineHeight(c), 20.0f), s + ": radhöjd 20 logiska");

        // ORDGRÄNS: "abc def" vid 40 logiska px = fyra tecken. Fyra tecken
        // är "abc " ⇒ backa till mellanslaget ⇒ "abc" / "def".
        gfx::Sizef m = glaze::Draw::measureWrapped(c, "abc def", 40.0f);
        ok(nära(m.h, 40.0f) && nära(m.w, 30.0f),
           s + ": ordgräns ger två rader à 30 px (" + std::to_string(m.w) +
               " × " + std::to_string(m.h) + ")");

        // Samma text med gott om plats ⇒ EN rad.
        m = glaze::Draw::measureWrapped(c, "abc def", 400.0f);
        ok(nära(m.h, 20.0f) && nära(m.w, 70.0f), s + ": ryms ⇒ en rad");

        // EXPLICIT '\n' bryter ALLTID, även när raden ryms.
        m = glaze::Draw::measureWrapped(c, "ab\ncd", 400.0f);
        ok(nära(m.h, 40.0f) && nära(m.w, 20.0f), s + ": '\\n' bryter alltid");

        // TOM TEXT ⇒ minst en radhöjd (aldrig 0 — en tom ruta är inte ett
        // svar layouten kan räkna med).
        m = glaze::Draw::measureWrapped(c, "", 400.0f);
        ok(nära(m.h, 20.0f) && nära(m.w, 0.0f), s + ": tom text ⇒ en radhöjd");

        // AVSLUTANDE '\n' ⇒ en tom rad till.
        m = glaze::Draw::measureWrapped(c, "abc\n", 400.0f);
        ok(nära(m.h, 40.0f), s + ": avslutande '\\n' ⇒ två rader");

        // LÅNGT UTF-8-ORD utan mellanslag: hårdbryts, minst ett tecken per
        // rad. "ååååå" = 5 kodpunkter à 2 byte; två per rad ⇒ tre rader.
        m = glaze::Draw::measureWrapped(c, "ååååå",
                                        20.0f);
        ok(nära(m.h, 60.0f) && nära(m.w, 20.0f),
           s + ": långt UTF-8-ord hårdbryts i tre rader");
    }

    // UTF-8-GRÄNSEN mätt direkt på råvaran: ingen radgräns får hamna mitt
    // i ett tecken. Faller den här raden är åäö sönderklippta i varje
    // radbruten text i hela ramverket.
    {
        FakeFont f(10.0f, 20.0f);
        const std::string s = "ååååå";   // 10 byte
        const auto        lines = gfx::wrapText(f, s, 20.0f);
        bool              jämna = true;
        for (const gfx::WrappedLine& l : lines)
            if (l.begin % 2 != 0 || l.end % 2 != 0)
                jämna = false;
        ok(lines.size() == 3 && jämna,
           "wrapText bryter på teckengräns, aldrig i byte (" +
               std::to_string(lines.size()) + " rader)");
    }

    // ETT ENDA tecken bredare än ytan: minst ett tecken per rad, ingen
    // oändlig loop (fit == 0-vakten i text_util.cpp).
    {
        FakeFont   f(10.0f, 20.0f);
        glaze::Ctx c = ctxAt(1.0f, f);
        const gfx::Sizef m = glaze::Draw::measureWrapped(c, "abc", 3.0f);
        ok(nära(m.h, 60.0f), "för smal yta ⇒ ett tecken per rad, tre rader");
    }
}

// ------------------------------------------------------------ 2. Label

void labelTester()
{
    std::printf("\n=== Label::wrap (contentSize: auto-höjd ur en satt bredd, "
                "annars en rad) ===\n");

    for (float scale : {1.0f, 2.0f}) {
        FakeFont   f(10.0f * scale, 20.0f * scale);
        glaze::Ctx c = ctxAt(scale, f);
        const std::string s = "skala " + std::to_string(static_cast<int>(scale));

        // SATT BREDD ⇒ auto-höjd ur radbrytningen. contentSize svarar i
        // LOGISKA px — svaret ska vara IDENTISKT i båda skalorna, inte
        // proportionellt mot dem. (`Widget::resolve` multiplicerar på
        // konsumtionssidan.)
        MättLabel l("abc def");
        l.setWrap(true);
        l.width = 40.0f;
        gfx::Sizef m = l.contentSize(c);
        ok(nära(m.w, 40.0f) && nära(m.h, 40.0f),
           s + ": satt bredd ⇒ två rader hög (" + std::to_string(m.h) +
               " logiska)");

        // UTAN bredd ⇒ `contentSize` är EN rad (skrivet kontrakt, inte en
        // bugg): vanlig enradsmätning, alltså textens fulla bredd. En bredd
        // ur layouten (sträckt, dockad) ger höjden genom `heightForWidth` —
        // layout_gate fall 20.
        MättLabel fri("abc def");
        fri.setWrap(true);
        m = fri.contentSize(c);
        ok(nära(m.w, 70.0f) && nära(m.h, 20.0f),
           s + ": utan satt bredd ⇒ enradsmätning");

        // wrap == false med satt bredd ⇒ oförändrat beteende (höjden är
        // EN rad, bredden den satta).
        MättLabel av("abc def");
        av.width = 40.0f;
        m = av.contentSize(c);
        ok(nära(m.w, 40.0f) && nära(m.h, 20.0f),
           s + ": wrap av ⇒ en rad, oförändrat");
    }
}

// ------------------------------------------------ 3. resultatmappningen

const char* namn(glaze::DialogResult r)
{
    switch (r) {
    case glaze::DialogResult::None:   return "None";
    case glaze::DialogResult::OK:     return "OK";
    case glaze::DialogResult::Cancel: return "Cancel";
    case glaze::DialogResult::Yes:    return "Yes";
    case glaze::DialogResult::No:     return "No";
    case glaze::DialogResult::Abort:  return "Abort";
    case glaze::DialogResult::Retry:  return "Retry";
    case glaze::DialogResult::Ignore: return "Ignore";
    }
    return "?";
}

void knappTester()
{
    std::printf("\n=== knappuppsättningarna (alla sex) ===\n");
    using B = glaze::MessageBoxButtons;
    using D = glaze::DialogResult;

    struct Fall {
        const char*            namn;
        B                      set;
        std::vector<D>         resultat;   // i VISUELL ordning
        D                      avbryt;
        bool                   kryss;      // har en avbryt-väg (Escape/X)
    };

    const Fall fall[] = {
        {"OK",               B::OK,               {D::OK},                        D::OK,     true},
        {"OKCancel",         B::OKCancel,         {D::OK, D::Cancel},             D::Cancel, true},
        {"YesNo",            B::YesNo,            {D::Yes, D::No},                D::No,     false},
        {"YesNoCancel",      B::YesNoCancel,      {D::Yes, D::No, D::Cancel},     D::Cancel, true},
        {"RetryCancel",      B::RetryCancel,      {D::Retry, D::Cancel},          D::Cancel, true},
        {"AbortRetryIgnore", B::AbortRetryIgnore, {D::Abort, D::Retry, D::Ignore},D::Abort,  false},
    };

    for (const Fall& f : fall) {
        const auto specs = glaze::msgbox::buttonSpecs(f.set);
        bool       lika  = specs.size() == f.resultat.size();
        for (std::size_t i = 0; lika && i < specs.size(); ++i)
            // TEXTEN ÄR ETT ID: kravet är att
            // det ligger i RAMVERKETS intervall (1–999) och inte är 0,
            // som betyder "ingen översättning". Att id:t faktiskt HAR en
            // text i alla fyra språken mäts av `lang_gate` del 2, med
            // `strings.h` som orakel.
            if (specs[i].result != f.resultat[i] || specs[i].textId == 0 ||
                specs[i].textId >= 1000)
                lika = false;
        ok(lika, std::string(f.namn) + ": " +
                     std::to_string(f.resultat.size()) +
                     " knappar med rätt resultat i visuell ordning");

        const D avbryt = glaze::msgbox::cancelEquivalent(f.set);
        ok(avbryt == f.avbryt, std::string(f.namn) + ": avbryt-ekvivalent = " +
                                   namn(avbryt));
        // Den viktigaste raden i hela grinden: `None` får ALDRIG komma
        // tillbaka ur `messageBox()`, för de 184 anropsställena ska kunna
        // skriva `if (r == Yes)` utan ett fjärde tyst utfall.
        ok(avbryt != D::None, std::string(f.namn) + ": None läcker inte ut");

        ok(glaze::msgbox::hasCancelPath(f.set) == f.kryss,
           std::string(f.namn) + ": Escape/kryss " +
               (f.kryss ? "aktivt" : "SLÄCKT"));
    }

    // Krysset är släckt för exakt de uppsättningar som saknar avbryt, och
    // de har ÄNDÅ ett svar — ägaren kan kaskadstänga rutan.
    ok(!glaze::msgbox::hasCancelPath(B::YesNo) &&
           glaze::msgbox::cancelEquivalent(B::YesNo) == D::No,
       "YesNo: släckt kryss men svar vid kaskadstängning (No)");
    ok(!glaze::msgbox::hasCancelPath(B::AbortRetryIgnore) &&
           glaze::msgbox::cancelEquivalent(B::AbortRetryIgnore) == D::Abort,
       "AbortRetryIgnore: släckt kryss men svar vid kaskadstängning (Abort)");

    // `DialogResult::Ignore` behövs — utan den kan
    // AbortRetryIgnore inte svara alls.
    const auto ari = glaze::msgbox::buttonSpecs(B::AbortRetryIgnore);
    ok(ari.size() == 3 && ari[2].result == D::Ignore,
       "DialogResult::Ignore finns och används");
}

// ------------------------------------------------- 4. StatusBadgens enhet
//
// Badgen är den enda `contentSize()`-implementationen i `message_box.cpp`,
// och den är LEVANDE mätkod: `Widget::resolve` frågar `contentSize(c)`
// OVILLKORLIGT, före all dock- och ankarlogik. Att `layoutAndSize()` sätter
// `badge_->width`/`height` explicit gör den INTE onåbar — den här
// implementationen läser inte fälten alls, den svarar alltid med stilens
// `msgIconSize`. Ett fel i enheten hade alltså slagit igenom vid varje
// skala ≠ 1, hur explicit måttet än sattes av layouten.
//
// Klassen ligger i den interna `status_badge.h`, av
// exakt det skälet: TU-lokal kod går inte att grinda.

void badgeTester()
{
    std::printf("\n=== StatusBadge (mätkontraktets enhet) ===\n");

    const float ikon = glaze::defaultStyle(glaze::UI::Slate).msgIconSize;

    for (float scale : {1.0f, 2.0f}) {
        FakeFont   f(10.0f * scale, 20.0f * scale);
        glaze::Ctx c = ctxAt(scale, f);
        const std::string s = "skala " + std::to_string(static_cast<int>(scale));

        // `final` ⇒ MättLabel-mönstret bär inte. Den statiska
        // `Widget::measure` når protected `contentSize` på en godtycklig
        // `Widget&` och bryr sig inte om `final`.
        glaze::StatusBadge badge(glaze::MessageBoxType::Information);
        const gfx::Sizef   m = Meter::measure(badge, c);
        ok(nära(m.w, ikon) && nära(m.h, ikon),
           s + ": badgen mäter msgIconSize i LOGISKA px (" +
               std::to_string(m.w) + ")");
    }
}

} // namespace

// ===================================================== SPRÅKEN
//
// FINSKAN MÄTS, PÅSTÅS INTE — samma FalskFont som ovan.
//
// VAD DEN HÄR DELEN MÄTER, exakt:
//   (a) en RIKTIG `glaze::Button` som bär `glaze::tr(id)` mäter minst så
//       brett som knappens egen formel kräver för den FÖRVÄNTADE texten
//       (`Draw::measure(förväntad).w + 2 × buttonPadX`), i alla FYRA språken
//       och i TVÅ skalor — och de förväntade texterna står HÄR, så grinden också
//       fäller ett id som pekar på fel ord;
//   (b) den KUMULATIVA högerplaceringen ger noll överlapp när riktiga
//       knappar med de mätta bredderna resolvas i en riktig förälder;
//   (c) knappraden är BREDARE på finska än på engelska — konsekvensen av
//       att bredden är MÄTT och inte hårdkodad.
//
// VAD DEN INTE MÄTER, och varför: `MessageBoxWindow` är TU-lokal i
// `message_box.cpp` och kan inte konstrueras härifrån, så rutans EGEN
// `layoutAndSize()` körs inte. Det som körs är widgetarnas mätväg och
// `Widget::resolve`s högerankare — alltså precis de två mekanismer
// formeln vilar på. Rutans hopsättning verifieras i den körda demon, och
// den SKRIVNA gränsen står här i stället för att grinden påstår mer än
// den gör.
void sprakTester()
{
    std::printf("\n=== språken: knappraden i alla fyra ===\n");

    // FÖRVÄNTADE TEXTER — skrivna här, aldrig hämtat ur language.bin.
    struct Rad {
        const char* tag;
        const char* avbryt;   // AbortRetryIgnore: Abort
        const char* forsok;   // Retry
        const char* ignorera; // Ignore
    };
    // Abort är INTE Cancel: engelskan och finskan skiljer dem, som
    // Windows egen ruta — "Abort"/"Keskeytä", inte "Cancel"/"Peruuta".
    const Rad rader[] = {
        {"sv", "Avbryt",   "Försök igen",     "Ignorera"},
        {"en", "Abort",    "Retry",           "Ignore"},
        {"nb", "Avbryt",   "Prøv igjen",      "Ignorer"},
        {"fi", "Keskeytä", "Yritä uudelleen", "Ohita"},
    };

    float bredEn = 0.0f, bredFi = 0.0f;
    for (const Rad& r : rader) {
        ok(glaze::setLanguage(r.tag),
           std::string("setLanguage(\"") + r.tag + "\")");
        const char* facit[3] = {r.avbryt, r.forsok, r.ignorera};

        for (float skala : {1.0f, 2.0f}) {
            FakeFont   f(10.0f * skala, 12.0f * skala);
            glaze::Ctx c = ctxAt(skala, f);
            const glaze::Style& m = *c.style;

            // De TRE knapparna, byggda ur SAMMA tabell rutan använder.
            const auto specs = glaze::msgbox::buttonSpecs(
                glaze::MessageBoxButtons::AbortRetryIgnore);
            ok(specs.size() == 3, "AbortRetryIgnore har tre knappar");

            // `Widget` är varken kopierbar eller flyttbar (barnlistan äger
            // pekare) — därför unique_ptr och inte en vektor av värden.
            std::vector<std::unique_ptr<glaze::Button>> knappar;
            for (const auto& s : specs)
                knappar.push_back(
                    std::make_unique<glaze::Button>(glaze::tr(s.textId)));

            std::vector<float> bredder;
            float              rowW = 0.0f;
            bool               allaBreda = true, allaRatt = true;
            for (std::size_t i = 0; i < knappar.size(); ++i) {
                allaRatt = allaRatt && knappar[i]->text() == facit[i];
                const float krav =
                    glaze::Draw::measure(c, facit[i]).w + 2.0f * m.buttonPadX;
                const float mätt = Meter::measure(*knappar[i], c).w;
                allaBreda        = allaBreda && mätt >= krav - 0.01f;
                const float w    = std::max(std::max(mätt, krav), m.msgBtnMinW);
                bredder.push_back(w);
                rowW += w;
            }
            rowW += 2.0f * m.msgBtnGap;

            ok(allaRatt, std::string(r.tag) + " @" +
                             std::to_string(int(skala)) +
                             "x: id:na ger EXAKT facittexterna");
            ok(allaBreda, std::string(r.tag) + " @" +
                              std::to_string(int(skala)) +
                              "x: (a) varje knapp rymmer sin text + 2 × "
                              "buttonPadX");

            // (b) KUMULATIVA `right`-offsets ⇒ noll överlapp. Mätt på
            //     RIKTIGA `resolve()`-rects i en riktig förälder — samma
            //     högerankare rutan använder. Samma `right` på alla hade
            //     lagt knapparna ovanpå varandra, och det är exakt det
            //     felet raden finns för.
            glaze::Panel far;
            far.width  = 600.0f;
            far.height = 100.0f;
            glaze::Ctx fc = c;
            fc.area = gfx::Rectf{0.0f, 0.0f, 600.0f * skala, 100.0f * skala};
            float höger = m.msgPad;
            for (std::size_t i = knappar.size(); i-- > 0;) {
                knappar[i]->anchors = {false, false, true, true};
                knappar[i]->width   = bredder[i];
                knappar[i]->height  = 24.0f;
                knappar[i]->right   = höger;
                knappar[i]->bottom  = m.msgPad;
                höger += bredder[i] + m.msgBtnGap;
            }
            bool överlapp = false;
            for (std::size_t i = 1; i < knappar.size(); ++i) {
                const gfx::Rectf a = knappar[i - 1]->resolve(fc);
                const gfx::Rectf b = knappar[i]->resolve(fc);
                överlapp = överlapp || (a.x + a.w > b.x + 0.01f);
            }
            ok(!överlapp, std::string(r.tag) + " @" +
                              std::to_string(int(skala)) +
                              "x: (b) kumulativa right-offsets ger NOLL "
                              "överlapp");

            if (skala == 1.0f && std::string(r.tag) == "en")
                bredEn = rowW;
            if (skala == 1.0f && std::string(r.tag) == "fi")
                bredFi = rowW;
        }
    }
    ok(bredEn > 0.0f && bredFi > bredEn,
       "(c) knappraden är BREDARE på finska än på engelska (" +
           std::to_string(bredEn) + " → " + std::to_string(bredFi) + ")");
    glaze::setLanguage("sv");
}

int main()
{
    wrapTester();
    labelTester();
    badgeTester();
    knappTester();
    sprakTester();

    std::printf("\nmsgbox_gate: %s (%d fel)\n", fel == 0 ? "allt grönt" : "FÄLLD",
                fel);
    return fel == 0 ? 0 : 1;
}
