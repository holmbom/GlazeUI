// Ytornas triangulering — EGEN öronklippning. Kontraktet står
// i earcut.h; här är metoden:
//
//  1. Ringarna blir dubbellänkade ringar av noder, ytterringen MOTURS och
//     hålen MEDURS (y uppåt). Då ligger ytan alltid till VÄNSTER om varje
//     kant — också sedan hålen bryggats in.
//  2. HÅLEN bryggas in ett i taget, från vänster: från hålets vänstraste
//     punkt dras en stråle åt vänster till närmaste kant, och bryggan går
//     till den synliga punkt som ligger närmast strålen. Två dubblerade
//     noder gör bryggan till en spalt utan bredd, och ringen blir EN (svagt
//     enkel) polygon.
//  3. ÖRONEN klipps: en konvex nod vars triangel (föregående, noden, nästa)
//     inte innehåller någon annan nod är ett öra. I en stor ring (> 80 noder)
//     prövas bara noderna i triangelns z-intervall — Morton-ordningen på ett
//     rutnät över ringens låda håller grannar nära varandra i listan.
//  4. Fastnar klippningen (ett helt varv utan öra) prövas tre reservsteg i
//     tur och ordning: rensa dubblerade och kollineära punkter; bota LOKALA
//     självkorsningar (två kanter med en nod emellan som korsar varandra);
//     dela ringen längs en giltig diagonal och klipp halvorna var för sig.
//     Hjälper inget av dem stängs resten med en solfjäder.
//  5. KONTROLLEN: trianglarnas sammanlagda area ska vara ytterringens minus
//     hålens. Gäller det inte var ringarna ingen enkel yta (en självkorsning
//     täcker båda öglorna, ett hål utanför dras inte av) och svaret blir
//     false — trianglarna står kvar, för de ritar det användaren ser.
//
// Koordinaterna räknas i double RELATIVT ytterringens första punkt: ett
// SWEREF-tal (~6,6·10⁶) hade ätit siffrorna i kryssprodukterna.

