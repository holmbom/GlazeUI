#pragma once
#include <cstddef>
#include <utility>
#include <vector>

#include <gfx/window.h>

#include "glaze/assets.h"
#include "glaze/canvas/image_source.h"

// INTERN header — ingår inte i publika API:t.
//
// IKONENS RAMAR — det OS:et får som `gfx::WindowIcon`:
// samma bild i varje storlek i `glzassets::kIconSizes` upp till ett tak, var
// och en räknad ur den källa valregeln väljer (`pickIconSource` — den lilla i
// sina storlekar, den stora över dem) och inpassad i en kvadrat
// (`fitSquare`). Backenden väljer sedan den storlek OS:et frågar efter och
// skalar aldrig själv. Två konsumenter av `osIcon`: `AppWindow::
// applyWindowIcon` (fönstret — alla storlekar, 16 … 256) och `glaze::
// TrayIcon` (systemfältet — bara upp till 64, för fältet visar en liten
// bild).
//
// KÄLLORNA är `ImageSource`:
//   asset          avkodad (`gfx::decodeImage`) ur appens eller ramverkets
//                  butik
//   Bitmap         räknad ur bildens LEVANDE pixlar — samma form som
//                  `decodeImage` ger (RGBA8, rak alfa, rad 0 överst), så
//                  skalaren tar dem rakt av. En ny version når OS:et först med
//                  ett nytt `setIcon` (det är settern som lägger om ikonen)
//   RenderTexture  AVVISAS: pixlarna finns bara på GPU:n, och en
//                  återläsning hade krävt en renderer utanför en frame — ingen
//                  ram, och EN loggrad per process. Det är regeln för en bild
//                  som inte går att avkoda: fönstret får ingen ikon,
//                  systemfältet förvalet
//
// CACHAD PER PROCESS på (stor, liten, asset-generation, tak) — men BARA när
// båda källorna är assets eller ogiltiga: varje fönster med samma ikon —
// dialogerna, ett andra huvudfönster — delar ramarna i stället för att
// avkoda och skala om, och systemfältets animationsrutor avkodas en gång
// var. Generationen är med för att `loadAssets` kan ge samma id en annan bild
// (`detail::assetGeneration`), och taket för att samma par ger olika listor
// åt fönstret och fältet. Ramar ur en BITMAP cachas aldrig: cachen hade vuxit
// med varje animerad bild, och ett id per bild hade aldrig släppts. Därför
// svarar `iconFrames` med ett VÄRDE. UI-trådens.

namespace glaze::detail {

// Ramarna för `large` (+ `smallIcon`, får vara ogiltig) i varje storlek
// till och med `maxSide`. 256 är den största i `kIconSizes`, alltså alla.
// Tom lista när ingen källa går att avkoda — anroparen loggar inte;
// `gfx::decodeImage` (eller avvisningen av en RenderTexture) har redan sagt
// varför.
std::vector<gfx::IconImage> iconFrames(const ImageSource& large,
                                       const ImageSource& smallIcon,
                                       int maxSide = 256);

// Antalet par i processcachen — GRINDARNAS fråga (icon_gate och
// tray_gate): samma par två gånger ger ingen ny post, ett nytt tak ger en,
// och en Bitmap ger aldrig någon.
std::size_t iconFrameCacheSize();

// FÖRVALET — ramverkets inbäddade GlazeUI-ikon, den stora
// (256) och den förenklade lilla (16), uppslagna EN gång per process. Assets,
// och därför `AssetHandle`.
AssetHandle defaultIconLarge();
AssetHandle defaultIconSmall();

// BILDPARET SOM GÄLLER — en giltig egen bild med sin lilla, annars FÖRVALET.
// EN regel för namnlistens ritning (`AppIcon::effectiveIcons`) och OS-ikonen
// (`osIcon`), så de två aldrig kan visa olika förval.
std::pair<ImageSource, ImageSource> effectiveIcons(const ImageSource& icon,
                                                   const ImageSource& smallIcon);

// OS-IKONEN för ett bildpar (ogiltig `icon` = förvalet): ramarna ur
// `effectiveIcons` till och med `maxSide`, och `programFirst` för FÖRVALET —
// programfilens egen ikon (exe:ns resurs 1, paketets `CFBundleIconFile`)
// vinner då över ramverkets. En egen bild som inte går att avkoda ger en tom
// lista; vad det betyder avgör anroparen (fönstret: ingen ikon, systemfältet:
// förvalet i stället).
gfx::WindowIcon osIcon(const ImageSource& icon, const ImageSource& smallIcon,
                       int maxSide = 256);

} // namespace glaze::detail
