// cpp_writer — kanonisk emittering + splasa. Emissionen är kontraktet
// mot cpp_reader (grammatiken): varje ny satsform här kräver
// reader-stöd + round-trip-test i samma ändring. De handkonverterade
// demo-fönstren är referensen — emitterns utdata ska vara byte-identisk med
// dem efter en oförändrad läs/skriv-cykel.

#include "cpp_writer.h"

#include <fstream>
#include <sstream>

#include "cpp_reader.h"   // findRegion — splasan letar om vid skrivning

namespace bld {
namespace {

bool readFileRaw(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileRaw(const std::string& path, const std::string& bytes,
                  std::string& err)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        err = path + ": kan inte skriva filen";
        return false;
    }
    out.write(bytes.data(),
              static_cast<std::streamsize>(bytes.size()));
    if (!out.good()) {
        err = path + ": skrivfel";
        return false;
    }
    return true;
}

bool fileExists(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return in.is_open();
}

// Fullformens sendertyp: modellen lagrar
// `glaze::Widget` som tom sträng (den vanliga kopplingen skrivs som den är),
// och en timers koppling bär `glaze::Timer`.
std::string senderType(const std::string& sender)
{
    return sender.empty() ? std::string("glaze::Widget") : sender;
}

} // namespace

// ------------------------------------------------------------ emittering

std::string emitRegion(const WindowModel& m, const std::string& nl)
{
    std::string s;
    s += "#pragma region GlazeBuilder" + nl;
    s += "    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, "
         "inte för hand." + nl;
    s += "public:" + nl;
    s += "    " + m.className + "();" + nl;
    s += nl;
    s += "private:" + nl;
    for (const auto& n : m.nodes) {
        // INBYGGDA fönstermedlemmar deklareras ALDRIG:
        // `glaze::AppWindow` har redan fältet, och en medlemsrad här hade
        // skuggat basklassens och inte kompilerat.
        if (n.builtin)
            continue;
        s += "    " + n.type + " " + n.id + ";" + nl;
    }
    s += "    void initializeComponent();" + nl;
    s += "#pragma endregion" + nl;
    return s;
}

