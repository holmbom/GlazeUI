#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "glaze/canvas/geometry.h"   // Length — textstorleken
#include "glaze/widgets/panel.h"

// TextArea — det FLERRADIGA textfältet, ANTECKNINGSFÄLTET (WinForms TextBox med `Multiline`, wx `wxTE_MULTILINE`, HTML
// `<textarea>` — som `TextInput` är `<input>`). Flera rader, radbrytning av
// och på, lodrät rullning, Enter ger en ny rad. MÖNSTER: widgeten ritar SIG
// SJÄLV — draw()/contentSize(), radlayouten och allt beteende bor i
// src/widgets/text_area.cpp (hård regel 3).
//
// EN EGEN WIDGET OCH INTE `TextInput::setMultiline(true)` — beslutet och
// skälet (WinForms bär båda i en klass, Qt/GTK/Cocoa har två):
//   * REGRESSIONSRISKEN. `TextInput` är INREDNINGEN i sex kompositer
//     (talfältet, datum- och färgväljaren, filväljarfältet, datagriddens
//     och egenskapslistans editorer) och i dialogernas fält. En egenskap
//     hade lagt en andra modell — radindex, brytning, lodrät scroll — i
//     varje kodväg de går igenom; som egen widget rörs deras fält inte.
//   * DET ÄR TVÅ MASKINER. En rad med justering och vågrät scroll mot en
//     radlayout med brytning, en caret som står på en RAD och ett
//     sidmått. Det enda de delar är FÄLTET — ytan per familj, markeringen,
//     caretens bredd, UTF-8-gränserna, filtren — och det bor DELAT i den
//     interna `src/text_field.h`, så det inte är kopierat.
//   * RULLNINGEN ÄR PANELS. Fältet ärver `Panel` (ListBox-prejudikatet):
//     overlay-remsorna med tumdrag och rännklick, hjulet och klippet kommer
//     gratis. `TextInput` är en `Widget` och hade inte kunnat ärva dem.
//
// RADINDEX ÄR ETT EGET BEGREPP. Texten är EN sträng där `\n` skiljer
// styckena, och caret/markering räknas i BYTE som i `TextInput` (alltid på
// hela teckengränser — åäö och emoji raderas och markeras hela). De
// VISUELLA raderna är en LAYOUT över strängen: utan brytning ett stycke per
// rad, med brytning `Draw::wrapLines` — SAMMA brytning som en radbruten
// `Label` (ordgräns, för brett ord hårdbryts). En position där ett ord
// HÅRDBRÖTS är både slutet på en rad och början på nästa; careten minns
// vilken sida den står på (End och ett klick bortom radslutet ger den övre).
//
// TANGENTERNA (fokuserat fält):
//   Vänster/Höger      ett tecken (en markering kollapsar till sin kant)
//   Upp/Ner            en VISUELL rad, och x-läget BEVARAS över korta rader
//   PageUp/PageDown    en sida (fältets höjd i rader) — vyn följer med
//   Home/End           radens början/slut (den visuella raden)
//   Ctrl+Home/End      hela textens början/slut
//   Shift + allt ovan  markerar
//   Enter              ny rad (med Ctrl/Alt/Cmd är tangenten inte vår —
//                      Ctrl+Enter förblir en genväg åt appen)
//   Backspace/Delete   ett tecken, eller markeringen
//   Ctrl+A/C/X/V       markera allt / kopiera / klipp ut / klistra in
//   Ctrl+Z             ångra (`undo()`, samma steg och samma
//                      gruppering som i `TextInput`)
//   Ctrl+Y, Ctrl+Shift+Z  gör om (`redo()`)
//   Tab                flyttar FOKUS, som i varje annat fält — med
//                      `setAcceptsTab(true)` skriver den en tabb (Shift+Tab
//                      och Ctrl+Tab flyttar fokus ändå)
// Genvägarna är plattformens, som `TextInput`s: A/C/X/V/Z/Y är Ctrl
// UTAN Alt på Windows och Linux — AltGr är Ctrl+Alt på Windows och skriver ett
// tecken — och Cmd utan Ctrl och Option på macOS (Ctrl+bokstav är inte
// fältets där; `textfield::editShortcut`).
//
// MUSEN: ett tryck placerar careten, ett drag markerar (också över rader),
// ett dubbelklick markerar ORDET (`textfield::wordAt`). Ett drag förbi
// fältets kant RULLAR mot pekaren — vid varje rörelse, för widgets har
// ingen egen timer (talfältets skrivna gräns), så en pekare som står helt
// still utanför kanten rullar inte vidare.
//
// RULLNINGEN: den lodräta remsan syns när texten inte ryms (Panels
// overlay), hjulet RULLAR fältet även utan fokus — det är en rullande
// widget, kontraktet vid `mouseWheel` i widget.h — och bubblar till en
// rullande förälder när kanten är nådd. Careten hålls synlig när DEN
// flyttas (tangent, skrivning, tryck); hjulet får rulla bort från den.
// Utan brytning rullar fältet också i sidled, med en vågrät remsa.
//
// URKLIPPET talar LF åt båda hållen: det som klistras in normaliseras
// (CRLF och ensamt CR blir LF, en tabb blir ett blanksteg — utom med
// `acceptsTab`, där den behålls), och det som kopieras är LF. Fasaden
// översätter till OS:ets form (CRLF på Windows, `gfx/platform.h`).
//
// TABBSTOPPEN: en tabb ritas till nästa stopp,
// var `Style::textAreaTabSpaces` blankstegsbredd räknat från den VISUELLA
// radens början — också en tabb ur `setText`. Den bryter aldrig en rad
// (blanksteget är brytpunkten), och caret, markering och musen talar samma
// stopp. Ersätter "fontens glyf", som var olika per OS och kunde bli "�".
//
// Värden läses ur SENDER (regel 6): `onChange` fyras vid varje ANVÄNDAR-
// ändring av texten; programmatisk sättning fyrar ALDRIG. `onCommit`
// fyras när fokus LÄMNAR fältet och texten ändrats under redigeringen
// — inte vid Enter, som här är en ny rad. Markeringen kan sättas
// programmatiskt (`selectAll`/`setSelection`, `TextInput`s kontrakt).
//
// MAXLÄNGDEN OCH PLATSHÅLLAREN har `TextInput`s namn
// och kontrakt — samma begrepp, samma namn (regel 6): gränsen räknar
// kodpunkter och en radbrytning är en, och hinten ritas radbruten mot
// textens bredd när `wrap` är på (toppställd, klippt mot textytan — en hög
// hint räknas inte i rullningen), annars kortad på första raden.
//
// INTE HÄR — skrivna gränser, inte förbiseenden: IME-komposition,
// justering (`align` — ett anteckningsfält är vänsterställt), egna
// tabbstopp per position (stoppen är jämna), ordhopp med Ctrl+pil och
// Shift+klick (inget av dem finns i `TextInput` heller), "rulla till
// careten" (en programmatisk markering
// rullar inte fältet), lösenordsläge (bara det enradiga fältet har det, som
// WinForms `PasswordChar`) och radfrågor (WinForms `GetLineFromCharIndex`).