#include "earcut.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace gfx {
namespace {

struct Node {
    std::uint32_t i = 0;       // index i den platta punktlistan
    double        x = 0.0;     // relativt ytterringens första punkt
    double        y = 0.0;
    Node*         prev  = nullptr;
    Node*         next  = nullptr;
    std::uint32_t z     = 0;   // Morton-värdet (bara i stora ringar)
    Node*         prevZ = nullptr;
    Node*         nextZ = nullptr;
};

// > 0 när a → b → c svänger åt vänster (moturs med y uppåt), 0 på en linje.
double turn(const Node* a, const Node* b, const Node* c)
{
    return (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
}

bool samePlace(const Node* a, const Node* b)
{
    return a->x == b->x && a->y == b->y;
}

// Ligger (px, py) i den SLUTNA triangeln (a, b, c)? Orienteringen spelar
// ingen roll: punkten är inne när den ligger på samma sida om alla tre
// kanterna (eller på en av dem).
bool inTriangle(double ax, double ay, double bx, double by, double cx,
                double cy, double px, double py)
{
    const double d1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    const double d2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
    const double d3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
    const bool   neg = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
    const bool   pos = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
    return !(neg && pos);
}

int sign(double v)
{
    return (v > 0.0) - (v < 0.0);
}

// q på sträckan p–r, givet att de tre ligger på en linje.
bool onSegment(const Node* p, const Node* q, const Node* r)
{
    return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) &&
           q->y <= std::max(p->y, r->y) && q->y >= std::min(p->y, r->y);
}

// Korsar sträckorna p1–q1 och p2–q2 varandra — också när de bara nuddar
// eller ligger på varandra?
bool segmentsMeet(const Node* p1, const Node* q1, const Node* p2,
                  const Node* q2)
{
    const int o1 = sign(turn(p1, q1, p2));
    const int o2 = sign(turn(p1, q1, q2));
    const int o3 = sign(turn(p2, q2, p1));
    const int o4 = sign(turn(p2, q2, q1));
    if (o1 != o2 && o3 != o4)
        return true;
    return (o1 == 0 && onSegment(p1, p2, q1)) ||
           (o2 == 0 && onSegment(p1, q2, q1)) ||
           (o3 == 0 && onSegment(p2, p1, q2)) ||
           (o4 == 0 && onSegment(p2, q1, q2));
}

// Går diagonalen a → b in i ytan precis vid a? Ytan ligger till vänster om
// varje kant, så vid en konvex nod är det inre KONEN mellan kanten ut
// (a → a.next) och kanten in baklänges (a → a.prev); vid en reflex nod är
// det allt UTOM konen mellan dem åt andra hållet.
bool locallyInside(const Node* a, const Node* b)
{
    if (turn(a->prev, a, a->next) >= 0.0)
        return turn(a, a->next, b) >= 0.0 && turn(a, b, a->prev) >= 0.0;
    return turn(a, a->next, b) >= 0.0 || turn(a, b, a->prev) >= 0.0;
}

// Ligger mittpunkten av a–b inne i ringen (jämn–udda-regeln)? `steps`
// räknar de besökta kanterna (reservstegens arbetsbudget).
bool middleInside(const Node* a, const Node* b, long& steps)
{
    const double px     = (a->x + b->x) / 2.0;
    const double py     = (a->y + b->y) / 2.0;
    bool         inside = false;
    const Node*  p      = a;
    do {
        const Node* q = p->next;
        if ((p->y > py) != (q->y > py) && q->y != p->y &&
            px < (q->x - p->x) * (py - p->y) / (q->y - p->y) + p->x)
            inside = !inside;
        ++steps;
        p = q;
    } while (p != a);
    return inside;
}

// Korsar diagonalen a–b någon kant i ringen som inte går ut från a eller b?
bool crossesRing(const Node* a, const Node* b, long& steps)
{
    const Node* p = a;
    do {
        const Node* q = p->next;
        ++steps;
        if (p->i != a->i && q->i != a->i && p->i != b->i && q->i != b->i &&
            segmentsMeet(p, q, a, b))
            return true;
        p = q;
    } while (p != a);
    return false;
}

std::uint32_t spread(std::uint32_t v)
{
    v &= 0x0000ffffu;
    v = (v | (v << 8)) & 0x00ff00ffu;
    v = (v | (v << 4)) & 0x0f0f0f0fu;
    v = (v | (v << 2)) & 0x33333333u;
    v = (v | (v << 1)) & 0x55555555u;
    return v;
}

class Triangulator {
public:
    Triangulator(std::span<const Point2d> pts, std::vector<std::uint32_t>& out)
        : pts_(pts), out_(out)
    {
    }

    bool run(std::span<const std::uint32_t> ringStarts);

private:
    Node* add(std::uint32_t i, Node* last);
    Node* linkRing(std::uint32_t begin, std::uint32_t end, bool ccw);
    void  unlink(Node* p);
    void  emit(const Node* a, const Node* b, const Node* c);

    Node* filterPoints(Node* start, Node* end);
    Node* bridgeHole(Node* hole, Node* outer);
    Node* findBridge(const Node* hole, Node* outer);
    Node* split(Node* a, Node* b);

    void  clip(Node* ear, int pass);
    bool  isEar(const Node* ear) const;
    bool  isEarHashed(const Node* ear) const;
    bool  blocks(const Node* a, const Node* b, const Node* c,
                 const Node* p) const;
    Node* cureLocalIntersections(Node* start);
    void  splitAndClip(Node* start);
    bool  isValidDiagonal(const Node* a, const Node* b);
    void  fan(Node* start);

    std::uint32_t zOrder(double x, double y) const;
    void          indexCurve(Node* start);

