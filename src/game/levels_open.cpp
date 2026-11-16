// Mapas grandes: terreno con relieve (mapa de alturas) y escenario hecho con piezas .glb
// (assets/props, generadas por tools/props o rehechas a mano). Por ahora solo construcción:
// sin habitantes ni combates, para recorrerlos.
//   - Campo: valle abierto de día, con colinas, un río con puente, una granja, molinos en la
//     loma y un pinar.
//   - Ciudad Neón (de noche): manzanas de torres con ventanas encendidas, tiendas, letreros
//     verticales, vía elevada y un santuario con torii y cerezos en el centro.
//   - Pueblo cafetero (de día): plaza con iglesia y kiosco, calles de casas de bahareque de
//     colores en la loma y cafetales, plataneras, guaduales y palmas de cera alrededor.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <algorithm>
#include <cmath>

using namespace maptools;

namespace {

const Vec3 kSun = normalize(Vec3{0.45f, 0.8f, 0.35f});  // la misma que el render

}  // namespace

// --- Herramientas ------------------------------------------------------------------------

void Game::makeTerrain(float size, float cell) {
    auto t = std::make_shared<Terrain>();
    t->cell = cell;
    t->nx = t->nz = int(size / cell) + 1;
    t->x0 = t->z0 = -size * 0.5f;
    t->heights.assign(size_t(t->nx) * size_t(t->nz), 0.0f);
    world_.terrain = t;
    terrainColor_.assign(t->heights.size(), rgba(80, 130, 70));
    terrainMaterial_.assign(t->heights.size(), uint8_t(Material::Grass));
    ++terrainVersion_;
}

void Game::paintTerrain(int ix, int iz, uint32_t color, Material m) {
    const Terrain& t = *world_.terrain;
    if (ix < 0 || iz < 0 || ix >= t.nx || iz >= t.nz) return;
    size_t k = size_t(iz) * size_t(t.nx) + size_t(ix);
    terrainColor_[k] = color;
    terrainMaterial_[k] = uint8_t(m);
}

