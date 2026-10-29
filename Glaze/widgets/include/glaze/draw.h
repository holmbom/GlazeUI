#pragma once
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include <gfx/color.h>
#include <gfx/geometry.h>
#include <gfx/text.h>   // gfx::WrappedLine — `wrapLines`

#include "glaze/canvas/image_source.h"
#include "glaze/widget.h"

// Draw — RITANDET, och ingenting annat. Tredelningen är hård:
//
//     Är det en FÄRG?          -> Theme   (glaze/theme.h)
//     Är det ett MÅTT/en FORM? -> Style   (glaze/style.h)
//     Är det RITNING?          -> Draw    (här)
//
// Draw innehåller därför INGA konstanter, INGA mått och INGA färger — allt
// sådant kommer in som argument. Det enda Draw vet på egen hand är DPI:
// hur logiska px blir fysiska, och var widgeten sitter.
//
// KOORDINATERNA ÄR WIDGET-RELATIVA LOGISKA px: (0,0) är widgetens egna
// övre vänstra hörn och `c.bounds()` är hela dess yta. Draw lägger på både
// origo (c.box) och skalan (c.scale). Därför står det varken `resolve()`
// eller `* c.scale` i ritkod — en widget ritar i sitt eget rum och kan
// flyttas utan att en enda rad ritkod rörs.
//
// Målet är att en widgets draw() ska vara FÅ RADER SOM GÖR MYCKET: en rad
// per sak man ser. Behöver du en ny primitiv — lägg den här, så får alla
// widgets den, och plattformsskillnader kan rättas på ETT ställe.

namespace glaze {

// Konturens penna. width i LOGISKA px; 0 = HÅRLINJE (1 fysisk px oavsett
// skala — fasadens default, den klassiska tunna ramen).
struct Pen {
    gfx::Color color{};
    float      width = 0.0f;
};

// Bildens plats i förhållande till texten (Button m.fl.).
enum class ImagePos { Left, Right, Above, Below };

// Textens placering i sidled inom sin rect (den är alltid v-centrerad).
enum class Align { Left, Center, Right };

// Textens placering i HÖJDLED — ett textblock som är lägre än sin
// yta: kolumnhuvudets rubriker, där en rubrik på två rader står i ett huvud
// på fyra. Ett EGET enum och inte `Align`: vänster och höger betyder
// ingenting i höjdled, och ett anropsställe ska inte kunna säga det.
enum class VAlign { Top, Center, Bottom };

// RIKTNINGEN på en riktad glyf (`Draw::chevron`): dropdownen behöver ∨/∧,
// undermenyn ►, och det är SAMMA tecken vridet. Ett enum i stället för en bool
// gör anropsstället läsbart och lämnar plats för ◄ utan en tredje
// parameter.
enum class Dir { Up, Down, Left, Right };

// TECKNET i ett märke (`Draw::badge`). Tre av glyf-formerna
// nedan, valda för drag-och-släppets effekter: `+` kopiera, `−` avvisat
// (förbudsmärket) och `›` länka. Ett enum och inte en funktionspekare, så
// anropsstället säger VAD märket betyder.
enum class Sign { Plus, Minus, Arrow };

// DE RITADE IKONERNA (`Draw::icon`). Filväljaren behöver mappar,
// filer och platser, och ramverket har inga egna BILDER — assets.bin är
// APPENS, och en dialog som visar tomma rutor i en app utan assets hade
// varit trasig. En ritad ikon är dessutom skarp i varje skala och ritas i
// den penna anroparen ger, så den byter färg med en vald rad precis som
// kryssglyfen. Namnen säger vad man SER, inte vad det betyder: anroparen
// avgör att `House` är "Hem".
enum class Icon {
    Folder,     // mapp med flik
    File,       // blad med vikt hörn
    Document,   // bladet med textrader
    Drive,      // en låda med en lampa — volymen
    House,      // tak och dörr
    Monitor,    // skärm på fot — skrivbordet
    Download,   // pil ned i ett tråg
    Picture,    // ram med berg och sol
    ArrowUp,    // pil upp — "en nivå upp"
    Calendar,   // bladet med två ringar och en markerad dag
                // (datumväljarens knapp)
    Pinned,     // en knappnål som STÅR, nålen nedåt — en fäst panel
                // (trycket döljer den automatiskt)
    Unpinned,   // samma nål LIGGANDE, nålen åt vänster — en panel som döljs
                // automatiskt (trycket fäster den)
};

// Bild + text som EN sak — "en fyrkant med en bild och text i". Blocket
// centreras i rect:en och varje del centreras på tväraxeln. Tom text = bara
// ikon; ogiltig/saknad bild = bara text. Bilden är ett `ImageSource` — en
// asset, en Bitmap eller en RenderTexture.
struct Content {
    ImageSource img{};
    std::string text;
    gfx::Color  color{};
    ImagePos    pos      = ImagePos::Left;
    float       gap      = 0.0f;   // avstånd bild <-> text
    float       iconSide = 0.0f;   // bildens sida (kvadratisk)

