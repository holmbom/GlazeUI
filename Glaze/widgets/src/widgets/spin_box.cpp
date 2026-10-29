// SpinBox — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3): fältet är ett
// riktigt barn som ritar sig själv, kompositen ritar STEGAREN — en platta
// med två halvor och var sin vinkelhake. Måtten kommer ur stilen
// (`spin*`, dropdownens `chevron*`), färgerna ur temat — inga tal och inga
// färger i den här filen. BETEENDET bor här också: värdets klampning och
// avrundning, de två användarvägarna och tolkningen av text till tal.

#include "glaze/widgets/spin_box.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

#include <gfx/platform.h>

#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/textinput.h"

namespace glaze {

namespace {

// Tecken som kan ingå i ett skrivet tal. Allt annat sväljs redan vid
// tangenttrycket — en bokstav i ett talfält är aldrig vad användaren menar.
bool taltecken(char ch)
{
    return (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == ',' ||
           ch == '.';
}

std::string trimma(const std::string& s)
{
    const std::size_t a = s.find_first_not_of(" \t");
    const std::size_t b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// ANVÄNDARENS TEXT → tal. Generös där det är ofarligt och strikt där det
// inte är det: valfritt tecken, siffror, och HÖGST ETT decimaltecken — "," ELLER
// "." oavsett språk, för numerisk tangentbords decimaltangent ger det
// layouten säger och användaren ska inte behöva tänka på det. Två
// avskiljare ("1,000.5") är TVETYDIGT — tusentalsgrupp eller fel? — och
// tolkas inte alls; inte heller exponenter, som ingen skriver i ett talfält.
// Tolkningen sker i "C"-lokalen (`std::locale::classic`), så en app som
// anropat `setlocale` kan inte ändra vad ett tal betyder.
bool tolkaText(const std::string& in, double& ut)
{
    std::string s = trimma(in);
    if (s.empty())
        return false;
    std::size_t i = (s[0] == '+' || s[0] == '-') ? 1u : 0u;
    int siffror = 0, avskiljare = 0;
    for (std::size_t k = i; k < s.size(); ++k) {
        if (s[k] >= '0' && s[k] <= '9') {
            ++siffror;
        } else if (s[k] == ',' || s[k] == '.') {
            ++avskiljare;
            s[k] = '.';
        } else {
            return false;
        }
    }
    if (siffror == 0 || avskiljare > 1)
        return false;
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0.0;
    is >> v;
    if (is.fail() || !std::isfinite(v))
        return false;
    ut = v;
    return true;
}

// EN C++-LITERAL → tal (`setProp`, alltså `.glazeui`-vägen). En ANNAN
// tolkning än användarens text, med flit: raden i filen är C++ som
// kompileras, så det som C++ godtar som flyttal ska previewn också godta
// (`1e3`, `.5`, `2.5f`) — en striktare parser hade gjort lagliga rader
// opaka, ett regel 8-brott. Komma är aldrig decimaltecken i C++.
bool tolkaLiteral(const std::string& in, double& ut)
{
    std::string s = trimma(in);
    if (!s.empty() && (s.back() == 'f' || s.back() == 'F' ||
                       s.back() == 'l' || s.back() == 'L'))
        s.pop_back();
    if (s.empty() || s.find_first_of("xX',") != std::string::npos)
        return false;
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0.0;
    is >> v;
    if (is.fail() || !std::isfinite(v))
        return false;
    is >> std::ws;
    if (!is.eof())
        return false;   // skräp efter talet
    ut = v;
    return true;
}

} // namespace

// ------------------------------------------------------------- fältet
//
// En riktig `TextInput` med tre tillägg och inget annat — allt som är
// fältets (caret, markering, klippbord, UTF-8, scroll) ärvs orört:
//   * PILTANGENTERNA upp/ner stegar. Utan subklassen hade de aldrig nått
//     kompositen: tangenter går till den FOKUSERADE widgeten, och fältet
//     svarar false på dem.
//   * Bara TALTECKEN tas emot vid skrivning (en inklistring prövas i stället
//     av tolkningen — den lämnar värdet orört om den inte går att läsa).
//   * Enter och FOKUSFÖRLUST COMMITTAR texten: den tolkas, blir
//     värdet och skrivs om. Ur `focusLost` POSTAS eventen — kroken rör bara
//     värdet och fältets egen text, aldrig en användar-handler.
class SpinBox::Field final : public TextInput {
public:
    explicit Field(SpinBox& owner) : owner_(owner) {}

protected:
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        switch (k) {
        case gfx::Key::Up:
            owner_.stepFromUser(+1);
            return true;
        case gfx::Key::Down:
            owner_.stepFromUser(-1);
            return true;
        case gfx::Key::Return:
            // COMMITTAR OCH BUBBLAR — `TextInput`s
            // kontrakt, "Enter konsumeras inte": i en dialog committar Enter
            // talet OCH trycker standardknappen, med det committade värdet.
            // Svaret (livet) behövs inte — inget här rör `this` efteråt.
            owner_.textFromUser();
            return false;
        default:
            return TextInput::keyDown(k, mods);
        }
    }

    bool textInput(const char* utf8) override
    {
        std::string ok;
        for (const char* p = utf8; *p; ++p)
            if (taltecken(*p))
                ok.push_back(*p);
        if (ok.empty())
            return true;   // sväljs — tecknet hör inte hemma i ett tal
        return TextInput::textInput(ok.c_str());
    }

    void focusLost() override
    {
        TextInput::focusLost();
        owner_.commitText(true);
    }

private:
    SpinBox& owner_;
};

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("spinbox", SpinBox)

// Fältets text prövas INTE medan man skriver — den committas vid
// Enter och fokusförlust (`Field`), så fältets `onChange` lämnas osatt.
SpinBox::SpinBox() : sep_(tr(str::DecimalSeparator))
{
    field_       = add<Field>(*this);
    field_->dock = Dock::Fill;   // fyller kompositens vänstra del (childArea)
    syncText();
}

// ------------------------------------------------------------ värdet

// KLAMPA och AVRUNDA — vid LÄSNING, så settrarnas ordning aldrig spelar
// roll. Avrundningen sker efter klampningen och klampas igen: en gräns som
// inte är en multipel av 10^−decimals (min 0.05 med en decimal) får då
// aldrig ett värde utanför intervallet, även om visningen rundar den.
// Aldrig −0: "−0,0" är ingen siffra någon vill se.
double SpinBox::effective(double v) const
{
    const double lo = min_;
    const double hi = std::max(min_, max_);
    if (!std::isfinite(v))
        v = lo;
    v                 = std::clamp(v, lo, hi);
    const double skal = std::pow(10.0, decimals_);
    v                 = std::clamp(std::round(v * skal) / skal, lo, hi);
    return v == 0.0 ? 0.0 : v;
}

double SpinBox::effectiveStep() const
{
    return step_ > 0.0 ? step_ : std::pow(10.0, -decimals_);
}

double SpinBox::value() const
{
    return effective(value_);
}

void SpinBox::setValue(double v)
{
    if (v == value_)
        return;
    value_ = v;
    syncText();
    invalidate();
}

void SpinBox::setMin(double v)
{
    if (v == min_)
        return;
    min_ = v;
    syncText();
    invalidate();
}

void SpinBox::setMax(double v)
{
    if (v == max_)
        return;
    max_ = v;
    syncText();
    invalidate();
}

void SpinBox::setStep(double v)
{
    if (v == step_)
        return;
    step_ = v;
    invalidate();
}

void SpinBox::setDecimals(int d)
{
    d = std::clamp(d, 0, 9);
    if (d == decimals_)
        return;
    decimals_ = d;
    syncText();
    invalidate();
}

const std::string& SpinBox::text() const
{
    return field_->text();
}

void SpinBox::selectAll()
{
    field_->selectAll();
}

void SpinBox::setSelection(int begin, int end)
{
    field_->setSelection(begin, end);
}

// Formaterat i "C"-lokalen och med SPRÅKETS decimaltecken — samma tal på
// varje plattform, oavsett vad appen gjort med `setlocale`.
std::string SpinBox::format(double v) const
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::fixed << std::setprecision(decimals_) << v;
    std::string       s     = os.str();
    const std::size_t punkt = s.find('.');
    if (punkt != std::string::npos)
        s.replace(punkt, 1, sep_.str());
    return s;
}

void SpinBox::syncText()
{
    field_->setText(format(value()));
}

// ------------------------------------------------------- användarvägarna

// ETT STEG utgår från det SKRIVNA talet när det går att tolka — en
// halvskriven text som "12" är det användaren menar (NumericUpDown validerar
// fältet före steget) — och annars från värdet. Stegaren skriver ALLTID om
// texten, också en som inte gick att tolka ("-"): efter ett steg är det
// värdet som gäller och inte det som stod.
bool SpinBox::stepFromUser(int n)
{
    if (n == 0)
        return true;
    const double före    = value();
    double       bas     = före;
    double       skrivet = 0.0;
    if (tolkaText(field_->text(), skrivet))
        bas = effective(skrivet);
    value_ = effective(bas + static_cast<double>(n) * effectiveStep());
    syncText();
    invalidate();
    if (value() == före)
        return true;
    return fireChange();   // nya värdet läses ur sender (value())
}

bool SpinBox::textFromUser()
{
    return commitText(false);
}

// COMMIT AV TEXTEN — Enter (`postad` falsk) och fokusförlusten
// (sann). Texten tolkas och klampas; den skrivs ALLTID om, så ett tal i en
// annan form ("3,50") får värdets form och en text som inte går att tolka
// ("-") försvinner. Värdet och texten är skrivna innan eventen fyras — eller,
// ur ett fokusbyte, POSTAS.
bool SpinBox::commitText(bool postad)
{
    const double före = value();
    double       v    = 0.0;
    if (tolkaText(field_->text(), v))
        value_ = effective(v);
    syncText();   // fältet invaliderar sig självt om texten skrivs om
    if (value() == före)
        return true;   // inget committades — inget att rita om
    invalidate();
    if (!postad)
        return fireChange();
    postEvent([this] { fireChange(); });
    return true;
}

// `onChange` först och `onCommit` sedan — för ett talfält är de samma
// handling (headern) — med vakten emellan (`Widget::firePair`).
bool SpinBox::fireChange()
{
    return firePair(onChange, onCommit);
}

// `commitEdit` (widget.h) är Enter utan tangenten.
void SpinBox::commitEdit()
{
    commitText(false);
}

// ----------------------------------------------------- kompositkrokarna

Widget* SpinBox::focusProxy() const
{
    return field_;
}

bool SpinBox::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(field_);
}

