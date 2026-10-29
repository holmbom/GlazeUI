#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "glaze/canvas/geometry.h"   // Length — textstorleken
#include "glaze/draw.h"     // Draw::Align — står i deklarationen nedan
#include "glaze/widget.h"

// TextInput — enradigt textfält: caret, selektion (Shift+pil/musdrag,
// Ctrl+A), Home/End, Backspace/Delete, klippbord (Ctrl+C/V/X), ångra/gör om
// (Ctrl+Z, Ctrl+Y och Ctrl+Shift+Z — se `undo()`) och lösenordsläget (se
// `setPassword`). IME är uttryckligen SENARE. MÖNSTER: widgeten ritar SIG
// SJÄLV — draw()/contentSize() bor i src/textinput.cpp med switch (c.ui),
// och allt BETEENDE (tangenter, mus → teckenindex, scroll, caret) bor där
// också, räknat ur samma privata innerRect() som ritningen (hård regel
// 3). En EGEN widget utanför ramverket ser exakt likadan ut.
//
// Texten är UTF-8; caret/selektion räknas i BYTE men flyttas alltid hela
// teckengränser (åäö och andra flerbyte-tecken raderas/markeras helt).
// Caret-blinket drivs av AppWindows caret-timer (wantsTextInput-kroken) —
// varje blink invalidaterar ENBART caret-recten (partiell omritning),
// appen sover mellan blinkningarna (hård regel 1).

// Fältets DELADE historik är intern — den bor i
// src/text_field.h, och här står bara det de privata deklarationerna nedan
// behöver: typnamnen, inte typerna.
namespace glaze::textfield {
class History;
enum class EditKind : std::uint8_t;
struct Restore;
} // namespace glaze::textfield

namespace glaze {

class TextInput : public Widget {
public:
    // Konstruktorn och destruktorn bor i cpp:n: historiken är en
    // `unique_ptr` till en INTERN typ, och dess raderare kräver hela typen —
    // GCC instansierar den redan i en inline-konstruktor, för
    // undantagsstädningens skull.
    explicit TextInput(std::string t = "");
    ~TextInput() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // Klampar caret OCH selektion mot den nya texten — annars pekar de ut
    // i tomma luften efter en kortare sträng — och PÅ TECKENGRÄNS:
    // en position som var en gräns i den gamla texten kan ligga mitt i ett
    // åäö i den nya, och nästa tecken hade då skrivits in i det och brutit
    // UTF-8. Programmatisk sättning avfyrar ALDRIG onChange eller onCommit
    // (kontraktet gäller alla fält) — och en text som BYTS flyttar
    // commit-baslinjen (se `onCommit`): appens text är inte användarens
    // ändring. Samma text är en no-op också för baslinjen, så en handler som
    // ekar tillbaka det användaren skrev inte äter hens commit.
    //
    // En text som BYTS TÖMMER ÅNGRA-HISTORIKEN (WinForms
    // Text-sättare och Qt:s `setText`): stegen beskrev en annan text. Samma
    // text är en no-op även här, så ekot ovan inte heller äter historiken.
    // Följden att känna till: en `onChange` som SKRIVER OM texten (versaler,
    // formatering) tömmer historiken vid varje tangent — WinForms gör
    // likadant. Inte heller en text som en app sätter i `onDrop` går att
    // ångra: ramverket har inget inbyggt textsläpp och ingen publik
    // insättningsmetod, så den vägen är `setText`.
    void setText(std::string t);
    const std::string& text() const { return text_; }

    // MARKERINGEN, programmatiskt. Indexen är BYTE i UTF-8-texten —
    // samma rymd som `text()` och `caret()` — KLAMPADE till [0, längden] och
    // nedsnappade till en teckengräns, så ett index mitt i ett åäö aldrig
    // delar tecknet. `setSelection(b, e)` lägger ankaret på `b` och careten
    // på `e`; `b > e` är en markering BAKÅT (careten först), och `b == e` en
    // ren caret. Invaliderar, fyrar inga events, och ritas bara när fältet
    // har fokus (som all markering). Den vanliga användningen är
    //     falt.onFocus = [this] { falt.selectAll(); };
    // som fungerar vid både Tab och klick — fokushändelsen postas och körs
    // EFTER klickets caretplacering (widget.h), och ett tryck som GAV fokus
    // blir ett markeringsdrag först bortom stilens `dragThreshold` (och
    // markerar då från trycket), så en darrig hand inte river markeringen.
    // Ingen `setSelectAllOnFocus`: raden ovan ÄR den egenskapen, i båda
    // vägarna (regel 8).
    void selectAll();
    void setSelection(int begin, int end);

