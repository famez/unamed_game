// El mapa de cada nivel (M): la zona vista desde arriba, pintada en la CPU la primera vez que se
// abre. Se rasteriza lo que se ve de arriba —el relieve con sus colores, los bloques, la
// decoración y las mallas de las piezas— quedándose en cada píxel con lo más alto, y después se
// sombrea como un mapa: el relieve iluminado desde arriba a la izquierda, las sombras que echa lo
// alto, el borde oscuro al pie de las paredes, las curvas de nivel del terreno y el agua según
// su hondura. Lo que tapa desde arriba lo que se pisa (los techos de la gruta, sus estalactitas,
// el tejado de las tabernas) no se pinta.

#include "game/game.h"
#include "game/level_map.h"
#include "game/props.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace {

constexpr float kNone = -1e9f;
enum : uint8_t { kVoid = 0, kTerrain = 1, kSolid = 2 };

struct Canvas {
    LevelMap& m;
    std::vector<float> h;       // lo más alto en cada píxel
    std::vector<uint32_t> c;    // su color
    std::vector<uint8_t> kind;  // vacío, relieve o algo puesto encima
    float cutY = 1e9f;          // por encima de esta altura no se pinta (los interiores: el techo)
    // Las copas de los árboles (sin relieve: la plaza, la selva, la aldea de las copas) van en
    // otra capa que se ve a medias, para que no tapen lo que se pisa debajo.
    bool soft = false;
    std::vector<float> hs;
    std::vector<uint32_t> cs;

    explicit Canvas(LevelMap& map) : m(map) {
        size_t n = size_t(m.w) * size_t(m.h);
        h.assign(n, kNone);
        c.assign(n, 0);
        kind.assign(n, kVoid);
        hs.assign(n, kNone);
        cs.assign(n, 0);
    }

    // Triángulo del mundo: en cada píxel cuyo centro cubre, si queda más alto que lo que hay.
    void tri(Vec3 a, Vec3 b, Vec3 d, uint32_t col) {
        if (std::min({a.y, b.y, d.y}) > cutY) return;
        float ax = m.u(a), ay = m.v(a), bx = m.u(b), by = m.v(b), dx = m.u(d), dy = m.v(d);
        float area = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
        if (std::fabs(area) < 1e-6f) return;
        int x0 = std::max(0, int(std::floor(std::min({ax, bx, dx}) - 0.5f)));
        int x1 = std::min(m.w - 1, int(std::ceil(std::max({ax, bx, dx}) - 0.5f)));
        int y0 = std::max(0, int(std::floor(std::min({ay, by, dy}) - 0.5f)));
        int y1 = std::min(m.h - 1, int(std::ceil(std::max({ay, by, dy}) - 0.5f)));
        float inv = 1.0f / area;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float px = float(x) + 0.5f, py = float(y) + 0.5f;
                float w0 = ((bx - px) * (dy - py) - (by - py) * (dx - px)) * inv;
                float w1 = ((dx - px) * (ay - py) - (dy - py) * (ax - px)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float y3 = a.y * w0 + b.y * w1 + d.y * w2;
                size_t k = size_t(y) * size_t(m.w) + size_t(x);
                if (soft) {
                    if (y3 > hs[k]) hs[k] = y3, cs[k] = col;
                    continue;
                }
                if (y3 <= h[k] || y3 > cutY) continue;
                h[k] = y3;
                c[k] = col;
                kind[k] = kSolid;
            }
    }

    // Caja unidad (±0.5) o esfera unidad (radio 0.5) transformada por `t`.
    void box(const Mat4& t, uint32_t col) {
        Vec3 p[8];
        for (int i = 0; i < 8; ++i) p[i] = transformPoint(t, {i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f});
        const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
        for (const auto& f : faces) {
            tri(p[f[0]], p[f[1]], p[f[2]], col);
            tri(p[f[0]], p[f[2]], p[f[3]], col);
        }
    }
    void sphere(const Mat4& t, uint32_t col) {
        const int kLat = 6, kLon = 12;
        auto at = [&](int i, int j) {
            float th = kPi * float(i) / float(kLat), ph = 2.0f * kPi * float(j) / float(kLon);
            return transformPoint(t, Vec3{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)} * 0.5f);
        };
        for (int i = 0; i < kLat; ++i)
            for (int j = 0; j < kLon; ++j) {
                Vec3 a = at(i, j), b = at(i + 1, j), d = at(i + 1, j + 1), e = at(i, j + 1);
                tri(a, b, d, col);
                tri(a, d, e, col);
            }
    }
};

