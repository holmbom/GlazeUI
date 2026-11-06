# Klippmaskerna: en mask per klippyta

Ritytans klipp blir masker (`Glaze/widgets/src/canvas/clip.h`). En mask nycklas på klippets KÄLLA, och källan är
klippets innehåll (`ClipState::content`): rymden och de ritade ytornas nycklar. Lager med samma klippyta delar
därför mask. Mätningen nedan jämför med källan `ClipState::version`, som är unik per sättare, så att varje lager
fick egna masker.

**Maskin och bygge:** Windows 11, Intel UHD Graphics, Debug (`build-test`, `-DGLAZE_TESTS=ON`).

**"Före"** är samma bygge med `version` som källa på de två raderna i `prepareClips` (`clip.cpp`). Det är exakt
det gamla beteendet.

## Serie K (`canvas_perf3d -v`, `--full`)

Upplägget:
- 16 lager, eller 64 med `--full`, i `ClipSpace::World`. Varje lager har en markyta och samma världsklipp med hål.
- 30 kameraändringar med +2° per steg, därefter en stilla bild.
- "3D" är en `Camera3D` i lutning 45°. "2D" är en kartkamera med tilt 45° och vridning; en lutad kartkamera
  ritas i 3D-målet.

| | Lager | Fyllningar per ändring | Maskmål som mest | Maskernas Mpx som mest | Mål skapade (30 ändringar) | Ändring p50 / p90 ms | Genomströmning |
|---|---|---|---|---|---|---|---|
| 3D före | 16 | 32 | 96 | 89,0 | 528 | 5,42 / 12,52 | 84 fps |
| 3D efter | 16 | **2** | **6** | **5,6** | 33 | 0,94 / 1,10 | 170 fps |
| 2D före | 16 | 32 | 96 | 56,0 | 560 | 3,63 / 9,24 | 101 fps |
| 2D efter | 16 | **2** | **6** | **3,5** | 35 | 0,91 / 1,07 | 221 fps |
| 3D före | 64 | 128 | 384 | 356,0 | 2176 | 29,20 / 42,23 | 19 fps |
| 3D efter | 64 | **2** | **6** | **5,6** | 34 | 1,51 / 1,71 | 203 fps |
| 2D före | 64 | 128 | 384 | 224,0 | 2240 | 17,46 / 27,51 | 26 fps |
| 2D efter | 64 | **2** | **6** | **3,5** | 35 | 1,43 / 1,55 | 229 fps |

Maskmålen är BGRA8. 356 Mpx är 1,4 GB, och 5,6 Mpx är 22 MB.

## `terrain_perf --synlig -v --steg N`, med och utan `--klipp`

`--klipp` klipper alla gloria-, vektor-, band-, objekt-, efter- och rymdlager mot samma världspolygon: ±1500 m
med ett hål på ±400 m.

| Steg | | Bilder | Intervall p50 / p95 / p99 / max ms | > 20 ms | Gpu p50 ms | Nya mål per bild | GPU-minne | Processen |
|---|---|---|---|---|---|---|---|---|
| 3 tilt 5°–60° | utan klipp | 134 | 19,0 / 25,5 / 28,4 / 35,7 | 51 | 13,9 | 0,2 | 882 MB | 1340 MB |
| | klipp, före | 158 | 1177 / 2050 / 2454 / 2680 | 157 | 518 | 200,8 | 24 819 MB | 25 622 MB |
| | klipp, efter | 114 | 22,3 / 40,1 / 47,3 / 48,4 | 89 | 14,1 | 1,7 | 1154 MB | 1940 MB |
| 4 vridning under tilt | utan klipp | 251 | 16,3 / 19,5 / 21,8 / 24,5 | 10 | 11,5 | 0,0 | 821 MB | 1312 MB |
| | klipp, före | 0 | blev inte klart på 300 s | — | — | — | — | — |
| | klipp, efter | 251 | 20,8 / 34,1 / 42,4 / 54,6 | 150 | 15,3 | 1,5 | 1083 MB | 2022 MB |
| 5 panorering under tilt | utan klipp | 631 | 9,6 / 19,0 / 21,4 / 24,4 | 25 | 3,4 | 0,0 | 893 MB | 1439 MB |
| | klipp, före | ej mätt (steg 4 blev inte klart) | | | | | | |
| | klipp, efter | 631 | 11,5 / 24,6 / 29,1 / 33,4 | 64 | 5,3 | 0,5 | 945 MB | 1649 MB |

## Efter världsmasken i 3D-målet och gittret som står kvar

I 3D-målet läser 2D-programmen världsmasken i sin världspunkt, så ett världsklipp projiceras aldrig till skärmen för
det som ritas i världen. Världsmaskens gitter följer inte vyn och står kvar så länge det täcker det som behövs i en
täthet inom en oktav. Ett hål klipps längre från ögat än sin ytterring.

**Serie K:** 0 fyllningar per kameraändring, ett enda maskmål och 0 skapade mål, med 16 och med 64 lager. Före var det
32 respektive 128 fyllningar per ändring och 528 respektive 2176 skapade mål.

**`terrain_perf`:**

| Steg | | Intervall p50 / p95 ms | Gpu p50 ms | Nya mål per bild | GPU-minne | Processen | Ofyllda ytor |
|---|---|---|---|---|---|---|---|
| 3 | utan klipp | 21,3 / 28,7 | 14,8 | 0,2 | 854 MB | 1287 MB | 0 |
| 3 | med klipp | 22,7 / 33,6 | 13,9 | 0,9 | 1012 MB | 1571 MB | 0 |
| 4 | utan klipp | 22,3 / 29,1 | 14,3 | 0,0 | 830 MB | 1365 MB | 0 |
| 4 | med klipp | 20,2 / 24,5 | 13,0 | 0,6 | 932 MB | 1466 MB | 0 |
| 5 | utan klipp | 9,9 / 19,5 | 2,9 | 0,0 | 902 MB | 1466 MB | 0 |
| 5 | med klipp | 11,0 / 24,2 | 3,7 | 0,1 | 932 MB | 1528 MB | 0 |

Klippet kostar nu ingen mätbar GPU-tid och omkring 100 MB grafikminne. De mål som fortfarande skapas kommer från:
- himmelslagret, som `--klipp` också klipper: ett helskärmspass ritas i skärmrymden och får klippet projicerat per
  kamerarörelse;
- draperingarna: varje draperad markruta bakar klippet i sin egen upplösning när rutan ritas om.

## Det som stod kvar efter delningen (före världsmasken i 3D-målet)

- **En skärmmask och en världsmask byggs om per kameraändring i lutning.** Världsmaskens gitter följer det synliga
  markområdet, så måttet ändras nästan varje bild och ett nytt mål skapas. Det ger 1,5–1,7 nya mål per bild i steg
  3–4.
- **Klippet kostar fortfarande något.** Med klipp är processen 600–700 MB större och Gpu p50 3,8 ms högre i steg 4.
- **Det projicerade världsklippet går inte att fylla när ytterringen och ett hål båda korsar närplanet.**
  - Orsak: `projectedAreas` (`clip.cpp`) klipper varje ring för sig mot ögat. Hålet hamnar då på ytterringens kant,
    och den strikta trianguleringen vägrar ytan.
  - Följd: loggen säger "en klippyta går inte att fylla", och lagrets platta former (Line, Fill, Glyph, Image)
    klipps bort helt i den bilden.
  - Det gäller före och efter: före en gång per lager och bild (9180 rader i steg 3), efter en gång per bild
    (79–529 rader per steg).
