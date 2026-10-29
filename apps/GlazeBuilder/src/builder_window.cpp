#include "builder_window.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "apply_props.h"
#include "cpp_reader.h"
#include "cpp_writer.h"
#include "design_theme.h"
#include "palette.h"
#include "preview_tree.h"
#include "standin_widget.h"

namespace {

constexpr float kToolbox = 170.0f;   // vänsterpanelens bredd, logiska px
constexpr float kProps   = 240.0f;   // högerpanelens bredd
constexpr float kMargin  = 16.0f;    // luft runt canvasen
constexpr int   kSnap    = 4;        // placering/drag snappar till rutnät
constexpr std::uint64_t kDblClickMs = 400;
// Hörnhandtagen. `kHandle` är RITMÅTTET (fysiska px, oskalat — handtaget
// är kosmetiskt och ska vara lika stort oavsett zoom), `kHandleSlack` den
// extra TRÄFFmarginalen: ett 6-px-mål är svårt att pricka med musen, och
// ritningen ska inte växa bara för att träffytan gör det.
constexpr float kHandle      = 6.0f;
constexpr float kHandleSlack = 3.0f;
// Minsta storlek en widget kan dras till, LOGISKA px. GlazeBuilder är en
// APP — literalen hör hemma här och inte i Style (regel 3 gäller
// Glaze/widgets/src/widgets).
constexpr float kMinSize = 8.0f;

// "start_window.h" → "StartWindow" (mallens klassnamn ur filnamnet).
std::string classNameFromPath(const std::string& hPath)
{
    size_t s = hPath.find_last_of("/\\");
    std::string base =
        s == std::string::npos ? hPath : hPath.substr(s + 1);
    const size_t dot = base.rfind(".h");
    if (dot != std::string::npos)
        base = base.substr(0, dot);

    std::string out;
    bool up = true;
    for (char c : base) {
        if (c == '_' || c == '-') {
            up = true;
            continue;
        }
        out += up ? static_cast<char>(
                        std::toupper(static_cast<unsigned char>(c)))
                  : c;
        up = false;
    }
    return out.empty() ? "Window1" : out;
}

std::string baseName(const std::string& p)
{
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

} // namespace

// Paletten (verktygslådans metadata: C++-klassnamn ↔ namn ↔ default-storlek)
// bor i palette.h/.cpp . Widgetarna skapas ur
// REGISTRET (`bld::makeNodeWidget`), inte ur paletten.

// Fält-vokabulären: kind styr omkodningen tillbaka till C++-text (Text →
// citerad literal, Number/Bool → rå, Integer → rå men heltalsgrindad,
// Enum → prefix + val, Raw → som den är efter validering). Ordningen
// spelar ingen roll — mest specifika (cppType satt) vinner i fieldMeta.
const BuilderWindow::FieldMeta BuilderWindow::kFieldMeta[] = {
    // Bas-placeringen (alla typer)
    {nullptr, "x",       FieldKind::Number, "", ""},
    {nullptr, "y",       FieldKind::Number, "", ""},
    {nullptr, "right",   FieldKind::Number, "", ""},
    {nullptr, "bottom",  FieldKind::Number, "", ""},
    {nullptr, "width",   FieldKind::Number, "", ""},
    {nullptr, "height",  FieldKind::Number, "", ""},
    {nullptr, "dock",    FieldKind::Enum,
     "None;Left;Top;Right;Bottom;Fill", "glaze::Dock::"},
    {nullptr, "anchors", FieldKind::Raw, "", ""},
    // SYSKONANKARNA — Raw av samma skäl som
    // `anchors`: värdet ÄR en C++-structliteral och redigeras som text,
    // med `commitPropEdit`s Raw-gren som grind mot rader som inte
    // kompilerar. De är LAYOUT-fält och ligger därför medvetet UTANFÖR
    // `kSetterFields` (window_model.cpp) — kontraktet i widget.h säger att
    // placeringens fält är RÅA. Den bekväma vägen är kopplingsläget;
    // den här raden är den som gör värdet läsbart och finjusterbart.
    {nullptr, "anchorLeft",   FieldKind::Raw, "", ""},
    {nullptr, "anchorTop",    FieldKind::Raw, "", ""},
    {nullptr, "anchorRight",  FieldKind::Raw, "", ""},
    {nullptr, "anchorBottom", FieldKind::Raw, "", ""},
    // Tab-ordningen (del C) — generell, alla typer. Raden SYNTETISERAS i
    // refreshPropPanel för noder som saknar propen, men skrivs till
    // modellen först när användaren faktiskt ändrar den (regel 4/5).
    {nullptr, "tabIndex", FieldKind::Integer, "", ""},
    // DRAG-AND-DROP — basens egenskaper, alla typer. Bool och
    // inte Raw: värdet är `true`/`false` och inget annat.
    {nullptr, "allowDrag", FieldKind::Bool, "true;false", ""},
    {nullptr, "allowDrop", FieldKind::Bool, "true;false", ""},
    // TOOLTIPEN — basens textfält, alla typer. Text som `text`:
    // ett översättnings-id visas skrivskyddat (översättningsspärren nedan gäller
    // varje Prop vars värde är `glaze::tr(N)`, inte bara `text`).
    {nullptr, "toolTip",   FieldKind::Text, "", ""},
    // PEKAREN — basens egenskap, alla typer. Valen är
    // `gfx::PointerShape`s poster; `resetCursor()` (tillbaka till typens
    // egen) är en Call och har ingen panelrad.
    {nullptr, "cursor", FieldKind::Enum,
     "Arrow;IBeam;Hand;ResizeWE;ResizeNS;NotAllowed;Crosshair;Move;ClosedHand;"
     "Hidden;Wait",
     "gfx::PointerShape::"},
    // Widget-specifika
    {"glaze::Label",     "text",    FieldKind::Text, "", ""},
    {"glaze::Label",     "role",    FieldKind::Enum,
     "Text;Dim;Accent;Titlebar;Heading", "glaze::Label::Role::"},
    {"glaze::Label",     "align",   FieldKind::Enum,
     "Left;Center;Right", "glaze::Align::"},
    // RADBRYTNINGEN är en PUBLIK egenskap, och en
    // publik egenskap som editorn inte modellerar är ett brutet
    // WYSIWYG-kontrakt: previewn hade ritat en rad där appen ritar tre.
    {"glaze::Label",     "wrap",    FieldKind::Bool, "true;false", ""},
    // KORTNINGEN, HÖJDLEDET OCH GRANNEN.
    {"glaze::Label",     "elide",   FieldKind::Bool, "true;false", ""},
    {"glaze::Label",     "vAlign",  FieldKind::Enum,
     "Top;Center;Bottom", "glaze::VAlign::"},
    {"glaze::Label",     "buddy",   FieldKind::Text, "", ""},
    // BREDDGRUPPEN — etiketterna med samma namn delar den
    // bredaste medlemmens bredd. En publik egenskap utan panelrad vore ett
    // brutet WYSIWYG-kontrakt.
    {"glaze::Label",     "widthGroup", FieldKind::Text, "", ""},
    // GLORIANS BREDD — logiska px, 0 = stilens. Färgen är en
    // anropssats (`setHalo(std::optional<gfx::Color>)`) utan panelrad.
    {"glaze::Label",     "haloWidth", FieldKind::Number, "", ""},
    {"glaze::Button",    "text",    FieldKind::Text, "", ""},
    // IKONENS SIDA — 0 = AUTO.
    {"glaze::Button",    "iconSize", FieldKind::Number, "", ""},
    // DIALOGKNAPPEN — knappens svar när den stänger sitt
    // fönster; `None` är en vanlig knapp. Fönstrets `setAcceptButton(ok);` är
    // ett fönsteranrop och har ingen panelrad här (fas 2).
    {"glaze::Button",    "dialogResult", FieldKind::Enum,
     "None;OK;Cancel;Yes;No;Abort;Retry;Ignore", "glaze::DialogResult::"},
    {"glaze::Checkbox",  "text",    FieldKind::Text, "", ""},
    {"glaze::Checkbox",  "checked", FieldKind::Bool, "true;false", ""},
    // TRE LÄGEN.
    {"glaze::Checkbox",  "threeState", FieldKind::Bool, "true;false", ""},
    {"glaze::Checkbox",  "checkState", FieldKind::Enum,
     "Unchecked;Checked;Indeterminate", "glaze::CheckState::"},
    {"glaze::TextInput", "text",    FieldKind::Text, "", ""},
    {"glaze::TextInput", "readOnly", FieldKind::Bool, "true;false", ""},
    // Lösenordsläget — panelraden bredvid `readOnly`: en
    // publik egenskap som editorn inte modellerar är ett brutet
    // WYSIWYG-kontrakt (fas 2-ytan, samma prejudikat).
    {"glaze::TextInput", "password", FieldKind::Bool, "true;false", ""},
    // PLATSHÅLLAREN OCH MAXLÄNGDEN — hinten är en text
    // som `text` (ett tr-id visas upplöst och skrivskyddat), gränsen ett
    // heltal i kodpunkter (0 = obegränsat).
    {"glaze::TextInput", "placeholder", FieldKind::Text, "", ""},
    {"glaze::TextInput", "maxLength", FieldKind::Integer, "", ""},
    {"glaze::TextInput", "align",   FieldKind::Enum,
     "Left;Center;Right", "glaze::Align::"},
    {"glaze::Panel",     "border",  FieldKind::Bool, "true;false", ""},
    {"glaze::Panel",     "clip",    FieldKind::Bool, "true;false", ""},
    // REN RAD — flexraden utan yta och hårlinje.
    {"glaze::Bar",       "plain",   FieldKind::Bool, "true;false", ""},
    // Fältet heter `selectedIndex` i `kSetterFields` och i modellen — en
    // panelrad med en annan nyckel (`selected`) vore en nyckel som bara
    // `Dropdown::setProp` känner, medan den GENERERADE raden
    // (`setSelectedIndex(1);`) blev opak i previewn. Ett namn, hela vägen.
    {"glaze::Dropdown",  "selectedIndex", FieldKind::Integer, "", ""},
    {"glaze::Dropdown",  "items",      FieldKind::Raw, "", ""},
    // Fältets justering — Labels och TextInputs rad.
    {"glaze::Dropdown",  "align",      FieldKind::Enum,
     "Left;Center;Right", "glaze::Align::"},
    {"glaze::ListBox",   "selectedIndex", FieldKind::Integer, "", ""},
    {"glaze::ListBox",   "items",      FieldKind::Raw, "", ""},
    {"glaze::ListBox",   "showChecks", FieldKind::Bool, "true;false", ""},
    // FLERVALET — listvyns Bool-rad.
    {"glaze::ListBox",   "multiSelect", FieldKind::Bool, "true;false", ""},
    {"glaze::Scrollbar", "min",     FieldKind::Number, "", ""},
    {"glaze::Scrollbar", "max",     FieldKind::Number, "", ""},
    {"glaze::Scrollbar", "value",   FieldKind::Number, "", ""},
    {"glaze::Scrollbar", "page",    FieldKind::Number, "", ""},
    {"glaze::Scrollbar", "orientation", FieldKind::Enum,
     "Horizontal;Vertical", "glaze::Scrollbar::Orientation::"},
    // PropertyGrid: fältet står i settertabellen och behöver både en
    // widget-ände och en panelrad — annars round-trippar
    // `grid.setNameWidth(120);` korrekt men är både
    // oritad och oredigerbar. Namnkolumnens bredd är LOGISKA px.
    {"glaze::PropertyGrid", "nameWidth", FieldKind::Number, "", ""},
    // VERKTYGSKNAPPEN. Bildraden behöver ingen rad här — `img`
    // har en egen gren i `refreshPropPanel` (choices ur assets.bin).
    {"glaze::ToolButton", "text",      FieldKind::Text, "", ""},
    {"glaze::ToolButton", "imagePos",  FieldKind::Enum,
     "Left;Right;Above;Below", "glaze::ImagePos::"},
    {"glaze::ToolButton", "checkable", FieldKind::Bool, "true;false", ""},
    {"glaze::ToolButton", "checked",   FieldKind::Bool, "true;false", ""},
    // Den DELADE knappen. Rullgardinens poster är anrop och
    // har ingen panelrad.
    {"glaze::ToolButton", "split",     FieldKind::Bool, "true;false", ""},
    // IKONENS SIDA — Buttons rad, 0 = AUTO.
    {"glaze::ToolButton", "iconSize",  FieldKind::Number, "", ""},
    // Verktygsfältets BAND i en dockningsyta — 0 är yttersta.
    // Titeln i det flytande fönstrets rubrik (Text) och det
    // flytande läget och flytbarheten (Bool), som panelens.
    {"glaze::ToolBar",    "band",      FieldKind::Integer, "", ""},
    {"glaze::ToolBar",    "text",      FieldKind::Text, "", ""},
    {"glaze::ToolBar",    "floating",  FieldKind::Bool, "true;false", ""},
    {"glaze::ToolBar",    "floatable", FieldKind::Bool, "true;false", ""},
    // ÖVERFLÖDET — överflödsknappen när barnen inte ryms.
    {"glaze::ToolBar",    "overflow",  FieldKind::Bool, "true;false", ""},
    // GRUPPRAMEN — rubriken är `text`, som på varje widget.
    {"glaze::GroupBox",   "text",      FieldKind::Text, "", ""},
    // TALFÄLTET. `decimals` är ett HELTAL (`int` i runtime).
    {"glaze::SpinBox",    "value",     FieldKind::Number, "", ""},
    {"glaze::SpinBox",    "min",       FieldKind::Number, "", ""},
    {"glaze::SpinBox",    "max",       FieldKind::Number, "", ""},
    {"glaze::SpinBox",    "step",      FieldKind::Number, "", ""},
    {"glaze::SpinBox",    "decimals",  FieldKind::Integer, "", ""},
    // TRÄDET. Valet är en INDEXSTIG — Raw som `items`, med
    // `commitPropEdit`s Raw-gren som grind mot text som inte kompilerar.
    // Flervalet och kryssen är listvyns och ListBoxens
    // Bool-rader; noderna, kryssen och färgerna är anrop utan panelrad.
    {"glaze::TreeView",   "selectedPath", FieldKind::Raw, "", ""},
    {"glaze::TreeView",   "multiSelect",  FieldKind::Bool, "true;false", ""},
    {"glaze::TreeView",   "showChecks",   FieldKind::Bool, "true;false", ""},
    // TRÄDETS KOLUMNER — listvyns sortering, huvud och fyllkolumn;
    // kolumnerna och cellerna är anrop utan panelrad.
    {"glaze::TreeView",   "sortColumn",    FieldKind::Integer, "", ""},
    {"glaze::TreeView",   "sortAscending", FieldKind::Bool, "true;false", ""},
    {"glaze::TreeView",   "sortable",      FieldKind::Bool, "true;false", ""},
    {"glaze::TreeView",   "headerLines",   FieldKind::Integer, "", ""},
    {"glaze::TreeView",   "headerVAlign",  FieldKind::Enum,
     "Top;Center;Bottom", "glaze::VAlign::"},
    {"glaze::TreeView",   "fillColumn",    FieldKind::Integer, "", ""},
    // LISTVYN. Kolumnerna och raderna är ANROP (kollektionen) och
    // har ingen panelrad. Kryssens
    // synlighet är ListBoxens och trädets Bool-rad; själva
    // kryssen är anrop utan panelrad.
    {"glaze::ListView",   "selectedIndex", FieldKind::Integer, "", ""},
    {"glaze::ListView",   "multiSelect",   FieldKind::Bool, "true;false", ""},
    {"glaze::ListView",   "showChecks",    FieldKind::Bool, "true;false", ""},
    {"glaze::ListView",   "sortColumn",    FieldKind::Integer, "", ""},
    {"glaze::ListView",   "sortAscending", FieldKind::Bool, "true;false", ""},
    {"glaze::ListView",   "sortable",      FieldKind::Bool, "true;false", ""},
    // KOLUMNHUVUDET: radantalet är ett heltal (0 = AUTO),
    // justeringen i höjdled ett enum med `glaze::VAlign`-prefixet.
    {"glaze::ListView",   "headerLines",   FieldKind::Integer, "", ""},
    {"glaze::ListView",   "headerVAlign",  FieldKind::Enum,
     "Top;Center;Bottom", "glaze::VAlign::"},
    // FYLLKOLUMNEN (−1 = ingen) och IKONSTORLEKEN (0 = AUTO) —
    // datagridens likadant nedan.
    {"glaze::ListView",   "fillColumn",    FieldKind::Integer, "", ""},
    {"glaze::ListView",   "iconSize",      FieldKind::Number, "", ""},
    // RADRUBRIKERNA: en bool, båda tabellerna.
    {"glaze::ListView",   "rowHeaders",    FieldKind::Bool, "true;false", ""},
    // DATAGRIDDEN — sorteringen och huvudet; kolumntypen och den
    // aktuella cellen är ANROP med två argument och har ingen panelrad.
    {"glaze::DataGrid",   "sortColumn",    FieldKind::Integer, "", ""},
    {"glaze::DataGrid",   "sortAscending", FieldKind::Bool, "true;false", ""},
    {"glaze::DataGrid",   "sortable",      FieldKind::Bool, "true;false", ""},
    {"glaze::DataGrid",   "headerLines",   FieldKind::Integer, "", ""},
    {"glaze::DataGrid",   "headerVAlign",  FieldKind::Enum,
     "Top;Center;Bottom", "glaze::VAlign::"},
    {"glaze::DataGrid",   "fillColumn",    FieldKind::Integer, "", ""},
    {"glaze::DataGrid",   "iconSize",      FieldKind::Number, "", ""},
    {"glaze::DataGrid",   "rowHeaders",    FieldKind::Bool, "true;false", ""},
    // RADVALET — läget och flervalet.
    {"glaze::DataGrid",   "selectionMode", FieldKind::Enum,
     "Cell;FullRow", "glaze::DataGrid::SelectionMode::"},
    {"glaze::DataGrid",   "multiSelect",   FieldKind::Bool, "true;false", ""},
    // DELAREN. Måtten är LOGISKA px (Number), den fasta rutan ett
    // enum med typens eget prefix.
    {"glaze::Splitter",   "vertical",  FieldKind::Bool, "true;false", ""},
    {"glaze::Splitter",   "fixedPane", FieldKind::Enum,
     "First;Second", "glaze::Splitter::Pane::"},
    {"glaze::Splitter",   "paneSize",  FieldKind::Number, "", ""},
    {"glaze::Splitter",   "firstMin",  FieldKind::Number, "", ""},
    {"glaze::Splitter",   "secondMin", FieldKind::Number, "", ""},
    // DOCKNINGEN. Värdens layout är en STRÄNG (Text) — den
    // redigeras sällan för hand, men en publik egenskap som editorn inte
    // modellerar är ett brutet WYSIWYG-kontrakt. Stängknappen,
    // knappnålen och det flytande läget är Bool; panelens IKON
    // behöver ingen rad här — `img` har en egen gren i `refreshPropPanel`
    // (choices ur assets.bin), som verktygsknappens.
    {"glaze::DockPanel",  "text",      FieldKind::Text, "", ""},
    {"glaze::DockPanel",  "collapsed", FieldKind::Bool, "true;false", ""},
    {"glaze::DockPanel",  "pinned",    FieldKind::Bool, "true;false", ""},
    {"glaze::DockPanel",  "closable",  FieldKind::Bool, "true;false", ""},
    {"glaze::DockPanel",  "floating",  FieldKind::Bool, "true;false", ""},
    {"glaze::DockPanel",  "floatable", FieldKind::Bool, "true;false", ""},
    {"glaze::DockHost",   "layout",    FieldKind::Text, "", ""},
    // FLIKARNA. Sidans `text` är FLIKENS rubrik; sidans clip/border
    // är Panels rader. Själva sidorna är barn (trädbyggets `adopt`) och har
    // ingen panelrad.
    {"glaze::TabControl", "selectedIndex", FieldKind::Integer, "", ""},
    {"glaze::TabControl", "closable",  FieldKind::Bool, "true;false", ""},
    {"glaze::TabPage",    "text",      FieldKind::Text, "", ""},
    // FILVÄLJARFÄLTET. Sökvägen är en sträng (Text), läget ett enum
    // med dialogens prefix. Filtren är ANROP (kollektionen) och har ingen
    // panelrad.
    {"glaze::FilePicker", "path",      FieldKind::Text, "", ""},
    {"glaze::FilePicker", "mode",      FieldKind::Enum,
     "Open;Save;Folder", "glaze::FileDialogMode::"},
    // DIALOGENS TITEL — tom = lägets.
    {"glaze::FilePicker", "dialogTitle", FieldKind::Text, "", ""},
    // FÄRGVÄLJAREN. Färgen är HEXTEXTEN — en vanlig textrad
    // (`#3366FF`), och raden skrivs tillbaka som strängliteral. En ogiltig
    // text bevaras som skriven och loggas av widgeten i previewn; den
    // kompilerar ändå (setColor tar en sträng).
    {"glaze::ColorPicker", "color",    FieldKind::Text, "", ""},
    // ALFAN och KLOCKSLAGET — två Bool-rader.
    {"glaze::ColorPicker", "allowAlpha", FieldKind::Bool, "true;false", ""},
    {"glaze::DatePicker",  "showTime",   FieldKind::Bool, "true;false", ""},
    // DET FLERRADIGA FÄLTET. Texten är Text som det enradigas —
    // en radbrytning skrivs tillbaka som `\n` i literalen (`cppString`) —
    // och skyddet och brytningen är bools. Panelens textrad är EN rad, så
    // en text med radbrytningar redigeras bekvämast i koden; det hör till
    // editorns yta (fas 2) och inte till formen.
    {"glaze::TextArea",   "text",      FieldKind::Text, "", ""},
    {"glaze::TextArea",   "readOnly",  FieldKind::Bool, "true;false", ""},
    {"glaze::TextArea",   "wrap",      FieldKind::Bool, "true;false", ""},
    // …och det enradigas hint och gräns, samma rader.
    {"glaze::TextArea",   "placeholder", FieldKind::Text, "", ""},
    {"glaze::TextArea",   "maxLength", FieldKind::Integer, "", ""},
    // TAB SKRIVER EN TABB.
    {"glaze::TextArea",   "acceptsTab", FieldKind::Bool, "true;false", ""},
    // TEXTSTORLEKEN — ett MÅTT (`glaze::mm(3)`, `glaze::px(18)`,
    // ett naket tal) på de fyra som har en: Raw, och `commitPropEdit` grindar
    // raden med previewns egen tolkning (`bld::parseLength`), så filen aldrig
    // bär ett mått som inte kompilerar.
    {"glaze::TextInput",  "textSize",  FieldKind::Raw, "", ""},
    {"glaze::TextArea",   "textSize",  FieldKind::Raw, "", ""},
    {"glaze::ListBox",    "textSize",  FieldKind::Raw, "", ""},
    {"glaze::Dropdown",   "textSize",  FieldKind::Raw, "", ""},
    // MARKDOWN-VYN — dokumentet är Text som det
    // flerradiga fältets, och av samma skäl redigeras ett dokument med
    // radbrytningar bekvämast i koden (editorns yta, fas 2). Bilderna och
    // ankaret är Call-satser utan panelrad.
    {"glaze::MarkdownView", "text",    FieldKind::Text, "", ""},
    // DEN FLYTTBARA PANELEN — rubriken och stängknappen, som
    // dockningspanelens rader.
    {"glaze::FloatingPanel", "text",     FieldKind::Text, "", ""},
    {"glaze::FloatingPanel", "closable", FieldKind::Bool, "true;false", ""},
    // TERMINALEN — programmet som tre texter, läget och
    // schemat som enum, historiken som heltal; textstorleken är ett MÅTT
    // (Raw, som fältens). Miljön och de egna färgerna är Call-satser utan
    // panelrad (en collection editor är fas 2).
    {"glaze::Terminal",   "program",          FieldKind::Text, "", ""},
    {"glaze::Terminal",   "arguments",        FieldKind::Text, "", ""},
    {"glaze::Terminal",   "workingDirectory", FieldKind::Text, "", ""},
    {"glaze::Terminal",   "colorMode",        FieldKind::Enum,
     "Auto;Light;Dark", "glaze::TerminalColorMode::"},
    {"glaze::Terminal",   "colorScheme",      FieldKind::Enum,
     "Theme;Solarized;One;Tango", "glaze::TerminalScheme::"},
    {"glaze::Terminal",   "scrollback",       FieldKind::Integer, "", ""},
    {"glaze::Terminal",   "altAsMeta",        FieldKind::Bool, "true;false", ""},
    {"glaze::Terminal",   "textSize",         FieldKind::Raw, "", ""},
    // RITYTAN — setter-fälten med ett värde: rutnätet
    // och kamerans tal är tal, kanten, y-riktningen, gesterna och
    // etiketternas toning bools. Färgen, de tvåargumentiga, kameran, lagren
    // och formlistan är Call-satser utan panelrad (en collection editor är
    // fas 2).
    {"glaze::Canvas",     "gridSize",  FieldKind::Number, "", ""},
    {"glaze::Canvas",     "border",    FieldKind::Bool, "true;false", ""},
    // PLATT YTA: ingen kant, raka hörn, fokus innanför.
    {"glaze::Canvas",     "flat",      FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "yUp",       FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "zoom",      FieldKind::Number, "", ""},
    {"glaze::Canvas",     "bearing",   FieldKind::Number, "", ""},
    {"glaze::Canvas",     "pitch",     FieldKind::Number, "", ""},
    {"glaze::Canvas",     "maxPitch",  FieldKind::Number, "", ""},
    {"glaze::Canvas",     "pannable",  FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "zoomable",  FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "rotatable", FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "pitchable", FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "inertia",   FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "labelFade", FieldKind::Bool, "true;false", ""},
    // Vridsnäppet: steget (0 = av, 360 = bara norr, 90 …) och
    // toleransen, grader.
    {"glaze::Canvas",     "rotationSnap",          FieldKind::Number, "", ""},
    {"glaze::Canvas",     "rotationSnapTolerance", FieldKind::Number, "", ""},
    // I RYMDEN: provtagningen är ett HELTAL (1, 2, 4
    // eller 8 — ritytan loggar och behåller sitt för annat), djupet och
    // orbitens tumling bools. 3D-kameran, ljuset, del-vyerna och lagrens
    // renderlägen är Call-satser utan panelrad (en collection editor är fas 2).
    {"glaze::Canvas",     "multisample", FieldKind::Integer, "", ""},
    {"glaze::Canvas",     "depth",       FieldKind::Bool, "true;false", ""},
    {"glaze::Canvas",     "orbitTumble", FieldKind::Bool, "true;false", ""},
    // Bindningarnas läge: tomt = automatiskt, annars "map",
    // "orbit", "firstPerson" eller appens eget. Raderna är Call-satser.
    {"glaze::Canvas",     "inputMode",   FieldKind::Text, "", ""},
    // MINNET: Number och inte Integer — Integer går genom `int` och
    // spricker över 2 GiB. Panelen räknar i builderns flyttal: multipler av
    // 1 MiB är exakta upp till 2⁴⁴, ett godtyckligt bytetal kan avrundas vid
    // redigering i panelen (fas 2); texten i filen round-trippar ordagrant.
    {"glaze::Canvas",     "memoryBudget", FieldKind::Number, "", ""},
    // PROGRAMIKONEN. Bilderna är Raw: värdet är
    // `glaze::img("lista", "nyckel")` ELLER `{}` (förvalet), och `{}` har
    // ingen plats i `img`-radens "lista/nyckel"-val — Raw-grenen i
    // `commitPropEdit` är grinden mot text som inte kompilerar. Den stora
    // formen och "ingen ikon alls" är bools. Programmenyn är Call-satser och
    // har ingen panelrad (en collection editor är fas 2).
    {"glaze::AppIcon",    "icon",        FieldKind::Raw, "", ""},
    {"glaze::AppIcon",    "smallIcon",   FieldKind::Raw, "", ""},
    {"glaze::AppIcon",    "large",       FieldKind::Bool, "true;false", ""},
    {"glaze::AppIcon",    "noIcon",      FieldKind::Bool, "true;false", ""},
};

const BuilderWindow::FieldMeta*
BuilderWindow::fieldMeta(const std::string& cppType,
                         const std::string& field)
{
    const FieldMeta* generell = nullptr;
    for (const auto& m : kFieldMeta) {
        if (field != m.field)
            continue;
        if (m.cppType && cppType == m.cppType)
            return &m;                    // mest specifika vinner
        if (!m.cppType)
            generell = &m;
    }
    return generell;
}

BuilderWindow::BuilderWindow(std::string hPath)
    : glaze::AppWindow(
          "GlazeBuilder — " + (hPath.empty() ? "nytt fönster" : hPath),
          1280, 800),
      hPath_(std::move(hPath))
{
    // Association: buildern kan öppnas med valfri triplett-fil
    // (.h/.cpp/.glazeui) — normalisera till .h-sökvägen som resten
    // av fil-flödet utgår från.
    if (!hPath_.empty())
        hPath_ = bld::headerPathFor(hPath_);

    auto* rubrik = add<glaze::Label>("Verktygslåda");
    rubrik->x = 12;
    rubrik->y = 12;

    float y = 44.0f;
    for (std::size_t i = 0; i < bld::kPaletteCount; ++i) {
        const bld::PaletteEntry& t = bld::kPalette[i];
        auto* b   = add<glaze::Button>(t.namn);
        b->width  = kToolbox - 24.0f;
        b->height = 30.0f;
        b->x      = 12;
        b->y      = y;
        const std::string type = t.cppType;
        b->onClick = [this, type](glaze::Widget*, glaze::ClickArgs&) {
            if (failSafe_)
                return;
            pendingType_  = type;
            status_->setText("Placera " + type + ": klicka på canvasen");
        };
        y += 38.0f;
    }

    // KOPPLINGSLÄGET. Botten-ankrad som de två
    // nedan, så verktygslådans palettlista får växa fritt ovanför.
    linkBtn_          = add<glaze::Button>("Koppla: av");
    linkBtn_->width   = kToolbox - 24.0f;
    linkBtn_->height  = 30.0f;
    linkBtn_->anchors = {true, false, false, true};
    linkBtn_->x       = 12;
    linkBtn_->bottom  = 88;
    linkBtn_->onClick = [this] { toggleLinkMode(); };

    auto* assets    = add<glaze::Button>("Assets…");
    assets->width   = kToolbox - 24.0f;
    assets->height  = 30.0f;
    assets->anchors = {true, false, false, true};
    assets->x       = 12;
    assets->bottom  = 50;
    assets->onClick = [this](glaze::Widget*, glaze::ClickArgs&) {
        if (!assetWin_)
            assetWin_ = std::make_unique<AssetWindow>(assetDoc_, [this] {
                // Dokumentet ändrat: img-choices + preview följer. Storen
                // läser FILEN — ikonbyten syns efter Spara i asset-läget.
                if (assetDoc_.loaded)
                    glaze::loadAssets(assetDoc_.path);
                refreshPropPanel();
                rebuildPreview();
            });
        assetWin_->show(*this);   // ägt — kaskad-stängs med buildern
    };

    auto* spara    = add<glaze::Button>("Spara");
    spara->width   = kToolbox - 24.0f;
    spara->height  = 30.0f;
    spara->anchors = {true, false, false, true};
    spara->x       = 12;
    spara->bottom  = 12;
    spara->onClick = [this](glaze::Widget*, glaze::ClickArgs&) { save(); };

    auto* props    = add<glaze::Label>("Egenskaper");
    props->anchors = {false, true, true, false};
    props->right   = kProps - 24.0f - 70.0f;
    props->y       = 12;

    // Egenskapsgridden (ramverkets PropertyGrid — generisk: namn/värde/
    // choices utifrån, känner inte builderns typer). Rader sätts vid
    // markering (refreshPropPanel); redigering committar live via
    // onChange → commitPropEdit → modell + preview.
    propGrid_          = add<glaze::PropertyGrid>();
    propGrid_->anchors = {false, true, true, true};
    propGrid_->right   = 12;
    propGrid_->y       = 44;
    propGrid_->width   = kProps - 24.0f;
    propGrid_->bottom  = 12;
    propGrid_->setBorder(true);
    propGrid_->onChange = [this](glaze::Widget*, glaze::ChangeArgs&) {
        commitPropEdit();
    };

    status_ = add<glaze::Label>(
        "Klicka: markera — dra: flytta — dubbelklick: koppla onClick — "
        "Delete: ta bort — Ctrl+S: spara");
    status_->anchors = {true, false, false, true};
    status_->x       = kToolbox + kMargin;
    status_->bottom  = 8.0f;
    status_->setRole(glaze::Label::Role::Dim);

    if (hPath_.empty())
        newWindowModel();
    else
        openFile();
    rebuildPreview();
    refreshPropPanel();
    if (failSafe_ && propGrid_)
        propGrid_->hide();   // fail-safe: visa koden, redigera inget

    // Den redigerade appens assets.bin — letas uppåt från
    // .h-filens katalog. Ingen fil = tom bildväljare, inte ett fel.
    const std::string dir =
        hPath_.empty() ? "."
                       : hPath_.substr(0, hPath_.find_last_of("/\\") + 1);
    assetDoc_.load(dir.empty() ? "." : dir);

    // Ladda appens assets i RUNTIME-storen så previewn ritar riktiga
    // ikoner (glaze::img i applyProp slår upp där). Ofarligt i dag:
    // buildern har inga egna assets att krocka med — när den får det
    // behövs skilda stores.
    if (assetDoc_.loaded)
        glaze::loadAssets(assetDoc_.path);

    // …och den redigerade appens language.bin, letad
    // uppåt på EXAKT samma sätt. Utan den ritar previewn platshållaren
    // ⟦1042⟧ där `.glazeui` säger `setText(glaze::tr(1042))`, vilket är
    // sant men oanvändbart. Ingen fil = ramverkets egna 1–999 fungerar
    // ändå (de är inbäddade i libglaze).
    {
        std::string ldir = dir.empty() ? std::string(".") : dir;
        for (int i = 0; i < 4; ++i) {
            const std::string kandidat = ldir + "language.bin";
            if (std::ifstream(kandidat, std::ios::binary)) {
                glaze::loadLanguage(kandidat);
                break;
            }
            ldir += "../";
        }
    }

    // Builderns utseende: SLATE med Teal. Ritningen är
    // app-global, så valet gäller även asset-fönstret och previewn — och
    // just därför sätts det HÄR och inte per fönster. Slate är verktygets
    // ton: den är basen allt annat jämförs mot, så previewn av en användares
    // fönster mäts mot referensformen och inte mot husets egen.
    //
    // I onLoad och inte i konstruktorn (AppWindow-kontraktet: startlogik hör
    // till livscykeln). ORDNINGEN är bärande — `setUI` återställer tema och
    // mått till familjens defaults, så temat måste sättas EFTER.
    //
    // DESIGNTAGGEN kommer SEDAN och skriver över
    // det som taggen anger — och just därför måste den ligga här och inte i
    // openFile(): openFile() körs i KONSTRUKTORN, onLoad senare, så
    // builderns Slate/Teal hade annars skrivit över taggen. Utseendet är
    // app-globalt, så hela verktyget byter skepnad efter det fönster som
    // redigeras (uttryckligen accepterat pris) — och previewn följer med
    // utan att en enda rad i ritvägen ändras, eftersom canvasCtx() hämtar
    // sin Ctx ur makeCtx().
    onLoad = [this](glaze::Widget*, glaze::EventArgs&) {
        setUI(glaze::UI::Slate);
        setTheme(glaze::Theme::slate(glaze::Hue::Teal));

        bld::applyDesignTag(*this, bld::decodeDesignLine(model_.designLine));

        // Statusraden SIST: konstruktorns egna meddelanden ("Öppnade …",
        // fail-safe-diagnosen) sätts före onLoad och skulle annars skriva
        // över den här. Fail-safe-läget behåller sin diagnos.
        if (!failSafe_ && status_)
            status_->setText(status_->text() + " · " +
                             bld::describeDesign(*this, model_.designLine));
    };
}

// ------------------------------------------------------------- fil-flödet

void BuilderWindow::openFile()
{
    bld::ReadResult r = bld::readWindow(hPath_);
    if (r.ok) {
        // NAMN-INVARIANTEN valideras och migreras HÄR,
        // innan modellen tas i bruk. En AVVIKANDE `name`-rad är ett
        // strukturellt identitetsbrott — inte ett okänt fält — och
        // behandlas därför som ett fail-safe-fall: buildern vägrar
        // redigera och visar koden, precis som vid skadat stängsel.
        //
        // MIGRERINGEN är en MEDVETEN engångsdiff på orörda filer
        // (tabIndex-prejudikatet gäller INTE här — se `migrateNames`).
        std::string namnFel;
        if (!bld::migrateNames(r.model, namnFel)) {
            enterFailSafe("namn-invarianten bruten i " + hPath_ + ": " +
                              namnFel,
                          false);
            return;
        }
        model_       = std::move(r.model);
        hNl_         = r.hNewline;
        cppNl_       = r.cppNewline;
        guiPath_     = bld::guiPathFor(hPath_);
        userCppPath_ = bld::userCppPathFor(hPath_);
        status_->setText("Öppnade " + hPath_ + " (" +
                         std::to_string(model_.nodes.size()) + " widgets)");
        return;
    }
    if (!r.hFound) {
        // Ny fil: tripletten skapas från mall vid första sparningen.
        newWindowModel();
        return;
    }
    enterFailSafe(r.error, false);
}

void BuilderWindow::newWindowModel()
{
    model_           = {};
    model_.className = hPath_.empty() ? "Window1"
                                      : classNameFromPath(hPath_);
    // Titel och storlek är SATSER och inte modellfält —
    // buildern initierar dem här, satslistan är sanningen.
    // INVARIANT: argumentet är en literal i den här filen och alltid
    // kodningsbart — grenen kan inte tas.
    std::string titelLit;
    if (bld::cppString("Nytt fönster", titelLit))
        model_.setWindowCall("setTitle", titelLit);
    model_.setWindowCall("setClientSize", "800, 520");
    if (hPath_.empty())
        hPath_ = "window1.h";
    guiPath_     = bld::guiPathFor(hPath_);
    userCppPath_ = bld::userCppPathFor(hPath_);
    newFile_     = true;
    status_->setText("Nytt fönster — Ctrl+S skapar " + hPath_ + " + syskon");
}

void BuilderWindow::enterFailSafe(const std::string& why, bool showGenerated)
{
    failSafe_ = true;
    failText_.clear();
    failText_.push_back("FAIL-SAFE — buildern vägrar redigera:");
    failText_.push_back(why);
    failText_.push_back("");
    if (showGenerated) {
        // Nödutgången (heliga regeln): visa koden som skulle ha skrivits
        // så användaren kan klistra in den själv. Inget röres på disk.
        failText_.push_back("Koden som skulle ha sparats — klistra in för "
                            "hand om du vill behålla ändringarna:");
        failText_.push_back("---- " + baseName(hPath_) +
                            " (GlazeBuilder-regionen) ----");
        std::string acc;
        for (char c : bld::emitRegion(model_, "\n")) {
            if (c == '\n') {
                failText_.push_back(acc);
                acc.clear();
            } else {
                acc += c;
            }
        }
        failText_.push_back("---- " + baseName(guiPath_) + " ----");
        for (char c : bld::emitGuiCpp(model_, baseName(hPath_), "\n")) {
            if (c == '\n') {
                failText_.push_back(acc);
                acc.clear();
            } else {
                acc += c;
            }
        }
    } else {
        failText_.push_back("Rätta filen för hand och öppna igen. Filen är "
                            "riktig C++ och kompilerar som den är.");
    }
    status_->setText("FAIL-SAFE: " + why);
    invalidate();
}

void BuilderWindow::save()
{
    if (failSafe_) {
        status_->setText("FAIL-SAFE-läge: sparning avstängd (rätta filen "
                         "och öppna om)");
        return;
    }

    std::string err;
    if (newFile_) {
        // "Nytt fönster"-mallen: tre filer skapas — aldrig över befintliga.
        if (!bld::createFromTemplate(hPath_,
                                     bld::emitHeaderTemplate(model_, hNl_),
                                     err) ||
            !bld::createFromTemplate(
                guiPath_,
                bld::emitGuiCpp(model_, baseName(hPath_), cppNl_), err) ||
            !bld::createFromTemplate(
                userCppPath_,
                bld::emitUserCppTemplate(model_, baseName(hPath_), cppNl_),
                err)) {
            status_->setText("FEL: " + err);
            return;
        }
        newFile_ = false;
        status_->setText("Skapade " + hPath_ + " + syskon — lägg till "
                         "filerna i CMakeLists.txt (buildern rör den "
                         "aldrig)");
        return;
    }

    // Splasa .h-regionen; skriv om hela *.glazeui (100 % genererad).
    // Stängslet letas upp i filens AKTUELLA bytes — har det skadats sedan
    // öppningen vägrar vi och visar koden (fail-safe, aldrig gissning).
    if (!bld::rewriteRegion(hPath_, bld::emitRegion(model_, hNl_), err)) {
        enterFailSafe(err, true);
        return;
    }
    if (!bld::rewriteGeneratedFile(
            guiPath_, bld::emitGuiCpp(model_, baseName(hPath_), cppNl_),
            err)) {
        status_->setText("FEL: " + err);
        return;
    }

    // Nya handlers: deklaration i användarområdet + stub i .cpp —
    // APPEND-ONLY, med substring-vakt mot dubbletter (ingen parsning av
    // användarkod).
    int appended = 0;
    for (const auto& e : pendingHandlers_) {
        const std::string declNeedle = "void " + e.handler + "(";
        const std::string stubNeedle =
            model_.className + "::" + e.handler + "(";
        if (!bld::fileContains(hPath_, declNeedle)) {
            const bool first =
                !bld::fileContains(hPath_, "Handler-deklarationer "
                                           "appendade av GlazeBuilder");
            if (!bld::appendToUserArea(
                    hPath_, bld::emitHandlerDecl(e, hNl_, first), err)) {
                status_->setText("FEL: " + err);
                return;
            }
        }
        if (!bld::fileContains(userCppPath_, stubNeedle)) {
            if (!bld::appendToCppEnd(
                    userCppPath_,
                    bld::emitHandlerStub(model_.className, e, cppNl_),
                    err)) {
                status_->setText("FEL: " + err);
                return;
            }
            ++appended;
        }
    }
    pendingHandlers_.clear();

    status_->setText("Sparade " + hPath_ + " + " + baseName(guiPath_) +
                     (appended ? " (+" + std::to_string(appended) +
                                     " handler-stubbar)"
                               : ""));
}

// ---------------------------------------------------------------- preview

// Egenskaper → widget bor i `bld::applyProp` (builder_preview), delad med glaze-render.

// --------------------------------------------------- egenskapspanelen

// Extrahera enum-värdets sista segment UTAN gemening (displayform som
// matchar choices-listorna: "None", "Left" …) — parseEnumTail gemenar
// (setProp-formen) och duger inte här.
static std::string enumDisplay(const std::string& v)
{
    const size_t p = v.rfind("::");
    return p == std::string::npos ? v : v.substr(p + 2);
}

// Nodens plats i MODELLENS djupet-först-ordning: adopterade rötter i
// nodordning, in i varje container i dess barnordning — samma vandring
// som runtime gör över widget-trädet. Icke-adopterade noder står utanför
// trädet och får -1.
//
// Det här är INTE runtimes auto-nummer och får inte påstås vara det:
// runtime numrerar bara de FOKUSERBARA, och listan innehåller dessutom
// widgets som skapas inuti andra widgets (PropertyGrids editorer) och som
// buildern aldrig ser. Talet duger för att VÄLJA en ordning, inte som
// absolut siffra — därför visas det som "auto (n)".
static int modelDfsIndex(const bld::WindowModel& m, int nodeIdx)
{
    std::vector<int> order;
    // Rekursiv vandring utan std::function: en enkel stack över id:n.
    struct Walk {
        const bld::WindowModel& m;
        std::vector<int>&       out;
        void operator()(const std::string& parentId) const
        {
            for (std::size_t i = 0; i < m.nodes.size(); ++i) {
                if (!m.isAdopted(m.nodes[i].id) ||
                    m.parentOf(m.nodes[i].id) != parentId)
                    continue;
                out.push_back(static_cast<int>(i));
                (*this)(m.nodes[i].id);
            }
        }
    };
    Walk{m, order}("");
    for (std::size_t i = 0; i < order.size(); ++i)
        if (order[i] == nodeIdx)
            return static_cast<int>(i);
    return -1;
}

void BuilderWindow::refreshPropPanel()
{
    if (!propGrid_)
        return;
    std::vector<glaze::PropertyGrid::Row> rows;
    if (selected_ >= 0 &&
        selected_ < static_cast<int>(model_.nodes.size())) {
        const bld::WidgetNode& n = model_.nodes[std::size_t(selected_)];
        // NAMNET ÄR REDIGERBART (etapp C e). Det är objektets namn:
        // ändras det byts BÅDE medlemsdeklarationen och allt som pekar på
        // den (satsernas target, adoptionsföräldrar, syskonankarnas `to`
        // och den kanoniska name-radens literalvärde) — `bld::renameNode`
        // gör alla fyra i ETT svep. Taggen `$name` kan inte kollidera med
        // ett fältnamn (`validFieldName` släpper aldrig igenom `$`), och
        // `commitPropEdit` fångar den FÖRE `kFieldMeta`-uppslaget.
        //
        // INBYGGDA medlemmar kan inte döpas om — namnet ägs av
        // `glaze::AppWindow`.
        rows.push_back({"name", n.id, {}, n.builtin, n.builtin ? "" : "$name"});
        rows.push_back({"typ", n.type, {}, true, ""});
        if (n.standIn)
            rows.push_back({"(okänd typ)", "placering redigerbar", {},
                            true, ""});
        const std::string parentId = model_.parentOf(n.id);
        if (!parentId.empty())
            rows.push_back({"i", parentId, {}, true, ""});

        // Fältraderna i SATSORDNING (= källordning). Satslistan är den
        // enda sanningen om kroppen; noden bär bara typ och id.
        for (const bld::Stmt& p : model_.stmts) {
            if (p.kind != bld::Stmt::Kind::Prop || p.target != n.id)
                continue;
            glaze::PropertyGrid::Row r;
            r.name = p.name;
            r.tag  = p.name;                // opak nyckel → commit-vägen

            // Bildfältet: DYNAMISKA choices ur den redigerade
            // appens assets.bin — inte den statiska tabellen. Utan
            // laddade assets: läs-bar (inget att välja på).
            if (p.name == "img") {
                std::string li, ny;
                r.value = bld::parseImgCall(p.value, li, ny)
                              ? li + "/" + ny
                              : p.value;
                r.choices  = assetDoc_.allKeys();
                r.readOnly = r.choices.empty();
                rows.push_back(std::move(r));
                continue;
            }

            // ÖVERSÄTTNINGEN — en FAS 2-SÄKERHETSÄNDRING
            // och inte en funktion. Panelen visade värdet RÅTT, och varje
            // commit av ett `FieldKind::Text`-fält kodar innehållet som
            // C++-strängliteral: minsta redigering hade gjort id:t till
            // literalen `"glaze::tr(1042)"`. Det är DATAFÖRLUST, inte en
            // saknad funktion — raden slutar följa språket och går inte
            // att få tillbaka.
            //
            // Tills id-redigering finns (fas 2) visas den UPPLÖSTA texten
            // SKRIVSKYDDAD med id:t bredvid. KLASSIFICERINGEN kommer ur
            // `builder_core` (`bld::classifyTextValue`) och grindas
            // headless i `roundtrip_gate` — den hör till GRAMMATIKEN, inte
            // till editorn; exet konsumerar bara svaret.
            {
                std::string         lit;
                std::uint32_t       trId = 0;
                const bld::TextValueKind k =
                    bld::classifyTextValue(p.value, lit, trId);
                if (k == bld::TextValueKind::TrId) {
                    const glaze::TrText t = glaze::tr(trId);
                    r.value    = t.str() + "   [tr " + std::to_string(trId) +
                              "]";
                    r.readOnly = true;
                    rows.push_back(std::move(r));
                    continue;
                }
                // Ett tr-anrop med en KONSTANT har samma dataförlust:
                // en redigering hade kodat uttrycket som literal. Rått och
                // skrivskyddat — previewn ritar det inte (opak rad).
                if (k == bld::TextValueKind::TrExpr) {
                    r.value    = p.value;
                    r.readOnly = true;
                    rows.push_back(std::move(r));
                    continue;
                }
            }

            const FieldMeta* m = fieldMeta(n.type, p.name);
            if (!m) {
                // Okänt fält: VISAS men redigeras inte — att skriva rå
                // C++-uttryckstext via gridden kunde skada .glazeui
                // (regel 4: gissa aldrig).
                r.value    = p.value;
                r.readOnly = true;
            } else {
                switch (m->kind) {
                case FieldKind::Text: {
                    std::string v;
                    r.value = bld::parseCppString(p.value, v) ? v : p.value;
                    break;
                }
                case FieldKind::Enum:
                    r.value = enumDisplay(p.value);
                    break;
                case FieldKind::Number:
                case FieldKind::Integer:
                case FieldKind::Bool:
                case FieldKind::Raw:
                    r.value = p.value;
                    break;
                }
                if (m->choices[0] != '\0') {
                    std::string      cs = m->choices;
                    std::size_t      s  = 0;
                    while (s <= cs.size()) {
                        const auto sep = cs.find(';', s);
                        r.choices.push_back(cs.substr(
                            s, sep == std::string::npos ? sep : sep - s));
                        if (sep == std::string::npos)
                            break;
                        s = sep + 1;
                    }
                }
            }
            rows.push_back(std::move(r));
        }

        // Tab-ordningen syns ALLTID, även för en nod som saknar propen —
        // annars går den inte att sätta. Raden är en VY: modellen får en
        // tabIndex-rad först när användaren skriver ett värde (regel 4/5
        // — buildern får aldrig börja emittera rader för widgets ingen
        // rört, då hade varje spara diffat).
        if (!model_.findProp(n.id, "tabIndex")) {
            const int pos = modelDfsIndex(model_, selected_);
            glaze::PropertyGrid::Row r;
            r.name  = "tabIndex";
            r.tag   = "tabIndex";
            r.value = pos >= 0 ? "auto (" + std::to_string(pos) + ")"
                               : "auto";
            rows.push_back(std::move(r));
        }

        for (const bld::Stmt& ev : model_.stmts)
            if (ev.kind == bld::Stmt::Kind::Event && ev.target == n.id)
                rows.push_back({ev.name, "→ " + ev.handler, {}, true, ""});
    }
    propGrid_->setRows(std::move(rows));
}

void BuilderWindow::commitPropEdit()
{
    if (failSafe_ || !propGrid_ || propGrid_->changedRow() < 0 ||
        selected_ < 0 || selected_ >= static_cast<int>(model_.nodes.size()))
        return;
    const glaze::PropertyGrid::Row& row =
        propGrid_->rows()[std::size_t(propGrid_->changedRow())];
    if (row.tag.empty())
        return;                          // läs-bar rad (typ/förälder/event)

    bld::WidgetNode& n = model_.nodes[std::size_t(selected_)];

    // ÖVERSÄTTNINGSSPÄRREN. Panelen visar tr-rader
    // SKRIVSKYDDADE, så den här grenen ska aldrig nås — men den är en
    // ANDRA spärr med flit: raden nedan skulle koda id:t som en
    // strängliteral och FÖRSTÖRA det, och en spärr som bara finns i
    // presentationen försvinner den dag någon lägger till en väg in.
    // Skyddet av användarens data får inte vila på en `readOnly`-flagga.
    //
    // Spärren prövar EXAKT DEN SATS SOM SKRIVNINGEN SKULLE TRÄFFA —
    // `WindowModel::setProp` uppdaterar FÖRSTA matchande satsen
    // (`findProp`, window_model.cpp:663), alltså slutar loopen vid den
    // första oavsett sort. Att i stället scanna vidare efter NÅGON tr-sats
    // bland dubbletterna blockerade fel rad: `setText(glaze::tr(1000));`
    // följt av `setText("literal");` gav två panelrader, och literalens —
    // som är den `setProp` skriver — låstes av den andra satsen.
    {
        std::string   lit;
        std::uint32_t trId = 0;
        for (const bld::Stmt& s : model_.stmts)
            if (s.kind == bld::Stmt::Kind::Prop && s.target == n.id &&
                s.name == row.tag) {
                const bld::TextValueKind k =
                    bld::classifyTextValue(s.value, lit, trId);
                if (k == bld::TextValueKind::TrExpr) {
                    // 🐛 58: samma spärr för ett tr-anrop med en konstant.
                    status_->setText("'" + row.tag + "' är en översättning "
                                     "med en konstant — ändra den i koden "
                                     "(eller skriv numret, glaze::tr(1042))");
                    refreshPropPanel();
                    return;
                }
                if (k != bld::TextValueKind::TrId)
                    break;   // första matchningen är INGEN översättning
                status_->setText("'" + row.tag +
                                 "' är en översättning (glaze::tr(" +
                                 std::to_string(trId) +
                                 ")) — id-redigering finns inte än, ändra "
                                 "texten i språkfilen");
                refreshPropPanel();
                return;
            }
    }

    // OMDÖPNING (etapp C e) — FÖRE `kFieldMeta`-uppslaget, för `$name` är
    // ingen egenskap utan en operation på hela modellen.
    if (row.tag == "$name") {
        const std::string från = n.id;
        const std::string till = row.value;
        std::string       fel;
        if (!bld::renameNode(model_, från, till, fel)) {
            status_->setText("kan inte döpa om: " + fel);
            refreshPropPanel();   // visa det gamla namnet igen
            return;
        }
        // VARNINGEN, inte en rättning (regel 5): buildern rör ALDRIG
        // användarens `.cpp`. Handler-BINDNINGEN retargetas av
        // `renameNode`, och handler-SYMBOLEN behöver aldrig byta namn —
        // det som kan gå sönder är DIREKTA referenser till det gamla
        // MEDLEMSNAMNET i användarkoden, och de blir ett kompileringsfel
        // användaren rättar. Samma sak händer i C# när man byter `Name`
        // utan att köra en refactor över projektet.
        int handlers = 0;
        for (const bld::Stmt& s : model_.stmts)
            if (s.kind == bld::Stmt::Kind::Event && s.target == till)
                ++handlers;
        rebuildPreview();
        refreshPropPanel();
        status_->setText(
            "Döpte om " + från + " → " + till +
            (handlers ? " · " + std::to_string(handlers) +
                            " handler(s) omkopplade — referenser till '" +
                            från + "' i .cpp måste rättas för hand"
                      : ""));
        return;
    }

    // Bildfältet: "lista/nyckel" → funktionsanrops-literalen.
    if (row.tag == "img") {
        const auto slash = row.value.find('/');
        if (slash == std::string::npos) {
            status_->setText("ogiltig bildreferens: " + row.value);
            return;
        }
        const std::string imgLit =
            bld::imgCallText(row.value.substr(0, slash),
                             row.value.substr(slash + 1));
        if (imgLit.empty()) {   // SKAPANDE av ny rad ⇒ avbryt helt
            status_->setText("bildreferensen kan inte skrivas som "
                             "C++-literal: " + row.value);
            return;
        }
        model_.setProp(n.id, "img", imgLit);
        rebuildPreview();
        return;
    }

    const FieldMeta* m = fieldMeta(n.type, row.tag);
    if (!m)
        return;

    // Omkodning display → C++-text per kind. Ogiltiga värden skrivs
    // ALDRIG in i modellen (status-raden säger ifrån) — .glazeui får
    // aldrig bära text som inte kompilerar.
    std::string encoded;
    switch (m->kind) {
    case FieldKind::Text:
        // Kategori "redigering av befintligt värde": går texten inte
        // att koda i delmängden AVVISAS committen och den gamla
        // modellraden står kvar orörd. Att emittera rått hade skrivit en
        // rå kontrollbyte mitt i literalen — kod som inte kompilerar.
        if (!bld::cppString(row.value, encoded)) {
            status_->setText("texten kan inte skrivas som C++-literal "
                             "(kontrolltecken): " + row.tag);
            return;
        }
        break;
    case FieldKind::Number: {
        float f = 0.0f;
        if (!bld::parseNum(row.value, f)) {
            status_->setText("ogiltigt tal: " + row.value);
            return;
        }
        encoded = bld::numText(f);
        break;
    }
    case FieldKind::Integer: {
        // Heltalskrav: fältet är en int i runtime, så ett decimaltal
        // hade trunkerats tyst och gridden visat något annat än koden.
        float f = 0.0f;
        if (!bld::parseNum(row.value, f) || f != std::floor(f)) {
            status_->setText("heltal krävs: " + row.value);
            return;
        }
        encoded = std::to_string(static_cast<int>(f));
        break;
    }
    case FieldKind::Bool:
        if (row.value != "true" && row.value != "false") {
            status_->setText("ogiltig bool: " + row.value);
            return;
        }
        encoded = row.value;
        break;
    case FieldKind::Enum:
        encoded = std::string(m->enumPrefix) + row.value;
        break;
    case FieldKind::Raw: {
        bool a[4];
        if (row.tag == "anchors" && !bld::parseAnchorsText(row.value, a)) {
            status_->setText("ogiltiga anchors: " + row.value);
            return;
        }
        // `items` är en C++-LISTLITERAL och inget annat — samma grind som
        // anchors, och av samma skäl: .glazeui får aldrig bära text som
        // inte kompilerar.
        std::vector<std::string> lista;
        if (row.tag == "items" && !bld::parseStringList(row.value, lista)) {
            status_->setText("ogiltig items-lista: " + row.value);
            return;
        }
        // Trädets VAL — en indexstig, `{0, 2}`, och inget annat.
        std::vector<int> stig;
        if (row.tag == "selectedPath" && !bld::parseIndexPath(row.value, stig)) {
            status_->setText("ogiltig stig (förväntar {0, 2}): " + row.value);
            return;
        }
        // TEXTSTORLEKEN — ett mått och inget annat, med previewns
        // egen tolkning.
        glaze::Length mått;
        if (row.tag == "textSize" && !bld::parseLength(row.value, mått)) {
            status_->setText("ogiltigt mått (förväntar glaze::mm(3), "
                             "glaze::px(18) eller ett tal): " + row.value);
            return;
        }
        // SYSKONANKARET: samma grind, tredje gången. Och en extra som de
        // andra två inte behöver: raden måste vara en giltig
        // structliteral, annars bär `.glazeui` text som inte kompilerar.
        // (Målets namn-rad behöver inte säkras — den är kanonisk och
        // står på varje nod.)
        if (bld::isSiblingAnchorField(row.tag)) {
            std::string till;
            float       gap = 0.0f;
            if (!bld::parseSiblingAnchorText(row.value, till, gap)) {
                status_->setText("ogiltigt syskonankare (förväntar "
                                 "{\"namn\", tal}): " + row.value);
                return;
            }
        }
        encoded = row.value;
        break;
    }
    }

    model_.setProp(n.id, row.tag, encoded);
    rebuildPreview();   // previewn följer direkt; raderna byggs INTE om
                        // (setRows skulle döda editorn mitt i skrivandet)
}

// Previewträdet är ägarlöst — `Widget::languageChanged` når det aldrig, så
// ett språkbyte bygger om det ur modellen (se headern).
void BuilderWindow::onLanguageChanged()
{
    rebuildPreview();
}

void BuilderWindow::rebuildPreview()
{
    previewByNode_.assign(model_.nodes.size(), nullptr);
    previewRoot_ = std::make_unique<glaze::Panel>();
    previewRoot_->setClip(false);

    // TVÅ PASS, samma delning som glaze-render:
    // TRÄDET byggs i NODORDNING — INBYGGDA fönstermedlemmar först, sedan
    // adopt-ordningen (= z-ordningen) — och SATSERNA körs sedan i
    // KÄLLORDNING. Ordningarna är skilda och får inte slås ihop.
    //
    // Att builtins ligger först i nodlistan är modellens ansvar
    // (cpp_reader): `statusBar` har ingen adopt-rad, och låg den
    // sist byggdes ett barn som `statusBar.adopt(chip)` pekar ut FÖRE sin
    // förälder. Previewn skapar en RIKTIG StatusBar ur registret, för
    // builderns previewROT är en `Panel` och inte det redigerade
    // `AppWindow`:t — och fönstret skapar raden DOLD, så previewn måste
    // börja likadant.
    for (size_t i = 0; i < model_.nodes.size(); ++i) {
        bld::WidgetNode& n = model_.nodes[i];
        const bool adopterad = model_.isAdopted(n.id);
        if (!n.builtin && !adopterad)
            continue;

        // REGISTRET avgör: medlemsdeklarationens klassnamn ÄR
        // upptäcktspunkten — ramverkets eller en registrerad egen widget i
        // ett bygge som länkar den; okänd klass = STAND-IN (fråga 11), ruta
        // i rätt storlek med klassnamnet inuti, placeringen redigerbar, allt
        // annat opakt. Samma väg som glaze-render (`bld::makeNodeWidget`).
        bool                           standIn = false;
        std::unique_ptr<glaze::Widget> w = bld::makeNodeWidget(n.type, &standIn);
        n.standIn = standIn;
        // NAMNET = MEDLEMSNAMNET (etapp C). För builtins är det exakt de
        // strängar `AppWindow`-konstruktorn sätter i en riktig app
        // ("menuBar"/"statusBar"/"appIcon"), så previewn och appen ankrar
        // mot samma namn — builderns rot är en `Panel` och inte ett
        // `AppWindow`, så den vägen måste sätta dem själv.
        //
        // DOLD för alla builtins — också `appIcon`, som är SYNLIG i en
        // riktig app: canvasen har ingen namnlist att sätta
        // den i, och en fristående ikon i canvasens hörn vore en bild av
        // något appen aldrig visar. Canvasens chrome är fas 2; glaze-render
        // bygger ett riktigt `AppWindow` och visar ikonen där den sitter.
        w->name = n.id;
        if (n.builtin)
            w->hide();

        glaze::Widget*    parent = previewRoot_.get();
        const std::string pid    = model_.parentOf(n.id);
        if (!pid.empty()) {
            bool hittad = false;
            for (size_t k = 0; k < model_.nodes.size(); ++k)
                if (model_.nodes[k].id == pid && previewByNode_[k]) {
                    parent = previewByNode_[k];
                    hittad = true;
                    break;
                }
            // TYSTNAD vore fel: utan raden hamnar barnet i roten
            // utan ett ord, och previewn ljuger med grönt bygge.
            if (!hittad)
                gfx::log("preview: '%s' skulle adopteras i '%s' som inte är "
                         "byggd — hamnar i roten",
                         n.id.c_str(), pid.c_str());
        }
        previewByNode_[i] = parent->adopt(std::move(w));
    }

    // Pass 2: fält och anrop i KÄLLORDNING.
    for (const bld::Stmt& s : model_.stmts) {
        if (s.kind != bld::Stmt::Kind::Prop && s.kind != bld::Stmt::Kind::Call)
            continue;
        for (size_t i = 0; i < model_.nodes.size(); ++i)
            if (model_.nodes[i].id == s.target && previewByNode_[i]) {
                bld::applyStmt(*previewByNode_[i], s, "preview");
                break;
            }
    }

    // De ICKE-VISUELLA medlemmarna — glaze-renders
    // väg: skapade ur komponentregistret och utförda med sina egna satser.
    // De gamla rivs först, så en timer eller ikon aldrig finns i två
    // exemplar med samma namn.
    previewComponents_.clear();
    previewComponents_ = bld::buildComponents(model_, "preview");
}

// PREVIEWVANDRINGEN bor i `bld::drawPreviewTree`
// (preview_tree.h, libbet `builder_preview`): EN vandring som `preview_gate`
// mäter, i stället för en privat kopia av `AppWindow::drawTree` som ingen
// grind kan nå.

glaze::Widget* BuilderWindow::hitTestTree(glaze::Widget& w,
                                          const glaze::Ctx& c, float mx,
                                          float my)
{
    if (!w.isVisible())
        return nullptr;
    if (!w.children().empty() &&
        (!w.clipsChildren() || w.clipRect(c).contains(mx, my))) {
        glaze::Ctx cc = c;
        cc.area = w.childArea(c);
        const auto& kids = w.children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            if (glaze::Widget* hit = hitTestTree(**it, cc, mx, my))
                return hit;
    }
    return w.resolve(c).contains(mx, my) ? &w : nullptr;
}

// Träffen kan vara en KOMPOSITS INREDNING: talfältets
// inre fält är ett riktigt barn i previewträdet men ingen nod i modellen —
// det skapas av `SpinBox`-konstruktorn. Utan uppvandringen gick ett talfält
// bara att markera genom att klicka på stegaren. Närmaste MODELLERADE
// förfader är den nod användaren menar.
int BuilderWindow::nodeIndexFor(glaze::Widget* w) const
{
    for (; w; w = w->parent())
        for (size_t i = 0; i < previewByNode_.size(); ++i)
            if (previewByNode_[i] == w)
                return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------- geometri

gfx::Rectf BuilderWindow::canvasRect()
{
    const float      s    = window().uiScale();
    const gfx::Rectf area = window().contentArea();
    // Canvasen ÄR innehållsytan (previewROTen är en Panel utan chrome), så
    // `setContentSize` vinner när filen bär en — det är exakt det måttet
    // widgetarna placeras mot. Utan den raden är klientytan bästa svaret.
    // `setFixedContentSize` bär samma mått: den SENARE av de två
    // satserna gäller — pekarna in i satslistan står i källordning.
    int              cw = 0, ch = 0;
    const bld::Stmt* cs = model_.lastWindowCall("setContentSize");
    if (const bld::Stmt* fs = model_.lastWindowCall("setFixedContentSize");
        fs && (!cs || fs > cs))
        cs = fs;
    const bool harContent = cs && bld::parseTwoInts(cs->value, cw, ch);
    const float w = static_cast<float>(harContent ? cw : model_.clientWidth());
    const float h = static_cast<float>(harContent ? ch : model_.clientHeight());
    return gfx::Rectf{area.x + (kToolbox + kMargin) * s,
                      area.y + (kMargin + 22.0f) * s,   // plats för rubriken
                      w * s, h * s};
}

// ARBETSYTAN: innehållsytan MINUS de två panelerna. Handtagen får sticka
// ut utanför `canvasRect()` (annars går en widget som ligger an mot
// canvaskanten inte att greppa), men de får ALDRIG svälja ett klick som
// hör till verktygspanelen eller egenskapspanelen — de ritas efter
// canvasen och döljer handtaget, så ett grepp där vore osynligt.
gfx::Rectf BuilderWindow::workArea()
{
    const float      s    = window().uiScale();
    const gfx::Rectf area = window().contentArea();
    const float      tb   = kToolbox * s;
    const float      pr   = kProps * s;
    return gfx::Rectf{area.x + tb, area.y,
                      std::max(0.0f, area.w - tb - pr), area.h};
}

glaze::Ctx BuilderWindow::canvasCtx()
{
    // makeCtx resolvar tema + stil (bygg aldrig Ctx för hand) — canvasen
    // byter bara innehållsytan mot sin egen.
    glaze::Ctx c = makeCtx();
    c.area = canvasRect();
    return c;
}

// ---------------------------------------------------------------- modell

// "glaze::Button" → "Button". Sista `::`-segmentet, ingen omskrivning:
// en EGEN typ (`MinWidget`) ger `MinWidget1`, precis som en glaze-typ ger
// `Button1`.
std::string BuilderWindow::classBaseName(const std::string& cppType)
{
    const std::size_t p = cppType.rfind("::");
    const std::string s =
        (p == std::string::npos) ? cppType : cppType.substr(p + 2);
    return s.empty() ? std::string("Widget") : s;
}

// AUTO-NAMNET (etapp C b). Basen är KLASSNAMNETS form och inte registrets
// gemena typnamn: `name` ÄR objektets namn, och ett objekt heter `Button1`
// i WinForms/C#. Löpnumret är oförändrat — funktionen letade redan upp
// första lediga.
std::string BuilderWindow::genId(const std::string& base) const
{
    for (int i = 1;; ++i) {
        std::string id = base + std::to_string(i);
        bool taken = false;
        for (const auto& n : model_.nodes)
            if (n.id == id)
                taken = true;
        if (!taken)
            return id;
    }
}

void BuilderWindow::placeAt(float lx, float ly)
{
    const bld::PaletteEntry* pe = bld::paletteByCpp(pendingType_);
    if (!pe)
        return;

    // INVERSEN: muspunkten är logisk mot canvasens origo, men
    // `resolve()` placerar ett icke-dockat barn mot den SKURNA arean. Utan
    // förskjutningen hamnar en widget som släpps direkt under menyraden
    // en menyradshöjd för långt ner i det ögonblick previewn byggs om.
    // `layoutArea(-1, …)` frågar rotcanvasen UTAN att utesluta någon —
    // noden finns ju inte som widget än .
    const glaze::Ctx cc = canvasCtx();
    const gfx::Rectf pa = layoutArea(-1, cc);
    const float      s  = cc.scale > 0.0f ? cc.scale : 1.0f;
    const float      lxA = lx - (pa.x - cc.area.x) / s;
    const float      lyA = ly - (pa.y - cc.area.y) / s;

    const int x = (static_cast<int>(lxA) / kSnap) * kSnap;
    const int y = (static_cast<int>(lyA) / kSnap) * kSnap;

    bld::WidgetNode& n =
        model_.appendNode(pe->cppType, genId(classBaseName(pe->cppType)));
    const std::string id = n.id;
    model_.setAdoption(id, "");   // rot-nod (adopt-satsen bär trädet)

    // NAMNET EMITTERAS ALLTID (etapp C c): det är enda sättet identiteten
    // finns i den BYGGDA appen, och det är hela poängen — `find<>()` och
    // syskonankarna slår upp exakt den strängen. Raden är kanonisk, alltså
    // ordagrant nodnamnet, och `nameState` validerar det vid varje öppning.
    std::string namnLit;
    if (bld::cppString(id, namnLit))   // nodnamn = invariant
        model_.setProp(id, "name", namnLit);
    // DEFAULTTEXTEN bara där typen HAR en text. Settertabellen är sanningen
    // om vilka typer som har `setText`: en `text`-Prop på en typ utan den
    // (Slider, Separator, Bar, ToolBar …) emitteras som FÄLTRAD
    // (`Slider1.text = "Slider";`), alltså en rad som inte kompilerar.
    // TextInput står kvar som undantag med flit (ett skrivfält ska inte
    // födas med sitt eget typnamn i).
    std::string textLit;
    if (std::string(pe->cppType) != "glaze::TextInput" &&
        bld::isSetterField(pe->cppType, "text") &&
        bld::cppString(pe->namn, textLit))   // palettnamn = invariant
        model_.setProp(id, "text", textLit);
    // PER AXEL (axelregeln): skriv bara den axel paletten faktiskt
    // har ett tal för — en rad som SÄGER noll (t.ex. height = 0 på remsorna)
    // dödar autosizen.
    if (pe->defW > 0.0f)
        model_.setProp(id, "width", bld::numText(pe->defW));
    if (pe->defH > 0.0f)
        model_.setProp(id, "height", bld::numText(pe->defH));
    model_.setProp(id, "x", bld::numText(static_cast<float>(x < 0 ? 0 : x)));
    model_.setProp(id, "y", bld::numText(static_cast<float>(y < 0 ? 0 : y)));
    if (std::string(pe->cppType) == "glaze::Panel")
        model_.setProp(id, "border", "true");

    rebuildPreview();
    selected_     = static_cast<int>(model_.nodes.size()) - 1;
    refreshPropPanel();
    status_->setText("Skapade " + id);
}

// ------------------------------------------------------ EN koordinatväg
//
// `frame()`, hit-testen, flytten OCH storleksändringen räknar alla genom
// de här tre funktionerna. Det är hela poängen: så länge de delar väg KAN
// de inte glida isär (handtag som ritas av en formel och greppas av en
// annan hade glidit isär).

glaze::Ctx BuilderWindow::ctxForNode(int nodeIdx)
{
    glaze::Ctx c = canvasCtx();
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(model_.nodes.size()))
        return c;

    // Förälderkedjan UPPÅT ur modellen ...
    std::vector<int> chain;
    std::string      pid = model_.parentOf(model_.nodes[nodeIdx].id);
    while (!pid.empty()) {
        int pi = -1;
        for (size_t k = 0; k < model_.nodes.size(); ++k)
            if (model_.nodes[k].id == pid) {
                pi = static_cast<int>(k);
                break;
            }
        if (pi < 0)
            break;
        // Cykelvakt: en trasig/handskriven modell får aldrig snurra här.
        if (std::find(chain.begin(), chain.end(), pi) != chain.end())
            break;
        chain.push_back(pi);
        pid = model_.parentOf(model_.nodes[pi].id);
    }
    // ... och NEDÅT igen, ett childArea per led — exakt vandringen i
    // drawPreviewTree/hitTestTree. En icke-adopterad nod mitt i kedjan
    // finns inte i previewn; då stannar vi på den area vi nått.
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        glaze::Widget* p = previewByNode_[*it];
        if (!p)
            break;
        c.area = p->childArea(c);
    }
    return c;
}

