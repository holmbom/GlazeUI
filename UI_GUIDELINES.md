# UI_GUIDELINES — luft och linjering i GlazeUI-appar

Den här filen är för agenter (och människor) som BYGGER APPAR med GlazeUI och
ska placera kontroller: hur långt mellan knappar, hur mycket marginal i ett
fönster, hur ett formulär linjeras. API:t står i [MANUAL.md](MANUAL.md)
(`layout`, `widget-label`, `app-window`); här står bara VILKA TAL man väljer
och varför. Filen är kort — läs den hel innan du lägger ut ett fönster.

**Riktningen:** macOS luft och Windows stramhet. Mac-apparna ser bättre ut för
att de andas — marginaler på 20 och tydlig luft mellan grupperna. Windows ser
ordnat ut för att allt står i raka kolumner och på ett fast rutnät. GlazeUI
tar båda: **ett 4-px-rutnät, få tal, raka kolumner och hellre för mycket luft
än för lite.** Vi härmar ingen plattform — samma tal gäller på alla tre.

## Kortformen

1. **Använd bara talen på skalan:** 4 · 8 · 12 · 16 · 24 (logiska px).
2. **Aldrig under 8 px** mellan två klickbara kontroller, eller mellan en
   kontroll och en kant som klipper (fönster, panel, flik, ruta).
3. **16 px marginal** i fönster, dialoger och flikar.
4. **8 px** mellan saker som hör ihop, **24 px** mellan grupper som inte gör det.
5. **Sätt aldrig `height`** på knappar, fält, kryssrutor och listrader — höjden kommer ur stilen.
6. **Linjera vänsterkanterna** och gör etiketterna till en kolumn (`setWidthGroup` + `setBuddy`).
7. **Knapparna nere till höger**, 8 px emellan, lika breda, minst 88 px, standardknappen först.

## Varför det blir för tätt — det som ritas UTANFÖR kontrollen

En kontroll ritar mer än sin rektangel. Fokusringen, skuggan och glorian ligger
UTANFÖR den yta layouten ger widgeten, och ramverket reserverar upp till
`Style::overdraw` = **6 px** runt varje widget för dem:

| Effekt | Slate | Glaze | Minimalistic |
|---|---|---|---|
| Fokusring (`focusOffset` + `focusWidth`) | 3 + 2 = **5 px** runt om | 3 + 1 = **4 px** | 2 + 1 = **3 px** |
| Knappens och tummens skugga (`buttonShadowDy`/`Blur`) | — | **~4 px** nedåt | — |
| Etikettens gloria (`setHalo`) | 1,5 px runt texten | 1,5 px | 1,5 px |

Två saker går fel när luften är mindre än så:

- **Glöden lyser över grannen.** Står två knappar 2–4 px isär hamnar den
  fokuserades ring ovanpå grannens kant, och Glazes skugga läggs på raden
  under. Det ser ut som ett fel, och det är det.
- **Ringen kapas av kanten.** `Panel` (förval), `GroupBox`, `TabControl`,
  `Splitter`, `DockPanel` och `Canvas` KLIPPER sina barn. En kontroll på
  `x = 0` i en sådan får en fokusring som tar slut mitt i — halv glöd på ena
  sidan.

Därav golvet: **8 px** till grannen och till en klippande kant. Det rymmer den
största ringen (5 px) med 3 px kvar, så ringen alltid syns hel och aldrig
rör något annat. `GroupBox` lägger sin egen luft innanför ramen (`groupPad`) —
där börjar barnen på `(0, 0)`.

## Skalan

Allt i logiska px; ramverket lägger på DPI och användarskalan. Rutnätet är 4.

| Steg | Px | Används till |
|---|---|---|
| **XS** | 4 | text till text: en beskrivning under sin rubrik, en etikett OVANFÖR sitt fält |
| **S** | 8 | saker som hör ihop: raderna i ett formulär, knappar i en rad, en lista med kryssrutor, etikett → fält i sidled, en kontroll mot en ramad panels kant |
| **M** | 12 | luften i en tät yta: innanför en dockningspanel, en splitterruta eller en ramad `Panel`; mellan två gruppramar |
| **L** | 16 | fönstrets, dialogens och flikens marginal; rubrikblocket → formuläret; formuläret → knappraden |
| **XL** | 24 | mellan grupper som INTE hör ihop och bara skiljs av luft (två rubricerade avsnitt i samma yta); indraget för en beroende kontroll |

