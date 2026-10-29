# CLAUDE.md — Glaze/widgets/src/markdown

**Markdown-vyns interna delar**:
TOLKNINGEN av en Markdown-text till ett dokument och LAYOUTEN av dokumentet i
en bredd. Widgeten själv — cacharna, ritningen per familj, länkarna, pekaren,
tooltipen och ankarna — bor i
[../widgets/markdown_view.cpp](../widgets/CLAUDE.md), headern med API:t,
delmängden och de skrivna gränserna i
[../../include/glaze/widgets/markdown_view.h](../../include/glaze/widgets/CLAUDE.md).
INTERNT (`dock/`-prejudikatet): en app har inget ärende hit, och ingen publik
header inkluderar något härifrån — vyn håller dokumentet genom en
`unique_ptr` till en framåtdeklarerad typ. Del av GlazeUI — se
[root /CLAUDE.md](../../../../CLAUDE.md).

## Filer

- **`md_document.h`** — DOKUMENTET som värden, ren std: `Block` (Paragraph,
  Heading med nivå och ANKARE, Code med koden, Quote, List med
  `ordered`/`tight`/`start`/`delim` — punktens tecken eller numrets `.`/`)` —,
  Item, Rule, Table med `align` och `rows` — rad 0 är huvudet, varje rad
  utfylld eller kapad till kolumnernas antal), `Span` (Text med stilbitarna
  `kBold`/`kItalic`/`kCode` och länkens index, Image med alt-texten och
  bildens index, Break = hård brytning) och `Document` med `links` (url,
  titel) och `images` (källan, titel) i den ordning de STÄNGDES.
- **`md_parse.h` / `md_parse.cpp`** — PARSERN, ren std och utan undantag
  (allt som inte blir en konstruktion är text). CommonMark 0.31.2:s
  "Appendix A" i cmarks struktur:
  - FÖRBEHANDLINGEN (`normalize`): CR LF och CR blir LF, NUL och varje
    ogiltig UTF-8-byte U+FFFD — allt efteråt ser giltiga kodpunkter.
  - BLOCKFASEN (`BlockParser`) en rad i taget: de öppna behållarna prövas
    uppifrån (`checkOpenBlocks`: citatets `>`, listpostens indrag, kodens
    prefix och staketets stängning), nya block öppnas (`openNewBlocks` i
    cmarks ordning — citat, ATX, staket, kommentar, setext, linje, listpost,
    indragen kod, sist GFM-tabellen), och texten hamnar i rätt block eller som
    en LAT fortsättningsrad (`addTextToContainer`). Tät/gles räknas i
    `finalize` (`computeTight`); referensdefinitionerna plockas ur styckenas
    början (`extractRefDefs`, den FÖRSTA vinner), och ett stycke som bara bar
    definitioner LÖSES UR trädet men lever i `graveyard_` — `current_` och den
    senast matchade behållaren får peka på det. Tabellen (`tryTable`): en
    avgränsningsrad (minst en `|`) under ett stycke vars SISTA rad har lika
    många celler; styckets tidigare rader står kvar som ett stycke.
  - INLINEFASEN (`InlineParser`) per stycke, rubrik och tabellcell: en
    nodlista där kodspann, escapes, entiteter, autolänkar och kommentarer
    avgörs direkt, medan `*`/`_` (avgränsarstacken) och `[`/`![`
    (hakparentesstacken) väntar. `]` gör en länk eller bild (inline, full,
    kollapsad, genväg) och kör `processEmphasis` ovanför sin hakparentes;
    `processEmphasis` parar med `openers_bottom` per tecken, stängarens längd
    mod 3 och om stängaren kan öppna. Trädet plattas till spans UTAN rekursion
    (`flatten` — tabb och radbrytning blir blanksteg, blanksteg i följd ETT,
    koden behåller sina; grannar i samma stil och länk slås ihop; den INNERSTA
    länken vinner); bildens alt-text är platt (`altText`).
  - `slug` (GitHubs regel), `schemeLength` (`[A-Za-z][A-Za-z0-9+.-]*:` — länkens
    förval och bildernas uppslag läser SAMMA regel) och `linkAction`
    (`#` → Anchor, http/https/mailto skiftlägesokänsligt → Open, allt annat
    None).