// Se builder_window.h — EN rad kod, men den är hela
// skillnaden mellan att buildern skriver koordinater `resolve()` läser och
// koordinater den inte gör.
gfx::Rectf BuilderWindow::layoutArea(int nodeIdx, const glaze::Ctx& pc) const
{
    const glaze::Widget* w =
        (nodeIdx >= 0 && nodeIdx < static_cast<int>(previewByNode_.size()))
            ? previewByNode_[static_cast<std::size_t>(nodeIdx)]
            : nullptr;
    const glaze::Widget* p = w ? w->parent() : previewRoot_.get();
    if (!p)
        return pc.area;
    return glaze::Widget::carveSiblingDocks(*p, w, pc);
}

gfx::Rectf BuilderWindow::selectionRect(int nodeIdx)
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(previewByNode_.size()) ||
        !previewByNode_[nodeIdx])
        return gfx::Rectf{};
    gfx::Rectf r = previewByNode_[nodeIdx]->resolve(ctxForNode(nodeIdx));
    r.x -= 2.0f;
    r.y -= 2.0f;
    r.w += 4.0f;
    r.h += 4.0f;
    return r;
}

std::array<gfx::Rectf, 4> BuilderWindow::handleRects(
    const gfx::Rectf& r) const
{
    std::array<gfx::Rectf, 4> h{};
    for (int i = 0; i < 4; ++i)
        h[static_cast<size_t>(i)] =
            gfx::Rectf{(i % 2 ? r.x + r.w : r.x) - kHandle / 2.0f,
                       (i / 2 ? r.y + r.h : r.y) - kHandle / 2.0f, kHandle,
                       kHandle};
    return h;
}

