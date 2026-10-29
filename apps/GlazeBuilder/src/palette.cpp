#include "palette.h"

namespace bld {

// Default-storleken är 0 där widgeten AUTOSIZAR (axelregeln): en placerad
// widget ska visa STILENS mått, inte ett tal
// buildern hittat på — annars ser man aldrig vad ett stilbyte gör.
// 0 per AXEL, så en typ kan ha default på en axel och auto på den andra.
//
// De som behåller ett tal, och varför:
//   Panel     — containern dimensioneras av appen och autosizar med
//               flit inte.
//   Scrollbar — autosizar visst (scrollbar.cpp), men listen är LODRÄT i
//               dag och paletten bevarar den formen. Talen ska bort när
//               widgeten får `rotation = 90` i stället; det här är
//               alltså ett tidsbundet undantag, inte ett undantag från
//               axelregeln.
//   Picture   — naturmåttet av ett OSATT bildhandtag är {0,0}
//               (Draw::imageSize), så utan tal blir den placerade
//               bilden osynlig. Talet är en placeholder, inte en stil.
const PaletteEntry kPalette[] = {
    {"glaze::Label",     "Label",     0.0f,   0.0f},
    {"glaze::Button",    "Button",    0.0f,   0.0f},
    {"glaze::Checkbox",  "Checkbox",  0.0f,   0.0f},
    {"glaze::TextInput", "TextInput", 0.0f,   0.0f},
    {"glaze::Panel",     "Panel",     200.0f, 120.0f},
    {"glaze::Dropdown",  "Dropdown",  0.0f,   0.0f},
    {"glaze::Scrollbar", "Scrollbar", 10.0f,  120.0f},
    // Nivå 1 — de sex ensamma komponenterna. Utan en rad
    // här finns typen inte i verktygslådan (previewn ritar den ändå —
    // trädet frågar registret).
    {"glaze::Separator", "Separator", 0.0f,   0.0f},
    {"glaze::Picture",   "Picture",   64.0f,  64.0f},
    {"glaze::Link",      "Link",      0.0f,   0.0f},
    {"glaze::Radio",     "Radio",     0.0f,   0.0f},
    {"glaze::Slider",    "Slider",    0.0f,   0.0f},
    {"glaze::Progress",  "Progress",  0.0f,   0.0f},
    // Fler typer, med storlekar enligt axelregeln — tal bara där widgeten
    // med flit inte autosizar:
    //   PropertyGrid — container som Panel (contentSize 0×0).
    //   Bar/TopBar   — höjden är stilens barHeight (auto), men en TOM bar
    //                  mäter bredden 0 och hade blivit osynlig.
    //   StatusBar    — dockar Bottom och äger sin höjd; bredden är fri.
    //   Spacer       — width 0 BETYDER grow-vikt, så en placerad spacer
    //                  behöver en fast bredd för att gå att se/greppa.
    {"glaze::PropertyGrid", "PropertyGrid",  200.0f, 120.0f},
    {"glaze::Bar",          "Bar",           200.0f, 0.0f},
    {"glaze::TopBar",       "TopBar",        200.0f, 0.0f},
    {"glaze::StatusBar",    "StatusBar",     200.0f, 0.0f},
    {"glaze::Spacer",       "Spacer",        40.0f,  0.0f},
    {"glaze::CaptionButton","CaptionButton", 0.0f,   0.0f},
    // ListBox — TAL på båda axlarna, som Panel och
    // PropertyGrid: listan autosizar med FLIT inte (skrivet beslut i
    // listbox.cpp), så utan mått blir en placerad lista osynlig.
    {"glaze::ListBox",      "ListBox",       160.0f, 120.0f},
    // MenuBar — som StatusBar: dockad remsa som äger
    // sin HÖJD (`menuRowH`), bredden fri. Talet på x-axeln finns bara för
    // att en TOM menyrad annars mäter bredden 0 och blir osynlig i
    // previewn. Menyer som står i `.glazeui` (den PLATTA formen
    // `addMenu`/`addItem`/…) UTFÖRS av `bld::applyCall` och syns i
    // verktygsvägen; menyer som byggs i appens `onLoad` gör det aldrig —
    // användarkod parsas inte, och det gäller varje widget.
    {"glaze::MenuBar",      "MenuBar",       200.0f, 0.0f},
    // VERKTYGSFÄLTET. ToolBar som StatusBar: en
    // dockad remsa (Dock::Top ur konstruktorn) som äger sin HÖJD
    // (`toolBarH`). En TOM remsa mäter bara sin egen
    // luft (egen längd) — talet på x-axeln ger en nyplacerad remsa en
    // yta att släppa knappar i. ToolButton autosizar på båda axlarna —
    // placerad får den palettnamnet som text och därmed ett mått.
    {"glaze::ToolBar",      "ToolBar",       200.0f, 0.0f},
    {"glaze::ToolButton",   "ToolButton",    0.0f,   0.0f},
    // GRUPPRAMEN. Den AUTOSIZAR ur sina barn — men en NYPLACERAD
    // ram har inga barn, och då blir auto-måttet bara rubriken och luften.
    // Tal på båda axlarna, som Panel: en yta man ska kunna släppa saker i.
    {"glaze::GroupBox",     "GroupBox",      200.0f, 120.0f},
    // TALFÄLTET autosizar på båda axlarna — bredden ur stilen
    // (`spinDefaultW`), höjden ur fältet.
    {"glaze::SpinBox",      "SpinBox",       0.0f,   0.0f},
    // TRÄDET — TAL på båda axlarna, som ListBox: trädet autosizar
    // med FLIT inte (skrivet beslut i tree_view.h), så utan mått blir ett
    // placerat träd osynligt.
    {"glaze::TreeView",     "TreeView",      160.0f, 120.0f},
    // LISTVYN — TAL på båda axlarna, som ListBox: tabellen
    // autosizar med FLIT inte (skrivet beslut i table.h). Bredare än listan:
    // en nyplacerad listvy ska rymma två standardkolumner och huvudet.
    {"glaze::ListView",     "ListView",      240.0f, 140.0f},
    // DATAGRIDDEN — listvyns skäl och listvyns mått, något
    // bredare: en datagrid har oftast en kolumn till.
    {"glaze::DataGrid",     "DataGrid",      280.0f, 140.0f},
    // DELAREN — TAL på båda axlarna, som Panel: delaren är en
    // placeringsyta och autosizar med FLIT inte (skrivet beslut i
    // splitter.cpp). En nyplacerad delare visar sitt skikt även utan rutor.
    {"glaze::Splitter",     "Splitter",      300.0f, 200.0f},
    // DOCKNINGEN. Värden har TAL, som delaren: en placeringsyta
    // som med flit inte autosizar (dock_host.cpp). Panelen autosizar på
    // båda axlarna (`dockDefaultSize`) — och den dockar Left ur
    // konstruktorn, så i en värd blir den en remsa direkt.
    {"glaze::DockHost",     "DockHost",      400.0f, 300.0f},
    {"glaze::DockPanel",    "DockPanel",     0.0f,   0.0f},
    // FLIKARNA. Flikkontrollen har TAL, som delaren: en
    // placeringsyta som med flit inte autosizar (tab_control.cpp), och en
    // nyplacerad flikkontroll ska visa sin flikrad och sin yta även utan
    // sidor. Sidan har tal av Panels skäl — utanför en flikkontroll är den
    // en panel, och i en flikkontroll placeras den av den (måtten ignoreras).
    {"glaze::TabControl",   "TabControl",    300.0f, 200.0f},
    {"glaze::TabPage",      "TabPage",       200.0f, 120.0f},
    // FILVÄLJARFÄLTET autosizar på båda axlarna, som talfältet —
    // bredden ur stilen (`filePickerDefaultW`), höjden ur fältet.
    {"glaze::FilePicker",   "FilePicker",    0.0f,   0.0f},
    // FÄRGVÄLJAREN autosizar på båda axlarna — bredden ur stilen
    // (`colorPickerDefaultW`), höjden är fältets, som dropdownens.
    {"glaze::ColorPicker",  "ColorPicker",   0.0f,   0.0f},
    // DATUMVÄLJAREN autosizar på båda axlarna, som talfältet —
    // bredden ur stilen (`dateDefaultW`), höjden ur fältet. En nyplacerad
    // visar DAGENS datum (utan `setDate`).
    {"glaze::DatePicker",   "DatePicker",    0.0f,   0.0f},
    // DET FLERRADIGA FÄLTET autosizar på båda axlarna — bredden
    // ur stilen (`textAreaDefaultW`), höjden `textAreaRows` textrader.
    {"glaze::TextArea",     "TextArea",      0.0f,   0.0f},
    // MARKDOWN-VYN — TAL på båda axlarna av
    // ritytans skäl: vyn autosizar ur stilen och dokumentet, men en
    // nyplacerad vy har ingen text, och en yta som ska rulla behöver ett
    // mått man ser och drar i.
    {"glaze::MarkdownView", "MarkdownView",  300.0f, 200.0f},
    // DEN FLYTTBARA PANELEN — TAL på båda axlarna av Panels skäl:
    // en auto-axel blir panelens minsta mått, men en nyplacerad panel ska ha
    // ett mått att lägga barn i.
    {"glaze::FloatingPanel", "FloatingPanel", 200.0f, 150.0f},
#ifdef GLAZE_PREVIEW_TERMINAL
    // TERMINALEN — TAL på båda axlarna av Markdown-vyns
    // skäl: en nyplacerad terminal ska ha en yta man ser och drar i. Raden
    // finns bara när sidopaketet är länkat (builder_preview får då
    // definitionen): paletten erbjuder bara typer registret kan skapa
    // (preview_gate mäter det). Utan paketet läses en fil med en terminal
    // ändå — typen blir en stand-in, som en apps egen widget.
    {"glaze::Terminal",     "Terminal",      400.0f, 240.0f},
#endif
    // RITYTAN — TAL på båda axlarna: ytan autosizar ur
    // stilen (`canvasDefaultW`/`H`), men en placerad rityta ska ha ett mått
    // man ser och drar i, som Panel. Previewn utför formlistan (`addShape`),
    // lagren och sättarna (rutnätet, kameran, bakgrunden)
    // genom setter-vägen.
    {"glaze::Canvas",       "Canvas",        300.0f, 200.0f},
    // PROGRAMIKONEN autosizar på båda axlarna — måttet
    // härleds ur stilen och ur fönstrets chrome (`AppIcon::contentSize`).
    // Raden finns för att typen ÄR registrerad ("appicon") och registret ska
    // vara kartlagt i paletten (`preview_gate`); i ett fönster är ikonen den
    // inbyggda medlemmen `appIcon` och placeras aldrig med verktygslådan.
    {"glaze::AppIcon",      "AppIcon",       0.0f,   0.0f},
};

const std::size_t kPaletteCount = sizeof(kPalette) / sizeof(kPalette[0]);

const PaletteEntry* paletteByCpp(const std::string& cppType)
{
    for (std::size_t i = 0; i < kPaletteCount; ++i)
        if (cppType == kPalette[i].cppType)
            return &kPalette[i];
    return nullptr;
}

} // namespace bld