Tre regler håller skalan ren:

- **Ett tal utanför skalan behöver ett skäl** — och "det fick plats" är inget
  skäl. Får det inte plats: gör fönstret större, låt det följa innehållet
  (`setSizeToContent`), rulla (`Panel` med rullning) eller dela upp
  (`TabControl`). Krymp aldrig luften.
- **Luft ELLER linje, inte båda fullt.** En `GroupBox` eller `Separator`
  skiljer redan — mellan två ramar räcker 12. Skiljs grupperna bara av luft
  behövs 24.
- **Närhet är betydelse.** Luften säger vad som hör ihop: avståndet INOM en
  grupp ska vara tydligt mindre än avståndet MELLAN grupper (8 mot 24). Lika
  avstånd överallt gör en skärm platt och svårläst — det är det vanligaste
  felet efter trängseln.

Samma tal gäller alla tre ritningarna. I Minimalistic (18 px höga kontroller)
blir det luftigare, och det är meningen — byt aldrig tal efter `ui()`.

## Situationerna

**Fönster och dialoger.** 16 px från innehållsytans kanter till allt innehåll.
Menyraden, verktygsfältet och statusraden räknas inte — de skär sina remsor
själva, och 16 räknas från det som är kvar. En dockad sidopanel (`Dock::Left`)
står kant i kant med fönstret; luften hör till INNEHÅLLET i den (12).

**Formulär.** En etikettkolumn och en fältkolumn:
- Etiketterna `setWidthGroup("form")` (samma bredd, också över flera gruppramar)
  och `setBuddy("<fältets namn>")` (lika höga som fältet, så texten står mitt
  för det i varje stil).
- Fältet `anchorLeft = {"<etikett>", 8}`, sträckt till marginalen
  (`anchors.right = true`, `right = 16`) eller lika brett som de andra fälten.
- Raderna 8 px isär (`anchorTop = {"<raden ovanför>", 8}`).
- En kryssruta eller radioknapp som hör till formuläret står i FÄLTKOLUMNEN,
  inte under etiketterna. En kontroll som beror på den (aktiv bara när rutan
  är ikryssad) dras in **24 px**.
- Ett formulär med få fält och långa etiketter kan ställa etiketten OVANFÖR
  fältet: 4 px från etikett till fält, 12 px till nästa etikett.

**Rubriker.** `Label::Role::Heading` för avsnittets rubrik; en förklarande
`Role::Dim`-text 4 px under; innehållet 8 px under det sista av dem (16 när
rubriken inleder hela fönstret). Fler än två textstorlekar i ett fönster är
fel — det finns bara rollerna, ingen fontstorlek per widget.

**Knapprader.** Dialogens knappar står nere till höger, 16 px från kanterna och
16 px under innehållet, 8 px isär. Ordningen är ramverkets egna dialogers:
**standardknappen först** (OK, Spara), sedan Avbryt längst till höger. Knappar i
samma rad har SAMMA bredd, minst **88 px** (som meddelanderutan); låt en
längre text växa i stället för att kapas. Bara EN standardknapp per fönster
(`setAcceptButton`) — den ritas på accentytan, och två accentknappar tävlar.
Hjälp och sekundära handlingar ("Återställ") står nere till VÄNSTER, på samma
rad. En knapp som hör till ett fält ("…", "Bläddra") står 8 px till höger om
fältet, överkanterna i linje.

**Grupper.** `GroupBox` för två eller fler kontroller som hör ihop; aldrig en
ram runt en enda kontroll, aldrig ramar i ramar, aldrig knappraden i en ram.
12 px mellan två ramar. Ramens rubrik är ett substantiv, utan kolon.

**Listor, träd, tabeller och textytor.** Containrarna har ingen autostorlek:
docka (`Dock::Fill`) eller sträck dem mellan kanterna, med samma marginal som
resten (16 i ett fönster, 12 i en ruta). Rader, kolumner och celler sköter sin
egen luft.