// Fältets DELADE historik är intern (src/text_field.h) — här står bara
// typnamnen de privata deklarationerna behöver.
namespace glaze::textfield {
class History;
enum class EditKind : std::uint8_t;
struct Restore;
} // namespace glaze::textfield

namespace glaze {

class TextArea : public Panel {
public:
    explicit TextArea(std::string t = "");
    // Ur headern av `TextInput`s skäl: historiken är en `unique_ptr` till en
    // intern typ.
    ~TextArea() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR: settern är
    // den som INVALIDERAR. LAYOUT-fälten (x/y/width/height/right/bottom/
    // dock/anchors/tabIndex/name) är fortsatt RÅA — se kontraktet i
    // widget.h.

    // TEXTEN — användarens, som i `TextInput`, och därför en `std::string`
    // och inget `TrText`. RADSLUTEN NORMALISERAS: CRLF och ett ensamt CR
    // blir LF, så `text()` svarar med LF-formen (en fil läst på Windows ger
    // samma rader som en läst på Linux). Allt annat lämnas som det är.
    // Caret och markering klampas mot den nya texten, på TECKENGRÄNS.
    // Programmatisk sättning avfyrar ALDRIG onChange eller onCommit, och en
    // text som BYTS flyttar commit-baslinjen och TÖMMER ÅNGRA-HISTORIKEN
    // (`TextInput`s regler — samma text är en no-op för båda).
    void setText(std::string t);
    const std::string& text() const { return text_; }

