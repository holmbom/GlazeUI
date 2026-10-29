// apply_props — se .h. Lyft ur BuilderWindow::applyProp och glaze-renders
// handkopierade spegel; de två är nu en.

#include "apply_props.h"
#include "apply_props_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bld {
namespace {

// TEXTFÄLTEN — regel 6: texten heter `text` på varje widget, `title` är
// regel 4-ARVET av den, `toolTip` basens, `placeholder` fältens
// hint och `dialogTitle` filväljarfältets dialogtitel
//. De tar EXAKT två värdeformer: en
// strängliteral och `glaze::tr(N)`.
bool ärTextfält(const std::string& field)
{
    return field == "text" || field == "title" || field == "toolTip" ||
           field == "placeholder" || field == "dialogTitle";
}

// Ett textfälts värde på previewns widget eller komponent (`Mål` har `name`,
// `setProp` och `setTextToken` — `glaze::Widget` och `glaze::Component`).
// true = utfört.
//
// TOKENET SÄTTS för `glaze::tr(N)`, inte den upplösta strängen:
// `setText(glaze::tr(id))` är exakt vad den kompilerade appen gör, och målet
// löser sedan upp texten själv vid varje ritning. Sätter man en färdig sträng
// ser det likadant ut i FÖRSTA framen men fryser texten — previewn hade
// slutat följa `lang=`-taggen och GlazeBuilders språkbyte, alltså gjort
// MINDRE än C++-koden. Regel 8 pekar åt samma håll i båda riktningarna.
//
// TYPEN AVGÖR via `setTextToken` — ALDRIG via `setProp`: en `setProp`-
// fallback sätter den UPPLÖSTA strängen på vilken widget som helst som
// känner fältnamnet, så previewn skulle visa
// `input.setText(glaze::tr(1000));` som fungerande — medan `TextInput::setText`
// tar `std::string` och den kompilerade appen alltså INTE bygger. Kan typen
// inte ta tokenet är raden OPAK (regel 4): den går inte att skriva i C++
// heller.
//
// ALLT ANNAT ÄR OPAKT: `glaze::tr(app::str::Hej)` kompilerar, men
// verktygen kan inte lösa konstanten (filen bär NUMRET), och ett
// annat uttryck (`kNamn`, `"a" "b"`) kan previewn inte räkna ut. Ett
// värde som föll igenom till `setProp` som RÅ text skulle visa uttrycket —
// ett värde appen aldrig visar. Därför loggas raden och målet lämnas
// ORÖRT.
template <class Mål>
bool applyTextField(Mål& m, const std::string& field, const std::string& value,
                    const char* origin)
{
    std::string         lit;
    std::uint32_t       id = 0;
    const TextValueKind k  = classifyTextValue(value, lit, id);
    switch (k) {
    case TextValueKind::TrId:
        if (m.setTextToken(field, glaze::tr(id)))
            return true;
        gfx::log("%s(%s): '%s' tar inget översättnings-id — raden bevaras "
                 "OPAKT (typen har ingen TrText-sättare)",
                 origin, m.name.c_str(), field.c_str());
        return false;   // OPAK: målet lämnas ORÖRT, raden round-trippas
    case TextValueKind::Literal:
        if (m.setProp(field, lit))
            return true;
        gfx::log("%s(%s): opak fältrad '%s' (ritas inte)", origin,
                 m.name.c_str(), field.c_str());
        return false;
    case TextValueKind::TrExpr:
        gfx::log("%s(%s): '%s' översätts med en konstant — verktygen läser "
                 "bara NUMRET (glaze::tr(1042)); raden är opak och ritas inte",
                 origin, m.name.c_str(), field.c_str());
        return false;
    case TextValueKind::Other:
        break;
    }
    gfx::log("%s(%s): '%s' är varken en strängliteral eller glaze::tr(<tal>) "
             "— ett uttryck previewn inte räknar ut; raden är opak och ritas "
             "inte",
             origin, m.name.c_str(), field.c_str());
    return false;
}

// `gfx::Color{r, g, b, a}` (0–4 kanaler) — definierad längre ner, bland
// Call-grenarnas literalparsrar; färgväljarens fältrad behöver den
// redan här.
bool parseColorLiteral(const std::string& v, gfx::Color& ut);

// `glaze::img("lista", "nyckel")` eller `{}` → bildvärdet (`ImageSource`) —
// definierad bland Call-grenarnas parsrar;
// programikonens fältrader (`icon`/`smallIcon`) behöver den redan här.
bool argImg(const std::string& v, glaze::ImageSource& ut);

// `{}` med valfria blanksteg — programmenyns rotstig.
bool tommaKlamrar(const std::string& v);

// `glaze::px(…)`/`glaze::mm(…)`/`glaze::world(…)` eller ett naket tal → ett
// mått — definierad bland ritytans literalparsrar; textstorleken
// behöver den redan här.
bool parseMått(const std::string& v, glaze::Length& ut);

// TEXTSTORLEKEN — de fyra typer som har en, och sättaren på dem:
// EN plats för fältraden och setter-fallbacken (en egen subklass).
bool harTextstorlek(const glaze::Widget& w)
{
#ifdef GLAZE_PREVIEW_TERMINAL
    if (detail::isTerminal(w))   // sidopaketets terminal
        return true;
#endif
    return dynamic_cast<const glaze::TextInput*>(&w) ||
           dynamic_cast<const glaze::TextArea*>(&w) ||
           dynamic_cast<const glaze::ListBox*>(&w) ||
           dynamic_cast<const glaze::Dropdown*>(&w);
}

void sättTextstorlek(glaze::Widget& w, const glaze::Length& l)
{
#ifdef GLAZE_PREVIEW_TERMINAL
    if (detail::isTerminal(w)) {
        detail::setTerminalTextSize(w, l);
        return;
    }
#endif
    if (auto* f = dynamic_cast<glaze::TextInput*>(&w))
        f->setTextSize(l);
    else if (auto* a = dynamic_cast<glaze::TextArea*>(&w))
        a->setTextSize(l);
    else if (auto* b = dynamic_cast<glaze::ListBox*>(&w))
        b->setTextSize(l);
    else if (auto* d = dynamic_cast<glaze::Dropdown*>(&w))
        d->setTextSize(l);
}

// VÄRDET SOM NYCKELTEXT för en `setProp` — fältradernas och setter-
// fallbackarnas EN normalisering, för widgetar och komponenter: en
// strängliteral avciteras, ett enum blir sista segmentet gement, och tal och
// bool står som de är.
std::string nyckeltext(const std::string& värde)
{
    std::string v;
    if (!parseCppString(värde, v) && !parseEnumTail(värde, v))
        v = värde;
    return v;
}

} // namespace

void applyProp(glaze::Widget& w, const Prop& p, const char* origin)
{
    // Bas-placeringen är publika fält — sätts direkt ur den råa
    // C++-uttryckstexten.
    float f = 0.0f;
    if (p.field == "x" && parseNum(p.value, f)) { w.x = f; return; }
    if (p.field == "y" && parseNum(p.value, f)) { w.y = f; return; }
    if (p.field == "right" && parseNum(p.value, f)) { w.right = f; return; }
    if (p.field == "bottom" && parseNum(p.value, f)) { w.bottom = f; return; }
    if (p.field == "width" && parseNum(p.value, f)) { w.width = f; return; }
    if (p.field == "height" && parseNum(p.value, f)) { w.height = f; return; }
    if (p.field == "anchors") {
        bool a[4];
        if (parseAnchorsText(p.value, a)) {
            w.anchors = {a[0], a[1], a[2], a[3]};
            return;
        }
    }

    // SYSKONANKARET — speglat på anchors-grenen ovan, och
    // det är den här grenen som gör previewn SANN: geometrin är sedan
    // gratis, eftersom previewträdet är ett riktigt adopterat träd med
    // `name` satta och `drawPreviewTree` går via `resolve()`.
    //
    // Vägen är TYPAD och slutar HÄR: en ogiltig literal faller INTE vidare
    // till `setProp` (vars textform är en annan — "namn,gap"), utan blir en
    // OPAK rad. Att tyst sätta ett halvt ankare vore värre än att inte
    // rita något, och regel 4:s dataskyddsintention pekar åt samma håll.
    if (isSiblingAnchorField(p.field)) {
        std::string till;
        float       gap = 0.0f;
        if (!parseSiblingAnchorText(p.value, till, gap)) {
            gfx::log("%s(%s): opak %s-rad (ingen {\"namn\", tal}-literal, "
                     "ritas inte)", origin, w.name.c_str(), p.field.c_str());
            return;
        }
        const glaze::SiblingAnchor sa{till, gap};
        if (p.field == "anchorLeft")        w.anchorLeft   = sa;
        else if (p.field == "anchorTop")    w.anchorTop    = sa;
        else if (p.field == "anchorRight")  w.anchorRight  = sa;
        else                                w.anchorBottom = sa;
        return;
    }

    // IDENTITETEN: `Widget::name` är publik och ÄR det dokumenterade sättet
    // att skilja avsändare åt (regel 6) — utan grenen blir `btn.name = "div";`
    // en opak rad och verktygen tappar identiteten medan bygget förblir grönt.
    // Anropsställena sätter `w.name = n.id` (medlemsnamnet) FÖRE
    // prop-loopen, och raden är KANONISK: den står på varje
    // nod och bär exakt nodnamnet, så de två kan aldrig säga olika saker.
    {
        std::string v;
        if (p.field == "name" && parseCppString(p.value, v)) {
            w.name = v;
            return;
        }
    }

    // Bildfältet: funktionsanrops-literalen
    // resolvas till handtags-formen "lista/nyckel" som setProp tar —
    // previewn ritar den RIKTIGA ikonen. Oparsbar form ⇒ faller igenom
    // till opak-vägen nedan.
    if (p.field == "img") {
        std::string lista, nyckel;
        if (parseImgCall(p.value, lista, nyckel)) {
            w.setProp("img", lista + "/" + nyckel);
            return;
        }
    }

    // PROGRAMIKONENS BILDER — bildfältets form, men
    // TYPAD och med en form till: `{}` är laglig C++ för ett `ImageSource`
    // och betyder INGEN egen bild, alltså förvalet (GlazeUI-ikonen). Den
    // går inte att uttrycka i `setProp`s "lista/nyckel"-text, så bildvärdet
    // byggs här och går direkt till sättaren. Oparsbar form ⇒ OPAK rad och
    // ikonen ORÖRD — aldrig en gissad bild. En EGEN widget med ett
    // `icon`-fält faller igenom till setProp-vägen nedan.
    if (p.field == "icon" || p.field == "smallIcon") {
        if (auto* ai = dynamic_cast<glaze::AppIcon*>(&w)) {
            glaze::ImageSource bild{};
            if (!argImg(p.value, bild)) {
                gfx::log("%s(%s): opak %s-rad (varken glaze::img(…) eller "
                         "{}, ritas inte)", origin, w.name.c_str(),
                         p.field.c_str());
                return;
            }
            if (p.field == "icon")
                ai->setIcon(bild);
            else
                ai->setSmallIcon(bild);
            return;
        }
    }

    // ITEMS är en LISTA och inte en sträng. Modellen lagrar
    // raden RÅ — `dd.setItems({"a", "b"})` normaliseras av readern till
    // `Prop{"items", "{\"a\", \"b\"}"}` — och den råa texten skulle annars falla
    // igenom till `setProp("items", …)`, som splittar på ';'. Previewn skulle få
    // alltså EN skräprad. På en STÄNGD dropdown syntes det inte (fältet
    // visar `selectedText()`), men i en ListBox hade det synts direkt.
    //
    // Vägen är TYPAD och FÖRLUSTFRI: `parseStringList` avkodar
    // C++-literalerna (items med semikolon, tomma element, escapes) och
    // resultatet går till `setItems(vec)` — aldrig genom textformen.
    // PARSEFEL ⇒ raden förblir OPAK och ignoreras; att tyst visa fel data
    // är värre än att inte visa något, och regel 4:s dataskyddsintention
    // pekar åt samma håll. En EGEN widget med ett `items`-fält faller
    // igenom till setProp-vägen nedan — omskrivningen
    // gäller bara de två typer ramverket äger.
    if (p.field == "items") {
        auto* dd = dynamic_cast<glaze::Dropdown*>(&w);
        auto* lb = dynamic_cast<glaze::ListBox*>(&w);
        if (dd || lb) {
            std::vector<std::string> lista;
            if (!parseStringList(p.value, lista)) {
                gfx::log("%s(%s): opak items-rad (ingen listliteral, "
                         "ritas inte)", origin, w.name.c_str());
                return;
            }
            if (dd)
                dd->setItems(std::move(lista));
            else
                lb->setItems(std::move(lista));
            return;
        }
    }

    // TRÄDETS VAL är en INDEXSTIG — `tree.setSelectedPath({0, 2});`
    // normaliseras av readern till `Prop{"selectedPath", "{0, 2}"}`. Vägen är
    // TYPAD som `items`: `parseIndexPath` avkodar strikt, och parsefel ⇒
    // raden förblir OPAK. En EGEN widget med ett sådant fält faller igenom
    // till setProp-vägen nedan.
    if (p.field == "selectedPath") {
        if (auto* tv = dynamic_cast<glaze::TreeView*>(&w)) {
            std::vector<int> stig;
            if (!parseIndexPath(p.value, stig)) {
                gfx::log("%s(%s): opak selectedPath-rad (ingen {0, 2}-stig, "
                         "ritas inte)", origin, w.name.c_str());
                return;
            }
            tv->setSelectedPath(stig);
            return;
        }
    }

    // FÄRGVÄLJARENS FÄRG — `farg.setColor(…)` normaliseras av
    // readern till `Prop{"color", …}`, och C++ tar TVÅ former: hextexten
    // (`"#3366FF"`, designerns form) och `gfx::Color{…}` (kodens). Texten går
    // den vanliga vägen nedan — `setProp("color")` tar just textformen —
    // medan literalen avkodas TYPAT här, med `Label::setColor`s strikta
    // delmängd (0–4 kanaler 0–255, C++:s standardvärden för resten). Annars
    // hade en rad som kompilerar och gäller i appen stått oritad i previewn,
    // regel 8 ordagrant.
    if (p.field == "color") {
        if (auto* cp = dynamic_cast<glaze::ColorPicker*>(&w)) {
            gfx::Color färg{};
            if (parseColorLiteral(p.value, färg)) {
                cp->setColor(färg);
                return;
            }
        }
    }

    // ETIKETTENS BREDDGRUPP — `lbl.setWidthGroup("form");`
    // normaliseras av readern till `Prop{"widthGroup", "\"form\""}`. Vägen är
    // TYPAD som `selectedPath`: bara en STRÄNGLITERAL sätter gruppen. Den
    // allmänna normaliseringen nedan (`nyckeltext`) hade gjort en identifierare
    // — `lbl.setWidthGroup(grupp);`, laglig C++ — till gruppnamnet "grupp", och
    // previewn hade då gjort MER än C++. Allt annat är OPAKT och etiketten
    // lämnas ORÖRD. En EGEN widget med ett sådant fält faller igenom till
    // setProp-vägen.
    // RITYTANS LÄGE går samma typade väg: bara en
    // strängliteral sätter läget.
    if (p.field == "inputMode") {
        if (auto* cv = dynamic_cast<glaze::Canvas*>(&w)) {
            std::string läge;
            if (!parseCppString(p.value, läge)) {
                gfx::log("%s(%s): opak inputMode-rad (ingen strängliteral, "
                         "utförs inte)", origin, w.name.c_str());
                return;
            }
            cv->setInputMode(std::move(läge));
            return;
        }
    }
    if (p.field == "widthGroup") {
        if (auto* l = dynamic_cast<glaze::Label*>(&w)) {
            std::string grupp;
            if (!parseCppString(p.value, grupp)) {
                gfx::log("%s(%s): opak widthGroup-rad (ingen strängliteral, "
                         "ritas inte)", origin, w.name.c_str());
                return;
            }
            l->setWidthGroup(std::move(grupp));
            return;
        }
    }

    // TEXTSTORLEKEN — `falt.setTextSize(glaze::mm(3));` normaliseras
    // av readern till `Prop{"textSize", "glaze::mm(3)"}`. Vägen är TYPAD:
    // måttet tolkas med ritytans regel (`parseMått` — `glaze::px/mm/world`
    // eller ett naket tal, px) och sätts på typen, som loggar världsenheter
    // och negativa mått precis som i C++. Ett värde som inte är ett mått är
    // OPAKT och widgeten ORÖRD. En egen widget med ett sådant fält faller
    // igenom till setProp-vägen.
    if (p.field == "textSize" && harTextstorlek(w)) {
        glaze::Length l;
        if (parseMått(p.value, l))
            sättTextstorlek(w, l);
        else
            gfx::log("%s(%s): opak textSize-rad (inget mått — glaze::px/mm "
                     "eller ett tal; ritas inte)",
                     origin, w.name.c_str());
        return;
    }

    // TEXTFÄLTEN. `okBtn.setText(glaze::tr(1042));`
    // normaliseras av readern till `Prop{"text", "glaze::tr(1042)"}`; grenen
    // sätter tokenet, en literal går genom `setProp`, och ALLT ANNAT — en
    // konstant i tr-anropet, ett uttryck — är en opak rad (`applyTextField`).
    // Grenen ligger FÖRE den allmänna värdenormaliseringen nedan, som gjorde
    // `glaze::tr(app::str::Hej)` till knappens text.
    //
    // BARA TEXTFÄLTEN: `name` är en IDENTITET och `img` ett handtag, och
    // ett tr-id i dem vore inte en översättning utan ett fel.
    if (ärTextfält(p.field)) {
        (void)applyTextField(w, p.field, p.value, origin);
        return;
    }

    // Övriga fält går via setProp med värdet konverterat till nyckeltext:
    // strängliteral → avciterad, enum → sista segmentet gement, tal/bool →
    // som de är. Okänd nyckel = opak rad: loggas + ignoreras i previewn,
    // round-trippas ordagrant vid spara (regel 4:s arvtagare).
    if (!w.setProp(p.field, nyckeltext(p.value)))
        gfx::log("%s(%s): opak fältrad '%s' (ritas inte)", origin,
                 w.name.c_str(), p.field.c_str());
}

