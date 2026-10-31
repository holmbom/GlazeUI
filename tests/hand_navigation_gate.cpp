// Prov av förflyttningen med händerna utan headset, runtime, fönster eller
// grafikenhet. De rena delarna (hand_navigation.h, locomotion.h) provas direkt
// med påhittade bildrutor.
//
// Prövar:
//   S  den virtuella spaken (MoveTurn): förskjutningen från nypets början i
//      huvudets låsta ram, skalad med räckvidden; dödzonen; klampningen; höger
//      hand vrider (sidled) och går inte framåt; ramen låses när nypet börjar
//   N  neutral återgång: ett nyp som redan hålls när läget slås på, byter läge
//      eller kommer tillbaka efter förlorad spårning ger inget förrän det
//      släppts
//   T  Teleport: höger nyp är spaken framåt, släppet noll; vänster hand rörs
//      inte; reserverade händer
//   A  avbrott: förlorad spårning, förlorat fokus, byte av källa och ny epok
//      avbryter utan utslag; NaN och ett huvud utan pose
//   M  `Mover`: landningen som begäran eller direkt, förlorat sikte avbryter
//      utan landning, det stegvisa vridningssteget
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "hand_navigation.h"
#include "locomotion.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace glaze::openxr;
using namespace glaze::openxr::detail;

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what, int line = 0)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FALL (rad " << line << "): " << what << "\n";
    }
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

constexpr double kPi = 3.14159265358979323846;

bool nearTo(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

// Huvudet vridet `yawDegrees` kring uppåt (positivt åt vänster, som OpenXR);
// 0 tittar längs −z, −90 längs +x.
TrackingPose headAt(double yawDegrees)
{
    TrackingPose head;
    head.position = Vec3{0.0, 1.7, 0.0};
    const double half = yawDegrees * kPi / 360.0;
    head.orientation = Quaternion{0.0, std::sin(half), 0.0, std::cos(half)};
    return head;
}

NavigationHand handAt(double x, double y, double z, bool pinching,
                      Controller::Source source = Controller::Source::Hand, std::uint64_t epoch = 1)
{
    NavigationHand h;
    h.source = source;
    h.epoch = epoch;
    h.tracked = true;
    h.pinching = pinching;
    h.pinchPoint = Vec3{x, y, z};
    return h;
}

NavigationInput inputOf(HandNavigation mode, const NavigationHand& left, const NavigationHand& right,
                        double headYaw = 0.0)
{
    NavigationInput in;
    in.mode = mode;
    in.rangeMetres = 0.12;
    in.deadzone = 0.2;
    in.focused = true;
    in.headValid = true;
    in.head = headAt(headYaw);
    in.hands = {left, right};
    return in;
}

// ------------------------------------------------------------ DEN VIRTUELLA SPAKEN
void stickCases()
{
    {   // S1: framåt, åt sidan, dödzonen och klampningen — i huvudets ram
        HandNavigator nav;
        const NavigationHand none = handAt(0.2, 1.2, -0.3, false);
        // Ett släppt nyp först: gesten väpnas.
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(-0.2, 1.2, -0.3, false), none));
        // Nypet börjar: ingen förskjutning än.
        NavigationOutput out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(-0.2, 1.2, -0.3, true), none));
        CHECK(out.drives[0] && out.drives[1], "S1 båda händerna styr i MoveTurn");
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "S1 nypets början är mitten");
        // Framåt (−z) 0,06 m = halva räckvidden: 0,5 → efter dödzonen (0,5 − 0,2) / 0,8.
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(-0.2, 1.2, -0.36, true), none));
        CHECK(nearTo(out.stickY[0], 0.375) && nearTo(out.stickX[0], 0.0), "S1 halv räckvidd framåt med dödzonen");
        // Hela räckvidden åt höger.
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(-0.08, 1.2, -0.3, true), none));
        CHECK(nearTo(out.stickX[0], 1.0) && nearTo(out.stickY[0], 0.0), "S1 hel räckvidd åt höger");
        // Långt över räckvidden klampas till enhetscirkeln.
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.3, 1.2, -0.8, true), none));
        CHECK(nearTo(std::hypot(out.stickX[0], out.stickY[0]), 1.0), "S1 klampad till enhetscirkeln");
        // Inom dödzonen: noll.
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(-0.19, 1.2, -0.31, true), none));
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "S1 inom dödzonen: noll");
        // Släppt: noll.
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.3, 1.2, -0.8, false), none));
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "S1 släppt: noll");
    }
    {   // S2: höger hand vrider — sidled, aldrig framåt
        HandNavigator nav;
        const NavigationHand left = handAt(-0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.2, 1.2, -0.3, false)));
        nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.2, 1.2, -0.3, true)));
        NavigationOutput out = nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.32, 1.2, -0.42, true)));
        CHECK(nearTo(out.stickX[1], 1.0), "S2 höger hand åt höger vrider åt höger");
        CHECK(out.stickY[1] == 0.0, "S2 höger hand ger aldrig framåt (teleporteringens spak)");
        out = nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.08, 1.2, -0.3, true)));
        CHECK(nearTo(out.stickX[1], -1.0), "S2 åt vänster vrider åt vänster");
    }
    {   // S3: referensramen låses när nypet börjar
        HandNavigator nav;
        const NavigationHand none = handAt(0.2, 1.2, 0.0, false);
        // Huvudet vridet åt höger (tittar längs +x) när nypet börjar.
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, false), none, -90.0));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, true), none, -90.0));
        NavigationOutput out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.12, 1.2, 0.0, true), none, -90.0));
        CHECK(nearTo(out.stickY[0], 1.0) && nearTo(out.stickX[0], 0.0, 1e-9), "S3 framåt är huvudets blick vid nypets början");
        // Huvudet vrids tillbaka medan nypet hålls: ramen ligger kvar.
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.12, 1.2, 0.0, true), none, 0.0));
        CHECK(nearTo(out.stickY[0], 1.0) && nearTo(out.stickX[0], 0.0, 1e-9), "S3 ramen är låst medan nypet hålls");
        // Ett nytt nyp tar den nya ramen.
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.12, 1.2, 0.0, false), none, 0.0));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.12, 1.2, 0.0, true), none, 0.0));
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.12, 1.2, -0.12, true), none, 0.0));
        CHECK(nearTo(out.stickY[0], 1.0), "S3 ett nytt nyp låser den nya ramen");
    }
    {   // S4: huvudet utan pose vid nypets början: gesten börjar inte, och börjar när huvudet kommer
        HandNavigator nav;
        const NavigationHand none = handAt(0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, false), none));
        NavigationInput in = inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, true), none);
        in.headValid = false;
        NavigationOutput out = nav.update(in);
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "S4 utan huvudpose börjar ingen gest");
        in.headValid = true;
        out = nav.update(in);
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "S4 gesten börjar där handen står nu");
        in.hands[0] = handAt(0.0, 1.2, -0.24, true);
        out = nav.update(in);
        CHECK(nearTo(out.stickY[0], 1.0), "S4 därefter räknas förskjutningen");
    }
}