    // MARKERINGEN, programmatiskt — `TextInput`s kontrakt: BYTE i
    // UTF-8-texten (LF-formen), klampade och nedsnappade till en
    // teckengräns, ankaret på `begin` och careten på `end`. Invaliderar,
    // inga events, och fältet rullar INTE till careten.
    void selectAll();
    void setSelection(int begin, int end);

    // SKRIVSKYDDAT LÄGE — samma kontrakt som `TextInput::setReadOnly`:
    // fältet visar och går att markera och kopiera ur (pilar, Home/End,
    // Ctrl+A/C, musen, fokus, tabbstopp), men tar inte emot text, Enter,
    // radering eller klipp/klistra, och ritar ingen caret. Ytan byter till
    // `fieldReadOnly`; texten dimmas inte. Skilt från `disable()` med flit.
    void setReadOnly(bool on);
    bool isReadOnly() const { return readOnly_; }

    // RADBRYTNINGEN. Namnet är `wrap` och inte WinForms `WordWrap`: regel 6
    // — `Label::wrap` är SAMMA sak (texten bryts mot widgetens bredd, på
    // ordgräns, med samma `Draw::wrapLines`). Default PÅ, som WinForms.
    // Utan brytning är en rad ett stycke och fältet rullar i sidled.
    void setWrap(bool on);
    bool wrap() const { return wrap_; }

    // TAB SKRIVER EN TABB (WinForms `AcceptsTab`).
    // På: Tab utan Ctrl, Alt och Cmd skriver `\t` (ett skrivet steg, en
    // kodpunkt för `maxLength`), och inklistrade tabbar behålls. Shift+Tab
    // och Ctrl+Tab flyttar fokus — Shift+Tab är "tillbaka"
    // överallt, och flikkontrollens Ctrl+Tab rörs inte — och i skrivskyddat
    // läge släpps Tab. Av (förval): Tab flyttar fokus, och en inklistrad tabb
    // blir ett blanksteg. TABBSTOPPEN gäller i båda lägena: en tabb, också ur
    // `setText`, ritas till nästa stopp (`Style::textAreaTabSpaces`
    // blanksteg, räknat från den visuella radens början).
    void setAcceptsTab(bool on) { acceptsTab_ = on; }
    bool acceptsTab() const { return acceptsTab_; }

    // MAXLÄNGDEN — `TextInput::setMaxLength`s kontrakt:
    // KODPUNKTER (en radbrytning är en), `0` obegränsat, negativt loggas och
    // ignoreras, och gränsen gäller INMATNINGEN — skrivande, inklistring och
    // Enter. Ryms inget händer ingenting: Enter vid gränsen ger ingen rad,
    // och tangenten är ändå fältets. `setText`, en sänkt gräns och ångra/gör
    // om kapar inte.
    void setMaxLength(int n);
    int  maxLength() const { return maxLength_; }

    // TEXTSTORLEKEN — `TextInput::setTextSize`s form och regel:
    // `glaze::px(…)` eller `glaze::mm(…)`, 0 = stilens. Raderna, careten,
    // markeringen, tabbstoppen, platshållaren och AUTO-höjden
    // (`heightForRows`) följer; luften, kanten och remsorna är fältets.
    void   setTextSize(Length size);
    Length textSize() const { return textSize_; }

    // PLATSHÅLLAREN — `TextInput::setPlaceholder`s
    // kontrakt: dämpad (`textDim`), bara när texten är TOM, också med fokus
    // tills man skriver, careten ovanpå och ett TOKEN. Radbruten mot
    // textens bredd med `wrap` (toppställd), annars kortad på första raden.
    void               setPlaceholder(TrText t);
    const std::string& placeholder() const { return placeholder_.str(); }
    const TrText&      placeholderToken() const { return placeholder_; }