bool BuilderWindow::isDocked(int nodeIdx) const
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(model_.nodes.size()))
        return false;
    if (const bld::Stmt* d =
            model_.findProp(model_.nodes[nodeIdx].id, "dock")) {
        std::string tail;
        if (bld::parseEnumTail(d->value, tail) && tail != "none")
            return true;
    }
    return false;
}

// Se builder_window.h — en syskonankrad KANT är låst.
bool BuilderWindow::edgeAnchored(int nodeIdx, const char* field) const
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(model_.nodes.size()))
        return false;
    const bld::Stmt* p = model_.findProp(model_.nodes[nodeIdx].id, field);
    if (!p)
        return false;
    std::string till;
    float       gap = 0.0f;
    // En OPAK rad (som previewn inte utför) låser ingenting — annars hade
    // UI:t låst en kant `resolve()` ändå placerar med x/y.
    return bld::parseSiblingAnchorText(p->value, till, gap) && !till.empty();
}

bool BuilderWindow::handleActive(int nodeIdx, int corner) const
{
    // Hörnhandtaget flyttar TVÅ kanter (frame-ordningen: 0=VÖ 1=HÖ 2=VN
    // 3=HN). Är någon av dem ankrad kan greppet inte hålla vad det lovar,
    // så handtaget visas inte och greppas inte.
    const bool movesRight  = (corner % 2) != 0;
    const bool movesBottom = (corner / 2) != 0;
    if (edgeAnchored(nodeIdx, movesRight ? "anchorRight" : "anchorLeft"))
        return false;
    if (edgeAnchored(nodeIdx, movesBottom ? "anchorBottom" : "anchorTop"))
        return false;
    return true;
}