// ------------------------------------------------------------ NEUTRAL ÅTERGÅNG
void neutralCases()
{
    {   // N1: ett nyp som redan hålls när läget slås på ger inget förrän det släppts
        HandNavigator nav;
        const NavigationHand none = handAt(0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::None, handAt(0.0, 1.2, 0.0, true), none));
        NavigationOutput out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, true), none));
        CHECK(!out.stickX[0] && !out.stickY[0], "N1 hållet nyp när läget slås på: inget");
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, true), none));
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "N1 handen rör sig men nypet är aldrig släppt: inget");
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, false), none));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, true), none));
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.24, true), none));
        CHECK(nearTo(out.stickY[0], 1.0), "N1 efter ett släpp börjar en ny gest");
    }
    {   // N2: byte av läge mitt i ett nyp kräver ett släpp
        HandNavigator nav;
        const NavigationHand left = handAt(-0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, false)));
        NavigationOutput out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickY[1] == 1.0, "N2 Teleport: nypet ger spaken framåt");
        out = nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickX[1] == 0.0 && out.stickY[1] == 0.0, "N2 läget byts under nypet: inget utslag");
        out = nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.32, 1.2, -0.3, true)));
        CHECK(out.stickX[1] == 0.0, "N2 nypet hålls fortfarande: inget utslag");
        nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.32, 1.2, -0.3, false)));
        nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.32, 1.2, -0.3, true)));
        out = nav.update(inputOf(HandNavigation::MoveTurn, left, handAt(0.44, 1.2, -0.3, true)));
        CHECK(nearTo(out.stickX[1], 1.0), "N2 efter släppet vrider höger hand igen");
    }
    {   // N3: ett nyp som hålls när spårningen kommer tillbaka börjar ingen gest
        HandNavigator nav;
        const NavigationHand none = handAt(0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, false), none));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, true), none));
        NavigationHand lost = handAt(0.0, 1.2, -0.06, true);
        lost.tracked = false;
        NavigationOutput out = nav.update(inputOf(HandNavigation::MoveTurn, lost, none));
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "N3 förlorad spårning: noll");
        CHECK(out.aborted[0] && !out.aborted[1], "N3 den pågående gesten markeras avbruten");
        out = nav.update(inputOf(HandNavigation::MoveTurn, lost, none));
        CHECK(!out.aborted[0], "N3 avbrottet markeras en gång");
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, true), none));
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "N3 spårningen tillbaka med nypet hållet: inget");
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, false), none));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.12, true), none));
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.24, true), none));
        CHECK(nearTo(out.stickY[0], 1.0), "N3 efter ett släpp börjar en ny gest");
        out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, -0.24, false), none));
        CHECK(!out.aborted[0], "N3 ett släpp är inget avbrott");
    }
}