    // INAKTIV BILD. Texten dimmas med sin FÄRG, men en bild har ingen, så
    // `imgOpacity` (0–1) är bildens OPACITET — `Renderer::drawImage`
    // multiplicerar in den i bildens egen alfa, så ikonen tonas mot VILKEN
    // yta som helst (en slöja i ytans färg stämmer bara mot en enfärgad yta
    // och syns som en ruta på en panel eller i en gruppram). Bara BILDEN
    // tonas; texten har sin färg.
    //
    // SIST i structen med flit: `Content` brace-initieras positionellt
    // (`Button::content`), och ett fält till på slutet ändrar inget där.
    float       imgOpacity = 1.0f;   // 1 = bilden som den är (default)
};

struct Draw {
    // ---------------------------------------------------------- ytor
    // Fylld rundad rekt (radius 0 = skarpa hörn).
    static void rect(const Ctx& c, const gfx::Rectf& r, float radius,
                     gfx::Color fill);
    // Fylld OCH konturad i ett anrop.
    static void rect(const Ctx& c, const gfx::Rectf& r, float radius,
                     gfx::Color fill, const Pen& pen);
    // Bara kontur (fokusringar, markeringar).
    static void frame(const Ctx& c, const gfx::Rectf& r, float radius,
                      const Pen& pen);

    // Kontur med en LUCKA i ÖVERKANTEN — gruppramens form: en
    // rubrik som står i linjen och inte under den. Luckan är [gapX,
    // gapX + gapW) i samma rymd som `r`; gapW <= 0 är en vanlig `frame`.
    // Bara överkanten bryts — sidorna och nederkanten ritas hela, även
    // när luckan ligger rakt ovanför nederkanten. En lucka som når ett
    // HÖRN kapas därför vid sidlinjens insida och förbi hörnbågen (annars tar
    // flikkontrollens valda flik i ramens högra hörn sidlinjens övre
    // halva med sig).
    static void frameGap(const Ctx& c, const gfx::Rectf& r, float radius,
                         const Pen& pen, float gapX, float gapW);

    // Pennans bredd i LOGISKA px — hårlinjen (width 0) är 1 FYSISK px, alltså
    // 1/skala. För en form som ska MÖTA en linje: flikkontrollens lucka går
    // mellan den valda flikens sidlinjer, så att de möter ramens överkant i
    // slutna hörn.
    static float penWidth(const Ctx& c, const Pen& pen);

    // Mjuk skugga UTANFÖR r — ritas FÖRE ytan. Allt som hamnar utanför
    // widgetens rect måste rymmas i stilens `overdraw` (damage).
    static void shadow(const Ctx& c, const gfx::Rectf& r, float radius,
                       float dx, float dy, float blur, gfx::Color col);

