#pragma once
#include <cstdint>
#include <string>

// Asset-API:t — det appen ser av assets.bin.
//
//   glaze::loadAssets();                        // i main, FÖRE run()
//   okBtn.setImg(glaze::img("toolbar", "ok"));  // handtag, inte textur
//
// FRIA funktioner med flit (inte App-metoder): glaze::app() är lat och
// App::App() kör gfx::init() — ett uppslag i genererad
// initializeComponent() får inte konstruera App i förtid. Lagringen är
// en fristående Meyers-singleton (samma precedens som App::themeState)
// som äger RÅBYTES; ingen renderer behövs. Texturen avkodas lazily PER
// FÖNSTER (ImageCache, nås via Ctx) — gfx::Image följer fontens
// livstidsregel och får aldrig ägas av en widget eller av App.
//
// Saknad/trasig fil och okända nycklar är TYSTA no-ops i ritvägen
// (ogiltigt handtag ⇒ ingen bild) men aldrig osynliga: loadAssets loggar
// vald sökväg/orsak, img() loggar FÖRSTA missen per nyckel.

namespace glaze {

// Handtag till en asset — 0 = ogiltigt ⇒ default är "ingen bild".
// Pekar på en LOGISK asset (kan senare bära @1x/@2x-varianter utan
// API-brott); värdesemantik, trivialt kopierbart, säkert i genererad kod.
struct AssetHandle {
    std::uint32_t id = 0;
    bool valid() const { return id != 0; }
    explicit operator bool() const { return valid(); }
    friend bool operator==(AssetHandle a, AssetHandle b) { return a.id == b.id; }
};

// Laddar assets.bin till råbytes — kör i main FÖRE första fönstret
// (ingen renderer krävs). Relativa sökvägar provas mot gfx::basePath(),
// sedan basePath()/../Resources/ (macOS-bundlen), sist som de är.
// false = saknas/trasig/för ny ⇒ inget laddas, alla uppslag ger ogiltiga
// handtag; appen kan välja att vara strikt på returvärdet.
bool loadAssets(const std::string& path = "assets.bin");

// Uppslag på (lista, nyckel) — gör det EN gång (initializeComponent),
// inte per frame. Ogiltigt handtag vid miss (+ en loggrad, första gången).
AssetHandle img(const std::string& list, const std::string& key);

// Listans storleksklass i LOGISKA px (ritsidan multiplicerar med
// ctx.scale); 0 = ospecificerad (widgeten väljer fallback).
float assetSizeClass(AssetHandle h);

} // namespace glaze
