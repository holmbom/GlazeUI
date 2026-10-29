#pragma once
#include <map>
#include <memory>
#include <type_traits>

// dll_gate — KONTROLLFIXTUREN (tests/dll_gate.cpp). Headern bryter MED FLIT
// mot regeln B5 (inget föränderligt statiskt tillstånd i en header): varje
// deklaration nedan är ett fall med KÄNT förväntat värde, och grinden kräver exakt det
// förväntat värde innan den litar på sig själv mot de riktiga headrarna och arkiven.
// Ligger under tests/ och inte under Glaze/, så de riktiga svepen ser den
// aldrig.
//
// DEL B (headers) ska ge, per regel:
//   H1 thread_local ................................ 1  (perTråd)
//   H2 föränderlig inline-variabel ................. 4  (delad, Medlem::antal,
//                                                        karta, ägd)
//   H3 föränderlig statisk lokal i en kropp ......... 3  (räknare, kräver,
//                                                        handtag)
// DEL A (objekten) ser bara det dll_gate_fixture.cpp ODR-använder: räknare,
// delad, perTråd, Medlem::antal, karta och ägd — exakt 6. Ändras en rad här
// ändras det förväntade värdet i dll_gate.cpp i samma ändring.

namespace glaze::dll_gate_fixture {

// ---- FYND

// H3 — kroppen inleds efter `)`.
inline int& räknare()
{
    static int n = 0;
    return n;
}

// H2 — en inline-variabel.
inline int delad = 0;

// H1 — trådlokal i en header (och inte H2: H1 äger thread_local).
inline thread_local int perTråd = 0;

// H2 — en inline static-medlem.
struct Medlem {
    inline static int antal = 0;
};

// H2 — FÖRÄNDERLIG, fast `const` står i deklarationen: den kvalificerar ett
// mallargument och inte objektet (en `const` var som
// helst undantog den här kartan).
inline std::map<const int, int> karta;

// H2 — objektet är `const`, men dynamiskt initierat och med föränderligt
// tillstånd bakom sig (smartpekarregeln). Del A ser det också: det ligger i
// en skrivbar sektion, för initieringen skriver det i körtid.
inline const std::unique_ptr<int> ägd = std::make_unique<int>(0);

// H3 — kroppen inleds efter en requires-klausul, inte efter `)`.
template <class T>
T& kräver() requires std::is_integral_v<T>
{
    static T v{};
    return v;
}

// H3 — `const`, men med föränderligt tillstånd BAKOM sig, och
// kroppen inleds efter en efterställd returtyp.
inline auto handtag() -> const std::shared_ptr<int>&
{
    static const std::shared_ptr<int> p = std::make_shared<int>(0);
    return p;
}

// ---- UNDANTAGNA (0 fynd i båda delarna)

inline constexpr int kOk[] = {1, 2, 3};
inline const int kKonst = 7;
inline const char* const kNamn = "dll_gate";

inline int undantagnaLokala()
{
    static constexpr int k  = 1;
    static const int     k2 = 2;
    static_assert(sizeof(int) >= 2);
    return static_cast<int>(k + k2);
}

struct Verktyg {
    static int f() { return 1; }   // statisk medlemsfunktion — ingen lokal
    static int deklarerad;         // definieras i en cpp — ingen inline-variabel
};

inline namespace v1 {
inline int funktionIInlineNamnrymd() { return 2; }
} // namespace v1

} // namespace glaze::dll_gate_fixture
