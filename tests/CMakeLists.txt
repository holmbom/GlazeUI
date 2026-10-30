find_package(Threads REQUIRED)

# Headless-testerna. Inga fönster, ingen pump — ren std + glaze-typerna,
# så de går att köra i CI och i ett terminalfönster utan skrivbord.
#
# contrast_gate  princip 0 gjord körbar: mäter varje genererad palett mot
#                4.5:1 (text) respektive 3:1 (kanter och tillstånds-
#                markeringar) i BÅDA lägena, och FALLERAR under kravet.
# roundtrip_gate  triplett-grammatikens regressionsgrind, särskilt
#                 för designtaggen: läs → skriv ger byte-identisk *.glazeui för demons
#                 tripletter OCH för designtagg-fixturerna; tagg på fel
#                 plats normaliseras; TVÅ designrader ⇒ fail-safe.
#                 Fixturernas RADSLUT är låsta av rotens .gitattributes
#                 (design_crlf CRLF, resten LF) och mäts av grinden — med
#                 core.autocrlf=true mättes annars bara CRLF-vägen.
#                 Länkar BARA builder_core — ren std, inget fönster.
add_executable(contrast_gate contrast_gate.cpp)
target_link_libraries(contrast_gate PRIVATE glaze)

# version_gate  VERSIONEN (glaze/version.h är den enda källan): CMakes
#               PROJECT_VERSION — läst ur headern i rot-CMakeLists — är
#               samma tre tal och samma sträng som headerns makron.
add_executable(version_gate version_gate.cpp)
target_link_libraries(version_gate PRIVATE glaze)
target_compile_definitions(version_gate PRIVATE
    GLAZE_CMAKE_VERSION="${PROJECT_VERSION}"
    GLAZE_CMAKE_MAJOR=${PROJECT_VERSION_MAJOR}
    GLAZE_CMAKE_MINOR=${PROJECT_VERSION_MINOR}
    GLAZE_CMAKE_PATCH=${PROJECT_VERSION_PATCH})