**Verktygsfält, statusrad, menyrad, flikar och dockning.** Ramverkets egna mått
gäller — lägg aldrig till egen luft mellan verktygsknappar eller runt
statusradens fält. Grupper i ett verktygsfält skiljs med en `Separator`.

## Storlekar

- **Höjden kommer ur stilen.** Sätt aldrig `height` på `Button`, `TextInput`,
  `Dropdown`, `SpinBox`, `Checkbox`, `Radio` eller en `Label` — den är 27 px i
  Slate och Glaze men 18 i Minimalistic, och ett spikat tal är fel i två av tre.
- **Bredden:** knappar i en rad lika breda (minst 88 i en dialog); fält i en
  kolumn lika breda eller sträckta till samma kant; ett fält aldrig smalare än
  det som ska skrivas i det (ett postnummer är smalt, en sökväg sträcks).
- **Positionera med grannen, inte med koordinater.** `x = 210` stämmer bara i en
  stil; `anchorLeft = {"föregående", 8}` stämmer i alla (MANUAL `layout`).
- **Låt fönstret följa innehållet** (`setSizeToContent(glaze::SizeToContent::Height)`)
  hellre än att gissa en höjd — en gissad höjd är den vanligaste orsaken till
  att knappraden kläms mot fälten. Luften ovanför knappraden blir då `bottom`
  på den sista raden uppifrån.

## Det du ska tänka på — glöd, effekter och färg

- **Glöden behöver plats.** Fokusring, skugga och gloria ligger utanför
  kontrollen (tabellen ovan). Inget klickbart närmare än 8 px till något annat
  klickbart eller till en klippande kant — annars lyser ringen över grannen
  eller kapas.
- **Glazes knappar kastar skugga nedåt.** Under en knapp eller en slider i
  Glaze behövs minst 8 px innan nästa kontroll, annars ligger skuggan på den.
- **Gloria (`setHalo`) är för text över en BILD** — aldrig för vanlig text
  på fönstrets yta, där den bara blir en suddig kant.
- **En accent.** Accentfärgen bär standardknappen, markeringen och fokus —
  inget mer. Färga inte etiketter eller knappar för att de ska synas
  (`setColor` bara för status, och då i statusrollernas färger). Ser allt
  viktigt ut ser ingenting viktigt ut.
- **Inga egna effekter.** Ingen extra skugga, glöd, gradient eller
  genomskinlighet i appen — ramverkets ritning är utseendet. En egen widget
  ritar innanför `c.bounds()` med temats roller (MANUAL `custom-widget`);
  `setTransparent` är för specialfönster, inte ett utseende.
- **Kontrasten sköter temat.** Rollerna (`Text`, `Dim`, `Accent`) håller
  4,5:1 i alla teman och båda lägena; en hårdkodad färg gör det inte i mörkt
  läge.
- **Pröva i alla tre ritningarna och båda lägena** innan du är klar —
  glaze-render på `.glazeui`-filen med designraden ändrad (`ui=Slate`,
  `ui=Glaze`, `ui=Minimalistic`) visar på en sekund om något krockar.

## Exempel — en dialog efter riktlinjerna