    // LAMINERINGEN — två klippta ljusfält som ljusar upp en ytas övre del
    // och ger den materialitet (Glazes "Soft Laminate"). Ritas OVANPÅ en
    // redan fylld yta, före konturen.
    //
    // ANDELARNA ÄR PARAMETRAR (0–1 av höjden), aldrig något primitiven
    // hämtar ur en widgets Style-fält: `Draw` innehåller inga mått och får
    // därför inte heller VETA om `sheenLower`. Anroparen skickar sina två
    // andelar och sina två färger; är färgerna genomskinliga (Slate,
    // Minimalistic) blir anropet en synlig no-op och behöver ingen vakt.
    //
    // Låg fart är inte ett problem: två klipp + två rektanglar, och bara på
    // ytor som faktiskt lamineras.
    static void laminate(const Ctx& c, const gfx::Rectf& r, float radius,
                         gfx::Color lower, float lowerFrac,
                         gfx::Color upper, float upperFrac);

    // RUTMÖNSTRET — genomskinlighetens bakgrund:
    // `r` fylld med `a` och var annan ruta i `b`, rutorna `cell` stora
    // (logiska px) med `a` i övre vänstra hörnet, klippt till `r` med
    // hörnradien `radius`. En färg med alfa ritad OVANPÅ visar då hur
    // genomskinlig den är. Rutans mått och de två färgerna är argument —
    // Draw har inga egna. `cell <= 0` fyller bara med `a`.
    static void checker(const Ctx& c, const gfx::Rectf& r, float radius,
                        float cell, gfx::Color a, gfx::Color b);

    // Cirkeln inskriven i box (radien = halva kortsidan) — trafikljusen.
    static void circle(const Ctx& c, const gfx::Rectf& box, gfx::Color fill);
    // Fylld OCH konturad — samma par som `rect` har. Radions ring och
    // sliderns tumme är cirklar med kant, inte bara fyllningar.
    static void circle(const Ctx& c, const gfx::Rectf& box, gfx::Color fill,
                       const Pen& pen);

    // ---------------------------------------------------------- streck
    static void line(const Ctx& c, float x1, float y1, float x2, float y2,
                     const Pen& pen);

    // ------------------------------------------------------- glyf-former
    // Små TECKEN vars proportioner ÄR formens definition — precis som
    // hörnbågen är en del av `rect`, inte ett stilmått. Anroparen ger
    // rutan tecknet ska rymmas i (logiska px) och pennan; storleken är
    // alltså rutans, och stilen bestämmer den. Ligger här och inte i
    // widgetarna för att en ✕ ska se likadan ut var den än ritas.
    static void check(const Ctx& c, const gfx::Rectf& box, const Pen& pen);
    // Vinkelhaken. Spetsen pekar åt `dir`, skänklarna mot motsatt kant —
    // rutan är hela måttet, som för de andra glyferna.
    static void chevron(const Ctx& c, const gfx::Rectf& box, Dir dir,
                        const Pen& pen);
    static void cross(const Ctx& c, const gfx::Rectf& box, const Pen& pen);
    static void plus(const Ctx& c, const gfx::Rectf& box, const Pen& pen);
    static void minus(const Ctx& c, const gfx::Rectf& box, const Pen& pen);

    // MÄRKET — en fylld skiva med ett TECKEN i. Pekarens
    // följeslagare i ett drag: OS-pekarens UTSEENDE är inte vårt (fasadens
    // `gfx::PointerShape` har inget "+", och ett OS-märke
    // hade sett olika ut på tre plattformar), så "+" och förbudsmärket
    // ritas här. Tecknets ruta och strecket är
    // PROPORTIONER av skivan — märkets definition, som bockens i `check` —
    // så storleken är rutan och färgerna argument.
    static void badge(const Ctx& c, const gfx::Rectf& box, Sign sign,
                      gfx::Color fill, gfx::Color fg);

