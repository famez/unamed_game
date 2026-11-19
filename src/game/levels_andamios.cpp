// Los Andamios: puzles de saltos sobre el vacío, en los andamios que suben por un pilar del
// Faro. Cada tramo enseña una cosa y el siguiente la combina con lo anterior:
//   1. La escalera de gato: del rellano de salida a lo alto de la primera torre.
//   2. Los saltos: losas sueltas, cada vez más separadas y más altas (la última, con doble salto).
//   3. El pozo: dos paredes enfrentadas; se sube saltando de una a otra.
//   4. Las cornisas: losas estrechas hasta una pared suelta; desde ella, un salto de pared
//      lleva a la cornisa alta. Y desde la cornisa, un salto a la escalera que cuelga de la torre.
//   5. La cima: la linterna del andamio.
// Sin suelo: quien se cae vuelve al principio del tramo en el que iba.
//
// Las paredes a las que agarrarse son de pizarra oscura con vetas cálidas; las losas, de piedra
// clara con el canto encendido, para que se lea bien dónde acaban.

#include "game/game.h"
#include "game/map_tools.h"

#include <cmath>

using namespace maptools;

namespace {

const uint32_t kSlab = rgba(150, 146, 140), kPillar = rgba(132, 130, 136), kSlate = rgba(82, 84, 104);
const uint32_t kWarm = rgba(255, 196, 120), kIron = rgba(74, 70, 66), kWood = rgba(120, 92, 66);
const uint32_t kMist = rgba(140, 142, 160);

}  // namespace

void Game::addLadder(Vec3 base, float top, Vec3 normal, float halfWidth) {
    world_.ladders.push_back({base, top, normal, halfWidth});
    const Mat4 id = Mat4::identity();
    Vec3 side{-normal.z, 0, normal.x};
    Vec3 at = base + normal * 0.07f;
    float h = top - base.y + 0.9f;  // los largueros pasan del borde: de ahí se agarra al subir
    for (float s : {-1.0f, 1.0f}) {
        Vec3 c = at + side * (s * halfWidth) + Vec3{0, h * 0.5f, 0};
        addDecor(id, c, {0.035f + std::fabs(side.x) * 0.0f, h * 0.5f, 0.035f}, kIron, Material::Metal, false);
        Vec3 hook = base + side * (s * halfWidth) + Vec3{0, top + 0.9f, 0} - normal * 0.25f;  // se dobla hacia el borde
        addDecor(id, (hook + c + Vec3{0, h * 0.5f, 0}) * 0.5f,
                 {std::fabs(normal.x) * 0.16f + 0.035f, 0.035f, std::fabs(normal.z) * 0.16f + 0.035f}, kIron, Material::Metal, false);
    }
    Vec3 rung = std::fabs(side.x) > 0.5f ? Vec3{halfWidth, 0.022f, 0.022f} : Vec3{0.022f, 0.022f, halfWidth};
    for (float y = 0.3f; y < top - base.y + 0.6f; y += 0.3f) addDecor(id, at + Vec3{0, y, 0}, rung, kIron, Material::Metal, false);
}

