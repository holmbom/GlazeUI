// roundtrip_gate — reader/writer-grinden för triplett-grammatiken, och
// särskilt för DESIGNTAGGEN.
//
// Headless och ren std: länkar bara `builder_core` (window_model +
// cpp_reader + cpp_writer), inget fönster och ingen pump. Repo-roten kommer
// in som GLAZE_TEST_ROOT från CMake, så testet kan köras från var som helst.
//
// Kontraktet som mäts:
//
//   1. Befintliga demo-tripletter: läs → skriv ger BYTE-IDENTISK *.glazeui.
//      De saknar designtagg ⇒ writern emitterar ingen rad ⇒ noll diff.
//   2. Fixtur MED tagg: byte-identisk (taggen bevaras).
//   3. Fixtur med CRLF: byte-identisk (radsluten följer filens egna).
//   4. Fixtur med OKÄND NYCKEL: nyckeln skrivs tillbaka (hård regel 4).
//   5. Fixtur med taggen på FEL PLATS: läses, och skrivs på KANONISK plats.
//      Dokumenterat utfall — *.glazeui skrivs om helt, så kontraktet är
//      kanonisk normalisering och inte ordagrann round-trip.
//   6. Fixtur med TVÅ designrader: ok == false + diagnos (fail-safe).
//
// FIXTURERNAS RADSLUT är en förutsättning för 3 och mäts själva:
// `design_crlf` är CRLF och de andra LF, låst av rotens `.gitattributes`.
// Utan låset checkar core.autocrlf=true ut ALLT som CRLF — då mäts LF-vägen
// aldrig, och ingenting faller. Ingen förväntan
// här antar ett radslut: den kanoniska raden i 5 jämförs med ORIGINALETS.
//
// Calls-fixturens fönsteranrop är ARTON
// (`setSizeToContentMode` som nummer 15), `etikett.setWidthGroup("form")` är
// en Prop med literalen, och demons formulär (`form_dialog.h`) är byte-
// identiskt och klarar spar-policyn. SABOTAGET som ska fälla det:
// `widthGroup`-raden borttagen ur `kSetterFields` — writern skriver då
// `etikett.widthGroup = "form";` och fixturens byte-identitet faller (och
// calls_fixture kompilerar inte: fältet är privat).
//
// Fönsteranropen är TJUGOTVÅ — gränserna och den
// låsta storleken i innehållsmått (`setMinimumContentSize`,
// `setMaximumContentSize`, `setFixedContentSize`) och `setPopupTakesKeys`
// står efter avbrytknappen och kompileras av calls_fixture. SABOTAGET som
// ska fälla det: en av sättarna bortdöpt i app_window.h (calls_fixture
// kompilerar inte) — round-trippen själv är formens, inte namnens.
//
// `karta.setLayerSnap(0, false);` är en Call-sats
// med argumenten ordagranna, ingen Prop, och emitteras. SABOTAGEN som ska
// fälla det: `layerSnap` som setter-fält för ritytan (raden blir en
// Prop — fallet "snap" faller) och sättaren bortdöpt i canvas.h
// (calls_fixture kompilerar inte).
//
// Exit 0 = allt grönt; 1 = minst ett fall föll (varje fall skriver sin rad).

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cpp_reader.h"
#include "cpp_writer.h"
#include "window_model.h"

namespace {

int fel = 0;

void ok(bool cond, const std::string& vad)
{
    std::printf("%-6s %s\n", cond ? "OK" : "FEL", vad.c_str());
    if (!cond)
        ++fel;
}

std::string root()
{
    return std::string(GLAZE_TEST_ROOT) + "/";
}

std::string läs(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Radsluten i en fil, räknade på bytesen — testets EGEN räkning och inte
// readerns `detectNewline`, som svarar CRLF så fort EN rad har det.
// "BLANDAT" är ett eget svar: en fixtur med båda mäter ingen av vägarna.
std::string radslut(const std::string& bytes)
{
    size_t crlf = 0, lf = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != '\n')
            continue;
        if (i > 0 && bytes[i - 1] == '\r') ++crlf;
        else                               ++lf;
    }
    if (crlf && lf) return "BLANDAT";
    if (crlf)       return "CRLF";
    if (lf)         return "LF";
    return "INGA";
}

// Anropssatserna för en nod, i satsordning (satslistan är den enda
// sanningen om kroppen).
std::vector<const bld::Stmt*> anropen(const bld::WindowModel& m,
                                      const std::string& id)
{
    std::vector<const bld::Stmt*> ut;
    for (const bld::Stmt& s : m.stmts)
        if (s.kind == bld::Stmt::Kind::Call && s.target == id)
            ut.push_back(&s);
    return ut;
}

std::string basName(const std::string& p)
{
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// Läser tripletten och emitterar *.glazeui igen — returnerar bytes.
bool skrivOm(const std::string& hPath, std::string& ut, std::string& diagnos)
{
    bld::ReadResult r = bld::readWindow(hPath);
    if (!r.ok) {
        diagnos = r.error;
        return false;
    }
    ut = bld::emitGuiCpp(r.model, basName(hPath), r.cppNewline);
    return true;
}

void byteIdentisk(const std::string& hPath)
{
    std::string ut, diagnos;
    if (!skrivOm(hPath, ut, diagnos)) {
        ok(false, basName(hPath) + ": läsningen föll — " + diagnos);
        return;
    }
    const std::string original = läs(bld::guiPathFor(hPath));
    ok(ut == original, basName(hPath) + ": *.glazeui byte-identisk (" +
                           std::to_string(original.size()) + " byte)");
}

// SPAR-POLICYN: öppna en ORÖRD fil → spara →
// NOLL diff. `byteIdentisk` ovan mäter reader→writer; den här mäter hela
// ÖPPNINGSVÄGEN, alltså med migreringen inlagd precis där GlazeBuilder kör
// den. Skillnaden är hela poängen: migreringen är en avsiktlig ENGÅNGSdiff
// på filer skrivna före invarianten, och måste vara EXAKT noll på filer
// som redan är kanoniska. Missas det diffar varje spara för evigt.
void sparPolicy(const std::string& hPath)
{
    bld::ReadResult r = bld::readWindow(hPath);
    if (!r.ok) {
        ok(false, basName(hPath) + ": spar-policy — läsningen föll (" +
                      r.error + ")");
        return;
    }
    std::string fel;
    if (!bld::migrateNames(r.model, fel)) {
        ok(false, basName(hPath) + ": spar-policy — migreringen vägrade (" +
                      fel + ")");
        return;
    }
    const std::string ut =
        bld::emitGuiCpp(r.model, basName(hPath), r.cppNewline);
    ok(ut == läs(bld::guiPathFor(hPath)),
       basName(hPath) + ": ÖPPNA → SPARA ger noll diff (migreringen är "
       "redan gjord)");
}

} // namespace