bool Game::addProp(const std::string& name, Vec3 pos, float yaw, float scl, bool onGround) {
    const PropLibrary& lib = propLibrary();
    int i = lib.find(name);
    if (i < 0) return false;
    const PropDef& d = lib[size_t(i)];
    Mat4 local = rotateY(yaw) * scale({scl, scl, scl});
    if (onGround) {  // en una cuesta se apoya en lo más bajo (el resto queda enterrado, como un cimiento)
        float lowest = world_.groundAt(pos.x, pos.z);
        for (float sx : {0.0f, 1.0f})
            for (float sz : {0.0f, 1.0f}) {
                Vec3 c = transformPoint(local, {sx ? d.bounds.max.x : d.bounds.min.x, 0, sz ? d.bounds.max.z : d.bounds.min.z});
                lowest = std::min(lowest, world_.groundAt(pos.x + c.x * 0.8f, pos.z + c.z * 0.8f));
            }
        pos.y = lowest;
    }
    Mat4 xf = translate(pos) * local;
    auto worldBox = [&](const AABB& b) {
        AABB out{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
        for (int c = 0; c < 8; ++c) {
            Vec3 p = transformPoint(xf, {c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z});
            out.min = {std::min(out.min.x, p.x), std::min(out.min.y, p.y), std::min(out.min.z, p.z)};
            out.max = {std::max(out.max.x, p.x), std::max(out.max.y, p.y), std::max(out.max.z, p.z)};
        }
        return out;
    };
    AABB wb = worldBox(d.bounds);
    props_.push_back({i, xf, wb.center(), length(wb.halfExtents())});
    for (const AABB& c : d.colliders) propSolids_.push_back(worldBox(c));
    for (const PropLight& pl : d.lights) {
        Lamp l;
        l.pos = transformPoint(xf, pl.pos);
        l.color = pl.color;
        l.style = LampStyle::Prop;
        lamps_.push_back(l);
    }
    return true;
}

// --- Campo -------------------------------------------------------------------------------

void Game::buildCampo() {
    levelName_ = "Campo abierto";
    floorColor_ = rgba(80, 130, 70);
    floorMaterial_ = Material::Grass;
    daylight_ = true;
    fogScale_ = 0.4f;
    viewDistance_ = 420;
    const float kSize = 400;
    makeTerrain(kSize, 2.0f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);

    // Camino (de la llegada al pueblo, al molino y al pinar) y el río que lo cruza.
    const std::vector<Vec3> path = {{0, 0, -150}, {10, 0, -100}, {25, 0, -55}, {30, 0, -20}, {45, 0, 20}, {52, 0, 45},
                                    {20, 0, 75},  {-30, 0, 95},  {-80, 0, 108}, {-100, 0, 112}};
    const std::vector<Vec3> branch = {{52, 0, 45}, {95, 0, 70}, {120, 0, 110}, {130, 0, 150}};
    auto riverZ = [](float x) { return -30.0f + 22.0f * std::sin(x / 45.0f) + 8.0f * std::sin(x / 17.0f); };
    const Vec3 village{55, 0, 45}, hill{-110, 0, 120};

    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float h = 4.0f * (fbm(x / 95.0f, z / 95.0f, 11) + 1.0f) + 1.2f * fbm(x / 23.0f, z / 23.0f, 12);
            h += 16.0f * std::exp(-length(flat(p - hill)) * length(flat(p - hill)) / (2 * 38.0f * 38.0f));  // la loma del molino
            h += 9.0f * std::exp(-((x - 120) * (x - 120) + (z - 140) * (z - 140)) / (2 * 50.0f * 50.0f));    // el monte del pinar
            // Bordes: montañas que cierran el valle.
            float edge = std::min({x - t.x0, -t.x0 - x, z - t.z0, -t.z0 - z});
            if (edge < 45) h += 40.0f * (1 - edge / 45) * (1 - edge / 45);
            // El pueblo, en un llano.
            float dv = length(flat(p - village));
            h = h + (4.2f - h) * smoothFalloff(dv, 42.0f);
            // Camino: se suaviza el relieve (no se hunde).
            float dp = std::min(distToPath(p, path), distToPath(p, branch));
            float hp = h * 0.92f + 0.3f;
            h = h + (hp - h) * smoothFalloff(dp, 6.0f);
            // Río: cauce con orillas suaves.
            float dr = std::fabs(z - riverZ(x));
            h = h + (-1.3f - h) * smoothFalloff(dr, 9.0f) * smoothFalloff(dr, 9.0f);
            t.at(ix, iz) = std::max(h, -1.3f);
            // Color: hierba con variación, tierra en el camino, arena en las orillas, roca en las cumbres.
            float v = noise(x / 7.0f, z / 7.0f, 5) * 0.5f + 0.5f;
            uint32_t grass = mixRgb(rgba(78, 128, 60), rgba(112, 156, 70), v);
            if (h > 18) grass = mixRgb(grass, rgba(120, 118, 108), clampf((h - 18) / 10, 0, 1));
            Material m = h > 22 ? Material::Stone : Material::Grass;
            if (dr < 7.5f) grass = mixRgb(rgba(176, 160, 120), grass, clampf((dr - 5.0f) / 2.5f, 0, 1)), m = Material::Cloth;
            if (dp < 2.2f) grass = mixRgb(rgba(150, 120, 82), rgba(132, 104, 70), v), m = Material::Cloth;
            paintTerrain(ix, iz, grass, m);
        }
    waterLevel_ = -0.45f;

    auto ground = [&](float x, float z) { return t.heightAt(x, z); };
    auto clearOf = [&](Vec3 p, float pathGap, float riverGap) {
        return std::min(distToPath(p, path), distToPath(p, branch)) > pathGap && std::fabs(p.z - riverZ(p.x)) > riverGap &&
               length(flat(p - village)) > 40 && std::fabs(p.x) < kSize / 2 - 30 && std::fabs(p.z) < kSize / 2 - 30;
    };
    Rng rng{20251116};

    // Puente donde el camino cruza el río.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        Vec3 a = path[i], b = path[i + 1];
        float fa = a.z - riverZ(a.x), fb = b.z - riverZ(b.x);
        if (fa * fb > 0) continue;
        Vec3 c = a + (b - a) * (fa / (fa - fb));
        float yaw = std::atan2(b.x - a.x, b.z - a.z);
        float bank = std::max(ground(c.x + std::sin(yaw) * 6, c.z + std::cos(yaw) * 6), ground(c.x - std::sin(yaw) * 6, c.z - std::cos(yaw) * 6));
        addProp("puente_madera", {c.x, bank - 0.55f, c.z}, yaw, 1.0f, false);
    }
    // Faroles a lo largo del camino.
    for (const auto* pl : {&path, &branch}) {
        float acc = 0;
        for (size_t i = 0; i + 1 < pl->size(); ++i) {
            Vec3 a = (*pl)[i], b = (*pl)[i + 1], d = flat(b - a);
            float len = length(d);
            Vec3 side = normalize(Vec3{d.z, 0, -d.x});
            for (float s = 0; s < len; s += 2) {
                acc += 2;
                if (acc < 28) continue;
                acc = 0;
                Vec3 p = a + d * (s / len) + side * 2.6f;
                if (std::fabs(p.z - riverZ(p.x)) < 9) continue;
                addProp("farol_camino", p, std::atan2(-side.x, -side.z));
            }
        }
    }
    // El pueblo: tres casas, el granero, el pozo, un huerto vallado con fardos.
    addProp("casa_campo", village + Vec3{-14, 0, 12}, 3.3f);
    addProp("casa_campo_verde", village + Vec3{8, 0, 18}, 3.0f);
    addProp("casa_campo", village + Vec3{18, 0, -4}, -1.6f);
    addProp("granero", village + Vec3{-18, 0, -14}, 0.4f);
    addProp("pozo", village + Vec3{0, 0, 2});
    addProp("arbusto_flores", village + Vec3{-9, 0, 8});
    addProp("arbusto_flores", village + Vec3{12, 0, 12});
    addProp("roble_grande", village + Vec3{-4, 0, 22});
    for (int k = 0; k < 8; ++k) {  // huerto vallado
        Vec3 c = village + Vec3{22, 0, 18};
        addProp("valla", c + Vec3{-6 + 3.0f * float(k % 4), 0, k < 4 ? -6.0f : 6.0f}, 0);
        addProp("valla", c + Vec3{k < 4 ? -7.5f : 7.5f, 0, -4.5f + 3.0f * float(k % 4)}, kPi * 0.5f);
    }
    for (int k = 0; k < 6; ++k) addProp("fardo_heno", village + Vec3{18.0f + float(k % 3) * 3.5f, 0, 14.0f + float(k / 3) * 4.5f}, 0.3f * float(k));
    addProp("senal_camino", {50, 0, 38}, 0.6f);
    // La loma de los molinos.
    addProp("molino", hill + Vec3{0, 0, 0}, 2.6f);
    addProp("molino", hill + Vec3{-26, 0, 24}, 2.2f);
    addProp("molino", hill + Vec3{22, 0, 30}, 2.9f);
    // Pinar al noreste.
    for (int k = 0; k < 260; ++k) {
        Vec3 p{rng.range(60, 180), 0, rng.range(85, 182)};
        if (!clearOf(p, 4, 10)) continue;
        addProp(rng.next() < 0.4f ? "pino_alto" : "pino", p, rng.range(0, 6.28f), rng.range(0.85f, 1.25f));
    }
    // Robles por los prados, arbustos, rocas, tocones y troncos.
    for (int k = 0; k < 140; ++k) {
        Vec3 p{rng.range(-170, 170), 0, rng.range(-170, 170)};
        if (!clearOf(p, 6, 11) || (p.x > 55 && p.z > 80)) continue;
        addProp(rng.next() < 0.3f ? "roble_grande" : "roble", p, rng.range(0, 6.28f), rng.range(0.85f, 1.2f));
        if (rng.next() < 0.5f) addProp("arbusto", p + Vec3{rng.range(-4, 4), 0, rng.range(2, 5)}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 160; ++k) {
        Vec3 p{rng.range(-170, 170), 0, rng.range(-170, 170)};
        if (!clearOf(p, 3, 8)) continue;
        float r = rng.next();
        const char* what = r < 0.35f ? "arbusto" : r < 0.5f ? "arbusto_flores" : r < 0.72f ? "piedra" : r < 0.86f ? "roca"
                         : r < 0.93f ? "tocon" : "tronco_caido";
        addProp(what, p, rng.range(0, 6.28f), rng.range(0.8f, 1.3f));
    }
    for (int k = 0; k < 18; ++k) {  // rocas grandes en las laderas
        Vec3 p{rng.range(-175, 175), 0, rng.range(-175, 175)};
        if (!clearOf(p, 8, 10) || ground(p.x, p.z) < 6) continue;
        addProp("roca_grande", p, rng.range(0, 6.28f), rng.range(0.8f, 1.5f));
    }
    // Hierba alta y flores (solo se dibujan cerca).
    for (int k = 0; k < 2600; ++k) {
        Vec3 p{rng.range(-170, 170), 0, rng.range(-170, 170)};
        if (!clearOf(p, 2.5f, 7) && length(flat(p - village)) > 40) continue;
        if (distToPath(p, path) < 2.5f || std::fabs(p.z - riverZ(p.x)) < 7) continue;
        float r = rng.next();
        addProp(r < 0.8f ? "hierba" : r < 0.9f ? "flores_amarillas" : "flores_lila", p, rng.range(0, 6.28f), rng.range(0.8f, 1.4f));
    }
    // Habitantes: la granja y la loma.
    addNpc("granjera", village + Vec3{22, 0, 11}, 4);
    addNpc("granjera", village + Vec3{-6, 0, -4}, 9);
    addNpc("abuelo", village + Vec3{2, 0, 5}, 4);
    addNpc("abuelo", {30, 0, -22}, 5);
    addNpc("granjera", hill + Vec3{8, 0, -12}, 12);
    addNpc("abuelo", hill + Vec3{-6, 0, -16}, 6);
    // Dos portales para cambiar de luz a sombra.
    addPortal({12, ground(12, -128), -128});
    addPortal({46, ground(46, 30), 30});

    heroStart_ = {0, ground(0, -150), -150};
    heroStartYaw_ = 0;
    LevelPart explore{"Campo abierto", "Sube a la loma de los molinos", PartKind::Reach, "", 0,
                      {hill + Vec3{-14, -10, -14}, hill + Vec3{14, 40, 14}}, hill + Vec3{0, 12, 0}, {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Campo;
}

// --- Ciudad Neón -------------------------------------------------------------------------

void Game::buildNeon() {
    levelName_ = "Ciudad Neón";
    floorColor_ = rgba(46, 48, 56);
    floorMaterial_ = Material::Ground;
    night_ = 1;
    fogScale_ = 0.55f;
    viewDistance_ = 300;
    const Mat4 id = Mat4::identity();
    Rng rng{8128};
    const float kBlock = 44, kRoad = 12, kWalk = 3;  // manzana (de eje a eje), calzada y acera
    const int kN = 3;  // manzanas de -kN a kN
    const float half = (float(kN) + 0.5f) * kBlock;
    // Aceras (un escalón bajo) y líneas de la calzada.
    for (int bx = -kN; bx <= kN; ++bx)
        for (int bz = -kN; bz <= kN; ++bz) {
            Vec3 c{float(bx) * kBlock, 0, float(bz) * kBlock};
            float inner = (kBlock - kRoad) * 0.5f;
            addDecor(id, c + Vec3{0, 0.07f, 0}, {inner, 0.07f, inner}, rgba(92, 94, 104), Material::Stone, false);
            addDecor(id, c + Vec3{0, 0.145f, 0}, {inner - kWalk, 0.005f, inner - kWalk}, rgba(60, 62, 70), Material::Ground, false);
        }
    for (int k = -kN; k <= kN + 1; ++k) {  // líneas discontinuas en el centro de cada calle
        float r = (float(k) - 0.5f) * kBlock;
        for (float s = -half; s < half; s += 6) {
            addDecor(id, {r, 0.012f, s}, {0.1f, 0.01f, 1.5f}, rgba(230, 210, 120), Material::Cloth, false);
            addDecor(id, {s, 0.012f, r}, {1.5f, 0.01f, 0.1f}, rgba(230, 210, 120), Material::Cloth, false);
        }
    }
    const char* towers[] = {"torre_neon_a", "torre_neon_b", "torre_neon_c", "torre_neon_d"};
    const char* shops[] = {"tienda_ramen", "tienda_kombini", "tienda_recreativos"};
    const char* signs[] = {"letrero_rosa", "letrero_cian", "letrero_amarillo"};
    for (int bx = -kN; bx <= kN; ++bx)
        for (int bz = -kN; bz <= kN; ++bz) {
            Vec3 c{float(bx) * kBlock, 0, float(bz) * kBlock};
            float inner = (kBlock - kRoad) * 0.5f;
            if (bx == 0 && bz == 0) {  // el santuario: torii, cerezos y puestos de comida
                addProp("torii", c + Vec3{0, 0, inner - 4}, 0);
                addProp("torii", c + Vec3{0, 0, inner - 10}, 0);
                for (int k = 0; k < 6; ++k) {
                    float a = float(k) * kPi / 3 + 0.4f;
                    addProp("cerezo", c + Vec3{std::cos(a) * 10, 0, std::sin(a) * 9 - 3}, a, rng.range(0.9f, 1.2f));
                }
                for (int k = 0; k < 3; ++k) addProp("puesto_comida", c + Vec3{-8.0f + float(k) * 8, 0, -inner + 4}, kPi);
                addProp("farola_led", c + Vec3{-5, 0, 2}, 0);
                addProp("farola_led", c + Vec3{5, 0, 2}, kPi);
                continue;
            }
            // Torres al fondo de la manzana; tiendas, letreros y expendedoras en el borde que da a la calle.
            int towersHere = 1 + rng.pick(3);
            for (int k = 0; k < towersHere; ++k) {
                float ox = (k == 0 ? -1.0f : k == 1 ? 1.0f : 0.0f) * 9.0f, oz = k == 2 ? -9.0f : 2.0f;
                addProp(towers[rng.pick(4)], c + Vec3{ox, 0.14f, oz}, float(rng.pick(4)) * kPi * 0.5f, 1.0f, false);
            }
            for (int side = 0; side < 4; ++side) {
                float yaw = float(side) * kPi * 0.5f;
                Vec3 out{std::sin(yaw), 0, std::cos(yaw)}, along{out.z, 0, -out.x};
                Vec3 edge = c + out * (inner - kWalk - 4.5f);
                if (rng.next() < 0.6f) addProp(shops[rng.pick(3)], edge + along * rng.range(-8, 8), yaw, 1.0f, false);
                Vec3 walk = c + out * (inner - 1.2f);
                if (side < 2)  // una acera de cada calle (la de enfrente es de la manzana vecina), al tresbolillo
                    for (float s = -inner + 6 + float(side) * 8; s < inner - 3; s += 16)
                        addProp("farola_led", walk + along * s + Vec3{0, 0.14f, 0}, yaw + kPi, 1.0f, false);
                if (rng.next() < 0.5f) addProp(signs[rng.pick(3)], walk + along * rng.range(-10, 10) - out * 1.5f + Vec3{0, 0.14f, 0}, yaw, 1.0f, false);
                if (rng.next() < 0.5f)
                    addProp(rng.next() < 0.5f ? "maquina_expendedora" : "maquina_expendedora_roja",
                            walk + along * rng.range(-12, 12) - out * 1.8f + Vec3{0, 0.14f, 0}, yaw, 1.0f, false);
                if (rng.next() < 0.3f) addProp("poste_electrico", walk + along * rng.range(-14, 14) + Vec3{0, 0.14f, 0}, yaw, 1.0f, false);
            }
            if (rng.next() < 0.25f) addProp("pantalla_gigante", c + Vec3{0, 0.14f, -inner + 6}, kPi, 1.0f, false);
        }
    // Cruces: semáforos y pasos de cebra; coches y taxis aparcados.
    for (int ix = -kN; ix <= kN + 1; ++ix)
        for (int iz = -kN; iz <= kN + 1; ++iz) {
            Vec3 x{(float(ix) - 0.5f) * kBlock, 0, (float(iz) - 0.5f) * kBlock};
            if (std::fabs(x.x) > half - 1 || std::fabs(x.z) > half - 1) continue;
            addProp("paso_cebra", x + Vec3{0, 0, kRoad * 0.5f + 2.5f}, kPi * 0.5f, 1.0f, false);
            addProp("paso_cebra", x + Vec3{kRoad * 0.5f + 2.5f, 0, 0}, 0, 1.0f, false);
            addProp("semaforo", x + Vec3{kRoad * 0.5f + 1, 0.14f, kRoad * 0.5f + 1}, kPi, 1.0f, false);
            addProp("semaforo", x + Vec3{-kRoad * 0.5f - 1, 0.14f, -kRoad * 0.5f - 1}, 0, 1.0f, false);
            for (int k = 0; k < 2; ++k)
                if (rng.next() < 0.55f) {
                    bool alongZ = k == 0;
                    Vec3 p = x + (alongZ ? Vec3{kRoad * 0.5f - 2.0f, 0, rng.range(12, 30)} : Vec3{rng.range(12, 30), 0, kRoad * 0.5f - 2.0f});
                    addProp(rng.next() < 0.4f ? "taxi" : "coche", p, alongZ ? 0.0f : kPi * 0.5f, 1.0f, false);
                }
        }
    // Vía elevada a lo largo de una avenida.
    for (float z = -half + 10; z < half - 5; z += 20) addProp("via_elevada", {kBlock * 0.5f, 0, z}, 0, 1.0f, false);
    // Cierre del mapa: torres altas en todo el borde.
    for (float s = -half; s <= half; s += 14) {
        for (Vec3 p : {Vec3{s, 0, half + 8}, Vec3{s, 0, -half - 8}, Vec3{half + 8, 0, s}, Vec3{-half - 8, 0, s}})
            addProp(towers[rng.pick(4)], p, float(rng.pick(4)) * kPi * 0.5f, 1.0f, false);
    }
    // Habitantes por las aceras y en el santuario.
    for (int i = 0; i < 22; ++i) {
        int bx = rng.pick(2 * kN + 1) - kN, bz = rng.pick(2 * kN + 1) - kN, side = rng.pick(4);
        float yaw = float(side) * kPi * 0.5f;
        Vec3 out{std::sin(yaw), 0, std::cos(yaw)}, along{out.z, 0, -out.x};
        Vec3 p = Vec3{float(bx) * kBlock, 0, float(bz) * kBlock} + out * ((kBlock - kRoad) * 0.5f - 1.5f) + along * rng.range(-12, 12);
        addNpc(i % 3 == 0 ? "chica_neon" : "oficinista", p, 9);
    }
    for (int k = 0; k < 3; ++k) addNpc("cocinero", {-8.0f + float(k) * 8, 0, -(kBlock - kRoad) * 0.5f + 6.5f}, 1.5f);
    addNpc("chica_neon", {3, 0, 6}, 6);
    addNpc("oficinista", {-4, 0, 9}, 6);
    heroStart_ = {-kBlock * 0.5f, 0, -12};
    heroStartYaw_ = 0;
    addPortal({-kBlock * 0.5f + 3, 0, 6});
    addPortal({kBlock * 1.5f, 0, kBlock * 0.5f});
    LevelPart explore{"Ciudad Neón", "Llega al santuario del centro", PartKind::Reach, "", 0, {{-12, -1, -12}, {12, 10, 12}},
                      {0, 2, 0}, {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Neon;
}

// --- Pueblo cafetero ---------------------------------------------------------------------

void Game::buildCafetero() {
    levelName_ = "Pueblo cafetero";
    floorColor_ = rgba(78, 138, 62);
    floorMaterial_ = Material::Grass;
    daylight_ = true;
    fogScale_ = 0.45f;
    viewDistance_ = 380;
    const float kSize = 320;
    makeTerrain(kSize, 2.0f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    Rng rng{57};
    // El pueblo está en lo alto de una loma: la plaza en el centro, calles en cruz y en anillo.
    const float kPlaza = 22;  // medio lado de la plaza
    const float plazaY = 12;
    auto streetAt = [&](Vec3 p) {  // distancia a la calle más cercana
        float d = 1e9f;
        for (float c : {-48.0f, 0.0f, 48.0f}) {
            if (std::fabs(p.z) < 80) d = std::min(d, std::fabs(p.x - c));
            if (std::fabs(p.x) < 80) d = std::min(d, std::fabs(p.z - c));
        }
        return d;
    };
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float r = length(flat(p));
            // Loma: el pueblo arriba, laderas de cafetal y una cañada al este con guaduas.
            float h = plazaY * std::exp(-r * r / (2 * 95.0f * 95.0f)) * 1.25f + 5.0f * (fbm(x / 60.0f, z / 60.0f, 33) + 0.6f);
            h -= 9.0f * std::exp(-((x - 110) * (x - 110)) / (2 * 14.0f * 14.0f)) * smoothFalloff(std::fabs(z), 150);
            float edge = std::min({x - t.x0, -t.x0 - x, z - t.z0, -t.z0 - z});
            if (edge < 40) h += 30.0f * (1 - edge / 40) * (1 - edge / 40);
            // La plaza, llana; las calles del pueblo bajan suaves.
            float town = smoothFalloff(r, 85);
            float townH = plazaY + 1.5f - 0.06f * r;
            h = h + (townH - h) * town;
            float plazaD = std::max(std::fabs(x), std::fabs(z)) - kPlaza;
            h = h + (plazaY - h) * smoothFalloff(std::max(plazaD, 0.0f), 8.0f);
            t.at(ix, iz) = h;
            float v = noise(x / 6.0f, z / 6.0f, 9) * 0.5f + 0.5f;
            uint32_t col = mixRgb(rgba(66, 128, 52), rgba(104, 160, 62), v);  // verde del trópico
            Material m = Material::Grass;
            float cob = noise(x * 1.3f, z * 1.3f, 4) * 0.5f + 0.5f;  // empedrado: manchas de piedra
            if (plazaD < 0) col = mixRgb(rgba(156, 146, 130), rgba(124, 114, 102), cob), m = Material::Stone;  // plaza empedrada
            else if (streetAt(p) < 4.0f && r < 82) col = mixRgb(rgba(134, 122, 106), rgba(106, 98, 86), cob), m = Material::Stone;
            else if (r > 95 && r < 150 && int((x + z) / 3.5f) % 2 == 0) col = mixRgb(col, rgba(150, 82, 52), 0.35f);  // surcos de tierra roja
            paintTerrain(ix, iz, col, m);
        }
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };

    // Plaza: iglesia al norte, kiosco en el centro, bancas, faroles, yipaos y palmas.
    addProp("iglesia", {0, 0, kPlaza + 12}, kPi);
    addProp("kiosco", {0, 0, 0});
    for (int k = 0; k < 8; ++k) {
        float a = float(k) * kPi / 4 + kPi / 8;
        addProp("banca_parque", {std::cos(a) * 9, 0, std::sin(a) * 9}, -a - kPi * 0.5f);
        addProp("heliconia", {std::cos(a) * 12, 0, std::sin(a) * 12}, a);
    }
    for (int k = 0; k < 4; ++k) {
        float a = float(k) * kPi / 2 + kPi / 4;
        addProp("palma_de_cera", {std::cos(a) * 15, 0, std::sin(a) * 15}, a, 0.7f);
    }
    for (float s = -kPlaza + 2; s <= kPlaza - 2; s += 11)
        for (Vec3 p : {Vec3{s, 0, kPlaza - 1}, Vec3{s, 0, -kPlaza + 1}, Vec3{kPlaza - 1, 0, s}, Vec3{-kPlaza + 1, 0, s}})
            addProp("farol_colonial", p);
    addProp("yipao_rojo", {-kPlaza + 4, 0, -10}, 0.2f);
    addProp("yipao_azul", {-kPlaza + 4, 0, -4}, 0.1f);
    addProp("yipao_rojo", {kPlaza - 4, 0, 9}, kPi - 0.15f);
    // Casas de colores a los dos lados de las calles, con la fachada hacia la calle.
    const char* houses[] = {"casa_cafetera_rojo",    "casa_cafetera_azul",     "casa_cafetera_verde",     "casa_cafetera_amarillo",
                            "casa_cafetera_naranja", "casa_cafetera_morado",   "casa_cafetera_turquesa",  "casa_cafetera_baja_verde",
                            "casa_cafetera_baja_rojo", "casa_cafetera_ancha_azul"};
    auto row = [&](Vec3 from, Vec3 dir, float len, float yaw) {
        for (float s = 0; s < len; s += 9.5f) {
            Vec3 p = from + dir * s;
            if (std::max(std::fabs(p.x), std::fabs(p.z)) < kPlaza + 6) continue;  // la plaza queda libre
            addProp(houses[rng.pick(10)], p, yaw);
        }
    };
    // Plaza: casas en tres lados (al norte está la iglesia).
    for (float s = -kPlaza + 2; s < kPlaza; s += 9.5f) {
        addProp(houses[rng.pick(7)], {s, 0, -kPlaza - 8.5f}, 0);
        if (std::fabs(s) > 8) {
            addProp(houses[rng.pick(7)], {kPlaza + 8.5f, 0, s}, -kPi * 0.5f);
            addProp(houses[rng.pick(7)], {-kPlaza - 8.5f, 0, s}, kPi * 0.5f);
        }
    }
    for (float c : {-48.0f, 48.0f}) {  // calles paralelas
        row({-78, 0, c - 8.5f}, {1, 0, 0}, 156, 0);
        row({-78, 0, c + 8.5f}, {1, 0, 0}, 156, kPi);
        row({c - 8.5f, 0, -78}, {0, 0, 1}, 156, kPi * 0.5f);
        row({c + 8.5f, 0, -78}, {0, 0, 1}, 156, -kPi * 0.5f);
    }
    for (Vec3 dir : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, -1}}) {  // calles que salen de la plaza
        Vec3 side{dir.z, 0, -dir.x};
        float yaw = std::atan2(side.x, side.z);
        row(dir * (kPlaza + 14) + side * 8.5f, dir, 48, yaw + kPi);
        row(dir * (kPlaza + 14) - side * 8.5f, dir, 48, yaw);
        for (float s = kPlaza + 10; s < 80; s += 16) addProp("farol_colonial", dir * s + side * 3.8f);
    }
    // Alrededor: cafetal con plataneras de sombrío, guadual en la cañada y palmas de cera en los cerros.
    for (int k = 0; k < 900; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(92, 150);
        Vec3 p{std::cos(a) * r, 0, std::sin(a) * r};
        if (std::fabs(p.x - 110) < 14 && std::fabs(p.z) < 120) continue;
        // En hileras siguiendo la ladera.
        float rr = std::round(r / 2.6f) * 2.6f;
        p = {std::cos(a) * rr, 0, std::sin(a) * rr};
        addProp(rng.next() < 0.88f ? "cafeto" : "platanera", p, rng.range(0, 6.28f), rng.range(0.85f, 1.15f));
    }
    for (int k = 0; k < 40; ++k) addProp("guadual", {rng.range(100, 120), 0, rng.range(-120, 120)}, rng.range(0, 6.28f), rng.range(0.8f, 1.2f));
    for (int k = 0; k < 45; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(152, 175);
        addProp(rng.next() < 0.4f ? "palma_de_cera_alta" : "palma_de_cera", {std::cos(a) * r, 0, std::sin(a) * r}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 60; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(84, 150);
        addProp(rng.next() < 0.5f ? "heliconia" : "platanera", {std::cos(a) * r, 0, std::sin(a) * r}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 1400; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(84, 160);
        addProp("hierba", {std::cos(a) * r, 0, std::sin(a) * r}, rng.range(0, 6.28f), rng.range(0.9f, 1.5f));
    }
    // Habitantes: la plaza y el cafetal.
    for (int i = 0; i < 9; ++i) {
        float a = float(i) * 0.7f, r = 6.0f + float(i % 3) * 4.5f;
        addNpc(i % 3 == 2 ? "recolectora" : i % 4 == 3 ? "abuelo" : "cafetero", {std::cos(a) * r, 0, std::sin(a) * r}, 7);
    }
    for (int i = 0; i < 6; ++i) {
        float a = rng.range(0, 6.28f), r = rng.range(98, 140);
        addNpc(i % 2 ? "recolectora" : "cafetero", {std::cos(a) * r, 0, std::sin(a) * r}, 6);
    }
    for (Vec3 p : {Vec3{0, 0, -40}, Vec3{-40, 0, 0}, Vec3{40, 0, 0}}) addNpc("cafetero", p, 10);
    addPortal({-8, ground(-8, -14), -14});
    addPortal({60, ground(60, 0), 0});
    heroStart_ = {0, ground(0, -12), -12};
    heroStartYaw_ = 0;
    LevelPart explore{"Pueblo cafetero", "Baja al guadual de la cañada", PartKind::Reach, "", 0, {{98, -20, -20}, {122, 30, 20}},
                      {110, ground(110, 0) + 2, 0}, {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Cafetero;
}

// Sombra de día: algo tapa el sol (rayo hacia el sol contra las cajas del mundo y el relieve).
bool Game::sunBlocked(Vec3 p) const {
    RayHit h;
    return segmentVsWorld(p + Vec3{0, 0.3f, 0}, p + Vec3{0, 0.3f, 0} + kSun * 40.0f, world_, h);
}
