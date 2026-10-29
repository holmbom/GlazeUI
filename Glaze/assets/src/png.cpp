// PNG-codecen — avkodare och deterministisk kodare i REN STD.
//
// Tre delar, nerifrån och upp:
//   INFLATE   zlib-strömmens deflate (RFC 1951) — lagrade block, fast och
//             dynamisk Huffman. Kanonisk avkodning efter zlib:s puff.c:
//             räkna koder per längd, gå längd för längd — liten och korrekt,
//             snabbheten spelar ingen roll för en ikon.
//   DEFLATE   kodarens: LZ77 över ett 32 KiB-fönster med hashkedjor (fast
//             kedjetak, girig matchning) och den FASTA Huffman-tabellen i ett
//             enda slutblock. Ingen dynamisk tabell: den sparar några procent
//             och kostar en längdbegränsad kodbyggare — fel avvägning för ett
//             verktyg som skriver tio ikoner.
//   PNG       chunkarna (CRC på var och en), IHDR/PLTE/tRNS/IDAT/IEND,
//             filtren 0–4, Adam7 och omvandlingen till RGBA8.
//
// Allt läses byte för byte — ingen memcpy av structar, ingen endian-gissning.

#include "glaze_assets/png.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "crc32.h"

namespace glzassets {
namespace {

constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr int kMaxSide   = 16384;
constexpr std::uint64_t kMaxPixels = 64ull * 1024 * 1024;

std::uint32_t rdBE32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

void wrBE32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(std::uint8_t(x >> 24));
    v.push_back(std::uint8_t((x >> 16) & 0xFF));
    v.push_back(std::uint8_t((x >> 8) & 0xFF));
    v.push_back(std::uint8_t(x & 0xFF));
}

std::uint32_t adler32(const std::uint8_t* p, std::size_t n)
{
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// Längd- och avståndstabellerna (RFC 1951 3.2.5) — delade av båda hållen.
constexpr std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11, 13,
                                        15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                        67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,    25,
    33,   49,   65,   97,   129,  193,   257,   385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,
                                         4, 4, 5, 5, 6, 6, 7, 7,  8,  8,
                                         9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// ---------------------------------------------------------------- INFLATE

struct Huffman {
    std::array<std::uint16_t, 16>  count{};   // antal koder per längd
    std::array<std::uint16_t, 320> symbol{};  // symbolerna i kanonisk ordning
};

// Bygger tabellen ur kodlängderna. false för en överfull uppsättning; en
// ofullständig är tillåten (RFC:n tillåter en enda avståndskod).
bool buildHuffman(Huffman& h, const std::uint8_t* lengths, int n)
{
    h.count.fill(0);
    for (int i = 0; i < n; ++i)
        ++h.count[lengths[i]];
    if (h.count[0] == n)
        return true;   // inga koder alls — upptäcks vid avkodning
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[std::size_t(len)];
        if (left < 0)
            return false;
    }
    std::array<std::uint16_t, 16> offs{};
    for (int len = 1; len < 15; ++len)
        offs[std::size_t(len) + 1] = std::uint16_t(offs[std::size_t(len)] + h.count[std::size_t(len)]);
    for (int i = 0; i < n; ++i)
        if (lengths[i] != 0)
            h.symbol[offs[lengths[i]]++] = std::uint16_t(i);
    return true;
}

class Inflater {
public:
    Inflater(const std::uint8_t* p, std::size_t n, std::size_t limit,
             std::vector<std::uint8_t>& out)
        : p_(p), n_(n), limit_(limit), out_(out)
    {
    }

    bool run(std::string& err)
    {
        int last = 0;
        do {
            if (!need(3))
                return fail(err, "deflate-strömmen tar slut");
            last = int(bits(1));
            const int type = int(bits(2));
            bool ok = false;
            if (type == 0)
                ok = stored(err);
            else if (type == 1)
                ok = fixedBlock(err);
            else if (type == 2)
                ok = dynamicBlock(err);
            else
                return fail(err, "deflate-block av okänd typ 3");
            if (!ok)
                return false;
        } while (!last);
        return true;
    }

    // Byte som förbrukats (hela byte — resten av sista byten kastas).
    std::size_t consumed() const { return pos_; }

private:
    bool fail(std::string& err, const char* why)
    {
        err = why;
        return false;
    }

