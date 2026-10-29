#pragma once
#include <string>

#include "window_model.h"

// cpp_reader — läser tillbaka GlazeBuilders EGEN genererade delmängd
// Buildern parsar ALDRIG
// användarkod: i .h läses enbart raderna innanför
// #pragma region GlazeBuilder … #pragma endregion (medlemsdeklarationer);
// *.glazeui är 100 % genererad (C++) och läses sats för sats enligt grammatiken.
// Allt utanför stängslet är användarens och bevaras byte för
// byte av cpp_writer.
//
// FAIL-SAFE i stället för gissning: skadade/saknade/oparade markörer, en
// oidentifierbar sats eller brace-obalans ⇒ ok = false + diagnos med
// fil:rad. Läsaren rör aldrig några filer. Okänd FÄLTRAD på känd widget
// (id.fält = värde;) är däremot INTE ett fel — den blir en opak Prop som
// round-trippas ordagrant (regel 4:s arvtagare).

namespace bld {

struct ReadResult {
    bool        ok = false;
    WindowModel model;
    std::string error;       // fail-safe-diagnos ("fil:rad: orsak")
    std::string hNewline   = "\n";   // detekterat radslut i .h
    std::string cppNewline = "\n";   // detekterat radslut i *.glazeui
    bool        hFound   = false;    // .h fanns på disk
    bool        cppFound = false;    // *.glazeui fanns på disk
};

// Syskonsökvägen per konvention: foo.h → foo.glazeui resp. foo.cpp.
std::string guiPathFor(const std::string& hPath);
std::string userCppPathFor(const std::string& hPath);
// Normaliserar valfri triplett-fil (.h/.cpp/.glazeui) → .h-sökvägen.
std::string headerPathFor(const std::string& anyPath);

// Läser hela fönstret via .h-sökvägen. Saknas .h helt → ok=false,
// hFound=false (anroparen väljer "nytt fönster"-mallen). Finns .h med
// stängsel men *.glazeui saknas → fail-safe (cppFound=false).
ReadResult readWindow(const std::string& hPath);

// Delarna — separat headless-testbara (rena strängar, ingen fil-I/O):
bool parseHeader(const std::string& hBytes, const std::string& hName,
                 WindowModel& m, std::string& err);
bool parseGuiCpp(const std::string& cppBytes, const std::string& cppName,
                 WindowModel& m, std::string& err);

// Hittar GlazeBuilder-regionen i rå .h-bytes: [regionStart, regionEnd) =
// från markörradens början t.o.m. endregion-radens radslut. false +
// diagnos vid saknade/dubblerade/oparade markörer. Delas av läsaren och
// writerns splasa (som letar om vid SKRIVNING — stängslet kan ha skadats
// sedan öppningen).
bool findRegion(const std::string& bytes, size_t& regionStart,
                size_t& regionEnd, std::string& err);

} // namespace bld