    // IKONEN — en linjeteckning i en KVADRAT, centrerad i rutan
    // (en avlång ruta får alltså luft på sidorna). Proportionerna är
    // IKONENS definition, som bockens; storleken är rutan och linjen
    // pennan — filväljaren skickar `iconFallback` och `iconStroke`.
    static void icon(const Ctx& c, const gfx::Rectf& box, Icon icon,
                     const Pen& pen);

    // GREPPET — verktygsfältets handtag i en dockningsyta: två
    // rader små kvadrater längs rutans LÅNGA axel, centrerade. Proportionerna
    // är greppets definition, som bockens: en prick är en fjärdedel av rutans
    // korta sida, med en pricks luft mellan raderna och mellan prickarna och
    // en halv mot kanterna. Storleken är rutan, färgen argumentet.
    // `vertical` = raderna står (greppet i början av ett LIGGANDE fält).
    static void grip(const Ctx& c, const gfx::Rectf& box, bool vertical,
                     gfx::Color col);

    // ------------------------------------------------------ text & bild
    // Alltid v-centrerad i r; align styr sidled.
    static void text(const Ctx& c, const gfx::Rectf& r,
                     const std::string& s, gfx::Color col,
                     Align align = Align::Left);

    // RADBRUTEN text — samma text som `text`, men den
    // bryts mot en BREDD i stället för att rinna ut ur sin rect. Bryter på
    // ordgräns, låter `\n` bryta alltid och hårdbryter ett ord som är
    // bredare än ytan, allt på UTF-8-teckengräns (`gfx::wrapText`) — åäö
    // är säkra per konstruktion, och Draw bryter aldrig i byte själv.
    //
    // ENHETERNA, skrivna och inte underförstådda: `r` är widget-relativa
    // LOGISKA px som allt annat här, medan fonten talar FYSISKT — därför
    // matas `wrapText` med `r.w * c.scale` och varje radbredd divideras
    // tillbaka med `c.scale`. Brytbredden ÄR `r.w`.
    //
    // VERTIKAL PLACERING (kontraktet `measureWrapped` delar): hela
    // radblocket centreras i `r`, och varje rad får en `lineHeight` hög
    // logisk rect som `Draw::text` v-centrerar i. Blockets höjd är alltid
    // minst EN radhöjd — tom text och avslutande `\n` ger en tom rad.
    static void textWrapped(const Ctx& c, const gfx::Rectf& r,
                            const std::string& s, gfx::Color col,
                            Align align = Align::Left);

    // Radbrutna textens storlek i LOGISKA px vid brytbredden `maxW`.
    // Bredden är den BREDASTE radens (alltså <= maxW, utom när ett enda
    // tecken är bredare än så), höjden radantalet × radhöjden.
    // Motstycket till `textWrapped` — skicka SAMMA `maxW` som rectens
    // bredd, annars bryts texten på ett annat ställe än den mättes.
    static gfx::Sizef measureWrapped(const Ctx& c, const std::string& s,
                                     float maxW);

    // RADERNA som `textWrapped` ritar och `measureWrapped` mäter, som
    // BYTE-INTERVALL [begin, end) i `s` — för en widget som äger
    // sina rader själv (det flerradiga fältets caret, markering och scroll)
    // och ändå ska bryta EXAKT där en radbruten etikett bryter. `maxW` i
    // LOGISKA px; alltid minst en rad. Radbrytande blanksteg ingår inte i
    // någon rad (`gfx::wrapText`s kontrakt), så raderna TÄCKER inte alltid
    // texten: luckan mellan två rader är det blanksteg brytningen åt.
    static std::vector<gfx::WrappedLine> wrapLines(const Ctx& c,
                                                   const std::string& s,
                                                   float maxW);

