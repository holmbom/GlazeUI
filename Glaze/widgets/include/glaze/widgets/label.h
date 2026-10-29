#pragma once
#include "glaze/draw.h"     // Draw::Align — står i deklarationen nedan
#include "glaze/widget.h"

// Label — enklaste inbyggda widgeten och den kortaste mallen att kopiera.
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/label.cpp med switch (c.ui) och ritar via c.r + de delade
// idiomen i glaze/draw.h (hård regel 3). En EGEN widget utanför ramverket
// ser exakt likadan ut: kopiera den här filen och src/label.cpp.

namespace glaze {

class Label : public Widget {
public:
    explicit Label(TrText t = {}) : text_(std::move(t)) {}

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // TEXTEN ÄR ETT TOKEN — `TrText` bär ANTINGEN
    // en literal ELLER ett översättnings-id, och `TrText(const char*)` är
    // implicit, så `setText("Hej")` kompilerar oförändrat. Det som
    // TILLKOM är `setText(glaze::tr(1042))`.
    //
    // GETTER-PARET är additivt, och gränsen mellan de två är hela
    // poängen:
    //   `text()`      den UPPLÖSTA texten — OFÖRÄNDRAD signatur och
    //                 OFÖRÄNDRAD betydelse. All kod som konkatenerar,
    //                 jämför eller kopierar den beter sig som med en vanlig sträng.
    //   `textToken()` IDENTITETEN — det writern emitterar och det
    //                 menystigen matchar på. Ett id är inte lika med en
    //                 literal, ens när de löser upp till samma text.
    //
    // JÄMFÖRELSEN i settern är därför TOKENjämförelse: `setText(tr(5))` på
    // en widget som redan visar samma STRÄNG är en riktig ändring, för
    // identiteten blir en annan och texten följer språket.
    //
    // En text kan ändra en BREDDGRUPPS bredd (`setWidthGroup`), och då ritas
    // hela fönstret om — `sizeMayChange`.
    void setText(TrText t)
    {
        if (text_ == t)
            return;
        text_ = std::move(t);
        sizeMayChange();
    }
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // Textfärgen är en ROLL som följer aktivt tema (byts temat byter
    // texten färg). Custom är undantaget: en fast färg i `color`
    // (setProp: color=#rrggbb — rollerna heter text/dim/accent där).
    // Titlebar = chromets titeltext (TopBars titel-label — följer temats
    // titlebarText i stället för vanliga textfärgen).
    // Heading = RUBRIKEN: samma mekanism en gång till, fast för STORLEK.
    // Rollen sa redan VAD texten är och avgjorde därigenom färgen; att
    // låta den avgöra storleken också (`Style::headingFontSize`) gör
    // typografisk hierarki möjlig UTAN att ett tal hamnar i appkoden.
    // Det är en tillämpning av regel 3, inte ett undantag från den —
    // ett `fontSize`-fält på widgeten hade brutit den rakt av, och en
    // fri multiplikator hade gjort mängden fontstorlekar öppen (se
    // Ctx::headingFont). Färgen är vanlig `text`: en rubrik skiljer sig
    // genom STORLEK, inte genom att vara accentfärgad.
    //
    // ENDAST Heading införs. `Small`/`Caption` är gissningar och hittas
    // inte på i förväg — behövs något mer blir det en roll till, vilket
    // är billigt att lägga till.
    enum class Role { Text, Dim, Accent, Titlebar, Heading, Custom };

    void setRole(Role r)
    {
        if (role_ == r)
            return;
        role_ = r;
        sizeMayChange();   // rollen bär storleken — se `setText`
    }
    Role role() const { return role_; }

    // Fast färg — och den AKTIVERAR sig själv genom att sätta `Custom`.
    //
    // Att `setColor` sätter rollen själv gör att färgen syns direkt. Annars hade
    // den först synts efter ett ANDRA anrop (`setRole(Role::Custom)`) — en
    // fälla: man satte en färg, ingenting hände, och felet var ett anrop som
    // ingenting pekade på.
    //
    // Textvägen (`parseColor`s `#rrggbb`-gren) ärver den ordningen i stället
    // för att upprepa den: färgen ska vara på plats när Custom slår till, och
    // den ordningen bor i EN operation, här.
    //
    // FÖLJDEN FÖR VERKTYGSVÄGEN (regel 8): `applyCall(setColor)` anropar
    // ENDAST `setColor`, så previewn gör exakt vad C++-metoden gör — och
    // färgen SYNS i båda. Att previewn satte rollen extra hade gjort den
    // rikare än appen, vilket är ett regel 8-brott i motsatt riktning.
    // rikare än appen, vilket är ett regel 8-brott i motsatt riktning.
    //
    // Vill man byta färg UTAN att aktivera den finns ingen väg, och det är
    // avsiktligt: en osynlig färg är inget tillstånd någon efterfrågat.
    // Byt tillbaka till en temaroll med `setRole()` som vanligt.
    void setColor(gfx::Color c)
    {
        const bool sammaFärg = color_.r == c.r && color_.g == c.g &&
                               color_.b == c.b && color_.a == c.a;
        if (sammaFärg && role_ == Role::Custom)
            return;
        color_ = c;
        role_  = Role::Custom;
        invalidate();
    }
    gfx::Color color() const { return color_; }

