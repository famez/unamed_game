// Niveles con noche y oscuridad de verdad, para jugar con la luz y la sombra: cada uno con sus
// propias fuentes de luz (antorchas, cristales, farolillos de papel, letreros de neón), que se
// pueden usar para viajar como luz o romper para crear sombra.
//   - Selva de los Puentes: plataformas en lo alto de árboles gigantes unidas por puentes
//     colgantes sobre un barranco, alumbradas con antorchas.
//   - Cueva de Cristal: salas de roca unidas por túneles, con racimos de cristal que brillan y
//     un río subterráneo.
//   - Callejones del Farol: calles estrechas de un barrio viejo con farolillos de papel
//     colgados de lado a lado y letreros de neón en las fachadas.

#include "game/game.h"

#include <algorithm>

namespace {

const uint32_t kBark = rgba(70, 52, 40), kBarkDark = rgba(48, 36, 30), kPlank = rgba(120, 92, 62), kRope = rgba(150, 128, 92);
const uint32_t kLeaf = rgba(38, 74, 52), kLeafDark = rgba(26, 52, 40), kFlame = rgba(255, 170, 70);
const uint32_t kRock = rgba(74, 70, 82), kRockDark = rgba(50, 48, 58);
const uint32_t kCrystalCyan = rgba(90, 220, 255), kCrystalViolet = rgba(190, 120, 255), kCrystalGreen = rgba(120, 255, 170);
const uint32_t kWall = rgba(120, 108, 104), kWallDark = rgba(92, 84, 86), kWindow = rgba(170, 120, 60);
const uint32_t kPaperRed = rgba(255, 90, 70), kPaperAmber = rgba(255, 170, 70), kNeonPink = rgba(255, 80, 190),
               kNeonCyan = rgba(60, 220, 255);


}  // namespace

// --- Fuentes de luz de cada estilo -----------------------------------------------------------

// Antorcha: un palo clavado en el suelo con la llama arriba (alumbra algo menos que una farola).
void Game::addTorch(Vec3 base) {
    addBlock(base + Vec3{0, 0.85f, 0}, {0.06f, 0.85f, 0.06f}, kBarkDark, Material::Cloth);
    Lamp l{base + Vec3{0, 1.95f, 0}, kFlame};
    l.style = LampStyle::Torch;
    l.reach = 0.8f;
    lamps_.push_back(l);
}

// Racimo de cristales en el suelo de la cueva.
void Game::addCrystal(Vec3 base, uint32_t color) {
    addDecor(Mat4::identity(), base + Vec3{0, 0.15f, 0}, {0.4f, 0.18f, 0.4f}, kRockDark, Material::Stone, true);
    Lamp l{base + Vec3{0, 0.8f, 0}, color};
    l.style = LampStyle::Crystal;
    l.reach = 0.85f;
    lamps_.push_back(l);
}

// Farolillo de papel colgado de un cable entre dos fachadas (de `a` a `b`, a la altura `y`).
void Game::addPaperLantern(Vec3 a, Vec3 b, float y, uint32_t color) {
    Vec3 mid = (a + b) * 0.5f;
    Vec3 d = b - a;
    addDecor(translate({mid.x, y + 0.45f, mid.z}) * rotateY(std::atan2(d.x, d.z)), {}, {0.012f, 0.012f, length(d) * 0.5f}, rgba(30, 28, 30),
             Material::Metal, false);
    Lamp l{{mid.x, y, mid.z}, color};
    l.style = LampStyle::Paper;
    l.reach = 0.75f;
    lamps_.push_back(l);
}

// Letrero de neón en una pared, mirando hacia `out`.
void Game::addNeonSign(Vec3 pos, Vec3 out, uint32_t color) {
    Lamp l{pos + out * 0.08f, color};
    l.style = LampStyle::Neon;
    l.reach = 0.7f;
    lamps_.push_back(l);
}

// --- Selva de los Puentes ------------------------------------------------------------------------