// `enabled` kaskadar inte i ramverket — men fältet är INREDNING, och ett
// inaktivt talfält vars fält gick att skriva i hade ljugit.
void SpinBox::enabledChanged()
{
    if (isEnabled())
        field_->enable();
    else
        field_->disable();
}

// SPRÅKBYTET: decimaltecknet följer språket, och fältets text är LAGRAD —
// så ett tal som redan visas skrivs om här, i händelseflödet, och aldrig i
// `draw()`. Bara ett fält som INTE redigeras (då är texten användarens),
// och `setText` gör ingenting när texten redan stämmer — kroken anropas
// också när fönstret visas och när talfältet adopteras, utan att språket
// nödvändigtvis bytts.
void SpinBox::languageChanged()
{
    if (!field_->focused())
        syncText();
}

bool SpinBox::setProp(const std::string& key, const std::string& value)
{
    if (key == "value" || key == "min" || key == "max" || key == "step") {
        double v = 0.0;
        if (!tolkaLiteral(value, v)) {
            gfx::log("SpinBox(%s): '%s' är inget tal för %s (ignoreras)",
                     name.c_str(), value.c_str(), key.c_str());
            return true;   // nyckeln är känd — värdet är fel
        }
        if (key == "value")      setValue(v);
        else if (key == "min")   setMin(v);
        else if (key == "max")   setMax(v);
        else                     setStep(v);
        return true;
    }
    if (key == "decimals") {
        double v = 0.0;
        if (!tolkaLiteral(value, v) || v != std::floor(v)) {
            gfx::log("SpinBox(%s): '%s' är inget heltal för decimals "
                     "(ignoreras)", name.c_str(), value.c_str());
            return true;
        }
        setDecimals(static_cast<int>(v));
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// ------------------------------------------------------------ geometrin

// Stegaren längst till höger, hela höjden; fältet får resten minus luften.
gfx::Rectf SpinBox::stepperRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float w = std::min(c.style->spinStepW, b.w);
    return gfx::Rectf{b.x + b.w - w, b.y, w, b.h};
}

gfx::Rectf SpinBox::partRect(const Ctx& c, const gfx::Rectf& b, Part p) const
{
    const gfx::Rectf st   = stepperRect(c, b);
    const float      halv = st.h / 2.0f;
    return p == Part::Up ? gfx::Rectf{st.x, st.y, st.w, halv}
                         : gfx::Rectf{st.x, st.y + halv, st.w, st.h - halv};
}

SpinBox::Part SpinBox::partAt(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const float      lx = Draw::localX(c, r, mx);
    const float      ly = Draw::localY(c, r, my);
    if (partRect(c, b, Part::Up).contains(lx, ly))
        return Part::Up;
    if (partRect(c, b, Part::Down).contains(lx, ly))
        return Part::Down;
    return Part::None;
}

gfx::Rectf SpinBox::childArea(const Ctx& c) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const gfx::Rectf st = stepperRect(c, b);
    return Draw::project(
        c, r, {b.x, b.y, std::max(0.0f, st.x - c.style->spinGap - b.x), b.h});
}

