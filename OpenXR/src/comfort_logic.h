#pragma once

// KOMFORTENS RENA DELAR — vinjettens styrka och svarttoningens förlopp. Ingen
// ritning, ingen session och ingen GPU, så delarna provas utan headset
// (tests/comfort_gate). Ritningen står i comfort_pass.h.
//
// VINJETTEN mäts ur den UTFÖRDA artificiella rörelsen: förankringens ändring
// under en bildruta, räknad med SAMMA huvudpose före och efter, så huvudets
// egen rörelse aldrig ger något. Hastigheten (m/s), vridningen (°/s) och
// skalningen (|ln skala|/s) delas med sitt `Comfort::full*`; det största av
// dem, klampat till 0 … 1, är det styrkan strävar mot. En stegvis vridning är
// en kort puls.
//
// SVARTTONINGEN gör en teleportering till: tona ut → en helt svart bildruta →
// flytten → tona in.

#include <cstdint>
#include <optional>

#include <glaze_openxr/session.h>

#include "locomotion.h"

namespace glaze::openxr::detail {

// ------------------------------------------------------------------ VINJETT

// Den utförda rörelsen under en bildruta, som hastigheter.
struct MotionSample {
    double deltaSeconds       = 0.0;
    double metresPerSecond    = 0.0;   // förflyttning i rummet (meter, utan skala)
    double degreesPerSecond   = 0.0;   // jämn vridning
    double scaleRatePerSecond = 0.0;   // |ln(skala efter / före)| per sekund
    double snapDegrees        = 0.0;   // ett stegvis vridningssteg i bildrutan, annars 0
};

// Rörelsen när förankringen gick från `before` till `after` under `deltaSeconds`
// och huvudet stod i `head` (spårningsrummet) i båda. `snap`: vridningen var ett
// steg, som då blir `snapDegrees` i stället för en hastighet. Noll vid
// `deltaSeconds` ≤ 0, icke-ändliga värden eller en ogiltig pose.
MotionSample measureMotion(const Anchor& before, const Anchor& after, const TrackingPose& head,
                           double deltaSeconds, bool snap);

// Vinjettens styrka 0 … 1 över tid. Den närmar sig målet linjärt: upp på
// `Comfort::attackSeconds`, ned på `Comfort::releaseSeconds`.
class Vignette {
public:
    // Tar en bildruta och ger styrkan efter den. `deltaSeconds` ≤ 0 och
    // icke-ändliga värden ändrar ingenting.
    double update(const MotionSample& motion, const Comfort& comfort);
    double strength() const noexcept { return strength_; }
    void   reset() noexcept;

private:
    double strength_  = 0.0;
    double pulseGoal_ = 0.0;   // pulsens mål medan `pulseLeft_` räcker
    double pulseLeft_ = 0.0;   // sekunder kvar av pulsen
};

// Öppningen i ett öga: den fria halvvinkeln (radianer från blickens axel)
// och kantens bredd. Vid styrka 0 är öppningen ögats bredaste hörn, så inget
// mörknar; vid 1 är den `Comfort::minFieldDegrees` (hela vinkeln). Kanten går
// från helt klart vid `innerRadians` till helt mörkt vid `innerRadians +
// featherRadians`. Ögats synfält är det ASYMMETRISKA från runtimen.
struct VignetteShape {
    double innerRadians   = 0.0;
    double featherRadians = 0.0;
};
VignetteShape vignetteShape(const Fov& fov, double strength, const Comfort& comfort);

// ------------------------------------------------------------- SVARTTONING

class TeleportFade {
public:
    enum class Phase : std::uint8_t { Idle, FadingOut, Black, FadingIn };

    // Det en bildruta ska göra: hur svart den ritas, och flytten som ska
    // utföras i början av den (efter en inlämnad svart bildruta).
    struct Step {
        double                 alpha = 0.0;
        std::optional<Landing> move;
    };

    // En ny begäran ersätter en väntande utan att börja om; under intoningen
    // tonar den ut igen från där den står.
    void request(const Landing& landing);
    // Avbryter utan flytt och tonar bort direkt (fokusförlust, stopp).
    void cancel() noexcept;
    // Avbryter och ger flytten som ännu inte utförts, till att göras direkt
    // (svarttoningen stängdes av).
    std::optional<Landing> flush() noexcept;

    // Tar en bildruta. Utan fokus avbryts allt. `canMove` falskt (huvudet
    // saknar pose) skjuter flytten upp. Efter en svart bildruta som inte
    // lämnades in (`blackSubmitted` uteblev) flyttar den ändå efter en
    // tidsgräns; saknas huvudet då avbryts flytten och bilden tonar in, så
    // den aldrig fastnar i svart.
    Step advance(double deltaSeconds, bool focused, bool canMove, const Comfort& comfort);
    // Bildrutan som `advance` gav med alpha 1 lämnades in till headsetet.
    void blackSubmitted() noexcept;

    Phase  phase() const noexcept { return phase_; }
    bool   active() const noexcept { return phase_ != Phase::Idle; }
    double alpha() const noexcept { return alpha_; }

private:
    Phase                  phase_ = Phase::Idle;
    double                 alpha_ = 0.0;
    bool                   submitted_ = false;
    double                 blackSeconds_ = 0.0;
    std::optional<Landing> pending_;
};

} // namespace glaze::openxr::detail