    // JUSTERINGEN — samma fält och samma namn som på
    // `Label` (regel 6: delade saker delar namn). Utan den fick en
    // DISPLAY välja mellan högerjustering (`Label`) och att kunna
    // markera/kopiera (`TextInput`), alltså två halva lösningar — och
    // `readOnly` byggdes just för det fall det inte kunde lösa.
    //
    // Meningsfull först när ytan är BREDARE än texten. Svämmar texten
    // över tar redigeringen och scrollen över: justeringens offset är
    // klampad ≥ 0 och blir noll så fort texten inte får plats, så caret
    // och markering fungerar precis som i ett vänsterjusterat fält.
    void setAlign(Align a)
    {
        if (align_ == a)
            return;
        align_ = a;
        invalidate();
    }
    Align align() const { return align_; }

    // Avfyras vid varje ANVÄNDAR-ändring av texten (skrivning, radering,
    // klipp/klistra, ångra/gör om) — läs nya värdet ur sender (text).
    // Programmatisk sättning (`setText`) avfyrar inget.
    Handler<ChangeArgs> onChange;

    // ANVÄNDAREN ÄR KLAR med texten — Enter, eller att fokus lämnar
    // fältet. Fyras BARA när texten ändrats under redigeringen:
    // BASLINJEN sätts när fokus kommer, efter varje commit och av en
    // programmatisk `setText` som byter texten, så Enter på en orörd text
    // fyrar ingenting.
    // Läs värdet ur sender (`text()`), som för `onChange`; när båda fyras för
    // samma handling kommer `onChange` först.
    //
    // ENTER KONSUMERAS INTE — tangenten bubblar vidare (en förfaders
    // `descendantKeyDown`, menyernas acceleratorer och fönstrets standardknapp,
    // `AppWindow::setAcceptButton`), och den som binder
    // Ctrl+Enter till "skicka" får den efter att texten committats. Enter med
    // eller utan modifierare är en commit; numpadens Enter ÄR `Return`
    // (gfx/events.h). I en dialog committar alltså Enter fältet FÖRST och
    // trycker OK sedan — OK:s handler läser den committade texten.
    //
    // FOKUSFÖRLUSTENS commit POSTAS till pump-svansen (`Widget::postEvent`,
    // med livstecken): `focusLost` körs mitt i fokusbytet och får aldrig köra
    // användarkod. Baslinjen flyttas direkt; det är bara eventet som väntar.
    // Ett fönster som tappar OS-fokus committar inte — fokus står kvar i
    // fältet.
    Handler<ChangeArgs> onCommit;

    // Enter utan tangenten (widget.h): en ändrad text committas NU och
    // `onCommit` fyras direkt. `AppWindow::commitEdits()` når den.
    void commitEdit() override;

    // SKRIVSKYDDAT LÄGE: fältet VISAR ett värde men tar inte
    // emot tangenter. Markering, Ctrl+C, Ctrl+A, piltangenter, Home/End,
    // fokus och tabbstopp är ORÖRDA — att kunna markera och kopiera är
    // hela poängen. Det som stängs av är textinmatning, radering,
    // klipp/klistra och caret-blinket.
    //
    // Skild från `disable()` med flit: inaktiv betyder "inte nu",
    // skrivskyddad "aldrig, men läs gärna". Inaktiv dimmar texten, vilket
    // hade sänkt läsbarheten på ett värde vars enda syfte är att läsas.
    // Ytan får därför en egen tyst ton (`Theme::Colors::fieldReadOnly`)
    // medan texten behåller full kontrast.
    //
    // SETTER och inte publikt fält: fältet byter HELA fyllningen (`panel` →
    // `fieldReadOnly`), och en tilldelning på ett redan visat fält sätter
    // ingen dirty-flagga. Då ligger den gamla tonen kvar tills något ANNAT
    // råkar rita om, vilket är precis regel 1:s "tillstånd ändrades men inget
    // invaliderade".
    //
    // `Label::text` är visserligen ett publikt fält med samma svaghet, men
    // prejudikatet åt det här hållet är `Progress`, som är en setter JUST
    // för att bytet ska invalidera. `readOnly` ändrar dessutom BETEENDET
    // (tangenter) och inte bara utseendet — då är en setter rätt val.
    void setReadOnly(bool on);
    bool isReadOnly() const { return readOnly_; }