namespace {

// Argumentets råa uttryckstext → bool. Grammatiken lagrar argument
// ordagrant, så "true"/"false" är precis det som står i filen; tomt
// argument tolkas som `true` (`showScrollbars()` finns inte, men en
// handskriven rad ska inte tyst bli `false`).
bool argBool(const std::string& args, bool& out)
{
    if (args.empty()) { out = true;  return true; }
    if (args == "true")  { out = true;  return true; }
    if (args == "false") { out = false; return true; }
    return false;
}

// Argumentlistan delad på KOMMA vid DJUP 0, aldrig inuti en
// strängliteral — menystigar och menytexter får innehålla komma
// ("Spara som…, igen"), och ett nästlat argument (`{glaze::tr(1),
// glaze::tr(2)}`) är ETT argument och inte två.
//
// Splittningen ÄR `bld::splitArgs` (builder_core) — den strukturella
// stacken över `{}` och `()`. Den här wrappern
// finns bara för att grenarna nedan vill ha en vektor och inte en
// felkod: ett AVSLAG ger noll argument, alltså faller ingen gren in och
// raden blir OPAK. Det är exakt rätt utfall för en felordnad
// argumentlista.
std::vector<std::string> delaArgument(const std::string& args)
{
    std::vector<std::string> ut;
    if (!bld::splitArgs(args, ut))
        ut.clear();
    return ut;
}

std::string trimma(const std::string& s)
{
    std::size_t a = s.find_first_not_of(" \t");
    std::size_t b = s.find_last_not_of(" \t");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}

// ------------------------------------------- EN argumentavkodning, delad
//
// Grenarna nedan delar EN avkodning: de här tre lambdorna är den
// ENDA avkodningen, och de är STRIKTA med flit: de svarar `false` i stället
// för att hitta på ett värde.
//
// ATOMICITETEN GÄLLER ALLA GRENAR, menyns inräknad: en menygren som gav
// TOM STRÄNG vid parsfel och ändå returnerade FRAMGÅNG hade gett en meny
// med en PÅHITTAD tom rubrik, ett tillstånd som inte finns någonstans.
// Regeln är: allt parsas till
// temporärer, widgeten muteras först när VARENDA argument lyckats,
// annars logg + `false` ⇒ OPAK rad.

bool argStr(const std::vector<std::string>& a, std::size_t i, std::string& ut)
{
    return i < a.size() && bld::parseCppString(trimma(a[i]), ut);
}

bool argFlagga(const std::vector<std::string>& a, std::size_t i, bool& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    if (t == "true")  { ut = true;  return true; }
    if (t == "false") { ut = false; return true; }
    return false;
}

// STRIKT heltal: valfritt TECKEN (`+` eller `-`), minst en siffra, inget
// släpande skräp. `12.5` och `1 + 1` avvisas — previewn får aldrig tolka en
// rad annorlunda än C++ gör.
//
// TVÅ FALL, båda samma felklass: en parser som är strängare än C++ gör
// LAGLIGA rader opaka, vilket är ett regel 8-brott och inte en försiktig
// delmängd.
//
//   (1) `+` måste godtas. `setRow(+0, "Ny")` är laglig C++ som muterar rad 0.
//   (2) SIFFERRÄKNING är ingen overflowkontroll: en gräns "högst nio
//       siffror" avvisar `1000000000`, `2147483647` och `-2147483648`,
//       alltså giltiga `int`. Det vore ofarligt för INDEX-metoder
//       (out-of-range ⇒ no-op ändå), men hjälparen kallar sig
//       en int-parser och nästa användning kan vara ett värde.
//
// `std::from_chars` gör båda rätt: den kräver full konsumtion och skiljer
// `result_out_of_range` från ett formatfel. Den hanterar däremot INTE ett
// ledande `+` (till skillnad från `strtol`), så tecknet plockas av här och
// beloppet negeras efteråt — vilket också gör `INT_MIN` säkert, eftersom
// `from_chars` läser hela det negativa talet i ett svep.
bool parseStriktInt(const std::string& s, int& ut)
{
    const std::string t = trimma(s);
    if (t.empty())
        return false;
    const bool        neg   = t[0] == '-';
    const std::size_t första = (neg || t[0] == '+') ? 1u : 0u;
    if (första >= t.size())
        return false;
    // Bara siffror efter tecknet — `from_chars` skulle annars acceptera ett
    // ledande blanksteg som redan trimmats bort, men aldrig `0x10`.
    for (std::size_t k = första; k < t.size(); ++k)
        if (t[k] < '0' || t[k] > '9')
            return false;

    // Negativa tal parsas HELA (inklusive minus) så `INT_MIN` ryms; ett
    // ledande `+` strippas, för `from_chars` känner det inte.
    const char* start = t.c_str() + (neg ? 0u : första);
    const char* slut  = t.c_str() + t.size();
    int         v     = 0;
    const auto  r     = std::from_chars(start, slut, v);
    if (r.ec != std::errc{} || r.ptr != slut)
        return false;
    ut = v;
    return true;
}

bool argInt(const std::vector<std::string>& a, std::size_t i, int& ut)
{
    return i < a.size() && parseStriktInt(a[i], ut);
}

// ETT ÖVERSÄTTBART TEXTARGUMENT: antingen en
// C++-strängliteral eller strikt `glaze::tr(N)`. Tokenet BEVARAS — sätter
// man den upplösta strängen fryser texten och previewn slutar följa
// språket, alltså gör MINDRE än den kompilerade koden.
bool argText(const std::vector<std::string>& a, std::size_t i, glaze::TrText& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    std::uint32_t     id = 0;
    if (bld::parseTrCall(t, id)) {
        ut = glaze::tr(id);
        return true;
    }
    std::string lit;
    if (!bld::parseCppString(t, lit))
        return false;
    ut = glaze::TrText(std::move(lit));
    return true;
}

// EN SEGMENTERAD MENYSTIG. STRIKT: `bld::parseMenuPath`
// avvisar allt utanför delmängden, och grenen blir då OPAK.
//
// Att avkoda stigen med `argStr`, som bara accepterar STRÄNGLITERALER,
// hade gjort en klammerform till en PÅHITTAD TOM stig medan grenen ändå
// returnerade framgång.
bool argMenuPath(const std::vector<std::string>& a, std::size_t i,
                 glaze::MenuPath& ut)
{
    if (i >= a.size())
        return false;
    std::vector<bld::PathSeg> segs;
    if (!bld::parseMenuPath(trimma(a[i]), segs))
        return false;
    std::vector<glaze::TrText> tok;
    tok.reserve(segs.size());
    for (const bld::PathSeg& s : segs)
        tok.push_back(s.isId ? glaze::tr(s.id) : glaze::TrText(s.text));
    ut = glaze::MenuPath(std::move(tok));
    return !ut.empty();   // `MenuPath` validerar SJÄLV och tömmer vid fel
}

// Delar på KOMMA vid djup 0 och utanför strängliteraler — SAMMA
// splittare som `delaArgument`, och det är hela poängen: två
// grammatiktolkningar kunde annars glida isär. Namnet finns för
// läsbarheten på anropsställena, men koden är EN.
bool delaKlammer(const std::string& s, std::vector<std::string>& ut)
{
    return bld::splitArgs(s, ut);
}

// Elementen i en klammerlista med C++:s regel för ett avslutande komma —
// definierad längre ned, deklarerad här för nod- och radlistorna.
bool listElement(const std::string& inre, std::vector<std::string>& ut);

// Skalar av EXAKT ett par yttre klamrar: "{…}" → "…". false = inte en
// klammerliteral.
bool skalaKlammer(const std::string& v, std::string& inre)
{
    const std::string t = trimma(v);
    if (t.size() < 2 || t.front() != '{' || t.back() != '}')
        return false;
    inre = t.substr(1, t.size() - 2);
    return true;
}

// `{{"Bredd", "120"}, {"Höjd", "80"}}` → PropertyGrid-rader.
//
// STRIKT delmängd, samma familj som `parseStringList`/`parseImgCall`:
// aggregatets fältordning är `Row`s egen (name, value, choices, readOnly,
// tag), varje sträng måste vara EN literal och varje bool exakt
// `true`/`false`. `{}` är en TOM lista och inte ett fel. ATOMISKT: `ut`
// fylls först när HELA literalen gått igenom.
bool parseRowsLiteral(const std::string& v,
                      std::vector<glaze::PropertyGrid::Row>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> radtexter;
    if (!delaKlammer(inre, radtexter))
        return false;

    std::vector<glaze::PropertyGrid::Row> rader;
    rader.reserve(radtexter.size());
    for (const std::string& rt : radtexter) {
        std::string radInre;
        if (!skalaKlammer(rt, radInre))
            return false;
        std::vector<std::string> fält;
        if (!delaKlammer(radInre, fält) || fält.size() > 5)
            return false;
        glaze::PropertyGrid::Row r;
        if (fält.size() > 0 && !bld::parseCppString(trimma(fält[0]), r.name))
            return false;
        if (fält.size() > 1 && !bld::parseCppString(trimma(fält[1]), r.value))
            return false;
        if (fält.size() > 2 &&
            !bld::parseStringList(trimma(fält[2]), r.choices))
            return false;
        if (fält.size() > 3) {
            const std::string t = trimma(fält[3]);
            if (t == "true")       r.readOnly = true;
            else if (t == "false") r.readOnly = false;
            else                   return false;
        }
        if (fält.size() > 4 && !bld::parseCppString(trimma(fält[4]), r.tag))
            return false;
        rader.push_back(std::move(r));
    }
    ut = std::move(rader);
    return true;
}

// `gfx::Color{40, 90, 180, 255}` → färgen.
//
// EXAKT specificerad, och "några tal" räcker INTE: `gfx::Color` bär
// BYTEkanaler, så en allmän floatparser eller en okontrollerad cast hade
// accepterat `-1`, `256`, `12.5` eller overflow — och previewn hade tolkat
// raden annorlunda än C++ gör.
//
// STÖDD DELMÄNGD: exakt skalet `gfx::Color{…}` · NOLL TILL FYRA DECIMALA
// HELTALSliteraler i 0–255 · blanksteg runt tal och komman · ett
// avslutande komma efter minst en kanal · inget annat. Alla parsas och
// valideras INNAN mutation.
//
// FÄRRE ÄN FYRA är laglig C++: `gfx::Color` är ett
// aggregat vars medlemmar har standardvärden, så `gfx::Color{51, 102, 255}`
// är r, g, b och a = 255, och `gfx::Color{}` är opak svart. De utelämnade
// kanalerna får SAMMA standardvärden här — de läses ur `gfx::Color{}` självt
// och skrivs inte av en gång till, så de kan inte glida isär från headern.
// Att avvisa tre kanaler lämnar `Label::setColor` oritad i
// previewn på en rad som kompilerar och gäller i appen (regel 8).
// Avslutande komma (`{51, 102, 255,}`) är laglig C++ på samma sätt — men
// inte ett ensamt (`{,}`) eller ett dubbelt (`{1,,}`), som C++ avvisar.
bool parseColorLiteral(const std::string& v, gfx::Color& ut)
{
    const std::string t = trimma(v);
    static const std::string kPrefix = "gfx::Color";
    if (t.rfind(kPrefix, 0) != 0)
        return false;
    std::string inre;
    if (!skalaKlammer(t.substr(kPrefix.size()), inre))
        return false;
    std::vector<std::string> delar;
    if (!delaKlammer(inre, delar))
        return false;
    // Splittern ger ett TOMT sista element för ett avslutande komma
    // (`f(a, )` har två argument där) — här är det C++:s initierarlista,
    // där ett avslutande komma efter minst ett element inte är något
    // element. Ett tomt element kvar efter det är ett fel.
    if (delar.size() >= 2 && trimma(delar.back()).empty())
        delar.pop_back();
    if (delar.size() > 4)
        return false;
    gfx::Color    färg{};   // C++:s standardvärden för det som utelämnas
    std::uint8_t* kanal[4] = {&färg.r, &färg.g, &färg.b, &färg.a};
    for (std::size_t i = 0; i < delar.size(); ++i) {
        int k = 0;
        if (!parseStriktInt(delar[i], k) || k < 0 || k > 255)
            return false;
        *kanal[i] = static_cast<std::uint8_t>(k);
    }
    ut = färg;
    return true;
}

// ------------------------------------------------ TRÄDETS argument

// En INDEXSTIG — `{0, 2}`, `{}` för roten. Strikt via builder_core.
bool argPath(const std::vector<std::string>& a, std::size_t i,
             glaze::TreePath& ut)
{
    return i < a.size() && bld::parseIndexPath(trimma(a[i]), ut);
}

// En LISTA AV STIGAR — `{{0, 1}, {2}}`, `moveNodes`s första argument
//. `{}` är en TOM lista (laglig C++ för en
// `std::vector<TreePath>`; widgeten loggar och avstår). Annars är varje
// element en klammerstig (`bld::parseIndexPath`), med C++:s regel för ett
// avslutande komma (`listElement` — efter minst ett element, aldrig ensamt).
// Ett element som inte är en klammerstig gör hela argumentet ogiltigt:
// `{0}` som stiglista är talet 0, som inte kompilerar, och avkodaren är lika
// sträng. ATOMISKT: `ut` fylls först när varje stig gått igenom.
bool argPathList(const std::vector<std::string>& a, std::size_t i,
                 std::vector<glaze::TreePath>& ut)
{
    if (i >= a.size())
        return false;
    if (tommaKlamrar(a[i])) {
        ut.clear();
        return true;
    }
    std::string              inre;
    std::vector<std::string> el;
    if (!skalaKlammer(a[i], inre) || !listElement(inre, el))
        return false;
    std::vector<glaze::TreePath> stigar;
    stigar.reserve(el.size());
    for (const std::string& e : el) {
        glaze::TreePath p;
        if (!bld::parseIndexPath(trimma(e), p))
            return false;
        stigar.push_back(std::move(p));
    }
    ut = std::move(stigar);
    return true;
}

// En BILD: `glaze::img("lista", "nyckel")`, en STANDARDIKON
// `glaze::stockIcon(glaze::StockIcon::Save)` med eller utan storleksklass och
// stil (`glaze::StockIconStyle::Color`) — eller `{}`, som är lika
// laglig C++ för ett `ImageSource` och
// betyder INGEN bild. Allt annat ⇒ false.
// Svaret är bildvärdet (`ImageSource`) och inte
// ett handtag, och det är EN regel för varje bild i grammatiken — widgetarnas
// och ritytans (`parseBildKälla` vidarebefordrar hit). En bild ur pixlar
// (`glaze::Bitmap`) har ingen form: den är koddata.
bool tommaKlamrar(const std::string& v)
{
    const std::string t = trimma(v);
    return t.size() >= 2 && t.front() == '{' && t.back() == '}' &&
           trimma(t.substr(1, t.size() - 2)).empty();
}

bool anrop(const std::string& v, const char* prefix, std::vector<std::string>& a);
template <class T>
bool enumVärde(const std::string& v, std::initializer_list<const char*> prefix,
               std::initializer_list<std::pair<const char*, T>> värden, T& ut);

bool argImg(const std::string& v, glaze::ImageSource& ut)
{
    const std::string t = trimma(v);
    if (tommaKlamrar(t)) {
        ut = glaze::ImageSource{};
        return true;
    }
    std::vector<std::string> a;
    if (anrop(t, "glaze::stockIcon", a)) {
        static const std::string kTyp = "glaze::StockIcon::";
        int                   storlek = 16;
        glaze::StockIconStyle stil    = glaze::StockIconStyle::Mono;
        if (a.empty() || a.size() > 3 || (a.size() >= 2 && !argInt(a, 1, storlek)) ||
            storlek <= 0 ||
            (a.size() == 3 &&
             !enumVärde<glaze::StockIconStyle>(
                 a[2], {"glaze::StockIconStyle::"},
                 {{"Mono", glaze::StockIconStyle::Mono},
                  {"Color", glaze::StockIconStyle::Color}},
                 stil)))
            return false;
        const std::string namn = trimma(a[0]);
        if (namn.compare(0, kTyp.size(), kTyp) != 0)
            return false;
        const std::optional<glaze::StockIcon> ikon =
            glaze::stockIconByName(std::string_view(namn).substr(kTyp.size()));
        if (!ikon)
            return false;
        ut = glaze::stockIcon(*ikon, storlek, stil);
        return true;
    }
    std::string lista, nyckel;
    if (!bld::parseImgCall(t, lista, nyckel))
        return false;
    ut = glaze::ImageSource(glaze::img(lista, nyckel));
    return true;
}

// Definierade längre ned, bland ritytans literaler: AGGREGATENS ENDA regel
// (typnamnet eller bara klamrarna, positionellt eller designerat i
// deklarationsordning) och den VALFRIA FÄRGEN (`valfri`s form).
bool skalaTyp(const std::string& v, std::initializer_list<const char*> typer,
              bool bar, std::string& inre);
bool aggregatFält(const std::string& inre, std::span<const char* const> namn,
                  std::vector<std::pair<std::size_t, std::string>>& ut);
bool argValfriFärg(const std::vector<std::string>& a, std::size_t i,
                   std::optional<gfx::Color>& ut);

// EN LISTA AV AGGREGAT — `{T{…}, {…}, …}`, nodernas, tabellradernas och
// statusfältens gemensamma form: de yttre klamrarna, elementen med C++:s regel
// för ett avslutande komma (`listElement`), varje element som `typer`s namn +
// `{…}` eller bara `{…}` (`skalaTyp`) med fälten positionellt eller designerat
// (`aggregatFält`), och `fält(post, index, värde)` för varje fält i `namn`s
// ordning. `minstEttFält` = ett aggregat utan fält (`{}`) är ogiltigt.
// ATOMISKT: `ut` fylls först när HELA literalen gått igenom.
template <class T, class Fält>
bool parseAggregatLista(const std::string& v,
                        std::initializer_list<const char*> typer,
                        std::span<const char* const> namn, bool minstEttFält,
                        std::vector<T>& ut, Fält fält)
{
    std::string              inre;
    std::vector<std::string> texter;
    if (!skalaKlammer(v, inre) || !listElement(inre, texter))
        return false;
    std::vector<T> poster;
    poster.reserve(texter.size());
    for (const std::string& et : texter) {
        std::string                                      eInre;
        std::vector<std::pair<std::size_t, std::string>> f;
        if (!skalaTyp(et, typer, true, eInre) || !aggregatFält(eInre, namn, f) ||
            (minstEttFält && f.empty()))
            return false;
        T post;
        for (const auto& [k, val] : f)
            if (!fält(post, k, val))
                return false;
        poster.push_back(std::move(post));
    }
    ut = std::move(poster);
    return true;
}

// `{{"Rot", {{"Barn"}}}, {"Annan"}}` → `setNodes`-listan. Varje nod är
// aggregatet `glaze::TreeNode` i den form C++ tar: `glaze::TreeNode{…}` eller
// bara `{…}`, POSITIONELLT i fältordningen (text, barn, bild, utfälld, släpp
// in i, kryssad, färg, celler — 1–8 fält) eller
// DESIGNERAT med namnen i DEKLARATIONSORDNING (`{.text = "Brev", .checked =
// true}` — fel ordning, ett okänt eller upprepat namn och en blandning
// kompilerar inte i C++20 och avvisas likadant, `aggregatFält`). Utelämnade
// fält tar sina default; ett positionellt aggregat bär minst texten. Texten
// är en strängliteral ELLER strikt `glaze::tr(N)` (tokenet bevaras), barnen
// samma form REKURSIVT, bilden `argImg`s två former, flaggorna `true`/`false`
// och färgen `gfx::Color{…}`, `std::nullopt` eller `{}` — en bar
// `{200, 0, 0}` kompilerar inte till en `std::optional` och är opak. Samma
// familj och samma ATOMICITET som `parseRowsLiteral`: `ut` fylls först när
// HELA literalen gått igenom.
bool parseTreeLiteral(const std::string& v, std::vector<glaze::TreeNode>& ut)
{
    static const char* const kFält[] = {"text",          "children", "img",
                                        "expanded",      "allowDropInto",
                                        "checked",       "color",
                                        "cells"};
    return parseAggregatLista(
        v, {"glaze::TreeNode"}, kFält, true, ut,
        [](glaze::TreeNode& n, std::size_t k, const std::string& val) {
            switch (k) {
            case 0: return argText({val}, 0, n.text);
            case 1: return parseTreeLiteral(val, n.children);
            case 2: return argImg(val, n.img);
            case 3: return argFlagga({val}, 0, n.expanded);
            case 4: return argFlagga({val}, 0, n.allowDropInto);
            case 5: return argFlagga({val}, 0, n.checked);
            case 6: return argValfriFärg({val}, 0, n.color);
            default: return bld::parseStringList(trimma(val), n.cells);
            }
        });
}

// ---------------------------------------------- TABELLENS argument

// En JUSTERING: exakt `glaze::Align::Left|Center|Right`. Prefixet krävs —
// `parseEnumTail` ensam hade tagit `glaze::Dock::Right` också, och den
// raden kompilerar inte som ett `Align`-argument.
bool argJustering(const std::vector<std::string>& a, std::size_t i,
                  glaze::Align& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    static const std::string kPrefix = "glaze::Align::";
    if (t.rfind(kPrefix, 0) != 0)
        return false;
    const std::string svans = t.substr(kPrefix.size());
    if (svans == "Left")        ut = glaze::Align::Left;
    else if (svans == "Center") ut = glaze::Align::Center;
    else if (svans == "Right")  ut = glaze::Align::Right;
    else                        return false;
    return true;
}

// ------------------------------------------- DOCKNINGENS argument

// En KANT eller zon: exakt `glaze::Dock::Left|Top|Right|Bottom|Fill|None` —
// prefixet krävs av justeringens skäl (`glaze::Align::Left` kompilerar inte
// som ett `Dock`-argument). `None` avkodas: raden kompilerar, och värden
// loggar och gör ingenting, precis som i appen.
bool argKant(const std::vector<std::string>& a, std::size_t i, glaze::Dock& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    static const std::string kPrefix = "glaze::Dock::";
    if (t.rfind(kPrefix, 0) != 0)
        return false;
    const std::string svans = t.substr(kPrefix.size());
    if (svans == "Left")        ut = glaze::Dock::Left;
    else if (svans == "Top")    ut = glaze::Dock::Top;
    else if (svans == "Right")  ut = glaze::Dock::Right;
    else if (svans == "Bottom") ut = glaze::Dock::Bottom;
    else if (svans == "Fill")   ut = glaze::Dock::Fill;
    else if (svans == "None")   ut = glaze::Dock::None;
    else                        return false;
    return true;
}

// Är den trimmade texten en C++-IDENTIFIERARE (bokstav eller `_` först, sedan
// bokstäver, siffror och `_`) — en MEDLEM som argument: dockningens paneler
// och fönstrets standard- och avbrytknapp. Nyckelorden
// skiljs inte ut: uppslaget mot trädet missar dem ändå.
bool ärIdentifierare(const std::string& t)
{
    if (t.empty() ||
        !(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_'))
        return false;
    for (const char ch : t)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
            return false;
    return true;
}

// Ett BARN som IDENTIFIERARE — medlemsnamnet i `.glazeui`
// (`dock1.dockPanel(sok, utforskare, …)`, `karta.setChildAnchor(info, …)`),
// slaget mot förälderns DIREKTA barn av typen `T` på `name`: previewträdet
// sätter `name` = medlemsnamnet, och raden gäller barn som redan är barn
// (trädbygget före satserna). Ett namn som inte är ett sådant barn ⇒ false ⇒
// OPAK rad — ingenting ändras, som i appen (som loggar).
template <class T>
bool argBarn(const glaze::Widget& förälder, const std::vector<std::string>& a,
             std::size_t i, T*& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    if (!ärIdentifierare(t))
        return false;
    for (glaze::Widget* ch : förälder.children())
        if (auto* p = dynamic_cast<T*>(ch); p && p->name == t) {
            ut = p;
            return true;
        }
    return false;
}

// RUBRIKENS JUSTERING — `Table::Column::headerAlign` är ett
// `std::optional<glaze::Align>`, så C++ tar TRE former på platsen: en
// justering (`argJustering`s exakta prefix) och de två tomma — `std::nullopt`
// och `{}` — som betyder "som cellerna". Alla tre kompilerar, så alla tre
// utförs; en strängare parser hade gjort lagliga rader opaka (regel 8).
bool argRubrikJustering(const std::vector<std::string>& a, std::size_t i,
                        std::optional<glaze::Align>& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    if (t == "std::nullopt" || t == "{}") {
        ut.reset();
        return true;
    }
    glaze::Align just = glaze::Align::Left;
    if (!argJustering(a, i, just))
        return false;
    ut = just;
    return true;
}

// DATAGRIDDENS KOLUMNTYP: exakt `glaze::DataGrid::Editor::Text|Choice|None`
// — prefixet krävs av samma skäl som justeringens.
bool argEditor(const std::vector<std::string>& a, std::size_t i,
               glaze::DataGrid::Editor& ut)
{
    if (i >= a.size())
        return false;
    const std::string t = trimma(a[i]);
    static const std::string kPrefix = "glaze::DataGrid::Editor::";
    if (t.rfind(kPrefix, 0) != 0)
        return false;
    const std::string svans = t.substr(kPrefix.size());
    if (svans == "Text")        ut = glaze::DataGrid::Editor::Text;
    else if (svans == "Choice") ut = glaze::DataGrid::Editor::Choice;
    else if (svans == "None")   ut = glaze::DataGrid::Editor::None;
    else                        return false;
    return true;
}

// Ett MÅTT (kolumnbredd) — samma tal som en fältrad tar (`bld::parseNum`:
// heltal, decimaltal, f-suffix; aldrig nan/inf).
bool argMått(const std::vector<std::string>& a, std::size_t i, float& ut)
{
    return i < a.size() && bld::parseNum(trimma(a[i]), ut);
}

// En KOLUMNBREDD: ett mått, eller konstanten för "efter innehållet"
// (`Column::autoWidth`, tal −1) — kolumntypen är basens, så konstanten nås
// genom varje typ som ärver den. Alla former kompilerar, så alla utförs.
bool argKolumnBredd(const std::vector<std::string>& a, std::size_t i, float& ut)
{
    if (i < a.size()) {
        const std::string t = trimma(a[i]);
        for (const char* typ :
             {"Table", "ColumnPanel", "ListView", "DataGrid", "TreeView"})
            if (t == std::string("glaze::") + typ + "::Column::autoWidth") {
                ut = glaze::ColumnPanel::Column::autoWidth;
                return true;
            }
    }
    return argMått(a, i, ut);
}

// Ett TAL för en `double`- eller `float`-parameter. Samma grind som
// `parseNum` (en C++-literal börjar med siffra, punkt eller tecken + en av
// dem, så `nan`/`inf` slipper aldrig in), och ett f-suffix tolkas med
// FLOAT-precision — exakt vad C++ gör med `12288.5f` innan det blir ett
// `double`. Ett HELTAL som börjar med 0 och fler siffror är OKTALT i C++
// (`010` är åtta) och avvisas hellre än läses som tio; HEXADECIMALT avvisas
// helt (`0x1f` är 31, men f:et hade lästs som ett suffix). Allt annat (u-/L-
// suffix, uttryck) ⇒ false ⇒ OPAK rad.
bool decimalt(const std::string& s, double& ut)
{
    const std::string t = trimma(s);
    if (t.empty() || t.find_first_of("xX") != std::string::npos)
        return false;
    const std::size_t d = (t[0] == '+' || t[0] == '-') ? 1u : 0u;
    if (d >= t.size() ||
        (!std::isdigit(static_cast<unsigned char>(t[d])) && t[d] != '.'))
        return false;
    if (t[d] == '0' && d + 1 < t.size() &&
        std::isdigit(static_cast<unsigned char>(t[d + 1])) &&
        t.find_first_of(".eE", d) == std::string::npos)
        return false;   // oktalt
    const bool  fSuffix = t.back() == 'f' || t.back() == 'F';
    const std::string tal = fSuffix ? t.substr(0, t.size() - 1) : t;
    char*       slut = nullptr;
    const double v = fSuffix ? double(std::strtof(tal.c_str(), &slut))
                             : std::strtod(tal.c_str(), &slut);
    if (slut != tal.c_str() + tal.size() || !std::isfinite(v))
        return false;
    ut = v;
    return true;
}

// En SORTERINGSNYCKEL — ett `double`, för en nyckel bär bytes och
// tidsstämplar som en `float` (sju siffror) tappar. `decimalt`s tal.
bool argDecimal(const std::vector<std::string>& a, std::size_t i, double& ut)
{
    return i < a.size() && decimalt(a[i], ut);
}

// `{{"Namn", 160}, {"Storlek", 70, glaze::Align::Right}}` → kolumnerna.
// Aggregatet `glaze::ColumnPanel::Column` (tabellens och trädets —
// `glaze::Table::Column` är samma typ) i FÄLTORDNING (rubrik, bredd,
// justering, rubrikens justering) med 1–4 fält, precis som C++
// tillåter. Rubriken är en strängliteral ELLER `glaze::tr(N)` (tokenet
// bevaras; `\n` i literalen är en radbrytning i rubriken). ATOMISKT: `ut`
// fylls först när hela literalen gått igenom — `parseRowsLiteral`s familj.
bool parseColumnsLiteral(const std::string& v,
                         std::vector<glaze::ColumnPanel::Column>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> texter;
    if (!delaKlammer(inre, texter))
        return false;
    std::vector<glaze::ColumnPanel::Column> kolumner;
    kolumner.reserve(texter.size());
    for (const std::string& kt : texter) {
        std::string kInre;
        if (!skalaKlammer(kt, kInre))
            return false;
        std::vector<std::string> fält;
        if (!delaKlammer(kInre, fält) || fält.empty() || fält.size() > 4)
            return false;
        glaze::ColumnPanel::Column k;
        if (!argText(fält, 0, k.text) ||
            (fält.size() > 1 && !argKolumnBredd(fält, 1, k.width)) ||
            (fält.size() > 2 && !argJustering(fält, 2, k.align)) ||
            (fält.size() > 3 && !argRubrikJustering(fält, 3, k.headerAlign)))
            return false;
        kolumner.push_back(std::move(k));
    }
    ut = std::move(kolumner);
    return true;
}

// `{{{"a.txt", "12 kB"}}, {{"b.png"}, glaze::img("fil", "bild")}}` →
// raderna. Aggregatet `glaze::Table::Row` i den form C++ tar — typnamnet
// (`glaze::Table::Row`, eller genom ättlingen `glaze::ListView::Row`/
// `glaze::DataGrid::Row`) eller bara `{…}`, POSITIONELLT i fältordningen
// (celler, bild, färg — 0–3 fält) eller
// DESIGNERAT i deklarationsordning (`{.cells = {"a"}, .color = gfx::Color{0,
// 140, 0}}`, `aggregatFält`). Cellerna är `parseStringList`s strikta
// listform, bilden `argImg`s två former och färgen `argValfriFärg`s tre.
// `{}` är en tom rad, och en tom lista inga rader.
bool parseTableRowsLiteral(const std::string& v,
                           std::vector<glaze::Table::Row>& ut)
{
    static const char* const kFält[] = {"cells", "img", "color"};
    return parseAggregatLista(
        v, {"glaze::Table::Row", "glaze::ListView::Row", "glaze::DataGrid::Row"},
        kFält, false, ut,
        [](glaze::Table::Row& r, std::size_t k, const std::string& val) {
            switch (k) {
            case 0: return bld::parseStringList(trimma(val), r.cells);
            case 1: return argImg(val, r.img);
            default: return argValfriFärg({val}, 0, r.color);
            }
        });
}

// `{{"Bilder", "*.png;*.jpg"}, {glaze::tr(43), "*.*"}}` → filtren. Aggregatet
// `glaze::FileFilter` i FÄLTORDNING (namn, mönster) med EXAKT två fält —
// ett filter utan mönster betyder ingenting. Namnet är en strängliteral
// ELLER `glaze::tr(N)` (tokenet bevaras), mönstren en strängliteral. `{}` är
// en tom lista. ATOMISKT, `parseColumnsLiteral`s familj.
bool parseFiltersLiteral(const std::string& v, std::vector<glaze::FileFilter>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> texter;
    if (!delaKlammer(inre, texter))
        return false;
    std::vector<glaze::FileFilter> filter;
    filter.reserve(texter.size());
    for (const std::string& ft : texter) {
        std::string fInre;
        if (!skalaKlammer(ft, fInre))
            return false;
        std::vector<std::string> fält;
        if (!delaKlammer(fInre, fält) || fält.size() != 2)
            return false;
        glaze::FileFilter f;
        if (!argText(fält, 0, f.name) || !argStr(fält, 1, f.patterns))
            return false;
        filter.push_back(std::move(f));
    }
    ut = std::move(filter);
    return true;
}

// `{{"Klar", 0, true}, {"Rad 1", 110}}` → statusradens fält.
// Aggregatet `glaze::StatusPanel` i den form C++ tar — typnamnet
// eller bara `{…}`, POSITIONELLT i fältordningen (text, bredd, fyll — 1–3
// fält) eller DESIGNERAT i deklarationsordning (`{.text = "Klar", .fill =
// true}`, `aggregatFält`). Texten är en strängliteral ELLER `glaze::tr(N)`,
// bredden ett mått och fyllningen `true`/`false`. ATOMISKT,
// `parseTableRowsLiteral`s familj.
bool parseStatusPanelsLiteral(const std::string& v,
                              std::vector<glaze::StatusPanel>& ut)
{
    static const char* const kFält[] = {"text", "width", "fill"};
    return parseAggregatLista(
        v, {"glaze::StatusPanel"}, kFält, true, ut,
        [](glaze::StatusPanel& p, std::size_t k, const std::string& val) {
            switch (k) {
            case 0: return argText({val}, 0, p.text);
            case 1: return argMått({val}, 0, p.width);
            default: return argFlagga({val}, 0, p.fill);
            }
        });
}

// ====================================================== RITYTANS LITERALER
//
// Regel 8: formlistan, stilarna, kameran och lagren i
// den form `.glazeui` bär — riktig C++ som KOMPILERAR (tests/fixtures/calls.*
// bevisar varje form här) — avkodade STRIKT och ATOMISKT: allt till
// temporärer, och ritytan muteras först när varenda del gått igenom. En form
// utanför delmängden ⇒ false ⇒ OPAK rad, ritytan orörd. Delmängden:
//   tal          `decimalt` (heltal eller decimaltal, valfritt f-suffix)
//   punkt        {x, y} eller glaze::Point{x, y}
//   punktlista   {punkt, …}; RINGAR {{punkt, …}, …} (polygon med hål)
//   rekt         {x, y, b, h} eller glaze::Rect{…}
//   mått         glaze::px/mm/world(tal), eller ett naket tal (px)
//   färg         gfx::Color{…} med 0–4 kanaler (`parseColorLiteral`)
//   Paint        en färg, glaze::Paint::linear(p, p, {stopp, …}) eller
//                glaze::Paint::radial(p, r0, r1, {stopp, …}); stopp
//                {offset, färg}
//   aggregat     Stroke · TextStyle · MarkerStyle · ImageStyle (opaciteten och
//                färgjusteringen) · Camera · Rect · Point ·
//                PathSegment · GradientStop: typens namn + {…}, eller bara
//                {…} där parametern har typen — DESIGNERAT med fälten i
//                DEKLARATIONSORDNING ({.paint = …, .width = …}; fel ordning
//                kompilerar inte och avvisas) eller POSITIONELLT; TextStyle
//                och MarkerStyle bär sist taket (maxLines, maxHeight — bara
//                text), golvet och taket (minSize, maxSize) och zoomen
//                (minZoom, maxZoom, tal) — och TextStyle efter
//                dem plattans kontur och radavståndet (outline, outlineWidth,
//                lineSpacing)
//   enum         med typens prefix: glaze::LineCap::Round, glaze::Unit::Mm,
//                glaze::TextAnchor::Bottom eller gfx::TextAnchor::Bottom …
//   bild         glaze::img("lista", "nyckel") eller {} (ingen bild)
//   text         en strängliteral eller glaze::tr(N) — tokenet bevaras
//   textdelar    {glaze::TextSpan{…}, {…}, …} —
//                {text, color, bold, italic, background, tabStop}, alla utom
//                texten valfria (std::nullopt eller {});
//                `{"abc"}` är TEXTEN, och `{}` och
//                `{{"abc"}}` är tvetydiga i C++ och därför opaka
//   väg          glaze::Path{{glaze::PathVerb::Move, punkt}, …} eller bara {…}
//   form         glaze::Shape::<fabrik>(…) — alla fabriker i shape.h, med
//                överlagringen som C++ väljer (punkter mot ringar avgörs av
//                klammerdjupet; en yta med kontur tar glaze::Stroke{…} eller
//                std::nullopt — en bar klammerlista kompilerar inte till en
//                std::optional; bilden på en rekt eller på FYRA FRIA HÖRN
//                avgörs av antalet argument, 2–3 mot 5–6)

// Elementen i en klammerlista (texten innanför `{…}`), med C++:s regel: ett
// avslutande komma efter minst ett element är inget element, men ett ensamt
// eller dubbelt komma är fel.
bool listElement(const std::string& inre, std::vector<std::string>& ut)
{
    if (!delaKlammer(inre, ut))
        return false;
    if (ut.size() >= 2 && trimma(ut.back()).empty())
        ut.pop_back();
    for (const std::string& e : ut)
        if (trimma(e).empty())
            return false;
    return true;
}

// `typ{…}` för något av typnamnen — eller, när parametern HAR typen, bara
// `{…}` (`bar`) — till texten innanför klamrarna.
bool skalaTyp(const std::string& v, std::initializer_list<const char*> typer,
              bool bar, std::string& inre)
{
    const std::string t = trimma(v);
    for (const char* typ : typer) {
        const std::size_t n = std::strlen(typ);
        if (t.compare(0, n, typ) == 0)
            return skalaKlammer(t.substr(n), inre);
    }
    return bar && skalaKlammer(t, inre);
}

// AGGREGATETS FÄLT: positionella i deklarationsordning, eller DESIGNERADE
// (`.namn = värde`) med namnen i DEKLARATIONSORDNING — C++20 kräver båda,
// så en blandning, ett okänt namn, ett upprepat eller fel ordning kompilerar
// inte och avvisas här. Svaret är fältets index och värdetext per angivet
// fält. En designerare är `.` följd av en bokstav eller `_` (`.5` är ett
// tal).
bool aggregatFält(const std::string& inre, std::span<const char* const> namn,
                  std::vector<std::pair<std::size_t, std::string>>& ut)
{
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    ut.clear();
    std::size_t designerade = 0, nästa = 0;
    for (std::size_t i = 0; i < el.size(); ++i) {
        const std::string e = trimma(el[i]);
        const bool designerad =
            e.size() > 1 && e[0] == '.' &&
            (std::isalpha(static_cast<unsigned char>(e[1])) || e[1] == '_');
        if (!designerad) {
            if (i >= namn.size())
                return false;
            ut.emplace_back(i, e);
            continue;
        }
        const std::size_t lika = e.find('=');
        if (lika == std::string::npos)
            return false;
        const std::string f = trimma(e.substr(1, lika - 1));
        std::size_t       k = nästa;
        while (k < namn.size() && f != namn[k])
            ++k;
        if (k == namn.size())
            return false;   // okänt, upprepat eller i fel ordning
        ut.emplace_back(k, trimma(e.substr(lika + 1)));
        nästa = k + 1;
        ++designerade;
    }
    return designerade == 0 || designerade == el.size();
}

// Ett ENUM-värde med något av typens prefix (`glaze::Unit::`, …).
template <class T>
bool enumVärde(const std::string& v, std::initializer_list<const char*> prefix,
               std::initializer_list<std::pair<const char*, T>> värden, T& ut)
{
    const std::string t = trimma(v);
    for (const char* p : prefix) {
        const std::size_t n = std::strlen(p);
        if (t.compare(0, n, p) != 0)
            continue;
        const std::string svans = t.substr(n);
        for (const auto& [namnet, värdet] : värden)
            if (svans == namnet) {
                ut = värdet;
                return true;
            }
        return false;
    }
    return false;
}

bool flagga(const std::string& v, bool& ut)
{
    const std::string t = trimma(v);
    if (t == "true")  { ut = true;  return true; }
    if (t == "false") { ut = false; return true; }
    return false;
}

bool flyttal(const std::string& v, float& ut)
{
    double d = 0.0;
    if (!decimalt(v, d))
        return false;
    ut = static_cast<float>(d);
    return true;
}

bool parsePunkt(const std::string& v, glaze::Point& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Point"}, true, inre))
        return false;
    static const char* const kFält[] = {"x", "y"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Point p;
    for (const auto& [k, val] : f)
        if (!decimalt(val, k == 0 ? p.x : p.y))
            return false;
    ut = p;
    return true;
}

// `{punkt, …}` — en tom lista är en tom vektor.
bool parsePunkter(const std::string& v, std::vector<glaze::Point>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<glaze::Point> p(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parsePunkt(el[i], p[i]))
            return false;
    ut = std::move(p);
    return true;
}

// Ringar `{{punkt, …}, …}` — ytterringen först, sedan hålen.
bool parseRingar(const std::string& v, std::vector<std::vector<glaze::Point>>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el) || el.empty())
        return false;
    std::vector<std::vector<glaze::Point>> r(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parsePunkter(el[i], r[i]))
            return false;
    ut = std::move(r);
    return true;
}

// Är listan RINGAR (dess första element är självt en lista av listor)?
// Det är den överlagring av `Shape::polygon` C++ väljer: `{{0, 0}, …}` kan
// inte bli en vektor av vektorer (`{0, 0}` är ingen punktvektor), och
// `{{{0, 0}, …}}` inte en punktvektor (en punkt har två fält).
bool ärRingar(const std::string& v)
{
    std::string inre;
    std::vector<std::string> el;
    if (!skalaKlammer(v, inre) || !listElement(inre, el) || el.empty())
        return false;
    std::string första;
    return skalaKlammer(el[0], första) && !trimma(första).empty() &&
           trimma(första)[0] == '{';
}

bool parseRekt(const std::string& v, glaze::Rect& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Rect"}, true, inre))
        return false;
    static const char* const kFält[] = {"x", "y", "w", "h"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Rect r;
    double* mål[] = {&r.x, &r.y, &r.w, &r.h};
    for (const auto& [k, val] : f)
        if (!decimalt(val, *mål[k]))
            return false;
    ut = r;
    return true;
}

// Ett MÅTT: glaze::px/mm/world(tal) — eller ett naket tal, som är px.
bool parseMått(const std::string& v, glaze::Length& ut)
{
    const std::string t = trimma(v);
    static const std::pair<const char*, glaze::Unit> kEnheter[] = {
        {"glaze::px(", glaze::Unit::Px},
        {"glaze::mm(", glaze::Unit::Mm},
        {"glaze::world(", glaze::Unit::World}};
    for (const auto& [namnet, enhet] : kEnheter) {
        const std::size_t n = std::strlen(namnet);
        if (t.compare(0, n, namnet) == 0) {
            float f = 0.0f;
            if (t.back() != ')' || !flyttal(t.substr(n, t.size() - n - 1), f))
                return false;
            ut = glaze::Length{f, enhet};
            return true;
        }
    }
    float f = 0.0f;
    if (!flyttal(t, f))
        return false;
    ut = glaze::Length{f};
    return true;
}

// Ett ANROP `prefix(args)` till argumenten — false om texten inte är det.
bool anrop(const std::string& v, const char* prefix, std::vector<std::string>& a)
{
    const std::string t = trimma(v);
    const std::size_t n = std::strlen(prefix);
    if (t.compare(0, n, prefix) != 0 || t.size() <= n || t[n] != '(' ||
        t.back() != ')')
        return false;
    return bld::splitArgs(t.substr(n + 1, t.size() - n - 2), a);
}

bool parseStopp(const std::string& v, std::vector<glaze::GradientStop>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    static const char* const kFält[] = {"offset", "color"};
    std::vector<glaze::GradientStop> s(el.size());
    for (std::size_t i = 0; i < el.size(); ++i) {
        std::string fi;
        std::vector<std::pair<std::size_t, std::string>> f;
        if (!skalaTyp(el[i], {"glaze::GradientStop", "gfx::GradientStop"}, true,
                      fi) ||
            !aggregatFält(fi, kFält, f))
            return false;
        for (const auto& [k, val] : f)
            if (k == 0 ? !flyttal(val, s[i].offset)
                       : !parseColorLiteral(val, s[i].color))
                return false;
    }
    ut = std::move(s);
    return true;
}

// FÄRGEN: en `gfx::Color` (Paint tar den implicit) eller en gradient.
bool parsePaint(const std::string& v, glaze::Paint& ut)
{
    gfx::Color c{};
    if (parseColorLiteral(v, c)) {
        ut = glaze::Paint(c);
        return true;
    }
    std::vector<std::string>         a;
    std::vector<glaze::GradientStop> stopp;
    glaze::Point                     p0, p1;
    if (anrop(v, "glaze::Paint::linear", a)) {
        if (a.size() != 3 || !parsePunkt(a[0], p0) || !parsePunkt(a[1], p1) ||
            !parseStopp(a[2], stopp))
            return false;
        ut = glaze::Paint::linear(p0, p1, std::move(stopp));
        return true;
    }
    double r0 = 0.0, r1 = 0.0;
    if (anrop(v, "glaze::Paint::radial", a)) {
        if (a.size() != 4 || !parsePunkt(a[0], p0) || !decimalt(a[1], r0) ||
            !decimalt(a[2], r1) || !parseStopp(a[3], stopp))
            return false;
        ut = glaze::Paint::radial(p0, r0, r1, std::move(stopp));
        return true;
    }
    return false;
}

// `{tal, …}` — strecken.
bool parseTalLista(const std::string& v, std::vector<float>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<float> t(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!flyttal(el[i], t[i]))
            return false;
    ut = std::move(t);
    return true;
}

bool parseStroke(const std::string& v, bool bar, glaze::Stroke& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Stroke"}, bar, inre))
        return false;
    static const char* const kFält[] = {"paint",      "width", "cap",
                                        "join",       "miterLimit", "dash",
                                        "dashUnit",   "dashOffset", "hitInside"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Stroke s;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = parsePaint(val, s.paint); break;
        case 1: ok = parseMått(val, s.width); break;
        case 2:
            ok = enumVärde(val, {"glaze::LineCap::"},
                           {std::pair{"Butt", glaze::LineCap::Butt},
                            std::pair{"Round", glaze::LineCap::Round},
                            std::pair{"Square", glaze::LineCap::Square}},
                           s.cap);
            break;
        case 3:
            ok = enumVärde(val, {"glaze::LineJoin::"},
                           {std::pair{"Round", glaze::LineJoin::Round},
                            std::pair{"Miter", glaze::LineJoin::Miter},
                            std::pair{"Bevel", glaze::LineJoin::Bevel}},
                           s.join);
            break;
        case 4: ok = flyttal(val, s.miterLimit); break;
        case 5: ok = parseTalLista(val, s.dash); break;
        case 6:
            ok = enumVärde(val, {"glaze::Unit::"},
                           {std::pair{"Px", glaze::Unit::Px},
                            std::pair{"Mm", glaze::Unit::Mm},
                            std::pair{"World", glaze::Unit::World}},
                           s.dashUnit);
            break;
        case 7: ok = flyttal(val, s.dashOffset); break;
        // INSIDAN TRÄFFAS.
        default: ok = flagga(val, s.hitInside); break;
        }
        if (!ok)
            return false;
    }
    ut = std::move(s);
    return true;
}

// En ytas KONTUR — `std::optional<Stroke>`: glaze::Stroke{…} eller
// std::nullopt (en bar klammerlista kompilerar inte till en optional).
bool parseValfriKontur(const std::string& v, std::optional<glaze::Stroke>& ut)
{
    if (trimma(v) == "std::nullopt") {
        ut.reset();
        return true;
    }
    glaze::Stroke s;
    if (!parseStroke(v, false, s))
        return false;
    ut = std::move(s);
    return true;
}

bool parseAnkare(const std::string& v, glaze::TextAnchor& ut)
{
    using A = glaze::TextAnchor;
    return enumVärde(v, {"glaze::TextAnchor::", "gfx::TextAnchor::"},
                     {std::pair{"TopLeft", A::TopLeft}, std::pair{"Top", A::Top},
                      std::pair{"TopRight", A::TopRight},
                      std::pair{"Left", A::Left}, std::pair{"Center", A::Center},
                      std::pair{"Right", A::Right},
                      std::pair{"BottomLeft", A::BottomLeft},
                      std::pair{"Bottom", A::Bottom},
                      std::pair{"BottomRight", A::BottomRight}},
                     ut);
}

bool parseRiktning(const std::string& v, glaze::Alignment& ut)
{
    return enumVärde(v, {"glaze::Alignment::", "gfx::TextRotation::"},
                     {std::pair{"Viewport", glaze::Alignment::Viewport},
                      std::pair{"Map", glaze::Alignment::Map},
                      std::pair{"Ground", glaze::Alignment::Ground}},
                     ut);
}

bool parseHeltal(const std::string& v, int& ut)
{
    return parseStriktInt(v, ut);
}

bool parseTextStil(const std::string& v, glaze::TextStyle& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::TextStyle"}, true, inre))
        return false;
    static const char* const kFält[] = {
        "size",      "family",   "bold",    "italic",   "color",
        "halo",      "haloWidth", "background", "padding", "anchor",
        "angle",     "rotation", "offsetX", "offsetY",  "wrapWidth",
        "align",     "collide",  "priority", "collisionPadding",
        "maxLines",  "maxHeight", "minSize", "maxSize", "minZoom", "maxZoom",
        "outline",   "outlineWidth", "lineSpacing"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::TextStyle s;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = parseMått(val, s.size); break;
        case 1:
            ok = enumVärde(val, {"gfx::FontFamily::"},
                           {std::pair{"UI", gfx::FontFamily::UI},
                            std::pair{"Mono", gfx::FontFamily::Mono}},
                           s.family);
            break;
        case 2: ok = flagga(val, s.bold); break;
        case 3: ok = flagga(val, s.italic); break;
        case 4: ok = parseColorLiteral(val, s.color); break;
        case 5: ok = parseColorLiteral(val, s.halo); break;
        case 6: ok = parseMått(val, s.haloWidth); break;
        case 7: ok = parseColorLiteral(val, s.background); break;
        case 8: ok = parseMått(val, s.padding); break;
        case 9: ok = parseAnkare(val, s.anchor); break;
        case 10: ok = flyttal(val, s.angle); break;
        case 11: ok = parseRiktning(val, s.rotation); break;
        case 12: ok = parseMått(val, s.offsetX); break;
        case 13: ok = parseMått(val, s.offsetY); break;
        case 14: ok = parseMått(val, s.wrapWidth); break;
        case 15: ok = argJustering({val}, 0, s.align); break;
        case 16: ok = flagga(val, s.collide); break;
        case 17: ok = parseHeltal(val, s.priority); break;
        case 18: ok = parseMått(val, s.collisionPadding); break;
        // TAKET, GOLVET OCH ZOOMEN.
        case 19: ok = parseHeltal(val, s.maxLines); break;
        case 20: ok = parseMått(val, s.maxHeight); break;
        case 21: ok = parseMått(val, s.minSize); break;
        case 22: ok = parseMått(val, s.maxSize); break;
        case 23: ok = decimalt(val, s.minZoom); break;
        case 24: ok = decimalt(val, s.maxZoom); break;
        // PLATTANS KONTUR OCH RADAVSTÅNDET.
        case 25: ok = parseColorLiteral(val, s.outline); break;
        case 26: ok = parseMått(val, s.outlineWidth); break;
        default: ok = flyttal(val, s.lineSpacing); break;
        }
        if (!ok)
            return false;
    }
    ut = std::move(s);
    return true;
}

// Ett VALFRITT fält (`std::optional<T>`): `std::nullopt` eller `{}` (också
// med blanksteg, som i C++) är osatt, annars värdet i fältets egen form.
template <class T, class Parse>
bool valfri(const std::string& v, std::optional<T>& ut, Parse parse)
{
    const std::string t = trimma(v);
    if (t == "std::nullopt" || tommaKlamrar(t)) {
        ut = std::nullopt;
        return true;
    }
    T värde{};
    if (!parse(t, värde))
        return false;
    ut = värde;
    return true;
}

// En VALFRI FÄRG som argument (en nods och en rads
// textfärg, `std::optional<gfx::Color>`): `std::nullopt` eller `{}` nollar
// (temats färg), `gfx::Color{…}` ger ett värde (`parseColorLiteral`) och allt
// annat — också en bar `{200, 0, 0}`, som inte kompilerar till en optional —
// är false. `valfri`s regel, ställd mot ett argument i listan.
bool argValfriFärg(const std::vector<std::string>& a, std::size_t i,
                   std::optional<gfx::Color>& ut)
{
    return i < a.size() && valfri(a[i], ut, parseColorLiteral);
}

// EN DEL AV EN ETIKETT: `glaze::TextSpan{…}` eller
// bara `{…}`, designerad eller positionell med fälten {text, color, bold,
// italic, background, tabStop} — texten en strängliteral eller glaze::tr(N)
// (tokenet bevaras), resten VALFRIA (`std::nullopt` eller `{}` är osatt):
// färgerna `gfx::Color{…}`, flaggorna true/false och tabbstoppet ett mått
//.
bool parseTextDel(const std::string& v, glaze::TextSpan& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::TextSpan"}, true, inre))
        return false;
    static const char* const kFält[] = {"text",   "color",      "bold",
                                        "italic", "background", "tabStop"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::TextSpan d;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = argText({val}, 0, d.text); break;
        case 1: ok = valfri(val, d.color, parseColorLiteral); break;
        case 2:
        case 3: ok = valfri(val, k == 2 ? d.bold : d.italic, flagga); break;
        case 4: ok = valfri(val, d.background, parseColorLiteral); break;
        default: ok = valfri(val, d.tabStop, parseMått); break;
        }
        if (!ok)
            return false;
    }
    ut = std::move(d);
    return true;
}