void Game::buildSelva() {
    levelName_ = "Selva de los Puentes";
    world_.ground = false;
    night_ = 1.0f;
    const Mat4 id = Mat4::identity();
    // Plataforma en lo alto de un árbol: tablones, borde de cuerda y el tronco que baja al barranco.
    auto platform = [&](Vec3 c, float hx, float hz) {
        addBlock(c - Vec3{0, 0.2f, 0}, {hx, 0.2f, hz}, kPlank, Material::Cloth);
        for (float x = -hx + 0.35f; x < hx; x += 0.7f)  // juntas de los tablones
            addDecor(id, c + Vec3{x, 0.005f, 0}, {0.015f, 0.01f, hz * 0.98f}, kBarkDark, Material::Cloth, false);
        addBlock(c - Vec3{0, 8.0f, 0}, {std::min(hx, hz) * 0.35f, 7.6f, std::min(hx, hz) * 0.35f}, kBark, Material::Cloth);
        for (float sx : {-1.0f, 1.0f})
            for (float sz : {-1.0f, 1.0f})
                addDecor(id, c + Vec3{sx * (hx - 0.1f), 0.5f, sz * (hz - 0.1f)}, {0.07f, 0.5f, 0.07f}, kBarkDark, Material::Cloth, false);
    };
    // Puente colgante: tablones sobre el vacío y dos cuerdas a la altura de la mano.
    auto bridge = [&](Vec3 a, Vec3 b) {
        Vec3 d = b - a;
        float len = length(flat(d));
        Vec3 dir = flat(d) * (1.0f / len), side{-dir.z, 0, dir.x};
        bool alongX = std::fabs(dir.x) > 0.5f;
        Vec3 c = (a + b) * 0.5f;
        addBlock(c - Vec3{0, 0.1f, 0}, alongX ? Vec3{len * 0.5f, 0.1f, 0.75f} : Vec3{0.75f, 0.1f, len * 0.5f}, kPlank, Material::Cloth);
        for (float t = 0.3f; t < len; t += 0.55f)
            addDecor(id, a + dir * t + Vec3{0, 0.01f, 0}, alongX ? Vec3{0.02f, 0.012f, 0.72f} : Vec3{0.72f, 0.012f, 0.02f}, kBarkDark,
                     Material::Cloth, false);
        for (float s : {-0.8f, 0.8f}) {
            Mat4 rope = translate(c + side * s + Vec3{0, 0.95f, 0}) * rotateY(std::atan2(dir.x, dir.z));
            addDecor(rope, {}, {0.025f, 0.025f, len * 0.5f}, kRope, Material::Cloth, false);
            for (float t = 0; t <= len; t += 2.0f)
                addDecor(id, a + dir * t + side * s + Vec3{0, 0.5f, 0}, {0.02f, 0.5f, 0.02f}, kRope, Material::Cloth, false);
        }
    };
    // Copa enorme muy por encima de las plataformas (tapa el cielo a trozos) y lianas.
    auto canopy = [&](Vec3 c, float s) {
        addDecor(id, c, Vec3{3.2f, 1.6f, 3.2f} * s, kLeafDark, Material::Cloth, true);
        addDecor(id, c + Vec3{1.4f, 0.7f, 0.6f} * s, Vec3{2.2f, 1.2f, 2.2f} * s, kLeaf, Material::Cloth, true);
        for (int i = 0; i < 4; ++i) {
            float a = float(i) * 1.6f;
            Vec3 v = c + Vec3{std::cos(a) * 2.0f, -2.0f, std::sin(a) * 2.0f} * s;
            addDecor(id, v, {0.03f, 1.8f, 0.03f}, kLeaf, Material::Cloth, false);
        }
    };

    platform({0, 0, 0}, 5, 5);           // llegada
    bridge({0, 0, 5}, {0, 0, 14});
    platform({0, 0.2f, 19}, 6, 5);       // el claro
    bridge({6, 0.3f, 19}, {16, 0.3f, 19});
    platform({22, 0.4f, 19}, 6, 6);      // el poblado
    bridge({22, 0.5f, 25}, {22, 0.5f, 35});
    platform({22, 0.6f, 42}, 8, 7);      // el altar
    platform({8, 0.4f, 34}, 3, 3);       // plataforma suelta: solo se llega viajando por la luz
    // Chozas del poblado.
    for (float sx : {-1.0f, 1.0f}) {
        addBlock({22 + sx * 3.8f, 1.6f, 22.5f}, {1.3f, 1.2f, 1.3f}, kBark, Material::Cloth);
        addDecor(id, {22 + sx * 3.8f, 3.1f, 22.5f}, {1.8f, 0.45f, 1.8f}, kLeafDark, Material::Cloth, true);
    }
    // Ruinas del altar.
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f}) addBlock({22 + sx * 5.5f, 1.8f, 42 + sz * 4.5f}, {0.5f, 1.2f, 0.5f}, kRock, Material::Stone);
    addBlock({22, 1.0f, 47.5f}, {2.5f, 0.4f, 1.0f}, kRock, Material::Stone);

    for (Vec3 c : {Vec3{0, 9, 2}, Vec3{-3, 10, 20}, Vec3{25, 10, 17}, Vec3{18, 11, 44}, Vec3{28, 10, 46}, Vec3{10, 9, 30}})
        canopy(c, 1.0f + 0.1f * std::fabs(c.x) / 20.0f);
    // Allá abajo, el río del barranco (solo se ve) y luciérnagas.
    addDecor(id, {12, -14, 25}, {40, 0.1f, 45}, rgba(20, 40, 60), Material::Water, false);
    for (int i = 0; i < 40; ++i) {
        float a = float(i) * 2.399f, r = 6.0f + float(i % 9) * 3.0f;
        addDecor(id, {12 + std::cos(a) * r, 1.5f + float(i % 5) * 0.8f, 22 + std::sin(a) * r}, {0.04f, 0.04f, 0.04f},
                 rgba(200, 255, 140), Material::Glow, true);
    }
    // Antorchas: pocas, para que entre ellas quede oscuridad de sobra.
    addTorch({-4.2f, 0, -4.2f});
    addTorch({4.2f, 0, 4.2f});
    addTorch({-5.2f, 0.2f, 15});
    addTorch({5.2f, 0.2f, 23});
    addTorch({17, 0.4f, 14});
    addTorch({27, 0.4f, 24});
    addTorch({8, 0.4f, 34});  // la de la plataforma suelta
    for (float sx : {-1.0f, 1.0f}) addTorch({22 + sx * 7.2f, 0.6f, 36});
    addTorch({22, 1.4f, 47.5f});
    portals_.push_back({{-2.5f, 0, 2.5f}});

    encounters_ = {
        {"selva_claro", AABB{{-6, -1, 14}, {6, 5, 24}},
         {{{EnemyKind::Hound, {-3, 0.2f, 22}}, {EnemyKind::Hound, {3, 0.2f, 22}}},
          {{EnemyKind::Dazzled, {0, 0.2f, 23}}, {EnemyKind::Hound, {-4, 0.2f, 16}}, {EnemyKind::Hound, {4, 0.2f, 16}}}},
         {}, "", ""},
        {"selva_poblado", AABB{{16, -1, 13}, {28, 5, 25}},
         {{{EnemyKind::Dazzled, {20, 0.4f, 16}}, {EnemyKind::Watcher, {24, 0.4f, 22}}, {EnemyKind::Hound, {25, 0.4f, 15}}},
          {{EnemyKind::Brute, {22, 0.4f, 20}}, {EnemyKind::Hound, {18, 0.4f, 22}}, {EnemyKind::Hound, {26, 0.4f, 22}}}},
         {}, "", ""},
        {"selva_altar", AABB{{14, -1, 35}, {30, 6, 49}},
         {{{EnemyKind::Hound, {18, 0.6f, 44}}, {EnemyKind::Hound, {26, 0.6f, 44}}, {EnemyKind::Watcher, {22, 0.6f, 46}}},
          {{EnemyKind::Hound, {22, 0.6f, 44}, true, "La Bestia del Barranco"}}},
         {}, "", ""},
    };
    LevelPart p1;
    p1.title = "El claro";
    p1.objective = "Cruza el puente y limpia el claro";
    p1.kind = PartKind::Encounter;
    p1.target = "selva_claro";
    p1.marker = {0, 1.5f, 19};
    LevelPart p2 = p1;
    p2.title = "El poblado";
    p2.objective = "Libera el poblado de los árboles";
    p2.target = "selva_poblado";
    p2.marker = {22, 1.5f, 19};
    LevelPart p3 = p1;
    p3.title = "El altar";
    p3.objective = "Vence a la Bestia del Barranco";
    p3.target = "selva_altar";
    p3.marker = {22, 1.8f, 42};
    parts_ = {p1, p2, p3};
    heroStart_ = {0, 0, -2};
    heroStartYaw_ = 0;
}