// Se builder_window.h — målnoden för ett syskonankare.
int BuilderWindow::anchorTargetNode(int nodeIdx, const char* field,
                                    float& gap) const
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(model_.nodes.size()))
        return -1;
    const bld::Stmt* p = model_.findProp(model_.nodes[nodeIdx].id, field);
    if (!p)
        return -1;
    std::string till;
    if (!bld::parseSiblingAnchorText(p->value, till, gap) || till.empty())
        return -1;

    const std::string minFörälder = model_.parentOf(model_.nodes[nodeIdx].id);
    // STANNA VID MIG SJÄLV — samma regel som `Widget::earlierSibling`, och
    // det är därför en framåtreferens visas som "ingen koppling" i stället
    // för som en linje ramverket ändå inte ritar.
    for (int i = 0; i < nodeIdx; ++i) {
        const bld::WidgetNode& s = model_.nodes[std::size_t(i)];
        if (s.id == till && model_.parentOf(s.id) == minFörälder)
            return i;
    }
    return -1;
}

// Se builder_window.h — rita kopplingarna för den markerade noden.
void BuilderWindow::drawAnchorLinks(const glaze::Ctx& c, int nodeIdx)
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(previewByNode_.size()) ||
        !previewByNode_[nodeIdx])
        return;

    const gfx::Rectf r = previewByNode_[nodeIdx]->resolve(ctxForNode(nodeIdx));
    const float      s   = c.scale;
    const auto&      col = c.theme->colors;

    struct Kant {
        const char* fält;
        bool        vågrät;
    };
    static const Kant kanter[] = {{"anchorLeft", true},
                                  {"anchorRight", true},
                                  {"anchorTop", false},
                                  {"anchorBottom", false}};

    for (const Kant& k : kanter) {
        float     gap = 0.0f;
        const int ti  = anchorTargetNode(nodeIdx, k.fält, gap);
        if (ti < 0 || ti >= static_cast<int>(previewByNode_.size()) ||
            !previewByNode_[ti])
            continue;
        const gfx::Rectf t = previewByNode_[ti]->resolve(ctxForNode(ti));

        // Linjen går mellan de två kanter avståndet FAKTISKT mäts mellan,
        // så den man ser är den `resolve()` räknar med.
        float x1, y1, x2, y2;
        if (k.vågrät) {
            const float y = r.y + r.h / 2.0f;
            y1 = y2 = y;
            if (std::strcmp(k.fält, "anchorLeft") == 0) {
                x1 = t.x + t.w;
                x2 = r.x;
            } else {
                x1 = r.x + r.w;
                x2 = t.x;
            }
        } else {
            const float x = r.x + r.w / 2.0f;
            x1 = x2 = x;
            if (std::strcmp(k.fält, "anchorTop") == 0) {
                y1 = t.y + t.h;
                y2 = r.y;
            } else {
                y1 = r.y + r.h;
                y2 = t.y;
            }
        }
        c.r->line(x1, y1, x2, y2, col.accent);

        // Ändmarkeringar: en kort tvärstreck i var ände så det syns VILKA
        // två kanter måttet gäller, även när gapet är noll.
        const float tick = 4.0f * s;
        if (k.vågrät) {
            c.r->line(x1, y1 - tick, x1, y1 + tick, col.accent);
            c.r->line(x2, y2 - tick, x2, y2 + tick, col.accent);
        } else {
            c.r->line(x1 - tick, y1, x1 + tick, y1, col.accent);
            c.r->line(x2 - tick, y2, x2 + tick, y2, col.accent);
        }

        const std::string txt = bld::numText(gap);
        c.r->drawText(*c.font, (x1 + x2) / 2.0f + 3.0f * s,
                      (y1 + y2) / 2.0f - c.font->height() - 2.0f * s, txt,
                      col.accent);
    }
}

