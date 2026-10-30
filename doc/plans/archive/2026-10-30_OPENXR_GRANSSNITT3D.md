# OpenXR — 3D-gränssnitt i scenen

**Målet:** byggstenar för gränssnitt som står i scenen — paneler av knappar, vippor, reglage, vred,
handtag och etiketter med text och ikoner — fritt placerade i världen, på en hand eller vid huvudet,
och pekade på med fingret, kontrollens spets eller strålen. Inget utseende eller layout är låst:
layouten är fri, ett rutnät, en rad eller en kolumn, stilen går att byta per gränssnitt, panel och
element, och ett element kan ritas helt av appen.

**Status: klar.** Provat i headset. `interface3d_gate` är skriven men körs först före merge till `main`.

## Gränssnittet (låst)

`OpenXR/include/glaze_openxr/interface3d.h`: `Mount` (`Attach` World/LeftHand/RightHand/PrimaryHand/
SecondaryHand/Head/Custom, `HandFrame`, förskjutning med spegling för vänster hand, mjuk följning),
`ElementStyle`/`PanelStyle`, `Element` (`ElementKind` Button/Toggle/Slider/Dial/Handle/Label,
`Content` med text, standardikon eller bild), `Layout` (Free/Grid/Row/Column, justering, autostorlek),
`Panel`, `Pointers` (fingertopp, kontrollens spets, stråle, närhet, vibration), `ElementEvent`,
`ElementState`, `ElementDrawer`, och `Interface3D` (paneler och element i kollektionsvokabulären,
`setPrimaryHand` — förval höger, så `SecondaryHand` är vänster —, `update` per bildruta, `consumes`).
`Visibility` (`Panel::showWhen`): blicken och den andra handens närhet, med toning.

**Pekaren:** EN per hand, räknad och ritad av gränssnittet — `PointerMode` Auto/Wand/Ray/None
(`setPointerMode`), `PointerState` med läge, geometri, mål (`PointerTarget` Element/Plate/Scene) och
`consumed` (`pointer(hand)`). Auto: staven nära ett element, en grepbar platta eller en yta i scenen,
annars strålen. En hand som bär en synlig panel har ingen pekare. `Session::claimHand(Hand)`: en hand
som gränssnittet tar greppar inte världen i nästa bildruta.

## Steg

1. **`OpenXR/src/interface3d.cpp`:** layouten (cellerna ur rutnät/rad/kolumn med span, autostorlek),
   placeringen (världen, händer med spegling, huvudet med mjuk följning, egen pose), ritningen i
   gränssnittets lager (bakplatta och kroppar som lådor med Placement; framsidan som en texturerad
   yta ur en `glaze::RenderTexture` med text och ikon; en texturyta per element som bara målas om när
   innehållet ändras), pekarna (fingertoppens led, kontrollens spets, strålen och avtryckaren),
   tryckdjupet längs framsidans normal med hysteres, vippor, reglage och vred, handtag som flyttar
   panelen, vibration, `onElement` och `consumes`.
2. **Testappen:** en verktygslåda på vänster hand (ett rutnät med ikoner och text som byter
   förflyttning, en vippa, ett reglage för världsskalan, ett vred) och en panel fast i världen.
   Verktygslådan ligger nästan plant framför handen (lutad 80° framåt) och syns när man tittar dit
   eller för den andra handen nära; appens grepp och markörer läser `pointer(hand)`.

## Prov

Bygget (0 varningar), sedan prov i headset. Grindarna skrivs och körs före merge till `main`.

## Dokumenten

`MANUAL.md` (`openxr`), `OpenXR/CLAUDE.md`, `VERSION.md`.