    // KORTAD text — samma text som `text`, men en text som inte
    // ryms i `r.w` KORTAS och får en ellips (…) i stället för att rinna ut
    // ur sin rect. Kortningen sker på UTF-8-TECKENGRÄNS (fontens egen
    // `fitWidth`, radbrytningens råvara), så åäö klyvs aldrig; ryms inte ens
    // ellipsen ritas ingenting. En text som ryms ritas exakt som `text` hade
    // ritat den.
    //
    // Tabellens celler är första användaren: en kolumn är smalare än sin
    // längsta text, och en avklippt bokstav läser som ett fel där en ellips
    // läser som "det finns mer".
    static void textElided(const Ctx& c, const gfx::Rectf& r,
                           const std::string& s, gfx::Color col,
                           Align align = Align::Left);

    // Den kortade STRÄNGEN för bredden `maxW` (LOGISKA px) — exakt det
    // `textElided` ritar. Egen funktion så att en grind kan mäta kortningen
    // utan pixlar, och så att mätning och ritning aldrig kan glida isär.
    static std::string elide(const Ctx& c, const std::string& s, float maxW);

    // GLORIAN — konturen runt text, som ritytans `TextStyle::halo`:
    // `paint` anropas en gång per punkt på en ring med radien `width` (LOGISKA
    // px) runt origo, med ett Ctx vars ruta är förskjuten dit, och ritar då
    // texten i konturens färg med vilken textprimitiv den vill (`text`,
    // `textWrapped`, `textElided`) — texten själv ritas sedan ovanpå, orörd.
    // UI-texten har ingen SDF som scenens (där glorian är ett eget pass i
    // shadern): kopiorna ligger tätt nog — en fysisk px isär längs ringen,
    // minst åtta och högst trettiotvå — för att bilda en sluten kontur. En
    // bredd ≤ 0 ritar ingenting. Kostar kopiorna i glyfer per ritning: för en
    // etikett över en bild, inte för löpande text.
    static void halo(const Ctx& c, float width,
                     const std::function<void(const Ctx&)>& paint);

    // RADERNA I ETT BLOCK med fast höjd (kolumnhuvudets rubriker):
    // texten bruten för bredden `maxW` (LOGISKA px) till HÖGST `maxLines`
    // rader, så som de ska ritas. `\n` bryter alltid, och resten bryts på
    // ORDGRÄNS genom `wrapLines` — men ett ORD som inte ryms på en egen rad
    // hårdbryts INTE, det står helt och KORTAS (`elide`): ett ord kluvet utan
    // bindestreck läser fel, medan "…" läser som "det finns mer". Blir
    // raderna fler än `maxLines` bär den sista synliga RESTEN av sitt stycke,
    // kortad — och slutar med en ellips även när den resten råkar rymmas,
    // för då finns det fler stycken under den.
    //
    // `maxLines <= 0` = obegränsat, och radantalet ÄR då svaret (tabellens
    // AUTO-höjd). Alltid minst en rad. Med EN rad och utan `\n` är svaret
    // exakt `{elide(c, s, maxW)}` — en enradsrubrik ritas som den alltid
    // gjort. Ritningen (`text` per rad, en radhöjd isär) och placeringen i
    // höjdled är anroparens, för de beror på ytan: blocket här är bara texten.
    static std::vector<std::string> elideLines(const Ctx& c,
                                               const std::string& s,
                                               float maxW, int maxLines);

    // Bilden skalas in i r med GPU:ns filter (en Bitmaps `setFilter` —
    // Nearest för pixelgrafik). Saknad/trasig bild ritar ingenting (tyst),
    // och en Bitmap som inte ryms i framens uppladdningsbudget ritas nästa
    // frame. `opacity` (0–1) tonar bilden mot vad som än ligger under (en
    // inaktiv ikon — se `Content::imgOpacity`); 1 = som den är.
    static void glyph(const Ctx& c, const gfx::Rectf& r, const ImageSource& img,
                      float opacity = 1.0f);