// Drag: uppdatera de offsets som nodens ANKARE pekar ut — vänsterkantens
// x, högerkantens right, osv. Bredden bevaras vid sträckning. Dockade
// widgets flyttas inte (docken äger placeringen), och en SYSKONANKRAD axel
// flyttas inte heller — av exakt samma skäl.
void BuilderWindow::dragTo(int nodeIdx, float lx, float ly)
{
    bld::WidgetNode& n = model_.nodes[nodeIdx];
    glaze::Widget*   w = previewByNode_[nodeIdx];
    if (!w)
        return;

    if (isDocked(nodeIdx)) {
        status_->setText("Dockad widget — placeringen ägs av docken");
        return;
    }

    // SYSKONANKARET äger sin axel. Per AXEL och inte per nod: en
    // widget som hänger på ett syskon i x-led ska fortfarande gå att dra
    // i y-led. Gapet redigeras i egenskapspanelen eller i kopplingsläget.
    const bool låstX = edgeAnchored(nodeIdx, "anchorLeft") ||
                       edgeAnchored(nodeIdx, "anchorRight");
    const bool låstY = edgeAnchored(nodeIdx, "anchorTop") ||
                       edgeAnchored(nodeIdx, "anchorBottom");
    if (låstX && låstY) {
        status_->setText("Syskonankrad i båda axlarna — placeringen ägs av "
                         "ankaret (ändra gapet i egenskapspanelen)");
        return;
    }
    if (låstX || låstY)
        status_->setText(std::string("Syskonankrad i ") +
                         (låstX ? "x-led" : "y-led") +
                         " — den axeln ägs av ankaret");

    // Widgetens aktuella mått + förälderns area i LOGISKA px. Vandringen
    // går genom `ctxForNode`: hela förälderkedjan och inte bara ETT trädled.
    const glaze::Ctx pc = ctxForNode(nodeIdx);
    const float      s  = pc.scale;
    const gfx::Rectf r  = w->resolve(pc);
    // INVERSEN räknar mot den SKURNA arean — den `resolve()`
    // faktiskt placerar mot. Muspunkten kommer i förälderns OSKURNA rymd
    // (samma som ritningen), så den flyttas hit FÖRE snappningen: annars
    // hade rutnätet legat i ett koordinatsystem och talen i ett annat.
    const gfx::Rectf pa   = layoutArea(nodeIdx, pc);
    const float      offX = (pa.x - pc.area.x) / s;
    const float      offY = (pa.y - pc.area.y) / s;
    const float wid     = r.w / s;
    const float hei     = r.h / s;
    const float areaW   = pa.w / s;
    const float areaH   = pa.h / s;

    float nx = static_cast<float>(
        (static_cast<int>(lx - offX) / kSnap) * kSnap);
    float ny = static_cast<float>(
        (static_cast<int>(ly - offY) / kSnap) * kSnap);
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;

    const glaze::Anchors a = w->anchors;
    if (!låstX) {
        if (a.left)
            model_.setProp(n.id, "x", bld::numText(nx));
        if (a.right)
            model_.setProp(n.id, "right", bld::numText(areaW - nx - wid));
        if (!a.left && !a.right)
            model_.setProp(n.id, "x",
                           bld::numText(nx - (areaW - wid) / 2.0f));
    }

    if (!låstY) {
        if (a.top)
            model_.setProp(n.id, "y", bld::numText(ny));
        if (a.bottom)
            model_.setProp(n.id, "bottom", bld::numText(areaH - ny - hei));
        if (!a.top && !a.bottom)
            model_.setProp(n.id, "y",
                           bld::numText(ny - (areaH - hei) / 2.0f));
    }

    rebuildPreview();
}