// --- Cueva de Cristal ----------------------------------------------------------------------------

void Game::buildCueva() {
    levelName_ = "Cueva de Cristal";
    night_ = 1.0f;
    floorColor_ = rgba(54, 50, 60);
    floorMaterial_ = Material::Ground;
    const Mat4 id = Mat4::identity();
    // Pared de roca de `a` a `b` (en el suelo): bloques altos irregulares y rocas redondas encima.
    auto wall = [&](Vec3 a, Vec3 b) {
        Vec3 d = b - a;
        float len = length(d);
        Vec3 dir = d * (1.0f / len);
        int n = std::max(1, int(len / 2.5f));
        for (int i = 0; i < n; ++i) {
            Vec3 c = a + dir * ((float(i) + 0.5f) * len / float(n));
            float h = 5.0f + 2.5f * std::fabs(std::sin(c.x * 1.7f + c.z * 0.9f));
            float w = len / float(n) * 0.5f + 0.5f;
            addBlock(c + Vec3{0, h * 0.5f, 0}, {w, h * 0.5f, w}, (i % 2) ? kRock : kRockDark, Material::Stone);
            addDecor(id, c + Vec3{0, h, 0}, Vec3{w * 1.1f, 0.8f, w * 1.1f}, kRockDark, Material::Stone, true);
            addDecor(id, c + Vec3{0, h + 0.6f, 0} + Vec3{0.3f, 0, 0.2f}, {0.15f, 0.9f, 0.15f}, kRock, Material::Stone, false);  // estalagmita
        }
    };
    // Salas: la de entrada (A), la del río (B) y la gran gruta (C), unidas por túneles.
    wall({-8, 0, -6}, {8, 0, -6});
    wall({-8, 0, -6}, {-8, 0, 8});
    wall({8, 0, -6}, {8, 0, 8});
    wall({-8, 0, 8}, {-3.9f, 0, 8});
    wall({3.9f, 0, 8}, {8, 0, 8});
    wall({-3.9f, 0, 8}, {-3.9f, 0, 16});  // túnel A-B
    wall({3.9f, 0, 8}, {3.9f, 0, 16});
    wall({-12, 0, 16}, {-3.9f, 0, 16});
    wall({3.9f, 0, 16}, {12, 0, 16});
    wall({-12, 0, 16}, {-12, 0, 32});
    wall({12, 0, 16}, {12, 0, 32});
    wall({-12, 0, 32}, {-3.9f, 0, 32});
    wall({3.9f, 0, 32}, {12, 0, 32});
    wall({-3.9f, 0, 32}, {-3.9f, 0, 40});  // túnel B-C
    wall({3.9f, 0, 32}, {3.9f, 0, 40});
    wall({-15, 0, 40}, {-3.9f, 0, 40});
    wall({3.9f, 0, 40}, {15, 0, 40});
    wall({-15, 0, 40}, {-15, 0, 62});
    wall({15, 0, 40}, {15, 0, 62});
    wall({-15, 0, 62}, {15, 0, 62});
    // El río subterráneo cruza la sala B (con dos losas para pasar sin mojarse).
    addDecor(id, {0, 0.02f, 24}, {11.8f, 0.02f, 1.6f}, rgba(30, 70, 110), Material::Water, false);
    for (float x : {-5.0f, 5.0f}) addBlock({x, 0.15f, 24}, {1.0f, 0.15f, 1.8f}, kRock, Material::Stone);
    // Columnas de la gran gruta.
    for (float sx : {-1.0f, 1.0f})
        for (float z : {46.0f, 54.0f}) {
            addBlock({sx * 7, 4, z}, {0.9f, 4, 0.9f}, kRock, Material::Stone);
            addDecor(id, {sx * 7, 8.2f, z}, {1.6f, 0.8f, 1.6f}, kRockDark, Material::Stone, true);
        }
    // Setas que brillan un poco (adorno, no alumbran).
    for (int i = 0; i < 24; ++i) {
        float a = float(i) * 2.4f;
        Vec3 p{std::cos(a) * (4 + float(i % 5)), 0, 24 + std::sin(a) * 5 + float(i % 3) * 12 - 12};
        addDecor(id, p + Vec3{0, 0.12f, 0}, {0.03f, 0.12f, 0.03f}, rgba(200, 200, 190), Material::Cloth, false);
        addDecor(id, p + Vec3{0, 0.26f, 0}, {0.11f, 0.05f, 0.11f}, i % 2 ? kCrystalGreen : kCrystalCyan, Material::Glow, true);
    }
    // Cristales: la única luz de la cueva.
    addCrystal({-5, 0, 2}, kCrystalCyan);
    addCrystal({5, 0, -2}, kCrystalViolet);
    addCrystal({-9, 0, 20}, kCrystalGreen);
    addCrystal({9, 0, 29}, kCrystalCyan);
    addCrystal({0, 0, 36}, kCrystalViolet);
    addCrystal({-11, 0, 44}, kCrystalCyan);
    addCrystal({11, 0, 58}, kCrystalGreen);
    addCrystal({-7, 0, 50}, kCrystalViolet);
    addCrystal({7, 0, 50}, kCrystalViolet);
    portals_.push_back({{-5, 0, 5}});

    encounters_ = {
        {"cueva_rio", AABB{{-12, -1, 16}, {12, 6, 32}},
         {{{EnemyKind::Dazzled, {-6, 0, 28}}, {EnemyKind::Dazzled, {6, 0, 28}}, {EnemyKind::Watcher, {0, 0, 30}}},
          {{EnemyKind::Hound, {-8, 0, 20}}, {EnemyKind::Hound, {8, 0, 20}}, {EnemyKind::Brute, {0, 0, 29}}}},
         {}, "", ""},
        {"cueva_gruta", AABB{{-15, -1, 40}, {15, 8, 62}},
         {{{EnemyKind::Watcher, {-8, 0, 56}}, {EnemyKind::Watcher, {8, 0, 56}}, {EnemyKind::Hound, {0, 0, 50}}},
          {{EnemyKind::Brute, {0, 0, 56}, true, "El Guardián de la Gruta"}, {EnemyKind::Dazzled, {-6, 0, 46}}}},
         {}, "", ""},
    };
    LevelPart p1;
    p1.title = "El río subterráneo";
    p1.objective = "Abre paso junto al río";
    p1.kind = PartKind::Encounter;
    p1.target = "cueva_rio";
    p1.marker = {0, 1.5f, 24};
    LevelPart p2 = p1;
    p2.title = "La gran gruta";
    p2.objective = "Vence al Guardián de la Gruta";
    p2.target = "cueva_gruta";
    p2.marker = {0, 1.8f, 52};
    parts_ = {p1, p2};
    heroStart_ = {0, 0, 0};
    heroStartYaw_ = 0;
}

