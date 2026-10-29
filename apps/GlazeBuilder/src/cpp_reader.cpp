// cpp_reader — parsar builderns egen genererade delmängd (se .h).
// Ambitionsnivån är en RADPARSER, inte en C++-parser: en sats per rad,
// exakt de satsformer grammatiken definierar. Allt annat är
// fail-safe — aldrig gissning, aldrig reparation.

#include "cpp_reader.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace bld {
namespace {

struct Line {
    std::string text;   // utan radslut (och utan \r)
    int         no;     // 1-baserat radnummer
};

std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool validIdentifier(const std::string& s)
{
    if (s.empty() || (!std::isalpha(static_cast<unsigned char>(s[0])) &&
                      s[0] != '_'))
        return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    return true;
}

// Fältnamn i en fältsättningsrad: identifierare, ev. med punktled
// (anchors.left) — okända fält är opaka men måste SE ut som fält.
bool validFieldName(const std::string& s)
{
    if (s.empty() || s.front() == '.' || s.back() == '.')
        return false;
    bool newPart = true;
    for (char c : s) {
        if (c == '.') {
            if (newPart)
                return false;
            newPart = true;
            continue;
        }
        if (newPart && !std::isalpha(static_cast<unsigned char>(c)) &&
            c != '_')
            return false;
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
        newPart = false;
    }
    return true;
}

// EVENT-VOKABULÄREN — de namn ramverket faktiskt har:
// widgetarnas tre (regel 6), fönstrets fyra livscykel-events, drag-and-drop-
// sexan på `Widget` (källans start/slut, målets fyra) och
// resten nedan, varje grupp med sitt skäl.
//
// Den finns för att den KORTA formen har en stor och naturlig träffyta.
// Fullformens nyckel (`= [this](<Sender>* s, ` — sendertypen är ett
// kvalificerat namn) är så smal att inget
// annat kan råka se ut som den, men `= [this] { … };` är exakt hur en
// vanlig callback skrivs — utan vokabulären hade en användarwidget med ett
// publikt `std::function`-fält (`custom.callback = [this] { refresh(); };`)
// klassats som en event-KOPPLING, vilket är fel och nytt.
//
// KÄND BEGRÄNSNING, skriven eftersom den inte går att lösa i en parser utan
// typinformation: ett publikt användarfält som FAKTISKT heter `onClick`
// (eller något av de övriga sex) och bär den exakta kortformen klassas som
// en koppling. Vokabulären skyddar `custom.callback`, men inte
// `custom.onClick` — att skilja dem åt kräver typkännedom som
// `builder_core` medvetet inte har (ren std, ingen kompilator).
bool isKnownEvent(const std::string& name)
{
    return name == "onClick" || name == "onHover" || name == "onChange" ||
           name == "onContextMenu" || name == "onLoad" || name == "onShow" ||
           name == "onHide" || name == "onClose" ||
           // DRAG-AND-DROP — utan raderna blir en handskriven KORT
           // koppling (`zon.onDrop = [this] { släppt(); };`) en opak
           // fältrad, och buildern skriver en ANDRA koppling bredvid.
           name == "onDragStart" || name == "onDragEnd" ||
           name == "onDragEnter" || name == "onDragOver" ||
           name == "onDragLeave" || name == "onDrop" ||
           // TRÄDET — utfällningens två events. Samma skäl som
           // drag-sexan: utan raderna blir en handskriven kort koppling
           // (`trad.onExpand = [this] { fyllBarn(); };`) en opak fältrad.
           name == "onExpand" || name == "onCollapse" ||
           // LISTVYN — aktiveringen (dubbelklick/Enter), samma
           // skäl: `filer.onActivate = [this] { oppna(); };` ska vara en
           // koppling och inte en opak rad.
           name == "onActivate" ||
           // FLIKARNA — stängknappens begäran, samma skäl:
           // `flikar.onTabClose = [this] { stang(); };` är en koppling.
           name == "onTabClose" ||
           // RITYTAN — målarna, musen, tangenten och
           // användarens kamerarörelse: `karta.onPaint = [this] { rita(); };`
           // är en koppling och inte en opak rad (en handler utan args kan
           // inte rita — men den kan be om något annat, och formen är laglig).
           // Lagrens målare (`onPaintLayer`) hör till samma grupp.
           name == "onPaint" || name == "onPaintLayer" ||
           name == "onPaintOverlay" ||
           // `onMouseDown`/`onMouseUp` och `onMouseCancel` nedan är
           // BASENS och gäller varje widget (`knapp.onMouseDown =
           // [this] { ner(); };`) — inte bara ritytans.
           name == "onMouseDown" || name == "onMouseMove" ||
           name == "onMouseUp" || name == "onMouseWheel" ||
           name == "onDoubleClick" || name == "onKeyDown" ||
           name == "onCameraChange" ||
           // RITYTAN I RYMDEN — ticken och
           // styrplattans gest, samma skäl: `scen.onTick = [this] { steg(); };`
           // är förstapersonens koppling och inte en opak rad.
           name == "onTick" || name == "onGesture" ||
           // COMMIT OCH FOKUS — "användaren är klar" på varje
           // inmatande widget och fokus INOM på varje widget, samma skäl:
           // `hojd.onFocus = [this] { markeraAllt(); };` är en koppling.
           name == "onCommit" || name == "onFocus" || name == "onFocusLost" ||
           // LISTBOXENS KRYSS — utan raden blir en handskriven kort
           // `lista.onCheck = …` en opak rad och buildern skriver en andra
           // koppling bredvid. Varje widgets event står i listan.
           name == "onCheck" ||
           // VERKTYGSKNAPPENS RULLGARDIN: `ny.onDropDown = [this] { fyll(); };`
           // är en koppling och inte en opak rad.
           name == "onDropDown" ||
           // INMATNINGEN: tangenttrycket och -släppet på VARJE widget
           // (`falt.onKeyUp = [this] { slapp(); };`) och ritytans
           // avbrutna tryck (`karta.onMouseCancel = [this] { avbryt(); };`)
           // är kopplingar och inte opaka rader.
           name == "onKeyUp" || name == "onMouseCancel" ||
           // DATAGRIDDENS RADVAL: `grid.onSelectionChange = [this] {
           // valdaRader(); };` är en koppling och inte en opak rad.
           name == "onSelectionChange" ||
           // MARKDOWN-VYNS LÄNKAR: `hjalp.onLinkClick = [this] {
           // lankKlickad(); };` är en koppling och inte en opak rad.
           name == "onLinkClick" ||
           // TERMINALEN: `term.onExit = [this] { slut(); };` och
           // `term.onTitleChange` är kopplingar och inte opaka rader.
           name == "onExit" || name == "onTitleChange" ||
           // STORLEKEN: `karta.onResize = [this] { ladda(); };` på
           // varje widget är en koppling och inte en opak rad.
           name == "onResize";
}

// Strukturkoll av ett rått värdeuttryck: balanserade {} () och jämnt
// antal oescapade citattecken — obalans är fail-safe (brace-obalans).
bool balancedValue(const std::string& v)
{
    int braces = 0, parens = 0;
    bool inStr = false;
    for (size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (inStr) {
            if (c == '\\')
                ++i;
            else if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') ++braces;
        else if (c == '}') --braces;
        else if (c == '(') ++parens;
        else if (c == ')') --parens;
        if (braces < 0 || parens < 0)
            return false;
    }
    return braces == 0 && parens == 0 && !inStr;
}

std::vector<Line> splitLines(const std::string& bytes)
{
    std::vector<Line> out;
    std::string cur;
    int no = 1;
    for (char c : bytes) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r')
                cur.pop_back();
            out.push_back(Line{cur, no++});
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r')
            cur.pop_back();
        out.push_back(Line{cur, no});
    }
    return out;
}

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string detectNewline(const std::string& bytes)
{
    return bytes.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

// Byteoffset för början av rad `lineIdx` (0-baserad) i bytes.
// (Radindelningen ovan tappar offsets — räkna om; filerna är små.)
size_t lineStartOffset(const std::string& bytes, int lineIdx)
{
    size_t off = 0;
    for (int i = 0; i < lineIdx; ++i) {
        const size_t nl = bytes.find('\n', off);
        if (nl == std::string::npos)
            return bytes.size();
        off = nl + 1;
    }
    return off;
}

} // namespace

std::string guiPathFor(const std::string& hPath)
{
    const size_t dot = hPath.rfind(".h");
    return (dot == hPath.size() - 2) ? hPath.substr(0, dot) + ".glazeui"
                                     : hPath + ".glazeui";
}

std::string userCppPathFor(const std::string& hPath)
{
    const size_t dot = hPath.rfind(".h");
    return (dot == hPath.size() - 2) ? hPath.substr(0, dot) + ".cpp"
                                     : hPath + ".cpp";
}

// Normaliserar valfri triplett-fil (.h/.cpp/.glazeui) → .h-sökvägen, så
// buildern kan öppnas med vilken som helst av de tre (fil-associationen).
std::string headerPathFor(const std::string& anyPath)
{
    auto endsWith = [&](const std::string& ext) {
        return anyPath.size() >= ext.size() &&
               anyPath.compare(anyPath.size() - ext.size(),
                               ext.size(), ext) == 0;
    };
    if (endsWith(".glazeui")) return anyPath.substr(0, anyPath.size() - 8) + ".h";
    if (endsWith(".cpp"))     return anyPath.substr(0, anyPath.size() - 4) + ".h";
    if (endsWith(".h"))       return anyPath;   // redan .h
    return anyPath + ".h";
}

bool findRegion(const std::string& bytes, size_t& regionStart,
                size_t& regionEnd, std::string& err)
{
    const std::vector<Line> lines = splitLines(bytes);

    int start = -1, end = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string t = trim(lines[i].text);
        if (t == "#pragma region GlazeBuilder") {
            if (start != -1) {
                err = "rad " + std::to_string(lines[i].no) +
                      ": dubblerad '#pragma region GlazeBuilder' — exakt "
                      "ETT markörpar krävs";
                return false;
            }
            start = i;
        } else if (start != -1 && end == -1 &&
                   t.rfind("#pragma region", 0) == 0) {
            err = "rad " + std::to_string(lines[i].no) +
                  ": nästlad '#pragma region' inne i GlazeBuilder-regionen";
            return false;
        } else if (start != -1 && end == -1 && t == "#pragma endregion") {
            end = i;
        }
    }
    if (start == -1) {
        err = "'#pragma region GlazeBuilder' saknas — inte en "
              "GlazeBuilder-fil";
        return false;
    }
    if (end == -1) {
        err = "'#pragma endregion' saknas — stängslet är oparat";
        return false;
    }