std::string emitGuiCpp(const WindowModel& m, const std::string& headerName,
                       const std::string& nl)
{
    std::string s;
    s += "// GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte "
         "här." + nl;
    // DESIGNTAGGEN på sin KANONISKA plats: efter GENERERAD-raden och FÖRE
    // #include. Emitteras BARA när modellen bär en — samma "emittera aldrig
    // orört"-regel som tabIndex, så befintliga tripletter får noll diff.
    //
    // Kontraktet är kanonisk NORMALISERING, inte ordagrann round-trip:
    // *.glazeui skrivs om helt vid spara, så en handskriven tagg längre ner
    // i filen läses men flyttas hit. Det som bevaras ordagrant är VÄRDENA —
    // inte indrag, radposition eller blankstegsmängd.
    //
    // Villkoret är NÄRVARON (`hasDesign`), inte innehållet: en tom tagg är
    // närvarande men innehållslös, och hade den emitterats på strängen allena
    // raderat sig själv vid första spara. Mellanslaget efter prefixet hör
    // till VÄRDET — utan värde emitteras inget, annars hade en tom tagg fått
    // ett släpande blanksteg och tappat byte-identiteten.
    if (m.hasDesign)
        s += std::string(kDesignPrefix) +
             (m.designLine.empty() ? "" : " " + m.designLine) + nl;
    s += "#include \"" + headerName + "\"" + nl;
    s += nl;
    s += m.className + "::" + m.className +
         "() { initializeComponent(); }" + nl;
    s += nl;
    s += "void " + m.className + "::initializeComponent()" + nl;
    s += "{" + nl;

    // EN emissionsväg: satslistan skrivs i LISTORDNING,
    // alltså källordning. Titel och storlek är vanliga `WindowCall`-satser
    // och har inget eget "först"-spår — därmed kan ett fönsteranrop aldrig
    // emitteras dubbelt, och skillnaden mellan "känd metod vi utför" och
    // "okänd metod vi bevarar" ligger enbart i `applyWindowCall`.
    //
    // Gruppvis emission (fälten först, anropen sedan, per nod) hade flyttat
    // rader: ett handskrivet anrop efter `adopt()` hade hamnat före hela
    // trädbygget. Regel 4 skyddar ORDNINGEN lika mycket som texten —
    // satserna är exekverbar C++.
    //
    // BLANKRADERNA är den enda normaliseringen: en blankrad
    // skjuts in när satsens BLOCK byter (fönsteranropen · en widgets
    // rader · event-kopplingarna · trädbygget). En kanoniskt ordnad fil
    // får därmed exakt sin gamla layout tillbaka, och en varvad fil
    // behåller sin ordning men får blankrader vid varje byte.
    auto blockKey = [](const Stmt& st) -> std::string {
        switch (st.kind) {
        case Stmt::Kind::WindowCall:  return "#window";
        case Stmt::Kind::WindowEvent:
        case Stmt::Kind::Event:       return "#event";
        case Stmt::Kind::Adopt:       return "#adopt";
        case Stmt::Kind::Prop:
        case Stmt::Kind::Call:        break;
        }
        return st.target;
    };

    std::string prevKey;
    bool        first = true;
    for (const Stmt& st : m.stmts) {
        const std::string key = blockKey(st);
        if (!first && key != prevKey)
            s += nl;
        prevKey = key;
        first   = false;

        switch (st.kind) {
        case Stmt::Kind::WindowCall:
            s += "    " + st.name + "(" + st.value + ");" + nl;
            break;
        case Stmt::Kind::WindowEvent:
            // TVÅ FORMER: kort in, kort ut. Flaggan
            // MINNS formen — den härleds inte ur tom `args`, och en
            // expansion till fullformen hade inte kompilerat (handlern tar
            // noll argument).
            s += st.shortForm
                     ? "    glaze::AppWindow::" + st.name + " = [this] { " +
                           st.handler + "(); };" + nl
                     : "    glaze::AppWindow::" + st.name + " = [this](" +
                           senderType(st.sender) + "* s, " + st.args +
                           "& e) { " + st.handler + "(s, e); };" + nl;
            break;
        case Stmt::Kind::Prop: {
            // SETTER-FÄLTEN emitteras i CALL-formen:
            // tillståndet är privat i C++, och `.glazeui`
            // KOMPILERAS — en fältrad hade inte byggt. Formen är dessutom
            // regel 4-säker: en äldre builder läser den som ett okänt
            // metodanrop och bevarar den opakt.
            //
            // BARA för `glaze::`-typer. En egen widget med ett publikt
            // `text`-fält ska fortsätta skrivas som en fältrad — annars
            // hade verktyget emitterat en rad som inte kompilerar i
            // användarens projekt.
            // TYPMEDVETET — samma
            // tabell som readern frågar, med samma typ. En Prop vars fält
            // INTE hör till nodtypen emitteras som en fältrad, precis som
            // en okänd fältrad: writern hittar aldrig på ett setteranrop
            // för en metod typen saknar.
            const WidgetNode* n = m.find(st.target);
            if (n && isGlazeType(n->type) && isSetterField(n->type, st.name))
                s += "    " + st.target + "." + setterName(st.name) + "(" +
                     st.value + ");" + nl;
            else
                s += "    " + st.target + "." + st.name + " = " + st.value +
                     ";" + nl;
            break;
        }
        case Stmt::Kind::Call:
            s += "    " + st.target + "." + st.name + "(" + st.value + ");" +
                 nl;
            break;
        case Stmt::Kind::Event:
            // Se WindowEvent ovan — samma två former.
            s += st.shortForm
                     ? "    " + st.target + "." + st.name + " = [this] { " +
                           st.handler + "(); };" + nl
                     : "    " + st.target + "." + st.name + " = [this](" +
                           senderType(st.sender) + "* s, " + st.args +
                           "& e) { " + st.handler + "(s, e); };" + nl;
            break;
        case Stmt::Kind::Adopt:
            // Trädbygget: adopt-ordningen ÄR z-ordningen.
            s += st.name.empty()
                     ? "    root().adopt(" + st.target + ");" + nl
                     : "    " + st.name + ".adopt(" + st.target + ");" + nl;
            break;
        }
    }

    s += "}" + nl;
    return s;
}

