# doc/os — plattformsgrindarna

GlazeUI utvecklas ofta på EN plattform åt gången, men fasaden och widget-lagret
är gemensamma. Kod som skrivs utan att kunna kompileras eller köras på de andra
OS:en (BLINT skriven kod) är inte klar — den är **ogrindad**. Här bor listan
över vad varje plattform har kvar att grinda, och referensbilderna som visar att
apparna ser likadana ut överallt.

```
doc/os/
├── windows/   TESTS.md + screenshots/   (backend_d3d11)
├── macos/     TESTS.md + screenshots/   (backend_metal)
└── linux/     TESTS.md + screenshots/   (backend_linux)
```

## Så fungerar det

- **Varje plattform äger sin egen `TESTS.md`.** Den som sitter vid en
  Windows-burk bockar av Windows-rader, ingen annan. Rader läggs till av den
  som skriver blint/otestad kod — det är avsändarens ansvar att raden finns,
  och mottagarplattformens ansvar att ta bort den.
- **Rader tas BORT när de är gröna** (inte bockas av och sparas) — filen ska
  läsas som "det här återstår", precis som TODO.md. Blev det ett fel i stället
  för ett godkännande: skriv en 🐛-rad i `/TODO.md` och ta bort raden här.
- **En rad ska gå att utföra utan att fråga:** vad som ändrats, var koden bor,
  vad man gör, och vad som är rätt utfall.
- `/TODO.md` förblir dashboarden för ARBETSSTRÖMMAR. Den här katalogen är
  plattformsgrindar — en 💡-rad i TODO kan peka hit i stället för att räkna upp
  varje detalj.

## Referensbilderna (screenshots/)

Sameness-grinden mellan OS:en går via **glaze-render**, inte via en delad
backend: samma fönster-triplett renderas genom den RIKTIGA backenden på varje
OS, i native scale och med chrome, och är pixelidentisk med den körda appen per
konstruktion.

```sh
./build/apps/glaze-render/glaze-render apps/demo/src/start_window.h \
    --scale 1.0 -o doc/os/<os>/screenshots/start_window_100.png
```

**Namnkonventionen är `<fönster>_<skala×100>.png`** — `start_window_100.png` för
skala 1.00, `_125` för 1.25, `_200` för 2.00. Skalan i namnet gör att den som
jämför ser direkt vad bilden är renderad i, och bilder med SAMMA suffix är de som
ska läggas bredvid varandra: `doc/os/*/screenshots/start_window_100.png`.

**`--scale` tar ABSOLUT skala** (L12/US del 2b — infriar den gamla
önskan): `--scale 1.0` ger scale 1 på alla maskiner, så bilder med
samma suffix har SAMMA pixelmått och geometrin är direkt diffbar mellan
OS:en. Utan flaggan ritas maskinens nativa skala (Linux-referensen är 1.25,
en Retina-Mac 2, en Windows-burk på 100 % ger 1).

**Pixelperfekt är INTE målet — "good enough" är, och Erik är domaren.**
Texten skiljer sig mellan textstackarna, så en byte-diff går aldrig helt
rent; jämförelsen görs med ÖGAT, sida vid sida:

- **Ska se likadant ut:** widgetarnas placering och proportioner, marginaler,
  rundade hörn, färger, skuggor, klippkanter, radbrytningar.
- **Får skilja:** fontrendering (DirectWrite / Core Text / FreeType hintar
  olika), systemets titelrads-knappar, och pixelmåtten i sig (om olika
  skalor renderats).

Ser något uppenbart fel ut — en knapp på fel plats, en ram som saknas, en färg
som inte stämmer — är det en bugg och blir en 🐛 i `/TODO.md`. Små skillnader i
kantutjämning och textvikt är förväntade och ska inte jagas.