// ------------------------------------------------------------------ TELEPORT
void teleportCases()
{
    HandNavigator nav;
    const NavigationHand leftOpen = handAt(-0.2, 1.2, -0.3, false);
    NavigationOutput out = nav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.2, 1.2, -0.3, false)));
    CHECK(!out.reserved[0] && !out.reserved[1], "T1 utan gest är ingen hand reserverad");
    CHECK(!out.drives[0] && out.drives[1], "T1 bara höger hand styr i Teleport");
    out = nav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.2, 1.2, -0.3, true)));
    CHECK(out.stickY[1] == 1.0 && out.stickX[1] == 0.0, "T1 nypet är spaken helt framåt");
    CHECK(!out.reserved[0] && out.reserved[1], "T1 höger hand är reserverad medan gesten pågår");
    out = nav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.5, 1.5, -0.9, true)));
    CHECK(out.stickY[1] == 1.0, "T1 spaken ligger kvar hur handen än rör sig");
    out = nav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.5, 1.5, -0.9, false)));
    CHECK(out.stickY[1] == 0.0 && out.stickX[1] == 0.0, "T1 släppet är spaken i mitten");

    // Vänster hand: ingen gest, men reserverad är den inte heller.
    nav.update(inputOf(HandNavigation::Teleport, handAt(-0.2, 1.2, -0.3, false), handAt(0.2, 1.2, -0.3, false)));
    out = nav.update(inputOf(HandNavigation::Teleport, handAt(-0.2, 1.2, -0.3, true), handAt(0.2, 1.2, -0.3, false)));
    CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0 && !out.reserved[0], "T2 vänster nyp rörs inte i Teleport");

    // MoveTurn styr med båda händerna; utan läge ingen.
    out = nav.update(inputOf(HandNavigation::MoveTurn, leftOpen, handAt(0.2, 1.2, -0.3, false)));
    CHECK(out.drives[0] && out.drives[1] && !out.reserved[0] && !out.reserved[1],
          "T3 MoveTurn styr med båda händerna, reserverar ingen utan gest");
    out = nav.update(inputOf(HandNavigation::None, leftOpen, handAt(0.2, 1.2, -0.3, true)));
    CHECK(!out.reserved[0] && !out.reserved[1] && !out.drives[0] && !out.drives[1], "T3 utan läge reserveras ingen");

    // T4: ett nyp som börjar medan gränssnittet tagit handen är gränssnittets —
    // ingen gest förrän nypet släppts, inte heller när handen lämnar panelen.
    {
        HandNavigator panelNav;
        panelNav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.2, 1.2, -0.3, false)));
        NavigationHand onPanel = handAt(0.2, 1.2, -0.3, true);
        onPanel.claimed = true;
        out = panelNav.update(inputOf(HandNavigation::Teleport, leftOpen, onPanel));
        CHECK(out.stickY[1] == 0.0 && !out.reserved[1], "T4 nyp på en panel: ingen gest");
        out = panelNav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickY[1] == 0.0 && !out.reserved[1], "T4 nypet hålls utanför panelen: fortfarande ingen gest");
        panelNav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.2, 1.2, -0.3, false)));
        out = panelNav.update(inputOf(HandNavigation::Teleport, leftOpen, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickY[1] == 1.0 && out.reserved[1], "T4 efter släppet börjar en gest");
    }
}

