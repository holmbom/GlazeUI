# Ritytans minneskonto och rumsliga träd

Två ändringar i ritytans motor, mätta före och efter:

- **Kontot** (`Glaze/widgets/src/memory_ledger.h`): ritytans minnesbudget räknar allt ritytan håller, inte bara
  bilderna och draperingarna, och det som inte syns hålls resident bara medan budgeten räcker.
- **Trädet** (`Glaze/widgets/src/canvas/spatial_index.h`, `run_index.h`): ritningen och förberedelsen frågar
  efter det som kan synas i stället för att pröva varje chunk och föremål.

**Maskin och bygge:** Windows 11, Intel UHD Graphics (förvalda kortet), Debug (`build-test`, `-DGLAZE_TESTS=ON`),
dolda fönster.

**"Före"** är koden före ändringarna, med ett undantag: bildcachens konton låg redan i den nya liggaren när den
råa scenen mättes (samma räkning och vräkning som förut — bara bilder och draperingar räknades).

## Många föremål (`canvas_perf3d`, scen G)

99 856 lådor om 2 × 2 × 6 m i ett rutnät över 20 × 20 km, alla med samma enhetsmesh, i ett World-lager. Två serier
om 30 kameraändringar.

| Serie | Rit-anrop som mest | Kameraändring före, median | Efter, median | Genomströmning efter |
|---|---|---|---|---|
| Rakt uppifrån, 400 m över marken | 57 | 211 ms | **2,3 ms** | 168 fps |
| Orbit nära marken (15°) | 19 938 | 271 ms | 145–170 ms | 6–7 fps |

- Rakt uppifrån syns 57 lådor av 99 856. Före gick tiden åt till att pröva varje låda två gånger per bild
  (förberedelsen och ritningen); efter frågas trädet, och bildens innehåll tar 1,7 ms.
- I orbit syns en femtedel av lådorna, och varje låda är ett eget rit-anrop. Det trädet tar bort är prövningen av
  de 80 000 som inte syns; de 20 000 anropen står kvar. Former som delar mesh behöver slås ihop till instansanrop
  för att den serien ska bli snabb.
- En kameraändring laddar upp 0 byte i båda serierna, före och efter.

## Den råa scenen (`terrain_perf --rå`)

Samma två fönster och 27 steg som `terrain_perf`, men med scenen som rådata: terrängen är en mesh om 3 998 792
trianglar över 21 × 21 km (samma handtag i båda fönstren), var och en av de 1 849 stolparna är en egen mesh
(sammanlagt 3 698 000 trianglar, en nivå), punktmolnet är ett moln om 8 000 000 punkter och träden en instansform
om 180 000 träd. Ingenting strömmas in och inga detaljnivåer byts.

Intervallen mellan bilderna, p50 / p95 i ms:

| Steg | Fönster | Före | Efter |
|---|---|---|---|
| 5 panorering 2 km under tilt | kartan | 17,2 / 40,2 | 15,1 / 16,3 |
| 9 redigering (24 000 höjdfrågor per bild) | kartan | 67,8 / 83,9 | 49,5 / 52,1 |
| 12 3D: körning 100 km/h | 3D | 24,2 / 43,2 | 22,1 / 23,5 |
| 13 3D: vridning | 3D | 25,0 / 32,3 | 21,6 / 24,8 |
| 16 3D: musrörelse | 3D | 22,5 / 23,9 | 18,3 / 19,8 |
| 19 appens ändringar, alla objektlager byggs om | kartan | 56,2 / 58,7 | 62,2 / 66,7 |
| 20 appens ändringar, bara det ändrade | kartan | 13,4 / 20,9 | 12,9 / 13,7 |
| 23 båda: körning, alla objektlager byggs om | 3D | 78,9 / 85,2 | 68,1 / 73,8 |
| 25 allt samtidigt, alla objektlager byggs om | båda | 81,6 / 86,3 | 73,3 / 80,2 |
| 26 allt samtidigt, bara det ändrade | båda | 35,4 / 39,1 | 34,5 / 39,1 |

Minnet efter steg 26:

| | Före | Efter |
|---|---|---|
| Processen | 5 474 MB | 4 660 MB |
| GPU (DXGI) | 3 072 MB | 2 605 MB |
| Ritytornas konton | räknades inte | GPU 1 106 MB + CPU 748 MB |

- Budgeten är 1 GiB per rityta. 3D-vyns rådata ryms inte, så det som inte syns vräks och laddas upp igen när det
  syns — därav det lägre GPU-minnet. Sessionen går igenom, och det orörda steget ritar 0 bilder.
- Stegen där appen bygger om alla 79 objektlager varje bild (17, 19, 21) blev 6 ms långsammare på omkring 56 ms.
  Förberedelsen tar 31,3 ms mot 26,5 och ritningen 0,7 mot 2,4: uppladdningen av det som inte syns har flyttat från
  ritningen till förberedelsen, och bokföringen per ombyggt lager (kontot, indexets nyckel, CPU-listan) kostar
  resten. Orsaken är inte isolerad längre än så.
- En rå terrängmesh är ETT föremål: trädet sållar den inte inuti, och hela meshen ritas så snart någon del syns.
  Tiderna för den råa scenen domineras därför fortfarande av GPU:n (fasen `gpu` 20–30 ms). Den blir snabb först
  när ritytan själv delar och förenklar terrängen.

## Den vanliga scenen

`terrain_perf` utan `--rå` (appens egna detaljnivåer och strömning) mättes en gång före ändringarna: allt
samtidigt, bara det ändrade, p50 31,7 / p95 49,6 ms; processen 4 571 MB. Den är inte mätt efter.