    // ÅNGRA och GÖR OM — samma steg som Ctrl+Z
    // respektive Ctrl+Y och Ctrl+Shift+Z: tangenterna ANROPAR dem, så det är
    // EN väg. Svaret är true om något ångrades eller gjordes om.
    //
    // Ett STEG är ett ord man skrev ("hej världen" är två: "hej " och
    // "världen"), ett backsteg- eller Delete-tryck i följd, eller en hel
    // inklistring, ett klipp eller en raderad markering. Gruppen bryts av en
    // caret- eller markeringsflytt, Enter, fokusförlust och ett byte av sort
    // — inte av en paus (beteendet beror aldrig på hur fort man skriver).
    // Högst 100 steg och 4 MiB text, det nyaste behålls alltid. Ångra
    // ÅTERSTÄLLER markeringen: skrev man över en markering är den markerad
    // igen efter ångra, och nästa tecken ersätter den, som i varje editor.
    //
    // Båda fyrar `onChange` — det är ANVÄNDARENS redigering som spelas upp,
    // inte appens sättning (`commitEdit()` fyrar `onCommit` av samma skäl) —
    // och `onCommit` följer sin baslinje som vanligt. I ett FOKUSERAT fält
    // konsumeras tangenterna ALLTID, också med tom historik, i skrivskyddat
    // läge och i lösenordsläge (som Ctrl+C utan markering): en Redigera-menys
    // Ctrl+Z tar aldrig över fältets. På macOS är genvägen Cmd (Cmd+Z,
    // Cmd+Shift+Z, utan Ctrl och Option), och `Key` är
    // level-0-symbolen — på tysk layout är Ctrl+Z tangenten som skriver "z".
    //
    // Från en meny eller ett verktygsfält: ingen av dem stjäl fokus
    // (`MenuBar`, `ToolButton`), så
    //     menuBar.setHandler({"Redigera", "Ångra"}, [this] {
    //         if (falt.focused()) falt.undo();
    //     });
    // fungerar, och `canUndo()` släcker posten. Kompositerna (talfältet,
    // datumväljaren, filväljarfältet) har inga vidarebefordrade metoder —
    // tangenterna når deras fält ändå, och det är en skriven gräns.
    bool undo();
    bool redo();
    // Finns det något att ångra/göra om? Alltid false i skrivskyddat läge
    // (historiken står kvar och gäller igen när fältet låses upp) och i
    // lösenordsläge (där finns ingen historik).
    bool canUndo() const;
    bool canRedo() const;

    // LÖSENORDSFÄLTET (WinForms `PasswordChar`,
    // Qt `EchoMode::Password`). Texten VISAS som stilens mask
    // (`Style::passwordMask`, "•") — en per KODPUNKT — medan `text()`,
    // `caret()`, markeringen och `onChange`/`onCommit` talar den RIKTIGA
    // texten: ett grafemkluster av flera kodpunkter (e + U+0301) blir två
    // prickar, men indexen är byte i texten som utan mask.
    //
    // Masken är UTSEENDE och bor i Style, inte på widgeten — ingen
    // `setPasswordChar`. Ett läge och inte en enum (Qt:s `setEchoMode`): bara
    // det här efterfrågas, och en enum kan komma den dag "visa senaste
    // tecknet" behövs.
    //
    // I lösenordsläge är KOPIERA och KLIPP UT avstängda — Ctrl+C och Ctrl+X
    // konsumeras utan att göra något, och klipp ut raderar inte heller (Qt
    // gör likadant: kopiera får inte bli ett sätt att läsa ut lösenordet).
    // Klistra in och Ctrl+A fungerar. Det finns heller INGEN HISTORIK:
    // `setPassword` tömmer den, redigeringar registreras inte, och Ctrl+Z/
    // Ctrl+Y är no-op (Qt: "for security reasons undo is not available in any
    // password mode"). Ett byte i båda riktningarna tömmer den och ritar om.
    void setPassword(bool on);
    bool isPassword() const { return password_; }