// Storleksändring: spegelbilden av dragTo. Musen flyttar EN kant (den som
// hörnhandtaget hör till) och den motstående står still — men VILKET FÄLT
// som skrivs styrs av ankarna, för `Widget::resolve` läser olika fält i de
// fyra fallen:
//
//   left && right   STRÄCKNING — `width` IGNORERAS helt. Storleken ändras
//                   genom OFFSETS (`x` / `right`), aldrig genom måttet.
//   bara left       vänsterkanten är `x`, högerkanten är x + width
//   bara right      högerkanten är `right`, vänsterkanten right − width
//   inget ankare    centrerad + offset — inversen står utskriven nedan
//
// Skrivs `width` i sträckningsfallet ser användaren INGENTING hända, och
// det är exakt den sortens tysta lögn handtagen fanns för att undvika.
void BuilderWindow::resizeTo(int nodeIdx, float mx, float my)
{
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(model_.nodes.size()))
        return;
    bld::WidgetNode& n = model_.nodes[nodeIdx];
    glaze::Widget*   w = previewByNode_[nodeIdx];
    if (!w)
        return;

    // SAMMA SKURNA AREA som draget och som `resolve()`: `right`/`bottom`
    // får inte räknas ur `pc.area` direkt — en höger- eller bottenankrad
    // storleksändring skulle skriva offsets i fel koordinatsystem så fort
    // föräldern har en dockad remsa.
    // `resizeL_/T_/R_/B_` och `resizeGrab*_` lagras redan mot den här ytan
    // (se MouseButtonDown), så allt nedan är i EN rymd.
    const glaze::Ctx pc    = ctxForNode(nodeIdx);
    const gfx::Rectf pa    = layoutArea(nodeIdx, pc);
    const float      s     = pc.scale;
    const float      areaW = pa.w / s;
    const float      areaH = pa.h / s;

    auto snap = [](float v) {
        return static_cast<float>(
            static_cast<int>(std::lround(v / kSnap)) * kSnap);
    };

    // Musens förflyttning sedan greppet, i FÖRÄLDER-LOGISKA px.
    const float dx = (mx - pa.x) / s - resizeGrabX_;
    const float dy = (my - pa.y) / s - resizeGrabY_;

    const bool movesRight  = (resizeCorner_ % 2) != 0;
    const bool movesBottom = (resizeCorner_ / 2) != 0;

    float L = resizeL_, T = resizeT_, R = resizeR_, B = resizeB_;
    if (movesRight)
        R = snap(resizeR_ + dx);
    else
        L = snap(resizeL_ + dx);
    if (movesBottom)
        B = snap(resizeB_ + dy);
    else
        T = snap(resizeT_ + dy);

    // Klampa mot minsta storlek genom att flytta tillbaka den kant
    // ANVÄNDAREN drar — den motstående måste stå still.
    if (R - L < kMinSize) {
        if (movesRight) R = L + kMinSize;
        else            L = R - kMinSize;
    }
    if (B - T < kMinSize) {
        if (movesBottom) B = T + kMinSize;
        else             T = B - kMinSize;
    }

    const glaze::Anchors a = w->anchors;

    // ---- x-axeln
    if (a.left && a.right) {
        if (movesRight)
            model_.setProp(n.id, "right", bld::numText(areaW - R));
        else
            model_.setProp(n.id, "x", bld::numText(L));
    } else if (a.left) {
        if (!movesRight)
            model_.setProp(n.id, "x", bld::numText(L));
        model_.setProp(n.id, "width", bld::numText(R - L));
    } else if (a.right) {
        if (movesRight)
            model_.setProp(n.id, "right", bld::numText(areaW - R));
        model_.setProp(n.id, "width", bld::numText(R - L));
    } else {
        // CENTRERAD. `resolve` gör
        //   px = c.area.x + (c.area.w − sz.w) / 2 + x * c.scale
        // (widget.cpp:297), så inversen för en önskad vänsterkant `Lpx` i
        // FYSISKA px är
        //   x = (Lpx − c.area.x − (c.area.w − sz.w) / 2) / c.scale
        // där `sz.w` är den NYA bredden. Utskriven och inte härledd på
        // plats: teckenfelet här är lätt att göra och svårt att se.
        const float Lpx = pa.x + L * s;
        const float szW = (R - L) * s;
        model_.setProp(n.id, "width", bld::numText(R - L));
        model_.setProp(
            n.id, "x",
            bld::numText((Lpx - pa.x - (pa.w - szW) / 2.0f) / s));
    }

    // ---- y-axeln, identisk med widget.cpp:307 (h/y/area.h)
    if (a.top && a.bottom) {
        if (movesBottom)
            model_.setProp(n.id, "bottom", bld::numText(areaH - B));
        else
            model_.setProp(n.id, "y", bld::numText(T));
    } else if (a.top) {
        if (!movesBottom)
            model_.setProp(n.id, "y", bld::numText(T));
        model_.setProp(n.id, "height", bld::numText(B - T));
    } else if (a.bottom) {
        if (movesBottom)
            model_.setProp(n.id, "bottom", bld::numText(areaH - B));
        model_.setProp(n.id, "height", bld::numText(B - T));
    } else {
        const float Tpx = pa.y + T * s;
        const float szH = (B - T) * s;
        model_.setProp(n.id, "height", bld::numText(B - T));
        model_.setProp(
            n.id, "y",
            bld::numText((Tpx - pa.y - (pa.h - szH) / 2.0f) / s));
    }

    rebuildPreview();   // modell → preview är ENKELRIKTAD
}

