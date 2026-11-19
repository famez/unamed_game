#include "render/models.h"

#include "game/props.h"

#include <dirent.h>

#include <array>

#include "render/debug_draw.h"
#include "render/glb.h"
#include "render/glb_write.h"
#include "actors/enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// --- Funciones de distancia (Inigo Quilez) ---

float sdEllipsoid(Vec3 p, Vec3 r) {
    Vec3 q{p.x / r.x, p.y / r.y, p.z / r.z};
    Vec3 q2{p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)};
    float k0 = length(q), k1 = length(q2);
    return k1 > 1e-8f ? k0 * (k0 - 1.0f) / k1 : -std::min(r.x, std::min(r.y, r.z));
}

float sdCapsule(Vec3 p, Vec3 a, Vec3 b, float r) {
    Vec3 pa = p - a, ba = b - a;
    float h = clampf(dot(pa, ba) / std::max(dot(ba, ba), 1e-8f), 0, 1);
    return length(pa - ba * h) - r;
}

// Cono redondeado: de radio r1 en `a` a r2 en `b` (mechones de pelo que se afilan).
float sdRoundCone(Vec3 p, Vec3 a, Vec3 b, float r1, float r2) {
    Vec3 ba = b - a;
    float l2 = dot(ba, ba), rr = r1 - r2, a2 = l2 - rr * rr, il2 = 1.0f / std::max(l2, 1e-10f);
    Vec3 pa = p - a;
    float y = dot(pa, ba), z = y - l2;
    Vec3 xv = pa * l2 - ba * y;
    float x2 = dot(xv, xv), y2 = y * y * l2, z2 = z * z * l2;
    float k = (rr > 0 ? 1.0f : rr < 0 ? -1.0f : 0.0f) * rr * rr * x2;
    if ((z > 0 ? 1.0f : -1.0f) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
    if ((y > 0 ? 1.0f : -1.0f) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}

// Unión suave polinómica: funde dos superficies en una franja de ancho k.
float smin(float a, float b, float k) {
    if (k <= 0) return std::min(a, b);
    float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// Primitivas ya colocadas en la pose de reposo (espacio del modelo).
// Primitiva en el espacio de su articulación; `toLocal` lleva un punto del modelo a ese
// espacio (en la postura de reposo).
struct PlacedPrim {
    int joint;
    bool capsule;
    Vec3 a, b, r;
    uint32_t color;
    float blend;
    bool paint;
    uint8_t mat;
    Mat4 toLocal;
    Vec3 lo, hi;  // caja envolvente en el espacio del modelo (para saltarse las lejanas)
};

// Inversa de una matriz rígida (rotación + traslación): Rᵀ y -Rᵀ·t.
Mat4 rigidInverse(const Mat4& m) {
    Mat4 r = Mat4::identity();
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k) r.m[c * 4 + k] = m.m[k * 4 + c];
    Vec3 t = translationOf(m);
    for (int k = 0; k < 3; ++k) r.m[12 + k] = -(r.m[k] * t.x + r.m[4 + k] * t.y + r.m[8 + k] * t.z);
    return r;
}

struct Field {
    std::vector<PlacedPrim> prims;

    float primDist(const PlacedPrim& p, Vec3 q) const {
        Vec3 l = transformPoint(p.toLocal, q);
        if (p.capsule)  // r.y distinto de r.x: cono redondeado
            return p.r.y != p.r.x ? sdRoundCone(l, p.a, p.b, p.r.x, p.r.y) : sdCapsule(l, p.a, p.b, p.r.x);
        return sdEllipsoid(l - p.a, p.r);
    }
    float eval(Vec3 q) const {
        float d = 1e9f;
        for (const PlacedPrim& p : prims) {
            if (p.paint) continue;
            // Lejos de su caja, la primitiva no cambia el resultado: se ahorra el cálculo.
            float ox = std::max({p.lo.x - q.x, q.x - p.hi.x, 0.0f}), oy = std::max({p.lo.y - q.y, q.y - p.hi.y, 0.0f}),
                  oz = std::max({p.lo.z - q.z, q.z - p.hi.z, 0.0f});
            float boxDist = std::sqrt(ox * ox + oy * oy + oz * oz);
            if (p.blend < 0) {  // tallado: resta suave (huecos de los ojos, comisuras...)
                if (boxDist > -p.blend) continue;
                d = -smin(-d, primDist(p, q), -p.blend);
                continue;
            }
            if (boxDist > d + p.blend) continue;
            d = smin(d, primDist(p, q), p.blend);
        }
        return d;
    }
    Vec3 gradient(Vec3 q) const {
        const float e = 0.0015f;
        return Vec3{eval(q + Vec3{e, 0, 0}) - eval(q - Vec3{e, 0, 0}), eval(q + Vec3{0, e, 0}) - eval(q - Vec3{0, e, 0}),
                    eval(q + Vec3{0, 0, e}) - eval(q - Vec3{0, 0, e})} *
               (0.5f / e);
    }
};

}  // namespace

void buildSkinnedMesh(const ModelDef& def, std::vector<SkinVertex>& verts, std::vector<uint32_t>& indices) {
    if (def.primCount <= 0) return;  // todo en piezas aparte (la luciérnaga: solo brillos)
    Mat4 bind[kMaxJoints];
    bindMatrices(def, bind);
    Field f;
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (int i = 0; i < def.primCount; ++i) {
        const BodyPrim& bp = def.prims[i];
        PlacedPrim p{bp.joint, bp.capsule, bp.a, bp.b, bp.r, bp.color, bp.blend, bp.paint, bp.mat, rigidInverse(bind[bp.joint]), {}, {}};
        // Caja envolvente local → las 8 esquinas al espacio del modelo.
        float cr = std::max(bp.r.x, bp.r.y);
        Vec3 ext = bp.capsule ? Vec3{cr, cr, cr} : bp.r;
        Vec3 lmin = bp.a, lmax = bp.a;
        if (bp.capsule) {
            lmin = {std::min(bp.a.x, bp.b.x), std::min(bp.a.y, bp.b.y), std::min(bp.a.z, bp.b.z)};
            lmax = {std::max(bp.a.x, bp.b.x), std::max(bp.a.y, bp.b.y), std::max(bp.a.z, bp.b.z)};
        }
        lmin = lmin - ext;
        lmax = lmax + ext;
        p.lo = {1e9f, 1e9f, 1e9f};
        p.hi = {-1e9f, -1e9f, -1e9f};
        for (int c = 0; c < 8; ++c) {
            Vec3 w = transformPoint(bind[bp.joint], {c & 1 ? lmax.x : lmin.x, c & 2 ? lmax.y : lmin.y, c & 4 ? lmax.z : lmin.z});
            p.lo = {std::min(p.lo.x, w.x), std::min(p.lo.y, w.y), std::min(p.lo.z, w.z)};
            p.hi = {std::max(p.hi.x, w.x), std::max(p.hi.y, w.y), std::max(p.hi.z, w.z)};
        }
        lo = {std::min(lo.x, p.lo.x), std::min(lo.y, p.lo.y), std::min(lo.z, p.lo.z)};
        hi = {std::max(hi.x, p.hi.x), std::max(hi.y, p.hi.y), std::max(hi.z, p.hi.z)};
        f.prims.push_back(p);
    }
    const float h = def.voxel;
    lo = lo - Vec3{2 * h, 2 * h, 2 * h};
    hi = hi + Vec3{2 * h, 2 * h, 2 * h};
    const int nx = int((hi.x - lo.x) / h) + 2, ny = int((hi.y - lo.y) / h) + 2, nz = int((hi.z - lo.z) / h) + 2;
    auto idx = [&](int x, int y, int z) { return (size_t(z) * size_t(ny) + size_t(y)) * size_t(nx) + size_t(x); };
    auto pos = [&](int x, int y, int z) { return lo + Vec3{float(x), float(y), float(z)} * h; };

    std::vector<float> field(size_t(nx) * ny * nz);
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) field[idx(x, y, z)] = f.eval(pos(x, y, z));

    // Surface nets: un vértice por celda que cruza la superficie (media de los cruces en
    // sus aristas, luego proyectado sobre la superficie), y un quad por cada arista de la
    // rejilla que cruza, uniendo las 4 celdas que la rodean.
    std::vector<int> cellVert(size_t(nx) * ny * nz, -1);
    const int corners[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::vector<Vec3> positions;
    for (int z = 0; z + 1 < nz; ++z)
        for (int y = 0; y + 1 < ny; ++y)
            for (int x = 0; x + 1 < nx; ++x) {
                float v[8];
                int inside = 0;
                for (int c = 0; c < 8; ++c) {
                    v[c] = field[idx(x + corners[c][0], y + corners[c][1], z + corners[c][2])];
                    inside += v[c] < 0;
                }
                if (inside == 0 || inside == 8) continue;
                Vec3 sum;
                int n = 0;
                for (const auto& e : edges) {
                    float a = v[e[0]], b = v[e[1]];
                    if ((a < 0) == (b < 0)) continue;
                    float t = a / (a - b);
                    Vec3 pa{float(corners[e[0]][0]), float(corners[e[0]][1]), float(corners[e[0]][2])};
                    Vec3 pb{float(corners[e[1]][0]), float(corners[e[1]][1]), float(corners[e[1]][2])};
                    sum += pa + (pb - pa) * t;
                    ++n;
                }
                Vec3 p = pos(x, y, z) + sum * (h / float(n));
                Vec3 g = f.gradient(p);  // un paso de Newton hacia la superficie
                float gg = dot(g, g);
                if (gg > 1e-8f) p = p - g * (f.eval(p) / gg);
                cellVert[idx(x, y, z)] = int(positions.size());
                positions.push_back(p);
            }

    for (int z = 1; z + 1 < nz; ++z)
        for (int y = 1; y + 1 < ny; ++y)
            for (int x = 1; x + 1 < nx; ++x) {
                float a = field[idx(x, y, z)];
                for (int axis = 0; axis < 3; ++axis) {
                    int dx = axis == 0, dy = axis == 1, dz = axis == 2;
                    float b = field[idx(x + dx, y + dy, z + dz)];
                    if ((a < 0) == (b < 0)) continue;
                    // Las 4 celdas que comparten la arista (x,y,z)→(x+d).
                    int c[4];
                    if (axis == 0) c[0] = cellVert[idx(x, y - 1, z - 1)], c[1] = cellVert[idx(x, y, z - 1)],
                                   c[2] = cellVert[idx(x, y, z)], c[3] = cellVert[idx(x, y - 1, z)];
                    else if (axis == 1) c[0] = cellVert[idx(x - 1, y, z - 1)], c[1] = cellVert[idx(x, y, z - 1)],
                                        c[2] = cellVert[idx(x, y, z)], c[3] = cellVert[idx(x - 1, y, z)];
                    else c[0] = cellVert[idx(x - 1, y - 1, z)], c[1] = cellVert[idx(x, y - 1, z)],
                         c[2] = cellVert[idx(x, y, z)], c[3] = cellVert[idx(x - 1, y, z)];
                    if (c[0] < 0 || c[1] < 0 || c[2] < 0 || c[3] < 0) continue;
                    // Orientación: la normal del quad debe apuntar hacia fuera.
                    Vec3 n = cross(positions[size_t(c[1])] - positions[size_t(c[0])], positions[size_t(c[2])] - positions[size_t(c[0])]);
                    Vec3 out{float(dx), float(dy), float(dz)};
                    if (a > 0) out = -out;  // fuera es hacia donde el campo es positivo
                    if (dot(n, out) < 0) std::swap(c[1], c[3]);
                    indices.insert(indices.end(), {uint32_t(c[0]), uint32_t(c[1]), uint32_t(c[2]), uint32_t(c[0]),
                                                   uint32_t(c[2]), uint32_t(c[3])});
                }
            }

    // Atributos: normal del gradiente, color de la primitiva cuya superficie está más
    // cerca (así las prendas tienen bordes limpios) y pesos de los huesos cercanos.
    verts.resize(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        Vec3 p = positions[i];
        Vec3 n = normalize(f.gradient(p));
        SkinVertex& v = verts[i];
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
        v.material = 0;  // tela con sombreado de animación
        float best = 1e9f;
        float w[kMaxJoints] = {};
        float cw = 0, cr = 0, cg = 0, cb = 0;  // mezcla suave de colores (colorBlend)
        for (const PlacedPrim& pr : f.prims) {
            float d = f.primDist(pr, p);
            if (pr.paint) {  // pintura: tiñe lo que queda dentro (y un poco alrededor)
                if (def.colorBlend > 0) {
                    float k = 3.0f * std::exp(-std::max(d, 0.0f) / 0.004f);
                    cw += k;
                    cr += k * float(pr.color & 0xFF);
                    cg += k * float((pr.color >> 8) & 0xFF);
                    cb += k * float((pr.color >> 16) & 0xFF);
                }
                continue;
            }
            if (std::fabs(d) < best) {
                best = std::fabs(d);
                v.color = pr.color;
                v.material = pr.mat;
            }
            if (def.colorBlend > 0) {
                float k = std::exp(-std::fabs(d) / def.colorBlend);
                cw += k;
                cr += k * float(pr.color & 0xFF);
                cg += k * float((pr.color >> 8) & 0xFF);
                cb += k * float((pr.color >> 16) & 0xFF);
            }
            // Influencia de la primitiva sobre su hueso: máxima en su superficie y nula a
            // 6 cm. En los codos y rodillas se solapan dos huesos y la piel se dobla suave.
            float k = clampf(1.0f - std::max(d, 0.0f) / 0.06f, 0, 1);
            w[pr.joint] += k * k;
        }
        if (def.roundNormals > 0 && v.material == 0) {  // piel: normales redondeadas
            Vec3 rc = transformPoint(bind[def.roundJoint], def.roundCenter);
            Vec3 rn = normalize(n + (normalize(p - rc) - n) * def.roundNormals);
            v.normal[0] = rn.x; v.normal[1] = rn.y; v.normal[2] = rn.z;
        }
        if (def.colorBlend > 0 && cw > 0)
            v.color = rgba(uint8_t(cr / cw + 0.5f), uint8_t(cg / cw + 0.5f), uint8_t(cb / cw + 0.5f));
        int j0 = 0, j1 = 0;
        for (int j = 0; j < def.jointCount; ++j)
            if (w[j] > w[j0]) j0 = j;
        j1 = j0 == 0 ? 1 : 0;
        for (int j = 0; j < def.jointCount; ++j)
            if (j != j0 && w[j] > w[j1]) j1 = j;
        float sum = w[j0] + w[j1];
        float w0 = sum > 0 ? w[j0] / sum : 1.0f;
        if (w0 > 0.97f) w0 = 1.0f;
        v.joints[0] = uint8_t(j0);
        v.joints[1] = uint8_t(j1);
        v.joints[2] = v.joints[3] = 0;
        v.weights[0] = uint8_t(w0 * 255.0f + 0.5f);
        v.weights[1] = uint8_t(255 - v.weights[0]);
        v.weights[2] = v.weights[3] = 0;
    }
}

// --- El protagonista -------------------------------------------------------------------
// Estilo de animación, en armonía con la cabeza: silueta delgada y limpia, formas que se
// afinan (conos redondeados) en vez de tubos. Chaqueta corta de cuello alto, abierta sobre
// una camisa clara, con dos faldones a la espalda; pantalón ajustado, botas y guantes
// grandes, cinturón y una bufanda carmesí. La luz son vetas finas (game_draw.cpp).
namespace {
const uint32_t kCoat = rgba(54, 62, 96), kCoatDark = rgba(36, 40, 64), kUnder = rgba(60, 60, 70);
const uint32_t kSkinC = rgba(238, 204, 178), kHairC = rgba(74, 72, 76), kBoot = rgba(110, 72, 50);
const uint32_t kScarf = rgba(172, 52, 56), kGlove = rgba(56, 40, 34), kShirt = rgba(222, 216, 204);
#define E(j, cx, cy, cz, rx, ry, rz, col, k) {j, false, {cx, cy, cz}, {}, {rx, ry, rz}, col, k}
#define C(j, ax, ay, az, bx, by, bz, rad, col, k) {j, true, {ax, ay, az}, {bx, by, bz}, {rad, rad, rad}, col, k}
#define P(j, cx, cy, cz, rx, ry, rz, col) {j, false, {cx, cy, cz}, {}, {rx, ry, rz}, col, 0, true}
#define K(j, ax, ay, az, bx, by, bz, r1, r2, col, k) {j, true, {ax, ay, az}, {bx, by, bz}, {r1, r2, r1}, col, k}
#define EH(j, cx, cy, cz, rx, ry, rz, col, k) {j, false, {cx, cy, cz}, {}, {rx, ry, rz}, col, k, false, 7}
#define KH(j, ax, ay, az, bx, by, bz, r1, r2, col, k) {j, true, {ax, ay, az}, {bx, by, bz}, {r1, r2, r1}, col, k, false, 7}
// clang-format off
const BodyPrim kHeroPrims[] = {
    // tronco: cintura estrecha, pecho algo más ancho; camisa clara asomando por delante
    E(Pelvis,    0, -0.01f, 0,        0.13f, 0.1f, 0.1f,      kUnder, 0.0f),
    E(Spine,     0, 0.08f, 0,         0.112f, 0.13f, 0.085f,  kShirt, 0.04f),
    E(Chest,     0, 0.1f, 0.0f,       0.15f, 0.14f, 0.095f,   kCoat, 0.04f),
    E(Chest,     0, 0.06f, 0.05f,     0.055f, 0.13f, 0.05f,   kShirt, 0.012f),   // abertura de la chaqueta
    E(Chest,     0, 0.215f, -0.02f,   0.1f, 0.062f, 0.09f,    kCoat, 0.03f),     // cuello alto
    E(Chest,     0, 0.235f, 0.012f,   0.088f, 0.042f, 0.084f, kScarf, 0.015f),   // bufanda
    E(Spine,     0, 0.0f, 0,          0.12f, 0.02f, 0.092f,   kCoatDark, 0.008f), // bajo de la chaqueta
    E(Pelvis,    0, 0.045f, 0,        0.135f, 0.024f, 0.103f, kCoatDark, 0.01f), // cinturón
    // faldones de la chaqueta, a la espalda
    K(Pelvis,    0.06f, 0.04f, -0.08f,  0.085f, -0.42f, -0.15f, 0.06f, 0.025f, kCoat, 0.02f),
    K(Pelvis,   -0.06f, 0.04f, -0.08f, -0.085f, -0.42f, -0.15f, 0.06f, 0.025f, kCoat, 0.02f),
    C(Neck,      0, 0, 0,  0, 0.07f, 0,               0.034f, kSkinC, 0.02f),  // la cabeza va en su propia malla
    // brazos: mangas que se afinan, puño vuelto y guantes grandes
    E(ShoulderL, 0.0f, -0.02f, 0,     0.05f, 0.05f, 0.052f,   kCoat, 0.03f),
    K(ShoulderL, 0, -0.02f, 0,  0, -0.27f, 0,         0.048f, 0.04f, kCoat, 0.03f),
    K(ElbowL,    0, 0, 0,  0, -0.19f, 0,              0.04f, 0.036f, kCoat, 0.03f),
    E(ElbowL,    0, -0.2f, 0,         0.046f, 0.026f, 0.046f, kCoatDark, 0.01f),
    E(WristL,    0, -0.065f, 0.008f,  0.052f, 0.078f, 0.062f, kGlove, 0.03f),
    E(ShoulderR, 0.0f, -0.02f, 0,     0.05f, 0.05f, 0.052f,   kCoat, 0.03f),
    K(ShoulderR, 0, -0.02f, 0,  0, -0.27f, 0,         0.048f, 0.04f, kCoat, 0.03f),
    K(ElbowR,    0, 0, 0,  0, -0.19f, 0,              0.04f, 0.036f, kCoat, 0.03f),
    E(ElbowR,    0, -0.2f, 0,         0.046f, 0.026f, 0.046f, kCoatDark, 0.01f),
    E(WristR,    0, -0.065f, 0.008f,  0.052f, 0.078f, 0.062f, kGlove, 0.03f),
    // piernas: pantalón ajustado y botas grandes y redondas
    K(HipL,      0, 0, 0,  0, -0.42f, 0,              0.075f, 0.052f, kUnder, 0.04f),
    K(KneeL,     0, 0, 0,  0, -0.22f, 0,              0.052f, 0.045f, kUnder, 0.03f),
    K(KneeL,     0, -0.2f, 0.005f,  0, -0.4f, 0.01f,  0.062f, 0.066f, kBoot, 0.025f),
    E(AnkleL,    0, -0.02f, 0.07f,    0.08f, 0.07f, 0.17f,    kBoot, 0.04f),
    E(AnkleL,    0, -0.01f, 0.18f,    0.068f, 0.058f, 0.07f,  kBoot, 0.03f),
    E(AnkleL,    0, -0.074f, 0.075f,  0.084f, 0.022f, 0.18f,  kCoatDark, 0.01f), // suela
    K(HipR,      0, 0, 0,  0, -0.42f, 0,              0.075f, 0.052f, kUnder, 0.04f),
    K(KneeR,     0, 0, 0,  0, -0.22f, 0,              0.052f, 0.045f, kUnder, 0.03f),
    K(KneeR,     0, -0.2f, 0.005f,  0, -0.4f, 0.01f,  0.062f, 0.066f, kBoot, 0.025f),
    E(AnkleR,    0, -0.02f, 0.07f,    0.08f, 0.07f, 0.17f,    kBoot, 0.04f),
    E(AnkleR,    0, -0.01f, 0.18f,    0.068f, 0.058f, 0.07f,  kBoot, 0.03f),
    E(AnkleR,    0, -0.074f, 0.075f,  0.084f, 0.022f, 0.18f,  kCoatDark, 0.01f),
};
// Cabeza del protagonista, en una malla aparte (5 mm), estilo de animación: cara en forma
// de huevo con el mentón en punta, nariz apenas insinuada y un pelo de mechones afilados
// (conos redondeados) que se funden con el cráneo. Pelo oscuro con un mechón claro en el
// flequillo: su luz. Ojos, cejas, boca y la veta de luz van aparte (game_draw.cpp).
const uint32_t kSkinWarm = rgba(238, 204, 178), kHairDark = rgba(50, 44, 52), kHairLight = rgba(232, 222, 196);
const BodyPrim kHeroHeadPrims[] = {
    C(Neck,      0, 0.0f, 0,  0, 0.1f, 0.004f,         0.034f, kSkinWarm, 0.0f),
    E(Head,      0, 0.15f, -0.012f,   0.1f, 0.11f, 0.106f,   kSkinWarm, 0.03f),   // cráneo
    E(Head,      0, 0.108f, 0.028f,   0.086f, 0.085f, 0.082f, kSkinWarm, 0.04f),  // mejillas
    K(Head,      0, 0.085f, 0.04f,  0, 0.03f, 0.082f,   0.066f, 0.022f, kSkinWarm, 0.035f),  // mentón en punta
    E(Head,      0, 0.106f, 0.112f,   0.0095f, 0.012f, 0.009f, kSkinWarm, 0.016f),  // nariz pequeña
    E(Head,      0.094f, 0.12f, -0.01f,  0.012f, 0.026f, 0.018f, kSkinWarm, 0.012f), // orejas
    E(Head,     -0.094f, 0.12f, -0.01f,  0.012f, 0.026f, 0.018f, kSkinWarm, 0.012f),
    // Pelo: casquete y mechones.
    EH(Head,      0, 0.178f, -0.02f,   0.108f, 0.098f, 0.114f, kHairDark, 0.006f),
    KH(Head,      0, 0.2f, -0.08f,     0, 0.16f, -0.21f,     0.05f, 0.008f, kHairDark, 0.02f),  // atrás
    KH(Head,      0.06f, 0.19f, -0.07f,  0.125f, 0.14f, -0.17f, 0.042f, 0.007f, kHairDark, 0.02f),
    KH(Head,     -0.06f, 0.19f, -0.07f, -0.125f, 0.14f, -0.17f, 0.042f, 0.007f, kHairDark, 0.02f),
    KH(Head,      0.02f, 0.25f, -0.01f,  0.055f, 0.315f, -0.09f, 0.045f, 0.008f, kHairDark, 0.02f), // arriba
    KH(Head,     -0.04f, 0.245f, 0.0f,  -0.1f, 0.295f, -0.06f,  0.042f, 0.008f, kHairDark, 0.02f),
    KH(Head,      0.0f, 0.24f, -0.05f,   0.0f, 0.3f, -0.15f,   0.042f, 0.008f, kHairDark, 0.02f),
    KH(Head,      0.088f, 0.165f, 0.01f,  0.135f, 0.095f, 0.0f, 0.032f, 0.006f, kHairDark, 0.015f), // patillas
    KH(Head,     -0.088f, 0.165f, 0.01f, -0.135f, 0.095f, 0.0f, 0.032f, 0.006f, kHairDark, 0.015f),
    KH(Head,      0, 0.12f, -0.09f,     0, 0.05f, -0.125f,   0.04f, 0.008f, kHairDark, 0.02f),  // nuca
    // Flequillo: barrido hacia su izquierda, con el mechón claro en medio.
    KH(Head,      0.035f, 0.225f, 0.075f,  0.085f, 0.17f, 0.118f, 0.028f, 0.005f, kHairDark, 0.014f),
    KH(Head,     -0.005f, 0.228f, 0.08f,   0.03f, 0.172f, 0.124f, 0.026f, 0.005f, kHairDark, 0.014f),
    KH(Head,     -0.045f, 0.222f, 0.075f, -0.05f, 0.165f, 0.12f,  0.024f, 0.005f, kHairDark, 0.014f),
    KH(Head,     -0.072f, 0.21f, 0.06f,   -0.098f, 0.158f, 0.098f, 0.016f, 0.004f, kHairLight, 0.008f),  // el mechón claro
    KH(Head,     -0.085f, 0.2f, 0.035f,   -0.118f, 0.13f, 0.06f,  0.024f, 0.005f, kHairDark, 0.014f),
};

// Ilse, la Archivera: túnica larga hasta los pies, capucha caída sobre los hombros y pelo
// largo y claro. Mismo esqueleto que el protagonista.
const uint32_t kRobe = rgba(40, 36, 50), kRobeDark = rgba(24, 22, 30), kIlseSkin = rgba(196, 184, 176);
const uint32_t kIlseHair = rgba(200, 196, 188);
const BodyPrim kIlsePrims[] = {
    E(Pelvis,    0, -0.01f, 0,        0.16f, 0.11f, 0.12f,    kRobe, 0.0f),
    E(Spine,     0, 0.08f, 0,         0.14f, 0.13f, 0.105f,   kRobe, 0.04f),
    E(Chest,     0, 0.10f, 0.0f,      0.165f, 0.15f, 0.115f,  kRobe, 0.04f),
    E(Chest,     0, 0.2f, -0.06f,     0.2f, 0.09f, 0.13f,     kRobeDark, 0.03f),  // capucha caída
    E(Pelvis,    0, -0.42f, -0.01f,   0.2f, 0.5f, 0.17f,      kRobe, 0.04f),      // túnica hasta los pies
    E(Pelvis,    0, -0.86f, 0.0f,     0.23f, 0.08f, 0.2f,     kRobeDark, 0.03f),
    C(Neck,      0, 0, 0,  0, 0.1f, 0,                0.04f,  kIlseSkin, 0.02f),
    E(Head,      0, 0.125f, 0.01f,    0.105f, 0.13f, 0.115f,  kIlseSkin, 0.03f),
    E(Head,      0, 0.06f, 0.035f,    0.072f, 0.06f, 0.075f,  kIlseSkin, 0.035f),
    E(Head,      0, 0.17f, -0.02f,    0.118f, 0.1f, 0.125f,   kIlseHair, 0.006f),
    E(Head,      0, 0.02f, -0.09f,    0.11f, 0.2f, 0.07f,     kIlseHair, 0.02f),  // melena larga
    C(ShoulderL, 0, -0.02f, 0,  0, -0.27f, 0,         0.055f, kRobe, 0.03f),
    C(ElbowL,    0, 0, 0,  0, -0.2f, 0,               0.06f,  kRobe, 0.03f),      // manga ancha
    E(WristL,    0, -0.05f, 0.006f,   0.038f, 0.06f, 0.045f,  kIlseSkin, 0.03f),
    C(ShoulderR, 0, -0.02f, 0,  0, -0.27f, 0,         0.055f, kRobe, 0.03f),
    C(ElbowR,    0, 0, 0,  0, -0.2f, 0,               0.06f,  kRobe, 0.03f),
    E(WristR,    0, -0.05f, 0.006f,   0.038f, 0.06f, 0.045f,  kIlseSkin, 0.03f),
    C(KneeL,     0, -0.3f, 0,  0, -0.4f, 0.01f,       0.05f,  kRobeDark, 0.02f),
    E(AnkleL,    0, -0.025f, 0.06f,   0.06f, 0.05f, 0.13f,    kRobeDark, 0.03f),
    C(KneeR,     0, -0.3f, 0,  0, -0.4f, 0.01f,       0.05f,  kRobeDark, 0.02f),
    E(AnkleR,    0, -0.025f, 0.06f,   0.06f, 0.05f, 0.13f,    kRobeDark, 0.03f),
};
// clang-format on
#undef E
#undef C
#undef P
#undef K
#undef EH
#undef KH

// Postura en A: brazos abiertos ~35° y piernas algo separadas, para que la unión suave no
// funda la mano con la cadera ni un muslo con el otro.
Pose heroBindPose() {
    Pose p;
    p.rotation[ShoulderL].z = 0.6f;
    p.rotation[ShoulderR].z = -0.6f;
    p.rotation[HipL].z = 0.12f;
    p.rotation[HipR].z = -0.12f;
    return p;
}
const Pose kHeroBind = heroBindPose();

ModelDef kModels[int(ModelId::Count)] = {
    {kSkeleton, JointCount, kHeroPrims, int(sizeof(kHeroPrims) / sizeof(kHeroPrims[0])), 0.011f, &kHeroBind},
};
std::vector<BodyPrim> gEnemyPrims[int(EnemyKind::Count)];
Pose gEnemyBind[int(EnemyKind::Count)];

// Los enemigos reutilizan sus formas (enemy.cpp): las opacas y gruesas forman la malla.
void defineEnemyModels() {
    for (int k = 0; k < int(EnemyKind::Count); ++k) {
        const EnemyDef& d = enemyDef(EnemyKind(k));
        std::vector<BodyPrim>& prims = gEnemyPrims[k];
        prims.clear();
        for (int i = 0; i < d.shapeCount; ++i) {
            const EnemyShape& sh = d.shapes[i];
            if (enemyShapeIsRigid(sh)) continue;
            prims.push_back({sh.joint, false, sh.center, {}, sh.radii, sh.color, 0.03f});
        }
        gEnemyBind[k] = enemyBindPose(EnemyKind(k));
        float voxel = k == int(EnemyKind::Brute) ? 0.017f : isSwarm(EnemyKind(k)) ? 0.008f : 0.011f;
        kModels[1 + k] = {d.rig, d.jointCount, prims.data(), int(prims.size()), voxel, &gEnemyBind[k]};
    }
    kModels[int(ModelId::HeroHead)] = {kSkeleton, JointCount, kHeroHeadPrims, int(sizeof(kHeroHeadPrims) / sizeof(kHeroHeadPrims[0])),
                                       0.005f, &kHeroBind, 0.0f, 0.75f, Head, {0, 0.11f, -0.01f}};
    kModels[int(ModelId::Ilse)] = {kSkeleton, JointCount, kIlsePrims, int(sizeof(kIlsePrims) / sizeof(kIlsePrims[0])), 0.011f, &kHeroBind};
}

BuiltModel gModels[int(ModelId::Count)];
BuiltModel gHeroLooks[2][2];  // [luz, sombra][cuerpo, cabeza]

// Paleta de cada aspecto: color de siempre → luz, sombra.
struct LookSwap {
    uint32_t base, light, shadow;
};
const LookSwap kLookSwaps[] = {
    {kCoat, rgba(236, 230, 214), rgba(30, 24, 40)},
    {kCoatDark, rgba(206, 164, 84), rgba(88, 44, 140)},
    {kUnder, rgba(196, 190, 178), rgba(22, 20, 28)},
    {kSkinC, kSkinC, rgba(220, 206, 214)},
    {kHairC, rgba(246, 224, 160), rgba(222, 220, 236)},
    {kHairDark, rgba(240, 212, 140), rgba(214, 212, 230)},
    {kHairLight, rgba(255, 252, 236), rgba(170, 110, 255)},
    {kBoot, rgba(176, 132, 80), rgba(34, 28, 44)},
    {kScarf, rgba(238, 178, 64), rgba(128, 58, 232)},
    {kGlove, rgba(226, 206, 164), rgba(18, 16, 24)},
    {kShirt, rgba(255, 250, 240), rgba(64, 46, 92)},
    {rgba(104, 58, 50), rgba(214, 150, 50), rgba(100, 44, 190)},  // la cola de la bufanda (pieza aparte)
};

int channel(uint32_t c, int i) { return int((c >> (8 * i)) & 0xFF); }

// Busca el color de la paleta más parecido y aplica su cambio conservando la diferencia
// (los vértices de los bordes entre prendas tienen colores mezclados).
uint32_t swapColor(HeroLook look, uint32_t c) {
    if (look == HeroLook::Base) return c;
    const LookSwap* best = nullptr;
    int bestD = 1 << 30;
    for (const LookSwap& s : kLookSwaps) {
        int d = 0;
        for (int i = 0; i < 3; ++i) d += (channel(c, i) - channel(s.base, i)) * (channel(c, i) - channel(s.base, i));
        if (d < bestD) bestD = d, best = &s;
    }
    uint32_t to = look == HeroLook::Light ? best->light : best->shadow;
    int out[3];
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(channel(to, i) + channel(c, i) - channel(best->base, i), 0, 255);
    return rgba(uint8_t(out[0]), uint8_t(out[1]), uint8_t(out[2]), uint8_t(channel(c, 3)));
}

}  // namespace

bool enemyShapeIsRigid(const EnemyShape& s) { return s.glow || std::min(s.radii.x, std::min(s.radii.y, s.radii.z)) < 0.025f; }
ModelId enemyModel(int enemyKind) { return ModelId(1 + enemyKind); }

const ModelDef& modelDef(ModelId id) { return kModels[int(id)]; }

void bindMatrices(const ModelDef& def, Mat4* out) {
    computeWorldMatrices(def.rig, def.jointCount, def.bindPose ? *def.bindPose : Pose{}, Mat4::identity(), out);
}
const BuiltModel& model(ModelId id) { return gModels[int(id)]; }

const BuiltModel& heroModel(HeroLook look, bool head) {
    if (look == HeroLook::Base) return gModels[int(head ? ModelId::HeroHead : ModelId::Hero)];
    return gHeroLooks[look == HeroLook::Light ? 0 : 1][head ? 1 : 0];
}

uint32_t heroLookColor(HeroLook look, uint32_t base) { return swapColor(look, base); }

// Punto de la cara (espacio de la cabeza) delante de (x, y): se busca la superficie de la
// malla de la cabeza avanzando desde delante. Para pegar ojos, cejas y boca a la piel.
Vec3 headSurface(float x, float y) {
    static Field field;
    static Mat4 headBind;
    if (field.prims.empty()) {
        Mat4 bind[kMaxJoints];
        bindMatrices(kModels[int(ModelId::HeroHead)], bind);
        headBind = bind[Head];
        const ModelDef& d = kModels[int(ModelId::HeroHead)];
        for (int i = 0; i < d.primCount; ++i) {
            const BodyPrim& bp = d.prims[i];
            if (bp.paint) continue;
            field.prims.push_back({bp.joint, bp.capsule, bp.a, bp.b, bp.r, bp.color, bp.blend, false, 0, rigidInverse(bind[bp.joint]),
                                   {-1e9f, -1e9f, -1e9f}, {1e9f, 1e9f, 1e9f}});
        }
    }
    float z = 0.25f;
    for (int i = 0; i < 200; ++i) {
        float dist = field.eval(transformPoint(headBind, {x, y, z}));
        if (dist < 0.0005f) break;
        z -= std::max(dist, 0.0005f);
    }
    return {x, y, z};
}

void buildModels(Renderer& r) {
    defineEnemyModels();
    for (int i = 0; i < int(ModelId::Count); ++i) {
        const ModelDef& def = kModels[i];
        std::vector<SkinVertex> verts;
        std::vector<uint32_t> indices;
        buildSkinnedMesh(def, verts, indices);
        BuiltModel& m = gModels[i];
        m.mesh = verts.empty() ? -1 : r.createSkinnedMesh(verts, indices);
        m.jointCount = def.jointCount;
        m.triangles = indices.size() / 3;
        Mat4 bind[kMaxJoints];
        bindMatrices(def, bind);
        for (int j = 0; j < def.jointCount; ++j) m.inverseBind[j] = rigidInverse(bind[j]);
        std::printf("modelo %d: %zu vertices, %zu triangulos\n", i, verts.size(), m.triangles);
        if (i == int(ModelId::Hero) || i == int(ModelId::HeroHead)) {  // aspectos de luz y de sombra
            for (int look = 0; look < 2; ++look) {
                std::vector<SkinVertex> v = verts;
                for (SkinVertex& sv : v) sv.color = swapColor(look == 0 ? HeroLook::Light : HeroLook::Shadow, sv.color);
                BuiltModel& lm = gHeroLooks[look][i == int(ModelId::HeroHead) ? 1 : 0];
                lm = m;
                lm.mesh = r.createSkinnedMesh(v, indices);
            }
        }
    }
}

void skinMatrices(const BuiltModel& m, const Mat4* joints, Mat4* out) {
    for (int j = 0; j < m.jointCount; ++j) out[j] = joints[j] * m.inverseBind[j];
}

// --- Personajes en .glb ----------------------------------------------------------------------

namespace {

struct GlbCharacter {
    const char* file;
    ModelId id;
    bool wolf;  // el Sabueso recoloreado como lobo de sombra
};
const GlbCharacter kGlbCharacters[] = {
    {"deslumbrado", ModelId::Dazzled, false}, {"coloso", ModelId::Brute, false}, {"vigia", ModelId::Watcher, false},
    {"sabueso", ModelId::Hound, false},       {"ilse", ModelId::Ilse, false},    {"lobo_de_sombra", ModelId::Hound, true},
    {"polilla", ModelId::Moth, false},        {"luciernaga", ModelId::Firefly, false}, {"cangrejo", ModelId::Crab, false},
};
bool gFromGlb[int(ModelId::Count)] = {};
BuiltModel gWolf;
bool gWolfLoaded = false;

uint8_t linearToSrgb(float lin) {
    lin = std::clamp(lin, 0.0f, 1.0f);
    float s = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(s * 255.0f));
}

// Elipsoide (pieza rígida) como malla pegada a un hueso.
void addEllipsoid(std::vector<SkinVertex>& v, std::vector<uint32_t>& idx, const Mat4& bone, int joint, Vec3 c, Vec3 r,
                  uint32_t color, float material) {
    const int rings = 8, segs = 14;
    uint32_t base = uint32_t(v.size());
    for (int i = 0; i <= rings; ++i) {
        float th = kPi * float(i) / float(rings);
        for (int k = 0; k <= segs; ++k) {
            float ph = 2.0f * kPi * float(k) / float(segs);
            Vec3 d{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
            Vec3 p = transformPoint(bone, c + Vec3{d.x * r.x, d.y * r.y, d.z * r.z});
            Vec3 nl = normalize(Vec3{d.x / r.x, d.y / r.y, d.z / r.z});
            Vec3 n = normalize(transformPoint(bone, nl) - transformPoint(bone, {}));
            SkinVertex sv{};
            sv.pos[0] = p.x, sv.pos[1] = p.y, sv.pos[2] = p.z;
            sv.normal[0] = n.x, sv.normal[1] = n.y, sv.normal[2] = n.z;
            sv.color = color;
            sv.joints[0] = uint8_t(joint);
            sv.weights[0] = 255;
            sv.material = material;
            v.push_back(sv);
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int k = 0; k < segs; ++k) {
            uint32_t a = base + uint32_t(i * (segs + 1) + k), b = a + uint32_t(segs + 1);
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}

std::string materialName(const SkinVertex& v) {
    int m = int(v.material + 0.5f);
    if (m == int(Material::Glow)) {
        char b[16];
        std::snprintf(b, sizeof(b), "Glow_%02X%02X%02X", v.color & 0xFF, (v.color >> 8) & 0xFF, (v.color >> 16) & 0xFF);
        return b;
    }
    return m == int(Material::Metal) ? "Metal" : m == 7 ? "Hair" : m == int(Material::Stone) ? "Stone" : "Cloth";
}

}  // namespace

int exportCharacterGlbs(const std::string& dir, bool overwrite) {
    defineEnemyModels();
    int written = 0;
    for (const GlbCharacter& ch : kGlbCharacters) {
        if (!overwrite)
            if (FILE* f = std::fopen((dir + "/" + ch.file + ".glb").c_str(), "rb")) {  // ya hay uno (quizá rediseñado): se respeta
                std::fclose(f);
                std::printf("%s/%s.glb ya existe (con --forzar se sustituye por el generado)\n", dir.c_str(), ch.file);
                continue;
            }
        const ModelDef& def = kModels[int(ch.id)];
        std::vector<SkinVertex> verts;
        std::vector<uint32_t> idx;
        buildSkinnedMesh(def, verts, idx);
        Mat4 bind[kMaxJoints];
        bindMatrices(def, bind);
        // Las piezas rígidas de los enemigos (ojos, puntas que brillan) entran en la malla.
        int kind = int(ch.id) - 1;
        if (kind >= 0 && kind < int(EnemyKind::Count)) {
            const EnemyDef& d = enemyDef(EnemyKind(kind));
            for (int s = 0; s < d.shapeCount; ++s) {
                const EnemyShape& sh = d.shapes[s];
                if (!enemyShapeIsRigid(sh)) continue;
                uint32_t col = sh.color;
                if (ch.wolf) col = sh.glow ? rgba(190, 120, 255) : rgba(16, 12, 22);
                addEllipsoid(verts, idx, bind[sh.joint], sh.joint, sh.center, sh.radii, col,
                             float(int(sh.glow ? Material::Glow : Material::Cloth)));
            }
        }
        if (ch.wolf)  // el lobo: negro con reflejos violeta (se conserva el claroscuro pintado)
            for (SkinVertex& v : verts)
                if (int(v.material + 0.5f) != int(Material::Glow)) {
                    float l = (0.3f * float(v.color & 0xFF) + 0.59f * float((v.color >> 8) & 0xFF) +
                               0.11f * float((v.color >> 16) & 0xFF)) / 255.0f;
                    v.color = mixRgb(rgba(14, 10, 22), rgba(70, 40, 110), std::clamp(l * 1.3f, 0.0f, 1.0f));
                }
        // Una primitiva por material (los vértices se reparten sin duplicar dentro de cada una).
        std::vector<GlbOutMaterial> mats;
        std::vector<GlbOutPrimitive> prims;
        std::vector<std::vector<int>> remap;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            std::string mn = materialName(verts[idx[t]]);
            size_t mi = 0;
            while (mi < mats.size() && mats[mi].name != mn) ++mi;
            if (mi == mats.size()) {
                GlbOutMaterial m;
                m.name = mn;
                if (mn.rfind("Glow", 0) == 0) {
                    uint32_t c = verts[idx[t]].color;
                    for (int k = 0; k < 3; ++k) {
                        float s = float((c >> (8 * k)) & 0xFF) / 255.0f;
                        m.emissive[k] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
                    }
                }
                mats.push_back(m);
                GlbOutPrimitive p;
                p.material = int(mi);
                prims.push_back(p);
                remap.emplace_back(verts.size(), -1);
            }
            for (int k = 0; k < 3; ++k) {
                uint32_t vi = idx[t + size_t(k)];
                int& to = remap[mi][vi];
                if (to < 0) to = int(prims[mi].verts.size()), prims[mi].verts.push_back(verts[vi]);
                prims[mi].indices.push_back(uint32_t(to));
            }
        }
        std::vector<GlbOutJoint> joints;
        for (int j = 0; j < def.jointCount; ++j) {
            int parent = def.rig[j].parent;
            joints.push_back({def.rig[j].name, parent, parent < 0 ? bind[j] : rigidInverse(bind[parent]) * bind[j]});
        }
        std::string path = dir + "/" + ch.file + ".glb";
        if (writeSkinnedGlb(path, ch.file, joints, prims, mats)) {
            std::printf("%s: %zu triangulos, %d huesos, %zu materiales\n", path.c_str(), idx.size() / 3, def.jointCount, mats.size());
            ++written;
        } else {
            std::fprintf(stderr, "No se pudo escribir %s\n", path.c_str());
        }
    }
    return written;
}

bool loadCharacterGlb(Renderer& r, const std::string& path, const JointDef* rig, int jointCount, BuiltModel& out,
                      std::string& error) {
    GlbFile g;
    if (!g.load(path, error)) return false;
    int meshNode = -1;
    for (size_t i = 0; i < g.nodes.size() && meshNode < 0; ++i)
        if (g.nodes[i].mesh >= 0 && g.nodes[i].skin >= 0) meshNode = int(i);
    if (meshNode < 0) return error = path + ": no hay ninguna malla con esqueleto", false;
    const GlbNode& mn = g.nodes[size_t(meshNode)];
    const GlbSkin& skin = g.skins[size_t(mn.skin)];
    // Hueso de la piel → hueso del juego (por nombre).
    std::vector<int> toRig(skin.joints.size(), -1);
    Mat4 inverseBind[kMaxJoints];
    bool have[kMaxJoints] = {};
    for (size_t i = 0; i < skin.joints.size(); ++i) {
        const std::string& name = g.nodes[size_t(skin.joints[i])].name;
        for (int k = 0; k < jointCount; ++k)
            if (name == rig[k].name) {
                toRig[i] = k;
                inverseBind[k] = skin.inverseBind[i];
                have[k] = true;
            }
    }
    for (int k = 0; k < jointCount; ++k)
        if (!have[k]) return error = path + ": falta el hueso " + rig[k].name, false;
    std::vector<SkinVertex> verts;
    std::vector<uint32_t> indices;
    for (const GlbPrimitive& p : g.meshes[size_t(mn.mesh)]) {
        GlbMaterial mat;
        if (p.material >= 0 && size_t(p.material) < g.materials.size()) mat = g.materials[size_t(p.material)];
        bool glow = mat.name.rfind("Glow", 0) == 0 || mat.emissive[0] + mat.emissive[1] + mat.emissive[2] > 0.05f;
        float m = glow ? float(int(Material::Glow))
                  : mat.name == "Metal" ? float(int(Material::Metal))
                  : mat.name.rfind("Hair", 0) == 0 ? 7.0f
                  : mat.name == "Stone" ? float(int(Material::Stone))
                                        : float(int(Material::Cloth));
        uint32_t base = uint32_t(verts.size());
        for (size_t i = 0; i < p.positions.size(); ++i) {
            SkinVertex v{};
            v.pos[0] = p.positions[i].x, v.pos[1] = p.positions[i].y, v.pos[2] = p.positions[i].z;
            Vec3 n = i < p.normals.size() ? p.normals[i] : Vec3{0, 1, 0};
            if (length(n) < 1e-4f) n = {0, 1, 0};  // triángulos degenerados: normal nula (el shader daría NaN)
            v.normal[0] = n.x, v.normal[1] = n.y, v.normal[2] = n.z;
            float c[3];
            for (int k = 0; k < 3; ++k)
                c[k] = (p.colors.size() >= (i + 1) * 4 ? p.colors[i * 4 + size_t(k)] : 1.0f) * mat.base[k];
            v.color = rgba(linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2]));
            v.material = m;
            // Los cuatro pesos, reescalados a 255.
            float w[4] = {1, 0, 0, 0}, sum = 0;
            for (int k = 0; k < 4; ++k) {
                size_t at = i * 4 + size_t(k);
                uint32_t sj = at < p.joints.size() ? p.joints[at] : 0;
                w[k] = at < p.weights.size() ? p.weights[at] : (k == 0 ? 1.0f : 0.0f);
                int rj = sj < toRig.size() ? toRig[sj] : -1;
                if (rj < 0) w[k] = 0, rj = 0;
                v.joints[k] = uint8_t(rj);
                sum += w[k];
            }
            int total = 0, heaviest = 0;
            for (int k = 0; k < 4; ++k) {
                v.weights[k] = uint8_t(std::lround(sum > 0 ? w[k] / sum * 255.0f : (k == 0 ? 255.0f : 0.0f)));
                total += v.weights[k];
                if (v.weights[k] > v.weights[heaviest]) heaviest = k;
            }
            v.weights[heaviest] = uint8_t(std::clamp(int(v.weights[heaviest]) + 255 - total, 0, 255));
            verts.push_back(v);
        }
        for (uint32_t ix : p.indices) indices.push_back(base + ix);
    }
    if (verts.empty()) return error = path + ": la malla está vacía", false;
    out = BuiltModel{};
    out.mesh = r.createSkinnedMesh(verts, indices);
    out.jointCount = jointCount;
    out.triangles = indices.size() / 3;
    for (int k = 0; k < jointCount; ++k) out.inverseBind[k] = inverseBind[k];
    return true;
}