    bool need(int n)
    {
        while (cnt_ < n) {
            if (pos_ >= n_)
                return false;
            buf_ |= std::uint32_t(p_[pos_++]) << cnt_;
            cnt_ += 8;
        }
        return true;
    }

    std::uint32_t bits(int n)
    {
        const std::uint32_t v = buf_ & ((1u << n) - 1u);
        buf_ >>= n;
        cnt_ -= n;
        return v;
    }

    bool put(std::uint8_t b)
    {
        if (out_.size() >= limit_)
            return false;
        out_.push_back(b);
        return true;
    }

    bool stored(std::string& err)
    {
        buf_ = 0;   // resten av byten kastas
        cnt_ = 0;
        if (pos_ + 4 > n_)
            return fail(err, "lagrat deflate-block tar slut");
        const unsigned len  = unsigned(p_[pos_]) | (unsigned(p_[pos_ + 1]) << 8);
        const unsigned nlen = unsigned(p_[pos_ + 2]) | (unsigned(p_[pos_ + 3]) << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu))
            return fail(err, "lagrat deflate-block med fel längdkomplement");
        if (pos_ + len > n_)
            return fail(err, "lagrat deflate-block tar slut");
        for (unsigned i = 0; i < len; ++i)
            if (!put(p_[pos_ + i]))
                return fail(err, "bilddatan är större än IHDR säger");
        pos_ += len;
        return true;
    }

    // Kanonisk avkodning bit för bit (puff.c). -1 = slut på data eller
    // ogiltig kod.
    int decode(const Huffman& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            if (!need(1))
                return -1;
            code |= int(bits(1));
            const int count = h.count[std::size_t(len)];
            if (code - count < first)
                return h.symbol[std::size_t(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool codes(const Huffman& lit, const Huffman& dist, std::string& err)
    {
        for (;;) {
            const int sym = decode(lit);
            if (sym < 0)
                return fail(err, "ogiltig Huffman-kod i deflate-strömmen");
            if (sym < 256) {
                if (!put(std::uint8_t(sym)))
                    return fail(err, "bilddatan är större än IHDR säger");
                continue;
            }
            if (sym == 256)
                return true;
            const int li = sym - 257;
            if (li >= 29)
                return fail(err, "ogiltig längdkod i deflate-strömmen");
            if (!need(kLenExtra[li]))
                return fail(err, "deflate-strömmen tar slut");
            const unsigned len = kLenBase[li] + bits(kLenExtra[li]);
            const int ds = decode(dist);
            if (ds < 0 || ds >= 30)
                return fail(err, "ogiltig avståndskod i deflate-strömmen");
            if (!need(kDistExtra[ds]))
                return fail(err, "deflate-strömmen tar slut");
            const std::size_t d = kDistBase[ds] + bits(kDistExtra[ds]);
            if (d > out_.size())
                return fail(err, "deflate-avstånd bakom strömmens början");
            for (unsigned i = 0; i < len; ++i)
                if (!put(out_[out_.size() - d]))
                    return fail(err, "bilddatan är större än IHDR säger");
        }
    }

    bool fixedBlock(std::string& err)
    {
        static const std::pair<Huffman, Huffman> fixed = [] {
            std::pair<Huffman, Huffman> t;
            std::uint8_t l[288];
            int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            buildHuffman(t.first, l, 288);
            std::uint8_t d[30];
            for (int k = 0; k < 30; ++k) d[k] = 5;
            buildHuffman(t.second, d, 30);
            return t;
        }();
        return codes(fixed.first, fixed.second, err);
    }

    bool dynamicBlock(std::string& err)
    {
        static constexpr std::uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                   11, 4,  12, 3, 13, 2, 14, 1, 15};
        if (!need(14))
            return fail(err, "deflate-strömmen tar slut");
        const int nlen  = int(bits(5)) + 257;
        const int ndist = int(bits(5)) + 1;
        const int ncode = int(bits(4)) + 4;
        if (nlen > 286 || ndist > 30)
            return fail(err, "dynamiskt deflate-block med för många koder");
        std::uint8_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i) {
            if (!need(3))
                return fail(err, "deflate-strömmen tar slut");
            lengths[order[i]] = std::uint8_t(bits(3));
        }
        Huffman lencode;
        if (!buildHuffman(lencode, lengths, 19))
            return fail(err, "trasig kodlängdstabell i deflate-strömmen");

        int idx = 0;
        while (idx < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 0)
                return fail(err, "ogiltig kodlängdskod i deflate-strömmen");
            if (sym < 16) {
                lengths[idx++] = std::uint8_t(sym);
                continue;
            }
            std::uint8_t val = 0;
            int rep = 0;
            if (sym == 16) {
                if (idx == 0)
                    return fail(err, "upprepning utan föregående kodlängd");
                val = lengths[idx - 1];
                if (!need(2)) return fail(err, "deflate-strömmen tar slut");
                rep = 3 + int(bits(2));
            } else if (sym == 17) {
                if (!need(3)) return fail(err, "deflate-strömmen tar slut");
                rep = 3 + int(bits(3));
            } else {
                if (!need(7)) return fail(err, "deflate-strömmen tar slut");
                rep = 11 + int(bits(7));
            }
            if (idx + rep > nlen + ndist)
                return fail(err, "kodlängderna går förbi tabellens slut");
            while (rep--)
                lengths[idx++] = val;
        }
        if (lengths[256] == 0)
            return fail(err, "dynamiskt deflate-block saknar slutkoden");
        Huffman lit, dist;
        if (!buildHuffman(lit, lengths, nlen) ||
            !buildHuffman(dist, lengths + nlen, ndist))
            return fail(err, "trasig Huffman-tabell i deflate-strömmen");
        return codes(lit, dist, err);
    }