// -------------------------------------------------------------------- AVBROTT
void cancelCases()
{
    {   // A1: förlorat fokus avbryter; nypet måste släppas efteråt
        HandNavigator nav;
        const NavigationHand left = handAt(-0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, false)));
        NavigationOutput out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickY[1] == 1.0, "A1 spaken framåt");
        NavigationInput blur = inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true));
        blur.focused = false;
        out = nav.update(blur);
        CHECK(out.stickY[1] == 0.0, "A1 utan fokus: spaken släpps");
        out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickY[1] == 0.0, "A1 fokus tillbaka med nypet hållet: ingen ny gest");
    }
    {   // A2: byte av källa och ny epok avbryter
        HandNavigator nav;
        const NavigationHand left = handAt(-0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, false)));
        NavigationOutput out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true)));
        CHECK(out.stickY[1] == 1.0, "A2 spaken framåt");
        out = nav.update(inputOf(HandNavigation::Teleport, left,
                                 handAt(0.2, 1.2, -0.3, true, Controller::Source::Controller, 2)));
        CHECK(!out.drives[1] && !out.reserved[1] && out.stickY[1] == 0.0,
              "A2 en kontroll tar över: handen styr inte och är inte reserverad");
        out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true, Controller::Source::Hand, 3)));
        CHECK(out.drives[1] && out.stickY[1] == 0.0, "A2 handen tillbaka med nypet hållet: väntar på släpp");
        nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, false, Controller::Source::Hand, 3)));
        out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true, Controller::Source::Hand, 3)));
        CHECK(out.stickY[1] == 1.0, "A2 efter släppet börjar en ny gest");
        // En ny epok med samma källa avbryter också.
        out = nav.update(inputOf(HandNavigation::Teleport, left, handAt(0.2, 1.2, -0.3, true, Controller::Source::Hand, 4)));
        CHECK(out.stickY[1] == 0.0, "A2 ny epok: avbrutet");
    }
    {   // A3: NaN och oändligt ger aldrig ett utslag med NaN
        HandNavigator nav;
        const NavigationHand none = handAt(0.2, 1.2, -0.3, false);
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, false), none));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, true), none));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        NavigationOutput out = nav.update(inputOf(HandNavigation::MoveTurn, handAt(nan, 1.2, 0.0, true), none));
        CHECK(out.stickX[0] == 0.0 && out.stickY[0] == 0.0, "A3 NaN i nypets punkt: noll");
        NavigationInput in = inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, true), none);
        in.rangeMetres = nan;
        in.deadzone = nan;
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, false), none));
        nav.update(inputOf(HandNavigation::MoveTurn, handAt(0.0, 1.2, 0.0, true), none));
        in.hands[0] = handAt(0.0, 1.2, -0.12, true);
        out = nav.update(in);
        CHECK(std::isfinite(out.stickX[0]) && std::isfinite(out.stickY[0]), "A3 NaN i räckvidd och dödzon: ändligt utslag");
        CHECK(out.stickY[0] > 0.9, "A3 förvalen gäller då (0,12 m, dödzon 0,2)");
    }
}

// ---------------------------------------------------------------------- MOVER
// Ett sikte rakt framåt och en träff på marken 3 m bort.
LocomotionInput teleportInput(bool defer, double stickY, bool aimValid)
{
    LocomotionInput in;
    in.deltaSeconds = 0.01;
    in.focused = true;
    in.headValid = true;
    in.head = headAt(0.0);
    in.floorHeight = 0.0;
    in.deferLanding = defer;
    in.hands[1].aimValid = aimValid;
    in.hands[1].aim = TrackingPose{Vec3{0.2, 1.2, -0.3}, Quaternion{}};
    in.hands[1].stickY = stickY;
    return in;
}

glaze::HitResult groundAt(double x, double y)
{
    glaze::HitResult hit;
    hit.kind = glaze::HitKind::Surface;
    hit.point = glaze::Point3{x, y, 0.0};
    hit.normal = glaze::Vec3{0.0, 0.0, 1.0};
    hit.distance = 1.0;
    return hit;
}