int loadCharacterGlbs(Renderer& r, const std::string& dir) {
    int loaded = 0;
    for (const GlbCharacter& ch : kGlbCharacters) {
        std::string path = dir + "/" + ch.file + ".glb", error;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;  // no hay: se queda el generado
        std::fclose(f);
        const ModelDef& def = kModels[int(ch.id)];
        BuiltModel m;
        if (!loadCharacterGlb(r, path, def.rig, def.jointCount, m, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            continue;
        }
        if (ch.wolf) {
            gWolf = m;
            gWolfLoaded = true;
        } else {
            gModels[int(ch.id)] = m;
            gFromGlb[int(ch.id)] = true;
        }
        ++loaded;
    }
    return loaded;
}

bool glbCharacterId(const std::string& stem, ModelId& id, bool& wolf) {
    for (const GlbCharacter& ch : kGlbCharacters)
        if (stem == ch.file) return id = ch.id, wolf = ch.wolf, true;
    return false;
}

bool modelFromGlb(ModelId id) { return gFromGlb[int(id)]; }
const BuiltModel* wolfModel() { return gWolfLoaded ? &gWolf : nullptr; }

namespace {
BuiltModel gCreatures[int(CreatureKind::Count)];
bool gCreatureLoaded[int(CreatureKind::Count)] = {};
}  // namespace

int loadCreatureModels(Renderer& r, const std::string& dir) {
    int loaded = 0;
    for (int k = 0; k < int(CreatureKind::Count); ++k) {
        const CreatureRig& rig = creatureRig(CreatureKind(k));
        gCreatureLoaded[k] = false;
        if (!rig.loaded()) continue;
        std::string error;
        if (!loadCharacterGlb(r, dir + "/" + creatureFile(CreatureKind(k)) + ".glb", rig.joints, rig.count, gCreatures[k], error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            continue;
        }
        gCreatureLoaded[k] = true;
        ++loaded;
    }
    return loaded;
}

const BuiltModel* creatureModel(CreatureKind k) { return gCreatureLoaded[int(k)] ? &gCreatures[int(k)] : nullptr; }

void uploadPropMeshes(Renderer& r) {
    PropLibrary& lib = propLibrary();
    for (size_t i = 0; i < lib.size(); ++i) {
        PropDef& d = lib[i];
        if (d.loaded && d.mesh < 0 && !d.verts.empty()) {
            d.mesh = r.createSkinnedMesh(d.verts, d.indices);
        } else if (!d.loaded && d.mesh >= 0) {  // la zona ya no la usa
            r.freeSkinnedMesh(d.mesh);
            d.mesh = -1;
        }
    }
}

namespace {
std::vector<std::pair<std::string, BuiltModel>> gInhabitants;
// Rotaciones de la pose de reposo de cada uno, por hueso del esqueleto (orden X, Y, Z sobre la
// pose de reposo del .glb, la pose A).
std::vector<std::pair<std::string, std::array<Vec3, JointCount>>> gIdlePoses;
}

int loadInhabitants(Renderer& r, const std::string& dir) {
    DIR* dp = opendir(dir.c_str());
    if (!dp) return 0;
    std::vector<std::string> files;
    while (dirent* e = readdir(dp)) {
        std::string f = e->d_name;
        if (f.size() > 4 && f.compare(f.size() - 4, 4, ".glb") == 0) files.push_back(f);
    }
    closedir(dp);
    for (const char* poses : {"poses_idle_ciudadanos.json", "poses_idle_tanda3.json"}) {
        GlbPoseSet set;
        if (!loadPoseFile(dir + "/" + poses, set)) continue;
        for (const auto& [who, bones] : set) {
            std::array<Vec3, JointCount> rot{};
            // Las poses del paquete escriben el lado izquierdo con los ejes del derecho, pero en el
            // esqueleto los dos lados son reflejo (en su render salen los brazos izquierdos en
            // cruz): se refleja Y y Z en los huesos «_l».
            for (const auto& [bone, angles] : bones)
                for (int k = 0; k < JointCount; ++k)
                    if (bone == kSkeleton[k].name) {
                        bool left = bone.size() > 2 && bone.compare(bone.size() - 2, 2, "_l") == 0;
                        rot[size_t(k)] = left ? Vec3{angles.x, -angles.y, -angles.z} : angles;
                    }
            gIdlePoses.push_back({who, rot});
        }
    }
    for (const std::string& f : files) {
        BuiltModel m;
        std::string error;
        if (loadCharacterGlb(r, dir + "/" + f, kSkeleton, JointCount, m, error))
            gInhabitants.push_back({f.substr(0, f.size() - 4), m});
        else
            std::fprintf(stderr, "%s\n", error.c_str());
    }
    return int(gInhabitants.size());
}

const BuiltModel* inhabitantModel(const std::string& name) {
    for (const auto& [n, m] : gInhabitants)
        if (n == name) return &m;
    return nullptr;
}

bool inhabitantIdleJoints(const std::string& name, const Mat4& model, float time, Mat4* out) {
    const BuiltModel* m = inhabitantModel(name);
    const std::array<Vec3, JointCount>* rot = nullptr;
    for (const auto& [n, r] : gIdlePoses)
        if (n == name) rot = &r;
    if (!m || !rot) return false;
    // La pose de reposo del .glb (la A) sale de las inversas de bind; encima, la rotación local
    // de la pose (X, luego Y, luego Z, como la escribe three.js) y una respiración muy leve.
    Mat4 bindWorld[JointCount];
    for (int k = 0; k < JointCount; ++k) bindWorld[k] = affineInverse(m->inverseBind[k]);
    float breath = std::sin(time * 1.6f + float(name.size()));
    for (int k = 0; k < JointCount; ++k) {
        int parent = kSkeleton[k].parent;
        Mat4 local = parent >= 0 ? affineInverse(bindWorld[parent]) * bindWorld[k] : bindWorld[k];
        Vec3 r = (*rot)[size_t(k)];
        if (k == Chest) r.x += 0.015f * breath;
        if (k == Head) r.x -= 0.01f * breath;
        Mat4 R = rotateX(r.x) * rotateY(r.y) * rotateZ(r.z);
        out[k] = (parent >= 0 ? out[parent] : model) * local * R;
    }
    if (name == "abuelo") {  // el bastón cuelga de la muñeca hacia -Y: que apunte al suelo (nota del JSON)
        Vec3 d = normalize(Vec3{-0.12f, 1.0f, -0.1f});  // el eje Y de la muñeca, hacia arriba (el bastón va hacia -Y)
        d = normalize(Vec3{model.m[0] * d.x + model.m[4] * d.y + model.m[8] * d.z, model.m[1] * d.x + model.m[5] * d.y + model.m[9] * d.z,
                           model.m[2] * d.x + model.m[6] * d.y + model.m[10] * d.z});
        Vec3 x = normalize(cross(std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, d)), z = cross(x, d);
        Mat4& w = out[WristR];
        w.m[0] = x.x, w.m[1] = x.y, w.m[2] = x.z, w.m[4] = d.x, w.m[5] = d.y, w.m[6] = d.z, w.m[8] = z.x, w.m[9] = z.y, w.m[10] = z.z;
    }
    return true;
}