    // SKARP NEDSKALNING: som `glyph`, men bilden
    // räknas om på CPU till rectets EXAKTA storlek i fysiska px (ytans
    // medelvärde) och ritas 1:1, med recten fäst på hela pixlar. För en STOR
    // källa i en LITEN ruta — programikonens 256 i namnlistens 16 — där
    // GPU:ns sampling hoppar över pixlar och ger en grynig bild. En omräknad
    // textur per storlek och fönster, så den är inget för bilder som ritas i
    // många storlekar. En Bitmap räknas om ur sina levande pixlar när den
    // ändrats; en RenderTexture har inga pixlar på CPU:n och ritas med
    // `glyph`.
    static void glyphResampled(const Ctx& c, const gfx::Rectf& r,
                               const ImageSource& img, float opacity = 1.0f);

    // VALREGELN för en ikon i flera källor: den källa
    // `glyphResampled` ska rita i `r` — den MINSTA vars sida i fysiska px är
    // minst rectets, annars den STÖRSTA (`glzassets::pickIconSource`). En
    // handritad liten bild vinner alltså i sin egen storlek, och den stora
    // tar allt över den. Ogiltiga källor väljs aldrig; ogiltigt svar när
    // ingen källa finns.
    static ImageSource pickIcon(const Ctx& c, const gfx::Rectf& r,
                                std::initializer_list<ImageSource> sources);

    // Bildens NATURLIGA mått i LOGISKA px — bildens pixlar räknade som
    // FYSISKA px, alltså pixlar / scale, för alla tre källorna. Det Picture
    // behöver för autostorleken och "ryms med bevarade proportioner". En
    // Bitmap eller RenderTexture läses direkt ur handtaget, UTAN uppladdning
    // (mätningen sker utanför en frame, och den behöver ingen cache); en
    // asset avkodas genom cachen. Följd: samma Bitmap är logiskt MINDRE på en
    // skärm med högre skala, som en asset. Ogiltig källa — och för en asset
    // ingen cache eller trasig bild — ⇒ {0, 0}.
    static gfx::Sizef imageSize(const Ctx& c, const ImageSource& img);

    // Bild + text som ett centrerat block (se Content).
    static void content(const Ctx& c, const gfx::Rectf& r, const Content& ct);

    // Blockets storlek i LOGISKA px — för contentSize()/autostorlek.
    // Läser ALDRIG c.box (körs inne i resolve()).
    static gfx::Sizef measure(const Ctx& c, const Content& ct);

    // Textens storlek i LOGISKA px.
    static gfx::Sizef measure(const Ctx& c, const std::string& s);

    // Textradens höjd i LOGISKA px (oberoende av vad som står i den).
    static float lineHeight(const Ctx& c);

    // BASLINJENS AVSTÅND från radens överkant, LOGISKA px —
    // fontens `ascent()` genom skalan. Kontraktet som gör blandade fonter på
    // EN rad möjliga utan en ny textprimitiv: varje backend lägger baslinjen
    // på round(y + ascent), och `text` i en rect lika hög som fontens
    // `lineHeight` ritar exakt i rectens överkant (`measure().h` är fontens
    // höjd). En del vars rect börjar på baslinjen − sin egen ascent står
    // alltså på samma baslinje som sina grannar, också i en annan font.
    static float ascent(const Ctx& c);

    // FONTEN ISATT — Ctx:t med en annan familj, stil
    // eller storlek, för en widget som ritar i Mono eller fetstil (terminalen
    // i sidopaketet, en egen widget i en app). `Draw` läser `c.font`, så ett
    // fontbyte är ett fältbyte: mät och rita med det svarade Ctx:t.
    //
    // Fonterna är fönstrets TEXTFONTER (`Ctx::textFonts`): utan storlek
    // brödtextens storlek i stilen, med en storlek > 0 i px (logiska) eller mm
    // (skärmens, `Ctx::pxPerMm`) en FRI plats — samma lata, delade fonter som
    // fältens `setTextSize`. UI i vanlig stil och stilens egen storlek är
    // fönstrets font. Utan textfonter (ett Ctx utanför ett fönster), med en
    // storlek i världsenheter eller när fonten inte går att ladda är svaret
    // `c` självt — texten ritas då i fönstrets font, aldrig inte alls.
    static Ctx withFont(const Ctx& c, gfx::FontFamily family,
                        gfx::FontStyle style = gfx::FontStyle::Regular,
                        Length size = {});

