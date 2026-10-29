#pragma once
#include <glaze/glaze.h>

#include "demo_text.h"

#include <optional>
#include <string>
#include <vector>

// Widget-galleriet — "allt måste synas i demot": varje widget, var och en i
// minst TVÅ tillstånd (normalt och inaktiverat), så ett byte av
// UI/Theme/Style i startfönstrets väljare går att BEDÖMA. Hover, tryck och
// öppen lista provas med musen — de iscensätts inte här.
//
// FLIKAR: grupperna ligger i en `TabControl`, en
// FLIK PER TEMA (grundläggande, text och listor, paneler och remsor, tabeller
// och träd, layout, interaktion, väljare, rityta, 3D och 3D-verktyg), i
// stället för i en rad kolumner som växte åt höger med varje ny komponent.
// Galleriet visar flikarna själv — layoutfliken har en flikkontroll i en
// flikkontroll.
// Galleri-FIXTUREN (tests/fixtures/gallery.*) förblir platt: pixel_gate
// kräver att varje widget SYNS, och en dold fliksida syns inte.
//
// HANDKODAT — och det är demons halva poäng.
// Startfönstret visar `.glazeui`-vägen med dock, ankare och SYSKONANKARE;
// det här fönstret visar samma ramverk byggt HELT i C++: ingen `.glazeui`,
// ingen `#pragma region`, inga värde-medlemmar, ingen GlazeBuilder. Hård
// regel 8 säger att designer och kod är LIKVÄRDIGA vägar, och ett par
// fönster som faktiskt gör det är ett bättre bevis än en mening.
//
// VAD HANDKODNING VISAR: en genererad väg skulle behöva rader i `onLoad`
// för `disable()`, `showScrollbars`, `setIndeterminate`, `setChecked(rad,
// på)` och `setRows()` — allt sådant som inte går att uttrycka som en
// FÄLTSÄTTNING. De är METODANROPSSATSER som previewn UTFÖR, så gränsen går
// vid loopar och lambdor, inte vid tillstånd. Handkodat sätts varje
// tillstånd DÄR WIDGETEN SKAPAS, och `onLoad` behövs inte alls. Det är inte
// ett fel i `.glazeui` — det är gränsen mellan vägarna, gjord synlig.
//
// Buildern kan inte öppna det här fönstret, precis som `about_dialog` och
// `free_window`, och det är avsiktligt.
class WidgetGallery : public glaze::AppWindow {
public:
    WidgetGallery();

private:
    // Gruppanelen: ram + rubrik + separator. I den genererade vägen är det
    // fem satser per grupp, nio gånger; här är det ETT anrop. Att kunna
    // abstrahera är handkodsvägens egen styrka — och att INTE kunna det är
    // priset designervägen betalar för att kunna ritas ut.
    // Gruppen hamnar på en FLIKSIDA (`sida`), i sidans koordinater.
    glaze::Panel* grupp(glaze::Widget* sida, std::uint32_t rubrik,
                        const std::string& namn, float x, float y, float w,
                        float h);

    // En FLIK i galleriet: en sida med översatt rubrik och synliga remsor —
    // blir sidan högre än ytan rullar den. Kommande komponenter lägger sin
    // grupp på en befintlig sida eller får en egen (apps/demo/src/CLAUDE.md).
    glaze::TabPage* sida(std::uint32_t titel);

    // Grupp 20:s stängbara flikar: en ny sida med en etikett, ÄGD av
    // flikkontrollen — så `unadopt` i `flikStangs` förstör den.
    glaze::TabPage* nySida(glaze::TabControl* tc, const std::string& titel);

    // Foten på varje grupp: den förklarande Dim-etiketten.
    glaze::Label* fot(glaze::Panel* g, std::uint32_t text,
                      const std::string& over, float gap);

    // RITYTAN — fliken "Rityta" byggs i en EGEN fil,
    // gallery_canvas.cpp: kartan, grafen med en inaktiverad kopia och
    // linjalprovet. Den är större än en grupp (plattorna och kartnålen
    // räknas fram ur pixlar), och resten av galleriet behöver inget av det.
    void ritytaFlik(glaze::TabPage* sida);

    // 3D — fliken "3D" byggs i gallery_3d.cpp: en
    // rityta med en Camera3D kring en liten kraftledning av primitiver och
    // rör, en egen mesh, instansierade träd, ett punktmoln färgat efter
    // höjden över mark (ett klick plockar en punkt), ett rutnät av linjer i
    // rymden, etiketter, en himmel, terräng — och FÖRSTAPERSONEN.
    void tredeFlik(glaze::TabPage* sida);
    // Fliken "3D-verktyg" byggs i gallery_3d_tools.cpp: konstruktionseditorns
    // förmågor byggda med ritytans verktyg — ett adaptivt rutnät, en
    // navigeringskub i en del-vy, kantläget och en miniatyr.
    void tredeVerktygFlik(glaze::TabPage* sida);