// --- Callejones del Farol --------------------------------------------------------------------------

void Game::buildCallejones() {
    levelName_ = "Callejones del Farol";
    night_ = 0.85f;
    floorColor_ = rgba(84, 78, 84);
    floorMaterial_ = Material::Ground;
    const Mat4 id = Mat4::identity();
    // Casa: bloque alto con ventanas (algunas encendidas) que miran a la calle (`in`).
    auto house = [&](float cx, float cz, float hx, float hz, float h, Vec3 in) {
        uint32_t tint = int(std::fabs(cx * 7 + cz * 3)) % 2 ? kWall : kWallDark;
        addBlock({cx, h * 0.5f, cz}, {hx, h * 0.5f, hz}, tint, Material::Stone);
        addDecor(id, {cx, h + 0.1f, cz}, {hx + 0.12f, 0.1f, hz + 0.12f}, rgba(70, 60, 60), Material::Stone, false);
        bool facesZ = std::fabs(in.z) > 0.5f;
        float half = facesZ ? hx : hz;
        Vec3 along = facesZ ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        Vec3 face = Vec3{cx, 0, cz} + in * ((facesZ ? hz : hx) + 0.02f);
        for (float t = -half + 1.0f; t < half - 0.5f; t += 1.6f)
            for (float y = 1.6f; y < h - 0.8f; y += 2.2f) {
                bool on = int(std::fabs(t * 5 + y * 3 + cx + cz)) % 3 == 0;
                Vec3 w = face + along * t + Vec3{0, y, 0};
                addDecor(id, w, facesZ ? Vec3{0.35f, 0.5f, 0.03f} : Vec3{0.03f, 0.5f, 0.35f}, on ? kWindow : rgba(30, 30, 40),
                         on ? Material::Glow : Material::Metal, false);
            }
    };
    // Calle principal (x ∈ [-2, 2]) de z = 0 a 40, un callejón a cada lado y la plazuela del final.
    for (float z : {2.0f, 9.0f, 21.0f, 29.0f}) {
        house(-5, z, 3, z == 9.0f || z == 29.0f ? 3.0f : 2.8f, 7 + std::fmod(z, 3.0f), {1, 0, 0});
        house(5, z, 3, 2.8f, 6.5f + std::fmod(z * 1.3f, 2.5f), {-1, 0, 0});
    }
    house(-5, 36, 3, 2.5f, 8, {1, 0, 0});
    house(5, 36, 3, 2.5f, 6, {-1, 0, 0});
    // Callejones (z = 15 al este, z = 33 al oeste... más estrechos todavía).
    house(12, 11, 4, 2.5f, 7, {0, 0, 1});
    house(12, 19, 4, 2.5f, 6, {0, 0, -1});
    house(-12, 22, 4, 2.5f, 6, {0, 0, -1});
    house(-12, 15.5f, 4, 2.2f, 7.5f, {0, 0, 1});
    addBlock({18, 3, 15}, {1, 3, 2}, kWallDark, Material::Stone);  // fondo de los callejones
    addBlock({-18, 3, 18.7f}, {1, 3, 2}, kWallDark, Material::Stone);
    // La plazuela del final, cerrada por casas.
    house(0, 54, 8, 2, 7, {0, 0, -1});
    house(-10, 46, 2, 6, 6, {1, 0, 0});
    house(10, 46, 2, 6, 6.5f, {-1, 0, 0});
    addBlock({0, 0.6f, 47}, {1.2f, 0.6f, 1.2f}, kWallDark, Material::Stone);  // fuente seca
    addDecor(id, {0, 1.25f, 47}, {1.0f, 0.05f, 1.0f}, rgba(40, 70, 100), Material::Water, false);
    addBlock({0, 4, -3}, {8, 4, 0.5f}, kWallDark, Material::Stone);  // el muro de detrás de la salida
    // Farolillos de papel cruzando la calle, letreros en las fachadas.
    for (float z : {5.0f, 13.0f, 25.0f, 33.0f})
        addPaperLantern({-2, 0, z}, {2, 0, z}, 3.4f, int(z) % 2 ? kPaperRed : kPaperAmber);
    addPaperLantern({8, 0, 13.7f}, {8, 0, 16.3f}, 3.2f, kPaperRed);
    addPaperLantern({-8, 0, 17.5f}, {-8, 0, 19.5f}, 3.2f, kPaperAmber);
    addPaperLantern({-5, 0, 44}, {5, 0, 44}, 3.6f, kPaperRed);
    addNeonSign({-2.02f, 3.0f, 9}, {1, 0, 0}, kNeonPink);
    addNeonSign({2.02f, 2.8f, 21}, {-1, 0, 0}, kNeonCyan);
    addNeonSign({0, 3.2f, 51.98f}, {0, 0, -1}, kNeonPink);
    portals_.push_back({{0, 0, 39}});

    encounters_ = {
        {"callejon_cruce", AABB{{-2, -1, 12}, {2, 6, 22}},
         {{{EnemyKind::Dazzled, {0, 0, 20}}, {EnemyKind::Hound, {10, 0, 15}}, {EnemyKind::Hound, {-10, 0, 18.7f}}},
          {{EnemyKind::Watcher, {0, 0, 22}}, {EnemyKind::Dazzled, {0, 0, 12}}}},
         {}, "", ""},
        {"callejon_plazuela", AABB{{-8, -1, 41}, {8, 6, 52}},
         {{{EnemyKind::Dazzled, {-5, 0, 49}}, {EnemyKind::Dazzled, {5, 0, 49}}, {EnemyKind::Watcher, {0, 0, 51}}},
          {{EnemyKind::Dazzled, {0, 0, 50}, true, "El Farolero"}, {EnemyKind::Hound, {-6, 0, 43}}, {EnemyKind::Hound, {6, 0, 43}}}},
         {}, "", ""},
    };
    LevelPart p1;
    p1.title = "El cruce";
    p1.objective = "Despeja el cruce de los callejones";
    p1.kind = PartKind::Encounter;
    p1.target = "callejon_cruce";
    p1.marker = {0, 1.5f, 17};
    LevelPart p2 = p1;
    p2.title = "La plazuela";
    p2.objective = "Vence al Farolero en la plazuela";
    p2.target = "callejon_plazuela";
    p2.marker = {0, 1.8f, 47};
    parts_ = {p1, p2};
    heroStart_ = {0, 0, 2};
    heroStartYaw_ = 0;
}