    std::span<const Point2d>    pts_;
    std::vector<std::uint32_t>& out_;
    std::deque<Node>            nodes_;   // stabila adresser vid tillväxt
    double                      ox_ = 0.0, oy_ = 0.0;   // ytterringens första
    double                      minX_ = 0.0, minY_ = 0.0, invSize_ = 0.0;
    bool                        hashed_ = false;
    bool                        failed_ = false;
    // Reservstegens ARBETSBUDGET i besökta kanter. Diagonalsökningen i
    // splitAndClip är O(n³) i värsta fall — en ring av slumpade punkter med
    // tusentals självkorsningar hade hängt UI-tråden i minuter. När budgeten
    // är slut stängs resten med en solfjäder (svaret är false ändå: en ring
    // som når hit är sällan en enkel yta).
    long                        budget_ = 20'000'000;
};

Node* Triangulator::add(std::uint32_t i, Node* last)
{
    Node& n = nodes_.emplace_back();
    n.i     = i;
    n.x     = pts_[i].x - ox_;
    n.y     = pts_[i].y - oy_;
    if (!last) {
        n.prev = n.next = &n;
    } else {
        n.next           = last->next;
        n.prev           = last;
        last->next->prev = &n;
        last->next       = &n;
    }
    return &n;
}

// Ringen [begin, end) som länkad ring i önskad riktning — ytterringen
// moturs, hålen medurs. Riktningen avgörs av ringens tecknade area.
Node* Triangulator::linkRing(std::uint32_t begin, std::uint32_t end, bool ccw)
{
    double area = 0.0;
    for (std::uint32_t i = begin, j = end - 1; i < end; j = i++)
        area += (pts_[j].x - ox_) * (pts_[i].y - oy_) -
                (pts_[i].x - ox_) * (pts_[j].y - oy_);
    Node* last = nullptr;
    if ((area > 0.0) == ccw) {
        for (std::uint32_t i = begin; i < end; ++i)
            last = add(i, last);
    } else {
        for (std::uint32_t i = end; i > begin; --i)
            last = add(i - 1, last);
    }
    if (last && last->next != last && samePlace(last, last->next)) {
        Node* n = last->next;
        unlink(last);
        last = n;
    }
    return last;
}

void Triangulator::unlink(Node* p)
{
    p->next->prev = p->prev;
    p->prev->next = p->next;
    if (p->prevZ)
        p->prevZ->nextZ = p->nextZ;
    if (p->nextZ)
        p->nextZ->prevZ = p->prevZ;
}

void Triangulator::emit(const Node* a, const Node* b, const Node* c)
{
    out_.insert(out_.end(), {a->i, b->i, c->i});
}

// Tar bort dubblerade och kollineära noder mellan start och end (hela
// ringen när end saknas). nullptr när färre än tre noder blir kvar — då
// finns ingen yta att triangulera.
Node* Triangulator::filterPoints(Node* start, Node* end)
{
    if (!start)
        return nullptr;
    if (!end)
        end = start;
    Node* p = start;
    bool  again;
    do {
        again = false;
        if (samePlace(p, p->next) || turn(p->prev, p, p->next) == 0.0) {
            unlink(p);
            p = end = p->prev;
            if (p == p->next || p->next == p->prev)
                return nullptr;
            again = true;
        } else {
            p = p->next;
        }
    } while (again || p != end);
    return end;
}

// Bryggan från hålets vänstraste punkt till ytterringen: strålen åt vänster
// träffar närmaste kant i I, och kantens ändpunkt BORTOM I längs strålen är
// kandidaten P. Ligger någon nod i triangeln (hålpunkten, I, P) skymmer den
// P — då blir bryggan den nod i triangeln som ligger närmast strålen.
Node* Triangulator::findBridge(const Node* hole, Node* outer)
{
    const double hx = hole->x;
    const double hy = hole->y;
    double       qx = -std::numeric_limits<double>::infinity();
    Node*        m  = nullptr;
    Node*        p  = outer;
    do {
        const Node* q = p->next;
        if (q->y != p->y && hy <= std::max(p->y, q->y) &&
            hy >= std::min(p->y, q->y)) {
            const double x = p->x + (hy - p->y) * (q->x - p->x) / (q->y - p->y);
            if (x <= hx && x > qx) {
                qx = x;
                m  = p->x < q->x ? p : p->next;
                if (x == hx)
                    return m;   // hålet nuddar kanten
            }
        }
        p = p->next;
    } while (p != outer);
    if (!m)
        return nullptr;

    const double mx     = m->x;
    const double my     = m->y;
    double       tanMin = std::numeric_limits<double>::infinity();
    Node* const  stop   = m;
    p                   = m;
    do {
        if (hx >= p->x && p->x >= mx && hx != p->x &&
            inTriangle(hx, hy, qx, hy, mx, my, p->x, p->y)) {
            const double t = std::abs(hy - p->y) / (hx - p->x);
            if (locallyInside(p, hole) &&
                (t < tanMin || (t == tanMin && p->x > m->x))) {
                m      = p;
                tanMin = t;
            }
        }
        p = p->next;
    } while (p != stop);
    return m;
}

// Länkar a → b och lägger dubbletter a2, b2 som stänger den andra sidan:
// … a → b … och … b2 → a2 → (a:s gamla nästa) … — för ett hål blir de två
// ringarna en, för en diagonal blir en ring två. Returnerar b2.
Node* Triangulator::split(Node* a, Node* b)
{
    Node* a2 = add(a->i, nullptr);   // samma punkt — samma koordinater
    Node* b2 = add(b->i, nullptr);
    Node* an = a->next;
    Node* bp = b->prev;
    a->next  = b;
    b->prev  = a;
    a2->next = an;
    an->prev = a2;
    b2->next = a2;
    a2->prev = b2;
    bp->next = b2;
    b2->prev = bp;
    return b2;
}

// Hålet bryggas in i ringen. Returnerar ringens nya start, eller nullptr
// när rensningen efter bryggan lämnade färre än tre noder (ingen yta).
Node* Triangulator::bridgeHole(Node* hole, Node* outer)
{
    Node* bridge = findBridge(hole, outer);
    if (!bridge)
        return outer;   // ingen kant till vänster: hålet ligger utanför
    Node* back = split(bridge, hole);
    filterPoints(back, back->next);
    return filterPoints(bridge, bridge->next);
}

// Ligger p i örats triangel så att den skymmer diagonalen? Bara reflexa och
// raka noder kan göra det (en konvex nod i triangeln drar alltid en reflex
// med sig), och en nod PÅ a:s eller c:s plats är en bryggdubblett — dess
// kanter kan inte gå in i triangeln utan att korsa en kant.
bool Triangulator::blocks(const Node* a, const Node* b, const Node* c,
                          const Node* p) const
{
    if (samePlace(p, a) || samePlace(p, c))
        return false;
    return inTriangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) &&
           turn(p->prev, p, p->next) <= 0.0;
}