gfx::Sizef SpinBox::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner. AUTO-bredden är stilens
        // `spinDefaultW` (ett tal är smalare än en textrad), AUTO-höjden är
        // FÄLTETS — talfältet ska stå i rad med ett vanligt textfält i
        // varje stil, och fältet vet sin höjd.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f) s.w = c.style->spinDefaultW;
        if (s.h <= 0.0f) s.h = measure(*field_, c).h;
        return s;
    }
    }
}

// ------------------------------------------------------------ beteendet

void SpinBox::mouseDown(const Ctx& c, float mx, float my)
{
    press_ = partAt(c, mx, my);
    if (press_ != Part::None && !stepFromUser(press_ == Part::Up ? +1 : -1))
        return;   // stegets handler rev eller flyttade talfältet
    invalidate();
}

void SpinBox::mouseMove(const Ctx& c, float mx, float my)
{
    const Part p = partAt(c, mx, my);
    if (p != hover_) {   // hård regel 1: bara vid ändring
        hover_ = p;
        invalidate();
    }
}

// HJULET KRÄVER FOKUS: utan fokus bubblar det till närmaste
// rullande förälder — kontraktet står i spin_box.h. Fokus står i FÄLTET
// (fokusombudet), aldrig i kompositen, så det är fältets som frågas.
//
// Hjul upp = uppåt i värde (Sliderns riktning). Hela steg — bråkdelen från
// ett pekplattehjul sparas tills den blivit ett, så ett mjukt hjul varken
// rusar (ett steg per litet event) eller står still (avrundat till noll).
bool SpinBox::mouseWheel(const Ctx& c, float, float, float wheelX, float wheelY)
{
    if (!field_->focused())
        return false;
    const float w = wheelY != 0.0f ? wheelY : wheelX;
    if (w == 0.0f)
        return false;
    wheelRest_ += wheelNotches(c, w);
    const int n = static_cast<int>(std::trunc(wheelRest_));
    if (n != 0) {
        wheelRest_ -= static_cast<float>(n);
        stepFromUser(n);
    }
    return true;
}