    const std::uint8_t*        p_;
    std::size_t                n_;
    std::size_t                limit_;
    std::vector<std::uint8_t>& out_;
    std::size_t                pos_ = 0;
    std::uint32_t              buf_ = 0;
    int                        cnt_ = 0;
};

// zlib-omslaget: CMF/FLG, deflate, adler32 (big-endian).
bool zlibInflate(const std::uint8_t* p, std::size_t n, std::size_t expected,
                 std::vector<std::uint8_t>& out, std::string& err)
{
    if (n < 6) {
        err = "zlib-strömmen är för kort";
        return false;
    }
    const unsigned cmf = p[0], flg = p[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0) {
        err = "zlib-huvudet är trasigt";
        return false;
    }
    if (flg & 0x20) {
        err = "zlib-strömmen kräver en förinställd ordlista";
        return false;
    }
    out.clear();
    out.reserve(expected);
    Inflater inf(p + 2, n - 2, expected, out);
    if (!inf.run(err))
        return false;
    const std::size_t end = 2 + inf.consumed();
    if (end + 4 > n) {
        err = "zlib-strömmen saknar adler32";
        return false;
    }
    if (rdBE32(p + end) != adler32(out.data(), out.size())) {
        err = "adler32-fel i bilddatan";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- DEFLATE

class BitWriter {
public:
    explicit BitWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    // LSB först — deflates bitordning för extrabitar och huvuden.
    void put(std::uint32_t v, int n)
    {
        acc_ |= std::uint64_t(v) << cnt_;
        cnt_ += n;
        while (cnt_ >= 8) {
            out_.push_back(std::uint8_t(acc_ & 0xFF));
            acc_ >>= 8;
            cnt_ -= 8;
        }
    }

    // En Huffman-kod skrivs MSB först — vänd den.
    void code(std::uint32_t c, int n)
    {
        std::uint32_t r = 0;
        for (int i = 0; i < n; ++i)
            r |= ((c >> i) & 1u) << (n - 1 - i);
        put(r, n);
    }

    void flush()
    {
        if (cnt_ > 0)
            out_.push_back(std::uint8_t(acc_ & 0xFF));
        acc_ = 0;
        cnt_ = 0;
    }

private:
    std::vector<std::uint8_t>& out_;
    std::uint64_t              acc_ = 0;
    int                        cnt_ = 0;
};

// Den fasta tabellen (RFC 1951 3.2.6).
void fixedLiteral(BitWriter& bw, int sym)
{
    if (sym < 144)
        bw.code(std::uint32_t(0x30 + sym), 8);
    else if (sym < 256)
        bw.code(std::uint32_t(0x190 + sym - 144), 9);
    else if (sym < 280)
        bw.code(std::uint32_t(sym - 256), 7);
    else
        bw.code(std::uint32_t(0xC0 + sym - 280), 8);
}

void fixedMatch(BitWriter& bw, int len, int dist)
{
    int li = 28;
    while (kLenBase[li] > len)
        --li;
    fixedLiteral(bw, 257 + li);
    bw.put(std::uint32_t(len - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (kDistBase[di] > dist)
        --di;
    bw.code(std::uint32_t(di), 5);
    bw.put(std::uint32_t(dist - kDistBase[di]), kDistExtra[di]);
}

void deflateFixed(const std::vector<std::uint8_t>& in,
                  std::vector<std::uint8_t>& out)
{
    constexpr int kWindow   = 32768;
    constexpr int kHashBits = 15;
    constexpr int kMaxChain = 128;   // FAST tak — bär determinismen
    constexpr int kMinMatch = 3;
    constexpr int kMaxMatch = 258;

    BitWriter bw(out);
    bw.put(1, 1);   // BFINAL
    bw.put(1, 2);   // BTYPE = 01, fast Huffman

    const int n = int(in.size());
    std::vector<int> head(std::size_t(1) << kHashBits, -1);
    std::vector<int> prev(std::size_t(n > 0 ? n : 1), -1);
    auto hashAt = [&](int i) {
        const std::uint32_t v = (std::uint32_t(in[std::size_t(i)]) << 16) |
                                (std::uint32_t(in[std::size_t(i) + 1]) << 8) |
                                std::uint32_t(in[std::size_t(i) + 2]);
        return std::size_t((v * 2654435761u) >> (32 - kHashBits));
    };
    auto insert = [&](int i) {
        if (i + kMinMatch > n)
            return;
        const std::size_t hv = hashAt(i);
        prev[std::size_t(i)] = head[hv];
        head[hv] = i;
    };

    int i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + kMinMatch <= n) {
            int cand = head[hashAt(i)];
            int chain = kMaxChain;
            const int maxLen = std::min(kMaxMatch, n - i);
            while (cand >= 0 && i - cand <= kWindow && chain-- > 0) {
                int l = 0;
                while (l < maxLen && in[std::size_t(cand + l)] == in[std::size_t(i + l)])
                    ++l;
                if (l > bestLen) {
                    bestLen  = l;
                    bestDist = i - cand;
                    if (l == maxLen)
                        break;
                }
                cand = prev[std::size_t(cand)];
            }
        }
        if (bestLen >= kMinMatch) {
            fixedMatch(bw, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k)
                insert(i + k);
            i += bestLen;
        } else {
            fixedLiteral(bw, in[std::size_t(i)]);
            insert(i);
            ++i;
        }
    }
    fixedLiteral(bw, 256);
    bw.flush();
}

// ---------------------------------------------------------------- PNG

int paeth(int a, int b, int c)
{
    const int p  = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

// Omvänder radfiltret på plats. `prior` är föregående (redan avfiltrerade)
// rad eller nullptr för passets första.
bool unfilter(int type, std::uint8_t* row, const std::uint8_t* prior,
              std::size_t len, std::size_t bpp)
{
    switch (type) {
    case 0:
        return true;
    case 1:
        for (std::size_t i = bpp; i < len; ++i)
            row[i] = std::uint8_t(row[i] + row[i - bpp]);
        return true;
    case 2:
        if (prior)
            for (std::size_t i = 0; i < len; ++i)
                row[i] = std::uint8_t(row[i] + prior[i]);
        return true;
    case 3:
        for (std::size_t i = 0; i < len; ++i) {
            const int a = i >= bpp ? row[i - bpp] : 0;
            const int b = prior ? prior[i] : 0;
            row[i] = std::uint8_t(row[i] + ((a + b) >> 1));
        }
        return true;
    case 4:
        for (std::size_t i = 0; i < len; ++i) {
            const int a = i >= bpp ? row[i - bpp] : 0;
            const int b = prior ? prior[i] : 0;
            const int c = (prior && i >= bpp) ? prior[i - bpp] : 0;
            row[i] = std::uint8_t(row[i] + paeth(a, b, c));
        }
        return true;
    default:
        return false;
    }
}

struct Header {
    int width = 0, height = 0, depth = 0, color = 0, interlace = 0;
    int channels() const
    {
        switch (color) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        default: return 4;
        }
    }
};

bool validDepth(int color, int depth)
{
    switch (color) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2:
    case 4:
    case 6: return depth == 8 || depth == 16;
    default: return false;
    }
}

std::size_t rowBytes(const Header& h, int w)
{
    return (std::size_t(w) * std::size_t(h.channels()) * std::size_t(h.depth) + 7) / 8;
}

// Sampel nr `i` i en avfiltrerad rad, i sitt råa bitdjup.
unsigned sample(const std::uint8_t* row, std::size_t i, int depth)
{
    switch (depth) {
    case 16: return (unsigned(row[i * 2]) << 8) | row[i * 2 + 1];
    case 8:  return row[i];
    default: {
        const std::size_t bit = i * std::size_t(depth);
        const unsigned byte = row[bit / 8];
        const unsigned shift = unsigned(8 - depth) - unsigned(bit % 8);
        return (byte >> shift) & ((1u << depth) - 1u);
    }
    }
}

std::uint8_t to8(unsigned v, int depth)
{
    switch (depth) {
    case 16: return std::uint8_t((v * 255u + 32767u) / 65535u);
    case 8:  return std::uint8_t(v);
    case 4:  return std::uint8_t(v * 17u);
    case 2:  return std::uint8_t(v * 85u);
    default: return std::uint8_t(v * 255u);
    }
}

struct Palette {
    std::uint8_t rgba[256][4];
    int          size = 0;
};

struct Trns {
    bool     has = false;
    unsigned gray = 0, r = 0, g = 0, b = 0;   // i råa bitdjupet
};

// En avfiltrerad rad (passets bredd `w`) till RGBA8 vid x0 + i·dx på rad y.
void emitRow(const Header& h, const Palette& pal, const Trns& trns,
             const std::uint8_t* row, int w, int y, int x0, int dx, Rgba& out)
{
    const int ch = h.channels();
    for (int i = 0; i < w; ++i) {
        std::uint8_t* o =
            out.px.data() +
            (std::size_t(y) * std::size_t(out.width) + std::size_t(x0 + i * dx)) * 4;
        const std::size_t s = std::size_t(i) * std::size_t(ch);
        switch (h.color) {
        case 0: {
            const unsigned v = sample(row, s, h.depth);
            o[0] = o[1] = o[2] = to8(v, h.depth);
            o[3] = (trns.has && v == trns.gray) ? 0 : 255;
            break;
        }
        case 2: {
            const unsigned r = sample(row, s, h.depth);
            const unsigned g = sample(row, s + 1, h.depth);
            const unsigned b = sample(row, s + 2, h.depth);
            o[0] = to8(r, h.depth);
            o[1] = to8(g, h.depth);
            o[2] = to8(b, h.depth);
            o[3] = (trns.has && r == trns.r && g == trns.g && b == trns.b) ? 0 : 255;
            break;
        }
        case 3: {
            const unsigned idx = sample(row, s, h.depth);
            // Ett index förbi paletten ger svart och genomskinligt — specen
            // kallar det ett fel, men en ikon ska inte falla på det.
            if (int(idx) < pal.size)
                std::memcpy(o, pal.rgba[idx], 4);
            else
                o[0] = o[1] = o[2] = o[3] = 0;
            break;
        }
        case 4:
            o[0] = o[1] = o[2] = to8(sample(row, s, h.depth), h.depth);
            o[3] = to8(sample(row, s + 1, h.depth), h.depth);
            break;
        default:
            for (int c = 0; c < 4; ++c)
                o[c] = to8(sample(row, s + std::size_t(c), h.depth), h.depth);
            break;
        }
    }
}

void chunk(std::vector<std::uint8_t>& out, const char type[4],
           const std::vector<std::uint8_t>& data)
{
    wrBE32(out, std::uint32_t(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    wrBE32(out, detail::crc32(out.data() + start, out.size() - start));
}

} // namespace

bool decodePng(const std::uint8_t* data, std::size_t size, Rgba& out,
               std::string& err)
{
    out = {};
    if (size < 8 || std::memcmp(data, kSig, 8) != 0) {
        err = "inte en PNG (fel signatur)";
        return false;
    }
    Header  h;
    Palette pal;
    Trns    trns;
    std::vector<std::uint8_t> idat;
    bool sawHeader = false, sawEnd = false, sawData = false;

    std::size_t pos = 8;
    while (pos < size && !sawEnd) {
        if (pos + 12 > size) {
            err = "PNG-chunk tar slut";
            return false;
        }
        const std::uint32_t len = rdBE32(data + pos);
        if (len > 0x7FFFFFFFu || std::size_t(len) > size - pos - 12) {
            err = "PNG-chunk med orimlig längd";
            return false;
        }
        const std::uint8_t* type = data + pos + 4;
        const std::uint8_t* body = data + pos + 8;
        if (detail::crc32(type, std::size_t(len) + 4) != rdBE32(body + len)) {
            err = "CRC-fel i PNG-chunken " + std::string(reinterpret_cast<const char*>(type), 4);
            return false;
        }
        const std::string t(reinterpret_cast<const char*>(type), 4);
        if (!sawHeader && t != "IHDR") {
            err = "PNG börjar inte med IHDR";
            return false;
        }
        if (t == "IHDR") {
            if (sawHeader || len != 13) {
                err = "trasig IHDR";
                return false;
            }
            sawHeader   = true;
            const std::uint32_t w = rdBE32(body), hh = rdBE32(body + 4);
            h.depth     = body[8];
            h.color     = body[9];
            h.interlace = body[12];
            if (w == 0 || hh == 0 || w > std::uint32_t(kMaxSide) ||
                hh > std::uint32_t(kMaxSide) ||
                std::uint64_t(w) * hh > kMaxPixels) {
                err = "PNG med orimlig storlek";
                return false;
            }
            h.width  = int(w);
            h.height = int(hh);
            if (!validDepth(h.color, h.depth) || body[10] != 0 || body[11] != 0 ||
                h.interlace > 1) {
                err = "PNG med okänd färgtyp, bitdjup, komprimering eller interlace";
                return false;
            }
        } else if (t == "PLTE") {
            if (len % 3 != 0 || len / 3 > 256 || len == 0) {
                err = "trasig PLTE";
                return false;
            }
            pal.size = int(len / 3);
            for (int i = 0; i < pal.size; ++i) {
                pal.rgba[i][0] = body[i * 3];
                pal.rgba[i][1] = body[i * 3 + 1];
                pal.rgba[i][2] = body[i * 3 + 2];
                pal.rgba[i][3] = 255;
            }
        } else if (t == "tRNS") {
            if (h.color == 3) {
                for (std::uint32_t i = 0; i < len && i < 256; ++i)
                    pal.rgba[i][3] = body[i];
            } else if (h.color == 0 && len >= 2) {
                trns.has  = true;
                trns.gray = (unsigned(body[0]) << 8) | body[1];
            } else if (h.color == 2 && len >= 6) {
                trns.has = true;
                trns.r   = (unsigned(body[0]) << 8) | body[1];
                trns.g   = (unsigned(body[2]) << 8) | body[3];
                trns.b   = (unsigned(body[4]) << 8) | body[5];
            }
        } else if (t == "IDAT") {
            idat.insert(idat.end(), body, body + len);
            sawData = true;
        } else if (t == "IEND") {
            sawEnd = true;
        } else if (type[0] >= 'A' && type[0] <= 'Z') {
            err = "okänd kritisk PNG-chunk " + t;
            return false;
        }
        pos += std::size_t(len) + 12;
    }
    if (!sawEnd || !sawData) {
        err = sawData ? "PNG saknar IEND" : "PNG saknar bilddata (IDAT)";
        return false;
    }
    if (h.color == 3 && pal.size == 0) {
        err = "palett-PNG utan PLTE";
        return false;
    }

    // Passen: hela bilden, eller Adam7:s sju.
    struct Pass { int x0, y0, dx, dy; };
    static constexpr Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8},
                                       {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2},
                                       {0, 1, 1, 2}};
    static constexpr Pass kWhole[1] = {{0, 0, 1, 1}};
    const Pass* passes = h.interlace ? kAdam7 : kWhole;
    const int   npass  = h.interlace ? 7 : 1;

    std::size_t expected = 0;
    for (int p = 0; p < npass; ++p) {
        const int pw = (h.width - passes[p].x0 + passes[p].dx - 1) / passes[p].dx;
        const int ph = (h.height - passes[p].y0 + passes[p].dy - 1) / passes[p].dy;
        if (pw > 0 && ph > 0)
            expected += (rowBytes(h, pw) + 1) * std::size_t(ph);
    }

    std::vector<std::uint8_t> raw;
    if (!zlibInflate(idat.data(), idat.size(), expected, raw, err))
        return false;
    if (raw.size() != expected) {
        err = "bilddatan är kortare än IHDR säger";
        return false;
    }

    out.width  = h.width;
    out.height = h.height;
    out.px.assign(std::size_t(h.width) * std::size_t(h.height) * 4, 0);
    const std::size_t bpp =
        std::max<std::size_t>(1, std::size_t(h.channels()) * std::size_t(h.depth) / 8);
    std::size_t off = 0;
    for (int p = 0; p < npass; ++p) {
        const int pw = (h.width - passes[p].x0 + passes[p].dx - 1) / passes[p].dx;
        const int ph = (h.height - passes[p].y0 + passes[p].dy - 1) / passes[p].dy;
        if (pw <= 0 || ph <= 0)
            continue;
        const std::size_t rb = rowBytes(h, pw);
        const std::uint8_t* prior = nullptr;
        for (int y = 0; y < ph; ++y) {
            const int ft = raw[off];
            std::uint8_t* row = raw.data() + off + 1;
            if (!unfilter(ft, row, prior, rb, bpp)) {
                out = {};
                err = "okänt PNG-radfilter " + std::to_string(ft);
                return false;
            }
            emitRow(h, pal, trns, row, pw, passes[p].y0 + y * passes[p].dy,
                    passes[p].x0, passes[p].dx, out);
            prior = row;
            off += rb + 1;
        }
    }
    return true;
}