    // SIDLEDS-justeringen inom widgetens yta (höjdled: `setVAlign` nedan).
    // `Draw::text` har tagit den som defaultad parameter hela tiden, men
    // Label anropade den utan argument — så varje sifferkolumn och varje
    // högerställd etikett i ett etikett/fält-par krävde en x-position
    // uträknad för hand, alltså ett spikat tal i appkoden.
    //
    // MENINGSFULL bara när ytan är STÖRRE än texten: en auto-etikett är
    // exakt så bred som sin text, så justering kräver en satt `width`,
    // ett dubbelankare eller en dockad remsa. Det är inte en brist —
    // det är axelregeln.
    void setAlign(Align a)
    {
        if (align_ == a)
            return;
        align_ = a;
        invalidate();
    }
    Align align() const { return align_; }

    // RADBRYTNING. Av som förval: en etikett är EN rad
    // och rinner ut ur sin yta. På: texten bryts mot widgetens BREDD på
    // ordgräns, `\n` bryter alltid och ett för brett ord hårdbryts på
    // UTF-8-teckengräns (`Draw::textWrapped` → `gfx::wrapText`).
    //
    // HÖJDEN FÖLJER BREDDEN:
    // `Widget::resolve()` frågar `contentSize()` innan den räknat fram en
    // sträckt eller dockad slutbredd, men den frågar därefter kroken
    // `heightForWidth` med den bredd layouten GAV — och etiketten svarar
    // med höjden för sina brutna rader. Tabellen:
    //
    //   wrap == false                 standardbeteendet, oförändrat
    //   satt `height`                 vinner (axelregeln)
    //   satt `width`                  höjden ur den
    //   STRÄCKT (båda sidokanterna    höjden ur den bredd layouten gav —
    //     bundna) eller Top/Bottom-   också när förälderns bredd ändras
    //     DOCKAD                      (en delare dras, fönstret växer)
    //   Fill / Left / Right / i en    ytan ger höjden — texten bryts mot
    //     Bar                         bredden och KLIPPS mot den egna kanten
    //   ingen bredd, inte sträckt     EN rad: etiketten är då så bred som
    //                                 texten, och inget finns att bryta mot
    //   barn till en komposit med     räknas som EN rad — kompositens
    //     autohöjd ur barnen          `childrenExtent` har ingen bredd än
    //                                 (skriven gräns)
    //
    // `width`, brytbredden och den mätta höjden är LOGISKA px
    // (mätkontraktet) — `Draw::measureWrapped` får bredden rakt av, och
    // ritningen bryter vid exakt samma flyttal (`c.bounds().w` är widgetens
    // fysiska bredd delad med skalan, precis som kroken frågas med).
    //
    // KLIPPET står kvar där ytan och inte texten ger höjden (Fill, Bar): där
    // hade raderna annars ritats rakt ut över syskonen, som varken känner
    // till dem eller invalideras av dem. Klippt syns felet där det hör
    // hemma, och sättet är detsamma som Dropdowns fälttext använder när den
    // inte får plats.
    void setWrap(bool on)
    {
        if (wrap_ == on)
            return;
        wrap_ = on;
        invalidate();
    }
    bool wrap() const { return wrap_; }

    // KORTNINGEN: på kortas EN rad med "…" vid
    // etikettens bredd (`Draw::textElided`, teckengräns), och HELA texten
    // blir tooltip när den faktiskt kortats — en egen `setToolTip` vinner.
    // Med `wrap` gäller brytningen och `elide` ignoreras. Meningsfull bara
    // när bredden kommer utifrån (satt, sträckt, dockad): en auto-etikett är
    // så bred som sin text och kortas aldrig.
    void setElide(bool on)
    {
        if (elide_ == on)
            return;
        elide_ = on;
        invalidate();
    }
    bool elide() const { return elide_; }