// ETIKETTENS TEXT — andra argumentet till `Shape::text`/`text3`, i den
// överlagring C++ väljer:
//   Text   en strängliteral eller glaze::tr(N) — också i en klammerlista med
//          ETT sådant element (`{"abc"}`: bara TrText tar den);
//   Delar  en lista av delar `{glaze::TextSpan{…}, {…}, …}` (`parseTextDel`).
// TVETYDIGT i C++ och därför OPAKT (Fel): `{}` och en lista vars ENDA element
// är en bar klammerlista med en strängliteral, glaze::tr(N) eller ingenting
// (`{{"abc"}}`, `{{}}`) — båda överlagringarna tar den och raden kompilerar
// inte. ATOMISKT: utvärdena fylls först när hela
// argumentet gått igenom.
enum class EtikettText { Fel, Text, Delar };

EtikettText parseEtikettText(const std::string& v, glaze::TrText& text,
                             std::vector<glaze::TextSpan>& delar)
{
    if (argText({v}, 0, text))
        return EtikettText::Text;
    std::string              inre;
    std::vector<std::string> el;
    if (!skalaKlammer(v, inre) || !listElement(inre, el) || el.empty())
        return EtikettText::Fel;   // `{}` är tvetydig
    if (el.size() == 1) {
        if (argText(el, 0, text))
            return EtikettText::Text;   // `{"abc"}`
        std::string              e1;
        std::vector<std::string> e1el;
        glaze::TrText            tvetydig;
        if (skalaKlammer(el[0], e1) && listElement(e1, e1el) &&
            (e1el.empty() || (e1el.size() == 1 && argText(e1el, 0, tvetydig))))
            return EtikettText::Fel;   // `{{"abc"}}`, `{{}}`
    }
    std::vector<glaze::TextSpan> d(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parseTextDel(el[i], d[i]))
            return EtikettText::Fel;
    delar = std::move(d);
    return EtikettText::Delar;
}

// En MARKÖRS (texturens, rutans) BILD: glaze::img("lista", "nyckel") — eller
// {}, som är INGEN bild. Det är `argImg`s regel och
// inget annat: en bild i grammatiken avkodas på EN plats, och `{ }` med
// blanksteg tas emot som i C++.
bool parseBildKälla(const std::string& v, glaze::ImageSource& ut)
{
    return argImg(v, ut);
}

// BILDENS STIL (shape.h `ImageStyle`): opaciteten och färgjusteringen —
// glaze::ImageStyle{…} eller bara {…}, fälten i deklarationsordning.
// Normeringen är fabrikens (`Shape::image`), som i C++.
bool parseBildStil(const std::string& v, glaze::ImageStyle& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::ImageStyle"}, true, inre))
        return false;
    static const char* const kFält[] = {"opacity", "saturation", "contrast",
                                        "brightness"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::ImageStyle s;
    for (const auto& [k, val] : f) {
        float& fält = k == 0   ? s.opacity
                      : k == 1 ? s.saturation
                      : k == 2 ? s.contrast
                               : s.brightness;
        if (!flyttal(val, fält))
            return false;
    }
    ut = s;
    return true;
}

bool parseMarkörStil(const std::string& v, glaze::MarkerStyle& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::MarkerStyle"}, true, inre))
        return false;
    static const char* const kFält[] = {
        "shape",  "size",   "fill",    "outline",  "outlineWidth",
        "angle",  "rotation", "anchor", "img",     "collide",
        "priority", "collisionPadding", "minSize", "maxSize", "minZoom",
        "maxZoom"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::MarkerStyle s;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0:
            ok = enumVärde(val, {"glaze::MarkerShape::", "gfx::MarkerShape::"},
                           {std::pair{"Circle", glaze::MarkerShape::Circle},
                            std::pair{"Square", glaze::MarkerShape::Square}},
                           s.shape);
            break;
        case 1: ok = parseMått(val, s.size); break;
        case 2: ok = parseColorLiteral(val, s.fill); break;
        case 3: ok = parseColorLiteral(val, s.outline); break;
        case 4: ok = parseMått(val, s.outlineWidth); break;
        case 5: ok = flyttal(val, s.angle); break;
        case 6: ok = parseRiktning(val, s.rotation); break;
        case 7: ok = parseAnkare(val, s.anchor); break;
        case 8: ok = parseBildKälla(val, s.img); break;
        case 9: ok = flagga(val, s.collide); break;
        case 10: ok = parseHeltal(val, s.priority); break;
        case 11: ok = parseMått(val, s.collisionPadding); break;
        // GOLVET, TAKET OCH ZOOMEN.
        case 12: ok = parseMått(val, s.minSize); break;
        case 13: ok = parseMått(val, s.maxSize); break;
        case 14: ok = decimalt(val, s.minZoom); break;
        default: ok = decimalt(val, s.maxZoom); break;
        }
        if (!ok)
            return false;
    }
    ut = std::move(s);
    return true;
}

// En VÄG: glaze::Path{{glaze::PathVerb::Move, punkt}, …}, eller bara {…} —
// fabrikens parameter HAR typen, och `Path`s initializer_list-konstruktor är
// implicit, så båda kompilerar till samma väg. Segmenten är
// `PathSegment`-aggregat (verb, p0, p1, p2).
bool parseVäg(const std::string& v, glaze::Path& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Path"}, true, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    static const char* const kFält[] = {"verb", "p0", "p1", "p2"};
    std::vector<glaze::PathSegment> seg(el.size());
    for (std::size_t i = 0; i < el.size(); ++i) {
        std::string fi;
        std::vector<std::pair<std::size_t, std::string>> f;
        if (!skalaTyp(el[i], {"glaze::PathSegment"}, true, fi) ||
            !aggregatFält(fi, kFält, f))
            return false;
        for (const auto& [k, val] : f) {
            const bool ok =
                k == 0 ? enumVärde(val, {"glaze::PathVerb::"},
                                   {std::pair{"Move", glaze::PathVerb::Move},
                                    std::pair{"Line", glaze::PathVerb::Line},
                                    std::pair{"Quad", glaze::PathVerb::Quad},
                                    std::pair{"Cubic", glaze::PathVerb::Cubic},
                                    std::pair{"Close", glaze::PathVerb::Close}},
                                   seg[i].verb)
                       : parsePunkt(val, k == 1   ? seg[i].p0
                                         : k == 2 ? seg[i].p1
                                                  : seg[i].p2);
            if (!ok)
                return false;
        }
    }
    ut = glaze::Path(std::move(seg));
    return true;
}

// Ett KONTURARGUMENT till `Shape::path` — en kontur är glaze::Stroke{…} eller
// en DESIGNERAD klammerlista (en bar `{}` är tvetydig mot Paint och avvisas).
bool ärKontur(const std::string& v)
{
    const std::string t = trimma(v);
    if (t.rfind("glaze::Stroke", 0) == 0)
        return true;
    std::string inre;
    return skalaKlammer(t, inre) && !trimma(inre).empty() &&
           trimma(inre)[0] == '.';
}

