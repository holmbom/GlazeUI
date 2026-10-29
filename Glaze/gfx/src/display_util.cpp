// Skärmens fysiska upplösning — det backend-neutrala (se display_util.h) och
// fasadens kalibrering (`gfx::setDisplayCalibration`/`effectiveDisplay`,
// platform.h). Delas av alla tre backends.

#include "display_util.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "gfx/platform.h"

namespace gfx::display {

namespace {

// EDID:s åtta första byte — blockets signatur.
constexpr std::uint8_t kHuvud[8] = {0x00, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0x00};

// Basblockets geometri (VESA E-EDID 1.4): fyra deskriptorblock à 18 byte
// från byte 54, och bildstorleken i hela centimeter på 21/22.
constexpr std::size_t kBlock       = 128;
constexpr std::size_t kDeskriptor0 = 54;
constexpr std::size_t kDeskriptorN = 4;
constexpr std::size_t kDeskriptorB = 18;
constexpr int         kMmPerCm     = 10;

} // namespace

std::string edidId(std::uint16_t manufacturer, std::uint16_t product,
                   std::uint32_t serial)
{
    // Tre bokstäver à fem bitar, 1 = 'A'. En kod utanför A–Z blir '?' —
    // talet är ändå en identitet, bara inte en läsbar.
    auto bokstav = [](unsigned v) {
        return v >= 1 && v <= 26 ? char('A' + v - 1) : '?';
    };
    char buf[32];
    std::snprintf(buf, sizeof buf, "%c%c%c-%04X-%08lX",
                  bokstav((manufacturer >> 10) & 0x1Fu),
                  bokstav((manufacturer >> 5) & 0x1Fu),
                  bokstav(manufacturer & 0x1Fu), unsigned(product),
                  static_cast<unsigned long>(serial));
    return buf;
}

bool parseEdid(const std::uint8_t* edid, std::size_t len, Edid& ut)
{
    if (!edid || len < kBlock)
        return false;
    for (std::size_t i = 0; i < sizeof kHuvud; ++i)
        if (edid[i] != kHuvud[i])
            return false;

    Edid e;
    e.id = edidId(std::uint16_t((edid[8] << 8) | edid[9]),
                  std::uint16_t(edid[10] | (edid[11] << 8)),
                  std::uint32_t(edid[12]) | (std::uint32_t(edid[13]) << 8) |
                      (std::uint32_t(edid[14]) << 16) |
                      (std::uint32_t(edid[15]) << 24));
    for (std::size_t i = 0; i < kDeskriptorN; ++i) {
        const std::uint8_t* d = edid + kDeskriptor0 + i * kDeskriptorB;
        if (d[0] == 0 && d[1] == 0)
            continue;   // MONITOR-deskriptor (namn, serie, gränser) — vakten
        const int w = d[12] | ((d[14] >> 4) << 8);
        const int h = d[13] | ((d[14] & 0x0F) << 8);
        if (w > 0 && h > 0) {
            e.widthMm  = w;
            e.heightMm = h;
            break;
        }
    }
    if (e.widthMm == 0 && edid[21] > 0 && edid[22] > 0) {
        e.widthMm  = edid[21] * kMmPerCm;
        e.heightMm = edid[22] * kMmPerCm;
    }
    ut = std::move(e);
    return true;
}

float pxPerMmFromDiagonal(int pxW, int pxH, double mmW, double mmH)
{
    if (pxW <= 0 || pxH <= 0 || !(mmW > 0.0) || !(mmH > 0.0))
        return 0.0f;
    const double px  = std::hypot(double(pxW), double(pxH));
    const double mm  = std::hypot(mmW, mmH);
    const double per = px / mm;
    const double dpi = per * kMmPerInch;
    if (!std::isfinite(dpi) || dpi < kMinDpi || dpi > kMaxDpi)
        return 0.0f;
    return float(per);
}

bool isApple72Estimate(int pxW, int pxH, double mmW, double mmH)
{
    constexpr double kApplePxPerMm = 72.0 / kMmPerInch;   // 2,835
    constexpr double kMarginal     = 1.0;                 // mm per axel
    if (pxW <= 0 || pxH <= 0 || !(mmW > 0.0) || !(mmH > 0.0))
        return false;
    return std::abs(mmW - pxW / kApplePxPerMm) <= kMarginal &&
           std::abs(mmH - pxH / kApplePxPerMm) <= kMarginal;
}

DisplayMetrics assumed(std::string id, std::string reason)
{
    DisplayMetrics m;
    m.pxPerMm = kAssumedPxPerMm;   // 96 dpi i fysiska px, vid varje skala
    m.source  = DisplaySource::Assumed;
    m.id      = std::move(id);
    m.reason  = std::move(reason);
    return m;
}

DisplayMetrics measure(int pxW, int pxH, int mmW, int mmH, std::string id)
{
    const float per = pxPerMmFromDiagonal(pxW, pxH, mmW, mmH);
    DisplayMetrics m;
    if (per > 0.0f) {
        m.pxPerMm = per;
        m.source  = DisplaySource::Measured;
        m.id      = std::move(id);
    } else if (mmW <= 0 || mmH <= 0) {
        m = assumed(std::move(id), "skärmen anger inga mått");
    } else if (pxW <= 0 || pxH <= 0) {
        m = assumed(std::move(id), "skärmens pixlar är okända");
    } else {
        char buf[96];
        std::snprintf(buf, sizeof buf,
                      "måtten %d × %d mm ger %.0f dpi — utanför %.0f–%.0f",
                      mmW, mmH,
                      std::hypot(double(pxW), double(pxH)) /
                          std::hypot(double(mmW), double(mmH)) * kMmPerInch,
                      double(kMinDpi), double(kMaxDpi));
        m = assumed(std::move(id), buf);
    }
    m.widthMm  = mmW > 0 ? mmW : 0;
    m.heightMm = mmH > 0 ? mmH : 0;
    return m;
}

} // namespace gfx::display

namespace gfx {

namespace {

// KALIBRERINGARNA, per skärm-id. UI-trådens, som resten av fasaden.
std::map<std::string, float>& kalibreringar()
{
    static std::map<std::string, float> k;
    return k;
}

} // namespace

void setDisplayCalibration(const std::string& id, float pxPerMm)
{
    if (id.empty()) {
        log("setDisplayCalibration: tomt skärm-id — ignoreras (en skärm utan "
            "id kan inte kännas igen)");
        return;
    }
    if (!std::isfinite(pxPerMm) || pxPerMm <= 0.0f) {
        kalibreringar().erase(id);   // 0 tar bort — skärmen mäts igen
        return;
    }
    const float dpi = pxPerMm * display::kMmPerInch;
    if (dpi < display::kMinDpi || dpi > display::kMaxDpi) {
        log("setDisplayCalibration(%s): %.3f px/mm = %.0f dpi ligger utanför "
            "%.0f–%.0f — ignoreras", id.c_str(), double(pxPerMm), double(dpi),
            double(display::kMinDpi), double(display::kMaxDpi));
        return;
    }
    kalibreringar()[id] = pxPerMm;
}

DisplayMetrics effectiveDisplay(const DisplayMetrics& m)
{
    const auto it = m.id.empty() ? kalibreringar().end()
                                 : kalibreringar().find(m.id);
    if (it == kalibreringar().end())
        return m;
    DisplayMetrics k = m;
    k.pxPerMm = it->second;
    k.source  = DisplaySource::Calibrated;
    k.reason.clear();
    return k;
}

} // namespace gfx