    // HÖJDLEDS-justeringen inom widgetens yta. Center (förval) är den vanliga
    // bild bit för bit; Top/Bottom lägger textblocket — en rad, eller de
    // brutna raderna med `wrap` — mot över- respektive underkanten.
    void setVAlign(VAlign v)
    {
        if (vAlign_ == v)
            return;
        vAlign_ = v;
        invalidate();
    }
    VAlign vAlign() const { return vAlign_; }

    // GRANNEN (filväljarens `Buddy`-mall gjord publik): AUTO-höjden blir
    // minst ett SYSKONS MÄTTA höjd, så "Namn" står mitt för fältet bredvid i
    // varje UI och stil. Grannen slås upp bland syskonen på `name` (som ett
    // syskonankares `to`), en dold granne räknas, och en satt `height`
    // vinner. En LABEL som granne ignoreras — två etiketter kan inte peka på
    // varandra, så cykeln går inte att uttrycka. Ett namn som inte finns ger
    // egen höjd, tyst (mätningen loggar aldrig). Tom = ingen granne.
    void setBuddy(std::string name)
    {
        if (buddy_ == name)
            return;
        buddy_ = std::move(name);
        invalidate();
    }
    const std::string& buddy() const { return buddy_; }

    // GEMENSAM BREDD FÖR EN GRUPP ETIKETTER (en
    // formulärkolumn): etiketter med samma GRUPPNAMN får den bredaste
    // medlemmens bredd som sin AUTO-bredd, så fälten som ankras efter dem
    // (`falt.anchorLeft = {"lblNamn", 8}`) står i linje i varje UI, stil och
    // språk — gruppens bredd räknas ur samma `Draw::measure` som allt annat,
    // och appen mäter ingenting. Med `setAlign(Align::Right)` blir det en
    // högerställd etikettkolumn. Tomt (förvalet) = ingen grupp.
    //   · En SATT `width` vinner för etiketten själv (axelregeln) men räknas
    //     in i gruppen — en medlem med satt bredd ger kolumnen ett golv.
    //   · RÄCKVIDDEN är fönstrets träd (roten via `parent()`), så en grupp
    //     spänner över flera föräldrar — en `GroupBox` per sektion. Två
    //     oberoende formulär i samma fönster tar olika namn. Utan fönster
    //     (GlazeBuilders preview, en fristående komposit) är roten trädets topp.
    //   · DOLDA MEDLEMMAR RÄKNAS INTE — en medlem vars kedja till roten inte
    //     är synlig (en dold flik i en `TabControl` döljs, en inaktiv
    //     dockningsflik gör det inte). Den egna etiketten räknas alltid, så en
    //     dold medlem som är ankarmål får gruppens bredd för sitt hål. Vill
    //     appen ha en kolumn som står still när en sektion visas, ger den en
    //     medlem en satt bredd.
    //   · Varje medlem mäts med SIN roll (en rubrik med `headingFont`) och som
    //     EN rad — lägg inte brödtext med `wrap` i en kolumngrupp.
    //   · OMRITNINGEN: en medlems text eller roll ändrar hela gruppens bredd,
    //     så `setText`, `setRole` och `setWidthGroup` ritar om HELA fönstret
    //     när etiketten har en grupp, och likaså när en medlem visas eller
    //     döljs (`shownChanged`). Språk- och stilbyten ritar redan om allt.
    //     SKRIVEN GRÄNS: `adopt`/`unadopt` av en grupperad etikett utanför en
    //     handler ritar inte om de andra medlemmarna — samma regel som för
    //     ett layoutfält: `invalidate()` på fönstret.
    //   · KOSTNADEN: en grupperad etikett söker igenom trädet och mäter
    //     medlemmarnas texter vid varje mätning; en etikett utan grupp kostar
    //     en jämförelse.
    void               setWidthGroup(std::string grupp);
    const std::string& widthGroup() const { return widthGroup_; }