// ======================================================= RYMDENS LITERALER
//
// Regel 8: formerna i rymden, deras stilar, 3D-kameran,
// ljuset, renderläget och del-vyerna — samma familj, samma strikthet och samma
// ATOMICITET som 2D-literalerna ovan, och samma regel för aggregaten: typens
// namn + {…}, eller bara {…} där parametern har typen, POSITIONELLT eller
// DESIGNERAT med fälten i DEKLARATIONSORDNING (fältordningen i geometry.h,
// camera.h och style3d.h är kontrakt). Delmängden utöver 2D:s:
//   punkt i rymden  {x, y, z} eller glaze::Point3{…}; Float3 och Vec3 i samma
//                   form med sina namn — utelämnade fält är 0, som i C++
//   låda            glaze::Box3{punkt, punkt} eller {…}
//   intervall       glaze::IndexRange{first, count}: heltal 0–2³²−1 eller
//                   glaze::kRangeToEnd
//   mesh            glaze::Mesh::triangles({punkt, …}, färg) och
//                   glaze::Mesh::indexed({punkt, …}, {index, …}, färg)
//   3D-kamera       glaze::Camera3D{…} eller glaze::Camera3D::lookFrom(öga,
//                   gir, lutning[, synfält[, upp-axel]]) — bar {…} där C++
//                   själv väljer den (`parseKameraArg`)
//   aggregat        Lighting · RenderState · View · MeshStyle (sist kontrasten
//                   och ljusheten) · PointStyle · PassStyle ·
//                   ColorRamp · Instance · Placement
//   enum            glaze::Projection::, glaze::UpAxis::,
//                   glaze::LayerPlacement:: — och DepthTest, Blend och Cull
//                   med glaze:: ELLER gfx:: (glaze-namnen är alias för
//                   fasadens, så båda prefixen kompilerar)
//   program         ett NAMN som strängliteral (`MeshStyle::program`,
//                   `PointStyle::program`, `screenPass`). Previewn kör aldrig
//                   användarkod och registrerar alltså inga program
//                   (`addProgram` är koddata): ramverkets reserv ritar —
//                   meshen och punkterna med det inbyggda programmet, passet
//                   med `PassStyle::fallback` — och loggar en gång per rityta
//                   och namn (style3d.h).
// KODDATA och ingen form: `Shape::pointCloud` (punkter ur
// data — `points3` är literalformen), en mesh ur arrayer (`MeshDesc`,
// `fromWorld`), programmen och bilder ur `Bitmap`/`RenderTexture`. BRACE
// ELISION (`glaze::Box3{0, 0, 0, 1, 1, 1}`) kompilerar i C++ men avvisas, som
// i 2D-literalerna (`glaze::Camera{1, 2, 4}`): delmängden kräver det inre
// aggregatets klamrar.

// {x, y, z} med typens namn eller bart — `Point3`, `Vec3` och `Float3` delar
// formen. Utelämnade fält är 0: AGGREGATETS egna förval och inte den omgivande
// medlemmens (`.direction = {0, 0}` är {0, 0, 0}, inte ljusets förval).
bool parseXyz(const std::string& v, const char* typ, double (&ut)[3])
{
    std::string inre;
    if (!skalaTyp(v, {typ}, true, inre))
        return false;
    static const char* const kFält[] = {"x", "y", "z"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    double t[3] = {0.0, 0.0, 0.0};
    for (const auto& [k, val] : f)
        if (!decimalt(val, t[k]))
            return false;
    std::copy(std::begin(t), std::end(t), std::begin(ut));
    return true;
}

bool parsePunkt3(const std::string& v, glaze::Point3& ut)
{
    double t[3];
    if (!parseXyz(v, "glaze::Point3", t))
        return false;
    ut = glaze::Point3{t[0], t[1], t[2]};
    return true;
}

bool parseVektor3(const std::string& v, glaze::Vec3& ut)
{
    double t[3];
    if (!parseXyz(v, "glaze::Vec3", t))
        return false;
    ut = glaze::Vec3{t[0], t[1], t[2]};
    return true;
}

// Float3 — talen i float som C++ (ett double-tal blir float vid kompilering).
bool parseFloat3(const std::string& v, glaze::Float3& ut)
{
    double t[3];
    if (!parseXyz(v, "glaze::Float3", t))
        return false;
    ut = glaze::Float3{static_cast<float>(t[0]), static_cast<float>(t[1]),
                       static_cast<float>(t[2])};
    return true;
}

// `{punkt, …}` — en tom lista är en tom vektor (3D-fabrikerna är inte
// överlagrade, så `{}` kompilerar till en tom väg).
bool parsePunkter3(const std::string& v, std::vector<glaze::Point3>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<glaze::Point3> p(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parsePunkt3(el[i], p[i]))
            return false;
    ut = std::move(p);
    return true;
}

bool parseLåda(const std::string& v, glaze::Box3& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Box3"}, true, inre))
        return false;
    static const char* const kFält[] = {"min", "max"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Box3 b;
    for (const auto& [k, val] : f)
        if (!parsePunkt3(val, k == 0 ? b.min : b.max))
            return false;
    ut = b;
    return true;
}

// Ett HELTAL UTAN TECKEN (uint32 — intervallen och meshens index): decimalt,
// 0–2³²−1 med valfritt `+`, eller `glaze::kRangeToEnd`. Ett negativt tal, ett
// decimaltal och ett tal över gränsen är NARROWING i en klammerlista och
// kompilerar inte; ett oktalt (`010`) avvisas som i `decimalt`, och ett suffix
// (`3u`) som överallt i delmängden.
bool parseU32(const std::string& v, std::uint32_t& ut)
{
    const std::string t = trimma(v);
    if (t == "glaze::kRangeToEnd") {
        ut = glaze::kRangeToEnd;
        return true;
    }
    const std::size_t d = (!t.empty() && t[0] == '+') ? 1u : 0u;
    if (d >= t.size() || (t[d] == '0' && t.size() > d + 1))
        return false;
    for (std::size_t k = d; k < t.size(); ++k)
        if (t[k] < '0' || t[k] > '9')
            return false;
    std::uint64_t n    = 0;
    const char*   slut = t.data() + t.size();
    const auto    r    = std::from_chars(t.data() + d, slut, n);
    if (r.ec != std::errc{} || r.ptr != slut ||
        n > std::numeric_limits<std::uint32_t>::max())
        return false;
    ut = static_cast<std::uint32_t>(n);
    return true;
}

// `{index, …}` — `Mesh::indexed`s index.
bool parseIndexLista(const std::string& v, std::vector<std::uint32_t>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<std::uint32_t> ix(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parseU32(el[i], ix[i]))
            return false;
    ut = std::move(ix);
    return true;
}

bool parseIntervall(const std::string& v, glaze::IndexRange& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::IndexRange"}, true, inre))
        return false;
    static const char* const kFält[] = {"first", "count"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::IndexRange r;
    for (const auto& [k, val] : f)
        if (!parseU32(val, k == 0 ? r.first : r.count))
            return false;
    ut = r;
    return true;
}

bool parseRamp(const std::string& v, glaze::ColorRamp& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::ColorRamp"}, true, inre))
        return false;
    static const char* const kFält[] = {"stops", "min", "max"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::ColorRamp r;
    for (const auto& [k, val] : f) {
        const bool ok = k == 0 ? parseStopp(val, r.stops)
                               : decimalt(val, k == 1 ? r.min : r.max);
        if (!ok)
            return false;
    }
    ut = std::move(r);
    return true;
}

// `{bild, …}` — de egna programmens texturer, varje en glaze::img(…) eller {}.
bool parseBildKällor(const std::string& v, std::vector<glaze::ImageSource>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<glaze::ImageSource> b(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parseBildKälla(el[i], b[i]))
            return false;
    ut = std::move(b);
    return true;
}

bool parseMeshStil(const std::string& v, glaze::MeshStyle& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::MeshStyle"}, true, inre))
        return false;
    static const char* const kFält[] = {
        "color",    "opacity",  "lit",      "fog",     "saturation",
        "texture",  "ramp",     "range",    "size",    "program",
        "uniforms", "textures", "contrast", "brightness"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::MeshStyle s;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = parseColorLiteral(val, s.color); break;
        case 1: ok = flyttal(val, s.opacity); break;
        case 2: ok = flagga(val, s.lit); break;
        case 3: ok = flagga(val, s.fog); break;
        case 4: ok = flyttal(val, s.saturation); break;
        case 5: ok = parseBildKälla(val, s.texture); break;
        case 6: ok = parseRamp(val, s.ramp); break;
        case 7: ok = parseIntervall(val, s.range); break;
        case 8: ok = parseMått(val, s.size); break;
        case 9: ok = bld::parseCppString(trimma(val), s.program); break;
        case 10: ok = parseTalLista(val, s.uniforms); break;
        case 11: ok = parseBildKällor(val, s.textures); break;
        // FÄRGJUSTERINGENS två andra tal — sist i stilen.
        case 12: ok = flyttal(val, s.contrast); break;
        default: ok = flyttal(val, s.brightness); break;
        }
        if (!ok)
            return false;
    }
    ut = std::move(s);
    return true;
}

bool parsePunktStil(const std::string& v, glaze::PointStyle& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::PointStyle"}, true, inre))
        return false;
    static const char* const kFält[] = {"size", "shape",   "color",   "ramp",
                                        "fog",  "program", "uniforms"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::PointStyle s;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = parseMått(val, s.size); break;
        case 1:
            ok = enumVärde(val, {"glaze::MarkerShape::", "gfx::MarkerShape::"},
                           {std::pair{"Circle", glaze::MarkerShape::Circle},
                            std::pair{"Square", glaze::MarkerShape::Square}},
                           s.shape);
            break;
        case 2: ok = parseColorLiteral(val, s.color); break;
        case 3: ok = parseRamp(val, s.ramp); break;
        case 4: ok = flagga(val, s.fog); break;
        case 5: ok = bld::parseCppString(trimma(val), s.program); break;
        default: ok = parseTalLista(val, s.uniforms); break;
        }
        if (!ok)
            return false;
    }
    ut = std::move(s);
    return true;
}

bool parsePassStil(const std::string& v, glaze::PassStyle& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::PassStyle"}, true, inre))
        return false;
    static const char* const kFält[] = {"uniforms", "textures", "fallback"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::PassStyle s;
    for (const auto& [k, val] : f) {
        const bool ok = k == 0   ? parseTalLista(val, s.uniforms)
                        : k == 1 ? parseBildKällor(val, s.textures)
                                 : parseColorLiteral(val, s.fallback);
        if (!ok)
            return false;
    }
    ut = std::move(s);
    return true;
}

// Placeringens och instansens fem första fält (style3d.h) — samma namn och
// samma ordning i båda: läget, vinklarna och skalan.
template <class T>
bool ställningsFält(std::size_t k, const std::string& val, T& p)
{
    switch (k) {
    case 0: return parsePunkt3(val, p.at);
    case 1: return flyttal(val, p.yaw);
    case 2: return flyttal(val, p.pitch);
    case 3: return flyttal(val, p.roll);
    default: return parseFloat3(val, p.scale);
    }
}

bool parsePlacering(const std::string& v, glaze::Placement& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Placement"}, true, inre))
        return false;
    static const char* const kFält[] = {"at", "yaw", "pitch", "roll", "scale"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Placement p;
    for (const auto& [k, val] : f)
        if (!ställningsFält(k, val, p))
            return false;
    ut = p;
    return true;
}

// `{instans, …}` — varje instans glaze::Instance{…} eller bara {…}.
bool parseInstanser(const std::string& v, std::vector<glaze::Instance>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    static const char* const kFält[] = {"at",   "yaw",   "pitch",
                                        "roll", "scale", "color"};
    std::vector<glaze::Instance> inst(el.size());
    for (std::size_t i = 0; i < el.size(); ++i) {
        std::string fi;
        std::vector<std::pair<std::size_t, std::string>> f;
        if (!skalaTyp(el[i], {"glaze::Instance"}, true, fi) ||
            !aggregatFält(fi, kFält, f))
            return false;
        for (const auto& [k, val] : f) {
            const bool ok = k < 5 ? ställningsFält(k, val, inst[i])
                                  : parseColorLiteral(val, inst[i].color);
            if (!ok)
                return false;
        }
    }
    ut = std::move(inst);
    return true;
}

// EN MESH SOM LITERAL — de två fabrikerna i mesh.h som tar listor (en mesh ur
// arrayer är koddata). Meshen byggs här, av ramverkets egen fabrik: en
// felaktig mesh (lägen som inte är hela trianglar, ett index utanför) blir
// ingen mesh + logg, precis som i den kompilerade appen.
bool parseMesh(const std::string& v, glaze::Mesh& ut)
{
    std::vector<std::string>   a;
    std::vector<glaze::Point3> lägen;
    gfx::Color                 färg{};
    if (anrop(v, "glaze::Mesh::triangles", a)) {
        if (a.size() != 2 || !parsePunkter3(a[0], lägen) ||
            !parseColorLiteral(a[1], färg))
            return false;
        ut = glaze::Mesh::triangles(std::move(lägen), färg);
        return true;
    }
    std::vector<std::uint32_t> index;
    if (anrop(v, "glaze::Mesh::indexed", a)) {
        if (a.size() != 3 || !parsePunkter3(a[0], lägen) ||
            !parseIndexLista(a[1], index) || !parseColorLiteral(a[2], färg))
            return false;
        ut = glaze::Mesh::indexed(std::move(lägen), std::move(index), färg);
        return true;
    }
    return false;
}

// EN FORM: glaze::Shape::<fabrik>(…) med fabrikens argument — 2D-fabrikerna
// och rymdens (`line3` … `screenPass`; INSTANSERNAS
// prototyp är själv en form, avkodad rekursivt). Ett utelämnat sista argument
// får fabrikens förval genom att fabriken anropas med färre argument — förvalen
// (`gfx::kCylinderSides` …) skrivs aldrig av här.
bool parseForm(const std::string& v, glaze::Shape& ut)
{
    const std::string t = trimma(v);
    static const std::string kPre = "glaze::Shape::";
    if (t.compare(0, kPre.size(), kPre) != 0)
        return false;
    const std::size_t öp = t.find('(', kPre.size());
    if (öp == std::string::npos)
        return false;
    const std::string        fabrik = t.substr(kPre.size(), öp - kPre.size());
    std::vector<std::string> a;
    if (!anrop(t, (kPre + fabrik).c_str(), a))
        return false;
    const std::size_t n = a.size();

    glaze::Point                 p0, p1;
    glaze::Point3                q0, q1;
    glaze::Rect                  r;
    glaze::Paint                 fyll;
    glaze::Stroke                kontur;
    std::optional<glaze::Stroke> valfri;
    double                       d0 = 0.0, d1 = 0.0, d2 = 0.0;
    int                          sidor = 0;
    std::vector<glaze::Point>    punkter;
    std::vector<glaze::Point3>   punkter3;
    glaze::MeshStyle             mstil;
    // Det sista, valfria argumentet: ett defaultvärde när det saknas.
    const auto ytKontur = [&](std::size_t i) {
        return n <= i || parseValfriKontur(a[i], valfri);
    };
    const auto linjeKontur = [&](std::size_t i) {
        return n <= i || parseStroke(a[i], true, kontur);
    };
    const auto meshStil = [&](std::size_t i) {
        return n <= i || parseMeshStil(a[i], mstil);
    };

    if (fabrik == "line") {
        if (n < 2 || n > 3 || !parsePunkt(a[0], p0) || !parsePunkt(a[1], p1) ||
            !linjeKontur(2))
            return false;
        ut = glaze::Shape::line(p0, p1, std::move(kontur));
    } else if (fabrik == "polyline") {
        if (n < 1 || n > 2 || !parsePunkter(a[0], punkter) || !linjeKontur(1))
            return false;
        ut = glaze::Shape::polyline(std::move(punkter), std::move(kontur));
    } else if (fabrik == "arc") {
        if (n < 4 || n > 5 || !parsePunkt(a[0], p0) || !decimalt(a[1], d0) ||
            !decimalt(a[2], d1) || !decimalt(a[3], d2) || !linjeKontur(4))
            return false;
        ut = glaze::Shape::arc(p0, d0, d1, d2, std::move(kontur));
    } else if (fabrik == "polygon") {
        if (n < 2 || n > 3 || !parsePaint(a[1], fyll) || !ytKontur(2))
            return false;
        if (ärRingar(a[0])) {
            std::vector<std::vector<glaze::Point>> ringar;
            if (!parseRingar(a[0], ringar))
                return false;
            ut = glaze::Shape::polygon(std::move(ringar), std::move(fyll),
                                       std::move(valfri));
        } else {
            std::string inre;
            std::vector<std::string> el;
            // En tom lista är tvetydig mellan överlagringarna — C++ avvisar
            // den, och här är den opak.
            if (!skalaKlammer(a[0], inre) || !listElement(inre, el) ||
                el.empty() || !parsePunkter(a[0], punkter))
                return false;
            ut = glaze::Shape::polygon(std::move(punkter), std::move(fyll),
                                       std::move(valfri));
        }
    } else if (fabrik == "rect") {
        if (n < 2 || n > 3 || !parseRekt(a[0], r) || !parsePaint(a[1], fyll) ||
            !ytKontur(2))
            return false;
        ut = glaze::Shape::rect(r, std::move(fyll), std::move(valfri));
    } else if (fabrik == "ellipse") {
        if (n < 4 || n > 5 || !parsePunkt(a[0], p0) || !decimalt(a[1], d0) ||
            !decimalt(a[2], d1) || !parsePaint(a[3], fyll) || !ytKontur(4))
            return false;
        ut = glaze::Shape::ellipse(p0, d0, d1, std::move(fyll), std::move(valfri));
    } else if (fabrik == "circle") {
        if (n < 3 || n > 4 || !parsePunkt(a[0], p0) || !decimalt(a[1], d0) ||
            !parsePaint(a[2], fyll) || !ytKontur(3))
            return false;
        ut = glaze::Shape::circle(p0, d0, std::move(fyll), std::move(valfri));
    } else if (fabrik == "path") {
        glaze::Path väg;
        if (n < 2 || n > 3 || !parseVäg(a[0], väg))
            return false;
        if (n == 2 && ärKontur(a[1])) {
            if (!parseStroke(a[1], true, kontur))
                return false;
            ut = glaze::Shape::path(väg, std::move(kontur));
        } else {
            if (!parsePaint(a[1], fyll) || !ytKontur(2))
                return false;
            ut = glaze::Shape::path(väg, std::move(fyll), std::move(valfri));
        }
    } else if (fabrik == "text" || fabrik == "text3") {
        // Ankaret i planet eller rymden, texten i någon av sina två former
        // (`parseEtikettText` — en sträng eller delar) och stilen.
        const bool                   rymd = fabrik == "text3";
        glaze::TrText                text;
        std::vector<glaze::TextSpan> delar;
        glaze::TextStyle             stil;
        if (n < 2 || n > 3 || !(rymd ? parsePunkt3(a[0], q0) : parsePunkt(a[0], p0)))
            return false;
        const EtikettText form = parseEtikettText(a[1], text, delar);
        if (form == EtikettText::Fel || (n > 2 && !parseTextStil(a[2], stil)))
            return false;
        if (form == EtikettText::Delar)
            ut = rymd ? glaze::Shape::text3(q0, std::move(delar), std::move(stil))
                      : glaze::Shape::text(p0, std::move(delar), std::move(stil));
        else
            ut = rymd ? glaze::Shape::text3(q0, std::move(text), std::move(stil))
                      : glaze::Shape::text(p0, std::move(text), std::move(stil));
    } else if (fabrik == "marker" || fabrik == "points") {
        glaze::MarkerStyle stil;
        const bool         en = fabrik == "marker";
        if (n < 1 || n > 2 ||
            (en ? !parsePunkt(a[0], p0) : !parsePunkter(a[0], punkter)) ||
            (n > 1 && !parseMarkörStil(a[1], stil)))
            return false;
        ut = en ? glaze::Shape::marker(p0, std::move(stil))
                : glaze::Shape::points(std::move(punkter), std::move(stil));
    } else if (fabrik == "image") {
        // Överlagringen avgörs av ANTALET argument, som i C++: rektangeln tar
        // 2–3 (rekt, källa, stil), de fyra fria hörnen 5–6 (fyra punkter i
        // ringordning, källa, stil). 4 och fler än 6 kompilerar inte. Stilen
        // är `ImageStyle` — opaciteten och färgjusteringen.
        glaze::ImageSource källa;
        glaze::ImageStyle  stil;
        if (n >= 5 && n <= 6) {
            glaze::Point hörn[4];
            for (std::size_t i = 0; i < 4; ++i)
                if (!parsePunkt(a[i], hörn[i]))
                    return false;
            if (!parseBildKälla(a[4], källa) ||
                (n > 5 && !parseBildStil(a[5], stil)))
                return false;
            ut = glaze::Shape::image(hörn[0], hörn[1], hörn[2], hörn[3],
                                     std::move(källa), stil);
        } else {
            if (n < 2 || n > 3 || !parseRekt(a[0], r) ||
                !parseBildKälla(a[1], källa) ||
                (n > 2 && !parseBildStil(a[2], stil)))
                return false;
            ut = glaze::Shape::image(r, std::move(källa), stil);
        }
    } else if (fabrik == "line3") {
        if (n < 2 || n > 3 || !parsePunkt3(a[0], q0) || !parsePunkt3(a[1], q1) ||
            !linjeKontur(2))
            return false;
        ut = glaze::Shape::line3(q0, q1, std::move(kontur));
    } else if (fabrik == "polyline3") {
        if (n < 1 || n > 2 || !parsePunkter3(a[0], punkter3) || !linjeKontur(1))
            return false;
        ut = glaze::Shape::polyline3(std::move(punkter3), std::move(kontur));
    } else if (fabrik == "polygon3") {
        if (n < 2 || n > 3 || !parsePunkter3(a[0], punkter3) ||
            !parsePaint(a[1], fyll) || !ytKontur(2))
            return false;
        ut = glaze::Shape::polygon3(std::move(punkter3), std::move(fyll),
                                    std::move(valfri));
    } else if (fabrik == "marker3") {
        glaze::MarkerStyle stil;
        if (n < 1 || n > 2 || !parsePunkt3(a[0], q0) ||
            (n > 1 && !parseMarkörStil(a[1], stil)))
            return false;
        ut = glaze::Shape::marker3(q0, std::move(stil));
    } else if (fabrik == "points3") {
        glaze::PointStyle stil;
        if (n < 1 || n > 2 || !parsePunkter3(a[0], punkter3) ||
            (n > 1 && !parsePunktStil(a[1], stil)))
            return false;
        ut = glaze::Shape::points3(std::move(punkter3), std::move(stil));
    } else if (fabrik == "mesh") {
        glaze::Mesh      m;
        glaze::Placement plats;
        if (n < 1 || n > 3 || !parseMesh(a[0], m) ||
            (n > 1 && !parsePlacering(a[1], plats)) || !meshStil(2))
            return false;
        ut = glaze::Shape::mesh(std::move(m), plats, std::move(mstil));
    } else if (fabrik == "box") {
        glaze::Box3 låda;
        if (n < 1 || n > 2 || !parseLåda(a[0], låda) || !meshStil(1))
            return false;
        ut = glaze::Shape::box(låda, std::move(mstil));
    } else if (fabrik == "sphere") {
        glaze::Length radie;
        if (n < 2 || n > 3 || !parsePunkt3(a[0], q0) || !parseMått(a[1], radie) ||
            !meshStil(2))
            return false;
        ut = glaze::Shape::sphere(q0, radie, std::move(mstil));
    } else if (fabrik == "cylinder") {
        if (n < 3 || n > 5 || !parsePunkt3(a[0], q0) || !decimalt(a[1], d0) ||
            !decimalt(a[2], d1) || !meshStil(3) ||
            (n > 4 && !parseHeltal(a[4], sidor)))
            return false;
        ut = n > 4 ? glaze::Shape::cylinder(q0, d0, d1, std::move(mstil), sidor)
                   : glaze::Shape::cylinder(q0, d0, d1, std::move(mstil));
    } else if (fabrik == "tube") {
        if (n < 2 || n > 4 || !parsePunkter3(a[0], punkter3) ||
            !decimalt(a[1], d0) || !meshStil(2) ||
            (n > 3 && !parseHeltal(a[3], sidor)))
            return false;
        ut = n > 3 ? glaze::Shape::tube(std::move(punkter3), d0, std::move(mstil),
                                        sidor)
                   : glaze::Shape::tube(std::move(punkter3), d0, std::move(mstil));
    } else if (fabrik == "instances") {
        // Prototypen är en FORM — en låda, sfär, cylinder eller mesh. Något
        // annat kompilerar också, och fabriken ger då en tom form + logg,
        // precis som i appen.
        glaze::Shape                 prototyp;
        std::vector<glaze::Instance> inst;
        if (n < 2 || n > 3 || !parseForm(a[0], prototyp) ||
            !parseInstanser(a[1], inst) || !meshStil(2))
            return false;
        ut = glaze::Shape::instances(std::move(prototyp), std::move(inst),
                                     std::move(mstil));
    } else if (fabrik == "screenPass") {
        std::string      program;
        glaze::PassStyle stil;
        if (n < 1 || n > 2 || !argStr(a, 0, program) ||
            (n > 1 && !parsePassStil(a[1], stil)))
            return false;
        ut = glaze::Shape::screenPass(std::move(program), std::move(stil));
    } else {
        return false;
    }
    return true;
}

// `{form, …}` — formlistan i `setShapes`; en tom lista tömmer lagret.
bool parseFormer(const std::string& v, std::vector<glaze::Shape>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<glaze::Shape> f(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parseForm(el[i], f[i]))
            return false;
    ut = std::move(f);
    return true;
}

// Kartkameran: glaze::Camera{…} — bar {…} bara när `bar` (se `parseKameraArg`).
bool parseKamera(const std::string& v, bool bar, glaze::Camera& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Camera"}, bar, inre))
        return false;
    static const char* const kFält[] = {"center", "zoom", "bearing", "pitch"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Camera k;
    for (const auto& [i, val] : f) {
        const bool ok = i == 0   ? parsePunkt(val, k.center)
                        : i == 1 ? decimalt(val, k.zoom)
                        : i == 2 ? flyttal(val, k.bearing)
                                 : flyttal(val, k.pitch);
        if (!ok)
            return false;
    }
    ut = k;
    return true;
}

bool parseUppAxel(const std::string& v, glaze::UpAxis& ut)
{
    return enumVärde(v, {"glaze::UpAxis::"},
                     {std::pair{"Z", glaze::UpAxis::Z},
                      std::pair{"Y", glaze::UpAxis::Y}},
                     ut);
}

// 3D-kameran: glaze::Camera3D{…} (bar {…} när `bar`) eller FÖRSTAPERSONEN
// glaze::Camera3D::lookFrom(öga, gir, lutning[, synfält[, upp-axel]]) — den
// ANDRA formen camera.h har, och den kör ramverkets egen funktion. Ett
// utelämnat argument får lookFroms förval genom färre argument i anropet.
bool parseKamera3D(const std::string& v, bool bar, glaze::Camera3D& ut)
{
    std::vector<std::string> a;
    if (anrop(v, "glaze::Camera3D::lookFrom", a)) {
        glaze::Point3 öga;
        float         gir = 0.0f, lutning = 0.0f, synfält = 0.0f;
        glaze::UpAxis upp = glaze::UpAxis::Z;
        if (a.size() < 3 || a.size() > 5 || !parsePunkt3(a[0], öga) ||
            !flyttal(a[1], gir) || !flyttal(a[2], lutning) ||
            (a.size() > 3 && !flyttal(a[3], synfält)) ||
            (a.size() > 4 && !parseUppAxel(a[4], upp)))
            return false;
        ut = a.size() == 3 ? glaze::Camera3D::lookFrom(öga, gir, lutning)
             : a.size() == 4
                 ? glaze::Camera3D::lookFrom(öga, gir, lutning, synfält)
                 : glaze::Camera3D::lookFrom(öga, gir, lutning, synfält, upp);
        return true;
    }
    std::string inre;
    if (!skalaTyp(v, {"glaze::Camera3D"}, bar, inre))
        return false;
    static const char* const kFält[] = {
        "target",     "distance",  "azimuth",  "elevation",
        "roll",       "projection", "fov",     "orthoHeight",
        "nearPlane",  "farPlane",  "up"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Camera3D k;
    for (const auto& [i, val] : f) {
        bool ok = false;
        switch (i) {
        case 0: ok = parsePunkt3(val, k.target); break;
        case 1: ok = decimalt(val, k.distance); break;
        case 2: ok = flyttal(val, k.azimuth); break;
        case 3: ok = flyttal(val, k.elevation); break;
        case 4: ok = flyttal(val, k.roll); break;
        case 5:
            ok = enumVärde(val, {"glaze::Projection::"},
                           {std::pair{"Perspective",
                                      glaze::Projection::Perspective},
                            std::pair{"Orthographic",
                                      glaze::Projection::Orthographic}},
                           k.projection);
            break;
        case 6: ok = flyttal(val, k.fov); break;
        case 7: ok = decimalt(val, k.orthoHeight); break;
        case 8: ok = decimalt(val, k.nearPlane); break;
        case 9: ok = decimalt(val, k.farPlane); break;
        default: ok = parseUppAxel(val, k.up); break;
        }
        if (!ok)
            return false;
    }
    ut = k;
    return true;
}

// En DESIGNERAD klammerlista — `{.namn = …, …}`, minst ett element.
bool ärDesignerad(const std::string& v)
{
    std::string              inre;
    std::vector<std::string> el;
    if (!skalaKlammer(v, inre) || !listElement(inre, el) || el.empty())
        return false;
    const std::string e = trimma(el[0]);
    return e.size() > 1 && e[0] == '.' &&
           (std::isalpha(static_cast<unsigned char>(e[1])) || e[1] == '_');
}

// KAMERAN I `setCamera` — två ÖVERLAGRINGAR (canvas.h), och avkodningen väljer
// som C++ väljer:
//   glaze::Camera{…}                          kartkameran
//   glaze::Camera3D{…}, Camera3D::lookFrom(…) 3D-kameran
//   {.namn = …}                               den typ som HAR namnen: en
//                                             designerad lista väljer
//                                             överlagring på namnen
//                                             ([over.ics.list]), och de två
//                                             typernas fält är disjunkta.
//                                             Ordningen prövas efter valet,
//                                             som i C++.
//   {…} positionellt, och {}                  TVETYDIG mellan överlagringarna
//                                             — kompilerar inte och avvisas,
//                                             så previewn aldrig visar en rad
//                                             som inte bygger
enum class KameraForm : std::uint8_t { Ingen, Karta, Rymd };

KameraForm parseKameraArg(const std::string& v, glaze::Camera& karta,
                          glaze::Camera3D& rymd)
{
    const std::string t = trimma(v);
    if (t.rfind("glaze::Camera3D", 0) == 0)
        return parseKamera3D(t, false, rymd) ? KameraForm::Rymd
                                             : KameraForm::Ingen;
    if (t.rfind("glaze::Camera", 0) == 0)
        return parseKamera(t, false, karta) ? KameraForm::Karta
                                            : KameraForm::Ingen;
    if (!ärDesignerad(t))
        return KameraForm::Ingen;
    if (parseKamera(t, true, karta))
        return KameraForm::Karta;
    return parseKamera3D(t, true, rymd) ? KameraForm::Rymd : KameraForm::Ingen;
}

bool parseLjus(const std::string& v, glaze::Lighting& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::Lighting"}, true, inre))
        return false;
    static const char* const kFält[] = {
        "direction", "ambient",  "diffuse", "exponent", "rimDirection", "rim",
        "fog",       "fogStart", "fogEnd",  "fogPower", "twoSided"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::Lighting l;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = parseVektor3(val, l.direction); break;
        case 1: ok = flyttal(val, l.ambient); break;
        case 2: ok = flyttal(val, l.diffuse); break;
        case 3: ok = flyttal(val, l.exponent); break;
        case 4: ok = parseVektor3(val, l.rimDirection); break;
        case 5: ok = flyttal(val, l.rim); break;
        case 6: ok = parseColorLiteral(val, l.fog); break;
        case 7: ok = decimalt(val, l.fogStart); break;
        case 8: ok = decimalt(val, l.fogEnd); break;
        case 9: ok = flyttal(val, l.fogPower); break;
        default: ok = flagga(val, l.twoSided); break;
        }
        if (!ok)
            return false;
    }
    ut = l;
    return true;
}

// LAGRETS renderläge — `glaze::RenderState` (appens förval Less + skrivning).
// `gfx::RenderState{…}` är en ANNAN typ utan konvertering och avvisas; enumen
// tar båda prefixen, för glaze-namnen är alias för fasadens.
bool parseRenderläge(const std::string& v, glaze::RenderState& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::RenderState"}, true, inre))
        return false;
    static const char* const kFält[] = {"depth",     "depthWrite", "depthBias",
                                        "slopeBias", "blend",      "cull"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    using DT = glaze::DepthTest;
    using BL = glaze::Blend;
    using CU = glaze::Cull;
    glaze::RenderState s;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0:
            ok = enumVärde(val, {"glaze::DepthTest::", "gfx::DepthTest::"},
                           {std::pair{"Off", DT::Off}, std::pair{"Less", DT::Less},
                            std::pair{"LessEqual", DT::LessEqual},
                            std::pair{"Greater", DT::Greater},
                            std::pair{"GreaterEqual", DT::GreaterEqual},
                            std::pair{"Equal", DT::Equal},
                            std::pair{"Always", DT::Always}},
                           s.depth);
            break;
        case 1: ok = flagga(val, s.depthWrite); break;
        case 2: ok = flyttal(val, s.depthBias); break;
        case 3: ok = flyttal(val, s.slopeBias); break;
        case 4:
            ok = enumVärde(val, {"glaze::Blend::", "gfx::Blend::"},
                           {std::pair{"Alpha", BL::Alpha},
                            std::pair{"Premultiplied", BL::Premultiplied},
                            std::pair{"Additive", BL::Additive},
                            std::pair{"Opaque", BL::Opaque},
                            std::pair{"DepthOnly", BL::DepthOnly}},
                           s.blend);
            break;
        default:
            ok = enumVärde(val, {"glaze::Cull::", "gfx::Cull::"},
                           {std::pair{"None", CU::None}, std::pair{"Back", CU::Back},
                            std::pair{"Front", CU::Front}},
                           s.cull);
            break;
        }
        if (!ok)
            return false;
    }
    ut = s;
    return true;
}

// EN DEL-VY — glaze::View{…} eller {…}. Kameran är medlemmens typ, så en bar
// {…} är 3D-kameran där (ingen överlagring att välja mellan).
bool parseVy(const std::string& v, glaze::View& ut)
{
    std::string inre;
    if (!skalaTyp(v, {"glaze::View"}, true, inre))
        return false;
    static const char* const kFält[] = {"anchor", "offset",         "width",
                                        "height", "camera", "followRotation",
                                        "background"};
    std::vector<std::pair<std::size_t, std::string>> f;
    if (!aggregatFält(inre, kFält, f))
        return false;
    glaze::View vy;
    for (const auto& [k, val] : f) {
        bool ok = false;
        switch (k) {
        case 0: ok = parseAnkare(val, vy.anchor); break;
        case 1: ok = parsePunkt(val, vy.offset); break;
        case 2: ok = decimalt(val, vy.width); break;
        case 3: ok = decimalt(val, vy.height); break;
        case 4: ok = parseKamera3D(val, true, vy.camera); break;
        case 5: ok = flagga(val, vy.followRotation); break;
        default: ok = parseColorLiteral(val, vy.background); break;
        }
        if (!ok)
            return false;
    }
    ut = vy;
    return true;
}

// `{vy, …}` — `setViews`; en tom lista tar bort alla del-vyer.
bool parseVyer(const std::string& v, std::vector<glaze::View>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<glaze::View> vyer(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parseVy(el[i], vyer[i]))
            return false;
    ut = std::move(vyer);
    return true;
}

// `{heltal, …}` — lagrets vyer i `setLayerViews`.
bool parseHeltalLista(const std::string& v, std::vector<int>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<int> tal(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!parseStriktInt(el[i], tal[i]))
            return false;
    ut = std::move(tal);
    return true;
}

// `{text, …}` — lagernamnen i `setLayers`: strängliteraler eller
// glaze::tr(N), tokenet bevarat.
bool parseTexter(const std::string& v, std::vector<glaze::TrText>& ut)
{
    std::string inre;
    if (!skalaKlammer(v, inre))
        return false;
    std::vector<std::string> el;
    if (!listElement(inre, el))
        return false;
    std::vector<glaze::TrText> t(el.size());
    for (std::size_t i = 0; i < el.size(); ++i)
        if (!argText(el, i, t[i]))
            return false;
    ut = std::move(t);
    return true;
}

// Utfallet av en rad i en meny med den tomma stigen som rot —
// `applyRootMenuCall` nedan.
enum class RootMenu { NotMenu, Applied, Opaque };

// DEN TOMMA STIGEN SOM ROT — menyradens PLATTA form på ett mål med EN meny:
// programikonen (`appIcon.addItem({}, "Nytt")`), systemfältsikonen
// (`ikon.addItem({}, "Visa")`) och verktygsknappens
// rullgardin (`ny.addItem({}, "Dokument")`). `addMenu` finns inte — det
// finns ingen rubrikrad att välja i — och blir en opak rad.
// `bld::parseMenuPath` avvisar fortfarande en tom stig, för den är ogiltig
// för menyraden, så `{}` känns igen HÄR och bara här.
//
// `MedGenväg` är skillnaden mellan målen: programikonens `addItem` tar en
// valfri genväg, systemfältsikonens och verktygsknappens inte (inget fönster
// tar deras acceleratorer, tray_icon.h och tool_button.h), och där är ett
// tredje argument en opak rad — den raden hade inte kompilerat.
//
// ATOMISK som menyradens gren: allt parsas till temporärer och målet muteras
// först när varenda argument lyckats, annars logg + `Opaque`. `setHandler`
// är känd men utförs inte (en lambda — previewn kör aldrig användarkod).
template <bool MedGenväg, class Mål>
RootMenu applyRootMenuCall(Mål& mål, const Call& call, const char* origin,
                           const std::string& namn, const char* meny)
{
    if (call.method == "setHandler")
        return RootMenu::Applied;   // känd men utförs inte — se ovan

    const bool ärMeny =
        call.method == "addItem" || call.method == "addCheck" ||
        call.method == "addRadio" || call.method == "addSeparator" ||
        call.method == "addSubmenu";
    if (!ärMeny)
        return RootMenu::NotMenu;

    std::vector<std::string> a = delaArgument(call.args);
    // Stigen: `{}` = roten, annars menyradens strikta avkodning.
    auto stigAv = [&](std::size_t i, glaze::MenuPath& ut) {
        if (i >= a.size())
            return false;
        if (tommaKlamrar(a[i])) {   // `{}` / `{ }` är roten
            ut = glaze::MenuPath{};
            return true;
        }
        return argMenuPath(a, i, ut);
    };
    const std::size_t maxItem = MedGenväg ? 3u : 2u;
    glaze::MenuPath   stig;
    glaze::TrText     text;
    std::string       genväg;
    bool              kryss = false;
    bool              ok    = false;

    if (call.method == "addSeparator")
        ok = a.size() == 1 && stigAv(0, stig);
    else if (call.method == "addSubmenu")
        ok = a.size() == 2 && stigAv(0, stig) && argText(a, 1, text);
    else if (call.method == "addItem")
        ok = a.size() >= 2 && a.size() <= maxItem && stigAv(0, stig) &&
             argText(a, 1, text) &&
             (a.size() < 3 || argStr(a, 2, genväg));
    else   // addCheck / addRadio
        ok = a.size() >= 2 && a.size() <= 3 && stigAv(0, stig) &&
             argText(a, 1, text) &&
             (a.size() < 3 || argFlagga(a, 2, kryss));

    if (!ok) {
        gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, %s lämnas "
                 "ORÖRD)", origin, namn.c_str(), call.method.c_str(), meny);
        return RootMenu::Opaque;
    }
    if (call.method == "addSeparator")
        mål.addSeparator(stig);
    else if (call.method == "addSubmenu")
        mål.addSubmenu(stig, text);
    else if (call.method == "addItem") {
        if constexpr (MedGenväg)
            mål.addItem(stig, text, genväg);
        else
            mål.addItem(stig, text);
    } else if (call.method == "addCheck")
        mål.addCheck(stig, text, kryss);
    else
        mål.addRadio(stig, text, kryss);
    return RootMenu::Applied;
}

// MENYPOSTENS TILLSTÅND — de tre
// platta mutatorerna, med SAMMA namn och SAMMA tillstånd på menyraden,
// programikonen och systemfältsikonen: `setItemIcon(stig, bild)`,
// `setItemEnabled(stig, på)` och `setItemChecked(stig, på)`. Stigen avkodas
// som bärarens add-satser — `{}`-roten bara där den tomma stigen är roten
// (`medRot`: programikonen och systemfältet; menyraden avvisar den), annars
// `argMenuPath` — bilden med `argImg`s två former och flaggan `true`/`false`.
// ATOMISK: allt parsas före mutationen, annars logg + `Opaque`. En stig som
// avkodas men inte finns i menyn är BÄRARENS logg + no-op, precis som när
// raden körs i appen.
template <class Mål>
RootMenu applyMenuItemState(Mål& mål, const Call& call, const char* origin,
                            const std::string& namn, const char* meny,
                            bool medRot)
{
    const bool ärTillstånd = call.method == "setItemIcon" ||
                             call.method == "setItemEnabled" ||
                             call.method == "setItemChecked";
    if (!ärTillstånd)
        return RootMenu::NotMenu;

    const std::vector<std::string> a = delaArgument(call.args);
    glaze::MenuPath                stig;
    glaze::ImageSource             bild;
    bool                           på = false;
    bool ok = a.size() == 2 &&
              ((medRot && tommaKlamrar(a[0])) || argMenuPath(a, 0, stig));
    if (ok && call.method == "setItemIcon")
        ok = argImg(a[1], bild);
    else if (ok)
        ok = argFlagga(a, 1, på);
    if (!ok) {
        gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, %s lämnas "
                 "ORÖRD)", origin, namn.c_str(), call.method.c_str(), meny);
        return RootMenu::Opaque;
    }
    if (call.method == "setItemIcon")
        mål.setItemIcon(stig, bild);
    else if (call.method == "setItemEnabled")
        mål.setItemEnabled(stig, på);
    else
        mål.setItemChecked(stig, på);
    return RootMenu::Applied;
}

// RADVALET OCH KRYSSEN PÅ EN PLATT LISTA — SAMMA ord och SAMMA form på
// ListBox, ListView och DataGrid:
// `setSelected(rad, på)` och — där listan bär kryss (`MedKryss`) —
// `setChecked(rad, på)`, index och flagga strikt, plus de två argumentlösa
// massformerna `selectAll()`/`clearSelection()`. ATOMISKT: fel argument ⇒
// logg + `false` ⇒ OPAK rad; en rad utanför är widgetens logg + no-op.
// `nullopt` = inget av orden, raden går vidare. Trädets val bär en STIG och
// står i trädgrenen.
template <bool MedKryss, class Lista>
std::optional<bool> applyRowSelectionCall(Lista& lista, const Call& call,
                                          const char*        origin,
                                          const std::string& namn)
{
    const std::string& mm = call.method;
    if (mm == "setSelected" || (MedKryss && mm == "setChecked")) {
        const std::vector<std::string> a = delaArgument(call.args);
        int  rad = 0;
        bool på  = false;
        if (a.size() != 2 || !argInt(a, 0, rad) || !argFlagga(a, 1, på)) {
            gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, ritas "
                     "inte)", origin, namn.c_str(), mm.c_str());
            return false;
        }
        if constexpr (MedKryss)
            if (mm == "setChecked") {
                lista.setChecked(rad, på);
                return true;
            }
        lista.setSelected(rad, på);
        return true;
    }
    if ((mm == "selectAll" || mm == "clearSelection") &&
        trimma(call.args).empty()) {
        if (mm == "selectAll")
            lista.selectAll();
        else
            lista.clearSelection();
        return true;
    }
    return std::nullopt;
}

// ------------------------------------------- INMATNINGEN
//
// Uppräkningarna i glaze/input.h, STRIKT: bara den kvalificerade formen
// (`glaze::InputSource::WheelY`) eller den med `using enum` (`InputSource::`,
// det bara namnet) — ett okänt namn är en opak rad.
bool parseEnhet(const std::string& v, glaze::InputDevice& ut)
{
    using D = glaze::InputDevice;
    return enumVärde(v, {"glaze::InputDevice::", "InputDevice::", ""},
                     {std::pair{"Mouse", D::Mouse}, std::pair{"Touchpad", D::Touchpad},
                      std::pair{"Keyboard", D::Keyboard}},
                     ut);
}

bool parseKälla(const std::string& v, glaze::InputSource& ut)
{
    using S = glaze::InputSource;
    return enumVärde(
        v, {"glaze::InputSource::", "InputSource::", ""},
        {std::pair{"LeftDragX", S::LeftDragX}, std::pair{"LeftDragY", S::LeftDragY},
         std::pair{"MiddleDragX", S::MiddleDragX},
         std::pair{"MiddleDragY", S::MiddleDragY},
         std::pair{"RightDragX", S::RightDragX}, std::pair{"RightDragY", S::RightDragY},
         std::pair{"WheelX", S::WheelX}, std::pair{"WheelY", S::WheelY},
         std::pair{"ScrollX", S::ScrollX}, std::pair{"ScrollY", S::ScrollY},
         std::pair{"Pinch", S::Pinch}, std::pair{"Twist", S::Twist},
         std::pair{"ArrowsX", S::ArrowsX}, std::pair{"ArrowsY", S::ArrowsY},
         std::pair{"PlusMinus", S::PlusMinus}},
        ut);
}

bool parseHandling(const std::string& v, glaze::InputAction& ut)
{
    using A = glaze::InputAction;
    return enumVärde(v, {"glaze::InputAction::", "InputAction::", ""},
                     {std::pair{"None", A::None}, std::pair{"PanX", A::PanX},
                      std::pair{"PanY", A::PanY}, std::pair{"Zoom", A::Zoom},
                      std::pair{"Rotate", A::Rotate}, std::pair{"Tilt", A::Tilt},
                      std::pair{"Fly", A::Fly}, std::pair{"Lift", A::Lift}},
                     ut);
}

bool parseRiktning(const std::string& v, glaze::InputDirection& ut)
{
    using R = glaze::InputDirection;
    return enumVärde(v, {"glaze::InputDirection::", "InputDirection::", ""},
                     {std::pair{"Normal", R::Normal}, std::pair{"Inverted", R::Inverted}},
                     ut);
}

// MODIFIERARNA — `0` eller `gfx::kModX`-konstanter med `|`
// emellan, som C++ läser dem: `gfx::kModCtrl | gfx::kModShift`. Ett uttryck
// i övrigt (`+`, ett tal utöver 0, en egen konstant) är en opak rad.
bool argMods(const std::string& v, std::uint32_t& ut)
{
    const std::string t = trimma(v);
    if (t == "0" || t == "0u") {
        ut = 0;
        return true;
    }
    std::uint32_t mask  = 0;
    std::size_t   start = 0;
    while (true) {
        const std::size_t slut = t.find('|', start);
        const std::string del  = trimma(t.substr(start, slut - start));
        std::uint32_t     bit  = 0;
        if (del == "gfx::kModCtrl")       bit = gfx::kModCtrl;
        else if (del == "gfx::kModShift") bit = gfx::kModShift;
        else if (del == "gfx::kModAlt")   bit = gfx::kModAlt;
        else if (del == "gfx::kModCmd")   bit = gfx::kModCmd;
        else                              return false;
        mask |= bit;
        if (slut == std::string::npos)
            break;
        start = slut + 1;
    }
    ut = mask;
    return true;
}

} // namespace

