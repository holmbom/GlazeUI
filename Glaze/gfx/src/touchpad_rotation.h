#pragma once
#include <cstdint>
#include <span>
#include <vector>

// STYRPLATTANS VRIDNING UR RÅA KONTAKTER — det backend-neutrala. INTERN
// header: Windows Direct Manipulation har ingen rotation,
// så D3D11-backenden läser precisionsplattans HID-rapporter själv
// (backend_d3d11/d3d11_touchpad.cpp) och lämnar KONTAKTERNA hit — vinkeln,
// tröskeln, omslaget och ramarna är ren geometri och bor därför utanför
// backenden, där tests/trackpad_gate.cpp mäter dem headless mot handräknade
// tal (display_util-prejudikatet). Ren std, inga OS-typer.
//
// MODELLEN: en vridning är TVÅ fingrar vars förbindelselinje byter riktning.
// Vinkeln räknas ur kontakternas läge i plattans FYSISKA enheter (backenden
// skalar de logiska värdena med HID-beskrivningens fysiska gränser — annars
// snedvrider en platta med olika upplösning per axel vinkeln) med y NEDÅT,
// alltså samma riktning som skärmen: atan2 växer MEDURS, och en positiv
// vridning är medurs, fasadens tecken (`Event::gestureRotation`).