    // ÅNGRA och GÖR OM — `TextInput`s kontrakt rakt av: samma
    // fyra metoder, tangenterna anropar dem, ett ord eller en följd backsteg
    // är ett steg, och Enter är SKRIVANDE här (en radbrytning är ett
    // blanktecken, så ordet efter den börjar ett nytt steg). Båda fyrar
    // `onChange`, och fältet rullar till careten efteråt. Ingen historik i
    // skrivskyddat läge; `setText` tömmer den.
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // Avfyras vid varje ANVÄNDAR-ändring av texten (skrivning, Enter,
    // radering, klipp/klistra, ångra/gör om) — läs nya värdet ur sender
    // (text).
    Handler<ChangeArgs> onChange;

    // ANVÄNDAREN ÄR KLAR med texten — fokus LÄMNADE fältet och
    // texten ändrades under redigeringen (baslinjen sätts när fokus
    // kommer och av `setText`). POSTAD till pump-svansen, som `TextInput`s
    // fokusförlust; Enter är en ny rad och ingen commit.
    Handler<ChangeArgs> onCommit;

    // Fokusförlustens commit NU och synkront (widget.h) — `AppWindow::
    // commitEdits()` når den.
    void commitEdit() override;

    bool focusable() const override { return true; }

    // setProp-egenskaper: text, readOnly, wrap, placeholder, acceptsTab,
    // maxLength (ett heltal ≥ 0; annat ⇒ false, en opak rad); resten tar
    // Panel (border/clip) och basen. Inget `title`-alias — en ny widget har
    // inga äldre filer.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT platshållare (regel 8:s tr-väg) — `text` tar inget token,
    // den är användarens.
    bool setTextToken(const std::string& key, const TrText& value) override;

    // Lästillstånd för ritningen och tester. Index i BYTE, på teckengräns.
    std::size_t caret() const { return clampIdx(caret_); }
    std::size_t selBegin() const;   // == selEnd() utan markering
    std::size_t selEnd() const;
    bool hasSelection() const { return selBegin() != selEnd(); }
    bool caretVisible() const { return focused_ && caretOn_ && !readOnly_; }

    // Caretens rektangel i FYSISKA px, KLIPPT mot textens yta — blink-
    // damagens yta, och tom när careten är bortrullad. En PROJEKTION av den
    // ritade caretBounds (logiska px), aldrig en egen uträkning.
    gfx::Rectf caretRect(const Ctx& c) const;

