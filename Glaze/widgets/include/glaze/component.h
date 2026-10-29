#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "glaze/language.h"   // TrText — `setTextToken`

// ICKE-VISUELLA MEDLEMMAR (WinForms `Component`) — basen för det som står som
// MEDLEM i ett fönster utan att
// vara en widget: `glaze::Timer` och `glaze::TrayIcon`, och egna komponenter.
// En komponent adopteras aldrig, ritas aldrig och har ingen plats i trädet —
// men den står i `.glazeui` precis som en widget (hård regel 8):
//
//     glaze::Timer    stangTimer;                     // medlem i regionen
//     stangTimer.name = "stangTimer";                 // rå fältrad
//     stangTimer.setInterval(1500);                   // setter-fält
//     stangTimer.onTick = [this](glaze::Timer* s, glaze::TimerArgs& e) {
//         stangTimerTick(s, e); };                    // sender = komponenten
//     stangTimer.start();                             // metodanrop
//
// TRE SAKER ÄR BASENS, och de är vad verktygen behöver:
//   * `name` — objektets namn, speglat som `Widget::name`. GlazeBuilder
//     emitterar namnraden på varje medlem, så varje medlemstyp i en region
//     måste ha ett publikt `name`.
//   * `setProp`/`setTextToken` — previewns väg in för setter-fälten, samma
//     kontrakt som widgetarnas (`Widget::setProp`, widget.h): true = nyckeln
//     KÄNDES IGEN (också med ett ogiltigt värde — logga då och ignorera),
//     false = okänd nyckel, som anroparen loggar och bevarar opakt (regel 4).
//   * REGISTRET, nycklat på C++-TYPNAMNET (`"glaze::Timer"`) — texten som
//     står i filens medlemsdeklaration. Därmed kan en komponent aldrig
//     hamna fel i en översättningstabell mellan gemena registernamn och
//     klassnamn.
//
// PREVIEWN. Previewn — glaze-render och
// GlazeBuilder — skapar BARA komponenter vars registrering uttryckligen
// säger att de är INERTA: konstruktorn och settrarna gör ingenting mot OS:et
// förrän `start()`/`show()`, som previewn aldrig utför. En komponent som inte
// sagt det hålls som STAND-IN: satserna bevaras opakt och utförs inte. En
// egen komponent som öppnar en port eller startar en tråd i konstruktorn
// hade annars gjort det i varje förhandsvisning.
//
// ICKE KOPIERBAR OCH ICKE FLYTTBAR: registren (timerns, systemfältets) och
// menyerna pekar på objektet, och en flytt hade lämnat dem dinglande.

namespace glaze {

class Component {
public:
    virtual ~Component() = default;
    Component(const Component&)            = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&)                 = delete;
    Component& operator=(Component&&)      = delete;

    // OBJEKTETS NAMN, speglat som `Widget::name` — för loggarna och för
    // verktygen. Ramverket läser det aldrig.
    std::string name;

    // Ett setter-fält som nyckel/värde-text (previewns väg). Värdet är
    // redan avkodat: en strängliteral avciterad, en enum som sitt sista
    // segment gement, tal och bool som de står. Basen känner inga nycklar.
    virtual bool setProp(const std::string& key, const std::string& value)
    {
        (void)key;
        (void)value;
        return false;
    }

    // Ett ÖVERSATT textfält (`glaze::tr(N)`) — `Widget::setTextToken`s
    // kontrakt: true bara när fältet FAKTISKT tog emot tokenet, annars blir
    // raden opak (regel 4) i stället för upplöst.
    virtual bool setTextToken(const std::string& key, const TrText& value)
    {
        (void)key;
        (void)value;
        return false;
    }

protected:
    Component() = default;
};

// HUR PREVIEWN FÅR BEHANDLA TYPEN — en del av registreringen, och utan
// förval: den som registrerar en komponent måste ta ställning.
//   Inert    konstruktorn och settrarna har inga sidoeffekter före
//            `start()`/`show()` (och de utför previewn aldrig) — previewn
//            skapar komponenten och utför dess setter-fält
//   StandIn  previewn skapar den ALDRIG; raderna bevaras opakt och utförs
//            inte (regel 4)
enum class PreviewForm : std::uint8_t { Inert, StandIn };

using ComponentFactory = std::unique_ptr<Component> (*)();

// Registrerar en fabrik för ett C++-typnamn. Första registreringen vinner;
// dubbletter loggas och ignoreras. Returvärdet finns bara för att kunna
// initiera en statisk bool (makrot nedan).
bool registerComponentType(const char* cppType, ComponentFactory make,
                           PreviewForm preview);

// Skapar en komponent ur registrets typnamn, utan egenskaper och utan
// event-koppling. nullptr vid okänd typ.
std::unique_ptr<Component> makeComponent(const std::string& cppType);

// Typens previewform, eller nullopt för en typ som inte är registrerad.
std::optional<PreviewForm> componentPreviewForm(const std::string& cppType);

// Alla registrerade typnamn, SORTERADE (`registeredWidgetTypes`s skäl: en
// grind som itererar registret ska ge samma utskrift varje körning).
std::vector<std::string> registeredComponentTypes();

// Läggs på namespace-nivå i komponentens cpp — `GLAZE_REGISTER_WIDGET`s
// form, med previewformen som tredje argument:
//     GLAZE_REGISTER_COMPONENT("glaze::Timer", Timer,
//                              ::glaze::PreviewForm::Inert)
// libglaze länkas WHOLE_ARCHIVE (Glaze/widgets/CMakeLists.txt), så de
// inbyggdas registreringar strippas aldrig; en egen komponent i app-target
// länkas alltid.
#define GLAZE_REGISTER_COMPONENT(cppType, Class, preview)                   \
    namespace {                                                              \
    const bool glazeComponentReg_##Class = ::glaze::registerComponentType(   \
        cppType,                                                             \
        []() -> std::unique_ptr<::glaze::Component> {                        \
            return std::make_unique<Class>();                                \
        },                                                                   \
        preview);                                                            \
    }

} // namespace glaze
