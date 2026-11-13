#include "models.h"

#include "debug_draw.h"
#include "enemy.h"

#include <algorithm>
#include <cstdio>

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

// Unión suave polinómica: funde dos superficies en una franja de ancho k.
float smin(float a, float b, float k) {
    if (k <= 0) return std::min(a, b);
    float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// Primitivas ya colocadas en la pose de reposo (espacio del modelo).
struct PlacedPrim {
    int joint;
    bool capsule;
    Vec3 a, b, r;
    uint32_t color;
    float blend;
    Vec3 lo, hi;  // caja envolvente (para saltarse las lejanas)
};

struct Field {
    std::vector<PlacedPrim> prims;

    float primDist(const PlacedPrim& p, Vec3 q) const {
        return p.capsule ? sdCapsule(q, p.a, p.b, p.r.x) : sdEllipsoid(q - p.a, p.r);
    }
    float eval(Vec3 q) const {
        float d = 1e9f;
        for (const PlacedPrim& p : prims) {
            // Lejos de su caja, la primitiva no cambia el resultado: se ahorra el cálculo.
            float ox = std::max({p.lo.x - q.x, q.x - p.hi.x, 0.0f}), oy = std::max({p.lo.y - q.y, q.y - p.hi.y, 0.0f}),
                  oz = std::max({p.lo.z - q.z, q.z - p.hi.z, 0.0f});
            float boxDist = std::sqrt(ox * ox + oy * oy + oz * oz);
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
    // Pose de reposo: rotaciones a cero; las matrices son traslaciones puras.
    Mat4 bind[kMaxJoints];
    computeWorldMatrices(def.rig, def.jointCount, Pose{}, Mat4::identity(), bind);
    Field f;
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (int i = 0; i < def.primCount; ++i) {
        const BodyPrim& bp = def.prims[i];
        PlacedPrim p{bp.joint, bp.capsule, transformPoint(bind[bp.joint], bp.a), transformPoint(bind[bp.joint], bp.b),
                     bp.r, bp.color, bp.blend, {}, {}};
        Vec3 ext = bp.capsule ? Vec3{bp.r.x, bp.r.x, bp.r.x} : bp.r;
        Vec3 pmin = p.a, pmax = p.a;
        if (bp.capsule) {
            pmin = {std::min(p.a.x, p.b.x), std::min(p.a.y, p.b.y), std::min(p.a.z, p.b.z)};
            pmax = {std::max(p.a.x, p.b.x), std::max(p.a.y, p.b.y), std::max(p.a.z, p.b.z)};
        }
        p.lo = pmin - ext;
        p.hi = pmax + ext;
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
        for (const PlacedPrim& pr : f.prims) {
            float d = f.primDist(pr, p);
            if (std::fabs(d) < best) {
                best = std::fabs(d);
                v.color = pr.color;
            }
            // Influencia de la primitiva sobre su hueso: máxima en su superficie y nula a
            // 6 cm. En los codos y rodillas se solapan dos huesos y la piel se dobla suave.
            float k = clampf(1.0f - std::max(d, 0.0f) / 0.06f, 0, 1);
            w[pr.joint] += k * k;
        }
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

// --- Prismero: el protagonista ---------------------------------------------------------
// Traje claro con acentos de neón, cabeza algo grande (proporciones de dibujo animado),
// pelo magenta, guantes y botas altas.
namespace {
const uint32_t kSuit = rgba(240, 244, 252), kUnder = rgba(38, 46, 86), kSkinC = rgba(250, 214, 188);
const uint32_t kHairC = rgba(232, 78, 176), kAccent = rgba(30, 200, 245), kBoot = rgba(236, 240, 250);
#define E(j, cx, cy, cz, rx, ry, rz, col, k) {j, false, {cx, cy, cz}, {}, {rx, ry, rz}, col, k}
#define C(j, ax, ay, az, bx, by, bz, rad, col, k) {j, true, {ax, ay, az}, {bx, by, bz}, {rad, rad, rad}, col, k}
// clang-format off
const BodyPrim kHeroPrims[] = {
    // tronco: malla interior oscura, chaqueta corta clara con cuello alto
    E(Pelvis,    0, -0.01f, 0,        0.15f, 0.10f, 0.11f,    kUnder, 0.0f),
    E(Spine,     0, 0.08f, 0,         0.135f, 0.13f, 0.10f,   kUnder, 0.04f),
    E(Chest,     0, 0.10f, 0.01f,     0.175f, 0.15f, 0.12f,   kSuit, 0.04f),
    E(Chest,     0, 0.22f, -0.01f,    0.12f, 0.075f, 0.10f,   kSuit, 0.03f),   // cuello alto
    E(Pelvis,    0, 0.06f, 0,         0.158f, 0.03f, 0.118f,  kAccent, 0.01f), // cinturón
    C(Neck,      0, 0, 0,  0, 0.1f, 0,                0.045f, kSkinC, 0.02f),
    // cabeza
    E(Head,      0, 0.15f, 0.01f,     0.145f, 0.165f, 0.15f,  kSkinC, 0.03f),
    E(Head,      0, 0.075f, 0.05f,    0.098f, 0.08f, 0.092f,  kSkinC, 0.04f),
    E(Head,      0, 0.12f, 0.162f,    0.016f, 0.02f, 0.016f,  kSkinC, 0.015f), // nariz
    E(Head,      0.148f, 0.14f, 0,    0.024f, 0.04f, 0.03f,   kSkinC, 0.01f),
    E(Head,     -0.148f, 0.14f, 0,    0.024f, 0.04f, 0.03f,   kSkinC, 0.01f),
    E(Head,      0, 0.215f, -0.02f,   0.162f, 0.135f, 0.168f, kHairC, 0.005f),
    E(Head,      0, 0.13f, -0.085f,   0.148f, 0.135f, 0.105f, kHairC, 0.02f),
    // brazos: hombrera, manga interior, guante con puño claro
    E(ShoulderL, 0.015f, -0.01f, 0,   0.078f, 0.07f, 0.078f,  kSuit, 0.03f),
    C(ShoulderL, 0, -0.03f, 0,  0, -0.26f, 0,         0.05f,  kUnder, 0.03f),
    C(ShoulderL, 0, -0.02f, 0,  0, -0.13f, 0,         0.06f,  kSuit, 0.02f),
    C(ElbowL,    0, 0, 0,  0, -0.22f, 0,              0.043f, kUnder, 0.03f),
    E(ElbowL,    0, -0.215f, 0,       0.052f, 0.03f, 0.052f,  kSuit, 0.01f),   // puño
    E(WristL,    0, -0.06f, 0.006f,   0.048f, 0.072f, 0.058f, kUnder, 0.03f),
    E(ShoulderR, -0.015f, -0.01f, 0,  0.078f, 0.07f, 0.078f,  kSuit, 0.03f),
    C(ShoulderR, 0, -0.03f, 0,  0, -0.26f, 0,         0.05f,  kUnder, 0.03f),
    C(ShoulderR, 0, -0.02f, 0,  0, -0.13f, 0,         0.06f,  kSuit, 0.02f),
    C(ElbowR,    0, 0, 0,  0, -0.22f, 0,              0.043f, kUnder, 0.03f),
    E(ElbowR,    0, -0.215f, 0,       0.052f, 0.03f, 0.052f,  kSuit, 0.01f),
    E(WristR,    0, -0.06f, 0.006f,   0.048f, 0.072f, 0.058f, kUnder, 0.03f),
    // piernas: protección del muslo, rodillera, bota alta
    C(HipL,      0, 0, 0,  0, -0.42f, 0,              0.08f,  kUnder, 0.04f),
    E(HipL,      0, -0.13f, 0.006f,   0.104f, 0.17f, 0.106f,  kSuit, 0.02f),
    C(KneeL,     0, 0, 0,  0, -0.40f, 0,              0.058f, kUnder, 0.03f),
    E(KneeL,     0, 0.0f, 0.03f,      0.064f, 0.07f, 0.058f,  kSuit, 0.02f),
    C(KneeL,     0, -0.22f, 0.005f,  0, -0.40f, 0.01f, 0.072f, kBoot, 0.03f),
    E(AnkleL,    0, -0.025f, 0.065f,  0.075f, 0.068f, 0.155f, kBoot, 0.04f),
    E(AnkleL,    0, -0.075f, 0.065f,  0.08f, 0.022f, 0.16f,   kUnder, 0.01f),  // suela
    C(HipR,      0, 0, 0,  0, -0.42f, 0,              0.08f,  kUnder, 0.04f),
    E(HipR,      0, -0.13f, 0.006f,   0.104f, 0.17f, 0.106f,  kSuit, 0.02f),
    C(KneeR,     0, 0, 0,  0, -0.40f, 0,              0.058f, kUnder, 0.03f),
    E(KneeR,     0, 0.0f, 0.03f,      0.064f, 0.07f, 0.058f,  kSuit, 0.02f),
    C(KneeR,     0, -0.22f, 0.005f,  0, -0.40f, 0.01f, 0.072f, kBoot, 0.03f),
    E(AnkleR,    0, -0.025f, 0.065f,  0.075f, 0.068f, 0.155f, kBoot, 0.04f),
    E(AnkleR,    0, -0.075f, 0.065f,  0.08f, 0.022f, 0.16f,   kUnder, 0.01f),
};
// clang-format on
#undef E
#undef C

ModelDef kModels[int(ModelId::Count)] = {
    {kSkeleton, JointCount, kHeroPrims, int(sizeof(kHeroPrims) / sizeof(kHeroPrims[0])), 0.011f},
};
std::vector<BodyPrim> gEnemyPrims[3];

// Los enemigos reutilizan sus formas (enemy.cpp): las opacas y gruesas forman la malla.
void defineEnemyModels() {
    for (int k = 0; k < 3; ++k) {
        const EnemyDef& d = enemyDef(EnemyKind(k));
        std::vector<BodyPrim>& prims = gEnemyPrims[k];
        prims.clear();
        for (int i = 0; i < d.shapeCount; ++i) {
            const EnemyShape& sh = d.shapes[i];
            if (enemyShapeIsRigid(sh)) continue;
            prims.push_back({sh.joint, false, sh.center, {}, sh.radii, sh.color, 0.03f});
        }
        kModels[1 + k] = {d.rig, d.jointCount, prims.data(), int(prims.size()), k == 1 ? 0.017f : 0.012f};
    }
}

BuiltModel gModels[int(ModelId::Count)];

}  // namespace

bool enemyShapeIsRigid(const EnemyShape& s) { return s.glow || std::min(s.radii.x, std::min(s.radii.y, s.radii.z)) < 0.025f; }
ModelId enemyModel(int enemyKind) { return ModelId(1 + enemyKind); }

const ModelDef& modelDef(ModelId id) { return kModels[int(id)]; }
const BuiltModel& model(ModelId id) { return gModels[int(id)]; }

void buildModels(Renderer& r) {
    defineEnemyModels();
    for (int i = 0; i < int(ModelId::Count); ++i) {
        const ModelDef& def = kModels[i];
        std::vector<SkinVertex> verts;
        std::vector<uint32_t> indices;
        buildSkinnedMesh(def, verts, indices);
        BuiltModel& m = gModels[i];
        m.mesh = r.createSkinnedMesh(verts, indices);
        m.jointCount = def.jointCount;
        m.triangles = indices.size() / 3;
        Mat4 bind[kMaxJoints];
        computeWorldMatrices(def.rig, def.jointCount, Pose{}, Mat4::identity(), bind);
        for (int j = 0; j < def.jointCount; ++j) m.inverseBind[j] = translate(-translationOf(bind[j]));
        std::printf("modelo %d: %zu vertices, %zu triangulos\n", i, verts.size(), m.triangles);
    }
}

void skinMatrices(const BuiltModel& m, const Mat4* joints, Mat4* out) {
    for (int j = 0; j < m.jointCount; ++j) out[j] = joints[j] * m.inverseBind[j];
}