// ------------------------------------------------------------ ritningen

void SpinBox::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Rectf st  = stepperRect(c, b);
    const gfx::Rectf upp = partRect(c, b, Part::Up);
    const gfx::Rectf ner = partRect(c, b, Part::Down);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const bool       på  = isEnabled();
    const float      rad = m.cornerRadius;

    // En HALVA tänd under pekaren eller trycket — fyllningen ritas som hela
    // plattan och KLIPPS till halvan, så plattans rundade hörn följer med
    // utan en radie per hörn (kapselns teknik).
    auto halva = [&](const gfx::Rectf& h, Part p, gfx::Color tänd,
                     gfx::Color tryckt) {
        const bool trycks = på && pressed() && press_ == p;
        const bool över   = på && hovered() && hover_ == p;
        if (!trycks && !över)
            return;
        Draw::clip(c, h);
        Draw::rect(c, st, rad, trycks ? tryckt : tänd);
        Draw::endClip(c);
    };
    // Vinkelhakarna — dropdownens tecken och storlek, mitt i varje halva.
    auto pilar = [&](gfx::Color färg) {
        const float w = 2.0f * m.chevronW;
        const float h = 2.0f * m.chevronH;
        auto mitt     = [&](const gfx::Rectf& r) {
            return gfx::Rectf{r.x + (r.w - w) / 2.0f, r.y + (r.h - h) / 2.0f,
                              w, h};
        };
        Draw::chevron(c, mitt(upp), Dir::Up, Pen{färg, 0.0f});
        Draw::chevron(c, mitt(ner), Dir::Down, Pen{färg, 0.0f});
    };
    auto skiljelinje = [&](gfx::Color färg) {
        Draw::line(c, st.x, ner.y, st.x + st.w, ner.y,
                   Pen{färg, m.borderWidth});
    };
    const bool tänd = på && hovered() && hover_ != Part::None;
    const bool tryck = på && pressed() && press_ != Part::None;

    switch (c.ui) {
    // GLAZE — stegaren är en Glaze-PLATTA: tonad kropp, laminering och
    // bestämd kontur, som knappen. Trycket tar bort
    // lamineringen — plattan ligger platt mot ytan.
    //
    // De fyra tillstånden (princip 7), per HALVA:
    //   vila       `button` + laminering + kontur `buttonBorder`
    //   hover      halvan `buttonHover`
    //   tryck      halvan `buttonPress`, ingen laminering
    //   inaktiv    `buttonDisabled`, kontur `border`, hakar `textDim`
    //   (fokus är FÄLTETS — dess ring, dess caret)
    case UI::Glaze:
        Draw::rect(c, st, rad, på ? col.button : col.buttonDisabled);
        halva(upp, Part::Up, col.buttonHover, col.buttonPress);
        halva(ner, Part::Down, col.buttonHover, col.buttonPress);
        if (på && !tryck)
            Draw::laminate(c, st, rad, col.sheen, m.sheenLower, col.sheenHigh,
                           m.sheenUpper);
        skiljelinje(på ? col.buttonBorder : col.border);
        Draw::frame(c, st, rad,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        pilar(på ? col.text : col.textDim);
        break;

    // MINIMALISTIC — "Outline": KANTEN är formen, och fyllningen säger
    // tillståndet. Plattan fylls i vila som
    // familjens knappar; trycket bär kanten i `accent`.
    //
    // De fyra tillstånden (princip 7), per HALVA:
    //   vila       `button` + kant `buttonBorder`
    //   hover      halvan `buttonHover`
    //   tryck      halvan `buttonPress`, kanten `accent`
    //   inaktiv    `buttonDisabled`, kant `border`, hakar `textDim`
    case UI::Minimalistic:
        Draw::rect(c, st, rad, på ? col.button : col.buttonDisabled);
        halva(upp, Part::Up, col.buttonHover, col.buttonPress);
        halva(ner, Part::Down, col.buttonHover, col.buttonPress);
        skiljelinje(på ? col.buttonBorder : col.border);
        Draw::frame(c, st, rad,
                    Pen{!på    ? col.border
                        : tryck ? col.accent
                                : col.buttonBorder,
                        m.borderWidth});
        pilar(på ? col.text : col.textDim);
        break;

    // SLATE — en PLATTA i knappens färger, utan
    // lyft: stegaren sitter tätt intill fältet, och en skugga hade fallit in
    // i det. Kanten tänds i `accent` under pekaren, som knappens.
    //
    // De fyra tillstånden (princip 7), per HALVA:
    //   vila       `button` + hårlinje `border`
    //   hover      halvan `buttonHover`, kanten `accent`
    //   tryck      halvan `buttonPress`
    //   inaktiv    `buttonDisabled`, hakar `textDim`
    case UI::Slate:
    default:
        Draw::rect(c, st, rad, på ? col.button : col.buttonDisabled);
        halva(upp, Part::Up, col.buttonHover, col.buttonPress);
        halva(ner, Part::Down, col.buttonHover, col.buttonPress);
        skiljelinje(col.border);
        Draw::frame(c, st, rad,
                    Pen{tänd ? col.accent : col.border, m.borderWidth});
        pilar(på ? col.text : col.textDim);
        break;
    }
}

} // namespace glaze