    // Handlers behöver nå två widgets — bara de sparas. I den genererade
    // vägen är VARJE widget en medlem, eftersom `.glazeui` måste kunna
    // referera dem vid namn.
    glaze::Label*      statusTxt_ = nullptr;
    glaze::Progress*   prgN_      = nullptr;
    glaze::TabControl* flikar_    = nullptr;   // galleriets egna flikar
    int                nyaFlikar_ = 0;         // grupp 20:s "Ny flik"-räknare
    // Dockningens SPARADE layout — det en riktig app lagrar i sina
    // inställningar och läser tillbaka vid start. Tom = inget sparat.
    std::string      dockLayout_;
    // Verktygsfältets ikon UR PIXLAR, räknad i ctorn och delad av
    // den aktiva och den inaktiverade remsan — en `Bitmap` är ett handtag.
    glaze::Bitmap    pixelIkon_;
    // RULLGARDINENS ÅNGRA-HISTORIK — demons påhittade
    // handlingar som text-id, senaste SIST. Den delade knappen ångrar den
    // sista, och menyn bakom pilen byggs om ur listan i `onDropDown`.
    std::vector<std::uint32_t> angraHistorik_;
    // DIALOGERNA (grupp 34): färgdialogen som MEDLEM — ett
    // behållet objekt öppnar med förra valet — och lagrets namn, förvalet i
    // inmatningsrutan.
    glaze::ColorDialog fargDialog_;
    std::string        lagerNamn_;

    // RITYTANS tillstånd (gallery_canvas.cpp). Kartans namn per lager och
    // form — det träffprovet skriver i statusraden — och var trycket föll:
    // ett släpp nära det är ett KLICK, längre bort var det en panorering.
    std::vector<std::vector<glaze::TrText>> kartNamn_;
    glaze::Point                            kartaNed_{};
    // LAGERLISTAN: ritytans lagerindex är POSITIONER, så
    // demon håller sin egen spegling plats → ursprungligt lager (det
    // `kartNamn_` är ordnad efter) och vallistan som visar lagren överst
    // först.
    std::vector<int>                        kartOrdning_;
    glaze::Dropdown*                        kartLager_ = nullptr;
    // Grafens gummiband (Skift+drag, lokala px) och världen under pekaren.
    bool                        band_ = false;
    glaze::Point                bandFran_{}, bandTill_{};
    std::optional<glaze::Point> grafPekare_;
    // LINJALPROVET: talet linjen ritas med (fysiska px per mm) och skärmen
    // det provades på. Står fönstret på en annan skärm gäller dess eget tal
    // igen — `kandidat()` läser det, så ingen skärmhändelse behövs.
    glaze::Canvas* linjal_ = nullptr;
    double         kand_   = 0.0;
    std::string    kandId_;
    double         kandidat() const;
    void           sattKandidat(double pxPerMm);

    // 3D-FLIKENS tillstånd (gallery_3d.cpp): ritytan och förstapersonens
    // växel, punktmolnets HÖJD ÖVER MARK per punkt (ett klick skriver den i
    // statusraden — plockningen svarar med punktens plats i indatan), var
    // trycket föll (ett släpp nära det är ett klick) och musblickens förra
    // punkt i förstapersonen.
    glaze::Canvas*     scen3d_       = nullptr;
    glaze::Checkbox*   forstaperson_ = nullptr;
    glaze::Dropdown*   inmLage_      = nullptr;
    glaze::Checkbox*   inmHjul_      = nullptr;
    glaze::Checkbox*   inmPlatta_    = nullptr;
    glaze::Checkbox*   inmZoom_      = nullptr;
    std::vector<float> punktHag_;
    glaze::Point       scenNed_{}, scenTitt_{};
    // 3D-VERKTYGENS (gallery_3d_tools.cpp): ritytan, miniatyrens yta och
    // modellens låda — miniatyrens kamera ramar in den.
    glaze::Canvas* verktyg3d_ = nullptr;
    glaze::Canvas* miniatyr_  = nullptr;
    glaze::Box3    modellLada_{};