    regionStart = lineStartOffset(bytes, start);
    const size_t endLineStart = lineStartOffset(bytes, end);
    const size_t nl = bytes.find('\n', endLineStart);
    regionEnd = (nl == std::string::npos) ? bytes.size() : nl + 1;
    return true;
}

bool parseHeader(const std::string& hBytes, const std::string& hName,
                 WindowModel& m, std::string& err)
{
    size_t rs = 0, re = 0;
    if (!findRegion(hBytes, rs, re, err)) {
        err = hName + ": " + err;
        return false;
    }

    // Radnumret för regionstarten (för diagnoser).
    const std::vector<Line> all = splitLines(hBytes);

    bool inRegion = false;
    bool sawInit  = false;
    for (const Line& ln : all) {
        const std::string t = trim(ln.text);
        if (t == "#pragma region GlazeBuilder") {
            inRegion = true;
            continue;
        }
        if (inRegion && t == "#pragma endregion")
            break;
        if (!inRegion)
            continue;

        if (t.empty() || t.rfind("//", 0) == 0)
            continue;
        if (t == "public:" || t == "private:" || t == "protected:")
            continue;
        if (t == "void initializeComponent();") {
            sawInit = true;
            continue;
        }

        // Konstruktorn: <Id>();
        if (t.size() > 3 && t.compare(t.size() - 3, 3, "();") == 0) {
            const std::string id = t.substr(0, t.size() - 3);
            if (validIdentifier(id)) {
                if (!m.className.empty() && m.className != id) {
                    err = hName + ":" + std::to_string(ln.no) +
                          ": två konstruktor-deklarationer i regionen";
                    return false;
                }
                m.className = id;
                continue;
            }
        }

        // Medlemsdeklaration: <KvalTyp> <id>;
        if (!t.empty() && t.back() == ';' &&
            t.find('(') == std::string::npos) {
            const std::string decl = trim(t.substr(0, t.size() - 1));
            const size_t sp = decl.find_last_of(" \t");
            if (sp != std::string::npos) {
                const std::string type = trim(decl.substr(0, sp));
                const std::string id   = trim(decl.substr(sp + 1));
                if (!type.empty() && validIdentifier(id)) {
                    WidgetNode n;
                    n.type = type;
                    n.id   = id;
                    m.nodes.push_back(std::move(n));
                    continue;
                }
            }
        }

        err = hName + ":" + std::to_string(ln.no) +
              ": oidentifierbar rad i GlazeBuilder-regionen: '" + t + "'";
        return false;
    }

    if (m.className.empty()) {
        err = hName + ": konstruktor-deklarationen saknas i regionen";
        return false;
    }
    if (!sawInit) {
        err = hName + ": 'void initializeComponent();' saknas i regionen";
        return false;
    }
    return true;
}

