#pragma once
// Mat4d — 4×4-matrisen i DOUBLE. Kameran räknar allt i
// double på CPU:n och laddar upp float först i sista steget (`toFloat`):
// det är det som gör att en SWEREF-koordinat (~6,6·10⁶) och en zoom på
// tusentals px per meter går ihop utan att float-precisionen äter kartan.
//
// KONVENTIONEN, en gång för alla tre shaderspråken:
//   * KOLUMNORDNING — m[kolumn * 4 + rad] — och KOLUMNVEKTORER, p' = M · p.
//     Det är GL:s och MapLibres (gl-matrix) konvention, och `toFloat` skriver
//     samma ordning. Den läses rakt av överallt: GLSL:s mat4 med
//     glUniformMatrix4fv utan transponering, HLSL:s float4x4 i en cbuffer
//     (column_major är standardpackningen) med mul(M, v), och MSL:s float4x4
//     (kolumner av float4) med M * v.
//   * `ortho`/`perspective` är HÖGERHÄNTA (kameran tittar längs −z, y uppåt)
//     med djupet i [0, 1] — D3D11:s och Metals klipprymd. GL:s klipprymd har
//     z i [−w, w]; dess vertexshader räknar om z en gång (2z − w), så
//     matrisen är densamma för alla tre.

namespace gfx {

struct Mat4d {
    double m[16] = {};   // kolumnordning: m[kolumn * 4 + rad]

    static Mat4d identity();
    static Mat4d translation(double x, double y, double z);
    static Mat4d scale(double x, double y, double z);
    // Rotation kring x-, y- resp. z-axeln, radianer, högerhänt (positiv
    // vinkel vrider y mot z, z mot x resp. x mot y).
    static Mat4d rotationX(double rad);
    static Mat4d rotationY(double rad);
    static Mat4d rotationZ(double rad);

    // VYN: rymden ögat står i → ÖGONRYMDEN `ortho`/
    // `perspective` vill ha — x höger, y upp, ögat i origo och blicken längs
    // −z. Ögat (ex, ey, ez) tittar mot punkten (tx, ty, tz); (ux, uy, uz)
    // säger var uppåt är och behöver varken vara enhetlig eller vinkelrät mot
    // blicken, bara inte parallell med den. Högerhänt: höger = blicken × upp.
    // NOLLMATRISEN när ögat och punkten sammanfaller eller upp är parallell
    // med blicken — ingen vy att bygga, och W = 0 ritar ingenting (samma svar
    // som en oanvändbar kameravy).
    static Mat4d lookAt(double ex, double ey, double ez, double tx, double ty,
                        double tz, double ux, double uy, double uz);

    // Ortografisk projektion av lådan [l, r] × [b, t] × [−n, −f] (ögonrymd)
    // till NDC med djupet i [0, 1].
    static Mat4d ortho(double l, double r, double b, double t, double n,
                       double f);

    // Perspektiv med vertikalt synfält `fovY` (radianer), bildförhållandet
    // `aspect` = bredd/höjd och klipplanen n och f (> 0, ögonrymdens
    // avstånd). f = +∞ ger det OÄNDLIGA bortre planet (djupet går mot 1 men
    // klipps aldrig) — 2D-kameran använder det, se camera.cpp.
    static Mat4d perspective(double fovY, double aspect, double n, double f);

    Mat4d operator*(const Mat4d& o) const;

    // Inversen. false (och `out` orörd) när matrisen är singulär.
    bool inverse(Mat4d& out) const;

    // Samma ordning som `m`, avrundat till float — uppladdningens form.
    void toFloat(float out[16]) const;

    // out = M · (x, y, z, w).
    void transform(double x, double y, double z, double w,
                   double out[4]) const;
};

} // namespace gfx