    void sldChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void gridChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void verktygKlickat(glaze::Widget* sender, glaze::ClickArgs& e);
    void angraKlickat(glaze::Widget* sender, glaze::ClickArgs& e);
    void angraMeny(glaze::Widget* sender, glaze::EventArgs& e);
    void byggAngraMeny(glaze::ToolButton& b);
    void angraSteg(int antal);
    void talfaltAndrat(glaze::Widget* sender, glaze::ChangeArgs& e);
    void tradAndrat(glaze::Widget* sender, glaze::ChangeArgs& e);
    void tradKryssad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void tradFalld(glaze::Widget* sender, glaze::ChangeArgs& e);
    void tradAktiverad(glaze::Widget* sender, glaze::ActivateArgs& e);
    // Trädets drag med APPENS GODKÄNNANDE: regeln per
    // plats i `onDragOver`, släppet — som frågas FÖRE trädets flytt — i
    // `onDrop`.
    void tradOver(glaze::Widget* sender, glaze::DragArgs& e);
    void tradSlapp(glaze::Widget* sender, glaze::DragArgs& e);
    void listaAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void listaAktiverad(glaze::Widget* sender, glaze::ActivateArgs& e);
    void listaKryssad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void dataAndrad(glaze::Widget* sender, glaze::CellChangeArgs& e);
    void delareFlyttad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void dockAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void flikBytt(glaze::Widget* sender, glaze::ChangeArgs& e);
    void flikStangs(glaze::Widget* sender, glaze::TabCloseArgs& e);
    void filvalAndrat(glaze::Widget* sender, glaze::ChangeArgs& e);
    void fargAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void datumAndrat(glaze::Widget* sender, glaze::ChangeArgs& e);
    // Flervalet i listboxen (32) och datagriden (33) delar
    // handlern, och dialogerna i grupp 34.
    void valdaRaderAndrade(glaze::Widget* sender, glaze::ChangeArgs& e);
    void fargDialogKlickad(glaze::Widget* sender, glaze::ClickArgs& e);
    void namnDialogKlickad(glaze::Widget* sender, glaze::ClickArgs& e);
    void anteckningAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    // SNABBMENYN MOT NODEN OCH RADEN (grupp 13 och 14):
    // högertrycket har redan valt noden eller raden, och den läses ur sender
    // (`contextPath()`/`contextRow()`). Trädets meny har "Ta bort".
    glaze::Menu      snabbmeny_;
    glaze::TreeView* tradN_ = nullptr;
    void tradMeny(glaze::Widget* sender, glaze::ContextMenuArgs& e);
    void tradTaBort();
    void listaMeny(glaze::Widget* sender, glaze::ContextMenuArgs& e);
    void kartaSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void kartaOverlay(glaze::Widget* sender, glaze::PaintArgs& e);
    // Lagerlistan: fyll vallistan överst först med raden för `plats` vald,
    // och flytta det valda lagret `steg` platser (+1 uppåt, −1 nedåt).
    void kartaLagerLista(const glaze::Canvas& k, int plats);
    void kartaLagerSteg(glaze::Canvas& k, int steg);
    void grafNed(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void grafRor(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void grafSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void grafPaint(glaze::Widget* sender, glaze::PaintArgs& e);
    void grafOverlay(glaze::Widget* sender, glaze::PaintArgs& e);
    void linjalPaint(glaze::Widget* sender, glaze::PaintArgs& e);
    void linjalDra(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void kalSpara(glaze::Widget* sender, glaze::ClickArgs& e);
    void kalAterstall(glaze::Widget* sender, glaze::ClickArgs& e);
    void scenNed(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void scenRor(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void scenSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void scenTangent(glaze::Widget* sender, glaze::KeyArgs& e);
    void scenTick(glaze::Widget* sender, glaze::TickArgs& e);
    void forstapersonAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    // Inmatningen: läget och vändningarna på scen3d.
    void inmatningAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void verktygNed(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void verktygKamera(glaze::Widget* sender, glaze::ChangeArgs& e);
    void verktygTick(glaze::Widget* sender, glaze::TickArgs& e);
    void kantlageAndrat(glaze::Widget* sender, glaze::ChangeArgs& e);
    void miniatyrKlickad(glaze::Widget* sender, glaze::ClickArgs& e);

    // INMATNINGEN (grupp 30 på fliken Interaktion):
    // meddelandefältet vars `onKeyDown` skickar med Ctrl+Enter, och ytan där
    // högerknappen drar en linjal — trycket och pekaren i ytans lokala px,
    // tomt när ingen linjal dras.
    glaze::TextArea*            inMeddelande_ = nullptr;
    glaze::Canvas*              inYta_        = nullptr;
    std::optional<glaze::Point> inLinjalFran_;
    glaze::Point                inLinjalTill_{};
    void inTangent(glaze::Widget* sender, glaze::KeyArgs& e);
    void inYtaNed(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void inYtaRor(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void inYtaSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void inYtaDubbel(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void inYtaAvbruten(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void inYtaTangent(glaze::Widget* sender, glaze::KeyArgs& e);
    void inYtaTangentUpp(glaze::Widget* sender, glaze::KeyArgs& e);
    void inYtaOverlay(glaze::Widget* sender, glaze::PaintArgs& e);

    // STORLEK OCH TANGENTER (grupp 35 på fliken
    // Interaktion): ritytan i en delare vars `onResize` skriver ytan i
    // statusraden, högertrycket som öppnar en snabbmeny vid `toWindow`, och
    // fältet vars `onKeyDown` visar tangentens text.
    glaze::Canvas*              stYta_ = nullptr;
    glaze::Menu                 stMeny_;
    glaze::Point                stMarkerat_{};   // högertryckets punkt
    void stYtaStorlek(glaze::Widget* sender, glaze::ResizeArgs& e);
    void stYtaNed(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void stYtaSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void stTangent(glaze::Widget* sender, glaze::KeyArgs& e);
};