    // Bredden av textens första `bytes` byte, i LOGISKA px — caret- och
    // selektionsgeometrin. Fasadens mätning, bara i ritrymdens enhet.
    static float prefixWidth(const Ctx& c, const std::string& s,
                             std::size_t bytes);

    // Närmaste teckengräns vid x (LOGISKA px från textens början) —
    // mus → teckenindex.
    static std::size_t byteIndexAtX(const Ctx& c, const std::string& s,
                                    float x);

    // ------------------------------------------------------------ klipp
    // Klipper efterföljande ritning mot r (widget-relativa logiska px).
    // Balansera alltid med endClip — nästlade klipp kan aldrig växa.
    //
    // `radius` (LOGISKA px, som allt annat i Draw) rundar klippets hörn —
    // 0 är en ren rektangel och kostar ingenting. Klippet blir då exakt
    // den form `Draw::rect(c, r, radius, …)` fyller, så en rundad yta och
    // dess innehåll delar kant.
    static void clip(const Ctx& c, const gfx::Rectf& r, float radius = 0.0f);
    static void endClip(const Ctx& c);

    // ------------------------------------------------------------ rymd
    // Krymper (negativt = växer) en logisk rect på alla sidor — för
    // fokusringar och innermarginaler.
    static gfx::Rectf inset(const gfx::Rectf& r, float d);

    // Widget-lokal LOGISK rect -> absolut FYSISK, mot widgetens egen
    // rect (`resolve(c)`). Det är HÄR mät- och damage-kontraktet möter
    // ritrymden: geometrin definieras EN gång i logiska px och beteendet
    // (hit-test, invalidateRect, editor-placering) projicerar den — aldrig
    // två uppsättningar mått. Läser inte c.box och fungerar därför även
    // utanför draw().
    static gfx::Rectf project(const Ctx& c, const gfx::Rectf& base,
                              const gfx::Rectf& local);

    // Widgetens egen yta i LOGISKA px räknad ur dess FYSISKA rect — samma
    // sak som Ctx::bounds(), men användbar UTANFÖR draw() (där c.box inte
    // är satt). Geometri-metoder som både ritningen och beteendet läser
    // bygger på den här: `Draw::bounds(c, resolve(c))`.
    static gfx::Rectf bounds(const Ctx& c, const gfx::Rectf& base);

    // Omvänt: absolut FYSISK muskoordinat -> widget-lokal LOGISK, så
    // hit-testen kan räkna i samma rymd som ritningen.
    static float localX(const Ctx& c, const gfx::Rectf& base, float physX);
    static float localY(const Ctx& c, const gfx::Rectf& base, float physY);

    // ------------------------------------------------- rullningslisterna
    // Scrollbar-widgeten och Panels overlay-remsor ritar SAMMA pixlar ⇒
    // EN sanning. Måtten är enhetsneutrala (bara förhållandena används),
    // men track/thumb talar widget-lokala LOGISKA px som allt annat här.
    struct ScrollMetrics {
        float min      = 0.0f;
        float max      = 0.0f;
        float value    = 0.0f;
        float page     = 0.0f;
        bool  vertical = true;
    };

    // Tummens rect i track:ens rymd — ritningen OCH greppet läser den.
    static gfx::Rectf scrollThumb(const ScrollMetrics& m,
                                  const gfx::Rectf& track, float pad,
                                  float minLen);

    // Tummen ritad som ett piller (radien = halva kortsidan).
    static void scrollThumbDraw(const Ctx& c, const gfx::Rectf& track,
                                const ScrollMetrics& m, float pad,
                                float minLen, gfx::Color col);
};

} // namespace glaze