std::vector<std::uint8_t> encodePng(const Rgba& img)
{
    std::vector<std::uint8_t> out;
    if (img.empty() ||
        img.px.size() < std::size_t(img.width) * std::size_t(img.height) * 4)
        return out;

    // Filtren: per rad det av 0–4 med minsta summan av |signerad byte| —
    // första lägsta vinner vid lika, så valet är deterministiskt.
    const std::size_t stride = std::size_t(img.width) * 4;
    std::vector<std::uint8_t> filtered;
    filtered.reserve((stride + 1) * std::size_t(img.height));
    std::vector<std::uint8_t> cand(stride), best(stride);
    for (int y = 0; y < img.height; ++y) {
        const std::uint8_t* row   = img.px.data() + std::size_t(y) * stride;
        const std::uint8_t* prior = y > 0 ? row - stride : nullptr;
        std::uint64_t bestSum = ~std::uint64_t(0);
        int bestType = 0;
        for (int ft = 0; ft < 5; ++ft) {
            std::uint64_t sum = 0;
            for (std::size_t i = 0; i < stride; ++i) {
                const int a = i >= 4 ? row[i - 4] : 0;
                const int b = prior ? prior[i] : 0;
                const int c = (prior && i >= 4) ? prior[i - 4] : 0;
                int pred = 0;
                switch (ft) {
                case 1: pred = a; break;
                case 2: pred = b; break;
                case 3: pred = (a + b) >> 1; break;
                case 4: pred = paeth(a, b, c); break;
                default: break;
                }
                cand[i] = std::uint8_t(row[i] - pred);
                sum += std::uint64_t(std::abs(int(std::int8_t(cand[i]))));
            }
            if (sum < bestSum) {
                bestSum  = sum;
                bestType = ft;
                best.swap(cand);
            }
        }
        filtered.push_back(std::uint8_t(bestType));
        filtered.insert(filtered.end(), best.begin(), best.end());
    }

    std::vector<std::uint8_t> z = {0x78, 0x01};   // deflate, 32 KiB fönster
    deflateFixed(filtered, z);
    wrBE32(z, adler32(filtered.data(), filtered.size()));

    out.assign(kSig, kSig + 8);
    std::vector<std::uint8_t> ihdr;
    wrBE32(ihdr, std::uint32_t(img.width));
    wrBE32(ihdr, std::uint32_t(img.height));
    ihdr.push_back(8);   // bitdjup
    ihdr.push_back(6);   // RGBA
    ihdr.push_back(0);   // deflate
    ihdr.push_back(0);   // filtermetod 0
    ihdr.push_back(0);   // ingen interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

} // namespace glzassets