bool applyCall(glaze::Widget& w, const Call& call, const char* origin)
{
    // De fyra generella — de bor på `Widget`, alltså på VARJE widget.
    // Att de är metoder och inte fält är hela skälet satsformen finns:
    // `setVisible`/`setEnabled` är det som INVALIDERAR (regel 1), och ett
    // publikt `visible`-fält hade gått runt dem.
    if (call.method == "show")    { w.show();    return true; }
    if (call.method == "hide")    { w.hide();    return true; }
    if (call.method == "enable")  { w.enable();  return true; }
    if (call.method == "disable") { w.disable(); return true; }
    // PEKAREN — tillbaka till typens egen. Sättaren
    // `setCursor(gfx::PointerShape::X)` är ett setter-fält (`cursor`) och går
    // `applyProp`s väg; återställningen har inget värde och är därför ett anrop.
    if (call.method == "resetCursor" && trimma(call.args).empty()) {
        w.resetCursor();
        return true;
    }
    // RULLNINGENS RIKTNING — på varje widget:
    // `lista.setWheelInvert(glaze::InputDevice::Mouse, true, false);` och
    // `lista.clearWheelInvert(glaze::InputDevice::Mouse);`. Lagras; previewn
    // har inget hjul.
    if (call.method == "setWheelInvert" || call.method == "clearWheelInvert") {
        const std::vector<std::string> a = delaArgument(call.args);
        glaze::InputDevice             enhet{};
        bool                           lodrätt = false, sidled = false;
        if (call.method == "clearWheelInvert" && a.size() == 1 &&
            parseEnhet(a[0], enhet)) {
            w.clearWheelInvert(enhet);
            return true;
        }
        if (call.method == "setWheelInvert" && a.size() == 3 &&
            parseEnhet(a[0], enhet) && argFlagga(a, 1, lodrätt) &&
            argFlagga(a, 2, sidled)) {
            w.setWheelInvert(enhet, lodrätt, sidled);
            return true;
        }
        gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte)", origin,
                 w.name.c_str(), call.method.c_str());
        return false;
    }

#ifdef GLAZE_PREVIEW_TERMINAL
    // TERMINALEN — sidopaketets anrop med flera argument,
    // i apply_terminal.cpp; nullopt = inte dess anrop.
    if (const std::optional<bool> t = detail::applyTerminalCall(w, call, origin))
        return *t;