```cpp
class NyKontakt : public glaze::AppWindow {
public:
    NyKontakt()
    {
        setTitle("Ny kontakt");
        setContentSize(420, 240);
        setSizeToContent(glaze::SizeToContent::Height);   // höjden ur innehållet
        setModal(true);

        rubrik.setText("Ny kontakt");
        rubrik.setRole(glaze::Label::Role::Heading);
        rubrik.name = "rubrik";
        rubrik.x = 16;                                    // L: marginalen
        rubrik.y = 16;

        info.setText("Fält med * måste fyllas i.");
        info.setRole(glaze::Label::Role::Dim);
        info.name = "info";
        info.x = 16;
        info.anchorTop = {"rubrik", 4};                   // XS: text till text

        lblNamn.setText("Namn *");
        lblNamn.name = "lblNamn";
        lblNamn.setWidthGroup("form");                    // etikettkolumnen
        lblNamn.setBuddy("namn");                         // lika hög som fältet
        lblNamn.x = 16;
        lblNamn.anchorTop = {"info", 16};                 // L: rubrikblock → formulär

        namn.name = "namn";
        namn.anchors = {true, true, true, false};         // sträckt till marginalen
        namn.anchorLeft = {"lblNamn", 8};                 // S: etikett → fält
        namn.anchorTop = {"info", 16};
        namn.right = 16;

        lblEpost.setText("E-post");
        lblEpost.name = "lblEpost";
        lblEpost.setWidthGroup("form");
        lblEpost.setBuddy("epost");
        lblEpost.x = 16;
        lblEpost.anchorTop = {"lblNamn", 8};              // S: rad till rad

        epost.name = "epost";
        epost.anchors = {true, true, true, false};
        epost.anchorLeft = {"lblEpost", 8};
        epost.anchorTop = {"namn", 8};
        epost.right = 16;

        nyhetsbrev.setText("Skicka nyhetsbrev");
        nyhetsbrev.name = "nyhetsbrev";
        nyhetsbrev.anchorLeft = {"lblEpost", 8};          // i fältkolumnen
        nyhetsbrev.anchorTop = {"epost", 8};
        nyhetsbrev.bottom = 16;                           // L: luften till knappraden

        spara.setText("Spara");
        spara.width = 88;                                 // lika breda, minst 88
        spara.anchors = {false, false, true, true};       // nere till höger
        spara.right = 112;                                // 16 + 88 + 8: vänster om Avbryt
        spara.bottom = 16;
        spara.setDialogResult(glaze::DialogResult::OK);

        avbryt.setText("Avbryt");
        avbryt.width = 88;
        avbryt.anchors = {false, false, true, true};
        avbryt.right = 16;
        avbryt.bottom = 16;
        avbryt.setDialogResult(glaze::DialogResult::Cancel);

        setAcceptButton(spara);                           // EN standardknapp
        setCancelButton(avbryt);

        root().adopt(rubrik);
        root().adopt(info);
        root().adopt(lblNamn);
        root().adopt(namn);
        root().adopt(lblEpost);
        root().adopt(epost);
        root().adopt(nyhetsbrev);
        root().adopt(spara);
        root().adopt(avbryt);
    }

private:
    glaze::Label     rubrik, info, lblNamn, lblEpost;
    glaze::TextInput namn, epost;
    glaze::Checkbox  nyhetsbrev;
    glaze::Button    spara, avbryt;
};
```

I `.glazeui` är det samma tal: layoutfälten är råa fältrader
(`namn.anchorLeft = {"lblNamn", 8};`, `nyhetsbrev.bottom = 16;`) och resten
settrar och fönsteranrop (MANUAL `glazeui-grammar`).

## Underlaget

Talen är vägda ur plattformarnas egna riktlinjer, inte kopierade — GlazeUI
ritar själv och ska se likadant ut överallt.

| | Marginal | Relaterade | Oberoende / grupper | Knappar emellan | Etikett → fält |
|---|---|---|---|---|---|
| Windows (Win32 UX, 96 dpi) | 11 | 7 | 11 | 7 | 5 |
| Windows (WinUI/Fluent, 4-px-rutnät) | 16 | 8 | 12 | 8 | 12 |
| macOS (Aqua-guiderna) | 20 | 6–8 | 12–24 | 12 | 6 |
| GNOME (6-px-steg) | 12 | 6 | 18 | 6 | 12 |
| **GlazeUI** | **16** | **8** | **24** (12 med ram) | **8** | **8** (4 ovanför) |

- Microsoft: [Content layout and spacing (Windows apps)](https://learn.microsoft.com/windows/apps/design/basics/content-basics),
  [Win32 Layout Specifications](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bb226818(v=vs.85))
  och Windows UX-guidens avståndstabell (DLU → px vid Segoe UI 9 pt).
- Apple: macOS-avstånden sammanställda i
  [Mario Guzmáns macOS Layout Guidelines](https://marioaguzman.github.io/design/layoutguidelines/)
  (Apples HIG anger inga tal längre, bara `layoutMarginsGuide`).
- GNOME: [HIG Layout](https://wiki.gnome.org/Design/HIG/Planning/Layout).
- Fokusringens, skuggans och `overdraw`-måttens värden: `Glaze/widgets/src/style.cpp`.