int main()
{
    // ---- 1. Befintliga demo-tripletter (saknar tagg ⇒ noll diff)
    for (const char* f : {"apps/demo/src/start_window.h",
                          "apps/demo/src/second_window.h",
                          // Splashen — en Timer som
                          // medlem och en koppling vars sender är TIMERN.
                          "apps/demo/src/splash_window.h",
                          // Dockningen — `dockPanel` och
                          // `activate()` EFTER trädbygget, och ett barn i
                          // statusraden.
                          "apps/demo/src/dock_window.h",
                          // Formuläret — läget, breddgruppen,
                          // luften och syskonankare mot nederkanten.
                          "apps/demo/src/form_dialog.h",
                          // Galleriets triplett ligger här och inte i demon, där
                          // fönstret är HANDKODAT (`.glazeui` visas av startfönstret,
                          // handkodsvägen av galleriet). Den är grindens
                          // BREDASTE fixtur — alla tjugoen widgettyper, alla
                          // fältformer — och den täckningen skulle annars
                          // försvinna tyst med en demoändring. Fixturen
                          // är FRYST och underhålls inte med galleriet.
                          "tests/fixtures/gallery.h"})
        byteIdentisk(root() + f);

    // SPAR-POLICYN på demons KANONISKA tripletter. `gallery` står
    // med flit INTE här: den är FRYST legacy och migreringsgrindens
    // indata, alltså den enda fil i repot som SKA diffa.
    for (const char* f : {"apps/demo/src/start_window.h",
                          "apps/demo/src/second_window.h",
                          "apps/demo/src/splash_window.h",
                          "apps/demo/src/dock_window.h",
                          "apps/demo/src/form_dialog.h",
                          "tests/fixtures/calls.h",
                          "tests/fixtures/components.h",
                          "tests/fixtures/terminal.h"})
        sparPolicy(root() + f);

    // ---- 2/3/4. Fixturer som ska round-trippa byte-identiskt
    for (const char* f : {"tests/fixtures/design_tag.h",
                          "tests/fixtures/design_notag.h",
                          "tests/fixtures/design_partial.h",
                          "tests/fixtures/design_unknown.h",
                          "tests/fixtures/design_order.h",
                          "tests/fixtures/design_crlf.h",
                          "tests/fixtures/design_empty.h",
                          // Komponenterna i regionen,
                          // deras kopplingar och fönstrets anrop.
                          "tests/fixtures/components.h",
                          // Sidopaketets widget i regionen.
                          "tests/fixtures/terminal.h"})
        byteIdentisk(root() + f);

    // En NÄRVARANDE men innehållslös tagg (`// glazeui-design:` utan nycklar)
    // ska skrivas tillbaka, inte raderas. Skilt fall eftersom närvaron bor i
    // `hasDesign` och innehållet i `designLine`: hade writern villkorat på
    // strängen allena hade första spara tagit bort användarens rad, vilket
    // regel 4 förbjuder. byte-identiteten ovan täcker RADEN; det här täcker
    // MODELLEN, så en framtida refaktor inte kan slå ihop de två fälten igen.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/design_empty.h");
        ok(r.ok && r.model.hasDesign && r.model.designLine.empty(),
           "tom tagg: hasDesign true, designLine tom");
        bld::ReadResult n =
            bld::readWindow(root() + "tests/fixtures/design_notag.h");
        ok(n.ok && !n.model.hasDesign,
           "tagglös fixtur: hasDesign false");
    }

    // Taggens VÄRDEN ska komma tillbaka ordagrant, okänd nyckel med.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/design_unknown.h");
        ok(r.ok && r.model.designLine ==
                       "ui=Minimalistic mode=Dark future=42",
           "okänd nyckel bevaras i designLine: '" + r.model.designLine + "'");
        std::vector<bld::DesignPair> par;
        ok(bld::parseDesignLine(r.model.designLine, par) && par.size() == 3 &&
               par[2].key == "future" && par[2].value == "42",
           "parseDesignLine splittrar tre fält");
    }

    // CRLF: radsluten ska ha detekterats som \r\n.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/design_crlf.h");
        ok(r.ok && r.cppNewline == "\r\n", "CRLF-fixturen detekteras som CRLF");
    }

    // …och fixturerna HAR de radslut fallen ovan förutsätter. Faller raden
    // är det klonen och inte koden: `.gitattributes` har inte verkat (t.ex.
    // en utcheckning gjord före filen) — checka ut tests/fixtures/ på nytt.
    for (const char* f : {"tests/fixtures/design_crlf.h",
                          "tests/fixtures/design_crlf.glazeui"}) {
        const std::string eol = radslut(läs(root() + f));
        ok(eol == "CRLF", std::string(f) + ": radsluten är CRLF (" + eol +
                              ") — CRLF-vägen mäts");
    }
    for (const char* f : {"tests/fixtures/design_tag.h",
                          "tests/fixtures/design_tag.glazeui",
                          "tests/fixtures/design_misplaced.h",
                          "tests/fixtures/design_misplaced.glazeui"}) {
        const std::string eol = radslut(läs(root() + f));
        ok(eol == "LF", std::string(f) + ": radsluten är LF (" + eol +
                            ") — LF-vägen mäts");
    }

    // ---- 5. Tagg på FEL plats: läses, men emitteras kanoniskt (rad två).
    {
        const std::string h = root() + "tests/fixtures/design_misplaced.h";
        std::string ut, diagnos;
        const bool läst = skrivOm(h, ut, diagnos);
        ok(läst, "fel-placerad tagg: filen läses (" + diagnos + ")");
        if (läst) {
            const std::string original = läs(bld::guiPathFor(h));
            ok(ut != original,
               "fel-placerad tagg: INTE byte-identisk (normaliseras)");
            // PLATSEN normaliseras, radslutet inte: det är filens eget
            // (fall 3). Ett antaget "\n" skulle falla
            // på varje Windows-klon med autocrlf — en rätt rad i en CRLF-fil.
            const std::string nl =
                radslut(original) == "CRLF" ? "\r\n" : "\n";
            const std::string förväntad =
                "// glazeui-design: ui=Minimalistic theme=minimalistic(Teal)" +
                nl;
            const size_t rad2 = ut.find('\n') + 1;
            ok(ut.compare(rad2, förväntad.size(), förväntad) == 0,
               "fel-placerad tagg: emitteras på rad två (kanonisk plats)");
            int antal = 0;
            for (size_t i = ut.find(bld::kDesignPrefix);
                 i != std::string::npos;
                 i = ut.find(bld::kDesignPrefix, i + 1))
                ++antal;
            ok(antal == 1,
               "fel-placerad tagg: exakt EN designrad i utdata (" +
                   std::to_string(antal) + ")");
        }
    }

    // ---- METODANROPSSATSEN — den nya satsformen.
    //
    // OBS: fixturen `calls` KOMPILERAS också, av targetet `calls_fixture`
    // i tests/CMakeLists.txt. Det är avsiktligt och bärande: en grön
    // round-trip bevisar bara att reader och writer är överens, inte att
    // raden är riktig C++ — och `.glazeui` byggs.
    {
        const std::string h = root() + "tests/fixtures/calls.h";
        byteIdentisk(h);

        bld::ReadResult r = bld::readWindow(h);
        ok(r.ok, "calls: fixturen läses (" + r.error + ")");
        if (r.ok) {
            // `btn` bär också textfärgens två
            // former, `setColor(gfx::Color{…})` och `setColor(std::nullopt)` —
            // Call-satser, för `color` är inget setter-fält för knapparna —
            // medan `setIconSize(32)` är setter-fältet iconSize.
            const auto btn = anropen(r.model, "btn");
            ok(btn.size() == 4 && btn[0]->name == "disable" &&
                   btn[0]->value.empty() && btn[1]->name == "focus" &&
                   btn[2]->name == "setColor" &&
                   btn[2]->value == "gfx::Color{200, 40, 40}" &&
                   btn[3]->name == "setColor" &&
                   btn[3]->value == "std::nullopt",
               "calls: btn bär fyra anrop i källordning");

            const auto prog = anropen(r.model, "prog");
            ok(prog.size() == 1 && prog[0]->name == "setIndeterminate" &&
                   prog[0]->value == "true",
               "calls: argumentet bevaras ordagrant");

            // INBYGGD medlem: `statusBar` har varken medlemsdeklaration
            // eller adopt-rad, men blir en nod med `builtin` satt. 
            // Den bär också statusradens fält: fältens
            // fyra anrop efter `show()`.
            const bld::WidgetNode* sb = r.model.find("statusBar");
            const auto             sbc = anropen(r.model, "statusBar");
            ok(sb && sb->builtin && !r.model.isAdopted("statusBar") &&
                   sb->type == "glaze::StatusBar" && sbc.size() == 5 &&
                   sbc[0]->name == "show" && sbc[1]->name == "setPanels" &&
                   sbc[2]->name == "addPanel" && sbc[3]->name == "setPanel" &&
                   sbc[4]->name == "setPanelText",
               "calls: statusBar blir en BUILTIN-nod utan adopt-rad");

            // …och writern får varken deklarera eller adoptera den.
            const std::string region = bld::emitRegion(r.model, "\n");
            ok(region.find("statusBar") == std::string::npos,
               "calls: regionen deklarerar INTE statusBar (skulle skugga "
               "basklassens fält)");
            const std::string gui =
                bld::emitGuiCpp(r.model, "calls.h", "\n");
            ok(gui.find("adopt(statusBar)") == std::string::npos,
               "calls: ingen adopt-rad för statusBar (fönstret äger den)");
            ok(gui.find("    statusBar.show();\n") != std::string::npos,
               "calls: anropsraden emitteras");

            // MENYRADEN är den ANDRA inbyggda medlemmen
            // och mäts likadant — det är den assertionen som visar att
            // `builtinMember` är en tabell och inte ett specialfall.
            const bld::WidgetNode* mb  = r.model.find("menuBar");
            const auto             mbc = anropen(r.model, "menuBar");
            // ÅTTA anrop: `show`, den PLATTA menyformen,
            // där stigen är SEGMENTERAD och en posttext bär ett tr-id,
            // och menypostens tre TILLSTÅND
            // (`setItemIcon`/`setItemEnabled`/`setItemChecked` — Call-satser,
            // inga setter-fält). Argumenten ska stå ordagrant kvar.
            ok(mb && mb->builtin && !r.model.isAdopted("menuBar") &&
                   mb->type == "glaze::MenuBar" && mbc.size() == 8 &&
                   mbc[0]->name == "show",
               "calls: menuBar blir en BUILTIN-nod utan adopt-rad (" +
                   std::to_string(mbc.size()) + " anrop)");
            ok(mbc.size() == 8 && mbc[3]->name == "addRadio" &&
                   mbc[3]->value == "{\"Utseende\", \"Läge\"}, \"System\"",
               "calls: den SEGMENTERADE menystigen bevaras ordagrant" +
                   (mbc.size() == 8 ? " (" + mbc[3]->value + ")" : ""));
            ok(mbc.size() == 8 && mbc[4]->name == "addItem" &&
                   mbc[4]->value ==
                       "\"Utseende\", glaze::tr(3), \"Ctrl+Backspace\"",
               "calls: tr-text och KANONISKT genvägsnamn bevaras ordagrant");
            ok(mbc.size() == 8 && mbc[5]->name == "setItemIcon" &&
                   mbc[5]->value ==
                       "{\"Utseende\", \"Läge\"}, glaze::img(\"toolbar\", "
                       "\"info\")" &&
                   mbc[6]->name == "setItemEnabled" &&
                   mbc[6]->value == "{\"Utseende\", glaze::tr(3)}, false" &&
                   mbc[7]->name == "setItemChecked" &&
                   mbc[7]->value ==
                       "{\"Utseende\", \"Läge\", \"System\"}, true",
               "calls: menypostens tre tillstånd är Call-satser, ordagranna");
            ok(region.find("menuBar") == std::string::npos,
               "calls: regionen deklarerar INTE menuBar");
            ok(gui.find("adopt(menuBar)") == std::string::npos,
               "calls: ingen adopt-rad för menuBar (fönstret äger den)");
            ok(gui.find("    menuBar.show();\n") != std::string::npos,
               "calls: menyradens anropsrad emitteras");

            // PROGRAMIKONEN — den TREDJE inbyggda
            // medlemmen. Bilderna och de två bools är SETTER-fält (Prop i
            // modellen, anropsform i filen), programmenyn Call-satser där
            // den TOMMA stigen `{}` står ordagrant kvar som roten.
            const bld::WidgetNode* ai = r.model.find("appIcon");
            ok(ai && ai->builtin && !r.model.isAdopted("appIcon") &&
                   ai->type == "glaze::AppIcon",
               "calls: appIcon blir en BUILTIN-nod utan adopt-rad");
            auto prop = [&](const std::string& fält) -> const bld::Stmt* {
                for (const bld::Stmt& st : r.model.stmts)
                    if (st.kind == bld::Stmt::Kind::Prop &&
                        st.target == "appIcon" && st.name == fält)
                        return &st;
                return nullptr;
            };
            const bld::Stmt* ikon  = prop("icon");
            const bld::Stmt* liten = prop("smallIcon");
            const bld::Stmt* stor  = prop("large");
            const bld::Stmt* ingen = prop("noIcon");
            ok(ikon && ikon->value == "glaze::img(\"toolbar\", \"gear\")",
               "calls: appIcon.setIcon(glaze::img(…)) normaliseras till "
               "Prop{icon}");
            ok(liten && liten->value == "{}",
               "calls: appIcon.setSmallIcon({}) är en Prop med värdet {} "
               "(förvalet)");
            ok(stor && stor->value == "true" && ingen &&
                   ingen->value == "false",
               "calls: setLarge/setNoIcon är setter-fält");
            const auto aic = anropen(r.model, "appIcon");
            ok(aic.size() == 8 && aic[0]->name == "addItem" &&
                   aic[0]->value == "{}, glaze::tr(3), \"Ctrl+N\"",
               "calls: programmenyns rotstig {} bevaras ordagrant (" +
                   std::to_string(aic.size()) + " anrop)");
            ok(aic.size() == 8 && aic[2]->name == "addItem" &&
                   aic[2]->value == "\"Exportera\", \"PDF\"" &&
                   aic[4]->name == "addSeparator" && aic[4]->value == "{}",
               "calls: undermenyns stig och rotens avdelare bevaras");
            ok(aic.size() == 8 && aic[5]->name == "setItemIcon" &&
                   aic[5]->value ==
                       "\"Exportera\", glaze::img(\"toolbar\", \"gear\")" &&
                   aic[6]->name == "setItemEnabled" &&
                   aic[6]->value == "{\"Exportera\", \"PDF\"}, false" &&
                   aic[7]->name == "setItemChecked" &&
                   aic[7]->value == "\"Visa\", false",
               "calls: programmenyns tre posttillstånd är "
               "Call-satser, ordagranna");
            ok(region.find("appIcon") == std::string::npos,
               "calls: regionen deklarerar INTE appIcon");
            ok(gui.find("adopt(appIcon)") == std::string::npos,
               "calls: ingen adopt-rad för appIcon (fönstret äger den)");
            ok(gui.find("    appIcon.setSmallIcon({});\n") !=
                   std::string::npos,
               "calls: {}-bilden emitteras i anropsform");

            // DOCKNINGEN: värdens `dockPanel` i båda formerna
            // med IDENTIFIERARE som argument, panelens min/max och
            // `activate()` är Call-satser och står ORDAGRANT kvar; stängknappen
            // och ikonen är setter-fält (Prop i modellen, anropsform i filen).
            // Och ORDNINGEN: anropen står EFTER trädbygget i den emitterade
            // filen — appen kör källordning, och `dockPanel` gäller barn.
            const auto dk = anropen(r.model, "dock1");
            ok(dk.size() == 3 && dk[0]->name == "dockPanel" &&
                   dk[0]->value == "utforskare, glaze::Dock::Left" &&
                   dk[1]->value == "sok, utforskare, glaze::Dock::Fill" &&
                   dk[2]->value == "egenskaper, utforskare, glaze::Dock::Bottom",
               "calls: dock1.dockPanel i båda formerna, ordagrant (" +
                   std::to_string(dk.size()) + " anrop)");
            const auto ut = anropen(r.model, "utforskare");
            ok(ut.size() == 4 && ut[0]->name == "setMinimumSize" &&
                   ut[0]->value == "160, 0" &&
                   ut[1]->name == "setMaximumSize" &&
                   ut[1]->value == "400, 0" &&
                   ut[2]->name == "setFloatingBounds" &&
                   ut[2]->value == "60, 40, 280, 320" &&
                   ut[3]->name == "activate" && ut[3]->value.empty(),
               "calls: panelens min/max, flytande plats och "
               "activate() är Call-satser");
            auto dockProp = [&](const std::string& mål,
                                const std::string& fält) -> const bld::Stmt* {
                for (const bld::Stmt& st : r.model.stmts)
                    if (st.kind == bld::Stmt::Kind::Prop && st.target == mål &&
                        st.name == fält)
                        return &st;
                return nullptr;
            };
            const bld::Stmt* bild  = dockProp("utforskare", "img");
            const bld::Stmt* stäng = dockProp("sok", "closable");
            ok(bild && bild->value == "glaze::img(\"toolbar\", \"gear\")" &&
                   stäng && stäng->value == "false",
               "calls: setImg/setClosable på en panel är setter-fält");
            // KNAPPNÅLEN är ett setter-fält: Prop i modellen,
            // `setPinned(false)` i filen (byte-identiteten ovan).
            const bld::Stmt* nål = dockProp("egenskaper", "pinned");
            ok(nål && nål->value == "false",
               "calls: egenskaper.setPinned(false) är setter-fältet pinned");
            // FLYTBARHETEN är ett setter-fält: Prop i modellen,
            // `setFloatable(false)` i filen (byte-identiteten ovan).
            const bld::Stmt* flytbar = dockProp("sok", "floatable");
            ok(flytbar && flytbar->value == "false",
               "calls: sok.setFloatable(false) är setter-fältet floatable");
            // VERKTYGSFÄLTETS BAND är ett setter-fält: Prop i
            // modellen, `setBand(2)` i filen (byte-identiteten ovan).
            const bld::Stmt* band = dockProp("verktygsrad", "band");
            ok(band && band->value == "2",
               "calls: verktygsrad.setBand(2) är setter-fältet band");
            // ÖVERFLÖDET är ett setter-fält: Prop i modellen,
            // `setOverflow(false)` i filen (byte-identiteten ovan).
            const bld::Stmt* över = dockProp("verktygsrad", "overflow");
            ok(över && över->value == "false",
               "calls: verktygsrad.setOverflow(false) är setter-fältet "
               "overflow");
            // DET FLYTANDE VERKTYGSFÄLTET: titeln, det
            // flytande läget och flytbarheten är setter-fält (Prop i
            // modellen, anropsform i filen — byte-identiteten ovan), och
            // platsen är en Call-sats med två tal, ordagrann.
            const bld::Stmt* fText    = dockProp("flytrad", "text");
            const bld::Stmt* fFlyt    = dockProp("flytrad", "floating");
            const bld::Stmt* vFlytbar = dockProp("verktygsrad", "floatable");
            ok(fText && fText->value == "glaze::tr(166)" && fFlyt &&
                   fFlyt->value == "true" && vFlytbar &&
                   vFlytbar->value == "false",
               "calls: flytrad.setText/setFloating och "
               "verktygsrad.setFloatable är setter-fält");
            const auto fr = anropen(r.model, "flytrad");
            ok(fr.size() == 1 && fr[0]->name == "setFloatingPosition" &&
                   fr[0]->value == "500, 80",
               "calls: flytrad.setFloatingPosition(500, 80) är en Call-sats "
               "med två tal, ordagrann (" +
                   std::to_string(fr.size()) + " anrop)");
            const std::size_t trädet = gui.find("    dock1.adopt(egenskaper);\n");
            const std::size_t dockat =
                gui.find("    dock1.dockPanel(sok, utforskare, "
                         "glaze::Dock::Fill);\n");
            const std::size_t aktiv = gui.find("    utforskare.activate();\n");
            ok(trädet != std::string::npos && dockat != std::string::npos &&
                   aktiv != std::string::npos && trädet < dockat &&
                   dockat < aktiv,
               "calls: dockPanel och activate emitteras EFTER trädbygget");
        }
    }

    // ---- LAGRETS SNAP: `karta.setLayerSnap(0, false);` är en
    // Call-sats med sina två argument ordagranna — inte en opak Prop — och
    // står kvar i den emitterade filen (byte-identiteten ovan täcker raden,
    // det här modellen).
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "snap: calls.h läses (" + r.error + ")");
        const bld::Stmt* snap = nullptr;
        if (r.ok)
            for (const bld::Stmt* s : anropen(r.model, "karta"))
                if (s->name == "setLayerSnap")
                    snap = s;
        ok(snap && snap->value == "0, false" &&
               r.model.findProp("karta", "layerSnap") == nullptr &&
               bld::emitGuiCpp(r.model, "calls.h", "\n")
                       .find("    karta.setLayerSnap(0, false);\n") !=
                   std::string::npos,
           "snap: karta.setLayerSnap(0, false) är en Call-sats, ordagrann, och "
           "emitteras");
    }

    // ---- DEN KORTA EVENT-FORMEN. Byte-identiteten ovan
    // täcker RADERNA; det här täcker MODELLEN, och det är den assertionen
    // som mäter att formen känns igen: raderna ska vara `Kind::Event` med `shortForm` och
    // INTE opaka `Prop`. Blev de Prop såg buildern knappen som okopplad
    // och skrev en ANDRA `onClick` vid nästa dubbelklick — den
    // handskrivna handlern slutade köras utan varning eller
    // kompileringsfel.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "kort form: fixturen läses (" + r.error + ")");
        if (r.ok) {
            const bld::WindowModel& m = r.model;
            auto sats = [&](bld::Stmt::Kind k, const std::string& target,
                            const std::string& namn) -> const bld::Stmt* {
                for (const bld::Stmt& s : m.stmts)
                    if (s.kind == k && s.target == target && s.name == namn)
                        return &s;
                return nullptr;
            };

            // 1. KORT WIDGETFORM ⇒ Event + shortForm + tom args.
            const bld::Stmt* kort = sats(bld::Stmt::Kind::Event, "btn",
                                         "onClick");
            ok(kort && kort->shortForm && kort->handler == "knappTryckt" &&
                   kort->args.empty(),
               "kort form: btn.onClick är Kind::Event med shortForm");

            // 2. FULLFORMEN bredvid ⇒ Event UTAN shortForm.
            const bld::Stmt* full = sats(bld::Stmt::Kind::Event, "btn",
                                         "onHover");
            ok(full && !full->shortForm && full->args == "glaze::HoverArgs" &&
                   full->handler == "knappHovrad",
               "kort form: fullformen bredvid är oförändrad");

            // 3. KORT FÖNSTERFORM ⇒ WindowEvent + shortForm.
            const bld::Stmt* win = sats(bld::Stmt::Kind::WindowEvent, "",
                                        "onLoad");
            ok(win && win->shortForm && win->handler == "fonstretLaddat",
               "kort form: glaze::AppWindow::onLoad är WindowEvent med "
               "shortForm");

            // 4. MISSLYCKAD kortformsparse på en WIDGET-rad ⇒ opak `Prop`,
            // ALDRIG fail-safe. `[this] { medArgument(1); }` är giltig C++
            // som glaze::Handler tar emot, men inte ett naket
            // parameterlöst anrop.
            const bld::Stmt* prop = m.findProp("panel", "onClick");
            ok(prop && prop->value == "[this] { medArgument(1); }" &&
                   sats(bld::Stmt::Kind::Event, "panel", "onClick") ==
                       nullptr,
               "kort form: kropp med argument faller till opak Prop");

            // 5. REGRESSIONSMÄTNINGEN: sekvensen inuti ett STRÄNGVÄRDE ska
            // förbli en opak `Prop` — varken `Event` eller fail-safe. En
            // breddad `find` utan det TOPPNIVÅ-ledet hade skickat raden
            // till strikt kortformsparse och fällt hela dokumentet.
            // (`name` är en KANONISK rad som måste bära nodnamnet, så testvärdet
            // ligger i `setText` — samma rad-form, samma fälla.)
            const bld::Stmt* str = m.findProp("btn", "text");
            ok(str && str->value == "\"onClick = [this] { fel(); };\"",
               "kort form: sekvens inuti strängvärde förblir opak Prop");

            // 6. RYMDENS TVÅ EVENTS: `onTick` i den
            // KORTA formen är en koppling bara om namnet står i
            // `isKnownEvent` — annars en opak Prop som round-trippar lika
            // byte-exakt, och byte-identiteten ovan hade inte märkt något.
            const bld::Stmt* tick = sats(bld::Stmt::Kind::Event, "karta",
                                         "onTick");
            const bld::Stmt* gest = sats(bld::Stmt::Kind::Event, "karta",
                                         "onGesture");
            ok(tick && tick->shortForm && tick->handler == "kartaTickar" &&
                   gest && !gest->shortForm &&
                   gest->args == "glaze::GestureArgs" &&
                   m.findProp("karta", "onTick") == nullptr,
               "kort form: karta.onTick (kort) och karta.onGesture (full) är "
               "Kind::Event");

            // 7. COMMIT, FOKUS OCH KRYSSET: de korta
            // formerna är kopplingar bara om namnen står i `isKnownEvent` —
            // annars opaka Prop som round-trippar lika byte-exakt (spar-
            // policyn ovan hade inte märkt något). Fullformerna bär sina args.
            const bld::Stmt* kryss  = sats(bld::Stmt::Kind::Event, "lista",
                                           "onCheck");
            const bld::Stmt* gridC  = sats(bld::Stmt::Kind::Event, "grid",
                                           "onCommit");
            const bld::Stmt* faltC  = sats(bld::Stmt::Kind::Event, "falt",
                                           "onCommit");
            const bld::Stmt* faltF  = sats(bld::Stmt::Kind::Event, "falt",
                                           "onFocus");
            const bld::Stmt* panelL = sats(bld::Stmt::Kind::Event, "panel",
                                           "onFocusLost");
            ok(kryss && kryss->shortForm && kryss->handler == "listaKryssad" &&
                   m.findProp("lista", "onCheck") == nullptr,
               "kort form: lista.onCheck (kort) är Kind::Event");
            ok(gridC && !gridC->shortForm && gridC->args == "glaze::ChangeArgs" &&
                   gridC->handler == "gridSparad" && faltC && faltC->shortForm &&
                   faltC->handler == "faltKlart" &&
                   m.findProp("falt", "onCommit") == nullptr,
               "onCommit: grid (full) och falt (kort) är Kind::Event");
            ok(faltF && !faltF->shortForm && faltF->args == "glaze::FocusArgs" &&
                   faltF->handler == "faltFokus" && panelL &&
                   panelL->shortForm && panelL->handler == "panelLamnad" &&
                   m.findProp("panel", "onFocusLost") == nullptr,
               "onFocus (full, glaze::FocusArgs) och onFocusLost (kort) är "
               "Kind::Event");
            // 8. INMATNINGEN: tangenterna på VARJE
            // widget och ritytans avbrott — varje nytt event i BÅDA formerna
            // är en koppling (`isKnownEvent`) och ingen opak Prop. En opak
            // Prop hade round-trippat LIKA byte-exakt, så det är
            // klassningen som mäter.
            const auto händelser = [&](const std::string& target,
                                       const std::string& namn) {
                std::vector<const bld::Stmt*> ut;
                for (const bld::Stmt& s : m.stmts)
                    if (s.kind == bld::Stmt::Kind::Event &&
                        s.target == target && s.name == namn)
                        ut.push_back(&s);
                return ut;
            };
            const bld::Stmt* faltNer = sats(bld::Stmt::Kind::Event, "falt",
                                            "onKeyDown");
            const bld::Stmt* faltUpp = sats(bld::Stmt::Kind::Event, "falt",
                                            "onKeyUp");
            const bld::Stmt* tabNer  = sats(bld::Stmt::Kind::Event, "tabell",
                                            "onKeyDown");
            const bld::Stmt* kartUpp = sats(bld::Stmt::Kind::Event, "karta",
                                            "onKeyUp");
            const auto       avbrott = händelser("karta", "onMouseCancel");
            ok(faltNer && !faltNer->shortForm &&
                   faltNer->args == "glaze::KeyArgs" &&
                   faltNer->handler == "faltTangent" && tabNer &&
                   tabNer->shortForm && tabNer->handler == "tabellTangent",
               "inmatning: onKeyDown på falt (full, glaze::KeyArgs) och tabell "
               "(kort) är Kind::Event — basens fält");
            ok(faltUpp && faltUpp->shortForm && faltUpp->handler == "faltSlapp" &&
                   kartUpp && !kartUpp->shortForm &&
                   kartUpp->args == "glaze::KeyArgs" &&
                   kartUpp->handler == "kartaSlapp" &&
                   m.findProp("falt", "onKeyUp") == nullptr &&
                   m.findProp("karta", "onKeyUp") == nullptr,
               "inmatning: onKeyUp på falt (kort) och karta (full) är "
               "Kind::Event och ingen opak Prop");
            ok(avbrott.size() == 2 && !avbrott[0]->shortForm &&
                   avbrott[0]->args == "glaze::CanvasMouseArgs" &&
                   avbrott[0]->handler == "kartaAvbruten" &&
                   avbrott[1]->shortForm &&
                   avbrott[1]->handler == "kartaAvbrott" &&
                   m.findProp("karta", "onMouseCancel") == nullptr,
               "inmatning: karta.onMouseCancel i båda formerna (full, "
               "glaze::CanvasMouseArgs, och kort) är Kind::Event");
            // 9. APPENS MUS PÅ VARJE WIDGET: basens
            // `onMouseDown` (full, `glaze::MouseArgs`) och `onMouseCancel`
            // (kort) på en KNAPP — kopplingar, inga opaka Prop.
            const bld::Stmt* knappNer = sats(bld::Stmt::Kind::Event, "btn",
                                             "onMouseDown");
            const bld::Stmt* knappAvb = sats(bld::Stmt::Kind::Event, "btn",
                                             "onMouseCancel");
            ok(knappNer && !knappNer->shortForm &&
                   knappNer->args == "glaze::MouseArgs" &&
                   knappNer->handler == "knappNer" && knappAvb &&
                   knappAvb->shortForm && knappAvb->handler == "knappAvbruten" &&
                   m.findProp("btn", "onMouseDown") == nullptr &&
                   m.findProp("btn", "onMouseCancel") == nullptr,
               "appens mus: btn.onMouseDown (full, glaze::MouseArgs) och "
               "btn.onMouseCancel (kort) är Kind::Event — basens fält");
            // 10. STORLEKEN: basens `onResize` på
            // ritytan (full, `glaze::ResizeArgs`) och på fältet (kort) —
            // kopplingar och inga opaka Prop. Sabotage:
            // raden bort ur `isKnownEvent`.
            const bld::Stmt* kartStr = sats(bld::Stmt::Kind::Event, "karta",
                                            "onResize");
            const bld::Stmt* faltStr = sats(bld::Stmt::Kind::Event, "falt",
                                            "onResize");
            ok(kartStr && !kartStr->shortForm &&
                   kartStr->args == "glaze::ResizeArgs" &&
                   kartStr->handler == "kartaStorlek" && faltStr &&
                   faltStr->shortForm && faltStr->handler == "faltStorlek" &&
                   m.findProp("falt", "onResize") == nullptr &&
                   m.findProp("karta", "onResize") == nullptr,
               "storleken: karta.onResize (full, glaze::ResizeArgs) och "
               "falt.onResize (kort) är Kind::Event");

            // Markeringens två anrop är vanliga Call-satser i källordning.
            const auto falt = anropen(m, "falt");
            ok(falt.size() == 2 && falt[0]->name == "selectAll" &&
                   falt[0]->value.empty() && falt[1]->name == "setSelection" &&
                   falt[1]->value == "0, 2",
               "markeringen: falt.selectAll() och falt.setSelection(0, 2) är "
               "Call-satser, argumenten ordagrant");
        }
    }

    // Okänt METODNAMN är OPAKT, aldrig fail-safe (regel 4: en okänd metod
    // är samma sak som ett okänt fält). `focus()` i fixturen ovan är den
    // levande varianten; här mäts att en helt påhittad metod läses in på
    // samma sätt i stället för att fälla filen.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        const auto panel = r.ok ? anropen(r.model, "panel")
                                : std::vector<const bld::Stmt*>{};
        ok(panel.size() == 1 && panel[0]->name == "showScrollbars" &&
               panel[0]->value == "true",
           "calls: widget-specifikt anrop med argument");
    }

    // ---- VERKTYGSKNAPPENS RULLGARDIN. Byte-identiteten ovan
    //      täcker RADERNA; det här täcker KLASSNINGEN — och den är hela
    //      grinden för `onDropDown`, för en kort koppling vars namn saknas i
    //      `isKnownEvent` blir en opak Prop som round-trippar LIKA
    //      byte-exakt. `setSplit(true)` är setter-fältet `split`,
    //      och menyn med `{}`-roten och postens tre tillstånd är Call-satser
    //      vars argument står ordagrant kvar.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "rullgardin: calls.h läses (" + r.error + ")");
        const bld::WindowModel& m = r.model;
        const bld::WidgetNode*  n = m.find("nyKnapp");
        ok(n && n->type == "glaze::ToolButton" && m.isAdopted("nyKnapp"),
           "rullgardin: nyKnapp är en adopterad glaze::ToolButton");
        const bld::Stmt* delad = m.findProp("nyKnapp", "split");
        ok(delad && delad->value == "true" &&
               bld::isSetterField("glaze::ToolButton", "split"),
           "rullgardin: setSplit(true) normaliseras till setter-fältet split");
        const auto  anrop = anropen(m, "nyKnapp");
        std::string följd;
        for (const bld::Stmt* s : anrop)
            följd += s->name + " ";
        ok(följd == "addItem addSubmenu addItem addCheck addRadio addSeparator "
                    "setItemIcon setItemEnabled setItemChecked ",
           "rullgardin: menyns nio satser är Call-satser i källordning (" +
               följd + ")");
        ok(anrop.size() == 9 && anrop[0]->value == "{}, \"Dokument\"" &&
               anrop[2]->value == "\"Mall\", glaze::tr(3)" &&
               anrop[4]->value == "{}, \"Liten\", true" &&
               anrop[5]->value == "{}",
           "rullgardin: {}-roten, undermenyns stig och tr-texten bevaras "
           "ordagrant");
        ok(anrop.size() == 9 &&
               anrop[6]->value ==
                   "\"Dokument\", glaze::img(\"toolbar\", \"info\")" &&
               anrop[7]->value == "{\"Mall\", glaze::tr(3)}, false" &&
               anrop[8]->value == "\"Visa\", false",
           "rullgardin: postens tre tillstånd bevaras ordagrant");
        const bld::Stmt* dd = nullptr;
        for (const bld::Stmt& s : m.stmts)
            if (s.kind == bld::Stmt::Kind::Event && s.target == "nyKnapp" &&
                s.name == "onDropDown")
                dd = &s;
        ok(dd && dd->shortForm && dd->handler == "knappMeny" &&
               m.findProp("nyKnapp", "onDropDown") == nullptr,
           "rullgardin: nyKnapp.onDropDown (kort) är Kind::Event och ingen "
           "opak Prop");
        const std::string gui = bld::emitGuiCpp(m, "calls.h", "\n");
        ok(gui.find("    nyKnapp.setSplit(true);\n") != std::string::npos &&
               gui.find("    nyKnapp.onDropDown = [this] { knappMeny(); };\n") !=
                   std::string::npos,
           "rullgardin: setSplit emitteras i anropsform och kopplingen KORT");
    }

    // ---- LABEL::WRAP. `wrap` är en SETTER-egenskap,
    // alltså en `Prop` i modellen som writern emitterar som `setWrap(true)`.
    // Byte-identiteten ovan täcker RADEN; det här täcker MODELLEN, så en
    // egenskap som glömts i `kSetterFields` (och därför blivit en opak
    // fältrad) faller här i stället för att tyst tappa sin form.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        bool nod = false, hittad = false;
        if (r.ok) {
            for (const bld::WidgetNode& x : r.model.nodes)
                if (x.id == "etikett")
                    nod = true;
            for (const bld::Stmt& st : r.model.stmts)
                if (st.kind == bld::Stmt::Kind::Prop &&
                    st.target == "etikett" && st.name == "wrap" &&
                    st.value == "true")
                    hittad = true;
        }
        ok(nod, "wrap: noden etikett finns i modellen");
        ok(hittad, "wrap: setWrap(true) normaliseras till fältet wrap=true");
        ok(bld::isSetterField("glaze::Label", "wrap"),
           "wrap: står i settertabellen FÖR Label (emitteras som anrop)");
    }

    // ---- KOLUMNHUVUDET. Byte-identiteten (spar-policyn ovan)
    //      täcker RADERNA; det här täcker KLASSNINGEN: huvudets två sättare
    //      är Props med ordagranna värden, kolumnernas fyra-fältsformer
    //      Call-satser vars argument — `\n` i rubriken, `{}` och
    //      `std::nullopt` på fjärde platsen — står kvar tecken för tecken,
    //      och dropdownens `setAlign` är en Prop.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "huvudet: calls.h läses (" + r.error + ")");
        auto prop = [&r](const char* target, const char* fält) {
            const bld::Stmt* hit = nullptr;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Prop && s.target == target &&
                    s.name == fält)
                    hit = &s;
            return hit;
        };
        const bld::Stmt* rader = prop("tabell", "headerLines");
        const bld::Stmt* hjust = prop("tabell", "headerVAlign");
        ok(rader && rader->value == "4",
           "huvudet: setHeaderLines(4) normaliseras till fältet headerLines");
        ok(hjust && hjust->value == "glaze::VAlign::Bottom",
           "huvudet: setHeaderVAlign behåller enumtexten ordagrant");
        const auto anrop = anropen(r.model, "tabell");
        std::string följd;
        for (const bld::Stmt* s : anrop)
            följd += s->name + " ";
        // Därefter radfärgens satser (`addRow`/
        // `setRow` med tredje argumentet, `setRows` med tre fält, designerat
        // och typat, `setRowColor`) — också de Call-satser i källordning — och
        // kryssets `setChecked(1, true)` SIST (två
        // argument, en Call; `setShowChecks` är ett setter-fält och står inte
        // i följden) och `setCellColor` ×2 och `setColumnWidth`
        // (Call-satser; `setRowHeaders` är ett setter-fält).
        ok(följd == "setColumns addColumn setColumn addRow addRow setRow "
                    "setRows setRowColor setRowColor setChecked setCellColor "
                    "setCellColor setColumnWidth ",
           "huvudet: kolumnerna och raderna är Call-satser i källordning (" +
               följd + ")");
        ok(anrop.size() == 13 &&
               anrop[4]->value == "{\"2b\", \"64,0\"}, {}, gfx::Color{0, 120, 0}" &&
               anrop[6]->value ==
                   "{{{\"a\"}, {}, gfx::Color{200, 0, 0}}, {{\"b\"}}, "
                   "{.cells = {\"c\"}, .color = gfx::Color{0, 140, 0}}, "
                   "glaze::Table::Row{{\"d\"}}}" &&
               anrop[8]->value == "0, {}",
           "radfärgen: tredje argumentet, de tre radformerna och {} bevaras "
           "ordagrant");
        ok(anrop.size() == 13 &&
               anrop[0]->value ==
                   "{{\"SS-EN 50341\\nStandard\\nload cases\", 90, "
                   "glaze::Align::Right, glaze::Align::Center}, {\"Spann\", "
                   "50}, {\"C\", 40, glaze::Align::Left, {}}, {\"D\", 40, "
                   "glaze::Align::Center, std::nullopt}}",
           "huvudet: setColumns med FJÄRDE fältet bevaras ordagrant — \\n, {} "
           "och std::nullopt");
        ok(anrop.size() == 13 &&
               anrop[1]->value == "\"Nedhäng\\nb\\n(m)\", 70, "
                                  "glaze::Align::Right, glaze::Align::Center",
           "huvudet: addColumn med fyra argument bevaras ordagrant");
        const bld::Stmt* dj = prop("dd", "align");
        ok(dj && dj->value == "glaze::Align::Right",
           "huvudet: dropdownens setAlign normaliseras till fältet align");
        // TEXTSTORLEKEN: ett SETTER-fält på de fyra, måttet
        // ordagrant — `glaze::mm(…)`, `glaze::px(…)` och ett naket tal.
        const bld::Stmt* tf = prop("falt", "textSize");
        const bld::Stmt* ta = prop("anteckning", "textSize");
        const bld::Stmt* td = prop("dd", "textSize");
        const bld::Stmt* tl = prop("lista", "textSize");
        ok(tf && tf->value == "glaze::mm(3)" && ta &&
               ta->value == "glaze::px(16)" && td && td->value == "14" && tl &&
               tl->value == "glaze::mm(2.5)",
           "textstorleken: setTextSize är ett setter-fält på fält, flerradigt "
           "fält, vallista och lista, måttet ordagrant");
    }

    // ---- KOLLEKTIONSVOKABULÄREN + TYPMEDVETEN SETTERTABELL
    //      . Byte-identiteten ovan täcker
    //      RADERNA; det här täcker KLASSNINGEN, alltså exakt den plats där
    //      de tre buggarna satt.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "vokabulär: calls.h läses (" + r.error + ")");

        auto prop = [&r](const char* target,
                         const char* fält) -> const bld::Stmt* {
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Prop && s.target == target &&
                    s.name == fält)
                    return &s;
            return nullptr;
        };

        // 1. KOLLISIONEN: `lista.setChecked(1, true);` får inte bli en
        //    `Prop{checked, "1, true"}` — Checkbox/Radios BOOL-fält
        //    med en ListBox-signatur i värdet round-trippar av en slump
        //    men utförs aldrig i previewn.
        //    REGRESSIONSLÅSET är att den INTE är en Prop.
        {
            const auto lista = anropen(r.model, "lista");
            bool       call  = false;
            for (const bld::Stmt* s : lista)
                if (s->name == "setChecked" && s->value == "1, true")
                    call = true;
            ok(call, "setChecked: lagras som Kind::Call på en ListBox");
            ok(prop("lista", "checked") == nullptr,
               "setChecked: blir ALDRIG en checked-Prop (den namn-globala "
               "felklassen)");
        }

        // 2. STYCKEFORMEN i KÄLLORDNING — och varvad med en okänd metod
        //    och en Prop, så ordningen mäts och inte bara förekomsten.
        {
            const auto g = anropen(r.model, "grid");
            std::string följd;
            for (const bld::Stmt* s : g)
                följd += s->name + " ";
            ok(följd == "setRows addRow setScroll setRow addRow ",
               "grid: anropen i KÄLLORDNING, varvade med okänd metod (" +
                   följd + ")");
            ok(g.size() >= 1 &&
                   g[0]->value ==
                       "{{\"Bredd\", \"120\"}, {\"Höjd\", \"80\", {}, true, "
                       "\"h\"}}",
               "grid: setRows-literalen bevaras ordagrant");
        }

        // 3. `setNameWidth` normaliseras till en Prop — fältet står i
        //    tabellen.
        {
            const bld::Stmt* p = prop("grid", "nameWidth");
            ok(p && p->value == "140",
               "nameWidth: setNameWidth(140) normaliseras till Prop");
        }

        // 4. `setColor` blir en CALL — `color` finns inte i tabellen:
        //    nyckeln skulle vara ett ALIAS för `role` och vill ha
        //    ett ROLLNAMN, medan settern tar en riktig gfx::Color.
        {
            const auto e = anropen(r.model, "etikett");
            bool       call = false;
            for (const bld::Stmt* s : e)
                if (s->name == "setColor" &&
                    s->value == "gfx::Color{40, 90, 180, 255}")
                    call = true;
            ok(call, "setColor: lagras som Kind::Call med literalen intakt");
            ok(prop("etikett", "color") == nullptr,
               "setColor: blir ALDRIG en color-Prop");
        }

        // 5. ListBox/Dropdown-styckeformen, båda typerna.
        {
            const auto l = anropen(r.model, "lista");
            const auto d = anropen(r.model, "dd");
            bool addL = false, setL = false, addD = false, setD = false;
            for (const bld::Stmt* s : l) {
                addL |= s->name == "addItem" && s->value == "\"Delta\"";
                setL |= s->name == "setItem" && s->value == "0, \"ALFA\"";
            }
            for (const bld::Stmt* s : d) {
                addD |= s->name == "addItem" && s->value == "\"Tre\"";
                setD |= s->name == "setItem" && s->value == "1, \"TVÅ\"";
            }
            ok(addL && setL, "lista: addItem/setItem är Calls");
            ok(addD && setD, "dd: addItem/setItem är Calls (samma form)");
        }
    }

    // ---- LISTVYN i galleriets åttonde kolumn. Byte-identiteten
    //      ovan täcker RADERNA; det här täcker KLASSNINGEN: egenskaperna
    //      (sortering, flerval, val) är setter-FÄLT och normaliseras till
    //      Prop, kollektionen (kolumner, rader, celler, nycklar, en rad i
    //      valet) är Call-satser med argumenten ORDAGRANT, och aktiveringen
    //      i den korta formen är en koppling och ingen opak rad.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "listvy: galleriet läses (" + r.error + ")");
        if (r.ok) {
            auto prop = [&r](const char* target, const char* fält,
                             const char* värde) {
                for (const bld::Stmt& s : r.model.stmts)
                    if (s.kind == bld::Stmt::Kind::Prop &&
                        s.target == target && s.name == fält)
                        return s.value == värde;
                return false;
            };
            auto anrop = [&r](const char* target, const char* metod,
                              const char* args) {
                for (const bld::Stmt* s : anropen(r.model, target))
                    if (s->name == metod && s->value == args)
                        return true;
                return false;
            };
            ok(prop("listaN", "sortColumn", "0") &&
                   prop("listaN", "multiSelect", "true") &&
                   prop("listaD", "sortAscending", "false") &&
                   prop("listaD", "selectedIndex", "0"),
               "listvy: setSortColumn/setMultiSelect/setSortAscending/"
               "setSelectedIndex normaliseras till Prop");
            ok(anrop("listaN", "addColumn",
                     "\"Storlek\", 60, glaze::Align::Right") &&
                   anrop("listaN", "addRow",
                         "{\"Rapport.pdf\", \"12 kB\", \"2026-10-07\"}, "
                         "glaze::img(\"toolbar\", \"info\")") &&
                   anrop("listaN", "setSortKey", "1, 1, 3145728") &&
                   anrop("listaN", "setCell", "3, 2, \"2026-01-03\"") &&
                   anrop("listaN", "setSelected", "2, true"),
               "listvy: styckeformen är Call-satser med argumenten intakta");
            ok(anrop("listaD", "setColumns",
                     "{{\"Namn\", 128}, {\"Storlek\", 60, "
                     "glaze::Align::Right}, {\"Ändrad\", 88}}") &&
                   anrop("listaD", "setRows",
                         "{{{\"Rapport.pdf\", \"12 kB\", \"2026-10-07\"}, "
                         "glaze::img(\"toolbar\", \"info\")}, "
                         "{{\"Anteckningar.txt\", \"900 B\", "
                         "\"2026-08-19\"}, {}, gfx::Color{200, 40, 40}}}") &&
                   anrop("listaN", "setRowColor", "3, gfx::Color{0, 140, 0}"),
               "listvy: massformens aggregatliteraler (också radens TREDJE "
               "fält, färgen) och setRowColor bevaras ordagrant");
            // KRYSSEN: synligheten är ett setter-fält,
            // `setChecked(rad, på)` en Call-sats — på båda listvyerna.
            ok(prop("listaN", "showChecks", "true") &&
                   prop("listaD", "showChecks", "true") &&
                   anrop("listaN", "setChecked", "1, true") &&
                   anrop("listaD", "setChecked", "0, true") &&
                   r.model.findProp("listaN", "checked") == nullptr,
               "listvy: setShowChecks normaliseras till Prop, setChecked är "
               "en Call-sats och ingen fältrad `checked`");
            const bld::Stmt* akt = nullptr;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Event && s.target == "listaN" &&
                    s.name == "onActivate")
                    akt = &s;
            ok(akt && akt->shortForm && akt->handler == "listaOppnad",
               "listvy: listaN.onActivate (kort form) är en koppling");
        }
    }

    // ---- DATAGRIDDEN under listvyn. Samma KLASSNING: sorteringen
    //      är setter-FÄLT, kolumntypen, valen och den aktuella cellen är
    //      Call-satser med argumenten ORDAGRANT — även när kolumntypen står
    //      FÖRE kolumnerna — och `onChange` bär sin egen args-typ
    //      (`CellChangeArgs`), som readern ska bevara och inte normalisera.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "datagrid: galleriet läses (" + r.error + ")");
        if (r.ok) {
            auto anrop = [&r](const char* target, const char* metod,
                              const char* args) {
                for (const bld::Stmt* s : anropen(r.model, target))
                    if (s->name == metod && s->value == args)
                        return true;
                return false;
            };
            bool sortProp = false;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Prop && s.target == "dataD" &&
                    s.name == "sortColumn" && s.value == "2")
                    sortProp = true;
            ok(sortProp, "datagrid: setSortColumn normaliseras till Prop");
            ok(anrop("dataN", "setColumnEditor",
                     "1, glaze::DataGrid::Editor::Choice") &&
                   anrop("dataN", "setColumnChoices",
                         "1, {\"Ny\", \"Pågår\", \"Klar\"}") &&
                   anrop("dataN", "setColumnEditor",
                         "2, glaze::DataGrid::Editor::None") &&
                   anrop("dataN", "setCurrentCell", "0, 1") &&
                   anrop("dataD", "setCurrentCell", "1, 0"),
               "datagrid: kolumntypen, valen och cellen är Call-satser med "
               "argumenten intakta");
            // Ordningen: kolumntypen FÖRE addColumn ska stå kvar där.
            std::string följd;
            for (const bld::Stmt* s : anropen(r.model, "dataN"))
                följd += s->name + " ";
            ok(följd.rfind("setColumnEditor setColumnChoices addColumn", 0) == 0,
               "datagrid: anropen i KÄLLORDNING (" + följd + ")");
            const bld::Stmt* ev = nullptr;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Event && s.target == "dataN" &&
                    s.name == "onChange")
                    ev = &s;
            ok(ev && !ev->shortForm && ev->args == "glaze::CellChangeArgs" &&
                   ev->handler == "dataAndrad",
               "datagrid: dataN.onChange bär CellChangeArgs ordagrant");
        }
    }

    // ---- FLIKARNA i galleriets nionde kolumn. Byte-identiteten
    //      ovan täcker raderna; det här täcker KLASSNINGEN och TRÄDET:
    //      valet, stängbarheten och sidornas rubriker är setter-FÄLT (Prop),
    //      `onTabClose` i den korta formen är en koppling (readern känner
    //      namnet), och sidorna är flikkontrollens BARN i adopt-ordning — det
    //      är hela formen (ett kedjat `flikN.page(0).adopt(…)` går inte att
    //      emittera). `setSelectedIndex` står i widgetens block, FÖRE
    //      trädbygget, och den platsen ska överleva läsningen.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "flikar: galleriet läses (" + r.error + ")");
        if (r.ok) {
            auto propIdx = [&r](const char* target, const char* fält,
                                const std::string& värde) {
                for (std::size_t i = 0; i < r.model.stmts.size(); ++i) {
                    const bld::Stmt& s = r.model.stmts[i];
                    if (s.kind == bld::Stmt::Kind::Prop &&
                        s.target == target && s.name == fält)
                        return s.value == värde ? static_cast<long>(i) : -1L;
                }
                return -1L;
            };
            const long valIdx = propIdx("flikN", "selectedIndex", "1");
            ok(valIdx >= 0 && propIdx("flikN", "closable", "true") >= 0 &&
                   propIdx("flikA", "text", "\"Allmänt\"") >= 0 &&
                   propIdx("flikDb", "text", "\"Andra\"") >= 0,
               "flikar: setSelectedIndex/setClosable/sidornas setText "
               "normaliseras till Prop");
            std::vector<std::string> sidor;
            long förstaSida = -1;
            for (std::size_t i = 0; i < r.model.stmts.size(); ++i) {
                const bld::Stmt& s = r.model.stmts[i];
                if (s.kind == bld::Stmt::Kind::Adopt && s.name == "flikN") {
                    sidor.push_back(s.target);
                    if (förstaSida < 0)
                        förstaSida = static_cast<long>(i);
                }
            }
            ok(sidor == std::vector<std::string>{"flikA", "flikB", "flikC"},
               "flikar: sidorna är flikkontrollens barn i adopt-ordning");
            ok(valIdx >= 0 && förstaSida > valIdx,
               "flikar: setSelectedIndex står kvar FÖRE trädbygget (" +
                   std::to_string(valIdx) + " < " +
                   std::to_string(förstaSida) + ")");
            const bld::Stmt* stäng = nullptr;
            const bld::Stmt* byt   = nullptr;
            for (const bld::Stmt& s : r.model.stmts) {
                if (s.kind != bld::Stmt::Kind::Event || s.target != "flikN")
                    continue;
                if (s.name == "onTabClose")
                    stäng = &s;
                if (s.name == "onChange")
                    byt = &s;
            }
            ok(stäng && stäng->shortForm && stäng->handler == "flikStangs",
               "flikar: flikN.onTabClose (kort form) är en koppling");
            ok(byt && !byt->shortForm && byt->args == "glaze::ChangeArgs" &&
                   byt->handler == "flikBytt",
               "flikar: flikN.onChange (full form) är en koppling");
        }
    }

    // ---- DELAREN OCH DOCKNINGEN i galleriets sjunde kolumn.
    //      Byte-identiteten ovan täcker raderna; det här täcker KLASSNINGEN
    //      — setter-fälten (`setPaneSize`, `setFixedPane` med enum-värdet,
    //      `setCollapsed`, `setLayout`) normaliseras till Prop, `dock` är
    //      ett RÅTT layoutfält — och PLATSEN: `setLayout` står EFTER
    //      trädbygget, där den måste stå för att den kompilerade appen ska
    //      hitta panelerna, och den platsen ska överleva läsningen.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "delare/dockning: galleriet läses (" + r.error + ")");
        if (r.ok) {
            auto propIdx = [&r](const char* target, const char* fält,
                                const std::string& värde) {
                for (std::size_t i = 0; i < r.model.stmts.size(); ++i) {
                    const bld::Stmt& s = r.model.stmts[i];
                    if (s.kind == bld::Stmt::Kind::Prop &&
                        s.target == target && s.name == fält)
                        return s.value == värde ? static_cast<long>(i) : -1L;
                }
                return -1L;
            };
            ok(propIdx("delaN", "paneSize", "110") >= 0 &&
                   propIdx("delaH", "vertical", "false") >= 0 &&
                   propIdx("delaH", "fixedPane",
                           "glaze::Splitter::Pane::Second") >= 0 &&
                   propIdx("delaH", "secondMin", "20") >= 0,
               "delare: setPaneSize/setVertical/setFixedPane/setSecondMin "
               "normaliseras till Prop");
            ok(propIdx("dockV", "text", "\"Verktyg\"") >= 0 &&
                   propIdx("dockT", "collapsed", "true") >= 0 &&
                   propIdx("dockU", "dock", "glaze::Dock::Bottom") >= 0,
               "dockning: setText/setCollapsed är Prop, dock ett rått fält");
            const long layoutIdx = propIdx(
                "dockVard", "layout",
                "\"v1;dockU=bottom,0,70,0,0;dockV=left,110,0,0,0\"");
            long sistaAdopt = -1;
            for (std::size_t i = 0; i < r.model.stmts.size(); ++i)
                if (r.model.stmts[i].kind == bld::Stmt::Kind::Adopt &&
                    r.model.stmts[i].target.rfind("dock", 0) == 0)
                    sistaAdopt = static_cast<long>(i);
            ok(layoutIdx >= 0 && sistaAdopt >= 0 && layoutIdx > sistaAdopt,
               "dockning: setLayout är en Prop och står kvar EFTER "
               "panelernas adopt (" + std::to_string(layoutIdx) + " > " +
                   std::to_string(sistaAdopt) + ")");
            const bld::Stmt* ev = nullptr;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Event &&
                    s.target == "dockVard" && s.name == "onChange")
                    ev = &s;
            ok(ev && !ev->shortForm && ev->handler == "dockAndrad",
               "dockning: dockVard.onChange är en koppling");
        }
    }

    // ---- `setRole` är OFÖRÄNDRAD. Nyckeln `color` PÅ LABEL finns inte,
    //      men rollen gör det — och galleriet är
    //      den korpus som bär rollerna (13 förekomster).
    //
    //      `color` FINNS som setter-fält —
    //      men BARA på färgväljaren, vars värde är hextexten. Mätningen är
    //      därför per TYP: ingen color-Prop på något annat än en
    //      `glaze::ColorPicker` (Labels `setColor(gfx::Color{…})` ska förbli
    //      en Call), och galleriets färgväljare bär sina.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        int  roller = 0, colorProps = 0, fargProps = 0;
        auto typAv  = [&r](const std::string& id) {
            for (const bld::WidgetNode& n : r.model.nodes)
                if (n.id == id)
                    return n.type;
            return std::string();
        };
        if (r.ok)
            for (const bld::Stmt& s : r.model.stmts) {
                if (s.kind == bld::Stmt::Kind::Prop && s.name == "role")
                    ++roller;
                if (s.kind == bld::Stmt::Kind::Prop && s.name == "color") {
                    if (typAv(s.target) == "glaze::ColorPicker")
                        ++fargProps;
                    else
                        ++colorProps;
                }
            }
        ok(roller >= 13,
           "role: setRole(…) förblir Prop{role} (" +
               std::to_string(roller) + " st)");
        ok(colorProps == 0,
           "role: ingen color-Prop på något annat än en färgväljare");
        ok(fargProps == 3,
           "färgväljare: galleriets tre setColor blir Prop{color} (" +
               std::to_string(fargProps) + " st)");
    }

    // ---- FÄRGVÄLJAREN i galleriets elfte kolumn. Byte-identiteten
    //      ovan täcker RADERNA; det här täcker KLASSNINGEN och VÄRDENA:
    //      `setColor` är ett setter-FÄLT i BÅDA formerna — hextexten som
    //      strängliteral och `gfx::Color`-literalen ORDAGRANT — och
    //      `disable()` en Call.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "färgväljare: galleriet läses (" + r.error + ")");
        if (r.ok) {
            auto prop = [&r](const char* target, const char* fält,
                             const char* värde) {
                for (const bld::Stmt& s : r.model.stmts)
                    if (s.kind == bld::Stmt::Kind::Prop &&
                        s.target == target && s.name == fält)
                        return s.value == värde;
                return false;
            };
            ok(prop("fargN", "color", "\"#3366FF\"") &&
                   prop("fargD", "color", "\"#FF9800\""),
               "färgväljare: setColor(\"#…\") är Prop{color} med literalen");
            ok(prop("fargB", "color", "gfx::Color{76, 175, 80, 255}"),
               "färgväljare: setColor(gfx::Color{…}) är Prop{color}, "
               "literalen ordagrant");
            bool av = false;
            for (const bld::Stmt* s : anropen(r.model, "fargD"))
                if (s->name == "disable")
                    av = true;
            ok(av, "färgväljare: fargD.disable() är en Call");
        }
    }

    // ---- DATUMVÄLJAREN i galleriets tolfte kolumn. Datumet och
    //      gränserna är CALL-satser i den platta formen (tre heltal, argumenten
    //      ORDAGRANT) — inte setter-fält, för de tar tre argument — i källans
    //      ordning (gränserna FÖRE datumet på datumG), och `onChange` i den
    //      korta formen är en koppling.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "datumväljare: galleriet läses (" + r.error + ")");
        if (r.ok) {
            auto anrop = [&r](const char* target, const char* metod,
                              const char* args) {
                for (const bld::Stmt* s : anropen(r.model, target))
                    if (s->name == metod && s->value == args)
                        return true;
                return false;
            };
            ok(anrop("datumN", "setDate", "2026, 10, 8") &&
                   anrop("datumG", "setMinDate", "2026, 10, 1") &&
                   anrop("datumG", "setMaxDate", "2026, 10, 31") &&
                   anrop("datumG", "setDate", "2026, 12, 24") &&
                   anrop("datumD", "disable", ""),
               "datumväljare: setDate/setMinDate/setMaxDate är Call-satser med "
               "argumenten intakta");
            const auto g = anropen(r.model, "datumG");
            ok(g.size() == 3 && g[0]->name == "setMinDate" &&
                   g[1]->name == "setMaxDate" && g[2]->name == "setDate",
               "datumväljare: satserna står i KÄLLANS ordning");
            int props = 0;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Prop &&
                    s.target.rfind("datum", 0) == 0 &&
                    (s.name == "date" || s.name == "minDate" ||
                     s.name == "maxDate"))
                    ++props;
            ok(props == 0, "datumväljare: inget blir ett setter-fält");
            const bld::Stmt* ev = nullptr;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::Event && s.target == "datumN" &&
                    s.name == "onChange")
                    ev = &s;
            ok(ev && ev->shortForm && ev->handler == "datumAndrat",
               "datumväljare: datumN.onChange (kort form) är en koppling");
        }
    }

    // ---- KÄLLKORPUSEN bär inga RÅA `.color = …`-rader. Måste
    //      mätas på TEXTEN och inte på modellen: en modellmätning är
    //      cirkulär — varje `setColor` är en Call, så testet hade
    //      rapporterat noll även om korpusen vore full av dem. Raden
    //      kompilerar inte heller (`color_` är privat), så fyndet vore en trasig fixtur.
    //      En FÄLTRAD är medlemsåtkomst — en identifierare före punkten
    //      (`etikett.color = …`); en DESIGNERARE i en aggregatliteral
    //      (`{.offset = 0, .color = …}`, ritytans gradientstopp) föregås av
    //      `{`, `,` eller ett blanksteg och är ingen fältrad.
    {
        int råa = 0;
        for (const char* f : {"apps/demo/src/start_window.glazeui",
                              "apps/demo/src/second_window.glazeui",
                              "tests/fixtures/gallery.glazeui",
                              "tests/fixtures/calls.glazeui",
                              "tests/fixtures/order.glazeui"}) {
            const std::string text = läs(root() + f);
            for (size_t i = text.find(".color");
                 i != std::string::npos; i = text.find(".color", i + 1)) {
                const bool medlem =
                    i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) ||
                              text[i - 1] == '_');
                const size_t e = text.find_first_not_of(" \t", i + 6);
                if (medlem && e != std::string::npos && text[e] == '=')
                    ++råa;
            }
        }
        ok(råa == 0, "korpus: inga råa `.color = …`-rader i understödda "
                     "*.glazeui (" + std::to_string(råa) + ")");
    }

    // ---- FELTYPAD PROP — writerbeteendet ska vara AVSIKTLIGT och inte en
    //      slump. En `Prop` vars fält inte hör till nodtypen emitteras som
    //      en FÄLTRAD (`lista.checked = …;`) och inte som ett setteranrop:
    //      writern hittar aldrig på en metod typen saknar. Det är också
    //      skälet att påståendet "texten är identisk oavsett Prop eller
    //      Call" måste begränsas till den omklassade raden.
    {
        bld::WindowModel m;
        m.className = "T";
        bld::WidgetNode n;
        n.id   = "lista";
        n.type = "glaze::ListBox";
        m.nodes.push_back(n);
        bld::Stmt s;
        s.kind   = bld::Stmt::Kind::Prop;
        s.target = "lista";
        s.name   = "checked";     // hör till Checkbox/Radio, inte ListBox
        s.value  = "true";
        m.stmts.push_back(s);
        const std::string ut = bld::emitGuiCpp(m, "t.h", "\n");
        ok(ut.find("lista.checked = true;") != std::string::npos,
           "feltypad Prop: emitteras som FÄLTRAD, aldrig som setteranrop");
        ok(ut.find("lista.setChecked(") == std::string::npos,
           "feltypad Prop: writern hittar inte på en metod typen saknar");
    }

    // ---- NAKNA FÖNSTERANROP. Formen är en `WindowCall`-sats med
    // samma regel 4-behandling som `<id>.<metod>()`.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "fönsteranrop: filen läses i stället för fail-safe (" +
                     r.error + ")");
        if (r.ok) {
            std::vector<std::string> namn;
            for (const bld::Stmt& s : r.model.stmts)
                if (s.kind == bld::Stmt::Kind::WindowCall)
                    namn.push_back(s.name);
            // TJUGOTRE fönsteranrop: `setShowInTaskbar`
            // står mellan `setTopmost` och det okända `setAnimating`.
            // Standard- och avbrytknappen står efter
            // `setShowInTaskbar`, med en MEDLEM som argument.
            // `setSizeToContent(…)` står mellan
            // `setShowInTaskbar` och standardknappen.
            // `setSizeToContentMode(…)` står
            // direkt efter `setSizeToContent`.
            // Gränserna och den låsta storleken i
            // innehållsmått och `setPopupTakesKeys` efter avbrytknappen.
            // Fönstrets `setWheelInvert(…)`
            // efter `setPopupTakesKeys`.
            ok(namn.size() == 23 && namn[0] == "setTitle" &&
                   namn[1] == "setClientSize" && namn[2] == "setContentSize" &&
                   namn[11] == "setTopmost" &&
                   namn[12] == "setShowInTaskbar" &&
                   namn[13] == "setSizeToContent" &&
                   namn[14] == "setSizeToContentMode" &&
                   namn[15] == "setAcceptButton" &&
                   namn[16] == "setCancelButton" &&
                   namn[17] == "setMinimumContentSize" &&
                   namn[18] == "setMaximumContentSize" &&
                   namn[19] == "setFixedContentSize" &&
                   namn[20] == "setPopupTakesKeys" &&
                   namn[21] == "setWheelInvert" &&
                   namn[22] == "setAnimating",
               "fönsteranrop: tjugotre satser i källordning (" +
                   std::to_string(namn.size()) + ")");

            // Den låsta storleken läses med sina två heltal ordagrant.
            const bld::Stmt* fast = r.model.lastWindowCall("setFixedContentSize");
            ok(fast && fast->value == "360, 200",
               "fönsteranrop: setFixedContentSize(360, 200) läses med sina "
               "argument");

            // ETIKETTENS BREDDGRUPP: ett SETTER-fält —
            // `etikett.setWidthGroup("form");` läses som en Prop med
            // literalen ordagrant, och skrivs tillbaka som anropet (fixturens
            // byte-identitet ovan; utan raden i `kSetterFields` hade writern
            // skrivit fältraden `etikett.widthGroup = "form";`).
            const bld::Stmt* grupp = r.model.findProp("etikett", "widthGroup");
            ok(grupp && grupp->value == "\"form\"",
               "fönsteranrop: etikett.setWidthGroup(\"form\") är en Prop med "
               "literalen");

            // INMATNINGEN: `karta.setInputMode("orbit")` är
            // ett SETTER-fält med literalen; bindningarna och vändningarna är
            // Call-satser med argumenten ORDAGRANT — också `|` mellan
            // modifierarna, som grammatiken bär som rå text utan ny form —
            // och basens `setWheelInvert` likaså. Byte-identiteten ovan bär
            // återskrivningen; SABOTAGET `inputMode` borttagen ur
            // `kSetterFields` fäller den (writern skriver fältraden).
            const bld::Stmt* läge = r.model.findProp("karta", "inputMode");
            ok(läge && läge->value == "\"orbit\"",
               "inmatning: karta.setInputMode(\"orbit\") är en Prop med literalen");
            bool rad = false, mods = false, vänd = false;
            for (const bld::Stmt* s : anropen(r.model, "karta")) {
                rad |= s->name == "setBinding" &&
                       s->value == "\"orbit\", glaze::InputSource::WheelY, 0, "
                                   "glaze::InputAction::Zoom, "
                                   "glaze::InputDirection::Inverted, 1.5f";
                mods |= s->name == "setBinding" &&
                        s->value == "\"orbit\", glaze::InputSource::ScrollX, "
                                    "gfx::kModCtrl | gfx::kModShift, "
                                    "glaze::InputAction::Rotate";
                vänd |= s->name == "setActionInvert" &&
                        s->value == "glaze::InputDevice::Touchpad, "
                                    "glaze::InputAction::Zoom, true";
            }
            ok(rad && mods && vänd,
               "inmatning: setBinding (sex argument, och | i modifierarna) och "
               "setActionInvert är Call-satser, ordagranna");
            bool hjul = false;
            for (const bld::Stmt* s : anropen(r.model, "lista"))
                hjul |= s->name == "setWheelInvert" &&
                        s->value == "glaze::InputDevice::Mouse, true, false";
            const bld::Stmt* fönsterhjul = r.model.lastWindowCall("setWheelInvert");
            ok(hjul && fönsterhjul &&
                   fönsterhjul->value == "glaze::InputDevice::Touchpad, false, true",
               "inmatning: basens och fönstrets setWheelInvert läses ordagrant");

            const bld::Stmt* sit = r.model.lastWindowCall("setShowInTaskbar");
            ok(sit && sit->value == "false",
               "fönsteranrop: setShowInTaskbar(false) läses med sitt argument");

            const bld::Stmt* standard = r.model.lastWindowCall("setAcceptButton");
            ok(standard && standard->value == "trBtn",
               "fönsteranrop: setAcceptButton(trBtn) läses med medlemmen som "
               "argument");

            const bld::Stmt* mini = r.model.lastWindowCall("setMinimumSize");
            ok(mini && mini->value == "320, 180",
               "fönsteranrop: argumenten bevaras ordagrant");

            // Det OKÄNDA anropet är den viktiga raden: en nyare sättare
            // får aldrig bricka filen för en äldre builder.
            ok(r.model.lastWindowCall("setAnimating") != nullptr,
               "fönsteranrop: okänt namn bevaras opakt (aldrig fail-safe)");
        }
    }

    // ---- SATSORDNINGEN — den ordnade satslistan.
    //
    // Fixturen är MEDVETET icke-kanonisk: fält och anrop varvas, ett anrop
    // står EFTER trädbygget och en inbyggd medlem introduceras av sin egen
    // adopt-rad. En fixtur i redan kanonisk ordning vore grön även
    // med en gruppvis emission och bevisade ingenting.
    {
        const std::string h = root() + "tests/fixtures/order.h";
        byteIdentisk(h);

        bld::ReadResult r = bld::readWindow(h);
        ok(r.ok, "order: fixturen läses (" + r.error + ")");
        if (r.ok) {
            const bld::WindowModel& m = r.model;

            // SATSORDNING = källordning. Anropet som står sist i filen ska
            // vara sist i listan — det får inte flyttas före adopt-blocket.
            ok(!m.stmts.empty() &&
                   m.stmts.back().kind == bld::Stmt::Kind::Call &&
                   m.stmts.back().target == "ruta" &&
                   m.stmts.back().name == "focus",
               "order: anropet efter trädbygget ligger SIST i satslistan");

            // Varvningen: ruta.x → ruta.disable() → ruta.y, ordagrant.
            std::vector<std::string> rutan;
            for (const bld::Stmt& s : m.stmts)
                if (s.target == "ruta" &&
                    (s.kind == bld::Stmt::Kind::Prop ||
                     s.kind == bld::Stmt::Kind::Call))
                    rutan.push_back(s.name);
            ok(rutan.size() == 5 && rutan[0] == "text" && rutan[1] == "x" &&
                   rutan[2] == "disable" && rutan[3] == "y" &&
                   rutan[4] == "focus",
               "order: fält och anrop varvas i källordning");

            // Adopt-raden är statusBars FÖRSTA referens — föräldrauppslaget
            // måste gå via `findTarget` och inte `find`, annars blir den fail-safe.
            ok(m.parentOf("chip") == "statusBar" && m.isAdopted("chip"),
               "order: chip adopteras i den inbyggda statusBar");

            // NODORDNING ≠ satsordning: builtins FÖRST, så föräldern byggs
            // före sitt barn i båda trädbyggarna.
            ok(m.nodes.size() == 3 && m.nodes[0].id == "statusBar" &&
                   m.nodes[0].builtin && m.nodes[1].id == "ruta" &&
                   m.nodes[2].id == "chip",
               "order: nodordningen är builtin-först, sedan adopt-ordning");
        }
    }

    // ---- KOMPONENTERNA OCH SENDERTYPEN. En
    // icke-visuell medlem står i regionen men adopteras aldrig, och dess
    // kopplingar bär SIN EGEN sendertyp (`glaze::Timer* s`). Byte-identiteten
    // ovan bevisar att formen överlever läs → skriv; de här raderna bevisar
    // att MODELLEN bär den — en reader som tappade sendern hade fått writern
    // att skriva `glaze::Widget* s`, och raden hade slutat kompilera.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/components.h");
        ok(r.ok, "komponenter: fixturen läses (" + r.error + ")");
        if (r.ok) {
            const bld::WindowModel& m = r.model;
            ok(m.nodes.size() == 4 && m.nodes[0].id == "status" &&
                   m.nodes[1].id == "knapp" && m.nodes[2].id == "klocka" &&
                   m.nodes[3].id == "ikon",
               "komponenter: nodordningen är adopt-ordningen och sedan de "
               "icke-adopterade (komponenterna sist)");
            ok(!m.isAdopted("klocka") && !m.isAdopted("ikon") &&
                   m.find("klocka")->type == "glaze::Timer" &&
                   m.find("ikon")->type == "glaze::TrayIcon",
               "komponenter: timern och ikonen är noder som aldrig adopteras");

            const bld::Stmt* iv = m.findProp("klocka", "interval");
            const bld::Stmt* ss = m.findProp("klocka", "singleShot");
            const bld::Stmt* tt = m.findProp("ikon", "toolTip");
            const bld::Stmt* ti = m.findProp("ikon", "templateImage");
            const bld::Stmt* cu = m.findProp("status", "cursor");
            ok(iv && iv->value == "250" && ss && ss->value == "true" && tt &&
                   tt->value == "glaze::tr(1)" && ti && cu &&
                   cu->value == "gfx::PointerShape::Wait",
               "komponenter: settrarna normaliseras till fält — timerns två, "
               "ikonens och basens pekare");
            const auto klockan = anropen(m, "klocka");
            ok(klockan.size() == 1 && klockan[0]->name == "start",
               "komponenter: start() är ett anrop");
            const auto ikonen = anropen(m, "ikon");
            ok(ikonen.size() == 6 && ikonen[0]->name == "addFrame" &&
                   ikonen[1]->name == "addFrame" &&
                   ikonen[1]->value == "{}" &&
                   ikonen[2]->name == "addItem" &&
                   ikonen[2]->value == "{}, \"Visa\"" &&
                   ikonen[5]->name == "show",
               "komponenter: ikonens bilder, meny och show() är anrop i "
               "källordning, ordagranna");
            const auto knappen = anropen(m, "knapp");
            ok(knappen.size() == 1 && knappen[0]->name == "resetCursor",
               "komponenter: resetCursor() är ett anrop");

            const bld::Stmt* tick  = nullptr;
            const bld::Stmt* klick = nullptr;
            const bld::Stmt* hover = nullptr;
            for (const bld::Stmt& s : m.stmts) {
                if (s.kind != bld::Stmt::Kind::Event)
                    continue;
                if (s.target == "klocka" && s.name == "onTick")
                    tick = &s;
                if (s.target == "ikon" && s.name == "onClick")
                    klick = &s;
                if (s.target == "ikon" && s.name == "onHover")
                    hover = &s;
            }
            ok(tick && !tick->shortForm && tick->sender == "glaze::Timer" &&
                   tick->args == "glaze::TimerArgs" &&
                   tick->handler == "klockaTick",
               "komponenter: timerns koppling bär sendertypen glaze::Timer");
            ok(klick && klick->sender == "glaze::TrayIcon" &&
                   klick->args == "glaze::ClickArgs",
               "komponenter: ikonens koppling bär sendertypen glaze::TrayIcon");
            ok(hover && hover->shortForm && hover->sender.empty(),
               "komponenter: den korta formen har ingen sendertyp");

            const bld::Stmt* krom = m.lastWindowCall("setChrome");
            const bld::Stmt* plats = m.lastWindowCall("setLocation");
            const bld::Stmt* start = m.lastWindowCall("setStartPosition");
            const bld::Stmt* läge  = m.lastWindowCall("setWindowState");
            const bld::Stmt* vänta = m.lastWindowCall("setUseWaitCursor");
            ok(krom && krom->value == "gfx::Chrome::None" && plats &&
                   plats->value == "120, 80" && start &&
                   start->value == "glaze::StartPosition::CenterOwner" &&
                   läge && läge->value == "gfx::WindowState::Maximized" &&
                   vänta && vänta->value == "true",
               "komponenter: fönstrets fem nya anrop är nakna fönsteranrop");

            // HANDLERNS DEKLARATION OCH STUB följer sendern — det är dem
            // GlazeBuilder appendar i användarens filer.
            if (tick) {
                bld::EventBind eb{tick->name, tick->args, tick->handler};
                eb.sender = tick->sender;
                const std::string decl = bld::emitHandlerDecl(eb, "\n", false);
                const std::string stub =
                    bld::emitHandlerStub("ComponentsFixture", eb, "\n");
                ok(decl == "    void klockaTick(glaze::Timer* sender, "
                           "glaze::TimerArgs& e);\n",
                   "komponenter: handlerdeklarationen tar glaze::Timer*");
                ok(stub.find("void ComponentsFixture::klockaTick(glaze::Timer* "
                             "sender, glaze::TimerArgs& e)") !=
                       std::string::npos,
                   "komponenter: stubben tar glaze::Timer*");
            }
        }

        // En vanlig widgetkoppling lagrar INGEN sendertyp (tom = Widget).
        bld::ReadResult d =
            bld::readWindow(root() + "apps/demo/src/second_window.h");
        bool alla = d.ok;
        for (const bld::Stmt& s : d.model.stmts)
            if (s.kind == bld::Stmt::Kind::Event)
                alla = alla && s.sender.empty();
        ok(alla, "sendertyp: glaze::Widget lagras som tom sträng");

        // appendEvent/appendWindowEvent KOPIERAR sendern.
        bld::WindowModel mm;
        bld::EventBind   eb{"onTick", "glaze::TimerArgs", "tick"};
        eb.sender = "glaze::Timer";
        mm.appendEvent("t", eb);
        mm.appendWindowEvent(eb);
        ok(mm.stmts.size() == 2 && mm.stmts[0].sender == "glaze::Timer" &&
               mm.stmts[1].sender == "glaze::Timer",
           "sendertyp: appendEvent och appendWindowEvent kopierar den");

        // GRAMMATIKEN: ett kvalificerat namn följt av `* s, ` — allt annat är
        // fail-safe, och ett namn ur en egen namnrymd bevaras.
        const std::string hBytes =
            "#pragma once\n"
            "class F : public glaze::AppWindow {\n"
            "#pragma region GlazeBuilder\n"
            "public:\n"
            "    F();\n"
            "private:\n"
            "    glaze::Timer t;\n"
            "    void initializeComponent();\n"
            "#pragma endregion\n"
            "};\n";
        const auto läsRad = [&](const std::string& rad, bld::WindowModel& ut) {
            const std::string kropp = "#include \"f.h\"\n\n"
                                      "F::F() { initializeComponent(); }\n\n"
                                      "void F::initializeComponent()\n{\n    " +
                                      rad + "\n}\n";
            std::string err;
            return bld::parseHeader(hBytes, "f.h", ut, err) &&
                   bld::parseGuiCpp(kropp, "f.glazeui", ut, err);
        };
        for (const char* trasig :
             {"t.onTick = [this](glaze::* s, glaze::TimerArgs& e) { h(s, e); };",
              "t.onTick = [this](2Timer* s, glaze::TimerArgs& e) { h(s, e); };",
              "t.onTick = [this](glaze::Timer s, glaze::TimerArgs& e) { h(s, e); };",
              "t.onTick = [this](glaze:Timer* s, glaze::TimerArgs& e) { h(s, e); };",
              "t.onTick = [this](* s, glaze::TimerArgs& e) { h(s, e); };"}) {
            bld::WindowModel ut;
            ok(!läsRad(trasig, ut),
               std::string("sendertyp: trasig form är fail-safe — ") + trasig);
        }
        {
            bld::WindowModel ut;
            ok(läsRad("t.onTick = [this](min::ns::Typ* s, glaze::TimerArgs& e) "
                      "{ h(s, e); };",
                      ut) &&
                   !ut.stmts.empty() && ut.stmts.back().sender == "min::ns::Typ",
               "sendertyp: ett namn ur en egen namnrymd bevaras");
        }
        {
            bld::WindowModel ut;
            ok(läsRad("t.onTick = [this](glaze::Widget* s, glaze::TimerArgs& e) "
                      "{ h(s, e); };",
                      ut) &&
                   !ut.stmts.empty() && ut.stmts.back().sender.empty(),
               "sendertyp: glaze::Widget normaliseras till tom sträng");
        }
    }

    // `setTitle(<uttryck>)` är en OPAK sats och
    // inte FAIL-SAFE — det nakna fönsteranropets regel. Satsen bevaras
    // ordagrant, och `title()` svarar TOMT för den (glaze-render konstruerar
    // fönstret med svaret). En obalanserad titel är fail-safe.
    //
    // SABOTAGE: återinför readerns krav `parseCppString || parseTrCall` ⇒ de
    // två första fallen faller (fail-safe på hela filen); låt `title()` svara
    // med den råa texten ⇒ de faller på `title()`.
    {
        const std::string hBytes = "#pragma once\n"
                                   "class F : public glaze::AppWindow {\n"
                                   "#pragma region GlazeBuilder\n"
                                   "public:\n"
                                   "    F();\n"
                                   "private:\n"
                                   "    void initializeComponent();\n"
                                   "#pragma endregion\n"
                                   "};\n";
        const auto läs = [&](const std::string& rad, bld::WindowModel& ut) {
            const std::string kropp = "#include \"f.h\"\n\n"
                                      "F::F() { initializeComponent(); }\n\n"
                                      "void F::initializeComponent()\n{\n    " +
                                      rad + "\n}\n";
            std::string err;
            return bld::parseHeader(hBytes, "f.h", ut, err) &&
                   bld::parseGuiCpp(kropp, "f.glazeui", ut, err);
        };
        for (const char* rad : {"setTitle(glaze::tr(app::str::Titel));",
                                "setTitle(kTitel);"}) {
            bld::WindowModel m;
            const bool       lästes = läs(rad, m);
            const std::string ut    = bld::emitGuiCpp(m, "f.h", "\n");
            ok(lästes && m.lastWindowCall("setTitle") && m.title().empty() &&
                   ut.find(std::string("    ") + rad + "\n") != std::string::npos,
               std::string("titel: ") + rad +
                   " läses som en OPAK sats, round-trippar och title() är tom");
        }
        {
            bld::WindowModel m;
            ok(!läs("setTitle(glaze::tr(app::str::Titel);", m),
               "titel: ett obalanserat argument är fail-safe som förut");
        }
        {
            bld::WindowModel lit, tr;
            ok(läs("setTitle(\"Hej\");", lit) && lit.title() == "Hej" &&
                   läs("setTitle(glaze::tr(1000));", tr) && tr.title().empty() &&
                   tr.lastWindowCall("setTitle") &&
                   tr.lastWindowCall("setTitle")->value == "glaze::tr(1000)",
               "titel: title() är literalens text; ett tr-id läses ur satsen");
        }
    }

    // Titel och storlek är LÄSFUNKTIONER över satslistan, inte fält —
    // "den som sätts sist gäller", precis som app_window.h säger.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/order.h");
        ok(r.ok && r.model.title() == "Satsordning" &&
               r.model.clientWidth() == 320 && r.model.clientHeight() == 200,
           "order: title()/clientWidth()/clientHeight() läses ur satserna");

        bld::WindowModel m = r.model;
        m.setWindowCall("setClientSize", "640, 480");
        ok(m.clientWidth() == 640 && m.clientHeight() == 480,
           "setWindowCall uppdaterar PÅ PLATS (ingen andra sats)");
        int antal = 0;
        for (const bld::Stmt& s : m.stmts)
            if (s.kind == bld::Stmt::Kind::WindowCall &&
                s.name == "setClientSize")
                ++antal;
        ok(antal == 1, "setWindowCall appendar inte en dubblett (" +
                           std::to_string(antal) + ")");
    }

    // ---- SETTER-FÄLTEN. Tillståndet är privat i C++
    // så `okBtn.text = "…";` kompilerar inte — writern
    // emitterar CALL-formen och readern normaliserar tillbaka till en
    // `Prop`, så modellens fältvokabulär är oförändrad.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "apps/demo/src/second_window.h");
        ok(r.ok, "setter: demons triplett läses (" + r.error + ")");
        if (r.ok) {
            // `info.setText("…")` ska ha blivit en Prop med fältnamnet.
            // Demons texter bär ID och inte
            // literaler — normaliseringen är densamma, VÄRDET är en
            // tr-form. Att `parseTrCall` avkodar just det här värdet är
            // vad som gör raden till en mätning och inte en textmatchning.
            const bld::Stmt* p    = r.model.findProp("info", "text");
            std::uint32_t    trId = 0;
            ok(p && bld::parseTrCall(p->value, trId) && trId == 1101,
               "setter: setText() normaliseras till fältet 'text'" +
                   (p ? " (" + p->value + ")" : std::string()));
            ok(r.model.findProp("tips", "role") != nullptr,
               "setter: setRole() normaliseras till fältet 'role'");
            // …och LAYOUT-fälten är kvar som råa fältrader.
            ok(r.model.findProp("info", "x") != nullptr,
               "setter: layoutfältet 'x' är kvar rått");
        }
    }

    // MIGRERINGEN: en gammal fältrad läses och skrivs ut
    // i den NYA formen. Det är så handskrivna tripletter tar sig över —
    // vid första öppna + spara.
    {
        bld::WindowModel m;
        m.className = "Migrering";
        m.appendNode("glaze::Button", "btn");
        m.appendNode("MinEgenWidget", "egen");
        m.setWindowCall("setTitle", "\"M\"");
        m.setWindowCall("setClientSize", "100, 100");
        m.setProp("btn", "text", "\"Hej\"");   // GAMLA formen in
        m.setProp("btn", "x", "12");
        m.setProp("egen", "text", "\"Hej\"");  // egen typ — rör den inte
        const std::string ut = bld::emitGuiCpp(m, "migrering.h", "\n");
        ok(ut.find("    btn.setText(\"Hej\");\n") != std::string::npos,
           "migrering: fältraden emitteras som setter-anrop");
        ok(ut.find("    btn.x = 12;\n") != std::string::npos,
           "migrering: layoutfältet är kvar som fältrad");
        ok(ut.find("    egen.text = \"Hej\";\n") != std::string::npos,
           "migrering: EGEN typ round-trippar i sin egen form");
    }

    // ---- 6. TVÅ designrader ⇒ fail-safe, aldrig "sista vinner".
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/design_dup.h");
        ok(!r.ok, "två designrader: ok == false (fail-safe)");
        ok(r.error.find("glazeui-design") != std::string::npos,
           "två designrader: diagnosen pekar ut raden — '" + r.error + "'");
    }

    // ---- ESCAPE-DELMÄNGDEN. Tabellgrind åt
    // BÅDA hållen: `cppString(parseCppString(x)) == x` på hela den
    // accepterade mängden, och `parseCppString(cppString(s)) == s` på hela
    // den kodningsbara. Utan byte-exaktheten hade en rå TAB lästs in och
    // emitterats som `\t` — semantiskt samma, textuellt en annan rad, och
    // därmed en diff i en fil användaren inte rört.
    {
        auto rundtur = [](const std::string& literal, const std::string& värde,
                          const std::string& vad) {
            std::string v;
            const bool  in = bld::parseCppString(literal, v);
            ok(in && v == värde, "escape in: " + vad);
            if (!in)
                return;
            std::string ut;
            ok(bld::cppString(v, ut) && ut == literal,
               "escape ut (byte-exakt): " + vad);
        };

        rundtur("\"Hej\"", "Hej", "ren text");
        // UTF-8 MÅSTE passera oförändrad — repot är fullt av svensk text,
        // och en avvisad å hade varit en katastrofal regression.
        rundtur("\"Hej \303\245\303\244\303\266\"", "Hej \303\245\303\244\303\266",
                "vanlig UTF-8 (\303\245\303\244\303\266)");
        rundtur("\"rad\\nrad\"", "rad\nrad", "\\n = radbrytning (buggen)");
        rundtur("\"a\\tb\"", "a\tb", "\\t");
        rundtur("\"a\\rb\"", "a\rb", "\\r");
        rundtur("\"sa \\\"hej\\\"\"", "sa \"hej\"", "\\\" (citat)");
        rundtur("\"C:\\\\temp\"", "C:\\temp", "dubbel backslash");
        rundtur("\"rad\\n\\\"citat\\\"\"", "rad\n\"citat\"",
                "kombinationen \\n + \\\"");
        rundtur("\"\"", "", "tom sträng");

        auto avvisas = [](const std::string& literal, const std::string& vad) {
            std::string v;
            ok(!bld::parseCppString(literal, v), "avvisas: " + vad);
        };
        avvisas("\"\\x41\"", "\\x41 (hex-escape)");
        avvisas("\"\\101\"", "oktal escape");
        avvisas("\"\\u00e5\"", "universal-character-name");
        avvisas("\"\\b\"", "\\b (beslut: utanför delmängden)");
        avvisas("\"\\f\"", "\\f (beslut: utanför delmängden)");
        avvisas("\"\\0\"", "\\0 (beslut: utanför delmängden)");
        avvisas("\"\\\303\245\"", "\\\303\245 — okänd escape (UTF-8 EFTER backslash)");
        avvisas("\"a\" \"b\"", "sammanslagna literaler");
        avvisas("\"abc\\\"", "AVSLUTANDE backslash (slutcitatet är escapat)");
        avvisas("\"a\tb\"", "rå TAB inne i literalen");
        avvisas("\"a\rb\"", "rå CR inne i literalen");
        avvisas("\"a\nb\"", "rå LF inne i literalen");
        avvisas("Hej", "ingen literal alls");
        avvisas("\"", "ensamt citattecken");

        // Emissionens motsvarande gräns: tecken utanför delmängden går
        // inte att koda, och `cppString` säger ifrån i stället för att
        // skriva en rad som inte kompilerar.
        {
            std::string ut;
            ok(!bld::cppString(std::string("a\bb"), ut),
               "cppString: \\b kan inte kodas ⇒ false");
            ok(!bld::cppString(std::string("a\fb"), ut),
               "cppString: \\f kan inte kodas ⇒ false");
            ok(!bld::cppString(std::string(1, '\0') + "x", ut),
               "cppString: NUL kan inte kodas ⇒ false");
            ok(bld::cppString("rad\nrad", ut) && ut == "\"rad\\nrad\"",
               "cppString: LF kodas som \\n (den latenta buggen)");
        }

        // LISTLITERALEN ärver delmängden — `parseStringList` går genom
        // samma parser, så ett item med `\n` avkodas och ett med `\x41`
        // gör hela raden opak.
        {
            std::vector<std::string> lista;
            ok(bld::parseStringList("{\"a\\nb\", \"c\"}", lista) &&
                   lista.size() == 2 && lista[0] == "a\nb",
               "items: \\n i ett element avkodas");
            ok(!bld::parseStringList("{\"a\\x41\"}", lista),
               "items: \\x41 i ett element ⇒ hela raden opak");
        }
    }

    // ---- NAMN-INVARIANTEN.
    //
    // TVÅ grindar och inte en: "noll diff efter migreringen"
    // bevisar bara IDEMPOTENS, inte att migreringen gjorde rätt sak.
    {
        // 1. MIGRERINGSGRINDEN. `gallery.*` är LEGACY per konstruktion —
        // fixturen är FRYST (galleriet är handkodat) och bär därför
        // inte en enda `name`-rad. Den är alltså exakt det en användares
        // orörda fil är: skriven före invarianten fanns.
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/gallery.h");
        ok(r.ok, "migrering: legacy-fixturen läses (" + r.error + ")");
        if (r.ok) {
            int saknas = 0, kanoniska = 0;
            for (const bld::WidgetNode& n : r.model.nodes) {
                if (n.builtin)
                    continue;
                if (bld::nameState(r.model, n) == bld::NameState::Saknas)
                    ++saknas;
            }
            ok(saknas > 0,
               "migrering: legacy-fixturen saknar name-rader (" +
                   std::to_string(saknas) + " noder) — annars mäter grinden "
                   "ingenting");

            std::string fel;
            ok(bld::migrateNames(r.model, fel),
               "migrering: går igenom (" + fel + ")");

            int vanliga = 0;
            for (const bld::WidgetNode& n : r.model.nodes) {
                if (n.builtin) {
                    // BUILTINS undantas HELT — en emitterad rad hade
                    // krävt att writern bryter mot regeln att de aldrig
                    // får en medlemsrad.
                    ok(bld::nameState(r.model, n) == bld::NameState::Saknas,
                       "migrering: builtin '" + n.id + "' får INGEN "
                       "name-rad");
                    continue;
                }
                ++vanliga;
                if (bld::nameState(r.model, n) == bld::NameState::Kanoniskt)
                    ++kanoniska;
            }
            ok(vanliga > 0 && kanoniska == vanliga,
               "migrering: EXAKT en korrekt name-rad per vanlig nod (" +
                   std::to_string(kanoniska) + "/" +
                   std::to_string(vanliga) + ")");

            // Ingen ANNAN sats flyttad eller ändrad: satslistan ska vara
            // originalet + n insättningar, med den ursprungliga relativa
            // ordningen intakt.
            bld::ReadResult r0 =
                bld::readWindow(root() + "tests/fixtures/gallery.h");
            std::vector<const bld::Stmt*> gamla, nya;
            for (const bld::Stmt& s : r0.model.stmts)
                gamla.push_back(&s);
            for (const bld::Stmt& s : r.model.stmts)
                if (!(s.kind == bld::Stmt::Kind::Prop && s.name == "name"))
                    nya.push_back(&s);
            bool samma = gamla.size() == nya.size();
            for (std::size_t i = 0; samma && i < gamla.size(); ++i)
                samma = gamla[i]->kind == nya[i]->kind &&
                        gamla[i]->target == nya[i]->target &&
                        gamla[i]->name == nya[i]->name &&
                        gamla[i]->value == nya[i]->value;
            ok(samma, "migrering: inga ANDRA satser flyttade eller ändrade");

            // Emissionen: raden hamnar i widgetens EGET block och inte i
            // ett eget block efter trädbygget (`setProp` hade appendat
            // sist i hela listan).
            const std::string gui =
                bld::emitGuiCpp(r.model, "gallery.h", "\n");
            const std::size_t förstaAdopt = gui.find("    root().adopt(");
            const std::size_t sistaName   = gui.rfind(".name = \"");
            ok(förstaAdopt != std::string::npos &&
                   sistaName != std::string::npos && sistaName < förstaAdopt,
               "migrering: alla name-rader står FÖRE trädbygget");

            // 2. IDEMPOTENSGRINDEN: en andra migrering ändrar ingenting.
            const std::size_t innan = r.model.stmts.size();
            std::string       fel2;
            ok(bld::migrateNames(r.model, fel2) &&
                   r.model.stmts.size() == innan,
               "idempotens: andra migreringen lägger till NOLL rader");
            ok(bld::emitGuiCpp(r.model, "gallery.h", "\n") == gui,
               "idempotens: emissionen är byte-identisk");
        }
    }

    // AVVIKANDE name-rad ⇒ FAIL-SAFE, aldrig tyst omskrivning. Raden ligger
    // i builderägd kod men är TVETYDIG i identitetsmodellen, och regel 4
    // säger vägra i stället för att gissa.
    {
        bld::WindowModel m;
        m.className = "Avvik";
        m.appendNode("glaze::Button", "Button1");
        m.setProp("Button1", "name", "\"annat\"");
        std::string fel;
        ok(bld::nameState(m, m.nodes[0]) == bld::NameState::Avviker,
           "invariant: värde ≠ nodnamn ⇒ Avviker");
        ok(!bld::migrateNames(m, fel) && !fel.empty(),
           "invariant: avvikande rad ⇒ migreringen VÄGRAR (" + fel + ")");
        // …och INGET skrevs (fail-safe är atomär).
        ok(m.stmts.size() == 1, "invariant: inget skrevs vid vägran");
    }
    {
        // FLERA name-rader — readern pushar `Prop` direkt, så en
        // handredigerad fil kan bära två.
        bld::WindowModel m;
        m.className = "Dubbel";
        m.appendNode("glaze::Button", "Button1");
        bld::Stmt a;
        a.kind = bld::Stmt::Kind::Prop;
        a.target = "Button1";
        a.name = "name";
        a.value = "\"Button1\"";
        m.stmts.push_back(a);
        m.stmts.push_back(a);
        ok(bld::nameState(m, m.nodes[0]) == bld::NameState::Avviker,
           "invariant: TVÅ name-rader ⇒ Avviker");
    }

    // BUILTINS FÅR ALDRIG BÄRA EN name-RAD. Första
    // versionen hoppade över dem FÖRE valideringen, så `menuBar.name =
    // "annat";` passerade — och `applyProp` skriver raden OVANPÅ det namn
    // `AppWindow`s konstruktor satt, i både previewn och glaze-render. Ett
    // ankare mot "menuBar" dog då tyst medan filen öppnades redigerbart.
    // Kravet är SAKNAS, inte "kanoniskt": en rad som råkar vara rätt är
    // bara ofarlig av en slump.
    {
        bld::WindowModel m;
        m.className = "Builtin";
        m.appendNode("glaze::MenuBar", "menuBar");
        m.nodes[0].builtin = true;
        m.setProp("menuBar", "name", "\"annat\"");
        std::string fel;
        ok(!bld::migrateNames(m, fel) && !fel.empty(),
           "builtin: AVVIKANDE name-rad ⇒ migreringen VÄGRAR (" + fel + ")");
    }
    {
        // …och även en KANONISK rad avvisas — den skriver ändå över
        // AppWindows tilldelning och är alltså inte harmlös.
        bld::WindowModel m;
        m.className = "BuiltinKanon";
        m.appendNode("glaze::StatusBar", "statusBar");
        m.nodes[0].builtin = true;
        m.setProp("statusBar", "name", "\"statusBar\"");
        std::string fel;
        ok(!bld::migrateNames(m, fel),
           "builtin: även KANONISK name-rad vägras (kravet är SAKNAS)");
    }
    {
        // Utan rad är den kanoniska vägen orörd: builtins migreras aldrig.
        bld::WindowModel m;
        m.className = "BuiltinRen";
        m.appendNode("glaze::MenuBar", "menuBar");
        m.nodes[0].builtin = true;
        std::string fel;
        ok(bld::migrateNames(m, fel),
           "builtin: UTAN rad passerar migreringen");
        ok(m.stmts.empty(),
           "builtin: migreringen lägger INTE till en rad för en builtin");
    }

    // OPAK ANKARRAD ⇒ renameNode VÄGRAR. En ankarrad
    // som inte går att parsa bevaras opakt av regel 4 — men den kan peka
    // på noden vi byter namn på, utan att vi kan se det. Att lyckas till
    // hälften och lämna ankaret pekande på ett namn som inte längre finns
    // är värre än att säga nej: kanten faller TYST tillbaka på x/y.
    {
        bld::WindowModel m;
        m.className = "Opak";
        m.appendNode("glaze::Button", "Button1");
        m.appendNode("glaze::Button", "Button2");
        m.setProp("Button1", "name", "\"Button1\"");
        m.setProp("Button2", "name", "\"Button2\"");
        m.setProp("Button2", "anchorLeft", "makeAnchor(\"Button1\", 8)");
        std::string fel;
        ok(!bld::renameNode(m, "Button1", "PrimaryButton", fel) &&
               !fel.empty(),
           "opak ankarrad ⇒ renameNode VÄGRAR (" + fel + ")");
        ok(m.find("Button1") && !m.find("PrimaryButton"),
           "opak ankarrad: INGET döptes om (operationen är atomisk)");
    }

    // ---- renameNode: retarget + ankaruppdatering + literalvärdet.
    {
        bld::WindowModel m;
        m.className = "Byt";
        m.appendNode("glaze::Panel", "Panel1");
        m.appendNode("glaze::Button", "Button1");
        m.appendNode("glaze::Button", "Button2");
        m.setProp("Button1", "name", "\"Button1\"");
        m.setProp("Button1", "x", "10");
        m.setProp("Button2", "name", "\"Button2\"");
        m.setProp("Button2", "anchorLeft", bld::siblingAnchorText("Button1", 8));
        m.setAdoption("Button1", "Panel1");
        m.appendEvent("Button1", bld::EventBind{"onClick", "glaze::ClickArgs",
                                                "Button1Clicked"});

        std::string fel;
        ok(bld::renameNode(m, "Button1", "BtnStatus", fel),
           "rename: går igenom (" + fel + ")");
        ok(m.find("BtnStatus") && !m.find("Button1"),
           "rename: nodnamnet bytt");
        const bld::Stmt* x = m.findProp("BtnStatus", "x");
        ok(x && x->value == "10", "rename: fältsatsen retargetad");
        const bld::Stmt* nm = m.findProp("BtnStatus", "name");
        ok(nm && nm->value == "\"BtnStatus\"",
           "rename: LITERALVÄRDET bytt (annars BtnStatus.name = \"Button1\")");
        ok(bld::nameState(m, *m.find("BtnStatus")) ==
               bld::NameState::Kanoniskt,
           "rename: noden är fortsatt KANONISK efteråt");
        ok(m.parentOf("BtnStatus") == "Panel1",
           "rename: adoptionen följde med");
        const bld::Stmt* ank = m.findProp("Button2", "anchorLeft");
        std::string till;
        float       gap = 0.0f;
        ok(ank && bld::parseSiblingAnchorText(ank->value, till, gap) &&
               till == "BtnStatus" && gap == 8.0f,
           "rename: syskonankarets 'to' uppdaterat");
        bool ev = false;
        for (const bld::Stmt& s : m.stmts)
            if (s.kind == bld::Stmt::Kind::Event && s.target == "BtnStatus" &&
                s.handler == "Button1Clicked")
                ev = true;
        ok(ev, "rename: event-BINDNINGEN retargetad, handler-SYMBOLEN "
               "oförändrad");

        // Adoptionsföräldern bor i `name` på Adopt-satsen — byts DEN inte
        // hamnar barnen i roten vid nästa spara.
        std::string fel2;
        ok(bld::renameNode(m, "Panel1", "Ruta1", fel2),
           "rename: föräldern kan döpas om (" + fel2 + ")");
        ok(m.parentOf("BtnStatus") == "Ruta1",
           "rename: adoptionsFÖRÄLDERN uppdaterad");

        // Avvisningarna.
        std::string f3;
        ok(!bld::renameNode(m, "BtnStatus", "class", f3),
           "rename: C++-nyckelord avvisas");
        ok(!bld::renameNode(m, "BtnStatus", "2Button", f3),
           "rename: ogiltig identifierare avvisas");
        ok(!bld::renameNode(m, "BtnStatus", "Button2", f3),
           "rename: upptaget namn avvisas");
        ok(!bld::renameNode(m, "BtnStatus", "statusBar", f3),
           "rename: inbyggt namn avvisas");
        ok(bld::validNodeName("Button1") && !bld::validNodeName("") &&
               !bld::validNodeName("a b") && !bld::validNodeName("int"),
           "rename: validNodeName-tabellen");
    }

    // ---- FÖNSTERANROP MED EN MEDLEM SOM ARGUMENT:
    //      `setAcceptButton(Button1);` följer ett namnbyte och går med en
    //      borttagen nod — kvar hade raden pekat på en medlem som inte finns,
    //      och filen inte kompilerat. `setCancelButton(nullptr);` pekar inte på
    //      någon nod och rörs inte, och en STRÄNG med nodens namn är ingen
    //      medlem.
    {
        bld::WindowModel m;
        m.className = "Standard";
        m.appendNode("glaze::Button", "Button1");
        m.appendNode("glaze::Button", "Button2");
        m.setProp("Button1", "name", "\"Button1\"");
        m.setProp("Button2", "name", "\"Button2\"");
        m.appendWindowCall("setAcceptButton", "Button1");
        m.appendWindowCall("setCancelButton", "nullptr");
        m.appendWindowCall("setTitle", "\"Button1\"");
        const auto anrop = [&m](const std::string& metod) -> const bld::Stmt* {
            for (const bld::Stmt& s : m.stmts)
                if (s.kind == bld::Stmt::Kind::WindowCall && s.name == metod)
                    return &s;
            return nullptr;
        };
        std::string fel;
        ok(bld::renameNode(m, "Button1", "BtnStatus", fel),
           "medlemsargument: namnbytet går igenom (" + fel + ")");
        const bld::Stmt* acc = anrop("setAcceptButton");
        ok(acc && acc->value == "BtnStatus",
           "medlemsargument: setAcceptButton(Button1) blir "
           "setAcceptButton(BtnStatus)");
        const bld::Stmt* av = anrop("setCancelButton");
        ok(av && av->value == "nullptr",
           "medlemsargument: setCancelButton(nullptr) rörs inte av namnbytet");
        const bld::Stmt* titel = anrop("setTitle");
        ok(titel && titel->value == "\"Button1\"",
           "medlemsargument: en titelSTRÄNG med samma text rörs inte");
        m.removeNode("BtnStatus");
        ok(anrop("setAcceptButton") == nullptr,
           "medlemsargument: removeNode tar bort setAcceptButton(BtnStatus)");
        ok(anrop("setCancelButton") && anrop("setTitle"),
           "medlemsargument: övriga fönsteranrop står kvar efter removeNode");
    }

    // ---- ARGUMENTSPLITTAREN. Grinden har TVÅ jobb:
    //      bevisa att BEFINTLIGA platta anropsformer splittas EXAKT som
    //      en enkel delning (djupet är alltid noll vid deras separatorer), och att de
    //      nya nästlade formerna nu håller ihop. FÖRVÄNTAT VÄRDE är skrivet här —
    //      förväntningarna är handskrivna listor, aldrig hämtade ur
    //      implementationen.
    {
        auto dela = [](const std::string& s, std::vector<std::string>& ut) {
            return bld::splitArgs(s, ut);
        };
        auto lika = [&](const std::string& in,
                        const std::vector<std::string>& facit,
                        const std::string& vad) {
            std::vector<std::string> ut;
            bool                     bra = dela(in, ut) && ut.size() == facit.size();
            for (std::size_t i = 0; bra && i < facit.size(); ++i)
                bra = ut[i] == facit[i];
            std::string visat;
            for (const std::string& d : ut)
                visat += "[" + d + "]";
            ok(bra, vad + " → " + visat);
        };

        // BEFINTLIGA former — oförändrade.
        lika("", {}, "splitArgs: `show()` ger noll argument");
        lika("true", {"true"}, "splitArgs: ett bool-argument");
        lika("\"Arkiv\", \"Nytt fönster\", \"Ctrl+N\"",
             {"\"Arkiv\"", " \"Nytt fönster\"", " \"Ctrl+N\""},
             "splitArgs: menyradens tre strängar");
        lika("\"Spara som…, igen\"", {"\"Spara som…, igen\""},
             "splitArgs: komma INUTI en literal delar inte");
        lika("0, \"Bredd\", \"120\", false",
             {"0", " \"Bredd\"", " \"120\"", " false"},
             "splitArgs: setRow med fyra argument");
        lika("{\"a\", \"b\"}", {"{\"a\", \"b\"}"},
             "splitArgs: en listliteral är ETT argument");
        lika("{{\"Bredd\", \"120\"}, {\"Höjd\", \"80\"}}",
             {"{{\"Bredd\", \"120\"}, {\"Höjd\", \"80\"}}"},
             "splitArgs: nästlad radliteral är ETT argument");
        lika("40, 90, 180, 255", {"40", " 90", " 180", " 255"},
             "splitArgs: gfx::Color-kanalerna");

        // NYA former.
        lika("{\"a,b\"}", {"{\"a,b\"}"},
             "splitArgs: komma i en literal INUTI klammer");
        lika("{glaze::tr(1), glaze::tr(2)}", {"{glaze::tr(1), glaze::tr(2)}"},
             "splitArgs: nästlat tr-par är ETT argument");
        lika("a, g(b, c)", {"a", " g(b, c)"},
             "splitArgs: parenteserna räknas (två argument, inte tre)");
        lika("glaze::tr(1042)", {"glaze::tr(1042)"},
             "splitArgs: ett tr-anrop är ETT argument");

        // TOMMA och AVSLUTANDE argument.
        lika("a, ", {"a", " "}, "splitArgs: avslutande komma ger tomt sista");
        lika(" ", {}, "splitArgs: bara blanksteg ger noll argument");
        lika(", a", {"", " a"}, "splitArgs: tomt FÖRSTA argument bevaras");

        // ESCAPES i strängar — `\"` avslutar inte literalen.
        lika("\"a\\\",b\"", {"\"a\\\",b\""},
             "splitArgs: escapat citat avslutar inte literalen");

        // AVSLAGEN — det är STACKEN som mäts, inte två räknare.
        std::vector<std::string> ut;
        ok(!dela("({)}", ut),
           "splitArgs: felordnade delimiters ({)} AVVISAS (stacken)");
        ok(!dela("{a", ut), "splitArgs: oavslutad klammer avvisas");
        ok(!dela("a)", ut), "splitArgs: överskjutande ) avvisas");
        ok(!dela("\"a", ut), "splitArgs: oavslutad strängliteral avvisas");
    }

    // ---- ÖVERSÄTTNINGEN: `glaze::tr(N)` som fältvärde.
    //      STRIKT delmängd efter `parseImgCall`s prejudikat — allt utanför
    //      den blir en OPAK rad, aldrig en gissning.
    {
        std::uint32_t id = 0;
        ok(bld::parseTrCall("glaze::tr(1042)", id) && id == 1042,
           "parseTrCall: den kanoniska formen");
        ok(bld::parseTrCall("  glaze::tr( 7 )  ", id) && id == 7,
           "parseTrCall: blanksteg runt och inuti");
        ok(bld::trCallText(1042) == "glaze::tr(1042)",
           "trCallText: kanonisk emission");
        ok(bld::trCallText(0).empty(), "trCallText: id 0 kan inte emitteras");

        ok(!bld::parseTrCall("tr(1042)", id),
           "parseTrCall: utan namnrymd avvisas");
        ok(!bld::parseTrCall("glaze::tr(glaze::str::Ok)", id),
           "parseTrCall: KONSTANT avvisas (builder_core har ingen reflektion)");
        ok(!bld::parseTrCall("glaze::tr(0x412)", id),
           "parseTrCall: hexform avvisas");
        ok(!bld::parseTrCall("glaze::tr(1042u)", id),
           "parseTrCall: suffix avvisas");
        ok(!bld::parseTrCall("glaze::tr(-1)", id),
           "parseTrCall: tecken avvisas");
        ok(!bld::parseTrCall("glaze::tr(0)", id),
           "parseTrCall: id 0 avvisas (0 = ingen översättning)");
        ok(!bld::parseTrCall("glaze::tr()", id),
           "parseTrCall: tomt argument avvisas");
        ok(!bld::parseTrCall("glaze::tr(1042) + x", id),
           "parseTrCall: släpande skräp avvisas");
        ok(!bld::parseTrCall("\"glaze::tr(1042)\"", id),
           "parseTrCall: citerad form är en LITERAL, inte ett tr-anrop");

        // KLASSIFICERINGEN — den GlazeBuilders panel konsumerar.
        std::string   text;
        std::uint32_t tid = 0;
        ok(bld::classifyTextValue("glaze::tr(1042)", text, tid) ==
                   bld::TextValueKind::TrId &&
               tid == 1042,
           "classifyTextValue: tr-id");
        ok(bld::classifyTextValue("\"Tryck här\"", text, tid) ==
                   bld::TextValueKind::Literal &&
               text == "Tryck här",
           "classifyTextValue: strängliteral avkodas");
        ok(bld::classifyTextValue("someExpr()", text, tid) ==
               bld::TextValueKind::Other,
           "classifyTextValue: allt annat är Other (opakt — rör det inte)");
        // ETT tr-anrop med en konstant är en egen
        // klass — opakt som Other, men panelen skyddar det som ett tr-id och
        // loggen säger "skriv numret". Två anrop i ett uttryck, ett tomt
        // anrop och ett tr-anrop med släpande skräp är INTE ett anrop.
        //
        // SABOTAGE: ta bort TrExpr-grenen i `classifyTextValue` ⇒ de två
        // första faller.
        ok(bld::classifyTextValue("glaze::tr(app::str::Hej)", text, tid) ==
               bld::TextValueKind::TrExpr,
           "classifyTextValue: glaze::tr(<konstant>) är TrExpr");
        ok(bld::classifyTextValue("  glaze::tr(kOk)  ", text, tid) ==
               bld::TextValueKind::TrExpr,
           "classifyTextValue: TrExpr också med blanksteg runt");
        ok(bld::classifyTextValue("glaze::tr(a) + glaze::tr(b)", text, tid) ==
               bld::TextValueKind::Other,
           "classifyTextValue: två tr-anrop i ett uttryck är Other");
        ok(bld::classifyTextValue("glaze::tr()", text, tid) ==
               bld::TextValueKind::Other,
           "classifyTextValue: ett tomt tr-anrop är Other");
        ok(bld::classifyTextValue("glaze::tr(kOk) + x", text, tid) ==
               bld::TextValueKind::Other,
           "classifyTextValue: släpande skräp efter anropet är Other");

        // DATAFÖRLUSTEN, mätt. Det här ÄR skälet till att
        // GlazeBuilders panel visar tr-rader skrivskyddade: går värdet
        // genom `FieldKind::Text`-committen kodas det som en
        // STRÄNGLITERAL, och id:t finns inte längre. Grinden mäter att de
        // två formerna är OLIKA — hade de varit lika vore spärren
        // onödig, och hade någon "förenklat" bort den vore det tyst.
        std::string kodad;
        const bool  förlust =
            bld::cppString("glaze::tr(1042)", kodad) &&
            kodad != "glaze::tr(1042)" &&
            bld::classifyTextValue(kodad, text, tid) ==
                bld::TextValueKind::Literal;
        ok(förlust,
           "en tr-rad genom textcommitten blir en LITERAL — id:t är BORTA "
           "(" + kodad + ")");
    }

    // ---- MENYSTIGEN: SEGMENTERAD tokenstig.
    {
        std::vector<bld::PathSeg> segs;

        ok(bld::parseMenuPath("\"Arkiv\"", segs) && segs.size() == 1 &&
               !segs[0].isId && segs[0].text == "Arkiv",
           "parseMenuPath: ETT literalsegment utan klamrar");
        ok(bld::parseMenuPath("glaze::tr(12)", segs) && segs.size() == 1 &&
               segs[0].isId && segs[0].id == 12,
           "parseMenuPath: ETT tr-segment utan klamrar");
        ok(bld::parseMenuPath("{\"Utseende\", \"Läge\"}", segs) &&
               segs.size() == 2 && segs[1].text == "Läge",
           "parseMenuPath: TVÅ literalsegment");
        ok(bld::parseMenuPath("{glaze::tr(1), glaze::tr(2)}", segs) &&
               segs.size() == 2 && segs[0].isId && segs[1].id == 2,
           "parseMenuPath: TVÅ tr-segment (den strukturella splittaren)");
        ok(bld::parseMenuPath("{\"Utseende\", glaze::tr(2)}", segs) &&
               segs.size() == 2 && !segs[0].isId && segs[1].isId,
           "parseMenuPath: BLANDAD stig är laglig — matchningen är per segment");
        ok(bld::parseMenuPath("{\"Spara som…, igen\"}", segs) &&
               segs.size() == 1 && segs[0].text == "Spara som…, igen",
           "parseMenuPath: komma inuti en literal delar inte segmenten");

        // EMISSIONEN — kanonisk, och den nakna formen för ETT segment.
        {
            std::vector<bld::PathSeg> a(2);
            a[0].text = "Utseende";
            a[1].isId = true;
            a[1].id   = 42;
            ok(bld::menuPathText(a) == "{\"Utseende\", glaze::tr(42)}",
               "menuPathText: kanonisk klammerform (" + bld::menuPathText(a) +
                   ")");
            a.resize(1);
            ok(bld::menuPathText(a) == "\"Utseende\"",
               "menuPathText: ETT segment emitteras NAKET");
            ok(bld::menuPathText({}).empty(),
               "menuPathText: tom stig kan inte emitteras");
        }

        // AVVISNINGSFALLEN — varje ett ⇒ OPAK rad.
        ok(!bld::parseMenuPath("\"Utseende/Läge\"", segs),
           "parseMenuPath: SNEDSTRECK i ett literalsegment AVVISAS "
           "(migreringen ska synas, inte tystna)");
        ok(!bld::parseMenuPath("{\"a\", \"b/c\"}", segs),
           "parseMenuPath: snedstreck i ETT av flera segment avvisas");
        ok(!bld::parseMenuPath("", segs), "parseMenuPath: tom stig avvisas");
        ok(!bld::parseMenuPath("{}", segs),
           "parseMenuPath: tom klammerlista avvisas");
        ok(!bld::parseMenuPath("{\"a\"", segs),
           "parseMenuPath: saknad avslutande klammer avvisas");
        ok(!bld::parseMenuPath("{\"a\", }", segs),
           "parseMenuPath: TOMT segment avvisas");
        ok(!bld::parseMenuPath("{\"a\", namn}", segs),
           "parseMenuPath: segment som varken är literal eller tr avvisas");
        ok(!bld::parseMenuPath("{\"a\", glaze::tr(kOk)}", segs),
           "parseMenuPath: tr med konstant avvisas");
        ok(!bld::parseMenuPath("{{\"a\"}, \"b\"}", segs),
           "parseMenuPath: NÄSTLAD klammer är inget segment");
    }

    // ---- Grammatiken: trasiga rader avkodas INTE (men bevaras av writern).
    {
        std::vector<bld::DesignPair> par;
        ok(!bld::parseDesignLine("ui=Slate ui=Glaze", par),
           "dubblerad NYCKEL i raden ⇒ ingen avkodning");
        ok(!bld::parseDesignLine("ui", par), "fält utan '=' ⇒ ingen avkodning");
        ok(!bld::parseDesignLine("ui=", par), "tomt värde ⇒ ingen avkodning");
        ok(!bld::parseDesignLine("UI=Slate", par),
           "versal nyckel ⇒ ingen avkodning (nyckel = [a-z]+)");
        ok(bld::parseDesignLine("", par) && par.empty(),
           "tom rad ⇒ noll fält, inget fel");
        ok(bld::parseDesignLine("theme=slate(Teal)", par) && par.size() == 1 &&
               par[0].value == "slate(Teal)",
           "värde med parenteser klaras");
        // Blanksteg i ett värde är INTE ett värde med blanksteg — raden
        // splittas på whitespace, så det blir två fält (och här ett fel).
        ok(!bld::parseDesignLine("theme=slate( Teal )", par),
           "blanksteg i värdet ⇒ trasig rad (dokumenterad fälla)");
    }

    // ---- WIDGETARNAS SETTER-FÄLT. Byte-identiteten (spar-policyn ovan)
    //      täcker RADERNA; det här täcker KLASSNINGEN — de nya setter-fälten
    //      är Props som writern skriver i anropsform, kollektionsformerna och
    //      klockslaget Call-satser vars argument står ordagrant kvar, och
    //      `rutnat.onSelectionChange` (KORT) en koppling. SABOTAGEN: en rad
    //      borttagen ur `kSetterFields` (Propen saknas — raden blir en Call
    //      som round-trippar lika byte-exakt, så BARA det här fallet ser det),
    //      `onSelectionChange` borttagen ur `isKnownEvent` (en opak Prop).
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "fas1b: calls.h läses (" + r.error + ")");
        const bld::WindowModel& m = r.model;
        struct Fält {
            const char* id;
            const char* fält;
            const char* värde;
        };
        for (const Fält& p :
             {Fält{"kryss", "threeState", "true"},
              Fält{"kryss", "checkState", "glaze::CheckState::Indeterminate"},
              Fält{"flerval", "multiSelect", "true"},
              Fält{"rutnat", "selectionMode",
                   "glaze::DataGrid::SelectionMode::FullRow"},
              Fält{"rutnat", "multiSelect", "true"},
              Fält{"alfa", "allowAlpha", "true"},
              Fält{"alfa", "color", "\"#3366FF80\""},
              Fält{"tid", "showTime", "true"}}) {
            const bld::Stmt* s = m.findProp(p.id, p.fält);
            ok(s && s->value == p.värde,
               std::string("fas1b: ") + p.id + "." + p.fält +
                   " är en Prop med värdet ordagrant");
        }
        const auto följd = [&m](const char* id) {
            std::string ut;
            for (const bld::Stmt* s : anropen(m, id))
                ut += s->name + "(" + s->value + ") ";
            return ut;
        };
        ok(följd("flerval") ==
               "selectAll() clearSelection() setSelected(1, true) "
               "setSelected(3, true) ",
           "fas1b: flervalets anrop i källordning (" + följd("flerval") + ")");
        ok(följd("flikar") ==
               "addPage(\"Andra\") addPage(glaze::tr(3)) "
               "setPage(1, \"Andra sidan\") ",
           "fas1b: flikarnas sidor är Call-satser (" + följd("flikar") + ")");
        ok(följd("flikar2") ==
               "setPages({\"A\", glaze::tr(3), \"C\"}) setPage(0, \"Ett\") ",
           "fas1b: setPages bevaras ordagrant (" + följd("flikar2") + ")");
        ok(följd("tid") == "setDate(2026, 10, 13) setTime(14, 30) ",
           "fas1b: klockslaget är en Call med två heltal (" + följd("tid") +
               ")");
        const std::string sb = följd("statusBar");
        ok(sb.find("setPanels({{\"Klar\", 0, true}, {glaze::tr(3), 110}}) "
                   "addPanel(\"100 %\") setPanel(2, \"75 %\", 60) "
                   "setPanelText(0, \"Sparat\") ") != std::string::npos,
           "fas1b: statusradens fält är Call-satser i källordning (" + sb +
               ")");
        const bld::Stmt* ev = nullptr;
        for (const bld::Stmt& s : m.stmts)
            if (s.kind == bld::Stmt::Kind::Event && s.target == "rutnat" &&
                s.name == "onSelectionChange")
                ev = &s;
        ok(ev && ev->shortForm && ev->handler == "rutnatValt" &&
               m.findProp("rutnat", "onSelectionChange") == nullptr,
           "fas1b: rutnat.onSelectionChange (kort) är Kind::Event");
        const std::string gui = bld::emitGuiCpp(m, "calls.h", "\n");
        ok(gui.find("    kryss.setCheckState(glaze::CheckState::Indeterminate);"
                    "\n") != std::string::npos &&
               gui.find("    rutnat.setSelectionMode(glaze::DataGrid::"
                        "SelectionMode::FullRow);\n") != std::string::npos &&
               gui.find("    tid.setShowTime(true);\n") != std::string::npos,
           "fas1b: setter-fälten emitteras i anropsform");
    }

    // ---- SETTER-FÄLT OCH `renameNode`. Byte-identiteten (spar-policyn
    //      ovan) täcker RADERNA; det här täcker KLASSNINGEN — de elva nya
    //      setter-fälten är Props som writern skriver i anropsform, och
    //      knapparnas `setColor(std::optional)` Call-satser med argumentet
    //      ordagrant. Och `renameNode` byter GRANNENS literal och vägrar
    //      vid en opak `buddy`-rad, FÖRE all mutation. SABOTAGEN: en rad
    //      borttagen ur `kSetterFields` (Propen saknas), `renameNode` utan
    //      buddy-grenen (literalen står kvar), utan vägran (namnbytet lyckas).
    //      MÄTER INTE att formerna kompilerar — det gör `calls_fixture`.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "calls: calls.h läses (" + r.error + ")");
        bld::WindowModel& m = r.model;
        struct Fält {
            const char* id;
            const char* fält;
            const char* värde;
        };
        for (const Fält& p :
             {Fält{"tabell", "fillColumn", "0"},
              Fält{"tabell", "iconSize", "48"},
              Fält{"rutnat", "fillColumn", "1"},
              Fält{"rutnat", "iconSize", "20"},
              Fält{"etikett", "elide", "true"},
              Fält{"etikett", "vAlign", "glaze::VAlign::Top"},
              Fält{"etikett", "buddy", "\"falt\""},
              Fält{"btn", "iconSize", "32"},
              Fält{"nyKnapp", "iconSize", "24"},
              Fält{"anteckning", "acceptsTab", "true"},
              Fält{"filval", "dialogTitle", "\"Välj CSV-fil\""}}) {
            const bld::Stmt* s = m.findProp(p.id, p.fält);
            ok(s && s->value == p.värde,
               std::string("calls: ") + p.id + "." + p.fält +
                   " är en Prop med värdet ordagrant");
        }
        const auto färgen = [&m](const char* id) {
            std::string ut;
            for (const bld::Stmt* s : anropen(m, id))
                if (s->name == "setColor")
                    ut += s->value + " ";
            return ut;
        };
        ok(färgen("kryss") == "gfx::Color{0, 120, 0} " &&
               färgen("val1") == "{} " && m.findProp("kryss", "color") == nullptr &&
               m.findProp("val1", "color") == nullptr,
           "calls: kryss/val1.setColor är Call-satser (" + färgen("kryss") + "/" +
               färgen("val1") + ")");
        const std::string gui = bld::emitGuiCpp(m, "calls.h", "\n");
        ok(gui.find("    tabell.setFillColumn(0);\n") != std::string::npos &&
               gui.find("    etikett.setBuddy(\"falt\");\n") != std::string::npos &&
               gui.find("    filval.setDialogTitle(\"Välj CSV-fil\");\n") !=
                   std::string::npos,
           "calls: setter-fälten emitteras i anropsform");

        // GRANNEN FÖLJER NAMNBYTET — och ingen annan rad rörs av det.
        bld::WindowModel kopia = m;
        std::string      fel2;
        ok(bld::renameNode(kopia, "falt", "hojd", fel2),
           "calls: renameNode falt → hojd lyckas (" + fel2 + ")");
        const bld::Stmt* g = kopia.findProp("etikett", "buddy");
        ok(g && g->value == "\"hojd\"",
           "calls: etikett.buddy följer namnbytet (" +
               (g ? g->value : std::string("saknas")) + ")");

        // En OPAK buddy-rad (inget strängliteral) ⇒ VÄGRAN, och modellen orörd.
        bld::WindowModel opak = m;
        for (bld::Stmt& s : opak.stmts)
            if (s.kind == bld::Stmt::Kind::Prop && s.target == "etikett" &&
                s.name == "buddy")
                s.value = "grannensNamn";
        std::string fel3;
        ok(!bld::renameNode(opak, "falt", "hojd", fel3) && !fel3.empty() &&
               opak.find("falt") != nullptr && opak.find("hojd") == nullptr,
           "calls: en opak buddy-rad ⇒ renameNode VÄGRAR före mutationen (" +
               fel3 + ")");
    }

    // ---- MARKDOWN-VYN — calls-fixturens `hjalp`.
    //      Dokumentet är setter-fältet `text`: literalen med `\n`, `\"` och
    //      `\\` står ORDAGRANT i Propen (och round-trippar byte-exakt genom
    //      fixturens byte-identitet ovan), `glaze::tr(2)` likaså. `setImage`
    //      och `scrollToAnchor` är Call-satser med argumenten ordagranna,
    //      `setBorder(true)` Panels setter-fält, och `onLinkClick` står i
    //      BÅDA formerna som `Kind::Event`. SABOTAGET som ska fälla den:
    //      `onLinkClick` borttagen ur `isKnownEvent` — den korta kopplingen
    //      blir då en opak Prop som round-trippar LIKA byte-exakt,
    //      och bara det här blocket ser det. MÄTER INTE att raderna
    //      kompilerar — det gör `calls_fixture`.
    {
        bld::ReadResult r =
            bld::readWindow(root() + "tests/fixtures/calls.h");
        ok(r.ok, "markdown: calls.h läses (" + r.error + ")");
        const bld::WindowModel& m = r.model;
        const bld::WidgetNode*  n = m.find("hjalp");
        ok(n && n->type == "glaze::MarkdownView" && m.isAdopted("hjalp"),
           "markdown: hjalp är en adopterad glaze::MarkdownView");
        std::vector<std::string> texter;
        for (const bld::Stmt& s : m.stmts)
            if (s.kind == bld::Stmt::Kind::Prop && s.target == "hjalp" &&
                s.name == "text")
                texter.push_back(s.value);
        const std::string literal =
            "\"# Nyheter\\n\\n- **Snabbare** start\\n- Citat: \\\"ja\\\" och \\\\ "
            "ett snedstreck\\n- Se [installationen](#installation)\\n\\n"
            "## Installation\\n\\nKör `make`.\"";
        ok(texter.size() == 2 && texter[0] == literal &&
               texter[1] == "glaze::tr(2)",
           "markdown: setText normaliseras till Prop — literalen och tr-id:t "
           "ordagranna");
        const auto  anrop = anropen(m, "hjalp");
        std::string följd;
        for (const bld::Stmt* s : anrop)
            följd += s->name + " ";
        ok(följd == "setImage setImage scrollToAnchor ",
           "markdown: bilderna och ankaret är Call-satser i källordning (" +
               följd + ")");
        ok(anrop.size() == 3 &&
               anrop[0]->value == "\"logo\", glaze::img(\"toolbar\", \"info\")" &&
               anrop[1]->value == "\"tom\", {}" &&
               anrop[2]->value == "\"installation\"",
           "markdown: argumenten bevaras ordagrant");
        const bld::Stmt* ram = m.findProp("hjalp", "border");
        ok(ram && ram->value == "true",
           "markdown: setBorder(true) är Panels setter-fält border");
        const bld::Stmt* full = nullptr;
        const bld::Stmt* kort = nullptr;
        for (const bld::Stmt& s : m.stmts)
            if (s.kind == bld::Stmt::Kind::Event && s.target == "hjalp" &&
                s.name == "onLinkClick")
                (s.shortForm ? kort : full) = &s;
        ok(full && full->args == "glaze::LinkClickArgs" &&
               full->handler == "hjalpLank" && kort &&
               kort->handler == "lankKlickad" &&
               m.findProp("hjalp", "onLinkClick") == nullptr,
           "markdown: onLinkClick i båda formerna är Kind::Event och ingen "
           "opak Prop");
        const std::string gui = bld::emitGuiCpp(m, "calls.h", "\n");
        ok(gui.find("    hjalp.setText(" + literal + ");\n") != std::string::npos &&
               gui.find("    hjalp.onLinkClick = [this] { lankKlickad(); };\n") !=
                   std::string::npos,
           "markdown: texten emitteras i anropsform och kopplingen KORT");
    }

    // ---- TERMINALEN. Byte-identiteten ovan bevisar att
    // formen överlever läs → skriv; de här raderna bevisar att MODELLEN bär den:
    // setter-fälten som FÄLT (en reader som missade dem hade fått writern att
    // skriva samma rad men previewn att tappa dem), anropen med flera argument
    // som Calls i källordning, och båda kopplingsformerna som Events — raden i
    // `isKnownEvent` (sabotaget att ta bort den fäller här).
    {
        bld::ReadResult r = bld::readWindow(root() + "tests/fixtures/terminal.h");
        ok(r.ok, "terminal: fixturen läses (" + r.error + ")");
        if (r.ok) {
            const bld::WindowModel& m = r.model;
            ok(m.nodes.size() == 1 && m.nodes[0].type == "glaze::Terminal",
               "terminal: noden är en glaze::Terminal");
            const struct {
                const char* fält;
                const char* värde;
            } fält[] = {
                {"program", "\"pwsh\""},
                {"arguments", "\"-NoLogo\""},
                {"workingDirectory", "\"C:/src\""},
                {"colorScheme", "glaze::TerminalScheme::Solarized"},
                {"colorMode", "glaze::TerminalColorMode::Dark"},
                {"scrollback", "5000"},
                {"altAsMeta", "false"},
                {"textSize", "glaze::px(12)"},
            };
            for (const auto& f : fält) {
                const bld::Stmt* p = m.findProp("term", f.fält);
                ok(p && p->value == f.värde,
                   std::string("terminal: setter-fältet ") + f.fält + " är ett fält (" +
                       (p ? p->value : std::string("saknas")) + ")");
            }
            const auto anrop = anropen(m, "term");
            ok(anrop.size() == 4 && anrop[0]->name == "setEnvironmentVariable" &&
                   anrop[0]->value == "\"LANG\", \"sv_SE.UTF-8\"" &&
                   anrop[1]->name == "unsetEnvironmentVariable" &&
                   anrop[2]->name == "setSchemeColor" &&
                   anrop[2]->value ==
                       "glaze::ThemeMode::Dark, glaze::TerminalColor::Red, "
                       "gfx::Color{230, 90, 80}" &&
                   anrop[3]->name == "start",
               "terminal: miljön, den egna färgen och start() är anrop i "
               "källordning, ordagranna");
            const bld::Stmt* slut  = nullptr;
            const bld::Stmt* titel = nullptr;
            for (const bld::Stmt& s : m.stmts) {
                if (s.kind != bld::Stmt::Kind::Event || s.target != "term")
                    continue;
                if (s.name == "onExit")
                    slut = &s;
                if (s.name == "onTitleChange")
                    titel = &s;
            }
            ok(slut && !slut->shortForm && slut->args == "glaze::EventArgs" &&
                   slut->handler == "termSlut" && titel && titel->shortForm &&
                   titel->handler == "visaTitel" &&
                   m.findProp("term", "onExit") == nullptr &&
                   m.findProp("term", "onTitleChange") == nullptr,
               "terminal: onExit (fullform) och onTitleChange (kort) är "
               "Kind::Event och inga opaka rader");
        }
    }

    std::printf(fel ? "\nroundtrip_gate: %d fall FÖLL\n"
                    : "\nroundtrip_gate: allt grönt (%d fel)\n",
                fel);
    return fel ? 1 : 0;
}