float channel(uint32_t c, int i) { return float((c >> (8 * i)) & 0xFF) / 255.0f; }
uint32_t pack(float r, float g, float b, float a = 1) {
    auto q = [](float v) { return uint8_t(clampf(v, 0, 1) * 255.0f + 0.5f); };
    return rgba(q(r), q(g), q(b), q(a));
}
uint32_t scaleRgb(uint32_t c, float k) { return pack(channel(c, 0) * k, channel(c, 1) * k, channel(c, 2) * k, channel(c, 3)); }

}  // namespace

void Game::buildLevelMap(LevelMap& out, int maxSide) const {
    const Terrain* t = world_.terrain.get();
    const bool covered = level_ == LevelId::Gruta;  // con techos: lo que cuelga no se pinta
    const bool interior = level_ == LevelId::Botequim || level_ == LevelId::TabernaLapa;

    // Lo que abarca: el recinto del relieve si lo tiene (lo que se pisa, con un margen), el
    // relieve entero si no; sin relieve, lo que hay puesto.
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    auto grow = [&](Vec3 p, float r) {
        x0 = std::min(x0, p.x - r), x1 = std::max(x1, p.x + r);
        z0 = std::min(z0, p.z - r), z1 = std::max(z1, p.z + r);
    };
    if (t && !t->fence.empty()) {
        for (int iz = 0; iz < t->nz; ++iz)
            for (int ix = 0; ix < t->nx; ++ix)
                if (t->fence[size_t(iz) * size_t(t->nx) + size_t(ix)] < 3.0f)
                    grow({t->x0 + float(ix) * t->cell, 0, t->z0 + float(iz) * t->cell}, 0);
        if (x0 <= x1) x0 -= 14, x1 += 14, z0 -= 14, z1 += 14;
    } else if (t) {
        grow({t->x0, 0, t->z0}, 0);
        grow({t->x0 + float(t->nx - 1) * t->cell, 0, t->z0 + float(t->nz - 1) * t->cell}, 0);
    } else {
        for (const Block& b : blocks_)
            if (b.color != 0) grow(b.box.min, 0), grow(b.box.max, 0);
        // Las piezas grandes (la sala entera de un interior) por lo que choca de ellas, no por su
        // esfera, que se va muy lejos.
        for (const PropInstance& p : props_)
            if (p.radius < 10) grow(p.center, p.radius);
        for (const AABB& b : propSolids_) grow(b.min, 0), grow(b.max, 0);
        grow(heroStart_, 4);
        for (const ZoneDoor& d : doors_) grow(d.pos, 4);
        if (x0 <= x1) x0 -= 4, x1 += 4, z0 -= 4, z1 += 4;
    }
    if (x0 > x1) grow(heroStart_, 30);
    // Sin pasarse: lo que quede lejísimos (un telón de fondo) se queda fuera.
    const float kMaxExtent = 900;
    if (x1 - x0 > kMaxExtent) x0 = std::max(x0, heroStart_.x - kMaxExtent * 0.5f), x1 = x0 + kMaxExtent;
    if (z1 - z0 > kMaxExtent) z0 = std::max(z0, heroStart_.z - kMaxExtent * 0.5f), z1 = z0 + kMaxExtent;

    out = LevelMap{};
    out.serial = levelSerial_;
    out.cell = std::max(std::max(x1 - x0, z1 - z0) / float(std::max(64, maxSide)), 0.04f);
    out.w = std::max(1, int(std::ceil((x1 - x0) / out.cell)));
    out.h = std::max(1, int(std::ceil((z1 - z0) / out.cell)));
    out.xLeft = x1;  // la izquierda de la imagen es +x
    out.zTop = z1;
    Canvas cv(out);
    if (interior) cv.cutY = 2.4f;
    const size_t n = size_t(out.w) * size_t(out.h);

    // 1. El suelo: el relieve con sus colores (mezclados entre los puntos), o el suelo plano.
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            size_t k = size_t(y) * size_t(out.w) + size_t(x);
            Vec3 p = out.world(float(x) + 0.5f, float(y) + 0.5f);
            if (t) {
                cv.h[k] = t->heightAt(p.x, p.z);
                float fx = clampf((p.x - t->x0) / t->cell, 0, float(t->nx - 1) - 1e-3f);
                float fz = clampf((p.z - t->z0) / t->cell, 0, float(t->nz - 1) - 1e-3f);
                int ix = int(fx), iz = int(fz);
                float a = fx - float(ix), b = fz - float(iz);
                auto col = [&](int i, int j) { return terrainColor_[size_t(j) * size_t(t->nx) + size_t(i)]; };
                cv.c[k] = mixRgb(mixRgb(col(ix, iz), col(ix + 1, iz), a), mixRgb(col(ix, iz + 1), col(ix + 1, iz + 1), a), b) | 0xFF000000u;
                cv.kind[k] = kTerrain;
            } else if (world_.ground) {
                cv.h[k] = 0;
                cv.c[k] = floorColor_ ? floorColor_ | 0xFF000000u : rgba(96, 92, 88);
                cv.kind[k] = kTerrain;
            }
        }

    // Lo que hay debajo de algo que está en el aire (para saber si cuelga de un techo).
    auto floorBelow = [&](Vec3 p, float top) {
        float f = t ? t->heightAt(p.x, p.z) : world_.ground ? 0.0f : kNone;
        for (const Block& b : blocks_)
            if (p.x > b.box.min.x && p.x < b.box.max.x && p.z > b.box.min.z && p.z < b.box.max.z && b.box.max.y <= top + 0.01f)
                f = std::max(f, b.box.max.y);
        return f;
    };
    auto hanging = [&](Vec3 center, float bottom) { return covered && bottom > floorBelow(center, bottom) + 2.5f; };

    // 2. Los bloques, la decoración y las piezas.
    for (const Block& b : blocks_) {
        if (b.color == 0 || hanging(b.box.center(), b.box.min.y)) continue;
        cv.box(translate(b.box.center()) * scale(b.box.halfExtents() * 2.0f), b.color | 0xFF000000u);
    }
    for (const Decor& d : decor_) {
        Mat4 m = d.transform * translate(d.center) * scale(d.half * 2.0f);
        Vec3 c = transformPoint(m, {});
        if (hanging(c, c.y - std::fabs(d.half.y))) continue;
        // Sin suelo, la niebla de muy abajo (Los Andamios) no deja ver lo que se pisa.
        if (!t && !world_.ground && d.sphere && c.y + std::fabs(d.half.y) < heroStart_.y - 8) continue;
        float r = channel(d.color, 0), g = channel(d.color, 1), b = channel(d.color, 2);
        cv.soft = !t && d.sphere && g > r * 1.08f && g > b * 1.08f;  // follaje
        if (d.sphere) cv.sphere(m, d.color | 0xFF000000u);
        else cv.box(m, d.color | 0xFF000000u);
    }
    cv.soft = false;
    const PropLibrary& lib = propLibrary();
    std::vector<Vec3> wv;
    for (const PropInstance& p : props_) {
        if (p.def < 0 || size_t(p.def) >= lib.size() || p.radius < 0.45f) continue;  // la hierba y las flores, no
        const PropDef& d = lib[size_t(p.def)];
        if (!d.loaded || d.verts.empty()) continue;
        if (p.center.x + p.radius < x0 || p.center.x - p.radius > x1 || p.center.z + p.radius < z0 || p.center.z - p.radius > z1) continue;
        if (covered && hanging(p.center, transformPoint(p.transform, d.bounds.min).y)) continue;
        wv.resize(d.verts.size());
        for (size_t i = 0; i < d.verts.size(); ++i) {
            const float* q = d.verts[i].pos;
            wv[i] = transformPoint(p.transform, {q[0], q[1], q[2]});
        }
        for (size_t i = 0; i + 2 < d.indices.size(); i += 3) {
            uint32_t a = d.indices[i], b = d.indices[i + 1], e = d.indices[i + 2];
            if (a >= wv.size() || b >= wv.size() || e >= wv.size()) continue;
            uint32_t col = d.verts[a].color;
            float r = channel(col, 0), g = channel(col, 1), bl = channel(col, 2);
            cv.soft = !t && g > r * 1.08f && g > bl * 1.08f;  // las hojas, a medias
            cv.tri(wv[a], wv[b], wv[e], col | 0xFF000000u);
        }
    }

    // Copas sobre el vacío: son lo único que hay, se pintan enteras.
    for (size_t k = 0; k < n; ++k)
        if (cv.hs[k] > kNone * 0.5f && (cv.kind[k] == kVoid || cv.hs[k] < cv.h[k])) {
            if (cv.kind[k] == kVoid) cv.h[k] = cv.hs[k], cv.c[k] = cv.cs[k], cv.kind[k] = kSolid;
            cv.hs[k] = kNone;
        }

    // En un interior, el suelo que no cubre la sala es el vacío de fuera.
    if (interior)
        for (size_t k = 0; k < n; ++k)
            if (cv.kind[k] == kTerrain) cv.kind[k] = kVoid, cv.h[k] = kNone;

    cv.soft = false;

    // 3. El agua: lo que queda por debajo de su nivel, más oscuro cuanto más hondo.
    std::vector<uint8_t> water(n, 0);
    for (size_t k = 0; k < n; ++k)
        if (cv.kind[k] != kVoid && cv.h[k] < waterLevel_) {
            float depth = waterLevel_ - cv.h[k];
            float s = clampf(depth / 6.0f, 0, 1);
            cv.c[k] = mixRgb(mixRgb(rgba(96, 178, 180), rgba(28, 72, 112), s), cv.c[k], 0.18f * (1 - s)) | 0xFF000000u;
            cv.h[k] = waterLevel_;
            water[k] = 1;
        }

    // 4. Sombreado: el relieve iluminado desde arriba a la izquierda (en el mundo, desde +x y +z),
    // las sombras que echa lo alto, la línea al pie de las paredes y las curvas de nivel.
    const Vec3 sun = normalize(Vec3{0.55f, 0.85f, 0.55f});
    const float tanSun = 0.85f;
    float lo = 1e9f, hi = -1e9f;
    for (size_t k = 0; k < n; ++k)
        if (cv.kind[k] == kTerrain && !water[k]) lo = std::min(lo, cv.h[k]), hi = std::max(hi, cv.h[k]);
    const float contour = hi - lo > 120 ? 10.0f : hi - lo > 30 ? 5.0f : 2.0f;
    const bool contours = t && !covered && hi > lo;
    auto H = [&](int x, int y) {
        x = std::clamp(x, 0, out.w - 1), y = std::clamp(y, 0, out.h - 1);
        return cv.h[size_t(y) * size_t(out.w) + size_t(x)];
    };
    out.pixels.assign(n, 0);
    // Cada fila es independiente: se reparten entre los núcleos (es lo que más tarda).
    auto shadeRow = [&](int y) {
        for (int x = 0; x < out.w; ++x) {
            size_t k = size_t(y) * size_t(out.w) + size_t(x);
            if (cv.kind[k] == kVoid) continue;
            float h0 = cv.h[k];
            auto nb = [&](int dx, int dy) {
                float v = H(x + dx, y + dy);
                return v <= kNone * 0.5f ? h0 : v;  // al borde del vacío, como si siguiera
            };
            // La imagen va al revés que el mundo en x (y en z): +u es −x, +v es −z.
            // En el relieve, la pendiente a la escala de su rejilla (si no, se ve cada faceta).
            int st = cv.kind[k] == kTerrain && t ? std::max(1, int(std::lround(t->cell / out.cell))) : 1;
            float gx = clampf((nb(-st, 0) - nb(st, 0)) / (2 * float(st) * out.cell), -3, 3);
            float gz = clampf((nb(0, -st) - nb(0, st)) / (2 * float(st) * out.cell), -3, 3);
            Vec3 nrm = normalize(Vec3{-gx, 1, -gz});
            float shade = clampf(0.32f + 0.68f * dot(nrm, sun) / sun.y, 0.35f, 1.22f);
            if (!covered && !water[k]) {  // sombra: algo más alto hacia el sol
                float dist = 0;
                for (int s = 1; s <= 48; s += s < 8 ? 1 : 3) {
                    dist = float(s) * out.cell * 1.41421356f;
                    float hs = H(x - s, y - s);
                    if (hs > h0 + dist * tanSun + 0.15f) {
                        shade *= 0.66f;
                        break;
                    }
                }
            }
            // Al pie de una pared, una línea oscura; en lo alto de un borde, un filo claro.
            float up = std::max({nb(1, 0), nb(-1, 0), nb(0, 1), nb(0, -1)}) - h0;
            float down = h0 - std::min({nb(1, 0), nb(-1, 0), nb(0, 1), nb(0, -1)});
            bool solidNear = cv.kind[k] == kSolid;
            for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                int xx = std::clamp(x + dx, 0, out.w - 1), yy = std::clamp(y + dy, 0, out.h - 1);
                solidNear |= cv.kind[size_t(yy) * size_t(out.w) + size_t(xx)] == kSolid;
            }
            if (solidNear && up > 0.9f) shade *= 0.55f;
            else if (solidNear && down > 0.9f) shade *= 1.12f;
            uint32_t col = scaleRgb(cv.c[k], shade);
            if (contours && cv.kind[k] == kTerrain && !water[k]) {
                int band = int(std::floor(h0 / contour));
                bool line = false;
                for (auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}}) {
                    size_t j = size_t(std::min(y + dy, out.h - 1)) * size_t(out.w) + size_t(std::min(x + dx, out.w - 1));
                    if (cv.kind[j] == kTerrain && !water[j] && int(std::floor(cv.h[j] / contour)) != band) line = true;
                }
                if (line) col = mixRgb(col, rgba(40, 30, 20), (band % 5 == 0) ? 0.38f : 0.2f);
            }
            if (water[k]) {  // la orilla, en claro
                bool shore = false;
                for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    int xx = std::clamp(x + dx, 0, out.w - 1), yy = std::clamp(y + dy, 0, out.h - 1);
                    size_t j = size_t(yy) * size_t(out.w) + size_t(xx);
                    if (cv.kind[j] != kVoid && !water[j]) shore = true;
                }
                if (shore) col = mixRgb(col, rgba(226, 236, 226), 0.55f);
            }
            // Lo que no se pisa (la roca alrededor de las salas), oscuro y sin color.
            if (covered && t) {
                float f = t->fenceAt(out.world(float(x) + 0.5f, float(y) + 0.5f).x, out.world(float(x) + 0.5f, float(y) + 0.5f).z);
                if (f > 0) {
                    float k2 = clampf(f / 1.5f, 0, 1);
                    float l = (channel(col, 0) * 0.3f + channel(col, 1) * 0.59f + channel(col, 2) * 0.11f) * 0.42f;
                    col = mixRgb(col, pack(l * 0.9f, l * 0.88f, l), 0.4f + 0.55f * k2);
                }
            }
            if (cv.hs[k] > kNone * 0.5f) {  // la copa por encima, a medias (y su sombra suave)
                float lc = 0.75f + 0.25f * std::sin(float(x * 7 + y * 13) * 0.37f) * 0.2f;
                col = mixRgb(scaleRgb(col, 0.82f), scaleRgb(cv.cs[k], lc), 0.45f);
            }
            // Un poco hacia el papel: el color se queda, pero entona con la interfaz.
            float l = channel(col, 0) * 0.3f + channel(col, 1) * 0.59f + channel(col, 2) * 0.11f;
            col = mixRgb(col, pack(l * 1.04f, l * 0.98f, l * 0.88f), 0.16f);
            out.pixels[k] = col | 0xFF000000u;
        }
    };
    int workers = int(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    std::vector<std::thread> pool;
    for (int w = 1; w < workers; ++w)
        pool.emplace_back([&, w] {
            for (int y = w; y < out.h; y += workers) shadeRow(y);
        });
    for (int y = 0; y < out.h; y += workers) shadeRow(y);
    for (std::thread& th : pool) th.join();
}
