#pragma once
// INTERN — EN PLACERINGS TRANSFORM: vinklarna och
// skalan i `glaze::Placement` och `glaze::Instance` (style3d.h) som en affin
// 3 × 4 i double, EN definition för ritningen (meshens modell, instansernas
// rader), cellernas lådor och plockningen (en stråle in i meshens ram).
//
// KONVENTIONEN (style3d.h): R = Rz(−yaw) · Rx(pitch) · Ry(roll), skalan före
// rotationen — yaw medurs sett ovanifrån som kompassen, pitch lyfter +y,
// roll sänker +x, i grader i den högerhänta världen med z upp.

#include "glaze/canvas/geometry.h"
#include "glaze/canvas/style3d.h"

namespace glaze::detail {

struct Affine3 {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // R · S, rad för rad
    double t[3]    = {0, 0, 0};                           // förflyttningen

    Point3 apply(const Point3& p) const
    {
        return Point3{m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + t[0],
                      m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + t[1],
                      m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + t[2]};
    }
    // En riktning (ingen förflyttning).
    Vec3 applyDir(const Vec3& v) const
    {
        return Vec3{m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                    m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                    m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    // Som tre rader (x, y, z, förflyttning) i float — `MeshUniforms::model`
    // och `MeshInstance::rows` (programs3d.h).
    void toRows(float (&rows)[12]) const;
};

// R · S ur vinklarna (grader) och skalan; förflyttningen (tx, ty, tz).
Affine3 rotationScale(float yawDeg, float pitchDeg, float rollDeg,
                      const Float3& scale, double tx = 0.0, double ty = 0.0,
                      double tz = 0.0);

// a ∘ b — först b, sedan a.
Affine3 compose(const Affine3& a, const Affine3& b);

// Inversen; false (och `out` orörd) för en singulär matris.
bool invert(const Affine3& a, Affine3& out);

// Lådan kring de åtta transformerade hörnen av `local`.
Box3 transformBox(const Affine3& a, const Box3& local);

// Avståndet från punkten till lådans närmaste punkt (0 inne i lådan) —
// detaljnivåns mått.
double distanceToBox(const Point3& p, const Box3& b);

} // namespace glaze::detail