- **`md_layout.h` / `md_layout.cpp`** — LAYOUTEN, glaze-bunden (Ctx, fonterna,
  Draw): `build(c, dokument, bilder, bredd)` → `Layout` i LOGISKA px från
  innehållets hörn: rader (`Line` — överkant, höjd, BASLINJE, delarna) SORTERADE
  på överkanten, delar (`Item` — Text, Image, Missing, Marker med font,
  länk, bild och roll), ytor (`Deco` — kodblock, inline-kod per rad,
  citatstreck, linje, punkterna per nivå, celler) och ankare (slug → y).
  `fontFor` är fontens ENDA regel — mätningen och ritningen frågar samma
  funktion. `metricsFor` är cachens nyckel som VÄRDEN (`Metrics`).

## Konventioner och kontrakt

- **EN RAD ÄR DELAR PÅ EN BASLINJE.** Radens höjd = största ascent + största
  nedre del (höjd − ascent) bland delarna OCH strutten (styckets egen font);
  en del står med överkanten på baslinjen minus sin EGEN ascent, en bild med
  sin höjd som ascent. `Draw::text` i en rect lika hög som fontens
  `lineHeight` ritar exakt i överkanten (alla tre backends avrundar
  baslinjen likadant), så blandade fonter står på samma linje utan en ny
  textprimitiv.
- **BRYTNINGEN är ordvis och girig:** bitar = ord + efterföljande blanksteg
  (U+0020 är enda brytpunkten), blanksteg sist på en rad mäts inte, ett ord
  som inte ryms på en TOM rad hårdbryts med `Draw::wrapLines` (en radbruten
  etikettens regel), koden behåller sina inledande blanksteg. Mätningen är
  ADDITIV i alla tre backends, så samma stil i följd slås ihop till EN del.
- **MÅTTEN ÄR STILENS** (`markdown*`, `iconFallback`/`iconGap`,
  `subheadingFontSize`) och fonterna `fontFor`s — inga tal och inga färger
  här. Färgen är widgetens: delen bär sin ROLL (Body/Dim) och sin länk.
- **Tabellen:** naturlig bredd (bredaste cellen på en rad) och minsta
  (längsta ordet), båda med cellens luft; ryms den naturliga summan står den
  så, annars minsta + resten fördelat efter (naturlig − minsta), och ryms
  inte ens minsta går tabellen ut och klipps. Huvudet är fett.

## Fallgropar

- **`kMaxNesting` (32) är ett skydd och ingen regel ur specen:** djupare
  behållare, hakparenteser och adressens parenteser blir text. Utan taket
  ger 1 000 `>` stackspill (`Converter` och layouten är rekursiva över
  blockträdet) — `markdown_gate` P17.
- **`openers_bottom` och backtickindexet håller tolkningen linjär** — en
  ändring i `processEmphasis` eller `findTicks` som börjar om sökningen gör
  20 000 `*a` kvadratiskt (P17 mäter tiden generöst).
- **Ett stycke med bara definitioner är LÖST, inte rivet** (`graveyard_`):
  cmark jämför pekarna efter att det frigjorts, och en riven nod hade
  dinglat.
- **Ankarets slug räknas ur spannens text EFTER blankstegens sammanslagning**
  — "a␣␣b" ger `a-b`, där GitHub ger `a--b`. Procentkodade fragment avkodas
  inte.
- **Fontens storlek är fönstrets** (`FontSet`, `../font_set.h`): en ny stil
  eller skala laddar om, och `Metrics` bär värdena — en nyckel på
  `c.style`-pekaren hade aldrig sett ett stilbyte.

## Grinden

`tests/markdown_gate.cpp`: del P (parsern mot ett HANDSKRIVET AST-facit ur
specens regler — specens exempel är inte inkopierade), del L (layouten
headless mot en inspelande renderer och en stegfont per stil) och del W
(widgeten i ett dolt fönster). Fallen och sabotagen står i filhuvudet.