void Game::buildAndamios() {
    levelName_ = "Los Andamios";
    world_.ground = false;
    night_ = 0.25f;  // el crepúsculo detenido
    fogScale_ = 1.15f;
    viewDistance_ = 240;
    const Mat4 id = Mat4::identity();
    Rng rng{907};

    // Losa suelta (arriba en y = top) con el canto encendido y dos puntales que bajan a la niebla.
    auto slab = [&](Vec3 c, float hx, float hz, float top, bool posts = true) {
        addBlock({c.x, top - 0.25f, c.z}, {hx, 0.25f, hz}, kSlab, Material::Stone);
        addRim({c.x, top, c.z}, hx, hz, kWarm);
        if (!posts) return;
        for (float s : {-1.0f, 1.0f}) {
            Vec3 p{c.x + s * (hx - 0.3f), top - 0.5f, c.z};
            addDecor(id, p - Vec3{0, 15, 0}, {0.12f, 15, 0.12f}, kWood, Material::Cloth, false);
        }
    };
    // Torre maciza que sube desde la niebla hasta `top`.
    auto tower = [&](Vec3 lo, Vec3 hi, float top, uint32_t col = kPillar) {
        addBlock({(lo.x + hi.x) * 0.5f, (top - 40) * 0.5f, (lo.z + hi.z) * 0.5f}, {(hi.x - lo.x) * 0.5f, (top + 40) * 0.5f, (hi.z - lo.z) * 0.5f},
                 col, Material::Stone);
        addRim({(lo.x + hi.x) * 0.5f, top, (lo.z + hi.z) * 0.5f}, (hi.x - lo.x) * 0.5f, (hi.z - lo.z) * 0.5f, kWarm);
    };
    // Pared para agarrarse: pizarra oscura con vetas cálidas en la cara (de y0 a y1).
    auto wall = [&](Vec3 lo, Vec3 hi, float y0, float y1) {
        addBlock({(lo.x + hi.x) * 0.5f, (y0 + y1) * 0.5f, (lo.z + hi.z) * 0.5f}, {(hi.x - lo.x) * 0.5f, (y1 - y0) * 0.5f, (hi.z - lo.z) * 0.5f},
                 kSlate, Material::Stone);
        bool alongZ = hi.z - lo.z > hi.x - lo.x;  // la cara larga
        float len = alongZ ? hi.z - lo.z : hi.x - lo.x;
        for (float t = 0.5f; t < len; t += 1.1f)
            for (float s : {-1.0f, 1.0f}) {
                float off = rng.range(-0.15f, 0.15f);
                Vec3 c = alongZ ? Vec3{s > 0 ? hi.x + 0.01f : lo.x - 0.01f, (y0 + y1) * 0.5f + off, lo.z + t}
                                : Vec3{lo.x + t, (y0 + y1) * 0.5f + off, s > 0 ? hi.z + 0.01f : lo.z - 0.01f};
                Vec3 half = alongZ ? Vec3{0.01f, (y1 - y0) * 0.42f, 0.03f} : Vec3{0.03f, (y1 - y0) * 0.42f, 0.01f};
                addDecor(id, c, half, rgba(150, 110, 80), Material::Glow, false);
            }
        addRim({(lo.x + hi.x) * 0.5f, y1, (lo.z + hi.z) * 0.5f}, (hi.x - lo.x) * 0.5f, (hi.z - lo.z) * 0.5f, kWarm);
    };

    // --- 1. La escalera de gato ------------------------------------------------------------
    slab({0, 0, 1.5f}, 3.5f, 4.5f, 0);                // el rellano de salida
    tower({-2, 0, 6}, {2, 0, 10}, 5);                 // la primera torre
    addLadder({0, 0, 6}, 5, {0, 0, -1});
    // --- 2. Los saltos ------------------------------------------------------------------------
    slab({0, 0, 13.5f}, 1.5f, 1.5f, 5);               // 2 m de hueco, a la misma altura
    slab({0, 0, 18.0f}, 1.5f, 1.5f, 6.2f);            // 1,5 m de hueco y algo más arriba
    slab({5.5f, 0, 22.5f}, 4.0f, 2.0f, 8.6f);         // 2,4 m más arriba: doble salto; es el suelo del pozo
    // --- 3. El pozo ---------------------------------------------------------------------------
    wall({6.0f, 0, 21.4f}, {6.8f, 0, 24.5f}, 8.6f, 13.8f);  // la pared baja (se rodea por el hueco de delante)
    tower({9.6f, 0, 19}, {14, 0, 26}, 15.2f, kSlate);       // la alta: su cara es la otra pared del pozo
    for (float z = 19.5f; z < 26; z += 1.1f)                // vetas en la cara del pozo
        addDecor(id, {9.59f, 11.9f + rng.range(-0.15f, 0.15f), z}, {0.01f, 2.8f, 0.03f}, rgba(150, 110, 80), Material::Glow, false);
    // --- 4. Las cornisas ----------------------------------------------------------------------
    slab({12, 0, 28.6f}, 0.9f, 0.9f, 15.2f);          // losas estrechas
    slab({12.0f, 0, 31.6f}, 0.8f, 0.8f, 15.8f);
    slab({8.6f, 0, 34.0f}, 1.2f, 2.5f, 16.4f);        // al pie de la pared suelta
    wall({10.2f, 0, 32.5f}, {10.8f, 0, 35.5f}, 14.0f, 23.5f);  // la pared suelta, frente a la cornisa
    tower({3, 0, 32}, {7, 0, 36}, 21.2f);             // la cornisa alta (4,8 m: no llega ni el doble salto)
    // De la cornisa a la torre de la cima: la escalera cuelga sobre el vacío; se llega saltando.
    tower({0, 0, 38}, {6, 0, 42}, 27.0f);
    addLadder({4.5f, 21.5f, 38}, 27.0f, {0, 0, -1});
    // --- 5. La cima: la linterna --------------------------------------------------------------
    const Vec3 lantern{3, 27, 40};
    addDecor(id, lantern + Vec3{0, 0.5f, 0}, {0.45f, 0.5f, 0.45f}, kIron, Material::Metal, false);
    addDecor(id, lantern + Vec3{0, 1.35f, 0}, {0.35f, 0.35f, 0.35f}, rgba(255, 210, 140), Material::Glow, false);
    addDecor(id, lantern + Vec3{0, 1.85f, 0}, {0.5f, 0.12f, 0.5f}, kIron, Material::Metal, false);
    blocks_.push_back({boxFromCenter(lantern + Vec3{0, 1.0f, 0}, {0.45f, 1.0f, 0.45f}), 0, Material::Stone});  // invisible

    // --- El fondo: el pilar del Faro, otros andamios y la niebla ----------------------------
    addDecor(id, {-30, 10, 60}, {9, 70, 9}, rgba(86, 86, 96), Material::Stone, false);  // el pilar, enorme, detrás
    addDecor(id, {-30, 82, 60}, {11, 2, 11}, rgba(70, 70, 80), Material::Stone, false);
    for (int i = 0; i < 18; ++i) {  // andamios abandonados alrededor
        float a = float(i) / 18 * 2 * kPi + rng.range(-0.1f, 0.1f), r = rng.range(45, 90);
        Vec3 p{std::cos(a) * r + 4, 0, std::sin(a) * r + 20};
        float top = rng.range(-6, 30), w = rng.range(1.5f, 3.5f);
        addDecor(id, {p.x, top - 25, p.z}, {w * 0.35f, 25, w * 0.35f}, rgba(92, 92, 102), Material::Stone, false);
        addDecor(id, {p.x, top - 0.2f, p.z}, {w, 0.2f, w}, kSlab, Material::Stone, false);
        if (rng.next() < 0.4f) addDecor(id, {p.x, top + 0.6f, p.z}, {0.12f, 0.12f, 0.12f}, kWarm, Material::Glow, true);
    }
    addDecor(id, {0, -38, 20}, {400, 0.5f, 400}, rgba(120, 120, 140), Material::Cloth, false);  // mar de nubes
    for (int i = 0; i < 22; ++i) {
        float a = rng.range(0, 6.28f), r = rng.range(8, 100);
        Vec3 p{std::cos(a) * r, rng.range(-30, -14), std::sin(a) * r + 20};
        float s = rng.range(8, 18);
        addDecor(id, p, {s, s * 0.22f, s * 0.8f}, kMist, Material::Cloth, true);
    }

    // --- Salida y tramos ------------------------------------------------------------------------
    heroStart_ = {0, 0, -1.0f};
    heroStartYaw_ = 0;
    auto reach = [](const char* title, const char* goal, AABB area, Vec3 marker) {
        return LevelPart{title, goal, PartKind::Reach, "", 0, area, marker, {}, "", ""};
    };
    parts_ = {
        reach("La escalera", "Sube por la escalera de gato (empuja contra ella)", {{-2, 4.5f, 6}, {2, 8, 10}}, {0, 6, 8}),
        reach("Los saltos", "Salta de losa en losa (la última está alta: doble salto)", {{1.5f, 8.1f, 20.5f}, {6.0f, 11, 24.5f}}, {4, 9.5f, 22.5f}),
        reach("El pozo", "Salta contra una pared, agárrate y salta a la otra", {{9.6f, 14.7f, 19}, {14, 18, 26}}, {11.8f, 16, 22.5f}),
        reach("Las cornisas", "Agárrate a la pared suelta y salta hacia la cornisa (y en el aire, otra vez)", {{3, 20.7f, 32}, {7, 24, 36}}, {5, 22.2f, 34}),
        reach("La cima", "Salta a la escalera que cuelga y sube a la linterna", {{0, 26.5f, 38}, {6, 30, 42}}, lantern + Vec3{0, 1.5f, 0}),
    };
    nextLevel_ = LevelId::Andamios;
}
