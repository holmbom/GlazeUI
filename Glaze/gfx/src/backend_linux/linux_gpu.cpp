// Grafikkorten på Linux — listan (`gfx::gpuAdapters`, ur
// sysfs), VALET i processen (`applyGpuEnvironment`, först i `gfx::init`),
// kortet EGL faktiskt ritar med (`eglAdapterId`), kontrollen efteråt
// (`reportGpuOutcome`) och barnprocessernas miljö (`linuxChildEnvironment`).
// Regeln själv — vilket kort, vilken variabel — står i ../gpu_util.h och mäts
// headless av gpu_gate.
//
// VARFÖR MILJÖN: EGL väljer tillverkarbibliotek (libglvnd) och kort (Mesas
// DRI_PRIME) ur miljövariabler som läses vid första EGL-anropet. Satta i
// `gfx::init` före `eglGetPlatformDisplayEXT` verkar de som om användaren
// startat appen med dem — och en variabel användaren redan satt rörs aldrig.
// Listan rör ALDRIG EGL: `eglQueryDevicesEXT` före valet hade laddat
// tillverkarbiblioteken och låst valet innan variabeln satts.

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../gpu_util.h"
#include "gfx/platform.h"
#include "linux_backend.h"

#include <EGL/eglext.h>   // efter egl.h (via linux_backend.h)

extern char** environ;

namespace gfx {

namespace {

namespace fs = std::filesystem;

// Det `applyGpuEnvironment` gjorde — UI-trådens, satt en gång per process.
// `g_satta` är namnen GlazeUI SJÄLV satte (aldrig användarens).
bool                     g_applied = false;
std::vector<std::string> g_satta;
std::string              g_målId;       // kortet valet siktade på; tomt = inget
std::string              g_begärtKort;  // begäran när valet gjordes
GpuPreference            g_begärdPref = GpuPreference::Default;

// Processens miljö UTAN det vi själva satt — som användaren startade appen.
const char* userGetenv(const char* namn)
{
    for (const std::string& s : g_satta)
        if (s == namn)
            return nullptr;
    return std::getenv(namn);
}

// Variablerna som väljer kortet, som de står i processen ("—" när ingen).
std::string primeVariablesNow()
{
    std::string ut;
    for (const char* namn : gpu::primeVariables())
        if (const char* v = std::getenv(namn)) {
            ut += ut.empty() ? "" : " ";
            ut += std::string(namn) + "=" + v;
        }
    return ut.empty() ? std::string("—") : ut;
}

} // namespace

std::vector<GpuAdapter> gpuAdapters()
{
    std::vector<gpu::DrmCard> kort = gpu::scanDrmCards();
    gpu::markSelectable(kort, userGetenv);
    std::vector<GpuAdapter> ut;
    for (gpu::DrmCard& k : kort)
        ut.push_back(std::move(k.a));
    return ut;
}

std::vector<std::string> childEnvironmentRemovals()
{
    return g_satta;
}

void applyGpuEnvironment()
{
    if (g_applied)
        return;
    g_applied    = true;
    g_begärtKort = gpuAdapter();
    g_begärdPref = gpuPreference();
    const std::vector<gpu::DrmCard> kort = gpu::scanDrmCards();
    const gpu::GetEnv   miljö = [](const char* namn) { return std::getenv(namn); };
    const gpu::PrimeEnv env =
        gpu::primeEnvironment(kort, g_begärtKort, g_begärdPref, miljö);
    // En TOM variabel är osatt (som i regeln) och skrivs över; en icke-tom
    // är användarens och rörs aldrig (`applyPrimeEnvironment`, mäts av gpu_gate).
    std::string satt;
    for (const auto& [namn, värde] : gpu::applyPrimeEnvironment(
             env, miljö, [](const char* namn, const char* värde) {
                 return ::setenv(namn, värde, 1) == 0;
             })) {
        g_satta.push_back(namn);
        satt += (satt.empty() ? "" : " ") + namn + "=" + värde;
    }
    if (env.chosen >= 0)
        g_målId = kort[static_cast<std::size_t>(env.chosen)].a.id;
    log("gfx: grafikkortet — mål %s, satt %s%s%s",
        g_målId.empty() ? "inget" : g_målId.c_str(),
        satt.empty() ? "ingenting" : satt.c_str(),
        env.reason.empty() ? "" : " — ", env.reason.c_str());
}

std::string eglAdapterId(EGLDisplay dpy)
{
    const char* klient = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!klient || (!std::strstr(klient, "EGL_EXT_device_query") &&
                    !std::strstr(klient, "EGL_EXT_device_base")))
        return {};
    const auto queryDisplay = reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(
        eglGetProcAddress("eglQueryDisplayAttribEXT"));
    const auto queryString = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
        eglGetProcAddress("eglQueryDeviceStringEXT"));
    EGLAttrib enhet = 0;
    if (!queryDisplay || !queryString ||
        !queryDisplay(dpy, EGL_DEVICE_EXT, &enhet) || !enhet)
        return {};
    const auto  dev = reinterpret_cast<EGLDeviceEXT>(enhet);
    const char* ext = queryString(dev, EGL_EXTENSIONS);
    if (!ext || !std::strstr(ext, "EGL_EXT_device_drm"))
        return {};   // llvmpipe: ingen DRM-fil
    const char* fil = queryString(dev, EGL_DRM_DEVICE_FILE_EXT);
    if (!fil)
        return {};
    // `/dev/dri/card1` (eller en renderD-nod) — kortet vars `device/drm` har
    // noden.
    const std::string nod = fs::path(fil).filename().string();
    const gpu::DrmPaths sti;
    for (const gpu::DrmCard& k : gpu::scanDrmCards(sti)) {
        std::error_code ec;
        if (fs::exists(fs::path(sti.sysClassDrm) / k.card / "device" / "drm" /
                           nod,
                       ec))
            return k.a.id;
    }
    return {};
}