    // HÖJDEN FÖR `rows` TEXTRADER i LOGISKA px — raderna plus fältets luft och
    // ramen, så de ryms utan att fältet rullar. AUTO-höjden är den för stilens
    // `textAreaRows` (contentSize frågar hit), och en dialog som ska veta
    // fältets höjd före layouten frågar samma funktion (`inputBox`).
    float heightForRows(const Ctx& c, int rows) const;

protected:
    // AXELREGELN: AUTO-bredden `textAreaDefaultW`, AUTO-höjden
    // `textAreaRows` textrader + fältets luft och ramen — en placerad ruta
    // visar sina rader utan att rulla. Mäts utan layouten (resolve frågar
    // den innan bredden finns).
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Raderna är DATA ⇒ Panels barn-extent räcker inte. FYSISKA px (Panels
    // kontrakt), relativt den oscrollade innehållsytan, MED fältets luft —
    // luften rullar med texten. Med brytning är bredden ytans egen, så den
    // vågräta remsan aldrig aktiveras.
    gfx::Sizef contentExtent(const Ctx& c) const override;

    // Kroken driver BÅDE textflödet och caret-timern — ett skrivskyddat
    // fält vill ha ingendera (TextInput-kontraktet).
    bool wantsTextInput() const override { return !readOnly_; }
    void caretBlink(const Ctx& c, bool on) override;
    bool textInput(const char* utf8) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;

    // COMMIT-BASLINJEN — `TextInput`s krokar.
    void focusGained() override;
    void focusLost() override;

    // REMSA-FÖRST (ListBox-mönstret): ett tryck i en remsa går till Panels
    // tumgrepp/rännklick och rör inte careten; annars placerar det careten
    // och ett drag markerar. Hjulet är Panels (rullar utan fokus, bubblar i
    // kanten) och har ingen override.
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseDrag(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    void mouseDoubleClick(const Ctx& c, float mx, float my) override;

    // I-BALKEN över texten (ett skrivskyddat fält går att markera i), pilen
    // över remsorna — samma geometri som trycket frågar.
    gfx::PointerShape cursorAt(const Ctx& c, float mx, float my) const override;

private:
    // En VISUELL rad: byte-intervallet [b, e) i `text_`, utan radbrytningen
    // och utan ett blanksteg som brytningen åt. `w` är radens bredd i
    // logiska px och mäts bara UTAN brytning, där det vågräta spelrummet är
    // den bredaste radens.
    struct Line {
        std::size_t b = 0;
        std::size_t e = 0;
        float       w = 0.0f;
    };

    // LAYOUTEN — de visuella raderna, CACHADE bakom const-ytan (en lat cache
    // som `TrText`s och tabellens sortering, därav `mutable`). NYCKELN är
    // allt raderna beror på utom texten: fonten (pekaren OCH radhöjden — en
    // omladdad font kan hamna på samma adress), skalan och brytbredden. En
    // ny nyckel ⇒ allt läggs om; en REDIGERING lägger bara om de stycken
    // den rörde (`relayout`), och ritningen besöker bara de synliga raderna.
    // En anteckning på tusentals rader kostar alltså per tangent ett stycke
    // och en förskjutning av radernas index, aldrig hela texten.
    struct Layout {
        const gfx::Font*  font  = nullptr;
        float             scale = 0.0f;
        float             lineH = 0.0f;
        float             wrapW = -1.0f;   // logiska px; < 0 = ingen brytning
        float             tabW  = 0.0f;    // tabbstoppens avstånd
        bool              valid = false;
        std::vector<Line> lines;           // i textordning, minst en
        float             maxW  = 0.0f;    // bredaste raden (utan brytning)
    };

    const Layout& layout(const Ctx& c) const;
    // Ctx:t för TEXTEN i fältets storlek (`sizedText` i den
    // interna src/font_set.h): raderna, tabbstoppen och radhöjden mäts med
    // det, placeringen och remsorna aldrig (`resolve` mäter syskonen).
    Ctx           textCtx(const Ctx& c) const;
    float         wrapWidth(const Ctx& c) const;   // < 0 utan brytning
    // TABBSTOPPENS AVSTÅND i logiska px:
    // `textAreaTabSpaces` (minst 1) blankstegsbredder i fältets font.
    float         tabWidth(const Ctx& c) const;
    // Raderna för [b, e) — alltid HELA stycken — i `out`.
    void          linesFor(const Ctx& c, float wrapW, float tabW, std::size_t b,
                           std::size_t e, std::vector<Line>& out) const;
    // [b, oldEnd) ersattes nyss av [b, newEnd): lägg om de berörda styckena
    // och förskjut resten. Utan ett öppet fönster eller med en annan nyckel
    // ogiltigförklaras layouten i stället (den läggs om vid nästa ritning).
    void          relayout(std::size_t b, std::size_t oldEnd,
                           std::size_t newEnd);

    // ---- GEOMETRIN, EN sanning i widget-lokala LOGISKA px (ritningen läser
    // den rakt av, beteendet projicerar den — Draw::project/localX/localY).
    // Luften till höger: fältets, men aldrig smalare än remsan, så den
    // lodräta remsan aldrig täcker text. Brytbredden räknas ALLTID med den,
    // så texten bryts inte om när remsan dyker upp.
    float       padRight(const Ctx& c) const;
    // Luften under: fältets, och remsans när texten rullar i sidled.
    float       padBottom(const Ctx& c) const;
    // TEXTENS YTA — fältets inre minus luften: det texten, markeringen och
    // careten klipps mot. Luften rullar med texten men täcks aldrig, så en
    // halvt utrullad rad kapas vid luftens kant och inte vid ramen.
    gfx::Rectf  textClip(const Ctx& c) const;
    // Textens ORIGO på skärmen — innehållsytans hörn + luften − scrollen
    // (klampad, så ritningen och remsorna läser samma värde).
    gfx::Pointf origin(const Ctx& c) const;
    gfx::Rectf  caretBounds(const Ctx& c) const;

    // Raden som visar positionen `p`. `upstream` väljer den ÖVRE raden när
    // `p` är en hårdbrytning (slutet på en rad och början på nästa).
    static int  lineAt(const Layout& L, std::size_t p, bool upstream);
    // Är `p` slutet på rad `i` OCH början på nästa (en hårdbrytning)?
    static bool hardWrapAt(const Layout& L, int i, std::size_t p);
    float       xAt(const Ctx& c, const Layout& L, int i, std::size_t p) const;
    std::size_t indexAt(const Ctx& c, const Layout& L, int i, float x) const;
    // Musen (FYSISKA px) → position; `upstream` sätts för radslutet.
    std::size_t indexAtPoint(const Ctx& c, float mx, float my,
                             bool& upstream) const;

    // Text + markering + caret, klippt mot textens yta. Delas av ALLA tre
    // ritningarna — markeringens färgPAR kommer ur fältets delade yta.
    void drawLines(const Ctx& c, gfx::Color selFill, gfx::Color selText) const;

    // ---- BETEENDET
    // Ctx:t för beteende UTANFÖR mus-krokarna — tangenterna och texten får
    // inget av AppWindow (`behaviorCtx()`). Bara i ett ÖPPET fönster
    // (renderer och font skapas i `show()`); annars inget, och då kan inget
    // mätas.
    std::optional<Ctx> openCtx() const;
    void scrollToCaret(const Ctx& c);
    void follow();                    // scrollToCaret när ett Ctx finns
    // Flytta careten (Shift behåller ankaret). `upstream` se lineAt.
    void moveTo(std::size_t p, bool shift, bool upstream = false);
    // Upp/Ner (`page` false) och PageUp/PageDown (`page` true), `dir` ±1.
    bool moveLines(int dir, bool page, bool shift);
    bool moveLineEdge(bool end, bool shift);   // Home/End
    // ERSÄTT [b, e) med `ins` — den ENDA väg användaren ändrar texten:
    // registrerar steget i historiken och går sedan `replaceRaw`.
    void replace(std::size_t b, std::size_t e, const std::string& ins,
                 textfield::EditKind kind);
    // Själva ändringen — texten, careten efter det infogade och den
    // INKREMENTELLA layouten (`relayout`). Ångra går hit direkt: steget är
    // redan registrerat.
    void replaceRaw(std::size_t b, std::size_t e, const std::string& ins);
    void insertText(const std::string& ins,      // ersätter markeringen
                    textfield::EditKind kind);
    void restore(const textfield::Restore& r);   // ångra/gör om utför svaret
    textfield::History& history();    // lat — allokeras vid första steget
    void sealHistory();               // nästa redigering = nytt steg
    void showCaret();                 // caret synlig + blink-omstart
    void movedCaret();                // flytt: seal + showCaret
    void edited();                    // redigering: showCaret + följ + onChange

    std::size_t clampIdx(std::size_t i) const
    {
        return i < text_.size() ? i : text_.size();
    }

    std::string text_;
    std::string commitBase_;         // texten när redigeringen började
    TrText      placeholder_;        // hinten i ett tomt fält
    int         maxLength_ = 0;      // kodpunkter, 0 = obegränsat
    Length      textSize_;           // 0 = stilens
    // ÅNGRA-HISTORIKEN, lat som `TextInput`s.
    std::unique_ptr<textfield::History> history_;
    bool        readOnly_   = false;
    bool        wrap_       = true;
    bool        acceptsTab_ = false;   // Tab skriver en tabb
    std::size_t caret_    = 0;       // insättningspunkt (byte-index)
    std::size_t anchor_   = 0;       // markeringens fasta ände
    bool        upstream_ = false;   // caret på den ÖVRE raden vid en hårdbrytning
    float       goalX_    = -1.0f;   // Upp/Ners x-läge (logiska px); < 0 = inget
    bool        caretOn_  = false;   // blinkfasen (drivs av AppWindow)
    bool        textDrag_ = false;   // trycket togs i texten, inte i en remsa
    // Det HÅLLNA trycket (`TextInput`s): ett tryck som gav fokus blir
    // ett drag först bortom `dragThreshold`. Punkten i widget-lokala
    // LOGISKA px, positionen i texten.
    bool        holdPress_  = false;
    std::size_t pressIndex_ = 0;
    float       pressX_     = 0.0f;
    float       pressY_     = 0.0f;
    mutable Layout layout_;
};

} // namespace glaze