#endif

    bool b = false;
    if (call.method == "setIndeterminate" && argBool(call.args, b)) {
        if (auto* p = dynamic_cast<glaze::Progress*>(&w)) {
            p->setIndeterminate(b);
            return true;
        }
    }
    if (call.method == "showScrollbars" && argBool(call.args, b)) {
        if (auto* p = dynamic_cast<glaze::Panel*>(&w)) {
            p->showScrollbars(b);
            return true;
        }
    }

    // MENYRADEN — den PLATTA formen (hård regel 8: allt ska gå i BÅDA
    // vägarna). Utan de här grenarna hade previewn visat en TOM menyrad
    // trots att `.glazeui` bär hela menyn, vilket är precis det
    // regelbrott regeln finns för att förhindra.
    //
    // `setHandler` utförs MEDVETET inte: argumentet är en lambda och
    // previewn kör aldrig användarkod — samma behandling som
    // event-kopplingar redan får. Raden bevaras och kompileras.
    // MENYGRENEN ÄR ATOMISK: en gren som gav TOM STRÄNG vid parsfel och ändå
    // returnerade FRAMGÅNG skulle ge en meny med en PÅHITTAD tom rubrik —
    // källtexten bevarades men previewn visade ett tillstånd som inte finns
    // någonstans. Samma krav som för resten av `applyCall`: ALLT parsas till
    // temporärer, och menyraden muteras först när varenda argument lyckats.
    // Annars logg + `false` ⇒ OPAK rad, precis som ett okänt fält.
    if (auto* mb = dynamic_cast<glaze::MenuBar*>(&w)) {
        if (call.method == "setHandler")
            return true;   // känd men utförs inte — se noten ovan

        // Postens bild, enabled och kryss — titelvägen
        // som stig, aldrig `{}`.
        const RootMenu tillstånd =
            applyMenuItemState(*mb, call, origin, w.name, "menyn", false);
        if (tillstånd != RootMenu::NotMenu)
            return tillstånd == RootMenu::Applied;

        const bool ärMeny =
            call.method == "addMenu" || call.method == "addItem" ||
            call.method == "addCheck" || call.method == "addRadio" ||
            call.method == "addSeparator" || call.method == "addSubmenu";
        if (ärMeny) {
            std::vector<std::string> a = delaArgument(call.args);
            // `addMenu` tar en RUBRIK och inte en stig — en tom `MenuPath`
            // hade dessutom varit ogiltig i typen.
            glaze::MenuPath stig{"?"};
            glaze::TrText   rubrik, text;
            std::string     genväg;
            bool            kryss = false;
            bool            ok    = false;

            if (call.method == "addMenu")
                ok = a.size() == 1 && argText(a, 0, rubrik);
            else if (call.method == "addSeparator")
                ok = a.size() == 1 && argMenuPath(a, 0, stig);
            else if (call.method == "addSubmenu")
                ok = a.size() == 2 && argMenuPath(a, 0, stig) &&
                     argText(a, 1, text);
            else if (call.method == "addItem")
                ok = a.size() >= 2 && a.size() <= 3 && argMenuPath(a, 0, stig) &&
                     argText(a, 1, text) &&
                     (a.size() < 3 || argStr(a, 2, genväg));
            else   // addCheck / addRadio
                ok = a.size() >= 2 && a.size() <= 3 && argMenuPath(a, 0, stig) &&
                     argText(a, 1, text) &&
                     (a.size() < 3 || argFlagga(a, 2, kryss));

            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "menyn lämnas ORÖRD)", origin, w.name.c_str(),
                         call.method.c_str());
                return false;
            }
            if (call.method == "addMenu")            mb->addMenu(rubrik);
            else if (call.method == "addSeparator")  mb->addSeparator(stig);
            else if (call.method == "addSubmenu")    mb->addSubmenu(stig, text);
            else if (call.method == "addItem")       mb->addItem(stig, text, genväg);
            else if (call.method == "addCheck")      mb->addCheck(stig, text, kryss);
            else                                     mb->addRadio(stig, text, kryss);
            return true;
        }
    }

    // PROGRAMMENYN — menyradens PLATTA form på
    // programikonen, med den TOMMA stigen `{}` som rot och genvägen som
    // valfritt tredje argument (`appIcon.addItem({}, "Nytt", "Ctrl+N")`).
    // Parsningen är `applyRootMenuCall`, delad med systemfältsikonen och
    // verktygsknappen.
    if (auto* ai = dynamic_cast<glaze::AppIcon*>(&w)) {
        const RootMenu r =
            applyRootMenuCall<true>(*ai, call, origin, w.name, "programmenyn");
        if (r != RootMenu::NotMenu)
            return r == RootMenu::Applied;
        // Postens bild, enabled och kryss.
        const RootMenu s =
            applyMenuItemState(*ai, call, origin, w.name, "programmenyn", true);
        if (s != RootMenu::NotMenu)
            return s == RootMenu::Applied;
    }

    // VERKTYGSKNAPPENS RULLGARDIN — systemfältsikonens form:
    // `{}` som rot och INGET genvägsargument (ingen acceleratorväg frågar en
    // knappmeny, tool_button.h), och postens tre tillstånd som på de andra
    // menybärarna. Previewn bygger menyn så knappen ritar sin pil; den öppnar
    // den aldrig. `setSplit` är ett setter-fält och går `applyProp`s väg.
    if (auto* tb = dynamic_cast<glaze::ToolButton*>(&w)) {
        const RootMenu r =
            applyRootMenuCall<false>(*tb, call, origin, w.name, "knappmenyn");
        if (r != RootMenu::NotMenu)
            return r == RootMenu::Applied;
        const RootMenu s =
            applyMenuItemState(*tb, call, origin, w.name, "knappmenyn", true);
        if (s != RootMenu::NotMenu)
            return s == RootMenu::Applied;
    }

    // ------------------------------------- KOLLEKTIONSVOKABULÄREN (REGEL 8)
    //
    // `addX` · `setX(index, …)` · `setXs(lista)` — samma form på
    // PropertyGrid, ListBox och Dropdown. Utan de här
    // grenarna ritas en fylld grid TOM i canvasen och i glaze-render medan
    // den byggda appen visar raderna, vilket är regel 8-brottet ordagrant.
    //
    // ATOMISKT: exakt argumentantal kontrolleras, ALLA argument
    // parsas till temporärer, och widgeten muteras först när allt lyckats.
    // Annars logg + `false` — aldrig en halv rad, aldrig ett påhittat
    // tillstånd. Grenarna ligger FÖRE setter-fallbacken: `setChecked` ger
    // fältnamnet `checked`, som fallbacken hade skickat till
    // `ListBox::setProp` som en skräpsträng.
    if (auto* pg = dynamic_cast<glaze::PropertyGrid*>(&w)) {
        if (call.method == "setRows") {
            std::vector<glaze::PropertyGrid::Row> rader;
            if (parseRowsLiteral(call.args, rader)) {
                pg->setRows(std::move(rader));
                return true;
            }
            gfx::log("%s(%s): opak setRows-rad (ingen radliteral, ritas "
                     "inte)", origin, w.name.c_str());
            return false;
        }
        if (call.method == "addRow" || call.method == "setRow") {
            const bool               sätt = call.method == "setRow";
            const std::size_t        bas  = sätt ? 1u : 0u;
            std::vector<std::string> a    = delaArgument(call.args);
            int         index = 0;
            std::string namn, värde, tag;
            bool        readOnly = false;
            const bool  ok =
                a.size() >= bas + 1 && a.size() <= bas + 4 &&
                (!sätt || argInt(a, 0, index)) && argStr(a, bas, namn) &&
                (a.size() <= bas + 1 || argStr(a, bas + 1, värde)) &&
                (a.size() <= bas + 2 || argFlagga(a, bas + 2, readOnly)) &&
                (a.size() <= bas + 3 || argStr(a, bas + 3, tag));
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "ritas inte)", origin, w.name.c_str(),
                         call.method.c_str());
                return false;
            }
            // Index utanför intervallet är widgetens eget logg + no-op.
            if (sätt)
                pg->setRow(index, namn, värde, readOnly, tag);
            else
                pg->addRow(namn, värde, readOnly, tag);
            return true;
        }
    }

    // ListBox och Dropdown bär IDENTISK form — en gren, två typer.
    {
        auto* lb = dynamic_cast<glaze::ListBox*>(&w);
        auto* dd = dynamic_cast<glaze::Dropdown*>(&w);
        if (lb || dd) {
            std::vector<std::string> a = delaArgument(call.args);
            std::string              text;
            if (call.method == "addItem") {
                if (a.size() == 1 && argStr(a, 0, text)) {
                    if (lb) lb->addItem(std::move(text));
                    else    dd->addItem(std::move(text));
                    return true;
                }
                gfx::log("%s(%s): opak addItem-rad (argumenten avkodas "
                         "inte, ritas inte)", origin, w.name.c_str());
                return false;
            }
            if (call.method == "setItem") {
                int index = 0;
                if (a.size() == 2 && argInt(a, 0, index) &&
                    argStr(a, 1, text)) {
                    if (lb) lb->setItem(index, std::move(text));
                    else    dd->setItem(index, std::move(text));
                    return true;
                }
                gfx::log("%s(%s): opak setItem-rad (argumenten avkodas "
                         "inte, ritas inte)", origin, w.name.c_str());
                return false;
            }
        }
        // KOLLISIONEN: `setChecked` heter likadant på
        // Checkbox/Radio (ETT bool-argument) och på ListBox (index + bool).
        // Eftersom settertabellen är typmedveten når raden hit som en
        // `Call` i stället för att bli en felsemantisk `Prop`. FLERVALET
        // är listvyns ord; `setMultiSelect` är ett
        // setter-fält.
        if (lb)
            if (const auto r = applyRowSelectionCall<true>(*lb, call, origin,
                                                           w.name))
                return *r;
    }

    // DOCKNINGEN — värdens `dockPanel` med TVÅ argument (panelen
    // och en kant: ytterst där) eller TRE (panelen, målet och var: `Fill` som
    // flik, en kant bredvid), där panelerna är IDENTIFIERARE som slås upp
    // bland värdens barn (`argBarn`) och kanten kräver `glaze::Dock::`
    // (`argKant`). Panelens min/max (två mått) och `activate()` (inga
    // argument). ATOMISKT som resten av `applyCall`: allt parsas först, annars
    // logg + `false` ⇒ OPAK rad. Ett fel som C++ också kompilerar (samma
    // panel två gånger, `Dock::None`) är värdens eget logg + no-op, som i
    // appen.
    if (auto* värd = dynamic_cast<glaze::DockHost*>(&w)) {
        if (call.method == "dockPanel") {
            const std::vector<std::string> a = delaArgument(call.args);
            glaze::DockPanel* p   = nullptr;
            glaze::DockPanel* mål = nullptr;
            glaze::Dock       var = glaze::Dock::None;
            const bool ok =
                (a.size() == 2 && argBarn(*värd, a, 0, p) &&
                 argKant(a, 1, var)) ||
                (a.size() == 3 && argBarn(*värd, a, 0, p) &&
                 argBarn(*värd, a, 1, mål) && argKant(a, 2, var));
            if (!ok) {
                gfx::log("%s(%s): opak dockPanel-rad (panelerna är inga barn "
                         "eller argumenten avkodas inte, värden lämnas ORÖRD)",
                         origin, w.name.c_str());
                return false;
            }
            if (mål)
                värd->dockPanel(*p, *mål, var);
            else
                värd->dockPanel(*p, var);
            return true;
        }
    }
    if (auto* panel = dynamic_cast<glaze::DockPanel*>(&w)) {
        if (call.method == "setMinimumSize" || call.method == "setMaximumSize") {
            const std::vector<std::string> a = delaArgument(call.args);
            float bw = 0.0f, bh = 0.0f;
            if (a.size() != 2 || !argMått(a, 0, bw) || !argMått(a, 1, bh)) {
                gfx::log("%s(%s): opak %s-rad (två mått väntades, panelen "
                         "lämnas ORÖRD)", origin, w.name.c_str(),
                         call.method.c_str());
                return false;
            }
            // Ett negativt mått kompilerar och är panelens eget logg + no-op.
            if (call.method == "setMinimumSize")
                panel->setMinimumSize(bw, bh);
            else
                panel->setMaximumSize(bw, bh);
            return true;
        }
        if (call.method == "activate" && trimma(call.args).empty()) {
            panel->activate();
            return true;
        }
        // DET FLYTANDE FÖNSTRETS PLATS — fyra tal, atomiskt:
        // ett fel lämnar panelen orörd. Previewn lagrar bara platsen; den
        // öppnar aldrig ett fönster.
        if (call.method == "setFloatingBounds") {
            const std::vector<std::string> a = delaArgument(call.args);
            float fx = 0.0f, fy = 0.0f, fb = 0.0f, fh = 0.0f;
            if (a.size() != 4 || !argMått(a, 0, fx) || !argMått(a, 1, fy) ||
                !argMått(a, 2, fb) || !argMått(a, 3, fh)) {
                gfx::log("%s(%s): opak setFloatingBounds-rad (fyra tal "
                         "väntades, panelen lämnas ORÖRD)",
                         origin, w.name.c_str());
                return false;
            }
            // En negativ storlek kompilerar och är panelens eget logg + no-op.
            panel->setFloatingBounds(fx, fy, fb, fh);
            return true;
        }
    }
    // DET FLYTANDE VERKTYGSFÄLTETS PLATS — två tal,
    // atomiskt: ett fel lämnar fältet orört. Storleken är fältets egen, så
    // ingen storlek att ta. Previewn lagrar bara platsen; den öppnar aldrig
    // ett fönster.
    if (auto* fält = dynamic_cast<glaze::ToolBar*>(&w)) {
        if (call.method == "setFloatingPosition") {
            const std::vector<std::string> a = delaArgument(call.args);
            float fx = 0.0f, fy = 0.0f;
            if (a.size() != 2 || !argMått(a, 0, fx) || !argMått(a, 1, fy)) {
                gfx::log("%s(%s): opak setFloatingPosition-rad (två tal "
                         "väntades, fältet lämnas ORÖRT)",
                         origin, w.name.c_str());
                return false;
            }
            fält->setFloatingPosition(fx, fy);
            return true;
        }
        // DE TILLÅTNA KANTERNA — exakt ett
        // argument: `{}` eller en klammerlista vars element alla är en KANT
        // (`argKant`, prefixet krävs; `Fill`/`None` är inga kanter här, fast
        // sättaren loggar och ignorerar dem i appen). Atomiskt: listan byggs
        // först, sättaren sist, och allt annat är en opak rad som lämnar
        // fältet ORÖRT.
        if (call.method == "setAllowedEdges") {
            const std::vector<std::string> a = delaArgument(call.args);
            std::string                    inre;
            std::vector<std::string>       delar;
            std::vector<glaze::Dock>       kanter;
            bool ok = a.size() == 1 && skalaKlammer(a[0], inre) &&
                      (trimma(inre).empty() || listElement(inre, delar));
            for (std::size_t i = 0; ok && i < delar.size(); ++i) {
                glaze::Dock k{};
                ok = argKant(delar, i, k) && k != glaze::Dock::Fill &&
                     k != glaze::Dock::None;
                if (ok)
                    kanter.push_back(k);
            }
            if (!ok) {
                gfx::log("%s(%s): opak setAllowedEdges-rad (en lista med "
                         "glaze::Dock::Left/Top/Right/Bottom väntades, fältet "
                         "lämnas ORÖRT)",
                         origin, w.name.c_str());
                return false;
            }
            fält->setAllowedEdges(std::move(kanter));
            return true;
        }
    }

    // LABEL::setColor. `color` står inte i
    // settertabellen — dess `setProp` är ett ALIAS för `role` och vill ha ett
    // ROLLNAMN, medan `setColor` tar en riktig `gfx::Color`. Raden är
    // därför en Call, och den här grenen utför den.
    //
    // ⚠ ENDAST `setColor`, ALDRIG `setRole`: `Label::setColor` sätter
    // `Role::Custom` SJÄLV. Skillnaden är exakt var beteendet bor: previewn
    // får aldrig göra MER än C++-metoden (det vore ett regel 8-brott i
    // motsatt riktning), men när METODEN gör mer ärver båda vägarna det
    // gratis. (Ett separat `setRole(Custom)` skulle falla på att
    // `parseColor` inte känner ordet "custom" — appen visade då sin färg och
    // previewn temats textfärg.)
    if (auto* lbl = dynamic_cast<glaze::Label*>(&w)) {
        if (call.method == "setColor") {
            gfx::Color färg{};
            if (parseColorLiteral(call.args, färg)) {
                lbl->setColor(färg);
                return true;
            }
            gfx::log("%s(%s): opak setColor-rad (ingen gfx::Color{…}-"
                     "literal med 0–4 kanaler 0–255, ritas inte)", origin,
                     w.name.c_str());
            return false;
        }
        // GLORIAN — `setHalo(std::optional<gfx::Color>)`, Buttons
        // `setColor`s form: EXAKT ett argument i `argValfriFärg`s tre former
        // (`gfx::Color{…}`, `std::nullopt`, `{}` = ingen gloria). En bar
        // `{255, 255, 255}` kompilerar inte till en optional och är OPAK —
        // etiketten lämnas orörd. Bredden är ett setter-fält (`haloWidth`).
        if (call.method == "setHalo") {
            const std::vector<std::string> a = delaArgument(call.args);
            std::optional<gfx::Color>      färg;
            if (a.size() != 1 || !argValfriFärg(a, 0, färg)) {
                gfx::log("%s(%s): opak setHalo-rad (ingen gfx::Color{…}, "
                         "std::nullopt eller {} — etiketten lämnas ORÖRD)",
                         origin, w.name.c_str());
                return false;
            }
            lbl->setHalo(färg);
            return true;
        }
    }

    // KNAPPARNAS TEXTFÄRG — `setColor(std::optional
    // <gfx::Color>)` på Button, Checkbox och Radio, trädnodens form: EXAKT ett
    // argument i `argValfriFärg`s tre former (`gfx::Color{…}`, `std::nullopt`,
    // `{}` = temat). En bar `{200, 0, 0}` kompilerar inte till en optional och
    // är OPAK — widgeten lämnas orörd. Inte ett setter-fält: Labels `color` är
    // ett annat begrepp med en annan värdeform (noten ovanför settertabellen).
    if (call.method == "setColor") {
        auto* knapp = dynamic_cast<glaze::Button*>(&w);
        auto* kryss = dynamic_cast<glaze::Checkbox*>(&w);
        auto* radio = dynamic_cast<glaze::Radio*>(&w);
        if (knapp || kryss || radio) {
            const std::vector<std::string> a = delaArgument(call.args);
            std::optional<gfx::Color>      färg;
            if (a.size() != 1 || !argValfriFärg(a, 0, färg)) {
                gfx::log("%s(%s): opak setColor-rad (ingen gfx::Color{…}, "
                         "std::nullopt eller {} — widgeten lämnas ORÖRD)",
                         origin, w.name.c_str());
                return false;
            }
            if (knapp)
                knapp->setColor(färg);
            else if (kryss)
                kryss->setColor(färg);
            else
                radio->setColor(färg);
            return true;
        }
    }

    // TRÄDET — kollektionsvokabulären med en STIG där listan har
    // ett index: `addNode(förälder, text[, bild[, färg[, celler]]])` ·
    // `setNode(stig, text[, bild[, färg[, celler]]])` · `setNodes(lista)`,
    // plus `removeNode(stig)`/
    // `moveNode(från, tillFörälder, index)`, utfällningen `setExpanded(stig,
    // på)` och släppregeln `setAllowDropInto(stig, på)`. Också
    // kryssen `setChecked(stig, på)` (två argument
    // här, som ListBoxens — settertabellen är typmedveten), valet
    // `setSelected(stig, på)`/`selectAll()`/`clearSelection()` och
    // textfärgen `setNodeColor(stig, färg)` — färgen `argValfriFärg`s tre
    // former, så `setNodeColor({0}, {200, 0, 0})` utan typ är OPAK (den
    // kompilerar inte). Flytten av flera,
    // `moveNodes(stigar, tillFörälder, index)` med stiglistan `argPathList`
    // (`{{0, 1}, {2}}` — `{0}` som lista kompilerar inte och är opak).
    // Cellerna: en strängliste-literal som femte argument till
    // `addNode`/`setNode` och `setCell(stig, kolumn, text)` — tabellens
    // `setCell` med en stig. KOLUMNERNA är ColumnPanels och tas av grenen
    // nedan, som tabellens. `multiSelect`/`showChecks` och sorteringen är
    // setter-fält.
    // ATOMISKT som resten av `applyCall`: exakt argumentantal, allt parsat
    // till temporärer, widgeten muterad först när allt lyckats — annars
    // logg + `false` ⇒ OPAK rad. En OGILTIG STIG (utanför trädet) är däremot
    // widgetens eget logg + no-op, precis som `setItem(99, …)`.
    if (auto* tv = dynamic_cast<glaze::TreeView*>(&w)) {
        const std::string& mm     = call.method;
        const bool         ärTräd =
            mm == "addNode" || mm == "setNode" || mm == "setNodes" ||
            mm == "removeNode" || mm == "moveNode" || mm == "moveNodes" ||
            mm == "setExpanded" || mm == "setAllowDropInto" ||
            mm == "setChecked" || mm == "setSelected" ||
            mm == "setNodeColor" || mm == "selectAll" ||
            mm == "clearSelection" || mm == "setCell";
        if (ärTräd) {
            std::vector<std::string>     a = delaArgument(call.args);
            glaze::TreePath              stig, till;
            std::vector<glaze::TreePath> stigar;
            glaze::TrText                text;
            glaze::ImageSource           bild{};
            std::optional<gfx::Color>    färg;
            std::vector<glaze::TreeNode> noder;
            std::vector<std::string>     celler;
            std::string                  cell;
            int                          index = 0;
            bool                         på    = false;
            bool                         ok    = false;

            if (mm == "addNode" || mm == "setNode")
                ok = a.size() >= 2 && a.size() <= 5 && argPath(a, 0, stig) &&
                     argText(a, 1, text) &&
                     (a.size() < 3 || argImg(a[2], bild)) &&
                     (a.size() < 4 || argValfriFärg(a, 3, färg)) &&
                     (a.size() < 5 ||
                      bld::parseStringList(trimma(a[4]), celler));
            else if (mm == "setCell")
                ok = a.size() == 3 && argPath(a, 0, stig) &&
                     argInt(a, 1, index) && argStr(a, 2, cell);
            else if (mm == "setNodes")
                ok = parseTreeLiteral(call.args, noder);
            else if (mm == "removeNode")
                ok = a.size() == 1 && argPath(a, 0, stig);
            else if (mm == "moveNode")
                ok = a.size() == 3 && argPath(a, 0, stig) &&
                     argPath(a, 1, till) && argInt(a, 2, index);
            else if (mm == "moveNodes")
                ok = a.size() == 3 && argPathList(a, 0, stigar) &&
                     argPath(a, 1, till) && argInt(a, 2, index);
            else if (mm == "setNodeColor")
                ok = a.size() == 2 && argPath(a, 0, stig) &&
                     argValfriFärg(a, 1, färg);
            else if (mm == "selectAll" || mm == "clearSelection")
                ok = trimma(call.args).empty();
            else   // setExpanded, setAllowDropInto, setChecked, setSelected
                ok = a.size() == 2 && argPath(a, 0, stig) &&
                     argFlagga(a, 1, på);

            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "trädet lämnas ORÖRT)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "addNode")
                tv->addNode(stig, std::move(text), bild, färg,
                            std::move(celler));
            else if (mm == "setNode")
                tv->setNode(stig, std::move(text), bild, färg,
                            std::move(celler));
            else if (mm == "setCell")
                tv->setCell(stig, index, std::move(cell));
            else if (mm == "setNodes")
                tv->setNodes(std::move(noder));
            else if (mm == "removeNode")
                tv->removeNode(stig);
            else if (mm == "moveNode")
                tv->moveNode(stig, till, index);
            else if (mm == "moveNodes")
                tv->moveNodes(stigar, till, index);
            else if (mm == "setExpanded")
                tv->setExpanded(stig, på);
            else if (mm == "setAllowDropInto")
                tv->setAllowDropInto(stig, på);
            else if (mm == "setChecked")
                tv->setChecked(stig, på);
            else if (mm == "setSelected")
                tv->setSelected(stig, på);
            else if (mm == "setNodeColor")
                tv->setNodeColor(stig, färg);
            else if (mm == "selectAll")
                tv->selectAll();
            else
                tv->clearSelection();
            return true;
        }
    }

    // KOLUMNERNA — tabellens (ListView, DataGrid) och
    // trädets, ur den delade basen `glaze::ColumnPanel`: kollektionsvokabulären
    // `addColumn`/`setColumn`/`setColumns` och bredden ensam
    // (`setColumnWidth`). ATOMISKT som resten av `applyCall`: exakt
    // argumentantal, allt parsat till temporärer, och listan muteras först när
    // allt lyckats — annars logg + `false` ⇒ OPAK rad. Ett index UTANFÖR
    // kolumnerna är däremot widgetens eget logg + no-op.
    if (auto* kp = dynamic_cast<glaze::ColumnPanel*>(&w)) {
        const std::string& mm       = call.method;
        const bool         ärKolumn =
            mm == "addColumn" || mm == "setColumn" || mm == "setColumns" ||
            mm == "setColumnWidth";
        if (ärKolumn) {
            std::vector<std::string>                a = delaArgument(call.args);
            int                                     index = 0;
            glaze::TrText                           rubrik;
            float                                   bredd = 0.0f;
            glaze::Align                            just  = glaze::Align::Left;
            std::optional<glaze::Align>             rubrikJust;
            std::vector<glaze::ColumnPanel::Column> kolumner;
            bool                                    ok = false;

            if (mm == "addColumn" || mm == "setColumn") {
                const std::size_t bas = mm == "setColumn" ? 1u : 0u;
                ok = a.size() >= bas + 1 && a.size() <= bas + 4 &&
                     (bas == 0 || argInt(a, 0, index)) &&
                     argText(a, bas, rubrik) &&
                     (a.size() <= bas + 1 || argKolumnBredd(a, bas + 1, bredd)) &&
                     (a.size() <= bas + 2 || argJustering(a, bas + 2, just)) &&
                     (a.size() <= bas + 3 ||
                      argRubrikJustering(a, bas + 3, rubrikJust));
            } else if (mm == "setColumns") {
                ok = parseColumnsLiteral(call.args, kolumner);
            } else {   // setColumnWidth
                ok = a.size() == 2 && argInt(a, 0, index) &&
                     argKolumnBredd(a, 1, bredd);
            }

            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "kolumnerna lämnas ORÖRDA)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "addColumn")
                kp->addColumn(std::move(rubrik), bredd, just, rubrikJust);
            else if (mm == "setColumn")
                kp->setColumn(index, std::move(rubrik), bredd, just,
                              rubrikJust);
            else if (mm == "setColumns")
                kp->setColumns(std::move(kolumner));
            else
                kp->setColumnWidth(index, bredd);
            return true;
        }
    }

    // TABELLEN — ListView och DataGrid delar raderna i
    // `glaze::Table` (kolumnerna tar grenen ovan), och kollektionsvokabulären
    // är densamma som överallt: `addRow`/`setRow`/`setRows`, plus cellen
    // (`setCell`) och sorteringsnyckeln (`setSortKey`) — och
    // radens textfärg: `setRowColor(index, färg)` och ett
    // valfritt tredje argument till `addRow`/`setRow`, avkodade av
    // `argValfriFärg` — och cellens: `setCellColor(rad, kolumn,
    // bakgrund, text)`, två valfria färger. ATOMISKT som resten av
    // `applyCall`: exakt argumentantal, allt parsat till temporärer, och
    // tabellen muteras först när allt lyckats — annars logg + `false` ⇒ OPAK
    // rad. Ett index UTANFÖR tabellen är däremot widgetens eget logg + no-op.
    if (auto* tb = dynamic_cast<glaze::Table*>(&w)) {
        const std::string& mm       = call.method;
        const bool         ärTabell =
            mm == "addRow" || mm == "setRow" || mm == "setRows" ||
            mm == "setCell" || mm == "setSortKey" || mm == "setRowColor" ||
            mm == "setCellColor";
        if (ärTabell) {
            std::vector<std::string>       a = delaArgument(call.args);
            int                            index = 0, kolumn = 0;
            std::vector<std::string>       celler;
            glaze::ImageSource             bild{};
            std::optional<gfx::Color>      färg, textFärg;
            std::vector<glaze::Table::Row> rader;
            std::string                    text;
            double                         nyckel = 0.0;
            bool                           ok     = false;

            if (mm == "addRow" || mm == "setRow") {
                const std::size_t bas = mm == "setRow" ? 1u : 0u;
                ok = a.size() >= bas + 1 && a.size() <= bas + 3 &&
                     (bas == 0 || argInt(a, 0, index)) &&
                     bld::parseStringList(trimma(a[bas]), celler) &&
                     (a.size() <= bas + 1 || argImg(a[bas + 1], bild)) &&
                     (a.size() <= bas + 2 || argValfriFärg(a, bas + 2, färg));
            } else if (mm == "setRowColor") {
                ok = a.size() == 2 && argInt(a, 0, index) &&
                     argValfriFärg(a, 1, färg);
            } else if (mm == "setCellColor") {
                ok = a.size() == 4 && argInt(a, 0, index) &&
                     argInt(a, 1, kolumn) && argValfriFärg(a, 2, färg) &&
                     argValfriFärg(a, 3, textFärg);
            } else if (mm == "setRows") {
                ok = parseTableRowsLiteral(call.args, rader);
            } else if (mm == "setCell") {
                ok = a.size() == 3 && argInt(a, 0, index) &&
                     argInt(a, 1, kolumn) && argStr(a, 2, text);
            } else {   // setSortKey
                ok = a.size() == 3 && argInt(a, 0, index) &&
                     argInt(a, 1, kolumn) && argDecimal(a, 2, nyckel);
            }

            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "tabellen lämnas ORÖRD)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "addRow")
                tb->addRow(std::move(celler), bild, färg);
            else if (mm == "setRow")
                tb->setRow(index, std::move(celler), bild, färg);
            else if (mm == "setRowColor")
                tb->setRowColor(index, färg);
            else if (mm == "setCellColor")
                tb->setCellColor(index, kolumn, färg, textFärg);
            else if (mm == "setRows")
                tb->setRows(std::move(rader));
            else if (mm == "setCell")
                tb->setCell(index, kolumn, std::move(text));
            else
                tb->setSortKey(index, kolumn, nyckel);
            return true;
        }
    }

    // LISTVYNS VAL — en rad för sig, och de två argumentlösa
    // massformerna — och KRYSSEN, `setChecked(rad,
    // på)`: index och flagga, som ListBoxens (settertabellen är typmedveten,
    // så raden når hit som en Call). `setSelectedIndex`/`setMultiSelect`/
    // `setShowChecks` är setter-FÄLT och når widgeten via `applyProp`.
    if (auto* lv = dynamic_cast<glaze::ListView*>(&w))
        if (const auto r = applyRowSelectionCall<true>(*lv, call, origin, w.name))
            return *r;

    // DATAGRIDDENS KOLUMNTYP och AKTUELLA CELL. Kolumntypen hör
    // till INDEXET (`setColumnEditor(1, glaze::DataGrid::Editor::Choice)`,
    // valen som strikt strängliste-literal) och cellen är två heltal.
    // Samma atomiska form som tabellgrenen: allt parsat till temporärer,
    // annars logg + `false` ⇒ OPAK rad; ett index utanför är widgetens
    // eget logg + no-op.
    if (auto* dg = dynamic_cast<glaze::DataGrid*>(&w)) {
        const std::string& mm = call.method;
        if (mm == "setColumnEditor" || mm == "setColumnChoices" ||
            mm == "setCurrentCell") {
            std::vector<std::string> a      = delaArgument(call.args);
            int                      kolumn = 0, rad = 0;
            glaze::DataGrid::Editor  typ    = glaze::DataGrid::Editor::Text;
            std::vector<std::string> val;
            bool                     ok = false;
            if (mm == "setColumnEditor")
                ok = a.size() == 2 && argInt(a, 0, kolumn) &&
                     argEditor(a, 1, typ);
            else if (mm == "setColumnChoices")
                ok = a.size() == 2 && argInt(a, 0, kolumn) &&
                     bld::parseStringList(trimma(a[1]), val);
            else
                ok = a.size() == 2 && argInt(a, 0, rad) &&
                     argInt(a, 1, kolumn);
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "datagriden lämnas ORÖRD)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "setColumnEditor")
                dg->setColumnEditor(kolumn, typ);
            else if (mm == "setColumnChoices")
                dg->setColumnChoices(kolumn, std::move(val));
            else
                dg->setCurrentCell(rad, kolumn);
            return true;
        }
        // RADVALET — listvyns ord och form, utan
        // kryss. `setSelectionMode` och `setMultiSelect` är setter-fält.
        if (const auto r = applyRowSelectionCall<false>(*dg, call, origin, w.name))
            return *r;
    }

    // FILVÄLJARFÄLTETS FILTER — kollektionsvokabulären:
    // `addFilter(namn, mönster)` · `setFilter(index, namn, mönster)` ·
    // `setFilters({{namn, mönster}, …})`. Samma atomiska form som tabellen:
    // allt parsat till temporärer, annars logg + `false` ⇒ OPAK rad; ett
    // index utanför är widgetens eget logg + no-op. Grenen står FÖRE
    // setter-fallbacken, som annars hade gjort `setFilter(…)` till ett
    // (okänt) fält `filter`.
    if (auto* fp = dynamic_cast<glaze::FilePicker*>(&w)) {
        const std::string& mm = call.method;
        if (mm == "addFilter" || mm == "setFilter" || mm == "setFilters") {
            std::vector<std::string>       a     = delaArgument(call.args);
            int                            index = 0;
            glaze::TrText                  namn;
            std::string                    mönster;
            std::vector<glaze::FileFilter> filter;
            bool                           ok = false;
            if (mm == "addFilter")
                ok = a.size() == 2 && argText(a, 0, namn) &&
                     argStr(a, 1, mönster);
            else if (mm == "setFilter")
                ok = a.size() == 3 && argInt(a, 0, index) &&
                     argText(a, 1, namn) && argStr(a, 2, mönster);
            else
                ok = parseFiltersLiteral(call.args, filter);
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "fältet lämnas ORÖRT)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "addFilter")
                fp->addFilter(std::move(namn), std::move(mönster));
            else if (mm == "setFilter")
                fp->setFilter(index, std::move(namn), std::move(mönster));
            else
                fp->setFilters(std::move(filter));
            return true;
        }
    }

    // DATUMVÄLJAREN — datumet och gränserna i den PLATTA formen,
    // tre heltal: `setDate(2026, 10, 8)` · `setMinDate(…)` · `setMaxDate(…)`.
    // ATOMISK som de andra: exakt tre STRIKTA heltal, annars logg + `false` ⇒
    // OPAK rad. Ett datum som inte finns (`setDate(2026, 2, 30)`) är däremot
    // en väl formad rad som WIDGETEN avvisar med sin egen logg — som ett index
    // utanför. Grenen står FÖRE setter-fallbacken, som annars hade skickat
    // "2026, 10, 8" till `setProp("date")` (ISO-formen) och fått en skräprad.
    // `setDate(std::chrono::year_month_day{…})` är kodens form och har ingen
    // gren: ett nästlat uttryck, och raden blir opak.
    if (auto* dp = dynamic_cast<glaze::DatePicker*>(&w)) {
        const std::string& mm = call.method;
        if (mm == "setDate" || mm == "setMinDate" || mm == "setMaxDate") {
            std::vector<std::string> a = delaArgument(call.args);
            int år = 0, månad = 0, dag = 0;
            if (a.size() != 3 || !argInt(a, 0, år) || !argInt(a, 1, månad) ||
                !argInt(a, 2, dag)) {
                gfx::log("%s(%s): opak %s-rad (tre heltal krävs, datumet "
                         "lämnas ORÖRT)", origin, w.name.c_str(), mm.c_str());
                return false;
            }
            if (mm == "setDate")
                dp->setDate(år, månad, dag);
            else if (mm == "setMinDate")
                dp->setMinDate(år, månad, dag);
            else
                dp->setMaxDate(år, månad, dag);
            return true;
        }
        // KLOCKSLAGET — `setTime(14, 30)`, två
        // STRIKTA heltal (datumets platta form); ett klockslag som inte finns
        // är widgetens logg + no-op. `setShowTime` är ett setter-fält.
        if (mm == "setTime") {
            std::vector<std::string> a = delaArgument(call.args);
            int timme = 0, minut = 0;
            if (a.size() != 2 || !argInt(a, 0, timme) || !argInt(a, 1, minut)) {
                gfx::log("%s(%s): opak setTime-rad (två heltal krävs, tiden "
                         "lämnas ORÖRD)", origin, w.name.c_str());
                return false;
            }
            dp->setTime(timme, minut);
            return true;
        }
    }

    // FLIKARNAS SIDOR — kollektionsvokabulären:
    // `addPage([text])` · `setPage(index, text)` · `setPages({text, …})`.
    // Texten är en literal eller `glaze::tr(N)`. Previewn bygger trädet FÖRE
    // satserna, så raderna står efter trädbygget (tab_control.h). ATOMISKT:
    // fel argument ⇒ logg + OPAK rad; ett index utanför är widgetens logg.
    if (auto* tc = dynamic_cast<glaze::TabControl*>(&w)) {
        const std::string& mm = call.method;
        if (mm == "addPage" || mm == "setPage" || mm == "setPages") {
            std::vector<std::string>   a = delaArgument(call.args);
            int                        index = 0;
            glaze::TrText              text;
            std::vector<glaze::TrText> texter;
            bool                       ok = false;
            if (mm == "addPage")
                ok = trimma(call.args).empty() ||
                     (a.size() == 1 && argText(a, 0, text));
            else if (mm == "setPage")
                ok = a.size() == 2 && argInt(a, 0, index) &&
                     argText(a, 1, text);
            else
                ok = parseTexter(call.args, texter);
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "flikarna lämnas ORÖRDA)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "addPage")
                tc->addPage(std::move(text));
            else if (mm == "setPage")
                tc->setPage(index, std::move(text));
            else
                tc->setPages(std::move(texter));
            return true;
        }
    }

    // STATUSRADENS FÄLT — kollektionsvokabulären:
    // `addPanel([text[, bredd[, fyll]]])` · `setPanel(index, text[, bredd[,
    // fyll]])` · `setPanels({…})` och textens egen `setPanelText(index,
    // text)`. ATOMISKT: fel argument ⇒ logg + OPAK rad; ett index utanför är
    // widgetens logg + no-op.
    if (auto* sb = dynamic_cast<glaze::StatusBar*>(&w)) {
        const std::string& mm = call.method;
        if (mm == "addPanel" || mm == "setPanel" || mm == "setPanels" ||
            mm == "setPanelText") {
            std::vector<std::string>        a = delaArgument(call.args);
            int                             index = 0;
            glaze::TrText                   text;
            float                           bredd = 0.0f;
            bool                            fyll  = false;
            std::vector<glaze::StatusPanel> fält;
            bool                            ok = false;
            if (mm == "addPanel" || mm == "setPanel") {
                const std::size_t bas = mm == "setPanel" ? 1u : 0u;
                ok = (mm == "addPanel" && trimma(call.args).empty()) ||
                     (a.size() >= bas + 1 && a.size() <= bas + 3 &&
                      (bas == 0 || argInt(a, 0, index)) &&
                      argText(a, bas, text) &&
                      (a.size() <= bas + 1 || argMått(a, bas + 1, bredd)) &&
                      (a.size() <= bas + 2 || argFlagga(a, bas + 2, fyll)));
            } else if (mm == "setPanelText") {
                ok = a.size() == 2 && argInt(a, 0, index) &&
                     argText(a, 1, text);
            } else {
                ok = parseStatusPanelsLiteral(call.args, fält);
            }
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "statusraden lämnas ORÖRD)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "addPanel")
                sb->addPanel(std::move(text), bredd, fyll);
            else if (mm == "setPanel")
                sb->setPanel(index, std::move(text), bredd, fyll);
            else if (mm == "setPanelText")
                sb->setPanelText(index, std::move(text));
            else
                sb->setPanels(std::move(fält));
            return true;
        }
    }

    // MARKERINGEN (0.1.2) — `selectAll()` och `setSelection(början, slut)` på
    // de fem som bär den: `TextInput`, `TextArea` och kompositerna som för den
    // vidare till sitt fält (`SpinBox`, `DatePicker`, `FilePicker`). Raden
    // UTFÖRS — markeringen sätts på previewns widget — men SYNS inte: ett fält
    // ritar sin markering bara med fokus, och previewn har inget. Det är
    // exakt vad den kompilerade raden gör i ett fönster som ännu inte fått
    // fokus, och raden är alltså känd och inte opak. ATOMISK som resten:
    // `selectAll` tar inga argument och `setSelection` exakt två strikta
    // heltal, annars logg + `false` ⇒ OPAK rad. (Listvyns `selectAll` — valet
    // av alla RADER — har sin egen gren ovan.)
    if (call.method == "selectAll" || call.method == "setSelection") {
        auto* ti = dynamic_cast<glaze::TextInput*>(&w);
        auto* ta = dynamic_cast<glaze::TextArea*>(&w);
        auto* sb = dynamic_cast<glaze::SpinBox*>(&w);
        auto* dp = dynamic_cast<glaze::DatePicker*>(&w);
        auto* fp = dynamic_cast<glaze::FilePicker*>(&w);
        if (ti || ta || sb || dp || fp) {
            const bool alla = call.method == "selectAll";
            int        b    = 0;
            int        e    = 0;
            bool       ok   = false;
            if (alla) {
                ok = trimma(call.args).empty();
            } else {
                const std::vector<std::string> a = delaArgument(call.args);
                ok = a.size() == 2 && argInt(a, 0, b) && argInt(a, 1, e);
            }
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "markeringen lämnas ORÖRD)", origin, w.name.c_str(),
                         call.method.c_str());
                return false;
            }
            // Samma två anrop på fem typer utan gemensam bas för dem.
            const auto utför = [&](auto* f) {
                if (alla)
                    f->selectAll();
                else
                    f->setSelection(b, e);
            };
            if (ti)
                utför(ti);
            else if (ta)
                utför(ta);
            else if (sb)
                utför(sb);
            else if (dp)
                utför(dp);
            else
                utför(fp);
            return true;
        }
    }

    // ÅNGRA och GÖR OM — `undo()` och `redo()` på de två fält
    // som bär historiken, `TextInput` och `TextArea` (kompositerna har inga
    // vidarebefordrade metoder — en skriven gräns). Raden UTFÖRS och är alltså
    // KÄND, men gör ingenting synligt: historiken är tom i previewn, precis som
    // i appens `initializeComponent` — samma form och skäl som `selectAll`.
    // GlazeBuilder emitterar den aldrig; den står där bara om någon skrivit den.
    // Argumenten måste vara tomma, annars logg + `false` ⇒ OPAK rad.
    if (call.method == "undo" || call.method == "redo") {
        auto* ti = dynamic_cast<glaze::TextInput*>(&w);
        auto* ta = dynamic_cast<glaze::TextArea*>(&w);
        if (ti || ta) {
            if (!trimma(call.args).empty()) {
                gfx::log("%s(%s): opak %s-rad (inga argument tas, fältet "
                         "lämnas ORÖRT)", origin, w.name.c_str(),
                         call.method.c_str());
                return false;
            }
            // Samma två anrop på två typer utan gemensam bas för dem.
            const bool ångra = call.method == "undo";
            const auto utför = [ångra](auto* f) {
                if (ångra)
                    f->undo();
                else
                    f->redo();
            };
            if (ti)
                utför(ti);
            else
                utför(ta);
            return true;
        }
    }

    // MARKDOWN-VYN — bildtabellen och ankaret:
    // `setImage(namn, bild)` med EXAKT två argument, namnet en strängliteral
    // och bilden `glaze::img("lista", "nyckel")` eller `{}` (tar bort posten),
    // och `scrollToAnchor(ankare)` med en strängliteral. Båda UTFÖRS — de
    // LAGRAR tillstånd, och ankaret väntar till previewns första ritning som i
    // appen. ATOMISKT: allt till temporärer, vyn muteras först när allt
    // lyckats, annars logg + `false` ⇒ OPAK rad. Dokumentet självt är
    // setter-fältet `text`.
    if (auto* md = dynamic_cast<glaze::MarkdownView*>(&w)) {
        if (call.method == "setImage" || call.method == "scrollToAnchor") {
            const std::vector<std::string> a = delaArgument(call.args);
            std::string                    namn;
            glaze::ImageSource             bild;
            const bool ok =
                call.method == "setImage"
                    ? a.size() == 2 && argStr(a, 0, namn) && argImg(a[1], bild)
                    : a.size() == 1 && argStr(a, 0, namn);
            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "vyn lämnas ORÖRD)", origin, w.name.c_str(),
                         call.method.c_str());
                return false;
            }
            if (call.method == "setImage")
                md->setImage(namn, bild);
            else
                md->scrollToAnchor(namn);
            return true;
        }
    }

    // KLICKA — `ok.performClick();` på en `Button` är en
    // KÄND sats men UTFÖRS ALDRIG: den kör användarkod (`onClick`) och stänger
    // fönstret med knappens resultat, och previewn kör ingen av delarna
    // (prejudikatet är systemfältsikonens `show` och timerns `start`). Raden
    // står där bara om någon skrivit den — GlazeBuilder emitterar den aldrig.
    // Med argument ⇒ logg + `false` ⇒ OPAK rad. En `ToolButton` har samma
    // handling (verktygsfältets överflöde klickar den så).
    if (call.method == "performClick" &&
        (dynamic_cast<glaze::Button*>(&w) ||
         dynamic_cast<glaze::ToolButton*>(&w))) {
        if (!trimma(call.args).empty()) {
            gfx::log("%s(%s): opak performClick-rad (inga argument tas)",
                     origin, w.name.c_str());
            return false;
        }
        return true;
    }

    // RITYTAN (regel 8) — kameran, lagren och FORMLISTAN
    // i den platta formen: `setCenter(x, y)` · `setZoomRange(min, max)` ·
    // `setMapScale(nämnare[, meter per enhet])` · `setBackground(gfx::Color{…})`
    // · `zoomToRect(rekt[, mått])` · `setCamera(glaze::Camera{…})`, lagrens
    // `addLayer`/`setLayer`/`setLayers`/`removeLayer`, deras ordning
    // `moveLayer(från, till)` (tillstånd, så den UTFÖRS) och
    // egenskaper, och formernas `addShape`/`setShape`/`setShapes`/
    // `removeShape` med eller utan lagrets index. ATOMISKT som de andra:
    // exakt argumentantal, allt — också en hel form med sina stilar — till
    // temporärer, och ritytan muteras först när allt lyckats; annars logg +
    // `false` ⇒ OPAK rad. Ett index utanför är ritytans eget logg + no-op.
    // Grenen står FÖRE setter-fallbacken, som annars hade skickat "674000,
    // 6580000" till `setProp("center")` utan att pröva formen strikt.
    // `easeTo` utförs MEDVETET inte: en glidning är ett förlopp, och
    // ritbordet visar tillståndet — raden bevaras opakt.
    //
    // I RYMDEN: `setCamera` tar också 3D-kameran (två
    // överlagringar — `parseKameraArg` väljer som C++), `setLighting`,
    // lagrens `setLayerPlacement`/`setLayerRenderState`/`setLayerViews`,
    // del-vyerna i kollektionsvokabulären (`addView`/`setView`/`setViews`/
    // `removeView`), orbitens spann (`setElevationRange`/`setDistanceRange`),
    // `setTicking` och inramningen `zoomToBox` — `zoomToRect`s tvilling, som
    // väntar på ytan som den. Formerna i rymden går genom samma `parseForm`.
    // `orbitBy`/`panBy`/`dollyBy`, `easeTo(Camera3D)` och `renderToBitmap` är
    // HANDLINGAR och inget tillstånd: de utförs inte och bevaras opakt, som
    // `easeTo` i 2D. `setMultisample`/`setDepth`/`setOrbitTumble` är
    // setter-fält och når ritytan genom `applyProp`. MÅLARENS PLATS
    // (`setLayerPainter(lager, på)`) är lagrets tillstånd och utförs — det
    // `onPaintLayer` ritar där kör previewn aldrig, som `onPaint`. Lagrets
    // snap (`setLayerSnap(lager, på)`) är också tillstånd och
    // utförs — bara träffprovet läser den.
    if (auto* cv = dynamic_cast<glaze::Canvas*>(&w)) {
        const std::string& mm = call.method;
        static const char* const kMetoder[] = {
            "setCenter",       "setZoomRange",      "setMapScale",
            "setBackground",   "zoomToRect",        "setCamera",
            "addLayer",        "setLayer",          "setLayers",
            "removeLayer",     "setLayerVisible",   "setLayerOpacity",
            "setLayerZoomRange", "setLayerCollision", "setLayerDrawOrder",
            "addShape",        "setShape",          "setShapes",
            "removeShape",     "setLighting",       "setLayerPlacement",
            "setLayerRenderState", "setLayerViews", "addView",
            "setView",         "setViews",          "removeView",
            "setElevationRange", "setDistanceRange", "setTicking",
            "zoomToBox",       "moveLayer",         "setChildAnchor",
            "clearChildAnchor", "setLayerPainter",  "setLayerSnap",
            // BINDNINGARNA — en rad per bindning, PLATTA.
            "setBinding",      "removeBinding",     "clearBindings",
            "setSourceInvert", "clearSourceInvert", "setActionInvert",
            "clearActionInvert"};
        bool känd = false;
        for (const char* k : kMetoder)
            känd = känd || mm == k;
        if (känd) {
            const std::vector<std::string> a = delaArgument(call.args);
            const std::size_t              n = a.size();
            double                         x = 0.0, y = 0.0;
            float                          f = 0.0f, g = 0.0f;
            int                            lager = 0, index = 0;
            bool                           på    = false;
            gfx::Color                     färg{};
            glaze::Rect                    rekt{};
            glaze::Length                  luft{};
            glaze::Camera                  kamera{};
            glaze::Camera3D                kamera3{};
            KameraForm                     kameraForm = KameraForm::Ingen;
            glaze::TrText                  namn;
            std::vector<glaze::TrText>     namnen;
            glaze::DrawOrder               ordning = glaze::DrawOrder::Strict;
            glaze::Shape                   form;
            std::vector<glaze::Shape>      former;
            glaze::Lighting                ljus{};
            glaze::LayerPlacement          placering = glaze::LayerPlacement::Auto;
            glaze::RenderState             läge{};
            std::vector<int>               vyNummer;
            glaze::View                    vy{};
            std::vector<glaze::View>       vyer;
            glaze::Box3                    låda{};
            glaze::Widget*                 barn = nullptr;
            glaze::Point                   punkt{};
            glaze::TextAnchor              ankare = glaze::TextAnchor::Center;
            std::string                    bindläge;
            glaze::InputSource             källa{};
            glaze::InputAction             handling{};
            glaze::InputDirection          riktning = glaze::InputDirection::Normal;
            glaze::InputDevice             enhet{};
            std::uint32_t                  mods = 0;
            bool                           ok = false;

            // DE FÖRANKRADE BARNEN: barnet är en IDENTIFIERARE bland
            // ritytans barn (`argBarn` — satserna står efter trädbygget),
            // punkten en klammerlista och hörnet `glaze::TextAnchor::`.
            // BINDNINGARNA: läget en strängliteral, källan,
            // handlingen, riktningen och enheten `glaze::Input…::`, och
            // modifierarna `argMods` — 0 eller `gfx::kModX | …`.
            if (mm == "setBinding")
                ok = n >= 4 && n <= 6 && argStr(a, 0, bindläge) &&
                     parseKälla(a[1], källa) && argMods(a[2], mods) &&
                     parseHandling(a[3], handling) &&
                     (n < 5 || parseRiktning(a[4], riktning)) &&
                     (n < 6 || flyttal(a[5], f));
            else if (mm == "removeBinding")
                ok = n == 3 && argStr(a, 0, bindläge) && parseKälla(a[1], källa) &&
                     argMods(a[2], mods);
            else if (mm == "clearBindings")
                ok = n == 0;
            else if (mm == "setSourceInvert")
                ok = n == 2 && parseKälla(a[0], källa) && argFlagga(a, 1, på);
            else if (mm == "clearSourceInvert")
                ok = n == 1 && parseKälla(a[0], källa);
            else if (mm == "setActionInvert")
                ok = n == 3 && parseEnhet(a[0], enhet) &&
                     parseHandling(a[1], handling) && argFlagga(a, 2, på);
            else if (mm == "clearActionInvert")
                ok = n == 2 && parseEnhet(a[0], enhet) &&
                     parseHandling(a[1], handling);
            else if (mm == "setChildAnchor")
                ok = (n == 2 || n == 3) && argBarn(*cv, a, 0, barn) &&
                     parsePunkt(a[1], punkt) &&
                     (n < 3 || parseAnkare(a[2], ankare));
            else if (mm == "clearChildAnchor")
                ok = n == 1 && argBarn(*cv, a, 0, barn);
            else if (mm == "setCenter" || mm == "setZoomRange")
                ok = n == 2 && decimalt(a[0], x) && decimalt(a[1], y);
            else if (mm == "setMapScale")
                ok = (n == 1 || n == 2) && decimalt(a[0], x) &&
                     (n < 2 || decimalt(a[1], y));
            else if (mm == "setBackground")
                ok = n == 1 && parseColorLiteral(a[0], färg);
            else if (mm == "zoomToRect")
                ok = (n == 1 || n == 2) && parseRekt(a[0], rekt) &&
                     (n < 2 || parseMått(a[1], luft));
            else if (mm == "setCamera") {
                if (n == 1)
                    kameraForm = parseKameraArg(a[0], kamera, kamera3);
                ok = kameraForm != KameraForm::Ingen;
            } else if (mm == "setLighting")
                ok = n == 1 && parseLjus(a[0], ljus);
            else if (mm == "setLayerPlacement")
                ok = n == 2 && argInt(a, 0, lager) &&
                     enumVärde(a[1], {"glaze::LayerPlacement::"},
                               {std::pair{"Auto", glaze::LayerPlacement::Auto},
                                std::pair{"Ground", glaze::LayerPlacement::Ground},
                                std::pair{"World", glaze::LayerPlacement::World},
                                std::pair{"Terrain",
                                          glaze::LayerPlacement::Terrain}},
                               placering);
            else if (mm == "setLayerRenderState")
                ok = n == 2 && argInt(a, 0, lager) && parseRenderläge(a[1], läge);
            else if (mm == "setLayerViews")
                ok = n == 2 && argInt(a, 0, lager) &&
                     parseHeltalLista(a[1], vyNummer);
            else if (mm == "addView")
                ok = n == 1 && parseVy(a[0], vy);
            else if (mm == "setView")
                ok = n == 2 && argInt(a, 0, index) && parseVy(a[1], vy);
            else if (mm == "setViews")
                ok = n == 1 && parseVyer(a[0], vyer);
            else if (mm == "removeView")
                ok = n == 1 && argInt(a, 0, index);
            else if (mm == "setElevationRange")
                ok = n == 2 && flyttal(a[0], f) && flyttal(a[1], g);
            else if (mm == "setDistanceRange")
                ok = n == 2 && decimalt(a[0], x) && decimalt(a[1], y);
            else if (mm == "setTicking")
                ok = n == 1 && argFlagga(a, 0, på);
            else if (mm == "zoomToBox")
                ok = (n == 1 || n == 2) && parseLåda(a[0], låda) &&
                     (n < 2 || parseMått(a[1], luft));
            else if (mm == "addLayer")
                ok = n == 1 && argText(a, 0, namn);
            else if (mm == "setLayer")
                ok = n == 2 && argInt(a, 0, lager) && argText(a, 1, namn);
            else if (mm == "setLayers")
                ok = n == 1 && parseTexter(a[0], namnen);
            else if (mm == "removeLayer")
                ok = n == 1 && argInt(a, 0, lager);
            else if (mm == "moveLayer")
                ok = n == 2 && argInt(a, 0, lager) && argInt(a, 1, index);
            else if (mm == "setLayerVisible" || mm == "setLayerCollision" ||
                     mm == "setLayerPainter" || mm == "setLayerSnap")
                ok = n == 2 && argInt(a, 0, lager) && argFlagga(a, 1, på);
            else if (mm == "setLayerOpacity")
                ok = n == 2 && argInt(a, 0, lager) && flyttal(a[1], f);
            else if (mm == "setLayerZoomRange")
                ok = n == 3 && argInt(a, 0, lager) && decimalt(a[1], x) &&
                     decimalt(a[2], y);
            else if (mm == "setLayerDrawOrder")
                ok = n == 2 && argInt(a, 0, lager) &&
                     enumVärde(a[1], {"glaze::DrawOrder::"},
                               {std::pair{"Strict", glaze::DrawOrder::Strict},
                                std::pair{"ByKind", glaze::DrawOrder::ByKind}},
                               ordning);
            else if (mm == "addShape")
                ok = (n == 1 && parseForm(a[0], form)) ||
                     (n == 2 && argInt(a, 0, lager) && parseForm(a[1], form));
            else if (mm == "setShape")
                ok = (n == 2 && argInt(a, 0, index) && parseForm(a[1], form)) ||
                     (n == 3 && argInt(a, 0, lager) && argInt(a, 1, index) &&
                      parseForm(a[2], form));
            else if (mm == "setShapes")
                ok = (n == 1 && parseFormer(a[0], former)) ||
                     (n == 2 && argInt(a, 0, lager) && parseFormer(a[1], former));
            else   // removeShape
                ok = (n == 1 && argInt(a, 0, index)) ||
                     (n == 2 && argInt(a, 0, lager) && argInt(a, 1, index));

            if (!ok) {
                gfx::log("%s(%s): opak %s-rad (argumenten avkodas inte, "
                         "ritytan lämnas ORÖRD)", origin, w.name.c_str(),
                         mm.c_str());
                return false;
            }
            if (mm == "setBinding")
                cv->setBinding(bindläge, källa, mods, handling, riktning,
                               n == 6 ? f : 1.0f);
            else if (mm == "removeBinding")
                cv->removeBinding(bindläge, källa, mods);
            else if (mm == "clearBindings")
                cv->clearBindings();
            else if (mm == "setSourceInvert")
                cv->setSourceInvert(källa, på);
            else if (mm == "clearSourceInvert")
                cv->clearSourceInvert(källa);
            else if (mm == "setActionInvert")
                cv->setActionInvert(enhet, handling, på);
            else if (mm == "clearActionInvert")
                cv->clearActionInvert(enhet, handling);
            else if (mm == "setChildAnchor")
                cv->setChildAnchor(*barn, punkt, ankare);
            else if (mm == "clearChildAnchor")
                cv->clearChildAnchor(*barn);
            else if (mm == "setCenter")
                cv->setCenter(x, y);
            else if (mm == "setZoomRange")
                cv->setZoomRange(x, y);
            else if (mm == "setMapScale")
                cv->setMapScale(x, n == 2 ? y : 1.0);
            else if (mm == "setBackground")
                cv->setBackground(färg);
            else if (mm == "zoomToRect")
                cv->zoomToRect(rekt, luft);
            else if (mm == "setCamera") {
                if (kameraForm == KameraForm::Rymd)
                    cv->setCamera(kamera3);
                else
                    cv->setCamera(kamera);
            } else if (mm == "setLighting")
                cv->setLighting(ljus);
            else if (mm == "setLayerPlacement")
                cv->setLayerPlacement(lager, placering);
            else if (mm == "setLayerRenderState")
                cv->setLayerRenderState(lager, läge);
            else if (mm == "setLayerViews")
                cv->setLayerViews(lager, std::move(vyNummer));
            else if (mm == "addView")
                cv->addView(std::move(vy));
            else if (mm == "setView")
                cv->setView(index, std::move(vy));
            else if (mm == "setViews")
                cv->setViews(std::move(vyer));
            else if (mm == "removeView")
                cv->removeView(index);
            else if (mm == "setElevationRange")
                cv->setElevationRange(f, g);
            else if (mm == "setDistanceRange")
                cv->setDistanceRange(x, y);
            else if (mm == "setTicking")
                cv->setTicking(på);
            else if (mm == "zoomToBox")
                cv->zoomToBox(låda, luft);
            else if (mm == "addLayer")
                cv->addLayer(std::move(namn));
            else if (mm == "setLayer")
                cv->setLayer(lager, std::move(namn));
            else if (mm == "setLayers")
                cv->setLayers(std::move(namnen));
            else if (mm == "removeLayer")
                cv->removeLayer(lager);
            else if (mm == "moveLayer")
                cv->moveLayer(lager, index);
            else if (mm == "setLayerVisible")
                cv->setLayerVisible(lager, på);
            else if (mm == "setLayerCollision")
                cv->setLayerCollision(lager, på);
            else if (mm == "setLayerPainter")
                cv->setLayerPainter(lager, på);
            else if (mm == "setLayerSnap")
                cv->setLayerSnap(lager, på);
            else if (mm == "setLayerOpacity")
                cv->setLayerOpacity(lager, f);
            else if (mm == "setLayerZoomRange")
                cv->setLayerZoomRange(lager, x, y);
            else if (mm == "setLayerDrawOrder")
                cv->setLayerDrawOrder(lager, ordning);
            else if (mm == "addShape") {
                if (n == 1)
                    cv->addShape(std::move(form));
                else
                    cv->addShape(lager, std::move(form));
            } else if (mm == "setShape") {
                if (n == 2)
                    cv->setShape(index, std::move(form));
                else
                    cv->setShape(lager, index, std::move(form));
            } else if (mm == "setShapes") {
                if (n == 1)
                    cv->setShapes(std::move(former));
                else
                    cv->setShapes(lager, std::move(former));
            } else {
                if (n == 1)
                    cv->removeShape(index);
                else
                    cv->removeShape(lager, index);
            }
            return true;
        }
    }

    // SETTER-FALLBACK. Reader/writer normaliserar
    // `setText("…")` ↔ `text` bara för `glaze::`-TYPER, eftersom ett eget
    // publikt `text`-fält på en användarwidget inte får emitteras som ett
    // setter-anrop den saknar. Men en användarwidget som ÄRVER en
    // Glaze-widget —
    //
    //     class MyPanel : public glaze::Panel {};
    //     settings.setBorder(true);      // helt korrekt C++
    //
    // — faller annars hit och blir en OPAK rad: den kompilerar, den round-trippar
    // ordagrant, men previewn tillämpar den ALDRIG. Det är ett TYST
    // WYSIWYG-brott, och tyst är det värsta utfallet av de tre.
    //
    // Här är det ofarligt att vara generös: det här är PREVIEW-vägen, inte
    // emissionen. Träffar `set<Fält>` en nyckel widgetens egen `setProp`
    // känner igen är det rätt sak att göra; känner den inte igen den faller
    // vi vidare till den opaka raden. Ingen emission
    // påverkas, så round-trip är oförändrad.
    std::string fält;
    if (setterField(call.method, fält)) {
        // ITEMS går ALLTID den typade vägen (se applyProp): en
        // användarwidget som ärver `glaze::Dropdown`/`glaze::ListBox`
        // emitteras som ett CALL (typvillkoret i writern), och utan den
        // här grenen hade den fallit på setProp-strängen och fått en
        // skräprad — exakt det tysta WYSIWYG-brott kommentaren ovan handlar
        // om, bara i listform.
        if (fält == "items" &&
            (dynamic_cast<glaze::Dropdown*>(&w) ||
             dynamic_cast<glaze::ListBox*>(&w))) {
            applyProp(w, Prop{fält, call.args}, origin);
            return true;
        }
        // FÄRGEN likaså: en användarwidget som ärver färgväljaren
        // emitteras som ett CALL, och `gfx::Color{…}`-formen har bara den
        // typade grenen i `applyProp` — `setProp` tar textformen.
        if (fält == "color" && dynamic_cast<glaze::ColorPicker*>(&w)) {
            applyProp(w, Prop{fält, call.args}, origin);
            return true;
        }
        // BREDDGRUPPEN likaså: en egen Label-subklass
        // emitteras som ett CALL, och värdenormaliseringen nedan hade gjort
        // identifieraren i `minEtikett.setWidthGroup(grupp);` till gruppnamnet.
        // Bara en strängliteral; annars opakt och etiketten orörd.
        if (fält == "widthGroup" && dynamic_cast<glaze::Label*>(&w)) {
            std::string grupp;
            if (!parseCppString(call.args, grupp)) {
                gfx::log("%s(%s): opakt setWidthGroup-anrop (ingen "
                         "strängliteral, utförs inte)", origin, w.name.c_str());
                return false;
            }
            applyProp(w, Prop{fält, call.args}, origin);
            return true;
        }
        // TEXTSTORLEKEN likaså: en egen subklass av de fyra
        // emitteras som ett CALL, och måttet har bara den typade grenen —
        // värdenormaliseringen nedan hade lämnat `glaze::mm(3)` åt `setProp`,
        // som ingen textform har för det.
        if (fält == "textSize" && harTextstorlek(w)) {
            glaze::Length l;
            if (!parseMått(call.args, l)) {
                gfx::log("%s(%s): opakt setTextSize-anrop (inget mått, utförs "
                         "inte)", origin, w.name.c_str());
                return false;
            }
            sättTextstorlek(w, l);
            return true;
        }
        // TEXTFÄLTEN: `minEtikett.setText(glaze::tr(1001));` på en
        // egen Label-subklass kom hit och blev TEXTEN "glaze::tr(1001)" —
        // värdenormaliseringen nedan känner ingen tr-form. Textfältets EN
        // regel (`applyTextField`): tokenet, literalen, annars opakt.
        if (ärTextfält(fält))
            return applyTextField(w, fält, call.args, origin);
        // Samma värdenormalisering som `applyProp` — strängliteral
        // avciteras, enum blir sista segmentet gement, tal/bool står som de
        // är. Annars hade `setText("Hej")` satt texten till `"Hej"` MED
        // citattecken i previewn.
        if (w.setProp(fält, nyckeltext(call.args)))
            return true;
    }

    // Okänd metod = OPAK rad (regel 4:s arvtagare): loggas, ignoreras i
    // previewn, round-trippas ordagrant vid spara. Aldrig fail-safe.
    gfx::log("%s(%s): opakt metodanrop '%s' (utförs inte)", origin,
             w.name.c_str(), call.method.c_str());
    return false;
}