void reportGpuOutcome(const std::string& adapterId, const std::string& renderer)
{
    static bool kontrollerad = false;
    if (!kontrollerad) {
        kontrollerad = true;
        if (!g_målId.empty() && adapterId != g_målId)
            log("gfx: grafikkortet %s var målet, men EGL ritar med %s (%s) — "
                "Mesas förval är kompositorns kort, inte alltid boot_vga",
                g_målId.c_str(), adapterId.empty() ? "?" : adapterId.c_str(),
                renderer.c_str());
        if (renderer.find("llvmpipe") != std::string::npos ||
            renderer.find("softpipe") != std::string::npos)
            log("gfx: mjukvarurendering (%s) — variablerna: %s",
                renderer.c_str(), primeVariablesNow().c_str());
    }
    static bool ändringLoggad = false;
    if (g_applied && !ändringLoggad &&
        (gpuAdapter() != g_begärtKort || gpuPreference() != g_begärdPref)) {
        ändringLoggad = true;
        log("gfx: grafikkortet väljs bara i gfx::init på Linux — den ändrade "
            "begäran (kortet %s, preferensen %s) gäller vid nästa start",
            gpuAdapter().empty() ? "inget" : gpuAdapter().c_str(),
            gpu::preferenceName(gpuPreference()));
    }
}

std::vector<std::string> linuxChildEnvironment()
{
    std::vector<std::string> ut;
    for (char** e = environ; e && *e; ++e) {
        const char* eq = std::strchr(*e, '=');
        if (!eq)
            continue;
        const std::string namn(*e, static_cast<std::size_t>(eq - *e));
        bool vår = false;
        for (const std::string& s : g_satta)
            vår = vår || s == namn;
        if (!vår)
            ut.emplace_back(*e);
    }
    return ut;
}

} // namespace gfx