    // GLORIAN — en kontur runt texten i en egen färg, som ritytans
    // `TextStyle::halo`: text över en bild med både ljusa och mörka partier
    // läses då överallt. `std::nullopt` (förvalet, eller `{}`) ritar ingen.
    // Glorian ritas i alla tre textlägen (en rad, `wrap`, `elide`) och
    // följer texten i färg och läge, men inte rollen — den är en egen färg.
    // Den sticker UTANFÖR etikettens yta (den växer inte med glorian), och
    // en genomskinlig färg staplas där de förskjutna kopiorna överlappar:
    // ge den full alfa. OBS: `setColor({})` är SVART (`gfx::Color{}`) men
    // `setHalo({})` är INGEN gloria — settern tar en valfri färg
    // (Buttons `setColor`s form). I `.glazeui`:
    // `lbl.setHalo(gfx::Color{255, 255, 255});` · `lbl.setHalo(std::nullopt);`.
    void setHalo(std::optional<gfx::Color> col)
    {
        if (halo_ == col)
            return;
        halo_ = col;
        invalidate();
    }
    std::optional<gfx::Color> halo() const { return halo_; }

    // GLORIANS BREDD i LOGISKA px — 0 (förvalet) är stilens
    // `labelHaloWidth`. Vid ritning begränsas den till stilens `overdraw`
    // (marginalen damage-ytan räknar med): en bredare gloria hade lämnat
    // spökpixlar vid partiell omritning. Negativt ⇒ logg + no-op.
    void  setHaloWidth(float px);
    float haloWidth() const { return haloWidth_; }

    // setProp-egenskaper: text (alias title — äldre kod, regel 4-arv) +
    // color (roll eller #rrggbb) + align (left/center/right) + wrap
    // (true/false) + elide (true/false) + vAlign (top/center/bottom) + buddy
    // (ett syskons namn) + widthGroup (gruppnamnet) + haloWidth (ett tal);
    // resten tar basen. Glorians färg har ingen textform:
    // `setHalo(std::optional)` är en anropssats.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT text — regel 8:s tr-väg. Utan den blir
    // `setText(glaze::tr(id))` en OPAK rad i previewn.
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text" || key == "title") { setText(value); return true; }
        return Widget::setTextToken(key, value);   // basens toolTip
    }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // HÖJD FÖR BREDD (widget.h) — höjden för de brutna raderna vid `width`
    // logiska px. `nullopt` utan `wrap`, med en satt `height` (den vinner)
    // och för en bredd ≤ 0 (en bredd på noll hade gett ett tecken per rad och
    // sprängt höjden). `contentSize` frågar samma krok för en SATT bredd, så
    // de två aldrig kan glida isär.
    std::optional<float> heightForWidth(const Ctx& c,
                                        float width) const override;

    // HELA TEXTEN som tooltip när `elide` faktiskt kortat den — jämfört
    // med `Draw::elide`, samma sanning som ritningen. En egen tooltip vinner.
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

    // En medlem i en BREDDGRUPP som visas eller döljs ändrar gruppens bredd
    // (hela fönstret ritas om).
    void shownChanged() override;

private:
    gfx::Color roleColor(const Ctx& c) const;   // roll -> temafärg
    gfx::Font* roleFont(const Ctx& c) const;    // roll -> fönstrets font
    Ctx        roleCtx(const Ctx& c) const;     // kopia med rollens font
    // Grannens MÄTTA höjd i logiska px, 0 utan granne. Körs i
    // `resolve()` och loggar därför aldrig.
    float      buddyHeight(const Ctx& c) const;
    // Glorians bredd i logiska px vid ritning: den satta eller stilens,
    // begränsad av `overdraw`; 0 utan gloria.
    float      haloWidthAt(const Ctx& c) const;
    // BREDDGRUPPEN, logiska px: min bredd UTAN grupp (satt
    // `width`, annars texten mätt med rollens font som en rad) och gruppens
    // bredd (den bredaste synliga medlemmens, jag själv alltid med). Körs i
    // `resolve()` och loggar därför aldrig.
    float      naturalWidth(const Ctx& c) const;
    float      groupWidth(const Ctx& c) const;
    // Mitt mått kan ha ändrats: med en grupp och ett fönster ritas hela
    // fönstret om (gruppens bredd och fälten ankrade mot medlemmarna följer),
    // annars bara jag.
    void       sizeMayChange();

private:
    TrText      text_;
    Role        role_ = Role::Text;
    gfx::Color  color_{};   // används bara när role_ == Custom
    Align       align_ = Align::Left;
    bool        wrap_  = false;
    bool        elide_ = false;
    VAlign      vAlign_ = VAlign::Center;
    std::string buddy_;
    std::string widthGroup_;
    std::optional<gfx::Color> halo_;   // nullopt = ingen gloria
    float       haloWidth_ = 0.0f;     // 0 = stilens `labelHaloWidth`
};

} // namespace glaze