bool Triangulator::isEar(const Node* ear) const
{
    const Node* a = ear->prev;
    const Node* c = ear->next;
    if (turn(a, ear, c) <= 0.0)
        return false;   // reflex eller rak: inget öra
    for (const Node* p = c->next; p != a; p = p->next)
        if (blocks(a, ear, c, p))
            return false;
    return true;
}

bool Triangulator::isEarHashed(const Node* ear) const
{
    const Node* a = ear->prev;
    const Node* c = ear->next;
    if (turn(a, ear, c) <= 0.0)
        return false;

    const double  x0 = std::min({a->x, ear->x, c->x});
    const double  y0 = std::min({a->y, ear->y, c->y});
    const double  x1 = std::max({a->x, ear->x, c->x});
    const double  y1 = std::max({a->y, ear->y, c->y});
    const std::uint32_t zMin = zOrder(x0, y0);
    const std::uint32_t zMax = zOrder(x1, y1);
    const auto inBox = [&](const Node* p) {
        return p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1;
    };
    const auto test = [&](const Node* p) {
        return p != a && p != c && inBox(p) && blocks(a, ear, c, p);
    };

    // Åt båda håll längs z-listan från örat, så länge värdena ligger i
    // triangellådans intervall.
    const Node* p = ear->prevZ;
    const Node* n = ear->nextZ;
    while (p && p->z >= zMin && n && n->z <= zMax) {
        if (test(p) || test(n))
            return false;
        p = p->prevZ;
        n = n->nextZ;
    }
    for (; p && p->z >= zMin; p = p->prevZ)
        if (test(p))
            return false;
    for (; n && n->z <= zMax; n = n->nextZ)
        if (test(n))
            return false;
    return true;
}