namespace gfx::touchpad {

// En kontakt i en ram. `tip` är fingret mot ytan (HID Tip Switch), och
// `confidence` att plattan tror att det ÄR ett finger (HID Confidence —
// obligatorisk i precisionsplattans specifikation; utan den är kontakten en
// handflata eller en tumme som vilar, och den räknas inte).
struct Contact {
    std::uint32_t id         = 0;
    double        x          = 0.0;   // fysiska enheter, x åt höger
    double        y          = 0.0;   // fysiska enheter, y NEDÅT
    bool          tip        = false;
    bool          confidence = false;
};

// TRÖSKELN innan en vridning GRIPER, i grader kumulativt sedan baslinjen —
// den filtrerar bort det minsta darret när två fingrar nyper eller drar. När
// den nåtts följer kartan fingrarnas TOTALA vridning: greppets ram hinner
// ikapp, och fingrarna tillbaka till startvinkeln ⇒ kartan exakt där den
// började. LÅG med flit, så att också en liten vridning nära ett snäpp
// snäpper: en nypning som darrar några grader vrider kartan lite, men
// vridsnäppet fångar den igen vid släppet.
inline constexpr double kGripDeg = 3.0;

// RAMEN UR RAPPORTERNA. En precisionsplatta rapporterar sina kontakter i
// PARALLELLT läge (alla i en rapport) eller i HYBRIDLÄGE: plattan har färre
// kontaktplatser per rapport än fingrar, och då bär den FÖRSTA rapporten
// antalet (Contact Count > 0) och de följande 0, tills ramens alla kontakter
// kommit. `add` tar en rapports räknare och dess platser i ordning och svarar
// true när en ram är komplett — den står då i `frame()` till nästa `add`.
//
// En rapport med räknaren 0 utan en påbörjad ram är en ram UTAN kontakter
// (en platta som rapporterar att allt lyfts), och den levereras tom. Av en
// fortsättningsrapport tas bara så många platser som ramen saknar — resten är
// tomma platser plattan fyller ut rapporten med.
class FrameAssembler {
public:
    bool add(int contactCount, std::span<const Contact> slots);
    std::span<const Contact> frame() const { return frame_; }
    void reset();

private:
    std::vector<Contact> collecting_;
    std::vector<Contact> frame_;
    int                  expected_ = 0;   // 0 = ingen ram påbörjad
};

// En rams utfall. `degrees` är vridningen att leverera sedan förra ramen
// (medurs +), och den är 0 tills vridningen gripit. `began` är ramen där den
// GREP (den bär HELA vridningen sedan baslinjen — kartan hinner ikapp),
// `ended` ramen där en gripen vridning tog slut.
struct RotationStep {
    double degrees = 0.0;
    bool   began   = false;
    bool   ended   = false;
};

// SPÅRAREN — ram för ram:
//   * EXAKT TVÅ kontakter med tip OCH confidence är en möjlig vridning; allt
//     annat (ett finger, tre, en ensam handflata) är ingen, och en gripen
//     vridning tar slut;
//   * vinkeln från kontakten med det LÄGRE id:t till den andra, så ordningen
//     i rapporten aldrig vänder den 180°;
//   * NY BASLINJE när kontaktmängden ändras — ett finger lyfts eller ett nytt
//     sätts ned, också i samma ram — och tröskeln gäller från början igen;
//   * ändringen mellan två ramar viks till (−180°, 180°], så en linje som
//     passerar ±180° inte ger ett varvs hopp;
//   * en handflata (confidence = false) räknas inte alls: två fingrar och en
//     vilande handflata vrider som två fingrar.
class RotationTracker {
public:
    RotationStep update(std::span<const Contact> frame);
    // Glöm allt — en gripen vridning tar slut utan att någon ram sagt det
    // (fönstret tappade tangentbordsfokus, en annan platta tog över).
    void reset();
    bool gripped() const { return gripped_; }

private:
    bool          haveBase_    = false;
    std::uint32_t idA_         = 0;
    std::uint32_t idB_         = 0;
    double        lastDeg_     = 0.0;
    double        total_       = 0.0;   // fingrarnas vridning sedan baslinjen
    double        delivered_   = 0.0;   // levererat hittills (= total_ gripen)
    bool          gripped_     = false;
};

// VAD TVÅ FINGRAR GÖR — gesten klassas EN gång per beröring och låses tills
// fingrarna lyfts. Rotering och zoom sker aldrig samtidigt — bara rotering +
// pan, zoom + pan, eller bara pan — och drar man två fingrar är panorering
// det som sker mest, så då ska ingen vridning ske:
//   * Undecided — fingrarna har inte rört sig tillräckligt än;
//   * Pan       — förflyttningen dominerar: ingen zoom och ingen vridning;
//   * Zoom      — zoom (+ den panorering som följer med), ingen vridning;
//   * Rotate    — vridning (+ panorering), ingen zoom.
// Zoom ELLER vridning avgörs HÄR ur fingrarna, där båda måtten finns i samma
// ram — Direct Manipulations skala kommer senare än kontakternas vinkel, så
// vridningen hade vunnit nästan varje nypning. Ritytan väljer också (glaze::Canvas,
// för Wayland, X11 och macOS, som levererar båda i samma gest), och får från
// Windows bara den ena.
// MÅTTEN är hur långt ETT finger rör sig av varje sak, sedan baslinjen och i
// samma enhet: förflyttningen = mittpunktens väg, zoomen = halva
// avståndsändringen, vridningen = vinkeln × halva avståndet (bågen). Det är
// en enhetslös jämförelse, och BESLUTET faller när den största når
// `kDecideFraction` av fingrarnas avstånd vid baslinjen — också det
// enhetslöst, så plattans upplösning och mätsystem spelar ingen roll.
// Panoreringen måste vara `kPanDominance` gånger större än den största av de
// andra: när ETT finger står still och det andra drar ut eller vrider rör
// sig mittpunkten lika långt som bågen, och den gesten menar zoom eller
// vridning. Vridningen måste på samma sätt vara `kRotateDominance` gånger
// zoomen: en nypning vrider nästan alltid lite, en vridning ändrar sällan
// avståndet — oavgjort blir ZOOM. Beslutet faller vid 15 % av avståndet
// (10 % var för snabbt för att hinna se vad fingrarna menade); panoreringen
// går ändå direkt, och zoom och vridning släpps ikapp när det faller.
enum class TwoFingerMode : std::uint8_t { None, Undecided, Pan, Zoom, Rotate };

inline constexpr double kDecideFraction  = 0.15;
inline constexpr double kPanDominance    = 2.0;
inline constexpr double kRotateDominance = 2.0;

// En rams utfall: läget nu, `decided` i ramen där beslutet föll, `ended`
// när en tvåfingersberöring tog slut (ett finger kom eller gick).
struct ClassifyStep {
    TwoFingerMode mode    = TwoFingerMode::None;
    bool          decided = false;
    bool          ended   = false;
};

class TwoFingerClassifier {
public:
    ClassifyStep update(std::span<const Contact> frame);
    void          reset();
    TwoFingerMode mode() const { return mode_; }

private:
    TwoFingerMode mode_ = TwoFingerMode::None;
    std::uint32_t idA_ = 0, idB_ = 0;
    double        cx0_ = 0.0, cy0_ = 0.0;   // mittpunkten vid baslinjen
    double        d0_  = 0.0;               // avståndet vid baslinjen
    double        a0_  = 0.0;               // vinkeln vid baslinjen (grader)
};

// IKAPP UTAN HOPP — hellre en liten seg del innan den startar, men sedan
// hinner den sakta ikapp, utan hopp. Det som HÖLLS före
// beslutet släpps inte på en gång utan blir en SKULD: varje följande steg
// levereras med en lika stor del av skulden till (`kCatchUp` × steget), så
// kartan rör sig dubbelt så fort som fingrarna tills den hunnit ikapp — och
// står still när fingrarna står still. Skulden som är kvar vid lyftet stryks.
// Samma regel står i glaze:s camera_math (`detail::catchUp`) för ritytans eget
// beslut; ändras den ena ändras den andra.
inline constexpr double kCatchUp = 1.0;
double catchUp(double step, double& debt);

// Vinkeln (grader) från a till b, medurs från x-axeln med y nedåt; false när
// punkterna sammanfaller och vinkeln saknas.
bool angleDeg(const Contact& a, const Contact& b, double& deg);

// Skillnaden b − a vikt till (−180°, 180°].
double wrapDeltaDeg(double a, double b);

} // namespace gfx::touchpad