std::string emitHeaderTemplate(const WindowModel& m, const std::string& nl)
{
    std::string s;
    s += "#pragma once" + nl;
    s += "#include <glaze/glaze.h>" + nl;
    s += nl;
    s += "class " + m.className + " : public glaze::AppWindow {" + nl;
    s += emitRegion(m, nl);
    s += nl;
    s += "    // Allt härunder är användarens — GlazeBuilder rör det "
         "aldrig (den" + nl;
    s += "    // appendar bara nya handler-deklarationer)." + nl;
    s += "};" + nl;
    return s;
}

std::string emitUserCppTemplate(const WindowModel& m,
                                const std::string& headerName,
                                const std::string& nl)
{
    std::string s;
    s += "#include \"" + headerName + "\"" + nl;
    s += nl;
    s += "// Handler-kroppar för " + m.className +
         " — en medlemsfunktion per event, formen (sender, args)." + nl;
    return s;
}

std::string emitHandlerDecl(const EventBind& e, const std::string& nl,
                            bool firstAppend)
{
    std::string s;
    if (firstAppend)
        s += "    // Handler-deklarationer appendade av GlazeBuilder "
             "(kroppen bor i .cpp)." + nl;
    s += "    void " + e.handler + "(" + senderType(e.sender) + "* sender, " +
         e.args + "& e);" + nl;
    return s;
}

std::string emitHandlerStub(const std::string& className, const EventBind& e,
                            const std::string& nl)
{
    std::string s;
    s += nl;
    s += "void " + className + "::" + e.handler + "(" +
         senderType(e.sender) + "* sender, " + e.args + "& e)" + nl;
    s += "{" + nl;
    s += "}" + nl;
    return s;
}

// ------------------------------------------------- de fyra operationerna

bool rewriteRegion(const std::string& hPath, const std::string& region,
                   std::string& err)
{
    std::string bytes;
    if (!readFileRaw(hPath, bytes)) {
        err = hPath + ": kan inte läsa filen";
        return false;
    }
    size_t rs = 0, re = 0;
    if (!findRegion(bytes, rs, re, err)) {
        err = hPath + ": " + err + " — sparandet vägras (fail-safe)";
        return false;
    }
    // Splasan: allt före och efter regionen klistras tillbaka byte för
    // byte — ENBART [rs, re) byts ut.
    const std::string out =
        bytes.substr(0, rs) + region + bytes.substr(re);
    return writeFileRaw(hPath, out, err);
}

bool rewriteGeneratedFile(const std::string& guiCppPath,
                          const std::string& content, std::string& err)
{
    // *.glazeui är 100 % genererad — hela filen ÄR regionen.
    return writeFileRaw(guiCppPath, content, err);
}

bool appendToUserArea(const std::string& hPath, const std::string& text,
                      std::string& err)
{
    std::string bytes;
    if (!readFileRaw(hPath, bytes)) {
        err = hPath + ": kan inte läsa filen";
        return false;
    }
    size_t rs = 0, re = 0;
    if (!findRegion(bytes, rs, re, err)) {
        err = hPath + ": " + err + " — append vägras (fail-safe)";
        return false;
    }
    // Append-punkten: direkt EFTER #pragma endregion-raden.
    const std::string out = bytes.substr(0, re) + text + bytes.substr(re);
    return writeFileRaw(hPath, out, err);
}

bool appendToCppEnd(const std::string& cppPath, const std::string& text,
                    std::string& err)
{
    std::string bytes;
    if (!readFileRaw(cppPath, bytes)) {
        err = cppPath + ": kan inte läsa filen";
        return false;
    }
    // Appenda i filslutet; säkra att ett radslut finns före texten.
    std::string out = bytes;
    if (!out.empty() && out.back() != '\n')
        out += "\n";
    out += text;
    return writeFileRaw(cppPath, out, err);
}

bool createFromTemplate(const std::string& path, const std::string& content,
                        std::string& err)
{
    if (fileExists(path)) {
        err = path + ": finns redan — mallen skriver aldrig över "
              "(fail-safe)";
        return false;
    }
    return writeFileRaw(path, content, err);
}

bool fileContains(const std::string& path, const std::string& needle)
{
    std::string bytes;
    if (!readFileRaw(path, bytes))
        return false;
    return bytes.find(needle) != std::string::npos;
}

} // namespace bld