    // MAXLÄNGDEN (WinForms `MaxLength`, Qt `maxLength`) —
    // hur många tecken ANVÄNDAREN får skriva, räknat i KODPUNKTER: ett fält
    // för tio tecken tar tio åäö och en emoji är ett tecken (byte hade gett
    // fem åäö, UTF-16-enheter två per emoji, och grafemkluster finns inte i
    // huset — e + U+0301 är två). `0` är obegränsat (förvalet, husets "0 =
    // obegränsat" från `setMinimumSize`); negativt loggas och ignoreras.
    // Gränsen gäller INMATNINGEN — skrivande och inklistring: en insättning
    // kapas på kodpunktsgräns så att den ryms, med markeringen räknad som
    // frigjord, och ryms INGET av en icke-tom insättning händer ingenting
    // (markeringen står kvar). Appens data är inte inmatning: `setText`, en
    // sänkt gräns under en längre text och ångra/gör om kapar INTE — texten
    // står kvar, skrivandet spärras och backsteg fungerar. Lösenordsläget
    // räknar den riktiga texten, alltså en mask per kodpunkt.
    void setMaxLength(int n);
    int  maxLength() const { return maxLength_; }

    // PLATSHÅLLAREN (HTML `placeholder`, WinForms
    // `PlaceholderText`) — en dämpad hint (`textDim`) som ritas när texten är
    // TOM, i fältets justering, kortad med ellips, och bara FÖRSTA raden. Den
    // syns OCKSÅ MED FOKUS, tills man skriver (dialoger fokuserar
    // sitt första fält, och en hint som döljs vid fokus syns aldrig just där;
    // en avvikelse från WinForms med flit). Careten ritas ovanpå. ALDRIG
    // maskerad i lösenordsläge — den är ingen hemlighet. Ett TOKEN som
    // `text` på en etikett, så `setPlaceholder(glaze::tr(id))` följer
    // språket; `placeholder()` är den upplösta texten, `placeholderToken()`
    // identiteten. Ingen användartext och inget `onChange`.
    void               setPlaceholder(TrText t);
    const std::string& placeholder() const { return placeholder_.str(); }
    const TrText&      placeholderToken() const { return placeholder_; }

    // TEXTSTORLEKEN (samma form på `TextArea`, `ListBox` och
    // `Dropdown`) — fältets text i ett EGET mått i stället för stilens
    // `uiFontSize`: `glaze::px(18)` (logiska px, följer skalan som allt
    // annat) eller `glaze::mm(3)` (millimeter på skärmen, som ritytans
    // etiketter — ett fält på en rityta kan ha ritytans textstorlek). `0` är
    // stilens (förvalet). Radhöjden, careten, markeringen, platshållaren och
    // AUTO-höjden följer storleken; luften och kanten är fältets. Ett mått i
    // världsenheter (ritytans kamera finns inte här) och ett negativt loggas
    // och ändrar ingenting. I `.glazeui` ett setter-fält:
    //     falt.setTextSize(glaze::mm(3));
    void   setTextSize(Length size);
    Length textSize() const { return textSize_; }

    bool focusable() const override { return true; }

    // setProp-egenskaper: text (alias title — regel 4-arv) + readOnly +
    // align + password + placeholder + maxLength (ett heltal ≥ 0; annat ⇒
    // false, alltså en opak rad). `textSize` har ingen textform — previewn
    // sätter måttet TYPAT (`bld::applyProp`).
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT platshållare — regel 8:s tr-väg: `x.setPlaceholder(
    // glaze::tr(N));` blir en utförd rad i previewn. `text` tar inget token
    // (`setText` tar `std::string` — användarens text), så den förblir opak
    // där, som i C++.
    bool setTextToken(const std::string& key, const TrText& value) override;

