#pragma once
#include <string>

#include "window_model.h"

// cpp_writer — emittern + SPLASA-sparandet. Hård regel 5 strukturellt:
// API:t har exakt FYRA skrivoperationer, och ingen av dem kan uttrycka
// "ändra eller radera användar-bytes":
//
//   1. rewriteRegion       — skriv om det GENERERADE (i .h: enbart
//      GlazeBuilder-regionen mellan markörraderna, splasa — allt utanför
//      klistras tillbaka byte för byte; i *.glazeui: hela filen, filen ÄR
//      regionen).
//   2. appendToUserArea    — appenda text i .h direkt EFTER
//      #pragma endregion-raden (nya handler-deklarationer).
//   3. appendToCppEnd      — appenda text i slutet av användarens .cpp
//      (handler-stubbar).
//   4. createFromTemplate  — skapa en NY fil från mall; VÄGRAR om filen
//      redan finns.
//
// Splasan arbetar på RÅBYTES (CRLF/LF/BOM bevaras — testerna diffar
// bytes) och letar upp markörerna i filens AKTUELLA innehåll vid varje
// skrivning — har stängslet skadats sedan öppningen vägrar den
// (fail-safe), och källfilerna lämnas orörda.

namespace bld {

// ------------------------------------------------------------ emittering
// Rena strängfunktioner (headless-testbara). nl = radslutet ("\n"/"\r\n").

// Hela GlazeBuilder-regionen för .h, inklusive båda markörraderna.
std::string emitRegion(const WindowModel& m, const std::string& nl);

// Hela *.glazeui (100 % genererad C++). headerName = "start_window.h".
std::string emitGuiCpp(const WindowModel& m, const std::string& headerName,
                       const std::string& nl);

// "Nytt fönster"-mallens tre filer.
std::string emitHeaderTemplate(const WindowModel& m, const std::string& nl);
std::string emitUserCppTemplate(const WindowModel& m,
                                const std::string& headerName,
                                const std::string& nl);

// Append-text för en ny handler: deklarationen (till användarområdet i
// .h) resp. stub-kroppen (till slutet av användarens .cpp).
std::string emitHandlerDecl(const EventBind& e, const std::string& nl,
                            bool firstAppend);
std::string emitHandlerStub(const std::string& className, const EventBind& e,
                            const std::string& nl);

// ------------------------------------------------- de fyra operationerna

bool rewriteRegion(const std::string& hPath, const std::string& region,
                   std::string& err);
bool rewriteGeneratedFile(const std::string& guiCppPath,
                          const std::string& content, std::string& err);
bool appendToUserArea(const std::string& hPath, const std::string& text,
                      std::string& err);
bool appendToCppEnd(const std::string& cppPath, const std::string& text,
                    std::string& err);
bool createFromTemplate(const std::string& path, const std::string& content,
                        std::string& err);

// Hjälpare: finns strängen i filens bytes? (Duplikat-vakt för appends —
// substring-sökning, ingen parsning av användarkod.)
bool fileContains(const std::string& path, const std::string& needle);

} // namespace bld