bool parseGuiCpp(const std::string& cppBytes, const std::string& cppName,
                 WindowModel& m, std::string& err)
{
    const std::vector<Line> lines = splitLines(cppBytes);

    enum class St { Preamble, WantBrace, Body, After };
    St st = St::Preamble;

    // Adopt-ordningen ÄR z-ordningen — noteras och används för att
    // ordna om noderna sist.
    std::vector<std::string> adoptOrder;

    // Designtaggen räknas SEPARAT från m.designLine: en innehållslös
    // `// glazeui-design:` ger tom text men ska ändå räknas som en rad.
    int designSeen = 0;

    auto fail = [&](const Line& ln, const std::string& msg) {
        err = cppName + ":" + std::to_string(ln.no) + ": " + msg;
        return false;
    };

    // Slår upp en nod, och SKAPAR den om namnet är en inbyggd
    // fönstermedlem: `statusBar` deklareras aldrig i
    // regionen — `glaze::AppWindow` har fältet — men får ändå bära fält-
    // och anropsrader. Noden markeras `builtin` så writern varken
    // deklarerar eller adopterar den. nullptr = verkligen okänd.
    auto findTarget = [&](const std::string& id) -> WidgetNode* {
        if (WidgetNode* n = m.find(id))
            return n;
        const BuiltinMember* b = builtinMember(id);
        if (!b)
            return nullptr;
        WidgetNode n;
        n.id      = b->id;
        n.type    = b->type;
        n.builtin = true;
        m.nodes.push_back(std::move(n));
        return &m.nodes.back();
    };

    for (const Line& ln : lines) {
        const std::string t = trim(ln.text);

        // DESIGNTAGGEN — direkt efter trim, FÖRE både St::After-grenen och
        // den generella kommentarskippen nedan. Annars hade radens POSITION
        // avgjort om taggen alls hittas, och en tagg på fel plats hade
        // raderats tyst vid nästa spara (den enda vägen i hela passet där
        // användardata kan gå förlorad). Placeringen är kanonisk vid
        // EMISSION, inte vid läsning.
        //
        // TVÅ designrader = tvetydig metadata ⇒ fail-safe, precis som ett
        // skadat stängsel. Aldrig "sista vinner" — regel 4 säger vägra i
        // stället för att gissa.
        if (t.rfind(kDesignPrefix, 0) == 0) {
            if (++designSeen > 1)
                return fail(ln, "två 'glazeui-design'-rader — tvetydigt "
                                "designtema (fail-safe)");
            m.hasDesign  = true;   // NÄRVARON skilt från innehållet: en tom
                                   // tagg ska skrivas tillbaka, inte raderas
            m.designLine = trim(t.substr(std::strlen(kDesignPrefix)));
            continue;
        }

        if (st == St::After) {
            if (t.empty() || t.rfind("//", 0) == 0)
                continue;
            return fail(ln, "innehåll efter initializeComponent-kroppen "
                            "(filen ska vara 100 % genererad)");
        }

        if (t.empty() || t.rfind("//", 0) == 0)
            continue;

        if (st == St::Preamble) {
            if (t.rfind("#include", 0) == 0)
                continue;
            // Ctor-enradaren: X::X() { initializeComponent(); }
            const std::string ctor =
                m.className + "::" + m.className +
                "() { initializeComponent(); }";
            if (t == ctor)
                continue;
            const std::string sig =
                "void " + m.className + "::initializeComponent()";
            if (t == sig) {
                st = St::WantBrace;
                continue;
            }
            if (t == sig + " {") {   // tolerera klammer på samma rad
                st = St::Body;
                continue;
            }
            return fail(ln, "oidentifierbar rad före initializeComponent: '" +
                                t + "'");
        }

        if (st == St::WantBrace) {
            if (t == "{") {
                st = St::Body;
                continue;
            }
            return fail(ln, "väntade '{' efter initializeComponent-"
                            "signaturen");
        }

        // ---- Body ----
        if (t == "}") {
            st = St::After;
            continue;
        }
        if (t.back() != ';')
            return fail(ln, "sats utan avslutande ';' (en sats per rad): '" +
                            t + "'");

        // setTitle("...");  — lagras som en vanlig WindowCall-sats: modellen
        // har inga cachefält för titel och storlek, de
        // LÄSES ur sista satsen.
        if (t.rfind("setTitle(", 0) == 0 &&
            t.compare(t.size() - 2, 2, ");") == 0) {
            const std::string arg = t.substr(9, t.size() - 9 - 2);
            // TRE FORMER: strängliteralen, ÖVERSÄTTNINGEN `glaze::tr(N)`
            // (`AppWindow::setTitle` tar en `TrText`) och VARJE annat
            // balanserat uttryck, som en OPAK sats:
            // `setTitle(glaze::tr(app::str::Titel))` kompilerar, men verktygen
            // kan inte lösa konstanten. Samma regel som för varje annat naket
            // fönsteranrop med ett okänt argument — bevaras, inte FAIL-SAFE:
            // `bld::applyWindowCall` utför literalen och tr-id:t och
            // loggar resten, `WindowModel::title()` svarar tomt för den.
            if (!balancedValue(arg))
                return fail(ln, "obalanserat argumentuttryck "
                                "(klammer/parentes/citat): '" + arg + "'");
            m.appendWindowCall("setTitle", arg);
            continue;
        }

        // setClientSize(B, H);
        if (t.rfind("setClientSize(", 0) == 0 &&
            t.compare(t.size() - 2, 2, ");") == 0) {
            const std::string arg = t.substr(14, t.size() - 14 - 2);
            int w = 0, h = 0;
            if (!parseTwoInts(arg, w, h))
                return fail(ln, "setClientSize-argumenten är inte två heltal");
            m.appendWindowCall("setClientSize", arg);
            continue;
        }

        // Adopt: root().adopt(id); eller parent.adopt(id);
        const size_t ad = t.find(".adopt(");
        if (ad != std::string::npos && t.compare(t.size() - 2, 2, ");") == 0) {
            const std::string lhs = t.substr(0, ad);
            const std::string arg = t.substr(ad + 7, t.size() - (ad + 7) - 2);
            if (!validIdentifier(arg))
                return fail(ln, "adopt-argumentet är ingen identifierare: '" +
                                arg + "'");
            WidgetNode* child = m.find(arg);
            if (!child)
                return fail(ln, "adopt av odeklarerad medlem '" + arg + "'");
            if (m.isAdopted(arg))
                return fail(ln, "'" + arg + "' adopteras två gånger");
            std::string parentId;
            if (lhs != "root()") {
                // FÖRÄLDERN via findTarget och inte m.find: en INBYGGD
                // fönstermedlem kan mycket väl ha adopt-raden som sin
                // första referens (`statusBar.adopt(chip);`).
                if (!validIdentifier(lhs) || !findTarget(lhs))
                    return fail(ln, "adopt på okänd förälder '" + lhs + "'");
                parentId = lhs;
            }
            m.setAdoption(arg, parentId);
            adoptOrder.push_back(arg);
            continue;
        }

        // Event-koppling (regel 6, EXAKT form):
        //   <lhs> = [this](<Sender>* s, <Args>& e) { <h>(s, e); };
        //
        // SENDERTYPEN ÄR FRI: den som avfyrar är
        // sender (`Handler<Args, Sender>`), så en timers koppling bär
        // `glaze::Timer* s`. Formen är ett KVALIFICERAT NAMN
        // (`[A-Za-z_]\w*(::[A-Za-z_]\w*)*`) följt av `* s, ` — allt annat är
        // fail-safe. `glaze::Widget` lagras som tom sträng, så en
        // vanlig koppling inte bär någon sendertyp i modellen.
        const size_t evAssign = t.find(" = [this](");
        if (evAssign != std::string::npos) {
            const std::string lhs = t.substr(0, evAssign);
            std::string rhs = t.substr(evAssign + 3);   // "[this](..."
            // Sendertypen är texten fram till den FÖRSTA `* s, ` — ett
            // kvalificerat namn har ingen `*`, så det är samma gräns som en
            // skanning av namnet hade gett — och varje led mellan `::` måste
            // vara en identifierare.
            const std::string lead        = "[this](";
            const std::string afterSender = "* s, ";
            const size_t      p = rhs.find(afterSender, lead.size());
            std::string       sender =
                p == std::string::npos ? std::string()
                                       : rhs.substr(lead.size(), p - lead.size());
            bool senderOk = !sender.empty();
            for (size_t b = 0; senderOk;) {
                const size_t e = sender.find("::", b);
                senderOk       = validIdentifier(sender.substr(
                    b, e == std::string::npos ? std::string::npos : e - b));
                if (e == std::string::npos)
                    break;
                b = e + 2;
            }
            if (!senderOk)
                return fail(ln, "event-lambdan avviker från kontraktet "
                                "(sender, args)");
            if (sender == "glaze::Widget")
                sender.clear();
            rhs = rhs.substr(p + afterSender.size());
            const size_t amp = rhs.find("& e) { ");
            if (amp == std::string::npos)
                return fail(ln, "event-lambdan avviker från kontraktet "
                                "(args-referensen)");
            const std::string args = rhs.substr(0, amp);
            std::string call = rhs.substr(amp + 7);
            const std::string suffix = "(s, e); };";
            if (call.size() <= suffix.size() ||
                call.compare(call.size() - suffix.size(), suffix.size(),
                             suffix) != 0)
                return fail(ln, "event-lambdan avviker från kontraktet "
                                "(handler-anropet)");
            const std::string handler =
                call.substr(0, call.size() - suffix.size());
            if (!validIdentifier(handler))
                return fail(ln, "handlernamnet är ingen identifierare: '" +
                                handler + "'");

            EventBind eb{"", args, handler};
            eb.sender = std::move(sender);
            if (lhs.rfind("glaze::AppWindow::", 0) == 0) {
                eb.event = lhs.substr(18);
                if (!validIdentifier(eb.event))
                    return fail(ln, "okänt fönster-event '" + eb.event + "'");
                m.appendWindowEvent(eb);
                continue;
            }
            const size_t dot = lhs.find('.');
            if (dot == std::string::npos)
                return fail(ln, "event på okänt mål: '" + lhs + "'");
            const std::string id = lhs.substr(0, dot);
            eb.event = lhs.substr(dot + 1);
            if (!m.find(id))
                return fail(ln, "event på odeklarerad medlem '" + id + "'");
            if (!validIdentifier(eb.event))
                return fail(ln, "ogiltigt eventnamn '" + eb.event + "'");
            m.appendEvent(id, eb);
            continue;
        }

        // KORT EVENT-FORM, den PARAMETERLÖSA lambdan:
        //   <lhs> = [this] { <handler>(); };
        //
        // Ligger EFTER fullformen med flit: fullformens smalare nyckel
        // (`" = [this]("`) fångar sin egen rad först, så den här grenen
        // ser bara rader utan parameterlista.
        //
        // MATCHNINGEN ÄR TVÅLEDAD, och båda leden är bärande:
        //
        //   1. Träffen måste vara radens TOPPNIVÅ-tilldelning — inte en
        //      sekvens inuti ett VÄRDE. En breddad `find` ensam hade
        //      skickat `btn.id = "x = [this] { fel(); };";` till strikt
        //      kortformsparse, alltså fail-safe på en rad som i dag blir
        //      en ofarlig opak `Prop`. Vänsterledet valideras därför som
        //      `<id>.<event>` respektive `glaze::AppWindow::<event>`.
        //   2. Namnet måste vara ett KÄNT event (`isKnownEvent`).
        //
        // MISS PÅ NÅGOT AV LEDEN ⇒ FALL IGENOM till fältgrenen nedan,
        // precis som i dag. Och det gäller ÄVEN en misslyckad
        // kortformsparse på en WIDGET-rad: `btn.onClick = [this] {
        // refresh(1); };` är giltig C++ som `glaze::Handler` tar emot, men
        // den är inte ett naket parameterlöst anrop — bara en
        // FULLSTÄNDIGT lyckad parse får skapa `Kind::Event`, allt annat
        // blir opak `Prop`. FÖNSTERformen fail-safe:ar däremot vid trasig
        // grammatik: den har ingen opak fältväg (`glaze::AppWindow::onLoad`
        // saknar punkt och faller till "oidentifierbar sats"), så där är
        // det avsett.
        {
            static const std::string kShortKey = " = [this] { ";
            const size_t sh = t.find(kShortKey);
            if (sh != std::string::npos) {
                const std::string lhs = t.substr(0, sh);
                const bool isWin =
                    lhs.rfind("glaze::AppWindow::", 0) == 0;
                std::string id, ev;
                bool led1 = false;
                if (isWin) {
                    ev   = lhs.substr(18);
                    led1 = validIdentifier(ev);
                } else if (const size_t d = lhs.find('.');
                           d != std::string::npos) {
                    id   = lhs.substr(0, d);
                    ev   = lhs.substr(d + 1);
                    led1 = validIdentifier(id) && validIdentifier(ev);
                }
                if (led1 && isKnownEvent(ev)) {
                    // Kroppen: exakt ett naket parameterlöst anrop.
                    const std::string rhs = t.substr(sh + kShortKey.size());
                    const std::string suffix = "(); };";
                    std::string handler;
                    const bool kropp =
                        rhs.size() > suffix.size() &&
                        rhs.compare(rhs.size() - suffix.size(),
                                    suffix.size(), suffix) == 0 &&
                        validIdentifier(
                            handler = rhs.substr(0, rhs.size() -
                                                        suffix.size()));
                    if (kropp) {
                        EventBind eb{ev, "", handler, true};
                        if (isWin) {
                            m.appendWindowEvent(eb);
                            continue;
                        }
                        if (!m.find(id))
                            return fail(ln, "event på odeklarerad medlem '" +
                                            id + "'");
                        m.appendEvent(id, eb);
                        continue;
                    }
                    if (isWin)
                        return fail(ln, "den korta event-lambdan avviker "
                                        "från kontraktet (kroppen ska vara "
                                        "ett parameterlöst anrop)");
                    // WIDGET-raden faller igenom till `Prop` — aldrig
                    // fail-safe (se resonemanget ovan).
                }
            }
        }

        // Fältsättning: <id>.<fält> = <värde>;  (okänt fält = opak rad)
        const size_t dot = t.find('.');
        const size_t eq  = t.find(" = ");
        if (dot != std::string::npos && eq != std::string::npos && dot < eq) {
            const std::string id    = t.substr(0, dot);
            const std::string field = t.substr(dot + 1, eq - dot - 1);
            const std::string value = t.substr(eq + 3, t.size() - eq - 4);
            if (validIdentifier(id) && validFieldName(field)) {
                if (!findTarget(id))
                    return fail(ln, "fältsättning på odeklarerad medlem '" +
                                    id + "'");
                if (!balancedValue(value))
                    return fail(ln, "obalanserat värdeuttryck (klammer/"
                                    "parentes/citat): '" + value + "'");
                Stmt s;
                s.kind   = Stmt::Kind::Prop;
                s.target = id;
                s.name   = field;
                s.value  = value;
                m.stmts.push_back(std::move(s));
                continue;
            }
        }

        // METODANROP: <id>.<metod>(<argument>);
        //
        // SIST bland satsformerna, och det är bärande: adopt-raden och
        // event-lambdan är också "punkt följt av parentes" respektive
        // "punkt följt av likhetstecken", och de har egna, striktare
        // grenar ovanför. Kommer man hit är det varken adopt (grenen
        // ovan fångar `.adopt(`) eller en tilldelning (inget " = ").
        //
        // Okänt METODNAMN är OPAKT — precis som ett okänt fält (regel 4):
        // raden bevaras ordagrant och ignoreras vid ritning. ALDRIG
        // fail-safe; en äldre builder som möter en nyare metod ska kunna
        // öppna och spara filen utan att förstöra den.
        if (t.size() > 3 && t.compare(t.size() - 2, 2, ");") == 0) {
            const size_t open = t.find('(');
            const size_t dotc = t.rfind('.', open);
            if (open != std::string::npos && dotc != std::string::npos &&
                dotc > 0 && open > dotc + 1) {
                const std::string id     = t.substr(0, dotc);
                const std::string method = t.substr(dotc + 1, open - dotc - 1);
                const std::string args =
                    t.substr(open + 1, t.size() - (open + 1) - 2);
                if (validIdentifier(id) && validIdentifier(method)) {
                    WidgetNode* n = findTarget(id);
                    if (!n)
                        return fail(ln, "metodanrop på odeklarerad medlem '" +
                                        id + "'");
                    if (!balancedValue(args))
                        return fail(ln, "obalanserat argumentuttryck "
                                        "(klammer/parentes/citat): '" +
                                        args + "'");
                    // SETTER-NORMALISERINGEN:
                    // `okBtn.setText("…")` lagras som en `Prop` med
                    // fältnamnet, så `kFieldMeta`, `applyProp` och
                    // egenskapspanelen ser samma vokabulär som för en fältrad.
                    // Bara `glaze::`-typer — en egen
                    // widget ska round-trippa i sin egen form.
                    //
                    // TYPMEDVETET:
                    // frågan är inte "finns fältnamnet någonstans" utan
                    // "finns det på DEN HÄR typen". Utan typen blir
                    // `lista.setChecked(1, true);` på en ListBox en
                    // `Prop{checked, "1, true"}` — Checkbox/Radios
                    // bool-fält med en ListBox-signatur i värdet, och
                    // previewn sätter aldrig krysset.
                    std::string fält;
                    if (isGlazeType(n->type) &&
                        setterField(n->type, method, fält)) {
                        Stmt sp;
                        sp.kind   = Stmt::Kind::Prop;
                        sp.target = id;
                        sp.name   = fält;
                        sp.value  = args;
                        m.stmts.push_back(std::move(sp));
                        continue;
                    }
                    m.appendCall(id, method, args);
                    continue;
                }
            }
        }

        // NAKET FÖNSTERANROP: <metod>(<argument>);
        //
        // En FORM och inte en lista: `setTitle`/`setClientSize` har egna
        // grenar ovan, och allt annat är ett naket fönsteranrop. Hade de
        // fallit till "oidentifierbar sats" vore det FAIL-SAFE PÅ HELA FILEN
        // för `setContentSize`, `setMinimumSize`, `setControlBox` … — regel 4
        // vore då osann för precis den satsform som är fönstrets EGNA
        // attribut, medan en okänd fältrad och ett okänt metodanrop på en
        // mottagare bevaras opakt.
        //
        // Behandlingen är densamma som för `<id>.<metod>()`: känt namn
        // utförs (`bld::applyWindowCall`), okänt bevaras ORDAGRANT och
        // loggas. En nyare sättare kan därmed aldrig bricka en fil för en
        // äldre builder.
        if (t.size() > 3 && t.compare(t.size() - 2, 2, ");") == 0) {
            const size_t open = t.find('(');
            if (open != std::string::npos && open > 0) {
                const std::string method = t.substr(0, open);
                const std::string args =
                    t.substr(open + 1, t.size() - open - 3);
                if (validIdentifier(method)) {
                    if (!balancedValue(args))
                        return fail(ln, "obalanserat argumentuttryck "
                                        "(klammer/parentes/citat): '" +
                                        args + "'");
                    m.appendWindowCall(method, args);
                    continue;
                }
            }
        }

        return fail(ln, "oidentifierbar sats: '" + t + "'");
    }

    if (st != St::After)
        return fail(lines.empty() ? Line{"", 1} : lines.back(),
                    "initializeComponent-kroppen avslutas aldrig ('}' "
                    "saknas — brace-obalans)");

    // NODORDNINGEN (inte satsordningen — de är skilda, se window_model.h):
    // INBYGGDA fönstermedlemmar FÖRST, sedan adopt-ordningen (= z-ordning),
    // sist de icke-adopterade i deklarationsordning.
    //
    // Builtins först är TRÄDBYGGARNAS sanning: `statusBar` adopteras av
    // fönstret i dess konstruktor, alltså före allt genererat. Låg den sist
    // (den har ingen adopt-rad) byggdes ett barn som `statusBar.adopt(chip)`
    // pekar ut FÖRE sin förälder, och båda trädbyggarna föll då tyst
    // tillbaka på roten.
    std::vector<WidgetNode> ordered;
    ordered.reserve(m.nodes.size());
    std::vector<bool> taken(m.nodes.size(), false);
    auto take = [&](size_t i) {
        ordered.push_back(std::move(m.nodes[i]));
        taken[i] = true;
    };
    for (size_t i = 0; i < m.nodes.size(); ++i)
        if (m.nodes[i].builtin)
            take(i);
    for (const std::string& id : adoptOrder)
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (!taken[i] && m.nodes[i].id == id)
                take(i);
    for (size_t i = 0; i < m.nodes.size(); ++i)
        if (!taken[i])
            take(i);
    m.nodes = std::move(ordered);
    return true;
}

ReadResult readWindow(const std::string& hPath)
{
    ReadResult r;

    std::string hBytes;
    if (!readFile(hPath, hBytes)) {
        r.error = hPath + ": kan inte öppna filen";
        return r;   // hFound = false → "nytt fönster"-mall
    }
    r.hFound   = true;
    r.hNewline = detectNewline(hBytes);

    if (!parseHeader(hBytes, hPath, r.model, r.error))
        return r;

    const std::string cppPath = guiPathFor(hPath);
    std::string cppBytes;
    if (!readFile(cppPath, cppBytes)) {
        r.error = cppPath + ": saknas — stängslet finns i .h men den "
                  "genererade filen fattas (fail-safe)";
        return r;
    }
    r.cppFound   = true;
    r.cppNewline = detectNewline(cppBytes);

    if (!parseGuiCpp(cppBytes, cppPath, r.model, r.error))
        return r;

    r.ok = true;
    return r;
}

} // namespace bld