// Två kanter med en nod emellan som korsar varandra (a → p och p.next → b)
// bildar en vriden ögla: triangeln (a, p, b) täcker den, och p och p.next
// tas bort.
Node* Triangulator::cureLocalIntersections(Node* start)
{
    if (!start)
        return nullptr;
    Node* p = start;
    do {
        if (p->next == p->prev)
            return nullptr;   // färre än tre noder kvar: ingen yta att bota
        Node* a = p->prev;
        Node* b = p->next->next;
        if (!samePlace(a, b) && segmentsMeet(a, p, p->next, b) &&
            locallyInside(a, b) && locallyInside(b, a)) {
            emit(a, p, b);
            unlink(p);
            unlink(p->next);
            p = start = b;
        }
        p = p->next;
    } while (p != start);
    return filterPoints(p, nullptr);
}

// De billiga lokala proven först; de två som går runt hela ringen dras från
// arbetsbudgeten.
bool Triangulator::isValidDiagonal(const Node* a, const Node* b)
{
    if (a->next->i == b->i || a->prev->i == b->i || !locallyInside(a, b) ||
        !locallyInside(b, a))
        return false;
    long       steps = 0;
    const bool ok    = !crossesRing(a, b, steps) && middleInside(a, b, steps);
    budget_ -= steps;
    return ok;
}

// Sista reservsteget: dela ringen längs den första giltiga diagonalen och
// klipp halvorna var för sig. Finns ingen — eller är arbetsbudgeten slut —
// stängs resten med en solfjäder.
void Triangulator::splitAndClip(Node* start)
{
    Node* a = start;
    do {
        for (Node* b = a->next->next; b != a->prev && budget_ > 0;
             b = b->next) {
            if (a->i != b->i && isValidDiagonal(a, b)) {
                Node* c = split(a, b);
                a       = filterPoints(a, a->next);
                c       = filterPoints(c, c->next);
                clip(a, 0);
                clip(c, 0);
                return;
            }
        }
        a = a->next;
    } while (a != start);
    fan(start);
    failed_ = true;
}

void Triangulator::fan(Node* start)
{
    for (Node* p = start->next; p->next != start; p = p->next)
        emit(start, p, p->next);
}

void Triangulator::clip(Node* ear, int pass)
{
    if (!ear)
        return;
    if (pass == 0 && hashed_)
        indexCurve(ear);
    Node* stop = ear;
    while (ear->prev != ear->next) {   // fler än två noder kvar
        Node* prev = ear->prev;
        Node* next = ear->next;
        if (hashed_ ? isEarHashed(ear) : isEar(ear)) {
            emit(prev, ear, next);
            unlink(ear);
            // Nästa öra prövas en nod längre fram: smala trianglar i följd
            // blir färre så.
            ear  = next->next;
            stop = ear;
            continue;
        }
        ear = next;
        if (ear == stop) {
            // Ett helt varv utan öra — reservstegen i tur och ordning.
            if (pass == 0)
                clip(filterPoints(ear, nullptr), 1);
            else if (pass == 1)
                clip(cureLocalIntersections(filterPoints(ear, nullptr)), 2);
            else
                splitAndClip(ear);
            break;
        }
    }
}

std::uint32_t Triangulator::zOrder(double x, double y) const
{
    const auto cell = [](double v) {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0, 32767.0));
    };
    return spread(cell((x - minX_) * invSize_)) |
           (spread(cell((y - minY_) * invSize_)) << 1);
}

// z-listan för ringen från start: värdena räknas om och noderna sorteras —
// också efter en delning, då varje halva får en egen lista.
void Triangulator::indexCurve(Node* start)
{
    std::vector<Node*> ring;
    Node*              p = start;
    do {
        p->z = zOrder(p->x, p->y);
        ring.push_back(p);
        p = p->next;
    } while (p != start);
    std::sort(ring.begin(), ring.end(),
              [](const Node* a, const Node* b) { return a->z < b->z; });
    for (std::size_t k = 0; k < ring.size(); ++k) {
        ring[k]->prevZ = k > 0 ? ring[k - 1] : nullptr;
        ring[k]->nextZ = k + 1 < ring.size() ? ring[k + 1] : nullptr;
    }
}