bool parseLength(const std::string& v, glaze::Length& ut)
{
    return parseMått(v, ut);
}

// apply_props_internal.h — parsrarna för grenarna i andra källfiler.
std::vector<std::string> detail::callArgs(const std::string& args)
{
    std::vector<std::string> a = delaArgument(args);
    for (std::string& s : a)
        s = trimma(s);
    return a;
}

bool detail::parseColor(const std::string& v, gfx::Color& ut)
{
    return parseColorLiteral(v, ut);
}

bool applyWindowCall(glaze::AppWindow& w, const Call& call,
                     const char* origin)
{
    // TVÅ HELTAL — storleks-sättarna. Argumenten lagras ordagrant, så
    // texten är precis den som står i filen.
    int a = 0, b = 0;
    if (parseTwoInts(call.args, a, b)) {
        const std::string& mm = call.method;
        if (mm == "setClientSize")  { w.setClientSize(a, b);  return true; }
        if (mm == "setContentSize") { w.setContentSize(a, b); return true; }
        if (mm == "setMinimumSize") { w.setMinimumSize(a, b); return true; }
        if (mm == "setMaximumSize") { w.setMaximumSize(a, b); return true; }
        // GRÄNSERNA OCH DEN LÅSTA STORLEKEN I INNEHÅLLSMÅTT — samma
        // två heltal. glaze-render öppnar sitt offscreen-fönster genom
        // `showOwned`, så bilden får innehållsytan och chromet räknas på.
        if (mm == "setMinimumContentSize") {
            w.setMinimumContentSize(a, b);
            return true;
        }
        if (mm == "setMaximumContentSize") {
            w.setMaximumContentSize(a, b);
            return true;
        }
        if (mm == "setFixedContentSize") {
            w.setFixedContentSize(a, b);
            return true;
        }
        // STARTPLATSEN — lagras och betyder `Manual`.
        // Previewns fönster visas aldrig, så platsen syns inte; den finns i
        // `geometry()` och `startPosition()`.
        if (mm == "setLocation")    { w.setLocation(a, b);    return true; }
    }

    // EN BOOL — chrome- och beteende-attributen.
    bool flag = false;
    if (argBool(call.args, flag)) {
        const std::string& mm = call.method;
        if (mm == "setControlBox")  { w.setControlBox(flag);  return true; }
        if (mm == "setMinimizeBox") { w.setMinimizeBox(flag); return true; }
        if (mm == "setMaximizeBox") { w.setMaximizeBox(flag); return true; }
        if (mm == "setModal")       { w.setModal(flag);       return true; }
        if (mm == "setPopup")       { w.setPopup(flag);       return true; }
        // POPUPEN SOM TAR TANGENTBORDET — ett attribut som
        // lagras före show; previewns fönster tar aldrig emot tangenter.
        if (mm == "setPopupTakesKeys") {
            w.setPopupTakesKeys(flag);
            return true;
        }
        if (mm == "setTransparent") { w.setTransparent(flag); return true; }
        // ALLTID ÖVERST: grammatiken tar redan formen —
        // raden gör bara att verktygsvägen UTFÖR den i stället
        // för att logga den som okänd.
        if (mm == "setTopmost")     { w.setTopmost(flag);     return true; }
        // AKTIVITETSFÄLTET — samma sak: ett attribut som
        // lagras före show, och previewns fönster visas aldrig med det.
        if (mm == "setShowInTaskbar") { w.setShowInTaskbar(flag); return true; }
        // VÄNTEPEKAREN — lagras; previewn har ingen mus.
        if (mm == "setUseWaitCursor") { w.setUseWaitCursor(flag); return true; }
    }

    // ETT ENUM-VÄRDE — ramen, startplatsen och
    // startläget, var och en med en STRIKT tabell. Både den kvalificerade
    // formen (`gfx::Chrome::None`) och den bara (`Chrome::None`, `None` — som
    // kompilerar efter `using enum`) godtas; ett okänt namn är en opak rad.
    // `setChrome` gör previewn SANN: glaze-render ritar ett ramlöst fönster
    // utan namnlist. Läget och platsen lagras och syns i getterna —
    // offscreen-fönstret får dem aldrig.
    if (call.method == "setChrome") {
        gfx::Chrome c{};
        if (enumVärde(call.args, {"gfx::Chrome::", "Chrome::", ""},
                      {{"Native", gfx::Chrome::Native},
                       {"Custom", gfx::Chrome::Custom},
                       {"None", gfx::Chrome::None}},
                      c)) {
            w.setChrome(c);
            return true;
        }
    }
    if (call.method == "setStartPosition") {
        glaze::StartPosition sp{};
        if (enumVärde(call.args,
                      {"glaze::StartPosition::", "StartPosition::", ""},
                      {{"Default", glaze::StartPosition::Default},
                       {"CenterScreen", glaze::StartPosition::CenterScreen},
                       {"CenterOwner", glaze::StartPosition::CenterOwner},
                       {"Manual", glaze::StartPosition::Manual}},
                      sp)) {
            w.setStartPosition(sp);
            return true;
        }
    }
    // FÖNSTRET FÖLJER INNEHÅLLET — samma strikta
    // tabell. Previewn UTFÖR attributet: glaze-render öppnar sitt offscreen-
    // fönster genom `showOwned`, som anpassar efter init, så bilden får den
    // anpassade storleken (builderns canvas tar modellens).
    if (call.method == "setSizeToContent") {
        glaze::SizeToContent st{};
        if (enumVärde(call.args,
                      {"glaze::SizeToContent::", "SizeToContent::", ""},
                      {{"Manual", glaze::SizeToContent::Manual},
                       {"Width", glaze::SizeToContent::Width},
                       {"Height", glaze::SizeToContent::Height},
                       {"WidthAndHeight", glaze::SizeToContent::WidthAndHeight}},
                      st)) {
            w.setSizeToContent(st);
            return true;
        }
    }
    // LÅS ELLER GOLV — attributets läge, samma strikta
    // tabell och tre prefixformer. `GrowOnly` gör innehållet till golvet; i
    // glaze-render är storleken då max(begäran, innehållet).
    if (call.method == "setSizeToContentMode") {
        glaze::SizeToContentMode sm{};
        if (enumVärde(call.args,
                      {"glaze::SizeToContentMode::", "SizeToContentMode::", ""},
                      {{"GrowAndShrink", glaze::SizeToContentMode::GrowAndShrink},
                       {"GrowOnly", glaze::SizeToContentMode::GrowOnly}},
                      sm)) {
            w.setSizeToContentMode(sm);
            return true;
        }
    }
    if (call.method == "setWindowState") {
        gfx::WindowState ws{};
        if (enumVärde(call.args, {"gfx::WindowState::", "WindowState::", ""},
                      {{"Normal", gfx::WindowState::Normal},
                       {"Minimized", gfx::WindowState::Minimized},
                       {"Maximized", gfx::WindowState::Maximized}},
                      ws)) {
            w.setWindowState(ws);
            return true;
        }
    }

    // RULLNINGENS RIKTNING I FÖNSTRET — på fönstrets
    // rot: `setWheelInvert(glaze::InputDevice::Touchpad, false, true);`.
    if (call.method == "setWheelInvert" || call.method == "clearWheelInvert") {
        const std::vector<std::string> a = delaArgument(call.args);
        glaze::InputDevice             enhet{};
        bool                           lodrätt = false, sidled = false;
        if (call.method == "clearWheelInvert" && a.size() == 1 &&
            parseEnhet(a[0], enhet)) {
            w.clearWheelInvert(enhet);
            return true;
        }
        if (call.method == "setWheelInvert" && a.size() == 3 &&
            parseEnhet(a[0], enhet) && argFlagga(a, 1, lodrätt) &&
            argFlagga(a, 2, sidled)) {
            w.setWheelInvert(enhet, lodrätt, sidled);
            return true;
        }
    }

    // EN MEDLEM SOM ARGUMENT — fönstrets standard- och
    // avbrytknapp: `setAcceptButton(okBtn);`. Identifieraren slås upp med
    // `find<glaze::Button>` i previewträdet (namnet är medlemsnamnet, satt
    // av trädbygget), och satsen står i pass 2, efter bygget — dockningens
    // `dockPanel(sok, …)` är prejudikatet. `nullptr` nollställer. Ett namn
    // som inte är en `Button` i trädet (en egen knappsubklass är en stand-in,
    // 🐛 60) och allt annat är OPAKT: loggat, orört, aldrig en gissning.
    if (call.method == "setAcceptButton" || call.method == "setCancelButton") {
        const bool        accept = call.method == "setAcceptButton";
        const std::string t      = trimma(call.args);
        if (t == "nullptr") {
            if (accept)
                w.setAcceptButton(nullptr);
            else
                w.setCancelButton(nullptr);
            return true;
        }
        if (ärIdentifierare(t))
            if (glaze::Button* b = w.find<glaze::Button>(t)) {
                if (accept)
                    w.setAcceptButton(*b);
                else
                    w.setCancelButton(*b);
                return true;
            }
        gfx::log("%s: opakt fönsteranrop '%s(%s)' — ingen Button med det "
                 "namnet i trädet (utförs inte)",
                 origin, call.method.c_str(), call.args.c_str());
        return false;
    }

    // EN STRÄNG — eller en ÖVERSÄTTNING. Tokenet
    // sätts, inte den upplösta strängen: `AppWindow::setTitle` tar en
    // `TrText`, och ett språkbyte trycker om titeln av sig självt. Allt annat
    // — en konstant i tr-anropet, ett uttryck — läser readern sedan 🐛 58 som
    // en opak sats i stället för fail-safe, och den utförs inte: fönstret får
    // ingen titel hellre än uttrycket som titel (textfältens regel).
    if (call.method == "setTitle") {
        std::string         text;
        std::uint32_t       trId = 0;
        const TextValueKind k    = classifyTextValue(call.args, text, trId);
        if (k == TextValueKind::TrId) {
            w.setTitle(glaze::tr(trId));
            return true;
        }
        if (k == TextValueKind::Literal) {
            w.setTitle(text);
            return true;
        }
        gfx::log("%s: setTitle(%s) är varken en strängliteral eller "
                 "glaze::tr(<tal>) — titeln är opak och sätts inte%s",
                 origin, call.args.c_str(),
                 k == TextValueKind::TrExpr
                     ? " (verktygen läser bara NUMRET, glaze::tr(1042))"
                     : "");
        return false;
    }

    // Okänt fönsteranrop = OPAKT (regel 4), aldrig fail-safe.
    gfx::log("%s: opakt fönsteranrop '%s' (utförs inte)", origin,
             call.method.c_str());
    return false;
}

