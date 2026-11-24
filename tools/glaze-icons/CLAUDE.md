# CLAUDE.md — tools/glaze-icons

**glaze-icons** — konsolprogram som läser glyfrecept ur filer i KÖRTID och
skriver ikonarket som PNG. Till för den som RITAR glyfer: ett recept ändras i
en textfil och arket ritas om på en sekund, utan att appen eller ramverket
byggs om. Del av GlazeUI — se [root /CLAUDE.md](../../CLAUDE.md) för de hårda
reglerna och MANUAL `drawn-icons` för ikonspråket.

```
glaze-icons <recept ...> [-o ark.png] [--only A,B] [--with Save,Open]
            [--zoom N] [--big N] [--list]
glaze-icons --version
```

| Flagga | Betydelse |
|---|---|
| `-o fil` | arkets fil (förval `glyfark.png` i arbetskatalogen) |
| `--only A,B` | bara de recepten (förval: alla, i filernas ordning) |
| `--with S,T` | standardikoner bredvid som jämförelse (namnen i `glaze::StockIcon`) |
| `--zoom N` | arket förstorat N gånger utan utjämning, 1–8 |
| `--big N` | en remsa under arket: varje ikon i färg i N px (32–256), ljust till vänster och mörkt till höger |
| `--list` | receptens namn, en per rad (delar med `@` före), inget ark |
| `--version` | `glaze-icons <GlazeUI-versionen>` |

**Slutkoder:** 0 = arket skrivet · 1 = fel i indata (filen, receptets form,
ett okänt namn i `--only`/`--with`) · 2 = felaktig användning eller rit- och
skrivfel. **Stdout vid OK:** `"<fil>: <b>x<h> px, <n> ikoner"`. Ramverkets
logg går till stderr — också felen i ett recept, som motorn loggar en gång
per ikon med namnet och ordet där felet sitter (`ikonen "X": beskrivningen
går inte att läsa vid ord …`). Ett sådant fel ändrar inte slutkoden: ikonen
ritas så långt receptet gick att läsa.

## Receptfilen

Samma form som i appens kod, så raderna flyttas dit oförändrade:

```cpp
#define KUB "M 12 3 L 19.8 7.5 V 16.5 L 12 21 L 4.2 16.5 V 7.5 Z"

GLAZE_REGISTER_ICON_PART("platta", "R 3 3 18 18 3", "all deep blue gradient blue B 3 3 18 18 3")
GLAZE_REGISTER_ICON("Kub", "@kub", "all deep blue gradient blue F " KUB)
```

- Argumenten är strängliteraler och filens **strängmakron** efter varandra
  (`#define NAMN "text" …`, också med tidigare makron och fortsättningsrader).
  Ett makro med parametrar, eller ett som inte bara är strängar, räknas inte.
- Färgen får utelämnas (två argument).
- Kommentarer (`//`, `/* */`) och makrots egen `#define` hoppas över.
- Delarna registreras FÖRE ikonerna, så en ikon får använda en del som står
  längre ned. Samma namn två gånger: det sista receptet gäller, som i
  `registerIcon`.
- Filen behöver inte vara giltig C++; en `.cpp` med makrona går också. Recept
  som står i en egen tabell (`{namn, kontur, färg}` och en slinga med
  `registerIcon`) läses INTE — skriv dem i makroform i en receptfil.

## Så funkar det (`src/main.cpp`)

1. Läser filerna, tar bort kommentarerna utanför strängarna, samlar
   strängmakrona och sedan varje `GLAZE_REGISTER_ICON[_PART](…)`.
2. `glaze::registerIconPart` och `glaze::registerIcon` — de publika
   funktionerna, samma som makrona anropar.
3. Arket: `glaze::detail::renderIconSheet` (`Glaze/widgets/src/icon_sheet.h`,
   samma bild som `glaze::writeIconSheet` och `tests/glyph_sheet`) — ikonerna
   i 16, 24, 32 och 48, mono och färg, ljust och mörkt, i skärmens skala.
4. `--zoom`: varje pixel blir en ruta på N × N, i programmet.
5. `--big`: ett eget dolt fönster ritar varje ikon med `Draw::glyph` i N px
   (motorn ritar vektorn i rutans fysiska storlek) i ljust och mörkt läge;
   remsan läggs under arket.
6. `gfx::encodePng`.

## När något annat ändras

- **Ikonspråket** (former, färgord, delar) läser verktyget inte själv — det
  lämnar strängarna till motorn. En ändring där kräver ingenting här.
- **Makronas form** (`GLAZE_REGISTER_ICON`, `GLAZE_REGISTER_ICON_PART` i
  `glaze/stock_icon.h`): läsaren i `readRecipes` följer med i samma commit.
- **Arket** (`renderIconSheet`, storlekarna, rutorna): `--big`-remsan och
  texten här och i MANUAL `drawn-icons` ses över.
- **Stdout-raden och slutkoderna** läses av skript utanför repot; ändra dem
  inte utan skäl.