# preview_gate  VERKTYGSVÄGENS grind: modellens råa
#                C++-uttryckstext → riktig glaze::Widget via den ENDA
#                `bld::applyProp` (builder_preview). Grinden finns
#                för att funktionen annars kan finnas i två handkopierade exemplar.
#                Länkar `glaze` — ALDRIG glaze_impl (WHOLE_ARCHIVE).
add_executable(preview_gate preview_gate.cpp)
# Repo-roten som kompileringskonstant: svepet
# som håller demons `.glazeui` fri från nakna UI-textliteraler läser
# tripletterna direkt ur trädet (roundtrip_gate-prejudikatet).
target_compile_definitions(preview_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(preview_gate PRIVATE glaze builder_preview)

add_executable(roundtrip_gate roundtrip_gate.cpp)
target_link_libraries(roundtrip_gate PRIVATE builder_core)
# Repo-roten som kompileringskonstant: fixturerna och demons tripletter är
# KÄLLKOD (regel 7) och läses direkt ur trädet, inte kopierade till bygget.
target_compile_definitions(roundtrip_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}")

# calls_fixture  KOMPILERINGSGRINDEN för metodanropssatsen.
#                Byggs, körs inte: den bevisar att den `.glazeui`
#                roundtrip_gate läser och skriver också går att BYGGA.
#                En grön round-trip är inget kompileringsbevis:
#                reader och writer kan vara överens om en rad kompilatorn
#                avvisar.
#                `fixtures/order` täcker satslistan
#                och bygger av exakt samma skäl: varvade fält/anrop, ett
#                anrop EFTER trädbygget och `statusBar.adopt(chip);` är alla
#                riktiga C++-rader, inte bara texter som round-trippar.
#                `fixtures/components` av samma skäl: en
#                timer och en systemfältsikon som medlemmar, kopplingar med
#                sin EGEN sendertyp och fönstrets nya nakna anrop.
set(CALLS_FIXTURE "")
glaze_add_window(CALLS_FIXTURE fixtures/calls fixtures/order fixtures/components)
add_executable(calls_fixture fixtures/calls_main.cpp ${CALLS_FIXTURE})
target_link_libraries(calls_fixture PRIVATE glaze)
target_include_directories(calls_fixture PRIVATE fixtures)

# terminal_fixture  samma kompileringsgrind för TERMINALENS form:
#                   setter-fälten, anropen med flera argument och de två
#                   kopplingarna mot sidopaketets header. Bara när paketet byggs.
if(TARGET glaze_terminal_ui)
    set(TERMINAL_FIXTURE "")
    glaze_add_window(TERMINAL_FIXTURE fixtures/terminal)
    add_executable(terminal_fixture fixtures/terminal_main.cpp ${TERMINAL_FIXTURE})
    target_link_libraries(terminal_fixture PRIVATE glaze glaze_terminal_ui)
    target_include_directories(terminal_fixture PRIVATE fixtures)
endif()

# window_gate  FÖNSTRETS grind: innehållsytan räknas
#              baklänges genom det chrome fönstret faktiskt har, och
#              följer med när statusraden visas/döljs och när stilen byts.
#              Uträkningen mäts UTAN fönster (tvåfasen gör konstruktorn
#              ofarlig); omräkningen vid stilbyte kräver ett DOLT fönster
#              och hoppas över med en skriven rad när ingen display finns.
add_executable(window_gate window_gate.cpp)
target_link_libraries(window_gate PRIVATE glaze)

# post_gate  TRÅDSÄKER POST: en bakgrundstråd postar
#            `close()` medan pumpen SOVER i waitEvent — `run()` ska
#            returnera. Grinden fångar att ett
#            rent OS-wakeup räcker inte, backenden måste också KÖA ett
#            `EventType::Wakeup`. Faller väckningen HÄNGER testet, så
#            ctest-TIMEOUTEN är en del av grinden.
add_executable(post_gate post_gate.cpp)
target_link_libraries(post_gate PRIVATE glaze Threads::Threads)

# msgbox_gate  MEDDELANDERUTANS grind: radbrytningen mot en
#              FALSK font med känt teckensteg (räknade brytpunkter, inte
#              uppskattade) i TVÅ skalor, `Label::wrap`s auto-höjd och det
#              skrivna kontraktet att den kräver en satt bredd, samt
#              resultatmappningen för alla sex knappuppsättningar.
#              Headless — ingen display, inget fönster, ingen pump.
#              Når `src/` för den INTERNA `message_box_table.h` (samma
#              förhållande som preview_gate har till builder_preview):
#              tabellen är ren std och grindas därför direkt.
add_executable(msgbox_gate msgbox_gate.cpp)
target_link_libraries(msgbox_gate PRIVATE glaze)
target_include_directories(msgbox_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)

# dropdown_gate  DROPDOWNENS regressionsgrind: popup-listan är en
#                ListBox-subklass, den största regressionsrisken.
#                Tvådelad:
#                headless för setProp-vokabulären, event-kontraktet,
#                STÄNGDA lägets tangenter och fältets justering
#                (textens x-läge i alla tre familjerna); med ett DOLT fönster
#                (`setOffscreen`, window_gate-prejudikatet) för öppna/stäng,
#                dropMaxRows-kapningen, ÖPPNA lägets tangenter, Escape utan
#                ändring, commit-exakt-en-gång och setItems under öppen
#                popup. Fönsterdelen körs som en kedja av `App::post`-steg,
#                för stängningen är UPPSKJUTEN till pump-svansen — därför är
#                ctest-TIMEOUTEN en del av grinden.
add_executable(dropdown_gate dropdown_gate.cpp)
target_link_libraries(dropdown_gate PRIVATE glaze)

# listbox_gate  LISTBOXENS egen grind: rena API-fall
#               headless (tom lista, index utanför intervallet, setItems som
#               krymper, kryssen) och kontraktet ANVÄNDARE vs PROGRAM —
#               `selectFromUser`/`toggleCheckFromUser` fyrar exakt en gång,
#               settrarna aldrig.
add_executable(listbox_gate listbox_gate.cpp)
target_link_libraries(listbox_gate PRIVATE glaze)

# toolbar_gate  VERKTYGSFÄLTETS grind: klickkontraktet (vanlig
#               knapp fyrar bara onClick, växlingsbar onChange FÖRE onClick,
#               programmatisk setChecked aldrig något), att verktygsknappen
#               inte är ett tabbstopp men ett fält i remsan är det,
#               placeringen med luft + lucka mot ett handräknat förväntat värde, att
#               style.cpp:s härledning (ikonknappen == remsans innerhöjd)
#               håller i alla tre UI, och den inaktiva ikonens OPACITET —
#               inspelad ur en test-renderer, så den mäts utan pixlar. Når
#               `Glaze/widgets/src/` för den interna `image_cache.h`
#               (lang_gate-prejudikatet) och läser demons assets.bin.
#               RULLGARDINEN: bredden med pilens ruta och
#               menyns platta form headless, och i ett DOLT fönster riktiga
#               tryck genom `handleEvent` — menyn öppnas utan klick,
#               `onDropDown` före öppningen (posten syns i popupens rader),
#               den delade knappens två delar. 77 = fönsterdelen kunde inte
#               köras (ingen display eller inga popupfönster).
add_executable(toolbar_gate toolbar_gate.cpp)
target_link_libraries(toolbar_gate PRIVATE glaze)
target_include_directories(toolbar_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
target_compile_definitions(toolbar_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}")

# icon_gate  PROGRAMIKONENS grind: PNG-codecen i
#            glaze_assets mot OS:ets kodek och mot grindens egna PNG:er,
#            skalaren (förmultiplicerad rymd), valregeln i båda
#            implementationerna, .ico/.icns-strukturen och de committade
#            filerna, inbäddningen (kFrameworkAssetsBin == den committade
#            framework_assets.bin, bit 31), gfx::setAppId och AppIcon i ett
#            okonstruerat fönster. Headless. Når Glaze/widgets/src och
#            Glaze/gfx/src för de interna assets_builtin.h, image_cache.h och
#            icon_util.h (toolbar_gate-prejudikatet), läser demons
#            assets.bin och KÖR `glazeassets icon` (D3: de committade
#            ikonfilerna är byte för byte verktygets utdata).
add_executable(icon_gate icon_gate.cpp)
target_link_libraries(icon_gate PRIVATE glaze glaze_assets)
target_include_directories(icon_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src
                                   ${CMAKE_SOURCE_DIR}/Glaze/gfx/src)
target_compile_definitions(icon_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}"
                                   GLAZE_ASSETS_CLI="$<TARGET_FILE:glazeassets>")
# D3 kör det RIKTIGA verktyget och jämför med de committade ikonfilerna.
add_dependencies(icon_gate glazeassets)

# groupbox_gate  GRUPPRAMENS grind: innerytan mot handräknat förväntat värde
#                (luften, hela rubrikbandet, tom rubrik flyttar inget), den
#                KONCENTRISKA klippradien mätt med en egen stil, autostorleken
#                ur barnen (kompositstödets första användare — per axel och
#                oberoende av förälderns yta) och LUCKAN i linjen, inspelad ur
#                en test-renderer: tre klipp, inget över överkanten i luckan,
#                nederkanten under den ritad.
add_executable(groupbox_gate groupbox_gate.cpp)
target_link_libraries(groupbox_gate PRIVATE glaze)

# spinbox_gate  TALFÄLTETS grind — och KOMPOSITSTÖDETS första
#               riktiga prov. Headless: värdet (klampning och avrundning vid
#               LÄSNING, ordningsoberoende settrar), tolkningen av användarens
#               text mot en handskriven tabell, användarvägarna (onChange + onCommit
#               exakt vid effektiv ändring, texten först vid Enter/fokus
#               ut), formateringen per språk och att
#               inredningen inte kan lossas. Med ett DOLT fönster:
#               ett tabbstopp, FOKUSOMBUDET och Tab inifrån fältet — de tre
#               hål talfältet visade i kompositstödet — och HJULET KRÄVER
#               FOKUS för talfältet OCH reglaget i samma rullande
#               panel: utan fokus rullar panelen, med fokus stegar värdet.
#               77 = fönsterdelen kunde inte köras (ingen display);
#               headless-delen är då ändå mätt, och skippet SYNS i ctest.
add_executable(spinbox_gate spinbox_gate.cpp)
target_link_libraries(spinbox_gate PRIVATE glaze)

# numpad_gate  NUMPADENS NAVIGATIONSLÄGE: vikningen
#              (`gfx::navigationKey`) och namnen — kanoniska och översatta i
#              fyra språk — mot HANDSKRIVET förväntat värde, headless. Med ett DOLT
#              fönster: vikningen genom `AppWindow::handleEvent` (fokuserad
#              lista, accelerator, öppen meny) och på Windows översättningen
#              genom fönstrets RIKTIGA WndProc (SendMessageW med och utan
#              extended-flaggan). På Linux mäts xkb-översättningen mot en
#              RIKTIG keymap ur systemets xkeyboard-config — grinden når den
#              interna `xkb_keys.h` (msgbox_gate-prejudikatet) och xkbcommon
#              direkt. 77 = ett block kunde inte köras (ingen display eller
#              ingen xkb-data) — SKIPPED, aldrig grönt.
add_executable(numpad_gate numpad_gate.cpp)
target_link_libraries(numpad_gate PRIVATE glaze)
if(GLAZE_BACKEND_RESOLVED STREQUAL "linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NUMPAD_XKB REQUIRED IMPORTED_TARGET xkbcommon)
    target_link_libraries(numpad_gate PRIVATE PkgConfig::NUMPAD_XKB)
    target_include_directories(numpad_gate PRIVATE
                               ${CMAKE_SOURCE_DIR}/Glaze/gfx/src/backend_linux)
endif()

# menu_gate  MENYERNAS grind. Headless för trädet,
#            index-stigen, aktiveringstabellen med radio-uteslutning,
#            acceleratormatchningen, genvägsformatteringen och menyradens
#            inverkan på innehållsytan; med ett DOLT fönster för
#            popup-kedjan, rubrikbytet och den UPPSKJUTNA stängningen —
#            därför är ctest-TIMEOUTEN en del av grinden. `kedjanKordes` +
#            en skip-räknare gör en tyst överhoppning omöjlig:
#            ett test som ljuger är sämre än inget test.
add_executable(menu_gate menu_gate.cpp)
target_link_libraries(menu_gate PRIVATE glaze)

# layout_gate  PLACERINGENS grind: syskonankaret,
#              sträckningen, fallbacken vid ankarmiss och att gamla
#              anchors-semantiken är oförändrad — allt HEADLESS, för
#              `Widget::resolve()` är const, stateless och kräver varken
#              fönster eller renderer. FÖRSTA fallet är stilbytet: samma
#              träd i Slate och Minimalistic ger 8 px gap i BÅDA medan
#              knapparna byter storlek, alltså strömmens motivering i
#              mätbar form. TIMEOUT:en är en del av grinden — en cykel i
#              uppslaget hade visat sig som ett häng och inte som ett fel.
#              Fall 22 är `childrenExtent`s invers åt båda
#              hållen: rader mot nederkanten, spända barn, luften, sidleds-
#              paren och det dolda ledet i en kedja — förväntat värde och sabotagen i
#              grindens huvud.
add_executable(layout_gate layout_gate.cpp)
target_link_libraries(layout_gate PRIVATE glaze)

# measure_gate  MÄTVÄGENS ENHETSGRIND: mäter
#               `contentSize()` DIREKT — inte via `resolve()` — för varje
#               typ i widget-registret, vid skala 1 och 2. `layout_gate`
#               kan omöjligt skilja enheterna åt (resolve-utdata är ∝ skala
#               i båda), så den grinden fångar HALVKONVERTERING medan den
#               här fångar ENHETEN. Länkar `glaze` — ALDRIG glaze_impl:
#               registreringarna strippas då och loopen mäter noll typer,
#               vilket är exakt vad antalskravet i grinden vaktar.
add_executable(measure_gate measure_gate.cpp)
target_link_libraries(measure_gate PRIVATE glaze)

# unadopt_gate  BORTTAGNINGENS grind: motsatsen till `adopt`.
#               Mäter symmetrin (lånat
#               släpps och kan adopteras OM, ägt förstörs eller lämnas ut),
#               ATOMICITETEN (varje nekat fall verifierar att trädet är
#               exakt som det var) och DENY-fallen — öppen Dropdown-popup
#               och öppen meny, båda djupt nere i delträdet. Deny-delen
#               körs som en kedja av `App::post`-steg, för stängningen är
#               UPPSKJUTEN till pump-svansen och nekandet gäller ÄVEN
#               direkt efter `close()`; därför är ctest-TIMEOUTEN en del av
#               grinden. `kedjanKordes` + en skip-räknare gör en tyst
#               överhoppning omöjlig.
add_executable(unadopt_gate unadopt_gate.cpp)
target_link_libraries(unadopt_gate PRIVATE glaze)

# clip_gate  RUNDAT BARNKLIPPETS grind — en av två
#            grindar som mäter PIXLAR (pixel_gate, nedan, är den andra och
#            mäter att varje widget RITAR). Anspråket står ordagrant i
#            filhuvudet, och det är avsiktligt smalt: hörnet bortmaskat,
#            kantmitten kvar, kontrollkörning med radie 0, i BÅDA
#            shader-vägarna (mode 1 = `Draw::rect`, mode 0 = texturmasken
#            som all text går genom). En ren TILLSTÅNDSgrind mäter inte
#            pixlar och en formelkopierande grind mäter formeln själv —
#            därför captureFrame och råa pixlar.
#            Vägen är glaze-renders (setOffscreen → show → renderFrame →
#            captureFrame) och läser retained-ytan FÖRE compositen, så det
#            är RITVÄGENS klipp som mäts och inte fönstermasken.
#            INGEN SKIP_RETURN_CODE: utan headless-yta
#            blir ctest RÖD, inte gul. En pixelgrind som hoppar bevisar
#            ingenting om shadern.
add_executable(clip_gate clip_gate.cpp)
target_link_libraries(clip_gate PRIVATE glaze)

add_test(NAME contrast_gate COMMAND contrast_gate)
add_test(NAME version_gate COMMAND version_gate)
add_test(NAME clip_gate COMMAND clip_gate)
set_tests_properties(clip_gate PROPERTIES TIMEOUT 30)
add_test(NAME layout_gate COMMAND layout_gate)
set_tests_properties(layout_gate PROPERTIES TIMEOUT 20)
add_test(NAME measure_gate COMMAND measure_gate)
add_test(NAME unadopt_gate COMMAND unadopt_gate)
# 77 = deny-fallen kunde inte köras (ingen display). Utan SKIP_RETURN_CODE
# hade en sådan körning rapporterats GRÖN trots att den mätt mindre än den
# påstår — se noten vid grinden ovan. Kärnlogiken mäts ändå alltid: fall 5b
# kör deny-rekursionen OS-fritt via en virtual.
set_tests_properties(unadopt_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
add_test(NAME dropdown_gate COMMAND dropdown_gate)
add_test(NAME listbox_gate COMMAND listbox_gate)
add_test(NAME toolbar_gate COMMAND toolbar_gate)
# 77 = rullgardinens fönsterdel kunde inte köras — utan SKIP_RETURN_CODE hade
# en sådan körning rapporterats GRÖN trots att den mätt mindre än den påstår.
set_tests_properties(toolbar_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
add_test(NAME icon_gate COMMAND icon_gate)
set_tests_properties(icon_gate PROPERTIES TIMEOUT 30)
add_test(NAME groupbox_gate COMMAND groupbox_gate)
add_test(NAME spinbox_gate COMMAND spinbox_gate)
set_tests_properties(spinbox_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
add_test(NAME menu_gate COMMAND menu_gate)
set_tests_properties(menu_gate PROPERTIES TIMEOUT 30)
add_test(NAME numpad_gate COMMAND numpad_gate)
set_tests_properties(numpad_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
set_tests_properties(dropdown_gate PROPERTIES TIMEOUT 30)
add_test(NAME msgbox_gate COMMAND msgbox_gate)
add_test(NAME preview_gate COMMAND preview_gate)
add_test(NAME window_gate COMMAND window_gate)
add_test(NAME roundtrip_gate COMMAND roundtrip_gate)
add_test(NAME post_gate COMMAND post_gate)
set_tests_properties(post_gate PROPERTIES TIMEOUT 20)

# pump_gate  D3D11-PUMPEN: ett meddelande som SKICKAS till
#            fönstret (SendMessage från en annan tråd, OS:ets utsändningar)
#            levereras INUTI GetMessageW, som sedan väntar vidare — ett
#            gfx-event som WndProc:en köade där låg kvar tills musen rördes.
#            En bakgrundstråd skickar WM_CLOSE medan pumpen sover; run() ska
#            returnera. Grinden väcker själv pumpen efter en frist, så felet
#            blir en skriven rad och inte ett häng. BARA Windows: anropet är
#            Win32 (SendMessageW/FindWindowW) och felet är Win32-semantik —
#            de andra backendarna har inget skickat meddelande att leverera.
if(WIN32)
    add_executable(pump_gate pump_gate.cpp)
    target_link_libraries(pump_gate PRIVATE glaze Threads::Threads)
    add_test(NAME pump_gate COMMAND pump_gate)
    set_tests_properties(pump_gate PROPERTIES TIMEOUT 20)
endif()

# lang_gate  SPRÅKPAKETETS grind. Del 1 mäter FORMATET mot
#            ett HANDSKRIVET förväntat värde — en minimal tvåspråksfil monterad byte
#            för byte i testet, med testets EGEN CRC32 — och packaren måste
#            producera exakt de bytesen; ingen förväntan läses ur
#            implementationen. Sabotagedelen muterar fyra bytes var för sig
#            (defaultLang, ett tabelloffset, ett id, ett langIndex) och
#            kräver CRC-avslag i alla fyra: det är beviset för att
#            CRC-täckningen är rätt vald, och exakt det assets blob-CRC inte
#            kunde ge. Del 2 mäter RUNTIME och länkar dessutom `glaze`.
#            Del 2 mäter RUNTIME med `glaze/strings.h` LÄST SOM TEXT som
#            orakel — inte bin-filen, som annars hade fått godkänna sig
#            själv: lägger någon till en konstant utan att översätta den
#            faller grinden med konstantens NAMN i felraden. Den mäter
#            också att `lang_builtin.cpp` är BYTE-IDENTISK med committade
#            `language.bin` (inget i bygget kör om `glazelanguage embed`).
#            Når `Glaze/widgets/src/` för `lang_builtin.h` — samma
#            förhållande som msgbox_gate har till message_box_table.h.
#            Del 4 läser varje .cpp/.h under `Glaze/widgets` som
#            TEXT och fäller en literal som ser ut som användartext (en
#            bokstav utanför ASCII, två ord, eller ett ord med versal) —
#            utom loggtexter, jämförelser och det som bär
#            `// lang_gate:rad`/`av … på` med ett skäl. Ett självtest med
#            handskrivet förväntat värde fäller grinden om en regel slutar verka.
add_executable(lang_gate lang_gate.cpp)
target_link_libraries(lang_gate PRIVATE glaze glaze_language)
target_include_directories(lang_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
target_compile_definitions(lang_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}")

add_test(NAME lang_gate COMMAND lang_gate)

# pixel_gate  VARJE widget i galleri-fixturen RITAR NÅGOT
#             SYNLIGT, i varje UI × tema × läge × stil, mätt i PIXLAR genom
#             den riktiga backenden (glaze-renders väg: dolt fönster →
#             renderFrame → captureFrame). Tre kontroller per widget och
#             palett: egen ritning (differentiellt nivå för nivå — barnen
#             räknas aldrig in), aktiverat/inaktiverat skiljer sig (alltid
#             för det som tar fokus och för verktygsknappen — dess ikons
#             opacitet är backendens shader —, konsekvent inom UI:t för
#             resten) och
#             texten syns. Kända palettfel står i `kKändaFel`, som är
#             självutlöpande. Trädet vandras GENERISKT, så
#             en ny komponent i tests/fixtures/gallery.* täcks utan att
#             grinden ändras. Länkar builder_preview för `buildWindowTree`
#             — samma trädbygge som glaze-render, inte en kopia.
#             INGEN SKIP_RETURN_CODE: samma beslut som clip_gate —
#             utan headless-yta blir ctest RÖD.
add_executable(pixel_gate pixel_gate.cpp)
target_link_libraries(pixel_gate PRIVATE glaze builder_preview)
target_compile_definitions(pixel_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME pixel_gate COMMAND pixel_gate)
# 600 s: på en Retina-skärm (skala 2) mäts fyra gånger så många pixlar, och
# ett Debug-bygge tog 303 s ensamt på macOS — tiden går i grindens
# egna pixelbuffertar, inte i backenden. Taket fångar en hängning, inget mer.
set_tests_properties(pixel_gate PROPERTIES TIMEOUT 600)

# ---- RITYTAN — scenen bredvid UI-pipelinen.
#
# scene_math_gate  KAMERAN headless: Mat4d, project/unproject mot ett
#                  HANDRÄKNAT förväntat värde (pitch 0 som ren skalning och vridning,
#                  pitch 45° ur en hålkamera härledd för sig), tur och retur
#                  vid SWEREF, ortho = perspektiv vid pitch → 0, horisonten,
#                  zoom mot punkt och RTC-precisionen i GPU:ns float-väg mot
#                  double-referensen (< 0,05 px), med kontrollen att samma
#                  väg UTAN RTC missar. DEL 3D: 3D-vyn
#                  och 3D-kameran mot en egen hålkamera — likheten
#                  toView3D/MapView inom 1e-6 px, konventionen (ögat söder om
#                  målet vid azimut 0), matchad orto, strålen tur och retur,
#                  skalan mot en numerisk derivata, tumlingen förbi 90°,
#                  UpAxis::Y, synliga delen av ett plan, RTC i 3D och
#                  kamerans verktyg. Headless; länkar glaze och når
#                  `Glaze/widgets/src/` för den interna `camera_math.h`.
# geom_gate        GEOMETRIBYGGARNA headless, linjerna: bitarna,
#                  grannarna, `dist`, rensningen och CHUNKGRÄNSEN (≤ 65 536
#                  vertex, eget origo, varje segment en gång). Ytorna:
#                  arean mot en egen skosnöresformel (hålet = 84,
#                  200 slumppolygoner med fast frö), degenererat och
#                  ogiltigt, kantremsans riktning och chunkarna. Texten:
#                  avståndsfältet mot det riktiga euklidiska
#                  avståndet, layoutens bredd = Font::measure, en
#                  uppladdning per unik glyf, syntesen och markörerna.
#                  DEL 3D: generatorerna (antal,
#                  tecknad volym mot formeln, slutna med svetsade lägen,
#                  normalerna utåt), polygonen i ett lutat plan (arean,
#                  planets normal), meshens SEGMENT (i följd, lokal
#                  ommappning, tabellen tillbaka, nivåerna ommappade, 64
#                  MiB), meshens data (avslagen, ändringsloggen) och
#                  punktmolnets chunkar. Textdelarna
#                  (S5a–S5g — bredden, färgen per vertex, en uppladdning per
#                  (stil, kodpunkt), brytningen över delarna och
#                  `wrapText`s regel på en korpus, syntesen per del,
#                  radhöjden, glorior före fyllningar över atlassidor) och
#                  bildens projektiva vikter mot en egen homografi (Q1–Q4).
#                  Länkar glaze och når `Glaze/widgets/src/` för de interna
#                  `canvas/mesh_data.h` och `canvas/build.h`.
# scene_pixel_gate PIPELINEN i PIXLAR genom den riktiga backenden
#                  (clip_gate-vägen): konstant skärmbredd vid zoom, bäring och
#                  tilt, världsläget, AA-kanten, scissorn och den rundade
#                  masken i Line-programmet, SWEREF-precisionen ±1 kolumn och
#                  `bytesUploaded == 0` efter tio kameraändringar. Del B
#                  (bilderna): perspektivkorrekt platta, filtret
#                  som batch-tillstånd och läckan mellan vägarna, delrekt,
#                  R8, fönstrets bildcache (budget, LRU, tak, ändringslogg)
#                  och Image-programmets mask. Del C (ytorna):
#                  hålet, kantremsan på 1 px vid ×1/×16 och under tilt,
#                  gradienterna (pad, radiell, fast i världen, samma block i
#                  Line-programmet, rampen linjär) och Fill-programmets
#                  mask. Del D (texten): upprätt under bäring och
#                  tilt, Map-rotationen, glorian, storleken i mm, fet mot
#                  vanlig, markörernas areor, plattan, Glyph-programmets
#                  mask och atlasen linjär. Del E (render-målen):
#                  orienteringen, premultiplikationen, klippet före och
#                  efter, scissorn i målet, nästling, RenderTexture och
#                  scenen I ett mål. B8–B11 (bilden
#                  på fyra hörn — strecken där grindens homografi lägger dem
#                  rakt uppifrån och under tilt, rektangeln = dess fyra hörn,
#                  rättvänd RenderTexture och Bitmap), D8–D9 (textdelarna —
#                  en chunk, färgerna vid delgränsen, den feta delens gloria
#                  under den vanligas fyllning) och del R (glyfreserven med
#                  backendens riktiga UI-font — hasGlyph, ett annat raster
#                  än ersättningens, mätningen = rastret, otilldelat och
#                  styrtecken, SDF-atlasen). INGEN SKIP_RETURN_CODE
#                  (clip_gate-beslutet).
add_executable(scene_math_gate scene_math_gate.cpp)
target_link_libraries(scene_math_gate PRIVATE glaze)
# Grindens float-modeller räknas UTAN FMA, så de ger samma tal på alla
# plattformar: KONTROLLEN att 3D-vägen utan RTC missar med mer än 1 px föll
# på macOS (0,93 px). Clang slår ihop multiplikation och addition inom ett
# uttryck (fp-contract=on, arm64 har FMA), och GCC gör det över satser också
# (fast) där målet har FMA — x86-64 utan -march har det inte.
if(NOT MSVC)
    target_compile_options(scene_math_gate PRIVATE -ffp-contract=off)
endif()
target_include_directories(scene_math_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME scene_math_gate COMMAND scene_math_gate)

add_executable(geom_gate geom_gate.cpp)
target_link_libraries(geom_gate PRIVATE glaze)
target_include_directories(geom_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME geom_gate COMMAND geom_gate)

# pick_gate        3D-PLOCKNINGENS KÄRNA headless:
#                  BVH:n mot BRUTE FORCE med grindens EGEN Möller–Trumbore
#                  (50 slumpmeshar × 1 000 strålar, samma triangel och punkt
#                  inom 1e-9 — också axelparallella strålar och strålar som
#                  ligger i lådornas plan), ett exakt rutnät liggande och
#                  stående (kantträffarna, och plattprovets NaN-regel på z),
#                  originalets triangelindex genom ett segmenterat mesh,
#                  nivån och intervallet, `opaqueOnly`, längsta avståndet,
#                  ombyggnaden efter `setPositions`, `keepCpuCopy`, bygget i
#                  en arbetartråd och strålen mot ett plan. `--stolpe` skriver
#                  ut BVH-byggets tid för en stolpe om ≈ 200 k trianglar
#                  (ingen grind, inte i ctest). RITYTANS PLOCKNING med ett
#                  DOLT fönster mot grindens egen hålkamera: rayAt ∘ project,
#                  worldPerPixel mot derivatan, visibleRegion mot en trapets,
#                  hörn och kant inom N px (och inte vid N + 0,3), punkterna
#                  med maxDistance och indatans plats, etiketten i 3D,
#                  heightAt på en lutande terräng, ytan genom placeringen,
#                  instansen, sorteringen, castRay, skymningen, opaqueOnly och
#                  routningen till en del-vy. Når `Glaze/widgets/src/` för
#                  de interna canvas-headrarna. 77 = ingen display ⇒ SKIPPED.
add_executable(pick_gate pick_gate.cpp)
target_link_libraries(pick_gate PRIVATE glaze)
target_include_directories(pick_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME pick_gate COMMAND pick_gate)
set_tests_properties(pick_gate PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

add_executable(scene_pixel_gate scene_pixel_gate.cpp)
target_link_libraries(scene_pixel_gate PRIVATE glaze)
# Del B:s budget mäter fönstrets bildcache direkt — den interna
# `image_cache.h` (tooltip_gate-prejudikatet).
target_include_directories(scene_pixel_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME scene_pixel_gate COMMAND scene_pixel_gate)
set_tests_properties(scene_pixel_gate PROPERTIES TIMEOUT 60)

# scene3d_pixel_gate DJUPET, MSAA OCH RENDERLÄGET i PIXLAR genom den
#                  riktiga backenden (del A och B): Less
#                  oavsett ordning i alla fyra programmen, Greater, LessEqual,
#                  utan skrivning, biasen, gallringen mot grindens egen
#                  vindning, de fem blandningarna mot sin formel och
#                  UI-återställningen efter ett scenanrop (A1–A8); MSAA:s
#                  mellantoner, supportedSamples, readTarget rättvänt med rak
#                  alfa, ritning utanför en frame och upplösningen — också för
#                  ett mål som lämnas för ett nästlat (B1–B5). Och fasadens två
#                  kontrakt: BARA PNG avkodas (P1–P3) och GPU-preferensen, på
#                  Windows mot DXGI:s egen ordning (K1 — dxgi länkas för den).
#                  Del C: EGNA PROGRAM — grindens egna program i
#                  HLSL, GLSL och MSL med eget format, heltalsattribut,
#                  GlazeUser, textur på plats 2, instanser (också med en del
#                  som börjar vid instans 1), icke-indexerat, helskärmspasset,
#                  felrad 7 och en saknad källa, GlazeFrame mot grindens vy,
#                  definitioner, renderläget och de delade reglerna (C1–C12).
#                  Del D: LINE3 OCH GLYPH3 mot hålkamerans förväntade värde —
#                  bredden nära och fjärran, närplanet, fyrkantiga ändar,
#                  streck i 3D-längd, Less i korsningen, markören på höjden
#                  och bakom kameran, världsbredden (D1–D7). Del G: RITYTANS
#                  3D-MÅL — 2D-vägens pixlar, kompositen, cachen, knapp-
#                  handlern, tonen och Ground-etiketterna överst (G1–G6).
#                  Del E och F: FÖREMÅLEN GENOM RITYTAN — ljuset,
#                  baksidan, den platta normalen, texturen, rampen,
#                  mättnaden, dimman, RTC, instanserna och ändringsloggen
#                  (E1–E10); punkternas yta i px och mm, rampen, djupet,
#                  färgen och sfären om 4 mm nära och fjärran (F1–F6).
#                  Når `Glaze/widgets/src/` för den interna
#                  `canvas/resources.h` (3D-målets räknare).
#                  INGEN SKIP_RETURN_CODE (clip_gate-beslutet).
add_executable(scene3d_pixel_gate scene3d_pixel_gate.cpp)
target_link_libraries(scene3d_pixel_gate PRIVATE glaze)
target_include_directories(scene3d_pixel_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
if(WIN32)
    target_link_libraries(scene3d_pixel_gate PRIVATE dxgi)
endif()
add_test(NAME scene3d_pixel_gate COMMAND scene3d_pixel_gate)
set_tests_properties(scene3d_pixel_gate PROPERTIES TIMEOUT 120)

# mesh_program_gate UTAN ritytan: de
#                  INBYGGDA Mesh- och Points-programmen (src/canvas/
#                  programs3d.cpp) genom fasaden direkt — createProgram +
#                  drawMesh i offscreen-mål med djup. Alla 128 + 4 varianter
#                  kompilerar och läser layoutens platser (V1); ljusmodellen,
#                  baksidan tvåsidigt med och utan djup, den platta normalen,
#                  texturen och ett måls bild, rampen (också linjärt mellan
#                  texlarna), mättnaden, dimman, RTC vid SWEREF, instanserna
#                  (läge, gir, skala, färg, normal, delen) och SCREEN_SIZED
#                  (E1–E10); punkternas yta i px, mm och världen, formen,
#                  rampen, djuptestet, färgen och gallringen (F1–F8). Förväntat värde är
#                  modellens definition och grindens egen geometri. Når
#                  `Glaze/widgets/src/` för den interna `canvas/programs3d.h`.
#                  INGEN SKIP_RETURN_CODE (clip_gate-beslutet).
add_executable(mesh_program_gate mesh_program_gate.cpp)
target_link_libraries(mesh_program_gate PRIVATE glaze)
target_include_directories(mesh_program_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME mesh_program_gate COMMAND mesh_program_gate)
set_tests_properties(mesh_program_gate PROPERTIES TIMEOUT 120)

# painter_gate  RITYTANS LAGER OCH MÅLARE headless: en
#               riktig Canvas mot en INSPELANDE renderer och riktiga
#               CanvasResources/ImageCache — körningarna i Strict och ByKind,
#               lagrens ordning och zoomintervall, residensen (kameran laddar
#               aldrig upp, inte heller för en chunk som panoreras in),
#               tillägget och borttagningen (bara deras byte), kompakteringen
#               över en fjärdedel döda, gallringen, ersättningen på plats,
#               rutnätets statiska mesh, målarens satser och rymder, tonen
#               och kollektionsvokabulären — och (TD)
#               målarens textdelar och fyrhörning mot lagrets. Pixlarna är
#               scene_pixel_gates (del F).
add_executable(painter_gate painter_gate.cpp)
target_link_libraries(painter_gate PRIVATE glaze)
target_include_directories(painter_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME painter_gate COMMAND painter_gate)

# dnd_gate  DRAG-AND-DROPS grind: lasten och effektmasken
#           headless, sedan ROUTINGEN genom fönstrets riktiga ingång
#           (`AppWindow::handleEvent`, protected — WinForms WndProc) i ett
#           DOLT fönster: tröskeln (mer än, inte lika med), enter/over/
#           leave/drop i rätt ordning på rätt mål, avvisat släpp, Escape,
#           fokusförlust, släpp utanför varje mål och utanför fönstret,
#           att ett drag inte ger ett klick, att en källa som unadopteras
#           mitt i draget inte lämnar en dinglande pekare — och att
#           INGENTING ritas om när ingenting ändras (räknat ur riktiga
#           `renderFrame`-anrop). 77 = ingen display ⇒ SKIPPED, aldrig grön.
#           AUTOSKROLLEN med ett injicerat
#           tick — så den når `Glaze/widgets/src/` för `drag_scroll.h`.
add_executable(dnd_gate dnd_gate.cpp)
target_link_libraries(dnd_gate PRIVATE glaze)
target_include_directories(dnd_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME dnd_gate COMMAND dnd_gate)
set_tests_properties(dnd_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# tree_gate  TRÄDETS grind. Headless: kollektionsvokabulären med
#            STIGAR (addNode/setNode/setNodes/removeNode/moveNode i
#            FÖRE-flytten-koordinater), att valet FÖLJER NODEN och inte
#            positionen, och ANVÄNDARE mot PROGRAM (tangenterna fyrar ett
#            event per handling, settrarna inget). Med ett DOLT fönster genom
#            den riktiga ingången: pilen fäller utan att välja, ihopfällningen
#            som gömmer det valda, och drag-and-drop — den inbyggda lasten,
#            omordning före/efter/in i, ett mål inuti den dragna noden som
#            avvisas, och en flytt till ett annat mål som tar bort noden.
#            Fall 19–30: flervalet, kryssen, nodens
#            textfärg och bilden ur pixlar — ritningen mot en inspelande
#            renderer, så den når `Glaze/widgets/src/` för den interna
#            `image_cache.h` (toolbar_gate-prejudikatet).
add_executable(tree_gate tree_gate.cpp)
target_link_libraries(tree_gate PRIVATE glaze)
target_include_directories(tree_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME tree_gate COMMAND tree_gate)
set_tests_properties(tree_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# label_gate  ETIKETTENS KORTNING, HÖJDLED OCH GRANNE och KNAPPARNAS TEXTFÄRG
#             Headless mot en inspelande
#             renderer och en falsk font: "…" och tooltipen när texten
#             faktiskt kortats (gränsfallet mot `elide`), `vAlign`, grannens
#             mätta höjd (dold, satt höjd, två etiketter som pekar på
#             varandra), och färgen på Button/Checkbox/Radio — appens, textDim,
#             `onAccent` på acceptknappen, och aldrig rutan i appens färg.
#             Del W: breddgruppen — över föräldrarna, utan de
#             dolda, satt bredd som golv, rollens font och fälten i linje.
add_executable(label_gate label_gate.cpp)
target_link_libraries(label_gate PRIVATE glaze)
add_test(NAME label_gate COMMAND label_gate)
set_tests_properties(label_gate PROPERTIES TIMEOUT 30)

# tooltip_gate  TOOLTIPENS grind. Headless: basens API och att varje
#               textwidgets `setTextToken` kedjar till basen. Med ett DOLT
#               fönster genom den riktiga ingången och popupen som RIKTIGT
#               fönster: vila visar (ramverkets timer-id matat syntetiskt för
#               logiken), rörelse inom står kvar, direkt till nästa källa,
#               routad uppslagning, klick/tangent tystar, pekaren ut och
#               fokusförlust gömmer, inaktiverad förklarar sig, ny text,
#               språkbyte, ingen tooltip medan en annan popup lever, källan
#               som lämnar trädet — och två fall på den RIKTIGA timern, där
#               det andra skiljer en varm väntan från en kall. Når
#               `Glaze/widgets/src/` för den interna `tooltip.h`
#               (msgbox_gate-prejudikatet). TIMEOUTEN är en del av grinden:
#               en kedja som stannar hänger.
add_executable(tooltip_gate tooltip_gate.cpp)
target_link_libraries(tooltip_gate PRIVATE glaze)
target_include_directories(tooltip_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME tooltip_gate COMMAND tooltip_gate)
set_tests_properties(tooltip_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# splitter_gate  DELARENS grind — och de två ramverksytor posten
#                byggde: SLÄPPET (`Widget::mouseUp`) och MUSPEKAREN
#                (`gfx::Window::setCursor`/`Widget::cursorAt`). Headless:
#                layouten mot ett HANDRÄKNAT förväntat värde (AUTO, fast ruta åt båda
#                hållen, minimimåtten, liggande, skala 2). Med ett DOLT
#                fönster genom den riktiga ingången: draget som
#                förhandsvisning, `onChange` EN gång vid släppet (också
#                utanför fönstret), fokusförlust som avbryter, pekarformen
#                under ett drag, explicit capture — och FASADENS lagring av
#                formen före/efter show(), som på Linux är proxyns två
#                vidarebefordringar. 77 = ingen display ⇒ SKIPPED.
add_executable(splitter_gate splitter_gate.cpp)
target_link_libraries(splitter_gate PRIVATE glaze)
add_test(NAME splitter_gate COMMAND splitter_gate)
set_tests_properties(splitter_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# dock_gate      DOCKNINGENS grind — `DockHost` +
#                `DockPanel`. Headless: geometrin mot ett HANDRÄKNAT förväntat värde
#                (celler 1:1 och 2:1, min/max, flikraden och den inaktiva
#                flikens tomma yta, handtagen ytterst, skala 2) och
#                layoutsträngen v2 (tur och retur, ATOMISK läsning — en
#                trasig form per grammatikregel, tal utanför float och NaN —,
#                okända namn, skrivarens filter). Med ett DOLT fönster genom
#                den riktiga ingången: rubrik och flik till panel och kant med
#                den SIMULERADE indikatorn i PIXLAR, skikten, handtag och
#                tabbstopp, fokusregeln, `show()` på en stängd flik och
#                fokusförlust mitt i ett drag. 77 = ingen display ⇒ SKIPPED.
add_executable(dock_gate dock_gate.cpp)
target_link_libraries(dock_gate PRIVATE glaze)
add_test(NAME dock_gate COMMAND dock_gate)
set_tests_properties(dock_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# listview_gate  LISTVYNS grind — och de två ramverksytor posten
#                byggde: musens MODIFIERARE (`gfx::Event::mods` på musens
#                knappar → `AppWindow::modifierKeys`) och DUBBELKLICKET
#                (`Widget::mouseDoubleClick`, räknat av AppWindow mot
#                `gfx::doubleClickMs`). Headless: kollektionsvokabulären,
#                sorteringen (naturlig, stabil, nycklar, indexen är appens),
#                valet (spannet i VISNINGSordningen), kortningen
#                (`Draw::elide` mot en falsk font) och ritningen mot en
#                inspelande renderer. Med ett DOLT fönster genom den riktiga
#                ingången: radträffen mot ett OBEROENDE förväntat värde ur kontraktet,
#                huvudets sortering och avdelare, tangenterna, dubbelklicket
#                (också ett LÅNGSAMT andra klick), Ctrl-/Shift-klick, hjulet
#                och rullningsytan under huvudet. Kolumnhuvudet på
#                flera rader — `Draw::elideLines`, huvudets höjd (1/2/4/AUTO),
#                rubrikens och cellernas justering och pilens plats som
#                koordinater i den inspelande renderern, och huvudets träff
#                över hela höjden i fönstret. Radens
#                färg, en Bitmap-ikon (en riktig bildcache — den interna
#                `image_cache.h` i `Glaze/widgets/src/`) och solo vid
#                släppet. 77 = ingen display ⇒ SKIPPED.
add_executable(listview_gate listview_gate.cpp)
target_link_libraries(listview_gate PRIVATE glaze)
target_include_directories(listview_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME listview_gate COMMAND listview_gate)
set_tests_properties(listview_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# datagrid_gate  DATAGRIDDENS grind. Headless: kompositen (två
#                dolda editorer som inte går att lossa), kolumntypen per
#                INDEX (överlever setColumns), den aktuella cellen och
#                ritningens LOGIK mot en inspelande renderer med en egen stil
#                (gridrad ≠ listrad). Med ett DOLT fönster, som en kedja av
#                postade steg: redigeringens tre starter och sex avslut,
#                fokusförlustens POSTADE onChange (och att det slopas efter
#                setRows), editorns plats mot ett oberoende förväntat värde (även efter
#                sortering och scroll), Tab ut ur tabellen och vallistan. Därtill:
#                cellernas och rubrikernas justering (R6), editorn under
#                ett huvud på tre rader och i kolumnens justering (E24).
add_executable(datagrid_gate datagrid_gate.cpp)
target_link_libraries(datagrid_gate PRIVATE glaze)
add_test(NAME datagrid_gate COMMAND datagrid_gate)
set_tests_properties(datagrid_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# commit_gate    COMMIT-HÄNDELSEN, FOKUSHÄNDELSERNA och MARKERINGEN —
#                och PropertyGrids redigering (en
#                omräkning i onChange får inte stänga editorn efter första tecknet).
#                Headless: `selectAll`/`setSelection` (byte, klampat,
#                nedsnappat, bakåt, inga events), vidarebefordran i
#                kompositerna och att INGEN programmatisk setter fyrar
#                onChange/onCommit. Med ett DOLT fönster genom den riktiga
#                ingången, som en kedja av postade steg (fokushändelserna och
#                fokusförlustens commits verkställs i pump-svansen): designens
#                tabell widget för widget — vilken handling som ger onChange,
#                onCommit eller båda och i vilken ordning — fallen i
#                PropertyGrid, fokus INOM (lost innerst först, focus ytterst
#                först, kompositerna, roten, fönstrets fokusförlust, en riven
#                widget) och `selectAll` ur `onFocus` vid klick och Tab. Förväntat värde
#                är en eventlogg grindens egna handlers skriver.
#                77 = ingen display ⇒ SKIPPED.
add_executable(commit_gate commit_gate.cpp)
target_link_libraries(commit_gate PRIVATE glaze)
add_test(NAME commit_gate COMMAND commit_gate)
set_tests_properties(commit_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# tab_gate       FLIKARNAS grind — `TabControl` + `TabPage`, och
#                trädkrokarna (`childAdded`/`childRemoved`) och tangentvägen
#                (`descendantKeyDown`) posten byggde. Headless: valet (det
#                lagrade talet mot det effektiva, satsordningen), vilken sida
#                som syns, borttagningen (valet följer sidan), ett barn som
#                inte är en sida och ritningens LOGIK mot en inspelande
#                renderer (vilken flik, var — också i en full rad — och
#                rubrikernas färg). Med ett DOLT fönster genom den riktiga
#                ingången: tryck, tangenterna, Ctrl+Tab INIFRÅN sidan (fokus
#                följer, också genom en datagrid), tabbordningen,
#                fokusräddningen och stängknappen.
#                77 = ingen display ⇒ SKIPPED.
add_executable(tab_gate tab_gate.cpp)
target_link_libraries(tab_gate PRIVATE glaze)
add_test(NAME tab_gate COMMAND tab_gate)
set_tests_properties(tab_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# display_gate   SKÄRMENS FYSISKA UPPLÖSNING.
#                Headless: EDID-parsern (med vakten mot monitor-deskriptorer),
#                diagonalformeln och spannet 50–800 dpi, Apples 72-dpi-
#                uppskattning, reserven (96 dpi i fysiska px, aldrig × skalan)
#                och kalibreringen — mot handskrivet förväntat värde.
#                Med ett DOLT fönster: mätningen efter show, att svaret är det
#                INRE fönstrets (proxyn på Linux), att användarskalan inte
#                ändrar millimetern, kalibreringen genom App och reserven i
#                backenden på en antagen skärm. Når `Glaze/gfx/src/` för den interna
#                `display_util.h` (msgbox_gate-prejudikatet).
#                77 = ingen display ⇒ SKIPPED.
add_executable(display_gate display_gate.cpp)
target_link_libraries(display_gate PRIVATE glaze)
target_include_directories(display_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/gfx/src)
add_test(NAME display_gate COMMAND display_gate)
set_tests_properties(display_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# gpu_gate       GRAFIKKORTEN. Headless på alla OS:
#                id:ts form och dubbletterna, tillverkarnamnen, Linux sysfs-
#                läsning mot ett FALSKT träd i en temporär katalog och
#                miljöregeln (NVIDIA-offload, DRI_PRIME, användarens variabel,
#                valbarheten först) mot en falsk getenv — handskrivet förväntat värde.
#                Når `Glaze/gfx/src/` för den interna `gpu_util.h`
#                (display_gate-prejudikatet). Den riktiga backenden mäts i
#                scene3d_pixel_gate K2–K4.
add_executable(gpu_gate gpu_gate.cpp)
target_link_libraries(gpu_gate PRIVATE glaze)
target_include_directories(gpu_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/gfx/src)
add_test(NAME gpu_gate COMMAND gpu_gate)
set_tests_properties(gpu_gate PROPERTIES TIMEOUT 30)

# middle_gate    MITTENKNAPPEN och OVERLAY-VANDRINGEN
#                Headless: `Renderer::clipBounds` mot NullRenderer.
#                Med ett DOLT fönster, genom den riktiga ingången
#                (`handleEvent`): trycket som bubblar, draget och släppet
#                till den som tog det, avbrotten utan middleUp (fokusförlust,
#                unadopt, ett nytt tryck), explicit capture, Crosshair/Move
#                ur den som håller trycket och overlay-lagrens box och klipp
#                (läst inne i drawOverlay). 77 = ingen display ⇒ SKIPPED.
add_executable(middle_gate middle_gate.cpp)
target_link_libraries(middle_gate PRIVATE glaze)
add_test(NAME middle_gate COMMAND middle_gate)
set_tests_properties(middle_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# input_gate     INMATNINGEN genom den riktiga
#                ingången (`handleEvent`) i ett DOLT fönster: högerknappens
#                krokar och snabbmenyn (H), dubbelklicken med alla tre
#                knapparna räknade på den som tog trycket (D — D3 väntar
#                `doubleClickMs() + 150` ms flera gånger), avbrottet
#                `mouseCancel` med nollad bokföring och knappmedveten capture
#                (A), tangenternas fokusenhet, tangentminne, `repeat`,
#                `cancelled`, `keyTaken` och `keyUp`-mottagaren (T), och på
#                D3D11 riktiga Win32-meddelanden (K: WM_CAPTURECHANGED till
#                ett STATIC-fönster, fönstret självt och ett annat
#                glaze-fönster, vårt eget släpp). 77 = ingen display eller
#                SetCapture tog inte i det dolda fönstret ⇒ SKIPPED.
add_executable(input_gate input_gate.cpp)
target_link_libraries(input_gate PRIVATE glaze)
add_test(NAME input_gate COMMAND input_gate)
set_tests_properties(input_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# resize_gate    STORLEKSHÄNDELSEN `onResize`
#                genom den riktiga ritningen och pumpens svans i
#                ett OFFSCREEN-fönster: upptäckten i ritningen och leveransen i
#                svansen (aldrig under `renderFrame`), sammanfogningen, A → B
#                → A, dolda widgets, roten, en handler som lossar sin widget
#                eller stänger fönstret, kaskaden och KEDJESPÄRREN (64
#                leveranser i en händelselös följd, sedan inga förrän en
#                händelse). På D3D11 ett riktigt fönster: den första
#                leveransen före `onShow` och före den första ritningen.
#                77 = inget fönster kunde skapas ⇒ SKIPPED.
add_executable(resize_gate resize_gate.cpp)
target_link_libraries(resize_gate PRIVATE glaze)
add_test(NAME resize_gate COMMAND resize_gate)
set_tests_properties(resize_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# plumbing_gate  RAMVERKETS RÖRMOKERI FÖR RITYTANS 3D-DEL —
#                tre ytor ritytans 3D-del bygger på. DAMAGE-STÄMPELN
#                (`Widget::damageStamp`: egen invalidate +1, ett barns +0,
#                fönstrets fulla +1, ett klick med handler EXAKT +1 genom
#                `handleEvent`, den ritade vägen +0), DE HÅLLNA TANGENTERNA
#                (`AppWindow::isKeyHeld` genom `handleEvent`: före appens
#                krok, numpaden ovikt och vikt, tömningen vid fokusförlust,
#                hide och stängning) och UPPLADDNINGSBUDGETEN
#                (`App::setCanvasUploadBudget`: headless mot en räknande
#                renderer och en riktig `ImageCache`, och genom fönstrets
#                `renderContent`). Egen grind och ingen utökning: ytorna har
#                ännu ingen konsument, och var och en har sitt eget sabotage.
#                Och RENDERARENS RÄKNARE mot fönstrets riktiga
#                renderare: skapade mål och kompileringens tid, med D3D11:s
#                processcache. Når `Glaze/widgets/src/` för den interna
#                `image_cache.h`. 77 = ingen display ⇒ SKIPPED.
add_executable(plumbing_gate plumbing_gate.cpp)
target_link_libraries(plumbing_gate PRIVATE glaze)
target_include_directories(plumbing_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME plumbing_gate COMMAND plumbing_gate)
set_tests_properties(plumbing_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# profiler_gate  PROFILERAREN. Headless: etiketterna från
#                fyra trådar med en läsare under tiden (exakt antal, samma id
#                för samma namn). Med ett DOLT fönster genom PUMPEN: avstängd
#                spelar den in ingenting, bilderna (fönstrets id, innehållet,
#                totalen, rit-anropen mot renderarens räknare), de postade
#                funktionerna i pumpvarven, följden (`continued`) med en
#                tickande rityta, rapporten på och av, och GPU-tiden. Kostnaden
#                per scope skrivs ut, grindas inte. 77 = ingen display, eller
#                ingen GPU-tid att mäta (WARP, llvmpipe) ⇒ SKIPPED.
add_executable(profiler_gate profiler_gate.cpp)
target_link_libraries(profiler_gate PRIVATE glaze)
add_test(NAME profiler_gate COMMAND profiler_gate)
set_tests_properties(profiler_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# placement_gate ETIKETTPLACERINGEN headless: den
#                rena `placeLabels` mot handräknade fall (prioritet, lager,
#                index, den vridna lådans omslutande, luften, collide =
#                false, grannceller, utanför skärmen) och mot en rättfram
#                referens utan rutnät på 2 000 slumpade lådor; determinismen
#                och svaret oberoende av skärmen. Tiden för 10 000 etiketter
#                skrivs ut, men grindas inte. Når den interna
#                `src/canvas/placement.h` (scene_pixel_gate-prejudikatet).
add_executable(placement_gate placement_gate.cpp)
target_link_libraries(placement_gate PRIVATE glaze)
target_include_directories(placement_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME placement_gate COMMAND placement_gate)

# canvas_gate    RITYTANS GESTER OCH TRÄFFPROV.
#                Headless: gesternas tal (camera_math — zoomstegen,
#                nordsnäppets gräns, att trögheten stannar). Med ett DOLT
#                fönster genom den riktiga ingången (`handleEvent`):
#                panoreringen (också under tilt och med mittenknappen),
#                Ctrl+drag, nordsnäppet, hjulet mot pekaren, tangenterna,
#                appens handler först, sättarna som inte fyrar
#                onCameraChange, fokusförlusten, pekaren, overlay-lagret och
#                träffprovet mot ett OBEROENDE skärmavstånd (också
#                bilden på fyra hörn) — och
#                trögheten genom PUMPEN, där det mäts att ritytan slutar be
#                om frames.
#                Når `Glaze/widgets/src/` för den interna `camera_math.h`.
#                77 = ingen display ⇒ SKIPPED.
add_executable(canvas_gate canvas_gate.cpp)
target_link_libraries(canvas_gate PRIVATE glaze)
target_include_directories(canvas_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME canvas_gate COMMAND canvas_gate)
set_tests_properties(canvas_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# trackpad_gate  STYRPLATTAN. Headless: kontaktspåraren
#                (touchpad_rotation — vinkeln, tröskeln, ±180°, handflatan,
#                hybridläget, ny baslinje), vridsnäppet och panoreringens
#                tecken (camera_math). Med ett DOLT fönster genom den riktiga
#                ingången (`handleEvent`), vid skalorna 1 och 1,5:
#                tvåfingersdraget panorerar exakt i px (platt och tiltat,
#                samma tecken som en rullad lista), mushjulet zoomar,
#                nypningen och vridningen kring pekaren (medurs = medurs),
#                snäppet vid släppet (också Ctrl+draget), gesterna av ⇒
#                bubblar, och gestmålet som hålls och släpps. ORBITEN med
#                Ctrl ur HJULETS och GESTENS egen mask
#                och BACKENDENS mask där den går att köra: Win32
#                genom den riktiga WndProc:en, på Linux modifierartangentens
#                bit (`xkb_keys.h`) mot riktiga keymaps — därav xkbcommon.
#                Når `Glaze/widgets/src/` (camera_math.h) och
#                `Glaze/gfx/src/` (touchpad_rotation.h, backend_linux/
#                xkb_keys.h). 77 = ingen display ⇒ SKIPPED.
add_executable(trackpad_gate trackpad_gate.cpp)
target_link_libraries(trackpad_gate PRIVATE glaze)
target_include_directories(trackpad_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src
                                   ${CMAKE_SOURCE_DIR}/Glaze/gfx/src)
if(GLAZE_BACKEND_RESOLVED STREQUAL "linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TRACKPAD_XKB REQUIRED IMPORTED_TARGET xkbcommon)
    target_link_libraries(trackpad_gate PRIVATE PkgConfig::TRACKPAD_XKB)
endif()
add_test(NAME trackpad_gate COMMAND trackpad_gate)
set_tests_properties(trackpad_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# binding_gate   RITYTANS BINDNINGSTABELL. Headless:
#                uppslaget (src/canvas/input_map — delmängd, specificitet,
#                arvet per rad, lägenas reserv, None, dragets plan) och
#                mängderna (camera_math `bindingAmount`, tecknet). Med ett
#                DOLT fönster genom den riktiga ingången: förvalen = ritytans
#                gester, vändningarna, omflyttningarna (också korsade och
#                högerknappen), lägena, förstapersonen och flygläget som
#                rader (tests/orbit_fly.h). Når `Glaze/widgets/src/`.
#                77 = ingen display ⇒ SKIPPED.
add_executable(binding_gate binding_gate.cpp)
target_link_libraries(binding_gate PRIVATE glaze)
target_include_directories(binding_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME binding_gate COMMAND binding_gate)
set_tests_properties(binding_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# wheel_gate     RULLNINGENS RIKTNING i ett DOLT
#                fönster: appens, fönstrets och widgetens vändning med arv,
#                Shift+mushjul i sidled, ritytans råa hjul, den fångade
#                widgeten och arvets gräns vid fönstrets rot.
#                77 = ingen display ⇒ SKIPPED.
add_executable(wheel_gate wheel_gate.cpp)
target_link_libraries(wheel_gate PRIVATE glaze)
add_test(NAME wheel_gate COMMAND wheel_gate)
set_tests_properties(wheel_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# canvas3d_gate  RITYTANS INTERAKTION I 3D.
#                Headless: orbitens tal och att en kamera utanför spannen inte
#                dras in med ett hopp (camera_math). Med ett DOLT fönster genom
#                den riktiga ingången (`handleEvent`), mot grindens EGEN
#                hålkamera: panoreringen (målplanets punkt under pekaren,
#                perspektiv och ortografiskt, mitten och Shift), orbiten,
#                elevationens spärr, spann och tumling, dollyn med sitt spann,
#                tangenterna, verktygen (orbitBy/panBy/dollyBy, zoomToBox,
#                easeTo med 3D-målets landning), mittenknappen till appen,
#                onGesture och ett TextInput PÅ ritytan — och genom PUMPEN:
#                tickningen (tagen tangent, KeyUp, fokusförlust, setTicking —
#                och inga bildrutor efteråt) och caret-blinket utan att 3D-målet
#                ritas om. Når `Glaze/widgets/src/` för de interna
#                `canvas/camera_math.h` och `canvas/resources.h`.
#                77 = ingen display ⇒ SKIPPED.
add_executable(canvas3d_gate canvas3d_gate.cpp)
target_link_libraries(canvas3d_gate PRIVATE glaze)
target_include_directories(canvas3d_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME canvas3d_gate COMMAND canvas3d_gate)
set_tests_properties(canvas3d_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# canvas_perf    RITYTANS PRESTANDA. En riktig
#                Canvas i ett DOLT fönster, 1280 × 800: ≈ 1 miljon vertex i
#                fyra lager och 10 000 kolliderande etiketter, och sex serier
#                kameraändringar. Grinden är DETERMINISTISK: en kameraändring
#                laddar upp högst en byte per etikett och skapar ingen
#                buffert, ingenting byggs om, placeringen körs en gång per
#                ändring, och rit-anropen ≤ det innehållet kräver (räknat ur
#                scenens former) + 8. Tiderna och GPU-adaptern SKRIVS UT men
#                grindas inte. INGEN SKIP_RETURN_CODE (clip_gate-beslutet).
#                Når `Glaze/widgets/src/` för den interna `canvas/resources.h`;
#                på Windows länkas DXGI för adapterns namn.
add_executable(canvas_perf canvas_perf.cpp)
target_link_libraries(canvas_perf PRIVATE glaze)
if(WIN32)
    target_link_libraries(canvas_perf PRIVATE dxgi)
endif()
target_include_directories(canvas_perf
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME canvas_perf COMMAND canvas_perf)
set_tests_properties(canvas_perf PROPERTIES TIMEOUT 300)

# canvas_perf3d  RITYTANS 3D-PRESTANDA. En riktig
#                Canvas i ett DOLT fönster, 1280 × 800, med en Camera3D:
#                gallerstolpar i fyra detaljnivåer (24 om 20 k trianglar, och
#                en ring om 360 som delar en mesh), 10 000 instansierade träd
#                och 600 000 punkter. DETERMINISTISKT: en kameraändring laddar
#                upp 0 byte och skapar 0 buffertar (U), inget byggs om (R), en
#                ersatt mesh strömmar inom budgeten och den gamla ritas tills
#                den nya är komplett — och sedan ber ritytan inte om fler
#                frames, genom pumpen (S), detaljnivåerna (L, T), gallringen
#                mot grindens egen synsektor och pyramid (C, T, P). Tiderna och
#                GPU:n SKRIVS UT men grindas inte; `--full` kör de verkliga
#                storlekarna (stolpar om 200 k, 100 000 träd, 6 M punkter) för
#                hand. Når `Glaze/widgets/src/` för de interna
#                canvas/resources.h, residency.h, instances.h och mesh_data.h.
#                INGEN SKIP_RETURN_CODE (clip_gate-beslutet).
add_executable(canvas_perf3d canvas_perf3d.cpp)
target_link_libraries(canvas_perf3d PRIVATE glaze)
target_include_directories(canvas_perf3d
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME canvas_perf3d COMMAND canvas_perf3d)
set_tests_properties(canvas_perf3d PROPERTIES TIMEOUT 600)

# app_perf       EN APP PÅ RIKTIGT: en kartvy och en 3D-vy i två riktiga
#                fönster som pumpas av run(), med en skriptad session (hover,
#                panorering och zoom med mittenknapp och hjul, en resa 8 km,
#                förstaperson i 3D med hållna tangenter och hopp om 3 km) vars
#                input går genom AppWindow::handleEvent. Plattor (PNG),
#                punktmoln och 3D-rutor läses in i arbetstrådar och släpps
#                efter kameran. Mäter frametiden ur onTick, input till
#                ritning, renderarens anrop och uppladdning, strömningen och
#                MINNET per steg (processens, Windows DXGI per process,
#                bildcachen). Grindar bara att sessionen går igenom och att
#                strömningen kommer ikapp — tiderna och minnet är mätningen
#               . ctest kör DOLDA fönster; `--synlig`
#                för hand, och `--varv N` kör samma varv N gånger och mäter om
#                minnet växer per varv (läckorna). Klockan, minnet, energin,
#                arbetstrådarna och sessionen är mätselens (perf_harness).
#
# perf_harness   MÄTSELEN som app_perf och terrain_perf
#                delar — inget program, ingen grind: klockan, minnet och
#                energin per process, plattornas PNG, arbetstrådarna,
#                stolpen och trädet, och sessionen över N mätta fönster.
#                Statisk; den når `Glaze/widgets/src/` för bildcachens byte och
#                ritytans chunkar och länkar glaze_assets för PNG-kodningen, på
#                Windows DXGI och psapi för minnet. Programmen länkar `glaze`
#                DIREKT också (programfilens ankare i delat läge, CLAUDE.md
#                DELAT LÄGE (5)).
add_library(perf_harness STATIC perf/harness.cpp)
target_link_libraries(perf_harness PUBLIC glaze PRIVATE glaze_assets)
if(WIN32)
    target_link_libraries(perf_harness PRIVATE dxgi psapi)
endif()
target_include_directories(perf_harness
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)

add_executable(app_perf app_perf.cpp)
target_link_libraries(app_perf PRIVATE perf_harness glaze)
add_test(NAME app_perf COMMAND app_perf)
set_tests_properties(app_perf PROPERTIES TIMEOUT 900)

# terrain_perf   TERRÄNGKARTAN OCH 3D-VYN:
#                kartan tiltad med terräng och drapering, baskartan i
#                batcher, N ledningar med fyra lager vardera och 3D-appens
#                ändringar ("idag" bygger om alla objektlager, "rättad" bara
#                det ändrade), och 3D-vyn i ett eget fönster med himlen som
#                eget program, MSAA 8, punktmoln, stolpar och träd. 27 steg —
#                dynamiska, med ALLT SAMTIDIGT som huvudmått — mätta med
#                PROFILERAREN (intervallen, faserna och räknarna per bild).
#                Grindar bara att sessionen går, att strömningen kommer ikapp,
#                att hovret träffar och att det orörda steget ritar 0 bilder
#                (hård regel 1); tiderna är mätningen.
#                ctest kör DOLDA fönster; `--synlig`, `--hp`, `--skärm N`,
#                `--objekt N`, `--mål Hz` och `--steg N` för hand.
add_executable(terrain_perf terrain_perf.cpp)
target_link_libraries(terrain_perf PRIVATE perf_harness glaze)
add_test(NAME terrain_perf COMMAND terrain_perf)
set_tests_properties(terrain_perf PROPERTIES TIMEOUT 900)

# stockicon_sheet  STANDARDIKONERNAS ARK — INGEN GRIND och inget
#                  add_test: varje `glaze::StockIcon` i 16/24/32/48, ljust och
#                  mörkt sida vid sida, som PNG (`stockicon_sheet ut.png`).
#                  Ögats underlag när stilen bedöms; mäter ingenting.
add_executable(stockicon_sheet stockicon_sheet.cpp)
target_link_libraries(stockicon_sheet PRIVATE glaze)

# filedialog_gate  FILVÄLJARDIALOGENS grind. Headless: den rena
#                  std-modellen — filtren, ändelsen, namnfältets citatform,
#                  namnordningen, storlekstexten, katalogläsningen och felen i
#                  ett träd grinden SJÄLV skapar i temp, sökvägarna och
#                  sorteringsnycklarna — och `gfx::knownPlaces` mot fasadens
#                  kontrakt. Med ett DOLT dialogfönster genom den riktiga
#                  ingången, som en kedja av postade steg inne i dialogens
#                  modala pump: navigeringen, valet och OK, Escape, sparandets
#                  ändelse, överskrivningsfrågan (den riktiga meddelanderutan),
#                  mappläget, tabbordningen, filtypsbytet, flervalet, en mapp
#                  som försvinner, språkbytet och kryssrutan för DOLDA filer
#                  (listan byts direkt, valet skrivs tillbaka också
#                  vid Avbryt, på Windows ett riktigt HIDDEN-attribut). Når
#                  `Glaze/widgets/src/` för de interna `file_dialog_model.h`/
#                  `file_dialog_window.h` (tooltip_gate-prejudikatet).
#                  77 = ingen display ⇒ SKIPPED.
add_executable(filedialog_gate filedialog_gate.cpp)
target_link_libraries(filedialog_gate PRIVATE glaze)
target_include_directories(filedialog_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME filedialog_gate COMMAND filedialog_gate)
set_tests_properties(filedialog_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# filepicker_gate  FILVÄLJARFÄLTETS grind. Headless: kompositen (två
#                  riktiga barn som inte går att lossa, disable() når båda),
#                  setProp-vokabulären, filtrens kollektionsvokabulär och att
#                  programmatisk setPath aldrig fyrar. Med ett DOLT ägarfönster
#                  genom den riktiga ingången: måttet, placeringen mot ett
#                  oberoende förväntat värde, tabbordningen, skrivningen — och dialogen
#                  genom produktens EGEN `FileDialog::run`, besvarad inifrån
#                  sin modala pump — och att kryssrutan för dolda filer står
#                  kvar till nästa "…", också efter Avbryt. Når
#                  `Glaze/widgets/src/` för de interna
#                  `file_dialog_model.h`/`file_dialog_window.h`.
#                  77 = ingen display ⇒ SKIPPED.
add_executable(filepicker_gate filepicker_gate.cpp)
target_link_libraries(filepicker_gate PRIVATE glaze)
target_include_directories(filepicker_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME filepicker_gate COMMAND filepicker_gate)
set_tests_properties(filepicker_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# colorpicker_gate  FÄRGVÄLJARENS grind. Headless: textformen
#                   `#RRGGBB` mot ett HANDSKRIVET förväntat värde (giltiga, ogiltiga som
#                   lämnar färgen orörd, kanonisk skrivning, alltid opak),
#                   användare mot program och setProp. Med ett DOLT ägarfönster
#                   och popupen som RIKTIGT popupfönster, allt genom ÄGARENS
#                   riktiga ingång: att innehållet ryms i den storlek som
#                   räknades före visningen, palettens markör och val, Tab till
#                   reglagen och hexfältet, reglagens LIVE-ändringar och
#                   hexfältets COMMIT (Enter, Tab) med deras event,
#                   Escape, fokusförlusten, toggeln, den inaktiverade — och
#                   att popupen TAR TANGENTBORDET
#                   (`setPopupTakesKeys`): ägarens fokus står kvar medan
#                   tangenterna går till popupen. Når `Glaze/widgets/src/` för
#                   den interna färgytan och färgdialogen
#                   (tooltip_gate-prejudikatet). `--png <katalog>` skriver
#                   kontrollen och popupen per UI × läge att ögna.
#                   77 = ingen display ⇒ SKIPPED.
add_executable(colorpicker_gate colorpicker_gate.cpp)
target_link_libraries(colorpicker_gate PRIVATE glaze)
target_include_directories(colorpicker_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME colorpicker_gate COMMAND colorpicker_gate)
set_tests_properties(colorpicker_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# datepicker_gate  DATUMVÄLJARENS grind. Headless: den rena
#                  std-modellen (`Glaze/widgets/src/date_model.h` — giltighet,
#                  ISO-texten åt båda hållen, klampning, månadssteg och
#                  månadsrutnätets första ruta) mot ett HANDSKRIVET förväntat värde,
#                  språkets veckostart och namn, och widgetens API (dagens
#                  datum, klampning vid läsning, användare mot program,
#                  fältets tolkning). Med ett DOLT ägarfönster och popupen som
#                  RIKTIGT popupfönster, genom ÄGARENS riktiga ingång:
#                  öppningarna, markörens tangenter, valet, Escape/Tab,
#                  gränserna, fältets skrivning, hjulet över den stängda,
#                  toggeln — och Wayland-vägen direkt till popupen.
#                  `--png <katalog>` skriver kontrollen och kalendern per UI ×
#                  läge att ögna. 77 = ingen display ⇒ SKIPPED.
add_executable(datepicker_gate datepicker_gate.cpp)
target_link_libraries(datepicker_gate PRIVATE glaze)
target_include_directories(datepicker_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME datepicker_gate COMMAND datepicker_gate)
set_tests_properties(datepicker_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# textarea_gate  DET FLERRADIGA FÄLTETS grind. Headless: fältets
#                DELADE kod (`Glaze/widgets/src/text_field.h` — filtren,
#                ordet, teckengränserna) och widgetens API (radslutens
#                normalisering, setProp, programmatiskt aldrig onChange, caret
#                på teckengräns efter setText i BÅDA fälten). Med ett DOLT
#                fönster och genom dess RIKTIGA ingång: skrivning och Enter,
#                Upp/Ner med bevarat x-läge, brytningen och den visuella
#                radens Home/End, hårdbrytningens sida, markering och urklipp
#                över rader (CRLF på Windows), skrivskyddat, rullningen och
#                PageDown, hjulet utan fokus och dess bubbling, musens tryck,
#                drag, autoscroll och dubbelklick, den inkrementella
#                layouten mot en hel omläggning, UTF-8 och Tab. Använder det
#                RIKTIGA urklippet och skriver tillbaka dess text.
#                77 = ingen display ⇒ SKIPPED.
add_executable(textarea_gate textarea_gate.cpp)
target_link_libraries(textarea_gate PRIVATE glaze)
target_include_directories(textarea_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME textarea_gate COMMAND textarea_gate)
set_tests_properties(textarea_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# markdown_gate  MARKDOWN-VYNS grind. Del P: parsern
#                (ren std, `Glaze/widgets/src/markdown/`) dumpad som ett AST
#                mot ett HANDSKRIVET förväntat värde ur CommonMark/GFM-reglerna; del L:
#                layouten och widgetens mätning headless mot en inspelande
#                renderer och en stegfont per stil (vanlig/fet/mono), via
#                fönstrets `FontSet`; del W: widgeten i ett DOLT fönster genom
#                dess riktiga ingång — länkarna, pekaren, tooltipen, ankarna
#                och vakten. Laddar demons assets.bin (L6: vyns bildtabell före
#                asseten). `-v` skriver layouttiden för 200 KB. 77 = ingen
#                display ⇒ SKIPPED.
add_executable(markdown_gate markdown_gate.cpp)
target_link_libraries(markdown_gate PRIVATE glaze)
target_include_directories(markdown_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
target_compile_definitions(markdown_gate
                           PRIVATE GLAZE_TEST_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME markdown_gate COMMAND markdown_gate)
set_tests_properties(markdown_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# textfield_gate TEXTFÄLTENS grind. Headless: ångra-
#                historiken (`Glaze/widgets/src/text_field.h` — grupperingen
#                efter ord, kapningen av gör om, djupet 100 och budgeten 4 MiB),
#                TextInput och TextArea genom sina krokar (ångra/gör om, ett
#                onChange per steg, markeringen återställd, vad som förseglar
#                en grupp, setText som tömmer), lösenordet mot en testfont där
#                masken har ett eget teckensteg (caret, klick och ritningens
#                strängar i en blandad 1–4-byte-text, ingen historik, Ctrl+X
#                raderar inte), AltGr-regeln (`editShortcut`), MAXLÄNGDEN i
#                kodpunkter och PLATSHÅLLAREN i `textDim` (H6,
#                H7, båda fälten). Med ett DOLT
#                fönster och genom dess RIKTIGA ingång: fältets Ctrl+Z vinner
#                över menyradens accelerator, och lösenordsfältets Ctrl+C/X
#                lämnar det RIKTIGA urklippet orört (dess text skrivs
#                tillbaka). 77 = ingen display, eller ett urklipp som inte tar
#                emot text (Wayland) ⇒ SKIPPED.
add_executable(textfield_gate textfield_gate.cpp)
target_link_libraries(textfield_gate PRIVATE glaze)
target_include_directories(textfield_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME textfield_gate COMMAND textfield_gate)
set_tests_properties(textfield_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# dialog_gate    STANDARD- OCH AVBRYTKNAPPEN.
#                Headless: knappens `dialogResult`/`performClick` och listorna
#                som tar Enter bara med `onActivate`. Med DOLDA fönster och
#                genom deras RIKTIGA ingång: Enter/Escape sist före Tab (fält,
#                knapp, TextArea, modifierarna), oanvändbara knappar, en dödd
#                knapp, standardknappens roll och invalidering, `run(ägare)` med
#                vetot, meddelanderutans sex uppsättningar, talfältets och
#                datumväljarens commit + bubbla, de fyra kontrollerna
#                förfadern, acceleratorerna, Escape med öppen
#                meny/popup, en handler som river eller flyttar knappen,
#                trädmutationen, en öppen popup UTAN fokus och
#                egenskapslistans vallista och
#                standardknappen i PIXLAR i alla tre
#                UI. Når `Glaze/widgets/src/` för `message_box_table.h`
#                (msgbox_gate-prejudikatet). Mäter INTE utseendet (färgvärden),
#                backendernas tangentöversättning eller GlazeBuilders duk.
#                Meddelanderutan visas ett ögonblick (den kan inte göras dold).
#                77 = ingen display ⇒ SKIPPED.
add_executable(dialog_gate dialog_gate.cpp)
target_link_libraries(dialog_gate PRIVATE glaze)
target_include_directories(dialog_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME dialog_gate COMMAND dialog_gate)
set_tests_properties(dialog_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# checkbox_gate  KRYSSRUTANS TRE LÄGEN, headless:
#                WinForms-cykeln med och utan `threeState`, `isChecked` i
#                mellanläget, användare mot program och `setProp`.
add_executable(checkbox_gate checkbox_gate.cpp)
target_link_libraries(checkbox_gate PRIVATE glaze)
add_test(NAME checkbox_gate COMMAND checkbox_gate)

# range_gate     REGLAGETS och RULLNINGSLISTENS värde klampat VID LÄSNING
#, headless: gränserna, satsordningen
#                mot `setMax`, min > max och att en `setMax` som flyttar värdet
#                inte ger ett falskt `onCommit`.
add_executable(range_gate range_gate.cpp)
target_link_libraries(range_gate PRIVATE glaze)
add_test(NAME range_gate COMMAND range_gate)

# statusbar_gate STATUSRADENS FÄLT. Headless:
#                kollektionsvokabulären, `setPanels` på plats mot ett
#                adopterat barn och ett fält som tas bort som ett barn. Med ett
#                DOLT fönster: bredderna i flexen (fast, AUTO, två fyllande).
#                77 = ingen display ⇒ SKIPPED (headless-delen är mätt).
add_executable(statusbar_gate statusbar_gate.cpp)
target_link_libraries(statusbar_gate PRIVATE glaze)
add_test(NAME statusbar_gate COMMAND statusbar_gate)
set_tests_properties(statusbar_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# tray_gate      SYSTEMFÄLTETS grind. Lägger ALDRIG en ikon i
#                utvecklarens systemfält: `TrayIcon::show()` anropas bara med
#                Linux-backenden, som saknar fält. Headless: den rena
#                modellen (`Glaze/widgets/src/tray_model.h` — animationen,
#                routningen och menyns ankare) mot ett HANDRÄKNAT förväntat värde, ikonens
#                ramar med fältets tak och cachen i båda ordningarna
#                (`window_icon.h`), vokabulären på en riktig men DOLD ikon och
#                dispatchen genom registret (`tray_registry.h` — okänt id, dold
#                ikon, riven ikon, lägsta lediga id; med Linux-backenden också
#                handlerordningen och en handler som river ikonen). Med
#                fönster: aktivitetsfältets ägarfönster (bara D3D11 —
#                `FindWindowW`, `GW_OWNER`, offscreen) och DOLD START (H1 och H2
#                VISAR ett riktigt fönster ett ögonblick, för OS-fönstrets
#                synlighet är det som mäts). Når `Glaze/widgets/src/` för de
#                interna headrarna (tooltip_gate-prejudikatet) och glaze_assets
#                för `kIconSizes`. 77 = ingen display ⇒ SKIPPED.
add_executable(tray_gate tray_gate.cpp)
target_link_libraries(tray_gate PRIVATE glaze glaze_assets)
target_include_directories(tray_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME tray_gate COMMAND tray_gate)
set_tests_properties(tray_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# timer_gate     APPENS TIMER: riktiga timrar i offscreen-
#                fönstrens pumpar (före första fönstret, en-gångs, omstart,
#                riven i sin handler, en modal i handlern) och leveransregeln
#                med SYNTETISKA ticks genom `src/timer_registry.h` (EN tick per
#                svans, gammal körning, ett fönsters id, återinträdet med två
#                förfallna timrar och en nästlad pump). En vakthundstråd
#                stänger fönstret efter 5 s, så ett fel blir ett fall och inte
#                ett häng. 77 = ingen display eller ingen timer ⇒ SKIPPED.
add_executable(timer_gate timer_gate.cpp)
target_link_libraries(timer_gate PRIVATE glaze)
target_include_directories(timer_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src)
add_test(NAME timer_gate COMMAND timer_gate)
set_tests_properties(timer_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# geometry_gate  FÖNSTRETS PLATS, LÄGE OCH RAM: startplatsen
#                headless (`src/window_placement.h` mot handräknat förväntat värde i
#                syntetiska skärmlayouter) och med fönster: positionen före och
#                efter öppning, normalrutan under maximering, kortet efter
#                stängning, den latta namnlisten och CenterOwner. W3, W4 och
#                W6b VISAR ett riktigt fönster ett ögonblick (bara D3D11).
#                STORLEKSRAMEN: KM headless (Linux marginal-
#                kanter, `Glaze/gfx/src/frame_margin.h` — därför den interna
#                sökvägen), K med visade fönster och WM_NCHITTEST (bara
#                D3D11 — dwmapi för DWM:s synliga kant i K10) och KL (Linux).
#                Offscreen: SB (rader mot nederkanten i ett
#                fönster som följer innehållet), SG (`GrowOnly` — innehållet
#                som golv) och BG (breddgruppens omritning).
#                77 = ingen display ⇒ SKIPPED.
add_executable(geometry_gate geometry_gate.cpp)
target_link_libraries(geometry_gate PRIVATE glaze)
if(WIN32)
    target_link_libraries(geometry_gate PRIVATE dwmapi)
endif()
target_include_directories(geometry_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/widgets/src
                                   ${CMAKE_SOURCE_DIR}/Glaze/gfx/src)
add_test(NAME geometry_gate COMMAND geometry_gate)
set_tests_properties(geometry_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# capture_gate   MUSENS CAPTURE: `hasPointerCapture`,
#                avbrottets krok (`mouseCancel` och `pointerCaptureLost`,
#                sammanslagna), `MouseCaptureLost` och bokföringens
#                ordning mot drag-and-drop-avbrottet genom fönstrets riktiga
#                ingång i ett dolt fönster (alla plattformar), och på D3D11
#                riktiga Win32-meddelanden: WM_CAPTURECHANGED →
#                `MouseCaptureLost` (och när det INTE blir en) i dolda
#                fönster, och — med ett VISAT värdfönster som förgrund —
#                övertagandet `takePointerCapture` mellan tre fönster, dess
#                återinträde och `showInactive` (fönster visas ett ögonblick).
#                77 = ingen display, SetCapture tog inte i det dolda fönstret,
#                eller ingen förgrund ⇒ SKIPPED.
add_executable(capture_gate capture_gate.cpp)
target_link_libraries(capture_gate PRIVATE glaze)
add_test(NAME capture_gate COMMAND capture_gate)
set_tests_properties(capture_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# cursor_gate    PEKARNA: den stängda handens bild
#                (`Glaze/gfx/src/cursor_bitmaps.h`, ren std — därför den
#                interna sökvägen) pixel för pixel mot mönstret, `setProp`-
#                formerna, och omprövningen genom fönstrets riktiga ingång i ett
#                offscreen-fönster: settrarna, disable/hide, väntepekaren, en
#                lossad widget och ritytans gester. 77 = ingen display ⇒
#                SKIPPED.
add_executable(cursor_gate cursor_gate.cpp)
target_link_libraries(cursor_gate PRIVATE glaze)
target_include_directories(cursor_gate
                           PRIVATE ${CMAKE_SOURCE_DIR}/Glaze/gfx/src)
add_test(NAME cursor_gate COMMAND cursor_gate)
set_tests_properties(cursor_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# ---- SIDOPAKETEN — grindarna länkar paketens EGNA target.
#
# threads_gate     KÄRNAN: poolens FIFO-ordning och kapacitet (högst OCH
#                  faktiskt N samtidigt), rivningen, Workerns värde/undantag/
#                  utplattning, avbrytningen (fortsättningen körs ALDRIG),
#                  dtorns väntan och leveransen i en POSTANDE kontext som
#                  grinden dränerar själv. Länkar BARA glaze_threads — att den
#                  bygger är beviset för att kärnan står utan glaze.
# threads_ui_gate  UI-BRYGGAN: självregistreringen, svaret i UI-tråden (även
#                  ett som kommer medan pumpen SOVER), Worker som dör eller
#                  avbryter med svaret i App-kön. Pumpen körs i ett DOLT
#                  fönster (post_gate-prejudikatet); 77 = ingen display ⇒
#                  SKIPPED, aldrig grönt. Ett häng är ett fel — därav TIMEOUT.
add_executable(threads_gate threads_gate.cpp)
target_link_libraries(threads_gate PRIVATE glaze_threads)
add_test(NAME threads_gate COMMAND threads_gate)
set_tests_properties(threads_gate PROPERTIES TIMEOUT 60)

add_executable(threads_ui_gate threads_ui_gate.cpp)
target_link_libraries(threads_ui_gate PRIVATE glaze_threads_ui)
add_test(NAME threads_ui_gate COMMAND threads_ui_gate)
set_tests_properties(threads_ui_gate PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

# net_gate   ADRESSER, DNS, TCP, UDP, PING — bara loopback och "localhost",
#            aldrig en extern värd. Adresserna mot ett handskrivet RFC 5952-
#            förväntat värde; TCP/UDP-utbyten, tak, avbrott (DIREKT, utan polling),
#            Refused/AddressInUse/Closed; det frikopplade OS-anropet mot en
#            FALSK långsam funktion (därför når den Networking/src för den
#            interna detached.h — msgbox_gate-prejudikatet). 77 = OS:et
#            nekade ICMP-socketen (Linux ping_group_range): bara svarsfallen
#            för ping kunde inte köras, och felets text är ändå mätt.
# http_gate  HTTP genom OS:ets stack mot en server som grinden SJÄLV startar
#            på 127.0.0.1 + ::1 (paketets eget TCP-lager). HTTPS kan inte
#            grindas utan nät — det är en rad per OS i doc/os/*/TESTS.md.
if(TARGET glaze_net)
    add_executable(net_gate net_gate.cpp)
    target_link_libraries(net_gate PRIVATE glaze_net)
    target_include_directories(net_gate PRIVATE ${CMAKE_SOURCE_DIR}/Networking/src)
    add_test(NAME net_gate COMMAND net_gate)
    set_tests_properties(net_gate PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

    add_executable(http_gate http_gate.cpp)
    target_link_libraries(http_gate PRIVATE glaze_net)
    add_test(NAME http_gate COMMAND http_gate)
    set_tests_properties(http_gate PROPERTIES TIMEOUT 120)
endif()

# terminal_gate     TERMINALENS KÄRNA headless: tolken och
#                   skärmen mot handskrivet förväntat värde, teckenbredden,
#                   kommandoraden, historiken och sessionen mot en RIKTIG
#                   process (terminal_probe) — storleken, rått läge, taket,
#                   den delade hämtningen, kill() och miljön. Länkar BARA
#                   glaze_terminal. 77 = ingen process gick att starta.
# terminal_ui_gate  WIDGETEN: registret, tangentkodningen, inklistringen,
#                   färgernas kontrast i alla tjugo paletter och i ett DOLT
#                   fönster routingen mot proben, markeringen, följningen och
#                   2026. 77 = ingen display eller ingen process.
# terminal_probe    hjälpprogrammet de två kör i pty:n (ingen grind själv).
if(TARGET glaze_terminal)
    add_executable(terminal_probe terminal_probe.cpp)
    target_link_libraries(terminal_probe PRIVATE Threads::Threads)

    add_executable(terminal_gate terminal_gate.cpp)
    target_link_libraries(terminal_gate PRIVATE glaze_terminal)
    target_compile_definitions(terminal_gate PRIVATE
                               TERMINAL_PROBE="$<TARGET_FILE:terminal_probe>")
    add_dependencies(terminal_gate terminal_probe)
    add_test(NAME terminal_gate COMMAND terminal_gate)
    set_tests_properties(terminal_gate PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
endif()
if(TARGET glaze_terminal_ui)
    add_executable(terminal_ui_gate terminal_ui_gate.cpp)
    target_link_libraries(terminal_ui_gate PRIVATE glaze glaze_terminal_ui)
    # keys.h är intern (Terminal/src/ui) — textarea_gate-prejudikatet.
    target_include_directories(terminal_ui_gate PRIVATE ${CMAKE_SOURCE_DIR}/Terminal/src/ui)
    target_compile_definitions(terminal_ui_gate PRIVATE
                               TERMINAL_PROBE="$<TARGET_FILE:terminal_probe>")
    add_dependencies(terminal_ui_gate terminal_probe)
    add_test(NAME terminal_ui_gate COMMAND terminal_ui_gate)
    set_tests_properties(terminal_ui_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

# openxr_math_gate  OPENXR-PAKETETS KÄRNA headless: axelbytet OpenXR → Glaze,
#                   förankringen, världslåsningen, kameravinklarna mot
#                   Camera3D:s konvention (yaw, pitch, rollens tecken, polerna),
#                   stora koordinater och den symmetriska projektionen mot
#                   asymmetriska synfält — mot analytiska fall. Länkar BARA
#                   glaze_openxr. Finns med -DGLAZE_OPENXR=ON.
if(TARGET glaze_openxr)
    add_executable(openxr_math_gate openxr_math_gate.cpp)
    target_link_libraries(openxr_math_gate PRIVATE glaze_openxr)
    add_test(NAME openxr_math_gate COMMAND openxr_math_gate)
    set_tests_properties(openxr_math_gate PROPERTIES TIMEOUT 30)
endif()

# ---- DELAT BIBLIOTEK — EN instans av ramverket
#      per process. Hela sviten ovan byggs också mot de delade biblioteken
#      (-DGLAZE_SHARED=ON): auto-exporten gör de interna symbolerna nåbara, och
#      att lägena beter sig likadant är det sviten visar.
#
# dll_gate     REGELN i den STATISKA sviten, vid varje commit: inget
#              föränderligt statiskt tillstånd i en header under Glaze/,
#              Threads/ eller Networking/ (i delat läge en kopia per modul på
#              Windows). Del A läser ARKIVEN — COMDAT-datasektionerna med
#              objdump -h (PE) eller readelf -g (ELF) — och del B HEADRARNA.
#              Kontrollen är handskriven: tests/fixtures/dll_gate/ och
#              dll_gate_fixture (-fdata-sections, så att en vanlig global får
#              samma sektionsnamnform som en COMDAT) med känt förväntat värde. Ren std —
#              länkar inte glaze. Byggs bara statiskt: ett delat bygge har inga
#              arkiv. 77 = del A kunde inte köras (macOS, MSVC, inget verktyg),
#              del B grön.
# shared_gate  BARA i delat läge: programmet laddar en plugin (MODULE) i
#              körtid med OS:ets laddare och jämför App, utseendet, språket,
#              resurserna, widget- och komponentregistret, timerregistret och
#              trådpaketets pool och kontext med pluginens — samma objekt.
#              Pluginen avlastas aldrig. 77 = fönsterfallen kunde inte
#              köras (ingen display).
if(NOT GLAZE_SHARED)
    add_library(dll_gate_fixture STATIC dll_gate_fixture.cpp)
    target_include_directories(dll_gate_fixture PRIVATE fixtures/dll_gate)
    if(NOT MSVC)
        target_compile_options(dll_gate_fixture PRIVATE -fdata-sections)
    endif()

    add_executable(dll_gate dll_gate.cpp)
    set(_dll_gate_libs glaze_impl common_gfx glaze_assets glaze_language
                       glaze_threads builder_preview builder_core)
    foreach(_lib glaze_threads_ui_impl glaze_net glaze_terminal
                 glaze_terminal_ui_impl)
        if(TARGET ${_lib})
            list(APPEND _dll_gate_libs ${_lib})
        endif()
    endforeach()
    set(_dll_gate_args "")
    if(CMAKE_OBJDUMP)
        list(APPEND _dll_gate_args --objdump "${CMAKE_OBJDUMP}")
    endif()
    if(CMAKE_READELF)
        list(APPEND _dll_gate_args --readelf "${CMAKE_READELF}")
    endif()
    list(APPEND _dll_gate_args
        --control-lib $<TARGET_FILE:dll_gate_fixture>
        --control-headers ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dll_gate
        --headers ${CMAKE_SOURCE_DIR}/Glaze ${CMAKE_SOURCE_DIR}/Threads
                  ${CMAKE_SOURCE_DIR}/Networking ${CMAKE_SOURCE_DIR}/Terminal
        --libs)
    foreach(_lib IN LISTS _dll_gate_libs)
        list(APPEND _dll_gate_args $<TARGET_FILE:${_lib}>)
    endforeach()
    add_test(NAME dll_gate COMMAND dll_gate ${_dll_gate_args})
    set_tests_properties(dll_gate PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    add_dependencies(dll_gate dll_gate_fixture ${_dll_gate_libs})
endif()

if(GLAZE_SHARED AND TARGET glaze_threads_ui)
    add_library(shared_gate_plugin MODULE shared_gate_plugin.cpp)
    target_link_libraries(shared_gate_plugin PRIVATE glaze glaze_threads_ui)
    # D13 — BARA pluginen länkar terminalen: dess ankare laddar DLL:en.
    if(TARGET glaze_terminal_ui)
        target_link_libraries(shared_gate_plugin PRIVATE glaze_terminal_ui)
    endif()

    add_executable(shared_gate shared_gate.cpp)
    target_link_libraries(shared_gate PRIVATE glaze glaze_threads_ui
                                              ${CMAKE_DL_LIBS})
    target_compile_definitions(shared_gate PRIVATE
        SHARED_GATE_PLUGIN="$<TARGET_FILE:shared_gate_plugin>"
        SHARED_GATE_ASSETS="${CMAKE_SOURCE_DIR}/apps/demo/assets.bin")
    add_dependencies(shared_gate shared_gate_plugin)
    if(TARGET glaze_terminal_ui)
        target_compile_definitions(shared_gate PRIVATE SHARED_GATE_TERMINAL)
    endif()
    add_test(NAME shared_gate COMMAND shared_gate)
    set_tests_properties(shared_gate PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

# SIST: i delat läge får varje program i tests/ GlazeUI:s DLL:er bredvid sig
# (glaze_runtime_dlls, cmake/GlazeShared.cmake) — utan dem startar inget av
# dem på Windows (STATUS_DLL_NOT_FOUND). En no-op i statiskt läge och utanför
# Windows. Sist i filen, så att varje grind ovan — också de som läggs till
# senare ovanför — kommer med.
if(GLAZE_SHARED)
    get_property(_tests_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _tests_targets)
        get_target_property(_typ ${_t} TYPE)
        if(_typ STREQUAL "EXECUTABLE")
            glaze_runtime_dlls(${_t})
        endif()
    endforeach()
endif()