bool Triangulator::run(std::span<const std::uint32_t> ringStarts)
{
    if (ringStarts.empty() || ringStarts[0] >= pts_.size())
        return true;
    const auto ringEnd = [&](std::size_t k) {
        return k + 1 < ringStarts.size()
                   ? ringStarts[k + 1]
                   : static_cast<std::uint32_t>(pts_.size());
    };
    ox_ = pts_[ringStarts[0]].x;
    oy_ = pts_[ringStarts[0]].y;

    // Den förväntade arean: ytterringen minus hålen (i lokala koordinater).
    double expected = 0.0, scale = 0.0;
    for (std::size_t k = 0; k < ringStarts.size(); ++k) {
        double a = 0.0;
        const std::uint32_t b = ringStarts[k], e = ringEnd(k);
        if (e - b < 3)
            continue;
        for (std::uint32_t i = b, j = e - 1; i < e; j = i++)
            a += (pts_[j].x - ox_) * (pts_[i].y - oy_) -
                 (pts_[i].x - ox_) * (pts_[j].y - oy_);
        a = std::abs(a) / 2.0;
        expected += k == 0 ? a : -a;
        scale += a;
    }

    if (ringEnd(0) - ringStarts[0] < 3)
        return true;   // ingen ytterring: ingen yta, inget fel
    Node* outer = linkRing(ringStarts[0], ringEnd(0), true);
    if (!outer || outer->next == outer->prev)
        return true;

    std::vector<Node*> holes;
    for (std::size_t k = 1; k < ringStarts.size(); ++k) {
        if (ringEnd(k) - ringStarts[k] < 3)
            continue;
        Node* h = linkRing(ringStarts[k], ringEnd(k), false);
        if (!h || h->next == h->prev)
            continue;
        Node* left = h;   // vänstraste noden (lägsta y vid lika x)
        for (Node* p = h->next; p != h; p = p->next)
            if (p->x < left->x || (p->x == left->x && p->y < left->y))
                left = p;
        holes.push_back(left);
    }
    std::sort(holes.begin(), holes.end(), [](const Node* a, const Node* b) {
        return a->x < b->x || (a->x == b->x && a->y < b->y);
    });
    for (Node* h : holes) {
        outer = bridgeHole(h, outer);
        if (!outer)
            return std::abs(expected) <= 1e-7 * std::max(scale, 1e-300);
    }

    // Stora ringar: z-ordning över ringens låda.
    std::size_t count = 0;
    double      x1 = outer->x, y1 = outer->y;
    minX_ = outer->x;
    minY_ = outer->y;
    Node* p = outer;
    do {
        ++count;
        minX_ = std::min(minX_, p->x);
        minY_ = std::min(minY_, p->y);
        x1    = std::max(x1, p->x);
        y1    = std::max(y1, p->y);
        p     = p->next;
    } while (p != outer);
    if (count > 80) {
        const double size = std::max(x1 - minX_, y1 - minY_);
        invSize_          = size > 0.0 ? 32767.0 / size : 0.0;
        hashed_           = invSize_ > 0.0;
    }

    const std::size_t first = out_.size();
    clip(outer, 0);

    // Kontrollen: trianglarna ska täcka exakt ytterringen minus hålen.
    double covered = 0.0;
    for (std::size_t t = first; t + 2 < out_.size(); t += 3) {
        const Point2d& a = pts_[out_[t]];
        const Point2d& b = pts_[out_[t + 1]];
        const Point2d& c = pts_[out_[t + 2]];
        covered += std::abs((b.x - a.x) * (c.y - a.y) -
                            (b.y - a.y) * (c.x - a.x)) / 2.0;
    }
    const bool areaOk = std::abs(covered - expected) <=
                        1e-7 * std::max(scale, 1e-300);
    return !failed_ && areaOk;
}

} // namespace

bool triangulate(std::span<const Point2d>       points,
                 std::span<const std::uint32_t> ringStarts,
                 std::vector<std::uint32_t>&    out)
{
    Triangulator t(points, out);
    return t.run(ringStarts);
}

} // namespace gfx
