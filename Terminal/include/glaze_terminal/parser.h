#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// VT-TOLKEN — Paul Williams DEC ANSI-tillståndsmaskin
// (https://vt100.net/emu/dec_ansi_parser), ren std. Den vet ingenting om vad
// en sekvens BETYDER: den delar byteströmmen i tecken, styrtecken och färdiga
// sekvenser och lämnar dem till en `ParserSink` (skärmen, screen.h).
//
// UTF-8 AVKODAS FÖRE MASKINEN, också över en buffertgräns (`feed` kan dela en
// följd var som helst), och en ogiltig följd blir U+FFFD. Kodpunkterna
// U+0080–U+009F (C1 i UTF-8-form) ignoreras — xterm i UTF-8-läge tolkar inga
// 8-bitars C1, och ett program som skickar dem menar sällan det.
//
// TILLSTÅNDEN är maskinens: ground, escape, escape_intermediate, csi_entry,
// csi_param, csi_intermediate, csi_ignore, osc_string, dcs_entry, dcs_param,
// dcs_intermediate, dcs_passthrough, dcs_ignore och sos/pm/apc_string. DCS och
// SOS/PM/APC tolkas till slut och IGNORERAS (DECRQSS, XTGETTCAP, Sixel): de
// når aldrig sänkan. Två avsteg från Williams, båda xterms: `:` i en CSI är en
// UNDERPARAMETER (SGR `38:2::r:g:b`, `4:3`) och inte en väg till csi_ignore,
// och BEL avslutar en OSC precis som ST.
//
// INGEN ALLOKERING PER TECKEN: parametrarna är en fast tabell, mellanteckens
// buffert fyra tecken och OSC-texten en sträng som återanvänds (taket
// `kOscMax` — en längre OSC kapas, resten sväljs).

namespace glaze::term {

// En CSI:s parametrar. Ett TOMT fält är −1 (förvalet), `sub[i]` är sant när
// värdet följde ett `:` (en underparameter till det före). Värdena klämms
// till 65535; fler än `kMax` fält ignoreras.
struct CsiParams {
    static constexpr int kMax = 32;
    int  value[kMax] = {};
    bool sub[kMax]   = {};
    int  count       = 0;

    // Fält `i`, eller `def` när det saknas eller är tomt. `def` gäller också
    // ett uttryckligt 0 när `zeroIsDefault` (markörens rörelser: `CSI 0 A` är
    // ett steg).
    int get(int i, int def, bool zeroIsDefault = false) const
    {
        if (i >= count || value[i] < 0)
            return def;
        return zeroIsDefault && value[i] == 0 ? def : value[i];
    }
};

// Det maskinen hittar. Sänkan anropas synkront ur `Parser::feed`.
class ParserSink {
public:
    virtual ~ParserSink() = default;
    // Ett skrivbart tecken (≥ U+0020, inte DEL).
    virtual void print(char32_t cp) = 0;
    // Ett C0-styrtecken (BEL, BS, HT, LF, VT, FF, CR, SO, SI …).
    virtual void execute(std::uint8_t c) = 0;
    // `ESC <mellantecken> <slut>` — `ESC ( 0`, `ESC 7`, `ESC c`.
    virtual void escDispatch(std::string_view intermediates, char final) = 0;
    // `CSI <prefix> <parametrar> <mellantecken> <slut>` — prefixet är det
    // privata tecknet (`?`, `>`, `<`, `=`) eller 0.
    virtual void csiDispatch(char prefix, const CsiParams& p,
                             std::string_view intermediates, char final) = 0;
    // `OSC <text> BEL|ST` — texten i UTF-8, `bel` = avslutad med BEL (ett svar
    // ska använda samma avslut).
    virtual void oscDispatch(std::string_view text, bool bel) = 0;
};

class Parser {
public:
    static constexpr std::size_t kOscMax = 8192;

    explicit Parser(ParserSink& sink) : sink_(sink) {}

    // Mata in bytes — en del av en ström, delad var som helst.
    void feed(std::string_view bytes);

    // Tillbaka till ground, en halv UTF-8-följd glömd (RIS).
    void reset();

private:
    enum class State : std::uint8_t {
        Ground, Escape, EscapeIntermediate,
        CsiEntry, CsiParam, CsiIntermediate, CsiIgnore,
        OscString,
        DcsEntry, DcsParam, DcsIntermediate, DcsPassthrough, DcsIgnore,
        SosPmApcString,
    };

    void codepoint(char32_t cp);
    void control(std::uint8_t c);
    void clear();
    void collect(char c);
    void param(char c);
    void endOsc(bool bel);

    ParserSink& sink_;
    State       state_ = State::Ground;

    // UTF-8-avkodaren: kodpunkten hittills och hur många följebytes som fattas.
    char32_t      utf_     = 0;
    int           utfNeed_ = 0;
    char32_t      utfMin_  = 0;   // minsta tillåtna värde (överlånga former)

    CsiParams   params_;
    bool        paramsFull_ = false;   // fler än `kMax` fält — resten ignoreras
    char        prefix_     = 0;
    char        inter_[4]   = {};
    int         interLen_   = 0;
    std::string osc_;
};

} // namespace glaze::term