void moverCases()
{
    Locomotion settings;
    settings.teleport = true;
    settings.teleportPath = Locomotion::Path::Straight;
    const SegmentCast cast = [](const glaze::Ray3&, double) { return std::vector<glaze::HitResult>{groundAt(2.0, 3.0)}; };

    {   // M1: släppt med svartoning på: en begäran, ingen flytt
        Mover mover;
        Anchor anchor;
        mover.update(teleportInput(true, 1.0, true), settings, anchor, cast);
        CHECK(mover.arcHit().has_value() && !mover.arc().empty(), "M1 vägen visas medan spaken hålls");
        mover.update(teleportInput(true, 0.0, true), settings, anchor, cast);
        const std::optional<Landing> request = mover.takeLanding();
        CHECK(request.has_value(), "M1 släppet ger en landningsbegäran");
        CHECK(request && nearTo(request->feet.x, 2.0) && nearTo(request->feet.y, 3.0) && nearTo(request->feet.z, 0.0),
              "M1 begäran är där vägen landade");
        CHECK(anchor.origin == Vec3{} && !mover.landed(), "M1 förankringen är orörd");
        CHECK(!mover.takeLanding().has_value(), "M1 begäran tas en gång");
    }
    {   // M2: utan svartoning landar den direkt
        Mover mover;
        Anchor anchor;
        mover.update(teleportInput(false, 1.0, true), settings, anchor, cast);
        mover.update(teleportInput(false, 0.0, true), settings, anchor, cast);
        CHECK(mover.landed() && !mover.takeLanding().has_value(), "M2 direkt landning, ingen begäran");
        const auto head = toWorldPose(anchor, headAt(0.0));
        CHECK(head && nearTo(head->position.x, 2.0, 1e-6) && nearTo(head->position.y, 3.0, 1e-6) &&
                  nearTo(head->position.z, 1.7, 1e-6),
              "M2 fötterna står på träffpunkten, huvudet över dem");
        mover.update(teleportInput(false, 0.0, true), settings, anchor, cast);
        CHECK(!mover.landed(), "M2 landed gäller bara senaste uppdateringen");
    }
    {   // M3: tappat sikte under teleporteringen: avbrutet, ingen landning
        Mover mover;
        Anchor anchor;
        mover.update(teleportInput(true, 1.0, true), settings, anchor, cast);
        mover.update(teleportInput(true, 1.0, false), settings, anchor, cast);
        mover.update(teleportInput(true, 0.0, true), settings, anchor, cast);
        CHECK(!mover.takeLanding().has_value() && !mover.landed() && anchor.origin == Vec3{},
              "M3 förlorat sikte avbryter utan landning (begäran)");

        Mover direct;
        Anchor anchor2;
        direct.update(teleportInput(false, 1.0, true), settings, anchor2, cast);
        direct.update(teleportInput(false, 0.0, false), settings, anchor2, cast);
        direct.update(teleportInput(false, 0.0, true), settings, anchor2, cast);
        CHECK(!direct.landed() && anchor2.origin == Vec3{}, "M3 förlorat sikte avbryter utan landning (direkt)");

        // Förlorat fokus likaså.
        Mover blurred;
        Anchor anchor3;
        blurred.update(teleportInput(true, 1.0, true), settings, anchor3, cast);
        LocomotionInput blur = teleportInput(true, 0.0, true);
        blur.focused = false;
        blurred.update(blur, settings, anchor3, cast);
        blurred.update(teleportInput(true, 0.0, true), settings, anchor3, cast);
        CHECK(!blurred.takeLanding().has_value(), "M3 förlorat fokus avbryter utan landning");
    }
    {   // M4: cancelTeleport avbryter, och steget i den stegvisa vridningen anmäls
        Mover mover;
        Anchor anchor;
        mover.update(teleportInput(true, 1.0, true), settings, anchor, cast);
        mover.cancelTeleport();
        mover.update(teleportInput(true, 0.0, true), settings, anchor, cast);
        CHECK(!mover.takeLanding().has_value(), "M4 cancelTeleport avbryter utan landning");

        Locomotion snap;
        snap.turn = TurnMode::Snap;
        snap.snapDegrees = 30.0;
        LocomotionInput in = teleportInput(false, 0.0, true);
        in.hands[1].stickX = 1.0;
        mover.update(in, snap, anchor, cast);
        CHECK(mover.snapped() && nearTo(anchor.yawDegrees, 30.0), "M4 ett vridningssteg anmäls");
        mover.update(in, snap, anchor, cast);
        CHECK(!mover.snapped() && nearTo(anchor.yawDegrees, 30.0), "M4 spaken hålls: inget nytt steg");
        Locomotion smooth;
        smooth.turn = TurnMode::Smooth;
        mover.update(in, smooth, anchor, cast);
        CHECK(!mover.snapped() && anchor.yawDegrees > 30.0, "M4 jämn vridning är inget steg");
    }
}

} // namespace

int main()
{
    stickCases();
    neutralCases();
    teleportCases();
    cancelCases();
    moverCases();
    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll\n";
        return 1;
    }
    std::printf("hand_navigation_gate: alla %d kontroller gröna\n", g_checks);
    return 0;
}