void BuilderWindow::removeSelected()
{
    if (selected_ < 0 || selected_ >= static_cast<int>(model_.nodes.size()))
        return;
    const std::string id = model_.nodes[selected_].id;

    // Ta bort noden + hela dess delträd (barn vars förälderkedja träffar
    // den). Handlers raderas ALDRIG (regel 5) — de blir tyst död kod som
    // kompilerar; varna i status.
    auto harEvent = [this](const std::string& nid) {
        for (const bld::Stmt& st : model_.stmts)
            if (st.kind == bld::Stmt::Kind::Event && st.target == nid)
                return true;
        return false;
    };
    bool hadEvents = harEvent(id);
    std::vector<std::string> doomed{id};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& n : model_.nodes) {
            bool parentDoomed = false;
            for (const auto& d : doomed)
                if (model_.parentOf(n.id) == d)
                    parentDoomed = true;
            bool already = false;
            for (const auto& d : doomed)
                if (n.id == d)
                    already = true;
            if (parentDoomed && !already) {
                doomed.push_back(n.id);
                hadEvents = hadEvents || harEvent(n.id);
                grew = true;
            }
        }
    }
    // removeNode tar noden OCH alla dess satser — en enda operation, så
    // ingen lista kan bli kvar med rader till en nod som inte finns.
    for (const auto& d : doomed)
        model_.removeNode(d);

    selected_ = -1;
    dragging_ = -1;
    rebuildPreview();
    refreshPropPanel();
    status_->setText("Tog bort " + id +
                     (hadEvents ? " — OBS: handler-koden lever kvar i .cpp "
                                  "(död kod; buildern raderar aldrig)"
                                : ""));
}

// ------------------------------------------------- KOPPLINGSLÄGET

void BuilderWindow::toggleLinkMode()
{
    if (failSafe_)
        return;
    linkMode_ = !linkMode_;
    linkFrom_ = -1;
    if (linkBtn_)
        linkBtn_->setText(linkMode_ ? "Koppla: PÅ" : "Koppla: av");
    status_->setText(linkMode_
                         ? "Kopplingsläge PÅ (Ctrl+L) — dra från widgeten "
                           "som ska HÄNGA till den den ska hänga PÅ"
                         : "Kopplingsläge av");
}

// Menyn efter släppet. OGILTIGA mål visas INAKTIVERADE med en förklaring i
// statusraden i stället för att tyst uteslutas — riktningsregeln
// ("tidigare syskon") är en konsekvens av adopt-/z-ordningen och alltså
// precis den sortens sak användaren inte kan gissa sig till.
void BuilderWindow::openLinkMenu(int fromNode, int toNode, float mx, float my)
{
    const int antal = static_cast<int>(model_.nodes.size());
    if (fromNode < 0 || fromNode >= antal || toNode < 0 || toNode >= antal)
        return;
    const bld::WidgetNode& f = model_.nodes[std::size_t(fromNode)];
    const bld::WidgetNode& t = model_.nodes[std::size_t(toNode)];

    // GILTIGHETEN är exakt `Widget::earlierSibling`s villkor: samma
    // förälder OCH tidigare i barnordningen. Menyn får inte kunna skriva
    // en rad `resolve()` sedan ignorerar.
    const bool sammaFörälder = model_.parentOf(f.id) == model_.parentOf(t.id);
    const bool tidigare      = toNode < fromNode;
    const bool giltigt       = sammaFörälder && tidigare;

    // AVSTÅNDET DEFAULTAR TILL DET FAKTISKA — kopplingen
    // ska inte FLYTTA något i det ögonblick den skapas; den ska bara göra
    // den placering som redan gäller till en RELATION.
    const glaze::Ctx fc = ctxForNode(fromNode);
    gfx::Rectf       r{}, tr{};
    if (previewByNode_[std::size_t(fromNode)])
        r = previewByNode_[std::size_t(fromNode)]->resolve(fc);
    if (previewByNode_[std::size_t(toNode)])
        tr = previewByNode_[std::size_t(toNode)]->resolve(ctxForNode(toNode));
    const float s = fc.scale > 0.0f ? fc.scale : 1.0f;

    struct Val {
        const char* fält;
        const char* namn;
        float       gap;   // logiska px, kant → kant
    };
    const Val val[4] = {
        {"anchorLeft", "Vänsterkant", (r.x - (tr.x + tr.w)) / s},
        {"anchorRight", "Högerkant", (tr.x - (r.x + r.w)) / s},
        {"anchorTop", "Överkant", (r.y - (tr.y + tr.h)) / s},
        {"anchorBottom", "Underkant", (tr.y - (r.y + r.h)) / s},
    };

    // EN FÄRSK `Menu` PER ÖPPNING, aldrig en återfylld. `close()` stänger
    // presentationen UPPSKJUTET (självdestruktionen är postad), medan
    // `items.clear()` verkar direkt — och `Menu::activatePath` slår upp
    // stigen i den LEVANDE `items`-vektorn, inte i presentationens
    // snapshot. En gammal presentation som överlevde omfyllningen hade
    // därför aktiverat den NYA menyns handler: rätt rad på skärmen, fel
    // koppling utförd.
    //
    // Anropsordningen gör det onåbart (hela sekvensen kör inom ETT
    // event, och pumpsvansen river den gamla presentationen innan nästa
    // event dispatchas) — men det är säkerhet som vilar på ordningen
    // mellan tre rader, och den sortens säkerhet överlever inte nästa
    // person som flyttar en av dem. Att byta ut hela objektet river
    // aliaset i stället för att undvika det: destruktorn stänger sina egna
    // presentationer, och det finns ingen kvarvarande vektor att peka in i.
    linkMenu_ = std::make_unique<glaze::Menu>();
    for (const Val& v : val) {
        const float gap = static_cast<float>(std::lround(v.gap));
        glaze::MenuItem& it = linkMenu_->addItem(
            std::string(v.namn) + " mot " + t.id + "  (" +
            bld::numText(gap) + " px)");
        it.enabled = giltigt;
        if (!giltigt)
            continue;
        // ID-STRÄNGAR och inte index: menyn är icke-blockerande, handlern
        // körs senare, och nodlistan kan ha ändrats till dess.
        const std::string frånId = f.id;
        const std::string tillId = t.id;
        const std::string fält   = v.fält;
        it.onClick = [this, frånId, tillId, fält, gap] {
            applyAnchorLink(frånId, tillId, fält, gap);
        };
    }

    linkMenu_->popupAt(root(), mx, my);

    if (giltigt)
        status_->setText("Välj kant för " + f.id + " → " + t.id);
    else if (!sammaFörälder)
        status_->setText("Ogiltigt mål: " + t.id + " ligger i en annan "
                         "container — ett syskonankare binder bara SYSKON");
    else
        status_->setText("Ogiltigt mål: " + t.id + " ligger EFTER " + f.id +
                         " i adopt-ordningen (= z-ordningen). Dra åt andra "
                         "hållet, eller byt ordning på widgetarna");
}

void BuilderWindow::applyAnchorLink(const std::string& fromId,
                                    const std::string& toId,
                                    const std::string& field, float gap)
{
    if (failSafe_ || !model_.find(fromId) || !model_.find(toId))
        return;
    // MÅLETS NAMN-RAD BEHÖVER INTE SÄKRAS LÄNGRE (etapp C): den är
    // KANONISK och står på varje nod redan när filen öppnats, så
    // nodnamnet ÄR runtime-namnet. Ankaret binder mot exakt det.
    const std::string ankarNamn = toId;
    const std::string lit = bld::siblingAnchorText(ankarNamn, gap);
    if (lit.empty()) {   // nodnamn = invariant, grenen kan inte tas
        status_->setText("kunde inte skriva ankarraden för " + ankarNamn);
        return;
    }
    model_.setProp(fromId, field, lit);
    rebuildPreview();
    refreshPropPanel();
    status_->setText(fromId + "." + field + " = {\"" + ankarNamn + "\", " +
                     bld::numText(gap) + "} — kopplat");
}

// Dubbelklicket (fråga 13): skapa/koppla <id>Clicked — WinForms-gesten.
void BuilderWindow::connectClickHandler(int nodeIdx)
{
    const bld::WidgetNode& n = model_.nodes[nodeIdx];
    for (const bld::Stmt& st : model_.stmts)
        if (st.kind == bld::Stmt::Kind::Event && st.target == n.id &&
            st.name == "onClick") {
            status_->setText(n.id + ".onClick → " + st.handler +
                             " (redan kopplad)");
            return;
        }
    bld::EventBind e{"onClick", "glaze::ClickArgs", n.id + "Clicked"};
    model_.appendEvent(n.id, e);
    pendingHandlers_.push_back(e);
    status_->setText("Kopplade " + n.id + ".onClick → " + e.handler +
                     " — Ctrl+S appendar deklaration + stub");
}

// ---------------------------------------------------------------- events