    // Lästillstånd för ritningen och tester. Index i byte, klampade mot
    // textens längd (fältet text är publikt och kan ha ändrats utifrån).
    std::size_t caret() const { return clampIdx(caret_); }
    std::size_t selBegin() const;    // == selEnd() utan selektion
    std::size_t selEnd() const;
    bool hasSelection() const { return selBegin() != selEnd(); }
    bool caretVisible() const { return focused_ && caretOn_ && !readOnly_; }
    float scrollX() const { return scrollX_; }   // horisontell textscroll, logiska px

    // Caretens rektangel i FYSISKA px — blink-damagens yta. Den är en
    // PROJEKTION av den ritade caretBounds (logiska px), inte en egen
    // uträkning: ritningen och damagen kan aldrig glida isär.
    gfx::Rectf caretRect(const Ctx& c) const;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Kroken driver BÅDE textflödet och caret-timern (AppWindow startar
    // den vid fokus när fältet vill ha text) — ett skrivskyddat fält vill
    // ha ingendera.
    bool wantsTextInput() const override { return !readOnly_; }
    void caretBlink(const Ctx& c, bool on) override;
    bool textInput(const char* utf8) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseDrag(const Ctx& c, float mx, float my) override;

    // COMMIT-BASLINJEN: fokus som kommer sätter den, fokus som går
    // committar (postat). En subklass som overridar kroken anropar basen —
    // kompositernas fält gör det, och då händer ingenting så länge ingen
    // satt fältets `onCommit`.
    void focusGained() override;
    void focusLost() override;

    // I-BALKEN över HELA fältet — det är fältets klickyta, och
    // ett skrivskyddat fält går att markera i, så det får den också. Ett
    // inaktiverat frågas inte (AppWindow ger det pilen).
    gfx::PointerShape cursorAt(const Ctx&, float, float) const override
    {
        return gfx::PointerShape::IBeam;
    }

    // TÖM HISTORIKEN OVILLKORLIGT — för en editor som ÅTERANVÄNDS mellan
    // poster (datagridens och egenskapslistans cell-editorer):
    // `setText`s likhetsvakt bevarar historiken när nästa cell råkar ha samma
    // text, och då hade Ctrl+Z i cell B spelat upp redigeringen av cell A.
    // Skyddad och inte publik: en app tömmer med `setText`, och ingen
    // `clearUndo()` behövs för det.
    void resetHistory();

private:
    // Ytan texten ritas i (widget-lokala LOGISKA px): recten minus
    // paddingen; vertikalt används hela höjden (texten centreras i den).
    // BÅDE ritningen och beteende-geometrin (mus → teckenindex, scroll,
    // caret) utgår härifrån — aldrig två sanningar om måtten. Privat: bara
    // den här widgeten använder den, så den hör inte till Draw.
    gfx::Rectf innerBounds(const Ctx& c) const;
    gfx::Rectf caretBounds(const Ctx& c) const;   // careten, logiska px

    // TEXTENS VÄNSTERKANT i widget-lokala LOGISKA px — justeringen OCH
    // scrollen i ETT tal. `drawContent`, `indexFromX`, `caretBounds` och
    // `ensureCaretVisible` läser ALLA den här: en sanning om måtten,
    // aldrig två (mapp-CLAUDE.md:s uttryckliga fallgrop).
    float textOriginX(const Ctx& c) const;
    // Den luft justeringen måste lämna åt careten (dess bredd + stilens
    // `caretMargin`). Egen metod för att `textOriginX` behöver måttet UTAN
    // att fråga `caretBounds`, som i sin tur frågar `textOriginX`. Själva
    // bredden är fältets DELADE (`textfield::caretWidth`, src/text_field.h).
    float caretEdge(const Ctx& c) const;

    // Ctx:t för TEXTEN i fältets storlek (`sizedText` i den interna
    // src/font_set.h) — bara för mätningen och ritningen av text, aldrig för
    // placeringen (`resolve` mäter också syskonen med sitt Ctx).
    Ctx textCtx(const Ctx& c) const;

    // Text + selektion + caret, klippt mot innerytan. Delas av ALLA tre
    // ritningarna — det som skiljer familjerna är ytan runt omkring, inte
    // innehållet, och en kopia per gren hade kunnat glida isär i UTF-8-
    // hanteringen. Selektionens färgPAR skickas in eftersom det ÄR det enda
    // familjerna är oense om (Slate/Glaze: accent + windowBg · Minimalistic:
    // accentSurface + onAccent). Ytan, paret och raden ritas av fältets
    // DELADE kod (src/text_field.h), som det flerradiga fältet också läser.
    void drawContent(const Ctx& c, gfx::Color selFill,
                     gfx::Color selText) const;

    // LÖSENORDETS AVBILDNING — caret och markering bor i den RIKTIGA
    // texten, och bara ritningen och träffen går genom de här tre. `shown`
    // är texten som RITAS: `text_` själv, eller masken i `masked_` (en
    // buffert som återanvänds — ritning är const). `toShown`/`fromShown`
    // översätter BYTE i den riktiga texten till byte i den visade och
    // tillbaka: `toShown(i)` = kodpunkterna före `i` × maskens längd, och
    // `fromShown(j)` = byten där kodpunkt `j / maskens längd` börjar (en hel
    // mask, avrundat nedåt), klampad till texten. En tom mask ger 0.
    const std::string& shown(const Ctx& c) const;
    std::size_t        toShown(const Ctx& c, std::size_t i) const;
    std::size_t        fromShown(const Ctx& c, std::size_t j) const;

    std::size_t clampIdx(std::size_t i) const
    {
        return i < text_.size() ? i : text_.size();
    }
    std::size_t indexFromX(const Ctx& c, float mx) const;
    void        ensureCaretVisible(const Ctx& c);
    // ERSÄTT [b, e) med `ins` — den ENDA väg användaren ändrar texten
    // (TextAreas form): skrivande, radering, klipp, klistra. Registrerar
    // steget i historiken (utom i lösenordsläge) och lägger careten efter
    // det infogade.
    void        replace(std::size_t b, std::size_t e, const std::string& ins,
                        textfield::EditKind kind);
    // Det skrivna eller inklistrade, filtrerat till en rad och KAPAT till
    // maxlängden (`textfield::fitMaxLength`), ERSÄTTER markeringen i ETT anrop
    // — så skrivande över en markering blir ett steg. Ryms inget av en
    // icke-tom insättning ändras ingenting.
    void        insertFiltered(const char* utf8, textfield::EditKind kind);
    // Ångra/gör om utför sitt svar: texten, markeringen ur steget, `edited()`.
    void        restore(const textfield::Restore& r);
    textfield::History& history();         // lat — allokeras vid första steget
    void        sealHistory();             // nästa redigering = nytt steg
    void        showCaret();               // caret synlig + blink-omstart
    void        movedCaret();              // flytt: seal + showCaret
    void        edited();                  // redigering: showCaret + onChange
    // Enter: en ändrad text committas och `onCommit` fyras DIREKT (vi står i
    // händelseflödet, inte i ett fokusbyte).
    void        commitFromUser();

    std::string text_;
    std::string commitBase_;               // texten när redigeringen började
    mutable std::string masked_;           // den visade masken (`shown`)
    TrText      placeholder_;              // hinten i ett tomt fält
    int         maxLength_ = 0;            // kodpunkter, 0 = obegränsat
    Length      textSize_;                 // 0 = stilens
    Align       align_    = Align::Left;
    bool        readOnly_ = false;
    bool        password_ = false;
    // ÅNGRA-HISTORIKEN — LAT: ett läsfält i en komposit betalar
    // ingenting, och i lösenordsläge allokeras den aldrig.
    std::unique_ptr<textfield::History> history_;
    std::size_t caret_   = 0;      // insättningspunkt (byte-index)
    std::size_t anchor_  = 0;      // selektionens fasta ände (== caret_ utan)
    bool        caretOn_ = false;  // blinkfasen (drivs av AppWindow)
    float       scrollX_ = 0.0f;   // logiska px; justeras i draw (ensureCaretVisible)

    // DET HÅLLNA TRYCKET — ett tryck som gav fokus blir ett drag först
    // bortom stilens `dragThreshold` (`mouseDrag`). `pressX_` är tryckets
    // punkt i widget-lokala LOGISKA px, `pressIndex_` dess teckenposition.
    bool        holdPress_  = false;
    std::size_t pressIndex_ = 0;
    float       pressX_     = 0.0f;
};

} // namespace glaze