void applyStmt(glaze::Widget& w, const Stmt& s, const char* origin)
{
    if (s.kind == Stmt::Kind::Prop)
        applyProp(w, Prop{s.name, s.value}, origin);
    else if (s.kind == Stmt::Kind::Call)
        applyCall(w, Call{s.name, s.value}, origin);
}

// ------------------------------------ DE ICKE-VISUELLA MEDLEMMARNA

namespace {

// En komponents FÄLTRAD — widgetens regler (`applyProp`) där de gäller:
// identiteten, bilderna typade, ett översättnings-id genom `setTextToken`
// och resten genom komponentens egen `setProp` med samma
// värdenormalisering. Okänt ⇒ opak rad, loggad, komponenten orörd.
bool applyComponentProp(glaze::Component& c, const Prop& p, const char* origin)
{
    // IDENTITETEN, speglad som widgetens: den emitterade `name`-raden sätter
    // samma sträng som `buildComponents` redan satt.
    {
        std::string v;
        if (p.field == "name" && parseCppString(p.value, v)) {
            c.name = v;
            return true;
        }
    }

    // SYSTEMFÄLTSIKONENS BILDER — programikonens regel: `glaze::img(…)` eller
    // `{}` (förvalet), typat direkt till sättaren. `parseBildKälla` är den
    // regel `argImg` har, med svaret som `ImageSource` — typen ikonens
    // sättare tar. Oparsbar form ⇒ opak rad och ikonen orörd, aldrig en
    // gissad bild.
    if (p.field == "icon" || p.field == "smallIcon") {
        if (auto* ti = dynamic_cast<glaze::TrayIcon*>(&c)) {
            glaze::ImageSource bild;
            if (!parseBildKälla(p.value, bild)) {
                gfx::log("%s(%s): opak %s-rad (varken glaze::img(…) eller "
                         "{}, utförs inte)", origin, c.name.c_str(),
                         p.field.c_str());
                return false;
            }
            if (p.field == "icon")
                ti->setIcon(bild);
            else
                ti->setSmallIcon(bild);
            return true;
        }
    }

    // TEXTFÄLTEN — widgetens regel (`applyTextField`, 🐛 58): tooltipens
    // `glaze::tr(N)` som token, en literal genom `setProp`, en konstant eller
    // ett uttryck opakt. Utan grenen hade `ikon.setToolTip(glaze::tr(kX));`
    // gett ikonen uttrycket som tooltip.
    if (ärTextfält(p.field))
        return applyTextField(c, p.field, p.value, origin);

    // ÖVERSÄTTNINGEN I ETT ANNAT FÄLT — tokenet och inte den upplösta
    // strängen. Komponenten säger själv vilka fält som tar ett:
    // `setTextToken` false ⇒ raden är opak (den hade inte kompilerat).
    {
        std::uint32_t trId = 0;
        if (parseTrCall(p.value, trId)) {
            if (c.setTextToken(p.field, glaze::tr(trId)))
                return true;
            gfx::log("%s(%s): '%s' tar inget översättnings-id — raden "
                     "bevaras OPAKT", origin, c.name.c_str(),
                     p.field.c_str());
            return false;
        }
    }

    if (c.setProp(p.field, nyckeltext(p.value)))
        return true;
    gfx::log("%s(%s): opak fältrad '%s' (utförs inte)", origin,
             c.name.c_str(), p.field.c_str());
    return false;
}

// Ett METODANROP på en komponent. KÄNT och utfört när det bara LAGRAR;
// KÄNT och medvetet INTE utfört när det hade gjort något i världen utanför
// ritbordet (en timer som tickar, en ikon i OS:ets fält); annars opakt.
bool applyComponentCall(glaze::Component& c, const Call& call,
                        const char* origin)
{
    const bool tomma = trimma(call.args).empty();

    // TIMERN: start och stopp är kända och utförs inte — ingen timer tickar
    // i ett förhandsvisat fönster, och en tick hade kört användarkod.
    if (dynamic_cast<glaze::Timer*>(&c) && tomma &&
        (call.method == "start" || call.method == "stop"))
        return true;

    if (auto* ti = dynamic_cast<glaze::TrayIcon*>(&c)) {
        // Synligheten och menyns öppning är kända och utförs inte: ritbordet
        // får aldrig lägga en ikon i OS:ets fält eller öppna dess meny.
        if (tomma && (call.method == "show" || call.method == "hide" ||
                      call.method == "openMenu" || call.method == "closeMenu"))
            return true;
        // Animationen är en BEGÄRAN — den går först när ikonen syns, så den
        // utförs och ingenting rör sig.
        if (tomma && call.method == "startAnimation") {
            ti->startAnimation();
            return true;
        }
        if (tomma && call.method == "stopAnimation") {
            ti->stopAnimation();
            return true;
        }
        // ANIMATIONENS BILDER — kollektionsvokabulären, ATOMISKT: allt
        // avkodas innan ikonen muteras. Index utanför är ikonens egen logg
        // och no-op.
        if (call.method == "addFrame" || call.method == "setFrame" ||
            call.method == "setFrames") {
            bool ok = false;
            if (call.method == "addFrame") {
                glaze::ImageSource bild;
                ok = parseBildKälla(call.args, bild);
                if (ok)
                    ti->addFrame(bild);
            } else if (call.method == "setFrame") {
                const std::vector<std::string> a = delaArgument(call.args);
                int                            index = 0;
                glaze::ImageSource             bild;
                ok = a.size() == 2 && argInt(a, 0, index) &&
                     parseBildKälla(a[1], bild);
                if (ok)
                    ti->setFrame(index, bild);
            } else {
                std::vector<glaze::ImageSource> bilder;
                ok = parseBildKällor(call.args, bilder);
                if (ok)
                    ti->setFrames(std::move(bilder));
            }
            if (!ok)
                gfx::log("%s(%s): opak %s-rad (bilderna avkodas inte, "
                         "ikonen lämnas ORÖRD)", origin, c.name.c_str(),
                         call.method.c_str());
            return ok;
        }
        // MENYN — programmenyns form med `{}`-roten, utan genvägsargument.
        const RootMenu r = applyRootMenuCall<false>(*ti, call, origin, c.name,
                                                    "systemfältsmenyn");
        if (r != RootMenu::NotMenu)
            return r == RootMenu::Applied;
        // Postens bild, enabled och kryss — samma tre
        // som menyraden och programikonen.
        const RootMenu s = applyMenuItemState(*ti, call, origin, c.name,
                                              "systemfältsmenyn", true);
        if (s != RootMenu::NotMenu)
            return s == RootMenu::Applied;
    }

    // SETTER-FALLBACKEN — widgetens (`applyCall`): en EGEN komponent som
    // ärver en glaze-typ emitteras i anropsform (writern skriver om bara
    // glaze-typerna), och `set<Fält>(v)` som komponentens `setProp` känner
    // utförs med samma värdenormalisering.
    std::string fält;
    if (setterField(call.method, fält)) {
        // Textfälten har widgetens EN regel — inte den råa texten.
        if (ärTextfält(fält))
            return applyTextField(c, fält, call.args, origin);
        if (c.setProp(fält, nyckeltext(call.args)))
            return true;
    }

    gfx::log("%s(%s): opakt metodanrop '%s' (utförs inte)", origin,
             c.name.c_str(), call.method.c_str());
    return false;
}

} // namespace

bool applyComponentStmt(glaze::Component& c, const Stmt& s,
                        const char* origin)
{
    switch (s.kind) {
    case Stmt::Kind::Prop:
        return applyComponentProp(c, Prop{s.name, s.value}, origin);
    case Stmt::Kind::Call:
        return applyComponentCall(c, Call{s.name, s.value}, origin);
    case Stmt::Kind::Event:
        return true;   // känd — kopplingen görs aldrig i previewn (som för widgetar)
    default:
        return false;  // fönster- och trädsatser hör inte till en komponent
    }
}

} // namespace bld