bool BuilderWindow::event(const gfx::Event& e)
{
    switch (e.type) {
    case gfx::EventType::KeyDown:
        // `navigationKey`: den råa kroken ser numpadens Delete (NumLock av)
        // som `KP_Delete` — kontraktet säger bind båda.
        if (gfx::navigationKey(e.key) == gfx::Key::Delete && selected_ >= 0 &&
            !failSafe_) {
            removeSelected();
            return true;
        }
        if ((e.mods & gfx::kModCtrl) && e.key == gfx::Key::S) {
            save();
            return true;
        }
        // KOPPLINGSLÄGET. Ctrl och inte en naken bokstav: `event()`
        // körs FÖRE routingen till fokuserad widget, så ett naket `L` hade
        // ätit tecknet mitt i en egenskapsredigering.
        if ((e.mods & gfx::kModCtrl) && e.key == gfx::Key::L) {
            toggleLinkMode();
            return true;
        }
        return false;

    case gfx::EventType::MouseButtonDown: {
        if (e.button != gfx::MouseButton::Left || failSafe_)
            return false;

        // KOPPLINGSLÄGET äger draget helt — inga handtag, ingen
        // markering, ingen placering. Gesten betyder något annat här, och
        // att blanda de två hade gjort båda otydliga.
        if (linkMode_) {
            const gfx::Rectf cv = canvasRect();
            if (!cv.contains(e.mouseX, e.mouseY))
                return false;
            glaze::Widget*   hit = nullptr;
            const glaze::Ctx cc  = canvasCtx();
            const auto&      kids = previewRoot_->children();
            for (auto it = kids.rbegin(); it != kids.rend() && !hit; ++it)
                hit = hitTestTree(**it, cc, e.mouseX, e.mouseY);
            linkFrom_ = hit ? nodeIndexFor(hit) : -1;
            linkX_    = e.mouseX;
            linkY_    = e.mouseY;
            if (linkFrom_ < 0)
                status_->setText("Kopplingsläge: börja draget PÅ en widget");
            else
                status_->setText("Drar koppling från " +
                                 model_.nodes[std::size_t(linkFrom_)].id +
                                 " — släpp på den widget den ska hänga på");
            return true;
        }

        // HANDTAGEN FÖRE TRÄD-HIT-TESTEN — och före CANVAS-GRINDEN. Ett
        // hörnhandtag är CENTRERAT på markeringens hörn och sticker
        // alltså ut halva sin bredd utanför widgeten. För en widget som
        // ligger an mot canvasens kant — vilket varje STRÄCKT eller
        // höger-/bottenankrad widget gör per definition — hamnar de yttre
        // handtagen då utanför `canvasRect()`, och grinden hade svalt
        // greppet innan hit-testen ens kördes. T.ex. slutar canvasen
        // vid y=737.5 medan panelens nedre handtag ligger på y=739.5.
        // Undantaget är SNÄVT: bara ett handtag på en REDAN markerad,
        // odockad nod, alltså aldrig en fri placering eller markering
        // utanför canvasen. Och den måste ligga före TRÄD-hit-testen av
        // samma skäl åt andra hållet: greppar man ett handtag som
        // överlappar en annan widget ska det bli en storleksändring, inte
        // en flytt av det som råkar ligga under. Dockade noder har inga
        // handtag alls (se frame()) och testas inte heller.
        if (selected_ >= 0 &&
            selected_ < static_cast<int>(previewByNode_.size()) &&
            previewByNode_[selected_] && !isDocked(selected_) &&
            workArea().contains(e.mouseX, e.mouseY)) {
            const std::array<gfx::Rectf, 4> hs =
                handleRects(selectionRect(selected_));
            for (int i = 0; i < 4; ++i) {
                // Ankrad kant = inget handtag. Frågan ställs på
                // SAMMA ställe som ritningen ställer den, annars kan ett
                // osynligt handtag ändå greppas.
                if (!handleActive(selected_, i))
                    continue;
                gfx::Rectf t = hs[static_cast<size_t>(i)];
                t.x -= kHandleSlack;
                t.y -= kHandleSlack;
                t.w += 2.0f * kHandleSlack;
                t.h += 2.0f * kHandleSlack;
                if (!t.contains(e.mouseX, e.mouseY))
                    continue;
                // Startgeometrin lagras NUMERISKT i förälder-logiska px:
                // rebuildPreview() river trädet vid varje motion, så en
                // sparad Widget*/referens hade dinglat direkt.
                const glaze::Ctx pc = ctxForNode(selected_);
                const gfx::Rectf r  = previewByNode_[selected_]->resolve(pc);
                const float      ps = pc.scale;
                // Mot den SKURNA arean — samma rymd som
                // `resizeTo` skriver `x`/`right` i, och som `resolve()`
                // läser dem i. Blandas de två uppstår ett hopp på exakt
                // remsans tjocklek i det ögonblick greppet släpps.
                const gfx::Rectf pa = layoutArea(selected_, pc);
                resizing_     = selected_;
                resizeCorner_ = i;
                resizeL_      = (r.x - pa.x) / ps;
                resizeT_      = (r.y - pa.y) / ps;
                resizeR_      = (r.x + r.w - pa.x) / ps;
                resizeB_      = (r.y + r.h - pa.y) / ps;
                resizeGrabX_  = (e.mouseX - pa.x) / ps;
                resizeGrabY_  = (e.mouseY - pa.y) / ps;
                status_->setText("Ändrar storlek på " +
                                 model_.nodes[selected_].id);
                return true;
            }
        }

        const gfx::Rectf cv = canvasRect();
        if (!cv.contains(e.mouseX, e.mouseY))
            return false;

        const float s  = window().uiScale();
        const float lx = (e.mouseX - cv.x) / s;
        const float ly = (e.mouseY - cv.y) / s;

        if (!pendingType_.empty()) {
            placeAt(lx, ly);
            pendingType_.clear();
            return true;
        }

        glaze::Widget* hit = nullptr;
        {
            const glaze::Ctx c = canvasCtx();
            const auto& kids = previewRoot_->children();
            for (auto it = kids.rbegin(); it != kids.rend() && !hit; ++it)
                hit = hitTestTree(**it, c, e.mouseX, e.mouseY);
        }
        selected_ = hit ? nodeIndexFor(hit) : -1;
        refreshPropPanel();

        if (selected_ >= 0) {
            // Dubbelklick = koppla onClick (WinForms-gesten).
            const std::uint64_t now = gfx::ticksMs();
            if (selected_ == lastClickNode_ &&
                now - lastClickMs_ < kDblClickMs) {
                connectClickHandler(selected_);
                lastClickMs_   = 0;
                lastClickNode_ = -1;
                return true;
            }
            lastClickMs_   = now;
            lastClickNode_ = selected_;

            // SAMMA koordinatväg som draget självt (`dragTo`) och som
            // handtagen: `ctxForNode` vandrar HELA förälderkedjan. Med
            // rotcanvasens ctx här blev greppunkten fel för varje barn och
            // barnbarn, och första motion-eventet ryckte till.
            const glaze::Ctx pc = ctxForNode(selected_);
            const gfx::Rectf r  = hit->resolve(pc);
            dragging_ = selected_;
            dragOffX_ = (e.mouseX - r.x) / pc.scale;
            dragOffY_ = (e.mouseY - r.y) / pc.scale;
            status_->setText("Markerade " + model_.nodes[selected_].id);
        } else {
            status_->setText("Inget markerat");
        }
        return true;
    }

    case gfx::EventType::MouseMotion: {
        if (failSafe_)
            return false;
        if (linkMode_) {
            if (linkFrom_ < 0)
                return false;
            linkX_ = e.mouseX;   // gummibandets fria ände
            linkY_ = e.mouseY;
            return true;
        }
        if (resizing_ >= 0) {
            resizeTo(resizing_, e.mouseX, e.mouseY);
            return true;
        }
        if (dragging_ < 0)
            return false;

        // Muspos → logiska px relativt FÖRÄLDERNS area (rotens = canvasen)
        // — samma vandring som ritningen, via ctxForNode.
        const glaze::Ctx pc = ctxForNode(dragging_);
        const float      s  = pc.scale;
        const float      lx = (e.mouseX - pc.area.x) / s - dragOffX_;
        const float      ly = (e.mouseY - pc.area.y) / s - dragOffY_;
        dragTo(dragging_, lx, ly);
        return true;
    }

    case gfx::EventType::MouseButtonUp:
        if (e.button == gfx::MouseButton::Left && linkMode_) {
            const int från = linkFrom_;
            linkFrom_      = -1;
            if (från < 0)
                return false;
            glaze::Widget*   hit = nullptr;
            const glaze::Ctx cc  = canvasCtx();
            const auto&      kids = previewRoot_->children();
            for (auto it = kids.rbegin(); it != kids.rend() && !hit; ++it)
                hit = hitTestTree(**it, cc, e.mouseX, e.mouseY);
            const int till = hit ? nodeIndexFor(hit) : -1;
            if (till < 0)
                status_->setText("Ingen widget under släppet — kopplingen "
                                 "avbröts");
            else if (till == från)
                status_->setText("En widget kan inte ankras mot sig själv");
            else
                openLinkMenu(från, till, e.mouseX, e.mouseY);
            return true;
        }
        if (e.button == gfx::MouseButton::Left &&
            (dragging_ >= 0 || resizing_ >= 0)) {
            const bool var = resizing_ >= 0;
            const int  nod = var ? resizing_ : dragging_;
            dragging_ = -1;
            resizing_ = -1;
            // `refreshPropPanel` körs efter ett drag — annars visar
            // width/height/x/y gamla tal så länge markeringen står kvar.
            // Den körs på MOUSE-UP och aldrig under
            // pågående motion: `setRows` river en aktiv editor, och på
            // mouse-up efter ett canvas-drag är ingen editor aktiv.
            refreshPropPanel();
            if (var && nod >= 0 &&
                nod < static_cast<int>(model_.nodes.size()))
                status_->setText("Ändrade storlek på " +
                                 model_.nodes[nod].id);
            return true;
        }
        return false;

    default:
        return false;
    }
}

// ---------------------------------------------------------------- ritning

void BuilderWindow::frame(const glaze::Ctx& c)
{
    const float s   = c.scale;
    const float tb  = kToolbox * s;
    const float pr  = kProps * s;
    const auto& col = c.theme->colors;

    // Mittenytan försänkt så canvasen (= fönstret som byggs) lyfter fram
    const gfx::Rectf mid{c.area.x + tb, c.area.y, c.area.w - tb - pr,
                         c.area.h};
    c.r->fillRect(mid, col.appWorkspace);

    // Fail-safe-läget: visa diagnosen + koden i stället för canvasen.
    if (failSafe_) {
        float px = mid.x + 16.0f * s;
        float py = c.area.y + 24.0f * s;
        for (const auto& t : failText_) {
            c.r->drawText(*c.font, px, py, t, col.text);
            py += c.font->height() + 2.0f * s;
            if (py > c.area.y + c.area.h - 24.0f * s)
                break;   // får inte plats — resten står i filen/statusen
        }
        const gfx::Rectf left{c.area.x, c.area.y, tb, c.area.h};
        const gfx::Rectf right{c.area.x + c.area.w - pr, c.area.y, pr,
                               c.area.h};
        c.r->fillRect(left, col.panel);
        c.r->fillRect(right, col.panel);
        return;
    }

    // Canvasen i fönstrets naturliga storlek
    const gfx::Rectf cv = canvasRect();
    // TITELN kan vara en ÖVERSÄTTNING. `builder_core` är
    // ren std och svarar bara med en LITERALS text (`title()`, 🐛 58);
    // buildern HAR en språkbutik och läser därför tr-id:t ur satsen själv,
    // precis som egenskapspanelen gör med tr-fälten. En titel med en konstant
    // eller ett uttryck visas inte — som i previewn.
    std::string   rubrik = model_.title();
    std::uint32_t trId   = 0;
    if (const bld::Stmt* ts = model_.lastWindowCall("setTitle");
        ts && bld::parseTrCall(ts->value, trId)) {
        const glaze::TrText t = glaze::tr(trId);
        rubrik                = t.str();
    }
    c.r->drawText(*c.font, cv.x, cv.y - 22.0f * s,
                  rubrik + "  —  " + model_.className + "  (" +
                      std::to_string(model_.clientWidth()) + "x" +
                      std::to_string(model_.clientHeight()) + ")",
                  col.textDim);
    c.r->fillRect(cv, col.windowBg);
    c.r->strokeRect(cv, col.border);

    // Förhandsvisningen — riktigt adopterat träd, samma vandring som appen
    glaze::Ctx pc = c;
    pc.area       = cv;
    for (glaze::Widget* w : previewRoot_->children())
        bld::drawPreviewTree(*w, pc);

    // Markering: accentram + hörnhandtag. Ramen och handtagen räknas ur
    // SAMMA `selectionRect`/`handleRects` som hit-testen och draget —
    // annars kan det man ser och det man kan greppa glida isär.
    if (selected_ >= 0 &&
        selected_ < static_cast<int>(previewByNode_.size()) &&
        previewByNode_[selected_]) {
        const gfx::Rectf r = selectionRect(selected_);
        // I KOPPLINGSLÄGET ritas varken ram eller handtag: draget
        // betyder något annat där, och en markeringsram hade lovat en
        // flytt-/storleksgest som inte finns. KOPPLINGSLINJERNA ritas
        // däremot — de är information och ingen affordans.
        if (!linkMode_) {
            c.r->strokeRect(r, col.accent);
            // DOCKAD NOD: markeringsramen visas, men INGA handtag. UI:t ska
            // inte lova vad draget inte kan hålla — docken äger både plats
            // och remsans mått, precis som `isFixedSize` gör att chromet
            // inte visar en resize-pekare på ett fönster OS:et vägrar
            // resiza. Remstjockleken (`width` för Left/Right, `height` för
            // Top/Bottom) redigeras i egenskapspanelen; Fill-dockade har
            // inga meningsfulla mått alls.
            //
            // ... och en SYSKONANKRAD KANT visar inget handtag på DEN
            // KANTEN, av exakt samma skäl: ankaret äger
            // placeringen, och ett handtag som inte kan hålla vad det lovar
            // är en tyst lögn.
            if (!isDocked(selected_)) {
                const std::array<gfx::Rectf, 4> hs = handleRects(r);
                for (int i = 0; i < 4; ++i)
                    if (handleActive(selected_, i))
                        c.r->fillRect(hs[static_cast<size_t>(i)], col.accent);
            }
        }

        // SYSKONKOPPLINGARNA, i samma lager som ramen och
        // handtagen — ovanpå previewn och utanför preview-trädets ritning.
        drawAnchorLinks(c, selected_);
    }

    // GUMMIBANDET: från källwidgetens mitt till pekaren.
    if (linkMode_ && linkFrom_ >= 0 &&
        linkFrom_ < static_cast<int>(previewByNode_.size()) &&
        previewByNode_[linkFrom_]) {
        const gfx::Rectf fr =
            previewByNode_[linkFrom_]->resolve(ctxForNode(linkFrom_));
        c.r->strokeRect(fr, col.accent);
        c.r->line(fr.x + fr.w / 2.0f, fr.y + fr.h / 2.0f, linkX_, linkY_,
                  col.accent);
    }

    // Paneler ritas efter canvasen så de täcker ev. överskjutande canvas
    const gfx::Rectf left{c.area.x, c.area.y, tb, c.area.h};
    const gfx::Rectf right{c.area.x + c.area.w - pr, c.area.y, pr, c.area.h};
    c.r->fillRect(left, col.panel);
    c.r->fillRect(right, col.panel);

    // Egenskaperna ritas av PropertyGrid-widgeten — panelen här
    // är bara bakgrunden den står på.
}
